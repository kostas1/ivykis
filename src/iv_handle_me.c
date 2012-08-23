/*
 * ivykis, an event handling library
 * Copyright (C) 2012 Lennert Buytenhek
 * Dedicated to Marija Kulikova.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version
 * 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License version 2.1 for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License version 2.1 along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include "iv_private.h"
#include "iv_handle_private.h"

/* handle group tls handling ************************************************/
static DWORD iv_handle_me_group_index = -1;

static inline struct iv_handle_group *iv_handle_me_get_group(void)
{
	return TlsGetValue(iv_handle_me_group_index);
}


/* poll handling ************************************************************/
static void __iv_handle_me_poll_state_update(struct iv_handle_group *grp)
{
	int i;
	int j;

	for (i = grp->u.me.num_handles; i < grp->u.me.addition_pointer; i++)
		grp->u.me.hnd[i] = grp->u.me.h[i]->handle;

	for (i = 0, j = 0; i < grp->u.me.addition_pointer; i++) {
		if (grp->u.me.h[i] != NULL) {
			if (j != i) {
				grp->u.me.h[j] = grp->u.me.h[i];
				grp->u.me.h[j]->u.me.index = j;
				grp->u.me.hnd[j] = grp->u.me.hnd[i];
			}
			j++;
		}
	}

	grp->u.me.addition_pointer = j;
	grp->u.me.num_deletions = 0;

	grp->u.me.quit = grp->u.me.st->quit;
	grp->u.me.num_handles = j;
	grp->u.me.hnd[j] = grp->u.me.thr_signal_handle;
	grp->u.me.have_active_handles =
		!iv_list_empty(&grp->u.me.active_handles);
}

static int
__iv_handle_me_ready(struct iv_handle_group *grp, int off, DWORD ret)
{
	int index;

	if (ret >= WAIT_ABANDONED_0)
		index = off + (ret - WAIT_ABANDONED_0);
	else
		index = off + (ret - WAIT_OBJECT_0);

	if (index < grp->u.me.num_handles) {
		struct iv_handle_ *h;

		h = grp->u.me.h[index];
		if (h != NULL) {
			if (iv_list_empty(&h->list_active)) {
				iv_list_add_tail(&h->list_active,
						 &grp->u.me.active_handles);
			}
			grp->u.me.have_active_handles = 1;
		}
	} else {
		__iv_handle_me_poll_state_update(grp);
	}

	return index + 1;
}

static void iv_handle_me_poll_handles(struct iv_handle_group *grp, DWORD msec)
{
	int num_handles;
	HANDLE *hnd;
	int off;
	DWORD ret;

	num_handles = grp->u.me.num_handles + 1;
	hnd = grp->u.me.hnd;

	if (!grp->u.me.quit) {
		if (grp->u.me.have_active_handles)
			msec = 0;
		off = 0;
	} else {
		off = grp->u.me.num_handles;
	}

	ret = WaitForMultipleObjectsEx(num_handles - off,
				       hnd + off, FALSE, msec, TRUE);
	if (ret == WAIT_FAILED)
		iv_fatal("iv_handle_me_poll_handles: wait failed");

	if (ret == WAIT_IO_COMPLETION || ret == WAIT_TIMEOUT)
		return;

	EnterCriticalSection(&grp->u.me.group_lock);

	off = __iv_handle_me_ready(grp, off, ret);
	while (off < num_handles) {
		ret = WaitForMultipleObjectsEx(num_handles - off,
					       hnd + off, FALSE, 0, FALSE);
		if (ret == WAIT_FAILED)
			iv_fatal("iv_handle_me_poll_handles: wait failed");

		if (ret == WAIT_TIMEOUT)
			break;

		off = __iv_handle_me_ready(grp, off, ret);
	}

	LeaveCriticalSection(&grp->u.me.group_lock);
}

static void __iv_handle_me_run_active_list(struct iv_handle_group *grp)
{
	struct iv_list_head active;

	__iv_list_steal_elements(&grp->u.me.active_handles, &active);
	grp->u.me.have_active_handles = 0;

	while (!iv_list_empty(&active)) {
		struct iv_handle_ *h;

		h = iv_container_of(active.next, struct iv_handle_,
				    list_active);
		iv_list_del_init(&h->list_active);

		h->handler(h->cookie);
	}
}


/* poll thread **************************************************************/
static int timespec_lt(struct timespec *a, struct timespec *b)
{
	if (a->tv_sec < b->tv_sec)
		return 1;
	if (a->tv_sec == b->tv_sec && a->tv_nsec < b->tv_nsec)
		return 1;

	return 0;
}

static int should_wake_zero(struct iv_state *st, struct iv_handle_group *grp,
			    int ts_expired, struct timespec *ts)
{
	if (!st->numobjs)
		return 1;

	if (!ts_expired) {
		int ts2_expired;
		struct timespec ts2;

		ts2_expired = iv_get_soonest_timeout(st, &ts2);
		if (ts2_expired || timespec_lt(&ts2, ts))
			return 1;
	}

	// @@@
	if (iv_pending_tasks(st))
		return 1;

	return 0;
}

static DWORD WINAPI iv_handle_me_poll_thread(void *_grp)
{
	struct iv_handle_group *grp = _grp;
	struct iv_state *st = grp->u.me.st;

	TlsSetValue(iv_state_index, grp->u.me.st);
	TlsSetValue(iv_handle_me_group_index, grp);

	while (grp->u.me.num_handles) {
		HANDLE h[2];
		DWORD ret;

		iv_handle_me_poll_handles(grp, INFINITE);

		if (grp->u.me.quit || !grp->u.me.have_active_handles)
			continue;

		h[0] = st->u.me.execution_mutex;
		h[1] = grp->u.me.thr_signal_handle;

		do {
			ret = WaitForMultipleObjectsEx(2, h, FALSE,
						       INFINITE, TRUE);
		} while (ret == WAIT_IO_COMPLETION);

		if (ret == WAIT_OBJECT_0 + 1) {
			EnterCriticalSection(&grp->u.me.group_lock);
			__iv_handle_me_poll_state_update(grp);
			LeaveCriticalSection(&grp->u.me.group_lock);
			continue;
		}

		if (ret != WAIT_OBJECT_0) {
			iv_fatal("iv_handle_me_poll_thread: got "
				 "error %x", (int)ret);
		}

		// @@@ condition?
		if (!grp->u.me.quit) {
			int ts_expired;
			struct timespec ts;

			ts_expired = iv_get_soonest_timeout(st, &ts);

			// @@@ determine who runs what!
			__iv_invalidate_now(grp->u.me.st);
			iv_run_tasks(st);
			iv_run_timers(st);
			__iv_handle_me_run_active_list(grp);

			if (should_wake_zero(st, grp, ts_expired, &ts)) {
				struct iv_handle_group *grp;

				grp = &st->u.me.group_0;
				SetEvent(grp->u.me.thr_signal_handle);
			}
		}

		ReleaseMutex(st->u.me.execution_mutex);
	}

	CloseHandle(grp->u.me.thr_signal_handle);
	DeleteCriticalSection(&grp->u.me.group_lock);
	free(grp);

	return 0;
}


/* internal use *************************************************************/
static void iv_handle_me_init(struct iv_state *st)
{
	struct iv_handle_group *me;

	if (iv_handle_me_group_index == -1) {
		iv_handle_me_group_index = TlsAlloc();
		if (iv_handle_me_group_index == TLS_OUT_OF_INDEXES) {
			iv_fatal("iv_handle_me_init: failed to "
				 "allocate TLS key");
		}
	}


	st->u.me.execution_mutex = CreateMutex(NULL, TRUE, NULL);
	INIT_IV_LIST_HEAD(&st->u.me.active_handles_no_handler);
	INIT_IV_LIST_HEAD(&st->u.me.groups);
	INIT_IV_LIST_HEAD(&st->u.me.groups_recent_deleted);

	me = &st->u.me.group_0;
	TlsSetValue(iv_handle_me_group_index, me);

	iv_list_add_tail(&me->u.me.list_all, &st->u.me.groups);
	iv_list_add(&me->u.me.list_recent_deleted,
		    &st->u.me.groups_recent_deleted);
	me->u.me.st = st;
	me->u.me.thr_handle = INVALID_HANDLE_VALUE;
	me->u.me.thr_signal_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&me->u.me.group_lock);
	INIT_IV_LIST_HEAD(&me->u.me.active_handles);
	me->u.me.addition_pointer = 0;
	me->u.me.num_deletions = 0;
	me->u.me.num_handles = 0;
	me->u.me.hnd[0] = me->u.me.thr_signal_handle;
	me->u.me.have_active_handles = 0;
}

static void iv_handle_me_deinit(struct iv_state *st)
{
	struct iv_handle_group *me = iv_handle_me_get_group();

	if (me != &st->u.me.group_0)
		iv_fatal("iv_handle_me_deinit: not called in subthread zero");

	iv_list_del(&me->u.me.list_all);

	while (!iv_list_empty(&st->u.me.groups)) {
		struct iv_list_head *lh;
		struct iv_handle_group *grp;
		int i;
		HANDLE signal;
		HANDLE wait;
		DWORD ret;

		lh = st->u.me.groups.next;
		grp = iv_container_of(lh, struct iv_handle_group,
				      u.me.list_all);

		EnterCriticalSection(&grp->u.me.group_lock);

		iv_list_del(&grp->u.me.list_all);
		iv_list_del(&grp->u.me.list_recent_deleted);

		for (i = 0; i < grp->u.me.addition_pointer; i++) {
			if (grp->u.me.h[i] != NULL) {
				iv_list_del_init(&grp->u.me.h[i]->list_active);
				grp->u.me.h[i] = NULL;
			}
		}

		grp->u.me.addition_pointer = grp->u.me.num_handles;
		grp->u.me.num_deletions = grp->u.me.addition_pointer;

		signal = grp->u.me.thr_signal_handle;
		wait = grp->u.me.thr_handle;

		LeaveCriticalSection(&grp->u.me.group_lock);

		ret = SignalObjectAndWait(signal, wait, INFINITE, TRUE);
		while (ret == WAIT_IO_COMPLETION)
			ret = WaitForSingleObjectEx(wait, INFINITE, TRUE);

		if (ret == WAIT_FAILED)
			iv_fatal("iv_handle_me_deinit: wait failed");

		CloseHandle(wait);
	}

	CloseHandle(st->u.me.execution_mutex);

	CloseHandle(me->u.me.thr_signal_handle);
	DeleteCriticalSection(&me->u.me.group_lock);
}

static int iv_handle_me_is_primary_thread(struct iv_state *st)
{
	struct iv_handle_group *me = iv_handle_me_get_group();

	return (me == &st->u.me.group_0);
}

static void iv_handle_me_poll_and_run(struct iv_state *st, struct timespec *to)
{
	struct iv_handle_group *me = &st->u.me.group_0;
	int msec;
	DWORD ret;

	ReleaseMutex(st->u.me.execution_mutex);

	msec = 1000 * to->tv_sec + (to->tv_nsec + 999999) / 1000000;
	iv_handle_me_poll_handles(me, msec);

	do {
		ret = WaitForSingleObjectEx(st->u.me.execution_mutex,
					    INFINITE, TRUE);
	} while (ret == WAIT_IO_COMPLETION);

	if (ret == WAIT_FAILED)
		iv_fatal("iv_handle_me_poll_and_run: wait failed");

	__iv_invalidate_now(st);
	__iv_handle_me_run_active_list(me);
}

static void iv_handle_me_bump_other_threads(struct iv_state *st)
{
	struct iv_handle_group *me = iv_handle_me_get_group();
	struct iv_list_head *lh;

	iv_list_for_each (lh, &st->u.me.groups) {
		struct iv_handle_group *grp;

		grp = iv_container_of(lh, struct iv_handle_group,
				      u.me.list_all);
		if (me != grp)
			SetEvent(grp->u.me.thr_signal_handle);
	}
}

static void iv_handle_me_quit(struct iv_state *st)
{
	iv_handle_me_bump_other_threads(st);
}

static void iv_handle_me_unquit(struct iv_state *st)
{
	iv_handle_me_bump_other_threads(st);
}

static void iv_handle_me_process_detach(void)
{
	TlsFree(iv_handle_me_group_index);
	iv_handle_me_group_index = -1;
}


/* public use ***************************************************************/
static void iv_handle_me_handle_init(struct iv_handle_ *h)
{
	h->u.me.grp = NULL;
	h->u.me.index = -1;
	INIT_IV_LIST_HEAD(&h->list_active);
}

static struct iv_handle_group *iv_handle_me_choose_group(struct iv_state *st)
{
	struct iv_handle_group *grp;

	grp = iv_handle_me_get_group();
	if (grp->u.me.addition_pointer < MAX_THREAD_HANDLES)
		return grp;

	if (grp != &st->u.me.group_0) {
		grp = &st->u.me.group_0;
		if (grp->u.me.addition_pointer < MAX_THREAD_HANDLES)
			return grp;
	}

	if (!iv_list_empty(&st->u.me.groups_recent_deleted)) {
		grp = iv_container_of(st->u.me.groups_recent_deleted.next,
				      struct iv_handle_group,
				      u.me.list_recent_deleted);

		return grp;
	}

	/*
	 * @@@
	 * ORRRRRRR
	 * - add to most recently non-full having executed thread !?!?!??!?!!?!
	 *   - this spreads the handles unfairly, busy threads get busier
	 *   - if load okay, has the danger of never filling any thread up
	 */

	return NULL;
}

static void
__iv_handle_me_new_poll_thread(struct iv_state *st, struct iv_handle_ *firsth)
{
	struct iv_handle_group *grp;
	HANDLE thr_handle;

	grp = malloc(sizeof(*grp));
	if (grp == NULL)
		iv_fatal("__iv_handle_me_new_poll_thread: out of memory");

	iv_list_add_tail(&grp->u.me.list_all, &st->u.me.groups);
	iv_list_add(&grp->u.me.list_recent_deleted,
		    &st->u.me.groups_recent_deleted);

	grp->u.me.st = st;

	grp->u.me.thr_handle = INVALID_HANDLE_VALUE;
	grp->u.me.thr_signal_handle = CreateEvent(NULL, FALSE, FALSE, NULL);

	InitializeCriticalSection(&grp->u.me.group_lock);
	INIT_IV_LIST_HEAD(&grp->u.me.active_handles);
	grp->u.me.h[0] = firsth;
	grp->u.me.addition_pointer = 1;
	grp->u.me.num_deletions = 0;
	grp->u.me.num_handles = 1;
	grp->u.me.hnd[0] = firsth->handle;
	grp->u.me.hnd[1] = grp->u.me.thr_signal_handle;
	grp->u.me.have_active_handles = !iv_list_empty(&firsth->list_active);

	firsth->u.me.grp = grp;
	firsth->u.me.index = 0;
	if (!iv_list_empty(&firsth->list_active)) {
		iv_list_del(&firsth->list_active);
		iv_list_add_tail(&firsth->list_active,
				 &grp->u.me.active_handles);
	}

	thr_handle = CreateThread(NULL, 0, iv_handle_me_poll_thread,
				  (void *)grp, 0, NULL);
	if (thr_handle == NULL)
		iv_fatal("__iv_handle_me_new_poll_thread: CreateThread failed");

	grp->u.me.thr_handle = thr_handle;
}

static void __iv_handle_me_register(struct iv_state *st, struct iv_handle_ *h)
{
	struct iv_handle_group *grp;

	grp = iv_handle_me_choose_group(st);
	if (grp == NULL) {
		__iv_handle_me_new_poll_thread(st, h);
		return;
	}

	EnterCriticalSection(&grp->u.me.group_lock);

	if (grp->u.me.addition_pointer == MAX_THREAD_HANDLES)
		iv_fatal("__iv_handle_me_register: group full");

	// @@@ optimise for registering to self!!!

	h->u.me.grp = grp;
	h->u.me.index = grp->u.me.addition_pointer;

	if (!iv_list_empty(&h->list_active)) {
		iv_list_del(&h->list_active);
		iv_list_add_tail(&h->list_active, &grp->u.me.active_handles);
	}

	grp->u.me.h[grp->u.me.addition_pointer] = h;
	grp->u.me.addition_pointer++;
	if (grp->u.me.addition_pointer == MAX_THREAD_HANDLES)
		iv_list_del(&grp->u.me.list_recent_deleted);

	LeaveCriticalSection(&grp->u.me.group_lock);

	SetEvent(grp->u.me.thr_signal_handle);
}

static void iv_handle_me_register(struct iv_handle_ *h)
{
	struct iv_state *st = iv_get_state();

	if (h->handler != NULL)
		__iv_handle_me_register(st, h);

	st->numobjs++;
}

static void __iv_handle_me_unregister(struct iv_handle_ *h)
{
	struct iv_handle_group *me = iv_handle_me_get_group();
	struct iv_handle_group *grp = h->u.me.grp;
	struct iv_state *st = grp->u.me.st;

	EnterCriticalSection(&grp->u.me.group_lock);

	if (!iv_list_empty(&h->list_active)) {
		iv_list_del(&h->list_active);
		iv_list_add_tail(&h->list_active,
				 &st->u.me.active_handles_no_handler);
	}

	if (grp->u.me.addition_pointer < MAX_THREAD_HANDLES)
		iv_list_del(&grp->u.me.list_recent_deleted);
	iv_list_add(&grp->u.me.list_recent_deleted,
		    &st->u.me.groups_recent_deleted);

	if (me == grp) {
		if (grp->u.me.addition_pointer != grp->u.me.num_handles ||
		    grp->u.me.num_deletions)
			ResetEvent(grp->u.me.thr_signal_handle);

		grp->u.me.h[h->u.me.index] = NULL;
		h->u.me.grp = NULL;
		h->u.me.index = -1;

		__iv_handle_me_poll_state_update(grp);
		if (grp->u.me.num_handles == 0 && grp != &st->u.me.group_0) {
			iv_list_del(&grp->u.me.list_all);
			iv_list_del(&grp->u.me.list_recent_deleted);
			CloseHandle(grp->u.me.thr_handle);
		}

		LeaveCriticalSection(&grp->u.me.group_lock);

		return;
	}

	if (h->u.me.index >= grp->u.me.addition_pointer) {
		int i;

		grp->u.me.addition_pointer--;
		for (i = h->u.me.index; i < grp->u.me.addition_pointer; i++)
			grp->u.me.h[i] = grp->u.me.h[i + 1];
	} else {
		grp->u.me.h[h->u.me.index] = NULL;
		grp->u.me.num_deletions++;
	}
	h->u.me.grp = NULL;
	h->u.me.index = -1;

	if (grp->u.me.addition_pointer == grp->u.me.num_deletions) {
		HANDLE signal;
		HANDLE wait;
		DWORD ret;

		iv_list_del(&grp->u.me.list_all);
		iv_list_del(&grp->u.me.list_recent_deleted);

		signal = grp->u.me.thr_signal_handle;
		wait = grp->u.me.thr_handle;

		LeaveCriticalSection(&grp->u.me.group_lock);

		ret = SignalObjectAndWait(signal, wait, INFINITE, TRUE);
		while (ret == WAIT_IO_COMPLETION)
			ret = WaitForSingleObjectEx(wait, INFINITE, TRUE);

		if (ret == WAIT_FAILED)
			iv_fatal("__iv_handle_me_unregister: wait failed");

		CloseHandle(wait);
	}

	LeaveCriticalSection(&grp->u.me.group_lock);
}

static void iv_handle_me_unregister(struct iv_handle_ *h)
{
	struct iv_handle_group *grp = h->u.me.grp;
	struct iv_state *st = grp->u.me.st;

	if (h->handler != NULL)
		__iv_handle_me_unregister(h);

	if (!iv_list_empty(&h->list_active))
		iv_list_del(&h->list_active);

	st->numobjs--;
}

static void
iv_handle_me_set_handler(struct iv_handle_ *h, void (*handler)(void *))
{
	struct iv_state *st = iv_get_state();

	if (h->handler == NULL && handler != NULL)
		__iv_handle_me_register(st, h);
	else if (h->handler != NULL && handler == NULL)
		__iv_handle_me_unregister(h);

	h->handler = handler;
}

struct iv_handle_poll_method iv_handle_poll_method_me = {
	.name			= "me",

	.init			= iv_handle_me_init,
	.deinit			= iv_handle_me_deinit,
	.is_primary_thread	= iv_handle_me_is_primary_thread,
	.poll_and_run		= iv_handle_me_poll_and_run,
	.quit			= iv_handle_me_quit,
	.unquit			= iv_handle_me_unquit,
	.process_detach		= iv_handle_me_process_detach,

	.handle_init		= iv_handle_me_handle_init,
	.handle_register	= iv_handle_me_register,
	.handle_unregister	= iv_handle_me_unregister,
	.handle_set_handler	= iv_handle_me_set_handler,
};

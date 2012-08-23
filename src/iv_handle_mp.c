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

/* poll thread **************************************************************/
static int iv_handle_mp_poll_handles(int *active, HANDLE *h, int num)
{
	DWORD ret;
	int num_active;
	int poll_from;

	ret = WaitForMultipleObjectsEx(num, h, FALSE, INFINITE, TRUE);
	if (ret == WAIT_FAILED)
		iv_fatal("iv_handle_mp_poll_handles: returned %x", (int)ret);

	if (ret == WAIT_IO_COMPLETION || ret == WAIT_TIMEOUT)
		return 0;

	if (ret >= WAIT_ABANDONED_0)
		ret -= WAIT_ABANDONED_0;
	else
		ret -= WAIT_OBJECT_0;
	active[0] = ret;
	num_active = 1;
	poll_from = ret + 1;

	while (poll_from < num) {
		ret = WaitForMultipleObjectsEx(num - poll_from, h + poll_from,
					       FALSE, 0, FALSE);
		if (ret == WAIT_FAILED) {
			iv_fatal("iv_handle_mp_poll_handles: returned %x",
				 (int)ret);
		}

		if (ret == WAIT_TIMEOUT)
			break;

		if (ret >= WAIT_ABANDONED_0)
			ret -= WAIT_ABANDONED_0;
		else
			ret -= WAIT_OBJECT_0;
		active[num_active++] = poll_from + ret;
		poll_from += ret + 1;
	}

	return num_active;
}

static DWORD WINAPI iv_handle_mp_poll_thread(void *_grp)
{
	struct iv_handle_group *grp = _grp;
	struct iv_state *st = grp->u.mp.st;

	while (grp->u.mp.num_handles) {
		int active[MAXIMUM_WAIT_OBJECTS];
		int num_active;
		int sig;
		int i;

		if (grp->u.mp.active_handles) {
			SleepEx(INFINITE, TRUE);
			continue;
		}

		num_active = iv_handle_mp_poll_handles(active, grp->u.mp.handle,
						    grp->u.mp.num_handles);
		if (!num_active)
			continue;

		grp->u.mp.active_handles = num_active;

		EnterCriticalSection(&st->u.mp.active_handles_lock);

		if (iv_list_empty(&st->u.mp.active_handles))
			sig = 1;

		for (i = 0; i < num_active; i++) {
			struct iv_handle_ *h;

			h = grp->u.mp.h[active[i]];
			if (!iv_list_empty(&h->list_active)) {
				iv_fatal("iv_handle_mp_poll_thread: handle "
					 "already active!");
			}

			iv_list_add_tail(&h->list_active,
					 &st->u.mp.active_handles);
		}

		LeaveCriticalSection(&st->u.mp.active_handles_lock);

		if (sig)
			SetEvent(st->u.mp.active_handles_wait);
	}

	free(grp);

	return 0;
}


/* poll thread mp handlers *************************************************/
static void WINAPI iv_handle_mp_thread_apc_kick(ULONG_PTR dummy)
{
}

static void WINAPI iv_handle_mp_thread_apc_add(ULONG_PTR data)
{
	struct iv_handle_ *h = (void *)data;
	struct iv_handle_group *grp = h->u.mp.grp;

	h->u.mp.index = grp->u.mp.num_handles++;

	if (!iv_list_empty(&h->list_active))
		grp->u.mp.active_handles++;

	grp->u.mp.h[h->u.mp.index] = h;
	grp->u.mp.handle[h->u.mp.index] = h->handle;

	SetEvent(grp->u.mp.st->u.mp.thr_wait);
}

static void WINAPI iv_handle_mp_thread_apc_delete(ULONG_PTR data)
{
	struct iv_handle_ *h = (void *)data;
	struct iv_handle_group *grp = h->u.mp.grp;

	grp->u.mp.num_handles--;

	if (!iv_list_empty(&h->list_active))
		grp->u.mp.active_handles--;

	if (h->u.mp.index != grp->u.mp.num_handles) {
		struct iv_handle_ *victim;

		victim = grp->u.mp.h[grp->u.mp.num_handles];

		grp->u.mp.h[h->u.mp.index] = victim;
		grp->u.mp.handle[h->u.mp.index] = victim->handle;
		victim->u.mp.index = h->u.mp.index;
	}

	h->u.mp.grp = NULL;
	h->u.mp.index = -1;

	SetEvent(grp->u.mp.st->u.mp.thr_wait);
}


/* poll thread requests *****************************************************/
static void iv_handle_mp_thread_queue(struct iv_handle_group *grp,
				   void (WINAPI *func)(ULONG_PTR), void *arg)
{
	if (!QueueUserAPC(func, grp->u.mp.thr_handle, (ULONG_PTR)arg))
		iv_fatal("iv_handle_mp_thread_queue: QueueUserAPC failed");
}

static void iv_handle_mp_thread_block(HANDLE h)
{
	if (WaitForSingleObjectEx(h, INFINITE, FALSE) != WAIT_OBJECT_0) {
		iv_fatal("iv_handle_mp_thread_block: "
			 "WaitForSingleObjectEx failed");
	}
}

static void iv_handle_mp_thread_add(struct iv_state *st,
				 struct iv_handle_group *grp,
				 struct iv_handle_ *h)
{
	h->u.mp.grp = grp;
	iv_handle_mp_thread_queue(grp, iv_handle_mp_thread_apc_add, h);

	iv_handle_mp_thread_block(st->u.mp.thr_wait);
}

static void iv_handle_mp_thread_delete(struct iv_state *st,
				    struct iv_handle_group *grp,
				    struct iv_handle_ *h)
{
	HANDLE toclose;

	if (grp->u.mp.num_handles == 1) {
		iv_list_del(&grp->u.mp.list);
		iv_list_del(&grp->u.mp.list_recent_deleted);
		toclose = grp->u.mp.thr_handle;
	} else {
		toclose = INVALID_HANDLE_VALUE;
	}

	iv_handle_mp_thread_queue(grp, iv_handle_mp_thread_apc_delete, h);
	iv_handle_mp_thread_block(st->u.mp.thr_wait);

	if (toclose != INVALID_HANDLE_VALUE)
		CloseHandle(toclose);
}


/* internal use *************************************************************/
static void iv_handle_mp_init(struct iv_state *st)
{
	st->u.mp.active_handles_wait = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&st->u.mp.active_handles_lock);
	INIT_IV_LIST_HEAD(&st->u.mp.active_handles);
	INIT_IV_LIST_HEAD(&st->u.mp.active_handles_no_handler);
	INIT_IV_LIST_HEAD(&st->u.mp.groups);
	INIT_IV_LIST_HEAD(&st->u.mp.groups_recent_deleted);
	st->u.mp.thr_wait = CreateEvent(NULL, FALSE, FALSE, NULL);
}

static void iv_handle_mp_deinit(struct iv_state *st)
{
	CloseHandle(st->u.mp.active_handles_wait);
	DeleteCriticalSection(&st->u.mp.active_handles_lock);
	CloseHandle(st->u.mp.thr_wait);
}

static void iv_handle_mp_poll_and_run(struct iv_state *st, struct timespec *to)
{
	int msec;
	int ret;
	struct iv_list_head handles;
	struct iv_list_head temp;

	msec = 1000 * to->tv_sec + (to->tv_nsec + 999999) / 1000000;

	ret = WaitForSingleObjectEx(st->u.mp.active_handles_wait, msec, TRUE);
	if (ret != WAIT_OBJECT_0 && ret != WAIT_IO_COMPLETION &&
	    ret != WAIT_TIMEOUT) {
		iv_fatal("iv_handle_mp_poll_and_run: WaitForSingleObjectEx "
			 "returned %x", (int)ret);
	}

	__iv_invalidate_now(st);

	EnterCriticalSection(&st->u.mp.active_handles_lock);
	__iv_list_steal_elements(&st->u.mp.active_handles, &handles);
	LeaveCriticalSection(&st->u.mp.active_handles_lock);

	INIT_IV_LIST_HEAD(&temp);

	while (!iv_list_empty(&handles)) {
		struct iv_handle_ *h;

		h = iv_list_entry(handles.next, struct iv_handle_, list_active);

		iv_list_del_init(&h->list_active);
		iv_list_add_tail(&h->list_active, &temp);

		h->handler(h->cookie);

		if (!iv_list_empty(&temp)) {
			struct iv_handle_group *grp = h->u.mp.grp;

			if (!--grp->u.mp.active_handles) {
				iv_handle_mp_thread_queue(grp,
					iv_handle_mp_thread_apc_kick, NULL);
			}
			iv_list_del_init(&h->list_active);
		}
	}
}

static void iv_handle_mp_quit(struct iv_state *st)
{
	// @@@
}

static void iv_handle_mp_unquit(struct iv_state *st)
{
	// @@@
}

static void iv_handle_mp_process_detach(void)
{
	// @@@
}


/* public use ***************************************************************/
static void iv_handle_mp_handle_init(struct iv_handle_ *h)
{
	h->u.mp.grp = NULL;
	h->u.mp.index = -1;
	INIT_IV_LIST_HEAD(&h->list_active);
}

static struct iv_handle_group *iv_handle_mp_last_group(struct iv_state *st)
{
	struct iv_handle_group *grp;

	grp = NULL;
	if (!iv_list_empty(&st->u.mp.groups)) {
		grp = iv_container_of(st->u.mp.groups.prev,
				      struct iv_handle_group, u.mp.list);
	}

	return grp;
}

static struct iv_handle_group *
iv_handle_mp_choose_group(struct iv_state *st, int rebal)
{
	struct iv_handle_group *grp;

	if (rebal && !iv_list_empty(&st->u.mp.groups)) {
		grp = iv_handle_mp_last_group(st);
		if (grp->u.mp.num_handles < MAXIMUM_WAIT_OBJECTS)
			return grp;
	} else if (!rebal && !iv_list_empty(&st->u.mp.groups_recent_deleted)) {
		grp = iv_container_of(st->u.mp.groups_recent_deleted.next,
				      struct iv_handle_group,
				      u.mp.list_recent_deleted);

		return grp;
	}

	return NULL;
}

static void
__iv_handle_mp_register(struct iv_state *st, struct iv_handle_ *h, int rebal)
{
	struct iv_handle_group *grp;

	if (!iv_list_empty(&h->list_active)) {
		EnterCriticalSection(&st->u.mp.active_handles_lock);
		iv_list_del(&h->list_active);
		iv_list_add_tail(&h->list_active, &st->u.mp.active_handles);
		LeaveCriticalSection(&st->u.mp.active_handles_lock);

		SetEvent(st->u.mp.active_handles_wait);
	}

	grp = iv_handle_mp_choose_group(st, rebal);
	if (grp == NULL) {
		HANDLE thr_handle;

		grp = malloc(sizeof(*grp));
		if (grp == NULL)
			iv_fatal("__iv_handle_mp_register: out of memory");

		iv_list_add_tail(&grp->u.mp.list, &st->u.mp.groups);
		iv_list_add(&grp->u.mp.list_recent_deleted,
			    &st->u.mp.groups_recent_deleted);
		grp->u.mp.st = st;
		grp->u.mp.num_handles = 1;
		grp->u.mp.active_handles = !iv_list_empty(&h->list_active);
		grp->u.mp.h[0] = h;
		grp->u.mp.handle[0] = h->handle;

		h->u.mp.grp = grp;
		h->u.mp.index = 0;

		thr_handle = CreateThread(NULL, 0, iv_handle_mp_poll_thread,
					  (void *)grp, 0, NULL);
		if (thr_handle == NULL) {
			iv_fatal("__iv_handle_mp_register: "
				 "CreateThread failed");
		}

		grp->u.mp.thr_handle = thr_handle;
	} else {
		iv_handle_mp_thread_add(st, grp, h);
		if (grp->u.mp.num_handles == MAXIMUM_WAIT_OBJECTS)
			iv_list_del_init(&grp->u.mp.list_recent_deleted);
	}
}

static void iv_handle_mp_register(struct iv_handle_ *h)
{
	struct iv_state *st = iv_get_state();

	if (h->handler != NULL)
		__iv_handle_mp_register(st, h, 1);

	st->numobjs++;
}

static void __iv_handle_mp_unregister(struct iv_handle_ *h, int rebal)
{
	struct iv_handle_group *grp = h->u.mp.grp;
	struct iv_state *st = grp->u.mp.st;

	if (!iv_list_empty(&h->list_active)) {
		EnterCriticalSection(&st->u.mp.active_handles_lock);
		iv_list_del(&h->list_active);
		iv_list_add_tail(&h->list_active,
				 &st->u.mp.active_handles_no_handler);
		LeaveCriticalSection(&st->u.mp.active_handles_lock);
	}

	if (rebal) {
		struct iv_handle_group *last = iv_handle_mp_last_group(st);

		if (grp == last) {
			iv_handle_mp_thread_delete(st, grp, h);
		} else {
			struct iv_handle_ *h2;

			h2 = last->u.mp.h[last->u.mp.num_handles - 1];
			iv_handle_mp_thread_delete(st, last, h2);

			if (grp->u.mp.num_handles == 1) {
				iv_handle_mp_thread_add(st, grp, h2);
				iv_handle_mp_thread_delete(st, grp, h);
			} else {
				iv_handle_mp_thread_delete(st, grp, h);
				iv_handle_mp_thread_add(st, grp, h2);
			}
		}
	} else {
		if (grp->u.mp.num_handles == 1) {
			iv_handle_mp_thread_delete(st, grp, h);
		} else {
			iv_handle_mp_thread_delete(st, grp, h);
			iv_list_del(&grp->u.mp.list_recent_deleted);
			iv_list_add(&grp->u.mp.list_recent_deleted,
				    &st->u.mp.groups_recent_deleted);
		}
	}
}

static void iv_handle_mp_unregister(struct iv_handle_ *h)
{
	struct iv_handle_group *grp = h->u.mp.grp;
	struct iv_state *st = grp->u.mp.st;

	if (h->handler != NULL)
		__iv_handle_mp_unregister(h, 1);

	if (!iv_list_empty(&h->list_active))
		iv_list_del(&h->list_active);

	st->numobjs--;
}

static void
iv_handle_mp_set_handler(struct iv_handle_ *h, void (*handler)(void *))
{
	struct iv_state *st = iv_get_state();

	if (h->handler == NULL && handler != NULL)
		__iv_handle_mp_register(st, h, 1);
	else if (h->handler != NULL && handler == NULL)
		__iv_handle_mp_unregister(h, 1);

	h->handler = handler;
}

struct iv_handle_poll_method iv_handle_poll_method_mp = {
	.name			= "mp",

	.init			= iv_handle_mp_init,
	.deinit			= iv_handle_mp_deinit,
	.poll_and_run		= iv_handle_mp_poll_and_run,
	.quit			= iv_handle_mp_quit,
	.unquit			= iv_handle_mp_unquit,
	.process_detach		= iv_handle_mp_process_detach,

	.handle_init		= iv_handle_mp_handle_init,
	.handle_register	= iv_handle_mp_register,
	.handle_unregister	= iv_handle_mp_unregister,
	.handle_set_handler	= iv_handle_mp_set_handler,
};


static void iv_handle_mp_norebalance_register(struct iv_handle_ *h)
{
	struct iv_state *st = iv_get_state();

	if (h->handler != NULL)
		__iv_handle_mp_register(st, h, 0);

	st->numobjs++;
}

static void iv_handle_mp_norebalance_unregister(struct iv_handle_ *h)
{
	struct iv_handle_group *grp = h->u.mp.grp;
	struct iv_state *st = grp->u.mp.st;

	if (h->handler != NULL)
		__iv_handle_mp_unregister(h, 0);

	if (!iv_list_empty(&h->list_active))
		iv_list_del(&h->list_active);

	st->numobjs--;
}

static void
iv_handle_mp_norebalance_set_handler(struct iv_handle_ *h,
				     void (*handler)(void *))
{
	struct iv_state *st = iv_get_state();

	if (h->handler == NULL && handler != NULL)
		__iv_handle_mp_register(st, h, 0);
	else if (h->handler != NULL && handler == NULL)
		__iv_handle_mp_unregister(h, 0);

	h->handler = handler;
}

struct iv_handle_poll_method iv_handle_poll_method_mp_norebalance = {
	.name			= "mp_norebalance",

	.init			= iv_handle_mp_init,
	.deinit			= iv_handle_mp_deinit,
	.poll_and_run		= iv_handle_mp_poll_and_run,
	.quit			= iv_handle_mp_quit,
	.unquit			= iv_handle_mp_unquit,
	.process_detach		= iv_handle_mp_process_detach,

	.handle_init		= iv_handle_mp_handle_init,
	.handle_register	= iv_handle_mp_norebalance_register,
	.handle_unregister	= iv_handle_mp_norebalance_unregister,
	.handle_set_handler	= iv_handle_mp_norebalance_set_handler,
};

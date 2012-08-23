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
static int iv_handle_poll_handles(int *active, HANDLE *h, int num)
{
	DWORD ret;
	int num_active;
	int poll_from;

	ret = WaitForMultipleObjectsEx(num, h, FALSE, INFINITE, TRUE);
	if (ret == WAIT_FAILED)
		iv_fatal("iv_handle_poll_handles: returned %x", (int)ret);

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
			iv_fatal("iv_handle_poll_handles: returned %x",
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

static DWORD WINAPI iv_handle_poll_thread(void *_grp)
{
	struct iv_handle_group *grp = _grp;
	struct iv_state *st = grp->st;

	while (grp->num_handles) {
		int active[MAXIMUM_WAIT_OBJECTS];
		int num_active;
		int sig;
		int i;

		if (grp->active_handles) {
			SleepEx(INFINITE, TRUE);
			continue;
		}

		num_active = iv_handle_poll_handles(active, grp->handle,
						    grp->num_handles);
		if (!num_active)
			continue;

		grp->active_handles = num_active;

		EnterCriticalSection(&st->active_handles_lock);

		if (iv_list_empty(&st->active_handles))
			sig = 1;

		for (i = 0; i < num_active; i++) {
			struct iv_handle_ *h;

			h = grp->h[active[i]];
			if (!iv_list_empty(&h->list)) {
				iv_fatal("iv_handle_poll_thread: handle "
					 "already active!");
			}

			iv_list_add_tail(&h->list, &st->active_handles);
		}

		LeaveCriticalSection(&st->active_handles_lock);

		if (sig)
			SetEvent(st->active_handles_wait);
	}

	free(grp);

	return 0;
}


/* poll thread apc handlers *************************************************/
static void WINAPI iv_handle_thread_apc_kick(ULONG_PTR dummy)
{
}

static void WINAPI iv_handle_thread_apc_add(ULONG_PTR data)
{
	struct iv_handle_ *h = (void *)data;
	struct iv_handle_group *grp = h->grp;

	h->index = grp->num_handles++;

	if (!iv_list_empty(&h->list))
		grp->active_handles++;

	grp->h[h->index] = h;
	grp->handle[h->index] = h->handle;

	SetEvent(grp->st->thr_wait);
}

static void WINAPI iv_handle_thread_apc_delete(ULONG_PTR data)
{
	struct iv_handle_ *h = (void *)data;
	struct iv_handle_group *grp = h->grp;

	grp->num_handles--;

	if (!iv_list_empty(&h->list))
		grp->active_handles--;

	if (h->index != grp->num_handles) {
		struct iv_handle_ *victim;

		victim = grp->h[grp->num_handles];

		grp->h[h->index] = victim;
		grp->handle[h->index] = victim->handle;
		victim->index = h->index;
	}

	h->grp = NULL;
	h->index = -1;

	SetEvent(grp->st->thr_wait);
}


/* poll thread requests *****************************************************/
static void iv_handle_thread_queue(struct iv_handle_group *grp,
				   void (WINAPI *func)(ULONG_PTR), void *arg)
{
	if (!QueueUserAPC(func, grp->thr_handle, (ULONG_PTR)arg))
		iv_fatal("iv_handle_thread_queue: QueueUserAPC fail");
}

static void iv_handle_thread_block(HANDLE h)
{
	if (WaitForSingleObjectEx(h, INFINITE, FALSE) != WAIT_OBJECT_0)
		iv_fatal("iv_handle_thread_block: WaitForSingleObjectEx fail");
}

static void iv_handle_thread_add(struct iv_state *st,
				 struct iv_handle_group *grp,
				 struct iv_handle_ *h)
{
	h->grp = grp;
	iv_handle_thread_queue(grp, iv_handle_thread_apc_add, h);

	iv_handle_thread_block(st->thr_wait);
}

static void iv_handle_thread_delete(struct iv_state *st,
				    struct iv_handle_group *grp,
				    struct iv_handle_ *h)
{
	HANDLE toclose;

	if (grp->num_handles == 1) {
		iv_list_del(&grp->list);
		toclose = grp->thr_handle;
	} else {
		toclose = INVALID_HANDLE_VALUE;
	}

	iv_handle_thread_queue(grp, iv_handle_thread_apc_delete, h);
	iv_handle_thread_block(st->thr_wait);

	if (toclose != INVALID_HANDLE_VALUE)
		CloseHandle(toclose);
}


/* user thread **************************************************************/
void iv_handle_init(struct iv_state *st)
{
	st->active_handles_wait = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&st->active_handles_lock);
	INIT_IV_LIST_HEAD(&st->active_handles);
	INIT_IV_LIST_HEAD(&st->active_handles_no_handler);
	INIT_IV_LIST_HEAD(&st->handle_groups);
	st->thr_wait = CreateEvent(NULL, FALSE, FALSE, NULL);
}

void iv_handle_deinit(struct iv_state *st)
{
	CloseHandle(st->active_handles_wait);
	DeleteCriticalSection(&st->active_handles_lock);
	CloseHandle(st->thr_wait);
}

void iv_handle_poll_and_run(struct iv_state *st, struct timespec *to)
{
	int msec;
	int ret;
	struct iv_list_head handles;
	struct iv_list_head temp;

	msec = 1000 * to->tv_sec + (to->tv_nsec + 999999) / 1000000;

	ret = WaitForSingleObjectEx(st->active_handles_wait, msec, TRUE);
	if (ret != WAIT_OBJECT_0 && ret != WAIT_IO_COMPLETION &&
	    ret != WAIT_TIMEOUT) {
		iv_fatal("iv_handle_poll_and_run: WaitForSingleObjectEx "
			 "returned %x", (int)ret);
	}

	__iv_invalidate_now(st);

	EnterCriticalSection(&st->active_handles_lock);
	__iv_list_steal_elements(&st->active_handles, &handles);
	LeaveCriticalSection(&st->active_handles_lock);

	INIT_IV_LIST_HEAD(&temp);

	while (!iv_list_empty(&handles)) {
		struct iv_handle_ *h;

		h = iv_list_entry(handles.next, struct iv_handle_, list);

		iv_list_del_init(&h->list);
		iv_list_add_tail(&h->list, &temp);

		h->handler(h->cookie);

		if (!iv_list_empty(&temp)) {
			struct iv_handle_group *grp = h->grp;

			if (!--grp->active_handles) {
				iv_handle_thread_queue(grp,
					iv_handle_thread_apc_kick, NULL);
			}
			iv_list_del_init(&h->list);
		}
	}
}

void IV_HANDLE_INIT(struct iv_handle *_h)
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	h->registered = 0;
	h->grp = NULL;
	h->index = -1;
	INIT_IV_LIST_HEAD(&h->list);
}

static struct iv_handle_group *iv_handle_last_group(struct iv_state *st)
{
	struct iv_handle_group *grp;

	grp = NULL;
	if (!iv_list_empty(&st->handle_groups)) {
		grp = iv_container_of(st->handle_groups.prev,
				      struct iv_handle_group, list);
	}

	return grp;
}

static void __iv_handle_register(struct iv_state *st, struct iv_handle_ *h)
{
	struct iv_handle_group *grp;

	if (!iv_list_empty(&h->list)) {
		EnterCriticalSection(&st->active_handles_lock);
		iv_list_del(&h->list);
		iv_list_add_tail(&h->list, &st->active_handles);
		LeaveCriticalSection(&st->active_handles_lock);

		SetEvent(st->active_handles_wait);
	}

	grp = iv_handle_last_group(st);
	if (grp == NULL || grp->num_handles == MAXIMUM_WAIT_OBJECTS) {
		HANDLE thr_handle;

		grp = malloc(sizeof(*grp));
		if (grp == NULL)
			iv_fatal("__iv_handle_register: out of memory");

		iv_list_add_tail(&grp->list, &st->handle_groups);
		grp->st = st;
		grp->num_handles = 1;
		grp->active_handles = !iv_list_empty(&h->list);
		grp->h[0] = h;
		grp->handle[0] = h->handle;

		h->grp = grp;
		h->index = 0;

		thr_handle = CreateThread(NULL, 0, iv_handle_poll_thread,
					  (void *)grp, 0, NULL);
		if (thr_handle == NULL)
			iv_fatal("__iv_handle_register: CreateThread failed");

		grp->thr_handle = thr_handle;
	} else {
		iv_handle_thread_add(st, grp, h);
	}
}

void iv_handle_register(struct iv_handle *_h)
{
	struct iv_state *st = iv_get_state();
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	if (h->registered) {
		iv_fatal("iv_handle_register: called with handle "
			 "which is still registered");
	}
	h->registered = 1;

	if (h->handler != NULL)
		__iv_handle_register(st, h);

	st->numobjs++;
}

static void __iv_handle_unregister(struct iv_handle_ *h)
{
	struct iv_handle_group *grp = h->grp;
	struct iv_state *st = grp->st;
	struct iv_handle_group *last = iv_handle_last_group(st);

	if (!iv_list_empty(&h->list)) {
		EnterCriticalSection(&st->active_handles_lock);
		iv_list_del(&h->list);
		iv_list_add_tail(&h->list, &st->active_handles_no_handler);
		LeaveCriticalSection(&st->active_handles_lock);
	}

	if (grp == last) {
		iv_handle_thread_delete(st, grp, h);
	} else {
		struct iv_handle_ *h2;

		h2 = last->h[last->num_handles - 1];
		iv_handle_thread_delete(st, last, h2);

		if (grp->num_handles == 1) {
			iv_handle_thread_add(st, grp, h2);
			iv_handle_thread_delete(st, grp, h);
		} else {
			iv_handle_thread_delete(st, grp, h);
			iv_handle_thread_add(st, grp, h2);
		}
	}
}

void iv_handle_unregister(struct iv_handle *_h)
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;
	struct iv_handle_group *grp = h->grp;
	struct iv_state *st = grp->st;

	if (!h->registered) {
		iv_fatal("iv_handle_unregister: called with handle "
			 "which is not registered");
	}
	h->registered = 0;

	if (h->handler != NULL)
		__iv_handle_unregister(h);

	if (!iv_list_empty(&h->list))
		iv_list_del(&h->list);

	st->numobjs--;
}

int iv_handle_registered(struct iv_handle *_h)
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	return h->registered;
}

void iv_handle_set_handler(struct iv_handle *_h, void (*handler)(void *))
{
	struct iv_state *st = iv_get_state();
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	if (!h->registered) {
		iv_fatal("iv_handle_set_handler: called with handle "
			 "which is not registered");
	}

	if (h->handler == NULL && handler != NULL)
		__iv_handle_register(st, h);
	else if (h->handler != NULL && handler == NULL)
		__iv_handle_unregister(h);

	h->handler = handler;
}

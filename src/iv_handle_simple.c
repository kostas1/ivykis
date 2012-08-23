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
static DWORD WINAPI iv_handle_simple_poll_thread(void *_h)
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;
	int waiting;

	waiting = 1;
	while (h->u.simple.polling) {
		struct iv_state *st = h->u.simple.st;
		HANDLE hnd;
		DWORD ret;
		int sig;

		hnd = waiting ? h->handle : h->u.simple.rewait_handle;

		ret = WaitForSingleObjectEx(hnd, INFINITE, TRUE);
		if (ret == WAIT_IO_COMPLETION)
			continue;

		if (ret != WAIT_OBJECT_0 && ret != WAIT_ABANDONED) {
			iv_fatal("iv_handle_simple_poll_thread(%d): %x",
				 (int)(ULONG_PTR)h->handle, (int)ret);
		}

		waiting ^= 1;
		if (waiting)
			continue;

		sig = 0;

		EnterCriticalSection(&st->u.simple.active_handles_lock);
		if (iv_list_empty(&h->list_active)) {
			if (iv_list_empty(&st->u.simple.active_handles))
				sig = 1;
			iv_list_add_tail(&h->list_active,
					 &st->u.simple.active_handles);
		}
		LeaveCriticalSection(&st->u.simple.active_handles_lock);

		if (sig)
			SetEvent(st->u.simple.active_handles_wait);
	}

	return 0;
}


/* internal use *************************************************************/
void iv_handle_simple_init(struct iv_state *st)
{
	st->u.simple.active_handles_wait =
		CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&st->u.simple.active_handles_lock);
	INIT_IV_LIST_HEAD(&st->u.simple.active_handles);
	st->u.simple.handled_handle = INVALID_HANDLE_VALUE;
}

void iv_handle_simple_deinit(struct iv_state *st)
{
	CloseHandle(st->u.simple.active_handles_wait);
	DeleteCriticalSection(&st->u.simple.active_handles_lock);
}

void iv_handle_simple_poll_and_run(struct iv_state *st, struct timespec *to)
{
	int msec;
	int ret;
	struct iv_list_head handles;

	msec = 1000 * to->tv_sec + (to->tv_nsec + 999999) / 1000000;

	ret = WaitForSingleObjectEx(st->u.simple.active_handles_wait,
				    msec, TRUE);
	if (ret != WAIT_OBJECT_0 && ret != WAIT_IO_COMPLETION &&
	    ret != WAIT_TIMEOUT) {
		iv_fatal("iv_handle_simple_poll_and_run: "
			 "WaitForMultipleObjectsEx returned %x", (int)ret);
	}

	__iv_invalidate_now(st);

	EnterCriticalSection(&st->u.simple.active_handles_lock);
	__iv_list_steal_elements(&st->u.simple.active_handles, &handles);
	LeaveCriticalSection(&st->u.simple.active_handles_lock);

	while (!iv_list_empty(&handles)) {
		struct iv_handle_ *h;

		h = iv_list_entry(handles.next, struct iv_handle_,
				  list_active);
		iv_list_del_init(&h->list_active);

		st->u.simple.handled_handle = h;
		h->handler(h->cookie);
		if (st->u.simple.handled_handle == h) {
			SetEvent(h->u.simple.rewait_handle);
			st->u.simple.handled_handle = INVALID_HANDLE_VALUE;
		}
	}
}

void iv_handle_simple_quit(struct iv_state *st)
{
	// @@@
}

void iv_handle_simple_unquit(struct iv_state *st)
{
	// @@@
}

void iv_handle_simple_process_detach(void)
{
	// @@@
}


/* public use ***************************************************************/
void iv_handle_simple_handle_init(struct iv_handle_ *h)
{
	h->u.simple.polling = 0;
	h->u.simple.st = NULL;
	INIT_IV_LIST_HEAD(&h->list_active);
	h->u.simple.rewait_handle = INVALID_HANDLE_VALUE;
	h->u.simple.thr_handle = INVALID_HANDLE_VALUE;
}

static void __iv_handle_simple_register(struct iv_handle_ *h)
{
	HANDLE w;

	h->u.simple.polling = 1;

	w = CreateThread(NULL, 0, iv_handle_simple_poll_thread,
			 (void *)h, 0, NULL);
	if (w == NULL)
		iv_fatal("__iv_handle_simple_register: CreateThread failed");

	h->u.simple.thr_handle = w;
}

static void iv_handle_simple_register(struct iv_handle_ *h)
{
	struct iv_state *st = iv_get_state();

	h->u.simple.st = st;
	INIT_IV_LIST_HEAD(&h->list_active);
	h->u.simple.rewait_handle = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (h->handler != NULL)
		__iv_handle_simple_register(h);

	st->numobjs++;
}

static void WINAPI iv_handle_simple_dummy_apcproc(ULONG_PTR dummy)
{
}

static void __iv_handle_simple_unregister(struct iv_handle_ *h)
{
	DWORD ret;

	h->u.simple.polling = 0;

	ret = QueueUserAPC(iv_handle_simple_dummy_apcproc,
			   h->u.simple.thr_handle, 0);
	if (ret == 0)
		iv_fatal("__iv_handle_simple_unregister: QueueUserAPC failed");

	do {
		ret = WaitForSingleObjectEx(h->u.simple.thr_handle,
					    INFINITE, TRUE);
	} while (ret == WAIT_IO_COMPLETION);

	if (ret != WAIT_OBJECT_0) {
		iv_fatal("__iv_handle_simple_unregister: "
			 "WaitForSingleObjectEx failed");
	}

	CloseHandle(h->u.simple.thr_handle);
	h->u.simple.thr_handle = INVALID_HANDLE_VALUE;
}

static void iv_handle_simple_unregister(struct iv_handle_ *h)
{
	struct iv_state *st = h->u.simple.st;

	if (h->handler != NULL)
		__iv_handle_simple_unregister(h);

	h->u.simple.st = NULL;
	iv_list_del_init(&h->list_active);
	CloseHandle(&h->u.simple.rewait_handle);

	st->numobjs--;
	if (st->u.simple.handled_handle == h)
		st->u.simple.handled_handle = INVALID_HANDLE_VALUE;
}

static void
iv_handle_simple_set_handler(struct iv_handle_ *h, void (*handler)(void *))
{
	if (h->handler == NULL && handler != NULL)
		__iv_handle_simple_register(h);
	else if (h->handler != NULL && handler == NULL)
		__iv_handle_simple_unregister(h);

	h->handler = handler;
}

struct iv_handle_poll_method iv_handle_poll_method_simple = {
	.name			= "simple",

	.init			= iv_handle_simple_init,
	.deinit			= iv_handle_simple_deinit,
	.poll_and_run		= iv_handle_simple_poll_and_run,
	.quit			= iv_handle_simple_quit,
	.unquit			= iv_handle_simple_unquit,
	.process_detach		= iv_handle_simple_process_detach,

	.handle_init		= iv_handle_simple_handle_init,
	.handle_register	= iv_handle_simple_register,
	.handle_unregister	= iv_handle_simple_unregister,
	.handle_set_handler	= iv_handle_simple_set_handler,
};

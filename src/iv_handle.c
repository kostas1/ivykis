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

struct iv_handle_poll_method *method;

void iv_handle_init(struct iv_state *st)
{
	method = &iv_handle_poll_method_simple;
	method->init(st);
}

void iv_handle_deinit(struct iv_state *st)
{
	method->deinit(st);
}

int iv_handle_is_primary_thread(struct iv_state *st)
{
	if (method->is_primary_thread != NULL)
		return method->is_primary_thread(st);

	return 1;
}

void iv_handle_poll_and_run(struct iv_state *st, struct timespec *to)
{
	method->poll_and_run(st, to);
}

void iv_handle_quit(struct iv_state *st)
{
	if (method->quit != NULL)
		method->quit(st);
}

void iv_handle_unquit(struct iv_state *st)
{
	if (method->unquit != NULL)
		method->unquit(st);
}

void iv_handle_process_detach(void)
{
	if (method->process_detach != NULL)
		method->process_detach();
}


void IV_HANDLE_INIT(struct iv_handle *_h)
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	h->registered = 0;
	method->handle_init(h);
}

void iv_handle_register(struct iv_handle *_h)
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	if (h->registered) {
		iv_fatal("iv_handle_register: called with handle "
			 "which is already registered");
	}
	h->registered = 1;

	method->handle_register(h);
}

void iv_handle_unregister(struct iv_handle *_h)
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	if (!h->registered) {
		iv_fatal("iv_handle_unregister: called with handle "
			 "which is not registered");
	}
	h->registered = 0;

	method->handle_unregister(h);
}

int iv_handle_registered(struct iv_handle *_h)
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	return h->registered;
}

void iv_handle_set_handler(struct iv_handle *_h, void (*handler)(void *))
{
	struct iv_handle_ *h = (struct iv_handle_ *)_h;

	if (!h->registered) {
		iv_fatal("iv_handle_set_handler: called with handle "
			 "which is not registered");
	}

	method->handle_set_handler(h, handler);
}

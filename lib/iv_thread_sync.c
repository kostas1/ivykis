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
#include <iv_list.h>
#include <pthread.h>
#include "iv_private.h"

static pthread_mutex_t thread_list_write_lock;
static struct iv_list_head thread_list;

void iv_thread_sync_init_first(void)
{
	pthread_mutex_init(&thread_list_write_lock, NULL);
	INIT_IV_LIST_HEAD(&thread_list);
}


static void sync_thread_list(void)
{
	struct iv_list_head *ilh;

	__sync_synchronize();

	iv_list_for_each (ilh, &thread_list) {
		struct iv_state *st;

		st = iv_container_of(ilh, struct iv_state, thread_list);
		__flag_wait_zero(&st->thread_traversing_thread_list);
	}
}

void iv_thread_sync_init(struct iv_state *st)
{
	pthread_mutex_lock(&thread_list_write_lock);

	st->thread_list.next = &thread_list;
	st->thread_list.prev = thread_list.prev;
	flag_init(&st->thread_traversing_thread_list);
	flag_init(&st->thread_busy);

	sync_thread_list();

	thread_list.prev->next = &st->thread_list;
	thread_list.prev = &st->thread_list;

	pthread_mutex_unlock(&thread_list_write_lock);
}

void iv_thread_sync_deinit(struct iv_state *st)
{
	if (flag_is_set(&st->thread_traversing_thread_list) ||
	    flag_is_set(&st->thread_busy)) {
		fprintf(stderr, "iv_thread_sync_deinit: thread busy\n");
		abort();
	}

	pthread_mutex_lock(&thread_list_write_lock);

	st->thread_list.prev->next = st->thread_list.next;
	st->thread_list.next->prev = st->thread_list.prev;

	sync_thread_list();

	pthread_mutex_unlock(&thread_list_write_lock);

	st->thread_list.next = NULL;
	st->thread_list.prev = NULL;
	flag_deinit(&st->thread_traversing_thread_list);
	flag_deinit(&st->thread_busy);
}


static void sync_thread_busy(struct iv_state *st)
{
	struct iv_list_head *ilh;

	flag_set(&st->thread_traversing_thread_list);
	iv_list_for_each (ilh, &thread_list) {
		struct iv_state *st2;

		st2 = iv_container_of(ilh, struct iv_state, thread_list);
		__flag_wait_zero(&st2->thread_busy);
	}
	flag_clear(&st->thread_traversing_thread_list);
}

void iv_thread_sync(void)
{
	struct iv_state *st = iv_get_state();

	if (flag_is_set(&st->thread_busy)) {
		flag_clear(&st->thread_busy);
		sync_thread_busy(st);
		flag_set(&st->thread_busy);
	} else {
		sync_thread_busy(st);
	}
}

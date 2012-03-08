/*
 * ivykis, an event handling library
 * Copyright (C) 2002, 2003 Lennert Buytenhek
 * Dedicated to Marija Kulikova.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version
 * 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License version 2.1 for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License version 2.1 along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <iv.h>
#include <iv_thread.h>
#include <pthread.h>
#include <sys/syscall.h>

static int dead;

static void thr_init_deinit(void *_dummy)
{
	while (!dead) {
		iv_init();
		sched_yield();

		iv_deinit();
		sched_yield();
	}
}

static void thr_sync(void *_dummy)
{
	iv_init();

	while (!dead)
		iv_thread_sync();

	iv_deinit();
}

static void timer_fire(void *_t)
{
	struct iv_timer *t = _t;
	struct timespec req;

	if (!dead) {
		t->expires.tv_sec++;
		iv_timer_register(t);
	}

	req.tv_sec = 0;
	req.tv_nsec = 100000000;
	nanosleep(&req, NULL);

	iv_invalidate_now();
}

static void thr_timer(void *_dl)
{
	struct iv_timer t;
	int offset;

	iv_init();

	offset = (unsigned long)_dl;

	iv_validate_now();
	IV_TIMER_INIT(&t);
	t.expires = iv_now;
	t.expires.tv_nsec += 1000000 * offset;
	if (t.expires.tv_nsec >= 1000000000) {
		t.expires.tv_sec++;
		t.expires.tv_nsec -= 1000000000;
	}
	t.cookie = &t;
	t.handler = timer_fire;
	iv_timer_register(&t);

	iv_main();

	iv_deinit();
}

static void list(void *_dummy)
{
	iv_thread_list_children();
}

static void die(void *_dummy)
{
	dead = 1;
}

int main()
{
	int i;
	struct iv_timer timer_list;
	struct iv_timer timer_die;

	iv_init();

	for (i = 0; i < 2; i++)
		iv_thread_create("init_deinit", thr_init_deinit, NULL);

	for (i = 0; i < 10; i++)
		iv_thread_create("sync", thr_sync, NULL);

	for (i = 0; i < 20; i++) {
		unsigned long offset;

		offset = 50 * i;
		iv_thread_create("timer", thr_timer, (void *)offset);
	}

	iv_validate_now();

	IV_TIMER_INIT(&timer_list);
	timer_list.expires = iv_now;
	timer_list.expires.tv_sec++;
	timer_list.handler = list;
	iv_timer_register(&timer_list);

	IV_TIMER_INIT(&timer_die);
	timer_die.expires = iv_now;
	timer_die.expires.tv_sec += 5;
	timer_die.handler = die;
	iv_timer_register(&timer_die);

	iv_main();

	iv_deinit();

	return 0;
}

/*
 * ivykis, an event handling library
 * Copyright (C) 2012 Lennert Buytenhek
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
#include <iv_rcu.h>

static void print(void *h)
{
	fprintf(stderr, "%p: waiting done\n", h);

	if (0)
		free(h);
}

static void rcu_wait(void)
{
	struct iv_rcu_head *h;

	h = malloc(sizeof(*h));
	if (h == NULL)
		exit(1);

	printf("%p: waiting\n", h);

	h->cookie = h;
	h->handler = print;
	iv_rcu_call(h);
}

static void debug_iv_rcu_read_lock(void)
{
	fprintf(stderr, "%p: lock starting\n", (void *)(unsigned long)pthread_self());
	iv_rcu_read_lock();
	fprintf(stderr, "%p: lock done\n", (void *)(unsigned long)pthread_self());
}

static void debug_iv_rcu_read_unlock(void)
{
	fprintf(stderr, "%p: unlock starting\n", (void *)(unsigned long)pthread_self());
	iv_rcu_read_unlock();
	fprintf(stderr, "%p: unlock done\n", (void *)(unsigned long)pthread_self());
}

int main()
{
	iv_init();

	rcu_wait();

	debug_iv_rcu_read_lock();

	rcu_wait();

	debug_iv_rcu_read_unlock();

	iv_main();

	iv_deinit();

	return 0;
}

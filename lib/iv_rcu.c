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
#include <iv_rcu.h>
#include <pthread.h>
#include "iv_private.h"

static pthread_rwlock_t iv_rcu_thread_list_lock;
static struct iv_list_head iv_rcu_thread_list;

void iv_rcu_init_first(void)
{
	pthread_rwlock_init(&iv_rcu_thread_list_lock, NULL);
	INIT_IV_LIST_HEAD(&iv_rcu_thread_list);
}

void iv_rcu_init(struct iv_state *st)
{
	pthread_mutex_init(&st->rcu_lock, NULL);
	st->rcu_read_lock_depth = 0;
	INIT_IV_LIST_HEAD(&st->rcu_heads);

	pthread_rwlock_wrlock(&iv_rcu_thread_list_lock);
	iv_list_add_tail(&st->rcu_thread_list, &iv_rcu_thread_list);
	pthread_rwlock_unlock(&iv_rcu_thread_list_lock);
}

void iv_rcu_deinit(struct iv_state *st)
{
	if (st->rcu_read_lock_depth) {
		fprintf(stderr, "iv_rcu_deinit: nonzero read lock depth\n");
		abort();
	}

	pthread_rwlock_wrlock(&iv_rcu_thread_list_lock);
	iv_list_del(&st->rcu_thread_list);
	pthread_rwlock_unlock(&iv_rcu_thread_list_lock);
}

void iv_rcu_read_lock(void)
{
	struct iv_state *st = iv_get_state();

	pthread_mutex_lock(&st->rcu_lock);
	st->rcu_read_lock_depth++;
	pthread_mutex_unlock(&st->rcu_lock);
}

static void __advance_heads(struct iv_state *st)
{
	struct iv_list_head *ilh;

	ilh = st->rcu_thread_list.next;
	if (ilh != &iv_rcu_thread_list) {
		struct iv_state *next;

		next = iv_container_of(ilh, struct iv_state, rcu_thread_list);

		pthread_mutex_lock(&next->rcu_lock);
		iv_list_splice_tail(&st->rcu_heads, &next->rcu_heads);
		pthread_mutex_unlock(&next->rcu_lock);

		return;
	}

	while (!iv_list_empty(&st->rcu_heads)) {
		struct iv_rcu_head *irh;

		irh = iv_container_of(st->rcu_heads.next,
				      struct iv_rcu_head, list);

		iv_list_del(&irh->list);
		irh->handler(irh->cookie);
	}
}

void iv_rcu_read_unlock(void)
{
	struct iv_state *st = iv_get_state();

	pthread_rwlock_rdlock(&iv_rcu_thread_list_lock);

	pthread_mutex_lock(&st->rcu_lock);
	if (!--st->rcu_read_lock_depth)
		__advance_heads(st);
	pthread_mutex_unlock(&st->rcu_lock);

	pthread_rwlock_unlock(&iv_rcu_thread_list_lock);
}

void iv_rcu_call(struct iv_rcu_head *irh)
{
	struct iv_list_head *ilh;

	pthread_rwlock_rdlock(&iv_rcu_thread_list_lock);

	iv_list_for_each (ilh, &iv_rcu_thread_list) {
		struct iv_state *st;

		st = iv_container_of(ilh, struct iv_state, rcu_thread_list);

		pthread_mutex_lock(&st->rcu_lock);

		if (st->rcu_read_lock_depth) {
			iv_list_add_tail(&irh->list, &st->rcu_heads);
			pthread_mutex_unlock(&st->rcu_lock);
			goto out;
		}

		pthread_mutex_unlock(&st->rcu_lock);
	}

	irh->handler(irh->cookie);

out:
	pthread_rwlock_unlock(&iv_rcu_thread_list_lock);
}

void iv_rcu_synchronize(void)
{
	struct iv_list_head *ilh;

	pthread_rwlock_rdlock(&iv_rcu_thread_list_lock);

	iv_list_for_each (ilh, &iv_rcu_thread_list) {
		struct iv_state *st;

		st = iv_container_of(ilh, struct iv_state, rcu_thread_list);

		pthread_mutex_lock(&st->rcu_lock);
		while (st->rcu_read_lock_depth) {
			pthread_mutex_unlock(&st->rcu_lock);
			sched_yield();
			pthread_mutex_lock(&st->rcu_lock);
		}
		pthread_mutex_unlock(&st->rcu_lock);
	}

	pthread_rwlock_unlock(&iv_rcu_thread_list_lock);
}

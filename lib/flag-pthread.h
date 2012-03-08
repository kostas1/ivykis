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

#include <pthread.h>

typedef struct {
	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
	int			flag;
} flag_t;


static inline void flag_init(flag_t *flag)
{
	pthread_mutex_init(&flag->mutex, NULL);
	pthread_cond_init(&flag->cond, NULL);
	flag->flag = 0;
}

static inline void flag_set(flag_t *flag)
{
	pthread_mutex_lock(&flag->mutex);
	flag->flag = 1;
	pthread_mutex_unlock(&flag->mutex);
	pthread_mutex_lock(&flag->mutex);
	pthread_mutex_unlock(&flag->mutex);
}

static inline void flag_clear(flag_t *flag)
{
	pthread_mutex_lock(&flag->mutex);
	pthread_mutex_unlock(&flag->mutex);
	pthread_mutex_lock(&flag->mutex);
	flag->flag = 0;
	pthread_mutex_unlock(&flag->mutex);

	pthread_cond_broadcast(&flag->cond);
}

static inline int flag_is_set(flag_t *flag)
{
	return flag->flag;
}

static inline void __flag_wait_zero(flag_t *flag)
{
	pthread_mutex_lock(&flag->mutex);
	while (flag->flag)
		pthread_cond_wait(&flag->cond, &flag->mutex);
	pthread_mutex_unlock(&flag->mutex);
}

static inline void flag_deinit(flag_t *flag)
{
	pthread_mutex_destroy(&flag->mutex);
	pthread_cond_destroy(&flag->cond);
}

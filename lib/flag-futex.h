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

#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef int flag_t;


static inline int futex_wake(int *uaddr, int val)
{
	return syscall(__NR_futex, uaddr, FUTEX_WAKE_PRIVATE, val);
}

static inline int futex_wait(int *uaddr, int val)
{
	return syscall(__NR_futex, uaddr, FUTEX_WAIT_PRIVATE, val, NULL);
}


static inline void flag_init(flag_t *flag)
{
	*flag = 0;
}

static inline void flag_set(flag_t *flag)
{
	*flag = 1;
	__sync_synchronize();
}

static inline void flag_clear(flag_t *flag)
{
	int oldval;

	__sync_synchronize();

	do {
		oldval = *flag;
	} while (!__sync_bool_compare_and_swap(flag, oldval, 0));

	if (oldval == 2)
		futex_wake(flag, INT_MAX);
}

static inline int flag_is_set(flag_t *flag)
{
	return !!*flag;
}

static inline void __flag_wait_zero(flag_t *flag)
{
	while (1) {
		int oldval;

		oldval = *flag;
		if (oldval == 0)
			break;

		if (oldval == 2 || __sync_bool_compare_and_swap(flag, 1, 2))
			futex_wait(flag, 2);
	}
}

static inline void flag_deinit(flag_t *flag)
{
}

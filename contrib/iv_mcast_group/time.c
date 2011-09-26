/*
 * ivykis, an event handling library
 * Copyright (C) 2002, 2003, 2011 Lennert Buytenhek
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
#include "iv_mcast_group.h"
#include "priv.h"

void timespec_add_ms(struct timespec *ts, int msec)
{
	ts->tv_sec += msec / 1000;
	ts->tv_nsec += 1000000 * (msec % 1000);
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000;
	}
}

void timespec_add_range(struct timespec *ts, int min, int max)
{
	int msec;

	msec = min;
	if (min != max)
		msec += rand() % (max - min);

	timespec_add_ms(ts, msec);
}

void timespec_add_rand(struct timespec *ts, int avg)
{
	int spread;

	spread = avg / 5;
	timespec_add_range(ts, avg - spread, avg + spread);
}

long long timespec_diff(struct timespec *a, struct timespec *b)
{
	long long a_nsec;
	long long b_nsec;

	a_nsec = 1000000000ULL * a->tv_sec + a->tv_nsec;
	b_nsec = 1000000000ULL * b->tv_sec + b->tv_nsec;

	if (a_nsec > b_nsec)
		return (a_nsec - b_nsec + 999999ULL) / 1000000ULL;
	else
		return -((b_nsec - a_nsec + 999999ULL) / 1000000ULL);
}

int timespec_less_eq(struct timespec *a, struct timespec *b)
{
	if (a->tv_sec < b->tv_sec)
		return 1;
	if (a->tv_sec > b->tv_sec)
		return 0;

	if (a->tv_nsec < b->tv_nsec)
		return 1;
	if (a->tv_nsec > b->tv_nsec)
		return 0;

	return 1;
}

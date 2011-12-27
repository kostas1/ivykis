/*
 * ivykis, an event handling library
 * Copyright (C) 2011 Lennert Buytenhek
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
#include <iv.h>
#include <iv_signal.h>
#include <iv_thread.h>
#include <signal.h>
#include <sys/syscall.h>

#ifdef __FreeBSD__
/* Older FreeBSDs (6.1) don't include ucontext.h in thr.h.  */
#include <sys/ucontext.h>
#include <sys/thr.h>
#endif

static pid_t gettid(void)
{
	pid_t tid;

	tid = 0;
#ifdef __NR_gettid
	tid = syscall(__NR_gettid);
#elif defined(__FreeBSD__)
	long thr;
	thr_self(&thr);
	tid = (pid_t)thr;
#endif

	return tid;
}


static void got_sigusr1(void *_x)
{
	printf("got SIGUSR1 in thread %d\n", gettid());
}

static void thr(void *_x)
{
	struct iv_signal is_sigusr1;

	printf("starting thread %d\n", gettid());

	iv_init();

	IV_SIGNAL_INIT(&is_sigusr1);
	is_sigusr1.signum = SIGUSR1;
	is_sigusr1.flags = IV_SIGNAL_FLAG_THIS_THREAD;
	is_sigusr1.handler = got_sigusr1;
	iv_signal_register(&is_sigusr1);

	iv_main();
}

int main()
{
	iv_init();

	iv_thread_create("1", thr, NULL);
	iv_thread_create("2", thr, NULL);
	iv_thread_create("3", thr, NULL);
	iv_thread_create("4", thr, NULL);

	iv_main();

	iv_deinit();

	return 0;
}

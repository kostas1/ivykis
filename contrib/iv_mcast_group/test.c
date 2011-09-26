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
#include <iv_signal.h>
#include "iv_mcast_group.h"

static struct iv_signal sigint;
static struct iv_signal sigusr1;
static struct iv_signal sigusr2;

#define MAX_MEMBERSHIPS		128
static int in_use[MAX_MEMBERSHIPS];
static struct iv_mcast_group img[MAX_MEMBERSHIPS];

static void conf_change(void *cookie)
{
	printf("%d: conf change\n", (int)(unsigned long)cookie);
}

static void got_sigint(void *dummy)
{
	int i;

	iv_signal_unregister(&sigint);
	iv_signal_unregister(&sigusr1);
	iv_signal_unregister(&sigusr2);
	for (i = 0; i < MAX_MEMBERSHIPS; i++) {
		if (in_use[i]) {
			printf("%d: killing\n", i);
			iv_mcast_group_unregister(&img[i]);
			in_use[i] = 0;
		}
	}
}

static void got_sigusr1(void *dummy)
{
	int i;

	for (i = 0; i < MAX_MEMBERSHIPS; i++) {
		if (!in_use[i]) {
			printf("%d: starting\n", i);
			uuid_clear(img[i].node_id);
			if (iv_mcast_group_register(&img[i]))
				abort();
			in_use[i] = 1;

			break;
		}
	}
}

static void got_sigusr2(void *dummy)
{
	int i;

	for (i = 0; i < MAX_MEMBERSHIPS; i++) {
		if (in_use[i]) {
			printf("%d: killing\n", i);
			iv_mcast_group_unregister(&img[i]);
			in_use[i] = 0;

			break;
		}
	}
}

int main()
{
	int i;

	srand(getpid() ^ time(NULL));

	iv_init();

	IV_SIGNAL_INIT(&sigint);
	sigint.signum = SIGINT;
	sigint.flags = 0;
	sigint.cookie = NULL;
	sigint.handler = got_sigint;
	iv_signal_register(&sigint);

	IV_SIGNAL_INIT(&sigusr1);
	sigusr1.signum = SIGUSR1;
	sigusr1.flags = 0;
	sigusr1.cookie = NULL;
	sigusr1.handler = got_sigusr1;
	iv_signal_register(&sigusr1);

	IV_SIGNAL_INIT(&sigusr2);
	sigusr2.signum = SIGUSR2;
	sigusr2.flags = 0;
	sigusr2.cookie = NULL;
	sigusr2.handler = got_sigusr2;
	iv_signal_register(&sigusr2);

	for (i = 0; i < MAX_MEMBERSHIPS; i++) {
		img[i].dest.sin_family = AF_INET;
		img[i].dest.sin_addr.s_addr = htonl(0xe00000ff);
		img[i].dest.sin_port = htons(1695);
		img[i].secret_len = 7;
		img[i].secret = (uint8_t *)"testing";
		img[i].local.sin_family = AF_INET;
		img[i].local.sin_addr.s_addr = htonl(INADDR_ANY);
		img[i].local.sin_port = htons(1695);
		uuid_clear(img[i].node_id);
		img[i].priority = 5;
		img[i].hello_interval_ms = 10000;
		img[i].cookie = (void *)(unsigned long)i;
		img[i].conf_change = conf_change;

		if (i < 8) {
			if (iv_mcast_group_register(&img[i]))
				abort();
			in_use[i] = 1;
		}
	}

	iv_main();

	iv_deinit();

	return 0;
}

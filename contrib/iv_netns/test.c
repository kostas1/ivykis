/*
 * ivykis, an event handling library
 * Copyright (C) 2011 Lennert Buytenhek
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
#include <iv_netns.h>

static struct iv_timer tim;
static struct iv_netns ns;

static void destroy_ns(void *dummy)
{
	printf("destroying new network namespace\n");

	iv_netns_destroy(&ns);
}

static void create_socket(void *dummy)
{
	int fd;

	printf("creating socket in new network namespace\n");

	fd = iv_netns_socket(&ns, AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		exit(1);
	}

	close(fd);

	tim.expires.tv_sec++;
	tim.handler = destroy_ns;
	iv_timer_register(&tim);
}

static void create_ns(void *dummy)
{
	printf("creating new network namespace\n");

	iv_netns_create(&ns);

	tim.expires.tv_sec++;
	tim.handler = create_socket;
	iv_timer_register(&tim);
}

int main(int argc, char *argv[])
{
	iv_init();

	IV_TIMER_INIT(&tim);
	iv_validate_now();
	tim.expires = iv_now;
	tim.expires.tv_sec++;
	tim.handler = create_ns;
	iv_timer_register(&tim);

	iv_main();

	iv_deinit();

	return 0;
}

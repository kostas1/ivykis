/*
 * kruva, a stackable network stack handling library
 * Copyright (C) 2012 Lennert Buytenhek
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
#include <iv.h>
#include <string.h>
#include "kruva.h"

static struct kru_stack *kst;
static struct kru_socket *kso;

static void handler(void *_cookie)
{
	int err;

	err = kru_socket_get_error(kso);

	printf("connect status, error %d\n", err);

	kru_socket_set_handler_in(kso, NULL);
	kru_socket_set_handler_out(kso, NULL);
	kru_socket_close(kso);
}

int main(void)
{
	int ret;

	iv_init();

//	kst = kru_stack_local();
	kst = kru_stack_netns_from_name("testing");
	if (kst == NULL) {
		printf("error creating network stack\n");
		return 1;
	}

	kso = kru_stack_socket(kst, AF_INET, SOCK_STREAM, 0);
	if (kso == NULL) {
		printf("error creating socket\n");
		return 1;
	}

	ret = kru_socket_connect(kso, "192.168.1.22:53");
	if (ret < 0 && errno != EINPROGRESS) {
		printf("connect: %s\n", strerror(-ret));
		return 1;
	}

	kru_socket_set_handler_in(kso, handler);
	kru_socket_set_handler_out(kso, handler);

	iv_main();

	return 0;
}

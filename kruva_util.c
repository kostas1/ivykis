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
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

int addr_to_sockaddr(struct sockaddr_storage *sa, char *addr)
{
	int a;
	int b;
	int c;
	int d;
	int port;

	/* 1.2.3.4:1234 */
	if (sscanf(addr, "%d.%d.%d.%d:%d", &a, &b, &c, &d, &port) == 5 &&
	    a >= 0 && a <= 255 && b >= 0 && b <= 255 && c >= 0 && c <= 255 &&
	    d >= 0 && d <= 255 && port >= 0 && port <= 65535) {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		sin->sin_addr.s_addr = htonl((a << 24) | (b << 16) |
					     (c << 8) | d);

		return 0;
	}

	/* 1.2.3.4 */
	if (sscanf(addr, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 &&
	    a >= 0 && a <= 255 && b >= 0 && b <= 255 && c >= 0 && c <= 255 &&
	    d >= 0 && d <= 255) {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		sin->sin_family = AF_INET;
		sin->sin_port = 0;
		sin->sin_addr.s_addr = htonl((a << 24) | (b << 16) |
					     (c << 8) | d);

		return 0;
	}

	/* [1234::5678]:1234 */
	/* [1234::5678] */
	if (addr[0] == '[') {
		char *end;

		end = strchr(addr, '[');
		if (end != NULL) {
		}
	}

	/* 1234::5678 */
	/* 1234--5678.ipv6-literal.net */

	/* scope ids? */

	return -EINVAL;
}

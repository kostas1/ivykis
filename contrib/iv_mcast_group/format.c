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
#include <printf.h>
#include "iv_mcast_group.h"
#include "priv.h"

static int print_time(FILE *stream, const struct printf_info *info,
		      const void *const *args)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	return fprintf(stream, "%9d.%.9d", ts.tv_sec, ts.tv_nsec);
}

static int print_time_arginfo(const struct printf_info *info, size_t n,
			      int *argtypes)
{
	return 0;
}

static int print_uuid(FILE *stream, const struct printf_info *info,
		      const void *const *args)
{
	const uuid_t *u;
	char txt[37];

	u = *((const uuid_t **)args[0]);

	uuid_unparse_lower(*u, txt);
	fwrite(txt, 36, 1, stream);

	return 36;
}

static int print_uuid_arginfo(const struct printf_info *info, size_t n,
			      int *argtypes)
{
	if (n > 0)
		argtypes[0] = PA_POINTER;
	return 1;
}

void __add_format_strings(void)
{
	register_printf_function('T', print_time, print_time_arginfo);
	register_printf_function('U', print_uuid, print_uuid_arginfo);
}

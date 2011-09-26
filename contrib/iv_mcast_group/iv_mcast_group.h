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

#ifndef __IV_MCAST_GROUP_H
#define __IV_MCAST_GROUP_H

#include <iv.h>
#include <iv_avl.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <uuid/uuid.h>

struct iv_mcast_group
{
	struct sockaddr_in	dest;
	int			secret_len;
	uint8_t			*secret;
	struct sockaddr_in	local;
	uuid_t			node_id;
	uint32_t		priority;
	uint32_t		hello_interval_ms;
	void			*cookie;
	void			(*conf_change)(void *cookie);

	const EVP_MD		*evp;
	uuid_t			master_id;
	uuid_t			backup_id;
	int			neighbour_count;
	struct iv_avl_tree	neighbours;
	struct iv_fd		listening_sock;
	struct iv_timer		election_delay_timer;
	struct timespec		last_hello_tx;
	struct iv_timer		hello_periodic_timer;
	struct iv_timer		hello_update_timer;
};

int iv_mcast_group_register(struct iv_mcast_group *img);
void iv_mcast_group_unregister(struct iv_mcast_group *img);


#endif

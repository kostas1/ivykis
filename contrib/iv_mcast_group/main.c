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
#include <iv_list.h>
#include <netinet/ip.h>
#include "iv_mcast_group.h"
#include "priv.h"

static int create_socket(struct iv_mcast_group *img)
{
	int fd;
	int i;
	uint8_t b;
	struct ip_mreqn imr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	i = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0) {
		perror("setsockopt SO_REUSEADDR");
		close(fd);
		return -1;
	}

	b = IPTOS_RELIABILITY;
	if (setsockopt(fd, IPPROTO_IP, IP_TOS, &b, sizeof(b)) < 0) {
		perror("setsockopt IP_TOS");
		close(fd);
		return -1;
	}

	i = 1;
	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &i, sizeof(i)) < 0) {
		perror("setsockopt IP_MULTICAST_LOOP");
		close(fd);
		return -1;
	}

	i = 1;
	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &i, sizeof(i)) < 0) {
		perror("setsockopt IP_MULTICAST_TTL");
		close(fd);
		return -1;
	}

	if (bind(fd, (struct sockaddr *)&img->local, sizeof(img->local)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}

	imr.imr_multiaddr = img->dest.sin_addr;
	imr.imr_address = img->local.sin_addr;
	imr.imr_ifindex = 0;
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imr,
		       sizeof(imr)) < 0) {
		perror("setsockopt IP_ADD_MEMBERSHIP");
		close(fd);
		return -1;
	}

	return fd;
}

int iv_mcast_group_register(struct iv_mcast_group *img)
{
	int fd;

	if (uuid_is_null(img->node_id))
		uuid_generate_random(img->node_id);

	dbg_printf("%T: %U: initialising\n", img->node_id);

	if (img->hello_interval_ms == 0)
		img->hello_interval_ms = 1000;

	img->evp = EVP_sha256();
	uuid_clear(img->master_id);
	uuid_clear(img->backup_id);
	img->neighbour_count = 0;
	INIT_IV_AVL_TREE(&img->neighbours, neighbour_compare);

	fd = create_socket(img);
	if (fd < 0)
		return -1;

	IV_FD_INIT(&img->listening_sock);
	img->listening_sock.fd = fd;
	img->listening_sock.cookie = (void *)img;
	img->listening_sock.handler_in = got_packet;
	iv_fd_register(&img->listening_sock);

	iv_validate_now();

	IV_TIMER_INIT(&img->election_delay_timer);
	img->election_delay_timer.expires = iv_now;
	timespec_add_rand(&img->election_delay_timer.expires,
			  img->hello_interval_ms / 3);
	img->election_delay_timer.cookie = (void *)img;
	img->election_delay_timer.handler = election_delay_timer_expiry;
	iv_timer_register(&img->election_delay_timer);

	img->last_hello_tx.tv_sec = 0;
	img->last_hello_tx.tv_nsec = 0;

	IV_TIMER_INIT(&img->hello_periodic_timer);
	img->hello_periodic_timer.expires = iv_now;
	timespec_add_range(&img->hello_periodic_timer.expires,
			   0, img->hello_interval_ms);
	img->hello_periodic_timer.cookie = (void *)img;
	img->hello_periodic_timer.handler = hello_periodic_timer_expiry;
	iv_timer_register(&img->hello_periodic_timer);

	IV_TIMER_INIT(&img->hello_update_timer);
	img->hello_update_timer.expires = iv_now;
	timespec_add_range(&img->hello_update_timer.expires,
			   0, img->hello_interval_ms / 4);
	img->hello_update_timer.cookie = (void *)img;
	img->hello_update_timer.handler = hello_update_timer_expiry;
	iv_timer_register(&img->hello_update_timer);

	return 0;
}

void iv_mcast_group_unregister(struct iv_mcast_group *img)
{
	struct iv_avl_node *an;
	struct iv_avl_node *an2;

	uuid_clear(img->master_id);
	uuid_clear(img->backup_id);

	img->neighbour_count = 0;

	iv_avl_tree_for_each_safe (an, an2, &img->neighbours) {
		struct neighbour *n;

		n = iv_container_of(an, struct neighbour, an);

		iv_avl_tree_delete(&img->neighbours, &n->an);
		iv_timer_unregister(&n->expiry_timer);
		free(n);
	}

	if (iv_timer_registered(&img->election_delay_timer))
		iv_timer_unregister(&img->election_delay_timer);
	iv_timer_unregister(&img->hello_periodic_timer);
	if (iv_timer_registered(&img->hello_update_timer))
		iv_timer_unregister(&img->hello_update_timer);

	xmit_hello_packet(img);

	iv_fd_unregister(&img->listening_sock);
	close(img->listening_sock.fd);
}


#ifdef DEBUG
void add_format_strings(void) __attribute__((constructor));
void add_format_strings(void)
{
	__add_format_strings();
}
#endif

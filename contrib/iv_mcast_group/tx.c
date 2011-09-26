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
#include <iv_list.h>
#include <openssl/hmac.h>
#include "iv_mcast_group.h"
#include "priv.h"

void xmit_hello_packet(struct iv_mcast_group *img)
{
	int pkt_size;
	struct hello *h;
	struct timespec ts;
	int i;
	struct iv_avl_node *an;

	dbg_printf("%T: %U: xmit hello, master %U, backup %U, with %d "
		   "neighbours\n", img->node_id, img->master_id,
		   img->backup_id, img->neighbour_count);

	pkt_size = sizeof(*h) + (img->neighbour_count * sizeof(uuid_t));

	h = alloca(pkt_size + EVP_MD_size(img->evp));

	uuid_copy(h->node_id, img->node_id);
	clock_gettime(CLOCK_REALTIME, &ts);
	h->time_sec = htonl(ts.tv_sec);
	h->time_nsec = htonl(ts.tv_nsec);
	h->hello_interval_ms = htons(img->hello_interval_ms);
	h->priority = img->priority;
	h->__pad0 = 0;
	uuid_copy(h->master_id, img->master_id);
	uuid_copy(h->backup_id, img->backup_id);

	i = 0;
	iv_avl_tree_for_each (an, &img->neighbours) {
		struct neighbour *neigh;

		neigh = iv_container_of(an, struct neighbour, an);
		uuid_copy(h->neighbour[i++], neigh->node_id);
	}

	HMAC(img->evp, img->secret, img->secret_len,
	     (void *)h, pkt_size, (void *)h + pkt_size, NULL);
	pkt_size += EVP_MD_size(img->evp);

	sendto(img->listening_sock.fd, h, pkt_size, 0,
	       (struct sockaddr *)&img->dest, sizeof(img->dest));

	iv_validate_now();
	img->last_hello_tx = iv_now;
}

void hello_periodic_timer_expiry(void *_img)
{
	struct iv_mcast_group *img = (struct iv_mcast_group *)_img;

	xmit_hello_packet(img);

	iv_validate_now();
	timespec_add_rand(&img->hello_periodic_timer.expires,
			  img->hello_interval_ms);
	iv_timer_register(&img->hello_periodic_timer);
}

void hello_update_timer_expiry(void *_img)
{
	struct iv_mcast_group *img = (struct iv_mcast_group *)_img;

	xmit_hello_packet(img);
}

void hello_update(struct iv_mcast_group *img)
{
	int hi = img->hello_interval_ms;

	/*
	 * If there is already a pending hello update, do nothing.
	 */
	if (iv_timer_registered(&img->hello_update_timer))
		return;

	/*
	 * If the periodic hello timer is due to expire soon, wait.
	 */
	iv_validate_now();
	if (timespec_diff(&img->hello_periodic_timer.expires, &iv_now) < hi / 5)
		return;

	/*
	 * If the last transmitted hello was too recent, schedule a
	 * timer to send out a new hello when we are again allowed
	 * to do so, otherwise send a hello immediately.
	 */
	if (timespec_diff(&iv_now, &img->last_hello_tx) < hi / 10) {
		img->hello_update_timer.expires = img->last_hello_tx;
		timespec_add_rand(&img->hello_update_timer.expires, hi / 10);
		iv_timer_register(&img->hello_update_timer);
	} else {
		xmit_hello_packet(img);
	}
}

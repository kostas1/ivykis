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
#include <string.h>
#include "iv_mcast_group.h"
#include "priv.h"

static struct neighbour *find_neighbour(struct iv_mcast_group *img, uuid_t id)
{
	struct iv_avl_node *an;

	an = img->neighbours.root;
	while (an != NULL) {
		struct neighbour *neigh;
		int ret;

		neigh = iv_container_of(an, struct neighbour, an);

		ret = uuid_compare(id, neigh->node_id);
		if (ret == 0)
			return neigh;

		if (ret < 0)
			an = an->left;
		else
			an = an->right;
	}

	return NULL;
}

static void neighbour_timer_expiry(void *_n)
{
	struct neighbour *n = (struct neighbour *)_n;
	struct iv_mcast_group *img = n->img;

	dbg_printf("%T: %U: neighbour %U died\n", img->node_id, n->node_id);

	img->neighbour_count--;
	iv_avl_tree_delete(&img->neighbours, &n->an);

	if (n->saw_me)
		recalc_master_backup(img);

	free(n);
}

static void
receive_hello(struct iv_mcast_group *img, struct hello *h, int neighbour_count)
{
	struct timespec pkt;
	struct timespec ts;
	int saw_me;
	int i;
	int new_neighbour;
	struct neighbour *n;

	/*
	 * Ignore packets that were looped back.
	 */
	if (!uuid_compare(img->node_id, h->node_id))
		return;

	/*
	 * Clocks must be approximately synchronised.
	 */
	pkt.tv_sec = ntohl(h->time_sec);
	pkt.tv_nsec = ntohl(h->time_nsec);
	clock_gettime(CLOCK_REALTIME, &ts);
	if (llabs(timespec_diff(&pkt, &ts)) > 10000ULL)
		return;

	/*
	 * All nodes in the group must agree on hello interval.
	 */
	if (img->hello_interval_ms != ntohs(h->hello_interval_ms))
		return;

#if 0
	dbg_printf("%T: %U: rcvd hello from %U, master %U, backup %U, "
		   "with %d neighbours\n", img->node_id, h->node_id,
		   h->master_id, h->backup_id, neighbour_count);
#endif

	saw_me = 0;
	for (i = 0; i < neighbour_count; i++) {
		if (!uuid_compare(h->neighbour[i], img->node_id)) {
			saw_me = 1;
			break;
		}
	}

	n = find_neighbour(img, h->node_id);
	if (n == NULL) {
		new_neighbour = 1;

		n = malloc(sizeof(struct neighbour));
		if (n == NULL)
			return;

		dbg_printf("%T: %U: found new neighbour %U\n",
			   img->node_id, h->node_id);

		uuid_copy(n->node_id, h->node_id);
		n->img = img;

		IV_TIMER_INIT(&n->expiry_timer);
		n->expiry_timer.cookie = (void *)n;
		n->expiry_timer.handler = neighbour_timer_expiry;

		img->neighbour_count++;
		iv_avl_tree_insert(&img->neighbours, &n->an);
	} else {
		new_neighbour = 0;

		if (timespec_less_eq(&pkt, &n->last_hello_rx))
			return;
		iv_timer_unregister(&n->expiry_timer);
	}

	n->last_hello_rx = pkt;
	n->priority = h->priority;
	n->saw_me = saw_me;
	n->claims_master = !uuid_compare(h->node_id, h->master_id);
	n->claims_backup = !uuid_compare(h->node_id, h->backup_id);

	iv_validate_now();
	n->expiry_timer.expires = iv_now;
	timespec_add_ms(&n->expiry_timer.expires, 4 * img->hello_interval_ms);
	iv_timer_register(&n->expiry_timer);

	if (!recalc_master_backup(img) && new_neighbour)
		hello_update(img);
}

void got_packet(void *_img)
{
	struct iv_mcast_group *img = (struct iv_mcast_group *)_img;
	int ret;
	uint8_t buf[65536];
	uint8_t *hmac;
	struct hello *h;
	int neighbour_count;

	ret = read(img->listening_sock.fd, buf, sizeof(buf));
	if (ret < 0 || ret < sizeof(struct hello) + EVP_MD_size(img->evp))
		return;

	ret -= EVP_MD_size(img->evp);

	hmac = alloca(EVP_MD_size(img->evp));

	HMAC(img->evp, img->secret, img->secret_len, buf, ret, hmac, NULL);
	if (memcmp(buf + ret, hmac, EVP_MD_size(img->evp)))
		return;

	h = (struct hello *)buf;
	neighbour_count = (ret - sizeof(*h)) / sizeof(h->neighbour[0]);

	receive_hello(img, h, neighbour_count);
}

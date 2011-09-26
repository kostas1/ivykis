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
#include "iv_mcast_group.h"
#include "priv.h"

static struct neighbour *
better(struct neighbour *a, struct neighbour *b, int as_master)
{
	/*
	 * Only consider neighbours that have a nonzero priority and
	 * have seen my Hello packets.
	 */
	if (!a->priority || !a->saw_me)
		return b;

	/*
	 * If we are choosing a backup, skip neighbours that have
	 * declared themselves master.
	 */
	if (!as_master && a->claims_master)
		return b;

	/*
	 * If there is no b, a is automatically better.
	 */
	if (b == NULL)
		return a;

	/*
	 * If we are choosing a master[/backup], neighbours that have
	 * already declared themselves master[/backup] are preferred over
	 * neighbours that have not declared themselves master[/backup].
	 */
	if (as_master) {
		if (a->claims_master && !b->claims_master)
			return a;
		if (!a->claims_master && b->claims_master)
			return b;
	} else {
		if (a->claims_backup && !b->claims_backup)
			return a;
		if (!a->claims_backup && b->claims_backup)
			return b;
	}

	/*
	 * Prefer neighbours with higher priority.
	 */
	if (a->priority > b->priority)
		return a;
	if (a->priority < b->priority)
		return b;

	/*
	 * Use the node ID as the tie breaker.
	 */
	return (uuid_compare(a->node_id, b->node_id) > 0) ? a : b;
}

static void __recalc_once(struct iv_mcast_group *img, struct neighbour *me)
{
	struct neighbour *best_master;
	struct neighbour *best_backup;
	struct iv_avl_node *an;

	best_master = better(me, NULL, 1);
	best_backup = better(me, NULL, 0);

	iv_avl_tree_for_each (an, &img->neighbours) {
		struct neighbour *neigh;

		neigh = iv_container_of(an, struct neighbour, an);

		best_master = better(neigh, best_master, 1);
		best_backup = better(neigh, best_backup, 0);
	}

	uuid_clear(img->master_id);
	if (best_master != NULL)
		uuid_copy(img->master_id, best_master->node_id);

	uuid_clear(img->backup_id);
	if (best_backup != NULL)
		uuid_copy(img->backup_id, best_backup->node_id);
}

int recalc_master_backup(struct iv_mcast_group *img)
{
	uuid_t old_master_id;
	uuid_t old_backup_id;
	struct neighbour me;
	int changed;

	/*
	 * Don't choose a master and backup until the initial election
	 * delay period is over.
	 */
	if (iv_timer_registered(&img->election_delay_timer))
		return 0;

	uuid_copy(old_master_id, img->master_id);
	uuid_copy(old_backup_id, img->backup_id);

	uuid_copy(me.node_id, img->node_id);
	me.priority = img->priority;
	me.saw_me = 1;

	me.claims_master = !uuid_compare(img->node_id, img->master_id);
	me.claims_backup = !uuid_compare(img->node_id, img->backup_id);
	__recalc_once(img, &me);

	me.claims_master = !uuid_compare(img->node_id, img->master_id);
	me.claims_backup = !uuid_compare(img->node_id, img->backup_id);
	__recalc_once(img, &me);

	changed = 0;
	if (uuid_compare(old_master_id, img->master_id)) {
		dbg_printf("%T: %U: master %U -> %U\n", img->node_id,
			   old_master_id, img->master_id);
		changed = 1;
	}

	if (uuid_compare(old_backup_id, img->backup_id)) {
		dbg_printf("%T: %U: backup %U -> %U\n", img->node_id,
			   old_backup_id, img->backup_id);
		changed = 1;
	}

	if (changed) {
		hello_update(img);
		img->conf_change(img->cookie);
	}

	return changed;
}

void election_delay_timer_expiry(void *_img)
{
	struct iv_mcast_group *img = (struct iv_mcast_group *)_img;

	recalc_master_backup(img);
}

int neighbour_compare(struct iv_avl_node *_a, struct iv_avl_node *_b)
{
	struct neighbour *a = iv_container_of(_a, struct neighbour, an);
	struct neighbour *b = iv_container_of(_b, struct neighbour, an);

	return uuid_compare(a->node_id, b->node_id);
}

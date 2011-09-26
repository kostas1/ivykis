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

struct hello
{
	uuid_t			node_id;
	uint32_t		time_sec;
	uint32_t		time_nsec;
	uint16_t		hello_interval_ms;
	uint8_t			priority;
	uint8_t			__pad0;
	uuid_t			master_id;
	uuid_t			backup_id;
	uuid_t			neighbour[0];
};

struct neighbour
{
	struct iv_avl_node	an;
	uuid_t			node_id;
	struct iv_mcast_group	*img;
	struct timespec		last_hello_rx;
	uint32_t		priority;
	int			saw_me;
	int			claims_master;
	int			claims_backup;
	struct iv_timer		expiry_timer;
};

#define DEBUG

#ifdef DEBUG
#define dbg_printf		printf
#else
#define dbg_printf(...)
#endif


/* election.c */
int recalc_master_backup(struct iv_mcast_group *img);
void election_delay_timer_expiry(void *_img);
int neighbour_compare(struct iv_avl_node *_a, struct iv_avl_node *_b);

/* format.c */
void __add_format_strings(void);

/* rx.c */
void got_packet(void *_img);

/* time.c */
void timespec_add_ms(struct timespec *ts, int msec);
void timespec_add_range(struct timespec *ts, int min, int max);
void timespec_add_rand(struct timespec *ts, int avg);
long long timespec_diff(struct timespec *a, struct timespec *b);
int timespec_less_eq(struct timespec *a, struct timespec *b);

/* tx.c */
void xmit_hello_packet(struct iv_mcast_group *img);
void hello_periodic_timer_expiry(void *_img);
void hello_update_timer_expiry(void *_img);
void hello_update(struct iv_mcast_group *img);

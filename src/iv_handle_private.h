/*
 * ivykis, an event handling library
 * Copyright (C) 2012 Lennert Buytenhek
 * Dedicated to Marija Kulikova.
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

#ifndef __IV_HANDLE_PRIVATE_H
#define __IV_HANDLE_PRIVATE_H

struct iv_handle_ {
	/*
	 * User data.
	 */
	HANDLE			handle;
	void			*cookie;
	void			(*handler)(void *);

	/*
	 * Private data.
	 */
	int			registered;
	struct iv_list_head	list_active;
	union {
		struct {
			struct iv_handle_group	*grp;
			int			index;
		} apc;
		struct {
			struct iv_handle_group	*grp;
			int			index;
		} me;
		struct {
			struct iv_handle_group	*grp;
			int			index;
		} mp;
		struct {
			int			polling;
			struct iv_state		*st;
			HANDLE			rewait_handle;
			HANDLE			thr_handle;
		} simple;
	} u;
};

#define MAX_THREAD_HANDLES	(MAXIMUM_WAIT_OBJECTS - 1)

struct iv_handle_group {
	union {
		struct {
			struct iv_list_head	list;
			struct iv_list_head	list_recent_deleted;

			struct iv_state		*st;

			HANDLE			thr_handle;

			int			num_handles;
			int			active_handles;
			struct iv_handle_	*h[MAXIMUM_WAIT_OBJECTS];
			HANDLE			handle[MAXIMUM_WAIT_OBJECTS];
		} apc;
		struct {
			struct iv_list_head	list_all;
			struct iv_list_head	list_recent_deleted;

			struct iv_state		*st;

			HANDLE			thr_handle;
			HANDLE			thr_signal_handle;

			CRITICAL_SECTION	group_lock;
			struct iv_list_head	active_handles;
			struct iv_handle_	*h[MAX_THREAD_HANDLES];
			int			addition_pointer;
			int			num_deletions;

			int			quit;
			int			num_handles;
			HANDLE			hnd[MAXIMUM_WAIT_OBJECTS];
			int			have_active_handles;
		} me;
		struct {
			struct iv_list_head	list;
			struct iv_list_head	list_recent_deleted;

			struct iv_state		*st;

			HANDLE			thr_handle;

			int			num_handles;
			int			active_handles;
			struct iv_handle_	*h[MAXIMUM_WAIT_OBJECTS];
			HANDLE			handle[MAXIMUM_WAIT_OBJECTS];
		} mp;
	} u;
};

struct iv_handle_poll_method {
	char	*name;

	void	(*init)(struct iv_state *st);
	void	(*deinit)(struct iv_state *st);
	int	(*is_primary_thread)(struct iv_state *st);
	void	(*poll_and_run)(struct iv_state *st, struct timespec *to);
	void	(*quit)(struct iv_state *st);
	void	(*unquit)(struct iv_state *st);
	void	(*process_detach)(void);

	void	(*handle_init)(struct iv_handle_ *h);
	void	(*handle_register)(struct iv_handle_ *h);
	void	(*handle_unregister)(struct iv_handle_ *h);
	void	(*handle_set_handler)(struct iv_handle_ *h,
				      void (*handler)(void *));
};

extern struct iv_handle_poll_method *method;

struct iv_handle_poll_method iv_handle_poll_method_apc;
struct iv_handle_poll_method iv_handle_poll_method_apc_norebalance;
struct iv_handle_poll_method iv_handle_poll_method_me;
struct iv_handle_poll_method iv_handle_poll_method_mp;
struct iv_handle_poll_method iv_handle_poll_method_mp_norebalance;
struct iv_handle_poll_method iv_handle_poll_method_simple;


#endif

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
		} mp;
	} u;
};

struct iv_handle_group
{
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
		} mp;
	} u;
};

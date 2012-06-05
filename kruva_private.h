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
#include <stdlib.h>
#include <iv.h>

struct kru_stack {
	char		*name;
	int		(*socket)(struct kru_socket *, struct kru_stack *,
				  int domain, int type, int protocol);
	void		(*destroy)(struct kru_stack *);
	int		sock_priv_size;
	int		refcount;
	unsigned char	priv[0];
};

struct kru_socket_ops {
	void	(*set_handler_in)(struct kru_socket *kso,
				  void (*handler_in)(void *cookie));
	void	(*set_handler_out)(struct kru_socket *kso,
				   void (*handler_out)(void *cookie));
	int	(*close)(struct kru_socket *kso);
	int	(*bind)(struct kru_socket *kso, char *addr);
	int	(*connect)(struct kru_socket *kso, char *addr);
	int	(*recvfrom)(struct kru_socket *kso, void *buf, size_t len,
			    int flags, struct sockaddr *src_addr,
			    socklen_t *addrlen);
	int	(*sendto)(struct kru_socket *kso, const void *buf,
			  size_t len, int flags, struct sockaddr *src_addr,
			  socklen_t addrlen);
	int	(*shutdown)(struct kru_socket *kso, int how);
	int	(*get_error)(struct kru_socket *kso);
};

struct kru_socket {
	struct kru_socket_ops	*ops;
	void			*cookie;
	unsigned char		priv[0];
};

/* kruva.c */
struct kru_stack *kru_stack_new(int priv_size);
void *kru_stack_priv(struct kru_stack *kst);
void *kru_socket_priv(struct kru_socket *kso);

/* kruva_local.h */
struct kru_local_socket_priv {
	struct iv_fd		fd;
};
extern struct kru_socket_ops kruva_local_ops;

/* kruva_util.c */
int addr_to_sockaddr(struct sockaddr_storage *sa, char *addr);

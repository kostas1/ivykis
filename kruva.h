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

#include <arpa/inet.h>

/* kruva.c */
struct kru_stack;
struct kru_socket;

struct kru_socket *kru_stack_socket(struct kru_stack *kst,
				    int domain, int type, int protocol);
void kru_stack_get(struct kru_stack *kst);
void kru_stack_put(struct kru_stack *kst);

void kru_socket_set_cookie(struct kru_socket *kso, void *cookie);
void kru_socket_set_handler_in(struct kru_socket *kso,
			       void (*handler_in)(void *cookie));
void kru_socket_set_handler_out(struct kru_socket *kso,
				void (*handler_in)(void *cookie));
int kru_socket_close(struct kru_socket *kso);
int kru_socket_bind(struct kru_socket *kso, char *addr);
int kru_socket_connect(struct kru_socket *kso, char *addr);
int kru_socket_recv(struct kru_socket *kso, void *buf, size_t len, int flags);
int kru_socket_recvfrom(struct kru_socket *kso, void *buf, size_t len,
			int flags, struct sockaddr *src_addr,
			socklen_t *addrlen);
int kru_socket_send(struct kru_socket *kso, const void *buf, size_t len,
		    int flags);
int kru_socket_sendto(struct kru_socket *kso, const void *buf, size_t len,
		      int flags, struct sockaddr *src_addr, socklen_t addrlen);
int kru_socket_shutdown(struct kru_socket *kso, int how);
int kru_socket_get_error(struct kru_socket *kso);


/* kruva_local.c */
struct kru_stack *kru_stack_local(void);


/* kruva_netns.c */
struct kru_stack *kru_stack_netns_from_path(char *path);
struct kru_stack *kru_stack_netns_from_name(char *netnsname);
struct kru_stack *kru_stack_netns_from_tid(int tid);

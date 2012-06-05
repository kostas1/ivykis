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

#include "kruva.h"
#include "kruva_private.h"

/* stack ops ****************************************************************/
struct kru_stack *kru_stack_new(int priv_size)
{
	struct kru_stack *kst;

	kst = calloc(1, sizeof(*kst) + priv_size);
	if (kst == NULL)
		return NULL;

	kst->refcount = 1;

	return kst;
}

void *kru_stack_priv(struct kru_stack *kst)
{
	return &kst->priv;
}

struct kru_socket *
kru_stack_socket(struct kru_stack *kst, int domain, int type, int protocol)
{
	struct kru_socket *kso;

	kso = calloc(1, sizeof(*kso) + kst->sock_priv_size);
	if (kso == NULL)
		return NULL;

	kst->socket(kso, kst, domain, type, protocol);

	return kso;
}

void kru_stack_get(struct kru_stack *kst)
{
	kst->refcount++;
}

void kru_stack_put(struct kru_stack *kst)
{
	if (!--kst->refcount) {
		if (kst->destroy != NULL)
			kst->destroy(kst);
		free(kst);
	}
}


/* socket ops ***************************************************************/
void *kru_socket_priv(struct kru_socket *kso)
{
	return &kso->priv;
}

void kru_socket_set_cookie(struct kru_socket *kso, void *cookie)
{
	kso->cookie = cookie;
}

void kru_socket_set_handler_in(struct kru_socket *kso,
			       void (*handler_in)(void *cookie))
{
	kso->ops->set_handler_in(kso, handler_in);
}

void kru_socket_set_handler_out(struct kru_socket *kso,
				void (*handler_in)(void *cookie))
{
	kso->ops->set_handler_out(kso, handler_in);
}

int kru_socket_close(struct kru_socket *kso)
{
	int ret;

	ret = kso->ops->close(kso);
	free(kso);

	return ret;
}

int kru_socket_bind(struct kru_socket *kso, char *addr)
{
	return kso->ops->bind(kso, addr);
}

int kru_socket_connect(struct kru_socket *kso, char *addr)
{
	return kso->ops->connect(kso, addr);
}

int kru_socket_recv(struct kru_socket *kso, void *buf, size_t len, int flags)
{
	return kso->ops->recvfrom(kso, buf, len, flags, NULL, NULL);
}

int kru_socket_recvfrom(struct kru_socket *kso, void *buf, size_t len,
			int flags, struct sockaddr *src_addr,
			socklen_t *addrlen)
{
	return kso->ops->recvfrom(kso, buf, len, flags, src_addr, addrlen);
}

int kru_socket_send(struct kru_socket *kso, const void *buf, size_t len,
		    int flags)
{
	return kso->ops->sendto(kso, buf, len, flags, NULL, 0);
}

int kru_socket_sendto(struct kru_socket *kso, const void *buf, size_t len,
		      int flags, struct sockaddr *src_addr, socklen_t addrlen)
{
	return kso->ops->sendto(kso, buf, len, flags, src_addr, addrlen);
}

int kru_socket_shutdown(struct kru_socket *kso, int how)
{
	return kso->ops->shutdown(kso, how);
}

int kru_socket_get_error(struct kru_socket *kso)
{
	return kso->ops->get_error(kso);
}

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
#include <iv.h>
#include "kruva.h"
#include "kruva_private.h"

/* socket ops ***************************************************************/
static void kruva_local_set_handler_in(struct kru_socket *kso,
				       void (*handler_in)(void *cookie))
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);

	iv_fd_set_handler_in(&skpriv->fd, handler_in);
}

static void kruva_local_set_handler_out(struct kru_socket *kso,
					void (*handler_out)(void *cookie))
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);

	iv_fd_set_handler_out(&skpriv->fd, handler_out);
}

static int kruva_local_close(struct kru_socket *kso)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	int ret;

	iv_fd_unregister(&skpriv->fd);

	ret = close(skpriv->fd.fd);
	return (ret >= 0) ? ret : -errno;
}

static int kruva_local_bind(struct kru_socket *kso, char *addr)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	struct sockaddr_storage sa;
	int ret;

	ret = addr_to_sockaddr(&sa, addr);
	if (ret < 0)
		return ret;

	ret = bind(skpriv->fd.fd, (struct sockaddr *)&sa, sizeof(sa));
	return (ret >= 0) ? ret : -errno;
}

static int kruva_local_connect(struct kru_socket *kso, char *addr)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	struct sockaddr_storage sa;
	int ret;

	ret = addr_to_sockaddr(&sa, addr);
	if (ret < 0)
		return ret;

	ret = connect(skpriv->fd.fd, (struct sockaddr *)&sa, sizeof(sa));
	return (ret >= 0) ? ret : -errno;
}

static int kruva_local_recvfrom(struct kru_socket *kso, void *buf, size_t len,
				int flags, struct sockaddr *src_addr,
				socklen_t *addrlen)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	int ret;

	ret = recvfrom(skpriv->fd.fd, buf, len, flags, src_addr, addrlen);
	return (ret >= 0) ? ret : -errno;
}

static int kruva_local_sendto(struct kru_socket *kso, const void *buf,
			      size_t len, int flags, struct sockaddr *src_addr,
			      socklen_t addrlen)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	int ret;

	ret = sendto(skpriv->fd.fd, buf, len, flags, src_addr, addrlen);
	return (ret >= 0) ? ret : -errno;
}

static int kruva_local_shutdown(struct kru_socket *kso, int how)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	int ret;

	ret = shutdown(skpriv->fd.fd, how);
	return (ret >= 0) ? ret : -errno;
}

static int kruva_local_get_error(struct kru_socket *kso)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	socklen_t len;
	int ret;

	len = sizeof(ret);
	if (getsockopt(skpriv->fd.fd, SOL_SOCKET, SO_ERROR, &ret, &len) < 0)
		return errno;

	return ret;
}

struct kru_socket_ops kruva_local_ops = {
	.set_handler_in		= kruva_local_set_handler_in,
	.set_handler_out	= kruva_local_set_handler_out,
	.close			= kruva_local_close,
	.bind			= kruva_local_bind,
	.connect		= kruva_local_connect,
	.recvfrom		= kruva_local_recvfrom,
	.sendto			= kruva_local_sendto,
	.shutdown		= kruva_local_shutdown,
	.get_error		= kruva_local_get_error,
};


/* stack ops ****************************************************************/
static int kru_local_socket(struct kru_socket *kso, struct kru_stack *kst,
			    int domain, int type, int protocol)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	int fd;

	fd = socket(domain, type, protocol);
	if (fd < 0)
		return -errno;

	kso->ops = &kruva_local_ops;

	IV_FD_INIT(&skpriv->fd);
	skpriv->fd.fd = fd;
	iv_fd_register(&skpriv->fd);

	return 0;
}

struct kru_stack *kru_stack_local(void)
{
	struct kru_stack *kst;

	kst = kru_stack_new(0);
	if (kst != NULL) {
		kst->name = "local";
		kst->socket = kru_local_socket;
		kst->sock_priv_size = sizeof(struct kru_local_socket_priv);
	}

	return kst;
}

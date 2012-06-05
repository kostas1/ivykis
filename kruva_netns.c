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

#define _GNU_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <iv.h>
#include <sched.h>
#include <string.h>
#include <syslog.h>
#include <sys/syscall.h>
#include "kruva.h"
#include "kruva_private.h"

struct kru_stack_netns {
	int	netnsfd;
};

static int gettid(void)
{
	return syscall(__NR_gettid);
}

static int kru_tid_netnsfd(int tid)
{
	char path[128];

	snprintf(path, sizeof(path), "/proc/%d/task/%d/ns/net", tid, tid);

	return open(path, O_CLOEXEC | O_RDONLY);
}

static int kru_netns_socket(struct kru_socket *kso, struct kru_stack *kst,
			    int domain, int type, int protocol)
{
	struct kru_local_socket_priv *skpriv = kru_socket_priv(kso);
	struct kru_stack_netns *kstl = kru_stack_priv(kst);
	int oldnetns;
	int fd;
	int err;

	oldnetns = kru_tid_netnsfd(gettid());
	if (oldnetns < 0)
		return -errno;

	if (setns(kstl->netnsfd, CLONE_NEWNET) < 0) {
		close(oldnetns);
		return -errno;
	}

	fd = socket(domain, type, protocol);
	err = errno;

	if (setns(oldnetns, CLONE_NEWNET) < 0) {
		syslog(LOG_CRIT, "kru_netns_socket: failed "
				 "to restore the previous netns");
		abort();
	}

	close(oldnetns);

	if (fd < 0)
		return -err;

	kso->ops = &kruva_local_ops;

	IV_FD_INIT(&skpriv->fd);
	skpriv->fd.fd = fd;
	iv_fd_register(&skpriv->fd);

	return 0;
}

static void kru_netns_destroy(struct kru_stack *kst)
{
	struct kru_stack_netns *kstl = kru_stack_priv(kst);

	close(kstl->netnsfd);
}

static struct kru_stack *__kru_stack_netns(char *name, char *path)
{
	int netns;
	struct kru_stack *kst;
	struct kru_stack_netns *kstl;

	netns = open(path, O_CLOEXEC | O_RDONLY);
	if (netns < 0)
		return NULL;

	kst = kru_stack_new(sizeof(struct kru_stack_netns));
	if (kst == NULL) {
		close(netns);
		return NULL;
	}

	kst->name = strdup(name);
	kst->socket = kru_netns_socket;
	kst->destroy = kru_netns_destroy;
	kst->sock_priv_size = sizeof(struct kru_local_socket_priv);

	kstl = kru_stack_priv(kst);
	kstl->netnsfd = netns;

	return kst;
}

struct kru_stack *kru_stack_netns_from_path(char *path)
{
	char name[strlen(path) + 32];

	snprintf(name, sizeof(name), "netns path: %s", path);

	return __kru_stack_netns(name, path);
}

struct kru_stack *kru_stack_netns_from_name(char *netnsname)
{
	char name[strlen(netnsname) + 32];
	char path[strlen(netnsname) + 32];

	snprintf(name, sizeof(name), "netns: %s", netnsname);
	snprintf(path, sizeof(path), "/var/run/netns/%s", netnsname);

	return __kru_stack_netns(name, path);
}

struct kru_stack *kru_stack_netns_from_tid(int tid)
{
	char name[128];
	char path[128];

	snprintf(name, sizeof(name), "netns: tid %d", tid);
	snprintf(path, sizeof(path), "/proc/%d/task/%d/ns/net", tid, tid);

	return __kru_stack_netns(name, path);
}

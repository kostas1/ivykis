/*
 * ivykis, an event handling library
 * Copyright (C) 2011 Lennert Buytenhek
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
#include <fcntl.h>
#include <iv.h>
#include <iv_thread.h>
#include <string.h>
#include <syslog.h>
#include <sys/syscall.h>
#include "iv_netns.h"


/* common code **************************************************************/
static int iv_netns_tid_or_die(void)
{
	int tid;

	tid = syscall(__NR_gettid);
	if (tid < 0) {
		syslog(LOG_CRIT, "iv_netns_tid_or_die: gettid() returned "
				 "error %d[%s]", errno, strerror(errno));
		abort();
	}

	return tid;
}


/* implementation using helper thread ***************************************/
enum status {
	STATUS_STARTING,
	STATUS_READY,
	STATUS_DEAD,
};

enum request {
	REQ_DIE,
	REQ_SOCKET,
};

struct iv_netns_thread_request {
	int	request;
	int	domain;
	int	type;
	int	protocol;

	int	ret;
	int	err;
};

static void iv_netns_thread(void *_ns)
{
	struct iv_netns *ns = _ns;
	int dead;

	if (unshare(CLONE_NEWNET) < 0) {
		syslog(LOG_CRIT, "iv_netns_thread: got error from unshare()");
		abort();
	}

	pthread_mutex_lock(&ns->lock);

	ns->tid = iv_netns_tid_or_die();
	ns->status = STATUS_READY;
	pthread_cond_signal(&ns->cond);

	for (dead = 0; !dead; ) {
		struct iv_netns_thread_request *req;

		while (ns->req == NULL) {
			if (pthread_cond_wait(&ns->cond, &ns->lock) < 0) {
				syslog(LOG_CRIT, "iv_netns_thread: got error "
						 "from pthread_cond_wait()");
				abort();
			}
		}

		req = ns->req;
		ns->req = NULL;

		if (req->request == REQ_DIE) {
			req->ret = 0;
			dead = 1;
			break;
		} else if (req->request == REQ_SOCKET) {
			req->ret = socket(req->domain, req->type,
					  req->protocol);
			req->err = errno;
		} else {
			syslog(LOG_CRIT, "iv_netns_thread: unknown "
					 "request/status type");
			abort();
		}

		ns->status = STATUS_READY;
		pthread_cond_signal(&ns->cond);
	}

	ns->status = STATUS_DEAD;
	pthread_cond_signal(&ns->cond);

	pthread_mutex_unlock(&ns->lock);
}

static void iv_netns_thread_create(struct iv_netns *this)
{
	pthread_mutex_init(&this->lock, NULL);
	pthread_cond_init(&this->cond, NULL);
	this->status = STATUS_STARTING;
	this->req = NULL;

	iv_thread_create("namespace thread", iv_netns_thread, (void *)this);

	pthread_mutex_lock(&this->lock);

	while (this->status == STATUS_STARTING) {
		if (pthread_cond_wait(&this->cond, &this->lock) < 0) {
			syslog(LOG_CRIT, "iv_netns_create: got error "
					 "from pthread_cond_wait()");
			abort();
		}
	}

	if (this->status != STATUS_READY) {
		syslog(LOG_CRIT, "iv_netns_create: namespace thread "
				 "did not move to ready status");
		abort();
	}

	pthread_mutex_unlock(&this->lock);
}

static void iv_netns_thread_destroy(struct iv_netns *this)
{
	struct iv_netns_thread_request req;

	req.request = REQ_DIE;

	pthread_mutex_lock(&this->lock);

	this->req = &req;
	pthread_cond_signal(&this->cond);

	while (this->status != STATUS_DEAD) {
		if (pthread_cond_wait(&this->cond, &this->lock) < 0) {
			syslog(LOG_CRIT, "iv_netns_destroy: got error "
					 "from pthread_cond_wait()");
			abort();
		}
	}

	pthread_mutex_unlock(&this->lock);

	pthread_mutex_destroy(&this->lock);
	pthread_cond_destroy(&this->cond);
}

static int iv_netns_thread_socket(struct iv_netns *this, int domain,
				  int type, int protocol)
{
	struct iv_netns_thread_request req;

	req.request = REQ_SOCKET;
	req.domain = domain;
	req.type = type;
	req.protocol = protocol;

	pthread_mutex_lock(&this->lock);

	this->req = &req;
	pthread_cond_signal(&this->cond);

	while (this->req != NULL) {
		if (pthread_cond_wait(&this->cond, &this->lock) < 0) {
			syslog(LOG_CRIT, "iv_netns_socket: got error "
					 "from pthread_cond_wait()");
			abort();
		}
	}

	if (this->status != STATUS_READY) {
		syslog(LOG_CRIT, "iv_netns_socket: worker thread "
				 "failed to execute request");
		abort();
	}

	pthread_mutex_unlock(&this->lock);

	errno = req.err;

	return req.ret;
}


/* implementation using setns(2) ********************************************/
static int __iv_netns_ns_fd(int tid)
{
	char path[128];

	snprintf(path, sizeof(path), "/proc/self/task/%d/ns/net", tid);

	return open(path, O_CLOEXEC | O_RDONLY);
}

static int iv_netns_ns_fd(int tid)
{
	int fd;

	fd = __iv_netns_ns_fd(tid);
	if (fd < 0) {
		syslog(LOG_CRIT, "iv_netns_ns_fd: open() returned error "
				 "%d[%s]", errno, strerror(errno));
		abort();
	}

	return fd;
}

static int iv_netns_setns_create(struct iv_netns *this)
{
	int tid;
	int oldns;

	tid = iv_netns_tid_or_die();

	oldns = __iv_netns_ns_fd(tid);
	if (oldns < 0)
		return -1;

	if (unshare(CLONE_NEWNET) < 0) {
		syslog(LOG_CRIT, "iv_netns_create: failed to "
				 "unshare the current netns");
		abort();
	}

	this->nsfd = iv_netns_ns_fd(tid);

	if (setns(oldns, CLONE_NEWNET) < 0) {
		syslog(LOG_CRIT, "iv_netns_create: failed to "
				 "restore the previous netns");
		abort();
	}

	close(oldns);

	return 0;
}

static void iv_netns_setns_destroy(struct iv_netns *this)
{
	close(this->nsfd);
}

static int iv_netns_setns_socket(struct iv_netns *this, int domain,
				 int type, int protocol)
{
	int oldns;
	int ret;
	int err;

	oldns = iv_netns_ns_fd(iv_netns_tid_or_die());

	if (setns(this->nsfd, CLONE_NEWNET) < 0) {
		syslog(LOG_CRIT, "iv_netns_socket: failed to "
				 "switch to target netns");
		abort();
	}

	ret = socket(domain, type, protocol);
	err = errno;

	if (setns(oldns, CLONE_NEWNET) < 0) {
		syslog(LOG_CRIT, "iv_netns_socket: failed to "
				 "restore the previous netns");
		abort();
	}

	close(oldns);

	errno = err;

	return ret;
}


/* entry points *************************************************************/
static int use_helper_thread;

void iv_netns_create(struct iv_netns *this)
{
	if (!use_helper_thread && iv_netns_setns_create(this) < 0)
		use_helper_thread = 1;

	if (use_helper_thread)
		iv_netns_thread_create(this);
}

void iv_netns_destroy(struct iv_netns *this)
{
	if (use_helper_thread)
		iv_netns_thread_destroy(this);
	else
		iv_netns_setns_destroy(this);
}

int iv_netns_socket(struct iv_netns *this, int domain, int type, int protocol)
{
	int ret;

	if (use_helper_thread)
		ret = iv_netns_thread_socket(this, domain, type, protocol);
	else
		ret = iv_netns_setns_socket(this, domain, type, protocol);

	return ret;
}

int iv_netns_get_fd(struct iv_netns *this)
{
	return use_helper_thread ? -1 : this->nsfd;
}

int iv_netns_get_pid(struct iv_netns *this)
{
	return use_helper_thread ? this->tid : -1;
}

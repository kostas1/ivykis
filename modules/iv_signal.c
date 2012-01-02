/*
 * ivykis, an event handling library
 * Copyright (C) 2010, 2011 Lennert Buytenhek
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

#include <stdio.h>
#include <stdlib.h>
#include <iv_list.h>
#include <iv_signal.h>
#include <iv_tls.h>
#include <pthread.h>
#include <inttypes.h>
#include "spinlock.h"

#define MAX_SIGS		32

static spinlock_t sig_lock;
static int total_num_interests[MAX_SIGS];
static struct iv_avl_tree process_sigs;
static sigset_t sig_mask_fork;

static int iv_signal_compare(struct iv_avl_node *_a, struct iv_avl_node *_b)
{
	struct iv_signal *a = iv_container_of(_a, struct iv_signal, an);
	struct iv_signal *b = iv_container_of(_b, struct iv_signal, an);

	if (a->signum < b->signum)
		return -1;
	if (a->signum > b->signum)
		return 1;

	if ((a->flags & IV_SIGNAL_FLAG_EXCLUSIVE) &&
	    !(b->flags & IV_SIGNAL_FLAG_EXCLUSIVE))
		return -1;
	if (!(a->flags & IV_SIGNAL_FLAG_EXCLUSIVE) &&
	    (b->flags & IV_SIGNAL_FLAG_EXCLUSIVE))
		return 1;

	if (a < b)
		return -1;
	if (a > b)
		return 1;

	return 0;
}

static void iv_signal_prepare(void)
{
	sigset_t mask;

	spin_lock_sigmask(&sig_lock, &mask);
	sig_mask_fork = mask;
}

static void iv_signal_parent(void)
{
	sigset_t mask;

	mask = sig_mask_fork;
	spin_unlock_sigmask(&sig_lock, &mask);
}

static void iv_signal_child(void)
{
	struct sigaction sa;
	int last_signum;
	struct iv_avl_node *an;

	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	last_signum = -1;
	iv_avl_tree_for_each (an, &process_sigs) {
		struct iv_signal *is;

		is = iv_container_of(an, struct iv_signal, an);
		if (is->signum != last_signum) {
			sigaction(is->signum, &sa, NULL);
			last_signum = is->signum;
		}
	}

	process_sigs.root = NULL;

	iv_signal_parent();
}

struct iv_signal_thr_info {
	struct iv_avl_tree	thr_sigs;
};

static void iv_signal_tls_init_thread(void *_tinfo)
{
	struct iv_signal_thr_info *tinfo = _tinfo;

	INIT_IV_AVL_TREE(&tinfo->thr_sigs, iv_signal_compare);
}

static struct iv_tls_user iv_signal_tls_user = {
	.sizeof_state	= sizeof(struct iv_signal_thr_info),
	.init_thread	= iv_signal_tls_init_thread,
};

static void iv_signal_init(void) __attribute__((constructor));
static void iv_signal_init(void)
{
	spin_init(&sig_lock);

	INIT_IV_AVL_TREE(&process_sigs, iv_signal_compare);

	pthread_atfork(iv_signal_prepare, iv_signal_parent, iv_signal_child);

	iv_tls_user_register(&iv_signal_tls_user);
}

static struct iv_avl_node *
__iv_signal_find_first(struct iv_avl_tree *tree, int signum)
{
	struct iv_avl_node *iter;
	struct iv_avl_node *best;

	for (iter = tree->root, best = NULL; iter != NULL; ) {
		struct iv_signal *is;

		is = iv_container_of(iter, struct iv_signal, an);
		if (signum == is->signum)
			best = iter;

		if (signum <= is->signum)
			iter = iter->left;
		else
			iter = iter->right;
	}

	return best;
}

static int __iv_signal_do_wake(struct iv_avl_tree *tree, int signum)
{
	struct iv_avl_node *an;
	int woken;

	an = __iv_signal_find_first(tree, signum);
	woken = 0;
	while (an != NULL) {
		struct iv_signal *is;

		is = iv_container_of(an, struct iv_signal, an);
		if (is->signum != signum)
			break;

		is->active = 1;
		iv_event_raw_post(&is->ev);

		woken++;

		if (is->flags & IV_SIGNAL_FLAG_EXCLUSIVE)
			break;

		an = iv_avl_tree_next(an);
	}

	return woken;
}

static void iv_signal_handler(int signum)
{
	struct iv_signal_thr_info *tinfo;

	tinfo = iv_tls_user_ptr(&iv_signal_tls_user);
	if (tinfo != NULL && __iv_signal_do_wake(&tinfo->thr_sigs, signum))
		return;

	spin_lock(&sig_lock);
	__iv_signal_do_wake(&process_sigs, signum);
	spin_unlock(&sig_lock);
}

static void iv_signal_event(void *_this)
{
	struct iv_signal *this = _this;
	sigset_t mask;

	sigfillset(&mask);
	pthread_sigmask(SIG_BLOCK, &mask, &mask);

	if (!(this->flags & IV_SIGNAL_FLAG_THIS_THREAD)) {
		spin_lock(&sig_lock);
		this->active = 0;
		spin_unlock(&sig_lock);
	} else {
		this->active = 0;
	}

	pthread_sigmask(SIG_SETMASK, &mask, NULL);

	this->handler(this->cookie);
}

static struct iv_avl_tree *iv_signal_tree(struct iv_signal *this)
{
	if (this->flags & IV_SIGNAL_FLAG_THIS_THREAD) {
		struct iv_signal_thr_info *tinfo;

		tinfo = iv_tls_user_ptr(&iv_signal_tls_user);
		return &tinfo->thr_sigs;
	} else {
		return &process_sigs;
	}
}

int iv_signal_register(struct iv_signal *this)
{
	sigset_t mask;

	IV_EVENT_RAW_INIT(&this->ev);
	this->ev.cookie = this;
	this->ev.handler = iv_signal_event;
	iv_event_raw_register(&this->ev);

	this->active = 0;

	spin_lock_sigmask(&sig_lock, &mask);

	iv_avl_tree_insert(iv_signal_tree(this), &this->an);

	if (!total_num_interests[this->signum]++) {
		struct sigaction sa;

		sa.sa_handler = iv_signal_handler;
		sigfillset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		sigaction(this->signum, &sa, NULL);
	}

	spin_unlock_sigmask(&sig_lock, &mask);

	return 0;
}

void iv_signal_unregister(struct iv_signal *this)
{
	sigset_t mask;

	spin_lock_sigmask(&sig_lock, &mask);

	iv_avl_tree_delete(iv_signal_tree(this), &this->an);

	if (!--total_num_interests[this->signum]) {
		struct sigaction sa;

		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(this->signum, &sa, NULL);
	} else if ((this->flags & IV_SIGNAL_FLAG_EXCLUSIVE) && this->active) {
		__iv_signal_do_wake(iv_signal_tree(this), this->signum);
	}

	spin_unlock_sigmask(&sig_lock, &mask);

	iv_event_raw_unregister(&this->ev);
}

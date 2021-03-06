/*
 * Copyright 2013 GRNET S.A. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   1. Redistributions of source code must retain the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY GRNET S.A. ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL GRNET S.A OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and
 * documentation are those of the authors and should not be
 * interpreted as representing official policies, either expressed
 * or implied, of GRNET S.A.
 */

#include <xseg/xtypes.h>
#include <xseg/xworkq.h>


int xworkq_init(struct xworkq *wq, struct xlock *lock, uint32_t flags)
{
	wq->lock = lock;
	wq->flags = flags;
	xlock_release(&wq->q_lock);
	wq->q = xtypes_malloc(sizeof(struct xq));
	if (!wq->q)
		return -1;
	if (!xq_alloc_empty(wq->q, 8)){
		xtypes_free(wq->q);
		return -1;
	}
	return 0;
}

void xworkq_destroy(struct xworkq *wq)
{
	//what about pending works ? 
	xq_free(wq->q);
	xtypes_free(wq->q);
}

int __xworkq_enqueue(struct xworkq *wq, struct work *w)
{
	//enqueue and resize if necessary
	xqindex r;
	struct xq *newq;
	xlock_acquire(&wq->q_lock, 4);
	r = __xq_append_tail(wq->q, (xqindex)w);
	if (r == Noneidx){
		newq = xtypes_malloc(sizeof(struct xq));
		if (!newq){
			r = Noneidx;
			goto out;
		}
		if (!xq_alloc_empty(newq, wq->q->size*2)){
			xtypes_free(newq);
			r = Noneidx;
			goto out;
		}
		if (__xq_resize(wq->q, newq) == Noneidx){
			xq_free(newq);
			xtypes_free(newq);
			r = Noneidx;
			goto out;
		}
		xtypes_free(wq->q);
		wq->q = newq;
		r = __xq_append_tail(wq->q, (xqindex)w);
	}
out:
	xlock_release(&wq->q_lock);

	return ((r == Noneidx)? -1 : 0);
}

int xworkq_enqueue(struct xworkq *wq, void (*job_fn)(void *q, void *arg), void *job)
{
	//maybe use xobj
	struct work *work = xtypes_malloc(sizeof(struct work));
	if (!work)
		return -1;
	work->job_fn = job_fn;
	work->job = job;
	if (__xworkq_enqueue(wq, work) < 0)
		return -1;
	return 0;
}

void xworkq_signal(struct xworkq *wq)
{
	xqindex xqi;
	struct work *w;
	while (xq_count(wq->q)){
		if (wq->lock && !xlock_try_lock(wq->lock, 2))
			return;

		xlock_acquire(&wq->q_lock, 3);
		xqi = __xq_pop_head(wq->q);
		xlock_release(&wq->q_lock);

		while (xqi != Noneidx){
			w = (struct work *)xqi;
			w->job_fn(wq, w->job);
			xtypes_free(w);
			xlock_acquire(&wq->q_lock, 3);
			xqi = __xq_pop_head(wq->q);
			xlock_release(&wq->q_lock);
		}
		if (wq->lock)
			xlock_release(wq->lock);
	}

	return;
}

#ifdef __KERNEL__
#include <linux/module.h>
#include <xtypes/xworkq_exports.h>
#endif

/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: mthca_provider.h 1349 2004-12-16 21:09:43Z roland $
 */

#ifndef MTHCA_PROVIDER_H
#define MTHCA_PROVIDER_H

#include <ib_verbs.h>
#include <ib_pack.h>

#define MTHCA_MPT_FLAG_ATOMIC        (1 << 14)
#define MTHCA_MPT_FLAG_REMOTE_WRITE  (1 << 13)
#define MTHCA_MPT_FLAG_REMOTE_READ   (1 << 12)
#define MTHCA_MPT_FLAG_LOCAL_WRITE   (1 << 11)
#define MTHCA_MPT_FLAG_LOCAL_READ    (1 << 10)

struct mthca_buf_list {
	void *buf;
	DECLARE_PCI_UNMAP_ADDR(mapping)
};

struct mthca_uar {
	unsigned long pfn;
	int           index;
};

struct mthca_mr {
	struct ib_mr ibmr;
	int order;
	u32 first_seg;
};

struct mthca_pd {
	struct ib_pd    ibpd;
	u32             pd_num;
	atomic_t        sqp_count;
	struct mthca_mr ntmr;
};

struct mthca_eq {
	struct mthca_dev      *dev;
	int                    eqn;
	u32                    eqn_mask;
	u32                    cons_index;
	u16                    msi_x_vector;
	u16                    msi_x_entry;
	int                    have_irq;
	int                    nent;
	struct mthca_buf_list *page_list;
	struct mthca_mr        mr;
};

struct mthca_av;

enum mthca_ah_type {
	MTHCA_AH_ON_HCA,
	MTHCA_AH_PCI_POOL,
	MTHCA_AH_KMALLOC
};

struct mthca_ah {
	struct ib_ah       ibah;
	enum mthca_ah_type type;
	u32                key;
	struct mthca_av   *av;
	dma_addr_t         avdma;
};

/*
 * Quick description of our CQ/QP locking scheme:
 *
 * We have one global lock that protects dev->cq/qp_table.  Each
 * struct mthca_cq/qp also has its own lock.  An individual qp lock
 * may be taken inside of an individual cq lock.  Both cqs attached to
 * a qp may be locked, with the send cq locked first.  No other
 * nesting should be done.
 *
 * Each struct mthca_cq/qp also has an atomic_t ref count.  The
 * pointer from the cq/qp_table to the struct counts as one reference.
 * This reference also is good for access through the consumer API, so
 * modifying the CQ/QP etc doesn't need to take another reference.
 * Access because of a completion being polled does need a reference.
 *
 * Finally, each struct mthca_cq/qp has a wait_queue_head_t for the
 * destroy function to sleep on.
 *
 * This means that access from the consumer API requires nothing but
 * taking the struct's lock.
 *
 * Access because of a completion event should go as follows:
 * - lock cq/qp_table and look up struct
 * - increment ref count in struct
 * - drop cq/qp_table lock
 * - lock struct, do your thing, and unlock struct
 * - decrement ref count; if zero, wake up waiters
 *
 * To destroy a CQ/QP, we can do the following:
 * - lock cq/qp_table, remove pointer, unlock cq/qp_table lock
 * - decrement ref count
 * - wait_event until ref count is zero
 *
 * It is the consumer's responsibilty to make sure that no QP
 * operations (WQE posting or state modification) are pending when the
 * QP is destroyed.  Also, the consumer must make sure that calls to
 * qp_modify are serialized.
 *
 * Possible optimizations (wait for profile data to see if/where we
 * have locks bouncing between CPUs):
 * - split cq/qp table lock into n separate (cache-aligned) locks,
 *   indexed (say) by the page in the table
 * - split QP struct lock into three (one for common info, one for the
 *   send queue and one for the receive queue)
 */

struct mthca_cq {
	struct ib_cq           ibcq;
	spinlock_t             lock;
	atomic_t               refcount;
	int                    cqn;
	u32                    cons_index;
	int                    is_direct;

	/* Next fields are Arbel only */
	int                    set_ci_db_index;
	u32                   *set_ci_db;
	int                    arm_db_index;
	u32                   *arm_db;
	int                    arm_sn;

	union {
		struct mthca_buf_list direct;
		struct mthca_buf_list *page_list;
	}                      queue;
	struct mthca_mr        mr;
	wait_queue_head_t      wait;
};

struct mthca_wq {
	spinlock_t lock;
	int        max;
	unsigned   next_ind;
	unsigned   last_comp;
	unsigned   head;
	unsigned   tail;
	void      *last;
	int        max_gs;
	int        wqe_shift;

	int        db_index;	/* Arbel only */
	u32       *db;
};

struct mthca_qp {
	struct ib_qp           ibqp;
	atomic_t               refcount;
	u32                    qpn;
	int                    is_direct;
	u8                     transport;
	u8                     state;
	u8                     atomic_rd_en;
	u8                     resp_depth;

	struct mthca_mr        mr;

	struct mthca_wq        rq;
	struct mthca_wq        sq;
	enum ib_sig_type       sq_policy;
	int                    send_wqe_offset;

	u64                   *wrid;
	union {
		struct mthca_buf_list direct;
		struct mthca_buf_list *page_list;
	}                      queue;

	wait_queue_head_t      wait;
};

struct mthca_sqp {
	struct mthca_qp qp;
	int             port;
	int             pkey_index;
	u32             qkey;
	u32             send_psn;
	struct ib_ud_header ud_header;
	int             header_buf_size;
	void           *header_buf;
	dma_addr_t      header_dma;
};

static inline struct mthca_mr *to_mmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct mthca_mr, ibmr);
}

static inline struct mthca_pd *to_mpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct mthca_pd, ibpd);
}

static inline struct mthca_ah *to_mah(struct ib_ah *ibah)
{
	return container_of(ibah, struct mthca_ah, ibah);
}

static inline struct mthca_cq *to_mcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct mthca_cq, ibcq);
}

static inline struct mthca_qp *to_mqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct mthca_qp, ibqp);
}

static inline struct mthca_sqp *to_msqp(struct mthca_qp *qp)
{
	return container_of(qp, struct mthca_sqp, qp);
}

#endif /* MTHCA_PROVIDER_H */

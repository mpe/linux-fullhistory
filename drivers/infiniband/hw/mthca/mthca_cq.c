/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
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
 * $Id: mthca_cq.c 1369 2004-12-20 16:17:07Z roland $
 */

#include <linux/init.h>
#include <linux/hardirq.h>

#include <ib_pack.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"
#include "mthca_memfree.h"

enum {
	MTHCA_MAX_DIRECT_CQ_SIZE = 4 * PAGE_SIZE
};

enum {
	MTHCA_CQ_ENTRY_SIZE = 0x20
};

/*
 * Must be packed because start is 64 bits but only aligned to 32 bits.
 */
struct mthca_cq_context {
	u32 flags;
	u64 start;
	u32 logsize_usrpage;
	u32 error_eqn;		/* Tavor only */
	u32 comp_eqn;
	u32 pd;
	u32 lkey;
	u32 last_notified_index;
	u32 solicit_producer_index;
	u32 consumer_index;
	u32 producer_index;
	u32 cqn;
	u32 ci_db;		/* Arbel only */
	u32 state_db;		/* Arbel only */
	u32 reserved;
} __attribute__((packed));

#define MTHCA_CQ_STATUS_OK          ( 0 << 28)
#define MTHCA_CQ_STATUS_OVERFLOW    ( 9 << 28)
#define MTHCA_CQ_STATUS_WRITE_FAIL  (10 << 28)
#define MTHCA_CQ_FLAG_TR            ( 1 << 18)
#define MTHCA_CQ_FLAG_OI            ( 1 << 17)
#define MTHCA_CQ_STATE_DISARMED     ( 0 <<  8)
#define MTHCA_CQ_STATE_ARMED        ( 1 <<  8)
#define MTHCA_CQ_STATE_ARMED_SOL    ( 4 <<  8)
#define MTHCA_EQ_STATE_FIRED        (10 <<  8)

enum {
	MTHCA_ERROR_CQE_OPCODE_MASK = 0xfe
};

enum {
	SYNDROME_LOCAL_LENGTH_ERR 	 = 0x01,
	SYNDROME_LOCAL_QP_OP_ERR  	 = 0x02,
	SYNDROME_LOCAL_EEC_OP_ERR 	 = 0x03,
	SYNDROME_LOCAL_PROT_ERR   	 = 0x04,
	SYNDROME_WR_FLUSH_ERR     	 = 0x05,
	SYNDROME_MW_BIND_ERR      	 = 0x06,
	SYNDROME_BAD_RESP_ERR     	 = 0x10,
	SYNDROME_LOCAL_ACCESS_ERR 	 = 0x11,
	SYNDROME_REMOTE_INVAL_REQ_ERR 	 = 0x12,
	SYNDROME_REMOTE_ACCESS_ERR 	 = 0x13,
	SYNDROME_REMOTE_OP_ERR     	 = 0x14,
	SYNDROME_RETRY_EXC_ERR 		 = 0x15,
	SYNDROME_RNR_RETRY_EXC_ERR 	 = 0x16,
	SYNDROME_LOCAL_RDD_VIOL_ERR 	 = 0x20,
	SYNDROME_REMOTE_INVAL_RD_REQ_ERR = 0x21,
	SYNDROME_REMOTE_ABORTED_ERR 	 = 0x22,
	SYNDROME_INVAL_EECN_ERR 	 = 0x23,
	SYNDROME_INVAL_EEC_STATE_ERR 	 = 0x24
};

struct mthca_cqe {
	u32 my_qpn;
	u32 my_ee;
	u32 rqpn;
	u16 sl_g_mlpath;
	u16 rlid;
	u32 imm_etype_pkey_eec;
	u32 byte_cnt;
	u32 wqe;
	u8  opcode;
	u8  is_send;
	u8  reserved;
	u8  owner;
};

struct mthca_err_cqe {
	u32 my_qpn;
	u32 reserved1[3];
	u8  syndrome;
	u8  reserved2;
	u16 db_cnt;
	u32 reserved3;
	u32 wqe;
	u8  opcode;
	u8  reserved4[2];
	u8  owner;
};

#define MTHCA_CQ_ENTRY_OWNER_SW      (0 << 7)
#define MTHCA_CQ_ENTRY_OWNER_HW      (1 << 7)

#define MTHCA_TAVOR_CQ_DB_INC_CI       (1 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT      (2 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT_SOL  (3 << 24)
#define MTHCA_TAVOR_CQ_DB_SET_CI       (4 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT_MULT (5 << 24)

#define MTHCA_ARBEL_CQ_DB_REQ_NOT_SOL  (1 << 24)
#define MTHCA_ARBEL_CQ_DB_REQ_NOT      (2 << 24)
#define MTHCA_ARBEL_CQ_DB_REQ_NOT_MULT (3 << 24)

static inline struct mthca_cqe *get_cqe(struct mthca_cq *cq, int entry)
{
	if (cq->is_direct)
		return cq->queue.direct.buf + (entry * MTHCA_CQ_ENTRY_SIZE);
	else
		return cq->queue.page_list[entry * MTHCA_CQ_ENTRY_SIZE / PAGE_SIZE].buf
			+ (entry * MTHCA_CQ_ENTRY_SIZE) % PAGE_SIZE;
}

static inline struct mthca_cqe *cqe_sw(struct mthca_cq *cq, int i)
{
	struct mthca_cqe *cqe = get_cqe(cq, i);
	return MTHCA_CQ_ENTRY_OWNER_HW & cqe->owner ? NULL : cqe;
}

static inline struct mthca_cqe *next_cqe_sw(struct mthca_cq *cq)
{
	return cqe_sw(cq, cq->cons_index & cq->ibcq.cqe);
}

static inline void set_cqe_hw(struct mthca_cqe *cqe)
{
	cqe->owner = MTHCA_CQ_ENTRY_OWNER_HW;
}

/*
 * incr is ignored in native Arbel (mem-free) mode, so cq->cons_index
 * should be correct before calling update_cons_index().
 */
static inline void update_cons_index(struct mthca_dev *dev, struct mthca_cq *cq,
				     int incr)
{
	u32 doorbell[2];

	if (dev->hca_type == ARBEL_NATIVE) {
		*cq->set_ci_db = cpu_to_be32(cq->cons_index);
		wmb();
	} else {
		doorbell[0] = cpu_to_be32(MTHCA_TAVOR_CQ_DB_INC_CI | cq->cqn);
		doorbell[1] = cpu_to_be32(incr - 1);

		mthca_write64(doorbell,
			      dev->kar + MTHCA_CQ_DOORBELL,
			      MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
	}
}

void mthca_cq_event(struct mthca_dev *dev, u32 cqn)
{
	struct mthca_cq *cq;

	cq = mthca_array_get(&dev->cq_table.cq, cqn & (dev->limits.num_cqs - 1));

	if (!cq) {
		mthca_warn(dev, "Completion event for bogus CQ %08x\n", cqn);
		return;
	}

	++cq->arm_sn;

	cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
}

void mthca_cq_clean(struct mthca_dev *dev, u32 cqn, u32 qpn)
{
	struct mthca_cq *cq;
	struct mthca_cqe *cqe;
	int prod_index;
	int nfreed = 0;

	spin_lock_irq(&dev->cq_table.lock);
	cq = mthca_array_get(&dev->cq_table.cq, cqn & (dev->limits.num_cqs - 1));
	if (cq)
		atomic_inc(&cq->refcount);
	spin_unlock_irq(&dev->cq_table.lock);

	if (!cq)
		return;

	spin_lock_irq(&cq->lock);

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->cons_index;
	     cqe_sw(cq, prod_index & cq->ibcq.cqe);
	     ++prod_index)
		if (prod_index == cq->cons_index + cq->ibcq.cqe)
			break;

	if (0)
		mthca_dbg(dev, "Cleaning QPN %06x from CQN %06x; ci %d, pi %d\n",
			  qpn, cqn, cq->cons_index, prod_index);

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while (prod_index > cq->cons_index) {
		cqe = get_cqe(cq, (prod_index - 1) & cq->ibcq.cqe);
		if (cqe->my_qpn == cpu_to_be32(qpn))
			++nfreed;
		else if (nfreed)
			memcpy(get_cqe(cq, (prod_index - 1 + nfreed) &
				       cq->ibcq.cqe),
			       cqe,
			       MTHCA_CQ_ENTRY_SIZE);
		--prod_index;
	}

	if (nfreed) {
		wmb();
		cq->cons_index += nfreed;
		update_cons_index(dev, cq, nfreed);
	}

	spin_unlock_irq(&cq->lock);
	if (atomic_dec_and_test(&cq->refcount))
		wake_up(&cq->wait);
}

static int handle_error_cqe(struct mthca_dev *dev, struct mthca_cq *cq,
			    struct mthca_qp *qp, int wqe_index, int is_send,
			    struct mthca_err_cqe *cqe,
			    struct ib_wc *entry, int *free_cqe)
{
	int err;
	int dbd;
	u32 new_wqe;

	if (1 && cqe->syndrome != SYNDROME_WR_FLUSH_ERR) {
		int j;

		mthca_dbg(dev, "%x/%d: error CQE -> QPN %06x, WQE @ %08x\n",
			  cq->cqn, cq->cons_index, be32_to_cpu(cqe->my_qpn),
			  be32_to_cpu(cqe->wqe));

		for (j = 0; j < 8; ++j)
			printk(KERN_DEBUG "  [%2x] %08x\n",
			       j * 4, be32_to_cpu(((u32 *) cqe)[j]));
	}

	/*
	 * For completions in error, only work request ID, status (and
	 * freed resource count for RD) have to be set.
	 */
	switch (cqe->syndrome) {
	case SYNDROME_LOCAL_LENGTH_ERR:
		entry->status = IB_WC_LOC_LEN_ERR;
		break;
	case SYNDROME_LOCAL_QP_OP_ERR:
		entry->status = IB_WC_LOC_QP_OP_ERR;
		break;
	case SYNDROME_LOCAL_EEC_OP_ERR:
		entry->status = IB_WC_LOC_EEC_OP_ERR;
		break;
	case SYNDROME_LOCAL_PROT_ERR:
		entry->status = IB_WC_LOC_PROT_ERR;
		break;
	case SYNDROME_WR_FLUSH_ERR:
		entry->status = IB_WC_WR_FLUSH_ERR;
		break;
	case SYNDROME_MW_BIND_ERR:
		entry->status = IB_WC_MW_BIND_ERR;
		break;
	case SYNDROME_BAD_RESP_ERR:
		entry->status = IB_WC_BAD_RESP_ERR;
		break;
	case SYNDROME_LOCAL_ACCESS_ERR:
		entry->status = IB_WC_LOC_ACCESS_ERR;
		break;
	case SYNDROME_REMOTE_INVAL_REQ_ERR:
		entry->status = IB_WC_REM_INV_REQ_ERR;
		break;
	case SYNDROME_REMOTE_ACCESS_ERR:
		entry->status = IB_WC_REM_ACCESS_ERR;
		break;
	case SYNDROME_REMOTE_OP_ERR:
		entry->status = IB_WC_REM_OP_ERR;
		break;
	case SYNDROME_RETRY_EXC_ERR:
		entry->status = IB_WC_RETRY_EXC_ERR;
		break;
	case SYNDROME_RNR_RETRY_EXC_ERR:
		entry->status = IB_WC_RNR_RETRY_EXC_ERR;
		break;
	case SYNDROME_LOCAL_RDD_VIOL_ERR:
		entry->status = IB_WC_LOC_RDD_VIOL_ERR;
		break;
	case SYNDROME_REMOTE_INVAL_RD_REQ_ERR:
		entry->status = IB_WC_REM_INV_RD_REQ_ERR;
		break;
	case SYNDROME_REMOTE_ABORTED_ERR:
		entry->status = IB_WC_REM_ABORT_ERR;
		break;
	case SYNDROME_INVAL_EECN_ERR:
		entry->status = IB_WC_INV_EECN_ERR;
		break;
	case SYNDROME_INVAL_EEC_STATE_ERR:
		entry->status = IB_WC_INV_EEC_STATE_ERR;
		break;
	default:
		entry->status = IB_WC_GENERAL_ERR;
		break;
	}

	err = mthca_free_err_wqe(dev, qp, is_send, wqe_index, &dbd, &new_wqe);
	if (err)
		return err;

	/*
	 * If we're at the end of the WQE chain, or we've used up our
	 * doorbell count, free the CQE.  Otherwise just update it for
	 * the next poll operation.
	 */
	if (!(new_wqe & cpu_to_be32(0x3f)) || (!cqe->db_cnt && dbd))
		return 0;

	cqe->db_cnt   = cpu_to_be16(be16_to_cpu(cqe->db_cnt) - dbd);
	cqe->wqe      = new_wqe;
	cqe->syndrome = SYNDROME_WR_FLUSH_ERR;

	*free_cqe = 0;

	return 0;
}

static void dump_cqe(struct mthca_cqe *cqe)
{
	int j;

	for (j = 0; j < 8; ++j)
		printk(KERN_DEBUG "  [%2x] %08x\n",
		       j * 4, be32_to_cpu(((u32 *) cqe)[j]));
}

static inline int mthca_poll_one(struct mthca_dev *dev,
				 struct mthca_cq *cq,
				 struct mthca_qp **cur_qp,
				 int *freed,
				 struct ib_wc *entry)
{
	struct mthca_wq *wq;
	struct mthca_cqe *cqe;
	int wqe_index;
	int is_error;
	int is_send;
	int free_cqe = 1;
	int err = 0;

	cqe = next_cqe_sw(cq);
	if (!cqe)
		return -EAGAIN;

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	if (0) {
		mthca_dbg(dev, "%x/%d: CQE -> QPN %06x, WQE @ %08x\n",
			  cq->cqn, cq->cons_index, be32_to_cpu(cqe->my_qpn),
			  be32_to_cpu(cqe->wqe));

		dump_cqe(cqe);
	}

	is_error = (cqe->opcode & MTHCA_ERROR_CQE_OPCODE_MASK) ==
		MTHCA_ERROR_CQE_OPCODE_MASK;
	is_send  = is_error ? cqe->opcode & 0x01 : cqe->is_send & 0x80;

	if (!*cur_qp || be32_to_cpu(cqe->my_qpn) != (*cur_qp)->qpn) {
		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		*cur_qp = mthca_array_get(&dev->qp_table.qp,
					  be32_to_cpu(cqe->my_qpn) &
					  (dev->limits.num_qps - 1));
		if (!*cur_qp) {
			mthca_warn(dev, "CQ entry for unknown QP %06x\n",
				   be32_to_cpu(cqe->my_qpn) & 0xffffff);
			err = -EINVAL;
			goto out;
		}
	}

	entry->qp_num = (*cur_qp)->qpn;

	if (is_send) {
		wq = &(*cur_qp)->sq;
		wqe_index = ((be32_to_cpu(cqe->wqe) - (*cur_qp)->send_wqe_offset)
			     >> wq->wqe_shift);
		entry->wr_id = (*cur_qp)->wrid[wqe_index +
					       (*cur_qp)->rq.max];
	} else {
		wq = &(*cur_qp)->rq;
		wqe_index = be32_to_cpu(cqe->wqe) >> wq->wqe_shift;
		entry->wr_id = (*cur_qp)->wrid[wqe_index];
	}

	if (wq->last_comp < wqe_index)
		wq->tail += wqe_index - wq->last_comp;
	else
		wq->tail += wqe_index + wq->max - wq->last_comp;

	wq->last_comp = wqe_index;

	if (0)
		mthca_dbg(dev, "%s completion for QP %06x, index %d (nr %d)\n",
			  is_send ? "Send" : "Receive",
			  (*cur_qp)->qpn, wqe_index, wq->max);

	if (is_error) {
		err = handle_error_cqe(dev, cq, *cur_qp, wqe_index, is_send,
				       (struct mthca_err_cqe *) cqe,
				       entry, &free_cqe);
		goto out;
	}

	if (is_send) {
		entry->opcode = IB_WC_SEND; /* XXX */
	} else {
		entry->byte_len = be32_to_cpu(cqe->byte_cnt);
		switch (cqe->opcode & 0x1f) {
		case IB_OPCODE_SEND_LAST_WITH_IMMEDIATE:
		case IB_OPCODE_SEND_ONLY_WITH_IMMEDIATE:
			entry->wc_flags = IB_WC_WITH_IMM;
			entry->imm_data = cqe->imm_etype_pkey_eec;
			entry->opcode = IB_WC_RECV;
			break;
		case IB_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE:
		case IB_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE:
			entry->wc_flags = IB_WC_WITH_IMM;
			entry->imm_data = cqe->imm_etype_pkey_eec;
			entry->opcode = IB_WC_RECV_RDMA_WITH_IMM;
			break;
		default:
			entry->wc_flags = 0;
			entry->opcode = IB_WC_RECV;
			break;
		}
		entry->slid 	   = be16_to_cpu(cqe->rlid);
		entry->sl   	   = be16_to_cpu(cqe->sl_g_mlpath) >> 12;
		entry->src_qp 	   = be32_to_cpu(cqe->rqpn) & 0xffffff;
		entry->dlid_path_bits = be16_to_cpu(cqe->sl_g_mlpath) & 0x7f;
		entry->pkey_index  = be32_to_cpu(cqe->imm_etype_pkey_eec) >> 16;
		entry->wc_flags   |= be16_to_cpu(cqe->sl_g_mlpath) & 0x80 ?
					IB_WC_GRH : 0;
	}

	entry->status = IB_WC_SUCCESS;

 out:
	if (likely(free_cqe)) {
		set_cqe_hw(cqe);
		++(*freed);
		++cq->cons_index;
	}

	return err;
}

int mthca_poll_cq(struct ib_cq *ibcq, int num_entries,
		  struct ib_wc *entry)
{
	struct mthca_dev *dev = to_mdev(ibcq->device);
	struct mthca_cq *cq = to_mcq(ibcq);
	struct mthca_qp *qp = NULL;
	unsigned long flags;
	int err = 0;
	int freed = 0;
	int npolled;

	spin_lock_irqsave(&cq->lock, flags);

	for (npolled = 0; npolled < num_entries; ++npolled) {
		err = mthca_poll_one(dev, cq, &qp,
				     &freed, entry + npolled);
		if (err)
			break;
	}

	if (freed) {
		wmb();
		update_cons_index(dev, cq, freed);
	}

	spin_unlock_irqrestore(&cq->lock, flags);

	return err == 0 || err == -EAGAIN ? npolled : err;
}

int mthca_tavor_arm_cq(struct ib_cq *cq, enum ib_cq_notify notify)
{
	u32 doorbell[2];

	doorbell[0] = cpu_to_be32((notify == IB_CQ_SOLICITED ?
				   MTHCA_TAVOR_CQ_DB_REQ_NOT_SOL :
				   MTHCA_TAVOR_CQ_DB_REQ_NOT)      |
				  to_mcq(cq)->cqn);
	doorbell[1] = 0xffffffff;

	mthca_write64(doorbell,
		      to_mdev(cq->device)->kar + MTHCA_CQ_DOORBELL,
		      MTHCA_GET_DOORBELL_LOCK(&to_mdev(cq->device)->doorbell_lock));

	return 0;
}

int mthca_arbel_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify notify)
{
	struct mthca_cq *cq = to_mcq(ibcq);
	u32 doorbell[2];
	u32 sn;
	u32 ci;

	sn = cq->arm_sn & 3;
	ci = cpu_to_be32(cq->cons_index);

	doorbell[0] = ci;
	doorbell[1] = cpu_to_be32((cq->cqn << 8) | (2 << 5) | (sn << 3) |
				  (notify == IB_CQ_SOLICITED ? 1 : 2));

	mthca_write_db_rec(doorbell, cq->arm_db);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	doorbell[0] = cpu_to_be32((sn << 28)                       |
				  (notify == IB_CQ_SOLICITED ?
				   MTHCA_ARBEL_CQ_DB_REQ_NOT_SOL :
				   MTHCA_ARBEL_CQ_DB_REQ_NOT)      |
				  cq->cqn);
	doorbell[1] = ci;

	mthca_write64(doorbell,
		      to_mdev(ibcq->device)->kar + MTHCA_CQ_DOORBELL,
		      MTHCA_GET_DOORBELL_LOCK(&to_mdev(ibcq->device)->doorbell_lock));

	return 0;
}

static void mthca_free_cq_buf(struct mthca_dev *dev, struct mthca_cq *cq)
{
	int i;
	int size;

	if (cq->is_direct)
		pci_free_consistent(dev->pdev,
				    (cq->ibcq.cqe + 1) * MTHCA_CQ_ENTRY_SIZE,
				    cq->queue.direct.buf,
				    pci_unmap_addr(&cq->queue.direct,
						   mapping));
	else {
		size = (cq->ibcq.cqe + 1) * MTHCA_CQ_ENTRY_SIZE;
		for (i = 0; i < (size + PAGE_SIZE - 1) / PAGE_SIZE; ++i)
			if (cq->queue.page_list[i].buf)
				pci_free_consistent(dev->pdev, PAGE_SIZE,
						    cq->queue.page_list[i].buf,
						    pci_unmap_addr(&cq->queue.page_list[i],
								   mapping));

		kfree(cq->queue.page_list);
	}
}

static int mthca_alloc_cq_buf(struct mthca_dev *dev, int size,
			      struct mthca_cq *cq)
{
	int err = -ENOMEM;
	int npages, shift;
	u64 *dma_list = NULL;
	dma_addr_t t;
	int i;

	if (size <= MTHCA_MAX_DIRECT_CQ_SIZE) {
		cq->is_direct = 1;
		npages        = 1;
		shift         = get_order(size) + PAGE_SHIFT;

		cq->queue.direct.buf = pci_alloc_consistent(dev->pdev,
							    size, &t);
		if (!cq->queue.direct.buf)
			return -ENOMEM;

		pci_unmap_addr_set(&cq->queue.direct, mapping, t);

		memset(cq->queue.direct.buf, 0, size);

		while (t & ((1 << shift) - 1)) {
			--shift;
			npages *= 2;
		}

		dma_list = kmalloc(npages * sizeof *dma_list, GFP_KERNEL);
		if (!dma_list)
			goto err_free;

		for (i = 0; i < npages; ++i)
			dma_list[i] = t + i * (1 << shift);
	} else {
		cq->is_direct = 0;
		npages        = (size + PAGE_SIZE - 1) / PAGE_SIZE;
		shift         = PAGE_SHIFT;

		dma_list = kmalloc(npages * sizeof *dma_list, GFP_KERNEL);
		if (!dma_list)
			return -ENOMEM;

		cq->queue.page_list = kmalloc(npages * sizeof *cq->queue.page_list,
					      GFP_KERNEL);
		if (!cq->queue.page_list)
			goto err_out;

		for (i = 0; i < npages; ++i)
			cq->queue.page_list[i].buf = NULL;

		for (i = 0; i < npages; ++i) {
			cq->queue.page_list[i].buf =
				pci_alloc_consistent(dev->pdev, PAGE_SIZE, &t);
			if (!cq->queue.page_list[i].buf)
				goto err_free;

			dma_list[i] = t;
			pci_unmap_addr_set(&cq->queue.page_list[i], mapping, t);

			memset(cq->queue.page_list[i].buf, 0, PAGE_SIZE);
		}
	}

	err = mthca_mr_alloc_phys(dev, dev->driver_pd.pd_num,
				  dma_list, shift, npages,
				  0, size,
				  MTHCA_MPT_FLAG_LOCAL_WRITE |
				  MTHCA_MPT_FLAG_LOCAL_READ,
				  &cq->mr);
	if (err)
		goto err_free;

	kfree(dma_list);

	return 0;

err_free:
	mthca_free_cq_buf(dev, cq);

err_out:
	kfree(dma_list);

	return err;
}

int mthca_init_cq(struct mthca_dev *dev, int nent,
		  struct mthca_cq *cq)
{
	int size = nent * MTHCA_CQ_ENTRY_SIZE;
	void *mailbox = NULL;
	struct mthca_cq_context *cq_context;
	int err = -ENOMEM;
	u8 status;
	int i;

	might_sleep();

	cq->ibcq.cqe = nent - 1;

	cq->cqn = mthca_alloc(&dev->cq_table.alloc);
	if (cq->cqn == -1)
		return -ENOMEM;

	if (dev->hca_type == ARBEL_NATIVE) {
		cq->arm_sn = 1;

		err = mthca_table_get(dev, dev->cq_table.table, cq->cqn);
		if (err)
			goto err_out;

		err = -ENOMEM;

		cq->set_ci_db_index = mthca_alloc_db(dev, MTHCA_DB_TYPE_CQ_SET_CI,
						     cq->cqn, &cq->set_ci_db);
		if (cq->set_ci_db_index < 0)
			goto err_out_icm;

		cq->arm_db_index = mthca_alloc_db(dev, MTHCA_DB_TYPE_CQ_ARM,
						  cq->cqn, &cq->arm_db);
		if (cq->arm_db_index < 0)
			goto err_out_ci;
	}

	mailbox = kmalloc(sizeof (struct mthca_cq_context) + MTHCA_CMD_MAILBOX_EXTRA,
			  GFP_KERNEL);
	if (!mailbox)
		goto err_out_mailbox;

	cq_context = MAILBOX_ALIGN(mailbox);

	err = mthca_alloc_cq_buf(dev, size, cq);
	if (err)
		goto err_out_mailbox;

	for (i = 0; i < nent; ++i)
		set_cqe_hw(get_cqe(cq, i));

	spin_lock_init(&cq->lock);
	atomic_set(&cq->refcount, 1);
	init_waitqueue_head(&cq->wait);

	memset(cq_context, 0, sizeof *cq_context);
	cq_context->flags           = cpu_to_be32(MTHCA_CQ_STATUS_OK      |
						  MTHCA_CQ_STATE_DISARMED |
						  MTHCA_CQ_FLAG_TR);
	cq_context->start           = cpu_to_be64(0);
	cq_context->logsize_usrpage = cpu_to_be32((ffs(nent) - 1) << 24 |
						  dev->driver_uar.index);
	cq_context->error_eqn       = cpu_to_be32(dev->eq_table.eq[MTHCA_EQ_ASYNC].eqn);
	cq_context->comp_eqn        = cpu_to_be32(dev->eq_table.eq[MTHCA_EQ_COMP].eqn);
	cq_context->pd              = cpu_to_be32(dev->driver_pd.pd_num);
	cq_context->lkey            = cpu_to_be32(cq->mr.ibmr.lkey);
	cq_context->cqn             = cpu_to_be32(cq->cqn);

	if (dev->hca_type == ARBEL_NATIVE) {
		cq_context->ci_db    = cpu_to_be32(cq->set_ci_db_index);
		cq_context->state_db = cpu_to_be32(cq->arm_db_index);
	}

	err = mthca_SW2HW_CQ(dev, cq_context, cq->cqn, &status);
	if (err) {
		mthca_warn(dev, "SW2HW_CQ failed (%d)\n", err);
		goto err_out_free_mr;
	}

	if (status) {
		mthca_warn(dev, "SW2HW_CQ returned status 0x%02x\n",
			   status);
		err = -EINVAL;
		goto err_out_free_mr;
	}

	spin_lock_irq(&dev->cq_table.lock);
	if (mthca_array_set(&dev->cq_table.cq,
			    cq->cqn & (dev->limits.num_cqs - 1),
			    cq)) {
		spin_unlock_irq(&dev->cq_table.lock);
		goto err_out_free_mr;
	}
	spin_unlock_irq(&dev->cq_table.lock);

	cq->cons_index = 0;

	kfree(mailbox);

	return 0;

err_out_free_mr:
	mthca_free_mr(dev, &cq->mr);
	mthca_free_cq_buf(dev, cq);

err_out_mailbox:
	kfree(mailbox);

	mthca_free_db(dev, MTHCA_DB_TYPE_CQ_ARM, cq->arm_db_index);

err_out_ci:
	mthca_free_db(dev, MTHCA_DB_TYPE_CQ_SET_CI, cq->set_ci_db_index);

err_out_icm:
	mthca_table_put(dev, dev->cq_table.table, cq->cqn);

err_out:
	mthca_free(&dev->cq_table.alloc, cq->cqn);

	return err;
}

void mthca_free_cq(struct mthca_dev *dev,
		   struct mthca_cq *cq)
{
	void *mailbox;
	int err;
	u8 status;

	might_sleep();

	mailbox = kmalloc(sizeof (struct mthca_cq_context) + MTHCA_CMD_MAILBOX_EXTRA,
			  GFP_KERNEL);
	if (!mailbox) {
		mthca_warn(dev, "No memory for mailbox to free CQ.\n");
		return;
	}

	err = mthca_HW2SW_CQ(dev, MAILBOX_ALIGN(mailbox), cq->cqn, &status);
	if (err)
		mthca_warn(dev, "HW2SW_CQ failed (%d)\n", err);
	else if (status)
		mthca_warn(dev, "HW2SW_CQ returned status 0x%02x\n",
			   status);

	if (0) {
		u32 *ctx = MAILBOX_ALIGN(mailbox);
		int j;

		printk(KERN_ERR "context for CQN %x (cons index %x, next sw %d)\n",
		       cq->cqn, cq->cons_index, !!next_cqe_sw(cq));
		for (j = 0; j < 16; ++j)
			printk(KERN_ERR "[%2x] %08x\n", j * 4, be32_to_cpu(ctx[j]));
	}

	spin_lock_irq(&dev->cq_table.lock);
	mthca_array_clear(&dev->cq_table.cq,
			  cq->cqn & (dev->limits.num_cqs - 1));
	spin_unlock_irq(&dev->cq_table.lock);

	if (dev->mthca_flags & MTHCA_FLAG_MSI_X)
		synchronize_irq(dev->eq_table.eq[MTHCA_EQ_COMP].msi_x_vector);
	else
		synchronize_irq(dev->pdev->irq);

	atomic_dec(&cq->refcount);
	wait_event(cq->wait, !atomic_read(&cq->refcount));

	mthca_free_mr(dev, &cq->mr);
	mthca_free_cq_buf(dev, cq);

	if (dev->hca_type == ARBEL_NATIVE) {
		mthca_free_db(dev, MTHCA_DB_TYPE_CQ_ARM,    cq->arm_db_index);
		mthca_free_db(dev, MTHCA_DB_TYPE_CQ_SET_CI, cq->set_ci_db_index);
		mthca_table_put(dev, dev->cq_table.table, cq->cqn);
	}

	mthca_free(&dev->cq_table.alloc, cq->cqn);
	kfree(mailbox);
}

int __devinit mthca_init_cq_table(struct mthca_dev *dev)
{
	int err;

	spin_lock_init(&dev->cq_table.lock);

	err = mthca_alloc_init(&dev->cq_table.alloc,
			       dev->limits.num_cqs,
			       (1 << 24) - 1,
			       dev->limits.reserved_cqs);
	if (err)
		return err;

	err = mthca_array_init(&dev->cq_table.cq,
			       dev->limits.num_cqs);
	if (err)
		mthca_alloc_cleanup(&dev->cq_table.alloc);

	return err;
}

void __devexit mthca_cleanup_cq_table(struct mthca_dev *dev)
{
	mthca_array_cleanup(&dev->cq_table.cq, dev->limits.num_cqs);
	mthca_alloc_cleanup(&dev->cq_table.alloc);
}

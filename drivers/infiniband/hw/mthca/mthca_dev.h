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
 * $Id: mthca_dev.h 1349 2004-12-16 21:09:43Z roland $
 */

#ifndef MTHCA_DEV_H
#define MTHCA_DEV_H

#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <asm/semaphore.h>

#include "mthca_provider.h"
#include "mthca_doorbell.h"

#define DRV_NAME	"ib_mthca"
#define PFX		DRV_NAME ": "
#define DRV_VERSION	"0.06-pre"
#define DRV_RELDATE	"November 8, 2004"

/* Types of supported HCA */
enum {
	TAVOR,			/* MT23108                        */
	ARBEL_COMPAT,		/* MT25208 in Tavor compat mode   */
	ARBEL_NATIVE		/* MT25208 with extended features */
};

enum {
	MTHCA_FLAG_DDR_HIDDEN = 1 << 1,
	MTHCA_FLAG_SRQ        = 1 << 2,
	MTHCA_FLAG_MSI        = 1 << 3,
	MTHCA_FLAG_MSI_X      = 1 << 4,
	MTHCA_FLAG_NO_LAM     = 1 << 5
};

enum {
	MTHCA_KAR_PAGE  = 1,
	MTHCA_MAX_PORTS = 2
};

enum {
	MTHCA_EQ_CONTEXT_SIZE =  0x40,
	MTHCA_CQ_CONTEXT_SIZE =  0x40,
	MTHCA_QP_CONTEXT_SIZE = 0x200,
	MTHCA_RDB_ENTRY_SIZE  =  0x20,
	MTHCA_AV_SIZE         =  0x20,
	MTHCA_MGM_ENTRY_SIZE  =  0x40,

	/* Arbel FW gives us these, but we need them for Tavor */
	MTHCA_MPT_ENTRY_SIZE  =  0x40,
	MTHCA_MTT_SEG_SIZE    =  0x40,
};

enum {
	MTHCA_EQ_CMD,
	MTHCA_EQ_ASYNC,
	MTHCA_EQ_COMP,
	MTHCA_NUM_EQ
};

struct mthca_cmd {
	int                       use_events;
	struct semaphore          hcr_sem;
	struct semaphore 	  poll_sem;
	struct semaphore 	  event_sem;
	int              	  max_cmds;
	spinlock_t                context_lock;
	int                       free_head;
	struct mthca_cmd_context *context;
	u16                       token_mask;
};

struct mthca_limits {
	int      num_ports;
	int      vl_cap;
	int      mtu_cap;
	int      gid_table_len;
	int      pkey_table_len;
	int      local_ca_ack_delay;
	int      max_sg;
	int      num_qps;
	int      reserved_qps;
	int      num_srqs;
	int      reserved_srqs;
	int      num_eecs;
	int      reserved_eecs;
	int      num_cqs;
	int      reserved_cqs;
	int      num_eqs;
	int      reserved_eqs;
	int      num_mpts;
	int      num_mtt_segs;
	int      mtt_seg_size;
	int      reserved_mtts;
	int      reserved_mrws;
	int      reserved_uars;
	int      num_mgms;
	int      num_amgms;
	int      reserved_mcgs;
	int      num_pds;
	int      reserved_pds;
};

struct mthca_alloc {
	u32            last;
	u32            top;
	u32            max;
	u32            mask;
	spinlock_t     lock;
	unsigned long *table;
};

struct mthca_array {
	struct {
		void    **page;
		int       used;
	} *page_list;
};

struct mthca_pd_table {
	struct mthca_alloc alloc;
};

struct mthca_mr_table {
	struct mthca_alloc      mpt_alloc;
	int                     max_mtt_order;
	unsigned long         **mtt_buddy;
	u64                     mtt_base;
	struct mthca_icm_table *mtt_table;
	struct mthca_icm_table *mpt_table;
};

struct mthca_eq_table {
	struct mthca_alloc alloc;
	void __iomem      *clr_int;
	u32                clr_mask;
	struct mthca_eq    eq[MTHCA_NUM_EQ];
	u64                icm_virt;
	struct page       *icm_page;
	dma_addr_t         icm_dma;
	int                have_irq;
	u8                 inta_pin;
};

struct mthca_cq_table {
	struct mthca_alloc 	alloc;
	spinlock_t         	lock;
	struct mthca_array      cq;
	struct mthca_icm_table *table;
};

struct mthca_qp_table {
	struct mthca_alloc     	alloc;
	u32                    	rdb_base;
	int                    	rdb_shift;
	int                    	sqp_start;
	spinlock_t             	lock;
	struct mthca_array     	qp;
	struct mthca_icm_table *qp_table;
	struct mthca_icm_table *eqp_table;
};

struct mthca_av_table {
	struct pci_pool   *pool;
	int                num_ddr_avs;
	u64                ddr_av_base;
	void __iomem      *av_map;
	struct mthca_alloc alloc;
};

struct mthca_mcg_table {
	struct semaphore   sem;
	struct mthca_alloc alloc;
};

struct mthca_dev {
	struct ib_device  ib_dev;
	struct pci_dev   *pdev;

	int          	 hca_type;
	unsigned long	 mthca_flags;

	u32              rev_id;

	/* firmware info */
	u64              fw_ver;
	union {
		struct {
			u64 fw_start;
			u64 fw_end;
		}        tavor;
		struct {
			u64 clr_int_base;
			u64 eq_arm_base;
			u64 eq_set_ci_base;
			struct mthca_icm *fw_icm;
			struct mthca_icm *aux_icm;
			u16 fw_pages;
		}        arbel;
	}                fw;

	u64              ddr_start;
	u64              ddr_end;

	MTHCA_DECLARE_DOORBELL_LOCK(doorbell_lock)
	struct semaphore cap_mask_mutex;

	void __iomem    *hcr;
	void __iomem    *ecr_base;
	void __iomem    *clr_base;
	void __iomem    *kar;

	struct mthca_cmd    cmd;
	struct mthca_limits limits;

	struct mthca_pd_table  pd_table;
	struct mthca_mr_table  mr_table;
	struct mthca_eq_table  eq_table;
	struct mthca_cq_table  cq_table;
	struct mthca_qp_table  qp_table;
	struct mthca_av_table  av_table;
	struct mthca_mcg_table mcg_table;

	struct mthca_pd       driver_pd;
	struct mthca_mr       driver_mr;

	struct ib_mad_agent  *send_agent[MTHCA_MAX_PORTS][2];
	struct ib_ah         *sm_ah[MTHCA_MAX_PORTS];
	spinlock_t            sm_lock;
};

#define mthca_dbg(mdev, format, arg...) \
	dev_dbg(&mdev->pdev->dev, format, ## arg)
#define mthca_err(mdev, format, arg...) \
	dev_err(&mdev->pdev->dev, format, ## arg)
#define mthca_info(mdev, format, arg...) \
	dev_info(&mdev->pdev->dev, format, ## arg)
#define mthca_warn(mdev, format, arg...) \
	dev_warn(&mdev->pdev->dev, format, ## arg)

extern void __buggy_use_of_MTHCA_GET(void);
extern void __buggy_use_of_MTHCA_PUT(void);

#define MTHCA_GET(dest, source, offset)                               \
	do {                                                          \
		void *__p = (char *) (source) + (offset);             \
		switch (sizeof (dest)) {                              \
			case 1: (dest) = *(u8 *) __p;       break;    \
			case 2: (dest) = be16_to_cpup(__p); break;    \
			case 4: (dest) = be32_to_cpup(__p); break;    \
			case 8: (dest) = be64_to_cpup(__p); break;    \
			default: __buggy_use_of_MTHCA_GET();          \
		}                                                     \
	} while (0)

#define MTHCA_PUT(dest, source, offset)                               \
	do {                                                          \
		__typeof__(source) *__p =                             \
			(__typeof__(source) *) ((char *) (dest) + (offset)); \
		switch (sizeof(source)) {                             \
			case 1: *__p = (source);            break;    \
			case 2: *__p = cpu_to_be16(source); break;    \
			case 4: *__p = cpu_to_be32(source); break;    \
			case 8: *__p = cpu_to_be64(source); break;    \
			default: __buggy_use_of_MTHCA_PUT();          \
		}                                                     \
	} while (0)

int mthca_reset(struct mthca_dev *mdev);

u32 mthca_alloc(struct mthca_alloc *alloc);
void mthca_free(struct mthca_alloc *alloc, u32 obj);
int mthca_alloc_init(struct mthca_alloc *alloc, u32 num, u32 mask,
		     u32 reserved);
void mthca_alloc_cleanup(struct mthca_alloc *alloc);
void *mthca_array_get(struct mthca_array *array, int index);
int mthca_array_set(struct mthca_array *array, int index, void *value);
void mthca_array_clear(struct mthca_array *array, int index);
int mthca_array_init(struct mthca_array *array, int nent);
void mthca_array_cleanup(struct mthca_array *array, int nent);

int mthca_init_pd_table(struct mthca_dev *dev);
int mthca_init_mr_table(struct mthca_dev *dev);
int mthca_init_eq_table(struct mthca_dev *dev);
int mthca_init_cq_table(struct mthca_dev *dev);
int mthca_init_qp_table(struct mthca_dev *dev);
int mthca_init_av_table(struct mthca_dev *dev);
int mthca_init_mcg_table(struct mthca_dev *dev);

void mthca_cleanup_pd_table(struct mthca_dev *dev);
void mthca_cleanup_mr_table(struct mthca_dev *dev);
void mthca_cleanup_eq_table(struct mthca_dev *dev);
void mthca_cleanup_cq_table(struct mthca_dev *dev);
void mthca_cleanup_qp_table(struct mthca_dev *dev);
void mthca_cleanup_av_table(struct mthca_dev *dev);
void mthca_cleanup_mcg_table(struct mthca_dev *dev);

int mthca_register_device(struct mthca_dev *dev);
void mthca_unregister_device(struct mthca_dev *dev);

int mthca_pd_alloc(struct mthca_dev *dev, struct mthca_pd *pd);
void mthca_pd_free(struct mthca_dev *dev, struct mthca_pd *pd);

int mthca_mr_alloc_notrans(struct mthca_dev *dev, u32 pd,
			   u32 access, struct mthca_mr *mr);
int mthca_mr_alloc_phys(struct mthca_dev *dev, u32 pd,
			u64 *buffer_list, int buffer_size_shift,
			int list_len, u64 iova, u64 total_size,
			u32 access, struct mthca_mr *mr);
void mthca_free_mr(struct mthca_dev *dev, struct mthca_mr *mr);

int mthca_map_eq_icm(struct mthca_dev *dev, u64 icm_virt);
void mthca_unmap_eq_icm(struct mthca_dev *dev);

int mthca_poll_cq(struct ib_cq *ibcq, int num_entries,
		  struct ib_wc *entry);
void mthca_arm_cq(struct mthca_dev *dev, struct mthca_cq *cq,
		  int solicited);
int mthca_init_cq(struct mthca_dev *dev, int nent,
		  struct mthca_cq *cq);
void mthca_free_cq(struct mthca_dev *dev,
		   struct mthca_cq *cq);
void mthca_cq_event(struct mthca_dev *dev, u32 cqn);
void mthca_cq_clean(struct mthca_dev *dev, u32 cqn, u32 qpn);

void mthca_qp_event(struct mthca_dev *dev, u32 qpn,
		    enum ib_event_type event_type);
int mthca_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask);
int mthca_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		    struct ib_send_wr **bad_wr);
int mthca_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		       struct ib_recv_wr **bad_wr);
int mthca_free_err_wqe(struct mthca_qp *qp, int is_send,
		       int index, int *dbd, u32 *new_wqe);
int mthca_alloc_qp(struct mthca_dev *dev,
		   struct mthca_pd *pd,
		   struct mthca_cq *send_cq,
		   struct mthca_cq *recv_cq,
		   enum ib_qp_type type,
		   enum ib_sig_type send_policy,
		   enum ib_sig_type recv_policy,
		   struct mthca_qp *qp);
int mthca_alloc_sqp(struct mthca_dev *dev,
		    struct mthca_pd *pd,
		    struct mthca_cq *send_cq,
		    struct mthca_cq *recv_cq,
		    enum ib_sig_type send_policy,
		    enum ib_sig_type recv_policy,
		    int qpn,
		    int port,
		    struct mthca_sqp *sqp);
void mthca_free_qp(struct mthca_dev *dev, struct mthca_qp *qp);
int mthca_create_ah(struct mthca_dev *dev,
		    struct mthca_pd *pd,
		    struct ib_ah_attr *ah_attr,
		    struct mthca_ah *ah);
int mthca_destroy_ah(struct mthca_dev *dev, struct mthca_ah *ah);
int mthca_read_ah(struct mthca_dev *dev, struct mthca_ah *ah,
		  struct ib_ud_header *header);

int mthca_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);
int mthca_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);

int mthca_process_mad(struct ib_device *ibdev,
		      int mad_flags,
		      u8 port_num,
		      struct ib_wc *in_wc,
		      struct ib_grh *in_grh,
		      struct ib_mad *in_mad,
		      struct ib_mad *out_mad);
int mthca_create_agents(struct mthca_dev *dev);
void mthca_free_agents(struct mthca_dev *dev);

static inline struct mthca_dev *to_mdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct mthca_dev, ib_dev);
}

#endif /* MTHCA_DEV_H */

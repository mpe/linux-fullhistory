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
 * $Id: mthca_cmd.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <ib_mad.h>

#include "mthca_dev.h"
#include "mthca_config_reg.h"
#include "mthca_cmd.h"
#include "mthca_memfree.h"

#define CMD_POLL_TOKEN 0xffff

enum {
	HCR_IN_PARAM_OFFSET    = 0x00,
	HCR_IN_MODIFIER_OFFSET = 0x08,
	HCR_OUT_PARAM_OFFSET   = 0x0c,
	HCR_TOKEN_OFFSET       = 0x14,
	HCR_STATUS_OFFSET      = 0x18,

	HCR_OPMOD_SHIFT        = 12,
	HCA_E_BIT              = 22,
	HCR_GO_BIT             = 23
};

enum {
	/* initialization and general commands */
	CMD_SYS_EN          = 0x1,
	CMD_SYS_DIS         = 0x2,
	CMD_MAP_FA          = 0xfff,
	CMD_UNMAP_FA        = 0xffe,
	CMD_RUN_FW          = 0xff6,
	CMD_MOD_STAT_CFG    = 0x34,
	CMD_QUERY_DEV_LIM   = 0x3,
	CMD_QUERY_FW        = 0x4,
	CMD_ENABLE_LAM      = 0xff8,
	CMD_DISABLE_LAM     = 0xff7,
	CMD_QUERY_DDR       = 0x5,
	CMD_QUERY_ADAPTER   = 0x6,
	CMD_INIT_HCA        = 0x7,
	CMD_CLOSE_HCA       = 0x8,
	CMD_INIT_IB         = 0x9,
	CMD_CLOSE_IB        = 0xa,
	CMD_QUERY_HCA       = 0xb,
	CMD_SET_IB          = 0xc,
	CMD_ACCESS_DDR      = 0x2e,
	CMD_MAP_ICM         = 0xffa,
	CMD_UNMAP_ICM       = 0xff9,
	CMD_MAP_ICM_AUX     = 0xffc,
	CMD_UNMAP_ICM_AUX   = 0xffb,
	CMD_SET_ICM_SIZE    = 0xffd,

	/* TPT commands */
	CMD_SW2HW_MPT 	    = 0xd,
	CMD_QUERY_MPT 	    = 0xe,
	CMD_HW2SW_MPT 	    = 0xf,
	CMD_READ_MTT        = 0x10,
	CMD_WRITE_MTT       = 0x11,
	CMD_SYNC_TPT        = 0x2f,

	/* EQ commands */
	CMD_MAP_EQ          = 0x12,
	CMD_SW2HW_EQ 	    = 0x13,
	CMD_HW2SW_EQ 	    = 0x14,
	CMD_QUERY_EQ        = 0x15,

	/* CQ commands */
	CMD_SW2HW_CQ 	    = 0x16,
	CMD_HW2SW_CQ 	    = 0x17,
	CMD_QUERY_CQ 	    = 0x18,
	CMD_RESIZE_CQ       = 0x2c,

	/* SRQ commands */
	CMD_SW2HW_SRQ 	    = 0x35,
	CMD_HW2SW_SRQ 	    = 0x36,
	CMD_QUERY_SRQ       = 0x37,

	/* QP/EE commands */
	CMD_RST2INIT_QPEE   = 0x19,
	CMD_INIT2RTR_QPEE   = 0x1a,
	CMD_RTR2RTS_QPEE    = 0x1b,
	CMD_RTS2RTS_QPEE    = 0x1c,
	CMD_SQERR2RTS_QPEE  = 0x1d,
	CMD_2ERR_QPEE       = 0x1e,
	CMD_RTS2SQD_QPEE    = 0x1f,
	CMD_SQD2SQD_QPEE    = 0x38,
	CMD_SQD2RTS_QPEE    = 0x20,
	CMD_ERR2RST_QPEE    = 0x21,
	CMD_QUERY_QPEE      = 0x22,
	CMD_INIT2INIT_QPEE  = 0x2d,
	CMD_SUSPEND_QPEE    = 0x32,
	CMD_UNSUSPEND_QPEE  = 0x33,
	/* special QPs and management commands */
	CMD_CONF_SPECIAL_QP = 0x23,
	CMD_MAD_IFC         = 0x24,

	/* multicast commands */
	CMD_READ_MGM        = 0x25,
	CMD_WRITE_MGM       = 0x26,
	CMD_MGID_HASH       = 0x27,

	/* miscellaneous commands */
	CMD_DIAG_RPRT       = 0x30,
	CMD_NOP             = 0x31,

	/* debug commands */
	CMD_QUERY_DEBUG_MSG = 0x2a,
	CMD_SET_DEBUG_MSG   = 0x2b,
};

/*
 * According to Mellanox code, FW may be starved and never complete
 * commands.  So we can't use strict timeouts described in PRM -- we
 * just arbitrarily select 60 seconds for now.
 */
#if 0
/*
 * Round up and add 1 to make sure we get the full wait time (since we
 * will be starting in the middle of a jiffy)
 */
enum {
	CMD_TIME_CLASS_A = (HZ + 999) / 1000 + 1,
	CMD_TIME_CLASS_B = (HZ +  99) /  100 + 1,
	CMD_TIME_CLASS_C = (HZ +   9) /   10 + 1
};
#else
enum {
	CMD_TIME_CLASS_A = 60 * HZ,
	CMD_TIME_CLASS_B = 60 * HZ,
	CMD_TIME_CLASS_C = 60 * HZ
};
#endif

enum {
	GO_BIT_TIMEOUT = HZ * 10
};

struct mthca_cmd_context {
	struct completion done;
	struct timer_list timer;
	int               result;
	int               next;
	u64               out_param;
	u16               token;
	u8                status;
};

static inline int go_bit(struct mthca_dev *dev)
{
	return readl(dev->hcr + HCR_STATUS_OFFSET) &
		swab32(1 << HCR_GO_BIT);
}

static int mthca_cmd_post(struct mthca_dev *dev,
			  u64 in_param,
			  u64 out_param,
			  u32 in_modifier,
			  u8 op_modifier,
			  u16 op,
			  u16 token,
			  int event)
{
	int err = 0;

	if (down_interruptible(&dev->cmd.hcr_sem))
		return -EINTR;

	if (event) {
		unsigned long end = jiffies + GO_BIT_TIMEOUT;

		while (go_bit(dev) && time_before(jiffies, end)) {
			set_current_state(TASK_RUNNING);
			schedule();
		}
	}

	if (go_bit(dev)) {
		err = -EAGAIN;
		goto out;
	}

	/*
	 * We use writel (instead of something like memcpy_toio)
	 * because writes of less than 32 bits to the HCR don't work
	 * (and some architectures such as ia64 implement memcpy_toio
	 * in terms of writeb).
	 */
	__raw_writel(cpu_to_be32(in_param >> 32),           dev->hcr + 0 * 4);
	__raw_writel(cpu_to_be32(in_param & 0xfffffffful),  dev->hcr + 1 * 4);
	__raw_writel(cpu_to_be32(in_modifier),              dev->hcr + 2 * 4);
	__raw_writel(cpu_to_be32(out_param >> 32),          dev->hcr + 3 * 4);
	__raw_writel(cpu_to_be32(out_param & 0xfffffffful), dev->hcr + 4 * 4);
	__raw_writel(cpu_to_be32(token << 16),              dev->hcr + 5 * 4);

	/* __raw_writel may not order writes. */
	wmb();

	__raw_writel(cpu_to_be32((1 << HCR_GO_BIT)                |
				 (event ? (1 << HCA_E_BIT) : 0)   |
				 (op_modifier << HCR_OPMOD_SHIFT) |
				 op),                       dev->hcr + 6 * 4);

out:
	up(&dev->cmd.hcr_sem);
	return err;
}

static int mthca_cmd_poll(struct mthca_dev *dev,
			  u64 in_param,
			  u64 *out_param,
			  int out_is_imm,
			  u32 in_modifier,
			  u8 op_modifier,
			  u16 op,
			  unsigned long timeout,
			  u8 *status)
{
	int err = 0;
	unsigned long end;

	if (down_interruptible(&dev->cmd.poll_sem))
		return -EINTR;

	err = mthca_cmd_post(dev, in_param,
			     out_param ? *out_param : 0,
			     in_modifier, op_modifier,
			     op, CMD_POLL_TOKEN, 0);
	if (err)
		goto out;

	end = timeout + jiffies;
	while (go_bit(dev) && time_before(jiffies, end)) {
		set_current_state(TASK_RUNNING);
		schedule();
	}

	if (go_bit(dev)) {
		err = -EBUSY;
		goto out;
	}

	if (out_is_imm) {
		memcpy_fromio(out_param, dev->hcr + HCR_OUT_PARAM_OFFSET, sizeof (u64));
		be64_to_cpus(out_param);
	}

	*status = be32_to_cpu(__raw_readl(dev->hcr + HCR_STATUS_OFFSET)) >> 24;

out:
	up(&dev->cmd.poll_sem);
	return err;
}

void mthca_cmd_event(struct mthca_dev *dev,
		     u16 token,
		     u8  status,
		     u64 out_param)
{
	struct mthca_cmd_context *context =
		&dev->cmd.context[token & dev->cmd.token_mask];

	/* previously timed out command completing at long last */
	if (token != context->token)
		return;

	context->result    = 0;
	context->status    = status;
	context->out_param = out_param;

	context->token += dev->cmd.token_mask + 1;

	complete(&context->done);
}

static void event_timeout(unsigned long context_ptr)
{
	struct mthca_cmd_context *context =
		(struct mthca_cmd_context *) context_ptr;

	context->result = -EBUSY;
	complete(&context->done);
}

static int mthca_cmd_wait(struct mthca_dev *dev,
			  u64 in_param,
			  u64 *out_param,
			  int out_is_imm,
			  u32 in_modifier,
			  u8 op_modifier,
			  u16 op,
			  unsigned long timeout,
			  u8 *status)
{
	int err = 0;
	struct mthca_cmd_context *context;

	if (down_interruptible(&dev->cmd.event_sem))
		return -EINTR;

	spin_lock(&dev->cmd.context_lock);
	BUG_ON(dev->cmd.free_head < 0);
	context = &dev->cmd.context[dev->cmd.free_head];
	dev->cmd.free_head = context->next;
	spin_unlock(&dev->cmd.context_lock);

	init_completion(&context->done);

	err = mthca_cmd_post(dev, in_param,
			     out_param ? *out_param : 0,
			     in_modifier, op_modifier,
			     op, context->token, 1);
	if (err)
		goto out;

	context->timer.expires  = jiffies + timeout;
	add_timer(&context->timer);

	wait_for_completion(&context->done);
	del_timer_sync(&context->timer);

	err = context->result;
	if (err)
		goto out;

	*status = context->status;
	if (*status)
		mthca_dbg(dev, "Command %02x completed with status %02x\n",
			  op, *status);

	if (out_is_imm)
		*out_param = context->out_param;

out:
	spin_lock(&dev->cmd.context_lock);
	context->next = dev->cmd.free_head;
	dev->cmd.free_head = context - dev->cmd.context;
	spin_unlock(&dev->cmd.context_lock);

	up(&dev->cmd.event_sem);
	return err;
}

/* Invoke a command with an output mailbox */
static int mthca_cmd_box(struct mthca_dev *dev,
			 u64 in_param,
			 u64 out_param,
			 u32 in_modifier,
			 u8 op_modifier,
			 u16 op,
			 unsigned long timeout,
			 u8 *status)
{
	if (dev->cmd.use_events)
		return mthca_cmd_wait(dev, in_param, &out_param, 0,
				      in_modifier, op_modifier, op,
				      timeout, status);
	else
		return mthca_cmd_poll(dev, in_param, &out_param, 0,
				      in_modifier, op_modifier, op,
				      timeout, status);
}

/* Invoke a command with no output parameter */
static int mthca_cmd(struct mthca_dev *dev,
		     u64 in_param,
		     u32 in_modifier,
		     u8 op_modifier,
		     u16 op,
		     unsigned long timeout,
		     u8 *status)
{
	return mthca_cmd_box(dev, in_param, 0, in_modifier,
			     op_modifier, op, timeout, status);
}

/*
 * Invoke a command with an immediate output parameter (and copy the
 * output into the caller's out_param pointer after the command
 * executes).
 */
static int mthca_cmd_imm(struct mthca_dev *dev,
			 u64 in_param,
			 u64 *out_param,
			 u32 in_modifier,
			 u8 op_modifier,
			 u16 op,
			 unsigned long timeout,
			 u8 *status)
{
	if (dev->cmd.use_events)
		return mthca_cmd_wait(dev, in_param, out_param, 1,
				      in_modifier, op_modifier, op,
				      timeout, status);
	else
		return mthca_cmd_poll(dev, in_param, out_param, 1,
				      in_modifier, op_modifier, op,
				      timeout, status);
}

/*
 * Switch to using events to issue FW commands (should be called after
 * event queue to command events has been initialized).
 */
int mthca_cmd_use_events(struct mthca_dev *dev)
{
	int i;

	dev->cmd.context = kmalloc(dev->cmd.max_cmds *
				   sizeof (struct mthca_cmd_context),
				   GFP_KERNEL);
	if (!dev->cmd.context)
		return -ENOMEM;

	for (i = 0; i < dev->cmd.max_cmds; ++i) {
		dev->cmd.context[i].token = i;
		dev->cmd.context[i].next = i + 1;
		init_timer(&dev->cmd.context[i].timer);
		dev->cmd.context[i].timer.data     =
			(unsigned long) &dev->cmd.context[i];
		dev->cmd.context[i].timer.function = event_timeout;
	}

	dev->cmd.context[dev->cmd.max_cmds - 1].next = -1;
	dev->cmd.free_head = 0;

	sema_init(&dev->cmd.event_sem, dev->cmd.max_cmds);
	spin_lock_init(&dev->cmd.context_lock);

	for (dev->cmd.token_mask = 1;
	     dev->cmd.token_mask < dev->cmd.max_cmds;
	     dev->cmd.token_mask <<= 1)
		; /* nothing */
	--dev->cmd.token_mask;

	dev->cmd.use_events = 1;
	down(&dev->cmd.poll_sem);

	return 0;
}

/*
 * Switch back to polling (used when shutting down the device)
 */
void mthca_cmd_use_polling(struct mthca_dev *dev)
{
	int i;

	dev->cmd.use_events = 0;

	for (i = 0; i < dev->cmd.max_cmds; ++i)
		down(&dev->cmd.event_sem);

	kfree(dev->cmd.context);

	up(&dev->cmd.poll_sem);
}

int mthca_SYS_EN(struct mthca_dev *dev, u8 *status)
{
	u64 out;
	int ret;

	ret = mthca_cmd_imm(dev, 0, &out, 0, 0, CMD_SYS_EN, HZ, status);

	if (*status == MTHCA_CMD_STAT_DDR_MEM_ERR)
		mthca_warn(dev, "SYS_EN DDR error: syn=%x, sock=%d, "
			   "sladdr=%d, SPD source=%s\n",
			   (int) (out >> 6) & 0xf, (int) (out >> 4) & 3,
			   (int) (out >> 1) & 7, (int) out & 1 ? "NVMEM" : "DIMM");

	return ret;
}

int mthca_SYS_DIS(struct mthca_dev *dev, u8 *status)
{
	return mthca_cmd(dev, 0, 0, 0, CMD_SYS_DIS, HZ, status);
}

static int mthca_map_cmd(struct mthca_dev *dev, u16 op, struct mthca_icm *icm,
			 u64 virt, u8 *status)
{
	u32 *inbox;
	dma_addr_t indma;
	struct mthca_icm_iter iter;
	int lg;
	int nent = 0;
	int i;
	int err = 0;
	int ts = 0, tc = 0;

	inbox = pci_alloc_consistent(dev->pdev, PAGE_SIZE, &indma);
	if (!inbox)
		return -ENOMEM;

	memset(inbox, 0, PAGE_SIZE);

	for (mthca_icm_first(icm, &iter);
	     !mthca_icm_last(&iter);
	     mthca_icm_next(&iter)) {
		/*
		 * We have to pass pages that are aligned to their
		 * size, so find the least significant 1 in the
		 * address or size and use that as our log2 size.
		 */
		lg = ffs(mthca_icm_addr(&iter) | mthca_icm_size(&iter)) - 1;
		if (lg < 12) {
			mthca_warn(dev, "Got FW area not aligned to 4K (%llx/%lx).\n",
				   (unsigned long long) mthca_icm_addr(&iter),
				   mthca_icm_size(&iter));
			err = -EINVAL;
			goto out;
		}
		for (i = 0; i < mthca_icm_size(&iter) / (1 << lg); ++i, ++nent) {
			if (virt != -1) {
				*((__be64 *) (inbox + nent * 4)) =
					cpu_to_be64(virt);
				virt += 1 << lg;
			}

			*((__be64 *) (inbox + nent * 4 + 2)) =
				cpu_to_be64((mthca_icm_addr(&iter) +
					     (i << lg)) | (lg - 12));
			ts += 1 << (lg - 10);
			++tc;

			if (nent == PAGE_SIZE / 16) {
				err = mthca_cmd(dev, indma, nent, 0, op,
						CMD_TIME_CLASS_B, status);
				if (err || *status)
					goto out;
				nent = 0;
			}
		}
	}

	if (nent)
		err = mthca_cmd(dev, indma, nent, 0, op,
				CMD_TIME_CLASS_B, status);

	switch (op) {
	case CMD_MAP_FA:
		mthca_dbg(dev, "Mapped %d chunks/%d KB for FW.\n", tc, ts);
		break;
	case CMD_MAP_ICM_AUX:
		mthca_dbg(dev, "Mapped %d chunks/%d KB for ICM aux.\n", tc, ts);
		break;
	case CMD_MAP_ICM:
		mthca_dbg(dev, "Mapped %d chunks/%d KB at %llx for ICM.\n",
			  tc, ts, (unsigned long long) virt - (ts << 10));
		break;
	}

out:
	pci_free_consistent(dev->pdev, PAGE_SIZE, inbox, indma);
	return err;
}

int mthca_MAP_FA(struct mthca_dev *dev, struct mthca_icm *icm, u8 *status)
{
	return mthca_map_cmd(dev, CMD_MAP_FA, icm, -1, status);
}

int mthca_UNMAP_FA(struct mthca_dev *dev, u8 *status)
{
	return mthca_cmd(dev, 0, 0, 0, CMD_UNMAP_FA, CMD_TIME_CLASS_B, status);
}

int mthca_RUN_FW(struct mthca_dev *dev, u8 *status)
{
	return mthca_cmd(dev, 0, 0, 0, CMD_RUN_FW, CMD_TIME_CLASS_A, status);
}

int mthca_QUERY_FW(struct mthca_dev *dev, u8 *status)
{
	u32 *outbox;
	dma_addr_t outdma;
	int err = 0;
	u8 lg;

#define QUERY_FW_OUT_SIZE             0x100
#define QUERY_FW_VER_OFFSET            0x00
#define QUERY_FW_MAX_CMD_OFFSET        0x0f
#define QUERY_FW_ERR_START_OFFSET      0x30
#define QUERY_FW_ERR_SIZE_OFFSET       0x38

#define QUERY_FW_START_OFFSET          0x20
#define QUERY_FW_END_OFFSET            0x28

#define QUERY_FW_SIZE_OFFSET           0x00
#define QUERY_FW_CLR_INT_BASE_OFFSET   0x20
#define QUERY_FW_EQ_ARM_BASE_OFFSET    0x40
#define QUERY_FW_EQ_SET_CI_BASE_OFFSET 0x48

	outbox = pci_alloc_consistent(dev->pdev, QUERY_FW_OUT_SIZE, &outdma);
	if (!outbox) {
		return -ENOMEM;
	}

	err = mthca_cmd_box(dev, 0, outdma, 0, 0, CMD_QUERY_FW,
			    CMD_TIME_CLASS_A, status);

	if (err)
		goto out;

	MTHCA_GET(dev->fw_ver,   outbox, QUERY_FW_VER_OFFSET);
	/*
	 * FW subminor version is at more signifant bits than minor
	 * version, so swap here.
	 */
	dev->fw_ver = (dev->fw_ver & 0xffff00000000ull) |
		((dev->fw_ver & 0xffff0000ull) >> 16) |
		((dev->fw_ver & 0x0000ffffull) << 16);

	MTHCA_GET(lg, outbox, QUERY_FW_MAX_CMD_OFFSET);
	dev->cmd.max_cmds = 1 << lg;

	mthca_dbg(dev, "FW version %012llx, max commands %d\n",
		  (unsigned long long) dev->fw_ver, dev->cmd.max_cmds);

	if (dev->hca_type == ARBEL_NATIVE) {
		MTHCA_GET(dev->fw.arbel.fw_pages,       outbox, QUERY_FW_SIZE_OFFSET);
		MTHCA_GET(dev->fw.arbel.clr_int_base,   outbox, QUERY_FW_CLR_INT_BASE_OFFSET);
		MTHCA_GET(dev->fw.arbel.eq_arm_base,    outbox, QUERY_FW_EQ_ARM_BASE_OFFSET);
		MTHCA_GET(dev->fw.arbel.eq_set_ci_base, outbox, QUERY_FW_EQ_SET_CI_BASE_OFFSET);
		mthca_dbg(dev, "FW size %d KB\n", dev->fw.arbel.fw_pages << 2);

		/*
		 * Arbel page size is always 4 KB; round up number of
		 * system pages needed.
		 */
		dev->fw.arbel.fw_pages =
			(dev->fw.arbel.fw_pages + (1 << (PAGE_SHIFT - 12)) - 1) >>
			(PAGE_SHIFT - 12);

		mthca_dbg(dev, "Clear int @ %llx, EQ arm @ %llx, EQ set CI @ %llx\n",
			  (unsigned long long) dev->fw.arbel.clr_int_base,
			  (unsigned long long) dev->fw.arbel.eq_arm_base,
			  (unsigned long long) dev->fw.arbel.eq_set_ci_base);
	} else {
		MTHCA_GET(dev->fw.tavor.fw_start, outbox, QUERY_FW_START_OFFSET);
		MTHCA_GET(dev->fw.tavor.fw_end,   outbox, QUERY_FW_END_OFFSET);

		mthca_dbg(dev, "FW size %d KB (start %llx, end %llx)\n",
			  (int) ((dev->fw.tavor.fw_end - dev->fw.tavor.fw_start) >> 10),
			  (unsigned long long) dev->fw.tavor.fw_start,
			  (unsigned long long) dev->fw.tavor.fw_end);
	}

out:
	pci_free_consistent(dev->pdev, QUERY_FW_OUT_SIZE, outbox, outdma);
	return err;
}

int mthca_ENABLE_LAM(struct mthca_dev *dev, u8 *status)
{
	u8 info;
	u32 *outbox;
	dma_addr_t outdma;
	int err = 0;

#define ENABLE_LAM_OUT_SIZE         0x100
#define ENABLE_LAM_START_OFFSET     0x00
#define ENABLE_LAM_END_OFFSET       0x08
#define ENABLE_LAM_INFO_OFFSET      0x13

#define ENABLE_LAM_INFO_HIDDEN_FLAG (1 << 4)
#define ENABLE_LAM_INFO_ECC_MASK    0x3

	outbox = pci_alloc_consistent(dev->pdev, ENABLE_LAM_OUT_SIZE, &outdma);
	if (!outbox)
		return -ENOMEM;

	err = mthca_cmd_box(dev, 0, outdma, 0, 0, CMD_ENABLE_LAM,
			    CMD_TIME_CLASS_C, status);

	if (err)
		goto out;

	if (*status == MTHCA_CMD_STAT_LAM_NOT_PRE)
		goto out;

	MTHCA_GET(dev->ddr_start, outbox, ENABLE_LAM_START_OFFSET);
	MTHCA_GET(dev->ddr_end,   outbox, ENABLE_LAM_END_OFFSET);
	MTHCA_GET(info,           outbox, ENABLE_LAM_INFO_OFFSET);

	if (!!(info & ENABLE_LAM_INFO_HIDDEN_FLAG) !=
	    !!(dev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN)) {
		mthca_info(dev, "FW reports that HCA-attached memory "
			   "is %s hidden; does not match PCI config\n",
			   (info & ENABLE_LAM_INFO_HIDDEN_FLAG) ?
			   "" : "not");
	}
	if (info & ENABLE_LAM_INFO_HIDDEN_FLAG)
		mthca_dbg(dev, "HCA-attached memory is hidden.\n");

	mthca_dbg(dev, "HCA memory size %d KB (start %llx, end %llx)\n",
		  (int) ((dev->ddr_end - dev->ddr_start) >> 10),
		  (unsigned long long) dev->ddr_start,
		  (unsigned long long) dev->ddr_end);

out:
	pci_free_consistent(dev->pdev, ENABLE_LAM_OUT_SIZE, outbox, outdma);
	return err;
}

int mthca_DISABLE_LAM(struct mthca_dev *dev, u8 *status)
{
	return mthca_cmd(dev, 0, 0, 0, CMD_SYS_DIS, CMD_TIME_CLASS_C, status);
}

int mthca_QUERY_DDR(struct mthca_dev *dev, u8 *status)
{
	u8 info;
	u32 *outbox;
	dma_addr_t outdma;
	int err = 0;

#define QUERY_DDR_OUT_SIZE         0x100
#define QUERY_DDR_START_OFFSET     0x00
#define QUERY_DDR_END_OFFSET       0x08
#define QUERY_DDR_INFO_OFFSET      0x13

#define QUERY_DDR_INFO_HIDDEN_FLAG (1 << 4)
#define QUERY_DDR_INFO_ECC_MASK    0x3

	outbox = pci_alloc_consistent(dev->pdev, QUERY_DDR_OUT_SIZE, &outdma);
	if (!outbox)
		return -ENOMEM;

	err = mthca_cmd_box(dev, 0, outdma, 0, 0, CMD_QUERY_DDR,
			    CMD_TIME_CLASS_A, status);

	if (err)
		goto out;

	MTHCA_GET(dev->ddr_start, outbox, QUERY_DDR_START_OFFSET);
	MTHCA_GET(dev->ddr_end,   outbox, QUERY_DDR_END_OFFSET);
	MTHCA_GET(info,           outbox, QUERY_DDR_INFO_OFFSET);

	if (!!(info & QUERY_DDR_INFO_HIDDEN_FLAG) !=
	    !!(dev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN)) {
		mthca_info(dev, "FW reports that HCA-attached memory "
			   "is %s hidden; does not match PCI config\n",
			   (info & QUERY_DDR_INFO_HIDDEN_FLAG) ?
			   "" : "not");
	}
	if (info & QUERY_DDR_INFO_HIDDEN_FLAG)
		mthca_dbg(dev, "HCA-attached memory is hidden.\n");

	mthca_dbg(dev, "HCA memory size %d KB (start %llx, end %llx)\n",
		  (int) ((dev->ddr_end - dev->ddr_start) >> 10),
		  (unsigned long long) dev->ddr_start,
		  (unsigned long long) dev->ddr_end);

out:
	pci_free_consistent(dev->pdev, QUERY_DDR_OUT_SIZE, outbox, outdma);
	return err;
}

int mthca_QUERY_DEV_LIM(struct mthca_dev *dev,
			struct mthca_dev_lim *dev_lim, u8 *status)
{
	u32 *outbox;
	dma_addr_t outdma;
	u8 field;
	u16 size;
	int err;

#define QUERY_DEV_LIM_OUT_SIZE             0x100
#define QUERY_DEV_LIM_MAX_SRQ_SZ_OFFSET     0x10
#define QUERY_DEV_LIM_MAX_QP_SZ_OFFSET      0x11
#define QUERY_DEV_LIM_RSVD_QP_OFFSET        0x12
#define QUERY_DEV_LIM_MAX_QP_OFFSET         0x13
#define QUERY_DEV_LIM_RSVD_SRQ_OFFSET       0x14
#define QUERY_DEV_LIM_MAX_SRQ_OFFSET        0x15
#define QUERY_DEV_LIM_RSVD_EEC_OFFSET       0x16
#define QUERY_DEV_LIM_MAX_EEC_OFFSET        0x17
#define QUERY_DEV_LIM_MAX_CQ_SZ_OFFSET      0x19
#define QUERY_DEV_LIM_RSVD_CQ_OFFSET        0x1a
#define QUERY_DEV_LIM_MAX_CQ_OFFSET         0x1b
#define QUERY_DEV_LIM_MAX_MPT_OFFSET        0x1d
#define QUERY_DEV_LIM_RSVD_EQ_OFFSET        0x1e
#define QUERY_DEV_LIM_MAX_EQ_OFFSET         0x1f
#define QUERY_DEV_LIM_RSVD_MTT_OFFSET       0x20
#define QUERY_DEV_LIM_MAX_MRW_SZ_OFFSET     0x21
#define QUERY_DEV_LIM_RSVD_MRW_OFFSET       0x22
#define QUERY_DEV_LIM_MAX_MTT_SEG_OFFSET    0x23
#define QUERY_DEV_LIM_MAX_AV_OFFSET         0x27
#define QUERY_DEV_LIM_MAX_REQ_QP_OFFSET     0x29
#define QUERY_DEV_LIM_MAX_RES_QP_OFFSET     0x2b
#define QUERY_DEV_LIM_MAX_RDMA_OFFSET       0x2f
#define QUERY_DEV_LIM_RSZ_SRQ_OFFSET        0x33
#define QUERY_DEV_LIM_ACK_DELAY_OFFSET      0x35
#define QUERY_DEV_LIM_MTU_WIDTH_OFFSET      0x36
#define QUERY_DEV_LIM_VL_PORT_OFFSET        0x37
#define QUERY_DEV_LIM_MAX_GID_OFFSET        0x3b
#define QUERY_DEV_LIM_MAX_PKEY_OFFSET       0x3f
#define QUERY_DEV_LIM_FLAGS_OFFSET          0x44
#define QUERY_DEV_LIM_RSVD_UAR_OFFSET       0x48
#define QUERY_DEV_LIM_UAR_SZ_OFFSET         0x49
#define QUERY_DEV_LIM_PAGE_SZ_OFFSET        0x4b
#define QUERY_DEV_LIM_MAX_SG_OFFSET         0x51
#define QUERY_DEV_LIM_MAX_DESC_SZ_OFFSET    0x52
#define QUERY_DEV_LIM_MAX_SG_RQ_OFFSET      0x55
#define QUERY_DEV_LIM_MAX_DESC_SZ_RQ_OFFSET 0x56
#define QUERY_DEV_LIM_MAX_QP_MCG_OFFSET     0x61
#define QUERY_DEV_LIM_RSVD_MCG_OFFSET       0x62
#define QUERY_DEV_LIM_MAX_MCG_OFFSET        0x63
#define QUERY_DEV_LIM_RSVD_PD_OFFSET        0x64
#define QUERY_DEV_LIM_MAX_PD_OFFSET         0x65
#define QUERY_DEV_LIM_RSVD_RDD_OFFSET       0x66
#define QUERY_DEV_LIM_MAX_RDD_OFFSET        0x67
#define QUERY_DEV_LIM_EEC_ENTRY_SZ_OFFSET   0x80
#define QUERY_DEV_LIM_QPC_ENTRY_SZ_OFFSET   0x82
#define QUERY_DEV_LIM_EEEC_ENTRY_SZ_OFFSET  0x84
#define QUERY_DEV_LIM_EQPC_ENTRY_SZ_OFFSET  0x86
#define QUERY_DEV_LIM_EQC_ENTRY_SZ_OFFSET   0x88
#define QUERY_DEV_LIM_CQC_ENTRY_SZ_OFFSET   0x8a
#define QUERY_DEV_LIM_SRQ_ENTRY_SZ_OFFSET   0x8c
#define QUERY_DEV_LIM_UAR_ENTRY_SZ_OFFSET   0x8e
#define QUERY_DEV_LIM_MTT_ENTRY_SZ_OFFSET   0x90
#define QUERY_DEV_LIM_MPT_ENTRY_SZ_OFFSET   0x92
#define QUERY_DEV_LIM_PBL_SZ_OFFSET         0x96
#define QUERY_DEV_LIM_BMME_FLAGS_OFFSET     0x97
#define QUERY_DEV_LIM_RSVD_LKEY_OFFSET      0x98
#define QUERY_DEV_LIM_LAMR_OFFSET           0x9f
#define QUERY_DEV_LIM_MAX_ICM_SZ_OFFSET     0xa0

	outbox = pci_alloc_consistent(dev->pdev, QUERY_DEV_LIM_OUT_SIZE, &outdma);
	if (!outbox)
		return -ENOMEM;

	err = mthca_cmd_box(dev, 0, outdma, 0, 0, CMD_QUERY_DEV_LIM,
			    CMD_TIME_CLASS_A, status);

	if (err)
		goto out;

	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_SRQ_SZ_OFFSET);
	dev_lim->max_srq_sz = 1 << field;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_QP_SZ_OFFSET);
	dev_lim->max_qp_sz = 1 << field;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_QP_OFFSET);
	dev_lim->reserved_qps = 1 << (field & 0xf);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_QP_OFFSET);
	dev_lim->max_qps = 1 << (field & 0x1f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_SRQ_OFFSET);
	dev_lim->reserved_srqs = 1 << (field >> 4);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_SRQ_OFFSET);
	dev_lim->max_srqs = 1 << (field & 0x1f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_EEC_OFFSET);
	dev_lim->reserved_eecs = 1 << (field & 0xf);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_EEC_OFFSET);
	dev_lim->max_eecs = 1 << (field & 0x1f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_CQ_SZ_OFFSET);
	dev_lim->max_cq_sz = 1 << field;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_CQ_OFFSET);
	dev_lim->reserved_cqs = 1 << (field & 0xf);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_CQ_OFFSET);
	dev_lim->max_cqs = 1 << (field & 0x1f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_MPT_OFFSET);
	dev_lim->max_mpts = 1 << (field & 0x3f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_EQ_OFFSET);
	dev_lim->reserved_eqs = 1 << (field & 0xf);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_EQ_OFFSET);
	dev_lim->max_eqs = 1 << (field & 0x7);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_MTT_OFFSET);
	dev_lim->reserved_mtts = 1 << (field >> 4);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_MRW_SZ_OFFSET);
	dev_lim->max_mrw_sz = 1 << field;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_MRW_OFFSET);
	dev_lim->reserved_mrws = 1 << (field & 0xf);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_MTT_SEG_OFFSET);
	dev_lim->max_mtt_seg = 1 << (field & 0x3f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_REQ_QP_OFFSET);
	dev_lim->max_requester_per_qp = 1 << (field & 0x3f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_RES_QP_OFFSET);
	dev_lim->max_responder_per_qp = 1 << (field & 0x3f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_RDMA_OFFSET);
	dev_lim->max_rdma_global = 1 << (field & 0x3f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_ACK_DELAY_OFFSET);
	dev_lim->local_ca_ack_delay = field & 0x1f;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MTU_WIDTH_OFFSET);
	dev_lim->max_mtu        = field >> 4;
	dev_lim->max_port_width = field & 0xf;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_VL_PORT_OFFSET);
	dev_lim->max_vl    = field >> 4;
	dev_lim->num_ports = field & 0xf;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_GID_OFFSET);
	dev_lim->max_gids = 1 << (field & 0xf);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_PKEY_OFFSET);
	dev_lim->max_pkeys = 1 << (field & 0xf);
	MTHCA_GET(dev_lim->flags, outbox, QUERY_DEV_LIM_FLAGS_OFFSET);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_UAR_OFFSET);
	dev_lim->reserved_uars = field >> 4;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_UAR_SZ_OFFSET);
	dev_lim->uar_size = 1 << ((field & 0x3f) + 20);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_PAGE_SZ_OFFSET);
	dev_lim->min_page_sz = 1 << field;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_SG_OFFSET);
	dev_lim->max_sg = field;

	MTHCA_GET(size, outbox, QUERY_DEV_LIM_MAX_DESC_SZ_OFFSET);
	dev_lim->max_desc_sz = size;

	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_QP_MCG_OFFSET);
	dev_lim->max_qp_per_mcg = 1 << field;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_MCG_OFFSET);
	dev_lim->reserved_mgms = field & 0xf;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_MCG_OFFSET);
	dev_lim->max_mcgs = 1 << field;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_PD_OFFSET);
	dev_lim->reserved_pds = field >> 4;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_PD_OFFSET);
	dev_lim->max_pds = 1 << (field & 0x3f);
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSVD_RDD_OFFSET);
	dev_lim->reserved_rdds = field >> 4;
	MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_RDD_OFFSET);
	dev_lim->max_rdds = 1 << (field & 0x3f);

	MTHCA_GET(size, outbox, QUERY_DEV_LIM_EEC_ENTRY_SZ_OFFSET);
	dev_lim->eec_entry_sz = size;
	MTHCA_GET(size, outbox, QUERY_DEV_LIM_QPC_ENTRY_SZ_OFFSET);
	dev_lim->qpc_entry_sz = size;
	MTHCA_GET(size, outbox, QUERY_DEV_LIM_EEEC_ENTRY_SZ_OFFSET);
	dev_lim->eeec_entry_sz = size;
	MTHCA_GET(size, outbox, QUERY_DEV_LIM_EQPC_ENTRY_SZ_OFFSET);
	dev_lim->eqpc_entry_sz = size;
	MTHCA_GET(size, outbox, QUERY_DEV_LIM_EQC_ENTRY_SZ_OFFSET);
	dev_lim->eqc_entry_sz = size;
	MTHCA_GET(size, outbox, QUERY_DEV_LIM_CQC_ENTRY_SZ_OFFSET);
	dev_lim->cqc_entry_sz = size;
	MTHCA_GET(size, outbox, QUERY_DEV_LIM_SRQ_ENTRY_SZ_OFFSET);
	dev_lim->srq_entry_sz = size;
	MTHCA_GET(size, outbox, QUERY_DEV_LIM_UAR_ENTRY_SZ_OFFSET);
	dev_lim->uar_scratch_entry_sz = size;

	mthca_dbg(dev, "Max QPs: %d, reserved QPs: %d, entry size: %d\n",
		  dev_lim->max_qps, dev_lim->reserved_qps, dev_lim->qpc_entry_sz);
	mthca_dbg(dev, "Max CQs: %d, reserved CQs: %d, entry size: %d\n",
		  dev_lim->max_cqs, dev_lim->reserved_cqs, dev_lim->cqc_entry_sz);
	mthca_dbg(dev, "Max EQs: %d, reserved EQs: %d, entry size: %d\n",
		  dev_lim->max_eqs, dev_lim->reserved_eqs, dev_lim->eqc_entry_sz);
	mthca_dbg(dev, "reserved MPTs: %d, reserved MTTs: %d\n",
		  dev_lim->reserved_mrws, dev_lim->reserved_mtts);
	mthca_dbg(dev, "Max PDs: %d, reserved PDs: %d, reserved UARs: %d\n",
		  dev_lim->max_pds, dev_lim->reserved_pds, dev_lim->reserved_uars);
	mthca_dbg(dev, "Max QP/MCG: %d, reserved MGMs: %d\n",
		  dev_lim->max_pds, dev_lim->reserved_mgms);

	mthca_dbg(dev, "Flags: %08x\n", dev_lim->flags);

	if (dev->hca_type == ARBEL_NATIVE) {
		MTHCA_GET(field, outbox, QUERY_DEV_LIM_RSZ_SRQ_OFFSET);
		dev_lim->hca.arbel.resize_srq = field & 1;
		MTHCA_GET(size, outbox, QUERY_DEV_LIM_MTT_ENTRY_SZ_OFFSET);
		dev_lim->mtt_seg_sz = size;
		MTHCA_GET(size, outbox, QUERY_DEV_LIM_MPT_ENTRY_SZ_OFFSET);
		dev_lim->mpt_entry_sz = size;
		MTHCA_GET(field, outbox, QUERY_DEV_LIM_PBL_SZ_OFFSET);
		dev_lim->hca.arbel.max_pbl_sz = 1 << (field & 0x3f);
		MTHCA_GET(dev_lim->hca.arbel.bmme_flags, outbox,
			  QUERY_DEV_LIM_BMME_FLAGS_OFFSET);
		MTHCA_GET(dev_lim->hca.arbel.reserved_lkey, outbox,
			  QUERY_DEV_LIM_RSVD_LKEY_OFFSET);
		MTHCA_GET(field, outbox, QUERY_DEV_LIM_LAMR_OFFSET);
		dev_lim->hca.arbel.lam_required = field & 1;
		MTHCA_GET(dev_lim->hca.arbel.max_icm_sz, outbox,
			  QUERY_DEV_LIM_MAX_ICM_SZ_OFFSET);

		if (dev_lim->hca.arbel.bmme_flags & 1)
			mthca_dbg(dev, "Base MM extensions: yes "
				  "(flags %d, max PBL %d, rsvd L_Key %08x)\n",
				  dev_lim->hca.arbel.bmme_flags,
				  dev_lim->hca.arbel.max_pbl_sz,
				  dev_lim->hca.arbel.reserved_lkey);
		else
			mthca_dbg(dev, "Base MM extensions: no\n");

		mthca_dbg(dev, "Max ICM size %lld MB\n",
			  (unsigned long long) dev_lim->hca.arbel.max_icm_sz >> 20);
	} else {
		MTHCA_GET(field, outbox, QUERY_DEV_LIM_MAX_AV_OFFSET);
		dev_lim->hca.tavor.max_avs = 1 << (field & 0x3f);
		dev_lim->mtt_seg_sz   = MTHCA_MTT_SEG_SIZE;
		dev_lim->mpt_entry_sz = MTHCA_MPT_ENTRY_SIZE;
	}

out:
	pci_free_consistent(dev->pdev, QUERY_DEV_LIM_OUT_SIZE, outbox, outdma);
	return err;
}

int mthca_QUERY_ADAPTER(struct mthca_dev *dev,
			struct mthca_adapter *adapter, u8 *status)
{
	u32 *outbox;
	dma_addr_t outdma;
	int err;

#define QUERY_ADAPTER_OUT_SIZE             0x100
#define QUERY_ADAPTER_VENDOR_ID_OFFSET     0x00
#define QUERY_ADAPTER_DEVICE_ID_OFFSET     0x04
#define QUERY_ADAPTER_REVISION_ID_OFFSET   0x08
#define QUERY_ADAPTER_INTA_PIN_OFFSET      0x10

	outbox = pci_alloc_consistent(dev->pdev, QUERY_ADAPTER_OUT_SIZE, &outdma);
	if (!outbox)
		return -ENOMEM;

	err = mthca_cmd_box(dev, 0, outdma, 0, 0, CMD_QUERY_ADAPTER,
			    CMD_TIME_CLASS_A, status);

	if (err)
		goto out;

	MTHCA_GET(adapter->vendor_id, outbox, QUERY_ADAPTER_VENDOR_ID_OFFSET);
	MTHCA_GET(adapter->device_id, outbox, QUERY_ADAPTER_DEVICE_ID_OFFSET);
	MTHCA_GET(adapter->revision_id, outbox, QUERY_ADAPTER_REVISION_ID_OFFSET);
	MTHCA_GET(adapter->inta_pin, outbox, QUERY_ADAPTER_INTA_PIN_OFFSET);

out:
	pci_free_consistent(dev->pdev, QUERY_DEV_LIM_OUT_SIZE, outbox, outdma);
	return err;
}

int mthca_INIT_HCA(struct mthca_dev *dev,
		   struct mthca_init_hca_param *param,
		   u8 *status)
{
	u32 *inbox;
	dma_addr_t indma;
	int err;

#define INIT_HCA_IN_SIZE             	 0x200
#define INIT_HCA_FLAGS_OFFSET        	 0x014
#define INIT_HCA_QPC_OFFSET          	 0x020
#define  INIT_HCA_QPC_BASE_OFFSET    	 (INIT_HCA_QPC_OFFSET + 0x10)
#define  INIT_HCA_LOG_QP_OFFSET      	 (INIT_HCA_QPC_OFFSET + 0x17)
#define  INIT_HCA_EEC_BASE_OFFSET    	 (INIT_HCA_QPC_OFFSET + 0x20)
#define  INIT_HCA_LOG_EEC_OFFSET     	 (INIT_HCA_QPC_OFFSET + 0x27)
#define  INIT_HCA_SRQC_BASE_OFFSET   	 (INIT_HCA_QPC_OFFSET + 0x28)
#define  INIT_HCA_LOG_SRQ_OFFSET     	 (INIT_HCA_QPC_OFFSET + 0x2f)
#define  INIT_HCA_CQC_BASE_OFFSET    	 (INIT_HCA_QPC_OFFSET + 0x30)
#define  INIT_HCA_LOG_CQ_OFFSET      	 (INIT_HCA_QPC_OFFSET + 0x37)
#define  INIT_HCA_EQPC_BASE_OFFSET   	 (INIT_HCA_QPC_OFFSET + 0x40)
#define  INIT_HCA_EEEC_BASE_OFFSET   	 (INIT_HCA_QPC_OFFSET + 0x50)
#define  INIT_HCA_EQC_BASE_OFFSET    	 (INIT_HCA_QPC_OFFSET + 0x60)
#define  INIT_HCA_LOG_EQ_OFFSET      	 (INIT_HCA_QPC_OFFSET + 0x67)
#define  INIT_HCA_RDB_BASE_OFFSET    	 (INIT_HCA_QPC_OFFSET + 0x70)
#define INIT_HCA_UDAV_OFFSET         	 0x0b0
#define  INIT_HCA_UDAV_LKEY_OFFSET   	 (INIT_HCA_UDAV_OFFSET + 0x0)
#define  INIT_HCA_UDAV_PD_OFFSET     	 (INIT_HCA_UDAV_OFFSET + 0x4)
#define INIT_HCA_MCAST_OFFSET        	 0x0c0
#define  INIT_HCA_MC_BASE_OFFSET         (INIT_HCA_MCAST_OFFSET + 0x00)
#define  INIT_HCA_LOG_MC_ENTRY_SZ_OFFSET (INIT_HCA_MCAST_OFFSET + 0x12)
#define  INIT_HCA_MC_HASH_SZ_OFFSET      (INIT_HCA_MCAST_OFFSET + 0x16)
#define  INIT_HCA_LOG_MC_TABLE_SZ_OFFSET (INIT_HCA_MCAST_OFFSET + 0x1b)
#define INIT_HCA_TPT_OFFSET              0x0f0
#define  INIT_HCA_MPT_BASE_OFFSET        (INIT_HCA_TPT_OFFSET + 0x00)
#define  INIT_HCA_MTT_SEG_SZ_OFFSET      (INIT_HCA_TPT_OFFSET + 0x09)
#define  INIT_HCA_LOG_MPT_SZ_OFFSET      (INIT_HCA_TPT_OFFSET + 0x0b)
#define  INIT_HCA_MTT_BASE_OFFSET        (INIT_HCA_TPT_OFFSET + 0x10)
#define INIT_HCA_UAR_OFFSET              0x120
#define  INIT_HCA_UAR_BASE_OFFSET        (INIT_HCA_UAR_OFFSET + 0x00)
#define  INIT_HCA_UARC_SZ_OFFSET         (INIT_HCA_UAR_OFFSET + 0x09)
#define  INIT_HCA_LOG_UAR_SZ_OFFSET      (INIT_HCA_UAR_OFFSET + 0x0a)
#define  INIT_HCA_UAR_PAGE_SZ_OFFSET     (INIT_HCA_UAR_OFFSET + 0x0b)
#define  INIT_HCA_UAR_SCATCH_BASE_OFFSET (INIT_HCA_UAR_OFFSET + 0x10)
#define  INIT_HCA_UAR_CTX_BASE_OFFSET    (INIT_HCA_UAR_OFFSET + 0x18)

	inbox = pci_alloc_consistent(dev->pdev, INIT_HCA_IN_SIZE, &indma);
	if (!inbox)
		return -ENOMEM;

	memset(inbox, 0, INIT_HCA_IN_SIZE);

#if defined(__LITTLE_ENDIAN)
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) &= ~cpu_to_be32(1 << 1);
#elif defined(__BIG_ENDIAN)
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 1);
#else
#error Host endianness not defined
#endif
	/* Check port for UD address vector: */
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1);

	/* We leave wqe_quota, responder_exu, etc as 0 (default) */

	/* QPC/EEC/CQC/EQC/RDB attributes */

	MTHCA_PUT(inbox, param->qpc_base,     INIT_HCA_QPC_BASE_OFFSET);
	MTHCA_PUT(inbox, param->log_num_qps,  INIT_HCA_LOG_QP_OFFSET);
	MTHCA_PUT(inbox, param->eec_base,     INIT_HCA_EEC_BASE_OFFSET);
	MTHCA_PUT(inbox, param->log_num_eecs, INIT_HCA_LOG_EEC_OFFSET);
	MTHCA_PUT(inbox, param->srqc_base,    INIT_HCA_SRQC_BASE_OFFSET);
	MTHCA_PUT(inbox, param->log_num_srqs, INIT_HCA_LOG_SRQ_OFFSET);
	MTHCA_PUT(inbox, param->cqc_base,     INIT_HCA_CQC_BASE_OFFSET);
	MTHCA_PUT(inbox, param->log_num_cqs,  INIT_HCA_LOG_CQ_OFFSET);
	MTHCA_PUT(inbox, param->eqpc_base,    INIT_HCA_EQPC_BASE_OFFSET);
	MTHCA_PUT(inbox, param->eeec_base,    INIT_HCA_EEEC_BASE_OFFSET);
	MTHCA_PUT(inbox, param->eqc_base,     INIT_HCA_EQC_BASE_OFFSET);
	MTHCA_PUT(inbox, param->log_num_eqs,  INIT_HCA_LOG_EQ_OFFSET);
	MTHCA_PUT(inbox, param->rdb_base,     INIT_HCA_RDB_BASE_OFFSET);

	/* UD AV attributes */

	/* multicast attributes */

	MTHCA_PUT(inbox, param->mc_base,         INIT_HCA_MC_BASE_OFFSET);
	MTHCA_PUT(inbox, param->log_mc_entry_sz, INIT_HCA_LOG_MC_ENTRY_SZ_OFFSET);
	MTHCA_PUT(inbox, param->mc_hash_sz,      INIT_HCA_MC_HASH_SZ_OFFSET);
	MTHCA_PUT(inbox, param->log_mc_table_sz, INIT_HCA_LOG_MC_TABLE_SZ_OFFSET);

	/* TPT attributes */

	MTHCA_PUT(inbox, param->mpt_base,   INIT_HCA_MPT_BASE_OFFSET);
	if (dev->hca_type != ARBEL_NATIVE)
		MTHCA_PUT(inbox, param->mtt_seg_sz, INIT_HCA_MTT_SEG_SZ_OFFSET);
	MTHCA_PUT(inbox, param->log_mpt_sz, INIT_HCA_LOG_MPT_SZ_OFFSET);
	MTHCA_PUT(inbox, param->mtt_base,   INIT_HCA_MTT_BASE_OFFSET);

	/* UAR attributes */
	{
		u8 uar_page_sz = PAGE_SHIFT - 12;
		MTHCA_PUT(inbox, uar_page_sz, INIT_HCA_UAR_PAGE_SZ_OFFSET);
	}

	MTHCA_PUT(inbox, param->uar_scratch_base, INIT_HCA_UAR_SCATCH_BASE_OFFSET);

	if (dev->hca_type == ARBEL_NATIVE) {
		MTHCA_PUT(inbox, param->log_uarc_sz, INIT_HCA_UARC_SZ_OFFSET);
		MTHCA_PUT(inbox, param->log_uar_sz,  INIT_HCA_LOG_UAR_SZ_OFFSET);
		MTHCA_PUT(inbox, param->uarc_base,   INIT_HCA_UAR_CTX_BASE_OFFSET);
	}

	err = mthca_cmd(dev, indma, 0, 0, CMD_INIT_HCA,
			HZ, status);

	pci_free_consistent(dev->pdev, INIT_HCA_IN_SIZE, inbox, indma);
	return err;
}

int mthca_INIT_IB(struct mthca_dev *dev,
		  struct mthca_init_ib_param *param,
		  int port, u8 *status)
{
	u32 *inbox;
	dma_addr_t indma;
	int err;
	u32 flags;

#define INIT_IB_IN_SIZE          56
#define INIT_IB_FLAGS_OFFSET     0x00
#define INIT_IB_FLAG_SIG         (1 << 18)
#define INIT_IB_FLAG_NG          (1 << 17)
#define INIT_IB_FLAG_G0          (1 << 16)
#define INIT_IB_FLAG_1X          (1 << 8)
#define INIT_IB_FLAG_4X          (1 << 9)
#define INIT_IB_FLAG_12X         (1 << 11)
#define INIT_IB_VL_SHIFT         4
#define INIT_IB_MTU_SHIFT        12
#define INIT_IB_MAX_GID_OFFSET   0x06
#define INIT_IB_MAX_PKEY_OFFSET  0x0a
#define INIT_IB_GUID0_OFFSET     0x10
#define INIT_IB_NODE_GUID_OFFSET 0x18
#define INIT_IB_SI_GUID_OFFSET   0x20

	inbox = pci_alloc_consistent(dev->pdev, INIT_IB_IN_SIZE, &indma);
	if (!inbox)
		return -ENOMEM;

	memset(inbox, 0, INIT_IB_IN_SIZE);

	flags = 0;
	flags |= param->enable_1x     ? INIT_IB_FLAG_1X  : 0;
	flags |= param->enable_4x     ? INIT_IB_FLAG_4X  : 0;
	flags |= param->set_guid0     ? INIT_IB_FLAG_G0  : 0;
	flags |= param->set_node_guid ? INIT_IB_FLAG_NG  : 0;
	flags |= param->set_si_guid   ? INIT_IB_FLAG_SIG : 0;
	flags |= param->vl_cap << INIT_IB_VL_SHIFT;
	flags |= param->mtu_cap << INIT_IB_MTU_SHIFT;
	MTHCA_PUT(inbox, flags, INIT_IB_FLAGS_OFFSET);

	MTHCA_PUT(inbox, param->gid_cap,   INIT_IB_MAX_GID_OFFSET);
	MTHCA_PUT(inbox, param->pkey_cap,  INIT_IB_MAX_PKEY_OFFSET);
	MTHCA_PUT(inbox, param->guid0,     INIT_IB_GUID0_OFFSET);
	MTHCA_PUT(inbox, param->node_guid, INIT_IB_NODE_GUID_OFFSET);
	MTHCA_PUT(inbox, param->si_guid,   INIT_IB_SI_GUID_OFFSET);

	err = mthca_cmd(dev, indma, port, 0, CMD_INIT_IB,
			CMD_TIME_CLASS_A, status);

	pci_free_consistent(dev->pdev, INIT_HCA_IN_SIZE, inbox, indma);
	return err;
}

int mthca_CLOSE_IB(struct mthca_dev *dev, int port, u8 *status)
{
	return mthca_cmd(dev, 0, port, 0, CMD_CLOSE_IB, HZ, status);
}

int mthca_CLOSE_HCA(struct mthca_dev *dev, int panic, u8 *status)
{
	return mthca_cmd(dev, 0, 0, panic, CMD_CLOSE_HCA, HZ, status);
}

int mthca_SET_IB(struct mthca_dev *dev, struct mthca_set_ib_param *param,
		 int port, u8 *status)
{
	u32 *inbox;
	dma_addr_t indma;
	int err;
	u32 flags = 0;

#define SET_IB_IN_SIZE         0x40
#define SET_IB_FLAGS_OFFSET    0x00
#define SET_IB_FLAG_SIG        (1 << 18)
#define SET_IB_FLAG_RQK        (1 <<  0)
#define SET_IB_CAP_MASK_OFFSET 0x04
#define SET_IB_SI_GUID_OFFSET  0x08

	inbox = pci_alloc_consistent(dev->pdev, SET_IB_IN_SIZE, &indma);
	if (!inbox)
		return -ENOMEM;

	memset(inbox, 0, SET_IB_IN_SIZE);

	flags |= param->set_si_guid     ? SET_IB_FLAG_SIG : 0;
	flags |= param->reset_qkey_viol ? SET_IB_FLAG_RQK : 0;
	MTHCA_PUT(inbox, flags, SET_IB_FLAGS_OFFSET);

	MTHCA_PUT(inbox, param->cap_mask, SET_IB_CAP_MASK_OFFSET);
	MTHCA_PUT(inbox, param->si_guid,  SET_IB_SI_GUID_OFFSET);

	err = mthca_cmd(dev, indma, port, 0, CMD_SET_IB,
			CMD_TIME_CLASS_B, status);

	pci_free_consistent(dev->pdev, INIT_HCA_IN_SIZE, inbox, indma);
	return err;
}

int mthca_MAP_ICM(struct mthca_dev *dev, struct mthca_icm *icm, u64 virt, u8 *status)
{
	return mthca_map_cmd(dev, CMD_MAP_ICM, icm, virt, status);
}

int mthca_MAP_ICM_page(struct mthca_dev *dev, u64 dma_addr, u64 virt, u8 *status)
{
	u64 *inbox;
	dma_addr_t indma;
	int err;

	inbox = pci_alloc_consistent(dev->pdev, 16, &indma);
	if (!inbox)
		return -ENOMEM;

	inbox[0] = cpu_to_be64(virt);
	inbox[1] = cpu_to_be64(dma_addr | (PAGE_SHIFT - 12));

	err = mthca_cmd(dev, indma, 1, 0, CMD_MAP_ICM, CMD_TIME_CLASS_B, status);

	pci_free_consistent(dev->pdev, 16, inbox, indma);

	if (!err)
		mthca_dbg(dev, "Mapped page at %llx for ICM.\n",
			  (unsigned long long) virt);

	return err;
}

int mthca_UNMAP_ICM(struct mthca_dev *dev, u64 virt, u32 page_count, u8 *status)
{
	return mthca_cmd(dev, virt, page_count, 0, CMD_UNMAP_ICM, CMD_TIME_CLASS_B, status);
}

int mthca_MAP_ICM_AUX(struct mthca_dev *dev, struct mthca_icm *icm, u8 *status)
{
	return mthca_map_cmd(dev, CMD_MAP_ICM_AUX, icm, -1, status);
}

int mthca_UNMAP_ICM_AUX(struct mthca_dev *dev, u8 *status)
{
	return mthca_cmd(dev, 0, 0, 0, CMD_UNMAP_ICM_AUX, CMD_TIME_CLASS_B, status);
}

int mthca_SET_ICM_SIZE(struct mthca_dev *dev, u64 icm_size, u64 *aux_pages,
		       u8 *status)
{
	int ret = mthca_cmd_imm(dev, icm_size, aux_pages, 0, 0, CMD_SET_ICM_SIZE,
				CMD_TIME_CLASS_A, status);

	if (ret || status)
		return ret;

	/*
	 * Arbel page size is always 4 KB; round up number of system
	 * pages needed.
	 */
	*aux_pages = (*aux_pages + (1 << (PAGE_SHIFT - 12)) - 1) >> (PAGE_SHIFT - 12);

	return 0;
}

int mthca_SW2HW_MPT(struct mthca_dev *dev, void *mpt_entry,
		    int mpt_index, u8 *status)
{
	dma_addr_t indma;
	int err;

	indma = pci_map_single(dev->pdev, mpt_entry,
			       MTHCA_MPT_ENTRY_SIZE,
			       PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(indma))
		return -ENOMEM;

	err = mthca_cmd(dev, indma, mpt_index, 0, CMD_SW2HW_MPT,
			CMD_TIME_CLASS_B, status);

	pci_unmap_single(dev->pdev, indma,
			 MTHCA_MPT_ENTRY_SIZE, PCI_DMA_TODEVICE);
	return err;
}

int mthca_HW2SW_MPT(struct mthca_dev *dev, void *mpt_entry,
		    int mpt_index, u8 *status)
{
	dma_addr_t outdma = 0;
	int err;

	if (mpt_entry) {
		outdma = pci_map_single(dev->pdev, mpt_entry,
					MTHCA_MPT_ENTRY_SIZE,
					PCI_DMA_FROMDEVICE);
		if (pci_dma_mapping_error(outdma))
			return -ENOMEM;
	}

	err = mthca_cmd_box(dev, 0, outdma, mpt_index, !mpt_entry,
			    CMD_HW2SW_MPT,
			    CMD_TIME_CLASS_B, status);

	if (mpt_entry)
		pci_unmap_single(dev->pdev, outdma,
				 MTHCA_MPT_ENTRY_SIZE,
				 PCI_DMA_FROMDEVICE);
	return err;
}

int mthca_WRITE_MTT(struct mthca_dev *dev, u64 *mtt_entry,
		    int num_mtt, u8 *status)
{
	dma_addr_t indma;
	int err;

	indma = pci_map_single(dev->pdev, mtt_entry,
			       (num_mtt + 2) * 8,
			       PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(indma))
		return -ENOMEM;

	err = mthca_cmd(dev, indma, num_mtt, 0, CMD_WRITE_MTT,
			CMD_TIME_CLASS_B, status);

	pci_unmap_single(dev->pdev, indma,
			 (num_mtt + 2) * 8, PCI_DMA_TODEVICE);
	return err;
}

int mthca_MAP_EQ(struct mthca_dev *dev, u64 event_mask, int unmap,
		 int eq_num, u8 *status)
{
	mthca_dbg(dev, "%s mask %016llx for eqn %d\n",
		  unmap ? "Clearing" : "Setting",
		  (unsigned long long) event_mask, eq_num);
	return mthca_cmd(dev, event_mask, (unmap << 31) | eq_num,
			 0, CMD_MAP_EQ, CMD_TIME_CLASS_B, status);
}

int mthca_SW2HW_EQ(struct mthca_dev *dev, void *eq_context,
		   int eq_num, u8 *status)
{
	dma_addr_t indma;
	int err;

	indma = pci_map_single(dev->pdev, eq_context,
			       MTHCA_EQ_CONTEXT_SIZE,
			       PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(indma))
		return -ENOMEM;

	err = mthca_cmd(dev, indma, eq_num, 0, CMD_SW2HW_EQ,
			CMD_TIME_CLASS_A, status);

	pci_unmap_single(dev->pdev, indma,
			 MTHCA_EQ_CONTEXT_SIZE, PCI_DMA_TODEVICE);
	return err;
}

int mthca_HW2SW_EQ(struct mthca_dev *dev, void *eq_context,
		   int eq_num, u8 *status)
{
	dma_addr_t outdma = 0;
	int err;

	outdma = pci_map_single(dev->pdev, eq_context,
				MTHCA_EQ_CONTEXT_SIZE,
				PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(outdma))
		return -ENOMEM;

	err = mthca_cmd_box(dev, 0, outdma, eq_num, 0,
			    CMD_HW2SW_EQ,
			    CMD_TIME_CLASS_A, status);

	pci_unmap_single(dev->pdev, outdma,
			 MTHCA_EQ_CONTEXT_SIZE,
			 PCI_DMA_FROMDEVICE);
	return err;
}

int mthca_SW2HW_CQ(struct mthca_dev *dev, void *cq_context,
		   int cq_num, u8 *status)
{
	dma_addr_t indma;
	int err;

	indma = pci_map_single(dev->pdev, cq_context,
			       MTHCA_CQ_CONTEXT_SIZE,
			       PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(indma))
		return -ENOMEM;

	err = mthca_cmd(dev, indma, cq_num, 0, CMD_SW2HW_CQ,
			CMD_TIME_CLASS_A, status);

	pci_unmap_single(dev->pdev, indma,
			 MTHCA_CQ_CONTEXT_SIZE, PCI_DMA_TODEVICE);
	return err;
}

int mthca_HW2SW_CQ(struct mthca_dev *dev, void *cq_context,
		   int cq_num, u8 *status)
{
	dma_addr_t outdma = 0;
	int err;

	outdma = pci_map_single(dev->pdev, cq_context,
				MTHCA_CQ_CONTEXT_SIZE,
				PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(outdma))
		return -ENOMEM;

	err = mthca_cmd_box(dev, 0, outdma, cq_num, 0,
			    CMD_HW2SW_CQ,
			    CMD_TIME_CLASS_A, status);

	pci_unmap_single(dev->pdev, outdma,
			 MTHCA_CQ_CONTEXT_SIZE,
			 PCI_DMA_FROMDEVICE);
	return err;
}

int mthca_MODIFY_QP(struct mthca_dev *dev, int trans, u32 num,
		    int is_ee, void *qp_context, u32 optmask,
		    u8 *status)
{
	static const u16 op[] = {
		[MTHCA_TRANS_RST2INIT]  = CMD_RST2INIT_QPEE,
		[MTHCA_TRANS_INIT2INIT] = CMD_INIT2INIT_QPEE,
		[MTHCA_TRANS_INIT2RTR]  = CMD_INIT2RTR_QPEE,
		[MTHCA_TRANS_RTR2RTS]   = CMD_RTR2RTS_QPEE,
		[MTHCA_TRANS_RTS2RTS]   = CMD_RTS2RTS_QPEE,
		[MTHCA_TRANS_SQERR2RTS] = CMD_SQERR2RTS_QPEE,
		[MTHCA_TRANS_ANY2ERR]   = CMD_2ERR_QPEE,
		[MTHCA_TRANS_RTS2SQD]   = CMD_RTS2SQD_QPEE,
		[MTHCA_TRANS_SQD2SQD]   = CMD_SQD2SQD_QPEE,
		[MTHCA_TRANS_SQD2RTS]   = CMD_SQD2RTS_QPEE,
		[MTHCA_TRANS_ANY2RST]   = CMD_ERR2RST_QPEE
	};
	u8 op_mod = 0;

	dma_addr_t indma;
	int err;

	if (trans < 0 || trans >= ARRAY_SIZE(op))
		return -EINVAL;

	if (trans == MTHCA_TRANS_ANY2RST) {
		indma  = 0;
		op_mod = 3;	/* don't write outbox, any->reset */

		/* For debugging */
		qp_context = pci_alloc_consistent(dev->pdev, MTHCA_QP_CONTEXT_SIZE,
						  &indma);
		op_mod = 2;	/* write outbox, any->reset */
	} else {
		indma = pci_map_single(dev->pdev, qp_context,
				       MTHCA_QP_CONTEXT_SIZE,
				       PCI_DMA_TODEVICE);
		if (pci_dma_mapping_error(indma))
			return -ENOMEM;

		if (0) {
			int i;
			mthca_dbg(dev, "Dumping QP context:\n");
			printk(" %08x\n", be32_to_cpup(qp_context));
			for (i = 0; i < 0x100 / 4; ++i) {
				if (i % 8 == 0)
					printk("[%02x] ", i * 4);
				printk(" %08x", be32_to_cpu(((u32 *) qp_context)[i + 2]));
				if ((i + 1) % 8 == 0)
					printk("\n");
			}
		}
	}

	if (trans == MTHCA_TRANS_ANY2RST) {
		err = mthca_cmd_box(dev, 0, indma, (!!is_ee << 24) | num,
				    op_mod, op[trans], CMD_TIME_CLASS_C, status);

		if (0) {
			int i;
			mthca_dbg(dev, "Dumping QP context:\n");
			printk(" %08x\n", be32_to_cpup(qp_context));
			for (i = 0; i < 0x100 / 4; ++i) {
				if (i % 8 == 0)
					printk("[%02x] ", i * 4);
				printk(" %08x", be32_to_cpu(((u32 *) qp_context)[i + 2]));
				if ((i + 1) % 8 == 0)
					printk("\n");
			}
		}

	} else
		err = mthca_cmd(dev, indma, (!!is_ee << 24) | num,
				op_mod, op[trans], CMD_TIME_CLASS_C, status);

	if (trans != MTHCA_TRANS_ANY2RST)
		pci_unmap_single(dev->pdev, indma,
				 MTHCA_QP_CONTEXT_SIZE, PCI_DMA_TODEVICE);
	else
		pci_free_consistent(dev->pdev, MTHCA_QP_CONTEXT_SIZE,
				    qp_context, indma);
	return err;
}

int mthca_QUERY_QP(struct mthca_dev *dev, u32 num, int is_ee,
		   void *qp_context, u8 *status)
{
	dma_addr_t outdma = 0;
	int err;

	outdma = pci_map_single(dev->pdev, qp_context,
				MTHCA_QP_CONTEXT_SIZE,
				PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(outdma))
		return -ENOMEM;

	err = mthca_cmd_box(dev, 0, outdma, (!!is_ee << 24) | num, 0,
			    CMD_QUERY_QPEE,
			    CMD_TIME_CLASS_A, status);

	pci_unmap_single(dev->pdev, outdma,
			 MTHCA_QP_CONTEXT_SIZE,
			 PCI_DMA_FROMDEVICE);
	return err;
}

int mthca_CONF_SPECIAL_QP(struct mthca_dev *dev, int type, u32 qpn,
			  u8 *status)
{
	u8 op_mod;

	switch (type) {
	case IB_QPT_SMI:
		op_mod = 0;
		break;
	case IB_QPT_GSI:
		op_mod = 1;
		break;
	case IB_QPT_RAW_IPV6:
		op_mod = 2;
		break;
	case IB_QPT_RAW_ETY:
		op_mod = 3;
		break;
	default:
		return -EINVAL;
	}

	return mthca_cmd(dev, 0, qpn, op_mod, CMD_CONF_SPECIAL_QP,
			 CMD_TIME_CLASS_B, status);
}

int mthca_MAD_IFC(struct mthca_dev *dev, int ignore_mkey, int ignore_bkey,
		  int port, struct ib_wc* in_wc, struct ib_grh* in_grh,
		  void *in_mad, void *response_mad, u8 *status)
{
	void *box;
	dma_addr_t dma;
	int err;
	u32 in_modifier = port;
	u8 op_modifier = 0;

#define MAD_IFC_BOX_SIZE      0x400
#define MAD_IFC_MY_QPN_OFFSET 0x100
#define MAD_IFC_RQPN_OFFSET   0x104
#define MAD_IFC_SL_OFFSET     0x108
#define MAD_IFC_G_PATH_OFFSET 0x109
#define MAD_IFC_RLID_OFFSET   0x10a
#define MAD_IFC_PKEY_OFFSET   0x10e
#define MAD_IFC_GRH_OFFSET    0x140

	box = pci_alloc_consistent(dev->pdev, MAD_IFC_BOX_SIZE, &dma);
	if (!box)
		return -ENOMEM;

	memcpy(box, in_mad, 256);

	/*
	 * Key check traps can't be generated unless we have in_wc to
	 * tell us where to send the trap.
	 */
	if (ignore_mkey || !in_wc)
		op_modifier |= 0x1;
	if (ignore_bkey || !in_wc)
		op_modifier |= 0x2;

	if (in_wc) {
		u8 val;

		memset(box + 256, 0, 256);

		MTHCA_PUT(box, in_wc->qp_num, 	  MAD_IFC_MY_QPN_OFFSET);
		MTHCA_PUT(box, in_wc->src_qp, 	  MAD_IFC_RQPN_OFFSET);

		val = in_wc->sl << 4;
		MTHCA_PUT(box, val,               MAD_IFC_SL_OFFSET);

		val = in_wc->dlid_path_bits |
			(in_wc->wc_flags & IB_WC_GRH ? 0x80 : 0);
		MTHCA_PUT(box, val,               MAD_IFC_GRH_OFFSET);

		MTHCA_PUT(box, in_wc->slid,       MAD_IFC_RLID_OFFSET);
		MTHCA_PUT(box, in_wc->pkey_index, MAD_IFC_PKEY_OFFSET);

		if (in_grh)
			memcpy((u8 *) box + MAD_IFC_GRH_OFFSET, in_grh, 40);

		op_modifier |= 0x10;

		in_modifier |= in_wc->slid << 16;
	}

	err = mthca_cmd_box(dev, dma, dma + 512, in_modifier, op_modifier,
			    CMD_MAD_IFC, CMD_TIME_CLASS_C, status);

	if (!err && !*status)
		memcpy(response_mad, box + 512, 256);

	pci_free_consistent(dev->pdev, MAD_IFC_BOX_SIZE, box, dma);
	return err;
}

int mthca_READ_MGM(struct mthca_dev *dev, int index, void *mgm,
		   u8 *status)
{
	dma_addr_t outdma = 0;
	int err;

	outdma = pci_map_single(dev->pdev, mgm,
				MTHCA_MGM_ENTRY_SIZE,
				PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(outdma))
		return -ENOMEM;

	err = mthca_cmd_box(dev, 0, outdma, index, 0,
			    CMD_READ_MGM,
			    CMD_TIME_CLASS_A, status);

	pci_unmap_single(dev->pdev, outdma,
			 MTHCA_MGM_ENTRY_SIZE,
			 PCI_DMA_FROMDEVICE);
	return err;
}

int mthca_WRITE_MGM(struct mthca_dev *dev, int index, void *mgm,
		    u8 *status)
{
	dma_addr_t indma;
	int err;

	indma = pci_map_single(dev->pdev, mgm,
			       MTHCA_MGM_ENTRY_SIZE,
			       PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(indma))
		return -ENOMEM;

	err = mthca_cmd(dev, indma, index, 0, CMD_WRITE_MGM,
			CMD_TIME_CLASS_A, status);

	pci_unmap_single(dev->pdev, indma,
			 MTHCA_MGM_ENTRY_SIZE, PCI_DMA_TODEVICE);
	return err;
}

int mthca_MGID_HASH(struct mthca_dev *dev, void *gid, u16 *hash,
		    u8 *status)
{
	dma_addr_t indma;
	u64 imm;
	int err;

	indma = pci_map_single(dev->pdev, gid, 16, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(indma))
		return -ENOMEM;

	err = mthca_cmd_imm(dev, indma, &imm, 0, 0, CMD_MGID_HASH,
			    CMD_TIME_CLASS_A, status);
	*hash = imm;

	pci_unmap_single(dev->pdev, indma, 16, PCI_DMA_TODEVICE);
	return err;
}

int mthca_NOP(struct mthca_dev *dev, u8 *status)
{
	return mthca_cmd(dev, 0, 0x1f, 0, CMD_NOP, msecs_to_jiffies(100), status);
}

/*
 * SCSI low-level driver for the MESH (Macintosh Enhanced SCSI Hardware)
 * bus adaptor found on Power Macintosh computers.
 * We assume the MESH is connected to a DBDMA (descriptor-based DMA)
 * controller.
 *
 * Paul Mackerras, August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/blk.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <asm/dbdma.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/spinlock.h>

#include "scsi.h"
#include "hosts.h"
#include "mesh.h"

#if 0
#undef KERN_DEBUG
#define KERN_DEBUG KERN_WARNING
#endif

#if CONFIG_SCSI_MESH_SYNC_RATE == 0
int mesh_sync_period = 100;
int mesh_sync_offset = 0;
#else
int mesh_sync_period = 1000 / CONFIG_SCSI_MESH_SYNC_RATE;	/* ns */
int mesh_sync_offset = 15;
#endif

int mesh_sync_targets = 0xff;	/* targets to set synchronous (bitmap) */
int mesh_resel_targets = 0xff;	/* targets that we let disconnect (bitmap) */
int mesh_debug_targets = 0;	/* print debug for these targets */

#define ALLOW_SYNC(tgt)		((mesh_sync_targets >> (tgt)) & 1)
#define ALLOW_RESEL(tgt)	((mesh_resel_targets >> (tgt)) & 1)
#define ALLOW_DEBUG(tgt)	((mesh_debug_targets >> (tgt)) & 1)
#define DEBUG_TARGET(cmd)	((cmd) && ALLOW_DEBUG((cmd)->target))

struct proc_dir_entry proc_scsi_mesh = {
	PROC_SCSI_MESH, 4, "mesh",
	S_IFDIR | S_IRUGO | S_IXUGO, 2
};

enum mesh_phase {
	idle,
	arbitrating,
	selecting,
	commanding,
	dataing,
	statusing,
	busfreeing,
	disconnecting,
	reselecting
};

enum msg_phase {
	msg_none,
	msg_out,
	msg_out_xxx,
	msg_out_last,
	msg_in,
};

enum sdtr_phase {
	do_sdtr,
	sdtr_sent,
	sdtr_done
};

struct mesh_target {
	enum sdtr_phase sdtr_state;
	enum mesh_phase phase;
	int	sync_params;
	int	data_goes_out;
	Scsi_Cmnd *current_req;
	u32	saved_ptr;
};

struct mesh_state {
	volatile struct	mesh_regs *mesh;
	int	meshintr;
	volatile struct	dbdma_regs *dma;
	int	dmaintr;
	struct	Scsi_Host *host;
	struct	mesh_state *next;
	Scsi_Cmnd *request_q;
	Scsi_Cmnd *request_qtail;
	enum mesh_phase phase;		/* what we're currently trying to do */
	enum msg_phase msgphase;
	int	conn_tgt;		/* target we're connected to */
	Scsi_Cmnd *current_req;		/* req we're currently working on */
	int	data_ptr;
	int	data_goes_out;		/* guess as to data direction */
	int	dma_started;
	int	dma_count;
	int	expect_reply;
	int	n_msgin;
	u8	msgin[16];
	int	n_msgout;
	int	last_n_msgout;
	u8	msgout[16];
	struct dbdma_cmd *dma_cmds;	/* space for dbdma commands, aligned */
	int	clk_freq;
	struct mesh_target tgts[8];
	struct tq_struct tqueue;
	Scsi_Cmnd *completed_q;
	Scsi_Cmnd *completed_qtail;
};

static struct mesh_state *all_meshes;

static void mesh_init(struct mesh_state *);
static int mesh_notify_reboot(struct notifier_block *, unsigned long, void *);
static void mesh_dump_regs(struct mesh_state *);
static void mesh_start(struct mesh_state *);
static void finish_cmds(void *);
static void add_sdtr_msg(struct mesh_state *);
static void set_sdtr(struct mesh_state *, int, int);
static void start_phase(struct mesh_state *);
static void get_msgin(struct mesh_state *);
static int msgin_length(struct mesh_state *);
static void cmd_complete(struct mesh_state *);
static void phase_mismatch(struct mesh_state *);
static void reselected(struct mesh_state *);
static void handle_reset(struct mesh_state *);
static void mesh_interrupt(int, void *, struct pt_regs *);
static void do_mesh_interrupt(int, void *, struct pt_regs *);
static void handle_msgin(struct mesh_state *);
static void mesh_done(struct mesh_state *);
static void mesh_completed(struct mesh_state *, Scsi_Cmnd *);
static void set_dma_cmds(struct mesh_state *, Scsi_Cmnd *);
static void halt_dma(struct mesh_state *);
static int data_goes_out(Scsi_Cmnd *);

static struct notifier_block mesh_notifier = {
	mesh_notify_reboot,
	NULL,
	0
};

int
mesh_detect(Scsi_Host_Template *tp)
{
	struct device_node *mesh;
	int nmeshes, tgt, *cfp, minper;
	struct mesh_state *ms, **prev_statep;
	struct Scsi_Host *mesh_host;
	void *dma_cmd_space;

	nmeshes = 0;
	prev_statep = &all_meshes;
	for (mesh = find_devices("mesh"); mesh != 0; mesh = mesh->next) {
		if (mesh->n_addrs != 2 || mesh->n_intrs != 2)
			panic("mesh: expected 2 addrs and intrs (got %d/%d)",
			      mesh->n_addrs, mesh->n_intrs);
		mesh_host = scsi_register(tp, sizeof(struct mesh_state));
		if (mesh_host == 0)
			panic("couldn't register mesh host");
		mesh_host->unique_id = nmeshes;
		note_scsi_host(mesh, mesh_host);

		ms = (struct mesh_state *) mesh_host->hostdata;
		if (ms == 0)
			panic("no mesh state");
		memset(ms, 0, sizeof(*ms));
		ms->host = mesh_host;
		ms->mesh = (volatile struct mesh_regs *)
			mesh->addrs[0].address;
		ms->meshintr = mesh->intrs[0];
		ms->dma = (volatile struct dbdma_regs *)
			mesh->addrs[1].address;
		ms->dmaintr = mesh->intrs[1];

		/* Space for dma command list: +1 for stop command,
		   +1 to allow for aligning. */
		dma_cmd_space = kmalloc((mesh_host->sg_tablesize + 2) *
					sizeof(struct dbdma_cmd), GFP_KERNEL);
		if (dma_cmd_space == 0)
			panic("mesh: couldn't allocate dma command space");
		ms->dma_cmds = (struct dbdma_cmd *) DBDMA_ALIGN(dma_cmd_space);
		memset(ms->dma_cmds, 0, (mesh_host->sg_tablesize + 1)
		       * sizeof(struct dbdma_cmd));

		ms->current_req = 0;
		for (tgt = 0; tgt < 8; ++tgt) {
			ms->tgts[tgt].sdtr_state = do_sdtr;
			ms->tgts[tgt].sync_params = ASYNC_PARAMS;
			ms->tgts[tgt].current_req = 0;
		}

		ms->tqueue.routine = finish_cmds;
		ms->tqueue.data = ms;

		*prev_statep = ms;
		prev_statep = &ms->next;

		if (request_irq(ms->meshintr, do_mesh_interrupt, 0, "MESH", ms)) {
			printk(KERN_ERR "MESH: can't get irq %d\n", ms->meshintr);
		}

		cfp = (int *) get_property(mesh, "clock-frequency", NULL);
		if (cfp) {
			ms->clk_freq = *cfp;
		} else {
			printk(KERN_INFO "mesh: assuming 50MHz clock frequency\n");
			ms->clk_freq = 50000000;
		}
		/* The maximum sync rate is clock / 5; increase
		   mesh_sync_period if necessary. */
		minper = 1000000000 / (ms->clk_freq / 5);	/* ns */
		if (mesh_sync_period < minper)
			mesh_sync_period = minper;

		mesh_init(ms);

		++nmeshes;
	}
	if (nmeshes > 0)
		register_reboot_notifier(&mesh_notifier);

	return nmeshes;
}

int
mesh_queue(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	unsigned long flags;
	struct mesh_state *ms;

#if 0
	if (data_goes_out(cmd)) {
		printk(KERN_DEBUG "mesh_queue %p: command is", cmd);
		for (i = 0; i < cmd->cmd_len; ++i)
			printk(" %.2x", cmd->cmnd[i]);
		printk("\n" KERN_DEBUG "use_sg=%d request_bufflen=%d request_buffer=%p\n",
		       cmd->use_sg, cmd->request_bufflen, cmd->request_buffer);
	}
#endif

	cmd->scsi_done = done;
	cmd->host_scribble = NULL;

	ms = (struct mesh_state *) cmd->host->hostdata;

	save_flags(flags);
	cli();
	if (ms->request_q == NULL)
		ms->request_q = cmd;
	else
		ms->request_qtail->host_scribble = (void *) cmd;
	ms->request_qtail = cmd;

	if (ms->phase == idle)
		mesh_start(ms);

	restore_flags(flags);
	return 0;
}

int
mesh_abort(Scsi_Cmnd *cmd)
{
	printk(KERN_DEBUG "mesh_abort(%p)\n", cmd);
	mesh_dump_regs((struct mesh_state *)(cmd->host->hostdata));
	return SCSI_ABORT_SNOOZE;
}

static void
mesh_dump_regs(struct mesh_state *ms)
{
	volatile struct mesh_regs *mr = ms->mesh;
	volatile struct dbdma_regs *md = ms->dma;
	int t;
	struct mesh_target *tp;

	printk(KERN_DEBUG "mesh: state at %p, regs at %p, dma at %p\n",
	       ms, mr, md);
	printk(KERN_DEBUG "    ct=%4x seq=%2x bs=%4x fc=%2x exc=%2x err=%2x sp=%2x\n",
	       (mr->count_hi << 8) + mr->count_lo, mr->sequence,
	       (mr->bus_status1 << 8) + mr->bus_status0, mr->fifo_count,
	       mr->exception, mr->error, mr->sync_params);
	printk(KERN_DEBUG "    dma stat=%x cmdptr=%x\n",
	       in_le32(&md->status), in_le32(&md->cmdptr));
	printk(KERN_DEBUG "    phase=%d msgphase=%d conn_tgt=%d data_ptr=%d\n",
	       ms->phase, ms->msgphase, ms->conn_tgt, ms->data_ptr);
	printk(KERN_DEBUG "    goes_out=%d dma_st=%d dma_ct=%d n_msgout=%d\n",
	       ms->data_goes_out, ms->dma_started, ms->dma_count, ms->n_msgout);
	for (t = 0; t < 8; ++t) {
		tp = &ms->tgts[t];
		if (tp->current_req == NULL)
			continue;
		printk(KERN_DEBUG "    target %d: req=%p phase=%d saved_ptr=%d\n",
		       t, tp->current_req, tp->phase, tp->saved_ptr);
	}
}

int
mesh_reset(Scsi_Cmnd *cmd, unsigned how)
{
	struct mesh_state *ms = (struct mesh_state *) cmd->host->hostdata;
	volatile struct mesh_regs *mr = ms->mesh;
	volatile struct dbdma_regs *md = ms->dma;
	unsigned long flags;
	int ret;

	printk(KERN_DEBUG "mesh_reset %x\n", how);
	ret = SCSI_RESET_BUS_RESET;
	save_flags(flags);
	cli();
	out_8(&mr->exception, 0xff);	/* clear all exception bits */
	out_8(&mr->error, 0xff);	/* clear all error bits */
	out_le32(&md->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
	if (how & SCSI_RESET_SUGGEST_HOST_RESET) {
		out_8(&mr->sequence, SEQ_RESETMESH);
		ret |= SCSI_RESET_HOST_RESET;
		udelay(1);
		out_8(&mr->intr_mask, INT_ERROR | INT_EXCEPTION | INT_CMDDONE);
	}
	out_8(&mr->bus_status1, BS1_RST);	/* assert RST */
	udelay(30);			/* leave it on for >= 25us */
	out_8(&mr->bus_status1, 0);	/* negate RST */
#ifdef DO_ASYNC_RESET
	if (how & SCSI_RESET_ASYNCHRONOUS) {
		restore_flags(flags);
		ret |= SCSI_RESET_PENDING;
	} else
#endif
	{
		out_8(&mr->interrupt, INT_ERROR | INT_EXCEPTION | INT_CMDDONE);
		handle_reset(ms);
		restore_flags(flags);
		finish_cmds(ms);
		ret |= SCSI_RESET_SUCCESS;
	}
	return ret;
}

/*
 * If we leave drives set for synchronous transfers (especially
 * CDROMs), and reboot to MacOS, it gets confused, poor thing.
 * So, on reboot we reset the SCSI bus.
 */
static int
mesh_notify_reboot(struct notifier_block *this, unsigned long code, void *x)
{
	struct mesh_state *ms;
	volatile struct mesh_regs *mr;

	if (code == SYS_DOWN || code == SYS_HALT) {
		printk(KERN_INFO "resetting MESH scsi bus(es)\n");
		for (ms = all_meshes; ms != 0; ms = ms->next) {
			mr = ms->mesh;
			out_8(&mr->intr_mask, 0);
			out_8(&mr->interrupt,
			      INT_ERROR | INT_EXCEPTION | INT_CMDDONE);
			out_8(&mr->bus_status1, BS1_RST);
			udelay(30);
			out_8(&mr->bus_status1, 0);
		}
	}
	return NOTIFY_DONE;
}

int
mesh_command(Scsi_Cmnd *cmd)
{
	printk(KERN_WARNING "whoops... mesh_command called\n");
	return -1;
}

static void
mesh_init(struct mesh_state *ms)
{
	volatile struct mesh_regs *mr = ms->mesh;
	volatile struct dbdma_regs *md = ms->dma;

	out_8(&mr->interrupt, 0xff);	/* clear all interrupt bits */
	out_8(&mr->intr_mask, INT_ERROR | INT_EXCEPTION | INT_CMDDONE);
	out_8(&mr->source_id, ms->host->this_id);
	out_8(&mr->sel_timeout, 25);	/* 250ms */
	out_8(&mr->sync_params, ASYNC_PARAMS);	/* asynchronous initially */
	out_le32(&md->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
}

/*
 * Start the next command for a MESH.
 * Should be called with interrupts disabled.
 */
static void
mesh_start(struct mesh_state *ms)
{
	Scsi_Cmnd *cmd, *prev, *next;
	volatile struct mesh_regs *mr = ms->mesh;

	if (ms->phase != idle || ms->current_req != NULL)
		panic("inappropriate mesh_start (ms=%p)", ms);

	prev = NULL;
	for (cmd = ms->request_q; ; cmd = (Scsi_Cmnd *) cmd->host_scribble) {
		if (cmd == NULL)
			return;
		if (ms->tgts[cmd->target].current_req == NULL)
			break;
		prev = cmd;
	}
	next = (Scsi_Cmnd *) cmd->host_scribble;
	if (prev == NULL)
		ms->request_q = next;
	else
		prev->host_scribble = (void *) next;
	if (next == NULL)
		ms->request_qtail = prev;

	ms->current_req = cmd;
	ms->data_goes_out = data_goes_out(cmd);
	ms->tgts[cmd->target].current_req = cmd;

#if 1
	if (DEBUG_TARGET(cmd)) {
		int i;
		printk(KERN_DEBUG "mesh_start: %p ser=%lu tgt=%d cmd=",
		       cmd, cmd->serial_number, cmd->target);
		for (i = 0; i < cmd->cmd_len; ++i)
			printk(" %x", cmd->cmnd[i]);
		printk(" use_sg=%d buffer=%p bufflen=%u\n",
		       cmd->use_sg, cmd->request_buffer, cmd->request_bufflen);
	}
#endif

	/* Off we go */
	out_8(&mr->sequence, SEQ_ARBITRATE);

	ms->phase = arbitrating;
	ms->msgphase = msg_none;
	ms->data_ptr = 0;
	ms->dma_started = 0;
	ms->n_msgout = 0;
	ms->last_n_msgout = 0;
	ms->expect_reply = 0;
	ms->conn_tgt = cmd->target;
	ms->tgts[cmd->target].saved_ptr = 0;
}

static void
finish_cmds(void *data)
{
	struct mesh_state *ms = data;
	Scsi_Cmnd *cmd;
	unsigned long flags;

	for (;;) {
		save_flags(flags);
		cli();
		cmd = ms->completed_q;
		if (cmd == NULL) {
			restore_flags(flags);
			break;
		}
		ms->completed_q = (Scsi_Cmnd *) cmd->host_scribble;
		restore_flags(flags);
		(*cmd->scsi_done)(cmd);
	}
}

static inline void
add_sdtr_msg(struct mesh_state *ms)
{
	int i = ms->n_msgout;

	ms->msgout[i] = EXTENDED_MESSAGE;
	ms->msgout[i+1] = 3;
	ms->msgout[i+2] = EXTENDED_SDTR;
	ms->msgout[i+3] = mesh_sync_period/4;
	ms->msgout[i+4] = (ALLOW_SYNC(ms->conn_tgt)? mesh_sync_offset: 0);
	ms->n_msgout = i + 5;
}

static void
set_sdtr(struct mesh_state *ms, int period, int offset)
{
	struct mesh_target *tp = &ms->tgts[ms->conn_tgt];
	volatile struct mesh_regs *mr = ms->mesh;
	int v, tr;

	tp->sdtr_state = sdtr_done;
	if (offset == 0) {
		/* asynchronous */
		if (SYNC_OFF(tp->sync_params))
			printk(KERN_INFO "mesh: target %d now asynchronous\n",
			       ms->conn_tgt);
		tp->sync_params = ASYNC_PARAMS;
		out_8(&mr->sync_params, ASYNC_PARAMS);
		return;
	}
	/*
	 * We need to compute ceil(clk_freq * period / 500e6) - 2
	 * without incurring overflow.
	 */
	v = (ms->clk_freq / 5000) * period;
	if (v <= 250000) {
		/* special case: sync_period == 5 * clk_period */
		v = 0;
		/* units of tr are 100kB/s */
		tr = (ms->clk_freq + 250000) / 500000;
	} else {
		/* sync_period == (v + 2) * 2 * clk_period */
		v = (v + 99999) / 100000 - 2;
		if (v > 15)
			v = 15;	/* oops */
		tr = ((ms->clk_freq / (v + 2)) + 199999) / 200000;
	}
	if (offset > 15)
		offset = 15;	/* can't happen */
	tp->sync_params = SYNC_PARAMS(offset, v);
	out_8(&mr->sync_params, tp->sync_params);
	printk(KERN_INFO "mesh: target %d synchronous at %d.%d MB/s\n",
	       ms->conn_tgt, tr/10, tr%10);
}

static void
start_phase(struct mesh_state *ms)
{
	int i, seq, nb;
	volatile struct mesh_regs *mr = ms->mesh;
	volatile struct dbdma_regs *md = ms->dma;
	Scsi_Cmnd *cmd = ms->current_req;
	struct mesh_target *tp = &ms->tgts[ms->conn_tgt];

	if (cmd == 0) {
		printk(KERN_ERR "mesh: start_phase but no cmd?\n");
		return;
	}
	seq = SEQ_ACTIVE_NEG + (ms->n_msgout? SEQ_ATN: 0);
	switch (ms->msgphase) {
	case msg_none:
		break;

	case msg_in:
		out_8(&mr->count_hi, 0);
		out_8(&mr->count_lo, 1);
		out_8(&mr->sequence, SEQ_MSGIN + seq);
		ms->n_msgin = 0;
		return;

	case msg_out:
		/*
		 * To make sure ATN drops before we assert ACK for
		 * the last byte of the message, we have to do the
		 * last byte specially.
		 */
		if (DEBUG_TARGET(cmd)) {
			printk(KERN_DEBUG "mesh: sending %d msg bytes:",
			       ms->n_msgout);
			for (i = 0; i < ms->n_msgout; ++i)
				printk(" %x", ms->msgout[i]);
			printk("\n");
		}
		out_8(&mr->count_hi, 0);
		if (ms->n_msgout == 1) {
			out_8(&mr->count_lo, 1);
			out_8(&mr->sequence, SEQ_MSGOUT + SEQ_ACTIVE_NEG);
			udelay(1);
			out_8(&mr->fifo, ms->msgout[0]);
			ms->msgphase = msg_out_last;
		} else {
			out_8(&mr->count_lo, ms->n_msgout - 1);
			out_8(&mr->sequence, SEQ_MSGOUT + seq);
			for (i = 0; i < ms->n_msgout - 1; ++i)
				out_8(&mr->fifo, ms->msgout[i]);
		}
		return;

	default:
		printk(KERN_ERR "mesh bug: start_phase msgphase=%d\n",
		       ms->msgphase);
	}

	switch (ms->phase) {
	case selecting:
		out_8(&mr->dest_id, cmd->target);
		out_8(&mr->sequence, SEQ_SELECT + SEQ_ATN);
		break;
	case commanding:
		out_8(&mr->sync_params, tp->sync_params);
		out_8(&mr->count_hi, 0);
		out_8(&mr->count_lo, cmd->cmd_len);
		out_8(&mr->sequence, SEQ_COMMAND + seq);
		for (i = 0; i < cmd->cmd_len; ++i)
			out_8(&mr->fifo, cmd->cmnd[i]);
		break;
	case dataing:
		/* transfer data, if any */
		if (!ms->dma_started) {
			set_dma_cmds(ms, cmd);
			out_le32(&md->cmdptr, virt_to_phys(ms->dma_cmds));
			out_le32(&md->control, (RUN << 16) | RUN);
			ms->dma_started = 1;
		}
		nb = ms->dma_count;
		if (nb > 0xfff0)
			nb = 0xfff0;
		ms->dma_count -= nb;
		ms->data_ptr += nb;
		out_8(&mr->count_lo, nb);
		out_8(&mr->count_hi, nb >> 8);
		out_8(&mr->sequence, (ms->data_goes_out?
				SEQ_DATAOUT: SEQ_DATAIN) + SEQ_DMA_MODE + seq);
		break;
	case statusing:
		out_8(&mr->count_hi, 0);
		out_8(&mr->count_lo, 1);
		out_8(&mr->sequence, SEQ_STATUS + seq);
		break;
	case busfreeing:
	case disconnecting:
		out_8(&mr->sequence, SEQ_ENBRESEL);
		udelay(1);
		out_8(&mr->sequence, SEQ_BUSFREE);
		break;
	default:
		printk(KERN_ERR "mesh: start_phase called with phase=%d\n",
		       ms->phase);
	}

}

static inline void
get_msgin(struct mesh_state *ms)
{
	volatile struct mesh_regs *mr = ms->mesh;
	int i, n;

	n = mr->fifo_count;
	if (n != 0) {
		i = ms->n_msgin;
		ms->n_msgin = i + n;
		for (; n > 0; --n)
			ms->msgin[i++] = in_8(&mr->fifo);
	}
}

static inline int
msgin_length(struct mesh_state *ms)
{
	int b, n;

	n = 1;
	if (ms->n_msgin > 0) {
		b = ms->msgin[0];
		if (b == 1) {
			/* extended message */
			n = ms->n_msgin < 2? 2: ms->msgin[1] + 2;
		} else if (0x20 <= b && b <= 0x2f) {
			/* 2-byte message */
			n = 2;
		}
	}
	return n;
}

static void
cmd_complete(struct mesh_state *ms)
{
	volatile struct mesh_regs *mr = ms->mesh;
	Scsi_Cmnd *cmd = ms->current_req;
	struct mesh_target *tp = &ms->tgts[ms->conn_tgt];
	int seq, n, t;

	seq = SEQ_ACTIVE_NEG + (ms->n_msgout? SEQ_ATN: 0);
	switch (ms->msgphase) {
	case msg_out_xxx:
		/* huh?  we expected a phase mismatch */
		ms->n_msgin = 0;
		ms->msgphase = msg_in;
		/* fall through */

	case msg_in:
		/* should have some message bytes in fifo */
		get_msgin(ms);
		n = msgin_length(ms);
		if (ms->n_msgin < n) {
			out_8(&mr->count_lo, n - ms->n_msgin);
			out_8(&mr->sequence, SEQ_MSGIN + seq);
		} else {
			ms->msgphase = msg_none;
			handle_msgin(ms);
			start_phase(ms);
		}
		break;

	case msg_out:
		/*
		 * To get the right timing on ATN wrt ACK, we have
		 * to get the MESH to drop ACK, wait until REQ gets
		 * asserted, then drop ATN.  To do this we first
		 * issue a SEQ_MSGOUT with ATN and wait for REQ,
		 * then change the command to a SEQ_MSGOUT w/o ATN.
		 * If we don't see REQ in a reasonable time, we
		 * change the command to SEQ_MSGIN with ATN,
		 * wait for the phase mismatch interrupt, then
		 * issue the SEQ_MSGOUT without ATN.
		 */
		out_8(&mr->count_lo, 1);
		out_8(&mr->sequence, SEQ_MSGOUT + SEQ_ACTIVE_NEG + SEQ_ATN);
		t = 30;		/* wait up to 30us */
		while ((mr->bus_status0 & BS0_REQ) == 0 && --t >= 0)
			udelay(1);
		if (mr->bus_status0 & BS0_REQ) {
			out_8(&mr->sequence, SEQ_MSGOUT + SEQ_ACTIVE_NEG);
			udelay(1);
			out_8(&mr->fifo, ms->msgout[ms->n_msgout-1]);
			ms->msgphase = msg_out_last;
		} else {
			out_8(&mr->sequence, SEQ_MSGIN + SEQ_ACTIVE_NEG + SEQ_ATN);
			ms->msgphase = msg_out_xxx;
		}
		break;

	case msg_out_last:
		ms->last_n_msgout = ms->n_msgout;
		ms->n_msgout = 0;
		ms->msgphase = ms->expect_reply? msg_in: msg_none;
		start_phase(ms);
		break;

	case msg_none:
		switch (ms->phase) {
		case selecting:
			ms->msgout[0] = IDENTIFY(ALLOW_RESEL(cmd->target), cmd->lun);
			ms->n_msgout = 1;
			ms->expect_reply = 0;
			if (tp->sdtr_state == do_sdtr) {
				/* add SDTR message */
				add_sdtr_msg(ms);
				ms->expect_reply = 1;
				tp->sdtr_state = sdtr_sent;
			}
			ms->msgphase = msg_out;
			/*
			 * We need to wait for REQ before dropping ATN.
			 * We wait for at most 30us, then fall back to
			 * a scheme where we issue a SEQ_COMMAND with ATN,
			 * which will give us a phase mismatch interrupt
			 * when REQ does come, and then we send the message.
			 */
			t = 30;		/* wait up to 30us */
			while ((mr->bus_status0 & BS0_REQ) == 0) {
				if (--t < 0) {
					ms->msgphase = msg_none;
					break;
				}
				udelay(1);
			}
			break;
		case dataing:
			if (ms->dma_count != 0) {
				start_phase(ms);
				return;
			}
			halt_dma(ms);
			break;
		case statusing:
			cmd->SCp.Status = mr->fifo;
			cmd->result = (DID_OK << 16) + cmd->SCp.Status;
			ms->msgphase = msg_in;
			if (DEBUG_TARGET(cmd))
				printk(KERN_DEBUG "mesh: status is %x\n",
				       cmd->SCp.Status);
			break;
		case busfreeing:
			mesh_done(ms);
			return;
		case disconnecting:
			ms->current_req = 0;
			ms->phase = idle;
			mesh_start(ms);
			return;
		default:
			break;
		}
		++ms->phase;
		start_phase(ms);
		break;
	}
}

static void phase_mismatch(struct mesh_state *ms)
{
	volatile struct mesh_regs *mr = ms->mesh;
	int phase;

	phase = mr->bus_status0 & BS0_PHASE;
	if (ms->msgphase == msg_out_xxx && phase == BP_MSGOUT) {
		/* output the last byte of the message, without ATN */
		out_8(&mr->count_lo, 1);
		out_8(&mr->sequence, SEQ_MSGOUT + SEQ_ACTIVE_NEG);
		udelay(1);
		out_8(&mr->fifo, ms->msgout[ms->n_msgout-1]);
		ms->msgphase = msg_out_last;
		return;
	}

	if (ms->msgphase == msg_in) {
		get_msgin(ms);
		if (ms->n_msgin)
			handle_msgin(ms);
	}

	if (ms->dma_started)
		halt_dma(ms);
	if (mr->fifo_count) {
		out_8(&mr->sequence, SEQ_FLUSHFIFO);
		udelay(1);
	}

	ms->msgphase = msg_none;
	switch (phase) {
	case BP_DATAIN:
		ms->data_goes_out = 0;
		ms->phase = dataing;
		break;
	case BP_DATAOUT:
		ms->data_goes_out = 1;
		ms->phase = dataing;
		break;
	case BP_COMMAND:
		ms->phase = commanding;
		break;
	case BP_STATUS:
		ms->phase = statusing;
		break;
	case BP_MSGIN:
		ms->msgphase = msg_in;
		ms->n_msgin = 0;
		break;
	case BP_MSGOUT:
		ms->msgphase = msg_out;
		if (ms->n_msgout == 0) {
			if (ms->last_n_msgout == 0) {
				printk(KERN_DEBUG "mesh: no msg to repeat\n");
				ms->msgout[0] = NOP;
				ms->last_n_msgout = 1;
			}
			ms->n_msgout = ms->last_n_msgout;
		}
		break;
	default:
		printk(KERN_DEBUG "mesh: unknown scsi phase %x\n", phase);
		ms->current_req->result = DID_ERROR << 16;
		mesh_done(ms);
		return;
	}

	start_phase(ms);
}

static void
reselected(struct mesh_state *ms)
{
	volatile struct mesh_regs *mr = ms->mesh;
	Scsi_Cmnd *cmd = ms->current_req;
	struct mesh_target *tp;
	int b, t;

	switch (ms->phase) {
	case idle:
	case arbitrating:
		break;
	case busfreeing:
		ms->phase = reselecting;
		mesh_done(ms);
		cmd = NULL;
		break;
	case disconnecting:
		cmd = NULL;
		break;
	default:
		printk(KERN_ERR "mesh: reselected in phase %d/%d\n",
		       ms->msgphase, ms->phase);
	}
	if (cmd) {
		/* put the command back on the queue */
		cmd->host_scribble = (void *) ms->request_q;
		if (ms->request_q == NULL)
			ms->request_qtail = cmd;
		ms->request_q = cmd;
		tp = &ms->tgts[cmd->target];
		tp->current_req = NULL;
		ms->current_req = NULL;
	}

	/*
	 * Find out who reselected us.
	 */
	if (mr->fifo_count == 0) {
		printk(KERN_ERR "mesh: reselection but nothing in fifo?\n");
		return;
	}
	/* get the last byte in the fifo */
	do {
		b = in_8(&mr->fifo);
	} while (in_8(&mr->fifo_count));
	for (t = 0; t < 8; ++t)
		if ((b & (1 << t)) != 0 && t != ms->host->this_id)
			break;
	if (b != (1 << t) + (1 << ms->host->this_id)) {
		printk(KERN_ERR "mesh: bad reselection data %x\n", b);
		return;
	}

	/*
	 * Set up to continue with that target's transfer.
	 */
	tp = &ms->tgts[t];
	if (ALLOW_DEBUG(t)) {
		printk(KERN_DEBUG "mesh: reselected by target %d\n", t);
		printk(KERN_DEBUG "mesh: saved_ptr=%x phase=%d cmd=%p\n",
		       tp->saved_ptr, tp->phase, tp->current_req);
	}
	if (tp->current_req == NULL) {
		printk(KERN_ERR "mesh: reselected by tgt %d but no cmd!\n", t);
		return;
	}
	ms->current_req = tp->current_req;
	ms->phase = tp->phase;
	ms->msgphase = msg_in;
	ms->data_goes_out = tp->data_goes_out;
	ms->data_ptr = tp->saved_ptr;
	ms->conn_tgt = t;
	ms->dma_started = 0;
	ms->n_msgout = 0;
	ms->last_n_msgout = 0;
	out_8(&mr->sync_params, tp->sync_params);
	start_phase(ms);
}

static void
handle_reset(struct mesh_state *ms)
{
	int tgt;
	struct mesh_target *tp;
	Scsi_Cmnd *cmd;
	volatile struct mesh_regs *mr = ms->mesh;

	for (tgt = 0; tgt < 8; ++tgt) {
		tp = &ms->tgts[tgt];
		if ((cmd = tp->current_req) != NULL) {
			cmd->result = DID_RESET << 16;
			tp->current_req = NULL;
			mesh_completed(ms, cmd);
		}
		ms->tgts[tgt].sdtr_state = do_sdtr;
		ms->tgts[tgt].sync_params = ASYNC_PARAMS;
	}
	ms->current_req = NULL;
	while ((cmd = ms->request_q) != NULL) {
		ms->request_q = (Scsi_Cmnd *) cmd->host_scribble;
		cmd->result = DID_RESET << 16;
		mesh_completed(ms, cmd);
	}
	ms->phase = idle;
	out_8(&mr->sync_params, ASYNC_PARAMS);
}

static void
do_mesh_interrupt(int irq, void *dev_id, struct pt_regs *ptregs)
{
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock, flags);
	mesh_interrupt(irq, dev_id, ptregs);
	spin_unlock_irqrestore(&io_request_lock, flags);
}

static void
mesh_interrupt(int irq, void *dev_id, struct pt_regs *ptregs)
{
	struct mesh_state *ms = (struct mesh_state *) dev_id;
	volatile struct mesh_regs *mr = ms->mesh;
	Scsi_Cmnd *cmd = ms->current_req;
	int stat, exc, err, intr;

#if 0
	if (DEBUG_TARGET(cmd))
		printk(KERN_DEBUG "mesh_intr, bs0=%x int=%x exc=%x err=%x phase=%d msgphase=%d\n",
		       mr->bus_status0, mr->interrupt, mr->exception, mr->error, ms->phase, ms->msgphase);
#endif
	while ((intr = in_8(&mr->interrupt)) != 0) {
		if (intr & INT_ERROR) {
			stat = DID_BAD_INTR << 16;
			err = in_8(&mr->error);
			exc = in_8(&mr->exception);
			out_8(&mr->interrupt, INT_ERROR | INT_EXCEPTION | INT_CMDDONE);
			if (err & ERR_SCSIRESET) {
				/* SCSI bus was reset */
				printk(KERN_INFO "mesh: SCSI bus reset detected: "
				       "waiting for end...");
				while ((mr->bus_status1 & BS1_RST) != 0)
					udelay(1);
				printk("done\n");
				handle_reset(ms);
				/* request_q is empty, no point in mesh_start() */
				continue;
			} else if (err & ERR_UNEXPDISC) {
				/* Unexpected disconnect */
				printk(KERN_WARNING "mesh: target %d aborted\n",
				       ms->conn_tgt);
				stat = DID_ABORT << 16;
			} else if (err & ERR_PARITY) {
				printk(KERN_ERR "mesh: parity error\n");
				stat = DID_PARITY << 16;
			} else if ((err & ERR_SEQERR) && (exc & EXC_RESELECTED)
				   && ms->phase == arbitrating) {
				/* This can happen if we issue a command to
				   get the bus just after the target
				   reselects us. */
				static int mesh_resel_seqerr;
				mesh_resel_seqerr++;
				reselected(ms);
				continue;
			} else {
				printk(KERN_ERR "mesh: error %x (exc = %x)\n",
				       err, exc);
				mesh_dump_regs(ms);
			}
			if (cmd != 0) {
				cmd->result = stat;
				mesh_done(ms);
			}

		} else if (intr & INT_EXCEPTION) {
			exc = in_8(&mr->exception);
			out_8(&mr->interrupt, INT_EXCEPTION | INT_CMDDONE);
			if (exc & EXC_RESELECTED) {
				static int mesh_resel_exc;
				mesh_resel_exc++;
				reselected(ms);
			} else if (cmd && exc == EXC_ARBLOST
				   && ms->phase == arbitrating) {
				printk(KERN_DEBUG "mesh: lost arbitration\n");
				cmd->result = DID_BUS_BUSY << 16;
				mesh_done(ms);
			} else if (cmd && exc == EXC_SELTO && ms->phase == selecting) {
				/* selection timed out */
				cmd->result = DID_BAD_TARGET << 16;
				mesh_done(ms);
			} else if (cmd && exc == EXC_PHASEMM
				   && (mr->bus_status0 & BS0_REQ) != 0) {
				/* target wants to do something different:
			   find out what it wants and do it. */
				phase_mismatch(ms);
			} else {
				printk(KERN_ERR "mesh: can't cope with exception %x\n",
				       exc);
				cmd->result = DID_ERROR << 16;
				mesh_done(ms);
			}

		} else if (intr & INT_CMDDONE) {
			out_8(&mr->interrupt, INT_CMDDONE);
			cmd_complete(ms);
		}
	}
}

static void
handle_msgin(struct mesh_state *ms)
{
	int i;
	Scsi_Cmnd *cmd = ms->current_req;
	struct mesh_target *tp = &ms->tgts[ms->conn_tgt];

	if (ms->n_msgin == 0)
		return;
	if (DEBUG_TARGET(cmd)) {
		printk(KERN_DEBUG "got %d message bytes:", ms->n_msgin);
		for (i = 0; i < ms->n_msgin; ++i)
			printk(" %x", ms->msgin[i]);
		printk("\n");
	}

	ms->expect_reply = 0;
	ms->n_msgout = 0;
	if (ms->n_msgin < msgin_length(ms))
		goto reject;
	if (cmd)
		cmd->SCp.Message = ms->msgin[0];
	switch (ms->msgin[0]) {
	case COMMAND_COMPLETE:
		break;
	case EXTENDED_MESSAGE:
		switch (ms->msgin[2]) {
		case EXTENDED_MODIFY_DATA_POINTER:
			ms->data_ptr += (ms->msgin[3] << 24) + ms->msgin[6]
				+ (ms->msgin[4] << 16) + (ms->msgin[5] << 8);
			break;
		case EXTENDED_SDTR:
			if (tp->sdtr_state != sdtr_sent) {
				/* reply with an SDTR */
				add_sdtr_msg(ms);
				/* limit period to at least his value,
				   offset to no more than his */
				if (ms->msgout[3] < ms->msgin[3])
					ms->msgout[3] = ms->msgin[3];
				if (ms->msgout[4] > ms->msgin[4])
					ms->msgout[4] = ms->msgin[4];
				set_sdtr(ms, ms->msgout[3], ms->msgout[4]);
				ms->msgphase = msg_out;
			} else {
				set_sdtr(ms, ms->msgin[3], ms->msgin[4]);
			}
			break;
		default:
			goto reject;
		}
		break;
	case SAVE_POINTERS:
		tp->saved_ptr = ms->data_ptr;
		break;
	case RESTORE_POINTERS:
		ms->data_ptr = tp->saved_ptr;
		break;
	case DISCONNECT:
		tp->phase = ms->phase;
		tp->data_goes_out = ms->data_goes_out;
		ms->phase = disconnecting;
		break;
	case ABORT:
		break;
	case MESSAGE_REJECT:
		if (tp->sdtr_state == sdtr_sent)
			set_sdtr(ms, 0, 0);
		break;
	case NOP:
		break;
	default:
		if (cmd && IDENTIFY_BASE <= ms->msgin[0]
		    && ms->msgin[0] <= IDENTIFY_BASE + 7) {
			i = ms->msgin[0] - IDENTIFY_BASE;
			if (i != cmd->lun)
				printk(KERN_WARNING "mesh: lun mismatch "
				       "(%d != %d) on reselection from "
				       "target %d\n", i, cmd->lun,
				       ms->conn_tgt);
			break;
		}
		goto reject;
	}
	return;

 reject:
	printk(KERN_WARNING "mesh: rejecting message %x from target %d\n",
	       ms->msgin[0], ms->conn_tgt);
	ms->msgout[0] = MESSAGE_REJECT;
	ms->n_msgout = 1;
	ms->msgphase = msg_out;
}

static void
mesh_done(struct mesh_state *ms)
{
	Scsi_Cmnd *cmd;
	struct mesh_target *tp = &ms->tgts[ms->conn_tgt];

	cmd = ms->current_req;
	if (DEBUG_TARGET(cmd)) {
		printk(KERN_DEBUG "mesh_done: result = %x, data_ptr=%d, buflen=%d\n",
		       cmd->result, ms->data_ptr, cmd->request_bufflen);
		if ((cmd->cmnd[0] == 0 || cmd->cmnd[0] == 0x12 || cmd->cmnd[0] == 3)
		    && cmd->request_buffer != 0) {
			unsigned char *b = cmd->request_buffer;
			printk(KERN_DEBUG "buffer = %x %x %x %x %x %x %x %x\n",
			       b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		}
	}
	tp->current_req = 0;
	cmd->SCp.this_residual -= ms->data_ptr;
	ms->current_req = NULL;
	mesh_completed(ms, cmd);
	if (ms->phase != reselecting) {
		ms->phase = idle;
		mesh_start(ms);
	}
}

static void
mesh_completed(struct mesh_state *ms, Scsi_Cmnd *cmd)
{
	if (ms->completed_q == NULL)
		ms->completed_q = cmd;
	else
		ms->completed_qtail->host_scribble = (void *) cmd;
	ms->completed_qtail = cmd;
	cmd->host_scribble = NULL;
	queue_task(&ms->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*
 * Set up DMA commands for transferring data.
 */
static void
set_dma_cmds(struct mesh_state *ms, Scsi_Cmnd *cmd)
{
	int i, dma_cmd, total, off, dtot;
	struct scatterlist *scl;
	struct dbdma_cmd *dcmds;

	dma_cmd = ms->data_goes_out? OUTPUT_MORE: INPUT_MORE;
	dcmds = ms->dma_cmds;
	dtot = 0;
	cmd->SCp.this_residual = cmd->request_bufflen;
	if (cmd->use_sg > 0) {
		total = 0;
		scl = (struct scatterlist *) cmd->buffer;
		off = ms->data_ptr;
		for (i = 0; i < cmd->use_sg; ++i, ++scl) {
			total += scl->length;
			if (off >= scl->length) {
				off -= scl->length;
				continue;
			}
			if (scl->length > 0xffff)
				panic("mesh: scatterlist element >= 64k");
			st_le16(&dcmds->req_count, scl->length - off);
			st_le16(&dcmds->command, dma_cmd);
			st_le32(&dcmds->phy_addr,
				virt_to_phys(scl->address) + off);
			dcmds->xfer_status = 0;
			++dcmds;
			dtot += scl->length - off;
			off = 0;
		}
	} else if (ms->data_ptr < cmd->request_bufflen) {
		dtot = cmd->request_bufflen - ms->data_ptr;
		if (dtot > 0xffff)
			panic("mesh: transfer size >= 64k");
		st_le16(&dcmds->req_count, dtot);
		st_le32(&dcmds->phy_addr,
			virt_to_phys(cmd->request_buffer) + ms->data_ptr);
		dcmds->xfer_status = 0;
		++dcmds;
	}
	if (dtot == 0) {
		/* Either the target has overrun our buffer,
		   or the caller didn't provide a buffer. */
		static char mesh_extra_buf[64];

		if (cmd->request_bufflen != 0)
			printk(KERN_DEBUG "mesh: target %d overrun, "
			       "data_ptr=%x total=%x goes_out=%d\n",
			       ms->conn_tgt, ms->data_ptr,
			       cmd->request_bufflen, ms->data_goes_out);
		dtot = sizeof(mesh_extra_buf);
		st_le16(&dcmds->req_count, dtot);
		st_le32(&dcmds->phy_addr, virt_to_phys(mesh_extra_buf));
		dcmds->xfer_status = 0;
		++dcmds;
	}
	dma_cmd += OUTPUT_LAST - OUTPUT_MORE;
	st_le16(&dcmds[-1].command, dma_cmd);
	memset(dcmds, 0, sizeof(*dcmds));
	st_le16(&dcmds->command, DBDMA_STOP);
	ms->dma_count = dtot;
}

static void
halt_dma(struct mesh_state *ms)
{
	volatile struct dbdma_regs *md = ms->dma;
	volatile struct mesh_regs *mr = ms->mesh;
	int t, nb;

	if (!ms->data_goes_out) {
		/* wait a little while until the fifo drains */
		t = 50;
		while (t > 0 && mr->fifo_count != 0
		       && (in_le32(&md->status) & ACTIVE) != 0) {
			--t;
			udelay(1);
		}
	}
	out_le32(&md->control, RUN << 16);	/* turn off RUN bit */
	nb = (mr->count_hi << 8) + mr->count_lo;
	if (ms->data_goes_out)
		nb += mr->fifo_count;
	/* nb is the number of bytes not yet transferred
	   to/from the target. */
	ms->data_ptr -= nb;
	if (ms->data_ptr < 0) {
		printk(KERN_ERR "mesh: halt_dma: data_ptr=%d (nb=%d, ms=%p)\n",
		       ms->data_ptr, nb, ms);
		ms->data_ptr = 0;
	}
	ms->dma_started = 0;
}

/*
 * Work out whether we expect data to go out from the host adaptor or into it.
 * (If this information is available from somewhere else in the scsi
 * code, somebody please let me know :-)
 */
static int
data_goes_out(Scsi_Cmnd *cmd)
{
	switch (cmd->cmnd[0]) {
	case MODE_SELECT:
	case MODE_SELECT_10:
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:		/* any others? */
		return 1;
	default:
		return 0;
	}
}

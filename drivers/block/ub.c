/*
 * The low performance USB storage driver (ub).
 *
 * Copyright (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 * Copyright (C) 2004 Pete Zaitcev (zaitcev@yahoo.com)
 *
 * This work is a part of Linux kernel, is derived from it,
 * and is not licensed separately. See file COPYING for details.
 *
 * TODO (sorted by decreasing priority)
 *  -- Do resets with usb_device_reset (needs a thread context, use khubd)
 *  -- set readonly flag for CDs, set removable flag for CF readers
 *  -- do inquiry and verify we got a disk and not a tape (for LUN mismatch)
 *  -- support pphaneuf's SDDR-75 with two LUNs (also broken capacity...)
 *  -- special case some senses, e.g. 3a/0 -> no media present, reduce retries
 *  -- verify the 13 conditions and do bulk resets
 *  -- normal pool of commands instead of cmdv[]?
 *  -- kill last_pipe and simply do two-state clearing on both pipes
 *  -- verify protocol (bulk) from USB descriptors (maybe...)
 *  -- highmem and sg
 *  -- move top_sense and work_bcs into separate allocations (if they survive)
 *     for cache purists and esoteric architectures.
 *  -- prune comments, they are too volumnous
 *  -- Exterminate P3 printks
 *  -- Resove XXX's
 *  -- Redo "benh's retries", perhaps have spin-up code to handle them. V:D=?
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/blkdev.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/timer.h>
#include <scsi/scsi.h>

#define DRV_NAME "ub"
#define DEVFS_NAME DRV_NAME

#define UB_MAJOR 180

/*
 * Definitions which have to be scattered once we understand the layout better.
 */

/* Transport (despite PR in the name) */
#define US_PR_BULK	0x50		/* bulk only */

/* Protocol */
#define US_SC_SCSI	0x06		/* Transparent */

/*
 */
#define UB_MINORS_PER_MAJOR	8

#define UB_MAX_CDB_SIZE      16		/* Corresponds to Bulk */

#define UB_SENSE_SIZE  18

/*
 */

/* command block wrapper */
struct bulk_cb_wrap {
	__le32	Signature;		/* contains 'USBC' */
	u32	Tag;			/* unique per command id */
	__le32	DataTransferLength;	/* size of data */
	u8	Flags;			/* direction in bit 0 */
	u8	Lun;			/* LUN normally 0 */
	u8	Length;			/* of of the CDB */
	u8	CDB[UB_MAX_CDB_SIZE];	/* max command */
};

#define US_BULK_CB_WRAP_LEN	31
#define US_BULK_CB_SIGN		0x43425355	/*spells out USBC */
#define US_BULK_FLAG_IN		1
#define US_BULK_FLAG_OUT	0

/* command status wrapper */
struct bulk_cs_wrap {
	__le32	Signature;		/* should = 'USBS' */
	u32	Tag;			/* same as original command */
	__le32	Residue;		/* amount not transferred */
	u8	Status;			/* see below */
};

#define US_BULK_CS_WRAP_LEN	13
#define US_BULK_CS_SIGN		0x53425355	/* spells out 'USBS' */
/* This is for Olympus Camedia digital cameras */
#define US_BULK_CS_OLYMPUS_SIGN	0x55425355	/* spells out 'USBU' */
#define US_BULK_STAT_OK		0
#define US_BULK_STAT_FAIL	1
#define US_BULK_STAT_PHASE	2

/* bulk-only class specific requests */
#define US_BULK_RESET_REQUEST	0xff
#define US_BULK_GET_MAX_LUN	0xfe

/*
 */
struct ub_dev;

#define UB_MAX_REQ_SG	1
#define UB_MAX_SECTORS 64

/*
 * A second is more than enough for a 32K transfer (UB_MAX_SECTORS)
 * even if a webcam hogs the bus, but some devices need time to spin up.
 */
#define UB_URB_TIMEOUT	(HZ*2)
#define UB_DATA_TIMEOUT	(HZ*5)	/* ZIP does spin-ups in the data phase */
#define UB_STAT_TIMEOUT	(HZ*5)	/* Same spinups and eject for a dataless cmd. */
#define UB_CTRL_TIMEOUT	(HZ/2)	/* 500ms ought to be enough to clear a stall */

/*
 * An instance of a SCSI command in transit.
 */
#define UB_DIR_NONE	0
#define UB_DIR_READ	1
#define UB_DIR_ILLEGAL2	2
#define UB_DIR_WRITE	3

#define UB_DIR_CHAR(c)  (((c)==UB_DIR_WRITE)? 'w': \
			 (((c)==UB_DIR_READ)? 'r': 'n'))

enum ub_scsi_cmd_state {
	UB_CMDST_INIT,			/* Initial state */
	UB_CMDST_CMD,			/* Command submitted */
	UB_CMDST_DATA,			/* Data phase */
	UB_CMDST_CLR2STS,		/* Clearing before requesting status */
	UB_CMDST_STAT,			/* Status phase */
	UB_CMDST_CLEAR,			/* Clearing a stall (halt, actually) */
	UB_CMDST_SENSE,			/* Sending Request Sense */
	UB_CMDST_DONE			/* Final state */
};

static char *ub_scsi_cmd_stname[] = {
	".  ",
	"Cmd",
	"dat",
	"c2s",
	"sts",
	"clr",
	"Sen",
	"fin"
};

struct ub_scsi_cmd {
	unsigned char cdb[UB_MAX_CDB_SIZE];
	unsigned char cdb_len;

	unsigned char dir;		/* 0 - none, 1 - read, 3 - write. */
	unsigned char trace_index;
	enum ub_scsi_cmd_state state;
	unsigned int tag;
	struct ub_scsi_cmd *next;

	int error;			/* Return code - valid upon done */
	unsigned int act_len;		/* Return size */
	unsigned char key, asc, ascq;	/* May be valid if error==-EIO */

	int stat_count;			/* Retries getting status. */

	/*
	 * We do not support transfers from highmem pages
	 * because the underlying USB framework does not do what we need.
	 */
	char *data;			/* Requested buffer */
	unsigned int len;		/* Requested length */
	// struct scatterlist sgv[UB_MAX_REQ_SG];

	void (*done)(struct ub_dev *, struct ub_scsi_cmd *);
	void *back;
};

/*
 */
struct ub_capacity {
	unsigned long nsec;		/* Linux size - 512 byte sectors */
	unsigned int bsize;		/* Linux hardsect_size */
	unsigned int bshift;		/* Shift between 512 and hard sects */
};

/*
 * The SCSI command tracing structure.
 */

#define SCMD_ST_HIST_SZ   8
#define SCMD_TRACE_SZ    63		/* Less than 4KB of 61-byte lines */

struct ub_scsi_cmd_trace {
	int hcur;
	unsigned int tag;
	unsigned int req_size, act_size;
	unsigned char op;
	unsigned char dir;
	unsigned char key, asc, ascq;
	char st_hst[SCMD_ST_HIST_SZ];	
};

struct ub_scsi_trace {
	int cur;
	struct ub_scsi_cmd_trace vec[SCMD_TRACE_SZ];
};

/*
 * This is a direct take-off from linux/include/completion.h
 * The difference is that I do not wait on this thing, just poll.
 * When I want to wait (ub_probe), I just use the stock completion.
 *
 * Note that INIT_COMPLETION takes no lock. It is correct. But why
 * in the bloody hell that thing takes struct instead of pointer to struct
 * is quite beyond me. I just copied it from the stock completion.
 */
struct ub_completion {
	unsigned int done;
	spinlock_t lock;
};

static inline void ub_init_completion(struct ub_completion *x)
{
	x->done = 0;
	spin_lock_init(&x->lock);
}

#define UB_INIT_COMPLETION(x)	((x).done = 0)

static void ub_complete(struct ub_completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->lock, flags);
	x->done++;
	spin_unlock_irqrestore(&x->lock, flags);
}

static int ub_is_completed(struct ub_completion *x)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&x->lock, flags);
	ret = x->done;
	spin_unlock_irqrestore(&x->lock, flags);
	return ret;
}

/*
 */
struct ub_scsi_cmd_queue {
	int qlen, qmax;
	struct ub_scsi_cmd *head, *tail;
};

/*
 * The UB device instance.
 */
struct ub_dev {
	spinlock_t lock;
	int id;				/* Number among ub's */
	atomic_t poison;		/* The USB device is disconnected */
	int openc;			/* protected by ub_lock! */
					/* kref is too implicit for our taste */
	unsigned int tagcnt;
	int changed;			/* Media was changed */
	int removable;
	int readonly;
	int first_open;			/* Kludge. See ub_bd_open. */
	char name[8];
	struct usb_device *dev;
	struct usb_interface *intf;

	struct ub_capacity capacity; 
	struct gendisk *disk;

	unsigned int send_bulk_pipe;	/* cached pipe values */
	unsigned int recv_bulk_pipe;
	unsigned int send_ctrl_pipe;
	unsigned int recv_ctrl_pipe;

	struct tasklet_struct tasklet;

	/* XXX Use Ingo's mempool (once we have more than one) */
	int cmda[1];
	struct ub_scsi_cmd cmdv[1];

	struct ub_scsi_cmd_queue cmd_queue;
	struct ub_scsi_cmd top_rqs_cmd;	/* REQUEST SENSE */
	unsigned char top_sense[UB_SENSE_SIZE];

	struct ub_completion work_done;
	struct urb work_urb;
	struct timer_list work_timer;
	int last_pipe;			/* What might need clearing */
	struct bulk_cb_wrap work_bcb;
	struct bulk_cs_wrap work_bcs;
	struct usb_ctrlrequest work_cr;

	struct ub_scsi_trace tr;
};

/*
 */
static int ub_bd_rq_fn_1(struct ub_dev *sc, struct request *rq);
static int ub_cmd_build_block(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    struct request *rq);
static int ub_cmd_build_packet(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    struct request *rq);
static void ub_rw_cmd_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_end_rq(struct request *rq, int uptodate);
static int ub_submit_scsi(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_urb_complete(struct urb *urb, struct pt_regs *pt);
static void ub_scsi_action(unsigned long _dev);
static void ub_scsi_dispatch(struct ub_dev *sc);
static void ub_scsi_urb_compl(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_state_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd, int rc);
static void __ub_state_stat(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_state_stat(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_state_sense(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static int ub_submit_clear_stall(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    int stalled_pipe);
static void ub_top_sense_done(struct ub_dev *sc, struct ub_scsi_cmd *scmd);
static int ub_sync_tur(struct ub_dev *sc);
static int ub_sync_read_cap(struct ub_dev *sc, struct ub_capacity *ret);

/*
 */
static struct usb_device_id ub_usb_ids[] = {
	// { USB_DEVICE_VER(0x0781, 0x0002, 0x0009, 0x0009) },	/* SDDR-31 */
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_SCSI, US_PR_BULK) },
	{ }
};

MODULE_DEVICE_TABLE(usb, ub_usb_ids);

/*
 * Find me a way to identify "next free minor" for add_disk(),
 * and the array disappears the next day. However, the number of
 * hosts has something to do with the naming and /proc/partitions.
 * This has to be thought out in detail before changing.
 * If UB_MAX_HOST was 1000, we'd use a bitmap. Or a better data structure.
 */
#define UB_MAX_HOSTS  26
static char ub_hostv[UB_MAX_HOSTS];
static DEFINE_SPINLOCK(ub_lock);	/* Locks globals and ->openc */

/*
 * The SCSI command tracing procedures.
 */

static void ub_cmdtr_new(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	int n;
	struct ub_scsi_cmd_trace *t;

	if ((n = sc->tr.cur + 1) == SCMD_TRACE_SZ) n = 0;
	t = &sc->tr.vec[n];

	memset(t, 0, sizeof(struct ub_scsi_cmd_trace));
	t->tag = cmd->tag;
	t->op = cmd->cdb[0];
	t->dir = cmd->dir;
	t->req_size = cmd->len;
	t->st_hst[0] = cmd->state;

	sc->tr.cur = n;
	cmd->trace_index = n;
}

static void ub_cmdtr_state(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	int n;
	struct ub_scsi_cmd_trace *t;

	t = &sc->tr.vec[cmd->trace_index];
	if (t->tag == cmd->tag) {
		if ((n = t->hcur + 1) == SCMD_ST_HIST_SZ) n = 0;
		t->st_hst[n] = cmd->state;
		t->hcur = n;
	}
}

static void ub_cmdtr_act_len(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_scsi_cmd_trace *t;

	t = &sc->tr.vec[cmd->trace_index];
	if (t->tag == cmd->tag)
		t->act_size = cmd->act_len;
}

static void ub_cmdtr_sense(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    unsigned char *sense)
{
	struct ub_scsi_cmd_trace *t;

	t = &sc->tr.vec[cmd->trace_index];
	if (t->tag == cmd->tag) {
		t->key = sense[2] & 0x0F;
		t->asc = sense[12];
		t->ascq = sense[13];
	}
}

static ssize_t ub_diag_show(struct device *dev, char *page)
{
	struct usb_interface *intf;
	struct ub_dev *sc;
	int cnt;
	unsigned long flags;
	int nc, nh;
	int i, j;
	struct ub_scsi_cmd_trace *t;

	intf = to_usb_interface(dev);
	sc = usb_get_intfdata(intf);
	if (sc == NULL)
		return 0;

	cnt = 0;
	spin_lock_irqsave(&sc->lock, flags);

	cnt += sprintf(page + cnt,
	    "qlen %d qmax %d changed %d removable %d readonly %d\n",
	    sc->cmd_queue.qlen, sc->cmd_queue.qmax,
	    sc->changed, sc->removable, sc->readonly);

	if ((nc = sc->tr.cur + 1) == SCMD_TRACE_SZ) nc = 0;
	for (j = 0; j < SCMD_TRACE_SZ; j++) {
		t = &sc->tr.vec[nc];

		cnt += sprintf(page + cnt, "%08x %02x", t->tag, t->op);
		if (t->op == REQUEST_SENSE) {
			cnt += sprintf(page + cnt, " [sense %x %02x %02x]",
					t->key, t->asc, t->ascq);
		} else {
			cnt += sprintf(page + cnt, " %c", UB_DIR_CHAR(t->dir));
			cnt += sprintf(page + cnt, " [%5d %5d]",
					t->req_size, t->act_size);
		}
		if ((nh = t->hcur + 1) == SCMD_ST_HIST_SZ) nh = 0;
		for (i = 0; i < SCMD_ST_HIST_SZ; i++) {
			cnt += sprintf(page + cnt, " %s",
					ub_scsi_cmd_stname[(int)t->st_hst[nh]]);
			if (++nh == SCMD_ST_HIST_SZ) nh = 0;
		}
		cnt += sprintf(page + cnt, "\n");

		if (++nc == SCMD_TRACE_SZ) nc = 0;
	}

	spin_unlock_irqrestore(&sc->lock, flags);
	return cnt;
}

static DEVICE_ATTR(diag, S_IRUGO, ub_diag_show, NULL); /* N.B. World readable */

/*
 * The id allocator.
 *
 * This also stores the host for indexing by minor, which is somewhat dirty.
 */
static int ub_id_get(void)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ub_lock, flags);
	for (i = 0; i < UB_MAX_HOSTS; i++) {
		if (ub_hostv[i] == 0) {
			ub_hostv[i] = 1;
			spin_unlock_irqrestore(&ub_lock, flags);
			return i;
		}
	}
	spin_unlock_irqrestore(&ub_lock, flags);
	return -1;
}

static void ub_id_put(int id)
{

	if (id < 0 || id >= UB_MAX_HOSTS) {
		printk(KERN_ERR DRV_NAME ": bad host ID %d\n", id);
		return;
	}
	if (ub_hostv[id] == 0) {
		printk(KERN_ERR DRV_NAME ": freeing free host ID %d\n", id);
		return;
	}
	ub_hostv[id] = 0;
}

/*
 * Final cleanup and deallocation.
 * This must be called with ub_lock taken.
 */
static void ub_cleanup(struct ub_dev *sc)
{

	/*
	 * If we zero disk->private_data BEFORE put_disk, we have to check
	 * for NULL all over the place in open, release, check_media and
	 * revalidate, because the block level semaphore is well inside the
	 * put_disk. But we cannot zero after the call, because *disk is gone.
	 * The sd.c is blatantly racy in this area.
	 */
	/* disk->private_data = NULL; */
	put_disk(sc->disk);
	sc->disk = NULL;

	ub_id_put(sc->id);
	kfree(sc);
}

/*
 * The "command allocator".
 */
static struct ub_scsi_cmd *ub_get_cmd(struct ub_dev *sc)
{
	struct ub_scsi_cmd *ret;

	if (sc->cmda[0])
		return NULL;
	ret = &sc->cmdv[0];
	sc->cmda[0] = 1;
	return ret;
}

static void ub_put_cmd(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	if (cmd != &sc->cmdv[0]) {
		printk(KERN_WARNING "%s: releasing a foreign cmd %p\n",
		    sc->name, cmd);
		return;
	}
	if (!sc->cmda[0]) {
		printk(KERN_WARNING "%s: releasing a free cmd\n", sc->name);
		return;
	}
	sc->cmda[0] = 0;
}

/*
 * The command queue.
 */
static void ub_cmdq_add(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_scsi_cmd_queue *t = &sc->cmd_queue;

	if (t->qlen++ == 0) {
		t->head = cmd;
		t->tail = cmd;
	} else {
		t->tail->next = cmd;
		t->tail = cmd;
	}

	if (t->qlen > t->qmax)
		t->qmax = t->qlen;
}

static void ub_cmdq_insert(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_scsi_cmd_queue *t = &sc->cmd_queue;

	if (t->qlen++ == 0) {
		t->head = cmd;
		t->tail = cmd;
	} else {
		cmd->next = t->head;
		t->head = cmd;
	}

	if (t->qlen > t->qmax)
		t->qmax = t->qlen;
}

static struct ub_scsi_cmd *ub_cmdq_pop(struct ub_dev *sc)
{
	struct ub_scsi_cmd_queue *t = &sc->cmd_queue;
	struct ub_scsi_cmd *cmd;

	if (t->qlen == 0)
		return NULL;
	if (--t->qlen == 0)
		t->tail = NULL;
	cmd = t->head;
	t->head = cmd->next;
	cmd->next = NULL;
	return cmd;
}

#define ub_cmdq_peek(sc)  ((sc)->cmd_queue.head)

/*
 * The request function is our main entry point
 */

static void ub_bd_rq_fn(request_queue_t *q)
{
	struct ub_dev *sc = q->queuedata;
	struct request *rq;

	while ((rq = elv_next_request(q)) != NULL) {
		if (ub_bd_rq_fn_1(sc, rq) != 0) {
			blk_stop_queue(q);
			break;
		}
	}
}

static int ub_bd_rq_fn_1(struct ub_dev *sc, struct request *rq)
{
	struct ub_scsi_cmd *cmd;
	int rc;

	if (atomic_read(&sc->poison) || sc->changed) {
		blkdev_dequeue_request(rq);
		ub_end_rq(rq, 0);
		return 0;
	}

	if ((cmd = ub_get_cmd(sc)) == NULL)
		return -1;
	memset(cmd, 0, sizeof(struct ub_scsi_cmd));

	blkdev_dequeue_request(rq);

	if (blk_pc_request(rq)) {
		rc = ub_cmd_build_packet(sc, cmd, rq);
	} else {
		rc = ub_cmd_build_block(sc, cmd, rq);
	}
	if (rc != 0) {
		ub_put_cmd(sc, cmd);
		ub_end_rq(rq, 0);
		blk_start_queue(sc->disk->queue);
		return 0;
	}

	cmd->state = UB_CMDST_INIT;
	cmd->done = ub_rw_cmd_done;
	cmd->back = rq;

	cmd->tag = sc->tagcnt++;
	if ((rc = ub_submit_scsi(sc, cmd)) != 0) {
		ub_put_cmd(sc, cmd);
		ub_end_rq(rq, 0);
		blk_start_queue(sc->disk->queue);
		return 0;
	}

	return 0;
}

static int ub_cmd_build_block(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    struct request *rq)
{
	int ub_dir;
#if 0 /* We use rq->buffer for now */
	struct scatterlist *sg;
	int n_elem;
#endif
	unsigned int block, nblks;

	if (rq_data_dir(rq) == WRITE)
		ub_dir = UB_DIR_WRITE;
	else
		ub_dir = UB_DIR_READ;

	/*
	 * get scatterlist from block layer
	 */
#if 0 /* We use rq->buffer for now */
	sg = &cmd->sgv[0];
	n_elem = blk_rq_map_sg(q, rq, sg);
	if (n_elem <= 0) {
		ub_put_cmd(sc, cmd);
		ub_end_rq(rq, 0);
		blk_start_queue(q);
		return 0;		/* request with no s/g entries? */
	}

	if (n_elem != 1) {		/* Paranoia */
		printk(KERN_WARNING "%s: request with %d segments\n",
		    sc->name, n_elem);
		ub_put_cmd(sc, cmd);
		ub_end_rq(rq, 0);
		blk_start_queue(q);
		return 0;
	}
#endif

	/*
	 * XXX Unfortunately, this check does not work. It is quite possible
	 * to get bogus non-null rq->buffer if you allow sg by mistake.
	 */
	if (rq->buffer == NULL) {
		/*
		 * This must not happen if we set the queue right.
		 * The block level must create bounce buffers for us.
		 */
		static int do_print = 1;
		if (do_print) {
			printk(KERN_WARNING "%s: unmapped block request"
			    " flags 0x%lx sectors %lu\n",
			    sc->name, rq->flags, rq->nr_sectors);
			do_print = 0;
		}
		return -1;
	}

	/*
	 * build the command
	 *
	 * The call to blk_queue_hardsect_size() guarantees that request
	 * is aligned, but it is given in terms of 512 byte units, always.
	 */
	block = rq->sector >> sc->capacity.bshift;
	nblks = rq->nr_sectors >> sc->capacity.bshift;

	cmd->cdb[0] = (ub_dir == UB_DIR_READ)? READ_10: WRITE_10;
	/* 10-byte uses 4 bytes of LBA: 2147483648KB, 2097152MB, 2048GB */
	cmd->cdb[2] = block >> 24;
	cmd->cdb[3] = block >> 16;
	cmd->cdb[4] = block >> 8;
	cmd->cdb[5] = block;
	cmd->cdb[7] = nblks >> 8;
	cmd->cdb[8] = nblks;
	cmd->cdb_len = 10;

	cmd->dir = ub_dir;
	cmd->data = rq->buffer;
	cmd->len = rq->nr_sectors * 512;

	return 0;
}

static int ub_cmd_build_packet(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    struct request *rq)
{

	if (rq->data_len != 0 && rq->data == NULL) {
		static int do_print = 1;
		if (do_print) {
			printk(KERN_WARNING "%s: unmapped packet request"
			    " flags 0x%lx length %d\n",
			    sc->name, rq->flags, rq->data_len);
			do_print = 0;
		}
		return -1;
	}

	memcpy(&cmd->cdb, rq->cmd, rq->cmd_len);
	cmd->cdb_len = rq->cmd_len;

	if (rq->data_len == 0) {
		cmd->dir = UB_DIR_NONE;
	} else {
		if (rq_data_dir(rq) == WRITE)
			cmd->dir = UB_DIR_WRITE;
		else
			cmd->dir = UB_DIR_READ;
	}
	cmd->data = rq->data;
	cmd->len = rq->data_len;

	return 0;
}

static void ub_rw_cmd_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct request *rq = cmd->back;
	struct gendisk *disk = sc->disk;
	request_queue_t *q = disk->queue;
	int uptodate;

	if (blk_pc_request(rq)) {
		/* UB_SENSE_SIZE is smaller than SCSI_SENSE_BUFFERSIZE */
		memcpy(rq->sense, sc->top_sense, UB_SENSE_SIZE);
		rq->sense_len = UB_SENSE_SIZE;
	}

	if (cmd->error == 0)
		uptodate = 1;
	else
		uptodate = 0;

	ub_put_cmd(sc, cmd);
	ub_end_rq(rq, uptodate);
	blk_start_queue(q);
}

static void ub_end_rq(struct request *rq, int uptodate)
{
	int rc;

	rc = end_that_request_first(rq, uptodate, rq->hard_nr_sectors);
	// assert(rc == 0);
	end_that_request_last(rq);
}

/*
 * Submit a regular SCSI operation (not an auto-sense).
 *
 * The Iron Law of Good Submit Routine is:
 * Zero return - callback is done, Nonzero return - callback is not done.
 * No exceptions.
 *
 * Host is assumed locked.
 *
 * XXX We only support Bulk for the moment.
 */
static int ub_submit_scsi(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{

	if (cmd->state != UB_CMDST_INIT ||
	    (cmd->dir != UB_DIR_NONE && cmd->len == 0)) {
		return -EINVAL;
	}

	ub_cmdq_add(sc, cmd);
	/*
	 * We can call ub_scsi_dispatch(sc) right away here, but it's a little
	 * safer to jump to a tasklet, in case upper layers do something silly.
	 */
	tasklet_schedule(&sc->tasklet);
	return 0;
}

/*
 * Submit the first URB for the queued command.
 * This function does not deal with queueing in any way.
 */
static int ub_scsi_cmd_start(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct bulk_cb_wrap *bcb;
	int rc;

	bcb = &sc->work_bcb;

	/*
	 * ``If the allocation length is eighteen or greater, and a device
	 * server returns less than eithteen bytes of data, the application
	 * client should assume that the bytes not transferred would have been
	 * zeroes had the device server returned those bytes.''
	 *
	 * We zero sense for all commands so that when a packet request
	 * fails it does not return a stale sense.
	 */
	memset(&sc->top_sense, 0, UB_SENSE_SIZE);

	/* set up the command wrapper */
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->Tag = cmd->tag;		/* Endianness is not important */
	bcb->DataTransferLength = cpu_to_le32(cmd->len);
	bcb->Flags = (cmd->dir == UB_DIR_READ) ? 0x80 : 0;
	bcb->Lun = 0;			/* No multi-LUN yet */
	bcb->Length = cmd->cdb_len;

	/* copy the command payload */
	memcpy(bcb->CDB, cmd->cdb, UB_MAX_CDB_SIZE);

	UB_INIT_COMPLETION(sc->work_done);

	sc->last_pipe = sc->send_bulk_pipe;
	usb_fill_bulk_urb(&sc->work_urb, sc->dev, sc->send_bulk_pipe,
	    bcb, US_BULK_CB_WRAP_LEN, ub_urb_complete, sc);
	sc->work_urb.transfer_flags = URB_ASYNC_UNLINK;

	/* Fill what we shouldn't be filling, because usb-storage did so. */
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_ATOMIC)) != 0) {
		/* XXX Clear stalls */
		printk("ub: cmd #%d start failed (%d)\n", cmd->tag, rc); /* P3 */
		ub_complete(&sc->work_done);
		return rc;
	}

	sc->work_timer.expires = jiffies + UB_URB_TIMEOUT;
	add_timer(&sc->work_timer);

	cmd->state = UB_CMDST_CMD;
	ub_cmdtr_state(sc, cmd);
	return 0;
}

/*
 * Timeout handler.
 */
static void ub_urb_timeout(unsigned long arg)
{
	struct ub_dev *sc = (struct ub_dev *) arg;
	unsigned long flags;

	spin_lock_irqsave(&sc->lock, flags);
	usb_unlink_urb(&sc->work_urb);
	spin_unlock_irqrestore(&sc->lock, flags);
}

/*
 * Completion routine for the work URB.
 *
 * This can be called directly from usb_submit_urb (while we have
 * the sc->lock taken) and from an interrupt (while we do NOT have
 * the sc->lock taken). Therefore, bounce this off to a tasklet.
 */
static void ub_urb_complete(struct urb *urb, struct pt_regs *pt)
{
	struct ub_dev *sc = urb->context;

	ub_complete(&sc->work_done);
	tasklet_schedule(&sc->tasklet);
}

static void ub_scsi_action(unsigned long _dev)
{
	struct ub_dev *sc = (struct ub_dev *) _dev;
	unsigned long flags;

	spin_lock_irqsave(&sc->lock, flags);
	del_timer(&sc->work_timer);
	ub_scsi_dispatch(sc);
	spin_unlock_irqrestore(&sc->lock, flags);
}

static void ub_scsi_dispatch(struct ub_dev *sc)
{
	struct ub_scsi_cmd *cmd;
	int rc;

	while ((cmd = ub_cmdq_peek(sc)) != NULL) {
		if (cmd->state == UB_CMDST_DONE) {
			ub_cmdq_pop(sc);
			(*cmd->done)(sc, cmd);
		} else if (cmd->state == UB_CMDST_INIT) {
			ub_cmdtr_new(sc, cmd);
			if ((rc = ub_scsi_cmd_start(sc, cmd)) == 0)
				break;
			cmd->error = rc;
			cmd->state = UB_CMDST_DONE;
			ub_cmdtr_state(sc, cmd);
		} else {
			if (!ub_is_completed(&sc->work_done))
				break;
			ub_scsi_urb_compl(sc, cmd);
		}
	}
}

static void ub_scsi_urb_compl(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct urb *urb = &sc->work_urb;
	struct bulk_cs_wrap *bcs;
	int pipe;
	int rc;

	if (atomic_read(&sc->poison)) {
		/* A little too simplistic, I feel... */
		goto Bad_End;
	}

	if (cmd->state == UB_CMDST_CLEAR) {
		if (urb->status == -EPIPE) {
			/*
			 * STALL while clearning STALL.
			 * The control pipe clears itself - nothing to do.
			 * XXX Might try to reset the device here and retry.
			 */
			printk(KERN_NOTICE "%s: "
			    "stall on control pipe for device %u\n",
			    sc->name, sc->dev->devnum);
			goto Bad_End;
		}

		/*
		 * We ignore the result for the halt clear.
		 */

		/* reset the endpoint toggle */
		usb_settoggle(sc->dev, usb_pipeendpoint(sc->last_pipe),
			usb_pipeout(sc->last_pipe), 0);

		ub_state_sense(sc, cmd);

	} else if (cmd->state == UB_CMDST_CLR2STS) {
		if (urb->status == -EPIPE) {
			/*
			 * STALL while clearning STALL.
			 * The control pipe clears itself - nothing to do.
			 * XXX Might try to reset the device here and retry.
			 */
			printk(KERN_NOTICE "%s: "
			    "stall on control pipe for device %u\n",
			    sc->name, sc->dev->devnum);
			goto Bad_End;
		}

		/*
		 * We ignore the result for the halt clear.
		 */

		/* reset the endpoint toggle */
		usb_settoggle(sc->dev, usb_pipeendpoint(sc->last_pipe),
			usb_pipeout(sc->last_pipe), 0);

		ub_state_stat(sc, cmd);

	} else if (cmd->state == UB_CMDST_CMD) {
		if (urb->status == -EPIPE) {
			rc = ub_submit_clear_stall(sc, cmd, sc->last_pipe);
			if (rc != 0) {
				printk(KERN_NOTICE "%s: "
				    "unable to submit clear for device %u"
				    " (code %d)\n",
				    sc->name, sc->dev->devnum, rc);
				/*
				 * This is typically ENOMEM or some other such shit.
				 * Retrying is pointless. Just do Bad End on it...
				 */
				goto Bad_End;
			}
			cmd->state = UB_CMDST_CLEAR;
			ub_cmdtr_state(sc, cmd);
			return;
		}
		if (urb->status != 0) {
			printk("ub: cmd #%d cmd status (%d)\n", cmd->tag, urb->status); /* P3 */
			goto Bad_End;
		}
		if (urb->actual_length != US_BULK_CB_WRAP_LEN) {
			printk("ub: cmd #%d xferred %d\n", cmd->tag, urb->actual_length); /* P3 */
			/* XXX Must do reset here to unconfuse the device */
			goto Bad_End;
		}

		if (cmd->dir == UB_DIR_NONE) {
			ub_state_stat(sc, cmd);
			return;
		}

		UB_INIT_COMPLETION(sc->work_done);

		if (cmd->dir == UB_DIR_READ)
			pipe = sc->recv_bulk_pipe;
		else
			pipe = sc->send_bulk_pipe;
		sc->last_pipe = pipe;
		usb_fill_bulk_urb(&sc->work_urb, sc->dev, pipe,
		    cmd->data, cmd->len, ub_urb_complete, sc);
		sc->work_urb.transfer_flags = URB_ASYNC_UNLINK;
		sc->work_urb.actual_length = 0;
		sc->work_urb.error_count = 0;
		sc->work_urb.status = 0;

		if ((rc = usb_submit_urb(&sc->work_urb, GFP_ATOMIC)) != 0) {
			/* XXX Clear stalls */
			printk("ub: data #%d submit failed (%d)\n", cmd->tag, rc); /* P3 */
			ub_complete(&sc->work_done);
			ub_state_done(sc, cmd, rc);
			return;
		}

		sc->work_timer.expires = jiffies + UB_DATA_TIMEOUT;
		add_timer(&sc->work_timer);

		cmd->state = UB_CMDST_DATA;
		ub_cmdtr_state(sc, cmd);

	} else if (cmd->state == UB_CMDST_DATA) {
		if (urb->status == -EPIPE) {
			rc = ub_submit_clear_stall(sc, cmd, sc->last_pipe);
			if (rc != 0) {
				printk(KERN_NOTICE "%s: "
				    "unable to submit clear for device %u"
				    " (code %d)\n",
				    sc->name, sc->dev->devnum, rc);
				/*
				 * This is typically ENOMEM or some other such shit.
				 * Retrying is pointless. Just do Bad End on it...
				 */
				goto Bad_End;
			}
			cmd->state = UB_CMDST_CLR2STS;
			ub_cmdtr_state(sc, cmd);
			return;
		}
		if (urb->status == -EOVERFLOW) {
			/*
			 * A babble? Failure, but we must transfer CSW now.
			 */
			cmd->error = -EOVERFLOW;	/* A cheap trick... */
		} else {
			if (urb->status != 0)
				goto Bad_End;
		}

		cmd->act_len = urb->actual_length;
		ub_cmdtr_act_len(sc, cmd);

		ub_state_stat(sc, cmd);

	} else if (cmd->state == UB_CMDST_STAT) {
		if (urb->status == -EPIPE) {
			rc = ub_submit_clear_stall(sc, cmd, sc->last_pipe);
			if (rc != 0) {
				printk(KERN_NOTICE "%s: "
				    "unable to submit clear for device %u"
				    " (code %d)\n",
				    sc->name, sc->dev->devnum, rc);
				/*
				 * This is typically ENOMEM or some other such shit.
				 * Retrying is pointless. Just do Bad End on it...
				 */
				goto Bad_End;
			}
			cmd->state = UB_CMDST_CLEAR;
			ub_cmdtr_state(sc, cmd);
			return;
		}
		if (urb->status != 0)
			goto Bad_End;

		if (urb->actual_length == 0) {
			/*
			 * Some broken devices add unnecessary zero-length
			 * packets to the end of their data transfers.
			 * Such packets show up as 0-length CSWs. If we
			 * encounter such a thing, try to read the CSW again.
			 */
			if (++cmd->stat_count >= 4) {
				printk(KERN_NOTICE "%s: "
				    "unable to get CSW on device %u\n",
				    sc->name, sc->dev->devnum);
				goto Bad_End;
			}
			__ub_state_stat(sc, cmd);
			return;
		}

		/*
		 * Check the returned Bulk protocol status.
		 */

		bcs = &sc->work_bcs;
		rc = le32_to_cpu(bcs->Residue);
		if (rc != cmd->len - cmd->act_len) {
			/*
			 * It is all right to transfer less, the caller has
			 * to check. But it's not all right if the device
			 * counts disagree with our counts.
			 */
			/* P3 */ printk("%s: resid %d len %d act %d\n",
			    sc->name, rc, cmd->len, cmd->act_len);
			goto Bad_End;
		}

#if 0
		if (bcs->Signature != cpu_to_le32(US_BULK_CS_SIGN) &&
		    bcs->Signature != cpu_to_le32(US_BULK_CS_OLYMPUS_SIGN)) {
			/* Windows ignores signatures, so do we. */
		}
#endif

		if (bcs->Tag != cmd->tag) {
			/*
			 * This usually happens when we disagree with the
			 * device's microcode about something. For instance,
			 * a few of them throw this after timeouts. They buffer
			 * commands and reply at commands we timed out before.
			 * Without flushing these replies we loop forever.
			 */
			if (++cmd->stat_count >= 4) {
				printk(KERN_NOTICE "%s: "
				    "tag mismatch orig 0x%x reply 0x%x "
				    "on device %u\n",
				    sc->name, cmd->tag, bcs->Tag,
				    sc->dev->devnum);
				goto Bad_End;
			}
			__ub_state_stat(sc, cmd);
			return;
		}

		switch (bcs->Status) {
		case US_BULK_STAT_OK:
			break;
		case US_BULK_STAT_FAIL:
			ub_state_sense(sc, cmd);
			return;
		case US_BULK_STAT_PHASE:
			/* XXX We must reset the transport here */
			/* P3 */ printk("%s: status PHASE\n", sc->name);
			goto Bad_End;
		default:
			printk(KERN_INFO "%s: unknown CSW status 0x%x\n",
			    sc->name, bcs->Status);
			goto Bad_End;
		}

		/* Not zeroing error to preserve a babble indicator */
		cmd->state = UB_CMDST_DONE;
		ub_cmdtr_state(sc, cmd);
		ub_cmdq_pop(sc);
		(*cmd->done)(sc, cmd);

	} else if (cmd->state == UB_CMDST_SENSE) {
		ub_state_done(sc, cmd, -EIO);

	} else {
		printk(KERN_WARNING "%s: "
		    "wrong command state %d on device %u\n",
		    sc->name, cmd->state, sc->dev->devnum);
		goto Bad_End;
	}
	return;

Bad_End: /* Little Excel is dead */
	ub_state_done(sc, cmd, -EIO);
}

/*
 * Factorization helper for the command state machine:
 * Finish the command.
 */
static void ub_state_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd, int rc)
{

	cmd->error = rc;
	cmd->state = UB_CMDST_DONE;
	ub_cmdtr_state(sc, cmd);
	ub_cmdq_pop(sc);
	(*cmd->done)(sc, cmd);
}

/*
 * Factorization helper for the command state machine:
 * Submit a CSW read.
 */
static void __ub_state_stat(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	int rc;

	UB_INIT_COMPLETION(sc->work_done);

	sc->last_pipe = sc->recv_bulk_pipe;
	usb_fill_bulk_urb(&sc->work_urb, sc->dev, sc->recv_bulk_pipe,
	    &sc->work_bcs, US_BULK_CS_WRAP_LEN, ub_urb_complete, sc);
	sc->work_urb.transfer_flags = URB_ASYNC_UNLINK;
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_ATOMIC)) != 0) {
		/* XXX Clear stalls */
		printk("%s: CSW #%d submit failed (%d)\n", sc->name, cmd->tag, rc); /* P3 */
		ub_complete(&sc->work_done);
		ub_state_done(sc, cmd, rc);
		return;
	}

	sc->work_timer.expires = jiffies + UB_STAT_TIMEOUT;
	add_timer(&sc->work_timer);
}

/*
 * Factorization helper for the command state machine:
 * Submit a CSW read and go to STAT state.
 */
static void ub_state_stat(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	__ub_state_stat(sc, cmd);

	cmd->stat_count = 0;
	cmd->state = UB_CMDST_STAT;
	ub_cmdtr_state(sc, cmd);
}

/*
 * Factorization helper for the command state machine:
 * Submit a REQUEST SENSE and go to SENSE state.
 */
static void ub_state_sense(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_scsi_cmd *scmd;
	int rc;

	if (cmd->cdb[0] == REQUEST_SENSE) {
		rc = -EPIPE;
		goto error;
	}

	scmd = &sc->top_rqs_cmd;
	scmd->cdb[0] = REQUEST_SENSE;
	scmd->cdb[4] = UB_SENSE_SIZE;
	scmd->cdb_len = 6;
	scmd->dir = UB_DIR_READ;
	scmd->state = UB_CMDST_INIT;
	scmd->data = sc->top_sense;
	scmd->len = UB_SENSE_SIZE;
	scmd->done = ub_top_sense_done;
	scmd->back = cmd;

	scmd->tag = sc->tagcnt++;

	cmd->state = UB_CMDST_SENSE;
	ub_cmdtr_state(sc, cmd);

	ub_cmdq_insert(sc, scmd);
	return;

error:
	ub_state_done(sc, cmd, rc);
}

/*
 * A helper for the command's state machine:
 * Submit a stall clear.
 */
static int ub_submit_clear_stall(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    int stalled_pipe)
{
	int endp;
	struct usb_ctrlrequest *cr;
	int rc;

	endp = usb_pipeendpoint(stalled_pipe);
	if (usb_pipein (stalled_pipe))
		endp |= USB_DIR_IN;

	cr = &sc->work_cr;
	cr->bRequestType = USB_RECIP_ENDPOINT;
	cr->bRequest = USB_REQ_CLEAR_FEATURE;
	cr->wValue = cpu_to_le16(USB_ENDPOINT_HALT);
	cr->wIndex = cpu_to_le16(endp);
	cr->wLength = cpu_to_le16(0);

	UB_INIT_COMPLETION(sc->work_done);

	usb_fill_control_urb(&sc->work_urb, sc->dev, sc->send_ctrl_pipe,
	    (unsigned char*) cr, NULL, 0, ub_urb_complete, sc);
	sc->work_urb.transfer_flags = URB_ASYNC_UNLINK;
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_ATOMIC)) != 0) {
		ub_complete(&sc->work_done);
		return rc;
	}

	sc->work_timer.expires = jiffies + UB_CTRL_TIMEOUT;
	add_timer(&sc->work_timer);
	return 0;
}

/*
 */
static void ub_top_sense_done(struct ub_dev *sc, struct ub_scsi_cmd *scmd)
{
	unsigned char *sense = scmd->data;
	struct ub_scsi_cmd *cmd;

	/*
	 * Ignoring scmd->act_len, because the buffer was pre-zeroed.
	 */
	ub_cmdtr_sense(sc, scmd, sense);

	/*
	 * Find the command which triggered the unit attention or a check,
	 * save the sense into it, and advance its state machine.
	 */
	if ((cmd = ub_cmdq_peek(sc)) == NULL) {
		printk(KERN_WARNING "%s: sense done while idle\n", sc->name);
		return;
	}
	if (cmd != scmd->back) {
		printk(KERN_WARNING "%s: "
		    "sense done for wrong command 0x%x on device %u\n",
		    sc->name, cmd->tag, sc->dev->devnum);
		return;
	}
	if (cmd->state != UB_CMDST_SENSE) {
		printk(KERN_WARNING "%s: "
		    "sense done with bad cmd state %d on device %u\n",
		    sc->name, cmd->state, sc->dev->devnum);
		return;
	}

	cmd->key = sense[2] & 0x0F;
	cmd->asc = sense[12];
	cmd->ascq = sense[13];

	ub_scsi_urb_compl(sc, cmd);
}

#if 0
/* Determine what the maximum LUN supported is */
int usb_stor_Bulk_max_lun(struct us_data *us)
{
	int result;

	/* issue the command */
	result = usb_stor_control_msg(us, us->recv_ctrl_pipe,
				 US_BULK_GET_MAX_LUN, 
				 USB_DIR_IN | USB_TYPE_CLASS | 
				 USB_RECIP_INTERFACE,
				 0, us->ifnum, us->iobuf, 1, HZ);

	/* 
	 * Some devices (i.e. Iomega Zip100) need this -- apparently
	 * the bulk pipes get STALLed when the GetMaxLUN request is
	 * processed.   This is, in theory, harmless to all other devices
	 * (regardless of if they stall or not).
	 */
	if (result < 0) {
		usb_stor_clear_halt(us, us->recv_bulk_pipe);
		usb_stor_clear_halt(us, us->send_bulk_pipe);
	}

	US_DEBUGP("GetMaxLUN command result is %d, data is %d\n", 
		  result, us->iobuf[0]);

	/* if we have a successful request, return the result */
	if (result == 1)
		return us->iobuf[0];

	/* return the default -- no LUNs */
	return 0;
}
#endif

/*
 * This is called from a process context.
 */
static void ub_revalidate(struct ub_dev *sc)
{

	sc->readonly = 0;	/* XXX Query this from the device */

	sc->capacity.nsec = 0;
	sc->capacity.bsize = 512;
	sc->capacity.bshift = 0;

	if (ub_sync_tur(sc) != 0)
		return;			/* Not ready */
	sc->changed = 0;

	if (ub_sync_read_cap(sc, &sc->capacity) != 0) {
		/*
		 * The retry here means something is wrong, either with the
		 * device, with the transport, or with our code.
		 * We keep this because sd.c has retries for capacity.
		 */
		if (ub_sync_read_cap(sc, &sc->capacity) != 0) {
			sc->capacity.nsec = 0;
			sc->capacity.bsize = 512;
			sc->capacity.bshift = 0;
		}
	}
}

/*
 * The open funcion.
 * This is mostly needed to keep refcounting, but also to support
 * media checks on removable media drives.
 */
static int ub_bd_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ub_dev *sc;
	unsigned long flags;
	int rc;

	if ((sc = disk->private_data) == NULL)
		return -ENXIO;
	spin_lock_irqsave(&ub_lock, flags);
	if (atomic_read(&sc->poison)) {
		spin_unlock_irqrestore(&ub_lock, flags);
		return -ENXIO;
	}
	sc->openc++;
	spin_unlock_irqrestore(&ub_lock, flags);

	/*
	 * This is a workaround for a specific problem in our block layer.
	 * In 2.6.9, register_disk duplicates the code from rescan_partitions.
	 * However, if we do add_disk with a device which persistently reports
	 * a changed media, add_disk calls register_disk, which does do_open,
	 * which will call rescan_paritions for changed media. After that,
	 * register_disk attempts to do it all again and causes double kobject
	 * registration and a eventually an oops on module removal.
	 *
	 * The bottom line is, Al Viro says that we should not allow
	 * bdev->bd_invalidated to be set when doing add_disk no matter what.
	 */
	if (sc->first_open) {
		if (sc->changed) {
			sc->first_open = 0;
			rc = -ENOMEDIUM;
			goto err_open;
		}
	}

	if (sc->removable || sc->readonly)
		check_disk_change(inode->i_bdev);

	/*
	 * The sd.c considers ->media_present and ->changed not equivalent,
	 * under some pretty murky conditions (a failure of READ CAPACITY).
	 * We may need it one day.
	 */
	if (sc->removable && sc->changed && !(filp->f_flags & O_NDELAY)) {
		rc = -ENOMEDIUM;
		goto err_open;
	}

	if (sc->readonly && (filp->f_mode & FMODE_WRITE)) {
		rc = -EROFS;
		goto err_open;
	}

	return 0;

err_open:
	spin_lock_irqsave(&ub_lock, flags);
	--sc->openc;
	if (sc->openc == 0 && atomic_read(&sc->poison))
		ub_cleanup(sc);
	spin_unlock_irqrestore(&ub_lock, flags);
	return rc;
}

/*
 */
static int ub_bd_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ub_dev *sc = disk->private_data;
	unsigned long flags;

	spin_lock_irqsave(&ub_lock, flags);
	--sc->openc;
	if (sc->openc == 0)
		sc->first_open = 0;
	if (sc->openc == 0 && atomic_read(&sc->poison))
		ub_cleanup(sc);
	spin_unlock_irqrestore(&ub_lock, flags);
	return 0;
}

/*
 * The ioctl interface.
 */
static int ub_bd_ioctl(struct inode *inode, struct file *filp,
    unsigned int cmd, unsigned long arg)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	void __user *usermem = (void __user *) arg;

	return scsi_cmd_ioctl(filp, disk, cmd, usermem);
}

/*
 * This is called once a new disk was seen by the block layer or by ub_probe().
 * The main onjective here is to discover the features of the media such as
 * the capacity, read-only status, etc. USB storage generally does not
 * need to be spun up, but if we needed it, this would be the place.
 *
 * This call can sleep.
 *
 * The return code is not used.
 */
static int ub_bd_revalidate(struct gendisk *disk)
{
	struct ub_dev *sc = disk->private_data;

	ub_revalidate(sc);
	/* This is pretty much a long term P3 */
	if (!atomic_read(&sc->poison)) {		/* Cover sc->dev */
		printk(KERN_INFO "%s: device %u capacity nsec %ld bsize %u\n",
		    sc->name, sc->dev->devnum,
		    sc->capacity.nsec, sc->capacity.bsize);
	}

	/* XXX Support sector size switching like in sr.c */
	blk_queue_hardsect_size(disk->queue, sc->capacity.bsize);
	set_capacity(disk, sc->capacity.nsec);
	// set_disk_ro(sdkp->disk, sc->readonly);

	return 0;
}

/*
 * The check is called by the block layer to verify if the media
 * is still available. It is supposed to be harmless, lightweight and
 * non-intrusive in case the media was not changed.
 *
 * This call can sleep.
 *
 * The return code is bool!
 */
static int ub_bd_media_changed(struct gendisk *disk)
{
	struct ub_dev *sc = disk->private_data;

	if (!sc->removable)
		return 0;

	/*
	 * We clean checks always after every command, so this is not
	 * as dangerous as it looks. If the TEST_UNIT_READY fails here,
	 * the device is actually not ready with operator or software
	 * intervention required. One dangerous item might be a drive which
	 * spins itself down, and come the time to write dirty pages, this
	 * will fail, then block layer discards the data. Since we never
	 * spin drives up, such devices simply cannot be used with ub anyway.
	 */
	if (ub_sync_tur(sc) != 0) {
		sc->changed = 1;
		return 1;
	}

	return sc->changed;
}

static struct block_device_operations ub_bd_fops = {
	.owner		= THIS_MODULE,
	.open		= ub_bd_open,
	.release	= ub_bd_release,
	.ioctl		= ub_bd_ioctl,
	.media_changed	= ub_bd_media_changed,
	.revalidate_disk = ub_bd_revalidate,
};

/*
 * Common ->done routine for commands executed synchronously.
 */
static void ub_probe_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct completion *cop = cmd->back;
	complete(cop);
}

/*
 * Test if the device has a check condition on it, synchronously.
 */
static int ub_sync_tur(struct ub_dev *sc)
{
	struct ub_scsi_cmd *cmd;
	enum { ALLOC_SIZE = sizeof(struct ub_scsi_cmd) };
	unsigned long flags;
	struct completion compl;
	int rc;

	init_completion(&compl);

	rc = -ENOMEM;
	if ((cmd = kmalloc(ALLOC_SIZE, GFP_KERNEL)) == NULL)
		goto err_alloc;
	memset(cmd, 0, ALLOC_SIZE);

	cmd->cdb[0] = TEST_UNIT_READY;
	cmd->cdb_len = 6;
	cmd->dir = UB_DIR_NONE;
	cmd->state = UB_CMDST_INIT;
	cmd->done = ub_probe_done;
	cmd->back = &compl;

	spin_lock_irqsave(&sc->lock, flags);
	cmd->tag = sc->tagcnt++;

	rc = ub_submit_scsi(sc, cmd);
	spin_unlock_irqrestore(&sc->lock, flags);

	if (rc != 0) {
		printk("ub: testing ready: submit error (%d)\n", rc); /* P3 */
		goto err_submit;
	}

	wait_for_completion(&compl);

	rc = cmd->error;

	if (rc == -EIO && cmd->key != 0)	/* Retries for benh's key */
		rc = cmd->key;

err_submit:
	kfree(cmd);
err_alloc:
	return rc;
}

/*
 * Read the SCSI capacity synchronously (for probing).
 */
static int ub_sync_read_cap(struct ub_dev *sc, struct ub_capacity *ret)
{
	struct ub_scsi_cmd *cmd;
	char *p;
	enum { ALLOC_SIZE = sizeof(struct ub_scsi_cmd) + 8 };
	unsigned long flags;
	unsigned int bsize, shift;
	unsigned long nsec;
	struct completion compl;
	int rc;

	init_completion(&compl);

	rc = -ENOMEM;
	if ((cmd = kmalloc(ALLOC_SIZE, GFP_KERNEL)) == NULL)
		goto err_alloc;
	memset(cmd, 0, ALLOC_SIZE);
	p = (char *)cmd + sizeof(struct ub_scsi_cmd);

	cmd->cdb[0] = 0x25;
	cmd->cdb_len = 10;
	cmd->dir = UB_DIR_READ;
	cmd->state = UB_CMDST_INIT;
	cmd->data = p;
	cmd->len = 8;
	cmd->done = ub_probe_done;
	cmd->back = &compl;

	spin_lock_irqsave(&sc->lock, flags);
	cmd->tag = sc->tagcnt++;

	rc = ub_submit_scsi(sc, cmd);
	spin_unlock_irqrestore(&sc->lock, flags);

	if (rc != 0) {
		printk("ub: reading capacity: submit error (%d)\n", rc); /* P3 */
		goto err_submit;
	}

	wait_for_completion(&compl);

	if (cmd->error != 0) {
		printk("ub: reading capacity: error %d\n", cmd->error); /* P3 */
		rc = -EIO;
		goto err_read;
	}
	if (cmd->act_len != 8) {
		printk("ub: reading capacity: size %d\n", cmd->act_len); /* P3 */
		rc = -EIO;
		goto err_read;
	}

	/* sd.c special-cases sector size of 0 to mean 512. Needed? Safe? */
	nsec = be32_to_cpu(*(__be32 *)p) + 1;
	bsize = be32_to_cpu(*(__be32 *)(p + 4));
	switch (bsize) {
	case 512:	shift = 0;	break;
	case 1024:	shift = 1;	break;
	case 2048:	shift = 2;	break;
	case 4096:	shift = 3;	break;
	default:
		printk("ub: Bad sector size %u\n", bsize); /* P3 */
		rc = -EDOM;
		goto err_inv_bsize;
	}

	ret->bsize = bsize;
	ret->bshift = shift;
	ret->nsec = nsec << shift;
	rc = 0;

err_inv_bsize:
err_read:
err_submit:
	kfree(cmd);
err_alloc:
	return rc;
}

/*
 */
static void ub_probe_urb_complete(struct urb *urb, struct pt_regs *pt)
{
	struct completion *cop = urb->context;
	complete(cop);
}

static void ub_probe_timeout(unsigned long arg)
{
	struct completion *cop = (struct completion *) arg;
	complete(cop);
}

/*
 * Clear initial stalls.
 */
static int ub_probe_clear_stall(struct ub_dev *sc, int stalled_pipe)
{
	int endp;
	struct usb_ctrlrequest *cr;
	struct completion compl;
	struct timer_list timer;
	int rc;

	init_completion(&compl);

	endp = usb_pipeendpoint(stalled_pipe);
	if (usb_pipein (stalled_pipe))
		endp |= USB_DIR_IN;

	cr = &sc->work_cr;
	cr->bRequestType = USB_RECIP_ENDPOINT;
	cr->bRequest = USB_REQ_CLEAR_FEATURE;
	cr->wValue = cpu_to_le16(USB_ENDPOINT_HALT);
	cr->wIndex = cpu_to_le16(endp);
	cr->wLength = cpu_to_le16(0);

	usb_fill_control_urb(&sc->work_urb, sc->dev, sc->send_ctrl_pipe,
	    (unsigned char*) cr, NULL, 0, ub_probe_urb_complete, &compl);
	sc->work_urb.transfer_flags = 0;
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_KERNEL)) != 0) {
		printk(KERN_WARNING
		     "%s: Unable to submit a probe clear (%d)\n", sc->name, rc);
		return rc;
	}

	init_timer(&timer);
	timer.function = ub_probe_timeout;
	timer.data = (unsigned long) &compl;
	timer.expires = jiffies + UB_CTRL_TIMEOUT;
	add_timer(&timer);

	wait_for_completion(&compl);

	del_timer_sync(&timer);
	usb_kill_urb(&sc->work_urb);

	/* reset the endpoint toggle */
	usb_settoggle(sc->dev, endp, usb_pipeout(sc->last_pipe), 0);

	return 0;
}

/*
 * Get the pipe settings.
 */
static int ub_get_pipes(struct ub_dev *sc, struct usb_device *dev,
    struct usb_interface *intf)
{
	struct usb_host_interface *altsetting = intf->cur_altsetting;
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct usb_endpoint_descriptor *ep;
	int i;

	/*
	 * Find the endpoints we need.
	 * We are expecting a minimum of 2 endpoints - in and out (bulk).
	 * We will ignore any others.
	 */
	for (i = 0; i < altsetting->desc.bNumEndpoints; i++) {
		ep = &altsetting->endpoint[i].desc;

		/* Is it a BULK endpoint? */
		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_BULK) {
			/* BULK in or out? */
			if (ep->bEndpointAddress & USB_DIR_IN)
				ep_in = ep;
			else
				ep_out = ep;
		}
	}

	if (ep_in == NULL || ep_out == NULL) {
		printk(KERN_NOTICE "%s: device %u failed endpoint check\n",
		    sc->name, sc->dev->devnum);
		return -EIO;
	}

	/* Calculate and store the pipe values */
	sc->send_ctrl_pipe = usb_sndctrlpipe(dev, 0);
	sc->recv_ctrl_pipe = usb_rcvctrlpipe(dev, 0);
	sc->send_bulk_pipe = usb_sndbulkpipe(dev,
		ep_out->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	sc->recv_bulk_pipe = usb_rcvbulkpipe(dev, 
		ep_in->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);

	return 0;
}

/*
 * Probing is done in the process context, which allows us to cheat
 * and not to build a state machine for the discovery.
 */
static int ub_probe(struct usb_interface *intf,
    const struct usb_device_id *dev_id)
{
	struct ub_dev *sc;
	request_queue_t *q;
	struct gendisk *disk;
	int rc;
	int i;

	rc = -ENOMEM;
	if ((sc = kmalloc(sizeof(struct ub_dev), GFP_KERNEL)) == NULL)
		goto err_core;
	memset(sc, 0, sizeof(struct ub_dev));
	spin_lock_init(&sc->lock);
	usb_init_urb(&sc->work_urb);
	tasklet_init(&sc->tasklet, ub_scsi_action, (unsigned long)sc);
	atomic_set(&sc->poison, 0);

	init_timer(&sc->work_timer);
	sc->work_timer.data = (unsigned long) sc;
	sc->work_timer.function = ub_urb_timeout;

	ub_init_completion(&sc->work_done);
	sc->work_done.done = 1;		/* A little yuk, but oh well... */

	rc = -ENOSR;
	if ((sc->id = ub_id_get()) == -1)
		goto err_id;
	snprintf(sc->name, 8, DRV_NAME "%c", sc->id + 'a');

	sc->dev = interface_to_usbdev(intf);
	sc->intf = intf;
	// sc->ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

	usb_set_intfdata(intf, sc);
	usb_get_dev(sc->dev);
	// usb_get_intf(sc->intf);	/* Do we need this? */

	/* XXX Verify that we can handle the device (from descriptors) */

	ub_get_pipes(sc, sc->dev, intf);

	if (device_create_file(&sc->intf->dev, &dev_attr_diag) != 0)
		goto err_diag;

	/*
	 * At this point, all USB initialization is done, do upper layer.
	 * We really hate halfway initialized structures, so from the
	 * invariants perspective, this ub_dev is fully constructed at
	 * this point.
	 */

	/*
	 * This is needed to clear toggles. It is a problem only if we do
	 * `rmmod ub && modprobe ub` without disconnects, but we like that.
	 */
	ub_probe_clear_stall(sc, sc->recv_bulk_pipe);
	ub_probe_clear_stall(sc, sc->send_bulk_pipe);

	/*
	 * The way this is used by the startup code is a little specific.
	 * A SCSI check causes a USB stall. Our common case code sees it
	 * and clears the check, after which the device is ready for use.
	 * But if a check was not present, any command other than
	 * TEST_UNIT_READY ends with a lockup (including REQUEST_SENSE).
	 *
	 * If we neglect to clear the SCSI check, the first real command fails
	 * (which is the capacity readout). We clear that and retry, but why
	 * causing spurious retries for no reason.
	 *
	 * Revalidation may start with its own TEST_UNIT_READY, but that one
	 * has to succeed, so we clear checks with an additional one here.
	 * In any case it's not our business how revaliadation is implemented.
	 */
	for (i = 0; i < 3; i++) {	/* Retries for benh's key */
		if ((rc = ub_sync_tur(sc)) <= 0) break;
		if (rc != 0x6) break;
		msleep(10);
	}

	sc->removable = 1;		/* XXX Query this from the device */
	sc->changed = 1;		/* ub_revalidate clears only */
	sc->first_open = 1;

	ub_revalidate(sc);
	/* This is pretty much a long term P3 */
	printk(KERN_INFO "%s: device %u capacity nsec %ld bsize %u\n",
	    sc->name, sc->dev->devnum, sc->capacity.nsec, sc->capacity.bsize);

	/*
	 * Just one disk per sc currently, but maybe more.
	 */
	rc = -ENOMEM;
	if ((disk = alloc_disk(UB_MINORS_PER_MAJOR)) == NULL)
		goto err_diskalloc;

	sc->disk = disk;
	sprintf(disk->disk_name, DRV_NAME "%c", sc->id + 'a');
	sprintf(disk->devfs_name, DEVFS_NAME "/%c", sc->id + 'a');
	disk->major = UB_MAJOR;
	disk->first_minor = sc->id * UB_MINORS_PER_MAJOR;
	disk->fops = &ub_bd_fops;
	disk->private_data = sc;
	disk->driverfs_dev = &intf->dev;

	rc = -ENOMEM;
	if ((q = blk_init_queue(ub_bd_rq_fn, &sc->lock)) == NULL)
		goto err_blkqinit;

	disk->queue = q;

        // blk_queue_bounce_limit(q, hba[i]->pdev->dma_mask);
	blk_queue_max_hw_segments(q, UB_MAX_REQ_SG);
	blk_queue_max_phys_segments(q, UB_MAX_REQ_SG);
	// blk_queue_segment_boundary(q, CARM_SG_BOUNDARY);
	blk_queue_max_sectors(q, UB_MAX_SECTORS);
	blk_queue_hardsect_size(q, sc->capacity.bsize);

	/*
	 * This is a serious infraction, caused by a deficiency in the
	 * USB sg interface (usb_sg_wait()). We plan to remove this once
	 * we get mileage on the driver and can justify a change to USB API.
	 * See blk_queue_bounce_limit() to understand this part.
	 *
	 * XXX And I still need to be aware of the DMA mask in the HC.
	 */
	q->bounce_pfn = blk_max_low_pfn;
	q->bounce_gfp = GFP_NOIO;

	q->queuedata = sc;

	set_capacity(disk, sc->capacity.nsec);
	if (sc->removable)
		disk->flags |= GENHD_FL_REMOVABLE;

	add_disk(disk);

	return 0;

err_blkqinit:
	put_disk(disk);
err_diskalloc:
	device_remove_file(&sc->intf->dev, &dev_attr_diag);
err_diag:
	usb_set_intfdata(intf, NULL);
	// usb_put_intf(sc->intf);
	usb_put_dev(sc->dev);
	spin_lock_irq(&ub_lock);
	ub_id_put(sc->id);
	spin_unlock_irq(&ub_lock);
err_id:
	kfree(sc);
err_core:
	return rc;
}

static void ub_disconnect(struct usb_interface *intf)
{
	struct ub_dev *sc = usb_get_intfdata(intf);
	struct gendisk *disk = sc->disk;
	request_queue_t *q = disk->queue;
	unsigned long flags;

	/*
	 * Fence stall clearnings, operations triggered by unlinkings and so on.
	 * We do not attempt to unlink any URBs, because we do not trust the
	 * unlink paths in HC drivers. Also, we get -84 upon disconnect anyway.
	 */
	atomic_set(&sc->poison, 1);

	/*
	 * Blow away queued commands.
	 *
	 * Actually, this never works, because before we get here
	 * the HCD terminates outstanding URB(s). It causes our
	 * SCSI command queue to advance, commands fail to submit,
	 * and the whole queue drains. So, we just use this code to
	 * print warnings.
	 */
	spin_lock_irqsave(&sc->lock, flags);
	{
		struct ub_scsi_cmd *cmd;
		int cnt = 0;
		while ((cmd = ub_cmdq_pop(sc)) != NULL) {
			cmd->error = -ENOTCONN;
			cmd->state = UB_CMDST_DONE;
			ub_cmdtr_state(sc, cmd);
			ub_cmdq_pop(sc);
			(*cmd->done)(sc, cmd);
			cnt++;
		}
		if (cnt != 0) {
			printk(KERN_WARNING "%s: "
			    "%d was queued after shutdown\n", sc->name, cnt);
		}
	}
	spin_unlock_irqrestore(&sc->lock, flags);

	/*
	 * Unregister the upper layer, this waits for all commands to end.
	 */
	if (disk->flags & GENHD_FL_UP)
		del_gendisk(disk);
	if (q)
		blk_cleanup_queue(q);

	/*
	 * We really expect blk_cleanup_queue() to wait, so no amount
	 * of paranoya is too much.
	 *
	 * Taking a lock on a structure which is about to be freed
	 * is very nonsensual. Here it is largely a way to do a debug freeze,
	 * and a bracket which shows where the nonsensual code segment ends.
	 *
	 * Testing for -EINPROGRESS is always a bug, so we are bending
	 * the rules a little.
	 */
	spin_lock_irqsave(&sc->lock, flags);
	if (sc->work_urb.status == -EINPROGRESS) {	/* janitors: ignore */
		printk(KERN_WARNING "%s: "
		    "URB is active after disconnect\n", sc->name);
	}
	spin_unlock_irqrestore(&sc->lock, flags);

	/*
	 * There is virtually no chance that other CPU runs times so long
	 * after ub_urb_complete should have called del_timer, but only if HCD
	 * didn't forget to deliver a callback on unlink.
	 */
	del_timer_sync(&sc->work_timer);

	/*
	 * At this point there must be no commands coming from anyone
	 * and no URBs left in transit.
	 */

	device_remove_file(&sc->intf->dev, &dev_attr_diag);
	usb_set_intfdata(intf, NULL);
	// usb_put_intf(sc->intf);
	sc->intf = NULL;
	usb_put_dev(sc->dev);
	sc->dev = NULL;

	spin_lock_irqsave(&ub_lock, flags);
	if (sc->openc == 0)
		ub_cleanup(sc);
	spin_unlock_irqrestore(&ub_lock, flags);
}

struct usb_driver ub_driver = {
	.owner =	THIS_MODULE,
	.name =		"ub",
	.probe =	ub_probe,
	.disconnect =	ub_disconnect,
	.id_table =	ub_usb_ids,
};

static int __init ub_init(void)
{
	int rc;

	/* P3 */ printk("ub: sizeof ub_scsi_cmd %zu ub_dev %zu\n",
			sizeof(struct ub_scsi_cmd), sizeof(struct ub_dev));

	if ((rc = register_blkdev(UB_MAJOR, DRV_NAME)) != 0)
		goto err_regblkdev;
	devfs_mk_dir(DEVFS_NAME);

	if ((rc = usb_register(&ub_driver)) != 0)
		goto err_register;

	return 0;

err_register:
	devfs_remove(DEVFS_NAME);
	unregister_blkdev(UB_MAJOR, DRV_NAME);
err_regblkdev:
	return rc;
}

static void __exit ub_exit(void)
{
	usb_deregister(&ub_driver);

	devfs_remove(DEVFS_NAME);
	unregister_blkdev(UB_MAJOR, DRV_NAME);
}

module_init(ub_init);
module_exit(ub_exit);

MODULE_LICENSE("GPL");

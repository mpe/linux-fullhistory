/* fc.c: Generic Fibre Channel and FC4 SCSI driver.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997,1998 Jirka Hanika (geo@ff.cuni.cz)
 *
 * Sources:
 *	Fibre Channel Physical & Signaling Interface (FC-PH), dpANS, 1994
 *	dpANS Fibre Channel Protocol for SCSI (X3.269-199X), Rev. 012, 1995
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/blk.h>

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/semaphore.h>
#include "fcp_scsi.h"
#include "../scsi/hosts.h"

/* #define FCDEBUG */

#define fc_printk printk ("%s: ", fc->name); printk 

#ifdef FCDEBUG
#define FCD(x)  fc_printk x;
#define FCND(x)	printk ("FC: "); printk x;
#else
#define FCD(x)
#define FCND(x)
#endif

#ifdef __sparc__
static inline void *fc_dma_alloc(long size, char *name, dma_handle *dma)
{
	return (void *) sparc_dvma_malloc (size, "FCP SCSI cmd & rsp queues", dma);
}

static inline dma_handle fc_sync_dma_entry(void *buf, int len, fc_channel *fc)
{
	return mmu_get_scsi_one (buf, len, fc->dev->my_bus);
}

static inline void fc_sync_dma_exit(dma_handle dmh, long size, fc_channel *fc)
{
	mmu_release_scsi_one (dmh, size, fc->dev->my_bus);
}

static inline void fc_sync_dma_entry_sg(struct scatterlist *list, int count, fc_channel *fc)
{
	mmu_get_scsi_sgl((struct mmu_sglist *)list, count - 1, fc->dev->my_bus);
}

static inline void fc_sync_dma_exit_sg(struct scatterlist *list, int count, fc_channel *fc)
{
	mmu_release_scsi_sgl ((struct mmu_sglist *)list, count - 1, fc->dev->my_bus);
}
#else
#error Port this
#endif							       

#define FCP_CMND(SCpnt) ((fcp_cmnd *)&(SCpnt->SCp))
#define FC_SCMND(SCpnt) ((fc_channel *)(SCpnt->host->hostdata[0]))
#define SC_FCMND(fcmnd) ((Scsi_Cmnd *)((long)fcmnd - (long)&(((Scsi_Cmnd *)0)->SCp)))

static void fcp_scsi_insert_queue (fc_channel *fc, fcp_cmnd *fcmd)
{
	if (!fc->scsi_que) {
		fc->scsi_que = fcmd;
		fcmd->next = fcmd;
		fcmd->prev = fcmd;
	} else {
		fc->scsi_que->prev->next = fcmd;
		fcmd->prev = fc->scsi_que->prev;
		fc->scsi_que->prev = fcmd;
		fcmd->next = fc->scsi_que;
	}
}

static void fcp_scsi_remove_queue (fc_channel *fc, fcp_cmnd *fcmd)
{
	if (fcmd == fcmd->next) {
		fc->scsi_que = NULL;
		return;
	}
	if (fcmd == fc->scsi_que)
		fc->scsi_que = fcmd->next;
	fcmd->prev->next = fcmd->next;
	fcmd->next->prev = fcmd->prev;
}

fc_channel *fc_channels = NULL;

#define LSMAGIC	0x2a3b4d2a
typedef struct {
	/* Must be first */
	struct semaphore sem;
	int magic;
	int count;
	logi *logi;
	fcp_cmnd *fcmds;
	atomic_t todo;
	struct timer_list timer;
	int grace[1];
} ls;

#define LSOMAGIC 0x2a3c4e3c
typedef struct {
	/* Must be first */
	struct semaphore sem;
	int magic;
	int count;
	fcp_cmnd *fcmds;
	atomic_t todo;
	struct timer_list timer;
} lso;

static void fcp_login_timeout(unsigned long data)
{
	ls *l = (ls *)data;
	FCND(("Login timeout\n"))
	up(&l->sem);
}

static void fcp_login_done(fc_channel *fc, int i, int status)
{
	fcp_cmnd *fcmd;
	logi *plogi;
	fc_hdr *fch;
	ls *l = (ls *)fc->ls;
	
	FCD(("Login done %d %d\n", i, status))
	if (i < l->count) {
		if (fc->state == FC_STATE_FPORT_OK) {
			FCD(("Additional FPORT_OK received with status %d\n", status))
			return;
		}
		switch (status) {
		case FC_STATUS_OK: /* Oh, we found a fabric */
		case FC_STATUS_P_RJT: /* Oh, we haven't found any */
			fc->state = FC_STATE_FPORT_OK;
			fcmd = l->fcmds + i;
			plogi = l->logi + 3 * i;
			fc_sync_dma_exit (fcmd->cmd, 3 * sizeof(logi), fc);
			plogi->code = LS_PLOGI;
			memcpy (&plogi->nport_wwn, &fc->wwn_nport, sizeof(fc_wwn));
			memcpy (&plogi->node_wwn, &fc->wwn_node, sizeof(fc_wwn));
			memcpy (&plogi->common, fc->common_svc, sizeof(common_svc_parm));
			memcpy (&plogi->class1, fc->class_svcs, 3*sizeof(svc_parm));
			fch = &fcmd->fch;
			fcmd->token += l->count;
			FILL_FCHDR_RCTL_DID(fch, R_CTL_ELS_REQ, fc->did);
			FILL_FCHDR_SID(fch, fc->sid);
#ifdef FCDEBUG
			{
				int i;
				unsigned *x = (unsigned *)plogi;
				printk ("logi: ");
				for (i = 0; i < 21; i++)
					printk ("%08x ", x[i]);
				printk ("\n");
			}
#endif			
			fcmd->cmd = fc_sync_dma_entry (plogi, 3 * sizeof(logi), fc);
			fcmd->rsp = fcmd->cmd + 2 * sizeof(logi);
			if (fc->hw_enque (fc, fcmd))
				printk ("FC: Cannot enque PLOGI packet on %s\n", fc->name);
			break;
		case FC_STATUS_ERR_OFFLINE:
			fc->state = FC_STATE_MAYBEOFFLINE;
			FCD (("FC is offline %d\n", l->grace[i]))
			break;
		default:
			printk ("FLOGI failed for %s with status %d\n", fc->name, status);
			/* Do some sort of error recovery here */
			break;
		}
	} else {
		i -= l->count;
		if (fc->state != FC_STATE_FPORT_OK) {
			FCD(("Unexpected N-PORT rsp received"))
			return;
		}
		switch (status) {
		case FC_STATUS_OK:
			plogi = l->logi + 3 * i;
			fc_sync_dma_exit (l->fcmds[i].cmd, 3 * sizeof(logi), fc);
			if (!fc->wwn_dest.lo && !fc->wwn_dest.hi) {
				memcpy (&fc->wwn_dest, &plogi[1].node_wwn, sizeof(fc_wwn)); 
				FCD(("Dest WWN %08x%08x\n", *(u32 *)&fc->wwn_dest, fc->wwn_dest.lo))
			} else if (fc->wwn_dest.lo != plogi[1].node_wwn.lo ||
				   fc->wwn_dest.hi != plogi[1].node_wwn.hi) {
				printk ("%s: mismatch in wwns. Got %08x%08x, expected %08x%08x\n",
					fc->name,
					*(u32 *)&plogi[1].node_wwn, plogi[1].node_wwn.lo,
					*(u32 *)&fc->wwn_dest, fc->wwn_dest.lo);
			}
			fc->state = FC_STATE_ONLINE;
			printk ("%s: ONLINE\n", fc->name);
			if (atomic_dec_and_test (&l->todo))
				up(&l->sem);
			break;
		case FC_STATUS_ERR_OFFLINE:
			fc->state = FC_STATE_OFFLINE;
			fc_sync_dma_exit (l->fcmds[i].cmd, 3 * sizeof(logi), fc);
			printk ("%s: FC is offline\n", fc->name);
			if (atomic_dec_and_test (&l->todo))
				up(&l->sem);
			break;
		default:
			printk ("PLOGI failed for %s with status %d\n", fc->name, status);
			/* Do some sort of error recovery here */
			break;
		}
	}
}

void fcp_register(fc_channel *fc, u8 type, int unregister)
{
	int size, i;
	int slots = (fc->can_queue * 3) >> 1;

	FCND(("Going to %sregister\n", unregister ? "un" : ""))

	if (type == TYPE_SCSI_FCP) {
		if (!unregister) {
			fc->scsi_cmd_pool = 
				(fcp_cmd *) fc_dma_alloc (slots * (sizeof (fcp_cmd) + fc->rsp_size), 
							  "FCP SCSI cmd & rsp queues", &fc->dma_scsi_cmd);
			fc->scsi_rsp_pool = (char *)(fc->scsi_cmd_pool + slots);
			fc->dma_scsi_rsp = fc->dma_scsi_cmd + slots * sizeof (fcp_cmd);
			fc->scsi_bitmap_end = (slots + 63) & ~63;
			size = fc->scsi_bitmap_end / 8;
			fc->scsi_bitmap = kmalloc (size, GFP_KERNEL);
			memset (fc->scsi_bitmap, 0, size);
			set_bit (0, fc->scsi_bitmap);
			for (i = fc->can_queue; i < fc->scsi_bitmap_end; i++)
				set_bit (i, fc->scsi_bitmap);
			fc->scsi_free = fc->can_queue;
			fc->token_tab = (fcp_cmnd **)kmalloc(slots * sizeof(fcp_cmnd*), GFP_KERNEL);
			fc->abort_count = 0;
		} else {
			fc->scsi_name[0] = 0;
			kfree (fc->scsi_bitmap);
			kfree (fc->token_tab);
			FCND(("Unregistering\n"));
			if (fc->rst_pkt) {
				if (fc->rst_pkt->eh_state == SCSI_STATE_UNUSED)
					kfree(fc->rst_pkt);
				else {
					/* Can't happen. Some memory would be lost. */
					printk("FC: Reset in progress. Now?!");
				}
			}
			FCND(("Unregistered\n"));
		}
	} else
		printk ("FC: %segistering unknown type %02x\n", unregister ? "Unr" : "R", type);
}

static void fcp_scsi_done(Scsi_Cmnd *SCpnt);

static inline void fcp_scsi_receive(fc_channel *fc, int token, int status, fc_hdr *fch)
{
	fcp_cmnd *fcmd;
	fcp_rsp  *rsp;
	int host_status;
	Scsi_Cmnd *SCpnt;
	int sense_len;
	int rsp_status;

	fcmd = fc->token_tab[token];
	if (!fcmd) return;
	rsp = (fcp_rsp *) (fc->scsi_rsp_pool + fc->rsp_size * token);
	SCpnt = SC_FCMND(fcmd);

	if (SCpnt->done != fcp_scsi_done)
		return;

	rsp_status = rsp->fcp_status;
	FCD(("rsp_status %08x status %08x\n", rsp_status, status))
	switch (status) {
	case FC_STATUS_OK:
		host_status=DID_OK;
		
		if (rsp_status & FCP_STATUS_RESID) {
#ifdef FCDEBUG
			FCD(("Resid %d\n", rsp->fcp_resid))
			{
				fcp_cmd *cmd = fc->scsi_cmd_pool + token;
				int i;
				
				printk ("Command ");
				for (i = 0; i < sizeof(fcp_cmd); i+=4)
					printk ("%08x ", *(u32 *)(((char *)cmd)+i));
				printk ("\nResponse ");
				for (i = 0; i < fc->rsp_size; i+=4)
					printk ("%08x ", *(u32 *)(((char *)rsp)+i));
				printk ("\n");
			}
#endif			
		}

		if (rsp_status & FCP_STATUS_SENSE_LEN) {
			sense_len = rsp->fcp_sense_len;
			if (sense_len > sizeof(SCpnt->sense_buffer)) sense_len = sizeof(SCpnt->sense_buffer);
			memcpy(SCpnt->sense_buffer, ((char *)(rsp+1)), sense_len);
		}
		
		if (fcmd->data) {
			if (SCpnt->use_sg)
				fc_sync_dma_exit_sg((struct scatterlist *)SCpnt->buffer, SCpnt->use_sg, fc);
			else
				fc_sync_dma_exit(fcmd->data, SCpnt->request_bufflen, fc);
		}
		break;
	default:
		host_status=DID_ERROR; /* FIXME */
		FCD(("Wrong FC status %d for token %d\n", status, token))
		break;
	}

	if (status_byte(rsp_status) == QUEUE_FULL) {
		printk ("%s: (%d,%d) Received rsp_status 0x%x\n", fc->name, SCpnt->channel, SCpnt->target, rsp_status);
	}	
	
	SCpnt->result = (host_status << 16) | (rsp_status & 0xff);
#ifdef FCDEBUG	
	if (host_status || SCpnt->result || rsp_status) printk("FC: host_status %d, packet status %d\n",
			host_status, SCpnt->result);
#endif
	SCpnt->done = fcmd->done;
	fcmd->done=NULL;
	clear_bit(token, fc->scsi_bitmap);
	fc->scsi_free++;
	FCD(("Calling scsi_done with %08lx\n", SCpnt->result))
	SCpnt->scsi_done(SCpnt);
}

void fcp_receive_solicited(fc_channel *fc, int proto, int token, int status, fc_hdr *fch)
{
	FCD(("receive_solicited %d %d %d\n", proto, token, status))
	switch (proto) {
	case TYPE_SCSI_FCP:
		fcp_scsi_receive(fc, token, status, fch); break;
	case TYPE_EXTENDED_LS:
		if (fc->ls && ((ls *)(fc->ls))->magic == LSMAGIC) {
			ls *l = (ls *)fc->ls;
			int i = (token >= l->count) ? token - l->count : token;

			/* Let's be sure */
			if ((unsigned)i < l->count && l->fcmds[i].fc == fc) {
				fcp_login_done(fc, token, status);
				break;
			}
		}
		break;
	case PROTO_OFFLINE:
		if (fc->ls && ((lso *)(fc->ls))->magic == LSOMAGIC) {
			lso *l = (lso *)fc->ls;

			if ((unsigned)token < l->count && l->fcmds[token].fc == fc) {
				/* Wow, OFFLINE response arrived :) */
				FCD(("OFFLINE Response arrived\n"))
				fc->state = FC_STATE_OFFLINE;
				if (atomic_dec_and_test (&l->todo))
					up(&l->sem);
			}
		}
		break;
		
	default:
		break;
	}
}

void fcp_state_change(fc_channel *fc, int state)
{
	FCD(("state_change %d %d\n", state, fc->state))
	if (state == FC_STATE_ONLINE && fc->state == FC_STATE_MAYBEOFFLINE)
		fc->state = FC_STATE_UNINITED;
	else if (state == FC_STATE_ONLINE)
		printk (KERN_WARNING "%s: state change to ONLINE\n", fc->name);
	else
		printk (KERN_ERR "%s: state change to OFFLINE\n", fc->name);
}

int fcp_initialize(fc_channel *fcchain, int count)
{
	fc_channel *fc;
	fcp_cmnd *fcmd;
	int i, retry, ret;
	ls *l;

	FCND(("fcp_inititialize %08lx\n", (long)fcp_init))
	FCND(("fc_channels %08lx\n", (long)fc_channels))
	FCND((" SID %d DID %d\n", fcchain->sid, fcchain->did))
	l = kmalloc(sizeof (ls) + count * sizeof(int), GFP_KERNEL);
	if (!l) {
		printk ("FC: Cannot allocate memory for initialization\n");
		return -ENOMEM;
	}
	memset (l, 0, sizeof(ls) + count * sizeof(int));
	l->magic = LSMAGIC;
	l->count = count;
	FCND(("FCP Init for %d channels\n", count))
	l->sem = MUTEX_LOCKED;
	l->timer.function = fcp_login_timeout;
	l->timer.data = (unsigned long)l;
	atomic_set (&l->todo, count);
	l->logi = kmalloc (count * 3 * sizeof(logi), GFP_DMA);
	l->fcmds = kmalloc (count * sizeof(fcp_cmnd), GFP_KERNEL);
	if (!l->logi || !l->fcmds) {
		if (l->logi) kfree (l->logi);
		if (l->fcmds) kfree (l->fcmds);
		kfree (l);
		printk ("FC: Cannot allocate DMA memory for initialization\n");
		return -ENOMEM;
	}
	memset (l->logi, 0, count * 3 * sizeof(logi));
	memset (l->fcmds, 0, count * sizeof(fcp_cmnd));
	FCND(("Initializing FLOGI packets\n"))
	for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++) {
		fc_hdr *fch;

		FCD(("SID %d DID %d\n", fc->sid, fc->did))
		fc->state = FC_STATE_UNINITED;
		fcmd = l->fcmds + i;
		fc->login = fcmd;
		fc->ls = (void *)l;
		fc->rst_pkt = NULL;	/* kmalloc when first used */
		fch = &fcmd->fch;
		FILL_FCHDR_RCTL_DID(fch, R_CTL_ELS_REQ, FS_FABRIC_F_PORT);
		FILL_FCHDR_SID(fch, 0);
		FILL_FCHDR_TYPE_FCTL(fch, TYPE_EXTENDED_LS, F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE);
		FILL_FCHDR_SEQ_DF_SEQ(fch, 0, 0, 0);
		FILL_FCHDR_OXRX(fch, 0xffff, 0xffff);
		fch->param = 0;
		l->logi [3 * i].code = LS_FLOGI;
		fcmd->cmd = fc_sync_dma_entry (l->logi + 3 * i, 3 * sizeof(logi), fc);
		fcmd->rsp = fcmd->cmd + sizeof(logi);
		fcmd->cmdlen = sizeof(logi);
		fcmd->rsplen = sizeof(logi);
		fcmd->data = (dma_handle)NULL;
		fcmd->class = FC_CLASS_SIMPLE;
		fcmd->proto = TYPE_EXTENDED_LS;
		fcmd->token = i;
		fcmd->fc = fc;
	}
	for (retry = 0; retry < 8; retry++) {
		FCND(("Sending FLOGI/PLOGI packets\n"))
		for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++) {
			if (fc->state == FC_STATE_ONLINE || fc->state == FC_STATE_OFFLINE)
				continue;
			disable_irq(fc->irq);
			if (fc->state == FC_STATE_MAYBEOFFLINE) {
				if (!l->grace[i]) {
					l->grace[i]++;
					FCD(("Grace\n"))
				} else {
					fc->state = FC_STATE_OFFLINE;
					enable_irq(fc->irq);
					fc_sync_dma_exit (l->fcmds[i].cmd, 3 * sizeof(logi), fc);
					if (atomic_dec_and_test (&l->todo))
						goto all_done;
				}
			}
			ret = fc->hw_enque (fc, fc->login);
			enable_irq(fc->irq);
			if (ret) printk ("FC: Cannot enque FLOGI packet on %s\n", fc->name);
		}
		
		l->timer.expires = jiffies + 5 * HZ;
		add_timer(&l->timer);

		down(&l->sem);
		if (!atomic_read(&l->todo)) {
			FCND(("All channels answered in time\n"))
			break; /* All fc channels have answered us */
		}
	}
all_done:
	for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i += 3) {
		switch (fc->state) {
		case FC_STATE_ONLINE: break;
		case FC_STATE_OFFLINE: break;
		default: fc_sync_dma_exit (l->fcmds[i].cmd, 3 * sizeof(logi), fc);
			break;
		}
		fc->ls = NULL;
	}
	del_timer(&l->timer);
	kfree (l->logi);
	kfree (l->fcmds);
	kfree (l);
	return 0;
}

int fcp_forceoffline(fc_channel *fcchain, int count)
{
	fc_channel *fc;
	fcp_cmnd *fcmd;
	int i, ret;
	lso l;

	memset (&l, 0, sizeof(lso));
	l.count = count;
	l.magic = LSOMAGIC;
	FCND(("FCP Force Offline for %d channels\n", count))
	l.sem = MUTEX_LOCKED;
	l.timer.function = fcp_login_timeout;
	l.timer.data = (unsigned long)&l;
	atomic_set (&l.todo, count);
	l.fcmds = kmalloc (count * sizeof(fcp_cmnd), GFP_KERNEL);
	if (!l.fcmds) {
		kfree (l.fcmds);
		printk ("FC: Cannot allocate memory for forcing offline\n");
		return -ENOMEM;
	}
	memset (l.fcmds, 0, count * sizeof(fcp_cmnd));
	FCND(("Initializing OFFLINE packets\n"))
	for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++) {
		fc->state = FC_STATE_UNINITED;
		fcmd = l.fcmds + i;
		fc->login = fcmd;
		fc->ls = (void *)&l;
		fcmd->class = FC_CLASS_OFFLINE;
		fcmd->proto = PROTO_OFFLINE;
		fcmd->token = i;
		fcmd->fc = fc;
		disable_irq(fc->irq);
		ret = fc->hw_enque (fc, fc->login);
		enable_irq(fc->irq);
		if (ret) printk ("FC: Cannot enque OFFLINE packet on %s\n", fc->name);
	}
		
	l.timer.expires = jiffies + 5 * HZ;
	add_timer(&l.timer);
	down(&l.sem);
	del_timer(&l.timer);
	
	kfree (l.fcmds);
	return 0;
}

int fcp_init(fc_channel *fcchain)
{
	fc_channel *fc;
	int count=0;
	int ret;
	
	for (fc = fcchain; fc; fc = fc->next) {
		fc->fcp_register = fcp_register;
		count++;
	}

	ret = fcp_initialize (fcchain, count);

	if (!ret) {	
		if (!fc_channels)
			fc_channels = fcchain;
		else {
			for (fc = fc_channels; fc->next; fc = fc->next);
			fc->next = fcchain;
		}
	}
	return ret;
}

void fcp_release(fc_channel *fcchain, int count)  /* count must > 0 */
{
	fc_channel *fc;
	fc_channel *fcx;

	for (fc = fcchain; --count && fc->next; fc = fc->next);
	if (count) {
		printk("FC: nothing to release\n");
		return;
	}
	
	if (fc_channels == fcchain)
		fc_channels = fc->next;
	else {
		for (fcx = fc_channels; fcx->next != fcchain; fcx = fcx->next);
		fcx->next = fc->next;
	}
	fc->next = NULL;

	/*
	 *  We've just grabbed fcchain out of the fc_channel list
	 *  and zero-terminated it, while destroying the count.
	 *
	 *  Freeing the fc's is the low level driver's responsibility.
	 */
}


static void fcp_scsi_done (Scsi_Cmnd *SCpnt)
{
	if (FCP_CMND(SCpnt)->done)
		FCP_CMND(SCpnt)->done(SCpnt);
}

static int fcp_scsi_queue_it(fc_channel *fc, Scsi_Cmnd *SCpnt, fcp_cmnd *fcmd, int prepare)
{
	long i;
	fcp_cmd *cmd;
	u32 fcp_cntl;
	if (prepare) {
		i = find_first_zero_bit (fc->scsi_bitmap, fc->scsi_bitmap_end);
		set_bit (i, fc->scsi_bitmap);
		fcmd->token = i;
		cmd = fc->scsi_cmd_pool + i;

		if (fc->encode_addr (SCpnt, cmd->fcp_addr)) {
			/* Invalid channel/id/lun and couldn't map it into fcp_addr */
			clear_bit (i, fc->scsi_bitmap);
			SCpnt->result = (DID_BAD_TARGET << 16);
			SCpnt->scsi_done(SCpnt);
			return 0;
		}
		fc->scsi_free--;
		fc->token_tab[fcmd->token] = fcmd;

		if (SCpnt->device->tagged_supported) {
			if (jiffies - fc->ages[SCpnt->channel * fc->targets + SCpnt->target] > (5 * 60 * HZ)) {
				fc->ages[SCpnt->channel * fc->targets + SCpnt->target] = jiffies;
				fcp_cntl = FCP_CNTL_QTYPE_ORDERED;
			} else
				fcp_cntl = FCP_CNTL_QTYPE_SIMPLE;
		} else
			fcp_cntl = FCP_CNTL_QTYPE_UNTAGGED;
		if (!SCpnt->request_bufflen && !SCpnt->use_sg) {
			cmd->fcp_cntl = fcp_cntl;
			fcmd->data = (dma_handle)NULL;
		} else {
			switch (SCpnt->cmnd[0]) {
			case WRITE_6:
			case WRITE_10:
			case WRITE_12:
				cmd->fcp_cntl = (FCP_CNTL_WRITE | fcp_cntl); break;
			default:
				cmd->fcp_cntl = (FCP_CNTL_READ | fcp_cntl); break;
			}
			if (!SCpnt->use_sg) {
				cmd->fcp_data_len = SCpnt->request_bufflen;
				fcmd->data = fc_sync_dma_entry ((char *)SCpnt->request_buffer,
								SCpnt->request_bufflen, fc);
			} else {
				struct scatterlist *sg = (struct scatterlist *)SCpnt->buffer;

				FCD(("XXX: Use_sg %d %d\n", SCpnt->use_sg, sg->length))
				if (SCpnt->use_sg > 1) printk ("%s: SG for use_sg > 1 not handled yet\n", fc->name);
				fc_sync_dma_entry_sg (sg, SCpnt->use_sg, fc);
				fcmd->data = sg->dvma_address;
				cmd->fcp_data_len = sg->length;
			}
		}
		memcpy (cmd->fcp_cdb, SCpnt->cmnd, SCpnt->cmd_len);
		memset (cmd->fcp_cdb+SCpnt->cmd_len, 0, sizeof(cmd->fcp_cdb)-SCpnt->cmd_len);
		FCD(("XXX: %04x.%04x.%04x.%04x - %08x%08x%08x\n", cmd->fcp_addr[0], cmd->fcp_addr[1], cmd->fcp_addr[2], cmd->fcp_addr[3], *(u32 *)SCpnt->cmnd, *(u32 *)(SCpnt->cmnd+4), *(u32 *)(SCpnt->cmnd+8)))
	}
	FCD(("Trying to enque %08x\n", (int)fcmd))
	if (!fc->scsi_que) {
		if (!fc->hw_enque (fc, fcmd)) {
			FCD(("hw_enque succeeded for %08x\n", (int)fcmd))
			return 0;
		}
	}
	FCD(("Putting into que1 %08x\n", (int)fcmd))
	fcp_scsi_insert_queue (fc, fcmd);
	return 0;
}

int fcp_scsi_queuecommand(Scsi_Cmnd *SCpnt, void (* done)(Scsi_Cmnd *))
{
	fcp_cmnd *fcmd = FCP_CMND(SCpnt);
	fc_channel *fc = FC_SCMND(SCpnt);
	
	FCD(("Entering SCSI queuecommand %08x\n", (int)fcmd))
	if (SCpnt->done != fcp_scsi_done) {
		fcmd->done = SCpnt->done;
		SCpnt->done = fcp_scsi_done;
		SCpnt->scsi_done = done;
		fcmd->proto = TYPE_SCSI_FCP;
		if (!fc->scsi_free) {
			FCD(("FC: !scsi_free, putting cmd on ML queue\n"))
#if (FCP_SCSI_USE_NEW_EH_CODE == 0)
			printk("fcp_scsi_queue_command: queue full, losing cmd, bad\n");
#endif
			return 1;
		}
		return fcp_scsi_queue_it(fc, SCpnt, fcmd, 1);
	}
	return fcp_scsi_queue_it(fc, SCpnt, fcmd, 0);
}

void fcp_queue_empty(fc_channel *fc)
{
	fcp_cmnd *fcmd;
	FCD(("Queue empty\n"))
	while ((fcmd = fc->scsi_que)) {
		/* The hw told us we can try again queue some packet */
		if (fc->hw_enque (fc, fcmd))
			return;
		fcp_scsi_remove_queue (fc, fcmd);
	}
}

int fcp_old_abort(Scsi_Cmnd *SCpnt)
{
	printk("FC: Abort not implemented\n");
	return 1;
}

int fcp_scsi_abort(Scsi_Cmnd *SCpnt)
{
	/* Internal bookkeeping only. Lose 1 token_tab slot. */
	fcp_cmnd *fcmd = FCP_CMND(SCpnt);
	fc_channel *fc = FC_SCMND(SCpnt);
	
	/*
	 * We react to abort requests by simply forgetting
	 * about the command and pretending everything's sweet.
	 * This may or may not be silly. We can't, however,
	 * immediately reuse the command's token_tab slot,
	 * as its result may arrive later and we cannot
	 * check whether it is the aborted one, can't we?
	 *
	 * Therefore, after the first few aborts are done,
	 * we tell the scsi error handler to do something clever.
	 * It will eventually call host reset, refreshing
	 * token_tab for us.
	 *
	 * There is a theoretical chance that we sometimes allow
	 * more than can_queue packets to the jungle this way,
	 * but the worst outcome possible is a series of
	 * more aborts and eventually the dev_reset catharsis.
	 */

	if (++fc->abort_count < (fc->can_queue >> 1)) {
		SCpnt->result = DID_ABORT;
		fcmd->done(SCpnt);
		printk("FC: soft abort\n");
		return SUCCESS;
	} else {
		printk("FC: hard abort refused\n");
		return FAILED;
	}
}

void fcp_scsi_reset_done(Scsi_Cmnd *SCpnt)
{
	fc_channel *fc = FC_SCMND(SCpnt);

	fc->rst_pkt->eh_state = SCSI_STATE_FINISHED;
	up(fc->rst_pkt->host->eh_action);
}

#define FCP_RESET_TIMEOUT (2*HZ)

int fcp_scsi_dev_reset(Scsi_Cmnd *SCpnt)
{
	fcp_cmd *cmd;
	fcp_cmnd *fcmd;
	fc_channel *fc = FC_SCMND(SCpnt);
        struct semaphore sem = MUTEX_LOCKED;

	if (!fc->rst_pkt) {
		fc->rst_pkt = (Scsi_Cmnd *) kmalloc(sizeof(SCpnt), GFP_KERNEL);
		if (!fc->rst_pkt) return FAILED;
		
		fcmd = FCP_CMND(fc->rst_pkt);


		fcmd->token = 0;
		cmd = fc->scsi_cmd_pool + 0;
		FCD(("Preparing rst packet\n"))
		if (fc->encode_addr (SCpnt, /*?*/cmd->fcp_addr))
		fc->rst_pkt->channel = SCpnt->channel;
		fc->rst_pkt->target = SCpnt->target;
		fc->rst_pkt->lun = 0;
		fc->rst_pkt->cmd_len = 0;
		
		fc->token_tab[0] = fcmd;

		cmd->fcp_cntl = FCP_CNTL_QTYPE_ORDERED | FCP_CNTL_RESET;
		fcmd->data = (dma_handle)NULL;
		fcmd->proto = TYPE_SCSI_FCP;

		memcpy (cmd->fcp_cdb, SCpnt->cmnd, SCpnt->cmd_len);
		memset (cmd->fcp_cdb+SCpnt->cmd_len, 0, sizeof(cmd->fcp_cdb)-SCpnt->cmd_len);
		FCD(("XXX: %04x.%04x.%04x.%04x - %08x%08x%08x\n", cmd->fcp_addr[0], cmd->fcp_addr[1], cmd->fcp_addr[2], cmd->fcp_addr[3], *(u32 *)SCpnt->cmnd, *(u32 *)(SCpnt->cmnd+4), *(u32 *)(SCpnt->cmnd+8)))
	} else {
		fcmd = FCP_CMND(fc->rst_pkt);
		if (fc->rst_pkt->eh_state == SCSI_STATE_QUEUED)
			return FAILED; /* or SUCCESS. Only these */
	}
	fc->rst_pkt->done = NULL;


        fc->rst_pkt->eh_state = SCSI_STATE_QUEUED;

	fc->rst_pkt->eh_timeout.data = (unsigned long) fc->rst_pkt;
	fc->rst_pkt->eh_timeout.expires = jiffies + FCP_RESET_TIMEOUT;
	fc->rst_pkt->eh_timeout.function = (void (*)(unsigned long))fcp_scsi_reset_done;

        add_timer(&fc->rst_pkt->eh_timeout);

	/*
	 * Set up the semaphore so we wait for the command to complete.
	 */

	fc->rst_pkt->host->eh_action = &sem;
	fc->rst_pkt->request.rq_status = RQ_SCSI_BUSY;

	fc->rst_pkt->done = fcp_scsi_reset_done;
	fcp_scsi_queue_it(fc, fc->rst_pkt, fcmd, 0);
	
	down(&sem);

	fc->rst_pkt->host->eh_action = NULL;
	del_timer(&fc->rst_pkt->eh_timeout);

	/*
	 * See if timeout.  If so, tell the host to forget about it.
	 * In other words, we don't want a callback any more.
	 */
	if (fc->rst_pkt->eh_state == SCSI_STATE_TIMEOUT ) {
		fc->rst_pkt->eh_state = SCSI_STATE_UNUSED;
		return FAILED;
	}
	fc->rst_pkt->eh_state = SCSI_STATE_UNUSED;
	return SUCCESS;
}

int fcp_scsi_bus_reset(Scsi_Cmnd *SCpnt)
{
	printk ("FC: bus reset!\n");
	return FAILED;
}

int fcp_scsi_host_reset(Scsi_Cmnd *SCpnt)
{
	fc_channel *fc = FC_SCMND(SCpnt);
	fcp_cmnd *fcmd = FCP_CMND(SCpnt);
	int i;

	printk ("FC: host reset\n");

	for (i=0; i < fc->can_queue; i++) {
		if (fc->token_tab[i] && SCpnt->result != DID_ABORT) {
			SCpnt->result = DID_RESET;
			fcmd->done(SCpnt);
			fc->token_tab[i] = NULL;
		}
	}
	fc->reset(fc);
	fc->abort_count = 0;
	if (fcp_initialize(fc, 1)) return SUCCESS;
	else return FAILED;
}

#ifdef MODULE
int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}
#endif

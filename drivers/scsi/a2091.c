#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blk.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>
#include <asm/irq.h>
#include <asm/spinlock.h>

#include "scsi.h"
#include "hosts.h"
#include "wd33c93.h"
#include "a2091.h"

#include<linux/stat.h>

struct proc_dir_entry proc_scsi_a2091 = {
    PROC_SCSI_A2091, 5, "A2091",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};

#define DMA(ptr) ((a2091_scsiregs *)((ptr)->base))
#define HDATA(ptr) ((struct WD33C93_hostdata *)((ptr)->hostdata))

static struct Scsi_Host *first_instance = NULL;
static Scsi_Host_Template *a2091_template;

static void a2091_intr (int irq, void *dummy, struct pt_regs *fp)
{
    unsigned int status;
    struct Scsi_Host *instance;
    for (instance = first_instance; instance &&
	 instance->hostt == a2091_template; instance = instance->next)
    {
	status = DMA(instance)->ISTR;
	if (!(status & (ISTR_INT_F|ISTR_INT_P)))
	    continue;

	if (status & ISTR_INTS)
	{
	    /* disable PORTS interrupt */
	    custom.intena = IF_PORTS;
	    wd33c93_intr (instance);
	    /* enable PORTS interrupt */
	    custom.intena = IF_SETCLR | IF_PORTS;
	}
    }
}

static void do_a2091_intr (int irq, void *dummy, struct pt_regs *fp)
{
    unsigned long flags;

    spin_lock_irqsave(&io_request_lock, flags);
    a2091_intr(irq, dummy, fp);
    spin_unlock_irqrestore(&io_request_lock, flags);
}

static int dma_setup (Scsi_Cmnd *cmd, int dir_in)
{
    unsigned short cntr = CNTR_PDMD | CNTR_INTEN;
    unsigned long addr = VTOP(cmd->SCp.ptr);
    struct Scsi_Host *instance = cmd->host;

    /* don't allow DMA if the physical address is bad */
    if (addr & A2091_XFER_MASK ||
	(!dir_in && mm_end_of_chunk (addr, cmd->SCp.this_residual)))
    {
	HDATA(instance)->dma_bounce_len = (cmd->SCp.this_residual + 511)
	    & ~0x1ff;
	HDATA(instance)->dma_bounce_buffer =
	    scsi_malloc (HDATA(instance)->dma_bounce_len);
	
	/* can't allocate memory; use PIO */
	if (!HDATA(instance)->dma_bounce_buffer) {
	    HDATA(instance)->dma_bounce_len = 0;
	    return 1;
	}

	/* get the physical address of the bounce buffer */
	addr = VTOP(HDATA(instance)->dma_bounce_buffer);

	/* the bounce buffer may not be in the first 16M of physmem */
	if (addr & A2091_XFER_MASK) {
	    /* we could use chipmem... maybe later */
	    scsi_free (HDATA(instance)->dma_bounce_buffer,
		       HDATA(instance)->dma_bounce_len);
	    HDATA(instance)->dma_bounce_buffer = NULL;
	    HDATA(instance)->dma_bounce_len = 0;
	    return 1;
	}

	if (!dir_in) {
	    /* copy to bounce buffer for a write */
	    if (cmd->use_sg)
#if 0
		panic ("scsi%ddma: incomplete s/g support",
		       instance->host_no);
#else
		memcpy (HDATA(instance)->dma_bounce_buffer,
			cmd->SCp.ptr, cmd->SCp.this_residual);
#endif
	    else
		memcpy (HDATA(instance)->dma_bounce_buffer,
			cmd->request_buffer, cmd->request_bufflen);
	}
    }

    /* setup dma direction */
    if (!dir_in)
	cntr |= CNTR_DDIR;

    /* remember direction */
    HDATA(cmd->host)->dma_dir = dir_in;

    DMA(cmd->host)->CNTR = cntr;

    /* setup DMA *physical* address */
    DMA(cmd->host)->ACR = addr;

    if (dir_in){
	/* invalidate any cache */
	cache_clear (addr, cmd->SCp.this_residual);
    }else{
	/* push any dirty cache */
	cache_push (addr, cmd->SCp.this_residual);
      }
    /* start DMA */
    DMA(cmd->host)->ST_DMA = 1;

    /* return success */
    return 0;
}

static void dma_stop (struct Scsi_Host *instance, Scsi_Cmnd *SCpnt, 
		      int status)
{
    /* disable SCSI interrupts */
    unsigned short cntr = CNTR_PDMD;

    if (!HDATA(instance)->dma_dir)
	    cntr |= CNTR_DDIR;

    /* disable SCSI interrupts */
    DMA(instance)->CNTR = cntr;

    /* flush if we were reading */
    if (HDATA(instance)->dma_dir) {
	DMA(instance)->FLUSH = 1;
	while (!(DMA(instance)->ISTR & ISTR_FE_FLG))
	    ;
    }

    /* clear a possible interrupt */
    DMA(instance)->CINT = 1;

    /* stop DMA */
    DMA(instance)->SP_DMA = 1;

    /* restore the CONTROL bits (minus the direction flag) */
    DMA(instance)->CNTR = CNTR_PDMD | CNTR_INTEN;

    /* copy from a bounce buffer, if necessary */
    if (status && HDATA(instance)->dma_bounce_buffer) {
	if (SCpnt && SCpnt->use_sg) {
#if 0
	    panic ("scsi%d: incomplete s/g support",
		   instance->host_no);
#else
	    if( HDATA(instance)->dma_dir )
		memcpy (SCpnt->SCp.ptr, 
			HDATA(instance)->dma_bounce_buffer,
			SCpnt->SCp.this_residual);
	    scsi_free (HDATA(instance)->dma_bounce_buffer,
		       HDATA(instance)->dma_bounce_len);
	    HDATA(instance)->dma_bounce_buffer = NULL;
	    HDATA(instance)->dma_bounce_len = 0;
	    
#endif
	} else {
	    if (HDATA(instance)->dma_dir && SCpnt)
		memcpy (SCpnt->request_buffer,
			HDATA(instance)->dma_bounce_buffer,
			SCpnt->request_bufflen);

	    scsi_free (HDATA(instance)->dma_bounce_buffer,
		       HDATA(instance)->dma_bounce_len);
	    HDATA(instance)->dma_bounce_buffer = NULL;
	    HDATA(instance)->dma_bounce_len = 0;
	}
    }
}

static int num_a2091 = 0;

__initfunc(int a2091_detect(Scsi_Host_Template *tpnt))
{
    static unsigned char called = 0;
    struct Scsi_Host *instance;
    caddr_t address;
    unsigned int key;
    const struct ConfigDev *cd;

    if (!MACH_IS_AMIGA || called)
	return 0;
    called = 1;

    tpnt->proc_dir = &proc_scsi_a2091;
    tpnt->proc_info = &wd33c93_proc_info;

    while ((key = zorro_find(ZORRO_PROD_CBM_A590_A2091_1, 0, 0)) ||
	   (key = zorro_find(ZORRO_PROD_CBM_A590_A2091_2, 0, 0))) {
	cd = zorro_get_board(key);
	address = cd->cd_BoardAddr;
	instance = scsi_register (tpnt, sizeof (struct WD33C93_hostdata));
	instance->base = (unsigned char *)ZTWO_VADDR(address);
	instance->irq = IRQ_AMIGA_PORTS;
	instance->unique_id = key;
	DMA(instance)->DAWR = DAWR_A2091;
	wd33c93_init(instance, (wd33c93_regs *)&(DMA(instance)->SASR),
		     dma_setup, dma_stop, WD33C93_FS_8_10);
	if (num_a2091++ == 0) {
	    first_instance = instance;
	    a2091_template = instance->hostt;
	    request_irq(IRQ_AMIGA_PORTS, do_a2091_intr, 0, "A2091 SCSI", a2091_intr);
	}
	DMA(instance)->CNTR = CNTR_PDMD | CNTR_INTEN;
	zorro_config_board(key, 0);
    }

    return num_a2091;
}

#ifdef MODULE

#define HOSTS_C

#include "a2091.h"

Scsi_Host_Template driver_template = A2091_SCSI;

#include "scsi_module.c"

#endif

int a2091_release(struct Scsi_Host *instance)
{
#ifdef MODULE
	DMA(instance)->CNTR = 0;
	zorro_unconfig_board(instance->unique_id, 0);
	if (--num_a2091 == 0)
		free_irq(IRQ_AMIGA_PORTS, a2091_intr);
	wd33c93_release();
#endif
	return 1;
}

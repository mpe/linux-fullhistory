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
#include "gvp11.h"

#include<linux/stat.h>

struct proc_dir_entry proc_scsi_gvp11 = {
    PROC_SCSI_GVP11, 5, "GVP11",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};

#define DMA(ptr) ((gvp11_scsiregs *)((ptr)->base))
#define HDATA(ptr) ((struct WD33C93_hostdata *)((ptr)->hostdata))

static struct Scsi_Host *first_instance = NULL;
static Scsi_Host_Template *gvp11_template;

static void gvp11_intr (int irq, void *dummy, struct pt_regs *fp)
{
    unsigned int status;
    struct Scsi_Host *instance;

    for (instance = first_instance; instance &&
	 instance->hostt == gvp11_template; instance = instance->next)
    {
	status = DMA(instance)->CNTR;
	if (!(status & GVP11_DMAC_INT_PENDING))
	    continue;

	/* disable PORTS interrupt */
	custom.intena = IF_PORTS;
	wd33c93_intr (instance);
	/* enable PORTS interrupt */
	custom.intena = IF_SETCLR | IF_PORTS;
    }
}

static void do_gvp11_intr (int irq, void *dummy, struct pt_regs *fp)
{
    unsigned long flags;

    spin_lock_irqsave(&io_request_lock, flags);
    gvp11_intr(irq, dummy, fp);
    spin_unlock_irqrestore(&io_request_lock, flags);
}

static int gvp11_xfer_mask = 0;

void gvp11_setup (char *str, int *ints)
{
    gvp11_xfer_mask = ints[1];
}

static int dma_setup (Scsi_Cmnd *cmd, int dir_in)
{
    unsigned short cntr = GVP11_DMAC_INT_ENABLE;
    unsigned long addr = VTOP(cmd->SCp.ptr);
    int bank_mask;

    /* don't allow DMA if the physical address is bad */
    if (addr & HDATA(cmd->host)->dma_xfer_mask ||
	(!dir_in && mm_end_of_chunk (addr, cmd->SCp.this_residual)))
    {
	HDATA(cmd->host)->dma_bounce_len = (cmd->SCp.this_residual + 511)
	    & ~0x1ff;
	HDATA(cmd->host)->dma_bounce_buffer =
	    scsi_malloc (HDATA(cmd->host)->dma_bounce_len);
	HDATA(cmd->host)->dma_buffer_pool = BUF_SCSI_ALLOCED;
	
	if (!HDATA(cmd->host)->dma_bounce_buffer) {
	    HDATA(cmd->host)->dma_bounce_buffer =
		amiga_chip_alloc(HDATA(cmd->host)->dma_bounce_len);

	    if(!HDATA(cmd->host)->dma_bounce_buffer)
	    {
		HDATA(cmd->host)->dma_bounce_len = 0;
		return 1;
	    }

	    HDATA(cmd->host)->dma_buffer_pool = BUF_CHIP_ALLOCED;
	}

	/* check if the address of the bounce buffer is OK */
	addr = VTOP(HDATA(cmd->host)->dma_bounce_buffer);

	if (addr & HDATA(cmd->host)->dma_xfer_mask) {
	    /* fall back to Chip RAM if address out of range */
	    if( HDATA(cmd->host)->dma_buffer_pool == BUF_SCSI_ALLOCED)
		scsi_free (HDATA(cmd->host)->dma_bounce_buffer,
			   HDATA(cmd->host)->dma_bounce_len);
	    else
		amiga_chip_free (HDATA(cmd->host)->dma_bounce_buffer);
		
	    HDATA(cmd->host)->dma_bounce_buffer =
		amiga_chip_alloc(HDATA(cmd->host)->dma_bounce_len);

	    if(!HDATA(cmd->host)->dma_bounce_buffer)
	    {
		HDATA(cmd->host)->dma_bounce_len = 0;
		return 1;
	    }

	    addr = VTOP(HDATA(cmd->host)->dma_bounce_buffer);
	    HDATA(cmd->host)->dma_buffer_pool = BUF_CHIP_ALLOCED;
	}
	    
	if (!dir_in) {
	    /* copy to bounce buffer for a write */
	    if (cmd->use_sg)
		memcpy (HDATA(cmd->host)->dma_bounce_buffer,
				cmd->SCp.ptr, cmd->SCp.this_residual);
	    else
		memcpy (HDATA(cmd->host)->dma_bounce_buffer,
			cmd->request_buffer, cmd->request_bufflen);
	}
    }

    /* setup dma direction */
    if (!dir_in)
	cntr |= GVP11_DMAC_DIR_WRITE;

    HDATA(cmd->host)->dma_dir = dir_in;
    DMA(cmd->host)->CNTR = cntr;

    /* setup DMA *physical* address */
    DMA(cmd->host)->ACR = addr;

    if (dir_in)
	/* invalidate any cache */
	cache_clear (addr, cmd->SCp.this_residual);
    else
	/* push any dirty cache */
	cache_push (addr, cmd->SCp.this_residual);

    if ((bank_mask = (~HDATA(cmd->host)->dma_xfer_mask >> 18) & 0x01c0))
	    DMA(cmd->host)->BANK = bank_mask & (addr >> 18);

    /* start DMA */
    DMA(cmd->host)->ST_DMA = 1;

    /* return success */
    return 0;
}

static void dma_stop (struct Scsi_Host *instance, Scsi_Cmnd *SCpnt,
		      int status)
{
    /* stop DMA */
    DMA(instance)->SP_DMA = 1;
    /* remove write bit from CONTROL bits */
    DMA(instance)->CNTR = GVP11_DMAC_INT_ENABLE;

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

	    if (HDATA(instance)->dma_buffer_pool == BUF_SCSI_ALLOCED)
		scsi_free (HDATA(instance)->dma_bounce_buffer,
			    HDATA(instance)->dma_bounce_len);
	    else
		amiga_chip_free(HDATA(instance)->dma_bounce_buffer);

	    HDATA(instance)->dma_bounce_buffer = NULL;
	    HDATA(instance)->dma_bounce_len = 0;
	    
#endif
	} else {
	    if (HDATA(instance)->dma_dir && SCpnt)
		memcpy (SCpnt->request_buffer,
			HDATA(instance)->dma_bounce_buffer,
			SCpnt->request_bufflen);

	    if (HDATA(instance)->dma_buffer_pool == BUF_SCSI_ALLOCED)
		scsi_free (HDATA(instance)->dma_bounce_buffer,
			    HDATA(instance)->dma_bounce_len);
	    else
		amiga_chip_free(HDATA(instance)->dma_bounce_buffer);

	    HDATA(instance)->dma_bounce_buffer = NULL;
	    HDATA(instance)->dma_bounce_len = 0;
	}
    }
}

static int num_gvp11 = 0;

#define CHECK_WD33C93

__initfunc(int gvp11_detect(Scsi_Host_Template *tpnt))
{
    static unsigned char called = 0;
    struct Scsi_Host *instance;
    caddr_t address;
    unsigned int epc;
    unsigned int key = 0, skey;
    const struct ConfigDev *cd;
    unsigned int default_dma_xfer_mask;
#ifdef CHECK_WD33C93
    volatile unsigned char *sasr_3393, *scmd_3393;
    unsigned char save_sasr;
    unsigned char q, qq;
#endif

    if (!MACH_IS_AMIGA || called)
	return 0;
    called = 1;

    tpnt->proc_dir = &proc_scsi_gvp11;
    tpnt->proc_info = &wd33c93_proc_info;

    while (1) {
	/* 
	 * This should (hopefully) be the correct way to identify
	 * all the different GVP SCSI controllers (except for the
	 * SERIES I though).
	 */
	skey = key;

	if ((key = zorro_find(ZORRO_PROD_GVP_COMBO_030_R3_SCSI, 0, skey)) ||
	    (key = zorro_find(ZORRO_PROD_GVP_SERIES_II, 0, skey)))
	    default_dma_xfer_mask = ~0x00ffffff;
	else if ((key = zorro_find(ZORRO_PROD_GVP_GFORCE_030_SCSI, 0, skey)) ||
		 (key = zorro_find(ZORRO_PROD_GVP_A530_SCSI, 0, skey)) ||
		 (key = zorro_find(ZORRO_PROD_GVP_COMBO_030_R4_SCSI, 0, skey)))
	    default_dma_xfer_mask = ~0x01ffffff;
	else if ((key = zorro_find(ZORRO_PROD_GVP_A1291, 0, skey)) ||
		 (key = zorro_find(ZORRO_PROD_GVP_GFORCE_040_SCSI_1, 0, skey)))
	    default_dma_xfer_mask = ~0x07ffffff;
	else
	    break;

	cd = zorro_get_board(key);
	address = cd->cd_BoardAddr;

	/*
	 * Rumors state that some GVP ram boards use the same product
	 * code as the SCSI controllers. Therefore if the board-size
	 * is not 64KB we asume it is a ram board and bail out.
	 */
	if (cd->cd_BoardSize != 0x10000)
		continue;

#ifdef CHECK_WD33C93

	/*
	 * These darn GVP boards are a problem - it can be tough to tell
	 * whether or not they include a SCSI controller. This is the
	 * ultimate Yet-Another-GVP-Detection-Hack in that it actually
	 * probes for a WD33c93 chip: If we find one, it's extremely
	 * likely that this card supports SCSI, regardless of Product_
	 * Code, Board_Size, etc. 
	 */

    /* Get pointers to the presumed register locations and save contents */

	sasr_3393 = &(((gvp11_scsiregs *)(ZTWO_VADDR(address)))->SASR);
	scmd_3393 = &(((gvp11_scsiregs *)(ZTWO_VADDR(address)))->SCMD);
	save_sasr = *sasr_3393;

    /* First test the AuxStatus Reg */

	q = *sasr_3393;		/* read it */
	if (q & 0x08)		/* bit 3 should always be clear */
		continue;
	*sasr_3393 = WD_AUXILIARY_STATUS;	 /* setup indirect address */
	if (*sasr_3393 == WD_AUXILIARY_STATUS) { /* shouldn't retain the write */
		*sasr_3393 = save_sasr;	/* Oops - restore this byte */
		continue;
		}
	if (*sasr_3393 != q) {	/* should still read the same */
		*sasr_3393 = save_sasr;	/* Oops - restore this byte */
		continue;
		}
	if (*scmd_3393 != q)	/* and so should the image at 0x1f */
		continue;


    /* Ok, we probably have a wd33c93, but let's check a few other places
     * for good measure. Make sure that this works for both 'A and 'B    
     * chip versions.
     */

	*sasr_3393 = WD_SCSI_STATUS;
	q = *scmd_3393;
	*sasr_3393 = WD_SCSI_STATUS;
	*scmd_3393 = ~q;
	*sasr_3393 = WD_SCSI_STATUS;
	qq = *scmd_3393;
	*sasr_3393 = WD_SCSI_STATUS;
	*scmd_3393 = q;
	if (qq != q)			/* should be read only */
		continue;
	*sasr_3393 = 0x1e;	/* this register is unimplemented */
	q = *scmd_3393;
	*sasr_3393 = 0x1e;
	*scmd_3393 = ~q;
	*sasr_3393 = 0x1e;
	qq = *scmd_3393;
	*sasr_3393 = 0x1e;
	*scmd_3393 = q;
	if (qq != q || qq != 0xff)	/* should be read only, all 1's */
		continue;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	q = *scmd_3393;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	*scmd_3393 = ~q;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	qq = *scmd_3393;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	*scmd_3393 = q;
	if (qq != (~q & 0xff))		/* should be read/write */
		continue;
#endif

	instance = scsi_register (tpnt, sizeof (struct WD33C93_hostdata));
	instance->base = (unsigned char *)ZTWO_VADDR(address);
	instance->irq = IRQ_AMIGA_PORTS;
	instance->unique_id = key;

	if (gvp11_xfer_mask)
		HDATA(instance)->dma_xfer_mask = gvp11_xfer_mask;
	else
		HDATA(instance)->dma_xfer_mask = default_dma_xfer_mask;


	DMA(instance)->secret2 = 1;
	DMA(instance)->secret1 = 0;
	DMA(instance)->secret3 = 15;
	while (DMA(instance)->CNTR & GVP11_DMAC_BUSY) ;
	DMA(instance)->CNTR = 0;

	DMA(instance)->BANK = 0;

	epc = *(unsigned short *)(ZTWO_VADDR(address) + 0x8000);

	/*
	 * Check for 14MHz SCSI clock
	 */
	if (epc & GVP_SCSICLKMASK)
		wd33c93_init(instance, (wd33c93_regs *)&(DMA(instance)->SASR),
			     dma_setup, dma_stop, WD33C93_FS_8_10);
	else
		wd33c93_init(instance, (wd33c93_regs *)&(DMA(instance)->SASR),
			     dma_setup, dma_stop, WD33C93_FS_12_15);

	if (num_gvp11++ == 0) {
		first_instance = instance;
		gvp11_template = instance->hostt;
		request_irq(IRQ_AMIGA_PORTS, do_gvp11_intr, 0,
			    "GVP11 SCSI", gvp11_intr);
	}
	DMA(instance)->CNTR = GVP11_DMAC_INT_ENABLE;
	zorro_config_board(key, 0);
    }

    return num_gvp11;
}


#ifdef MODULE

#define HOSTS_C

#include "gvp11.h"

Scsi_Host_Template driver_template = GVP11_SCSI;

#include "scsi_module.c"

#endif

int gvp11_release(struct Scsi_Host *instance)
{
#ifdef MODULE
    DMA(instance)->CNTR = 0;
    zorro_unconfig_board(instance->unique_id, 0);
    if (--num_gvp11 == 0)
	    free_irq(IRQ_AMIGA_PORTS, gvp11_intr);
    wd33c93_release();
#endif
    return 1;
}

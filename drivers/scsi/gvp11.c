#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blk.h>
#include <linux/version.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/zorro.h>
#include <asm/irq.h>

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

static void gvp11_intr (int irq, struct pt_regs *fp, void *dummy)
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

/*
 * DMA transfer mask for GVP Series II SCSI controller.
 * Some versions can only DMA into the 24 bit address space
 * (0->16M).  Others can DMA into the full 32 bit address
 * space.  The default is to only allow DMA into the 24 bit
 * address space.  The "gvp11=0xFFFFFFFE" setup parameter can
 * be supplied to force an alternate (32 bit) mask.
 */
static int gvp11_xfer_mask = GVP11_XFER_MASK;

void gvp11_setup (char *str, int *ints)
{
    gvp11_xfer_mask = ints[1];
}

static int dma_setup (Scsi_Cmnd *cmd, int dir_in)
{
    unsigned short cntr = GVP11_DMAC_INT_ENABLE;
    unsigned long addr = VTOP(cmd->SCp.ptr);

    /* don't allow DMA if the physical address is bad */
    if (addr & gvp11_xfer_mask ||
	(!dir_in && mm_end_of_chunk (addr, cmd->SCp.this_residual)))
    {
	HDATA(cmd->host)->dma_bounce_len = (cmd->SCp.this_residual + 511)
	    & ~0x1ff;
	HDATA(cmd->host)->dma_bounce_buffer =
	    scsi_malloc (HDATA(cmd->host)->dma_bounce_len);
	
	/* can't allocate memory; use PIO */
	if (!HDATA(cmd->host)->dma_bounce_buffer) {
	    HDATA(cmd->host)->dma_bounce_len = 0;
	    return 1;
	}

	/* check if the address of the bounce buffer is OK */
	addr = VTOP(HDATA(cmd->host)->dma_bounce_buffer);

	if (addr & gvp11_xfer_mask) {
	    scsi_free (HDATA(cmd->host)->dma_bounce_buffer,
		       HDATA(cmd->host)->dma_bounce_len);
	    HDATA(cmd->host)->dma_bounce_buffer = NULL;
	    HDATA(cmd->host)->dma_bounce_len = 0;
	    return 1;
	}
	    
	if (!dir_in) {
	    /* copy to bounce buffer for a write */
	    if (cmd->use_sg)
#if 0
		panic ("scsi%ddma: incomplete s/g support",
		       cmd->host->host_no);
#else
		memcpy (HDATA(cmd->host)->dma_bounce_buffer,
				cmd->SCp.ptr, cmd->SCp.this_residual);
#endif
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

int gvp11_detect(Scsi_Host_Template *tpnt)
{
    static unsigned char called = 0;
    struct Scsi_Host *instance;
    int i, manuf, product, num_gvp11 = 0;
    caddr_t address;
    enum GVP_ident epc;

    if (!MACH_IS_AMIGA || called)
	return 0;
    called = 1;

    tpnt->proc_dir = &proc_scsi_gvp11;

    for (i = 0; i < boot_info.bi_amiga.num_autocon; i++)
    {
	manuf = boot_info.bi_amiga.autocon[i].cd_Rom.er_Manufacturer;
	product = boot_info.bi_amiga.autocon[i].cd_Rom.er_Product;
#if 0
/* this seems to catch some non HD boards.  GVP is sooooo stupid */
	if (manuf == MANUF_GVP &&
	    ((product == PROD_GVPIISCSI) || (product == PROD_GVPIISCSI_2))) {
#else
        if (manuf == MANUF_GVP && product == PROD_GVPIISCSI) {
#endif
	    /* check extended product code */
	    address = boot_info.bi_amiga.autocon[i].cd_BoardAddr;
	    epc = *(unsigned short *)(ZTWO_VADDR(address) + 0x8000);

	    epc = epc & GVP_EPCMASK;

	    /* 
	     * This should (hopefully) be the correct way to identify
	     * all the different GVP SCSI controllers (except for the
	     * SERIES I though).
	     */
	    if (!((epc == GVP_A1291_SCSI) || 
		  (epc == GVP_GFORCE_040_SCSI) ||
		  (epc == GVP_GFORCE_030_SCSI) ||
		  (epc == GVP_A530_SCSI) ||
		  (epc == GVP_COMBO_R4_SCSI) ||
		  (epc == GVP_COMBO_R3_SCSI) ||
		  (epc == GVP_SERIESII)))
		continue;

	    instance = scsi_register (tpnt,
				      sizeof (struct WD33C93_hostdata));
	    instance->base = (unsigned char *)ZTWO_VADDR(address);
	    DMA(instance)->secret2 = 1;
	    DMA(instance)->secret1 = 0;
	    DMA(instance)->secret3 = 15;
	    while (DMA(instance)->CNTR & GVP11_DMAC_BUSY) ;
	    DMA(instance)->CNTR = 0;
	    wd33c93_init(instance, (wd33c93_regs *)&(DMA(instance)->SASR),
			 dma_setup, dma_stop, WD33C93_FS_8_10);
	    if (num_gvp11++ == 0) {
		first_instance = instance;
		gvp11_template = instance->hostt;
		add_isr(IRQ_AMIGA_PORTS, gvp11_intr, 0, NULL, "GVP11 SCSI");
	    }
	    DMA(instance)->CNTR = GVP11_DMAC_INT_ENABLE;

#if 0 /* The Zorro stuff is not totally integrated yet ! */
	    boot_info.bi_amiga.autocon_configured |= 1<<i;
#endif

	}
    }

    return num_gvp11;
}

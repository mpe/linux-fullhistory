/*
 * dec_esp.c: Driver for SCSI chips on IOASIC based TURBOchannel DECstations
 *
 * TURBOchannel changes by Harald Koerfgen
 *
 * based on jazz_esp.c:
 * Copyright (C) 1997 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 *
 * jazz_esp is based on David S. Miller's ESP driver and cyber_esp
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/blk.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include "scsi.h"
#include "hosts.h"
#include "NCR53C9x.h"
#include "dec_esp.h"

#include <asm/irq.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>
#include <asm/dma.h>

#include <asm/pgtable.h>

#include <asm/dec/tc.h>
#include <asm/dec/interrupts.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/ioasic_ints.h>
#include <asm/dec/machtype.h>

static int  dma_bytes_sent(struct NCR_ESP *esp, int fifo_count);
static void dma_drain(struct NCR_ESP *esp);
static int dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd * sp);
static void dma_dump_state(struct NCR_ESP *esp);
static void dma_init_read(struct NCR_ESP *esp, __u32 vaddress, int length);
static void dma_init_write(struct NCR_ESP *esp, __u32 vaddress, int length);
static void dma_ints_off(struct NCR_ESP *esp);
static void dma_ints_on(struct NCR_ESP *esp);
static int  dma_irq_p(struct NCR_ESP *esp);
static int  dma_ports_p(struct NCR_ESP *esp);
static void dma_setup(struct NCR_ESP *esp, __u32 addr, int count, int write);
static void dma_mmu_get_scsi_one(struct NCR_ESP *esp, Scsi_Cmnd * sp);
static void dma_mmu_get_scsi_sgl(struct NCR_ESP *esp, Scsi_Cmnd * sp);
static void dma_advance_sg(Scsi_Cmnd * sp);


volatile unsigned char cmd_buffer[16];
				/* This is where all commands are put
				 * before they are trasfered to the ESP chip
				 * via PIO.
				 */

volatile unsigned long *scsi_dma_ptr;
volatile unsigned long *scsi_next_ptr;
volatile unsigned long *scsi_scr;
volatile unsigned long *ioasic_ssr;
volatile unsigned long *scsi_sdr0;
volatile unsigned long *scsi_sdr1;

static void scsi_dma_int(int, void *, struct pt_regs *);

/***************************************************************** Detection */
int dec_esp_detect(Scsi_Host_Template * tpnt)
{
    struct NCR_ESP *esp;
    struct ConfigDev *esp_dev;

	if (IOASIC) {
	esp_dev = 0;
	esp = esp_allocate(tpnt, (void *) esp_dev);
	
		scsi_dma_ptr = (unsigned long *) (system_base + IOCTL + SCSI_DMA_P);
		scsi_next_ptr = (unsigned long *) (system_base + IOCTL + SCSI_DMA_BP);
		scsi_scr = (unsigned long *) (system_base + IOCTL + SCSI_SCR);
		ioasic_ssr = (unsigned long *) (system_base + IOCTL + SSR);
		scsi_sdr0 = (unsigned long *) (system_base + IOCTL + SCSI_SDR0);
		scsi_sdr1 = (unsigned long *) (system_base + IOCTL + SCSI_SDR1);

	/* Do command transfer with programmed I/O */
	esp->do_pio_cmds = 1;
	
	/* Required functions */
	esp->dma_bytes_sent = &dma_bytes_sent;
	esp->dma_can_transfer = &dma_can_transfer;
	esp->dma_dump_state = &dma_dump_state;
	esp->dma_init_read = &dma_init_read;
	esp->dma_init_write = &dma_init_write;
	esp->dma_ints_off = &dma_ints_off;
	esp->dma_ints_on = &dma_ints_on;
	esp->dma_irq_p = &dma_irq_p;
	esp->dma_ports_p = &dma_ports_p;
	esp->dma_setup = &dma_setup;

	/* Optional functions */
	esp->dma_barrier = 0;
		esp->dma_drain = &dma_drain;
	esp->dma_invalidate = 0;
	esp->dma_irq_entry = 0;
	esp->dma_irq_exit = 0;
	esp->dma_poll = 0;
	esp->dma_reset = 0;
	esp->dma_led_off = 0;
	esp->dma_led_on = 0;
	
	/* virtual DMA functions */
	esp->dma_mmu_get_scsi_one = &dma_mmu_get_scsi_one;
	esp->dma_mmu_get_scsi_sgl = &dma_mmu_get_scsi_sgl;
		esp->dma_mmu_release_scsi_one = 0;
		esp->dma_mmu_release_scsi_sgl = 0;
	esp->dma_advance_sg = &dma_advance_sg;


	/* SCSI chip speed */
		esp->cfreq = 25000000;

	/* 
	 * we don't give the address of DMA channel, but the number
	 * of DMA channel, so we can use the jazz DMA functions
	 * 
	 */
	esp->dregs = JAZZ_SCSI_DMA;
	
	/* ESP register base */
		esp->eregs = (struct ESP_regs *) (system_base + SCSI);
	
	/* Set the command buffer */
		esp->esp_command = (volatile unsigned char *) cmd_buffer;
	
	/* get virtual dma address for command buffer */
		esp->esp_command_dvma = KSEG1ADDR((volatile unsigned char *) cmd_buffer);
	
		esp->irq = SCSI_INT;
	request_irq(esp->irq, esp_intr, SA_INTERRUPT, "NCR 53C94 SCSI",
	            NULL);
		request_irq(SCSI_DMA_INT, scsi_dma_int, SA_INTERRUPT, "JUNKIO SCSI DMA",
			    NULL);

	/*
	 * FIXME, look if the scsi id is availabe from NVRAM
	 */
	esp->scsi_id = 7;
		
	/* Check for differential SCSI-bus */
	/* What is this stuff? */
	esp->diff = 0;

	esp_initialize(esp);
	
		printk("ESP: Total of %d ESP hosts found, %d actually in use.\n", nesps, esps_in_use);
	esps_running = esps_in_use;
	return esps_in_use;
    }
    return 0;
}

/************************************************************* DMA Functions */
static void scsi_dma_int(int irq, void *dev_id, struct pt_regs *regs)
{
	extern volatile unsigned int *isr;
	unsigned int dummy;

	if (*isr & SCSI_PTR_LOADED) {
		/* next page */
		*scsi_next_ptr = ((*scsi_dma_ptr + PAGE_SIZE) & PAGE_MASK) << 3;
		*isr &= ~SCSI_PTR_LOADED;
	} else {
		printk("Got unexpected SCSI DMA Interrupt! < ");
		if (*isr & SCSI_PAGOVRRUN)
			printk("SCSI_PAGOVRRUN ");
		if (*isr & SCSI_DMA_MEMRDERR)
			printk("SCSI_DMA_MEMRDERR ");
		printk(">\n");
//		panic("stop");
		*isr &= ~(SCSI_PAGOVRRUN || SCSI_DMA_MEMRDERR);
	}

	/*
	 * This driver will only work on IOASIC machines
	 * so we can avoid an indirect function call here
	 * and flush the writeback buffer the fast way
	 */
	dummy = *isr;
	dummy = *isr;
}

static int dma_bytes_sent(struct NCR_ESP *esp, int fifo_count)
{
    return fifo_count;
}

static void dma_drain(struct NCR_ESP *esp)
{
	unsigned long nw;
	unsigned short *p = KSEG1ADDR((unsigned short *) ((*scsi_dma_ptr) >> 3));

    /*
	 * Is there something in the dma buffers left?
     */
	if (nw = *scsi_scr) {
		switch (nw) {
		case 1:
			*p = (unsigned short) *scsi_sdr0;
			break;
		case 2:
			*p++ = (unsigned short) (*scsi_sdr0);
			*p = (unsigned short) ((*scsi_sdr0) >> 16);
			break;
		case 3:
			*p++ = (unsigned short) (*scsi_sdr0);
			*p++ = (unsigned short) ((*scsi_sdr0) >> 16);
			*p = (unsigned short) (*scsi_sdr1);
			break;
		default:
			printk("Strange: %d words in dma buffer left\n", (int) nw);
			break;
		}
	}
}

static int dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd * sp)
{
	return sp->SCp.this_residual;;
}
    
static void dma_dump_state(struct NCR_ESP *esp)
{
/*    
    ESPLOG(("esp%d: dma -- enable <%08x> residue <%08x\n",
	    esp->esp_id, vdma_get_enable((int)esp->dregs), vdma_get_resdiue((int)esp->dregs)));
 */
}

static void dma_init_read(struct NCR_ESP *esp, __u32 vaddress, int length)
{
	extern volatile unsigned int *isr;
	unsigned int dummy;

	if (vaddress & 3)
		panic("dec_efs.c: unable to handle partial word transfers, yet...");

	dma_cache_wback_inv((unsigned long) phys_to_virt(vaddress), length);

	*ioasic_ssr &= ~SCSI_DMA_EN;
	*scsi_scr = 0;
	*scsi_dma_ptr = vaddress << 3;

	/* prepare for next page */
	*scsi_next_ptr = ((vaddress + PAGE_SIZE) & PAGE_MASK) << 3;
	*ioasic_ssr |= (SCSI_DMA_DIR | SCSI_DMA_EN);

	/*
	 * see above
	 */
	dummy = *isr;
	dummy = *isr;
}

static void dma_init_write(struct NCR_ESP *esp, __u32 vaddress, int length)
{
	extern volatile unsigned int *isr;
	unsigned int dummy;

	if (vaddress & 3)
		panic("dec_efs.c: unable to handle partial word transfers, yet...");

	dma_cache_wback_inv((unsigned long) phys_to_virt(vaddress), length);

	*ioasic_ssr &= ~(SCSI_DMA_DIR | SCSI_DMA_EN);
	*scsi_scr = 0;
	*scsi_dma_ptr = vaddress << 3;

	/* prepare for next page */
	*scsi_next_ptr = ((vaddress + PAGE_SIZE) & PAGE_MASK) << 3;
	*ioasic_ssr |= SCSI_DMA_EN;

	/*
	 * see above
	 */
	dummy = *isr;
	dummy = *isr;
}

static void dma_ints_off(struct NCR_ESP *esp)
{
	disable_irq(SCSI_DMA_INT);
}

static void dma_ints_on(struct NCR_ESP *esp)
{
	enable_irq(SCSI_DMA_INT);
}

static int dma_irq_p(struct NCR_ESP *esp)
{
    return (esp->eregs->esp_status & ESP_STAT_INTR);
}

static int dma_ports_p(struct NCR_ESP *esp)
{
/*
 * FIXME: what's this good for?
 */
	return 1;
}

static void dma_setup(struct NCR_ESP *esp, __u32 addr, int count, int write)
{
    /* 
     * On the Sparc, DMA_ST_WRITE means "move data from device to memory"
     * so when (write) is true, it actually means READ!
     */
	if (write) {
	dma_init_read(esp, addr, count);
    } else {
	dma_init_write(esp, addr, count);
    }
}

/*
 * These aren't used yet
 */
static void dma_mmu_get_scsi_one(struct NCR_ESP *esp, Scsi_Cmnd * sp)
{
	sp->SCp.have_data_in = PHYSADDR(sp->SCp.buffer);
	sp->SCp.ptr = (char *) ((unsigned long) sp->SCp.have_data_in);
}

static void dma_mmu_get_scsi_sgl(struct NCR_ESP *esp, Scsi_Cmnd * sp)
{
    int sz = sp->SCp.buffers_residual;
    struct mmu_sglist *sg = (struct mmu_sglist *) sp->SCp.buffer;
    
    while (sz >= 0) {
		sg[sz].dvma_addr = PHYSADDR(sg[sz].addr);
	sz--;
    }
	sp->SCp.ptr = (char *) ((unsigned long) sp->SCp.buffer->dvma_address);
}

static void dma_advance_sg(Scsi_Cmnd * sp)
{    
	sp->SCp.ptr = (char *) ((unsigned long) sp->SCp.buffer->dvma_address);
}

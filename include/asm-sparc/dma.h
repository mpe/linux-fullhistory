/*
 * include/asm-sparc/dma.h
 *
 * Don't even ask, I am figuring out how this crap works
 * on the Sparc. It may end up being real hairy to plug
 * into this code, maybe not, we'll see.
 *
 * Copyright 1995 (C) David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _ASM_SPARC_DMA_H
#define _ASM_SPARC_DMA_H

#include <asm/vac-ops.h>  /* for invalidate's, etc. */
#include <asm/sbus.h>
#include <asm/delay.h>
#include <asm/oplib.h>

/* DMA probing routine */
extern unsigned long probe_dma(unsigned long);

/* These are irrelevant for Sparc DMA, but we leave it in so that
 * things can compile.
 */
#define MAX_DMA_CHANNELS 8
#define MAX_DMA_ADDRESS  0x0

/* Structure to describe the current status of DMA registers on the Sparc */
struct sparc_dma_registers {
  volatile unsigned long cond_reg;   /* DMA condition register */
  volatile char * st_addr;           /* Start address of this transfer */
  volatile unsigned long cnt;        /* How many bytes to transfer */
  volatile unsigned long dma_test;   /* DMA test register */
};

/* Linux DMA information structure, filled during probe. */
struct Linux_SBus_DMA {
  struct linux_sbus_device *SBus_dev;   /* pointer to sbus device struct */
  struct sparc_dma_registers *DMA_regs; /* Pointer to DMA regs in IO space */

  /* Status, misc info */
  int node;                /* Prom node for this DMA device */
  int dma_running;         /* Are we using the DMA now? */

  /* DMA revision: 0=REV0 1=REV1 2=REV2 3=DMA_PLUS */
  int dma_rev;
};

extern struct Linux_SBus_DMA Sparc_DMA;

/* Main routines in dma.c */
extern void dump_dma_regs(struct sparc_dma_registers *);
extern unsigned long probe_dma(unsigned long);
extern void sparc_dma_init_transfer(struct sparc_dma_registers *,
				    unsigned long, int, int);
extern int sparc_dma_interrupt(struct sparc_dma_registers *);

/* Fields in the cond_reg register */
/* First, the version identification bits */
#define DMA_DEVICE_ID    0xf0000000        /* Device identification bits */
#define DMA_VERS0        0x00000000        /* Sunray DMA version */
#define DMA_VERS1        0x80000000        /* DMA rev 1 */
#define DMA_VERS2        0xa0000000        /* DMA rev 2 */
#define DMA_VERSPLUS     0x90000000        /* DMA rev 1 PLUS */

#define DMA_HNDL_INTR    0x00000001        /* An interrupt needs to be handled */
#define DMA_HNDL_ERROR   0x00000002        /* We need to take care of an error */
#define DMA_FIFO_ISDRAIN 0x0000000c        /* The DMA FIFO is draining */
#define DMA_INT_ENAB     0x00000010        /* Turn on interrupts */
#define DMA_FIFO_INV     0x00000020        /* Invalidate the FIFO */
#define DMA_ACC_SZ_ERR   0x00000040        /* The access size was bad */
#define DMA_FIFO_STDRAIN 0x00000040        /* DMA_VERS1 Drain the FIFO */
#define DMA_RST_SCSI     0x00000080        /* Reset the SCSI controller */
#define DMA_ST_WRITE     0x00000100        /* If set, write from device to memory */
#define DMA_ENABLE       0x00000200        /* Fire up DMA, handle requests */
#define DMA_PEND_READ    0x00000400        /* DMA_VERS1/0/PLUS Read is pending */
#define DMA_BCNT_ENAB    0x00002000        /* If on, use the byte counter */
#define DMA_TERM_CNTR    0x00004000        /* Terminal counter */
#define DMA_CSR_DISAB    0x00010000        /* No FIFO drains during csr */
#define DMA_SCSI_DISAB   0x00020000        /* No FIFO drains during reg */
#define DMA_BRST_SZ      0x000c0000        /* SBUS transfer r/w burst size */
#define DMA_ADDR_DISAB   0x00100000        /* No FIFO drains during addr */
#define DMA_2CLKS        0x00200000        /* Each transfer equals 2 clock ticks */
#define DMA_3CLKS        0x00400000        /* Each transfer equals 3 clock ticks */
#define DMA_CNTR_DISAB   0x00800000        /* No intr's when DMA_TERM_CNTR is set */
#define DMA_AUTO_NADDR   0x01000000        /* Use "auto next address" feature */
#define DMA_SCSI_ON      0x02000000        /* Enable SCSI dma */
#define DMA_LOADED_ADDR  0x04000000        /* Address has been loaded */
#define DMA_LOADED_NADDR 0x08000000        /* Next address has been loaded */

/* Only 24-bits of the byte count are significant */
#define DMA_BYTE_CNT_MASK  0x00ffffff

/* Pause until counter runs out or BIT isn't set in the DMA condition
 * register.
 */
extern inline void sparc_dma_pause(struct sparc_dma_registers *dma_regs,
				   unsigned long bit)
{
  int ctr = 50000;   /* Let's find some bugs ;) */

  /* Busy wait until the bit is not set any more */
  while((dma_regs->cond_reg&bit) && (ctr>0)) {
    ctr--;
    __delay(1);
  }

  /* Check for bogus outcome. */
  if(ctr==0) {
    printk("DMA Grrr:  I tried for wait for the assertion of bit %08xl to clear",
	   (unsigned int) bit);
    printk("           in the DMA condition register and it did not!\n");
    printk("Cannot continue, halting...\n");
    prom_halt();
  }

  return;
}

/* Enable DMA interrupts */
extern inline void sparc_dma_enable_interrupts(struct sparc_dma_registers *dma_regs)
{
  dma_regs->cond_reg |= DMA_INT_ENAB;
}

/* Disable DMA interrupts from coming in */
extern inline void sparc_dma_disable_interrupts(struct sparc_dma_registers *dma_regs)
{
  dma_regs->cond_reg &= ~(DMA_INT_ENAB);
}

/* Reset the DMA module. */
extern inline void sparc_dma_reset(struct sparc_dma_registers *dma_regs)
{
  /* Let the current FIFO drain itself */
  sparc_dma_pause(dma_regs, (DMA_FIFO_ISDRAIN));

  /* Reset the logic */
  dma_regs->cond_reg |= (DMA_RST_SCSI);     /* assert */
  __delay(400);                             /* let the bits set ;) */
  dma_regs->cond_reg &= ~(DMA_RST_SCSI);    /* de-assert */

  sparc_dma_enable_interrupts(dma_regs);    /* Re-enable interrupts */

  /* Enable FAST transfers if available */
  if(Sparc_DMA.dma_rev>1) { dma_regs->cond_reg |= DMA_3CLKS; }
  Sparc_DMA.dma_running = 0;

  return;
}

#endif /* !(_ASM_SPARC_DMA_H) */

  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* dma routines for the AP1000 */
#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apreg.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/irq.h>
#include <asm/pgtable.h>

#define DMA_MAX_TRANS_SIZE2 (0xfffffc)

int ap_dma_wait(int ch)
{
	int i = 0;
	while (DMA_IN(ch+DMA_DMST) & DMA_DMST_AC) i++;
	return i;
}

/* send some data out a dma channel */
int ap_dma_go(unsigned long ch,unsigned int p,int size,unsigned long cmd)
{
  int rest;

  p = mmu_v2p(p);

  cmd |= DMA_DCMD_ST | DMA_DCMD_TYP_AUTO;

#if 0
  if (ap_dma_wait(ch)) {
	  printk("WARNING: dma started when not complete\n");
  }

  if (cmd == DMA_DCMD_TD_MD && !(BIF_IN(BIF_SDCSR) & BIF_SDCSR_BG)) {
	  ap_led(0xAA);
	  printk("attempt to dma without holding the bus\n");
	  return -1;
  }
#endif

  /* reset the dma system */
  DMA_OUT(ch + DMA_DMST,DMA_DMST_RST);

  if (size <= DMA_MAX_TRANS_SIZE) {
    DMA_OUT(ch + DMA_MADDR,(unsigned long)p);
    DMA_OUT(ch + DMA_HSKIP,1);
    DMA_OUT(ch + DMA_VSKIP,1);
    DMA_OUT(ch + DMA_DCMD,cmd | B2W(size));
    return 0;
  } 

  if (size <= DMA_MAX_TRANS_SIZE2) {
    if(size & 0x3) size += 4;
    rest = (size & (DMA_TRANS_BLOCK_SIZE - 1)) >> 2;
    if (rest) {
      DMA_OUT(ch + DMA_HDRP,(unsigned)p);
      p += rest << 2;
    }
    DMA_OUT(ch + DMA_MADDR,(unsigned)p);
    DMA_OUT(ch + DMA_HSKIP,size >> (2 + 6));
    DMA_OUT(ch + DMA_VSKIP,1);
    DMA_OUT(ch + DMA_DCMD,cmd | (rest << 16) | 64);
    return 0;
  }

  printk("AP1000 DMA operation too big (%d bytes) - aborting\n",size);
  return(-1);
}


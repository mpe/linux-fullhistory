/*
 * arch/arm/kernel/dma-arc.c
 *
 * Copyright (C) 1998-1999 Dave Gilbert / Russell King
 *
 * DMA functions specific to Archimedes architecture
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hardware.h>

#include "dma.h"

#define DEBUG

int arch_request_dma(dmach_t channel, dma_t *dma, const char * dev_id)
{
  printk("arch_request_dma channel=%d F0=%d F1=%d\n",channel,DMA_VIRTUAL_FLOPPY0,DMA_VIRTUAL_FLOPPY1);
	if (channel == DMA_VIRTUAL_FLOPPY0 ||
	    channel == DMA_VIRTUAL_FLOPPY1)
		return 0;
	else
		return -EINVAL;
}

void arch_free_dma(dmach_t channel, dma_t *dma)
{
}

void arch_enable_dma(dmach_t channel, dma_t *dma)
{
  printk("arch_enable_dma channel=%d F0=%d F1=%d\n",channel,DMA_VIRTUAL_FLOPPY0,DMA_VIRTUAL_FLOPPY1);
	switch (channel) {
#ifdef CONFIG_BLK_DEV_FD1772
	case DMA_VIRTUAL_FLOPPY0: { /* Data DMA */
		switch (dma->dma_mode) {
		case DMA_MODE_READ: /* read */
    	    	{
			extern unsigned char fdc1772_dma_read, fdc1772_dma_read_end;
			extern void fdc1772_setupdma(unsigned int count,unsigned int addr);
			unsigned long flags;
#ifdef DEBUG
			printk("enable_dma fdc1772 data read\n");
#endif
			save_flags(flags);
			cliIF();
			
			memcpy ((void *)0x1c, (void *)&fdc1772_dma_read,
				&fdc1772_dma_read_end - &fdc1772_dma_read);
			fdc1772_setupdma(dma->buf.length, __bus_to_virt(dma->buf.address)); /* Sets data pointer up */
			enable_irq (64);
			restore_flags(flags);
		}
		break;

		case DMA_MODE_WRITE: /* write */
	        {
			extern unsigned char fdc1772_dma_write, fdc1772_dma_write_end;
			extern void fdc1772_setupdma(unsigned int count,unsigned int addr);
			unsigned long flags;

#ifdef DEBUG
			printk("enable_dma fdc1772 data write\n");
#endif
			save_flags(flags);
			cliIF();
			memcpy ((void *)0x1c, (void *)&fdc1772_dma_write,
				&fdc1772_dma_write_end - &fdc1772_dma_write);
			fdc1772_setupdma(dma->buf.length, __bus_to_virt(dma->buf.address)); /* Sets data pointer up */
			enable_irq (64);

			restore_flags(flags);
		}
		break;
    		default:
			printk ("enable_dma: dma%d not initialised\n", channel);
			return;
		}
	}
	break;

	case DMA_VIRTUAL_FLOPPY1: { /* Command end FIQ - actually just sets a flag */
		/* Need to build a branch at the FIQ address */
		extern void fdc1772_comendhandler(void);
		unsigned long flags;

		/*printk("enable_dma fdc1772 command end FIQ\n");*/
		save_flags(flags);
		cliIF();
	
		*((unsigned int *)0x1c)=0xea000000 | (((unsigned int)fdc1772_comendhandler-(0x1c+8))/4); /* B fdc1772_comendhandler */

		restore_flags(flags);
	}
	break;
#endif
	}
}

int arch_get_dma_residue(dmach_t channel, dma_t *dma)
{
  switch (channel) {
#ifdef CONFIG_BLK_DEV_FD1772
    case DMA_VIRTUAL_FLOPPY0: { /* Data DMA */
        extern unsigned int fdc1772_bytestogo;

        /* 10/1/1999 DAG - I presume its the number of bytes left? */
        return fdc1772_bytestogo;
      };
      break;

    case DMA_VIRTUAL_FLOPPY1: { /* Command completed */
        /* 10/1/1999 DAG - Presume whether there is an outstanding command? */
        extern unsigned int fdc1772_fdc_int_done;

        return (fdc1772_fdc_int_done==0)?1:0; /* Explicit! If the int done is 0 then 1 int to go */
      };
      break;

#endif

    default:
      printk("dma-arc.c:arch_get_dma_residue called with unknown/unconfigured DMA channel\n");
      return 0;
  };
}

void arch_disable_dma(dmach_t channel, dma_t *dma)
{
	if (channel != DMA_VIRTUAL_FLOPPY0 &&
	    channel != DMA_VIRTUAL_FLOPPY1)
		printk("arch_disable_dma: invalid channel %d\n", channel);
	else
		disable_irq(dma->dma_irq);
}

int arch_set_dma_speed(dmach_t channel, dma_t *dma, int cycle_ns)
{
	return 0;
}

void __init arch_dma_init(dma_t *dma)
{
	dma[DMA_VIRTUAL_FLOPPY0].dma_irq = 64;
	dma[DMA_VIRTUAL_FLOPPY1].dma_irq = 65;
}

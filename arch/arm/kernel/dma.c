/*
 * linux/arch/arm/kernel/dma.c
 *
 * Copyright (C) 1995, 1996 Russell King
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/mman.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/io.h>
#define KERNEL_ARCH_DMA
#include <asm/dma.h>

static unsigned long dma_address[8];
static unsigned long dma_count[8];
static char dma_direction[8] = { -1, -1, -1, -1, -1, -1, -1};

#if defined(CONFIG_ARCH_A5K) || defined(CONFIG_ARCH_RPC)
#define DMA_PCIO
#endif
#if defined(CONFIG_ARCH_ARC) && defined(CONFIG_BLK_DEV_FD)
#define DMA_OLD
#endif

void enable_dma (unsigned int dmanr)
{
	switch (dmanr) {
#ifdef DMA_PCIO
		case 2: {
			void *fiqhandler_start;
			unsigned int fiqhandler_length;
			extern void floppy_fiqsetup (unsigned long len, unsigned long addr,
					unsigned long port);
		    	switch (dma_direction[dmanr]) {
	    		case 1: {
				extern unsigned char floppy_fiqin_start, floppy_fiqin_end;
				fiqhandler_start = &floppy_fiqin_start;
				fiqhandler_length = &floppy_fiqin_end - &floppy_fiqin_start;
				break;
			}
			case 0: {
				extern unsigned char floppy_fiqout_start, floppy_fiqout_end;
				fiqhandler_start = &floppy_fiqout_start;
				fiqhandler_length = &floppy_fiqout_end - &floppy_fiqout_start;
				break;
			}
		    	default:
				printk ("enable_dma: dma%d not initialised\n", dmanr);
				return;
			}
			memcpy ((void *)0x1c, fiqhandler_start, fiqhandler_length);
			flush_page_to_ram(0);
			floppy_fiqsetup (dma_count[dmanr], dma_address[dmanr], (int)PCIO_FLOPPYDMABASE);
			enable_irq (64);
			return;
		}
#endif
#ifdef DMA_OLD
		case 0: { /* Data DMA */
			switch (dma_direction[dmanr]) {
			case 1: /* read */
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
				fdc1772_setupdma(dma_count[dmanr],dma_address[dmanr]); /* Sets data pointer up */
				enable_irq (64);
				restore_flags(flags);
			}
			break;

			case 0: /* write */
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
				fdc1772_setupdma(dma_count[dmanr],dma_address[dmanr]); /* Sets data pointer up */
				enable_irq (64);

				restore_flags(flags);
			}
			break;
	    		default:
				printk ("enable_dma: dma%d not initialised\n", dmanr);
				return;
			}
		}
		break;

		case 1: { /* Command end FIQ - actually just sets a flag */
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
		case DMA_0:
		case DMA_1:
		case DMA_2:
		case DMA_3:
		case DMA_S0:
		case DMA_S1:
			arch_enable_dma (dmanr - DMA_0);
			break;

		default:
			printk ("enable_dma: dma %d not supported\n", dmanr);
	}
}

void set_dma_mode (unsigned int dmanr, char mode)
{
	if (dmanr < 8) {
		if (mode == DMA_MODE_READ)
			dma_direction[dmanr] = 1;
		else if (mode == DMA_MODE_WRITE)
			dma_direction[dmanr] = 0;
		else
			printk ("set_dma_mode: dma%d: invalid mode %02X not supported\n",
				dmanr, mode);
	} else if (dmanr < MAX_DMA_CHANNELS)
		arch_set_dma_mode (dmanr - DMA_0, mode);
	else
		printk ("set_dma_mode: dma %d not supported\n", dmanr);
}

void set_dma_addr (unsigned int dmanr, unsigned int addr)
{
	if (dmanr < 8)
		dma_address[dmanr] = (unsigned long)addr;
	else if (dmanr < MAX_DMA_CHANNELS)
		arch_set_dma_addr (dmanr - DMA_0, addr);
	else
		printk ("set_dma_addr: dma %d not supported\n", dmanr);
}

void set_dma_count (unsigned int dmanr, unsigned int count)
{
	if (dmanr < 8)
		dma_count[dmanr] = (unsigned long)count;
	else if (dmanr < MAX_DMA_CHANNELS)
		arch_set_dma_count (dmanr - DMA_0, count);
	else
	    	printk ("set_dma_count: dma %d not supported\n", dmanr);
}

int get_dma_residue (unsigned int dmanr)
{
	if (dmanr < 8) {
		switch (dmanr) {
#if defined(CONFIG_ARCH_A5K) || defined(CONFIG_ARCH_RPC)
		case 2: {
			extern int floppy_fiqresidual (void);
			return floppy_fiqresidual ();
		}
#endif
#if defined(CONFIG_ARCH_ARC) && defined(CONFIG_BLK_DEV_FD)
		case 0: {
			extern unsigned int fdc1772_bytestogo;
			return fdc1772_bytestogo;
		}
#endif
		default:
			return -1;
		}
	} else if (dmanr < MAX_DMA_CHANNELS)
		return arch_dma_count (dmanr - DMA_0);
	return -1;
}

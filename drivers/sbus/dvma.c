/* dvma.c:  Routines that are used to access DMA on the Sparc SBus.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/oplib.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/sbus.h>

struct Linux_SBus_DMA *dma_chain;

/* Print out the current values in the DMA control registers */
extern __inline__ void
dump_dma_regs(struct sparc_dma_registers *dregs)
{
	printk("DMA CONTROL<%08lx>  ADDR<%08lx> CNT<%08lx> TEST<%08lx>\n",
	       (unsigned long) dregs->cond_reg,
	       (unsigned long) dregs->st_addr,
	       (unsigned long) dregs->cnt,
	       (unsigned long) dregs->dma_test);
	return;
}

__initfunc(void
init_one_dvma(struct Linux_SBus_DMA *dma, int num_dma))
{
	printk("dma%d: ", num_dma);
	
	dma->next = 0;
	dma->running=0;      /* No transfers going on as of yet */
	dma->allocated=0;    /* No one has allocated us yet */
	switch((dma->regs->cond_reg)&DMA_DEVICE_ID) {
	case DMA_VERS0:
		dma->revision=dvmarev0;
		printk("Revision 0 ");
		break;
	case DMA_ESCV1:
		dma->revision=dvmaesc1;
		printk("ESC Revision 1 ");
		break;
	case DMA_VERS1:
		dma->revision=dvmarev1;
		printk("Revision 1 ");
		break;
	case DMA_VERS2:
		dma->revision=dvmarev2;
		printk("Revision 2 ");
		break;
	case DMA_VERHME:
		dma->revision=dvmahme;
		printk("HME DVMA gate array ");
		break;
	case DMA_VERSPLUS:
		dma->revision=dvmarevplus;
		printk("Revision 1 PLUS ");
		break;
	default:
		printk("unknown dma version %x",
		       (dma->regs->cond_reg)&DMA_DEVICE_ID);
		dma->allocated = 1;
		break;
	}
	printk("\n");
#if 0 /* Clutters up the screen */
	dump_dma_regs(dma->regs);
#endif
}

/* Probe this SBus DMA module(s) */
__initfunc(void
dvma_init(struct linux_sbus *sbus))
{
	struct linux_sbus_device *this_dev;
	struct Linux_SBus_DMA *dma;
	struct Linux_SBus_DMA *dchain;
	static int num_dma=0;

	for_each_sbusdev(this_dev, sbus) {
		int hme = 0;

		if(!strcmp(this_dev->prom_name, "SUNW,fas"))
			hme = 1;
		else if(strcmp(this_dev->prom_name, "dma") &&
			strcmp(this_dev->prom_name, "ledma") &&
			strcmp(this_dev->prom_name, "espdma"))
			continue;

		/* Found one... */
		dma = kmalloc(sizeof(struct Linux_SBus_DMA), GFP_ATOMIC);

		dma->SBus_dev = this_dev;

		/* Put at end of dma chain */
		dchain = dma_chain;
		if(dchain) {
			while(dchain->next) dchain=dchain->next;
			dchain->next=dma;
		} else {
			/* We're the first in line */
			dma_chain=dma;
		}

		/* The constant PAGE_SIZE that is passed to sparc_alloc_io makes the
		 * routine only alloc 1 page, that was what the original code did
		 */
		if(hme) /* On HME cards, dvma lives with esp, 2 reg sets. */
			prom_apply_sbus_ranges(sbus, dma->SBus_dev->reg_addrs,
					       0x2, dma->SBus_dev);
		else    /* All others have only 1 reg set. */
			prom_apply_sbus_ranges(sbus, dma->SBus_dev->reg_addrs,
					       0x1, dma->SBus_dev);
		dma->regs = (struct sparc_dma_registers *)
			sparc_alloc_io (dma->SBus_dev->reg_addrs[0].phys_addr, 0,
					PAGE_SIZE, "dma",
					dma->SBus_dev->reg_addrs[0].which_io, 0x0);

		dma->node = dma->SBus_dev->prom_node;
		
		init_one_dvma(dma, num_dma++);
		
	};  /* while(this_dev) */
}

#ifdef CONFIG_SUN4

#include <asm/sun4paddr.h>

__initfunc(void sun4_dvma_init(void))
{
	struct Linux_SBus_DMA *dma;
	struct Linux_SBus_DMA *dchain;

	if(sun4_dma_physaddr) {
		dma = kmalloc(sizeof(struct Linux_SBus_DMA), GFP_ATOMIC);

		/* No SBUS */
		dma->SBus_dev = 0x0;

		/* Only one DMA device */
		dma_chain=dma;

		dma->regs = (struct sparc_dma_registers *)
			sparc_alloc_io (sun4_dma_physaddr, 0,
					PAGE_SIZE, "dma", 0x0, 0x0);

		/* No prom node */
		dma->node = 0x0;

		init_one_dvma(dma, 0);
	} else {
	  	dma_chain=0x0;
	}
}

#endif

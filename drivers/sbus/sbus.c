/* $Id: sbus.c,v 1.73 1998/10/07 11:35:50 jj Exp $
 * sbus.c:  SBus support routines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/pci.h>

#include <asm/system.h>
#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/oplib.h>
#include <asm/bpp.h>
#include <asm/irq.h>

/* This file has been written to be more dynamic and a bit cleaner,
 * but it still needs some spring cleaning.
 */

struct linux_sbus *SBus_chain;
static struct linux_prom_irqs irqs[PROMINTR_MAX] __initdata = { { 0 } };

static char lbuf[128];

extern void prom_sbus_ranges_init (int, struct linux_sbus *);

/* Perhaps when I figure out more about the iommu we'll put a
 * device registration routine here that probe_sbus() calls to
 * setup the iommu for each Sbus.
 */

/* We call this for each SBus device, and fill the structure based
 * upon the prom device tree.  We return the start of memory after
 * the things we have allocated.
 */

/* #define DEBUG_FILL */

__initfunc(static void
fill_sbus_device(int nd, struct linux_sbus_device *sbus_dev))
{
	int grrr, len;
	unsigned long dev_base_addr, base;

	sbus_dev->prom_node = nd;
	prom_getstring(nd, "name", lbuf, sizeof(lbuf));
	strcpy(sbus_dev->prom_name, lbuf);

	dev_base_addr = prom_getint(nd, "address");
	if(dev_base_addr != -1)
		sbus_dev->sbus_addr = dev_base_addr;

	len = prom_getproperty(nd, "reg", (void *) sbus_dev->reg_addrs,
			       sizeof(sbus_dev->reg_addrs));
	if(len == -1)
		goto no_regs;
	if(len%sizeof(struct linux_prom_registers)) {
		prom_printf("WHOOPS:  proplen for %s was %d, need multiple of %d\n",
		       sbus_dev->prom_name, len,
		       (int) sizeof(struct linux_prom_registers));
		panic("fill_sbus_device");
	}
	sbus_dev->num_registers = (len/sizeof(struct linux_prom_registers));
	sbus_dev->ranges_applied = 0;

	base = (unsigned long) sbus_dev->reg_addrs[0].phys_addr;
	if(base>=SUN_SBUS_BVADDR ||
	   (sparc_cpu_model != sun4c &&
	   sparc_cpu_model != sun4)) {
		/* Ahh, we can determine the slot and offset */
		if(sparc_cpu_model == sun4u) {
			/* A bit tricky on the SYSIO. */
			sbus_dev->slot = sbus_dev->reg_addrs[0].which_io;
			sbus_dev->offset = sbus_dev_offset(base);
		} else if (sparc_cpu_model == sun4d) {
			sbus_dev->slot = sbus_dev->reg_addrs[0].which_io;
			sbus_dev->offset = base;
		} else {
			sbus_dev->slot = sbus_dev_slot(base);
			sbus_dev->offset = sbus_dev_offset(base);
		}
	} else {   /* Grrr, gotta do calculations to fix things up */
		sbus_dev->slot = sbus_dev->reg_addrs[0].which_io;
		sbus_dev->offset = base;
		sbus_dev->reg_addrs[0].phys_addr = 
			sbus_devaddr(sbus_dev->slot, base);
		for(grrr=1; grrr<sbus_dev->num_registers; grrr++) {
			base = (unsigned long) sbus_dev->reg_addrs[grrr].phys_addr;
			sbus_dev->reg_addrs[grrr].phys_addr =
				sbus_devaddr(sbus_dev->slot, base);
		}
		/* That surely sucked */
	}
	sbus_dev->sbus_addr = (unsigned long) sbus_dev->reg_addrs[0].phys_addr;
	if(len>(sizeof(struct linux_prom_registers)*PROMREG_MAX)) {
		prom_printf("WHOOPS:  I got too many register addresses for %s  len=%d\n",
		       sbus_dev->prom_name, len);
		panic("sbus device register overflow");
	}

no_regs:
	len = prom_getproperty(nd, "address", (void *) sbus_dev->sbus_vaddrs,
			       sizeof(sbus_dev->sbus_vaddrs));
	if(len == -1) len=0;
	if(len&3) {
		prom_printf("Grrr, I didn't get a multiple of 4 proplen "
		       "for device %s got %d\n", sbus_dev->prom_name, len);
		len=0;
	}
	sbus_dev->num_vaddrs = (len/4);

#ifdef __sparc_v9__  
	len = prom_getproperty(nd, "interrupts", (void *)irqs, sizeof(irqs));
	if((len == -1) || (len == 0)) {
		sbus_dev->irqs[0] = 0;
		sbus_dev->num_irqs = 0;
	} else {
		sbus_dev->num_irqs = 1;
		if (irqs[0].pri < 0x20)
			sbus_dev->irqs[0] = sbus_build_irq(sbus_dev->my_bus, 
					irqs[0].pri + (sbus_dev->slot * 8));
		else
			sbus_dev->irqs[0] = sbus_build_irq(sbus_dev->my_bus, 
					irqs[0].pri);
	}
#else
	len = prom_getproperty(nd, "intr", (void *)irqs, sizeof(irqs));
	if (len == -1) len=0;
	if (len&7) {
		prom_printf("Grrr, I didn't get a multiple of 8 proplen for "
			    "device %s got %d\n", sbus_dev->prom_name, len);
		len=0;
	}
	if (len > 4 * 8) {
		prom_printf("Device %s has more than 4 interrupts\n", sbus_dev->prom_name);
		len = 4 * 8;
	}
	sbus_dev->num_irqs=(len/8);
	if(sbus_dev->num_irqs == 0)
		sbus_dev->irqs[0]=0;
	else if (sparc_cpu_model != sun4d)
		for (len = 0; len < sbus_dev->num_irqs; len++)
			sbus_dev->irqs[len] = irqs[len].pri;
	else {
		extern unsigned int sun4d_build_irq(struct linux_sbus_device *sdev, int irq);
		
		for (len = 0; len < sbus_dev->num_irqs; len++)
			sbus_dev->irqs[len] = sun4d_build_irq(sbus_dev, irqs[len].pri);
	}
#endif

#ifdef DEBUG_FILL
#ifdef __sparc_v9__
	prom_printf("Found %s at SBUS slot %x offset %016lx ",
	       sbus_dev->prom_name, sbus_dev->slot, sbus_dev->offset);
	if (sbus_dev->irqs[0])
		prom_printf("irq %s\n", __irq_itoa(sbus_dev->irqs[0]));
	else
		prom_printf("\n");
	prom_printf("Base address %016lx\n", sbus_dev->sbus_addr);
#else
	prom_printf("Found %s at SBUS slot %x offset %08lx irq-level %d\n",
	       sbus_dev->prom_name, sbus_dev->slot, sbus_dev->offset,
	       sbus_dev->irqs[0]);
	prom_printf("Base address %08lx\n", sbus_dev->sbus_addr);
#endif
	prom_printf("REGISTERS: Probed %d register(s)\n", sbus_dev->num_registers);
	for(len=0; len<sbus_dev->num_registers; len++)
#ifdef __sparc_v9__
		prom_printf("Regs<%d> at address<%08lx> IO-space<%d> size<%d "
		       "bytes, %d words>\n", (int) len,
		       (unsigned long) sbus_dev->reg_addrs[len].phys_addr,
		       sbus_dev->reg_addrs[len].which_io,
		       sbus_dev->reg_addrs[len].reg_size,
		       (sbus_dev->reg_addrs[len].reg_size/4));
#else
		prom_printf("Regs<%d> at address<%016lx> IO-space<%d> size<%d "
		       "bytes, %d words>\n", (int) len,
		       (unsigned long) sbus_dev->reg_addrs[len].phys_addr,
		       sbus_dev->reg_addrs[len].which_io,
		       sbus_dev->reg_addrs[len].reg_size,
		       (sbus_dev->reg_addrs[len].reg_size/4));
#endif
#endif
}

/* This routine gets called from whoever needs the sbus first, to scan
 * the SBus device tree.  Currently it just prints out the devices
 * found on the bus and builds trees of SBUS structs and attached
 * devices.
 */

extern void iommu_init(int iommu_node, struct linux_sbus *sbus);
extern void iounit_init(int sbi_node, int iounit_node, struct linux_sbus *sbus);
void sun4_init(void);
#ifdef CONFIG_SUN_OPENPROMIO
extern int openprom_init(void);
#endif
#ifdef CONFIG_SUN_AUXIO
extern void auxio_probe(void);
#endif
#ifdef CONFIG_OBP_FLASH
extern int flash_init(void);
#endif

__initfunc(static void
sbus_do_child_siblings(int start_node, struct linux_sbus_device *child,
		       struct linux_sbus *sbus))
{
	struct linux_sbus_device *this_dev = child;
	int this_node = start_node;

	/* Child already filled in, just need to traverse siblings. */
	child->child = 0;
	while((this_node = prom_getsibling(this_node)) != 0) {
		this_dev->next = kmalloc(sizeof(struct linux_sbus_device), GFP_ATOMIC);
		this_dev = this_dev->next;
		this_dev->next = 0;

		this_dev->my_bus = sbus;
		fill_sbus_device(this_node, this_dev);

		if(prom_getchild(this_node)) {
			this_dev->child = kmalloc(sizeof(struct linux_sbus_device),
						  GFP_ATOMIC);
			this_dev->child->my_bus = sbus;
			fill_sbus_device(prom_getchild(this_node), this_dev->child);
			sbus_do_child_siblings(prom_getchild(this_node),
					       this_dev->child, sbus);
		} else {
			this_dev->child = 0;
		}
	}
}

__initfunc(void sbus_init(void))
{
	register int nd, this_sbus, sbus_devs, topnd, iommund;
	unsigned int sbus_clock;
	struct linux_sbus *sbus;
	struct linux_sbus_device *this_dev;
	int num_sbus = 0;  /* How many did we find? */
	
#ifdef CONFIG_SUN4
	return sun4_dvma_init();
#endif

	topnd = prom_getchild(prom_root_node);
	
	/* Finding the first sbus is a special case... */
	iommund = 0;
	if(sparc_cpu_model == sun4u) {
		nd = prom_searchsiblings(topnd, "sbus");
		if(nd == 0) {
#ifdef CONFIG_PCI
			if (!pcibios_present()) {	
				prom_printf("Neither SBUS nor PCI found.\n");
				prom_halt();
			}
			return;
#else
			prom_printf("YEEE, UltraSparc sbus not found\n");
			prom_halt();
#endif
		}
	} else if(sparc_cpu_model == sun4d) {
		if((iommund = prom_searchsiblings(topnd, "io-unit")) == 0 ||
		   (nd = prom_getchild(iommund)) == 0 ||
		   (nd = prom_searchsiblings(nd, "sbi")) == 0) {
		   	panic("sbi not found");
		}
	} else if((nd = prom_searchsiblings(topnd, "sbus")) == 0) {
		if((iommund = prom_searchsiblings(topnd, "iommu")) == 0 ||
		   (nd = prom_getchild(iommund)) == 0 ||
		   (nd = prom_searchsiblings(nd, "sbus")) == 0) {
#ifdef CONFIG_PCI
                        if (!pcibios_present()) {       
                                prom_printf("Neither SBUS nor PCI found.\n");
                                prom_halt();
                        }
                        return;
#else
			/* No reason to run further - the data access trap will occur. */
			panic("sbus not found");
#endif
		}
	}

	/* Ok, we've found the first one, allocate first SBus struct
	 * and place in chain.
	 */
	sbus = SBus_chain = kmalloc(sizeof(struct linux_sbus), GFP_ATOMIC);
	sbus->next = 0;
	this_sbus=nd;

	if(iommund && sparc_cpu_model != sun4u && sparc_cpu_model != sun4d)
		iommu_init(iommund, sbus);

	/* Loop until we find no more SBUS's */
	while(this_sbus) {
		/* IOMMU hides inside SBUS/SYSIO prom node on Ultra. */
		if(sparc_cpu_model == sun4u)
			iommu_init(this_sbus, sbus);
#ifndef __sparc_v9__						  
		else if (sparc_cpu_model == sun4d)
			iounit_init(this_sbus, iommund, sbus);
#endif						   
		printk("sbus%d: ", num_sbus);
		sbus_clock = prom_getint(this_sbus, "clock-frequency");
		if(sbus_clock==-1) sbus_clock = (25*1000*1000);
		printk("Clock %d.%d MHz\n", (int) ((sbus_clock/1000)/1000),
		       (int) (((sbus_clock/1000)%1000 != 0) ? 
			      (((sbus_clock/1000)%1000) + 1000) : 0));

		prom_getstring(this_sbus, "name", lbuf, sizeof(lbuf));
		lbuf[sizeof(sbus->prom_name) - 1] = 0;
		sbus->prom_node = this_sbus;
		strcpy(sbus->prom_name, lbuf);
		sbus->clock_freq = sbus_clock;
#ifndef __sparc_v9__		
		if (sparc_cpu_model == sun4d) {
			sbus->devid = prom_getint(iommund, "device-id");
			sbus->board = prom_getint(iommund, "board#");
		}
#endif
		
		prom_sbus_ranges_init (iommund, sbus);

		sbus_devs = prom_getchild(this_sbus);

		sbus->devices = kmalloc(sizeof(struct linux_sbus_device), GFP_ATOMIC);

		this_dev = sbus->devices;
		this_dev->next = 0;

		this_dev->my_bus = sbus;
		fill_sbus_device(sbus_devs, this_dev);

		/* Should we traverse for children? */
		if(prom_getchild(sbus_devs)) {
			/* Allocate device node */
			this_dev->child = kmalloc(sizeof(struct linux_sbus_device),
						  GFP_ATOMIC);
			/* Fill it */
			this_dev->child->my_bus = sbus;
			fill_sbus_device(prom_getchild(sbus_devs), this_dev->child);
			sbus_do_child_siblings(prom_getchild(sbus_devs),
					       this_dev->child, sbus);
		} else {
			this_dev->child = 0;
		}

		while((sbus_devs = prom_getsibling(sbus_devs)) != 0) {
			/* Allocate device node */
			this_dev->next = kmalloc(sizeof(struct linux_sbus_device),
						 GFP_ATOMIC);
			this_dev=this_dev->next;
			this_dev->next=0;

			/* Fill it */
			this_dev->my_bus = sbus;
			fill_sbus_device(sbus_devs, this_dev);

			/* Is there a child node hanging off of us? */
			if(prom_getchild(sbus_devs)) {
				/* Get new device struct */
				this_dev->child =
					kmalloc(sizeof(struct linux_sbus_device),
						GFP_ATOMIC);
				/* Fill it */
				this_dev->child->my_bus = sbus;
				fill_sbus_device(prom_getchild(sbus_devs),
						 this_dev->child);
				sbus_do_child_siblings(prom_getchild(sbus_devs),
						     this_dev->child, sbus);
			} else {
				this_dev->child = 0;
			}
		}

		dvma_init(sbus);

		num_sbus++;
		if(sparc_cpu_model == sun4u) {
			this_sbus = prom_getsibling(this_sbus);
			if(!this_sbus)
				break;
			this_sbus = prom_searchsiblings(this_sbus, "sbus");
		} else if(sparc_cpu_model == sun4d) {
			iommund = prom_getsibling(iommund);
			if(!iommund) break;
			iommund = prom_searchsiblings(iommund, "io-unit");
			if(!iommund) break;
			this_sbus = prom_searchsiblings(prom_getchild(iommund), "sbi");
		} else {
			this_sbus = prom_getsibling(this_sbus);
			if(!this_sbus) break;
			this_sbus = prom_searchsiblings(this_sbus, "sbus");
		}
		if(this_sbus) {
			sbus->next = kmalloc(sizeof(struct linux_sbus), GFP_ATOMIC);
			sbus = sbus->next;
			sbus->next = 0;
		} else {
			break;
		}
	} /* while(this_sbus) */
	if (sparc_cpu_model == sun4d) {
		extern void sun4d_init_sbi_irq(void);
		sun4d_init_sbi_irq();
	}
	
#ifdef CONFIG_SUN_OPENPROMIO
	openprom_init();
#endif
#ifdef CONFIG_SUN_BPP
	bpp_init();
#endif
#ifdef CONFIG_SUN_AUXIO
	if (sparc_cpu_model == sun4u)
		auxio_probe ();
#endif
#ifdef CONFIG_OBP_FLASH
	flash_init();
#endif
#ifdef __sparc_v9__
	if (sparc_cpu_model == sun4u) {
		extern void clock_probe(void);

		clock_probe();
	}
#endif
}

/* sbus.c:  SBus support routines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/malloc.h>

#include <asm/system.h>
#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/oplib.h>

/* This file has been written to be more dynamic and a bit cleaner,
 * but it still needs some spring cleaning.
 */

struct linux_sbus *SBus_chain;

static char lbuf[128];

/* Perhaps when I figure out more about the iommu we'll put a
 * device registration routine here that probe_sbus() calls to
 * setup the iommu for each Sbus.
 */

/* We call this for each SBus device, and fill the structure based
 * upon the prom device tree.  We return the start of memory after
 * the things we have allocated.
 */

/* #define DEBUG_FILL */
void
fill_sbus_device(int nd, struct linux_sbus_device *sbus_dev)
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
	if(len%sizeof(struct linux_prom_registers)) {
		prom_printf("WHOOPS:  proplen for %s was %d, need multiple of %d\n",
		       sbus_dev->prom_name, len,
		       (int) sizeof(struct linux_prom_registers));
		panic("fill_sbus_device");
	}
	sbus_dev->num_registers = (len/sizeof(struct linux_prom_registers));

	base = (unsigned long) sbus_dev->reg_addrs[0].phys_addr;
	if(base>=SUN_SBUS_BVADDR || sparc_cpu_model == sun4m) {
		/* Ahh, we can determine the slot and offset */
		sbus_dev->slot = sbus_dev_slot(base);
		sbus_dev->offset = sbus_dev_offset(base);
	} else {   /* Grrr, gotta do calculations to fix things up */
		sbus_dev->slot = sbus_dev->reg_addrs[0].which_io;
		sbus_dev->offset = base;
		sbus_dev->reg_addrs[0].phys_addr = 
			(char *) sbus_devaddr(sbus_dev->slot, base);
		for(grrr=1; grrr<sbus_dev->num_registers; grrr++) {
			base = (unsigned long) sbus_dev->reg_addrs[grrr].phys_addr;
			sbus_dev->reg_addrs[grrr].phys_addr = (char *) 
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

	len = prom_getproperty(nd, "address", (void *) sbus_dev->sbus_vaddrs,
			       sizeof(sbus_dev->sbus_vaddrs));
	if(len == -1) len=0;
	if(len&3) {
		prom_printf("Grrr, I didn't get a multiple of 4 proplen "
		       "for device %s got %d\n", sbus_dev->prom_name, len);
		len=0;
	}
	sbus_dev->num_vaddrs = (len/4);
  
	len = prom_getproperty(nd, "intr", (void *)sbus_dev->irqs,
			       sizeof(sbus_dev->irqs));
	if (len == -1) len=0;
	if (len&7) {
		prom_printf("Grrr, I didn't get a multiple of 8 proplen for "
		       "device %s got %d\n", sbus_dev->prom_name, len);
		len=0;
	}
	sbus_dev->num_irqs=(len/8);
#if OLD_STYLE_IRQ
	/* Grrr, V3 prom tries to be efficient */
	for(len=0; len<sbus_dev->num_irqs; len++) {
		sbus_dev->irqs[len].pri &= 0xf;
	}
#endif
	if(sbus_dev->num_irqs == 0) sbus_dev->irqs[0].pri=0;

#ifdef DEBUG_FILL
	prom_printf("Found %s at SBUS slot %x offset %08lx irq-level %d\n",
	       sbus_dev->prom_name, sbus_dev->slot, sbus_dev->offset,
	       sbus_dev->irqs[0].pri);
	prom_printf("Base address %08lx\n", sbus_dev->sbus_addr);
	prom_printf("REGISTERS: Probed %d register(s)\n", sbus_dev->num_registers);
	for(len=0; len<sbus_dev->num_registers; len++)
		prom_printf("Regs<%d> at address<%08lx> IO-space<%d> size<%d "
		       "bytes, %d words>\n", (int) len,
		       (unsigned long) sbus_dev->reg_addrs[len].phys_addr,
		       sbus_dev->reg_addrs[len].which_io,
		       sbus_dev->reg_addrs[len].reg_size,
		       (sbus_dev->reg_addrs[len].reg_size/4));
#endif

	return;
}

/* This routine gets called from whoever needs the sbus first, to scan
 * the SBus device tree.  Currently it just prints out the devices
 * found on the bus and builds trees of SBUS structs and attached
 * devices.
 */

extern void sun_console_init(void);
extern unsigned long iommu_init(int iommu_node, unsigned long memstart,
				unsigned long memend, struct linux_sbus *sbus);

unsigned long
sbus_init(unsigned long memory_start, unsigned long memory_end)
{
	register int nd, this_sbus, sbus_devs, topnd, iommund;
	unsigned int sbus_clock;
	struct linux_sbus *sbus;
	struct linux_sbus_device *this_dev;
	int num_sbus = 0;  /* How many did we find? */

	memory_start = ((memory_start + 7) & (~7));

	topnd = prom_getchild(prom_root_node);

	/* Finding the first sbus is a special case... */
	iommund = 0;
	if((nd = prom_searchsiblings(topnd, "sbus")) == 0) {
		if((iommund = prom_searchsiblings(topnd, "iommu")) == 0 ||
		   (nd = prom_getchild(iommund)) == 0 ||
		   (nd = prom_searchsiblings(nd, "sbus")) == 0) {
			/* No reason to run further - the data access trap will occur. */
			panic("sbus not found");
		}
	}

	/* Ok, we've found the first one, allocate first SBus struct
	 * and place in chain.
	 */
	sbus = SBus_chain = (struct linux_sbus *) memory_start;
	memory_start += sizeof(struct linux_sbus);
	sbus->next = 0;
	this_sbus=nd;

	/* Have IOMMU will travel. XXX grrr - this should be per sbus... */
	if(iommund)
		memory_start = iommu_init(iommund, memory_start, memory_end, sbus);

	/* Loop until we find no more SBUS's */
	while(this_sbus) {
		printk("sbus%d: ", num_sbus);
		sbus_clock = prom_getint(this_sbus, "clock-frequency");
		if(sbus_clock==-1) sbus_clock = (25*1000*1000);
		printk("Clock %d.%d MHz\n", (int) ((sbus_clock/1000)/1000),
		       (int) (((sbus_clock/1000)%1000 != 0) ? 
			      (((sbus_clock/1000)%1000) + 1000) : 0));

		prom_getstring(this_sbus, "name", lbuf, sizeof(lbuf));
		sbus->prom_node = this_sbus;
		strcpy(sbus->prom_name, lbuf);
		sbus->clock_freq = sbus_clock;

		sbus_devs = prom_getchild(this_sbus);

		sbus->devices = (struct linux_sbus_device *) memory_start;
		memory_start += sizeof(struct linux_sbus_device);

		this_dev = sbus->devices;
		this_dev->next = 0;

		fill_sbus_device(sbus_devs, this_dev);
		this_dev->my_bus = sbus;

		/* Should we traverse for children? */
		if(strcmp(this_dev->prom_name, "espdma")==0 ||
		   strcmp(this_dev->prom_name, "ledma")==0) {
			/* Allocate device node */
			this_dev->child = (struct linux_sbus_device *) memory_start;
			memory_start += sizeof(struct linux_sbus_device);
			/* Fill it */
			fill_sbus_device(prom_getchild(sbus_devs), this_dev->child);
			this_dev->child->my_bus = sbus;
		} else {
			this_dev->child = 0;
		}

		while((sbus_devs = prom_getsibling(sbus_devs)) != 0) {
			/* Allocate device node */
			this_dev->next = (struct linux_sbus_device *) memory_start;
			memory_start += sizeof(struct linux_sbus_device);
			this_dev=this_dev->next;
			this_dev->next=0;

			/* Fill it */
			fill_sbus_device(sbus_devs, this_dev);
			this_dev->my_bus = sbus;

			/* Is there a child node hanging off of us? */
			if(strcmp(this_dev->prom_name, "espdma")==0 ||
			   strcmp(this_dev->prom_name, "ledma")==0) {
				/* Get new device struct */
				this_dev->child =
					(struct linux_sbus_device *) memory_start;
				memory_start += sizeof(struct linux_sbus_device);

				/* Fill it */
				fill_sbus_device(prom_getchild(sbus_devs),
						 this_dev->child);
				this_dev->child->my_bus = sbus;
			} else {
				this_dev->child = 0;
			}
		}

		memory_start = dvma_init(sbus, memory_start);

		num_sbus++;
		this_sbus = prom_getsibling(this_sbus);
		if(!this_sbus) break;
		this_sbus = prom_searchsiblings(this_sbus, "sbus");
		if(this_sbus) {
			sbus->next = (struct linux_sbus *) memory_start;
			memory_start += sizeof(struct linux_sbus);
			sbus = sbus->next;
			sbus->next = 0;
		} else {
			break;
		}
	} /* while(this_sbus) */
	sun_console_init(); /* whee... */
	return memory_start;
}

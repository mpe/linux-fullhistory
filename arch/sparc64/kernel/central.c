/* $Id: central.c,v 1.3 1997/08/12 14:51:55 davem Exp $
 * central.c: Central FHC driver for Sunfire/Starfire/Wildfire.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>

#include <asm/page.h>
#include <asm/fhc.h>

struct linux_central *central_bus = NULL;

static inline unsigned long long_align(unsigned long addr)
{
	return ((addr + (sizeof(unsigned long) - 1)) &
		~(sizeof(unsigned long) - 1));
}

extern void prom_central_ranges_init(int cnode, struct linux_central *central);
extern void prom_fhc_ranges_init(int fnode, struct linux_fhc *fhc);

unsigned long central_probe(unsigned long memory_start)
{
	struct linux_prom_registers fpregs[6];
	struct linux_fhc *fhc;
	char namebuf[128];
	int cnode, fnode, err;

	prom_printf("CENTRAL: ");
	printk("CENTRAL: ");
	cnode = prom_finddevice("/central");
	if(cnode == 0 || cnode == -1) {
		prom_printf("no central found.\n");
		printk("no central found.\n");
		return memory_start;
	}
	prom_printf("found central PROM node.\n");
	printk("found central PROM node.\n");

	/* Ok we got one, grab some memory for software state. */
	memory_start = long_align(memory_start);
	central_bus = (struct linux_central *) (memory_start);

	prom_printf("CENTRAL: central_bus[%p] ", central_bus);
	memory_start += sizeof(struct linux_central);
	memory_start = long_align(memory_start);
	fhc = (struct linux_fhc *)(memory_start);
	memory_start += sizeof(struct linux_fhc);
	memory_start = long_align(memory_start);

	prom_printf("fhc[%p] ", fhc);

	/* First init central. */
	central_bus->child = fhc;
	central_bus->prom_node = cnode;

	prom_getstring(cnode, "name", namebuf, sizeof(namebuf));
	strcpy(central_bus->prom_name, namebuf);

	prom_printf("init_central_ranges ");
	prom_central_ranges_init(cnode, central_bus);

	/* And then central's FHC. */
	fhc->next = NULL;
	fhc->parent = central_bus;
	fnode = prom_searchsiblings(prom_getchild(cnode), "fhc");
	if(fnode == 0 || fnode == -1) {
		prom_printf("Critical error, central board lacks fhc.\n");
		prom_halt();
	}
	fhc->prom_node = fnode;
	prom_getstring(fnode, "name", namebuf, sizeof(namebuf));
	strcpy(fhc->prom_name, namebuf);

	prom_printf("cnode[%x] fnode[%x] init_fhc_ranges\n", cnode, fnode);
	prom_fhc_ranges_init(fnode, fhc);

	/* Finally, map in FHC register set.  (From the prtconf dumps
	 * I have seen on Ex000 boxes only the central ranges need to
	 * be applied to the fhc internal register set) -DaveM
	 */
	err = prom_getproperty(fnode, "reg", (char *)&fpregs[0], sizeof(fpregs));
	if(err == -1) {
		prom_printf("CENTRAL: fatal error, cannot get fhc regs.\n");
		prom_halt();
	}
	prom_apply_central_ranges(central_bus, &fpregs[0], 6);
	prom_printf("CENTRAL: FHC_REGS[(%08x,%08x) (%08x,%08x) "
		    "(%08x,%08x) (%08x,%08x) (%08x,%08x) (%08x,%08x)]\n",
		    fpregs[0].which_io, fpregs[0].phys_addr,
		    fpregs[1].which_io, fpregs[1].phys_addr,
		    fpregs[2].which_io, fpregs[2].phys_addr,
		    fpregs[3].which_io, fpregs[3].phys_addr,
		    fpregs[4].which_io, fpregs[4].phys_addr,
		    fpregs[5].which_io, fpregs[5].phys_addr);
	fhc->fhc_regs.pregs = (struct fhc_internal_regs *)
		__va((((unsigned long)fpregs[0].which_io)<<32) |
		     (((unsigned long)fpregs[0].phys_addr)));
	fhc->fhc_regs.ireg = (struct fhc_ign_reg *)
		__va((((unsigned long)fpregs[1].which_io)<<32) |
		     (((unsigned long)fpregs[1].phys_addr)));
	fhc->fhc_regs.ffregs = (struct fhc_fanfail_regs *)
		__va((((unsigned long)fpregs[2].which_io)<<32) |
		     (((unsigned long)fpregs[2].phys_addr)));
	fhc->fhc_regs.sregs = (struct fhc_system_regs *)
		__va((((unsigned long)fpregs[3].which_io)<<32) |
		     (((unsigned long)fpregs[3].phys_addr)));
	fhc->fhc_regs.uregs = (struct fhc_uart_regs *)
		__va((((unsigned long)fpregs[4].which_io)<<32) |
		     (((unsigned long)fpregs[4].phys_addr)));
	fhc->fhc_regs.tregs = (struct fhc_tod_regs *)
		__va((((unsigned long)fpregs[5].which_io)<<32) |
		     (((unsigned long)fpregs[5].phys_addr)));
	prom_printf("CENTRAL: FHC_REGS[%p %p %p %p %p %p]\n",
		    fhc->fhc_regs.pregs, fhc->fhc_regs.ireg,
		    fhc->fhc_regs.ffregs, fhc->fhc_regs.sregs,
		    fhc->fhc_regs.uregs, fhc->fhc_regs.tregs);

	prom_printf("CENTRAL: reading FHC_ID register... ");
	err = fhc->fhc_regs.pregs->fhc_id;
	prom_printf("VALUE[%x]\n", err);
	printk("FHC Version[%x] PartID[%x] Manufacturer[%x]\n",
	       ((err & FHC_ID_VERS) >> 28),
	       ((err & FHC_ID_PARTID) >> 12),
	       ((err & FHC_ID_MANUF) >> 1));

	return memory_start;
}

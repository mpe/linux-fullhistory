/*
 *	linux/arch/alpha/kernel/sys_mikasa.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the MIKASA (AlphaServer 1000).
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_apecs.h>
#include <asm/core_cia.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"

static void
mikasa_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16)
		outw(~(mask >> 16), 0x536); /* note invert */
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);
	else
		outb(mask, 0x21);
}

static void 
mikasa_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary registers */
	pld = (((unsigned long) (~inw(0x534)) & 0x0000ffffUL) << 16) |
		(((unsigned long) inb(0xa0))  <<  8) |
		((unsigned long) inb(0x20));

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i < 16) {
			isa_device_interrupt(vector, regs);
		} else {
			handle_irq(i, i, regs);
		}
	}
}

static void __init
mikasa_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	if (alpha_using_srm)
		alpha_mv.device_interrupt = srm_device_interrupt;

	outw(~(alpha_irq_mask >> 16), 0x536);	/* note invert */
	enable_irq(2);				/* enable cascade */
}


/*
 * PCI Fixup configuration.
 *
 * Summary @ 0x536:
 * Bit      Meaning
 * 0        Interrupt Line A from slot 0
 * 1        Interrupt Line B from slot 0
 * 2        Interrupt Line C from slot 0
 * 3        Interrupt Line D from slot 0
 * 4        Interrupt Line A from slot 1
 * 5        Interrupt line B from slot 1
 * 6        Interrupt Line C from slot 1
 * 7        Interrupt Line D from slot 1
 * 8        Interrupt Line A from slot 2
 * 9        Interrupt Line B from slot 2
 *10        Interrupt Line C from slot 2
 *11        Interrupt Line D from slot 2
 *12        NCR 810 SCSI
 *13        Power Supply Fail
 *14        Temperature Warn
 *15        Reserved
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  6       NCR SCSI controller
 *  7       Intel PCI-EISA bridge chip
 * 11       PCI on board slot 0
 * 12       PCI on board slot 1
 * 13       PCI on board slot 2
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */

static int __init
mikasa_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[8][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{16+12, 16+12, 16+12, 16+12, 16+12},	/* IdSel 17,  SCSI */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 18,  PCEB */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 19,  ???? */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 20,  ???? */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 21,  ???? */
		{ 16+0,  16+0,  16+1,  16+2,  16+3},	/* IdSel 22,  slot 0 */
		{ 16+4,  16+4,  16+5,  16+6,  16+7},	/* IdSel 23,  slot 1 */
		{ 16+8,  16+8,  16+9, 16+10, 16+11},	/* IdSel 24,  slot 2 */
	};
	const long min_idsel = 6, max_idsel = 13, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static void __init
mikasa_pci_fixup(void)
{
	layout_all_busses(EISA_DEFAULT_IO_BASE,APECS_AND_LCA_DEFAULT_MEM_BASE);
	common_pci_fixup(mikasa_map_irq, common_swizzle);
}

static void __init
mikasa_primo_pci_fixup(void)
{
	layout_all_busses(EISA_DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(mikasa_map_irq, common_swizzle);
}

static void
mikasa_machine_check(unsigned long vector, unsigned long la_ptr,
		     struct pt_regs * regs)
{
#define MCHK_NO_DEVSEL 0x205L
#define MCHK_NO_TABT 0x204L

	struct el_common *mchk_header;
	struct el_apecs_procdata *mchk_procdata;
	struct el_apecs_mikasa_sysdata_mcheck *mchk_sysdata;
	unsigned long *ptr;
	int i;

	mchk_header = (struct el_common *)la_ptr;

	mchk_procdata = (struct el_apecs_procdata *)
		(la_ptr + mchk_header->proc_offset
		 - sizeof(mchk_procdata->paltemp));

	mchk_sysdata = (struct el_apecs_mikasa_sysdata_mcheck *)
		(la_ptr + mchk_header->sys_offset);

#ifdef DEBUG
	printk("mikasa_machine_check: vector=0x%lx la_ptr=0x%lx\n",
	       vector, la_ptr);
	printk("        pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
	       regs->pc, mchk_header->size, mchk_header->proc_offset,
	       mchk_header->sys_offset);
	printk("mikasa_machine_check: expected %d DCSR 0x%lx PEAR 0x%lx\n",
	       apecs_mcheck_expected, mchk_sysdata->epic_dcsr,
	       mchk_sysdata->epic_pear);
	ptr = (unsigned long *)la_ptr;
	for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
		printk(" +%lx %lx %lx\n", i*sizeof(long), ptr[i], ptr[i+1]);
	}
#endif

	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */

	if (apecs_mcheck_expected
	    && ((unsigned int)mchk_header->code == MCHK_NO_DEVSEL
		|| (unsigned int)mchk_header->code == MCHK_NO_TABT)) {
		apecs_mcheck_expected = 0;
		apecs_mcheck_taken = 1;
		mb();
		mb(); /* magic */
		apecs_pci_clr_err();
		wrmces(0x7);
		mb();
		draina();
	}
	else if (vector == 0x620 || vector == 0x630) {
		/* Disable correctable from now on.  */
		wrmces(0x1f);
		mb();
		draina();
		printk("mikasa_machine_check: HW correctable (0x%lx)\n",
		       vector);
	}
	else {
		printk(KERN_CRIT "APECS machine check:\n");
		printk(KERN_CRIT "  vector=0x%lx la_ptr=0x%lx\n",
		       vector, la_ptr);
		printk(KERN_CRIT
		       "  pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
		       regs->pc, mchk_header->size, mchk_header->proc_offset,
		       mchk_header->sys_offset);
		printk(KERN_CRIT "  expected %d DCSR 0x%lx PEAR 0x%lx\n",
		       apecs_mcheck_expected, mchk_sysdata->epic_dcsr,
		       mchk_sysdata->epic_pear);

		ptr = (unsigned long *)la_ptr;
		for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
			printk(KERN_CRIT " +%lx %lx %lx\n",
			       i*sizeof(long), ptr[i], ptr[i+1]);
		}
#if 0
		/* doesn't work with MILO */
		show_regs(regs);
#endif
	}
}


/*
 * The System Vector
 */

#if defined(CONFIG_ALPHA_GENERIC) || !defined(CONFIG_ALPHA_PRIMO)
struct alpha_machine_vector mikasa_mv __initmv = {
	vector_name:		"Mikasa",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	DO_APECS_BUS,
	machine_check:		mikasa_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		32,
	irq_probe_mask:		_PROBE_MASK(32),
	update_irq_hw:		mikasa_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	mikasa_device_interrupt,

	init_arch:		apecs_init_arch,
	init_irq:		mikasa_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		mikasa_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(mikasa)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_PRIMO)
struct alpha_machine_vector mikasa_primo_mv __initmv = {
	vector_name:		"Mikasa-Primo",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_CIA_IO,
	DO_CIA_BUS,
	machine_check:		mikasa_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		32,
	irq_probe_mask:		_PROBE_MASK(32),
	update_irq_hw:		mikasa_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	mikasa_device_interrupt,

	init_arch:		cia_init_arch,
	init_irq:		mikasa_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		mikasa_primo_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(mikasa_primo)
#endif

/*
 *	linux/arch/alpha/kernel/sys_sx164.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *
 * Code supporting the SX164 (PCA56+PYXIS).
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_pyxis.h>

#include "proto.h"
#include "pci_impl.h"
#include "machvec_impl.h"


/* Note invert on MASK bits. */
static unsigned long cached_irq_mask;

static inline void
sx164_change_irq_mask(unsigned long mask)
{
	*(vulp)PYXIS_INT_MASK = mask;
	mb();
	*(vulp)PYXIS_INT_MASK;
}

static inline void
sx164_enable_irq(unsigned int irq)
{
	sx164_change_irq_mask(cached_irq_mask |= 1UL << (irq - 16));
}

static void
sx164_disable_irq(unsigned int irq)
{
	sx164_change_irq_mask(cached_irq_mask &= ~(1UL << (irq - 16)));
}

static unsigned int
sx164_startup_irq(unsigned int irq)
{
	sx164_enable_irq(irq);
	return 0;
}

static inline void
sx164_srm_enable_irq(unsigned int irq)
{
	cserve_ena(irq - 16);
}

static void
sx164_srm_disable_irq(unsigned int irq)
{
	cserve_dis(irq - 16);
}

static unsigned int
sx164_srm_startup_irq(unsigned int irq)
{
	sx164_srm_enable_irq(irq);
	return 0;
}

static struct hw_interrupt_type sx164_irq_type = {
	typename:	"SX164",
	startup:	sx164_startup_irq,
	shutdown:	sx164_disable_irq,
	enable:		sx164_enable_irq,
	disable:	sx164_disable_irq,
	ack:		sx164_disable_irq,
	end:		sx164_enable_irq,
};

static struct hw_interrupt_type sx164_srm_irq_type = {
	typename:	"SX164-SRM",
	startup:	sx164_srm_startup_irq,
	shutdown:	sx164_srm_disable_irq,
	enable:		sx164_srm_enable_irq,
	disable:	sx164_srm_disable_irq,
	ack:		sx164_srm_disable_irq,
	end:		sx164_srm_enable_irq,
};

static void 
sx164_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary register of PYXIS */
	pld = *(vulp)PYXIS_INT_REQ;

	/*
	 * For now, AND off any bits we are not interested in:
	 *  HALT (2), timer (6), ISA Bridge (7)
	 * then all the PCI slots/INTXs (8-23)
	 */
	/* Maybe HALT should only be used for SRM console boots? */
	pld &= 0x0000000000ffffc0UL;

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i == 7) {
			isa_device_interrupt(vector, regs);
		} else if (i == 6) {
			continue;
		} else {
			/* if not timer int */
			handle_irq(16 + i, regs);
		}

		*(vulp)PYXIS_INT_REQ = 1UL << i;
		mb();
		*(vulp)PYXIS_INT_REQ;
	}
}

static void
sx164_init_irq(void)
{
	struct hw_interrupt_type *ops;
	long i;

	outb(0, DMA1_RESET_REG);
	outb(0, DMA2_RESET_REG);
	outb(DMA_MODE_CASCADE, DMA2_MODE_REG);
	outb(0, DMA2_MASK_REG);

	init_ISA_irqs();
	init_RTC_irq();

	if (alpha_using_srm) {
		alpha_mv.device_interrupt = srm_device_interrupt;
		ops = &sx164_srm_irq_type;
	}
	else {
		sx164_change_irq_mask(0);
		ops = &sx164_irq_type;
	}

	for (i = 16; i < 40; ++i) {
		/* Make CERTAIN none of the bogus ints get enabled.  */
		if ((0x3b0000 >> i) & 1)
			continue;
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].handler = ops;
	}

	ops->startup(16 + 6);	/* enable timer */
	ops->startup(16 + 7);	/* enable ISA PIC cascade */
}

/*
 * PCI Fixup configuration.
 *
 * Summary @ PYXIS_INT_REQ:
 * Bit      Meaning
 * 0        RSVD
 * 1        NMI
 * 2        Halt/Reset switch
 * 3        MBZ
 * 4        RAZ
 * 5        RAZ
 * 6        Interval timer (RTC)
 * 7        PCI-ISA Bridge
 * 8        Interrupt Line A from slot 3
 * 9        Interrupt Line A from slot 2
 *10        Interrupt Line A from slot 1
 *11        Interrupt Line A from slot 0
 *12        Interrupt Line B from slot 3
 *13        Interrupt Line B from slot 2
 *14        Interrupt Line B from slot 1
 *15        Interrupt line B from slot 0
 *16        Interrupt Line C from slot 3
 *17        Interrupt Line C from slot 2
 *18        Interrupt Line C from slot 1
 *19        Interrupt Line C from slot 0
 *20        Interrupt Line D from slot 3
 *21        Interrupt Line D from slot 2
 *22        Interrupt Line D from slot 1
 *23        Interrupt Line D from slot 0
 *
 * IdSel       
 *   5  32 bit PCI option slot 2
 *   6  64 bit PCI option slot 0
 *   7  64 bit PCI option slot 1
 *   8  Cypress I/O
 *   9  32 bit PCI option slot 3
 * 
 */

static int __init
sx164_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static char irq_tab[5][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 16+ 9, 16+ 9, 16+13, 16+17, 16+21}, /* IdSel 5 slot 2 J17 */
		{ 16+11, 16+11, 16+15, 16+19, 16+23}, /* IdSel 6 slot 0 J19 */
		{ 16+10, 16+10, 16+14, 16+18, 16+22}, /* IdSel 7 slot 1 J18 */
		{    -1,    -1,    -1,	  -1,    -1}, /* IdSel 8 SIO        */
		{ 16+ 8, 16+ 8, 16+12, 16+16, 16+20}  /* IdSel 9 slot 3 J15 */
	};
	const long min_idsel = 5, max_idsel = 9, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

void __init
sx164_init_pci(void)
{
	common_init_pci();
	SMC669_Init(0);
}


/*
 * The System Vector
 */

struct alpha_machine_vector sx164_mv __initmv = {
	vector_name:		"SX164",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	DO_PYXIS_BUS,
	machine_check:		pyxis_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,
	min_io_address:		DEFAULT_IO_BASE,
	min_mem_address:	DEFAULT_MEM_BASE,

	nr_irqs:		40,
	device_interrupt:	sx164_device_interrupt,

	init_arch:		pyxis_init_arch,
	init_irq:		sx164_init_irq,
	init_pit:		common_init_pit,
	init_pci:		sx164_init_pci,
	kill_arch:		NULL,
	pci_map_irq:		sx164_map_irq,
	pci_swizzle:		common_swizzle,
};
ALIAS_MV(sx164)

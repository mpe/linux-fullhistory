/*
 *	linux/arch/alpha/kernel/sys_ruffian.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *
 * Code supporting the RUFFIAN.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_pyxis.h>

#include "proto.h"
#include <asm/hw_irq.h>
#include "pci_impl.h"
#include "machvec_impl.h"

static void
ruffian_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16) {
		/* Note inverted sense of mask bits: */
		/* Make CERTAIN none of the bogus ints get enabled... */
		*(vulp)PYXIS_INT_MASK =
			~((long)mask >> 16) & 0x00000000ffffffbfUL;
		mb();
		/* ... and read it back to make sure it got written.  */
		*(vulp)PYXIS_INT_MASK;
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else
		outb(mask, 0x21);	/* ISA PIC1 */
}

static void
ruffian_ack_irq(unsigned long irq)
{
	if (irq < 16) {
		/* Ack PYXIS ISA interrupt.  */
		*(vulp)PYXIS_INT_REQ = 1L << 7; mb();
		/* ... and read it back to make sure it got written.  */
		*(vulp)PYXIS_INT_REQ;
		if (irq > 7) {
			outb(0x20, 0xa0);
		}
		outb(0x20, 0x20);
	} else {
		/* Ack PYXIS PCI interrupt.  */
		*(vulp)PYXIS_INT_REQ = (1UL << (irq - 16)); mb();
		/* ... and read it back to make sure it got written.  */
		*(vulp)PYXIS_INT_REQ;
	}
}

static void
ruffian_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary register of PYXIS */
	pld = *(vulp)PYXIS_INT_REQ;

	/* For now, AND off any bits we are not interested in:
	 * HALT (2), timer (6), ISA Bridge (7), 21142 (8)
	 * then all the PCI slots/INTXs (12-31) 
	 * flash(5) :DWH:
	 */
	pld &= 0x00000000ffffff9fUL; /* was ffff7f */

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i == 7) { /* if ISA int */
			/* Ruffian does not have the RTC connected to 
			   the CPU timer interrupt.  Instead, it uses the
			   PIT connected to IRQ 0.  So we must detect that
			   and route that specifically to where we expected
			   to find the timer interrupt come in.  */

			/* Copy this code from isa_device_interrupt because
			   we need to hook into int 0 for the timer.  I
			   refuse to soil device_interrupt with ifdefs.  */

			/* Generate a PCI interrupt acknowledge cycle.
			   The PIC will respond with the interrupt
			   vector of the highest priority interrupt
			   that is pending.  The PALcode sets up the
			   interrupts vectors such that irq level L
			   generates vector L.  */

			unsigned int j = *(vuip)PYXIS_IACK_SC & 0xff;
			if (j == 7 && !(inb(0x20) & 0x80)) {
				/* It's only a passive release... */
			} else if (j == 0) {
			  	handle_irq(TIMER_IRQ, -1, regs);
				ruffian_ack_irq(0);
			} else {
				handle_irq(j, j, regs);
			}
		} else { /* if not an ISA int */
			handle_irq(16 + i, 16 + i, regs);
		}

		*(vulp)PYXIS_INT_REQ = 1UL << i; mb();
		*(vulp)PYXIS_INT_REQ; /* read to force the write */
	}
}

static void __init
ruffian_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	/* Invert 6&7 for i82371 */
	*(vulp)PYXIS_INT_HILO  = 0x000000c0UL; mb();
	*(vulp)PYXIS_INT_CNFG  = 0x00002064UL; mb();	 /* all clear */
	*(vulp)PYXIS_INT_MASK  = 0x00000000UL; mb();
	*(vulp)PYXIS_INT_REQ   = 0xffffffffUL; mb();

	outb(0x11,0xA0);
	outb(0x08,0xA1);
	outb(0x02,0xA1);
	outb(0x01,0xA1);
	outb(0xFF,0xA1);
	
	outb(0x11,0x20);
	outb(0x00,0x21);
	outb(0x04,0x21);
	outb(0x01,0x21);
	outb(0xFF,0x21);
	
	/* Send -INTA pulses to clear any pending interrupts ...*/
	*(vuip) PYXIS_IACK_SC;

	/* Finish writing the 82C59A PIC Operation Control Words */
	outb(0x20,0xA0);
	outb(0x20,0x20);
	
	/* Turn on the interrupt controller, the timer interrupt  */
	enable_irq(16 + 7);	/* enable ISA PIC cascade */
	enable_irq(0);		/* enable timer */
	enable_irq(2);		/* enable 2nd PIC cascade */
}


#ifdef BUILDING_FOR_MILO
/*
 * The DeskStation Ruffian motherboard firmware does not place
 * the memory size in the PALimpure area.  Therefore, we use
 * the Bank Configuration Registers in PYXIS to obtain the size.
 */
static unsigned long __init
ruffian_get_bank_size(unsigned long offset)
{
	unsigned long bank_addr, bank, ret = 0;
  
	/* Valid offsets are: 0x800, 0x840 and 0x880
	   since Ruffian only uses three banks.  */
	bank_addr = (unsigned long)PYXIS_MCR + offset;
	bank = *(vulp)bank_addr;
    
	/* Check BANK_ENABLE */
	if (bank & 0x01) {
		static unsigned long size[] __initlocaldata = {
			0x40000000UL, /* 0x00,   1G */ 
			0x20000000UL, /* 0x02, 512M */
			0x10000000UL, /* 0x04, 256M */
			0x08000000UL, /* 0x06, 128M */
			0x04000000UL, /* 0x08,  64M */
			0x02000000UL, /* 0x0a,  32M */
			0x01000000UL, /* 0x0c,  16M */
			0x00800000UL, /* 0x0e,   8M */
			0x80000000UL, /* 0x10,   2G */
		};

		bank = (bank & 0x1e) >> 1;
		if (bank < sizeof(size)/sizeof(*size))
			ret = size[bank];
	}

	return ret;
}
#endif /* BUILDING_FOR_MILO */

static void
ruffian_init_pit (void)
{
	outb(0xb6, 0x43);       /* pit counter 2: speaker */
	outb(0x31, 0x42);
	outb(0x13, 0x42);
}

static void
ruffian_kill_arch (int mode)
{
#if 0
	/* This only causes re-entry to ARCSBIOS */
	/* Perhaps this works for other PYXIS as well?  */
	*(vuip) PYXIS_RESET = 0x0000dead;
	mb();
#endif
}

static int __init
ruffian_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/* We don't know anything about the PCI routing, so leave
	   the IRQ unchanged.  */
	return dev->irq;
}


/*
 * The System Vector
 */

struct alpha_machine_vector ruffian_mv __initmv = {
	vector_name:		"Ruffian",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	DO_PYXIS_BUS,
	machine_check:		pyxis_machine_check,
	max_dma_address:	ALPHA_RUFFIAN_MAX_DMA_ADDRESS,
	min_io_address:		DEFAULT_IO_BASE,
	min_mem_address:	DEFAULT_MEM_BASE,

	nr_irqs:		48,
	irq_probe_mask:		RUFFIAN_PROBE_MASK,
	update_irq_hw:		ruffian_update_irq_hw,
	ack_irq:		ruffian_ack_irq,
	device_interrupt:	ruffian_device_interrupt,

	init_arch:		pyxis_init_arch,
	init_irq:		ruffian_init_irq,
	init_pit:		ruffian_init_pit,
	init_pci:		common_init_pci,
	kill_arch:		ruffian_kill_arch,
	pci_map_irq:		ruffian_map_irq,
	pci_swizzle:		common_swizzle,
};
ALIAS_MV(ruffian)

/*
 * Setup pointers to hardware dependand routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997 by Ralf Baechle
 *
 * $Id: setup.c,v 1.2 1997/07/23 17:40:54 ralf Exp $
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/vector.h>

/*
 * Initial irq handlers.
 */
static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL};

extern asmlinkage void deskstation_handle_int(void);
extern asmlinkage void deskstation_fd_cacheflush(const void *addr, size_t size);
extern struct feature deskstation_tyne_feature;
extern struct feature deskstation_rpc44_feature;

extern void deskstation_machine_reboot(void);
extern void deskstation_machine_halt(void);
extern void deskstation_machine_power_off(void);

#ifdef CONFIG_DESKSTATION_TYNE
unsigned long mips_dma_cache_size = 0;
unsigned long mips_dma_cache_base = KSEG0;

__initfunc(static void tyne_irq_setup(void))
{
	set_except_vector(0, deskstation_handle_int);
	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	request_region(0x20,0x20, "pic1");
	request_region(0xa0,0x20, "pic2");	
	setup_x86_irq(2, &irq2);
}
#endif

#ifdef CONFIG_DESKSTATION_RPC44
__initfunc(static void rpc44_irq_setup(void))
{
	/*
	 * For the moment just steal the TYNE support.  In the
	 * future, we need to consider merging the two -- imp
	 */
	set_except_vector(0, deskstation_handle_int);
	/* set the clock to 100 Hz */
	outb_p(0x34, 0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	request_region(0x20,0x20, "pic1");
	request_region(0xa0,0x20, "pic2");
	setup_x86_irq(2, &irq2);
	set_cp0_status(ST0_IM, IE_IRQ4 | IE_IRQ3 | IE_IRQ2 | IE_IRQ1);
}
#endif

__initfunc(void deskstation_setup(void))
{
	switch(mips_machtype) {
#ifdef CONFIG_DESKSTATION_TYNE
	case MACH_DESKSTATION_TYNE:
		atag = bi_TagFind(tag_dma_cache_size);
		memcpy(&mips_dma_cache_size, TAGVALPTR(atag), atag->size);

		atag = bi_TagFind(tag_dma_cache_base);
		memcpy(&mips_dma_cache_base, TAGVALPTR(atag), atag->size);

		irq_setup = tyne_irq_setup;
                feature = &deskstation_tyne_feature;
                isa_slot_offset = 0xe3000000;		// Will go away
		break;
#endif
#ifdef CONFIG_DESKSTATION_RPC44
	case MACH_DESKSTATION_RPC44:
		irq_setup = rpc44_irq_setup;
		mips_memory_upper = KSEG0 + (32 << 20); /* xxx fixme imp */
		feature = &deskstation_rpc44_feature;	// Will go away
		isa_slot_offset = 0xa0000000;
		break;
#endif
	}
	fd_cacheflush = deskstation_fd_cacheflush;
	keyboard_setup = dtc_keyboard_setup;
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x70,0x10,"rtc");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
}

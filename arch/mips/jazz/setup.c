/* $Id: setup.c,v 1.14 1998/09/16 22:50:40 ralf Exp $
 *
 * Setup pointers to hardware-dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 */
#include <linux/config.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kbd_ll.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/mc146818rtc.h>
#include <asm/bootinfo.h>
#include <asm/keyboard.h>
#include <asm/ide.h>
#include <asm/irq.h>
#include <asm/jazz.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/io.h>
#include <asm/pgtable.h>

/*
 * Initial irq handlers.
 */
static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL};

extern asmlinkage void jazz_handle_int(void);
extern void jazz_keyboard_setup(void);

extern void jazz_machine_restart(char *command);
extern void jazz_machine_halt(void);
extern void jazz_machine_power_off(void);

extern struct ide_ops std_ide_ops;
extern struct rtc_ops jazz_rtc_ops;

void (*board_time_init)(struct irqaction *irq);

__initfunc(static void jazz_time_init(struct irqaction *irq))
{
        /* set the clock to 100 Hz */
        r4030_write_reg32(JAZZ_TIMER_INTERVAL, 9);
        setup_x86_irq(0, irq);
}

__initfunc(static void jazz_irq_setup(void))
{
        set_except_vector(0, jazz_handle_int);
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE,
			  JAZZ_IE_ETHERNET |
			  JAZZ_IE_SCSI     |
			  JAZZ_IE_SERIAL1  |
			  JAZZ_IE_SERIAL2  |
 			  JAZZ_IE_PARALLEL |
			  JAZZ_IE_FLOPPY);
	r4030_read_reg16(JAZZ_IO_IRQ_SOURCE); /* clear pending IRQs */
	r4030_read_reg32(JAZZ_R4030_INVAL_ADDR); /* clear error bits */
	set_cp0_status(ST0_IM, IE_IRQ4 | IE_IRQ3 | IE_IRQ2 | IE_IRQ1);
	/* set the clock to 100 Hz */
	r4030_write_reg32(JAZZ_TIMER_INTERVAL, 9);
	request_region(0x20, 0x20, "pic1");
	request_region(0xa0, 0x20, "pic2");
	setup_x86_irq(2, &irq2);
}

__initfunc(void jazz_setup(void))
{
    tag *atag;

    /*
     * we just check if a tag_screen_info can be gathered
     * in setup_arch(), if yes we don't proceed futher...
     */
    atag = bi_TagFind(tag_screen_info);
    if (!atag) {
	/*
	 * If no, we try to find the tag_arc_displayinfo which is
	 * always created by Milo for an ARC box (for now Milo only
	 * works on ARC boxes :) -Stoned.
	 */
	atag = bi_TagFind(tag_arcdisplayinfo);
	if (atag) {
	    screen_info.orig_x = 
		((mips_arc_DisplayInfo*)TAGVALPTR(atag))->cursor_x;
	    screen_info.orig_y = 
		((mips_arc_DisplayInfo*)TAGVALPTR(atag))->cursor_y;
	    screen_info.orig_video_cols  = 
		((mips_arc_DisplayInfo*)TAGVALPTR(atag))->columns;
	    screen_info.orig_video_lines  = 
		((mips_arc_DisplayInfo*)TAGVALPTR(atag))->lines;
	}
    }

	add_wired_entry (0x02000017, 0x03c00017, 0xe0000000, PM_64K);
	add_wired_entry (0x02400017, 0x02440017, 0xe2000000, PM_16M);
	add_wired_entry (0x01800017, 0x01000017, 0xe4000000, PM_4M);

	irq_setup = jazz_irq_setup;
	keyboard_setup = jazz_keyboard_setup;
	mips_io_port_base = JAZZ_PORT_BASE;
	isa_slot_offset = 0xe3000000;
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
        board_time_init = jazz_time_init;
	/* The RTC is outside the port address space */

	_machine_restart = jazz_machine_restart;
	_machine_halt = jazz_machine_halt;
	_machine_power_off = jazz_machine_power_off;

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &std_ide_ops;
#endif
	conswitchp = &fb_con;
	rtc_ops = &jazz_rtc_ops;
}

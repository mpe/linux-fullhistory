/*
 * FILE NAME
 *	arch/mips/vr41xx/workpad/setup.c
 *
 * BRIEF MODULE DESCRIPTION
 *	Setup for the IBM WorkPad z50.
 *
 * Copyright 2002 Yoichi Yuasa
 *                yuasa@hh.iij4u.or.jp
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/ide.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>

#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/vr41xx/workpad.h>

#ifdef CONFIG_BLK_DEV_INITRD
extern unsigned long initrd_start, initrd_end;
extern void * __rd_start, * __rd_end;
#endif

#ifdef CONFIG_BLK_DEV_IDE
extern struct ide_ops workpad_ide_ops;
#endif

void __init ibm_workpad_setup(void)
{
	set_io_port_base(IO_PORT_BASE);
	ioport_resource.start = IO_PORT_RESOURCE_START;
	ioport_resource.end = IO_PORT_RESOURCE_END;
	iomem_resource.start = IO_MEM_RESOURCE_START;
	iomem_resource.end = IO_MEM_RESOURCE_END;

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = Root_RAM0;
	initrd_start = (unsigned long)&__rd_start;
	initrd_end = (unsigned long)&__rd_end;
#endif

	_machine_restart = vr41xx_restart;
	_machine_halt = vr41xx_halt;
	_machine_power_off = vr41xx_power_off;

	board_time_init = vr41xx_time_init;
	board_timer_setup = vr41xx_timer_setup;

#ifdef CONFIG_FB
	conswitchp = &dummy_con;
#endif

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &workpad_ide_ops;
#endif

	vr41xx_bcu_init();

	vr41xx_cmu_init(0);

#ifdef CONFIG_SERIAL_8250
	vr41xx_siu_init(SIU_RS232C, 0);
#endif
}

/* $Id: cf-enabler.c,v 1.2 1999/12/20 10:14:40 gniibe Exp $
 *
 *  linux/drivers/block/cf-enabler.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *
 *  Enable the CF configuration.
 */

#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>

#define CF_CIS_BASE	0xb8000000
/*
 * 0xB8000000 : Attribute
 * 0xB8001000 : Common Memory
 * 0xBA000000 : I/O
 */

int __init cf_init(void)
{
	outw(0x0042, CF_CIS_BASE+0x0200);
	make_imask_irq(14);
	disable_irq(14);
	return 0;
}

__initcall (cf_init);

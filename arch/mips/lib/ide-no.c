/* $Id: ide-no.c,v 1.2 1998/05/28 03:17:57 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Stub IDE routines to keep Linux from crashing on machine which don't
 * have IDE like the Indy.
 *
 * Copyright (C) 1998 by Ralf Baechle
 */
#include <linux/hdreg.h>
#include <linux/kernel.h>
#include <asm/ptrace.h>
#include <asm/ide.h>

static int no_ide_default_irq(ide_ioreg_t base)
{
	return 0;
}

static ide_ioreg_t no_ide_default_io_base(int index)
{
	return 0;
}

static void no_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base,
                                     int *irq)
{
}

static int no_ide_request_irq(unsigned int irq,
                                void (*handler)(int,void *, struct pt_regs *),
                                unsigned long flags, const char *device,
                                void *dev_id)
{
	panic("no_no_ide_request_irq called - shouldn't happen");
}			

static void no_ide_free_irq(unsigned int irq, void *dev_id)
{
	panic("no_ide_free_irq called - shouldn't happen");
}

static int no_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
	panic("no_ide_check_region called - shouldn't happen");
}

static void no_ide_request_region(ide_ioreg_t from, unsigned int extent,
                                    const char *name)
{
	panic("no_ide_request_region called - shouldn't happen");
}

static void no_ide_release_region(ide_ioreg_t from, unsigned int extent)
{
	panic("no_ide_release_region called - shouldn't happen");
}

struct ide_ops no_ide_ops = {
	&no_ide_default_irq,
	&no_ide_default_io_base,
	&no_ide_init_hwif_ports,
	&no_ide_request_irq,
	&no_ide_free_irq,
	&no_ide_check_region,
	&no_ide_request_region,
	&no_ide_release_region
};

/*
 * include/asm-mips/types.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * IDE routines for typical pc-like standard configurations.
 *
 * Copyright (C) 1998 by Ralf Baechle
 */
#include <linux/hdreg.h>
#include <asm/ptrace.h>
#include <asm/ide.h>

static int std_ide_default_irq(ide_ioreg_t base)
{
	switch (base) {
		case 0x1f0: return 14;
		case 0x170: return 15;
		case 0x1e8: return 11;
		case 0x168: return 10;
		case 0x1e0: return 8;
		case 0x160: return 12;
		default:
			return 0;
	}
}

static ide_ioreg_t std_ide_default_io_base(int index)
{
	switch (index) {
		case 0:	return 0x1f0;
		case 1:	return 0x170;
		case 2: return 0x1e8;
		case 3: return 0x168;
		case 4: return 0x1e0;
		case 5: return 0x160;
		default:
			return 0;
	}
}

static void std_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base,
                                     int *irq)
{
	ide_ioreg_t port = base;
	int i = 8;

	while (i--)
		*p++ = port++;
	*p++ = base + 0x206;
	if (irq != NULL)
		*irq = 0;
}

static int std_ide_request_irq(unsigned int irq,
                                void (*handler)(int,void *, struct pt_regs *),
                                unsigned long flags, const char *device,
                                void *dev_id)
{
	return request_irq(irq, handler, flags, device, dev_id);
}			

static void std_ide_free_irq(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
}

static int std_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
	return check_region(from, extent);
}

static void std_ide_request_region(ide_ioreg_t from, unsigned int extent,
                                    const char *name)
{
	request_region(from, extent, name);
}

static void std_ide_release_region(ide_ioreg_t from, unsigned int extent)
{
	release_region(from, extent);
}

struct ide_ops std_ide_ops = {
	&std_ide_default_irq,
	&std_ide_default_io_base,
	&std_ide_init_hwif_ports,
	&std_ide_request_irq,
	&std_ide_free_irq,
	&std_ide_check_region,
	&std_ide_request_region,
	&std_ide_release_region
};

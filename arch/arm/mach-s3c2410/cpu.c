/* linux/arch/arm/mach-s3c2410/cpu.c
 *
 * Copyright (c) 2004 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX CPU Support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/delay.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/regs-gpio.h>

#include "cpu.h"
#include "s3c2410.h"
#include "s3c2440.h"

struct cpu_table {
	unsigned long	idcode;
	unsigned long	idmask;
	void		(*map_io)(struct map_desc *mach_desc, int size);
	int		(*init)(void);
	const char	*name;
};

/* table of supported CPUs */

static const char name_s3c2410[]  = "S3C2410";
static const char name_s3c2440[]  = "S3C2440";
static const char name_s3c2410a[] = "S3C2410A";
static const char name_s3c2440a[] = "S3C2440A";

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode	= 0x32410000,
		.idmask = 0xffffffff,
		.map_io = s3c2410_map_io,
		.init   = s3c2410_init,
		.name   = name_s3c2410
	},
	{
		.idcode	= 0x3241002,
		.idmask = 0xffffffff,
		.map_io = s3c2410_map_io,
		.init   = s3c2410_init,
		.name   = name_s3c2410a
	},
	{
		.idcode	= 0x32440000,
		.idmask = 0xffffffff,
		.map_io = s3c2440_map_io,
		.init   = s3c2440_init,
		.name   = name_s3c2440
	},
	{
		.idcode	= 0x32440001,
		.idmask = 0xffffffff,
		.map_io = s3c2440_map_io,
		.init   = s3c2440_init,
		.name   = name_s3c2440a
	}
};

/* minimal IO mapping */

static struct map_desc s3c_iodesc[] __initdata = {
	IODESC_ENT(GPIO),
	IODESC_ENT(IRQ),
	IODESC_ENT(MEMCTRL),
	IODESC_ENT(UART)
};


static struct cpu_table *
s3c_lookup_cpu(unsigned long idcode)
{
	struct cpu_table *tab;
	int count;

	tab = cpu_ids;
	for (count = 0; count < ARRAY_SIZE(cpu_ids); count++) {
		if ((idcode & tab->idmask) == tab->idcode)
			return tab;
	}

	return NULL;
}

static struct cpu_table *cpu;

void __init s3c24xx_init_io(struct map_desc *mach_desc, int size)
{
	unsigned long idcode;

	/* initialise the io descriptors we need for initialisation */
	iotable_init(s3c_iodesc, ARRAY_SIZE(s3c_iodesc));

	idcode = __raw_readl(S3C2410_GSTATUS1);
	cpu = s3c_lookup_cpu(idcode);

	if (cpu == NULL) {
		printk(KERN_ERR "Unknown CPU type 0x%08lx\n", idcode);
		panic("Unknown S3C24XX CPU");
	}

	if (cpu->map_io == NULL || cpu->init == NULL) {
		printk(KERN_ERR "CPU %s support not enabled\n", cpu->name);
		panic("Unsupported S3C24XX CPU");
	}

	printk("CPU %s (id 0x%08lx)\n", cpu->name, idcode);

	(cpu->map_io)(mach_desc, size);
}

static int __init s3c_arch_init(void)
{
	// do the correct init for cpu

	if (cpu == NULL)
		panic("s3c_arch_init: NULL cpu\n");

	return (cpu->init)();
}

arch_initcall(s3c_arch_init);

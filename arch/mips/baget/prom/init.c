/*
 * init.c: PROM library initialisation code.
 *
 * Copyright (C) 1998 Gleb Raiko & Vladimir Roganov 
 *
 * $Id$
 */
#include <linux/init.h>
#include <asm/bootinfo.h>

char arcs_cmdline[CL_SIZE];

int __init prom_init(unsigned int mem_upper)
{
	mips_memory_upper = mem_upper;
	mips_machgroup  = MACH_GROUP_UNKNOWN;
	mips_machtype   = MACH_UNKNOWN;
	arcs_cmdline[0] = 0;
	return 0;
}

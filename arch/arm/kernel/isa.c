/*
 * arch/arm/kernel/isa.c
 *
 * ISA shared memory and I/O port support
 *
 * Copyright (C) 1999 Phil Blundell
 */

/* 
 * Nothing about this is actually ARM specific.  One day we could move
 * it into kernel/resource.c or some place like that.  
 */

#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/linkage.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/init.h>

static unsigned int isa_membase, isa_portbase, isa_portshift;

static ctl_table ctl_isa_vars[4] = {
	{BUS_ISA_MEM_BASE, "membase", &isa_membase, 
	 sizeof(isa_membase), 0444, NULL, &proc_dointvec},
	{BUS_ISA_PORT_BASE, "portbase", &isa_portbase, 
	 sizeof(isa_portbase), 0444, NULL, &proc_dointvec},
	{BUS_ISA_PORT_SHIFT, "portshift", &isa_portshift, 
	 sizeof(isa_portshift), 0444, NULL, &proc_dointvec},
	{0}
};

static struct ctl_table_header *isa_sysctl_header;

static ctl_table ctl_isa[2] = {{BUS_ISA, "isa", NULL, 0, 0555, ctl_isa_vars},
			       {0}};
static ctl_table ctl_bus[2] = {{CTL_BUS, "bus", NULL, 0, 0555, ctl_isa},
			       {0}};

__initfunc(void
register_isa_ports(unsigned int membase, unsigned int portbase, unsigned int portshift))
{
	isa_membase = membase;
	isa_portbase = portbase;
	isa_portshift = portshift;
	isa_sysctl_header = register_sysctl_table(ctl_bus, 0);
}

/*
 * tags.c: Initialize the arch tags the way the MIPS kernel setup
 *         expects.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: tags.c,v 1.2 1998/03/27 08:53:48 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/addrspace.h>
#include <asm/sgialib.h>
#include <asm/bootinfo.h>
#include <asm/sgimc.h>

/* XXX This tag thing is a fucking rats nest, I'm very inclined to completely
 * XXX rework the MIPS people's multi-arch code _NOW_.
 */

static unsigned long machtype_SGI_INDY = MACH_SGI_INDY;
static unsigned long machgroup_SGI = MACH_GROUP_SGI;
static unsigned long memlower_SGI_INDY = (KSEG0 + SGIMC_SEG0_BADDR);
static unsigned long cputype_SGI_INDY = CPU_R4400SC;
static unsigned long tlb_entries_SGI_INDY = 48;
static unsigned long dummy_SGI_INDY = 0;
static struct drive_info_struct dummy_dinfo_SGI_INDY = { { 0, }, };
char arcs_cmdline[CL_SIZE];

#define TAG(t,l)   {tag_##t,(l)} /* XXX RATS NEST CODE!!! XXX */
#define TAGVAL(v)  (void*)&(v)   /* XXX FUCKING LOSING!!! XXX */

tag_def taglist_sgi_indy[] = {
	{TAG(machtype, ULONGSIZE), TAGVAL(machtype_SGI_INDY)},
	{TAG(machgroup, ULONGSIZE), TAGVAL(machgroup_SGI)},
	{TAG(memlower, ULONGSIZE), TAGVAL(memlower_SGI_INDY)},
	{TAG(cputype, ULONGSIZE), TAGVAL(cputype_SGI_INDY)},
	{TAG(tlb_entries, ULONGSIZE), TAGVAL(tlb_entries_SGI_INDY)},
	{TAG(vram_base, ULONGSIZE), TAGVAL(dummy_SGI_INDY)},
	{TAG(drive_info, DRVINFOSIZE), TAGVAL(dummy_dinfo_SGI_INDY)},
	{TAG(mount_root_rdonly, ULONGSIZE), TAGVAL(dummy_SGI_INDY)},
	{TAG(command_line, CL_SIZE), TAGVAL(arcs_cmdline[0])},
	{TAG(dummy, 0), NULL}
	/* XXX COLOSTOMY BAG!!!! XXX */
};

__initfunc(void prom_setup_archtags(void))
{
	tag_def *tdp = &taglist_sgi_indy[0];
	tag *tp;

	tp = (tag *) (mips_memory_upper - sizeof(tag));
	while(tdp->t.tag != tag_dummy) {
		unsigned long size;
		char *d;

		*tp = tdp->t;
		size = tp->size;
		d = (char *) tdp->d;
		tp = (tag *)(((unsigned long)tp) - (tp->size));
		if(size)
			memcpy(tp, d, size);

		tp--;
		tdp++;
	}
	*tp = tdp->t; /* copy last dummy element over */
}

/* ranges.c: Handle ranges in newer proms for obio.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>
#include <asm/oplib.h>

struct linux_prom_ranges promlib_obio_ranges[PROMREG_MAX];
struct linux_prom_ranges promlib_sbus_ranges[PROMREG_MAX];
int num_obio_ranges, num_sbus_ranges;

/* Adjust register values based upon the ranges parameters. */
void
prom_adjust_regs(struct linux_prom_registers *regp, int nregs,
		 struct linux_prom_ranges *rangep, int nranges)
{
	int regc, rngc;

	for(regc=0; regc < nregs; regc++) {
		for(rngc=0; rngc < nranges; rngc++)
			if(regp[regc].which_io == rangep[rngc].ot_child_space)
				break; /* Fount it */
		if(rngc==nranges) /* oops */
			prom_printf("adjust_regs: Could not find range with matching bus type...\n");
		regp[regc].which_io = rangep[rngc].ot_parent_space;
		regp[regc].phys_addr += rangep[rngc].ot_parent_base;
	}

	return;
}

void
prom_adjust_ranges(struct linux_prom_ranges *ranges1, int nranges1,
		   struct linux_prom_ranges *ranges2, int nranges2)
{
	int rng1c, rng2c;

	for(rng1c=0; rng1c < nranges1; rng1c++) {
		for(rng2c=0; rng2c < nranges2; rng2c++)
			if(ranges1[rng1c].ot_child_space ==
			   ranges2[rng2c].ot_child_space) break;
		if(rng2c == nranges2) /* oops */
			prom_printf("adjust_ranges: Could not find matching bus type...\n");
		ranges1[rng1c].ot_parent_space = ranges2[rng2c].ot_parent_space;
		ranges1[rng1c].ot_parent_base += ranges2[rng2c].ot_parent_base;
	}

	return;
}

/* Apply probed obio ranges to registers passed, if no ranges return. */
void
prom_apply_obio_ranges(struct linux_prom_registers *regs, int nregs)
{
	if(!num_obio_ranges) return;
	prom_adjust_regs(regs, nregs, promlib_obio_ranges, num_obio_ranges);
	return;
}

/* Apply probed sbus ranges to registers passed, if no ranges return. */
void
prom_apply_sbus_ranges(struct linux_prom_registers *regs, int nregs)
{
	if(!num_sbus_ranges) return;
	prom_adjust_regs(regs, nregs, promlib_sbus_ranges, num_sbus_ranges);
	return;
}

void
prom_ranges_init(void)
{
	int node, obio_node, sbus_node;
	int success;

	num_obio_ranges = 0;
	num_sbus_ranges = 0;

	/* Check for obio and sbus ranges. */
	node = prom_getchild(prom_root_node);
	obio_node = prom_searchsiblings(node, "obio");
	sbus_node = prom_searchsiblings(node, "iommu");
	if(sbus_node) {
		sbus_node = prom_getchild(sbus_node);
		sbus_node = prom_searchsiblings(sbus_node, "sbus");
	}

	if(obio_node) {
		success = prom_getproperty(obio_node, "ranges",
					   (char *) promlib_obio_ranges,
					   sizeof(promlib_obio_ranges));
		if(success != -1)
			num_obio_ranges = (success/sizeof(struct linux_prom_ranges));
	}

	if(sbus_node) {
		success = prom_getproperty(sbus_node, "ranges",
					   (char *) promlib_sbus_ranges,
					   sizeof(promlib_sbus_ranges));
		if(success != -1)
			num_sbus_ranges = (success/sizeof(struct linux_prom_ranges));
	}

	if(num_obio_ranges || num_sbus_ranges)
		prom_printf("PROMLIB: obio_ranges %d sbus_ranges %d\n",
			    num_obio_ranges, num_sbus_ranges);

	return;
}

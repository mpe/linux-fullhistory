/* $Id: fhc.h,v 1.1 1997/08/08 04:26:40 davem Exp $
 * fhc.h: Structures for central/fhc pseudo driver on Sunfire/Starfire/Wildfire.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_FHC_H
#define _SPARC64_FHC_H

#include <asm/firehose.h>
#include <asm/oplib.h>

struct linux_fhc;

struct linux_central {
	struct linux_fhc		*child;
	int				prom_node;
	char				prom_name[64];

	struct linux_prom_ranges	central_ranges[PROMREG_MAX];
	int				num_central_ranges;
};

struct linux_fhc {
	struct linux_fhc		*next;
	struct linux_central		*parent;	/* NULL if not central FHC */
	struct fhc_regs			fhc_regs;
	int				prom_node;
	char				prom_name[64];

	struct linux_prom_ranges	fhc_ranges[PROMREG_MAX];
	int				num_fhc_ranges;
};

extern struct linux_central *central_bus;

extern void prom_apply_central_ranges(struct linux_central *central, 
				      struct linux_prom_registers *regs,
				      int nregs);

extern void prom_apply_fhc_ranges(struct linux_fhc *fhc, 
				   struct linux_prom_registers *regs,
				  int nregs);

#endif /* !(_SPARC64_FHC_H) */

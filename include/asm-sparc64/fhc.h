/* $Id: fhc.h,v 1.4 1998/12/14 12:18:20 davem Exp $
 * fhc.h: Structures for central/fhc pseudo driver on Sunfire/Starfire/Wildfire.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_FHC_H
#define _SPARC64_FHC_H

#include <linux/timer.h>

#include <asm/firehose.h>
#include <asm/oplib.h>

struct linux_fhc;

struct clock_board_regs {
	u8	control;
	u8	_unused1[0x10 - 0x01];
	u8	stat1;
	u8	_unused2[0x10 - 0x01];
	u8	stat2;
	u8	_unused3[0x10 - 0x01];
	u8	pwr_stat;
	u8	_unused4[0x10 - 0x01];
	u8	pwr_presence;
	u8	_unused5[0x10 - 0x01];
	u8	temperature;
	u8	_unused6[0x10 - 0x01];
	u8	irq_diag;
	u8	_unused7[0x10 - 0x01];
	u8	pwr_stat2;
	u8	_unused8[0x10 - 0x01];
};

#define CLOCK_CTRL_LLED		0x04	/* Left LED, 0 == on */
#define CLOCK_CTRL_MLED		0x02	/* Mid LED, 1 == on */
#define CLOCK_CTRL_RLED		0x01	/* RIght LED, 1 == on */

struct linux_central {
	struct linux_fhc		*child;
	volatile u8			*cfreg;
	struct clock_board_regs		*clkregs;
	volatile u8			*clkver;
	int				slots;
	int				prom_node;
	char				prom_name[64];

	struct linux_prom_ranges	central_ranges[PROMREG_MAX];
	int				num_central_ranges;
};

struct linux_fhc {
	struct linux_fhc		*next;
	struct linux_central		*parent;	/* NULL if not central FHC */
	struct fhc_regs			fhc_regs;
	int				board;
	int				jtag_master;
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

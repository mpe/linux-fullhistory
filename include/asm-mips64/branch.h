/* $Id: branch.h,v 1.1 1999/08/21 22:19:17 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Branch and jump emulation.
 *
 * Copyright (C) 1996, 1997, 1998, 1999 by Ralf Baechle
 */
#include <asm/ptrace.h>

extern inline int delay_slot(struct pt_regs *regs)
{
	return regs->cp0_cause & CAUSEF_BD;
}

extern int __compute_return_epc(struct pt_regs *regs);

extern inline int compute_return_epc(struct pt_regs *regs)
{
	if (!delay_slot(regs)) {
		regs->cp0_epc += 4;
		return 0;
	}

	return __compute_return_epc(regs);
}

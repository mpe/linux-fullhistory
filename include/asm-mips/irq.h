/*
 * include/asm-mips/irq.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH
 * written by Ralf Baechle
 *
 */
#ifndef __ASM_MIPS_IRQ_H
#define __ASM_MIPS_IRQ_H

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif /* __ASM_MIPS_IRQ_H */

/*
 * Makefile for MIPS Linux main source directory
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 */
#ifndef __ASM_MIPS_REG_H
#define __ASM_MIPS_REG_H

/*
 * This defines/structures correspond to the register layout on stack -
 * if the order here is changed, it needs to be updated in
 * include/asm-mips/stackframe.h
 */
#define EF_REG1       5
#define EF_REG2       6
#define EF_REG3       7
#define EF_REG4       8
#define EF_REG5       9
#define EF_REG6       10
#define EF_REG7       11
#define EF_REG8       12
#define EF_REG9       13
#define EF_REG10      14
#define EF_REG11      15
#define EF_REG12      16
#define EF_REG13      17
#define EF_REG14      18
#define EF_REG15      19
#define EF_REG16      20
#define EF_REG17      21
#define EF_REG18      22
#define EF_REG19      23
#define EF_REG20      24
#define EF_REG21      25
#define EF_REG22      26
#define EF_REG23      27
#define EF_REG24      28
#define EF_REG25      29
/*
 * k0/k1 unsaved
 */
#define EF_REG28      30
#define EF_REG29      31
#define EF_REG30      32
#define EF_REG31      33

/*
 * Saved special registers
 */
#define EF_LO         34
#define EF_HI         35

/*
 * saved cp0 registers
 */
#define EF_CP0_STATUS 36
#define EF_CP0_EPC    37
#define EF_CP0_CAUSE  38

/*
 * Some goodies
 */
#define EF_INTERRUPT  39
#define EF_ORIG_REG2  40

#define EF_SIZE		(41*4)

/*
 * Map register number into core file offset.
 */
#define CORE_REG(reg, ubase) \
	(((unsigned long *)((unsigned long)(ubase)))[reg])

#endif /* __ASM_MIPS_REG_H */

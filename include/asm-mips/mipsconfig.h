/*
 * linux/include/asm-mips/mipsconfig.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 by Waldorf Electronics
 * written by Ralf Baechle
 *
 */
#ifndef __ASM_MIPS_MIPS_CONFIG_H
#define __ASM_MIPS_MIPS_CONFIG_H

/*
 * This is the virtual address to which all ports are being mapped.
 * Must be a value that can be load with a lui instruction.
 */
#define PORT_BASE		0xe0000000

/* #define NUMBER_OF_TLB_ENTRIES	48  */ /* see bootinfo.h -- Andy */
#define NUMBER_OF_TLB_ENTRIES	48

/*
 * Pagetables are 4MB mapped at 0xe3000000
 * Must be a value that can be load with a lui instruction.
 */
#define TLBMAP			0xe4000000

/*
 * The virtual address where we'll map the pagetables
 * For a base address of 0xe3000000 this is 0xe338c000
 * For a base address of 0xe4000000 this is 0xe4390000
 * FIXME: Gas miscomputes the following expression!
#define TLB_ROOT		(TLBMAP + (TLBMAP >> (12-2)))
 */
#define TLB_ROOT		0xe4390000

/*
 * This ASID is reserved for the swapper
 */
#define SWAPPER_ASID		0

#endif /* __ASM_MIPS_MIPS_CONFIG_H */

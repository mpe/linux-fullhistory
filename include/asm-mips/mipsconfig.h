/*
 * include/asm-mips/mipsconfig.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1997 by Ralf Baechle
 */
#ifndef __ASM_MIPS_MIPSCONFIG_H
#define __ASM_MIPS_MIPSCONFIG_H

/*
 * This is the virtual address to which all ports are being mapped.
 * Must be a value that can be load with a lui instruction.
 */
#ifndef PORT_BASE
#if !defined (__LANGUAGE_ASSEMBLY__)
extern unsigned long port_base;
#endif
#define PORT_BASE port_base
#endif

/* Pgdir is 1 page mapped at 0xff800000. */
#define TLBMAP			0xff800000

/* The virtual address where we'll map the pgdir. */
#define TLB_ROOT		0xff000000

#endif /* __ASM_MIPS_MIPSCONFIG_H */

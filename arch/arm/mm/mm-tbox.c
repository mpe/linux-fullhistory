/*
 * arch/arm/mm/mm-tbox.c
 *  from arch/arm/mm/mm-ebsa110.c
 *
 * Extra MM routines for the Tbox architecture
 *
 * Copyright (C) 1998 Phil Blundell
 * Copyright (C) 1998 Russell King
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/proc/mm-init.h>
 
 
/*    Logical    Physical
 * 0xffff1000	0x00100000	DMA registers
 * 0xffff2000	0x00200000	MPEG
 * 0xffff3000	0x00300000	FPGA1 local control
 * 0xffff4000	0x00400000	External serial
 * 0xffff5000	0x00500000	Internal serial
 * 0xffff6000	0x00600000	Parallel
 * 0xffff7000	0x00700000	Interrupt control
 * 0xffff8000	0x00800000	Computer video
 * 0xffff9000	0x00900000	Control register 0
 * 0xffffs000	0x00a00000	Control register 1
 * 0xffffb000	0x00b00000	Control register 2
 * 0xffffc000	0x00c00000	FPGA2 local control
 * 0xffffd000	0x00d00000	Interrupt reset
 * 0xffffe000	0x00e00000	MPEG DMA throttle
 */
 
#define MAPPING								\
  	{ 0xffff0000, 0x01000000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff1000, 0x00100000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff2000, 0x00200000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff3000, 0x00300000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff4000, 0x00400000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xfe000000, 0x00400000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff5000, 0x00500000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff6000, 0x00600000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff7000, 0x00700000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff8000, 0x00800000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffff9000, 0x00900000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffffa000, 0x00a00000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffffb000, 0x00b00000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffffc000, 0x00c00000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffffd000, 0x00d00000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffffe000, 0x00e00000, 0x00001000, DOMAIN_IO, 0, 1 }

#include "mm-armv.c"

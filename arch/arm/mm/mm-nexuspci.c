/*
 * arch/arm/mm/mm-nexuspci.c
 *  from arch/arm/mm/mm-ebsa110.c
 *
 * Extra MM routines for the NexusPCI architecture
 *
 * Copyright (C) 1998 Phil Blundell
 * Copyright (C) 1998 Russell King
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/proc/mm-init.h>
 
#define MAPPING								\
 	{ 0xfff00000, 0x10000000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffe00000, 0x20000000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xffc00000, 0x60000000, 0x00001000, DOMAIN_IO, 0, 1 },	\
 	{ 0xfe000000, 0x80000000, 0x00100000, DOMAIN_IO, 0, 1 },	\
 	{ 0xfd000000, 0x88000000, 0x00100000, DOMAIN_IO, 0, 1 }

#include "mm-armv.c"

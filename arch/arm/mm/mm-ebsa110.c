/*
 * arch/arm/mm/mm-ebsa110.c
 *
 * Extra MM routines for the EBSA-110 architecture
 *
 * Copyright (C) 1998 Russell King
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/proc/mm-init.h>
 
#define MAPPING \
	{ IO_BASE - PGDIR_SIZE	, 0xc0000000	, PGDIR_SIZE	, DOMAIN_IO, 0, 1 }, \
	{ IO_BASE		, IO_START	, IO_SIZE	, DOMAIN_IO, 0, 1 }

#include "mm-armv.c"

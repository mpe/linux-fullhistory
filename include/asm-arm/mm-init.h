/*
 * linux/include/asm-arm/mm-init.h
 *
 * Copyright (C) 1997,1998 Russell King
 *
 * Contained within are structures to describe how to set up the
 * initial memory map.  It includes both a processor-specific header
 * for parsing these structures, and an architecture-specific header
 * to fill out the structures.
 */
#ifndef __ASM_MM_INIT_H
#define __ASM_MM_INIT_H

typedef enum {
	// physical address is absolute
	init_mem_map_absolute,
	/* physical address is relative to start_mem
	 *  as passed in paging_init
	 */
	init_mem_map_relative_start_mem
} init_memmap_type_t;

typedef struct {
	init_memmap_type_t type;
	unsigned long physical_address;
	unsigned long virtual_address;
	unsigned long size;
} init_memmap_t;

#define INIT_MEM_MAP_SENTINEL { init_mem_map_absolute, 0, 0, 0 }
#define INIT_MEM_MAP_ABSOLUTE(p,l,s) { init_mem_map_absolute,p,l,s }
#define INIT_MEM_MAP_RELATIVE(o,l,s) { init_mem_map_relative_start_mem,o,l,s }

/*
 * Within this file, initialise an array of init_mem_map_t's
 * to describe your initial memory mapping structure.
 */
#include <asm/arch/mm-init.h>

/*
 * Contained within this file is code to read the array
 * of init_mem_map_t's created above.
 */
#include <asm/proc/mm-init.h>

#endif

/*
 * arch/arm/mm/mm-shark.c
 *
 * by Alexander.Schulz@stud.uni-karlsruhe.de
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>

#include "map.h"

struct map_desc io_desc[] __initdata = {
	{ IO_BASE	, IO_START	, IO_SIZE	, DOMAIN_IO, 0, 1, 0, 0 },
	{ FB_BASE	, FB_START	, FB_SIZE	, DOMAIN_IO, 0, 1, 0, 0 },
	{ FBREG_BASE	, FBREG_START	, FBREG_SIZE	, DOMAIN_IO, 0, 1, 0, 0 }
};


#define SIZEOFMAP (sizeof(io_desc) / sizeof(io_desc[0]))

unsigned int __initdata io_desc_size = SIZEOFMAP;

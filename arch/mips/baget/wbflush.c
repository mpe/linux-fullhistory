/*
 * Setup the right wbflush routine for Baget/MIPS.
 *
 * Copyright (C) 1999 Gleb Raiko & Vladimir Roganov
 */

#include <asm/bootinfo.h>
#include <asm/init.h>

void (*__wbflush) (void);

static void wbflush_baget(void);

__initfunc(void wbflush_setup(void))
{
	__wbflush = wbflush_baget;
}

/*
 * Baget/MIPS doesnt need to write back the WB.
 */
static void wbflush_baget(void)
{
}

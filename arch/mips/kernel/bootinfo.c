/*
 * arch/mips/kernel/bootinfo.c
 *
 * Copyright (C) 1995  Ralf Baechle
 *
 * Kernel data passed by the loader 
 */
#include <asm/bootinfo.h>

/*
 * Initialise this structure so that it will be placed in the
 * .data section of the object file
 */
struct bootinfo boot_info = BOOT_INFO;

/*
 * linux/include/asm-arm/arch-nexuspci/uncompress.h
 *  from linux/include/asm-arm/arch-ebsa110/uncompress.h
 *
 * Copyright (C) 1996,1997,1998 Russell King
 */

/*
 * This does not append a newline
 */
static void puts(const char *s)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

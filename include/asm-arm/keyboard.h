/*
 * linux/include/asm-arm/keyboard.h
 *
 * Keyboard driver definitions for ARM
 *
 * (C) 1998 Russell King
 */
#ifndef __ASM_ARM_KEYBOARD_H
#define __ASM_ARM_KEYBOARD_H

/*
 * We provide a unified keyboard interface when in VC_MEDIUMRAW
 * mode.  This means that all keycodes must be common between
 * all supported keyboards.  This unfortunately puts us at odds
 * with the PC keyboard interface chip... but we can't do anything
 * about that now.
 */
#ifdef __KERNEL__

#include <asm/arch/keyboard.h>

#endif /* __KERNEL__ */

#endif /* __ASM_ARM_KEYBOARD_H */

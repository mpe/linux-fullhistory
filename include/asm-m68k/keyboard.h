/*
 *  linux/include/asm-m68k/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 */

/*
 *  This file contains the m68k architecture specific keyboard definitions
 */

#ifndef __M68K_KEYBOARD_H
#define __M68K_KEYBOARD_H

#ifdef __KERNEL__

#define TRANSLATE_SCANCODES		0
#define USE_MACHDEP_ABSTRACTION		1
#include <asm/machdep.h>

#endif /* __KERNEL__ */

#endif /* __ASMm68k_KEYBOARD_H */

/*
 *  linux/include/asm-m68k/system.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

/*
 * 680x0 support added by Hamish Macdonald
 */

#ifndef _M68K_SYSTEM_H
#define _M68K_SYSTEM_H

#include <linux/config.h> /* get configuration macros */

#if defined(CONFIG_ATARI) && !defined(CONFIG_AMIGA) && !defined(CONFIG_MAC)
/* block out HSYNC on the atari */
#define sti() __asm__ __volatile__ ("andiw #0xfbff,sr": : : "memory")
#else /* portable version */
#define sti() __asm__ __volatile__ ("andiw #0xf8ff,sr": : : "memory")
#endif /* machine compilation types */ 
#define cli() __asm__ __volatile__ ("oriw  #0x0700,sr": : : "memory")
#define nop() __asm__ __volatile__ ("nop"::)

#define save_flags(x) \
__asm__ __volatile__("movew sr,%0":"=d" (x) : /* no input */ :"memory")

#define restore_flags(x) \
__asm__ __volatile__("movew %0,sr": /* no outputs */ :"d" (x) : "memory")

#define iret() __asm__ __volatile__ ("rte": : :"memory", "sp", "cc")

#define move_to_user_mode() \
    __asm__ __volatile__ ("movel sp,usp\n\t"     /* setup user sp       */ \
			  "movec %0,msp\n\t"     /* setup kernel sp     */ \
			  "andiw #0xdfff,sr"     /* return to user mode */ \
			  : /* no output */                                \
			  : "r" (current->kernel_stack_page + PAGE_SIZE)   \
			  : "memory", "sp")

static inline void clear_fpu(void) {
	unsigned long nilstate = 0;
	__asm__ __volatile__ ("frestore %0@" : : "a" (&nilstate));
}

#define halt() \
        __asm__ __volatile__ ("halt")

#endif /* _M68K_SYSTEM_H */

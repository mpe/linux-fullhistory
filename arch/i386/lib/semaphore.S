/*
 *  linux/arch/i386/lib/semaphore.S
 *
 *  Copyright (C) 1996  Linus Torvalds
 */

#include <linux/linkage.h>

/*
 * "down_failed" is called with the eventual return address
 * in %eax, and the address of the semaphore in %ecx. We need
 * to call "__down()", and then re-try until we succeed..
 */
ENTRY(__down_failed)
	pushl %eax	/* return address */
	pushl %edx	/* save %edx */
1:	pushl %ecx	/* save %ecx (and argument) */
	call SYMBOL_NAME(__down)
	popl %ecx	/* restore %ecx (count on __down not changing it) */
#ifdef __SMP__
	lock
#endif
	decl (%ecx)
	js 1b
	popl %edx	/* restore %edx */
	ret

ENTRY(__up_wakeup)
	pushl %eax	/* return address */
	pushl %edx	/* save %edx */
	pushl %ecx	/* save %ecx (and argument) */
	call SYMBOL_NAME(__up)
	popl %ecx	/* restore %ecx (count on __up not changing it) */
	popl %edx	/* restore %edx */
	ret

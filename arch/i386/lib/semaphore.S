/*
 *  linux/arch/i386/lib/semaphore.S
 *
 *  Copyright (C) 1996  Linus Torvalds
 */

#include <linux/linkage.h>

/*
 * "down_failed" is called with the eventual return address
 * in %eax, and the address of the semaphore in %ecx. We need
 * to increment the number of waiters on the semaphore,
 * call "__down()", and then eventually return to try again.
 */
.globl down_failed
ALIGN
down_failed:
	pushl %eax
	pushl %ecx
	call SYMBOL_NAME(__down)
	popl %ecx
	ret

.globl up_wakeup
ALIGN
up_wakeup:
	pushl %eax
	pushl %ecx
	call SYMBOL_NAME(__up)
	popl %ecx
	ret

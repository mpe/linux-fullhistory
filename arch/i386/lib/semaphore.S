/*
 *  linux/arch/i386/lib/semaphore.S
 *
 *  Copyright (C) 1996  Linus Torvalds
 */

#include <linux/linkage.h>

/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 */
ENTRY(__down_failed)
	pushl %eax	/* return address */
	pushl %edx	/* save %edx */
	pushl %ecx	/* save %ecx (and argument) */
	call SYMBOL_NAME(__down)
	popl %ecx	/* restore %ecx (count on __down not changing it) */
	popl %edx	/* restore %edx */
	ret

ENTRY(__down_failed_interruptible)
	pushl %eax	/* return address */
	pushl %edx	/* save %edx */
	pushl %ecx	/* save %ecx (and argument) */
	call SYMBOL_NAME(__down_interruptible)
	popl %ecx	/* restore %ecx (count on __down_interruptible not changing it) */
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

/* locks.S: Wheee... I'm coding Intel assembly...
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/linkage.h>

	/* Caller does atomic increment on current->lock_depth,
	 * if it was found to originally be zero then we get here,
	 * %eax contains callers PC and %edx holds this cpu ID.
	 */
ENTRY(__lock_kernel)
1:
	lock
	btsl	$0, SYMBOL_NAME(kernel_flag)
	jnc	3f
2:
	btl	%dl, SYMBOL_NAME(smp_invalidate_needed)
	jnc	0f
	lock
	btrl	%dl, SYMBOL_NAME(smp_invalidate_needed)
	jnc	0f
	pushl	%eax
	movl	%cr3, %eax
	movl	%eax, %cr3
	popl	%eax
0:
	btl	$0, SYMBOL_NAME(kernel_flag)
	jc	2b
	jmp	1b

3:
	movb	%dl, SYMBOL_NAME(active_kernel_processor)
	ret

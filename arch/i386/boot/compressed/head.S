/*
 *  linux/boot/head.S
 *
 *  Copyright (C) 1991, 1992, 1993  Linus Torvalds
 */

/*
 *  head.S contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00001000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory. [According to comments etc elsewhere on a compressed
 * kernel it will end up at 0x1000 + 1Mb I hope so as I assume this. - AC]
 *
 * In SMP mode we keep this page safe. Really we ought to shuffle things and
 * put the trampoline here. - AC. An SMP trampoline enters with %cx holding
 * the stack base.
 *
 * Page 0 is deliberately kept safe, since System Management Mode code in 
 * laptops may need to access the BIOS data stored there.  This is also
 * useful for future device drivers that either access the BIOS via VM86 
 * mode.
 */
.text

#define __ASSEMBLY__
#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/segment.h>

	.globl startup_32
	
startup_32:
	cld
	cli
	movl $(KERNEL_DS),%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
#ifdef __SMP__
	orw %bx,%bx			# What state are we in BX=1 for SMP
					# 0 for boot
	jz  2f				# Initial boot

/*
 *	We are trampolining an SMP processor
 */
	mov %ax,%ss
	xorl %eax,%eax			# Back to 0
	mov  %cx,%ax			# SP low 16 bits
	movl %eax,%esp
	pushl 0				# Clear NT
	popfl
	ljmp $(KERNEL_CS), $0x100000    # Into C and sanity

2:
#endif
	lss SYMBOL_NAME(stack_start),%esp
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b
/*
 * Initialize eflags.  Some BIOS's leave bits like NT set.  This would
 * confuse the debugger if this code is traced.
 * XXX - best to initialize before switching to protected mode.
 */
	pushl $0
	popfl
/*
 * Clear BSS
 */
	xorl %eax,%eax
	movl $ SYMBOL_NAME(_edata),%edi
	movl $ SYMBOL_NAME(_end),%ecx
	subl %edi,%ecx
	cld
	rep
	stosb
/*
 * Do the decompression, and jump to the new kernel..
 */
	call SYMBOL_NAME(decompress_kernel)
	xorl %ebx,%ebx
	ljmp $(KERNEL_CS), $0x100000


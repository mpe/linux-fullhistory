/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for interrupts that cannot
 * result in an task-switch. These are things like the hd- and
 * floppy-interrupt etc. With these interrupts, we don't have to
 * care about the stack layout etc.
 */

.globl _floppy_interrupt,_parallel_interrupt

_floppy_interrupt:
	cld
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl _do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $_unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

_parallel_interrupt:
	cld
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret

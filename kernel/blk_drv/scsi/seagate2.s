/*
 *	seagate2.S
 *	low level scsi driver for ST01/ST02 by
 *		Drew Eckhardt 
 *
 *	<drew@colorado.edu>
 */
.text
.globl _seagate_intr
_seagate_intr:
	cld			# GCC thing

	pushal 
	push %ds
	push %es
	
	mov $0x10, %ax		# switch to kernel space
	mov %ax, %ds
	mov %ax, %es
	


	xor %eax, %eax 
	xchg _do_seagate, %eax
	test %eax, %eax
	jnz 1f
	mov $_seagate_unexpected_intr, %eax	
1:	call *%eax

	mov $0x20, %al		# non-specific EOI
	out %al, $0x20

	pop %es
	pop %ds
	popal
	iret

/*
 *  linux/kernel/sys_call.S
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * sys_call.S  contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * Stack layout in 'ret_from_system_call':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be 
 *	updated in fork.c:copy_process, signal.c:do_signal,
 *	ptrace.c and ptrace.h
 *
 *	 0(%esp) - %ebx
 *	 4(%esp) - %ecx
 *	 8(%esp) - %edx
 *       C(%esp) - %esi
 *	10(%esp) - %edi
 *	14(%esp) - %ebp
 *	18(%esp) - %eax
 *	1C(%esp) - %ds
 *	20(%esp) - %es
 *      24(%esp) - %fs
 *	28(%esp) - %gs
 *	2C(%esp) - orig_eax
 *	30(%esp) - %eip
 *	34(%esp) - %cs
 *	38(%esp) - %eflags
 *	3C(%esp) - %oldesp
 *	40(%esp) - %oldss
 */

SIG_CHLD	= 17

EBX		= 0x00
ECX		= 0x04
EDX		= 0x08
ESI		= 0x0C
EDI		= 0x10
EBP		= 0x14
EAX		= 0x18
DS		= 0x1C
ES		= 0x20
FS		= 0x24
GS		= 0x28
ORIG_EAX	= 0x2C
EIP		= 0x30
CS		= 0x34
EFLAGS		= 0x38
OLDESP		= 0x3C
OLDSS		= 0x40

/*
 * these are offsets into the task-struct.
 */
state		= 0
counter		= 4
priority	= 8
signal		= 12
sigaction	= 16		# MUST be 16 (=len of sigaction)
blocked		= (33*16)

/*
 * offsets within sigaction
 */
sa_handler	= 0
sa_mask		= 4
sa_flags	= 8
sa_restorer	= 12

ENOSYS = 38

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl _system_call,_timer_interrupt,_sys_execve
.globl _device_not_available, _coprocessor_error
.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_irq13,_reserved
.globl _alignment_check,_page_fault
.globl _keyboard_interrupt,_hd_interrupt
.globl _IRQ3_interrupt,_IRQ4_interrupt,_IRQ5_interrupt,_IRQ9_interrupt

#define SAVE_ALL \
	cld; \
	push %gs; \
	push %fs; \
	push %es; \
	push %ds; \
	pushl %eax; \
	pushl %ebp; \
	pushl %edi; \
	pushl %esi; \
	pushl %edx; \
	pushl %ecx; \
	pushl %ebx; \
	movl $0x10,%edx; \
	mov %dx,%ds; \
	mov %dx,%es; \
	movl $0x17,%edx; \
	mov %dx,%fs

#define ACK_FIRST(mask) \
	inb $0x21,%al; \
	jmp 1f; \
1:	jmp 1f; \
1:	orb $(mask),%al; \
	outb %al,$0x21; \
	jmp 1f; \
1:	jmp 1f; \
1:	movb $0x20,%al; \
	outb %al,$0x20

#define ACK_SECOND(mask) \
	inb $0xA1,%al; \
	jmp 1f; \
1:	jmp 1f; \
1:	orb $(mask),%al; \
	outb %al,$0xA1; \
	jmp 1f; \
1:	jmp 1f; \
1:	movb $0x20,%al; \
	outb %al,$0xA0 \
	jmp 1f; \
1:	jmp 1f; \
1:	outb %al,$0x20

#define UNBLK_FIRST(mask) \
	inb $0x21,%al; \
	jmp 1f; \
1:	jmp 1f; \
1:	andb $~(mask),%al; \
	outb %al,$0x21

#define UNBLK_SECOND(mask) \
	inb $0xA1,%al; \
	jmp 1f; \
1:	jmp 1f; \
1:	andb $~(mask),%al; \
	outb %al,$0xA1

.align 2
bad_sys_call:
	movl $-ENOSYS,EAX(%esp)
	jmp ret_from_sys_call
.align 2
reschedule:
	pushl $ret_from_sys_call
	jmp _schedule
.align 2
_system_call:
	pushl %eax		# save orig_eax
	SAVE_ALL
	cmpl _NR_syscalls,%eax
	jae bad_sys_call
	call _sys_call_table(,%eax,4)
	movl %eax,EAX(%esp)		# save the return value
ret_from_sys_call:
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 2f
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?
	jne 2f
1:	movl _current,%eax
	cmpl _task,%eax			# task[0] cannot have signals
	je 2f
	cmpl $0,_need_resched
	jne reschedule
	cmpl $0,state(%eax)		# state
	jne reschedule
	cmpl $0,counter(%eax)		# counter
	je reschedule
	movl signal(%eax),%ebx
	movl blocked(%eax),%ecx
	notl %ecx
	andl %ebx,%ecx
	bsfl %ecx,%ecx
	je 2f
	btrl %ecx,%ebx
	movl %ebx,signal(%eax)
	movl %esp,%ebx
	pushl %ebx
	incl %ecx
	pushl %ecx
	call _do_signal
	popl %ecx
	popl %ebx
	testl %eax, %eax
	jne 1b			# see if we need to switch tasks, or do more signals
2:	popl %ebx
	popl %ecx
	popl %edx
	popl %esi
	popl %edi
	popl %ebp
	popl %eax
	pop %ds
	pop %es
	pop %fs
	pop %gs
	addl $4,%esp 		# skip the orig_eax
	iret

.align 2
_irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20
	jmp 1f
1:	jmp 1f
1:	outb %al,$0xA0
	popl %eax
_coprocessor_error:
	pushl $-1		# mark this as an int. 
	SAVE_ALL
	pushl $ret_from_sys_call
	jmp _math_error

.align 2
_device_not_available:
	pushl $-1		# mark this as an int
	SAVE_ALL
	pushl $ret_from_sys_call
	clts				# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je _math_state_restore
	pushl $0		# temporary storage for ORIG_EIP
	call _math_emulate
	addl $4,%esp
	ret

.align 2
_keyboard_interrupt:
	pushl $-1
	SAVE_ALL
	ACK_FIRST(0x02)
	sti
	call _do_keyboard
	cli
	UNBLK_FIRST(0x02)
	jmp ret_from_sys_call

.align 2
_IRQ3_interrupt:
	pushl $-1
	SAVE_ALL
	ACK_FIRST(0x08)
	sti
	pushl $3
	call _do_IRQ
	addl $4,%esp
	cli
	UNBLK_FIRST(0x08)
	jmp ret_from_sys_call

.align 2
_IRQ4_interrupt:
	pushl $-1
	SAVE_ALL
	ACK_FIRST(0x10)
	sti
	pushl $4
	call _do_IRQ
	addl $4,%esp
	cli
	UNBLK_FIRST(0x10)
	jmp ret_from_sys_call

.align 2
_IRQ5_interrupt:
	pushl $-1
	SAVE_ALL
	ACK_FIRST(0x20)
	sti
	pushl $5
	call _do_IRQ
	addl $4,%esp
	cli
	UNBLK_FIRST(0x20)
	jmp ret_from_sys_call

.align 2
_IRQ9_interrupt:
	pushl $-1
	SAVE_ALL
	ACK_SECOND(0x02)
	sti
	pushl $9
	call _do_IRQ
	addl $4,%esp
	cli
	UNBLK_SECOND(0x02)
	jmp ret_from_sys_call

.align 2
_timer_interrupt:
	pushl $-1		# mark this as an int
	SAVE_ALL
	ACK_FIRST(0x01)
	sti
	incl _jiffies
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call _do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	cli
	UNBLK_FIRST(0x01)
	jmp ret_from_sys_call

.align 2
_hd_interrupt:
	pushl $-1
	SAVE_ALL
	ACK_SECOND(0x40)
	andl $0xfffeffff,_timer_active
	xorl %edx,%edx
	xchgl _do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $_unexpected_hd_interrupt,%edx
1:	call *%edx		# "interesting" way of handling intr.
	cli
	UNBLK_SECOND(0x40)
	jmp ret_from_sys_call

.align 2
_sys_execve:
	lea (EIP+4)(%esp),%eax  # don't forget about the return address.
	pushl %eax
	call _do_execve
	addl $4,%esp
	ret

_divide_error:
	pushl $0 		# no error code
	pushl $_do_divide_error
error_code:
	push %fs
	push %es
	push %ds
	pushl %eax
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %edx
	pushl %ecx
	pushl %ebx
	cld
	movl $-1, %eax
	xchgl %eax, ORIG_EAX(%esp)	# orig_eax (get the error code. )
	xorl %ebx,%ebx			# zero ebx
	mov %gs,%bx			# get the lower order bits of gs
	xchgl %ebx, GS(%esp)		# get the address and save gs.
	pushl %eax			# push the error code
	lea 52(%esp),%edx
	pushl %edx
	movl $0x10,%edx
	mov %dx,%ds
	mov %dx,%es
	movl $0x17,%edx
	mov %dx,%fs
	call *%ebx
	addl $8,%esp
	jmp ret_from_sys_call

_debug:
	pushl $0
	pushl $_do_int3		# _do_debug
	jmp error_code

_nmi:
	pushl $0
	pushl $_do_nmi
	jmp error_code

_int3:
	pushl $0
	pushl $_do_int3
	jmp error_code

_overflow:
	pushl $0
	pushl $_do_overflow
	jmp error_code

_bounds:
	pushl $0
	pushl $_do_bounds
	jmp error_code

_invalid_op:
	pushl $0
	pushl $_do_invalid_op
	jmp error_code

_coprocessor_segment_overrun:
	pushl $0
	pushl $_do_coprocessor_segment_overrun
	jmp error_code

_reserved:
	pushl $0
	pushl $_do_reserved
	jmp error_code

_double_fault:
	pushl $_do_double_fault
	jmp error_code

_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code

_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code

_stack_segment:
	pushl $_do_stack_segment
	jmp error_code

_general_protection:
	pushl $_do_general_protection
	jmp error_code

_alignment_check:
	pushl $_do_alignment_check
	jmp error_code

_page_fault:
	pushl $_do_page_fault
	jmp error_code

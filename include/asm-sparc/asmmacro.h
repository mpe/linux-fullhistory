/* asmmacro.h: Assembler macros.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 */

#ifndef _SPARC_ASMMACRO_H
#define _SPARC_ASMMACRO_H

/* #define SMP_DEBUG */

#define GET_PROCESSOR_ID(reg) \
	rd	%tbr, %reg; \
	srl	%reg, 12, %reg; \
	and	%reg, 3, %reg;

#define GET_PROCESSOR_MID(reg, tmp) \
	GET_PROCESSOR_ID(reg) \
	set	C_LABEL(mid_xlate), %tmp; \
	ldub	[%tmp + %reg], %reg;

#define GET_PROCESSOR_OFFSET(reg) \
	rd	%tbr, %reg; \
	srl	%reg, 10, %reg; \
	and	%reg, 0xc, %reg;

#define PROCESSOR_OFFSET_TO_ID(reg) \
	srl	%reg, 2, %reg;

#define PROCESSOR_ID_TO_OFFSET(reg) \
	sll	%reg, 2, %reg;

/* All trap entry points _must_ begin with this macro or else you
 * lose.  It makes sure the kernel has a proper window so that
 * c-code can be called.
 */
#ifndef SMP_DEBUG
#define SAVE_ALL \
	sethi	%hi(trap_setup), %l4; \
	jmpl	%l4 + %lo(trap_setup), %l6; \
	 nop;
#else
#define SAVE_ALL \
	GET_PROCESSOR_ID(l4); \
	set	C_LABEL(trap_log), %l5; \
	sll	%l4, 11, %l6; \
	add	%l5, %l6, %l5; \
	set	C_LABEL(trap_log_ent), %l6; \
	sll	%l4, 2, %l4; \
	add	%l6, %l4, %l6; \
	ld	[%l6], %l6; \
	sll	%l6, 3, %l6; \
	st	%l1, [%l5 + %l6]; \
	add	%l5, 4, %l5; \
	st	%l0, [%l5 + %l6]; \
	set	C_LABEL(trap_log_ent), %l5; \
	add	%l5, %l4, %l5; \
	srl	%l6, 3, %l6; \
	add	%l6, 1, %l6; \
	and	%l6, 255, %l6; \
	st	%l6, [%l5]; \
	sethi	%hi(trap_setup), %l4; \
	jmpl	%l4 + %lo(trap_setup), %l6; \
	 nop;
#endif

/* All traps low-level code here must end with this macro.
 * For SMP configurations the ret_trap_entry routine will
 * have to appropriate code to actually release the kernel
 * entry lock.
 */
#define RESTORE_ALL \
	b	ret_trap_entry; \
	 nop;

#ifndef __SMP__

#define ENTER_SYSCALL
#define LEAVE_SYSCALL
#define ENTER_IRQ
#define LEAVE_IRQ

#else

#define INCREMENT_COUNTER(symbol, tmp1, tmp2) \
	set	C_LABEL(symbol), %tmp1; \
	ld	[%tmp1], %tmp2; \
	add	%tmp2, 1, %tmp2; \
	st	%tmp2, [%tmp1];

#define DECREMENT_COUNTER(symbol, tmp1, tmp2) \
	set	C_LABEL(symbol), %tmp1; \
	ld	[%tmp1], %tmp2; \
	sub	%tmp2, 1, %tmp2; \
	st	%tmp2, [%tmp1];

	/* This is so complicated I suggest you don't look at it. */
#define ENTER_MASK(mask) \
	GET_PROCESSOR_OFFSET(l4) \
	set	C_LABEL(smp_spinning), %l6; \
	add	%l6, %l4, %l6; \
	mov	1, %l5; \
	st	%l5, [%l6]; \
	set	C_LABEL(smp_proc_in_lock), %l5; \
	ld	[%l5 + %l4], %l6; \
	or	%l6, mask, %l6; \
	st	%l6, [%l5 + %l4]; \
1: \
	set	C_LABEL(kernel_flag), %l5; \
	ldstub	[%l5], %l6; \
	cmp	%l6, 0; \
	be	3f; \
	 nop; \
	set	C_LABEL(active_kernel_processor), %l5; \
	GET_PROCESSOR_ID(l4) \
	ldub	[%l5], %l6; \
	cmp	%l6, %l4; \
	be	4f; \
	 nop; \
2: \
	GET_PROCESSOR_MID(l4, l5) \
	set	C_LABEL(sun4m_interrupts), %l5; \
	ld	[%l5], %l5; \
	sll	%l4, 12, %l4; \
	add	%l5, %l4, %l5; \
	ld	[%l5], %l4; \
	sethi	%hi(0x80000000), %l6; \
	andcc	%l6, %l4, %g0; \
	be	5f; \
	 nop; \
	st	%l6, [%l5 + 4]; \
	nop; nop; nop; \
	ld	[%l5], %g0; \
	nop; nop; nop; \
	or	%l0, PSR_PIL, %l4; \
	wr	%l4, 0x0, %psr; \
	nop; nop; nop; \
	wr	%l4, PSR_ET, %psr; \
	nop; nop; nop; \
	call	C_LABEL(smp_message_irq); \
       	 nop; \
	wr	%l0, 0x0, %psr; \
	nop; nop; nop; \
5: \
	set	C_LABEL(kernel_flag), %l5; \
	ldub	[%l5], %l6; \
	cmp	%l6, 0; \
	bne	2b; \
	 nop; \
	b	1b; \
	 nop; \
3: \
	GET_PROCESSOR_ID(l4) \
	set	C_LABEL(active_kernel_processor), %l5; \
	stb	%l4, [%l5]; \
	GET_PROCESSOR_MID(l4, l5) \
	set	C_LABEL(irq_rcvreg), %l5; \
	ld	[%l5], %l5; \
	st	%l4, [%l5]; \
4: \
	GET_PROCESSOR_OFFSET(l4) \
	set	C_LABEL(smp_spinning), %l6; \
	st	%g0, [%l6 + %l4];

#define ENTER_SYSCALL \
	ENTER_MASK(SMP_FROM_SYSCALL) \
	INCREMENT_COUNTER(kernel_counter, l6, l5) \
	INCREMENT_COUNTER(syscall_count, l6, l5)

#define ENTER_IRQ \
	ENTER_MASK(SMP_FROM_INT) \
	INCREMENT_COUNTER(kernel_counter, l6, l5)

#define LEAVE_MASK(mask) \
	GET_PROCESSOR_OFFSET(l4) \
	set	C_LABEL(smp_proc_in_lock), %l5; \
	ld	[%l5 + %l4], %l6; \
	andn	%l6, mask, %l6; \
	st	%l6, [%l5 + %l4];

#define LEAVE_SYSCALL \
	LEAVE_MASK(SMP_FROM_SYSCALL) \
	DECREMENT_COUNTER(syscall_count, l6, l5) \
	set	C_LABEL(kernel_counter), %l6; \
	ld	[%l6], %l5; \
	subcc	%l5, 1, %l5; \
	st	%l5, [%l6]; \
	bne	1f; \
	 nop; \
	set	C_LABEL(active_kernel_processor), %l6; \
	mov	NO_PROC_ID, %l5; \
	stb	%l5, [%l6]; \
	set	C_LABEL(kernel_flag), %l6; \
	stb	%g0, [%l6]; \
1:

#define LEAVE_IRQ \
	LEAVE_MASK(SMP_FROM_INT) \
	INCREMENT_COUNTER(syscall_count, l6, l5)


#define RESTORE_ALL_FASTIRQ \
	b	ret_irq_entry; \
	 nop;

#endif /* !(__SMP__) */

#endif /* !(_SPARC_ASMMACRO_H) */

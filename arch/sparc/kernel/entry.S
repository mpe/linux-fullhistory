/* $Id: entry.S,v 1.90 1996/04/18 01:00:37 davem Exp $
 * arch/sparc/kernel/entry.S:  Sparc trap low-level entry points.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/errno.h>

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/kgdb.h>
#include <asm/contregs.h>
#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/cprefix.h>
#include <asm/vaddrs.h>
#include <asm/memreg.h>
#include <asm/page.h>
#include <asm/winmacro.h>
#include <asm/signal.h>

#include <asm/asmmacro.h>

#define NR_SYSCALLS 255      /* Each OS is different... */

/* First, KGDB low level things.  This is a rewrite
 * of the routines found in the sparc-stub.c asm() statement
 * from the gdb distribution.  This is also dual-purpose
 * as a software trap for userlevel programs.
 */
	.data
	.align	4

in_trap_handler:
	.word	0

	.text
	.align	4

! This function is called when any SPARC trap (except window overflow or
! underflow) occurs.  It makes sure that the invalid register window is still
! available before jumping into C code.  It will also restore the world if you
! return from handle_exception.

	.globl	C_LABEL(trap_low)
C_LABEL(trap_low):
	rd	%wim, %l3
	SAVE_ALL
	ENTER_SYSCALL

	sethi	%hi(in_trap_handler), %l4
	ld	[%lo(in_trap_handler) + %l4], %l5
	inc	%l5
	st	%l5, [%lo(in_trap_handler) + %l4]

	/* Make sure kgdb sees the same state we just saved. */
	LOAD_PT_GLOBALS(sp)
	LOAD_PT_INS(sp)
	ld	[%sp + REGWIN_SZ + PT_Y], %l4
	ld	[%sp + REGWIN_SZ + PT_WIM], %l3
	ld	[%sp + REGWIN_SZ + PT_PSR], %l0
	ld	[%sp + REGWIN_SZ + PT_PC], %l1
	ld	[%sp + REGWIN_SZ + PT_NPC], %l2
	rd	%tbr, %l5	/* Never changes... */

	/* Make kgdb exception frame. */	
	sub	%sp,(16+1+6+1+72)*4,%sp	! Make room for input & locals
 					! + hidden arg + arg spill
					! + doubleword alignment
					! + registers[72] local var
	SAVE_KGDB_GLOBALS(sp)
	SAVE_KGDB_INS(sp)
	SAVE_KGDB_SREGS(sp, l4, l0, l3, l5, l1, l2)

	/* We are increasing PIL, so two writes. */
	or	%l0, PSR_PIL, %l0
	wr	%l0, 0, %psr
	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(handle_exception)
	 add	%sp, REGWIN_SZ, %o0	! Pass address of registers

	/* Load new kgdb register set. */
	LOAD_KGDB_GLOBALS(sp)
	LOAD_KGDB_INS(sp)
	LOAD_KGDB_SREGS(sp, l0, l2)
	wr	%l0, 0x0, %y

	sethi	%hi(in_trap_handler), %l4
	ld	[%lo(in_trap_handler) + %l4], %l5
	dec	%l5
	st	%l5, [%lo(in_trap_handler) + %l4]

	add	%sp,(16+1+6+1+72)*4,%sp	! Undo the kgdb trap frame.

	/* Now take what kgdb did and place it into the pt_regs
	 * frame which SparcLinux RESTORE_ALL understands.,
	 */
	STORE_PT_INS(sp)
	STORE_PT_GLOBALS(sp)
	STORE_PT_YREG(sp, g2)
	STORE_PT_PRIV(sp, l1, l2, l3)

	RESTORE_ALL


#ifdef CONFIG_BLK_DEV_FD
#ifdef TRACE_FLOPPY_HARDINT
/* Useful tracing */
	.data
	.align	4
	.globl	C_LABEL(floppy_hardint_trace)
C_LABEL(floppy_hardint_trace):
	.skip	32
	.globl	C_LABEL(floppy_hardint_index)
C_LABEL(floppy_hardint_index):
	.word	0
#endif

	.text
	.align	4
	.globl	C_LABEL(floppy_hardint)
C_LABEL(floppy_hardint):
	/* Can only use regs %l3->%l7:
	 * %l3 -- base address of fdc registers
	 * %l4 -- pdma_vaddr
	 * %l5 -- scratch for ld/st address
	 * %l6 -- pdma_size
	 * %l7 -- floppy_softint
	 */

#ifdef TRACE_FLOPPY_HARDINT
        sethi   %hi(C_LABEL(floppy_hardint_trace)), %l5
        or      %l5, %lo(C_LABEL(floppy_hardint_trace)), %l5
        ld      [%l5 + 32], %l7
        add     %l7, 1, %l7
        and     %l7, 31, %l7
        st      %l7, [%l5 + 32]
        sub     %l7, 1, %l7
        and     %l7, 31, %l7
        add     %l7, %l5, %l5
	or	%g0, 0xf, %l7
	stb	%l7, [%l5]
#endif

	/* Do we have work to do? */
	sethi	%hi(C_LABEL(doing_pdma)), %l4
	ld	[%l4 + %lo(C_LABEL(doing_pdma))], %l4
	cmp	%l4, 0
	be	floppy_dosoftint
	 nop

	/* Load fdc register base */
	sethi	%hi(C_LABEL(fdc_status)), %l3
	ld	[%l3 + %lo(C_LABEL(fdc_status))], %l3

	/* Setup register addresses */
	sethi	%hi(C_LABEL(pdma_vaddr)), %l5	! transfer buffer
	ld	[%l5 + %lo(C_LABEL(pdma_vaddr))], %l4
	sethi	%hi(C_LABEL(pdma_size)), %l5	! bytes to go
	ld	[%l5 + %lo(C_LABEL(pdma_size))], %l6
next_byte:
#ifdef TRACE_FLOPPY_HARDINT
	sethi	%hi(C_LABEL(floppy_hardint_trace)), %l5
	or	%l5, %lo(C_LABEL(floppy_hardint_trace)), %l5
	ld	[%l5 + 32], %l7
	add	%l7, 1, %l7
	and	%l7, 31, %l7
	st	%l7, [%l5 + 32]
	sub	%l7, 1, %l7
	and	%l7, 31, %l7
	add	%l7, %l5, %l5
	ldub	[%l3], %l7
	stb	%l7, [%l5]
#else
  	ldub	[%l3], %l7
#endif

	andcc	%l7, 0x80, %g0		! Does fifo still have data
	bz	floppy_fifo_emptied	! fifo has been emptied...
	 andcc	%l7, 0x20, %g0		! in non-dma mode still?
	bz	floppy_overrun		! nope, overrun
	 andcc	%l7, 0x40, %g0		! 0=write 1=read
	bz	floppy_write
	 sub	%l6, 0x1, %l6

	/* Ok, actually read this byte */
	ldub	[%l3 + 1], %l7
	orcc	%g0, %l6, %g0
	stb	%l7, [%l4]
	bne	next_byte
	 add	%l4, 0x1, %l4

	b	floppy_tdone
	 nop

floppy_write:
	/* Ok, actually write this byte */
	ldub	[%l4], %l7
	orcc	%g0, %l6, %g0
	stb	%l7, [%l3 + 1]
	bne	next_byte
	 add	%l4, 0x1, %l4

	/* fall through... */
floppy_tdone:
	sethi	%hi(C_LABEL(pdma_vaddr)), %l5
	st	%l4, [%l5 + %lo(C_LABEL(pdma_vaddr))]
	sethi	%hi(C_LABEL(pdma_size)), %l5
	st	%l6, [%l5 + %lo(C_LABEL(pdma_size))]
	/* Flip terminal count pin */
	set	C_LABEL(auxio_register), %l4
	ld	[%l4], %l4

	set	C_LABEL(sparc_cpu_model), %l5
	ld	[%l5], %l5
	subcc   %l5, 1, %g0		/* enum { sun4c = 1 }; */
	be	1f
	 ldub	[%l4], %l5

	or	%l5, 0xc2, %l5
	stb	%l5, [%l4]
	andn    %l5, 0x02, %l5
	b	2f
	 nop

1:
	or      %l5, 0xf4, %l5
	stb     %l5, [%l4]
	andn    %l5, 0x04, %l5

2:
	/* Kill some time so the bits set */
	WRITE_PAUSE
	WRITE_PAUSE

	stb     %l5, [%l4]

	/* Prevent recursion */
	sethi	%hi(C_LABEL(doing_pdma)), %l4
	b	floppy_dosoftint
	 st	%g0, [%l4 + %lo(C_LABEL(doing_pdma))]

	/* We emptied the FIFO, but we haven't read everything
	 * as of yet.  Store the current transfer address and
	 * bytes left to read so we can continue when the next
	 * fast IRQ comes in.
	 */
floppy_fifo_emptied:
	sethi	%hi(C_LABEL(pdma_vaddr)), %l5
	st	%l4, [%l5 + %lo(C_LABEL(pdma_vaddr))]
	sethi	%hi(C_LABEL(pdma_size)), %l7
	st	%l6, [%l7 + %lo(C_LABEL(pdma_size))]

	/* Restore condition codes */
	wr	%l0, 0x0, %psr
	WRITE_PAUSE

	jmp	%l1
	rett	%l2

floppy_overrun:
	sethi	%hi(C_LABEL(pdma_vaddr)), %l5
	st	%l4, [%l5 + %lo(C_LABEL(pdma_vaddr))]
	sethi	%hi(C_LABEL(pdma_size)), %l5
	st	%l6, [%l5 + %lo(C_LABEL(pdma_size))]
	/* Prevent recursion */
	sethi	%hi(C_LABEL(doing_pdma)), %l4
	st	%g0, [%l4 + %lo(C_LABEL(doing_pdma))]

	/* fall through... */
floppy_dosoftint:
	rd	%wim, %l3
	SAVE_ALL
	ENTER_IRQ

	/* Set all IRQs off. */
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	mov	11, %o0			! floppy irq level
	call	C_LABEL(floppy_interrupt)
	 add	%sp, REGWIN_SZ, %o1	! struct pt_regs *regs

	LEAVE_IRQ
	RESTORE_ALL
	
#endif /* (CONFIG_BLK_DEV_FD) */

	/* Bad trap handler */
	.globl	bad_trap_handler
bad_trap_handler:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	mov	%l7, %o0		! trap number
	mov	%l0, %o1		! psr
	call	C_LABEL(do_hw_interrupt)
	 mov	%l1, %o2		! pc

	RESTORE_ALL
	
/* For now all IRQ's not registered get sent here. handler_irq() will
 * see if a routine is registered to handle this interrupt and if not
 * it will say so on the console.
 */

	.align	4
	.globl	real_irq_entry
real_irq_entry:
	SAVE_ALL
#ifdef __SMP__
	cmp	%l7, 9
	bne	1f
	 nop

	GET_PROCESSOR_MID(l4, l5)
	set	C_LABEL(sun4m_interrupts), %l5
	ld	[%l5], %l5
	sethi	%hi(0x02000000), %l6
	sll	%l4, 12, %l4
	add	%l5, %l4, %l5
	ld	[%l5], %l4
	andcc	%l4, %l6, %g0
	be	1f
	 nop

	b	linux_trap_ipi9_sun4m
	 nop

1:
#endif
	ENTER_IRQ

#ifdef __SMP__
	cmp	%l7, 13
	bne	1f
	 nop

	/* This is where we catch the level 13 reschedule soft-IRQ. */
	GET_PROCESSOR_MID(o3, o2)
	set	C_LABEL(sun4m_interrupts), %l5
	ld	[%l5], %o5
	sethi	%hi(0x20000000), %o4
	sll	%o3, 12, %o3
	add	%o5, %o3, %o5
	ld	[%o5], %o1		! read processor irq pending reg
	andcc	%o1, %o4, %g0
	be	1f
	 nop

	b	linux_trap_ipi13_sun4m
	 nop

1:	

#endif

	/* start atomic operation with respect to software interrupts */
	sethi	%hi(C_LABEL(intr_count)), %l4
	ld	[%l4 + %lo(C_LABEL(intr_count))], %l5
	add	%l5, 0x1, %l5
	st	%l5, [%l4 + %lo(C_LABEL(intr_count))]

	/* Enable traps w/IRQs off, so we can call c-code properly.
	 * Note how we are increasing PIL so we need to do two writes
	 * to work around a MicroSPARC bug of sorts.
	 */
	or	%l0, PSR_PIL, %l4

	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	mov	%l7, %o0		! irq level
	call	C_LABEL(handler_irq)
	 add	%sp, REGWIN_SZ, %o1	! pt_regs ptr

rie_checkbh:
	sethi	%hi(C_LABEL(intr_count)), %l4
	ld	[%l4 + %lo(C_LABEL(intr_count))], %l5
	subcc	%l5, 0x1, %l5
	bne	2f	/* IRQ within IRQ, get out of here... */
	 nop

	sethi	%hi(C_LABEL(bh_active)), %l3
	ld	[%l3 + %lo(C_LABEL(bh_active))], %g2
	sethi	%hi(C_LABEL(bh_mask)), %l3
	ld	[%l3 + %lo(C_LABEL(bh_mask))], %g3
	andcc	%g2, %g3, %g0
	be	2f
	 nop

	call	C_LABEL(do_bottom_half)	
	 nop

	/* Try again... */
	b	rie_checkbh
	 nop
	
2:
	st	%l5, [%l4 + %lo(C_LABEL(intr_count))]

	LEAVE_IRQ
	RESTORE_ALL

	/* This routine handles illegal isntructions and privileged
	 * instruction attempts from user code.
	 */
	.align	4
	.globl	bad_instruction
bad_instruction:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_illegal_instruction)
	 mov	%l0, %o3

	RESTORE_ALL

	.align	4
	.globl	priv_instruction
priv_instruction:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_priv_instruction)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles unaligned data accesses.
	 */
	.align	4
	.globl	mna_handler
mna_handler:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_memaccess_unaligned)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles floating point disabled traps. */
	.align	4
	.globl	fpd_trap_handler
fpd_trap_handler:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_fpd_trap)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Floating Point Exceptions. */
	.align	4
	.globl	fpe_trap_handler
fpe_trap_handler:
	set	fpsave_magic, %l5
	cmp	%l1, %l5
	be	1f
	 sethi	%hi(C_LABEL(fpsave)), %l5
	or	%l5, %lo(C_LABEL(fpsave)), %l5
	cmp	%l1, %l5
	bne	2f
	 sethi	%hi(fpsave_catch2), %l5
	or	%l5, %lo(fpsave_catch2), %l5
	wr	%l0, 0x0, %psr
	WRITE_PAUSE
	jmp	%l5
	 rett	%l5 + 4
1:	
	sethi	%hi(fpsave_catch), %l5
	or	%l5, %lo(fpsave_catch), %l5
	wr	%l0, 0x0, %psr
	WRITE_PAUSE
	jmp	%l5
	 rett	%l5 + 4

2:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_fpe_trap)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Tag Overflow Exceptions. */
	.align	4
	.globl	do_tag_overflow
do_tag_overflow:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_tag_overflow)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Watchpoint Exceptions. */
	.align	4
	.globl	do_watchpoint
do_watchpoint:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_watchpoint)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Register Access Exceptions. */
	.align	4
	.globl	do_reg_access
do_reg_access:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_reg_access)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Co-Processor Disabled Exceptions. */
	.align	4
	.globl	do_cp_disabled
do_cp_disabled:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_cp_disabled)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Unimplemented FLUSH Exceptions. */
	.align	4
	.globl	do_bad_flush
do_bad_flush:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_bad_flush)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Co-Processor Exceptions. */
	.align	4
	.globl	do_cp_exception
do_cp_exception:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_cp_exception)
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Hardware Divide By Zero Exceptions. */
	.align	4
	.globl	do_hw_divzero
do_hw_divzero:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, REGWIN_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_hw_divzero)
	 mov	%l0, %o3

	RESTORE_ALL

	.align	4
	.globl	do_flush_windows
do_flush_windows:
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	andcc	%l0, PSR_PS, %g0
	bne	dfw_kernel
	 nop

	call	C_LABEL(flush_user_windows)
	 nop

	/* Advance over the trap instruction. */
	ld	[%sp + REGWIN_SZ + PT_NPC], %l1
	add	%l1, 0x4, %l2
	st	%l1, [%sp + REGWIN_SZ + PT_PC]
	st	%l2, [%sp + REGWIN_SZ + PT_NPC]

	RESTORE_ALL

	/* We get these for debugging routines using __builtin_return_address() */
dfw_kernel:
	FLUSH_ALL_KERNEL_WINDOWS

	/* Advance over the trap instruction. */
	ld	[%sp + REGWIN_SZ + PT_NPC], %l1
	add	%l1, 0x4, %l2
	st	%l1, [%sp + REGWIN_SZ + PT_PC]
	st	%l2, [%sp + REGWIN_SZ + PT_NPC]

	RESTORE_ALL

	/* The getcc software trap.  The user wants the condition codes from
	 * the %psr in register %g1.
	 */

	.align	4
	.globl	getcc_trap_handler
getcc_trap_handler:
	srl	%l0, 20, %g1	! give user
	and	%g1, 0xf, %g1	! only ICC bits in %psr
	jmp	%l2		! advance over trap instruction
	rett	%l2 + 0x4	! like this...

	/* The setcc software trap.  The user has condition codes in %g1
	 * that it would like placed in the %psr.  Be careful not to flip
	 * any unintentional bits!
	 */

	.align	4
	.globl	setcc_trap_handler
setcc_trap_handler:
	sll	%g1, 0x14, %l4
	set	PSR_ICC, %l5
	andn	%l0, %l5, %l0	! clear ICC bits in current %psr
	and	%l4, %l5, %l4	! clear non-ICC bits in user value
	or	%l4, %l0, %l4	! or them in... mix mix mix

	wr	%l4, 0x0, %psr	! set new %psr
	WRITE_PAUSE		! TI scumbags...

	jmp	%l2		! advance over trap instruction
	rett	%l2 + 0x4	! like this...

	.align	4
	.globl	linux_trap_nmi_sun4c
linux_trap_nmi_sun4c:
	SAVE_ALL
	ENTER_SYSCALL

	/* Ugh, we need to clear the IRQ line.  This is now
	 * a very sun4c specific trap hanler...
	 */
	sethi	%hi(C_LABEL(interrupt_enable)), %l5
	ld	[%l5 + %lo(C_LABEL(interrupt_enable))], %l5
	ldub	[%l5], %l6
	andn	%l6, INTS_ENAB, %l6
	stb	%l6, [%l5]

	/* Now it is safe to re-enable traps without recursion. */
	or	%l0, PSR_PIL, %l0
	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	/* Now call the c-code with the pt_regs frame ptr and the
	 * memory error registers as arguments.  The ordering chosen
	 * here is due to unlatching semantics.
	 */
	sethi	%hi(AC_SYNC_ERR), %o0
	add	%o0, 0x4, %o0
	lda	[%o0] ASI_CONTROL, %o2	! sync vaddr
	sub	%o0, 0x4, %o0
	lda	[%o0] ASI_CONTROL, %o1	! sync error
	add	%o0, 0xc, %o0
	lda	[%o0] ASI_CONTROL, %o4	! async vaddr
	sub	%o0, 0x4, %o0
	lda	[%o0] ASI_CONTROL, %o3	! async error
	call	C_LABEL(sparc_lvl15_nmi)
	 add	%sp, REGWIN_SZ, %o0

	RESTORE_ALL

#ifdef __SMP__

	.align	4
	.globl	linux_trap_ipi9_sun4m
linux_trap_ipi9_sun4m:
	sethi	%hi(0x02000000), %o2
	GET_PROCESSOR_MID(o0, o1)
	set	C_LABEL(sun4m_interrupts), %l5
	ld	[%l5], %o5
	sll	%o0, 12, %o0
	add	%o5, %o0, %o5
	st	%o2, [%o5 + 4]
	WRITE_PAUSE

	ld	[%o5], %g0
	WRITE_PAUSE

	/* IRQ's off else we deadlock. */
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE

	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(smp_message_irq)
	 nop

	RESTORE_ALL_FASTIRQ

	.align	4
	.globl	linux_trap_ipi13_sun4m
linux_trap_ipi13_sun4m:
	/* NOTE: real_irq_entry saved state and grabbed klock already. */

	/* start atomic operation with respect to software interrupts */
	sethi	%hi(C_LABEL(intr_count)), %l4
	ld	[%l4 + %lo(C_LABEL(intr_count))], %l5
	add	%l5, 0x1, %l5
	st	%l5, [%l4 + %lo(C_LABEL(intr_count))]

	sethi	%hi(0x20000000), %o2
	GET_PROCESSOR_MID(o0, o1)
	set	C_LABEL(sun4m_interrupts), %l5
	ld	[%l5], %o5
	sll	%o0, 12, %o0
	add	%o5, %o0, %o5
	st	%o2, [%o5 + 4]
	WRITE_PAUSE

	ld	[%o5], %g0
	WRITE_PAUSE

	/* IRQ's off else we deadlock. */
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE

	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(smp_reschedule_irq)
	 nop

	sethi	%hi(C_LABEL(intr_count)), %l4
	ld	[%l4 + %lo(C_LABEL(intr_count))], %l5
	sub	%l5, 0x1, %l5
	st	%l5, [%l4 + %lo(C_LABEL(intr_count))]

	LEAVE_IRQ
	RESTORE_ALL

	.align	4
	.globl	linux_trap_ipi15_sun4m
linux_trap_ipi15_sun4m:
	SAVE_ALL

	/* First check for hard NMI memory error. */
	sethi	%hi(0xf0000000), %o2
	set	C_LABEL(sun4m_interrupts), %l5
	set	0x4000, %o3
	ld	[%l5], %l5
	add	%l5, %o3, %l5
	ld	[%l5], %l6
	andcc	%o2, %l6, %o2
	be	1f
	 nop

	/* Asyncronous fault, why you little ?!#&%@... */
	sethi	%hi(0x80000000), %o2
	st	%o2, [%l5 + 0xc]
	WRITE_PAUSE
	ld	[%l5], %g0
	WRITE_PAUSE

	/* All interrupts are off... now safe to enable traps
	 * and call C-code.
	 */	
	or	%l0, PSR_PIL, %l4	! I am very paranoid...
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE
	call	C_LABEL(sun4m_nmi)
	 nop

	sethi	%hi(0x80000000), %o2
	st	%o2, [%l5 + 0x8]
	WRITE_PAUSE
	ld	[%l5], %g0
	WRITE_PAUSE

	RESTORE_ALL_FASTIRQ

1:
	sethi	%hi(0x80000000), %o2
	GET_PROCESSOR_MID(o0, o1)
	set	C_LABEL(sun4m_interrupts), %l5
	ld	[%l5], %o5
	sll	%o0, 12, %o0
	add	%o5, %o0, %o5
	st	%o2, [%o5 + 4]
	WRITE_PAUSE

	ld	[%o5], %g0
	WRITE_PAUSE

	/* IRQ's off else we deadlock. */
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE

	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(smp_message_irq)
	 nop

	RESTORE_ALL_FASTIRQ

#endif

	.align	4
	.globl	sun4c_fault
sun4c_fault:
	SAVE_ALL
	ENTER_SYSCALL

	/* XXX This needs to be scheduled better */
	sethi	%hi(AC_SYNC_ERR), %l4
	add	%l4, 0x4, %l5		! AC_SYNC_VA in %l5
	lda	[%l5] ASI_CONTROL, %o3		/* Address */
	lda	[%l4] ASI_CONTROL, %l6
	srl	%l6, 15, %l6
	and	%l6, 1, %o2	/* Write? */

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	mov	%l7, %o1			/* Text fault? */
	call	C_LABEL(do_sparc_fault)
	 add	%sp, REGWIN_SZ, %o0		/* pt_regs */

	RESTORE_ALL

	.align	4
	.globl	C_LABEL(srmmu_fault)
C_LABEL(srmmu_fault):
	mov	0x400, %l5
	mov	0x300, %l4

	lda	[%l5] ASI_M_MMUREGS, %l6	! read sfar first
	lda	[%l4] ASI_M_MMUREGS, %l5	! read sfsr last

	andn	%l6, 0xfff, %l6
	srl	%l5, 6, %l5			! and encode all info into l7

	and	%l5, 2, %l5
	or	%l5, %l6, %l6

	or	%l6, %l7, %l7			! l7 = [addr,write,txtfault]

	SAVE_ALL
	ENTER_SYSCALL

	mov	%l7, %o1
	mov	%l7, %o2
	and	%o1, 1, %o1		! arg2 = text_faultp
	mov	%l7, %o3
	and	%o2, 2, %o2		! arg3 = writep
	andn	%o3, 0xfff, %o3		! arg4 = faulting address

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(do_sparc_fault)
	 add	%sp, REGWIN_SZ, %o0	! arg1 = pt_regs ptr

	RESTORE_ALL

	/* SunOS uses syscall zero as the 'indirect syscall' it looks
	 * like indir_syscall(scall_num, arg0, arg1, arg2...);  etc.
	 * This is complete brain damage.
	 */
	.globl	C_LABEL(sunos_indir)
C_LABEL(sunos_indir):
	ld	[%sp + REGWIN_SZ + PT_I0], %g1
	cmp	%g1, NR_SYSCALLS
	blu,a	1f
	 sll	%g1, 0x2, %g1

	set	C_LABEL(sunos_nosys), %l6
	b	2f
	 nop

1:
	set	C_LABEL(sunos_sys_table), %l7
	ld	[%l7 + %g1], %l6

2:	
	ld	[%sp + REGWIN_SZ + PT_I1], %o0
	ld	[%sp + REGWIN_SZ + PT_I2], %o1
	ld	[%sp + REGWIN_SZ + PT_I3], %o2
	mov	%o7, %l5
	ld	[%sp + REGWIN_SZ + PT_I4], %o3
	call	%l6
	 ld	[%sp + REGWIN_SZ + PT_I5], %o4

	jmp	%l5 + 0x8		/* so stupid... */
	 nop

#if 0 /* work in progress */
	.align 4
	.globl	C_LABEL(sys_ptrace)
C_LABEL(sys_ptrace):
	call	C_LABEL(do_ptrace)
	 add	%sp, REGWIN_SZ, %o0

	RESTORE_ALL
#endif

	.align	4
	.globl	C_LABEL(sys_execve)
C_LABEL(sys_execve):
	mov	%o7, %l5
	call	C_LABEL(sparc_execve)
	 add	%sp, REGWIN_SZ, %o0		! pt_regs *regs arg

	jmp	%l5 + 0x8
	 nop

	.align	4
	.globl	C_LABEL(sys_pipe)
C_LABEL(sys_pipe):
	mov	%o7, %l5

	call	C_LABEL(sparc_pipe)
	 add	%sp, REGWIN_SZ, %o0		! pt_regs *regs arg

	jmp	%l5 + 0x8
	 nop

	.align	4
	.globl	C_LABEL(sys_sigpause)
C_LABEL(sys_sigpause):
	ld	[%sp + REGWIN_SZ + PT_I0], %o0
	call	C_LABEL(do_sigpause)
	 add	%sp, REGWIN_SZ, %o1

	/* We are returning to a signal handler. */
	RESTORE_ALL

	.align	4
	.globl	C_LABEL(sys_sigsuspend)
C_LABEL(sys_sigsuspend):
	call	C_LABEL(do_sigsuspend)
	 add	%sp, REGWIN_SZ, %o0

	/* We are returning to a signal handler. */
	RESTORE_ALL

	.align	4
	.globl	C_LABEL(sys_sigreturn)
C_LABEL(sys_sigreturn):
	call	C_LABEL(do_sigreturn)
	 add	%sp, REGWIN_SZ, %o0

	/* We don't want to muck with user registers like a
	 * normal syscall, just return.
	 */
	RESTORE_ALL

	/* Now that we have a real sys_clone, sys_fork() is
	 * implemented in terms of it.  Our _real_ implementation
	 * of SunOS vfork() will use sys_clone() instead.
	 */
	.align	4
	.globl	C_LABEL(sys_fork), C_LABEL(sys_vfork)
C_LABEL(sys_vfork):
C_LABEL(sys_fork):
	mov	%o7, %l5

	/* Save the kernel state as of now. */
	FLUSH_ALL_KERNEL_WINDOWS;
	STORE_WINDOW(sp)
	LOAD_CURRENT(g6, g5)
	rd	%psr, %g4
	rd	%wim, %g5
	std	%g4, [%g6 + THREAD_FORK_KPSR]

	mov	SIGCHLD, %o0			! arg0:	clone flags
	ld	[%sp + REGWIN_SZ + PT_FP], %o1	! arg1:	usp
	call	C_LABEL(do_fork)
	 add	%sp, REGWIN_SZ, %o2		! arg2:	pt_regs ptr

	jmp	%l5 + 0x8
	 nop
	
	/* Whee, kernel threads! */
	.globl	C_LABEL(sys_clone)
C_LABEL(sys_clone):
	mov	%o7, %l5

	/* Save the kernel state as of now. */
	FLUSH_ALL_KERNEL_WINDOWS;
	STORE_WINDOW(sp)
	LOAD_CURRENT(g6, g5)
	rd	%psr, %g4
	rd	%wim, %g5
	std	%g4, [%g6 + THREAD_FORK_KPSR]

	ldd	[%sp + REGWIN_SZ + PT_I0], %o0	! arg0,1: flags,usp
	cmp	%o1, 0x0			! Is new_usp NULL?
	be,a	1f
	 ld	[%sp + REGWIN_SZ + PT_FP], %o1	! yes, use current usp
1:
	call	C_LABEL(do_fork)
	 add	%sp, REGWIN_SZ, %o2		! arg2:	pt_regs ptr

	jmp	%l5 + 0x8
	 nop

	/* Linux native and SunOS system calls enter here... */
	.align	4
	.globl	linux_sparc_syscall
linux_sparc_syscall:
	/* While we are here trying to optimize our lives
	 * away, handle the easy bogus cases like a
	 * ni_syscall or sysnum > NR_SYSCALLS etc.
	 * In the cases where we cannot optimize the
	 * call inline we don't really lose anything
	 * performance wise because we are doing here
	 * things which we did anyway in the original
	 * routine.  The only added complexity is a
	 * bit test, compare, and branch to decide
	 * if we need to save process state or not.
	 */

	/* XXX TODO: When we have ptrace working test
	 * XXX       test for PF_TRACESYS in task flags.
	 */

	/* Direct access to user regs, must faster. */
	cmp	%g1, NR_SYSCALLS
	blu,a	1f
	 sll	%g1, 2, %l4

	set	C_LABEL(sys_ni_syscall), %l7
	b	syscall_is_too_hard
	 nop

1:
	ld	[%l7 + %l4], %l7

	/* If bit-1 is set, this is a "fast" syscall.
	 * This is the _complete_ overhead of this optimization,
	 * and we save ourselves a load, so it evens out to nothing.
	 */
	andcc	%l7, 0x1, %g0
	be	syscall_is_too_hard
	 andn	%l7, 0x1, %l7

	jmpl	%l7, %g0
	 nop

	.globl	syscall_is_too_hard
syscall_is_too_hard:
	rd	%wim, %l3
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

2:
	ldd	[%sp + REGWIN_SZ + PT_I0], %o0
	st	%o0, [%sp + REGWIN_SZ + PT_G0]	! for restarting syscalls
	ldd	[%sp + REGWIN_SZ + PT_I2], %o2
	call	%l7
	 ldd	[%sp + REGWIN_SZ + PT_I4], %o4

	st	%o0, [%sp + REGWIN_SZ + PT_I0]

	.globl	C_LABEL(ret_sys_call)
C_LABEL(ret_sys_call):
	ld	[%sp + REGWIN_SZ + PT_I0], %o0
	set	PSR_C, %l6
	cmp	%o0, -ENOIOCTLCMD
	bgeu	1f
	 ld	[%sp + REGWIN_SZ + PT_PSR], %l5

	/* System call success, clear Carry condition code. */		
	andn	%l5, %l6, %l5
	b	2f
	 st	%l5, [%sp + REGWIN_SZ + PT_PSR]	

1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub	%g0, %o0, %o0
	st	%o0, [%sp + REGWIN_SZ + PT_I0]
	or	%l5, %l6, %l5
	st	%l5, [%sp + REGWIN_SZ + PT_PSR]

	/* Advance the pc and npc over the trap instruction. */
2:
	ld	[%sp + REGWIN_SZ + PT_NPC], %l1	/* pc  = npc   */
	add	%l1, 0x4, %l2			/* npc = npc+4 */
	st	%l1, [%sp + REGWIN_SZ + PT_PC]
	st	%l2, [%sp + REGWIN_SZ + PT_NPC]

	RESTORE_ALL

	/* Solaris system calls enter here... */
	.align	4
	.globl	solaris_syscall
solaris_syscall:
	/* While we are here trying to optimize our lives
	 * away, handle the easy bogus cases like a
	 * ni_syscall or sysnum > NR_SYSCALLS etc.
	 * In the cases where we cannot optimize the
	 * call inline we don't really lose anything
	 * performance wise because we are doing here
	 * things which we did anyway in the original
	 * routine.  The only added complexity is a
	 * bit test, compare, and branch to decide
	 * if we need to save process state or not.
	 */

	/* XXX TODO: When we have ptrace working test
	 * XXX       test for PF_TRACESYS in task flags.
	 */

	/* Direct access to user regs, must faster. */
	cmp	%g1, NR_SYSCALLS
	blu,a	1f
	 sll	%g1, 2, %l4

	set	C_LABEL(sys_ni_syscall), %l7
	b	solaris_is_too_hard
	 nop

1:
	ld	[%l7 + %l4], %l7

	/* If bit-1 is set, this is a "fast" syscall.
	 * This is the _complete_ overhead of this optimization,
	 * and we save ourselves a load, so it evens out to nothing.
	 */
	andcc	%l7, 0x1, %g0
	be	solaris_is_too_hard
	 andn	%l7, 0x1, %l7

	jmpl	%l7, %g0
	 nop

	.globl	solaris_is_too_hard
solaris_is_too_hard:
	rd	%wim, %l3
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

2:
	ldd	[%sp + REGWIN_SZ + PT_I0], %o0
	st	%o0, [%sp + REGWIN_SZ + PT_G0]	! for restarting syscalls
	ldd	[%sp + REGWIN_SZ + PT_I2], %o2
	call	%l7
	 ldd	[%sp + REGWIN_SZ + PT_I4], %o4

	st	%o0, [%sp + REGWIN_SZ + PT_I0]
	set	PSR_C, %l6
	cmp	%o0, -ENOIOCTLCMD
	bgeu	1f
	 ld	[%sp + REGWIN_SZ + PT_PSR], %l5

	/* System call success, clear Carry condition code. */		
	andn	%l5, %l6, %l5
	b	2f
	 st	%l5, [%sp + REGWIN_SZ + PT_PSR]	

1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub	%g0, %o0, %o0
	sethi	%hi(C_LABEL(solaris_xlatb_rorl)), %o3
	or	%o3, %lo(C_LABEL(solaris_xlatb_rorl)), %o3
	sll	%o0, 2, %o0
	ld	[%o3 + %o0], %o0
	st	%o0, [%sp + REGWIN_SZ + PT_I0]
	or	%l5, %l6, %l5
	st	%l5, [%sp + REGWIN_SZ + PT_PSR]

	/* Advance the pc and npc over the trap instruction. */
2:
	ld	[%sp + REGWIN_SZ + PT_NPC], %l1	/* pc  = npc   */
	add	%l1, 0x4, %l2			/* npc = npc+4 */
	st	%l1, [%sp + REGWIN_SZ + PT_PC]
	st	%l2, [%sp + REGWIN_SZ + PT_NPC]

	RESTORE_ALL

	/* {net, open}bsd system calls enter here... */
	.align	4
	.globl	bsd_syscall
bsd_syscall:
	/* While we are here trying to optimize our lives
	 * away, handle the easy bogus cases like a
	 * ni_syscall or sysnum > NR_SYSCALLS etc.
	 * In the cases where we cannot optimize the
	 * call inline we don't really lose anything
	 * performance wise because we are doing here
	 * things which we did anyway in the original
	 * routine.  The only added complexity is a
	 * bit test, compare, and branch to decide
	 * if we need to save process state or not.
	 */

	/* XXX TODO: When we have ptrace working test
	 * XXX       test for PF_TRACESYS in task flags.
	 */

	/* Direct access to user regs, must faster. */
	cmp	%g1, NR_SYSCALLS
	blu,a	1f
	 sll	%g1, 2, %l4

	set	C_LABEL(sys_ni_syscall), %l7
	b	bsd_is_too_hard
	 nop

1:
	ld	[%l7 + %l4], %l7

	/* If bit-1 is set, this is a "fast" syscall.
	 * This is the _complete_ overhead of this optimization,
	 * and we save ourselves a load, so it evens out to nothing.
	 */
	andcc	%l7, 0x1, %g0
	be	bsd_is_too_hard
	 andn	%l7, 0x1, %l7

	jmpl	%l7, %g0
	 nop

	.globl	bsd_is_too_hard
bsd_is_too_hard:
	rd	%wim, %l3
	SAVE_ALL
	ENTER_SYSCALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

2:
	ldd	[%sp + REGWIN_SZ + PT_I0], %o0
	st	%o0, [%sp + REGWIN_SZ + PT_G0]	! for restarting syscalls
	ldd	[%sp + REGWIN_SZ + PT_I2], %o2
	call	%l7
	 ldd	[%sp + REGWIN_SZ + PT_I4], %o4

	st	%o0, [%sp + REGWIN_SZ + PT_I0]
	set	PSR_C, %l6
	cmp	%o0, -ENOIOCTLCMD
	bgeu	1f
	 ld	[%sp + REGWIN_SZ + PT_PSR], %l5

	/* System call success, clear Carry condition code. */		
	andn	%l5, %l6, %l5
	b	2f
	 st	%l5, [%sp + REGWIN_SZ + PT_PSR]	

1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub	%g0, %o0, %o0
#if 0 /* XXX todo XXX */
	sethi	%hi(C_LABEL(bsd_xlatb_rorl), %o3
	or	%o3, %lo(C_LABEL(bsd_xlatb_rorl)), %o3
	sll	%o0, 2, %o0
	ld	[%o3 + %o0], %o0
#endif
	st	%o0, [%sp + REGWIN_SZ + PT_I0]
	or	%l5, %l6, %l5
	st	%l5, [%sp + REGWIN_SZ + PT_PSR]

	/* Advance the pc and npc over the trap instruction. */
2:
	ld	[%sp + REGWIN_SZ + PT_NPC], %l1	/* pc  = npc   */
	add	%l1, 0x4, %l2			/* npc = npc+4 */
	st	%l1, [%sp + REGWIN_SZ + PT_PC]
	st	%l2, [%sp + REGWIN_SZ + PT_NPC]

	RESTORE_ALL

/* Saving and restoring the FPU state is best done from lowlevel code.
 *
 * void fpsave(unsigned long *fpregs, unsigned long *fsr,
 *             void *fpqueue, unsigned long *fpqdepth)
 */

	.globl	C_LABEL(fpsave)
C_LABEL(fpsave):
	st	%fsr, [%o1]	! this can trap on us if fpu is in bogon state
	ld	[%o1], %g1
	set	0x2000, %g4
	andcc	%g1, %g4, %g0
	be	2f
	 mov	0, %g2

	/* We have an fpqueue to save. */
1:
	std	%fq, [%o2]
fpsave_magic:
	st	%fsr, [%o1]
	ld	[%o1], %g3
	andcc	%g3, %g4, %g0
	add	%g2, 1, %g2
	bne	1b
	 add	%o2, 8, %o2

2:
	st	%g2, [%o3]

	std	%f0, [%o0 + 0x00]
	std	%f2, [%o0 + 0x08]
	std	%f4, [%o0 + 0x10]
	std	%f6, [%o0 + 0x18]
	std	%f8, [%o0 + 0x20]
	std	%f10, [%o0 + 0x28]
	std	%f12, [%o0 + 0x30]
	std	%f14, [%o0 + 0x38]
	std	%f16, [%o0 + 0x40]
	std	%f18, [%o0 + 0x48]
	std	%f20, [%o0 + 0x50]
	std	%f22, [%o0 + 0x58]
	std	%f24, [%o0 + 0x60]
	std	%f26, [%o0 + 0x68]
	std	%f28, [%o0 + 0x70]
	retl
	 std	%f30, [%o0 + 0x78]

	/* Thanks for Theo Deraadt and the authors of the Sprite/netbsd/openbsd
	 * code for pointing out this possible deadlock, while we save state
	 * above we could trap on the fsr store so our low level fpu trap
	 * code has to know how to deal with this.
	 */
fpsave_catch:
	b	fpsave_magic + 4
	 st	%fsr, [%o1]

fpsave_catch2:
	b	C_LABEL(fpsave) + 4
	 st	%fsr, [%o1]

	/* void fpload(unsigned long *fpregs, unsigned long *fsr); */

	.globl	C_LABEL(fpload)
C_LABEL(fpload):
	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ld	[%o1], %fsr
	retl
	 nop

	.globl	C_LABEL(udelay)
C_LABEL(udelay):
	save	%sp, -REGWIN_SZ, %sp
	mov	%i0, %o0
	sethi	%hi(0x10c6), %o1
	call	.umul
	 or	%o1, %lo(0x10c6), %o1
#ifndef __SMP__
	sethi	%hi(C_LABEL(loops_per_sec)), %o3
	call	.umul
	 ld	[%o3 + %lo(C_LABEL(loops_per_sec))], %o1
#else
	GET_PROCESSOR_OFFSET(o4)
	set	C_LABEL(cpu_data), %o3
	call	.umul
	 ld	[%o3 + %o4], %o1
#endif

	cmp	%o1, 0x0
1:
	bne	1b
	 subcc	%o1, 1, %o1
	
	ret
	restore

/* End of entry.S */

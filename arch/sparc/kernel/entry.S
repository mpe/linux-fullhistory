/* $Id: entry.S,v 1.64 1995/11/25 00:57:53 davem Exp $
 * arch/sparc/kernel/entry.S:  Sparc trap low-level entry points.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>

#include <asm/head.h>
#include <asm/asi.h>
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
#include <asm/errno.h>

#define NR_SYSCALLS 255      /* Each OS is different... */

/* All trap entry points _must_ begin with this macro or else you
 * lose.  It makes sure the kernel has a proper window so that
 * c-code can be called.  Some day for SMP we'll grab klock here.
 */
#define SAVE_ALL \
	sethi	%hi(trap_setup), %l4; \
	jmpl	%l4 + %lo(trap_setup), %l6; \
	 nop;

/* All traps low-level code here must end with this macro.
 * For SMP configurations the ret_trap_entry routine will
 * have to appropriate code to actually release the kernel
 * entry lock.
 */
#define RESTORE_ALL \
	b	ret_trap_entry; \
	 nop;

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

	sethi	%hi(in_trap_handler), %l4
	ld	[%lo(in_trap_handler) + %l4], %l5
	inc	%l5
	st	%l5, [%lo(in_trap_handler) + %l4]

	/* Make sure kgdb sees the same state we just saved. */
	LOAD_PT_GLOBALS(sp)
	LOAD_PT_INS(sp)
	ld	[%sp + STACKFRAME_SZ + PT_Y], %l4
	ld	[%sp + STACKFRAME_SZ + PT_WIM], %l3
	ld	[%sp + STACKFRAME_SZ + PT_PSR], %l0
	ld	[%sp + STACKFRAME_SZ + PT_PC], %l1
	ld	[%sp + STACKFRAME_SZ + PT_NPC], %l2
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
	 add	%sp, STACKFRAME_SZ, %o0	! Pass address of registers

	/* Load new kgdb register set. */
	LOAD_KGDB_GLOBALS(sp)
	LOAD_KGDB_INS(sp)
	LOAD_KGDB_SREGS(sp, l0, l2)
	ld	[%sp + STACKFRAME_SZ + KGDB_WIM], %l6
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
	STORE_PT_PRIV(sp, l1, l2, l3, l6)

	/* Cross your fingers... */
	RESTORE_ALL


#ifdef CONFIG_BLK_DEV_FD
	.align	4
	.globl	C_LABEL(floppy_hardint)
C_LABEL(floppy_hardint):
	/* Can only use regs %l3->%l7:
	 * %l3 -- base address of fdc registers
	 * %l4 -- doing_pdma upon entry, AUXIO reg vaddr after
	 * %l5 -- pdma_vaddr
	 * %l6 -- pdma_size
	 * %l7 -- floppy_softint
	 */

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
	ldub	[%l3], %l7
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
	sethi	%hi(AUXIO_VADDR), %l4
	ldub	[%l4 + %lo(AUXIO_VADDR) + 0x3], %l5
	or	%l5, 0xf4, %l5
	stb	%l5, [%l4 + %lo(AUXIO_VADDR) + 0x3]

	/* Kill some time so the bits set */
	WRITE_PAUSE
	WRITE_PAUSE

	ldub	[%l4 + %lo(AUXIO_VADDR) + 0x3], %l5
	andn	%l5, 0x04, %l5
	or	%l5, 0xf0, %l5
	stb	%l5, [%l4 + %lo(AUXIO_VADDR) + 0x3]

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

	/* Set all IRQs off. */
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	mov	11, %o0			! floppy irq level
	call	C_LABEL(floppy_interrupt)
	 add	%sp, STACKFRAME_SZ, %o1	! struct pt_regs *regs

	RESTORE_ALL
	
#endif /* (CONFIG_BLK_DEV_FD) */

	/* Bad trap handler */
	.globl	bad_trap_handler
bad_trap_handler:
	SAVE_ALL
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
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	mov	%l7, %o0		! irq level
	call	C_LABEL(handler_irq)
	 add	%sp, STACKFRAME_SZ, %o1	! pt_regs ptr

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
	RESTORE_ALL

/* Soft IRQ's are handled just like hard IRQ's except that we
 * need to clear the IRQ line ourselves (in the interrupt reg)
 * and we don't take care of bottom-half handlers here.  We'll
 * just deal with it at the next clock tick, and since software
 * IRQ's relatively don't happen that often....
 * XXX BIG XXX Turn the software IRQ bit we need to clear into
 * XXX BIG XXX an element reference in an array that we can set
 * XXX BIG XXX a boot time based upon arch type
 * XXX BIG XXX OR... rewrite the appropriate IRQ trap table
 * XXX BIG XXX entries once the arch is detected (sun4/4c or sun4m)
 *
 * XXX EVEN BIGGER XXX Linux has bh_handlers for software interrupts
 * XXX EVEN BIGGER XXX so we do not even have to worry about this
 * XXX EVEN BIGGER XXX brain damaged software interrupt mechanism.
 */

	.align	4
	.globl	soft_irq_entry
soft_irq_entry:
	SAVE_ALL

	/* We have tucked the bit to clear in the int reg into
	 * %l4, take care of it now before we do anything else.
	 */
	sethi	%hi(INTREG_VADDR), %l5
	ldsb	[%l5 + %lo(INTREG_VADDR)], %l6
	andn	%l6, %l4, %l6
	stb	%l6, [%l5 + %lo(INTREG_VADDR)]

	/* start atomic operation with respect to software interrupts */
	sethi	%hi(C_LABEL(intr_count)), %l4
	ld	[%l4 + %lo(C_LABEL(intr_count))], %l5
	add	%l5, 0x1, %l5
	st	%l5, [%l4 + %lo(C_LABEL(intr_count))]

	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr		! grrr!
	wr	%l4, PSR_ET, %psr	! double grrr!

	mov	%l7, %o0	
	add	%sp, STACKFRAME_SZ, %o1
	call	C_LABEL(handler_irq)
	 nop

	sethi	%hi(C_LABEL(intr_count)), %l4
	ld	[%l4 + %lo(C_LABEL(intr_count))], %l5
	subcc	%l5, 0x1, %l5
	st	%l5, [%l4 + %lo(C_LABEL(intr_count))]

	RESTORE_ALL

	/* This routine handles illegal isntructions and privileged
	 * instruction attempts from user code.
	 */
	.align	4
	.globl	bad_instruction
bad_instruction:
	SAVE_ALL
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_illegal_instruction)
	 mov	%l0, %o3
	RESTORE_ALL

	.align	4
	.globl	priv_instruction
priv_instruction:
	SAVE_ALL
	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(do_fpd_trap)
	 mov	%l0, %o3
	RESTORE_ALL

	/* This routine handles Floating Point Exceptions. */
	.align	4
	.globl	fpe_trap_handler
fpe_trap_handler:
	SAVE_ALL
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
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
	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	C_LABEL(handle_hw_divzero)
	 mov	%l0, %o3
	RESTORE_ALL

	.align	4
	.globl	do_flush_windows
do_flush_windows:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(flush_user_windows)
	 nop

	/* Advance over the trap instruction. */
	ld	[%sp + STACKFRAME_SZ + PT_NPC], %l1
	add	%l1, 0x4, %l2
	st	%l1, [%sp + STACKFRAME_SZ + PT_PC]
	st	%l2, [%sp + STACKFRAME_SZ + PT_NPC]

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
	.globl	linux_trap_nmi
linux_trap_nmi:
	SAVE_ALL
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
	 add	%sp, STACKFRAME_SZ, %o0

	RESTORE_ALL

	.align	4
	.globl	sparc_fault
sparc_fault:
	SAVE_ALL
	rd	%tbr, %o1
	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(do_sparc_fault)
	 add	%sp, STACKFRAME_SZ, %o0
	RESTORE_ALL

	/* SunOS uses syscall zero as the 'indirect syscall' it looks
	 * like indir_syscall(scall_num, arg0, arg1, arg2...);  etc.
	 * This is complete brain damage.
	 */
	.globl	C_LABEL(sunos_indir)
C_LABEL(sunos_indir):
	ld	[%sp + STACKFRAME_SZ + PT_I0], %g1
	cmp	%g1, NR_SYSCALLS
	bleu,a	1f
	 sll	%g1, 0x2, %g1

	set	C_LABEL(sunos_nosys), %l6
	b	2f
	 nop

1:
	set	C_LABEL(sunos_sys_table), %l7
	ld	[%l7 + %g1], %l6

2:	
	ld	[%sp + STACKFRAME_SZ + PT_I1], %o0
	ld	[%sp + STACKFRAME_SZ + PT_I2], %o1
	ld	[%sp + STACKFRAME_SZ + PT_I3], %o2
	ld	[%sp + STACKFRAME_SZ + PT_I4], %o3
	call	%l6
	 ld	[%sp + STACKFRAME_SZ + PT_I5], %o4

	b	scall_store_args		/* so stupid... */
	 nop

	.align	4
	.globl	C_LABEL(sys_execve)
C_LABEL(sys_execve):
	call	C_LABEL(sparc_execve)
	 add	%sp, STACKFRAME_SZ, %o0		! pt_regs *regs arg

	b	scall_store_args
	 nop

	.align	4
	.globl	C_LABEL(sys_pipe)
C_LABEL(sys_pipe):
	call	C_LABEL(sparc_pipe)
	 add	%sp, STACKFRAME_SZ, %o0		! pt_regs *regs arg

	b	C_LABEL(ret_sys_call)
	 nop

	.align	4
	.globl	C_LABEL(sys_sigreturn)
C_LABEL(sys_sigreturn):
	call	C_LABEL(do_sigreturn)
	 add	%sp, STACKFRAME_SZ, %o0

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
	/* Save the kernel state as of now. */
	FLUSH_ALL_KERNEL_WINDOWS;
	STORE_WINDOW(sp)
	LOAD_CURRENT(g6)
	rd	%psr, %g4
	rd	%wim, %g5
	std	%g4, [%g6 + THREAD_KPSR]
	std	%sp, [%g6 + THREAD_KSP]

	mov	SIGCHLD, %o0			! arg0:	clone flags
	mov	%fp, %o1			! arg1:	usp
	call	C_LABEL(do_fork)
	 add	%sp, STACKFRAME_SZ, %o2		! arg2:	pt_regs ptr

	b	scall_store_args
	 nop
	
	/* Whee, kernel threads! */
	.globl	C_LABEL(sys_clone)
C_LABEL(sys_clone):
	/* Save the kernel state as of now. */
	FLUSH_ALL_KERNEL_WINDOWS;
	STORE_WINDOW(sp)
	LOAD_CURRENT(g6)
	rd	%psr, %g4
	rd	%wim, %g5
	std	%g4, [%g6 + THREAD_KPSR]
	std	%sp, [%g6 + THREAD_KSP]

	ldd	[%sp + STACKFRAME_SZ + PT_I0], %o0	! arg0,1: flags,usp
	cmp	%o1, 0x0				! Is new_usp NULL?
	be,a	1f
	 mov	%fp, %o1				! yes, use current usp
1:
	call	C_LABEL(do_fork)
	 add	%sp, STACKFRAME_SZ, %o2			! arg2:	pt_regs ptr

	b	scall_store_args
	 nop

#if 0 /* XXX Much later... XXX */
	/* Whee, vfork... */
	.globl	C_LABEL(sys_vfork)
C_LABEL(sys_vfork):
	/* Save the kernel state as of now. */
	FLUSH_ALL_KERNEL_WINDOWS;
	STORE_WINDOW(sp)
	LOAD_CURRENT(g6)
	rd	%psr, %g4
	rd	%wim, %g5
	std	%g4, [%g6 + THREAD_KPSR]
	std	%sp, [%g6 + THREAD_KSP]

	set	(0x2100 | SIGCHLD), %o0			! CLONE_VFORK,CLONE_VM,SIGCHLD
	mov	%fp, %o1				! use current usp
1:
	call	C_LABEL(do_fork)
	 add	%sp, STACKFRAME_SZ, %o2			! arg2:	pt_regs ptr

	b	scall_store_args
	 nop
#endif

	/* All system calls enter here... */
	.align	4
	.globl	linux_sparc_syscall
linux_sparc_syscall:
	/* Don't dork with %l7, it holds the pointer to the
	 * system call vector table.  SAVE_ALL does not
	 * modify its value.
	 */
	rd	%wim, %l3
	SAVE_ALL

	wr	%l0, PSR_ET, %psr	/* Turn on traps + interrupts */
	WRITE_PAUSE

#if 0 /* Trace all system calls... */
	add	%sp, STACKFRAME_SZ, %o0
	call	C_LABEL(syscall_trace_entry)
	 nop
#endif

	/* SAVE_ALL may have blown away %g1, reload it. */
	ld	[%sp + STACKFRAME_SZ + PT_G1], %g1
	cmp	%g1, NR_SYSCALLS
	bleu,a	1f
	 sll	%g1, 0x2, %g1

	set	C_LABEL(sys_ni_syscall), %l6
	b	2f
	 nop

1:
	/* Syscall table ptr is in %l7. */
	ld	[%l7 + %g1], %l6	! load up ptr to syscall handler

	/* Pt_regs is your friend... Make the syscall... */
2:
	ldd	[%sp + STACKFRAME_SZ + PT_I0], %o0
	ldd	[%sp + STACKFRAME_SZ + PT_I2], %o2
	ldd	[%sp + STACKFRAME_SZ + PT_I4], %o4
	call	%l6
	 nop

scall_store_args:
	st	%o0, [%sp + STACKFRAME_SZ + PT_I0]

	.globl	C_LABEL(ret_sys_call)
C_LABEL(ret_sys_call):
	ld	[%sp + STACKFRAME_SZ + PT_I0], %o0
	set	PSR_C, %l6
	cmp	%o0, -ELIBSCN
	bgeu	1f
	 ld	[%sp + STACKFRAME_SZ + PT_PSR], %l5

	/* System call success, clear Carry condition code. */		
	andn	%l5, %l6, %l5
	b	2f
	 st	%l5, [%sp + STACKFRAME_SZ + PT_PSR]	

1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub	%g0, %o0, %o0
	st	%o0, [%sp + STACKFRAME_SZ + PT_I0]
	or	%l5, %l6, %l5
	st	%l5, [%sp + STACKFRAME_SZ + PT_PSR]

	/* %i6 is our frame pointer, the restore done by the rett
	 * instruction will automatically put us back on the users
	 * stack.  Advance the pc and npc past the trap instruction.
	 */
2:
	ld	[%sp + STACKFRAME_SZ + PT_NPC], %l1	/* pc  = npc   */
	add	%l1, 0x4, %l2				/* npc = npc+4 */
	st	%l1, [%sp + STACKFRAME_SZ + PT_PC]
	st	%l2, [%sp + STACKFRAME_SZ + PT_NPC]

#if 0 /* Trace all system calls... */
	add	%sp, STACKFRAME_SZ, %o0
	call	C_LABEL(syscall_trace_exit)
	 nop
#endif

	RESTORE_ALL

	.globl	C_LABEL(flush_user_windows)
C_LABEL(flush_user_windows):
	LOAD_CURRENT(g2)
	ld	[%g2 + THREAD_UMASK], %g1
	orcc	%g0, %g1, %g0
	be	3f
	 clr	%g3
1:
	_SV
	LOAD_CURRENT(g2)
	ld	[%g2 + THREAD_UMASK], %g1
	orcc	%g0, %g1, %g0
	bne	1b
	 add	%g3, 1, %g3
2:
	subcc	%g3, 1, %g3
	bne	2b
	 _RS
3:
	jmp	%o7 + 0x8
	 nop

/* End of entry.S */

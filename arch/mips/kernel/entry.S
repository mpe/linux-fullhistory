/*
 * arch/mips/kernel/entry.S
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * written by Ralf Baechle
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 */

#include <linux/sys.h>
#include <linux/autoconf.h>
#include <asm/segment.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>
#include <asm/page.h>
#include <asm/stackframe.h>
#include <asm/regdef.h>
#include <asm/processor.h>

/*
 * These are offsets into the task-struct.
 */
state		=  0
counter		=  4
priority	=  8
signal		= 12
blocked		= 16
flags		= 20
errno		= 24			#/* MIPS OK */
exec_domain	= 60			#/* ??? */

ENOSYS		= 38

		.globl	ret_from_sys_call
		.globl	_sys_call_table

		.text
		.set	noreorder
		.align 4
handle_bottom_half:
		/*
		 * If your assembler breaks on the next line it's
		 * time to update!
		 */
		lui	s0,%hi(_intr_count)
		lw	s1,%lo(_intr_count)(s0)
		mfc0	s3,CP0_STATUS			# Enable IRQs
		addiu	s2,s1,1
		sw	s2,%lo(_intr_count)(s0)
		ori	t0,s3,0x1f
		xori	t0,t0,0x1e
		jal	_do_bottom_half
		mtc0	t0,CP0_STATUS			# delay slot
		mtc0	s3,CP0_STATUS			# Restore old IRQ state
		j	9f
		sw	s1,%lo(_intr_count)(s0)		# delay slot

		.set	reorder
reschedule:
		la	ra,ret_from_sys_call
		j	_schedule
		nop

		.align	5
		.globl	_handle_sys
_handle_sys:
		.set	noreorder
		.set	noat
		SAVE_ALL
		.set	at
		STI
		/*
		 * Compute return address. For now we assume that syscalls never
		 * appear in branch delay slots. For the Linux/MIPS standard
		 * libraries this assumption is always try.
		 */
		lw	t3,FR_EPC(sp)
		lw	s1,FR_REG2(sp)
		li	t0,-ENOSYS
		addiu	t3,t3,4
		sw	t3,FR_EPC(sp)
		li	t2,NR_syscalls
		bge	s1,t2,ret_from_sys_call
		sw	t0,FR_REG2(sp)		# delay slot
		sll	s1,s1,2
		lw	s1,_sys_call_table(s1)
		lw	s0,_current

		beqz	s1,ret_from_sys_call
		lw	t0,flags(s0)
		sll	t0,t0,2			# PF_TRACESYS
		bltz	t0,1f
		sw	zero,errno(s0)		# delay slot

		lw	a0,FR_REG4(sp)
		lw	a1,FR_REG5(sp)
		lw	a2,FR_REG6(sp)
		lw	a3,FR_REG7(sp)
		lw	t0,FR_REG3(sp)
		jalr	s1			# do the real work
		sw	t0,16(sp)		# delay slot

		lw	t0,errno(s0)
		sw	v0,FR_REG2(sp)		# save the return value
		subu	t0,zero,t0		# t0 = -t0
		beqz	t0,ret_from_sys_call
		nop
		/*
		 * Fixme: should set error flag
		 */
		j	ret_from_sys_call
		sw	t0,FR_REG2(sp)		# delay slot

		.align	4
1:		jal	_syscall_trace
		nop				# delay slot

		lw	a0,FR_REG4(sp)
		lw	a1,FR_REG5(sp)
		lw	a2,FR_REG6(sp)
		lw	a3,FR_REG7(sp)
		lw	t0,FR_REG3(sp)
		jalr	s1			# do the real work
		sw	t0,16(sp)		# delay slot

		lw	t0,errno(s0)
		sw	v0,FR_REG2(sp)		# save the return value
		subu	t0,zero,t0
		beqz	t0,1f
		nop				# delay slot
		sw	t1,FR_REG2(sp)
		/*
		 * Fixme: should set error flag
		 */
1:		jal	_syscall_trace
		nop

		.align	4
ret_from_sys_call:
		lw	t0,_intr_count		# bottom half
		bnez	t0,2f
9:
		lw	t0,_bh_mask		# delay slot
		lw	t1,_bh_active		# unused delay slot
		and	t0,t1
		bnez	t0,handle_bottom_half

		lw	t0,FR_STATUS(sp)	# returning to supervisor ?
		andi	t1,t0,0x10
		beqz	t1,2f

		mfc0	t0,CP0_STATUS		# delay slot
		lw	t1,_need_resched
		ori	t0,0x1f			# enable irqs
		xori	t0,0x1e
		bnez	t1,reschedule
		mtc0	t0,CP0_STATUS		# delay slot

		lw	s0,_current
		lw	t0,_task
		lw	t1,state(s0)		# state
		beq	s0,t0,2f		# task[0] cannot have signals
		lw	t0,counter(s0)		# counter
		bnez	t1,reschedule		# state == 0 ?
		lw	a0,blocked(s0)
						# save blocked in a0 for
						# signal handling
		beqz	t0,reschedule		# counter == 0 ?
		lw	t0,signal(s0)
		nor	t1,zero,a0
		and	t1,t0,t1
		beqz	t1,skip_signal_return
		nop

		jal	_do_signal
		move	a1,sp			# delay slot
		
skip_signal_return:
		.set	noreorder
		.set	noat
2:
return:		RESTORE_ALL
		.set	at

#ifdef CONFIG_DESKSTATION_TYNE
/*
 * Deskstation Tyne interrupt handler
 */
		.text
		.set	noreorder
		.set	noat
		.globl	_deskstation_tyne_handle_int
		.align	5
_deskstation_tyne_handle_int:
		SAVE_ALL
		.set	at
		CLI
		lui	s0,%hi(PORT_BASE)
		li	t1,0x0f
		sb	t1,%lo(PORT_BASE+0x20)(s0)	# poll command
		lb	t1,%lo(PORT_BASE+0x20)(s0)	# read result
		li	s1,1
		bgtz	t1,Lpoll_second
		andi	t1,t1,7
		/*
		 * Acknowledge first pic
		 */
		lb	t2,%lo(PORT_BASE+0x21)(s0)
		lui	s4,%hi(_cache_21)
		lb	t0,%lo(_cache_21)(s4)
		sllv	s1,s1,t1
		or	t0,t0,s1
		sb	t0,%lo(_cache_21)(s4)
		sb	t0,%lo(PORT_BASE+0x21)(s0)
		lui	s3,%hi(_intr_count)
		lw	t0,%lo(_intr_count)(s3)
		li	t2,0x20
		sb	t2,%lo(PORT_BASE+0x20)(s0)
		/*
		 * Now call the real handler
		 */
		la	t3,_IRQ_vectors
		sll	t2,t1,2
		addu	t3,t3,t2
		lw	t3,(t3)
		addiu	t0,t0,1
		jalr	t3
		sw	t0,%lo(_intr_count)(s3)		# delay slot
		lw	t0,%lo(_intr_count)(s3)
		/*
		 * Unblock first pic
		 */
		lbu	t1,%lo(PORT_BASE+0x21)(s0)
		lb	t1,%lo(_cache_21)(s4)
		subu	t0,t0,1
		sw	t0,%lo(_intr_count)(s3)
		nor	s1,zero,s1
		and	t1,t1,s1
		sb	t1,%lo(_cache_21)(s4)
		jr	v0
		sb	t1,%lo(PORT_BASE+0x21)(s0)	# delay slot

		.align	5
Lpoll_second:	li	t1,0x0f
		sb	t1,%lo(PORT_BASE+0xa0)(s0)	# poll command
		lb	t1,%lo(PORT_BASE+0xa0)(s0)	# read result
		lui	s4,%hi(_cache_A1)
		bgtz	t1,Lspurious_interrupt
		andi	t1,t1,7
		/*
		 * Acknowledge second pic
		 */
		lbu	t2,%lo(PORT_BASE+0xa1)(s0)
		lb	t3,%lo(_cache_A1)(s4)
		sllv	s1,s1,t1
		or	t3,t3,s1
		sb	t3,%lo(_cache_A1)(s4)
		sb	t3,%lo(PORT_BASE+0xa1)(s0)
		li	t3,0x20
		sb	t3,%lo(PORT_BASE+0xa0)(s0)
		lui	s3,%hi(_intr_count)
		lw	t0,%lo(_intr_count)(s3)
		sb	t3,%lo(PORT_BASE+0x20)(s0)
		/*
		 * Now call the real handler
		 */
		la	t0,_IRQ_vectors
		sll	t2,t1,2
		addu	t0,t0,t2
		lw	t0,32(t0)
		addiu	t0,t0,1
		jalr	t0
		sw	t0,%lo(_intr_count)(s3)		# delay slot
		lw	t0,%lo(_intr_count)(s3)
		/*
		 * Unblock second pic
		 */
		lb	t1,%lo(PORT_BASE+0xa1)(s0)
		lb	t1,%lo(_cache_A1)(s4)
		subu	t0,t0,1
		lw	t0,%lo(_intr_count)(s3)
		nor	s1,zero,s1
		and	t1,t1,s1
		sb	t1,%lo(_cache_A1)(s4)
		jr	v0
		sb	t1,%lo(PORT_BASE+0xa1)(s0)	# delay slot

		.align	5
Lspurious_interrupt:
		/*
		 * Nothing happened... (whistle)
		 */
		lui	t1,%hi(_spurious_count)
		lw	t0,%lo(_spurious_count)(t1)
		la	v0,return
		addiu	t0,t0,1
		jr	ra
		sw	t0,%lo(_spurious_count)(t1)
#endif /* CONFIG_DESKSTATION_TYNE */
		
#ifdef CONFIG_ACER_PICA_61
/*
 * Acer PICA interrupt handler dummy
 */
		.set	noreorder
		.set	noat
		.globl	_acer_pica_61_handle_int
		.align	5
_acer_pica_61_handle_int:
		la	a0,acer_text
		jal	_panic
		nop
1:		b	1b
		nop
acer_text:	.asciz	"Interrupt handler for Acer PICA not written yet"
		.align	2
#endif /* CONFIG_ACER_PICA_61 */

		.text
		.set	noreorder
		.set	at
		.globl	_interrupt
		.align	5
_interrupt:	move	s2,ra
		mfc0	t0,CP0_STATUS
		ori	t0,t0,0x1f
		xori	t0,t0,0x1e
		mtc0	t0,CP0_STATUS
		move	a0,t1
		jal	_do_IRQ
		move	a1,sp				# delay slot
		mfc0	t0,CP0_STATUS
		ori	t0,t0,1
		xori	t0,t0,1
		la	v0,ret_from_sys_call
		jr	s2
		mtc0	t0,CP0_STATUS			# delay slot

		.globl	_fast_interrupt
		.align	5
_fast_interrupt:
		move	s2,ra
		move	a0,t1
		jal	_do_fast_IRQ
		move	a1,sp				# delay slot
		la	v0,return
		jr	s2
		nop					# delay slot

		.globl	_bad_interrupt
_bad_interrupt:
		/*
		 * Don't return & unblock the pic
		 */
		j	return
		nop

		.globl	_handle_tlbl
		.align	5
_handle_tlbl:
		.set	noreorder
		.set	noat
		/*
		 * Check whether this is a refill or an invalid exception
		 *
		 * NOTE: Some MIPS manuals say that the R4x00 sets the
		 * BadVAddr only when EXL == 0. This is wrong - BadVaddr
		 * is being set for all Reload, Invalid and Modified
		 * exceptions.
		 */
		mfc0	k0,CP0_BADVADDR
		mfc0	k1,CP0_ENTRYHI
		ori	k0,k0,0x1fff
		xori	k0,k0,0x1fff
		andi	k1,k1,0xff
		or	k0,k0,k1
		mfc0	k1,CP0_ENTRYHI
		mtc0	k0,CP0_ENTRYHI
		nop					# for R4[04]00 pipeline
		nop
		nop
		tlbp
		nop					# for R4[04]00 pipeline
		nop
		mfc0	k0,CP0_INDEX
		srl	k0,k0,31
		beqz	k0,invalid_tlbl
		mtc0	k1,CP0_ENTRYHI			# delay slot
		/*
		 * Not in tlb -> nested refill exception
		 * Load the missing entry and return. This is the most
		 * efficient way to regain the faulting address.
		 */
		dmfc0	k1,CP0_CONTEXT
		dsra	k1,k1,1
		lwu	k0,(k1)			# Never causes another exception
		lwu	k1,4(k1)
		dsrl	k0,k0,6			# Convert to EntryLo format
		dsrl	k1,k1,6			# Convert to EntryLo format
		dmtc0	k0,CP0_ENTRYLO0
		dmtc0	k1,CP0_ENTRYLO1
		nop				# for R4[04]00 pipeline
		tlbwr
		eret

		/*
		 * Handle invalid exception
		 *
		 * There are two possible causes for an invalid (tlbl)
		 * exception:
		 * 1) pages that have the present bit set but the valid bit
		 *    unset.
		 * 2) pages that don't exist
		 * Case one needs fast handling, therefore don't save
		 * registers yet.
		 *
		 * k0 now contains the bad virtual address.
		 */
invalid_tlbl:
		/*
		 * Remove entry so we don't need to care later
		 */
		mfc0	k0,CP0_INDEX
		lui	k1,0x0008
		or	k0,k0,k1
		dsll	k0,k0,13
		dmtc0	k0,CP0_ENTRYHI
		dmtc0	zero,CP0_ENTRYLO0
		dmtc0	zero,CP0_ENTRYLO1
		/*
		 * Test whether present bit in entry is set
		 */
		dmfc0	k0,CP0_BADVADDR
		tlbwi						# delayed, for R4[04]00 pipeline
		srl	k0,k0,10
		lui	k1,%HI(TLBMAP)
		addu	k0,k0,k1
		ori	k0,k0,3
		xori	k0,k0,3
		lw	k1,(k0)
		andi	k1,k1,_PAGE_PRESENT
		beqz	k1,nopage_tlbl
		/*
		 * Present bit is set -> set valid and accessed bits
		 */
		lw	k1,(k0)				# delay slot
		ori	k1,k1,_PAGE_ACCESSED
		sw	k1,(k0)
		eret

		/*
		 * Page doesn't exist. Lots of work which is less important
		 * for speed needs to be done, so hand it all over to the
		 * kernel memory management routines.
		 */
nopage_tlbl:
		SAVE_ALL
		.set	at
		STI
		/*
		 * Create a Intel-style errorcode
		 * Bit 0: P Present
		 *        0 == Page not in memory
		 *        1 == privilege violation
		 * Bit 1: R/W Read/Write
		 *        0 == ReadAccess
		 *        1 == WriteAccess
		 * Bit 2: U/S User/Supervisor
		 *        0 == User mode
		 *        1 == Kernel mode
		 *
		 * a0 (struct pt_regs *) regs
		 * a1 (unsigned long)    error_code
		 */
		lw	a1,FR_STATUS(sp)
		move	a0,sp
		srl	a1,a1,4
		andi	a1,a1,1
		jal	_do_page_fault
		xori	a1,a1,1				# delay slot
		j	ret_from_sys_call
		nop					# delay slot

		.text
		.globl	_handle_tlbs
		.align	5
_handle_tlbs:
		.set	noreorder
		.set	noat
		/*
		 * It is impossible that is a nested reload exception.
		 * Therefore this must be a invalid exception.
		 * Two possible cases:
		 * 1) Page not used yet
		 * 2) Page doesn't exist yet. Let the kernel handle the trouble.
		 *
		 * Test whether present bit in entry is set
		 */
		dmfc0	k0,CP0_BADVADDR
		srl	k0,k0,10
		lui	k1,%HI(TLBMAP)
		addu	k0,k0,k1
		ori	k0,k0,3
		xori	k0,k0,3
		lw	k1,(k0)
		andi	k1,k1,(_PAGE_PRESENT|_PAGE_RW)
		beqz	k1,nopage_tlbs
		/*
		 * Present and writable bits set -> set accessed and dirty bits.
		 */
		lw	k1,(k0)				# delay slot
		ori	k1,k1,(_PAGE_ACCESSED|_PAGE_DIRTY)
		sw	k1,(k0)
		/*
		 * Now reload the entry into the tlb
		 */
		ori	k0,k0,0x1000
		xori	k0,k0,0x1000
		lw	k1,4(k0)
		lw	k0,(k0)
		srl	k0,k0,6
		srl	k1,k1,6
		dmtc0	k0,CP0_ENTRYLO0
		dmtc0	k1,CP0_ENTRYLO1
		tlbwi
		eret

		/*
		 * Page doesn't exist. Lots of work which is less important
		 * for speed needs to be done, so hand it all over to the
		 * kernel memory management routines.
		 */
nopage_tlbs:
nowrite_mod:
		/*
		 * Remove entry so we don't need to care later
		 */
		mfc0	k0,CP0_INDEX
		lui	k1,0x0008
		or	k0,k0,k1
		dsll	k0,k0,13
		dmtc0	k0,CP0_ENTRYHI
		dmtc0	zero,CP0_ENTRYLO0
		dmtc0	zero,CP0_ENTRYLO1
		tlbwi
		SAVE_ALL
		.set	at
		STI
		/*
		 * Create a Intel-style errorcode
		 * Bit 0: P Present
		 *        0 == Page not in memory
		 *        1 == privilege violation
		 * Bit 1: R/W Read/Write
		 *        0 == ReadAccess
		 *        1 == WriteAccess
		 * Bit 2: U/S User/Supervisor
		 *        0 == User mode
		 *        1 == Kernel mode
		 *
		 * a0 (struct pt_regs *) regs
		 * a1 (unsigned long)    error_code
		 */
		lw	a1,FR_STATUS(sp)
		move	a0,sp
		srl	a1,a1,4
		andi	a1,a1,1
		jal	_do_page_fault
		xori	a1,a1,3				# branch delay slot
		j	ret_from_sys_call
		nop					# branch delay slot

		.globl	_handle_mod
		.align	5
_handle_mod:
		.set	noreorder
		.set	noat
		/*
		 * Two possible cases:
		 * 1) Page is rw but not dirty -> set dirty and return
		 * 2) Page is not rw -> call C handler
		 */
		dmfc0	k0,CP0_BADVADDR
		srl	k0,k0,10
		lui	k1,%HI(TLBMAP)
		addu	k0,k0,k1
		ori	k0,k0,3
		xori	k0,k0,3
		lw	k1,(k0)
		andi	k1,k1,_PAGE_RW
		beqz	k1,nopage_tlbs
		/*
		 * Present and writable bits set -> set accessed and dirty bits.
		 */
		lw	k1,(k0)				# delay slot
		ori	k1,k1,(_PAGE_ACCESSED|_PAGE_DIRTY)
		sw	k1,(k0)
		/*
		 * Now reload the entry into the tlb
		 */
		ori	k0,k0,0x1000
		xori	k0,k0,0x1000
		lw	k1,4(k0)
		lw	k0,(k0)
		srl	k0,k0,6
		srl	k1,k1,6
		dmtc0	k0,CP0_ENTRYLO0
		dmtc0	k1,CP0_ENTRYLO1
		tlbwi
		eret

		.globl	_handle_adel
		.align	5
_handle_adel:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_adel
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_ades
		.align	5
_handle_ades:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_ades
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_ibe
		.align	5
_handle_ibe:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_ibe
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_dbe
		.align	5
_handle_dbe:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_dbe
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_ov
		.align	5
_handle_ov:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_ov
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_fpe
		.align	5
_handle_fpe:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_fpe
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_bp
		.align	5
_handle_bp:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_bp
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_tr
		.align	5
_handle_tr:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_tr
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_ri
		.align	5
_handle_ri:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_ri
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_cpu
		.align	5
_handle_cpu:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_cpu
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_vcei
		.align	5
_handle_vcei:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_vcei
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_vced
		.align	5
_handle_vced:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_vced
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_watch
		.align	5
_handle_watch:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_watch
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

		.globl	_handle_reserved
		.align	5
_handle_reserved:
		.set	noreorder
		.set	noat
		SAVE_ALL
		STI
		li	t0,-1
		sw	t0,FR_ORIG_REG2(sp)
		jal	_do_reserved
		move	a0,sp			# delay slot
		j	ret_from_sys_call
		nop				# delay slot

/*
 * Exception handler table with 32 entries.
 * This might be extended to handle software exceptions
 */
		.bss
		.globl	_exception_handlers
		.align	2
_exception_handlers:
		.fill	32,4,0

/*
 * Table of syscalls
 */
		.data
_sys_call_table:
		.word	_sys_setup		/* 0 */
		.word	_sys_exit
		.word	_sys_fork
		.word	_sys_read
		.word	_sys_write
		.word	_sys_open		/* 5 */
		.word	_sys_close
		.word	_sys_waitpid
		.word	_sys_creat
		.word	_sys_link
		.word	_sys_unlink		/* 10 */
		.word	_sys_execve
		.word	_sys_chdir
		.word	_sys_time
		.word	_sys_mknod
		.word	_sys_chmod		/* 15 */
		.word	_sys_chown
		.word	_sys_break
		.word	_sys_stat
		.word	_sys_lseek
		.word	_sys_getpid		/* 20 */
		.word	_sys_mount
		.word	_sys_umount
		.word	_sys_setuid
		.word	_sys_getuid
		.word	_sys_stime		/* 25 */
		.word	_sys_ptrace
		.word	_sys_alarm
		.word	_sys_fstat
		.word	_sys_pause
		.word	_sys_utime		/* 30 */
		.word	_sys_stty
		.word	_sys_gtty
		.word	_sys_access
		.word	_sys_nice
		.word	_sys_ftime		/* 35 */
		.word	_sys_sync
		.word	_sys_kill
		.word	_sys_rename
		.word	_sys_mkdir
		.word	_sys_rmdir		/* 40 */
		.word	_sys_dup
		.word	_sys_pipe
		.word	_sys_times
		.word	_sys_prof
		.word	_sys_brk		/* 45 */
		.word	_sys_setgid
		.word	_sys_getgid
		.word	_sys_signal
		.word	_sys_geteuid
		.word	_sys_getegid		/* 50 */
		.word	_sys_acct
		.word	_sys_phys
		.word	_sys_lock
		.word	_sys_ioctl
		.word	_sys_fcntl		/* 55 */
		.word	_sys_mpx
		.word	_sys_setpgid
		.word	_sys_ulimit
		.word	_sys_olduname
		.word	_sys_umask		/* 60 */
		.word	_sys_chroot
		.word	_sys_ustat
		.word	_sys_dup2
		.word	_sys_getppid
		.word	_sys_getpgrp		/* 65 */
		.word	_sys_setsid
		.word	_sys_sigaction
		.word	_sys_sgetmask
		.word	_sys_ssetmask
		.word	_sys_setreuid		/* 70 */
		.word	_sys_setregid
		.word	_sys_sigsuspend
		.word	_sys_sigpending
		.word	_sys_sethostname
		.word	_sys_setrlimit		/* 75 */
		.word	_sys_getrlimit
		.word	_sys_getrusage
		.word	_sys_gettimeofday
		.word	_sys_settimeofday
		.word	_sys_getgroups		/* 80 */
		.word	_sys_setgroups
		.word	_sys_select
		.word	_sys_symlink
		.word	_sys_lstat
		.word	_sys_readlink		/* 85 */
		.word	_sys_uselib
		.word	_sys_swapon
		.word	_sys_reboot
		.word	_sys_readdir
		.word	_sys_mmap		/* 90 */
		.word	_sys_munmap
		.word	_sys_truncate
		.word	_sys_ftruncate
		.word	_sys_fchmod
		.word	_sys_fchown		/* 95 */
		.word	_sys_getpriority
		.word	_sys_setpriority
		.word	_sys_profil
		.word	_sys_statfs
		.word	_sys_fstatfs		/* 100 */
		.word	_sys_ioperm
		.word	_sys_socketcall
		.word	_sys_syslog
		.word	_sys_setitimer
		.word	_sys_getitimer		/* 105 */
		.word	_sys_newstat
		.word	_sys_newlstat
		.word	_sys_newfstat
		.word	_sys_uname
		.word	_sys_iopl		/* 110 */
		.word	_sys_vhangup
		.word	_sys_idle
		.word	0 #_sys_vm86
		.word	_sys_wait4
		.word	_sys_swapoff		/* 115 */
		.word	_sys_sysinfo
		.word	_sys_ipc
		.word	_sys_fsync
		.word	_sys_sigreturn
		.word	_sys_clone		/* 120 */
		.word	_sys_setdomainname
		.word	_sys_newuname
		.word	0 #_sys_modify_ldt
		.word	_sys_adjtimex
		.word	_sys_mprotect		/* 125 */
		.word	_sys_sigprocmask
		.word	_sys_create_module
		.word	_sys_init_module
		.word	_sys_delete_module
		.word	_sys_get_kernel_syms	/* 130 */
		.word	_sys_quotactl
		.word	_sys_getpgid
		.word	_sys_fchdir
		.word	_sys_bdflush
		.word	_sys_sysfs		/* 135 */
		.word	_sys_personality
		.word	0			/* for afs_syscall */
		.word	_sys_setfsuid
		.word	_sys_setfsgid
		.word	_sys_llseek		/* 140 */
		.space	(NR_syscalls-140)*4

		.bss
		.globl	_IRQ_vectors
_IRQ_vectors:	.fill	16,4,0


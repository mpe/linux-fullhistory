/*
 * arch/mips/jazz/pica.S
 *
 * Copyright (C) 1995, 1996 by Ralf Baechle and Andreas Busse
 *
 * Jazz family specific stuff
 *
 * To do: On Jazz machines we remap some non-ISA interrupts to ISA
 *        interrupts.  These interrupts should use their own vectors.
 *        Squeeze the last cycles out of the handlers.  Only a dead
 *        cycle is a good cycle.
 */
#include <asm/asm.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/jazz.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/*
 * jazz_handle_int: Interrupt handler for the ACER Pica-61 boards
 */
		.set	noreorder

		NESTED(jazz_handle_int, PT_SIZE, ra)
		.set	noat
		SAVE_ALL
		REG_S	sp,PT_OR2(sp)
		CLI
		.set	at

		/*
		 * Get pending interrupts
		 */
		mfc0	t0,CP0_CAUSE		# get pending interrupts
		mfc0	t1,CP0_STATUS		# get enabled interrupts
		and	t0,t1			# isolate allowed ones
		andi	t0,0xff00		# isolate pending bits
		beqz	t0,3f
		sll	t0,16			# delay slot

		/*
		 * Find irq with highest priority
		 * FIXME: This is slow - use binary search
		 */
		la	t1,ll_vectors
1:		bltz	t0,2f			# found pending irq
		sll	t0,1
		b	1b
		subu	t1,PTRSIZE		# delay slot

		/*
		 * Do the low-level stuff
		 */
2:		lw	t0,(t1)
		jr	t0
		nop				# delay slot
		END(jazz_handle_int)

ll_sw0:		li	s1,~IE_SW0
		mfc0	t0,CP0_CAUSE
		and	t0,s1
		mtc0	t0,CP0_CAUSE
		PANIC("Unimplemented sw0 handler")

ll_sw1:		li	s1,~IE_SW1
		mfc0	t0,CP0_CAUSE
		and	t0,s1
		mtc0	t0,CP0_CAUSE
		PANIC("Unimplemented sw1 handler")

ll_local_dma:	li	s1,~IE_IRQ0
		PANIC("Unimplemented local_dma handler")

ll_local_dev:	lbu	t0,JAZZ_IO_IRQ_SOURCE
#if PTRSIZE == 8	/* True 64 bit kernel */
		dsll	t0,1
#endif
		.set	reorder
		LONG_L	t0,local_vector(t0)
		jr	t0
		.set	noreorder

/*
 * The braindead PICA hardware gives us no way to distinguish if we really
 * received interrupt 7 from the (E)ISA bus or if we just received an
 * interrupt with no findable cause.  This sometimes happens with braindead
 * cards.  Oh well - for all the Jazz boxes slots are more or less just
 * whistles and bells and we're aware of the problem.
 */
ll_isa_irq:	lw	a0,JAZZ_EISA_IRQ_ACK
		lui	s0,%hi(JAZZ_PORT_BASE)
		li	s1,1
		andi	t0,a0,8				# which pic?
		bnez	t0,ack_second
		andi	a0,7				# delay slot

		/*
		 * Acknowledge first pic
		 */
		lb	t2,%lo(JAZZ_PORT_BASE)+0x21(s0)
		lui	s4,%hi(cache_21)
		lb	t0,%lo(cache_21)(s4)
		sllv	s1,s1,a0
		or	t0,s1
		sb	t0,%lo(cache_21)(s4)
		sb	t0,%lo(JAZZ_PORT_BASE)+0x21(s0)
		li	t2,0x20
		sb	t2,%lo(JAZZ_PORT_BASE)+0x20(s0)
		/*
		 * Now call the real handler
		 */
		la	t3,IRQ_vectors
		sll	t2,a0,PTRLOG
		addu	t3,t2
		LONG_L	t3,(t3)
		jalr	t3
		nop					# delay slot
		/*
		 * Unblock first pic
		 */
		lbu	a0,%lo(JAZZ_PORT_BASE)+0x21(s0)
		lb	a0,%lo(cache_21)(s4)
		nor	s1,zero,s1
		and	a0,s1
		sb	a0,%lo(cache_21)(s4)
		jr	v0
		sb	a0,%lo(JAZZ_PORT_BASE)+0x21(s0)	# delay slot

		.align	5
ack_second:	/*
		 * Acknowledge second pic
		 */
		lbu	t2,%lo(JAZZ_PORT_BASE)+0xa1(s0)
		lui	s4,%hi(cache_A1)
		lb	t3,%lo(cache_A1)(s4)
		sllv	s1,s1,a0
		or	t3,s1
		sb	t3,%lo(cache_A1)(s4)
		sb	t3,%lo(JAZZ_PORT_BASE)+0xa1(s0)
		li	t3,0x20
		sb	t3,%lo(JAZZ_PORT_BASE)+0xa0(s0)
		sb	t3,%lo(JAZZ_PORT_BASE)+0x20(s0)
		/*
		 * Now call the real handler
		 */
		la	t3,IRQ_vectors
		addiu	a0,8
		sll	t2,a0,PTRLOG
		addu	t3,t2
		LONG_L	t3,(t3)
		jalr	t3
		nop					# delay slot

		/*
		 * Unblock second pic
		 */
		lb	a0,%lo(JAZZ_PORT_BASE)+0xa1(s0)
		lb	a0,%lo(cache_A1)(s4)
		nor	s1,zero,s1
		and	a0,s1
		sb	a0,%lo(cache_A1)(s4)
		jr	v0
		sb	a0,%lo(JAZZ_PORT_BASE)+0xa1(s0)	# delay slot

/*
 * Hmm...  This is not just a plain PC clone so the question is
 * which devices on Jazz machines can generate an (E)ISA NMI?
 * (Writing to nonexistant memory?)
 */
ll_isa_nmi:	li	s1,~IE_IRQ3
		PANIC("Unimplemented isa_nmi handler")

/*
 * Timer IRQ - remapped to be more similar to an IBM compatible.
 *
 * The timer interrupt is handled specially to insure that the jiffies
 * variable is updated at all times.  Specifically, the timer interrupt is
 * just like the complete handlers except that it is invoked with interrupts
 * disabled and should never re-enable them.  If other interrupts were
 * allowed to be processed while the timer interrupt is active, then the
 * other interrupts would have to avoid using the jiffies variable for delay
 * and interval timing operations to avoid hanging the system.
 */
ll_timer:	lw	zero,JAZZ_TIMER_REGISTER # timer irq cleared on read
		li	s1,~IE_IRQ4

		li	a0,0
		jal	do_IRQ
		move	a1,sp			# delay slot

		mfc0	t0,CP0_STATUS		# disable interrupts again
		ori	t0,1
		xori	t0,1
		mtc0	t0,CP0_STATUS

		j	ret_from_sys_call
		nop				# delay slot

/*
 * CPU count/compare IRQ (unused)
 */
ll_count:	j	return
		mtc0	zero,CP0_COMPARE

#if 0
/*
 * Call the handler for the interrupt
 * (Currently unused)
 */
call_real:	la	t0,IRQ_vectors

		/*
		 * temporarily disable interrupt
		 */
		mfc0	t2,CP0_STATUS
		and	t2,s1

		addu	t0,t3
		lw	t0,(t0)
		mtc0	t2,CP0_STATUS		# delay slot
		jalr	t0
		nor	s1,zero,s1		# delay slot

		/*
		 * reenable interrupt
		 */
		mfc0	t2,CP0_STATUS
		or	t2,s1
		mtc0	t2,CP0_STATUS

		jr	v0
		nop				# delay slot
#endif

		.data
		PTR	ll_sw0			# SW0
		PTR	ll_sw1			# SW1
		PTR	ll_local_dma		# Local DMA
		PTR	ll_local_dev		# Local devices
		PTR	ll_isa_irq		# ISA IRQ
		PTR	ll_isa_nmi		# ISA NMI
		PTR	ll_timer		# Timer
ll_vectors:	PTR	ll_count		# Count/Compare IRQ

		/*
		 * Interrupt handlers for local devices.
		 */
		.text
loc_no_irq:	PANIC("Unimplemented loc_no_irq handler")
/*
 * Parallel port IRQ, remapped to level 5
 */
loc_parallel:	li	s1,~JAZZ_IE_PARALLEL
		li	a0,JAZZ_PARALLEL_IRQ
		b	loc_call
		li	t3,PTRSIZE*JAZZ_PARALLEL_IRQ 	# delay slot

/*
 * Floppy IRQ, remapped to level 6
 */
loc_floppy:	li	s1,~JAZZ_IE_FLOPPY
		li	a0,JAZZ_FLOPPY_IRQ
		b	loc_call
		li	t3,PTRSIZE*JAZZ_FLOPPY_IRQ 	# delay slot

/*
 * Sound? What sound hardware (whistle) ???
 */
loc_sound:	PANIC("Unimplemented loc_sound handler")
loc_video:	PANIC("Unimplemented loc_video handler")

/*
 * Ethernet interrupt handler, remapped to level 13
 */
loc_ethernet: 	li	s1,~JAZZ_IE_ETHERNET
		li	a0,JAZZ_ETHERNET_IRQ
		b	loc_call
		li	t3,PTRSIZE*JAZZ_ETHERNET_IRQ 	# delay slot

loc_scsi:	PANIC("Unimplemented loc_scsi handler")

/*
 * Keyboard interrupt handler
 */
loc_keyboard:	li	s1,~JAZZ_IE_KEYBOARD
		li	a0,JAZZ_KEYBOARD_IRQ
		b	loc_call
		li	t3,PTRSIZE*JAZZ_KEYBOARD_IRQ	# re-map to irq level 1

loc_mouse:	PANIC("Unimplemented loc_mouse handler")

/*
 * Serial port 1 IRQ, remapped to level 3
 */
loc_serial1:	li	s1,~JAZZ_IE_SERIAL1
		li	a0,JAZZ_SERIAL1_IRQ
		b	loc_call
		li	t3,PTRSIZE*JAZZ_SERIAL1_IRQ 	# delay slot

/*
 * Serial port 2 IRQ, remapped to level 4
 */
loc_serial2:	li	s1,~JAZZ_IE_SERIAL2
		li	a0,JAZZ_SERIAL2_IRQ
		b	loc_call
		li	t3,PTRSIZE*JAZZ_SERIAL2_IRQ 	# delay slot

/*
 * Call the interrupt handler for an interrupt generated by a
 * local device.
 */
loc_call:	la	t0,IRQ_vectors			# delay slot

		/*
		 * Temporarily disable interrupt source
		 */
		lhu	t2,JAZZ_IO_IRQ_ENABLE
		addu	t0,t3				# make ptr to IRQ handler
		lw	t0,(t0)
		and	t2,s1				# delay slot
		sh	t2,JAZZ_IO_IRQ_ENABLE
		jalr	t0				# call IRQ handler
 		nor	s1,zero,s1			# delay slot
 
 		/*
 		 * Reenable interrupt
 		 */
		lhu	t2,JAZZ_IO_IRQ_ENABLE
 		or	t2,s1
		sh	t2,JAZZ_IO_IRQ_ENABLE
 
 		jr	v0
		nop					# delay slot

/*
 * "Jump extender" to reach spurious_interrupt
 */
3:		j	spurious_interrupt
		nop					# delay slot

/*
 * Vectors for interrupts generated by local devices
 */
		.data
local_vector:	PTR	loc_no_irq
		PTR	loc_parallel
		PTR	loc_floppy
		PTR	loc_sound
		PTR	loc_video
		PTR	loc_ethernet
		PTR	loc_scsi
		PTR	loc_keyboard
		PTR	loc_mouse
		PTR	loc_serial1
		PTR	loc_serial2

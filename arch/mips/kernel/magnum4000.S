/*
 * arch/mips/kernel/magnum4000.S
 *
 * Copyright (C) 1995 Waldorf Electronics
 * written by Ralf Baechle and Andreas Busse
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/jazz.h>
#include <asm/stackframe.h>

/*
 * mips_magnum_4000_handle_int: Interrupt handler for Mips Magnum 4000 
 */
		.set	noreorder

		NESTED(mips_magnum_4000_handle_int, FR_SIZE, ra)
		.set	noat
		SAVE_ALL
		CLI
		.set	at

		/*
		 * Get pending interrupts
		 */
		mfc0	t0,CP0_CAUSE		# get pending interrupts
		mfc0	t1,CP0_STATUS		# get enabled interrupts
		and	t0,t1			# isolate allowed ones
		andi	t0,0xff00		# isolate pending bits
		beqz	t0,spurious_interrupt
		sll	t0,16			# delay slot

		/*
		 * Find irq with highest priority
		 * FIXME: This is slow
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
		END(mips_magnum_4000_handle_int)

/*
 * Used for keyboard driver's fake_keyboard_interrupt()
 */
ll_sw0:		li	s1,~IE_SW0
		mfc0	t0,CP0_CAUSE
		and	t0,s1
		mtc0	t0,CP0_CAUSE
	PRINT("sw0 received...\n")
		li	t1,1
		b	call_real
		li	t3,PTRSIZE	# delay slot, re-map to irq level 1

ll_sw1:		li	s1,~IE_SW1
		PANIC("Unimplemented sw1 handler")

ll_local_dma:	li	s1,~IE_IRQ0
		PANIC("Unimplemented local_dma handler")

ll_local_dev:	lbu	t0,JAZZ_IO_IRQ_SOURCE
#if __mips == 3
		dsll	t0,1
		ld	t0,local_vector(t0)
#else /* 32 bit */
		lw	t0,local_vector(t0)
#endif
		jr	t0
		nop


loc_no_irq:	PANIC("Unimplemented loc_no_irq handler")
loc_sound:	PANIC("Unimplemented loc_sound handler")
loc_video:	PANIC("Unimplemented loc_video handler")
loc_scsi:	PANIC("Unimplemented loc_scsi handler")

/*
 * Keyboard interrupt handler
 */
loc_keyboard:	li	s1,~JAZZ_IE_KEYBOARD
		li	t1,JAZZ_KEYBOARD_IRQ
		b	loc_call
		li	t3,PTRSIZE*JAZZ_KEYBOARD_IRQ	# delay slot

/*
 * Ethernet interrupt handler, remapped to level 2
 */
loc_ethernet: /*	PRINT ("ethernet IRQ\n"); */
		li	s1,~JAZZ_IE_ETHERNET
		li	t1,JAZZ_ETHERNET_IRQ
 		b	loc_call
		li	t3,PTRSIZE*JAZZ_ETHERNET_IRQ 	# delay slot


loc_mouse:	PANIC("Unimplemented loc_mouse handler")

/*
 * Serial port 1 IRQ, remapped to level 3
 */
loc_serial1:	li	s1,~JAZZ_IE_SERIAL1
		li	t1,JAZZ_SERIAL1_IRQ
 		b	loc_call
		li	t3,PTRSIZE*JAZZ_SERIAL1_IRQ 	# delay slot

/*
 * Serial port 2 IRQ, remapped to level 4
 */
loc_serial2:	li	s1,~JAZZ_IE_SERIAL2
		li	t1,JAZZ_SERIAL2_IRQ
 		b	loc_call
		li	t3,PTRSIZE*JAZZ_SERIAL2_IRQ 	# delay slot

/*
 * Parallel port IRQ, remapped to level 5
 */
loc_parallel:	li	s1,~JAZZ_IE_PARALLEL
		li	t1,JAZZ_PARALLEL_IRQ
 		b	loc_call
		li	t3,PTRSIZE*JAZZ_PARALLEL_IRQ 	# delay slot

/*
 * Floppy IRQ, remapped to level 6
 */
loc_floppy:	li	s1,~JAZZ_IE_FLOPPY
		li	t1,JAZZ_FLOPPY_IRQ
 		b	loc_call
		li	t3,PTRSIZE*JAZZ_FLOPPY_IRQ 	# delay slot

/*
 * Now call the real handler
 */
loc_call:	lui	s3,%hi(intr_count)
		lw	t2,%lo(intr_count)(s3)
		la	t0,IRQ_vectors			# delay slot
		addiu	t2,1
		sw	t2,%lo(intr_count)(s3)

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
		lw	t1,%lo(intr_count)(s3)		# delay slot
		or	t2,s1
		sh	t2,JAZZ_IO_IRQ_ENABLE

		subu	t1,1
		jr	v0
		sw	t1,%lo(intr_count)(s3)

ll_eisa_irq:	li	s1,~IE_IRQ2
		PANIC("Unimplemented eisa_irq handler")

ll_eisa_nmi:	li	s1,~IE_IRQ3
		PANIC("Unimplemented eisa_nmi handler")

/*
 * Timer IRQ
 * We remap the timer irq to be more similar to a IBM compatible
 */
ll_timer:	lw	t0,JAZZ_TIMER_REGISTER # timer irq cleared on read
		li	s1,~IE_IRQ4
		li	t1,0
		b	call_real
		li	t3,0		# delay slot, re-map to irq level 0

/*
 * CPU count/compare IRQ (unused)
 */
ll_count:	j	return
		mtc0	zero,CP0_COMPARE

/*
 * Now call the real handler
 */
call_real:	lui	s3,%hi(intr_count)
		lw	t2,%lo(intr_count)(s3)
		la	t0,IRQ_vectors			# delay slot
		addiu	t2,1
		sw	t2,%lo(intr_count)(s3)

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

		lw	t2,%lo(intr_count)(s3)
		subu	t2,1

		jr	v0
		sw	t2,%lo(intr_count)(s3)

/*
 * Just for debugging...
 */
		LEAF(drawline)
		li	t1,0xffffffff
		li	t2,0x100
1:		sw	t1,(a0)
		addiu	a0,a0,4
		addiu	t2,t2,-1
		bnez	t2,1b
		nop
		jr 	ra
		nop
		END(drawline)


		.data
		PTR	ll_sw0			# SW0
		PTR	ll_sw1			# SW1
		PTR	ll_local_dma		# Local DMA
		PTR	ll_local_dev		# Local devices
		PTR	ll_eisa_irq		# EISA IRQ
		PTR	ll_eisa_nmi		# EISA NMI
		PTR	ll_timer		# Timer
ll_vectors:	PTR	ll_count		# Count/Compare IRQ

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

		.align	5
LEAF(spurious_interrupt)
		/*
		 * Nothing happened... (whistle)
		 */
		lui	t1,%hi(spurious_count)
		lw	t0,%lo(spurious_count)(t1)
		la	v0,return
		addiu	t0,1
		jr	ra
		sw	t0,%lo(spurious_count)(t1)
		END(spurious_interrupt)


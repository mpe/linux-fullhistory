/*
 * arch/mips/dec/int-handler.S
 *
 * Copyright (C) 1995, 1996 Paul M. Antoine
 *
 * Written by Ralf Baechle and Andreas Busse, modified for DECStation
 * support by Paul Antoine.
 *
 * NOTE: There are references to R4X00 code in here, because there is an
 *	 upgrade module for Personal DECStations with such a CPU!
 *
 * FIXME: still plenty to do in this file, as much of the code hasn't been
 *	  modified to suit the DECStation's interrupts.
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/bootinfo.h>

		.text
		.set	noreorder
/*
 * decstation_handle_int: Interrupt handler for Personal DECStation 5000/2x
 *
 * FIXME: this is *extremely* experimental, though it is probably o.k. for
 *	  most DECStation models.
 */
		NESTED(decstation_handle_int, FR_SIZE, ra)
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
/*
 * FIXME: The following branch was:
 *					beqz	t0,spurious_interrupt
 *
 *	  ...but the wonders of ecoff cause the gas assembler (ver 2.5.1 )
 *	  to complain:
 *
 *		  "Can not represent relocation in this object file format"...
 *
 *	  hence this hack to branch foward a bit, and then jump <sigh>
 *	  Perhaps a later version of gas will cope? - Paul
 */
		beqz	t0,3f;
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
		END(decstation_handle_int)

/*
 * FIXME: The hack mentioned above.
 */
3:		j	spurious_interrupt
		nop

/*
 * FIXME: the rest of this is pretty suspect, as it's straight from
 * 	  jazz.S... and I really haven't altered it at all - Paul
 */

/*
 * Used for keyboard driver's fake_keyboard_interrupt()
 * (Paul, even for i386 this is no longer being used -- Ralf)
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

loc_no_irq:	PANIC("Unimplemented loc_no_irq handler")
loc_sound:	PANIC("Unimplemented loc_sound handler")
loc_video:	PANIC("Unimplemented loc_video handler")
loc_scsi:	PANIC("Unimplemented loc_scsi handler")

/*
 * Ethernet interrupt, remapped to level 15
 * NOTE: Due to a bug somewhere in the kernel I was not able
 * to figure out, the PRINT() is necessary. Without this,
 * I get a "gfp called nonatomically from interrupt 00000000".
 * Only god knows why... Tell me if you find the reason!
 * Andy, 6/16/95
 */
loc_ethernet:	PANIC("Unimplemented loc_ethernet\n")

/*
 * Keyboard interrupt, remapped to level 1
 */
loc_keyboard:	PANIC("Unimplemented loc_keyboard\n")

loc_mouse:	PANIC("Unimplemented loc_mouse handler")

/*
 * Serial port 1 IRQ, remapped to level 3
 */
loc_serial1:	PANIC("Unimplemented loc_serial handler")

/*
 * Serial port 2 IRQ, remapped to level 4
 */
loc_serial2:	PANIC("Unimplemented loc_serial handler")

/*
 * Parallel port IRQ, remapped to level 5
 */
loc_parallel:	PANIC("Unimplemented loc_parallel handler")

/*
 * Floppy IRQ, remapped to level 6
 */
loc_floppy:	PANIC("Unimplemented loc_floppy handler")
  
/*
 * Now call the real handler
 */
loc_call:	la	t0,IRQ_vectors			# delay slot

		/*
		 * Temporarily disable interrupt source
		 */
/*		lhu	t2,JAZZ_IO_IRQ_ENABLE
*/
		addu	t0,t3			# make ptr to IRQ handler
		lw	t0,(t0)
		and	t2,s1			# delay slot
/*		sh	t2,JAZZ_IO_IRQ_ENABLE */
		jalr	t0			# call IRQ handler
		nor	s1,zero,s1		# delay slot

		/*
		 * Reenable interrupt
		 */
/*		lhu	t2,JAZZ_IO_IRQ_ENABLE */
		or	t2,s1
/*		sh	t2,JAZZ_IO_IRQ_ENABLE */

		jr	v0
		nop				# delay slot

ll_tc3:		PANIC("Unimplemented tc3 interrupt handler")

ll_fpu:		PANIC("Unimplemented fpu interrupt handler")

ll_io_error:	PANIC("Unimplemented I/O write timeout interrupt handler")

ll_rtc:		PANIC("Unimplemented RTC interrupt handler")

/*
 * Timer IRQ
 * We remap the timer irq to be more similar to a IBM compatible
 */
ll_timer:	PANIC("Timer interrupt!\n");
/*
 * CPU count/compare IRQ (unused)
 */
ll_reset:	li	a0,0
		jal	pmax_halt
		li	a1,0			# delay slot

/*
 * Now call the real handler
 */
call_real:	la	t0,IRQ_vectors			# delay slot

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

/*
 * Just for debugging... load a0 with address of the point inside the
 * framebuffer at which you want to draw a line of 16x32 pixels.
 * Maxine's framebuffer starts at 0xaa000000.
 */
		.set	reorder
		LEAF(drawline)
		li	t1,0xffffffff		# set all pixels on
		li	t2,0x10			# we will write 16 words
1:		sw	t1,(a0)			# write the first word
		addiu	a0,a0,4			# move our framebuffer pointer
		addiu	t2,t2,-1		# one less to do
		bnez	t2,1b			# finished?
		jr 	ra
		END(drawline)

/*
 * FIXME: I have begun to alter this table to reflect Personal DECStation
 *	  (i.e. Maxine) interrupts... Paul.
 */
		.data
		PTR	ll_sw0			# SW0
		PTR	ll_sw1			# SW1
		PTR	ll_timer		# Periodic interrupt
		PTR	ll_rtc			# RTC periodic interrupt
		PTR	ll_io_error		# Timeout on I/O writes
		PTR	ll_tc3			# TC slot 3, motherboard
		PTR	ll_reset		# Halt keycode (CTRL+ALT+ENTER)
ll_vectors:	PTR	ll_fpu			# FPU

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

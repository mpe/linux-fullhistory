/*
 * arch/mips/kernel/decstation.S
 *
 * Copyright (C) 1995, 1996 Paul M. Antoine
 *
 * Written by Ralf Baechle and Andreas Busse, modified for DECStation
 * support by Paul Antoine.
 *
 * NOTE: There are references to R4X00 code in here, because I believe
 * 	 that there is an upgrade module for Personal DECStations with
 *	 such CPU's!
 *
 * FIXME: still plenty to do in this file, as much of the code towards
 * 	  the end hasn't been modified to suit the DECStation's interrupts.
 *        (Paul, you need to fix this file to comply with NAPS.  Won't be
 *         too hard - Ralf)
 */
#include <asm/asm.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/decstation.h>
#include <asm/stackframe.h>
#include <asm/bootinfo.h>

/*
 * dec_entry: Called at boot in head.S to do DECStation setup, and to
 * fill in the boot_info structure.
 */
		.text
		.globl	dec_entry
dec_entry:
		/* Save the address of the REX call vector for later
		 * use in printing debug messages.
		 */
		sw	a3,pmax_rex_base
		la	a0,dec_signon
		jal	pmax_printf
		nop
		/* Now set up the bootinfo structure with things that
		 * should be loaded by the boot loader, except that
		 * for the moment we're booting using tftp.
		 */
		la	t0,boot_info
		li	t1,0x40			# 64 TLB entries
/*
 * FIXME: Ideally, all DEC workstations should be supported, so here we
 *	  should put some clevernesses to determine machine type and CPU
 *	  type.  Needs a hierarchy of DEC machine types.  Perhaps Machine
 *	  AND Model fields in bootinfo structure?
 */
		sw	t1,OFFSET_BOOTINFO_TLB_ENTRIES(t0)
		li	t1,MACH_DECSTATION	# Machine type
		sw	t1,(t0)
		li	t1,CPU_R3000A		# CPU type
		sw	t1,OFFSET_BOOTINFO_CPUTYPE(t0)
/*
 * FIXME: the following should find the memory size from the boot PROM
 */
		li	t1,0x80000000		# Lower memory bound
		sw	t1,OFFSET_BOOTINFO_MEMLOWER(t0)
		li	t1,0x88000000		# Upper memory bound (8MB)
		sw	t1,OFFSET_BOOTINFO_MEMUPPER(t0)
/*
 * FIXME: the following should determine the cache size a la the method
 *	  used in MACH.  For now we just guess - PMA.
 */
		li	t1,0x100000		# 64K icache
		sw	t1,OFFSET_BOOTINFO_ICACHE_SIZE(t0)
		li	t1,0x100000		# 64K dcache
		sw	t1,OFFSET_BOOTINFO_DCACHE_SIZE(t0)

/*
 * FIXME: template for other bootinfo fields that probably need filling in...
 *
		li	t1,0x80000000
		sw	t1,OFFSET_BOOTINFO_(t0)
*/

/*
 * Now we need to move exception vector handler routines that appear
 * in head.S down to the right addresses, 'cos the DECStation loads
 * kernels at 0x80030000... <sigh>
 */

/*
 * First move the TLB refill code down to offset 0x000, at addr 0x80000000
 */
		la	t0,except_vec0		# begining of TLB exception code
		la	t1,except_vec1		# end of TLB exception code
		la	t2,0x80000000		# where the code should live
		lw	t3,(t0)			# get first word
1:		sw	t3,(t2)			# put it where it should go
		addiu	t0,4			# increment both pointers
		addiu	t2,4
		lw	t3,(t0)			# will be in the delay slot
		bne	t0,t1,1b

/*
 * Now move the General Exception code down to offset 0x080 at 0x80000000
 */
		la	t0,except_vec3		# begining of general exception code
		la	t1,kernel_entry		# end of general exception code
		la	t2,0x80000080		# where the code should live
		lw	t3,(t0)			# get first word
1:		sw	t3,(t2)
		addiu	t0,4
		addiu	t2,4
		lw	t3,(t0)
		bne	t0,t1,1b

/*
 * FIXME: Don't forget to set the gp regster... why do I need this?
 */
		la	gp,_gp
		la	a0,dec_launch		# say where we are going
		jal	pmax_printf
		nop
		j	kernel_entry
		nop

		.data
		.align	2
dec_signon:	.ascii	"\n\nLinux/MIPS DECStation Boot\n";
		.asciiz "Copyright (C) Paul M. Antoine 1995, 1996 and others, 1994, 1995, 1996\n\n";
dec_launch:	.asciiz	"Launching kernel...\n";
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
		REG_S	sp,FR_ORIG_REG2(sp)
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
 *	  (No, this is impossible in COFF as well as in ELF. - Ralf)
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
		.set	reorder
2:		LOAD_L	t0,(t1)
		jr	t0
		.set	noreorder
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
 * (You were fouled by the caches and this is the wrong file for this
 *  comment - Ralf)
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
		LOAD_L	t0,(t0)
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
		LOAD_L	t0,(t0)
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

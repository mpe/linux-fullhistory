/*
 * arch/mips/dec/boot.S
 *
 * Copyright (C) 1995, 1996 Paul M. Antoine
 *
 * Written by Ralf Baechle and Andreas Busse, modified for DECStation
 * support by Paul Antoine.
 *
 * NOTE: There are references to R4X00 code in here, because there is an
 *	 upgrade module for Personal DECStations with such a CPU!
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/bootinfo.h>

/*
 * dec_entry: Called by the boot PROM loader to do DECStation setup, prior
 *	      to calling dec_setup() to fill in the boot_info structure.
 *
 *	      This code should also go in the boot loader for loading off
 *	      floppy and HD... in addition to the tags code in dec_setup().
 *
 *   FIXME:   arrange for this code only to be linked in when building a
 *	      kernel image to be booted via tftp from the boot prom??
 */
		.text
		.globl	dec_entry
dec_entry:
		/* Save the address of the REX call vector for later
		 * use in printing debug messages.
		 */
		sw	a3,pmax_rex_base
		sw	a2,rex_prom_magic
		la	a0,dec_signon
		jal	pmax_printf
		nop

		/* Now set up the bootinfo with things that
		 * should be loaded by the boot loader, except that
		 * for the moment we're booting using tftp.
		 */
		jal	dec_setup
		nop
/*
 * Now we need to move exception vector handler routines that appear
 * in head.S down to the right addresses, 'cos the DECStation loads
 * kernels at 0x80030000... <sigh>
 */

/*
 * First move the TLB refill code down to offset 0x000, at addr 0x80000000
 */
		la	t0,except_vec0		# begining of exception code
		la	t1,except_vec1		# end of exception code
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
		la	t1,end_except		# end of general exception code
		la	t2,0x80000080		# where the code should live
		lw	t3,(t0)			# get first word
1:		sw	t3,(t2)
		addiu	t0,4
		addiu	t2,4
		lw	t3,(t0)
		bne	t0,t1,1b

		la	a0,dec_launch		# say where we are going
		jal	pmax_printf
		nop

		la	t0,mach_mem_upper	# get upper memory bound
		lw	a0,(t0)
		j	kernel_entry
		nop

		.data
		.align	2
dec_signon:	.ascii	"\n\nLinux/MIPS DECStation Boot\n";
		.asciiz "Copyright (C) Paul M. Antoine 1995, 1996 and others, 1994, 1995, 1996\n\n";
dec_launch:	.asciiz	"Setup complete, launching kernel...\n";

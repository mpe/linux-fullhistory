/* $Id: head.S,v 1.13 1998/10/14 20:26:27 ralf Exp $
 *
 * arch/mips/kernel/head.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1995, 1996, 1997, 1998 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Modified for DECStation and hence R3000 support by Paul M. Antoine
 * Further modifications by David S. Miller
 *
 * Head.S contains the MIPS exception handler and startup code.
 */
#include <linux/config.h>
#include <linux/tasks.h>

#include <asm/asm.h>
#include <asm/cacheops.h>
#include <asm/current.h>
#include <asm/offset.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>
#include <asm/stackframe.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>

	.text
	/*
	 * Reserved space for exception handlers.
	 * Necessary for machines which link their kernels at KSEG0.
	 * FIXME: Use the initcode feature to get rid of unused handler
	 * variants.
	 */
	.fill	0x280
/*	
 * This is space for the interrupt handlers.
 * After trap_init() they are located at virtual address KSEG0.
 *
 * These handlers much be written in a relocatable manner
 * because based upon the cpu type an arbitrary one of the
 * following pieces of code will be copied to the KSEG0
 * vector location.
 */
	/* TLB refill, EXL == 0, R4xx0, non-R4600 version */
	.set	noreorder
	.set	noat
	LEAF(except_vec0_r4000)
	.set	mips3
	mfc0	k0, CP0_BADVADDR		# Get faulting address
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22			# get pgd only bits
	lw	k1, THREAD_PGDIR(k1)		# get task pg_dir
	sll	k0, k0, 2
	addu	k1, k1, k0			# add in pgd offset
	mfc0	k0, CP0_CONTEXT			# get context reg
	lw	k1, (k1)
	srl	k0, k0, 1			# get pte offset
	and	k0, k0, 0xff8
	addu	k1, k1, k0			# add in offset
	lw	k0, 0(k1)			# get even pte
	lw	k1, 4(k1)			# get odd pte
	srl	k0, k0, 6			# convert to entrylo0
	mtc0	k0, CP0_ENTRYLO0		# load it
	srl	k1, k1, 6			# convert to entrylo1
	mtc0	k1, CP0_ENTRYLO1		# load it
	b	1f
	 tlbwr					# write random tlb entry
1:	
	nop
	eret					# return from trap
	END(except_vec0_r4000)

	/* TLB refill, EXL == 0, R4600 version */
	LEAF(except_vec0_r4600)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mtc0	k1, CP0_ENTRYLO1
	nop
	tlbwr
	nop
	eret
	END(except_vec0_r4600)

	/* TLB refill, EXL == 0, R4xx0, non-R4600 version */
	.set	noreorder
	.set	noat
	LEAF(except_vec0_nevada)
	.set	mips3
	mfc0	k0, CP0_BADVADDR		# Get faulting address
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22			# get pgd only bits
	lw	k1, THREAD_PGDIR(k1)		# get task pg_dir
	sll	k0, k0, 2
	addu	k1, k1, k0			# add in pgd offset
	lw	k1, (k1)
	mfc0	k0, CP0_CONTEXT			# get context reg
	srl	k0, k0, 1			# get pte offset
	and	k0, k0, 0xff8
	addu	k1, k1, k0			# add in offset
	lw	k0, 0(k1)			# get even pte
	lw	k1, 4(k1)			# get odd pte
	srl	k0, k0, 6			# convert to entrylo0
	mtc0	k0, CP0_ENTRYLO0		# load it
	srl	k1, k1, 6			# convert to entrylo1
	mtc0	k1, CP0_ENTRYLO1		# load it
	tlbwr					# write random tlb entry
	nop
	nop
	eret					# return from trap
	END(except_vec0_nevada)

	/* TLB refill, EXL == 0, R4[40]00/R5000 badvaddr hwbug version */
	LEAF(except_vec0_r45k_bvahwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mfc0	k0, CP0_INDEX
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r45k_bvahwbug)

#ifdef __SMP__
	/* TLB refill, EXL == 0, R4000 MP badvaddr hwbug version */
	LEAF(except_vec0_r4k_mphwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mfc0	k0, CP0_INDEX
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_mphwbug)
#endif

	/* TLB refill, EXL == 0, R4000 UP 250MHZ entrylo[01] hwbug version */
	LEAF(except_vec0_r4k_250MHZhwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	srl	k0, k0, 6
	mtc0	zero, CP0_ENTRYLO0
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mtc0	zero, CP0_ENTRYLO1
	mtc0	k1, CP0_ENTRYLO1
	b	1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_250MHZhwbug)

#ifdef __SMP__
	/* TLB refill, EXL == 0, R4000 MP 250MHZ entrylo[01]+badvaddr bug version */
	LEAF(except_vec0_r4k_MP250MHZhwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	zero, CP0_ENTRYLO0
	mtc0	k0, CP0_ENTRYLO0
	mfc0	k0, CP0_INDEX
	srl	k1, k1, 6
	mtc0	zero, CP0_ENTRYLO1
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_MP250MHZhwbug)
#endif

	/* TLB refill, EXL == 0, R[23]00 version */
	LEAF(except_vec0_r2300)
	.set	mips1
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xffc
	addu	k1, k1, k0
	lw	k0, (k1)
	srl	k0, k0, 12
	mtc0	k0, CP0_ENTRYLO0
	mfc0	k1, CP0_EPC
	tlbwr
	nop
	nop
	nop
	nop
	jr	k1
	rfe
	END(except_vec0_r2300)


	/* XTLB refill, EXL == 0, R4xx0 cpus only use this... */
	NESTED(except_vec1_generic, 0, sp)
	.set	noat
	.set	mips3
	/* Register saving is delayed as long as we don't know
	 * which registers really need to be saved.
	 */
	mfc0	k1, CP0_CONTEXT
	dsra	k1, 1
	lwu	k0,  (k1)		# May cause another exception
	lwu	k1, 4(k1)
	dsrl	k0, 6			# Convert to EntryLo format
	dsrl	k1, 6			# Convert to EntryLo format
	dmtc0	k0, CP0_ENTRYLO0
	dmtc0	k1, CP0_ENTRYLO1
	nop				# Needed for R4[04]00 pipeline
	tlbwr
	nop				# Needed for R4[04]00 pipeline
	nop
	nop
	eret
	nop				/* Workaround for R4000 bug. */
	eret
	END(except_vec1_generic)

	/* Cache Error */
	LEAF(except_vec2_generic)
	/* Famous last words: unreached */
	mfc0	a1,CP0_ERROREPC
	PRINT("Cache error exception: c0_errorepc == %08x\n")
1:
	j	1b
	 nop
	END(except_vec2_generic)

	/* General exception vector R4000 version. */
	NESTED(except_vec3_r4000, 0, sp)
	.set	noat
	mfc0	k1, CP0_CAUSE
	andi	k1, k1, 0x7c
	li	k0, 31<<2
	beq	k1, k0, handle_vced
	 li	k0, 14<<2
	beq	k1, k0, handle_vcei
	 la	k0, exception_handlers
	addu	k0, k0, k1
	lw	k0, (k0)
	nop
	jr	k0
	 nop

/*
 * Big shit, we now may have two dirty primary cache lines for the same
 * physical address.  We can savely invalidate the line pointed to by
 * c0_badvaddr because after return from this exception handler the load /
 * store will be re-executed.
 */
handle_vced:
	mfc0	k0, CP0_BADVADDR
 li k1, -4
 and k0, k1
	mtc0	zero, CP0_TAGLO
 //	nop;nop
	cache	Index_Store_Tag_D,(k0)
 //	nop;nop
	cache	Hit_Writeback_Inv_SD,(k0)
	lui	k0, %hi(vced_count)
	lw	k1, %lo(vced_count)(k0)
	addiu	k1, 1
	sw	k1, %lo(vced_count)(k0)
	eret

handle_vcei:
	mfc0	k0, CP0_BADVADDR
	cache	Hit_Writeback_Inv_SD,(k0)		# also cleans pi
	lui	k0, %hi(vcei_count)
	lw	k1, %lo(vcei_count)(k0)
	addiu	k1, 1
	sw	k1, %lo(vcei_count)(k0)
	eret

	END(except_vec3_r4000)
	.set	at

	/* General exception vector. */
	NESTED(except_vec3_generic, 0, sp)
	.set	noat
	mfc0	k1, CP0_CAUSE
	la	k0, exception_handlers
	andi	k1, k1, 0x7c
	addu	k0, k0, k1
	lw	k0, (k0)
	nop
	jr	k0
	 nop
	END(except_vec3_generic)
	.set	at

	/*
	 * Special interrupt vector for embedded MIPS.  This is a
	 * dedicated interrupt vector which reduces interrupt processing
	 * overhead.  The jump instruction will be inserted here at
	 * initialization time.  This handler may only be 8 bytes in size!
	 */
	NESTED(except_vec4, 0, sp)
1:	j	1b			/* Dummy, will be replaced */
	 nop
	END(except_vec4)

/*
 * Kernel entry point
 */
NESTED(kernel_entry, 16, sp)
	.set	noreorder
	/* The following two symbols are used for kernel profiling. */
	EXPORT(stext)
	EXPORT(_stext)

	/* Determine which MIPS variant we are running on. */
	b	cpu_probe
	 nop

probe_done:

#ifndef CONFIG_SGI
	/* Get the memory upper limit the bootloader passed to us
	 * in a0
	 */
	la	t0, mips_memory_upper
	nop
	sw	a0, (t0)		
#else
	/* On SGI's the firmware/bootloader passes argc/argp/envp
	 * to us as arguments.  But clear bss first because
	 * the romvec and other important info is stored there
	 * by prom_init().
	 */
	la	t0, _edata
	sw	zero, (t0)
	la	t1, (_end - 4)
1:
	addiu	t0, 4
	bne	t0, t1, 1b
	 sw	zero, (t0)

	jal	prom_init /* prom_init(argc, argv, envp); */
	 nop
	jal	sgi_sysinit
	 nop
#endif

#ifdef CONFIG_COBALT_MICRO_SERVER
	jal	SetUpBootInfo
	 nop
#endif

	/* Get the very one tags we need early in the boot process */
	nop
	jal	bi_EarlySnarf
	 nop
#ifndef CONFIG_SGI
	/* Clear BSS first so that there are no surprises... */
	la	t0, _edata
	la	t1, _end
1:	addiu	t0, 1
	bne	t0, t1, 1b
	sb	zero, -1(t0)
#endif
	/*
	 * Determine the mmu/cache attached to this machine,
	 * then flush the tlb and caches.  On the r4xx0
	 * variants this also sets CP0_WIRED to zero.
	 */
	jal	loadmmu
	 nop

	la	t2, mips_cputype
	lw	t4, (t2)
	li	t1, CPU_R2000
	li	t2, CPU_R3000
	li	t3, CPU_R3000A
	beq	t4,t1,2f
	 nop		

	beq	t4,t2,2f
	 nop		

	beq	t4,t3,2f
	 nop		

	jal	wire_mappings_r4xx0
	 nop	

	b	9f
	 nop

2:
	jal	wire_mappings_r3000
	 nop	

	/*
	 * Stack for kernel and init, current variable
	 */
9:	la	$28, init_task_union
	addiu	t0, $28, KERNEL_STACK_SIZE-32
	sw	t0, kernelsp
	subu	sp, t0, 4*SZREG

	/* Disable coprocessors */
	mfc0	t0, CP0_STATUS
	li	t1, ~(ST0_CU1|ST0_CU2|ST0_CU3|ST0_KX|ST0_SX)
	and	t0, t1
	or	t0, ST0_CU0
	mtc0	t0, CP0_STATUS

1:	jal	start_kernel
	 nop
	/*
	 * Main should never return here, but
	 * just in case, we know what happens.
	 */
	b	1b
	 nop					# delay slot
	END(kernel_entry)

/*
 * wire_mappings - used to map hardware registers, r4xx0 version.
 */
LEAF(wire_mappings_r4xx0)
	mtc0	zero, CP0_WIRED
	nop
	nop
	nop
	j	ra
	 nop
	END(wire_mappings_r4xx0)

/*
 * R3000 version of wire_mappings.
 */
LEAF(wire_mappings_r3000)
	/*
	 * Get base address of map0 table for the
	 * the board we're running on
	 */
	lw	t1, mips_machtype
	la	t0, map0table
	sll	t1, PTRLOG		# machtype used as index
	addu	t0, t1
	lw	t0, (t0)		# get base address
	nop
	/* Get number of wired TLB entries and
	 * loop over selected map0 table.
	 */
	lw	t1, (t0)		# number of wired TLB entries
	move	t2, zero		# TLB entry counter
	addiu	t3, t1, 1		# wire one additional entry
	beqz	t1, 2f			# null, exit
	 nop

	addiu	t0, 8
1:
	lw	t4, 24(t0)		# PageMask
	ld	t5,  0(t0)		# entryHi
	ld	t6,  8(t0)		# entryLo0
	addiu	t2, 1			# increment ctr
	mtc0	t2, CP0_INDEX		# set TLB entry
	nop
	mtc0	t5, CP0_ENTRYHI
	nop
	mtc0	t6, CP0_ENTRYLO0
	addiu	t0, 32
	bne	t1, t2, 1b		# next TLB entry
	 tlbwi

	/* We use only 4k pages. Therefore the PageMask register
	 * is expected to be setup for 4k pages.
	 */
2:
	/* Now map the pagetables */
	mtc0	zero, CP0_INDEX
	la	t0, TLB_ROOT
	mtc0	t0, CP0_ENTRYHI
	nop
	la	t0, swapper_pg_dir
	srl	t0, 12
	ori	t0, (0x00e0|0x0100)	# uncachable, dirty, valid
	mtc0	t0, CP0_ENTRYLO0
	nop
	tlbwi				# delayed

	/* Load the context register with zero.  To see why, look
	 * at how the tlb refill code above works.
	 */
	mtc0	zero, CP0_CONTEXT

	jr	ra
	 nop
	END(wire_mappings_r3000)

	/* CPU type probing code, called at Kernel entry. */
	LEAF(cpu_probe)
	mfc0	t0, CP0_PRID
	la	t3, mips_cputype
	andi	t1, t0, 0xff00
	li	t2, PRID_IMP_R2000
	bne	t1, t2, 1f
	 andi	t0, 0x00ff

	li	t2, CPU_R2000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R3000
	bne	t1, t2, 1f
	 nop

	li	t2, PRID_REV_R3000A
	bne	t0, t2, 9f
	 nop

	li	t2, CPU_R3000A
	b	probe_done
	sw	t2, (t3)
9:
	li	t2, CPU_R3000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R6000
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R6000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R4000
	bne	t1, t2, 1f
	 nop

	li	t2, PRID_REV_R4400
	bne	t0, t2, 9f
	 nop

	li	t2, CPU_R4400SC
	b	probe_done
	 sw	t2, (t3)
9:
	li	t2, CPU_R4000SC
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R6000A
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R6000A
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R10000
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R10000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R8000
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R8000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R4600
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R4600
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R4700
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R4700
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R4650
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R4650
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R5000
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R5000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_NEVADA
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_NEVADA
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, CPU_UNKNOWN
	sw	t2, (t3)

	b	probe_done
	 nop
	END(cpu_probe)

	.data
/*
 * Build an entry for table of wired entries
 */
#define MAPDATA(q1,q2,q3,w1)                                    \
		.quad	q1;                                     \
		.quad	q2;                                     \
		.quad	q3;                                     \
		.word	w1;                                     \
		.word	0

/*
 * Initial mapping tables for supported Mips boards.
 * First item is always the number of wired TLB entries,
 * following by EntryHi/EntryLo pairs and page mask.
 * Since everything must be quad-aligned (8) we insert
 * some dummy zeros.
 *
 * Keep in mind that the PFN does not depend on the page size in the
 * TLB page mask register.  See milo's lib/dumptlb.c for how to decode
 * and encode these entries.  Don't see the same routine in the linux
 * kernel distribution, since it is older and unreliable.
 */

/*
 * Address table of mapping tables for supported Mips boards.
 * Add your own stuff here but don't forget to define your
 * target system in bootinfo.h
 */

map0table:	PTR	map0_dummy		# machtype = unknown
		PTR	map0_rpc		# Deskstation rPC44
		PTR	map0_tyne		# Deskstation Tyne
		PTR	map0_pica61		# Acer Pica-61
		PTR	map0_magnum4000		# MIPS Magnum 4000PC (RC4030)
		PTR	map0_dummy
		PTR	map0_dummy		# DEC Personal DECStation 5000/2x (for now)
		PTR	map0_sni_rm200_pci	# SNI RM200 PCI
		PTR	map0_dummy		# SGI INDY

map0_dummy:	.word	0			# 0 entries

		.align  3
/*
 * Deskstation rpc44 mappings.  This machine has its EISA bus at physical
 * address 0xa0000000 which we map for 32M, but that doesn't match EISA
 * spec.  Not sure what to do about this.  Its I/O ports are memory mapped
 * at physical memory location 0xb0000000.
 */
map0_rpc: 	.word   2			# no. of wired TLB entries
		.word	0			# pad for alignment

MAPDATA(0xffffffffe0000000, 0x02800017, 0x00000001, PM_16M)	# ISA Memory space
MAPDATA(0xffffffffe2000000, 0x02c00017, 0x00000001, PM_64K)	# ISA I/O Space

/*
 * Initial mappings for Deskstation Tyne boards.
 */
map0_tyne: 	.word   2			# no. of wired TLB entries
		.word	0			# pad for alignment

MAPDATA(0xffffffffe0000000, 0x04020017, 0x00000001, PM_1M)	# VESA DMA cache
MAPDATA(0xffffffffe2000000, 0x24000017, 0x04000017, PM_16M)	# VESA I/O and memory space

/*
 * Initial mapping for ACER PICA-61 boards.
 * FIXME: These are rather preliminary since many drivers, such as serial,
 * parallel, scsi and ethernet need some changes to distinguish between "local"
 * (built-in) and "optional" (ISA/PCI) I/O hardware. Local video ram is mapped
 * to the same location as the bios maps it to. Console driver has been changed
 * accordingly (new video type: VIDEO_TYPE_PICA_S3).
 * FIXME: Remove or merge some of the mappings.
 */
map0_pica61:	.word  	7			# no. wired TLB entries
		.word	0			# dummy

MAPDATA(0xffffffffe0000000, 0x02000017, 0x00000001, PM_64K)	# Local I/O space
MAPDATA(0xffffffffe0100000, 0x03c00017, 0x00000001, PM_4K)	# Interrupt source register
MAPDATA(0xffffffffe0200000, 0x01800017, 0x01804017, PM_1M)	# Local video control
MAPDATA(0xffffffffe0400000, 0x01808017, 0x0180c017, PM_1M)	# Extended video control
MAPDATA(0xffffffffe0800000, 0x01000017, 0x01010017, PM_4M)	# Local video memory (BIOS mapping)
MAPDATA(0xffffffffe2000000, 0x02400017, 0x02440017, PM_16M)	# ISA I/O and ISA memory space (both 16M)
MAPDATA(0xffffffffffffe000, 0x00000001, 0x0001ffd7, PM_4K)	# PCR (???)

/*
 * Initial mapping for Mips Magnum 4000PC systems.
 * Do you believe me now that the Acer and Mips boxes are nearly the same ? :-)
 * FIXME: Remove or merge some of the mappings.
 */
map0_magnum4000:
		.word  	8			# no. wired TLB entries
		.word	0			# dummy

MAPDATA(0xffffffffe1000000, 0x03ffc013, 0x00000001, PM_256K)	# 0
MAPDATA(0xffffffffe0000000, 0x02000017, 0x00000001, PM_64K)	# 1 local I/O
MAPDATA(0xffffffffe0100000, 0x03c00017, 0x00000001, PM_4K)	# 2 IRQ source
MAPDATA(0xffffffffe0200000, 0x01800017, 0x01804017, PM_1M)	# 3 local video ctrl
MAPDATA(0xffffffffe0400000, 0x01808017, 0x0180c017, PM_1M)	# 4 ext. video ctrl
MAPDATA(0xffffffffe0800000, 0x01000017, 0x01010017, PM_4M)	# 5 local video mem.
MAPDATA(0xffffffffe2000000, 0x02400017, 0x02440017, PM_16M)	# 6 ISA I/O and mem.
MAPDATA(0xffffffffffffe000, 0x00000001, 0x0001ffd7, PM_4K)	# 7 PCR

/*
 * The RM200 doesn't need any wired entries.
 */
map0_sni_rm200_pci:
		.word  	0			# no. wired TLB entries
		.word	0			# dummy

/*
 * This buffer is reserved for the use of the cache error handler.
 */
		.data
		EXPORT(cache_error_buffer)
		.fill	32*4,1,0

EXPORT(kernelsp)
		PTR	0
		.text

		.org	0x1000
EXPORT(swapper_pg_dir)

		.org	0x2000
EXPORT(empty_bad_page)

		.org	0x3000
EXPORT(empty_bad_page_table)

		.org	0x4000
EXPORT(invalid_pte_table)

		.org	0x5000
/* XXX This label is required to keep GAS trying to be too clever ...
   Bug?  */
dummy:
/*
 * Align to 8kb boundary for init_task_union which follows in the
 * .text segment.
 */
		.align	13

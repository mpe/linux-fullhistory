/*
 *  arch/mips/kernel/head.S
 *
 *  Copyright (C) 1994, 1995 Waldorf Electronics
 *  Written by Ralf Baechle and Andreas Busse
 *
 *  Head.S contains the MIPS exception handler and startup code.
 */
#include <linux/tasks.h>

#include <asm/asm.h>
#include <asm/segment.h>
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>
#include <asm/stackframe.h>
#include <asm/bootinfo.h>

#define PAGE_SIZE	0x1000

#define MODE_GLOBAL	0x0001	/* shared for all processes */
#define MODE_ALIAS	0x0016	/* uncachable */

		.text
		.set	mips3
/*
 * This is space for the interrupt handlers.
 * They are located at virtual address KSEG[01] (physical 0x0)
 */
		/*
		 * TLB refill, EXL == 0
		 */
		.set	noreorder
		.set	noat
		LEAF(except_vec0)
		dmfc0	k1,CP0_CONTEXT
		dsra	k1,1
		lwu	k0,(k1)			# May cause another exception
		lwu	k1,4(k1)
		dsrl	k0,6			# Convert to EntryLo format
		dsrl	k1,6			# Convert to EntryLo format
		dmtc0	k0,CP0_ENTRYLO0
		dmtc0	k1,CP0_ENTRYLO1
		nop				# Needed for R4[04]00 pipeline
		tlbwr
		nop				# Needed for R4[04]00 pipeline
		nop
		nop
		eret
		/*
		 * Workaround for R4000 bug.  For explanation see MIPS
		 * docs.  Note that this that obscure that it wont almost
		 * never happen.  Well, but Mips writes about it's bugs.
		 */
		nop
		eret
		END(except_vec0)

		/*
		 * XTLB refill, EXL == 0
		 * Should never be reached
		 */
		.org	except_vec0+0x80
		LEAF(except_vec1)
		PANIC("XTLB Refill exception.\n")
1:		j	1b
		nop
		END(except_vec1)

		/*
		 * Cache Error
		 */
		.org	except_vec1+0x80
		LEAF(except_vec2)
		/*
		 * Famous last words: unreached
		 */
		mfc0	a1,CP0_ERROREPC
		PRINT("Cache error exception: c0_errorepc == %08x\n")
1:		j	1b
		nop
		END(except_vec2)

		/*
		 * General exception vector.
		 */
		.org	except_vec2+0x80
		NESTED(except_vec3, 0, sp)
		.set	noat
		/*
		 * Register saving is delayed as long as we don't know
		 * which registers really need to be saved.
		 */
		mfc0	k1,CP0_CAUSE
		la	k0,exception_handlers
		/*
		 * Next lines assumes that the used CPU type has max.
		 * 32 different types of exceptions. We might use this
		 * to implement software exceptions in the future.
		 */
		andi	k1,0x7c
		addu	k0,k1
		lw	k0,(k0)
		NOP
		jr	k0
		nop
		END(except_vec3)
		.set	at

/******************************************************************************/

/*
 * Kernel entry
 */
		.set	noreorder
		NESTED(kernel_entry, 16, sp)
		/*
		 * The followin two symbols are used for kernel profiling.
		 */
		EXPORT(stext)
		EXPORT(_stext)

#ifdef CONF_DISABLE_KSEG0_CACHING
		/*
		 * Disable all caching for KSEG0.  This option is usefull
		 * when cache trouble with drivers is suspected
		 */
		mfc0	t0,CP0_CONFIG
		ori	t0,7
		xori	t0,5
		mtc0	t0,CP0_CONFIG
#endif
		/*
		 * Clear BSS first so that there are no surprises...
		 */
		la	t0,_edata
		la	t1,_end
		sw	zero,(t0)
1:		addiu	t0,4
		bnel	t0,t1,1b
		sw	zero,(t0)

		/*
		 * Initialize low level part of memory management
		 * First flush the TLB to make shure that we don't get a
		 * TLB shutdown during wire_mappings.
		 */
		jal	tlbflush
		mtc0	zero,CP0_WIRED			# delay slot
		jal	wire_mappings
		nop

		/*
		 * Stack for kernel and init
		 */
		la	sp,init_user_stack+PAGE_SIZE-24
		la	t0,init_kernel_stack+PAGE_SIZE
		sw	t0,kernelsp

		/*
		 * Disable coprocessors; set ST0_CU0 to indicate that
		 * we're running on the kernel stack
		 */
		mfc0	t0,CP0_STATUS
		li	t1,~(ST0_CU1|ST0_CU2|ST0_CU3)
		and	t0,t1
		li	t1,ST0_CU0
		or	t0,ST0_CU0
		mtc0	t0,CP0_STATUS

1:		jal	start_kernel
		nop					# delay slot
		/*
		 * Main should never return here, but
		 * just in case, we know what happens.
		 */
		b	1b
		nop					# delay slot
		END(kernel_entry)

/*
 * wire_mappings - used to map hardware registers
 */
		LEAF(wire_mappings)
		/*
 		 * Get base address of map0 table for the
		 * the board we're running on
		 */
		la	t0,boot_info
		lw	t1,OFFSET_BOOTINFO_MACHTYPE(t0)
		la	t0,map0table
		sll	t1,PTRLOG		# machtype used as index
		addu	t0,t1
		lw	t0,(t0)			# get base address

		/*
		 * Get number of wired TLB entries and
		 * loop over selected map0 table.
		 */
		lw	t1,(t0)			# number of wired TLB entries
		move	t2,zero			# TLB entry counter
		addiu	t3,t1,1			# wire one additional entry
		beqz	t1,2f			# null, exit
		mtc0	t3,CP0_WIRED		# delay slot
		addiu	t0,8
1:		lw	t4,24(t0)		# PageMask
		ld	t5,0(t0)		# entryHi
		ld	t6,8(t0)		# entryLo0
		ld	t7,16(t0)		# entryLo1
		addiu	t2,1			# increment ctr
		mtc0	t2,CP0_INDEX		# set TLB entry
		mtc0	t4,CP0_PAGEMASK
		dmtc0	t5,CP0_ENTRYHI
		dmtc0	t6,CP0_ENTRYLO0
		dmtc0	t7,CP0_ENTRYLO1
		addiu	t0,32
		bne	t1,t2,1b		# next TLB entry
 		tlbwi 				# delay slot

		/*
		 * We use only 4k pages. Therefore the PageMask register
		 * is expected to be setup for 4k pages.
		 */
2:		li	t0,PM_4K
		mtc0	t0,CP0_PAGEMASK

		/*
		 * Now map the pagetables
		 */
		mtc0	zero,CP0_INDEX
		la	t0,TLB_ROOT
		dmtc0	t0,CP0_ENTRYHI
		la	t0,swapper_pg_dir-KSEG1
		srl	t0,6
		ori	t0,(MODE_ALIAS|MODE_GLOBAL) # uncachable, dirty, valid
		dmtc0	t0,CP0_ENTRYLO0
		li	t0,MODE_GLOBAL
		dmtc0	t0,CP0_ENTRYLO1
		nop
		tlbwi				# delayed

		/*
		 * Load the context register with a value that allows
		 * it to be used as fast as possible in tlb exceptions.
		 * It is expected that this register's content will
		 * NEVER be changed.
		 */
		li	t0,TLBMAP
		dsll	t0,1
		dmtc0	t0,CP0_CONTEXT
		jr	ra			# delay slot
		nop
		END(wire_mappings)

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

map0_dummy:	.word	0			# 0 entries

		.align  3
/*
 * Initial mappings for Deskstation rPC boards.
 * RB: Untested goodie - I don't have such a board.
 */
map0_rpc: 	.word   2			# no. of wired TLB entries
		.word	0			# pad for alignment

MAPDATA(0xffffffffe0000000, 0x04020017, 0x00000001, PM_1M)	# VESA DMA cache
MAPDATA(0xffffffffe2000000, 0x24000017, 0x04000017, PM_16M)	# VESA I/O and memory space

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

MAPDATA(0xffffffffe1000000, 0x03ffc013, 0x00000001, 0x7e000)	# 0
MAPDATA(0xffffffffe0000000, 0x02000017, 0x00000001, 0x1e000)	# 1 local I/O
MAPDATA(0xffffffffe0100000, 0x03c00017, 0x00000001, 0)		# 2 IRQ source
MAPDATA(0xffffffffe0200000, 0x01800017, 0x01804017, 0x1fe000)	# 3 local video ctrl
MAPDATA(0xffffffffe0400000, 0x01808017, 0x0180c017, 0x1fe000)	# 4 ext. video ctrl
MAPDATA(0xffffffffe0800000, 0x01000017, 0x01010017, 0x7fe000)	# 5 local video mem.
MAPDATA(0xffffffffe2000000, 0x02400017, 0x02440017, 0x1ffe000)	# 6 ISA I/O and mem.
MAPDATA(0xffffffffffffe000, 0x00000001, 0x0001ffd7, 0)		# 7 PCR


			.text

			.org	0x1000
			.globl	swapper_pg_dir
swapper_pg_dir		=	. + (KSEG1-KSEG0)

/*
 * The page tables are initialized to only 4MB here - the final page
 * tables are set up later depending on memory size.
 */
			.org	0x2000
			EXPORT(pg0)

			.org	0x3000
			EXPORT(empty_bad_page)

			.org	0x4000
			EXPORT(empty_bad_page_table)

			.org	0x5000
			EXPORT(empty_zero_page)

			.org	0x6000
			EXPORT(invalid_pte_table)

			.org	0x7000

			EXPORT(cache_error_buffer)
			.fill	32*4,1,0

			.data
			EXPORT(kernelsp)
			PTR	0

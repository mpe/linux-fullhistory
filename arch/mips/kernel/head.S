/*
 *  mips/head.S
 *
 *  Copyright (C) 1994 Waldorf Electronics
 *  Written by Ralf Baechle and Andreas Busse
 *
 *  Head.S contains the MIPS exception handler and startup code.
 */

#undef DEBUGPICA /* undef this if you have a different system */

#include <linux/tasks.h>

#include <asm/segment.h>
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>
#include <asm/stackframe.h>
#include <asm/regdef.h>
#include <asm/bootinfo.h>
#include <asm/segment.h>

#define PAGE_SIZE	0x1000

/*
 * For now we can't enable write caching. This would cause trouble
 * with the page aliases used by the memory management.
 * The page table's aliases even have to be uncachable, but that
 * doesn't hurt much anyway.
 */
#define PAGE_TABLE	0x0580  /* uncachable */
#define PAGE_SHARED	0x0580  /* cachable, writethrough, no write allocate */
#define MODE_ALIAS	0x0016	/* uncachable */

		.globl	_empty_bad_page
		.globl	_empty_bad_page_table
		.globl	_pg0
		.globl	_empty_zero_page
		.globl	_swapper_pg_dir

		.text
		.globl	_kernelbase
_kernelbase:

/*
 * This is space for the interrupt handlers.
 * They are located at virtual address KSEG[01] (physical 0x0)
 */
		/*
		 * TLB refill, EXL == 0
		 */
		.set	noreorder
		.set	noat
except_vec0:
#if KERNELBASE == KSEG1
		la	k0,1f
		jr	k0
		nop
1:
#endif
		dmfc0	k1,CP0_CONTEXT
		dsra	k1,k1,1
		lwu	k0,(k1)			# May cause another exception
		lwu	k1,4(k1)
		dsrl	k0,k0,6			# Convert to EntryLo format
		dsrl	k1,k1,6			# Convert to EntryLo format
		dmtc0	k0,CP0_ENTRYLO0
		dmtc0	k1,CP0_ENTRYLO1
		tlbwr
		eret

		/*
		 * XTLB refill, EXL == 0
		 * Should never be reached
		 */
		.org	except_vec0+0x80
except_vec1:	
#if KERNELBASE == KSEG1
		la	k0,1f
		jr	k0
		nop
1:
#endif
		la	a0,xtlb_text
		jal	_panic
		nop
1:		j	1b
		nop
xtlb_text:	.asciz	"XTLB Refill exception.\n"

		/*
		 * Cache Error
		 */
		.org	except_vec1+0x80
except_vec2:
#if KERNELBASE == KSEG1
		la	k0,1f
		jr	k0
		nop
1:
#endif
		/*
		 * Should never be reached
		 */
		la	a0,xtlb_text
		jal	_panic
		nop
1:		j	1b
		nop
cache_text:	.asciz	"Cache error exception\n"

		/*
		 * General exception vector.
		 */
		.org	except_vec2+0x80
except_vec3:	/*
		 * Register saving is delayed as long as we don't know
		 * which registers really need to be saved.
		 */
#if KERNELBASE == KSEG1
		la	k0,1f
		jr	k0
		nop
1:
#endif
		.set	noat
		mfc0	k1,CP0_CAUSE
		la	k0,_exception_handlers
		/*
		 * Next lines assumes that the used CPU type has max.
		 * 32 different types of exceptions. We might use this
		 * to implement software exceptions in the future.
		 */
		andi	k1,k1,0x7c
		addu	k0,k0,k1
		lw	k0,(k0)
		FILL_LDS
		jr	k0
		nop

/******************************************************************************/

/*
 * Kernel entry
 */
		.set	noreorder
		.set	at
kernel_entry:
		jal	refill
		nop

		/*
		 * Clear BSS first so that there are no surprises...
		 */
		la	t0,__edata
		la	t1,__end
		sw	zero,(t0)
1:		addiu	t0,t0,4
		bnel	t0,t1,1b
		sw	zero,(t0)

#ifdef DEBUGPICA
		la      t0,_boot_info
		lw      t0,OFFSET_BOOTINFO_VRAM_BASE(t0)
		li      t1,0x0f00 + '3'
		sh      t1,4(t0)
#endif

		.set	noreorder
		jal	_tlbflush
		mtc0	zero,CP0_WIRED
		/*
		 * Spread some mines...
		 */
		la	t0,_end
		la	t1,0x003ffffc
		la	t2,KERNELBASE
		or	t1,t2
		li	t2,0xdeadbeef
1:		sw	t2,(t0)
		bne	t0,t1,1b
		addiu	t0,t0,4
		/*
		 * Initialize memory management, map lowest 4MB
		 */
		.set	reorder
		jal	setup_paging
#if KERNELBASE == KSEG0
		jal	_sys_cacheflush
#endif

#ifdef DEBUGPICA
		la      t0,_boot_info
		lw      t0,OFFSET_BOOTINFO_VRAM_BASE(t0)
		li      t1,0x0f00 + '4'
		sh      t1,6(t0)
#endif
		/*
		 * Stack for kernel and init
		 */
		la	sp,_init_user_stack+PAGE_SIZE-24
		sw	sp,_kernelsp

1:		jal	_start_kernel
		/*
		 * Main should never return here, but
		 * just in case, we know what happens.
		 */
		j	1b

/*
 * Setup_paging
 *
 * Wire mappings for page_tables.
 * The page tables are set up, identity-mapping
 * the first 4MB.  The rest are initialized later.
 */
		.set	noreorder
setup_paging:
		/*
 		 * get base address of map0 table for the
		 * the board we're running on
		 */
		la	t0,_boot_info
		lw	t1,OFFSET_BOOTINFO_MACHTYPE(t0)
		sll	t1,t1,2			# machtype used as index
		la	t0,map0table
		addu	t0,t0,t1
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
		addiu	t0,t0,8
1:		lw	t4,24(t0)		# PageMask
		ld	t5,0(t0)		# entryHi
		ld	t6,8(t0)		# entryLo0
		ld	t7,16(t0)		# entryLo1
		addiu	t2,t2,1			# increment ctr
		mtc0	t2,CP0_INDEX		# set TLB entry
		mtc0	t4,CP0_PAGEMASK
		dmtc0	t5,CP0_ENTRYHI
		dmtc0	t6,CP0_ENTRYLO0
		dmtc0	t7,CP0_ENTRYLO1
 		tlbwi 
		bne	t1,t2,1b		# next TLB entry
		addiu	t0,t0,32		# delay slot

		/*
		 * We use only 4k pages. Therefore the PageMask register
		 * is expected to be setup for 4k pages.
		 */
2:		li	t0,PM_4K
		mtc0	t0,CP0_PAGEMASK

		la	t1,_swapper_pg_dir	# swapper_pg_dir is at 0x1000
		la	t2,_swapper_pg_dir+(PAGE_SIZE-4)
1:		sw	zero,(t1)
		bne	t1,t2,1b
		addiu	t1,t1,4			# delay slot

		/*
		 * Setup invalid_pg_table and
		 * clear page table for the first 4MB
		 */
		la	t0,_pg0			# swapper_pg_dir is at 0x1000
		la	t1,_pg0+PAGE_SIZE
		li	t2,KERNELBASE
		addu	t0,t2
		addu	t1,t2
1:		sw	zero,(t0)
		addiu	t0,t0,4
		bne	t0,t1,1b
		addiu	t2,t2,4			# delay slot

		/*
		 * Identity-map the kernel in low 4MB memory for ease
		 * of transition. Unlike the Intel version the kernel
		 * code/data is automagically being mapped by kseg0.
		 */
		la	t0,_pg0+PAGE_TABLE	# set valid bit/user r/w
		sw	t0,_swapper_pg_dir

		li	t0,PAGE_SHARED		# set valid bit/user r/w
		la	t1,_pg0
		la	t2,_pg0+PAGE_SIZE
		li	t3,KERNELBASE
		addu	t1,t3
		addu	t2,t3
1:		sw	t0,(t1)
		addiu	t1,t1,4
		bne	t1,t2,1b
		addiu	t0,t0,PAGE_SIZE		# delay slot

		/*
		 * Now map the pagetables
		 */
		mtc0	zero,CP0_INDEX
		la	t0,TLB_ROOT
		dmtc0	t0,CP0_ENTRYHI
		la	t0,_swapper_pg_dir
		srl	t0,t0,6
		ori	t0,t0,MODE_ALIAS	# uncachable, dirty, valid
		dmtc0	t0,CP0_ENTRYLO0
		dmtc0	zero,CP0_ENTRYLO1
		tlbwi
		/*
		 * Make page zero unaccessible to catch zero references
		 */
		la	t0,_pg0
		li	t0,KERNELBASE
		addu	t0,t1
		sw	zero,(t0)
		/*
		 * Load the context register with a value that allows
		 * it to be used as fast as possible in tlb exceptions.
		 * It is expected that this register's content never
		 * will be changed.
		 */
		li	t0,TLBMAP
		dsll	t0,t0,1
		jr	ra
		dmtc0	t0,CP0_CONTEXT		# delay slot

		/*
		 * Flush the TLB
		 *
		 * FIXME: knows only how to handle R4x00
		 * Read appendix f of the R4000 manual before you change
		 * something!
		 */
		.globl	_tlbflush
_tlbflush:	li	t0,PM_4K
		mtc0	t0,CP0_PAGEMASK
		lw	t0,_boot_info+OFFSET_BOOTINFO_TLB_ENTRIES(t0)
		dmtc0	zero,CP0_ENTRYLO0
		dmtc0	zero,CP0_ENTRYLO1
		mfc0	t2,CP0_WIRED
1:		subu	t0,t0,1
		mtc0	t0,CP0_INDEX
		lui	t1,0x0008
		or	t1,t0,t1
		dsll	t1,t1,13
		dmtc0	t1,CP0_ENTRYHI
		bne	t2,t0,1b
		tlbwi					# delay slot
		jr	ra
		nop

/*
 * Refill icache
 */
#include <asm/mipsconfig.h>
#include <asm/regdef.h>
#include <asm/segment.h>

#define PAGE_SIZE	0x1000

#define CACHELINES	512             /* number of cachelines    */

		.set	noreorder
		.text
refill:
		/*
		 * Refill icache with cache fill command
		 */
		li	t0,KSEG0
		li	t1,CACHELINES
1:		cache	21,0(t0)
		cache	21,32(t0)
		cache	21,64(t0)
		cache	21,96(t0)
		cache	21,128(t0)
		cache	21,160(t0)
		cache	21,192(t0)
		cache	21,224(t0)
		cache	21,256(t0)
		cache	21,288(t0)
		cache	21,320(t0)
		cache	21,352(t0)
		cache	21,384(t0)
		cache	21,416(t0)
		cache	21,448(t0)
		cache	21,480(t0)
		subu    t1,t1,1
		bnez	t1,1b
		addiu	t0,t0,512			# delay slot

		jr	ra
		nop

/*
 * Just for debugging...
 */
		.globl	_beep
_beep:		lw	t0,beepflag
		nop
		bnez	t0,1f
		lbu	t0,0xe0000061
		xori	t0,t0,3
		sb	t0,0xe0000061
		li	t0,1
		sw	t0,beepflag
1:		jr	ra
		nop

/*
 * Compute kernel code checksum to check kernel code against corruption
 */
		.globl	_csum
#if 0
_csum:		jal	_sys_cacheflush
		move	t8,ra			# delay slot
#else
_csum:		move	t8,ra
#endif
		li	t0,KSEG1
		la	t1,final
		li	t2,KSEG1
		or	t0,t2
		or	t1,t2
		move	v0,zero
1:		lw	t2,(t0)
		addiu	t0,t0,4
		bne	t0,t1,1b
		xor	v0,v0,t2
		jr	t8
		nop
final:

		.data
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

map0table:	.word	map0_dummy		# machtype = unknown
		.word	map0_tyne		# Deskstation Tyne
		.word	map0_pica61		# Acer Pica-61

map0_dummy:	.word	0			# 0 entries

/*
 * Initial mappings for Deskstation Tyne boards.
 */
		.align  8

map0_tyne: 	.word   3			# no. of wired TLB entries
		.word	0			# pad for alignment

# TLB entry 1: ISA I/O

		.quad	0xffffffffe0000000	# TLB #0 EntryHi
		.quad   0x24000017		# TLB #0 EntryLo0
		.quad	0			# TLB #0 EntryLo1
		.word   PM_64K			# page mask
		.word   0			# pad for alignment

# TLB entry 2: ISA memory space

		.quad	0xffffffffe1000000	# TLB #1 EntryHi
		.quad   0x04000017		# TLB #1 EntryLo0
		.quad	0			# TLB #1 EntryLo1
		.word   PM_1M
		.word	0			# pad for alignment

# TLB entry 3: ISA DMA cache

		.quad	0xffffffffe2000000	# TLB #2 EntryHi
		.quad 	0x04020017		# TLB #2 EntryLo0
		.quad	0			# TLB #2 EntryLo1
		.word	PM_1M
		.word 	0			# pad for alignment

/*
 * Initial mapping for ACER PICA-61 boards.
 * FIXME: These are rather preliminary since many drivers,
 * such as serial, parallel, scsi and ethernet need some
 * changes to distinguish between "local" (built-in) and
 * "optional" (ISA/PCI) I/O hardware.
 * Local video ram is mapped to the same location as the
 * bios maps it to. Console driver has been changed
 * accordingly (new video type: VIDEO_TYPE_PICA_S3).
 */

map0_pica61:	.word  	9			# no. wired TLB entries
		.word	0			# dummy

# TLB entry 1: PROM

#		.quad	0xffffffffe1000000	# BIOS mapping
		.quad	0xffffffffe4000000	# new mapping
		.quad	0x03ffc013
		.quad	0x00000001		# global, not valid
		.word	PM_256K
		.word	0

# TLB entry 2: local I/O space

		.quad   0xffffffffe0000000
		.quad 	0x02000017
		.quad	0x00000001		# global, not valid
		.word	PM_64K
		.word	0

# TLB entry 3: DRAM config register

		.quad	0xffffffffe00e0000
		.quad	0x02003817
		.quad	0x02003c17
		.word	PM_64K
		.word	0

# TLB entry 4: Interrupt source register

		.quad	0xffffffffe0100000
		.quad	0x03c00017
		.quad	0x00000001		# global, not valid
		.word	PM_4K
		.word	0

# TLB entry 5: Local video control

		.quad	0xffffffffe0200000
		.quad	0x01800017
		.quad	0x01804017
		.word	PM_1M
		.word	0

# TLB entry 6: Extended video control

		.quad	0xffffffffe0400000
		.quad	0x01808017
		.quad	0x0180c017
		.word	PM_1M
		.word	0

# TLB entry 7: Local video memory (BIOS mapping)

		.quad	0xffffffffe0800000
		.quad	0x01000017
		.quad	0x01010017
		.word   PM_4M
		.word	0

# TLB entry 8: Local video memory (mapped to where Linux expects it)
# not needed anymore
#		.quad	0xffffffffe1000000
#		.quad	0x01000017
#		.quad	0x01010017
#		.word   PM_4M
#		.word	0

# TLB entry 9: ISA I/O and ISA memory space (both 16M)

		.quad	0xffffffffe2000000
		.quad	0x02400017
		.quad	0x02440017
		.word	PM_16M
		.word	0

# TLB entry 10: PCR (???)

		.quad	0xffffffffffffe000
		.quad	0x00000001		# nonsense...
		.quad	0x0001ffd7
		.word	PM_4K
		.word	0


/* ------------------------------------------------
 * Mapping as presented by the PICA BIOS.
 * This table works. Please leave unmodified!
 * ------------------------------------------------ */
#if 0
map0_pica61:	.word	11			# no. wired TLB entries
		.word	0			# dummy

# TLB entry 0: Don't know what this is good for...

		.quad	0xfffffffffffe2000
		.quad	0x0000029e
		.quad	0x00000000
		.word	PM_4K
		.word	0

# TLB entry 1: PROM

		.quad	0xffffffffe1000000
		.quad	0x03ffc013
		.quad	0x00000001		# nonsense ...
		.word	PM_256K
		.word	0

# TLB entry 2: local I/O space

		.quad   0xffffffffe0000000
		.quad 	0x02000017
		.quad	0x00000001		# nonsense ...
		.word	PM_64K
		.word	0

# TLB entry 3: DRAM config register

		.quad	0xffffffffe00e0000
		.quad	0x02003817
		.quad	0x02003c17
		.word	PM_64K
		.word	0

# TLB entry 4: Interrupt source register

		.quad	0xffffffffe0100000
		.quad	0x03c00017
		.quad	0x00000001		# nonsense ...
		.word	PM_4K
		.word	0

# TLB entry 5: Local video control

		.quad	0xffffffffe0200000
		.quad	0x01800017
		.quad	0x01804017
		.word	PM_1M
		.word	0

# TLB entry 6: Extended video control

		.quad	0xffffffffe0400000
		.quad	0x01808017
		.quad	0x0180c017
		.word	PM_1M
		.word	0

# TLB entry 7: Local video memory

		.quad	0xffffffffe0800000
		.quad	0x01000017
		.quad	0x01010017
		.word   PM_4M
		.word	0

# TLB entry 8: ISA I/O space

		.quad	0xffffffffe2000000
		.quad	0x02400017
		.quad	0x02440017
		.word	PM_16M
		.word	0

# TLB entry 9: PCR (???)

		.quad	0xffffffffffffe000
		.quad	0x00000001		# nonsense...
		.quad	0x0001ffd7
		.word	PM_4K
		.word	0

# TLB entry 10: Extended video prom

		.quad	0xffffffff10000000
		.quad	0x0000141f
		.quad	0x00000001		# nonsense
		.word   PM_64K
		.word	0
#endif

/*
 * page 0 is made non-existent, so that kernel NULL pointer references get
 * caught. Thus the swapper page directory has been moved to 0x1000
 *
 * XXX Actually, the swapper page directory is at 0x1000 plus 1 megabyte,
 * with the introduction of the compressed boot code.  Theoretically,
 * the original design of overlaying the startup code with the swapper
 * page directory is still possible --- it would reduce the size of the kernel
 * by 2-3k.  This would be a good thing to do at some point.....
 */
			.text

			.org	0x1000
_swapper_pg_dir		=	0x1000
/*
 * The page tables are initialized to only 4MB here - the final page
 * tables are set up later depending on memory size.
 */
			.org	0x2000
_pg0			=	0x2000

			.org	0x3000
_empty_bad_page		=	0x3000

			.org	0x4000
_empty_bad_page_table	=	0x4000

			.org	0x5000
_empty_zero_page	=	0x5000

			.org	0x6000

#if defined (CONFIG_DESKSTATION_TYNE) && !defined (CONFIG_ACER_PICA_61)
#if 0
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
			.globl	_tmp_floppy_area
_tmp_floppy_area:	.fill	1024,1,0
#endif
/*
 * floppy_track_buffer is used to buffer one track of floppy data: it
 * has to be separate from the tmp_floppy area, as otherwise a single-
 * sector read/write can mess it up. It can contain one full cylinder (sic) of
 * data (36*2*512 bytes).
 */
			.globl	_floppy_track_buffer
_floppy_track_buffer:	.fill	512*2*36,1,0
#endif /* defined (CONFIG_DESKSTATION_TYNE) && !defined (CONFIG_ACER_PICA_61) */
			.globl	_kernelsp
_kernelsp:		.word	0
beepflag:		.word	0

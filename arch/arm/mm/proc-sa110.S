/*
 * linux/arch/arm/mm/sa110.S: MMU functions for SA110
 *
 * (C) 1997 Russell King
 *
 * These are the low level assembler for performing cache and TLB
 * functions on the sa110.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "../lib/constants.h"

/* This is the maximum size of an area which will be flushed.  If the area
 * is larger than this, then we flush the whole cache
 */
#define MAX_AREA_SIZE	32768

		.data
Lclean_switch:	.long	0
		.text

/*
 * Function: sa110_flush_cache_all (void)
 *
 * Purpose : Flush all cache lines
 */
		.align	5
_sa110_flush_cache_all:					@ preserves r0
		mov	r2, #1
_sa110_flush_cache_all_r2:
		ldr	r3, =Lclean_switch
		ldr	r1, [r3]
		ands	r1, r1, #1
		eor	r1, r1, #1
		str	r1, [r3]
		ldr	ip, =0xdf000000
		addne	ip, ip, #32768
		add	r1, ip, #16384			@ only necessary for 16k
1:		ldr	r3, [ip], #32
		teq	r1, ip
		bne	1b
		mov	ip, #0
		tst	r2, #1
		mcrne	p15, 0, ip, c7, c5, 0		@ flush I cache
		mcr	p15, 0, ip, c7, c10, 4		@ drain WB
		mov	pc, lr

/*
 * Function: sa110_flush_cache_area (unsigned long address, int end, int flags)
 *
 * Params  : address	Area start address
 *	   : end	Area end address
 *	   : flags	b0 = I cache as well
 *
 * Purpose : clean & flush all cache lines associated with this area of memory
 */
		.align	5
_sa110_flush_cache_area:
		sub	r3, r1, r0
		cmp	r3, #MAX_AREA_SIZE
		bgt	_sa110_flush_cache_all_r2
1:		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r0, c7, c6, 1		@ flush D entry
		add	r0, r0, #32
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r0, c7, c6, 1		@ flush D entry
		add	r0, r0, #32
		cmp	r0, r1
		blt	1b
		tst	r2, #1
		movne	r0, #0
		mcrne	p15, 0, r0, c7, c5, 0		@ flush I cache
		mov	pc, lr

/*
 * Function: sa110_cache_wback_area(unsigned long address, unsigned long end)
 *
 * Params  : address	Area start address
 *	   : end	Area end address
 *
 * Purpose : ensure all dirty cachelines in the specified area have been
 *	     written out to memory (for DMA)
 */
		.align	5
_sa110_cache_wback_area:
		sub	r3, r1, r0
		cmp	r3, #MAX_AREA_SIZE
		mov	r2, #0
		bgt	_sa110_flush_cache_all_r2
		bic	r0, r0, #31
1:		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		add	r0, r0, #32
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		add	r0, r0, #32
		cmp	r0, r1
		blt	1b
		mcr	p15, 0, r2, c7, c10, 4		@ drain WB
		mov	pc, lr

/*
 * Function: sa110_cache_purge_area(unsigned long address, unsigned long end)
 *
 * Params  : address	Area start address
 *	   : end	Area end address
 *
 * Purpose : throw away all D-cached data in specified region without
 *	     an obligation to write it ack.
 *
 * Note    : Must clean the D-cached entries around the boundaries if the
 *	     start and/or end address are not cache aligned.
 */
		.align	5
_sa110_cache_purge_area:
		tst	r0, #31
		bic	r0, r0, #31
		mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
		tst	r1, #31
		mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
1:		mcr	p15, 0, r0, c7, c6, 1		@ flush D entry
		add	r0, r0, #32
		cmp	r0, r1
		blt	1b
		mov	pc, lr

/*
 * Function: sa110_flush_cache_entry (unsigned long address)
 *
 * Params  : address	Address of cache line to flush
 *
 * Purpose : clean & flush an entry
 */
		.align	5
_sa110_flush_cache_entry:
		mov	r1, #0
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r1, c7, c10, 4		@ drain WB
		mcr	p15, 0, r1, c7, c5, 0		@ flush I cache
		mov	pc, lr

/*
 * Function: sa110_flush_cache_pte (unsigned long address)
 *
 * Params  : address	Address of cache line to clean
 *
 * Purpose : Ensure that physical memory reflects cache at this location
 *	     for page table purposes.
 */
_sa110_flush_cache_pte:
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry	 (drain is done by TLB fns)
		mov	pc, lr

/*
 * Function: sa110_flush_ram_page (unsigned long page)
 *
 * Params  : address	Area start address
 *	   : size	size of area
 *	   : flags	b0 = I cache as well
 *
 * Purpose : clean & flush all cache lines associated with this area of memory
 */
		.align	5
_sa110_flush_ram_page:
		mov	r1, #4096
1:		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r0, c7, c6, 1		@ flush D entry
		add	r0, r0, #32
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r0, c7, c6, 1		@ flush D entry
		add	r0, r0, #32
		subs	r1, r1, #64
		bne	1b
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c7, c5, 0		@ flush I cache
		mov	pc, lr

/*
 * Function: sa110_flush_tlb_all (void)
 *
 * Purpose : flush all TLB entries in all caches
 */
		.align	5
_sa110_flush_tlb_all:
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c8, c7, 0		@ flush I & D tlbs
		mov	pc, lr

/*
 * Function: sa110_flush_tlb_area (unsigned long address, unsigned long end, int flags)
 *
 * Params  : address	Area start address
 *	   : end	Area end address
 *	   : flags	b0 = I cache as well
 *
 * Purpose : flush a TLB entry
 */
		.align	5
_sa110_flush_tlb_area:
		mov	r3, #0
		mcr	p15, 0, r3, c7, c10, 4		@ drain WB
1:		cmp	r0, r1
		mcrlt	p15, 0, r0, c8, c6, 1		@ flush D TLB entry
		addlt	r0, r0, #4096
		cmp	r0, r1
		mcrlt	p15, 0, r0, c8, c6, 1		@ flush D TLB entry
		addlt	r0, r0, #4096
		blt	1b
		tst	r2, #1
		mcrne	p15, 0, r3, c8, c5, 0		@ flush I TLB
		mov	pc, lr

		.align	5
_sa110_flush_icache_area:
		mov	r3, #0
1:		mcr	p15, 0, r0, c7, c10, 1		@ Clean D entry
		add	r0, r0, #32
		cmp	r0, r1
		blt	1b
		mcr	p15, 0, r0, c7, c5, 0		@ flush I cache
		mov	pc, lr
/*
 * Function: sa110_switch_to (struct task_struct *prev, struct task_struct *next)
 *
 * Params  : prev	Old task structure
 *	   : next	New task structure for process to run
 *
 * Purpose : Perform a task switch, saving the old processes state, and restoring
 *	     the new.
 *
 * Notes   : We don't fiddle with the FP registers here - we postpone this until
 *	     the new task actually uses FP.  This way, we don't swap FP for tasks
 *	     that do not require it.
 */
		.align	5
_sa110_switch_to:
		stmfd	sp!, {r4 - r9, fp, lr}		@ Store most regs on stack
		mrs	ip, cpsr
		stmfd	sp!, {ip}			@ Save cpsr_SVC
		str	sp, [r0, #TSS_SAVE]		@ Save sp_SVC
		ldr	sp, [r1, #TSS_SAVE]		@ Get saved sp_SVC
		ldr	r0, [r1, #TSK_ADDR_LIMIT]
		teq	r0, #0
		moveq	r0, #DOM_KERNELDOMAIN
		movne	r0, #DOM_USERDOMAIN
		mcr	p15, 0, r0, c3, c0		@ Set segment
		ldr	r0, [r1, #TSS_MEMMAP]		@ Page table pointer
		ldr	r3, =Lclean_switch
		ldr	r2, [r3]
		ands	r2, r2, #1
		eor	r2, r2, #1
		str	r2, [r3]
		ldr	r2, =0xdf000000
		addne	r2, r2, #32768
		add	r1, r2, #16384			@ only necessary for 16k
1:		ldr	r3, [r2], #32
		teq	r1, r2
		bne	1b
		mov	r1, #0
		mcr	p15, 0, r1, c7, c5, 0		@ flush I cache
		mcr	p15, 0, r1, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
		mcr	p15, 0, r1, c8, c7, 0		@ flush TLBs
		ldmfd	sp!, {ip}
		msr	spsr, ip			@ Save tasks CPSR into SPSR for this return
		ldmfd	sp!, {r4 - r9, fp, pc}^		@ Load all regs saved previously

/*
 * Function: sa110_data_abort ()
 *
 * Params  : r0 = address of aborted instruction
 *
 * Purpose : obtain information about current aborted instruction
 *
 * Returns : r0 = address of abort
 *	   : r1 = FSR
 *	   : r2 != 0 if writing
 */
		.align	5
_sa110_data_abort:
		ldr	r2, [r0]			@ read instruction causing problem
		mrc	p15, 0, r0, c6, c0, 0		@ get FAR
		mov	r2, r2, lsr #19			@ b1 = L
		and	r3, r2, #0x69 << 2
		and	r2, r2, #2
		mrc	p15, 0, r1, c5, c0, 0		@ get FSR
		and	r1, r1, #255
		mov	pc, lr

/*
 * Function: sa110_set_pmd ()
 *
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 *
 * Purpose : Set a PMD and flush it out of any WB cache
 */
		.align	5
_sa110_set_pmd:	str	r1, [r0]
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry	 (drain is done by TLB fns)
		mov	pc, lr

/*
 * Function: sa110_check_bugs (void)
 *	   : sa110_proc_init (void)
 *	   : sa110_proc_fin (void)
 *
 * Notes   : This processor does not require these
 */
_sa110_check_bugs:
		mrs	ip, cpsr
		bic	ip, ip, #F_BIT
		msr	cpsr, ip
_sa110_proc_init:
_sa110_proc_fin:
		mov	pc, lr

/*
 * Function: sa110_reset
 *
 * Notes   : This sets up everything for a reset
 */
_sa110_reset:	mrs	r1, cpsr
		orr	r1, r1, #F_BIT | I_BIT
		msr	cpsr, r1
		stmfd	sp!, {r1, lr}
		mov	r2, #1
		bl	_sa110_flush_cache_all
		bl	_sa110_flush_tlb_all
		mcr	p15, 0, ip, c7, c7, 0		@ flush I,D caches
		mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
		bic	r0, r0, #0x1800
		bic	r0, r0, #0x000f
		ldmfd	sp!, {r1, pc}
/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
_sa110_name:	.ascii	"sa110\0"
		.align

ENTRY(sa110_processor_functions)
		.word	_sa110_name			@  0
		.word	_sa110_switch_to		@  4
		.word	_sa110_data_abort		@  8
		.word	_sa110_check_bugs		@ 12
		.word	_sa110_proc_init		@ 16
		.word	_sa110_proc_fin			@ 20

		.word	_sa110_flush_cache_all		@ 24
		.word	_sa110_flush_cache_area		@ 28
		.word	_sa110_flush_cache_entry	@ 32
		.word	_sa110_flush_cache_pte		@ 36
		.word	_sa110_flush_ram_page		@ 40
		.word	_sa110_flush_tlb_all		@ 44
		.word	_sa110_flush_tlb_area		@ 48

		.word	_sa110_set_pmd			@ 52
		.word	_sa110_reset			@ 56
		.word	_sa110_flush_icache_area	@ 60

		.word	_sa110_cache_wback_area		@ 64
		.word	_sa110_cache_purge_area		@ 68

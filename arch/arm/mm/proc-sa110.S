/*
 * linux/arch/arm/mm/proc-sa110.S: MMU functions for SA110
 *
 * (C) 1997-2000 Russell King
 *
 * These are the low level assembler for performing cache and TLB
 * functions on the StrongARM-110, StrongARM-1100 and StrongARM-1110.
 * 
 * Note that SA1100 and SA1110 share everything but their name and CPU ID.
 *
 * 12-jun-2000, Erik Mouw (J.A.K.Mouw@its.tudelft.nl):
 *   Flush the read buffer at context switches
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/procinfo.h>
#include <asm/hardware.h>
#include "../lib/constants.h"

/* This is the maximum size of an area which will be flushed.  If the area
 * is larger than this, then we flush the whole cache
 */
#define MAX_AREA_SIZE	32768
#define FLUSH_OFFSET	32768

		.macro flush_110_dcache	rd, ra, re
		add	\re, \ra, #16384		@ only necessary for 16k
1001:		ldr	\rd, [\ra], #32
		teq	\re, \ra
		bne	1001b
		.endm

		.macro flush_1100_dcache	rd, ra, re
		add	\re, \ra, #8192			@ only necessary for 8k
1001:		ldr	\rd, [\ra], #32
		teq	\re, \ra
		bne	1001b
#ifdef FLUSH_BASE_MINICACHE
		add	\ra, \ra, #FLUSH_BASE_MINICACHE - FLUSH_BASE
		add	\re, \ra, #512			@ only 512 bytes
1002:		ldr	\rd, [\ra], #32
		teq	\re, \ra
		bne	1002b
#endif
		.endm

		.data
Lclean_switch:	.long	0
		.text

/*
 * Function: sa110_flush_cache_all (void)
 * Purpose : Flush all cache lines
 */
		.align	5
ENTRY(cpu_sa110_flush_cache_all)			@ preserves r0
		mov	r2, #1
cpu_sa110_flush_cache_all_r2:
		ldr	r3, =Lclean_switch
		ldr	ip, =FLUSH_BASE
		ldr	r1, [r3]
		ands	r1, r1, #1
		eor	r1, r1, #1
		str	r1, [r3]
		addne	ip, ip, #FLUSH_OFFSET
		flush_110_dcache	r3, ip, r1
		mov	ip, #0
		teq	r2, #0
		mcrne	p15, 0, ip, c7, c5, 0		@ flush I cache
		mcr	p15, 0, ip, c7, c10, 4		@ drain WB
		mov	pc, lr

		.align	5
ENTRY(cpu_sa1100_flush_cache_all)			@ preserves r0
		mov	r2, #1
cpu_sa1100_flush_cache_all_r2:
		ldr	r3, =Lclean_switch
		ldr	ip, =FLUSH_BASE
		ldr	r1, [r3]
		ands	r1, r1, #1
		eor	r1, r1, #1
		str	r1, [r3]
		addne	ip, ip, #FLUSH_OFFSET
		flush_1100_dcache	r3, ip, r1
		mov	ip, #0
		teq	r2, #0
		mcrne	p15, 0, ip, c7, c5, 0		@ flush I cache
		mcr	p15, 0, r1, c9, c0, 0		@ flush RB
		mcr	p15, 0, ip, c7, c10, 4		@ drain WB
		mov	pc, lr

/*
 * Function: sa110_flush_cache_area (unsigned long address, int end, int flags)
 * Params  : address	Area start address
 *	   : end	Area end address
 *	   : flags	b0 = I cache as well
 * Purpose : clean & flush all cache lines associated with this area of memory
 */
		.align	5
ENTRY(cpu_sa110_flush_cache_area)
		sub	r3, r1, r0
		cmp	r3, #MAX_AREA_SIZE
		bgt	cpu_sa110_flush_cache_all_r2
1:		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r0, c7, c6, 1		@ flush D entry
		add	r0, r0, #32
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r0, c7, c6, 1		@ flush D entry
		add	r0, r0, #32
		cmp	r0, r1
		blt	1b
		teq	r2, #0
		movne	r0, #0
		mcrne	p15, 0, r0, c7, c5, 0		@ flush I cache
		mov	pc, lr

ENTRY(cpu_sa1100_flush_cache_area)
		sub	r3, r1, r0
		cmp	r3, #MAX_AREA_SIZE
		bgt	cpu_sa1100_flush_cache_all_r2
		b	1b

/*
 * Function: sa110_cache_wback_area(unsigned long address, unsigned long end)
 * Params  : address	Area start address
 *	   : end	Area end address
 * Purpose : ensure all dirty cachelines in the specified area have been
 *	     written out to memory (for DMA)
 */
		.align	5
ENTRY(cpu_sa110_cache_wback_area)
		sub	r3, r1, r0
		cmp	r3, #MAX_AREA_SIZE
		mov	r2, #0
		bgt	cpu_sa110_flush_cache_all_r2
		bic	r0, r0, #31
1:		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		add	r0, r0, #32
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		add	r0, r0, #32
		cmp	r0, r1
		blt	1b
		mcr	p15, 0, r2, c7, c10, 4		@ drain WB
		mov	pc, lr

ENTRY(cpu_sa1100_cache_wback_area)
		sub	r3, r1, r0
		cmp	r3, #MAX_AREA_SIZE
		mov	r2, #0
		bgt	cpu_sa1100_flush_cache_all_r2
		bic	r0, r0, #31
		b	1b
/*
 * Function: sa110_cache_purge_area(unsigned long address, unsigned long end)
 * Params  : address	Area start address
 *	   : end	Area end address
 * Purpose : throw away all D-cached data in specified region without
 *	     an obligation to write it back.
 * Note    : Must clean the D-cached entries around the boundaries if the
 *	     start and/or end address are not cache aligned.
 */
		.align	5
ENTRY(cpu_sa110_cache_purge_area)
ENTRY(cpu_sa1100_cache_purge_area)
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
 * Params  : address	Address of cache line to flush
 * Purpose : clean & flush an entry
 */
		.align	5
ENTRY(cpu_sa110_flush_cache_entry)
ENTRY(cpu_sa1100_flush_cache_entry)
		mov	r1, #0
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r1, c7, c10, 4		@ drain WB
		mcr	p15, 0, r1, c7, c5, 0		@ flush I cache
		mov	pc, lr

/*
 * Function: sa110_clean_cache_area(unsigned long start, unsigned long size)
 * Params  : address	Address of cache line to clean
 * Purpose : Ensure that physical memory reflects cache at this location
 *	     for page table purposes.
 */
ENTRY(cpu_sa110_clean_cache_area)
ENTRY(cpu_sa1100_clean_cache_area)
1:		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry	 (drain is done by TLB fns)
		add	r0, r0, #32
		subs	r1, r1, #32
		bhi	1b
		mov	pc, lr

/*
 * Function: sa110_flush_ram_page (unsigned long page)
 * Params  : page	Area start address
 * Purpose : clean all cache lines associated with this area of memory
 */
		.align	5
ENTRY(cpu_sa110_flush_ram_page)
ENTRY(cpu_sa1100_flush_ram_page)
		mov	r1, #4096
1:		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		add	r0, r0, #32
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		add	r0, r0, #32
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		add	r0, r0, #32
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		add	r0, r0, #32
		subs	r1, r1, #128
		bne	1b
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mov	pc, lr

/*
 * Function: sa110_flush_tlb_all (void)
 * Purpose : flush all TLB entries in all caches
 */
		.align	5
ENTRY(cpu_sa110_flush_tlb_all)
ENTRY(cpu_sa1100_flush_tlb_all)
		mov	ip, #0
		mcr	p15, 0, ip, c7, c10, 4		@ drain WB
		mcr	p15, 0, ip, c8, c7, 0		@ flush I & D tlbs
		mov	pc, lr

/*
 * Function: sa110_flush_tlb_area (unsigned long address, unsigned long end, int flags)
 * Params  : address	Area start address
 *	   : end	Area end address
 *	   : flags	b0 = I-TLB as well
 * Purpose : flush a TLB entry
 */
		.align	5
ENTRY(cpu_sa110_flush_tlb_area)
ENTRY(cpu_sa1100_flush_tlb_area)
		mov	r3, #0
		mcr	p15, 0, r3, c7, c10, 4		@ drain WB
1:		cmp	r0, r1
		mcrlt	p15, 0, r0, c8, c6, 1		@ flush D TLB entry
		addlt	r0, r0, #4096
		cmp	r0, r1
		mcrlt	p15, 0, r0, c8, c6, 1		@ flush D TLB entry
		addlt	r0, r0, #4096
		blt	1b
		teq	r2, #0
		mcrne	p15, 0, r3, c8, c5, 0		@ flush I TLB
		mov	pc, lr

/*
 * Function: sa110_flush_tlb_page (unsigned long address, int flags)
 * Params  : address	Address to flush
 *	   : flags	b0 = I-TLB as well
 * Purpose : flush a TLB entry
 */
		.align	5
ENTRY(cpu_sa110_flush_tlb_page)
ENTRY(cpu_sa1100_flush_tlb_page)
		mov	r3, #0
		mcr	p15, 0, r3, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c8, c6, 1		@ flush D TLB entry
		teq	r1, #0
		mcrne	p15, 0, r3, c8, c5, 0		@ flush I TLB
		mov	pc, lr

/*
 * Function: sa110_flush_icache_area (unsigned long address, unsigned long size)
 * Params  : address	Address of area to flush
 *	   : size	Size of area to flush
 * Purpose : flush an area from the Icache
 */
		.align	5
ENTRY(cpu_sa110_flush_icache_area)
ENTRY(cpu_sa1100_flush_icache_area)
1:		mcr	p15, 0, r0, c7, c10, 1		@ Clean D entry
		add	r0, r0, #32
		subs	r1, r1, #32
		bhi	1b
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c7, c5, 0		@ flush I cache
		mov	pc, lr

		.align	5
ENTRY(cpu_sa110_flush_icache_page)
ENTRY(cpu_sa1100_flush_icache_page)
		mcr	p15, 0, r0, c7, c5, 0		@ flush I cache
		mov	pc, lr

/*
 * Function: sa110_data_abort ()
 * Params  : r0 = address of aborted instruction
 * Purpose : obtain information about current aborted instruction
 * Returns : r0 = address of abort
 *	   : r1 != 0 if writing
 *	   : r3 = FSR
 */
		.align	5
ENTRY(cpu_sa110_data_abort)
ENTRY(cpu_sa1100_data_abort)
		ldr	r1, [r0]			@ read instruction causing problem
		mrc	p15, 0, r0, c6, c0, 0		@ get FAR
		mov	r1, r1, lsr #19			@ b1 = L
		and	r1, r1, #2
		mrc	p15, 0, r3, c5, c0, 0		@ get FSR
		and	r3, r3, #255
		mov	pc, lr

		.align	5
/*
 * Function: sa110_set_pgd(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, saving the old processes state, and restoring
 *	     the new.
 */
		.align	5
ENTRY(cpu_sa110_set_pgd)
		ldr	r3, =Lclean_switch
		ldr	ip, =FLUSH_BASE
		ldr	r2, [r3]
		ands	r2, r2, #1
		eor	r2, r2, #1
		str	r2, [r3]
		addne	ip, ip, #FLUSH_OFFSET
		flush_110_dcache	r3, ip, r1
		mov	r1, #0
		mcr	p15, 0, r1, c7, c5, 0		@ flush I cache
		mcr	p15, 0, r1, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
		mcr	p15, 0, r1, c8, c7, 0		@ flush TLBs
		mov	pc, lr

		.align	5
ENTRY(cpu_sa1100_set_pgd)
		ldr	r3, =Lclean_switch
		ldr	ip, =FLUSH_BASE
		ldr	r2, [r3]
		ands	r2, r2, #1
		eor	r2, r2, #1
		str	r2, [r3]
		addne	ip, ip, #FLUSH_OFFSET
		flush_1100_dcache	r3, ip, r1
		mov	r1, #0
		mcr	p15, 0, r1, c7, c5, 0		@ flush I cache
		mcr	p15, 0, r1, c9, c0, 0		@ flush RB
		mcr	p15, 0, r1, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
		mcr	p15, 0, r1, c8, c7, 0		@ flush TLBs
		mov	pc, lr

/*
 * Function: sa110_set_pmd(pmd_t *pmdp, pmd_t pmd)
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 * Purpose : Set a PMD and flush it out
 */
		.align	5
ENTRY(cpu_sa110_set_pmd)
ENTRY(cpu_sa1100_set_pmd)
		str	r1, [r0]
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mov	pc, lr

/*
 * Function: sa110_set_pte(pte_t *ptep, pte_t pte)
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 * Purpose : Set a PTE and flush it out
 */
		.align	5
ENTRY(cpu_sa110_set_pte)
ENTRY(cpu_sa1100_set_pte)
		str	r1, [r0], #-1024		@ linux version

		eor	r1, r1, #LPTE_PRESENT | LPTE_YOUNG | LPTE_WRITE | LPTE_DIRTY

		bic	r2, r1, #0xff0
		bic	r2, r2, #3
		orr	r2, r2, #HPTE_TYPE_SMALL

		tst	r1, #LPTE_USER | LPTE_EXEC	@ User or Exec?
		orrne	r2, r2, #HPTE_AP_READ

		tst	r1, #LPTE_WRITE | LPTE_DIRTY	@ Write and Dirty?
		orreq	r2, r2, #HPTE_AP_WRITE

		tst	r1, #LPTE_PRESENT | LPTE_YOUNG	@ Present and Young?
		movne	r2, #0

		str	r2, [r0]			@ hardware version
		mov	r0, r0
		mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mov	pc, lr

/*
 * Function: sa110_check_bugs (void)
 *	   : sa110_proc_init (void)
 *	   : sa110_proc_fin (void)
 * Notes   : This processor does not require these
 */
ENTRY(cpu_sa110_check_bugs)
ENTRY(cpu_sa1100_check_bugs)
		mrs	ip, cpsr
		bic	ip, ip, #F_BIT
		msr	cpsr, ip
		mov	pc, lr

ENTRY(cpu_sa110_proc_init)
ENTRY(cpu_sa1100_proc_init)
		mov	r0, #0
		mcr	p15, 0, r0, c15, c1, 2		@ Enable clock switching
		mov	pc, lr

ENTRY(cpu_sa110_proc_fin)
		stmfd	sp!, {r1, lr}
		mov	ip, #F_BIT | I_BIT | SVC_MODE
		msr	cpsr_c, ip
		bl	cpu_sa110_flush_cache_all	@ clean caches
1:		mov	r0, #0
		mcr	p15, 0, r0, c15, c2, 2		@ Disable clock switching
		mrc	p15, 0, r0, c1, c0, 0
		bic	r0, r0, #0x1000			@ ...i............
		bic	r0, r0, #0x000e			@ ............wca.
		mcr	p15, 0, r0, c1, c0, 0		@ disable caches
		ldmfd	sp!, {r1, pc}

ENTRY(cpu_sa1100_proc_fin)
		stmfd	sp!, {r1, lr}
		mov	ip, #F_BIT | I_BIT | SVC_MODE
		msr	cpsr_c, ip
		bl	cpu_sa1100_flush_cache_all	@ clean caches
		b	1b


		.align	5
idle:		mcr	p15, 0, r0, c15, c8, 2		@ Wait for interrupt, cache aligned
		mov	r0, r0				@ safety
		mov	pc, lr
/*
 * Function: *_do_idle
 * Params  : r0 = call type:
 *           0 = slow idle
 *           1 = fast idle
 *           2 = switch to slow processor clock
 *           3 = switch to fast processor clock
 */
ENTRY(cpu_sa110_do_idle)
ENTRY(cpu_sa1100_do_idle)
		mov	ip, #0
		cmp	r0, #4
		addcc	pc, pc, r0, lsl #2
		mov	pc, lr

		b	idle
		b	idle
		b	slow_clock
		b	fast_clock

fast_clock:	mcr	p15, 0, ip, c15, c1, 2		@ enable clock switching
		mov	pc, lr

slow_clock:	mcr	p15, 0, ip, c15, c2, 2		@ disable clock switching
		ldr	r1, =UNCACHEABLE_ADDR		@ load from uncacheable loc
		ldr	r1, [r1, #0]			@ force switch to MCLK
		mov	pc, lr

/*
 * Function: sa110_reset
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
		.align	5
ENTRY(cpu_sa110_reset)
ENTRY(cpu_sa1100_reset)
		mov	ip, #0
		mcr	p15, 0, ip, c7, c7, 0		@ flush I,D caches
		mcr	p15, 0, ip, c7, c10, 4		@ drain WB
		mcr	p15, 0, ip, c8, c7, 0		@ flush I & D tlbs
		mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
		bic	ip, ip, #0x000f			@ ............wcam
		bic	ip, ip, #0x1100			@ ...i...s........
		mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
		mov	pc, r0


cpu_manu_name:	.asciz	"Intel"
cpu_sa110_name:	.asciz	"StrongARM-110"
cpu_sa1100_name:
		.asciz	"StrongARM-1100"
cpu_sa1110_name:
		.asciz	"StrongARM-1110"
		.align

		.section ".text.init", #alloc, #execinstr

__sa1100_setup:	@ Allow read-buffer operations from userland
		mcr	p15, 0, r0, c9, c0, 5

__sa110_setup:	mov	r0, #F_BIT | I_BIT | SVC_MODE
		msr	cpsr_c, r0
		mov	r0, #0
		mcr	p15, 0, r0, c7, c7		@ flush I,D caches on v4
		mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
		mcr	p15, 0, r0, c8, c7		@ flush I,D TLBs on v4
		mcr	p15, 0, r4, c2, c0		@ load page table pointer
		mov	r0, #0x1f			@ Domains 0, 1 = client
		mcr	p15, 0, r0, c3, c0		@ load domain access register
		mrc	p15, 0, r0, c1, c0		@ get control register v4
		bic	r0, r0, #0x0e00			@ ....??r.........
		bic	r0, r0, #0x0002			@ ..............a.
		orr	r0, r0, #0x003d			@ ..........DPWC.M
		orr	r0, r0, #0x1100			@ ...I...S........
		mov	pc, lr

		.text

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */

		.type	sa110_processor_functions, #object
ENTRY(sa110_processor_functions)
		.word	cpu_sa110_data_abort
		.word	cpu_sa110_check_bugs
		.word	cpu_sa110_proc_init
		.word	cpu_sa110_proc_fin
		.word	cpu_sa110_flush_cache_all
		.word	cpu_sa110_flush_cache_area
		.word	cpu_sa110_flush_cache_entry
		.word	cpu_sa110_clean_cache_area
		.word	cpu_sa110_flush_ram_page
		.word	cpu_sa110_flush_tlb_all
		.word	cpu_sa110_flush_tlb_area
		.word	cpu_sa110_set_pgd
		.word	cpu_sa110_set_pmd
		.word	cpu_sa110_set_pte
		.word	cpu_sa110_reset
		.word	cpu_sa110_flush_icache_area
		.word	cpu_sa110_cache_wback_area
		.word	cpu_sa110_cache_purge_area
		.word	cpu_sa110_flush_tlb_page
		.word	cpu_sa110_do_idle
		.word	cpu_sa110_flush_icache_page
		.size	sa110_processor_functions, . - sa110_processor_functions

		.type	cpu_sa110_info, #object
cpu_sa110_info:
		.long	cpu_manu_name
		.long	cpu_sa110_name
		.size	cpu_sa110_info, . - cpu_sa110_info


/*
 * SA1100 and SA1110 share the same function calls
 */
		.type	sa1100_processor_functions, #object
ENTRY(sa1100_processor_functions)
		.word	cpu_sa1100_data_abort
		.word	cpu_sa1100_check_bugs
		.word	cpu_sa1100_proc_init
		.word	cpu_sa1100_proc_fin
		.word	cpu_sa1100_flush_cache_all
		.word	cpu_sa1100_flush_cache_area
		.word	cpu_sa1100_flush_cache_entry
		.word	cpu_sa1100_clean_cache_area
		.word	cpu_sa1100_flush_ram_page
		.word	cpu_sa1100_flush_tlb_all
		.word	cpu_sa1100_flush_tlb_area
		.word	cpu_sa1100_set_pgd
		.word	cpu_sa1100_set_pmd
		.word	cpu_sa1100_set_pte
		.word	cpu_sa1100_reset
		.word	cpu_sa1100_flush_icache_area
		.word	cpu_sa1100_cache_wback_area
		.word	cpu_sa1100_cache_purge_area
		.word	cpu_sa1100_flush_tlb_page
		.word	cpu_sa1100_do_idle
		.word	cpu_sa1100_flush_icache_page
		.size	sa1100_processor_functions, . - sa1100_processor_functions

cpu_sa1100_info:
		.long	cpu_manu_name
		.long	cpu_sa1100_name
		.size	cpu_sa1100_info, . - cpu_sa1100_info

cpu_sa1110_info:
		.long	cpu_manu_name
		.long	cpu_sa1110_name
		.size	cpu_sa1110_info, . - cpu_sa1110_info


		.type	cpu_arch_name, #object
cpu_arch_name:	.asciz	"armv4"
		.size	cpu_arch_name, . - cpu_arch_name

		.type	cpu_elf_name, #object
cpu_elf_name:	.asciz	"v4"
		.size	cpu_elf_name, . - cpu_elf_name
		.align

		.section ".proc.info", #alloc, #execinstr

		.type	__sa110_proc_info,#object
__sa110_proc_info:
		.long	0x4401a100
		.long	0xfffffff0
		.long	0x00000c02
		b	__sa110_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT
		.long	cpu_sa110_info
		.long	sa110_processor_functions
		.size	__sa110_proc_info, . - __sa110_proc_info

		.type	__sa1100_proc_info,#object
__sa1100_proc_info:
		.long	0x4401a110
		.long	0xfffffff0
		.long	0x00000c02
		b	__sa1100_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT
		.long	cpu_sa1100_info
		.long	sa1100_processor_functions
		.size	__sa1100_proc_info, . - __sa1100_proc_info

		.type	__sa1110_proc_info,#object
__sa1110_proc_info:
		.long	0x6901b110
		.long	0xfffffff0
		.long	0x00000c02
		b	__sa1100_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT
		.long	cpu_sa1110_info
		.long	sa1100_processor_functions
		.size	__sa1110_proc_info, . - __sa1110_proc_info



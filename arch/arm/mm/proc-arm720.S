/*
 * linux/arch/arm/mm/proc-arm720.S: MMU functions for ARM720
 *
 * Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *                    Rob Scott (rscott@mtrob.fdns.net)
 *
 * These are the low level assembler for performing cache and TLB
 * functions on the ARM720T.
 *
 * Changelog:
 *  05-09-2000  SJH	Created by moving 720 specific functions
 *			out of 'proc-arm6,7.S' per RSK discussion
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/procinfo.h>
#include <asm/errno.h>
#include "../lib/constants.h"

/*
 * Function: arm720_flush_cache_all (void)
 *	   : arm720_flush_cache_page (unsigned long address, int size,
 *                                    int flags)
 *
 * Params  : address	Area start address
 *	   : size	size of area
 *	   : flags	b0 = I cache as well
 *
 * Purpose : Flush all cache lines
 */
ENTRY(cpu_arm720_flush_cache_all)
ENTRY(cpu_arm720_flush_cache_area)
ENTRY(cpu_arm720_flush_cache_entry)
ENTRY(cpu_arm720_flush_icache_area)
ENTRY(cpu_arm720_flush_icache_page)
ENTRY(cpu_arm720_cache_wback_area)
ENTRY(cpu_arm720_cache_purge_area)
		mov	r0, #0
		mcr	p15, 0, r0, c7, c7, 0		@ flush cache
		mov	pc, lr

ENTRY(cpu_arm720_clean_cache_area)
ENTRY(cpu_arm720_flush_ram_page)
		mov	pc, lr

/*
 * Function: arm720_flush_tlb_all (void)
 *
 * Purpose : flush all TLB entries in all caches
 */
ENTRY(cpu_arm720_flush_tlb_all)
		mov	r0, #0
		mcr	p15, 0, r0, c8, c7, 0		@ flush TLB (v4)
		mov	pc, lr

/*
 * Function: arm720_flush_tlb_page (unsigned long address, int end, int flags)
 *
 * Params  : address	Area start address
 *	   : end	Area end address
 *	   : flags	b0 = I cache as well
 *
 * Purpose : flush a TLB entry
 */
ENTRY(cpu_arm720_flush_tlb_area)
1:		mcr	p15, 0, r0, c8, c7, 1		@ flush TLB (v4)
		add	r0, r0, #4096
		cmp	r0, r1
		blt	1b
		mov	pc, lr

/*
 * Function: arm720_flush_tlb_page (unsigned long address, int flags)
 *
 * Params  : address	Address
 *	   : flags	b0 = I-TLB as well
 *
 * Purpose : flush a TLB entry
 */
ENTRY(cpu_arm720_flush_tlb_page)
		mcr	p15, 0, r0, c8, c7, 1		@ flush TLB (v4)
		mov	pc, lr

/*
 * Function: arm720_data_abort ()
 *
 * Params  : r0 = address of aborted instruction
 *
 * Purpose : obtain information about current aborted instruction
 *
 * Returns : r0 = address of abort
 *	   : r1 != 0 if writing
 *	   : r3 = FSR
 *	   : sp = pointer to registers
 */

Ldata_ldmstm:	tst	r4, #1 << 21			@ check writeback bit
		beq	Ldata_simple
		mov	r7, #0x11
		orr	r7, r7, r7, lsl #8
		and	r0, r4, r7
		and	r2, r4, r7, lsl #1
		add	r0, r0, r2, lsr #1
		and	r2, r4, r7, lsl #2
		add	r0, r0, r2, lsr #2
		and	r2, r4, r7, lsl #3
		add	r0, r0, r2, lsr #3
		add	r0, r0, r0, lsr #8
		add	r0, r0, r0, lsr #4
		and	r7, r0, #15			@ r7 = no. of registers to transfer.
		and	r5, r4, #15 << 16		@ Get Rn
		ldr	r0, [sp, r5, lsr #14]		@ Get register
		tst	r4, #1 << 23			@ U bit
		subne	r7, r0, r7, lsl #2
		addeq	r7, r0, r7, lsl #2		@ Do correction (signed)
Ldata_saver7:	str	r7, [sp, r5, lsr #14]		@ Put register
Ldata_simple:	mrc	p15, 0, r0, c6, c0, 0		@ get FAR
		mrc	p15, 0, r3, c5, c0, 0		@ get FSR
		and	r3, r3, #15
		mov	pc, lr

ENTRY(cpu_arm720_data_abort)
		ldr	r4, [r0]			@ read instruction causing problem
		mov	r1, r4, lsr #19			@ r1 b1 = L
		and	r2, r4, #15 << 24
		and	r1, r1, #2			@ check read/write bit
		add	pc, pc, r2, lsr #22		@ Now branch to the relevent processing routine
		movs	pc, lr

		b	Ldata_unknown
		b	Ldata_unknown
		b	Ldata_unknown
		b	Ldata_unknown
		b	Ldata_lateldrpostconst		@ ldr	rd, [rn], #m
		b	Ldata_lateldrpreconst		@ ldr	rd, [rn, #m]	@ RegVal
		b	Ldata_lateldrpostreg		@ ldr	rd, [rn], rm
		b	Ldata_lateldrprereg		@ ldr	rd, [rn, rm]
		b	Ldata_ldmstm			@ ldm*a	rn, <rlist>
		b	Ldata_ldmstm			@ ldm*b	rn, <rlist>
		b	Ldata_unknown
		b	Ldata_unknown
		b	Ldata_simple			@ ldc	rd, [rn], #m	@ Same as ldr	rd, [rn], #m
		b	Ldata_simple			@ ldc	rd, [rn, #m]
		b	Ldata_unknown
Ldata_unknown:	@ Part of jumptable
		mov	r0, r2
		mov	r1, r4
		mov	r2, r3
		b	baddataabort


Ldata_lateldrpreconst:
		tst	r4, #1 << 21			@ check writeback bit
		beq	Ldata_simple
Ldata_lateldrpostconst:
		movs	r2, r4, lsl #20			@ Get offset
		beq	Ldata_simple
		and	r5, r4, #15 << 16		@ Get Rn
		ldr	r0, [sp, r5, lsr #14]
		tst	r4, #1 << 23			@ U bit
		subne	r7, r0, r2, lsr #20
		addeq	r7, r0, r2, lsr #20
		b	Ldata_saver7

Ldata_lateldrprereg:
		tst	r4, #1 << 21			@ check writeback bit
		beq	Ldata_simple
Ldata_lateldrpostreg:
		and	r5, r4, #15
		ldr	r2, [sp, r5, lsl #2]		@ Get Rm
		mov	r3, r4, lsr #7
		ands	r3, r3, #31
		and	r6, r4, #0x70
		orreq	r6, r6, #8
		add	pc, pc, r6
		mov	r0, r0

		mov	r2, r2, lsl r3			@ 0: LSL #!0
		b	1f
		b	1f				@ 1: LSL #0
		mov	r0, r0
		b	1f				@ 2: MUL?
		mov	r0, r0
		b	1f				@ 3: MUL?
		mov	r0, r0
		mov	r2, r2, lsr r3			@ 4: LSR #!0
		b	1f
		mov	r2, r2, lsr #32			@ 5: LSR #32
		b	1f
		b	1f				@ 6: MUL?
		mov	r0, r0
		b	1f				@ 7: MUL?
		mov	r0, r0
		mov	r2, r2, asr r3			@ 8: ASR #!0
		b	1f
		mov	r2, r2, asr #32			@ 9: ASR #32
		b	1f
		b	1f				@ A: MUL?
		mov	r0, r0
		b	1f				@ B: MUL?
		mov	r0, r0
		mov	r2, r2, ror r3			@ C: ROR #!0
		b	1f
		mov	r2, r2, rrx			@ D: RRX
		b	1f
		mov	r0, r0				@ E: MUL?
		mov	r0, r0
		mov	r0, r0				@ F: MUL?


1:		and	r5, r4, #15 << 16		@ Get Rn
		ldr	r0, [sp, r5, lsr #14]
		tst	r4, #1 << 23			@ U bit
		subne	r7, r0, r2
		addeq	r7, r0, r2
		b	Ldata_saver7

/*
 * Function: arm720_check_bugs (void)
 *	   : arm720_proc_init (void)
 *	   : arm720_proc_fin (void)
 *	   : arm720_proc_do_idle (void)
 *
 * Notes   : This processor does not require these
 */
ENTRY(cpu_arm720_check_bugs)
		mrs	ip, cpsr
		bic	ip, ip, #F_BIT
		msr	cpsr, ip
		mov	pc, lr

ENTRY(cpu_arm720_proc_init)
		mov	pc, lr

ENTRY(cpu_arm720_proc_fin)
		mrs	r0, cpsr
		orr	r0, r0, #F_BIT | I_BIT
		msr	cpsr, r0
		mov	r0, #0x31			@ ....S..DP...M
		mcr	p15, 0, r0, c1, c0, 0		@ disable caches
		mov	pc, lr

ENTRY(cpu_arm720_do_idle)
		mov	r0, #-EINVAL
		mov	pc, lr

/*
 * Function: arm720_set_pgd(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, saving the old process' state and restoring
 *	     the new.
 */
ENTRY(cpu_arm720_set_pgd)
		mov	r1, #0
		mcr	p15, 0, r1, c7, c7, 0		@ flush cache
		mcr	p15, 0, r0, c2, c0, 0		@ update page table ptr
		mcr	p15, 0, r1, c8, c7, 0		@ flush TLB (v4)
		mov	pc, lr

/*
 * Function: arm720_set_pmd ()
 *
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 *
 * Purpose : Set a PMD and flush it out of any WB cache
 */
ENTRY(cpu_arm720_set_pmd)
		tst	r1, #3
		orrne	r1, r1, #16			@ Updatable bit is
		str	r1, [r0]			@ always set on ARM720
		mov	pc, lr

/*
 * Function: arm720_set_pte(pte_t *ptep, pte_t pte)
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 * Purpose : Set a PTE and flush it out of any WB cache
 */
		.align	5
ENTRY(cpu_arm720_set_pte)
		str	r1, [r0], #-1024		@ linux version

		eor	r1, r1, #LPTE_PRESENT | LPTE_YOUNG | LPTE_WRITE | LPTE_DIRTY

		bic	r2, r1, #0xff0
		bic	r2, r2, #3
		orr	r2, r2, #HPTE_TYPE_SMALL

		tst	r1, #LPTE_USER | LPTE_EXEC	@ User or Exec?
		orrne	r2, r2, #HPTE_AP_READ

		tst	r1, #LPTE_WRITE | LPTE_DIRTY	@ Write and Dirty?
		orreq	r2, r2, #HPTE_AP_WRITE

		tst	r1, #LPTE_PRESENT | LPTE_YOUNG	@ Present and Young
		movne	r2, #0

		str	r2, [r0]			@ hardware version

		mcr	p15, 0, r0, c7, c7, 0		@ flush cache
		mcr	p15, 0, r0, c8, c7, 0		@ flush TLB (v4)
		mov	pc, lr

/*
 * Function: arm720_reset
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm720_reset)
		mov	r0, #0
		mcr	p15, 0, r0, c7, c7, 0		@ flush cache
		mcr	p15, 0, r0, c8, c7, 0		@ flush TLB (v4)
		mov	pc, lr

	
cpu_armvlsi_name:
		.asciz	"ARM/VLSI"
cpu_arm720_name:
		.asciz	"ARM 720"
		.align

		.section ".text.init", #alloc, #execinstr

__arm720_setup:	mov	r0, #0
		mcr	p15, 0, r0, c7, c7, 0		@ flush caches on v4
		mcr	p15, 0, r0, c8, c7, 0		@ flush TLB (v4)
		mcr	p15, 0, r4, c2, c0		@ load page table pointer
		mov	r0, #0x1f			@ Domains 0, 1 = client
		mcr	p15, 0, r0, c3, c0		@ load domain access register
	
		/* Set CP15 Control reg bits		 (RSBLDPWCAM) */
		mov	r0, #0x7d			@ ...LDPWC.M
		orr	r0, r0, #0x100			@ .S.LDPWC.M
		mov	pc, lr				@ __ret (head-armv.S)

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.type	arm720_processor_functions, #object
ENTRY(arm720_processor_functions)
		.word	cpu_arm720_data_abort
		.word	cpu_arm720_check_bugs
		.word	cpu_arm720_proc_init
		.word	cpu_arm720_proc_fin
		.word	cpu_arm720_flush_cache_all
		.word	cpu_arm720_flush_cache_area
		.word	cpu_arm720_flush_cache_entry
		.word	cpu_arm720_clean_cache_area
		.word	cpu_arm720_flush_ram_page
		.word	cpu_arm720_flush_tlb_all
		.word	cpu_arm720_flush_tlb_area
		.word	cpu_arm720_set_pgd
		.word	cpu_arm720_set_pmd
		.word	cpu_arm720_set_pte
		.word	cpu_arm720_reset
		.word	cpu_arm720_flush_icache_area
		.word	cpu_arm720_cache_wback_area
		.word	cpu_arm720_cache_purge_area
		.word	cpu_arm720_flush_tlb_page
		.word	cpu_arm720_do_idle
		.word	cpu_arm720_flush_icache_page
		.size	arm720_processor_functions, . - arm720_processor_functions

		.type	cpu_arm720_info, #object
cpu_arm720_info:
		.long	cpu_armvlsi_name
		.long	cpu_arm720_name
		.size	cpu_arm720_info, . - cpu_arm720_info
	
		.type	cpu_arch_name, #object
cpu_arch_name:	.asciz	"armv4"
		.size	cpu_arch_name, . - cpu_arch_name

		.type	cpu_elf_name, #object
cpu_elf_name:	.asciz	"v4"
		.size	cpu_elf_name, . - cpu_elf_name
		.align

/*
 * See /include/asm-arm for a definition of this structure.
 */
	
		.section ".proc.info", #alloc, #execinstr

		.type	__arm720_proc_info, #object
__arm720_proc_info:
		.long	0x41807200				@ cpu_val
		.long	0xffffff00				@ cpu_mask
		.long	0x00000c12				@ __cpu_mmu_flags
		b	__arm720_setup				@ --cpu_flush
		.long	cpu_arch_name				@ arch_name
		.long	cpu_elf_name				@ elf_name
		.long	HWCAP_SWP | HWCAP_26BIT			@ elf_hwcap
		.long	cpu_arm720_info				@ info
		.long	arm720_processor_functions
		.size	__arm720_proc_info, . - __arm720_proc_info

/* $Id: r2300_misc.S,v 1.1.1.1 1997/06/01 03:16:42 ralf Exp $
 * r2300_misc.S: Misc. exception handling code for R3000/R2000.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle and Andreas Busse
 *
 * Multi-cpu abstraction reworking:
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/config.h>

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/fpregdef.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
	.set	mips1
	.set	noreorder

	.align	5
	NESTED(r2300_handle_tlbl, PT_SIZE, sp)
	.set	noat
	/* Check whether this is a refill or an invalid exception */
	mfc0	k0,CP0_BADVADDR
	mfc0	k1,CP0_ENTRYHI
	ori	k0,0xfff			# clear ASID...
	xori	k0,0xfff			# in BadVAddr
	andi	k1,0xfc0			# get current ASID
	or	k0,k1				# make new entryhi
	mfc0	k1,CP0_ENTRYHI
	mtc0	k0,CP0_ENTRYHI
	nop					# for pipeline
	nop
	nop
	tlbp
	nop					# for pipeline
	nop
	mfc0	k0,CP0_INDEX

	bgez	k0,invalid_tlbl			# bad addr in c0_badvaddr
	 mtc0	k1,CP0_ENTRYHI

	/* Damn... The next nop is required on the R4400PC V5.0, but
	 * I don't know why - at least there is no documented
	 * reason as for the others :-(
	 * And I haven't tested it as being necessary on R3000 - PMA.
	 * (The R3000 pipeline has only 5 stages, so it's probably not
	 * required -- Ralf)
	 */
	nop

#ifdef CONF_DEBUG_TLB
	/* OK, this is a double fault. Let's see whether this is
	 * due to an invalid entry in the page_table.
	 */
	/* used to be dmfc0 */
	mfc0	k0,CP0_BADVADDR
	/* FIXME: This srl/sll sequence is as it is for the R4xx0,
	 *        and I suspect that it should be different for
	 *	  the R[23]000.  PMA
	 *        (No, it's the assembler way to do
	 *            k0 = k0 / PAGE_SIZE;
	 *            k0 = k0 * sizeof(pte_t)
	 *        Acutally the R4xx0 code will have to change when
	 *        switching to 64 bit ... -- Ralf)
	 */
	srl	k0,12				# get PFN?
	sll	k0,2
	lui	k1,%HI(TLBMAP)
	addu	k0,k1
	lw	k1,(k0)
	andi	k1,(_PAGE_PRESENT|_PAGE_ACCESSED)
	bnez	k1,reload_pgd_entries
	 nop

	.set	noat
	SAVE_ALL
	.set	at
	PRINT("Double fault caused by invalid entries in pgd:\n")
	mfc0	a1,CP0_BADVADDR
	PRINT("Double fault address     : %08lx\n")
	mfc0	a1,CP0_EPC
	PRINT("c0_epc                   : %08lx\n")

	jal	show_regs
	 move	a0,sp

	jal	dump_tlb_nonwired
	 nop

	mfc0	a0,CP0_BADVADDR

	jal	dump_list_current
	 nop

	.set	noat
	STI
	.set	at
	PANIC("Corrupted pagedir")
	.set	noat

reload_pgd_entries:
#endif /* CONF_DEBUG_TLB */

	/* Load missing pair of entries from the pgd and return. */
	mfc0	k1,CP0_CONTEXT
	lw	k0,(k1)			# Never causes nested exception
	mfc0	k1,CP0_EPC		# get the return PC
	srl	k0,12			# Convert to EntryLo format
	mtc0	k0,CP0_ENTRYLO0
	nop				# for pipeline
	tlbwr
	nop				# for pipeline
	nop
	nop
	/* We don't know whether the original access was read or
	 * write, so return and see what happens...
	 */
	jr	k1
	 rfe

	/* Handle invalid exception
	 *
	 * There are two possible causes for an invalid (tlbl)
	 * exception:
	 * 1) pages with present bit set but the valid bit clear
	 * 2) nonexistant pages
	 * Case one needs fast handling, therefore don't save
	 * registers yet.
	 *
	 * k0 contains c0_index.
	 */
invalid_tlbl:	
#ifdef CONFIG_TLB_SHUTDOWN
	/* Remove entry so we don't need to care later
	 * For sake of the pipeline the tlbwi insn has been moved down.
	 * Moving it around is juggling with explosives...
	 */
	/* FIXME: Why is Ralf setting bit 3 of k1?  This may need to
	 *	  be changed for R[236]000! PMA
	 *        (The new ENTRYHI value will then point represent a
	 *        inique virtual address outside the 32 bit address
	 *        limit.  This is just paranoia to avoid a tlb
	 *        shutdown.  This whole part of the routine is probably
	 *        no longer required and can be removed -- Ralf)
	 */
	lui	k1,0x0008
	or	k0,k1
	sll	k0,12				# make it EntryHi format
	mtc0	k0,CP0_ENTRYHI
	mtc0	zero,CP0_ENTRYLO0
#endif
	/* Test present bit in entry */
	mfc0	k0,CP0_BADVADDR
	/* FIXME: This srl/sll sequence is as it is for the R4xx0,
	 *        and I suspect that it should be different for
	 *	  the R[23]000.  PMA
	 *        (No, it's the assembler way to do
	 *            k0 = k0 / PAGE_SIZE;
	 *            k0 = k0 * sizeof(pte_t)
	 *        Acutally the R4xx0 code will have to change when
	 *        switching to 64 bit ... -- Ralf)
	 */
	srl	k0,12
	sll	k0,2
#ifdef CONFIG_TLB_SHUTDOWN
	tlbwi						# do not move!
#endif
	lui	k1,%HI(TLBMAP)
	addu	k0,k1
	lw	k1,(k0)
	andi	k1,(_PAGE_PRESENT|_PAGE_READ)
	xori	k1,(_PAGE_PRESENT|_PAGE_READ)

	bnez	k1,nopage_tlbl
	 lw	k1,(k0)

	/* Present and read bits are set -> set valid and accessed bits */
	ori	k1,(_PAGE_VALID|_PAGE_ACCESSED)
	sw	k1,(k0)
	mfc0	k1,CP0_EPC
	nop

	jr	k1
	 rfe

	/* Page doesn't exist. Lots of work which is less important
	 * for speed needs to be done, so hand it all over to the
	 * kernel memory management routines.
	 */
nopage_tlbl:
	SAVE_ALL
	mfc0	a2,CP0_BADVADDR
	STI
	.set	at
	/* a0 (struct pt_regs *) regs
	 * a1 (unsigned long)    0 for read access
	 * a2 (unsigned long)    faulting virtual address
	 */
	move	a0,sp
	jal	do_page_fault
	 li	a1,0

	j	ret_from_sys_call
	 nop
	END(r2300_handle_tlbl)


	.text
	.align	5
	NESTED(r2300_handle_tlbs, PT_SIZE, sp)
	.set	noat
	/* It is impossible that is a nested reload exception.
	 * Therefore this must be a invalid exception.
	 * Two possible cases:
	 * 1) Page exists but not dirty.
	 * 2) Page doesn't exist yet. Hand over to the kernel.
	 *
	 * Test whether present bit in entry is set
	 */
	/* used to be dmfc0 */
	mfc0	k0,CP0_BADVADDR
	/* FIXME: This srl/sll sequence is as it is for the R4xx0,
	 *        and I suspect that it should be different for
	 *	  the R[23]000.  PMA
	 */
	srl	k0,12
	sll	k0,2
	lui	k1,%HI(TLBMAP)
	addu	k0,k1
	lw	k1,(k0)
	tlbp					# find faulting entry
	andi	k1,(_PAGE_PRESENT|_PAGE_WRITE)
	xori	k1,(_PAGE_PRESENT|_PAGE_WRITE)

	bnez	k1,nopage_tlbs
	 lw	k1,(k0)

	/* Present and writable bits set: set accessed and dirty bits. */
	ori	k1,k1,(_PAGE_ACCESSED|_PAGE_MODIFIED| \
		       _PAGE_VALID|_PAGE_DIRTY)
	sw	k1,(k0)
	/* Now reload the entry into the TLB */
	/* FIXME: Why has Ralf set bit 2?  Should it be different for
	 *	  R[23]000?  PMA
	 *        (The ori/xori combination actually _clears_ bit 2.
	 *        This is required for the R4xx0 these CPUs always
	 *        map page pairs; a page pair of 4k pages therfore
	 *        has always an address with bit 2 set to zero. -- Ralf)
	 */
	ori	k0,0x0004
	xori	k0,0x0004
	lw	k0,(k0)
	srl	k0,12
	mtc0	k0,CP0_ENTRYLO0
	mfc0	k1,CP0_EPC
	nop				# for pipeline
	tlbwi
	nop				# for pipeline
	nop
	nop

	jr	k1
	 rfe

	/* Page doesn't exist. Lots of work which is less important
	 * for speed needs to be done, so hand it all over to the
	 * kernel memory management routines.
	 */
nopage_tlbs:
nowrite_mod:
#ifdef CONFIG_TLB_SHUTDOWN
	/* Remove entry so we don't need to care later */
	mfc0	k0,CP0_INDEX
#ifdef CONF_DEBUG_TLB
	bgez	k0,2f
	 nop
	/* We got a tlbs exception but found no matching entry in
	 * the tlb.  This should never happen.  Paranoia makes us
	 * check it, though.
	 */
	SAVE_ALL
	jal	show_regs
	 move	a0,sp
	.set	at
	mfc0	a1,CP0_BADVADDR
	PRINT("c0_badvaddr == %08lx\n")
	mfc0	a1,CP0_INDEX
	PRINT("c0_index    == %08x\n")
	mfc0	a1,CP0_ENTRYHI
	PRINT("c0_entryhi  == %08x\n")
	.set	noat
	STI
	.set	at
	PANIC("Tlbs or tlbm exception with no matching entry in tlb")
1:
	j	1b
	 nop
2:
#endif /* CONF_DEBUG_TLB */
	/* FIXME: Why is Ralf setting bit 3 of k1?  This may need to
	 *	  be changed for R[236]000! PMA
	 *        (The new ENTRYHI value will then point represent a
	 *        inique virtual address outside the 32 bit address
	 *        limit.  This is just paranoia to avoid a tlb
	 *        shutdown.  This whole part of the routine is probably
	 *        no longer required and can be removed -- Ralf)
	 */
	lui	k1,0x0008
	or	k0,k1
	sll	k0,12
	mtc0	k0,CP0_ENTRYHI
	mtc0	zero,CP0_ENTRYLO0
	nop				# for pipeline
	nop				# R4000 V2.2 requires 4 NOPs
	nop
	nop
	tlbwi
#endif /* CONFIG_TLB_SHUTDOWN */
	.set	noat
	SAVE_ALL
	mfc0	a2,CP0_BADVADDR
	STI
	.set	at
	/* a0 (struct pt_regs *) regs
	 * a1 (unsigned long)    1 for write access
	 * a2 (unsigned long)    faulting virtual address
	 */
	move	a0,sp
	jal	do_page_fault
	 li	a1,1

	j	ret_from_sys_call
	 nop
	END(r2300_handle_tlbs)


	.align	5
	NESTED(r2300_handle_mod, PT_SIZE, sp)
	.set	noat
	/* Two possible cases:
	 * 1) Page is writable but not dirty -> set dirty and return
	 * 2) Page is not writable -> call C handler
	 */
	/* used to be dmfc0 */
	mfc0	k0,CP0_BADVADDR
	/* FIXME: This srl/sll sequence is as it is for the R4xx0,
	 *        and I suspect that it should be different for
	 *	  the R[23]000.  PMA
	 */
	srl	k0,12
	sll	k0,2
	lui	k1,%HI(TLBMAP)
	addu	k0,k1
	lw	k1,(k0)
	tlbp					# find faulting entry
	andi	k1,_PAGE_WRITE

	beqz	k1,nowrite_mod
	 lw	k1,(k0)

	/* Present and writable bits set: set accessed and dirty bits. */
	ori	k1,(_PAGE_ACCESSED|_PAGE_DIRTY)
	sw	k1,(k0)
	/* Now reload the entry into the tlb */
	/* FIXME: Why has Ralf set bit 2?  Should it be different for
	 *	  R[23]000?  PMA
	 *        (The ori/xori combination actually _clears_ bit 2.
	 *        This is required for the R4xx0 these CPUs always
	 *        map page pairs; a page pair of 4k pages therfore
	 *        has always an address with bit 2 set to zero. -- Ralf)
	 */
	ori	k0,0x0004
	xori	k0,0x0004
	lw	k0,(k0)
	srl	k0,12
	mtc0	k0,CP0_ENTRYLO0
	mfc0	k1,CP0_EPC
	nop				# for pipeline
	nop
	nop
	tlbwi
	nop				# for pipeline
	nop
	nop

	jr	k1
	 rfe
	END(r2300_handle_mod)
	.set	at

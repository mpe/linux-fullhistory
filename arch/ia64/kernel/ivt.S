/*
 * arch/ia64/kernel/ivt.S
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1998-2000 David Mosberger <davidm@hpl.hp.com>
 */
/*
 * This file defines the interrupt vector table used by the CPU.
 * It does not include one entry per possible cause of interruption.
 *
 * External interrupts only use 1 entry. All others are internal interrupts
 *
 * The first 20 entries of the table contain 64 bundles each while the 
 * remaining 48 entries contain only 16 bundles each.
 *
 * The 64 bundles are used to allow inlining the whole handler for critical
 * interrupts like TLB misses.
 *
 *  For each entry, the comment is as follows:
 *
 * 		// 0x1c00 Entry 7 (size 64 bundles) Data Key Miss (12,51)
 *  entry offset ----/     /         /                  /          /
 *  entry number ---------/         /                  /          /
 *  size of the entry -------------/                  /          /
 *  vector name -------------------------------------/          /
 *  related interrupts (what is the real interrupt?) ----------/
 *
 * The table is 32KB in size and must be aligned on 32KB boundary.
 * (The CPU ignores the 15 lower bits of the address)
 *
 * Table is based upon EAS2.6 (Oct 1999)
 */

#include <linux/config.h>

#include <asm/break.h>
#include <asm/offsets.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/unistd.h>

#define MINSTATE_START_SAVE_MIN	/* no special action needed */
#define MINSTATE_END_SAVE_MIN									\
	or r2=r2,r14;		/* make first base a kernel virtual address */			\
	or r12=r12,r14;		/* make sp a kernel virtual address */				\
	or r13=r13,r14;		/* make `current' a kernel virtual address */			\
	bsw.1;			/* switch back to bank 1 (must be last in insn group) */	\
	;;

#include "minstate.h"

#define FAULT(n)									\
	rsm psr.dt;			/* avoid nested faults due to TLB misses... */	\
	;;										\
	srlz.d;				/* ensure everyone knows psr.dt is off... */	\
	mov r31=pr;									\
	mov r19=n;;			/* prepare to save predicates */		\
	br.cond.sptk.many dispatch_to_fault_handler

/*
 * As we don't (hopefully) use the space available, we need to fill it with
 * nops. the parameter may be used for debugging and is representing the entry
 * number
 */ 
#define BREAK_BUNDLE(a) 	break.m (a); \
				break.i (a); \
				break.i (a)
/*
 * 4 breaks bundles all together
 */
#define BREAK_BUNDLE4(a); BREAK_BUNDLE(a); BREAK_BUNDLE(a); BREAK_BUNDLE(a); BREAK_BUNDLE(a)

/*
 * 8 bundles all together (too lazy to use only 4 at a time !)
 */
#define BREAK_BUNDLE8(a); BREAK_BUNDLE4(a); BREAK_BUNDLE4(a)

	.psr abi64
	.psr lsb
	.lsb

	.section __ivt_section,"ax"

	.align 32768	// align on 32KB boundary
	.global ia64_ivt
ia64_ivt:
/////////////////////////////////////////////////////////////////////////////////////////
// 0x0000 Entry 0 (size 64 bundles) VHPT Translation (8,20,47)
	/*
	 * The VHPT vector is invoked when the TLB entry for the virtual page table
	 * is missing.  This happens only as a result of a previous
	 * (the "original") TLB miss, which may either be caused by an instruction
	 * fetch or a data access (or non-access).
	 *
	 * What we do here is normal TLB miss handing for the _original_ miss, followed
	 * by inserting the TLB entry for the virtual page table page that the VHPT
	 * walker was attempting to access.  The latter gets inserted as long
	 * as both L1 and L2 have valid mappings for the faulting address.
	 * The TLB entry for the original miss gets inserted only if
	 * the L3 entry indicates that the page is present.
	 *
	 * do_page_fault gets invoked in the following cases:
	 *	- the faulting virtual address uses unimplemented address bits
	 *	- the faulting virtual address has no L1, L2, or L3 mapping
	 */
	mov r16=cr.ifa				// get address that caused the TLB miss
	;;
	rsm psr.dt				// use physical addressing for data
	mov r31=pr				// save the predicate registers
	mov r19=ar.k7				// get page table base address
	shl r21=r16,3				// shift bit 60 into sign bit
	shr.u r17=r16,61			// get the region number into r17
	;;
	cmp.eq p6,p7=5,r17			// is IFA pointing into to region 5?
	shr.u r18=r16,PGDIR_SHIFT		// get bits 33-63 of the faulting address
	;;
(p7)	dep r17=r17,r19,(PAGE_SHIFT-3),3	// put region number bits in place
	srlz.d					// ensure "rsm psr.dt" has taken effect
(p6)	movl r19=__pa(SWAPPER_PGD_ADDR)		// region 5 is rooted at swapper_pg_dir
(p6)	shr r21=r21,PGDIR_SHIFT+PAGE_SHIFT-1
(p7)	shr r21=r21,PGDIR_SHIFT+PAGE_SHIFT-4
	;;
(p6)	dep r17=r18,r19,3,(PAGE_SHIFT-3)	// r17=PTA + IFA(33,42)*8
(p7)	dep r17=r18,r17,3,(PAGE_SHIFT-6)	// r17=PTA + (((IFA(61,63) << 7) | IFA(33,39))*8)
	cmp.eq p7,p6=0,r21			// unused address bits all zeroes?
	shr.u r18=r16,PMD_SHIFT			// shift L2 index into position
	;;
(p6)	cmp.eq p7,p6=-1,r21			// unused address bits all ones?
	ld8 r17=[r17]				// fetch the L1 entry (may be 0)
	;;
(p7)	cmp.eq p6,p7=r17,r0			// was L1 entry NULL?
	dep r17=r18,r17,3,(PAGE_SHIFT-3)	// compute address of L2 page table entry
	;;
(p7)	ld8 r17=[r17]				// fetch the L2 entry (may be 0)
	shr.u r19=r16,PAGE_SHIFT		// shift L3 index into position
	;;
(p7)	cmp.eq.or.andcm p6,p7=r17,r0		// was L2 entry NULL?
	dep r17=r19,r17,3,(PAGE_SHIFT-3)	// compute address of L3 page table entry
	;;
(p7)	ld8 r18=[r17]				// read the L3 PTE
	mov r19=cr.isr				// cr.isr bit 0 tells us if this is an insn miss
	;;
(p7)	tbit.z p6,p7=r18,0			// page present bit cleared?
	mov r21=cr.iha				// get the VHPT address that caused the TLB miss
	;;					// avoid RAW on p7
(p7)	tbit.nz.unc p10,p11=r19,32		// is it an instruction TLB miss?
	dep r17=0,r17,0,PAGE_SHIFT		// clear low bits to get page address
	;;
(p10)	itc.i r18				// insert the instruction TLB entry
(p11)	itc.d r18				// insert the data TLB entry
(p6)	br.spnt.few page_fault			// handle bad address/page not present (page fault)
	mov cr.ifa=r21

	// Now compute and insert the TLB entry for the virtual page table.
	// We never execute in a page table page so there is no need to set
	// the exception deferral bit.
	adds r16=__DIRTY_BITS_NO_ED|_PAGE_PL_0|_PAGE_AR_RW,r17
	;;
(p7)	itc.d r16
	mov pr=r31,-1				// restore predicate registers
	rfi

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x0400 Entry 1 (size 64 bundles) ITLB (21)
	/*
	 * The ITLB basically does the same as the VHPT handler except
	 * that we always insert exactly one instruction TLB entry.
	 */
	mov r16=cr.ifa				// get address that caused the TLB miss
	;;
	rsm psr.dt				// use physical addressing for data
	mov r31=pr				// save the predicate registers
	mov r19=ar.k7				// get page table base address
	shl r21=r16,3				// shift bit 60 into sign bit
	shr.u r17=r16,61			// get the region number into r17
	;;
	cmp.eq p6,p7=5,r17			// is IFA pointing into to region 5?
	shr.u r18=r16,PGDIR_SHIFT		// get bits 33-63 of the faulting address
	;;
(p7)	dep r17=r17,r19,(PAGE_SHIFT-3),3	// put region number bits in place
	srlz.d					// ensure "rsm psr.dt" has taken effect
(p6)	movl r19=__pa(SWAPPER_PGD_ADDR)		// region 5 is rooted at swapper_pg_dir
(p6)	shr r21=r21,PGDIR_SHIFT+PAGE_SHIFT-1
(p7)	shr r21=r21,PGDIR_SHIFT+PAGE_SHIFT-4
	;;
(p6)	dep r17=r18,r19,3,(PAGE_SHIFT-3)	// r17=PTA + IFA(33,42)*8
(p7)	dep r17=r18,r17,3,(PAGE_SHIFT-6)	// r17=PTA + (((IFA(61,63) << 7) | IFA(33,39))*8)
	cmp.eq p7,p6=0,r21			// unused address bits all zeroes?
	shr.u r18=r16,PMD_SHIFT			// shift L2 index into position
	;;
(p6)	cmp.eq p7,p6=-1,r21			// unused address bits all ones?
	ld8 r17=[r17]				// fetch the L1 entry (may be 0)
	;;
(p7)	cmp.eq p6,p7=r17,r0			// was L1 entry NULL?
	dep r17=r18,r17,3,(PAGE_SHIFT-3)	// compute address of L2 page table entry
	;;
(p7)	ld8 r17=[r17]				// fetch the L2 entry (may be 0)
	shr.u r19=r16,PAGE_SHIFT		// shift L3 index into position
	;;
(p7)	cmp.eq.or.andcm p6,p7=r17,r0		// was L2 entry NULL?
	dep r17=r19,r17,3,(PAGE_SHIFT-3)	// compute address of L3 page table entry
	;;
(p7)	ld8 r18=[r17]				// read the L3 PTE
	;;
(p7)	tbit.z p6,p7=r18,0			// page present bit cleared?
	;;
(p7)	itc.i r18				// insert the instruction TLB entry
(p6)	br.spnt.few page_fault			// handle bad address/page not present (page fault)
	;;
	mov pr=r31,-1				// restore predicate registers
	rfi

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x0800 Entry 2 (size 64 bundles) DTLB (9,48)
	/*
	 * The DTLB basically does the same as the VHPT handler except
	 * that we always insert exactly one data TLB entry.
	 */
	mov r16=cr.ifa				// get address that caused the TLB miss
	;;
	rsm psr.dt				// use physical addressing for data
	mov r31=pr				// save the predicate registers
	mov r19=ar.k7				// get page table base address
	shl r21=r16,3				// shift bit 60 into sign bit
	shr.u r17=r16,61			// get the region number into r17
	;;
	cmp.eq p6,p7=5,r17			// is IFA pointing into to region 5?
	shr.u r18=r16,PGDIR_SHIFT		// get bits 33-63 of the faulting address
	;;
(p7)	dep r17=r17,r19,(PAGE_SHIFT-3),3	// put region number bits in place
	srlz.d					// ensure "rsm psr.dt" has taken effect
(p6)	movl r19=__pa(SWAPPER_PGD_ADDR)		// region 5 is rooted at swapper_pg_dir
(p6)	shr r21=r21,PGDIR_SHIFT+PAGE_SHIFT-1
(p7)	shr r21=r21,PGDIR_SHIFT+PAGE_SHIFT-4
	;;
(p6)	dep r17=r18,r19,3,(PAGE_SHIFT-3)	// r17=PTA + IFA(33,42)*8
(p7)	dep r17=r18,r17,3,(PAGE_SHIFT-6)	// r17=PTA + (((IFA(61,63) << 7) | IFA(33,39))*8)
	cmp.eq p7,p6=0,r21			// unused address bits all zeroes?
	shr.u r18=r16,PMD_SHIFT			// shift L2 index into position
	;;
(p6)	cmp.eq p7,p6=-1,r21			// unused address bits all ones?
	ld8 r17=[r17]				// fetch the L1 entry (may be 0)
	;;
(p7)	cmp.eq p6,p7=r17,r0			// was L1 entry NULL?
	dep r17=r18,r17,3,(PAGE_SHIFT-3)	// compute address of L2 page table entry
	;;
(p7)	ld8 r17=[r17]				// fetch the L2 entry (may be 0)
	shr.u r19=r16,PAGE_SHIFT		// shift L3 index into position
	;;
(p7)	cmp.eq.or.andcm p6,p7=r17,r0		// was L2 entry NULL?
	dep r17=r19,r17,3,(PAGE_SHIFT-3)	// compute address of L3 page table entry
	;;
(p7)	ld8 r18=[r17]				// read the L3 PTE
	;;
(p7)	tbit.z p6,p7=r18,0			// page present bit cleared?
	;;
(p7)	itc.d r18				// insert the instruction TLB entry
(p6)	br.spnt.few page_fault			// handle bad address/page not present (page fault)
	;;
	mov pr=r31,-1				// restore predicate registers
	rfi

	//-----------------------------------------------------------------------------------
	// call do_page_fault (predicates are in r31, psr.dt is off, r16 is faulting address)
page_fault:
	SAVE_MIN_WITH_COVER
	//
	// Copy control registers to temporary registers, then turn on psr bits,
	// then copy the temporary regs to the output regs.  We have to do this
	// because the "alloc" can cause a mandatory store which could lead to
	// an "Alt DTLB" fault which we can handle only if psr.ic is on.
	//
	mov r8=cr.ifa
	mov r9=cr.isr
	adds r3=8,r2				// set up second base pointer
	;;
	ssm psr.ic | psr.dt
	;;
	srlz.i					// guarantee that interrupt collection is enabled
	;;
(p15)	ssm psr.i				// restore psr.i
	movl r14=ia64_leave_kernel
	;;
	alloc r15=ar.pfs,0,0,3,0		// must be first in insn group
	mov out0=r8
	mov out1=r9
	;;
	SAVE_REST
	mov rp=r14
	;;
	adds out2=16,r12			// out2 = pointer to pt_regs
	br.call.sptk.few b6=ia64_do_page_fault	// ignore return address

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x0c00 Entry 3 (size 64 bundles) Alt ITLB (19)
	mov r16=cr.ifa		// get address that caused the TLB miss
	movl r17=__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RX
	;;
	shr.u r18=r16,57	// move address bit 61 to bit 4
	dep r16=0,r16,IA64_PHYS_BITS,(64-IA64_PHYS_BITS) // clear ed, resvd, and unimpl. phys bits
	;;
	andcm r18=0x10,r18	// bit 4=~address-bit(61)
	dep r16=r17,r16,0,12	// insert PTE control bits into r16
	;;
	or r16=r16,r18		// set bit 4 (uncached) if the access was to region 6
	;;
	itc.i r16		// insert the TLB entry
	rfi

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x1000 Entry 4 (size 64 bundles) Alt DTLB (7,46)
	mov r16=cr.ifa		// get address that caused the TLB miss
	movl r17=__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RW
	;;
	shr.u r18=r16,57	// move address bit 61 to bit 4
	dep r16=0,r16,IA64_PHYS_BITS,(64-IA64_PHYS_BITS) // clear ed, resvd, and unimpl. phys bits
	;;
	andcm r18=0x10,r18	// bit 4=~address-bit(61)
	dep r16=r17,r16,0,12	// insert PTE control bits into r16
	;;
	or r16=r16,r18		// set bit 4 (uncached) if the access was to region 6
	;;
	itc.d r16		// insert the TLB entry
	rfi

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x1400 Entry 5 (size 64 bundles) Data nested TLB (6,45)
	//
	// In the absence of kernel bugs, we get here when the Dirty-bit, Instruction
	// Access-bit, or Data Access-bit faults cause a nested fault because the
	// dTLB entry for the virtual page table isn't present.  In such a case,
	// we lookup the pte for the faulting address by walking the page table
	// and return to the contination point passed in register r30.
	// In accessing the page tables, we don't need to check for NULL entries
	// because if the page tables didn't map the faulting address, it would not
	// be possible to receive one of the above faults.
	//
	// Input:	r16:	faulting address
	//		r29:	saved b0
	//		r30:	continuation address
	//
	// Output:	r17:	physical address of L3 PTE of faulting address
	//		r29:	saved b0
	//		r30:	continuation address
	//
	// Clobbered:	b0, r18, r19, r21, r31, psr.dt (cleared)
	//
	rsm psr.dt				// switch to using physical data addressing
	mov r19=ar.k7				// get the page table base address
	shl r21=r16,3				// shift bit 60 into sign bit
	;;
	mov r31=pr				// save the predicate registers
	shr.u r17=r16,61			// get the region number into r17
	;;
	cmp.eq p6,p7=5,r17			// is faulting address in region 5?
	shr.u r18=r16,PGDIR_SHIFT		// get bits 33-63 of faulting address
	;;
(p7)	dep r17=r17,r19,(PAGE_SHIFT-3),3	// put region number bits in place
	srlz.d
(p6)	movl r17=__pa(SWAPPER_PGD_ADDR)		// region 5 is rooted at swapper_pg_dir
(p6)	shr r21=r21,PGDIR_SHIFT+PAGE_SHIFT-1
(p7)	shr r21=r21,PGDIR_SHIFT+PAGE_SHIFT-4
	;;
(p6)	dep r17=r18,r17,3,(PAGE_SHIFT-3)	// r17=PTA + IFA(33,42)*8
(p7)	dep r17=r18,r17,3,(PAGE_SHIFT-6)	// r17=PTA + (((IFA(61,63) << 7) | IFA(33,39))*8)
	shr.u r18=r16,PMD_SHIFT			// shift L2 index into position
	;;
	ld8 r17=[r17]				// fetch the L1 entry
	mov b0=r30
	;;
	dep r17=r18,r17,3,(PAGE_SHIFT-3)	// compute address of L2 page table entry
	;;
	ld8 r17=[r17]				// fetch the L2 entry
	shr.u r19=r16,PAGE_SHIFT		// shift L3 index into position
	;;
	dep r17=r19,r17,3,(PAGE_SHIFT-3)	// compute address of L3 page table entry
	;;
	mov pr=r31,-1				// restore predicates
	br.cond.sptk.few b0			// return to continuation point

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x1800 Entry 6 (size 64 bundles) Instruction Key Miss (24)
	FAULT(6)

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x1c00 Entry 7 (size 64 bundles) Data Key Miss (12,51)
	FAULT(7)

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x2000 Entry 8 (size 64 bundles) Dirty-bit (54)
	//
	// What we do here is to simply turn on the dirty bit in the PTE.  We need
	// to update both the page-table and the TLB entry.  To efficiently access
	// the PTE, we address it through the virtual page table.  Most likely, the
	// TLB entry for the relevant virtual page table page is still present in
	// the TLB so we can normally do this without additional TLB misses.
	// In case the necessary virtual page table TLB entry isn't present, we take
	// a nested TLB miss hit where we look up the physical address of the L3 PTE
	// and then continue at label 1 below.
	//
	mov r16=cr.ifa				// get the address that caused the fault
	movl r30=1f				// load continuation point in case of nested fault
	;;
	thash r17=r16				// compute virtual address of L3 PTE
	mov r29=b0				// save b0 in case of nested fault
	;;
1:	ld8 r18=[r17]
	;;					// avoid RAW on r18
	or r18=_PAGE_D,r18			// set the dirty bit
	mov b0=r29				// restore b0
	;;
	st8 [r17]=r18				// store back updated PTE
	itc.d r18				// install updated PTE
	rfi

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x2400 Entry 9 (size 64 bundles) Instruction Access-bit (27)
	// Like Entry 8, except for instruction access
	mov r16=cr.ifa				// get the address that caused the fault
#ifdef CONFIG_ITANIUM
	/*
	 * Erratum 10 (IFA may contain incorrect address) now has
	 * "NoFix" status.  There are no plans for fixing this.
	 */
	mov r17=cr.ipsr
	mov r31=pr				// save predicates
	;;
	mov r18=cr.iip
	tbit.z p6,p0=r17,IA64_PSR_IS_BIT	// IA64 instruction set?
	;;
(p6)	mov r16=r18				// if so, use cr.iip instead of cr.ifa
#if 0
	;;
#endif
	mov pr=r31,-1
#endif /* CONFIG_ITANIUM */
	movl r30=1f				// load continuation point in case of nested fault
	;;
	thash r17=r16				// compute virtual address of L3 PTE
	mov r29=b0				// save b0 in case of nested fault)
	;;
1:	ld8 r18=[r17]
	;;					// avoid raw on r18
	or r18=_PAGE_A,r18			// set the accessed bit
	mov b0=r29				// restore b0
	;;
	st8 [r17]=r18				// store back updated PTE
	itc.i r18				// install updated PTE
	rfi

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x2800 Entry 10 (size 64 bundles) Data Access-bit (15,55)
	// Like Entry 8, except for data access
	mov r16=cr.ifa				// get the address that caused the fault
	movl r30=1f				// load continuation point in case of nested fault
	;;
	thash r17=r16				// compute virtual address of L3 PTE
	mov r29=b0				// save b0 in case of nested fault)
	;;
1:	ld8 r18=[r17]
	;;					// avoid RAW on r18
	or r18=_PAGE_A,r18			// set the accessed bit
	mov b0=r29				// restore b0
	;;
	st8 [r17]=r18				// store back updated PTE
	itc.d r18				// install updated PTE
	rfi

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x2c00 Entry 11 (size 64 bundles) Break instruction (33)
	mov r16=cr.iim
	mov r17=__IA64_BREAK_SYSCALL
	mov r31=pr		// prepare to save predicates
	rsm psr.dt		// avoid nested faults due to TLB misses...
	;;
	srlz.d			// ensure everyone knows psr.dt is off...
	cmp.eq p0,p7=r16,r17	// is this a system call? (p7 <- false, if so)

#if 1
	// Allow syscalls via the old system call number for the time being.  This is
	// so we can transition to the new syscall number in a relatively smooth
	// fashion.
	mov r17=0x80000
	;;
(p7)	cmp.eq.or.andcm p0,p7=r16,r17		// is this the old syscall number?
#endif

(p7)	br.cond.spnt.many non_syscall

	SAVE_MIN				// uses r31; defines r2:

	// turn interrupt collection and data translation back on:
	ssm psr.ic | psr.dt
	;;
	srlz.i					// guarantee that interrupt collection is enabled
	cmp.eq pSys,pNonSys=r0,r0		// set pSys=1, pNonSys=0
	;;
(p15)	ssm psr.i		// restore psr.i
	adds r8=(IA64_PT_REGS_R8_OFFSET-IA64_PT_REGS_R16_OFFSET),r2
	;;
	stf8 [r8]=f1		// ensure pt_regs.r8 != 0 (see handle_syscall_error)
	adds r3=8,r2		// set up second base pointer for SAVE_REST
	;;
	SAVE_REST
	;;			// avoid WAW on r2 & r3

	mov r3=255
	adds r15=-1024,r15			// r15 contains the syscall number---subtract 1024
	adds r2=IA64_TASK_FLAGS_OFFSET,r13	// r2 = &current->flags

	;;
	cmp.geu.unc p6,p7=r3,r15		// (syscall > 0 && syscall <= 1024+255) ?
	movl r16=sys_call_table
	;;
(p6)	shladd r16=r15,3,r16
	movl r15=ia64_ret_from_syscall
(p7)	adds r16=(__NR_ni_syscall-1024)*8,r16	// force __NR_ni_syscall
	;;
	ld8 r16=[r16]				// load address of syscall entry point
	mov rp=r15				// set the real return addr
	;;
	ld8 r2=[r2]				// r2 = current->flags
	mov b6=r16

	// arrange things so we skip over break instruction when returning:

	adds r16=16,sp				// get pointer to cr_ipsr
	adds r17=24,sp				// get pointer to cr_iip
	;;
	ld8 r18=[r16]				// fetch cr_ipsr
	tbit.z p8,p0=r2,5			// (current->flags & PF_TRACESYS) == 0?
	;;
	ld8 r19=[r17]				// fetch cr_iip
	extr.u r20=r18,41,2			// extract ei field
	;;
	cmp.eq p6,p7=2,r20			// isr.ei==2?
	adds r19=16,r19				// compute address of next bundle
	;;
(p6)	mov r20=0				// clear ei to 0
(p7)	adds r20=1,r20				// increment ei to next slot
	;;
(p6)	st8 [r17]=r19				// store new cr.iip if cr.isr.ei wrapped around
	dep r18=r20,r18,41,2			// insert new ei into cr.isr
	;;
	st8 [r16]=r18				// store new value for cr.isr

(p8)	br.call.sptk.few b6=b6			// ignore this return addr 
	br.call.sptk.few rp=ia64_trace_syscall	// rp will be overwritten (ignored)
	// NOT REACHED

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x3000 Entry 12 (size 64 bundles) External Interrupt (4)
	rsm psr.dt		// avoid nested faults due to TLB misses...
	;;
	srlz.d			// ensure everyone knows psr.dt is off...
	mov r31=pr		// prepare to save predicates
	;;

	SAVE_MIN_WITH_COVER	// uses r31; defines r2 and r3
	ssm psr.ic | psr.dt	// turn interrupt collection and data translation back on
	;;
	adds r3=8,r2		// set up second base pointer for SAVE_REST
	cmp.eq pEOI,p0=r0,r0	// set pEOI flag so that ia64_leave_kernel writes cr.eoi
	srlz.i			// ensure everybody knows psr.ic and psr.dt are back on
	;;
	SAVE_REST
	;;
	alloc r14=ar.pfs,0,0,2,0 // must be first in an insn group
#ifdef	CONFIG_ITANIUM_ASTEP_SPECIFIC
	mov out0=r0		// defer reading of cr.ivr to handle_irq...
#else
	mov out0=cr.ivr		// pass cr.ivr as first arg
#endif
	add out1=16,sp		// pass pointer to pt_regs as second arg
	;;
	srlz.d			// make  sure we see the effect of cr.ivr
	movl r14=ia64_leave_kernel
	;;
	mov rp=r14
	br.call.sptk.few b6=ia64_handle_irq

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x3400 Entry 13 (size 64 bundles) Reserved
	FAULT(13)

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x3800 Entry 14 (size 64 bundles) Reserved
	FAULT(14)

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x3c00 Entry 15 (size 64 bundles) Reserved
	FAULT(15)

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x4000 Entry 16 (size 64 bundles) Reserved
	FAULT(16)

#ifdef CONFIG_IA32_SUPPORT

	// There is no particular reason for this code to be here, other than that
	// there happens to be space here that would go unused otherwise.  If this
	// fault ever gets "unreserved", simply moved the following code to a more
	// suitable spot...

	// IA32 interrupt entry point

dispatch_to_ia32_handler:
	SAVE_MIN
	;;
	mov r14=cr.isr
	ssm psr.ic | psr.dt
	;;
	srlz.i					// guarantee that interrupt collection is enabled
	;;
(p15)	ssm psr.i
	adds r3=8,r2            // Base pointer for SAVE_REST
	;;
	SAVE_REST
	;;
	mov r15=0x80
	shr r14=r14,16          // Get interrupt number
	;; 
	cmp.ne p6,p0=r14,r15
(p6)    br.call.dpnt.few b6=non_ia32_syscall

	adds r14=IA64_PT_REGS_R8_OFFSET + 16,sp	// 16 byte hole per SW conventions

	;;
	alloc r15=ar.pfs,0,0,6,0	// must first in an insn group
	;; 
	ld4 r8=[r14],8          // r8 == EAX (syscall number)
	mov r15=0xff
	;;
	cmp.ltu.unc p6,p7=r8,r15
	ld4 out1=[r14],8        // r9 == ecx
	;;
	ld4 out2=[r14],8         // r10 == edx
	;;
	ld4 out0=[r14]           // r11 == ebx
	adds r14=(IA64_PT_REGS_R8_OFFSET-(8*3)) + 16,sp
	;;
	ld4 out5=[r14],8         // r13 == ebp
	;;
	ld4 out3=[r14],8         // r14 == esi
	adds r2=IA64_TASK_FLAGS_OFFSET,r13	// r2 = &current->flags
	;;
	ld4 out4=[r14]           // R15 == edi
	movl r16=ia32_syscall_table
	;; 
(p6)    shladd r16=r8,3,r16     // Force ni_syscall if not valid syscall number
	ld8 r2=[r2]		// r2 = current->flags
	;; 
	ld8 r16=[r16]
	tbit.z p8,p0=r2,5	// (current->flags & PF_TRACESYS) == 0?
	;;
	mov b6=r16
	movl r15=ia32_ret_from_syscall
	;;
	mov rp=r15
(p8)	br.call.sptk.many b6=b6
	;;
	br.call.sptk.many rp=ia32_trace_syscall	// rp will be overwritten (ignored)

non_ia32_syscall:       
	alloc r15=ar.pfs,0,0,2,0
	mov out0=r14                            // interrupt #
	add out1=16,sp                          // pointer to pt_regs
	;;			// avoid WAW on CFM
	br.call.sptk.few rp=ia32_bad_interrupt
	;; 
	movl r15=ia64_leave_kernel
	;;
	mov rp=r15
	br.ret.sptk.many rp

#endif /* CONFIG_IA32_SUPPORT */

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x4400 Entry 17 (size 64 bundles) Reserved
	FAULT(17)

non_syscall:
	SAVE_MIN_WITH_COVER

	// There is no particular reason for this code to be here, other than that
	// there happens to be space here that would go unused otherwise.  If this
	// fault ever gets "unreserved", simply moved the following code to a more
	// suitable spot...

	mov r8=cr.iim			// get break immediate (must be done while psr.ic is off)
	adds r3=8,r2			// set up second base pointer for SAVE_REST

	// turn interrupt collection and data translation back on:
	ssm psr.ic | psr.dt
	;;
	srlz.i				// guarantee that interrupt collection is enabled
	;;
(p15)	ssm psr.i			// restore psr.i
	movl r15=ia64_leave_kernel
	;;
	alloc r14=ar.pfs,0,0,2,0
	mov out0=r8			// break number
	add out1=16,sp			// pointer to pt_regs
	;;
	SAVE_REST
	mov rp=r15
	;;
	br.call.sptk.few b6=ia64_bad_break	// avoid WAW on CFM and ignore return addr

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x4800 Entry 18 (size 64 bundles) Reserved
	FAULT(18)

	// There is no particular reason for this code to be here, other than that
	// there happens to be space here that would go unused otherwise.  If this
	// fault ever gets "unreserved", simply moved the following code to a more
	// suitable spot...

dispatch_unaligned_handler:
	SAVE_MIN_WITH_COVER
	;;
	//
	// we can't have the alloc while psr.ic is cleared because 
	// we might get a mandatory RSE (when you reach the end of the 
	// rotating partition when doing the alloc) spill which could cause 
	// a page fault on the kernel virtual address and the handler 
	// wouldn't get the state to recover.
	//
	mov r15=cr.ifa
	ssm psr.ic | psr.dt
	;;
	srlz.i					// guarantee that interrupt collection is enabled
	;;
(p15)	ssm psr.i				// restore psr.i
	adds r3=8,r2				// set up second base pointer
	;;
	SAVE_REST
	;;
	alloc r14=ar.pfs,0,0,2,0		// now it's safe (must be first in insn group!)
	;;					// avoid WAW on r14
	movl r14=ia64_leave_kernel
	mov out0=r15				// out0 = faulting address
	adds out1=16,sp				// out1 = pointer to pt_regs
	;;
	mov rp=r14
	br.sptk.few ia64_prepare_handle_unaligned

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x4c00 Entry 19 (size 64 bundles) Reserved
	FAULT(19)

	// There is no particular reason for this code to be here, other than that
	// there happens to be space here that would go unused otherwise.  If this
	// fault ever gets "unreserved", simply moved the following code to a more
	// suitable spot...

dispatch_to_fault_handler:
	//
	// Input:
	//	psr.ic:	off
	//	psr.dt:	off
	//	r19:	fault vector number (e.g., 24 for General Exception)
	//	r31:	contains saved predicates (pr)
	//
	SAVE_MIN_WITH_COVER_R19
	//
	// Copy control registers to temporary registers, then turn on psr bits,
	// then copy the temporary regs to the output regs.  We have to do this
	// because the "alloc" can cause a mandatory store which could lead to
	// an "Alt DTLB" fault which we can handle only if psr.ic is on.
	//
	mov r8=cr.isr
	mov r9=cr.ifa
	mov r10=cr.iim
	mov r11=cr.itir
	;;
	ssm psr.ic | psr.dt
	;;
	srlz.i					// guarantee that interrupt collection is enabled
	;;
(p15)	ssm psr.i				// restore psr.i
	adds r3=8,r2				// set up second base pointer for SAVE_REST
	;;
	alloc r14=ar.pfs,0,0,5,0		// must be first in insn group
	mov out0=r15
	mov out1=r8
	mov out2=r9
	mov out3=r10
	mov out4=r11
	;;
	SAVE_REST
	movl r14=ia64_leave_kernel
	;;
	mov rp=r14
	br.call.sptk.few b6=ia64_fault
//
// --- End of long entries, Beginning of short entries
//

	.align 1024
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5000 Entry 20 (size 16 bundles) Page Not Present (10,22,49)
	mov r16=cr.ifa
	rsm psr.dt
#if 1
	// If you disable this, you MUST re-enable to update_mmu_cache() code in pgtable.h
	mov r17=_PAGE_SIZE_4K<<2
	;;
	ptc.l r16,r17
#endif
	;;
	mov r31=pr
	srlz.d
	br.cond.sptk.many page_fault

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5100 Entry 21 (size 16 bundles) Key Permission (13,25,52)
	mov r16=cr.ifa
	rsm psr.dt
	mov r31=pr
	;;
	srlz.d
	br.cond.sptk.many page_fault

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5200 Entry 22 (size 16 bundles) Instruction Access Rights (26)
	mov r16=cr.ifa
	rsm psr.dt
	mov r31=pr
	;;
	srlz.d
	br.cond.sptk.many page_fault

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5300 Entry 23 (size 16 bundles) Data Access Rights (14,53)
	mov r16=cr.ifa
	rsm psr.dt
	mov r31=pr
	;;
	srlz.d
	br.cond.sptk.many page_fault

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5400 Entry 24 (size 16 bundles) General Exception (5,32,34,36,38,39)
	FAULT(24)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5500 Entry 25 (size 16 bundles) Disabled FP-Register (35)
	rsm psr.dt | psr.dfh			// ensure we can access fph
	;;
	srlz.d
	mov r31=pr
	mov r19=25
	br.cond.sptk.many dispatch_to_fault_handler

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5600 Entry 26 (size 16 bundles) Nat Consumption (11,23,37,50)
	FAULT(26)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5700 Entry 27 (size 16 bundles) Speculation (40)
	//
	// A [f]chk.[as] instruction needs to take the branch to
	// the recovery code but this part of the architecture is
	// not implemented in hardware on some CPUs, such as Itanium.
	// Thus, in general we need to emulate the behavior.
	// IIM contains the relative target (not yet sign extended).
	// So after sign extending it we simply add it to IIP.
	// We also need to reset the EI field of the IPSR to zero,
	// i.e., the slot to restart into.
	//
	// cr.imm contains zero_ext(imm21)
	//
	mov r18=cr.iim
	;;
	mov r17=cr.iip
	shl r18=r18,43			// put sign bit in position (43=64-21)
	;;

	mov r16=cr.ipsr
	shr r18=r18,39			// sign extend (39=43-4)
	;;

	add r17=r17,r18			// now add the offset
	;;
	mov cr.iip=r17
	dep r16=0,r16,41,2		// clear EI
	;;

	mov cr.ipsr=r16
	;;

	rfi				// and go back

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5800 Entry 28 (size 16 bundles) Reserved
	FAULT(28)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5900 Entry 29 (size 16 bundles) Debug (16,28,56)
	FAULT(29)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5a00 Entry 30 (size 16 bundles) Unaligned Reference (57)
	rsm psr.dt		// avoid nested faults due to TLB misses...
	mov r16=cr.ipsr
	mov r31=pr		// prepare to save predicates
	;;									
	srlz.d			// ensure everyone knows psr.dt is off
	mov r19=30		// error vector for fault_handler (when kernel)
	br.cond.sptk.many dispatch_unaligned_handler

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5b00 Entry 31 (size 16 bundles) Unsupported Data Reference (57)
	FAULT(31)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5c00 Entry 32 (size 16 bundles) Floating-Point Fault (64)
	FAULT(32)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5d00 Entry 33 (size 16 bundles) Floating Point Trap (66)
	FAULT(33)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5e00 Entry 34 (size 16 bundles) Lower Privilege Tranfer Trap (66)
	FAULT(34)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5f00 Entry 35 (size 16 bundles) Taken Branch Trap (68)
	FAULT(35)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6000 Entry 36 (size 16 bundles) Single Step Trap (69)
	FAULT(36)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6100 Entry 37 (size 16 bundles) Reserved
	FAULT(37)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6200 Entry 38 (size 16 bundles) Reserved
	FAULT(38)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6300 Entry 39 (size 16 bundles) Reserved
	FAULT(39)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6400 Entry 40 (size 16 bundles) Reserved
	FAULT(40)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6500 Entry 41 (size 16 bundles) Reserved
	FAULT(41)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6600 Entry 42 (size 16 bundles) Reserved
	FAULT(42)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6700 Entry 43 (size 16 bundles) Reserved
	FAULT(43)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6800 Entry 44 (size 16 bundles) Reserved
	FAULT(44)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6900 Entry 45 (size 16 bundles) IA-32 Exeception (17,18,29,41,42,43,44,58,60,61,62,72,73,75,76,77)
	FAULT(45)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6a00 Entry 46 (size 16 bundles) IA-32 Intercept  (30,31,59,70,71)
#ifdef	CONFIG_IA32_SUPPORT
	rsm psr.dt
	;;
	srlz.d
	mov r31=pr
	mov r16=cr.isr
	;;
	extr.u r17=r16,16,8	// get ISR.code
	mov r18=ar.eflag
	mov r19=cr.iim		// old eflag value
	;;
	cmp.ne p2,p0=2,r17
(p2)	br.cond.spnt 1f		// not a system flag fault
	xor r16=r18,r19
	;;
	extr.u r17=r16,18,1	// get the eflags.ac bit
	;;
	cmp.eq p2,p0=0,r17
(p2)	br.cond.spnt 1f		// eflags.ac bit didn't change
	;;
	mov pr=r31,-1		// restore predicate registers
	rfi

1:
#endif	// CONFIG_IA32_SUPPORT
	FAULT(46)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6b00 Entry 47 (size 16 bundles) IA-32 Interrupt  (74)
#ifdef CONFIG_IA32_SUPPORT
	rsm psr.dt
	;;
	srlz.d
	mov r31=pr
	br.cond.sptk.many dispatch_to_ia32_handler
#else
	FAULT(47)
#endif

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6c00 Entry 48 (size 16 bundles) Reserved
	FAULT(48)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6d00 Entry 49 (size 16 bundles) Reserved
	FAULT(49)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6e00 Entry 50 (size 16 bundles) Reserved
	FAULT(50)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6f00 Entry 51 (size 16 bundles) Reserved
	FAULT(51)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7000 Entry 52 (size 16 bundles) Reserved
	FAULT(52)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7100 Entry 53 (size 16 bundles) Reserved
	FAULT(53)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7200 Entry 54 (size 16 bundles) Reserved
	FAULT(54)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7300 Entry 55 (size 16 bundles) Reserved
	FAULT(55)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7400 Entry 56 (size 16 bundles) Reserved
	FAULT(56)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7500 Entry 57 (size 16 bundles) Reserved
	FAULT(57)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7600 Entry 58 (size 16 bundles) Reserved
	FAULT(58)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7700 Entry 59 (size 16 bundles) Reserved
	FAULT(59)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7800 Entry 60 (size 16 bundles) Reserved
	FAULT(60)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7900 Entry 61 (size 16 bundles) Reserved
	FAULT(61)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7a00 Entry 62 (size 16 bundles) Reserved
	FAULT(62)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7b00 Entry 63 (size 16 bundles) Reserved
	FAULT(63)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7c00 Entry 64 (size 16 bundles) Reserved
	FAULT(64)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7d00 Entry 65 (size 16 bundles) Reserved
	FAULT(65)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7e00 Entry 66 (size 16 bundles) Reserved
	FAULT(66)

	.align 256
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7f00 Entry 67 (size 16 bundles) Reserved
	FAULT(67)

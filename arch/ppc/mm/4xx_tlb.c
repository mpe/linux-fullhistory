/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: 4xx_tlb.c
 *
 *    Description:
 *      Routines for manipulating the TLB on PowerPC 400-class processors.
 *
 */

#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/system.h>


/* Preprocessor Defines */

#if !defined(TRUE) || TRUE != 1
#define TRUE    1
#endif

#if !defined(FALSE) || FALSE != 0
#define FALSE   0
#endif


/* Function Macros */


/* Type Definitios */

typedef struct pin_entry_s {
	unsigned int	e_pinned: 1,	/* This TLB entry is pinned down. */
			e_used: 23;	/* Number of users for this mapping. */
} pin_entry_t;


/* Global Variables */

static pin_entry_t pin_table[PPC4XX_TLB_SIZE];


/* Function Prototypes */


void
PPC4xx_tlb_pin(unsigned long va, unsigned long pa, int pagesz, int cache)
{
	int i, found = FALSE;
	unsigned long tag, data;
	unsigned long opid;

	opid = mfspr(SPRN_PID);
	mtspr(SPRN_PID, 0);

	data = (pa & TLB_RPN_MASK) | TLB_WR;

	if (cache)
		data |= (TLB_EX | TLB_I);
	else
		data |= (TLB_G | TLB_I);

	tag = (va & TLB_EPN_MASK) | TLB_VALID | pagesz;

	for (i = 0; i < PPC4XX_TLB_SIZE; i++) {
		if (pin_table[i].e_pinned == FALSE) {
			found = TRUE;
			break;
		}
	}

	if (found) {
		/* printk("Pinning %#x -> %#x in entry %d...\n", va, pa, i); */
		asm("tlbwe %0,%1,1" : : "r" (data), "r" (i));
		asm("tlbwe %0,%1,0" : : "r" (tag), "r" (i));
		asm("isync");
		pin_table[i].e_pinned = found;
	}

	mtspr(SPRN_PID, opid);
	return;
}

void
PPC4xx_tlb_unpin(unsigned long va, unsigned long pa, int size)
{
	/* XXX - To beimplemented. */
}

void
PPC4xx_tlb_flush_all(void)
{
	int i;
	unsigned long flags, opid;

	save_flags(flags);
	cli();

	opid = mfspr(SPRN_PID);
	mtspr(SPRN_PID, 0);

	for (i = 0; i < PPC4XX_TLB_SIZE; i++) {
		unsigned long ov = 0;

		if (pin_table[i].e_pinned)
			continue;

		asm("tlbwe %0,%1,0" : : "r" (ov), "r" (i));
		asm("tlbwe %0,%1,1" : : "r" (ov), "r" (i));
	}

	asm("sync;isync");

	mtspr(SPRN_PID, opid);
	restore_flags(flags);
}

void
PPC4xx_tlb_flush(unsigned long va, int pid)
{
	unsigned long i, tag, flags, found = 1, opid;

	save_flags(flags);
	cli();

	opid = mfspr(SPRN_PID);
	mtspr(SPRN_PID, pid);

	asm("tlbsx. %0,0,%2;beq 1f;li %1,0;1:" : "=r" (i), "=r" (found) : "r" (va));

	if (found && pin_table[i].e_pinned == 0) {
		asm("tlbre %0,%1,0" : "=r" (tag) : "r" (i));
		tag &= ~ TLB_VALID;
		asm("tlbwe %0,%1,0" : : "r" (tag), "r" (i));
	}

	mtspr(SPRN_PID, opid);

	restore_flags(flags);
}

#if 0
/*
 * TLB miss handling code.
 */

/*
 * Handle TLB faults. We should push this back to assembly code eventually.
 * Caller is responsible for turning off interrupts ...
 */
static inline void
tlbDropin(unsigned long tlbhi, unsigned long tlblo) {
	/*
	 * Avoid the divide at the slight cost of a little too
	 * much emphasis on the last few entries.
	 */
	unsigned long rand = mfspr(SPRN_TBLO);
	rand &= 0x3f;
        rand += NTLB_WIRED;
        if (rand >= NTLB)
		rand -= NTLB_WIRED;

	asm("tlbwe %0,%1,1" : : "r" (tlblo), "r" (rand));
	asm("tlbwe %0,%1,0" : : "r" (tlbhi), "r" (rand));
	asm("isync;sync");
}

static inline void
mkTlbEntry(unsigned long addr, pte_t *pte) {
	unsigned long tlbhi;
	unsigned long tlblo;
	int found = 1;
	int idx;

	/*
	 * Construct the TLB entry.
	 */
	tlbhi = addr & ~(PAGE_SIZE-1);
	tlblo = virt_to_phys(pte_page(*pte)) & TLBLO_RPN;
	if (pte_val(*pte) & _PAGE_HWWRITE)
		tlblo |= TLBLO_WR;
	if (pte_val(*pte) & _PAGE_NO_CACHE)
		tlblo |= TLBLO_I;
	tlblo |= TLBLO_EX;
	if (addr < KERNELBASE)
		tlblo |= TLBLO_Z_USER;
	tlbhi |= TLBHI_PGSZ_4K;
	tlbhi |= TLBHI_VALID;

	/*
	 * See if a match already exists in the TLB.
	 */
	asm("tlbsx. %0,0,%2;beq 1f;li %1,0;1:" : "=r" (idx), "=r" (found) : "r" (tlbhi));
	if (found) {
		/*
		 * Found an existing entry. Just reuse the index.
		 */
		asm("tlbwe %0,%1,0" : : "r" (tlbhi), "r" (idx));
		asm("tlbwe %0,%1,1" : : "r" (tlblo), "r" (idx));
	}
	else {
		/*
		 * Do the more expensive operation
		 */
		tlbDropin(tlbhi, tlblo);
	}
}

/*
 * Mainline of the TLB miss handler. The above inline routines should fold into
 * this one, eliminating most function call overhead.
 */
#ifdef TLBMISS_DEBUG
volatile unsigned long miss_start;
volatile unsigned long miss_end;
#endif

static inline int tlbMiss(struct pt_regs *regs, unsigned long badaddr, int wasWrite)
{
	int spid, ospid;
	struct mm_struct *mm;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	if (!user_mode(regs) && (badaddr >= KERNELBASE)) {
		mm = task[0]->mm;
		spid = 0;
#ifdef TLBMISS_DEBUG
                miss_start = 0;
#endif
	}
	else {
		mm = current->mm;
		spid = mfspr(SPRN_PID);
#ifdef TLBMISS_DEBUG
                miss_start = 1;
#endif
	}
#ifdef TLBMISS_DEBUG
        store_cache_range((unsigned long)&miss_start, sizeof(miss_start));
#endif

	pgd = pgd_offset(mm, badaddr);
	if (pgd_none(*pgd))
		goto NOGOOD;

	pmd = pmd_offset(pgd, badaddr);
	if (pmd_none(*pmd))
		goto NOGOOD;

	pte = pte_offset(pmd, badaddr);
	if (pte_none(*pte))
		goto NOGOOD;
	if (!pte_present(*pte))
		goto NOGOOD;
#if 1
        prohibit_if_guarded(badaddr, sizeof(int));
#endif
	if (wasWrite) {
		if (!pte_write(*pte)) {
			goto NOGOOD;
		}
		set_pte(pte, pte_mkdirty(*pte));
	}
	set_pte(pte, pte_mkyoung(*pte));

	ospid = mfspr(SPRN_PID);
	mtspr(SPRN_PID, spid);
	mkTlbEntry(badaddr, pte);
	mtspr(SPRN_PID, ospid);

#ifdef TLBMISS_DEBUG
        miss_end = 0;
        store_cache_range((unsigned long)&miss_end, sizeof(miss_end));
#endif
	return 0;

NOGOOD:
#ifdef TLBMISS_DEBUG
        miss_end = 1;
        store_cache_range((unsigned long)&miss_end, sizeof(miss_end));
#endif
        return 1;
}

/*
 * End TLB miss handling code.
 */
/* ---------- */

/*
 * Used to flush the TLB if the page fault handler decides to change
 * something.
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t pte) {
	int spid;
	unsigned long flags;

	save_flags(flags);
	cli();

	if (addr >= KERNELBASE)
		spid = 0;
	else
		spid = vma->vm_mm->context;
	tlbFlush1(addr, spid);

	restore_flags(flags);
}

/*
 * Given a virtual address in the current address space, make
 * sure the associated physical page is present in memory,
 * and if the data is to be modified, that any copy-on-write
 * actions have taken place.
 */
unsigned long make_page_present(unsigned long p, int rw) {
        pte_t *pte;
        char c;

        get_user(c, (char *) p);

        pte = findPTE(current->mm, p);
        if (pte_none(*pte) || !pte_present(*pte))
            debug("make_page_present didn't load page", 0);

        if (rw) {
            /*
            * You have to write-touch the page, so that
            * zero-filled pages are forced to be copied
            * rather than still pointing at the zero
            * page.
            */
            extern void tlbFlush1(unsigned long, int);
            tlbFlush1(p, get_context());
            put_user(c, (char *) p);
            if (!pte_write(*pte))
                debug("make_page_present didn't make page writable", 0);

            tlbFlush1(p, get_context());
        }
        return pte_page(*pte);
}

void DataTLBMissException(struct pt_regs *regs)
{
	unsigned long badaddr = mfspr(SPRN_DEAR);
	int wasWrite = mfspr(SPRN_ESR) & 0x800000;
	if (tlbMiss(regs, badaddr, wasWrite)) {
		sti();
		do_page_fault(regs, badaddr, wasWrite);
		cli();
    	}
}

void InstructionTLBMissException(struct pt_regs *regs)
{
	if (!current) {
		debug("ITLB Miss with no current task", regs);
		sti();
		bad_page_fault(regs, regs->nip);
		cli();
		return;
	}
	if (tlbMiss(regs, regs->nip, 0)) {
		sti();
		do_page_fault(regs, regs->nip, 0);
		cli();
    	}
}

void DataPageFault(struct pt_regs *regs)
{
	unsigned long badaddr = mfspr(SPRN_DEAR);
	int wasWrite = mfspr(SPRN_ESR) & 0x800000;
	sti();
	do_page_fault(regs, badaddr, wasWrite);
	cli();
}

void InstructionPageFault(struct pt_regs *regs)
{
	if (!current) {
		debug("ITLB fault with no current task", regs);
		sti();
		bad_page_fault(regs, regs->nip);
		cli();
		return;
	}
	sti();
	do_page_fault(regs, regs->nip, 0);
	cli();
}
#endif

/* $Id: sun4c.c,v 1.56 1995/11/25 00:59:39 davem Exp $
 * sun4c.c:  Sun4C specific mm routines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* The SUN4C has an MMU based upon a Translation Lookaside Buffer scheme
 * where only so many translations can be loaded at once.  As Linus said
 * in Boston, this is a broken way of doing things.
 */

#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/vac-ops.h>
#include <asm/vaddrs.h>
#include <asm/asi.h>
#include <asm/system.h>
#include <asm/contregs.h>
#include <asm/oplib.h>
#include <asm/idprom.h>
#include <asm/machines.h>
#include <asm/memreg.h>
#include <asm/kdebug.h>

/* Pseg allocation structures. */
static struct pseg_list s4cpseg_pool[256];

struct pseg_list s4cpseg_free;
struct pseg_list s4cpseg_used;
static struct pseg_list s4cpseg_locked;
static struct pseg_list s4cpseg_per_context[16];

static unsigned char pseg_count_per_context[16];

unsigned int sun4c_pmd_align(unsigned int addr) { return SUN4C_PMD_ALIGN(addr); }
unsigned int sun4c_pgdir_align(unsigned int addr) { return SUN4C_PGDIR_ALIGN(addr); }

extern int num_segmaps, num_contexts;

/* First the functions which the mid-level code uses to directly
 * manipulate the software page tables.  Some defines since we are
 * emulating the i386 page directory layout.
 */
#define PGD_PRESENT  0x001
#define PGD_RW       0x002
#define PGD_USER     0x004
#define PGD_ACCESSED 0x020
#define PGD_DIRTY    0x040
#define PGD_TABLE    (PGD_PRESENT | PGD_RW | PGD_USER | PGD_ACCESSED | PGD_DIRTY)

unsigned long sun4c_vmalloc_start(void)
{
	return SUN4C_VMALLOC_START;
}

/* Update the root mmu directory on the sun4c mmu. */
void sun4c_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdir)
{
	(tsk)->tss.pgd_ptr = (unsigned long) (pgdir);
}

int sun4c_pte_none(pte_t pte)		{ return !pte_val(pte); }
int sun4c_pte_present(pte_t pte)	{ return pte_val(pte) & _SUN4C_PAGE_VALID; }
int sun4c_pte_inuse(pte_t *ptep)        { return mem_map[MAP_NR(ptep)].reserved || mem_map[MAP_NR(ptep)].count != 1; }
void sun4c_pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }
void sun4c_pte_reuse(pte_t *ptep)
{
	if(!mem_map[MAP_NR(ptep)].reserved)
		mem_map[MAP_NR(ptep)].count++;
}

int sun4c_pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
int sun4c_pmd_bad(pmd_t pmd)
{
	return (pmd_val(pmd) & ~PAGE_MASK) != PGD_TABLE || pmd_val(pmd) > high_memory;
}

int sun4c_pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & PGD_PRESENT; }
int sun4c_pmd_inuse(pmd_t *pmdp)        { return 0; }
void sun4c_pmd_clear(pmd_t *pmdp)	{ pmd_val(*pmdp) = 0; }
void sun4c_pmd_reuse(pmd_t * pmdp)      { }

int sun4c_pgd_none(pgd_t pgd)		{ return 0; }
int sun4c_pgd_bad(pgd_t pgd)		{ return 0; }
int sun4c_pgd_present(pgd_t pgd)	{ return 1; }
int sun4c_pgd_inuse(pgd_t *pgdp)        { return mem_map[MAP_NR(pgdp)].reserved; }
void sun4c_pgd_clear(pgd_t * pgdp)	{ }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
int sun4c_pte_read(pte_t pte)		{ return !(pte_val(pte) & _SUN4C_PAGE_PRIV); }
int sun4c_pte_write(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_WRITE; }
int sun4c_pte_exec(pte_t pte)		{ return !(pte_val(pte) & _SUN4C_PAGE_PRIV); }
int sun4c_pte_dirty(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_DIRTY; }
int sun4c_pte_young(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_REF; }
int sun4c_pte_cow(pte_t pte)		{ return pte_val(pte) & _SUN4C_PAGE_COW; }

pte_t sun4c_pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_WRITE; return pte; }
pte_t sun4c_pte_rdprotect(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_PRIV; return pte; }
pte_t sun4c_pte_exprotect(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_PRIV; return pte; }
pte_t sun4c_pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_DIRTY; return pte; }
pte_t sun4c_pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_REF; return pte; }
pte_t sun4c_pte_uncow(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_COW; return pte; }
pte_t sun4c_pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_WRITE; return pte; }
pte_t sun4c_pte_mkread(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_PRIV; return pte; }
pte_t sun4c_pte_mkexec(pte_t pte)	{ pte_val(pte) &= ~_SUN4C_PAGE_PRIV; return pte; }
pte_t sun4c_pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_DIRTY; return pte; }
pte_t sun4c_pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_REF; return pte; }
pte_t sun4c_pte_mkcow(pte_t pte)	{ pte_val(pte) |= _SUN4C_PAGE_COW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
pte_t sun4c_mk_pte(unsigned long page, pgprot_t pgprot)
{
	return __pte(((page - PAGE_OFFSET) >> PAGE_SHIFT) | pgprot_val(pgprot));
}

pte_t sun4c_pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _SUN4C_PAGE_CHG_MASK) | pgprot_val(newprot));
}

unsigned long sun4c_pte_page(pte_t pte)
{
	return (PAGE_OFFSET + ((pte_val(pte) & 0xffff) << (PAGE_SHIFT)));
}

unsigned long sun4c_pmd_page(pmd_t pmd)
{
	return (pmd_val(pmd) & PAGE_MASK);
}

/* to find an entry in a page-table-directory */
pgd_t *sun4c_pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> SUN4C_PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
pmd_t *sun4c_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
pte_t *sun4c_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) sun4c_pmd_page(*dir) +	((address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1));
}

/* Here comes the sun4c mmu-tlb management engine.  It is here because
 * some of the mid-level mm support needs to be able to lock down
 * critical areas of kernel memory into the tlb.
 */
static inline void add_pseg_list(struct pseg_list *head, struct pseg_list *entry)
{
	entry->next = head;
	(entry->prev = head->prev)->next = entry;
	head->prev = entry;
}
#define add_to_used_pseg_list(entry) add_pseg_list(&s4cpseg_used, entry)
#define add_to_free_pseg_list(entry) add_pseg_list(&s4cpseg_free, entry)
#define add_to_locked_pseg_list(entry) add_pseg_list(&s4cpseg_locked, entry)

static inline void remove_pseg_list(struct pseg_list *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

static inline void add_pseg_ctxlist(struct pseg_list *entry, int ctx)
{
	struct pseg_list *head = &s4cpseg_per_context[ctx];

	entry->ctx_next = head;
	(entry->ctx_prev = head->ctx_prev)->ctx_next = entry;
	head->ctx_prev = entry;
	pseg_count_per_context[ctx]++;
}

static inline void remove_pseg_ctxlist(struct pseg_list *entry, int ctx)
{
	entry->ctx_next->ctx_prev = entry->ctx_prev;
	entry->ctx_prev->ctx_next = entry->ctx_next;
	pseg_count_per_context[ctx]--;
}

static inline void sun4c_init_pseg_lists(void)
{
	int i;

	s4cpseg_free.prev = s4cpseg_free.next = &s4cpseg_free;
	s4cpseg_used.prev = s4cpseg_used.next = &s4cpseg_used;
	s4cpseg_locked.prev = s4cpseg_locked.next = &s4cpseg_locked;
	for(i = 0; i < num_contexts; i++) {
		s4cpseg_per_context[i].ctx_prev = s4cpseg_per_context[i].ctx_next =
			&s4cpseg_per_context[i];
	}
	for(i = 0; i <= invalid_segment; i++) {
		s4cpseg_pool[i].vaddr = 0;
		s4cpseg_pool[i].context = 0;
		s4cpseg_pool[i].ref_cnt = 0;
		s4cpseg_pool[i].hardlock = 0;
		s4cpseg_pool[i].pseg = i;
	}
	s4cpseg_pool[invalid_segment].hardlock = 1;
}

static inline void sun4c_distribute_kernel_mapping(unsigned long address,
						   unsigned char pseg)
{
	unsigned int flags;
	int ctx, save_ctx;

	save_flags(flags); cli();
	save_ctx = get_context();
	flush_user_windows();
	for(ctx = 0; ctx < num_contexts; ctx++) {
		set_context(ctx);
		put_segmap(address, pseg);
	}
	set_context(save_ctx);
	restore_flags(flags);
}

static inline void sun4c_delete_kernel_mapping(unsigned long address)
{
	unsigned int flags;
	int ctx, save_ctx;

	save_flags(flags); cli();
	save_ctx = get_context();
	flush_user_windows();

	/* Flush only needed in one context for kernel mappings. */
	sun4c_flush_segment(address);
	for(ctx = 0; ctx < num_contexts; ctx++) {
		set_context(ctx);
		put_segmap(address, invalid_segment);
	}
	set_context(save_ctx);
	restore_flags(flags);
}

/* NOTE: You can only lock kernel tlb entries, attempts to lock
 *       pages in user vm will bolix the entire system.
 */
static inline void sun4c_lock_tlb_entry(unsigned long address)
{
	unsigned long flags;
	unsigned char pseg;

	save_flags(flags); cli();
	/* Fault it in. */
	__asm__ __volatile__("ldub [%0], %%g0\n\t" : : "r" (address));
	address &= SUN4C_REAL_PGDIR_MASK;
	pseg = get_segmap(address);
	if(address < KERNBASE)
		panic("locking user address space into tlb!");
	if(pseg == invalid_segment)
		panic("cannot lock kernel tlb entry...");
	if(!s4cpseg_pool[pseg].ref_cnt++ && !s4cpseg_pool[pseg].hardlock) {
		/* Move from used to locked list. */
		remove_pseg_list(&s4cpseg_pool[pseg]);
		add_to_locked_pseg_list(&s4cpseg_pool[pseg]);
	}
	restore_flags(flags);
}

static inline void sun4c_unlock_tlb_entry(unsigned long address)
{
	unsigned long flags;
	struct pseg_list *psegp;
	unsigned char pseg;

	save_flags(flags); cli();
	address &= SUN4C_REAL_PGDIR_MASK;
	pseg = get_segmap(address);
	if(address < KERNBASE)
		panic("unlocking user tlb entry!");
	if(pseg == invalid_segment)
		panic("unlocking non-locked kernel tlb entry...");
	psegp = &s4cpseg_pool[pseg];
	if(!--psegp->ref_cnt && !psegp->hardlock) {
		/* Move from locked list to used list. */
		remove_pseg_list(psegp);
		add_to_used_pseg_list(psegp);
	}
	restore_flags(flags);
}

/* Anyone who calls this must turn _all_ interrupts off and flush
 * any necessary user windows beforehand.
 */
static inline void sun4c_unload_context_from_tlb(unsigned char ctx)
{
	struct pseg_list *psegp, *pnextp;

	if(pseg_count_per_context[ctx]) {
		sun4c_flush_context(); /* Most efficient */
		psegp = s4cpseg_per_context[ctx].ctx_next;
		while(psegp != &s4cpseg_per_context[ctx]) {
			pnextp = psegp->ctx_next;
			if(psegp->vaddr >= KERNBASE)
				panic("Unloading kernel from tlb, not good.");
			put_segmap(psegp->vaddr, invalid_segment);
			remove_pseg_ctxlist(psegp, ctx);
			remove_pseg_list(psegp);
			add_to_free_pseg_list(psegp);
			psegp = pnextp;
		}
		if(pseg_count_per_context[ctx])
			panic("pseg_count_per_context inconsistant after "
			      "invalidate.");
	}
}

/* This page must be a page in user vma... again all IRQ's gotta be off. */
static inline void sun4c_unload_page_from_tlb(unsigned long addr,
					      struct task_struct *tsk)
{
	unsigned char save_ctx;

	if(tsk->tss.context != -1) {
		save_ctx = get_context();
		flush_user_windows();
		set_context(tsk->tss.context);
		sun4c_flush_page(addr);
		put_pte(addr, 0);
		set_context(save_ctx);
	}
}

/* NOTE: When we have finer grained invalidate()'s (RSN) this
 *       whole scheme will be much more efficient and need to
 *       be re-written.  Also note that this routine only
 *       unloads user page translations, this may need to
 *       be changed at some point.
 */
void sun4c_invalidate(void)
{
	int orig_ctx, cur_ctx, flags;

	save_flags(flags); cli();
	flush_user_windows();
	orig_ctx = get_context();
	for(cur_ctx = 0; cur_ctx < num_contexts; cur_ctx++) {
		set_context(cur_ctx);
		sun4c_unload_context_from_tlb(cur_ctx);
	}
	set_context(orig_ctx);
	restore_flags(flags);
}

/* We're only updating software tables on the sun4c. */
void sun4c_set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

/* Now back to the mid-level interface code:
 *
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
void sun4c_pte_free_kernel(pte_t *pte)
{
	mem_map[MAP_NR(pte)].reserved = 0;
	free_page((unsigned long) pte);
}

pte_t *sun4c_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1);
	if (sun4c_pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if (sun4c_pmd_none(*pmd)) {
			if (page) {
				pmd_val(*pmd) = PGD_TABLE | (unsigned long) page;
				mem_map[MAP_NR(page)].reserved = 1;
				return page + address;
			}
			pmd_val(*pmd) = PGD_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (sun4c_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = PGD_TABLE | (unsigned long) BAD_PAGETABLE;
		return NULL;
	}
	return (pte_t *) sun4c_pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
void sun4c_pmd_free_kernel(pmd_t *pmd)
{
	pmd_val(*pmd) = 0;
}

pmd_t *sun4c_pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

void sun4c_pte_free(pte_t *pte)
{
	free_page((unsigned long) pte);
}

pte_t *sun4c_pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SUN4C_PTRS_PER_PTE - 1);
	if (sun4c_pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if (sun4c_pmd_none(*pmd)) {
			if (page) {
				pmd_val(*pmd) = PGD_TABLE | (unsigned long) page;
				return page + address;
			}
			pmd_val(*pmd) = PGD_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (sun4c_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = PGD_TABLE | (unsigned long) BAD_PAGETABLE;
		return NULL;
	}
	return (pte_t *) sun4c_pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
void sun4c_pmd_free(pmd_t * pmd)
{
	pmd_val(*pmd) = 0;
}

pmd_t *sun4c_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

void sun4c_pgd_free(pgd_t *pgd)
{
	free_page((unsigned long) pgd);
	sun4c_unlock_tlb_entry((unsigned long) pgd);
}

pgd_t *sun4c_pgd_alloc(void)
{
	unsigned long new_pgd = get_free_page(GFP_KERNEL);
	sun4c_lock_tlb_entry(new_pgd);
	return (pgd_t *) new_pgd;
}

/* Jumping to and fro different contexts, the modifying of the pseg lists
 * must be atomic during the switch, or else...
 */
void sun4c_switch_to_context(void *new_task)
{
	struct task_struct *tsk = (struct task_struct *) new_task;
	struct task_struct *old_tsk;
	struct ctx_list *ctxp;
	unsigned long flags;
	int ctx = tsk->tss.context;

	/* Swapper can execute in any context, or this task
	 * has already been allocated a piece of the mmu real-
	 * estate.
	 */
	if(tsk->pid == 0 || ctx != -1)
		return;
	ctxp = ctx_free.next;
	if(ctxp != &ctx_free) {
		save_flags(flags); cli();
		ctx = ctxp->ctx_number;
		remove_from_ctx_list(ctxp);
		add_to_used_ctxlist(ctxp);
		tsk->tss.context = ctx;
		ctxp->ctx_task = tsk;
		restore_flags(flags);
		return;
	}
	save_flags(flags); cli();
	ctxp = ctx_used.prev;
	/* Don't steal from current, thank you. */
	if(ctxp->ctx_task == current)
		ctxp = ctxp->prev;
	if(ctxp == &ctx_used)
		panic("out of contexts");
	remove_from_ctx_list(ctxp);
	old_tsk = ctxp->ctx_task;
	old_tsk->tss.context = -1;
	ctxp->ctx_task = tsk;
	tsk->tss.context = ctxp->ctx_number;
	add_to_used_ctxlist(ctxp);
	/* User windows flushed already by switch_to(p) macro. */
	set_context(ctxp->ctx_number);
	sun4c_unload_context_from_tlb(ctxp->ctx_number);
	restore_flags(flags);
}

/* Low level IO area allocation on the Sun4c MMU.  This function is called
 * for each page of IO area you need.  Kernel code should not call this
 * routine directly, use sparc_alloc_io() instead.
 */
void sun4c_mapioaddr(unsigned long physaddr, unsigned long virt_addr,
		     int bus_type, int rdonly)
{
	unsigned long page_entry;

	page_entry = ((physaddr >> PAGE_SHIFT) & 0xffff);
	page_entry |= (_SUN4C_PAGE_VALID | _SUN4C_PAGE_WRITE |
		       _SUN4C_PAGE_NOCACHE | _SUN4C_PAGE_IO);
	if(rdonly)
		page_entry &= (~_SUN4C_PAGE_WRITE);
	sun4c_flush_page(virt_addr);
	put_pte(virt_addr, page_entry);
}

/* These routines are used to lock down and unlock data transfer
 * areas in the sun4c tlb.  If the pages need to be uncached the
 * caller must do that himself.
 */
inline char *sun4c_lockarea(char *vaddr, unsigned long size)
{
	unsigned long flags;
	unsigned long orig_addr = (unsigned long) vaddr;
	unsigned long first_seg = (orig_addr & SUN4C_REAL_PGDIR_MASK);
	unsigned long last_seg = ((orig_addr + size) & SUN4C_REAL_PGDIR_MASK);

	save_flags(flags); cli();
	for(; first_seg <= last_seg; first_seg += SUN4C_REAL_PGDIR_SIZE)
		sun4c_lock_tlb_entry(first_seg);

	restore_flags(flags);
	return vaddr;
}

/* Note that when calling unlockarea you pass as 'vaddr' the address that
 * was returned to you by lockarea for this pool above.
 */
inline void sun4c_unlockarea(char *vaddr, unsigned long size)
{
	unsigned long flags;
	unsigned long orig_addr = (unsigned long) vaddr;
	unsigned long first_seg = (orig_addr & SUN4C_REAL_PGDIR_MASK);
	unsigned long last_seg = ((orig_addr + size) & SUN4C_REAL_PGDIR_MASK);

	save_flags(flags); cli();
	for(; first_seg <= last_seg; first_seg += SUN4C_REAL_PGDIR_SIZE)
		sun4c_unlock_tlb_entry(first_seg);

	restore_flags(flags);
}

/* Getting and Releasing scsi dvma buffers. */
char *sun4c_get_scsi_buffer(char *bufptr, unsigned long len)
{
	unsigned long first_page = ((unsigned long) bufptr) & PAGE_MASK;
	unsigned long last_page = (((unsigned long) bufptr) + len) & PAGE_MASK;

	/* First lock down the area. */
	bufptr = sun4c_lockarea(bufptr, len);

	/* Uncache and flush all the pages. */
	for(; first_page <= last_page; first_page += PAGE_SIZE) {
		sun4c_flush_page(first_page);
		put_pte(first_page, get_pte(first_page) | PTE_NC);
	}
	return bufptr;
}

void sun4c_release_scsi_buffer(char *bufptr, unsigned long len)
{
	unsigned long first_page = ((unsigned long) bufptr) & PAGE_MASK;
	unsigned long last_page = (((unsigned long) bufptr) + len) & PAGE_MASK;


	/* Recache all the pages. */
	for(; first_page <= last_page; first_page += PAGE_SIZE)
		put_pte(first_page, get_pte(first_page) & ~PTE_NC);

	sun4c_unlockarea(bufptr, len);
}

/* Code to fill the sun4c tlb during a fault.  Plus fault helper routine. */
int sun4c_get_fault_info(unsigned long *address, unsigned long *error_code,
			 unsigned long from_user)
{
	unsigned long faddr, fstatus, new_code;

	faddr = sun4c_get_synchronous_address();
	*address = faddr;
	if(faddr >= 0x20000000 && faddr < 0xe0000000) {
		printk("SUN4C: Fault in vm hole at %08lx\n", faddr);
		*error_code = from_user;
		return 1;
	}
	fstatus = sun4c_get_synchronous_error();
	if(fstatus & SUN4C_SYNC_BOLIXED)
		panic("SUN4C: Unrecoverable fault type.");
	new_code = 0;
	if(fstatus & SUN4C_SYNC_PROT)
		new_code |= FAULT_CODE_PROT;
	if(fstatus & SUN4C_SYNC_BADWRITE)
		new_code |= FAULT_CODE_WRITE;
	*error_code = (new_code | from_user);
	return 0;
}

static inline void sun4c_alloc_pseg(unsigned long address)
{
	struct pseg_list *psegp;
	unsigned char cur_ctx = get_context();
	int kernel_address = (address >= KERNBASE);
	int user_address = !kernel_address;

	psegp = s4cpseg_free.next;
	if(psegp != &s4cpseg_free) {
		remove_pseg_list(psegp);
		add_to_used_pseg_list(psegp);
		if(user_address)
			add_pseg_ctxlist(psegp, cur_ctx);
		psegp->vaddr = address;
		psegp->context = cur_ctx;
		/* No cache flush needed */
		if(kernel_address)
			sun4c_distribute_kernel_mapping(address, psegp->pseg);
		else
			put_segmap(address, psegp->pseg);
		return;
	}
	psegp = s4cpseg_used.prev; /* Take last used list entry. */
	if(psegp == &s4cpseg_used)
		panic("Sun4c psegs have disappeared...");
	if(psegp->vaddr >= KERNBASE) {
		sun4c_delete_kernel_mapping(psegp->vaddr);
	} else {
		flush_user_windows();
		set_context(psegp->context);
		sun4c_flush_segment(psegp->vaddr);
		put_segmap(psegp->vaddr, invalid_segment);
		set_context(cur_ctx);
	}
	remove_pseg_list(psegp);
	if(psegp->vaddr < KERNBASE)
		remove_pseg_ctxlist(psegp, psegp->context);
	psegp->vaddr = address;
	psegp->context = cur_ctx;
	if(kernel_address)
		sun4c_distribute_kernel_mapping(address, psegp->pseg);
	else
		put_segmap(address, psegp->pseg);
	add_to_used_pseg_list(psegp);
	if(user_address)
		add_pseg_ctxlist(psegp, cur_ctx);
}

/*
 * handle_mm_fault() gets here so that we can update our 'view'
 * of a new address translation.  A lot of the time, mappings
 * don't change and we are just 'working the tlb cache'.
 */
void sun4c_update_mmu_cache(struct vm_area_struct * vma,
			    unsigned long address, pte_t pte)
{
	unsigned long flags, segmap, segaddr, clean;

	save_flags(flags); cli();
	address &= PAGE_MASK;
	segaddr = address & SUN4C_REAL_PGDIR_MASK;
	segmap = get_segmap(segaddr);
	if(segmap == invalid_segment) {
		sun4c_alloc_pseg(segaddr);
		/* XXX make segmap freeing routines do this. XXX */
		for(clean = segaddr; clean < (segaddr + SUN4C_REAL_PGDIR_SIZE);
		    clean += PAGE_SIZE)
			put_pte(clean, 0);
	}

	/* If this is a user fault, only load the one pte so that
	 * the kernel's ref/mod bits accurately reflect what is
	 * in the tlb.  handle_pte_fault() causes this to work.
	 */
	if(address < TASK_SIZE)
		put_pte(address, pte_val(pte));
	else {
		/* We have a kernel fault here, load entire segment. */
		pgd_t *pgdp;
		pte_t *ptable;
		int pnum = 64;

		pgdp = sun4c_pgd_offset(&init_mm, segaddr);
		ptable = sun4c_pte_offset((pmd_t *)pgdp, segaddr);
		while(pnum--) {
			put_pte(segaddr, pte_val(*ptable++));
			segaddr += PAGE_SIZE;
		};
	}
	restore_flags(flags);
}

/* Paging initialization on the Sun4c. */
static inline void sun4c_free_all_nonlocked_psegs(void)
{
	struct pseg_list *plp;
	int i;

	for(i=0; i < invalid_segment; i++)
		if(!s4cpseg_pool[i].hardlock)
			add_to_free_pseg_list(&s4cpseg_pool[i]);
	/* Now for every free pseg, make all the ptes invalid. */
	plp = s4cpseg_free.next;
	while(plp != &s4cpseg_free) {
		put_segmap(0x0, plp->pseg);
		for(i=0; i<64; i++)
			put_pte((i * PAGE_SIZE), 0x0);
		plp = plp->next;
	}
	put_segmap(0x0, invalid_segment);
}

static inline struct pseg_list *sun4c_alloc_pseg_from_free_list(void)
{
	struct pseg_list *psegp;

	psegp = s4cpseg_free.next;
	if(psegp != &s4cpseg_free) {
		remove_pseg_list(psegp);
		return psegp;
	}
	return 0;
}

static inline void sun4c_init_lock_area(unsigned long start_addr,
					unsigned long end_addr)
{
	struct pseg_list *psegp;
	unsigned long a;
	int ctx;

	for(a = start_addr; a < end_addr; a += SUN4C_REAL_PGDIR_SIZE) {
		psegp = sun4c_alloc_pseg_from_free_list();
		if(!psegp) {
			prom_printf("whoops...");
			prom_halt();
		}
		for(ctx=0;ctx<num_contexts;ctx++)
			prom_putsegment(ctx,a,psegp->pseg);
		add_to_locked_pseg_list(psegp);
		psegp->hardlock = 1;
	}
}

static inline void sun4c_check_for_ss2_cache_bug(void)
{
	extern unsigned long start;

	/* Well we've now got a problem, on the SS2 a cache bug
	 * causes line entries to get severely corrupted if the
	 * trap table is able to be cached.  A sane and simple
	 * workaround, at least for now, is to mark the trap
	 * table page as uncacheable.
	 *
	 * XXX Investigate other possible workarounds and see
	 * XXX if they help performance enough to warrant using
	 * XXX them.                      -- 8/6/95 davem
	 */
	if(idprom->id_machtype == (SM_SUN4C | SM_4C_SS2)) {
		/* Whee.. */
		printk("SS2 cache bug detected, uncaching trap table page\n");
		sun4c_flush_page((unsigned int) &start);
		put_pte(((unsigned long) &start),
			(get_pte((unsigned long) &start) | PTE_NC));
	}
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/* Whee, this is now *ultra* clean and more managable */
extern unsigned long end;
extern void probe_mmu(void);

unsigned long sun4c_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long addr, vaddr, kern_begin, kern_end;
	unsigned long prom_begin, prom_end, kadb_begin;
	pgd_t *pgdp;
	pte_t *pg_table;
	int phys_seg, i, ctx;

	start_mem = PAGE_ALIGN(start_mem);

	probe_mmu();
	invalid_segment = (num_segmaps - 1);
	sun4c_init_pseg_lists();
	for(kern_begin = KERNBASE;
	    kern_begin < (unsigned long) &end;
	    kern_begin += SUN4C_REAL_PGDIR_SIZE) {
		unsigned char pseg = get_segmap(kern_begin);

		s4cpseg_pool[pseg].hardlock=1;
		for(ctx=0; ctx<num_contexts;ctx++)
			prom_putsegment(ctx,kern_begin,pseg);
	}
	for(kern_begin = SUN4C_REAL_PGDIR_ALIGN((unsigned long) &end);
	    kern_begin < KADB_DEBUGGER_BEGVM;
	    kern_begin += SUN4C_REAL_PGDIR_SIZE)
		for(ctx=0; ctx<num_contexts;ctx++)
			prom_putsegment(ctx, kern_begin, invalid_segment);
	for(prom_begin = KADB_DEBUGGER_BEGVM;
	    prom_begin < LINUX_OPPROM_ENDVM;
	    prom_begin += SUN4C_REAL_PGDIR_SIZE) {
		unsigned long pseg = get_segmap(prom_begin);

		if(pseg != invalid_segment) {
			s4cpseg_pool[pseg].hardlock=1;
			for(ctx=0; ctx<num_contexts; ctx++)
				prom_putsegment(ctx,prom_begin,pseg);
		}
	}
	/* Clean the MMU of excess garbage... */
	for(ctx=0; ctx<num_contexts;ctx++) {
		set_context(ctx);
		for(vaddr = 0; vaddr < 0x20000000;
		    vaddr += SUN4C_REAL_PGDIR_SIZE)
			put_segmap(vaddr,invalid_segment);
		for(vaddr = 0xe0000000; vaddr < KERNBASE;
		    vaddr += SUN4C_REAL_PGDIR_SIZE)
			put_segmap(vaddr,invalid_segment);
		for(vaddr = LINUX_OPPROM_ENDVM; vaddr != 0;
		    vaddr += SUN4C_REAL_PGDIR_SIZE)
			put_segmap(vaddr,invalid_segment);
	}
	set_context(0);
	sun4c_free_all_nonlocked_psegs();
	/* Lock I/O and DVMA areas for the system. */
	sun4c_init_lock_area(IOBASE_VADDR, IOBASE_END);
	sun4c_init_lock_area(DVMA_VADDR, DVMA_END);
	/* Zero out swapper_pg_dir and pg0 */
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	memset(pg0, 0, PAGE_SIZE);
	/* This makes us Solaris boot-loader 'safe' */
	pgd_val(swapper_pg_dir[KERNBASE>>SUN4C_PGDIR_SHIFT]) =
		PGD_TABLE | (unsigned long) pg0;

	/* Initialize swapper_pg_dir to map the kernel
	 * addresses in high memory.  Note that as soon as we get past
	 * the 4MB lower mapping and start using dynamic memory from
	 * start_mem we can start faulting and this is ok since our
	 * pseg free list and the lower 4MB of the kernel is mapped
	 * properly in the software page tables.
	 */
	pgdp = swapper_pg_dir;
	kern_end = PAGE_ALIGN(end_mem);
	kern_begin = KERNBASE;
	while(kern_begin < kern_end) {
		unsigned long pte, tmp;

		/* We only need _one_ mapping, the high address one. */
		pg_table = (pte_t *) (PAGE_MASK & pgd_val(pgdp[KERNBASE>>SUN4C_PGDIR_SHIFT]));
		if(!pg_table) {
			pg_table = (pte_t *) start_mem;
			start_mem += PAGE_SIZE;
		}
		pgd_val(pgdp[KERNBASE>>SUN4C_PGDIR_SHIFT]) =
			PGD_TABLE | (unsigned long) pg_table;
		pgdp++;
		for(tmp = 0; tmp < SUN4C_PTRS_PER_PTE; tmp++, pg_table++) {
			if(kern_begin < kern_end)
				sun4c_set_pte(pg_table,
					      mk_pte(kern_begin,
						     SUN4C_PAGE_SHARED));
			else
				sun4c_pte_clear(pg_table);
			pte = get_pte(kern_begin);
			if(pte & _SUN4C_PAGE_VALID) {
				pte &= ~(_SUN4C_PAGE_NOCACHE);
				pte |= (_SUN4C_PAGE_PRIV | _SUN4C_PAGE_WRITE |
					_SUN4C_PAGE_REF | _SUN4C_PAGE_DIRTY);
				put_pte(kern_begin, pte);
			}
			kern_begin += PAGE_SIZE;
		}
	}
	sun4c_check_for_ss2_cache_bug();
	/* Fix kadb/prom permissions. */
	kadb_begin = KADB_DEBUGGER_BEGVM;
	prom_end = LINUX_OPPROM_ENDVM;
	for(; kadb_begin < prom_end; kadb_begin += PAGE_SIZE) {
		unsigned long pte = get_pte(kadb_begin);
		if(pte & _SUN4C_PAGE_VALID)
			put_pte(kadb_begin, (pte | _SUN4C_PAGE_PRIV));
	}
	/* Allocate the DVMA pages */
	addr = DVMA_VADDR;
	start_mem = PAGE_ALIGN(start_mem);
	while(addr < DVMA_END) {
		unsigned long dvmapte = start_mem - PAGE_OFFSET;

		start_mem += PAGE_SIZE;
		dvmapte = ((dvmapte>>PAGE_SHIFT) & 0xffff);
		dvmapte |= (_SUN4C_PAGE_VALID |
			    _SUN4C_PAGE_WRITE |
			    _SUN4C_PAGE_NOCACHE);
		put_pte(addr, dvmapte);
		addr += PAGE_SIZE;
	}
	/* Tell the user our allocations */
	for(phys_seg=0, i=0; i<=invalid_segment; i++)
		if(s4cpseg_pool[i].hardlock)
			phys_seg++;
	printk("SUN4C: Hard locked %d boot-up psegs\n", phys_seg);
	/* Init the context pool and lists */
	ctx_list_pool = (struct ctx_list *) start_mem;
	start_mem += (num_contexts * sizeof(struct ctx_list));
	for(ctx = 0; ctx < num_contexts; ctx++) {
		struct ctx_list *clist;

		clist = (ctx_list_pool + ctx);
		clist->ctx_number = ctx;
		clist->ctx_task = 0;
	}
	ctx_free.next = ctx_free.prev = &ctx_free;
	ctx_used.next = ctx_used.prev = &ctx_used;
	for(ctx = 0; ctx < num_contexts; ctx++)
		add_to_free_ctxlist(ctx_list_pool + ctx);
	start_mem = PAGE_ALIGN(start_mem);
	start_mem = free_area_init(start_mem, end_mem);
	start_mem = PAGE_ALIGN(start_mem);
	return start_mem;
}

/* Test the WP bit on the sun4c. */
void sun4c_test_wp(void)
{
	wp_works_ok = -1;

	/* Let it rip... */
	put_pte((unsigned long) 0x0, (PTE_V | PTE_P));
	__asm__ __volatile__("st %%g0, [0x0]\n\t": : :"memory");
	put_pte((unsigned long) 0x0, 0x0);
	if (wp_works_ok < 0)
		wp_works_ok = 0;
}

void sun4c_lock_entire_kernel(unsigned long start_mem)
{
	unsigned long addr = (unsigned long) &end;

	addr = (addr & SUN4C_REAL_PGDIR_MASK);
	start_mem = SUN4C_REAL_PGDIR_ALIGN(start_mem);
	while(addr < start_mem) {
		int pseg;

		sun4c_lock_tlb_entry(addr);
		pseg = get_segmap(addr);
		if(!s4cpseg_pool[pseg].hardlock) {
			s4cpseg_pool[pseg].hardlock = 1;
			remove_pseg_list(&s4cpseg_pool[pseg]);
		}
		addr += SUN4C_REAL_PGDIR_SIZE;
	}
}

static void sun4c_fork_hook(void *vtask, unsigned long kthread_usp)
{
	struct task_struct *new_task = vtask;

	/* These pages must not cause a fault when traps
	 * are off (such as in a window spill/fill) so
	 * lock them down for the life of the task.
	 */
	sun4c_lock_tlb_entry((unsigned long) new_task);
	sun4c_lock_tlb_entry(new_task->kernel_stack_page);
	if(kthread_usp)
		sun4c_lock_tlb_entry(kthread_usp);
}

static void sun4c_release_hook(void *vtask)
{
	struct task_struct *old_task = vtask;
	struct ctx_list *ctx_old;
	struct pt_regs *regs;
	unsigned char this_ctx = get_context();
	unsigned long flags;

	save_flags(flags); cli();
	if(old_task == &init_task)
		panic("AIEEE releasing swapper");
	if(old_task->tss.context != -1) {

		/* Clear from the mmu, all notions of this dead task. */
		flush_user_windows();
		set_context(old_task->tss.context);
		sun4c_unload_context_from_tlb(old_task->tss.context);
		set_context(this_ctx);

		ctx_old = ctx_list_pool + old_task->tss.context;
		remove_from_ctx_list(ctx_old);
		add_to_free_ctxlist(ctx_old);
		old_task->tss.context = -1;
	}
	regs = (struct pt_regs *) 
		(((old_task->tss.ksp & ~0xfff)) + (0x1000 - TRACEREG_SZ));
	if(regs->u_regs[UREG_FP] > KERNBASE)
		sun4c_unlock_tlb_entry(regs->u_regs[UREG_FP] & PAGE_MASK);
	sun4c_unlock_tlb_entry(old_task->kernel_stack_page);
	sun4c_unlock_tlb_entry((unsigned long) old_task);
	restore_flags(flags);
	/* bye bye... */
}

static void sun4c_flush_hook(void *vtask)
{
	struct task_struct *dead_task = vtask;

	if(dead_task->tss.context != -1)
		sun4c_flush_context();
}

static void sun4c_task_cacheflush(void *vtask)
{
	struct task_struct *flush_task = vtask;

	if(flush_task->tss.context != -1)
		sun4c_flush_context();
}

static void sun4c_exit_hook(void *vtask)
{
}

/* Load up routines and constants for sun4c mmu */
void ld_mmu_sun4c(void)
{
	printk("Loading sun4c MMU routines\n");

	/* First the constants */
	pmd_shift = SUN4C_PMD_SHIFT;
	pmd_size = SUN4C_PMD_SIZE;
	pmd_mask = SUN4C_PMD_MASK;
	pgdir_shift = SUN4C_PGDIR_SHIFT;
	pgdir_size = SUN4C_PGDIR_SIZE;
	pgdir_mask = SUN4C_PGDIR_MASK;

	ptrs_per_pte = SUN4C_PTRS_PER_PTE;
	ptrs_per_pmd = SUN4C_PTRS_PER_PMD;
	ptrs_per_pgd = SUN4C_PTRS_PER_PGD;

	page_none = SUN4C_PAGE_NONE;
	page_shared = SUN4C_PAGE_SHARED;
	page_copy = SUN4C_PAGE_COPY;
	page_readonly = SUN4C_PAGE_READONLY;
	page_kernel = SUN4C_PAGE_KERNEL;
	page_invalid = SUN4C_PAGE_INVALID;
	
	/* Functions */
	invalidate = sun4c_invalidate;
	set_pte = sun4c_set_pte;
	switch_to_context = sun4c_switch_to_context;
	pmd_align = sun4c_pmd_align;
	pgdir_align = sun4c_pgdir_align;
	vmalloc_start = sun4c_vmalloc_start;

	pte_page = sun4c_pte_page;
	pmd_page = sun4c_pmd_page;

	sparc_update_rootmmu_dir = sun4c_update_rootmmu_dir;

	pte_none = sun4c_pte_none;
	pte_present = sun4c_pte_present;
	pte_inuse = sun4c_pte_inuse;
	pte_clear = sun4c_pte_clear;
	pte_reuse = sun4c_pte_reuse;

	pmd_none = sun4c_pmd_none;
	pmd_bad = sun4c_pmd_bad;
	pmd_present = sun4c_pmd_present;
	pmd_inuse = sun4c_pmd_inuse;
	pmd_clear = sun4c_pmd_clear;
	pmd_reuse = sun4c_pmd_reuse;

	pgd_none = sun4c_pgd_none;
	pgd_bad = sun4c_pgd_bad;
	pgd_present = sun4c_pgd_present;
	pgd_inuse = sun4c_pgd_inuse;
	pgd_clear = sun4c_pgd_clear;

	mk_pte = sun4c_mk_pte;
	pte_modify = sun4c_pte_modify;
	pgd_offset = sun4c_pgd_offset;
	pmd_offset = sun4c_pmd_offset;
	pte_offset = sun4c_pte_offset;
	pte_free_kernel = sun4c_pte_free_kernel;
	pmd_free_kernel = sun4c_pmd_free_kernel;
	pte_alloc_kernel = sun4c_pte_alloc_kernel;
	pmd_alloc_kernel = sun4c_pmd_alloc_kernel;
	pte_free = sun4c_pte_free;
	pte_alloc = sun4c_pte_alloc;
	pmd_free = sun4c_pmd_free;
	pmd_alloc = sun4c_pmd_alloc;
	pgd_free = sun4c_pgd_free;
	pgd_alloc = sun4c_pgd_alloc;

	pte_read = sun4c_pte_read;
	pte_write = sun4c_pte_write;
	pte_exec = sun4c_pte_exec;
	pte_dirty = sun4c_pte_dirty;
	pte_young = sun4c_pte_young;
	pte_cow = sun4c_pte_cow;
	pte_wrprotect = sun4c_pte_wrprotect;
	pte_rdprotect = sun4c_pte_rdprotect;
	pte_exprotect = sun4c_pte_exprotect;
	pte_mkclean = sun4c_pte_mkclean;
	pte_mkold = sun4c_pte_mkold;
	pte_uncow = sun4c_pte_uncow;
	pte_mkwrite = sun4c_pte_mkwrite;
	pte_mkread = sun4c_pte_mkread;
	pte_mkexec = sun4c_pte_mkexec;
	pte_mkdirty = sun4c_pte_mkdirty;
	pte_mkyoung = sun4c_pte_mkyoung;
	pte_mkcow = sun4c_pte_mkcow;
	get_fault_info = sun4c_get_fault_info;
	update_mmu_cache = sun4c_update_mmu_cache;
	mmu_exit_hook = sun4c_exit_hook;
	mmu_fork_hook = sun4c_fork_hook;
	mmu_release_hook = sun4c_release_hook;
	mmu_flush_hook = sun4c_flush_hook;
	mmu_task_cacheflush = sun4c_task_cacheflush;
	mmu_lockarea = sun4c_lockarea;
	mmu_unlockarea = sun4c_unlockarea;
	mmu_get_scsi_buffer = sun4c_get_scsi_buffer;
	mmu_release_scsi_buffer = sun4c_release_scsi_buffer;

	/* These should _never_ get called with two level tables. */
	pgd_set = 0;
	pgd_reuse = 0;
	pgd_page = 0;
}

/* $Id: fault.c,v 1.96 1998/11/08 11:13:56 davem Exp $
 * fault.c:  Page fault handlers for the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/head.h>

#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/memreg.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/smp.h>
#include <asm/traps.h>
#include <asm/kdebug.h>
#include <asm/uaccess.h>

#define ELEMENTS(arr) (sizeof (arr)/sizeof (arr[0]))

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];
extern int prom_node_root;

struct linux_romvec *romvec;

/* At boot time we determine these two values necessary for setting
 * up the segment maps and page table entries (pte's).
 */

int num_segmaps, num_contexts;
int invalid_segment;

/* various Virtual Address Cache parameters we find at boot time... */

int vac_size, vac_linesize, vac_do_hw_vac_flushes;
int vac_entries_per_context, vac_entries_per_segment;
int vac_entries_per_page;

/* Nice, simple, prom library does all the sweating for us. ;) */
int prom_probe_memory (void)
{
	register struct linux_mlist_v0 *mlist;
	register unsigned long bytes, base_paddr, tally;
	register int i;

	i = 0;
	mlist= *prom_meminfo()->v0_available;
	bytes = tally = mlist->num_bytes;
	base_paddr = (unsigned long) mlist->start_adr;
  
	sp_banks[0].base_addr = base_paddr;
	sp_banks[0].num_bytes = bytes;

	while (mlist->theres_more != (void *) 0){
		i++;
		mlist = mlist->theres_more;
		bytes = mlist->num_bytes;
		tally += bytes;
		if (i >= SPARC_PHYS_BANKS-1) {
			printk ("The machine has more banks than "
				"this kernel can support\n"
				"Increase the SPARC_PHYS_BANKS "
				"setting (currently %d)\n",
				SPARC_PHYS_BANKS);
			i = SPARC_PHYS_BANKS-1;
			break;
		}
    
		sp_banks[i].base_addr = (unsigned long) mlist->start_adr;
		sp_banks[i].num_bytes = mlist->num_bytes;
	}

	i++;
	sp_banks[i].base_addr = 0xdeadbeef;
	sp_banks[i].num_bytes = 0;

	/* Now mask all bank sizes on a page boundary, it is all we can
	 * use anyways.
	 */
	for(i=0; sp_banks[i].num_bytes != 0; i++)
		sp_banks[i].num_bytes &= PAGE_MASK;

	return tally;
}

/* Traverse the memory lists in the prom to see how much physical we
 * have.
 */
unsigned long
probe_memory(void)
{
	int total;

	total = prom_probe_memory();

	/* Oh man, much nicer, keep the dirt in promlib. */
	return total;
}

extern void sun4c_complete_all_stores(void);

/* Whee, a level 15 NMI interrupt memory error.  Let's have fun... */
asmlinkage void sparc_lvl15_nmi(struct pt_regs *regs, unsigned long serr,
				unsigned long svaddr, unsigned long aerr,
				unsigned long avaddr)
{
	sun4c_complete_all_stores();
	printk("FAULT: NMI received\n");
	printk("SREGS: Synchronous Error %08lx\n", serr);
	printk("       Synchronous Vaddr %08lx\n", svaddr);
	printk("      Asynchronous Error %08lx\n", aerr);
	printk("      Asynchronous Vaddr %08lx\n", avaddr);
	if (sun4c_memerr_reg)
		printk("     Memory Parity Error %08lx\n", *sun4c_memerr_reg);
	printk("REGISTER DUMP:\n");
	show_regs(regs);
	prom_halt();
}

static void unhandled_fault(unsigned long, struct task_struct *,
		struct pt_regs *) __attribute__ ((noreturn));

static void unhandled_fault(unsigned long address, struct task_struct *tsk,
                     struct pt_regs *regs)
{
	if((unsigned long) address < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL "
		       "pointer dereference");
	} else {
		printk(KERN_ALERT "Unable to handle kernel paging request "
		       "at virtual address %08lx\n", address);
	}
	printk(KERN_ALERT "tsk->mm->context = %08lx\n",
	       (unsigned long) tsk->mm->context);
	printk(KERN_ALERT "tsk->mm->pgd = %08lx\n",
	       (unsigned long) tsk->mm->pgd);
	lock_kernel();
	die_if_kernel("Oops", regs);
	unlock_kernel();
}

asmlinkage int lookup_fault(unsigned long pc, unsigned long ret_pc, 
			    unsigned long address)
{
	unsigned long g2;
	int i;
	unsigned insn;
	struct pt_regs regs;
	
	i = search_exception_table (ret_pc, &g2);
	switch (i) {
	/* load & store will be handled by fixup */
	case 3: return 3;
	/* store will be handled by fixup, load will bump out */
	/* for _to_ macros */
	case 1: insn = (unsigned)pc; if ((insn >> 21) & 1) return 1; break;
	/* load will be handled by fixup, store will bump out */
	/* for _from_ macros */
	case 2: insn = (unsigned)pc; 
		if (!((insn >> 21) & 1) || ((insn>>19)&0x3f) == 15) return 2; 
		break; 
	default: break;
	}
	memset (&regs, 0, sizeof (regs));
	regs.pc = pc;
	regs.npc = pc + 4;
	__asm__ __volatile__ ("
		rd %%psr, %0
		nop
		nop
		nop" : "=r" (regs.psr));
	unhandled_fault (address, current, &regs);
	/* Not reached */
	return 0;
}

asmlinkage void do_sparc_fault(struct pt_regs *regs, int text_fault, int write,
			       unsigned long address)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	unsigned int fixup;
	unsigned long g2;
	int from_user = !(regs->psr & PSR_PS);

	if(text_fault)
		address = regs->pc;

	down(&mm->mmap_sem);
	/* The kernel referencing a bad kernel pointer can lock up
	 * a sun4c machine completely, so we must attempt recovery.
	 */
	if(!from_user && address >= PAGE_OFFSET)
		goto bad_area;

	vma = find_vma(mm, address);
	if(!vma)
		goto bad_area;
	if(vma->vm_start <= address)
		goto good_area;
	if(!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if(expand_stack(vma, address))
		goto bad_area;
	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
good_area:
	if(write) {
		if(!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		/* Allow reads even for write-only mappings */
		if(!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}
	if (!handle_mm_fault(current, vma, address, write))
		goto do_sigbus;
	up(&mm->mmap_sem);
	return;
	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up(&mm->mmap_sem);
	/* Is this in ex_table? */
do_kernel_fault:
	g2 = regs->u_regs[UREG_G2];
	if (!from_user && (fixup = search_exception_table (regs->pc, &g2))) {
		if (fixup > 10) { /* Values below are reserved for other things */
			extern const unsigned __memset_start[];
			extern const unsigned __memset_end[];
			extern const unsigned __csum_partial_copy_start[];
			extern const unsigned __csum_partial_copy_end[];

#ifdef DEBUG_EXCEPTIONS
			printk("Exception: PC<%08lx> faddr<%08lx>\n", regs->pc, address);
			printk("EX_TABLE: insn<%08lx> fixup<%08x> g2<%08lx>\n",
				regs->pc, fixup, g2);
#endif
			if ((regs->pc >= (unsigned long)__memset_start &&
			     regs->pc < (unsigned long)__memset_end) ||
			    (regs->pc >= (unsigned long)__csum_partial_copy_start &&
			     regs->pc < (unsigned long)__csum_partial_copy_end)) {
			        regs->u_regs[UREG_I4] = address;
				regs->u_regs[UREG_I5] = regs->pc;
			}
			regs->u_regs[UREG_G2] = g2;
			regs->pc = fixup;
			regs->npc = regs->pc + 4;
			return;
		}
	}
	if(from_user) {
#if 0
		printk("Fault whee %s [%d]: segfaults at %08lx pc=%08lx\n",
		       tsk->comm, tsk->pid, address, regs->pc);
#endif
		tsk->tss.sig_address = address;
		tsk->tss.sig_desc = SUBSIG_NOMAPPING;
		force_sig(SIGSEGV, tsk);
		return;
	}
	unhandled_fault (address, tsk, regs);
	return;

do_sigbus:
	up(&mm->mmap_sem);
	tsk->tss.sig_address = address;
	tsk->tss.sig_desc = SUBSIG_MISCERROR;
	force_sig(SIGBUS, tsk);
	if (! from_user)
		goto do_kernel_fault;
}

asmlinkage void do_sun4c_fault(struct pt_regs *regs, int text_fault, int write,
			       unsigned long address)
{
	extern void sun4c_update_mmu_cache(struct vm_area_struct *,
					   unsigned long,pte_t);
	extern pgd_t *sun4c_pgd_offset(struct mm_struct *,unsigned long);
	extern pte_t *sun4c_pte_offset(pmd_t *,unsigned long);
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	pgd_t *pgdp;
	pte_t *ptep;

	if (text_fault)
		address = regs->pc;

	pgdp = sun4c_pgd_offset(mm, address);
	ptep = sun4c_pte_offset((pmd_t *) pgdp, address);

	if (pgd_val(*pgdp)) {
	    if (write) {
		if ((pte_val(*ptep) & (_SUN4C_PAGE_WRITE|_SUN4C_PAGE_PRESENT))
				   == (_SUN4C_PAGE_WRITE|_SUN4C_PAGE_PRESENT)) {

			*ptep = __pte(pte_val(*ptep) | _SUN4C_PAGE_ACCESSED |
				      _SUN4C_PAGE_MODIFIED |
				      _SUN4C_PAGE_VALID |
				      _SUN4C_PAGE_DIRTY);

			if (sun4c_get_segmap(address) != invalid_segment) {
				sun4c_put_pte(address, pte_val(*ptep));
				return;
			}
		}
	    } else {
		if ((pte_val(*ptep) & (_SUN4C_PAGE_READ|_SUN4C_PAGE_PRESENT))
				   == (_SUN4C_PAGE_READ|_SUN4C_PAGE_PRESENT)) {

			*ptep = __pte(pte_val(*ptep) | _SUN4C_PAGE_ACCESSED |
				      _SUN4C_PAGE_VALID);

			if (sun4c_get_segmap(address) != invalid_segment) {
				sun4c_put_pte(address, pte_val(*ptep));
				return;
			}
		}
	    }
	}

	/* This conditional is 'interesting'. */
	if (pgd_val(*pgdp) && !(write && !(pte_val(*ptep) & _SUN4C_PAGE_WRITE))
	    && (pte_val(*ptep) & _SUN4C_PAGE_VALID))
		/* Note: It is safe to not grab the MMAP semaphore here because
		 *       we know that update_mmu_cache() will not sleep for
		 *       any reason (at least not in the current implementation)
		 *       and therefore there is no danger of another thread getting
		 *       on the CPU and doing a shrink_mmap() on this vma.
		 */
		sun4c_update_mmu_cache (find_vma(current->mm, address), address,
					*ptep);
	else
		do_sparc_fault(regs, text_fault, write, address);
}

/* This always deals with user addresses. */
inline void force_user_fault(unsigned long address, int write)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;

#if 0
	printk("wf<pid=%d,wr=%d,addr=%08lx>\n",
	       tsk->pid, write, address);
#endif
	down(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if(!vma)
		goto bad_area;
	if(vma->vm_start <= address)
		goto good_area;
	if(!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if(expand_stack(vma, address))
		goto bad_area;
good_area:
	if(write)
		if(!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	else
		if(!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	if (!handle_mm_fault(current, vma, address, write))
		goto do_sigbus;
	up(&mm->mmap_sem);
	return;
bad_area:
	up(&mm->mmap_sem);
#if 0
	printk("Window whee %s [%d]: segfaults at %08lx\n",
	       tsk->comm, tsk->pid, address);
#endif
	tsk->tss.sig_address = address;
	tsk->tss.sig_desc = SUBSIG_NOMAPPING;
	send_sig(SIGSEGV, tsk, 1);
	return;

do_sigbus:
	up(&mm->mmap_sem);
	tsk->tss.sig_address = address;
	tsk->tss.sig_desc = SUBSIG_MISCERROR;
	force_sig(SIGBUS, tsk);
}

void window_overflow_fault(void)
{
	unsigned long sp;

	lock_kernel();
	sp = current->tss.rwbuf_stkptrs[0];
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 1);
	force_user_fault(sp, 1);
	unlock_kernel();
}

void window_underflow_fault(unsigned long sp)
{
	lock_kernel();
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 0);
	force_user_fault(sp, 0);
	unlock_kernel();
}

void window_ret_fault(struct pt_regs *regs)
{
	unsigned long sp;

	lock_kernel();
	sp = regs->u_regs[UREG_FP];
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 0);
	force_user_fault(sp, 0);
	unlock_kernel();
}

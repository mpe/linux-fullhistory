/* $Id: fault.c,v 1.77 1996/10/28 00:56:02 davem Exp $
 * fault.c:  Page fault handlers for the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
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

#define ELEMENTS(arr) (sizeof (arr)/sizeof (arr[0]))

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];
extern int prom_node_root;

extern void die_if_kernel(char *,struct pt_regs *);

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
			printk ("The machine has more banks that this kernel can support\n"
				"Increase the SPARC_PHYS_BANKS setting (currently %d)\n",
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

asmlinkage void do_sparc_fault(struct pt_regs *regs, int text_fault, int write,
			       unsigned long address)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	int from_user = !(regs->psr & PSR_PS);
#if 0
	static unsigned long last_one;
#endif

	down(&mm->mmap_sem);
	if(text_fault)
		address = regs->pc;

#if 0
	if(current->tss.ex.count) {
		printk("f<pid=%d,tf=%d,wr=%d,addr=%08lx,pc=%08lx>\n",
		       tsk->pid, text_fault, write, address, regs->pc);
		printk("EX: count<%d> pc<%08lx> expc<%08lx> address<%08lx>\n",
		       (int) current->tss.ex.count, current->tss.ex.pc,
		       current->tss.ex.expc, current->tss.ex.address);
#if 0
		if(last_one == address) {
			printk("Twice in a row, AIEEE.  Spinning so you can see the dump.\n");
			show_regs(regs);
			sti();
			while(1)
				barrier();
		}
		last_one = address;
#endif
	}
#endif
	/* Now actually handle the fault.  Do kernel faults special,
	 * because on the sun4c we could have faulted trying to read
	 * the vma area of the task and without the following code
	 * we'd fault recursively until all our stack is gone. ;-(
	 */
	if(!from_user && address >= PAGE_OFFSET) {
		quick_kernel_fault(address);
		return;
	}

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
	handle_mm_fault(vma, address, write);
	up(&mm->mmap_sem);
	return;
	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up(&mm->mmap_sem);
	/* Did we have an exception handler installed? */
	if(current->tss.ex.count == 1) {
		if(from_user) {
			printk("Yieee, exception signalled from user mode.\n");
		} else {
			/* Set pc to %g1, set %g1 to -EFAULT and %g2 to
			 * the faulting address so we can cleanup.
			 */
			printk("Exception: PC<%08lx> faddr<%08lx>\n", regs->pc, address);
			printk("EX: count<%d> pc<%08lx> expc<%08lx> address<%08lx>\n",
			       (int) current->tss.ex.count, current->tss.ex.pc,
			       current->tss.ex.expc, current->tss.ex.address);
			current->tss.ex.count = 0;
			regs->pc = current->tss.ex.expc;
			regs->npc = regs->pc + 4;
			regs->u_regs[UREG_G1] = -EFAULT;
			regs->u_regs[UREG_G2] = address - current->tss.ex.address;
			regs->u_regs[UREG_G3] = current->tss.ex.pc;
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
		send_sig(SIGSEGV, tsk, 1);
		return;
	}
	if((unsigned long) address < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	} else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(KERN_ALERT " at virtual address %08lx\n",address);
	printk(KERN_ALERT "tsk->mm->context = %08lx\n",
	       (unsigned long) tsk->mm->context);
	printk(KERN_ALERT "tsk->mm->pgd = %08lx\n",
	       (unsigned long) tsk->mm->pgd);
	die_if_kernel("Oops", regs);
}

asmlinkage void do_sun4c_fault(struct pt_regs *regs, int text_fault, int write,
			       unsigned long address)
{
	extern void sun4c_update_mmu_cache(struct vm_area_struct *,unsigned long,pte_t);
	extern pgd_t *sun4c_pgd_offset(struct mm_struct *,unsigned long);
	extern pte_t *sun4c_pte_offset(pmd_t *,unsigned long);
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	pgd_t *pgd;
	pte_t *pte;

	if(text_fault)
		address = regs->pc;

	pgd = sun4c_pgd_offset(mm, address);
	pte = sun4c_pte_offset((pmd_t *) pgd, address);

	/* This conditional is 'interesting'. */
	if(pgd_val(*pgd) && !(write && !(pte_val(*pte) & _SUN4C_PAGE_WRITE))
	   && (pte_val(*pte) & _SUN4C_PAGE_VALID))
		/* XXX Very bad, can't do this optimization when VMA arg is actually
		 * XXX used by update_mmu_cache()!
		 */
		sun4c_update_mmu_cache((struct vm_area_struct *) 0, address, *pte);
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
	handle_mm_fault(vma, address, write);
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
}

void window_overflow_fault(void)
{
	unsigned long sp;

	sp = current->tss.rwbuf_stkptrs[0];
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 1);
	force_user_fault(sp, 1);
}

void window_underflow_fault(unsigned long sp)
{
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 0);
	force_user_fault(sp, 0);
}

void window_ret_fault(struct pt_regs *regs)
{
	unsigned long sp;

	sp = regs->u_regs[UREG_FP];
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 0);
	force_user_fault(sp, 0);
}

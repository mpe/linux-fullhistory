/* $Id: fault.c,v 1.2 1996/12/26 18:03:04 davem Exp $
 * arch/sparc64/mm/fault.c: Page fault handlers for the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#define ELEMENTS(arr) (sizeof (arr)/sizeof (arr[0]))

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];
extern int prom_node_root;

/* Nice, simple, prom library does all the sweating for us. ;) */
unsigned int prom_probe_memory (void)
{
	register struct linux_mlist_v0 *mlist;
	register unsigned int bytes, base_paddr, tally;
	register int i;

	i = 0;
	mlist= *prom_meminfo()->v0_available;
	bytes = tally = mlist->num_bytes;
	base_paddr = (unsigned int) mlist->start_adr;
  
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
    
		sp_banks[i].base_addr = (unsigned int) mlist->start_adr;
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
unsigned int
probe_memory(void)
{
	unsigned int total;

	total = prom_probe_memory();

	/* Oh man, much nicer, keep the dirt in promlib. */
	return total;
}

asmlinkage void do_sparc64_fault(struct pt_regs *regs, int text_fault, int write,
				 unsigned long address)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	unsigned int fixup;
	unsigned long g2;
	int from_user = !(regs->tstate & TSTATE_PRIV);

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
	/* Is this in ex_table? */
	
	g2 = regs->u_regs[UREG_G2];
	if (!from_user && (fixup = search_exception_table (regs->pc, &g2))) {
		printk("Exception: PC<%08lx> faddr<%08lx>\n", regs->pc, address);
		printk("EX_TABLE: insn<%08lx> fixup<%08x> g2<%08lx>\n",
			regs->pc, fixup, g2);
		regs->pc = fixup;
		regs->npc = regs->pc + 4;
		regs->u_regs[UREG_G2] = g2;
		return;
	}
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
	die_if_kernel("Oops", regs);
}

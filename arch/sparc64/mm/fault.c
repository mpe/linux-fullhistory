/* $Id: fault.c,v 1.4 1997/03/11 17:37:07 jj Exp $
 * arch/sparc64/mm/fault.c: Page fault handlers for the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/head.h>

#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>

#define ELEMENTS(arr) (sizeof (arr)/sizeof (arr[0]))

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

/* Nice, simple, prom library does all the sweating for us. ;) */
unsigned long prom_probe_memory (void)
{
	register struct linux_mlist_p1275 *mlist;
	register unsigned long bytes, base_paddr, tally;
	register int i;

	i = 0;
	mlist = *prom_meminfo()->p1275_available;
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
	unsigned long total;

	total = prom_probe_memory();

	/* Oh man, much nicer, keep the dirt in promlib. */
	return total;
}

void unhandled_fault(unsigned long address, struct task_struct *tsk,
                     struct pt_regs *regs)
{
	if((unsigned long) address < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL "
		       "pointer dereference");
	} else {
		printk(KERN_ALERT "Unable to handle kernel paging request "
		       "at virtual address %016lx\n", (unsigned long)address);
	}
	printk(KERN_ALERT "tsk->mm->context = %016lx\n",
	       (unsigned long) tsk->mm->context);
	printk(KERN_ALERT "tsk->mm->pgd = %016lx\n",
	       (unsigned long) tsk->mm->pgd);
	die_if_kernel("Oops", regs);
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
	case 1: insn = *(unsigned *)pc; if ((insn >> 21) & 1) return 1; break;
	/* load will be handled by fixup, store will bump out */
	/* for _from_ macros */
	case 2: insn = *(unsigned *)pc; 
		if (!((insn >> 21) & 1) || ((insn>>19)&0x3f) == 15) return 2; 
		break; 
	default: break;
	}
	memset (&regs, 0, sizeof (regs));
	regs.tpc = pc;
	regs.tnpc = pc + 4;
	/* FIXME: Should set up regs->tstate? */
	unhandled_fault (address, current, &regs);
	/* Not reached */
	return 0;
}

asmlinkage void do_sparc64_fault(struct pt_regs *regs, int text_fault, int write,
				 unsigned long address)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	unsigned long fixup;
	unsigned long g2;
	int from_user = !(regs->tstate & TSTATE_PRIV);

	lock_kernel ();
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
	goto out;
	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up(&mm->mmap_sem);
	/* Is this in ex_table? */
	
	g2 = regs->u_regs[UREG_G2];
	if (!from_user && (fixup = search_exception_table (regs->tpc, &g2))) {
		printk("Exception: PC<%016lx> faddr<%016lx>\n", regs->tpc, address);
		printk("EX_TABLE: insn<%016lx> fixup<%016lx> g2<%016lx>\n",
			regs->tpc, fixup, g2);
		regs->tpc = fixup;
		regs->tnpc = regs->tpc + 4;
		regs->u_regs[UREG_G2] = g2;
		goto out;
	}
	if(from_user) {
		tsk->tss.sig_address = address;
		tsk->tss.sig_desc = SUBSIG_NOMAPPING;
		send_sig(SIGSEGV, tsk, 1);
		goto out;
	}
	unhandled_fault (address, tsk, regs);
out:
	unlock_kernel();
}

/* $Id: fault.c,v 1.26 1998/11/08 11:14:03 davem Exp $
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
	lock_kernel();
	die_if_kernel("Oops", regs);
	unlock_kernel();
}

/* #define DEBUG_EXCEPTIONS */
/* #define DEBUG_LOCKUPS */

asmlinkage void do_sparc64_fault(struct pt_regs *regs, unsigned long address, int write)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
#ifdef DEBUG_LOCKUPS
	static unsigned long lastaddr, lastpc;
	static int lastwrite, lockcnt;
#endif

	down(&mm->mmap_sem);
#ifdef DEBUG_LOCKUPS
	if (regs->tpc == lastpc && address == lastaddr && write == lastwrite) {
		lockcnt++;
		if (lockcnt == 100000) {
			printk("do_sparc64_fault: possible fault loop for %016lx %s\n", address, write ? "write" : "read");
			show_regs(regs);
		}
	} else {
		lastpc = regs->tpc;
		lastaddr = address;
		lastwrite = write;
		lockcnt = 0;
	}
#endif
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
	current->mm->segments = (void *) (address & PAGE_SIZE);
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

do_kernel_fault:
	{
		unsigned long g2 = regs->u_regs[UREG_G2];

		/* Is this in ex_table? */
		if (regs->tstate & TSTATE_PRIV) {
			unsigned char asi = ASI_P;
			unsigned int insn;
			unsigned long fixup;
		
			insn = *(unsigned int *)regs->tpc;
			if ((insn & 0xc0800000) == 0xc0800000) {
				if (insn & 0x2000)
					asi = (regs->tstate >> 24);
				else
					asi = (insn >> 5);
			}
	
			/* Look in asi.h: All _S asis have LS bit set */
			if ((asi & 0x1) &&
			    (fixup = search_exception_table (regs->tpc, &g2))) {
#ifdef DEBUG_EXCEPTIONS
				printk("Exception: PC<%016lx> faddr<%016lx>\n",
				       regs->tpc, address);
				printk("EX_TABLE: insn<%016lx> fixup<%016lx> "
				       "g2<%016lx>\n", regs->tpc, fixup, g2);
#endif
				regs->tpc = fixup;
				regs->tnpc = regs->tpc + 4;
				regs->u_regs[UREG_G2] = g2;
				return;
			}
		} else {
			current->tss.sig_address = address;
			current->tss.sig_desc = SUBSIG_NOMAPPING;
			force_sig(SIGSEGV, current);
			return;
		}
		unhandled_fault (address, current, regs);
	}
	return;

do_sigbus:
	up(&mm->mmap_sem);
	current->tss.sig_address = address;
	current->tss.sig_desc = SUBSIG_MISCERROR;
	force_sig(SIGBUS, current);
	if (regs->tstate & TSTATE_PRIV)
		goto do_kernel_fault;
}

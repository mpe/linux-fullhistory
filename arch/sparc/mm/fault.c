/* $Id: fault.c,v 1.61 1996/04/12 06:52:35 davem Exp $
 * fault.c:  Page fault handlers for the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
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
#include <asm/openprom.h>
#include <asm/idprom.h>
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

	/* Now mask all bank sizes on a page boundry, it is all we can
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
	printk("REGISTER DUMP:\n");
	show_regs(regs);
	prom_halt();
}

asmlinkage void do_sparc_fault(struct pt_regs *regs, int text_fault, int write,
			       unsigned long address)
{
	struct vm_area_struct *vma;
	int from_user = !(regs->psr & PSR_PS);

#if 0
	printk("CPU[%d]: f<pid=%d,tf=%d,wr=%d,addr=%08lx",
	       smp_processor_id(), current->pid, text_fault,
	       write, address);
	printk(",pc=%08lx> ", regs->pc);
#endif

	if(text_fault)
		address = regs->pc;

	/* Now actually handle the fault.  Do kernel faults special,
	 * because on the sun4c we could have faulted trying to read
	 * the vma area of the task and without the following code
	 * we'd fault recursively until all our stack is gone. ;-(
	 */
	if(!from_user && address >= KERNBASE) {
#ifdef __SMP__
		printk("CPU[%d]: Kernel faults at addr=%08lx\n",
		       smp_processor_id(), address);
		while(1)
			;
#else
		quick_kernel_fault(address);
		return;
#endif
	}

	vma = find_vma(current, address);
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
	return;
	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	if(from_user) {
#if 0
		printk("%s [%d]: segfaults at %08lx pc=%08lx\n",
		       current->comm, current->pid, address, regs->pc);
#endif
		current->tss.sig_address = address;
		current->tss.sig_desc = SUBSIG_NOMAPPING;
		send_sig(SIGSEGV, current, 1);
		return;
	}
	if((unsigned long) address < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	} else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	printk(KERN_ALERT "current->mm->context = %08lx\n",
	       (unsigned long) current->mm->context);
	printk(KERN_ALERT "current->mm->pgd = %08lx\n",
	       (unsigned long) current->mm->pgd);
	die_if_kernel("Oops", regs);
}

/* This always deals with user addresses. */
inline void force_user_fault(unsigned long address, int write)
{
	struct vm_area_struct *vma;

	vma = find_vma(current, address);
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
	return;
bad_area:
	current->tss.sig_address = address;
	current->tss.sig_desc = SUBSIG_NOMAPPING;
	send_sig(SIGSEGV, current, 1);
	return;
}

void window_overflow_fault(void)
{
	unsigned long sp = current->tss.rwbuf_stkptrs[0];

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
	unsigned long sp = regs->u_regs[UREG_FP];

	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 0);
	force_user_fault(sp, 0);
}

/*
 *  arch/ppc/mm/fault.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Ported to PPC by Gary Thomas
 *  Modified by Cort Dougan and Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

#ifdef CONFIG_PMAC
extern void (*xmon_fault_handler)(void);
#endif

/* the linux norm for the function name is show_regs() so
   make it call dump_regs() on the mac -- Cort */
#ifdef CONFIG_PMAC 
#define show_regs dump_regs
#endif

extern void die_if_kernel(char *, struct pt_regs *, long);
void bad_page_fault(struct pt_regs *, unsigned long);
void do_page_fault(struct pt_regs *, unsigned long, unsigned long);
void print_pte(struct _PTE);

void do_page_fault(struct pt_regs *regs, unsigned long address, unsigned long error_code)     
{
	struct task_struct *tsk = current;
	extern unsigned _end[];
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;
	
	/*printk("do_page_fault() %s/%d addr %x nip %x regs %x error %x\n",
	       current->comm,current->pid,address,regs->nip,regs,error_code);*/
#ifdef CONFIG_PMAC
	if (xmon_fault_handler && regs->trap == 0x300) {
		xmon_fault_handler();
		return;
	}
#endif
	if (in_interrupt()) {
		static int complained;
		if (complained < 20) {
			++complained;
			printk("page fault in interrupt handler, addr=%lx\n",
			       address);
			show_regs(regs);
		}
	}
	if (current == NULL)
		goto bad_area;
	
do_page:
	down(&mm->mmap_sem);
	vma = find_vma(tsk->mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;
  
good_area:
	if (error_code & 0xb5700000)
		/* an error such as lwarx to I/O controller space,
		   address matching DABR, eciwx, etc. */
		goto bad_area;
	
	/* a write */
	if (error_code & 0x02000000) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	/* a read */
	} else {
		/* protection fault */
		if ( error_code & 0x08000000 )
			goto bad_area;
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}
	handle_mm_fault(current, vma, address, error_code & 0x02000000);
        up(&mm->mmap_sem);
	/*printk("do_page_fault() return %s/%d addr %x msr %x\n",
	      current->comm,current->pid,address,regs->msr);*/
	/* not needed since flush_page_to_ram() works */
#if 0
	flush_page(address);
#endif
	return;
  
bad_area:
	up(&current->mm->mmap_sem);
	bad_page_fault(regs, address);
}


void
bad_page_fault(struct pt_regs *regs, unsigned long address)
{
	extern unsigned int probingmem;
	struct task_struct *tsk = current;
	unsigned long fixup;
	
	
	/* Are we prepared to handle this fault?  */
	if ((fixup = search_exception_table(regs->nip)) != 0) {
		if ( user_mode(regs) )
			printk("Exception from user mode\n");
#if 0    
		printk(KERN_DEBUG "Exception at %lx (%lx)\n", regs->nip, fixup);
#endif    
		regs->nip = fixup;
		return;
	}

	if ( user_mode(regs) )
	{
		force_sig(SIGSEGV, tsk);
		return;
	}
  
bad_kernel_access:
	/* make sure it's not a bootup probe test */
	if ( probingmem )
	{
		probingmem = 0;
		return;
	}
	/* kernel has accessed a bad area */
	show_regs(regs);
	print_backtrace( regs->gpr[1] );
#ifdef CONFIG_PMAC
	xmon(regs);
#endif
	panic("kernel access of bad area\n pc %x address %X tsk %s/%d",
	      regs->nip,address,tsk->comm,tsk->pid);
}

unsigned long va_to_phys(unsigned long address)
{
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;
	
	dir = pgd_offset(current->mm, address & PAGE_MASK);
	if (dir)
	{
		pmd = pmd_offset(dir, address & PAGE_MASK);
		if (pmd && pmd_present(*pmd))
		{
			pte = pte_offset(pmd, address & PAGE_MASK);
			if (pte && pte_present(*pte))
			{
				return(pte_page(*pte) | (address & ~(PAGE_MASK-1)));
			}
		} else
		{
			return (0);
		}
	} else
	{
		return (0);
	}
	return (0);
}

void print_pte(struct _PTE p)
{
	printk(
"%08x %08x vsid: %06x h: %01x api: %02x rpn: %05x rcwimg: %d%d%d%d%d%d pp: %02x\n",
		*((unsigned long *)(&p)), *((long *)&p+1),
		p.vsid, p.h, p.api, p.rpn,
		p.r,p.c,p.w,p.i,p.m,p.g,p.pp);
}

/*
 * Search the hw hash table for a mapping to the given physical
 * address. -- Cort
 */
unsigned long htab_phys_to_va(unsigned long address)
{
	extern PTE *Hash, *Hash_end;
	PTE *ptr;

	for ( ptr = Hash ; ptr < Hash_end ; ptr++ )
	{
		if ( ptr->rpn == (address>>12) )
			printk("phys %08X -> va ???\n",
			       address);
	}
}

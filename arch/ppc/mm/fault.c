/*
 *  arch/ppc/mm/fault.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
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
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
extern void (*debugger)(struct pt_regs *);
extern void (*debugger_fault_handler)(struct pt_regs *);
extern int (*debugger_dabr_match)(struct pt_regs *);
int debugger_kernel_faults = 1;
#endif

unsigned long htab_reloads = 0; /* updated by head.S:hash_page() */
unsigned long htab_evicts = 0;  /* updated by head.S:hash_page() */
unsigned long pte_misses = 0; /* updated by do_page_fault() */
unsigned long pte_errors = 0; /* updated by do_page_fault() */
unsigned int probingmem = 0;

extern void die_if_kernel(char *, struct pt_regs *, long);
void bad_page_fault(struct pt_regs *, unsigned long);
void do_page_fault(struct pt_regs *, unsigned long, unsigned long);

/*
 * The error_code parameter is DSISR for a data fault, SRR1 for
 * an instruction fault.
 */
void do_page_fault(struct pt_regs *regs, unsigned long address,
		   unsigned long error_code)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;

	/*printk("address: %08lx nip:%08lx code: %08lx %s%s%s%s%s%s\n",
	       address,regs->nip,error_code,
	       (error_code&0x40000000)?"604 tlb&htab miss ":"",
	       (error_code&0x20000000)?"603 tlbmiss ":"",
	       (error_code&0x02000000)?"write ":"",
	       (error_code&0x08000000)?"prot ":"",
	       (error_code&0x80000000)?"I/O ":"",
	       (regs->trap == 0x400)?"instr":"data"
	       );*/
	       
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_fault_handler && regs->trap == 0x300) {
		debugger_fault_handler(regs);
		return;
	}
	if (error_code & 0x00400000) {
		/* DABR match */
		if (debugger_dabr_match(regs))
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
			instruction_dump((unsigned long *)regs->nip);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
			if (debugger_kernel_faults)
				debugger(regs);
#endif
		}
	}
	if (current == NULL) {
		bad_page_fault(regs, address);
		return;
	}
	down(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;

good_area:
#ifdef CONFIG_6xx
	if (error_code & 0x95700000)
		/* an error such as lwarx to I/O controller space,
		   address matching DABR, eciwx, etc. */
#endif /* CONFIG_6xx */
#ifdef CONFIG_8xx
        /* The MPC8xx seems to always set 0x80000000, which is
         * "undefined".  Of those that can be set, this is the only
         * one which seems bad.
         */
	if (error_code & 0x10000000)
                /* Guarded storage error. */
#endif /* CONFIG_8xx */
		goto bad_area;
	
	
	/* a write */
	if (error_code & 0x02000000) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	/* a read */
	} else {
		/* protection fault */
		if (error_code & 0x08000000)
			goto bad_area;
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}
	handle_mm_fault(current, vma, address, error_code & 0x02000000);
	up(&mm->mmap_sem);
	/*
	 * keep track of tlb+htab misses that are good addrs but
	 * just need pte's created via handle_mm_fault()
	 * -- Cort
	 */
	pte_misses++;
	return;

bad_area:

	up(&mm->mmap_sem);
	pte_errors++;	
	bad_page_fault(regs, address);
}

void
bad_page_fault(struct pt_regs *regs, unsigned long address)
{
	unsigned long fixup;
	if (user_mode(regs)) {
		force_sig(SIGSEGV, current);
		return;
	}
	
	/* Are we prepared to handle this fault?  */
	if ((fixup = search_exception_table(regs->nip)) != 0) {
		regs->nip = fixup;
		return;
	}

	/* kernel has accessed a bad area */
	show_regs(regs);
	print_backtrace( (unsigned long *)regs->gpr[1] );
	instruction_dump((unsigned long *)regs->nip);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_kernel_faults)
		debugger(regs);
#endif
	panic("kernel access of bad area pc %lx lr %lx address %lX tsk %s/%d",
	      regs->nip,regs->link,address,current->comm,current->pid);
}

#ifdef CONFIG_8xx
/*
 * I need a va to pte function for the MPC8xx so I can set the cache
 * attributes on individual pages used by the Communication Processor
 * Module.
 */
pte_t *va_to_pte(struct task_struct *tsk, unsigned long address)
{
        pgd_t *dir;
        pmd_t *pmd;
        pte_t *pte;
        
        dir = pgd_offset(tsk->mm, address & PAGE_MASK);
        if (dir)
        {
                pmd = pmd_offset(dir, address & PAGE_MASK);
                if (pmd && pmd_present(*pmd))
                {
                        pte = pte_offset(pmd, address & PAGE_MASK);
                        if (pte && pte_present(*pte))
                        {
                                return(pte);
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

void
print_8xx_pte(struct mm_struct *mm, unsigned long addr)
{
        pgd_t * pgd;
        pmd_t * pmd;
        pte_t * pte;

        printk(" pte @ 0x%8lx: ", addr);
        pgd = pgd_offset(mm, addr & PAGE_MASK);
        if (pgd) {
                pmd = pmd_offset(pgd, addr & PAGE_MASK);
                if (pmd && pmd_present(*pmd)) {
                        pte = pte_offset(pmd, addr & PAGE_MASK);
                        if (pte) {
                                printk(" (0x%08lx)->(0x%08lx)->0x%08lx\n",
                                        (long)pgd, (long)pte, (long)pte_val(*pte));
#define pp ((long)pte_val(*pte))				
				printk(" RPN: %05x PP: %x SPS: %x SH: %x "
				       "CI: %x v: %x\n",
				       pp>>12,    /* rpn */
				       (pp>>10)&3, /* pp */
				       (pp>>3)&1, /* small */
				       (pp>>2)&1, /* shared */
				       (pp>>1)&1, /* cache inhibit */
				       pp&1       /* valid */
				       );
#undef pp				
                        }
                        else {
                                printk("no pte\n");
                        }
                }
                else {
                        printk("no pmd\n");
                }
        }
        else {
                printk("no pgd\n");
        }
}

int
get_8xx_pte(struct mm_struct *mm, unsigned long addr)
{
        pgd_t * pgd;
        pmd_t * pmd;
        pte_t * pte;
        int     retval = 0;

        pgd = pgd_offset(mm, addr & PAGE_MASK);
        if (pgd) {
                pmd = pmd_offset(pgd, addr & PAGE_MASK);
                if (pmd && pmd_present(*pmd)) {
                        pte = pte_offset(pmd, addr & PAGE_MASK);
                        if (pte) {
                                        retval = (int)pte_val(*pte);
                        }
                }
        }
        return(retval);
}
#endif /* CONFIG_8xx */

#if 0
/*
 * Misc debugging functions.  Please leave them here. -- Cort
 */
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
			printk("phys %08lX -> va ???\n",
			       address);
	}
}
#endif

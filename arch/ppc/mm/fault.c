/*
 *  ARCH/ppc/mm/fault.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Ported to PPC by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu) 
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

#include <asm/page.h>
#include <asm/pgtable.h>

extern void die_if_kernel(char *, struct pt_regs *, long);
extern void do_page_fault(struct pt_regs *, unsigned long, unsigned long);
void new_page_fault(unsigned long address, unsigned long code, unsigned long text,
		    struct pt_regs *regs);


#undef SHOW_FAULTS
#undef NOISY_INSTRFAULT
#undef NOISY_DATAFAULT

unsigned int probingmem = 0;
#define NEWMM 1

void new_page_fault(unsigned long address, unsigned long ppc_code,
		    unsigned long text, struct pt_regs *regs)
{
  struct vm_area_struct * vma;
  struct mm_struct *mm = current->mm;

  int intel_code = 0;
  pgd_t *dir;
  pmd_t *pmd;
  pte_t *pte;
  
  /*
   *	bit 0 == 0 means no page found, 1 means protection fault
   *	bit 1 == 0 means read, 1 means write
   *	bit 2 == 0 means kernel, 1 means user-mode
   */
  if (user_mode(regs)) intel_code |= 0x04;
  if (!text && (ppc_code & 0x02000000)) intel_code |= 0x02;  /* Load/store */
  if (!text && (ppc_code & 0x08000000))
  {
    intel_code |= 0x01;  /* prot viol */
    goto do_page;
  }
  
  dir = pgd_offset(mm, address & PAGE_MASK);
  if (dir)
  {
    pmd = pmd_offset(dir, address & PAGE_MASK);
    if (pmd && pmd_present(*pmd))
    {
      pte = pte_offset(pmd, address & PAGE_MASK);
      if (pte && pte_present(*pte))
      {
	MMU_hash_page(&current->tss, address & PAGE_MASK, pte);
	return;
      }
    }
  }

do_page:
  down(&mm->mmap_sem);
  vma = find_vma(current->mm, address);
  if (!vma)
    goto bad_area;
  if (vma->vm_start <= address)
    goto good_area;
  if (!(vma->vm_flags & VM_GROWSDOWN))
    goto bad_area;
  if (expand_stack(vma, address))
    goto bad_area;
  
good_area:
  /* a write */
  if (intel_code & 2) {
    if (!(vma->vm_flags & VM_WRITE))
    {
      goto bad_area;
    }
  /* a read */
  } else {
    /* protection fault */
    if (intel_code & 1)
    {
      printk("prot fault\n");
      goto bad_area;
    }
    if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
    {
      printk("no read or exec\n");      
      goto bad_area; 
    }
  }
  handle_mm_fault(vma, address, intel_code & 2);
  up(&mm->mmap_sem);  flush_page(address);  /* Flush & Invalidate cache - note: address is OK now */  
  return;
  
bad_area:
  up(&mm->mmap_sem);

  /* Did we have an exception handler installed? */
  if(current->tss.excount != 0) {
    if(user_mode(regs)) {
      printk("Exception signalled from user mode!\n");
    } else {
#if 0      
      printk("Exception from kernel mode. pc %x expc %x count %d\n",
	     regs->nip,current->tss.expc,current->tss.excount);
#endif      
      current->tss.excount = 0;
      regs->gpr[3] = -EFAULT;
      regs->nip = current->tss.expc;
      return;
    }
  }
  
  if (user_mode(regs))
  {
    force_sig(SIGSEGV, current);
    return;
  }
  panic("KERNEL access of bad area PC %x address %x vm_flags %x\n",
	regs->nip,address,vma->vm_flags);
}

va_to_phys(unsigned long address)
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

inline void
update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t _pte)
{
   MMU_hash_page(&current->tss, address & PAGE_MASK, (pte *)&_pte);
}

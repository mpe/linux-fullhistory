/*
 *  ARCH/ppc/mm/fault.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Ported to PPC by Gary Thomas
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

#undef SHOW_FAULTS
#undef NOISY_INSTRFAULT
#undef NOISY_DATAFAULT

#define NEWMM 1

void
DataAccessException(struct pt_regs *regs)
{
  pgd_t *dir;
  pmd_t *pmd;
  pte_t *pte;
  int tries, mode = 0;
  
#ifdef NOISY_DATAFAULT
  printk("Data fault on %x\n",regs->dar);
#endif
  
  if (user_mode(regs)) mode |= 0x04;
  if (regs->dsisr & 0x02000000) mode |= 0x02;  /* Load/store */
  if (regs->dsisr & 0x08000000) mode |= 0x01;  /* Protection violation */
  if (mode & 0x01)
  {
#ifdef NOISY_DATAFAULT
    printk("Write Protect fault\n ");
#endif
    do_page_fault(regs, regs->dar, mode);
#ifdef NOISY_DATAFAULT    
    printk("Write Protect fault handled\n");
#endif
    return;
  }

    
  for (tries = 0;  tries < 1;  tries++)
  {
    dir = pgd_offset(current->mm, regs->dar & PAGE_MASK);
    if (dir)
    {
      pmd = pmd_offset(dir, regs->dar & PAGE_MASK);
      if (pmd && pmd_present(*pmd))
      {
	pte = pte_offset(pmd, regs->dar & PAGE_MASK);
	if (pte && pte_present(*pte))
	{
#if NOISY_DATAFAULT
	  printk("Page mapped - PTE: %x[%x]\n", pte, *(long *)pte);
#endif
	  MMU_hash_page(&current->tss, regs->dar & PAGE_MASK, pte);
	  /*MMU_hash_page2(current->mm, regs->dar & PAGE_MASK, pte);*/
	  return;
	}
      }
    } else
    {
#if NOISY_DATAFAULT
      printk("No PGD\n");      
#endif 
    }
    do_page_fault(regs, regs->dar, mode);
  }
}

void
InstructionAccessException(struct pt_regs *regs)
{
  pgd_t *dir;
  pmd_t *pmd;
  pte_t *pte;
  int tries, mode = 0;
  
#if NOISY_INSTRFAULT
  printk("Instr fault on %x\n",regs->dar);
#endif

#ifdef NEWMM
  if (!user_mode(regs))
  {
    
    panic("InstructionAcessException in kernel mode. PC %x addr %x",
	  regs->nip, regs->dar);
  }
#endif
  if (user_mode(regs)) mode |= 0x04;
  if (regs->dsisr & 0x02000000) mode |= 0x02;  /* Load/store */
  if (regs->dsisr & 0x08000000) mode |= 0x01;  /* Protection violation */
  
  if (mode & 0x01)
  {
    do_page_fault(regs, regs->dar, mode); 
    return;
  }
  for (tries = 0;  tries < 1;  tries++)
  {
    dir = pgd_offset(current->mm, regs->dar & PAGE_MASK);
    if (dir)
    {
      pmd = pmd_offset(dir, regs->dar & PAGE_MASK); 
      if (pmd && pmd_present(*pmd))
      {
 	pte = pte_offset(pmd, regs->dar & PAGE_MASK); 
	if (pte && pte_present(*pte))
	  {
	    
	    MMU_hash_page(&current->tss, regs->dar & PAGE_MASK, pte); 
	    /*	  MMU_hash_page2(current->mm, regs->dar & PAGE_MASK, pte);*/
	    return;
	  }
      }
    } else
      {
      }
    do_page_fault(regs, regs->dar, mode);
  }
}


/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * The error_code parameter just the same as in the i386 version:
 *
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *	bit 2 == 0 means kernel, 1 means user-mode
 */
void do_page_fault(struct pt_regs *regs, unsigned long address, unsigned long error_code)
{
  struct vm_area_struct * vma;
  unsigned long page;

  vma = find_vma(current, address);
  if (!vma)
  {
    goto bad_area;
  }

  if (vma->vm_start <= address){
    goto good_area;
  }
  if (!(vma->vm_flags & VM_GROWSDOWN))
  {
    goto bad_area;
  }
  if (vma->vm_end - address > current->rlim[RLIMIT_STACK].rlim_cur)
  {
    goto bad_area;
  }
  vma->vm_offset -= vma->vm_start - (address & PAGE_MASK);

  vma->vm_start = (address & PAGE_MASK);
  
good_area:
  /* a write */
  if (error_code & 2) {
    if (!(vma->vm_flags & VM_WRITE))
    {
      goto bad_area;
    }
  /* a read */
  } else {
    /* protection fault */
    if (error_code & 1)
    {
      goto bad_area;
    }
    if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
    {
      goto bad_area; 
    }
  }
  handle_mm_fault(vma, address, error_code & 2);
  flush_page(address);  /* Flush & Invalidate cache - note: address is OK now */  
  return;
  
bad_area:
  if (user_mode(regs))
  {
/*    printk("Bad User Area: Addr %x PC %x Task %x pid %d %s\n",
	   address,regs->nip, current,current->pid,current->comm);*/
    send_sig(SIGSEGV, current, 1);
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


/*
 * See if an address should be valid in the current context.
 */
valid_addr(unsigned long addr)
{
	struct vm_area_struct * vma;
	for (vma = current->mm->mmap ; ; vma = vma->vm_next)
	{
		if (!vma)
		{
			return (0);
		}
		if (vma->vm_end > addr)
			break;
	}
	if (vma->vm_start <= addr)
	{
		return (1);
	}
	return (0);
}

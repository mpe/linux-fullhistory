/* $Id: fault.c,v 1.40 1999/12/01 10:44:53 davem Exp $
 * arch/sparc64/mm/fault.c: Page fault handlers for the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#include <asm/head.h>

#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>

#define ELEMENTS(arr) (sizeof (arr)/sizeof (arr[0]))

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

/* Nice, simple, prom library does all the sweating for us. ;) */
unsigned long __init prom_probe_memory (void)
{
	register struct linux_mlist_p1275 *mlist;
	register unsigned long bytes, base_paddr, tally;
	register int i;

	i = 0;
	mlist = *prom_meminfo()->p1275_available;
	bytes = tally = mlist->num_bytes;
	base_paddr = mlist->start_adr;
  
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
    
		sp_banks[i].base_addr = mlist->start_adr;
		sp_banks[i].num_bytes = mlist->num_bytes;
	}

	i++;
	sp_banks[i].base_addr = 0xdeadbeefbeefdeadUL;
	sp_banks[i].num_bytes = 0;

	/* Now mask all bank sizes on a page boundary, it is all we can
	 * use anyways.
	 */
	for(i=0; sp_banks[i].num_bytes != 0; i++)
		sp_banks[i].num_bytes &= PAGE_MASK;

	return tally;
}

void unhandled_fault(unsigned long address, struct task_struct *tsk,
                     struct pt_regs *regs)
{
	if((unsigned long) address < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL "
		       "pointer dereference\n");
	} else {
		printk(KERN_ALERT "Unable to handle kernel paging request "
		       "at virtual address %016lx\n", (unsigned long)address);
	}
	printk(KERN_ALERT "tsk->{mm,active_mm}->context = %016lx\n",
	       (tsk->mm ? tsk->mm->context : tsk->active_mm->context));
	printk(KERN_ALERT "tsk->{mm,active_mm}->pgd = %016lx\n",
	       (tsk->mm ? (unsigned long) tsk->mm->pgd :
		          (unsigned long) tsk->active_mm->pgd));
	die_if_kernel("Oops", regs);
}

/* #define DEBUG_EXCEPTIONS */
/* #define DEBUG_LOCKUPS */

/* #define INSN_VPTE_LOOKUP */

static inline u32 get_user_insn(unsigned long tpc)
{
	u32 insn;
#ifndef INSN_VPTE_LOOKUP
	pgd_t *pgdp = pgd_offset(current->mm, tpc);
	pmd_t *pmdp;
	pte_t *ptep;

	if(pgd_none(*pgdp))
		return 0;
	pmdp = pmd_offset(pgdp, tpc);
	if(pmd_none(*pmdp))
		return 0;
	ptep = pte_offset(pmdp, tpc);
	if(!pte_present(*ptep))
		return 0;
	insn = *(unsigned int *)
		((unsigned long)__va(pte_pagenr(*ptep) << PAGE_SHIFT) +
		 (tpc & ~PAGE_MASK));
#else
	register unsigned long pte asm("l1");

	/* So that we don't pollute TLB, we read the instruction
	 * using PHYS bypass. For that, we of course need
	 * to know its page table entry. Do this by simulating
	 * dtlb_miss handler. -jj */
	pte = ((((long)tpc) >> (PAGE_SHIFT-3)) & ~7);
	asm volatile ("
		rdpr    %%pstate, %%l0
		wrpr    %%l0, %2, %%pstate
		wrpr    %%g0, 1, %%tl
		mov	%%l1, %%g6
		ldxa    [%%g3 + %%l1] %3, %%g5
		mov     %%g5, %%l1
		wrpr    %%g0, 0, %%tl
		wrpr    %%l0, 0, %%pstate
	" : "=r" (pte) : "0" (pte), "i" (PSTATE_MG|PSTATE_IE), "i" (ASI_S) : "l0");
			
	if ((long)pte >= 0) return 0;

	pte = (pte & _PAGE_PADDR) + (tpc & ~PAGE_MASK);
	asm ("lduwa	[%1] %2, %0" : "=r" (insn) : "r" (pte), "i" (ASI_PHYS_USE_EC));
#endif

	return insn;
}

asmlinkage void do_sparc64_fault(struct pt_regs *regs, unsigned long address, int write)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned int insn = 0;
#ifdef DEBUG_LOCKUPS
	static unsigned long lastaddr, lastpc;
	static int lastwrite, lockcnt;
#endif
	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_interrupt() || !mm)
		goto do_kernel_fault;

	down(&mm->mmap_sem);
#ifdef DEBUG_LOCKUPS
	if (regs->tpc == lastpc &&
	    address == lastaddr &&
	    write == lastwrite) {
		lockcnt++;
		if (lockcnt == 100000) {
			unsigned char tmp;
			register unsigned long tmp1 asm("o5");
			register unsigned long tmp2 asm("o4");

			printk("do_sparc64_fault[%s:%d]: possible fault loop for %016lx %s\n",
			       current->comm, current->pid,
			       address, write ? "write" : "read");
			printk("do_sparc64_fault: CHECK[papgd[%016lx],pcac[%016lx]]\n",
			       __pa(mm->pgd), pgd_val(mm->pgd[0])<<11UL);
			__asm__ __volatile__(
				"wrpr	%%g0, 0x494, %%pstate\n\t"
				"mov	%3, %%g4\n\t"
				"mov	%%g7, %0\n\t"
				"ldxa	[%%g4] %2, %1\n\t"
				"wrpr	%%g0, 0x096, %%pstate"
				: "=r" (tmp1), "=r" (tmp2)
				: "i" (ASI_DMMU), "i" (TSB_REG));
			printk("do_sparc64_fault:    IS[papgd[%016lx],pcac[%016lx]]\n",
			       tmp1, tmp2);
			printk("do_sparc64_fault: CHECK[ctx(%016lx)] IS[ctx(%016lx)]\n",
			       mm->context, spitfire_get_secondary_context());
			__asm__ __volatile__("rd	%%asi, %0"
					     : "=r" (tmp));
			printk("do_sparc64_fault: CHECK[seg(%02x)] IS[seg(%02x)]\n",
			       current->thread.current_ds.seg, tmp);
			show_regs(regs);
			__sti();
			while(1)
				barrier();
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
#ifndef INSN_VPTE_LOOKUP
	write &= 0xf;
#else
	if (write & 0x10) {
		write = 0;
		if((vma->vm_flags & VM_WRITE)) {
			if (regs->tstate & TSTATE_PRIV)
				insn = *(unsigned int *)regs->tpc;
			else
				insn = get_user_insn(regs->tpc);
			if ((insn & 0xc0200000) == 0xc0200000 && (insn & 0x1780000) != 0x1680000)
				write = 1;
		}
	}
#endif
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
		unsigned long g2;
		unsigned char asi = ASI_P;
		
		if (!insn) {
			if (regs->tstate & TSTATE_PRIV)
				insn = *(unsigned int *)regs->tpc;
			else
				insn = get_user_insn(regs->tpc);
		}
		if (write != 1 && (insn & 0xc0800000) == 0xc0800000) {
			if (insn & 0x2000)
				asi = (regs->tstate >> 24);
			else
				asi = (insn >> 5);
			if ((asi & 0xf2) == 0x82) {
				/* This was a non-faulting load. Just clear the
				   destination register(s) and continue with the next
				   instruction. -jj */
				if (insn & 0x1000000) {
					extern int handle_ldf_stq(u32, struct pt_regs *);
					
					handle_ldf_stq(insn, regs);
				} else {
					extern int handle_ld_nf(u32, struct pt_regs *);
					
					handle_ld_nf(insn, regs);
				}
				return;
			}
		}
		
		g2 = regs->u_regs[UREG_G2];

		/* Is this in ex_table? */
		if (regs->tstate & TSTATE_PRIV) {
			unsigned long fixup;

			if (asi == ASI_P && (insn & 0xc0800000) == 0xc0800000) {
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
#if 0
			extern void __show_regs(struct pt_regs *);
			printk("SHIT(%s:%d:cpu(%d)): PC[%016lx] ADDR[%016lx]\n",
			       current->comm, current->pid, smp_processor_id(),
			       regs->tpc, address);
			__show_regs(regs);
			__sti();
			while(1)
				barrier();
#endif
			current->thread.sig_address = address;
			current->thread.sig_desc = SUBSIG_NOMAPPING;
			force_sig(SIGSEGV, current);
			return;
		}
		unhandled_fault (address, current, regs);
	}
	return;

do_sigbus:
	up(&mm->mmap_sem);
	current->thread.sig_address = address;
	current->thread.sig_desc = SUBSIG_MISCERROR;
	force_sig(SIGBUS, current);
	if (regs->tstate & TSTATE_PRIV)
		goto do_kernel_fault;
}

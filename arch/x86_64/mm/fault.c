/*
 *  linux/arch/x86-64/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2001,2002 Andi Kleen, SuSE Labs.
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
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>		/* For unblank_screen() */
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/kprobes.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>
#include <asm/proto.h>
#include <asm/kdebug.h>
#include <asm-generic/sections.h>
#include <asm/kdebug.h>

void bust_spinlocks(int yes)
{
	int loglevel_save = console_loglevel;
	if (yes) {
		oops_in_progress = 1;
	} else {
#ifdef CONFIG_VT
		unblank_screen();
#endif
		oops_in_progress = 0;
		/*
		 * OK, the message is on the console.  Now we call printk()
		 * without oops_in_progress set so that printk will give klogd
		 * a poke.  Hold onto your hats...
		 */
		console_loglevel = 15;		/* NMI oopser may have shut the console up */
		printk(" ");
		console_loglevel = loglevel_save;
	}
}

/* Sometimes the CPU reports invalid exceptions on prefetch.
   Check that here and ignore.
   Opcode checker based on code by Richard Brunner */
static noinline int is_prefetch(struct pt_regs *regs, unsigned long addr,
				unsigned long error_code)
{ 
	unsigned char *instr = (unsigned char *)(regs->rip);
	int scan_more = 1;
	int prefetch = 0; 
	unsigned char *max_instr = instr + 15;

	/* If it was a exec fault ignore */
	if (error_code & (1<<4))
		return 0;
	
	/* Code segments in LDT could have a non zero base. Don't check
	   when that's possible */
	if (regs->cs & (1<<2))
		return 0;

	if ((regs->cs & 3) != 0 && regs->rip >= TASK_SIZE)
		return 0;

	while (scan_more && instr < max_instr) { 
		unsigned char opcode;
		unsigned char instr_hi;
		unsigned char instr_lo;

		if (__get_user(opcode, instr))
			break; 

		instr_hi = opcode & 0xf0; 
		instr_lo = opcode & 0x0f; 
		instr++;

		switch (instr_hi) { 
		case 0x20:
		case 0x30:
			/* Values 0x26,0x2E,0x36,0x3E are valid x86
			   prefixes.  In long mode, the CPU will signal
			   invalid opcode if some of these prefixes are
			   present so we will never get here anyway */
			scan_more = ((instr_lo & 7) == 0x6);
			break;
			
		case 0x40:
			/* In AMD64 long mode, 0x40 to 0x4F are valid REX prefixes
			   Need to figure out under what instruction mode the
			   instruction was issued ... */
			/* Could check the LDT for lm, but for now it's good
			   enough to assume that long mode only uses well known
			   segments or kernel. */
			scan_more = ((regs->cs & 3) == 0) || (regs->cs == __USER_CS);
			break;
			
		case 0x60:
			/* 0x64 thru 0x67 are valid prefixes in all modes. */
			scan_more = (instr_lo & 0xC) == 0x4;
			break;		
		case 0xF0:
			/* 0xF0, 0xF2, and 0xF3 are valid prefixes in all modes. */
			scan_more = !instr_lo || (instr_lo>>1) == 1;
			break;			
		case 0x00:
			/* Prefetch instruction is 0x0F0D or 0x0F18 */
			scan_more = 0;
			if (__get_user(opcode, instr)) 
				break;
			prefetch = (instr_lo == 0xF) &&
				(opcode == 0x0D || opcode == 0x18);
			break;			
		default:
			scan_more = 0;
			break;
		} 
	}
	return prefetch;
}

static int bad_address(void *p) 
{ 
	unsigned long dummy;
	return __get_user(dummy, (unsigned long *)p);
} 

void dump_pagetable(unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	asm("movq %%cr3,%0" : "=r" (pgd));

	pgd = __va((unsigned long)pgd & PHYSICAL_PAGE_MASK); 
	pgd += pgd_index(address);
	printk("PGD %lx ", pgd_val(*pgd));
	if (bad_address(pgd)) goto bad;
	if (!pgd_present(*pgd)) goto ret; 

	pud = __pud_offset_k((pud_t *)pgd_page(*pgd), address);
	if (bad_address(pud)) goto bad;
	printk("PUD %lx ", pud_val(*pud));
	if (!pud_present(*pud))	goto ret;

	pmd = pmd_offset(pud, address);
	if (bad_address(pmd)) goto bad;
	printk("PMD %lx ", pmd_val(*pmd));
	if (!pmd_present(*pmd))	goto ret;	 

	pte = pte_offset_kernel(pmd, address);
	if (bad_address(pte)) goto bad;
	printk("PTE %lx", pte_val(*pte)); 
ret:
	printk("\n");
	return;
bad:
	printk("BAD\n");
}

static const char errata93_warning[] = 
KERN_ERR "******* Your BIOS seems to not contain a fix for K8 errata #93\n"
KERN_ERR "******* Working around it, but it may cause SEGVs or burn power.\n"
KERN_ERR "******* Please consider a BIOS update.\n"
KERN_ERR "******* Disabling USB legacy in the BIOS may also help.\n";

/* Workaround for K8 erratum #93 & buggy BIOS.
   BIOS SMM functions are required to use a specific workaround
   to avoid corruption of the 64bit RIP register on C stepping K8. 
   A lot of BIOS that didn't get tested properly miss this. 
   The OS sees this as a page fault with the upper 32bits of RIP cleared.
   Try to work around it here.
   Note we only handle faults in kernel here. */

static int is_errata93(struct pt_regs *regs, unsigned long address) 
{
	static int warned;
	if (address != regs->rip)
		return 0;
	if ((address >> 32) != 0) 
		return 0;
	address |= 0xffffffffUL << 32;
	if ((address >= (u64)_stext && address <= (u64)_etext) || 
	    (address >= MODULES_VADDR && address <= MODULES_END)) { 
		if (!warned) {
			printk(errata93_warning); 		
			warned = 1;
		}
		regs->rip = address;
		return 1;
	}
	return 0;
} 

int unhandled_signal(struct task_struct *tsk, int sig)
{
	if (tsk->pid == 1)
		return 1;
	/* Warn for strace, but not for gdb */
	if (!test_ti_thread_flag(tsk->thread_info, TIF_SYSCALL_TRACE) &&
	    (tsk->ptrace & PT_PTRACED))
		return 0;
	return (tsk->sighand->action[sig-1].sa.sa_handler == SIG_IGN) ||
		(tsk->sighand->action[sig-1].sa.sa_handler == SIG_DFL);
}

static noinline void pgtable_bad(unsigned long address, struct pt_regs *regs,
				 unsigned long error_code)
{
	oops_begin();
	printk(KERN_ALERT "%s: Corrupted page table at address %lx\n",
	       current->comm, address);
	dump_pagetable(address);
	__die("Bad pagetable", regs, error_code);
	oops_end();
	do_exit(SIGKILL);
}

/*
 * Handle a fault on the vmalloc or module mapping area
 */
static int vmalloc_fault(unsigned long address)
{
	pgd_t *pgd, *pgd_ref;
	pud_t *pud, *pud_ref;
	pmd_t *pmd, *pmd_ref;
	pte_t *pte, *pte_ref;

	/* Copy kernel mappings over when needed. This can also
	   happen within a race in page table update. In the later
	   case just flush. */

	pgd = pgd_offset(current->mm ?: &init_mm, address);
	pgd_ref = pgd_offset_k(address);
	if (pgd_none(*pgd_ref))
		return -1;
	if (pgd_none(*pgd))
		set_pgd(pgd, *pgd_ref);

	/* Below here mismatches are bugs because these lower tables
	   are shared */

	pud = pud_offset(pgd, address);
	pud_ref = pud_offset(pgd_ref, address);
	if (pud_none(*pud_ref))
		return -1;
	if (pud_none(*pud) || pud_page(*pud) != pud_page(*pud_ref))
		BUG();
	pmd = pmd_offset(pud, address);
	pmd_ref = pmd_offset(pud_ref, address);
	if (pmd_none(*pmd_ref))
		return -1;
	if (pmd_none(*pmd) || pmd_page(*pmd) != pmd_page(*pmd_ref))
		BUG();
	pte_ref = pte_offset_kernel(pmd_ref, address);
	if (!pte_present(*pte_ref))
		return -1;
	pte = pte_offset_kernel(pmd, address);
	if (!pte_present(*pte) || pte_page(*pte) != pte_page(*pte_ref))
		BUG();
	__flush_tlb_all();
	return 0;
}

int page_fault_trace = 0;
int exception_trace = 1;

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * error_code:
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *	bit 2 == 0 means kernel, 1 means user-mode
 *      bit 3 == 1 means fault was an instruction fetch
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	unsigned long address;
	const struct exception_table_entry *fixup;
	int write;
	siginfo_t info;

#ifdef CONFIG_CHECKING
	{ 
		unsigned long gs; 
		struct x8664_pda *pda = cpu_pda + stack_smp_processor_id(); 
		rdmsrl(MSR_GS_BASE, gs); 
		if (gs != (unsigned long)pda) { 
			wrmsrl(MSR_GS_BASE, pda); 
			printk("page_fault: wrong gs %lx expected %p\n", gs, pda);
		}
	}
#endif

	/* get the address */
	__asm__("movq %%cr2,%0":"=r" (address));
	if (notify_die(DIE_PAGE_FAULT, "page fault", regs, error_code, 14,
					SIGSEGV) == NOTIFY_STOP)
		return;

	if (likely(regs->eflags & X86_EFLAGS_IF))
		local_irq_enable();

	if (unlikely(page_fault_trace))
		printk("pagefault rip:%lx rsp:%lx cs:%lu ss:%lu address %lx error %lx\n",
		       regs->rip,regs->rsp,regs->cs,regs->ss,address,error_code); 

	tsk = current;
	mm = tsk->mm;
	info.si_code = SEGV_MAPERR;


	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 *
	 * This verifies that the fault happens in kernel space
	 * (error_code & 4) == 0, and that the fault was not a
	 * protection error (error_code & 1) == 0.
	 */
	if (unlikely(address >= TASK_SIZE)) {
		if (!(error_code & 5)) {
			if (vmalloc_fault(address) < 0)
				goto bad_area_nosemaphore;
			return;
		}
		/*
		 * Don't take the mm semaphore here. If we fixup a prefetch
		 * fault we could otherwise deadlock.
		 */
		goto bad_area_nosemaphore;
	}

	if (unlikely(error_code & (1 << 3)))
		pgtable_bad(address, regs, error_code);

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (unlikely(in_atomic() || !mm))
		goto bad_area_nosemaphore;

 again:
	/* When running in the kernel we expect faults to occur only to
	 * addresses in user space.  All other faults represent errors in the
	 * kernel and should generate an OOPS.  Unfortunatly, in the case of an
	 * erroneous fault occuring in a code path which already holds mmap_sem
	 * we will deadlock attempting to validate the fault against the
	 * address space.  Luckily the kernel only validly references user
	 * space from well defined areas of code, which are listed in the
	 * exceptions table.
	 *
	 * As the vast majority of faults will be valid we will only perform
	 * the source reference check when there is a possibilty of a deadlock.
	 * Attempt to lock the address space, if we cannot we then validate the
	 * source.  If this is invalid we can skip the address space check,
	 * thus avoiding the deadlock.
	 */
	if (!down_read_trylock(&mm->mmap_sem)) {
		if ((error_code & 4) == 0 &&
		    !search_exception_tables(regs->rip))
			goto bad_area_nosemaphore;
		down_read(&mm->mmap_sem);
	}

	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (likely(vma->vm_start <= address))
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (error_code & 4) {
		// XXX: align red zone size with ABI 
		if (address + 128 < regs->rsp)
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	info.si_code = SEGV_ACCERR;
	write = 0;
	switch (error_code & 3) {
		default:	/* 3: write, present */
			/* fall through */
		case 2:		/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto bad_area;
			write++;
			break;
		case 1:		/* read, present */
			goto bad_area;
		case 0:		/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
				goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	switch (handle_mm_fault(mm, vma, address, write)) {
	case 1:
		tsk->min_flt++;
		break;
	case 2:
		tsk->maj_flt++;
		break;
	case 0:
		goto do_sigbus;
	default:
		goto out_of_memory;
	}

	up_read(&mm->mmap_sem);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up_read(&mm->mmap_sem);

bad_area_nosemaphore:

#ifdef CONFIG_IA32_EMULATION
	/* 32bit vsyscall. map on demand. */
	if (test_thread_flag(TIF_IA32) &&
	    address >= VSYSCALL32_BASE && address < VSYSCALL32_END) {
		if (map_syscall32(mm, address) < 0)
			goto out_of_memory2;
		return;
	}
#endif

	/* User mode accesses just cause a SIGSEGV */
	if (error_code & 4) {
		if (is_prefetch(regs, address, error_code))
			return;

		/* Work around K8 erratum #100 K8 in compat mode
		   occasionally jumps to illegal addresses >4GB.  We
		   catch this here in the page fault handler because
		   these addresses are not reachable. Just detect this
		   case and return.  Any code segment in LDT is
		   compatibility mode. */
		if ((regs->cs == __USER32_CS || (regs->cs & (1<<2))) &&
		    (address >> 32))
			return;

		if (exception_trace && unhandled_signal(tsk, SIGSEGV)) {
			printk(
		       "%s%s[%d]: segfault at %016lx rip %016lx rsp %016lx error %lx\n",
					tsk->pid > 1 ? KERN_INFO : KERN_EMERG,
					tsk->comm, tsk->pid, address, regs->rip,
					regs->rsp, error_code);
		}
       
		tsk->thread.cr2 = address;
		/* Kernel addresses are always protection faults */
		tsk->thread.error_code = error_code | (address >= TASK_SIZE);
		tsk->thread.trap_no = 14;
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void __user *)address;
		force_sig_info(SIGSEGV, &info, tsk);
		return;
	}

no_context:
	
	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_tables(regs->rip);
	if (fixup) {
		regs->rip = fixup->fixup;
		return;
	}

	/* 
	 * Hall of shame of CPU/BIOS bugs.
	 */

 	if (is_prefetch(regs, address, error_code))
 		return;

	if (is_errata93(regs, address))
		return; 

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */

	oops_begin(); 

	if (address < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at %016lx RIP: \n" KERN_ALERT,address);
	printk_address(regs->rip);
	printk("\n");
	dump_pagetable(address);
	__die("Oops", regs, error_code);
	/* Executive summary in case the body of the oops scrolled away */
	printk(KERN_EMERG "CR2: %016lx\n", address);
	oops_end(); 
	do_exit(SIGKILL);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
out_of_memory2:
	if (current->pid == 1) { 
		yield();
		goto again;
	}
	printk("VM: killing process %s\n", tsk->comm);
	if (error_code & 4)
		do_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/* Kernel mode? Handle exceptions or die */
	if (!(error_code & 4))
		goto no_context;

	tsk->thread.cr2 = address;
	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = 14;
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void __user *)address;
	force_sig_info(SIGBUS, &info, tsk);
	return;
}

/*
 *  linux/arch/arm/kernel/traps.c
 *
 *  Copyright (C) 1995, 1996 Russell King
 *  Fragments that appear the same as linux/arch/i386/kernel/traps.c (C) Linus Torvalds
 */

/*
 * 'traps.c' handles hardware exceptions after we have saved some state in
 * 'linux/arch/arm/lib/traps.S'.  Mostly a debugging aid, but will probably
 * kill the offending process.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/spinlock.h>
#include <asm/atomic.h>
#include <asm/pgtable.h>

extern void c_backtrace (unsigned long fp, int pmode);
extern int ptrace_cancel_bpt (struct task_struct *);

char *processor_modes[]=
{ "USER_26", "FIQ_26" , "IRQ_26" , "SVC_26" , "UK4_26" , "UK5_26" , "UK6_26" , "UK7_26" ,
  "UK8_26" , "UK9_26" , "UK10_26", "UK11_26", "UK12_26", "UK13_26", "UK14_26", "UK15_26",
  "USER_32", "FIQ_32" , "IRQ_32" , "SVC_32" , "UK4_32" , "UK5_32" , "UK6_32" , "ABT_32" ,
  "UK8_32" , "UK9_32" , "UK10_32", "UND_32" , "UK12_32", "UK13_32", "UK14_32", "SYS_32"
};

static char *handler[]= { "prefetch abort", "data abort", "address exception", "interrupt" };

static inline void console_verbose(void)
{
	extern int console_loglevel;
	console_loglevel = 15;
}

int kstack_depth_to_print = 200;

/*
 * Stack pointers should always be within the kernels view of
 * physical memory.  If it is not there, then we can't dump
 * out any information relating to the stack.
 */
static int verify_stack(unsigned long sp)
{
	if (sp < PAGE_OFFSET || sp > (unsigned long)high_memory)
		return -EFAULT;

	return 0;
}

/*
 * Dump out the contents of some memory nicely...
 */
void dump_mem(unsigned long bottom, unsigned long top)
{
	unsigned long p = bottom & ~31;
	int i;

	for (p = bottom & ~31; p < top;) {
		printk("%08lx: ", p);

		for (i = 0; i < 8; i++, p += 4) {
			if (p < bottom || p >= top)
				printk("         ");
			else
				printk("%08lx ", *(unsigned long *)p);
			if (i == 3)
				printk(" ");
		}
		printk ("\n");
	}
}

/*
 * These constants are for searching for possible module text
 * segments.  VMALLOC_OFFSET comes from mm/vmalloc.c; MODULE_RANGE is
 * a guess of how much space is likely to be vmalloced.
 */
#define VMALLOC_OFFSET (8*1024*1024)
#define MODULE_RANGE (8*1024*1024)

static void dump_instr(unsigned long pc, int user)
{
	int pmin = -2, pmax = 3, ok = 0;
	extern char start_kernel, _etext;

	if (!user) {
		unsigned long module_start, module_end;
		unsigned long kernel_start, kernel_end;

		module_start = VMALLOC_START;
		module_end   = module_start + MODULE_RANGE;

		kernel_start = (unsigned long)&start_kernel;
		kernel_end   = (unsigned long)&_etext;

		if (pc >= kernel_start && pc < kernel_end) {
			if (pc + pmin < kernel_start)
				pmin = kernel_start - pc;
			if (pc + pmax > kernel_end)
				pmax = kernel_end - pc;
			ok = 1;
		} else if (pc >= module_start && pc < module_end) {
			if (pc + pmin < module_start)
				pmin = module_start - pc;
			if (pc + pmax > module_end)
				pmax = module_end - pc;
			ok = 1;
		}
	} else
		ok = verify_area(VERIFY_READ, (void *)(pc + pmin), pmax - pmin) == 0;

	printk ("Code: ");
	if (ok) {
		int i;
		for (i = pmin; i < pmax; i++)
			printk(i == 0 ? "(%08lx) " : "%08lx ", ((unsigned long *)pc)[i]);
		printk ("\n");
	} else
		printk ("pc not in code space\n");
}

spinlock_t die_lock;

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	struct task_struct *tsk = current;

	spin_lock_irq(&die_lock);

	console_verbose();
	printk("Internal error: %s: %x\n", str, err);
	printk("CPU: %d\n", smp_processor_id());
	show_regs(regs);
	printk("Process %s (pid: %d, stackpage=%08lx)\n",
		current->comm, current->pid, 4096+(unsigned long)tsk);

	if (!user_mode(regs)) {
		unsigned long sp = (unsigned long)(regs + 1);
		unsigned long fp;
		int dump_info = 1;

		printk("Stack: ");
		if (verify_stack(sp)) {
			printk("invalid kernel stack pointer %08lx", sp);
			dump_info = 0;
		} else if (sp < 4096+(unsigned long)tsk)
			printk("kernel stack pointer underflow");
		printk("\n");

		if (dump_info)
			dump_mem(sp - 16, 8192+(unsigned long)tsk);

		dump_info = 1;

		printk("Backtrace: ");
		fp = regs->ARM_fp;
		if (!fp) {
			printk("no frame pointer");
			dump_info = 0;
		} else if (verify_stack(fp)) {
			printk("invalid frame pointer %08lx", fp);
			dump_info = 0;
		} else if (fp < 4096+(unsigned long)tsk)
			printk("frame pointer underflow");
		printk("\n");

		if (dump_info)
			c_backtrace(fp, processor_mode(regs));

		dump_instr(instruction_pointer(regs), 0);
	}

	spin_unlock_irq(&die_lock);	
}

static void die_if_kernel(const char *str, struct pt_regs *regs, int err)
{
	if (user_mode(regs))
    		return;

    	die(str, regs, err);
}

void bad_user_access_alignment(const void *ptr)
{
	printk(KERN_ERR "bad user access alignment: ptr = %p, pc = %p\n", ptr, 
		__builtin_return_address(0));
	current->tss.error_code = 0;
	current->tss.trap_no = 11;
	force_sig(SIGBUS, current);
/*	die_if_kernel("Oops - bad user access alignment", regs, mode);*/
}

asmlinkage void do_undefinstr(int address, struct pt_regs *regs, int mode)
{
#ifdef CONFIG_DEBUG_USER
	printk(KERN_INFO "%s (%d): undefined instruction: pc=%08lx\n",
		current->comm, current->pid, instruction_pointer(regs));
#endif
	current->tss.error_code = 0;
	current->tss.trap_no = 6;
	force_sig(SIGILL, current);
	die_if_kernel("Oops - undefined instruction", regs, mode);
}

asmlinkage void do_excpt(int address, struct pt_regs *regs, int mode)
{
#ifdef CONFIG_DEBUG_USER
	printk(KERN_INFO "%s (%d): address exception: pc=%08lx\n",
		current->comm, current->pid, instruction_pointer(regs));
#endif
	current->tss.error_code = 0;
	current->tss.trap_no = 11;
	force_sig(SIGBUS, current);
	die_if_kernel("Oops - address exception", regs, mode);
}

asmlinkage void do_unexp_fiq (struct pt_regs *regs)
{
#ifndef CONFIG_IGNORE_FIQ
	printk("Hmm.  Unexpected FIQ received, but trying to continue\n");
	printk("You may have a hardware problem...\n");
#endif
}

/*
 * bad_mode handles the impossible case in the vectors.
 * If you see one of these, then it's extremely serious,
 * and could mean you have buggy hardware.  It never
 * returns, and never tries to sync.  We hope that we
 * can dump out some state information...
 */
asmlinkage void bad_mode(struct pt_regs *regs, int reason, int proc_mode)
{
	console_verbose();

	printk(KERN_CRIT "Bad mode in %s handler detected: mode %s\n",
		handler[reason], processor_modes[proc_mode]);

	/*
	 * Dump out the vectors and stub routines
	 */
	printk(KERN_CRIT "Vectors:\n");
	dump_mem(0, 0x40);
	printk(KERN_CRIT "Stubs:\n");
	dump_mem(0x200, 0x4b8);

	die("Oops", regs, 0);
	cli();
	while(1);
}

/*
 * 'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task.
 *
 * We no longer save/restore the math state on every context switch
 * any more.  We only do this now if it actually gets used.
 */
asmlinkage void math_state_restore (void)
{
	current->used_math = 1;
}

asmlinkage int arm_syscall (int no, struct pt_regs *regs)
{
	switch (no) {
	case 0: /* branch through 0 */
		force_sig(SIGSEGV, current);
		die_if_kernel("branch through zero", regs, 0);
		break;

	case 1: /* SWI_BREAK_POINT */
		regs->ARM_pc -= 4; /* Decrement PC by one instruction */
		ptrace_cancel_bpt(current);
		force_sig(SIGTRAP, current);
		return regs->ARM_r0;

	case 2:	/* sys_cacheflush */
#ifdef CONFIG_CPU_32
		/* r0 = start, r1 = length, r2 = flags */
		processor.u.armv3v4._flush_cache_area(regs->ARM_r0,
						      regs->ARM_r1,
						      1);
#endif
		break;

	default:
		/* Calls 9f00xx..9f07ff are defined to return -ENOSYS
		   if not implemented, rather than raising SIGILL.  This
		   way the calling program can gracefully determine whether
		   a feature is supported.  */
		if (no <= 0x7ff)
			return -ENOSYS;
#ifdef CONFIG_DEBUG_USER
		/* experiance shows that these seem to indicate that
		 * something catastrophic has happened
		 */
		printk("[%d] %s: arm syscall %d\n", current->pid, current->comm, no);
		if (user_mode(regs)) {
			show_regs(regs);
			c_backtrace(regs->ARM_fp, processor_mode(regs));
		}
#endif
		force_sig(SIGILL, current);
		die_if_kernel("Oops", regs, no);
		break;
	}
	return 0;
}

asmlinkage void deferred(int n, struct pt_regs *regs)
{
	/* You might think just testing `handler' would be enough, but PER_LINUX
	 * points it to no_lcall7 to catch undercover SVr4 binaries.  Gutted.
	 */
	if (current->personality != PER_LINUX && current->exec_domain->handler) {
		/* Hand it off to iBCS.  The extra parameter and consequent type 
		 * forcing is necessary because of the weird ARM calling convention.
		 */
		void (*handler)(int nr, struct pt_regs *regs) = (void *)current->exec_domain->handler;
		(*handler)(n, regs);
		return;
	}

#ifdef CONFIG_DEBUG_USER
	printk(KERN_ERR "[%d] %s: old system call.\n", current->pid, 
	       current->comm);
#endif
	force_sig(SIGILL, current);
}

asmlinkage void arm_malalignedptr(const char *str, void *pc, volatile void *ptr)
{
	printk("Mal-aligned pointer in %s: %p (PC=%p)\n", str, ptr, pc);
}

asmlinkage void arm_invalidptr(const char *function, int size)
{
	printk("Invalid pointer size in %s (pc=%p) size %d\n",
		function, __builtin_return_address(0), size);
}

#ifdef CONFIG_CPU_26
asmlinkage void baddataabort(int code, unsigned long instr, struct pt_regs *regs)
{
	unsigned long phys, addr = instruction_pointer(regs);

#ifdef CONFIG_DEBUG_ERRORS
	printk("pid=%d\n", current->pid);

	show_regs(regs);
	dump_instr(instruction_pointer(regs), 1);
	{
		pgd_t *pgd;

		printk ("current->tss.memmap = %08lX\n", current->tss.memmap);
		pgd = pgd_offset(current->mm, addr);
		printk ("*pgd = %08lx", pgd_val (*pgd));
		if (!pgd_none (*pgd)) {
			pmd_t *pmd;
			pmd = pmd_offset (pgd, addr);
			printk (", *pmd = %08lx", pmd_val (*pmd));
			if (!pmd_none (*pmd)) {
				unsigned long ptr = pte_page(*pte_offset(pmd, addr));
				printk (", *pte = %08lx", pte_val (*pte_offset (pmd, addr)));
				phys = ptr + (addr & 0x7fff);
			}
		}
		printk ("\n");
	}
#endif
	panic("unknown data abort code %d [pc=%08lx *pc=%08lx lr=%08lx sp=%08lx]",
		code, regs->ARM_pc, instr, regs->ARM_lr, regs->ARM_sp);
}
#endif

asmlinkage void __div0(void)
{
	printk("Awooga, division by zero in kernel.\n");
	__backtrace();
}

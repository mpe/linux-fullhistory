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

extern void die_if_kernel(char *str, struct pt_regs *regs, int err, int ret);
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

static int verify_stack_pointer (unsigned long stackptr, int size)
{
#ifdef CONFIG_CPU_26
	if (stackptr < 0x02048000 || stackptr + size > 0x03000000)
        	return -EFAULT;
#else
	if (stackptr < PAGE_OFFSET || stackptr + size > (unsigned long)high_memory)
		return -EFAULT;
#endif
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
	unsigned long module_start, module_end;
	int pmin = -2, pmax = 3, ok = 0;
	extern char start_kernel, _etext;

	if (!user) {
		module_start = VMALLOC_START;
		module_end   = module_start + MODULE_RANGE;

		if ((pc >= (unsigned long) &start_kernel) &&
		    (pc <= (unsigned long) &_etext)) {
			if (pc + pmin < (unsigned long) &start_kernel)
				pmin = ((unsigned long) &start_kernel) - pc;
			if (pc + pmax > (unsigned long) &_etext)
				pmax = ((unsigned long) &_etext) - pc;
			ok = 1;
		} else if (pc >= module_start && pc <= module_end) {
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

static void dump_state(char *str, struct pt_regs *regs, int err)
{
	console_verbose();
	printk("Internal error: %s: %x\n", str, err);
	printk("CPU: %d\n", smp_processor_id());
	show_regs(regs);
	printk("Process %s (pid: %d, stackpage=%08lx)\n",
		current->comm, current->pid, 4096+(unsigned long)current);
}

/*
 * This function is protected against kernel-mode re-entrancy.  If it
 * is re-entered it will hang the system since we can't guarantee in
 * this case that any of the functions that it calls are safe any more.
 * Even the panic function could be a problem, but we'll give it a go.
 */
void die_if_kernel(char *str, struct pt_regs *regs, int err, int ret)
{
	static int died = 0;
	unsigned long cstack, sstack, frameptr;
	
	if (user_mode(regs))
    		return;

	switch (died) {
	case 2:
		while (1);
	case 1:
		died ++;
		panic ("die_if_kernel re-entered.  Major kernel corruption.  Please reboot me!");
		break;
	case 0:
		died ++;
		break;
	}

	dump_state(str, regs, err);

	cstack = (unsigned long)(regs + 1);
	sstack = 4096+(unsigned long)current;

	printk("Stack: ");
	if (verify_stack_pointer(cstack, 4))
		printk("invalid kernel stack pointer %08lx", cstack);
	else if(cstack > sstack + 4096)
		printk("(sp overflow)");
	else if(cstack < sstack)
		printk("(sp underflow)");
	printk("\n");

	dump_mem(cstack - 16, sstack + 4096);

	frameptr = regs->ARM_fp;
	if (frameptr) {
		if (verify_stack_pointer (frameptr, 4))
			printk ("Backtrace: invalid frame pointer\n");
		else {
			printk("Backtrace: \n");
			c_backtrace (frameptr, processor_mode(regs));
		}
	}

	dump_instr(instruction_pointer(regs), 0);
	died = 0;
	if (ret != -1)
		do_exit (ret);
	else {
		cli ();
		while (1);
	}
}

void bad_user_access_alignment (const void *ptr)
{
	void *pc;
	__asm__("mov %0, lr\n": "=r" (pc));
	printk (KERN_ERR "bad_user_access_alignment called: ptr = %p, pc = %p\n", ptr, pc);
	current->tss.error_code = 0;
	current->tss.trap_no = 11;
	force_sig (SIGBUS, current);
/*	die_if_kernel("Oops - bad user access alignment", regs, mode, SIGBUS);*/
}

asmlinkage void do_undefinstr (int address, struct pt_regs *regs, int mode)
{
	current->tss.error_code = 0;
	current->tss.trap_no = 6;
	force_sig (SIGILL, current);
	die_if_kernel("Oops - undefined instruction", regs, mode, SIGILL);
}

asmlinkage void do_excpt (int address, struct pt_regs *regs, int mode)
{
	current->tss.error_code = 0;
	current->tss.trap_no = 11;
	force_sig (SIGBUS, current);
	die_if_kernel("Oops - address exception", regs, mode, SIGBUS);
}

asmlinkage void do_unexp_fiq (struct pt_regs *regs)
{
#ifndef CONFIG_IGNORE_FIQ
	printk ("Hmm.  Unexpected FIQ received, but trying to continue\n");
	printk ("You may have a hardware problem...\n");
#endif
}

asmlinkage void bad_mode(struct pt_regs *regs, int reason, int proc_mode)
{
	printk (KERN_CRIT "Bad mode in %s handler detected: mode %s\n",
		handler[reason],
		processor_modes[proc_mode]);
	die_if_kernel ("Oops", regs, 0, -1);
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

asmlinkage void arm_syscall (int no, struct pt_regs *regs)
{
	switch (no) {
	case 0: /* branch through 0 */
		force_sig(SIGSEGV, current);
//		if (user_mode(regs)) {
//			dump_state("branch through zero", regs, 0);
//			if (regs->ARM_fp)
//				c_backtrace (regs->ARM_fp, processor_mode(regs));
//		}
		die_if_kernel ("branch through zero", regs, 0, SIGSEGV);
		break;

	case 1: /* SWI_BREAK_POINT */
		regs->ARM_pc -= 4; /* Decrement PC by one instruction */
		ptrace_cancel_bpt (current);
		force_sig (SIGTRAP, current);
		break;

	default:
		printk ("[%d] %s: arm syscall %d\n", current->pid, current->comm, no);
		force_sig (SIGILL, current);
		if (user_mode(regs)) {
			show_regs (regs);
			c_backtrace (regs->ARM_fp, processor_mode(regs));
		}
		die_if_kernel ("Oops", regs, no, SIGILL);
		break;
	}
}

asmlinkage void deferred(int n, struct pt_regs *regs)
{
	dump_state("old system call", regs, n);
	force_sig (SIGILL, current);
}

asmlinkage void arm_malalignedptr(const char *str, void *pc, volatile void *ptr)
{
	printk ("Mal-aligned pointer in %s: %p (PC=%p)\n", str, ptr, pc);
}

asmlinkage void arm_invalidptr (const char *function, int size)
{
	printk ("Invalid pointer size in %s (PC=%p) size %d\n",
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

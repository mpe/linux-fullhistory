/* $Id: traps.c,v 1.19 1997/06/05 06:22:49 davem Exp $
 * arch/sparc/kernel/traps.c
 *
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

/*
 * I hate traps on the sparc, grrr...
 */

#include <linux/sched.h>  /* for jiffies */
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/delay.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/fpumacro.h>

/* #define SYSCALL_TRACING */
/* #define VERBOSE_SYSCALL_TRACING */

#ifdef SYSCALL_TRACING
#ifdef VERBOSE_SYSCALL_TRACING
struct sdesc {
	int	scall_num;
	char	*name;
	int	num_args;
	char	arg_is_string[6];
} sdesc_entries[] = {
	{ 0, "setup", 0, },
	{ 1, "exit", 1, { 0, } },
	{ 2, "fork", 0, },
	{ 3, "read", 3, { 0, 0, 0, } },
	{ 4, "write", 3, { 0, 0, 0, } },
	{ 5, "open", 3, { 1, 0, 0, } },
	{ 6, "close", 1, { 0, } },
	{ 7, "wait4", 4, { 0, 0, 0, 0, } },
	{ 8, "creat", 2, { 1, 0, } },
	{ 9, "link", 2, { 1, 1, } },
	{ 10, "unlink", 1, { 1, } },
	{ 11, "execv", 2, { 1, 0, } },
	{ 12, "chdir", 1, { 1, } },
	{ 15, "chmod", 2, { 1, 0, } },
	{ 16, "chown", 3, { 1, 0, 0, } },
	{ 17, "brk", 1, { 0, } },
	{ 19, "lseek", 3, { 0, 0, 0, } },
	{ 27, "alarm", 1, { 0, } },
	{ 29, "pause", 0, },
	{ 33, "access", 2, { 1, 0, } },
	{ 36, "sync", 0, },
	{ 37, "kill", 2, { 0, 0, } },
	{ 38, "stat", 2, { 1, 0, } },
	{ 40, "lstat", 2, { 1, 0, } },
	{ 41, "dup", 1, { 0, } },
	{ 42, "pipd", 0, },
	{ 54, "ioctl", 3, { 0, 0, 0, } },
	{ 57, "symlink", 2, { 1, 1, } },
	{ 58, "readlink", 3, { 1, 0, 0, } },
	{ 59, "execve", 3, { 1, 0, 0, } },
	{ 60, "umask", 1, { 0, } },
	{ 62, "fstat", 2, { 0, 0, } },
	{ 64, "getpagesize", 0, },
	{ 71, "mmap", 6, { 0, 0, 0, 0, 0, 0, } },
	{ 73, "munmap", 2, { 0, 0, } },
	{ 74, "mprotect", 3, { 0, 0, 0, } },
	{ 83, "setitimer", 3, { 0, 0, 0, } },
	{ 90, "dup2", 2, { 0, 0, } },
	{ 92, "fcntl", 3, { 0, 0, 0, } },
	{ 93, "select", 5, { 0, 0, 0, 0, 0, } },
	{ 97, "socket", 3, { 0, 0, 0, } },
	{ 98, "connect", 3, { 0, 0, 0, } },
	{ 99, "accept", 3, { 0, 0, 0, } },
	{ 101, "send", 4, { 0, 0, 0, 0, } },
	{ 102, "recv", 4, { 0, 0, 0, 0, } },
	{ 104, "bind", 3, { 0, 0, 0, } },
	{ 105, "setsockopt", 5, { 0, 0, 0, 0, 0, } },
	{ 106, "listen", 2, { 0, 0, } },
	{ 120, "readv", 3, { 0, 0, 0, } },
	{ 121, "writev", 3, { 0, 0, 0, } },
	{ 123, "fchown", 3, { 0, 0, 0, } },
	{ 124, "fchmod", 2, { 0, 0, } },
	{ 128, "rename", 2, { 1, 1, } },
	{ 129, "truncate", 2, { 1, 0, } },
	{ 130, "ftruncate", 2, { 0, 0, } },
	{ 131, "flock", 2, { 0, 0, } },
	{ 136, "mkdir", 2, { 1, 0, } },
	{ 137, "rmdir", 1, { 1, } },
	{ 146, "killpg", 1, { 0, } },
	{ 157, "statfs", 2, { 1, 0, } },
	{ 158, "fstatfs", 2, { 0, 0, } },
	{ 159, "umount", 1, { 1, } },
	{ 167, "mount", 5, { 1, 1, 1, 0, 0, } },
	{ 174, "getdents", 3, { 0, 0, 0, } },
	{ 176, "fchdir", 2, { 0, 0, } },
	{ 198, "sigaction", 3, { 0, 0, 0, } },
	{ 201, "sigsuspend", 1, { 0, } },
	{ 206, "socketcall", 2, { 0, 0, } },
	{ 216, "sigreturn", 0, },
	{ 230, "newselect", 5, { 0, 0, 0, 0, 0, } },
	{ 236, "llseek", 5, { 0, 0, 0, 0, 0, } },
	{ 251, "sysctl", 1, { 0, } },
};
#define NUM_SDESC_ENTRIES (sizeof(sdesc_entries) / sizeof(sdesc_entries[0]))
#endif

#ifdef VERBOSE_SYSCALL_TRACING
static char scall_strbuf[512];
#endif

void syscall_trace_entry(unsigned long g1, struct pt_regs *regs)
{
#ifdef VERBOSE_SYSCALL_TRACING
	struct sdesc *sdp;
	int i;
#endif

	printk("SYS[%s:%d]: PC(%016lx) <%3d> ",
	       current->comm, current->pid, regs->tpc, (int)g1);
#ifdef VERBOSE_SYSCALL_TRACING
	sdp = NULL;
	for(i = 0; i < NUM_SDESC_ENTRIES; i++)
		if(sdesc_entries[i].scall_num == g1) {
			sdp = &sdesc_entries[i];
			break;
		}
	if(sdp) {
		printk("%s(", sdp->name);
		for(i = 0; i < sdp->num_args; i++) {
			if(i)
				printk(",");
			if(!sdp->arg_is_string[i])
				printk("%08x", (unsigned int)regs->u_regs[UREG_I0 + i]);
			else {
				strncpy_from_user(scall_strbuf,
						  (char *)regs->u_regs[UREG_I0 + i],
						  512);
				printk("%s", scall_strbuf);
			}
		}
		printk(") ");
	}
#endif
}

unsigned long syscall_trace_exit(unsigned long retval, struct pt_regs *regs)
{
	printk("ret[%016lx]\n", retval);
	return retval;
}
#endif /* SYSCALL_TRACING */

#if 0
void user_rtrap_report(struct pt_regs *regs)
{
	static int hits = 0;

	/* Bwahhhhrggg... */
	if(regs->tpc == 0x1f294UL && ++hits == 2) {
		register unsigned long ctx asm("o4");
		register unsigned long paddr asm("o5");
		unsigned long cwp, wstate;

		printk("RT[%016lx:%016lx] ", regs->tpc, regs->u_regs[UREG_I6]);
		__asm__ __volatile__("rdpr %%cwp, %0" : "=r" (cwp));
		__asm__ __volatile__("rdpr %%wstate, %0" : "=r" (wstate));
		printk("CWP[%d] WSTATE[%016lx]\n"
		       "TSS( ksp[%016lx] kpc[%016lx] wstate[%016lx] w_saved[%d] flgs[%x]"
		       " cur_ds[%d] )\n", cwp, wstate,
		       current->tss.ksp, current->tss.kpc, current->tss.wstate,
		       (int) current->tss.w_saved, current->tss.flags,
		       current->tss.current_ds);
		__asm__ __volatile__("
		rdpr	%%pstate, %%o3
		wrpr	%%o3, %2, %%pstate
		mov	%%g7, %%o5
		mov	0x10, %%o4
		ldxa	[%%o4] %3, %%o4
		wrpr	%%o3, 0x0, %%pstate
		" : "=r" (ctx), "=r" (paddr)
		  : "i" (PSTATE_MG|PSTATE_IE), "i" (ASI_DMMU));

		printk("MMU[ppgd(%016lx)sctx(%d)] ", paddr, ctx);
		printk("mm->context(%016lx) mm->pgd(%p)\n",
		       current->mm->context, current->mm->pgd);
		printk("TASK: signal[%016lx] blocked[%016lx]\n",
		       current->signal, current->blocked);
		show_regs(regs);
		while(1)
			barrier();
	}
}
#endif

void bad_trap (struct pt_regs *regs, long lvl)
{
	lock_kernel ();
	if (lvl < 0x100) {
		char buffer[24];
		
		sprintf (buffer, "Bad hw trap %lx at tl0\n", lvl);
		die_if_kernel (buffer, regs);
	}
	if (regs->tstate & TSTATE_PRIV)
		die_if_kernel ("Kernel bad trap", regs);
        current->tss.sig_desc = SUBSIG_BADTRAP(lvl - 0x100);
        current->tss.sig_address = regs->tpc;
        send_sig(SIGILL, current, 1);
	unlock_kernel ();
}

void bad_trap_tl1 (struct pt_regs *regs, long lvl)
{
	char buffer[24];
	
	lock_kernel ();
	sprintf (buffer, "Bad trap %lx at tl>0", lvl);
	die_if_kernel (buffer, regs);
}

void data_access_exception (struct pt_regs *regs)
{
	lock_kernel ();
	printk ("Unhandled data access exception ");
	printk("sfsr %016lx sfar %016lx\n", spitfire_get_dsfsr(), spitfire_get_sfar());
	die_if_kernel("Data access exception", regs);
}

void do_dae(struct pt_regs *regs)
{
	printk("DAE: at %016lx\n", regs->tpc);
	while(1)
		barrier();
}

void instruction_access_exception (struct pt_regs *regs)
{
	lock_kernel ();
	printk ("Unhandled instruction access exception ");
	printk("sfsr %016lx\n", spitfire_get_isfsr());
	die_if_kernel("Instruction access exception", regs);
}

void do_iae(struct pt_regs *regs)
{
	printk("IAE at %016lx\n", regs->tpc);
	while(1)
		barrier();
}

static unsigned long init_fsr = 0x0UL;
static unsigned int init_fregs[64] __attribute__ ((aligned (64))) =
                { ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U,
		  ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U,
		  ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U,
		  ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U,
		  ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U,
		  ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U,
		  ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U,
		  ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U, ~0U };

void do_fpdis(struct pt_regs *regs)
{
	lock_kernel();

	regs->tstate |= TSTATE_PEF;
	fprs_write(FPRS_FEF);

	/* This is allowed now because the V9 ABI varargs passes floating
	 * point args in floating point registers, so vsprintf() and sprintf()
	 * cause problems.  Luckily we never actually pass floating point values
	 * to those routines in the kernel and the code generated just does
	 * stores of them to the stack.  Therefore, for the moment this fix
	 * is sufficient. -DaveM
	 */
	if(regs->tstate & TSTATE_PRIV)
		goto out;

#ifndef __SMP__
	if(last_task_used_math == current)
		goto out;
	if(last_task_used_math) {
		struct task_struct *fptask = last_task_used_math;

		if(fptask->tss.flags & SPARC_FLAG_32BIT)
			fpsave32((unsigned long *)&fptask->tss.float_regs[0],
				 &fptask->tss.fsr);
		else
			fpsave((unsigned long *)&fptask->tss.float_regs[0],
			       &fptask->tss.fsr);
	}
	last_task_used_math = current;
	if(current->used_math) {
		if(current->tss.flags & SPARC_FLAG_32BIT)
			fpload32(&current->tss.float_regs[0],
				 &current->tss.fsr);
		else
			fpload(&current->tss.float_regs[0],
			       &current->tss.fsr);
	} else {
		/* Set inital sane state. */
		fpload(&init_fregs[0], &init_fsr);
		current->used_math = 1;
	}
#else
	if(!current->used_math) {
		fpload(&init_fregs[0], &init_fsr);
		current->used_math = 1;
	} else {
		if(current->tss.flags & SPARC_FLAG_32BIT)
			fpload32(&current->tss.float_regs[0],
				 &current->tss.fsr);
		else
			fpload(&current->tss.float_regs[0],
			       &current->tss.fsr);
	}
	current->flags |= PF_USEDFPU;
#endif
#ifndef __SMP__
out:
#endif
	unlock_kernel();
}

static unsigned long fake_regs[32] __attribute__ ((aligned (8)));
static unsigned long fake_fsr;

void do_fpe_common(struct pt_regs *regs)
{
	static int calls = 0;
#ifndef __SMP__
	struct task_struct *fpt = last_task_used_math;
#else
	struct task_struct *fpt = current;
#endif

	lock_kernel();
	fprs_write(FPRS_FEF);

#ifndef __SMP__
	if(!fpt) {
#else
	if(!(fpt->flags & PF_USEDFPU)) {
#endif
		fpsave(&fake_regs[0], &fake_fsr);
		regs->tstate &= ~(TSTATE_PEF);
		goto out;
	}
	if(fpt->tss.flags & SPARC_FLAG_32BIT)
		fpsave32((unsigned long *)&fpt->tss.float_regs[0], &fpt->tss.fsr);
	else
		fpsave((unsigned long *)&fpt->tss.float_regs[0], &fpt->tss.fsr);
	fpt->tss.sig_address = regs->tpc;
	fpt->tss.sig_desc = SUBSIG_FPERROR;
#ifdef __SMP__
	fpt->flags &= ~PF_USEDFPU;
#endif
	if(regs->tstate & TSTATE_PRIV) {
		printk("WARNING: FPU exception from kernel mode. at pc=%016lx\n",
		       regs->tpc);
		regs->tpc = regs->tnpc;
		regs->tnpc += 4;
		calls++;
		if(calls > 2)
			die_if_kernel("Too many Penguin-FPU traps from kernel mode",
				      regs);
		goto out;
	}
	send_sig(SIGFPE, fpt, 1);
#ifndef __SMP__
	last_task_used_math = NULL;
#endif
	regs->tstate &= ~TSTATE_PEF;
	if(calls > 0)
		calls = 0;
out:
	unlock_kernel();
}

void do_fpieee(struct pt_regs *regs)
{
	do_fpe_common(regs);
}

void do_fpother(struct pt_regs *regs)
{
	do_fpe_common(regs);
}

void do_tof(struct pt_regs *regs)
{
	printk("TOF: at %016lx\n", regs->tpc);
	while(1)
		barrier();
}

void do_div0(struct pt_regs *regs)
{
	printk("DIV0: at %016lx\n", regs->tpc);
	while(1)
		barrier();
}

void instruction_dump (unsigned int *pc)
{
	int i;
	
	if((((unsigned long) pc) & 3))
                return;

	for(i = -3; i < 6; i++)
		printk("%c%08x%c",i?' ':'<',pc[i],i?' ':'>');
	printk("\n");
}

void die_if_kernel(char *str, struct pt_regs *regs)
{
	/* Amuse the user. */
	printk(
"              \\|/ ____ \\|/\n"
"              \"@'/ .` \\`@\"\n"
"              /_| \\__/ |_\\\n"
"                 \\__U_/\n");

	printk("%s(%d): %s\n", current->comm, current->pid, str);
	__asm__ __volatile__("flushw");
	show_regs(regs);
	{
		struct reg_window *rw = (struct reg_window *)
			(regs->u_regs[UREG_FP] + STACK_BIAS);

		if(rw) {
			printk("Caller[%016lx]\n", rw->ins[7]);
			rw = (struct reg_window *)
				(rw->ins[6] + STACK_BIAS);
			if(rw) {
				printk("Caller[%016lx]\n", rw->ins[7]);
				rw = (struct reg_window *)
					(rw->ins[6] + STACK_BIAS);
				if(rw)
					printk("Caller[%016lx]\n", rw->ins[7]);
			}
		}
	}
	printk("Instruction DUMP:");
	instruction_dump ((unsigned int *) regs->tpc);
	if(regs->tstate & TSTATE_PRIV)
		do_exit(SIGKILL);
	do_exit(SIGSEGV);
}

void do_illegal_instruction(struct pt_regs *regs)
{
	unsigned long pc = regs->tpc;
	unsigned long tstate = regs->tstate;

	lock_kernel();
	if(tstate & TSTATE_PRIV)
		die_if_kernel("Kernel illegal instruction", regs);
#if 1
	{
		unsigned int insn;

		printk("Ill instr. at pc=%016lx ", pc);
		get_user(insn, ((unsigned int *)pc));
		printk("insn=[%08x]\n", insn);
		show_regs(regs);
	}
#endif
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_ILLINST;
	send_sig(SIGILL, current, 1);
	unlock_kernel();
}

void mem_address_unaligned(struct pt_regs *regs)
{
	printk("AIEEE: do_mna at %016lx\n", regs->tpc);
	show_regs(regs);
	if(regs->tstate & TSTATE_PRIV) {
		printk("MNA from kernel, spinning\n");
		sti();
		while(1)
			barrier();
	} else {
		current->tss.sig_address = regs->tpc;
		current->tss.sig_desc = SUBSIG_PRIVINST;
		send_sig(SIGBUS, current, 1);
	}
}

void do_privop(struct pt_regs *regs)
{
	printk("PRIVOP: at %016lx\n", regs->tpc);
	while(1)
		barrier();
}

void do_privact(struct pt_regs *regs)
{
	printk("PRIVACT: at %016lx\n", regs->tpc);
	while(1)
		barrier();
}

void do_priv_instruction(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			 unsigned long tstate)
{
	lock_kernel();
	if(tstate & TSTATE_PRIV)
		die_if_kernel("Penguin instruction from Penguin mode??!?!", regs);
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGILL, current, 1);
	unlock_kernel();
}

/* XXX User may want to be allowed to do this. XXX */

void do_memaccess_unaligned(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			    unsigned long tstate)
{
	lock_kernel();
	if(regs->tstate & TSTATE_PRIV) {
		printk("KERNEL MNA at pc %016lx npc %016lx called by %016lx\n", pc, npc,
		       regs->u_regs[UREG_RETPC]);
		die_if_kernel("BOGUS", regs);
		/* die_if_kernel("Kernel MNA access", regs); */
	}
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
#if 0
	show_regs (regs);
	instruction_dump ((unsigned long *) regs->tpc);
	printk ("do_MNA!\n");
#endif
	send_sig(SIGBUS, current, 1);
	unlock_kernel();
}

void handle_hw_divzero(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	lock_kernel();
	send_sig(SIGILL, current, 1);
	unlock_kernel();
}

void trap_init(void)
{
}

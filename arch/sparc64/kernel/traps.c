/* $Id: traps.c,v 1.10 1997/05/18 08:42:16 davem Exp $
 * arch/sparc/kernel/traps.c
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
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

/* #define TRAP_DEBUG */

struct trap_trace_entry {
	unsigned long pc;
	unsigned long type;
};

int trap_curbuf = 0;
struct trap_trace_entry trapbuf[1024];

void syscall_trace_entry(struct pt_regs *regs)
{
	printk("%s[%d]: ", current->comm, current->pid);
	printk("scall<%ld> (could be %ld)\n", (long) regs->u_regs[UREG_G1],
	       (long) regs->u_regs[UREG_I0]);
}

void syscall_trace_exit(struct pt_regs *regs)
{
	printk("Syscall return check, reg dump.\n");
	show_regs(regs);
}

void sparc64_dtlb_fault_handler (void)
{
	printk ("sparc64_dtlb_fault_handler\n");
	while (1);
	/* Die for now... */
}

void sparc64_dtlb_refbit_handler (struct pt_regs *regs)
{
	printk ("sparc64_dtlb_refbit_handler[%016lx]\n", regs->tpc);
	while (1);
	/* Die for now... */
}

void sparc64_itlb_refbit_handler (void)
{
	printk ("sparc64_itlb_refbit_handler\n");
	while (1);
	/* Die for now... */
}

void bad_trap (struct pt_regs *regs, long lvl)
{
	printk ("Bad trap %d (tstate %016lx tpc %016lx tnpc %016lx)\n", lvl, regs->tstate, regs->tpc, regs->tnpc);
	while (1);
	/* Die for now... */
}

void bad_trap_tl1 (struct pt_regs *regs, long lvl)
{
	printk ("Bad trap %d at tl1+ (tstate %016lx tpc %016lx tnpc %016lx)\n", lvl, regs->tstate, regs->tpc, regs->tnpc);
	while (1);
	/* Die for now... */
}

void data_access_exception (struct pt_regs *regs)
{
	printk ("Unhandled data access exception sfsr %016lx sfar %016lx\n", spitfire_get_dsfsr(), spitfire_get_sfar());
	die_if_kernel("Data access exception", regs);
}

void instruction_access_exception (struct pt_regs *regs)
{
	printk ("Unhandled instruction access exception sfsr %016lx\n", spitfire_get_isfsr());
	die_if_kernel("Instruction access exception", regs);
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
	show_regs(regs);
	printk("Instruction DUMP:");
	instruction_dump ((unsigned int *) regs->tpc);
	while(1)
		barrier();
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
	}
#endif
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_ILLINST;
	send_sig(SIGILL, current, 1);
	unlock_kernel();

	while(1)
		barrier();
}

void do_mna(struct pt_regs *regs)
{
	printk("AIEEE: do_mna at %016lx\n", regs->tpc);
	show_regs(regs);
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

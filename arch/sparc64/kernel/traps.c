/* $Id: traps.c,v 1.1 1997/03/18 17:59:12 jj Exp $
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
	if(regs->tstate & TSTATE_PRIV)
		do_exit(SIGKILL);
	do_exit(SIGSEGV);
}

void do_illegal_instruction(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			    unsigned long tstate)
{
	lock_kernel();
	if(tstate & TSTATE_PRIV)
		die_if_kernel("Kernel illegal instruction", regs);
#ifdef TRAP_DEBUG
	printk("Ill instr. at pc=%016lx instruction is %08x\n",
	       regs->tpc, *(unsigned int *)regs->tpc);
#endif
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_ILLINST;
	send_sig(SIGILL, current, 1);
	unlock_kernel();
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

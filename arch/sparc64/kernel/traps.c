/* $Id: traps.c,v 1.29 1997/07/05 09:52:38 davem Exp $
 * arch/sparc64/kernel/traps.c
 *
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

/*
 * I like traps on v9, :))))
 */

#include <linux/config.h>
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

	if(strcmp(current->comm, "bash.sunos"))
		return;
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
	if(!strcmp(current->comm, "bash.sunos"))
		printk("ret[%016lx]\n", retval);
	return retval;
}
#endif /* SYSCALL_TRACING */

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
	
	lock_kernel();
	sprintf (buffer, "Bad trap %lx at tl>0", lvl);
	die_if_kernel (buffer, regs);
	unlock_kernel();
}

void data_access_exception (struct pt_regs *regs)
{
	send_sig(SIGSEGV, current, 1);
}

void do_dae(struct pt_regs *regs)
{
	send_sig(SIGSEGV, current, 1);
}

void instruction_access_exception (struct pt_regs *regs)
{
	send_sig(SIGSEGV, current, 1);
}

void do_iae(struct pt_regs *regs)
{
	send_sig(SIGSEGV, current, 1);
}

void do_fpe_common(struct pt_regs *regs)
{
	if(regs->tstate & TSTATE_PRIV) {
		regs->tpc = regs->tnpc;
		regs->tnpc += 4;
	} else {
		lock_kernel();
		current->tss.sig_address = regs->tpc;
		current->tss.sig_desc = SUBSIG_FPERROR;
		send_sig(SIGFPE, current, 1);
		unlock_kernel();
	}
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
	if(regs->tstate & TSTATE_PRIV)
		die_if_kernel("Penguin overflow trap from kernel mode", regs);
	current->tss.sig_address = regs->tpc;
	current->tss.sig_desc = SUBSIG_TAG; /* as good as any */
	send_sig(SIGEMT, current, 1);
}

void do_div0(struct pt_regs *regs)
{
	send_sig(SIGILL, current, 1);
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
"              \"@'/ .. \\`@\"\n"
"              /_| \\__/ |_\\\n"
"                 \\__U_/\n");

	printk("%s(%d): %s\n", current->comm, current->pid, str);
	__asm__ __volatile__("flushw");
	show_regs(regs);
	{
		struct reg_window *rw = (struct reg_window *)
			(regs->u_regs[UREG_FP] + STACK_BIAS);

		/* Stop the back trace when we hit userland or we
		 * find some badly aligned kernel stack.
		 */
		while(rw					&&
		      (((unsigned long) rw) >= PAGE_OFFSET)	&&
		      !(((unsigned long) rw) & 0x7)) {
			printk("Caller[%016lx]\n", rw->ins[7]);
			rw = (struct reg_window *)
				(rw->ins[6] + STACK_BIAS);
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
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_ILLINST;
	send_sig(SIGILL, current, 1);
	unlock_kernel();
}

void mem_address_unaligned(struct pt_regs *regs)
{
	if(regs->tstate & TSTATE_PRIV) {
		extern void kernel_unaligned_trap(struct pt_regs *regs,
						  unsigned int insn);

		return kernel_unaligned_trap(regs, *((unsigned int *)regs->tpc));
	} else {
		current->tss.sig_address = regs->tpc;
		current->tss.sig_desc = SUBSIG_PRIVINST;
		send_sig(SIGBUS, current, 1);
	}
}

void do_privop(struct pt_regs *regs)
{
	current->tss.sig_address = regs->tpc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGILL, current, 1);
}

void do_privact(struct pt_regs *regs)
{
	current->tss.sig_address = regs->tpc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
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

/* Trap level 1 stuff or other traps we should never see... */
void do_cee(struct pt_regs *regs)
{
	die_if_kernel("TL0: Cache Error Exception", regs);
}

void do_cee_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Cache Error Exception", regs);
}

void do_dae_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Data Access Exception", regs);
}

void do_iae_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Instruction Access Exception", regs);
}

void do_div0_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: DIV0 Exception", regs);
}

void do_fpdis_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: FPU Disabled", regs);
}

void do_fpieee_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: FPU IEEE Exception", regs);
}

void do_fpother_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: FPU Other Exception", regs);
}

void do_ill_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Illegal Instruction Exception", regs);
}

void do_irq_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: IRQ Exception", regs);
}

void do_lddfmna(struct pt_regs *regs)
{
	die_if_kernel("TL0: LDDF Exception", regs);
}

void do_lddfmna_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: LDDF Exception", regs);
}

void do_stdfmna(struct pt_regs *regs)
{
	die_if_kernel("TL0: STDF Exception", regs);
}

void do_stdfmna_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: STDF Exception", regs);
}

void do_paw(struct pt_regs *regs)
{
	die_if_kernel("TL0: Phys Watchpoint Exception", regs);
}

void do_paw_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Phys Watchpoint Exception", regs);
}

void do_vaw(struct pt_regs *regs)
{
	die_if_kernel("TL0: Virt Watchpoint Exception", regs);
}

void do_vaw_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Virt Watchpoint Exception", regs);
}

void do_tof_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Tag Overflow Exception", regs);
}

#ifdef CONFIG_EC_FLUSH_TRAP
void cache_flush_trap(struct pt_regs *regs)
{
#ifndef __SMP__
	unsigned node = linux_cpus[get_cpuid()].prom_node;
#else
#error SMP not supported on sparc64 yet
#endif
	int size = prom_getintdefault(node, "ecache-size", 512*1024);
	int i, j;
	unsigned long addr, page_nr;

	regs->tpc = regs->tnpc;
	regs->tnpc = regs->tnpc + 4;
	if (!suser()) return;
	size >>= PAGE_SHIFT;
	addr = PAGE_OFFSET - PAGE_SIZE;
	for (i = 0; i < size; i++) {
		do {
			addr += PAGE_SIZE;
			page_nr = MAP_NR(addr);
			if (page_nr >= max_mapnr) {
				return;
			}
		} while (!PageReserved (mem_map + page_nr));
		/* E-Cache line size is 64B. Let us pollute it :)) */
		for (j = 0; j < PAGE_SIZE; j += 64)
			__asm__ __volatile__ ("ldx [%0 + %1], %%g1" : : "r" (j), "r" (addr) : "g1");
	}
}
#endif

void trap_init(void)
{
}

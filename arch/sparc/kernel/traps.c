/* $Id: traps.c,v 1.18 1995/11/25 00:58:47 davem Exp $
 * arch/sparc/kernel/traps.c
 *
 * Copyright 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/*
 * I hate traps on the sparc, grrr...
 */

#include <linux/sched.h>  /* for jiffies */
#include <linux/kernel.h>

#include <asm/delay.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mp.h>
#include <asm/kdebug.h>

void
syscall_trace_entry(struct pt_regs *regs)
{
	printk("%s[%d]: sys[%d](%d, %d, %d, %d) ",
	       current->comm, current->pid,
	       regs->u_regs[UREG_G1], regs->u_regs[UREG_I0],
	       regs->u_regs[UREG_I1], regs->u_regs[UREG_I2],
	       regs->u_regs[UREG_I3]);
	return;
}

void
syscall_trace_exit(struct pt_regs *regs)
{
	printk("retvals[%d,%d] at pc<%08lx>\n",
	       regs->u_regs[UREG_I0], regs->u_regs[UREG_I1],
	       regs->pc, regs->npc);
	return;
}

void
do_cwp_assertion_failure(struct pt_regs *regs, unsigned long psr)
{
	printk("CWP return from trap assertion fails:\n");
	printk("Current psr %08lx, new psr %08lx\n", psr, regs->psr);
	show_regs(regs);
	panic("bogus CWP");
}

void
do_hw_interrupt(unsigned long type, unsigned long psr, unsigned long pc)
{

  printk("Unimplemented Sparc TRAP, type = %02lx psr = %08lx pc = %08lx\n",
	 type, psr, pc);
  halt();

  return;
}

void
do_illegal_instruction(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Illegal instruction at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	if(psr & PSR_PS)
		panic("Kernel illegal instruction, how are ya!");
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_ILLINST;
	send_sig(SIGILL, current, 1);
	return;
}

void
do_priv_instruction(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Privileged instruction at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGILL, current, 1);
	return;
}

/* XXX User may want to be allowed to do this. XXX */

void
do_memaccess_unaligned(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Unaligned memory access at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	if(regs->psr & PSR_PS)
		panic("Kernel does unaligned memory access, yuck!");
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGBUS, current, 1);
	return;
}

void
do_fpd_trap(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	/* Sanity check... */
	if(psr & PSR_PS)
		panic("FPE disabled trap from kernel, die die die...");

	put_psr(get_psr() | PSR_EF);    /* Allow FPU ops. */
	if(last_task_used_math == current) {
		/* No state save necessary */
		regs->psr |= PSR_EF;
		return;
	}
	if(last_task_used_math) {
		/* Other processes fpu state, save away */
		__asm__ __volatile__("st %%fsr, [%0]\n\t" : :
				     "r" (&current->tss.fsr) : "memory");

		/* Save away the floating point queue if necessary. */
		if(current->tss.fsr & 0x2000)
			__asm__ __volatile__("mov 0x0, %%g2\n\t"
					     "1: std %%fq, [%2 + %%g2]\n\t"
					     "st %%fsr, [%0]\n\t"
					     "ld [%0], %%g3\n\t"
					     "andcc %%g3, %1, %%g0\n\t"
					     "bne 1b\n\t"
					     "add %%g2, 0x8, %%g2\n\t"
					     "srl %%g2, 0x3, %%g2\n\t"
					     "st %%g2, [%3]\n\t" : :
					     "r" (&current->tss.fsr), "r" (0x2000),
					     "r" (&current->tss.fpqueue[0]),
					     "r" (&current->tss.fpqdepth) :
					     "g2", "g3", "memory");
		else
			current->tss.fpqdepth = 0;

		__asm__ __volatile__("std %%f0, [%0 + 0x00]\n\t"
				     "std %%f2, [%0 + 0x08]\n\t"
				     "std %%f4, [%0 + 0x10]\n\t"
				     "std %%f6, [%0 + 0x18]\n\t"
				     "std %%f8, [%0 + 0x20]\n\t"
				     "std %%f10, [%0 + 0x28]\n\t"
				     "std %%f12, [%0 + 0x30]\n\t"
				     "std %%f14, [%0 + 0x38]\n\t"
				     "std %%f16, [%0 + 0x40]\n\t"
				     "std %%f18, [%0 + 0x48]\n\t"
				     "std %%f20, [%0 + 0x50]\n\t"
				     "std %%f22, [%0 + 0x58]\n\t"
				     "std %%f24, [%0 + 0x60]\n\t"
				     "std %%f26, [%0 + 0x68]\n\t"
				     "std %%f28, [%0 + 0x70]\n\t"
				     "std %%f30, [%0 + 0x78]\n\t" : :
				     "r" (&current->tss.float_regs[0]) :
				     "memory");
	}
	last_task_used_math = current;
	if(current->used_math) {
		/* Restore the old state. */
		__asm__ __volatile__("ldd [%0 + 0x00], %%f0\n\t"
				     "ldd [%0 + 0x08], %%f2\n\t"
				     "ldd [%0 + 0x10], %%f4\n\t"
				     "ldd [%0 + 0x18], %%f6\n\t"
				     "ldd [%0 + 0x20], %%f8\n\t"
				     "ldd [%0 + 0x28], %%f10\n\t"
				     "ldd [%0 + 0x30], %%f12\n\t"
				     "ldd [%0 + 0x38], %%f14\n\t"
				     "ldd [%0 + 0x40], %%f16\n\t"
				     "ldd [%0 + 0x48], %%f18\n\t"
				     "ldd [%0 + 0x50], %%f20\n\t"
				     "ldd [%0 + 0x58], %%f22\n\t"
				     "ldd [%0 + 0x60], %%f24\n\t"
				     "ldd [%0 + 0x68], %%f26\n\t"
				     "ldd [%0 + 0x70], %%f28\n\t"
				     "ldd [%0 + 0x78], %%f30\n\t"
				     "ld  [%1], %%fsr\n\t" : :
				     "r" (&current->tss.float_regs[0]),
				     "r" (&current->tss.fsr));
	} else {
		/* Set initial sane state. */
		auto unsigned long init_fsr = 0x0UL;
		auto unsigned long init_fregs[32] __attribute__ ((aligned (8))) =
		{ ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL,
		  ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL,
		  ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL,
		  ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL };
		__asm__ __volatile__("ldd [%0 + 0x00], %%f0\n\t"
				     "ldd [%0 + 0x08], %%f2\n\t"
				     "ldd [%0 + 0x10], %%f4\n\t"
				     "ldd [%0 + 0x18], %%f6\n\t"
				     "ldd [%0 + 0x20], %%f8\n\t"
				     "ldd [%0 + 0x28], %%f10\n\t"
				     "ldd [%0 + 0x30], %%f12\n\t"
				     "ldd [%0 + 0x38], %%f14\n\t"
				     "ldd [%0 + 0x40], %%f16\n\t"
				     "ldd [%0 + 0x48], %%f18\n\t"
				     "ldd [%0 + 0x50], %%f20\n\t"
				     "ldd [%0 + 0x58], %%f22\n\t"
				     "ldd [%0 + 0x60], %%f24\n\t"
				     "ldd [%0 + 0x68], %%f26\n\t"
				     "ldd [%0 + 0x70], %%f28\n\t"
				     "ldd [%0 + 0x78], %%f30\n\t"
				     "ld  [%1], %%fsr\n\t" : :
				     "r" (&init_fregs[0]),
				     "r" (&init_fsr) : "memory");
		current->used_math = 1;
	}
}

void
do_fpe_trap(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	if(psr & PSR_PS)
		panic("FPE exception trap from kernel, die die die...");
	/* XXX Do something real... XXX */
	regs->psr &= ~PSR_EF;
	last_task_used_math = (struct task_struct *) 0;
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_FPERROR; /* as good as any */
	send_sig(SIGFPE, current, 1);
	return;
}

void
handle_tag_overflow(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Tag overflow trap at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	if(psr & PSR_PS)
		panic("KERNEL tag overflow trap, wowza!");
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_TAG; /* as good as any */
	send_sig(SIGEMT, current, 1);
	return;
}

void
handle_watchpoint(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Watchpoint detected at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	if(psr & PSR_PS)
		panic("Tell me what a watchpoint trap is, and I'll then deal "
		      "with such a beast...");
	return;
}

void
handle_reg_access(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Register Access Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
handle_cp_disabled(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Co-Processor disabled trap at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
handle_bad_flush(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Unimplemented FLUSH Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
handle_cp_exception(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Co-Processor Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
handle_hw_divzero(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Divide By Zero Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void do_ast(struct pt_regs *regs)
{
	panic("Don't know how to handle AST traps yet ;-(\n");
	return;
}

/* Since we have our mappings set up, on multiprocessors we can spin them
 * up here so that timer interrupts work during initialization.
 */

extern void sparc_cpu_startup(void);

extern int linux_num_cpus;
extern pgd_t *lnx_root;

int linux_smp_still_initting;
unsigned int thiscpus_tbr;
int thiscpus_mid;

void
trap_init(void)
{
	struct linux_prom_registers ctx_reg;
	int i;

	if(linux_num_cpus == 1) {
		printk("trap_init: Uniprocessor detected.\n");
		return;
	}
	if(sparc_cpu_model != sun4m) {
		prom_printf("trap_init: Multiprocessor on a non-sun4m! Aieee...\n");
		prom_printf("trap_init: Cannot continue, bailing out.\n");
		prom_halt();
	}
	/* Ok, we are on a sun4m with multiple cpu's */
	prom_printf("trap_init: Multiprocessor detected, initiating CPU-startup. cpus=%d\n",
	       linux_num_cpus);
	linux_smp_still_initting = 1;
	ctx_reg.which_io = 0x0;  /* real ram */
	ctx_reg.phys_addr = (char *) (((unsigned long) lnx_root) - PAGE_OFFSET);
	ctx_reg.reg_size = 0x0;
	/* This basically takes every cpu, loads up our Linux context table
	 * into it's context table pointer register, inits it at the low level
	 * and then makes it spin in an endless loop...
	 */
	for(i=0; i<linux_num_cpus; i++) {
		if((linux_cpus[i].mid & (~8)) != 0x0) {
			static int cpuid = 0;
			cpuid = (linux_cpus[i].mid & (~8));
			percpu_table[cpuid].cpu_is_alive = 0;
			thiscpus_mid = linux_cpus[i].mid;
			thiscpus_tbr = (unsigned int)
				percpu_table[cpuid].trap_table;
			prom_startcpu(linux_cpus[i].prom_node, &ctx_reg, 0x0,
				      (char *) sparc_cpu_startup);
			prom_printf("Waiting for cpu %d to start up...\n", i);
			while(percpu_table[cpuid].cpu_is_alive == 0) {
				static int counter = 0;
				counter++;
				if(counter>200)
					break;
				__delay(200000);
			}
		}
	}

	linux_smp_still_initting = 1;

	return;
}

void
die_if_kernel(char * str, struct pt_regs * regs, long err)
{
  return;
}

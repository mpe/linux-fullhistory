/* $Id: traps.c,v 1.32 1996/03/01 07:16:08 davem Exp $
 * arch/sparc/kernel/traps.c
 *
 * Copyright 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/*
 * I hate traps on the sparc, grrr...
 */

#include <linux/sched.h>  /* for jiffies */
#include <linux/kernel.h>
#include <linux/signal.h>

#include <asm/delay.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mp.h>
#include <asm/kdebug.h>
#include <asm/unistd.h>

struct trap_trace_entry {
	unsigned long pc;
	unsigned long type;
};

int trap_curbuf = 0;
struct trap_trace_entry trapbuf[1024];

void syscall_trace_entry(struct pt_regs *regs)
{
	printk("%s[%d]: ", current->comm, current->pid);
	printk("scall<%d> (could be %d)\n", (int) regs->u_regs[UREG_G1],
	       (int) regs->u_regs[UREG_I0]);
}

void syscall_trace_exit(struct pt_regs *regs)
{
}

void die_if_kernel(char *str, struct pt_regs *regs)
{
	unsigned long i;
	unsigned long *pc;

	if(regs->psr & PSR_PS)
		do_exit(SIGKILL);
	printk("%s(%d): %s\n", current->comm, current->pid, str);
	show_regs(regs);
	printk("Instruction DUMP:");
	pc = (unsigned long *) regs->pc;
	for(i = -3; i < 6; i++)
		printk("%c%08lx%c",i?' ':'<',pc[i],i?' ':'>');
	printk("\n");
	do_exit(SIGSEGV);
}

void do_hw_interrupt(unsigned long type, unsigned long psr, unsigned long pc)
{
	if(type < 0x80) {
		/* Sun OS's puke from bad traps, Linux survives! */
		printk("Unimplemented Sparc TRAP, type = %02lx\n", type);
		panic("Whee... Hello Mr. Penguin");
	}	
	current->tss.sig_desc = SUBSIG_BADTRAP(type - 0x80);
	current->tss.sig_address = pc;
	send_sig(SIGILL, current, 1);
}

void do_illegal_instruction(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			    unsigned long psr)
{
	if(psr & PSR_PS)
		die_if_kernel("Kernel illegal instruction", regs);
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_ILLINST;
	send_sig(SIGILL, current, 1);
}

void do_priv_instruction(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			 unsigned long psr)
{
	if(psr & PSR_PS)
		die_if_kernel("Penguin instruction from Penguin mode??!?!", regs);
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGILL, current, 1);
}

/* XXX User may want to be allowed to do this. XXX */

void do_memaccess_unaligned(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			    unsigned long psr)
{
	if(regs->psr & PSR_PS)
		die_if_kernel("Kernel MNA access", regs);
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_PRIVINST;
	send_sig(SIGBUS, current, 1);
}

extern void fpsave(unsigned long *fpregs, unsigned long *fsr,
		   void *fpqueue, unsigned long *fpqdepth);
extern void fpload(unsigned long *fpregs, unsigned long *fsr);

static unsigned long init_fsr = 0x0UL;
static unsigned long init_fregs[32] __attribute__ ((aligned (8))) =
                { ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL,
		  ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL,
		  ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL,
		  ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL, ~0UL };

void do_fpd_trap(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		 unsigned long psr)
{
	/* Sanity check... */
	if(psr & PSR_PS)
		die_if_kernel("Kernel gets Penguin-FPU disabled trap", regs);

	put_psr(get_psr() | PSR_EF);    /* Allow FPU ops. */
	regs->psr |= PSR_EF;
	if(last_task_used_math == current)
		return;
	if(last_task_used_math) {
		/* Other processes fpu state, save away */
		struct task_struct *fptask = last_task_used_math;
		fpsave(&fptask->tss.float_regs[0], &fptask->tss.fsr,
		       &fptask->tss.fpqueue[0], &fptask->tss.fpqdepth);
	}
	last_task_used_math = current;
	if(current->used_math) {
		fpload(&current->tss.float_regs[0], &current->tss.fsr);
	} else {
		/* Set initial sane state. */
		fpload(&init_fregs[0], &init_fsr);
		current->used_math = 1;
	}
}

static unsigned long fake_regs[32] __attribute__ ((aligned (8)));
static unsigned long fake_fsr;
static unsigned long fake_queue[32] __attribute__ ((aligned (8)));
static unsigned long fake_depth;

void do_fpe_trap(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		 unsigned long psr)
{
	static calls = 0;
	struct task_struct *fpt = last_task_used_math;

	put_psr(get_psr() | PSR_EF);
	/* If nobody owns the fpu right now, just clear the
	 * error into our fake static buffer and hope it don't
	 * happen again.  Thank you crashme...
	 */
	if(!fpt) {
		fpsave(&fake_regs[0], &fake_fsr, &fake_queue[0], &fake_depth);
		regs->psr &= ~PSR_EF;
		return;
	}
	fpsave(&fpt->tss.float_regs[0], &fpt->tss.fsr,
	       &fpt->tss.fpqueue[0], &fpt->tss.fpqdepth);
	last_task_used_math->tss.sig_address = pc;
	last_task_used_math->tss.sig_desc = SUBSIG_FPERROR; /* as good as any */
	if(psr & PSR_PS) {
		/* The first fsr store/load we tried trapped,
		 * the second one will not (we hope).
		 */
		printk("WARNING: FPU exception from kernel mode. at pc=%08lx\n",
		       regs->pc);
		regs->pc = regs->npc;
		regs->npc += 4;
		calls++;
		if(calls > 2)
			die_if_kernel("Too many Penguin-FPU traps from kernel mode",
				      regs);
		return;
	}
	send_sig(SIGFPE, last_task_used_math, 1);
	last_task_used_math = NULL;
	regs->psr &= ~PSR_EF;
	if(calls > 0)
		calls=0;
}

void handle_tag_overflow(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			 unsigned long psr)
{
	if(psr & PSR_PS)
		die_if_kernel("Penguin overflow trap from kernel mode", regs);
	current->tss.sig_address = pc;
	current->tss.sig_desc = SUBSIG_TAG; /* as good as any */
	send_sig(SIGEMT, current, 1);
}

void handle_watchpoint(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Watchpoint detected at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	if(psr & PSR_PS)
		panic("Tell me what a watchpoint trap is, and I'll then deal "
		      "with such a beast...");
}

void handle_reg_access(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Register Access Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	send_sig(SIGILL, current, 1);
}

void handle_cp_disabled(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			unsigned long psr)
{
	send_sig(SIGILL, current, 1);
}

void handle_bad_flush(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		      unsigned long psr)
{
	printk("Unimplemented FLUSH Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	send_sig(SIGILL, current, 1);
}

void handle_cp_exception(struct pt_regs *regs, unsigned long pc, unsigned long npc,
			 unsigned long psr)
{
	printk("Co-Processor Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	send_sig(SIGILL, current, 1);
}

void handle_hw_divzero(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Divide By Zero Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	send_sig(SIGILL, current, 1);
}

/* Since we have our mappings set up, on multiprocessors we can spin them
 * up here so that timer interrupts work during initialization.
 */

extern void sparc_cpu_startup(void);

extern int linux_num_cpus;
extern pgd_t **srmmu_context_table;

int linux_smp_still_initting;
unsigned int thiscpus_tbr;
int thiscpus_mid;

void trap_init(void)
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
	ctx_reg.phys_addr = (char *) (((unsigned long) srmmu_context_table) - PAGE_OFFSET);
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
}

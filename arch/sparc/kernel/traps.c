/*
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
	halt();
	return;
}

void
do_priv_instruction(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Privileged instruction at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
do_memaccess_unaligned(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Unaligned memory access at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
do_fpd_trap(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Floating Point Disabled trap at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
do_fpe_trap(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Floating Point Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
handle_tag_overflow(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Tag overflow trap at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
handle_watchpoint(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Watchpoint detected at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
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
handle_iacc_error(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Instruction Access Error at PC %08lx NPC %08lx PSR %08lx\n",
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
handle_dacc_error(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Data Access Error at PC %08lx NPC %08lx PSR %08lx\n",
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

void
handle_dstore_error(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Data Store Error at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
handle_dacc_mmu_miss(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Data Access MMU-Miss Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
	return;
}

void
handle_iacc_mmu_miss(struct pt_regs *regs, unsigned long pc, unsigned long npc,
		       unsigned long psr)
{
	printk("Instruction Access MMU-Miss Exception at PC %08lx NPC %08lx PSR %08lx\n",
	       pc, npc, psr);
	halt();
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

/* #define SMP_TESTING */

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
		printk("trap_init: Multiprocessor on a non-sun4m! Aieee...\n");
		printk("trap_init: Cannot continue, bailing out.\n");
		prom_halt();
	}
	/* Ok, we are on a sun4m with multiple cpu's */
	printk("trap_init: Multiprocessor detected, initiating CPU-startup. cpus=%d\n",
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
#ifdef SMP_TESTING
			printk("thiscpus_tbr = %08x\n", thiscpus_tbr);
			printk("About to fire up cpu %d mid %d cpuid %d\n", i,
			       linux_cpus[i].mid, cpuid);
#endif
			prom_startcpu(linux_cpus[i].prom_node, &ctx_reg, 0x0,
				      (char *) sparc_cpu_startup);
			printk("Waiting for cpu %d to start up...\n", i);
			while(percpu_table[cpuid].cpu_is_alive == 0) {
				static int counter = 0;
				counter++;
				if(counter>200) {
#ifdef SMP_TESTING
					printk("UGH, CPU would not start up ;-( \n");
#endif
					break;
				}
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

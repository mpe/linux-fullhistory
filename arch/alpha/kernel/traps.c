/*
 * kernel/traps.c
 *
 * (C) Copyright 1994 Linus Torvalds
 */

/*
 * This file initializes the trap entry points
 */

#include <linux/sched.h>

void die_if_kernel(char * str, struct pt_regs * regs, long err)
{
	unsigned long i;

	printk("%s %ld\n", str, err);
	printk("PC = %016lx PS = %04lx\n", regs->pc, regs->ps);
	for (i = 0 ; i < 5000000000 ; i++)
		/* pause */;
	halt();
}

asmlinkage void do_entArith(unsigned long summary, unsigned long write_mask, unsigned long a2, struct pt_regs * regs)
{
	printk("Arithmetic trap: %02lx %016lx\n", summary, write_mask);
	die_if_kernel("Arithmetic fault", regs, 0);
}

asmlinkage void do_entIF(unsigned long type, unsigned long a1, unsigned long a2, struct pt_regs * regs)
{
	die_if_kernel("Instruction fault", regs, type);
}

asmlinkage void do_entUna(unsigned long va, unsigned long opcode, unsigned long reg, struct pt_regs * regs)
{
	printk("Unaligned trap: %016lx %ld %ld\n", va, opcode, reg);
	die_if_kernel("Unaligned", regs, 0);
}

extern asmlinkage void entMM(void);
extern asmlinkage void entIF(void);
extern asmlinkage void entArith(void);
extern asmlinkage void entUna(void);

void trap_init(void)
{
	unsigned long gptr;

	__asm__("br %0,___tmp\n"
		"___tmp:\tldgp %0,0(%0)"
		: "=r" (gptr));
	wrkgp(gptr);

	wrent(entArith, 1);
	wrent(entMM, 2);
	wrent(entIF, 3);
	wrent(entUna, 4);
}

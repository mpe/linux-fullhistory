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
	printk("pc = %016lx ps = %04lx\n", regs->pc, regs->ps);
	printk("rp = %016lx sp = %p\n", regs->r26, regs+1);
	for (i = 0 ; i < 5000000000 ; i++)
		/* pause */;
	halt();
}

asmlinkage void do_entArith(unsigned long summary, unsigned long write_mask,
	unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	printk("Arithmetic trap: %02lx %016lx\n", summary, write_mask);
	die_if_kernel("Arithmetic fault", &regs, 0);
}

asmlinkage void do_entIF(unsigned long type, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	die_if_kernel("Instruction fault", &regs, type);
}

asmlinkage void do_entUna(unsigned long va, unsigned long opcode, unsigned long reg,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	printk("Unaligned trap: %016lx %ld %ld\n", va, opcode, reg);
	die_if_kernel("Unaligned", &regs, 0);
}

/*
 * DEC means people to use the "retsys" instruction for return from
 * a system call, but they are clearly misguided about this. We use
 * "rti" in all cases, and fill in the stack with the return values.
 * That should make signal handling etc much cleaner.
 *
 * Even more horribly, DEC doesn't allow system calls from kernel mode.
 * "Security" features letting the user do something the kernel can't
 * are a thinko. DEC palcode is strange. The PAL-code designers probably
 * got terminally tainted by VMS at some point.
 */
asmlinkage void do_entSys(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5, struct pt_regs regs)
{
	printk("System call %ld(%ld,%ld)\n", regs.r0, a0, a1);
	die_if_kernel("Syscall", &regs, 0);
}

extern asmlinkage void entMM(void);
extern asmlinkage void entIF(void);
extern asmlinkage void entArith(void);
extern asmlinkage void entUna(void);
extern asmlinkage void entSys(void);

void trap_init(void)
{
	unsigned long gptr;

	/*
	 * Tell PAL-code what global pointer we want in the kernel..
	 */
	__asm__("br %0,___tmp\n"
		"___tmp:\tldgp %0,0(%0)"
		: "=r" (gptr));
	wrkgp(gptr);

	wrent(entArith, 1);
	wrent(entMM, 2);
	wrent(entIF, 3);
	wrent(entUna, 4);
	wrent(entSys, 5);
}

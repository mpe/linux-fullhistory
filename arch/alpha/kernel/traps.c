/*
 * kernel/traps.c
 *
 * (C) Copyright 1994 Linus Torvalds
 */

/*
 * This file initializes the trap entry points
 */

#include <linux/sched.h>
#include <linux/tty.h>

#include <asm/unaligned.h>

void die_if_kernel(char * str, struct pt_regs * regs, long err)
{
	long i;
	unsigned long sp;
	unsigned int * pc;

	printk("%s(%d): %s %ld\n", current->comm, current->pid, str, err);
	sp = (unsigned long) (regs+1);
	if (regs->ps & 8)
		sp = rdusp();
	printk("pc = %lx ps = %04lx\n", regs->pc, regs->ps);
	printk("rp = %lx sp = %lx\n", regs->r26, sp);
	printk("r0=%lx r1=%lx r2=%lx r3=%lx\n",
		regs->r0, regs->r1, regs->r2, regs->r3);
	printk("r8=%lx\n", regs->r8);
	printk("r16=%lx r17=%lx r18=%lx r19=%lx\n",
		regs->r16, regs->r17, regs->r18, regs->r19);
	printk("r20=%lx r21=%lx r22=%lx r23=%lx\n",
		regs->r20, regs->r21, regs->r22, regs->r23);
	printk("r24=%lx r25=%lx r26=%lx r27=%lx\n",
		regs->r24, regs->r25, regs->r26, regs->r27);
	printk("r28=%lx r29=%lx r30=%lx\n",
		regs->r28, regs->gp, sp);
	if (regs->ps & 8)
		return;
	printk("Code:");
	pc = (unsigned int *) regs->pc;
	for (i = -3; i < 6; i++)
		printk("%c%08x%c",i?' ':'<',pc[i],i?' ':'>');
	printk("\n");
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

/*
 * entUna has a different register layout to be reasonably simple. It
 * needs access to all the integer registers (the kernel doesn't use
 * fp-regs), and it needs to have them in order for simpler access.
 *
 * Due to the non-standard register layout (and because we don't want
 * to handle floating-point regs), we disallow user-mode unaligned
 * accesses (we'd need to do "verify_area()" checking, as well as
 * do a full "ret_from_sys_call" return).
 *
 * Oh, btw, we don't handle the "gp" register correctly, but if we fault
 * on a gp-register unaligned load/store, something is _very_ wrong
 * in the kernel anyway..
 */
struct allregs {
	unsigned long regs[32];
	unsigned long ps, pc, gp, a0, a1, a2;
};

asmlinkage void do_entUna(void * va, unsigned long opcode, unsigned long reg,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct allregs regs)
{
	static int cnt = 0;

	if (regs.ps & 8)
		do_exit(SIGSEGV);
	if (++cnt < 5)
		printk("Unaligned trap at %016lx: %p %lx %ld\n",
			regs.pc, va, opcode, reg);
	/* $16-$18 are PAL-saved, and are offset by 19 entries */
	if (reg >= 16 && reg <= 18)
		reg += 19;
	switch (opcode) {
		case 0x28: /* ldl */
			*(reg+regs.regs) = (int) ldl_u(va);
			return;
		case 0x29: /* ldq */
			*(reg+regs.regs) = ldq_u(va);
			return;
		case 0x2c: /* stl */
			stl_u(*(reg+regs.regs), va);
			return;
		case 0x2d: /* stq */
			stq_u(*(reg+regs.regs), va);
			return;
	}
	printk("Bad unaligned kernel access at %016lx: %p %lx %ld\n",
		regs.pc, va, opcode, reg);
	do_exit(SIGSEGV);
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
asmlinkage long do_entSys(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5, struct pt_regs regs)
{
	printk("<sc %ld(%lx,%lx,%lx)>", regs.r0, a0, a1, a2);
	return -1;
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

/*
 *  include/asm-i386/bugs.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 */

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

#define CONFIG_BUGi386

static void no_halt(char *s, int *ints)
{
	hlt_works_ok = 0;
}

static void no_387(char *s, int *ints)
{
	hard_math = 0;
	__asm__("movl %%cr0,%%eax\n\t"
		"orl $0xE,%%eax\n\t"
		"movl %%eax,%%cr0\n\t" : : : "ax");
}

static char fpu_error = 0;

static void copro_timeout(void)
{
	fpu_error = 1;
	timer_table[COPRO_TIMER].expires = jiffies+100;
	timer_active |= 1<<COPRO_TIMER;
	printk("387 failed: trying to reset\n");
	send_sig(SIGFPE, last_task_used_math, 1);
	outb_p(0,0xf1);
	outb_p(0,0xf0);
}

static void check_fpu(void)
{
	static double x = 4195835.0;
	static double y = 3145727.0;
	unsigned short control_word;

	if (!hard_math) {
#ifndef CONFIG_MATH_EMULATION
		printk("No coprocessor found and no math emulation present.\n");
		printk("Giving up.\n");
		for (;;) ;
#endif
		return;
	}
	/*
	 * check if exception 16 works correctly.. This is truly evil
	 * code: it disables the high 8 interrupts to make sure that
	 * the irq13 doesn't happen. But as this will lead to a lockup
	 * if no exception16 arrives, it depends on the fact that the
	 * high 8 interrupts will be re-enabled by the next timer tick.
	 * So the irq13 will happen eventually, but the exception 16
	 * should get there first..
	 */
	printk("Checking 386/387 coupling... ");
	timer_table[COPRO_TIMER].expires = jiffies+50;
	timer_table[COPRO_TIMER].fn = copro_timeout;
	timer_active |= 1<<COPRO_TIMER;
	__asm__("clts ; fninit ; fnstcw %0 ; fwait":"=m" (*&control_word));
	control_word &= 0xffc0;
	__asm__("fldcw %0 ; fwait": :"m" (*&control_word));
	outb_p(inb_p(0x21) | (1 << 2), 0x21);
	__asm__("fldz ; fld1 ; fdiv %st,%st(1) ; fwait");
	timer_active &= ~(1<<COPRO_TIMER);
	if (fpu_error)
		return;
	if (!ignore_irq13) {
		printk("Ok, fpu using old IRQ13 error reporting\n");
		return;
	}
	__asm__("fninit\n\t"
		"fldl %1\n\t"
		"fdivl %2\n\t"
		"fmull %2\n\t"
		"fldl %1\n\t"
		"fsubp %%st,%%st(1)\n\t"
		"fistpl %0\n\t"
		"fwait\n\t"
		"fninit"
		: "=m" (*&fdiv_bug)
		: "m" (*&x), "m" (*&y));
	if (!fdiv_bug) {
		printk("Ok, fpu using exception 16 error reporting.\n");
		return;

	}
	printk("Hmm, FDIV bug i%c86 system\n", '0'+x86);
}

static void check_hlt(void)
{
	printk("Checking 'hlt' instruction... ");
	if (!hlt_works_ok) {
		printk("disabled\n");
		return;
	}
	__asm__ __volatile__("hlt ; hlt ; hlt ; hlt");
	printk("Ok.\n");
}

static void check_bugs(void)
{
	check_fpu();
	check_hlt();
	system_utsname.machine[1] = '0' + x86;
}

/*
 *  include/asm-i386/bugs.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 *
 *  Cyrix stuff, June 1998 by:
 *	- Rafael R. Reilova (moved everything from head.S),
 *	- Channing Corn (tests & fixes),
 *	- Andrew D. Balsa (code cleanup).
 */

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

#include <linux/config.h>
#include <asm/processor.h>

#define CONFIG_BUGi386

__initfunc(static void no_halt(char *s, int *ints))
{
	boot_cpu_data.hlt_works_ok = 0;
}

__initfunc(static void no_387(char *s, int *ints))
{
	boot_cpu_data.hard_math = 0;
	__asm__("movl %%cr0,%%eax\n\t"
		"orl $0xE,%%eax\n\t"
		"movl %%eax,%%cr0\n\t" : : : "ax");
}

static char __initdata fpu_error = 0;

__initfunc(static void copro_timeout(void))
{
	fpu_error = 1;
	timer_table[COPRO_TIMER].expires = jiffies+100;
	timer_active |= 1<<COPRO_TIMER;
	printk(KERN_ERR "387 failed: trying to reset\n");
	send_sig(SIGFPE, current, 1);
	outb_p(0,0xf1);
	outb_p(0,0xf0);
}

static double __initdata x = 4195835.0;
static double __initdata y = 3145727.0;

__initfunc(static void check_fpu(void))
{
	unsigned short control_word;

	if (!boot_cpu_data.hard_math) {
#ifndef CONFIG_MATH_EMULATION
		printk(KERN_EMERG "No coprocessor found and no math emulation present.\n");
		printk(KERN_EMERG "Giving up.\n");
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
	printk(KERN_INFO "Checking 386/387 coupling... ");
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
		printk("OK, FPU using old IRQ 13 error reporting\n");
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
		: "=m" (*&boot_cpu_data.fdiv_bug)
		: "m" (*&x), "m" (*&y));
	if (!boot_cpu_data.fdiv_bug)
		printk("OK, FPU using exception 16 error reporting.\n");
	else
		printk("Hmm, FPU using exception 16 error reporting with FDIV bug.\n");
}

__initfunc(static void check_hlt(void))
{
	printk(KERN_INFO "Checking 'hlt' instruction... ");
	if (!boot_cpu_data.hlt_works_ok) {
		printk("disabled\n");
		return;
	}
	__asm__ __volatile__("hlt ; hlt ; hlt ; hlt");
	printk("OK.\n");
}

__initfunc(static void check_tlb(void))
{
#ifndef CONFIG_M386
	/*
	 * The 386 chips don't support TLB finegrained invalidation.
	 * They will fault when they hit an invlpg instruction.
	 */
	if (boot_cpu_data.x86 == 3) {
		printk(KERN_EMERG "CPU is a 386 and this kernel was compiled for 486 or better.\n");
		printk("Giving up.\n");
		for (;;) ;
	}
#endif
}

/*
 *	Most 386 processors have a bug where a POPAD can lock the 
 *	machine even from user space.
 */
 
__initfunc(static void check_popad(void))
{
#ifdef CONFIG_M386
	int res, inp = (int) &res;

	printk(KERN_INFO "Checking for popad bug... ");
	__asm__ __volatile__( 
	  "movl $12345678,%%eax; movl $0,%%edi; pusha; popa; movl (%%edx,%%edi),%%ecx "
	  : "=eax" (res)
	  : "edx" (inp)
	  : "eax", "ecx", "edx", "edi" );
	/* If this fails, it means that any user program may lock the CPU hard. Too bad. */
	if (res != 12345678) printk( "Buggy.\n" );
		        else printk( "OK.\n" );
#endif
}

/*
 *	B step AMD K6 before B 9730xxxx have hardware bugs that can cause
 *	misexecution of code under Linux. Owners of such processors should
 *	contact AMD for precise details and a CPU swap.
 *
 *	See	http://www.mygale.com/~poulot/k6bug.html
 *		http://www.amd.com/K6/k6docs/revgd.html
 *
 *	The following test is erm.. interesting. AMD neglected to up
 *	the chip setting when fixing the bug but they also tweaked some
 *	performance at the same time..
 */
 
extern void vide(void);
__asm__(".align 4\nvide: ret");

__initfunc(static void check_amd_k6(void))
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
	    boot_cpu_data.x86_model == 6 &&
	    boot_cpu_data.x86_mask == 1)
	{
		int n;
		void (*f_vide)(void);
		unsigned long d, d2;

		printk(KERN_INFO "AMD K6 stepping B detected - ");

#define K6_BUG_LOOP 1000000

		/*
		 * It looks like AMD fixed the 2.6.2 bug and improved indirect 
		 * calls at the same time.
		 */

		n = K6_BUG_LOOP;
		f_vide = vide;
		__asm__ ("rdtsc" : "=a" (d));
		while (n--) 
			f_vide();
		__asm__ ("rdtsc" : "=a" (d2));
		d = d2-d;

		/* Knock these two lines out if it debugs out ok */
		printk(KERN_INFO "K6 BUG %ld %d (Report these if test report is incorrect)\n", d, 20*K6_BUG_LOOP);
		printk(KERN_INFO "AMD K6 stepping B detected - ");
		/* -- cut here -- */
		if (d > 20*K6_BUG_LOOP) 
			printk("system stability may be impaired when more than 32 MB are used.\n");
		else 
			printk("probably OK (after B9730xxxx).\n");
		printk(KERN_INFO "Please see http://www.mygale.com/~poulot/k6bug.html\n");
	}
}

/*
 * All current models of Pentium and Pentium with MMX technology CPUs
 * have the F0 0F bug, which lets nonpriviledged users lock up the system:
 */

extern void trap_init_f00f_bug(void);

__initfunc(static void check_pentium_f00f(void))
{
	/*
	 * Pentium and Pentium MMX
	 */
	boot_cpu_data.f00f_bug = 0;
	if (boot_cpu_data.x86 == 5 && boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) {
		printk(KERN_INFO "Intel Pentium with F0 0F bug - workaround enabled.\n");
		boot_cpu_data.f00f_bug = 1;
		trap_init_f00f_bug();
	}
}

/*
 * Perform the Cyrix 5/2 test. A Cyrix won't change
 * the flags, while other 486 chips will.
 */

static inline int test_cyrix_52div(void)
{
	unsigned int test;

	__asm__ __volatile__(
	     "sahf\n\t"		/* clear flags (%eax = 0x0005) */
	     "div %b2\n\t"	/* divide 5 by 2 */
	     "lahf"		/* store flags into %ah */
	     : "=a" (test)
	     : "0" (5), "q" (2)
	     : "cc");

	/* AH is 0x02 on Cyrix after the divide.. */
	return (unsigned char) (test >> 8) == 0x02;
}

/*
 * Cyrix CPUs without cpuid or with cpuid not yet enabled can be detected
 * by the fact that they preserve the flags across the division of 5/2.
 * PII and PPro exhibit this behavior too, but they have cpuid available.
 */

__initfunc(static void check_cyrix_cpu(void))
{
	if ((boot_cpu_data.cpuid_level == -1) && (boot_cpu_data.x86 == 4)
	    && test_cyrix_52div()) {

		/* default to an unknown Cx486, (we will differentiate later) */
		/* NOTE:  using 0xff since 0x00 is a valid DIR0 value */
		strcpy(boot_cpu_data.x86_vendor_id, "CyrixInstead");
		boot_cpu_data.x86_model = 0xff;
		boot_cpu_data.x86_mask = 0;
	}
}

/*
 * Fix two problems with the Cyrix 6x86 and 6x86L:
 *   -- the cpuid is disabled on power up, enable it, use it.
 *   -- the SLOP bit needs resetting on some motherboards due to old BIOS,
 *      so that the udelay loop calibration works well.  Recalibrate.
 */

extern void calibrate_delay(void) __init;

__initfunc(static void check_cx686_cpuid_slop(void))
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_CYRIX &&
	    (boot_cpu_data.x86_model & 0xf0) == 0x30) {  /* 6x86(L) */
		int dummy;
		unsigned char ccr3, ccr4, ccr5;

		cli();
		ccr3 = getCx86(CX86_CCR3);
		setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10);      /* enable MAPEN  */
		ccr4 = getCx86(CX86_CCR4);
		setCx86(CX86_CCR4, ccr4 | 0x80);               /* enable cpuid  */
		ccr5 = getCx86(CX86_CCR5);
		if (ccr5 & 2)   /* reset SLOP if needed, old BIOS do this wrong */
			setCx86(CX86_CCR5, ccr5 & 0xfd);
		setCx86(CX86_CCR3, ccr3);                      /* disable MAPEN */
		sti();

		boot_cpu_data.cpuid_level = 1;  /* should cover all 6x86(L) */
		boot_cpu_data.x86 = 5;

		/* we know we have level 1 available on the 6x86(L) */
		cpuid(1, &dummy, &dummy, &dummy,
		      &boot_cpu_data.x86_capability);
		/*
		 * DON'T use the x86_mask and x86_model from cpuid, these are
		 * not as accurate (or the same) as those from the DIR regs.
		 * already in place after cyrix_model() in setup.c
		 */

		if (ccr5 & 2) { /* possible wrong calibration done */
			printk(KERN_INFO "Recalibrating delay loop with SLOP bit reset\n");
			calibrate_delay();
			boot_cpu_data.loops_per_sec = loops_per_sec;
		}
	}
}

__initfunc(static void check_bugs(void))
{
	check_cyrix_cpu();
	identify_cpu(&boot_cpu_data);
#ifndef __SMP__
	printk("CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif
	check_cx686_cpuid_slop();
	check_tlb();
	check_fpu();
	check_hlt();
	check_popad();
	check_amd_k6();
	check_pentium_f00f();
	system_utsname.machine[1] = '0' + boot_cpu_data.x86;
}

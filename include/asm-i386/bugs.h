/*
 *  include/asm-i386/bugs.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 *
 *  Cyrix stuff, June 1998 by:
 *	- Rafael R. Reilova (moved everything from head.S),
 *        <rreilova@ececs.uc.edu>
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
#include <asm/msr.h>

#define CONFIG_BUGi386

static int __init no_halt(char *s)
{
	boot_cpu_data.hlt_works_ok = 0;
	return 1;
}

__setup("no-hlt", no_halt);

static int __init mca_pentium(char *s)
{
	mca_pentium_flag = 1;
	return 1;
}

__setup("mca-pentium", mca_pentium);

static int __init no_387(char *s)
{
	boot_cpu_data.hard_math = 0;
	write_cr0(0xE | read_cr0());
	return 1;
}

__setup("no387", no_387);

static char __initdata fpu_error = 0;

static void __init copro_timeout(void)
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

static void __init check_fpu(void)
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
	if (mca_pentium_flag) {
		/* The IBM Model 95 machines with pentiums lock up on
		 * fpu test, so we avoid it. All pentiums have inbuilt
		 * FPU and thus should use exception 16. We still do
		 * the FDIV test, although I doubt there where ever any
		 * MCA boxes built with non-FDIV-bug cpus.
		 */
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
		printk("mca-pentium specified, avoiding FPU coupling test... ");
		if (!boot_cpu_data.fdiv_bug)
			printk("??? No FDIV bug? Lucky you...\n");
		else
			printk("detected FDIV bug though.\n");
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
	timer_table[COPRO_TIMER].expires = jiffies+HZ/2;
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

static void __init check_hlt(void)
{
	printk(KERN_INFO "Checking 'hlt' instruction... ");
	if (!boot_cpu_data.hlt_works_ok) {
		printk("disabled\n");
		return;
	}
	__asm__ __volatile__("hlt ; hlt ; hlt ; hlt");
	printk("OK.\n");
}

/*
 *	Most 386 processors have a bug where a POPAD can lock the 
 *	machine even from user space.
 */
 
static void __init check_popad(void)
{
#ifndef CONFIG_X86_POPAD_OK
	int res, inp = (int) &res;

	printk(KERN_INFO "Checking for popad bug... ");
	__asm__ __volatile__( 
	  "movl $12345678,%%eax; movl $0,%%edi; pusha; popa; movl (%%edx,%%edi),%%ecx "
	  : "=&a" (res)
	  : "d" (inp)
	  : "ecx", "edi" );
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

static void __init check_amd_k6(void)
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
		rdtscl(d);
		while (n--) 
			f_vide();
		rdtscl(d2);
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

static void __init check_pentium_f00f(void)
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
 * Fix cpuid problems with Cyrix CPU's:
 *   -- on the Cx686(L) the cpuid is disabled on power up.
 *   -- braindamaged BIOS disable cpuid on the Cx686MX.
 */

extern unsigned char Cx86_dir0_msb;  /* exported HACK from cyrix_model() */

static void __init check_cx686_cpuid(void)
{
	if (boot_cpu_data.cpuid_level == -1 &&
	    ((Cx86_dir0_msb == 5) || (Cx86_dir0_msb == 3))) {
		int eax, dummy;
		unsigned char ccr3, ccr4;
		__u32 old_cap;

		cli();
		ccr3 = getCx86(CX86_CCR3);
		setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10); /* enable MAPEN  */
		ccr4 = getCx86(CX86_CCR4);
		setCx86(CX86_CCR4, ccr4 | 0x80);          /* enable cpuid  */
		setCx86(CX86_CCR3, ccr3);                 /* disable MAPEN */
		sti();

		/* we have up to level 1 available on the Cx6x86(L|MX) */
		boot_cpu_data.cpuid_level = 1;
		/*  Need to preserve some externally computed capabilities  */
		old_cap = boot_cpu_data.x86_capability & X86_FEATURE_MTRR;
		cpuid(1, &eax, &dummy, &dummy,
		      &boot_cpu_data.x86_capability);
		boot_cpu_data.x86_capability |= old_cap;

		boot_cpu_data.x86 = (eax >> 8) & 15;
		/*
 		 * we already have a cooked step/rev number from DIR1
		 * so we don't use the cpuid-provided ones.
		 */
	}
}

/*
 * Reset the slow-loop (SLOP) bit on the 686(L) which is set by some old
 * BIOSes for compatability with DOS games.  This makes the udelay loop
 * work correctly, and improves performance.
 */

extern void calibrate_delay(void) __init;

static void __init check_cx686_slop(void)
{
	if (Cx86_dir0_msb == 3) {
		unsigned char ccr3, ccr5;

		cli();
		ccr3 = getCx86(CX86_CCR3);
		setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10); /* enable MAPEN  */
		ccr5 = getCx86(CX86_CCR5);
		if (ccr5 & 2)
			setCx86(CX86_CCR5, ccr5 & 0xfd);  /* reset SLOP */
		setCx86(CX86_CCR3, ccr3);                 /* disable MAPEN */
		sti();

		if (ccr5 & 2) { /* possible wrong calibration done */
			printk(KERN_INFO "Recalibrating delay loop with SLOP bit reset\n");
			calibrate_delay();
			boot_cpu_data.loops_per_sec = loops_per_sec;
		}
	}
}

/*
 * Cyrix CPUs without cpuid or with cpuid not yet enabled can be detected
 * by the fact that they preserve the flags across the division of 5/2.
 * PII and PPro exhibit this behavior too, but they have cpuid available.
 */

static void __init check_cyrix_cpu(void)
{
	if ((boot_cpu_data.cpuid_level == -1) && (boot_cpu_data.x86 == 4)
	    && test_cyrix_52div()) {

		strcpy(boot_cpu_data.x86_vendor_id, "CyrixInstead");
	}
}
 
/*
 * In setup.c's cyrix_model() we have set the boot_cpu_data.coma_bug
 * on certain processors that we know contain this bug and now we
 * enable the workaround for it.
 */

static void __init check_cyrix_coma(void)
{
}
 
/*
 * Check wether we are able to run this kernel safely on SMP.
 *
 * - In order to run on a i386, we need to be compiled for i386
 *   (for due to lack of "invlpg" and working WP on a i386)
 * - In order to run on anything without a TSC, we need to be
 *   compiled for a i486.
 * - In order to work on a Pentium/SMP machine, we need to be
 *   compiled for a Pentium or lower, as a PPro config implies
 *   a properly working local APIC without the need to do extra
 *   reads from the APIC.
*/

static void __init check_config(void)
{
/*
 * We'd better not be a i386 if we're configured to use some
 * i486+ only features! (WP works in supervisor mode and the
 * new "invlpg" and "bswap" instructions)
 */
#if defined(CONFIG_X86_WP_WORKS_OK) || defined(CONFIG_X86_INVLPG) || defined(CONFIG_X86_BSWAP)
	if (boot_cpu_data.x86 == 3)
		panic("Kernel requires i486+ for 'invlpg' and other features");
#endif

/*
 * If we configured ourselves for a TSC, we'd better have one!
 */
#ifdef CONFIG_X86_TSC
	if (!(boot_cpu_data.x86_capability & X86_FEATURE_TSC))
		panic("Kernel compiled for Pentium+, requires TSC");
#endif

/*
 * If we were told we had a good APIC for SMP, we'd better be a PPro
 */
#if defined(CONFIG_X86_GOOD_APIC) && defined(CONFIG_SMP)
	if (smp_found_config && boot_cpu_data.x86 <= 5)
		panic("Kernel compiled for PPro+, assumes local APIC without read-before-write bug");
#endif
}

static void __init check_bugs(void)
{
	check_cyrix_cpu();
	identify_cpu(&boot_cpu_data);
	check_cx686_cpuid();
	check_cx686_slop();
#ifndef __SMP__
	printk("CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif
	check_config();
	check_fpu();
	check_hlt();
	check_popad();
	check_amd_k6();
	check_pentium_f00f();
	check_cyrix_coma();
	system_utsname.machine[1] = '0' + boot_cpu_data.x86;
}

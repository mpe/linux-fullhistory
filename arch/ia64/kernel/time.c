/*
 * linux/arch/ia64/kernel/time.c
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999-2000 David Mosberger <davidm@hpl.hp.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 * Copyright (C) 1999-2000 VA Linux Systems
 * Copyright (C) 1999-2000 Walt Drummond <drummond@valinux.com>
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>

#include <asm/delay.h>
#include <asm/efi.h>
#include <asm/irq.h>
#include <asm/machvec.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/system.h>

extern rwlock_t xtime_lock;
extern volatile unsigned long lost_ticks;

#ifdef CONFIG_IA64_DEBUG_IRQ

unsigned long last_cli_ip;

#endif

static struct {
	unsigned long delta;
	unsigned long next[NR_CPUS];
} itm;

static void
do_profile (unsigned long ip)
{
	extern char _stext;

	if (prof_buffer && current->pid) {
		ip -= (unsigned long) &_stext;
		ip >>= prof_shift;
		/*
		 * Don't ignore out-of-bounds IP values silently,
		 * put them into the last histogram slot, so if
		 * present, they will show up as a sharp peak.
		 */
		if (ip > prof_len - 1)
			ip = prof_len - 1;

		atomic_inc((atomic_t *) &prof_buffer[ip]);
	} 
}

/*
 * Return the number of micro-seconds that elapsed since the last
 * update to jiffy.  The xtime_lock must be at least read-locked when
 * calling this routine.
 */
static inline unsigned long
gettimeoffset (void)
{
	unsigned long now = ia64_get_itc();
	unsigned long elapsed_cycles, lost;

	elapsed_cycles = now - (itm.next[smp_processor_id()] - itm.delta);

	lost = lost_ticks;
	if (lost)
		elapsed_cycles += lost*itm.delta;

	return (elapsed_cycles*my_cpu_data.usec_per_cyc) >> IA64_USEC_PER_CYC_SHIFT;
}

void
do_settimeofday (struct timeval *tv)
{
	write_lock_irq(&xtime_lock);
	{
		/*
		 * This is revolting. We need to set the xtime.tv_usec
		 * correctly. However, the value in this location is
		 * is value at the last tick.  Discover what
		 * correction gettimeofday would have done, and then
		 * undo it!
		 */
		tv->tv_usec -= gettimeoffset();
		while (tv->tv_usec < 0) {
			tv->tv_usec += 1000000;
			tv->tv_sec--;
		}

		xtime = *tv;
		time_adjust = 0;		/* stop active adjtime() */
		time_status |= STA_UNSYNC;
		time_maxerror = NTP_PHASE_LIMIT;
		time_esterror = NTP_PHASE_LIMIT;
	}
	write_unlock_irq(&xtime_lock);
}

void
do_gettimeofday (struct timeval *tv)
{
	unsigned long flags, usec, sec;

	read_lock_irqsave(&xtime_lock, flags);
	{
		usec = gettimeoffset();
	
		sec = xtime.tv_sec;
		usec += xtime.tv_usec;
	}
	read_unlock_irqrestore(&xtime_lock, flags);

	while (usec >= 1000000) {
		usec -= 1000000;
		++sec;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

static void
timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	static unsigned long last_time;
	static unsigned char count;
	int cpu = smp_processor_id();

	/*
	 * Here we are in the timer irq handler. We have irqs locally
	 * disabled, but we don't know if the timer_bh is running on
	 * another CPU. We need to avoid to SMP race by acquiring the
	 * xtime_lock.
	 */
	write_lock(&xtime_lock);
	while (1) {
		/* do kernel PC profiling here.  */
		if (!user_mode(regs)) 
			do_profile(regs->cr_iip);

#ifdef CONFIG_SMP
		smp_do_timer(regs);
		if (smp_processor_id() == bootstrap_processor)
			do_timer(regs);
#else
		do_timer(regs);
#endif

		itm.next[cpu] += itm.delta;
		/*
		 * There is a race condition here: to be on the "safe"
		 * side, we process timer ticks until itm.next is
		 * ahead of the itc by at least half the timer
		 * interval.  This should give us enough time to set
		 * the new itm value without losing a timer tick.
		 */
		if (time_after(itm.next[cpu], ia64_get_itc() + itm.delta/2)) {
			ia64_set_itm(itm.next[cpu]);
			break;
		}

#if !(defined(CONFIG_IA64_SOFTSDV_HACKS) && defined(CONFIG_SMP))
		/*
		 * SoftSDV in SMP mode is _slow_, so we do "loose" ticks, 
		 * but it's really OK...
		 */
		if (count > 0 && jiffies - last_time > 5*HZ)
			count = 0;
		if (count++ == 0) {
			last_time = jiffies;
			printk("Lost clock tick on CPU %d (now=%lx, next=%lx)!!\n",
			       cpu, ia64_get_itc(), itm.next[cpu]);
# ifdef CONFIG_IA64_DEBUG_IRQ
			printk("last_cli_ip=%lx\n", last_cli_ip);
# endif
		}
#endif
	}
	write_unlock(&xtime_lock);
}

/*
 * Encapsulate access to the itm structure for SMP.
 */
void __init
ia64_cpu_local_tick(void)
{
	/* arrange for the cycle counter to generate a timer interrupt: */
	ia64_set_itv(TIMER_IRQ, 0);
	ia64_set_itc(0);
	itm.next[smp_processor_id()] = ia64_get_itc() + itm.delta;
	ia64_set_itm(itm.next[smp_processor_id()]);
}

void __init
ia64_init_itm (void)
{
	unsigned long platform_base_freq, itc_freq, drift;
	struct pal_freq_ratio itc_ratio, proc_ratio;
	long status;

	/*
	 * According to SAL v2.6, we need to use a SAL call to determine the
	 * platform base frequency and then a PAL call to determine the
	 * frequency ratio between the ITC and the base frequency.
	 */
	status = ia64_sal_freq_base(SAL_FREQ_BASE_PLATFORM, &platform_base_freq, &drift);
	if (status != 0) {
		printk("SAL_FREQ_BASE_PLATFORM failed: %s\n", ia64_sal_strerror(status));
	} else {
		status = ia64_pal_freq_ratios(&proc_ratio, 0, &itc_ratio);
		if (status != 0)
			printk("PAL_FREQ_RATIOS failed with status=%ld\n", status);
	}
	if (status != 0) {
		/* invent "random" values */
		printk("SAL/PAL failed to obtain frequency info---inventing reasonably values\n");
		platform_base_freq = 100000000;
		itc_ratio.num = 3;
		itc_ratio.den = 1;
	}
#if defined(CONFIG_IA64_LION_HACKS)
	/* Our Lion currently returns base freq 104.857MHz, which
	   ain't right (it really is 100MHz).  */
	printk("SAL/PAL returned: base-freq=%lu, itc-ratio=%lu/%lu, proc-ratio=%lu/%lu\n",
	       platform_base_freq, itc_ratio.num, itc_ratio.den,
	       proc_ratio.num, proc_ratio.den);
	platform_base_freq = 100000000;
#elif 0 && defined(CONFIG_IA64_BIGSUR_HACKS)
	/* BigSur with 991020 firmware returned itc-ratio=9/2 and base
	   freq 75MHz, which wasn't right.  The 991119 firmware seems
	   to return the right values, so this isn't necessary
	   anymore... */
	printk("SAL/PAL returned: base-freq=%lu, itc-ratio=%lu/%lu, proc-ratio=%lu/%lu\n",
	       platform_base_freq, itc_ratio.num, itc_ratio.den,
	       proc_ratio.num, proc_ratio.den);
	platform_base_freq = 100000000;
	proc_ratio.num = 5; proc_ratio.den = 1;
	itc_ratio.num  = 5; itc_ratio.den  = 1;
#elif defined(CONFIG_IA64_SOFTSDV_HACKS)
	platform_base_freq = 10000000;
	proc_ratio.num = 4; proc_ratio.den = 1;
	itc_ratio.num  = 4; itc_ratio.den  = 1;
#else
	if (platform_base_freq < 40000000) {
		printk("Platform base frequency %lu bogus---resetting to 75MHz!\n",
		       platform_base_freq);
		platform_base_freq = 75000000;
	}
#endif
	if (!proc_ratio.den)
		proc_ratio.num = 1;	/* avoid division by zero */
	if (!itc_ratio.den)
		itc_ratio.num = 1;	/* avoid division by zero */

        itc_freq = (platform_base_freq*itc_ratio.num)/itc_ratio.den;
        itm.delta = itc_freq / HZ;
        printk("timer: base freq=%lu.%03luMHz, ITC ratio=%lu/%lu, ITC freq=%lu.%03luMHz\n",
               platform_base_freq / 1000000, (platform_base_freq / 1000) % 1000,
               itc_ratio.num, itc_ratio.den, itc_freq / 1000000, (itc_freq / 1000) % 1000);

	my_cpu_data.proc_freq = (platform_base_freq*proc_ratio.num)/proc_ratio.den;
	my_cpu_data.itc_freq = itc_freq;
	my_cpu_data.cyc_per_usec = itc_freq / 1000000;
	my_cpu_data.usec_per_cyc = (1000000UL << IA64_USEC_PER_CYC_SHIFT) / itc_freq;

	/* Setup the CPU local timer tick */
	ia64_cpu_local_tick();
}

void __init
time_init (void)
{
	/*
	 * Request the IRQ _before_ doing anything to cause that
	 * interrupt to be posted.
	 */
	if (request_irq(TIMER_IRQ, timer_interrupt, 0, "timer", NULL)) 
		panic("Could not allocate timer IRQ!");

	efi_gettimeofday(&xtime);
	ia64_init_itm();
}

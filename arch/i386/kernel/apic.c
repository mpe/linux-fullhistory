/*
 *	Local APIC handling, local APIC timers
 *
 *	(c) 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *	Fixes
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX APICs;
 *					thanks to Eric Gilmore for
 *					testing these extensively
 */

#include <linux/config.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/kernel_stat.h>

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/pgalloc.h>

int prof_multiplier[NR_CPUS] = { 1, };
int prof_old_multiplier[NR_CPUS] = { 1, };
int prof_counter[NR_CPUS] = { 1, };

int get_maxlvt(void)
{
	unsigned int v, ver, maxlvt;

	v = apic_read(APIC_LVR);
	ver = GET_APIC_VERSION(v);
	/* 82489DXs do not report # of LVT entries. */
	maxlvt = APIC_INTEGRATED(ver) ? GET_APIC_MAXLVT(v) : 2;
	return maxlvt;
}

static void clear_local_APIC(void)
{
	int maxlvt;
	unsigned long v;

	maxlvt = get_maxlvt();

	/*
	 * Careful: we have to set masks only first to deassert
	 * any level-triggered sources.
	 */
	v = apic_read(APIC_LVTT);
	apic_write_around(APIC_LVTT, v | APIC_LVT_MASKED);
	v = apic_read(APIC_LVT0);
	apic_write_around(APIC_LVT0, v | APIC_LVT_MASKED);
	v = apic_read(APIC_LVT1);
	apic_write_around(APIC_LVT1, v | APIC_LVT_MASKED);
	if (maxlvt >= 3) {
		v = apic_read(APIC_LVTERR);
		apic_write_around(APIC_LVTERR, v | APIC_LVT_MASKED);
	}
	if (maxlvt >= 4) {
		v = apic_read(APIC_LVTPC);
		apic_write_around(APIC_LVTPC, v | APIC_LVT_MASKED);
	}

	/*
	 * Clean APIC state for other OSs:
	 */
	apic_write_around(APIC_LVTT, APIC_LVT_MASKED);
	apic_write_around(APIC_LVT0, APIC_LVT_MASKED);
	apic_write_around(APIC_LVT1, APIC_LVT_MASKED);
	if (maxlvt >= 3)
		apic_write_around(APIC_LVTERR, APIC_LVT_MASKED);
	if (maxlvt >= 4)
		apic_write_around(APIC_LVTPC, APIC_LVT_MASKED);
}

void __init connect_bsp_APIC(void)
{
	if (pic_mode) {
		/*
		 * Do not trust the local APIC being empty at bootup.
		 */
		clear_local_APIC();
		/*
		 * PIC mode, enable symmetric IO mode in the IMCR,
		 * i.e. connect BSP's local APIC to INT and NMI lines.
		 */
		printk("leaving PIC mode, enabling symmetric IO mode.\n");
		outb(0x70, 0x22);
		outb(0x01, 0x23);
	}
}

void disconnect_bsp_APIC(void)
{
	if (pic_mode) {
		/*
		 * Put the board back into PIC mode (has an effect
		 * only on certain older boards).  Note that APIC
		 * interrupts, including IPIs, won't work beyond
		 * this point!  The only exception are INIT IPIs.
		 */
		printk("disabling symmetric IO mode, entering PIC mode.\n");
		outb(0x70, 0x22);
		outb(0x00, 0x23);
	}
}

void disable_local_APIC(void)
{
	unsigned long value;

	clear_local_APIC();

	/*
	 * Disable APIC (implies clearing of registers
	 * for 82489DX!).
	 */
	value = apic_read(APIC_SPIV);
	value &= ~(1<<8);
	apic_write_around(APIC_SPIV, value);
}

void __init sync_Arb_IDs(void)
{
	Dprintk("Synchronizing Arb IDs.\n");
	apic_write_around(APIC_ICR, APIC_DEST_ALLINC | APIC_INT_LEVELTRIG
				| APIC_DM_INIT);
}

extern void __error_in_apic_c (void);

void __init setup_local_APIC (void)
{
	unsigned long value, ver, maxlvt;

	value = apic_read(APIC_LVR);
	ver = GET_APIC_VERSION(value);

	if ((SPURIOUS_APIC_VECTOR & 0x0f) != 0x0f)
		__error_in_apic_c();

	/*
	 * Double-check wether this APIC is really registered.
	 */
	if (!test_bit(GET_APIC_ID(apic_read(APIC_ID)), &phys_cpu_present_map))
		BUG();

	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	/*
	 * Enable APIC
	 */
	value |= (1<<8);

	/*
	 * Some unknown Intel IO/APIC (or APIC) errata is biting us with
	 * certain networking cards. If high frequency interrupts are
	 * happening on a particular IOAPIC pin, plus the IOAPIC routing
	 * entry is masked/unmasked at a high rate as well then sooner or
	 * later IOAPIC line gets 'stuck', no more interrupts are received
	 * from the device. If focus CPU is disabled then the hang goes
	 * away, oh well :-(
	 *
	 * [ This bug can be reproduced easily with a level-triggered
	 *   PCI Ne2000 networking cards and PII/PIII processors, dual
	 *   BX chipset. ]
	 */
#if 0
	/* Enable focus processor (bit==0) */
	value &= ~(1<<9);
#else
	/* Disable focus processor (bit==1) */
	value |= (1<<9);
#endif
	/*
	 * Set spurious IRQ vector
	 */
	value |= SPURIOUS_APIC_VECTOR;
	apic_write_around(APIC_SPIV, value);

	/*
	 * Set up LVT0, LVT1:
	 *
	 * set up through-local-APIC on the BP's LINT0. This is not
	 * strictly necessery in pure symmetric-IO mode, but sometimes
	 * we delegate interrupts to the 8259A.
	 */
	/*
	 * TODO: set up through-local-APIC from through-I/O-APIC? --macro
	 */
	value = apic_read(APIC_LVT0) & APIC_LVT_MASKED;
	if (!smp_processor_id() && (pic_mode || !value)) {
		value = APIC_DM_EXTINT;
		printk("enabled ExtINT on CPU#%d\n", smp_processor_id());
	} else {
		value = APIC_DM_EXTINT | APIC_LVT_MASKED;
		printk("masked ExtINT on CPU#%d\n", smp_processor_id());
	}
	apic_write_around(APIC_LVT0, value);

	/*
	 * only the BP should see the LINT1 NMI signal, obviously.
	 */
	if (!smp_processor_id())
		value = APIC_DM_NMI;
	else
		value = APIC_DM_NMI | APIC_LVT_MASKED;
	if (!APIC_INTEGRATED(ver))		/* 82489DX */
		value |= APIC_LVT_LEVEL_TRIGGER;
	apic_write_around(APIC_LVT1, value);

	if (APIC_INTEGRATED(ver)) {		/* !82489DX */
		maxlvt = get_maxlvt();
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP. */
			apic_write(APIC_ESR, 0);
		value = apic_read(APIC_ESR);
		printk("ESR value before enabling vector: %08lx\n", value);

		value = ERROR_APIC_VECTOR;      // enables sending errors
		apic_write_around(APIC_LVTERR, value);
		/*
		 * spec says clear errors after enabling vector.
		 */
		if (maxlvt > 3)
			apic_write(APIC_ESR, 0);
		value = apic_read(APIC_ESR);
		printk("ESR value after enabling vector: %08lx\n", value);
	} else
		printk("No ESR for 82489DX.\n");

	/*
	 * Set Task Priority to 'accept all'. We never change this
	 * later on.
	 */
	value = apic_read(APIC_TASKPRI);
	value &= ~APIC_TPRI_MASK;
	apic_write_around(APIC_TASKPRI, value);

	/*
	 * Set up the logical destination ID and put the
	 * APIC into flat delivery mode.
	 */
	value = apic_read(APIC_LDR);
	value &= ~APIC_LDR_MASK;
	value |= (1<<(smp_processor_id()+24));
	apic_write_around(APIC_LDR, value);

	/*
	 * Must be "all ones" explicitly for 82489DX.
	 */
	apic_write_around(APIC_DFR, 0xffffffff);
}

void __init init_apic_mappings(void)
{
	unsigned long apic_phys;

	if (smp_found_config) {
		apic_phys = mp_lapic_addr;
	} else {
		/*
		 * set up a fake all zeroes page to simulate the
		 * local APIC and another one for the IO-APIC. We
		 * could use the real zero-page, but it's safer
		 * this way if some buggy code writes to this page ...
		 */
		apic_phys = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
		apic_phys = __pa(apic_phys);
	}
	set_fixmap_nocache(FIX_APIC_BASE, apic_phys);
	Dprintk("mapped APIC to %08lx (%08lx)\n", APIC_BASE, apic_phys);

	/*
	 * Fetch the APIC ID of the BSP in case we have a
	 * default configuration (or the MP table is broken).
	 */
	if (boot_cpu_id == -1U)
		boot_cpu_id = GET_APIC_ID(apic_read(APIC_ID));

#ifdef CONFIG_X86_IO_APIC
	{
		unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
		int i;

		for (i = 0; i < nr_ioapics; i++) {
			if (smp_found_config) {
				ioapic_phys = mp_ioapics[i].mpc_apicaddr;
			} else {
				ioapic_phys = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
				ioapic_phys = __pa(ioapic_phys);
			}
			set_fixmap_nocache(idx, ioapic_phys);
			Dprintk("mapped IOAPIC to %08lx (%08lx)\n",
					__fix_to_virt(idx), ioapic_phys);
			idx++;
		}
	}
#endif
}

/*
 * This part sets up the APIC 32 bit clock in LVTT1, with HZ interrupts
 * per second. We assume that the caller has already set up the local
 * APIC.
 *
 * The APIC timer is not exactly sync with the external timer chip, it
 * closely follows bus clocks.
 */

/*
 * The timer chip is already set up at HZ interrupts per second here,
 * but we do not accept timer interrupts yet. We only allow the BP
 * to calibrate.
 */
static unsigned int __init get_8254_timer_count(void)
{
	extern rwlock_t xtime_lock;
	unsigned long flags;

	unsigned int count;

	write_lock_irqsave(&xtime_lock, flags);

	outb_p(0x00, 0x43);
	count = inb_p(0x40);
	count |= inb_p(0x40) << 8;

	write_unlock_irqrestore(&xtime_lock, flags);

	return count;
}

void __init wait_8254_wraparound(void)
{
	unsigned int curr_count, prev_count=~0;
	int delta;

	curr_count = get_8254_timer_count();

	do {
		prev_count = curr_count;
		curr_count = get_8254_timer_count();
		delta = curr_count-prev_count;

	/*
	 * This limit for delta seems arbitrary, but it isn't, it's
	 * slightly above the level of error a buggy Mercury/Neptune
	 * chipset timer can cause.
	 */

	} while (delta < 300);
}

/*
 * This function sets up the local APIC timer, with a timeout of
 * 'clocks' APIC bus clock. During calibration we actually call
 * this function twice on the boot CPU, once with a bogus timeout
 * value, second time for real. The other (noncalibrating) CPUs
 * call this function only once, with the real, calibrated value.
 *
 * We do reads before writes even if unnecessary, to get around the
 * P5 APIC double write bug.
 */

#define APIC_DIVISOR 16

void __setup_APIC_LVTT(unsigned int clocks)
{
	unsigned int lvtt1_value, tmp_value;

	lvtt1_value = SET_APIC_TIMER_BASE(APIC_TIMER_BASE_DIV) |
			APIC_LVT_TIMER_PERIODIC | LOCAL_TIMER_VECTOR;
	apic_write_around(APIC_LVTT, lvtt1_value);

	/*
	 * Divide PICLK by 16
	 */
	tmp_value = apic_read(APIC_TDCR);
	apic_write_around(APIC_TDCR, (tmp_value
				& ~(APIC_TDR_DIV_1 | APIC_TDR_DIV_TMBASE))
				| APIC_TDR_DIV_16);

	apic_write_around(APIC_TMICT, clocks/APIC_DIVISOR);
}

void setup_APIC_timer(void * data)
{
	unsigned int clocks = (unsigned int) data, slice, t0, t1;
	unsigned long flags;
	int delta;

	__save_flags(flags);
	__sti();
	/*
	 * ok, Intel has some smart code in their APIC that knows
	 * if a CPU was in 'hlt' lowpower mode, and this increases
	 * its APIC arbitration priority. To avoid the external timer
	 * IRQ APIC event being in synchron with the APIC clock we
	 * introduce an interrupt skew to spread out timer events.
	 *
	 * The number of slices within a 'big' timeslice is smp_num_cpus+1
	 */

	slice = clocks / (smp_num_cpus+1);
	printk("cpu: %d, clocks: %d, slice: %d\n",
		smp_processor_id(), clocks, slice);

	/*
	 * Wait for IRQ0's slice:
	 */
	wait_8254_wraparound();

	__setup_APIC_LVTT(clocks);

	t0 = apic_read(APIC_TMCCT)*APIC_DIVISOR;
	do {
		/*
		 * It looks like the 82489DX cannot handle
		 * consecutive reads of the TMCCT register well;
		 * this dummy read prevents it from a lockup.
		 */
		apic_read(APIC_SPIV);
		t1 = apic_read(APIC_TMCCT)*APIC_DIVISOR;
		delta = (int)(t0 - t1 - slice*(smp_processor_id()+1));
	} while (delta < 0);

	__setup_APIC_LVTT(clocks);

	printk("CPU%d<C0:%d,C:%d,D:%d,S:%d,C:%d>\n",
			smp_processor_id(), t0, t1, delta, slice, clocks);

	__restore_flags(flags);
}

/*
 * In this function we calibrate APIC bus clocks to the external
 * timer. Unfortunately we cannot use jiffies and the timer irq
 * to calibrate, since some later bootup code depends on getting
 * the first irq? Ugh.
 *
 * We want to do the calibration only once since we
 * want to have local timer irqs syncron. CPUs connected
 * by the same APIC bus have the very same bus frequency.
 * And we want to have irqs off anyways, no accidental
 * APIC irq that way.
 */

int __init calibrate_APIC_clock(void)
{
	unsigned long long t1 = 0, t2 = 0;
	long tt1, tt2;
	long result;
	int i;
	const int LOOPS = HZ/10;

	printk("calibrating APIC timer ... ");

	/*
	 * Put whatever arbitrary (but long enough) timeout
	 * value into the APIC clock, we just want to get the
	 * counter running for calibration.
	 */
	__setup_APIC_LVTT(1000000000);

	/*
	 * The timer chip counts down to zero. Let's wait
	 * for a wraparound to start exact measurement:
	 * (the current tick might have been already half done)
	 */

	wait_8254_wraparound();

	/*
	 * We wrapped around just now. Let's start:
	 */
	if (cpu_has_tsc)
		rdtscll(t1);
	tt1 = apic_read(APIC_TMCCT);

	/*
	 * Let's wait LOOPS wraprounds:
	 */
	for (i = 0; i < LOOPS; i++)
		wait_8254_wraparound();

	tt2 = apic_read(APIC_TMCCT);
	if (cpu_has_tsc)
		rdtscll(t2);

	/*
	 * The APIC bus clock counter is 32 bits only, it
	 * might have overflown, but note that we use signed
	 * longs, thus no extra care needed.
	 *
	 * underflown to be exact, as the timer counts down ;)
	 */

	result = (tt1-tt2)*APIC_DIVISOR/LOOPS;

	if (cpu_has_tsc)
		printk("\n..... CPU clock speed is %ld.%04ld MHz.\n",
			((long)(t2-t1)/LOOPS)/(1000000/HZ),
			((long)(t2-t1)/LOOPS)%(1000000/HZ));

	printk("..... host bus clock speed is %ld.%04ld MHz.\n",
		result/(1000000/HZ),
		result%(1000000/HZ));

	return result;
}

static unsigned int calibration_result;

void __init setup_APIC_clocks (void)
{
	__cli();

	calibration_result = calibrate_APIC_clock();
	/*
	 * Now set up the timer for real.
	 */
	setup_APIC_timer((void *)calibration_result);

	__sti();

	/* and update all other cpus */
	smp_call_function(setup_APIC_timer, (void *)calibration_result, 1, 1);
}

/*
 * the frequency of the profiling timer can be changed
 * by writing a multiplier value into /proc/profile.
 */
int setup_profiling_timer(unsigned int multiplier)
{
	int i;

	/*
	 * Sanity check. [at least 500 APIC cycles should be
	 * between APIC interrupts as a rule of thumb, to avoid
	 * irqs flooding us]
	 */
	if ( (!multiplier) || (calibration_result/multiplier < 500))
		return -EINVAL;

	/* 
	 * Set the new multiplier for each CPU. CPUs don't start using the
	 * new values until the next timer interrupt in which they do process
	 * accounting. At that time they also adjust their APIC timers
	 * accordingly.
	 */
	for (i = 0; i < NR_CPUS; ++i)
		prof_multiplier[i] = multiplier;

	return 0;
}

#undef APIC_DIVISOR

#ifdef CONFIG_SMP
static inline void handle_smp_time (int user, int cpu)
{
	int system = !user;
	struct task_struct * p = current;
	/*
	 * After doing the above, we need to make like
	 * a normal interrupt - otherwise timer interrupts
	 * ignore the global interrupt lock, which is the
	 * WrongThing (tm) to do.
	 */

	irq_enter(cpu, 0);
	update_one_process(p, 1, user, system, cpu);
	if (p->pid) {
		p->counter -= 1;
		if (p->counter <= 0) {
			p->counter = 0;
			p->need_resched = 1;
		}
		if (p->priority < DEF_PRIORITY) {
			kstat.cpu_nice += user;
			kstat.per_cpu_nice[cpu] += user;
		} else {
			kstat.cpu_user += user;
			kstat.per_cpu_user[cpu] += user;
		}
		kstat.cpu_system += system;
		kstat.per_cpu_system[cpu] += system;

	}
	irq_exit(cpu, 0);
}
#endif

/*
 * Local timer interrupt handler. It does both profiling and
 * process statistics/rescheduling.
 *
 * We do profiling in every local tick, statistics/rescheduling
 * happen only every 'profiling multiplier' ticks. The default
 * multiplier is 1 and it can be changed by writing the new multiplier
 * value into /proc/profile.
 */

inline void smp_local_timer_interrupt(struct pt_regs * regs)
{
	int user = user_mode(regs);
	int cpu = smp_processor_id();

	/*
	 * The profiling function is SMP safe. (nothing can mess
	 * around with "current", and the profiling counters are
	 * updated with atomic operations). This is especially
	 * useful with a profiling multiplier != 1
	 */
	if (!user)
		x86_do_profile(regs->eip);

	if (--prof_counter[cpu] <= 0) {
		/*
		 * The multiplier may have changed since the last time we got
		 * to this point as a result of the user writing to
		 * /proc/profile. In this case we need to adjust the APIC
		 * timer accordingly.
		 *
		 * Interrupts are already masked off at this point.
		 */
		prof_counter[cpu] = prof_multiplier[cpu];
		if (prof_counter[cpu] != prof_old_multiplier[cpu]) {
			__setup_APIC_LVTT(calibration_result/prof_counter[cpu]);
			prof_old_multiplier[cpu] = prof_counter[cpu];
		}

#ifdef CONFIG_SMP
		handle_smp_time(user, cpu);
#endif
	}

	/*
	 * We take the 'long' return path, and there every subsystem
	 * grabs the apropriate locks (kernel lock/ irq lock).
	 *
	 * we might want to decouple profiling from the 'long path',
	 * and do the profiling totally in assembly.
	 *
	 * Currently this isn't too much of an issue (performance wise),
	 * we can take more than 100K local irqs per second on a 100 MHz P5.
	 */
}

/*
 * Local APIC timer interrupt. This is the most natural way for doing
 * local interrupts, but local timer interrupts can be emulated by
 * broadcast interrupts too. [in case the hw doesnt support APIC timers]
 *
 * [ if a single-CPU system runs an SMP kernel then we call the local
 *   interrupt as well. Thus we cannot inline the local irq ... ]
 */
unsigned int apic_timer_irqs [NR_CPUS] = { 0, };

void smp_apic_timer_interrupt(struct pt_regs * regs)
{
	/*
	 * the NMI deadlock-detector uses this.
	 */
	apic_timer_irqs[smp_processor_id()]++;

	/*
	 * NOTE! We'd better ACK the irq immediately,
	 * because timer handling can be slow.
	 */
	ack_APIC_irq();
	smp_local_timer_interrupt(regs);
}

/*
 * This interrupt should _never_ happen with our APIC/SMP architecture
 */
asmlinkage void smp_spurious_interrupt(void)
{
	unsigned long v;

	/*
	 * Check if this really is a spurious interrupt and ACK it
	 * if it is a vectored one.  Just in case...
	 * Spurious interrupts should not be ACKed.
	 */
	v = apic_read(APIC_ISR + ((SPURIOUS_APIC_VECTOR & ~0x1f) >> 1));
	if (v & (1 << (SPURIOUS_APIC_VECTOR & 0x1f)))
		ack_APIC_irq();

	/* see sw-dev-man vol 3, chapter 7.4.13.5 */
	printk("spurious APIC interrupt on CPU#%d, should never happen.\n",
			smp_processor_id());
}

/*
 * This interrupt should never happen with our APIC/SMP architecture
 */

static spinlock_t err_lock = SPIN_LOCK_UNLOCKED;

asmlinkage void smp_error_interrupt(void)
{
	unsigned long v;

	spin_lock(&err_lock);

	v = apic_read(APIC_ESR);
	printk("APIC error interrupt on CPU#%d, should never happen.\n",
			smp_processor_id());
	printk("... APIC ESR0: %08lx\n", v);

	apic_write(APIC_ESR, 0);
	v |= apic_read(APIC_ESR);
	printk("... APIC ESR1: %08lx\n", v);
	/*
	 * Be a bit more verbose. (multiple bits can be set)
	 */
	if (v & 0x01)
		printk("... bit 0: APIC Send CS Error (hw problem).\n");
	if (v & 0x02)
		printk("... bit 1: APIC Receive CS Error (hw problem).\n");
	if (v & 0x04)
		printk("... bit 2: APIC Send Accept Error.\n");
	if (v & 0x08)
		printk("... bit 3: APIC Receive Accept Error.\n");
	if (v & 0x10)
		printk("... bit 4: Reserved!.\n");
	if (v & 0x20)
		printk("... bit 5: Send Illegal Vector (kernel bug).\n");
	if (v & 0x40)
		printk("... bit 6: Received Illegal Vector.\n");
	if (v & 0x80)
		printk("... bit 7: Illegal Register Address.\n");

	ack_APIC_irq();

	irq_err_count++;

	spin_unlock(&err_lock);
}


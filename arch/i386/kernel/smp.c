/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998-99 Ingo Molnar <mingo@redhat.com>
 *
 *	This code is released under the GNU public license version 2 or
 *	later.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/smp_lock.h>
#include <linux/irq.h>

#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <asm/mtrr.h>

/*
 *	Some notes on processor bugs:
 *
 *	Pentium, Pentium Pro, II, III (and all CPUs) have bugs.
 *	The Linux implications for SMP are handled as follows:
 *
 *	Pentium III / [Xeon]
 *		None of the E1AP-E3AP erratas are visible to the user.
 *
 *	E1AP.	see PII A1AP
 *	E2AP.	see PII A2AP
 *	E3AP.	see PII A3AP
 *
 *	Pentium II / [Xeon]
 *		None of the A1AP-A3AP erratas are visible to the user.
 *
 *	A1AP.	see PPro 1AP
 *	A2AP.	see PPro 2AP
 *	A3AP.	see PPro 7AP
 *
 *	Pentium Pro
 *		None of 1AP-9AP erratas are visible to the normal user,
 *	except occasional delivery of 'spurious interrupt' as trap #15.
 *	This is very rare and a non-problem.
 *
 *	1AP.	Linux maps APIC as non-cacheable
 *	2AP.	worked around in hardware
 *	3AP.	fixed in C0 and above steppings microcode update.
 *		Linux does not use excessive STARTUP_IPIs.
 *	4AP.	worked around in hardware
 *	5AP.	symmetric IO mode (normal Linux operation) not affected.
 *		'noapic' mode has vector 0xf filled out properly.
 *	6AP.	'noapic' mode might be affected - fixed in later steppings
 *	7AP.	We do not assume writes to the LVT deassering IRQs
 *	8AP.	We do not enable low power mode (deep sleep) during MP bootup
 *	9AP.	We do not use mixed mode
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPUs may hang. There are hardware work arounds
 *	for this. We warn about it in case your board doesnt have the work
 *	arounds. Basically thats so I can tell anyone with a B stepping
 *	CPU and SMP problems "tough".
 *
 *	Specific items [From Pentium Processor Specification Update]
 *
 *	1AP.	Linux doesn't use remote read
 *	2AP.	Linux doesn't trust APIC errors
 *	3AP.	We work around this
 *	4AP.	Linux never generated 3 interrupts of the same priority
 *		to cause a lost local interrupt.
 *	5AP.	Remote read is never used
 *	6AP.	not affected - worked around in hardware
 *	7AP.	not affected - worked around in hardware
 *	8AP.	worked around in hardware - we get explicit CS errors if not
 *	9AP.	only 'noapic' mode affected. Might generate spurious
 *		interrupts, we log only the first one and count the
 *		rest silently.
 *	10AP.	not affected - worked around in hardware
 *	11AP.	Linux reads the APIC between writes to avoid this, as per
 *		the documentation. Make sure you preserve this as it affects
 *		the C stepping chips too.
 *	12AP.	not affected - worked around in hardware
 *	13AP.	not affected - worked around in hardware
 *	14AP.	we always deassert INIT during bootup
 *	15AP.	not affected - worked around in hardware
 *	16AP.	not affected - worked around in hardware
 *	17AP.	not affected - worked around in hardware
 *	18AP.	not affected - worked around in hardware
 *	19AP.	not affected - worked around in BIOS
 *
 *	If this sounds worrying believe me these bugs are either ___RARE___,
 *	or are signal timing bugs worked around in hardware and there's
 *	about nothing of note with C stepping upwards.
 */

/* The 'big kernel lock' */
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;

volatile unsigned long smp_invalidate_needed;

/*
 * the following functions deal with sending IPIs between CPUs.
 *
 * We use 'broadcast', CPU->CPU IPIs and self-IPIs too.
 */

static unsigned int cached_APIC_ICR;
static unsigned int cached_APIC_ICR2;

/*
 * Caches reserved bits, APIC reads are (mildly) expensive
 * and force otherwise unnecessary CPU synchronization.
 *
 * (We could cache other APIC registers too, but these are the
 * main ones used in RL.)
 */
#define slow_ICR (apic_read(APIC_ICR) & ~0xFDFFF)
#define slow_ICR2 (apic_read(APIC_ICR2) & 0x00FFFFFF)

void cache_APIC_registers (void)
{
	cached_APIC_ICR = slow_ICR;
	cached_APIC_ICR2 = slow_ICR2;
	mb();
}

static inline unsigned int __get_ICR (void)
{
#if FORCE_READ_AROUND_WRITE
	/*
	 * Wait for the APIC to become ready - this should never occur. It's
	 * a debugging check really.
	 */
	int count = 0;
	unsigned int cfg;

	while (count < 1000)
	{
		cfg = slow_ICR;
		if (!(cfg&(1<<12)))
			return cfg;
		printk("CPU #%d: ICR still busy [%08x]\n",
					smp_processor_id(), cfg);
		irq_err_count++;
		count++;
		udelay(10);
	}
	printk("CPU #%d: previous IPI still not cleared after 10mS\n",
			smp_processor_id());
	return cfg;
#else
	return cached_APIC_ICR;
#endif
}

static inline unsigned int __get_ICR2 (void)
{
#if FORCE_READ_AROUND_WRITE
	return slow_ICR2;
#else
	return cached_APIC_ICR2;
#endif
}

#define LOGICAL_DELIVERY 1

static inline int __prepare_ICR (unsigned int shortcut, int vector)
{
	unsigned int cfg;

	cfg = __get_ICR();
	cfg |= APIC_DEST_DM_FIXED|shortcut|vector
#if LOGICAL_DELIVERY
		|APIC_DEST_LOGICAL
#endif
		;

	return cfg;
}

static inline int __prepare_ICR2 (unsigned int dest)
{
	unsigned int cfg;

	cfg = __get_ICR2();
#if LOGICAL_DELIVERY
	cfg |= SET_APIC_DEST_FIELD((1<<dest));
#else
	cfg |= SET_APIC_DEST_FIELD(dest);
#endif

	return cfg;
}

static inline void __send_IPI_shortcut(unsigned int shortcut, int vector)
{
	unsigned int cfg;
/*
 * Subtle. In the case of the 'never do double writes' workaround we
 * have to lock out interrupts to be safe. Otherwise it's just one
 * single atomic write to the APIC, no need for cli/sti.
 */
#if FORCE_READ_AROUND_WRITE
	unsigned long flags;

	__save_flags(flags);
	__cli();
#endif

	/*
	 * No need to touch the target chip field
	 */
	cfg = __prepare_ICR(shortcut, vector);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
#if FORCE_READ_AROUND_WRITE
	__restore_flags(flags);
#endif
}

static inline void send_IPI_allbutself(int vector)
{
	/*
	 * if there are no other CPUs in the system then
	 * we get an APIC send error if we try to broadcast.
	 * thus we have to avoid sending IPIs in this case.
	 */
	if (smp_num_cpus > 1)
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
}

static inline void send_IPI_all(int vector)
{
	__send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

void send_IPI_self(int vector)
{
	__send_IPI_shortcut(APIC_DEST_SELF, vector);
}

static inline void send_IPI_single(int dest, int vector)
{
	unsigned long cfg;
#if FORCE_READ_AROUND_WRITE
	unsigned long flags;

	__save_flags(flags);
	__cli();
#endif

	/*
	 * prepare target chip field
	 */

	cfg = __prepare_ICR2(dest);
	apic_write(APIC_ICR2, cfg);

	/*
	 * program the ICR 
	 */
	cfg = __prepare_ICR(0, vector);
	
	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
#if FORCE_READ_AROUND_WRITE
	__restore_flags(flags);
#endif
}

/*
 * This is fraught with deadlocks. Probably the situation is not that
 * bad as in the early days of SMP, so we might ease some of the
 * paranoia here.
 */
static void flush_tlb_others(unsigned int cpumask)
{
	int cpu = smp_processor_id();
	int stuck;
	unsigned long flags;

	/*
	 * it's important that we do not generate any APIC traffic
	 * until the AP CPUs have booted up!
	 */
	cpumask &= cpu_online_map;
	if (cpumask) {
		atomic_set_mask(cpumask, &smp_invalidate_needed);

		/*
		 * Processors spinning on some lock with IRQs disabled
		 * will see this IRQ late. The smp_invalidate_needed
		 * map will ensure they don't do a spurious flush tlb
		 * or miss one.
		 */
	
		__save_flags(flags);
		__cli();

		send_IPI_allbutself(INVALIDATE_TLB_VECTOR);

		/*
		 * Spin waiting for completion
		 */

		stuck = 50000000;
		while (smp_invalidate_needed) {
			/*
			 * Take care of "crossing" invalidates
			 */
			if (test_bit(cpu, &smp_invalidate_needed)) {
				struct mm_struct *mm = current->mm;
				clear_bit(cpu, &smp_invalidate_needed);
				if (mm)
					atomic_set_mask(1 << cpu, &mm->cpu_vm_mask);
				local_flush_tlb();
			}
			--stuck;
			if (!stuck) {
				printk("stuck on TLB IPI wait (CPU#%d)\n",cpu);
				break;
			}
		}
		__restore_flags(flags);
	}
}

/*
 *	Smarter SMP flushing macros. 
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 */	
void flush_tlb_current_task(void)
{
	unsigned long vm_mask = 1 << current->processor;
	struct mm_struct *mm = current->mm;
	unsigned long cpu_mask = mm->cpu_vm_mask & ~vm_mask;

	mm->cpu_vm_mask = vm_mask;
	flush_tlb_others(cpu_mask);
	local_flush_tlb();
}

void flush_tlb_mm(struct mm_struct * mm)
{
	unsigned long vm_mask = 1 << current->processor;
	unsigned long cpu_mask = mm->cpu_vm_mask & ~vm_mask;

	mm->cpu_vm_mask = 0;
	if (current->active_mm == mm) {
		mm->cpu_vm_mask = vm_mask;
		local_flush_tlb();
	}
	flush_tlb_others(cpu_mask);
}

void flush_tlb_page(struct vm_area_struct * vma, unsigned long va)
{
	unsigned long vm_mask = 1 << current->processor;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long cpu_mask = mm->cpu_vm_mask & ~vm_mask;

	mm->cpu_vm_mask = 0;
	if (current->active_mm == mm) {
		__flush_tlb_one(va);
		mm->cpu_vm_mask = vm_mask;
	}
	flush_tlb_others(cpu_mask);
}

void flush_tlb_all(void)
{
	flush_tlb_others(~(1 << current->processor));
	local_flush_tlb();
}


/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */

void smp_send_reschedule(int cpu)
{
	send_IPI_single(cpu, RESCHEDULE_VECTOR);
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 */
static volatile struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
} *call_data = NULL;

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */

int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
			int wait)
/*
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <nonatomic> If true, we might schedule away to lock the mutex
 * <wait> If true, wait (atomically) until function has completed on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 */
{
	struct call_data_struct data;
	int ret, cpus = smp_num_cpus-1;
	static DECLARE_MUTEX(lock);
	unsigned long timeout;

	if (nonatomic)
		down(&lock);
	else
		if (down_trylock(&lock))
			return -EBUSY;

	if (call_data) // temporary debugging check
		BUG();

	call_data = &data;
	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);
	mb();

	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_allbutself(CALL_FUNCTION_VECTOR);

	/* Wait for response */
	timeout = jiffies + HZ;
	while ((atomic_read(&data.started) != cpus)
			&& time_before(jiffies, timeout))
		barrier();
	ret = -ETIMEDOUT;
	if (atomic_read(&data.started) != cpus)
		goto out;
	ret = 0;
	if (wait)
		while (atomic_read(&data.finished) != cpus)
			barrier();
out:
	call_data = NULL;
	up(&lock);
	return 0;
}

static void stop_this_cpu (void * dummy)
{
	/*
	 * Remove this CPU:
	 */
	clear_bit(smp_processor_id(), &cpu_online_map);

	if (cpu_data[smp_processor_id()].hlt_works_ok)
		for(;;) __asm__("hlt");
	for (;;);
}

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */

void smp_send_stop(void)
{
        smp_call_function(stop_this_cpu, NULL, 1, 0);
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
asmlinkage void smp_reschedule_interrupt(void)
{
	ack_APIC_irq();
}

/*
 * Invalidate call-back.
 *
 * Mark the CPU as a VM user if there is a active
 * thread holding on to an mm at this time. This
 * allows us to optimize CPU cross-calls even in the
 * presense of lazy TLB handling.
 */
asmlinkage void smp_invalidate_interrupt(void)
{
	struct task_struct *tsk = current;
	unsigned int cpu = tsk->processor;

	if (test_and_clear_bit(cpu, &smp_invalidate_needed)) {
		struct mm_struct *mm = tsk->mm;
		if (mm)
			atomic_set_mask(1 << cpu, &mm->cpu_vm_mask);
		local_flush_tlb();
	}
	ack_APIC_irq();

}

asmlinkage void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	ack_APIC_irq();
	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	atomic_inc(&call_data->started);
	/*
	 * At this point the structure may be out of scope unless wait==1
	 */
	(*func)(info);
	if (wait)
		atomic_inc(&call_data->finished);
}

/*
 * This interrupt should _never_ happen with our APIC/SMP architecture
 */
asmlinkage void smp_spurious_interrupt(void)
{
	ack_APIC_irq();
	/* see sw-dev-man vol 3, chapter 7.4.13.5 */
	printk("spurious APIC interrupt on CPU#%d, should never happen.\n",
			smp_processor_id());
}

/*
 * This interrupt should never happen with our APIC/SMP architecture
 */

static spinlock_t err_lock;

asmlinkage void smp_error_interrupt(void)
{
	unsigned long v;

	spin_lock(&err_lock);

	v = apic_read(APIC_ESR);
	printk("APIC error interrupt on CPU#%d, should never happen.\n",
			smp_processor_id());
	printk("... APIC ESR0: %08lx\n", v);

	apic_write(APIC_ESR, 0);
	v = apic_read(APIC_ESR);
	printk("... APIC ESR1: %08lx\n", v);

	ack_APIC_irq();

	irq_err_count++;

	spin_unlock(&err_lock);
}

/*
 * This part sets up the APIC 32 bit clock in LVTT1, with HZ interrupts
 * per second. We assume that the caller has already set up the local
 * APIC.
 *
 * The APIC timer is not exactly sync with the external timer chip, it
 * closely follows bus clocks.
 */

int prof_multiplier[NR_CPUS] = { 1, };
int prof_old_multiplier[NR_CPUS] = { 1, };
int prof_counter[NR_CPUS] = { 1, };

/*
 * The timer chip is already set up at HZ interrupts per second here,
 * but we do not accept timer interrupts yet. We only allow the BP
 * to calibrate.
 */
static unsigned int __init get_8254_timer_count(void)
{
	unsigned int count;

	outb_p(0x00, 0x43);
	count = inb_p(0x40);
	count |= inb_p(0x40) << 8;

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

	} while (delta<300);
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

	tmp_value = apic_read(APIC_LVTT);
	lvtt1_value = SET_APIC_TIMER_BASE(APIC_TIMER_BASE_DIV) |
			APIC_LVT_TIMER_PERIODIC | LOCAL_TIMER_VECTOR;
	apic_write(APIC_LVTT, lvtt1_value);

	/*
	 * Divide PICLK by 16
	 */
	tmp_value = apic_read(APIC_TDCR);
	apic_write(APIC_TDCR, (tmp_value
				& ~(APIC_TDR_DIV_1 | APIC_TDR_DIV_TMBASE))
				| APIC_TDR_DIV_16);

	tmp_value = apic_read(APIC_TMICT);
	apic_write(APIC_TMICT, clocks/APIC_DIVISOR);
}

void setup_APIC_timer(void * data)
{
	unsigned int clocks = (unsigned int) data, slice, t0, t1, nr;
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
	nr = cpu_number_map[smp_processor_id()] + 1;
	printk("cpu: %d, clocks: %d, slice: %d, nr: %d.\n",
		smp_processor_id(), clocks, slice, nr);
	/*
	 * Wait for IRQ0's slice:
	 */
	wait_8254_wraparound();

	__setup_APIC_LVTT(clocks);

	t0 = apic_read(APIC_TMCCT)*APIC_DIVISOR;
	do {
		t1 = apic_read(APIC_TMCCT)*APIC_DIVISOR;
		delta = (int)(t0 - t1 - slice*nr);
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

void __init setup_APIC_clocks(void)
{
	unsigned long flags;

	__save_flags(flags);
	__cli();

	calibration_result = calibrate_APIC_clock();

	smp_call_function(setup_APIC_timer, (void *)calibration_result, 1, 1);

	/*
	 * Now set up the timer for real.
	 */
	setup_APIC_timer((void *)calibration_result);

	__restore_flags(flags);
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
	int user = (user_mode(regs) != 0);
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
		int system = 1 - user;
		struct task_struct * p = current;

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


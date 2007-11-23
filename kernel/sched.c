/*
 *  linux/kernel/sched.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1996-12-23  Modified by Dave Grothe to fix bugs in semaphores and
 *              make semaphores SMP safe
 *  1997-01-28  Modified by Finn Arne Gangstad to make timers scale better.
 *  1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 *  1998-11-19	Implemented schedule_timeout() and related stuff
 *		by Andrea Arcangeli
 *  1998-12-24	Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *		serialize accesses to xtime/lost_ticks).
 *				Copyright (C) 1998  Andrea Arcangeli
 *  1998-12-28  Implemented better SMP scheduling by Ingo Molnar
 *  1999-03-10	Improved NTP compatibility by Ulrich Windl
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid()), which just extract a field from
 * current-task
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/fdreg.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/semaphore-helper.h>

#include <linux/timex.h>

/*
 * kernel variables
 */

unsigned securebits = SECUREBITS_DEFAULT; /* systemwide security settings */

long tick = (1000000 + HZ/2) / HZ;	/* timer interrupt period */

/* The current time */
volatile struct timeval xtime __attribute__ ((aligned (16)));

/* Don't completely fail for HZ > 500.  */
int tickadj = 500/HZ ? : 1;		/* microsecs */

DECLARE_TASK_QUEUE(tq_timer);
DECLARE_TASK_QUEUE(tq_immediate);
DECLARE_TASK_QUEUE(tq_scheduler);

/*
 * phase-lock loop variables
 */
/* TIME_ERROR prevents overwriting the CMOS clock */
int time_state = TIME_OK;	/* clock synchronization status */
int time_status = STA_UNSYNC;	/* clock status bits */
long time_offset = 0;		/* time adjustment (us) */
long time_constant = 2;		/* pll time constant */
long time_tolerance = MAXFREQ;	/* frequency tolerance (ppm) */
long time_precision = 1;	/* clock precision (us) */
long time_maxerror = NTP_PHASE_LIMIT;	/* maximum error (us) */
long time_esterror = NTP_PHASE_LIMIT;	/* estimated error (us) */
long time_phase = 0;		/* phase offset (scaled us) */
long time_freq = ((1000000 + HZ/2) % HZ - HZ/2) << SHIFT_USEC;	/* frequency offset (scaled ppm) */
long time_adj = 0;		/* tick adjust (scaled 1 / HZ) */
long time_reftime = 0;		/* time at last adjustment (s) */

long time_adjust = 0;
long time_adjust_step = 0;

unsigned long event = 0;

extern int do_setitimer(int, struct itimerval *, struct itimerval *);
unsigned int * prof_buffer = NULL;
unsigned long prof_len = 0;
unsigned long prof_shift = 0;

extern void mem_use(void);

unsigned long volatile jiffies=0;

/*
 *	Init task must be ok at boot for the ix86 as we will check its signals
 *	via the SMP irq return path.
 */
 
struct task_struct * task[NR_TASKS] = {&init_task, };

/*
 * We align per-CPU scheduling data on cacheline boundaries,
 * to prevent cacheline ping-pong.
 */
static union {
	struct schedule_data {
		struct task_struct * curr;
		cycles_t last_schedule;
	} schedule_data;
	char __pad [SMP_CACHE_BYTES];
} aligned_data [NR_CPUS] __cacheline_aligned = { {{&init_task,0}}};

#define cpu_curr(cpu) aligned_data[(cpu)].schedule_data.curr

struct kernel_stat kstat = { 0 };

#ifdef __SMP__

#define idle_task(cpu) (task[cpu_number_map[(cpu)]])
#define can_schedule(p)	(!(p)->has_cpu)

#else

#define idle_task(cpu) (&init_task)
#define can_schedule(p) (1)

#endif

void scheduling_functions_start_here(void) { }

/*
 * This is the function that decides how desirable a process is..
 * You can weigh different processes against each other depending
 * on what CPU they've run on lately etc to try to handle cache
 * and TLB miss penalties.
 *
 * Return values:
 *	 -1000: never select this
 *	     0: out of time, recalculate counters (but it might still be
 *		selected)
 *	   +ve: "goodness" value (the larger, the better)
 *	 +1000: realtime process, select this.
 */

static inline int goodness (struct task_struct * prev,
				 struct task_struct * p, int this_cpu)
{
	int weight;

	/*
	 * Realtime process, select the first one on the
	 * runqueue (taking priorities within processes
	 * into account).
	 */
	if (p->policy != SCHED_OTHER) {
		weight = 1000 + p->rt_priority;
		goto out;
	}

	/*
	 * Give the process a first-approximation goodness value
	 * according to the number of clock-ticks it has left.
	 *
	 * Don't do any other calculations if the time slice is
	 * over..
	 */
	weight = p->counter;
	if (!weight)
		goto out;
			
#ifdef __SMP__
	/* Give a largish advantage to the same processor...   */
	/* (this is equivalent to penalizing other processors) */
	if (p->processor == this_cpu)
		weight += PROC_CHANGE_PENALTY;
#endif

	/* .. and a slight advantage to the current MM */
	if (p->mm == prev->mm)
		weight += 1;
	weight += p->priority;

out:
	return weight;
}

/*
 * subtle. We want to discard a yielded process only if it's being
 * considered for a reschedule. Wakeup-time 'queries' of the scheduling
 * state do not count. Another optimization we do: sched_yield()-ed
 * processes are runnable (and thus will be considered for scheduling)
 * right when they are calling schedule(). So the only place we need
 * to care about SCHED_YIELD is when we calculate the previous process'
 * goodness ...
 */
static inline int prev_goodness (struct task_struct * prev,
					struct task_struct * p, int this_cpu)
{
	if (p->policy & SCHED_YIELD) {
		p->policy &= ~SCHED_YIELD;
		return 0;
	}
	return goodness(prev, p, this_cpu);
}

/*
 * the 'goodness value' of replacing a process on a given CPU.
 * positive value means 'replace', zero or negative means 'dont'.
 */
static inline int preemption_goodness (struct task_struct * prev,
				struct task_struct * p, int cpu)
{
	return goodness(prev, p, cpu) - goodness(prev, prev, cpu);
}

/*
 * If there is a dependency between p1 and p2,
 * don't be too eager to go into the slow schedule.
 * In particular, if p1 and p2 both want the kernel
 * lock, there is no point in trying to make them
 * extremely parallel..
 *
 * (No lock - lock_depth < 0)
 *
 * There are two additional metrics here:
 *
 * first, a 'cutoff' interval, currently 0-200 usecs on
 * x86 CPUs, depending on the size of the 'SMP-local cache'.
 * If the current process has longer average timeslices than
 * this, then we utilize the idle CPU.
 *
 * second, if the wakeup comes from a process context,
 * then the two processes are 'related'. (they form a
 * 'gang')
 *
 * An idle CPU is almost always a bad thing, thus we skip
 * the idle-CPU utilization only if both these conditions
 * are true. (ie. a 'process-gang' rescheduling with rather
 * high frequency should stay on the same CPU).
 *
 * [We can switch to something more finegrained in 2.3.]
 *
 * do not 'guess' if the to-be-scheduled task is RT.
 */
#define related(p1,p2) (((p1)->lock_depth >= 0) && (p2)->lock_depth >= 0) && \
	(((p2)->policy == SCHED_OTHER) && ((p1)->avg_slice < cacheflush_time))

static inline void reschedule_idle_slow(struct task_struct * p)
{
#ifdef __SMP__
/*
 * (see reschedule_idle() for an explanation first ...)
 *
 * Pass #2
 *
 * We try to find another (idle) CPU for this woken-up process.
 *
 * On SMP, we mostly try to see if the CPU the task used
 * to run on is idle.. but we will use another idle CPU too,
 * at this point we already know that this CPU is not
 * willing to reschedule in the near future.
 *
 * An idle CPU is definitely wasted, especially if this CPU is
 * running long-timeslice processes. The following algorithm is
 * pretty good at finding the best idle CPU to send this process
 * to.
 *
 * [We can try to preempt low-priority processes on other CPUs in
 * 2.3. Also we can try to use the avg_slice value to predict
 * 'likely reschedule' events even on other CPUs.]
 */
	int this_cpu = smp_processor_id(), target_cpu;
	struct task_struct *tsk, *target_tsk;
	int cpu, best_cpu, weight, best_weight, i;
	unsigned long flags;

	best_weight = 0; /* prevents negative weight */

	spin_lock_irqsave(&runqueue_lock, flags);

	/*
	 * shortcut if the woken up task's last CPU is
	 * idle now.
	 */
	best_cpu = p->processor;
	target_tsk = idle_task(best_cpu);
	if (cpu_curr(best_cpu) == target_tsk)
		goto send_now;

	target_tsk = NULL;
	for (i = 0; i < smp_num_cpus; i++) {
		cpu = cpu_logical_map(i);
		tsk = cpu_curr(cpu);
		if (related(tsk, p))
			goto out_no_target;
		weight = preemption_goodness(tsk, p, cpu);
		if (weight > best_weight) {
			best_weight = weight;
			target_tsk = tsk;
		}
	}

	/*
	 * found any suitable CPU?
	 */
	if (!target_tsk)
		goto out_no_target;
		
send_now:
	target_cpu = target_tsk->processor;
	target_tsk->need_resched = 1;
	spin_unlock_irqrestore(&runqueue_lock, flags);
	/*
	 * the APIC stuff can go outside of the lock because
	 * it uses no task information, only CPU#.
	 */
	if (target_cpu != this_cpu)
		smp_send_reschedule(target_cpu);
	return;
out_no_target:
	spin_unlock_irqrestore(&runqueue_lock, flags);
	return;
#else /* UP */
	int this_cpu = smp_processor_id();
	struct task_struct *tsk;

	tsk = cpu_curr(this_cpu);
	if (preemption_goodness(tsk, p, this_cpu) > 0)
		tsk->need_resched = 1;
#endif
}

static void reschedule_idle(struct task_struct * p)
{
#ifdef __SMP__
	int cpu = smp_processor_id();
	/*
	 * ("wakeup()" should not be called before we've initialized
	 * SMP completely.
	 * Basically a not-yet initialized SMP subsystem can be
	 * considered as a not-yet working scheduler, simply dont use
	 * it before it's up and running ...)
	 *
	 * SMP rescheduling is done in 2 passes:
	 *  - pass #1: faster: 'quick decisions'
	 *  - pass #2: slower: 'lets try and find a suitable CPU'
	 */

	/*
	 * Pass #1. (subtle. We might be in the middle of __switch_to, so
	 * to preserve scheduling atomicity we have to use cpu_curr)
	 */
	if ((p->processor == cpu) && related(cpu_curr(cpu), p))
		return;
#endif /* __SMP__ */
	/*
	 * Pass #2
	 */
	reschedule_idle_slow(p);
}

/*
 * Careful!
 *
 * This has to add the process to the _beginning_ of the
 * run-queue, not the end. See the comment about "This is
 * subtle" in the scheduler proper..
 */
static inline void add_to_runqueue(struct task_struct * p)
{
	struct task_struct *next = init_task.next_run;

	p->prev_run = &init_task;
	init_task.next_run = p;
	p->next_run = next;
	next->prev_run = p;
	nr_running++;
}

static inline void del_from_runqueue(struct task_struct * p)
{
	struct task_struct *next = p->next_run;
	struct task_struct *prev = p->prev_run;

	nr_running--;
	next->prev_run = prev;
	prev->next_run = next;
	p->next_run = NULL;
	p->prev_run = NULL;
}

static inline void move_last_runqueue(struct task_struct * p)
{
	struct task_struct *next = p->next_run;
	struct task_struct *prev = p->prev_run;

	/* remove from list */
	next->prev_run = prev;
	prev->next_run = next;
	/* add back to list */
	p->next_run = &init_task;
	prev = init_task.prev_run;
	init_task.prev_run = p;
	p->prev_run = prev;
	prev->next_run = p;
}

static inline void move_first_runqueue(struct task_struct * p)
{
	struct task_struct *next = p->next_run;
	struct task_struct *prev = p->prev_run;

	/* remove from list */
	next->prev_run = prev;
	prev->next_run = next;
	/* add back to list */
	p->prev_run = &init_task;
	next = init_task.next_run;
	init_task.next_run = p;
	p->next_run = next;
	next->prev_run = p;
}

/*
 * The tasklist_lock protects the linked list of processes.
 *
 * The scheduler lock is protecting against multiple entry
 * into the scheduling code, and doesn't need to worry
 * about interrupts (because interrupts cannot call the
 * scheduler).
 *
 * The run-queue lock locks the parts that actually access
 * and change the run-queues, and have to be interrupt-safe.
 */
spinlock_t runqueue_lock = SPIN_LOCK_UNLOCKED;  /* second */
rwlock_t tasklist_lock = RW_LOCK_UNLOCKED;	/* third */

/*
 * Wake up a process. Put it on the run-queue if it's not
 * already there.  The "current" process is always on the
 * run-queue (except when the actual re-schedule is in
 * progress), and as such you're allowed to do the simpler
 * "current->state = TASK_RUNNING" to mark yourself runnable
 * without the overhead of this.
 */
void wake_up_process(struct task_struct * p)
{
	unsigned long flags;

	/*
	 * We want the common case fall through straight, thus the goto.
	 */
	spin_lock_irqsave(&runqueue_lock, flags);
	p->state = TASK_RUNNING;
	if (p->next_run)
		goto out;
	add_to_runqueue(p);
	spin_unlock_irqrestore(&runqueue_lock, flags);

	reschedule_idle(p);
	return;
out:
	spin_unlock_irqrestore(&runqueue_lock, flags);
}

static void process_timeout(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;

	wake_up_process(p);
}

/*
 * Event timer code
 */
#define TVN_BITS 6
#define TVR_BITS 8
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)

struct timer_vec {
        int index;
        struct timer_list *vec[TVN_SIZE];
};

struct timer_vec_root {
        int index;
        struct timer_list *vec[TVR_SIZE];
};

static struct timer_vec tv5 = { 0 };
static struct timer_vec tv4 = { 0 };
static struct timer_vec tv3 = { 0 };
static struct timer_vec tv2 = { 0 };
static struct timer_vec_root tv1 = { 0 };

static struct timer_vec * const tvecs[] = {
	(struct timer_vec *)&tv1, &tv2, &tv3, &tv4, &tv5
};

#define NOOF_TVECS (sizeof(tvecs) / sizeof(tvecs[0]))

static unsigned long timer_jiffies = 0;

static inline void insert_timer(struct timer_list *timer,
				struct timer_list **vec, int idx)
{
	if ((timer->next = vec[idx]))
		vec[idx]->prev = timer;
	vec[idx] = timer;
	timer->prev = (struct timer_list *)&vec[idx];
}

static inline void internal_add_timer(struct timer_list *timer)
{
	/*
	 * must be cli-ed when calling this
	 */
	unsigned long expires = timer->expires;
	unsigned long idx = expires - timer_jiffies;

	if (idx < TVR_SIZE) {
		int i = expires & TVR_MASK;
		insert_timer(timer, tv1.vec, i);
	} else if (idx < 1 << (TVR_BITS + TVN_BITS)) {
		int i = (expires >> TVR_BITS) & TVN_MASK;
		insert_timer(timer, tv2.vec, i);
	} else if (idx < 1 << (TVR_BITS + 2 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
		insert_timer(timer, tv3.vec, i);
	} else if (idx < 1 << (TVR_BITS + 3 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
		insert_timer(timer, tv4.vec, i);
	} else if ((signed long) idx < 0) {
		/* can happen if you add a timer with expires == jiffies,
		 * or you set a timer to go off in the past
		 */
		insert_timer(timer, tv1.vec, tv1.index);
	} else if (idx <= 0xffffffffUL) {
		int i = (expires >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
		insert_timer(timer, tv5.vec, i);
	} else {
		/* Can only get here on architectures with 64-bit jiffies */
		timer->next = timer->prev = timer;
	}
}

spinlock_t timerlist_lock = SPIN_LOCK_UNLOCKED;

void add_timer(struct timer_list *timer)
{
	unsigned long flags;

	spin_lock_irqsave(&timerlist_lock, flags);
	if (timer->prev)
		goto bug;
	internal_add_timer(timer);
out:
	spin_unlock_irqrestore(&timerlist_lock, flags);
	return;

bug:
	printk("bug: kernel timer added twice at %p.\n",
			__builtin_return_address(0));
	goto out;
}

static inline int detach_timer(struct timer_list *timer)
{
	struct timer_list *prev = timer->prev;
	if (prev) {
		struct timer_list *next = timer->next;
		prev->next = next;
		if (next)
			next->prev = prev;
		return 1;
	}
	return 0;
}

void mod_timer(struct timer_list *timer, unsigned long expires)
{
	unsigned long flags;

	spin_lock_irqsave(&timerlist_lock, flags);
	timer->expires = expires;
	detach_timer(timer);
	internal_add_timer(timer);
	spin_unlock_irqrestore(&timerlist_lock, flags);
}

int del_timer(struct timer_list * timer)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&timerlist_lock, flags);
	ret = detach_timer(timer);
	timer->next = timer->prev = 0;
	spin_unlock_irqrestore(&timerlist_lock, flags);
	return ret;
}

signed long schedule_timeout(signed long timeout)
{
	struct timer_list timer;
	unsigned long expire;

	switch (timeout)
	{
	case MAX_SCHEDULE_TIMEOUT:
		/*
		 * These two special cases are useful to be comfortable
		 * in the caller. Nothing more. We could take
		 * MAX_SCHEDULE_TIMEOUT from one of the negative value
		 * but I' d like to return a valid offset (>=0) to allow
		 * the caller to do everything it want with the retval.
		 */
		schedule();
		goto out;
	default:
		/*
		 * Another bit of PARANOID. Note that the retval will be
		 * 0 since no piece of kernel is supposed to do a check
		 * for a negative retval of schedule_timeout() (since it
		 * should never happens anyway). You just have the printk()
		 * that will tell you if something is gone wrong and where.
		 */
		if (timeout < 0)
		{
			printk(KERN_ERR "schedule_timeout: wrong timeout "
			       "value %lx from %p\n", timeout,
			       __builtin_return_address(0));
			goto out;
		}
	}

	expire = timeout + jiffies;

	init_timer(&timer);
	timer.expires = expire;
	timer.data = (unsigned long) current;
	timer.function = process_timeout;

	add_timer(&timer);
	schedule();
	del_timer(&timer);

	timeout = expire - jiffies;

 out:
	return timeout < 0 ? 0 : timeout;
}

/*
 * schedule_tail() is getting called from the fork return path. This
 * cleans up all remaining scheduler things, without impacting the
 * common case.
 */
static inline void __schedule_tail (struct task_struct *prev)
{
#ifdef __SMP__
	if ((prev->state == TASK_RUNNING) &&
			(prev != idle_task(smp_processor_id())))
		reschedule_idle(prev);
	wmb();
	prev->has_cpu = 0;
#endif /* __SMP__ */
}

void schedule_tail (struct task_struct *prev)
{
	__schedule_tail(prev);
}

/*
 *  'schedule()' is the scheduler function. It's a very simple and nice
 * scheduler: it's not perfect, but certainly works for most things.
 *
 * The goto is "interesting".
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
asmlinkage void schedule(void)
{
	struct schedule_data * sched_data;
	struct task_struct *prev, *next, *p;
	int this_cpu, c;

	if (tq_scheduler)
		goto handle_tq_scheduler;
tq_scheduler_back:

	prev = current;
	this_cpu = prev->processor;

	if (in_interrupt())
		goto scheduling_in_interrupt;

	release_kernel_lock(prev, this_cpu);

	/* Do "administrative" work here while we don't hold any locks */
	if (bh_mask & bh_active)
		goto handle_bh;
handle_bh_back:

	/*
	 * 'sched_data' is protected by the fact that we can run
	 * only one process per CPU.
	 */
	sched_data = & aligned_data[this_cpu].schedule_data;

	spin_lock_irq(&runqueue_lock);

	/* move an exhausted RR process to be last.. */
	if (prev->policy == SCHED_RR)
		goto move_rr_last;
move_rr_back:

	switch (prev->state) {
		case TASK_INTERRUPTIBLE:
			if (signal_pending(prev)) {
				prev->state = TASK_RUNNING;
				break;
			}
		default:
			del_from_runqueue(prev);
		case TASK_RUNNING:
	}
	prev->need_resched = 0;

repeat_schedule:

	/*
	 * this is the scheduler proper:
	 */

	p = init_task.next_run;
	/* Default process to select.. */
	next = idle_task(this_cpu);
	c = -1000;
	if (prev->state == TASK_RUNNING)
		goto still_running;
still_running_back:

	/*
	 * This is subtle.
	 * Note how we can enable interrupts here, even
	 * though interrupts can add processes to the run-
	 * queue. This is because any new processes will
	 * be added to the front of the queue, so "p" above
	 * is a safe starting point.
	 * run-queue deletion and re-ordering is protected by
	 * the scheduler lock
	 */
/*
 * Note! there may appear new tasks on the run-queue during this, as
 * interrupts are enabled. However, they will be put on front of the
 * list, so our list starting at "p" is essentially fixed.
 */
	while (p != &init_task) {
		if (can_schedule(p)) {
			int weight = goodness(prev, p, this_cpu);
			if (weight > c)
				c = weight, next = p;
		}
		p = p->next_run;
	}

	/* Do we need to re-calculate counters? */
	if (!c)
		goto recalculate;
	/*
	 * from this point on nothing can prevent us from
	 * switching to the next task, save this fact in
	 * sched_data.
	 */
	sched_data->curr = next;
#ifdef __SMP__
 	next->has_cpu = 1;
	next->processor = this_cpu;
#endif
	spin_unlock_irq(&runqueue_lock);

	if (prev == next)
		goto same_process;

#ifdef __SMP__
 	/*
 	 * maintain the per-process 'average timeslice' value.
 	 * (this has to be recalculated even if we reschedule to
 	 * the same process) Currently this is only used on SMP,
	 * and it's approximate, so we do not have to maintain
	 * it while holding the runqueue spinlock.
 	 */
	{
		cycles_t t, this_slice;

		t = get_cycles();
		this_slice = t - sched_data->last_schedule;
		sched_data->last_schedule = t;

		/*
		 * Exponentially fading average calculation, with
		 * some weight so it doesnt get fooled easily by
		 * smaller irregularities.
		 */
		prev->avg_slice = (this_slice*1 + prev->avg_slice*1)/2;
	}

	/*
	 * We drop the scheduler lock early (it's a global spinlock),
	 * thus we have to lock the previous process from getting
	 * rescheduled during switch_to().
	 */

#endif /* __SMP__ */

	kstat.context_swtch++;
	get_mmu_context(next);
	switch_to(prev, next, prev);
	__schedule_tail(prev);

same_process:
  
	reacquire_kernel_lock(current);
	return;

recalculate:
	{
		struct task_struct *p;
		spin_unlock_irq(&runqueue_lock);
		read_lock(&tasklist_lock);
		for_each_task(p)
			p->counter = (p->counter >> 1) + p->priority;
		read_unlock(&tasklist_lock);
		spin_lock_irq(&runqueue_lock);
		goto repeat_schedule;
	}

still_running:
	c = prev_goodness(prev, prev, this_cpu);
	next = prev;
	goto still_running_back;

handle_bh:
	do_bottom_half();
	goto handle_bh_back;

handle_tq_scheduler:
	run_task_queue(&tq_scheduler);
	goto tq_scheduler_back;

move_rr_last:
	if (!prev->counter) {
		prev->counter = prev->priority;
		move_last_runqueue(prev);
	}
	goto move_rr_back;

scheduling_in_interrupt:
	printk("Scheduling in interrupt\n");
	*(int *)0 = 0;
	return;
}

rwlock_t waitqueue_lock = RW_LOCK_UNLOCKED;

/*
 * wake_up doesn't wake up stopped processes - they have to be awakened
 * with signals or similar.
 *
 * Note that we only need a read lock for the wait queue (and thus do not
 * have to protect against interrupts), as the actual removal from the
 * queue is handled by the process itself.
 */
void __wake_up(struct wait_queue **q, unsigned int mode)
{
	struct task_struct *p;
	struct wait_queue *head, *next;

        if (!q)
		goto out;
	/*
	 * this is safe to be done before the check because it
	 * means no deference, just pointer operations.
	 */
	head = WAIT_QUEUE_HEAD(q);

	read_lock(&waitqueue_lock);
	next = *q;
	if (!next)
		goto out_unlock;

	while (next != head) {
		p = next->task;
		next = next->next;
		if (p->state & mode) {
			/*
			 * We can drop the read-lock early if this
			 * is the only/last process.
			 */
			if (next == head) {
				read_unlock(&waitqueue_lock);
				wake_up_process(p);
				goto out;
			}
			wake_up_process(p);
		}
	}
out_unlock:
	read_unlock(&waitqueue_lock);
out:
	return;
}

/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to sleep, while the "waking" variable is
 * incremented when the "up()" code goes to wake up waiting
 * processes.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * waking_non_zero() (from asm/semaphore.h) must execute
 * atomically.
 *
 * When __up() is called, the count was negative before
 * incrementing it, and we need to wake up somebody.
 *
 * This routine adds one to the count of processes that need to
 * wake up and exit.  ALL waiting processes actually wake up but
 * only the one that gets to the "waking" field first will gate
 * through and acquire the semaphore.  The others will go back
 * to sleep.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */
void __up(struct semaphore *sem)
{
	wake_one_more(sem);
	wake_up(&sem->wait);
}

/*
 * Perform the "down" function.  Return zero for semaphore acquired,
 * return negative for signalled out of the function.
 *
 * If called from __down, the return is ignored and the wait loop is
 * not interruptible.  This means that a task waiting on a semaphore
 * using "down()" cannot be killed until someone does an "up()" on
 * the semaphore.
 *
 * If called from __down_interruptible, the return value gets checked
 * upon return.  If the return value is negative then the task continues
 * with the negative value in the return register (it can be tested by
 * the caller).
 *
 * Either form may be used in conjunction with "up()".
 *
 */

#define DOWN_VAR				\
	struct task_struct *tsk = current;	\
	struct wait_queue wait = { tsk, NULL };

#define DOWN_HEAD(task_state)						\
									\
									\
	tsk->state = (task_state);					\
	add_wait_queue(&sem->wait, &wait);				\
									\
	/*								\
	 * Ok, we're set up.  sem->count is known to be less than zero	\
	 * so we must wait.						\
	 *								\
	 * We can let go the lock for purposes of waiting.		\
	 * We re-acquire it after awaking so as to protect		\
	 * all semaphore operations.					\
	 *								\
	 * If "up()" is called before we call waking_non_zero() then	\
	 * we will catch it right away.  If it is called later then	\
	 * we will have to go through a wakeup cycle to catch it.	\
	 *								\
	 * Multiple waiters contend for the semaphore lock to see	\
	 * who gets to gate through and who has to wait some more.	\
	 */								\
	for (;;) {

#define DOWN_TAIL(task_state)			\
		tsk->state = (task_state);	\
	}					\
	tsk->state = TASK_RUNNING;		\
	remove_wait_queue(&sem->wait, &wait);

void __down(struct semaphore * sem)
{
	DOWN_VAR
	DOWN_HEAD(TASK_UNINTERRUPTIBLE)
	if (waking_non_zero(sem))
		break;
	schedule();
	DOWN_TAIL(TASK_UNINTERRUPTIBLE)
}

int __down_interruptible(struct semaphore * sem)
{
	DOWN_VAR
	int ret = 0;
	DOWN_HEAD(TASK_INTERRUPTIBLE)

	ret = waking_non_zero_interruptible(sem, tsk);
	if (ret)
	{
		if (ret == 1)
			/* ret != 0 only if we get interrupted -arca */
			ret = 0;
		break;
	}
	schedule();
	DOWN_TAIL(TASK_INTERRUPTIBLE)
	return ret;
}

int __down_trylock(struct semaphore * sem)
{
	return waking_non_zero_trylock(sem);
}

#define	SLEEP_ON_VAR				\
	unsigned long flags;			\
	struct wait_queue wait;

#define	SLEEP_ON_HEAD					\
	wait.task = current;				\
	write_lock_irqsave(&waitqueue_lock,flags);	\
	__add_wait_queue(p, &wait);			\
	write_unlock(&waitqueue_lock);

#define	SLEEP_ON_TAIL						\
	write_lock_irq(&waitqueue_lock);			\
	__remove_wait_queue(p, &wait);				\
	write_unlock_irqrestore(&waitqueue_lock,flags);

void interruptible_sleep_on(struct wait_queue **p)
{
	SLEEP_ON_VAR

	current->state = TASK_INTERRUPTIBLE;

	SLEEP_ON_HEAD
	schedule();
	SLEEP_ON_TAIL
}

long interruptible_sleep_on_timeout(struct wait_queue **p, long timeout)
{
	SLEEP_ON_VAR

	current->state = TASK_INTERRUPTIBLE;

	SLEEP_ON_HEAD
	timeout = schedule_timeout(timeout);
	SLEEP_ON_TAIL

	return timeout;
}

void sleep_on(struct wait_queue **p)
{
	SLEEP_ON_VAR
	
	current->state = TASK_UNINTERRUPTIBLE;

	SLEEP_ON_HEAD
	schedule();
	SLEEP_ON_TAIL
}

long sleep_on_timeout(struct wait_queue **p, long timeout)
{
	SLEEP_ON_VAR
	
	current->state = TASK_UNINTERRUPTIBLE;

	SLEEP_ON_HEAD
	timeout = schedule_timeout(timeout);
	SLEEP_ON_TAIL

	return timeout;
}

void scheduling_functions_end_here(void) { }

static inline void cascade_timers(struct timer_vec *tv)
{
        /* cascade all the timers from tv up one level */
        struct timer_list *timer;
        timer = tv->vec[tv->index];
        /*
         * We are removing _all_ timers from the list, so we don't  have to
         * detach them individually, just clear the list afterwards.
         */
        while (timer) {
                struct timer_list *tmp = timer;
                timer = timer->next;
                internal_add_timer(tmp);
        }
        tv->vec[tv->index] = NULL;
        tv->index = (tv->index + 1) & TVN_MASK;
}

static inline void run_timer_list(void)
{
	spin_lock_irq(&timerlist_lock);
	while ((long)(jiffies - timer_jiffies) >= 0) {
		struct timer_list *timer;
		if (!tv1.index) {
			int n = 1;
			do {
				cascade_timers(tvecs[n]);
			} while (tvecs[n]->index == 1 && ++n < NOOF_TVECS);
		}
		while ((timer = tv1.vec[tv1.index])) {
			void (*fn)(unsigned long) = timer->function;
			unsigned long data = timer->data;
			detach_timer(timer);
			timer->next = timer->prev = NULL;
			spin_unlock_irq(&timerlist_lock);
			fn(data);
			spin_lock_irq(&timerlist_lock);
		}
		++timer_jiffies; 
		tv1.index = (tv1.index + 1) & TVR_MASK;
	}
	spin_unlock_irq(&timerlist_lock);
}


static inline void run_old_timers(void)
{
	struct timer_struct *tp;
	unsigned long mask;

	for (mask = 1, tp = timer_table+0 ; mask ; tp++,mask += mask) {
		if (mask > timer_active)
			break;
		if (!(mask & timer_active))
			continue;
		if (time_after(tp->expires, jiffies))
			continue;
		timer_active &= ~mask;
		tp->fn();
		sti();
	}
}

spinlock_t tqueue_lock;

void tqueue_bh(void)
{
	run_task_queue(&tq_timer);
}

void immediate_bh(void)
{
	run_task_queue(&tq_immediate);
}

unsigned long timer_active = 0;
struct timer_struct timer_table[32];

/*
 * Hmm.. Changed this, as the GNU make sources (load.c) seems to
 * imply that avenrun[] is the standard name for this kind of thing.
 * Nothing else seems to be standardized: the fractional size etc
 * all seem to differ on different machines.
 */
unsigned long avenrun[3] = { 0,0,0 };

/*
 * Nr of active tasks - counted in fixed-point numbers
 */
static unsigned long count_active_tasks(void)
{
	struct task_struct *p;
	unsigned long nr = 0;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if ((p->state == TASK_RUNNING ||
		     p->state == TASK_UNINTERRUPTIBLE ||
		     p->state == TASK_SWAPPING))
			nr += FIXED_1;
	}
	read_unlock(&tasklist_lock);
	return nr;
}

static inline void calc_load(unsigned long ticks)
{
	unsigned long active_tasks; /* fixed-point */
	static int count = LOAD_FREQ;

	count -= ticks;
	if (count < 0) {
		count += LOAD_FREQ;
		active_tasks = count_active_tasks();
		CALC_LOAD(avenrun[0], EXP_1, active_tasks);
		CALC_LOAD(avenrun[1], EXP_5, active_tasks);
		CALC_LOAD(avenrun[2], EXP_15, active_tasks);
	}
}

/*
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 *
 */
static void second_overflow(void)
{
    long ltemp;

    /* Bump the maxerror field */
    time_maxerror += time_tolerance >> SHIFT_USEC;
    if ( time_maxerror > NTP_PHASE_LIMIT ) {
        time_maxerror = NTP_PHASE_LIMIT;
	time_status |= STA_UNSYNC;
    }

    /*
     * Leap second processing. If in leap-insert state at
     * the end of the day, the system clock is set back one
     * second; if in leap-delete state, the system clock is
     * set ahead one second. The microtime() routine or
     * external clock driver will insure that reported time
     * is always monotonic. The ugly divides should be
     * replaced.
     */
    switch (time_state) {

    case TIME_OK:
	if (time_status & STA_INS)
	    time_state = TIME_INS;
	else if (time_status & STA_DEL)
	    time_state = TIME_DEL;
	break;

    case TIME_INS:
	if (xtime.tv_sec % 86400 == 0) {
	    xtime.tv_sec--;
	    time_state = TIME_OOP;
	    printk(KERN_NOTICE "Clock: inserting leap second 23:59:60 UTC\n");
	}
	break;

    case TIME_DEL:
	if ((xtime.tv_sec + 1) % 86400 == 0) {
	    xtime.tv_sec++;
	    time_state = TIME_WAIT;
	    printk(KERN_NOTICE "Clock: deleting leap second 23:59:59 UTC\n");
	}
	break;

    case TIME_OOP:
	time_state = TIME_WAIT;
	break;

    case TIME_WAIT:
	if (!(time_status & (STA_INS | STA_DEL)))
	    time_state = TIME_OK;
    }

    /*
     * Compute the phase adjustment for the next second. In
     * PLL mode, the offset is reduced by a fixed factor
     * times the time constant. In FLL mode the offset is
     * used directly. In either mode, the maximum phase
     * adjustment for each second is clamped so as to spread
     * the adjustment over not more than the number of
     * seconds between updates.
     */
    if (time_offset < 0) {
	ltemp = -time_offset;
	if (!(time_status & STA_FLL))
	    ltemp >>= SHIFT_KG + time_constant;
	if (ltemp > (MAXPHASE / MINSEC) << SHIFT_UPDATE)
	    ltemp = (MAXPHASE / MINSEC) << SHIFT_UPDATE;
	time_offset += ltemp;
	time_adj = -ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
    } else {
	ltemp = time_offset;
	if (!(time_status & STA_FLL))
	    ltemp >>= SHIFT_KG + time_constant;
	if (ltemp > (MAXPHASE / MINSEC) << SHIFT_UPDATE)
	    ltemp = (MAXPHASE / MINSEC) << SHIFT_UPDATE;
	time_offset -= ltemp;
	time_adj = ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
    }

    /*
     * Compute the frequency estimate and additional phase
     * adjustment due to frequency error for the next
     * second. When the PPS signal is engaged, gnaw on the
     * watchdog counter and update the frequency computed by
     * the pll and the PPS signal.
     */
    pps_valid++;
    if (pps_valid == PPS_VALID) {	/* PPS signal lost */
	pps_jitter = MAXTIME;
	pps_stabil = MAXFREQ;
	time_status &= ~(STA_PPSSIGNAL | STA_PPSJITTER |
			 STA_PPSWANDER | STA_PPSERROR);
    }
    ltemp = time_freq + pps_freq;
    if (ltemp < 0)
	time_adj -= -ltemp >>
	    (SHIFT_USEC + SHIFT_HZ - SHIFT_SCALE);
    else
	time_adj += ltemp >>
	    (SHIFT_USEC + SHIFT_HZ - SHIFT_SCALE);

#if HZ == 100
    /* Compensate for (HZ==100) != (1 << SHIFT_HZ).
     * Add 25% and 3.125% to get 128.125; => only 0.125% error (p. 14)
     */
    if (time_adj < 0)
	time_adj -= (-time_adj >> 2) + (-time_adj >> 5);
    else
	time_adj += (time_adj >> 2) + (time_adj >> 5);
#endif
}

/* in the NTP reference this is called "hardclock()" */
static void update_wall_time_one_tick(void)
{
	if ( (time_adjust_step = time_adjust) != 0 ) {
	    /* We are doing an adjtime thing. 
	     *
	     * Prepare time_adjust_step to be within bounds.
	     * Note that a positive time_adjust means we want the clock
	     * to run faster.
	     *
	     * Limit the amount of the step to be in the range
	     * -tickadj .. +tickadj
	     */
	     if (time_adjust > tickadj)
		time_adjust_step = tickadj;
	     else if (time_adjust < -tickadj)
		time_adjust_step = -tickadj;
	     
	    /* Reduce by this step the amount of time left  */
	    time_adjust -= time_adjust_step;
	}
	xtime.tv_usec += tick + time_adjust_step;
	/*
	 * Advance the phase, once it gets to one microsecond, then
	 * advance the tick more.
	 */
	time_phase += time_adj;
	if (time_phase <= -FINEUSEC) {
		long ltemp = -time_phase >> SHIFT_SCALE;
		time_phase += ltemp << SHIFT_SCALE;
		xtime.tv_usec -= ltemp;
	}
	else if (time_phase >= FINEUSEC) {
		long ltemp = time_phase >> SHIFT_SCALE;
		time_phase -= ltemp << SHIFT_SCALE;
		xtime.tv_usec += ltemp;
	}
}

/*
 * Using a loop looks inefficient, but "ticks" is
 * usually just one (we shouldn't be losing ticks,
 * we're doing this this way mainly for interrupt
 * latency reasons, not because we think we'll
 * have lots of lost timer ticks
 */
static void update_wall_time(unsigned long ticks)
{
	do {
		ticks--;
		update_wall_time_one_tick();
	} while (ticks);

	if (xtime.tv_usec >= 1000000) {
	    xtime.tv_usec -= 1000000;
	    xtime.tv_sec++;
	    second_overflow();
	}
}

static inline void do_process_times(struct task_struct *p,
	unsigned long user, unsigned long system)
{
	long psecs;

	psecs = (p->times.tms_utime += user);
	psecs += (p->times.tms_stime += system);
	if (psecs / HZ > p->rlim[RLIMIT_CPU].rlim_cur) {
		/* Send SIGXCPU every second.. */
		if (!(psecs % HZ))
			send_sig(SIGXCPU, p, 1);
		/* and SIGKILL when we go over max.. */
		if (psecs / HZ > p->rlim[RLIMIT_CPU].rlim_max)
			send_sig(SIGKILL, p, 1);
	}
}

static inline void do_it_virt(struct task_struct * p, unsigned long ticks)
{
	unsigned long it_virt = p->it_virt_value;

	if (it_virt) {
		if (it_virt <= ticks) {
			it_virt = ticks + p->it_virt_incr;
			send_sig(SIGVTALRM, p, 1);
		}
		p->it_virt_value = it_virt - ticks;
	}
}

static inline void do_it_prof(struct task_struct * p, unsigned long ticks)
{
	unsigned long it_prof = p->it_prof_value;

	if (it_prof) {
		if (it_prof <= ticks) {
			it_prof = ticks + p->it_prof_incr;
			send_sig(SIGPROF, p, 1);
		}
		p->it_prof_value = it_prof - ticks;
	}
}

void update_one_process(struct task_struct *p,
	unsigned long ticks, unsigned long user, unsigned long system, int cpu)
{
	p->per_cpu_utime[cpu] += user;
	p->per_cpu_stime[cpu] += system;
	do_process_times(p, user, system);
	do_it_virt(p, user);
	do_it_prof(p, ticks);
}	

static void update_process_times(unsigned long ticks, unsigned long system)
{
/*
 * SMP does this on a per-CPU basis elsewhere
 */
#ifndef  __SMP__
	struct task_struct * p = current;
	unsigned long user = ticks - system;
	if (p->pid) {
		p->counter -= ticks;
		if (p->counter < 0) {
			p->counter = 0;
			p->need_resched = 1;
		}
		if (p->priority < DEF_PRIORITY)
			kstat.cpu_nice += user;
		else
			kstat.cpu_user += user;
		kstat.cpu_system += system;
	}
	update_one_process(p, ticks, user, system, 0);
#endif
}

volatile unsigned long lost_ticks = 0;
static unsigned long lost_ticks_system = 0;

/*
 * This spinlock protect us from races in SMP while playing with xtime. -arca
 */
rwlock_t xtime_lock = RW_LOCK_UNLOCKED;

static inline void update_times(void)
{
	unsigned long ticks;

	/*
	 * update_times() is run from the raw timer_bh handler so we
	 * just know that the irqs are locally enabled and so we don't
	 * need to save/restore the flags of the local CPU here. -arca
	 */
	write_lock_irq(&xtime_lock);

	ticks = lost_ticks;
	lost_ticks = 0;

	if (ticks) {
		unsigned long system;
		system = xchg(&lost_ticks_system, 0);

		calc_load(ticks);
		update_wall_time(ticks);
		write_unlock_irq(&xtime_lock);
		
		update_process_times(ticks, system);

	} else
		write_unlock_irq(&xtime_lock);
}

static void timer_bh(void)
{
	update_times();
	run_old_timers();
	run_timer_list();
}

void do_timer(struct pt_regs * regs)
{
	(*(unsigned long *)&jiffies)++;
	lost_ticks++;
	mark_bh(TIMER_BH);
	if (!user_mode(regs))
		lost_ticks_system++;
	if (tq_timer)
		mark_bh(TQUEUE_BH);
}

#ifndef __alpha__

/*
 * For backwards compatibility?  This can be done in libc so Alpha
 * and all newer ports shouldn't need it.
 */
asmlinkage unsigned int sys_alarm(unsigned int seconds)
{
	struct itimerval it_new, it_old;
	unsigned int oldalarm;

	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	do_setitimer(ITIMER_REAL, &it_new, &it_old);
	oldalarm = it_old.it_value.tv_sec;
	/* ehhh.. We can't return 0 if we have an alarm pending.. */
	/* And we'd better return too much than too little anyway */
	if (it_old.it_value.tv_usec)
		oldalarm++;
	return oldalarm;
}

/*
 * The Alpha uses getxpid, getxuid, and getxgid instead.  Maybe this
 * should be moved into arch/i386 instead?
 */
 
asmlinkage int sys_getpid(void)
{
	/* This is SMP safe - current->pid doesn't change */
	return current->pid;
}

/*
 * This is not strictly SMP safe: p_opptr could change
 * from under us. However, rather than getting any lock
 * we can use an optimistic algorithm: get the parent
 * pid, and go back and check that the parent is still
 * the same. If it has changed (which is extremely unlikely
 * indeed), we just try again..
 *
 * NOTE! This depends on the fact that even if we _do_
 * get an old value of "parent", we can happily dereference
 * the pointer: we just can't necessarily trust the result
 * until we know that the parent pointer is valid.
 *
 * The "mb()" macro is a memory barrier - a synchronizing
 * event. It also makes sure that gcc doesn't optimize
 * away the necessary memory references.. The barrier doesn't
 * have to have all that strong semantics: on x86 we don't
 * really require a synchronizing instruction, for example.
 * The barrier is more important for code generation than
 * for any real memory ordering semantics (even if there is
 * a small window for a race, using the old pointer is
 * harmless for a while).
 */
asmlinkage int sys_getppid(void)
{
	int pid;
	struct task_struct * me = current;
	struct task_struct * parent;

	parent = me->p_opptr;
	for (;;) {
		pid = parent->pid;
#if __SMP__
{
		struct task_struct *old = parent;
		mb();
		parent = me->p_opptr;
		if (old != parent)
			continue;
}
#endif
		break;
	}
	return pid;
}

asmlinkage int sys_getuid(void)
{
	/* Only we change this so SMP safe */
	return current->uid;
}

asmlinkage int sys_geteuid(void)
{
	/* Only we change this so SMP safe */
	return current->euid;
}

asmlinkage int sys_getgid(void)
{
	/* Only we change this so SMP safe */
	return current->gid;
}

asmlinkage int sys_getegid(void)
{
	/* Only we change this so SMP safe */
	return  current->egid;
}

/*
 * This has been replaced by sys_setpriority.  Maybe it should be
 * moved into the arch dependent tree for those ports that require
 * it for backward compatibility?
 */

asmlinkage int sys_nice(int increment)
{
	unsigned long newprio;
	int increase = 0;

	/*
	 *	Setpriority might change our priority at the same moment.
	 *	We don't have to worry. Conceptually one call occurs first
	 *	and we have a single winner.
	 */
	 
	newprio = increment;
	if (increment < 0) {
		if (!capable(CAP_SYS_NICE))
			return -EPERM;
		newprio = -increment;
		increase = 1;
	}

	if (newprio > 40)
		newprio = 40;
	/*
	 * do a "normalization" of the priority (traditionally
	 * Unix nice values are -20 to 20; Linux doesn't really
	 * use that kind of thing, but uses the length of the
	 * timeslice instead (default 210 ms). The rounding is
	 * why we want to avoid negative values.
	 */
	newprio = (newprio * DEF_PRIORITY + 10) / 20;
	increment = newprio;
	if (increase)
		increment = -increment;
	/*
	 *	Current->priority can change between this point
	 *	and the assignment. We are assigning not doing add/subs
	 *	so thats ok. Conceptually a process might just instantaneously
	 *	read the value we stomp over. I don't think that is an issue
	 *	unless posix makes it one. If so we can loop on changes
	 *	to current->priority.
	 */
	newprio = current->priority - increment;
	if ((signed) newprio < 1)
		newprio = 1;
	if (newprio > DEF_PRIORITY*2)
		newprio = DEF_PRIORITY*2;
	current->priority = newprio;
	return 0;
}

#endif

static inline struct task_struct *find_process_by_pid(pid_t pid)
{
	struct task_struct *tsk = current;

	if (pid)
		tsk = find_task_by_pid(pid);
	return tsk;
}

static int setscheduler(pid_t pid, int policy, 
			struct sched_param *param)
{
	struct sched_param lp;
	struct task_struct *p;
	int retval;

	retval = -EINVAL;
	if (!param || pid < 0)
		goto out_nounlock;

	retval = -EFAULT;
	if (copy_from_user(&lp, param, sizeof(struct sched_param)))
		goto out_nounlock;

	/*
	 * We play safe to avoid deadlocks.
	 */
	spin_lock_irq(&runqueue_lock);
	read_lock(&tasklist_lock);

	p = find_process_by_pid(pid);

	retval = -ESRCH;
	if (!p)
		goto out_unlock;
			
	if (policy < 0)
		policy = p->policy;
	else {
		retval = -EINVAL;
		if (policy != SCHED_FIFO && policy != SCHED_RR &&
				policy != SCHED_OTHER)
			goto out_unlock;
	}
	
	/*
	 * Valid priorities for SCHED_FIFO and SCHED_RR are 1..99, valid
	 * priority for SCHED_OTHER is 0.
	 */
	retval = -EINVAL;
	if (lp.sched_priority < 0 || lp.sched_priority > 99)
		goto out_unlock;
	if ((policy == SCHED_OTHER) != (lp.sched_priority == 0))
		goto out_unlock;

	retval = -EPERM;
	if ((policy == SCHED_FIFO || policy == SCHED_RR) && 
	    !capable(CAP_SYS_NICE))
		goto out_unlock;
	if ((current->euid != p->euid) && (current->euid != p->uid) &&
	    !capable(CAP_SYS_NICE))
		goto out_unlock;

	retval = 0;
	p->policy = policy;
	p->rt_priority = lp.sched_priority;
	if (p->next_run)
		move_first_runqueue(p);

	current->need_resched = 1;

out_unlock:
	read_unlock(&tasklist_lock);
	spin_unlock_irq(&runqueue_lock);

out_nounlock:
	return retval;
}

asmlinkage int sys_sched_setscheduler(pid_t pid, int policy, 
				      struct sched_param *param)
{
	return setscheduler(pid, policy, param);
}

asmlinkage int sys_sched_setparam(pid_t pid, struct sched_param *param)
{
	return setscheduler(pid, -1, param);
}

asmlinkage int sys_sched_getscheduler(pid_t pid)
{
	struct task_struct *p;
	int retval;

	retval = -EINVAL;
	if (pid < 0)
		goto out_nounlock;

	read_lock(&tasklist_lock);

	retval = -ESRCH;
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;
			
	retval = p->policy;

out_unlock:
	read_unlock(&tasklist_lock);

out_nounlock:
	return retval;
}

asmlinkage int sys_sched_getparam(pid_t pid, struct sched_param *param)
{
	struct task_struct *p;
	struct sched_param lp;
	int retval;

	retval = -EINVAL;
	if (!param || pid < 0)
		goto out_nounlock;

	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	retval = -ESRCH;
	if (!p)
		goto out_unlock;
	lp.sched_priority = p->rt_priority;
	read_unlock(&tasklist_lock);

	/*
	 * This one might sleep, we cannot do it with a spinlock held ...
	 */
	retval = copy_to_user(param, &lp, sizeof(*param)) ? -EFAULT : 0;

out_nounlock:
	return retval;

out_unlock:
	read_unlock(&tasklist_lock);
	return retval;
}

asmlinkage int sys_sched_yield(void)
{
	spin_lock_irq(&runqueue_lock);
	if (current->policy == SCHED_OTHER)
		current->policy |= SCHED_YIELD;
	current->need_resched = 1;
	move_last_runqueue(current);
	spin_unlock_irq(&runqueue_lock);
	return 0;
}

asmlinkage int sys_sched_get_priority_max(int policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 99;
		break;
	case SCHED_OTHER:
		ret = 0;
		break;
	}
	return ret;
}

asmlinkage int sys_sched_get_priority_min(int policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 1;
		break;
	case SCHED_OTHER:
		ret = 0;
	}
	return ret;
}

asmlinkage int sys_sched_rr_get_interval(pid_t pid, struct timespec *interval)
{
	struct timespec t;

	t.tv_sec = 0;
	t.tv_nsec = 150000;
	if (copy_to_user(interval, &t, sizeof(struct timespec)))
		return -EFAULT;
	return 0;
}

asmlinkage int sys_nanosleep(struct timespec *rqtp, struct timespec *rmtp)
{
	struct timespec t;
	unsigned long expire;

	if(copy_from_user(&t, rqtp, sizeof(struct timespec)))
		return -EFAULT;

	if (t.tv_nsec >= 1000000000L || t.tv_nsec < 0 || t.tv_sec < 0)
		return -EINVAL;


	if (t.tv_sec == 0 && t.tv_nsec <= 2000000L &&
	    current->policy != SCHED_OTHER)
	{
		/*
		 * Short delay requests up to 2 ms will be handled with
		 * high precision by a busy wait for all real-time processes.
		 *
		 * Its important on SMP not to do this holding locks.
		 */
		udelay((t.tv_nsec + 999) / 1000);
		return 0;
	}

	expire = timespec_to_jiffies(&t) + (t.tv_sec || t.tv_nsec);

	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire);

	if (expire) {
		if (rmtp) {
			jiffies_to_timespec(expire, &t);
			if (copy_to_user(rmtp, &t, sizeof(struct timespec)))
				return -EFAULT;
		}
		return -EINTR;
	}
	return 0;
}

static void show_task(int nr,struct task_struct * p)
{
	unsigned long free = 0;
	int state;
	static const char * stat_nam[] = { "R", "S", "D", "Z", "T", "W" };

	printk("%-8s %3d ", p->comm, (p == current) ? -nr : nr);
	state = p->state ? ffz(~p->state) + 1 : 0;
	if (((unsigned) state) < sizeof(stat_nam)/sizeof(char *))
		printk(stat_nam[state]);
	else
		printk(" ");
#if (BITS_PER_LONG == 32)
	if (p == current)
		printk(" current  ");
	else
		printk(" %08lX ", thread_saved_pc(&p->tss));
#else
	if (p == current)
		printk("   current task   ");
	else
		printk(" %016lx ", thread_saved_pc(&p->tss));
#endif
	{
		unsigned long * n = (unsigned long *) (p+1);
		while (!*n)
			n++;
		free = (unsigned long) n - (unsigned long)(p+1);
	}
	printk("%5lu %5d %6d ", free, p->pid, p->p_pptr->pid);
	if (p->p_cptr)
		printk("%5d ", p->p_cptr->pid);
	else
		printk("      ");
	if (p->p_ysptr)
		printk("%7d", p->p_ysptr->pid);
	else
		printk("       ");
	if (p->p_osptr)
		printk(" %5d\n", p->p_osptr->pid);
	else
		printk("\n");

	{
		struct signal_queue *q;
		char s[sizeof(sigset_t)*2+1], b[sizeof(sigset_t)*2+1]; 

		render_sigset_t(&p->signal, s);
		render_sigset_t(&p->blocked, b);
		printk("   sig: %d %s %s :", signal_pending(p), s, b);
		for (q = p->sigqueue; q ; q = q->next)
			printk(" %d", q->info.si_signo);
		printk(" X\n");
	}
}

char * render_sigset_t(sigset_t *set, char *buffer)
{
	int i = _NSIG, x;
	do {
		i -= 4, x = 0;
		if (sigismember(set, i+1)) x |= 1;
		if (sigismember(set, i+2)) x |= 2;
		if (sigismember(set, i+3)) x |= 4;
		if (sigismember(set, i+4)) x |= 8;
		*buffer++ = (x < 10 ? '0' : 'a' - 10) + x;
	} while (i >= 4);
	*buffer = 0;
	return buffer;
}

void show_state(void)
{
	struct task_struct *p;

#if (BITS_PER_LONG == 32)
	printk("\n"
	       "                         free                        sibling\n");
	printk("  task             PC    stack   pid father child younger older\n");
#else
	printk("\n"
	       "                                 free                        sibling\n");
	printk("  task                 PC        stack   pid father child younger older\n");
#endif
	read_lock(&tasklist_lock);
	for_each_task(p)
		show_task((p->tarray_ptr - &task[0]),p);
	read_unlock(&tasklist_lock);
}

void __init init_idle(void)
{
	cycles_t t;
	struct schedule_data * sched_data;
	sched_data = &aligned_data[smp_processor_id()].schedule_data;

	t = get_cycles();
	sched_data->curr = current;
	sched_data->last_schedule = t;
}

void __init sched_init(void)
{
	/*
	 * We have to do a little magic to get the first
	 * process right in SMP mode.
	 */
	int cpu=hard_smp_processor_id();
	int nr = NR_TASKS;

	init_task.processor=cpu;

	/* Init task array free list and pidhash table. */
	while(--nr > 0)
		add_free_taskslot(&task[nr]);

	for(nr = 0; nr < PIDHASH_SZ; nr++)
		pidhash[nr] = NULL;

	init_bh(TIMER_BH, timer_bh);
	init_bh(TQUEUE_BH, tqueue_bh);
	init_bh(IMMEDIATE_BH, immediate_bh);
}

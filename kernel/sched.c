/*
 *  linux/kernel/sched.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fdreg.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/ptrace.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/tqueue.h>
#include <linux/resource.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/pgtable.h>

#define TIMER_IRQ 0

#include <linux/timex.h>

/*
 * kernel variables
 */
long tick = 1000000 / HZ;               /* timer interrupt period */
volatile struct timeval xtime;		/* The current time */
int tickadj = 500/HZ;			/* microsecs */

DECLARE_TASK_QUEUE(tq_timer);
DECLARE_TASK_QUEUE(tq_immediate);
DECLARE_TASK_QUEUE(tq_scheduler);

/*
 * phase-lock loop variables
 */
int time_status = TIME_BAD;     /* clock synchronization status */
long time_offset = 0;           /* time adjustment (us) */
long time_constant = 0;         /* pll time constant */
long time_tolerance = MAXFREQ;  /* frequency tolerance (ppm) */
long time_precision = 1; 	/* clock precision (us) */
long time_maxerror = 0x70000000;/* maximum error */
long time_esterror = 0x70000000;/* estimated error */
long time_phase = 0;            /* phase offset (scaled us) */
long time_freq = 0;             /* frequency offset (scaled ppm) */
long time_adj = 0;              /* tick adjust (scaled 1 / HZ) */
long time_reftime = 0;          /* time at last adjustment (s) */

long time_adjust = 0;
long time_adjust_step = 0;

int need_resched = 0;
unsigned long event = 0;

extern int _setitimer(int, struct itimerval *, struct itimerval *);
unsigned long * prof_buffer = NULL;
unsigned long prof_len = 0;

#define _S(nr) (1<<((nr)-1))

extern void mem_use(void);

extern int timer_interrupt(void);
 
static unsigned long init_kernel_stack[1024] = { STACK_MAGIC, };
unsigned long init_user_stack[1024] = { STACK_MAGIC, };
static struct vm_area_struct init_mmap = INIT_MMAP;
struct task_struct init_task = INIT_TASK;

unsigned long volatile jiffies=0;

struct task_struct *current = &init_task;
struct task_struct *last_task_used_math = NULL;

struct task_struct * task[NR_TASKS] = {&init_task, };

struct kernel_stat kstat = { 0 };

static inline void add_to_runqueue(struct task_struct * p)
{
#if 1	/* sanity tests */
	if (p->next_run || p->prev_run) {
		printk("task already on run-queue\n");
		return;
	}
#endif
	if (p->counter > current->counter + 3)
		need_resched = 1;
	nr_running++;
	(p->next_run = init_task.next_run)->prev_run = p;
	p->prev_run = &init_task;
	init_task.next_run = p;
}

static inline void del_from_runqueue(struct task_struct * p)
{
	struct task_struct *next = p->next_run;
	struct task_struct *prev = p->prev_run;

#if 1	/* sanity tests */
	if (!next || !prev) {
		printk("task not on run-queue\n");
		return;
	}
#endif
	if (p == &init_task) {
		static int nr = 0;
		if (nr < 5) {
			nr++;
			printk("idle task may not sleep\n");
		}
		return;
	}
	nr_running--;
	next->prev_run = prev;
	prev->next_run = next;
	p->next_run = NULL;
	p->prev_run = NULL;
}

/*
 * Wake up a process. Put it on the run-queue if it's not
 * already there.  The "current" process is always on the
 * run-queue (except when the actual re-schedule is in
 * progress), and as such you're allowed to do the simpler
 * "current->state = TASK_RUNNING" to mark yourself runnable
 * without the overhead of this.
 */
inline void wake_up_process(struct task_struct * p)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	p->state = TASK_RUNNING;
	if (!p->next_run)
		add_to_runqueue(p);
	restore_flags(flags);
}

static void process_timeout(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;

	p->timeout = 0;
	wake_up_process(p);
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
	int c;
	struct task_struct * p;
	struct task_struct * next;
	unsigned long timeout = 0;

/* check alarm, wake up any interruptible tasks that have got a signal */

	if (intr_count) {
		printk("Aiee: scheduling in interrupt\n");
		return;
	}
	run_task_queue(&tq_scheduler);

	need_resched = 0;
	cli();
	switch (current->state) {
		case TASK_INTERRUPTIBLE:
			if (current->signal & ~current->blocked)
				goto makerunnable;
			timeout = current->timeout;
			if (timeout && (timeout <= jiffies)) {
				current->timeout = 0;
				timeout = 0;
		makerunnable:
				current->state = TASK_RUNNING;
				break;
			}
		default:
			del_from_runqueue(current);
		case TASK_RUNNING:
	}
	p = init_task.next_run;
	sti();

/*
 * Note! there may appear new tasks on the run-queue during this, as
 * interrupts are enabled. However, they will be put on front of the
 * list, so our list starting at "p" is essentially fixed.
 */
/* this is the scheduler proper: */
	c = -1000;
	next = &init_task;
	while (p != &init_task) {
		if (p->counter > c)
			c = p->counter, next = p;
		p = p->next_run;
	}

	/* if all runnable processes have "counter == 0", re-calculate counters */
	if (!c) {
		for_each_task(p)
			p->counter = (p->counter >> 1) + p->priority;
	}
	if (current != next) {
		struct timer_list timer;

		kstat.context_swtch++;
		if (timeout) {
			init_timer(&timer);
			timer.expires = timeout;
			timer.data = (unsigned long) current;
			timer.function = process_timeout;
			add_timer(&timer);
		}
		switch_to(next);
		if (timeout)
			del_timer(&timer);
	}
}

asmlinkage int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

/*
 * wake_up doesn't wake up stopped processes - they have to be awakened
 * with signals or similar.
 *
 * Note that this doesn't need cli-sti pairs: interrupts may not change
 * the wait-queue structures directly, but only call wake_up() to wake
 * a process. The process itself must remove the queue once it has woken.
 */
void wake_up(struct wait_queue **q)
{
	struct wait_queue *tmp;
	struct task_struct * p;

	if (!q || !(tmp = *q))
		return;
	do {
		if ((p = tmp->task) != NULL) {
			if ((p->state == TASK_UNINTERRUPTIBLE) ||
			    (p->state == TASK_INTERRUPTIBLE))
				wake_up_process(p);
		}
		if (!tmp->next) {
			printk("wait_queue is bad (eip = %p)\n",
				__builtin_return_address(0));
			printk("        q = %p\n",q);
			printk("       *q = %p\n",*q);
			printk("      tmp = %p\n",tmp);
			break;
		}
		tmp = tmp->next;
	} while (tmp != *q);
}

void wake_up_interruptible(struct wait_queue **q)
{
	struct wait_queue *tmp;
	struct task_struct * p;

	if (!q || !(tmp = *q))
		return;
	do {
		if ((p = tmp->task) != NULL) {
			if (p->state == TASK_INTERRUPTIBLE)
				wake_up_process(p);
		}
		if (!tmp->next) {
			printk("wait_queue is bad (eip = %p)\n",
				__builtin_return_address(0));
			printk("        q = %p\n",q);
			printk("       *q = %p\n",*q);
			printk("      tmp = %p\n",tmp);
			break;
		}
		tmp = tmp->next;
	} while (tmp != *q);
}

void __down(struct semaphore * sem)
{
	struct wait_queue wait = { current, NULL };
	add_wait_queue(&sem->wait, &wait);
	current->state = TASK_UNINTERRUPTIBLE;
	while (sem->count <= 0) {
		schedule();
		current->state = TASK_UNINTERRUPTIBLE;
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&sem->wait, &wait);
}

static inline void __sleep_on(struct wait_queue **p, int state)
{
	unsigned long flags;
	struct wait_queue wait = { current, NULL };

	if (!p)
		return;
	if (current == task[0])
		panic("task[0] trying to sleep");
	current->state = state;
	add_wait_queue(p, &wait);
	save_flags(flags);
	sti();
	schedule();
	remove_wait_queue(p, &wait);
	restore_flags(flags);
}

void interruptible_sleep_on(struct wait_queue **p)
{
	__sleep_on(p,TASK_INTERRUPTIBLE);
}

void sleep_on(struct wait_queue **p)
{
	__sleep_on(p,TASK_UNINTERRUPTIBLE);
}

/*
 * The head for the timer-list has a "expires" field of MAX_UINT,
 * and the sorting routine counts on this..
 */
static struct timer_list timer_head = { &timer_head, &timer_head, ~0, 0, NULL };
#define SLOW_BUT_DEBUGGING_TIMERS 1

void add_timer(struct timer_list * timer)
{
	unsigned long flags;
	struct timer_list *p;

#if SLOW_BUT_DEBUGGING_TIMERS
	if (timer->next || timer->prev) {
		printk("add_timer() called with non-zero list from %p\n",
			__builtin_return_address(0));
		return;
	}
#endif
	p = &timer_head;
	save_flags(flags);
	cli();
	do {
		p = p->next;
	} while (timer->expires > p->expires);
	timer->next = p;
	timer->prev = p->prev;
	p->prev = timer;
	timer->prev->next = timer;
	restore_flags(flags);
}

int del_timer(struct timer_list * timer)
{
	unsigned long flags;
#if SLOW_BUT_DEBUGGING_TIMERS
	struct timer_list * p;

	p = &timer_head;
	save_flags(flags);
	cli();
	while ((p = p->next) != &timer_head) {
		if (p == timer) {
			timer->next->prev = timer->prev;
			timer->prev->next = timer->next;
			timer->next = timer->prev = NULL;
			restore_flags(flags);
			return 1;
		}
	}
	if (timer->next || timer->prev)
		printk("del_timer() called from %p with timer not initialized\n",
			__builtin_return_address(0));
	restore_flags(flags);
	return 0;
#else	
	save_flags(flags);
	cli();
	if (timer->next) {
		timer->next->prev = timer->prev;
		timer->prev->next = timer->next;
		timer->next = timer->prev = NULL;
		restore_flags(flags);
		return 1;
	}
	restore_flags(flags);
	return 0;
#endif
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
	struct task_struct **p;
	unsigned long nr = 0;

	for(p = &LAST_TASK; p > &FIRST_TASK; --p)
		if (*p && ((*p)->state == TASK_RUNNING ||
			   (*p)->state == TASK_UNINTERRUPTIBLE ||
			   (*p)->state == TASK_SWAPPING))
			nr += FIXED_1;
	return nr;
}

static inline void calc_load(void)
{
	unsigned long active_tasks; /* fixed-point */
	static int count = LOAD_FREQ;

	if (count-- > 0)
		return;
	count = LOAD_FREQ;
	active_tasks = count_active_tasks();
	CALC_LOAD(avenrun[0], EXP_1, active_tasks);
	CALC_LOAD(avenrun[1], EXP_5, active_tasks);
	CALC_LOAD(avenrun[2], EXP_15, active_tasks);
}

/*
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 *
 * These were ported to Linux by Philip Gladstone.
 */
static void second_overflow(void)
{
	long ltemp;

	/* Bump the maxerror field */
	time_maxerror = (0x70000000-time_maxerror < time_tolerance) ?
	  0x70000000 : (time_maxerror + time_tolerance);

	/* Run the PLL */
	if (time_offset < 0) {
		ltemp = (-(time_offset+1) >> (SHIFT_KG + time_constant)) + 1;
		time_adj = ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
		time_offset += (time_adj * HZ) >> (SHIFT_SCALE - SHIFT_UPDATE);
		time_adj = - time_adj;
	} else if (time_offset > 0) {
		ltemp = ((time_offset-1) >> (SHIFT_KG + time_constant)) + 1;
		time_adj = ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
		time_offset -= (time_adj * HZ) >> (SHIFT_SCALE - SHIFT_UPDATE);
	} else {
		time_adj = 0;
	}

	time_adj += (time_freq >> (SHIFT_KF + SHIFT_HZ - SHIFT_SCALE))
	    + FINETUNE;

	/* Handle the leap second stuff */
	switch (time_status) {
		case TIME_INS:
		/* ugly divide should be replaced */
		if (xtime.tv_sec % 86400 == 0) {
			xtime.tv_sec--; /* !! */
			time_status = TIME_OOP;
			printk("Clock: inserting leap second 23:59:60 UTC\n");
		}
		break;

		case TIME_DEL:
		/* ugly divide should be replaced */
		if (xtime.tv_sec % 86400 == 86399) {
			xtime.tv_sec++;
			time_status = TIME_OK;
			printk("Clock: deleting leap second 23:59:59 UTC\n");
		}
		break;

		case TIME_OOP:
		time_status = TIME_OK;
		break;
	}
}

/*
 * disregard lost ticks for now.. We don't care enough.
 */
static void timer_bh(void * unused)
{
	unsigned long mask;
	struct timer_struct *tp;
	struct timer_list * timer;

	cli();
	while ((timer = timer_head.next) != &timer_head && timer->expires < jiffies) {
		void (*fn)(unsigned long) = timer->function;
		unsigned long data = timer->data;
		timer->next->prev = timer->prev;
		timer->prev->next = timer->next;
		timer->next = timer->prev = NULL;
		sti();
		fn(data);
		cli();
	}
	sti();
	
	for (mask = 1, tp = timer_table+0 ; mask ; tp++,mask += mask) {
		if (mask > timer_active)
			break;
		if (!(mask & timer_active))
			continue;
		if (tp->expires > jiffies)
			continue;
		timer_active &= ~mask;
		tp->fn();
		sti();
	}
}

void tqueue_bh(void * unused)
{
	run_task_queue(&tq_timer);
}

void immediate_bh(void * unused)
{
	run_task_queue(&tq_immediate);
}

/*
 * The int argument is really a (struct pt_regs *), in case the
 * interrupt wants to know from where it was called. The timer
 * irq uses this to decide if it should update the user or system
 * times.
 */
static void do_timer(int irq, struct pt_regs * regs)
{
	unsigned long mask;
	struct timer_struct *tp;
	/* last time the cmos clock got updated */
	static long last_rtc_update=0;
	extern int set_rtc_mmss(unsigned long);

	long ltemp, psecs;

	/* Advance the phase, once it gets to one microsecond, then
	 * advance the tick more.
	 */
	time_phase += time_adj;
	if (time_phase < -FINEUSEC) {
		ltemp = -time_phase >> SHIFT_SCALE;
		time_phase += ltemp << SHIFT_SCALE;
		xtime.tv_usec += tick + time_adjust_step - ltemp;
	}
	else if (time_phase > FINEUSEC) {
		ltemp = time_phase >> SHIFT_SCALE;
		time_phase -= ltemp << SHIFT_SCALE;
		xtime.tv_usec += tick + time_adjust_step + ltemp;
	} else
		xtime.tv_usec += tick + time_adjust_step;

	if (time_adjust)
	{
	    /* We are doing an adjtime thing. 
	     *
	     * Modify the value of the tick for next time.
	     * Note that a positive delta means we want the clock
	     * to run fast. This means that the tick should be bigger
	     *
	     * Limit the amount of the step for *next* tick to be
	     * in the range -tickadj .. +tickadj
	     */
	     if (time_adjust > tickadj)
	       time_adjust_step = tickadj;
	     else if (time_adjust < -tickadj)
	       time_adjust_step = -tickadj;
	     else
	       time_adjust_step = time_adjust;
	     
	    /* Reduce by this step the amount of time left  */
	    time_adjust -= time_adjust_step;
	}
	else
	    time_adjust_step = 0;

	if (xtime.tv_usec >= 1000000) {
	    xtime.tv_usec -= 1000000;
	    xtime.tv_sec++;
	    second_overflow();
	}

	/* If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (time_status != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 500000 - (tick >> 1) &&
	    xtime.tv_usec < 500000 + (tick >> 1))
	  if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */

	jiffies++;
	calc_load();
	if (user_mode(regs)) {
		current->utime++;
		if (current != task[0]) {
			if (current->priority < 15)
				kstat.cpu_nice++;
			else
				kstat.cpu_user++;
		}
		/* Update ITIMER_VIRT for current task if not in a system call */
		if (current->it_virt_value && !(--current->it_virt_value)) {
			current->it_virt_value = current->it_virt_incr;
			send_sig(SIGVTALRM,current,1);
		}
	} else {
		current->stime++;
		if(current != task[0])
			kstat.cpu_system++;
#ifdef CONFIG_PROFILE
		if (prof_buffer && current != task[0]) {
			extern int _stext;
			unsigned long eip = regs->eip - (unsigned long) &_stext;
			eip >>= CONFIG_PROFILE_SHIFT;
			if (eip < prof_len)
				prof_buffer[eip]++;
		}
#endif
	}
	/*
	 * check the cpu time limit on the process.
	 */
	if ((current->rlim[RLIMIT_CPU].rlim_max != RLIM_INFINITY) &&
	    (((current->stime + current->utime) / HZ) >= current->rlim[RLIMIT_CPU].rlim_max))
		send_sig(SIGKILL, current, 1);
	if ((current->rlim[RLIMIT_CPU].rlim_cur != RLIM_INFINITY) &&
	    (((current->stime + current->utime) % HZ) == 0)) {
		psecs = (current->stime + current->utime) / HZ;
		/* send when equal */
		if (psecs == current->rlim[RLIMIT_CPU].rlim_cur)
			send_sig(SIGXCPU, current, 1);
		/* and every five seconds thereafter. */
		else if ((psecs > current->rlim[RLIMIT_CPU].rlim_cur) &&
		        ((psecs - current->rlim[RLIMIT_CPU].rlim_cur) % 5) == 0)
			send_sig(SIGXCPU, current, 1);
	}

	if (current != task[0] && 0 > --current->counter) {
		current->counter = 0;
		need_resched = 1;
	}
	/* Update ITIMER_PROF for the current task */
	if (current->it_prof_value && !(--current->it_prof_value)) {
		current->it_prof_value = current->it_prof_incr;
		send_sig(SIGPROF,current,1);
	}
	for (mask = 1, tp = timer_table+0 ; mask ; tp++,mask += mask) {
		if (mask > timer_active)
			break;
		if (!(mask & timer_active))
			continue;
		if (tp->expires > jiffies)
			continue;
		mark_bh(TIMER_BH);
	}
	cli();
	if (timer_head.next->expires < jiffies)
		mark_bh(TIMER_BH);
	if (tq_timer != &tq_last)
		mark_bh(TQUEUE_BH);
	sti();
}

asmlinkage int sys_alarm(long seconds)
{
	struct itimerval it_new, it_old;

	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	_setitimer(ITIMER_REAL, &it_new, &it_old);
	return(it_old.it_value.tv_sec + (it_old.it_value.tv_usec / 1000000));
}

asmlinkage int sys_getpid(void)
{
	return current->pid;
}

asmlinkage int sys_getppid(void)
{
	return current->p_opptr->pid;
}

asmlinkage int sys_getuid(void)
{
	return current->uid;
}

asmlinkage int sys_geteuid(void)
{
	return current->euid;
}

asmlinkage int sys_getgid(void)
{
	return current->gid;
}

asmlinkage int sys_getegid(void)
{
	return current->egid;
}

asmlinkage int sys_nice(long increment)
{
	int newprio;

	if (increment < 0 && !suser())
		return -EPERM;
	newprio = current->priority - increment;
	if (newprio < 1)
		newprio = 1;
	if (newprio > 35)
		newprio = 35;
	current->priority = newprio;
	return 0;
}

static void show_task(int nr,struct task_struct * p)
{
	unsigned long free;
	static char * stat_nam[] = { "R", "S", "D", "Z", "T", "W" };

	printk("%-8s %3d ", p->comm, (p == current) ? -nr : nr);
	if (((unsigned) p->state) < sizeof(stat_nam)/sizeof(char *))
		printk(stat_nam[p->state]);
	else
		printk(" ");
#if ((~0UL) == 0xffffffff)
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
	for (free = 1; free < PAGE_SIZE/sizeof(long) ; free++) {
		if (((unsigned long *)p->kernel_stack_page)[free])
			break;
	}
	printk("%5lu %5d %6d ", free*sizeof(long), p->pid, p->p_pptr->pid);
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
}

void show_state(void)
{
	int i;

#if ((~0UL) == 0xffffffff)
	printk("\n"
	       "                         free                        sibling\n");
	printk("  task             PC    stack   pid father child younger older\n");
#else
	printk("\n"
	       "                                 free                        sibling\n");
	printk("  task                 PC        stack   pid father child younger older\n");
#endif
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i])
			show_task(i,task[i]);
}

void sched_init(void)
{
	bh_base[TIMER_BH].routine = timer_bh;
	bh_base[TQUEUE_BH].routine = tqueue_bh;
	bh_base[IMMEDIATE_BH].routine = immediate_bh;
	if (request_irq(TIMER_IRQ, do_timer, 0, "timer") != 0)
		panic("Could not allocate timer IRQ!");
	enable_bh(TIMER_BH);
	enable_bh(TQUEUE_BH);
	enable_bh(IMMEDIATE_BH);
}

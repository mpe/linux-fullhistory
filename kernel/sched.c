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
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/ptrace.h>
#include <linux/segment.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define TIMER_IRQ 0

int need_resched = 0;
int hard_math = 0;		/* set by boot/head.S */
int ignore_irq13 = 0;		/* set if exception 16 works */

extern int _setitimer(int, struct itimerval *, struct itimerval *);
unsigned long * prof_buffer = NULL;
unsigned long prof_len = 0;

#define _S(nr) (1<<((nr)-1))

#define LATCH ((1193180 + HZ/2)/HZ)

extern void mem_use(void);

extern int timer_interrupt(void);
extern "C" int system_call(void);

static unsigned long init_kernel_stack[1024];
struct task_struct init_task = INIT_TASK;

unsigned long volatile jiffies=0;
unsigned long startup_time=0;
int jiffies_offset = 0;		/* # clock ticks to add to get "true
				   time".  Should always be less than
				   1 second's worth.  For time fanatics
				   who like to syncronize their machines
				   to WWV :-) */

struct task_struct *current = &init_task;
struct task_struct *last_task_used_math = NULL;

struct task_struct * task[NR_TASKS] = {&init_task, };

long user_stack [ PAGE_SIZE>>2 ] ;

struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , KERNEL_DS };
/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 */
extern "C" void math_state_restore(void)
{
	__asm__ __volatile__("clts");
	if (last_task_used_math == current)
		return;
	timer_table[COPRO_TIMER].expires = jiffies+50;
	timer_active |= 1<<COPRO_TIMER;	
	if (last_task_used_math)
		__asm__("fnsave %0":"=m" (last_task_used_math->tss.i387));
	else
		__asm__("fnclex");
	last_task_used_math = current;
	if (current->used_math) {
		__asm__("frstor %0": :"m" (current->tss.i387));
	} else {
		__asm__("fninit");
		current->used_math=1;
	}
	timer_active &= ~(1<<COPRO_TIMER);
}

/*
 *  'schedule()' is the scheduler function. It's a very simple and nice
 * scheduler: it's not perfect, but certainly works for most things.
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 *
 * The "confuse_gcc" goto is used only to get better assembly code..
 * Djikstra probably hates me.
 */
extern "C" void schedule(void)
{
	int c;
	struct task_struct * p;
	struct task_struct * next;

/* check alarm, wake up any interruptible tasks that have got a signal */

	sti();
	need_resched = 0;
	p = &init_task;
	for (;;) {
		if ((p = p->next_task) == &init_task)
			goto confuse_gcc1;
		if (p->state != TASK_INTERRUPTIBLE)
			continue;
		if (p->signal & ~p->blocked) {
			p->state = TASK_RUNNING;
			continue;
		}
		if (p->timeout && p->timeout < jiffies) {
			p->timeout = 0;
			p->state = TASK_RUNNING;
		}
	}
confuse_gcc1:

/* this is the scheduler proper: */

	c = -1;
	next = p = &init_task;
	for (;;) {
		if ((p = p->next_task) == &init_task)
			goto confuse_gcc2;
		if (p->state == TASK_RUNNING && p->counter > c)
			c = p->counter, next = p;
	}
confuse_gcc2:
	if (!c) {
		p = &init_task;
		while ((p = p->next_task) != &init_task)
			p->counter = (p->counter >> 1) + p->priority;
	}
	switch_to(next);
}

extern "C" int sys_pause(void)
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
			    (p->state == TASK_INTERRUPTIBLE)) {
				p->state = TASK_RUNNING;
				if (p->counter > current->counter)
					need_resched = 1;
			}
		}
		if (!tmp->next) {
			printk("wait_queue is bad (eip = %08x)\n",((unsigned long *) q)[-1]);
			printk("        q = %08x\n",q);
			printk("       *q = %08x\n",*q);
			printk("      tmp = %08x\n",tmp);
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
			if (p->state == TASK_INTERRUPTIBLE) {
				p->state = TASK_RUNNING;
				if (p->counter > current->counter)
					need_resched = 1;
			}
		}
		if (!tmp->next) {
			printk("wait_queue is bad (eip = %08x)\n",((unsigned long *) q)[-1]);
			printk("        q = %08x\n",q);
			printk("       *q = %08x\n",*q);
			printk("      tmp = %08x\n",tmp);
			break;
		}
		tmp = tmp->next;
	} while (tmp != *q);
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

static struct timer_list * next_timer = NULL;

void add_timer(struct timer_list * timer)
{
	unsigned long flags;
	struct timer_list ** p;

	if (!timer)
		return;
	timer->next = NULL;
	p = &next_timer;
	save_flags(flags);
	cli();
	while (*p) {
		if ((*p)->expires > timer->expires) {
			(*p)->expires -= timer->expires;
			timer->next = *p;
			break;
		}
		timer->expires -= (*p)->expires;
		p = &(*p)->next;
	}
	*p = timer;
	restore_flags(flags);
}

void del_timer(struct timer_list * timer)
{
	unsigned long flags;
	struct timer_list **p;

	p = &next_timer;
	save_flags(flags);
	cli();
	while (*p) {
		if (*p == timer) {
			if ((*p = timer->next) != NULL)
				(*p)->expires += timer->expires;
			break;
		}
		p = &(*p)->next;
	}
	restore_flags(flags);
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
		if (*p && (*p)->state == TASK_RUNNING)
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
 * The int argument is really a (struct pt_regs *), in case the
 * interrupt wants to know from where it was called. The timer
 * irq uses this to decide if it should update the user or system
 * times.
 */
static void do_timer(struct pt_regs * regs)
{
	unsigned long mask;
	struct timer_struct *tp = timer_table+0;
	struct task_struct ** task_p;

	jiffies++;
	calc_load();
	if ((VM_MASK & regs->eflags) || (3 & regs->cs)) {
		current->utime++;
		/* Update ITIMER_VIRT for current task if not in a system call */
		if (current->it_virt_value && !(--current->it_virt_value)) {
			current->it_virt_value = current->it_virt_incr;
			send_sig(SIGVTALRM,current,1);
		}
	} else {
		current->stime++;
#ifdef CONFIG_PROFILE
		if (prof_buffer && current != task[0]) {
			unsigned long eip = regs->eip;
			eip >>= 2;
			if (eip < prof_len)
				prof_buffer[eip]++;
		}
#endif
	}
	if (current == task[0] || (--current->counter)<=0) {
		current->counter=0;
		need_resched = 1;
	}
	/* Update ITIMER_REAL for every task */
	for (task_p = &LAST_TASK; task_p >= &FIRST_TASK; task_p--)
		if (*task_p && (*task_p)->it_real_value
			&& !(--(*task_p)->it_real_value)) {
			send_sig(SIGALRM,*task_p,1);
			(*task_p)->it_real_value = (*task_p)->it_real_incr;
			need_resched = 1;
		}
	/* Update ITIMER_PROF for the current task */
	if (current->it_prof_value && !(--current->it_prof_value)) {
		current->it_prof_value = current->it_prof_incr;
		send_sig(SIGPROF,current,1);
	}
	for (mask = 1 ; mask ; tp++,mask += mask) {
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
	cli();
	while (next_timer && next_timer->expires == 0) {
		void (*fn)(unsigned long) = next_timer->function;
		unsigned long data = next_timer->data;
		next_timer = next_timer->next;
		sti();
		fn(data);
		cli();
	}
	if (next_timer)
		next_timer->expires--;
	sti();
}

extern "C" int sys_alarm(long seconds)
{
	struct itimerval it_new, it_old;

	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	_setitimer(ITIMER_REAL, &it_new, &it_old);
	return(it_old.it_value.tv_sec + (it_old.it_value.tv_usec / 1000000));
}

extern "C" int sys_getpid(void)
{
	return current->pid;
}

extern "C" int sys_getppid(void)
{
	return current->p_pptr->pid;
}

extern "C" int sys_getuid(void)
{
	return current->uid;
}

extern "C" int sys_geteuid(void)
{
	return current->euid;
}

extern "C" int sys_getgid(void)
{
	return current->gid;
}

extern "C" int sys_getegid(void)
{
	return current->egid;
}

extern "C" int sys_nice(long increment)
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
	int i, j;
	unsigned char * stack;

	printk("%d: pid=%d, state=%d, father=%d, child=%d, ",(p == current)?-nr:nr,p->pid,
		p->state, p->p_pptr->pid, p->p_cptr ? p->p_cptr->pid : -1);
	i = 0;
	j = 4096;
	if (!(stack = (unsigned char *) p->kernel_stack_page)) {
		stack = (unsigned char *) init_kernel_stack;
		j = sizeof(init_kernel_stack);
	}
	while (i<j && !*(stack++))
		i++;
	printk("%d/%d chars free in kstack\n",i,j);
	printk("   PC=%08X.", *(1019 + (unsigned long *) p));
	if (p->p_ysptr || p->p_osptr) 
		printk("   Younger sib=%d, older sib=%d\n", 
			p->p_ysptr ? p->p_ysptr->pid : -1,
			p->p_osptr ? p->p_osptr->pid : -1);
	else
		printk("\n");
}

void show_state(void)
{
	int i;

	printk("Task-info:\n");
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i])
			show_task(i,task[i]);
}

void sched_init(void)
{
	int i;
	struct desc_struct * p;

	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");
	set_tss_desc(gdt+FIRST_TSS_ENTRY,&init_task.tss);
	set_ldt_desc(gdt+FIRST_LDT_ENTRY,&init_task.ldt);
	set_system_gate(0x80,&system_call);
	p = gdt+2+FIRST_TSS_ENTRY;
	for(i=1 ; i<NR_TASKS ; i++) {
		task[i] = NULL;
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}
/* Clear NT, so that we won't have troubles with that later on */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
	load_TR(0);
	load_ldt(0);
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	if (request_irq(TIMER_IRQ,(void (*)(int)) do_timer)!=0)
		panic("Could not allocate timer IRQ!");
}

#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#include <asm/param.h>	/* for HZ */

extern unsigned long event;

#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/tasks.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/times.h>

#include <asm/system.h>
#include <asm/semaphore.h>
#include <asm/page.h>

#include <linux/smp.h>
#include <linux/tty.h>
#include <linux/sem.h>
#include <linux/signal.h>

/*
 * cloning flags:
 */
#define CSIGNAL		0x000000ff	/* signal mask to be sent at exit */
#define CLONE_VM	0x00000100	/* set if VM shared between processes */
#define CLONE_FS	0x00000200	/* set if fs info shared between processes */
#define CLONE_FILES	0x00000400	/* set if open files shared between processes */
#define CLONE_SIGHAND	0x00000800	/* set if signal handlers shared */
#define CLONE_PID	0x00001000	/* set if pid shared */

/*
 * These are the constant used to fake the fixed-point load-average
 * counting. Some notes:
 *  - 11 bit fractions expand to 22 bits by the multiplies: this gives
 *    a load-average precision of 10 bits integer + 11 bits fractional
 *  - if you want to count load-averages more often, you need more
 *    precision, or rounding will get you. With 2-second counting freq,
 *    the EXP_n values would be 1981, 2034 and 2043 if still using only
 *    11 bit fractions.
 */
extern unsigned long avenrun[];		/* Load averages */

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_FREQ	(5*HZ)		/* 5 sec intervals */
#define EXP_1		1884		/* 1/exp(5sec/1min) as fixed-point */
#define EXP_5		2014		/* 1/exp(5sec/5min) */
#define EXP_15		2037		/* 1/exp(5sec/15min) */

#define CALC_LOAD(load,exp,n) \
	load *= exp; \
	load += n*(FIXED_1-exp); \
	load >>= FSHIFT;

#define CT_TO_SECS(x)	((x) / HZ)
#define CT_TO_USECS(x)	(((x) % HZ) * 1000000/HZ)

extern int nr_running, nr_tasks;
extern int last_pid;

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/time.h>
#include <linux/param.h>
#include <linux/resource.h>
#include <linux/ptrace.h>
#include <linux/timer.h>

#include <asm/processor.h>

#define TASK_RUNNING		0
#define TASK_INTERRUPTIBLE	1
#define TASK_UNINTERRUPTIBLE	2
#define TASK_ZOMBIE		3
#define TASK_STOPPED		4
#define TASK_SWAPPING		5

/*
 * Scheduling policies
 */
#define SCHED_OTHER		0
#define SCHED_FIFO		1
#define SCHED_RR		2

struct sched_param {
	int sched_priority;
};

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifdef __KERNEL__

#include <asm/spinlock.h>

/*
 * This serializes "schedule()" and also protects
 * the run-queue from deletions/modifications (but
 * _adding_ to the beginning of the run-queue has
 * a separate lock).
 */
extern rwlock_t tasklist_lock;
extern spinlock_t scheduler_lock;

extern void sched_init(void);
extern void show_state(void);
extern void trap_init(void);

asmlinkage void schedule(void);

/* Open file table structure */
struct files_struct {
	int count;
	fd_set close_on_exec;
	fd_set open_fds;
	struct file * fd[NR_OPEN];
};

#define INIT_FILES { \
	1, \
	{ { 0, } }, \
	{ { 0, } }, \
	{ NULL, } \
}

struct fs_struct {
	int count;
	int umask;
	struct dentry * root, * pwd;
};

#define INIT_FS { \
	1, \
	0022, \
	NULL, NULL \
}

struct mm_struct {
	struct vm_area_struct *mmap, *mmap_cache;
	pgd_t * pgd;
	int count;
	struct semaphore mmap_sem;
	unsigned long context;
	unsigned long start_code, end_code, start_data, end_data;
	unsigned long start_brk, brk, start_stack, start_mmap;
	unsigned long arg_start, arg_end, env_start, env_end;
	unsigned long rss, total_vm, locked_vm;
	unsigned long def_flags;
	unsigned long cpu_vm_mask;
};

#define INIT_MM {					\
		&init_mmap, NULL, swapper_pg_dir, 1,	\
		MUTEX,					\
		0,					\
		0, 0, 0, 0,				\
		0, 0, 0, 0,				\
		0, 0, 0, 0,				\
		0, 0, 0,				\
		0, 0 }

struct signal_struct {
	atomic_t		count;
	struct k_sigaction	action[_NSIG];
	spinlock_t		siglock;
};


#define INIT_SIGNALS { \
		ATOMIC_INIT(1), \
		{ {{0,}}, }, \
		SPIN_LOCK_UNLOCKED }

struct task_struct {
/* these are hardcoded - don't touch */
	volatile long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
	long counter;
	long priority;
	unsigned long flags;	/* per process flags, defined below */
	int sigpending;
	long debugreg[8];  /* Hardware debugging registers */
	struct exec_domain *exec_domain;
/* various fields */
	struct linux_binfmt *binfmt;
	struct task_struct *next_task, *prev_task;
	struct task_struct *next_run,  *prev_run;
	int exit_code, exit_signal;
	int pdeath_signal;  /*  The signal sent when the parent dies  */
	/* ??? */
	unsigned long personality;
	int dumpable:1;
	int did_exec:1;
	pid_t pid;
	pid_t pgrp;
	pid_t tty_old_pgrp;
	pid_t session;
	/* boolean value for session group leader */
	int leader;
	int ngroups;
	gid_t groups[NGROUPS];
	/* 
	 * pointers to (original) parent process, youngest child, younger sibling,
	 * older sibling, respectively.  (p->father can be replaced with 
	 * p->p_pptr->pid)
	 */
	struct task_struct *p_opptr, *p_pptr, *p_cptr, *p_ysptr, *p_osptr;

	/* PID hash table linkage. */
	struct task_struct *pidhash_next;
	struct task_struct **pidhash_pprev;

	/* Pointer to task[] array linkage. */
	struct task_struct **tarray_ptr;

	struct wait_queue *wait_chldexit;	/* for wait4() */
	uid_t uid,euid,suid,fsuid;
	gid_t gid,egid,sgid,fsgid;
	unsigned long timeout, policy, rt_priority;
	unsigned long it_real_value, it_prof_value, it_virt_value;
	unsigned long it_real_incr, it_prof_incr, it_virt_incr;
	struct timer_list real_timer;
	struct tms times;
	unsigned long start_time;
/* mm fault and swap info: this can arguably be seen as either mm-specific or thread-specific */
	unsigned long min_flt, maj_flt, nswap, cmin_flt, cmaj_flt, cnswap;
	int swappable:1;
	unsigned long swap_address;
	unsigned long old_maj_flt;	/* old value of maj_flt */
	unsigned long dec_flt;		/* page fault count of the last time */
	unsigned long swap_cnt;		/* number of pages to swap on next pass */
/* limits */
	struct rlimit rlim[RLIM_NLIMITS];
	unsigned short used_math;
	char comm[16];
/* file system info */
	int link_count;
	struct tty_struct *tty; /* NULL if no tty */
/* ipc stuff */
	struct sem_undo *semundo;
	struct sem_queue *semsleeping;
/* ldt for this task - used by Wine.  If NULL, default_ldt is used */
	struct desc_struct *ldt;
/* tss for this task */
	struct thread_struct tss;
/* filesystem information */
	struct fs_struct *fs;
/* open file information */
	struct files_struct *files;
/* memory management info */
	struct mm_struct *mm;
/* signal handlers */
	struct signal_struct *sig;
	sigset_t signal, blocked;
	struct signal_queue *sigqueue, **sigqueue_tail;
/* SMP state */
	int has_cpu;
	int processor;
	int last_processor;
	int lock_depth;		/* Lock depth. We can context switch in and out of holding a syscall kernel lock... */	
	/* Spinlocks for various pieces or per-task state. */
	spinlock_t sigmask_lock;	/* Protects signal and blocked */
};

/*
 * Per process flags
 */
#define PF_ALIGNWARN	0x00000001	/* Print alignment warning msgs */
					/* Not implemented yet, only for 486*/
#define PF_STARTING	0x00000002	/* being created */
#define PF_EXITING	0x00000004	/* getting shut down */
#define PF_PTRACED	0x00000010	/* set if ptrace (0) has been called */
#define PF_TRACESYS	0x00000020	/* tracing system calls */
#define PF_FORKNOEXEC	0x00000040	/* forked but didn't exec */
#define PF_SUPERPRIV	0x00000100	/* used super-user privileges */
#define PF_DUMPCORE	0x00000200	/* dumped core */
#define PF_SIGNALED	0x00000400	/* killed by a signal */

#define PF_USEDFPU	0x00100000	/* task used FPU this quantum (SMP) */
#define PF_DTRACE	0x00200000	/* delayed trace (used on m68k) */
#define PF_ONSIGSTK	0x00400000	/* works on signal stack (m68k only) */

/*
 * Limit the stack by to some sane default: root can always
 * increase this limit if needed..  8MB seems reasonable.
 */
#define _STK_LIM	(8*1024*1024)

#define DEF_PRIORITY	(20*HZ/100)	/* 200 ms time slices */

/* Note: This is very ugly I admit.  But some versions of gcc will
 *       dump core when an empty structure constant is parsed at
 *       the end of a large top level structure initialization. -DaveM
 */
#ifdef __SMP__
#define INIT_LOCKS	SPIN_LOCK_UNLOCKED
#else
#define INIT_LOCKS
#endif

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK \
/* state etc */	{ 0,DEF_PRIORITY,DEF_PRIORITY,0,0, \
/* debugregs */ { 0, },            \
/* exec domain */&default_exec_domain, \
/* binfmt */	NULL, \
/* schedlink */	&init_task,&init_task, &init_task, &init_task, \
/* ec,brk... */	0,0,0,0,0,0, \
/* pid etc.. */	0,0,0,0,0, \
/* suppl grps*/ 0, {0,}, \
/* proc links*/ &init_task,&init_task,NULL,NULL,NULL, \
/* pidhash */	NULL, NULL, \
/* tarray */	&task[0], \
/* chld wait */	NULL, \
/* uid etc */	0,0,0,0,0,0,0,0, \
/* timeout */	0,SCHED_OTHER,0,0,0,0,0,0,0, \
/* timer */	{ NULL, NULL, 0, 0, it_real_fn }, \
/* utime */	{0,0,0,0},0, \
/* flt */	0,0,0,0,0,0, \
/* swp */	0,0,0,0,0, \
/* rlimits */   INIT_RLIMITS, \
/* math */	0, \
/* comm */	"swapper", \
/* fs info */	0,NULL, \
/* ipc */	NULL, NULL, \
/* ldt */	NULL, \
/* tss */	INIT_TSS, \
/* fs */	&init_fs, \
/* files */	&init_files, \
/* mm */	&init_mm, \
/* signals */	&init_signals, {{0}}, {{0}}, NULL, &init_task.sigqueue, \
/* SMP */	0,0,0,0, \
/* locks */	INIT_LOCKS \
}

union task_union {
	struct task_struct task;
	unsigned long stack[2048];
};

extern union task_union init_task_union;

extern struct   mm_struct init_mm;
extern struct task_struct *task[NR_TASKS];
extern struct task_struct *last_task_used_math;

extern struct task_struct **tarray_freelist;
extern spinlock_t taskslot_lock;

extern __inline__ void add_free_taskslot(struct task_struct **t)
{
	spin_lock(&taskslot_lock);
	*t = (struct task_struct *) tarray_freelist;
	tarray_freelist = t;
	spin_unlock(&taskslot_lock);
}

extern __inline__ struct task_struct **get_free_taskslot(void)
{
	struct task_struct **tslot;

	spin_lock(&taskslot_lock);
	if((tslot = tarray_freelist) != NULL)
		tarray_freelist = (struct task_struct **) *tslot;
	spin_unlock(&taskslot_lock);

	return tslot;
}

/* PID hashing. */
#define PIDHASH_SZ (NR_TASKS >> 2)
extern struct task_struct *pidhash[PIDHASH_SZ];
extern spinlock_t pidhash_lock;

#define pid_hashfn(x)	((((x) >> 8) ^ (x)) & (PIDHASH_SZ - 1))

extern __inline__ void hash_pid(struct task_struct *p)
{
	struct task_struct **htable = &pidhash[pid_hashfn(p->pid)];
	unsigned long flags;

	spin_lock_irqsave(&pidhash_lock, flags);
	if((p->pidhash_next = *htable) != NULL)
		(*htable)->pidhash_pprev = &p->pidhash_next;
	*htable = p;
	p->pidhash_pprev = htable;
	spin_unlock_irqrestore(&pidhash_lock, flags);
}

extern __inline__ void unhash_pid(struct task_struct *p)
{
	unsigned long flags;

	spin_lock_irqsave(&pidhash_lock, flags);
	if(p->pidhash_next)
		p->pidhash_next->pidhash_pprev = p->pidhash_pprev;
	*p->pidhash_pprev = p->pidhash_next;
	spin_unlock_irqrestore(&pidhash_lock, flags);
}

extern __inline__ struct task_struct *find_task_by_pid(int pid)
{
	struct task_struct *p, **htable = &pidhash[pid_hashfn(pid)];
	unsigned long flags;

	spin_lock_irqsave(&pidhash_lock, flags);
	for(p = *htable; p && p->pid != pid; p = p->pidhash_next)
		;
	spin_unlock_irqrestore(&pidhash_lock, flags);

	return p;
}

/* per-UID process charging. */
extern int charge_uid(struct task_struct *p, int count);

#include <asm/current.h>

extern unsigned long volatile jiffies;
extern unsigned long itimer_ticks;
extern unsigned long itimer_next;
extern struct timeval xtime;
extern int need_resched;
extern void do_timer(struct pt_regs *);

extern unsigned int * prof_buffer;
extern unsigned long prof_len;
extern unsigned long prof_shift;

extern int securelevel;	/* system security level */

#define CURRENT_TIME (xtime.tv_sec)

extern void FASTCALL(sleep_on(struct wait_queue ** p));
extern void FASTCALL(interruptible_sleep_on(struct wait_queue ** p));
extern void FASTCALL(wake_up(struct wait_queue ** p));
extern void FASTCALL(wake_up_interruptible(struct wait_queue ** p));
extern void FASTCALL(wake_up_process(struct task_struct * tsk));

extern int in_group_p(gid_t grp);

extern void flush_signals(struct task_struct *);
extern void flush_signal_handlers(struct task_struct *);
extern int dequeue_signal(sigset_t *block, siginfo_t *);
extern int send_sig_info(int, struct siginfo *info, struct task_struct *);
extern int force_sig_info(int, struct siginfo *info, struct task_struct *);
extern int kill_pg_info(int, struct siginfo *info, pid_t);
extern int kill_sl_info(int, struct siginfo *info, pid_t);
extern int kill_proc_info(int, struct siginfo *info, pid_t);
extern int kill_something_info(int, struct siginfo *info, int);
extern void notify_parent(struct task_struct * tsk, int);
extern void force_sig(int sig, struct task_struct * p);
extern int send_sig(int sig, struct task_struct * p, int priv);
extern int kill_pg(pid_t, int, int);
extern int kill_sl(pid_t, int, int);
extern int kill_proc(pid_t, int, int);
extern int do_sigaction(int sig, const struct k_sigaction *act,
			struct k_sigaction *oact);

extern inline int signal_pending(struct task_struct *p)
{
	return (p->sigpending != 0);
}

/* Reevaluate whether the task has signals pending delivery.
   This is required every time the blocked sigset_t changes.
   All callers should have t->sigmask_lock.  */

static inline void recalc_sigpending(struct task_struct *t)
{
	unsigned long ready;
	long i;

	switch (_NSIG_WORDS) {
	default:
		for (i = _NSIG_WORDS, ready = 0; --i >= 0 ;)
			ready |= t->signal.sig[i] &~ t->blocked.sig[i];
		break;

	case 4: ready  = t->signal.sig[3] &~ t->blocked.sig[3];
		ready |= t->signal.sig[2] &~ t->blocked.sig[2];
		ready |= t->signal.sig[1] &~ t->blocked.sig[1];
		ready |= t->signal.sig[0] &~ t->blocked.sig[0];
		break;

	case 2: ready  = t->signal.sig[1] &~ t->blocked.sig[1];
		ready |= t->signal.sig[0] &~ t->blocked.sig[0];
		break;

	case 1: ready  = t->signal.sig[0] &~ t->blocked.sig[0];
	}

	t->sigpending = (ready != 0);
}


extern int request_irq(unsigned int irq,
		       void (*handler)(int, void *, struct pt_regs *),
		       unsigned long flags, 
		       const char *device,
		       void *dev_id);
extern void free_irq(unsigned int irq, void *dev_id);

/*
 * This has now become a routine instead of a macro, it sets a flag if
 * it returns true (to do BSD-style accounting where the process is flagged
 * if it uses root privs). The implication of this is that you should do
 * normal permissions checks first, and check suser() last.
 */
extern inline int suser(void)
{
	if (current->euid == 0) {
		current->flags |= PF_SUPERPRIV;
		return 1;
	}
	return 0;
}

/*
 * Routines for handling mm_structs
 */
extern struct mm_struct * mm_alloc(void);
static inline void mmget(struct mm_struct * mm)
{
	mm->count++;
}
extern void mmput(struct mm_struct *);

extern int  copy_thread(int, unsigned long, unsigned long, struct task_struct *, struct pt_regs *);
extern void flush_thread(void);
extern void exit_thread(void);

extern void exit_mm(struct task_struct *);
extern void exit_fs(struct task_struct *);
extern void exit_files(struct task_struct *);
extern void exit_sighand(struct task_struct *);

extern int do_execve(char *, char **, char **, struct pt_regs *);
extern int do_fork(unsigned long, unsigned long, struct pt_regs *);

/* See if we have a valid user level fd.
 * If it makes sense, return the file structure it references.
 * Otherwise return NULL.
 */
extern inline struct file *file_from_fd(const unsigned int fd)
{

	if (fd >= NR_OPEN)
		return NULL;
	/* either valid or null */
	return current->files->fd[fd];
}
	
/*
 * The wait-queues are circular lists, and you have to be *very* sure
 * to keep them correct. Use only these two functions to add/remove
 * entries in the queues.
 */
extern inline void __add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	wait->next = *p ? : WAIT_QUEUE_HEAD(p);
	*p = wait;
}

extern rwlock_t waitqueue_lock;

extern inline void add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	unsigned long flags;

	write_lock_irqsave(&waitqueue_lock, flags);
	__add_wait_queue(p, wait);
	write_unlock_irqrestore(&waitqueue_lock, flags);
}

extern inline void __remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	struct wait_queue * next = wait->next;
	struct wait_queue * head = next;
	struct wait_queue * tmp;

	while ((tmp = head->next) != wait) {
		head = tmp;
	}
	head->next = next;
}

extern inline void remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	unsigned long flags;

	write_lock_irqsave(&waitqueue_lock, flags);
	__remove_wait_queue(p, wait);
	write_unlock_irqrestore(&waitqueue_lock, flags); 
}

#define REMOVE_LINKS(p) do { unsigned long flags; \
	write_lock_irqsave(&tasklist_lock, flags); \
	(p)->next_task->prev_task = (p)->prev_task; \
	(p)->prev_task->next_task = (p)->next_task; \
	write_unlock_irqrestore(&tasklist_lock, flags); \
	if ((p)->p_osptr) \
		(p)->p_osptr->p_ysptr = (p)->p_ysptr; \
	if ((p)->p_ysptr) \
		(p)->p_ysptr->p_osptr = (p)->p_osptr; \
	else \
		(p)->p_pptr->p_cptr = (p)->p_osptr; \
	} while (0)

#define SET_LINKS(p) do { unsigned long flags; \
	write_lock_irqsave(&tasklist_lock, flags); \
	(p)->next_task = &init_task; \
	(p)->prev_task = init_task.prev_task; \
	init_task.prev_task->next_task = (p); \
	init_task.prev_task = (p); \
	write_unlock_irqrestore(&tasklist_lock, flags); \
	(p)->p_ysptr = NULL; \
	if (((p)->p_osptr = (p)->p_pptr->p_cptr) != NULL) \
		(p)->p_osptr->p_ysptr = p; \
	(p)->p_pptr->p_cptr = p; \
	} while (0)

#define for_each_task(p) \
	for (p = &init_task ; (p = p->next_task) != &init_task ; )

#endif /* __KERNEL__ */

#endif

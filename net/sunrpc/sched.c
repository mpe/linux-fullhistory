/*
 * linux/net/sunrpc/sched.c
 *
 * Scheduling for synchronous and asynchronous RPC requests.
 *
 * Copyright (C) 1996 Olaf Kirch, <okir@monad.swb.de>
 * 
 * TCP NFS related read + write fixes
 * (C) 1999 Dave Airlie, University of Limerick, Ireland <airlied@linux.ie>
 */

#include <linux/module.h>

#define __KERNEL_SYSCALLS__
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/unistd.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <linux/sunrpc/clnt.h>

#ifdef RPC_DEBUG
#define RPCDBG_FACILITY		RPCDBG_SCHED
static int			rpc_task_id = 0;
#endif

/*
 * We give RPC the same get_free_pages priority as NFS
 */
#define GFP_RPC			GFP_NFS

static void			__rpc_default_timer(struct rpc_task *task);
static void			rpciod_killall(void);

/*
 * When an asynchronous RPC task is activated within a bottom half
 * handler, or while executing another RPC task, it is put on
 * schedq, and rpciod is woken up.
 */
static struct rpc_wait_queue	schedq = RPC_INIT_WAITQ("schedq");

/*
 * RPC tasks that create another task (e.g. for contacting the portmapper)
 * will wait on this queue for their child's completion
 */
static struct rpc_wait_queue	childq = RPC_INIT_WAITQ("childq");

/*
 * RPC tasks sit here while waiting for conditions to improve.
 */
static struct rpc_wait_queue	delay_queue = RPC_INIT_WAITQ("delayq");

/*
 * All RPC tasks are linked into this list
 */
static struct rpc_task *	all_tasks = NULL;

/*
 * rpciod-related stuff
 */
static struct wait_queue *	rpciod_idle = NULL;
static struct wait_queue *	rpciod_killer = NULL;
static struct semaphore		rpciod_sema = MUTEX;
static unsigned int		rpciod_users = 0;
static pid_t			rpciod_pid = 0;
static int			rpc_inhibit = 0;

/*
 * This is the last-ditch buffer for NFS swap requests
 */
static u32			swap_buffer[PAGE_SIZE >> 2];
static int			swap_buffer_used = 0;

/*
 * Add new request to wait queue.
 *
 * Swapper tasks always get inserted at the head of the queue.
 * This should avoid many nasty memory deadlocks and hopefully
 * improve overall performance.
 * Everyone else gets appended to the queue to ensure proper FIFO behavior.
 */
int
rpc_add_wait_queue(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	if (task->tk_rpcwait) {
		if (task->tk_rpcwait != queue)
		{
			printk(KERN_WARNING "RPC: doubly enqueued task!\n");
			return -EWOULDBLOCK;
		}
		return 0;
	}
	if (RPC_IS_SWAPPER(task))
		rpc_insert_list(&queue->task, task);
	else
		rpc_append_list(&queue->task, task);
	task->tk_rpcwait = queue;

	dprintk("RPC: %4d added to queue %p \"%s\"\n",
				task->tk_pid, queue, rpc_qname(queue));

	return 0;
}

/*
 * Remove request from queue.
 * Note: must be called with interrupts disabled.
 */
void
rpc_remove_wait_queue(struct rpc_task *task)
{
	struct rpc_wait_queue *queue;

	if (!(queue = task->tk_rpcwait))
		return;
	rpc_remove_list(&queue->task, task);
	task->tk_rpcwait = NULL;

	dprintk("RPC: %4d removed from queue %p \"%s\"\n",
				task->tk_pid, queue, rpc_qname(queue));
}

/*
 * Set up a timer for the current task.
 */
inline void
rpc_add_timer(struct rpc_task *task, rpc_action timer)
{
	unsigned long	expires = jiffies + task->tk_timeout;

	dprintk("RPC: %4d setting alarm for %lu ms\n",
			task->tk_pid, task->tk_timeout * 1000 / HZ);
	if (!timer)
		timer = __rpc_default_timer;
	if (time_before(expires, jiffies)) {
		printk(KERN_ERR "RPC: bad timeout value %ld - setting to 10 sec!\n",
					task->tk_timeout);
		expires = jiffies + 10 * HZ;
	}
	task->tk_timer.expires  = expires;
	task->tk_timer.data     = (unsigned long) task;
	task->tk_timer.function = (void (*)(unsigned long)) timer;
	task->tk_timer.prev     = NULL;
	task->tk_timer.next     = NULL;
	add_timer(&task->tk_timer);
}

/*
 * Delete any timer for the current task.
 * Must be called with interrupts off.
 */
inline void
rpc_del_timer(struct rpc_task *task)
{
	if (task->tk_timeout) {
		dprintk("RPC: %4d deleting timer\n", task->tk_pid);
		del_timer(&task->tk_timer);
		task->tk_timeout = 0;
	}
}

/*
 * Make an RPC task runnable.
 *
 * Note: If the task is ASYNC, this must be called with 
 * interrupts disabled to protect the wait queue operation.
 */
static inline void
rpc_make_runnable(struct rpc_task *task)
{
	if (task->tk_timeout) {
		printk(KERN_ERR "RPC: task w/ running timer in rpc_make_runnable!!\n");
		return;
	}
	if (RPC_IS_ASYNC(task)) {
		int status;
		status = rpc_add_wait_queue(&schedq, task);
		if (status)
		{
			printk(KERN_WARNING "RPC: failed to add task to queue: error: %d!\n", status);
			task->tk_status = status;
		}
		wake_up(&rpciod_idle);
	} else {
		wake_up(&task->tk_wait);
	}
	task->tk_flags |= RPC_TASK_RUNNING;
}


/*
 *	For other people who may need to wake the I/O daemon
 *	but should (for now) know nothing about its innards
 */
 
void rpciod_wake_up(void)
{
	if(rpciod_pid==0)
	{
		printk(KERN_ERR "rpciod: wot no daemon?\n");
	}
	wake_up(&rpciod_idle);
}

/*
 * Prepare for sleeping on a wait queue.
 * By always appending tasks to the list we ensure FIFO behavior.
 * NB: An RPC task will only receive interrupt-driven events as long
 * as it's on a wait queue.
 */
static void
__rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
			rpc_action action, rpc_action timer)
{
	unsigned long	oldflags;
	int status;

	dprintk("RPC: %4d sleep_on(queue \"%s\" time %ld)\n", task->tk_pid,
				rpc_qname(q), jiffies);

	/*
	 * Protect the execution below.
	 */
	save_flags(oldflags); cli();

	status = rpc_add_wait_queue(q, task);
	if (status)
	{
		printk(KERN_WARNING "RPC: failed to add task to queue: error: %d!\n", status);
		task->tk_status = status;
		task->tk_flags |= RPC_TASK_RUNNING;
	}
	else
	{
		task->tk_callback = action;
		if (task->tk_timeout)
			rpc_add_timer(task, timer);
		task->tk_flags &= ~RPC_TASK_RUNNING;
	}

	restore_flags(oldflags);
	return;
}

void
rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
				rpc_action action, rpc_action timer)
{
	__rpc_sleep_on(q, task, action, timer);
}

/*
 * Wake up a single task -- must be invoked with bottom halves off.
 *
 * It would probably suffice to cli/sti the del_timer and remove_wait_queue
 * operations individually.
 */
static void
__rpc_wake_up(struct rpc_task *task)
{
	dprintk("RPC: %4d __rpc_wake_up (now %ld inh %d)\n",
					task->tk_pid, jiffies, rpc_inhibit);

#ifdef RPC_DEBUG
	if (task->tk_magic != 0xf00baa) {
		printk(KERN_ERR "RPC: attempt to wake up non-existing task!\n");
		rpc_debug = ~0;
		return;
	}
#endif
	rpc_del_timer(task);
	if (task->tk_rpcwait != &schedq)
		rpc_remove_wait_queue(task);
	if (!RPC_IS_RUNNING(task)) {
		task->tk_flags |= RPC_TASK_CALLBACK;
		rpc_make_runnable(task);
	}
	dprintk("RPC:      __rpc_wake_up done\n");
}

/*
 * Default timeout handler if none specified by user
 */
static void
__rpc_default_timer(struct rpc_task *task)
{
	dprintk("RPC: %d timeout (default timer)\n", task->tk_pid);
	task->tk_status = -ETIMEDOUT;
	task->tk_timeout = 0;
	__rpc_wake_up(task);
}

/*
 * Wake up the specified task
 */
void
rpc_wake_up_task(struct rpc_task *task)
{
	unsigned long	oldflags;

	save_flags(oldflags); cli();
	__rpc_wake_up(task);
	restore_flags(oldflags);
}

/*
 * Wake up the next task on the wait queue.
 */
struct rpc_task *
rpc_wake_up_next(struct rpc_wait_queue *queue)
{
	unsigned long	oldflags;
	struct rpc_task	*task;

	dprintk("RPC:      wake_up_next(%p \"%s\")\n", queue, rpc_qname(queue));
	save_flags(oldflags); cli();
	if ((task = queue->task) != 0)
		__rpc_wake_up(task);
	restore_flags(oldflags);

	return task;
}

/*
 * Wake up all tasks on a queue
 */
void
rpc_wake_up(struct rpc_wait_queue *queue)
{
	unsigned long	oldflags;

	save_flags(oldflags); cli();
	while (queue->task)
		__rpc_wake_up(queue->task);
	restore_flags(oldflags);
}

/*
 * Wake up all tasks on a queue, and set their status value.
 */
void
rpc_wake_up_status(struct rpc_wait_queue *queue, int status)
{
	struct rpc_task	*task;
	unsigned long	oldflags;

	save_flags(oldflags); cli();
	while ((task = queue->task) != NULL) {
		task->tk_status = status;
		__rpc_wake_up(task);
	}
	restore_flags(oldflags);
}

/*
 * Run a task at a later time
 */
static void	__rpc_atrun(struct rpc_task *);
void
rpc_delay(struct rpc_task *task, unsigned long delay)
{
	task->tk_timeout = delay;
	rpc_sleep_on(&delay_queue, task, NULL, __rpc_atrun);
}

static void
__rpc_atrun(struct rpc_task *task)
{
	task->tk_status = 0;
	__rpc_wake_up(task);
}

/*
 * This is the RPC `scheduler' (or rather, the finite state machine).
 */
static int
__rpc_execute(struct rpc_task *task)
{
	unsigned long	oldflags;
	int		status = 0;

	dprintk("RPC: %4d rpc_execute flgs %x\n",
				task->tk_pid, task->tk_flags);

	if (!RPC_IS_RUNNING(task)) {
		printk(KERN_WARNING "RPC: rpc_execute called for sleeping task!!\n");
		return 0;
	}

	while (1) {
		/*
		 * Execute any pending callback.
		 */
		if (task->tk_flags & RPC_TASK_CALLBACK) {
			/* Define a callback save pointer */
			void (*save_callback)(struct rpc_task *);
	
			task->tk_flags &= ~RPC_TASK_CALLBACK;
			/* 
			 * If a callback exists, save it, reset it,
			 * call it.
			 * The save is needed to stop from resetting
			 * another callback set within the callback handler
			 * - Dave
			 */
			if (task->tk_callback) {
				save_callback=task->tk_callback;
				task->tk_callback=NULL;
				save_callback(task);
			}
		}

		/*
		 * No handler for next step means exit.
		 */
		if (!task->tk_action)
			break;

		/*
		 * Perform the next FSM step.
		 * tk_action may be NULL when the task has been killed
		 * by someone else.
		 */
		if (RPC_IS_RUNNING(task) && task->tk_action)
			task->tk_action(task);

		/*
		 * Check whether task is sleeping.
		 * Note that if the task may go to sleep in tk_action,
		 * and the RPC reply arrives before we get here, it will
		 * have state RUNNING, but will still be on schedq.
		 */
		save_flags(oldflags); cli();
		if (RPC_IS_RUNNING(task)) {
			if (task->tk_rpcwait == &schedq)
				rpc_remove_wait_queue(task);
		} else while (!RPC_IS_RUNNING(task)) {
			if (RPC_IS_ASYNC(task)) {
				restore_flags(oldflags);
				return 0;
			}

			/* sync task: sleep here */
			dprintk("RPC: %4d sync task going to sleep\n",
							task->tk_pid);
			if (current->pid == rpciod_pid)
				printk(KERN_ERR "RPC: rpciod waiting on sync task!\n");
			sleep_on(&task->tk_wait);

			/*
			 * When the task received a signal, remove from
			 * any queues etc, and make runnable again.
			 */
			if (signalled())
				__rpc_wake_up(task);

			dprintk("RPC: %4d sync task resuming\n",
							task->tk_pid);
		}
		restore_flags(oldflags);

		/*
		 * When a sync task receives a signal, it exits with
		 * -ERESTARTSYS. In order to catch any callbacks that
		 * clean up after sleeping on some queue, we don't
		 * break the loop here, but go around once more.
		 */
		if (!RPC_IS_ASYNC(task) && signalled()) {
			dprintk("RPC: %4d got signal\n", task->tk_pid);
			rpc_exit(task, -ERESTARTSYS);
		}
	}

	dprintk("RPC: %4d exit() = %d\n", task->tk_pid, task->tk_status);
	if (task->tk_exit) {
		status = task->tk_status;
		task->tk_exit(task);
	}

	return status;
}

/*
 * User-visible entry point to the scheduler.
 * The recursion protection is for debugging. It should go away once
 * the code has stabilized.
 */
void
rpc_execute(struct rpc_task *task)
{
	static int	executing = 0;
	int		incr = RPC_IS_ASYNC(task)? 1 : 0;

	if (incr) {
		if (rpc_inhibit) {
			printk(KERN_INFO "RPC: execution inhibited!\n");
			return;
		}
		if (executing)
			printk(KERN_WARNING "RPC: %d tasks executed\n", executing);
	}
	
	executing += incr;
	__rpc_execute(task);
	executing -= incr;
}

/*
 * This is our own little scheduler for async RPC tasks.
 */
static void
__rpc_schedule(void)
{
	struct rpc_task	*task;
	int		count = 0;
	unsigned long	oldflags;
	int need_resched = current->need_resched;

	dprintk("RPC:      rpc_schedule enter\n");
	save_flags(oldflags);
	while (1) {
		cli();
		if (!(task = schedq.task))
			break;
		rpc_del_timer(task);
		rpc_remove_wait_queue(task);
		task->tk_flags |= RPC_TASK_RUNNING;
		restore_flags(oldflags);

		__rpc_execute(task);

		if (++count >= 200) {
			count = 0;
			need_resched = 1;
		}
		if (need_resched)
			schedule();
	}
	restore_flags(oldflags);
	dprintk("RPC:      rpc_schedule leave\n");
}

/*
 * Allocate memory for RPC purpose.
 *
 * This is yet another tricky issue: For sync requests issued by
 * a user process, we want to make kmalloc sleep if there isn't
 * enough memory. Async requests should not sleep too excessively
 * because that will block rpciod (but that's not dramatic when
 * it's starved of memory anyway). Finally, swapout requests should
 * never sleep at all, and should not trigger another swap_out
 * request through kmalloc which would just increase memory contention.
 *
 * I hope the following gets it right, which gives async requests
 * a slight advantage over sync requests (good for writeback, debatable
 * for readahead):
 *
 *   sync user requests:	GFP_KERNEL
 *   async requests:		GFP_RPC		(== GFP_NFS)
 *   swap requests:		GFP_ATOMIC	(or new GFP_SWAPPER)
 */
void *
rpc_allocate(unsigned int flags, unsigned int size)
{
	u32	*buffer;
	int	gfp;

	if (flags & RPC_TASK_SWAPPER)
		gfp = GFP_ATOMIC;
	else if (flags & RPC_TASK_ASYNC)
		gfp = GFP_RPC;
	else
		gfp = GFP_KERNEL;

	do {
		if ((buffer = (u32 *) kmalloc(size, gfp)) != NULL) {
			dprintk("RPC:      allocated buffer %p\n", buffer);
			return buffer;
		}
		if ((flags & RPC_TASK_SWAPPER) && !swap_buffer_used++) {
			dprintk("RPC:      used last-ditch swap buffer\n");
			return swap_buffer;
		}
		if (flags & RPC_TASK_ASYNC)
			return NULL;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ>>4);
	} while (!signalled());

	return NULL;
}

void
rpc_free(void *buffer)
{
	if (buffer != swap_buffer) {
		kfree(buffer);
		return;
	}
	swap_buffer_used = 0;
}

/*
 * Creation and deletion of RPC task structures
 */
inline void
rpc_init_task(struct rpc_task *task, struct rpc_clnt *clnt,
				rpc_action callback, int flags)
{
	memset(task, 0, sizeof(*task));
	task->tk_client = clnt;
	task->tk_flags  = RPC_TASK_RUNNING | flags;
	task->tk_exit   = callback;
	if (current->uid != current->fsuid || current->gid != current->fsgid)
		task->tk_flags |= RPC_TASK_SETUID;

	/* Initialize retry counters */
	task->tk_garb_retry = 2;
	task->tk_cred_retry = 2;
	task->tk_suid_retry = 1;

	/* Add to global list of all tasks */
	task->tk_next_task = all_tasks;
	task->tk_prev_task = NULL;
	if (all_tasks)
		all_tasks->tk_prev_task = task;
	all_tasks = task;

	if (clnt)
		clnt->cl_users++;

#ifdef RPC_DEBUG
	task->tk_magic = 0xf00baa;
	task->tk_pid = rpc_task_id++;
#endif
	dprintk("RPC: %4d new task procpid %d\n", task->tk_pid,
				current->pid);
}

/*
 * Create a new task for the specified client.  We have to
 * clean up after an allocation failure, as the client may
 * have specified "oneshot".
 */
struct rpc_task *
rpc_new_task(struct rpc_clnt *clnt, rpc_action callback, int flags)
{
	struct rpc_task	*task;

	task = (struct rpc_task *) rpc_allocate(flags, sizeof(*task));
	if (!task)
		goto cleanup;

	rpc_init_task(task, clnt, callback, flags);

	dprintk("RPC: %4d allocated task\n", task->tk_pid);
	task->tk_flags |= RPC_TASK_DYNAMIC;
out:
	return task;

cleanup:
	/* Check whether to release the client */
	if (clnt) {
		printk("rpc_new_task: failed, users=%d, oneshot=%d\n",
			clnt->cl_users, clnt->cl_oneshot);
		clnt->cl_users++; /* pretend we were used ... */
		rpc_release_client(clnt);
	}
	goto out;
}

void
rpc_release_task(struct rpc_task *task)
{
	struct rpc_task	*next, *prev;

	dprintk("RPC: %4d release task\n", task->tk_pid);

	/* Remove from global task list */
	prev = task->tk_prev_task;
	next = task->tk_next_task;
	if (next)
		next->tk_prev_task = prev;
	if (prev)
		prev->tk_next_task = next;
	else
		all_tasks = next;

	/* Release resources */
	if (task->tk_rqstp)
		xprt_release(task);
	if (task->tk_cred)
		rpcauth_releasecred(task);
	if (task->tk_buffer) {
		rpc_free(task->tk_buffer);
		task->tk_buffer = NULL;
	}
	if (task->tk_client) {
		rpc_release_client(task->tk_client);
		task->tk_client = NULL;
	}

#ifdef RPC_DEBUG
	task->tk_magic = 0;
#endif

	if (task->tk_flags & RPC_TASK_DYNAMIC) {
		dprintk("RPC: %4d freeing task\n", task->tk_pid);
		task->tk_flags &= ~RPC_TASK_DYNAMIC;
		rpc_free(task);
	}
}

/*
 * Handling of RPC child tasks
 * We can't simply call wake_up(parent) here, because the
 * parent task may already have gone away
 */
static inline struct rpc_task *
rpc_find_parent(struct rpc_task *child)
{
	struct rpc_task	*temp, *parent;

	parent = (struct rpc_task *) child->tk_calldata;
	for (temp = childq.task; temp; temp = temp->tk_next) {
		if (temp == parent)
			return parent;
	}
	return NULL;
}

static void
rpc_child_exit(struct rpc_task *child)
{
	struct rpc_task	*parent;

	if ((parent = rpc_find_parent(child)) != NULL) {
		parent->tk_status = child->tk_status;
		rpc_wake_up_task(parent);
	}
	rpc_release_task(child);
}

/*
 * Note: rpc_new_task releases the client after a failure.
 */
struct rpc_task *
rpc_new_child(struct rpc_clnt *clnt, struct rpc_task *parent)
{
	struct rpc_task	*task;

	task = rpc_new_task(clnt, NULL, RPC_TASK_ASYNC | RPC_TASK_CHILD);
	if (!task)
		goto fail;
	task->tk_exit = rpc_child_exit;
	task->tk_calldata = parent;
	return task;

fail:
	parent->tk_status = -ENOMEM;
	return NULL;
}

void
rpc_run_child(struct rpc_task *task, struct rpc_task *child, rpc_action func)
{
	unsigned long oldflags;

	save_flags(oldflags); cli();
	rpc_make_runnable(child);
	restore_flags(oldflags);
	/* N.B. Is it possible for the child to have already finished? */
	rpc_sleep_on(&childq, task, func, NULL);
}

/*
 * Kill all tasks for the given client.
 * XXX: kill their descendants as well?
 */
void
rpc_killall_tasks(struct rpc_clnt *clnt)
{
	struct rpc_task	**q, *rovr;

	dprintk("RPC:      killing all tasks for client %p\n", clnt);
	/* N.B. Why bother to inhibit? Nothing blocks here ... */
	rpc_inhibit++;
	for (q = &all_tasks; (rovr = *q); q = &rovr->tk_next_task) {
		if (!clnt || rovr->tk_client == clnt) {
			rovr->tk_flags |= RPC_TASK_KILLED;
			rpc_exit(rovr, -EIO);
			rpc_wake_up_task(rovr);
		}
	}
	rpc_inhibit--;
}

static struct semaphore rpciod_running = MUTEX_LOCKED;

/*
 * This is the rpciod kernel thread
 */
static int
rpciod(void *ptr)
{
	struct wait_queue **assassin = (struct wait_queue **) ptr;
	unsigned long	oldflags;
	int		rounds = 0;

	MOD_INC_USE_COUNT;
	lock_kernel();
	/*
	 * Let our maker know we're running ...
	 */
	rpciod_pid = current->pid;
	up(&rpciod_running);

	exit_files(current);
	exit_mm(current);

	spin_lock_irq(&current->sigmask_lock);
	siginitsetinv(&current->blocked, sigmask(SIGKILL));
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "rpciod");

	dprintk("RPC: rpciod starting (pid %d)\n", rpciod_pid);
	while (rpciod_users) {
		if (signalled()) {
			rpciod_killall();
			flush_signals(current);
		}
		__rpc_schedule();

		if (++rounds >= 64) {	/* safeguard */
			schedule();
			rounds = 0;
		}
		save_flags(oldflags); cli();
		dprintk("RPC: rpciod running checking dispatch\n");
		rpciod_tcp_dispatcher();

		if (!schedq.task) {
			dprintk("RPC: rpciod back to sleep\n");
			interruptible_sleep_on(&rpciod_idle);
			dprintk("RPC: switch to rpciod\n");
			rpciod_tcp_dispatcher();
			rounds = 0;
		}
		restore_flags(oldflags);
	}

	dprintk("RPC: rpciod shutdown commences\n");
	if (all_tasks) {
		printk(KERN_ERR "rpciod: active tasks at shutdown?!\n");
		rpciod_killall();
	}

	rpciod_pid = 0;
	wake_up(assassin);

	dprintk("RPC: rpciod exiting\n");
	MOD_DEC_USE_COUNT;
	return 0;
}

static void
rpciod_killall(void)
{
	unsigned long flags;

	while (all_tasks) {
		current->sigpending = 0;
		rpc_killall_tasks(NULL);
		__rpc_schedule();
		if (all_tasks) {
			dprintk("rpciod_killall: waiting for tasks to exit\n");
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
	}

	spin_lock_irqsave(&current->sigmask_lock, flags);
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);
}

/*
 * Start up the rpciod process if it's not already running.
 */
int
rpciod_up(void)
{
	int error = 0;

	MOD_INC_USE_COUNT;
	down(&rpciod_sema);
	dprintk("rpciod_up: pid %d, users %d\n", rpciod_pid, rpciod_users);
	rpciod_users++;
	if (rpciod_pid)
		goto out;
	/*
	 * If there's no pid, we should be the first user.
	 */
	if (rpciod_users > 1)
		printk(KERN_WARNING "rpciod_up: no pid, %d users??\n", rpciod_users);
	/*
	 * Create the rpciod thread and wait for it to start.
	 */
	error = kernel_thread(rpciod, &rpciod_killer, 0);
	if (error < 0) {
		printk(KERN_WARNING "rpciod_up: create thread failed, error=%d\n", error);
		rpciod_users--;
		goto out;
	}
	down(&rpciod_running);
	error = 0;
out:
	up(&rpciod_sema);
	MOD_DEC_USE_COUNT;
	return error;
}

void
rpciod_down(void)
{
	unsigned long flags;

	MOD_INC_USE_COUNT;
	down(&rpciod_sema);
	dprintk("rpciod_down pid %d sema %d\n", rpciod_pid, rpciod_users);
	if (rpciod_users) {
		if (--rpciod_users)
			goto out;
	} else
		printk(KERN_WARNING "rpciod_down: pid=%d, no users??\n", rpciod_pid);

	if (!rpciod_pid) {
		dprintk("rpciod_down: Nothing to do!\n");
		goto out;
	}

	kill_proc(rpciod_pid, SIGKILL, 1);
	/*
	 * Usually rpciod will exit very quickly, so we
	 * wait briefly before checking the process id.
	 */
	current->sigpending = 0;
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(1);
	/*
	 * Display a message if we're going to wait longer.
	 */
	while (rpciod_pid) {
		dprintk("rpciod_down: waiting for pid %d to exit\n", rpciod_pid);
		if (signalled()) {
			dprintk("rpciod_down: caught signal\n");
			break;
		}
		interruptible_sleep_on(&rpciod_killer);
	}
	spin_lock_irqsave(&current->sigmask_lock, flags);
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);
out:
	up(&rpciod_sema);
	MOD_DEC_USE_COUNT;
}

#ifdef RPC_DEBUG
#include <linux/nfs_fs.h>
void rpc_show_tasks(void)
{
	struct rpc_task *t = all_tasks, *next;
	struct nfs_wreq *wreq;

	if (!t)
		return;
	printk("-pid- proc flgs status -client- -prog- --rqstp- -timeout "
		"-rpcwait -action- --exit--\n");
	for (; t; t = next) {
		next = t->tk_next_task;
		printk("%05d %04d %04x %06d %8p %6d %8p %08ld %8s %8p %8p\n",
			t->tk_pid, t->tk_proc, t->tk_flags, t->tk_status,
			t->tk_client, t->tk_client->cl_prog,
			t->tk_rqstp, t->tk_timeout,
			t->tk_rpcwait ? rpc_qname(t->tk_rpcwait) : " <NULL> ",
			t->tk_action, t->tk_exit);

		if (!(t->tk_flags & RPC_TASK_NFSWRITE))
			continue;
		/* NFS write requests */
		wreq = (struct nfs_wreq *) t->tk_calldata;
		printk("     NFS: flgs=%08x, pid=%d, pg=%p, off=(%d, %d)\n",
			wreq->wb_flags, wreq->wb_pid, wreq->wb_page,
			wreq->wb_offset, wreq->wb_bytes);
		printk("          name=%s/%s\n",
			wreq->wb_dentry->d_parent->d_name.name,
			wreq->wb_dentry->d_name.name);
	}
}
#endif

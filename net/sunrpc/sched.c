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
#include <linux/spinlock.h>

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
static DECLARE_WAIT_QUEUE_HEAD(rpciod_idle);
static DECLARE_WAIT_QUEUE_HEAD(rpciod_killer);
static DECLARE_MUTEX(rpciod_sema);
static unsigned int		rpciod_users = 0;
static pid_t			rpciod_pid = 0;
static int			rpc_inhibit = 0;

/*
 * Spinlock for wait queues. Access to the latter also has to be
 * interrupt-safe in order to allow timers to wake up sleeping tasks.
 */
spinlock_t rpc_queue_lock = SPIN_LOCK_UNLOCKED;
/*
 * Spinlock for other critical sections of code.
 */
spinlock_t rpc_sched_lock = SPIN_LOCK_UNLOCKED;

/*
 * This is the last-ditch buffer for NFS swap requests
 */
static u32			swap_buffer[PAGE_SIZE >> 2];
static int			swap_buffer_used = 0;

/*
 * Make allocation of the swap_buffer SMP-safe
 */
static __inline__ int rpc_lock_swapbuf(void)
{
	return !test_and_set_bit(1, &swap_buffer_used);
}
static __inline__ void rpc_unlock_swapbuf(void)
{
	clear_bit(1, &swap_buffer_used);
}

/*
 * Set up a timer for the current task.
 */
static inline void
__rpc_add_timer(struct rpc_task *task, rpc_action timer)
{
	if (!task->tk_timeout)
		return;

	dprintk("RPC: %4d setting alarm for %lu ms\n",
			task->tk_pid, task->tk_timeout * 1000 / HZ);

	if (timer_pending(&task->tk_timer)) {
		printk(KERN_ERR "RPC: Bug! Overwriting active timer\n");
		del_timer(&task->tk_timer);
	}
	if (!timer)
		timer = __rpc_default_timer;
	init_timer(&task->tk_timer);
	task->tk_timer.expires  = jiffies + task->tk_timeout;
	task->tk_timer.data     = (unsigned long) task;
	task->tk_timer.function = (void (*)(unsigned long)) timer;
	add_timer(&task->tk_timer);
}

/*
 * Set up a timer for an already sleeping task.
 */
void rpc_add_timer(struct rpc_task *task, rpc_action timer)
{
	spin_lock_bh(&rpc_queue_lock);
	if (!(RPC_IS_RUNNING(task) || task->tk_wakeup))
		__rpc_add_timer(task, timer);
	spin_unlock_bh(&rpc_queue_lock);
}

/*
 * Delete any timer for the current task.
 */
static inline void
__rpc_del_timer(struct rpc_task *task)
{
	dprintk("RPC: %4d deleting timer\n", task->tk_pid);
	if (timer_pending(&task->tk_timer))
		del_timer(&task->tk_timer);
	task->tk_timeout = 0;
}

/*
 * Add new request to wait queue.
 *
 * Swapper tasks always get inserted at the head of the queue.
 * This should avoid many nasty memory deadlocks and hopefully
 * improve overall performance.
 * Everyone else gets appended to the queue to ensure proper FIFO behavior.
 */
static inline int
__rpc_add_wait_queue(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	if (task->tk_rpcwait == queue)
		return 0;

	if (task->tk_rpcwait) {
		printk(KERN_WARNING "RPC: doubly enqueued task!\n");
		return -EWOULDBLOCK;
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

int
rpc_add_wait_queue(struct rpc_wait_queue *q, struct rpc_task *task)
{
	int		result;

	spin_lock_bh(&rpc_queue_lock);
	result = __rpc_add_wait_queue(q, task);
	spin_unlock_bh(&rpc_queue_lock);
	return result;
}

/*
 * Remove request from queue.
 * Note: must be called with spin lock held.
 */
static inline void
__rpc_remove_wait_queue(struct rpc_task *task)
{
	struct rpc_wait_queue *queue = task->tk_rpcwait;

	if (!queue)
		return;

	rpc_remove_list(&queue->task, task);
	task->tk_rpcwait = NULL;

	dprintk("RPC: %4d removed from queue %p \"%s\"\n",
				task->tk_pid, queue, rpc_qname(queue));
}

void
rpc_remove_wait_queue(struct rpc_task *task)
{
	if (!task->tk_rpcwait)
		return;
	spin_lock_bh(&rpc_queue_lock);
	__rpc_remove_wait_queue(task);
	spin_unlock_bh(&rpc_queue_lock);
}

/*
 * Make an RPC task runnable.
 *
 * Note: If the task is ASYNC, this must be called with 
 * the spinlock held to protect the wait queue operation.
 */
static inline void
rpc_make_runnable(struct rpc_task *task)
{
	if (task->tk_timeout) {
		printk(KERN_ERR "RPC: task w/ running timer in rpc_make_runnable!!\n");
		return;
	}
	task->tk_flags |= RPC_TASK_RUNNING;
	if (RPC_IS_ASYNC(task)) {
		if (RPC_IS_SLEEPING(task)) {
			int status;
			status = __rpc_add_wait_queue(&schedq, task);
			if (status < 0) {
				printk(KERN_WARNING "RPC: failed to add task to queue: error: %d!\n", status);
				task->tk_status = status;
			} else
				task->tk_sleeping = 0;
		}
		wake_up(&rpciod_idle);
	} else {
		task->tk_sleeping = 0;
		wake_up(&task->tk_wait);
	}
}

/*
 * Place a newly initialized task on the schedq.
 */
static inline void
rpc_schedule_run(struct rpc_task *task)
{
	/* Don't run a child twice! */
	if (RPC_IS_ACTIVATED(task))
		return;
	task->tk_active = 1;
	task->tk_sleeping = 1;
	rpc_make_runnable(task);
}

/*
 *	For other people who may need to wake the I/O daemon
 *	but should (for now) know nothing about its innards
 */
void rpciod_wake_up(void)
{
	if(rpciod_pid==0)
		printk(KERN_ERR "rpciod: wot no daemon?\n");
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
	int status;

	dprintk("RPC: %4d sleep_on(queue \"%s\" time %ld)\n", task->tk_pid,
				rpc_qname(q), jiffies);

	if (!RPC_IS_ASYNC(task) && !RPC_IS_ACTIVATED(task)) {
		printk(KERN_ERR "RPC: Inactive synchronous task put to sleep!\n");
		return;
	}

	/* Mark the task as being activated if so needed */
	if (!RPC_IS_ACTIVATED(task)) {
		task->tk_active = 1;
		task->tk_sleeping = 1;
	}

	status = __rpc_add_wait_queue(q, task);
	if (status) {
		printk(KERN_WARNING "RPC: failed to add task to queue: error: %d!\n", status);
		task->tk_status = status;
	} else {
		task->tk_flags &= ~RPC_TASK_RUNNING;
		task->tk_callback = action;
		__rpc_add_timer(task, timer);
	}

	return;
}

void
rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
				rpc_action action, rpc_action timer)
{
	/*
	 * Protect the queue operations.
	 */
	spin_lock_bh(&rpc_queue_lock);
	__rpc_sleep_on(q, task, action, timer);
	spin_unlock_bh(&rpc_queue_lock);
}

void
rpc_sleep_locked(struct rpc_wait_queue *q, struct rpc_task *task,
		 rpc_action action, rpc_action timer)
{
	/*
	 * Protect the queue operations.
	 */
	spin_lock_bh(&rpc_queue_lock);
	__rpc_sleep_on(q, task, action, timer);
	rpc_lock_task(task);
	spin_unlock_bh(&rpc_queue_lock);
}

/*
 * Wake up a single task -- must be invoked with spin lock held.
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
		rpc_show_tasks();
		return;
	}
#endif
	/* Has the task been executed yet? If not, we cannot wake it up! */
	if (!RPC_IS_ACTIVATED(task)) {
		printk(KERN_ERR "RPC: Inactive task (%p) being woken up!\n", task);
		return;
	}
	if (RPC_IS_RUNNING(task))
		return;

	__rpc_del_timer(task);

	/* If the task has been locked, then set tk_wakeup so that
	 * rpc_unlock_task() wakes us up... */
	if (task->tk_lock) {
		task->tk_wakeup = 1;
		return;
	} else
		task->tk_wakeup = 0;

	if (task->tk_rpcwait != &schedq)
		__rpc_remove_wait_queue(task);
	task->tk_flags |= RPC_TASK_CALLBACK;
	rpc_make_runnable(task);

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
	rpc_wake_up_task(task);
}

/*
 * Wake up the specified task
 */
void
rpc_wake_up_task(struct rpc_task *task)
{
	if (RPC_IS_RUNNING(task))
		return;
	spin_lock_bh(&rpc_queue_lock);
	__rpc_wake_up(task);
	spin_unlock_bh(&rpc_queue_lock);
}

/*
 * Wake up the next task on the wait queue.
 */
struct rpc_task *
rpc_wake_up_next(struct rpc_wait_queue *queue)
{
	struct rpc_task	*task;

	dprintk("RPC:      wake_up_next(%p \"%s\")\n", queue, rpc_qname(queue));
	spin_lock_bh(&rpc_queue_lock);
	if ((task = queue->task) != 0)
		__rpc_wake_up(task);
	spin_unlock_bh(&rpc_queue_lock);

	return task;
}

/*
 * Wake up all tasks on a queue
 */
void
rpc_wake_up(struct rpc_wait_queue *queue)
{
	spin_lock_bh(&rpc_queue_lock);
	while (queue->task)
		__rpc_wake_up(queue->task);
	spin_unlock_bh(&rpc_queue_lock);
}

/*
 * Wake up all tasks on a queue, and set their status value.
 */
void
rpc_wake_up_status(struct rpc_wait_queue *queue, int status)
{
	struct rpc_task	*task;

	spin_lock_bh(&rpc_queue_lock);
	while ((task = queue->task) != NULL) {
		task->tk_status = status;
		__rpc_wake_up(task);
	}
	spin_unlock_bh(&rpc_queue_lock);
}

/*
 * Lock down a sleeping task to prevent it from waking up
 * and disappearing from beneath us.
 *
 * This function should always be called with the
 * rpc_queue_lock held.
 */
int
rpc_lock_task(struct rpc_task *task)
{
	if (!RPC_IS_RUNNING(task))
		return ++task->tk_lock;
	return 0;
}

void
rpc_unlock_task(struct rpc_task *task)
{
	spin_lock_bh(&rpc_queue_lock);
	if (task->tk_lock && !--task->tk_lock && task->tk_wakeup)
		__rpc_wake_up(task);
	spin_unlock_bh(&rpc_queue_lock);
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
	rpc_wake_up_task(task);
}

/*
 * This is the RPC `scheduler' (or rather, the finite state machine).
 */
static int
__rpc_execute(struct rpc_task *task)
{
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
		if (RPC_DO_CALLBACK(task)) {
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
		 * Perform the next FSM step.
		 * tk_action may be NULL when the task has been killed
		 * by someone else.
		 */
		if (RPC_IS_RUNNING(task)) {
			if (!task->tk_action)
				break;
			task->tk_action(task);
		}

		/*
		 * Check whether task is sleeping.
		 * Note that if the task goes to sleep in tk_action,
		 * and the RPC reply arrives before we get here, it will
		 * have state RUNNING, but will still be on schedq.
		 * 27/9/99: The above has been attempted fixed by
		 *          introduction of task->tk_sleeping.
		 */
		spin_lock_bh(&rpc_queue_lock);
		if (!RPC_IS_RUNNING(task)) {
			task->tk_sleeping = 1;
			if (RPC_IS_ASYNC(task)) {
				spin_unlock_bh(&rpc_queue_lock);
				return 0;
			}
		} else
			task->tk_sleeping = 0;
		spin_unlock_bh(&rpc_queue_lock);

		while (RPC_IS_SLEEPING(task)) {
			/* sync task: sleep here */
			dprintk("RPC: %4d sync task going to sleep\n",
							task->tk_pid);
			if (current->pid == rpciod_pid)
				printk(KERN_ERR "RPC: rpciod waiting on sync task!\n");

			__wait_event(task->tk_wait, !RPC_IS_SLEEPING(task));
			dprintk("RPC: %4d sync task resuming\n", task->tk_pid);

			/*
			 * When a sync task receives a signal, it exits with
			 * -ERESTARTSYS. In order to catch any callbacks that
			 * clean up after sleeping on some queue, we don't
			 * break the loop here, but go around once more.
			 */
			if (task->tk_client->cl_intr && signalled()) {
				dprintk("RPC: %4d got signal\n", task->tk_pid);
				task->tk_flags |= RPC_TASK_KILLED;
				rpc_exit(task, -ERESTARTSYS);
				rpc_wake_up_task(task);
			}
		}
	}

	dprintk("RPC: %4d exit() = %d\n", task->tk_pid, task->tk_status);
	status = task->tk_status;
	if (task->tk_exit)
		task->tk_exit(task);

	return status;
}

/*
 * User-visible entry point to the scheduler.
 *
 * This may be called recursively if e.g. an async NFS task updates
 * the attributes and finds that dirty pages must be flushed.
 */
int
rpc_execute(struct rpc_task *task)
{
	if (rpc_inhibit) {
		printk(KERN_INFO "RPC: execution inhibited!\n");
		return -EIO;
	}
	task->tk_flags |= RPC_TASK_RUNNING;
	if (task->tk_active) {
		printk(KERN_ERR "RPC: active task was run twice!\n");
		return -EWOULDBLOCK;
	}
	task->tk_active = 1;
	
	return __rpc_execute(task);
}

/*
 * This is our own little scheduler for async RPC tasks.
 */
static void
__rpc_schedule(void)
{
	struct rpc_task	*task;
	int		count = 0;

	dprintk("RPC:      rpc_schedule enter\n");
	while (1) {
		/* Ensure equal rights for tcp tasks... */
		rpciod_tcp_dispatcher();

		spin_lock_bh(&rpc_queue_lock);
		if (!(task = schedq.task)) {
			spin_unlock_bh(&rpc_queue_lock);
			break;
		}
		if (task->tk_lock) {
			spin_unlock_bh(&rpc_queue_lock);
			printk(KERN_ERR "RPC: Locked task was scheduled !!!!\n");
			rpc_debug = ~0;
			rpc_show_tasks();
			break;
		}
		__rpc_remove_wait_queue(task);
		spin_unlock_bh(&rpc_queue_lock);

		__rpc_execute(task);

		if (++count >= 200 || current->need_resched) {
			count = 0;
			schedule();
		}
	}
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
		if ((flags & RPC_TASK_SWAPPER) && size <= sizeof(swap_buffer)
		    && rpc_lock_swapbuf()) {
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
	rpc_unlock_swapbuf();
}

/*
 * Creation and deletion of RPC task structures
 */
inline void
rpc_init_task(struct rpc_task *task, struct rpc_clnt *clnt,
				rpc_action callback, int flags)
{
	memset(task, 0, sizeof(*task));
	init_timer(&task->tk_timer);
	task->tk_client = clnt;
	task->tk_flags  = flags;
	task->tk_exit   = callback;
	init_waitqueue_head(&task->tk_wait);
	if (current->uid != current->fsuid || current->gid != current->fsgid)
		task->tk_flags |= RPC_TASK_SETUID;

	/* Initialize retry counters */
	task->tk_garb_retry = 2;
	task->tk_cred_retry = 2;
	task->tk_suid_retry = 1;

	/* Add to global list of all tasks */
	spin_lock(&rpc_sched_lock);
	task->tk_next_task = all_tasks;
	task->tk_prev_task = NULL;
	if (all_tasks)
		all_tasks->tk_prev_task = task;
	all_tasks = task;
	spin_unlock(&rpc_sched_lock);

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

#ifdef RPC_DEBUG
	if (task->tk_magic != 0xf00baa) {
		printk(KERN_ERR "RPC: attempt to release a non-existing task!\n");
		rpc_debug = ~0;
		rpc_show_tasks();
		return;
	}
#endif

	/* Remove from global task list */
	spin_lock(&rpc_sched_lock);
	prev = task->tk_prev_task;
	next = task->tk_next_task;
	if (next)
		next->tk_prev_task = prev;
	if (prev)
		prev->tk_next_task = next;
	else
		all_tasks = next;
	task->tk_next_task = task->tk_prev_task = NULL;
	spin_unlock(&rpc_sched_lock);

	/* Protect the execution below. */
	spin_lock_bh(&rpc_queue_lock);

	/* Delete any running timer */
	__rpc_del_timer(task);

	/* Remove from any wait queue we're still on */
	__rpc_remove_wait_queue(task);

	task->tk_active = 0;

	spin_unlock_bh(&rpc_queue_lock);

	/* Release resources */
	if (task->tk_rqstp)
		xprt_release(task);
	if (task->tk_msg.rpc_cred)
		rpcauth_unbindcred(task);
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

	spin_lock_bh(&rpc_queue_lock);
	if ((parent = rpc_find_parent(child)) != NULL) {
		parent->tk_status = child->tk_status;
		__rpc_wake_up(parent);
	}
	spin_unlock_bh(&rpc_queue_lock);
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
	spin_lock_bh(&rpc_queue_lock);
	/* N.B. Is it possible for the child to have already finished? */
	__rpc_sleep_on(&childq, task, func, NULL);
	rpc_schedule_run(child);
	spin_unlock_bh(&rpc_queue_lock);
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

	/*
	 * Spin lock all_tasks to prevent changes...
	 */
	spin_lock(&rpc_sched_lock);
	for (q = &all_tasks; (rovr = *q); q = &rovr->tk_next_task) {
		if (!clnt || rovr->tk_client == clnt) {
			rovr->tk_flags |= RPC_TASK_KILLED;
			rpc_exit(rovr, -EIO);
			rpc_wake_up_task(rovr);
		}
	}
	spin_unlock(&rpc_sched_lock);
}

static DECLARE_MUTEX_LOCKED(rpciod_running);

static inline int
rpciod_task_pending(void)
{
	return schedq.task != NULL || xprt_tcp_pending();
}


/*
 * This is the rpciod kernel thread
 */
static int
rpciod(void *ptr)
{
	wait_queue_head_t *assassin = (wait_queue_head_t*) ptr;
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

		if (!rpciod_task_pending()) {
			dprintk("RPC: rpciod back to sleep\n");
			wait_event_interruptible(rpciod_idle, rpciod_task_pending());
			dprintk("RPC: switch to rpciod\n");
			rounds = 0;
		}
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
void rpc_show_tasks(void)
{
	struct rpc_task *t = all_tasks, *next;

	spin_lock(&rpc_sched_lock);
	t = all_tasks;
	if (!t) {
		spin_unlock(&rpc_sched_lock);
		return;
	}
	printk("-pid- proc flgs status -client- -prog- --rqstp- -timeout "
		"-rpcwait -action- --exit--\n");
	for (; t; t = next) {
		next = t->tk_next_task;
		printk("%05d %04d %04x %06d %8p %6d %8p %08ld %8s %8p %8p\n",
			t->tk_pid, t->tk_msg.rpc_proc, t->tk_flags, t->tk_status,
			t->tk_client, t->tk_client->cl_prog,
			t->tk_rqstp, t->tk_timeout,
			t->tk_rpcwait ? rpc_qname(t->tk_rpcwait) : " <NULL> ",
			t->tk_action, t->tk_exit);
	}
	spin_unlock(&rpc_sched_lock);
}
#endif

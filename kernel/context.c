/*
 * linux/kernel/context.c
 *
 * Mechanism for running arbitrary tasks in process context
 *
 * dwmw2@redhat.com
 */

#define __KERNEL_SYSCALLS__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/signal.h>

static DECLARE_TASK_QUEUE(tq_context);
static DECLARE_WAIT_QUEUE_HEAD(context_task_wq);
static int keventd_running;

void schedule_task(struct tq_struct *task)
{
	if (keventd_running == 0)
		printk(KERN_ERR "schedule_task(): keventd has not started\n");
	queue_task(task, &tq_context);
	wake_up(&context_task_wq);
}

EXPORT_SYMBOL(schedule_task);

static int context_thread(void *dummy)
{
	struct task_struct *curtask = current;
	DECLARE_WAITQUEUE(wait, curtask);
	struct k_sigaction sa;

	daemonize();
	strcpy(curtask->comm, "keventd");
	keventd_running = 1;

	spin_lock_irq(&curtask->sigmask_lock);
	siginitsetinv(&curtask->blocked, sigmask(SIGCHLD));
	recalc_sigpending(curtask);
	spin_unlock_irq(&curtask->sigmask_lock);

	/* Install a handler so SIGCLD is delivered */
	sa.sa.sa_handler = SIG_IGN;
	sa.sa.sa_flags = 0;
	siginitset(&sa.sa.sa_mask, sigmask(SIGCHLD));
	do_sigaction(SIGCHLD, &sa, (struct k_sigaction *)0);

	for (;;) {
		__set_task_state(curtask, TASK_INTERRUPTIBLE);
		add_wait_queue(&context_task_wq, &wait);

		/*
		 * Careful: we depend on the wait-queue modifications
		 * to also act as memory barriers.
		 */
		if (!tq_context)
			schedule();

		remove_wait_queue(&context_task_wq, &wait);
		__set_task_state(curtask, TASK_RUNNING);
		run_task_queue(&tq_context);
		if (signal_pending(curtask)) {
			while (waitpid(-1, (unsigned int *)0, __WALL|WNOHANG) > 0)
				;
			flush_signals(curtask);
			recalc_sigpending(curtask);
		}
	}
}

int start_context_thread(void)
{
	kernel_thread(context_thread, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	return 0;
}

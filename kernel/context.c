/*
 * linux/kernel/context.c
 *
 * Mechanism for running arbitrary tasks in process context
 *
 * dwmw2@redhat.com
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>

static DECLARE_TASK_QUEUE(tq_context);
static DECLARE_WAIT_QUEUE_HEAD(context_task_wq);

void schedule_task(struct tq_struct *task)
{
	queue_task(task, &tq_context);
	wake_up(&context_task_wq);
}

EXPORT_SYMBOL(schedule_task);

static int context_thread(void *dummy)
{
	DECLARE_WAITQUEUE(wait, current);

	daemonize();
	strcpy(current->comm, "eventd");

        spin_lock_irq(&current->sigmask_lock);
        sigfillset(&current->blocked);
        recalc_sigpending(current);
        spin_unlock_irq(&current->sigmask_lock);

	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&context_task_wq, &wait);

		/*
		 * Careful: we depend on the wait-queue modifications
		 * to also act as memory barriers.
		 */
		if (!tq_context)
			schedule();

		remove_wait_queue(&context_task_wq, &wait);
		current->state = TASK_RUNNING;
		run_task_queue(&tq_context);
	}
}

static int __init start_context_thread(void)
{
	kernel_thread(context_thread, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	return 0;
}

module_init(start_context_thread);

#ifndef _LINUX_PROFILE_H
#define _LINUX_PROFILE_H

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <asm/errno.h>

#define CPU_PROFILING	1
#define SCHED_PROFILING	2

struct proc_dir_entry;
struct pt_regs;

/* init basic kernel profiler */
void __init profile_init(void);
void profile_tick(int, struct pt_regs *);
void profile_hit(int, void *);
#ifdef CONFIG_PROC_FS
void create_prof_cpu_mask(struct proc_dir_entry *);
#else
#define create_prof_cpu_mask(x)			do { (void)(x); } while (0)
#endif

enum profile_type {
	PROFILE_TASK_EXIT,
	PROFILE_MUNMAP
};

#ifdef CONFIG_PROFILING

struct notifier_block;
struct task_struct;
struct mm_struct;

/* task is in do_exit() */
void profile_task_exit(struct task_struct * task);

/* task is dead, free task struct ? Returns 1 if
 * the task was taken, 0 if the task should be freed.
 */
int profile_handoff_task(struct task_struct * task);

/* sys_munmap */
void profile_munmap(unsigned long addr);

int task_handoff_register(struct notifier_block * n);
int task_handoff_unregister(struct notifier_block * n);

int profile_event_register(enum profile_type, struct notifier_block * n);
int profile_event_unregister(enum profile_type, struct notifier_block * n);

int register_profile_notifier(struct notifier_block * nb);
int unregister_profile_notifier(struct notifier_block * nb);

struct pt_regs;

/* profiling hook activated on each timer interrupt */
void profile_hook(struct pt_regs * regs);

#else

static inline int task_handoff_register(struct notifier_block * n)
{
	return -ENOSYS;
}

static inline int task_handoff_unregister(struct notifier_block * n)
{
	return -ENOSYS;
}

static inline int profile_event_register(enum profile_type t, struct notifier_block * n)
{
	return -ENOSYS;
}

static inline int profile_event_unregister(enum profile_type t, struct notifier_block * n)
{
	return -ENOSYS;
}

#define profile_task_exit(a) do { } while (0)
#define profile_handoff_task(a) (0)
#define profile_munmap(a) do { } while (0)

static inline int register_profile_notifier(struct notifier_block * nb)
{
	return -ENOSYS;
}

static inline int unregister_profile_notifier(struct notifier_block * nb)
{
	return -ENOSYS;
}

#define profile_hook(regs) do { } while (0)

#endif /* CONFIG_PROFILING */

#endif /* __KERNEL__ */

#endif /* _LINUX_PROFILE_H */

extern void __ptrace_cancel_bpt(struct task_struct *);
extern int ptrace_set_bpt(struct task_struct *);

/*
 * Clear a breakpoint, if one exists.
 */
static inline int ptrace_cancel_bpt(struct task_struct *tsk)
{
	int nsaved = tsk->thread.debug.nsaved;

	if (nsaved)
		__ptrace_cancel_bpt(tsk);

	return nsaved;
}


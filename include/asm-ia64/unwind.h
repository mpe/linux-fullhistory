#ifndef _ASM_IA64_UNWIND_H
#define _ASM_IA64_UNWIND_H

/*
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * A simple API for unwinding kernel stacks.  This is used for
 * debugging and error reporting purposes.  The kernel doesn't need
 * full-blown stack unwinding with all the bells and whitles, so there
 * is not much point in implementing the full IA-64 unwind API (though
 * it would of course be possible to implement the kernel API on top
 * of it).
 */

struct task_struct;	/* forward declaration */
struct switch_stack;	/* forward declaration */

/*
 * The following declarations are private to the unwind
 * implementation:
 */

struct ia64_stack {
	unsigned long *limit;
	unsigned long *top;
};

/*
 * No user of this module should every access this structure directly
 * as it is subject to change.  It is declared here solely so we can
 * use automatic variables.
 */
struct ia64_frame_info {
	struct ia64_stack regstk;
	unsigned long *bsp;
	unsigned long top_rnat;		/* RSE NaT collection at top of backing store */
	unsigned long cfm;
	unsigned long ip;		/* instruction pointer */
};

/*
 * The official API follows below:
 */

/*
 * Prepare to unwind blocked task t.
 */
extern void ia64_unwind_init_from_blocked_task (struct ia64_frame_info *info,
						struct task_struct *t);

/*
 * Prepare to unwind the current task.  For this to work, the kernel
 * stack identified by REGS must look like this:
 *
 *	//		      //
 *	|		      |
 *	|   kernel stack      |
 *	|		      |
 *	+=====================+
 *	|   struct pt_regs    |
 *	+---------------------+ <--- REGS
 *	| struct switch_stack |
 *	+---------------------+
 */
extern void ia64_unwind_init_from_current (struct ia64_frame_info *info, struct pt_regs *regs);

/*
 * Unwind to previous to frame.  Returns 0 if successful, negative
 * number in case of an error.
 */
extern int ia64_unwind_to_previous_frame (struct ia64_frame_info *info);

#define ia64_unwind_get_ip(info)	((info)->ip)
#define ia64_unwind_get_bsp(info)	((unsigned long) (info)->bsp)

#endif /* _ASM_IA64_UNWIND_H */

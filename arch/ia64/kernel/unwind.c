/*
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/unwind.h>

void
ia64_unwind_init_from_blocked_task (struct ia64_frame_info *info, struct task_struct *t)
{
	struct switch_stack *sw = (struct switch_stack *) (t->thread.ksp + 16);
	unsigned long sol, limit, top;

	memset(info, 0, sizeof(*info));

	sol = (sw->ar_pfs >> 7) & 0x7f;	/* size of locals */

	limit = (unsigned long) t + IA64_RBS_OFFSET;
	top   = sw->ar_bspstore;
	if (top - (unsigned long) t >= IA64_STK_OFFSET)
		top = limit;

	info->regstk.limit = (unsigned long *) limit;
	info->regstk.top   = (unsigned long *) top;
	info->bsp	   = ia64_rse_skip_regs(info->regstk.top, -sol);
	info->top_rnat	   = sw->ar_rnat;
	info->cfm	   = sw->ar_pfs;
	info->ip	   = sw->b0;
}

void
ia64_unwind_init_from_current (struct ia64_frame_info *info, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *) regs - 1;
	unsigned long sol, sof, *bsp, limit, top;

	limit = (unsigned long) current + IA64_RBS_OFFSET;
	top   = sw->ar_bspstore;
	if (top - (unsigned long) current >= IA64_STK_OFFSET)
		top = limit;

	memset(info, 0, sizeof(*info));

	sol = (sw->ar_pfs >> 7) & 0x7f;	/* size of frame */
	info->regstk.limit = (unsigned long *) limit;
	info->regstk.top   = (unsigned long *) top;
	info->top_rnat	   = sw->ar_rnat;

	/* this gives us the bsp top level frame (kdb interrupt frame): */
	bsp = ia64_rse_skip_regs((unsigned long *) top, -sol);

	/* now skip past the interrupt frame: */
	sof = regs->cr_ifs & 0x7f;	/* size of frame */
	info->cfm = regs->cr_ifs;
	info->bsp = ia64_rse_skip_regs(bsp, -sof);
	info->ip  = regs->cr_iip;
}

static unsigned long
read_reg (struct ia64_frame_info *info, int regnum, int *is_nat)
{
	unsigned long *addr, *rnat_addr, rnat;

	addr = ia64_rse_skip_regs(info->bsp, regnum);
	if (addr < info->regstk.limit || addr >= info->regstk.top || ((long) addr & 0x7) != 0) {
		*is_nat = 1;
		return 0xdeadbeefdeadbeef;
	}
	rnat_addr = ia64_rse_rnat_addr(addr);

	if (rnat_addr >= info->regstk.top)
		rnat = info->top_rnat;
	else
		rnat = *rnat_addr;
	*is_nat = (rnat & (1UL << ia64_rse_slot_num(addr))) != 0;
	return *addr;
}

/*
 * On entry, info->regstk.top should point to the register backing
 * store for r32.
 */
int
ia64_unwind_to_previous_frame (struct ia64_frame_info *info)
{
	unsigned long sol, cfm = info->cfm;
	int is_nat;

	sol = (cfm >> 7) & 0x7f;	/* size of locals */

	/*
	 * In general, we would have to make use of unwind info to
	 * unwind an IA-64 stack, but for now gcc uses a special
	 * convention that makes this possible without full-fledged
	 * unwindo info.  Specifically, we expect "rp" in the second
	 * last, and "ar.pfs" in the last local register, so the
	 * number of locals in a frame must be at least two.  If it's
	 * less than that, we reached the end of the C call stack.
	 */
	if (sol < 2)
		return -1;

	info->ip = read_reg(info, sol - 2, &is_nat);
	if (is_nat)
		return -1;

	cfm = read_reg(info, sol - 1, &is_nat);
	if (is_nat)
		return -1;

	sol = (cfm >> 7) & 0x7f;

	info->cfm = cfm;
	info->bsp = ia64_rse_skip_regs(info->bsp, -sol);
	return 0;
}

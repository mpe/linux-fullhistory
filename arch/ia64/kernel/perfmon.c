/*
 * This file contains the code to configure and read/write the ia64 performance
 * monitoring stuff.
 *
 * Originaly Written by Ganesh Venkitachalam, IBM Corp.
 * Modifications by David Mosberger-Tang, Hewlett-Packard Co.
 * Copyright (C) 1999 Ganesh Venkitachalam <venkitac@us.ibm.com>
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>

#include <asm/errno.h>
#include <asm/hw_irq.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>

/* Long blurb on how this works: 
 * We set dcr.pp, psr.pp, and the appropriate pmc control values with
 * this.  Notice that we go about modifying _each_ task's pt_regs to
 * set cr_ipsr.pp.  This will start counting when "current" does an
 * _rfi_. Also, since each task's cr_ipsr.pp, and cr_ipsr is inherited
 * across forks, we do _not_ need additional code on context
 * switches. On stopping of the counters we dont need to go about
 * changing every task's cr_ipsr back to where it wuz, because we can
 * just set pmc[0]=1. But we do it anyways becuase we will probably
 * add thread specific accounting later.
 *
 * The obvious problem with this is that on SMP systems, it is a bit
 * of work (when someone wants to do it:-)) - it would be easier if we
 * just added code to the context-switch path, but if we wanted to support
 * per-thread accounting, the context-switch path might be long unless 
 * we introduce a flag in the task_struct. Right now, the following code 
 * will NOT work correctly on MP (for more than one reason:-)).
 *
 * The short answer is that to make this work on SMP,  we would need 
 * to lock the run queue to ensure no context switches, send 
 * an IPI to each processor, and in that IPI handler, set processor regs,
 * and just modify the psr bit of only the _current_ thread, since we have 
 * modified the psr bit correctly in the kernel stack for every process 
 * which is not running. Also, we need pmd arrays per-processor, and 
 * the READ_PMD command will need to get values off of other processors. 
 * IPIs are the answer, irrespective of what the question is. Might 
 * crash on SMP systems without the lock_kernel().
 */

#ifdef CONFIG_PERFMON

#define MAX_PERF_COUNTER	4	/* true for Itanium, at least */
#define WRITE_PMCS_AND_START	0xa0
#define WRITE_PMCS		0xa1
#define READ_PMDS		0xa2
#define STOP_PMCS		0xa3
#define IA64_COUNTER_MASK	0xffffffffffffff6fL
#define PERF_OVFL_VAL		0xffffffffL

volatile int used_by_system;

struct perfmon_counter {
        unsigned long data;
        unsigned long counter_num;
};

unsigned long pmds[NR_CPUS][MAX_PERF_COUNTER];

asmlinkage unsigned long
sys_perfmonctl (int cmd1, int cmd2, void *ptr)
{
        struct perfmon_counter tmp, *cptr = ptr;
        unsigned long cnum, dcr, flags;
        struct perf_counter;
        int i;

        switch (cmd1) {
	      case WRITE_PMCS:           /* Writes to PMC's and clears PMDs */
	      case WRITE_PMCS_AND_START: /* Also starts counting */

		if (cmd2 <= 0 || cmd2 > MAX_PERF_COUNTER - used_by_system)
			return -EINVAL;

		if (!access_ok(VERIFY_READ, cptr, sizeof(struct perf_counter)*cmd2))
			return -EFAULT;

		current->thread.flags |= IA64_THREAD_PM_VALID;

		for (i = 0; i < cmd2; i++, cptr++) {
			copy_from_user(&tmp, cptr, sizeof(tmp));
			/* XXX need to check validity of counter_num and perhaps data!! */
			if (tmp.counter_num < 4
			    || tmp.counter_num >= 4 + MAX_PERF_COUNTER - used_by_system)
				return -EFAULT;

			ia64_set_pmc(tmp.counter_num, tmp.data);
			ia64_set_pmd(tmp.counter_num, 0);
			pmds[smp_processor_id()][tmp.counter_num - 4] = 0;
		}

		if (cmd1 == WRITE_PMCS_AND_START) {
			local_irq_save(flags);
			dcr = ia64_get_dcr();
			dcr |= IA64_DCR_PP;
			ia64_set_dcr(dcr);
			local_irq_restore(flags);
			ia64_set_pmc(0, 0);
		}
                break;

	      case READ_PMDS:
		if (cmd2 <= 0 || cmd2 > MAX_PERF_COUNTER - used_by_system)
			return -EINVAL;
		if (!access_ok(VERIFY_WRITE, cptr, sizeof(struct perf_counter)*cmd2))
			return -EFAULT;

		/* This looks shady, but IMHO this will work fine. This is  
		 * the sequence that I could come up with to avoid races
		 * with the interrupt handler. See explanation in the 
		 * following comment.
		 */

		local_irq_save(flags);
		__asm__ __volatile__("rsm psr.pp\n");
		dcr = ia64_get_dcr();
		dcr &= ~IA64_DCR_PP;
		ia64_set_dcr(dcr);
		local_irq_restore(flags);

		/*
		 * We cannot write to pmc[0] to stop counting here, as
		 * that particular instruction might cause an overflow
		 * and the mask in pmc[0] might get lost. I'm _not_ 
		 * sure of the hardware behavior here. So we stop
		 * counting by psr.pp = 0. And we reset dcr.pp to
		 * prevent an interrupt from mucking up psr.pp in the
		 * meanwhile. Perfmon interrupts are pended, hence the
		 * above code should be ok if one of the above instructions 
		 * caused overflows, i.e the interrupt should get serviced
		 * when we re-enabled interrupts. When I muck with dcr, 
		 * is the irq_save/restore needed?
		 */
		for (i = 0, cnum = 4;i < cmd2; i++, cnum++, cptr++) {
			tmp.data = (pmds[smp_processor_id()][i]
				    + (ia64_get_pmd(cnum) & PERF_OVFL_VAL));
			tmp.counter_num = cnum;
			if (copy_to_user(cptr, &tmp, sizeof(tmp)))
				return -EFAULT;
			//put_user(pmd, &cptr->data);
		}
		local_irq_save(flags);
		__asm__ __volatile__("ssm psr.pp");
		dcr = ia64_get_dcr();
		dcr |= IA64_DCR_PP;
		ia64_set_dcr(dcr);
		local_irq_restore(flags);
                break;

	      case STOP_PMCS:
		ia64_set_pmc(0, 1);
		ia64_srlz_d();
		for (i = 0; i < MAX_PERF_COUNTER - used_by_system; ++i)
			ia64_set_pmc(4+i, 0);

		if (!used_by_system) {
			local_irq_save(flags);
			dcr = ia64_get_dcr();
			dcr &= ~IA64_DCR_PP;
			ia64_set_dcr(dcr);
			local_irq_restore(flags);
		}
		current->thread.flags &= ~(IA64_THREAD_PM_VALID);
		break;

	      default:
		return -EINVAL;
		break;
        }
        return 0;
}

static inline void
update_counters (void)
{
	unsigned long mask, i, cnum, val;

	mask = ia64_get_pmc(0) >> 4;
	for (i = 0, cnum = 4; i < MAX_PERF_COUNTER - used_by_system; cnum++, i++, mask >>= 1) {
		val = 0;
		if (mask & 0x1) 
			val += PERF_OVFL_VAL + 1;
		/* since we got an interrupt, might as well clear every pmd. */
		val += ia64_get_pmd(cnum) & PERF_OVFL_VAL;
		pmds[smp_processor_id()][i] += val;
		ia64_set_pmd(cnum, 0);
	}
}

static void
perfmon_interrupt (int irq, void *arg, struct pt_regs *regs)
{
	update_counters();
	ia64_set_pmc(0, 0);
	ia64_srlz_d();
}

static struct irqaction perfmon_irqaction = {
	handler:	perfmon_interrupt,
	flags:		SA_INTERRUPT,
	name:		"perfmon"
};

void
perfmon_init (void)
{
	irq_desc[PERFMON_IRQ].status |= IRQ_PER_CPU;
	irq_desc[PERFMON_IRQ].handler = &irq_type_ia64_sapic;
	setup_irq(PERFMON_IRQ, &perfmon_irqaction);

	ia64_set_pmv(PERFMON_IRQ);
	ia64_srlz_d();
	printk("Initialized perfmon vector to %u\n",PERFMON_IRQ);
}

void
perfmon_init_percpu (void)
{
	ia64_set_pmv(PERFMON_IRQ);
	ia64_srlz_d();
}

void
ia64_save_pm_regs (struct thread_struct *t)
{
	int i;

	ia64_set_pmc(0, 1);
	ia64_srlz_d();
	for (i=0; i< IA64_NUM_PM_REGS - used_by_system ; i++) {
		t->pmd[i] = ia64_get_pmd(4+i);
		t->pmod[i] = pmds[smp_processor_id()][i];
		t->pmc[i] = ia64_get_pmc(4+i);
	}
}

void
ia64_load_pm_regs (struct thread_struct *t)
{
	int i;

	for (i=0; i< IA64_NUM_PM_REGS - used_by_system ; i++) {
		ia64_set_pmd(4+i, t->pmd[i]);
		pmds[smp_processor_id()][i] = t->pmod[i];
		ia64_set_pmc(4+i, t->pmc[i]);
	}
	ia64_set_pmc(0, 0);
	ia64_srlz_d();
}

#else /* !CONFIG_PERFMON */

asmlinkage unsigned long
sys_perfmonctl (int cmd1, int cmd2, void *ptr)
{
	return -ENOSYS;
}

#endif /* !CONFIG_PERFMON */

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
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>

#include <asm/errno.h>
#include <asm/hw_irq.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pal.h>

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
#define PMU_FIRST_COUNTER	4	/* first generic counter */

#define WRITE_PMCS_AND_START	0xa0
#define WRITE_PMCS		0xa1
#define READ_PMDS		0xa2
#define STOP_PMCS		0xa3


/*
 * this structure needs to be enhanced
 */
typedef struct {
	unsigned long pmu_reg_data;	/* generic PMD register */
	unsigned long pmu_reg_num;	/* which register number */
} perfmon_reg_t; 

/*
 * This structure is initialize at boot time and contains
 * a description of the PMU main characteristic as indicated
 * by PAL
 */
typedef struct {
	unsigned long perf_ovfl_val;	/* overflow value for generic counters */
	unsigned long max_pmc;		/* highest PMC */
	unsigned long max_pmd;		/* highest PMD */
	unsigned long max_counters;	/* number of generic counter pairs (PMC/PMD) */
} pmu_config_t;

/* XXX will go static when ptrace() is cleaned */
unsigned long perf_ovfl_val;	/* overflow value for generic counters */

static pmu_config_t pmu_conf;

/*
 * could optimize to avoid cache conflicts in SMP
 */
unsigned long pmds[NR_CPUS][MAX_PERF_COUNTER];

asmlinkage unsigned long
sys_perfmonctl (int cmd, int count, void *ptr, long arg4, long arg5, long arg6, long arg7, long arg8, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
        perfmon_reg_t tmp, *cptr = ptr;
        unsigned long cnum;
        int i;

        switch (cmd) {
	      case WRITE_PMCS:           /* Writes to PMC's and clears PMDs */
	      case WRITE_PMCS_AND_START: /* Also starts counting */

		if (!access_ok(VERIFY_READ, cptr, sizeof(struct perfmon_reg_t)*count))
			return -EFAULT;

		for (i = 0; i < count; i++, cptr++) {

			copy_from_user(&tmp, cptr, sizeof(tmp));

			/* XXX need to check validity of pmu_reg_num and perhaps data!! */

			if (tmp.pmu_reg_num > pmu_conf.max_pmc || tmp.pmu_reg_num == 0) return -EFAULT;

			ia64_set_pmc(tmp.pmu_reg_num, tmp.pmu_reg_data);

			/* to go away */
			if (tmp.pmu_reg_num >= PMU_FIRST_COUNTER && tmp.pmu_reg_num < PMU_FIRST_COUNTER+pmu_conf.max_counters) {
				ia64_set_pmd(tmp.pmu_reg_num, 0);
				pmds[smp_processor_id()][tmp.pmu_reg_num - PMU_FIRST_COUNTER] = 0;

				printk(__FUNCTION__" setting PMC/PMD[%ld] es=0x%lx pmd[%ld]=%lx\n", tmp.pmu_reg_num, (tmp.pmu_reg_data>>8) & 0x7f, tmp.pmu_reg_num, ia64_get_pmd(tmp.pmu_reg_num));
			} else
				printk(__FUNCTION__" setting PMC[%ld]=0x%lx\n", tmp.pmu_reg_num, tmp.pmu_reg_data);
		}

		if (cmd == WRITE_PMCS_AND_START) {
#if 0
/* irrelevant with user monitors */
			local_irq_save(flags);

			dcr = ia64_get_dcr();
			dcr |= IA64_DCR_PP;
			ia64_set_dcr(dcr);

			local_irq_restore(flags);
#endif

			ia64_set_pmc(0, 0);

			/* will start monitoring right after rfi */
			ia64_psr(regs)->up = 1;
		}
		/* 
		 * mark the state as valid.
		 * this will trigger save/restore at context switch
		 */
		current->thread.flags |= IA64_THREAD_PM_VALID;
                break;

	      case READ_PMDS:
		if (count <= 0 || count > MAX_PERF_COUNTER)
			return -EINVAL;
		if (!access_ok(VERIFY_WRITE, cptr, sizeof(struct perfmon_reg_t)*count))
			return -EFAULT;

		/* This looks shady, but IMHO this will work fine. This is  
		 * the sequence that I could come up with to avoid races
		 * with the interrupt handler. See explanation in the 
		 * following comment.
		 */
#if 0
/* irrelevant with user monitors */
		local_irq_save(flags);
		__asm__ __volatile__("rsm psr.pp\n");
		dcr = ia64_get_dcr();
		dcr &= ~IA64_DCR_PP;
		ia64_set_dcr(dcr);
		local_irq_restore(flags);
#endif
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


		/* XXX: This needs to change to read more than just the counters */
		for (i = 0, cnum = PMU_FIRST_COUNTER;i < count; i++, cnum++, cptr++) {

			tmp.pmu_reg_data = (pmds[smp_processor_id()][i]
				    + (ia64_get_pmd(cnum) & pmu_conf.perf_ovfl_val));

			tmp.pmu_reg_num = cnum;

			if (copy_to_user(cptr, &tmp, sizeof(tmp))) return -EFAULT;
		}
#if 0
/* irrelevant with user monitors */
		local_irq_save(flags);
		__asm__ __volatile__("ssm psr.pp");
		dcr = ia64_get_dcr();
		dcr |= IA64_DCR_PP;
		ia64_set_dcr(dcr);
		local_irq_restore(flags);
#endif
                break;

	      case STOP_PMCS:
		ia64_set_pmc(0, 1);
		ia64_srlz_d();
		for (i = 0; i < MAX_PERF_COUNTER; ++i)
			ia64_set_pmc(4+i, 0);

#if 0
/* irrelevant with user monitors */
		local_irq_save(flags);
		dcr = ia64_get_dcr();
		dcr &= ~IA64_DCR_PP;
		ia64_set_dcr(dcr);
		local_irq_restore(flags);
		ia64_psr(regs)->up = 0;
#endif

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
	for (i = 0, cnum = PMU_FIRST_COUNTER ; i < pmu_conf.max_counters; cnum++, i++, mask >>= 1) {


		val = mask & 0x1 ? pmu_conf.perf_ovfl_val + 1 : 0;

		if (mask & 0x1) 
			printk(__FUNCTION__ " PMD%ld overflowed pmd=%lx pmod=%lx\n", cnum, ia64_get_pmd(cnum), pmds[smp_processor_id()][i]); 

		/* since we got an interrupt, might as well clear every pmd. */
		val += ia64_get_pmd(cnum) & pmu_conf.perf_ovfl_val;

		printk(__FUNCTION__ " adding val=%lx to pmod[%ld]=%lx \n", val, i, pmds[smp_processor_id()][i]); 

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

static int
perfmon_proc_info(char *page)
{
	char *p = page;
	u64 pmc0 = ia64_get_pmc(0);

	p += sprintf(p, "PMC[0]=%lx\n", pmc0);

	return p - page;
}

static int
perfmon_read_entry(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = perfmon_proc_info(page);

        if (len <= off+count) *eof = 1;

        *start = page + off;
        len   -= off;

        if (len>count) len = count;
        if (len<0) len = 0;

        return len;
}

static struct proc_dir_entry *perfmon_dir;

void __init
perfmon_init (void)
{
	pal_perf_mon_info_u_t pm_info;
	u64 pm_buffer[16];
	s64 status;
	
	irq_desc[PERFMON_IRQ].status |= IRQ_PER_CPU;
	irq_desc[PERFMON_IRQ].handler = &irq_type_ia64_sapic;
	setup_irq(PERFMON_IRQ, &perfmon_irqaction);

	ia64_set_pmv(PERFMON_IRQ);
	ia64_srlz_d();

	printk("perfmon: Initialized vector to %u\n",PERFMON_IRQ);

	if ((status=ia64_pal_perf_mon_info(pm_buffer, &pm_info)) != 0) {
		printk(__FUNCTION__ " pal call failed (%ld)\n", status);
		return;
	} 
	pmu_conf.perf_ovfl_val = perf_ovfl_val = (1L << pm_info.pal_perf_mon_info_s.width) - 1; 

	/* XXX need to use PAL instead */
	pmu_conf.max_pmc       = 13;
	pmu_conf.max_pmd       = 17;
	pmu_conf.max_counters  = pm_info.pal_perf_mon_info_s.generic;

	printk("perfmon: Counters are %d bits\n", pm_info.pal_perf_mon_info_s.width);
	printk("perfmon: Maximum counter value 0x%lx\n", pmu_conf.perf_ovfl_val);

	/*
	 * for now here for debug purposes
	 */
	perfmon_dir = create_proc_read_entry ("perfmon", 0, 0, perfmon_read_entry, NULL);
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
	/*
	 * XXX: this will need to be extended beyong just counters
	 */
	for (i=0; i< IA64_NUM_PM_REGS; i++) {
		t->pmd[i]  = ia64_get_pmd(4+i);
		t->pmod[i] = pmds[smp_processor_id()][i];
		t->pmc[i]  = ia64_get_pmc(4+i);
	}
}

void
ia64_load_pm_regs (struct thread_struct *t)
{
	int i;

	/*
	 * XXX: this will need to be extended beyong just counters 
	 */
	for (i=0; i< IA64_NUM_PM_REGS ; i++) {
		ia64_set_pmd(4+i, t->pmd[i]);
		pmds[smp_processor_id()][i] = t->pmod[i];
		ia64_set_pmc(4+i, t->pmc[i]);
	}
	ia64_set_pmc(0, 0);
	ia64_srlz_d();
}

#else /* !CONFIG_PERFMON */

asmlinkage unsigned long
sys_perfmonctl (int cmd, int count, void *ptr)
{
	return -ENOSYS;
}

#endif /* !CONFIG_PERFMON */

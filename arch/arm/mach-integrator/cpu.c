/*
 *  linux/arch/arm/mach-integrator/cpu.c
 *
 *  Copyright (C) 2001-2002 Deep Blue Solutions Ltd.
 *
 *  $Id: cpu.c,v 1.6 2002/07/18 13:58:51 rmk Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * CPU support functions
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>

static struct cpufreq_driver integrator_driver;

#define CM_ID  	(IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_ID_OFFSET)
#define CM_OSC	(IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_OSC_OFFSET)
#define CM_STAT (IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_STAT_OFFSET)
#define CM_LOCK (IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_LOCK_OFFSET)

struct vco {
	unsigned char vdw;
	unsigned char od;
};

/*
 * Divisors for each OD setting.
 */
static unsigned char cc_divisor[8] = { 10, 2, 8, 4, 5, 7, 9, 6 };

static unsigned int vco_to_freq(struct vco vco, int factor)
{
	return 2000 * (vco.vdw + 8) / cc_divisor[vco.od] / factor;
}

/*
 * Divisor indexes in ascending divisor order
 */
static unsigned char s2od[] = { 1, 3, 4, 7, 5, 2, 6, 0 };

static struct vco freq_to_vco(unsigned int freq_khz, int factor)
{
	struct vco vco = {0, 0};
	unsigned int i, f;

	freq_khz *= factor;

	for (i = 0; i < 8; i++) {
		f = freq_khz * cc_divisor[s2od[i]];
		/* f must be between 10MHz and 320MHz */
		if (f > 10000 && f <= 320000)
			break;
	}

	vco.od  = s2od[i];
	vco.vdw = f / 2000 - 8;

	return vco;
}


/*
 * Validate the speed policy.
 */
static int integrator_verify_policy(struct cpufreq_policy *policy)
{
	struct vco vco;

	cpufreq_verify_within_limits(policy, 
				     policy->cpuinfo.min_freq, 
				     policy->cpuinfo.max_freq);

	vco = freq_to_vco(policy->max, 1);

	if (vco.vdw < 4)
		vco.vdw = 4;
	if (vco.vdw > 152)
		vco.vdw = 152;

	policy->max = vco_to_freq(vco, 1);

	vco = freq_to_vco(policy->min, 1);

	if (vco.vdw < 4)
		vco.vdw = 4;
	if (vco.vdw > 152)
		vco.vdw = 152;

	policy->min = vco_to_freq(vco, 1);

	cpufreq_verify_within_limits(policy, 
				     policy->cpuinfo.min_freq, 
				     policy->cpuinfo.max_freq);

	return 0;
}


static int integrator_set_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation)
{
	unsigned long cpus_allowed;
	int cpu = policy->cpu;
	struct vco vco;
	struct cpufreq_freqs freqs;
	u_int cm_osc;

	/*
	 * Save this threads cpus_allowed mask.
	 */
	cpus_allowed = current->cpus_allowed;

	/*
	 * Bind to the specified CPU.  When this call returns,
	 * we should be running on the right CPU.
	 */
	set_cpus_allowed(current, 1 << cpu);
	BUG_ON(cpu != smp_processor_id());

	/* get current setting */
	cm_osc = __raw_readl(CM_OSC);
	vco.od = (cm_osc >> 8) & 7;
	vco.vdw = cm_osc & 255;
	freqs.old = vco_to_freq(vco, 1);

	/* freq_to_vco rounds down -- so we need the next larger freq in
	 * case of CPUFREQ_RELATION_L.
	 */
	if (relation == CPUFREQ_RELATION_L)
		target_freq += 1999;
	if (target_freq > policy->max)
		target_freq = policy->max;
	vco = freq_to_vco(target_freq, 1);
	freqs.new = vco_to_freq(vco, 1);

	freqs.cpu = policy->cpu;

	if (freqs.old == freqs.new) {
		set_cpus_allowed(current, cpus_allowed);
		return 0;
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	cm_osc = __raw_readl(CM_OSC);
	cm_osc &= 0xfffff800;
	cm_osc |= vco.vdw | vco.od << 8;

	__raw_writel(0xa05f, CM_LOCK);
	__raw_writel(cm_osc, CM_OSC);
	__raw_writel(0, CM_LOCK);

	/*
	 * Restore the CPUs allowed mask.
	 */
	set_cpus_allowed(current, cpus_allowed);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static int integrator_cpufreq_init(struct cpufreq *policy)
{
	unsigned long cus_allowed;
	unsigned int cpu = policy->cpu;
	u_int cm_osc, cm_stat, mem_freq_khz;
	struct vco vco;

	cpus_allowed = current->cpus_allowed;

	set_cpus_allowed(current, 1 << cpu);
	BUG_ON(cpu != smp_processor_id());

	/* detect memory etc. */
	cm_stat = __raw_readl(CM_STAT);
	cm_osc = __raw_readl(CM_OSC);
	vco.od  = (cm_osc >> 20) & 7;
	vco.vdw = (cm_osc >> 12) & 255;
	mem_freq_khz = vco_to_freq(vco, 2);

	printk(KERN_INFO "CPU%d: Module id: %d\n", cpu, cm_stat & 255);
	printk(KERN_INFO "CPU%d: Memory clock = %d.%03d MHz\n",
	       cpu, mem_freq_khz / 1000, mem_freq_khz % 1000);

	vco.od = (cm_osc >> 8) & 7;
	vco.vdw = cm_osc & 255;

	/* set default policy and cpuinfo */
	policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	policy->cpuinfo.max_freq = 160000;
	policy->cpuinfo.min_freq = 12000;
	policy->cpuinfo.transition_latency = 1000; /* 1 ms, assumed */
	policy->cur = policy->min = policy->max = vco_to_freq(vco, 1); /* current freq */

	set_cpus_allowed(current, cpus_allowed);

	return 0;
}

static struct cpufreq_driver integrator_driver = {
	.verify		= integrator_verify_policy,
	.target		= integrator_set_target,
	.init		= integrator_cpufreq_init,
	.name		= "integrator",
};

static int __init integrator_cpu_init(void)
{
	return cpufreq_register_driver(&integrator_driver);
}

static void __exit integrator_cpu_exit(void)
{
	cpufreq_unregister_driver(&integrator_driver);
}

MODULE_AUTHOR ("Russell M. King");
MODULE_DESCRIPTION ("cpufreq driver for ARM Integrator CPUs");
MODULE_LICENSE ("GPL");

module_init(integrator_cpu_init);
module_exit(integrator_cpu_exit);

/*
 *  drivers/cpufreq/cpufreq_ondemand.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/cpufreq.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/config.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define MIN_FREQUENCY_UP_THRESHOLD		(0)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)

#define DEF_FREQUENCY_DOWN_THRESHOLD		(20)
#define MIN_FREQUENCY_DOWN_THRESHOLD		(0)
#define MAX_FREQUENCY_DOWN_THRESHOLD		(100)

/* 
 * The polling frequency of this governor depends on the capability of 
 * the processor. Default polling frequency is 1000 times the transition
 * latency of the processor. The governor will work on any processor with 
 * transition latency <= 10mS, using appropriate sampling 
 * rate.
 * For CPUs with transition latency > 10mS (mostly drivers with CPUFREQ_ETERNAL)
 * this governor will not work.
 * All times here are in uS.
 */
static unsigned int 				def_sampling_rate;
#define MIN_SAMPLING_RATE			(def_sampling_rate / 2)
#define MAX_SAMPLING_RATE			(500 * def_sampling_rate)
#define DEF_SAMPLING_RATE_LATENCY_MULTIPLIER	(1000)
#define DEF_SAMPLING_DOWN_FACTOR		(10)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000)
#define sampling_rate_in_HZ(x)			(((x * HZ) < (1000 * 1000))?1:((x * HZ) / (1000 * 1000)))

static void do_dbs_timer(void *data);

struct cpu_dbs_info_s {
	struct cpufreq_policy 	*cur_policy;
	unsigned int 		prev_cpu_idle_up;
	unsigned int 		prev_cpu_idle_down;
	unsigned int 		enable;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, cpu_dbs_info);

static unsigned int dbs_enable;	/* number of CPUs using this policy */

static DECLARE_MUTEX 	(dbs_sem);
static DECLARE_WORK	(dbs_work, do_dbs_timer, NULL);

struct dbs_tuners {
	unsigned int 		sampling_rate;
	unsigned int		sampling_down_factor;
	unsigned int		up_threshold;
	unsigned int		down_threshold;
};

struct dbs_tuners dbs_tuners_ins = {
	.up_threshold 		= DEF_FREQUENCY_UP_THRESHOLD,
	.down_threshold 	= DEF_FREQUENCY_DOWN_THRESHOLD,
	.sampling_down_factor 	= DEF_SAMPLING_DOWN_FACTOR,
};

/************************** sysfs interface ************************/
static ssize_t show_sampling_rate_max(struct cpufreq_policy *policy, char *buf)
{
	return sprintf (buf, "%u\n", MAX_SAMPLING_RATE);
}

static ssize_t show_sampling_rate_min(struct cpufreq_policy *policy, char *buf)
{
	return sprintf (buf, "%u\n", MIN_SAMPLING_RATE);
}

#define define_one_ro(_name) 					\
static struct freq_attr _name = { 				\
	.attr = { .name = __stringify(_name), .mode = 0444 }, 	\
	.show = show_##_name, 					\
}

define_one_ro(sampling_rate_max);
define_one_ro(sampling_rate_min);

/* cpufreq_ondemand Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct cpufreq_policy *unused, char *buf)				\
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(sampling_down_factor, sampling_down_factor);
show_one(up_threshold, up_threshold);
show_one(down_threshold, down_threshold);

static ssize_t store_sampling_down_factor(struct cpufreq_policy *unused, 
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);
	down(&dbs_sem);
	if (ret != 1 )
		goto out;

	dbs_tuners_ins.sampling_down_factor = input;
out:
	up(&dbs_sem);
	return count;
}

static ssize_t store_sampling_rate(struct cpufreq_policy *unused, 
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);
	down(&dbs_sem);
	if (ret != 1 || input > MAX_SAMPLING_RATE || input < MIN_SAMPLING_RATE)
		goto out;

	dbs_tuners_ins.sampling_rate = input;
out:
	up(&dbs_sem);
	return count;
}

static ssize_t store_up_threshold(struct cpufreq_policy *unused, 
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);
	down(&dbs_sem);
	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD || 
			input < MIN_FREQUENCY_UP_THRESHOLD ||
			input <= dbs_tuners_ins.down_threshold)
		goto out;

	dbs_tuners_ins.up_threshold = input;
out:
	up(&dbs_sem);
	return count;
}

static ssize_t store_down_threshold(struct cpufreq_policy *unused, 
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);
	down(&dbs_sem);
	if (ret != 1 || input > MAX_FREQUENCY_DOWN_THRESHOLD || 
			input < MIN_FREQUENCY_DOWN_THRESHOLD ||
			input >= dbs_tuners_ins.up_threshold)
		goto out;

	dbs_tuners_ins.down_threshold = input;
out:
	up(&dbs_sem);
	return count;
}

#define define_one_rw(_name) 					\
static struct freq_attr _name = { 				\
	.attr = { .name = __stringify(_name), .mode = 0644 }, 	\
	.show = show_##_name, 					\
	.store = store_##_name, 				\
}

define_one_rw(sampling_rate);
define_one_rw(sampling_down_factor);
define_one_rw(up_threshold);
define_one_rw(down_threshold);

static struct attribute * dbs_attributes[] = {
	&sampling_rate_max.attr,
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&sampling_down_factor.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "ondemand",
};

/************************** sysfs end ************************/

static void dbs_check_cpu(int cpu)
{
	unsigned int idle_ticks, up_idle_ticks, down_idle_ticks;
	unsigned int total_idle_ticks;
	unsigned int freq_down_step;
	unsigned int freq_down_sampling_rate;
	static int down_skip[NR_CPUS];
	struct cpu_dbs_info_s *this_dbs_info;

	this_dbs_info = &per_cpu(cpu_dbs_info, cpu);
	if (!this_dbs_info->enable)
		return;

	/* 
	 * The default safe range is 20% to 80% 
	 * Every sampling_rate, we check
	 * 	- If current idle time is less than 20%, then we try to 
	 * 	  increase frequency
	 * Every sampling_rate*sampling_down_factor, we check
	 * 	- If current idle time is more than 80%, then we try to
	 * 	  decrease frequency
	 *
	 * Any frequency increase takes it to the maximum frequency. 
	 * Frequency reduction happens at minimum steps of 
	 * 5% of max_frequency 
	 */
	/* Check for frequency increase */
	total_idle_ticks = kstat_cpu(cpu).cpustat.idle +
		kstat_cpu(cpu).cpustat.iowait;
	idle_ticks = total_idle_ticks -
		this_dbs_info->prev_cpu_idle_up;
	this_dbs_info->prev_cpu_idle_up = total_idle_ticks;

	/* Scale idle ticks by 100 and compare with up and down ticks */
	idle_ticks *= 100;
	up_idle_ticks = (100 - dbs_tuners_ins.up_threshold) *
			sampling_rate_in_HZ(dbs_tuners_ins.sampling_rate);

	if (idle_ticks < up_idle_ticks) {
		__cpufreq_driver_target(this_dbs_info->cur_policy,
			this_dbs_info->cur_policy->max, 
			CPUFREQ_RELATION_H);
		down_skip[cpu] = 0;
		this_dbs_info->prev_cpu_idle_down = total_idle_ticks;
		return;
	}

	/* Check for frequency decrease */
	down_skip[cpu]++;
	if (down_skip[cpu] < dbs_tuners_ins.sampling_down_factor)
		return;

	idle_ticks = total_idle_ticks -
		this_dbs_info->prev_cpu_idle_down;
	/* Scale idle ticks by 100 and compare with up and down ticks */
	idle_ticks *= 100;
	down_skip[cpu] = 0;
	this_dbs_info->prev_cpu_idle_down = total_idle_ticks;

	freq_down_sampling_rate = dbs_tuners_ins.sampling_rate *
		dbs_tuners_ins.sampling_down_factor;
	down_idle_ticks = (100 - dbs_tuners_ins.down_threshold) *
			sampling_rate_in_HZ(freq_down_sampling_rate);

	if (idle_ticks > down_idle_ticks ) {
		freq_down_step = (5 * this_dbs_info->cur_policy->max) / 100;

		/* max freq cannot be less than 100. But who knows.... */
		if (unlikely(freq_down_step == 0))
			freq_down_step = 5;

		__cpufreq_driver_target(this_dbs_info->cur_policy,
			this_dbs_info->cur_policy->cur - freq_down_step, 
			CPUFREQ_RELATION_H);
		return;
	}
}

static void do_dbs_timer(void *data)
{ 
	int i;
	down(&dbs_sem);
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_online(i))
			dbs_check_cpu(i);
	schedule_delayed_work(&dbs_work, 
			sampling_rate_in_HZ(dbs_tuners_ins.sampling_rate));
	up(&dbs_sem);
} 

static inline void dbs_timer_init(void)
{
	INIT_WORK(&dbs_work, do_dbs_timer, NULL);
	schedule_work(&dbs_work);
	return;
}

static inline void dbs_timer_exit(void)
{
	cancel_delayed_work(&dbs_work);
	return;
}

static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;

	this_dbs_info = &per_cpu(cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || 
		    (!policy->cur))
			return -EINVAL;

		if (policy->cpuinfo.transition_latency >
				(TRANSITION_LATENCY_LIMIT * 1000))
			return -EINVAL;
		if (this_dbs_info->enable) /* Already enabled */
			break;
		 
		down(&dbs_sem);
		this_dbs_info->cur_policy = policy;
		
		this_dbs_info->prev_cpu_idle_up = 
				kstat_cpu(cpu).cpustat.idle +
				kstat_cpu(cpu).cpustat.iowait;
		this_dbs_info->prev_cpu_idle_down = 
				kstat_cpu(cpu).cpustat.idle +
				kstat_cpu(cpu).cpustat.iowait;
		this_dbs_info->enable = 1;
		sysfs_create_group(&policy->kobj, &dbs_attr_group);
		dbs_enable++;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 1) {
			unsigned int latency;
			/* policy latency is in nS. Convert it to uS first */

			latency = policy->cpuinfo.transition_latency;
			if (latency < 1000)
				latency = 1000;

			def_sampling_rate = (latency / 1000) *
					DEF_SAMPLING_RATE_LATENCY_MULTIPLIER;
			dbs_tuners_ins.sampling_rate = def_sampling_rate;

			dbs_timer_init();
		}
		
		up(&dbs_sem);
		break;

	case CPUFREQ_GOV_STOP:
		down(&dbs_sem);
		this_dbs_info->enable = 0;
		sysfs_remove_group(&policy->kobj, &dbs_attr_group);
		dbs_enable--;
		/*
		 * Stop the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 0) 
			dbs_timer_exit();
		
		up(&dbs_sem);

		break;

	case CPUFREQ_GOV_LIMITS:
		down(&dbs_sem);
		if (policy->max < this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
				       	policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
				       	policy->min, CPUFREQ_RELATION_L);
		up(&dbs_sem);
		break;
	}
	return 0;
}

struct cpufreq_governor cpufreq_gov_dbs = {
	.name		= "ondemand",
	.governor	= cpufreq_governor_dbs,
	.owner		= THIS_MODULE,
};
EXPORT_SYMBOL(cpufreq_gov_dbs);

static int __init cpufreq_gov_dbs_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_dbs);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	/* Make sure that the scheduled work is indeed not running */
	flush_scheduled_work();

	cpufreq_unregister_governor(&cpufreq_gov_dbs);
}


MODULE_AUTHOR ("Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>");
MODULE_DESCRIPTION ("'cpufreq_ondemand' - A dynamic cpufreq governor for "
		"Low Latency Frequency Transition capable processors");
MODULE_LICENSE ("GPL");

module_init(cpufreq_gov_dbs_init);
module_exit(cpufreq_gov_dbs_exit);

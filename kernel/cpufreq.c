/*
 *  linux/kernel/cpufreq.c
 *
 *  Copyright (C) 2001 Russell King
 *            (C) 2002 Dominik Brodowski <linux@brodo.de>
 *
 *  $Id: cpufreq.c,v 1.50 2002/11/11 15:35:48 db Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/proc_fs.h>

#include <asm/uaccess.h>

#ifdef CONFIG_CPU_FREQ_24_API
#include <linux/sysctl.h>
#endif


/**
 * The "cpufreq driver" - the arch- or hardware-dependend low
 * level driver of CPUFreq support, and its locking mutex. 
 * cpu_max_freq is in kHz.
 */
static struct cpufreq_driver   	*cpufreq_driver;
static DECLARE_MUTEX            (cpufreq_driver_sem);


/**
 * Two notifier lists: the "policy" list is involved in the 
 * validation process for a new CPU frequency policy; the 
 * "transition" list for kernel code that needs to handle
 * changes to devices when the CPU clock speed changes.
 * The mutex locks both lists. If both cpufreq_driver_sem
 * and cpufreq_notifier_sem need to be hold, get cpufreq_driver_sem
 * first.
 */
static struct notifier_block    *cpufreq_policy_notifier_list;
static struct notifier_block    *cpufreq_transition_notifier_list;
static DECLARE_MUTEX            (cpufreq_notifier_sem);


/**
 * The cpufreq default policy. Can be set by a "cpufreq=..." command
 * line option.
 */
static struct cpufreq_policy default_policy = {
	.cpu    = CPUFREQ_ALL_CPUS,
	.min    = 0,
	.max    = 0,
	.policy = 0,
};


#ifdef CONFIG_CPU_FREQ_24_API
/**
 * A few values needed by the 2.4.-compatible API
 */
static unsigned int     cpu_max_freq[NR_CPUS];
static unsigned int     cpu_min_freq[NR_CPUS];
static unsigned int     cpu_cur_freq[NR_CPUS];
#endif



/*********************************************************************
 *                              2.6. API                             *
 *********************************************************************/

/**
 * cpufreq_parse_policy - parse a policy string
 * @input_string: the string to parse.
 * @policy: the policy written inside input_string
 *
 * This function parses a "policy string" - something the user echo'es into
 * /proc/cpufreq or gives as boot parameter - into a struct cpufreq_policy.
 * If there are invalid/missing entries, they are replaced with current
 * cpufreq policy.
 */
static int cpufreq_parse_policy(char input_string[42], struct cpufreq_policy *policy)
{
	unsigned int            min = 0;
	unsigned int            max = 0;
	unsigned int            cpu = 0;
	char			policy_string[42] = {'\0'};
	struct cpufreq_policy   current_policy;
	unsigned int            result = -EFAULT;
	unsigned int            i = 0;

	if (!policy)
		return -EINVAL;

	policy->min = 0;
	policy->max = 0;
	policy->policy = 0;
	policy->cpu = CPUFREQ_ALL_CPUS;

	if (sscanf(input_string, "%d:%d:%d:%s", &cpu, &min, &max, policy_string) == 4) 
	{
		policy->min = min;
		policy->max = max;
		policy->cpu = cpu;
		result = 0;
		goto scan_policy;
	}
	if (sscanf(input_string, "%d%%%d%%%d%%%s", &cpu, &min, &max, policy_string) == 4)
	{
		if (!cpufreq_get_policy(&current_policy, cpu)) {
			policy->min = (min * current_policy.cpuinfo.max_freq) / 100;
			policy->max = (max * current_policy.cpuinfo.max_freq) / 100;
			policy->cpu = cpu;
			result = 0;
			goto scan_policy;
		}
	}

	if (sscanf(input_string, "%d:%d:%s", &min, &max, policy_string) == 3) 
	{
		policy->min = min;
		policy->max = max;
		result = 0;
		goto scan_policy;
	}

	if (sscanf(input_string, "%d%%%d%%%s", &min, &max, policy_string) == 3)
	{
		if (!cpufreq_get_policy(&current_policy, cpu)) {
			policy->min = (min * current_policy.cpuinfo.max_freq) / 100;
			policy->max = (max * current_policy.cpuinfo.max_freq) / 100;
			result = 0;
			goto scan_policy;
		}
	}

	return -EINVAL;

scan_policy:

	for (i=0;i<sizeof(policy_string);i++){
		if (policy_string[i]=='\0')
			break;
		policy_string[i] = tolower(policy_string[i]);
	}

	if (!strncmp(policy_string, "powersave", 6) ||  
            !strncmp(policy_string, "eco", 3) ||       
	    !strncmp(policy_string, "batter", 6) ||
	    !strncmp(policy_string, "low", 3)) 
	{
		result = 0;
		policy->policy = CPUFREQ_POLICY_POWERSAVE;
	}
	else if (!strncmp(policy_string, "performance",6) ||
	    !strncmp(policy_string, "high",4) ||
	    !strncmp(policy_string, "full",4))
	{
		result = 0;
		policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	}
	else if (!cpufreq_get_policy(&current_policy, policy->cpu))
	{
		policy->policy = current_policy.policy;
	}
	else
	{
		policy->policy = 0;
	}

	return result;
}


/*
 * cpufreq command line parameter.  Must be hard values (kHz)
 *  cpufreq=1000000:2000000:PERFORMANCE   
 * to set the default CPUFreq policy.
 */
static int __init cpufreq_setup(char *str)
{
	cpufreq_parse_policy(str, &default_policy);
	default_policy.cpu = CPUFREQ_ALL_CPUS;
	return 1;
}
__setup("cpufreq=", cpufreq_setup);


#ifdef CONFIG_PROC_FS

/**
 * cpufreq_proc_read - read /proc/cpufreq
 *
 * This function prints out the current cpufreq policy.
 */
static int cpufreq_proc_read (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	char			*p = page;
	int			len = 0;
	struct cpufreq_policy   policy;
	unsigned int            min_pctg = 0;
	unsigned int            max_pctg = 0;
	unsigned int            i = 0;

	if (off != 0)
		goto end;

	p += sprintf(p, "          minimum CPU frequency  -  maximum CPU frequency  -  policy\n");
	for (i=0;i<NR_CPUS;i++) {
		if (!cpu_online(i))
			continue;

		cpufreq_get_policy(&policy, i);

		if (!policy.cpuinfo.max_freq)
			continue;

		min_pctg = (policy.min * 100) / policy.cpuinfo.max_freq;
		max_pctg = (policy.max * 100) / policy.cpuinfo.max_freq;

		p += sprintf(p, "CPU%3d    %9d kHz (%3d %%)  -  %9d kHz (%3d %%)  -  ",
			     i , policy.min, min_pctg, policy.max, max_pctg);
		switch (policy.policy) {
		case CPUFREQ_POLICY_POWERSAVE:
			p += sprintf(p, "powersave\n");
			break;
		case CPUFREQ_POLICY_PERFORMANCE:
			p += sprintf(p, "performance\n");
			break;
		default:
			p += sprintf(p, "INVALID\n");
			break;
		}
	}
end:
	len = (p - page);
	if (len <= off+count) 
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) 
		len = count;
	if (len<0) 
		len = 0;

	return len;
}


/**
 * cpufreq_proc_write - handles writing into /proc/cpufreq
 *
 * This function calls the parsing script and then sets the policy
 * accordingly.
 */
static int cpufreq_proc_write (
        struct file		*file,
        const char		*buffer,
        unsigned long		count,
        void			*data)
{
	int                     result = 0;
	char			proc_string[42] = {'\0'};
	struct cpufreq_policy   policy;
	unsigned int            i = 0;


	if ((count > sizeof(proc_string) - 1))
		return -EINVAL;
	
	if (copy_from_user(proc_string, buffer, count))
		return -EFAULT;
	
	proc_string[count] = '\0';

	result = cpufreq_parse_policy(proc_string, &policy);
	if (result)
		return -EFAULT;

	if (policy.cpu == CPUFREQ_ALL_CPUS)
	{
		for (i=0; i<NR_CPUS; i++) 
		{
			policy.cpu = i;
			if (cpu_online(i))
				cpufreq_set_policy(&policy);
		}
	} 
	else
		cpufreq_set_policy(&policy);

	return count;
}


/**
 * cpufreq_proc_init - add "cpufreq" to the /proc root directory
 *
 * This function adds "cpufreq" to the /proc root directory.
 */
static unsigned int cpufreq_proc_init (void)
{
	struct proc_dir_entry *entry = NULL;

        /* are these acceptable values? */
	entry = create_proc_entry("cpufreq", S_IFREG|S_IRUGO|S_IWUSR, 
				  &proc_root);

	if (!entry) {
		printk(KERN_ERR "unable to create /proc/cpufreq entry\n");
		return -EIO;
	} else {
		entry->read_proc = cpufreq_proc_read;
		entry->write_proc = cpufreq_proc_write;
	}

	return 0;
}


/**
 * cpufreq_proc_exit - removes "cpufreq" from the /proc root directory.
 *
 * This function removes "cpufreq" from the /proc root directory.
 */
static void cpufreq_proc_exit (void)
{
	remove_proc_entry("cpufreq", &proc_root);
	return;
}
#endif /* CONFIG_PROC_FS */



/*********************************************************************
 *                        2.4. COMPATIBLE API                        *
 *********************************************************************/

#ifdef CONFIG_CPU_FREQ_24_API
/** 
 * cpufreq_set - set the CPU frequency
 * @freq: target frequency in kHz
 * @cpu: CPU for which the frequency is to be set
 *
 * Sets the CPU frequency to freq.
 */
int cpufreq_set(unsigned int freq, unsigned int cpu)
{
	struct cpufreq_policy policy;
	down(&cpufreq_driver_sem);
	if (!cpufreq_driver || !freq || (cpu > NR_CPUS)) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	policy.min = freq;
	policy.max = freq;
	policy.policy = CPUFREQ_POLICY_POWERSAVE;
	policy.cpu = cpu;
	
	up(&cpufreq_driver_sem);

	if (policy.cpu == CPUFREQ_ALL_CPUS)
	{
		unsigned int i;
		unsigned int ret = 0;
		for (i=0; i<NR_CPUS; i++) 
		{
			policy.cpu = i;
			if (cpu_online(i))
				ret |= cpufreq_set_policy(&policy);
		}
		return ret;
	} 
	else
		return cpufreq_set_policy(&policy);
}
EXPORT_SYMBOL_GPL(cpufreq_set);


/** 
 * cpufreq_setmax - set the CPU to the maximum frequency
 * @cpu - affected cpu;
 *
 * Sets the CPU frequency to the maximum frequency supported by
 * this CPU.
 */
int cpufreq_setmax(unsigned int cpu)
{
	if (!cpu_online(cpu) && (cpu != CPUFREQ_ALL_CPUS))
		return -EINVAL;
	return cpufreq_set(cpu_max_freq[cpu], cpu);
}
EXPORT_SYMBOL_GPL(cpufreq_setmax);


/** 
 * cpufreq_get - get the current CPU frequency (in kHz)
 * @cpu: CPU number - currently without effect.
 *
 * Get the CPU current (static) CPU frequency
 */
unsigned int cpufreq_get(unsigned int cpu)
{
	if (!cpu_online(cpu))
		return -EINVAL;
	return cpu_cur_freq[cpu];
}
EXPORT_SYMBOL(cpufreq_get);


#ifdef CONFIG_SYSCTL


/*********************** cpufreq_sysctl interface ********************/
static int
cpufreq_procctl(ctl_table *ctl, int write, struct file *filp,
		void *buffer, size_t *lenp)
{
	char buf[16], *p;
	int cpu = (int) ctl->extra1;
	int len, left = *lenp;

	if (!left || (filp->f_pos && !write) || !cpu_online(cpu)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		unsigned int freq;

		len = left;
		if (left > sizeof(buf))
			left = sizeof(buf);
		if (copy_from_user(buf, buffer, left))
			return -EFAULT;
		buf[sizeof(buf) - 1] = '\0';

		freq = simple_strtoul(buf, &p, 0);
		cpufreq_set(freq, cpu);
	} else {
		len = sprintf(buf, "%d\n", cpufreq_get(cpu));
		if (len > left)
			len = left;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
	}

	*lenp = len;
	filp->f_pos += len;
	return 0;
}

static int
cpufreq_sysctl(ctl_table *table, int *name, int nlen,
	       void *oldval, size_t *oldlenp,
	       void *newval, size_t newlen, void **context)
{
	int cpu = (int) table->extra1;

	if (!cpu_online(cpu))
		return -EINVAL;

	if (oldval && oldlenp) {
		size_t oldlen;

		if (get_user(oldlen, oldlenp))
			return -EFAULT;

		if (oldlen != sizeof(unsigned int))
			return -EINVAL;

		if (put_user(cpufreq_get(cpu), (unsigned int *)oldval) ||
		    put_user(sizeof(unsigned int), oldlenp))
			return -EFAULT;
	}
	if (newval && newlen) {
		unsigned int freq;

		if (newlen != sizeof(unsigned int))
			return -EINVAL;

		if (get_user(freq, (unsigned int *)newval))
			return -EFAULT;

		cpufreq_set(freq, cpu);
	}
	return 1;
}

/* ctl_table ctl_cpu_vars_{0,1,...,(NR_CPUS-1)} */
/* due to NR_CPUS tweaking, a lot of if/endifs are required, sorry */
        CTL_TABLE_CPU_VARS(0);
#if NR_CPUS > 1
	CTL_TABLE_CPU_VARS(1);
#endif
#if NR_CPUS > 2
	CTL_TABLE_CPU_VARS(2);
#endif
#if NR_CPUS > 3
	CTL_TABLE_CPU_VARS(3);
#endif
#if NR_CPUS > 4
	CTL_TABLE_CPU_VARS(4);
#endif
#if NR_CPUS > 5
	CTL_TABLE_CPU_VARS(5);
#endif
#if NR_CPUS > 6
	CTL_TABLE_CPU_VARS(6);
#endif
#if NR_CPUS > 7
	CTL_TABLE_CPU_VARS(7);
#endif
#if NR_CPUS > 8
	CTL_TABLE_CPU_VARS(8);
#endif
#if NR_CPUS > 9
	CTL_TABLE_CPU_VARS(9);
#endif
#if NR_CPUS > 10
	CTL_TABLE_CPU_VARS(10);
#endif
#if NR_CPUS > 11
	CTL_TABLE_CPU_VARS(11);
#endif
#if NR_CPUS > 12
	CTL_TABLE_CPU_VARS(12);
#endif
#if NR_CPUS > 13
	CTL_TABLE_CPU_VARS(13);
#endif
#if NR_CPUS > 14
	CTL_TABLE_CPU_VARS(14);
#endif
#if NR_CPUS > 15
	CTL_TABLE_CPU_VARS(15);
#endif
#if NR_CPUS > 16
	CTL_TABLE_CPU_VARS(16);
#endif
#if NR_CPUS > 17
	CTL_TABLE_CPU_VARS(17);
#endif
#if NR_CPUS > 18
	CTL_TABLE_CPU_VARS(18);
#endif
#if NR_CPUS > 19
	CTL_TABLE_CPU_VARS(19);
#endif
#if NR_CPUS > 20
	CTL_TABLE_CPU_VARS(20);
#endif
#if NR_CPUS > 21
	CTL_TABLE_CPU_VARS(21);
#endif
#if NR_CPUS > 22
	CTL_TABLE_CPU_VARS(22);
#endif
#if NR_CPUS > 23
	CTL_TABLE_CPU_VARS(23);
#endif
#if NR_CPUS > 24
	CTL_TABLE_CPU_VARS(24);
#endif
#if NR_CPUS > 25
	CTL_TABLE_CPU_VARS(25);
#endif
#if NR_CPUS > 26
	CTL_TABLE_CPU_VARS(26);
#endif
#if NR_CPUS > 27
	CTL_TABLE_CPU_VARS(27);
#endif
#if NR_CPUS > 28
	CTL_TABLE_CPU_VARS(28);
#endif
#if NR_CPUS > 29
	CTL_TABLE_CPU_VARS(29);
#endif
#if NR_CPUS > 30
	CTL_TABLE_CPU_VARS(30);
#endif
#if NR_CPUS > 31
	CTL_TABLE_CPU_VARS(31);
#endif
#if NR_CPUS > 32
#error please extend CPU enumeration
#endif

/* due to NR_CPUS tweaking, a lot of if/endifs are required, sorry */
static ctl_table ctl_cpu_table[NR_CPUS + 1] = {
	CPU_ENUM(0),
#if NR_CPUS > 1
	CPU_ENUM(1),
#endif
#if NR_CPUS > 2
	CPU_ENUM(2),
#endif
#if NR_CPUS > 3
	CPU_ENUM(3),
#endif
#if NR_CPUS > 4
	CPU_ENUM(4),
#endif
#if NR_CPUS > 5
	CPU_ENUM(5),
#endif
#if NR_CPUS > 6
	CPU_ENUM(6),
#endif
#if NR_CPUS > 7
	CPU_ENUM(7),
#endif
#if NR_CPUS > 8
	CPU_ENUM(8),
#endif
#if NR_CPUS > 9
	CPU_ENUM(9),
#endif
#if NR_CPUS > 10
	CPU_ENUM(10),
#endif
#if NR_CPUS > 11
	CPU_ENUM(11),
#endif
#if NR_CPUS > 12
	CPU_ENUM(12),
#endif
#if NR_CPUS > 13
	CPU_ENUM(13),
#endif
#if NR_CPUS > 14
	CPU_ENUM(14),
#endif
#if NR_CPUS > 15
	CPU_ENUM(15),
#endif
#if NR_CPUS > 16
	CPU_ENUM(16),
#endif
#if NR_CPUS > 17
	CPU_ENUM(17),
#endif
#if NR_CPUS > 18
	CPU_ENUM(18),
#endif
#if NR_CPUS > 19
	CPU_ENUM(19),
#endif
#if NR_CPUS > 20
	CPU_ENUM(20),
#endif
#if NR_CPUS > 21
	CPU_ENUM(21),
#endif
#if NR_CPUS > 22
	CPU_ENUM(22),
#endif
#if NR_CPUS > 23
	CPU_ENUM(23),
#endif
#if NR_CPUS > 24
	CPU_ENUM(24),
#endif
#if NR_CPUS > 25
	CPU_ENUM(25),
#endif
#if NR_CPUS > 26
	CPU_ENUM(26),
#endif
#if NR_CPUS > 27
	CPU_ENUM(27),
#endif
#if NR_CPUS > 28
	CPU_ENUM(28),
#endif
#if NR_CPUS > 29
	CPU_ENUM(29),
#endif
#if NR_CPUS > 30
	CPU_ENUM(30),
#endif
#if NR_CPUS > 31
	CPU_ENUM(31),
#endif
#if NR_CPUS > 32
#error please extend CPU enumeration
#endif
	{
		.ctl_name	= 0,
	}
};

static ctl_table ctl_cpu[2] = {
	{
		.ctl_name	= CTL_CPU,
		.procname	= "cpu",
		.mode		= 0555,
		.child		= ctl_cpu_table,
	},
	{
		.ctl_name	= 0,
	}
};

struct ctl_table_header *cpufreq_sysctl_table;

static inline void cpufreq_sysctl_init(void)
{
	cpufreq_sysctl_table = register_sysctl_table(ctl_cpu, 0);
}

static inline void cpufreq_sysctl_exit(void)
{
	unregister_sysctl_table(cpufreq_sysctl_table);
}

#else
#define cpufreq_sysctl_init() do {} while(0)
#define cpufreq_sysctl_exit() do {} while(0)
#endif /* CONFIG_SYSCTL */
#endif /* CONFIG_CPU_FREQ_24_API */



/*********************************************************************
 *                     NOTIFIER LISTS INTERFACE                      *
 *********************************************************************/

/**
 *	cpufreq_register_notifier - register a driver with cpufreq
 *	@nb: notifier function to register
 *      @list: CPUFREQ_TRANSITION_NOTIFIER or CPUFREQ_POLICY_NOTIFIER
 *
 *	Add a driver to one of two lists: either a list of drivers that 
 *      are notified about clock rate changes (once before and once after
 *      the transition), or a list of drivers that are notified about
 *      changes in cpufreq policy.
 *
 *	This function may sleep, and has the same return conditions as
 *	notifier_chain_register.
 */
int cpufreq_register_notifier(struct notifier_block *nb, unsigned int list)
{
	int ret;

	down(&cpufreq_notifier_sem);
	switch (list) {
	case CPUFREQ_TRANSITION_NOTIFIER:
		ret = notifier_chain_register(&cpufreq_transition_notifier_list, nb);
		break;
	case CPUFREQ_POLICY_NOTIFIER:
		ret = notifier_chain_register(&cpufreq_policy_notifier_list, nb);
		break;
	default:
		ret = -EINVAL;
	}
	up(&cpufreq_notifier_sem);

	return ret;
}
EXPORT_SYMBOL(cpufreq_register_notifier);


/**
 *	cpufreq_unregister_notifier - unregister a driver with cpufreq
 *	@nb: notifier block to be unregistered
 *      @list: CPUFREQ_TRANSITION_NOTIFIER or CPUFREQ_POLICY_NOTIFIER
 *
 *	Remove a driver from the CPU frequency notifier list.
 *
 *	This function may sleep, and has the same return conditions as
 *	notifier_chain_unregister.
 */
int cpufreq_unregister_notifier(struct notifier_block *nb, unsigned int list)
{
	int ret;

	down(&cpufreq_notifier_sem);
	switch (list) {
	case CPUFREQ_TRANSITION_NOTIFIER:
		ret = notifier_chain_unregister(&cpufreq_transition_notifier_list, nb);
		break;
	case CPUFREQ_POLICY_NOTIFIER:
		ret = notifier_chain_unregister(&cpufreq_policy_notifier_list, nb);
		break;
	default:
		ret = -EINVAL;
	}
	up(&cpufreq_notifier_sem);

	return ret;
}
EXPORT_SYMBOL(cpufreq_unregister_notifier);



/*********************************************************************
 *                          POLICY INTERFACE                         *
 *********************************************************************/

/**
 * cpufreq_get_policy - get the current cpufreq_policy
 * @policy: struct cpufreq_policy into which the current cpufreq_policy is written
 *
 * Reads the current cpufreq policy.
 */
int cpufreq_get_policy(struct cpufreq_policy *policy, unsigned int cpu)
{
	down(&cpufreq_driver_sem);
	if (!cpufreq_driver  || !policy || 
	    (cpu >= NR_CPUS) || (!cpu_online(cpu))) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}
	
	policy->min    = cpufreq_driver->policy[cpu].min;
	policy->max    = cpufreq_driver->policy[cpu].max;
	policy->policy = cpufreq_driver->policy[cpu].policy;
	policy->cpuinfo.max_freq       = cpufreq_driver->policy[cpu].cpuinfo.max_freq;
	policy->cpuinfo.min_freq       = cpufreq_driver->policy[cpu].cpuinfo.min_freq;
	policy->cpuinfo.transition_latency = cpufreq_driver->policy[cpu].cpuinfo.transition_latency;
	policy->cpu    = cpu;

	up(&cpufreq_driver_sem);

	return 0;
}
EXPORT_SYMBOL(cpufreq_get_policy);


/**
 *	cpufreq_set_policy - set a new CPUFreq policy
 *	@policy: policy to be set.
 *
 *	Sets a new CPU frequency and voltage scaling policy.
 */
int cpufreq_set_policy(struct cpufreq_policy *policy)
{
	int ret;

	down(&cpufreq_driver_sem);
	if (!cpufreq_driver || !cpufreq_driver->verify || 
	    !cpufreq_driver->setpolicy || !policy ||
	    (policy->cpu >= NR_CPUS) || (!cpu_online(policy->cpu))) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	policy->cpuinfo.max_freq       = cpufreq_driver->policy[policy->cpu].cpuinfo.max_freq;
	policy->cpuinfo.min_freq       = cpufreq_driver->policy[policy->cpu].cpuinfo.min_freq;
	policy->cpuinfo.transition_latency = cpufreq_driver->policy[policy->cpu].cpuinfo.transition_latency;

	/* verify the cpu speed can be set within this limit */
	ret = cpufreq_driver->verify(policy);
	if (ret) {
		up(&cpufreq_driver_sem);
		return ret;
	}

	down(&cpufreq_notifier_sem);

	/* adjust if neccessary - all reasons */
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_ADJUST,
			    policy);

	/* adjust if neccessary - hardware incompatibility*/
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_INCOMPATIBLE,
			    policy);

	/* verify the cpu speed can be set within this limit,
	   which might be different to the first one */
	ret = cpufreq_driver->verify(policy);
	if (ret) {
		up(&cpufreq_notifier_sem);
		up(&cpufreq_driver_sem);
		return ret;
	}

	/* notification of the new policy */
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_NOTIFY,
			    policy);

	up(&cpufreq_notifier_sem);

	cpufreq_driver->policy[policy->cpu].min    = policy->min;
	cpufreq_driver->policy[policy->cpu].max    = policy->max;
	cpufreq_driver->policy[policy->cpu].policy = policy->policy;

#ifdef CONFIG_CPU_FREQ_24_API
	cpu_cur_freq[policy->cpu] = policy->max;
#endif

	ret = cpufreq_driver->setpolicy(policy);
	
	up(&cpufreq_driver_sem);

	return ret;
}
EXPORT_SYMBOL(cpufreq_set_policy);



/*********************************************************************
 *            EXTERNALLY AFFECTING FREQUENCY CHANGES                 *
 *********************************************************************/

/**
 * adjust_jiffies - adjust the system "loops_per_jiffy"
 *
 * This function alters the system "loops_per_jiffy" for the clock
 * speed change. Note that loops_per_jiffy cannot be updated on SMP
 * systems as each CPU might be scaled differently. So, use the arch 
 * per-CPU loops_per_jiffy value wherever possible.
 */
#ifndef CONFIG_SMP
static unsigned long l_p_j_ref = 0;
static unsigned int  l_p_j_ref_freq = 0;

static inline void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci)
{
	if (!l_p_j_ref_freq) {
		l_p_j_ref = loops_per_jiffy;
		l_p_j_ref_freq = ci->old;
	}
	if ((val == CPUFREQ_PRECHANGE  && ci->old < ci->new) ||
	    (val == CPUFREQ_POSTCHANGE && ci->old > ci->new))
		loops_per_jiffy = cpufreq_scale(l_p_j_ref, l_p_j_ref_freq, ci->new);
}
#else
#define adjust_jiffies(x...) do {} while (0)
#endif


/**
 * cpufreq_notify_transition - call notifier chain and adjust_jiffies on frequency transition
 *
 * This function calls the transition notifiers and the "adjust_jiffies" function. It is called
 * twice on all CPU frequency changes that have external effects. 
 */
void cpufreq_notify_transition(struct cpufreq_freqs *freqs, unsigned int state)
{
	down(&cpufreq_notifier_sem);
	switch (state) {
	case CPUFREQ_PRECHANGE:
		notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_PRECHANGE, freqs);
		adjust_jiffies(CPUFREQ_PRECHANGE, freqs);		
		break;
	case CPUFREQ_POSTCHANGE:
		adjust_jiffies(CPUFREQ_POSTCHANGE, freqs);
		notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_POSTCHANGE, freqs);
#ifdef CONFIG_CPU_FREQ_24_API
		cpu_cur_freq[freqs->cpu] = freqs->new;
#endif
		break;
	}
	up(&cpufreq_notifier_sem);
}
EXPORT_SYMBOL_GPL(cpufreq_notify_transition);



/*********************************************************************
 *               REGISTER / UNREGISTER CPUFREQ DRIVER                *
 *********************************************************************/

/**
 * cpufreq_register - register a CPU Frequency driver
 * @driver_data: A struct cpufreq_driver containing the values submitted by the CPU Frequency driver.
 *
 *   Registers a CPU Frequency driver to this core code. This code 
 * returns zero on success, -EBUSY when another driver got here first
 * (and isn't unregistered in the meantime). 
 *
 */
int cpufreq_register(struct cpufreq_driver *driver_data)
{
	unsigned int            ret;
	unsigned int            i;
	struct cpufreq_policy   policy;

	if (cpufreq_driver)
		return -EBUSY;
	
	if (!driver_data || !driver_data->verify || 
	    !driver_data->setpolicy)
		return -EINVAL;

	down(&cpufreq_driver_sem);
	cpufreq_driver        = driver_data;
	
	/* check for a default policy - if it exists, use it on _all_ CPUs*/
	for (i=0; i<NR_CPUS; i++)
	{
		if (default_policy.policy)
			cpufreq_driver->policy[i].policy = default_policy.policy;
		if (default_policy.min)
			cpufreq_driver->policy[i].min = default_policy.min;
		if (default_policy.max)
			cpufreq_driver->policy[i].max = default_policy.max;
	}

	/* set default policy on all CPUs. Must be called per-CPU and not
	 * with CPUFREQ_ALL_CPUs as there might be no common policy for all
	 * CPUs (UltraSPARC etc.)
	 */
	for (i=0; i<NR_CPUS; i++)
	{
		policy.policy = cpufreq_driver->policy[i].policy;
		policy.min    = cpufreq_driver->policy[i].min;
		policy.max    = cpufreq_driver->policy[i].max;
		policy.cpu    = i;
		up(&cpufreq_driver_sem);
		ret = cpufreq_set_policy(&policy);
		down(&cpufreq_driver_sem);
		if (ret) {
			cpufreq_driver = NULL;
			up(&cpufreq_driver_sem);
			return ret;
		}
	}

	up(&cpufreq_driver_sem);

	cpufreq_proc_init();

#ifdef CONFIG_CPU_FREQ_24_API
 	down(&cpufreq_driver_sem);
	for (i=0; i<NR_CPUS; i++) 
	{
		cpu_min_freq[i] = driver_data->policy[i].cpuinfo.min_freq;
		cpu_max_freq[i] = driver_data->policy[i].cpuinfo.max_freq;
		cpu_cur_freq[i] = driver_data->cpu_cur_freq[i];
	}
	up(&cpufreq_driver_sem);

	cpufreq_sysctl_init();
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_register);


/**
 * cpufreq_unregister - unregister the current CPUFreq driver
 *
 *    Unregister the current CPUFreq driver. Only call this if you have 
 * the right to do so, i.e. if you have succeeded in initialising before!
 * Returns zero if successful, and -EINVAL if the cpufreq_driver is
 * currently not initialised.
 */
int cpufreq_unregister(void)
{
	down(&cpufreq_driver_sem);

	if (!cpufreq_driver) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	cpufreq_driver = NULL;

	up(&cpufreq_driver_sem);

	cpufreq_proc_exit();

#ifdef CONFIG_CPU_FREQ_24_API
	cpufreq_sysctl_exit();
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_unregister);


#ifdef CONFIG_PM
/**
 *	cpufreq_restore - restore the CPU clock frequency after resume
 *
 *	Restore the CPU clock frequency so that our idea of the current
 *	frequency reflects the actual hardware.
 */
int cpufreq_restore(void)
{
	struct cpufreq_policy policy;
	unsigned int i;
	unsigned int ret = 0;

	if (in_interrupt())
		panic("cpufreq_restore() called from interrupt context!");

	for (i=0;i<NR_CPUS;i++) {
		if (!cpu_online(i))
			continue;

		down(&cpufreq_driver_sem);
		if (!cpufreq_driver) {
			up(&cpufreq_driver_sem);
			return 0;
		}
	
		policy.min    = cpufreq_driver->policy[i].min;
		policy.max    = cpufreq_driver->policy[i].max;
		policy.policy = cpufreq_driver->policy[i].policy;
		policy.cpu    = i;
		up(&cpufreq_driver_sem);

		ret += cpufreq_set_policy(&policy);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_restore);
#else
#define cpufreq_restore() do {} while (0)
#endif /* CONFIG_PM */


/*********************************************************************
 *                     FREQUENCY TABLE HELPERS                       *
 *********************************************************************/

int cpufreq_frequency_table_cpuinfo(struct cpufreq_policy *policy,
				    struct cpufreq_frequency_table *table)
{
	unsigned int min_freq = ~0;
	unsigned int max_freq = 0;
	unsigned int i = 0;

	for (i=0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if (freq < min_freq)
			min_freq = freq;
		if (freq > max_freq)
			max_freq = freq;
	}

	policy->min = policy->cpuinfo.min_freq = min_freq;
	policy->max = policy->cpuinfo.max_freq = max_freq;

	if (policy->min == ~0)
		return -EINVAL;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_cpuinfo);


int cpufreq_frequency_table_verify(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table)
{
	unsigned int next_larger = ~0;
	unsigned int i = 0;
	unsigned int count = 0;

	if (!cpu_online(policy->cpu))
		return -EINVAL;

	cpufreq_verify_within_limits(policy, 
				     policy->cpuinfo.min_freq, 
				     policy->cpuinfo.max_freq);

	for (i=0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if ((freq >= policy->min) && (freq <= policy->max))
			count++;
		else if ((next_larger > freq) && (freq > policy->max))
			next_larger = freq;
	}

	if (!count)
		policy->max = next_larger;

	cpufreq_verify_within_limits(policy, 
				     policy->cpuinfo.min_freq, 
				     policy->cpuinfo.max_freq);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_verify);


int cpufreq_frequency_table_setpolicy(struct cpufreq_policy *policy,
				      struct cpufreq_frequency_table *table,
				      unsigned int *index)
{
	struct cpufreq_frequency_table optimal = { .index = ~0, };
	unsigned int i;

	switch (policy->policy) {
	case CPUFREQ_POLICY_PERFORMANCE:
		optimal.frequency = 0;
		break;
	case CPUFREQ_POLICY_POWERSAVE:
		optimal.frequency = ~0;
		break;
	}

	if (!cpu_online(policy->cpu))
		return -EINVAL;

	for (i=0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if ((freq < policy->min) || (freq > policy->max))
			continue;
		switch(policy->policy) {
		case CPUFREQ_POLICY_PERFORMANCE:
			if (optimal.frequency <= freq) {
				optimal.frequency = freq;
				optimal.index = i;
			}
			break;
		case CPUFREQ_POLICY_POWERSAVE:
			if (optimal.frequency >= freq) {
				optimal.frequency = freq;
				optimal.index = i;
			}
			break;
		}
	}
	if (optimal.index > i)
		return -EINVAL;

	*index = optimal.index;
	
	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_setpolicy);

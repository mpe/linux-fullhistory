/*
 * cpufreq driver for Enhanced SpeedStep, as found in Intel's Pentium
 * M (part of the Centrino chipset).
 *
 * Despite the "SpeedStep" in the name, this is almost entirely unlike
 * traditional SpeedStep.
 *
 * Modelled on speedstep.c
 *
 * Copyright (C) 2003 Jeremy Fitzhardinge <jeremy@goop.org>
 *
 * WARNING WARNING WARNING
 *
 * This driver manipulates the PERF_CTL MSR, which is only somewhat
 * documented.  While it seems to work on my laptop, it has not been
 * tested anywhere else, and it may not work for you, do strange
 * things or simply crash.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/config.h>

#ifdef CONFIG_X86_SPEEDSTEP_CENTRINO_ACPI
#include <linux/acpi.h>
#include <acpi/processor.h>
#endif

#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>

#define PFX		"speedstep-centrino: "
#define MAINTAINER	"Jeremy Fitzhardinge <jeremy@goop.org>"

/*#define CENTRINO_DEBUG*/

#ifdef CENTRINO_DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0)
#endif

struct cpu_id
{
	__u8	x86;            /* CPU family */
	__u8	x86_model;	/* model */
	__u8	x86_mask;	/* stepping */
};

enum {
	CPU_BANIAS,
	CPU_DOTHAN_A1,
	CPU_DOTHAN_A2,
	CPU_DOTHAN_B0,
};

static const struct cpu_id cpu_ids[] = {
	[CPU_BANIAS]	= { 6,  9, 5 },
	[CPU_DOTHAN_A1]	= { 6, 13, 1 },
	[CPU_DOTHAN_A2]	= { 6, 13, 2 },
	[CPU_DOTHAN_B0]	= { 6, 13, 6 },
};
#define N_IDS	(sizeof(cpu_ids)/sizeof(cpu_ids[0]))

struct cpu_model
{
	const struct cpu_id *cpu_id;
	const char	*model_name;
	unsigned	max_freq; /* max clock in kHz */

	struct cpufreq_frequency_table *op_points; /* clock/voltage pairs */
};
static int centrino_verify_cpu_id(const struct cpuinfo_x86 *c, const struct cpu_id *x);

/* Operating points for current CPU */
static struct cpu_model *centrino_model;
static int centrino_cpu;

#ifdef CONFIG_X86_SPEEDSTEP_CENTRINO_TABLE

/* Computes the correct form for IA32_PERF_CTL MSR for a particular
   frequency/voltage operating point; frequency in MHz, volts in mV.
   This is stored as "index" in the structure. */
#define OP(mhz, mv)							\
	{								\
		.frequency = (mhz) * 1000,				\
		.index = (((mhz)/100) << 8) | ((mv - 700) / 16)		\
	}

/*
 * These voltage tables were derived from the Intel Pentium M
 * datasheet, document 25261202.pdf, Table 5.  I have verified they
 * are consistent with my IBM ThinkPad X31, which has a 1.3GHz Pentium
 * M.
 */

/* Ultra Low Voltage Intel Pentium M processor 900MHz (Banias) */
static struct cpufreq_frequency_table banias_900[] =
{
	OP(600,  844),
	OP(800,  988),
	OP(900, 1004),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Ultra Low Voltage Intel Pentium M processor 1000MHz (Banias) */
static struct cpufreq_frequency_table banias_1000[] =
{
	OP(600,   844),
	OP(800,   972),
	OP(900,   988),
	OP(1000, 1004),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Low Voltage Intel Pentium M processor 1.10GHz (Banias) */
static struct cpufreq_frequency_table banias_1100[] =
{
	OP( 600,  956),
	OP( 800, 1020),
	OP( 900, 1100),
	OP(1000, 1164),
	OP(1100, 1180),
	{ .frequency = CPUFREQ_TABLE_END }
};


/* Low Voltage Intel Pentium M processor 1.20GHz (Banias) */
static struct cpufreq_frequency_table banias_1200[] =
{
	OP( 600,  956),
	OP( 800, 1004),
	OP( 900, 1020),
	OP(1000, 1100),
	OP(1100, 1164),
	OP(1200, 1180),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.30GHz (Banias) */
static struct cpufreq_frequency_table banias_1300[] =
{
	OP( 600,  956),
	OP( 800, 1260),
	OP(1000, 1292),
	OP(1200, 1356),
	OP(1300, 1388),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.40GHz (Banias) */
static struct cpufreq_frequency_table banias_1400[] =
{
	OP( 600,  956),
	OP( 800, 1180),
	OP(1000, 1308),
	OP(1200, 1436),
	OP(1400, 1484),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.50GHz (Banias) */
static struct cpufreq_frequency_table banias_1500[] =
{
	OP( 600,  956),
	OP( 800, 1116),
	OP(1000, 1228),
	OP(1200, 1356),
	OP(1400, 1452),
	OP(1500, 1484),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.60GHz (Banias) */
static struct cpufreq_frequency_table banias_1600[] =
{
	OP( 600,  956),
	OP( 800, 1036),
	OP(1000, 1164),
	OP(1200, 1276),
	OP(1400, 1420),
	OP(1600, 1484),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.70GHz (Banias) */
static struct cpufreq_frequency_table banias_1700[] =
{
	OP( 600,  956),
	OP( 800, 1004),
	OP(1000, 1116),
	OP(1200, 1228),
	OP(1400, 1308),
	OP(1700, 1484),
	{ .frequency = CPUFREQ_TABLE_END }
};
#undef OP

#define _BANIAS(cpuid, max, name)	\
{	.cpu_id		= cpuid,	\
	.model_name	= "Intel(R) Pentium(R) M processor " name "MHz", \
	.max_freq	= (max)*1000,	\
	.op_points	= banias_##max,	\
}
#define BANIAS(max)	_BANIAS(&cpu_ids[CPU_BANIAS], max, #max)

/* CPU models, their operating frequency range, and freq/voltage
   operating points */
static struct cpu_model models[] =
{
	_BANIAS(&cpu_ids[CPU_BANIAS], 900, " 900"),
	BANIAS(1000),
	BANIAS(1100),
	BANIAS(1200),
	BANIAS(1300),
	BANIAS(1400),
	BANIAS(1500),
	BANIAS(1600),
	BANIAS(1700),

	/* NULL model_name is a wildcard */
	{ &cpu_ids[CPU_DOTHAN_A1], NULL, 0, NULL },
	{ &cpu_ids[CPU_DOTHAN_A2], NULL, 0, NULL },
	{ &cpu_ids[CPU_DOTHAN_B0], NULL, 0, NULL },

	{ NULL, }
};
#undef _BANIAS
#undef BANIAS

static int centrino_cpu_init_table(struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *cpu = &cpu_data[policy->cpu];
	struct cpu_model *model;

	for(model = models; model->cpu_id != NULL; model++)
		if (centrino_verify_cpu_id(cpu, model->cpu_id) &&
		    (model->model_name == NULL ||
		     strcmp(cpu->x86_model_id, model->model_name) == 0))
			break;

	if (model->cpu_id == NULL) {
		/* No match at all */
		printk(KERN_INFO PFX "no support for CPU model \"%s\": "
		       "send /proc/cpuinfo to " MAINTAINER "\n",
		       cpu->x86_model_id);
		return -ENOENT;
	}

	if (model->op_points == NULL) {
		/* Matched a non-match */
		printk(KERN_INFO PFX "no table support for CPU model \"%s\": \n",
		       cpu->x86_model_id);
		printk(KERN_INFO PFX "try compiling with CONFIG_X86_SPEEDSTEP_CENTRINO_ACPI enabled\n");
		return -ENOENT;
	}

	centrino_model = model;

	printk(KERN_INFO PFX "found \"%s\": max frequency: %dkHz\n",
	       model->model_name, model->max_freq);

	return 0;
}

#else
static inline int centrino_cpu_init_table(struct cpufreq_policy *policy) { return -ENODEV; }
#endif /* CONFIG_X86_SPEEDSTEP_CENTRINO_TABLE */

static int centrino_verify_cpu_id(const struct cpuinfo_x86 *c, const struct cpu_id *x)
{
	if ((c->x86 == x->x86) &&
	    (c->x86_model == x->x86_model) &&
	    (c->x86_mask == x->x86_mask))
		return 1;
	return 0;
}

/* To be called only after centrino_model is initialized */
static unsigned extract_clock(unsigned msr)
{
	int i;

	/*
	 * Extract clock in kHz from PERF_CTL value
	 * for centrino, as some DSDTs are buggy.
	 * Ideally, this can be done using the acpi_data structure.
	 */
	if (centrino_cpu) {
		msr = (msr >> 8) & 0xff;
		return msr * 100000;
	}

	if ((!centrino_model) || (!centrino_model->op_points))
		return 0;

	msr &= 0xffff;
	for (i=0;centrino_model->op_points[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (msr == centrino_model->op_points[i].index)
		return centrino_model->op_points[i].frequency;
	}
	return 0;
}

/* Return the current CPU frequency in kHz */
static unsigned int get_cur_freq(unsigned int cpu)
{
	unsigned l, h;
	cpumask_t saved_mask;

	saved_mask = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	if (smp_processor_id() != cpu)
		return 0;

	rdmsr(MSR_IA32_PERF_STATUS, l, h);
	set_cpus_allowed(current, saved_mask);
	return extract_clock(l);
}


#ifdef CONFIG_X86_SPEEDSTEP_CENTRINO_ACPI

static struct acpi_processor_performance p;

/*
 * centrino_cpu_init_acpi - register with ACPI P-States library
 *
 * Register with the ACPI P-States library (part of drivers/acpi/processor.c)
 * in order to determine correct frequency and voltage pairings by reading
 * the _PSS of the ACPI DSDT or SSDT tables.
 */
static int centrino_cpu_init_acpi(struct cpufreq_policy *policy)
{
	union acpi_object		arg0 = {ACPI_TYPE_BUFFER};
	u32				arg0_buf[3];
	struct acpi_object_list		arg_list = {1, &arg0};
	unsigned long			cur_freq;
	int				result = 0, i;

	/* _PDC settings */
        arg0.buffer.length = 12;
        arg0.buffer.pointer = (u8 *) arg0_buf;
        arg0_buf[0] = ACPI_PDC_REVISION_ID;
        arg0_buf[1] = 1;
        arg0_buf[2] = ACPI_PDC_EST_CAPABILITY_SMP | ACPI_PDC_EST_CAPABILITY_MSR;

	p.pdc = &arg_list;

	/* register with ACPI core */
        if (acpi_processor_register_performance(&p, policy->cpu))
                return -EIO;

	/* verify the acpi_data */
	if (p.state_count <= 1) {
                printk(KERN_DEBUG "No P-States\n");
                result = -ENODEV;
                goto err_unreg;
	}

	if ((p.control_register.space_id != ACPI_ADR_SPACE_FIXED_HARDWARE) ||
	    (p.status_register.space_id != ACPI_ADR_SPACE_FIXED_HARDWARE)) {
		printk(KERN_DEBUG "Invalid control/status registers\n");
		result = -EIO;
		goto err_unreg;
	}

	for (i=0; i<p.state_count; i++) {
		if (p.states[i].control != p.states[i].status) {
			printk(KERN_DEBUG "Different control and status values\n");
			result = -EINVAL;
			goto err_unreg;
		}

		if (!p.states[i].core_frequency) {
			printk(KERN_DEBUG "Zero core frequency\n");
			result = -EINVAL;
			goto err_unreg;
		}

		if (p.states[i].core_frequency > p.states[0].core_frequency) {
			printk(KERN_DEBUG "P%u has larger frequency than P0, skipping\n", i);
			p.states[i].core_frequency = 0;
			continue;
		}
	}

	centrino_model = kmalloc(sizeof(struct cpu_model), GFP_KERNEL);
	if (!centrino_model) {
		result = -ENOMEM;
		goto err_unreg;
	}
	memset(centrino_model, 0, sizeof(struct cpu_model));

	centrino_model->model_name=NULL;
	centrino_model->max_freq = p.states[0].core_frequency * 1000;
	centrino_model->op_points =  kmalloc(sizeof(struct cpufreq_frequency_table) *
					     (p.state_count + 1), GFP_KERNEL);
        if (!centrino_model->op_points) {
                result = -ENOMEM;
                goto err_kfree;
        }

        for (i=0; i<p.state_count; i++) {
		centrino_model->op_points[i].index = p.states[i].control;
		centrino_model->op_points[i].frequency = p.states[i].core_frequency * 1000;
	}
	centrino_model->op_points[p.state_count].frequency = CPUFREQ_TABLE_END;

	cur_freq = get_cur_freq(policy->cpu);

	for (i=0; i<p.state_count; i++) {
		if (extract_clock(centrino_model->op_points[i].index) !=
		    (centrino_model->op_points[i].frequency)) {
			printk(KERN_DEBUG "Invalid encoded frequency\n");
			result = -EINVAL;
			goto err_kfree_all;
		}

		if (cur_freq == centrino_model->op_points[i].frequency)
			p.state = i;
		if (!p.states[i].core_frequency)
			centrino_model->op_points[i].frequency = CPUFREQ_ENTRY_INVALID;
	}

	return 0;

 err_kfree_all:
	kfree(centrino_model->op_points);
 err_kfree:
	kfree(centrino_model);
 err_unreg:
	acpi_processor_unregister_performance(&p, policy->cpu);
	return (result);
}
#else
static inline int centrino_cpu_init_acpi(struct cpufreq_policy *policy) { return -ENODEV; }
#endif

static int centrino_cpu_init(struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *cpu = &cpu_data[policy->cpu];
	unsigned freq;
	unsigned l, h;
	int ret;
	int i;

	/* Only Intel makes Enhanced Speedstep-capable CPUs */
	if (cpu->x86_vendor != X86_VENDOR_INTEL || !cpu_has(cpu, X86_FEATURE_EST))
		return -ENODEV;

	for (i = 0; i < N_IDS; i++)
		if (centrino_verify_cpu_id(cpu, &cpu_ids[i]))
			break;

	if (i != N_IDS)
		centrino_cpu = 1;

	if (centrino_cpu_init_acpi(policy)) {
		if (policy->cpu != 0)
			return -ENODEV;

		if (!centrino_cpu) {
			printk(KERN_INFO PFX "found unsupported CPU with "
			"Enhanced SpeedStep: send /proc/cpuinfo to "
			MAINTAINER "\n");
			return -ENODEV;
		}

		if (centrino_cpu_init_table(policy)) {
			return -ENODEV;
		}
	}

	/* Check to see if Enhanced SpeedStep is enabled, and try to
	   enable it if not. */
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);

	if (!(l & (1<<16))) {
		l |= (1<<16);
		wrmsr(MSR_IA32_MISC_ENABLE, l, h);

		/* check to see if it stuck */
		rdmsr(MSR_IA32_MISC_ENABLE, l, h);
		if (!(l & (1<<16))) {
			printk(KERN_INFO PFX "couldn't enable Enhanced SpeedStep\n");
			return -ENODEV;
		}
	}

	freq = get_cur_freq(policy->cpu);

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = 10000; /* 10uS transition latency */
	policy->cur = freq;

	dprintk(KERN_INFO PFX "centrino_cpu_init: policy=%d cur=%dkHz\n",
		policy->policy, policy->cur);

	ret = cpufreq_frequency_table_cpuinfo(policy, centrino_model->op_points);
	if (ret)
		return (ret);

	cpufreq_frequency_table_get_attr(centrino_model->op_points, policy->cpu);

	return 0;
}

static int centrino_cpu_exit(struct cpufreq_policy *policy)
{
	if (!centrino_model)
		return -ENODEV;

	cpufreq_frequency_table_put_attr(policy->cpu);

#ifdef CONFIG_X86_SPEEDSTEP_CENTRINO_ACPI
	if (!centrino_model->model_name) {
		acpi_processor_unregister_performance(&p, policy->cpu);
		kfree(centrino_model->op_points);
		kfree(centrino_model);
	}
#endif

	centrino_model = NULL;

	return 0;
}

/**
 * centrino_verify - verifies a new CPUFreq policy
 * @policy: new policy
 *
 * Limit must be within this model's frequency range at least one
 * border included.
 */
static int centrino_verify (struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, centrino_model->op_points);
}

/**
 * centrino_setpolicy - set a new CPUFreq policy
 * @policy: new policy
 * @target_freq: the target frequency
 * @relation: how that frequency relates to achieved frequency (CPUFREQ_RELATION_L or CPUFREQ_RELATION_H)
 *
 * Sets a new CPUFreq policy.
 */
static int centrino_target (struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	unsigned int    newstate = 0;
	unsigned int	msr, oldmsr, h;
	struct cpufreq_freqs	freqs;
	cpumask_t		saved_mask;
	int			retval;

	if (centrino_model == NULL)
		return -ENODEV;

	/*
	 * Support for SMP systems.
	 * Make sure we are running on the CPU that wants to change frequency
	 */
	saved_mask = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(policy->cpu));
	if (smp_processor_id() != policy->cpu) {
		return(-EAGAIN);
	}

	if (cpufreq_frequency_table_target(policy, centrino_model->op_points, target_freq,
					   relation, &newstate)) {
		retval = -EINVAL;
		goto migrate_end;
	}

	msr = centrino_model->op_points[newstate].index;
	rdmsr(MSR_IA32_PERF_CTL, oldmsr, h);

	if (msr == (oldmsr & 0xffff)) {
		retval = 0;
		goto migrate_end;
	}

	/* Hm, old frequency can either be the last value we put in
	   PERF_CTL, or whatever it is now. The trouble is that TM2
	   can change it behind our back, which means we never get to
	   see the speed change.  Reading back the current speed would
	   tell us something happened, but it may leave the things on
	   the notifier chain confused; we therefore stick to using
	   the last programmed speed rather than the current speed for
	   "old".

	   TODO: work out how the TCC interrupts work, and try to
	   catch the CPU changing things under us.
	*/
	freqs.cpu = policy->cpu;
	freqs.old = extract_clock(oldmsr);
	freqs.new = extract_clock(msr);

	dprintk(KERN_INFO PFX "target=%dkHz old=%d new=%d msr=%04x\n",
		target_freq, freqs.old, freqs.new, msr);

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* all but 16 LSB are "reserved", so treat them with
	   care */
	oldmsr &= ~0xffff;
	msr &= 0xffff;
	oldmsr |= msr;

	wrmsr(MSR_IA32_PERF_CTL, oldmsr, h);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	retval = 0;
migrate_end:
	set_cpus_allowed(current, saved_mask);
	return (retval);
}

static struct freq_attr* centrino_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver centrino_driver = {
	.name		= "centrino", /* should be speedstep-centrino,
					 but there's a 16 char limit */
	.init		= centrino_cpu_init,
	.exit		= centrino_cpu_exit,
	.verify		= centrino_verify,
	.target		= centrino_target,
	.get		= get_cur_freq,
	.attr           = centrino_attr,
	.owner		= THIS_MODULE,
};


/**
 * centrino_init - initializes the Enhanced SpeedStep CPUFreq driver
 *
 * Initializes the Enhanced SpeedStep support. Returns -ENODEV on
 * unsupported devices, -ENOENT if there's no voltage table for this
 * particular CPU model, -EINVAL on problems during initiatization,
 * and zero on success.
 *
 * This is quite picky.  Not only does the CPU have to advertise the
 * "est" flag in the cpuid capability flags, we look for a specific
 * CPU model and stepping, and we need to have the exact model name in
 * our voltage tables.  That is, be paranoid about not releasing
 * someone's valuable magic smoke.
 */
static int __init centrino_init(void)
{
	struct cpuinfo_x86 *cpu = cpu_data;

	if (!cpu_has(cpu, X86_FEATURE_EST))
		return -ENODEV;

	return cpufreq_register_driver(&centrino_driver);
}

static void __exit centrino_exit(void)
{
	cpufreq_unregister_driver(&centrino_driver);
}

MODULE_AUTHOR ("Jeremy Fitzhardinge <jeremy@goop.org>");
MODULE_DESCRIPTION ("Enhanced SpeedStep driver for Intel Pentium M processors.");
MODULE_LICENSE ("GPL");

late_initcall(centrino_init);
module_exit(centrino_exit);

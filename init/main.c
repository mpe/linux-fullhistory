/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org> 
 */

#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/initrd.h>
#include <linux/hdreg.h>
#include <linux/bootmem.h>
#include <linux/tty.h>
#include <linux/gfp.h>
#include <linux/percpu.h>
#include <linux/kernel_stat.h>
#include <linux/security.h>
#include <linux/workqueue.h>
#include <linux/profile.h>
#include <linux/rcupdate.h>
#include <linux/moduleparam.h>
#include <linux/writeback.h>
#include <linux/cpu.h>
#include <linux/efi.h>

#include <asm/io.h>
#include <asm/bugs.h>

/*
 * This is one of the first .c files built. Error out early
 * if we have compiler trouble..
 */
#if __GNUC__ == 2 && __GNUC_MINOR__ == 96
#ifdef CONFIG_FRAME_POINTER
#error This compiler cannot compile correctly with frame pointers enabled
#endif
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/smp.h>
#endif

/*
 * Versions of gcc older than that listed below may actually compile
 * and link okay, but the end product can have subtle run time bugs.
 * To avoid associated bogus bug reports, we flatly refuse to compile
 * with a gcc that is known to be too old from the very beginning.
 */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 95)
#error Sorry, your GCC is too old. It builds incorrect kernels.
#endif

extern char *linux_banner;

static int init(void *);

extern void init_IRQ(void);
extern void sock_init(void);
extern void fork_init(unsigned long);
extern void mca_init(void);
extern void sbus_init(void);
extern void sysctl_init(void);
extern void signals_init(void);
extern void buffer_init(void);
extern void pidhash_init(void);
extern void pidmap_init(void);
extern void pte_chain_init(void);
extern void radix_tree_init(void);
extern void free_initmem(void);
extern void populate_rootfs(void);
extern void driver_init(void);

#ifdef CONFIG_TC
extern void tc_init(void);
#endif

/*
 * Are we up and running (ie do we have all the infrastructure
 * set up)
 */
int system_running;

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS 8
#define MAX_INIT_ENVS 8

extern void time_init(void);
/* Default late time init is NULL. archs can override this later. */
void (*late_time_init)(void) = NULL;
extern void softirq_init(void);

static char *execute_command;

/* Setup configured maximum number of CPUs to activate */
static unsigned int max_cpus = NR_CPUS;

/*
 * Setup routine for controlling SMP activation
 *
 * Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 * activation entirely (the MPS table probe still happens, though).
 *
 * Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 * greater than 0, limits the maximum number of CPUs activated in
 * SMP mode to <NUM>.
 */
static int __init nosmp(char *str)
{
	max_cpus = 0;
	return 1;
}

__setup("nosmp", nosmp);

static int __init maxcpus(char *str)
{
	get_option(&str, &max_cpus);
	return 1;
}

__setup("maxcpus=", maxcpus);

static char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };

__setup("profile=", profile_setup);

static int __init obsolete_checksetup(char *line)
{
	struct obs_kernel_param *p;
	extern struct obs_kernel_param __setup_start, __setup_end;

	p = &__setup_start;
	do {
		int n = strlen(p->str);
		if (!strncmp(line,p->str,n)) {
			if (p->setup_func(line+n))
				return 1;
		}
		p++;
	} while (p < &__setup_end);
	return 0;
}

/* this should be approx 2 Bo*oMips to start (note initial shift), and will
   still work even if initially too large, it will just take slightly longer */
unsigned long loops_per_jiffy = (1<<12);

#ifndef __ia64__
EXPORT_SYMBOL(loops_per_jiffy);
#endif

/* This is the number of bits of precision for the loops_per_jiffy.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 8

void __init calibrate_delay(void)
{
	unsigned long ticks, loopbit;
	int lps_precision = LPS_PREC;

	loops_per_jiffy = (1<<12);

	printk("Calibrating delay loop... ");
	while (loops_per_jiffy <<= 1) {
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies)
			/* nothing */;
		/* Go .. */
		ticks = jiffies;
		__delay(loops_per_jiffy);
		ticks = jiffies - ticks;
		if (ticks)
			break;
	}

/* Do a binary approximation to get loops_per_jiffy set to equal one clock
   (up to lps_precision bits) */
	loops_per_jiffy >>= 1;
	loopbit = loops_per_jiffy;
	while ( lps_precision-- && (loopbit >>= 1) ) {
		loops_per_jiffy |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		__delay(loops_per_jiffy);
		if (jiffies != ticks)	/* longer than 1 tick */
			loops_per_jiffy &= ~loopbit;
	}

/* Round the value and print it */	
	printk("%lu.%02lu BogoMIPS\n",
		loops_per_jiffy/(500000/HZ),
		(loops_per_jiffy/(5000/HZ)) % 100);
}

static int __init debug_kernel(char *str)
{
	if (*str)
		return 0;
	console_loglevel = 10;
	return 1;
}

static int __init quiet_kernel(char *str)
{
	if (*str)
		return 0;
	console_loglevel = 4;
	return 1;
}

__setup("debug", debug_kernel);
__setup("quiet", quiet_kernel);

/* Unknown boot options get handed to init, unless they look like
   failed parameters */
static int __init unknown_bootoption(char *param, char *val)
{
	/* Change NUL term back to "=", to make "param" the whole string. */
	if (val)
		val[-1] = '=';

	/* Handle obsolete-style parameters */
	if (obsolete_checksetup(param))
		return 0;

	/* Preemptive maintenance for "why didn't my mispelled command
           line work?" */
	if (strchr(param, '.') && (!val || strchr(param, '.') < val)) {
		printk(KERN_ERR "Unknown boot option `%s': ignoring\n", param);
		return 0;
	}

	if (val) {
		/* Environment option */
		unsigned int i;
		for (i = 0; envp_init[i]; i++) {
			if (i == MAX_INIT_ENVS)
				panic("Too many boot env vars at `%s'", param);
		}
		envp_init[i] = param;
	} else {
		/* Command line option */
		unsigned int i;
		for (i = 0; argv_init[i]; i++) {
			if (i == MAX_INIT_ARGS)
				panic("Too many boot init vars at `%s'",param);
		}
		argv_init[i] = param;
	}
	return 0;
}

static int __init init_setup(char *str)
{
	unsigned int i;

	execute_command = str;
	/* In case LILO is going to boot us with default command line,
	 * it prepends "auto" before the whole cmdline which makes
	 * the shell think it should execute a script with such name.
	 * So we ignore all arguments entered _before_ init=... [MJ]
	 */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("init=", init_setup);

extern void setup_arch(char **);
extern void cpu_idle(void);

#ifndef CONFIG_SMP

#ifdef CONFIG_X86_LOCAL_APIC
static void __init smp_init(void)
{
	APIC_init_uniprocessor();
}
#else
#define smp_init()	do { } while (0)
#endif

static inline void setup_per_cpu_areas(void) { }
static inline void smp_prepare_cpus(unsigned int maxcpus) { }

#else

#ifdef __GENERIC_PER_CPU
unsigned long __per_cpu_offset[NR_CPUS];

EXPORT_SYMBOL(__per_cpu_offset);

static void __init setup_per_cpu_areas(void)
{
	unsigned long size, i;
	char *ptr;
	/* Created by linker magic */
	extern char __per_cpu_start[], __per_cpu_end[];

	/* Copy section for each CPU (we discard the original) */
	size = ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES);
#ifdef CONFIG_MODULES
	if (size < PERCPU_ENOUGH_ROOM)
		size = PERCPU_ENOUGH_ROOM;
#endif

	ptr = alloc_bootmem(size * NR_CPUS);

	for (i = 0; i < NR_CPUS; i++, ptr += size) {
		__per_cpu_offset[i] = ptr - __per_cpu_start;
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);
	}
}
#endif /* !__GENERIC_PER_CPU */

/* Called by boot processor to activate the rest. */
static void __init smp_init(void)
{
	unsigned int i;

	/* FIXME: This should be done in userspace --RR */
	for (i = 0; i < NR_CPUS; i++) {
		if (num_online_cpus() >= max_cpus)
			break;
		if (cpu_possible(i) && !cpu_online(i)) {
			printk("Bringing up %i\n", i);
			cpu_up(i);
		}
	}

	/* Any cleanup work */
	printk("CPUS done %u\n", max_cpus);
	smp_cpus_done(max_cpus);
#if 0
	/* Get other processors into their bootup holding patterns. */

	smp_threads_ready=1;
	smp_commence();
#endif
}

#endif

/*
 * We need to finalize in a non-__init function or else race conditions
 * between the root thread and the init thread may cause start_kernel to
 * be reaped by free_initmem before the root thread has proceeded to
 * cpu_idle.
 */

static void rest_init(void)
{
	kernel_thread(init, NULL, CLONE_FS | CLONE_SIGHAND);
	unlock_kernel();
 	cpu_idle();
} 

/*
 *	Activate the first processor.
 */

asmlinkage void __init start_kernel(void)
{
	char * command_line;
	extern char saved_command_line[];
	extern struct kernel_param __start___param[], __stop___param[];
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	lock_kernel();
	printk(linux_banner);
	setup_arch(&command_line);
	setup_per_cpu_areas();

	/*
	 * Mark the boot cpu "online" so that it can call console drivers in
	 * printk() and can access its per-cpu storage.
	 */
	smp_prepare_boot_cpu();

	build_all_zonelists();
	page_alloc_init();
	printk("Kernel command line: %s\n", saved_command_line);
	parse_args("Booting kernel", command_line, __start___param,
		   __stop___param - __start___param,
		   &unknown_bootoption);
	trap_init();
	rcu_init();
	init_IRQ();
	pidhash_init();
	sched_init();
	softirq_init();
	time_init();

	/*
	 * HACK ALERT! This is early. We're enabling the console before
	 * we've done PCI setups etc, and console_init() must be aware of
	 * this. But we do want output early, in case something goes wrong.
	 */
	console_init();
	profile_init();
	local_irq_enable();
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && !initrd_below_start_ok &&
			initrd_start < min_low_pfn << PAGE_SHIFT) {
		printk(KERN_CRIT "initrd overwritten (0x%08lx < 0x%08lx) - "
		    "disabling it.\n",initrd_start,min_low_pfn << PAGE_SHIFT);
		initrd_start = 0;
	}
#endif
	page_address_init();
	mem_init();
	kmem_cache_init();
	if (late_time_init)
		late_time_init();
	calibrate_delay();
	pidmap_init();
	pgtable_cache_init();
	pte_chain_init();
#ifdef CONFIG_X86
	if (efi_enabled)
		efi_enter_virtual_mode();
#endif
	fork_init(num_physpages);
	proc_caches_init();
	buffer_init();
	security_scaffolding_startup();
	vfs_caches_init(num_physpages);
	radix_tree_init();
	signals_init();
	/* rootfs populating might need page-writeback */
	page_writeback_init();
	populate_rootfs();
#ifdef CONFIG_PROC_FS
	proc_root_init();
#endif
	check_bugs();
	printk("POSIX conformance testing by UNIFIX\n");

	/* 
	 *	We count on the initial thread going ok 
	 *	Like idlers init is an unlocked kernel thread, which will
	 *	make syscalls (and thus be locked).
	 */
	init_idle(current, smp_processor_id());

	/* Do the rest non-__init'ed, we're now alive */
	rest_init();
}

static int __initdata initcall_debug;

static int __init initcall_debug_setup(char *str)
{
	initcall_debug = 1;
	return 1;
}
__setup("initcall_debug", initcall_debug_setup);

struct task_struct *child_reaper = &init_task;

extern initcall_t __initcall_start, __initcall_end;

static void __init do_initcalls(void)
{
	initcall_t *call;
	int count = preempt_count();

	for (call = &__initcall_start; call < &__initcall_end; call++) {
		char *msg;

		if (initcall_debug)
			printk("calling initcall 0x%p\n", *call);

		(*call)();

		msg = NULL;
		if (preempt_count() != count) {
			msg = "preemption imbalance";
			preempt_count() = count;
		}
		if (irqs_disabled()) {
			msg = "disabled interrupts";
			local_irq_enable();
		}
		if (msg) {
			printk("error in initcall at 0x%p: "
				"returned with %s\n", *call, msg);
		}
	}

	/* Make sure there is no pending stuff from the initcall sequence */
	flush_scheduled_work();
}

/*
 * Ok, the machine is now initialized. None of the devices
 * have been touched yet, but the CPU subsystem is up and
 * running, and memory and process management works.
 *
 * Now we can finally start doing some real work..
 */
static void __init do_basic_setup(void)
{
	driver_init();

#ifdef CONFIG_SYSCTL
	sysctl_init();
#endif

	/* Networking initialization needs a process context */ 
	sock_init();

	init_workqueues();
	do_initcalls();
}

static void do_pre_smp_initcalls(void)
{
	extern int spawn_ksoftirqd(void);
#ifdef CONFIG_SMP
	extern int migration_init(void);

	migration_init();
#endif
	node_nr_running_init();
	spawn_ksoftirqd();
}

static void run_init_process(char *init_filename)
{
	argv_init[0] = init_filename;
	execve(init_filename, argv_init, envp_init);
}

extern void prepare_namespace(void);

static int init(void * unused)
{
	lock_kernel();
	/*
	 * Tell the world that we're going to be the grim
	 * reaper of innocent orphaned children.
	 *
	 * We don't want people to have to make incorrect
	 * assumptions about where in the task array this
	 * can be found.
	 */
	child_reaper = current;

	/* Sets up cpus_possible() */
	smp_prepare_cpus(max_cpus);

	do_pre_smp_initcalls();

	smp_init();
	do_basic_setup();

	prepare_namespace();

	/*
	 * Ok, we have completed the initial bootup, and
	 * we're essentially up and running. Get rid of the
	 * initmem segments and start the user-mode stuff..
	 */
	free_initmem();
	unlock_kernel();
	system_running = 1;

	if (open("/dev/console", O_RDWR, 0) < 0)
		printk("Warning: unable to open an initial console.\n");

	(void) dup(0);
	(void) dup(0);
	
	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are 
	 * trying to recover a really broken machine.
	 */

	if (execute_command)
		run_init_process(execute_command);

	run_init_process("/sbin/init");
	run_init_process("/etc/init");
	run_init_process("/bin/init");
	run_init_process("/bin/sh");

	panic("No init found.  Try passing init= option to kernel.");
}

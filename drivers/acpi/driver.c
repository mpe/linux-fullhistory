/*
 *  driver.c - ACPI driver
 *
 *  Copyright (C) 2000 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include <asm/uaccess.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("driver")

struct acpi_run_entry
{
	void (*callback)(void*);
	void *context;
	struct tq_struct task;
};

static spinlock_t acpi_event_lock = SPIN_LOCK_UNLOCKED;
static volatile u32 acpi_event_status = 0;
static volatile acpi_sstate_t acpi_event_state = ACPI_S0;
static DECLARE_WAIT_QUEUE_HEAD(acpi_event_wait);

static volatile int acpi_thread_pid = -1;
static DECLARE_TASK_QUEUE(acpi_thread_run);
static DECLARE_WAIT_QUEUE_HEAD(acpi_thread_wait);

static struct ctl_table_header *acpi_sysctl = NULL;

/*
 * Examine/modify value
 */
static int 
acpi_do_ulong(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	char str[2 * sizeof(unsigned long) + 4], *strend;
	unsigned long val;
	int size;

	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}

		val = *(unsigned long *) ctl->data;
		size = sprintf(str, "0x%08lx\n", val);
		if (*len >= size) {
			copy_to_user(buffer, str, size);
			*len = size;
		}
		else
			*len = 0;
	}
	else {
		size = sizeof(str) - 1;
		if (size > *len)
			size = *len;
		copy_from_user(str, buffer, size);
		str[size] = '\0';
		val = simple_strtoul(str, &strend, 0);
		if (strend == str)
			return -EINVAL;
		*(unsigned long *) ctl->data = val;
	}

	file->f_pos += *len;
	return 0;
}

/*
 * Handle ACPI event
 */
static u32
acpi_event(void *context)
{
	unsigned long flags;
	int event = (int) context;
	int mask = 0;

	switch (event) {
	case ACPI_EVENT_POWER_BUTTON: mask = ACPI_PWRBTN; break;
	case ACPI_EVENT_SLEEP_BUTTON: mask = ACPI_SLPBTN; break;
	default: return AE_ERROR;
	}

	if (mask) {
		// notify process waiting on /dev/acpi
		spin_lock_irqsave(&acpi_event_lock, flags);
		acpi_event_status |= mask;
		spin_unlock_irqrestore(&acpi_event_lock, flags);
		acpi_event_state = acpi_sleep_state;
		wake_up_interruptible(&acpi_event_wait);
	}

	return AE_OK;
}

/*
 * Wait for next event
 */
static int
acpi_do_event(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	u32 event_status = 0;
	acpi_sstate_t event_state = 0;
	char str[27];
	int size;

	if (write)
		return -EPERM;
	if (*len < sizeof(str)) {
		*len = 0;
		return 0;
	}

	for (;;) {
		unsigned long flags;

		// we need an atomic exchange here
		spin_lock_irqsave(&acpi_event_lock, flags);
		event_status = acpi_event_status;
		acpi_event_status = 0;
		spin_unlock_irqrestore(&acpi_event_lock, flags);
		event_state = acpi_event_state;

		if (event_status)
			break;

		// wait for an event to arrive
		interruptible_sleep_on(&acpi_event_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	size = sprintf(str,
		       "0x%08x 0x%08x 0x%01x\n",
		       event_status,
		       0,
		       event_state);
	copy_to_user(buffer, str, size);
	*len = size;
	file->f_pos += size;

	return 0;
}

/*
 * Enter system sleep state
 */
static int 
acpi_do_sleep(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}
	}
	else {
#ifdef CONFIG_ACPI_S1_SLEEP
		int status = acpi_enter_sx(ACPI_S1);
		if (status)
			return status;
#endif
	}
	file->f_pos += *len;
	return 0;
}

/*
 * Run queued callback
 */
static void
acpi_run_exec(void *context)
{
	struct acpi_run_entry *entry
		= (struct acpi_run_entry*) context;
	(*entry->callback)(entry->context);
	kfree(entry);
}

/*
 * Queue for execution by the ACPI thread
 */
int
acpi_run(void (*callback)(void*), void *context)
{
	struct acpi_run_entry *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -1;

	memset(entry, 0, sizeof(entry));
	entry->callback = callback;
	entry->context = context;
	entry->task.routine = acpi_run_exec;
	entry->task.data = entry;

	queue_task(&entry->task, &acpi_thread_run);

	if (waitqueue_active(&acpi_thread_wait))
		wake_up(&acpi_thread_wait);

	return 0;
}

static struct ctl_table acpi_table[] =
{
	{ACPI_P_LVL2_LAT, "c2_exit_latency",
	 &acpi_c2_exit_latency, sizeof(acpi_c2_exit_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_ENTER_LVL2_LAT, "c2_enter_latency",
	 &acpi_c2_enter_latency, sizeof(acpi_c2_enter_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_P_LVL3_LAT, "c3_exit_latency",
	 &acpi_c3_exit_latency, sizeof(acpi_c3_exit_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_ENTER_LVL3_LAT, "c3_enter_latency",
	 &acpi_c3_enter_latency, sizeof(acpi_c3_enter_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_SLEEP, "sleep", NULL, 0, 0600, NULL, &acpi_do_sleep},

	{ACPI_EVENT, "event", NULL, 0, 0400, NULL, &acpi_do_event},

	{0}
};

static struct ctl_table acpi_dir_table[] =
{
	{CTL_ACPI, "acpi", NULL, 0, 0555, acpi_table},
	{0}
};

/*
 * Initialize and run interpreter within a kernel thread
 */
static int
acpi_thread(void *context)
{
	/*
	 * initialize
	 */
	exit_files(current);
	daemonize();
	strcpy(current->comm, "acpi");

	if (!ACPI_SUCCESS(acpi_initialize(NULL))) {
		printk(KERN_ERR "ACPI: initialize failed\n");
		return -ENODEV;
	}
	
	if (acpi_load_tables())
		return -ENODEV;

	if (PM_IS_ACTIVE()) {
		printk(KERN_NOTICE "ACPI: APM is already active.\n");
		acpi_terminate();
		return -ENODEV;
	}

	pm_active = 1;

	if (!ACPI_SUCCESS(acpi_enable())) {
		printk(KERN_ERR "ACPI: enable failed\n");
		acpi_terminate();
		return -ENODEV;
	}

	acpi_cpu_init();
	acpi_sys_init();
	acpi_ec_init();

	if (!ACPI_SUCCESS(acpi_install_fixed_event_handler(
		ACPI_EVENT_POWER_BUTTON,
		acpi_event,
		(void *) ACPI_EVENT_POWER_BUTTON))) {
		printk(KERN_ERR "ACPI: power button enable failed\n");
	}
	if (!ACPI_SUCCESS(acpi_install_fixed_event_handler(
		ACPI_EVENT_SLEEP_BUTTON,
		acpi_event,
		(void *) ACPI_EVENT_SLEEP_BUTTON))) {
		printk(KERN_ERR "ACPI: sleep button enable failed\n");
	}

	acpi_sysctl = register_sysctl_table(acpi_dir_table, 1);

	/*
	 * run
	 */
	for (;;) {
		interruptible_sleep_on(&acpi_thread_wait);
		if (signal_pending(current))
			break;
		do {
			run_task_queue(&acpi_thread_run);
		} while (acpi_thread_run);
	}

	/*
	 * terminate
	 */
	unregister_sysctl_table(acpi_sysctl);
	acpi_terminate();

	acpi_thread_pid = -1;

	return 0;
}

/*
 * Start the interpreter
 */
int __init
acpi_init(void)
{
	acpi_thread_pid = kernel_thread(acpi_thread,
					NULL,
					(CLONE_FS | CLONE_FILES
					 | CLONE_SIGHAND | SIGCHLD));
	return ((acpi_thread_pid >= 0) ? 0:-ENODEV);
}

/*
 * Terminate the interpreter
 */
void __exit
acpi_exit(void)
{
	int count;

	if (!kill_proc(acpi_thread_pid, SIGTERM, 1)) {
		// wait until thread terminates (at most 5 seconds)
		count = 5 * HZ;
		while (acpi_thread_pid >= 0 && --count) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
	}

	pm_idle = NULL;
	pm_power_off = NULL;
	pm_active = 0;
}

module_init(acpi_init);
module_exit(acpi_exit);

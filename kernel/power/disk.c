/*
 * kernel/power/disk.c - Suspend-to-disk support.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 *
 * This file is released under the GPLv2.
 *
 */

#include <linux/suspend.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include "power.h"


extern u32 pm_disk_mode;
extern struct pm_ops * pm_ops;

extern int swsusp_suspend(void);
extern int swsusp_write(void);
extern int swsusp_read(void);
extern int swsusp_resume(void);
extern int swsusp_free(void);


static int noresume = 0;
char resume_file[256] = CONFIG_PM_STD_PARTITION;

/**
 *	power_down - Shut machine down for hibernate.
 *	@mode:		Suspend-to-disk mode
 *
 *	Use the platform driver, if configured so, and return gracefully if it
 *	fails.
 *	Otherwise, try to power off and reboot. If they fail, halt the machine,
 *	there ain't no turning back.
 */

static int power_down(u32 mode)
{
	unsigned long flags;
	int error = 0;

	local_irq_save(flags);
	switch(mode) {
	case PM_DISK_PLATFORM:
		device_power_down(PM_SUSPEND_DISK);
		error = pm_ops->enter(PM_SUSPEND_DISK);
		break;
	case PM_DISK_SHUTDOWN:
		printk("Powering off system\n");
		device_shutdown();
		machine_power_off();
		break;
	case PM_DISK_REBOOT:
		device_shutdown();
		machine_restart(NULL);
		break;
	}
	machine_halt();
	/* Valid image is on the disk, if we continue we risk serious data corruption
	   after resume. */
	printk(KERN_CRIT "Please power me down manually\n");
	while(1);
	return 0;
}


static int in_suspend __nosavedata = 0;


/**
 *	free_some_memory -  Try to free as much memory as possible
 *
 *	... but do not OOM-kill anyone
 *
 *	Notice: all userland should be stopped at this point, or
 *	livelock is possible.
 */

static void free_some_memory(void)
{
	printk("Freeing memory: ");
	while (shrink_all_memory(10000))
		printk(".");
	printk("|\n");
}


static inline void platform_finish(void)
{
	if (pm_disk_mode == PM_DISK_PLATFORM) {
		if (pm_ops && pm_ops->finish)
			pm_ops->finish(PM_SUSPEND_DISK);
	}
}

static void finish(void)
{
	device_resume();
	platform_finish();
	enable_nonboot_cpus();
	thaw_processes();
	pm_restore_console();
}


static int prepare(void)
{
	int error;

	pm_prepare_console();

	sys_sync();
	if (freeze_processes()) {
		error = -EBUSY;
		goto Thaw;
	}

	if (pm_disk_mode == PM_DISK_PLATFORM) {
		if (pm_ops && pm_ops->prepare) {
			if ((error = pm_ops->prepare(PM_SUSPEND_DISK)))
				goto Thaw;
		}
	}

	/* Free memory before shutting down devices. */
	free_some_memory();

	disable_nonboot_cpus();
	if ((error = device_suspend(PM_SUSPEND_DISK)))
		goto Finish;

	return 0;
 Finish:
	platform_finish();
 Thaw:
	enable_nonboot_cpus();
	thaw_processes();
	pm_restore_console();
	return error;
}


/**
 *	pm_suspend_disk - The granpappy of power management.
 *
 *	If we're going through the firmware, then get it over with quickly.
 *
 *	If not, then call pmdis to do it's thing, then figure out how
 *	to power down the system.
 */

int pm_suspend_disk(void)
{
	int error;

	if ((error = prepare()))
		return error;

	pr_debug("PM: Attempting to suspend to disk.\n");
	if (pm_disk_mode == PM_DISK_FIRMWARE)
		return pm_ops->enter(PM_SUSPEND_DISK);

	pr_debug("PM: snapshotting memory.\n");
	in_suspend = 1;
	if ((error = swsusp_suspend()))
		goto Done;

	if (in_suspend) {
		pr_debug("PM: writing image.\n");

		/*
		 * FIXME: Leftover from swsusp. Are they necessary?
		 */
		mb();
		barrier();

		error = swsusp_write();
		if (!error) {
			error = power_down(pm_disk_mode);
			pr_debug("PM: Power down failed.\n");
		}
	} else
		pr_debug("PM: Image restored successfully.\n");
	swsusp_free();
 Done:
	finish();
	return error;
}


/**
 *	software_resume - Resume from a saved image.
 *
 *	Called as a late_initcall (so all devices are discovered and
 *	initialized), we call pmdisk to see if we have a saved image or not.
 *	If so, we quiesce devices, the restore the saved image. We will
 *	return above (in pm_suspend_disk() ) if everything goes well.
 *	Otherwise, we fail gracefully and return to the normally
 *	scheduled program.
 *
 */

static int software_resume(void)
{
	int error;

	if (noresume) {
		/**
		 * FIXME: If noresume is specified, we need to find the partition
		 * and reset it back to normal swap space.
		 */
		return 0;
	}

	pr_debug("PM: Reading pmdisk image.\n");

	if ((error = swsusp_read()))
		goto Done;

	pr_debug("PM: Preparing system for restore.\n");

	if ((error = prepare()))
		goto Free;

	barrier();
	mb();

	pr_debug("PM: Restoring saved image.\n");
	swsusp_resume();
	pr_debug("PM: Restore failed, recovering.n");
	finish();
 Free:
	swsusp_free();
 Done:
	pr_debug("PM: Resume from disk failed.\n");
	return 0;
}

late_initcall(software_resume);


static char * pm_disk_modes[] = {
	[PM_DISK_FIRMWARE]	= "firmware",
	[PM_DISK_PLATFORM]	= "platform",
	[PM_DISK_SHUTDOWN]	= "shutdown",
	[PM_DISK_REBOOT]	= "reboot",
};

/**
 *	disk - Control suspend-to-disk mode
 *
 *	Suspend-to-disk can be handled in several ways. The greatest
 *	distinction is who writes memory to disk - the firmware or the OS.
 *	If the firmware does it, we assume that it also handles suspending
 *	the system.
 *	If the OS does it, then we have three options for putting the system
 *	to sleep - using the platform driver (e.g. ACPI or other PM registers),
 *	powering off the system or rebooting the system (for testing).
 *
 *	The system will support either 'firmware' or 'platform', and that is
 *	known a priori (and encoded in pm_ops). But, the user may choose
 *	'shutdown' or 'reboot' as alternatives.
 *
 *	show() will display what the mode is currently set to.
 *	store() will accept one of
 *
 *	'firmware'
 *	'platform'
 *	'shutdown'
 *	'reboot'
 *
 *	It will only change to 'firmware' or 'platform' if the system
 *	supports it (as determined from pm_ops->pm_disk_mode).
 */

static ssize_t disk_show(struct subsystem * subsys, char * buf)
{
	return sprintf(buf,"%s\n",pm_disk_modes[pm_disk_mode]);
}


static ssize_t disk_store(struct subsystem * s, const char * buf, size_t n)
{
	int error = 0;
	int i;
	int len;
	char *p;
	u32 mode = 0;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	down(&pm_sem);
	for (i = PM_DISK_FIRMWARE; i < PM_DISK_MAX; i++) {
		if (!strncmp(buf, pm_disk_modes[i], len)) {
			mode = i;
			break;
		}
	}
	if (mode) {
		if (mode == PM_DISK_SHUTDOWN || mode == PM_DISK_REBOOT)
			pm_disk_mode = mode;
		else {
			if (pm_ops && pm_ops->enter &&
			    (mode == pm_ops->pm_disk_mode))
				pm_disk_mode = mode;
			else
				error = -EINVAL;
		}
	} else
		error = -EINVAL;

	pr_debug("PM: suspend-to-disk mode set to '%s'\n",
		 pm_disk_modes[mode]);
	up(&pm_sem);
	return error ? error : n;
}

power_attr(disk);

static struct attribute * g[] = {
	&disk_attr.attr,
	NULL,
};


static struct attribute_group attr_group = {
	.attrs = g,
};


static int __init pm_disk_init(void)
{
	return sysfs_create_group(&power_subsys.kset.kobj,&attr_group);
}

core_initcall(pm_disk_init);


static int __init resume_setup(char *str)
{
	if (noresume)
		return 1;

	strncpy( resume_file, str, 255 );
	return 1;
}

static int __init noresume_setup(char *str)
{
	noresume = 1;
	return 1;
}

__setup("noresume", noresume_setup);
__setup("resume=", resume_setup);

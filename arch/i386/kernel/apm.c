/* -*- linux-c -*-
 * APM BIOS driver for Linux
 * Copyright 1994-1998 Stephen Rothwell
 *                     (Stephen.Rothwell@canb.auug.org.au)
 * Development of this driver was funded by NEC Australia P/L
 *	and NEC Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * October 1995, Rik Faith (faith@cs.unc.edu):
 *    Minor enhancements and updates (to the patch set) for 1.3.x
 *    Documentation
 * January 1996, Rik Faith (faith@cs.unc.edu):
 *    Make /proc/apm easy to format (bump driver version)
 * March 1996, Rik Faith (faith@cs.unc.edu):
 *    Prohibit APM BIOS calls unless apm_enabled.
 *    (Thanks to Ulrich Windl <Ulrich.Windl@rz.uni-regensburg.de>)
 * April 1996, Stephen Rothwell (Stephen.Rothwell@canb.auug.org.au)
 *    Version 1.0 and 1.1
 * May 1996, Version 1.2
 * Feb 1998, Version 1.3
 * Feb 1998, Version 1.4
 * Aug 1998, Version 1.5
 * Sep 1998, Version 1.6
 * Nov 1998, Version 1.7
 * Jan 1999, Version 1.8
 * Jan 1999, Version 1.9
 *
 * History:
 *    0.6b: first version in official kernel, Linux 1.3.46
 *    0.7: changed /proc/apm format, Linux 1.3.58
 *    0.8: fixed gcc 2.7.[12] compilation problems, Linux 1.3.59
 *    0.9: only call bios if bios is present, Linux 1.3.72
 *    1.0: use fixed device number, consolidate /proc/apm into this file,
 *         Linux 1.3.85
 *    1.1: support user-space standby and suspend, power off after system
 *         halted, Linux 1.3.98
 *    1.2: When resetting RTC after resume, take care so that the time
 *         is only incorrect by 30-60mS (vs. 1S previously) (Gabor J. Toth
 *         <jtoth@princeton.edu>); improve interaction between
 *         screen-blanking and gpm (Stephen Rothwell); Linux 1.99.4
 *    1.2a:Simple change to stop mysterious bug reports with SMP also added
 *	   levels to the printk calls. APM is not defined for SMP machines.
 *         The new replacment for it is, but Linux doesn't yet support this.
 *         Alan Cox Linux 2.1.55
 *    1.3: Set up a valid data descriptor 0x40 for buggy BIOS's
 *    1.4: Upgraded to support APM 1.2. Integrated ThinkPad suspend patch by 
 *         Dean Gaudet <dgaudet@arctic.org>.
 *         C. Scott Ananian <cananian@alumni.princeton.edu> Linux 2.1.87
 *    1.5: Fix segment register reloading (in case of bad segments saved
 *         across BIOS call).
 *         Stephen Rothwell
 *    1.6: Cope with complier/assembler differences.
 *         Only try to turn off the first display device.
 *         Fix OOPS at power off with no APM BIOS by Jan Echternach
 *                   <echter@informatik.uni-rostock.de>
 *         Stephen Rothwell
 *    1.7: Modify driver's cached copy of the disabled/disengaged flags
 *         to reflect current state of APM BIOS.
 *         Chris Rankin <rankinc@bellsouth.net>
 *         Reset interrupt 0 timer to 100Hz after suspend
 *         Chad Miller <cmiller@surfsouth.com>
 *         Add CONFIG_APM_IGNORE_SUSPEND_BOUNCE
 *         Richard Gooch <rgooch@atnf.csiro.au>
 *         Allow boot time disabling of APM
 *         Make boot messages far less verbose by default
 *         Make asm safer
 *         Stephen Rothwell
 *    1.8: Add CONFIG_APM_RTC_IS_GMT
 *         Richard Gooch <rgooch@atnf.csiro.au>
 *         change APM_NOINTS to CONFIG_APM_ALLOW_INTS
 *         remove dependency on CONFIG_PROC_FS
 *         Stephen Rothwell
 *    1.9: Fix small typo.  <laslo@ilo.opole.pl>
 *         Try to cope with BIOS's that need to have all display
 *         devices blanked and not just the first one.
 *         Ross Paterson <ross@soi.city.ac.uk>
 *         Fix segment limit setting it has always been wrong as
 *         the segments needed to have byte granularity.
 *         Mark a few things __init.
 *         Add hack to allow power off of SMP systems by popular request.
 *         Use CONFIG_SMP instead of __SMP__
 *         Ignore BOUNCES for three seconds.
 *         Stephen Rothwell
 *
 * APM 1.1 Reference:
 *
 *   Intel Corporation, Microsoft Corporation. Advanced Power Management
 *   (APM) BIOS Interface Specification, Revision 1.1, September 1993.
 *   Intel Order Number 241704-001.  Microsoft Part Number 781-110-X01.
 *
 * [This document is available free from Intel by calling 800.628.8686 (fax
 * 916.356.6100) or 800.548.4725; or via anonymous ftp from
 * ftp://ftp.intel.com/pub/IAL/software_specs/apmv11.doc.  It is also
 * available from Microsoft by calling 206.882.8080.]
 *
 * APM 1.2 Reference:
 *   Intel Corporation, Microsoft Corporation. Advanced Power Management
 *   (APM) BIOS Interface Specification, Revision 1.2, February 1996.
 *
 * [This document is available from Intel at:
 *    http://www.intel.com/IAL/powermgm
 *  or Microsoft at
 *    http://www.microsoft.com/windows/thirdparty/hardware/pcfuture.htm
 * ]
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/poll.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/fcntl.h>
#include <linux/malloc.h>
#include <linux/linkage.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/apm_bios.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/desc.h>

EXPORT_SYMBOL(apm_register_callback);
EXPORT_SYMBOL(apm_unregister_callback);

extern unsigned long get_cmos_time(void);

/*
 * The apm_bios device is one of the misc char devices.
 * This is its minor number.
 */
#define	APM_MINOR_DEV	134

/* Configurable options:
 *
 * CONFIG_APM_IGNORE_USER_SUSPEND: define to ignore USER SUSPEND requests.
 * This is necessary on the NEC Versa M series, which generates these when
 * resuming from SYSTEM SUSPEND.  However, enabling this on other laptops
 * will cause the laptop to generate a CRITICAL SUSPEND when an appropriate
 * USER SUSPEND is ignored -- this may prevent the APM driver from updating
 * the system time on a RESUME.
 *
 * CONFIG_APM_DO_ENABLE: enable APM features at boot time.  From page 36 of
 * the specification: "When disabled, the APM BIOS does not automatically
 * power manage devices, enter the Standby State, enter the Suspend State,
 * or take power saving steps in response to CPU Idle calls."  This driver
 * will make CPU Idle calls when Linux is idle (unless this feature is
 * turned off -- see below).  This should always save battery power, but
 * more complicated APM features will be dependent on your BIOS
 * implementation.  You may need to turn this option off if your computer
 * hangs at boot time when using APM support, or if it beeps continuously
 * instead of suspending.  Turn this off if you have a NEC UltraLite Versa
 * 33/C or a Toshiba T400CDT.  This is off by default since most machines
 * do fine without this feature.
 *
 * CONFIG_APM_CPU_IDLE: enable calls to APM CPU Idle/CPU Busy inside the
 * idle loop.  On some machines, this can activate improved power savings,
 * such as a slowed CPU clock rate, when the machine is idle.  These idle
 * call is made after the idle loop has run for some length of time (e.g.,
 * 333 mS).  On some machines, this will cause a hang at boot time or
 * whenever the CPU becomes idle.
 *
 * CONFIG_APM_DISPLAY_BLANK: enable console blanking using the APM.  Some
 * laptops can use this to turn of the LCD backlight when the VC screen
 * blanker blanks the screen.  Note that this is only used by the VC screen
 * blanker, and probably won't turn off the backlight when using X11.  Some
 * problems have been reported when using this option with gpm (if you'd
 * like to debug this, please do so).
 *
 * CONFIG_APM_IGNORE_MULTIPLE_SUSPEND: The IBM TP560 bios seems to insist
 * on returning multiple suspend/standby events whenever one occurs.  We
 * really only need one at a time, so just ignore any beyond the first.
 * This is probably safe on most laptops.
 *
 * If you are debugging the APM support for your laptop, note that code for
 * all of these options is contained in this file, so you can #define or
 * #undef these on the next line to avoid recompiling the whole kernel.
 *
 */

/* KNOWN PROBLEM MACHINES:
 *
 * U: TI 4000M TravelMate: BIOS is *NOT* APM compliant
 *                         [Confirmed by TI representative]
 * ?: ACER 486DX4/75: uses dseg 0040, in violation of APM specification
 *                    [Confirmed by BIOS disassembly]
 *                    [This may work now ...]
 * P: Toshiba 1950S: battery life information only gets updated after resume
 * P: Midwest Micro Soundbook Elite DX2/66 monochrome: screen blanking
 * 	broken in BIOS [Reported by Garst R. Reese <reese@isn.net>]
 *
 * Legend: U = unusable with APM patches
 *         P = partially usable with APM patches
 */

/*
 * Define to have debug messages.
 */
#undef APM_DEBUG

/*
 * Define to always call the APM BIOS busy routine even if the clock was
 * not slowed by the idle routine.
 */
#define ALWAYS_CALL_BUSY

/*
 * Define to make the APM BIOS calls zero all data segment registers (so
 * that an incorrect BIOS implementation will cause a kernel panic if it
 * tries to write to arbitrary memory).
 */
#define APM_ZERO_SEGS

/*
 * Define to make all _set_limit calls use 64k limits.  The APM 1.1 BIOS is
 * supposed to provide limit information that it recognizes.  Many machines
 * do this correctly, but many others do not restrict themselves to their
 * claimed limit.  When this happens, they will cause a segmentation
 * violation in the kernel at boot time.  Most BIOS's, however, will
 * respect a 64k limit, so we use that.  If you want to be pedantic and
 * hold your BIOS to its claims, then undefine this.
 */
#define APM_RELAX_SEGMENTS

/*
 * Define to re-initialize the interrupt 0 timer to 100 Hz after a suspend.
 * This patched by Chad Miller <cmiller@surfsouth.com>, orig code by David 
 * Chen <chen@ctpa04.mit.edu>
 */
#undef INIT_TIMER_AFTER_SUSPEND

#ifdef INIT_TIMER_AFTER_SUSPEND
#include <linux/timex.h>
#include <asm/io.h>
#include <linux/delay.h>
#endif

/*
 * Need to poll the APM BIOS every second
 */
#define APM_CHECK_TIMEOUT	(HZ)

/*
 * If CONFIG_APM_IGNORE_SUSPEND_BOUNCE is defined then
 * ignore suspend events for this amount of time
 */
#define BOUNCE_INTERVAL		(3 * HZ)

/*
 * Save a segment register away
 */
#define savesegment(seg, where) \
		__asm__ __volatile__("movl %%" #seg ",%0" : "=m" (where))

/*
 * Forward declarations
 */
static void	suspend(void);
static void	standby(void);
static void	set_time(void);

static void	check_events(void);
static void	do_apm_timer(unsigned long);

static int	do_open(struct inode *, struct file *);
static int	do_release(struct inode *, struct file *);
static ssize_t	do_read(struct file *, char *, size_t , loff_t *);
static unsigned int do_poll(struct file *, poll_table *);
static int	do_ioctl(struct inode *, struct file *, u_int, u_long);

static int	apm_get_info(char *, char **, off_t, int, int);

extern int	apm_register_callback(int (*)(apm_event_t));
extern void	apm_unregister_callback(int (*)(apm_event_t));

/*
 * Local variables
 */
static asmlinkage struct {
	unsigned long	offset;
	unsigned short	segment;
}				apm_bios_entry;
static int			apm_enabled = 0;
static int			smp_hack = 0;
#ifdef CONFIG_APM_CPU_IDLE
static int			clock_slowed = 0;
#endif
static int			suspends_pending = 0;
static int			standbys_pending = 0;
#ifdef CONFIG_APM_IGNORE_MULTIPLE_SUSPEND
static int			waiting_for_resume = 0;
#endif

#ifdef CONFIG_APM_RTC_IS_GMT
#	define	clock_cmos_diff	0
#	define	got_clock_diff	1
#else
static long			clock_cmos_diff;
static int			got_clock_diff = 0;
#endif
static int			debug = 0;
static int			apm_disabled = 0;

static struct wait_queue *	process_list = NULL;
static struct apm_bios_struct *	user_list = NULL;

static struct timer_list	apm_timer;

static char			driver_version[] = "1.9";	/* no spaces */

#ifdef APM_DEBUG
static char *	apm_event_name[] = {
	"system standby",
	"system suspend",
	"normal resume",
	"critical resume",
	"low battery",
	"power status change",
	"update time",
	"critical suspend",
	"user standby",
	"user suspend",
	"system standby resume",
	"capabilities change"
};
#define NR_APM_EVENT_NAME	\
		(sizeof(apm_event_name) / sizeof(apm_event_name[0]))
#endif

static struct file_operations apm_bios_fops = {
	NULL,		/* lseek */
	do_read,
	NULL,		/* write */
	NULL,		/* readdir */
	do_poll,
	do_ioctl,
	NULL,		/* mmap */
	do_open,
	NULL,		/* flush */
	do_release,
	NULL,		/* fsync */
	NULL		/* fasync */
};

static struct miscdevice apm_device = {
	APM_MINOR_DEV,
	"apm",
	&apm_bios_fops
};

typedef struct callback_list_t {
	int (*				callback)(apm_event_t);
	struct callback_list_t *	next;
} callback_list_t;

static callback_list_t *	callback_list = NULL;

typedef struct lookup_t {
	int	key;
	char *	msg;
} lookup_t;

static const lookup_t error_table[] = {
/* N/A	{ APM_SUCCESS,		"Operation succeeded" }, */
	{ APM_DISABLED,		"Power management disabled" },
	{ APM_CONNECTED,	"Real mode interface already connected" },
	{ APM_NOT_CONNECTED,	"Interface not connected" },
	{ APM_16_CONNECTED,	"16 bit interface already connected" },
/* N/A	{ APM_16_UNSUPPORTED,	"16 bit interface not supported" }, */
	{ APM_32_CONNECTED,	"32 bit interface already connected" },
	{ APM_32_UNSUPPORTED,	"32 bit interface not supported" },
	{ APM_BAD_DEVICE,	"Unrecognized device ID" },
	{ APM_BAD_PARAM,	"Parameter out of range" },
	{ APM_NOT_ENGAGED,	"Interface not engaged" },
	{ APM_BAD_FUNCTION,     "Function not supported" },
	{ APM_RESUME_DISABLED,	"Resume timer disabled" },
	{ APM_BAD_STATE,	"Unable to enter requested state" },
/* N/A	{ APM_NO_EVENTS,	"No events pending" }, */
	{ APM_NOT_PRESENT,	"No APM present" }
};
#define ERROR_COUNT	(sizeof(error_table)/sizeof(lookup_t))

/*
 * These are the actual BIOS calls.  Depending on APM_ZERO_SEGS and
 * CONFIG_APM_ALLOW_INTS, we are being really paranoid here!  Not only
 * are interrupts disabled, but all the segment registers (except SS)
 * are saved and zeroed this means that if the BIOS tries to reference
 * any data without explicitly loading the segment registers, the kernel
 * will fault immediately rather than have some unforeseen circumstances
 * for the rest of the kernel.  And it will be very obvious!  :-) Doing
 * this depends on CS referring to the same physical memory as DS so that
 * DS can be zeroed before the call. Unfortunately, we can't do anything
 * about the stack segment/pointer.  Also, we tell the compiler that
 * everything could change.
 *
 * Also, we KNOW that for the non error case of apm_bios_call, there
 * is no useful data returned in the low order 8 bits of eax.
 */
#ifndef CONFIG_APM_ALLOW_INTS
#	define APM_DO_CLI	__cli()
#else
#	define APM_DO_CLI
#endif
#ifdef APM_ZERO_SEGS
#	define APM_DO_SAVE_SEGS \
		savesegment(fs, saved_fs); \
		savesegment(gs, saved_gs)
#	define APM_DO_ZERO_SEGS \
		"pushl %%ds\n\t" \
		"pushl %%es\n\t" \
		"xorl %%edx, %%edx\n\t" \
		"mov %%dx, %%ds\n\t" \
		"mov %%dx, %%es\n\t" \
		"mov %%dx, %%fs\n\t" \
		"mov %%dx, %%gs\n\t"
#	define APM_DO_POP_SEGS \
		"popl %%es\n\t" \
		"popl %%ds\n\t"
#	define APM_DO_RESTORE_SEGS \
		loadsegment(fs, saved_fs); \
		loadsegment(gs, saved_gs)
#else
#	define APM_DO_SAVE_SEGS
#	define APM_DO_ZERO_SEGS
#	define APM_DO_POP_SEGS
#	define APM_DO_RESTORE_SEGS
#endif

static u8 apm_bios_call(u32 eax_in, u32 ebx_in, u32 ecx_in,
	u32 *eax, u32 *ebx, u32 *ecx, u32 *edx, u32 *esi)
{
	unsigned int	saved_fs;
	unsigned int	saved_gs;
	unsigned long	flags;

	__save_flags(flags);
	APM_DO_CLI;
	APM_DO_SAVE_SEGS;
	__asm__ __volatile__(APM_DO_ZERO_SEGS
		"pushl %%edi\n\t"
		"pushl %%ebp\n\t"
		"lcall %%cs:" SYMBOL_NAME_STR(apm_bios_entry) "\n\t"
		"setc %%al\n\t"
		"popl %%ebp\n\t"
		"popl %%edi\n\t"
		APM_DO_POP_SEGS
		: "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx),
		  "=S" (*esi)
		: "a" (eax_in), "b" (ebx_in), "c" (ecx_in)
		: "memory", "cc");
	APM_DO_RESTORE_SEGS;
	__restore_flags(flags);
	return *eax & 0xff;
}

/*
 * This version only returns one value (usually an error code)
 */

static u8 apm_bios_call_simple(u32 eax_in, u32 ebx_in, u32 ecx_in,
	u32 *eax)
{
	u8		error;
	unsigned int	saved_fs;
	unsigned int	saved_gs;
	unsigned long	flags;

	__save_flags(flags);
	APM_DO_CLI;
	APM_DO_SAVE_SEGS;
	{
		int	cx, dx, si;

		__asm__ __volatile__(APM_DO_ZERO_SEGS
			"pushl %%edi\n\t"
			"pushl %%ebp\n\t"
			"lcall %%cs:" SYMBOL_NAME_STR(apm_bios_entry) "\n\t"
			"setc %%bl\n\t"
			"popl %%ebp\n\t"
			"popl %%edi\n\t"
			APM_DO_POP_SEGS
			: "=a" (*eax), "=b" (error), "=c" (cx), "=d" (dx),
			  "=S" (si)
			: "a" (eax_in), "b" (ebx_in), "c" (ecx_in)
			: "memory", "cc");
	}
	APM_DO_RESTORE_SEGS;
	__restore_flags(flags);
	return error;
}

static int apm_driver_version(u_short *val)
{
	u32	eax;

	if (apm_bios_call_simple(0x530e, 0, *val, &eax))
		return (eax >> 8) & 0xff;
	*val = eax;
	return APM_SUCCESS;
}

static int apm_get_event(apm_event_t *event, apm_eventinfo_t *info)
{
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	dummy;

	if (apm_bios_call(0x530b, 0, 0, &eax, &ebx, &ecx, &dummy, &dummy))
		return (eax >> 8) & 0xff;
	*event = ebx;
	if (apm_bios_info.version < 0x0102)
		*info = ~0; /* indicate info not valid */
	else
		*info = ecx;
	return APM_SUCCESS;
}

static int set_power_state(u_short what, u_short state)
{
	u32	eax;

	if (apm_bios_call_simple(0x5307, what, state, &eax))
		return (eax >> 8) & 0xff;
	return APM_SUCCESS;
}

static int apm_set_power_state(u_short state)
{
	return set_power_state(0x0001, state);
}

void apm_power_off(void)
{
	/*
	 * smp_hack == 2 means that we would have enabled APM support
	 * except there is more than one processor and so most of
	 * the APM stuff is unsafe.  We will still try power down
	 * because is is useful to some people and they know what
	 * they are doing because they booted with the smp-power-off
	 * kernel option.
	 */
	if (apm_enabled || (smp_hack == 2))
		(void) apm_set_power_state(APM_STATE_OFF);
}

#ifdef CONFIG_APM_DISPLAY_BLANK
/* Called by apm_display_blank and apm_display_unblank when apm_enabled. */
static int apm_set_display_power_state(u_short state)
{
	int	error;

	/* Blank the first display device */
	error = set_power_state(0x0100, state);
	if (error == APM_BAD_DEVICE)
		/* try to blank them all instead */
		error = set_power_state(0x01ff, state);
	return error;
}
#endif

#ifdef CONFIG_APM_DO_ENABLE
static int __init apm_enable_power_management(void)
{
	u32	eax;

	if (apm_bios_call_simple(0x5308,
			(apm_bios_info.version > 0x100) ? 0x0001 : 0xffff,
			1, &eax))
		return (eax >> 8) & 0xff;
	apm_bios_info.flags &= ~APM_BIOS_DISABLED;
	return APM_SUCCESS;
}
#endif

static int apm_get_power_status(u_short *status, u_short *bat, u_short *life)
{
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	edx;
	u32	dummy;

	if (apm_bios_call(0x530a, 1, 0, &eax, &ebx, &ecx, &edx, &dummy))
		return (eax >> 8) & 0xff;
	*status = ebx;
	*bat = ecx;
	*life = edx;
	return APM_SUCCESS;
}

static int apm_get_battery_status(u_short which, u_short *status,
				  u_short *bat, u_short *life, u_short *nbat)
{
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	edx;
	u32	esi;

	if (apm_bios_info.version < 0x0102) {
		/* pretend we only have one battery. */
		if (which != 1)
			return APM_BAD_DEVICE;
		*nbat = 1;
		return apm_get_power_status(status, bat, life);
	}

	if (apm_bios_call(0x530a, (0x8000 | (which)), 0, &eax,
			&ebx, &ecx, &edx, &esi))
		return (eax >> 8) & 0xff;
	*status = ebx;
	*bat = ecx;
	*life = edx;
	*nbat = esi;
	return APM_SUCCESS;
}

static int __init apm_engage_power_management(u_short device)
{
	u32	eax;

	if (apm_bios_call_simple(0x530f, device, 1, &eax))
		return (eax >> 8) & 0xff;
	return APM_SUCCESS;
}

static void apm_error(char *str, int err)
{
	int	i;

	for (i = 0; i < ERROR_COUNT; i++)
		if (error_table[i].key == err) break;
	if (i < ERROR_COUNT)
		printk(KERN_NOTICE "apm: %s: %s\n", str, error_table[i].msg);
	else
		printk(KERN_NOTICE "apm: %s: unknown error code %#2.2x\n",
			str, err);
}

/* Called from console driver -- must make sure apm_enabled. */
int apm_display_blank(void)
{
#ifdef CONFIG_APM_DISPLAY_BLANK
	int	error;

	if (!apm_enabled)
		return 0;
	error = apm_set_display_power_state(APM_STATE_STANDBY);
	if (error == APM_SUCCESS)
		return 1;
	apm_error("set display standby", error);
#endif
	return 0;
}

/* Called from console driver -- must make sure apm_enabled. */
int apm_display_unblank(void)
{
#ifdef CONFIG_APM_DISPLAY_BLANK
	int error;

	if (!apm_enabled)
		return 0;
	error = apm_set_display_power_state(APM_STATE_READY);
	if (error == APM_SUCCESS)
		return 1;
	apm_error("set display ready", error);
#endif
	return 0;
}

int apm_register_callback(int (*callback)(apm_event_t))
{
	callback_list_t *	new;

	new = kmalloc(sizeof(callback_list_t), GFP_KERNEL);
	if (new == NULL)
		return -ENOMEM;
	new->callback = callback;
	new->next = callback_list;
	callback_list = new;
	return 0;
}

void apm_unregister_callback(int (*callback)(apm_event_t))
{
	callback_list_t **	ptr;
	callback_list_t *	old;

	for (ptr = &callback_list; *ptr != NULL; ptr = &(*ptr)->next)
		if ((*ptr)->callback == callback)
			break;
	old = *ptr;
	*ptr = old->next;
	kfree_s(old, sizeof(callback_list_t));
}

static int queue_empty(struct apm_bios_struct * as)
{
	return as->event_head == as->event_tail;
}

static apm_event_t get_queued_event(struct apm_bios_struct * as)
{
	as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
	return as->events[as->event_tail];
}

static int queue_event(apm_event_t event, struct apm_bios_struct *sender)
{
	struct apm_bios_struct *	as;

	if (user_list == NULL)
		return 0;
	for (as = user_list; as != NULL; as = as->next) {
		if (as == sender)
			continue;
		as->event_head = (as->event_head + 1) % APM_MAX_EVENTS;
		if (as->event_head == as->event_tail) {
			static int notified;

			if (notified == 0) {
			    printk(KERN_ERR "apm: an event queue overflowed\n");
			    notified = 1;
			}
			as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
		}
		as->events[as->event_head] = event;
		if (!as->suser)
			continue;
		switch (event) {
		case APM_SYS_SUSPEND:
		case APM_USER_SUSPEND:
			as->suspends_pending++;
			suspends_pending++;
			break;

		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
			as->standbys_pending++;
			standbys_pending++;
			break;
		}
	}
	wake_up_interruptible(&process_list);
	return 1;
}

static void set_time(void)
{
	unsigned long	flags;

	if (!got_clock_diff)	/* Don't know time zone, can't set clock */
		return;

	save_flags(flags);
	cli();
	CURRENT_TIME = get_cmos_time() + clock_cmos_diff;
	restore_flags(flags);
}

static void suspend(void)
{
	unsigned long	flags;
	int		err;

#ifndef CONFIG_APM_RTC_IS_GMT
	/*
	 * Estimate time zone so that set_time can update the clock
	 */
	save_flags(flags);
	clock_cmos_diff = -get_cmos_time();
	cli();
	clock_cmos_diff += CURRENT_TIME;
	got_clock_diff = 1;
	restore_flags(flags);
#endif

	err = apm_set_power_state(APM_STATE_SUSPEND);
	if (err)
		apm_error("suspend", err);
#ifdef INIT_TIMER_AFTER_SUSPEND
	save_flags(flags);
	cli();
	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	udelay(10);
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	udelay(10);
	outb(LATCH >> 8 , 0x40);	/* MSB */
	udelay(10);
	restore_flags(flags);
#endif
	set_time();
}

static void standby(void)
{
	int	err;

	err = apm_set_power_state(APM_STATE_STANDBY);
	if (err)
		apm_error("standby", err);
}

static apm_event_t get_event(void)
{
	int		error;
	apm_event_t	event;
	apm_eventinfo_t	info;

	static int notified = 0;

	/* we don't use the eventinfo */
	error = apm_get_event(&event, &info);
	if (error == APM_SUCCESS)
		return event;

	if ((error != APM_NO_EVENTS) && (notified++ == 0))
		apm_error("get_event", error);

	return 0;
}

static void send_event(apm_event_t event, apm_event_t undo,
		       struct apm_bios_struct *sender)
{
	callback_list_t *	call;
	callback_list_t *	fix;

	for (call = callback_list; call != NULL; call = call->next) {
		if (call->callback(event) && undo) {
			for (fix = callback_list; fix != call; fix = fix->next)
				fix->callback(undo);
			if (apm_bios_info.version > 0x100)
				apm_set_power_state(APM_STATE_REJECT);
			return;
		}
	}

	queue_event(event, sender);
}

static void check_events(void)
{
	apm_event_t		event;
#ifdef CONFIG_APM_IGNORE_SUSPEND_BOUNCE
	static unsigned long	last_resume = 0;
	static int		ignore_bounce = 0;
#endif

	while ((event = get_event()) != 0) {
#ifdef APM_DEBUG
		if (event <= NR_APM_EVENT_NAME)
			printk(KERN_DEBUG "apm: received %s notify\n",
			       apm_event_name[event - 1]);
		else
			printk(KERN_DEBUG "apm: received unknown "
			       "event 0x%02x\n", event);
#endif
#ifdef CONFIG_APM_IGNORE_SUSPEND_BOUNCE
		if (ignore_bounce
		    && ((jiffies - last_resume) > BOUNCE_INTERVAL))
			ignore_bounce = 0;
#endif
		switch (event) {
		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
#ifdef CONFIG_APM_IGNORE_MULTIPLE_SUSPEND
			if (waiting_for_resume)
				return;
			waiting_for_resume = 1;
#endif
			send_event(event, APM_STANDBY_RESUME, NULL);
			if (standbys_pending <= 0)
				standby();
			break;

		case APM_USER_SUSPEND:
#ifdef CONFIG_APM_IGNORE_USER_SUSPEND
			if (apm_bios_info.version > 0x100)
				apm_set_power_state(APM_STATE_REJECT);
			break;
#endif
		case APM_SYS_SUSPEND:
#ifdef CONFIG_APM_IGNORE_SUSPEND_BOUNCE
			if (ignore_bounce)
				break;
#endif
#ifdef CONFIG_APM_IGNORE_MULTIPLE_SUSPEND
			if (waiting_for_resume)
				return;
			waiting_for_resume = 1;
#endif
			send_event(event, APM_NORMAL_RESUME, NULL);
			if (suspends_pending <= 0)
				suspend();
			break;

		case APM_NORMAL_RESUME:
		case APM_CRITICAL_RESUME:
		case APM_STANDBY_RESUME:
#ifdef CONFIG_APM_IGNORE_MULTIPLE_SUSPEND
			waiting_for_resume = 0;
#endif
#ifdef CONFIG_APM_IGNORE_SUSPEND_BOUNCE
			last_resume = jiffies;
			ignore_bounce = 1;
#endif
			set_time();
			send_event(event, 0, NULL);
			break;

		case APM_LOW_BATTERY:
		case APM_POWER_STATUS_CHANGE:
		case APM_CAPABILITY_CHANGE:
			send_event(event, 0, NULL);
			break;

		case APM_UPDATE_TIME:
			set_time();
			break;

		case APM_CRITICAL_SUSPEND:
			suspend();
			break;
		}
	}
}

static void do_apm_timer(unsigned long unused)
{
	int	err;

	static int	pending_count = 0;

	if (((standbys_pending > 0) || (suspends_pending > 0))
	    && (apm_bios_info.version > 0x100)
	    && (pending_count-- <= 0)) {
		pending_count = 4;

		err = apm_set_power_state(APM_STATE_BUSY);
		if (err)
			apm_error("busy", err);
	}

	if (!(((standbys_pending > 0) || (suspends_pending > 0))
	      && (apm_bios_info.version == 0x100)))
		check_events();

	init_timer(&apm_timer);
	apm_timer.expires = APM_CHECK_TIMEOUT + jiffies;
	add_timer(&apm_timer);
}

/* Called from sys_idle, must make sure apm_enabled. */
int apm_do_idle(void)
{
#ifdef CONFIG_APM_CPU_IDLE
	u32	dummy;

	if (!apm_enabled)
		return 0;

	if (apm_bios_call_simple(0x5305, 0, 0, &dummy))
		return 0;

	clock_slowed = (apm_bios_info.flags & APM_IDLE_SLOWS_CLOCK) != 0;
	return 1;
#else
	return 0;
#endif
}

/* Called from sys_idle, must make sure apm_enabled. */
void apm_do_busy(void)
{
#ifdef CONFIG_APM_CPU_IDLE
	u32	dummy;

	if (apm_enabled
#ifndef ALWAYS_CALL_BUSY
		&& clock_slowed
#endif
	) {
		(void) apm_bios_call_simple(0x5306, 0, 0, &dummy);
		clock_slowed = 0;
	}
#endif
}

static int check_apm_bios_struct(struct apm_bios_struct *as, const char *func)
{
	if ((as == NULL) || (as->magic != APM_BIOS_MAGIC)) {
		printk(KERN_ERR "apm: %s passed bad filp", func);
		return 1;
	}
	return 0;
}

static ssize_t do_read(struct file *fp, char *buf, size_t count, loff_t *ppos)
{
	struct apm_bios_struct *	as;
	int			i;
	apm_event_t		event;
	struct wait_queue	wait = { current,	NULL };

	as = fp->private_data;
	if (check_apm_bios_struct(as, "read"))
		return -EIO;
	if (count < sizeof(apm_event_t))
		return -EINVAL;
	if (queue_empty(as)) {
		if (fp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&process_list, &wait);
repeat:
		current->state = TASK_INTERRUPTIBLE;
		if (queue_empty(as) && !signal_pending(current)) {
			schedule();
			goto repeat;
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&process_list, &wait);
	}
	i = count;
	while ((i >= sizeof(event)) && !queue_empty(as)) {
		event = get_queued_event(as);
		copy_to_user(buf, &event, sizeof(event));
		switch (event) {
		case APM_SYS_SUSPEND:
		case APM_USER_SUSPEND:
			as->suspends_read++;
			break;

		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
			as->standbys_read++;
			break;
		}
		buf += sizeof(event);
		i -= sizeof(event);
	}
	if (i < count)
		return count - i;
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

static unsigned int do_poll(struct file *fp, poll_table * wait)
{
	struct apm_bios_struct * as;

	as = fp->private_data;
	if (check_apm_bios_struct(as, "select"))
		return 0;
	poll_wait(fp, &process_list, wait);
	if (!queue_empty(as))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int do_ioctl(struct inode * inode, struct file *filp,
		    u_int cmd, u_long arg)
{
	struct apm_bios_struct *	as;

	as = filp->private_data;
	if (check_apm_bios_struct(as, "ioctl"))
		return -EIO;
	if (!as->suser)
		return -EPERM;
	switch (cmd) {
	case APM_IOC_STANDBY:
		if (as->standbys_read > 0) {
			as->standbys_read--;
			as->standbys_pending--;
			standbys_pending--;
		}
		else
			send_event(APM_USER_STANDBY, APM_STANDBY_RESUME, as);
		if (standbys_pending <= 0)
			standby();
		break;
	case APM_IOC_SUSPEND:
		if (as->suspends_read > 0) {
			as->suspends_read--;
			as->suspends_pending--;
			suspends_pending--;
		}
		else
			send_event(APM_USER_SUSPEND, APM_NORMAL_RESUME, as);
		if (suspends_pending <= 0)
			suspend();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int do_release(struct inode * inode, struct file * filp)
{
	struct apm_bios_struct *	as;

	as = filp->private_data;
	filp->private_data = NULL;
	if (check_apm_bios_struct(as, "release"))
		return 0;
	if (as->standbys_pending > 0) {
		standbys_pending -= as->standbys_pending;
		if (standbys_pending <= 0)
			standby();
	}
	if (as->suspends_pending > 0) {
		suspends_pending -= as->suspends_pending;
		if (suspends_pending <= 0)
			suspend();
	}
	if (user_list == as)
		user_list = as->next;
	else {
		struct apm_bios_struct *	as1;

		for (as1 = user_list;
		     (as1 != NULL) && (as1->next != as);
		     as1 = as1->next)
			;
		if (as1 == NULL)
			printk(KERN_ERR "apm: filp not in user list");
		else
			as1->next = as->next;
	}
	kfree_s(as, sizeof(*as));
	return 0;
}

static int do_open(struct inode * inode, struct file * filp)
{
	struct apm_bios_struct *	as;

	as = (struct apm_bios_struct *)kmalloc(sizeof(*as), GFP_KERNEL);
	if (as == NULL) {
		printk(KERN_ERR "apm: cannot allocate struct of size %d bytes",
		       sizeof(*as));
		return -ENOMEM;
	}
	as->magic = APM_BIOS_MAGIC;
	as->event_tail = as->event_head = 0;
	as->suspends_pending = as->standbys_pending = 0;
	as->suspends_read = as->standbys_read = 0;
	/*
	 * XXX - this is a tiny bit broken, when we consider BSD
         * process accounting. If the device is opened by root, we
	 * instantly flag that we used superuser privs. Who knows,
	 * we might close the device immediately without doing a
	 * privileged operation -- cevans
	 */
	as->suser = capable(CAP_SYS_ADMIN);
	as->next = user_list;
	user_list = as;
	filp->private_data = as;
	return 0;
}

int apm_get_info(char *buf, char **start, off_t fpos, int length, int dummy)
{
	char *		p;
	unsigned short	bx;
	unsigned short	cx;
	unsigned short	dx;
	unsigned short	nbat;
	unsigned short	error;
	unsigned short  ac_line_status = 0xff;
	unsigned short  battery_status = 0xff;
	unsigned short  battery_flag   = 0xff;
	int		percentage     = -1;
	int             time_units     = -1;
	char            *units         = "?";

	if (!apm_enabled)
		return 0;
	p = buf;

	if (!(error = apm_get_power_status(&bx, &cx, &dx))) {
		ac_line_status = (bx >> 8) & 0xff;
		battery_status = bx & 0xff;
		if ((cx & 0xff) != 0xff)
			percentage = cx & 0xff;

		if (apm_bios_info.version > 0x100) {
			battery_flag = (cx >> 8) & 0xff;
			if (dx != 0xffff) {
				units = (dx & 0x8000) ? "min" : "sec";
				time_units = dx & 0x7fff;
			}
		}
	}
	/* Arguments, with symbols from linux/apm_bios.h.  Information is
	   from the Get Power Status (0x0a) call unless otherwise noted.

	   0) Linux driver version (this will change if format changes)
	   1) APM BIOS Version.  Usually 1.0 or 1.1.
	   2) APM flags from APM Installation Check (0x00):
	      bit 0: APM_16_BIT_SUPPORT
	      bit 1: APM_32_BIT_SUPPORT
	      bit 2: APM_IDLE_SLOWS_CLOCK
	      bit 3: APM_BIOS_DISABLED
	      bit 4: APM_BIOS_DISENGAGED
	   3) AC line status
	      0x00: Off-line
	      0x01: On-line
	      0x02: On backup power (APM BIOS 1.1 only)
	      0xff: Unknown
	   4) Battery status
	      0x00: High
	      0x01: Low
	      0x02: Critical
	      0x03: Charging
	      0xff: Unknown
	   5) Battery flag
	      bit 0: High
	      bit 1: Low
	      bit 2: Critical
	      bit 3: Charging
	      bit 7: No system battery
	      0xff: Unknown
	   6) Remaining battery life (percentage of charge):
	      0-100: valid
	      -1: Unknown
	   7) Remaining battery life (time units):
	      Number of remaining minutes or seconds
	      -1: Unknown
	   8) min = minutes; sec = seconds */

	p += sprintf(p, "%s %d.%d 0x%02x 0x%02x 0x%02x 0x%02x %d%% %d %s\n",
		     driver_version,
		     (apm_bios_info.version >> 8) & 0xff,
		     apm_bios_info.version & 0xff,
		     apm_bios_info.flags,
		     ac_line_status,
		     battery_status,
		     battery_flag,
		     percentage,
		     time_units,
		     units);

	return p - buf;
}

void __init apm_setup(char *str, int *dummy)
{
	int	invert;

	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "off", 3) == 0)
			apm_disabled = 1;
		if (strncmp(str, "on", 2) == 0)
			apm_disabled = 0;
		invert = (strncmp(str, "no-", 3) == 0);
		if (invert)
			str += 3;
		if (strncmp(str, "debug", 5) == 0)
			debug = !invert;
		if (strncmp(str, "smp-power-off", 13) == 0)
			smp_hack = !invert;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
}

void __init apm_bios_init(void)
{
	unsigned short	bx;
	unsigned short	cx;
	unsigned short	dx;
	unsigned short	error;
	char *		power_stat;
	char *		bat_stat;
	static struct proc_dir_entry *ent;

	if (apm_bios_info.version == 0) {
		printk(KERN_INFO "apm: BIOS not found.\n");
		return;
	}
	printk(KERN_INFO
		"apm: BIOS version %d.%d Flags 0x%02x (Driver version %s)\n",
		((apm_bios_info.version >> 8) & 0xff),
		(apm_bios_info.version & 0xff),
		apm_bios_info.flags,
		driver_version);
	if ((apm_bios_info.flags & APM_32_BIT_SUPPORT) == 0) {
		printk(KERN_INFO "apm: no 32 bit BIOS support\n");
		return;
	}

	/*
	 * Fix for the Compaq Contura 3/25c which reports BIOS version 0.1
	 * but is reportedly a 1.0 BIOS.
	 */
	if (apm_bios_info.version == 0x001)
		apm_bios_info.version = 0x100;

	/* BIOS < 1.2 doesn't set cseg_16_len */
	if (apm_bios_info.version < 0x102)
		apm_bios_info.cseg_16_len = 0; /* 64k */

	if (debug) {
		printk(KERN_INFO "apm: entry %x:%lx cseg16 %x dseg %x",
			apm_bios_info.cseg, apm_bios_info.offset,
			apm_bios_info.cseg_16, apm_bios_info.dseg);
		if (apm_bios_info.version > 0x100)
			printk(" cseg len %x, dseg len %x",
				apm_bios_info.cseg_len,
				apm_bios_info.dseg_len);
		if (apm_bios_info.version > 0x101)
			printk(" cseg16 len %x", apm_bios_info.cseg_16_len);
		printk("\n");
	}

	if (apm_disabled) {
		printk(KERN_NOTICE "apm: disabled on user request.\n");
		return;
	}

	/*
	 * Set up a segment that references the real mode segment 0x40
	 * that extends up to the end of page zero (that we have reserved).
	 * This is for buggy BIOS's that refer to (real mode) segment 0x40
	 * even though they are called in protected mode.
	 */
	set_base(gdt[APM_40 >> 3],
		 __va((unsigned long)0x40 << 4));
	_set_limit((char *)&gdt[APM_40 >> 3], 4095 - (0x40 << 4));

	apm_bios_entry.offset = apm_bios_info.offset;
	apm_bios_entry.segment = APM_CS;
	set_base(gdt[APM_CS >> 3],
		 __va((unsigned long)apm_bios_info.cseg << 4));
	set_base(gdt[APM_CS_16 >> 3],
		 __va((unsigned long)apm_bios_info.cseg_16 << 4));
	set_base(gdt[APM_DS >> 3],
		 __va((unsigned long)apm_bios_info.dseg << 4));
#ifndef APM_RELAX_SEGMENTS
	if (apm_bios_info.version == 0x100)
#endif
	{
		/* For ASUS motherboard, Award BIOS rev 110 (and others?) */
		_set_limit((char *)&gdt[APM_CS >> 3], 64 * 1024 - 1);
		/* For some unknown machine. */
		_set_limit((char *)&gdt[APM_CS_16 >> 3], 64 * 1024 - 1);
		/* For the DEC Hinote Ultra CT475 (and others?) */
		_set_limit((char *)&gdt[APM_DS >> 3], 64 * 1024 - 1);
	}
#ifndef APM_RELAX_SEGMENTS
	else {
		_set_limit((char *)&gdt[APM_CS >> 3],
			(apm_bios_info.cseg_len - 1) & 0xffff);
		_set_limit((char *)&gdt[APM_CS_16 >> 3],
			(apm_bios_info.cseg_16_len - 1) & 0xffff);
		_set_limit((char *)&gdt[APM_DS >> 3],
			(apm_bios_info.dseg_len - 1) & 0xffff);
	}
#endif
#ifdef CONFIG_SMP
	if (smp_num_cpus > 1) {
		printk(KERN_NOTICE "apm: disabled - APM is not SMP safe.\n");
		if (smp_hack)
			smp_hack = 2;
		return;
	}
#endif
	if (apm_bios_info.version > 0x100) {
		/*
		 * We only support BIOSs up to version 1.2
		 */
		if (apm_bios_info.version > 0x0102)
			apm_bios_info.version = 0x0102;
		if (apm_driver_version(&apm_bios_info.version) != APM_SUCCESS) {
			/* Fall back to an APM 1.0 connection. */
			apm_bios_info.version = 0x100;
		}
	}
	if (debug) {
		printk(KERN_INFO "apm: Connection version %d.%d\n",
			(apm_bios_info.version >> 8) & 0xff,
			apm_bios_info.version & 0xff );

		error = apm_get_power_status(&bx, &cx, &dx);
		if (error)
			printk(KERN_INFO "apm: power status not available\n");
		else {
			switch ((bx >> 8) & 0xff) {
			case 0: power_stat = "off line"; break;
			case 1: power_stat = "on line"; break;
			case 2: power_stat = "on backup power"; break;
			default: power_stat = "unknown"; break;
			}
			switch (bx & 0xff) {
			case 0: bat_stat = "high"; break;
			case 1: bat_stat = "low"; break;
			case 2: bat_stat = "critical"; break;
			case 3: bat_stat = "charging"; break;
			default: bat_stat = "unknown"; break;
			}
			printk(KERN_INFO
			       "apm: AC %s, battery status %s, battery life ",
			       power_stat, bat_stat);
			if ((cx & 0xff) == 0xff)
				printk("unknown\n");
			else
				printk("%d%%\n", cx & 0xff);
			if (apm_bios_info.version > 0x100) {
				printk(KERN_INFO
				       "apm: battery flag 0x%02x, battery life ",
				       (cx >> 8) & 0xff);
				if (dx == 0xffff)
					printk("unknown\n");
				else
					printk("%d %s\n", dx & 0x7fff,
						(dx & 0x8000) ?
						"minutes" : "seconds");
			}
		}
	}

#ifdef CONFIG_APM_DO_ENABLE
	if (apm_bios_info.flags & APM_BIOS_DISABLED) {
		/*
		 * This call causes my NEC UltraLite Versa 33/C to hang if it
		 * is booted with PM disabled but not in the docking station.
		 * Unfortunate ...
		 */
		error = apm_enable_power_management();
		if (error) {
			apm_error("enable power management", error);
			return;
		}
	}
#endif
	if (((apm_bios_info.flags & APM_BIOS_DISABLED) == 0)
	    && (apm_bios_info.version > 0x0100)) {
		if (apm_engage_power_management(0x0001) == APM_SUCCESS)
			apm_bios_info.flags &= ~APM_BIOS_DISENGAGED;
	}

	init_timer(&apm_timer);
	apm_timer.function = do_apm_timer;
	apm_timer.expires = APM_CHECK_TIMEOUT + jiffies;
	add_timer(&apm_timer);

	ent = create_proc_entry("apm", 0, 0);
	if (ent != NULL)
		ent->get_info = apm_get_info;

	misc_register(&apm_device);

	apm_enabled = 1;
}

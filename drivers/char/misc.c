/*
 * linux/drivers/char/misc.c
 *
 * Generic misc open routine by Johan Myreen
 *
 * Based on code from Linus
 *
 * Teemu Rantanen's Microsoft Busmouse support and Derrick Cole's
 *   changes incorporated into 0.97pl4
 *   by Peter Cervasio (pete%q106fm.uucp@wupost.wustl.edu) (08SEP92)
 *   See busmouse.c for particulars.
 *
 * Made things a lot mode modular - easy to compile in just one or two
 * of the misc drivers, as they are now completely independent. Linus.
 *
 * Support for loadable modules. 8-Sep-95 Philip Blundell <pjb27@cam.ac.uk>
 *
 * Fixed a failing symbol register to free the device registration
 *		Alan Cox <alan@lxorguk.ukuu.org.uk> 21-Jan-96
 *
 * Dynamic minors and /proc/mice by Alessandro Rubini. 26-Mar-96
 *
 * Renamed to misc and miscdevice to be more accurate. Alan Cox 26-Mar-96
 *
 * Handling of mouse minor numbers for kerneld:
 *  Idea by Jacques Gelinas <jack@solucorp.qc.ca>,
 *  adapted by Bjorn Ekwall <bj0rn@blox.se>
 *  corrected by Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * Changes for kmod (from kerneld):
 	Cyrus Durgin <cider@speakeasy.org>
 */

#include <linux/module.h>
#include <linux/config.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

#include <linux/tty.h>
#include <linux/selection.h>
#include <linux/kmod.h>

/*
 * Head entry for the doubly linked miscdevice list
 */
static struct miscdevice misc_list = { 0, "head", NULL, &misc_list, &misc_list };

/*
 * Assigned numbers, used for dynamic minors
 */
#define DYNAMIC_MINORS 64 /* like dynamic majors */
static unsigned char misc_minors[DYNAMIC_MINORS / 8];

extern int bus_mouse_init(void);
extern int qpmouse_init(void);
extern int ms_bus_mouse_init(void);
extern int atixl_busmouse_init(void);
extern int amiga_mouse_init(void);
extern int atari_mouse_init(void);
extern int sun_mouse_init(void);
extern int adb_mouse_init(void);
extern void watchdog_init(void);
extern void wdt_init(void);
extern void acq_init(void);
extern void pcwatchdog_init(void);
extern int rtc_init(void);
extern int rtc_DP8570A_init(void);
extern int rtc_MK48T08_init(void);
extern int dsp56k_init(void);
extern int nvram_init(void);
extern int radio_init(void);
extern void hfmodem_init(void);
extern int pc110pad_init(void);
extern int pmu_device_init(void);

static int misc_read_proc(char *buf, char **start, off_t offset,
			  int len, int *eof, void *private)
{
	struct miscdevice *p;

	len=0;
	for (p = misc_list.next; p != &misc_list && len < 4000; p = p->next)
		len += sprintf(buf+len, "%3i %s\n",p->minor, p->name ?: "");
	*start = buf + offset;
	return len > offset ? len - offset : 0;
}


static int misc_open(struct inode * inode, struct file * file)
{
	int minor = MINOR(inode->i_rdev);
	struct miscdevice *c = misc_list.next;
	file->f_op = NULL;

	while ((c != &misc_list) && (c->minor != minor))
		c = c->next;
	if (c == &misc_list) {
		char modname[20];
		sprintf(modname, "char-major-%d-%d", MISC_MAJOR, minor);
		request_module(modname);
		c = misc_list.next;
		while ((c != &misc_list) && (c->minor != minor))
			c = c->next;
		if (c == &misc_list)
			return -ENODEV;
	}

	if ((file->f_op = c->fops) && file->f_op->open)
		return file->f_op->open(inode,file);
	else
		return -ENODEV;
}

static struct file_operations misc_fops = {
        NULL,		/* seek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
        misc_open,
	NULL,		/* flush */
        NULL		/* release */
};

int misc_register(struct miscdevice * misc)
{
	if (misc->next || misc->prev)
		return -EBUSY;
	if (misc->minor == MISC_DYNAMIC_MINOR) {
		int i = DYNAMIC_MINORS;
		while (--i >= 0)
			if ( (misc_minors[i>>3] & (1 << (i&7))) == 0)
				break;
		if (i<0) return -EBUSY;
		misc->minor = i;
	}
	if (misc->minor < DYNAMIC_MINORS)
		misc_minors[misc->minor >> 3] |= 1 << (misc->minor & 7);

	/*
	 * Add it to the front, so that later devices can "override"
	 * earlier defaults
	 */
	misc->prev = &misc_list;
	misc->next = misc_list.next;
	misc->prev->next = misc;
	misc->next->prev = misc;
	return 0;
}

int misc_deregister(struct miscdevice * misc)
{
	int i = misc->minor;
	if (!misc->next || !misc->prev)
		return -EINVAL;
	misc->prev->next = misc->next;
	misc->next->prev = misc->prev;
	misc->next = NULL;
	misc->prev = NULL;
	if (i < DYNAMIC_MINORS && i>0) {
		misc_minors[i>>3] &= ~(1 << (misc->minor & 7));
	}
	return 0;
}

EXPORT_SYMBOL(misc_register);
EXPORT_SYMBOL(misc_deregister);

static struct proc_dir_entry *proc_misc;	

int __init misc_init(void)
{
	proc_misc = create_proc_entry("misc", 0, 0);
	if (proc_misc)
		proc_misc->read_proc = misc_read_proc;
#ifdef CONFIG_BUSMOUSE
	bus_mouse_init();
#endif
#if defined CONFIG_82C710_MOUSE
	qpmouse_init();
#endif
#ifdef CONFIG_MS_BUSMOUSE
	ms_bus_mouse_init();
#endif
#ifdef CONFIG_ATIXL_BUSMOUSE
 	atixl_busmouse_init();
#endif
#ifdef CONFIG_AMIGAMOUSE
	amiga_mouse_init();
#endif
#ifdef CONFIG_ATARIMOUSE
	atari_mouse_init();
#endif
#ifdef CONFIG_SUN_MOUSE
	sun_mouse_init();
#endif
#ifdef CONFIG_ADBMOUSE
	adb_mouse_init();
#endif
#ifdef CONFIG_PC110_PAD
	pc110pad_init();
#endif
/*
 *	Only one watchdog can succeed. We probe the pcwatchdog first,
 *	then the wdt cards and finally the software watchdog which always
 *	works. This means if your hardware watchdog dies or is 'borrowed'
 *	for some reason the software watchdog still gives you some cover.
 */
#ifdef CONFIG_PCWATCHDOG
	pcwatchdog_init();
#endif
#ifdef CONFIG_WDT
	wdt_init();
#endif
#ifdef CONFIG_ACQUIRE_WDT
	acq_init();
#endif
#ifdef CONFIG_SOFT_WATCHDOG
	watchdog_init();
#endif
#ifdef CONFIG_APM
	apm_bios_init();
#endif
#ifdef CONFIG_H8
	h8_init();
#endif
#ifdef CONFIG_MVME16x
	rtc_MK48T08_init();
#endif
#ifdef CONFIG_BVME6000
	rtc_DP8570A_init();
#endif
#if defined(CONFIG_RTC) || defined(CONFIG_SUN_MOSTEK_RTC)
	rtc_init();
#endif
#ifdef CONFIG_ATARI_DSP56K
	dsp56k_init();
#endif
#ifdef CONFIG_HFMODEM
	hfmodem_init();
#endif
#ifdef CONFIG_NVRAM
	nvram_init();
#endif
#ifdef CONFIG_MISC_RADIO
	radio_init();
#endif
#ifdef CONFIG_HFMODEM
	hfmodem_init();
#endif
#ifdef CONFIG_PMAC_PBOOK
	pmu_device_init();
#endif
	if (register_chrdev(MISC_MAJOR,"misc",&misc_fops)) {
		printk("unable to get major %d for misc devices\n",
		       MISC_MAJOR);
		return -EIO;
	}

	return 0;
}

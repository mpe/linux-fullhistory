/* -*- linux-c -*-
 *
 *	$Id: sysrq.c,v 1.15 1998/08/23 14:56:41 mj Exp $
 *
 *	Linux Magic System Request Key Hacks
 *
 *	(c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *	based on ideas by Pavel Machek <pavel@atrey.karlin.mff.cuni.cz>
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/kbd_kern.h>
#include <linux/quotaops.h>
#include <linux/smp_lock.h>

#include <asm/ptrace.h>

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

extern void wakeup_bdflush(int);
extern void reset_vc(unsigned int);
extern int console_loglevel;
extern struct vfsmount *vfsmntlist;

/* Send a signal to all user processes */

static void send_sig_all(int sig, int even_init)
{
	struct task_struct *p;

	for_each_task(p) {
		if (p->pid && p->mm != &init_mm) {	    /* Not swapper nor kernel thread */
			if (p->pid == 1 && even_init)	    /* Ugly hack to kill init */
				p->pid = 0x8000;
			force_sig(sig, p);
		}
	}
}

/*
 * This function is called by the keyboard handler when SysRq is pressed
 * and any other keycode arrives.
 */

void handle_sysrq(int key, struct pt_regs *pt_regs,
		  struct kbd_struct *kbd, struct tty_struct *tty)
{
	int orig_log_level = console_loglevel;

	if (!key)
		return;

	console_loglevel = 7;
	printk(KERN_INFO "SysRq: ");
	switch (key) {
	case 'r':					    /* R -- Reset raw mode */
		if (kbd) {
			kbd->kbdmode = VC_XLATE;
			printk("Keyboard mode set to XLATE\n");
		}
		break;
#ifdef CONFIG_VT
	case 'k':					    /* K -- SAK */
		printk("SAK\n");
		if (tty)
			do_SAK(tty);
		reset_vc(fg_console);
		break;
#endif
	case 'b':					    /* B -- boot immediately */
		printk("Resetting\n");
		machine_restart(NULL);
		break;
#ifdef CONFIG_APM
	case 'o':					    /* O -- power off */
		printk("Power off\n");
		apm_power_off();
		break;
#endif
	case 's':					    /* S -- emergency sync */
		printk("Emergency Sync\n");
		emergency_sync_scheduled = EMERG_SYNC;
		wakeup_bdflush(0);
		break;
	case 'u':					    /* U -- emergency remount R/O */
		printk("Emergency Remount R/O\n");
		emergency_sync_scheduled = EMERG_REMOUNT;
		wakeup_bdflush(0);
		break;
	case 'p':					    /* P -- show PC */
		printk("Show Regs\n");
		if (pt_regs)
			show_regs(pt_regs);
		break;
	case 't':					    /* T -- show task info */
		printk("Show State\n");
		show_state();
		break;
	case 'm':					    /* M -- show memory info */
		printk("Show Memory\n");
		show_mem();
		break;
	case '0' ... '9':				    /* 0-9 -- set console logging level */
		orig_log_level = key - '0';
		printk("Log level set to %d\n", orig_log_level);
		break;
	case 'e':					    /* E -- terminate all user processes */
		printk("Terminate All Tasks\n");
		send_sig_all(SIGTERM, 0);
		orig_log_level = 8;			    /* We probably have killed syslogd */
		break;
	case 'i':					    /* I -- kill all user processes */
		printk("Kill All Tasks\n");
		send_sig_all(SIGKILL, 0);
		orig_log_level = 8;
		break;
	case 'l':					    /* L -- kill all processes including init */
		printk("Kill ALL Tasks (even init)\n");
		send_sig_all(SIGKILL, 1);
		orig_log_level = 8;
		break;
	default:					    /* Unknown: help */
		if (kbd)
			printk("unRaw ");
#ifdef CONFIG_VT
		if (tty)
			printk("saK ");
#endif
		printk("Boot "
#ifdef CONFIG_APM
		       "Off "
#endif
		       "Sync Unmount showPc showTasks showMem loglevel0-8 tErm kIll killalL\n");
		/* Don't use 'A' as it's handled specially on the Sparc */
	}

	console_loglevel = orig_log_level;
}

/* Aux routines for the syncer */

static void all_files_read_only(void)	    /* Kill write permissions of all files */
{
	struct file *file;

	for (file = inuse_filps; file; file = file->f_next)
		if (file->f_dentry && file->f_count && S_ISREG(file->f_dentry->d_inode->i_mode))
			file->f_mode &= ~2;
}

static int is_local_disk(kdev_t dev)	    /* Guess if the device is a local hard drive */
{
	unsigned int major = MAJOR(dev);

	switch (major) {
	case IDE0_MAJOR:
	case IDE1_MAJOR:
	case IDE2_MAJOR:
	case IDE3_MAJOR:
	case SCSI_DISK0_MAJOR:
	case SCSI_DISK1_MAJOR:
	case SCSI_DISK2_MAJOR:
	case SCSI_DISK3_MAJOR:
	case SCSI_DISK4_MAJOR:
	case SCSI_DISK5_MAJOR:
	case SCSI_DISK6_MAJOR:
	case SCSI_DISK7_MAJOR:
		return 1;
	default:
		return 0;
	}
}

static void go_sync(kdev_t dev, int remount_flag)
{
	printk(KERN_INFO "%sing device %s ... ",
	       remount_flag ? "Remount" : "Sync",
	       kdevname(dev));

	if (remount_flag) {				    /* Remount R/O */
		struct super_block *sb = get_super(dev);
		struct vfsmount *vfsmnt;
		int ret, flags;

		if (!sb) {
			printk("Superblock not found\n");
			return;
		}
		if (sb->s_flags & MS_RDONLY) {
			printk("R/O\n");
			return;
		}
		DQUOT_OFF(dev);
		fsync_dev(dev);
		flags = MS_RDONLY;
		if (sb->s_op && sb->s_op->remount_fs) {
			ret = sb->s_op->remount_fs(sb, &flags, NULL);
			if (ret)
				printk("error %d\n", ret);
			else {
				sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | (flags & MS_RMT_MASK);
				if ((vfsmnt = lookup_vfsmnt(sb->s_dev)))
					vfsmnt->mnt_flags = sb->s_flags;
				printk("OK\n");
			}
		} else
			printk("nothing to do\n");
	} else {
		fsync_dev(dev);				    /* Sync only */
		printk("OK\n");
	}
}

/*
 * Emergency Sync or Unmount. We cannot do it directly, so we set a special
 * flag and wake up the bdflush kernel thread which immediately calls this function.
 * We process all mounted hard drives first to recover from crashed experimental
 * block devices and malfunctional network filesystems.
 */

int emergency_sync_scheduled;

void do_emergency_sync(void)
{
	struct vfsmount *mnt;
	int remount_flag;

	lock_kernel();
	remount_flag = (emergency_sync_scheduled == EMERG_REMOUNT);
	emergency_sync_scheduled = 0;

	if (remount_flag)
		all_files_read_only();

	for (mnt = vfsmntlist; mnt; mnt = mnt->mnt_next)
		if (is_local_disk(mnt->mnt_dev))
			go_sync(mnt->mnt_dev, remount_flag);

	for (mnt = vfsmntlist; mnt; mnt = mnt->mnt_next)
		if (!is_local_disk(mnt->mnt_dev) && MAJOR(mnt->mnt_dev))
			go_sync(mnt->mnt_dev, remount_flag);

	unlock_kernel();
	printk(KERN_INFO "Done.\n");
}

/*
 *  linux/kernel/acct.c
 *
 *  BSD Process Accounting for Linux
 *
 *  Author: Marco van Wieringen <mvw@planets.elm.net>
 *
 *  Some code based on ideas and code from:
 *  Thomas K. Dyas <tdyas@eden.rutgers.edu>
 *
 *  This file implements BSD-style process accounting. Whenever any
 *  process exits, an accounting record of type "struct acct" is
 *  written to the file specified with the acct() system call. It is
 *  up to user-level programs to do useful things with the accounting
 *  log. The kernel just provides the raw accounting information.
 *
 * (C) Copyright 1995 - 1997 Marco van Wieringen - ELM Consultancy B.V.
 *
 *  Plugged two leaks. 1) It didn't return acct_file into the free_filps if
 *  the file happened to be read-only. 2) If the accounting was suspended
 *  due to the lack of space it happily allowed to reopen it and completely
 *  lost the old acct_file. 3/10/98, Al Viro.
 *
 *  Now we silently close acct_file on attempt to reopen. Cleaned sys_acct().
 *  XTerms and EMACS are manifestations of pure evil. 21/10/98, AV.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#ifdef CONFIG_BSD_PROCESS_ACCT
#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/acct.h>
#include <linux/major.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/file.h>

#include <asm/uaccess.h>

/*
 * These constants control the amount of freespace that suspend and
 * resume the process accounting system, and the time delay between
 * each check.
 */

#define RESUME		(4)       /* More than 4% free space will resume */
#define SUSPEND		(2)       /* Less than 2% free space will suspend */
#define ACCT_TIMEOUT	(30 * HZ) /* 30 second timeout between checks */

/*
 * External references and all of the globals.
 */
void acct_timeout(unsigned long);

static volatile int acct_active = 0;
static volatile int acct_needcheck = 0;
static struct file *acct_file = NULL;
static struct timer_list acct_timer = { NULL, NULL, 0, 0, acct_timeout };

/*
 * Called whenever the timer says to check the free space.
 */
void acct_timeout(unsigned long unused)
{
	acct_needcheck = 1;
}

/*
 * Check the amount of free space and suspend/resume accordingly.
 */
static void check_free_space(void)
{
	mm_segment_t fs;
	struct statfs sbuf;
	struct super_block *sb;

	if (!acct_file || !acct_needcheck)
		return;

	sb = acct_file->f_dentry->f_inode->i_sb;
	if (!sb->s_op || !sb->s_op->statfs)
		return;

	fs = get_fs();
	set_fs(KERNEL_DS);
	sb->s_op->statfs(sb, &sbuf, sizeof(struct statfs));
	set_fs(fs);

	if (acct_active) {
		if (sbuf.f_bavail <= SUSPEND * sbuf.f_blocks / 100) {
			acct_active = 0;
			printk(KERN_INFO "Process accounting paused\r\n");
		}
	} else {
		if (sbuf.f_bavail >= RESUME * sbuf.f_blocks / 100) {
			acct_active = 1;
			printk(KERN_INFO "Process accounting resumed\r\n");
		}
	}
	del_timer(&acct_timer);
	acct_needcheck = 0;
	acct_timer.expires = jiffies + ACCT_TIMEOUT;
	add_timer(&acct_timer);
}

/*
 *  sys_acct() is the only system call needed to implement process
 *  accounting. It takes the name of the file where accounting records
 *  should be written. If the filename is NULL, accounting will be
 *  shutdown.
 */
asmlinkage int sys_acct(const char *name)
{
	struct dentry *dentry;
	struct inode *inode;
	struct file_operations *ops;
	char *tmp;
	int error = -EPERM;

	lock_kernel();
	if (!capable(CAP_SYS_PACCT))
		goto out;

	if (acct_file) {
		/* fput() may block, so just in case... */
		struct file *tmp = acct_file;
		if (acct_active)
			acct_process(0); 
		del_timer(&acct_timer);
		acct_active = 0;
		acct_needcheck = 0;
		acct_file = NULL;
		fput(tmp);
	}
	error = 0;
	if (!name)		/* We are done */
		goto out;

	tmp = getname(name);
	error = PTR_ERR(tmp);
	if (IS_ERR(tmp))
		goto out;

	dentry = open_namei(tmp, O_RDWR, 0600);
	putname(tmp);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;

	inode = dentry->d_inode;

	error = -EACCES;
	if (!S_ISREG(inode->i_mode)) 
		goto out_d;

	error = -EIO;
	if (!inode->i_op || !(ops = inode->i_op->default_file_ops) || 
	    !ops->write) 
		goto out_d;

	error = -EUSERS;
	if (!(acct_file = get_empty_filp()))
		goto out_d;

	acct_file->f_mode = (O_WRONLY + 1) & O_ACCMODE;
	acct_file->f_flags = O_WRONLY;
	acct_file->f_dentry = dentry;
	acct_file->f_pos = inode->i_size;
	acct_file->f_reada = 0;
	acct_file->f_op = ops;
	error = get_write_access(inode);
	if (error)
		goto out_f;
	if (ops->open)
		error = ops->open(inode, acct_file);
	if (error) {
		put_write_access(inode);
		goto out_f;
	}
	acct_needcheck = 0;
	acct_active = 1;
	acct_timer.expires = jiffies + ACCT_TIMEOUT;
	add_timer(&acct_timer);
	goto out;
out_f:
	/* decrementing f_count is _not_ enough */
	put_filp(acct_file);
	acct_file = NULL;
out_d:
	dput(dentry);
out:
	unlock_kernel();
	return error;
}

void acct_auto_close(kdev_t dev)
{
	if (acct_active && acct_file && acct_file->f_dentry->d_inode->i_dev == dev)
		sys_acct((char *)NULL);
}

/*
 *  encode an unsigned long into a comp_t
 *
 *  This routine has been adopted from the encode_comp_t() function in
 *  the kern_acct.c file of the FreeBSD operating system. The encoding
 *  is a 13-bit fraction with a 3-bit (base 8) exponent.
 */

#define	MANTSIZE	13			/* 13 bit mantissa. */
#define	EXPSIZE		3			/* Base 8 (3 bit) exponent. */
#define	MAXFRACT	((1 << MANTSIZE) - 1)	/* Maximum fractional value. */

static comp_t encode_comp_t(unsigned long value)
{
	int exp, rnd;

	exp = rnd = 0;
	while (value > MAXFRACT) {
		rnd = value & (1 << (EXPSIZE - 1));	/* Round up? */
		value >>= EXPSIZE;	/* Base 8 exponent == 3 bit shift. */
		exp++;
	}

	/*
         * If we need to round up, do it (and handle overflow correctly).
         */
	if (rnd && (++value > MAXFRACT)) {
		value >>= EXPSIZE;
		exp++;
	}

	/*
         * Clean it up and polish it off.
         */
	exp <<= MANTSIZE;		/* Shift the exponent into place */
	exp += value;			/* and add on the mantissa. */
	return exp;
}

/*
 *  Write an accounting entry for an exiting process
 *
 *  The acct_process() call is the workhorse of the process
 *  accounting system. The struct acct is built here and then written
 *  into the accounting file. This function should only be called from
 *  do_exit().
 */
#define KSTK_EIP(stack) (((unsigned long *)(stack))[1019])
#define KSTK_ESP(stack) (((unsigned long *)(stack))[1022])

int acct_process(long exitcode)
{
	struct acct ac;
	mm_segment_t fs;
	unsigned long vsize;

	/*
	 * First check to see if there is enough free_space to continue
	 * the process accounting system. Check_free_space toggles the
	 * acct_active flag so we need to check that after check_free_space.
	 */
	check_free_space();

	if (!acct_active)
		return 0;


	/*
	 * Fill the accounting struct with the needed info as recorded
	 * by the different kernel functions.
	 */
	memset((caddr_t)&ac, 0, sizeof(struct acct));

	strncpy(ac.ac_comm, current->comm, ACCT_COMM);
	ac.ac_comm[ACCT_COMM - 1] = '\0';

	ac.ac_btime = CT_TO_SECS(current->start_time) + (xtime.tv_sec - (jiffies / HZ));
	ac.ac_etime = encode_comp_t(jiffies - current->start_time);
	ac.ac_utime = encode_comp_t(current->times.tms_utime);
	ac.ac_stime = encode_comp_t(current->times.tms_stime);
	ac.ac_uid = current->uid;
	ac.ac_gid = current->gid;
	ac.ac_tty = (current->tty) ? kdev_t_to_nr(current->tty->device) : 0;

	ac.ac_flag = 0;
	if (current->flags & PF_FORKNOEXEC)
		ac.ac_flag |= AFORK;
	if (current->flags & PF_SUPERPRIV)
		ac.ac_flag |= ASU;
	if (current->flags & PF_DUMPCORE)
		ac.ac_flag |= ACORE;
	if (current->flags & PF_SIGNALED)
		ac.ac_flag |= AXSIG;

	vsize = 0;
	if (current->mm) {
		struct vm_area_struct *vma = current->mm->mmap;
		while (vma) {
			vsize += vma->vm_end - vma->vm_start;
			vma = vma->vm_next;
		}
	}
	vsize = vsize / 1024;
	ac.ac_mem = encode_comp_t(vsize);
	ac.ac_io = encode_comp_t(0 /* current->io_usage */);	/* %% */
	ac.ac_rw = encode_comp_t(ac.ac_io / 1024);
	ac.ac_minflt = encode_comp_t(current->min_flt);
	ac.ac_majflt = encode_comp_t(current->maj_flt);
	ac.ac_swaps = encode_comp_t(current->nswap);
	ac.ac_exitcode = exitcode;

	/*
         * Kernel segment override to datasegment and write it
         * to the accounting file.
         */
	fs = get_fs();
	set_fs(KERNEL_DS);
	acct_file->f_op->write(acct_file, (char *)&ac,
			       sizeof(struct acct), &acct_file->f_pos);
	set_fs(fs);
	return 0;
}

#else
/*
 * Dummy system call when BSD process accounting is not configured
 * into the kernel.
 */

asmlinkage int sys_acct(const char * filename)
{
	return -ENOSYS;
}
#endif

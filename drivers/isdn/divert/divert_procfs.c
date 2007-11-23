/*
 * $Id: divert_procfs.c,v 1.8 2000/03/03 16:37:11 kai Exp $
 *
 * Filesystem handling for the diversion supplementary services.
 *
 * Copyright 1998       by Werner Cornelius (werner@isdn4linux.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log: divert_procfs.c,v $
 * Revision 1.8  2000/03/03 16:37:11  kai
 * incorporated some cosmetic changes from the official kernel tree back
 * into CVS
 *
 * Revision 1.7  2000/03/02 00:11:06  werner
 *
 * Changes related to procfs for 2.3.48
 *
 * Revision 1.6  2000/02/14 19:23:03  werner
 *
 * Changed handling of proc filesystem tables to a more portable version
 *
 * Revision 1.5  1999/09/14 20:31:01  werner
 *
 * Removed obsoleted functions for proc fs and synced with new ones.
 *
 * Revision 1.4  1999/08/06 07:42:48  calle
 * Added COMPAT_HAS_NEW_WAITQ for rd_queue for newer kernels.
 *
 * Revision 1.3  1999/07/05 20:21:41  werner
 * changes to use diversion sources for all kernel versions.
 * removed static device, only proc filesystem used
 *
 * Revision 1.2  1999/07/04 21:37:31  werner
 * Ported from kernel version 2.0
 *
 *
 *
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/version.h>
#include <linux/poll.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#else
#include <linux/fs.h>
#endif
#include <linux/isdnif.h>
#include "isdn_divert.h"

/*********************************/
/* Variables for interface queue */
/*********************************/
ulong if_used = 0;		/* number of interface users */
static struct divert_info *divert_info_head = NULL;	/* head of queue */
static struct divert_info *divert_info_tail = NULL;	/* pointer to last entry */
static wait_queue_head_t rd_queue;

/*********************************/
/* put an info buffer into queue */
/*********************************/
void
put_info_buffer(char *cp)
{
	struct divert_info *ib;
	int flags;

	if (if_used <= 0)
		return;
	if (!cp)
		return;
	if (!*cp)
		return;
	if (!(ib = (struct divert_info *) kmalloc(sizeof(struct divert_info) + strlen(cp), GFP_ATOMIC)))
		 return;	/* no memory */
	strcpy(ib->info_start, cp);	/* set output string */
	ib->next = NULL;
	save_flags(flags);
	cli();
	ib->usage_cnt = if_used;
	if (!divert_info_head)
		divert_info_head = ib;	/* new head */
	else
		divert_info_tail->next = ib;	/* follows existing messages */
	divert_info_tail = ib;	/* new tail */
	restore_flags(flags);

	/* delete old entrys */
	while (divert_info_head->next) {
		if ((divert_info_head->usage_cnt <= 0) &&
		    (divert_info_head->next->usage_cnt <= 0)) {
			ib = divert_info_head;
			divert_info_head = divert_info_head->next;
			kfree(ib);
		} else
			break;
	}			/* divert_info_head->next */
	wake_up_interruptible(&(rd_queue));
}				/* put_info_buffer */

/**********************************/
/* deflection device read routine */
/**********************************/
static ssize_t
isdn_divert_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	struct divert_info *inf;
	int len;

	if (!*((struct divert_info **) file->private_data)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		interruptible_sleep_on(&(rd_queue));
	}
	if (!(inf = *((struct divert_info **) file->private_data)))
		return (0);

	inf->usage_cnt--;	/* new usage count */
	(struct divert_info **) file->private_data = &inf->next;	/* next structure */
	if ((len = strlen(inf->info_start)) <= count) {
		if (copy_to_user(buf, inf->info_start, len))
			return -EFAULT;
		file->f_pos += len;
		return (len);
	}
	return (0);
}				/* isdn_divert_read */

/**********************************/
/* deflection device write routine */
/**********************************/
static ssize_t
isdn_divert_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
	return (-ENODEV);
}				/* isdn_divert_write */


/***************************************/
/* select routines for various kernels */
/***************************************/
static unsigned int
isdn_divert_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = 0;

	poll_wait(file, &(rd_queue), wait);
	/* mask = POLLOUT | POLLWRNORM; */
	if (*((struct divert_info **) file->private_data)) {
		mask |= POLLIN | POLLRDNORM;
	}
	return mask;
}				/* isdn_divert_poll */

/****************/
/* Open routine */
/****************/
static int
isdn_divert_open(struct inode *ino, struct file *filep)
{
	int flags;

	MOD_INC_USE_COUNT;
	save_flags(flags);
	cli();
	if_used++;
	if (divert_info_head)
		(struct divert_info **) filep->private_data = &(divert_info_tail->next);
	else
		(struct divert_info **) filep->private_data = &divert_info_head;
	restore_flags(flags);
	/*  start_divert(); */
	return (0);
}				/* isdn_divert_open */

/*******************/
/* close routine   */
/*******************/
static int
isdn_divert_close(struct inode *ino, struct file *filep)
{
	struct divert_info *inf;
	int flags;

	save_flags(flags);
	cli();
	if_used--;
	inf = *((struct divert_info **) filep->private_data);
	while (inf) {
		inf->usage_cnt--;
		inf = inf->next;
	}
	restore_flags(flags);
	if (if_used <= 0)
		while (divert_info_head) {
			inf = divert_info_head;
			divert_info_head = divert_info_head->next;
			kfree(inf);
		}
	MOD_DEC_USE_COUNT;
	return (0);
}				/* isdn_divert_close */

/*********/
/* IOCTL */
/*********/
static int
isdn_divert_ioctl(struct inode *inode, struct file *file,
		  uint cmd, ulong arg)
{
	divert_ioctl dioctl;
	int i, flags;
	divert_rule *rulep;
	char *cp;

	if ((i = copy_from_user(&dioctl, (char *) arg, sizeof(dioctl))))
		return (i);

	switch (cmd) {
		case IIOCGETVER:
			dioctl.drv_version = DIVERT_IIOC_VERSION;	/* set version */
			break;

		case IIOCGETDRV:
			if ((dioctl.getid.drvid = divert_if.name_to_drv(dioctl.getid.drvnam)) < 0)
				return (-EINVAL);
			break;

		case IIOCGETNAM:
			cp = divert_if.drv_to_name(dioctl.getid.drvid);
			if (!cp)
				return (-EINVAL);
			if (!*cp)
				return (-EINVAL);
			strcpy(dioctl.getid.drvnam, cp);
			break;

		case IIOCGETRULE:
			if (!(rulep = getruleptr(dioctl.getsetrule.ruleidx)))
				return (-EINVAL);
			dioctl.getsetrule.rule = *rulep;	/* copy data */
			break;

		case IIOCMODRULE:
			if (!(rulep = getruleptr(dioctl.getsetrule.ruleidx)))
				return (-EINVAL);
			save_flags(flags);
			cli();
			*rulep = dioctl.getsetrule.rule;	/* copy data */
			restore_flags(flags);
			return (0);	/* no copy required */
			break;

		case IIOCINSRULE:
			return (insertrule(dioctl.getsetrule.ruleidx, &dioctl.getsetrule.rule));
			break;

		case IIOCDELRULE:
			return (deleterule(dioctl.getsetrule.ruleidx));
			break;

		case IIOCDODFACT:
			return (deflect_extern_action(dioctl.fwd_ctrl.subcmd,
						  dioctl.fwd_ctrl.callid,
						 dioctl.fwd_ctrl.to_nr));

		case IIOCDOCFACT:
		case IIOCDOCFDIS:
		case IIOCDOCFINT:
			if (!divert_if.drv_to_name(dioctl.cf_ctrl.drvid))
				return (-EINVAL);	/* invalid driver */
			if ((i = cf_command(dioctl.cf_ctrl.drvid,
					    (cmd == IIOCDOCFACT) ? 1 : (cmd == IIOCDOCFDIS) ? 0 : 2,
					    dioctl.cf_ctrl.cfproc,
					    dioctl.cf_ctrl.msn,
					    dioctl.cf_ctrl.service,
					    dioctl.cf_ctrl.fwd_nr,
					    &dioctl.cf_ctrl.procid)))
				return (i);
			break;

		default:
			return (-EINVAL);
	}			/* switch cmd */
	return (copy_to_user((char *) arg, &dioctl, sizeof(dioctl)));	/* success */
}				/* isdn_divert_ioctl */


#ifdef CONFIG_PROC_FS
static loff_t
isdn_divert_lseek(struct file *file, loff_t offset, int orig)
{
	return -ESPIPE;
}

static struct file_operations isdn_fops =
{
	llseek:         isdn_divert_lseek,
	read:           isdn_divert_read,
	write:          isdn_divert_write,
	poll:           isdn_divert_poll,
	ioctl:          isdn_divert_ioctl,
	open:           isdn_divert_open,
	release:        isdn_divert_close,                                      
};

/****************************/
/* isdn subdir in /proc/net */
/****************************/
static struct proc_dir_entry *isdn_proc_entry = NULL;
static struct proc_dir_entry *isdn_divert_entry = NULL;
#endif	/* CONFIG_PROC_FS */

/***************************************************************************/
/* divert_dev_init must be called before the proc filesystem may be used   */
/***************************************************************************/
int
divert_dev_init(void)
{

	init_waitqueue_head(&rd_queue);

#ifdef CONFIG_PROC_FS
	isdn_proc_entry = create_proc_entry("isdn", S_IFDIR | S_IRUGO | S_IXUGO, proc_net);
	if (!isdn_proc_entry)
		return (-1);
	isdn_divert_entry = create_proc_entry("divert", S_IFREG | S_IRUGO, isdn_proc_entry);
	if (!isdn_divert_entry) {
		remove_proc_entry("isdn", proc_net);
		return (-1);
	}
	isdn_divert_entry->proc_fops = &isdn_fops; 
#endif	/* CONFIG_PROC_FS */

	return (0);
}				/* divert_dev_init */

/***************************************************************************/
/* divert_dev_deinit must be called before leaving isdn when included as   */
/* a module.                                                               */
/***************************************************************************/
int
divert_dev_deinit(void)
{

#ifdef CONFIG_PROC_FS
	remove_proc_entry("divert", isdn_proc_entry);
	remove_proc_entry("isdn", proc_net);
#endif	/* CONFIG_PROC_FS */

	return (0);
}				/* divert_dev_deinit */

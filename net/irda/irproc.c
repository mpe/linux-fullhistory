/*********************************************************************
 *                
 * Filename:      irproc.c
 * Version:       
 * Description:   Various entries in the /proc file system
 * Status:        Experimental.
 * Author:        Thomas Davis, <ratbert@radiks.net>
 * Created at:    Sat Feb 21 21:33:24 1998
 * Modified at:   Tue Dec 15 09:21:50 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>, 
 *     All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     I, Thomas Davis, provide no warranty for any of this software. 
 *     This material is provided "AS-IS" and at no charge. 
 *     
 *     Portions lifted from the linux/fs/procfs/ files.
 *
 ********************************************************************/

#include <linux/miscdevice.h>
#include <linux/proc_fs.h>

#include <net/irda/irmod.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>

static int proc_irda_21x_lookup(struct inode * dir, struct dentry *dentry);

static int proc_irda_readdir(struct file *filp, void *dirent, 
 			     filldir_t filldir);

extern int irda_device_proc_read( char *buf, char **start, off_t offset, 
				  int len, int unused);
extern int irlap_proc_read( char *buf, char **start, off_t offset, int len, 
			    int unused);
extern int irlmp_proc_read( char *buf, char **start, off_t offset, int len, 
			    int unused);
extern int irttp_proc_read( char *buf, char **start, off_t offset, int len, 
			    int unused);
extern int irias_proc_read( char *buf, char **start, off_t offset, int len,
 			    int unused);

static int proc_discovery_read( char *buf, char **start, off_t offset, int len,
				int unused);

/* int proc_irda_readdir(struct inode *inode, struct file *filp, void *dirent,  */
/* 		      filldir_t filldir); */

enum irda_directory_inos {
	PROC_IRDA_LAP = 1,
	PROC_IRDA_LMP,
	PROC_IRDA_TTP,
	PROC_IRDA_LPT,
	PROC_IRDA_COMM,
 	PROC_IRDA_IRDA_DEVICE,
	PROC_IRDA_IRIAS
};

static struct file_operations proc_irda_dir_operations = {
        NULL,                   /* lseek - default */
        NULL,                   /* read - bad */
        NULL,                   /* write - bad */
        proc_irda_readdir,      /* readdir */
        NULL,                   /* select - default */
        NULL,                   /* ioctl - default */
        NULL,                   /* mmap */
        NULL,                   /* no special open code */
        NULL,                   /* no special release code */
        NULL                    /* can't fsync */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_irda_dir_inode_operations = {
        &proc_irda_dir_operations,   /* default net directory file-ops */
        NULL,                   /* create */
	proc_irda_21x_lookup, 
        NULL,                   /* link */
        NULL,                   /* unlink */
        NULL,                   /* symlink */
        NULL,                   /* mkdir */
        NULL,                   /* rmdir */
        NULL,                   /* mknod */
        NULL,                   /* rename */ 
        NULL,                   /* readlink */
        NULL,                   /* follow_link */
        NULL,                   /* readpage */
        NULL,                   /* writepage */
        NULL,                   /* bmap */
        NULL,                   /* truncate */   
        NULL                    /* permission */
};

struct proc_dir_entry proc_irda = {
	0, 4, "irda",
        S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
        0, &proc_irda_dir_inode_operations,
        NULL, NULL,
        NULL,
        NULL, NULL
};

#if 0 
struct proc_dir_entry proc_lpt = {
	0, 3, "lpt",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL /* ops -- default to array */,
	&irlpt_proc_read /* get_info */,
};
#endif

struct proc_dir_entry proc_discovery = {
	0, 9, "discovery",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL /* ops -- default to array */,
	&proc_discovery_read /* get_info */,
};

struct proc_dir_entry proc_irda_device = {
	0, 11, "irda_device",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL,
	&irda_device_proc_read,
};

struct proc_dir_entry proc_ttp = {
	0, 5, "irttp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL /* ops -- default to array */,
	&irttp_proc_read /* get_info */,
};

struct proc_dir_entry proc_lmp = {
	0, 5, "irlmp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL /* ops -- default to array */,
	&irlmp_proc_read /* get_info */,
};

struct proc_dir_entry proc_lap = {
	0, 5, "irlap",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL /* ops -- default to array */,
	&irlap_proc_read /* get_info */,
};

struct proc_dir_entry proc_ias = {
	0, 5, "irias",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL /* ops -- default to array */,
	&irias_proc_read /* get_info */,
};

/*
 * Function proc_delete_dentry (dentry)
 *
 *    Copy of proc/root.c because this function is invisible to the irda
 *    module
 * 
 */
static void proc_delete_dentry(struct dentry * dentry)
{
	d_drop(dentry);
}

static struct dentry_operations proc_dentry_operations =
{
	NULL,			/* revalidate */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
	proc_delete_dentry	/* d_delete(struct dentry *) */
};

/*
 * Function irda_proc_register (void)
 *
 *    Register irda entry in /proc file system
 *
 */
void irda_proc_register(void) {
	proc_net_register( &proc_irda);
	proc_register( &proc_irda, &proc_lap);
	proc_register( &proc_irda, &proc_lmp);
	proc_register( &proc_irda, &proc_ttp);
	proc_register( &proc_irda, &proc_ias);
 	proc_register( &proc_irda, &proc_irda_device); 
	proc_register( &proc_irda, &proc_discovery);
}

/*
 * Function irda_proc_unregister (void)
 *
 *    Unregister irda entry in /proc file system
 *
 */
void irda_proc_unregister(void) {
	proc_unregister( &proc_irda, proc_discovery.low_ino);
 	proc_unregister(&proc_irda, proc_irda_device.low_ino);
	proc_unregister( &proc_irda, proc_ias.low_ino);
	proc_unregister( &proc_irda, proc_ttp.low_ino);
	proc_unregister( &proc_irda, proc_lmp.low_ino);
	proc_unregister( &proc_irda, proc_lap.low_ino);
	proc_unregister( proc_net, proc_irda.low_ino);
}

/*
 * Function proc_irda_21x_lookup (dir, dentry)
 *
 *    This is a copy of proc_lookup from the linux-2.1.x 
 *
 */
int proc_irda_21x_lookup(struct inode * dir, struct dentry *dentry)
{
	struct inode *inode;
	struct proc_dir_entry * de;
	int error;

	error = -ENOTDIR;
	if (!dir || !S_ISDIR(dir->i_mode))
		goto out;

	error = -ENOENT;
	inode = NULL;
	de = (struct proc_dir_entry *) dir->u.generic_ip;
	if (de) {
		for (de = de->subdir; de ; de = de->next) {
			if (!de || !de->low_ino)
				continue;
			if (de->namelen != dentry->d_name.len)
				continue;
			if (!memcmp(dentry->d_name.name, de->name, de->namelen)) {
				int ino = de->low_ino | (dir->i_ino & ~(0xffff));
				error = -EINVAL;
				inode = proc_get_inode(dir->i_sb, ino, de);
				break;
			}
		}
	}

	if (inode) {
		dentry->d_op = &proc_dentry_operations;
		d_add(dentry, inode);
		error = 0;
	}
out:
	return error;
}

/*
 * Function proc_irda_readdir (filp, dirent, filldir)
 *
 *    This is a copy from linux/fs/proc because the function is invisible
 *    to the irda module
 * 
 */
static int proc_irda_readdir( struct file *filp, void *dirent, 
			      filldir_t filldir)
{
	struct proc_dir_entry * de;
	unsigned int ino;
	int i;
	
	struct inode *inode = filp->f_dentry->d_inode;
	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;
	ino = inode->i_ino;
	de = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!de)
		return -EINVAL;
	i = filp->f_pos;
	switch (i) {
	case 0:
		if (filldir(dirent, ".", 1, i, ino) < 0)
			return 0;
		i++;
		filp->f_pos++;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, i, de->parent->low_ino) < 0)
			return 0;
		i++;
		filp->f_pos++;
		/* fall through */
	default:
		ino &= ~0xffff;
		de = de->subdir;
		i -= 2;
		for (;;) {
			if (!de)
				return 1;
			if (!i)
				break;
			de = de->next;
			i--;
		}
		
		do {
			if (filldir(dirent, de->name, de->namelen, filp->f_pos,
				    ino | de->low_ino) < 0)
				return 0;
			filp->f_pos++;
			de = de->next;
		} while (de);
	}
	return 1;
}

/*
 * Function proc_discovery_read (buf, start, offset, len, unused)
 *
 *    Print discovery information in /proc file system
 *
 */
int proc_discovery_read( char *buf, char **start, off_t offset, int len, 
			 int unused)
{
	DISCOVERY *discovery;
	struct lap_cb *lap;
	unsigned long flags;

	if ( !irlmp)
		return len;

	len = sprintf(buf, "IrLMP: Discovery log:\n\n");	

	save_flags(flags);
	cli();

	lap = ( struct lap_cb *) hashbin_get_first( irlmp->links);
	while( lap != NULL) {
		ASSERT( lap->magic == LMP_LAP_MAGIC, return 0;);
		
		len += sprintf( buf+len, "Link saddr=0x%08x\n", lap->saddr);
		discovery = ( DISCOVERY *) hashbin_get_first( lap->cachelog);
		while ( discovery != NULL) {
			len += sprintf( buf+len, "  name: %s,", 
					discovery->info);
			
			len += sprintf( buf+len, " hint: ");
			if ( discovery->hint[0] & HINT_PNP)
				len += sprintf( buf+len, "PnP Compatible ");
			if ( discovery->hint[0] & HINT_PDA)
				len += sprintf( buf+len, "PDA/Palmtop ");
			if ( discovery->hint[0] & HINT_COMPUTER)
				len += sprintf( buf+len, "Computer ");
			if ( discovery->hint[0] & HINT_PRINTER)
				len += sprintf( buf+len, "Printer ");
			if ( discovery->hint[0] & HINT_MODEM)
				len += sprintf( buf+len, "Modem ");
			if ( discovery->hint[0] & HINT_FAX)
				len += sprintf( buf+len, "Fax ");
			if ( discovery->hint[0] & HINT_LAN)
				len += sprintf( buf+len, "LAN Access");
			
			if ( discovery->hint[1] & HINT_TELEPHONY)
				len += sprintf( buf+len, "Telephony ");
			if ( discovery->hint[1] & HINT_FILE_SERVER)
				len += sprintf( buf+len, "File Server ");       
			if ( discovery->hint[1] & HINT_COMM)
				len += sprintf( buf+len, "IrCOMM ");
			if ( discovery->hint[1] & HINT_OBEX)
				len += sprintf( buf+len, "IrOBEX ");
			
			len += sprintf( buf+len, ", daddr: 0x%08x\n", 
					discovery->daddr);
			
			len += sprintf( buf+len, "\n");
			
			discovery = ( DISCOVERY *) hashbin_get_next( lap->cachelog);
		}
		lap = ( struct lap_cb *) hashbin_get_next( irlmp->links);
	}
	restore_flags(flags);

	return len;
}


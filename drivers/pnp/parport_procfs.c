/* $Id: parport_procfs.c,v 1.1.2.2 1997/03/26 17:50:36 phil Exp $
 * Parallel port /proc interface code.
 * 
 * Authors: David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Tim Waugh <tmw20@cam.ac.uk>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <linux/parport.h>

#if defined(CONFIG_PROC_FS) && defined(NOT_DEFINED)

/************************************************/
static long proc_readparport(struct inode * inode, struct file * file,
							 char * buf, unsigned long count)
{
	printk("proc_readparport\n");
	return 0;
}

static long proc_writeparport(struct inode * inode, struct file * file,
							  const char * buf, unsigned long count)
{
	printk("proc_writeparport\n");
	
	return 0;
}

static long long proc_parportlseek(struct inode * inode, struct file * file, 
				long long offset, int orig)
{
    switch (orig) {
    case 0:
	file->f_pos = offset;
	return(file->f_pos);
    case 1:
	file->f_pos += offset;
	return(file->f_pos);
    case 2:
	return(-EINVAL);
    default:
	return(-EINVAL);
    }
}

static struct file_operations proc_dir_operations = {
    proc_parportlseek,	/* lseek   */
    proc_readparport,	/* read	   */
    proc_writeparport,	/* write   */
    proc_readdir,	    /* readdir */
    NULL,		/* poll    */
    NULL,		/* ioctl   */
    NULL,		/* mmap	   */
    NULL,		/* no special open code	   */
    NULL,		/* no special release code */
    NULL		/* can't fsync */
};

/************************************************/
static struct inode_operations parport_proc_dir_inode_operations = {
	&proc_dir_operations,	/* default net directory file-ops */
	NULL,			/* create */
	proc_lookup,	/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct proc_dir_entry proc_root_parport = {
	PROC_PARPORT, 7, "parport",
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
	0, &parport_proc_dir_inode_operations,
	NULL, NULL,
	NULL, &proc_root, NULL,
	NULL, NULL
};
#endif

int parport_proc_register(struct parport *pp)
{
#if defined(CONFIG_PROC_FS) && defined(NOT_DEFINED)
	return proc_register(&proc_root, &proc_root_parport);
#else
	return 0;
#endif
}

void parport_proc_unregister(struct parport *pp)
{
#if defined(CONFIG_PROC_FS) && defined(NOT_DEFINED)
	if( pp ){
		proc_unregister(&proc_root_parport, pp->proc_dir->low_ino);
		kfree(pp->proc_dir);
	}else{
		proc_unregister(&proc_root, proc_root_parport.low_ino);
	}
#endif
}

/*
 * File operations for Coda.
 * Original version: (C) 1996 Peter Braam 
 * Rewritten for Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <asm/segment.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_psdev.h>
#include <linux/coda_cache.h>
#include <linux/coda_proc.h>

/* file operations */
static int coda_file_mmap(struct file * file, struct vm_area_struct * vma);

/* also exported from this file (used for dirs) */
int coda_fsync(struct file *, struct dentry *dentry);

struct inode_operations coda_file_inode_operations = {
        permission:	coda_permission,
        revalidate:	coda_revalidate_inode,
	setattr:	coda_notify_change,
};

struct file_operations coda_file_operations = {
	read:		generic_file_read,
	write:		generic_file_write,
	mmap:		coda_file_mmap,
	open:		coda_open,
	release:	coda_release,
	fsync:		coda_fsync,
};
 
/*  File operations */

static int coda_file_mmap(struct file * file, struct vm_area_struct * vma)
{
        struct coda_inode_info *cii;
	int res;

	coda_vfs_stat.file_mmap++;

        ENTRY;
	cii = ITOC(file->f_dentry->d_inode);
	cii->c_mmcount++;
  
	res =generic_file_mmap(file, vma);
	EXIT;
	return res;
}

int coda_fsync(struct file *coda_file, struct dentry *coda_dentry)
{
        struct coda_inode_info *cnp;
	struct inode *coda_inode = coda_dentry->d_inode;
        struct inode *cont_inode = NULL;
        struct file  cont_file;
	struct dentry cont_dentry;
        int result = 0;
        ENTRY;
	coda_vfs_stat.fsync++;

	if (!(S_ISREG(coda_inode->i_mode) || S_ISDIR(coda_inode->i_mode) ||
	      S_ISLNK(coda_inode->i_mode)))
		return -EINVAL;

	lock_kernel();
        cnp = ITOC(coda_inode);
        CHECK_CNODE(cnp);

        cont_inode = cnp->c_ovp;
        if ( cont_inode == NULL ) {
                printk("coda_file_write: cached inode is 0!\n");
		unlock_kernel();
                return -1; 
        }

        coda_prepare_openfile(coda_inode, coda_file, cont_inode, 
			      &cont_file, &cont_dentry);

	down(&cont_inode->i_sem);

        result = file_fsync(&cont_file ,&cont_dentry);
	if ( result == 0 ) {
		result = venus_fsync(coda_inode->i_sb, &(cnp->c_fid));
	}

	up(&cont_inode->i_sem);

        coda_restore_codafile(coda_inode, coda_file, cont_inode, &cont_file);
	unlock_kernel();
        return result;
}
/* 
 * support routines
 */

/* instantiate the container file and dentry object to do io */                
void coda_prepare_openfile(struct inode *i, struct file *coda_file, 
			   struct inode *cont_inode, struct file *cont_file,
			   struct dentry *cont_dentry)
{
        cont_file->f_pos = coda_file->f_pos;
        cont_file->f_mode = coda_file->f_mode;
        cont_file->f_flags = coda_file->f_flags;
        atomic_set(&cont_file->f_count, atomic_read(&coda_file->f_count));
        cont_file->f_owner  = coda_file->f_owner;
	cont_file->f_op = cont_inode->i_fop;
	cont_file->f_dentry = cont_dentry;
        cont_file->f_dentry->d_inode = cont_inode;
        return ;
}

/* update the Coda file & inode after I/O */
void coda_restore_codafile(struct inode *coda_inode, struct file *coda_file, 
			   struct inode *open_inode, struct file *open_file)
{
        coda_file->f_pos = open_file->f_pos;
	/* XXX what about setting the mtime here too? */
	/* coda_inode->i_mtime = open_inode->i_mtime; */
	coda_inode->i_size = open_inode->i_size;
        return;
}

/* grab the ext2 inode of the container file */
int coda_inode_grab(dev_t dev, ino_t ino, struct inode **ind)
{
        struct super_block *sbptr;

        sbptr = get_super(dev);

        if ( !sbptr ) {
                printk("coda_inode_grab: coda_find_super returns NULL.\n");
                return -ENXIO;
        }
                
        *ind = NULL;
        *ind = iget(sbptr, ino);

        if ( *ind == NULL ) {
		printk("coda_inode_grab: iget(dev: %d, ino: %ld) "
		       "returns NULL.\n", dev, (long)ino);
                return -ENOENT;
        }
	CDEBUG(D_FILE, "ino: %ld, ops at %p\n", (long)ino, (*ind)->i_op);
        return 0;
}


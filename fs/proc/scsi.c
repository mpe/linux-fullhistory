/*
 *  linux/fs/proc/scsi.c  
 *  (c) 1995 Michael Neuffer neuffer@goofy.zdv.uni-mainz.de
 *
 *  The original version was derived from linux/fs/proc/net.c,
 *  which is Copyright (C) 1991, 1992 Linus Torvalds. 
 *  Much has been rewritten, but some of the code still remains.
 *
 *  /proc/scsi directory handling functions
 *
 *  last change: 95/07/04    
 *
 *  Initial version: March '95
 *  95/05/15 Added subdirectories for each driver and show every
 *	     registered HBA as a single file. 
 *  95/05/30 Added rudimentary write support for parameter passing
 *  95/07/04 Fixed bugs in directory handling
 *  95/09/13 Update to support the new proc-dir tree
 *
 *  TODO: Improve support to write to the driver files
 *	  Add some more comments
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>

#include <asm/uaccess.h>

/* forward references */
static ssize_t proc_readscsi(struct file * file, char * buf,
                             size_t count, loff_t *ppos);
static ssize_t proc_writescsi(struct file * file, const char * buf,
                              size_t count, loff_t *ppos);
static long long proc_scsilseek(struct file *, long long, int);

extern void build_proc_dir_hba_entries(uint);

/* the *_get_info() functions are in the respective scsi driver code */
int (* dispatch_scsi_info_ptr) (int ino, char *buffer, char **start,
				off_t offset, int length, int inout) = 0;

static struct file_operations proc_scsi_operations = {
    proc_scsilseek,	/* lseek   */
    proc_readscsi,	/* read	   */
    proc_writescsi,	/* write   */
    proc_readdir,	/* readdir */
    NULL,		/* poll    */
    NULL,		/* ioctl   */
    NULL,		/* mmap	   */
    NULL,		/* no special open code	   */
    NULL,		/* flush */
    NULL,		/* no special release code */
    NULL		/* can't fsync */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_scsi_inode_operations = {
    &proc_scsi_operations,  /* default scsi directory file-ops */
    NULL,	    /* create	   */
    proc_lookup,    /* lookup	   */
    NULL,	    /* link	   */
    NULL,	    /* unlink	   */
    NULL,	    /* symlink	   */
    NULL,	    /* mkdir	   */
    NULL,	    /* rmdir	   */
    NULL,	    /* mknod	   */
    NULL,	    /* rename	   */
    NULL,	    /* readlink	   */
    NULL,	    /* follow_link */
    NULL,	    /* readpage	   */
    NULL,	    /* writepage   */
    NULL,	    /* bmap	   */
    NULL,	    /* truncate	   */
    NULL	    /* permission  */
};

int get_not_present_info(char *buffer, char **start, off_t offset, int length)
{
    int len, pos, begin;
    
    begin = 0;
    pos = len = sprintf(buffer, 
			"No low-level scsi modules are currently present\n");
    if(pos < offset) {
	len = 0;
	begin = pos;
    }
    
    *start = buffer + (offset - begin);	  /* Start of wanted data */
    len -= (offset - begin);
    if(len > length)
	len = length;
    
    return(len);
}

#define PROC_BLOCK_SIZE (3*1024)     /* 4K page size, but our output routines 
				      * use some slack for overruns 
				      */

static ssize_t proc_readscsi(struct file * file, char * buf,
                             size_t count, loff_t *ppos)
{
    struct inode * inode = file->f_dentry->d_inode; 
    ssize_t length;
    ssize_t bytes = count;
    ssize_t copied = 0;
    ssize_t thistime;
    char * page;
    char * start;
    
    if (!(page = (char *) __get_free_page(GFP_KERNEL)))
	return(-ENOMEM);
    
    while (bytes > 0) {	
	thistime = bytes;
	if(bytes > PROC_BLOCK_SIZE)
	    thistime = PROC_BLOCK_SIZE;
	
	if(dispatch_scsi_info_ptr)
	    length = dispatch_scsi_info_ptr(inode->i_ino, page, &start, 
					    *ppos, thistime, 0);
	else
	    length = get_not_present_info(page, &start, *ppos, thistime);
	if(length < 0) {
	    free_page((ulong) page);
	    return(length);
	}
	
	/*
	 *  We have been given a non page aligned block of
	 *  the data we asked for + a bit. We have been given
	 *  the start pointer and we know the length.. 
	 */
	if (length <= 0)
	    break;
	/*
	 *  Copy the bytes
	 */
	copy_to_user(buf + copied, start, length);
	*ppos += length;	/* Move down the file */
	bytes -= length;
	copied += length;
	
	if(length < thistime)
	    break;  /* End of file */
	
    }
    
    free_page((ulong) page);
    return(copied);
}


static ssize_t proc_writescsi(struct file * file, const char * buf,
                              size_t count, loff_t *ppos)
{
    struct inode * inode = file->f_dentry->d_inode;
    ssize_t ret = 0;
    char * page;
    
    if(count > PROC_BLOCK_SIZE) {
	return(-EOVERFLOW);
    }

    if(dispatch_scsi_info_ptr != NULL) {
        if (!(page = (char *) __get_free_page(GFP_KERNEL)))
            return(-ENOMEM);
	copy_from_user(page, buf, count);
	ret = dispatch_scsi_info_ptr(inode->i_ino, page, 0, 0, count, 1);
    } else 
	return(-ENOPKG);	  /* Nothing here */
    
    free_page((ulong) page);
    return(ret);
}


static long long proc_scsilseek(struct file * file, long long offset, int orig)
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

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

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

#include <asm/segment.h>

/* forward references */
static int proc_readscsi(struct inode * inode, struct file * file,
			 char * buf, int count);
static int proc_writescsi(struct inode * inode, struct file * file,
			 const char * buf, int count);
static int proc_scsilseek(struct inode *, struct file *, off_t, int);

extern void build_proc_dir_hba_entries(uint);

/* the *_get_info() functions are in the respective scsi driver code */
int (* dispatch_scsi_info_ptr) (int ino, char *buffer, char **start,
				off_t offset, int length, int inout) = 0;

static struct file_operations proc_scsi_operations = {
    proc_scsilseek,	/* lseek   */
    proc_readscsi,	/* read	   */
    proc_writescsi,	/* write   */
    proc_readdir,	/* readdir */
    NULL,		/* select  */
    NULL,		/* ioctl   */
    NULL,		/* mmap	   */
    NULL,		/* no special open code	   */
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

static int proc_readscsi(struct inode * inode, struct file * file,
			 char * buf, int count)
{
    int length;
    int bytes = count;
    int copied = 0;
    int thistime;
    char * page;
    char * start;
    
    if (count < -1)		  /* Normally I wouldn't do this, */ 
	return(-EINVAL);	  /* but it saves some redundant code.
				   * Now it is possible to seek to the 
				   * end of the file */
    if (!(page = (char *) __get_free_page(GFP_KERNEL)))
	return(-ENOMEM);
    
    while(bytes > 0 || count == -1) {	
	thistime = bytes;
	if(bytes > PROC_BLOCK_SIZE || count == -1)
	    thistime = PROC_BLOCK_SIZE;
	
	if(dispatch_scsi_info_ptr)
	    length = dispatch_scsi_info_ptr(inode->i_ino, page, &start, 
					    file->f_pos, thistime, 0);
	else
	    length = get_not_present_info(page, &start, file->f_pos, thistime);
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
	 *  Copy the bytes, if we're not doing a seek to 
	 *	the end of the file 
	 */
	if (count != -1)
	    memcpy_tofs(buf + copied, start, length);
	file->f_pos += length;	/* Move down the file */
	bytes -= length;
	copied += length;
	
	if(length < thistime)
	    break;  /* End of file */
	
    }
    
    free_page((ulong) page);
    return(copied);
}


static int proc_writescsi(struct inode * inode, struct file * file,
			 const char * buf, int count)
{
    int ret = 0;
    char * page;
    
    if(count > PROC_BLOCK_SIZE) {
	return(-EOVERFLOW);
    }

    if(dispatch_scsi_info_ptr != NULL) {
        if (!(page = (char *) __get_free_page(GFP_KERNEL)))
            return(-ENOMEM);
	memcpy_fromfs(page, buf, count);
	ret = dispatch_scsi_info_ptr(inode->i_ino, page, 0, 0, count, 1);
    } else 
	return(-ENOPKG);	  /* Nothing here */
    
    free_page((ulong) page);
    return(ret);
}


static int proc_scsilseek(struct inode * inode, struct file * file, 
			  off_t offset, int orig)
{
    switch (orig) {
    case 0:
	file->f_pos = offset;
	return(file->f_pos);
    case 1:
	file->f_pos += offset;
	return(file->f_pos);
    case 2:		     /* This ugly hack allows us to    */
	if (offset)	     /* to determine the length of the */
	    return(-EINVAL); /* file and then later safely to  */ 
	proc_readscsi(inode, file, 0, -1); /* seek in it       */ 
	return(file->f_pos);
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

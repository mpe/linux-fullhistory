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
 *  last change: 95/06/13    
 *
 *  Initial version: March '95
 *  95/15/05 Added subdirectories for each driver and show every
 *	     registered HBA as a single file. 
 *  95/30/05 Added rudimentary write support for parameter passing
 *
 *  TODO: Improve support to write to the driver files
 *	  Optimize directory handling 
 *	  Add some more comments
 */
#include <linux/autoconf.h>
#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <linux/mm.h>

/* forward references */
static int proc_readscsi(struct inode * inode, struct file * file,
			 char * buf, int count);
static int proc_writescsi(struct inode * inode, struct file * file,
			 char * buf, int count);
static int proc_readscsidir(struct inode *, struct file *, 
			    void *, filldir_t filldir);
static int proc_lookupscsi(struct inode *,const char *,int,struct inode **);
static int proc_scsilseek(struct inode *, struct file *, off_t, int);

extern uint count_templates(void);
extern void build_proc_dir_hba_entries(uint);

/* the *_get_info() functions are in the respective scsi driver code */
extern int (* dispatch_scsi_info_ptr)(int, char *, char **, off_t, int, int);
    
    
static struct file_operations proc_scsi_operations = {
    proc_scsilseek,	/* lseek   */
    proc_readscsi,	/* read	   */
    proc_writescsi,	/* write   */
    proc_readscsidir,	/* readdir */
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
    proc_lookupscsi,/* lookup	   */
    NULL,	    /* link	   */
    NULL,	    /* unlink	   */
    NULL,	    /* symlink	   */
    NULL,	    /* mkdir	   */
    NULL,	    /* rmdir	   */
    NULL,	    /* mknod	   */
    NULL,	    /* rename	   */
    NULL,	    /* readlink	   */
    NULL,	    /* follow_link */
    NULL,	    /* bmap	   */
    NULL,	    /* truncate	   */
    NULL	    /* permission  */
};

struct proc_dir_entry scsi_dir[PROC_SCSI_FILE - PROC_SCSI_SCSI + 3]; 
struct proc_dir_entry scsi_hba_dir[(PROC_SCSI_LAST - PROC_SCSI_FILE) * 4]; 

static struct proc_dir_entry scsi_dir2[] = {
    { PROC_SCSI,		 1, "." },
    { PROC_ROOT_INO,		 2, ".." },
    { PROC_SCSI_NOT_PRESENT,	11, "not.present" },
    { 0, 0, NULL }
};

inline static uint count_dir_entries(uint inode)
{
    struct proc_dir_entry *dir;
    uint i = 0;
    
    
    if(dispatch_scsi_info_ptr)
	if (inode <= PROC_SCSI_SCSI_DEBUG)
	    dir = scsi_dir;
	else
	    dir = scsi_hba_dir;
    else dir = scsi_dir2;
    
    while(dir[i].low_ino)
	i++;
    
    return(i);
}

static int proc_lookupscsi(struct inode * dir, const char * name, int len,
			   struct inode ** result)
{
    struct proc_dir_entry *de = NULL;
    
    *result = NULL;
    if (!dir)
	return(-ENOENT);
    if (!S_ISDIR(dir->i_mode)) {
	iput(dir);
	return(-ENOENT);
    }
    if (dispatch_scsi_info_ptr != NULL)
	if (dir->i_ino <= PROC_SCSI_SCSI)
	    de = scsi_dir;
	else {
	    de = &scsi_hba_dir[dispatch_scsi_info_ptr(dir->i_ino, 0, 0, 0, 0, 2)];
	}
    else
	de = scsi_dir2;
    
    for (; de->name ; de++) {
	if (!proc_match(len, name, de))
	    continue;
	*result = iget(dir->i_sb, de->low_ino);
	iput(dir);
	if (!*result)
	    return(-ENOENT);
	return(0);
    }
    iput(dir);
    return(-ENOENT);
}

static int proc_readscsidir(struct inode * inode, struct file * filp,
			    void * dirent, filldir_t filldir)
{
    struct proc_dir_entry * de;
    unsigned int ino;
    
    if (!inode || !S_ISDIR(inode->i_mode))
	return(-EBADF);
    ino = inode->i_ino;
    while (((unsigned) filp->f_pos) < count_dir_entries(ino)) {
	if (dispatch_scsi_info_ptr)
	    if (ino <= PROC_SCSI_SCSI)
		de = scsi_dir + filp->f_pos;
	    else
		de = scsi_hba_dir + filp->f_pos;
	else
	    de = scsi_dir2 + filp->f_pos;
	if (filldir(dirent, de->name, de->namelen, filp->f_pos, de->low_ino)<0)
	    break;
	filp->f_pos++;
    }
    return(0);
}

int get_not_present_info(char *buffer, char **start, off_t offset, int length)
{
    int len, pos, begin;
    
    begin = 0;
    pos = len = sprintf(buffer, 
			"The scsi core module is currently not present\n");
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
    uint ino;
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
    ino = inode->i_ino;
    
    
    while(bytes > 0 || count == -1)
    {
	
	thistime = bytes;
	if(bytes > PROC_BLOCK_SIZE || count == -1)
	    thistime = PROC_BLOCK_SIZE;
	
	if(dispatch_scsi_info_ptr)
	    length = dispatch_scsi_info_ptr(ino, page, &start, 
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
			 char * buf, int count)
{
    uint ino;
    int ret = 0;
    char * page;
    
    if (!(page = (char *) __get_free_page(GFP_KERNEL)))
	return(-ENOMEM);

    if(count > PROC_BLOCK_SIZE) {
	return(-EOVERFLOW);
    }

    ino = inode->i_ino;
    
    if(dispatch_scsi_info_ptr != NULL) {
	memcpy_fromfs(page, buf, count);
	ret = dispatch_scsi_info_ptr(ino, page, 0, 0, count, 1);
    } else {
	free_page((ulong) page);   
	return(-ENOPKG);	  /* Nothing here */
    }
    
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
	    return(-EINVAL); /* file and then later savely to  */ 
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

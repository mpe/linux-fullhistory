/*
 *  linux/fs/ncpfs/symlink.c
 *
 *  Code for allowing symbolic links on NCPFS (i.e. NetWare)
 *  Symbolic links are not supported on native NetWare, so we use an
 *  infrequently-used flag (Sh) and store a two-word magic header in
 *  the file to make sure we don't accidentally use a non-link file
 *  as a link.
 *
 *  from linux/fs/ext2/symlink.c
 *
 *  Copyright (C) 1998-99, Frank A. Vorstenbosch
 *
 *  ncpfs symlink handling code
 *  NLS support (c) 1999 Petr Vandrovec
 *
 */

#include <linux/config.h>

#ifdef CONFIG_NCPFS_EXTRAS

#include <asm/uaccess.h>
#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ncp_fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include "ncplib_kernel.h"


/* these magic numbers must appear in the symlink file -- this makes it a bit
   more resilient against the magic attributes being set on random files. */

#define NCP_SYMLINK_MAGIC0	le32_to_cpu(0x6c6d7973)     /* "symlnk->" */
#define NCP_SYMLINK_MAGIC1	le32_to_cpu(0x3e2d6b6e)

static int ncp_readlink(struct dentry *, char *, int);
static struct dentry *ncp_follow_link(struct dentry *, struct dentry *, unsigned int);
int ncp_create_new(struct inode *dir, struct dentry *dentry,
                          int mode,int attributes);

/*
 * symlinks can't do much...
 */
struct inode_operations ncp_symlink_inode_operations={
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	ncp_readlink,		/* readlink */
	ncp_follow_link,	/* follow_link */
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

/* ----- follow a symbolic link ------------------------------------------ */

static struct dentry *ncp_follow_link(struct dentry *dentry,
				      struct dentry *base,
				      unsigned int follow)
{
	struct inode *inode=dentry->d_inode;
	int error, length, cnt;
	char *link;

#ifdef DEBUG
	printk("ncp_follow_link(dentry=%p,base=%p,follow=%u)\n",dentry,base,follow);
#endif

	if(!S_ISLNK(inode->i_mode)) {
		dput(base);
		return ERR_PTR(-EINVAL);
	}

	if(ncp_make_open(inode,O_RDONLY)) {
		dput(base);
		return ERR_PTR(-EIO);
	}

	for (cnt = 0; (link=(char *)kmalloc(NCP_MAX_SYMLINK_SIZE+1, GFP_NFS))==NULL; cnt++) {
		if (cnt > 10) {
			dput(base);
			return ERR_PTR(-EAGAIN); /* -ENOMEM? */
		}
		schedule();
	}

	error=ncp_read_kernel(NCP_SERVER(inode),NCP_FINFO(inode)->file_handle,
                         0,NCP_MAX_SYMLINK_SIZE,link,&length);

	if (error!=0 || length<NCP_MIN_SYMLINK_SIZE || 
	   ((__u32 *)link)[0]!=NCP_SYMLINK_MAGIC0 || ((__u32 *)link)[1]!=NCP_SYMLINK_MAGIC1) {
		dput(base);
		kfree(link);
		return ERR_PTR(-EIO);
	}
 
	link[length]=0;

	vol2io(NCP_SERVER(inode), link+8, 0);
	
	/* UPDATE_ATIME(inode); */
	base=lookup_dentry(link+8, base, follow);
	kfree(link);

	return base;
}

/* ----- read symbolic link ---------------------------------------------- */

static int ncp_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	struct inode *inode=dentry->d_inode;
	char *link;
	int length,error;

#ifdef DEBUG
	printk("ncp_readlink(dentry=%p,buffer=%p,buflen=%d)\n",dentry,buffer,buflen);
#endif

	if(!S_ISLNK(inode->i_mode))
		return -EINVAL;

	if(ncp_make_open(inode,O_RDONLY))
		return -EIO;

	if((link=(char *)kmalloc(NCP_MAX_SYMLINK_SIZE+1,GFP_NFS))==NULL)
		return -ENOMEM;

	error = ncp_read_kernel(NCP_SERVER(inode),NCP_FINFO(inode)->file_handle,
		0,NCP_MAX_SYMLINK_SIZE,link,&length);

	if (error!=0 || length < NCP_MIN_SYMLINK_SIZE || buflen < (length-8) ||
	   ((__u32 *)link)[0]!=NCP_SYMLINK_MAGIC0 ||((__u32 *)link)[1]!=NCP_SYMLINK_MAGIC1) {
	   	error = -EIO;
		goto out;
	}

	link[length] = 0;

	vol2io(NCP_SERVER(inode), link+8, 0);
	
	error = length - 8;
	if(copy_to_user(buffer, link+8, error))
		error = -EFAULT;
      
out:;
	kfree(link);
	return error;
}

/* ----- create a new symbolic link -------------------------------------- */
 
int ncp_symlink(struct inode *dir, struct dentry *dentry, const char *symname) {
	int i,length;
	struct inode *inode;
	char *link;

#ifdef DEBUG
	printk("ncp_symlink(dir=%p,dentry=%p,symname=%s)\n",dir,dentry,symname);
#endif

	if (!(NCP_SERVER(dir)->m.flags & NCP_MOUNT_SYMLINKS))
		return -EPERM;	/* EPERM is returned by VFS if symlink procedure does not exist */

	if ((length=strlen(symname))>NCP_MAX_SYMLINK_SIZE)
		return -EINVAL;

	if ((link=(char *)kmalloc(length+9,GFP_NFS))==NULL)
		return -ENOMEM;

	if (ncp_create_new(dir,dentry,0,aSHARED|aHIDDEN)) {
		kfree(link);
		return -EIO;
	}

	inode=dentry->d_inode;

	((__u32 *)link)[0]=NCP_SYMLINK_MAGIC0;
	((__u32 *)link)[1]=NCP_SYMLINK_MAGIC1;
	memcpy(link+8, symname, length+1); /* including last zero for io2vol */

	/* map to/from server charset, do not touch upper/lower case as
	   symlink can point out of ncp filesystem */
	io2vol(NCP_SERVER(inode), link+8, 0);
	
	if(ncp_write_kernel(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle, 
	    		    0, length+8, link, &i) || i!=length+8) {
		kfree(link);
		return -EIO;
	}

	kfree(link);
	return 0;
}
#endif

/* ----- EOF ----- */

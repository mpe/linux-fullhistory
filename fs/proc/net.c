/*
 *  linux/fs/proc/net.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  gjh 3/'93 heim@peanuts.informatik.uni-tuebingen.de (Gerald J. Heim)
 *            most of this file is stolen from base.c
 *            it works, but you shouldn't use it as a guideline
 *            for new proc-fs entries. once i'll make it better.
 * fvk 3/'93  waltje@uwalt.nl.mugnet.org (Fred N. van Kempen)
 *	      cleaned up the whole thing, moved "net" specific code to
 *	      the NET kernel layer (where it belonged in the first place).
 * Michael K. Johnson (johnsonm@stolaf.edu) 3/93
 *            Added support from my previous inet.c.  Cleaned things up
 *            quite a bit, modularized the code.
 * fvk 4/'93  waltje@uwalt.nl.mugnet.org (Fred N. van Kempen)
 *	      Renamed "route_get_info()" to "rt_get_info()" for consistency.
 * Alan Cox (gw4pts@gw4pts.ampr.org) 4/94
 *	      Dusted off the code and added IPX. Fixed the 4K limit.
 * Erik Schoenfelder (schoenfr@ibr.cs.tu-bs.de)
 *	      /proc/net/snmp.
 * Alan Cox (gw4pts@gw4pts.ampr.org) 1/95
 *	      Added Appletalk slots
 *
 *  proc net directory handling functions
 */
#include <linux/autoconf.h>

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/config.h>
#include <linux/mm.h>

/* forward references */
static int proc_readnet(struct inode * inode, struct file * file,
			 char * buf, int count);
static int proc_readnetdir(struct inode *, struct file *,
			   void *, filldir_t filldir);
static int proc_lookupnet(struct inode *,const char *,int,struct inode **);

static struct file_operations proc_net_operations = {
	NULL,			/* lseek - default */
	proc_readnet,		/* read - bad */
	NULL,			/* write - bad */
	proc_readnetdir,	/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_net_inode_operations = {
	&proc_net_operations,	/* default net directory file-ops */
	NULL,			/* create */
	proc_lookupnet,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

#define NR_MAX_PROC_NET_DIR 100
static struct proc_dir_entry *net_dir[NR_MAX_PROC_NET_DIR] = {
	NULL,
};

static int nr_net_direntry = 0;

int proc_net_register(struct proc_dir_entry *dp)
{
	int i;

	for (i = 0; net_dir[i] != NULL; ++i ) ;

	if (i >= NR_MAX_PROC_NET_DIR)
	  return -ENOMEM;

	net_dir[i] = dp;
	net_dir[i+1] = NULL; /* Just make sure.. */
	++nr_net_direntry;
	return i;
}

int proc_net_unregister(int ino)
{
	int i;
	for (i = 0; net_dir[i] != NULL && i < nr_net_direntry; ++i)
	  if (net_dir[i]->low_ino == ino) {
	    for ( ; net_dir[i] != NULL; ++i )
	      net_dir[i] = net_dir[i+1];
	    --nr_net_direntry;
	    return 0;
	  }
	return -ENOENT;
}

static int dir_get_info(char * a, char ** b, off_t d, int e, int f)
{
	return -EISDIR;
}

void proc_net_init(void)
{
	static struct proc_dir_entry
	  nd_thisdir = { PROC_NET, dir_get_info, 1, "." },
	  nd_rootdir = { PROC_ROOT_INO, dir_get_info, 1, ".." };
	static int already = 0;

	if (already) return;
	already = 1;

	proc_net_register(&nd_thisdir);
	proc_net_register(&nd_rootdir);
}


static int proc_lookupnet(struct inode * dir,const char * name, int len,
			  struct inode ** result)
{
	struct proc_dir_entry **de;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	for (de = net_dir ; (*de)->name ; de++) {
		if (!proc_match(len, name, *de))
			continue;
		*result = iget(dir->i_sb, (*de)->low_ino);
		iput(dir);
		if (!*result)
			return -ENOENT;
		return 0;
	}
	iput(dir);
	return -ENOENT;
}

static int proc_readnetdir(struct inode * inode, struct file * filp,
	void * dirent, filldir_t filldir)
{
	struct proc_dir_entry * de;
	unsigned int ino;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
	ino = inode->i_ino;
	while (((unsigned) filp->f_pos) < nr_net_direntry) {
		de = net_dir[filp->f_pos];
		if (filldir(dirent, de->name, de->namelen, filp->f_pos, de->low_ino) < 0)
			break;
		filp->f_pos++;
	}
	return 0;
}


#define PROC_BLOCK_SIZE	(3*1024)		/* 4K page size but our output routines use some slack for overruns */

static int proc_readnet(struct inode * inode, struct file * file,
			char * buf, int count)
{
	char * page;
	unsigned int ino;
	int bytes=count;
	int i;
	int copied=0;
	char *start;
	struct proc_dir_entry * dp;

	if (count < 0)
		return -EINVAL;
	ino = inode->i_ino;
	for (i = 0; ;i++) {
		if (i >= NR_MAX_PROC_NET_DIR || (dp = net_dir[i]) == NULL)
			return -EBADF;
		if (dp->low_ino == ino)
			break;
	}
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	while (bytes>0)
	{
		int length, thistime=bytes;
		if (bytes > PROC_BLOCK_SIZE)
			thistime=PROC_BLOCK_SIZE;

		length = dp->get_info(page, &start,
				      file->f_pos,
				      thistime,
				      (file->f_flags & O_ACCMODE) == O_RDWR);

		/*
 		 *	We have been given a non page aligned block of
		 *	the data we asked for + a bit. We have been given
 		 *	the start pointer and we know the length.. 
		 */

		if (length <= 0)
			break;
		/*
 		 *	Copy the bytes
		 */
		memcpy_tofs(buf+copied, start, length);
		file->f_pos += length;	/* Move down the file */
		bytes  -= length;
		copied += length;
		if (length<thistime)
			break;	/* End of file */
	}
	free_page((unsigned long) page);
	return copied;
}

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
 *
 *  proc net directory handling functions
 */
#include <linux/autoconf.h>

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

/* forward references */
static int proc_readnet(struct inode * inode, struct file * file,
			 char * buf, int count);
static int proc_readnetdir(struct inode *, struct file *,
			   struct dirent *, int);
static int proc_lookupnet(struct inode *,const char *,int,struct inode **);

/* the get_*_info() functions are in the net code, and are configured
   in via the standard mechanism... */
extern int unix_get_info(char *, char **, off_t, int);
#ifdef CONFIG_INET
extern int tcp_get_info(char *, char **, off_t, int);
extern int udp_get_info(char *, char **, off_t, int);
extern int raw_get_info(char *, char **, off_t, int);
extern int arp_get_info(char *, char **, off_t, int);
extern int rarp_get_info(char *, char **, off_t, int);
extern int dev_get_info(char *, char **, off_t, int);
extern int rt_get_info(char *, char **, off_t, int);
extern int snmp_get_info(char *, char **, off_t, int);
#endif /* CONFIG_INET */
#ifdef CONFIG_IPX
extern int ipx_get_info(char *, char **, off_t, int);
extern int ipx_rt_get_info(char *, char **, off_t, int);
#endif /* CONFIG_IPX */
#ifdef CONFIG_AX25
extern int ax25_get_info(char *, char **, off_t, int);
extern int ax25_rt_get_info(char *, char **, off_t, int);
#ifdef CONFIG_NETROM
extern int nr_get_info(char *, char **, off_t, int);
extern int nr_nodes_get_info(char *, char **, off_t, int);
extern int nr_neigh_get_info(char *, char **, off_t, int);
#endif /* CONFIG_NETROM */
#endif /* CONFIG_AX25 */


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

static struct proc_dir_entry net_dir[] = {
	{ 1,2,".." },
	{ 8,1,"." },
	{ 128,4,"unix" }
#ifdef CONFIG_INET
	,{ 129,3,"arp" },
	{ 130,5,"route" },
	{ 131,3,"dev" },
	{ 132,3,"raw" },
	{ 133,3,"tcp" },
	{ 134,3,"udp" },
	{ 135,4,"snmp" }
#ifdef CONFIG_INET_RARP
	,{ 136,4,"rarp"}
#endif
#endif	/* CONFIG_INET */
#ifdef CONFIG_IPX
	,{ 137,9,"ipx_route" },
	{ 138,3,"ipx" }
#endif /* CONFIG_IPX */
#ifdef CONFIG_AX25
	,{ 139,10,"ax25_route" },
	{ 140,4,"ax25" }
#ifdef CONFIG_NETROM
	,{ 141,8,"nr_nodes" },
	{ 142,8,"nr_neigh" },
	{ 143,2,"nr" }
#endif /* CONFIG_NETROM */
#endif /* CONFIG_AX25 */
};

#define NR_NET_DIRENTRY ((sizeof (net_dir))/(sizeof (net_dir[0])))


static int proc_lookupnet(struct inode * dir,const char * name, int len,
	struct inode ** result)
{
	unsigned int ino;
	int i;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	i = NR_NET_DIRENTRY;
	while (i-- > 0 && !proc_match(len,name,net_dir+i))
		/* nothing */;
	if (i < 0) {
		iput(dir);
		return -ENOENT;
	}
	ino = net_dir[i].low_ino;
	if (!(*result = iget(dir->i_sb,ino))) {
		iput(dir);
		return -ENOENT;
	}
	iput(dir);
	return 0;
}

static int proc_readnetdir(struct inode * inode, struct file * filp,
	struct dirent * dirent, int count)
{
	struct proc_dir_entry * de;
	unsigned int ino;
	int i,j;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
	ino = inode->i_ino;
	if (((unsigned) filp->f_pos) < NR_NET_DIRENTRY) {
		de = net_dir + filp->f_pos;
		filp->f_pos++;
		i = de->namelen;
		ino = de->low_ino;
		put_fs_long(ino, &dirent->d_ino);
		put_fs_word(i,&dirent->d_reclen);
		put_fs_byte(0,i+dirent->d_name);
		j = i;
		while (i--)
			put_fs_byte(de->name[i], i+dirent->d_name);
		return j;
	}
	return 0;
}


#define PROC_BLOCK_SIZE	(3*1024)		/* 4K page size but our output routines use some slack for overruns */

static int proc_readnet(struct inode * inode, struct file * file,
			char * buf, int count)
{
	char * page;
	int length;
	unsigned int ino;
	int bytes=count;
	int thistime;
	int copied=0;
	char *start;

	if (count < 0)
		return -EINVAL;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	ino = inode->i_ino;

	while(bytes>0)
	{
		thistime=bytes;
		if(bytes>PROC_BLOCK_SIZE)
			thistime=PROC_BLOCK_SIZE;

		switch (ino) 
		{
			case 128:
				length = unix_get_info(page,&start,file->f_pos,thistime);
				break;
#ifdef CONFIG_INET
			case 129:
				length = arp_get_info(page,&start,file->f_pos,thistime);
				break;
			case 130:
				length = rt_get_info(page,&start,file->f_pos,thistime);
				break;
			case 131:
				length = dev_get_info(page,&start,file->f_pos,thistime);
				break;
			case 132:
				length = raw_get_info(page,&start,file->f_pos,thistime);
				break;
			case 133:
				length = tcp_get_info(page,&start,file->f_pos,thistime);
				break;
			case 134:
				length = udp_get_info(page,&start,file->f_pos,thistime);
				break;
			case 135:
				length = snmp_get_info(page, &start, file->f_pos,thistime);
				break;
#ifdef CONFIG_INET_RARP				
			case 136:
				length = rarp_get_info(page,&start,file->f_pos,thistime);
				break;
#endif /* CONFIG_INET_RARP */				
#endif /* CONFIG_INET */
#ifdef CONFIG_IPX
			case 137:
				length = ipx_rt_get_info(page,&start,file->f_pos,thistime);
				break;
			case 138:
				length = ipx_get_info(page,&start,file->f_pos,thistime);
				break;
#endif /* CONFIG_IPX */
#ifdef CONFIG_AX25
			case 139:
				length = ax25_rt_get_info(page,&start,file->f_pos,thistime);
				break;
			case 140:
				length = ax25_get_info(page,&start,file->f_pos,thistime);
				break;
#ifdef CONFIG_NETROM
			case 141:
				length = nr_nodes_get_info(page,&start,file->f_pos,thistime);
				break;
			case 142:
				length = nr_neigh_get_info(page,&start,file->f_pos,thistime);
				break;
			case 143:
				length = nr_get_info(page,&start,file->f_pos,thistime);
				break;
#endif /* CONFIG_NETROM */
#endif /* CONFIG_AX25 */

			default:
				free_page((unsigned long) page);
				return -EBADF;
		}
		
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
		file->f_pos+=length;	/* Move down the file */
		bytes-=length;
		copied+=length;
		if(length<thistime)
			break;	/* End of file */
	}
	free_page((unsigned long) page);
	return copied;

}

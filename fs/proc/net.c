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
extern int afinet_get_info(char *, char **, off_t, int);
#if	defined(CONFIG_WAVELAN)
extern int wavelan_get_info(char *, char **, off_t, int);
#endif	/* defined(CONFIG_WAVELAN) */
#ifdef CONFIG_IP_ACCT
extern int ip_acct_procinfo(char *, char **, off_t, int, int);
#endif /* CONFIG_IP_ACCT */
#ifdef CONFIG_IP_FIREWALL
extern int ip_fw_blk_procinfo(char *, char **, off_t, int, int);
extern int ip_fw_fwd_procinfo(char *, char **, off_t, int, int);
#endif /* CONFIG_IP_FIREWALL */
extern int ip_msqhst_procinfo(char *, char **, off_t, int);
extern int ip_mc_procinfo(char *, char **, off_t, int);
#endif /* CONFIG_INET */
#ifdef CONFIG_IPX
extern int ipx_get_info(char *, char **, off_t, int);
extern int ipx_rt_get_info(char *, char **, off_t, int);
extern int ipx_get_interface_info(char *, char **, off_t , int);
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
#ifdef CONFIG_ATALK
extern int atalk_get_info(char *, char **, off_t, int);
extern int atalk_rt_get_info(char *, char **, off_t, int);
extern int atalk_if_get_info(char *, char **, off_t, int);
#endif


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
	{ PROC_NET,		1, "." },
	{ PROC_ROOT_INO,	2, ".." },
	{ PROC_NET_UNIX,	4, "unix" },
#ifdef CONFIG_INET
	{ PROC_NET_ARP,		3, "arp" },
	{ PROC_NET_ROUTE,	5, "route" },
	{ PROC_NET_DEV,		3, "dev" },
	{ PROC_NET_RAW,		3, "raw" },
	{ PROC_NET_TCP,		3, "tcp" },
	{ PROC_NET_UDP,		3, "udp" },
	{ PROC_NET_SNMP,	4, "snmp" },
	{ PROC_NET_SOCKSTAT,	8, "sockstat" },
#ifdef CONFIG_INET_RARP
	{ PROC_NET_RARP,	4, "rarp"},
#endif
#ifdef CONFIG_IP_MULTICAST
	{ PROC_NET_IGMP,	4, "igmp"},
#endif
#ifdef CONFIG_IP_FIREWALL
	{ PROC_NET_IPFWFWD,	10, "ip_forward"},
	{ PROC_NET_IPFWBLK,	8,  "ip_block"},
#endif
#ifdef CONFIG_IP_MASQUERADE
	{ PROC_NET_IPMSQHST,	13, "ip_masquerade"},
#endif
#ifdef CONFIG_IP_ACCT
	{ PROC_NET_IPACCT,	7,  "ip_acct"},
#endif
#if	defined(CONFIG_WAVELAN)
	{ PROC_NET_WAVELAN,	7, "wavelan" },
#endif	/* defined(CONFIG_WAVELAN) */
#endif	/* CONFIG_INET */
#ifdef CONFIG_IPX
	{ PROC_NET_IPX_ROUTE,	9, "ipx_route" },
	{ PROC_NET_IPX,		3, "ipx" },
	{ PROC_NET_IPX_INTERFACE, 13, "ipx_interface" },
#endif /* CONFIG_IPX */
#ifdef CONFIG_AX25
	{ PROC_NET_AX25_ROUTE,	10, "ax25_route" },
	{ PROC_NET_AX25,	4, "ax25" },
#ifdef CONFIG_NETROM
	{ PROC_NET_NR_NODES,	8, "nr_nodes" },
	{ PROC_NET_NR_NEIGH,	8, "nr_neigh" },
	{ PROC_NET_NR,		2, "nr" },
#endif /* CONFIG_NETROM */
#endif /* CONFIG_AX25 */
#ifdef CONFIG_ATALK
	{ PROC_NET_ATALK,	9, "appletalk" },
	{ PROC_NET_AT_ROUTE,	11,"atalk_route" },
	{ PROC_NET_ATIF,	11,"atalk_iface" },
#endif /* CONFIG_ATALK */
	{ 0, 0, NULL }
};

#define NR_NET_DIRENTRY ((sizeof (net_dir))/(sizeof (net_dir[0])) - 1)

static int proc_lookupnet(struct inode * dir,const char * name, int len,
	struct inode ** result)
{
	struct proc_dir_entry *de;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	for (de = net_dir ; de->name ; de++) {
		if (!proc_match(len, name, de))
			continue;
		*result = iget(dir->i_sb, de->low_ino);
		iput(dir);
		if (!*result)
			return -ENOENT;
		return 0;
	}
	iput(dir);
	return -ENOENT;
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
			case PROC_NET_UNIX:
				length = unix_get_info(page,&start,file->f_pos,thistime);
				break;
#ifdef CONFIG_INET
			case PROC_NET_SOCKSTAT:
				length = afinet_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_ARP:
				length = arp_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_ROUTE:
				length = rt_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_DEV:
				length = dev_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_RAW:
				length = raw_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_TCP:
				length = tcp_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_UDP:
				length = udp_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_SNMP:
				length = snmp_get_info(page, &start, file->f_pos,thistime);
				break;
#ifdef CONFIG_IP_MULTICAST
			case PROC_NET_IGMP:
				length = ip_mc_procinfo(page, &start, file->f_pos,thistime);
				break;
#endif
#ifdef CONFIG_IP_FIREWALL
			case PROC_NET_IPFWFWD:
				length = ip_fw_fwd_procinfo(page, &start, file->f_pos,
					thistime, (file->f_flags & O_ACCMODE) == O_RDWR);
				break;
			case PROC_NET_IPFWBLK:
				length = ip_fw_blk_procinfo(page, &start, file->f_pos,
					thistime, (file->f_flags & O_ACCMODE) == O_RDWR);
				break;
#endif
#ifdef CONFIG_IP_ACCT
			case PROC_NET_IPACCT:
				length = ip_acct_procinfo(page, &start, file->f_pos,
					thistime, (file->f_flags & O_ACCMODE) == O_RDWR);
				break;
#endif
#ifdef CONFIG_IP_MASQUERADE
			case PROC_NET_IPMSQHST:
				length = ip_msqhst_procinfo(page, &start, file->f_pos,thistime);
				break;
#endif
#ifdef CONFIG_INET_RARP				
			case PROC_NET_RARP:
				length = rarp_get_info(page,&start,file->f_pos,thistime);
				break;
#endif /* CONFIG_INET_RARP */				
#if	defined(CONFIG_WAVELAN)
			case PROC_NET_WAVELAN:
				length = wavelan_get_info(page, &start, file->f_pos, thistime);
				break;
#endif	/* defined(CONFIG_WAVELAN) */
#endif /* CONFIG_INET */
#ifdef CONFIG_IPX
			case PROC_NET_IPX_INTERFACE:
				length = ipx_get_interface_info(page, &start, file->f_pos, thistime);
				break;
			case PROC_NET_IPX_ROUTE:
				length = ipx_rt_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_IPX:
				length = ipx_get_info(page,&start,file->f_pos,thistime);
				break;
#endif /* CONFIG_IPX */
#ifdef CONFIG_ATALK
			case PROC_NET_ATALK:
				length = atalk_get_info(page, &start, file->f_pos, thistime);
				break;
			case PROC_NET_AT_ROUTE:
				length = atalk_rt_get_info(page, &start, file->f_pos, thistime);
				break;
			case PROC_NET_ATIF:
				length = atalk_if_get_info(page, &start, file->f_pos, thistime);
				break;
#endif /* CONFIG_ATALK */
#ifdef CONFIG_AX25
			case PROC_NET_AX25_ROUTE:
				length = ax25_rt_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_AX25:
				length = ax25_get_info(page,&start,file->f_pos,thistime);
				break;
#ifdef CONFIG_NETROM
			case PROC_NET_NR_NODES:
				length = nr_nodes_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_NR_NEIGH:
				length = nr_neigh_get_info(page,&start,file->f_pos,thistime);
				break;
			case PROC_NET_NR:
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

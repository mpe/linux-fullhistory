/*
 * sysctl.c: General linux system control interface
 *
 * Begun 24 March 1995, Stephen Tweedie
 * Added /proc support, Dec 1995
 * Added bdflush entry and intvec min/max checking, 2/23/96, Tom Dyas.
 * Added hooks for /proc/sys/net (minor, minor patch), 96/4/1, Mike Shaver.
 * Added kernel/java-{interpreter,appletviewer}, 96/5/10, Mike Shaver.
 * Dynamic registration fixes, Stephen Tweedie.
 * Added kswapd-interval, ctrl-alt-del, printk stuff, 1/8/97, Chris Horn.
 * Made sysctl support optional via CONFIG_SYSCTL, 1/10/97, Chris Horn.
 */

#include <linux/config.h>
#include <linux/malloc.h>
#include <linux/sysctl.h>
#include <linux/swapctl.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/uaccess.h>

#ifdef CONFIG_ROOT_NFS
#include <linux/nfs_fs.h>
#endif

#if defined(CONFIG_SYSCTL)

/* External variables not in a header file. */
extern int panic_timeout;
extern int console_loglevel, C_A_D;
extern int bdf_prm[], bdflush_min[], bdflush_max[];
extern char binfmt_java_interpreter[], binfmt_java_appletviewer[];
extern int sysctl_overcommit_memory;
extern int nr_queued_signals, max_queued_signals;

#ifdef CONFIG_KMOD
extern char modprobe_path[];
#endif
#ifdef CONFIG_CHR_DEV_SG
extern int sg_big_buff;
#endif
#ifdef CONFIG_SYSVIPC
extern int shmmax;
#endif

#ifdef __sparc__
extern char reboot_command [];
#endif
#ifdef __powerpc__
extern unsigned long htab_reclaim_on, zero_paged_on, powersave_nap;
int proc_dol2crvec(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp);
#endif

#ifdef CONFIG_BSD_PROCESS_ACCT
extern int acct_parm[];
#endif

extern int pgt_cache_water[];

static int parse_table(int *, int, void *, size_t *, void *, size_t,
		       ctl_table *, void **);
static int proc_doutsstring(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp);


static ctl_table root_table[];
static struct ctl_table_header root_table_header = 
	{root_table, DNODE_SINGLE(&root_table_header)};

static ctl_table kern_table[];
static ctl_table vm_table[];
#ifdef CONFIG_NET
extern ctl_table net_table[];
#endif
static ctl_table proc_table[];
static ctl_table fs_table[];
static ctl_table debug_table[];
static ctl_table dev_table[];


/* /proc declarations: */

#ifdef CONFIG_PROC_FS

static ssize_t proc_readsys(struct file *, char *, size_t, loff_t *);
static ssize_t proc_writesys(struct file *, const char *, size_t, loff_t *);
static int proc_sys_permission(struct inode *, int);

struct file_operations proc_sys_file_operations =
{
	NULL,		/* lseek   */
	proc_readsys,	/* read	   */
	proc_writesys,	/* write   */
	NULL,		/* readdir */
	NULL,		/* poll    */
	NULL,		/* ioctl   */
	NULL,		/* mmap	   */
	NULL,		/* no special open code	   */
	NULL,		/* no special flush code */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

struct inode_operations proc_sys_inode_operations =
{
	&proc_sys_file_operations,
	NULL,		/* create */
	NULL,		/* lookup */
	NULL,		/* link */
	NULL,		/* unlink */
	NULL,		/* symlink */
	NULL,		/* mkdir */
	NULL,		/* rmdir */
	NULL,		/* mknod */
	NULL,		/* rename */
	NULL,		/* readlink */
	NULL,		/* follow_link */
	NULL,		/* readpage */
	NULL,		/* writepage */
	NULL,		/* bmap */
	NULL,		/* truncate */
	proc_sys_permission
};

extern struct proc_dir_entry proc_sys_root;

static void register_proc_table(ctl_table *, struct proc_dir_entry *);
static void unregister_proc_table(ctl_table *, struct proc_dir_entry *);
#endif
extern int inodes_stat[];
extern int dentry_stat[];

/* The default sysctl tables: */

static ctl_table root_table[] = {
	{CTL_KERN, "kernel", NULL, 0, 0555, kern_table},
	{CTL_VM, "vm", NULL, 0, 0555, vm_table},
#ifdef CONFIG_NET
	{CTL_NET, "net", NULL, 0, 0555, net_table},
#endif
	{CTL_PROC, "proc", NULL, 0, 0555, proc_table},
	{CTL_FS, "fs", NULL, 0, 0555, fs_table},
	{CTL_DEBUG, "debug", NULL, 0, 0555, debug_table},
        {CTL_DEV, "dev", NULL, 0, 0555, dev_table},
	{0}
};

static ctl_table kern_table[] = {
	{KERN_OSTYPE, "ostype", system_utsname.sysname, 64,
	 0444, NULL, &proc_doutsstring, &sysctl_string},
	{KERN_OSRELEASE, "osrelease", system_utsname.release, 64,
	 0444, NULL, &proc_doutsstring, &sysctl_string},
	{KERN_VERSION, "version", system_utsname.version, 64,
	 0444, NULL, &proc_doutsstring, &sysctl_string},
	{KERN_NODENAME, "hostname", system_utsname.nodename, 64,
	 0644, NULL, &proc_doutsstring, &sysctl_string},
	{KERN_DOMAINNAME, "domainname", system_utsname.domainname, 64,
	 0644, NULL, &proc_doutsstring, &sysctl_string},
	{KERN_PANIC, "panic", &panic_timeout, sizeof(int),
	 0644, NULL, &proc_dointvec},
#ifdef CONFIG_BLK_DEV_INITRD
	{KERN_REALROOTDEV, "real-root-dev", &real_root_dev, sizeof(int),
	 0644, NULL, &proc_dointvec},
#endif
#ifdef CONFIG_BINFMT_JAVA
	{KERN_JAVA_INTERPRETER, "java-interpreter", binfmt_java_interpreter,
	 64, 0644, NULL, &proc_dostring, &sysctl_string },
	{KERN_JAVA_APPLETVIEWER, "java-appletviewer", binfmt_java_appletviewer,
	 64, 0644, NULL, &proc_dostring, &sysctl_string },
#endif
#ifdef __sparc__
	{KERN_SPARC_REBOOT, "reboot-cmd", reboot_command,
	 256, 0644, NULL, &proc_dostring, &sysctl_string },
#endif
#ifdef __powerpc__
	{KERN_PPC_HTABRECLAIM, "htab-reclaim", &htab_reclaim_on, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{KERN_PPC_ZEROPAGED, "zero-paged", &zero_paged_on, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{KERN_PPC_POWERSAVE_NAP, "powersave-nap", &powersave_nap, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{KERN_PPC_L2CR, "l2cr", NULL, 0,
	 0644, NULL, &proc_dol2crvec},
#endif
	{KERN_CTLALTDEL, "ctrl-alt-del", &C_A_D, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{KERN_PRINTK, "printk", &console_loglevel, 4*sizeof(int),
	 0644, NULL, &proc_dointvec},
#ifdef CONFIG_KMOD
	{KERN_MODPROBE, "modprobe", &modprobe_path, 256,
	 0644, NULL, &proc_dostring, &sysctl_string },
#endif
#ifdef CONFIG_CHR_DEV_SG
	{KERN_SG_BIG_BUFF, "sg-big-buff", &sg_big_buff, sizeof (int),
	 0444, NULL, &proc_dointvec},
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
	{KERN_ACCT, "acct", &acct_parm, 3*sizeof(int),
	0644, NULL, &proc_dointvec},
#endif
	{KERN_RTSIGNR, "rtsig-nr", &nr_queued_signals, sizeof(int),
	 0444, NULL, &proc_dointvec},
	{KERN_RTSIGMAX, "rtsig-max", &max_queued_signals, sizeof(int),
	 0644, NULL, &proc_dointvec},
#ifdef CONFIG_SYSVIPC
	{KERN_SHMMAX, "shmmax", &shmmax, sizeof (int),
	 0644, NULL, &proc_dointvec},
#endif
	{0}
};

static ctl_table vm_table[] = {
	{VM_FREEPG, "freepages", 
	 &freepages, sizeof(freepages_t), 0644, NULL, &proc_dointvec},
	{VM_BDFLUSH, "bdflush", &bdf_prm, 9*sizeof(int), 0600, NULL,
	 &proc_dointvec_minmax, &sysctl_intvec, NULL,
	 &bdflush_min, &bdflush_max},
	{VM_OVERCOMMIT_MEMORY, "overcommit_memory", &sysctl_overcommit_memory,
	 sizeof(sysctl_overcommit_memory), 0644, NULL, &proc_dointvec},
	{VM_BUFFERMEM, "buffermem",
	 &buffer_mem, sizeof(buffer_mem_t), 0644, NULL, &proc_dointvec},
	{VM_PAGECACHE, "pagecache",
	 &page_cache, sizeof(buffer_mem_t), 0644, NULL, &proc_dointvec},
	{VM_PAGERDAEMON, "kswapd",
	 &pager_daemon, sizeof(pager_daemon_t), 0644, NULL, &proc_dointvec},
	{VM_PGT_CACHE, "pagetable_cache", 
	 &pgt_cache_water, 2*sizeof(int), 0600, NULL, &proc_dointvec},
	{VM_PAGE_CLUSTER, "page-cluster", 
	 &page_cluster, sizeof(int), 0600, NULL, &proc_dointvec},
	{0}
};

static ctl_table proc_table[] = {
	{0}
};

static ctl_table fs_table[] = {
	{FS_NRINODE, "inode-nr", &inodes_stat, 2*sizeof(int),
	 0444, NULL, &proc_dointvec},
	{FS_STATINODE, "inode-state", &inodes_stat, 7*sizeof(int),
	 0444, NULL, &proc_dointvec},
	{FS_MAXINODE, "inode-max", &max_inodes, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{FS_NRFILE, "file-nr", &nr_files, 3*sizeof(int),
	 0444, NULL, &proc_dointvec},
	{FS_MAXFILE, "file-max", &max_files, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{FS_NRSUPER, "super-nr", &nr_super_blocks, sizeof(int),
	 0444, NULL, &proc_dointvec},
	{FS_MAXSUPER, "super-max", &max_super_blocks, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{FS_NRDQUOT, "dquot-nr", &nr_dquots, 2*sizeof(int),
	 0444, NULL, &proc_dointvec},
	{FS_MAXDQUOT, "dquot-max", &max_dquots, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{FS_DENTRY, "dentry-state", &dentry_stat, 6*sizeof(int),
	 0444, NULL, &proc_dointvec},
	{0}
};

static ctl_table debug_table[] = {
	{0}
};

static ctl_table dev_table[] = {
	{0}
};  


void __init sysctl_init(void)
{
#ifdef CONFIG_PROC_FS
	register_proc_table(root_table, &proc_sys_root);
#endif
}


int do_sysctl (int *name, int nlen,
	       void *oldval, size_t *oldlenp,
	       void *newval, size_t newlen)
{
	int error;
	struct ctl_table_header *tmp;
	void *context;
	
	if (nlen == 0 || nlen >= CTL_MAXNAME)
		return -ENOTDIR;
	
	if (oldval) 
	{
		int old_len;
		if (!oldlenp)
			return -EFAULT;
		if(get_user(old_len, oldlenp))
			return -EFAULT;
	}
	tmp = &root_table_header;
	do {
		context = NULL;
		error = parse_table(name, nlen, oldval, oldlenp, 
				    newval, newlen, tmp->ctl_table, &context);
		if (context)
			kfree(context);
		if (error != -ENOTDIR)
			return error;
		tmp = tmp->DLIST_NEXT(ctl_entry);
	} while (tmp != &root_table_header);
	return -ENOTDIR;
}

extern asmlinkage int sys_sysctl(struct __sysctl_args *args)
{
	struct __sysctl_args tmp;
	int error;

	if(copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;
		
	lock_kernel();
	error = do_sysctl(tmp.name, tmp.nlen, tmp.oldval, tmp.oldlenp,
			  tmp.newval, tmp.newlen);
	unlock_kernel();
	return error;
}

/* Like in_group_p, but testing against egid, not fsgid */
static int in_egroup_p(gid_t grp)
{
	if (grp != current->egid) {
		int i = current->ngroups;
		if (i) {
			gid_t *groups = current->groups;
			do {
				if (*groups == grp)
					goto out;
				groups++;
				i--;
			} while (i);
		}
		return 0;
	}
out:
	return 1;
}

/* ctl_perm does NOT grant the superuser all rights automatically, because
   some sysctl variables are readonly even to root. */

static int test_perm(int mode, int op)
{
	if (!current->euid)
		mode >>= 6;
	else if (in_egroup_p(0))
		mode >>= 3;
	if ((mode & op & 0007) == op)
		return 0;
	return -EACCES;
}

static inline int ctl_perm(ctl_table *table, int op)
{
	return test_perm(table->mode, op);
}

static int parse_table(int *name, int nlen,
		       void *oldval, size_t *oldlenp,
		       void *newval, size_t newlen,
		       ctl_table *table, void **context)
{
	int error;
repeat:
	if (!nlen)
		return -ENOTDIR;

	for ( ; table->ctl_name; table++) {
		int n;
		if(get_user(n,name))
			return -EFAULT;
		if (n == table->ctl_name ||
		    table->ctl_name == CTL_ANY) {
			if (table->child) {
				if (ctl_perm(table, 001))
					return -EPERM;
				if (table->strategy) {
					error = table->strategy(
						table, name, nlen,
						oldval, oldlenp,
						newval, newlen, context);
				if (error)
					return error;
				}
				name++;
				nlen--;
				table = table->child;
				goto repeat;
			}
			error = do_sysctl_strategy(table, name, nlen,
						   oldval, oldlenp,
						   newval, newlen, context);
			return error;
		}
	};
	return -ENOTDIR;
}

/* Perform the actual read/write of a sysctl table entry. */
int do_sysctl_strategy (ctl_table *table, 
			int *name, int nlen,
			void *oldval, size_t *oldlenp,
			void *newval, size_t newlen, void **context)
{
	int op = 0, rc, len;

	if (oldval)
		op |= 004;
	if (newval) 
		op |= 002;
	if (ctl_perm(table, op))
		return -EPERM;

	if (table->strategy) {
		rc = table->strategy(table, name, nlen, oldval, oldlenp,
				     newval, newlen, context);
		if (rc < 0)
			return rc;
		if (rc > 0)
			return 0;
	}

	/* If there is no strategy routine, or if the strategy returns
	 * zero, proceed with automatic r/w */
	if (table->data && table->maxlen) {
		if (oldval && oldlenp) {
			get_user(len, oldlenp);
			if (len) {
				if (len > table->maxlen)
					len = table->maxlen;
				if(copy_to_user(oldval, table->data, len))
					return -EFAULT;
				if(put_user(len, oldlenp))
					return -EFAULT;
			}
		}
		if (newval && newlen) {
			len = newlen;
			if (len > table->maxlen)
				len = table->maxlen;
			if(copy_from_user(table->data, newval, len))
				return -EFAULT;
		}
	}
	return 0;
}

struct ctl_table_header *register_sysctl_table(ctl_table * table, 
					       int insert_at_head)
{
	struct ctl_table_header *tmp;
	tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return 0;
	*tmp = ((struct ctl_table_header) {table, DNODE_NULL});
	if (insert_at_head)
		DLIST_INSERT_AFTER(&root_table_header, tmp, ctl_entry);
	else
		DLIST_INSERT_BEFORE(&root_table_header, tmp, ctl_entry);
#ifdef CONFIG_PROC_FS
	register_proc_table(table, &proc_sys_root);
#endif
	return tmp;
}

/*
 * Unlink and free a ctl_table.
 */
void unregister_sysctl_table(struct ctl_table_header * header)
{
	DLIST_DELETE(header, ctl_entry);
#ifdef CONFIG_PROC_FS
	unregister_proc_table(header->ctl_table, &proc_sys_root);
#endif
	kfree(header);
}

/*
 * /proc/sys support
 */

#ifdef CONFIG_PROC_FS

/* Scan the sysctl entries in table and add them all into /proc */
static void register_proc_table(ctl_table * table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	int len;
	mode_t mode;
	
	for (; table->ctl_name; table++) {
		/* Can't do anything without a proc name. */
		if (!table->procname)
			continue;
		/* Maybe we can't do anything with it... */
		if (!table->proc_handler && !table->child) {
			printk(KERN_WARNING "SYSCTL: Can't register %s\n",
				table->procname);
			continue;
		}

		len = strlen(table->procname);
		mode = table->mode;

		de = NULL;
		if (table->proc_handler)
			mode |= S_IFREG;
		else {
			mode |= S_IFDIR;
			for (de = root->subdir; de; de = de->next) {
				if (proc_match(len, table->procname, de))
					break;
			}
			/* If the subdir exists already, de is non-NULL */
		}

		if (!de) {
			de = create_proc_entry(table->procname, mode, root);
			if (!de)
				continue;
			de->data = (void *) table;
			if (table->proc_handler)
				de->ops = &proc_sys_inode_operations;

		}
		table->de = de;
		if (de->mode & S_IFDIR)
			register_proc_table(table->child, de);
	}
}

/*
 * Unregister a /proc sysctl table and any subdirectories.
 */
static void unregister_proc_table(ctl_table * table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	for (; table->ctl_name; table++) {
		if (!(de = table->de))
			continue;
		if (de->mode & S_IFDIR) {
			if (!table->child) {
				printk (KERN_ALERT "Help - malformed sysctl tree on free\n");
				continue;
			}
			unregister_proc_table(table->child, de);
		}
		/* Don't unregister proc directories which still have
		   entries... */
		if (!((de->mode & S_IFDIR) && de->subdir)) {
			proc_unregister(root, de->low_ino);
			table->de = NULL;
			kfree(de);
		} 
	}
}

static ssize_t do_rw_proc(int write, struct file * file, char * buf,
			  size_t count, loff_t *ppos)
{
	int op;
	struct proc_dir_entry *de;
	struct ctl_table *table;
	size_t res;
	ssize_t error;
	
	de = (struct proc_dir_entry*) file->f_dentry->d_inode->u.generic_ip;
	if (!de || !de->data)
		return -ENOTDIR;
	table = (struct ctl_table *) de->data;
	if (!table || !table->proc_handler)
		return -ENOTDIR;
	op = (write ? 002 : 004);
	if (ctl_perm(table, op))
		return -EPERM;
	
	res = count;

	/*
	 * FIXME: we need to pass on ppos to the handler.
	 */

	error = (*table->proc_handler) (table, write, file, buf, &res);
	if (error)
		return error;
	return res;
}

static ssize_t proc_readsys(struct file * file, char * buf,
			    size_t count, loff_t *ppos)
{
	return do_rw_proc(0, file, buf, count, ppos);
}

static ssize_t proc_writesys(struct file * file, const char * buf,
			     size_t count, loff_t *ppos)
{
	return do_rw_proc(1, file, (char *) buf, count, ppos);
}

static int proc_sys_permission(struct inode *inode, int op)
{
	return test_perm(inode->i_mode, op);
}

int proc_dostring(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	int len;
	char *p, c;
	
	if (!table->data || !table->maxlen || !*lenp ||
	    (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	if (write) {
		len = 0;
		p = buffer;
		while (len < *lenp) {
			if(get_user(c, p++))
				return -EFAULT;
			if (c == 0 || c == '\n')
				break;
			len++;
		}
		if (len >= table->maxlen)
			len = table->maxlen-1;
		if(copy_from_user(table->data, buffer, len))
			return -EFAULT;
		((char *) table->data)[len] = 0;
		filp->f_pos += *lenp;
	} else {
		len = strlen(table->data);
		if (len > table->maxlen)
			len = table->maxlen;
		if (len > *lenp)
			len = *lenp;
		if (len)
			if(copy_to_user(buffer, table->data, len))
				return -EFAULT;
		if (len < *lenp) {
			if(put_user('\n', ((char *) buffer) + len))
				return -EFAULT;
			len++;
		}
		*lenp = len;
		filp->f_pos += len;
	}
	return 0;
}

/*
 *	Special case of dostring for the UTS structure. This has locks
 *	to observe. Should this be in kernel/sys.c ????
 */
 
static int proc_doutsstring(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	int r;
	down(&uts_sem);
	r=proc_dostring(table,write,filp,buffer,lenp);
	up(&uts_sem);
	return r;
}

static int do_proc_dointvec(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp, int conv)
{
	int *i, vleft, first=1, len, left, neg, val;
	#define TMPBUFLEN 20
	char buf[TMPBUFLEN], *p;
	
	if (!table->data || !table->maxlen || !*lenp ||
	    (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (int *) table->data;
	vleft = table->maxlen / sizeof(int);
	left = *lenp;
	
	for (; left && vleft--; i++, first=0) {
		if (write) {
			while (left) {
				char c;
				if(get_user(c,(char *) buffer))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				((char *) buffer)++;
			}
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if(copy_from_user(buf, buffer, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p == '-' && left > 1) {
				neg = 1;
				left--, p++;
			}
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0) * conv;
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			if (neg)
				val = -val;
			buffer += len;
			left -= len;
			*i = val;
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			sprintf(p, "%d", (*i) / conv);
			len = strlen(buf);
			if (len > left)
				len = left;
			if(copy_to_user(buffer, buf, len))
				return -EFAULT;
			left -= len;
			buffer += len;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', (char *) buffer))
			return -EFAULT;
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left) {
			char c;
			if(get_user(c, p++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
}

int proc_dointvec(ctl_table *table, int write, struct file *filp,
		     void *buffer, size_t *lenp)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,1);
}

int proc_dointvec_minmax(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	int *i, *min, *max, vleft, first=1, len, left, neg, val;
	#define TMPBUFLEN 20
	char buf[TMPBUFLEN], *p;
	
	if (!table->data || !table->maxlen || !*lenp ||
	    (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (int *) table->data;
	min = (int *) table->extra1;
	max = (int *) table->extra2;
	vleft = table->maxlen / sizeof(int);
	left = *lenp;
	
	for (; left && vleft--; i++, first=0) {
		if (write) {
			while (left) {
				char c;
				if(get_user(c, (char *) buffer))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				((char *) buffer)++;
			}
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if(copy_from_user(buf, buffer, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p == '-' && left > 1) {
				neg = 1;
				left--, p++;
			}
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0);
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			if (neg)
				val = -val;
			buffer += len;
			left -= len;

			if (min && val < *min++)
				continue;
			if (max && val > *max++)
				continue;
			*i = val;
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			sprintf(p, "%d", *i);
			len = strlen(buf);
			if (len > left)
				len = left;
			if(copy_to_user(buffer, buf, len))
				return -EFAULT;
			left -= len;
			buffer += len;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', (char *) buffer))
			return -EFAULT;
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left) {
			char c;
			if(get_user(c, p++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
}

/* Like proc_dointvec, but converts seconds to jiffies */
int proc_dointvec_jiffies(ctl_table *table, int write, struct file *filp,
			  void *buffer, size_t *lenp)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,HZ);
}

#else /* CONFIG_PROC_FS */

int proc_dostring(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

static int proc_doutsstring(ctl_table *table, int write, struct file *filp,
			    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_minmax(ctl_table *table, int write, struct file *filp,
		    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_jiffies(ctl_table *table, int write, struct file *filp,
		    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

#endif /* CONFIG_PROC_FS */


/*
 * General sysctl support routines 
 */

/* The generic string strategy routine: */
int sysctl_string(ctl_table *table, int *name, int nlen,
		  void *oldval, size_t *oldlenp,
		  void *newval, size_t newlen, void **context)
{
	int l, len;
	
	if (!table->data || !table->maxlen) 
		return -ENOTDIR;
	
	if (oldval && oldlenp) {
		if(get_user(len, oldlenp))
			return -EFAULT;
		if (len) {
			l = strlen(table->data);
			if (len > l) len = l;
			if (len >= table->maxlen)
				len = table->maxlen;
			if(copy_to_user(oldval, table->data, len))
				return -EFAULT;
			if(put_user(0, ((char *) oldval) + len))
				return -EFAULT;
			if(put_user(len, oldlenp))
				return -EFAULT;
		}
	}
	if (newval && newlen) {
		len = newlen;
		if (len > table->maxlen)
			len = table->maxlen;
		if(copy_from_user(table->data, newval, len))
			return -EFAULT;
		if (len == table->maxlen)
			len--;
		((char *) table->data)[len] = 0;
	}
	return 0;
}

/*
 * This function makes sure that all of the integers in the vector
 * are between the minimum and maximum values given in the arrays
 * table->extra1 and table->extra2, respectively.
 */
int sysctl_intvec(ctl_table *table, int *name, int nlen,
		void *oldval, size_t *oldlenp,
		void *newval, size_t newlen, void **context)
{
	int i, length, *vec, *min, *max;

	if (newval && newlen) {
		if (newlen % sizeof(int) != 0)
			return -EINVAL;

		if (!table->extra1 && !table->extra2)
			return 0;

		if (newlen > table->maxlen)
			newlen = table->maxlen;
		length = newlen / sizeof(int);

		vec = (int *) newval;
		min = (int *) table->extra1;
		max = (int *) table->extra2;

		for (i = 0; i < length; i++) {
			int value;
			get_user(value, vec + i);
			if (min && value < min[i])
				return -EINVAL;
			if (max && value > max[i])
				return -EINVAL;
		}
	}
	return 0;
}

int do_string (
	void *oldval, size_t *oldlenp, void *newval, size_t newlen,
	int rdwr, char *data, size_t max)
{
	int l = strlen(data) + 1;
	if (newval && !rdwr)
		return -EPERM;
	if (newval && newlen >= max)
		return -EINVAL;
	if (oldval) {
		int old_l;
		if(get_user(old_l, oldlenp))
			return -EFAULT;
		if (l > old_l)
			return -ENOMEM;
		if(put_user(l, oldlenp) || copy_to_user(oldval, data, l))
			return -EFAULT;
	}
	if (newval) {
		if(copy_from_user(data, newval, newlen))
			return -EFAULT;
		data[newlen] = 0;
	}
	return 0;
}

int do_int (
	void *oldval, size_t *oldlenp, void *newval, size_t newlen,
	int rdwr, int *data)
{
	if (newval && !rdwr)
		return -EPERM;
	if (newval && newlen != sizeof(int))
		return -EINVAL;
	if (oldval) {
		int old_l;
		if(get_user(old_l, oldlenp))
			return -EFAULT;
		if (old_l < sizeof(int))
			return -ENOMEM;
		if(put_user(sizeof(int), oldlenp)||copy_to_user(oldval, data, sizeof(int)))
			return -EFAULT;
	}
	if (newval)
		if(copy_from_user(data, newval, sizeof(int)))
			return -EFAULT;
	return 0;
}

int do_struct (
	void *oldval, size_t *oldlenp, void *newval, size_t newlen,
	int rdwr, void *data, size_t len)
{
	if (newval && !rdwr)
		return -EPERM;
	if (newval && newlen != len)
		return -EINVAL;
	if (oldval) {
		int old_l;
		if(get_user(old_l, oldlenp))
			return -EFAULT;
		if (old_l < len)
			return -ENOMEM;
		if(put_user(len, oldlenp) || copy_to_user(oldval, data, len))
			return -EFAULT;
	}
	if (newval)
		if(copy_from_user(data, newval, len))
			return -EFAULT;
	return 0;
}


#else /* CONFIG_SYSCTL */


extern asmlinkage int sys_sysctl(struct __sysctl_args *args)
{
	return -ENOSYS;
}

int sysctl_string(ctl_table *table, int *name, int nlen,
		  void *oldval, size_t *oldlenp,
		  void *newval, size_t newlen, void **context)
{
	return -ENOSYS;
}

int sysctl_intvec(ctl_table *table, int *name, int nlen,
		void *oldval, size_t *oldlenp,
		void *newval, size_t newlen, void **context)
{
	return -ENOSYS;
}

int proc_dostring(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_minmax(ctl_table *table, int write, struct file *filp,
		    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_jiffies(ctl_table *table, int write, struct file *filp,
		    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

struct ctl_table_header * register_sysctl_table(ctl_table * table, 
						int insert_at_head)
{
	return 0;
}

void unregister_sysctl_table(struct ctl_table_header * table)
{
}

#endif /* CONFIG_SYSCTL */

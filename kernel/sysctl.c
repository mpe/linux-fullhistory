/*
 * sysctl.c: General linux system control interface
 *
 * Begun 24 March 1995, Stephen Tweedie
 * Added /proc support, Dec 1995
 * Added bdflush entry and intvec min/max checking, 2/23/96, Tom Dyas.
 * Added hooks for /proc/sys/net (minor, minor patch), 96/4/1, Mike Shaver.
 * Added kernel/java-{interpreter,appletviewer}, 96/5/10, Mike Shaver.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/swapctl.h>
#include <linux/proc_fs.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <asm/bitops.h>
#include <asm/segment.h>

#include <linux/utsname.h>
#include <linux/swapctl.h>

/* External variables not in a header file. */
extern int panic_timeout;


#ifdef CONFIG_ROOT_NFS
#include <linux/nfs_fs.h>
#endif

static ctl_table root_table[];
static struct ctl_table_header root_table_header = 
	{root_table, DNODE_SINGLE(&root_table_header)};

static int parse_table(int *, int, void *, size_t *, void *, size_t,
		       ctl_table *, void **);

static ctl_table kern_table[];
static ctl_table vm_table[];
extern ctl_table net_table[];

/* /proc declarations: */

#ifdef CONFIG_PROC_FS

static int proc_readsys(struct inode * inode, struct file * file,
			char * buf, int count);
static int proc_writesys(struct inode * inode, struct file * file,
			 const char * buf, int count);
static int proc_sys_permission(struct inode *, int);

struct file_operations proc_sys_file_operations =
{
	NULL,		/* lseek   */
	proc_readsys,	/* read	   */
	proc_writesys,	/* write   */
	NULL,		/* readdir */
	NULL,		/* select  */
	NULL,		/* ioctl   */
	NULL,		/* mmap	   */
	NULL,		/* no special open code	   */
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

extern int bdf_prm[], bdflush_min[], bdflush_max[];

static int do_securelevel_strategy (ctl_table *, int *, int, void *, size_t *,
				    void *, size_t, void **);

extern char binfmt_java_interpreter[], binfmt_java_appletviewer[];

/* The default sysctl tables: */

static ctl_table root_table[] = {
	{CTL_KERN, "kernel", NULL, 0, 0555, kern_table},
	{CTL_VM, "vm", NULL, 0, 0555, vm_table},
	{CTL_NET, "net", NULL, 0, 0555, net_table},
	{0}
};

static ctl_table kern_table[] = {
	{KERN_OSTYPE, "ostype", system_utsname.sysname, 64,
	 0444, NULL, &proc_dostring, &sysctl_string},
	{KERN_OSRELEASE, "osrelease", system_utsname.release, 64,
	 0444, NULL, &proc_dostring, &sysctl_string},
	{KERN_VERSION, "version", system_utsname.version, 64,
	 0444, NULL, &proc_dostring, &sysctl_string},
	{KERN_NODENAME, "hostname", system_utsname.nodename, 64,
	 0644, NULL, &proc_dostring, &sysctl_string},
	{KERN_DOMAINNAME, "domainname", system_utsname.domainname, 64,
	 0644, NULL, &proc_dostring, &sysctl_string},
	{KERN_NRINODE, "inode-nr", &nr_inodes, 2*sizeof(int),
	 0444, NULL, &proc_dointvec},
	{KERN_MAXINODE, "inode-max", &max_inodes, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{KERN_NRFILE, "file-nr", &nr_files, sizeof(int),
	 0444, NULL, &proc_dointvec},
	{KERN_MAXFILE, "file-max", &max_files, sizeof(int),
	 0644, NULL, &proc_dointvec},
	{KERN_SECURELVL, "securelevel", &securelevel, sizeof(int),
	 0444, NULL, &proc_dointvec, (ctl_handler *)&do_securelevel_strategy},
	{KERN_PANIC, "panic", &panic_timeout, sizeof(int),
	 0644, NULL, &proc_dointvec},
#ifdef CONFIG_BLK_DEV_INITRD
	{KERN_REALROOTDEV, "real-root-dev", &real_root_dev, sizeof(int),
	 0644, NULL, &proc_dointvec},
#endif
#ifdef CONFIG_ROOT_NFS
	{KERN_NFSRNAME, "nfs-root-name", nfs_root_name, NFS_ROOT_NAME_LEN,
	 0644, NULL, &proc_dostring, &sysctl_string },
	{KERN_NFSRNAME, "nfs-root-addrs", nfs_root_addrs, NFS_ROOT_ADDRS_LEN,
	 0644, NULL, &proc_dostring, &sysctl_string },
#endif
#ifdef CONFIG_BINFMT_JAVA
	{KERN_JAVA_INTERPRETER, "java-interpreter", binfmt_java_interpreter,
	 64, 0644, NULL, &proc_dostring, &sysctl_string },
	{KERN_JAVA_APPLETVIEWER, "java-appletviewer", binfmt_java_appletviewer,
	 64, 0644, NULL, &proc_dostring, &sysctl_string },
#endif
	{0}
};

static ctl_table vm_table[] = {
	{VM_SWAPCTL, "swapctl", 
	 &swap_control, sizeof(swap_control_t), 0600, NULL, &proc_dointvec},
	{VM_KSWAPD, "kswapd", 
	 &kswapd_ctl, sizeof(kswapd_ctl), 0600, NULL, &proc_dointvec},
	{VM_FREEPG, "freepages", 
	 &min_free_pages, 3*sizeof(int), 0600, NULL, &proc_dointvec},
	{VM_BDFLUSH, "bdflush", &bdf_prm, 9*sizeof(int), 0600, NULL,
	 &proc_dointvec_minmax, &sysctl_intvec, NULL,
	 &bdflush_min, &bdflush_max},
	{0}
};

void sysctl_init(void)
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
	
	error = verify_area(VERIFY_READ,name,nlen*sizeof(int));
	if (error) return error;
	if (oldval) {
		if (!oldlenp)
			return -EFAULT;
		error = verify_area(VERIFY_WRITE,oldlenp,sizeof(size_t));
		if (error) return error;
		error = verify_area(VERIFY_WRITE,oldval,get_user(oldlenp));
		if (error) return error;
	}
	if (newval) {
		error = verify_area(VERIFY_READ,newval,newlen);
		if (error) return error;
	}
	tmp = &root_table_header;
	do {
		context = 0;
		error = parse_table(name, nlen, oldval, oldlenp, 
				    newval, newlen, root_table, &context);
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
	error = verify_area(VERIFY_READ, args, sizeof(*args));
	if (error)
		return error;
	memcpy_fromfs(&tmp, args, sizeof(tmp));
	return do_sysctl(tmp.name, tmp.nlen, tmp.oldval, tmp.oldlenp, 
			 tmp.newval, tmp.newlen);
}

/* Like in_group_p, but testing against egid, not fsgid */
static int in_egroup_p(gid_t grp)
{
	int	i;

	if (grp == current->euid)
		return 1;

	for (i = 0; i < NGROUPS; i++) {
		if (current->groups[i] == NOGROUP)
			break;
		if (current->groups[i] == grp)
			return 1;
	}
	return 0;
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
		if (get_user(name) == table->ctl_name ||
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
		if (oldval && oldlenp && get_user(oldlenp)) {
			len = get_user(oldlenp);
			if (len > table->maxlen)
				len = table->maxlen;
			memcpy_tofs(oldval, table->data, len);
			put_user(len, oldlenp);
		}
		if (newval && newlen) {
			len = newlen;
			if (len > table->maxlen)
				len = table->maxlen;
			memcpy_fromfs(table->data, newval, len);
		}
	}
	return 0;
}

/*
 * This function only checks permission for changing the security level
 * If the tests are successful, the actual change is done by
 * do_sysctl_strategy
 */
static int do_securelevel_strategy (ctl_table *table, 
				    int *name, int nlen,
				    void *oldval, size_t *oldlenp,
				    void *newval, size_t newlen, void **context)
{
	int level;

	if (newval && newlen) {
		if (newlen != sizeof (int))
			return -EINVAL;
		memcpy_fromfs (&level, newval, newlen);
		if (level < securelevel && current->pid != 1)
			return -EPERM;
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

void unregister_sysctl_table(struct ctl_table_header * table)
{
	DLIST_DELETE(table, ctl_entry);
#ifdef CONFIG_PROC_FS
	unregister_proc_table(table->ctl_table, &proc_sys_root);
#endif
}

/*
 * /proc/sys support
 */

#ifdef CONFIG_PROC_FS

/* Scan the sysctl entries in table and add them all into /proc */
static void register_proc_table(ctl_table * table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	
	for (; table->ctl_name; table++) {
		/* Can't do anything without a proc name. */
		if (!table->procname)
			continue;
		/* Maybe we can't do anything with it... */
		if (!table->proc_handler &&
		    !table->child)
			continue;
		
		de = kmalloc(sizeof(*de), GFP_KERNEL);
		if (!de) continue;
		de->namelen = strlen(table->procname);
		de->name = table->procname;
		de->mode = table->mode;
		de->nlink = 1;
		de->uid = 0;
		de->gid = 0;
		de->size = 0;
		de->get_info = 0;	/* For internal use if we want it */
		de->fill_inode = 0;	/* To override struct inode fields */
		de->next = de->subdir = 0;
		de->data = (void *) table;
		/* Is it a file? */
		if (table->proc_handler) {
			de->ops = &proc_sys_inode_operations;
			de->mode |= S_IFREG;
		}
		/* Otherwise it's a subdir */
		else  {
			de->ops = &proc_dir_inode_operations;
			de->nlink++;
			de->mode |= S_IFDIR;
		}
		table->de = de;
		proc_register_dynamic(root, de);
		if (de->mode & S_IFDIR )
			register_proc_table(table->child, de);
	}
}

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
		proc_unregister(root, de->low_ino);
		kfree(de);			
	}
}


static int do_rw_proc(int write, struct inode * inode, struct file * file,
		      char * buf, int count)
{
	int error, op;
	struct proc_dir_entry *de;
	struct ctl_table *table;
	size_t res;
	
	error = verify_area(write ? VERIFY_READ : VERIFY_WRITE, buf, count);
	if (error)
		return error;

	de = (struct proc_dir_entry*) inode->u.generic_ip;
	if (!de || !de->data)
		return -ENOTDIR;
	table = (struct ctl_table *) de->data;
	if (!table || !table->proc_handler)
		return -ENOTDIR;
	op = (write ? 002 : 004);
	if (ctl_perm(table, op))
		return -EPERM;
	
	res = count;
	error = (*table->proc_handler) (table, write, file, buf, &res);
	if (error)
		return error;
	return res;
}

static int proc_readsys(struct inode * inode, struct file * file,
			char * buf, int count)
{
	return do_rw_proc(0, inode, file, buf, count);
}

static int proc_writesys(struct inode * inode, struct file * file,
			 const char * buf, int count)
{
	return do_rw_proc(1, inode, file, (char *) buf, count);
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
		while (len < *lenp && 
		       (c = get_user(p++)) != 0 && c != '\n')
			len++;
		if (len >= table->maxlen)
			len = table->maxlen-1;
		memcpy_fromfs(table->data, buffer, len);
		((char *) table->data)[len] = 0;
		filp->f_pos += *lenp;
	} else {
		len = strlen(table->data);
		if (len > table->maxlen)
			len = table->maxlen;
		if (len > *lenp)
			len = *lenp;
		if (len)
			memcpy_tofs(buffer, table->data, len);
		if (len < *lenp) {
			put_user('\n', ((char *) buffer) + len);
			len++;
		}
		*lenp = len;
		filp->f_pos += len;
	}
	return 0;
}

int proc_dointvec(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
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
			while (left && isspace(get_user((char *) buffer)))
				left--, ((char *) buffer)++;
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			memcpy_fromfs(buf, buffer, len);
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
			*i = val;
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			sprintf(p, "%d", *i);
			len = strlen(buf);
			if (len > left)
				len = left;
			memcpy_tofs(buffer, buf, len);
			left -= len;
			buffer += len;
		}
	}

	if (!write && !first && left) {
		put_user('\n', (char *) buffer);
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left && isspace(get_user(p++)))
			left--;
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
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
			while (left && isspace(get_user((char *) buffer)))
				left--, ((char *) buffer)++;
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			memcpy_fromfs(buf, buffer, len);
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
			memcpy_tofs(buffer, buf, len);
			left -= len;
			buffer += len;
		}
	}

	if (!write && !first && left) {
		put_user('\n', (char *) buffer);
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left && isspace(get_user(p++)))
			left--;
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
}

#else /* CONFIG_PROC_FS */

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
	
	if (oldval && oldlenp && get_user(oldlenp)) {
		len = get_user(oldlenp);
		l = strlen(table->data);
		if (len > l) len = l;
		if (len >= table->maxlen)
			len = table->maxlen;
		memcpy_tofs(oldval, table->data, len);
		put_user(0, ((char *) oldval) + len);
		put_user(len, oldlenp);
	}
	if (newval && newlen) {
		len = newlen;
		if (len > table->maxlen)
			len = table->maxlen;
		memcpy_fromfs(table->data, newval, len);
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
			int value = get_user(vec + i);
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
		if (l > get_user(oldlenp))
			return -ENOMEM;
		put_user(l, oldlenp);
		memcpy_tofs(oldval, data, l);
	}
	if (newval) {
		memcpy_fromfs(data, newval, newlen);
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
		if (get_user(oldlenp) < sizeof(int))
			return -ENOMEM;
		put_user(sizeof(int), oldlenp);
		memcpy_tofs(oldval, data, sizeof(int));
	}
	if (newval)
		memcpy_fromfs(data, newval, sizeof(int));
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
		if (get_user(oldlenp) < len)
			return -ENOMEM;
		put_user(len, oldlenp);
		memcpy_tofs(oldval, data, len);
	}
	if (newval)
		memcpy_fromfs(data, newval, len);
	return 0;
}


/*
 *  linux/fs/proc/root.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc root directory handling functions
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <asm/bitops.h>
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

/*
 * Offset of the first process in the /proc root directory..
 */
#define FIRST_PROCESS_ENTRY 256

static int proc_root_readdir(struct inode *, struct file *, void *, filldir_t);
static int proc_root_lookup(struct inode *,const char *,int,struct inode **);

static unsigned char proc_alloc_map[PROC_NDYNAMIC / 8] = {0};

/*
 * These are the generic /proc directory operations. They
 * use the in-memory "struct proc_dir_entry" tree to parse
 * the /proc directory.
 *
 * NOTE! The /proc/scsi directory currently does not correctly
 * build up the proc_dir_entry tree, and will show up empty.
 */
static struct file_operations proc_dir_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	proc_readdir,		/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_dir_inode_operations = {
	&proc_dir_operations,	/* default net directory file-ops */
	NULL,			/* create */
	proc_lookup,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/*
 * The root /proc directory is special, as it has the
 * <pid> directories. Thus we don't use the generic
 * directory handling functions for that..
 */
static struct file_operations proc_root_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	proc_root_readdir,	/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* no fsync */
};

/*
 * proc root can do almost nothing..
 */
static struct inode_operations proc_root_inode_operations = {
	&proc_root_operations,	/* default base directory file-ops */
	NULL,			/* create */
	proc_root_lookup,	/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/*
 * This is the root "inode" in the /proc tree..
 */
struct proc_dir_entry proc_root = {
	PROC_ROOT_INO, 5, "/proc",
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
	0, &proc_root_inode_operations,
	NULL, NULL,
	NULL,
	&proc_root, NULL
};

struct proc_dir_entry *proc_net, *proc_scsi;

#ifdef CONFIG_MCA
struct proc_dir_entry proc_mca = {
	PROC_MCA, 3, "mca",
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
	0, &proc_dir_inode_operations,
	NULL, NULL,
	NULL, &proc_root, NULL
};
#endif

#ifdef CONFIG_SYSCTL
struct proc_dir_entry proc_sys_root = {
	PROC_SYS, 3, "sys",			/* inode, name */
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,	/* mode, nlink, uid, gid */
	0, &proc_dir_inode_operations,		/* size, ops */
	NULL, NULL,				/* get_info, fill_inode */
	NULL,					/* next */
	NULL, NULL				/* parent, subdir */
};
#endif

#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)

static int (*proc_openprom_defreaddir_ptr)(struct inode *, struct file *, void *, filldir_t);
static int (*proc_openprom_deflookup_ptr)(struct inode *, const char *, int, struct inode **);
void (*proc_openprom_use)(struct inode *, int) = 0;
static struct openpromfs_dev *proc_openprom_devices = NULL;
static ino_t proc_openpromdev_ino = PROC_OPENPROMD_FIRST;

struct inode_operations *
proc_openprom_register(int (*readdir)(struct inode *, struct file *, void *, filldir_t),
		       int (*lookup)(struct inode *, const char *, int, struct inode **),
		       void (*use)(struct inode *, int),
		       struct openpromfs_dev ***devices)
{
	proc_openprom_defreaddir_ptr = (proc_openprom_inode_operations.default_file_ops)->readdir;
	proc_openprom_deflookup_ptr = proc_openprom_inode_operations.lookup;
	(proc_openprom_inode_operations.default_file_ops)->readdir = readdir;
	proc_openprom_inode_operations.lookup = lookup;
	proc_openprom_use = use;
	*devices = &proc_openprom_devices;
	return &proc_openprom_inode_operations;
}

int proc_openprom_regdev(struct openpromfs_dev *d)
{
	if (proc_openpromdev_ino == PROC_OPENPROMD_FIRST + PROC_NOPENPROMD) return -1;
	d->next = proc_openprom_devices;
	d->inode = proc_openpromdev_ino++;
	proc_openprom_devices = d;
	return 0;
}

int proc_openprom_unregdev(struct openpromfs_dev *d)
{
	if (d == proc_openprom_devices) {
		proc_openprom_devices = d->next;
	} else if (!proc_openprom_devices)
		return -1;
	else {
		struct openpromfs_dev *p;
		
		for (p = proc_openprom_devices; p->next != d && p->next; p = p->next);
		if (!p->next) return -1;
		p->next = d->next;
	}
	return 0;
}

#ifdef CONFIG_SUN_OPENPROMFS_MODULE
void
proc_openprom_deregister(void)
{
	(proc_openprom_inode_operations.default_file_ops)->readdir = proc_openprom_defreaddir_ptr;
	proc_openprom_inode_operations.lookup = proc_openprom_deflookup_ptr;
	proc_openprom_use = 0;
}		      
#endif

#if defined(CONFIG_SUN_OPENPROMFS_MODULE) && defined(CONFIG_KERNELD)
static int 
proc_openprom_defreaddir(struct inode * inode, struct file * filp,
			 void * dirent, filldir_t filldir)
{
	request_module("openpromfs");
	if ((proc_openprom_inode_operations.default_file_ops)->readdir !=
	    proc_openprom_defreaddir)
		return (proc_openprom_inode_operations.default_file_ops)->readdir 
				(inode, filp, dirent, filldir);
	return -EINVAL;
}

static int 
proc_openprom_deflookup(struct inode * dir,const char * name, int len,
			struct inode ** result)
{
	request_module("openpromfs");
	if (proc_openprom_inode_operations.lookup !=
	    proc_openprom_deflookup)
		return proc_openprom_inode_operations.lookup 
				(dir, name, len, result);
	iput(dir);
	return -ENOENT;
}
#endif

static struct file_operations proc_openprom_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
#if defined(CONFIG_SUN_OPENPROMFS_MODULE) && defined(CONFIG_KERNELD)
	proc_openprom_defreaddir,/* readdir */
#else
	NULL,			/* readdir */
#endif	
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

struct inode_operations proc_openprom_inode_operations = {
	&proc_openprom_operations,/* default net directory file-ops */
	NULL,			/* create */
#if defined(CONFIG_SUN_OPENPROMFS_MODULE) && defined(CONFIG_KERNELD)
	proc_openprom_deflookup,/* lookup */
#else
	NULL,			/* lookup */
#endif	
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

struct proc_dir_entry proc_openprom = {
	PROC_OPENPROM, 8, "openprom",
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
	0, &proc_openprom_inode_operations,
	NULL, NULL,
	NULL,
	&proc_root, NULL
};

extern void openpromfs_init (void);
#endif /* CONFIG_SUN_OPENPROMFS */

static int make_inode_number(void)
{
	int i = find_first_zero_bit((void *) proc_alloc_map, PROC_NDYNAMIC);
	if (i<0 || i>=PROC_NDYNAMIC) 
		return -1;
	set_bit(i, (void *) proc_alloc_map);
	return PROC_DYNAMIC_FIRST + i;
}

int proc_register(struct proc_dir_entry * dir, struct proc_dir_entry * dp)
{
	int	i;
	
	if (dp->low_ino == 0) {
		i = make_inode_number();
		if (i < 0)
			return -EAGAIN;
		dp->low_ino = i;
	}
	dp->next = dir->subdir;
	dp->parent = dir;
	dir->subdir = dp;
	if (S_ISDIR(dp->mode)) {
		if (dp->ops == NULL)
			dp->ops = &proc_dir_inode_operations;
		dir->nlink++;
	} else {
		if (dp->ops == NULL)
			dp->ops = &proc_file_inode_operations;
	}
	/*
	 * kludge until we fixup the md device driver
	 */
	if (dp->low_ino == PROC_MD)
		dp->ops = &proc_array_inode_operations;
	return 0;
}

int proc_unregister(struct proc_dir_entry * dir, int ino)
{
	struct proc_dir_entry **p = &dir->subdir, *dp;

	while ((dp = *p) != NULL) {
		if (dp->low_ino == ino) {
			*p = dp->next;
			dp->next = NULL;
			if (S_ISDIR(dp->mode))
				dir->nlink--;
			if (ino >= PROC_DYNAMIC_FIRST &&
			    ino < PROC_DYNAMIC_FIRST+PROC_NDYNAMIC)
				clear_bit(ino-PROC_DYNAMIC_FIRST, 
					  (void *) proc_alloc_map);
			return 0;
		}
		p = &dp->next;
	}
	return -EINVAL;
}	

/*
 * /proc/self:
 */
static int proc_self_readlink(struct inode * inode, char * buffer, int buflen)
{
	int len;
	char tmp[30];

	iput(inode);
	len = sprintf(tmp, "%d", current->pid);
	if (buflen < len)
		len = buflen;
	copy_to_user(buffer, tmp, len);
	return len;
}

static struct inode_operations proc_self_inode_operations = {
	NULL,			/* no file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	proc_self_readlink,	/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct proc_dir_entry proc_root_loadavg = {
	PROC_LOADAVG, 7, "loadavg",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_uptime = {
	PROC_UPTIME, 6, "uptime",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_meminfo = {
	PROC_MEMINFO, 7, "meminfo",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_kmsg = {
	PROC_KMSG, 4, "kmsg",
	S_IFREG | S_IRUSR, 1, 0, 0,
	0, &proc_kmsg_inode_operations
};
static struct proc_dir_entry proc_root_version = {
	PROC_VERSION, 7, "version",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#ifdef CONFIG_PCI
static struct proc_dir_entry proc_root_pci = {
	PROC_PCI, 3, "pci",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif
#ifdef CONFIG_ZORRO
static struct proc_dir_entry proc_root_zorro = {
	PROC_ZORRO, 5, "zorro",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif
static struct proc_dir_entry proc_root_cpuinfo = {
	PROC_CPUINFO, 7, "cpuinfo",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#if defined (CONFIG_AMIGA) || defined (CONFIG_ATARI)
static struct proc_dir_entry proc_root_hardware = {
	PROC_HARDWARE, 8, "hardware",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif
static struct proc_dir_entry proc_root_self = {
	PROC_SELF, 4, "self",
	S_IFLNK | S_IRUGO | S_IWUGO | S_IXUGO, 1, 0, 0,
	64, &proc_self_inode_operations,
};
#ifdef CONFIG_DEBUG_MALLOC
static struct proc_dir_entry proc_root_malloc = {
	PROC_MALLOC, 6, "malloc",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif
static struct proc_dir_entry proc_root_kcore = {
	PROC_KCORE, 5, "kcore",
	S_IFREG | S_IRUSR, 1, 0, 0,
	0, &proc_kcore_inode_operations
};
#ifdef CONFIG_MODULES
static struct proc_dir_entry proc_root_modules = {
	PROC_MODULES, 7, "modules",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_ksyms = {
	PROC_KSYMS, 5, "ksyms",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif
static struct proc_dir_entry proc_root_stat = {
	PROC_STAT, 4, "stat",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_devices = {
	PROC_DEVICES, 7, "devices",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_interrupts = {
	PROC_INTERRUPTS, 10,"interrupts",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#ifdef __SMP_PROF__
static struct proc_dir_entry proc_root_smp = {
	PROC_SMP_PROF, 3,"smp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif
static struct proc_dir_entry proc_root_filesystems = {
	PROC_FILESYSTEMS, 11,"filesystems",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_dma = {
	PROC_DMA, 3, "dma",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_ioports = {
	PROC_IOPORTS, 7, "ioports",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_cmdline = {
	PROC_CMDLINE, 7, "cmdline",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#ifdef CONFIG_RTC
static struct proc_dir_entry proc_root_rtc = {
	PROC_RTC, 3, "rtc",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif
static struct proc_dir_entry proc_root_locks = {
	PROC_LOCKS, 5, "locks",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_mounts = {
	PROC_MTAB, 6, "mounts",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_swaps = {
	PROC_SWAP, 5, "swaps",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_profile = {
	PROC_PROFILE, 7, "profile",
	S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0,
	0, &proc_profile_inode_operations
};
static struct proc_dir_entry proc_root_slab = {
	PROC_SLABINFO, 8, "slabinfo",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#ifdef CONFIG_OMIRR
static struct proc_dir_entry proc_root_omirr = {
	PROC_OMIRR, 5, "omirr",
	S_IFREG | S_IRUSR, 1, 0, 0,
	0, &proc_omirr_inode_operations
};
#endif

void proc_root_init(void)
{
	proc_base_init();
	proc_register(&proc_root, &proc_root_loadavg);
	proc_register(&proc_root, &proc_root_uptime);
	proc_register(&proc_root, &proc_root_meminfo);
	proc_register(&proc_root, &proc_root_kmsg);
	proc_register(&proc_root, &proc_root_version);
#ifdef CONFIG_PCI
	proc_register(&proc_root, &proc_root_pci);
#endif
#ifdef CONFIG_ZORRO
	proc_register(&proc_root, &proc_root_zorro);
#endif
	proc_register(&proc_root, &proc_root_cpuinfo);
	proc_register(&proc_root, &proc_root_self);
	proc_net = create_proc_entry("net", S_IFDIR, 0);
	proc_scsi = create_proc_entry("scsi", S_IFDIR, 0);
#ifdef CONFIG_SYSCTL
	proc_register(&proc_root, &proc_sys_root);
#endif
#ifdef CONFIG_MCA
	proc_register(&proc_root, &proc_mca);
#endif

#ifdef CONFIG_DEBUG_MALLOC
	proc_register(&proc_root, &proc_root_malloc);
#endif
	proc_register(&proc_root, &proc_root_kcore);
	proc_root_kcore.size = (MAP_NR(high_memory) << PAGE_SHIFT) + PAGE_SIZE;

#ifdef CONFIG_MODULES
	proc_register(&proc_root, &proc_root_modules);
	proc_register(&proc_root, &proc_root_ksyms);
#endif
	proc_register(&proc_root, &proc_root_stat);
	proc_register(&proc_root, &proc_root_devices);
	proc_register(&proc_root, &proc_root_interrupts);
#ifdef __SMP_PROF__
	proc_register(&proc_root, &proc_root_smp);
#endif 
	proc_register(&proc_root, &proc_root_filesystems);
	proc_register(&proc_root, &proc_root_dma);
	proc_register(&proc_root, &proc_root_ioports);
	proc_register(&proc_root, &proc_root_cmdline);
#ifdef CONFIG_RTC
	proc_register(&proc_root, &proc_root_rtc);
#endif
	proc_register(&proc_root, &proc_root_locks);

	proc_register(&proc_root, &proc_root_mounts);
	proc_register(&proc_root, &proc_root_swaps);

#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)
#ifdef CONFIG_SUN_OPENPROMFS
	openpromfs_init ();
#endif
	proc_register(&proc_root, &proc_openprom);
#endif
#if defined (CONFIG_AMIGA) || defined (CONFIG_ATARI)
	proc_register(&proc_root, &proc_root_hardware);
#endif
	proc_register(&proc_root, &proc_root_slab);

	if (prof_shift) {
		proc_register(&proc_root, &proc_root_profile);
		proc_root_profile.size = (1+prof_len) * sizeof(unsigned long);
	}

	proc_tty_init();
}


int proc_match(int len,const char * name,struct proc_dir_entry * de)
{
	if (!de || !de->low_ino)
		return 0;
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && (de->name[0]=='.') && (de->name[1]=='\0'))
		return 1;
	if (de->namelen != len)
		return 0;
	return !memcmp(name, de->name, len);
}

int proc_lookup(struct inode * dir,const char * name, int len,
	struct inode ** result)
{
	struct proc_dir_entry * de;
	int ino;

	*result = NULL;
	if (!dir || !S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOTDIR;
	}

	de = (struct proc_dir_entry *) dir->u.generic_ip;
	if (!de) {
		iput(dir);
		return -EINVAL;
	}

	/* Either remove this as soon as possible due to security problems,
	 * or uncomment the root-only usage.
	 */

	/* Allow generic inode lookups everywhere.
	 * No other name in /proc must begin with a '['.
	 */
	if(/*!current->uid &&*/ name[0] == '[')
		return proc_arbitrary_lookup(dir,name,len,result);

	/* Special case "." and "..": they aren't on the directory list */
	*result = dir;
	if (!len)
		return 0;
	if (name[0] == '.') {
		if (len == 1)
			return 0;
		if (name[1] == '.' && len == 2) {
			struct inode * inode;
			inode = proc_get_inode(dir->i_sb, de->parent->low_ino, de->parent);
			iput(dir);
			if (!inode)
				return -EINVAL;
			*result = inode;
			return 0;
		}
	}

	*result = NULL;
	for (de = de->subdir; de ; de = de->next) {
		if (proc_match(len, name, de))
			break;
	}
	if (!de) {
		iput(dir);
		return -ENOENT;
	}

	ino = de->low_ino | (dir->i_ino & ~(0xffff));

	if (!(*result = proc_get_inode(dir->i_sb, ino, de))) {
		iput(dir);
		return -EINVAL;
	}
	iput(dir);
	return 0;
}

static int proc_root_lookup(struct inode * dir,const char * name, int len,
	struct inode ** result)
{
	unsigned int pid, c;
	int ino, retval;
	struct task_struct *p;

	atomic_inc(&dir->i_count);

	if (dir->i_ino == PROC_ROOT_INO) { /* check for safety... */
		dir->i_nlink = proc_root.nlink;

		read_lock(&tasklist_lock);
		for_each_task(p) {
			if (p->pid)
				dir->i_nlink++;
		}
		read_unlock(&tasklist_lock);
	}

	retval = proc_lookup(dir, name, len, result);
	if (retval != -ENOENT) {
		iput(dir);
		return retval;
	}
	
	pid = 0;
	while (len-- > 0) {
		c = *name - '0';
		name++;
		if (c > 9) {
			pid = 0;
			break;
		}
		pid *= 10;
		pid += c;
		if (pid & 0xffff0000) {
			pid = 0;
			break;
		}
	}
	p = find_task_by_pid(pid);
	if (!pid || !p) {
		iput(dir);
		return -ENOENT;
	}
	ino = (pid << 16) + PROC_PID_INO;
	if (!(*result = proc_get_inode(dir->i_sb, ino, &proc_pid))) {
		iput(dir);
		return -EINVAL;
	}
	iput(dir);
	return 0;
}

/*
 * This returns non-zero if at EOF, so that the /proc
 * root directory can use this and check if it should
 * continue with the <pid> entries..
 *
 * Note that the VFS-layer doesn't care about the return
 * value of the readdir() call, as long as it's non-negative
 * for success..
 */
int proc_readdir(struct inode * inode, struct file * filp,
	void * dirent, filldir_t filldir)
{
	struct proc_dir_entry * de;
	unsigned int ino;
	int i;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;
	ino = inode->i_ino;
	de = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!de)
		return -EINVAL;
	i = filp->f_pos;
	switch (i) {
		case 0:
			if (filldir(dirent, ".", 1, i, ino) < 0)
				return 0;
			i++;
			filp->f_pos++;
			/* fall through */
		case 1:
			if (filldir(dirent, "..", 2, i, de->parent->low_ino) < 0)
				return 0;
			i++;
			filp->f_pos++;
			/* fall through */
		default:
			ino &= ~0xffff;
			de = de->subdir;
			i -= 2;
			for (;;) {
				if (!de)
					return 1;
				if (!i)
					break;
				de = de->next;
				i--;
			}

			do {
				if (filldir(dirent, de->name, de->namelen, filp->f_pos, ino | de->low_ino) < 0)
					return 0;
				filp->f_pos++;
				de = de->next;
			} while (de);
	}
	return 1;
}

#define NUMBUF 10

static int proc_root_readdir(struct inode * inode, struct file * filp,
	void * dirent, filldir_t filldir)
{
	struct task_struct *p;
	char buf[NUMBUF];
	unsigned int nr = filp->f_pos;

	if (nr < FIRST_PROCESS_ENTRY) {
		int error = proc_readdir(inode, filp, dirent, filldir);
		if (error <= 0)
			return error;
		filp->f_pos = FIRST_PROCESS_ENTRY;
	}
	nr = FIRST_PROCESS_ENTRY;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		unsigned int pid;

		if(nr++ < filp->f_pos)
			continue;

		if((pid = p->pid) != 0) {
			unsigned long j = NUMBUF, i = pid;

			do {
				j--;
				buf[j] = '0' + (i % 10);
				i /= 10;
			} while (i);

			if (filldir(dirent, buf+j, NUMBUF-j,
				    filp->f_pos, (pid << 16) + PROC_PID_INO) < 0)
				break;
		}
		filp->f_pos++;
	}
	read_unlock(&tasklist_lock);
	return 0;
}

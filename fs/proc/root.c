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
#include <linux/init.h>
#include <asm/bitops.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#ifdef CONFIG_ZORRO
#include <linux/zorro.h>
#endif

/*
 * Offset of the first process in the /proc root directory..
 */
#define FIRST_PROCESS_ENTRY 256

static int proc_root_readdir(struct file *, void *, filldir_t);
static int proc_root_lookup(struct inode *,struct dentry *);
static int proc_unlink(struct inode *, struct dentry *);

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
	NULL,			/* flush */
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
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/*
 * /proc dynamic directories now support unlinking
 */
struct inode_operations proc_dyna_dir_inode_operations = {
	&proc_dir_operations,	/* default proc dir ops */
	NULL,			/* create */
	proc_lookup,		/* lookup */
	NULL,			/* link	*/
	proc_unlink,		/* unlink(struct inode *, struct dentry *) */
	NULL,			/* symlink	*/
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
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
	NULL,			/* flush */
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
	NULL,			/* follow_link */
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

struct proc_dir_entry *proc_net, *proc_scsi, *proc_bus;

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

static int (*proc_openprom_defreaddir_ptr)(struct file *, void *, filldir_t);
static int (*proc_openprom_deflookup_ptr)(struct inode *, struct dentry *);
void (*proc_openprom_use)(struct inode *, int) = 0;
static struct openpromfs_dev *proc_openprom_devices = NULL;
static ino_t proc_openpromdev_ino = PROC_OPENPROMD_FIRST;

struct inode_operations *
proc_openprom_register(int (*readdir)(struct file *, void *, filldir_t),
		       int (*lookup)(struct inode *, struct dentry *),
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
	if (proc_openpromdev_ino == PROC_OPENPROMD_FIRST + PROC_NOPENPROMD)
		return -1;
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

#if defined(CONFIG_SUN_OPENPROMFS_MODULE) && defined(CONFIG_KMOD)
static int 
proc_openprom_defreaddir(struct file * filp, void * dirent, filldir_t filldir)
{
	request_module("openpromfs");
	if ((proc_openprom_inode_operations.default_file_ops)->readdir !=
	    proc_openprom_defreaddir)
		return (proc_openprom_inode_operations.default_file_ops)->readdir 
				(filp, dirent, filldir);
	return -EINVAL;
}
#define OPENPROM_DEFREADDIR proc_openprom_defreaddir

static int 
proc_openprom_deflookup(struct inode * dir, struct dentry *dentry)
{
	request_module("openpromfs");
	if (proc_openprom_inode_operations.lookup !=
	    proc_openprom_deflookup)
		return proc_openprom_inode_operations.lookup 
				(dir, dentry);
	return -ENOENT;
}
#define OPENPROM_DEFLOOKUP proc_openprom_deflookup
#else
#define OPENPROM_DEFREADDIR NULL
#define OPENPROM_DEFLOOKUP NULL
#endif

static struct file_operations proc_openprom_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	OPENPROM_DEFREADDIR,	/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

struct inode_operations proc_openprom_inode_operations = {
	&proc_openprom_operations,/* default net directory file-ops */
	NULL,			/* create */
	OPENPROM_DEFLOOKUP,	/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
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
	} else if (S_ISLNK(dp->mode)) {
		if (dp->ops == NULL)
			dp->ops = &proc_link_inode_operations;
	} else {
		if (dp->ops == NULL)
			dp->ops = &proc_file_inode_operations;
	}
	return 0;
}

/*
 * Kill an inode that got unregistered..
 */
static void proc_kill_inodes(int ino)
{
	struct file *filp;

	/* inuse_filps is protected by the single kernel lock */
	for (filp = inuse_filps; filp; filp = filp->f_next) {
		struct dentry * dentry;
		struct inode * inode;

		dentry = filp->f_dentry;
		if (!dentry)
			continue;
		if (dentry->d_op != &proc_dentry_operations)
			continue;
		inode = dentry->d_inode;
		if (!inode)
			continue;
		if (inode->i_ino != ino)
			continue;
		filp->f_op = NULL;
	}
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
			proc_kill_inodes(ino);
			return 0;
		}
		p = &dp->next;
	}
	return -EINVAL;
}	

/*
 * /proc/self:
 */
static int proc_self_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	int len;
	char tmp[30];

	len = sprintf(tmp, "%d", current->pid);
	if (buflen < len)
		len = buflen;
	copy_to_user(buffer, tmp, len);
	return len;
}

static struct dentry * proc_self_follow_link(struct dentry *dentry,
						struct dentry *base,
						unsigned int follow)
{
	char tmp[30];

	sprintf(tmp, "%d", current->pid);
	return lookup_dentry(tmp, base, follow);
}	

int proc_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct proc_dir_entry * de;
	char 	*page;
	int len = 0;

	de = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!de)
		return -ENOENT;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	if (de->readlink_proc)
		len = de->readlink_proc(de, page);

	if (len > buflen)
		len = buflen;

	copy_to_user(buffer, page, len);
	free_page((unsigned long) page);
	return len;
}

struct dentry * proc_follow_link(struct dentry * dentry, struct dentry *base, unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct proc_dir_entry * de;
	char 	*page;
	struct dentry *d;
	int len = 0;

	de = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return NULL;

	if (de->readlink_proc)
		len = de->readlink_proc(de, page);

	d = lookup_dentry(page, base, follow);
	free_page((unsigned long) page);
	return d;
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
	proc_self_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct inode_operations proc_link_inode_operations = {
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
	proc_readlink,		/* readlink */
	proc_follow_link,	/* follow_link */
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
static struct proc_dir_entry proc_root_cpuinfo = {
	PROC_CPUINFO, 7, "cpuinfo",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#if defined (CONFIG_PROC_HARDWARE)
static struct proc_dir_entry proc_root_hardware = {
	PROC_HARDWARE, 8, "hardware",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif
#ifdef CONFIG_STRAM_PROC
static struct proc_dir_entry proc_root_stram = {
	PROC_STRAM, 5, "stram",
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
static struct proc_dir_entry proc_root_partitions = {
	PROC_PARTITIONS, 10, "partitions",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_interrupts = {
	PROC_INTERRUPTS, 10,"interrupts",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
static struct proc_dir_entry proc_root_filesystems = {
	PROC_FILESYSTEMS, 11,"filesystems",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
struct proc_dir_entry proc_root_fs = {
        PROC_FS, 2, "fs",
        S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
        0, &proc_dir_inode_operations,
	NULL, NULL,
	NULL,
	NULL, NULL
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
#ifdef __powerpc__
static struct proc_dir_entry proc_root_ppc_htab = {
	PROC_PPC_HTAB, 8, "ppc_htab",
	S_IFREG | S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, 1, 0, 0,
	0, &proc_ppc_htab_inode_operations,
	NULL, NULL,                		/* get_info, fill_inode */
	NULL,					/* next */
	NULL, NULL				/* parent, subdir */
};
#endif

__initfunc(void proc_root_init(void))
{
	proc_base_init();
	proc_register(&proc_root, &proc_root_loadavg);
	proc_register(&proc_root, &proc_root_uptime);
	proc_register(&proc_root, &proc_root_meminfo);
	proc_register(&proc_root, &proc_root_kmsg);
	proc_register(&proc_root, &proc_root_version);
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
	proc_register(&proc_root, &proc_root_partitions);
	proc_register(&proc_root, &proc_root_interrupts);
	proc_register(&proc_root, &proc_root_filesystems);
	proc_register(&proc_root, &proc_root_fs);
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
#ifdef CONFIG_PROC_HARDWARE
	proc_register(&proc_root, &proc_root_hardware);
#endif
#ifdef CONFIG_STRAM_PROC
	proc_register(&proc_root, &proc_root_stram);
#endif
	proc_register(&proc_root, &proc_root_slab);

	if (prof_shift) {
		proc_register(&proc_root, &proc_root_profile);
		proc_root_profile.size = (1+prof_len) * sizeof(unsigned int);
	}

	proc_tty_init();
#ifdef __powerpc__
	proc_register(&proc_root, &proc_root_ppc_htab);
#endif
#ifdef CONFIG_PROC_DEVICETREE
	proc_device_tree_init();
#endif

	proc_bus = create_proc_entry("bus", S_IFDIR, 0);
}

/*
 * As some entries in /proc are volatile, we want to 
 * get rid of unused dentries.  This could be made 
 * smarter: we could keep a "volatile" flag in the 
 * inode to indicate which ones to keep.
 */
static void
proc_delete_dentry(struct dentry * dentry)
{
	d_drop(dentry);
}

struct dentry_operations proc_dentry_operations =
{
	NULL,			/* revalidate */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
	proc_delete_dentry	/* d_delete(struct dentry *) */
};

/*
 * Don't create negative dentries here, return -ENOENT by hand
 * instead.
 */
int proc_lookup(struct inode * dir, struct dentry *dentry)
{
	struct inode *inode;
	struct proc_dir_entry * de;
	int error;

	error = -ENOTDIR;
	if (!dir || !S_ISDIR(dir->i_mode))
		goto out;

	error = -ENOENT;
	inode = NULL;
	de = (struct proc_dir_entry *) dir->u.generic_ip;
	if (de) {
		for (de = de->subdir; de ; de = de->next) {
			if (!de || !de->low_ino)
				continue;
			if (de->namelen != dentry->d_name.len)
				continue;
			if (!memcmp(dentry->d_name.name, de->name, de->namelen)) {
				int ino = de->low_ino | (dir->i_ino & ~(0xffff));
				error = -EINVAL;
				inode = proc_get_inode(dir->i_sb, ino, de);
				break;
			}
		}
	}

	if (inode) {
		dentry->d_op = &proc_dentry_operations;
		d_add(dentry, inode);
		error = 0;
	}
out:
	return error;
}

static int proc_root_lookup(struct inode * dir, struct dentry * dentry)
{
	unsigned int pid, c;
	struct task_struct *p;
	const char *name;
	struct inode *inode;
	int len;

	if (dir->i_ino == PROC_ROOT_INO) { /* check for safety... */
		dir->i_nlink = proc_root.nlink;

		read_lock(&tasklist_lock);
		for_each_task(p) {
			if (p->pid)
				dir->i_nlink++;
		}
		read_unlock(&tasklist_lock);
	}

	if (!proc_lookup(dir, dentry))
		return 0;
	
	pid = 0;
	name = dentry->d_name.name;
	len = dentry->d_name.len;
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
	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);
	inode = NULL;
	if (pid && p) {
		unsigned long ino = (pid << 16) + PROC_PID_INO;
		inode = proc_get_inode(dir->i_sb, ino, &proc_pid);
		if (!inode)
			return -EINVAL;
		inode->i_flags|=S_IMMUTABLE;
	}

	dentry->d_op = &proc_dentry_operations;
	d_add(dentry, inode);
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
int proc_readdir(struct file * filp,
	void * dirent, filldir_t filldir)
{
	struct proc_dir_entry * de;
	unsigned int ino;
	int i;
	struct inode *inode = filp->f_dentry->d_inode;

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

#define PROC_NUMBUF 10
#define PROC_MAXPIDS 20

/*
 * Get a few pid's to return for filldir - we need to hold the
 * tasklist lock while doing this, and we must release it before
 * we actually do the filldir itself, so we use a temp buffer..
 */
static int get_pid_list(int index, unsigned int *pids)
{
	struct task_struct *p;
	int nr_pids = 0;

	index -= FIRST_PROCESS_ENTRY;
	read_lock(&tasklist_lock);
	for_each_task(p) {
		int pid = p->pid;
		if (!pid)
			continue;
		if (--index >= 0)
			continue;
		pids[nr_pids] = pid;
		nr_pids++;
		if (nr_pids >= PROC_MAXPIDS)
			break;
	}
	read_unlock(&tasklist_lock);
	return nr_pids;
}

static int proc_root_readdir(struct file * filp,
	void * dirent, filldir_t filldir)
{
	unsigned int pid_array[PROC_MAXPIDS];
	char buf[PROC_NUMBUF];
	unsigned int nr = filp->f_pos;
	unsigned int nr_pids, i;

	if (nr < FIRST_PROCESS_ENTRY) {
		int error = proc_readdir(filp, dirent, filldir);
		if (error <= 0)
			return error;
		filp->f_pos = nr = FIRST_PROCESS_ENTRY;
	}

	nr_pids = get_pid_list(nr, pid_array);

	for (i = 0; i < nr_pids; i++) {
		int pid = pid_array[i];
		ino_t ino = (pid << 16) + PROC_PID_INO;
		unsigned long j = PROC_NUMBUF;

		do {
			j--;
			buf[j] = '0' + (pid % 10);
			pid /= 10;
		} while (pid);

		if (filldir(dirent, buf+j, PROC_NUMBUF-j, filp->f_pos, ino) < 0)
			break;
		filp->f_pos++;
	}
	return 0;
}

static int proc_unlink(struct inode *dir, struct dentry *dentry)
{
	struct proc_dir_entry * dp = dir->u.generic_ip;

printk("proc_file_unlink: deleting %s/%s\n", dp->name, dentry->d_name.name);

	remove_proc_entry(dentry->d_name.name, dp);
	dentry->d_inode->i_nlink = 0;
	d_delete(dentry);
	return 0;
}

#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/config.h>
#include <linux/malloc.h>

/*
 * The proc filesystem constants/structures
 */

/*
 * Offset of the first process in the /proc root directory..
 */
#define FIRST_PROCESS_ENTRY 256


/*
 * We always define these enumerators
 */

enum {
	PROC_ROOT_INO = 1,
};

/* Finally, the dynamically allocatable proc entries are reserved: */

#define PROC_DYNAMIC_FIRST 4096
#define PROC_NDYNAMIC      4096
#define PROC_OPENPROM_FIRST (PROC_DYNAMIC_FIRST+PROC_NDYNAMIC)
#define PROC_OPENPROM	   PROC_OPENPROM_FIRST
#define PROC_NOPENPROM	   4096
#define PROC_OPENPROMD_FIRST (PROC_OPENPROM_FIRST+PROC_NOPENPROM)
#define PROC_NOPENPROMD	   4096

#define PROC_SUPER_MAGIC 0x9fa0

/*
 * This is not completely implemented yet. The idea is to
 * create an in-memory tree (like the actual /proc filesystem
 * tree) of these proc_dir_entries, so that we can dynamically
 * add new files to /proc.
 *
 * The "next" pointer creates a linked list of one /proc directory,
 * while parent/subdir create the directory structure (every
 * /proc file has a parent, but "subdir" is NULL for all
 * non-directory entries).
 *
 * "get_info" is called at "read", while "owner" is used to protect module
 * from unloading while proc_dir_entry is in use
 */

typedef	int (read_proc_t)(char *page, char **start, off_t off,
			  int count, int *eof, void *data);
typedef	int (write_proc_t)(struct file *file, const char *buffer,
			   unsigned long count, void *data);
typedef int (get_info_t)(char *, char **, off_t, int, int);

struct proc_dir_entry {
	unsigned short low_ino;
	unsigned short namelen;
	const char *name;
	mode_t mode;
	nlink_t nlink;
	uid_t uid;
	gid_t gid;
	unsigned long size;
	struct inode_operations * ops;
	get_info_t *get_info;
	struct module *owner;
	struct proc_dir_entry *next, *parent, *subdir;
	void *data;
	read_proc_t *read_proc;
	write_proc_t *write_proc;
	int (*readlink_proc)(struct proc_dir_entry *de, char *page);
	unsigned int count;	/* use count */
	int deleted;		/* delete flag */
};

#define PROC_INODE_PROPER(inode) ((inode)->i_ino & ~0xffff)
#define PROC_INODE_OPENPROM(inode) \
	((inode->i_ino >= PROC_OPENPROM_FIRST) \
	    && (inode->i_ino < PROC_OPENPROM_FIRST + PROC_NOPENPROM))

#ifdef CONFIG_PROC_FS

extern struct proc_dir_entry proc_root;
extern struct proc_dir_entry *proc_root_fs;
extern struct proc_dir_entry *proc_net;
extern struct proc_dir_entry proc_sys;
extern struct proc_dir_entry proc_openprom;
extern struct proc_dir_entry *proc_mca;
extern struct proc_dir_entry *proc_bus;
extern struct proc_dir_entry *proc_root_driver;
extern struct proc_dir_entry proc_root_kcore;

extern void proc_root_init(void);
extern void proc_misc_init(void);

struct dentry *proc_pid_lookup(struct inode *dir, struct dentry * dentry);
void proc_pid_delete_inode(struct inode *inode);
int proc_pid_readdir(struct file * filp, void * dirent, filldir_t filldir);

extern int proc_register(struct proc_dir_entry *, struct proc_dir_entry *);
extern int proc_unregister(struct proc_dir_entry *, int);

extern struct proc_dir_entry *create_proc_entry(const char *name, mode_t mode,
						struct proc_dir_entry *parent);
extern void remove_proc_entry(const char *name, struct proc_dir_entry *parent);


/*
 * retrieve the proc_dir_entry associated with /proc/driver/$module_name
 */
extern inline
struct proc_dir_entry *proc_driver_find (const char *module_name)
{
        struct proc_dir_entry *p;
        
        p = proc_root_driver->subdir;
        while (p != NULL) {
                if (strcmp (p->name, module_name) == 0)
                        return p;
                
                p = p->next;
        }
        return NULL;
}


/*
 * remove /proc/driver/$module_name, and all its contents
 */
extern inline int proc_driver_unregister(const char *module_name)
{
        remove_proc_entry (module_name, proc_root_driver);
        return 0;
}


/*
 * create driver-specific playground directory, /proc/driver/$module_name
 */
extern inline int proc_driver_register(const char *module_name)
{
        struct proc_dir_entry *p;

        p = create_proc_entry (module_name, S_IFDIR, proc_root_driver);

        return (p == NULL) ? -1 : 0;
}

extern struct super_block *proc_super_blocks;
extern struct dentry_operations proc_dentry_operations;
extern struct super_block *proc_read_super(struct super_block *,void *,int);
extern int init_proc_fs(void);
extern struct inode * proc_get_inode(struct super_block *, int, struct proc_dir_entry *);
extern int proc_statfs(struct super_block *, struct statfs *, int);
extern void proc_read_inode(struct inode *);
extern void proc_write_inode(struct inode *);

extern int proc_match(int, const char *,struct proc_dir_entry *);

/*
 * These are generic /proc routines that use the internal
 * "struct proc_dir_entry" tree to traverse the filesystem.
 *
 * The /proc root directory has extended versions to take care
 * of the /proc/<pid> subdirectories.
 */
extern int proc_readdir(struct file *, void *, filldir_t);
extern struct dentry *proc_lookup(struct inode *, struct dentry *);

struct openpromfs_dev {
 	struct openpromfs_dev *next;
 	u32 node;
 	ino_t inode;
 	kdev_t rdev;
 	mode_t mode;
 	char name[32];
};
extern struct inode_operations *
proc_openprom_register(int (*readdir)(struct file *, void *, filldir_t),
		       struct dentry * (*lookup)(struct inode *, struct dentry *),
		       void (*use)(struct inode *, int),
		       struct openpromfs_dev ***);
extern void proc_openprom_deregister(void);
extern void (*proc_openprom_use)(struct inode *,int);
extern int proc_openprom_regdev(struct openpromfs_dev *);
extern int proc_openprom_unregdev(struct openpromfs_dev *);
  
extern struct inode_operations proc_dir_inode_operations;
extern struct inode_operations proc_file_inode_operations;
extern struct inode_operations proc_openprom_inode_operations;
extern struct inode_operations proc_sys_inode_operations;
extern struct inode_operations proc_kcore_inode_operations;
extern struct inode_operations proc_profile_inode_operations;
extern struct inode_operations proc_kmsg_inode_operations;
#if CONFIG_AP1000
extern struct inode_operations proc_ringbuf_inode_operations;
#endif
extern struct inode_operations proc_omirr_inode_operations;
extern struct inode_operations proc_ppc_htab_inode_operations;

/*
 * proc_tty.c
 */
extern void proc_tty_init(void);
extern void proc_tty_register_driver(struct tty_driver *driver);
extern void proc_tty_unregister_driver(struct tty_driver *driver);

/*
 * proc_devtree.c
 */
extern void proc_device_tree_init(void);

extern inline struct proc_dir_entry *create_proc_read_entry(const char *name,
	mode_t mode, struct proc_dir_entry *base, 
	read_proc_t *read_proc, void * data)
{
	struct proc_dir_entry *res=create_proc_entry(name,mode,base);
	if (res) {
		res->read_proc=read_proc;
		res->data=data;
	}
	return res;
}
 
extern inline struct proc_dir_entry *create_proc_info_entry(const char *name,
	mode_t mode, struct proc_dir_entry *base, get_info_t *get_info)
{
	struct proc_dir_entry *res=create_proc_entry(name,mode,base);
	if (res) res->get_info=get_info;
	return res;
}
 
extern inline struct proc_dir_entry *proc_net_create(const char *name,
	mode_t mode, get_info_t *get_info)
{
	return create_proc_info_entry(name,mode,proc_net,get_info);
}

extern inline void proc_net_remove(const char *name)
{
	remove_proc_entry(name,proc_net);
}

#else

extern inline int proc_register(struct proc_dir_entry *a, struct proc_dir_entry *b) { return 0; }
extern inline int proc_unregister(struct proc_dir_entry *a, int b) { return 0; }
extern inline struct proc_dir_entry *proc_net_create(const char *name, mode_t mode, 
	get_info_t *get_info) {return NULL;}
extern inline void proc_net_remove(const char *name) {}

extern inline struct proc_dir_entry *create_proc_entry(const char *name,
	mode_t mode, struct proc_dir_entry *parent) { return NULL; }

extern inline void remove_proc_entry(const char *name, struct proc_dir_entry *parent) {};

extern inline struct proc_dir_entry *create_proc_read_entry(const char *name,
	mode_t mode, struct proc_dir_entry *base, 
	int (*read_proc)(char *, char **, off_t, int, int *, void *),
	void * data) { return NULL; }
extern inline struct proc_dir_entry *create_proc_info_entry(const char *name,
	mode_t mode, struct proc_dir_entry *base, get_info_t *get_info)
	{ return NULL; }

extern inline void proc_tty_register_driver(struct tty_driver *driver) {};
extern inline void proc_tty_unregister_driver(struct tty_driver *driver) {};

extern inline
struct proc_dir_entry *proc_driver_find (const char *module_name)
{
        return NULL;
}

extern inline int proc_driver_unregister(const char *module_name)
{
        return 0;
}

extern inline int proc_driver_register(const char *module_name)
{
        return 0;
}

extern struct proc_dir_entry proc_root;

#endif /* CONFIG_PROC_FS */

#endif /* _LINUX_PROC_FS_H */

/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
 */

#include <linux/config.h>
#include <linux/fs.h>

#include <linux/minix_fs.h>
#include <linux/ext_fs.h>
#include <linux/ext2_fs.h>
#include <linux/xia_fs.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>
#include <linux/proc_fs.h>
#include <linux/nfs_fs.h>
#include <linux/iso_fs.h>
#include <linux/sysv_fs.h>
#include <linux/hpfs_fs.h>
#include <linux/smb_fs.h>
#include <linux/major.h>

extern void device_setup(void);

#ifdef CONFIG_ROOT_NFS
extern int nfs_root_init(char *nfsname);
extern char nfs_root_name [];
#endif

/* This may be used only once, enforced by 'static int callable' */
asmlinkage int sys_setup(void)
{
	static int callable = 1;

	if (!callable)
		return -1;
	callable = 0;

	device_setup();

#ifdef CONFIG_EXT_FS
	register_filesystem(&(struct file_system_type)
		{ext_read_super, "ext", 1, NULL});
#endif

#ifdef CONFIG_EXT2_FS
	register_filesystem(&(struct file_system_type)
		{ext2_read_super, "ext2", 1, NULL});
#endif

#ifdef CONFIG_XIA_FS
	register_filesystem(&(struct file_system_type)
		{xiafs_read_super, "xiafs", 1, NULL});
#endif

#ifdef CONFIG_MINIX_FS
	register_filesystem(&(struct file_system_type)
		{minix_read_super, "minix", 1, NULL});
#endif

#ifdef CONFIG_UMSDOS_FS
	register_filesystem(&(struct file_system_type)
	{UMSDOS_read_super,	"umsdos",	1, NULL});
#endif

#ifdef CONFIG_MSDOS_FS
	register_filesystem(&(struct file_system_type)
		{msdos_read_super, "msdos", 1, NULL});
#endif

#ifdef CONFIG_PROC_FS
	register_filesystem(&(struct file_system_type)
		{proc_read_super, "proc", 0, NULL});
#endif

#ifdef CONFIG_NFS_FS
	register_filesystem(&(struct file_system_type)
		{nfs_read_super, "nfs", 0, NULL});
#endif

#ifdef CONFIG_SMB_FS
	register_filesystem(&(struct file_system_type)
		{smb_read_super, "smbfs", 0, NULL});
#endif

#ifdef CONFIG_ISO9660_FS
	register_filesystem(&(struct file_system_type)
		{isofs_read_super, "iso9660", 1, NULL});
#endif

#ifdef CONFIG_SYSV_FS
	register_filesystem(&(struct file_system_type)
		{sysv_read_super, "xenix", 1, NULL});

	register_filesystem(&(struct file_system_type)
		{sysv_read_super, "sysv", 1, NULL});

	register_filesystem(&(struct file_system_type)
		{sysv_read_super, "coherent", 1, NULL});
#endif

#ifdef CONFIG_HPFS_FS
	register_filesystem(&(struct file_system_type)
		{hpfs_read_super, "hpfs", 1, NULL});
#endif

#ifdef CONFIG_ROOT_NFS
	if (nfs_root_name [0]){
		if (nfs_root_init(nfs_root_name) < 0) {
			printk(KERN_ERR "Root-NFS: Unable to mount NFS filesystem as /, using /dev/fd0 instead\n");
			ROOT_DEV = MKDEV(FLOPPY_MAJOR, 0);
		}
	}
#endif
	mount_root();
	return 0;
}

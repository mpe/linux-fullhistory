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
#include <linux/ncp_fs.h>
#include <linux/major.h>

extern void device_setup(void);

#ifdef CONFIG_ROOT_NFS
extern int nfs_root_init(char *nfsname, char *nfsaddrs);
extern char nfs_root_name [];
extern char nfs_root_addrs [];
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
	init_ext_fs();
#endif

#ifdef CONFIG_EXT2_FS
	init_ext2_fs();
#endif

#ifdef CONFIG_XIA_FS
	init_xiafs_fs();
#endif

#ifdef CONFIG_MINIX_FS
	init_minix_fs();
#endif

#ifdef CONFIG_UMSDOS_FS
	init_umsdos_fs();
#endif

#ifdef CONFIG_MSDOS_FS
	init_msdos_fs();
#endif

#ifdef CONFIG_PROC_FS
	init_proc_fs();
#endif

#ifdef CONFIG_NFS_FS
	init_nfs_fs();
#endif

#ifdef CONFIG_SMB_FS
	init_smb_fs();
#endif

#ifdef CONFIG_NCP_FS
	init_ncp_fs();
#endif

#ifdef CONFIG_ISO9660_FS
	init_iso9660_fs();
#endif

#ifdef CONFIG_SYSV_FS
	init_sysv_fs();
#endif

#ifdef CONFIG_HPFS_FS
	init_hpfs_fs();
#endif

#ifdef CONFIG_ROOT_NFS
	if (ROOT_DEV == MKDEV(UNNAMED_MAJOR, 255)) {
		if (nfs_root_init(nfs_root_name, nfs_root_addrs) < 0) {
			printk(KERN_ERR "Root-NFS: Unable to contact NFS server for root fs, using /dev/fd0 instead\n");
			ROOT_DEV = MKDEV(FLOPPY_MAJOR, 0);
		}
	}
#endif

	mount_root();
	return 0;
}

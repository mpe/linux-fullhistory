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
#include <linux/affs_fs.h>
#include <linux/ufs_fs.h>
#include <linux/major.h>

extern void device_setup(void);
extern void binfmt_setup(void);

/* This may be used only once, enforced by 'static int callable' */
asmlinkage int sys_setup(void)
{
	static int callable = 1;

	if (!callable)
		return -1;
	callable = 0;

	device_setup();

	binfmt_setup();

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

#ifdef CONFIG_FAT_FS
	init_fat_fs();
#endif

#ifdef CONFIG_MSDOS_FS
	init_msdos_fs();
#endif

#ifdef CONFIG_VFAT_FS
	init_vfat_fs();
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

#ifdef CONFIG_AFFS_FS
	init_affs_fs();
#endif

#ifdef CONFIG_UFS_FS
	init_ufs_fs();
#endif

	mount_root();
	return 0;
}

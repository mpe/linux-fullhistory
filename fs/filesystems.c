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
#include <linux/ext2_fs.h>
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
#include <linux/romfs_fs.h>
#include <linux/auto_fs.h>
#include <linux/qnx4_fs.h>
#include <linux/ntfs_fs.h>
#include <linux/hfs_fs.h>
#include <linux/devpts_fs.h>
#include <linux/major.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#include <linux/lockd/bind.h>
#include <linux/lockd/xdr.h>
#include <linux/init.h>
#include <linux/nls.h>

#ifdef CONFIG_CODA_FS
extern int init_coda(void);
#endif

#ifdef CONFIG_DEVPTS_FS
extern int init_devpts_fs(void);
#endif

void __init filesystem_setup(void)
{
#ifdef CONFIG_EXT2_FS
	init_ext2_fs();
#endif

#ifdef CONFIG_MINIX_FS
	init_minix_fs();
#endif

#ifdef CONFIG_ROMFS_FS
	init_romfs_fs();
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

#ifdef CONFIG_LOCKD
	nlmxdr_init();
#endif

#ifdef CONFIG_NFS_FS
	init_nfs_fs();
#endif

#ifdef CONFIG_CODA_FS
	init_coda();
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

#ifdef CONFIG_NTFS_FS
	init_ntfs_fs();
#endif

#ifdef CONFIG_HFS_FS
	init_hfs_fs();
#endif

#ifdef CONFIG_AFFS_FS
	init_affs_fs();
#endif

#ifdef CONFIG_UFS_FS
	init_ufs_fs();
#endif

#ifdef CONFIG_AUTOFS_FS
	init_autofs_fs();
#endif

#ifdef CONFIG_ADFS_FS
	init_adfs_fs();
#endif

#ifdef CONFIG_DEVPTS_FS
	init_devpts_fs();
#endif

#ifdef CONFIG_QNX4FS_FS
	init_qnx4_fs();
#endif
   
#ifdef CONFIG_NLS
	init_nls();
#endif
}

#ifndef CONFIG_NFSD
#ifdef CONFIG_NFSD_MODULE
int (*do_nfsservctl)(int, void *, void *) = NULL;
#endif
int
asmlinkage sys_nfsservctl(int cmd, void *argp, void *resp)
{
#ifndef CONFIG_NFSD_MODULE
	return -ENOSYS;
#else
	int ret = -ENOSYS;
	
	lock_kernel();
	if (do_nfsservctl) {
		ret = do_nfsservctl(cmd, argp, resp);
		goto out;
	}
#ifdef CONFIG_KMOD
	if (request_module ("nfsd") == 0) {
		if (do_nfsservctl)
			ret = do_nfsservctl(cmd, argp, resp);
	}
#endif /* CONFIG_KMOD */
out:
	unlock_kernel();
	return ret;
#endif /* CONFIG_NFSD_MODULE */
}
#endif /* CONFIG_NFSD */

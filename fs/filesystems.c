/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
 */

#include <linux/config.h>
#include <linux/fs.h>

#include <linux/devfs_fs_kernel.h>
#include <linux/nfs_fs.h>
#include <linux/auto_fs.h>
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

#ifdef CONFIG_CODA_FS
extern int init_coda(void);
#endif

#ifdef CONFIG_DEVPTS_FS
extern int init_devpts_fs(void);
#endif

void __init filesystem_setup(void)
{
	init_devfs_fs();  /*  Header file may make this empty  */

#ifdef CONFIG_NFS_FS
	init_nfs_fs();
#endif

#ifdef CONFIG_CODA_FS
	init_coda();
#endif

#ifdef CONFIG_DEVPTS_FS
	init_devpts_fs();
#endif
}

#ifndef CONFIG_NFSD
#ifdef CONFIG_NFSD_MODULE
long (*do_nfsservctl)(int, void *, void *);
#endif
long
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

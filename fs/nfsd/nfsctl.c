/*
 * linux/fs/nfsd/nfsctl.c
 *
 * Syscall interface to knfsd.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>

#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/nfs.h>
#include <linux/version.h>
#include <linux/unistd.h>
#include <linux/malloc.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>

#if LINUX_VERSION_CODE >= 0x020100
#include <asm/uaccess.h>
#else
# define copy_from_user		memcpy_fromfs
# define copy_to_user		memcpy_tofs
# define access_ok		!verify_area
#endif
#include <linux/smp.h>
#include <linux/smp_lock.h>

extern long sys_call_table[];

static int	nfsctl_svc(struct nfsctl_svc *data);
static int	nfsctl_addclient(struct nfsctl_client *data);
static int	nfsctl_delclient(struct nfsctl_client *data);
static int	nfsctl_export(struct nfsctl_export *data);
static int	nfsctl_unexport(struct nfsctl_export *data);
static int	nfsctl_getfh(struct nfsctl_fhparm *, struct knfs_fh *);
/* static int	nfsctl_ugidupdate(struct nfsctl_ugidmap *data); */

static int	initialized = 0;

/*
 * Initialize nfsd
 */
static void
nfsd_init(void)
{
	nfsd_xdr_init();	/* XDR */
#ifdef CONFIG_PROC_FS
	nfsd_stat_init();	/* Statistics */
#endif
	nfsd_cache_init();	/* RPC reply cache */
	nfsd_export_init();	/* Exports table */
	nfsd_lockd_init();	/* lockd->nfsd callbacks */
	nfsd_racache_init();	/* Readahead param cache */
	initialized = 1;
}

static inline int
nfsctl_svc(struct nfsctl_svc *data)
{
	return nfsd_svc(data->svc_port, data->svc_nthreads);
}

static inline int
nfsctl_addclient(struct nfsctl_client *data)
{
	return exp_addclient(data);
}

static inline int
nfsctl_delclient(struct nfsctl_client *data)
{
	return exp_delclient(data);
}

static inline int
nfsctl_export(struct nfsctl_export *data)
{
	return exp_export(data);
}

static inline int
nfsctl_unexport(struct nfsctl_export *data)
{
	return exp_unexport(data);
}

#ifdef notyet
static inline int
nfsctl_ugidupdate(nfs_ugidmap *data)
{
	return -EINVAL;
}
#endif

static inline int
nfsctl_getfh(struct nfsctl_fhparm *data, struct knfs_fh *res)
{
	struct sockaddr_in	*sin;
	struct svc_client	*clp;
	int			err = 0;

	if (data->gf_addr.sa_family != AF_INET)
		return -EPROTONOSUPPORT;
	if (data->gf_version < 2 || data->gf_version > NFSSVC_MAXVERS)
		return -EINVAL;
	sin = (struct sockaddr_in *)&data->gf_addr;

	exp_readlock();
	if (!(clp = exp_getclient(sin)))
		err = -EPERM;
	else
		err = exp_rootfh(clp, data->gf_dev, data->gf_ino, res);
	exp_unlock();

	return err;
}

#ifdef CONFIG_NFSD
#define handle_sys_nfsservctl sys_nfsservctl
#endif

int
asmlinkage handle_sys_nfsservctl(int cmd, struct nfsctl_arg *argp,
			         union nfsctl_res *resp)
{
	struct nfsctl_arg *	arg = NULL;
	union nfsctl_res *	res = NULL;
	int			err;

	lock_kernel ();
	if (!initialized)
		nfsd_init();
	if (!suser()) {
		err = -EPERM;
		goto done;
	}
	if (!access_ok(VERIFY_READ, argp, sizeof(*argp))
	 || (resp && !access_ok(VERIFY_WRITE, resp, sizeof(*resp)))) {
		err = -EFAULT;
		goto done;
	}
	if (!(arg = kmalloc(sizeof(*arg), GFP_USER)) ||
	    (resp && !(res = kmalloc(sizeof(*res), GFP_USER)))) {
		err = -ENOMEM;	/* ??? */
		goto done;
	}
	copy_from_user(arg, argp, sizeof(*argp));
	if (arg->ca_version != NFSCTL_VERSION) {
		printk(KERN_WARNING "nfsd: incompatible version in syscall.\n");
		err = -EINVAL;
		goto done;
	}

	MOD_INC_USE_COUNT;
	switch(cmd) {
	case NFSCTL_SVC:
		err = nfsctl_svc(&arg->ca_svc);
		break;
	case NFSCTL_ADDCLIENT:
		err = nfsctl_addclient(&arg->ca_client);
		break;
	case NFSCTL_DELCLIENT:
		err = nfsctl_delclient(&arg->ca_client);
		break;
	case NFSCTL_EXPORT:
		err = nfsctl_export(&arg->ca_export);
		break;
	case NFSCTL_UNEXPORT:
		err = nfsctl_unexport(&arg->ca_export);
		break;
#ifdef notyet
	case NFSCTL_UGIDUPDATE:
		err = nfsctl_ugidupdate(&arg->ca_umap);
		break;
#endif
	case NFSCTL_GETFH:
		err = nfsctl_getfh(&arg->ca_getfh, &res->cr_getfh);
		break;
	default:
		err = -EINVAL;
	}
	MOD_DEC_USE_COUNT;

	if (!err && resp)
		copy_to_user(resp, res, sizeof(*resp));

done:
	if (arg)
		kfree(arg);
	if (res)
		kfree(res);

	unlock_kernel ();
	return err;
}

#ifdef MODULE
/* New-style module support since 2.1.18 */
#if LINUX_VERSION_CODE >= 131346
EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
#endif

extern int (*do_nfsservctl)(int, void *, void *);

/*
 * Initialize the module
 */
int
init_module(void)
{
	printk("Installing knfsd (copyright (C) 1996 okir@monad.swb.de).\n");
	nfsd_init();
	do_nfsservctl = handle_sys_nfsservctl;
	return 0;
}

/*
 * Clean up the mess before unloading the module
 */
void
cleanup_module(void)
{
	if (MOD_IN_USE) {
		printk("nfsd: nfsd busy, remove delayed\n");
		return;
	}
	do_nfsservctl = NULL;
	nfsd_export_shutdown();
	nfsd_cache_shutdown();
#ifdef CONFIG_PROC_FS
	nfsd_stat_shutdown();
#endif
	nfsd_lockd_shutdown();
}
#endif

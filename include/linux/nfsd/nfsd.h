/*
 * linux/include/linux/nfsd/nfsd.h
 *
 * Hodge-podge collection of knfsd-related stuff.
 * I will sort this out later.
 *
 * Copyright (C) 1995 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_NFSD_H
#define LINUX_NFSD_NFSD_H

#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/dirent.h>
#include <linux/fs.h>

#include <linux/nfsd/debug.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/export.h>
#include <linux/nfsd/auth.h>
#include <linux/nfsd/stats.h>

/*
 * nfsd version
 */
#define NFSD_VERSION		"0.4"

#ifdef __KERNEL__
/*
 * Special flags for nfsd_permission. These must be different from MAY_READ,
 * MAY_WRITE, and MAY_EXEC.
 */
#define MAY_NOP			0
#define MAY_SATTR		8
#define MAY_TRUNC		16
#if (MAY_SATTR | MAY_TRUNC) & (MAY_READ | MAY_WRITE | MAY_EXEC)
# error "please use a different value for MAY_SATTR or MAY_TRUNC."
#endif
#define MAY_CREATE		(MAY_EXEC|MAY_WRITE)
#define MAY_REMOVE		(MAY_EXEC|MAY_WRITE|MAY_TRUNC)

/*
 * Callback function for readdir
 */
struct readdir_cd {
	struct svc_rqst *	rqstp;
	struct svc_fh *		dirfh;
	u32 *			buffer;
	int			buflen;
	u32 *			offset;		/* previous dirent->d_next */
	char			plus;		/* readdirplus */
	char			eob;		/* end of buffer */
	char			dotonly;
};
typedef int		(*encode_dent_fn)(struct readdir_cd *, const char *,
						int, off_t, ino_t);
typedef int (*nfsd_dirop_t)(struct inode *, struct dentry *, int, int);

/*
 * Procedure table for NFSv2
 */
extern struct svc_procedure	nfsd_procedures2[];
extern struct svc_program	nfsd_program;

/*
 * Function prototypes.
 */
int		nfsd_svc(unsigned short port, int nrservs);

/* nfsd/vfs.c */
int		fh_lock_parent(struct svc_fh *, struct dentry *);
void		nfsd_racache_init(void);
void		nfsd_racache_shutdown(void);
int		nfsd_lookup(struct svc_rqst *, struct svc_fh *,
				const char *, int, struct svc_fh *);
int		nfsd_setattr(struct svc_rqst *, struct svc_fh *,
				struct iattr *);
int		nfsd_create(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct iattr *attrs,
				int type, dev_t rdev, struct svc_fh *res);
int		nfsd_open(struct svc_rqst *, struct svc_fh *, int,
				int, struct file *);
void		nfsd_close(struct file *);
int		nfsd_read(struct svc_rqst *, struct svc_fh *,
				loff_t, char *, unsigned long *);
int		nfsd_write(struct svc_rqst *, struct svc_fh *,
				loff_t, char *, unsigned long, int);
int		nfsd_readlink(struct svc_rqst *, struct svc_fh *,
				char *, int *);
int		nfsd_symlink(struct svc_rqst *, struct svc_fh *,
				char *name, int len, char *path, int plen,
				struct svc_fh *res);
int		nfsd_link(struct svc_rqst *, struct svc_fh *,
				char *, int, struct svc_fh *);
int		nfsd_rename(struct svc_rqst *,
				struct svc_fh *, char *, int,
				struct svc_fh *, char *, int);
int		nfsd_remove(struct svc_rqst *,
				struct svc_fh *, char *, int);
int		nfsd_unlink(struct svc_rqst *, struct svc_fh *, int type,
				char *name, int len);
int		nfsd_truncate(struct svc_rqst *, struct svc_fh *,
				unsigned long size);
int		nfsd_readdir(struct svc_rqst *, struct svc_fh *,
				loff_t, encode_dent_fn,
				u32 *buffer, int *countp);
int		nfsd_statfs(struct svc_rqst *, struct svc_fh *,
				struct statfs *);
int		nfsd_notify_change(struct inode *, struct iattr *);
int		nfsd_permission(struct svc_export *, struct dentry *, int);

/* nfsd/nfsctl.c */
void		nfsd_modcount(struct inode *, int);

/*
 * lockd binding
 */
void		nfsd_lockd_init(void);
void		nfsd_lockd_shutdown(void);
void		nfsd_lockd_unexport(struct svc_client *);


#ifndef makedev
#define makedev(maj, min)	(((maj) << 8) | (min))
#endif

/*
 * These variables contain pre-xdr'ed values for faster operation.
 * FIXME: should be replaced by macros for big-endian machines.
 */
extern u32	nfs_ok,
		nfserr_perm,
		nfserr_noent,
		nfserr_io,
		nfserr_nxio,
		nfserr_acces,
		nfserr_exist,
		nfserr_xdev,
		nfserr_nodev,
		nfserr_notdir,
		nfserr_isdir,
		nfserr_inval,
		nfserr_fbig,
		nfserr_nospc,
		nfserr_rofs,
		nfserr_mlink,
		nfserr_nametoolong,
		nfserr_dquot,
		nfserr_stale,
		nfserr_remote,
		nfserr_badhandle,
		nfserr_notsync,
		nfserr_badcookie,
		nfserr_notsupp,
		nfserr_toosmall,
		nfserr_serverfault,
		nfserr_badtype,
		nfserr_jukebox;

/*
 * Time of server startup
 */
extern struct timeval	nfssvc_boot;

/*
 * The number of nfsd threads.
 */
extern int		nfsd_nservers;

#endif /* __KERNEL__ */

#endif /* LINUX_NFSD_NFSD_H */

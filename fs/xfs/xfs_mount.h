/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_MOUNT_H__
#define __XFS_MOUNT_H__


typedef struct xfs_trans_reservations {
	uint	tr_write;	/* extent alloc trans */
	uint	tr_itruncate;	/* truncate trans */
	uint	tr_rename;	/* rename trans */
	uint	tr_link;	/* link trans */
	uint	tr_remove;	/* unlink trans */
	uint	tr_symlink;	/* symlink trans */
	uint	tr_create;	/* create trans */
	uint	tr_mkdir;	/* mkdir trans */
	uint	tr_ifree;	/* inode free trans */
	uint	tr_ichange;	/* inode update trans */
	uint	tr_growdata;	/* fs data section grow trans */
	uint	tr_swrite;	/* sync write inode trans */
	uint	tr_addafork;	/* cvt inode to attributed trans */
	uint	tr_writeid;	/* write setuid/setgid file */
	uint	tr_attrinval;	/* attr fork buffer invalidation */
	uint	tr_attrset;	/* set/create an attribute */
	uint	tr_attrrm;	/* remove an attribute */
	uint	tr_clearagi;	/* clear bad agi unlinked ino bucket */
	uint	tr_growrtalloc; /* grow realtime allocations */
	uint	tr_growrtzero;	/* grow realtime zeroing */
	uint	tr_growrtfree;	/* grow realtime freeing */
} xfs_trans_reservations_t;


#ifndef __KERNEL__
/*
 * Moved here from xfs_ag.h to avoid reordering header files
 */
#define XFS_DADDR_TO_AGNO(mp,d) \
	((xfs_agnumber_t)(XFS_BB_TO_FSBT(mp, d) / (mp)->m_sb.sb_agblocks))
#define XFS_DADDR_TO_AGBNO(mp,d) \
	((xfs_agblock_t)(XFS_BB_TO_FSBT(mp, d) % (mp)->m_sb.sb_agblocks))
#else
struct cred;
struct vfs;
struct vnode;
struct xfs_mount_args;
struct xfs_ihash;
struct xfs_chash;
struct xfs_inode;
struct xfs_perag;
struct xfs_quotainfo;
struct xfs_iocore;
struct xfs_bmbt_irec;
struct xfs_bmap_free;

#define SPLDECL(s)		unsigned long s
#define AIL_LOCK_T		lock_t
#define AIL_LOCKINIT(x,y)	spinlock_init(x,y)
#define AIL_LOCK_DESTROY(x)	spinlock_destroy(x)
#define AIL_LOCK(mp,s)		s=mutex_spinlock(&(mp)->m_ail_lock)
#define AIL_UNLOCK(mp,s)	mutex_spinunlock(&(mp)->m_ail_lock, s)


/* Prototypes and functions for I/O core modularization, a vector
 * of functions is used to indirect from xfs/cxfs independent code
 * to the xfs/cxfs dependent code.
 * The vector is placed in the mount structure so that we can
 * minimize the number of memory indirections involved.
 */

typedef int		(*xfs_bmapi_t)(struct xfs_trans *, void *,
				xfs_fileoff_t, xfs_filblks_t, int,
				xfs_fsblock_t *, xfs_extlen_t,
				struct xfs_bmbt_irec *, int *,
				struct xfs_bmap_free *);
typedef int		(*xfs_bmap_eof_t)(void *, xfs_fileoff_t, int, int *);
typedef void		(*xfs_lock_t)(void *, uint);
typedef void		(*xfs_lock_demote_t)(void *, uint);
typedef int		(*xfs_lock_nowait_t)(void *, uint);
typedef void		(*xfs_unlk_t)(void *, unsigned int);
typedef void		(*xfs_chgtime_t)(void *, int);
typedef xfs_fsize_t	(*xfs_size_t)(void *);
typedef xfs_fsize_t	(*xfs_lastbyte_t)(void *);

typedef struct xfs_ioops {
	xfs_bmapi_t		xfs_bmapi_func;
	xfs_bmap_eof_t		xfs_bmap_eof_func;
	xfs_lock_t		xfs_ilock;
	xfs_lock_demote_t	xfs_ilock_demote;
	xfs_lock_nowait_t	xfs_ilock_nowait;
	xfs_unlk_t		xfs_unlock;
	xfs_chgtime_t		xfs_chgtime;
	xfs_size_t		xfs_size_func;
	xfs_lastbyte_t		xfs_lastbyte;
} xfs_ioops_t;


#define XFS_BMAPI(mp, trans,io,bno,len,f,first,tot,mval,nmap,flist)	\
	(*(mp)->m_io_ops.xfs_bmapi_func) \
		(trans,(io)->io_obj,bno,len,f,first,tot,mval,nmap,flist)

#define XFS_BMAP_EOF(mp, io, endoff, whichfork, eof) \
	(*(mp)->m_io_ops.xfs_bmap_eof_func) \
		((io)->io_obj, endoff, whichfork, eof)

#define XFS_ILOCK(mp, io, mode) \
	(*(mp)->m_io_ops.xfs_ilock)((io)->io_obj, mode)

#define XFS_IUNLOCK(mp, io, mode) \
	(*(mp)->m_io_ops.xfs_unlock)((io)->io_obj, mode)

#define XFS_ILOCK_DEMOTE(mp, io, mode) \
	(*(mp)->m_io_ops.xfs_ilock_demote)((io)->io_obj, mode)

#define XFS_SIZE(mp, io) \
	(*(mp)->m_io_ops.xfs_size_func)((io)->io_obj)

#define XFS_LASTBYTE(mp, io) \
	(*(mp)->m_io_ops.xfs_lastbyte)((io)->io_obj)


typedef struct xfs_mount {
	bhv_desc_t		m_bhv;		/* vfs xfs behavior */
	xfs_tid_t		m_tid;		/* next unused tid for fs */
	AIL_LOCK_T		m_ail_lock;	/* fs AIL mutex */
	xfs_ail_entry_t		m_ail;		/* fs active log item list */
	uint			m_ail_gen;	/* fs AIL generation count */
	xfs_sb_t		m_sb;		/* copy of fs superblock */
	lock_t			m_sb_lock;	/* sb counter mutex */
	struct xfs_buf		*m_sb_bp;	/* buffer for superblock */
	char			*m_fsname;	/* filesystem name */
	int			m_fsname_len;	/* strlen of fs name */
	int			m_bsize;	/* fs logical block size */
	xfs_agnumber_t		m_agfrotor;	/* last ag where space found */
	xfs_agnumber_t		m_agirotor;	/* last ag dir inode alloced */
	xfs_agnumber_t		m_maxagi;	/* highest inode alloc group */
	int			m_ihsize;	/* size of next field */
	struct xfs_ihash	*m_ihash;	/* fs private inode hash table*/
	struct xfs_inode	*m_inodes;	/* active inode list */
	mutex_t			m_ilock;	/* inode list mutex */
	uint			m_ireclaims;	/* count of calls to reclaim*/
	uint			m_readio_log;	/* min read size log bytes */
	uint			m_readio_blocks; /* min read size blocks */
	uint			m_writeio_log;	/* min write size log bytes */
	uint			m_writeio_blocks; /* min write size blocks */
	void			*m_log;		/* log specific stuff */
	int			m_logbufs;	/* number of log buffers */
	int			m_logbsize;	/* size of each log buffer */
	uint			m_rsumlevels;	/* rt summary levels */
	uint			m_rsumsize;	/* size of rt summary, bytes */
	struct xfs_inode	*m_rbmip;	/* pointer to bitmap inode */
	struct xfs_inode	*m_rsumip;	/* pointer to summary inode */
	struct xfs_inode	*m_rootip;	/* pointer to root directory */
	struct xfs_quotainfo	*m_quotainfo;	/* disk quota information */
	xfs_buftarg_t		*m_ddev_targp;	/* saves taking the address */
	xfs_buftarg_t		*m_logdev_targp;/* ptr to log device */
	xfs_buftarg_t		*m_rtdev_targp;	/* ptr to rt device */
#define m_dev		m_ddev_targp->pbr_dev
	__uint8_t		m_dircook_elog; /* log d-cookie entry bits */
	__uint8_t		m_blkbit_log;	/* blocklog + NBBY */
	__uint8_t		m_blkbb_log;	/* blocklog - BBSHIFT */
	__uint8_t		m_agno_log;	/* log #ag's */
	__uint8_t		m_agino_log;	/* #bits for agino in inum */
	__uint8_t		m_nreadaheads;	/* #readahead buffers */
	__uint16_t		m_inode_cluster_size;/* min inode buf size */
	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	uint			m_blockwmask;	/* blockwsize-1 */
	uint			m_alloc_mxr[2]; /* XFS_ALLOC_BLOCK_MAXRECS */
	uint			m_alloc_mnr[2]; /* XFS_ALLOC_BLOCK_MINRECS */
	uint			m_bmap_dmxr[2]; /* XFS_BMAP_BLOCK_DMAXRECS */
	uint			m_bmap_dmnr[2]; /* XFS_BMAP_BLOCK_DMINRECS */
	uint			m_inobt_mxr[2]; /* XFS_INOBT_BLOCK_MAXRECS */
	uint			m_inobt_mnr[2]; /* XFS_INOBT_BLOCK_MINRECS */
	uint			m_ag_maxlevels; /* XFS_AG_MAXLEVELS */
	uint			m_bm_maxlevels[2]; /* XFS_BM_MAXLEVELS */
	uint			m_in_maxlevels; /* XFS_IN_MAXLEVELS */
	struct xfs_perag	*m_perag;	/* per-ag accounting info */
	struct rw_semaphore	m_peraglock;	/* lock for m_perag (pointer) */
	sema_t			m_growlock;	/* growfs mutex */
	int			m_fixedfsid[2]; /* unchanged for life of FS */
	uint			m_dmevmask;	/* DMI events for this FS */
	uint			m_flags;	/* global mount flags */
	uint			m_attroffset;	/* inode attribute offset */
	uint			m_dir_node_ents; /* #entries in a dir danode */
	uint			m_attr_node_ents; /* #entries in attr danode */
	int			m_ialloc_inos;	/* inodes in inode allocation */
	int			m_ialloc_blks;	/* blocks in inode allocation */
	int			m_litino;	/* size of inode union area */
	int			m_inoalign_mask;/* mask sb_inoalignmt if used */
	uint			m_qflags;	/* quota status flags */
	xfs_trans_reservations_t m_reservations;/* precomputed res values */
	__uint64_t		m_maxicount;	/* maximum inode count */
	__uint64_t		m_resblks;	/* total reserved blocks */
	__uint64_t		m_resblks_avail;/* available reserved blocks */
#if XFS_BIG_FILESYSTEMS
	xfs_ino_t		m_inoadd;	/* add value for ino64_offset */
#endif
	int			m_dalign;	/* stripe unit */
	int			m_swidth;	/* stripe width */
	int			m_lstripemask;	/* log stripe mask */
	int			m_sinoalign;	/* stripe unit inode alignmnt */
	int			m_attr_magicpct;/* 37% of the blocksize */
	int			m_dir_magicpct; /* 37% of the dir blocksize */
	__uint8_t		m_mk_sharedro;	/* mark shared ro on unmount */
	__uint8_t		m_inode_quiesce;/* call quiesce on new inodes.
						   field governed by m_ilock */
	__uint8_t		m_dirversion;	/* 1 or 2 */
	xfs_dirops_t		m_dirops;	/* table of dir funcs */
	int			m_dirblksize;	/* directory block sz--bytes */
	int			m_dirblkfsbs;	/* directory block sz--fsbs */
	xfs_dablk_t		m_dirdatablk;	/* blockno of dir data v2 */
	xfs_dablk_t		m_dirleafblk;	/* blockno of dir non-data v2 */
	xfs_dablk_t		m_dirfreeblk;	/* blockno of dirfreeindex v2 */
	int			m_chsize;	/* size of next field */
	struct xfs_chash	*m_chash;	/* fs private inode per-cluster
						 * hash table */
	struct xfs_ioops	m_io_ops;	/* vector of I/O ops */
	struct xfs_expinfo	*m_expinfo;	/* info to export to other
						   cells. */
	uint64_t		m_shadow_pinmask;
						/* which bits matter in rpc
						   log item pin masks */
	uint			m_cxfstype;	/* mounted shared, etc. */
	lock_t			m_freeze_lock;	/* Lock for m_frozen */
	uint			m_frozen;	/* FS frozen for shutdown or
						 * snapshot */
	sv_t			m_wait_unfreeze;/* waiting to unfreeze */
	atomic_t		m_active_trans; /* number trans frozen */
	struct timer_list	m_sbdirty_timer;/* superblock dirty timer
						 * for nfs refcache */
} xfs_mount_t;

/*
 * Flags for m_flags.
 */
#define XFS_MOUNT_WSYNC		0x00000001	/* for nfs - all metadata ops
						   must be synchronous except
						   for space allocations */
#if XFS_BIG_FILESYSTEMS
#define XFS_MOUNT_INO64		0x00000002
#endif
#define XFS_MOUNT_ROOTQCHECK	0x00000004
			     /* 0x00000008	-- currently unused */
#define XFS_MOUNT_FS_SHUTDOWN	0x00000010	/* atomic stop of all filesystem
						   operations, typically for
						   disk errors in metadata */
#define XFS_MOUNT_NOATIME	0x00000020	/* don't modify inode access
						   times on reads */
#define XFS_MOUNT_RETERR	0x00000040	/* return alignment errors to
						   user */
#define XFS_MOUNT_NOALIGN	0x00000080	/* turn off stripe alignment
						   allocations */
			     /* 0x00000100	-- currently unused */
#define XFS_MOUNT_REGISTERED	0x00000200	/* registered with cxfs master
						   cell logic */
#define XFS_MOUNT_NORECOVERY	0x00000400	/* no recovery - dirty fs */
#define XFS_MOUNT_SHARED	0x00000800	/* shared mount */
#define XFS_MOUNT_DFLT_IOSIZE	0x00001000	/* set default i/o size */
#define XFS_MOUNT_OSYNCISOSYNC	0x00002000	/* o_sync is REALLY o_sync */
						/* osyncisdsync is now default*/
#define XFS_MOUNT_NOUUID	0x00004000	/* ignore uuid during mount */
#define XFS_MOUNT_32BITINODES	0x00008000	/* do not create inodes above
						 * 32 bits in size */
#define XFS_MOUNT_NOLOGFLUSH	0x00010000

/*
 * Flags for m_cxfstype
 */
#define XFS_CXFS_NOT		0x00000001	/* local mount */
#define XFS_CXFS_SERVER		0x00000002	/* we're the CXFS server */
#define XFS_CXFS_CLIENT		0x00000004	/* We're a CXFS client */
#define XFS_CXFS_REC_ENABLED	0x00000008	/* recovery is enabled */

#define XFS_FORCED_SHUTDOWN(mp) ((mp)->m_flags & XFS_MOUNT_FS_SHUTDOWN)

/*
 * Default minimum read and write sizes.
 */
#define XFS_READIO_LOG_LARGE	12
#define XFS_WRITEIO_LOG_LARGE	12
/*
 * Default allocation size
 */
#define XFS_WRITE_IO_LOG	16

/*
 * Max and min values for UIO and mount-option defined I/O sizes;
 * min value can't be less than a page.	 Currently unused.
 */
#define XFS_MAX_IO_LOG		16	/* 64K */
#define XFS_MIN_IO_LOG		PAGE_SHIFT

/*
 * Synchronous read and write sizes.  This should be
 * better for NFSv2 wsync filesystems.
 */
#define XFS_WSYNC_READIO_LOG	15	/* 32K */
#define XFS_WSYNC_WRITEIO_LOG	14	/* 16K */

#define xfs_force_shutdown(m,f)	VFS_FORCE_SHUTDOWN(XFS_MTOVFS(m),f)
/*
 * Flags sent to xfs_force_shutdown.
 */
#define XFS_METADATA_IO_ERROR	0x1
#define XFS_LOG_IO_ERROR	0x2
#define XFS_FORCE_UMOUNT	0x4
#define XFS_CORRUPT_INCORE	0x8	/* corrupt in-memory data structures */
#define XFS_SHUTDOWN_REMOTE_REQ 0x10	/* shutdown came from remote cell */

/*
 * xflags for xfs_syncsub
 */
#define XFS_XSYNC_RELOC		0x01

/*
 * Flags for xfs_mountfs
 */
#define XFS_MFSI_SECOND		0x01	/* Is a cxfs secondary mount -- skip */
					/* stuff which should only be done */
					/* once. */
#define XFS_MFSI_CLIENT		0x02	/* Is a client -- skip lots of stuff */
#define XFS_MFSI_NOUNLINK	0x08	/* Skip unlinked inode processing in */
					/* log recovery */

/*
 * Macros for getting from mount to vfs and back.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_MTOVFS)
struct vfs *xfs_mtovfs(xfs_mount_t *mp);
#define XFS_MTOVFS(mp)		xfs_mtovfs(mp)
#else
#define XFS_MTOVFS(mp)		(bhvtovfs(&(mp)->m_bhv))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BHVTOM)
xfs_mount_t *xfs_bhvtom(bhv_desc_t *bdp);
#define XFS_BHVTOM(bdp) xfs_bhvtom(bdp)
#else
#define XFS_BHVTOM(bdp)		((xfs_mount_t *)BHV_PDATA(bdp))
#endif


/*
 * Moved here from xfs_ag.h to avoid reordering header files
 */

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DADDR_TO_AGNO)
xfs_agnumber_t xfs_daddr_to_agno(struct xfs_mount *mp, xfs_daddr_t d);
#define XFS_DADDR_TO_AGNO(mp,d)		xfs_daddr_to_agno(mp,d)
#else

static inline xfs_agnumber_t XFS_DADDR_TO_AGNO(xfs_mount_t *mp, xfs_daddr_t d)
{
	d = XFS_BB_TO_FSBT(mp, d);
	do_div(d, mp->m_sb.sb_agblocks);
	return (xfs_agnumber_t) d;
}

#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DADDR_TO_AGBNO)
xfs_agblock_t xfs_daddr_to_agbno(struct xfs_mount *mp, xfs_daddr_t d);
#define XFS_DADDR_TO_AGBNO(mp,d)	xfs_daddr_to_agbno(mp,d)
#else

static inline xfs_agblock_t XFS_DADDR_TO_AGBNO(xfs_mount_t *mp, xfs_daddr_t d)
{
	d = XFS_BB_TO_FSBT(mp, d);
	return (xfs_agblock_t) do_div(d, mp->m_sb.sb_agblocks);
}

#endif

/*
 * This structure is for use by the xfs_mod_incore_sb_batch() routine.
 */
typedef struct xfs_mod_sb {
	xfs_sb_field_t	msb_field;	/* Field to modify, see below */
	int		msb_delta;	/* change to make to the specified field */
} xfs_mod_sb_t;

#define XFS_MOUNT_ILOCK(mp)	mutex_lock(&((mp)->m_ilock), PINOD)
#define XFS_MOUNT_IUNLOCK(mp)	mutex_unlock(&((mp)->m_ilock))
#define XFS_SB_LOCK(mp)		mutex_spinlock(&(mp)->m_sb_lock)
#define XFS_SB_UNLOCK(mp,s)	mutex_spinunlock(&(mp)->m_sb_lock,(s))

void		xfs_mod_sb(xfs_trans_t *, __int64_t);
xfs_mount_t	*xfs_mount_init(void);
void		xfs_mount_free(xfs_mount_t *mp, int remove_bhv);
int		xfs_mountfs(struct vfs *, xfs_mount_t *mp, dev_t, int);

int		xfs_unmountfs(xfs_mount_t *, struct cred *);
void		xfs_unmountfs_close(xfs_mount_t *, struct cred *);
int		xfs_unmountfs_writesb(xfs_mount_t *);
int		xfs_unmount_flush(xfs_mount_t *, int);
int		xfs_mod_incore_sb(xfs_mount_t *, xfs_sb_field_t, int, int);
int		xfs_mod_incore_sb_batch(xfs_mount_t *, xfs_mod_sb_t *, uint, int);
int		xfs_readsb(xfs_mount_t *mp);
struct xfs_buf	*xfs_getsb(xfs_mount_t *, int);
void		xfs_freesb(xfs_mount_t *);
void		xfs_do_force_shutdown(bhv_desc_t *, int, char *, int);
int		xfs_syncsub(xfs_mount_t *, int, int, int *);
void		xfs_initialize_perag(xfs_mount_t *, int);
void		xfs_xlatesb(void *, struct xfs_sb *, int, xfs_arch_t, __int64_t);

int		xfs_blkdev_get(const char *, struct block_device **);
void		xfs_blkdev_put(struct block_device *);
struct xfs_buftarg *xfs_alloc_buftarg(struct block_device *);
void		xfs_free_buftarg(struct xfs_buftarg *);

/*
 * Flags for freeze operations.
 */
#define XFS_FREEZE_WRITE	1
#define XFS_FREEZE_TRANS	2

void		xfs_start_freeze(xfs_mount_t *, int);
void		xfs_finish_freeze(xfs_mount_t *);
void		xfs_check_frozen(xfs_mount_t *, bhv_desc_t *, int);

extern	struct vfsops xfs_vfsops;

#endif	/* __KERNEL__ */

#endif	/* __XFS_MOUNT_H__ */

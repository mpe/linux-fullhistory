/*
 *  linux/include/linux/ufs_fs.h
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_fs.h,v 1.1 1996/04/21 14:45:11 davem Exp $
 *
 */

#ifndef __LINUX_UFS_FS_H
#define __LINUX_UFS_FS_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/stat.h>

#define UFS_BBLOCK 0
#define UFS_BBSIZE 8192
#define UFS_SBLOCK 8192
#define UFS_SBSIZE 8192

#define UFS_MAGIC 0x00011954

#define UFS_FSIZE 1024
#define UFS_BSIZE 8192

#define UFS_NDADDR 12
#define UFS_NINDIR 3

#define UFS_ROOTINO 2

#define UFS_USEEFT  ((__u16)65535)

#define UFS_FSOK      0x7c269d38
#define UFS_FSACTIVE  ((char)0x00)
#define UFS_FSCLEAN   ((char)0x01)
#define UFS_FSSTABLE  ((char)0x02)
#define UFS_FSBAD     ((char)0xff)

/* Flags for ufs_sb_info */
#define UFS_DEBUG       0x00000001
#define UFS_DEBUG_INODE 0x00000002
#define UFS_DEBUG_NAMEI 0x00000004
#define UFS_DEBUG_LINKS 0x00000008


/* Test if the inode number is valid. */
#define ufs_ino_ok(inode)  ((inode->i_ino < 2) &&  \
	                    (inode->i_ino > (inode->i_sb->u.ufs_sb.s_ncg * inode->i_sb->u.ufs_sb.s_ipg - 1)))

/* Convert (sb,cg) to the first physical block number for that cg. */
#define ufs_cgstart(sb, cg)   \
  (((sb)->u.ufs_sb.s_fpg * (cg)) + (sb)->u.ufs_sb.s_cgoffset * ((cg) & ~((sb)->u.ufs_sb.s_cgmask)))

/* Convert (sb,cg) to the first phys. block number for inodes in that cg. */
#define ufs_cgimin(sb, cg) (ufs_cgstart((sb), (cg)) + (sb)->u.ufs_sb.s_iblkno)
#define ufs_cgdmin(sb, cg) (ufs_cgstart((sb), (cg)) + (sb)->u.ufs_sb.s_dblkno)

/* Convert an inode number to a cg number. */
/* XXX - this can be optimized if s_ipg is a power of 2. */
#define ufs_ino2cg(inode)  ((inode)->i_ino/(inode)->i_sb->u.ufs_sb.s_ipg)

#define	UFS_MAXNAMLEN 255

struct ufs_direct {
	__u32  d_ino;			/* inode number of this entry */
	__u16  d_reclen;		/* length of this entry */
	__u16  d_namlen;		/* actual length of d_name */
	char   d_name[UFS_MAXNAMLEN + 1];	/* file name */
};

#define MAXMNTLEN 512
#define MAXCSBUFS 32

struct ufs_csum {
	__u32	cs_ndir;	/* number of directories */
	__u32	cs_nbfree;	/* number of free blocks */
	__u32	cs_nifree;	/* number of free inodes */
	__u32	cs_nffree;	/* number of free frags */
};

typedef struct _ufsquad {
	__u32 val[2];
} ufsquad;

/*
 * This is the actual superblock, as it is laid out on the disk.
 */
struct ufs_superblock {
	__u32	fs_link;	/* UNUSED */
	__u32	fs_rlink;	/* UNUSED */
	__u32	fs_sblkno;
	__u32	fs_cblkno;
	__u32	fs_iblkno;
	__u32	fs_dblkno;
	__u32	fs_cgoffset;
	__u32	fs_cgmask;
	time_t	fs_time;	/* XXX - check type */
	__u32	fs_size;
	__u32	fs_dsize;
	__u32	fs_ncg;
	__u32	fs_bsize;
	__u32	fs_fsize;
	__u32	fs_frag;
	__u32	fs_minfree;
	__u32	fs_rotdelay;
	__u32	fs_rps;
	__u32	fs_bmask;
	__u32	fs_fmask;
	__u32	fs_bshift;
	__u32	fs_fshift;
	__u32	fs_maxcontig;
	__u32	fs_maxbpg;
	__u32	fs_fragshift;
	__u32	fs_fsbtodb;
	__u32	fs_sbsize;
	__u32	fs_csmask;
	__u32	fs_csshift;
	__u32	fs_nindir;
	__u32	fs_inopb;
	__u32	fs_nspf;
	__u32	fs_optim;
	__u32	fs_XXX1;
	__u32	fs_interleave;
	__u32	fs_trackskew;
	__u32	fs_id[2];
	__u32	fs_csaddr;
	__u32	fs_cssize;
	__u32	fs_cgsize;
	__u32	fs_ntrak;
	__u32	fs_nsect;
	__u32	fs_spc;
	__u32	fs_ncyl;
	__u32	fs_cpg;
	__u32	fs_ipg;
	__u32	fs_fpg;
	struct ufs_csum fs_cstotal;
	__u8	fs_fmod;
	__u8	fs_clean;
	__u8	fs_ronly;
	__u8	fs_flags;
	__u8	fs_fsmnt[MAXMNTLEN];
	__u32	fs_cgrotor;
	struct ufs_csum * fs_csp[MAXCSBUFS];
	__u32	fs_cpc;
	__u16	fs_opostbl[16][8];	/* old rotation block list head */
	__s32	fs_sparecon[55];	/* reserved for future constants */
	__s32	fs_state;		/* file system state time stamp */
	ufsquad	fs_qbmask;		/* ~usb_bmask - for use with __s64 size */
	ufsquad	fs_qfmask;		/* ~usb_fmask - for use with __s64 size */
	__s32	fs_postblformat;	/* format of positional layout tables */
	__s32	fs_nrpos;		/* number of rotational positions */
	__s32	fs_postbloff;		/* (__s16) rotation block list head */
	__s32	fs_rotbloff;		/* (__u8) blocks for each rotation */
	__s32	fs_magic;		/* magic number */
	__u8	fs_space[1];		/* list of blocks for each rotation */

};

/*
 * structure of an on-disk inode
 */
struct ufs_inode {
	__u16	ui_mode;		/*  0x0 */
	__u16	ui_nlink;		/*  0x2 */
	__u16	ui_suid;		/*  0x4 */
	__u16	ui_sgid;		/*  0x6 */
	ufsquad	ui_size;		/*  0x8 */  /* XXX - should be __u64 */
	struct timeval ui_atime;	/* 0x10 */
	struct timeval ui_mtime;	/* 0x18 */
	struct timeval ui_ctime;	/* 0x20 */
	__u32	ui_db[UFS_NDADDR];		/* 0x28 data blocks */
	__u32	ui_ib[UFS_NINDIR];		/* 0x58 indirect blocks */
	__u32	ui_flags;		/* 0x64 unused */
	__u32	ui_blocks;		/* 0x68 blocks in use */
	__u32	ui_gen;			/* 0x6c generation number XXX - what is this? */
	__u32	ui_shadow;		/* 0x70 shadow inode XXX - what is this?*/
	__u32	ui_uid;			/* 0x74 long EFT version of uid */
	__u32	ui_gid;			/* 0x78 long EFT version of gid */
	__u32	ui_oeftflag;		/* 0x7c reserved */
};

extern int init_ufs_fs(void);

#endif /* __LINUX_UFS_FS_H */
/*
 * Local Variables: ***
 * c-indent-level: 8 ***
 * c-continued-statement-offset: 8 ***
 * c-brace-offset: -8 ***
 * c-argdecl-indent: 0 ***
 * c-label-offset: -8 ***
 * End: ***
 */

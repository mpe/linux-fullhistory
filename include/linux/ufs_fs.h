/*
 *  linux/include/linux/ufs_fs.h
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * Clean swab support by Fare <rideau@ens.fr>
 * just hope no one is using NNUUXXI on __?64 structure elements
 * 64-bit clean thanks to Maciej W. Rozycki <macro@ds2.pg.gda.pl>
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 *
 * NeXTstep support added on February 5th 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk>.
 */

#ifndef __LINUX_UFS_FS_H
#define __LINUX_UFS_FS_H

#undef UFS_HEAVY_DEBUG
/*#define UFS_HEAVY_DEBUG 1*/
/* Uncomment the line above when hacking ufs code */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/stat.h>

#define UFS_BBLOCK 0
#define UFS_BBSIZE 8192
#define UFS_SBLOCK 8192
#define UFS_SBSIZE 8192

#define UFS_MAGIC 0x00011954
#define UFS_CIGAM 0x54190100 /* byteswapped MAGIC */

#define UFS_FSIZE 1024
#define UFS_BSIZE 8192

#define UFS_NDADDR 12
#define UFS_NINDIR 3

#define UFS_IND_BLOCK	(UFS_NDADDR + 0)
#define UFS_DIND_BLOCK	(UFS_NDADDR + 1)
#define UFS_TIND_BLOCK	(UFS_NDADDR + 2)

#define UFS_ROOTINO 2

#define UFS_USEEFT  ((__u16)65535)

#define UFS_FSOK      0x7c269d38
#define UFS_FSACTIVE  ((char)0x00)
#define UFS_FSCLEAN   ((char)0x01)
#define UFS_FSSTABLE  ((char)0x02)
#define UFS_FSOSF1    ((char)0x03)	/* is this correct for DEC OSF/1? */
#define UFS_FSBAD     ((char)0xff)

/* From here to next blank line, s_flags for ufs_sb_info */
/* endianness */
#define UFS_BYTESEX		0x00000001	/* mask; leave room to 0xF */
#define UFS_LITTLE_ENDIAN	0x00000000
#define UFS_BIG_ENDIAN		0x00000001
/* directory entry encoding */
#define UFS_DE_MASK		0x00000010	/* mask for the following */
#define UFS_DE_OLD		0x00000000
#define UFS_DE_44BSD		0x00000010
/* uid encoding */
#define UFS_UID_MASK		0x00000060	/* mask for the following */
#define UFS_UID_OLD		0x00000000
#define UFS_UID_44BSD		0x00000020
#define UFS_UID_EFT		0x00000040
/* superblock state encoding */
#define UFS_ST_MASK		0x00000700	/* mask for the following */
#define UFS_ST_OLD		0x00000000
#define UFS_ST_44BSD		0x00000100
#define UFS_ST_SUN		0x00000200
#define UFS_ST_NEXT		0x00000400
/* filesystem flavors (combo of features) */
#define UFS_FEATURES		0x00FFFFF0	/* room for extension */
#define UFS_VANILLA		0x00000000
#define UFS_OLD			0x00000000	/* 4.2BSD */
#define UFS_44BSD		0x00000130
#define UFS_HURD		0x00000130
#define UFS_SUN			0x00000200
#define UFS_NEXT		0x00000400
/* we preserve distinction in flavor identification even without difference,
 * because yet-to-be-supported features may introduce difference in the future
 */
/* last but not least, debug flags */
#define UFS_DEBUG       	0x01000000
#define UFS_DEBUG_INODE 	0x02000000
#define UFS_DEBUG_NAMEI 	0x04000000
#define UFS_DEBUG_LINKS 	0x08000000

#ifdef UFS_HEAVY_DEBUG
#  define UFS_DEBUG_INITIAL UFS_DEBUG
#else
#  define UFS_DEBUG_INITIAL 0
#endif

/* fs_inodefmt options */
#define UFS_42INODEFMT	-1
#define UFS_44INODEFMT	2

#define UFS_ADDR_PER_BLOCK(sb)		((sb)->u.ufs_sb.s_bsize >> 2)
#define UFS_ADDR_PER_BLOCK_BITS(sb)	((sb)->u.ufs_sb.s_bshift - 2)

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

/* current filesystem state; method depends on flags */
#define UFS_STATE(usb) \
		( ((flags&UFS_ST_MASK) == UFS_ST_OLD) \
                  ? (usb)->fs_u.fs_sun.fs_state /* old normal way */ \
                  : (usb)->fs_u.fs_44.fs_state /* 4.4BSD way */ )

#define	UFS_MAXNAMLEN 255

#define ufs_lbn(sb, block)		((block) >> (sb)->u.ufs_sb.s_lshift)
#define ufs_boff(sb, block)		((block) & ~((sb)->u.ufs_sb.s_lmask))
#define ufs_dbn(sb, block, boff)	((block) + ufs_boff((sb), (boff)))

struct ufs_timeval {
	__s32	tv_sec;
	__s32	tv_usec;
};

struct ufs_direct {
	__u32  d_ino;			/* inode number of this entry */
	__u16  d_reclen;		/* length of this entry */
	union {
		__u16	d_namlen;		/* actual length of d_name */
		struct {
			__u8	d_type;		/* file type */
			__u8	d_namlen;	/* length of string in d_name */
		} d_44;
	} d_u;
	__u8	d_name[UFS_MAXNAMLEN + 1];	/* file name */
};

#define MAXMNTLEN 512
#define MAXCSBUFS 32

struct ufs_csum {
	__u32	cs_ndir;	/* number of directories */
	__u32	cs_nbfree;	/* number of free blocks */
	__u32	cs_nifree;	/* number of free inodes */
	__u32	cs_nffree;	/* number of free frags */
};

/*
 * This is the actual superblock, as it is laid out on the disk.
 */
struct ufs_superblock {
	__u32	fs_link;	/* UNUSED */
	__u32	fs_rlink;	/* UNUSED */
	__u32	fs_sblkno;	/* addr of super-block in filesys */
	__u32	fs_cblkno;	/* offset of cyl-block in filesys */
	__u32	fs_iblkno;	/* offset of inode-blocks in filesys */
	__u32	fs_dblkno;	/* offset of first data after cg */
	__u32	fs_cgoffset;	/* cylinder group offset in cylinder */
	__u32	fs_cgmask;	/* used to calc mod fs_ntrak */
	__u32	fs_time;	/* last time written -- time_t */
	__u32	fs_size;	/* number of blocks in fs */
	__u32	fs_dsize;	/* number of data blocks in fs */
	__u32	fs_ncg;		/* number of cylinder groups */
	__u32	fs_bsize;	/* size of basic blocks in fs */
	__u32	fs_fsize;	/* size of frag blocks in fs */
	__u32	fs_frag;	/* number of frags in a block in fs */
/* these are configuration parameters */
	__u32	fs_minfree;	/* minimum percentage of free blocks */
	__u32	fs_rotdelay;	/* num of ms for optimal next block */
	__u32	fs_rps;		/* disk revolutions per second */
/* these fields can be computed from the others */
	__u32	fs_bmask;	/* ``blkoff'' calc of blk offsets */
	__u32	fs_fmask;	/* ``fragoff'' calc of frag offsets */
	__u32	fs_bshift;	/* ``lblkno'' calc of logical blkno */
	__u32	fs_fshift;	/* ``numfrags'' calc number of frags */
/* these are configuration parameters */
	__u32	fs_maxcontig;	/* max number of contiguous blks */
	__u32	fs_maxbpg;	/* max number of blks per cyl group */
/* these fields can be computed from the others */
	__u32	fs_fragshift;	/* block to frag shift */
	__u32	fs_fsbtodb;	/* fsbtodb and dbtofsb shift constant */
	__u32	fs_sbsize;	/* actual size of super block */
	__u32	fs_csmask;	/* csum block offset */
	__u32	fs_csshift;	/* csum block number */
	__u32	fs_nindir;	/* value of NINDIR */
	__u32	fs_inopb;	/* value of INOPB */
	__u32	fs_nspf;	/* value of NSPF */
/* yet another configuration parameter */
	__u32	fs_optim;	/* optimization preference, see below */
/* these fields are derived from the hardware */
	__u32	fs_npsect;	/* # sectors/track including spares */
	__u32	fs_interleave;	/* hardware sector interleave */
	__u32	fs_trackskew;	/* sector 0 skew, per track */
/* a unique id for this filesystem (currently unused and unmaintained) */
/* In 4.3 Tahoe this space is used by fs_headswitch and fs_trkseek */
/* Neither of those fields is used in the Tahoe code right now but */
/* there could be problems if they are.                            */
	__u32	fs_id[2];	/* file system id */
/* sizes determined by number of cylinder groups and their sizes */
	__u32	fs_csaddr;	/* blk addr of cyl grp summary area */
	__u32	fs_cssize;	/* size of cyl grp summary area */
	__u32	fs_cgsize;	/* cylinder group size */
/* these fields are derived from the hardware */
	__u32	fs_ntrak;	/* tracks per cylinder */
	__u32	fs_nsect;	/* sectors per track */
	__u32	fs_spc;		/* sectors per cylinder */
/* this comes from the disk driver partitioning */
	__u32	fs_ncyl;	/* cylinders in file system */
/* these fields can be computed from the others */
	__u32	fs_cpg;		/* cylinders per group */
	__u32	fs_ipg;		/* inodes per group */
	__u32	fs_fpg;		/* blocks per group * fs_frag */
/* this data must be re-computed after crashes */
	struct ufs_csum fs_cstotal;	/* cylinder summary information */
/* these fields are cleared at mount time */
	__u8	fs_fmod;	/* super block modified flag */
	__u8	fs_clean;	/* file system is clean flag */
	__u8	fs_ronly;	/* mounted read-only flag */
	__u8	fs_flags;	/* currently unused flag */
	__u8	fs_fsmnt[MAXMNTLEN];	/* name mounted on */
/* these fields retain the current block allocation info */
	__u32	fs_cgrotor;	/* last cg searched */
	__u32	fs_csp[MAXCSBUFS];	/* list of fs_cs info buffers */
	__u32	fs_cpc;		/* cyl per cycle in postbl */
	__u16	fs_opostbl[16][8];	/* old rotation block list head */	
	union {
		struct {
			__s32	fs_sparecon[55];/* reserved for future constants */
			__s32	fs_state;	/* file system state time stamp */
			__u32	fs_qbmask[2];	/* ~usb_bmask */
			__u32	fs_qfmask[2];	/* ~usb_fmask */
		} fs_sun;
		struct {
			__s32	fs_sparecon[50];/* reserved for future constants */
			__s32	fs_contigsumsize;/* size of cluster summary array */
			__s32	fs_maxsymlinklen;/* max length of an internal symlink */
			__s32	fs_inodefmt;	/* format of on-disk inodes */
			__u32	fs_maxfilesize[2];/* max representable file size */
			__u32	fs_qbmask[2];	/* ~usb_bmask */
			__u32	fs_qfmask[2];	/* ~usb_fmask */
			__s32	fs_state;	/* file system state time stamp */
		} fs_44;
	} fs_u;
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
	union {
		struct {
			__u16	suid;	/*  0x4 */
			__u16	sgid;	/*  0x6 */
		} oldids;
		__u32	inumber;	/*  0x4 lsf: inode number */
		__u32	author;		/*  0x4 GNU HURD: author */
	} ui_u1;
	__u64	ui_size;		/*  0x8 */
	struct ufs_timeval ui_atime;	/* 0x10 access */
	struct ufs_timeval ui_mtime;	/* 0x18 modification */
	struct ufs_timeval ui_ctime;	/* 0x20 creation */
	union {
		struct {
			__u32	ui_db[UFS_NDADDR];/* 0x28 data blocks */
			__u32	ui_ib[UFS_NINDIR];/* 0x58 indirect blocks */
		} ui_addr;
		__u8	ui_symlink[4*(UFS_NDADDR+UFS_NINDIR)];/* 0x28 fast symlink */
	} ui_u2;
	__u32	ui_flags;		/* 0x64 immutable, append-only... */
	__u32	ui_blocks;		/* 0x68 blocks in use */
	__u32	ui_gen;			/* 0x6c like ext2 i_version, for NFS support */
	union {
		struct {
			__u32	ui_shadow;/* 0x70 shadow inode with security data */
			__u32	ui_uid;	/* 0x74 long EFT version of uid */
			__u32	ui_gid;	/* 0x78 long EFT version of gid */
			__u32	ui_oeftflag;/* 0x7c reserved */
		} ui_sun;
		struct {
			__u32	ui_uid;	/* 0x70 File owner */
			__u32	ui_gid;	/* 0x74 File group */
			__s32	ui_spare[2];/* 0x78 reserved */
		} ui_44;
		struct {
			__u32	ui_uid;	/* 0x70 */
			__u32	ui_gid;	/* 0x74 */
			__u16	ui_modeh;/* 0x78 mode high bits */
			__u16	ui_spare;/* 0x7A unused */
			__u32	ui_trans;/* 0x7c filesystem translator */
		} ui_hurd;
	} ui_u3;
};

/* FreeBSD has these in sys/stat.h */
/* ui_flags that can be set by a file owner */
#define UFS_UF_SETTABLE   0x0000ffff
#define UFS_UF_NODUMP     0x00000001  /* do not dump */
#define UFS_UF_IMMUTABLE  0x00000002  /* immutable (can't "change") */
#define UFS_UF_APPEND     0x00000004  /* append-only */
#define UFS_UF_OPAQUE     0x00000008  /* directory is opaque (unionfs) */
#define UFS_UF_NOUNLINK   0x00000010  /* can't be removed or renamed */
/* ui_flags that only root can set */
#define UFS_SF_SETTABLE   0xffff0000
#define UFS_SF_ARCHIVED   0x00010000  /* archived */
#define UFS_SF_IMMUTABLE  0x00020000  /* immutable (can't "change") */
#define UFS_SF_APPEND     0x00040000  /* append-only */
#define UFS_SF_NOUNLINK   0x00100000  /* can't be removed or renamed */
    
 
#ifdef __KERNEL__
/*
 * Function prototypes
 */

/* ufs_inode.c */
extern int ufs_bmap (struct inode *, int);
extern void ufs_read_inode(struct inode * inode);
extern void ufs_put_inode(struct inode * inode);

extern void ufs_print_inode (struct inode *);

/* ufs_namei.c */
extern int ufs_lookup (struct inode *, struct dentry *);

/* ufs_super.c */
extern void ufs_warning (struct super_block *, const char *, const char *, ...)
        __attribute__ ((format (printf, 3, 4)));
extern int init_ufs_fs(void);

/*
 * Inodes and files operations
 */

/* ufs_dir.c */
extern struct inode_operations ufs_dir_inode_operations;
extern struct file_operations ufs_dir_operations;

/* ufs_file.c */
extern struct inode_operations ufs_file_inode_operations;
extern struct file_operations ufs_file_operations;

/* ufs_symlink.c */
extern struct inode_operations ufs_symlink_inode_operations;
extern struct file_operations ufs_symlink_operations;

#endif	/* __KERNEL__ */

#endif /* __LINUX_UFS_FS_H */

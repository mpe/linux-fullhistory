/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *   This product includes software developed by the University of
 *   California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Version: $Id: quota.h,v 1.8 1995/03/11 11:43:07 mvw Exp mvw $
 */

#ifndef _LINUX_QUOTA_
#define _LINUX_QUOTA_

#include <linux/errno.h>

/*
 * Convert diskblocks to blocks and the other way around.
 * currently only to fool the BSD source. :-)
 */
#define dbtob(num) (num << 10)
#define btodb(num) (num >> 10)

/*
 * Convert count of filesystem blocks to diskquota blocks, meant
 * for filesystems where i_blksize != BLOCK_SIZE
 */
#define fs_to_dq_blocks(num, blksize) (((num) * (blksize)) / BLOCK_SIZE)

/*
 * Definitions for disk quotas imposed on the average user
 * (big brother finally hits Linux).
 *
 * The following constants define the amount of time given a user
 * before the soft limits are treated as hard limits (usually resulting
 * in an allocation failure). The timer is started when the user crosses
 * their soft limit, it is reset when they go below their soft limit.
 */
#define MAX_IQ_TIME  604800	/* (7*24*60*60) 1 week */
#define MAX_DQ_TIME  604800	/* (7*24*60*60) 1 week */

#define MAXQUOTAS 2
#define USRQUOTA  0		/* element used for user quotas */
#define GRPQUOTA  1		/* element used for group quotas */

/*
 * Definitions for the default names of the quotas files.
 */
#define INITQFNAMES { \
   "user",      /* USRQUOTA */ \
   "group",   /* GRPQUOTA */ \
   "undefined", \
};

#define QUOTAFILENAME "quota"
#define QUOTAGROUP "staff"

#define NR_DQHASH 43            /* Just an arbitrary number any suggestions ? */
#define NR_DQUOTS 256           /* Number of quotas active at one time */

/*
 * Command definitions for the 'quotactl' system call.
 * The commands are broken into a main command defined below
 * and a subcommand that is used to convey the type of
 * quota that is being manipulated (see above).
 */
#define SUBCMDMASK  0x00ff
#define SUBCMDSHIFT 8
#define QCMD(cmd, type)  (((cmd) << SUBCMDSHIFT) | ((type) & SUBCMDMASK))

#define Q_QUOTAON  0x0100	/* enable quotas */
#define Q_QUOTAOFF 0x0200	/* disable quotas */
#define Q_GETQUOTA 0x0300	/* get limits and usage */
#define Q_SETQUOTA 0x0400	/* set limits and usage */
#define Q_SETUSE   0x0500	/* set usage */
#define Q_SYNC     0x0600	/* sync disk copy of a filesystems quotas */
#define Q_SETQLIM  0x0700	/* set limits */
#define Q_GETSTATS 0x0800	/* get collected stats */

/*
 * The following structure defines the format of the disk quota file
 * (as it appears on disk) - the file is an array of these structures
 * indexed by user or group number.
 */
struct dqblk {
    __u32 dqb_bhardlimit;	/* absolute limit on disk blks alloc */
    __u32 dqb_bsoftlimit;	/* preferred limit on disk blks */
    __u32 dqb_curblocks;	/* current block count */
    __u32 dqb_ihardlimit;	/* maximum # allocated inodes */
    __u32 dqb_isoftlimit;	/* preferred inode limit */
    __u32 dqb_curinodes;	/* current # allocated inodes */
    time_t dqb_btime;		/* time limit for excessive disk use */
    time_t dqb_itime;		/* time limit for excessive files */
};

/*
 * Shorthand notation.
 */
#define	dq_bhardlimit	dq_dqb.dqb_bhardlimit
#define	dq_bsoftlimit	dq_dqb.dqb_bsoftlimit
#define	dq_curblocks	dq_dqb.dqb_curblocks
#define	dq_ihardlimit	dq_dqb.dqb_ihardlimit
#define	dq_isoftlimit	dq_dqb.dqb_isoftlimit
#define	dq_curinodes	dq_dqb.dqb_curinodes
#define	dq_btime	dq_dqb.dqb_btime
#define	dq_itime	dq_dqb.dqb_itime

#define dqoff(UID)      ((off_t)((UID) * sizeof (struct dqblk)))

struct dqstats {
   __u32 lookups;
   __u32 drops;
   __u32 reads;
   __u32 writes;
   __u32 cache_hits;
   __u32 pages_allocated;
   __u32 allocated_dquots;
   __u32 free_dquots;
   __u32 syncs;
};

#ifdef __KERNEL__

#include <linux/mount.h>

/*
 * Maximum length of a message generated in the quota system,
 * that needs to be kicked onto the tty.
 */
#define MAX_QUOTA_MESSAGE 75

#define DQ_LOCKED     0x01	/* locked for update */
#define DQ_WANT       0x02	/* wanted for update */
#define DQ_MOD        0x04	/* dquot modified since read */
#define DQ_BLKS       0x10	/* uid/gid has been warned about blk limit */
#define DQ_INODES     0x20	/* uid/gid has been warned about inode limit */
#define DQ_FAKE       0x40	/* no limits only usage */

struct dquot {
   unsigned int dq_id;		/* id this applies to (uid, gid) */
   short dq_type;		/* type of quota */
   kdev_t dq_dev;                /* Device this applies to */
   short dq_flags;		/* see DQ_* */
   short dq_count;		/* reference count */
   struct vfsmount *dq_mnt;     /* vfsmountpoint this applies to */
   struct dqblk dq_dqb;         /* diskquota usage */
   struct wait_queue *dq_wait;	/* pointer to waitqueue */
   struct dquot *dq_prev;	/* pointer to prev dquot */
   struct dquot *dq_next;	/* pointer to next dquot */
   struct dquot *dq_hash_prev;	/* pointer to prev dquot */
   struct dquot *dq_hash_next;	/* pointer to next dquot */
};

#define NODQUOT (struct dquot *)NULL

/*
 * Flags used for set_dqblk.
 */
#define QUOTA_SYSCALL     0x01
#define SET_QUOTA         0x02
#define SET_USE           0x04
#define SET_QLIMIT        0x08

#define QUOTA_OK          0
#define NO_QUOTA          1

/*
 * declaration of quota_function calls in kernel.
 */

extern void dquot_initialize(struct inode *inode, short type);
extern void dquot_drop(struct inode *inode);
extern int dquot_alloc_block(const struct inode *inode, unsigned long number);
extern int dquot_alloc_inode(const struct inode *inode, unsigned long number);
extern void dquot_free_block(const struct inode *inode, unsigned long number);
extern void dquot_free_inode(const struct inode *inode, unsigned long number);
extern int dquot_transfer(struct inode *inode, struct iattr *iattr, char direction);

extern void invalidate_dquots(kdev_t dev, short type);
extern int quota_off(kdev_t dev, short type);
extern int sync_dquots(kdev_t dev, short type);

#else

#include <sys/cdefs.h>

__BEGIN_DECLS
int quotactl __P ((int, const char *, int, caddr_t));
__END_DECLS

#endif /* __KERNEL__ */
#endif /* _QUOTA_ */

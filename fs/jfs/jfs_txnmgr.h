/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _H_JFS_TXNMGR
#define _H_JFS_TXNMGR

#include "jfs_logmgr.h"

/*
 * Hide implementation of TxBlock and TxLock
 */
#define tid_to_tblock(tid) (&TxBlock[tid])

#define lid_to_tlock(lid) (&TxLock[lid])

/*
 *	transaction block
 */
typedef struct tblock {
	/*
	 * tblock_t and jbuf_t common area: struct logsyncblk
	 *
	 * the following 5 fields are the same as struct logsyncblk
	 * which is common to tblock and jbuf to form logsynclist
	 */
	u16 xflag;		/* tx commit type */
	u16 flag;		/* tx commit state */
	lid_t dummy;		/* Must keep structures common */
	s32 lsn;		/* recovery lsn */
	struct list_head synclist;	/* logsynclist link */

	/* lock management */
	struct super_block *sb;	/* 4: super block */
	lid_t next;		/* 2: index of first tlock of tid */
	lid_t last;		/* 2: index of last tlock of tid */
	wait_queue_head_t waitor;	/* 4: tids waiting on this tid */

	/* log management */
	u32 logtid;		/* 4: log transaction id */
				/* (32) */

	/* commit management */
	struct tblock *cqnext;	/* 4: commit queue link */
	s32 clsn;		/* 4: commit lsn */
	struct lbuf *bp;	/* 4: */
	s32 pn;			/* 4: commit record log page number */
	s32 eor;		/* 4: commit record eor */
	wait_queue_head_t gcwait;	/* 4: group commit event list:
					 *    ready transactions wait on this
					 *    event for group commit completion.
					 */
	struct inode *ip;	/* 4: inode being created or deleted */
	s32 rsrvd;		/* 4: */
} tblock_t;			/* (64) */

extern struct tblock *TxBlock;	/* transaction block table */

/* commit flags: tblk->xflag */
#define	COMMIT_SYNC	0x0001	/* synchronous commit */
#define	COMMIT_FORCE	0x0002	/* force pageout at end of commit */
#define	COMMIT_FLUSH	0x0004	/* init flush at end of commit */
#define COMMIT_MAP	0x00f0
#define	COMMIT_PMAP	0x0010	/* update pmap */
#define	COMMIT_WMAP	0x0020	/* update wmap */
#define	COMMIT_PWMAP	0x0040	/* update pwmap */
#define	COMMIT_FREE	0x0f00
#define	COMMIT_DELETE	0x0100	/* inode delete */
#define	COMMIT_TRUNCATE	0x0200	/* file truncation */
#define	COMMIT_CREATE	0x0400	/* inode create */
#define	COMMIT_LAZY	0x0800	/* lazy commit */
#define COMMIT_PAGE	0x1000	/* Identifies element as metapage */
#define COMMIT_INODE	0x2000	/* Identifies element as inode */

/* group commit flags tblk->flag: see jfs_logmgr.h */

/*
 *	transaction lock
 */
typedef struct tlock {
	lid_t next;		/* index next lockword on tid locklist
				 *          next lockword on freelist
				 */
	tid_t tid;		/* transaction id holding lock */

	u16 flag;		/* 2: lock control */
	u16 type;		/* 2: log type */

	struct metapage *mp;	/* 4: object page buffer locked */
	struct inode *ip;	/* 4: object */
	/* (16) */

	s16 lock[24];		/* 48: overlay area */
} tlock_t;			/* (64) */

extern struct tlock *TxLock;	/* transaction lock table */

/*
 * tlock flag
 */
/* txLock state */
#define tlckPAGELOCK		0x8000
#define tlckINODELOCK		0x4000
#define tlckLINELOCK		0x2000
#define tlckINLINELOCK		0x1000
/* lmLog state */
#define tlckLOG			0x0800
/* updateMap state */
#define	tlckUPDATEMAP		0x0080
/* freeLock state */
#define tlckFREELOCK		0x0008
#define tlckWRITEPAGE		0x0004
#define tlckFREEPAGE		0x0002

/*
 * tlock type
 */
#define	tlckTYPE		0xfe00
#define	tlckINODE		0x8000
#define	tlckXTREE		0x4000
#define	tlckDTREE		0x2000
#define	tlckMAP			0x1000
#define	tlckEA			0x0800
#define	tlckACL			0x0400
#define	tlckDATA		0x0200
#define	tlckBTROOT		0x0100

#define	tlckOPERATION		0x00ff
#define tlckGROW		0x0001	/* file grow */
#define tlckREMOVE		0x0002	/* file delete */
#define tlckTRUNCATE		0x0004	/* file truncate */
#define tlckRELOCATE		0x0008	/* file/directory relocate */
#define tlckENTRY		0x0001	/* directory insert/delete */
#define tlckEXTEND		0x0002	/* directory extend in-line */
#define tlckSPLIT		0x0010	/* splited page */
#define tlckNEW			0x0020	/* new page from split */
#define tlckFREE		0x0040	/* free page */
#define tlckRELINK		0x0080	/* update sibling pointer */

/*
 *	linelock for lmLog()
 *
 * note: linelock_t and its variations are overlaid
 * at tlock.lock: watch for alignment;
 */
typedef struct {
	u8 offset;		/* 1: */
	u8 length;		/* 1: */
} lv_t;				/* (2) */

#define	TLOCKSHORT	20
#define	TLOCKLONG	28

typedef struct {
	u16 next;		/* 2: next linelock */

	s8 maxcnt;		/* 1: */
	s8 index;		/* 1: */

	u16 flag;		/* 2: */
	u8 type;		/* 1: */
	u8 l2linesize;		/* 1: log2 of linesize */
	/* (8) */

	lv_t lv[20];		/* 40: */
} linelock_t;			/* (48) */

#define dtlock_t	linelock_t
#define itlock_t	linelock_t

typedef struct {
	u16 next;		/* 2: */

	s8 maxcnt;		/* 1: */
	s8 index;		/* 1: */

	u16 flag;		/* 2: */
	u8 type;		/* 1: */
	u8 l2linesize;		/* 1: log2 of linesize */
				/* (8) */

	lv_t header;		/* 2: */
	lv_t lwm;		/* 2: low water mark */
	lv_t hwm;		/* 2: high water mark */
	lv_t twm;		/* 2: */
				/* (16) */

	s32 pxdlock[8];		/* 32: */
} xtlock_t;			/* (48) */


/*
 *	maplock for txUpdateMap()
 *
 * note: maplock_t and its variations are overlaid
 * at tlock.lock/linelock: watch for alignment;
 * N.B. next field may be set by linelock, and should not
 * be modified by maplock;
 * N.B. index of the first pxdlock specifies index of next 
 * free maplock (i.e., number of maplock) in the tlock; 
 */
typedef struct {
	u16 next;		/* 2: */

	u8 maxcnt;		/* 2: */
	u8 index;		/* 2: next free maplock index */

	u16 flag;		/* 2: */
	u8 type;		/* 1: */
	u8 count;		/* 1: number of pxd/xad */
				/* (8) */

	pxd_t pxd;		/* 8: */
} maplock_t;			/* (16): */

/* maplock flag */
#define	mlckALLOC		0x00f0
#define	mlckALLOCXADLIST	0x0080
#define	mlckALLOCPXDLIST	0x0040
#define	mlckALLOCXAD		0x0020
#define	mlckALLOCPXD		0x0010
#define	mlckFREE		0x000f
#define	mlckFREEXADLIST		0x0008
#define	mlckFREEPXDLIST		0x0004
#define	mlckFREEXAD		0x0002
#define	mlckFREEPXD		0x0001

#define	pxdlock_t	maplock_t

typedef struct {
	u16 next;		/* 2: */

	u8 maxcnt;		/* 2: */
	u8 index;		/* 2: */

	u16 flag;		/* 2: */
	u8 type;		/* 1: */
	u8 count;		/* 1: number of pxd/xad */
				/* (8) */

	/*
	 * We need xdlistlock_t to be 64 bits (8 bytes), regardless of
	 * whether void * is 32 or 64 bits
	 */
	union {
		void *_xdlist;	/* pxd/xad list */
		s64 pad;	/* 8: Force 64-bit xdlist size */
	} union64;
} xdlistlock_t;			/* (16): */

#define xdlist union64._xdlist

/*
 *	commit
 *
 * parameter to the commit manager routines
 */
typedef struct commit {
	tid_t tid;		/* 4: tid = index of tblock */
	int flag;		/* 4: flags */
	log_t *log;		/* 4: log */
	struct super_block *sb;	/* 4: superblock */

	int nip;		/* 4: number of entries in iplist */
	struct inode **iplist;	/* 4: list of pointers to inodes */
				/* (32) */

	/* log record descriptor on 64-bit boundary */
	lrd_t lrd;		/* : log record descriptor */
} commit_t;

/*
 * external declarations
 */
extern tlock_t *txLock(tid_t tid, struct inode *ip, struct metapage *mp, int flag);

extern tlock_t *txMaplock(tid_t tid, struct inode *ip, int flag);

extern int txCommit(tid_t tid, int nip, struct inode **iplist, int flag);

extern tid_t txBegin(struct super_block *sb, int flag);

extern void txBeginAnon(struct super_block *sb);

extern void txEnd(tid_t tid);

extern void txAbort(tid_t tid, int dirty);

extern linelock_t *txLinelock(linelock_t * tlock);

extern void txFreeMap(struct inode *ip,
		      maplock_t * maplock, tblock_t * tblk, int maptype);

extern void txEA(tid_t tid, struct inode *ip, dxd_t * oldea, dxd_t * newea);

extern void txFreelock(struct inode *ip);

extern int lmLog(log_t * log, tblock_t * tblk, lrd_t * lrd, tlock_t * tlck);

#endif				/* _H_JFS_TXNMGR */

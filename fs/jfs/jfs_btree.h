/*
 *   Copyright (c) International Business Machines Corp., 2000-2001
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
#ifndef	_H_JFS_BTREE
#define _H_JFS_BTREE

/*
 *	jfs_btree.h: B+-tree
 *
 * JFS B+-tree (dtree and xtree) common definitions
 */

/*
 *	basic btree page - btpage_t
 */
typedef struct {
	s64 next;		/* 8: right sibling bn */
	s64 prev;		/* 8: left sibling bn */

	u8 flag;		/* 1: */
	u8 rsrvd[7];		/* 7: type specific */
	s64 self;		/* 8: self address */

	u8 entry[4064];		/* 4064: */
} btpage_t;			/* (4096) */

/* btpaget_t flag */
#define BT_TYPE		0x07	/* B+-tree index */
#define	BT_ROOT		0x01	/* root page */
#define	BT_LEAF		0x02	/* leaf page */
#define	BT_INTERNAL	0x04	/* internal page */
#define	BT_RIGHTMOST	0x10	/* rightmost page */
#define	BT_LEFTMOST	0x20	/* leftmost page */
#define	BT_SWAPPED	0x80	/* used by fsck for endian swapping */

/* btorder (in inode) */
#define	BT_RANDOM		0x0000
#define	BT_SEQUENTIAL		0x0001
#define	BT_LOOKUP		0x0010
#define	BT_INSERT		0x0020
#define	BT_DELETE		0x0040

/*
 *	btree page buffer cache access
 */
#define BT_IS_ROOT(MP) (((MP)->xflag & COMMIT_PAGE) == 0)

/* get page from buffer page */
#define BT_PAGE(IP, MP, TYPE, ROOT)\
	(BT_IS_ROOT(MP) ? (TYPE *)&JFS_IP(IP)->ROOT : (TYPE *)(MP)->data)

/* get the page buffer and the page for specified block address */
#define BT_GETPAGE(IP, BN, MP, TYPE, SIZE, P, RC, ROOT)\
{\
	if ((BN) == 0)\
	{\
		MP = (metapage_t *)&JFS_IP(IP)->bxflag;\
		P = (TYPE *)&JFS_IP(IP)->ROOT;\
		RC = 0;\
		jEVENT(0,("%d BT_GETPAGE returning root\n", __LINE__));\
	}\
	else\
	{\
		jEVENT(0,("%d BT_GETPAGE reading block %d\n", __LINE__,\
			 (int)BN));\
		MP = read_metapage((IP), BN, SIZE, 1);\
		if (MP) {\
			RC = 0;\
			P = (MP)->data;\
		} else {\
			P = NULL;\
			jERROR(1,("bread failed!\n"));\
			RC = EIO;\
		}\
	}\
}

#define BT_MARK_DIRTY(MP, IP)\
{\
	if (BT_IS_ROOT(MP))\
		mark_inode_dirty(IP);\
	else\
		mark_metapage_dirty(MP);\
}

/* put the page buffer */
#define BT_PUTPAGE(MP)\
{\
	if (! BT_IS_ROOT(MP)) \
		release_metapage(MP); \
}


/*
 *	btree traversal stack
 *
 * record the path traversed during the search;
 * top frame record the leaf page/entry selected.
 */
#define	MAXTREEHEIGHT		8
typedef struct btframe {	/* stack frame */
	s64 bn;			/* 8: */
	s16 index;		/* 2: */
	s16 lastindex;		/* 2: */
	struct metapage *mp;	/* 4: */
} btframe_t;			/* (16) */

typedef struct btstack {
	btframe_t *top;		/* 4: */
	int nsplit;		/* 4: */
	btframe_t stack[MAXTREEHEIGHT];
} btstack_t;

#define BT_CLR(btstack)\
	(btstack)->top = (btstack)->stack

#define BT_PUSH(BTSTACK, BN, INDEX)\
{\
	(BTSTACK)->top->bn = BN;\
	(BTSTACK)->top->index = INDEX;\
	++(BTSTACK)->top;\
	assert((BTSTACK)->top != &((BTSTACK)->stack[MAXTREEHEIGHT]));\
}

#define BT_POP(btstack)\
	( (btstack)->top == (btstack)->stack ? NULL : --(btstack)->top )

#define BT_STACK(btstack)\
	( (btstack)->top == (btstack)->stack ? NULL : (btstack)->top )

/* retrieve search results */
#define BT_GETSEARCH(IP, LEAF, BN, MP, TYPE, P, INDEX, ROOT)\
{\
	BN = (LEAF)->bn;\
	MP = (LEAF)->mp;\
	if (BN)\
		P = (TYPE *)MP->data;\
	else\
		P = (TYPE *)&JFS_IP(IP)->ROOT;\
	INDEX = (LEAF)->index;\
}

/* put the page buffer of search */
#define BT_PUTSEARCH(BTSTACK)\
{\
	if (! BT_IS_ROOT((BTSTACK)->top->mp))\
		release_metapage((BTSTACK)->top->mp);\
}
#endif				/* _H_JFS_BTREE */

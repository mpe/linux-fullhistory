/*
 *  linux/fs/ufs/util.h
 *
 * Copyright (C) 1998 
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 */

#include <linux/fs.h>
#include "swab.h"


/*
 * some usefull marcos
 */
#define in_range(b,first,len)	((b)>=(first)&&(b)<(first)+(len))
#define howmany(x,y)		(((x)+(y)-1)/(y))
#define min(x,y)		((x)<(y)?(x):(y))
#define max(x,y)		((x)>(y)?(x):(y))


/*
 * current filesystem state; method depends on flags
 */
#define ufs_state(usb3) \
	(((flags & UFS_ST_MASK) == UFS_ST_OLD) \
	? (usb3)->fs_u.fs_sun.fs_state /* old normal way */ \
	: (usb3)->fs_u.fs_44.fs_state /* 4.4BSD way */)

/*
 * namlen, it's format depends of flags
 */
#define ufs_namlen(de) _ufs_namlen_(de,flags,swab)
static inline __u16 _ufs_namlen_(struct ufs_dir_entry * de, unsigned flags, unsigned swab) {
	if ((flags & UFS_DE_MASK) == UFS_DE_OLD) {
		return SWAB16(de->d_u.d_namlen);
	} else /* UFS_DE_44BSD */ {
		return de->d_u.d_44.d_namlen;
	}
}

/*
 * Here is how the uid is computed:
 * if the file system is 4.2BSD, get it from oldids.
 * if it has sun extension and oldids is USEEFT, get it from ui_sun.
 * if it is 4.4 or Hurd, get it from ui_44 (which is the same as ui_hurd).
 */
#define ufs_uid(inode) _ufs_uid_(inode,flags,swab)
static inline __u32 _ufs_uid_(struct ufs_inode * inode, unsigned flags, unsigned swab) {
	switch (flags & UFS_UID_MASK) {
		case UFS_UID_EFT:
			return SWAB32(inode->ui_u3.ui_sun.ui_uid);
		case UFS_UID_44BSD:
			return SWAB32(inode->ui_u3.ui_44.ui_uid);
		case UFS_UID_OLD:
		default:
			return SWAB16(inode->ui_u1.oldids.ui_suid);
	}
}

#define ufs_gid(inode) _ufs_gid_(inode,flags,swab)
static inline __u32 _ufs_gid_(struct ufs_inode * inode, unsigned flags, unsigned swab) {
	switch (flags & UFS_UID_MASK) {
		case UFS_UID_EFT:
			return SWAB32(inode->ui_u3.ui_sun.ui_gid);
		case UFS_UID_44BSD:
			return SWAB32(inode->ui_u3.ui_44.ui_gid);
		case UFS_UID_OLD:
		default:
			return SWAB16(inode->ui_u1.oldids.ui_sgid);
	}
}

/*
 * marcros used for retyping
 */
#define UCPI_UBH ((struct ufs_buffer_head *)ucpi)
#define USPI_UBH ((struct ufs_buffer_head *)uspi)

/*
 * This functions manipulate with ufs_buffers
 */
#define ubh_bread(dev,fragment,size) _ubh_bread_(uspi,dev,fragment,size)  
extern struct ufs_buffer_head * _ubh_bread_(struct ufs_sb_private_info *, kdev_t, unsigned, unsigned);
#define ubh_bread2(dev,fragment,size) _ubh_bread2_(uspi,dev,fragment,size)  
extern struct ufs_buffer_head * _ubh_bread2_(struct ufs_sb_private_info *, kdev_t, unsigned, unsigned);
extern void ubh_brelse (struct ufs_buffer_head *);
extern void ubh_brelse2 (struct ufs_buffer_head *);
extern void ubh_mark_buffer_dirty (struct ufs_buffer_head *, int);
extern void ubh_mark_buffer_uptodate (struct ufs_buffer_head *, int);
extern void ubh_ll_rw_block (int, unsigned, struct ufs_buffer_head **);
extern void ubh_wait_on_buffer (struct ufs_buffer_head *);
extern unsigned ubh_max_bcount (struct ufs_buffer_head *);
extern void ubh_bforget (struct ufs_buffer_head *);
extern int  ubh_buffer_dirty (struct ufs_buffer_head *);
#define ubh_ubhcpymem(mem,ubh,size) _ubh_ubhcpymem_(uspi,mem,ubh,size)
extern void _ubh_ubhcpymem_(struct ufs_sb_private_info *, unsigned char *, struct ufs_buffer_head *, unsigned);
#define ubh_memcpyubh(ubh,mem,size) _ubh_memcpyubh_(uspi,ubh,mem,size)
extern void _ubh_memcpyubh_(struct ufs_sb_private_info *, struct ufs_buffer_head *, unsigned char *, unsigned);

/*
 * macros to get important structures from ufs_buffer_head
 */
#define ubh_get_usb_first(ubh) \
	((struct ufs_super_block_first *)((ubh)->bh[0]->b_data))

#define ubh_get_usb_second(ubh) \
	((struct ufs_super_block_second *)(ubh)-> \
	bh[SECTOR_SIZE >> uspi->s_fshift]->b_data + (SECTOR_SIZE & ~uspi->s_fmask))

#define ubh_get_usb_third(ubh) \
	((struct ufs_super_block_third *)((ubh)-> \
	bh[SECTOR_SIZE*2 >> uspi->s_fshift]->b_data + (SECTOR_SIZE*2 & ~uspi->s_fmask)))

#define ubh_get_ucg(ubh) \
	((struct ufs_cylinder_group *)((ubh)->bh[0]->b_data))

/*
 * Extract byte from ufs_buffer_head
 * Extract the bits for a block from a map inside ufs_buffer_head
 */
#define ubh_get_addr8(ubh,begin) \
	((u8*)(ubh)->bh[(begin) >> uspi->s_fshift]->b_data + ((begin) & ~uspi->s_fmask))

#define ubh_get_addr16(ubh,begin) \
	(((u16*)((ubh)->bh[(begin) >> (uspi->s_fshift-1)]->b_data)) + ((begin) & (uspi->fsize>>1) - 1)))

#define ubh_get_addr32(ubh,begin) \
	(((u32*)((ubh)->bh[(begin) >> (BLOCK_SIZE_BITS-2)]->b_data)) + \
	((begin) & ((BLOCK_SIZE>>2) - 1)))

#define ubh_get_addr ubh_get_addr8

#define ubh_blkmap(ubh,begin,bit) \
	((*ubh_get_addr(ubh, (begin) + ((bit) >> 3)) >> ((bit) & 7)) & (0xff >> (UFS_MAXFRAG - uspi->s_fpb)))

/*
 * Macros for access to superblock array structures
 */
#define ubh_postbl(ubh,cylno,i) \
	((uspi->s_postblformat != UFS_DYNAMICPOSTBLFMT) \
	? (*(__s16*)(ubh_get_addr(ubh, \
	(unsigned)(&((struct ufs_super_block *)0)->fs_opostbl) \
	+ (((cylno) * 16 + (i)) << 1) ) )) \
	: (*(__s16*)(ubh_get_addr(ubh, \
	uspi->s_postbloff + (((cylno) * uspi->s_nrpos + (i)) << 1) ))))

#define ubh_rotbl(ubh,i) \
	((uspi->s_postblformat != UFS_DYNAMICPOSTBLFMT) \
	? (*(__u8*)(ubh_get_addr(ubh, \
	(unsigned)(&((struct ufs_super_block *)0)->fs_space) + (i)))) \
	: (*(__u8*)(ubh_get_addr(ubh, uspi->s_rotbloff + (i)))))

/*
 * Determine the number of available frags given a
 * percentage to hold in reserve.
 */
#define ufs_freespace(usb, percentreserved) \
	(ufs_blkstofrags(SWAB32((usb)->fs_cstotal.cs_nbfree)) + \
	SWAB32((usb)->fs_cstotal.cs_nffree) - (uspi->s_dsize * (percentreserved) / 100))

/*
 * Macros for access to cylinder group array structures
 */
#define ubh_cg_blktot(ucpi,cylno) \
	(*((__u32*)ubh_get_addr(UCPI_UBH, (ucpi)->c_btotoff + ((cylno) << 2))))

#define ubh_cg_blks(ucpi,cylno,rpos) \
	(*((__u16*)ubh_get_addr(UCPI_UBH, \
	(ucpi)->c_boff + (((cylno) * uspi->s_nrpos + (rpos)) << 1 ))))

/*
 * Bitmap operation
 * This functions work like classical bitmap operations. The diference 
 * is that we havn't the whole bitmap in one continuous part of memory,
 * but in a few buffers.
 * The parameter of each function is super_block, ufs_buffer_head and
 * position of the begining of the bitmap.
 */
#define ubh_setbit(ubh,begin,bit) \
	(*ubh_get_addr(ubh, (begin) + ((bit) >> 3)) |= (1 << ((bit) & 7)))

#define ubh_clrbit(ubh,begin,bit) \
	(*ubh_get_addr (ubh, (begin) + ((bit) >> 3)) &= ~(1 << ((bit) & 7)))

#define ubh_isset(ubh,begin,bit) \
	(*ubh_get_addr (ubh, (begin) + ((bit) >> 3)) & (1 << ((bit) & 7)))

#define ubh_isclr(ubh,begin,bit) (!ubh_isset(ubh,begin,bit))

#define ubh_find_first_zero_bit(ubh,begin,size) _ubh_find_next_zero_bit_(uspi,ubh,begin,size,0)
#define ubh_find_next_zero_bit(ubh,begin,size,offset) _ubh_find_next_zero_bit_(uspi,ubh,begin,size,offset)
static inline unsigned _ubh_find_next_zero_bit_(
	struct ufs_sb_private_info * uspi, struct ufs_buffer_head * ubh,
	unsigned begin, unsigned size, unsigned offset)
{
	unsigned base, rest;

	begin <<= 3;
	size += begin;
	offset += begin;
	base = offset >> (uspi->s_fshift + 3);
	offset &= ((uspi->s_fsize << 3) - 1);
	for (;;) {
		rest = min (size, uspi->s_fsize << 3);
		size -= rest;
		offset = ext2_find_next_zero_bit (ubh->bh[base]->b_data, rest, offset);
		if (offset < rest || !size)
			break;
		base++;
		offset = 0;
	}
	return (base << (uspi->s_fshift + 3)) + offset - begin;
} 	

#define ubh_isblockclear(ubh,begin,block) (!_ubh_isblockset_(uspi,ubh,begin,block))
#define ubh_isblockset(ubh,begin,block) _ubh_isblockset_(uspi,ubh,begin,block)
static inline int _ubh_isblockset_(struct ufs_sb_private_info * uspi,
	struct ufs_buffer_head * ubh, unsigned begin, unsigned block)
{
	switch (uspi->s_fpb) {
	case 8:
	    	return (*ubh_get_addr (ubh, begin + block) == 0xff);
	case 4:
		return (*ubh_get_addr (ubh, begin + (block >> 1)) == (0x0f << ((block & 0x01) << 2)));
	case 2:
		return (*ubh_get_addr (ubh, begin + (block >> 2)) == (0x03 << ((block & 0x03) << 1)));
	case 1:
		return (*ubh_get_addr (ubh, begin + (block >> 3)) == (0x01 << (block & 0x07)));
	}
	return 0;	
}

#define ubh_clrblock(ubh,begin,block) _ubh_clrblock_(uspi,ubh,begin,block)
static inline void _ubh_clrblock_(struct ufs_sb_private_info * uspi,
	struct ufs_buffer_head * ubh, unsigned begin, unsigned block)
{
	switch (uspi->s_fpb) {
	case 8:
	    	*ubh_get_addr (ubh, begin + block) = 0x00;
	    	return; 
	case 4:
		*ubh_get_addr (ubh, begin + (block >> 1)) &= ~(0x0f << ((block & 0x01) << 2));
		return;
	case 2:
		*ubh_get_addr (ubh, begin + (block >> 2)) &= ~(0x03 << ((block & 0x03) << 1));
		return;
	case 1:
		*ubh_get_addr (ubh, begin + (block >> 3)) &= ~(0x01 << ((block & 0x07)));
		return;
	}
}

#define ubh_setblock(ubh,begin,block) _ubh_setblock_(uspi,ubh,begin,block)
static inline void _ubh_setblock_(struct ufs_sb_private_info * uspi,
	struct ufs_buffer_head * ubh, unsigned begin, unsigned block)
{
	switch (uspi->s_fpb) {
	case 8:
	    	*ubh_get_addr(ubh, begin + block) = 0xff;
	    	return;
	case 4:
		*ubh_get_addr(ubh, begin + (block >> 1)) |= (0x0f << ((block & 0x01) << 2));
		return;
	case 2:
		*ubh_get_addr(ubh, begin + (block >> 2)) |= (0x03 << ((block & 0x03) << 1));
		return;
	case 1:
		*ubh_get_addr(ubh, begin + (block >> 3)) |= (0x01 << ((block & 0x07)));
		return;
	}
}

static inline void ufs_fragacct (struct super_block * sb, unsigned blockmap,
	unsigned * fraglist, int cnt)
{
	struct ufs_sb_private_info * uspi;
	unsigned fragsize, pos;
	unsigned swab;
	
	swab = sb->u.ufs_sb.s_swab;
	uspi = sb->u.ufs_sb.s_uspi;
	
	fragsize = 0;
	for (pos = 0; pos < uspi->s_fpb; pos++) {
		if (blockmap & (1 << pos)) {
			fragsize++;
		}
		else if (fragsize > 0) {
			ADD_SWAB32(fraglist[fragsize], cnt);
			fragsize = 0;
		}
	}
	if (fragsize > 0 && fragsize < uspi->s_fpb)
		ADD_SWAB32(fraglist[fragsize], cnt);
}

#define ubh_scanc(ubh,begin,size,table,mask) _ubh_scanc_(uspi,ubh,begin,size,table,mask)
static inline unsigned _ubh_scanc_(struct ufs_sb_private_info * uspi, struct ufs_buffer_head * ubh, 
	unsigned begin, unsigned size, unsigned char * table, unsigned char mask)
{
	unsigned rest, offset;
	unsigned char * cp;
	

	offset = begin & ~uspi->s_fmask;
	begin >>= uspi->s_fshift;
	for (;;) {
		if ((offset + size) < uspi->s_fsize)
			rest = size;
		else
			rest = uspi->s_fsize - offset;
		size -= rest;
		cp = ubh->bh[begin]->b_data + offset;
		while ((table[*cp++] & mask) == 0 && --rest);
		if (rest || !size)
			break;
		begin++;
		offset = 0;
	}
	return (size + rest);
}

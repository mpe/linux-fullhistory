/*
 * include/asm-mips/segment.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 by Ralf Baechle
 *
 */
#ifndef __ASM_MIPS_SEGMENT_H
#define __ASM_MIPS_SEGMENT_H

/*
 * Memory segments (32bit kernel mode addresses)
 */
#define KUSEG                   0x00000000
#define KSEG0                   0x80000000
#define KSEG1                   0xa0000000
#define KSEG2                   0xc0000000
#define KSEG3                   0xe0000000

/*
 * returns the kernel segment base of a given address
 */
#define KSEGX(a)                (a & 0xe0000000)

#ifndef __ASSEMBLY__

/*
 * Beware: the xxx_fs_word functions work on 16bit words!
 */
#define get_fs_byte(addr) get_user_byte((char *)(addr))
static inline unsigned char get_user_byte(const char *addr)
{
	return *addr;
}

#define get_fs_word(addr) get_user_word((short *)(addr))
static inline unsigned short get_user_word(const short *addr)
{
	return *addr;
}

#define get_fs_long(addr) get_user_long((int *)(addr))
static inline unsigned long get_user_long(const int *addr)
{
	return *addr;
}

#define get_fs_dlong(addr) get_user_dlong((long long *)(addr))
static inline unsigned long get_user_dlong(const long long *addr)
{
	return *addr;
}

#define put_fs_byte(x,addr) put_user_byte((x),(char *)(addr))
static inline void put_user_byte(char val,char *addr)
{
	*addr = val;
}

#define put_fs_word(x,addr) put_user_word((x),(short *)(addr))
static inline void put_user_word(short val,short * addr)
{
	*addr = val;
}

#define put_fs_long(x,addr) put_user_long((x),(int *)(addr))
static inline void put_user_long(unsigned long val,int * addr)
{
	*addr = val;
}

#define put_fs_dlong(x,addr) put_user_dlong((x),(int *)(addr))
static inline void put_user_dlong(unsigned long val,long long * addr)
{
	*addr = val;
}

#define memcpy_fromfs(to, from, n) memcpy((to),(from),(n))

#define memcpy_tofs(to, from, n) memcpy((to),(from),(n))

/*
 * For segmented architectures, these are used to specify which segment
 * to use for the above functions.
 *
 * MIPS is not segmented, so these are just dummies.
 */

#define KERNEL_DS 0
#define USER_DS 1

static inline unsigned long get_fs(void)
{
	return 0;
}

static inline unsigned long get_ds(void)
{
	return 0;
}

static inline void set_fs(unsigned long val)
{
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_MIPS_SEGMENT_H */

#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#include <linux/string.h>

static inline unsigned char get_user_byte(const char * addr)
{
	return *addr;
}

#define get_fs_byte(addr) get_user_byte((char *)(addr))

static inline unsigned short get_user_word(const short *addr)
{
	return *addr;
}

#define get_fs_word(addr) get_user_word((short *)(addr))

static inline unsigned long get_user_long(const int *addr)
{
	return *addr;
}

#define get_fs_long(addr) get_user_long((int *)(addr))

static inline unsigned long get_user_quad(const long *addr)
{
	return *addr;
}

#define get_fs_quad(addr) get_user_quad((long *)(addr))

static inline void put_user_byte(char val,char *addr)
{
	*addr = val;
}

#define put_fs_byte(x,addr) put_user_byte((x),(char *)(addr))

static inline void put_user_word(short val,short * addr)
{
	*addr = val;
}

#define put_fs_word(x,addr) put_user_word((x),(short *)(addr))

static inline void put_user_long(unsigned long val,int * addr)
{
	*addr = val;
}

#define put_fs_long(x,addr) put_user_long((x),(int *)(addr))

static inline void put_user_quad(unsigned long val,long * addr)
{
	*addr = val;
}

#define put_fs_quad(x,addr) put_user_quad((x),(long *)(addr))

#define memcpy_fromfs(to, from, n) memcpy((to),(from),(n))

#define memcpy_tofs(to, from, n) memcpy((to),(from),(n))

/*
 * For segmented architectures, these are used to specify which segment
 * to use for the above functions.
 *
 * The alpha is not segmented, so these are just dummies.
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

#endif /* _ASM_SEGMENT_H */

#ifndef _ASM_PPC_SEGMENT_H
#define _ASM_PPC_SEGMENT_H

#include <linux/string.h>

#define put_user(x,ptr) __put_user((unsigned long)(x),(ptr),sizeof(*(ptr)))
#define get_user(ptr) ((__typeof__(*(ptr)))__get_user((ptr),sizeof(*(ptr))))

extern int bad_user_access_length(void);

static inline void __put_user(unsigned long x, void * y, int size)
{
	switch (size) {
		case 1:
			*(char *) y = x;
			break;
		case 2:
			*(short *) y = x;
			break;
		case 4:
			*(int *) y = x;
			break;
		case 8:
			*(long *) y = x;
			break;
		default:
			bad_user_access_length();
	}
}

static inline unsigned long __get_user(const void * y, int size)
{
	switch (size) {
		case 1:
			return *(unsigned char *) y;
		case 2:
			return *(unsigned short *) y;
		case 4:
			return *(unsigned int *) y;
		case 8:
			return *(unsigned long *) y;
		default:
			return bad_user_access_length();
	}
}

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
 * The powerpc is not segmented, so these are just dummies.
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

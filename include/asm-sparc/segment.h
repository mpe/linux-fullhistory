/* $Id: segment.h,v 1.10 1996/03/08 01:19:38 miguel Exp $ */
#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#ifdef __KERNEL__
#include <asm/vac-ops.h>
#endif

#ifndef __ASSEMBLY__

/*
 * Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 */
#define put_user(x,ptr) __put_user((unsigned long)(x),(ptr),sizeof(*(ptr)))
#define get_user(ptr) ((__typeof__(*(ptr)))__get_user((ptr),sizeof(*(ptr))))

/*
 * This is a silly but good way to make sure that
 * the __put_user function is indeed always optimized,
 * and that we use the correct sizes..
 */
extern int bad_user_access_length(void);

/* I should make this use unaligned transfers etc.. */
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
		default:
			bad_user_access_length();
	}
}

/* I should make this use unaligned transfers etc.. */
static inline unsigned long __get_user(const void * y, int size)
{
	switch (size) {
		case 1:
			return *(unsigned char *) y;
		case 2:
			return *(unsigned short *) y;
		case 4:
			return *(unsigned int *) y;
		default:
			return bad_user_access_length();
	}
}

/*
 * Deprecated routines
 */

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

#define memcpy_fromfs(to, from, n) memcpy((to),(from),(n))

#define memcpy_tofs(to, from, n) memcpy((to),(from),(n))

/* Sparc is not segmented, however we need to be able to fool verify_area()
 * when doing system calls from kernel mode legitimately.
 */
#define KERNEL_DS   0
#define USER_DS     1

extern int active_ds;

static inline int get_fs(void)
{
	return active_ds;
}

static inline int get_ds(void)
{
	return KERNEL_DS;
}

static inline void set_fs(int val)
{
	active_ds = val;
}

#endif  /* __ASSEMBLY__ */

#endif /* _ASM_SEGMENT_H */

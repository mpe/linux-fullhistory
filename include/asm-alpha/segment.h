#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#include <linux/string.h>

/*
 * This is a gcc optimization barrier, which essentially
 * inserts a sequence point in the gcc RTL tree that gcc
 * can't move code around. This is needed when we enter
 * or exit a critical region (in this case around user-level
 * accesses that may sleep, and we can't let gcc optimize
 * global state around them).
 */
#define __gcc_barrier() __asm__ __volatile__("": : :"memory")

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
	__gcc_barrier();
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
	__gcc_barrier();
}

/* I should make this use unaligned transfers etc.. */
static inline unsigned long __get_user(const void * y, int size)
{
	unsigned long result;

	__gcc_barrier();
	switch (size) {
		case 1:
			result = *(unsigned char *) y;
			break;
		case 2:
			result = *(unsigned short *) y;
			break;
		case 4:
			result = *(unsigned int *) y;
			break;
		case 8:
			result = *(unsigned long *) y;
			break;
		default:
			result = bad_user_access_length();
	}
	__gcc_barrier();
	return result;
}

#define get_fs_byte(addr) get_user((unsigned char *)(addr))
#define get_fs_word(addr) get_user((unsigned short *)(addr))
#define get_fs_long(addr) get_user((unsigned int *)(addr))
#define get_fs_quad(addr) get_user((unsigned long *)(addr))

#define put_fs_byte(x,addr) put_user((x),(char *)(addr))
#define put_fs_word(x,addr) put_user((x),(short *)(addr))
#define put_fs_long(x,addr) put_user((x),(int *)(addr))
#define put_fs_quad(x,addr) put_user((x),(long *)(addr))

static inline void memcpy_fromfs(void * to, const void * from, unsigned long n)
{
	__gcc_barrier();
	memcpy(to, from, n);
	__gcc_barrier();
}

static inline void memcpy_tofs(void * to, const void * from, unsigned long n)
{
	__gcc_barrier();
	memcpy(to, from, n);
	__gcc_barrier();
}

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define KERNEL_DS	0
#define USER_DS		1

#define get_fs()  (current->tss.flags & 0x1)
#define set_fs(x) (current->tss.flags = (current->tss.flags & ~0x1) | ((x) & 0x1))

static inline unsigned long get_ds(void)
{
	return 0;
}

#endif /* _ASM_SEGMENT_H */

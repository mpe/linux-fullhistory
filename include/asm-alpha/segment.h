#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#include <linux/string.h>

/*
 * Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 *
 * As the alpha uses the same address space for kernel and user
 * data, we can just do these as direct assignments.
 */
#define put_user(x,ptr)	do { (*(ptr)=(x)); } while (0)
#define get_user(ptr)	(*(ptr))

/*
 * These are deprecated..
 *
 * Use "put_user()" and "get_user()" with the proper pointer types instead.
 */
#define get_fs_byte(addr) get_user((unsigned char *)(addr))
#define get_fs_word(addr) get_user((unsigned short *)(addr))
#define get_fs_long(addr) get_user((unsigned int *)(addr))
#define get_fs_quad(addr) get_user((unsigned long *)(addr))

#define put_fs_byte(x,addr) put_user((x),(char *)(addr))
#define put_fs_word(x,addr) put_user((x),(short *)(addr))
#define put_fs_long(x,addr) put_user((x),(int *)(addr))
#define put_fs_quad(x,addr) put_user((x),(long *)(addr))

#define memcpy_fromfs(to,from,n) memcpy((to),(from),(n))
#define memcpy_tofs(to,from,n) memcpy((to),(from),(n))

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

#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#define KERNEL_CS	0x10
#define KERNEL_DS	0x18

#define USER_CS		0x23
#define USER_DS		0x2B

#ifndef __ASSEMBLY__

#include <linux/string.h>

/*
 * Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 */
#define put_user(x,ptr)	do { (*(ptr)=(x)); } while (0)
#define get_user(ptr)	(*(ptr))

/*
 * These are deprecated..
 *
 * Use "put_user()" and "get_user()" with the proper pointer types instead.
 */

#define get_fs_byte(addr) get_user((const unsigned char *)(addr))
#define get_fs_word(addr) get_user((const unsigned short *)(addr))
#define get_fs_long(addr) get_user((const unsigned int *)(addr))

#define put_fs_byte(x,addr) put_user((x),(unsigned char *)(addr))
#define put_fs_word(x,addr) put_user((x),(unsigned short *)(addr))
#define put_fs_long(x,addr) put_user((x),(unsigned int *)(addr))

#define memcpy_fromfs(to,from,n) memcpy((to),(from),(n))
#define memcpy_tofs(to,from,n)   memcpy((to),(from),(n))

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 * 
 * For historical reasons, these macros are grossly misnamed.
 */
      
#define get_fs()	(current->tss.segment)
#define set_fs(x)	(current->tss.segment = (x))
#define get_ds()	(KERNEL_DS)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_SEGMENT_H */

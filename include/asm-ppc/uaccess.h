#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#ifndef __ASSEMBLY__
#include <linux/sched.h>
#include <linux/errno.h>

#define KERNEL_DS   (0)
#define USER_DS		(1)

#define VERIFY_READ	0
#define VERIFY_WRITE	1

#define get_fs() (current->tss.fs)
#define get_ds() (KERNEL_DS)
#define set_fs(val) ( current->tss.fs = (val))

#define __user_ok(addr,size) (((size) <= 0x80000000)&&((addr) <= 0x80000000-(size)))
#define __kernel_ok (get_fs() == KERNEL_DS)
#define __access_ok(addr,size) (__kernel_ok || __user_ok((addr),(size)))
#define access_ok(type,addr,size) __access_ok((unsigned long)(addr),(size))

extern inline int verify_area(int type, const void * addr, unsigned long size)
{
	return access_ok(type,addr,size) ? 0 : -EFAULT;
}

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * As the powerpc uses the same address space for kernel and user
 * data, we can just do these as direct assignments.  (Of course, the
 * exception handling means that it's no longer "just"...)
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof/typeof is ok)
 * (b) require any knowledge of processes at this stage
 */
/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the programmer has to do the
 * checks by hand with "access_ok()")
 */
#define put_user(x,ptr) ({ \
unsigned long __pu_addr = (unsigned long)(ptr); \
__put_user_check((__typeof__(*(ptr)))(x),__pu_addr,sizeof(*(ptr))); })

#define get_user(x,ptr) ({ \
unsigned long __gu_addr = (unsigned long)(ptr); \
__get_user_check((x),__gu_addr,sizeof(*(ptr)),__typeof__(*(ptr))); })

#define __put_user(x,ptr) __put_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __get_user(x,ptr) __get_user_nocheck((x),(ptr),sizeof(*(ptr)),__typeof__(*(ptr)))
struct __large_struct { unsigned long buf[100]; };
#define __m(x) ((struct __large_struct *)(x))

#define __put_user_check(x,addr,size) ({ \
int __pu_ret; \
__pu_ret = -EFAULT; \
if (__access_ok(addr,size)) { \
switch (size) { \
case 1: __pu_ret =__put_user_8(x,addr); break; \
case 2: __pu_ret =__put_user_16(x,addr); break; \
case 4: __pu_ret =__put_user_32(x,addr); break; \
default: __pu_ret = __put_user_bad(); break; \
} } __pu_ret; })

#define __put_user_nocheck(x,addr,size) ({ \
int __pu_ret; \
__pu_ret = -EFAULT; \
switch (size) { \
case 1: __pu_ret =__put_user_8(x,addr); break; \
case 2: __pu_ret =__put_user_16(x,addr); break; \
case 4: __pu_ret =__put_user_32(x,addr); break; \
default: __pu_ret = __put_user_bad(); break; \
} __pu_ret; })

extern int __put_user_bad(void);

#define __get_user_check(x,addr,size,type) ({ \
register int __gu_ret asm("r4"); \
unsigned long __gu_val = 0; \
__gu_ret = -EFAULT; \
if (__access_ok(addr,size)) { \
switch (size) { \
case 1: __gu_val = __get_user_8(__gu_val,addr); break; \
case 2: __gu_val = __get_user_16(__gu_val,addr); break; \
case 4: __gu_val = __get_user_32(__gu_val,addr); break; \
default: __get_user_bad(); break; \
} } (x) = (type) __gu_val; __gu_ret; })

#define __get_user_nocheck(x,addr,size,type) ({ \
register int __gu_ret asm("r4"); \
unsigned long __gu_val = 0; \
__gu_ret = -EFAULT; \
switch (size) { \
case 1: __gu_val =__get_user_8(__gu_val,addr); break; \
case 2: __gu_val =__get_user_16(__gu_val,addr); break; \
case 4: __gu_val =__get_user_32(__gu_val,addr); break; \
default: __gu_val = __get_user_bad(); break; \
} (x) = (type) __gu_val; __gu_ret;  })
     

/* more complex routines */

extern int __copy_tofrom_user(unsigned long to, unsigned long from, int size);

#define copy_to_user(to,from,n) ({ \
unsigned long __copy_to = (unsigned long) (to); \
unsigned long __copy_size = (unsigned long) (n); \
unsigned long __copy_res = -EFAULT; \
if(__copy_size && __access_ok(__copy_to, __copy_size)) { \
__copy_res = __copy_tofrom_user(__copy_to, (unsigned long) (from), __copy_size); \
}  \
__copy_res; })

#define copy_from_user(to,from,n) ({ \
unsigned long __copy_from = (unsigned long) (from); \
unsigned long __copy_size = (unsigned long) (n); \
unsigned long __copy_res = -EFAULT; \
if(__copy_size && __access_ok(__copy_from, __copy_size)) { \
__copy_res = __copy_tofrom_user((unsigned long) (to), __copy_from, __copy_size); \
} \
__copy_res; })

extern int __clear_user(unsigned long addr, int size);

#define clear_user(addr,n) ({ \
unsigned long __clear_addr = (unsigned long) (addr); \
int __clear_size = (int) (n); \
int __clear_res = -EFAULT; \
if(__clear_size && __access_ok(__clear_addr, __clear_size)) { \
__clear_res = __clear_user(__clear_addr, __clear_size); \
} \
__clear_res; })

extern int __strncpy_from_user(unsigned long dest, unsigned long src, int count);

#define strncpy_from_user(dest,src,count) ({ \
unsigned long __sfu_src = (unsigned long) (src); \
int __sfu_count = (int) (count); \
long __sfu_res = -EFAULT; \
if(__access_ok(__sfu_src, __sfu_count)) { \
__sfu_res = __strncpy_from_user((unsigned long) (dest), __sfu_src, __sfu_count); \
} __sfu_res; })

#endif  /* __ASSEMBLY__ */

#endif /* _ASM_UACCESS_H */

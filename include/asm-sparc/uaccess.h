/* $Id: uaccess.h,v 1.2 1996/10/31 00:59:56 davem Exp $ */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

/*
 * User space memory access functions
 */

#ifdef __KERNEL__
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/vac-ops.h>
#endif

#ifndef __ASSEMBLY__

/* Sparc is not segmented, however we need to be able to fool verify_area()
 * when doing system calls from kernel mode legitimately.
 *
 * "For historical reasons, these macros are grossly misnamed." -Linus
 */
#define KERNEL_DS   0
#define USER_DS     1

#define VERIFY_READ	0
#define VERIFY_WRITE	1

#define get_fs() (current->tss.current_ds)
#define get_ds() (KERNEL_DS)
#define set_fs(val) ((current->tss.current_ds) = (val))

#define __user_ok(addr,size) (((size) <= page_offset)&&((addr) <= page_offset-(size)))
#define __kernel_ok (get_fs() == KERNEL_DS)
#define __access_ok(addr,size) (__kernel_ok || __user_ok((addr),(size)))
#define access_ok(type,addr,size) __access_ok((unsigned long)(addr),(size))

extern inline int verify_area(int type, const void * addr, unsigned long size)
{
	return access_ok(type,addr,size)?0:-EFAULT;
}

/* Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the uglyness from the user.
 */
#define put_user(x,ptr) ({ \
unsigned long __pu_addr = (unsigned long)(ptr); \
__put_user_check((__typeof__(*(ptr)))(x),__pu_addr,sizeof(*(ptr))); })

#define get_user(x,ptr) ({ \
unsigned long __gu_addr = (unsigned long)(ptr); \
__get_user_check((x),__gu_addr,sizeof(*(ptr)),__typeof__(*(ptr))); })

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x,ptr) __put_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __get_user(x,ptr) __get_user_nocheck((x),(ptr),sizeof(*(ptr)),__typeof__(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) ((struct __large_struct *)(x))

#define __put_user_check(x,addr,size) ({ \
register int __pu_ret asm("g1"); \
__pu_ret = -EFAULT; \
if (__access_ok(addr,size)) { \
switch (size) { \
case 1: __put_user_8(x,addr,__pu_ret); break; \
case 2: __put_user_16(x,addr,__pu_ret); break; \
case 4: __put_user_32(x,addr,__pu_ret); break; \
default: __pu_ret = __put_user_bad(); break; \
} } __pu_ret; })

#define __put_user_nocheck(x,addr,size) ({ \
register int __pu_ret asm("g1"); \
__pu_ret = -EFAULT; \
switch (size) { \
case 1: __put_user_8(x,addr,__pu_ret); break; \
case 2: __put_user_16(x,addr,__pu_ret); break; \
case 4: __put_user_32(x,addr,__pu_ret); break; \
default: __pu_ret = __put_user_bad(); break; \
} __pu_ret; })

#define __put_user_8(x,addr,ret)						\
__asm__ __volatile(								\
	"/* Put user 8, inline. */\n\t"						\
	"ld	[%%g6 + %3], %%g2\n\t"						\
	"set	1f, %0\n\t"							\
	"add	%%g2, 1, %%g3\n\t"						\
	"st	%0, [%%g6 + %4]\n\t"						\
	"st	%%g3, [%%g6 + %3]\n\t"						\
	"stb	%1, [%2]\n\t"							\
	"mov	0, %0\n\t"							\
	"st	%%g2, [%%g6 + %3]\n"						\
"1:\n" : "=&r" (ret) : "r" (x), "r" (__m(addr)),				\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.count)),	\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.expc))	\
       : "g2", "g3")

#define __put_user_16(x,addr,ret)						\
__asm__ __volatile(								\
	"/* Put user 16, inline. */\n\t"					\
	"ld	[%%g6 + %3], %%g2\n\t"						\
	"set	1f, %0\n\t"							\
	"add	%%g2, 1, %%g3\n\t"						\
	"st	%0, [%%g6 + %4]\n\t"						\
	"st	%%g3, [%%g6 + %3]\n\t"						\
	"sth	%1, [%2]\n\t"							\
	"mov	0, %0\n\t"							\
	"st	%%g2, [%%g6 + %3]\n"						\
"1:\n" : "=&r" (ret) : "r" (x), "r" (__m(addr)),				\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.count)),	\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.expc))	\
       : "g2", "g3")

#define __put_user_32(x,addr,ret)						\
__asm__ __volatile(								\
	"/* Put user 32, inline. */\n\t"					\
	"ld	[%%g6 + %3], %%g2\n\t"						\
	"set	1f, %0\n\t"							\
	"add	%%g2, 1, %%g3\n\t"						\
	"st	%0, [%%g6 + %4]\n\t"						\
	"st	%%g3, [%%g6 + %3]\n\t"						\
	"st	%1, [%2]\n\t"							\
	"mov	0, %0\n\t"							\
	"st	%%g2, [%%g6 + %3]\n"						\
"1:\n" : "=&r" (ret) : "r" (x), "r" (__m(addr)),				\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.count)),	\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.expc))	\
       : "g2", "g3")

extern int __put_user_bad(void);

#define __get_user_check(x,addr,size,type) ({ \
register int __gu_ret asm("g1"); \
register unsigned long __gu_val = 0; \
__gu_ret = -EFAULT; \
if (__access_ok(addr,size)) { \
switch (size) { \
case 1: __get_user_8(__gu_val,addr,__gu_ret); break; \
case 2: __get_user_16(__gu_val,addr,__gu_ret); break; \
case 4: __get_user_32(__gu_val,addr,__gu_ret); break; \
default: __gu_ret = __get_user_bad(); break; \
} } x = (type) __gu_val; __gu_ret; })

#define __get_user_nocheck(x,addr,size,type) ({ \
register int __gu_ret asm("g1"); \
register unsigned long __gu_val = 0; \
__gu_ret = -EFAULT; \
switch (size) { \
case 1: __get_user_8(__gu_val,addr,__gu_ret); break; \
case 2: __get_user_16(__gu_val,addr,__gu_ret); break; \
case 4: __get_user_32(__gu_val,addr,__gu_ret); break; \
default: __gu_ret = __get_user_bad(); break; \
} x = (type) __gu_val; __gu_ret; })

#define __get_user_8(x,addr,ret)						\
__asm__ __volatile__(								\
	"/* Get user 8, inline. */\n\t"						\
	"ld	[%%g6 + %3], %%g2\n\t"						\
	"set	1f, %0\n\t"							\
	"add	%%g2, 1, %%g3\n\t"						\
	"st	%0, [%%g6 + %4]\n\t"						\
	"st	%%g3, [%%g6 + %3]\n\t"						\
	"ldub	[%2], %1\n\t"							\
	"mov	0, %0\n\t"							\
	"st	%%g2, [%%g6 + %3]\n"						\
"1:\n" : "=&r" (ret), "=&r" (x) : "r" (__m(addr)),				\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.count)),	\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.expc))	\
       : "g2", "g3")

#define __get_user_16(x,addr,ret)						\
__asm__ __volatile__(								\
	"/* Get user 16, inline. */\n\t"					\
	"ld	[%%g6 + %3], %%g2\n\t"						\
	"set	1f, %0\n\t"							\
	"add	%%g2, 1, %%g3\n\t"						\
	"st	%0, [%%g6 + %4]\n\t"						\
	"st	%%g3, [%%g6 + %3]\n\t"						\
	"lduh	[%2], %1\n\t"							\
	"mov	0, %0\n\t"							\
	"st	%%g2, [%%g6 + %3]\n"						\
"1:\n" : "=&r" (ret), "=&r" (x) : "r" (__m(addr)),				\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.count)),	\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.expc))	\
       : "g2", "g3")

#define __get_user_32(x,addr,ret)						\
__asm__ __volatile__(								\
	"/* Get user 32, inline. */\n\t"					\
	"ld	[%%g6 + %3], %%g2\n\t"						\
	"set	1f, %0\n\t"							\
	"add	%%g2, 1, %%g3\n\t"						\
	"st	%0, [%%g6 + %4]\n\t"						\
	"st	%%g3, [%%g6 + %3]\n\t"						\
	"ld	[%2], %1\n\t"							\
	"mov	0, %0\n\t"							\
	"st	%%g2, [%%g6 + %3]\n"						\
"1:\n" : "=&r" (ret), "=&r" (x) : "r" (__m(addr)),				\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.count)),	\
	 "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ex.expc))	\
       : "g2", "g3")

extern int __get_user_bad(void);

extern int __copy_to_user(unsigned long to, unsigned long from, int size);
extern int __copy_from_user(unsigned long to, unsigned long from, int size);

#define copy_to_user(to,from,n) ({ \
unsigned long __copy_to = (unsigned long) (to); \
unsigned long __copy_size = (unsigned long) (n); \
unsigned long __copy_res; \
if(__copy_size && __access_ok(__copy_to, __copy_size)) { \
__copy_res = __copy_to_user(__copy_to, (unsigned long) (from), __copy_size); \
if(__copy_res) __copy_res = __copy_size - __copy_res; \
} else __copy_res = __copy_size; \
__copy_res; })

#define copy_from_user(to,from,n) ({ \
unsigned long __copy_from = (unsigned long) (from); \
unsigned long __copy_size = (unsigned long) (n); \
unsigned long __copy_res; \
if(__copy_size && __access_ok(__copy_from, __copy_size)) { \
__copy_res = __copy_from_user((unsigned long) (to), __copy_from, __copy_size); \
if(__copy_res) __copy_res = __copy_size - __copy_res; \
} else __copy_res = __copy_size; \
__copy_res; })

extern int __clear_user(unsigned long addr, int size);

#define clear_user(addr,n) ({ \
unsigned long __clear_addr = (unsigned long) (addr); \
int __clear_size = (int) (n); \
int __clear_res; \
if(__clear_size && __access_ok(__clear_addr, __clear_size)) { \
__clear_res = __clear_user(__clear_addr, __clear_size); \
if(__clear_res) __clear_res = __clear_size - __clear_res; \
} else __clear_res = __clear_size; \
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

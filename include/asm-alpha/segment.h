#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#include <linux/string.h>

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

/*
 * Is a address valid? This does a straighforward calculation rather
 * than tests.
 *
 * Address valid if:
 *  - "addr" doesn't have any high-bits set
 *  - AND "size" doesn't have any high-bits set
 *  - AND "addr+size" doesn't have any high-bits set
 *  - OR we are in kernel mode.
 */
#define __access_ok(addr,size,mask) \
(((mask)&((addr | size | (addr+size)) >> 42))==0)
#define __access_mask (-(long)get_fs())

#define access_ok(type,addr,size) \
__access_ok(((unsigned long)(addr)),(size),__access_mask)

/*
 * Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 *
 * As the alpha uses the same address space for kernel and user
 * data, we can just do these as direct assignments.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x,ptr)	__put_user((x),(ptr),sizeof(*(ptr)),__access_mask)
#define get_user(x,ptr) __get_user((x),(ptr),sizeof(*(ptr)),__access_mask)

#define copy_to_user(to,from,n)   __copy_tofrom_user((to),(from),(n),__cu_to)
#define copy_from_user(to,from,n) __copy_tofrom_user((to),(from),(n),__cu_from)

/*
 * Not pretty? What do you mean not "not pretty"?
 */
extern void __copy_user(void);

#define __copy_tofrom_user(to,from,n,v) ({ \
register void * __cu_to __asm__("$6"); \
register const void * __cu_from __asm__("$7"); \
register long __cu_len __asm__("$0"); \
__cu_to = (to); __cu_from = (from); \
__cu_len = (n); \
if (__access_ok(((unsigned long)(v)),__cu_len,__access_mask)) { \
register void * __cu_ex __asm__("$8"); \
__cu_ex = &current->tss.ex; \
__asm__ __volatile__("jsr $28,(%7),__copy_user" \
:"=r" (__cu_len), "=r" (__cu_from), "=r" (__cu_to) \
:"0" (__cu_len), "1" (__cu_from), "2" (__cu_to), \
 "r" (__cu_ex), "r" (__copy_user) \
:"$1","$2","$3","$4","$5","memory"); \
} __cu_len; })

#define __get_user(x,ptr,size,mask) ({ \
register long __gu_err __asm__("$0"); \
register long __gu_val __asm__("$1"); \
register long __gu_addr __asm__("$2"); \
register void * __gu_ex __asm__("$3"); \
__gu_addr = (long) (ptr); \
__gu_ex = &current->tss.ex; \
__gu_err = -EFAULT; \
__asm__("":"=r" (__gu_val)); \
if (__access_ok(__gu_addr,size,mask)) { \
switch (size) { \
case 1: __get_user_asm(8); break; \
case 2: __get_user_asm(16); break; \
case 4: __get_user_asm(32); break; \
case 8: __get_user_asm(64); break; \
default: __get_user_asm(unknown); break; \
} } x = (__typeof__(*(ptr))) __gu_val; __gu_err; })

extern void __get_user_8(void);
extern void __get_user_16(void);
extern void __get_user_32(void);
extern void __get_user_64(void);
extern void __get_user_unknown(void);

#define __get_user_asm(x) \
__asm__ __volatile__("jsr $28,(%4),__get_user_" #x \
:"=r" (__gu_err),"=r" (__gu_val) \
:"r" (__gu_ex), "r" (__gu_addr),"r" (__get_user_##x) \
:"$4","$5","$28")

#define __put_user(x,ptr,size,mask) ({ \
register long __pu_err __asm__("$0"); \
register __typeof__(*(ptr)) __pu_val __asm__("$6"); \
register long __pu_addr __asm__("$7"); \
__pu_val = (x); \
__pu_addr = (long) (ptr); \
__pu_err = -EFAULT; \
if (__access_ok(__pu_addr,size,mask)) { \
register void * __pu_ex __asm__("$8"); \
__pu_ex = &current->tss.ex; \
switch (size) { \
case 1: __put_user_asm(8); break; \
case 2: __put_user_asm(16); break; \
case 4: __put_user_asm(32); break; \
case 8: __put_user_asm(64); break; \
default: __put_user_asm(unknown); break; \
} } __pu_err; })

extern void __put_user_8(void);
extern void __put_user_16(void);
extern void __put_user_32(void);
extern void __put_user_64(void);
extern void __put_user_unknown(void);

#define __put_user_asm(x) \
__asm__ __volatile__("jsr $28,(%5),__put_user_" #x \
:"=r" (__pu_err),"=r" (__pu_val) \
:"1" (__pu_val), "r" (__pu_ex), "r" (__pu_addr), "r" (__put_user_##x) \
:"$2","$3","$4","$5","$6","$28")

#endif /* _ASM_SEGMENT_H */

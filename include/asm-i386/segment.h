#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#define KERNEL_CS	0x10
#define KERNEL_DS	0x18

#define USER_CS		0x23
#define USER_DS		0x2B

#ifndef __ASSEMBLY__

#include <linux/string.h>

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

#define __user_ok(addr,size) \
((size <= 0xC0000000) && (addr <= 0xC0000000 - size))
#define __kernel_ok \
(get_fs() == KERNEL_DS)

extern int __verify_write(const void *, unsigned long);

#if CPU > 386
#define __access_ok(type,addr,size) \
(__kernel_ok || __user_ok(addr,size))
#else
#define __access_ok(type,addr,size) \
(__kernel_ok || (__user_ok(addr,size) && \
  ((type) == VERIFY_READ || wp_works_ok || __verify_write((void *)(addr),(size)))))
#endif /* CPU */

#define access_ok(type,addr,size) \
__access_ok((type),(unsigned long)(addr),(size))

/*
 * Uh, these should become the main single-value transfer routines..
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
__put_user((__typeof__(*(ptr)))(x),__pu_addr,sizeof(*(ptr))); })

#define get_user(x,ptr) ({ \
unsigned long __gu_addr = (unsigned long)(ptr); \
__get_user((x),__gu_addr,sizeof(*(ptr)),__typeof__(*(ptr))); })

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

#define __put_user(x,addr,size) ({ \
int __pu_ret = -EFAULT; \
if (access_ok(VERIFY_WRITE,addr,size)) { \
switch (size) { \
case 1: __put_user_8(x,addr,__pu_ret); break; \
case 2: __put_user_16(x,addr,__pu_ret); break; \
case 4: __put_user_32(x,addr,__pu_ret); break; \
default: __pu_ret = __put_user_bad(); break; \
} } __pu_ret; })

#define __put_user_asm(x,addr,ret,bwl,reg,rtype) \
__asm__ __volatile__( \
	"movl $1f,%0\n\t" \
	"incl %3\n\t" \
	"mov" #bwl " %" reg "1,%2\n\t" \
	"xorl %0,%0\n\t" \
	"decl %3\n1:" \
:"=d" (ret) \
:#rtype (x), "m" (__m(addr)),"m" (current->tss.ex.count), "0" (ret))

#define __put_user_8(x,addr,ret) \
__put_user_asm(x,addr,ret,b,"b","iq")
#define __put_user_16(x,addr,ret) \
__put_user_asm(x,addr,ret,w,"w","ir")
#define __put_user_32(x,addr,ret) \
__put_user_asm(x,addr,ret,l,"","ir")

extern int __put_user_bad(void);

#define __get_user(x,addr,size,type) ({ \
int __gu_ret = -EFAULT; \
unsigned long __gu_val = 0; \
if (access_ok(VERIFY_WRITE,addr,size)) { \
switch (size) { \
case 1: __get_user_8(__gu_val,addr,__gu_ret); break; \
case 2: __get_user_16(__gu_val,addr,__gu_ret); break; \
case 4: __get_user_32(__gu_val,addr,__gu_ret); break; \
default: __gu_ret = __get_user_bad(); break; \
} } x = (type) __gu_val; __gu_ret; })

#define __get_user_asm(x,addr,ret,bwl,reg,rtype) \
__asm__ __volatile__( \
	"movl $1f,%0\n\t" \
	"incl %3\n\t" \
	"mov" #bwl " %2,%" reg "1\n\t" \
	"xorl %0,%0\n\t" \
	"decl %3\n1:" \
:"=d" (ret), #rtype (x) \
:"m" (__m(addr)),"m" (current->tss.ex.count), "0" (ret), "1" (x))

#define __get_user_8(x,addr,ret) \
__get_user_asm(x,addr,ret,b,"b","=q")
#define __get_user_16(x,addr,ret) \
__get_user_asm(x,addr,ret,w,"w","=r")
#define __get_user_32(x,addr,ret) \
__get_user_asm(x,addr,ret,l,"","=r")

extern int __get_user_bad(void);

#define __copy_user(to,from,size) \
__asm__ __volatile__( \
	"movl $3f,%0\n\t" \
	"incl %2\n\t" \
	"rep; movsl\n\t" \
	"testb $2,%b3\n\t" \
	"je 1f\n\t" \
	"movsw\n\t" \
	"subb $2,%b3\n" \
	"1:\t" \
	"testb $1,%b3\n\t" \
	"je 2f\n\t" \
	"movsb\n\t" \
	"decb %b3\n" \
	"2:\t" \
	"decl %2\n" \
	"3:\tlea 0(%3,%1,4),%0" \
	:"=d" (size) \
	:"c" (size >> 2), "m" (current->tss.ex), "q" (size & 3), \
	 "D" (to), "S" (from), "0" (size) \
	:"cx","di","si","memory");

#define copy_to_user(to,from,n) ({ \
unsigned long __cu_to = (unsigned long) (to); \
unsigned long __cu_size = (unsigned long) (n); \
if (__cu_size && __access_ok(VERIFY_WRITE, __cu_to, __cu_size)) \
__copy_user(__cu_to,from,__cu_size); \
__cu_size; })

#define copy_from_user(to,from,n) ({ \
unsigned long __cu_from = (unsigned long) (from); \
unsigned long __cu_size = (unsigned long) (n); \
if (__cu_size && __access_ok(VERIFY_READ, __cu_from, __cu_size)) \
__copy_user(to,__cu_from,__cu_size); \
__cu_size; })

#define __clear_user(addr,size) \
__asm__ __volatile__( \
	"movl $3f,%0\n\t" \
	"incl %2\n\t" \
	"rep; stosl\n\t" \
	"testl $2,%3\n\t" \
	"je 1f\n\t" \
	"stosw\n\t" \
	"subl $2,%3\n" \
	"1:\t" \
	"testl $1,%3\n\t" \
	"je 2f\n\t" \
	"stosb\n\t" \
	"decl %3\n" \
	"2:\t" \
	"decl %2\n" \
	"3:\tlea 0(%3,%1,4),%0" \
	:"=d" (size) \
	:"c" (size >> 2), "m" (current->tss.ex), "r" (size & 3), \
	 "D" (addr), "0" (size), "a" (0) \
	:"cx","di","memory");

#define clear_user(addr,n) ({ \
void * __cl_addr = (addr); \
unsigned long __cl_size = (n); \
if (__cl_size && __access_ok(VERIFY_WRITE, ((unsigned long)(__cl_addr)), __cl_size)) \
__clear_user(__cl_addr, __cl_size); \
__cl_size; })

#define __strncpy_from_user(dst,src,count,res) \
__asm__ __volatile__( \
	"cld\n\t" \
	"movl $3f,%0\n\t" \
	"incl %2\n" \
	"1:\tdecl %1\n\t" \
	"js 2f\n\t" \
	"lodsb\n\t" \
	"stosb\n\t" \
	"testb %%al,%%al\n\t" \
	"jne 1b\n" \
	"2:\t" \
	"incl %1\n\t" \
	"xorl %0,%0\n\t" \
	"decl %2\n" \
	"3:" \
	:"=d" (res), "=r" (count) \
	:"m" (current->tss.ex), "1" (count), "S" (src),"D" (dst),"0" (res) \
	:"si","di","ax","cx","memory")

#define strncpy_from_user(dest,src,count) ({ \
const void * __sc_src = (src); \
unsigned long __sc_count = (count); \
long __sc_res = -EFAULT; \
if (__access_ok(VERIFY_READ, ((unsigned long)(__sc_src)), __sc_count)) { \
	unsigned long __sc_residue = __sc_count; \
	__strncpy_from_user(dest,__sc_src,__sc_count,__sc_res); \
	if (!__sc_res) __sc_res = __sc_residue - __sc_count; \
} __sc_res; })


#endif /* __ASSEMBLY__ */

#endif /* _ASM_SEGMENT_H */

#ifndef __i386_UACCESS_H
#define __i386_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>

#include <asm/segment.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

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
((size <= 0xC0000000UL) && (addr <= 0xC0000000UL - size))
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
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 */
#define put_user(x,ptr) \
__do_put_user((unsigned long)((__typeof__(*(ptr)))(x)),(ptr),(sizeof(*(ptr))))
#define __put_user(x,ptr) \
__do_put_user_nocheck((unsigned long)((__typeof__(*(ptr)))(x)),(ptr),(sizeof(*(ptr))))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

#define __put_user_asm(x,addr,ret,bwl,reg,rtype) \
__asm__ __volatile__( \
	"movl $1f,%0\n\t" \
	"incl %3\n\t" \
	"mov" #bwl " %" reg "1,%2\n\t" \
	"xorl %0,%0\n\t" \
	"decl %3\n1:" \
:"=&d" (ret) \
:#rtype (x), "m" (__m(addr)),"m" (current->tss.ex.count))

extern int __put_user_bad(void);

#define __put_user_size(x,ptr,size,retval) \
switch (size) { \
case 1: __put_user_asm(x,ptr,retval,b,"b","iq"); break; \
case 2: __put_user_asm(x,ptr,retval,w,"w","ir"); break; \
case 4: __put_user_asm(x,ptr,retval,l,"","ir"); break; \
default: retval = __put_user_bad(); }

static inline int __do_put_user(unsigned long x, void * ptr, int size)
{
	int retval = -EFAULT;
	if (access_ok(VERIFY_WRITE, ptr, size))
		__put_user_size(x,ptr,size,retval);
	return retval;
}

#define __do_put_user_nocheck(x, ptr, size) \
({ int retval; __put_user_size(x,ptr,size,retval); retval; })

#define get_user(x,ptr) \
__do_get_user((x),(unsigned long)(ptr),sizeof(*(ptr)),__typeof__(*(ptr)))

#define __get_user(x,ptr) \
__do_get_user_nocheck((x),(unsigned long)(ptr),sizeof(*(ptr)),__typeof__(*(ptr)))

#define __do_get_user(x,ptr,size,type) ({ \
unsigned long __gu_addr = ptr; \
int __gu_ret = -EFAULT; \
unsigned long __gu_val = 0; \
if (access_ok(VERIFY_READ,__gu_addr,size)) { \
switch (size) { \
case 1: __do_get_user_8(__gu_val,__gu_addr,__gu_ret); break; \
case 2: __do_get_user_16(__gu_val,__gu_addr,__gu_ret); break; \
case 4: __do_get_user_32(__gu_val,__gu_addr,__gu_ret); break; \
default: __gu_ret = __do_get_user_bad(); break; \
} } x = (type) __gu_val; __gu_ret; })

#define __do_get_user_nocheck(x,ptr,size,type) ({ \
int __gu_ret; \
unsigned long __gu_val; \
switch (size) { \
case 1: __do_get_user_8(__gu_val,ptr,__gu_ret); break; \
case 2: __do_get_user_16(__gu_val,ptr,__gu_ret); break; \
case 4: __do_get_user_32(__gu_val,ptr,__gu_ret); break; \
default: __gu_ret = __do_get_user_bad(); __gu_val = 0; break; \
} x = (type) __gu_val; __gu_ret; })

#define __do_get_user_asm(x,addr,ret,bwl,reg,rtype) \
__asm__ __volatile__( \
	"movl $1f,%0\n\t" \
	"incl %3\n\t" \
	"mov" #bwl " %2,%" reg "1\n\t" \
	"xorl %0,%0\n\t" \
	"decl %3\n1:" \
:"=&d" (ret), #rtype (x) \
:"m" (__m(addr)),"m" (current->tss.ex.count))

#define __do_get_user_8(x,addr,ret) \
__do_get_user_asm(x,addr,ret,b,"b","=&q")
#define __do_get_user_16(x,addr,ret) \
__do_get_user_asm(x,addr,ret,w,"w","=&r")
#define __do_get_user_32(x,addr,ret) \
__do_get_user_asm(x,addr,ret,l,"","=&r")

extern int __do_get_user_bad(void);

#define __copy_user(to,from,size) \
__asm__ __volatile__( \
	"shrl $2,%1\n\t" \
	"movl $3f,%0\n\t" \
	"incl %3\n\t" \
	"rep; movsl\n\t" \
	"testl $2,%2\n\t" \
	"je 1f\n\t" \
	"movsw\n\t" \
	"subl $2,%2\n" \
	"1:\t" \
	"testl $1,%2\n\t" \
	"je 2f\n\t" \
	"movsb\n\t" \
	"decl %2\n" \
	"2:\t" \
	"decl %3\n" \
	"3:\tlea 0(%2,%1,4),%0" \
	:"=&d" (size) \
	:"c" (size), "r" (size & 3), "m" (current->tss.ex), \
	 "D" (to), "S" (from) \
	:"cx","di","si","memory");

static inline unsigned long __constant_copy_user(void * to, const void * from, unsigned long size)
{
	unsigned long result;

	switch (size & 3) {
	default:
		__asm__ __volatile__(
			"movl $1f,%0\n\t"
			"incl %1\n\t"
			"rep ; movsl\n\t"
			"decl %1\n"
			"1:\tlea 0(,%%ecx,4),%0"
			:"=&d" (result)
			:"m" (current->tss.ex),
			 "S" (from),"D" (to),"c" (size/4)
			:"cx","di","si","memory");
		break;
	case 1:
		__asm__ __volatile__(
			"movl $1f,%0\n\t"
			"incl %3\n\t"
			"rep ; movsl\n\t"
			"movsb\n\t"
			"decl %1\n\t"
			"decl %3\n"
			"1:\tlea 0(%1,%%ecx,4),%0"
			:"=&d" (result)
			:"ab" (1),"m" (current->tss.ex),
			 "S" (from),"D" (to), "c" (size/4)
			:"cx","di","si","memory");
		break;
	case 2:
		__asm__ __volatile__(
			"movl $1f,%0\n\t"
			"incl %2\n\t"
			"rep ; movsl\n\t"
			"movsw\n\t"
			"subl $2,%1\n\t"
			"decl %2\n"
			"1:\tlea 0(%1,%%ecx,4),%0"
			:"=&d" (result)
			:"ab" (2),"m" (current->tss.ex),
			 "S" (from),"D" (to),"c" (size/4)
			:"cx","di","si","memory");
		break;
	case 3:
		__asm__ __volatile__(
			"movl $1f,%0\n\t"
			"incl %2\n\t"
			"rep ; movsl\n\t"
			"movsw\n\t"
			"subl $2,%1\n\t"
			"movsb\n\t"
			"decl %1\n\t"
			"decl %2\n"
			"1:\tlea 0(%1,%%ecx,4),%0"
			:"=&d" (result)
			:"ab" (3),"m" (current->tss.ex),
			 "S" (from),"D" (to),"c" (size/4)
			:"cx","di","si","memory");
		break;
	}
	return result;
}

static inline unsigned long __generic_copy_to_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__copy_user(to,from,n);
	return n;
}

static inline unsigned long __constant_copy_to_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		n = __constant_copy_user(to,from,n);
	return n;
}

static inline unsigned long __generic_copy_from_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		__copy_user(to,from,n);
	return n;
}

static inline unsigned long __constant_copy_from_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		n = __constant_copy_user(to,from,n);
	return n;
}

#define copy_to_user(to,from,n) \
(__builtin_constant_p(n) ? \
 __constant_copy_to_user((to),(from),(n)) : \
 __generic_copy_to_user((to),(from),(n)))

#define copy_from_user(to,from,n) \
(__builtin_constant_p(n) ? \
 __constant_copy_from_user((to),(from),(n)) : \
 __generic_copy_from_user((to),(from),(n)))

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
	:"=&d" (size) \
	:"c" (size >> 2), "m" (current->tss.ex), "r" (size & 3), \
	 "D" (addr), "a" (0) \
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
	:"=&d" (res), "=r" (count) \
	:"m" (current->tss.ex), "1" (count), "S" (src),"D" (dst) \
	:"si","di","ax","memory")

#define strncpy_from_user(dest,src,count) ({ \
const void * __sc_src = (src); \
unsigned long __sc_count = (count); \
long __sc_res = -EFAULT; \
if (__access_ok(VERIFY_READ, ((unsigned long)(__sc_src)), __sc_count)) { \
	unsigned long __sc_residue = __sc_count; \
	__strncpy_from_user(dest,__sc_src,__sc_count,__sc_res); \
	if (!__sc_res) __sc_res = __sc_residue - __sc_count; \
} __sc_res; })


extern inline int verify_area(int type, const void * addr, unsigned long size)
{
	return access_ok(type,addr,size)?0:-EFAULT;
}

#endif /* __i386_UACCESS_H */

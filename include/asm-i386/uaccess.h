#ifndef __i386_UACCESS_H
#define __i386_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })
#define KERNEL_DS	MAKE_MM_SEG(0)
#define USER_DS		MAKE_MM_SEG(3)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->tss.segment)
#define set_fs(x)	(current->tss.segment = (x))

#define segment_eq(a,b)	((a).seg == (b).seg)


/*
 * Address Ok:
 *
 *				     segment
 *			00 (kernel)		11 (user)
 *
 * high		00	1			1
 * two 		01	1			1
 * bits of	10	1			1
 * address	11	1			0
 */
#define __addr_ok(x) \
	((((unsigned long)(x)>>30)&get_fs().seg) != 3)

#define __user_ok(addr,size) \
	((size <= 0xC0000000UL) && (addr <= 0xC0000000UL - size))
#define __kernel_ok \
	(!get_fs().seg)

extern int __verify_write(const void *, unsigned long);

#if CPU > 386
#define __access_ok(type,addr,size) \
	(__kernel_ok || __user_ok(addr,size))
#else
#define __access_ok(type,addr,size) \
	(__kernel_ok || (__user_ok(addr,size) && \
			 ((type) == VERIFY_READ || wp_works_ok || \
			  __verify_write((void *)(addr),(size)))))
#endif /* CPU */

#define access_ok(type,addr,size) \
	__access_ok((type),(unsigned long)(addr),(size))

extern inline int verify_area(int type, const void * addr, unsigned long size)
{
	return access_ok(type,addr,size) ? 0 : -EFAULT;
}


/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	unsigned long insn, fixup;
};

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_exception_table(unsigned long);


/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
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
#define get_user(x,ptr) \
  __get_user_check((x),(ptr),sizeof(*(ptr)))
#define put_user(x,ptr) \
  __put_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

#define __get_user(x,ptr) \
  __get_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __put_user(x,ptr) \
  __put_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

/*
 * The "xxx_ret" versions return constant specified in third argument, if
 * something bad happens. These macros can be optimized for the
 * case of just returning from the function xxx_ret is used.
 */

#define put_user_ret(x,ptr,ret) ({ \
if (put_user(x,ptr)) return ret; })

#define get_user_ret(x,ptr,ret) ({ \
if (get_user(x,ptr)) return ret; })

#define __put_user_ret(x,ptr,ret) ({ \
if (__put_user(x,ptr)) return ret; })

#define __get_user_ret(x,ptr,ret) ({ \
if (__get_user(x,ptr)) return ret; })



extern long __put_user_bad(void);

#define __put_user_nocheck(x,ptr,size)			\
({							\
	long __pu_err;					\
	__put_user_size((x),(ptr),(size),__pu_err);	\
	__pu_err;					\
})

#define __put_user_check(x,ptr,size)				\
({								\
	long __pu_err = -EFAULT;				\
	__typeof__(*(ptr)) *__pu_addr = (ptr);			\
	if (access_ok(VERIFY_WRITE,__pu_addr,size))		\
		__put_user_size((x),__pu_addr,(size),__pu_err);	\
	__pu_err;						\
})

#define __put_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	switch (size) {							\
	  case 1: __put_user_asm(x,ptr,retval,"b","b","iq"); break;	\
	  case 2: __put_user_asm(x,ptr,retval,"w","w","ir"); break;	\
	  case 4: __put_user_asm(x,ptr,retval,"l","","ir"); break;	\
	  default: __put_user_bad();					\
	}								\
} while (0)

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */
#define __put_user_asm(x, addr, err, itype, rtype, ltype)	\
	__asm__ __volatile__(					\
		"1:	mov"itype" %"rtype"1,%2\n"		\
		"2:\n"						\
		".section .fixup,\"ax\"\n"			\
		"3:	movl %3,%0\n"				\
		"	jmp 2b\n"				\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"	.align 4\n"				\
		"	.long 1b,3b\n"				\
		".previous"					\
		: "=r"(err)					\
		: ltype (x), "m"(__m(addr)), "i"(-EFAULT), "0"(err))


#define __get_user_nocheck(x,ptr,size)				\
({								\
	long __gu_err, __gu_val;				\
	__get_user_size(__gu_val,(ptr),(size),__gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;			\
	__gu_err;						\
})

#define __get_user_check(x,ptr,size)					\
({									\
	long __gu_err = -EFAULT, __gu_val = 0;				\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);			\
	if (access_ok(VERIFY_READ,__gu_addr,size))			\
		__get_user_size(__gu_val,__gu_addr,(size),__gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

extern long __get_user_bad(void);

#define __get_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	switch (size) {							\
	  case 1: __get_user_asm(x,ptr,retval,"b","b","=q"); break;	\
	  case 2: __get_user_asm(x,ptr,retval,"w","w","=r"); break;	\
	  case 4: __get_user_asm(x,ptr,retval,"l","","=r"); break;	\
	  default: (x) = __get_user_bad();				\
	}								\
} while (0)

#define __get_user_asm(x, addr, err, itype, rtype, ltype)	\
	__asm__ __volatile__(					\
		"1:	mov"itype" %2,%"rtype"1\n"		\
		"2:\n"						\
		".section .fixup,\"ax\"\n"			\
		"3:	movl %3,%0\n"				\
		"	xor"itype" %"rtype"1,%"rtype"1\n"	\
		"	jmp 2b\n"				\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"	.align 4\n"				\
		"	.long 1b,3b\n"				\
		".previous"					\
		: "=r"(err), ltype (x)				\
		: "m"(__m(addr)), "i"(-EFAULT), "0"(err))


/*
 * Copy To/From Userspace
 */

/* Generic arbitrary sized copy.  */
#define __copy_user(to,from,size)					\
	__asm__ __volatile__(						\
		"0:	rep; movsl\n"					\
		"	movl %1,%0\n"					\
		"1:	rep; movsb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"3:	lea 0(%1,%0,4),%0\n"				\
		"	jmp 2b\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.align 4\n"					\
		"	.long 0b,3b\n"					\
		"	.long 1b,2b\n"					\
		".previous"						\
		: "=c"(size)						\
		: "r"(size & 3), "0"(size / 4), "D"(to), "S"(from)	\
		: "di", "si", "memory")

/* Optimize just a little bit when we know the size of the move. */
#define __constant_copy_user(to, from, size)			\
do {								\
	switch (size & 3) {					\
	default:						\
		__asm__ __volatile__(				\
			"0:	rep; movsl\n"			\
			"1:\n"					\
			".section .fixup,\"ax\"\n"		\
			"2:	shl $2,%0\n"			\
			"	jmp 1b\n"			\
			".previous\n"				\
			".section __ex_table,\"a\"\n"		\
			"	.align 4\n"			\
			"	.long 0b,2b\n"			\
			".previous"				\
			: "=c"(size)				\
			: "S"(from), "D"(to), "0"(size/4)	\
			: "di", "si", "memory");		\
		break;						\
	case 1:							\
		__asm__ __volatile__(				\
			"0:	rep; movsl\n"			\
			"1:	movsb\n"			\
			"2:\n"					\
			".section .fixup,\"ax\"\n"		\
			"3:	shl $2,%0\n"			\
			"4:	incl %0\n"			\
			"	jmp 2b\n"			\
			".previous\n"				\
			".section __ex_table,\"a\"\n"		\
			"	.align 4\n"			\
			"	.long 0b,3b\n"			\
			"	.long 1b,4b\n"			\
			".previous"				\
			: "=c"(size)				\
			: "S"(from), "D"(to), "0"(size/4)	\
			: "di", "si", "memory");		\
		break;						\
	case 2:							\
		__asm__ __volatile__(				\
			"0:	rep; movsl\n"			\
			"1:	movsw\n"			\
			"2:\n"					\
			".section .fixup,\"ax\"\n"		\
			"3:	shl $2,%0\n"			\
			"4:	addl $2,%0\n"			\
			"	jmp 2b\n"			\
			".previous\n"				\
			".section __ex_table,\"a\"\n"		\
			"	.align 4\n"			\
			"	.long 0b,3b\n"			\
			"	.long 1b,4b\n"			\
			".previous"				\
			: "=c"(size)				\
			: "S"(from), "D"(to), "0"(size/4)	\
			: "di", "si", "memory");		\
		break;						\
	case 3:							\
		__asm__ __volatile__(				\
			"0:	rep; movsl\n"			\
			"1:	movsw\n"			\
			"2:	movsb\n"			\
			"3:\n"					\
			".section .fixup,\"ax\"\n"		\
			"4:	shl $2,%0\n"			\
			"5:	addl $2,%0\n"			\
			"6:	incl %0\n"			\
			"	jmp 3b\n"			\
			".previous\n"				\
			".section __ex_table,\"a\"\n"		\
			"	.align 4\n"			\
			"	.long 0b,4b\n"			\
			"	.long 1b,5b\n"			\
			"	.long 2b,6b\n"			\
			".previous"				\
			: "=c"(size)				\
			: "S"(from), "D"(to), "0"(size/4)	\
			: "di", "si", "memory");		\
		break;						\
	}							\
} while (0)

static inline unsigned long
__generic_copy_to_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__copy_user(to,from,n);
	return n;
}

static inline unsigned long
__constant_copy_to_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__constant_copy_user(to,from,n);
	return n;
}

static inline unsigned long
__generic_copy_from_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		__copy_user(to,from,n);
	return n;
}

static inline unsigned long
__constant_copy_from_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		__constant_copy_user(to,from,n);
	return n;
}

static inline unsigned long
__generic_copy_to_user_nocheck(void *to, const void *from, unsigned long n)
{
	__copy_user(to,from,n);
	return n;
}

static inline unsigned long
__constant_copy_to_user_nocheck(void *to, const void *from, unsigned long n)
{
	__constant_copy_user(to,from,n);
	return n;
}

static inline unsigned long
__generic_copy_from_user_nocheck(void *to, const void *from, unsigned long n)
{
	__copy_user(to,from,n);
	return n;
}

static inline unsigned long
__constant_copy_from_user_nocheck(void *to, const void *from, unsigned long n)
{
	__constant_copy_user(to,from,n);
	return n;
}

#define copy_to_user(to,from,n)				\
	(__builtin_constant_p(n) ?			\
	 __constant_copy_to_user((to),(from),(n)) :	\
	 __generic_copy_to_user((to),(from),(n)))

#define copy_from_user(to,from,n)			\
	(__builtin_constant_p(n) ?			\
	 __constant_copy_from_user((to),(from),(n)) :	\
	 __generic_copy_from_user((to),(from),(n)))

#define copy_to_user_ret(to,from,n,retval) ({ \
if (copy_to_user(to,from,n)) \
	return retval; \
})

#define copy_from_user_ret(to,from,n,retval) ({ \
if (copy_from_user(to,from,n)) \
	return retval; \
})

#define __copy_to_user(to,from,n)			\
	(__builtin_constant_p(n) ?			\
	 __constant_copy_to_user_nocheck((to),(from),(n)) :	\
	 __generic_copy_to_user_nocheck((to),(from),(n)))

#define __copy_from_user(to,from,n)			\
	(__builtin_constant_p(n) ?			\
	 __constant_copy_from_user_nocheck((to),(from),(n)) :	\
	 __generic_copy_from_user_nocheck((to),(from),(n)))


/*
 * Zero Userspace
 */

#define __do_clear_user(addr,size)						\
	__asm__ __volatile__(						\
		"0:	rep; stosl\n"					\
		"	movl %1,%0\n"					\
		"1:	rep; stosb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"3:	lea 0(%1,%0,4),%0\n"				\
		"	jmp 2b\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.align 4\n"					\
		"	.long 0b,3b\n"					\
		"	.long 1b,2b\n"					\
		".previous"						\
		: "=c"(size)						\
		: "r"(size & 3), "0"(size / 4), "D"(addr), "a"(0)	\
		: "di")

static inline unsigned long
clear_user(void *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}

static inline unsigned long
__clear_user(void *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}


/*
 * Copy a null terminated string from userspace.
 */

#define __do_strncpy_from_user(dst,src,count,res)			   \
	__asm__ __volatile__(						   \
		"	testl %1,%1\n"					   \
		"	jz 2f\n"					   \
		"0:	lodsb\n"					   \
		"	stosb\n"					   \
		"	testb %%al,%%al\n"				   \
		"	jz 1f\n"					   \
		"	decl %1\n"					   \
		"	jnz 0b\n"					   \
		"1:	subl %1,%0\n"					   \
		"2:\n"							   \
		".section .fixup,\"ax\"\n"				   \
		"3:	movl %2,%0\n"					   \
		"	jmp 2b\n"					   \
		".previous\n"						   \
		".section __ex_table,\"a\"\n"				   \
		"	.align 4\n"					   \
		"	.long 0b,3b\n"					   \
		".previous"						   \
		: "=d"(res), "=c"(count)				   \
		: "i"(-EFAULT), "0"(count), "1"(count), "S"(src), "D"(dst) \
		: "si", "di", "ax", "memory")

static inline long
__strncpy_from_user(char *dst, const char *src, long count)
{
	long res;
	__do_strncpy_from_user(dst, src, count, res);
	return res;
}

static inline long
strncpy_from_user(char *dst, const char *src, long count)
{
	long res = -EFAULT;
	if (access_ok(VERIFY_READ, src, 1))
		__do_strncpy_from_user(dst, src, count, res);
	return res;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */

extern inline long strlen_user(const char *s)
{
	unsigned long res;

	__asm__ __volatile__(
		"0:	repne; scasb\n"
		"	notl %0\n"
		"1:\n"
		".section .fixup,\"ax\"\n"
		"2:	xorl %0,%0\n"
		"	jmp 1b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 0b,2b\n"
		".previous"
		:"=c" (res), "=D" (s)
		:"1" (s), "a" (0), "0" (-__addr_ok(s)));
	return res & -__addr_ok(s);
}

#endif /* __i386_UACCESS_H */

#ifndef _ASMARM_UACCESS_H
#define _ASMARM_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

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

#include <asm/proc/uaccess.h>

extern inline int verify_area(int type, const void * addr, unsigned long size)
{
	return access_ok(type,addr,size) ? 0 : -EFAULT;
}

/*
 * Single-value transfer routines.  They automatically use the right
 * size if we just have the right pointer type.
 *
 * The "__xxx" versions of the user access functions do not verify the
 * address space - it must have been done previously with a separate
 * "access_ok()" call.
 *
 * The "xxx_ret" versions return constant specified in the third
 * argument if something bad happens.
 */
#define get_user(x,p)		__get_user_check((x),(p),sizeof(*(p)))
#define __get_user(x,p)		__get_user_nocheck((x),(p),sizeof(*(p)))
#define get_user_ret(x,p,r)	({ if (get_user(x,p)) return r; })
#define __get_user_ret(x,p,r)	({ if (__get_user(x,p)) return r; })

#define put_user(x,p)		__put_user_check((__typeof(*(p)))(x),(p),sizeof(*(p)))
#define __put_user(x,p)		__put_user_nocheck((__typeof(*(p)))(x),(p),sizeof(*(p)))
#define put_user_ret(x,p,r)	({ if (put_user(x,p)) return r; })
#define __put_user_ret(x,p,r)	({ if (__put_user(x,p)) return r; })

static __inline__ unsigned long copy_from_user(void *to, const void *from, unsigned long n)
{
	char *end = (char *)to + n;
	if (access_ok(VERIFY_READ, from, n)) {
		__do_copy_from_user(to, from, n);
		if (n) memset(end - n, 0, n);
	}
	return n;
}

static __inline__ unsigned long __copy_from_user(void *to, const void *from, unsigned long n)
{
	__do_copy_from_user(to, from, n);
	return n;
}

#define copy_from_user_ret(t,f,n,r)					\
	({ if (copy_from_user(t,f,n)) return r; })

static __inline__ unsigned long copy_to_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_copy_to_user(to, from, n);
	return n;
}

static __inline__ unsigned long __copy_to_user(void *to, const void *from, unsigned long n)
{
	__do_copy_to_user(to, from, n);
	return n;
}

#define copy_to_user_ret(t,f,n,r)					\
	({ if (copy_to_user(t,f,n)) return r; })

static __inline__ unsigned long clear_user (void *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}

static __inline__ unsigned long __clear_user (void *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}

static __inline__ long strncpy_from_user (char *dst, const char *src, long count)
{
	long res = -EFAULT;
	if (access_ok(VERIFY_READ, src, 1))
		__do_strncpy_from_user(dst, src, count, res);
	return res;
}

static __inline__ long __strncpy_from_user (char *dst, const char *src, long count)
{
	long res;
	__do_strncpy_from_user(dst, src, count, res);
	return res;
}

extern __inline__ long strlen_user (const char *s)
{
	unsigned long res = 0;

	if (__addr_ok(s))
		__do_strlen_user (s, res);

	return res;
}

/*
 * These are the work horses of the get/put_user functions
 */
#define __get_user_check(x,ptr,size)					\
({									\
	long __gu_err = -EFAULT, __gu_val = 0;				\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);			\
	if (access_ok(VERIFY_READ,__gu_addr,size))			\
		__get_user_size(__gu_val,__gu_addr,(size),__gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

#define __get_user_nocheck(x,ptr,size)					\
({									\
	long __gu_err = 0, __gu_val = 0;				\
	__get_user_size(__gu_val,(ptr),(size),__gu_err);		\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

#define __put_user_check(x,ptr,size)					\
({									\
	long __pu_err = -EFAULT;					\
	__typeof__(*(ptr)) *__pu_addr = (ptr);				\
	if (access_ok(VERIFY_WRITE,__pu_addr,size))			\
		__put_user_size((x),__pu_addr,(size),__pu_err);		\
	__pu_err;							\
})

#define __put_user_nocheck(x,ptr,size)					\
({									\
	long __pu_err = 0;						\
	__put_user_size((x),(ptr),(size),__pu_err);			\
	__pu_err;							\
})

extern long __get_user_bad(void);

#define __get_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	switch (size) {							\
	case 1:	__get_user_asm_byte(x,ptr,retval);	break;		\
	case 2:	__get_user_asm_half(x,ptr,retval);	break;		\
	case 4:	__get_user_asm_word(x,ptr,retval);	break;		\
	default: (x) = __get_user_bad();				\
	}								\
} while (0)

extern long __put_user_bad(void);

#define __put_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	switch (size) {							\
	case 1: __put_user_asm_byte(x,ptr,retval);	break;		\
	case 2: __put_user_asm_half(x,ptr,retval);	break;		\
	case 4: __put_user_asm_word(x,ptr,retval);	break;		\
	default: __put_user_bad();					\
	}								\
} while (0)

#endif /* _ASMARM_UACCESS_H */

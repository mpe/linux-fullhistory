#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

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
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * As the alpha uses the same address space for kernel and user
 * data, we can just do these as direct assignments.  (Of course, the
 * exception handling means that it's no longer "just"...)
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof/typeof is ok)
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

#define __copy_tofrom_user(to,from,n,v)					    \
({									    \
	register void * __cu_to __asm__("$6") = (to);			    \
	register const void * __cu_from __asm__("$7") = (from);		    \
	register long __cu_len __asm__("$0") = (n);			    \
	if (__access_ok(((long)(v)),__cu_len,__access_mask)) {		    \
		register void * __cu_ex __asm__("$8");			    \
		__cu_ex = &current->tss.ex;				    \
		__asm__ __volatile__(					    \
			"jsr $28,(%7),__copy_user"			    \
			: "=r" (__cu_len), "=r" (__cu_from), "=r" (__cu_to) \
			: "0" (__cu_len), "1" (__cu_from), "2" (__cu_to),   \
			  "r" (__cu_ex), "r" (__copy_user)		    \
			: "$1","$2","$3","$4","$5","$28","memory");	    \
	}								    \
	__cu_len;							    \
})

extern void __get_user_unknown(void);

#define __get_user(x,ptr,size,mask)				\
({								\
	long __gu_err = -EFAULT, __gu_val = 0;			\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);		\
	if (__access_ok((long)__gu_addr,size,mask)) {		\
		long __gu_ex_count = current->tss.ex.count;	\
		switch (size) {					\
		case 1: __get_user_8; break;			\
		case 2: __get_user_16; break;			\
		case 4: __get_user_32; break;			\
		case 8: __get_user_64; break;			\
		default: __get_user_unknown(); break;		\
		}						\
	}							\
	(x) = (__typeof__(*(ptr))) __gu_val;			\
	__gu_err;						\
})

#define __get_user_64							   \
	__asm__("/* Inline __get_user_64 */\n\t"			   \
		"br $28,1f\n\t"		/* set up exception address */	   \
		"br 2f\n"		/* exception! */		   \
		"1:\t"							   \
		"stq %5,%3\n\t"		/* store inc'ed exception count */ \
		"ldq %1,%2\n\t"		/* actual data load */		   \
		"stq %4,%3\n\t"		/* restore exception count */	   \
		"clr %0\n"		/* no exception: error = 0 */	   \
		"2:\t/* End __get_user_64 */"				   \
		: "=r"(__gu_err), "=r"(__gu_val)			   \
		: "m"(*__gu_addr), "m"(current->tss.ex.count),		   \
		  "r"(__gu_ex_count), "r"(__gu_ex_count+1),		   \
		  "0"(__gu_err), "1"(__gu_val)				   \
		: "$28")

#define __get_user_32							   \
	__asm__("/* Inline __get_user_32 */\n\t"			   \
		"br $28,1f\n\t"		/* set up exception address */	   \
		"br 2f\n"		/* exception! */		   \
		"1:\t"							   \
		"stq %5,%3\n\t"		/* store inc'ed exception count */ \
		"ldl %1,%2\n\t"		/* actual data load */		   \
		"stq %4,%3\n\t"		/* restore exception count */	   \
		"clr %0\n"		/* no exception: error = 0 */	   \
		"2:\t/* End __get_user_32 */"				   \
		: "=r"(__gu_err), "=r"(__gu_val)			   \
		: "m"(*__gu_addr), "m"(current->tss.ex.count),		   \
		  "r"(__gu_ex_count), "r"(__gu_ex_count+1),		   \
		  "0"(__gu_err), "1"(__gu_val)				   \
		: "$28")

#define __get_user_16							   \
	__asm__("/* Inline __get_user_16 */\n\t"			   \
		"br $28,1f\n\t"		/* set up exception address */	   \
		"br 2f\n"		/* exception! */		   \
		"1:\t"							   \
		"stq %6,%4\n\t"		/* store inc'ed exception count */ \
		"ldq_u %1,%2\n\t"	/* actual data load */		   \
		"stq %5,%4\n\t"		/* restore exception count */	   \
		"clr %0\n\t"		/* no exception: error = 0 */	   \
		"extwl %1,%3,%1\n"	/* extract the short */		   \
		"2:\t/* End __get_user_16 */"				   \
		: "=r"(__gu_err), "=r"(__gu_val)			   \
		: "m"(*__gu_addr), "r"(__gu_addr),			   \
		  "m"(current->tss.ex.count), "r"(__gu_ex_count),	   \
		  "r"(__gu_ex_count+1), "0"(__gu_err), "1"(__gu_val)	   \
		: "$28")

#define __get_user_8							   \
	__asm__("/* Inline __get_user_8 */\n\t"				   \
		"br $28,1f\n\t"		/* set up exception address */	   \
		"br 2f\n"		/* exception! */		   \
		"1:\t"							   \
		"stq %6,%4\n\t"		/* store inc'ed exception count */ \
		"ldq_u %1,%2\n\t"	/* actual data load */		   \
		"stq %5,%4\n\t"		/* restore exception count */	   \
		"clr %0\n\t"		/* no exception: error = 0 */	   \
		"extbl %1,%3,%1\n"	/* extract the byte */		   \
		"2:\t/* End __get_user_8 */"				   \
		: "=r"(__gu_err), "=r"(__gu_val)			   \
		: "m"(*__gu_addr), "r"(__gu_addr),			   \
		  "m"(current->tss.ex.count), "r"(__gu_ex_count),	   \
		  "r"(__gu_ex_count+1), "0"(__gu_err), "1"(__gu_val)	   \
		: "$28")

extern void __put_user_unknown(void);

#define __put_user(x,ptr,size,mask)				\
({								\
	long __pu_err = -EFAULT;				\
	__typeof__(*(ptr)) *__pu_addr = (ptr);			\
        __typeof__(*(ptr)) __pu_val = (x);			\
	if (__access_ok((long)__pu_addr,size,mask)) {		\
		long __pu_ex_count = current->tss.ex.count;	\
		switch (size) {					\
		case 1: __put_user_8; break;			\
		case 2: __put_user_16; break;			\
		case 4: __put_user_32; break;			\
		case 8: __put_user_64; break;			\
		default: __put_user_unknown(); break;		\
		}						\
	}							\
	__pu_err;						\
})

#define __put_user_64							   \
	__asm__("/* Inline __put_user_64 */\n\t"			   \
		"br $28,1f\n\t"		/* set up exception address */	   \
		"br 2f\n"		/* exception! */		   \
		"1:\t"							   \
		"stq %5,%3\n\t"		/* store inc'ed exception count */ \
		"stq %2,%1\n\t"		/* actual data store */		   \
		"stq %4,%3\n\t"		/* restore exception count */	   \
		"clr %0\n"		/* no exception: error = 0 */	   \
		"2:\t/* End __put_user_64 */"				   \
		: "=r"(__pu_err), "=m"(*__pu_addr)			   \
		: "r"(__pu_val), "m"(current->tss.ex.count),		   \
		  "r"(__pu_ex_count), "r"(__pu_ex_count+1),		   \
		  "0"(__pu_err)						   \
		: "$28")

#define __put_user_32							   \
	__asm__("/* Inline __put_user_32 */\n\t"			   \
		"br $28,1f\n\t"		/* set up exception address */	   \
		"br 2f\n"		/* exception! */		   \
		"1:\t"							   \
		"stq %5,%3\n\t"		/* store inc'ed exception count */ \
		"stl %2,%1\n\t"		/* actual data store */		   \
		"stq %4,%3\n\t"		/* restore exception count */	   \
		"clr %0\n"		/* no exception: error = 0 */	   \
		"2:\t/* End __put_user_32 */"				   \
		: "=r"(__pu_err), "=m"(*__pu_addr)			   \
		: "r"(__pu_val), "m"(current->tss.ex.count),		   \
		  "r"(__pu_ex_count), "r"(__pu_ex_count+1),		   \
		  "0"(__pu_err)						   \
		: "$28")

#define __put_user_16							   \
	__asm__("/* Inline __put_user_16 */\n\t"			   \
		"br $28,1f\n\t"		/* set up exception address */	   \
		"lda %0,%7\n\t"		/* exception! error = -EFAULT */   \
		"br 2f\n"						   \
		"1:\t"							   \
		"stq %6,%4\n\t"		/* store inc'ed exception count */ \
		"ldq_u %0,%1\n\t"	/* masked data store */		   \
		"inswl %2,%3,%2\n\t"					   \
		"mskwl %0,%3,%0\n\t"					   \
		"or %0,%2,%2\n\t"					   \
		"stq_u %2,%1\n\t"					   \
		"stq %5,%4\n\t"		/* restore exception count */	   \
		"clr %0\n"		/* no exception: error = 0 */	   \
		"2:\t/* End __put_user_16 */"				   \
		: "=r"(__pu_err), "=m"(*__pu_addr), "=r"(__pu_val)	   \
		: "r"(__pu_addr), "m"(current->tss.ex.count),		   \
		  "r"(__pu_ex_count), "r"(__pu_ex_count+1), "i"(-EFAULT),  \
		  "2"(__pu_val)						   \
		: "$28")

#define __put_user_8							   \
	__asm__("/* Inline __put_user_8 */\n\t"				   \
		"br $28,1f\n\t"		/* set up exception address */	   \
		"lda %0,%7\n\t"		/* exception! error = -EFAULT */   \
		"br 2f\n"						   \
		"1:\t"							   \
		"stq %6,%4\n\t"		/* store inc'ed exception count */ \
		"ldq_u %0,%1\n\t"	/* masked data store */		   \
		"insbl %2,%3,%2\n\t"					   \
		"mskbl %0,%3,%0\n\t"					   \
		"or %0,%2,%2\n\t"					   \
		"stq_u %2,%1\n\t"					   \
		"stq %5,%4\n\t"		/* restore exception count */	   \
		"clr %0\n"		/* no exception: error = 0 */	   \
		"2:\t/* End __put_user_8 */"				   \
		: "=r"(__pu_err), "=m"(*__pu_addr), "=r"(__pu_val)	   \
		: "r"(__pu_addr), "m"(current->tss.ex.count),		   \
		  "r"(__pu_ex_count), "r"(__pu_ex_count+1), "i"(-EFAULT),  \
		  "2"(__pu_val)						   \
		: "$28")


extern void __clear_user(void);

#define clear_user(to,n)						\
({									\
	register void * __cl_to __asm__("$6") = (to);			\
	register long __cl_len __asm__("$0") = (n);			\
	if (__access_ok(((long)__cl_to),__cl_len,__access_mask)) {	\
		register void * __cl_ex __asm__("$7");			\
		__cl_ex = &current->tss.ex;				\
		__asm__ __volatile__(					\
			"jsr $28,(%2),__clear_user"			\
			: "=r"(__cl_len), "=r"(__cl_to)			\
			: "r"(__clear_user), "r"(__cl_ex),		\
			  "0"(__cl_len), "1"(__cl_to)			\
			: "$1","$2","$3","$4","$5","$28","memory");	\
	}								\
	__cl_len;							\
})


/* Returns: -EFAULT if exception before terminator, N if the entire
   buffer filled, else strlen.  */

struct exception_struct;
extern long __strncpy_from_user(char *__to, const char *__from,
				long __to_len, struct exception_struct *);

#define strncpy_from_user(to,from,n)					      \
({									      \
	char * __sfu_to = (to);						      \
	const char * __sfu_from = (from);				      \
	long __sfu_len = (n), __sfu_ret = -EFAULT;			      \
	if (__access_ok(((long)__sfu_from),__sfu_len,__access_mask)) {	      \
		__sfu_ret = __strncpy_from_user(__sfu_to,__sfu_from,	      \
						__sfu_len, &current->tss.ex); \
	__sfu_ret;							      \
})

#endif /* _ASM_SEGMENT_H */

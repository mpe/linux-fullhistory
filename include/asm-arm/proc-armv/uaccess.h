/*
 * linux/include/asm-arm/proc-armv/uaccess.h
 */

/*
 * The fs functions are implemented on the ARMV3 and V4 architectures
 * using the domain register.
 *
 *  DOMAIN_IO     - domain 2 includes all IO only
 *  DOMAIN_KERNEL - domain 1 includes all kernel memory only
 *  DOMAIN_USER   - domain 0 includes all user memory only
 */

#include <asm/hardware.h>

#define DOMAIN_CLIENT	1
#define DOMAIN_MANAGER	3
 
#define DOMAIN_USER_CLIENT	((DOMAIN_CLIENT) << 0)
#define DOMAIN_USER_MANAGER	((DOMAIN_MANAGER) << 0)

#define DOMAIN_KERNEL_CLIENT	((DOMAIN_CLIENT) << 2)
#define DOMAIN_KERNEL_MANAGER	((DOMAIN_MANAGER) << 2)

#define DOMAIN_IO_CLIENT	((DOMAIN_CLIENT) << 4)
#define DOMAIN_IO_MANAGER	((DOMAIN_MANAGER) << 4)

/*
 * When we want to access kernel memory in the *_user functions,
 * we change the domain register to KERNEL_DS, thus allowing
 * unrestricted access
 */
#define KERNEL_DOMAIN	(DOMAIN_USER_CLIENT | DOMAIN_KERNEL_MANAGER | DOMAIN_IO_CLIENT)
#define USER_DOMAIN	(DOMAIN_USER_CLIENT | DOMAIN_KERNEL_CLIENT  | DOMAIN_IO_CLIENT)

/*
 * Note that this is actually 0x1,0000,0000
 */
#define KERNEL_DS	0x00000000
#define USER_DS		PAGE_OFFSET

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->addr_limit)

#define segment_eq(a,b)	((a) == (b))

extern __inline__ void set_fs (mm_segment_t fs)
{
	current->addr_limit = fs;

	__asm__ __volatile__("mcr	p15, 0, %0, c3, c0" :
		: "r" (fs ? USER_DOMAIN : KERNEL_DOMAIN));
}

/* We use 33-bit arithmetic here... */
#define __range_ok(addr,size) ({ \
	unsigned long flag, sum; \
	__asm__ __volatile__("adds %1, %2, %3; sbcccs %1, %1, %0; movcc %0, #0" \
		: "=&r" (flag), "=&r" (sum) \
		: "r" (addr), "Ir" (size), "0" (current->addr_limit) \
		: "cc"); \
	flag; })

#define __addr_ok(addr) ({ \
	unsigned long flag; \
	__asm__ __volatile__("cmp %2, %0; movlo %0, #0" \
		: "=&r" (flag) \
		: "0" (current->addr_limit), "r" (addr) \
		: "cc"); \
	(flag == 0); })

#define access_ok(type,addr,size) (__range_ok(addr,size) == 0)

#define __put_user_asm_byte(x,addr,err)				\
	__asm__ __volatile__(					\
	"1:	strbt	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mvn	%0, %3\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "=r" (err)						\
	: "r" (x), "r" (addr), "i" (EFAULT), "0" (err))

#define __put_user_asm_half(x,addr,err)				\
({								\
	unsigned long __temp = (unsigned long)(x);		\
	__asm__ __volatile__(					\
	"1:	strbt	%1,[%3],#0\n"				\
	"2:	strbt	%2,[%4],#0\n"				\
	"3:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"4:	mvn	%0, %5\n"				\
	"	b	3b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 4b\n"				\
	"	.long	2b, 4b\n"				\
	"	.previous"					\
	: "=r" (err)						\
	: "r" (__temp), "r" (__temp >> 8),			\
	  "r" (addr), "r" ((int)(addr) + 1),			\
	   "i" (EFAULT), "0" (err));				\
})

#define __put_user_asm_word(x,addr,err)				\
	__asm__ __volatile__(					\
	"1:	strt	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mvn	%0, %3\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "=r" (err)						\
	: "r" (x), "r" (addr), "i" (EFAULT), "0" (err))

#define __get_user_asm_byte(x,addr,err)				\
	__asm__ __volatile__(					\
	"1:	ldrbt	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mvn	%0, %3\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "=r" (err), "=r" (x)					\
	: "r" (addr), "i" (EFAULT), "0" (err))

#define __get_user_asm_half(x,addr,err)				\
({								\
	unsigned long __temp;					\
	__asm__ __volatile__(					\
	"1:	ldrbt	%1,[%3],#0\n"				\
	"2:	ldrbt	%2,[%4],#0\n"				\
	"	orr	%1, %1, %2, lsl #8\n"			\
	"3:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"4:	mvn	%0, %5\n"				\
	"	b	3b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 4b\n"				\
	"	.long	2b, 4b\n"				\
	"	.previous"					\
	: "=r" (err), "=r" (x), "=&r" (__temp)			\
	: "r" (addr), "r" ((int)(addr) + 1),			\
	   "i" (EFAULT), "0" (err));				\
})


#define __get_user_asm_word(x,addr,err)				\
	__asm__ __volatile__(					\
	"1:	ldrt	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mvn	%0, %3\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "=r" (err), "=r" (x)					\
	: "r" (addr), "i" (EFAULT), "0" (err))

extern unsigned long __arch_copy_from_user(void *to, const void *from, unsigned long n);
#define __do_copy_from_user(to,from,n)				\
	(n) = __arch_copy_from_user(to,from,n)

extern unsigned long __arch_copy_to_user(void *to, const void *from, unsigned long n);
#define __do_copy_to_user(to,from,n)				\
	(n) = __arch_copy_to_user(to,from,n)

extern unsigned long __arch_clear_user(void *addr, unsigned long n);
#define __do_clear_user(addr,sz)				\
	(sz) = __arch_clear_user(addr,sz)

extern unsigned long __arch_strncpy_from_user(char *to, const char *from, unsigned long count);
#define __do_strncpy_from_user(dst,src,count,res)		\
	(res) = __arch_strncpy_from_user(dst,src,count)

extern unsigned long __arch_strlen_user(const char *s);
#define __do_strlen_user(s,res)					\
	(res) = __arch_strlen_user(s)

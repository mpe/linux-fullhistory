/*
 * linux/include/asm-arm/proc-armo/segment.h
 *
 * Copyright (C) 1996 Russell King
 */

/*
 * The fs functions are implemented on the ARM2 and ARM3 architectures
 * manually.
 * Use *_user functions to access user memory with faulting behaving
 *   as though the user is accessing the memory.
 * Use set_fs(get_ds()) and then the *_user functions to allow them to
 *   access kernel memory.
 */

/*
 * These are the values used to represent the user `fs' and the kernel `ds'
 */
#define KERNEL_DS	0x03000000
#define USER_DS   	0x02000000

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->addr_limit)
#define segment_eq(a,b)	((a) == (b))

extern uaccess_t uaccess_user, uaccess_kernel;

extern __inline__ void set_fs (mm_segment_t fs)
{
	current->addr_limit = fs;
	current->tss.uaccess = fs == USER_DS ? &uaccess_user : &uaccess_kernel;
}

#define __range_ok(addr,size) ({					\
	unsigned long flag, sum;					\
	__asm__ __volatile__("subs %1, %0, %3; cmpcs %1, %2; movcs %0, #0" \
		: "=&r" (flag), "=&r" (sum)				\
		: "r" (addr), "Ir" (size), "0" (current->addr_limit)	\
		: "cc");						\
	flag; })

#define __addr_ok(addr) ({						\
	unsigned long flag;						\
	__asm__ __volatile__("cmp %2, %0; movlo %0, #0"			\
		: "=&r" (flag)						\
		: "0" (current->addr_limit), "r" (addr)			\
		: "cc");						\
	(flag == 0); })

#define access_ok(type,addr,size) (__range_ok(addr,size) == 0)

#define __put_user_asm_byte(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %1\n"					\
	"	mov	r1, %2\n"					\
	"	mov	r2, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r2\n"					\
	: "=r" (err)							\
	: "r" (x), "r" (addr), "r" (current->tss.uaccess->put_byte),	\
	  "0" (err)							\
	: "r0", "r1", "r2", "lr")

#define __put_user_asm_half(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %1\n"					\
	"	mov	r1, %2\n"					\
	"	mov	r2, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r2\n"					\
	: "=r" (err)							\
	: "r" (x), "r" (addr), "r" (current->tss.uaccess->put_half),	\
	  "0" (err)							\
	: "r0", "r1", "r2", "lr")

#define __put_user_asm_word(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %1\n"					\
	"	mov	r1, %2\n"					\
	"	mov	r2, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r2\n"					\
	: "=r" (err)							\
	: "r" (x), "r" (addr), "r" (current->tss.uaccess->put_word),	\
	  "0" (err)							\
	: "r0", "r1", "r2", "lr")

#define __get_user_asm_byte(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %2\n"					\
	"	mov	r1, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r1\n"					\
	"	mov	%1, r0\n"					\
	: "=r" (err), "=r" (x)						\
	: "r" (addr), "r" (current->tss.uaccess->get_byte), "0" (err)	\
	: "r0", "r1", "r2", "lr")

#define __get_user_asm_half(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %2\n"					\
	"	mov	r1, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r1\n"					\
	"	mov	%1, r0\n"					\
	: "=r" (err), "=r" (x)						\
	: "r" (addr), "r" (current->tss.uaccess->get_half), "0" (err)	\
	: "r0", "r1", "r2", "lr")

#define __get_user_asm_word(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %2\n"					\
	"	mov	r1, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r1\n"					\
	"	mov	%1, r0\n"					\
	: "=r" (err), "=r" (x)						\
	: "r" (addr), "r" (current->tss.uaccess->get_word), "0" (err)	\
	: "r0", "r1", "r2", "lr")

#define __do_copy_from_user(to,from,n)					\
	(n) = current->tss.uaccess->copy_from_user((to),(from),(n))

#define __do_copy_to_user(to,from,n)					\
	(n) = current->tss.uaccess->copy_to_user((to),(from),(n))

#define __do_clear_user(addr,sz)					\
	(sz) = current->tss.uaccess->clear_user((addr),(sz))

#define __do_strncpy_from_user(dst,src,count,res)			\
	(res) = current->tss.uaccess->strncpy_from_user(dst,src,count)

#define __do_strlen_user(s,res)						\
	(res) = current->tss.uaccess->strlen_user(s)

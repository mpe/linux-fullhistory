/*
 *  linux/include/asm-m68k/segment.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

/*
 * 680x0 support added by Hamish Macdonald
 */

#ifndef _M68K_SEGMENT_H
#define _M68K_SEGMENT_H

static inline unsigned char get_user_byte(const char * addr)
{
	register unsigned char _v;

	__asm__ __volatile__ ("movesb %1,%0":"=r" (_v):"m" (*addr));
	return _v;
}

#define get_fs_byte(addr) get_user_byte((char *)(addr))

static inline unsigned short get_user_word(const short *addr)
{
	unsigned short _v;

	__asm__ __volatile__ ("movesw %1,%0":"=r" (_v):"m" (*addr));
	return _v;
}

#define get_fs_word(addr) get_user_word((short *)(addr))

static inline unsigned long get_user_long(const int *addr)
{
	unsigned long _v;

	__asm__ __volatile__ ("movesl %1,%0":"=r" (_v):"m" (*addr)); \
	return _v;
}

#define get_fs_long(addr) get_user_long((int *)(addr))

static inline void put_user_byte(char val,char *addr)
{
	__asm__ __volatile__ ("movesb %0,%1": /* no outputs */ :"r" (val),"m" (*addr) : "memory");
}

#define put_fs_byte(x,addr) put_user_byte((x),(char *)(addr))

static inline void put_user_word(short val,short * addr)
{
	__asm__ __volatile__ ("movesw %0,%1": /* no outputs */ :"r" (val),"m" (*addr) : "memory");
}

#define put_fs_word(x,addr) put_user_word((x),(short *)(addr))

static inline void put_user_long(unsigned long val,int * addr)
{
	__asm__ __volatile__ ("movesl %0,%1": /* no outputs */ :"r" (val),"m" (*addr) : "memory");
}

#define put_fs_long(x,addr) put_user_long((x),(int *)(addr))

static inline void __generic_memcpy_tofs(void * to, const void * from, unsigned long n)
{
	if (n == 0) return;
	__asm__ __volatile__ ("1:\n\t"
			      "moveb %1@+,d0\n\t"
			      "movesb d0,%2@+\n\t"
			      "dbra %0,1b\n\t"
			      "clrw %0\n\t"
			      "subql #1,%0\n\t"
			      "bccs 1b"
			      : "=d" (n), "=a" (from), "=a" (to)
			      : "1" (from), "2" (to), "0" (n-1)
			      : "d0", "memory");
}

static inline void __constant_memcpy_tofs(void * to, const void * from, unsigned long n)
{
	if (n == 0) {
			return;
	} else if (n == 1) {
			put_user_byte(*(const char *) from, (char *) to);
			return;
	} else if (n == 2) {
			put_user_word(*(const short *) from, (short *) to);
			return;
	} else if (n == 3) {
			put_user_word(*(const short *) from, (short *) to);
			put_user_byte(*(2+(const char *) from), 2+(char *) to);
			return;
	} else if (n == 4) {
			put_user_long(*(const int *) from, (int *) to);
			return;
	}
#if 0
#define COMMON(x) \
__asm__("cld\n\t" \
	"push %%es\n\t" \
	"push %%fs\n\t" \
	"pop %%es\n\t" \
	"rep ; movsl\n\t" \
	x \
	"pop %%es" \
	: /* no outputs */ \
	:"c" (n/4),"D" ((long) to),"S" ((long) from) \
	:"cx","di","si")

	switch (n % 4) {
		case 0:
			COMMON("");
			return;
		case 1:
			COMMON("movsb\n\t");
			return;
		case 2:
			COMMON("movsw\n\t");
			return;
		case 3:
			COMMON("movsw\n\tmovsb\n\t");
			return;
	}
#undef COMMON
#else
        __generic_memcpy_tofs(to,from,n);
#endif
}

static inline void __generic_memcpy_fromfs(void * to, const void * from, unsigned long n)
{
	if (n == 0) return;
	__asm__ __volatile__ ("1:\n\t"
			      "movesb %1@+,d0\n\t"
			      "moveb d0,%2@+\n\t"
			      "dbra %0,1b\n\t"
			      "clrw %0\n\t"
			      "subql #1,%0\n\t"
			      "bccs 1b"
			      : "=d" (n), "=a" (from), "=a" (to)
			      : "1" (from), "2" (to), "0" (n-1)
			      : "d0", "memory");
}

static inline void __constant_memcpy_fromfs(void * to, const void * from, unsigned long n)
{
	if (n == 0) {
			return;
	} else if (n == 1) {
			*(char *)to = get_user_byte((const char *) from);
			return;
	} else if (n == 2) {
			*(short *)to = get_user_word((const short *) from);
			return;
	} else if (n == 3) {
			*(short *) to = get_user_word((const short *) from);
			*(2+(char *) to) = get_user_byte(2+(const char *) from);
			return;
	} else if (n == 4) {
			*(int *) to = get_user_long((const int *) from);
			return;
	}
#if 0
#define COMMON(x) \
__asm__("cld\n\t" \
	"rep ; fs ; movsl\n\t" \
	x \
	: /* no outputs */ \
	:"c" (n/4),"D" ((long) to),"S" ((long) from) \
	:"cx","di","si","memory")

	switch (n % 4) {
		case 0:
			COMMON("");
			return;
		case 1:
			COMMON("fs ; movsb");
			return;
		case 2:
			COMMON("fs ; movsw");
			return;
		case 3:
			COMMON("fs ; movsw\n\tfs ; movsb");
			return;
	}
#undef COMMON
#else
        __generic_memcpy_fromfs(to,from,n);
#endif
}

#define memcpy_fromfs(to, from, n) \
(__builtin_constant_p(n) ? \
 __constant_memcpy_fromfs((to),(from),(n)) : \
 __generic_memcpy_fromfs((to),(from),(n)))

#define memcpy_tofs(to, from, n) \
(__builtin_constant_p(n) ? \
 __constant_memcpy_tofs((to),(from),(n)) : \
 __generic_memcpy_tofs((to),(from),(n)))

/*
 * Get/set the SFC/DFC registers for MOVES instructions
 */

static inline unsigned long get_fs(void)
{
	unsigned long _v;
	__asm__ ("movec dfc,%0":"=r" (_v):);

	return _v;
}

static inline unsigned long get_ds(void)
{
    /* return the supervisor data space code */
    return 0x5;
}

static inline void set_fs(unsigned long val)
{
	__asm__ __volatile__ ("movec %0,sfc\n\t"
			      "movec %0,dfc\n\t"
			      : /* no outputs */ : "r" (val), "r" (val) : "memory");
}

/* define constants */
/* Address spaces (FC0-FC2) */
#ifndef USER_DS
#define USER_DS       (1)
#endif
#ifndef KERNEL_DS
#define KERNEL_DS     (5)
#endif

#endif /* _M68K_SEGMENT_H */

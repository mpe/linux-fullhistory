#ifndef _M68K_SEGMENT_H
#define _M68K_SEGMENT_H

/* define constants */
/* Address spaces (FC0-FC2) */
#define USER_DATA     (1)
#ifndef USER_DS
#define USER_DS       (USER_DATA)
#endif
#define USER_PROGRAM  (2)
#define SUPER_DATA    (5)
#ifndef KERNEL_DS
#define KERNEL_DS     (SUPER_DATA)
#endif
#define SUPER_PROGRAM (6)
#define CPU_SPACE     (7)

#ifndef __ASSEMBLY__

/*
 * Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 */
#define put_user(x,ptr) __put_user((unsigned long)(x),(ptr),sizeof(*(ptr)))
#define get_user(ptr) ((__typeof__(*(ptr)))__get_user((ptr),sizeof(*(ptr))))

/*
 * This is a silly but good way to make sure that
 * the __put_user function is indeed always optimized,
 * and that we use the correct sizes..
 */
extern int bad_user_access_length(void);

#define __ptr(x) ((unsigned long *)(x))

static inline void __put_user(unsigned long x, void * y, int size)
{
	switch (size) {
		case 1:
			__asm__ ("movesb %0,%1"
				: /* no outputs */
				:"r" (x),"m" (*__ptr(y)) : "memory");
			break;
		case 2:
			__asm__ ("movesw %0,%1"
				: /* no outputs */
				:"r" (x),"m" (*__ptr(y)) : "memory");
			break;
		case 4:
			__asm__ ("movesl %0,%1"
				: /* no outputs */
				:"r" (x),"m" (*__ptr(y)) : "memory");
			break;
		default:
			bad_user_access_length();
	}
}

static inline unsigned long __get_user(const void * y, int size)
{
	unsigned long result;

	switch (size) {
		case 1:
			__asm__ ("movesb %1,%0"
				 :"=r" (result)
				 :"m" (*__ptr(y)));
			return (unsigned char) result;
		case 2:
			__asm__ ("movesw %1,%0"
				 :"=r" (result)
				 :"m" (*__ptr(y)));
			return (unsigned short) result;
		case 4:
			__asm__ ("movesl %1,%0"
				 :"=r" (result)
				 :"m" (*__ptr(y)));
			return result;
		default:
			return bad_user_access_length();
	}
}
#undef __ptr

/*
 * These are deprecated..
 *
 * Use "put_user()" and "get_user()" with the proper pointer types instead.
 */

#define get_fs_byte(addr) __get_user((const unsigned char *)(addr),1)
#define get_fs_word(addr) __get_user((const unsigned short *)(addr),2)
#define get_fs_long(addr) __get_user((const unsigned int *)(addr),4)

#define put_fs_byte(x,addr) __put_user((x),(unsigned char *)(addr),1)
#define put_fs_word(x,addr) __put_user((x),(unsigned short *)(addr),2)
#define put_fs_long(x,addr) __put_user((x),(unsigned int *)(addr),4)

#ifdef WE_REALLY_WANT_TO_USE_A_BROKEN_INTERFACE

static inline unsigned char get_user_byte(const char * addr)
{
	return __get_user(addr,1);
}

static inline unsigned short get_user_word(const short *addr)
{
	return __get_user(addr,2);
}

static inline unsigned long get_user_long(const int *addr)
{
	return __get_user(addr,4);
}

static inline void put_user_byte(char val,char *addr)
{
	__put_user(val, addr, 1);
}

static inline void put_user_word(short val,short * addr)
{
	__put_user(val, addr, 2);
}

static inline void put_user_long(unsigned long val,int * addr)
{
	__put_user(val, addr, 4);
}

#endif

static inline void __generic_memcpy_tofs(void * to, const void * from, unsigned long n)
{
	unsigned long tmp;
	if (n == 0) return;
	tmp = n;
	n >>= 2;
	if (n != 0)
		__asm__ ("1:\t"
			 "movel %1@+,%/d0\n\t"
			 "movesl %/d0,%2@+\n\t"
			      "dbra %0,1b\n\t"
			      "clrw %0\n\t"
			      "subql #1,%0\n\t"
			 "bccs 1b\n\t"
			      : "=d" (n), "=a" (from), "=a" (to)
			 : "0" (n-1), "1" (from), "2" (to)
			 : "d0", "memory");
	if (tmp & 2)
		__asm__ ("movew %0@+,%/d0\n\t"
			 "movesw %/d0,%1@+\n\t"
			 : "=a" (from), "=a" (to)
			 : "0" (from), "1" (to)
			 : "d0", "memory");
	if (tmp & 1)
		__asm__ __volatile__ ("moveb %0@,%/d0\n\t"
			 "movesb %/d0,%1@\n\t"
			 : /* no outputs */
			 : "a" (from), "a" (to)
			      : "d0", "memory");
}

static inline void __constant_memcpy_tofs(void * to, const void * from, unsigned long n)
{
	switch (n) {
		case 0:
			return;
		case 1:
			__put_user(*(const char *) from, (char *) to, 1);
			return;
		case 2:
			__put_user(*(const short *) from, (short *) to, 2);
			return;
		case 3:
			__put_user(*(const short *) from, (short *) to, 2);
			__put_user(*(2+(const char *) from), 2+(char *) to, 1);
			return;
		case 4:
			__put_user(*(const int *) from, (int *) to, 4);
			return;
		case 8:
			__put_user(*(const int *) from, (int *) to, 4);
			__put_user(*(1+(const int *) from), 1+(int *) to, 4);
			return;
		case 12:
			__put_user(*(const int *) from, (int *) to, 4);
			__put_user(*(1+(const int *) from), 1+(int *) to, 4);
			__put_user(*(2+(const int *) from), 2+(int *) to, 4);
			return;
		case 16:
			__put_user(*(const int *) from, (int *) to, 4);
			__put_user(*(1+(const int *) from), 1+(int *) to, 4);
			__put_user(*(2+(const int *) from), 2+(int *) to, 4);
			__put_user(*(3+(const int *) from), 3+(int *) to, 4);
			return;
	}
#define COMMON(x)                     \
__asm__ __volatile__ ("1:\n\t"        \
            "movel %1@+,%/d0\n\t"     \
            "movesl %/d0,%2@+\n\t"    \
            "dbra %0,1b\n\t"          \
            "clrw %0\n\t"             \
            "subql #1,%0\n\t"         \
            "bccs 1b\n\t"             \
            x                     \
            : "=d" (n), "=a" (from), "=a" (to)    \
            : "1" (from), "2" (to), "0" (n/4-1)   \
            : "d0", "memory");

  switch (n % 4) {
      case 0:
          COMMON("");
          return;
      case 1:
          COMMON("moveb %1@+,%/d0; movesb %/d0,%2@+");
          return;
      case 2:
          COMMON("movew %1@+,%/d0; movesw %/d0,%2@+");
          return;
      case 3:
          COMMON("movew %1@+,%/d0; movesw %/d0,%2@+\n\t"
                 "moveb %1@+,%/d0; movesb %/d0,%2@+");
          return;
  }
#undef COMMON
}

static inline void __generic_memcpy_fromfs(void * to, const void * from, unsigned long n)
{
	unsigned long tmp;
	if (n == 0) return;
	tmp = n;
	n >>= 2;
	if (n != 0)
		__asm__ ("1:\t"
			 "movesl %1@+,%/d0\n\t"
			 "movel %/d0,%2@+\n\t"
			      "dbra %0,1b\n\t"
			      "clrw %0\n\t"
			      "subql #1,%0\n\t"
			 "bccs 1b\n\t"
			      : "=d" (n), "=a" (from), "=a" (to)
			 : "0" (n-1), "1" (from), "2" (to)
			 : "d0", "memory");
	if (tmp & 2)
		__asm__ ("movesw %0@+,%/d0\n\t"
			 "movew %/d0,%1@+\n\t"
			 : "=a" (from), "=a" (to)
			 : "0" (from), "1" (to)
			 : "d0", "memory");
	if (tmp & 1)
		__asm__ __volatile__ ("movesb %0@,%/d0\n\t"
			 "moveb %/d0,%1@\n\t"
			 : /* no outputs */
			 : "a" (from), "a" (to)
			      : "d0", "memory");
}

static inline void __constant_memcpy_fromfs(void * to, const void * from, unsigned long n)
{
	switch (n) {
		case 0:
			return;
		case 1:
			*(char *)to = __get_user((const char *) from, 1);
			return;
		case 2:
			*(short *)to = __get_user((const short *) from, 2);
			return;
		case 3:
			*(short *) to = __get_user((const short *) from, 2);
			*((char *) to + 2) = __get_user(2+(const char *) from, 1);
			return;
		case 4:
			*(int *) to = __get_user((const int *) from, 4);
			return;
		case 8:
			*(int *) to = __get_user((const int *) from, 4);
			*(1+(int *) to) = __get_user(1+(const int *) from, 4);
			return;
		case 12:
			*(int *) to = __get_user((const int *) from, 4);
			*(1+(int *) to) = __get_user(1+(const int *) from, 4);
			*(2+(int *) to) = __get_user(2+(const int *) from, 4);
			return;
		case 16:
			*(int *) to = __get_user((const int *) from, 4);
			*(1+(int *) to) = __get_user(1+(const int *) from, 4);
			*(2+(int *) to) = __get_user(2+(const int *) from, 4);
			*(3+(int *) to) = __get_user(3+(const int *) from, 4);
			return;
	}
#define COMMON(x)                     \
__asm__ __volatile__ ("1:\n\t"        \
            "movesl %1@+,%/d0\n\t"    \
            "movel %/d0,%2@+\n\t"     \
            "dbra %0,1b\n\t"          \
            "clrw %0\n\t"             \
            "subql #1,%0\n\t"         \
            "bccs 1b\n\t"             \
            x                         \
            : "=d" (n), "=a" (from), "=a" (to)    \
            : "1" (from), "2" (to), "0" (n/4-1)   \
            : "d0", "memory");

  switch (n % 4) {
      case 0:
          COMMON("");
          return;
      case 1:
          COMMON("movesb %1@+,%/d0; moveb %/d0,%2@+");
          return;
      case 2:
          COMMON("movesw %1@+,%/d0; movew %/d0,%2@+");
          return;
      case 3:
          COMMON("movesw %1@+,%/d0; movew %/d0,%2@+\n\t"
                 "movesb %1@+,%/d0; moveb %/d0,%2@+");
          return;
  }
#undef COMMON
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
	__asm__ ("movec %/dfc,%0":"=r" (_v):);

	return _v;
}

static inline unsigned long get_ds(void)
{
    /* return the supervisor data space code */
    return KERNEL_DS;
}

static inline void set_fs(unsigned long val)
{
	__asm__ __volatile__ ("movec %0,%/sfc\n\t"
			      "movec %0,%/dfc\n\t"
			      : /* no outputs */ : "r" (val) : "memory");
}

#endif /* __ASSEMBLY__ */

#endif /* _M68K_SEGMENT_H */

#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#define KERNEL_CS	0x10
#define KERNEL_DS	0x18

#define USER_CS		0x23
#define USER_DS		0x2B

#ifndef __ASSEMBLY__

/*
 * Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 */
#define put_user(x,ptr) __put_user((unsigned long)(x),(ptr),sizeof(*(ptr)))
#define get_user(ptr) __get_user((ptr),sizeof(*(ptr)))

/*
 * This is a silly but good way to make sure that
 * the __put_user function is indeed always optimized,
 * and that we use the correct sizes..
 */
extern int bad_user_access_length(void);

/*
 * dummy pointer type structure.. gcc won't try to do something strange
 * this way..
 */
struct __segment_dummy { unsigned long a[100]; };
#define __sd(x) ((struct __segment_dummy *) (x))

static inline void __put_user(unsigned long x, void * y, int size)
{
	switch (size) {
		case 1:
			__asm__ ("movb %b1,%%fs:%0"
				:"=m" (*__sd(y))
				:"iq" ((unsigned char) x), "m" (*__sd(y)));
			break;
		case 2:
			__asm__ ("movw %w1,%%fs:%0"
				:"=m" (*__sd(y))
				:"iq" ((unsigned short) x), "m" (*__sd(y)));
			break;
		case 4:
			__asm__ ("movl %1,%%fs:%0"
				:"=m" (*__sd(y))
				:"ir" (x), "m" (*__sd(y)));
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
			__asm__ ("movb %%fs:%1,%b0"
				:"=q" (result)
				:"m" (*__sd(y)));
			return (unsigned char) result;
		case 2:
			__asm__ ("movw %%fs:%1,%w0"
				:"=q" (result)
				:"m" (*__sd(y)));
			return (unsigned short) result;
		case 4:
			__asm__ ("movl %%fs:%1,%0"
				:"=r" (result)
				:"m" (*__sd(y)));
			return result;
		default:
			return bad_user_access_length();
	}
}

/*
 * These are depracated..
 */

static inline unsigned char get_user_byte(const char * addr)
{
	return __get_user(addr,1);
}

#define get_fs_byte(addr) get_user_byte((char *)(addr))

static inline unsigned short get_user_word(const short *addr)
{
	return __get_user(addr, 2);
}

#define get_fs_word(addr) get_user_word((short *)(addr))

static inline unsigned long get_user_long(const int *addr)
{
	return __get_user(addr, 4);
}

#define get_fs_long(addr) get_user_long((int *)(addr))

static inline void put_user_byte(char val,char *addr)
{
	__put_user(val, addr, 1);
}

#define put_fs_byte(x,addr) put_user_byte((x),(char *)(addr))

static inline void put_user_word(short val,short * addr)
{
	__put_user(val, addr, 2);
}

#define put_fs_word(x,addr) put_user_word((x),(short *)(addr))

static inline void put_user_long(unsigned long val,int * addr)
{
	__put_user(val, addr, 4);
}

#define put_fs_long(x,addr) put_user_long((x),(int *)(addr))

static inline void __generic_memcpy_tofs(void * to, const void * from, unsigned long n)
{
__asm__("cld\n\t"
	"push %%es\n\t"
	"push %%fs\n\t"
	"pop %%es\n\t"
	"testb $1,%%cl\n\t"
	"je 1f\n\t"
	"movsb\n"
	"1:\ttestb $2,%%cl\n\t"
	"je 2f\n\t"
	"movsw\n"
	"2:\tshrl $2,%%ecx\n\t"
	"rep ; movsl\n\t"
	"pop %%es"
	: /* no outputs */
	:"c" (n),"D" ((long) to),"S" ((long) from)
	:"cx","di","si");
}

static inline void __constant_memcpy_tofs(void * to, const void * from, unsigned long n)
{
	switch (n) {
		case 0:
			return;
		case 1:
			put_user_byte(*(const char *) from, (char *) to);
			return;
		case 2:
			put_user_word(*(const short *) from, (short *) to);
			return;
		case 3:
			put_user_word(*(const short *) from, (short *) to);
			put_user_byte(*(2+(const char *) from), 2+(char *) to);
			return;
		case 4:
			put_user_long(*(const int *) from, (int *) to);
			return;
	}
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
}

static inline void __generic_memcpy_fromfs(void * to, const void * from, unsigned long n)
{
__asm__("cld\n\t"
	"testb $1,%%cl\n\t"
	"je 1f\n\t"
	"fs ; movsb\n"
	"1:\ttestb $2,%%cl\n\t"
	"je 2f\n\t"
	"fs ; movsw\n"
	"2:\tshrl $2,%%ecx\n\t"
	"rep ; fs ; movsl"
	: /* no outputs */
	:"c" (n),"D" ((long) to),"S" ((long) from)
	:"cx","di","si","memory");
}

static inline void __constant_memcpy_fromfs(void * to, const void * from, unsigned long n)
{
	switch (n) {
		case 0:
			return;
		case 1:
			*(char *)to = get_user_byte((const char *) from);
			return;
		case 2:
			*(short *)to = get_user_word((const short *) from);
			return;
		case 3:
			*(short *) to = get_user_word((const short *) from);
			*((char *) to + 2) = get_user_byte(2+(const char *) from);
			return;
		case 4:
			*(int *) to = get_user_long((const int *) from);
			return;
	}
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
 * Someone who knows GNU asm better than I should double check the following.
 * It seems to work, but I don't know if I'm doing something subtly wrong.
 * --- TYT, 11/24/91
 * [ nothing wrong here, Linus: I just changed the ax to be any reg ]
 */

static inline unsigned long get_fs(void)
{
	unsigned long _v;
	__asm__("mov %%fs,%w0":"=r" (_v):"0" (0));
	return _v;
}

static inline unsigned long get_ds(void)
{
	unsigned long _v;
	__asm__("mov %%ds,%w0":"=r" (_v):"0" (0));
	return _v;
}

static inline void set_fs(unsigned long val)
{
	__asm__ __volatile__("mov %w0,%%fs": /* no output */ :"r" (val));
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_SEGMENT_H */

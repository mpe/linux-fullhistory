#ifndef __ALPHA_UNALIGNED_H
#define __ALPHA_UNALIGNED_H

/* 
 * The main single-value unaligned transfer routines.
 */
#define get_unaligned(ptr) \
	((__typeof__(*(ptr)))__get_unaligned((ptr), sizeof(*(ptr))))
#define put_unaligned(x,ptr) \
	__put_unaligned((unsigned long)(x), (ptr), sizeof(*(ptr)))

/*
 * This is a silly but good way to make sure that
 * the get/put functions are indeed always optimized,
 * and that we use the correct sizes.
 */
extern void bad_unaligned_access_length(void);

/*
 * EGCS 1.1 knows about arbitrary unaligned loads.  Define some
 * packed structures to talk about such things with.
 */

struct __una_u64 { __u64 x __attribute__((packed)); };
struct __una_u32 { __u32 x __attribute__((packed)); };
struct __una_u16 { __u16 x __attribute__((packed)); };

/*
 * Elemental unaligned loads 
 */

extern inline unsigned long __uldq(const unsigned long * r11)
{
#if __GNUC__ > 2 || __GNUC_MINOR__ >= 91
	const struct __una_u64 *ptr = (const struct __una_u64 *) r11;
	return ptr->x;
#else
	unsigned long r1,r2;
	__asm__("ldq_u %0,%3\n\t"
		"ldq_u %1,%4\n\t"
		"extql %0,%2,%0\n\t"
		"extqh %1,%2,%1"
		:"=&r" (r1), "=&r" (r2)
		:"r" (r11),
		 "m" (*r11),
		 "m" (*(const unsigned long *)(7+(char *) r11)));
	return r1 | r2;
#endif
}

extern inline unsigned long __uldl(const unsigned int * r11)
{
#if __GNUC__ > 2 || __GNUC_MINOR__ >= 91
	const struct __una_u32 *ptr = (const struct __una_u32 *) r11;
	return ptr->x;
#else
	unsigned long r1,r2;
	__asm__("ldq_u %0,%3\n\t"
		"ldq_u %1,%4\n\t"
		"extll %0,%2,%0\n\t"
		"extlh %1,%2,%1"
		:"=&r" (r1), "=&r" (r2)
		:"r" (r11),
		 "m" (*r11),
		 "m" (*(const unsigned long *)(3+(char *) r11)));
	return r1 | r2;
#endif
}

extern inline unsigned long __uldw(const unsigned short * r11)
{
#if __GNUC__ > 2 || __GNUC_MINOR__ >= 91
	const struct __una_u16 *ptr = (const struct __una_u16 *) r11;
	return ptr->x;
#else
	unsigned long r1,r2;
	__asm__("ldq_u %0,%3\n\t"
		"ldq_u %1,%4\n\t"
		"extwl %0,%2,%0\n\t"
		"extwh %1,%2,%1"
		:"=&r" (r1), "=&r" (r2)
		:"r" (r11),
		 "m" (*r11),
		 "m" (*(const unsigned long *)(1+(char *) r11)));
	return r1 | r2;
#endif
}

/*
 * Elemental unaligned stores 
 */

extern inline void __ustq(unsigned long r5, unsigned long * r11)
{
#if __GNUC__ > 2 || __GNUC_MINOR__ >= 91
	struct __una_u64 *ptr = (struct __una_u64 *) r11;
	ptr->x = r5;
#else
	unsigned long r1,r2,r3,r4;

	__asm__("ldq_u %3,%1\n\t"
		"ldq_u %2,%0\n\t"
		"insqh %6,%7,%5\n\t"
		"insql %6,%7,%4\n\t"
		"mskqh %3,%7,%3\n\t"
		"mskql %2,%7,%2\n\t"
		"bis %3,%5,%3\n\t"
		"bis %2,%4,%2\n\t"
		"stq_u %3,%1\n\t"
		"stq_u %2,%0"
		:"=m" (*r11),
		 "=m" (*(unsigned long *)(7+(char *) r11)),
		 "=&r" (r1), "=&r" (r2), "=&r" (r3), "=&r" (r4)
		:"r" (r5), "r" (r11));
#endif
}

extern inline void __ustl(unsigned long r5, unsigned int * r11)
{
#if __GNUC__ > 2 || __GNUC_MINOR__ >= 91
	struct __una_u32 *ptr = (struct __una_u32 *) r11;
	ptr->x = r5;
#else
	unsigned long r1,r2,r3,r4;

	__asm__("ldq_u %3,%1\n\t"
		"ldq_u %2,%0\n\t"
		"inslh %6,%7,%5\n\t"
		"insll %6,%7,%4\n\t"
		"msklh %3,%7,%3\n\t"
		"mskll %2,%7,%2\n\t"
		"bis %3,%5,%3\n\t"
		"bis %2,%4,%2\n\t"
		"stq_u %3,%1\n\t"
		"stq_u %2,%0"
		:"=m" (*r11),
		 "=m" (*(unsigned long *)(3+(char *) r11)),
		 "=&r" (r1), "=&r" (r2), "=&r" (r3), "=&r" (r4)
		:"r" (r5), "r" (r11));
#endif
}

extern inline void __ustw(unsigned long r5, unsigned short * r11)
{
#if __GNUC__ > 2 || __GNUC_MINOR__ >= 91
	struct __una_u16 *ptr = (struct __una_u16 *) r11;
	ptr->x = r5;
#else
	unsigned long r1,r2,r3,r4;

	__asm__("ldq_u %3,%1\n\t"
		"ldq_u %2,%0\n\t"
		"inswh %6,%7,%5\n\t"
		"inswl %6,%7,%4\n\t"
		"mskwh %3,%7,%3\n\t"
		"mskwl %2,%7,%2\n\t"
		"bis %3,%5,%3\n\t"
		"bis %2,%4,%2\n\t"
		"stq_u %3,%1\n\t"
		"stq_u %2,%0"
		:"=m" (*r11),
		 "=m" (*(unsigned long *)(1+(char *) r11)),
		 "=&r" (r1), "=&r" (r2), "=&r" (r3), "=&r" (r4)
		:"r" (r5), "r" (r11));
#endif
}

extern inline unsigned long __get_unaligned(const void *ptr, size_t size)
{
	unsigned long val;
	switch (size) {
	      case 1:
		val = *(const unsigned char *)ptr;
		break;
	      case 2:
		val = __uldw((const unsigned short *)ptr);
		break;
	      case 4:
		val = __uldl((const unsigned int *)ptr);
		break;
	      case 8:
		val = __uldq((const unsigned long *)ptr);
		break;
	      default:
		bad_unaligned_access_length();
	}
	return val;
}

extern inline void __put_unaligned(unsigned long val, void *ptr, size_t size)
{
	switch (size) {
	      case 1:
		*(unsigned char *)ptr = (val);
	        break;
	      case 2:
		__ustw(val, (unsigned short *)ptr);
		break;
	      case 4:
		__ustl(val, (unsigned int *)ptr);
		break;
	      case 8:
		__ustq(val, (unsigned long *)ptr);
		break;
	      default:
	    	bad_unaligned_access_length();
	}
}

#endif

#ifndef __ASM_ARM_UNALIGNED_H
#define __ASM_ARM_UNALIGNED_H

#define get_unaligned(ptr) \
	((__typeof__(*(ptr)))__get_unaligned_size((ptr), sizeof(*(ptr))))

#define put_unaligned(val, ptr) \
	__put_unaligned_size((unsigned long)(val), (ptr), sizeof(*(ptr)))

/*
 * We use a similar method to the uaccess.h badness detection.
 *
 * These are actually never defined anywhere, and therefore
 * catch errors at compile/link time.  Don't be tempted to
 * provide a declaration for them; doing so will mask the
 * errors.
 */
extern unsigned long __get_unaligned_bad(void);
extern void __put_unaligned_bad(void);

extern __inline__ unsigned long __get_unaligned_size(const void *ptr, size_t size)
{
	const unsigned char *p = (const unsigned char *)ptr;
	unsigned long val = 0;

	switch (size) {
	case 4:		val  = p[2] << 16 | p[3] << 24;
	case 2:		val |= p[1] << 8;
	case 1:		val |= p[0];				break;
	default:	val = __get_unaligned_bad();		break;
	}
	return val;
}

extern __inline__ void __put_unaligned_size(unsigned long val, void *ptr, size_t size)
{
	switch (size) {
	case 4:		((unsigned char *)ptr)[3] = val >> 24;
			((unsigned char *)ptr)[2] = val >> 16;
	case 2:		((unsigned char *)ptr)[1] = val >> 8;
	case 1:		((unsigned char *)ptr)[0] = val;	break;
	default:	__put_unaligned_bad();			break;
	}
}

#endif

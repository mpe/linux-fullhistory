#ifndef __ARM_UNALIGNED_H
#define __ARM_UNALIGNED_H

#define get_unaligned(ptr) \
	((__typeof__(*(ptr)))__get_unaligned((ptr), sizeof(*(ptr))))

#define put_unaligned(val, ptr) \
	__put_unaligned((unsigned long)(val), (ptr), sizeof(*(ptr)))

extern void bad_unaligned_access_length (void);
	
extern inline unsigned long __get_unaligned(const void *ptr, size_t size)
{
    unsigned long val;
    switch (size) {
	case 1:
	    val = *(const unsigned char *)ptr;
	    break;

	case 2:
	    val = ((const unsigned char *)ptr)[0] | (((const unsigned char *)ptr)[1] << 8);
	    break;

	case 4:
	    val = ((const unsigned char *)ptr)[0]        | (((const unsigned char *)ptr)[1] << 8) |
		 (((const unsigned char *)ptr)[2]) << 16 | (((const unsigned char *)ptr)[3] << 24);
	    break;

	default:
	    bad_unaligned_access_length ();
    }
    return val;
}

extern inline void __put_unaligned(unsigned long val, void *ptr, size_t size)
{
    switch (size) {
	case 1:
	    *(unsigned char *)ptr = val;
	    break;

	case 2:
	    ((unsigned char *)ptr)[0] = val;
	    ((unsigned char *)ptr)[1] = val >> 8;
	    break;

	case 4:
	    ((unsigned char *)ptr)[0] = val;
	    ((unsigned char *)ptr)[1] = val >> 8;
	    ((unsigned char *)ptr)[2] = val >> 16;
	    ((unsigned char *)ptr)[3] = val >> 24;
	    break;

	default:
	    bad_unaligned_access_length ();
    }
}

#endif

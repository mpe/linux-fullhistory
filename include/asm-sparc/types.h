#ifndef _SPARC_TYPES_H
#define _SPARC_TYPES_H

/*
 * _xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space   <-- Linus sez this
 */

/* NOTE: I will have to change these when the V9 sparcs come into play,
 *       however this won't be for a while.
 */

typedef signed char __s8;
typedef unsigned char __u8;

typedef signed short __s16;
typedef unsigned short __u16;

typedef signed int __s32;
typedef unsigned int __u32;

/* Only 32-bit sparcs for now so.... */

typedef signed long long __s64;
typedef unsigned long long __s64;

#ifdef __KERNEL__

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

/* Again, only have to worry about 32-bits */

typedef signed long long s64;
typedef unsigned long long u64;

#endif /* __KERNEL__ */

#endif /* defined(_SPARC_TYPES_H) */

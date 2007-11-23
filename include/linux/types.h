#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

/*
 * This allows for 256 file descriptors: if NR_OPEN is ever grown beyond that
 * you'll have to change this too. But 256 fd's seem to be enough even for such
 * "real" unices like SunOS, so hopefully this is one limit that doesn't have
 * to be changed.
 *
 * Note that POSIX wants the FD_CLEAR(fd,fdsetp) defines to be in <sys/time.h>
 * (and thus <linux/time.h>) - but this is a more logical place for them. Solved
 * by having dummy defines in <sys/time.h>.
 */

/*
 * Those macros may have been defined in <gnu/types.h>. But we always
 * use the ones here. 
 */
#undef __FDSET_LONGS
#define __FDSET_LONGS (256/(8*sizeof(unsigned long)))

typedef struct fd_set {
	unsigned long fds_bits [__FDSET_LONGS];
} fd_set;

#undef __NFDBITS
#define __NFDBITS	(8 * sizeof(unsigned long))

#undef __FD_SETSIZE
#define __FD_SETSIZE	(__FDSET_LONGS*__NFDBITS)

#include <asm/types.h>

#ifndef NULL
#define NULL ((void *) 0)
#endif

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
#define _LOFF_T
typedef long long loff_t;
#endif

/* bsd */
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

/* sysv */
typedef unsigned char unchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

typedef char *caddr_t;

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

struct ustat {
	daddr_t f_tfree;
	ino_t f_tinode;
	char f_fname[6];
	char f_fpack[6];
};

#endif

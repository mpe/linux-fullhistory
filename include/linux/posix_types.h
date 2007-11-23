#ifndef _LINUX_POSIX_TYPES_H
#define _LINUX_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

#ifndef NULL
# define NULL		((void *) 0)
#endif

/*
 * This allows for 256 file descriptors: if NR_OPEN is ever grown
 * beyond that you'll have to change this too. But 256 fd's seem to be
 * enough even for such "real" unices like SunOS, so hopefully this is
 * one limit that doesn't have to be changed.
 *
 * Note that POSIX wants the FD_CLEAR(fd,fdsetp) defines to be in
 * <sys/time.h> (and thus <linux/time.h>) - but this is a more logical
 * place for them. Solved by having dummy defines in <sys/time.h>.
 */

/*
 * Those macros may have been defined in <gnu/types.h>. But we always
 * use the ones here. 
 */
#undef __NFDBITS
#define __NFDBITS	(8 * sizeof(unsigned int))

#undef __FD_SETSIZE
#define __FD_SETSIZE	256

#undef __FDSET_INTS
#define __FDSET_INTS	(__FD_SETSIZE/__NFDBITS)

#undef __FDELT
#define	__FDELT(d)	((d) / __NFDBITS)

#undef __FDMASK
#define	__FDMASK(d)	(1 << ((d) % __NFDBITS))

typedef struct fd_set {
	unsigned int fds_bits [__FDSET_INTS];
} __kernel_fd_set;

#include <asm/posix_types.h>

#endif /* _LINUX_POSIX_TYPES_H */

#ifndef __ARCH_I386_POSIX_TYPES_H
#define __ARCH_I386_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef unsigned short	__dev_t;
typedef unsigned long	__ino_t;
typedef unsigned short	__mode_t;
typedef unsigned short	__nlink_t;
typedef long		__off_t;
typedef int		__pid_t;
typedef unsigned short	__uid_t;
typedef unsigned short	__gid_t;
typedef unsigned int	__size_t;
typedef int		__ssize_t;
typedef int		__ptrdiff_t;
typedef long		__time_t;
typedef long		__clock_t;
typedef int		__daddr_t;
typedef char *		__caddr_t;

#undef	__FD_SET
#define __FD_SET(fd,fdsetp) \
		__asm__ __volatile__("btsl %1,%0": \
			"=m" (*(fd_set *) (fdsetp)):"r" ((int) (fd)))

#undef	__FD_CLR
#define __FD_CLR(fd,fdsetp) \
		__asm__ __volatile__("btrl %1,%0": \
			"=m" (*(fd_set *) (fdsetp)):"r" ((int) (fd)))

#undef	__FD_ISSET
#define __FD_ISSET(fd,fdsetp) (__extension__ ({ \
		unsigned char __result; \
		__asm__ __volatile__("btl %1,%2 ; setb %0" \
			:"=q" (__result) :"r" ((int) (fd)), \
			"m" (*(fd_set *) (fdsetp))); \
		__result; }))

#undef	__FD_ZERO
#define __FD_ZERO(fdsetp) \
		__asm__ __volatile__("cld ; rep ; stosl" \
			:"=m" (*(fd_set *) (fdsetp)) \
			:"a" (0), "c" (__FDSET_INTS), \
			"D" ((fd_set *) (fdsetp)) :"cx","di")

#endif

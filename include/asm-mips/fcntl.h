#ifndef __ASM_MIPS_FCNTL_H
#define __ASM_MIPS_FCNTL_H

/* open/fcntl - O_SYNC is only implemented on blocks devices and on files
   located on an ext2 file system */
#define O_ACCMODE	0x0003
#define O_RDONLY	0x0000
#define O_WRONLY	0x0001
#define O_RDWR		0x0002
#define O_APPEND	0x0008
#define O_SYNC		0x0010
#define O_NONBLOCK	0x0080
#define O_CREAT         0x0100	/* not fcntl */
#define O_TRUNC		0x0200	/* not fcntl */
#define O_EXCL		0x0400	/* not fcntl */
#define O_NOCTTY	0x0800	/* not fcntl */
#define FASYNC		0x1000	/* fcntl, for BSD compatibility */

#define O_NDELAY	O_NONBLOCK

#define F_DUPFD		0	/* dup */
#define F_GETFD		1	/* get f_flags */
#define F_SETFD		2	/* set f_flags */
#define F_GETFL		3	/* more flags (cloexec) */
#define F_SETFL		4
#define F_GETLK		14
#define F_SETLK		6
#define F_SETLKW	7

#define F_SETOWN	24	/*  for sockets. */
#define F_GETOWN	23	/*  for sockets. */

/* for F_[GET|SET]FL */
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

/* for posix fcntl() and lockf() */
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2

/* for old implementation of bsd flock () */
#define F_EXLCK		4	/* or 3 */
#define F_SHLCK		8	/* or 4 */

/* operations for bsd flock(), also used by the kernel implementation */
#define LOCK_SH		1	/* shared lock */
#define LOCK_EX		2	/* exclusive lock */
#define LOCK_NB		4	/* or'd with one of the above to prevent		XXXXXXXXXXXXXXXXXX
				   blocking */
#define LOCK_UN		8	/* remove lock */

#ifdef __KERNEL__
#define F_POSIX		1
#define F_FLOCK		2
#define F_BROKEN	4	/* broken flock() emulation */
#endif /* __KERNEL__ */

typedef struct flock {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	long  l_sysid;			/* XXXXXXXXXXXXXXXXXXXXXXXXX */
	pid_t l_pid;
	long  pad[4];			/* ZZZZZZZZZZZZZZZZZZZZZZZZZZ */
} flock_t;

#endif /* __ASM_MIPS_FCNTL_H */

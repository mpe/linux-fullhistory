#ifndef _UNISTD_H
#define _UNISTD_H

/* ok, this may be a joke, but I'm working on it */
#define _POSIX_VERSION 198808L

#define _POSIX_CHOWN_RESTRICTED	1    /* only root can do a chown (I think..) */
#define _POSIX_NO_TRUNC		1    /* no pathname truncation (but see kernel) */
#define _POSIX_VDISABLE		'\0' /* character to disable things like ^C */
#define _POSIX_JOB_CONTROL	1
#define _POSIX_SAVED_IDS	1    /* Implemented, for whatever good it is */

#define STDIN_FILENO	0
#define STDOUT_FILENO	1
#define STDERR_FILENO	2

#ifndef NULL
#define NULL    ((void *)0)
#endif

/* access */
#define F_OK	0
#define X_OK	1
#define W_OK	2
#define R_OK	4

/* lseek */
#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2

/* _SC stands for System Configuration. We don't use them much */
#define _SC_ARG_MAX		1
#define _SC_CHILD_MAX		2
#define _SC_CLOCKS_PER_SEC	3
#define _SC_NGROUPS_MAX		4
#define _SC_OPEN_MAX		5
#define _SC_JOB_CONTROL		6
#define _SC_SAVED_IDS		7
#define _SC_VERSION		8

/* more (possibly) configurable things - now pathnames */
#define _PC_LINK_MAX		1
#define _PC_MAX_CANON		2
#define _PC_MAX_INPUT		3
#define _PC_NAME_MAX		4
#define _PC_PATH_MAX		5
#define _PC_PIPE_BUF		6
#define _PC_NO_TRUNC		7
#define _PC_VDISABLE		8
#define _PC_CHOWN_RESTRICTED	9

#if 0
/* XXX - <sys/stat.h> illegally <sys/types.h> already.
 * The rest of these includes are also illegal (too much pollution).
 */
#include <sys/types.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <utime.h>

#ifdef __LIBRARY__
#include <linux/unistd.h>
#endif /* __LIBRARY__ */

/* XXX - illegal. */
extern int errno;

#ifdef __cplusplus
extern "C" {
#endif

/* XXX - several non-POSIX functions here, and POSIX functions that are
 * supposed to be declared elsewhere.  Non-promotion of short types in
 * prototypes may cause trouble.  Arg names should be prefixed by
 * underscores.
 */
int access(const char * filename, mode_t mode);	/* XXX - short type */
int acct(const char * filename);
int brk(void * end_data_segment);
/* XXX - POSIX says unsigned alarm(unsigned sec) */
int alarm(int sec);
void * sbrk(ptrdiff_t increment);
int chdir(const char * filename);
int chmod(const char * filename, mode_t mode);	/* XXX - short type */
int chown(const char * filename, uid_t owner, gid_t group); /* XXX - shorts */
int chroot(const char * filename);
int close(int fildes);
int creat(const char * filename, mode_t mode);	/* XXX - short type */
int dup(int fildes);
int execve(const char * filename, char ** argv, char ** envp);
int execv(const char * pathname, char ** argv);
int execvp(const char * file, char ** argv);
int execl(const char * pathname, char * arg0, ...);
int execlp(const char * file, char * arg0, ...);
int execle(const char * pathname, char * arg0, ...);
volatile void exit(int status);
volatile void _exit(int status);
int fcntl(int fildes, int cmd, ...);
pid_t fork(void);
pid_t getpid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int ioctl(int fildes, int cmd, ...);
int kill(pid_t pid, int signal);
int link(const char * filename1, const char * filename2);
off_t lseek(int fildes, off_t offset, int origin);
int mknod(const char * filename, mode_t mode, dev_t dev); /* XXX - shorts */
int mount(const char * specialfile, const char * dir, const char * type, int rwflag);
int nice(int val);
int open(const char * filename, int flag, ...);
int pause(void);
int pipe(int * fildes);
/* XXX**2 - POSIX says unsigned count */
int read(int fildes, char * buf, off_t count);
int setpgrp(void);
int setpgid(pid_t pid,pid_t pgid);	/* XXX - short types */
int setuid(uid_t uid);		/* XXX - short type */
int setgid(gid_t gid);		/* XXX - short type */
void (*signal(int sig, void (*fn)(int)))(int);
int stat(const char * filename, struct stat * stat_buf);
int fstat(int fildes, struct stat * stat_buf);
int stime(time_t * tptr);
int sync(void);
time_t time(time_t * tloc);
time_t times(struct tms * tbuf);
int ulimit(int cmd, long limit);
mode_t umask(mode_t mask);
int umount(const char * specialfile);
int uname(struct utsname * name);
int unlink(const char * filename);
int ustat(dev_t dev, struct ustat * ubuf);
int utime(const char * filename, struct utimbuf * times);
pid_t waitpid(pid_t pid,int * wait_stat,int options);
pid_t wait(int * wait_stat);
/* XXX**2 - POSIX says unsigned count */
int write(int fildes, const char * buf, off_t count);
int dup2(int oldfd, int newfd);
int getppid(void);
pid_t getpgrp(void);
pid_t setsid(void);
int sethostname(char *name, int len);
int setrlimit(int resource, struct rlimit *rlp);
int getrlimit(int resource, struct rlimit *rlp);
int getrusage(int who, struct rusage *rusage);
int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(struct timeval *tv, struct timezone *tz);
int getgroups(int gidsetlen, gid_t *gidset);
int setgroups(int gidsetlen, gid_t *gidset);
int select(int width, fd_set * readfds, fd_set * writefds,
	fd_set * exceptfds, struct timeval * timeout);
int swapon(const char * specialfile);

#ifdef __cplusplus
}
#endif

#endif

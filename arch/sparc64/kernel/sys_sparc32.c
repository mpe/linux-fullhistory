/* $Id: sys_sparc32.c,v 1.6 1997/04/16 14:52:29 jj Exp $
 * sys_sparc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 */

#include <linux/kernel.h>
#include <linux/fs.h> 
#include <linux/signal.h>
#include <linux/utime.h>
#include <linux/resource.h>
#include <linux/sched.h>
#include <linux/times.h>
#include <linux/utime.h>
#include <linux/utsname.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/malloc.h>
#include <linux/uio.h>

#include <asm/types.h>
#include <asm/poll.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>

/* As gcc will warn about casting u32 to some ptr, we have to cast it to unsigned long first, and that's what is A() for.
 * You just do (void *)A(x), instead of having to type (void *)((unsigned long)x) or instead of just (void *)x, which will
 * produce warnings */
#define A(x) ((unsigned long)x)
 
extern asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on);
extern asmlinkage unsigned long sys_brk(unsigned long brk);
extern asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len, unsigned long prot, unsigned long flags, unsigned long fd, unsigned long off);
extern asmlinkage int sys_bdflush(int func, long data);
extern asmlinkage int sys_uselib(const char * library);
extern asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);
extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);
extern asmlinkage int sys_mknod(const char * filename, int mode, dev_t dev);
extern asmlinkage int sys_mkdir(const char * pathname, int mode);
extern asmlinkage int sys_rmdir(const char * pathname);
extern asmlinkage int sys_unlink(const char * pathname);
extern asmlinkage int sys_symlink(const char * oldname, const char * newname);
extern asmlinkage int sys_link(const char * oldname, const char * newname);
extern asmlinkage int sys_rename(const char * oldname, const char * newname);
extern asmlinkage int sys_quotactl(int cmd, const char *special, int id, caddr_t addr);
extern asmlinkage int sys_statfs(const char * path, struct statfs * buf);
extern asmlinkage int sys_fstatfs(unsigned int fd, struct statfs * buf);
extern asmlinkage int sys_truncate(const char * path, unsigned long length);
extern asmlinkage int sys_ftruncate(unsigned int fd, unsigned long length);
extern asmlinkage int sys_utime(char * filename, struct utimbuf * times);
extern asmlinkage int sys_utimes(char * filename, struct timeval * utimes);
extern asmlinkage int sys_access(const char * filename, int mode);
extern asmlinkage int sys_chdir(const char * filename);
extern asmlinkage int sys_chroot(const char * filename);
extern asmlinkage int sys_chmod(const char * filename, mode_t mode);
extern asmlinkage int sys_chown(const char * filename, uid_t user, gid_t group);
extern asmlinkage int sys_open(const char * filename,int flags,int mode);
extern asmlinkage int sys_creat(const char * pathname, int mode);
extern asmlinkage long sys_lseek(unsigned int fd, off_t offset, unsigned int origin);
extern asmlinkage int sys_llseek(unsigned int fd, unsigned long offset_high, unsigned long offset_low, loff_t *result, unsigned int origin);
extern asmlinkage long sys_read(unsigned int fd, char * buf, unsigned long count);
extern asmlinkage long sys_write(unsigned int fd, const char * buf, unsigned long count);
extern asmlinkage long sys_readv(unsigned long fd, const struct iovec * vector, unsigned long count);
extern asmlinkage long sys_writev(unsigned long fd, const struct iovec * vector, unsigned long count);
extern asmlinkage int sys_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp);
extern asmlinkage int sys_poll(struct pollfd * ufds, unsigned int nfds, int timeout);
extern asmlinkage int sys_newstat(char * filename, struct stat * statbuf);
extern asmlinkage int sys_newlstat(char * filename, struct stat * statbuf);
extern asmlinkage int sys_newfstat(unsigned int fd, struct stat * statbuf);
extern asmlinkage int sys_readlink(const char * path, char * buf, int bufsiz);
extern asmlinkage int sys_sysfs(int option, ...);
extern asmlinkage int sys_ustat(dev_t dev, struct ustat * ubuf);
extern asmlinkage int sys_umount(char * name);
extern asmlinkage int sys_mount(char * dev_name, char * dir_name, char * type, unsigned long new_flags, void *data);
extern asmlinkage int sys_syslog(int type, char * bug, int count);
extern asmlinkage int sys_personality(unsigned long personality);
extern asmlinkage int sys_wait4(pid_t pid,unsigned int * stat_addr, int options, struct rusage * ru);
extern asmlinkage int sys_waitpid(pid_t pid,unsigned int * stat_addr, int options);
extern asmlinkage int sys_sysinfo(struct sysinfo *info);
extern asmlinkage int sys_getitimer(int which, struct itimerval *value);
extern asmlinkage int sys_setitimer(int which, struct itimerval *value, struct itimerval *ovalue);
extern asmlinkage int sys_sched_setscheduler(pid_t pid, int policy, struct sched_param *param);
extern asmlinkage int sys_sched_setparam(pid_t pid, struct sched_param *param);
extern asmlinkage int sys_sched_getparam(pid_t pid, struct sched_param *param);
extern asmlinkage int sys_sched_rr_get_interval(pid_t pid, struct timespec *interval);
extern asmlinkage int sys_nanosleep(struct timespec *rqtp, struct timespec *rmtp);
extern asmlinkage int sys_sigprocmask(int how, sigset_t *set, sigset_t *oset);
extern asmlinkage int sys_sigpending(sigset_t *set);
extern asmlinkage unsigned long sys_signal(int signum, __sighandler_t handler);
extern asmlinkage int sys_reboot(int magic1, int magic2, int cmd, void * arg);
extern asmlinkage int sys_acct(const char *name);
extern asmlinkage int sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
extern asmlinkage long sys_times(struct tms * tbuf);
extern asmlinkage int sys_getgroups(int gidsetsize, gid_t *grouplist);
extern asmlinkage int sys_setgroups(int gidsetsize, gid_t *grouplist);
extern asmlinkage int sys_newuname(struct new_utsname * name);
extern asmlinkage int sys_uname(struct old_utsname * name);
extern asmlinkage int sys_olduname(struct oldold_utsname * name);
extern asmlinkage int sys_sethostname(char *name, int len);
extern asmlinkage int sys_gethostname(char *name, int len);
extern asmlinkage int sys_setdomainname(char *name, int len);
extern asmlinkage int sys_getrlimit(unsigned int resource, struct rlimit *rlim);
extern asmlinkage int sys_setrlimit(unsigned int resource, struct rlimit *rlim);
extern asmlinkage int sys_getrusage(int who, struct rusage *ru);
extern asmlinkage int sys_time(int * tloc);
extern asmlinkage int sys_gettimeofday(struct timeval *tv, struct timezone *tz);
extern asmlinkage int sys_settimeofday(struct timeval *tv, struct timezone *tz);
extern asmlinkage int sys_adjtimex(struct timex *txc_p);
extern asmlinkage int sys_msync(unsigned long start, size_t len, int flags);
extern asmlinkage int sys_mlock(unsigned long start, size_t len);
extern asmlinkage int sys_munlock(unsigned long start, size_t len);
extern asmlinkage int sys_munmap(unsigned long addr, size_t len);
extern asmlinkage int sys_mprotect(unsigned long start, size_t len, unsigned long prot);
extern asmlinkage unsigned long sys_mremap(unsigned long addr, unsigned long old_len, unsigned long new_len, unsigned long flags);
extern asmlinkage int sys_swapoff(const char * specialfile);
extern asmlinkage int sys_swapon(const char * specialfile, int swap_flags);
extern asmlinkage int sys_bind(int fd, struct sockaddr *umyaddr, int addrlen);
extern asmlinkage int sys_accept(int fd, struct sockaddr *upeer_sockaddr, int *upeer_addrlen);
extern asmlinkage int sys_connect(int fd, struct sockaddr *uservaddr, int addrlen);
extern asmlinkage int sys_getsockname(int fd, struct sockaddr *usockaddr, int *usockaddr_len);
extern asmlinkage int sys_getpeername(int fd, struct sockaddr *usockaddr, int *usockaddr_len);
extern asmlinkage int sys_send(int fd, void * buff, size_t len, unsigned flags);
extern asmlinkage int sys_sendto(int fd, void * buff, size_t len, unsigned flags, struct sockaddr *addr, int addr_len);
extern asmlinkage int sys_recv(int fd, void * ubuf, size_t size, unsigned flags);
extern asmlinkage int sys_recvfrom(int fd, void * ubuf, size_t size, unsigned flags, struct sockaddr *addr, int *addr_len); 
extern asmlinkage int sys_setsockopt(int fd, int level, int optname, char *optval, int optlen);
extern asmlinkage int sys_getsockopt(int fd, int level, int optname, char *optval, int *optlen);
extern asmlinkage int sys_sendmsg(int fd, struct msghdr *msg, unsigned flags);
extern asmlinkage int sys_recvmsg(int fd, struct msghdr *msg, unsigned int flags);
extern asmlinkage int sys_socketcall(int call, unsigned long *args);

asmlinkage int sys32_ioperm(u32 from, u32 num, int on)
{
	return sys_ioperm((unsigned long)from, (unsigned long)num, on);
}

struct msgbuf32 { s32 mtype; char mtext[1]; };

struct ipc_perm32
{
	key_t    	  key;
        __kernel_uid_t32  uid;
        __kernel_gid_t32  gid;
        __kernel_uid_t32  cuid;
        __kernel_gid_t32  cgid;
        __kernel_mode_t32 mode;
        unsigned short  seq;
};

struct msqid_ds32
{
        struct ipc_perm32 msg_perm;
        u32 msg_first;
        u32 msg_last;
        __kernel_time_t32 msg_stime;
        __kernel_time_t32 msg_rtime;
        __kernel_time_t32 msg_ctime;
        u32 wwait;
        u32 rwait;
        unsigned short msg_cbytes;
        unsigned short msg_qnum;  
        unsigned short msg_qbytes;
        __kernel_ipc_pid_t32 msg_lspid;
        __kernel_ipc_pid_t32 msg_lrpid;
};

struct shmid_ds32 {
        struct ipc_perm32       shm_perm;
        int                     shm_segsz;
        __kernel_time_t32       shm_atime;
        __kernel_time_t32       shm_dtime;
        __kernel_time_t32       shm_ctime;
        __kernel_ipc_pid_t32    shm_cpid; 
        __kernel_ipc_pid_t32    shm_lpid; 
        unsigned short          shm_nattch;
        unsigned short          shm_npages;
        u32			shm_pages;
        u32			attaches; 
};
                                                        
/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit emulation..
 *
 * This is really horribly ugly.
 */

asmlinkage int sys32_ipc (u32 call, int first, int second, int third, u32 ptr, u32 fifth)
{
	int version, err;

	lock_kernel();
	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			/* struct sembuf is the same on 32 and 64bit :)) */
			err = sys_semop (first, (struct sembuf *)A(ptr), second);
			goto out;
		case SEMGET:
			err = sys_semget (first, second, third);
			goto out;
		case SEMCTL: {
			/* XXX union semun32 to union semun64 and back conversion */
			union semun fourth;
			err = -EINVAL;
			if (!ptr)
				goto out;
			err = -EFAULT;
			if(get_user(fourth.__pad, (void **)A(ptr)))
				goto out;
			err = sys_semctl (first, second, third, fourth);
			goto out;
			}
		default:
			err = -EINVAL;
			goto out;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			{
				struct msgbuf *p = kmalloc (second + sizeof (struct msgbuf), GFP_KERNEL);
				
				if (!p) err = -ENOMEM;
				else {
					if (get_user(p->mtype, &(((struct msgbuf32 *)A(ptr))->mtype)) ||
					    __copy_from_user(p->mtext, &(((struct msgbuf32 *)A(ptr))->mtext), second))
						err = -EFAULT;
					else {
						unsigned long old_fs = get_fs();
						set_fs (KERNEL_DS);
						err = sys_msgsnd (first, p, second, third);
						set_fs (old_fs);
					}
					kfree (p);
				}
			}
			goto out;
		case MSGRCV:
			{
				struct msgbuf *p;
				unsigned long old_fs;
				
				if (!version) {
					struct ipc_kludge tmp;
					err = -EINVAL;
					if (!ptr)
						goto out;
					err = -EFAULT;
					if(copy_from_user(&tmp,(struct ipc_kludge *)A(ptr), sizeof (tmp)))
						goto out;
					ptr = tmp.msgp;
					fifth = tmp.msgtyp;
				}
				p = kmalloc (second + sizeof (struct msgbuf), GFP_KERNEL);
				if (!p) {
					err = -EFAULT;
					goto out;
				}
				old_fs = get_fs();
				set_fs (KERNEL_DS);
				err = sys_msgrcv (first, p, second, fifth, third);
				set_fs (old_fs);
				if (put_user (p->mtype, &(((struct msgbuf32 *)A(ptr))->mtype)) ||
				    __copy_to_user(&(((struct msgbuf32 *)A(ptr))->mtext), p->mtext, second))
					err = -EFAULT;
				kfree (p);
				goto out;
			}
		case MSGGET:
			err = sys_msgget ((key_t) first, second);
			goto out;
		case MSGCTL:
			{
				struct msqid_ds m;
				unsigned long old_fs;
				
				switch (second) {
					case IPC_INFO:
					case MSG_INFO:
						/* struct msginfo is the same */
					case IPC_RMID:
						/* and this doesn't care about ptr */
						err = sys_msgctl (first, second, (struct msqid_ds *)A(ptr));
						goto out;
						
					case IPC_SET:
						if (get_user (m.msg_perm.uid, &(((struct msqid_ds32 *)A(ptr))->msg_perm.uid)) ||
						    __get_user (m.msg_perm.gid, &(((struct msqid_ds32 *)A(ptr))->msg_perm.gid)) ||
						    __get_user (m.msg_perm.mode, &(((struct msqid_ds32 *)A(ptr))->msg_perm.mode)) ||
						    __get_user (m.msg_qbytes, &(((struct msqid_ds32 *)A(ptr))->msg_qbytes))) {
							err = -EFAULT;  
							goto out;
						}
					default:
						break;
				}
				old_fs = get_fs();
				set_fs (KERNEL_DS);
				err = sys_msgctl (first, second, &m);
				set_fs (old_fs);
				switch (second) {
					case MSG_STAT:
					case IPC_STAT:
						if (put_user (m.msg_perm.key, &(((struct msqid_ds32 *)A(ptr))->msg_perm.key)) ||
						    __put_user (m.msg_perm.uid, &(((struct msqid_ds32 *)A(ptr))->msg_perm.uid)) ||
						    __put_user (m.msg_perm.gid, &(((struct msqid_ds32 *)A(ptr))->msg_perm.gid)) ||
						    __put_user (m.msg_perm.cuid, &(((struct msqid_ds32 *)A(ptr))->msg_perm.cuid)) ||
						    __put_user (m.msg_perm.cgid, &(((struct msqid_ds32 *)A(ptr))->msg_perm.cgid)) ||
						    __put_user (m.msg_perm.mode, &(((struct msqid_ds32 *)A(ptr))->msg_perm.mode)) ||
						    __put_user (m.msg_perm.seq, &(((struct msqid_ds32 *)A(ptr))->msg_perm.seq)) ||
						    __put_user (m.msg_stime, &(((struct msqid_ds32 *)A(ptr))->msg_stime)) ||
						    __put_user (m.msg_rtime, &(((struct msqid_ds32 *)A(ptr))->msg_rtime)) ||
						    __put_user (m.msg_ctime, &(((struct msqid_ds32 *)A(ptr))->msg_ctime)) ||
						    __put_user (m.msg_cbytes, &(((struct msqid_ds32 *)A(ptr))->msg_cbytes)) ||
						    __put_user (m.msg_qnum, &(((struct msqid_ds32 *)A(ptr))->msg_qnum)) ||
						    __put_user (m.msg_qbytes, &(((struct msqid_ds32 *)A(ptr))->msg_qbytes)) ||
						    __put_user (m.msg_lspid, &(((struct msqid_ds32 *)A(ptr))->msg_lspid)) ||
						    __put_user (m.msg_lrpid, &(((struct msqid_ds32 *)A(ptr))->msg_lrpid)))
							err = -EFAULT;
						break;
					default:
						break;
				}
			}
			goto out;
		default:
			err = -EINVAL;
			goto out;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			case 0: default: {
				unsigned long raddr;
				err = sys_shmat (first, (char *)A(ptr), second, &raddr);
				if (err)
					goto out;
				err = -EFAULT;
				if(put_user (raddr, ((u32 *)A(third))))
					goto out;
				err = 0;
				goto out;
				}
			case 1: /* If iBCS2 should ever run, then for sure in 64bit mode, not 32bit... */
				err = -EINVAL;
				goto out;
			}
		case SHMDT: 
			err = sys_shmdt ((char *)A(ptr));
			goto out;
		case SHMGET:
			err = sys_shmget (first, second, third);
			goto out;
		case SHMCTL:
			{
				struct shmid_ds s;
				unsigned long old_fs;
				
				switch (second) {
					case IPC_INFO:
						/* struct shminfo is the same */
					case SHM_LOCK:
					case SHM_UNLOCK:
					case IPC_RMID:
						/* and these three aren't using ptr at all */
						err = sys_shmctl (first, second, (struct shmid_ds *)A(ptr));
						goto out;
						
					case IPC_SET:
						if (get_user (s.shm_perm.uid, &(((struct shmid_ds32 *)A(ptr))->shm_perm.uid)) ||
						    __get_user (s.shm_perm.gid, &(((struct shmid_ds32 *)A(ptr))->shm_perm.gid)) ||
						    __get_user (s.shm_perm.mode, &(((struct shmid_ds32 *)A(ptr))->shm_perm.mode))) {
							err = -EFAULT; 
							goto out;
						}
					default:
						break;
				}
				old_fs = get_fs();
				set_fs (KERNEL_DS);
				err = sys_shmctl (first, second, &s);
				set_fs (old_fs);
				switch (second) {
					case SHM_INFO:
						{
							struct shm_info32 { int used_ids; u32 shm_tot; u32 shm_rss; u32 shm_swp; u32 swap_attempts; u32 swap_successes; };
							struct shm_info *si = (struct shm_info *)&s;

							if (put_user (si->used_ids, &(((struct shm_info32 *)A(ptr))->used_ids)) ||
							    __put_user (si->shm_tot, &(((struct shm_info32 *)A(ptr))->shm_tot)) ||
							    __put_user (si->shm_rss, &(((struct shm_info32 *)A(ptr))->shm_rss)) ||
							    __put_user (si->shm_swp, &(((struct shm_info32 *)A(ptr))->shm_swp)) ||
							    __put_user (si->swap_attempts, &(((struct shm_info32 *)A(ptr))->swap_attempts)) ||
							    __put_user (si->swap_successes, &(((struct shm_info32 *)A(ptr))->swap_successes)))
								err = -EFAULT;
						}
						break;
					case SHM_STAT:
					case IPC_STAT:
						if (put_user (s.shm_perm.key, &(((struct shmid_ds32 *)A(ptr))->shm_perm.key)) ||
						    __put_user (s.shm_perm.uid, &(((struct shmid_ds32 *)A(ptr))->shm_perm.uid)) ||
						    __put_user (s.shm_perm.gid, &(((struct shmid_ds32 *)A(ptr))->shm_perm.gid)) ||
						    __put_user (s.shm_perm.cuid, &(((struct shmid_ds32 *)A(ptr))->shm_perm.cuid)) ||
						    __put_user (s.shm_perm.cgid, &(((struct shmid_ds32 *)A(ptr))->shm_perm.cgid)) ||
						    __put_user (s.shm_perm.mode, &(((struct shmid_ds32 *)A(ptr))->shm_perm.mode)) ||
						    __put_user (s.shm_perm.seq, &(((struct shmid_ds32 *)A(ptr))->shm_perm.seq)) ||
						    __put_user (s.shm_atime, &(((struct shmid_ds32 *)A(ptr))->shm_atime)) ||
						    __put_user (s.shm_dtime, &(((struct shmid_ds32 *)A(ptr))->shm_dtime)) ||
						    __put_user (s.shm_ctime, &(((struct shmid_ds32 *)A(ptr))->shm_ctime)) ||
						    __put_user (s.shm_segsz, &(((struct shmid_ds32 *)A(ptr))->shm_segsz)) ||
						    __put_user (s.shm_nattch, &(((struct shmid_ds32 *)A(ptr))->shm_nattch)) ||
						    __put_user (s.shm_lpid, &(((struct shmid_ds32 *)A(ptr))->shm_cpid)) ||
						    __put_user (s.shm_cpid, &(((struct shmid_ds32 *)A(ptr))->shm_lpid)))
							err = -EFAULT;
						break;
					default:
						break;
				}
			}
			goto out;
		default:
			err = -EINVAL;
			goto out;
		}
	else
		err = -EINVAL;
out:
	unlock_kernel();
	return err;
}

asmlinkage unsigned long sys32_mmap(u32 addr, u32 len, u32 prot, u32 flags, u32 fd, u32 off)
{
	return sys_mmap((unsigned long)addr, (unsigned long)len, (unsigned long)prot, (unsigned long)flags, 
			(unsigned long)fd, (unsigned long)off);
}

asmlinkage int sys32_bdflush(int func, s32 data)
{
	return sys_bdflush(func, (long)data);
}

asmlinkage int sys32_uselib(u32 library)
{
	return sys_uselib((const char *)A(library));
}

asmlinkage long sys32_fcntl(unsigned int fd, unsigned int cmd, u32 arg)
{
	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		{
			struct flock f;
			unsigned long old_fs;
			long ret;
			
			if (get_user (f.l_type, &(((struct flock32 *)A(arg))->l_type)) ||
			    __get_user (f.l_whence, &(((struct flock32 *)A(arg))->l_whence)) ||
			    __get_user (f.l_start, &(((struct flock32 *)A(arg))->l_start)) ||
			    __get_user (f.l_len, &(((struct flock32 *)A(arg))->l_len)) ||
			    __get_user (f.l_pid, &(((struct flock32 *)A(arg))->l_pid)))
				return -EFAULT;
			old_fs = get_fs(); set_fs (KERNEL_DS);
			ret = sys_fcntl(fd, cmd, (unsigned long)&f);
			set_fs (old_fs);
   			if (__put_user (f.l_type, &(((struct flock32 *)A(arg))->l_type)) ||
			    __put_user (f.l_whence, &(((struct flock32 *)A(arg))->l_whence)) ||
			    __put_user (f.l_start, &(((struct flock32 *)A(arg))->l_start)) ||
			    __put_user (f.l_len, &(((struct flock32 *)A(arg))->l_len)) ||
			    __put_user (f.l_pid, &(((struct flock32 *)A(arg))->l_pid)))
				return -EFAULT;
			return ret;
		}
	default:
		return sys_fcntl(fd, cmd, (unsigned long)arg);
	}
}

/* Conversion of args should be probably done in all the locations where it is handled,
   using if (current->tss.flags & SPARC_FLAG_32BIT */
asmlinkage int sys32_ioctl(unsigned int fd, unsigned int cmd, u32 arg)
{
	return sys_ioctl(fd, cmd, (unsigned long)arg);
}

asmlinkage int sys32_mknod(u32 filename, int mode, __kernel_dev_t32 dev)
{
	return sys_mknod((const char *)A(filename), mode, dev);
}

asmlinkage int sys32_mkdir(u32 pathname, int mode)
{
	return sys_mkdir((const char *)A(pathname), mode);
}

asmlinkage int sys32_rmdir(u32 pathname)
{
	return sys_rmdir((const char *)A(pathname));
}

asmlinkage int sys32_unlink(u32 pathname)
{
	return sys_unlink((const char *)A(pathname));
}

asmlinkage int sys32_symlink(u32 oldname, u32 newname)
{
	return sys_symlink((const char *)A(oldname), (const char *)A(newname));
}

asmlinkage int sys32_link(u32 oldname, u32 newname)
{
	return sys_link((const char *)A(oldname), (const char *)A(newname));
}

asmlinkage int sys32_rename(u32 oldname, u32 newname)
{
	return sys_rename((const char *)A(oldname), (const char *)A(newname));
}

/* XXX: Play with the addr, it will be ugly :(( */
asmlinkage int sys32_quotactl(int cmd, u32 special, int id, u32 addr)
{
	return sys_quotactl(cmd, (const char *)A(special), id, (caddr_t)A(addr));
}

static int put_statfs (u32 buf, struct statfs *s)
{
	if (put_user (s->f_type, &(((struct statfs32 *)A(buf))->f_type)) ||
	    __put_user (s->f_bsize, &(((struct statfs32 *)A(buf))->f_bsize)) ||
	    __put_user (s->f_blocks, &(((struct statfs32 *)A(buf))->f_blocks)) ||
	    __put_user (s->f_bfree, &(((struct statfs32 *)A(buf))->f_bfree)) ||
	    __put_user (s->f_bavail, &(((struct statfs32 *)A(buf))->f_bavail)) ||
	    __put_user (s->f_files, &(((struct statfs32 *)A(buf))->f_files)) ||
	    __put_user (s->f_ffree, &(((struct statfs32 *)A(buf))->f_ffree)) ||
	    __put_user (s->f_namelen, &(((struct statfs32 *)A(buf))->f_namelen)) ||
	    __put_user (s->f_fsid.val[0], &(((struct statfs32 *)A(buf))->f_fsid.val[0])) ||
	    __put_user (s->f_fsid.val[1], &(((struct statfs32 *)A(buf))->f_fsid.val[1])))
		return -EFAULT;
	return 0;
}

asmlinkage int sys32_statfs(u32 path, u32 buf)
{
	int ret;
	struct statfs s;
	unsigned long old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_statfs((const char *)A(path), &s);
	set_fs (old_fs);
	if (put_statfs(buf, &s)) return -EFAULT;
	return ret;
}

asmlinkage int sys32_fstatfs(unsigned int fd, u32 buf)
{
	int ret;
	struct statfs s;
	unsigned long old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_fstatfs(fd, &s);
	set_fs (old_fs);
	if (put_statfs(buf, &s)) return -EFAULT;
	return ret;
}

asmlinkage int sys32_truncate(u32 path, u32 length)
{
	return sys_truncate((const char *)A(path), (unsigned long)length);
}

asmlinkage int sys32_ftruncate(unsigned int fd, u32 length)
{
	return sys_ftruncate(fd, (unsigned long)length);
}

asmlinkage int sys32_utime(u32 filename, u32 times)
{
	struct utimbuf32 { __kernel_time_t32 actime, modtime; };
	struct utimbuf t;
	unsigned long old_fs;
	int ret;
	
	if (get_user (t.actime, &(((struct utimbuf32 *)A(times))->actime)) ||
	    __get_user (t.modtime, &(((struct utimbuf32 *)A(times))->modtime)))
		return -EFAULT;
	old_fs = get_fs();
	set_fs (KERNEL_DS); 
	ret = sys_utime((char *)A(filename), &t);
	set_fs (old_fs);
	return ret;
}

asmlinkage int sys32_utimes(u32 filename, u32 utimes)
{
	/* struct timeval is the same :)) */
	return sys_utimes((char *)A(filename), (struct timeval *)A(utimes));
}

asmlinkage int sys32_access(u32 filename, int mode)
{
	return sys_access((const char *)A(filename), mode);
}

asmlinkage int sys32_chdir(u32 filename)
{
	return sys_chdir((const char *)A(filename));
}

asmlinkage int sys32_chroot(u32 filename)
{
	return sys_chroot((const char *)A(filename));
}

asmlinkage int sys32_chmod(u32 filename, __kernel_mode_t32 mode)
{
	return sys_chmod((const char *)A(filename), mode);
}

asmlinkage int sys32_chown(u32 filename, __kernel_uid_t32 user, __kernel_gid_t32 group)
{
	return sys_chown((const char *)A(filename), user, group);
}

asmlinkage int sys32_open(u32 filename, int flags, int mode)
{
	return sys_open((const char *)A(filename), flags, mode);
}

asmlinkage int sys32_creat(u32 pathname, int mode)
{
	return sys_creat((const char *)A(pathname), mode);
}

asmlinkage long sys32_lseek(unsigned int fd, s32 offset, unsigned int origin)
{
	return sys_lseek(fd, (off_t)offset, origin);
}

asmlinkage int sys32_llseek(unsigned int fd, u32 offset_high, u32 offset_low, u32 result, unsigned int origin)
{
	/* loff_t is the same :)) */
	return sys_llseek(fd, (unsigned long)offset_high, (unsigned long)offset_low, (loff_t *)A(result), origin);
}

asmlinkage long sys32_read(unsigned int fd, u32 buf, u32 count)
{
	return sys_read(fd, (char *)A(buf), (unsigned long)count);
}

asmlinkage long sys32_write(unsigned int fd, u32 buf, u32 count)
{
	return sys_write(fd, (const char *)A(buf), (unsigned long)count);
}

asmlinkage long sys32_readv(u32 fd, u32 vector, u32 count)
{
	struct iovec32 { u32 iov_base; __kernel_size_t32 iov_len; };
	struct iovec *v;
	u32 i;
	long ret;
	unsigned long old_fs;
	
	if (!count) return 0; if (count > UIO_MAXIOV) return -EINVAL;
	v = kmalloc (count * sizeof (struct iovec), GFP_KERNEL); if (!v) return -ENOMEM;
	for (i = 0; i < count; i++) {
		if (__get_user ((unsigned long)(v[i].iov_base), &((((struct iovec32 *)A(vector))+i)->iov_base)) ||
		    __get_user (v[i].iov_len, &((((struct iovec32 *)A(vector))+i)->iov_len))) {
			kfree (v);
			return -EFAULT;
		}
	}
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	ret = sys_readv((unsigned long)fd, v, (unsigned long)count);
	set_fs (old_fs);
	kfree (v);
	return ret;
}

asmlinkage long sys32_writev(u32 fd, u32 vector, u32 count)
{
	struct iovec32 { u32 iov_base; __kernel_size_t32 iov_len; };
	struct iovec *v;
	u32 i;
	long ret;
	unsigned long old_fs;
	
	if (!count) return 0; if (count > UIO_MAXIOV) return -EINVAL;
	v = kmalloc (count * sizeof (struct iovec), GFP_KERNEL); if (!v) return -ENOMEM;
	for (i = 0; i < count; i++) {
		if (__get_user ((unsigned long)(v[i].iov_base), &((((struct iovec32 *)A(vector))+i)->iov_base)) ||
		    __get_user (v[i].iov_len, &((((struct iovec32 *)A(vector))+i)->iov_len))) {
			kfree (v);
			return -EFAULT;
		}
	}
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	ret = sys_writev((unsigned long)fd, v, (unsigned long)count);
	set_fs (old_fs);
	kfree (v);
	return ret;
}

/* readdir & getdents */

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(long)-1) & ~(sizeof(long)-1))

struct old_linux_dirent32 {
	u32		d_ino;
	u32		d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct readdir_callback32 {
	struct old_linux_dirent32 * dirent;
	int count;
};

static int fillonedir(void * __buf, const char * name, int namlen, off_t offset, ino_t ino)
{
	struct readdir_callback32 * buf = (struct readdir_callback32 *) __buf;
	struct old_linux_dirent32 * dirent;

	if (buf->count)
		return -EINVAL;
	buf->count++;
	dirent = buf->dirent;
	put_user(ino, &dirent->d_ino);
	put_user(offset, &dirent->d_offset);
	put_user(namlen, &dirent->d_namlen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	return 0;
}

asmlinkage int old32_readdir(unsigned int fd, u32 dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct readdir_callback32 buf;

	lock_kernel();
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		goto out;
	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;
	error = verify_area(VERIFY_WRITE, (void *)A(dirent), sizeof(struct old_linux_dirent32));
	if (error)
		goto out;
	buf.count = 0;
	buf.dirent = (struct old_linux_dirent32 *)A(dirent);
	error = file->f_op->readdir(file->f_inode, file, &buf, fillonedir);
	if (error < 0)
		goto out;
	error = buf.count;
out:
	unlock_kernel();
	return error;
}

struct linux_dirent32 {
	u32		d_ino;
	u32		d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct getdents_callback32 {
	struct linux_dirent32 * current_dir;
	struct linux_dirent32 * previous;
	int count;
	int error;
};

static int filldir(void * __buf, const char * name, int namlen, off_t offset, ino_t ino)
{
	struct linux_dirent32 * dirent;
	struct getdents_callback32 * buf = (struct getdents_callback32 *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent)
		put_user(offset, &dirent->d_off);
	dirent = buf->current_dir;
	buf->previous = dirent;
	put_user(ino, &dirent->d_ino);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int sys32_getdents(unsigned int fd, u32 dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent32 * lastdirent;
	struct getdents_callback32 buf;
	int error = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		goto out;
	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;
	error = verify_area(VERIFY_WRITE, (void *)A(dirent), count);
	if (error)
		goto out;
	buf.current_dir = (struct linux_dirent32 *) A(dirent);
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;
	error = file->f_op->readdir(file->f_inode, file, &buf, filldir);
	if (error < 0)
		goto out;
	lastdirent = buf.previous;
	if (!lastdirent) {
		error = buf.error;
	} else {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}
out:
	unlock_kernel();
	return error;
}

/* end of readdir & getdents */

asmlinkage int sys32_select(int n, u32 inp, u32 outp, u32 exp, u32 tvp)
{
	unsigned long old_fs;
	char *p;
	u32 *q;
	int i, ret = -EINVAL, nn;
	u32 *Inp, *Outp, *Exp;
	
	if (n < 0 || n > PAGE_SIZE*2) return -EINVAL;
	lock_kernel ();
	p = (char *)__get_free_page (GFP_KERNEL);
	if (!p) goto out;
	q = (u32 *)p;
	nn = (n + 8 * sizeof(unsigned long) - 1) / (8 * sizeof (unsigned long));
	Inp = (u32 *)A(inp); Outp = (u32 *)A(outp); Exp = (u32 *)A(exp);
	ret = -EFAULT;
	for (i = 0; i < ret; i++, Inp += 2, Outp += 2, Exp += 2, q += 2) {
		if (__get_user (q[1], Inp) ||
		    __get_user (q[0], Inp+1) ||
		    __get_user (q[1+PAGE_SIZE/4], Outp) ||
		    __get_user (q[PAGE_SIZE/4], Outp+1) ||
		    __get_user (q[1+PAGE_SIZE/2], Exp) ||
		    __get_user (q[PAGE_SIZE/2], Exp+1))
			goto out;
	}
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	ret = sys_select(n, (fd_set *)p, (fd_set *)(p + PAGE_SIZE/4), (fd_set *)(p + PAGE_SIZE/2), (struct timeval *)A(tvp));
	set_fs (old_fs);
	q = (u32 *)p;
	Inp = (u32 *)A(inp); Outp = (u32 *)A(outp); Exp = (u32 *)A(exp);
	for (i = 0; i < ret; i++, Inp += 2, Outp += 2, Exp += 2, q += 2) {
		if (__put_user (q[1], Inp) ||
		    __put_user (q[0], Inp+1) ||
		    __put_user (q[1+PAGE_SIZE/4], Outp) ||
		    __put_user (q[PAGE_SIZE/4], Outp+1) ||
		    __put_user (q[1+PAGE_SIZE/2], Exp) ||
		    __put_user (q[PAGE_SIZE/2], Exp+1)) {
		    	ret = -EFAULT;
			goto out;
		}
	}
out:
	free_page ((unsigned long)p);
	return ret;
}

asmlinkage int sys32_poll(u32 ufds, unsigned int nfds, int timeout)
{
	return sys_poll((struct pollfd *)A(ufds), nfds, timeout);
}

static inline int putstat(u32 statbuf, struct stat *s)
{
	if (put_user (s->st_dev, &(((struct stat32 *)A(statbuf))->st_dev)) ||
	    __put_user (s->st_ino, &(((struct stat32 *)A(statbuf))->st_ino)) ||
	    __put_user (s->st_mode, &(((struct stat32 *)A(statbuf))->st_mode)) ||
	    __put_user (s->st_nlink, &(((struct stat32 *)A(statbuf))->st_nlink)) ||
	    __put_user (s->st_uid, &(((struct stat32 *)A(statbuf))->st_uid)) ||
	    __put_user (s->st_gid, &(((struct stat32 *)A(statbuf))->st_gid)) ||
	    __put_user (s->st_rdev, &(((struct stat32 *)A(statbuf))->st_rdev)) ||
	    __put_user (s->st_size, &(((struct stat32 *)A(statbuf))->st_size)) ||
	    __put_user (s->st_atime, &(((struct stat32 *)A(statbuf))->st_atime)) ||
	    __put_user (s->st_mtime, &(((struct stat32 *)A(statbuf))->st_mtime)) ||
	    __put_user (s->st_ctime, &(((struct stat32 *)A(statbuf))->st_ctime)) ||
	    __put_user (s->st_blksize, &(((struct stat32 *)A(statbuf))->st_blksize)) ||
	    __put_user (s->st_blocks, &(((struct stat32 *)A(statbuf))->st_blocks)))
		return -EFAULT;
	return 0;
}

asmlinkage int sys32_newstat(u32 filename, u32 statbuf)
{
	int ret;
	struct stat s;
	unsigned long old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newstat((char *)A(filename), &s);
	set_fs (old_fs);
	if (putstat (statbuf, &s)) return -EFAULT;
	return ret;
}

asmlinkage int sys32_newlstat(u32 filename, u32 statbuf)
{
	int ret;
	struct stat s;
	unsigned long old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newlstat((char *)A(filename), &s);
	set_fs (old_fs);
	if (putstat (statbuf, &s)) return -EFAULT;
	return ret;
}

asmlinkage int sys32_newfstat(unsigned int fd, u32 statbuf)
{
	int ret;
	struct stat s;
	unsigned long old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newfstat(fd, &s);
	set_fs (old_fs);
	if (putstat (statbuf, &s)) return -EFAULT;
	return ret;
}

asmlinkage int sys32_readlink(u32 path, u32 buf, int bufsiz)
{
	return sys_readlink((const char *)A(path), (char *)A(buf), bufsiz);
}

asmlinkage int sys32_sysfs(int option, ...)
{
        va_list args;
	unsigned int x;
	int ret = -EINVAL;

        va_start(args, option);
        switch (option) {
                case 1:
			ret = sys_sysfs(option, (const char *)A(va_arg(args, u32)));
			break;
                case 2:
			x = va_arg(args, unsigned int);
			ret = sys_sysfs(option, x, (char *)A(va_arg(args, u32)));
			break;
                case 3:
			ret = sys_sysfs(option);
			break;
	}
        va_end(args);
	return ret;
}

/* Continue here */
asmlinkage int sys32_ustat(dev_t dev, u32 ubuf)
{
	return sys_ustat(dev, (struct ustat *)A(ubuf));
}

asmlinkage int sys32_umount(u32 name)
{
	return sys_umount((char *)A(name));
}

asmlinkage int sys32_mount(u32 dev_name, u32 dir_name, u32 type, u32 new_flags, u32 data)
{
	return sys_mount((char *)A(dev_name), (char *)A(dir_name), (char *)A(type), 
			 (unsigned long)new_flags, (void *)A(data));
}

asmlinkage int sys32_syslog(int type, u32 bug, int count)
{
	return sys_syslog(type, (char *)A(bug), count);
}

asmlinkage int sys32_personality(u32 personality)
{
	return sys_personality((unsigned long)personality);
}

asmlinkage int sys32_wait4(pid_t pid, u32 stat_addr, int options, u32 ru)
{
	return sys_wait4(pid, (unsigned int *)A(stat_addr), options, (struct rusage *)A(ru));
}

asmlinkage int sys32_waitpid(pid_t pid, u32 stat_addr, int options)
{
	return sys_waitpid(pid, (unsigned int *)A(stat_addr), options);
}

asmlinkage int sys32_sysinfo(u32 info)
{
	return sys_sysinfo((struct sysinfo *)A(info));
}

asmlinkage int sys32_getitimer(int which, u32 value)
{
	return sys_getitimer(which, (struct itimerval *)A(value));
}

asmlinkage int sys32_setitimer(int which, u32 value, u32 ovalue)
{
	return sys_setitimer(which, (struct itimerval *)A(value), (struct itimerval *)A(ovalue));
}

asmlinkage int sys32_sched_setscheduler(pid_t pid, int policy, u32 param)
{
	return sys_sched_setscheduler(pid, policy, (struct sched_param *)A(param));
}

asmlinkage int sys32_sched_setparam(pid_t pid, u32 param)
{
	return sys_sched_setparam(pid, (struct sched_param *)A(param));
}

asmlinkage int sys32_sched_getparam(pid_t pid, u32 param)
{
	return sys_sched_getparam(pid, (struct sched_param *)A(param));
}

asmlinkage int sys32_sched_rr_get_interval(pid_t pid, u32 interval)
{
	return sys_sched_rr_get_interval(pid, (struct timespec *)A(interval));
}

asmlinkage int sys32_nanosleep(u32 rqtp, u32 rmtp)
{
	return sys_nanosleep((struct timespec *)A(rqtp), (struct timespec *)A(rmtp));
}

asmlinkage int sys32_sigprocmask(int how, u32 set, u32 oset)
{
	return sys_sigprocmask(how, (sigset_t *)A(set), (sigset_t *)A(oset));
}

asmlinkage int sys32_sigpending(u32 set)
{
	return sys_sigpending((sigset_t *)A(set));
}

asmlinkage unsigned long sys32_signal(int signum, u32 handler)
{
	return sys_signal(signum, (__sighandler_t)A(handler));
}

asmlinkage int sys32_reboot(int magic1, int magic2, int cmd, u32 arg)
{
	return sys_reboot(magic1, magic2, cmd, (void *)A(arg));
}

asmlinkage int sys32_acct(u32 name)
{
	return sys_acct((const char *)A(name));
}

asmlinkage int sys32_getresuid(u32 ruid, u32 euid, u32 suid)
{
	return sys_getresuid((uid_t *)A(ruid), (uid_t *)A(euid), (uid_t *)A(suid));
}

asmlinkage long sys32_times(u32 tbuf)
{
	return sys_times((struct tms *)A(tbuf));
}

asmlinkage int sys32_getgroups(int gidsetsize, u32 grouplist)
{
	return sys_getgroups(gidsetsize, (gid_t *)A(grouplist));
}

asmlinkage int sys32_setgroups(int gidsetsize, u32 grouplist)
{
	return sys_setgroups(gidsetsize, (gid_t *)A(grouplist));
}

asmlinkage int sys32_newuname(u32 name)
{
	return sys_newuname((struct new_utsname *)A(name));
}

asmlinkage int sys32_uname(u32 name)
{
	return sys_uname((struct old_utsname *)A(name));
}

asmlinkage int sys32_olduname(u32 name)
{
	return sys_olduname((struct oldold_utsname *)A(name));
}

asmlinkage int sys32_sethostname(u32 name, int len)
{
	return sys_sethostname((char *)A(name), len);
}

asmlinkage int sys32_gethostname(u32 name, int len)
{
	return sys_gethostname((char *)A(name), len);
}

asmlinkage int sys32_setdomainname(u32 name, int len)
{
	return sys_setdomainname((char *)A(name), len);
}

asmlinkage int sys32_getrlimit(unsigned int resource, u32 rlim)
{
	return sys_getrlimit(resource, (struct rlimit *)A(rlim));
}

asmlinkage int sys32_setrlimit(unsigned int resource, u32 rlim)
{
	return sys_setrlimit(resource, (struct rlimit *)A(rlim));
}

asmlinkage int sys32_getrusage(int who, u32 ru)
{
	return sys_getrusage(who, (struct rusage *)A(ru));
}

asmlinkage int sys32_time(u32 tloc)
{
	return sys_time((int *)A(tloc));
}

asmlinkage int sys32_gettimeofday(u32 tv, u32 tz)
{
	return sys_gettimeofday((struct timeval *)A(tv), (struct timezone *)A(tz));
}

asmlinkage int sys32_settimeofday(u32 tv, u32 tz)
{
	return sys_settimeofday((struct timeval *)A(tv), (struct timezone *)A(tz));
}

asmlinkage int sys32_adjtimex(u32 txc_p)
{
	return sys_adjtimex((struct timex *)A(txc_p));
}

asmlinkage int sys32_msync(u32 start, u32 len, int flags)
{
	return sys_msync((unsigned long)start, (size_t)len, flags);
}

asmlinkage int sys32_mlock(u32 start, u32 len)
{
	return sys_mlock((unsigned long)start, (size_t)len);
}

asmlinkage int sys32_munlock(u32 start, u32 len)
{
	return sys_munlock((unsigned long)start, (size_t)len);
}

asmlinkage unsigned long sparc32_brk(u32 brk)
{
	return sys_brk((unsigned long)brk);
}

asmlinkage int sys32_munmap(u32 addr, u32 len)
{
	return sys_munmap((unsigned long)addr, (size_t)len);
}

asmlinkage int sys32_mprotect(u32 start, u32 len, u32 prot)
{
	return sys_mprotect((unsigned long)start, (size_t)len, (unsigned long)prot);
}

asmlinkage unsigned long sys32_mremap(u32 addr, u32 old_len, u32 new_len, u32 flags)
{
	return sys_mremap((unsigned long)addr, (unsigned long)old_len, (unsigned long)new_len, (unsigned long)flags);
}

asmlinkage int sys32_swapoff(u32 specialfile)
{
	return sys_swapoff((const char *)A(specialfile));
}

asmlinkage int sys32_swapon(u32 specialfile, int swap_flags)
{
	return sys_swapon((const char *)A(specialfile), swap_flags);
}

asmlinkage int sys32_bind(int fd, u32 umyaddr, int addrlen)
{
	return sys_bind(fd, (struct sockaddr *)A(umyaddr), addrlen);
}

asmlinkage int sys32_accept(int fd, u32 upeer_sockaddr, u32 upeer_addrlen)
{
	return sys_accept(fd, (struct sockaddr *)A(upeer_sockaddr), (int *)A(upeer_addrlen));
}

asmlinkage int sys32_connect(int fd, u32 uservaddr, int addrlen)
{
	return sys_connect(fd, (struct sockaddr *)A(uservaddr), addrlen);
}

asmlinkage int sys32_getsockname(int fd, u32 usockaddr, u32 usockaddr_len)
{
	return sys_getsockname(fd, (struct sockaddr *)A(usockaddr), (int *)A(usockaddr_len));
}

asmlinkage int sys32_getpeername(int fd, u32 usockaddr, u32 usockaddr_len)
{
	return sys_getpeername(fd, (struct sockaddr *)A(usockaddr), (int *)A(usockaddr_len));
}

asmlinkage int sys32_send(int fd, u32 buff, u32 len, unsigned flags)
{
	return sys_send(fd, (void *)A(buff), (size_t)len, flags);
}

asmlinkage int sys32_sendto(int fd, u32 buff, u32 len, unsigned flags, u32 addr, int addr_len)
{
	return sys_sendto(fd, (void *)A(buff), (size_t)len, flags, (struct sockaddr *)A(addr), addr_len);
}

asmlinkage int sys32_recv(int fd, u32 ubuf, u32 size, unsigned flags)
{
	return sys_recv(fd, (void *)A(ubuf), (size_t)size, flags);
}

asmlinkage int sys32_recvfrom(int fd, u32 ubuf, u32 size, unsigned flags, u32 addr, u32 addr_len)
{
	return sys_recvfrom(fd, (void *)A(ubuf), (size_t)size, flags, (struct sockaddr *)A(addr), (int *)A(addr_len));
}
 
asmlinkage int sys32_setsockopt(int fd, int level, int optname, u32 optval, int optlen)
{
	return sys_setsockopt(fd, level, optname, (char *)A(optval), optlen);
}

asmlinkage int sys32_getsockopt(int fd, int level, int optname, u32 optval, u32 optlen)
{
	return sys_getsockopt(fd, level, optname, (char *)A(optval), (int *)A(optlen));
}

asmlinkage int sys32_sendmsg(int fd, u32 msg, unsigned flags)
{
	return sys_sendmsg(fd, (struct msghdr *)A(msg), flags);
}

asmlinkage int sys32_recvmsg(int fd, u32 msg, unsigned int flags)
{
	return sys_recvmsg(fd, (struct msghdr *)A(msg), flags);
}

asmlinkage int sys32_socketcall(int call, u32 args)
{
	return sys_socketcall(call, (unsigned long *)A(args));
}

extern void check_pending(int signum);

asmlinkage int sparc32_sigaction (int signum, u32 action, u32 oldaction)
{
	struct sigaction32 new_sa, old_sa;
	struct sigaction *p;
	int err = -EINVAL;

	lock_kernel();
	if(signum < 0) {
		current->tss.new_signal = 1;
		signum = -signum;
	}

	if (signum<1 || signum>32)
		goto out;
	p = signum - 1 + current->sig->action;
	if (action) {
		err = -EINVAL;
		if (signum==SIGKILL || signum==SIGSTOP)
			goto out;
		err = -EFAULT;
		if(copy_from_user(&new_sa, A(action), sizeof(struct sigaction32)))
			goto out;
		if (((__sighandler_t)A(new_sa.sa_handler)) != SIG_DFL && 
		    ((__sighandler_t)A(new_sa.sa_handler)) != SIG_IGN) {
			err = verify_area(VERIFY_READ, (__sighandler_t)A(new_sa.sa_handler), 1);
			if (err)
				goto out;
		}
	}

	if (oldaction) {
		err = -EFAULT;
		old_sa.sa_handler = (unsigned)(u64)(p->sa_handler);
		old_sa.sa_mask = (sigset32_t)(p->sa_mask);
		old_sa.sa_flags = (unsigned)(p->sa_flags);
		old_sa.sa_restorer = (unsigned)(u64)(p->sa_restorer);
		if (copy_to_user(A(oldaction), p, sizeof(struct sigaction32)))
			goto out;	
	}

	if (action) {
		p->sa_handler = (__sighandler_t)A(new_sa.sa_handler);
		p->sa_mask = (sigset_t)(new_sa.sa_mask);
		p->sa_flags = new_sa.sa_flags;
		p->sa_restorer = (void (*)(void))A(new_sa.sa_restorer);
		check_pending(signum);
	}

	err = 0;
out:
	unlock_kernel();
	return err;
}

/* $Id: sys_sparc32.c,v 1.100 1998/11/08 11:14:00 davem Exp $
 * sys_sparc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/file.h> 
#include <linux/signal.h>
#include <linux/utime.h>
#include <linux/resource.h>
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
#include <linux/nfs_fs.h>
#include <linux/smb_fs.h>
#include <linux/smb_mount.h>
#include <linux/ncp_fs.h>
#include <linux/quota.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>

#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>
#include <asm/fpumacro.h>
#include <asm/semaphore.h>

/* Use this to get at 32-bit user passed pointers. */
/* Things to consider: the low-level assembly stub does
   srl x, 0, x for first four arguments, so if you have
   pointer to something in the first four arguments, just
   declare it as a pointer, not u32. On the other side, 
   arguments from 5th onwards should be declared as u32
   for pointers, and need AA() around each usage.
   A() macro should be used for places where you e.g.
   have some internal variable u32 and just want to get
   rid of a compiler warning. AA() has to be used in
   places where you want to convert a function argument
   to 32bit pointer or when you e.g. access pt_regs
   structure and want to consider 32bit registers only.
   -jj
 */
#define A(__x) ((unsigned long)(__x))
#define AA(__x)				\
({	unsigned long __ret;		\
	__asm__ ("srl	%0, 0, %0"	\
		 : "=r" (__ret)		\
		 : "0" (__x));		\
	__ret;				\
})

static inline char * get_page(void)
{
	char * res;
	res = (char *)__get_free_page(GFP_KERNEL);
	return res;
}

#define putname32 putname

/* In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 */
static inline int do_getname32(const char *filename, char *page)
{
	int retval;

	/* 32bit pointer will be always far below TASK_SIZE :)) */
	retval = strncpy_from_user((char *)page, (char *)filename, PAGE_SIZE);
	if (retval > 0) {
		if (retval < PAGE_SIZE)
			return 0;
		return -ENAMETOOLONG;
	} else if (!retval)
		retval = -ENOENT;
	return retval;
}

char * getname32(const char *filename)
{
	char *tmp, *result;

	result = ERR_PTR(-ENOMEM);
	tmp = get_page();
	if (tmp)  {
		int retval = do_getname32(filename, tmp);

		result = tmp;
		if (retval < 0) {
			putname32(tmp);
			result = ERR_PTR(retval);
		}
	}
	return result;
}

/* 32-bit timeval and related flotsam.  */

struct timeval32
{
    int tv_sec, tv_usec;
};

struct itimerval32
{
    struct timeval32 it_interval;
    struct timeval32 it_value;
};

static inline long get_tv32(struct timeval *o, struct timeval32 *i)
{
	return (!access_ok(VERIFY_READ, tv32, sizeof(*tv32)) ||
		(__get_user(o->tv_sec, &i->tv_sec) |
		 __get_user(o->tv_usec, &i->tv_usec)));
}

static inline long put_tv32(struct timeval32 *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}

static inline long get_it32(struct itimerval *o, struct itimerval32 *i)
{
	return (!access_ok(VERIFY_READ, i32, sizeof(*i32)) ||
		(__get_user(o->it_interval.tv_sec, &i->it_interval.tv_sec) |
		 __get_user(o->it_interval.tv_usec, &i->it_interval.tv_usec) |
		 __get_user(o->it_value.tv_sec, &i->it_value.tv_sec) |
		 __get_user(o->it_value.tv_usec, &i->it_value.tv_usec)));
}

static inline long put_it32(struct itimerval32 *o, struct itimerval *i)
{
	return (!access_ok(VERIFY_WRITE, i32, sizeof(*i32)) ||
		(__put_user(i->it_interval.tv_sec, &o->it_interval.tv_sec) |
		 __put_user(i->it_interval.tv_usec, &o->it_interval.tv_usec) |
		 __put_user(i->it_value.tv_sec, &o->it_value.tv_sec) |
		 __put_user(i->it_value.tv_usec, &o->it_value.tv_usec)));
}

extern asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on);

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

struct semid_ds32 {
        struct ipc_perm32 sem_perm;               /* permissions .. see ipc.h */
        __kernel_time_t32 sem_otime;              /* last semop time */
        __kernel_time_t32 sem_ctime;              /* last change time */
        u32 sem_base;              /* ptr to first semaphore in array */
        u32 sem_pending;          /* pending operations to be processed */
        u32 sem_pending_last;    /* last pending operation */
        u32 undo;                  /* undo requests on this array */
        unsigned short  sem_nsems;              /* no. of semaphores in array */
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
};
                                                        
/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit emulation..
 *
 * This is really horribly ugly.
 */
#define IPCOP_MASK(__x)	(1UL << (__x))
static int do_sys32_semctl(int first, int second, int third, void *uptr)
{
	union semun fourth;
	u32 pad;
	int err = -EINVAL;

	if (!uptr)
		goto out;
	err = -EFAULT;
	if (get_user (pad, (u32 *)uptr))
		goto out;
	fourth.__pad = (void *)A(pad);
	if (IPCOP_MASK (third) &
	    (IPCOP_MASK (IPC_INFO) | IPCOP_MASK (SEM_INFO) | IPCOP_MASK (GETVAL) |
	     IPCOP_MASK (GETPID) | IPCOP_MASK (GETNCNT) | IPCOP_MASK (GETZCNT) |
	     IPCOP_MASK (GETALL) | IPCOP_MASK (SETALL) | IPCOP_MASK (IPC_RMID))) {
		err = sys_semctl (first, second, third, fourth);
	} else {
		struct semid_ds s;
		struct semid_ds32 *usp = (struct semid_ds32 *)A(pad);
		mm_segment_t old_fs;
		int need_back_translation;

		if (third == IPC_SET) {
			err = get_user (s.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user (s.sem_perm.gid, &usp->sem_perm.gid);
			err |= __get_user (s.sem_perm.mode, &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s;
		}
		need_back_translation =
			(IPCOP_MASK (third) &
			 (IPCOP_MASK (SEM_STAT) | IPCOP_MASK (IPC_STAT))) != 0;
		if (need_back_translation)
			fourth.__pad = &s;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_semctl (first, second, third, fourth);
		set_fs (old_fs);
		if (need_back_translation) {
			int err2 = put_user (s.sem_perm.key, &usp->sem_perm.key);
			err2 |= __put_user (s.sem_perm.uid, &usp->sem_perm.uid);
			err2 |= __put_user (s.sem_perm.gid, &usp->sem_perm.gid);
			err2 |= __put_user (s.sem_perm.cuid, &usp->sem_perm.cuid);
			err2 |= __put_user (s.sem_perm.cgid, &usp->sem_perm.cgid);
			err2 |= __put_user (s.sem_perm.mode, &usp->sem_perm.mode);
			err2 |= __put_user (s.sem_perm.seq, &usp->sem_perm.seq);
			err2 |= __put_user (s.sem_otime, &usp->sem_otime);
			err2 |= __put_user (s.sem_ctime, &usp->sem_ctime);
			err2 |= __put_user (s.sem_nsems, &usp->sem_nsems);
			if (err2) err = -EFAULT;
		}
	}
out:
	return err;
}

static int do_sys32_msgsnd (int first, int second, int third, void *uptr)
{
	struct msgbuf *p = kmalloc (second + sizeof (struct msgbuf) + 4, GFP_USER);
	struct msgbuf32 *up = (struct msgbuf32 *)uptr;
	mm_segment_t old_fs;
	int err;

	if (!p)
		return -ENOMEM;
	err = get_user (p->mtype, &up->mtype);
	err |= __copy_from_user (p->mtext, &up->mtext, second);
	if (err)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgsnd (first, p, second, third);
	set_fs (old_fs);
out:
	kfree (p);
	return err;
}

static int do_sys32_msgrcv (int first, int second, int msgtyp, int third,
			    int version, void *uptr)
{
	struct msgbuf32 *up;
	struct msgbuf *p;
	mm_segment_t old_fs;
	int err;

	if (!version) {
		struct ipc_kludge *uipck = (struct ipc_kludge *)uptr;
		struct ipc_kludge ipck;

		err = -EINVAL;
		if (!uptr)
			goto out;
		err = -EFAULT;
		if (copy_from_user (&ipck, uipck, sizeof (struct ipc_kludge)))
			goto out;
		uptr = (void *)A(ipck.msgp);
		msgtyp = ipck.msgtyp;
	}
	err = -ENOMEM;
	p = kmalloc (second + sizeof (struct msgbuf) + 4, GFP_USER);
	if (!p)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgrcv (first, p, second + 4, msgtyp, third);
	set_fs (old_fs);
	if (err < 0)
		goto free_then_out;
	up = (struct msgbuf32 *)uptr;
	if (put_user (p->mtype, &up->mtype) ||
	    __copy_to_user (&up->mtext, p->mtext, err))
		err = -EFAULT;
free_then_out:
	kfree (p);
out:
	return err;
}

static int do_sys32_msgctl (int first, int second, void *uptr)
{
	int err;

	if (IPCOP_MASK (second) &
	    (IPCOP_MASK (IPC_INFO) | IPCOP_MASK (MSG_INFO) |
	     IPCOP_MASK (IPC_RMID))) {
		err = sys_msgctl (first, second, (struct msqid_ds *)uptr);
	} else {
		struct msqid_ds m;
		struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;
		mm_segment_t old_fs;

		if (second == IPC_SET) {
			err = get_user (m.msg_perm.uid, &up->msg_perm.uid);
			err |= __get_user (m.msg_perm.gid, &up->msg_perm.gid);
			err |= __get_user (m.msg_perm.mode, &up->msg_perm.mode);
			err |= __get_user (m.msg_qbytes, &up->msg_qbytes);
			if (err)
				goto out;
		}
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_msgctl (first, second, &m);
		set_fs (old_fs);
		if (IPCOP_MASK (second) &
		    (IPCOP_MASK (MSG_STAT) | IPCOP_MASK (IPC_STAT))) {
			int err2 = put_user (m.msg_perm.key, &up->msg_perm.key);
			err2 |= __put_user (m.msg_perm.uid, &up->msg_perm.uid);
			err2 |= __put_user (m.msg_perm.gid, &up->msg_perm.gid);
			err2 |= __put_user (m.msg_perm.cuid, &up->msg_perm.cuid);
			err2 |= __put_user (m.msg_perm.cgid, &up->msg_perm.cgid);
			err2 |= __put_user (m.msg_perm.mode, &up->msg_perm.mode);
			err2 |= __put_user (m.msg_perm.seq, &up->msg_perm.seq);
			err2 |= __put_user (m.msg_stime, &up->msg_stime);
			err2 |= __put_user (m.msg_rtime, &up->msg_rtime);
			err2 |= __put_user (m.msg_ctime, &up->msg_ctime);
			err2 |= __put_user (m.msg_cbytes, &up->msg_cbytes);
			err2 |= __put_user (m.msg_qnum, &up->msg_qnum);
			err2 |= __put_user (m.msg_qbytes, &up->msg_qbytes);
			err2 |= __put_user (m.msg_lspid, &up->msg_lspid);
			err2 |= __put_user (m.msg_lrpid, &up->msg_lrpid);
			if (err2)
				err = -EFAULT;
		}
	}

out:
	return err;
}

static int do_sys32_shmat (int first, int second, int third, int version, void *uptr)
{
	unsigned long raddr;
	u32 *uaddr = (u32 *)A((u32)third);
	int err = -EINVAL;

	if (version == 1)
		goto out;
	err = sys_shmat (first, uptr, second, &raddr);
	if (err)
		goto out;
	err = put_user (raddr, uaddr);
out:
	return err;
}

static int do_sys32_shmctl (int first, int second, void *uptr)
{
	int err;

	if (IPCOP_MASK (second) &
	    (IPCOP_MASK (IPC_INFO) | IPCOP_MASK (SHM_LOCK) | IPCOP_MASK (SHM_UNLOCK) |
	     IPCOP_MASK (IPC_RMID))) {
		err = sys_shmctl (first, second, (struct shmid_ds *)uptr);
	} else {
		struct shmid_ds s;
		struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
		mm_segment_t old_fs;

		if (second == IPC_SET) {
			err = get_user (s.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user (s.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user (s.shm_perm.mode, &up->shm_perm.mode);
			if (err)
				goto out;
		}
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, &s);
		set_fs (old_fs);
		if (err < 0)
			goto out;

		/* Mask it even in this case so it becomes a CSE. */
		if (second == SHM_INFO) {
			struct shm_info32 {
				int used_ids;
				u32 shm_tot, shm_rss, shm_swp;
				u32 swap_attempts, swap_successes;
			} *uip = (struct shm_info32 *)uptr;
			struct shm_info *kp = (struct shm_info *)&s;
			int err2 = put_user (kp->used_ids, &uip->used_ids);
			err2 |= __put_user (kp->shm_tot, &uip->shm_tot);
			err2 |= __put_user (kp->shm_rss, &uip->shm_rss);
			err2 |= __put_user (kp->shm_swp, &uip->shm_swp);
			err2 |= __put_user (kp->swap_attempts, &uip->swap_attempts);
			err2 |= __put_user (kp->swap_successes, &uip->swap_successes);
			if (err2)
				err = -EFAULT;
		} else if (IPCOP_MASK (second) &
			   (IPCOP_MASK (SHM_STAT) | IPCOP_MASK (IPC_STAT))) {
			int err2 = put_user (s.shm_perm.key, &up->shm_perm.key);
			err2 |= __put_user (s.shm_perm.uid, &up->shm_perm.uid);
			err2 |= __put_user (s.shm_perm.gid, &up->shm_perm.gid);
			err2 |= __put_user (s.shm_perm.cuid, &up->shm_perm.cuid);
			err2 |= __put_user (s.shm_perm.cgid, &up->shm_perm.cgid);
			err2 |= __put_user (s.shm_perm.mode, &up->shm_perm.mode);
			err2 |= __put_user (s.shm_perm.seq, &up->shm_perm.seq);
			err2 |= __put_user (s.shm_atime, &up->shm_atime);
			err2 |= __put_user (s.shm_dtime, &up->shm_dtime);
			err2 |= __put_user (s.shm_ctime, &up->shm_ctime);
			err2 |= __put_user (s.shm_segsz, &up->shm_segsz);
			err2 |= __put_user (s.shm_nattch, &up->shm_nattch);
			err2 |= __put_user (s.shm_cpid, &up->shm_cpid);
			err2 |= __put_user (s.shm_lpid, &up->shm_lpid);
			if (err2)
				err = -EFAULT;
		}
	}
out:
	return err;
}

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
			err = sys_semop (first, (struct sembuf *)AA(ptr), second);
			goto out;
		case SEMGET:
			err = sys_semget (first, second, third);
			goto out;
		case SEMCTL:
			err = do_sys32_semctl (first, second, third, (void *)AA(ptr));
			goto out;
		default:
			err = -EINVAL;
			goto out;
		};
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			err = do_sys32_msgsnd (first, second, third, (void *)AA(ptr));
			goto out;
		case MSGRCV:
			err = do_sys32_msgrcv (first, second, fifth, third,
					       version, (void *)AA(ptr));
			goto out;
		case MSGGET:
			err = sys_msgget ((key_t) first, second);
			goto out;
		case MSGCTL:
			err = do_sys32_msgctl (first, second, (void *)AA(ptr));
			goto out;
		default:
			err = -EINVAL;
			goto out;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			err = do_sys32_shmat (first, second, third,
					      version, (void *)AA(ptr));
			goto out;
		case SHMDT: 
			err = sys_shmdt ((char *)AA(ptr));
			goto out;
		case SHMGET:
			err = sys_shmget (first, second, third);
			goto out;
		case SHMCTL:
			err = do_sys32_shmctl (first, second, (void *)AA(ptr));
			goto out;
		default:
			err = -EINVAL;
			goto out;
		}

	err = -EINVAL;

out:
	unlock_kernel();
	return err;
}

static inline int get_flock(struct flock *kfl, struct flock32 *ufl)
{
	int err;
	
	err = get_user(kfl->l_type, &ufl->l_type);
	err |= __get_user(kfl->l_whence, &ufl->l_whence);
	err |= __get_user(kfl->l_start, &ufl->l_start);
	err |= __get_user(kfl->l_len, &ufl->l_len);
	err |= __get_user(kfl->l_pid, &ufl->l_pid);
	return err;
}

static inline int put_flock(struct flock *kfl, struct flock32 *ufl)
{
	int err;
	
	err = __put_user(kfl->l_type, &ufl->l_type);
	err |= __put_user(kfl->l_whence, &ufl->l_whence);
	err |= __put_user(kfl->l_start, &ufl->l_start);
	err |= __put_user(kfl->l_len, &ufl->l_len);
	err |= __put_user(kfl->l_pid, &ufl->l_pid);
	return err;
}

extern asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);

asmlinkage long sys32_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		{
			struct flock f;
			mm_segment_t old_fs;
			long ret;
			
			if(get_flock(&f, (struct flock32 *)arg))
				return -EFAULT;
			old_fs = get_fs(); set_fs (KERNEL_DS);
			ret = sys_fcntl(fd, cmd, (unsigned long)&f);
			set_fs (old_fs);
			if(put_flock(&f, (struct flock32 *)arg))
				return -EFAULT;
			return ret;
		}
	default:
		return sys_fcntl(fd, cmd, (unsigned long)arg);
	}
}

struct dqblk32 {
    __u32 dqb_bhardlimit;
    __u32 dqb_bsoftlimit;
    __u32 dqb_curblocks;
    __u32 dqb_ihardlimit;
    __u32 dqb_isoftlimit;
    __u32 dqb_curinodes;
    __kernel_time_t32 dqb_btime;
    __kernel_time_t32 dqb_itime;
};
                                
extern asmlinkage int sys_quotactl(int cmd, const char *special, int id, caddr_t addr);

asmlinkage int sys32_quotactl(int cmd, const char *special, int id, unsigned long addr)
{
	int cmds = cmd >> SUBCMDSHIFT;
	int err;
	struct dqblk d;
	mm_segment_t old_fs;
	char *spec;
	
	switch (cmds) {
	case Q_GETQUOTA:
		break;
	case Q_SETQUOTA:
	case Q_SETUSE:
	case Q_SETQLIM:
		if (copy_from_user (&d, (struct dqblk32 *)addr,
				    sizeof (struct dqblk32)))
			return -EFAULT;
		d.dqb_itime = ((struct dqblk32 *)&d)->dqb_itime;
		d.dqb_btime = ((struct dqblk32 *)&d)->dqb_btime;
		break;
	default:
		return sys_quotactl(cmd, special,
				    id, (caddr_t)addr);
	}
	spec = getname32 (special);
	err = PTR_ERR(spec);
	if (IS_ERR(spec)) return err;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_quotactl(cmd, (const char *)spec, id, (caddr_t)&d);
	set_fs (old_fs);
	putname32 (spec);
	if (cmds == Q_GETQUOTA) {
		__kernel_time_t b = d.dqb_btime, i = d.dqb_itime;
		((struct dqblk32 *)&d)->dqb_itime = i;
		((struct dqblk32 *)&d)->dqb_btime = b;
		if (copy_to_user ((struct dqblk32 *)addr, &d,
				  sizeof (struct dqblk32)))
			return -EFAULT;
	}
	return err;
}

static inline int put_statfs (struct statfs32 *ubuf, struct statfs *kbuf)
{
	int err;
	
	err = put_user (kbuf->f_type, &ubuf->f_type);
	err |= __put_user (kbuf->f_bsize, &ubuf->f_bsize);
	err |= __put_user (kbuf->f_blocks, &ubuf->f_blocks);
	err |= __put_user (kbuf->f_bfree, &ubuf->f_bfree);
	err |= __put_user (kbuf->f_bavail, &ubuf->f_bavail);
	err |= __put_user (kbuf->f_files, &ubuf->f_files);
	err |= __put_user (kbuf->f_ffree, &ubuf->f_ffree);
	err |= __put_user (kbuf->f_namelen, &ubuf->f_namelen);
	err |= __put_user (kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]);
	err |= __put_user (kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]);
	return err;
}

extern asmlinkage int sys_statfs(const char * path, struct statfs * buf);

asmlinkage int sys32_statfs(const char * path, struct statfs32 *buf)
{
	int ret;
	struct statfs s;
	mm_segment_t old_fs = get_fs();
	char *pth;
	
	pth = getname32 (path);
	ret = PTR_ERR(pth);
	if (!IS_ERR(pth)) {
		set_fs (KERNEL_DS);
		ret = sys_statfs((const char *)pth, &s);
		set_fs (old_fs);
		putname32 (pth);
		if (put_statfs(buf, &s))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_fstatfs(unsigned int fd, struct statfs * buf);

asmlinkage int sys32_fstatfs(unsigned int fd, struct statfs32 *buf)
{
	int ret;
	struct statfs s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_fstatfs(fd, &s);
	set_fs (old_fs);
	if (put_statfs(buf, &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_utime(char * filename, struct utimbuf * times);

struct utimbuf32 {
	__kernel_time_t32 actime, modtime;
};

asmlinkage int sys32_utime(char * filename, struct utimbuf32 *times)
{
	struct utimbuf t;
	mm_segment_t old_fs;
	int ret;
	char *filenam;
	
	if (!times)
		return sys_utime(filename, NULL);
	if (get_user (t.actime, &times->actime) ||
	    __get_user (t.modtime, &times->modtime))
		return -EFAULT;
	filenam = getname32 (filename);
	ret = PTR_ERR(filenam);
	if (!IS_ERR(filenam)) {
		old_fs = get_fs();
		set_fs (KERNEL_DS); 
		ret = sys_utime(filenam, &t);
		set_fs (old_fs);
		putname32 (filenam);
	}
	return ret;
}

struct iovec32 { u32 iov_base; __kernel_size_t32 iov_len; };

typedef ssize_t (*IO_fn_t)(struct file *, char *, size_t, loff_t *);

static long do_readv_writev32(int type, struct file *file,
			      const struct iovec32 *vector, u32 count)
{
	unsigned long tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack, *ivp;
	struct inode *inode;
	long retval, i;
	IO_fn_t fn;

	/* First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	if (!count)
		return 0;
	if(verify_area(VERIFY_READ, vector, sizeof(struct iovec32)*count))
		return -EFAULT;
	if (count > UIO_MAXIOV)
		return -EINVAL;
	if (count > UIO_FASTIOV) {
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return -ENOMEM;
	}

	tot_len = 0;
	i = count;
	ivp = iov;
	while(i > 0) {
		u32 len;
		u32 buf;

		__get_user(len, &vector->iov_len);
		__get_user(buf, &vector->iov_base);
		tot_len += len;
		ivp->iov_base = (void *)A(buf);
		ivp->iov_len = (__kernel_size_t) len;
		vector++;
		ivp++;
		i--;
	}

	inode = file->f_dentry->d_inode;
	retval = locks_verify_area((type == VERIFY_READ) ?
				   FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE,
				   inode, file, file->f_pos, tot_len);
	if (retval) {
		if (iov != iovstack)
			kfree(iov);
		return retval;
	}

	/* Then do the actual IO.  Note that sockets need to be handled
	 * specially as they have atomicity guarantees and can handle
	 * iovec's natively
	 */
	if (inode->i_sock) {
		int err;
		err = sock_readv_writev(type, inode, file, iov, count, tot_len);
		if (iov != iovstack)
			kfree(iov);
		return err;
	}

	if (!file->f_op) {
		if (iov != iovstack)
			kfree(iov);
		return -EINVAL;
	}
	/* VERIFY_WRITE actually means a read, as we write to user space */
	fn = file->f_op->read;
	if (type == VERIFY_READ)
		fn = (IO_fn_t) file->f_op->write;		
	ivp = iov;
	while (count > 0) {
		void * base;
		int len, nr;

		base = ivp->iov_base;
		len = ivp->iov_len;
		ivp++;
		count--;
		nr = fn(file, base, len, &file->f_pos);
		if (nr < 0) {
			if (retval)
				break;
			retval = nr;
			break;
		}
		retval += nr;
		if (nr != len)
			break;
	}
	if (iov != iovstack)
		kfree(iov);
	return retval;
}

asmlinkage long sys32_readv(int fd, struct iovec32 *vector, u32 count)
{
	struct file *file;
	long ret = -EBADF;

	lock_kernel();
	file = fget(fd);
	if(!file)
		goto bad_file;

	if(!(file->f_mode & 1))
		goto out;

	ret = do_readv_writev32(VERIFY_WRITE, file,
				vector, count);
out:
	fput(file);
bad_file:
	unlock_kernel();
	return ret;
}

asmlinkage long sys32_writev(int fd, struct iovec32 *vector, u32 count)
{
	struct file *file;
	int ret = -EBADF;

	lock_kernel();
	file = fget(fd);
	if(!file)
		goto bad_file;

	if(!(file->f_mode & 2))
		goto out;

	down(&file->f_dentry->d_inode->i_sem);
	ret = do_readv_writev32(VERIFY_READ, file,
				vector, count);
	up(&file->f_dentry->d_inode->i_sem);
out:
	fput(file);
bad_file:
	unlock_kernel();
	return ret;
}

/* readdir & getdents */

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(u32)-1) & ~(sizeof(u32)-1))

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

static int fillonedir(void * __buf, const char * name, int namlen,
		      off_t offset, ino_t ino)
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

asmlinkage int old32_readdir(unsigned int fd, struct old_linux_dirent32 *dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct inode * inode;
	struct readdir_callback32 buf;

	lock_kernel();
	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out_putf;
	
	inode = file->f_dentry->d_inode;
	down(&inode->i_sem);
	error = file->f_op->readdir(file, &buf, fillonedir);
	up(&inode->i_sem);
	if (error < 0)
		goto out_putf;
	error = buf.count;

out_putf:
	fput(file);
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

asmlinkage int sys32_getdents(unsigned int fd, struct linux_dirent32 *dirent, unsigned int count)
{
	struct file * file;
	struct inode * inode;
	struct linux_dirent32 * lastdirent;
	struct getdents_callback32 buf;
	int error = -EBADF;

	lock_kernel();
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out_putf;

	inode = file->f_dentry->d_inode;
	down(&inode->i_sem);
	error = file->f_op->readdir(file, &buf, filldir);
	up(&inode->i_sem);
	if (error < 0)
		goto out_putf;
	lastdirent = buf.previous;
	error = buf.error;
	if(lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}
out_putf:
	fput(file);
out:
	unlock_kernel();
	return error;
}

/* end of readdir & getdents */

/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */

static inline int
get_fd_set32(unsigned long n, unsigned long *fdset, u32 *ufdset)
{
	if (ufdset) {
		unsigned long odd;

		if (verify_area(VERIFY_WRITE, ufdset, n*sizeof(u32)))
			return -EFAULT;

		odd = n & 1UL;
		n &= ~1UL;
		while (n) {
			unsigned long h, l;
			__get_user(l, ufdset);
			__get_user(h, ufdset+1);
			ufdset += 2;
			*fdset++ = h << 32 | l;
			n -= 2;
		}
		if (odd)
			__get_user(*fdset, ufdset);
	} else {
		/* Tricky, must clear full unsigned long in the
		 * kernel fdset at the end, this makes sure that
		 * actually happens.
		 */
		memset(fdset, 0, ((n + 1) & ~1)*sizeof(u32));
	}
	return 0;
}

static inline void
set_fd_set32(unsigned long n, u32 *ufdset, unsigned long *fdset)
{
	unsigned long odd;

	if (!ufdset)
		return;

	odd = n & 1UL;
	n &= ~1UL;
	while (n) {
		unsigned long h, l;
		l = *fdset++;
		h = l >> 32;
		__put_user(l, ufdset);
		__put_user(h, ufdset+1);
		ufdset += 2;
		n -= 2;
	}
	if (odd)
		__put_user(*fdset, ufdset);
}

asmlinkage int sys32_select(int n, u32 *inp, u32 *outp, u32 *exp, u32 tvp_x)
{
	fd_set_buffer *fds;
	struct timeval32 *tvp = (struct timeval32 *)AA(tvp_x);
	unsigned long nn;
	long timeout;
	int ret;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		time_t sec, usec;

		if ((ret = verify_area(VERIFY_READ, tvp, sizeof(*tvp)))
		    || (ret = __get_user(sec, &tvp->tv_sec))
		    || (ret = __get_user(usec, &tvp->tv_usec)))
			goto out_nofds;

		timeout = (usec + 1000000/HZ - 1) / (1000000/HZ);
		timeout += sec * HZ;
	}

	ret = -ENOMEM;
	fds = (fd_set_buffer *) __get_free_page(GFP_KERNEL);
	if (!fds)
		goto out_nofds;
	ret = -EINVAL;
	if (n < 0)
		goto out;
	if (n > KFDS_NR)
		n = KFDS_NR;

	nn = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));
	if ((ret = get_fd_set32(nn, fds->in, inp)) ||
	    (ret = get_fd_set32(nn, fds->out, outp)) ||
	    (ret = get_fd_set32(nn, fds->ex, exp)))
		goto out;
	zero_fd_set(n, fds->res_in);
	zero_fd_set(n, fds->res_out);
	zero_fd_set(n, fds->res_ex);

	ret = do_select(n, fds, &timeout);

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		put_user(sec, &tvp->tv_sec);
		put_user(usec, &tvp->tv_usec);
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	set_fd_set32(nn, inp, fds->res_in);
	set_fd_set32(nn, outp, fds->res_out);
	set_fd_set32(nn, exp, fds->res_ex);

out:
	free_page ((unsigned long)fds);
out_nofds:
	return ret;
}

static inline int putstat(struct stat32 *ubuf, struct stat *kbuf)
{
	int err;
	
	err = put_user (kbuf->st_dev, &ubuf->st_dev);
	err |= __put_user (kbuf->st_ino, &ubuf->st_ino);
	err |= __put_user (kbuf->st_mode, &ubuf->st_mode);
	err |= __put_user (kbuf->st_nlink, &ubuf->st_nlink);
	err |= __put_user (kbuf->st_uid, &ubuf->st_uid);
	err |= __put_user (kbuf->st_gid, &ubuf->st_gid);
	err |= __put_user (kbuf->st_rdev, &ubuf->st_rdev);
	err |= __put_user (kbuf->st_size, &ubuf->st_size);
	err |= __put_user (kbuf->st_atime, &ubuf->st_atime);
	err |= __put_user (kbuf->st_mtime, &ubuf->st_mtime);
	err |= __put_user (kbuf->st_ctime, &ubuf->st_ctime);
	err |= __put_user (kbuf->st_blksize, &ubuf->st_blksize);
	err |= __put_user (kbuf->st_blocks, &ubuf->st_blocks);
	return err;
}

extern asmlinkage int sys_newstat(char * filename, struct stat * statbuf);

asmlinkage int sys32_newstat(char * filename, struct stat32 *statbuf)
{
	int ret;
	struct stat s;
	char *filenam;
	mm_segment_t old_fs = get_fs();
	
	filenam = getname32 (filename);
	ret = PTR_ERR(filenam);
	if (!IS_ERR(filenam)) {
		set_fs (KERNEL_DS);
		ret = sys_newstat(filenam, &s);
		set_fs (old_fs);
		putname32 (filenam);
		if (putstat (statbuf, &s))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_newlstat(char * filename, struct stat * statbuf);

asmlinkage int sys32_newlstat(char * filename, struct stat32 *statbuf)
{
	int ret;
	struct stat s;
	char *filenam;
	mm_segment_t old_fs = get_fs();
	
	filenam = getname32 (filename);
	ret = PTR_ERR(filenam);
	if (!IS_ERR(filenam)) {
		set_fs (KERNEL_DS);
		ret = sys_newlstat(filenam, &s);
		set_fs (old_fs);
		putname32 (filenam);
		if (putstat (statbuf, &s))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_newfstat(unsigned int fd, struct stat * statbuf);

asmlinkage int sys32_newfstat(unsigned int fd, struct stat32 *statbuf)
{
	int ret;
	struct stat s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newfstat(fd, &s);
	set_fs (old_fs);
	if (putstat (statbuf, &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_sysfs(int option, unsigned long arg1, unsigned long arg2);

asmlinkage int sys32_sysfs(int option, u32 arg1, u32 arg2)
{
	return sys_sysfs(option, arg1, arg2);
}

struct ncp_mount_data32 {
        int version;
        unsigned int ncp_fd;
        __kernel_uid_t32 mounted_uid;
        __kernel_pid_t32 wdog_pid;
        unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
        unsigned int time_out;
        unsigned int retry_count;
        unsigned int flags;
        __kernel_uid_t32 uid;
        __kernel_gid_t32 gid;
        __kernel_mode_t32 file_mode;
        __kernel_mode_t32 dir_mode;
};

static void *do_ncp_super_data_conv(void *raw_data)
{
	struct ncp_mount_data *n = (struct ncp_mount_data *)raw_data;
	struct ncp_mount_data32 *n32 = (struct ncp_mount_data32 *)raw_data;

	n->dir_mode = n32->dir_mode;
	n->file_mode = n32->file_mode;
	n->gid = n32->gid;
	n->uid = n32->uid;
	memmove (n->mounted_vol, n32->mounted_vol, (sizeof (n32->mounted_vol) + 3 * sizeof (unsigned int)));
	n->wdog_pid = n32->wdog_pid;
	n->mounted_uid = n32->mounted_uid;
	return raw_data;
}

struct smb_mount_data32 {
        int version;
        __kernel_uid_t32 mounted_uid;
        __kernel_uid_t32 uid;
        __kernel_gid_t32 gid;
        __kernel_mode_t32 file_mode;
        __kernel_mode_t32 dir_mode;
};

static void *do_smb_super_data_conv(void *raw_data)
{
	struct smb_mount_data *s = (struct smb_mount_data *)raw_data;
	struct smb_mount_data32 *s32 = (struct smb_mount_data32 *)raw_data;

	s->version = s32->version;
	s->mounted_uid = s32->mounted_uid;
	s->uid = s32->uid;
	s->gid = s32->gid;
	s->file_mode = s32->file_mode;
	s->dir_mode = s32->dir_mode;
	return raw_data;
}

static int copy_mount_stuff_to_kernel(const void *user, unsigned long *kernel)
{
	int i;
	unsigned long page;
	struct vm_area_struct *vma;

	*kernel = 0;
	if(!user)
		return 0;
	vma = find_vma(current->mm, (unsigned long)user);
	if(!vma || (unsigned long)user < vma->vm_start)
		return -EFAULT;
	if(!(vma->vm_flags & VM_READ))
		return -EFAULT;
	i = vma->vm_end - (unsigned long) user;
	if(PAGE_SIZE <= (unsigned long) i)
		i = PAGE_SIZE - 1;
	if(!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	if(copy_from_user((void *) page, user, i)) {
		free_page(page);
		return -EFAULT;
	}
	*kernel = page;
	return 0;
}

extern asmlinkage int sys_mount(char * dev_name, char * dir_name, char * type,
				unsigned long new_flags, void *data);

#define SMBFS_NAME	"smbfs"
#define NCPFS_NAME	"ncpfs"

asmlinkage int sys32_mount(char *dev_name, char *dir_name, char *type, unsigned long new_flags, u32 data)
{
	unsigned long type_page;
	int err, is_smb, is_ncp;

	if(!capable(CAP_SYS_ADMIN))
		return -EPERM;
	is_smb = is_ncp = 0;
	err = copy_mount_stuff_to_kernel((const void *)type, &type_page);
	if(err)
		return err;
	if(type_page) {
		is_smb = !strcmp((char *)type_page, SMBFS_NAME);
		is_ncp = !strcmp((char *)type_page, NCPFS_NAME);
	}
	if(!is_smb && !is_ncp) {
		if(type_page)
			free_page(type_page);
		return sys_mount(dev_name, dir_name, type, new_flags, (void *)AA(data));
	} else {
		unsigned long dev_page, dir_page, data_page;
		mm_segment_t old_fs;

		err = copy_mount_stuff_to_kernel((const void *)dev_name, &dev_page);
		if(err)
			goto out;
		err = copy_mount_stuff_to_kernel((const void *)dir_name, &dir_page);
		if(err)
			goto dev_out;
		err = copy_mount_stuff_to_kernel((const void *)AA(data), &data_page);
		if(err)
			goto dir_out;
		if(is_ncp)
			do_ncp_super_data_conv((void *)data_page);
		else if(is_smb)
			do_smb_super_data_conv((void *)data_page);
		else
			panic("The problem is here...");
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_mount((char *)dev_page, (char *)dir_page,
				(char *)type_page, new_flags,
				(void *)data_page);
		set_fs(old_fs);

		if(data_page)
			free_page(data_page);
	dir_out:
		if(dir_page)
			free_page(dir_page);
	dev_out:
		if(dev_page)
			free_page(dev_page);
	out:
		if(type_page)
			free_page(type_page);
		return err;
	}
}

struct rusage32 {
        struct timeval32 ru_utime;
        struct timeval32 ru_stime;
        s32    ru_maxrss;
        s32    ru_ixrss;
        s32    ru_idrss;
        s32    ru_isrss;
        s32    ru_minflt;
        s32    ru_majflt;
        s32    ru_nswap;
        s32    ru_inblock;
        s32    ru_oublock;
        s32    ru_msgsnd; 
        s32    ru_msgrcv; 
        s32    ru_nsignals;
        s32    ru_nvcsw;
        s32    ru_nivcsw;
};

static int put_rusage (struct rusage32 *ru, struct rusage *r)
{
	int err;
	
	err = put_user (r->ru_utime.tv_sec, &ru->ru_utime.tv_sec);
	err |= __put_user (r->ru_utime.tv_usec, &ru->ru_utime.tv_usec);
	err |= __put_user (r->ru_stime.tv_sec, &ru->ru_stime.tv_sec);
	err |= __put_user (r->ru_stime.tv_usec, &ru->ru_stime.tv_usec);
	err |= __put_user (r->ru_maxrss, &ru->ru_maxrss);
	err |= __put_user (r->ru_ixrss, &ru->ru_ixrss);
	err |= __put_user (r->ru_idrss, &ru->ru_idrss);
	err |= __put_user (r->ru_isrss, &ru->ru_isrss);
	err |= __put_user (r->ru_minflt, &ru->ru_minflt);
	err |= __put_user (r->ru_majflt, &ru->ru_majflt);
	err |= __put_user (r->ru_nswap, &ru->ru_nswap);
	err |= __put_user (r->ru_inblock, &ru->ru_inblock);
	err |= __put_user (r->ru_oublock, &ru->ru_oublock);
	err |= __put_user (r->ru_msgsnd, &ru->ru_msgsnd);
	err |= __put_user (r->ru_msgrcv, &ru->ru_msgrcv);
	err |= __put_user (r->ru_nsignals, &ru->ru_nsignals);
	err |= __put_user (r->ru_nvcsw, &ru->ru_nvcsw);
	err |= __put_user (r->ru_nivcsw, &ru->ru_nivcsw);
	return err;
}

extern asmlinkage int sys_wait4(pid_t pid,unsigned int * stat_addr,
				int options, struct rusage * ru);

asmlinkage int sys32_wait4(__kernel_pid_t32 pid, unsigned int *stat_addr, int options, struct rusage32 *ru)
{
	if (!ru)
		return sys_wait4(pid, stat_addr, options, NULL);
	else {
		struct rusage r;
		int ret;
		unsigned int status;
		mm_segment_t old_fs = get_fs();
		
		set_fs (KERNEL_DS);
		ret = sys_wait4(pid, stat_addr ? &status : NULL, options, &r);
		set_fs (old_fs);
		if (put_rusage (ru, &r)) return -EFAULT;
		if (stat_addr && put_user (status, stat_addr))
			return -EFAULT;
		return ret;
	}
}

struct sysinfo32 {
        s32 uptime;
        u32 loads[3];
        u32 totalram;
        u32 freeram;
        u32 sharedram;
        u32 bufferram;
        u32 totalswap;
        u32 freeswap;
        unsigned short procs;
        char _f[22];
};

extern asmlinkage int sys_sysinfo(struct sysinfo *info);

asmlinkage int sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo s;
	int ret, err;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);
	err = put_user (s.uptime, &info->uptime);
	err |= __put_user (s.loads[0], &info->loads[0]);
	err |= __put_user (s.loads[1], &info->loads[1]);
	err |= __put_user (s.loads[2], &info->loads[2]);
	err |= __put_user (s.totalram, &info->totalram);
	err |= __put_user (s.freeram, &info->freeram);
	err |= __put_user (s.sharedram, &info->sharedram);
	err |= __put_user (s.bufferram, &info->bufferram);
	err |= __put_user (s.totalswap, &info->totalswap);
	err |= __put_user (s.freeswap, &info->freeswap);
	err |= __put_user (s.procs, &info->procs);
	if (err)
		return -EFAULT;
	return ret;
}

struct timespec32 {
	s32    tv_sec;
	s32    tv_nsec;
};
                
extern asmlinkage int sys_sched_rr_get_interval(pid_t pid, struct timespec *interval);

asmlinkage int sys32_sched_rr_get_interval(__kernel_pid_t32 pid, struct timespec32 *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, &t);
	set_fs (old_fs);
	if (put_user (t.tv_sec, &interval->tv_sec) ||
	    __put_user (t.tv_nsec, &interval->tv_nsec))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_nanosleep(struct timespec *rqtp, struct timespec *rmtp);

asmlinkage int sys32_nanosleep(struct timespec32 *rqtp, struct timespec32 *rmtp)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	if (get_user (t.tv_sec, &rqtp->tv_sec) ||
	    __get_user (t.tv_nsec, &rqtp->tv_nsec))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_nanosleep(&t, rmtp ? &t : NULL);
	set_fs (old_fs);
	if (rmtp && ret == -EINTR) {
		if (__put_user (t.tv_sec, &rmtp->tv_sec) ||
	    	    __put_user (t.tv_nsec, &rmtp->tv_nsec))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_sigprocmask(int how, old_sigset_t *set, old_sigset_t *oset);

asmlinkage int sys32_sigprocmask(int how, old_sigset_t32 *set, old_sigset_t32 *oset)
{
	old_sigset_t s;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (set && get_user (s, set)) return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL);
	set_fs (old_fs);
	if (ret) return ret;
	if (oset && put_user (s, oset)) return -EFAULT;
	return 0;
}

extern asmlinkage int sys_rt_sigprocmask(int how, sigset_t *set, sigset_t *oset, size_t sigsetsize);

asmlinkage int sys32_rt_sigprocmask(int how, sigset_t32 *set, sigset_t32 *oset, __kernel_size_t32 sigsetsize)
{
	sigset_t s;
	sigset_t32 s32;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (set) {
		if (copy_from_user (&s32, set, sizeof(sigset_t32)))
			return -EFAULT;
		switch (_NSIG_WORDS) {
		case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
		case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
		case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
		case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
		}
	}
	set_fs (KERNEL_DS);
	ret = sys_rt_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL, sigsetsize);
	set_fs (old_fs);
	if (ret) return ret;
	if (oset) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user (oset, &s32, sizeof(sigset_t32)))
			return -EFAULT;
	}
	return 0;
}

extern asmlinkage int sys_sigpending(old_sigset_t *set);

asmlinkage int sys32_sigpending(old_sigset_t32 *set)
{
	old_sigset_t s;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_sigpending(&s);
	set_fs (old_fs);
	if (put_user (s, set)) return -EFAULT;
	return ret;
}

extern asmlinkage int sys_rt_sigpending(sigset_t *set, size_t sigsetsize);

asmlinkage int sys32_rt_sigpending(sigset_t32 *set, __kernel_size_t32 sigsetsize)
{
	sigset_t s;
	sigset_t32 s32;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_rt_sigpending(&s, sigsetsize);
	set_fs (old_fs);
	if (!ret) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user (set, &s32, sizeof(sigset_t32)))
			return -EFAULT;
	}
	return ret;
}

siginfo_t32 *
siginfo64to32(siginfo_t32 *d, siginfo_t *s)
{
	memset (&d, 0, sizeof(siginfo_t32));
	d->si_signo = s->si_signo;
	d->si_errno = s->si_errno;
	d->si_code = s->si_code;
	if (s->si_signo >= SIGRTMIN) {
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		/* XXX: Ouch, how to find this out??? */
		d->si_int = s->si_int;
	} else switch (s->si_signo) {
	/* XXX: What about POSIX1.b timers */
	case SIGCHLD:
		d->si_pid = s->si_pid;
		d->si_status = s->si_status;
		d->si_utime = s->si_utime;
		d->si_stime = s->si_stime;
		break;
	case SIGSEGV:
	case SIGBUS:
	case SIGFPE:
	case SIGILL:
		d->si_addr = (long)(s->si_addr);
		/* XXX: Do we need to translate this from sparc64 to sparc32 traps? */
		d->si_trapno = s->si_trapno;
		break;
	case SIGPOLL:
		d->si_band = s->si_band;
		d->si_fd = s->si_fd;
		break;
	default:
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		break;
	}
	return d;
}

siginfo_t *
siginfo32to64(siginfo_t *d, siginfo_t32 *s)
{
	d->si_signo = s->si_signo;
	d->si_errno = s->si_errno;
	d->si_code = s->si_code;
	if (s->si_signo >= SIGRTMIN) {
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		/* XXX: Ouch, how to find this out??? */
		d->si_int = s->si_int;
	} else switch (s->si_signo) {
	/* XXX: What about POSIX1.b timers */
	case SIGCHLD:
		d->si_pid = s->si_pid;
		d->si_status = s->si_status;
		d->si_utime = s->si_utime;
		d->si_stime = s->si_stime;
		break;
	case SIGSEGV:
	case SIGBUS:
	case SIGFPE:
	case SIGILL:
		d->si_addr = (void *)A(s->si_addr);
		/* XXX: Do we need to translate this from sparc32 to sparc64 traps? */
		d->si_trapno = s->si_trapno;
		break;
	case SIGPOLL:
		d->si_band = s->si_band;
		d->si_fd = s->si_fd;
		break;
	default:
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		break;
	}
	return d;
}

extern asmlinkage int
sys_rt_sigtimedwait(const sigset_t *uthese, siginfo_t *uinfo,
		    const struct timespec *uts, size_t sigsetsize);

asmlinkage int
sys32_rt_sigtimedwait(sigset_t32 *uthese, siginfo_t32 *uinfo,
		      struct timespec32 *uts, __kernel_size_t32 sigsetsize)
{
	sigset_t s;
	sigset_t32 s32;
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();
	siginfo_t info;
	siginfo_t32 info32;
		
	if (copy_from_user (&s32, uthese, sizeof(sigset_t32)))
		return -EFAULT;
	switch (_NSIG_WORDS) {
	case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
	case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
	case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
	case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
	}
	if (uts) {
		ret = get_user (t.tv_sec, &uts->tv_sec);
		ret |= __get_user (t.tv_nsec, &uts->tv_nsec);
		if (ret)
			return -EFAULT;
	}
	set_fs (KERNEL_DS);
	ret = sys_rt_sigtimedwait(&s, &info, &t, sigsetsize);
	set_fs (old_fs);
	if (ret >= 0 && uinfo) {
		if (copy_to_user (uinfo, siginfo64to32(&info32, &info), sizeof(siginfo_t32)))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int
sys_rt_sigqueueinfo(int pid, int sig, siginfo_t *uinfo);

asmlinkage int
sys32_rt_sigqueueinfo(int pid, int sig, siginfo_t32 *uinfo)
{
	siginfo_t info;
	siginfo_t32 info32;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (copy_from_user (&info32, uinfo, sizeof(siginfo_t32)))
		return -EFAULT;
	/* XXX: Is this correct? */
	siginfo32to64(&info, &info32);
	set_fs (KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, &info);
	set_fs (old_fs);
	return ret;
}

extern asmlinkage int sys_setreuid(uid_t ruid, uid_t euid);

asmlinkage int sys32_setreuid(__kernel_uid_t32 ruid, __kernel_uid_t32 euid)
{
	uid_t sruid, seuid;

	sruid = (ruid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)ruid);
	seuid = (euid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)euid);
	return sys_setreuid(sruid, seuid);
}

extern asmlinkage int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);

asmlinkage int sys32_setresuid(__kernel_uid_t32 ruid,
			       __kernel_uid_t32 euid,
			       __kernel_uid_t32 suid)
{
	uid_t sruid, seuid, ssuid;

	sruid = (ruid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)ruid);
	seuid = (euid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)euid);
	ssuid = (suid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)suid);
	return sys_setresuid(sruid, seuid, ssuid);
}

extern asmlinkage int sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);

asmlinkage int sys32_getresuid(__kernel_uid_t32 *ruid, __kernel_uid_t32 *euid, __kernel_uid_t32 *suid)
{
	uid_t a, b, c;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_getresuid(&a, &b, &c);
	set_fs (old_fs);
	if (put_user (a, ruid) || put_user (b, euid) || put_user (c, suid))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_setregid(gid_t rgid, gid_t egid);

asmlinkage int sys32_setregid(__kernel_gid_t32 rgid, __kernel_gid_t32 egid)
{
	gid_t srgid, segid;

	srgid = (rgid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)rgid);
	segid = (egid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)egid);
	return sys_setregid(srgid, segid);
}

extern asmlinkage int sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);

asmlinkage int sys32_setresgid(__kernel_gid_t32 rgid,
			       __kernel_gid_t32 egid,
			       __kernel_gid_t32 sgid)
{
	gid_t srgid, segid, ssgid;

	srgid = (rgid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)rgid);
	segid = (egid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)egid);
	ssgid = (sgid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)sgid);
	return sys_setresgid(srgid, segid, ssgid);
}

extern asmlinkage int sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);

asmlinkage int sys32_getresgid(__kernel_gid_t32 *rgid, __kernel_gid_t32 *egid, __kernel_gid_t32 *sgid)
{
	gid_t a, b, c;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_getresgid(&a, &b, &c);
	set_fs (old_fs);
	if (!ret) {
		ret = put_user (a, rgid);
		ret |= put_user (b, egid);
		ret |= put_user (c, sgid);
	}
	return ret;
}

struct tms32 {
	__kernel_clock_t32 tms_utime;
	__kernel_clock_t32 tms_stime;
	__kernel_clock_t32 tms_cutime;
	__kernel_clock_t32 tms_cstime;
};
                                
extern asmlinkage long sys_times(struct tms * tbuf);

asmlinkage long sys32_times(struct tms32 *tbuf)
{
	struct tms t;
	long ret;
	mm_segment_t old_fs = get_fs ();
	int err;
	
	set_fs (KERNEL_DS);
	ret = sys_times(tbuf ? &t : NULL);
	set_fs (old_fs);
	if (tbuf) {
		err = put_user (t.tms_utime, &tbuf->tms_utime);
		err |= __put_user (t.tms_stime, &tbuf->tms_stime);
		err |= __put_user (t.tms_cutime, &tbuf->tms_cutime);
		err |= __put_user (t.tms_cstime, &tbuf->tms_cstime);
		if (err)
			ret = -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_getgroups(int gidsetsize, gid_t *grouplist);

asmlinkage int sys32_getgroups(int gidsetsize, __kernel_gid_t32 *grouplist)
{
	gid_t gl[NGROUPS];
	int ret, i;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_getgroups(gidsetsize, gl);
	set_fs (old_fs);
	if (gidsetsize && ret > 0 && ret <= NGROUPS)
		for (i = 0; i < ret; i++, grouplist++)
			if (__put_user (gl[i], grouplist))
				return -EFAULT;
	return ret;
}

extern asmlinkage int sys_setgroups(int gidsetsize, gid_t *grouplist);

asmlinkage int sys32_setgroups(int gidsetsize, __kernel_gid_t32 *grouplist)
{
	gid_t gl[NGROUPS];
	int ret, i;
	mm_segment_t old_fs = get_fs ();
	
	if ((unsigned) gidsetsize > NGROUPS)
		return -EINVAL;
	for (i = 0; i < gidsetsize; i++, grouplist++)
		if (__get_user (gl[i], grouplist))
			return -EFAULT;
        set_fs (KERNEL_DS);
	ret = sys_setgroups(gidsetsize, gl);
	set_fs (old_fs);
	return ret;
}

#define RLIM_INFINITY32	0x7fffffff
#define RESOURCE32(x) ((x > RLIM_INFINITY32) ? RLIM_INFINITY32 : x)

struct rlimit32 {
	s32	rlim_cur;
	s32	rlim_max;
};

extern asmlinkage int sys_getrlimit(unsigned int resource, struct rlimit *rlim);

asmlinkage int sys32_getrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_getrlimit(resource, &r);
	set_fs (old_fs);
	if (!ret) {
		ret = put_user (RESOURCE32(r.rlim_cur), &rlim->rlim_cur);
		ret |= __put_user (RESOURCE32(r.rlim_max), &rlim->rlim_max);
	}
	return ret;
}

extern asmlinkage int sys_setrlimit(unsigned int resource, struct rlimit *rlim);

asmlinkage int sys32_setrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();

	if (resource >= RLIM_NLIMITS) return -EINVAL;	
	if (get_user (r.rlim_cur, &rlim->rlim_cur) ||
	    __get_user (r.rlim_max, &rlim->rlim_max))
		return -EFAULT;
	if (r.rlim_cur == RLIM_INFINITY32)
		r.rlim_cur = RLIM_INFINITY;
	if (r.rlim_max == RLIM_INFINITY32)
		r.rlim_max = RLIM_INFINITY;
	set_fs (KERNEL_DS);
	ret = sys_setrlimit(resource, &r);
	set_fs (old_fs);
	return ret;
}

extern asmlinkage int sys_getrusage(int who, struct rusage *ru);

asmlinkage int sys32_getrusage(int who, struct rusage32 *ru)
{
	struct rusage r;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_getrusage(who, &r);
	set_fs (old_fs);
	if (put_rusage (ru, &r)) return -EFAULT;
	return ret;
}

struct timex32 {
	unsigned int modes;
	s32 offset;
	s32 freq;
	s32 maxerror;
	s32 esterror;
	int status;
	s32 constant;
	s32 precision;
	s32 tolerance;
	struct timeval32 time;
	s32 tick;
	s32 ppsfreq;
	s32 jitter;
	int shift;
	s32 stabil;
	s32 jitcnt;
	s32 calcnt;
	s32 errcnt;
	s32 stbcnt;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
};

extern int do_adjtimex(struct timex *);

asmlinkage int sys32_adjtimex(struct timex32 *txc_p)
{
	struct timex t;
	int ret;

	ret = get_user (t.modes, &txc_p->modes);
	ret |= __get_user (t.offset, &txc_p->offset);
	ret |= __get_user (t.freq, &txc_p->freq);
	ret |= __get_user (t.maxerror, &txc_p->maxerror);
	ret |= __get_user (t.esterror, &txc_p->esterror);
	ret |= __get_user (t.status, &txc_p->status);
	ret |= __get_user (t.constant, &txc_p->constant);
	ret |= __get_user (t.tick, &txc_p->tick);
	ret |= __get_user (t.shift, &txc_p->shift);
	if (ret || (ret = do_adjtimex(&t)))
		return ret;
	ret = __put_user (t.modes, &txc_p->modes);
	ret |= __put_user (t.offset, &txc_p->offset);
	ret |= __put_user (t.freq, &txc_p->freq);
	ret |= __put_user (t.maxerror, &txc_p->maxerror);
	ret |= __put_user (t.esterror, &txc_p->esterror);
	ret |= __put_user (t.status, &txc_p->status);
	ret |= __put_user (t.constant, &txc_p->constant);
	ret |= __put_user (t.precision, &txc_p->precision);
	ret |= __put_user (t.tolerance, &txc_p->tolerance);
	ret |= __put_user (t.time.tv_sec, &txc_p->time.tv_sec);
	ret |= __put_user (t.time.tv_usec, &txc_p->time.tv_usec);
	ret |= __put_user (t.tick, &txc_p->tick);
	ret |= __put_user (t.ppsfreq, &txc_p->ppsfreq);
	ret |= __put_user (t.jitter, &txc_p->jitter);
	ret |= __put_user (t.shift, &txc_p->shift);
	ret |= __put_user (t.stabil, &txc_p->stabil);
	ret |= __put_user (t.jitcnt, &txc_p->jitcnt);
	ret |= __put_user (t.calcnt, &txc_p->calcnt);
	ret |= __put_user (t.errcnt, &txc_p->errcnt);
	ret |= __put_user (t.stbcnt, &txc_p->stbcnt);
	if (!ret)
		ret = time_state;
	return ret;
}

/* XXX This really belongs in some header file... -DaveM */
#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 
					   16 for IP, 16 for IPX,
					   24 for IPv6,
					   about 80 for AX.25 */

/* XXX These as well... */
extern __inline__ struct socket *socki_lookup(struct inode *inode)
{
	return &inode->u.socket_i;
}

extern __inline__ struct socket *sockfd_lookup(int fd, int *err)
{
	struct file *file;
	struct inode *inode;

	if (!(file = fget(fd)))
	{
		*err = -EBADF;
		return NULL;
	}

	inode = file->f_dentry->d_inode;
	if (!inode || !inode->i_sock || !socki_lookup(inode))
	{
		*err = -ENOTSOCK;
		fput(file);
		return NULL;
	}

	return socki_lookup(inode);
}

extern __inline__ void sockfd_put(struct socket *sock)
{
	fput(sock->file);
}

struct msghdr32 {
        u32               msg_name;
        int               msg_namelen;
        u32               msg_iov;
        __kernel_size_t32 msg_iovlen;
        u32               msg_control;
        __kernel_size_t32 msg_controllen;
        unsigned          msg_flags;
};

struct cmsghdr32 {
        __kernel_size_t32 cmsg_len;
        int               cmsg_level;
        int               cmsg_type;
        unsigned char     cmsg_data[0];
};

static inline int iov_from_user32_to_kern(struct iovec *kiov,
					  struct iovec32 *uiov32,
					  int niov)
{
	int tot_len = 0;

	while(niov > 0) {
		u32 len, buf;

		if(get_user(len, &uiov32->iov_len) ||
		   get_user(buf, &uiov32->iov_base)) {
			tot_len = -EFAULT;
			break;
		}
		tot_len += len;
		kiov->iov_base = (void *)A(buf);
		kiov->iov_len = (__kernel_size_t) len;
		uiov32++;
		kiov++;
		niov--;
	}
	return tot_len;
}

static inline int msghdr_from_user32_to_kern(struct msghdr *kmsg,
					     struct msghdr32 *umsg)
{
	u32 tmp1, tmp2, tmp3;
	int err;

	err = get_user(tmp1, &umsg->msg_name);
	err |= __get_user(tmp2, &umsg->msg_iov);
	err |= __get_user(tmp3, &umsg->msg_control);
	if (err)
		return -EFAULT;

	kmsg->msg_name = (void *)A(tmp1);
	kmsg->msg_iov = (struct iovec *)A(tmp2);
	kmsg->msg_control = (void *)A(tmp3);

	err = get_user(kmsg->msg_namelen, &umsg->msg_namelen);
	err |= get_user(kmsg->msg_controllen, &umsg->msg_controllen);
	err |= get_user(kmsg->msg_flags, &umsg->msg_flags);
	
	return err;
}

/* I've named the args so it is easy to tell whose space the pointers are in. */
static int verify_iovec32(struct msghdr *kern_msg, struct iovec *kern_iov,
			  char *kern_address, int mode)
{
	int tot_len;

	if(kern_msg->msg_namelen) {
		if(mode==VERIFY_READ) {
			int err = move_addr_to_kernel(kern_msg->msg_name,
						      kern_msg->msg_namelen,
						      kern_address);
			if(err < 0)
				return err;
		}
		kern_msg->msg_name = kern_address;
	} else
		kern_msg->msg_name = NULL;

	if(kern_msg->msg_iovlen > UIO_FASTIOV) {
		kern_iov = kmalloc(kern_msg->msg_iovlen * sizeof(struct iovec),
				   GFP_KERNEL);
		if(!kern_iov)
			return -ENOMEM;
	}

	tot_len = iov_from_user32_to_kern(kern_iov,
					  (struct iovec32 *)kern_msg->msg_iov,
					  kern_msg->msg_iovlen);
	if(tot_len >= 0)
		kern_msg->msg_iov = kern_iov;
	else if(kern_msg->msg_iovlen > UIO_FASTIOV)
		kfree(kern_iov);

	return tot_len;
}

asmlinkage int sys32_sendmsg(int fd, struct msghdr32 *user_msg, unsigned user_flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iov[UIO_FASTIOV];
	unsigned char ctl[sizeof(struct cmsghdr) + 20];
	unsigned char *ctl_buf = ctl;
	struct msghdr kern_msg;
	int err, total_len;

	if(msghdr_from_user32_to_kern(&kern_msg, user_msg))
		return -EFAULT;
	if(kern_msg.msg_iovlen > UIO_MAXIOV)
		return -EINVAL;
	err = verify_iovec32(&kern_msg, iov, address, VERIFY_READ);
	if (err < 0)
		goto out;
	total_len = err;

	if(kern_msg.msg_controllen) {
		struct cmsghdr32 *ucmsg = (struct cmsghdr32 *)kern_msg.msg_control;
		unsigned long *kcmsg;
		__kernel_size_t32 cmlen;

		if(kern_msg.msg_controllen > sizeof(ctl) &&
		   kern_msg.msg_controllen <= 256) {
			err = -ENOBUFS;
			ctl_buf = kmalloc(kern_msg.msg_controllen, GFP_KERNEL);
			if(!ctl_buf)
				goto out_freeiov;
		}
		__get_user(cmlen, &ucmsg->cmsg_len);
		kcmsg = (unsigned long *) ctl_buf;
		*kcmsg++ = (unsigned long)cmlen;
		err = -EFAULT;
		if(copy_from_user(kcmsg, &ucmsg->cmsg_level,
				  kern_msg.msg_controllen - sizeof(__kernel_size_t32)))
			goto out_freectl;
		kern_msg.msg_control = ctl_buf;
	}
	kern_msg.msg_flags = user_flags;

	lock_kernel();
	sock = sockfd_lookup(fd, &err);
	if (sock != NULL) {
		if (sock->file->f_flags & O_NONBLOCK)
			kern_msg.msg_flags |= MSG_DONTWAIT;
		err = sock_sendmsg(sock, &kern_msg, total_len);
		sockfd_put(sock);
	}
	unlock_kernel();

out_freectl:
	/* N.B. Use kfree here, as kern_msg.msg_controllen might change? */
	if(ctl_buf != ctl)
		kfree(ctl_buf);
out_freeiov:
	if(kern_msg.msg_iov != iov)
		kfree(kern_msg.msg_iov);
out:
	return err;
}

asmlinkage int sys32_recvmsg(int fd, struct msghdr32 *user_msg, unsigned int user_flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct msghdr kern_msg;
	char addr[MAX_SOCK_ADDR];
	struct socket *sock;
	struct iovec *iov = iovstack;
	struct sockaddr *uaddr;
	int *uaddr_len;
	unsigned long cmsg_ptr;
	int err, total_len, len = 0;

	if(msghdr_from_user32_to_kern(&kern_msg, user_msg))
		return -EFAULT;
	if(kern_msg.msg_iovlen > UIO_MAXIOV)
		return -EINVAL;

	uaddr = kern_msg.msg_name;
	uaddr_len = &user_msg->msg_namelen;
	err = verify_iovec32(&kern_msg, iov, addr, VERIFY_WRITE);
	if (err < 0)
		goto out;
	total_len = err;

	cmsg_ptr = (unsigned long) kern_msg.msg_control;
	kern_msg.msg_flags = 0;

	lock_kernel();
	sock = sockfd_lookup(fd, &err);
	if (sock != NULL) {
		if (sock->file->f_flags & O_NONBLOCK)
			user_flags |= MSG_DONTWAIT;
		err = sock_recvmsg(sock, &kern_msg, total_len, user_flags);
		if(err >= 0)
			len = err;
		sockfd_put(sock);
	}
	unlock_kernel();

	if(uaddr != NULL && err >= 0)
		err = move_addr_to_user(addr, kern_msg.msg_namelen, uaddr, uaddr_len);
	if(err >= 0) {
		err = __put_user(kern_msg.msg_flags, &user_msg->msg_flags);
		if(!err) {
			/* XXX Convert cmsg back into userspace 32-bit format... */
			err = __put_user((unsigned long)kern_msg.msg_control - cmsg_ptr,
					 &user_msg->msg_controllen);
		}
	}

	if(kern_msg.msg_iov != iov)
		kfree(kern_msg.msg_iov);
out:
	if(err < 0)
		return err;
	return len;
}

/* Argument list sizes for sys_socketcall */
#define AL(x) ((x) * sizeof(u32))
static unsigned char nargs[18]={AL(0),AL(3),AL(3),AL(3),AL(2),AL(3),
                                AL(3),AL(3),AL(4),AL(4),AL(4),AL(6),
                                AL(6),AL(2),AL(5),AL(5),AL(3),AL(3)};
#undef AL

extern asmlinkage int sys_bind(int fd, struct sockaddr *umyaddr, int addrlen);
extern asmlinkage int sys_connect(int fd, struct sockaddr *uservaddr, int addrlen);
extern asmlinkage int sys_accept(int fd, struct sockaddr *upeer_sockaddr, int *upeer_addrlen);
extern asmlinkage int sys_getsockname(int fd, struct sockaddr *usockaddr, int *usockaddr_len);
extern asmlinkage int sys_getpeername(int fd, struct sockaddr *usockaddr, int *usockaddr_len);
extern asmlinkage int sys_send(int fd, void *buff, size_t len, unsigned flags);
extern asmlinkage int sys32_sendto(int fd, u32 buff, __kernel_size_t32 len,
				   unsigned flags, u32 addr, int addr_len);
extern asmlinkage int sys_recv(int fd, void *ubuf, size_t size, unsigned flags);
extern asmlinkage int sys32_recvfrom(int fd, u32 ubuf, __kernel_size_t32 size,
				     unsigned flags, u32 addr, u32 addr_len);
extern asmlinkage int sys_setsockopt(int fd, int level, int optname,
				     char *optval, int optlen);
extern asmlinkage int sys32_getsockopt(int fd, int level, int optname,
				       u32 optval, u32 optlen);

extern asmlinkage int sys_socket(int family, int type, int protocol);
extern asmlinkage int sys_socketpair(int family, int type, int protocol,
				     int usockvec[2]);
extern asmlinkage int sys_shutdown(int fd, int how);
extern asmlinkage int sys_listen(int fd, int backlog);

asmlinkage int sys32_socketcall(int call, u32 *args)
{
	u32 a[6];
	u32 a0,a1;
				 
	if (call<SYS_SOCKET||call>SYS_RECVMSG)
		return -EINVAL;
	if (copy_from_user(a, args, nargs[call]))
		return -EFAULT;
	a0=a[0];
	a1=a[1];
	
	switch(call) 
	{
		case SYS_SOCKET:
			return sys_socket(a0, a1, a[2]);
		case SYS_BIND:
			return sys_bind(a0, (struct sockaddr *)A(a1), a[2]);
		case SYS_CONNECT:
			return sys_connect(a0, (struct sockaddr *)A(a1), a[2]);
		case SYS_LISTEN:
			return sys_listen(a0, a1);
		case SYS_ACCEPT:
			return sys_accept(a0, (struct sockaddr *)A(a1), (int *)A(a[2]));
		case SYS_GETSOCKNAME:
			return sys_getsockname(a0, (struct sockaddr *)A(a1), (int *)A(a[2]));
		case SYS_GETPEERNAME:
			return sys_getpeername(a0, (struct sockaddr *)A(a1), (int *)A(a[2]));
		case SYS_SOCKETPAIR:
			return sys_socketpair(a0, a1, a[2], (int *)A(a[3]));
		case SYS_SEND:
			return sys_send(a0, (void *)A(a1), a[2], a[3]);
		case SYS_SENDTO:
			return sys32_sendto(a0, a1, a[2], a[3], a[4], a[5]);
		case SYS_RECV:
			return sys_recv(a0, (void *)A(a1), a[2], a[3]);
		case SYS_RECVFROM:
			return sys32_recvfrom(a0, a1, a[2], a[3], a[4], a[5]);
		case SYS_SHUTDOWN:
			return sys_shutdown(a0,a1);
		case SYS_SETSOCKOPT:
			return sys_setsockopt(a0, a1, a[2], (char *)A(a[3]), a[4]);
		case SYS_GETSOCKOPT:
			return sys32_getsockopt(a0, a1, a[2], a[3], a[4]);
		case SYS_SENDMSG:
			return sys32_sendmsg(a0, (struct msghdr32 *)A(a1), a[2]);
		case SYS_RECVMSG:
			return sys32_recvmsg(a0, (struct msghdr32 *)A(a1), a[2]);
	}
	return -EINVAL;
}

extern void check_pending(int signum);

asmlinkage int sys32_sigaction (int sig, struct old_sigaction32 *act, struct old_sigaction32 *oact)
{
        struct k_sigaction new_ka, old_ka;
        int ret;

	if(sig < 0) {
		current->tss.new_signal = 1;
		sig = -sig;
	}

        if (act) {
		old_sigset_t32 mask;
		
		ret = get_user((long)new_ka.sa.sa_handler, &act->sa_handler);
		ret |= __get_user((long)new_ka.sa.sa_restorer, &act->sa_restorer);
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		ret |= __get_user(mask, &act->sa_mask);
		if (ret)
			return ret;
		new_ka.ka_restorer = NULL;
		siginitset(&new_ka.sa.sa_mask, mask);
        }

        ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		ret = put_user((long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __put_user((long)old_ka.sa.sa_restorer, &oact->sa_restorer);
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		ret |= __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
        }

	return ret;
}

asmlinkage int
sys32_rt_sigaction(int sig, struct sigaction32 *act, struct sigaction32 *oact,
		   void *restorer, __kernel_size_t32 sigsetsize)
{
        struct k_sigaction new_ka, old_ka;
        int ret;
	sigset_t32 set32;

        /* XXX: Don't preclude handling different sized sigset_t's.  */
        if (sigsetsize != sizeof(sigset_t32))
                return -EINVAL;

	/* All tasks which use RT signals (effectively) use
	 * new style signals.
	 */
	current->tss.new_signal = 1;

        if (act) {
		new_ka.ka_restorer = restorer;
		ret = get_user((long)new_ka.sa.sa_handler, &act->sa_handler);
		ret |= __copy_from_user(&set32, &act->sa_mask, sizeof(sigset_t32));
		switch (_NSIG_WORDS) {
		case 4: new_ka.sa.sa_mask.sig[3] = set32.sig[6] | (((long)set32.sig[7]) << 32);
		case 3: new_ka.sa.sa_mask.sig[2] = set32.sig[4] | (((long)set32.sig[5]) << 32);
		case 2: new_ka.sa.sa_mask.sig[1] = set32.sig[2] | (((long)set32.sig[3]) << 32);
		case 1: new_ka.sa.sa_mask.sig[0] = set32.sig[0] | (((long)set32.sig[1]) << 32);
		}
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		ret |= __get_user((long)new_ka.sa.sa_restorer, &act->sa_restorer);
                if (ret)
                	return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		switch (_NSIG_WORDS) {
		case 4: set32.sig[7] = (old_ka.sa.sa_mask.sig[3] >> 32); set32.sig[6] = old_ka.sa.sa_mask.sig[3];
		case 3: set32.sig[5] = (old_ka.sa.sa_mask.sig[2] >> 32); set32.sig[4] = old_ka.sa.sa_mask.sig[2];
		case 2: set32.sig[3] = (old_ka.sa.sa_mask.sig[1] >> 32); set32.sig[2] = old_ka.sa.sa_mask.sig[1];
		case 1: set32.sig[1] = (old_ka.sa.sa_mask.sig[0] >> 32); set32.sig[0] = old_ka.sa.sa_mask.sig[0];
		}
		ret = put_user((long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __copy_to_user(&oact->sa_mask, &set32, sizeof(sigset_t32));
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		ret |= __put_user((long)old_ka.sa.sa_restorer, &oact->sa_restorer);
        }

        return ret;
}


/*
 * count32() counts the number of arguments/envelopes
 */
static int count32(u32 * argv)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			u32 p; int error;

			error = get_user(p,argv);
			if (error) return error;
			if (!p) break;
			argv++; i++;
		}
	}
	return i;
}

/*
 * 'copy_string32()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 */
static unsigned long
copy_strings32(int argc,u32 * argv,unsigned long *page,
	       unsigned long p)
{
	u32 str;

	if (!p) return 0;	/* bullet-proofing */
	while (argc-- > 0) {
		int len;
		unsigned long pos;

		get_user(str, argv+argc);
		if (!str) panic("VFS: argc is wrong");
		len = strlen_user((char *)A(str));	/* includes the '\0' */
		if (p < len)	/* this shouldn't happen - 128kB */
			return 0;
		p -= len; pos = p;
		while (len) {
			char *pag;
			int offset, bytes_to_copy;

			offset = pos % PAGE_SIZE;
			if (!(pag = (char *) page[pos/PAGE_SIZE]) &&
			    !(pag = (char *) page[pos/PAGE_SIZE] =
			      (unsigned long *) get_free_page(GFP_USER)))
				return 0;
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len)
				bytes_to_copy = len;
			copy_from_user(pag + offset, (char *)A(str), bytes_to_copy);
			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	return p;
}

/*
 * sys32_execve() executes a new program.
 */
static inline int 
do_execve32(char * filename, u32 * argv, u32 * envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct dentry * dentry;
	int retval;
	int i;

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
		bprm.page[i] = 0;

	dentry = open_namei(filename, 0, 0);
	retval = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		return retval;

	bprm.dentry = dentry;
	bprm.filename = filename;
	bprm.sh_bang = 0;
	bprm.java = 0;
	bprm.loader = 0;
	bprm.exec = 0;
	if ((bprm.argc = count32(argv)) < 0) {
		dput(dentry);
		return bprm.argc;
	}
	if ((bprm.envc = count32(envp)) < 0) {
		dput(dentry);
		return bprm.envc;
	}

	retval = prepare_binprm(&bprm);
	
	if(retval>=0) {
		bprm.p = copy_strings(1, &bprm.filename, bprm.page, bprm.p, 2);
		bprm.exec = bprm.p;
		bprm.p = copy_strings32(bprm.envc,envp,bprm.page,bprm.p);
		bprm.p = copy_strings32(bprm.argc,argv,bprm.page,bprm.p);
		if (!bprm.p)
			retval = -E2BIG;
	}

	if(retval>=0)
		retval = search_binary_handler(&bprm,regs);
	if(retval>=0)
		/* execve success */
		return retval;

	/* Something went wrong, return the inode and free the argument pages*/
	if(bprm.dentry)
		dput(bprm.dentry);

	for (i=0 ; i<MAX_ARG_PAGES ; i++)
		free_page(bprm.page[i]);
	return(retval);
}

/*
 * sparc32_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage int sparc32_execve(struct pt_regs *regs)
{
        int error, base = 0;
        char *filename;

        /* Check for indirect call. */
        if((u32)regs->u_regs[UREG_G1] == 0)
                base = 1;

	lock_kernel();
        filename = getname32((char *)AA(regs->u_regs[base + UREG_I0]));
	error = PTR_ERR(filename);
        if(IS_ERR(filename))
                goto out;
        error = do_execve32(filename,
        	(u32 *)AA((u32)regs->u_regs[base + UREG_I1]),
        	(u32 *)AA((u32)regs->u_regs[base + UREG_I2]), regs);
        putname32(filename);

	if(!error) {
		fprs_write(0);
		current->tss.xfsr[0] = 0;
		current->tss.fpsaved[0] = 0;
		regs->tstate &= ~TSTATE_PEF;
	}
out:
	unlock_kernel();
        return error;
}

#ifdef CONFIG_MODULES

extern asmlinkage unsigned long sys_create_module(const char *name_user, size_t size);

asmlinkage unsigned long sys32_create_module(const char *name_user, __kernel_size_t32 size)
{
	return sys_create_module(name_user, (size_t)size);
}

extern asmlinkage int sys_init_module(const char *name_user, struct module *mod_user);

/* Hey, when you're trying to init module, take time and prepare us a nice 64bit
 * module structure, even if from 32bit modutils... Why to pollute kernel... :))
 */
asmlinkage int sys32_init_module(const char *name_user, struct module *mod_user)
{
	return sys_init_module(name_user, mod_user);
}

extern asmlinkage int sys_delete_module(const char *name_user);

asmlinkage int sys32_delete_module(const char *name_user)
{
	return sys_delete_module(name_user);
}

struct module_info32 {
	u32 addr;
	u32 size;
	u32 flags;
	s32 usecount;
};

/* Query various bits about modules.  */

static inline long
get_mod_name(const char *user_name, char **buf)
{
	unsigned long page;
	long retval;

	if ((unsigned long)user_name >= TASK_SIZE
	    && !segment_eq(get_fs (), KERNEL_DS))
		return -EFAULT;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	retval = strncpy_from_user((char *)page, user_name, PAGE_SIZE);
	if (retval > 0) {
		if (retval < PAGE_SIZE) {
			*buf = (char *)page;
			return retval;
		}
		retval = -ENAMETOOLONG;
	} else if (!retval)
		retval = -EINVAL;

	free_page(page);
	return retval;
}

static inline void
put_mod_name(char *buf)
{
	free_page((unsigned long)buf);
}

static __inline__ struct module *find_module(const char *name)
{
	struct module *mod;

	for (mod = module_list; mod ; mod = mod->next) {
		if (mod->flags & MOD_DELETED)
			continue;
		if (!strcmp(mod->name, name))
			break;
	}

	return mod;
}

static int
qm_modules(char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	struct module *mod;
	size_t nmod, space, len;

	nmod = space = 0;

	for (mod = module_list; mod->next != NULL; mod = mod->next, ++nmod) {
		len = strlen(mod->name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, mod->name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(nmod, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while ((mod = mod->next)->next != NULL)
		space += strlen(mod->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static int
qm_deps(struct module *mod, char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	size_t i, space, len;

	if (mod->next == NULL)
		return -EINVAL;
	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = 0;
	for (i = 0; i < mod->ndeps; ++i) {
		const char *dep_name = mod->deps[i].dep->name;

		len = strlen(dep_name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, dep_name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(i, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while (++i < mod->ndeps)
		space += strlen(mod->deps[i].dep->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static int
qm_refs(struct module *mod, char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	size_t nrefs, space, len;
	struct module_ref *ref;

	if (mod->next == NULL)
		return -EINVAL;
	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = 0;
	for (nrefs = 0, ref = mod->refs; ref ; ++nrefs, ref = ref->next_ref) {
		const char *ref_name = ref->ref->name;

		len = strlen(ref_name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, ref_name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(nrefs, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while ((ref = ref->next_ref) != NULL)
		space += strlen(ref->ref->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static inline int
qm_symbols(struct module *mod, char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	size_t i, space, len;
	struct module_symbol *s;
	char *strings;
	unsigned *vals;

	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = mod->nsyms * 2*sizeof(u32);

	i = len = 0;
	s = mod->syms;

	if (space > bufsize)
		goto calc_space_needed;

	if (!access_ok(VERIFY_WRITE, buf, space))
		return -EFAULT;

	bufsize -= space;
	vals = (unsigned *)buf;
	strings = buf+space;

	for (; i < mod->nsyms ; ++i, ++s, vals += 2) {
		len = strlen(s->name)+1;
		if (len > bufsize)
			goto calc_space_needed;

		if (copy_to_user(strings, s->name, len)
		    || __put_user(s->value, vals+0)
		    || __put_user(space, vals+1))
			return -EFAULT;

		strings += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(i, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	for (; i < mod->nsyms; ++i, ++s)
		space += strlen(s->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static inline int
qm_info(struct module *mod, char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	int error = 0;

	if (mod->next == NULL)
		return -EINVAL;

	if (sizeof(struct module_info32) <= bufsize) {
		struct module_info32 info;
		info.addr = (unsigned long)mod;
		info.size = mod->size;
		info.flags = mod->flags;
		info.usecount = (mod_member_present(mod, can_unload)
				 && mod->can_unload ? -1 : mod->usecount);

		if (copy_to_user(buf, &info, sizeof(struct module_info32)))
			return -EFAULT;
	} else
		error = -ENOSPC;

	if (put_user(sizeof(struct module_info32), ret))
		return -EFAULT;

	return error;
}

asmlinkage int sys32_query_module(char *name_user, int which, char *buf, __kernel_size_t32 bufsize, u32 ret)
{
	struct module *mod;
	int err;

	lock_kernel();
	if (name_user == 0) {
		/* This finds "kernel_module" which is not exported. */
		for(mod = module_list; mod->next != NULL; mod = mod->next)
			;
	} else {
		long namelen;
		char *name;

		if ((namelen = get_mod_name(name_user, &name)) < 0) {
			err = namelen;
			goto out;
		}
		err = -ENOENT;
		if (namelen == 0) {
			/* This finds "kernel_module" which is not exported. */
			for(mod = module_list; mod->next != NULL; mod = mod->next)
				;
		} else if ((mod = find_module(name)) == NULL) {
			put_mod_name(name);
			goto out;
		}
		put_mod_name(name);
	}

	switch (which)
	{
	case 0:
		err = 0;
		break;
	case QM_MODULES:
		err = qm_modules(buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	case QM_DEPS:
		err = qm_deps(mod, buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	case QM_REFS:
		err = qm_refs(mod, buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	case QM_SYMBOLS:
		err = qm_symbols(mod, buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	case QM_INFO:
		err = qm_info(mod, buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	default:
		err = -EINVAL;
		break;
	}
out:
	unlock_kernel();
	return err;
}

struct kernel_sym32 {
	u32 value;
	char name[60];
};
		 
extern asmlinkage int sys_get_kernel_syms(struct kernel_sym *table);

asmlinkage int sys32_get_kernel_syms(struct kernel_sym32 *table)
{
	int len, i;
	struct kernel_sym *tbl;
	mm_segment_t old_fs;
	
	len = sys_get_kernel_syms(NULL);
	if (!table) return len;
	tbl = kmalloc (len * sizeof (struct kernel_sym), GFP_KERNEL);
	if (!tbl) return -ENOMEM;
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	sys_get_kernel_syms(tbl);
	set_fs (old_fs);
	for (i = 0; i < len; i++, table += sizeof (struct kernel_sym32)) {
		if (put_user (tbl[i].value, &table->value) ||
		    copy_to_user (table->name, tbl[i].name, 60))
			break;
	}
	kfree (tbl);
	return i;
}

#else /* CONFIG_MODULES */

asmlinkage unsigned long
sys32_create_module(const char *name_user, size_t size)
{
	return -ENOSYS;
}

asmlinkage int
sys32_init_module(const char *name_user, struct module *mod_user)
{
	return -ENOSYS;
}

asmlinkage int
sys32_delete_module(const char *name_user)
{
	return -ENOSYS;
}

asmlinkage int
sys32_query_module(const char *name_user, int which, char *buf, size_t bufsize,
		 size_t *ret)
{
	/* Let the program know about the new interface.  Not that
	   it'll do them much good.  */
	if (which == 0)
		return 0;

	return -ENOSYS;
}

asmlinkage int
sys32_get_kernel_syms(struct kernel_sym *table)
{
	return -ENOSYS;
}

#endif  /* CONFIG_MODULES */

/* Stuff for NFS server syscalls... */
struct nfsctl_svc32 {
	u16			svc32_port;
	s32			svc32_nthreads;
};

struct nfsctl_client32 {
	s8			cl32_ident[NFSCLNT_IDMAX+1];
	s32			cl32_naddr;
	struct in_addr		cl32_addrlist[NFSCLNT_ADDRMAX];
	s32			cl32_fhkeytype;
	s32			cl32_fhkeylen;
	u8			cl32_fhkey[NFSCLNT_KEYMAX];
};

struct nfsctl_export32 {
	s8			ex32_client[NFSCLNT_IDMAX+1];
	s8			ex32_path[NFS_MAXPATHLEN+1];
	__kernel_dev_t32	ex32_dev;
	__kernel_ino_t32	ex32_ino;
	s32			ex32_flags;
	__kernel_uid_t32	ex32_anon_uid;
	__kernel_gid_t32	ex32_anon_gid;
};

struct nfsctl_uidmap32 {
	u32			ug32_ident;   /* char * */
	__kernel_uid_t32	ug32_uidbase;
	s32			ug32_uidlen;
	u32			ug32_udimap;  /* uid_t * */
	__kernel_uid_t32	ug32_gidbase;
	s32			ug32_gidlen;
	u32			ug32_gdimap;  /* gid_t * */
};

struct nfsctl_fhparm32 {
	struct sockaddr		gf32_addr;
	__kernel_dev_t32	gf32_dev;
	__kernel_ino_t32	gf32_ino;
	s32			gf32_version;
};

struct nfsctl_arg32 {
	s32			ca32_version;	/* safeguard */
	union {
		struct nfsctl_svc32	u32_svc;
		struct nfsctl_client32	u32_client;
		struct nfsctl_export32	u32_export;
		struct nfsctl_uidmap32	u32_umap;
		struct nfsctl_fhparm32	u32_getfh;
		u32			u32_debug;
	} u;
#define ca32_svc	u.u32_svc
#define ca32_client	u.u32_client
#define ca32_export	u.u32_export
#define ca32_umap	u.u32_umap
#define ca32_getfh	u.u32_getfh
#define ca32_authd	u.u32_authd
#define ca32_debug	u.u32_debug
};

union nfsctl_res32 {
	struct knfs_fh		cr32_getfh;
	u32			cr32_debug;
};

static int nfs_svc32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= __get_user(karg->ca_svc.svc_port, &arg32->ca32_svc.svc32_port);
	err |= __get_user(karg->ca_svc.svc_nthreads, &arg32->ca32_svc.svc32_nthreads);
	return err;
}

static int nfs_clnt32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_client.cl_ident[0],
			  &arg32->ca32_client.cl32_ident[0],
			  NFSCLNT_IDMAX);
	err |= __get_user(karg->ca_client.cl_naddr, &arg32->ca32_client.cl32_naddr);
	err |= copy_from_user(&karg->ca_client.cl_addrlist[0],
			  &arg32->ca32_client.cl32_addrlist[0],
			  (sizeof(struct in_addr) * NFSCLNT_ADDRMAX));
	err |= __get_user(karg->ca_client.cl_fhkeytype,
		      &arg32->ca32_client.cl32_fhkeytype);
	err |= __get_user(karg->ca_client.cl_fhkeylen,
		      &arg32->ca32_client.cl32_fhkeylen);
	err |= copy_from_user(&karg->ca_client.cl_fhkey[0],
			  &arg32->ca32_client.cl32_fhkey[0],
			  NFSCLNT_KEYMAX);
	return err;
}

static int nfs_exp32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_export.ex_client[0],
			  &arg32->ca32_export.ex32_client[0],
			  NFSCLNT_IDMAX);
	err |= copy_from_user(&karg->ca_export.ex_path[0],
			  &arg32->ca32_export.ex32_path[0],
			  NFS_MAXPATHLEN);
	err |= __get_user(karg->ca_export.ex_dev,
		      &arg32->ca32_export.ex32_dev);
	err |= __get_user(karg->ca_export.ex_ino,
		      &arg32->ca32_export.ex32_ino);
	err |= __get_user(karg->ca_export.ex_flags,
		      &arg32->ca32_export.ex32_flags);
	err |= __get_user(karg->ca_export.ex_anon_uid,
		      &arg32->ca32_export.ex32_anon_uid);
	err |= __get_user(karg->ca_export.ex_anon_gid,
		      &arg32->ca32_export.ex32_anon_gid);
	return err;
}

static int nfs_uud32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	u32 uaddr;
	int i;
	int err;

	memset(karg, 0, sizeof(*karg));
	if(__get_user(karg->ca_version, &arg32->ca32_version))
		return -EFAULT;
	karg->ca_umap.ug_ident = (char *)get_free_page(GFP_USER);
	if(!karg->ca_umap.ug_ident)
		return -ENOMEM;
	err = __get_user(uaddr, &arg32->ca32_umap.ug32_ident);
	if(strncpy_from_user(karg->ca_umap.ug_ident,
			     (char *)A(uaddr), PAGE_SIZE) <= 0)
		return -EFAULT;
	err |= __get_user(karg->ca_umap.ug_uidbase,
		      &arg32->ca32_umap.ug32_uidbase);
	err |= __get_user(karg->ca_umap.ug_uidlen,
		      &arg32->ca32_umap.ug32_uidlen);
	err |= __get_user(uaddr, &arg32->ca32_umap.ug32_udimap);
	if (err)
		return -EFAULT;
	karg->ca_umap.ug_udimap = kmalloc((sizeof(uid_t) * karg->ca_umap.ug_uidlen),
					  GFP_USER);
	if(!karg->ca_umap.ug_udimap)
		return -ENOMEM;
	for(i = 0; i < karg->ca_umap.ug_uidlen; i++)
		err |= __get_user(karg->ca_umap.ug_udimap[i],
			      &(((__kernel_uid_t32 *)A(uaddr))[i]));
	err |= __get_user(karg->ca_umap.ug_gidbase,
		      &arg32->ca32_umap.ug32_gidbase);
	err |= __get_user(karg->ca_umap.ug_uidlen,
		      &arg32->ca32_umap.ug32_gidlen);
	err |= __get_user(uaddr, &arg32->ca32_umap.ug32_gdimap);
	if (err)
		return -EFAULT;
	karg->ca_umap.ug_gdimap = kmalloc((sizeof(gid_t) * karg->ca_umap.ug_uidlen),
					  GFP_USER);
	if(!karg->ca_umap.ug_gdimap)
		return -ENOMEM;
	for(i = 0; i < karg->ca_umap.ug_gidlen; i++)
		err |= __get_user(karg->ca_umap.ug_gdimap[i],
			      &(((__kernel_gid_t32 *)A(uaddr))[i]));

	return err;
}

static int nfs_getfh32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfh.gf_addr,
			  &arg32->ca32_getfh.gf32_addr,
			  (sizeof(struct sockaddr)));
	err |= __get_user(karg->ca_getfh.gf_dev,
		      &arg32->ca32_getfh.gf32_dev);
	err |= __get_user(karg->ca_getfh.gf_ino,
		      &arg32->ca32_getfh.gf32_ino);
	err |= __get_user(karg->ca_getfh.gf_version,
		      &arg32->ca32_getfh.gf32_version);
	return err;
}

static int nfs_getfh32_res_trans(union nfsctl_res *kres, union nfsctl_res32 *res32)
{
	int err;
	
	err = copy_to_user(&res32->cr32_getfh,
			&kres->cr_getfh,
			sizeof(res32->cr32_getfh));
	err |= __put_user(kres->cr_debug, &res32->cr32_debug);
	return err;
}

extern asmlinkage int sys_nfsservctl(int cmd, void *arg, void *resp);

int asmlinkage sys32_nfsservctl(int cmd, struct nfsctl_arg32 *arg32, union nfsctl_res32 *res32)
{
	struct nfsctl_arg *karg = NULL;
	union nfsctl_res *kres = NULL;
	mm_segment_t oldfs;
	int err;

	karg = kmalloc(sizeof(*karg), GFP_USER);
	if(!karg)
		return -ENOMEM;
	if(res32) {
		kres = kmalloc(sizeof(*kres), GFP_USER);
		if(!kres) {
			kfree(karg);
			return -ENOMEM;
		}
	}
	switch(cmd) {
	case NFSCTL_SVC:
		err = nfs_svc32_trans(karg, arg32);
		break;
	case NFSCTL_ADDCLIENT:
		err = nfs_clnt32_trans(karg, arg32);
		break;
	case NFSCTL_DELCLIENT:
		err = nfs_clnt32_trans(karg, arg32);
		break;
	case NFSCTL_EXPORT:
		err = nfs_exp32_trans(karg, arg32);
		break;
	/* This one is unimplemented, be we're ready for it. */
	case NFSCTL_UGIDUPDATE:
		err = nfs_uud32_trans(karg, arg32);
		break;
	case NFSCTL_GETFH:
		err = nfs_getfh32_trans(karg, arg32);
		break;
	default:
		err = -EINVAL;
		break;
	}
	if(err)
		goto done;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_nfsservctl(cmd, karg, kres);
	set_fs(oldfs);

	if(!err && cmd == NFSCTL_GETFH)
		err = nfs_getfh32_res_trans(kres, res32);

done:
	if(karg) {
		if(cmd == NFSCTL_UGIDUPDATE) {
			if(karg->ca_umap.ug_ident)
				kfree(karg->ca_umap.ug_ident);
			if(karg->ca_umap.ug_udimap)
				kfree(karg->ca_umap.ug_udimap);
			if(karg->ca_umap.ug_gdimap)
				kfree(karg->ca_umap.ug_gdimap);
		}
		kfree(karg);
	}
	if(kres)
		kfree(kres);
	return err;
}

/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */

extern struct timezone sys_tz;
extern int do_sys_settimeofday(struct timeval *tv, struct timezone *tz);

asmlinkage int sys32_gettimeofday(struct timeval32 *tv, struct timezone *tz)
{
	if (tv) {
		struct timeval ktv;
		do_gettimeofday(&ktv);
		if (put_tv32(tv, &ktv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	return 0;
}

asmlinkage int sys32_settimeofday(struct timeval32 *tv, struct timezone *tz)
{
	struct timeval ktv;
	struct timezone ktz;

 	if (tv) {
		if (get_tv32(&ktv, tv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &ktv : NULL, tz ? &ktz : NULL);
}

extern int do_getitimer(int which, struct itimerval *value);

asmlinkage int sys32_getitimer(int which, struct itimerval32 *it)
{
	struct itimerval kit;
	int error;

	error = do_getitimer(which, &kit);
	if (!error && put_it32(it, &kit))
		error = -EFAULT;

	return error;
}

extern int do_setitimer(int which, struct itimerval *, struct itimerval *);

asmlinkage int sys32_setitimer(int which, struct itimerval32 *in, struct itimerval32 *out)
{
	struct itimerval kin, kout;
	int error;

	if (in) {
		if (get_it32(&kin, in))
			return -EFAULT;
	} else
		memset(&kin, 0, sizeof(kin));

	error = do_setitimer(which, &kin, out ? &kout : NULL);
	if (error || !out)
		return error;
	if (put_it32(out, &kout))
		return -EFAULT;

	return 0;

}

asmlinkage int sys_utimes(char *, struct timeval *);

asmlinkage int sys32_utimes(char *filename, struct timeval32 *tvs)
{
	char *kfilename;
	struct timeval ktvs[2];
	mm_segment_t old_fs;
	int ret;

	kfilename = getname32(filename);
	ret = PTR_ERR(kfilename);
	if (!IS_ERR(kfilename)) {
		if (tvs) {
			if (get_tv32(&ktvs[0], tvs) ||
			    get_tv32(&ktvs[1], 1+tvs))
				return -EFAULT;
		}

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_utimes(kfilename, &ktvs[0]);
		set_fs(old_fs);

		putname32(kfilename);
	}
	return ret;
}

/* These are here just in case some old sparc32 binary calls it. */
asmlinkage int sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

/* PCI config space poking. */
extern asmlinkage int sys_pciconfig_read(unsigned long bus,
					 unsigned long dfn,
					 unsigned long off,
					 unsigned long len,
					 unsigned char *buf);

extern asmlinkage int sys_pciconfig_write(unsigned long bus,
					  unsigned long dfn,
					  unsigned long off,
					  unsigned long len,
					  unsigned char *buf);

asmlinkage int sys32_pciconfig_read(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_read((unsigned long) bus,
				  (unsigned long) dfn,
				  (unsigned long) off,
				  (unsigned long) len,
				  (unsigned char *)AA(ubuf));
}

asmlinkage int sys32_pciconfig_write(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_write((unsigned long) bus,
				   (unsigned long) dfn,
				   (unsigned long) off,
				   (unsigned long) len,
				   (unsigned char *)AA(ubuf));
}

extern asmlinkage int sys_prctl(int option, unsigned long arg2, unsigned long arg3,
				unsigned long arg4, unsigned long arg5);

asmlinkage int sys32_prctl(int option, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return sys_prctl(option,
			 (unsigned long) arg2,
			 (unsigned long) arg3,
			 (unsigned long) arg4,
			 (unsigned long) arg5);
}


extern asmlinkage int sys_newuname(struct new_utsname * name);

asmlinkage int sys32_newuname(struct new_utsname * name)
{
	int ret = sys_newuname(name);
	
	if (current->personality == PER_LINUX32 && !ret) {
		ret = copy_to_user(name->machine, "sparc\0\0", 8);
	}
	return ret;
}

extern asmlinkage ssize_t sys_pread(unsigned int fd, char * buf,
				    size_t count, loff_t pos);

extern asmlinkage ssize_t sys_pwrite(unsigned int fd, const char * buf,
				     size_t count, loff_t pos);

typedef __kernel_ssize_t32 ssize_t32;

asmlinkage ssize_t32 sys32_pread(unsigned int fd, char *ubuf,
				 __kernel_size_t32 count, u32 poshi, u32 poslo)
{
	return sys_pread(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage ssize_t32 sys32_pwrite(unsigned int fd, char *ubuf,
				  __kernel_size_t32 count, u32 poshi, u32 poslo)
{
	return sys_pwrite(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}


extern asmlinkage int sys_personality(unsigned long);

asmlinkage int sys32_personality(unsigned long personality)
{
	int ret;
	lock_kernel();
	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	unlock_kernel();
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}

extern asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);

asmlinkage int sys32_sendfile(int out_fd, int in_fd, __kernel_off_t32 *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	
	if (offset && get_user(of, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile(out_fd, in_fd, offset ? &of : NULL, count);
	set_fs(old_fs);
	
	if (!ret && offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

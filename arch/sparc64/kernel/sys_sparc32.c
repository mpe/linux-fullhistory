/* $Id: sys_sparc32.c,v 1.43 1997/07/17 02:20:45 davem Exp $
 * sys_sparc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 */

#include <linux/config.h>
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
#include <linux/nfs_fs.h>
#include <linux/smb_fs.h>
#include <linux/ncp_fs.h>
#include <linux/quota.h>
#include <linux/file.h>
#include <linux/module.h>

#include <asm/types.h>
#include <asm/poll.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>
#include <asm/fpumacro.h>

/* As gcc will warn about casting u32 to some ptr, we have to cast it to
 * unsigned long first, and that's what is A() for.
 * You just do (void *)A(x), instead of having to
 * type (void *)((unsigned long)x) or instead of just (void *)x, which will
 * produce warnings.
 */
#define A(x) ((unsigned long)x)
 
/* In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 */
static inline int do_getname32(u32 filename, char *page)
{
	int retval;

	/* 32bit pointer will be always far below TASK_SIZE :)) */
	retval = strncpy_from_user((char *)page, (char *)A(filename), PAGE_SIZE);
	if (retval > 0) {
		if (retval < PAGE_SIZE)
			return 0;
		return -ENAMETOOLONG;
	} else if (!retval)
		retval = -ENOENT;
	return retval;
}

/* This is a single page for faster getname.
 *   If the page is available when entering getname, use it.
 *   If the page is not available, call __get_free_page instead.
 * This works even though do_getname can block (think about it).
 * -- Michael Chastain, based on idea of Linus Torvalds, 1 Dec 1996.
 * We don't use the common getname/putname from namei.c, so that
 * this still works well, as every routine which calls getname32
 * will then call getname, then putname and then putname32.
 */
static unsigned long name_page_cache32 = 0;

void putname32(char * name)
{
	if (name_page_cache32 == 0)
		name_page_cache32 = (unsigned long) name;
	else
		free_page((unsigned long) name);
}

int getname32(u32 filename, char **result)
{
	unsigned long page;
	int retval;

	page = name_page_cache32;
	name_page_cache32 = 0;
	if (!page) {
		page = __get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
	}

	retval = do_getname32(filename, (char *) page);
	if (retval < 0)
		putname32( (char *) page );
	else
		*result = (char *) page;
	return retval;
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
			union semun fourth;
			void *pad;
			unsigned long old_fs;
			struct semid_ds s;
			
			err = -EINVAL;
			if (!ptr)
				goto out;
			err = -EFAULT;
			if(get_user(pad, (void **)A(ptr)))
				goto out;
			fourth.__pad = pad;
			switch (third) {
				case IPC_INFO:
				case SEM_INFO:
				case GETVAL:
				case GETPID:
				case GETNCNT:
				case GETZCNT:
				case GETALL:
				case SETALL:
				case IPC_RMID:
					err = sys_semctl (first, second, third, fourth);
					goto out;
				case IPC_SET:
					if (get_user (s.sem_perm.uid, &(((struct semid_ds32 *)A(pad))->sem_perm.uid)) ||
					    __get_user (s.sem_perm.gid, &(((struct semid_ds32 *)A(pad))->sem_perm.gid)) ||
					    __get_user (s.sem_perm.mode, &(((struct semid_ds32 *)A(pad))->sem_perm.mode))) {
						err = -EFAULT;
						goto out;
					}
					/* Fall through */
				case SEM_STAT:
				case IPC_STAT:
					fourth.__pad = &s;
					break;
			}
			old_fs = get_fs();
			set_fs (KERNEL_DS);
			err = sys_semctl (first, second, third, fourth);
			set_fs (old_fs);
			switch (third) {
				case SEM_STAT:
				case IPC_STAT:
					if (put_user (s.sem_perm.key, &(((struct semid_ds32 *)A(pad))->sem_perm.key)) ||
					    __put_user (s.sem_perm.uid, &(((struct semid_ds32 *)A(pad))->sem_perm.uid)) ||
					    __put_user (s.sem_perm.gid, &(((struct semid_ds32 *)A(pad))->sem_perm.gid)) ||
					    __put_user (s.sem_perm.cuid, &(((struct semid_ds32 *)A(pad))->sem_perm.cuid)) ||
					    __put_user (s.sem_perm.cgid, &(((struct semid_ds32 *)A(pad))->sem_perm.cgid)) ||
					    __put_user (s.sem_perm.mode, &(((struct semid_ds32 *)A(pad))->sem_perm.mode)) ||
					    __put_user (s.sem_perm.seq, &(((struct semid_ds32 *)A(pad))->sem_perm.seq)) ||
					    __put_user (s.sem_otime, &(((struct semid_ds32 *)A(pad))->sem_otime)) ||
					    __put_user (s.sem_ctime, &(((struct semid_ds32 *)A(pad))->sem_ctime)) ||
					    __put_user (s.sem_nsems, &(((struct semid_ds32 *)A(pad))->sem_nsems)))
						err = -EFAULT;
			}
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
				u32 *uptr = (u32 *) A(((u32)third));
				err = sys_shmat (first, (char *)A(ptr), second, &raddr);
				if (err)
					goto out;
				err = -EFAULT;
				if(put_user (raddr, uptr))
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

static inline int get_flock(struct flock *kfl, struct flock32 *ufl)
{
	if(get_user(kfl->l_type, &ufl->l_type)		||
	   __get_user(kfl->l_whence, &ufl->l_whence)	||
	   __get_user(kfl->l_start, &ufl->l_start)	||
	   __get_user(kfl->l_len, &ufl->l_len)		||
	   __get_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}

static inline int put_flock(struct flock *kfl, struct flock32 *ufl)
{
	if(__put_user(kfl->l_type, &ufl->l_type)	||
	   __put_user(kfl->l_whence, &ufl->l_whence)	||
	   __put_user(kfl->l_start, &ufl->l_start)	||
	   __put_user(kfl->l_len, &ufl->l_len)		||
	   __put_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}

extern asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);

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
			
			if(get_flock(&f, (struct flock32 *)A(arg)))
				return -EFAULT;
			old_fs = get_fs(); set_fs (KERNEL_DS);
			ret = sys_fcntl(fd, cmd, (unsigned long)&f);
			set_fs (old_fs);
			if(put_flock(&f, (struct flock32 *)A(arg)))
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

asmlinkage int sys32_quotactl(int cmd, u32 special, int id, u32 addr)
{
	int cmds = cmd >> SUBCMDSHIFT;
	int err;
	struct dqblk d;
	unsigned long old_fs;
	char *spec;
	
	switch (cmds) {
	case Q_GETQUOTA:
		break;
	case Q_SETQUOTA:
	case Q_SETUSE:
	case Q_SETQLIM:
		if (copy_from_user (&d, (struct dqblk32 *)A(addr),
				    sizeof (struct dqblk32)))
			return -EFAULT;
		d.dqb_itime = ((struct dqblk32 *)&d)->dqb_itime;
		d.dqb_btime = ((struct dqblk32 *)&d)->dqb_btime;
		break;
	default:
		return sys_quotactl(cmd, (const char *)A(special),
				    id, (caddr_t)A(addr));
	}
	err = getname32 (special, &spec);
	if (err) return err;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_quotactl(cmd, (const char *)spec, id, (caddr_t)A(addr));
	set_fs (old_fs);
	putname32 (spec);
	if (cmds == Q_GETQUOTA) {
		__kernel_time_t b = d.dqb_btime, i = d.dqb_itime;
		((struct dqblk32 *)&d)->dqb_itime = i;
		((struct dqblk32 *)&d)->dqb_btime = b;
		if (copy_to_user ((struct dqblk32 *)A(addr), &d,
				  sizeof (struct dqblk32)))
			return -EFAULT;
	}
	return err;
}

static inline int put_statfs (struct statfs32 *ubuf, struct statfs *kbuf)
{
	if (put_user (kbuf->f_type, &ubuf->f_type)			||
	    __put_user (kbuf->f_bsize, &ubuf->f_bsize)			||
	    __put_user (kbuf->f_blocks, &ubuf->f_blocks)		||
	    __put_user (kbuf->f_bfree, &ubuf->f_bfree)			||
	    __put_user (kbuf->f_bavail, &ubuf->f_bavail)		||
	    __put_user (kbuf->f_files, &ubuf->f_files)			||
	    __put_user (kbuf->f_ffree, &ubuf->f_ffree)			||
	    __put_user (kbuf->f_namelen, &ubuf->f_namelen)		||
	    __put_user (kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0])	||
	    __put_user (kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]))
		return -EFAULT;
	return 0;
}

extern asmlinkage int sys_statfs(const char * path, struct statfs * buf);

asmlinkage int sys32_statfs(u32 path, u32 buf)
{
	int ret;
	struct statfs s;
	unsigned long old_fs = get_fs();
	char *pth;
	
	ret = getname32 (path, &pth);
	if (!ret) {
		set_fs (KERNEL_DS);
		ret = sys_statfs((const char *)pth, &s);
		set_fs (old_fs);
		putname32 (pth);
		if (put_statfs((struct statfs32 *)A(buf), &s))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_fstatfs(unsigned int fd, struct statfs * buf);

asmlinkage int sys32_fstatfs(unsigned int fd, u32 buf)
{
	int ret;
	struct statfs s;
	unsigned long old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_fstatfs(fd, &s);
	set_fs (old_fs);
	if (put_statfs((struct statfs32 *)A(buf), &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_utime(char * filename, struct utimbuf * times);

asmlinkage int sys32_utime(u32 filename, u32 times)
{
	struct utimbuf32 { __kernel_time_t32 actime, modtime; };
	struct utimbuf t;
	unsigned long old_fs;
	int ret;
	char *filenam;
	
	if (!times)
		return sys_utime((char *)A(filename), NULL);
	if (get_user (t.actime, &(((struct utimbuf32 *)A(times))->actime)) ||
	    __get_user (t.modtime, &(((struct utimbuf32 *)A(times))->modtime)))
		return -EFAULT;
	ret = getname32 (filename, &filenam);
	if (!ret) {
		old_fs = get_fs();
		set_fs (KERNEL_DS); 
		ret = sys_utime(filenam, &t);
		set_fs (old_fs);
		putname32 (filenam);
	}
	return ret;
}

struct iovec32 { u32 iov_base; __kernel_size_t32 iov_len; };

typedef long (*IO_fn_t)(struct inode *, struct file *, char *, unsigned long);

static long do_readv_writev32(int type, struct inode *inode, struct file *file,
			      const struct iovec32 *vector, u32 count)
{
	unsigned long tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack, *ivp;
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
		nr = fn(inode, file, base, len);
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

asmlinkage long sys32_readv(int fd, u32 vector, u32 count)
{
	struct file *file;
	struct dentry *dentry;
	struct inode *inode;
	long err = -EBADF;

	lock_kernel();
	if(fd >= NR_OPEN)
		goto out;

	file = current->files->fd[fd];
	if(!file)
		goto out;

	if(!(file->f_mode & 1))
		goto out;

	dentry = file->f_dentry;
	if(!dentry)
		goto out;

	inode = dentry->d_inode;
	if(!inode)
		goto out;

	err = do_readv_writev32(VERIFY_WRITE, inode, file,
				(struct iovec32 *)A(vector), count);
out:
	unlock_kernel();
	return err;
}

asmlinkage long sys32_writev(int fd, u32 vector, u32 count)
{
	int error = -EBADF;
	struct file *file;
	struct dentry *dentry;
	struct inode *inode;

	lock_kernel();
	if(fd >= NR_OPEN)
		goto out;

	file = current->files->fd[fd];
	if(!file)
		goto out;

	if(!(file->f_mode & 2))
		goto out;

	dentry = file->f_dentry;
	if(!dentry)
		goto out;

	inode = dentry->d_inode;
	if(!inode)
		goto out;

	down(&inode->i_sem);
	error = do_readv_writev32(VERIFY_READ, inode, file,
				(struct iovec32 *)A(vector), count);
	up(&inode->i_sem);
out:
	unlock_kernel();
	return error;
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

asmlinkage int old32_readdir(unsigned int fd, u32 dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;
	struct readdir_callback32 buf;

	lock_kernel();
	if(fd >= NR_OPEN)
		goto out;

	file = current->files->fd[fd];
	if(!file)
		goto out;

	dentry = file->f_dentry;
	if(!dentry)
		goto out;

	inode = dentry->d_inode;
	if(!inode)
		goto out;

	buf.count = 0;
	buf.dirent = (struct old_linux_dirent32 *)A(dirent);

	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;

	error = file->f_op->readdir(inode, file, &buf, fillonedir);
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
	struct dentry * dentry;
	struct inode *inode;
	struct linux_dirent32 * lastdirent;
	struct getdents_callback32 buf;
	int error = -EBADF;

	lock_kernel();
	if(fd >= NR_OPEN)
		goto out;

	file = current->files->fd[fd];
	if(!file)
		goto out;

	dentry = file->f_dentry;
	if(!dentry)
		goto out;

	inode = dentry->d_inode;
	if(!inode)
		goto out;

	buf.current_dir = (struct linux_dirent32 *) A(dirent);
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;

	error = file->f_op->readdir(inode, file, &buf, filldir);
	if (error < 0)
		goto out;
	lastdirent = buf.previous;
	error = buf.error;
	if(lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}
out:
	unlock_kernel();
	return error;
}

/* end of readdir & getdents */

extern asmlinkage int sys_select(int n, fd_set *inp, fd_set *outp,
				 fd_set *exp, struct timeval *tvp);

asmlinkage int sys32_select(int n, u32 inp, u32 outp, u32 exp, u32 tvp)
{
	struct timeval kern_tv, *ktvp;
	unsigned long old_fs;
	char *p;
	u32 *q, *Inp, *Outp, *Exp;
	int i, ret = -EINVAL, nn;
	
	if (n < 0 || n > PAGE_SIZE*2)
		return -EINVAL;

	lock_kernel ();
	p = (char *)__get_free_page (GFP_KERNEL);
	if (!p)
		goto out;

	q = (u32 *)p;
	Inp = (u32 *)A(inp);
	Outp = (u32 *)A(outp);
	Exp = (u32 *)A(exp);

	ret = -EFAULT;

	nn = (n + (8 * sizeof(unsigned long)) - 1) / (8 * sizeof (unsigned long));
	for (i = 0; i < nn; i++, Inp += 2, Outp += 2, Exp += 2, q += 2) {
		if(inp && (__get_user (q[1], Inp) || __get_user (q[0], Inp+1)))
			goto out;
		if(outp && (__get_user (q[1+(PAGE_SIZE/4/sizeof(u32))], Outp) ||
			    __get_user (q[(PAGE_SIZE/4/sizeof(u32))], Outp+1)))
			goto out;
		if(exp && (__get_user (q[1+(PAGE_SIZE/2/sizeof(u32))], Exp) ||
			   __get_user (q[(PAGE_SIZE/2/sizeof(u32))], Exp+1)))
			goto out;
	}

	ktvp = NULL;
	if(tvp) {
		if(copy_from_user(&kern_tv, (struct timeval *)A(tvp), sizeof(*ktvp)))
			goto out;
		ktvp = &kern_tv;
	}

	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	q = (u32 *) p;
	ret = sys_select(n,
			 inp ? (fd_set *)&q[0] : (fd_set *)0,
			 outp ? (fd_set *)&q[PAGE_SIZE/4/sizeof(u32)] : (fd_set *)0,
			 exp ? (fd_set *)&q[PAGE_SIZE/2/sizeof(u32)] : (fd_set *)0,
			 ktvp);
	set_fs (old_fs);

	if(tvp && !(current->personality & STICKY_TIMEOUTS))
		copy_to_user((struct timeval *)A(tvp), &kern_tv, sizeof(*ktvp));

	q = (u32 *)p;
	Inp = (u32 *)A(inp);
	Outp = (u32 *)A(outp);
	Exp = (u32 *)A(exp);

	if(ret < 0)
		goto out;

	for (i = 0;
	     i < nn;
	     i++, Inp += 2, Outp += 2, Exp += 2, q += 2) {
		if(inp && (__put_user (q[1], Inp) || __put_user (q[0], Inp+1))) {
			ret = -EFAULT;
			goto out;
		}
		if(outp && (__put_user (q[1+(PAGE_SIZE/4/sizeof(u32))], Outp) ||
			    __put_user (q[(PAGE_SIZE/4/sizeof(u32))], Outp+1))) {
			ret = -EFAULT;
			goto out;
		}
		if(exp && (__put_user (q[1+(PAGE_SIZE/2/sizeof(u32))], Exp) ||
			   __put_user (q[(PAGE_SIZE/2/sizeof(u32))], Exp+1))) {
		    	ret = -EFAULT;
			goto out;
		}
	}
out:
	free_page ((unsigned long)p);
	unlock_kernel();
	return ret;
}

static inline int putstat(struct stat32 *ubuf, struct stat *kbuf)
{
	if (put_user (kbuf->st_dev, &ubuf->st_dev)		||
	    __put_user (kbuf->st_ino, &ubuf->st_ino)		||
	    __put_user (kbuf->st_mode, &ubuf->st_mode)		||
	    __put_user (kbuf->st_nlink, &ubuf->st_nlink)	||
	    __put_user (kbuf->st_uid, &ubuf->st_uid)		||
	    __put_user (kbuf->st_gid, &ubuf->st_gid)		||
	    __put_user (kbuf->st_rdev, &ubuf->st_rdev)		||
	    __put_user (kbuf->st_size, &ubuf->st_size)		||
	    __put_user (kbuf->st_atime, &ubuf->st_atime)	||
	    __put_user (kbuf->st_mtime, &ubuf->st_mtime)	||
	    __put_user (kbuf->st_ctime, &ubuf->st_ctime)	||
	    __put_user (kbuf->st_blksize, &ubuf->st_blksize)	||
	    __put_user (kbuf->st_blocks, &ubuf->st_blocks))
		return -EFAULT;
	return 0;
}

extern asmlinkage int sys_newstat(char * filename, struct stat * statbuf);

asmlinkage int sys32_newstat(u32 filename, u32 statbuf)
{
	int ret;
	struct stat s;
	char *filenam;
	unsigned long old_fs = get_fs();
	
	ret = getname32 (filename, &filenam);
	if (!ret) {
		set_fs (KERNEL_DS);
		ret = sys_newstat(filenam, &s);
		set_fs (old_fs);
		putname32 (filenam);
		if (putstat ((struct stat32 *)A(statbuf), &s))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_newlstat(char * filename, struct stat * statbuf);

asmlinkage int sys32_newlstat(u32 filename, u32 statbuf)
{
	int ret;
	struct stat s;
	char *filenam;
	unsigned long old_fs = get_fs();
	
	ret = getname32 (filename, &filenam);
	if (!ret) {
		set_fs (KERNEL_DS);
		ret = sys_newlstat(filenam, &s);
		set_fs (old_fs);
		putname32 (filenam);
		if (putstat ((struct stat32 *)A(statbuf), &s))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_newfstat(unsigned int fd, struct stat * statbuf);

asmlinkage int sys32_newfstat(unsigned int fd, u32 statbuf)
{
	int ret;
	struct stat s;
	unsigned long old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newfstat(fd, &s);
	set_fs (old_fs);
	if (putstat ((struct stat32 *)A(statbuf), &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_sysfs(int option, ...);

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
        unsigned int fd;
        __kernel_uid_t32 mounted_uid;
        struct sockaddr_in addr;
        char server_name[17];
        char client_name[17];
        char service[64];
        char root_path[64];
        char username[64];
        char password[64];
        char domain[64];
        unsigned short max_xmit;
        __kernel_uid_t32 uid;
        __kernel_gid_t32 gid;
        __kernel_mode_t32 file_mode;
        __kernel_mode_t32 dir_mode;
};

static void *do_smb_super_data_conv(void *raw_data)
{
	struct smb_mount_data *s = (struct smb_mount_data *)raw_data;
	struct smb_mount_data32 *s32 = (struct smb_mount_data32 *)raw_data;

	s->dir_mode = s32->dir_mode;
	s->file_mode = s32->file_mode;
	s->gid = s32->gid;
	s->uid = s32->uid;
	memmove (&s->addr, &s32->addr, (((long)&s->uid) - ((long)&s->addr)));
	s->mounted_uid = s32->mounted_uid;
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

asmlinkage int sys32_mount(u32 dev_name, u32 dir_name, u32 type, u32 new_flags, u32 data)
{
	unsigned long type_page;
	int err, is_smb, is_ncp;

	if(!suser())
		return -EPERM;
	is_smb = is_ncp = 0;
	err = copy_mount_stuff_to_kernel((const void *)A(type), &type_page);
	if(err)
		return err;
	if(type_page) {
		is_smb = !strcmp((char *)type_page, SMBFS_NAME);
		is_ncp = !strcmp((char *)type_page, NCPFS_NAME);
	}
	if(!is_smb && !is_ncp) {
		if(type_page)
			free_page(type_page);
		return sys_mount((char *)A(dev_name), (char *)A(dir_name),
				 (char *)A(type), (unsigned long)new_flags,
				 (void *)A(data));
	} else {
		unsigned long dev_page, dir_page, data_page;
		int old_fs;

		err = copy_mount_stuff_to_kernel((const void *)A(dev_name), &dev_page);
		if(err)
			goto out;
		err = copy_mount_stuff_to_kernel((const void *)A(dir_name), &dir_page);
		if(err)
			goto dev_out;
		err = copy_mount_stuff_to_kernel((const void *)A(data), &data_page);
		if(err)
			goto dir_out;
		if(is_ncp)
			do_ncp_super_data_conv((void *)data_page);
		else if(is_smb)
			do_smb_super_data_conv((void *)data_page);
		else
			panic("Tell DaveM he fucked up...");
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_mount((char *)dev_page, (char *)dir_page,
				(char *)type_page, (unsigned long)new_flags,
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
        struct timeval ru_utime;
        struct timeval ru_stime;
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

static int put_rusage (u32 ru, struct rusage *r)
{
	if (put_user (r->ru_utime.tv_sec, &(((struct rusage32 *)A(ru))->ru_utime.tv_sec)) ||
	    __put_user (r->ru_utime.tv_usec, &(((struct rusage32 *)A(ru))->ru_utime.tv_usec)) ||
	    __put_user (r->ru_stime.tv_sec, &(((struct rusage32 *)A(ru))->ru_stime.tv_sec)) ||
	    __put_user (r->ru_stime.tv_usec, &(((struct rusage32 *)A(ru))->ru_stime.tv_usec)) ||
	    __put_user (r->ru_maxrss, &(((struct rusage32 *)A(ru))->ru_maxrss)) ||
	    __put_user (r->ru_ixrss, &(((struct rusage32 *)A(ru))->ru_ixrss)) ||
	    __put_user (r->ru_idrss, &(((struct rusage32 *)A(ru))->ru_idrss)) ||
	    __put_user (r->ru_isrss, &(((struct rusage32 *)A(ru))->ru_isrss)) ||
	    __put_user (r->ru_minflt, &(((struct rusage32 *)A(ru))->ru_minflt)) ||
	    __put_user (r->ru_majflt, &(((struct rusage32 *)A(ru))->ru_majflt)) ||
	    __put_user (r->ru_nswap, &(((struct rusage32 *)A(ru))->ru_nswap)) ||
	    __put_user (r->ru_inblock, &(((struct rusage32 *)A(ru))->ru_inblock)) ||
	    __put_user (r->ru_oublock, &(((struct rusage32 *)A(ru))->ru_oublock)) ||
	    __put_user (r->ru_msgsnd, &(((struct rusage32 *)A(ru))->ru_msgsnd)) ||
	    __put_user (r->ru_msgrcv, &(((struct rusage32 *)A(ru))->ru_msgrcv)) ||
	    __put_user (r->ru_nsignals, &(((struct rusage32 *)A(ru))->ru_nsignals)) ||
	    __put_user (r->ru_nvcsw, &(((struct rusage32 *)A(ru))->ru_nvcsw)) ||
	    __put_user (r->ru_nivcsw, &(((struct rusage32 *)A(ru))->ru_nivcsw)))
		return -EFAULT;
	return 0;
}

extern asmlinkage int sys_wait4(pid_t pid,unsigned int * stat_addr,
				int options, struct rusage * ru);

asmlinkage int sys32_wait4(__kernel_pid_t32 pid, u32 stat_addr, int options, u32 ru)
{
	if (!ru)
		return sys_wait4(pid, (unsigned int *)A(stat_addr), options, NULL);
	else {
		struct rusage r;
		int ret;
		unsigned int status;
		unsigned long old_fs = get_fs();
		
		set_fs (KERNEL_DS);
		ret = sys_wait4(pid, stat_addr ? &status : NULL, options, &r);
		set_fs (old_fs);
		if (put_rusage (ru, &r)) return -EFAULT;
		if (stat_addr && put_user (status, (unsigned int *)A(stat_addr)))
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

asmlinkage int sys32_sysinfo(u32 info)
{
	struct sysinfo s;
	int ret;
	unsigned long old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);
	if (put_user (s.uptime, &(((struct sysinfo32 *)A(info))->uptime)) ||
	    __put_user (s.loads[0], &(((struct sysinfo32 *)A(info))->loads[0])) ||
	    __put_user (s.loads[1], &(((struct sysinfo32 *)A(info))->loads[1])) ||
	    __put_user (s.loads[2], &(((struct sysinfo32 *)A(info))->loads[2])) ||
	    __put_user (s.totalram, &(((struct sysinfo32 *)A(info))->totalram)) ||
	    __put_user (s.freeram, &(((struct sysinfo32 *)A(info))->freeram)) ||
	    __put_user (s.sharedram, &(((struct sysinfo32 *)A(info))->sharedram)) ||
	    __put_user (s.bufferram, &(((struct sysinfo32 *)A(info))->bufferram)) ||
	    __put_user (s.totalswap, &(((struct sysinfo32 *)A(info))->totalswap)) ||
	    __put_user (s.freeswap, &(((struct sysinfo32 *)A(info))->freeswap)) ||
	    __put_user (s.procs, &(((struct sysinfo32 *)A(info))->procs)))
		return -EFAULT;
	return ret;
}

struct timespec32 {
	s32    tv_sec;
	s32    tv_nsec;
};
                
extern asmlinkage int sys_sched_rr_get_interval(pid_t pid, struct timespec *interval);

asmlinkage int sys32_sched_rr_get_interval(__kernel_pid_t32 pid, u32 interval)
{
	struct timespec t;
	int ret;
	unsigned long old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, &t);
	set_fs (old_fs);
	if (put_user (t.tv_sec, &(((struct timespec32 *)A(interval))->tv_sec)) ||
	    __put_user (t.tv_nsec, &(((struct timespec32 *)A(interval))->tv_nsec)))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_nanosleep(struct timespec *rqtp, struct timespec *rmtp);

asmlinkage int sys32_nanosleep(u32 rqtp, u32 rmtp)
{
	struct timespec t;
	int ret;
	unsigned long old_fs = get_fs ();
	
	if (get_user (t.tv_sec, &(((struct timespec32 *)A(rqtp))->tv_sec)) ||
	    __get_user (t.tv_nsec, &(((struct timespec32 *)A(rqtp))->tv_nsec)))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_nanosleep(&t, rmtp ? &t : NULL);
	set_fs (old_fs);
	if (rmtp && ret == -EINTR) {
		if (__put_user (t.tv_sec, &(((struct timespec32 *)A(rmtp))->tv_sec)) ||
	    	    __put_user (t.tv_nsec, &(((struct timespec32 *)A(rmtp))->tv_nsec)))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_sigprocmask(int how, sigset_t *set, sigset_t *oset);

asmlinkage int sys32_sigprocmask(int how, u32 set, u32 oset)
{
	sigset_t s;
	int ret;
	unsigned long old_fs = get_fs();
	
	if (set && get_user (s, (sigset_t32 *)A(set))) return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL);
	set_fs (old_fs);
	if (oset && put_user (s, (sigset_t32 *)A(oset))) return -EFAULT;
	return ret;
}

extern asmlinkage int sys_sigpending(sigset_t *set);

asmlinkage int sys32_sigpending(u32 set)
{
	sigset_t s;
	int ret;
	unsigned long old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_sigpending(&s);
	set_fs (old_fs);
	if (put_user (s, (sigset_t32 *)A(set))) return -EFAULT;
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

asmlinkage int sys32_getresuid(u32 ruid, u32 euid, u32 suid)
{
	uid_t a, b, c;
	int ret;
	unsigned long old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_getresuid(&a, &b, &c);
	set_fs (old_fs);
	if (put_user (a, (__kernel_uid_t32 *)A(ruid)) ||
	    put_user (b, (__kernel_uid_t32 *)A(euid)) ||
	    put_user (c, (__kernel_uid_t32 *)A(suid)))
		return -EFAULT;
	return ret;
}

struct tms32 {
	__kernel_clock_t32 tms_utime;
	__kernel_clock_t32 tms_stime;
	__kernel_clock_t32 tms_cutime;
	__kernel_clock_t32 tms_cstime;
};
                                
extern asmlinkage long sys_times(struct tms * tbuf);

asmlinkage long sys32_times(u32 tbuf)
{
	struct tms t;
	long ret;
	unsigned long old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_times(tbuf ? &t : NULL);
	set_fs (old_fs);
	if (tbuf && (
	    put_user (t.tms_utime, &(((struct tms32 *)A(tbuf))->tms_utime)) ||
	    __put_user (t.tms_stime, &(((struct tms32 *)A(tbuf))->tms_stime)) ||
	    __put_user (t.tms_cutime, &(((struct tms32 *)A(tbuf))->tms_cutime)) ||
	    __put_user (t.tms_cstime, &(((struct tms32 *)A(tbuf))->tms_cstime))))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_getgroups(int gidsetsize, gid_t *grouplist);

asmlinkage int sys32_getgroups(int gidsetsize, u32 grouplist)
{
	gid_t gl[NGROUPS];
	int ret, i;
	unsigned long old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_getgroups(gidsetsize, gl);
	set_fs (old_fs);
	if (gidsetsize && ret > 0 && ret <= NGROUPS)
		for (i = 0; i < ret; i++, grouplist += sizeof(__kernel_gid_t32))
			if (__put_user (gl[i], (__kernel_gid_t32 *)A(grouplist)))
				return -EFAULT;
	return ret;
}

extern asmlinkage int sys_setgroups(int gidsetsize, gid_t *grouplist);

asmlinkage int sys32_setgroups(int gidsetsize, u32 grouplist)
{
	gid_t gl[NGROUPS];
	int ret, i;
	unsigned long old_fs = get_fs ();
	
	if ((unsigned) gidsetsize > NGROUPS)
		return -EINVAL;
	for (i = 0; i < gidsetsize; i++, grouplist += sizeof(__kernel_gid_t32))
		if (__get_user (gl[i], (__kernel_gid_t32 *)A(grouplist)))
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

asmlinkage int sys32_getrlimit(unsigned int resource, u32 rlim)
{
	struct rlimit r;
	int ret;
	unsigned long old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_getrlimit(resource, &r);
	set_fs (old_fs);
	if (!ret && (
	    put_user (RESOURCE32(r.rlim_cur), &(((struct rlimit32 *)A(rlim))->rlim_cur)) ||
	    __put_user (RESOURCE32(r.rlim_max), &(((struct rlimit32 *)A(rlim))->rlim_max))))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_setrlimit(unsigned int resource, struct rlimit *rlim);

asmlinkage int sys32_setrlimit(unsigned int resource, u32 rlim)
{
	struct rlimit r;
	int ret;
	unsigned long old_fs = get_fs ();

	if (resource >= RLIM_NLIMITS) return -EINVAL;	
	if (get_user (r.rlim_cur, &(((struct rlimit32 *)A(rlim))->rlim_cur)) ||
	    __get_user (r.rlim_max, &(((struct rlimit32 *)A(rlim))->rlim_max)))
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

asmlinkage int sys32_getrusage(int who, u32 ru)
{
	struct rusage r;
	int ret;
	unsigned long old_fs = get_fs();
		
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
	struct timeval time;
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

extern asmlinkage int sys_adjtimex(struct timex *txc_p);

asmlinkage int sys32_adjtimex(u32 txc_p)
{
	struct timex t;
	int ret;
	unsigned long old_fs = get_fs ();

	if (get_user (t.modes, &(((struct timex32 *)A(txc_p))->modes)) ||
	    __get_user (t.offset, &(((struct timex32 *)A(txc_p))->offset)) ||
	    __get_user (t.freq, &(((struct timex32 *)A(txc_p))->freq)) ||
	    __get_user (t.maxerror, &(((struct timex32 *)A(txc_p))->maxerror)) ||
	    __get_user (t.esterror, &(((struct timex32 *)A(txc_p))->esterror)) ||
	    __get_user (t.status, &(((struct timex32 *)A(txc_p))->status)) ||
	    __get_user (t.constant, &(((struct timex32 *)A(txc_p))->constant)) ||
	    __get_user (t.tick, &(((struct timex32 *)A(txc_p))->tick)) ||
	    __get_user (t.shift, &(((struct timex32 *)A(txc_p))->shift)))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_adjtimex(&t);
	set_fs (old_fs);
	if ((unsigned)ret >= 0 && (
	    __put_user (t.modes, &(((struct timex32 *)A(txc_p))->modes)) ||
	    __put_user (t.offset, &(((struct timex32 *)A(txc_p))->offset)) ||
	    __put_user (t.freq, &(((struct timex32 *)A(txc_p))->freq)) ||
	    __put_user (t.maxerror, &(((struct timex32 *)A(txc_p))->maxerror)) ||
	    __put_user (t.esterror, &(((struct timex32 *)A(txc_p))->esterror)) ||
	    __put_user (t.status, &(((struct timex32 *)A(txc_p))->status)) ||
	    __put_user (t.constant, &(((struct timex32 *)A(txc_p))->constant)) ||
	    __put_user (t.precision, &(((struct timex32 *)A(txc_p))->precision)) ||
	    __put_user (t.tolerance, &(((struct timex32 *)A(txc_p))->tolerance)) ||
	    __put_user (t.time.tv_sec, &(((struct timex32 *)A(txc_p))->time.tv_sec)) ||
	    __put_user (t.time.tv_usec, &(((struct timex32 *)A(txc_p))->time.tv_usec)) ||
	    __put_user (t.tick, &(((struct timex32 *)A(txc_p))->tick)) ||
	    __put_user (t.ppsfreq, &(((struct timex32 *)A(txc_p))->ppsfreq)) ||
	    __put_user (t.jitter, &(((struct timex32 *)A(txc_p))->jitter)) ||
	    __put_user (t.shift, &(((struct timex32 *)A(txc_p))->shift)) ||
	    __put_user (t.stabil, &(((struct timex32 *)A(txc_p))->stabil)) ||
	    __put_user (t.jitcnt, &(((struct timex32 *)A(txc_p))->jitcnt)) ||
	    __put_user (t.calcnt, &(((struct timex32 *)A(txc_p))->calcnt)) ||
	    __put_user (t.errcnt, &(((struct timex32 *)A(txc_p))->errcnt)) ||
	    __put_user (t.stbcnt, &(((struct timex32 *)A(txc_p))->stbcnt))))
		return -EFAULT;
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

	if(get_user(tmp1, &umsg->msg_name)	||
	   get_user(tmp2, &umsg->msg_iov)	||
	   get_user(tmp3, &umsg->msg_control))
		return -EFAULT;

	kmsg->msg_name = (void *)A(tmp1);
	kmsg->msg_iov = (struct iovec *)A(tmp2);
	kmsg->msg_control = (void *)A(tmp3);

	if(get_user(kmsg->msg_namelen, &umsg->msg_namelen)		||
	   get_user(kmsg->msg_controllen, &umsg->msg_controllen)	||
	   get_user(kmsg->msg_flags, &umsg->msg_flags))
		return -EFAULT;

	return 0;
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

asmlinkage int sys32_sendmsg(int fd, u32 user_msg, unsigned user_flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iov[UIO_FASTIOV];
	unsigned char ctl[sizeof(struct cmsghdr) + 20];
	struct msghdr kern_msg;
	int err;
	int total_len;
	unsigned char *ctl_buf = ctl;

	if(msghdr_from_user32_to_kern(&kern_msg, (struct msghdr32 *)A(user_msg)))
		return -EFAULT;
	if(kern_msg.msg_iovlen > UIO_MAXIOV)
		return -EINVAL;
	total_len = verify_iovec32(&kern_msg, iov, address, VERIFY_READ);
	if(total_len < 0)
		return total_len;
	if(kern_msg.msg_controllen) {
		struct cmsghdr32 *ucmsg = (struct cmsghdr32 *)kern_msg.msg_control;
		unsigned long *kcmsg;
		__kernel_size_t32 cmlen;

		if(kern_msg.msg_controllen > sizeof(ctl) &&
		   kern_msg.msg_controllen <= 256) {
			ctl_buf = kmalloc(kern_msg.msg_controllen, GFP_KERNEL);
			if(!ctl_buf) {
				if(kern_msg.msg_iov != iov)
					kfree(kern_msg.msg_iov);
				return -ENOBUFS;
			}
		}
		__get_user(cmlen, &ucmsg->cmsg_len);
		kcmsg = (unsigned long *) ctl_buf;
		*kcmsg++ = (unsigned long)cmlen;
		if(copy_from_user(kcmsg, &ucmsg->cmsg_level,
				  kern_msg.msg_controllen - sizeof(__kernel_size_t32))) {
			if(ctl_buf != ctl)
				kfree_s(ctl_buf, kern_msg.msg_controllen);
			if(kern_msg.msg_iov != iov)
				kfree(kern_msg.msg_iov);
			return -EFAULT;
		}
		kern_msg.msg_control = ctl_buf;
	}
	kern_msg.msg_flags = user_flags;

	lock_kernel();
	if(current->files->fd[fd]->f_flags & O_NONBLOCK)
		kern_msg.msg_flags |= MSG_DONTWAIT;
	if((sock = sockfd_lookup(fd, &err)) != NULL) {
		err = sock_sendmsg(sock, &kern_msg, total_len);
		sockfd_put(sock);
	}
	unlock_kernel();

	if(ctl_buf != ctl)
		kfree_s(ctl_buf, kern_msg.msg_controllen);
	if(kern_msg.msg_iov != iov)
		kfree(kern_msg.msg_iov);
	return err;
}

asmlinkage int sys32_recvmsg(int fd, u32 user_msg, unsigned int user_flags)
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

	if(msghdr_from_user32_to_kern(&kern_msg, (struct msghdr32 *)A(user_msg)))
		return -EFAULT;
	if(kern_msg.msg_iovlen > UIO_MAXIOV)
		return -EINVAL;

	uaddr = kern_msg.msg_name;
	uaddr_len = &((struct msghdr32 *)A(user_msg))->msg_namelen;
	err = verify_iovec32(&kern_msg, iov, addr, VERIFY_WRITE);
	if(err < 0)
		return err;
	total_len = err;

	cmsg_ptr = (unsigned long) kern_msg.msg_control;
	kern_msg.msg_flags = 0;

	lock_kernel();
	if(current->files->fd[fd]->f_flags & O_NONBLOCK)
		user_flags |= MSG_DONTWAIT;
	if((sock = sockfd_lookup(fd, &err)) != NULL) {
		err = sock_recvmsg(sock, &kern_msg, total_len, user_flags);
		if(err >= 0)
			len = err;
		sockfd_put(sock);
	}
	unlock_kernel();

	if(kern_msg.msg_iov != iov)
		kfree(kern_msg.msg_iov);
	if(uaddr != NULL && err >= 0)
		err = move_addr_to_user(addr, kern_msg.msg_namelen, uaddr, uaddr_len);
	if(err >= 0) {
		err = __put_user(kern_msg.msg_flags,
				 &((struct msghdr32 *)A(user_msg))->msg_flags);
		if(!err) {
			/* XXX Convert cmsg back into userspace 32-bit format... */
			err = __put_user((unsigned long)kern_msg.msg_control - cmsg_ptr,
					 &((struct msghdr32 *)A(user_msg))->msg_controllen);
		}
	}
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

extern asmlinkage int sys32_bind(int fd, u32 umyaddr, int addrlen);
extern asmlinkage int sys32_connect(int fd, u32 uservaddr, int addrlen);
extern asmlinkage int sys32_accept(int fd, u32 upeer_sockaddr, u32 upeer_addrlen);
extern asmlinkage int sys32_getsockname(int fd, u32 usockaddr, u32 usockaddr_len);
extern asmlinkage int sys32_getpeername(int fd, u32 usockaddr, u32 usockaddr_len);
extern asmlinkage int sys32_send(int fd, u32 buff, __kernel_size_t32 len,
				 unsigned flags);
extern asmlinkage int sys32_sendto(int fd, u32 buff, __kernel_size_t32 len,
				   unsigned flags, u32 addr, int addr_len);
extern asmlinkage int sys32_recv(int fd, u32 ubuf, __kernel_size_t32 size,
				 unsigned flags);
extern asmlinkage int sys32_recvfrom(int fd, u32 ubuf, __kernel_size_t32 size,
				     unsigned flags, u32 addr, u32 addr_len);
extern asmlinkage int sys32_setsockopt(int fd, int level, int optname,
				       u32 optval, int optlen);
extern asmlinkage int sys32_getsockopt(int fd, int level, int optname,
				       u32 optval, u32 optlen);

extern asmlinkage int sys_socket(int family, int type, int protocol);
extern asmlinkage int sys_socketpair(int family, int type, int protocol,
				     int usockvec[2]);
extern asmlinkage int sys_shutdown(int fd, int how);
extern asmlinkage int sys_listen(int fd, int backlog);

asmlinkage int sys32_socketcall(int call, u32 args)
{
	u32 a[6];
	u32 a0,a1;
				 
	if (call<SYS_SOCKET||call>SYS_RECVMSG)
		return -EINVAL;
	if (copy_from_user(a, (u32 *)A(args), nargs[call]))
		return -EFAULT;
	a0=a[0];
	a1=a[1];
	
	switch(call) 
	{
		case SYS_SOCKET:
			return sys_socket(a0, a1, a[2]);
		case SYS_BIND:
			return sys32_bind(a0, a1, a[2]);
		case SYS_CONNECT:
			return sys32_connect(a0, a1, a[2]);
		case SYS_LISTEN:
			return sys_listen(a0, a1);
		case SYS_ACCEPT:
			return sys32_accept(a0, a1, a[2]);
		case SYS_GETSOCKNAME:
			return sys32_getsockname(a0, a1, a[2]);
		case SYS_GETPEERNAME:
			return sys32_getpeername(a0, a1, a[2]);
		case SYS_SOCKETPAIR:
			return sys_socketpair(a0, a1, a[2], (int *)A(a[3]));
		case SYS_SEND:
			return sys32_send(a0, a1, a[2], a[3]);
		case SYS_SENDTO:
			return sys32_sendto(a0, a1, a[2], a[3], a[4], a[5]);
		case SYS_RECV:
			return sys32_recv(a0, a1, a[2], a[3]);
		case SYS_RECVFROM:
			return sys32_recvfrom(a0, a1, a[2], a[3], a[4], a[5]);
		case SYS_SHUTDOWN:
			return sys_shutdown(a0,a1);
		case SYS_SETSOCKOPT:
			return sys32_setsockopt(a0, a1, a[2], a[3], a[4]);
		case SYS_GETSOCKOPT:
			return sys32_getsockopt(a0, a1, a[2], a[3], a[4]);
		case SYS_SENDMSG:
			return sys32_sendmsg(a0, a1, a[2]);
		case SYS_RECVMSG:
			return sys32_recvmsg(a0, a1, a[2]);
	}
	return -EINVAL;
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
		old_sa.sa_mask = (sigset_t32)(p->sa_mask);
		old_sa.sa_flags = (unsigned)(p->sa_flags);
		old_sa.sa_restorer = (unsigned)(u64)(p->sa_restorer);
		if (copy_to_user(A(oldaction), &old_sa, sizeof(struct sigaction32)))
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
        filename = getname((char *)(unsigned long)(u32)regs->u_regs[base + UREG_I0]);
	error = PTR_ERR(filename);
        if(IS_ERR(filename))
                goto out;
        error = do_execve32(filename,
        	(u32 *)A((u32)regs->u_regs[base + UREG_I1]),
        	(u32 *)A((u32)regs->u_regs[base + UREG_I2]), regs);
        putname(filename);

	if(!error) {
		fprs_write(0);
		regs->fprs = 0;
	}
out:
	unlock_kernel();
        return error;
}

#ifdef CONFIG_MODULES

extern asmlinkage unsigned long sys_create_module(const char *name_user, size_t size);

asmlinkage unsigned long sys32_create_module(u32 name_user, __kernel_size_t32 size)
{
	return sys_create_module((const char *)A(name_user), (size_t)size);
}

extern asmlinkage int sys_init_module(const char *name_user, struct module *mod_user);

/* Hey, when you're trying to init module, take time and prepare us a nice 64bit
 * module structure, even if from 32bit modutils... Why to pollute kernel... :))
 */
asmlinkage int sys32_init_module(u32 nameuser, u32 mod_user)
{
	return sys_init_module((const char *)A(nameuser), (struct module *)A(mod_user));
}

extern asmlinkage int sys_delete_module(const char *name_user);

asmlinkage int sys32_delete_module(u32 name_user)
{
	return sys_delete_module((const char *)A(name_user));
}

struct module_info32 {
	u32 addr;
	u32 size;
	u32 flags;
	s32 usecount;
};

extern asmlinkage int sys_query_module(const char *name_user, int which, char *buf, size_t bufsize, size_t *ret);

asmlinkage int sys32_query_module(u32 name_user, int which, u32 buf, __kernel_size_t32 bufsize, u32 retv)
{
	char *buff;
	unsigned long old_fs = get_fs();
	size_t val;
	int ret, i, j;
	unsigned long *p;
	char *usernam = NULL;
	int bufsiz = bufsize;
	struct module_info mi;
	
	switch (which) {
	case 0:	return sys_query_module ((const char *)A(name_user), which, (char *)A(buf), (size_t)bufsize, (size_t *)A(retv));
	case QM_SYMBOLS:
		bufsiz <<= 1;
	case QM_MODULES:
	case QM_REFS:
	case QM_DEPS:
		if (name_user && (ret = getname32 (name_user, &usernam)))
			return ret;
		buff = kmalloc (bufsiz, GFP_KERNEL);
		if (!buff) {
			if (name_user) putname32 (usernam);
			return -ENOMEM;
		}
qmsym_toshort:
		set_fs (KERNEL_DS);
		ret = sys_query_module (usernam, which, buff, bufsiz, &val);
		set_fs (old_fs);
		if (which != QM_SYMBOLS) {
			if (ret == -ENOSPC || !ret) {
				if (put_user (val, (__kernel_size_t32 *)A(retv)))
					ret = -EFAULT;
			}
			if (!ret) {
				if (copy_to_user ((char *)A(buf), buff, bufsize))
					ret = -EFAULT;
			}
		} else {
			if (ret == -ENOSPC) {
				if (put_user (2 * val, (__kernel_size_t32 *)A(retv)))
					ret = -EFAULT;
			}
			p = (unsigned long *)buff;
			if (!ret) {
				if (put_user (val, (__kernel_size_t32 *)A(retv)))
					ret = -EFAULT;
			}
			if (!ret) {
				j = val * 8;
				for (i = 0; i < val; i++, p += 2) {
					if (bufsize < (2 * sizeof (u32))) {
						bufsiz = 0;
						goto qmsym_toshort;
					}
					if (put_user (p[0], (u32 *)A(buf)) ||
				    	    __put_user (p[1] - j, (((u32 *)A(buf))+1))) {
						ret = -EFAULT;
						break;
					}
					bufsize -= (2 * sizeof (u32));
					buf += (2 * sizeof (u32));
				}
			}
			if (!ret && val) {
				char *strings = buff + ((unsigned long *)buff)[1];
				j = *(p - 1) - ((unsigned long *)buff)[1];
				j = j + strlen (buff + j) + 1;
				if (bufsize < j) {
					bufsiz = 0;
					goto qmsym_toshort;
				}
				if (copy_to_user ((char *)A(buf), strings, j))
					ret = -EFAULT;
			}
		}
		kfree (buff);
		if (name_user) putname32 (usernam);
		return ret;
	case QM_INFO:
		if (name_user && (ret = getname32 (name_user, &usernam)))
			return ret;
		set_fs (KERNEL_DS);
		ret = sys_query_module (usernam, which, (char *)&mi, sizeof (mi), &val);
		set_fs (old_fs);
		if (!ret) {
			if (put_user (sizeof (struct module_info32), (__kernel_size_t32 *)A(retv)))
				ret = -EFAULT;
			else if (bufsize < sizeof (struct module_info32))
				ret = -ENOSPC;
		}
		if (!ret) {
			if (put_user (mi.addr, &(((struct module_info32 *)A(buf))->addr)) ||
			    __put_user (mi.size, &(((struct module_info32 *)A(buf))->size)) ||
			    __put_user (mi.flags, &(((struct module_info32 *)A(buf))->flags)) ||
			    __put_user (mi.usecount, &(((struct module_info32 *)A(buf))->usecount)))
				ret = -EFAULT;
		}
		if (name_user) putname32 (usernam);
		return ret;
	default:
		return -EINVAL;
	}
}

struct kernel_sym32 {
	u32 value;
	char name[60];
};
		 
extern asmlinkage int sys_get_kernel_syms(struct kernel_sym *table);

asmlinkage int sys32_get_kernel_syms(u32 table)
{
	int len, i;
	struct kernel_sym *tbl;
	unsigned long old_fs;
	
	len = sys_get_kernel_syms(NULL);
	if (!table) return len;
	tbl = kmalloc (len * sizeof (struct kernel_sym), GFP_KERNEL);
	if (!tbl) return -ENOMEM;
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	sys_get_kernel_syms(tbl);
	set_fs (old_fs);
	for (i = 0; i < len; i++, table += sizeof (struct kernel_sym32)) {
		if (put_user (tbl[i].value, &(((struct kernel_sym32 *)A(table))->value)) ||
		    copy_to_user (((struct kernel_sym32 *)A(table))->name, tbl[i].name, 60))
			break;
	}
	kfree (tbl);
	return i;
}

#else /* CONFIG_MODULES */

asmlinkage unsigned long
sys_create_module(const char *name_user, size_t size)
{
	return -ENOSYS;
}

asmlinkage int
sys_init_module(const char *name_user, struct module *mod_user)
{
	return -ENOSYS;
}

asmlinkage int
sys_delete_module(const char *name_user)
{
	return -ENOSYS;
}

asmlinkage int
sys_query_module(const char *name_user, int which, char *buf, size_t bufsize,
		 size_t *ret)
{
	/* Let the program know about the new interface.  Not that
	   it'll do them much good.  */
	if (which == 0)
		return 0;

	return -ENOSYS;
}

asmlinkage int
sys_get_kernel_syms(struct kernel_sym *table)
{
	return -ENOSYS;
}

#endif  /* CONFIG_MODULES */

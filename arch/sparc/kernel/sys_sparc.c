/* $Id: sys_sparc.c,v 1.10 1996/04/20 08:33:55 davem Exp $
 * linux/arch/sparc/kernel/sys_sparc.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/sparc
 * platform.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>

#include <asm/segment.h>

/* XXX Make this per-binary type, this way we can detect the type of
 * XXX a binary.  Every Sparc executable calls this very early on.
 */
asmlinkage unsigned long sys_getpagesize(void)
{
	return PAGE_SIZE; /* Possibly older binaries want 8192 on sun4's? */
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sparc_pipe(struct pt_regs *regs)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (error) {
		return error;
	} else {
		regs->u_regs[UREG_I1] = fd[1];
		return fd[0];
	}
}

/* Note most sanity checking already done in sclow.S code. */
asmlinkage int quick_sys_write(unsigned int fd, char *buf, unsigned int count)
{
	struct file *file = current->files->fd[fd];
	struct inode *inode = file->f_inode;
	int error;

	error = verify_area(VERIFY_READ, buf, count);
	if(error)
		return error;
	/*
	 * If data has been written to the file, remove the setuid and
	 * the setgid bits. We do it anyway otherwise there is an
	 * extremely exploitable race - does your OS get it right |->
	 *
	 * Set ATTR_FORCE so it will always be changed.
	 */
	if (!suser() && (inode->i_mode & (S_ISUID | S_ISGID))) {
		struct iattr newattrs;
		newattrs.ia_mode = inode->i_mode & ~(S_ISUID | S_ISGID);
		newattrs.ia_valid = ATTR_CTIME | ATTR_MODE | ATTR_FORCE;
		notify_change(inode, &newattrs);
	}

	down(&inode->i_sem);
	error = file->f_op->write(inode,file,buf,count);
	up(&inode->i_sem);
	return error;
}

/* XXX do we need this crap? XXX */

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			return sys_semop (first, (struct sembuf *)ptr, second);
		case SEMGET:
			return sys_semget (first, second, third);
		case SEMCTL: {
			union semun fourth;
			int err;
			if (!ptr)
				return -EINVAL;
			if ((err = verify_area (VERIFY_READ, ptr, sizeof(long))))
				return err;
			fourth.__pad = (void *) get_fs_long(ptr);
			return sys_semctl (first, second, third, fourth);
			}
		default:
			return -EINVAL;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			return sys_msgsnd (first, (struct msgbuf *) ptr, 
					   second, third);
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				int err;
				if (!ptr)
					return -EINVAL;
				if ((err = verify_area (VERIFY_READ, ptr, sizeof(tmp))))
					return err;
				memcpy_fromfs (&tmp,(struct ipc_kludge *) ptr,
					       sizeof (tmp));
				return sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp, third);
				}
			case 1: default:
				return sys_msgrcv (first, (struct msgbuf *) ptr, second, fifth, third);
			}
		case MSGGET:
			return sys_msgget ((key_t) first, second);
		case MSGCTL:
			return sys_msgctl (first, second, (struct msqid_ds *) ptr);
		default:
			return -EINVAL;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			case 0: default: {
				ulong raddr;
				int err;
				if ((err = verify_area(VERIFY_WRITE, (ulong*) third, sizeof(ulong))))
					return err;
				err = sys_shmat (first, (char *) ptr, second, &raddr);
				if (err)
					return err;
				put_fs_long (raddr, (ulong *) third);
				return 0;
				}
			case 1:	/* iBCS2 emulator entry point */
				if (get_fs() != get_ds())
					return -EINVAL;
				return sys_shmat (first, (char *) ptr, second, (ulong *) third);
			}
		case SHMDT: 
			return sys_shmdt ((char *)ptr);
		case SHMGET:
			return sys_shmget (first, second, third);
		case SHMCTL:
			return sys_shmctl (first, second, (struct shmid_ds *) ptr);
		default:
			return -EINVAL;
		}
	return -EINVAL;
}

unsigned long get_sparc_unmapped_area(unsigned long len)
{
	unsigned long addr = 0xE8000000UL;
	struct vm_area_struct * vmm;

	if (len > TASK_SIZE)
		return 0;
	for (vmm = find_vma(current, addr); ; vmm = vmm->vm_next) {
		/* At this point:  (!vmm || addr < vmm->vm_end). */
		if (TASK_SIZE - len < addr)
			return 0;
		if (!vmm || addr + len <= vmm->vm_start)
			return addr;
		addr = vmm->vm_end;
	}
}

/* Linux version of mmap */
asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long off)
{
	struct file * file = NULL;
	long retval;

	if (!(flags & MAP_ANONYMOUS)) {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd])){
			return -EBADF;
	    }
	}
	if(!(flags & MAP_FIXED) && !addr) {
		addr = get_sparc_unmapped_area(len);
		if(!addr){
			return -ENOMEM;
		}
	}
	retval = do_mmap(file, addr, len, prot, flags, off);
	return retval;
}

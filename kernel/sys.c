/*
 *  linux/kernel/sys.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/param.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/acct.h>
#include <linux/tty.h>
#include <linux/nametrans.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/io.h>

/*
 * this indicates whether you can reboot with ctrl-alt-del: the default is yes
 */

int C_A_D = 1;


/*
 *	Notifier list for kernel code which wants to be called
 *	at shutdown. This is used to stop any idling DMA operations
 *	and the like. 
 */

struct notifier_block *reboot_notifier_list = NULL;

int register_reboot_notifier(struct notifier_block * nb)
{
	return notifier_chain_register(&reboot_notifier_list, nb);
}

int unregister_reboot_notifier(struct notifier_block * nb)
{
	return notifier_chain_unregister(&reboot_notifier_list, nb);
}



extern void adjust_clock(void);

asmlinkage int sys_ni_syscall(void)
{
	return -ENOSYS;
}

static int proc_sel(struct task_struct *p, int which, int who)
{
	if(p->pid)
	{
		switch (which) {
			case PRIO_PROCESS:
				if (!who && p == current)
					return 1;
				return(p->pid == who);
			case PRIO_PGRP:
				if (!who)
					who = current->pgrp;
				return(p->pgrp == who);
			case PRIO_USER:
				if (!who)
					who = current->uid;
				return(p->uid == who);
		}
	}
	return 0;
}

asmlinkage int sys_setpriority(int which, int who, int niceval)
{
	struct task_struct *p;
	unsigned int priority;
	int error;

	if (which > 2 || which < 0)
		return -EINVAL;

	/* normalize: avoid signed division (rounding problems) */
	error = ESRCH;
	priority = niceval;
	if (niceval < 0)
		priority = -niceval;
	if (priority > 20)
		priority = 20;
	priority = (priority * DEF_PRIORITY + 10) / 20 + DEF_PRIORITY;

	if (niceval >= 0) {
		priority = 2*DEF_PRIORITY - priority;
		if (!priority)
			priority = 1;
	}

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!proc_sel(p, which, who))
			continue;
		if (p->uid != current->euid &&
			p->uid != current->uid && !suser()) {
			error = EPERM;
			continue;
		}
		if (error == ESRCH)
			error = 0;
		if (priority > p->priority && !suser())
			error = EACCES;
		else
			p->priority = priority;
	}
	read_unlock(&tasklist_lock);

	return -error;
}

/*
 * Ugh. To avoid negative return values, "getpriority()" will
 * not return the normal nice-value, but a value that has been
 * offset by 20 (ie it returns 0..40 instead of -20..20)
 */
asmlinkage int sys_getpriority(int which, int who)
{
	struct task_struct *p;
	long max_prio = -ESRCH;

	if (which > 2 || which < 0)
		return -EINVAL;

	read_lock(&tasklist_lock);
	for_each_task (p) {
		if (!proc_sel(p, which, who))
			continue;
		if (p->priority > max_prio)
			max_prio = p->priority;
	}
	read_unlock(&tasklist_lock);

	/* scale the priority from timeslice to 0..40 */
	if (max_prio > 0)
		max_prio = (max_prio * 20 + DEF_PRIORITY/2) / DEF_PRIORITY;
	return max_prio;
}

#ifndef __alpha__

/*
 * Why do these exist?  Binary compatibility with some other standard?
 * If so, maybe they should be moved into the appropriate arch
 * directory.
 */

asmlinkage int sys_profil(void)
{
	return -ENOSYS;
}

asmlinkage int sys_ftime(void)
{
	return -ENOSYS;
}

asmlinkage int sys_break(void)
{
	return -ENOSYS;
}

asmlinkage int sys_stty(void)
{
	return -ENOSYS;
}

asmlinkage int sys_gtty(void)
{
	return -ENOSYS;
}

asmlinkage int sys_prof(void)
{
	return -ENOSYS;
}

#endif

extern asmlinkage int sys_kill(int, int);

/*
 * Reboot system call: for obvious reasons only root may call it,
 * and even root needs to set up some magic numbers in the registers
 * so that some mistake won't make this reboot the whole machine.
 * You can also set the meaning of the ctrl-alt-del-key here.
 *
 * reboot doesn't sync: do that yourself before calling this.
 *
 */
asmlinkage int sys_reboot(int magic1, int magic2, int cmd, void * arg)
{
	char buffer[256];

	/* We only trust the superuser with rebooting the system. */
	if (!suser())
		return -EPERM;

	/* For safety, we require "magic" arguments. */
	if (magic1 != LINUX_REBOOT_MAGIC1 ||
	    (magic2 != LINUX_REBOOT_MAGIC2 && magic2 != LINUX_REBOOT_MAGIC2A))
		return -EINVAL;

	lock_kernel();
	switch (cmd) {
	case LINUX_REBOOT_CMD_RESTART:
		notifier_call_chain(&reboot_notifier_list, SYS_RESTART, NULL);
		printk(KERN_EMERG "Restarting system.\n");
		machine_restart(NULL);
		break;

	case LINUX_REBOOT_CMD_CAD_ON:
		C_A_D = 1;
		break;

	case LINUX_REBOOT_CMD_CAD_OFF:
		C_A_D = 0;
		break;

	case LINUX_REBOOT_CMD_HALT:
		notifier_call_chain(&reboot_notifier_list, SYS_HALT, NULL);
		printk(KERN_EMERG "System halted.\n");
		machine_halt();
		do_exit(0);
		break;

	case LINUX_REBOOT_CMD_POWER_OFF:
		notifier_call_chain(&reboot_notifier_list, SYS_POWER_OFF, NULL);
		printk(KERN_EMERG "Power down.\n");
		machine_power_off();
		do_exit(0);
		break;

	case LINUX_REBOOT_CMD_RESTART2:
		if (strncpy_from_user(&buffer[0], (char *)arg, sizeof(buffer) - 1) < 0) {
			unlock_kernel();
			return -EFAULT;
		}
		buffer[sizeof(buffer) - 1] = '\0';

		notifier_call_chain(&reboot_notifier_list, SYS_RESTART, buffer);
		printk(KERN_EMERG "Restarting system with command '%s'.\n", buffer);
		machine_restart(buffer);
		break;

	default:
		unlock_kernel();
		return -EINVAL;
		break;
	};
	unlock_kernel();
	return 0;
}

/*
 * This function gets called by ctrl-alt-del - ie the keyboard interrupt.
 * As it's called within an interrupt, it may NOT sync: the only choice
 * is whether to reboot at once, or just ignore the ctrl-alt-del.
 */
void ctrl_alt_del(void)
{
	if (C_A_D) {
		notifier_call_chain(&reboot_notifier_list, SYS_RESTART, NULL);
		machine_restart(NULL);
	} else
		kill_proc(1, SIGINT, 1);
}
	

/*
 * Unprivileged users may change the real gid to the effective gid
 * or vice versa.  (BSD-style)
 *
 * If you set the real gid at all, or set the effective gid to a value not
 * equal to the real gid, then the saved gid is set to the new effective gid.
 *
 * This makes it possible for a setgid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setregid() will be
 * 100% compatible with BSD.  A program which uses just setgid() will be
 * 100% compatible with POSIX w/ Saved ID's. 
 *
 * SMP: There are not races, the gid's are checked only by filesystem
 *      operations (as far as semantic preservation is concerned).
 */
asmlinkage int sys_setregid(gid_t rgid, gid_t egid)
{
	int old_rgid = current->gid;
	int old_egid = current->egid;

	if (rgid != (gid_t) -1) {
		if ((old_rgid == rgid) ||
		    (current->egid==rgid) ||
		    suser())
			current->gid = rgid;
		else
			return -EPERM;
	}
	if (egid != (gid_t) -1) {
		if ((old_rgid == egid) ||
		    (current->egid == egid) ||
		    (current->sgid == egid) ||
		    suser())
			current->fsgid = current->egid = egid;
		else {
			current->gid = old_rgid;
			return -EPERM;
		}
	}
	if (rgid != (gid_t) -1 ||
	    (egid != (gid_t) -1 && egid != old_rgid))
		current->sgid = current->egid;
	current->fsgid = current->egid;
	if (current->egid != old_egid)
		current->dumpable = 0;
	return 0;
}

/*
 * setgid() is implemented like SysV w/ SAVED_IDS 
 *
 * SMP: Same implicit races as above.
 */
asmlinkage int sys_setgid(gid_t gid)
{
	int old_egid = current->egid;

	if (suser())
		current->gid = current->egid = current->sgid = current->fsgid = gid;
	else if ((gid == current->gid) || (gid == current->sgid))
		current->egid = current->fsgid = gid;
	else
		return -EPERM;

	if (current->egid != old_egid)
		current->dumpable = 0;
	return 0;
}
  
static char acct_active = 0;
static struct file acct_file;

int acct_process(long exitcode)
{
   struct acct ac;
   unsigned short fs;

   if (acct_active) {
      strncpy(ac.ac_comm, current->comm, ACCT_COMM);
      ac.ac_comm[ACCT_COMM-1] = '\0';
      ac.ac_utime = current->times.tms_utime;
      ac.ac_stime = current->times.tms_stime;
      ac.ac_btime = CT_TO_SECS(current->start_time) + (xtime.tv_sec - (jiffies / HZ));
      ac.ac_etime = CURRENT_TIME - ac.ac_btime;
      ac.ac_uid   = current->uid;
      ac.ac_gid   = current->gid;
      ac.ac_tty   = (current)->tty == NULL ? -1 :
	  kdev_t_to_nr(current->tty->device);
      ac.ac_flag  = 0;
      if (current->flags & PF_FORKNOEXEC)
         ac.ac_flag |= AFORK;
      if (current->flags & PF_SUPERPRIV)
         ac.ac_flag |= ASU;
      if (current->flags & PF_DUMPCORE)
         ac.ac_flag |= ACORE;
      if (current->flags & PF_SIGNALED)
         ac.ac_flag |= AXSIG;
      ac.ac_minflt = current->min_flt;
      ac.ac_majflt = current->maj_flt;
      ac.ac_exitcode = exitcode;

      /* Kernel segment override */
      fs = get_fs();
      set_fs(KERNEL_DS);

      acct_file.f_op->write(acct_file.f_inode, &acct_file,
                             (char *)&ac, sizeof(struct acct));
      /* inode->i_status |= ST_MODIFIED is willingly *not* done here */

      set_fs(fs);
   }
   return 0;
}

asmlinkage int sys_acct(const char *name)
{
	struct inode *inode = (struct inode *)0;
	char *tmp;
	int error = -EPERM;

	lock_kernel();
	if (!suser())
		goto out;

	if (name == (char *)0) {
		if (acct_active) {
			if (acct_file.f_op->release)
				acct_file.f_op->release(acct_file.f_inode, &acct_file);

			if (acct_file.f_inode != (struct inode *) 0)
				iput(acct_file.f_inode);

			acct_active = 0;
		}
		error = 0;
	} else {
		error = -EBUSY;
		if (!acct_active) {
			if ((error = getname(name, &tmp)) != 0)
				goto out;

			error = open_namei(tmp, O_RDWR, 0600, &inode, 0);
			putname(tmp);
			if (error)
				goto out;

			error = -EACCES;
			if (!S_ISREG(inode->i_mode)) {
				iput(inode);
				goto out;
			}

			error = -EIO;
			if (!inode->i_op || !inode->i_op->default_file_ops || 
			    !inode->i_op->default_file_ops->write) {
				iput(inode);
				goto out;
			}

			acct_file.f_mode = 3;
			acct_file.f_flags = 0;
			acct_file.f_count = 1;
			acct_file.f_inode = inode;
			acct_file.f_pos = inode->i_size;
			acct_file.f_reada = 0;
			acct_file.f_op = inode->i_op->default_file_ops;

			if(acct_file.f_op->open)
				if(acct_file.f_op->open(acct_file.f_inode, &acct_file)) {
					iput(inode);
					goto out;
				}

			acct_active = 1;
			error = 0;
		}
	}
out:
	unlock_kernel();
	return error;
}

#ifndef __alpha__

/*
 * Why do these exist?  Binary compatibility with some other standard?
 * If so, maybe they should be moved into the appropriate arch
 * directory.
 */

asmlinkage int sys_phys(void)
{
	return -ENOSYS;
}

asmlinkage int sys_lock(void)
{
	return -ENOSYS;
}

asmlinkage int sys_mpx(void)
{
	return -ENOSYS;
}

asmlinkage int sys_ulimit(void)
{
	return -ENOSYS;
}

asmlinkage int sys_old_syscall(void)
{
	return -ENOSYS;
}

#endif

/*
 * Unprivileged users may change the real uid to the effective uid
 * or vice versa.  (BSD-style)
 *
 * If you set the real uid at all, or set the effective uid to a value not
 * equal to the real uid, then the saved uid is set to the new effective uid.
 *
 * This makes it possible for a setuid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setreuid() will be
 * 100% compatible with BSD.  A program which uses just setuid() will be
 * 100% compatible with POSIX w/ Saved ID's. 
 */
asmlinkage int sys_setreuid(uid_t ruid, uid_t euid)
{
	int old_ruid, old_euid, new_ruid;

	new_ruid = old_ruid = current->uid;
	old_euid = current->euid;
	if (ruid != (uid_t) -1) {
		if ((old_ruid == ruid) || 
		    (current->euid==ruid) ||
		    suser())
			new_ruid = ruid;
		else
			return -EPERM;
	}
	if (euid != (uid_t) -1) {
		if ((old_ruid == euid) ||
		    (current->euid == euid) ||
		    (current->suid == euid) ||
		    suser())
			current->fsuid = current->euid = euid;
		else
			return -EPERM;
	}
	if (ruid != (uid_t) -1 ||
	    (euid != (uid_t) -1 && euid != old_ruid))
		current->suid = current->euid;
	current->fsuid = current->euid;
	if (current->euid != old_euid)
		current->dumpable = 0;

	if(new_ruid != old_ruid) {
		/* What if a process setreuid()'s and this brings the
		 * new uid over his NPROC rlimit?  We can check this now
		 * cheaply with the new uid cache, so if it matters
		 * we should be checking for it.  -DaveM
		 */
		charge_uid(current, -1);
		current->uid = new_ruid;
		if(new_ruid)
			charge_uid(current, 1);
	}
	return 0;
}

/*
 * setuid() is implemented like SysV w/ SAVED_IDS 
 * 
 * Note that SAVED_ID's is deficient in that a setuid root program
 * like sendmail, for example, cannot set its uid to be a normal 
 * user and then switch back, because if you're root, setuid() sets
 * the saved uid too.  If you don't like this, blame the bright people
 * in the POSIX committee and/or USG.  Note that the BSD-style setreuid()
 * will allow a root program to temporarily drop privileges and be able to
 * regain them by swapping the real and effective uid.  
 */
asmlinkage int sys_setuid(uid_t uid)
{
	int old_euid = current->euid;
	int old_ruid, new_ruid;

	old_ruid = new_ruid = current->uid;
	if (suser())
		new_ruid = current->euid = current->suid = current->fsuid = uid;
	else if ((uid == current->uid) || (uid == current->suid))
		current->fsuid = current->euid = uid;
	else
		return -EPERM;

	if (current->euid != old_euid)
		current->dumpable = 0;

	if(new_ruid != old_ruid) {
		/* See comment above about NPROC rlimit issues... */
		charge_uid(current, -1);
		current->uid = new_ruid;
		if(new_ruid)
			charge_uid(current, 1);
	}
	return 0;
}


/*
 * This function implementes a generic ability to update ruid, euid,
 * and suid.  This allows you to implement the 4.4 compatible seteuid().
 */
asmlinkage int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	uid_t old_ruid, old_euid, old_suid;

	old_ruid = current->uid;
	old_euid = current->euid;
	old_suid = current->suid;

	if ((ruid != (uid_t) -1) && (ruid != current->uid) &&
	    (ruid != current->euid) && (ruid != current->suid))
		return -EPERM;
	if ((euid != (uid_t) -1) && (euid != current->uid) &&
	    (euid != current->euid) && (euid != current->suid))
		return -EPERM;
	if ((suid != (uid_t) -1) && (suid != current->uid) &&
	    (suid != current->euid) && (suid != current->suid))
		return -EPERM;
	if (ruid != (uid_t) -1) {
		/* See above commentary about NPROC rlimit issues here. */
		charge_uid(current, -1);
		current->uid = ruid;
		if(ruid)
			charge_uid(current, 1);
	}
	if (euid != (uid_t) -1)
		current->euid = euid;
	if (suid != (uid_t) -1)
		current->suid = suid;
	return 0;
}

asmlinkage int sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	int retval;

	if (!(retval = put_user(current->uid, ruid)) &&
	    !(retval = put_user(current->euid, euid)))
		retval = put_user(current->suid, suid);

	return retval;
}


/*
 * "setfsuid()" sets the fsuid - the uid used for filesystem checks. This
 * is used for "access()" and for the NFS daemon (letting nfsd stay at
 * whatever uid it wants to). It normally shadows "euid", except when
 * explicitly set by setfsuid() or for access..
 */
asmlinkage int sys_setfsuid(uid_t uid)
{
	int old_fsuid;

	old_fsuid = current->fsuid;
	if (uid == current->uid || uid == current->euid ||
	    uid == current->suid || uid == current->fsuid || suser())
		current->fsuid = uid;
	if (current->fsuid != old_fsuid)
		current->dumpable = 0;

	return old_fsuid;
}

/*
 * Samma på svenska..
 */
asmlinkage int sys_setfsgid(gid_t gid)
{
	int old_fsgid;

	old_fsgid = current->fsgid;
	if (gid == current->gid || gid == current->egid ||
	    gid == current->sgid || gid == current->fsgid || suser())
		current->fsgid = gid;
	if (current->fsgid != old_fsgid)
		current->dumpable = 0;

	return old_fsgid;
}

asmlinkage long sys_times(struct tms * tbuf)
{
	/*
	 *	In the SMP world we might just be unlucky and have one of
	 *	the times increment as we use it. Since the value is an
	 *	atomically safe type this is just fine. Conceptually its
	 *	as if the syscall took an instant longer to occur.
	 */
	if (tbuf)
		if (copy_to_user(tbuf, &current->times, sizeof(struct tms)))
			return -EFAULT;
	return jiffies;
}

/*
 * This needs some heavy checking ...
 * I just haven't the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 *
 * OK, I think I have the protection semantics right.... this is really
 * only important on a multi-user system anyway, to make sure one user
 * can't send a signal to a process owned by another.  -TYT, 12/12/91
 *
 * Auch. Had to add the 'did_exec' flag to conform completely to POSIX.
 * LBT 04.03.94
 */

asmlinkage int sys_setpgid(pid_t pid, pid_t pgid)
{
	struct task_struct * p;
	int err = -EINVAL;

	if (!pid)
		pid = current->pid;
	if (!pgid)
		pgid = pid;
	if (pgid < 0)
		return -EINVAL;

	if((p = find_task_by_pid(pid)) == NULL)
		return -ESRCH;

	/* From this point forward we keep holding onto the tasklist lock
	 * so that our parent does not change from under us. -DaveM
	 */
	read_lock(&tasklist_lock);
	err = -ESRCH;
	if (p->p_pptr == current || p->p_opptr == current) {
		err = -EPERM;
		if (p->session != current->session)
			goto out;
		err = -EACCES;
		if (p->did_exec)
			goto out;
	} else if (p != current)
		goto out;
	err = -EPERM;
	if (p->leader)
		goto out;
	if (pgid != pid) {
		struct task_struct * tmp;
		for_each_task (tmp) {
			if (tmp->pgrp == pgid &&
			    tmp->session == current->session)
				goto ok_pgid;
		}
		goto out;
	}

ok_pgid:
	p->pgrp = pgid;
	err = 0;
out:
	/* All paths lead to here, thus we are safe. -DaveM */
	read_unlock(&tasklist_lock);
	return err;
}

asmlinkage int sys_getpgid(pid_t pid)
{
	if (!pid) {
		return current->pgrp;
	} else {
		struct task_struct *p = find_task_by_pid(pid);

		if(p)
			return p->pgrp;
		else
			return -ESRCH;
	}
}

asmlinkage int sys_getpgrp(void)
{
	/* SMP - assuming writes are word atomic this is fine */
	return current->pgrp;
}

asmlinkage int sys_getsid(pid_t pid)
{
	if (!pid) {
		return current->session;
	} else {
		struct task_struct *p = find_task_by_pid(pid);

		if(p)
			return p->session;
		else
			return -ESRCH;
	}
}

asmlinkage int sys_setsid(void)
{
	struct task_struct * p;
	int err = -EPERM;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (p->pgrp == current->pid)
		        goto out;
	}

	current->leader = 1;
	current->session = current->pgrp = current->pid;
	current->tty = NULL;
	current->tty_old_pgrp = 0;
	err = current->pgrp;
out:
	read_unlock(&tasklist_lock);
	return err;
}

/*
 * Supplementary group ID's
 */
asmlinkage int sys_getgroups(int gidsetsize, gid_t *grouplist)
{
	int i;
	
	/*
	 *	SMP: Nobody else can change our grouplist. Thus we are
	 *	safe.
	 */

	if (gidsetsize < 0)
		return -EINVAL;
	i = current->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize)
		        return -EINVAL;
		if (copy_to_user(grouplist, current->groups, sizeof(gid_t)*i))
			return -EFAULT;
	}
	return i;
}

/*
 *	SMP: Our groups are not shared. We can copy to/from them safely
 *	without another task interfering.
 */
 
asmlinkage int sys_setgroups(int gidsetsize, gid_t *grouplist)
{
	if (!suser())
		return -EPERM;
	if ((unsigned) gidsetsize > NGROUPS)
		return -EINVAL;
	if(copy_from_user(current->groups, grouplist, gidsetsize * sizeof(gid_t)))
		return -EFAULT;
	current->ngroups = gidsetsize;
	return 0;
}

int in_group_p(gid_t grp)
{
	if (grp != current->fsgid) {
		int i = current->ngroups;
		if (i) {
			gid_t *groups = current->groups;
			do {
				if (*groups == grp)
					goto out;
				groups++;
				i--;
			} while (i);
		}
		return 0;
	}
out:
	return 1;
}

asmlinkage int sys_newuname(struct new_utsname * name)
{
	if (!name)
		return -EFAULT;
	if (copy_to_user(name,&system_utsname,sizeof *name))
		return -EFAULT;
	return 0;
}

#ifndef __alpha__

/*
 * Move these to arch dependent dir since they are for
 * backward compatibility only?
 */

#ifndef __sparc__
asmlinkage int sys_uname(struct old_utsname * name)
{
	if (name && !copy_to_user(name, &system_utsname, sizeof (*name)))
		return 0;
	return -EFAULT;
}
#endif

asmlinkage int sys_olduname(struct oldold_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error -= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error -= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error -= __put_user(0,name->release+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error -= __put_user(0,name->version+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error = __put_user(0,name->machine+__OLD_UTS_LEN);
	error = error ? -EFAULT : 0;

	return error;
}

#endif

asmlinkage int sys_sethostname(char *name, int len)
{
	if (!suser())
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;
	if(copy_from_user(system_utsname.nodename, name, len))
		return -EFAULT;
	system_utsname.nodename[len] = 0;
#ifdef CONFIG_TRANS_NAMES
	translations_dirty = 1;
#endif
	return 0;
}

asmlinkage int sys_gethostname(char *name, int len)
{
	int i;

	if (len < 0)
		return -EINVAL;
	i = 1 + strlen(system_utsname.nodename);
	if (i > len)
		i = len;
	return copy_to_user(name, system_utsname.nodename, i) ? -EFAULT : 0;
}

/*
 * Only setdomainname; getdomainname can be implemented by calling
 * uname()
 */
asmlinkage int sys_setdomainname(char *name, int len)
{
	if (!suser())
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;
	if(copy_from_user(system_utsname.domainname, name, len))
		return -EFAULT;
	system_utsname.domainname[len] = 0;
#ifdef CONFIG_TRANS_NAMES
	translations_dirty = 1;
#endif
	return 0;
}

asmlinkage int sys_getrlimit(unsigned int resource, struct rlimit *rlim)
{
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	else
		return copy_to_user(rlim, current->rlim + resource, sizeof(*rlim))
			? -EFAULT : 0;
}

asmlinkage int sys_setrlimit(unsigned int resource, struct rlimit *rlim)
{
	struct rlimit new_rlim, *old_rlim;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	if(copy_from_user(&new_rlim, rlim, sizeof(*rlim)))
		return -EFAULT;
	old_rlim = current->rlim + resource;
	if (((new_rlim.rlim_cur > old_rlim->rlim_max) ||
	     (new_rlim.rlim_max > old_rlim->rlim_max)) &&
	    !suser())
		return -EPERM;
	if (resource == RLIMIT_NOFILE) {
		if (new_rlim.rlim_cur > NR_OPEN || new_rlim.rlim_max > NR_OPEN)
			return -EPERM;
	}
	*old_rlim = new_rlim;
	return 0;
}

/*
 * It would make sense to put struct rusage in the task_struct,
 * except that would make the task_struct be *really big*.  After
 * task_struct gets moved into malloc'ed memory, it would
 * make sense to do this.  It will make moving the rest of the information
 * a lot simpler!  (Which we're not doing right now because we're not
 * measuring them yet).
 *
 * This is SMP safe.  Either we are called from sys_getrusage on ourselves
 * below (we know we aren't going to exit/disappear and only we change our
 * rusage counters), or we are called from wait4() on a process which is
 * either stopped or zombied.  In the zombied case the task won't get
 * reaped till shortly after the call to getrusage(), in both cases the
 * task being examined is in a frozen state so the counters won't change.
 */
int getrusage(struct task_struct *p, int who, struct rusage *ru)
{
	struct rusage r;

	memset((char *) &r, 0, sizeof(r));
	switch (who) {
		case RUSAGE_SELF:
			r.ru_utime.tv_sec = CT_TO_SECS(p->times.tms_utime);
			r.ru_utime.tv_usec = CT_TO_USECS(p->times.tms_utime);
			r.ru_stime.tv_sec = CT_TO_SECS(p->times.tms_stime);
			r.ru_stime.tv_usec = CT_TO_USECS(p->times.tms_stime);
			r.ru_minflt = p->min_flt;
			r.ru_majflt = p->maj_flt;
			r.ru_nswap = p->nswap;
			break;
		case RUSAGE_CHILDREN:
			r.ru_utime.tv_sec = CT_TO_SECS(p->times.tms_cutime);
			r.ru_utime.tv_usec = CT_TO_USECS(p->times.tms_cutime);
			r.ru_stime.tv_sec = CT_TO_SECS(p->times.tms_cstime);
			r.ru_stime.tv_usec = CT_TO_USECS(p->times.tms_cstime);
			r.ru_minflt = p->cmin_flt;
			r.ru_majflt = p->cmaj_flt;
			r.ru_nswap = p->cnswap;
			break;
		default:
			r.ru_utime.tv_sec = CT_TO_SECS(p->times.tms_utime + p->times.tms_cutime);
			r.ru_utime.tv_usec = CT_TO_USECS(p->times.tms_utime + p->times.tms_cutime);
			r.ru_stime.tv_sec = CT_TO_SECS(p->times.tms_stime + p->times.tms_cstime);
			r.ru_stime.tv_usec = CT_TO_USECS(p->times.tms_stime + p->times.tms_cstime);
			r.ru_minflt = p->min_flt + p->cmin_flt;
			r.ru_majflt = p->maj_flt + p->cmaj_flt;
			r.ru_nswap = p->nswap + p->cnswap;
			break;
	}
	return copy_to_user(ru, &r, sizeof(r)) ? -EFAULT : 0;
}

asmlinkage int sys_getrusage(int who, struct rusage *ru)
{
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN)
		return -EINVAL;
	return getrusage(current, who, ru);
}

asmlinkage int sys_umask(int mask)
{
	mask = xchg(&current->fs->umask, mask & S_IRWXUGO);
	return mask;
}

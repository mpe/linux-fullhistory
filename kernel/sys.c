/*
 *  linux/kernel/sys.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/mman.h>
#include <linux/smp_lock.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/prctl.h>

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
			p->uid != current->uid && !capable(CAP_SYS_NICE)) {
			error = EPERM;
			continue;
		}
		if (error == ESRCH)
			error = 0;
		if (priority > p->priority && !capable(CAP_SYS_NICE))
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


/*
 * Reboot system call: for obvious reasons only root may call it,
 * and even root needs to set up some magic numbers in the registers
 * so that some mistake won't make this reboot the whole machine.
 * You can also set the meaning of the ctrl-alt-del-key here.
 *
 * reboot doesn't sync: do that yourself before calling this.
 */
asmlinkage int sys_reboot(int magic1, int magic2, int cmd, void * arg)
{
	char buffer[256];

	/* We only trust the superuser with rebooting the system. */
	if (!capable(CAP_SYS_BOOT))
		return -EPERM;

	/* For safety, we require "magic" arguments. */
	if (magic1 != LINUX_REBOOT_MAGIC1 ||
	    (magic2 != LINUX_REBOOT_MAGIC2 && magic2 != LINUX_REBOOT_MAGIC2A &&
			magic2 != LINUX_REBOOT_MAGIC2B))
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
 * 100% compatible with POSIX with saved IDs. 
 *
 * SMP: There are not races, the GIDs are checked only by filesystem
 *      operations (as far as semantic preservation is concerned).
 */
asmlinkage int sys_setregid(gid_t rgid, gid_t egid)
{
	int old_rgid = current->gid;
	int old_egid = current->egid;

	if (rgid != (gid_t) -1) {
		if ((old_rgid == rgid) ||
		    (current->egid==rgid) ||
		    capable(CAP_SETGID))
			current->gid = rgid;
		else
			return -EPERM;
	}
	if (egid != (gid_t) -1) {
		if ((old_rgid == egid) ||
		    (current->egid == egid) ||
		    (current->sgid == egid) ||
		    capable(CAP_SETGID))
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

	if (capable(CAP_SETGID))
		current->gid = current->egid = current->sgid = current->fsgid = gid;
	else if ((gid == current->gid) || (gid == current->sgid))
		current->egid = current->fsgid = gid;
	else
		return -EPERM;

	if (current->egid != old_egid)
		current->dumpable = 0;
	return 0;
}
  
/* 
 * cap_emulate_setxuid() fixes the effective / permitted capabilities of
 * a process after a call to setuid, setreuid, or setresuid.
 *
 *  1) When set*uiding _from_ one of {r,e,s}uid == 0 _to_ all of
 *  {r,e,s}uid != 0, the permitted and effective capabilities are
 *  cleared.
 *
 *  2) When set*uiding _from_ euid == 0 _to_ euid != 0, the effective
 *  capabilities of the process are cleared.
 *
 *  3) When set*uiding _from_ euid != 0 _to_ euid == 0, the effective
 *  capabilities are set to the permitted capabilities.
 *
 *  fsuid is handled elsewhere. fsuid == 0 and {r,e,s}uid!= 0 should 
 *  never happen.
 *
 *  -astor 
 */
extern inline void cap_emulate_setxuid(int old_ruid, int old_euid, 
				       int old_suid)
{
	if ((old_ruid == 0 || old_euid == 0 || old_suid == 0) &&
	    (current->uid != 0 && current->euid != 0 && current->suid != 0)) {
		cap_clear(current->cap_permitted);
		cap_clear(current->cap_effective);
	}
	if (old_euid == 0 && current->euid != 0) {
		cap_clear(current->cap_effective);
	}
	if (old_euid != 0 && current->euid == 0) {
		current->cap_effective = current->cap_permitted;
	}
}

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
 * 100% compatible with POSIX with saved IDs. 
 */
asmlinkage int sys_setreuid(uid_t ruid, uid_t euid)
{
	int old_ruid, old_euid, old_suid, new_ruid;

	new_ruid = old_ruid = current->uid;
	old_euid = current->euid;
	old_suid = current->suid;
	if (ruid != (uid_t) -1) {
		if ((old_ruid == ruid) || 
		    (current->euid==ruid) ||
		    capable(CAP_SETUID))
			new_ruid = ruid;
		else
			return -EPERM;
	}
	if (euid != (uid_t) -1) {
		if ((old_ruid == euid) ||
		    (current->euid == euid) ||
		    (current->suid == euid) ||
		    capable(CAP_SETUID))
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
		free_uid(current);
		current->uid = new_ruid;
		alloc_uid(current);
	}
	
	if (!issecure(SECURE_NO_SETUID_FIXUP)) {
		cap_emulate_setxuid(old_ruid, old_euid, old_suid);
	}

	return 0;
}


		
/*
 * setuid() is implemented like SysV with SAVED_IDS 
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
	int old_ruid, old_suid, new_ruid;

	old_ruid = new_ruid = current->uid;
	old_suid = current->suid;
	if (capable(CAP_SETUID))
		new_ruid = current->euid = current->suid = current->fsuid = uid;
	else if ((uid == current->uid) || (uid == current->suid))
		current->fsuid = current->euid = uid;
	else
		return -EPERM;

	if (current->euid != old_euid)
		current->dumpable = 0;

       if (new_ruid != old_ruid) {
		/* See comment above about NPROC rlimit issues... */
		free_uid(current);
		current->uid = new_ruid;
		alloc_uid(current);
	}

	if (!issecure(SECURE_NO_SETUID_FIXUP)) {
		cap_emulate_setxuid(old_ruid, old_euid, old_suid);
	}

	return 0;
}


/*
 * This function implements a generic ability to update ruid, euid,
 * and suid.  This allows you to implement the 4.4 compatible seteuid().
 */
asmlinkage int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	int old_ruid = current->uid;
	int old_euid = current->euid;
	int old_suid = current->suid;

	if (!capable(CAP_SETUID)) {
		if ((ruid != (uid_t) -1) && (ruid != current->uid) &&
		    (ruid != current->euid) && (ruid != current->suid))
			return -EPERM;
		if ((euid != (uid_t) -1) && (euid != current->uid) &&
		    (euid != current->euid) && (euid != current->suid))
			return -EPERM;
		if ((suid != (uid_t) -1) && (suid != current->uid) &&
		    (suid != current->euid) && (suid != current->suid))
			return -EPERM;
	}
	if (ruid != (uid_t) -1) {
		/* See above commentary about NPROC rlimit issues here. */
		free_uid(current);
		current->uid = ruid;
		alloc_uid(current);
	}
	if (euid != (uid_t) -1) {
		if (euid != current->euid)
			current->dumpable = 0;
		current->euid = euid;
		current->fsuid = euid;
	}
	if (suid != (uid_t) -1)
		current->suid = suid;

	if (!issecure(SECURE_NO_SETUID_FIXUP)) {
		cap_emulate_setxuid(old_ruid, old_euid, old_suid);
	}

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
 * Same as above, but for rgid, egid, sgid.
 */
asmlinkage int sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
       if (!capable(CAP_SETGID)) {
		if ((rgid != (gid_t) -1) && (rgid != current->gid) &&
		    (rgid != current->egid) && (rgid != current->sgid))
			return -EPERM;
		if ((egid != (gid_t) -1) && (egid != current->gid) &&
		    (egid != current->egid) && (egid != current->sgid))
			return -EPERM;
		if ((sgid != (gid_t) -1) && (sgid != current->gid) &&
		    (sgid != current->egid) && (sgid != current->sgid))
			return -EPERM;
	}
	if (rgid != (gid_t) -1)
		current->gid = rgid;
	if (egid != (gid_t) -1) {
		if (egid != current->egid)
			current->dumpable = 0;
		current->egid = egid;
		current->fsgid = egid;
	}
	if (sgid != (gid_t) -1)
		current->sgid = sgid;
	return 0;
}

asmlinkage int sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	int retval;

	if (!(retval = put_user(current->gid, rgid)) &&
	    !(retval = put_user(current->egid, egid)))
		retval = put_user(current->sgid, sgid);

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
	    uid == current->suid || uid == current->fsuid || 
	    capable(CAP_SETUID))
		current->fsuid = uid;
	if (current->fsuid != old_fsuid)
		current->dumpable = 0;

	/* We emulate fsuid by essentially doing a scaled-down version
	 * of what we did in setresuid and friends. However, we only
	 * operate on the fs-specific bits of the process' effective
	 * capabilities 
	 *
	 * FIXME - is fsuser used for all CAP_FS_MASK capabilities?
	 *          if not, we might be a bit too harsh here.
	 */
	
	if (!issecure(SECURE_NO_SETUID_FIXUP)) {
		if (old_fsuid == 0 && current->fsuid != 0) {
			cap_t(current->cap_effective) &= ~CAP_FS_MASK;
		}
		if (old_fsuid != 0 && current->fsuid == 0) {
			cap_t(current->cap_effective) |=
				(cap_t(current->cap_permitted) & CAP_FS_MASK);
		}
	}

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
	    gid == current->sgid || gid == current->fsgid || 
	    capable(CAP_SETGID))
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

	/* From this point forward we keep holding onto the tasklist lock
	 * so that our parent does not change from under us. -DaveM
	 */
	read_lock(&tasklist_lock);

	err = -ESRCH;
	p = find_task_by_pid(pid);
	if (!p)
		goto out;

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
		int retval;
		struct task_struct *p;

		read_lock(&tasklist_lock);
		p = find_task_by_pid(pid);

		retval = -ESRCH;
		if (p)
			retval = p->pgrp;
		read_unlock(&tasklist_lock);
		return retval;
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
		int retval;
		struct task_struct *p;

		read_lock(&tasklist_lock);
		p = find_task_by_pid(pid);

		retval = -ESRCH;
		if(p)
			retval = p->session;
		read_unlock(&tasklist_lock);
		return retval;
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
 * Supplementary group IDs
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
	if (!capable(CAP_SETGID))
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

/*
 * This should really be a blocking read-write lock
 * rather than a semaphore. Anybody want to implement
 * one?
 */
struct semaphore uts_sem = MUTEX;

asmlinkage int sys_newuname(struct new_utsname * name)
{
	int errno = 0;

	down(&uts_sem);
	if (copy_to_user(name,&system_utsname,sizeof *name))
		errno = -EFAULT;
	up(&uts_sem);
	return errno;
}

asmlinkage int sys_sethostname(char *name, int len)
{
	int errno;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;
	down(&uts_sem);
	errno = -EFAULT;
	if (!copy_from_user(system_utsname.nodename, name, len)) {
		system_utsname.nodename[len] = 0;
		errno = 0;
	}
	up(&uts_sem);
	return errno;
}

asmlinkage int sys_gethostname(char *name, int len)
{
	int i, errno;

	if (len < 0)
		return -EINVAL;
	down(&uts_sem);
	i = 1 + strlen(system_utsname.nodename);
	if (i > len)
		i = len;
	errno = 0;
	if (copy_to_user(name, system_utsname.nodename, i))
		errno = -EFAULT;
	up(&uts_sem);
	return errno;
}

/*
 * Only setdomainname; getdomainname can be implemented by calling
 * uname()
 */
asmlinkage int sys_setdomainname(char *name, int len)
{
	int errno;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;

	down(&uts_sem);
	errno = -EFAULT;
	if (!copy_from_user(system_utsname.domainname, name, len)) {
		errno = 0;
		system_utsname.domainname[len] = 0;
	}
	up(&uts_sem);
	return errno;
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
	    !capable(CAP_SYS_RESOURCE))
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
    
asmlinkage int sys_prctl(int option, unsigned long arg2, unsigned long arg3,
			 unsigned long arg4, unsigned long arg5)
{
	int error = 0;
	int sig;

	switch (option) {
		case PR_SET_PDEATHSIG:
			sig = arg2;
			if (sig > _NSIG) {
				error = -EINVAL;
				break;
			}
			current->pdeath_signal = sig;
			break;
		default:
			error = -EINVAL;
			break;
	}
	return error;
}


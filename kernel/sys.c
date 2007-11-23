/*
 *  linux/kernel/sys.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

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

#include <asm/segment.h>
#include <asm/io.h>

/*
 * this indicates whether you can reboot with ctrl-alt-del: the default is yes
 */
static int C_A_D = 1;

extern void adjust_clock(void);

#define	PZERO	15

asmlinkage int sys_ni_syscall(void)
{
	return -ENOSYS;
}

static int proc_sel(struct task_struct *p, int which, int who)
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
	return 0;
}

asmlinkage int sys_setpriority(int which, int who, int niceval)
{
	struct task_struct **p;
	int error = ESRCH;
	int priority;

	if (which > 2 || which < 0)
		return -EINVAL;

	if ((priority = PZERO - niceval) <= 0)
		priority = 1;

	for(p = &LAST_TASK; p > &FIRST_TASK; --p) {
		if (!*p || !proc_sel(*p, which, who))
			continue;
		if ((*p)->uid != current->euid &&
			(*p)->uid != current->uid && !suser()) {
			error = EPERM;
			continue;
		}
		if (error == ESRCH)
			error = 0;
		if (priority > (*p)->priority && !suser())
			error = EACCES;
		else
			(*p)->priority = priority;
	}
	return -error;
}

asmlinkage int sys_getpriority(int which, int who)
{
	struct task_struct **p;
	int max_prio = 0;

	if (which > 2 || which < 0)
		return -EINVAL;

	for(p = &LAST_TASK; p > &FIRST_TASK; --p) {
		if (!*p || !proc_sel(*p, which, who))
			continue;
		if ((*p)->priority > max_prio)
			max_prio = (*p)->priority;
	}
	return(max_prio ? max_prio : -ESRCH);
}

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

extern void hard_reset_now(void);
extern asmlinkage sys_kill(int, int);

/*
 * Reboot system call: for obvious reasons only root may call it,
 * and even root needs to set up some magic numbers in the registers
 * so that some mistake won't make this reboot the whole machine.
 * You can also set the meaning of the ctrl-alt-del-key here.
 *
 * reboot doesn't sync: do that yourself before calling this.
 */
asmlinkage int sys_reboot(int magic, int magic_too, int flag)
{
	if (!suser())
		return -EPERM;
	if (magic != 0xfee1dead || magic_too != 672274793)
		return -EINVAL;
	if (flag == 0x01234567)
		hard_reset_now();
	else if (flag == 0x89ABCDEF)
		C_A_D = 1;
	else if (!flag)
		C_A_D = 0;
	else if (flag == 0xCDEF0123) {
		printk(KERN_EMERG "System halted\n");
		sys_kill(-1, SIGKILL);
		do_exit(0);
	} else
		return -EINVAL;
	return (0);
}

/*
 * This function gets called by ctrl-alt-del - ie the keyboard interrupt.
 * As it's called within an interrupt, it may NOT sync: the only choice
 * is whether to reboot at once, or just ignore the ctrl-alt-del.
 */
void ctrl_alt_del(void)
{
	if (C_A_D)
		hard_reset_now();
	else
		send_sig(SIGINT,task[1],1);
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
 */
asmlinkage int sys_setregid(gid_t rgid, gid_t egid)
{
	int old_rgid = current->gid;

	if (rgid != (gid_t) -1) {
		if ((old_rgid == rgid) ||
		    (current->egid==rgid) ||
		    suser())
			current->gid = rgid;
		else
			return(-EPERM);
	}
	if (egid != (gid_t) -1) {
		if ((old_rgid == egid) ||
		    (current->egid == egid) ||
		    (current->sgid == egid) ||
		    suser())
			current->egid = egid;
		else {
			current->gid = old_rgid;
			return(-EPERM);
		}
	}
	if (rgid != (gid_t) -1 ||
	    (egid != (gid_t) -1 && egid != old_rgid))
		current->sgid = current->egid;
	current->fsgid = current->egid;
	return 0;
}

/*
 * setgid() is implemented like SysV w/ SAVED_IDS 
 */
asmlinkage int sys_setgid(gid_t gid)
{
	if (suser())
		current->gid = current->egid = current->sgid = current->fsgid = gid;
	else if ((gid == current->gid) || (gid == current->sgid))
		current->egid = current->fsgid = gid;
	else
		return -EPERM;
	return 0;
}

asmlinkage int sys_acct(void)
{
	return -ENOSYS;
}

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
	int old_ruid = current->uid;

	if (ruid != (uid_t) -1) {
		if ((old_ruid == ruid) || 
		    (current->euid==ruid) ||
		    suser())
			current->uid = ruid;
		else
			return(-EPERM);
	}
	if (euid != (uid_t) -1) {
		if ((old_ruid == euid) ||
		    (current->euid == euid) ||
		    (current->suid == euid) ||
		    suser())
			current->euid = euid;
		else {
			current->uid = old_ruid;
			return(-EPERM);
		}
	}
	if (ruid != (uid_t) -1 ||
	    (euid != (uid_t) -1 && euid != old_ruid))
		current->suid = current->euid;
	current->fsuid = current->euid;
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
	if (suser())
		current->uid = current->euid = current->suid = current->fsuid = uid;
	else if ((uid == current->uid) || (uid == current->suid))
		current->fsuid = current->euid = uid;
	else
		return -EPERM;
	return(0);
}

/*
 * "setfsuid()" sets the fsuid - the uid used for filesystem checks. This
 * is used for "access()" and for the NFS daemon (letting nfsd stay at
 * whatever uid it wants to). It normally shadows "euid", except when
 * explicitly set by setfsuid() or for access..
 */
asmlinkage int sys_setfsuid(uid_t uid)
{
	int old_fsuid = current->fsuid;

	if (uid == current->uid || uid == current->euid ||
	    uid == current->suid || uid == current->fsuid || suser())
		current->fsuid = uid;
	return old_fsuid;
}

/*
 * Samma p� svenska..
 */
asmlinkage int sys_setfsgid(gid_t gid)
{
	int old_fsgid = current->fsgid;

	if (gid == current->gid || gid == current->egid ||
	    gid == current->sgid || gid == current->fsgid || suser())
		current->fsgid = gid;
	return old_fsgid;
}

asmlinkage int sys_times(struct tms * tbuf)
{
	if (tbuf) {
		int error = verify_area(VERIFY_WRITE,tbuf,sizeof *tbuf);
		if (error)
			return error;
		put_fs_long(current->utime,(unsigned long *)&tbuf->tms_utime);
		put_fs_long(current->stime,(unsigned long *)&tbuf->tms_stime);
		put_fs_long(current->cutime,(unsigned long *)&tbuf->tms_cutime);
		put_fs_long(current->cstime,(unsigned long *)&tbuf->tms_cstime);
	}
	return jiffies;
}

asmlinkage unsigned long sys_brk(unsigned long brk)
{
	int freepages;
	unsigned long rlim;
	unsigned long newbrk, oldbrk;

	if (brk < current->mm->end_code)
		return current->mm->brk;
	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(current->mm->brk);
	if (oldbrk == newbrk)
		return current->mm->brk = brk;

	/*
	 * Always allow shrinking brk
	 */
	if (brk <= current->mm->brk) {
		current->mm->brk = brk;
		do_munmap(newbrk, oldbrk-newbrk);
		return brk;
	}
	/*
	 * Check against rlimit and stack..
	 */
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (brk - current->mm->end_code > rlim)
		return current->mm->brk;
	/*
	 * Check against existing mmap mappings.
	 */
	if (find_vma_intersection(current, oldbrk, newbrk+PAGE_SIZE))
		return current->mm->brk;
	/*
	 * stupid algorithm to decide if we have enough memory: while
	 * simple, it hopefully works in most obvious cases.. Easy to
	 * fool it, but this should catch most mistakes.
	 */
	freepages = buffermem >> 12;
	freepages += nr_free_pages;
	freepages += nr_swap_pages;
	freepages -= (high_memory - 0x100000) >> 16;
	freepages -= (newbrk-oldbrk) >> 12;
	if (freepages < 0)
		return current->mm->brk;
#if 0
	freepages += current->mm->rss;
	freepages -= oldbrk >> 12;
	if (freepages < 0)
		return current->mm->brk;
#endif
	/*
	 * Ok, we have probably got enough memory - let it rip.
	 */
	current->mm->brk = brk;
	do_mmap(NULL, oldbrk, newbrk-oldbrk,
		PROT_READ|PROT_WRITE|PROT_EXEC,
		MAP_FIXED|MAP_PRIVATE, 0);
	return brk;
}

/*
 * This needs some heave checking ...
 * I just haven't get the stomach for it. I also don't fully
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

	if (!pid)
		pid = current->pid;
	if (!pgid)
		pgid = pid;
	if (pgid < 0)
		return -EINVAL;
	for_each_task(p) {
		if (p->pid == pid)
			goto found_task;
	}
	return -ESRCH;

found_task:
	if (p->p_pptr == current || p->p_opptr == current) {
		if (p->session != current->session)
			return -EPERM;
		if (p->did_exec)
			return -EACCES;
	} else if (p != current)
		return -ESRCH;
	if (p->leader)
		return -EPERM;
	if (pgid != pid) {
		struct task_struct * tmp;
		for_each_task (tmp) {
			if (tmp->pgrp == pgid &&
			 tmp->session == current->session)
				goto ok_pgid;
		}
		return -EPERM;
	}

ok_pgid:
	p->pgrp = pgid;
	return 0;
}

asmlinkage int sys_getpgid(pid_t pid)
{
	struct task_struct * p;

	if (!pid)
		return current->pgrp;
	for_each_task(p) {
		if (p->pid == pid)
			return p->pgrp;
	}
	return -ESRCH;
}

asmlinkage int sys_getpgrp(void)
{
	return current->pgrp;
}

asmlinkage int sys_setsid(void)
{
	if (current->leader)
		return -EPERM;
	current->leader = 1;
	current->session = current->pgrp = current->pid;
	current->tty = NULL;
	current->tty_old_pgrp = 0;
	return current->pgrp;
}

/*
 * Supplementary group ID's
 */
asmlinkage int sys_getgroups(int gidsetsize, gid_t *grouplist)
{
	int i;
	int * groups;

	if (gidsetsize) {
		i = verify_area(VERIFY_WRITE, grouplist, sizeof(gid_t) * gidsetsize);
		if (i)
			return i;
	}
	groups = current->groups;
	for (i = 0 ; (i < NGROUPS) && (*groups != NOGROUP) ; i++, groups++) {
		if (!gidsetsize)
			continue;
		if (i >= gidsetsize)
			break;
		put_user(*groups, grouplist);
		grouplist++;
	}
	return(i);
}

asmlinkage int sys_setgroups(int gidsetsize, gid_t *grouplist)
{
	int	i;

	if (!suser())
		return -EPERM;
	if (gidsetsize > NGROUPS)
		return -EINVAL;
	for (i = 0; i < gidsetsize; i++, grouplist++) {
		current->groups[i] = get_fs_word((unsigned short *) grouplist);
	}
	if (i < NGROUPS)
		current->groups[i] = NOGROUP;
	return 0;
}

int in_group_p(gid_t grp)
{
	int	i;

	if (grp == current->fsgid)
		return 1;

	for (i = 0; i < NGROUPS; i++) {
		if (current->groups[i] == NOGROUP)
			break;
		if (current->groups[i] == grp)
			return 1;
	}
	return 0;
}

asmlinkage int sys_newuname(struct new_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	error = verify_area(VERIFY_WRITE, name, sizeof *name);
	if (!error)
		memcpy_tofs(name,&system_utsname,sizeof *name);
	return error;
}

asmlinkage int sys_uname(struct old_utsname * name)
{
	int error;
	if (!name)
		return -EFAULT;
	error = verify_area(VERIFY_WRITE, name,sizeof *name);
	if (error)
		return error;
	memcpy_tofs(&name->sysname,&system_utsname.sysname,
		sizeof (system_utsname.sysname));
	memcpy_tofs(&name->nodename,&system_utsname.nodename,
		sizeof (system_utsname.nodename));
	memcpy_tofs(&name->release,&system_utsname.release,
		sizeof (system_utsname.release));
	memcpy_tofs(&name->version,&system_utsname.version,
		sizeof (system_utsname.version));
	memcpy_tofs(&name->machine,&system_utsname.machine,
		sizeof (system_utsname.machine));
	return 0;
}

asmlinkage int sys_olduname(struct oldold_utsname * name)
{
	int error;
	if (!name)
		return -EFAULT;
	error = verify_area(VERIFY_WRITE, name,sizeof *name);
	if (error)
		return error;
	memcpy_tofs(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	put_fs_byte(0,name->sysname+__OLD_UTS_LEN);
	memcpy_tofs(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	put_fs_byte(0,name->nodename+__OLD_UTS_LEN);
	memcpy_tofs(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	put_fs_byte(0,name->release+__OLD_UTS_LEN);
	memcpy_tofs(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	put_fs_byte(0,name->version+__OLD_UTS_LEN);
	memcpy_tofs(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	put_fs_byte(0,name->machine+__OLD_UTS_LEN);
	return 0;
}

asmlinkage int sys_sethostname(char *name, int len)
{
	int error;

	if (!suser())
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;
	error = verify_area(VERIFY_READ, name, len);
	if (error)
		return error;
	memcpy_fromfs(system_utsname.nodename, name, len);
	system_utsname.nodename[len] = 0;
	return 0;
}

asmlinkage int sys_gethostname(char *name, int len)
{
	int i;

	if (len < 0)
		return -EINVAL;
	i = verify_area(VERIFY_WRITE, name, len);
	if (i)
		return i;
	i = 1+strlen(system_utsname.nodename);
	if (i > len)
		i = len;
	memcpy_tofs(name, system_utsname.nodename, i);
	return 0;
}

/*
 * Only setdomainname; getdomainname can be implemented by calling
 * uname()
 */
asmlinkage int sys_setdomainname(char *name, int len)
{
	int	i;
	
	if (!suser())
		return -EPERM;
	if (len > __NEW_UTS_LEN)
		return -EINVAL;
	for (i=0; i < len; i++) {
		if ((system_utsname.domainname[i] = get_fs_byte(name+i)) == 0)
			return 0;
	}
	system_utsname.domainname[i] = 0;
	return 0;
}

asmlinkage int sys_getrlimit(unsigned int resource, struct rlimit *rlim)
{
	int error;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	error = verify_area(VERIFY_WRITE,rlim,sizeof *rlim);
	if (error)
		return error;
	memcpy_tofs(rlim, current->rlim + resource, sizeof(*rlim));
	return 0;	
}

asmlinkage int sys_setrlimit(unsigned int resource, struct rlimit *rlim)
{
	struct rlimit new_rlim, *old_rlim;
	int err;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	err = verify_area(VERIFY_READ, rlim, sizeof(*rlim));
	if (err)
		return err;
	memcpy_fromfs(&new_rlim, rlim, sizeof(*rlim));
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
 */
int getrusage(struct task_struct *p, int who, struct rusage *ru)
{
	int error;
	struct rusage r;

	error = verify_area(VERIFY_WRITE, ru, sizeof *ru);
	if (error)
		return error;
	memset((char *) &r, 0, sizeof(r));
	switch (who) {
		case RUSAGE_SELF:
			r.ru_utime.tv_sec = CT_TO_SECS(p->utime);
			r.ru_utime.tv_usec = CT_TO_USECS(p->utime);
			r.ru_stime.tv_sec = CT_TO_SECS(p->stime);
			r.ru_stime.tv_usec = CT_TO_USECS(p->stime);
			r.ru_minflt = p->mm->min_flt;
			r.ru_majflt = p->mm->maj_flt;
			break;
		case RUSAGE_CHILDREN:
			r.ru_utime.tv_sec = CT_TO_SECS(p->cutime);
			r.ru_utime.tv_usec = CT_TO_USECS(p->cutime);
			r.ru_stime.tv_sec = CT_TO_SECS(p->cstime);
			r.ru_stime.tv_usec = CT_TO_USECS(p->cstime);
			r.ru_minflt = p->mm->cmin_flt;
			r.ru_majflt = p->mm->cmaj_flt;
			break;
		default:
			r.ru_utime.tv_sec = CT_TO_SECS(p->utime + p->cutime);
			r.ru_utime.tv_usec = CT_TO_USECS(p->utime + p->cutime);
			r.ru_stime.tv_sec = CT_TO_SECS(p->stime + p->cstime);
			r.ru_stime.tv_usec = CT_TO_USECS(p->stime + p->cstime);
			r.ru_minflt = p->mm->min_flt + p->mm->cmin_flt;
			r.ru_majflt = p->mm->maj_flt + p->mm->cmaj_flt;
			break;
	}
	memcpy_tofs(ru, &r, sizeof(r));
	return 0;
}

asmlinkage int sys_getrusage(int who, struct rusage *ru)
{
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN)
		return -EINVAL;
	return getrusage(current, who, ru);
}

asmlinkage int sys_umask(int mask)
{
	int old = current->fs->umask;

	current->fs->umask = mask & S_IRWXUGO;
	return (old);
}

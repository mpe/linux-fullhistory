/*
 *  linux/kernel/sys.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/param.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/ptrace.h>

#include <asm/segment.h>
#include <asm/io.h>

/*
 * this indicates wether you can reboot with ctrl-alt-del: the default is yes
 */
static int C_A_D = 1;

/* 
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
struct timezone sys_tz = { 0, 0};

extern int session_of_pgrp(int pgrp);

#define	PZERO	15

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

int sys_setpriority(int which, int who, int niceval)
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

int sys_getpriority(int which, int who)
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

int sys_profil(void)
{
	return -ENOSYS;
}

int sys_ftime(void)
{
	return -ENOSYS;
}

int sys_break(void)
{
	return -ENOSYS;
}

int sys_stty(void)
{
	return -ENOSYS;
}

int sys_gtty(void)
{
	return -ENOSYS;
}

int sys_prof(void)
{
	return -ENOSYS;
}

int sys_ipc(void)
{
	return -ENOSYS;
}

unsigned long save_v86_state(struct vm86_regs * regs)
{
	unsigned long stack;

	if (!current->vm86_info) {
		printk("no vm86_info: BAD\n");
		do_exit(SIGSEGV);
	}
	memcpy_tofs(&(current->vm86_info->regs),regs,sizeof(*regs));
	put_fs_long(current->screen_bitmap,&(current->vm86_info->screen_bitmap));
	stack = current->tss.esp0;
	current->tss.esp0 = current->saved_kernel_stack;
	current->saved_kernel_stack = 0;
	return stack;
}

static void mark_screen_rdonly(struct task_struct * tsk)
{
	unsigned long tmp;
	unsigned long *pg_table;

	if ((tmp = tsk->tss.cr3) != 0) {
		tmp = *(unsigned long *) tmp;
		if (tmp & PAGE_PRESENT) {
			tmp &= 0xfffff000;
			pg_table = (0xA0000 >> PAGE_SHIFT) + (unsigned long *) tmp;
			tmp = 32;
			while (tmp--) {
				if (PAGE_PRESENT & *pg_table)
					*pg_table &= ~PAGE_RW;
				pg_table++;
			}
		}
	}
}

int sys_vm86(struct vm86_struct * v86)
{
	struct vm86_struct info;
	struct pt_regs * pt_regs = (struct pt_regs *) &v86;

	if (current->saved_kernel_stack)
		return -EPERM;
	memcpy_fromfs(&info,v86,sizeof(info));
/*
 * make sure the vm86() system call doesn't try to do anything silly
 */
	info.regs.__null_ds = 0;
	info.regs.__null_es = 0;
	info.regs.__null_fs = 0;
	info.regs.__null_gs = 0;
/*
 * The eflags register is also special: we cannot trust that the user
 * has set it up safely, so this makes sure interrupt etc flags are
 * inherited from protected mode.
 */
	info.regs.eflags &= 0x00000dd5;
	info.regs.eflags |= 0xfffff22a & pt_regs->eflags;
	info.regs.eflags |= VM_MASK;
	current->saved_kernel_stack = current->tss.esp0;
	current->tss.esp0 = (unsigned long) pt_regs;
	current->vm86_info = v86;
	current->screen_bitmap = info.screen_bitmap;
	if (info.flags & VM86_SCREEN_BITMAP)
		mark_screen_rdonly(current);
	__asm__ __volatile__("movl %0,%%esp\n\t"
		"pushl $ret_from_sys_call\n\t"
		"ret"::"g" ((long) &(info.regs)),"a" (info.regs.eax));
	return 0;
}

extern void hard_reset_now(void);

/*
 * Reboot system call: for obvious reasons only root may call it,
 * and even root needs to set up some magic numbers in the registers
 * so that some mistake won't make this reboot the whole machine.
 * You can also set the meaning of the ctrl-alt-del-key here.
 *
 * reboot doesn't sync: do that yourself before calling this.
 */
int sys_reboot(int magic, int magic_too, int flag)
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
	else
		return -EINVAL;
	return (0);
}

/*
 * This function gets called by ctrl-alt-del - ie the keyboard interrupt.
 * As it's called within an interrupt, it may NOT sync: the only choice
 * is wether to reboot at once, or just ignore the ctrl-alt-del.
 */
void ctrl_alt_del(void)
{
	if (C_A_D)
		hard_reset_now();
	else
		send_sig(SIGINT,task[1],1);
}
	

/*
 * This is done BSD-style, with no consideration of the saved gid, except
 * that if you set the effective gid, it sets the saved gid too.  This 
 * makes it possible for a setgid program to completely drop its privileges,
 * which is often a useful assertion to make when you are doing a security
 * audit over a program.
 *
 * The general idea is that a program which uses just setregid() will be
 * 100% compatible with BSD.  A program which uses just setgid() will be
 * 100% compatible with POSIX w/ Saved ID's. 
 */
int sys_setregid(gid_t rgid, gid_t egid)
{
	int old_rgid = current->gid;

	if (rgid != (gid_t) -1) {
		if ((current->egid==rgid) ||
		    (old_rgid == rgid) || 
		    suser())
			current->gid = rgid;
		else
			return(-EPERM);
	}
	if (egid != (gid_t) -1) {
		if ((old_rgid == egid) ||
		    (current->egid == egid) ||
		    suser()) {
			current->egid = egid;
			current->sgid = egid;
		} else {
			current->gid = old_rgid;
			return(-EPERM);
		}
	}
	return 0;
}

/*
 * setgid() is implemeneted like SysV w/ SAVED_IDS 
 */
int sys_setgid(gid_t gid)
{
	if (suser())
		current->gid = current->egid = current->sgid = gid;
	else if ((gid == current->gid) || (gid == current->sgid))
		current->egid = gid;
	else
		return -EPERM;
	return 0;
}

int sys_acct(void)
{
	return -ENOSYS;
}

int sys_phys(void)
{
	return -ENOSYS;
}

int sys_lock(void)
{
	return -ENOSYS;
}

int sys_mpx(void)
{
	return -ENOSYS;
}

int sys_ulimit(void)
{
	return -ENOSYS;
}

int sys_time(long * tloc)
{
	int i, error;

	i = CURRENT_TIME;
	if (tloc) {
		error = verify_area(VERIFY_WRITE, tloc, 4);
		if (error)
			return error;
		put_fs_long(i,(unsigned long *)tloc);
	}
	return i;
}

/*
 * Unprivileged users may change the real user id to the effective uid
 * or vice versa.  (BSD-style)
 *
 * When you set the effective uid, it sets the saved uid too.  This 
 * makes it possible for a setuid program to completely drop its privileges,
 * which is often a useful assertion to make when you are doing a security
 * audit over a program.
 *
 * The general idea is that a program which uses just setreuid() will be
 * 100% compatible with BSD.  A program which uses just setuid() will be
 * 100% compatible with POSIX w/ Saved ID's. 
 */
int sys_setreuid(uid_t ruid, uid_t euid)
{
	int old_ruid = current->uid;
	
	if (ruid != (uid_t) -1) {
		if ((current->euid==ruid) ||
		    (old_ruid == ruid) ||
		    suser())
			current->uid = ruid;
		else
			return(-EPERM);
	}
	if (euid != (uid_t) -1) {
		if ((old_ruid == euid) ||
		    (current->euid == euid) ||
		    suser()) {
			current->euid = euid;
			current->suid = euid;
		} else {
			current->uid = old_ruid;
			return(-EPERM);
		}
	}
	return 0;
}

/*
 * setuid() is implemeneted like SysV w/ SAVED_IDS 
 * 
 * Note that SAVED_ID's is deficient in that a setuid root program
 * like sendmail, for example, cannot set its uid to be a normal 
 * user and then switch back, because if you're root, setuid() sets
 * the saved uid too.  If you don't like this, blame the bright people
 * in the POSIX commmittee and/or USG.  Note that the BSD-style setreuid()
 * will allow a root program to temporarily drop privileges and be able to
 * regain them by swapping the real and effective uid.  
 */
int sys_setuid(uid_t uid)
{
	if (suser())
		current->uid = current->euid = current->suid = uid;
	else if ((uid == current->uid) || (uid == current->suid))
		current->euid = uid;
	else
		return -EPERM;
	return(0);
}

int sys_stime(long * tptr)
{
	if (!suser())
		return -EPERM;
	startup_time = get_fs_long((unsigned long *)tptr) - jiffies/HZ;
	jiffies_offset = 0;
	return 0;
}

int sys_times(struct tms * tbuf)
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

int sys_brk(unsigned long newbrk)
{
	unsigned long rlim;
	unsigned long oldbrk;

	oldbrk = current->brk;
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = 0xffffffff;
	if (newbrk >= current->end_code &&
	    newbrk - current->end_code <= rlim &&
	    newbrk < current->start_stack - 16384) {
		current->brk = newbrk;
		newbrk += 0x00000fff;
		newbrk &= 0xfffff000;
		oldbrk += 0x00000fff;
		oldbrk &= 0xfffff000;
		if (newbrk < oldbrk)
			unmap_page_range(newbrk, oldbrk-newbrk);
		else
			zeromap_page_range(oldbrk, newbrk-oldbrk, PAGE_COPY);
	}
	return current->brk;
}

/*
 * This needs some heave checking ...
 * I just haven't get the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 *
 * OK, I think I have the protection semantics right.... this is really
 * only important on a multi-user system anyway, to make sure one user
 * can't send a signal to a process owned by another.  -TYT, 12/12/91
 */
int sys_setpgid(pid_t pid, pid_t pgid)
{
	int i; 

	if (!pid)
		pid = current->pid;
	if (!pgid)
		pgid = current->pid;
	if (pgid < 0)
		return -EINVAL;
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && (task[i]->pid == pid) &&
		    ((task[i]->p_pptr == current) || 
		     (task[i] == current))) {
			if (task[i]->leader)
				return -EPERM;
			if ((task[i]->session != current->session) ||
			    ((pgid != pid) && 
			     (session_of_pgrp(pgid) != current->session)))
				return -EPERM;
			task[i]->pgrp = pgid;
			return 0;
		}
	return -ESRCH;
}

int sys_getpgrp(void)
{
	return current->pgrp;
}

int sys_setsid(void)
{
	if (current->leader && !suser())
		return -EPERM;
	current->leader = 1;
	current->session = current->pgrp = current->pid;
	current->tty = -1;
	return current->pgrp;
}

/*
 * Supplementary group ID's
 */
int sys_getgroups(int gidsetsize, gid_t *grouplist)
{
	int i;

	if (gidsetsize) {
		i = verify_area(VERIFY_WRITE, grouplist, sizeof(gid_t) * gidsetsize);
		if (i)
			return i;
	}
	for (i = 0; (i < NGROUPS) && (current->groups[i] != NOGROUP);
	     i++, grouplist++) {
		if (gidsetsize) {
			if (i >= gidsetsize)
				return -EINVAL;
			put_fs_word(current->groups[i], (short *) grouplist);
		}
	}
	return(i);
}

int sys_setgroups(int gidsetsize, gid_t *grouplist)
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

	if (grp == current->egid)
		return 1;

	for (i = 0; i < NGROUPS; i++) {
		if (current->groups[i] == NOGROUP)
			break;
		if (current->groups[i] == grp)
			return 1;
	}
	return 0;
}

int sys_newuname(struct new_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	error = verify_area(VERIFY_WRITE, name, sizeof *name);
	if (!error)
		memcpy_tofs(name,&system_utsname,sizeof *name);
	return error;
}

int sys_uname(struct old_utsname * name)
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

/*
 * Only sethostname; gethostname can be implemented by calling uname()
 */
int sys_sethostname(char *name, int len)
{
	int	i;
	
	if (!suser())
		return -EPERM;
	if (len > __NEW_UTS_LEN)
		return -EINVAL;
	for (i=0; i < len; i++) {
		if ((system_utsname.nodename[i] = get_fs_byte(name+i)) == 0)
			return 0;
	}
	system_utsname.nodename[i] = 0;
	return 0;
}

int sys_getrlimit(unsigned int resource, struct rlimit *rlim)
{
	int error;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	error = verify_area(VERIFY_WRITE,rlim,sizeof *rlim);
	if (error)
		return error;
	put_fs_long(current->rlim[resource].rlim_cur, 
		    (unsigned long *) rlim);
	put_fs_long(current->rlim[resource].rlim_max, 
		    ((unsigned long *) rlim)+1);
	return 0;	
}

int sys_setrlimit(unsigned int resource, struct rlimit *rlim)
{
	struct rlimit new, *old;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	old = current->rlim + resource;
	new.rlim_cur = get_fs_long((unsigned long *) rlim);
	new.rlim_max = get_fs_long(((unsigned long *) rlim)+1);
	if (((new.rlim_cur > old->rlim_max) ||
	     (new.rlim_max > old->rlim_max)) &&
	    !suser())
		return -EPERM;
	*old = new;
	return 0;
}

/*
 * It would make sense to put struct rusuage in the task_struct,
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
	unsigned long	*lp, *lpend, *dest;

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
			r.ru_minflt = p->min_flt;
			r.ru_majflt = p->maj_flt;
			break;
		case RUSAGE_CHILDREN:
			r.ru_utime.tv_sec = CT_TO_SECS(p->cutime);
			r.ru_utime.tv_usec = CT_TO_USECS(p->cutime);
			r.ru_stime.tv_sec = CT_TO_SECS(p->cstime);
			r.ru_stime.tv_usec = CT_TO_USECS(p->cstime);
			r.ru_minflt = p->cmin_flt;
			r.ru_majflt = p->cmaj_flt;
			break;
		default:
			r.ru_utime.tv_sec = CT_TO_SECS(p->utime + p->cutime);
			r.ru_utime.tv_usec = CT_TO_USECS(p->utime + p->cutime);
			r.ru_stime.tv_sec = CT_TO_SECS(p->stime + p->cstime);
			r.ru_stime.tv_usec = CT_TO_USECS(p->stime + p->cstime);
			r.ru_minflt = p->min_flt + p->cmin_flt;
			r.ru_majflt = p->maj_flt + p->cmaj_flt;
			break;
	}
	lp = (unsigned long *) &r;
	lpend = (unsigned long *) (&r+1);
	dest = (unsigned long *) ru;
	for (; lp < lpend; lp++, dest++) 
		put_fs_long(*lp, dest);
	return 0;
}

int sys_getrusage(int who, struct rusage *ru)
{
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN)
		return -EINVAL;
	return getrusage(current, who, ru);
}

#define LATCH ((1193180 + HZ/2)/HZ)

/*
 * This version of gettimeofday has near nanosecond resolution.
 * It was inspired by Steve McCanne's microtime-i386 for BSD.  -- jrs
 */
static inline void do_gettimeofday(struct timeval *tv)
{
	unsigned long nowtime;
	long count;

#ifdef __i386__
	cli();
	/* timer count may underflow right here */
	outb_p(0x00, 0x43);	/* latch the count ASAP */
	nowtime = jiffies;	/* must be saved inside cli/sti */
	count = inb_p(0x40);	/* read the latched count */
	count |= inb_p(0x40) << 8;
	/* we know probability of underflow is always MUCH less than 1% */
	if (count < (LATCH - LATCH/100))
		sti();
	else {
		/* check for pending timer interrupt */
		outb_p(0x0a, 0x20);
		if (inb(0x20) & 1)
			nowtime++;
		sti();
	}
	nowtime += jiffies_offset;
	tv->tv_sec = startup_time + CT_TO_SECS(nowtime);
	/* the correction term is always in the range [0, 1 clocktick) */
	tv->tv_usec = CT_TO_USECS(nowtime)
		+ ((LATCH - 1) - count)*(1000000/HZ)/LATCH;
#else /* not __i386__ */
	nowtime = jiffies + jiffes_offset;
	tv->tv_sec = startup_time + CT_TO_SECS(nowtime);
	tv->tv_usec = CT_TO_USECS(nowtime);
#endif /* not __i386__ */
}

int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int error;

	if (tv) {
		struct timeval ktv;
		error = verify_area(VERIFY_WRITE, tv, sizeof *tv);
		if (error)
			return error;
		do_gettimeofday(&ktv);
		put_fs_long(ktv.tv_sec, &tv->tv_sec);
		put_fs_long(ktv.tv_usec, &tv->tv_usec);
	}
	if (tz) {
		error = verify_area(VERIFY_WRITE, tz, sizeof *tz);
		if (error)
			return error;
		put_fs_long(sys_tz.tz_minuteswest, (unsigned long *) tz);
		put_fs_long(sys_tz.tz_dsttime, ((unsigned long *) tz)+1);
	}
	return 0;
}

/*
 * The first time we set the timezone, we will warp the clock so that
 * it is ticking GMT time instead of local time.  Presumably, 
 * if someone is setting the timezone then we are running in an
 * environment where the programs understand about timezones.
 * This should be done at boot time in the /etc/rc script, as
 * soon as possible, so that the clock can be set right.  Otherwise,
 * various programs will get confused when the clock gets warped.
 */
int sys_settimeofday(struct timeval *tv, struct timezone *tz)
{
	static int	firsttime = 1;
	void 		adjust_clock(void);

	if (!suser())
		return -EPERM;
	if (tz) {
		sys_tz.tz_minuteswest = get_fs_long((unsigned long *) tz);
		sys_tz.tz_dsttime = get_fs_long(((unsigned long *) tz)+1);
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				adjust_clock();
		}
	}
	if (tv) {
		int sec, usec;

		sec = get_fs_long((unsigned long *)tv);
		usec = get_fs_long(((unsigned long *)tv)+1);
	
		startup_time = sec - jiffies/HZ;
		jiffies_offset = usec * HZ / 1000000 - jiffies%HZ;
	}
	return 0;
}

/*
 * Adjust the time obtained from the CMOS to be GMT time instead of
 * local time.
 * 
 * This is ugly, but preferable to the alternatives.  Otherwise we
 * would either need to write a program to do it in /etc/rc (and risk
 * confusion if the program gets run more than once; it would also be 
 * hard to make the program warp the clock precisely n hours)  or
 * compile in the timezone information into the kernel.  Bad, bad....
 *
 * XXX Currently does not adjust for daylight savings time.  May not
 * need to do anything, depending on how smart (dumb?) the BIOS
 * is.  Blast it all.... the best thing to do not depend on the CMOS
 * clock at all, but get the time via NTP or timed if you're on a 
 * network....				- TYT, 1/1/92
 */
void adjust_clock(void)
{
	startup_time += sys_tz.tz_minuteswest*60;
}

int sys_umask(int mask)
{
	int old = current->umask;

	current->umask = mask & 0777;
	return (old);
}


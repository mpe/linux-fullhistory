/*
 *  linux/kernel/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002-2003 Stephen Rothwell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/sched.h>	/* for MAX_SCHEDULE_TIMEOUT */
#include <linux/futex.h>	/* for FUTEX_WAIT */
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/security.h>

#include <asm/uaccess.h>
#include <asm/bug.h>

int get_compat_timespec(struct timespec *ts, const struct compat_timespec __user *cts)
{
	return (!access_ok(VERIFY_READ, cts, sizeof(*cts)) ||
			__get_user(ts->tv_sec, &cts->tv_sec) ||
			__get_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

int put_compat_timespec(const struct timespec *ts, struct compat_timespec __user *cts)
{
	return (!access_ok(VERIFY_WRITE, cts, sizeof(*cts)) ||
			__put_user(ts->tv_sec, &cts->tv_sec) ||
			__put_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

static long compat_nanosleep_restart(struct restart_block *restart)
{
	unsigned long expire = restart->arg0, now = jiffies;
	struct compat_timespec __user *rmtp;

	/* Did it expire while we handled signals? */
	if (!time_after(expire, now))
		return 0;

	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire - now);
	if (expire == 0)
		return 0;

	rmtp = (struct compat_timespec __user *)restart->arg1;
	if (rmtp) {
		struct compat_timespec ct;
		struct timespec t;

		jiffies_to_timespec(expire, &t);
		ct.tv_sec = t.tv_sec;
		ct.tv_nsec = t.tv_nsec;
		if (copy_to_user(rmtp, &ct, sizeof(ct)))
			return -EFAULT;
	}
	/* The 'restart' block is already filled in */
	return -ERESTART_RESTARTBLOCK;
}

asmlinkage long compat_sys_nanosleep(struct compat_timespec __user *rqtp,
		struct compat_timespec __user *rmtp)
{
	struct timespec t;
	struct restart_block *restart;
	unsigned long expire;

	if (get_compat_timespec(&t, rqtp))
		return -EFAULT;

	if ((t.tv_nsec >= 1000000000L) || (t.tv_nsec < 0) || (t.tv_sec < 0))
		return -EINVAL;

	expire = timespec_to_jiffies(&t) + (t.tv_sec || t.tv_nsec);
	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire);
	if (expire == 0)
		return 0;

	if (rmtp) {
		jiffies_to_timespec(expire, &t);
		if (put_compat_timespec(&t, rmtp))
			return -EFAULT;
	}
	restart = &current_thread_info()->restart_block;
	restart->fn = compat_nanosleep_restart;
	restart->arg0 = jiffies + expire;
	restart->arg1 = (unsigned long) rmtp;
	return -ERESTART_RESTARTBLOCK;
}

static inline long get_compat_itimerval(struct itimerval *o,
		struct compat_itimerval __user *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->it_interval.tv_sec, &i->it_interval.tv_sec) |
		 __get_user(o->it_interval.tv_usec, &i->it_interval.tv_usec) |
		 __get_user(o->it_value.tv_sec, &i->it_value.tv_sec) |
		 __get_user(o->it_value.tv_usec, &i->it_value.tv_usec)));
}

static inline long put_compat_itimerval(struct compat_itimerval __user *o,
		struct itimerval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->it_interval.tv_sec, &o->it_interval.tv_sec) |
		 __put_user(i->it_interval.tv_usec, &o->it_interval.tv_usec) |
		 __put_user(i->it_value.tv_sec, &o->it_value.tv_sec) |
		 __put_user(i->it_value.tv_usec, &o->it_value.tv_usec)));
}

asmlinkage long compat_sys_getitimer(int which,
		struct compat_itimerval __user *it)
{
	struct itimerval kit;
	int error;

	error = do_getitimer(which, &kit);
	if (!error && put_compat_itimerval(it, &kit))
		error = -EFAULT;
	return error;
}

asmlinkage long compat_sys_setitimer(int which,
		struct compat_itimerval __user *in,
		struct compat_itimerval __user *out)
{
	struct itimerval kin, kout;
	int error;

	if (in) {
		if (get_compat_itimerval(&kin, in))
			return -EFAULT;
	} else
		memset(&kin, 0, sizeof(kin));

	error = do_setitimer(which, &kin, out ? &kout : NULL);
	if (error || !out)
		return error;
	if (put_compat_itimerval(out, &kout))
		return -EFAULT;
	return 0;
}

asmlinkage long compat_sys_times(struct compat_tms __user *tbuf)
{
	/*
	 *	In the SMP world we might just be unlucky and have one of
	 *	the times increment as we use it. Since the value is an
	 *	atomically safe type this is just fine. Conceptually its
	 *	as if the syscall took an instant longer to occur.
	 */
	if (tbuf) {
		struct compat_tms tmp;
		struct task_struct *tsk = current;
		struct task_struct *t;
		cputime_t utime, stime, cutime, cstime;

		read_lock(&tasklist_lock);
		utime = tsk->signal->utime;
		stime = tsk->signal->stime;
		t = tsk;
		do {
			utime = cputime_add(utime, t->utime);
			stime = cputime_add(stime, t->stime);
			t = next_thread(t);
		} while (t != tsk);

		/*
		 * While we have tasklist_lock read-locked, no dying thread
		 * can be updating current->signal->[us]time.  Instead,
		 * we got their counts included in the live thread loop.
		 * However, another thread can come in right now and
		 * do a wait call that updates current->signal->c[us]time.
		 * To make sure we always see that pair updated atomically,
		 * we take the siglock around fetching them.
		 */
		spin_lock_irq(&tsk->sighand->siglock);
		cutime = tsk->signal->cutime;
		cstime = tsk->signal->cstime;
		spin_unlock_irq(&tsk->sighand->siglock);
		read_unlock(&tasklist_lock);

		tmp.tms_utime = compat_jiffies_to_clock_t(cputime_to_jiffies(utime));
		tmp.tms_stime = compat_jiffies_to_clock_t(cputime_to_jiffies(stime));
		tmp.tms_cutime = compat_jiffies_to_clock_t(cputime_to_jiffies(cutime));
		tmp.tms_cstime = compat_jiffies_to_clock_t(cputime_to_jiffies(cstime));
		if (copy_to_user(tbuf, &tmp, sizeof(tmp)))
			return -EFAULT;
	}
	return compat_jiffies_to_clock_t(jiffies);
}

/*
 * Assumption: old_sigset_t and compat_old_sigset_t are both
 * types that can be passed to put_user()/get_user().
 */

asmlinkage long compat_sys_sigpending(compat_old_sigset_t __user *set)
{
	old_sigset_t s;
	long ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_sigpending((old_sigset_t __user *) &s);
	set_fs(old_fs);
	if (ret == 0)
		ret = put_user(s, set);
	return ret;
}

asmlinkage long compat_sys_sigprocmask(int how, compat_old_sigset_t __user *set,
		compat_old_sigset_t __user *oset)
{
	old_sigset_t s;
	long ret;
	mm_segment_t old_fs;

	if (set && get_user(s, set))
		return -EFAULT;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sigprocmask(how,
			      set ? (old_sigset_t __user *) &s : NULL,
			      oset ? (old_sigset_t __user *) &s : NULL);
	set_fs(old_fs);
	if (ret == 0)
		if (oset)
			ret = put_user(s, oset);
	return ret;
}

#ifdef CONFIG_FUTEX
asmlinkage long compat_sys_futex(u32 __user *uaddr, int op, int val,
		struct compat_timespec __user *utime, u32 __user *uaddr2,
		int val3)
{
	struct timespec t;
	unsigned long timeout = MAX_SCHEDULE_TIMEOUT;
	int val2 = 0;

	if ((op == FUTEX_WAIT) && utime) {
		if (get_compat_timespec(&t, utime))
			return -EFAULT;
		timeout = timespec_to_jiffies(&t) + 1;
	}
	if (op >= FUTEX_REQUEUE)
		val2 = (int) (unsigned long) utime;

	return do_futex((unsigned long)uaddr, op, val, timeout,
			(unsigned long)uaddr2, val2, val3);
}
#endif

asmlinkage long compat_sys_setrlimit(unsigned int resource,
		struct compat_rlimit __user *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();

	if (resource >= RLIM_NLIMITS) 
		return -EINVAL;	

	if (!access_ok(VERIFY_READ, rlim, sizeof(*rlim)) ||
	    __get_user(r.rlim_cur, &rlim->rlim_cur) ||
	    __get_user(r.rlim_max, &rlim->rlim_max))
		return -EFAULT;

	if (r.rlim_cur == COMPAT_RLIM_INFINITY)
		r.rlim_cur = RLIM_INFINITY;
	if (r.rlim_max == COMPAT_RLIM_INFINITY)
		r.rlim_max = RLIM_INFINITY;
	set_fs(KERNEL_DS);
	ret = sys_setrlimit(resource, (struct rlimit __user *) &r);
	set_fs(old_fs);
	return ret;
}

#ifdef COMPAT_RLIM_OLD_INFINITY

asmlinkage long compat_sys_old_getrlimit(unsigned int resource,
		struct compat_rlimit __user *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_old_getrlimit(resource, &r);
	set_fs(old_fs);

	if (!ret) {
		if (r.rlim_cur > COMPAT_RLIM_OLD_INFINITY)
			r.rlim_cur = COMPAT_RLIM_INFINITY;
		if (r.rlim_max > COMPAT_RLIM_OLD_INFINITY)
			r.rlim_max = COMPAT_RLIM_INFINITY;

		if (!access_ok(VERIFY_WRITE, rlim, sizeof(*rlim)) ||
		    __put_user(r.rlim_cur, &rlim->rlim_cur) ||
		    __put_user(r.rlim_max, &rlim->rlim_max))
			return -EFAULT;
	}
	return ret;
}

#endif

asmlinkage long compat_sys_getrlimit (unsigned int resource,
		struct compat_rlimit __user *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_getrlimit(resource, (struct rlimit __user *) &r);
	set_fs(old_fs);
	if (!ret) {
		if (r.rlim_cur > COMPAT_RLIM_INFINITY)
			r.rlim_cur = COMPAT_RLIM_INFINITY;
		if (r.rlim_max > COMPAT_RLIM_INFINITY)
			r.rlim_max = COMPAT_RLIM_INFINITY;

		if (!access_ok(VERIFY_WRITE, rlim, sizeof(*rlim)) ||
		    __put_user(r.rlim_cur, &rlim->rlim_cur) ||
		    __put_user(r.rlim_max, &rlim->rlim_max))
			return -EFAULT;
	}
	return ret;
}

int put_compat_rusage(const struct rusage *r, struct compat_rusage __user *ru)
{
	if (!access_ok(VERIFY_WRITE, ru, sizeof(*ru)) ||
	    __put_user(r->ru_utime.tv_sec, &ru->ru_utime.tv_sec) ||
	    __put_user(r->ru_utime.tv_usec, &ru->ru_utime.tv_usec) ||
	    __put_user(r->ru_stime.tv_sec, &ru->ru_stime.tv_sec) ||
	    __put_user(r->ru_stime.tv_usec, &ru->ru_stime.tv_usec) ||
	    __put_user(r->ru_maxrss, &ru->ru_maxrss) ||
	    __put_user(r->ru_ixrss, &ru->ru_ixrss) ||
	    __put_user(r->ru_idrss, &ru->ru_idrss) ||
	    __put_user(r->ru_isrss, &ru->ru_isrss) ||
	    __put_user(r->ru_minflt, &ru->ru_minflt) ||
	    __put_user(r->ru_majflt, &ru->ru_majflt) ||
	    __put_user(r->ru_nswap, &ru->ru_nswap) ||
	    __put_user(r->ru_inblock, &ru->ru_inblock) ||
	    __put_user(r->ru_oublock, &ru->ru_oublock) ||
	    __put_user(r->ru_msgsnd, &ru->ru_msgsnd) ||
	    __put_user(r->ru_msgrcv, &ru->ru_msgrcv) ||
	    __put_user(r->ru_nsignals, &ru->ru_nsignals) ||
	    __put_user(r->ru_nvcsw, &ru->ru_nvcsw) ||
	    __put_user(r->ru_nivcsw, &ru->ru_nivcsw))
		return -EFAULT;
	return 0;
}

asmlinkage long compat_sys_getrusage(int who, struct compat_rusage __user *ru)
{
	struct rusage r;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_getrusage(who, (struct rusage __user *) &r);
	set_fs(old_fs);

	if (ret)
		return ret;

	if (put_compat_rusage(&r, ru))
		return -EFAULT;

	return 0;
}

asmlinkage long
compat_sys_wait4(compat_pid_t pid, compat_uint_t __user *stat_addr, int options,
	struct compat_rusage __user *ru)
{
	if (!ru) {
		return sys_wait4(pid, stat_addr, options, NULL);
	} else {
		struct rusage r;
		int ret;
		unsigned int status;
		mm_segment_t old_fs = get_fs();

		set_fs (KERNEL_DS);
		ret = sys_wait4(pid,
				(stat_addr ?
				 (unsigned int __user *) &status : NULL),
				options, (struct rusage __user *) &r);
		set_fs (old_fs);

		if (ret > 0) {
			if (put_compat_rusage(&r, ru))
				return -EFAULT;
			if (stat_addr && put_user(status, stat_addr))
				return -EFAULT;
		}
		return ret;
	}
}

asmlinkage long compat_sys_waitid(int which, compat_pid_t pid,
		struct compat_siginfo __user *uinfo, int options,
		struct compat_rusage __user *uru)
{
	siginfo_t info;
	struct rusage ru;
	long ret;
	mm_segment_t old_fs = get_fs();

	memset(&info, 0, sizeof(info));

	set_fs(KERNEL_DS);
	ret = sys_waitid(which, pid, (siginfo_t __user *)&info, options,
			 uru ? (struct rusage __user *)&ru : NULL);
	set_fs(old_fs);

	if ((ret < 0) || (info.si_signo == 0))
		return ret;

	if (uru) {
		ret = put_compat_rusage(&ru, uru);
		if (ret)
			return ret;
	}

	BUG_ON(info.si_code & __SI_MASK);
	info.si_code |= __SI_CHLD;
	return copy_siginfo_to_user32(uinfo, &info);
}

static int compat_get_user_cpu_mask(compat_ulong_t __user *user_mask_ptr,
				    unsigned len, cpumask_t *new_mask)
{
	unsigned long *k;

	if (len < sizeof(cpumask_t))
		memset(new_mask, 0, sizeof(cpumask_t));
	else if (len > sizeof(cpumask_t))
		len = sizeof(cpumask_t);

	k = cpus_addr(*new_mask);
	return compat_get_bitmap(k, user_mask_ptr, len * 8);
}

asmlinkage long compat_sys_sched_setaffinity(compat_pid_t pid,
					     unsigned int len,
					     compat_ulong_t __user *user_mask_ptr)
{
	cpumask_t new_mask;
	int retval;

	retval = compat_get_user_cpu_mask(user_mask_ptr, len, &new_mask);
	if (retval)
		return retval;

	return sched_setaffinity(pid, new_mask);
}

asmlinkage long compat_sys_sched_getaffinity(compat_pid_t pid, unsigned int len,
					     compat_ulong_t __user *user_mask_ptr)
{
	int ret;
	cpumask_t mask;
	unsigned long *k;
	unsigned int min_length = sizeof(cpumask_t);

	if (NR_CPUS <= BITS_PER_COMPAT_LONG)
		min_length = sizeof(compat_ulong_t);

	if (len < min_length)
		return -EINVAL;

	ret = sched_getaffinity(pid, &mask);
	if (ret < 0)
		return ret;

	k = cpus_addr(mask);
	ret = compat_put_bitmap(user_mask_ptr, k, min_length * 8);
	if (ret)
		return ret;

	return min_length;
}

static int get_compat_itimerspec(struct itimerspec *dst, 
				 struct compat_itimerspec __user *src)
{ 
	if (get_compat_timespec(&dst->it_interval, &src->it_interval) ||
	    get_compat_timespec(&dst->it_value, &src->it_value))
		return -EFAULT;
	return 0;
} 

static int put_compat_itimerspec(struct compat_itimerspec __user *dst, 
				 struct itimerspec *src)
{ 
	if (put_compat_timespec(&src->it_interval, &dst->it_interval) ||
	    put_compat_timespec(&src->it_value, &dst->it_value))
		return -EFAULT;
	return 0;
} 

long compat_sys_timer_settime(timer_t timer_id, int flags,
			  struct compat_itimerspec __user *new, 
			  struct compat_itimerspec __user *old)
{ 
	long err;
	mm_segment_t oldfs;
	struct itimerspec newts, oldts;

	if (!new)
		return -EINVAL;
	if (get_compat_itimerspec(&newts, new))
		return -EFAULT;	
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_timer_settime(timer_id, flags,
				(struct itimerspec __user *) &newts,
				(struct itimerspec __user *) &oldts);
	set_fs(oldfs); 
	if (!err && old && put_compat_itimerspec(old, &oldts))
		return -EFAULT;
	return err;
} 

long compat_sys_timer_gettime(timer_t timer_id,
		struct compat_itimerspec __user *setting)
{ 
	long err;
	mm_segment_t oldfs;
	struct itimerspec ts; 

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_timer_gettime(timer_id,
				(struct itimerspec __user *) &ts); 
	set_fs(oldfs); 
	if (!err && put_compat_itimerspec(setting, &ts))
		return -EFAULT;
	return err;
} 

long compat_sys_clock_settime(clockid_t which_clock,
		struct compat_timespec __user *tp)
{
	long err;
	mm_segment_t oldfs;
	struct timespec ts; 

	if (get_compat_timespec(&ts, tp))
		return -EFAULT; 
	oldfs = get_fs();
	set_fs(KERNEL_DS);	
	err = sys_clock_settime(which_clock,
				(struct timespec __user *) &ts);
	set_fs(oldfs);
	return err;
} 

long compat_sys_clock_gettime(clockid_t which_clock,
		struct compat_timespec __user *tp)
{
	long err;
	mm_segment_t oldfs;
	struct timespec ts; 

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_clock_gettime(which_clock,
				(struct timespec __user *) &ts);
	set_fs(oldfs);
	if (!err && put_compat_timespec(&ts, tp))
		return -EFAULT; 
	return err;
} 

long compat_sys_clock_getres(clockid_t which_clock,
		struct compat_timespec __user *tp)
{
	long err;
	mm_segment_t oldfs;
	struct timespec ts; 

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_clock_getres(which_clock,
			       (struct timespec __user *) &ts);
	set_fs(oldfs);
	if (!err && tp && put_compat_timespec(&ts, tp))
		return -EFAULT; 
	return err;
} 

long compat_sys_clock_nanosleep(clockid_t which_clock, int flags,
			    struct compat_timespec __user *rqtp,
			    struct compat_timespec __user *rmtp)
{
	long err;
	mm_segment_t oldfs;
	struct timespec in, out; 

	if (get_compat_timespec(&in, rqtp)) 
		return -EFAULT;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_clock_nanosleep(which_clock, flags,
				  (struct timespec __user *) &in,
				  (struct timespec __user *) &out);
	set_fs(oldfs);
	if ((err == -ERESTART_RESTARTBLOCK) && rmtp &&
	    put_compat_timespec(&out, rmtp))
		return -EFAULT;
	return err;	
} 

/*
 * We currently only need the following fields from the sigevent
 * structure: sigev_value, sigev_signo, sig_notify and (sometimes
 * sigev_notify_thread_id).  The others are handled in user mode.
 * We also assume that copying sigev_value.sival_int is sufficient
 * to keep all the bits of sigev_value.sival_ptr intact.
 */
int get_compat_sigevent(struct sigevent *event,
		const struct compat_sigevent __user *u_event)
{
	memset(&event, 0, sizeof(*event));
	return (!access_ok(VERIFY_READ, u_event, sizeof(*u_event)) ||
		__get_user(event->sigev_value.sival_int,
			&u_event->sigev_value.sival_int) ||
		__get_user(event->sigev_signo, &u_event->sigev_signo) ||
		__get_user(event->sigev_notify, &u_event->sigev_notify) ||
		__get_user(event->sigev_notify_thread_id,
			&u_event->sigev_notify_thread_id))
		? -EFAULT : 0;
}

/* timer_create is architecture specific because it needs sigevent conversion */

long compat_get_bitmap(unsigned long *mask, compat_ulong_t __user *umask,
		       unsigned long bitmap_size)
{
	int i, j;
	unsigned long m;
	compat_ulong_t um;
	unsigned long nr_compat_longs;

	/* align bitmap up to nearest compat_long_t boundary */
	bitmap_size = ALIGN(bitmap_size, BITS_PER_COMPAT_LONG);

	if (!access_ok(VERIFY_READ, umask, bitmap_size / 8))
		return -EFAULT;

	nr_compat_longs = BITS_TO_COMPAT_LONGS(bitmap_size);

	for (i = 0; i < BITS_TO_LONGS(bitmap_size); i++) {
		m = 0;

		for (j = 0; j < sizeof(m)/sizeof(um); j++) {
			/*
			 * We dont want to read past the end of the userspace
			 * bitmap. We must however ensure the end of the
			 * kernel bitmap is zeroed.
			 */
			if (nr_compat_longs-- > 0) {
				if (__get_user(um, umask))
					return -EFAULT;
			} else {
				um = 0;
			}

			umask++;
			m |= (long)um << (j * BITS_PER_COMPAT_LONG);
		}
		*mask++ = m;
	}

	return 0;
}

long compat_put_bitmap(compat_ulong_t __user *umask, unsigned long *mask,
		       unsigned long bitmap_size)
{
	int i, j;
	unsigned long m;
	compat_ulong_t um;
	unsigned long nr_compat_longs;

	/* align bitmap up to nearest compat_long_t boundary */
	bitmap_size = ALIGN(bitmap_size, BITS_PER_COMPAT_LONG);

	if (!access_ok(VERIFY_WRITE, umask, bitmap_size / 8))
		return -EFAULT;

	nr_compat_longs = BITS_TO_COMPAT_LONGS(bitmap_size);

	for (i = 0; i < BITS_TO_LONGS(bitmap_size); i++) {
		m = *mask++;

		for (j = 0; j < sizeof(m)/sizeof(um); j++) {
			um = m;

			/*
			 * We dont want to write past the end of the userspace
			 * bitmap.
			 */
			if (nr_compat_longs-- > 0) {
				if (__put_user(um, umask))
					return -EFAULT;
			}

			umask++;
			m >>= 4*sizeof(um);
			m >>= 4*sizeof(um);
		}
	}

	return 0;
}

void
sigset_from_compat (sigset_t *set, compat_sigset_t *compat)
{
	switch (_NSIG_WORDS) {
#if defined (__COMPAT_ENDIAN_SWAP__)
	case 4: set->sig[3] = compat->sig[7] | (((long)compat->sig[6]) << 32 );
	case 3: set->sig[2] = compat->sig[5] | (((long)compat->sig[4]) << 32 );
	case 2: set->sig[1] = compat->sig[3] | (((long)compat->sig[2]) << 32 );
	case 1: set->sig[0] = compat->sig[1] | (((long)compat->sig[0]) << 32 );
#else
	case 4: set->sig[3] = compat->sig[6] | (((long)compat->sig[7]) << 32 );
	case 3: set->sig[2] = compat->sig[4] | (((long)compat->sig[5]) << 32 );
	case 2: set->sig[1] = compat->sig[2] | (((long)compat->sig[3]) << 32 );
	case 1: set->sig[0] = compat->sig[0] | (((long)compat->sig[1]) << 32 );
#endif
	}
}

asmlinkage long
compat_sys_rt_sigtimedwait (compat_sigset_t __user *uthese,
		struct compat_siginfo __user *uinfo,
		struct compat_timespec __user *uts, compat_size_t sigsetsize)
{
	compat_sigset_t s32;
	sigset_t s;
	int sig;
	struct timespec t;
	siginfo_t info;
	long ret, timeout = 0;

	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&s32, uthese, sizeof(compat_sigset_t)))
		return -EFAULT;
	sigset_from_compat(&s, &s32);
	sigdelsetmask(&s,sigmask(SIGKILL)|sigmask(SIGSTOP));
	signotset(&s);

	if (uts) {
		if (get_compat_timespec (&t, uts))
			return -EFAULT;
		if (t.tv_nsec >= 1000000000L || t.tv_nsec < 0
				|| t.tv_sec < 0)
			return -EINVAL;
	}

	spin_lock_irq(&current->sighand->siglock);
	sig = dequeue_signal(current, &s, &info);
	if (!sig) {
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (uts)
			timeout = timespec_to_jiffies(&t)
				+(t.tv_sec || t.tv_nsec);
		if (timeout) {
			current->real_blocked = current->blocked;
			sigandsets(&current->blocked, &current->blocked, &s);

			recalc_sigpending();
			spin_unlock_irq(&current->sighand->siglock);

			current->state = TASK_INTERRUPTIBLE;
			timeout = schedule_timeout(timeout);

			spin_lock_irq(&current->sighand->siglock);
			sig = dequeue_signal(current, &s, &info);
			current->blocked = current->real_blocked;
			siginitset(&current->real_blocked, 0);
			recalc_sigpending();
		}
	}
	spin_unlock_irq(&current->sighand->siglock);

	if (sig) {
		ret = sig;
		if (uinfo) {
			if (copy_siginfo_to_user32(uinfo, &info))
				ret = -EFAULT;
		}
	}else {
		ret = timeout?-EINTR:-EAGAIN;
	}
	return ret;

}

#ifdef __ARCH_WANT_COMPAT_SYS_TIME

/* compat_time_t is a 32 bit "long" and needs to get converted. */

asmlinkage long compat_sys_time(compat_time_t __user * tloc)
{
	compat_time_t i;
	struct timeval tv;

	do_gettimeofday(&tv);
	i = tv.tv_sec;

	if (tloc) {
		if (put_user(i,tloc))
			i = -EFAULT;
	}
	return i;
}

asmlinkage long compat_sys_stime(compat_time_t __user *tptr)
{
	struct timespec tv;
	int err;

	if (get_user(tv.tv_sec, tptr))
		return -EFAULT;

	tv.tv_nsec = 0;

	err = security_settime(&tv, NULL);
	if (err)
		return err;

	do_settimeofday(&tv);
	return 0;
}

#endif /* __ARCH_WANT_COMPAT_SYS_TIME */

/*
 * linux/kernel/itimer.c
 *
 * Copyright (C) 1992 Darren Senn
 */

/* These are all the functions necessary to implement itimers */

#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/time.h>

#include <asm/uaccess.h>

/*
 * change timeval to jiffies, trying to avoid the
 * most obvious overflows..
 *
 * The tv_*sec values are signed, but nothing seems to
 * indicate whether we really should use them as signed values
 * when doing itimers. POSIX doesn't mention this (but if
 * alarm() uses itimers without checking, we have to use unsigned
 * arithmetic).
 */
static unsigned long tvtojiffies(struct timeval *value)
{
	unsigned long sec = (unsigned) value->tv_sec;
	unsigned long usec = (unsigned) value->tv_usec;

	if (sec > (ULONG_MAX / HZ))
		return ULONG_MAX;
	usec += 1000000 / HZ - 1;
	usec /= 1000000 / HZ;
	return HZ*sec+usec;
}

static void jiffiestotv(unsigned long jiffies, struct timeval *value)
{
	value->tv_usec = (jiffies % HZ) * (1000000 / HZ);
	value->tv_sec = jiffies / HZ;
}

int do_getitimer(int which, struct itimerval *value)
{
	register unsigned long val, interval;

	switch (which) {
	case ITIMER_REAL:
		interval = current->it_real_incr;
		val = 0;
		start_bh_atomic();
		if (timer_pending(&current->real_timer)) {
			val = current->real_timer.expires - jiffies;

			/* look out for negative/zero itimer.. */
			if ((long) val <= 0)
				val = 1;
		}
		end_bh_atomic();
		break;
	case ITIMER_VIRTUAL:
		val = current->it_virt_value;
		interval = current->it_virt_incr;
		break;
	case ITIMER_PROF:
		val = current->it_prof_value;
		interval = current->it_prof_incr;
		break;
	default:
		return(-EINVAL);
	}
	jiffiestotv(val, &value->it_value);
	jiffiestotv(interval, &value->it_interval);
	return 0;
}

/* SMP: Only we modify our itimer values. */
asmlinkage int sys_getitimer(int which, struct itimerval *value)
{
	int error = -EFAULT;
	struct itimerval get_buffer;

	if (value) {
		error = do_getitimer(which, &get_buffer);
		if (!error &&
		    copy_to_user(value, &get_buffer, sizeof(get_buffer)))
			error = -EFAULT;
	}
	return error;
}

void it_real_fn(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;
	unsigned long interval;

	send_sig(SIGALRM, p, 1);
	interval = p->it_real_incr;
	if (interval) {
		if (interval > (unsigned long) LONG_MAX)
			interval = LONG_MAX;
		p->real_timer.expires = jiffies + interval;
		add_timer(&p->real_timer);
	}
}

int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	register unsigned long i, j;
	int k;

	i = tvtojiffies(&value->it_interval);
	j = tvtojiffies(&value->it_value);
	if (ovalue && (k = do_getitimer(which, ovalue)) < 0)
		return k;
	switch (which) {
		case ITIMER_REAL:
			start_bh_atomic();
			del_timer(&current->real_timer);
			end_bh_atomic();
			current->it_real_value = j;
			current->it_real_incr = i;
			if (!j)
				break;
			if (j > (unsigned long) LONG_MAX)
				j = LONG_MAX;
			i = j + jiffies;
			current->real_timer.expires = i;
			add_timer(&current->real_timer);
			break;
		case ITIMER_VIRTUAL:
			if (j)
				j++;
			current->it_virt_value = j;
			current->it_virt_incr = i;
			break;
		case ITIMER_PROF:
			if (j)
				j++;
			current->it_prof_value = j;
			current->it_prof_incr = i;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

/* SMP: Again, only we play with our itimers, and signals are SMP safe
 *      now so that is not an issue at all anymore.
 */
asmlinkage int sys_setitimer(int which, struct itimerval *value,
			     struct itimerval *ovalue)
{
	struct itimerval set_buffer, get_buffer;
	int error;

	if (value) {
		if(verify_area(VERIFY_READ, value, sizeof(*value)))
			return -EFAULT;
		if(copy_from_user(&set_buffer, value, sizeof(set_buffer)))
			return -EFAULT;
	} else
		memset((char *) &set_buffer, 0, sizeof(set_buffer));

	error = do_setitimer(which, &set_buffer, ovalue ? &get_buffer : 0);
	if (error || !ovalue)
		return error;

	if (copy_to_user(ovalue, &get_buffer, sizeof(get_buffer)))
		return -EFAULT;
	return 0;
}

/* PRECONDITION:
 * timr->it_lock must be locked
 */
static void timer_notify_task(struct k_itimer *timr)
{
	struct siginfo info;
	int ret;

	if (timr->it_signal.sigev_notify == SIGEV_SIGNAL) {

		/* Send signal to the process that owns this timer. */
		info.si_signo = timr->it_signal.sigev_signo;
		info.si_errno = 0;
		info.si_code = SI_TIMER;
		/* TODO: if someone has better ideas what to put in 
		 * the next two fields...
		 * si_timer1 is currently used in signal.c to check
		 * whether a signal from this timer is already in the signal
		 * queue.
		 */
		info.si_timer1 = timr->it_id;
		info.si_timer2 = 0;
		info.si_value = timr->it_signal.sigev_value;
		ret = send_sig_info(info.si_signo, &info, timr->it_process);
		switch (ret) {
		case 0:		/* all's well */
			timr->it_overrun = 0;
			break;
		case 1:	/* signal from this timer was already in the queue */
			timr->it_overrun++;
			break;
		default:
			printk(KERN_WARNING "sending signal failed: %d\n", ret);
			break;
		}
	}
}

/* This function gets called when a POSIX.1b interval timer expires. */
static void posix_timer_fn(unsigned long __data)
{  
	struct k_itimer *timr = (struct k_itimer *)__data;
	unsigned long interval;

	spin_lock(&timr->it_lock);
	
	timer_notify_task(timr);

	/* Set up the timer for the next interval (if there is one) */
	if ((interval = timr->it_incr) == 0) goto out;
		
	if (interval > (unsigned long) LONG_MAX)
		interval = LONG_MAX;
	timr->it_timer.expires = jiffies + interval;
	add_timer(&timr->it_timer);
out:
	spin_unlock(&timr->it_lock);
}

/* Find the first available slot for the new timer. */
static int timer_find_slot(struct itimer_struct *timers)
{
	int i;

	for (i = 0; i < MAX_ITIMERS; i++) {
		if (timers->itimer[i] == NULL) return i;
	}
	return -1;
}

static int good_sigevent(const struct sigevent *sigev)
{
	switch (sigev->sigev_notify) {
	case SIGEV_NONE:
		break;
	case SIGEV_SIGNAL:
		if ((sigev->sigev_signo <= 0) ||
		    (sigev->sigev_signo > SIGRTMAX))
			return 0;
		break;
	default:
		return 0;
	}
	return 1;
}

/* Create a POSIX.1b interval timer. */

asmlinkage int sys_timer_create(clockid_t which_clock,
				struct sigevent *timer_event_spec,
				timer_t *created_timer_id)
{
	int error = 0;
	struct k_itimer *new_timer = NULL;
	struct itimer_struct *timers = current->posix_timers;
	int new_timer_id;
 
	/* Right now, we only support CLOCK_REALTIME for timers. */
	if (which_clock != CLOCK_REALTIME) return -EINVAL;

	new_timer = (struct k_itimer *)kmalloc(sizeof(*new_timer), GFP_KERNEL);
	if (new_timer == NULL) return -EAGAIN;

	spin_lock_init(&new_timer->it_lock);
	new_timer->it_clock = which_clock;
	new_timer->it_incr = 0;
	new_timer->it_overrun = 0;

	if (timer_event_spec) {
		if (copy_from_user(&new_timer->it_signal, timer_event_spec,
				   sizeof(new_timer->it_signal))) {
			error = -EFAULT;
			goto out;
		}
		if (!good_sigevent(&new_timer->it_signal)) {
			error = -EINVAL;
			goto out;
		}
	}
	else {
		new_timer->it_signal.sigev_notify = SIGEV_SIGNAL;
		new_timer->it_signal.sigev_signo = SIGALRM;
	}

	new_timer->it_interval.tv_sec = 0;
	new_timer->it_interval.tv_nsec = 0;
	new_timer->it_process = current;
	new_timer->it_timer.next = NULL;
	new_timer->it_timer.prev = NULL;
	new_timer->it_timer.expires = 0;
	new_timer->it_timer.data = (unsigned long)new_timer;
	new_timer->it_timer.function = posix_timer_fn;

	spin_lock(&timers->its_lock);

	new_timer_id = timer_find_slot(timers);
	if (new_timer_id == -1) {
		error = -EAGAIN;
		goto out;
	}
	new_timer->it_id = new_timer_id;
	timers->itimer[new_timer_id] = new_timer;
	if (timer_event_spec == NULL) {
		new_timer->it_signal.sigev_value.sival_int = new_timer_id;
	}

	if (copy_to_user(created_timer_id, &new_timer_id, sizeof(new_timer_id))) {
		error = -EFAULT;
		timers->itimer[new_timer_id] = NULL;
	}

	spin_unlock(&timers->its_lock);
out:
	if (error) {
		kfree(new_timer);
	}
	return error;
}


/* good_timespec
 *
 * This function checks the elements of a timespec structure.
 *
 * Arguments:
 * ts       : Pointer to the timespec structure to check
 *
 * Return value:
 * If a NULL pointer was passed in, or the tv_nsec field was less than 0 or
 * greater than NSEC_PER_SEC, or the tv_sec field was less than 0, this
 * function returns 0. Otherwise it returns 1.
 */

static int good_timespec(const struct timespec *ts)
{
	if (ts == NULL) return 0;
	if (ts->tv_sec < 0) return 0;
	if ((ts->tv_nsec < 0) || (ts->tv_nsec >= NSEC_PER_SEC)) return 0;
	return 1;
}

static inline struct k_itimer* lock_timer(struct task_struct *tsk, timer_t timer_id)
{
	struct k_itimer *timr;

	if ((timer_id < 0) || (timer_id >= MAX_ITIMERS)) return NULL;
	spin_lock(&tsk->posix_timers->its_lock);
	timr = tsk->posix_timers->itimer[timer_id];
	if (timr) spin_lock(&timr->it_lock);
	spin_unlock(&tsk->posix_timers->its_lock);
	return timr;
}

static inline void unlock_timer(struct k_itimer *timr)
{
	spin_unlock(&timr->it_lock);
}

/* Get the time remaining on a POSIX.1b interval timer. */
static void do_timer_gettime(struct k_itimer *timr,
			     struct itimerspec *cur_setting)
{
	unsigned long expires = timr->it_timer.expires;

	if (expires) expires -= jiffies;
	
	jiffies_to_timespec(expires, &cur_setting->it_value);
	cur_setting->it_interval = timr->it_interval;
}

/* Get the time remaining on a POSIX.1b interval timer. */
asmlinkage int sys_timer_gettime(timer_t timer_id, struct itimerspec *setting)
{
	struct k_itimer *timr;
	struct itimerspec cur_setting;

	timr = lock_timer(current, timer_id);
	if (!timr) return -EINVAL;

	do_timer_gettime(timr, &cur_setting);

	unlock_timer(timr);
	
	copy_to_user_ret(setting, &cur_setting, sizeof(cur_setting), -EFAULT);

	return 0;
}

/* Get the number of overruns of a POSIX.1b interval timer */
asmlinkage int sys_timer_getoverrun(timer_t timer_id)
{
	struct k_itimer *timr;
	int overrun;

	timr = lock_timer(current, timer_id);
	if (!timr) return -EINVAL;

	overrun = timr->it_overrun;
	
	unlock_timer(timr);

	return overrun;
}

static void timer_value_abs_to_rel(struct timespec *val)
{
	struct timeval tv;
	struct timespec ts;

	do_gettimeofday(&tv);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * NSEC_PER_USEC;

	/* check whether the time lies in the past */
	if ((val->tv_sec < ts.tv_sec) || 
	    ((val->tv_sec == ts.tv_sec) &&
	     (val->tv_nsec <= ts.tv_nsec))) {
		/* expire immediately */
		val->tv_sec = 0;
		val->tv_nsec = 0;
	}
	else {
		val->tv_sec -= ts.tv_sec;
		val->tv_nsec -= ts.tv_nsec;
		if (val->tv_nsec < 0) {
			val->tv_nsec += NSEC_PER_SEC;
			val->tv_sec--;
		}
	}
}

/* Set a POSIX.1b interval timer. */
static void do_timer_settime(struct k_itimer *timr, int flags,
			     struct itimerspec *new_setting,
			     struct itimerspec *old_setting)
{
	/* disable the timer */
	start_bh_atomic();
	del_timer(&timr->it_timer);
	end_bh_atomic();

	if (old_setting) {
		do_timer_gettime(timr, old_setting);
	}

	/* switch off the timer when it_value is zero */
	if ((new_setting->it_value.tv_sec == 0) &&
	    (new_setting->it_value.tv_nsec == 0)) {
		timr->it_incr = 0;
		timr->it_timer.expires = 0;
		timr->it_interval.tv_sec = 0;
		timr->it_interval.tv_nsec = 0;
		return;
	}

	timr->it_incr = timespec_to_jiffies(&new_setting->it_interval);
	/* save the interval rounded to jiffies */
	jiffies_to_timespec(timr->it_incr, &timr->it_interval);

	if (flags & TIMER_ABSTIME) {
		timer_value_abs_to_rel(&new_setting->it_value);
	}

	timr->it_timer.expires = timespec_to_jiffies(&new_setting->it_value) + jiffies;

	/*
	 * For some reason the timer does not fire immediately if expires is
	 * equal to jiffies, so the timer callback function is called directly.
	 */
	if (timr->it_timer.expires == jiffies) {
		posix_timer_fn((unsigned long)timr);
	}
	else {
		add_timer(&timr->it_timer);
	}
}


/* Set a POSIX.1b interval timer */
asmlinkage int sys_timer_settime(timer_t timer_id, int flags,
				 const struct itimerspec *new_setting,
				 struct itimerspec *old_setting)
{
	struct k_itimer *timr;
	struct itimerspec new_spec, old_spec;
	int error = 0;

	timr = lock_timer(current, timer_id);
	if (!timr) return -EINVAL;

	if (new_setting == NULL) {
		error = -EINVAL;
		goto out;
	}

	if (copy_from_user(&new_spec, new_setting, sizeof(new_spec))) {
		error = -EFAULT;
		goto out;
	}

	if ((!good_timespec(&new_spec.it_interval)) ||
	    (!good_timespec(&new_spec.it_value))) {
		error = -EINVAL;
		goto out;
	}

	do_timer_settime(timr, flags, &new_spec,
			 old_setting ? &old_spec : NULL);

	if (old_setting) {
		if (copy_to_user(old_setting, &old_spec, sizeof(old_spec))) {
			error = -EFAULT;
		}
	}

out:
	unlock_timer(timr);
	return error;
}


/* Delete a POSIX.1b interval timer. */
asmlinkage int sys_timer_delete(timer_t timer_id)
{
	struct k_itimer *timr;

	timr = lock_timer(current, timer_id);
	if (!timr) return -EINVAL;

	start_bh_atomic();
	del_timer(&timr->it_timer);
	end_bh_atomic();

	spin_lock(&current->posix_timers->its_lock);

	kfree(timr);
	current->posix_timers->itimer[timer_id] = NULL;

	spin_unlock(&current->posix_timers->its_lock);

	return 0;
}

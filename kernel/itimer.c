/*
 * linux/kernel/itimer.c
 *
 * Copyright (C) 1992 Darren Senn
 */

/* These are all the functions necessary to implement itimers */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/mm.h>

#include <asm/segment.h>

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
	return;
}

static int _getitimer(int which, struct itimerval *value)
{
	register unsigned long val, interval;

	switch (which) {
	case ITIMER_REAL:
		interval = current->it_real_incr;
		val = 0;
		if (del_timer(&current->real_timer)) {
			unsigned long now = jiffies;
			val = current->real_timer.expires;
			add_timer(&current->real_timer);
			/* look out for negative/zero itimer.. */
			if (val <= now)
				val = now+1;
			val -= now;
		}
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

asmlinkage int sys_getitimer(int which, struct itimerval *value)
{
	int error;
	struct itimerval get_buffer;

	if (!value)
		return -EFAULT;
	error = _getitimer(which, &get_buffer);
	if (error)
		return error;
	error = verify_area(VERIFY_WRITE, value, sizeof(struct itimerval));
	if (error)
		return error;
	memcpy_tofs(value, &get_buffer, sizeof(get_buffer));
	return 0;
}

void it_real_fn(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;
	unsigned long interval;

	send_sig(SIGALRM, p, 1);
	interval = p->it_real_incr;
	if (interval) {
		unsigned long timeout = jiffies + interval;
		/* check for overflow */
		if (timeout < interval)
			timeout = ULONG_MAX;
		p->real_timer.expires = timeout;
		add_timer(&p->real_timer);
	}
}

int _setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	register unsigned long i, j;
	int k;

	i = tvtojiffies(&value->it_interval);
	j = tvtojiffies(&value->it_value);
	if (ovalue && (k = _getitimer(which, ovalue)) < 0)
		return k;
	switch (which) {
		case ITIMER_REAL:
			del_timer(&current->real_timer);
			current->it_real_value = j;
			current->it_real_incr = i;
			if (!j)
				break;
			i = j + jiffies;
			/* check for overflow.. */
			if (i < j)
				i = ULONG_MAX;
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

asmlinkage int sys_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	int error;
	struct itimerval set_buffer, get_buffer;

	if (value) {
		error = verify_area(VERIFY_READ, value, sizeof(*value));
		if (error)
			return error;
		memcpy_fromfs(&set_buffer, value, sizeof(set_buffer));
	} else
		memset((char *) &set_buffer, 0, sizeof(set_buffer));

	if (ovalue) {
		error = verify_area(VERIFY_WRITE, ovalue, sizeof(struct itimerval));
		if (error)
			return error;
	}

	error = _setitimer(which, &set_buffer, ovalue ? &get_buffer : 0);
	if (error || !ovalue)
		return error;

	memcpy_tofs(ovalue, &get_buffer, sizeof(get_buffer));
	return error;
}

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

static unsigned long tvtojiffies(struct timeval *value)
{
	return((unsigned long )value->tv_sec * HZ +
		(unsigned long )(value->tv_usec + (1000000 / HZ - 1)) /
		(1000000 / HZ));
}

static void jiffiestotv(unsigned long jiffies, struct timeval *value)
{
	value->tv_usec = (jiffies % HZ) * (1000000 / HZ);
	value->tv_sec = jiffies / HZ;
	return;
}

static int _getitimer(int which, struct itimerval *value)
{
	register long val, interval;

	switch (which) {
	case ITIMER_REAL:
		interval = current->it_real_incr;
		val = 0;
		if (del_timer(&current->real_timer)) {
			val = current->real_timer.expires;
			add_timer(&current->real_timer);
			if (val <= 0)
				val = interval;
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

	send_sig(SIGALRM, p, 1);
	if (p->it_real_incr) {
		p->real_timer.expires = p->it_real_incr;
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
			if (j) {
				current->real_timer.expires = j;
				add_timer(&current->real_timer);
			}
			current->it_real_value = j;
			current->it_real_incr = i;
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

/*
 * joystick.c  Version 1.2
 *
 * Copyright (c) 1996-1998 Vojtech Pavlik
 */

/*
 * This is the main joystick driver for Linux. It doesn't support any
 * devices directly, rather is lets you use sub-modules to do that job. See
 * Documentation/joystick.txt for more info.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/module.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
#include <asm/spinlock.h>
#include <linux/poll.h>
#endif

/*
 * Configurable parameters.
 */

#define JS_REFRESH_TIME		HZ/50	/* Time between two reads of joysticks (20ms) */

/*
 * Buffer macros.
 */

#define ROT(A,B,C)	((((A)<(C))&&(((B)>(A))&&((B)<(C))))||(((A)>(C))&&(((B)>(A))||((B)<(C)))))
#define GOF(X)		(((X)==JS_BUFF_SIZE-1)?0:(X)+1)
#define GOB(X)		((X)?(X)-1:JS_BUFF_SIZE-1)
#define DIFF(X,Y)	((X)>(Y)?(X)-(Y):(Y)-(X))

/*
 * Global variables.
 */

static struct JS_DATA_SAVE_TYPE js_comp_glue;
static struct js_port  *js_port  = NULL;
static struct js_dev   *js_dev   = NULL;
static struct timer_list js_timer;
spinlock_t js_lock = SPIN_LOCK_UNLOCKED;
static int js_use_count = 0;

/*
 * Exported variables.
 */

unsigned int js_time_speed = 0;
js_time_func js_get_time;
js_delta_func js_delta;

unsigned int js_time_speed_a = 0;
js_time_func js_get_time_a;
js_delta_func js_delta_a;

/*
 * Module info.
 */

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_SUPPORTED_DEVICE("js");

/*
 * js_get_time_*() are different functions to get current time.
 * js_delta_*() are functions to compute time difference.
 */

#ifdef __i386__

static unsigned int js_get_time_rdtsc(void)
{
	unsigned int x;
	__asm__ __volatile__ ( "rdtsc" : "=A" (x) );
	return x;
}

static unsigned int js_get_time_pit(void)
{
	unsigned long flags;
	unsigned int x;

	__save_flags(flags);
	__cli();
	outb(0, 0x43);
	x = inb(0x40);
	x |= inb(0x40) << 8;
	__restore_flags(flags);

	return x;
}

static int js_delta_pit(unsigned int x, unsigned int y)
{
	return y - x + ( y < x ? 1193180L / HZ : 0 );
}

static unsigned int js_get_time_counter(void)
{
	static int time_counter = 0;
	return time_counter++;
}

#else
#ifdef __alpha__

static unsigned int js_get_time_rpcc(void)
{
	unsigned int x;
	__asm__ __volatile__ ( "rpcc %0" : "=r" (x) );
	return x;
}

#else

#ifndef MODULE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
static unsigned int js_get_time_system(void)
{
	static struct timeval js_tv;
	get_fast_time(&js_tv);
	return js_tv.tv_sec * 1000000L + js_tv.tv_usec;
}
#endif
#endif

#endif
#endif

static int js_delta_normal(unsigned int x, unsigned int y)
{
	return x - y;
}

/*
 * js_calibrate_time() calibrates a given timer.
 */

static int __init js_calibrate_time(js_time_func get_time, js_delta_func delta)
{
	unsigned int t1, t2, t3;
	unsigned long flags;

	__save_flags(flags);
	__cli();
	t1 = get_time();
	udelay(1000);
	t2 = get_time();
	t3 = get_time();
	__restore_flags(flags);

	return delta(t2, t1) - delta(t3, t2);
}

/*
 * js_calibrate_time_counter() calibrates the counter timer, which can't
 * be calibrated using the above function.
 */

#ifdef __i386__

static int __init js_calibrate_time_counter(void)
{
	unsigned int i, j, t1, t2, t3;

	j = jiffies; do { inb(0x201); t1 = js_get_time_counter(); } while (j == jiffies);
	j = jiffies; do { inb(0x201); t2 = js_get_time_counter(); } while (j == jiffies);

	j = (t2 - t1) * HZ / 1000;

	t1 = js_get_time_pit();
	for (i = 0; i < 1000; i++) {
		inb(0x201);
		js_get_time_counter();
	}
	t2 = js_get_time_pit();
	t3 = js_get_time_pit();

	i = 1193180L / (js_delta_pit(t2, t1) - js_delta_pit(t3, t2));

	if (DIFF(i,j) > 5)
		printk(KERN_WARNING "js: Counter timer calibration unsure,"
			" pass1 (0.%d MHz) and pass2 (0.%d MHz) differ.\n", j, i);

	return (i + j) >> 1;
}

#endif

/*
 * js_setup_time chooses the best available timers
 * on the system and calibrates them.
 */

static int __init js_setup_time(void)
{
	int t;
	char *name, *name_a;

	name = "";
	name_a = "";
	js_time_speed = 0;
	js_time_speed_a = 0;

#ifdef __i386__

	t = js_calibrate_time(js_get_time_pit, js_delta_pit);

	if (DIFF(t, 1193) > 5)
		printk(KERN_WARNING "js: Measured PIT speed is %d.%03d MHz, but should be 1.193 MHz.\n"
		       KERN_WARNING "js: This is probably caused by wrong BogoMIPS value. It is: %ld, should be: %ld.\n",
			t / 1000, t % 1000, loops_per_sec / 500000, loops_per_sec / (t * 500000 / 1193));

	if (JS_HAS_RDTSC && (t = js_calibrate_time(js_get_time_rdtsc, js_delta_normal)) > 0) {

		js_time_speed_a = t;
		js_get_time_a = js_get_time_rdtsc;
		js_delta_a = js_delta_normal;
		js_time_speed = t;
		js_get_time = js_get_time_rdtsc;
		js_delta = js_delta_normal;
		name = "RDTSC";

	} else {

		js_time_speed_a = t;
		js_get_time_a = js_get_time_pit;
		js_delta_a = js_delta_pit;
		name_a = "PIT";

		t = js_calibrate_time_counter();

		js_time_speed = t;
		js_get_time = js_get_time_counter;
		js_delta = js_delta_normal;
		name = "counter";

	}

#else
#ifdef __alpha__

	t = js_calibrate_time(js_get_time_rpcc, js_delta_normal);

	js_time_speed_a = t;
	js_get_time_a = js_get_time_rpcc;
	js_delta_a = js_delta_normal;
	js_time_speed = t;
	js_get_time = js_get_time_rpcc;
	js_delta = js_delta_normal;
	name = "RPCC";

#else

#ifndef MODULE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
	t = js_calibrate_time(js_get_time_system, js_delta_normal);

	js_time_speed_a = t;
	js_get_time_a = js_get_time_system;
	js_delta_a = js_delta_normal;
	js_time_speed = t;
	js_get_time = js_get_time_system;
	js_delta = js_delta_normal;
	name = "system";
#endif
#endif

#endif
#endif

	printk(KERN_INFO "js: Version %d.%d.%d ",
		JS_VERSION >> 16 & 0xff, JS_VERSION >> 8 & 0xff, JS_VERSION & 0xff);

	if (js_time_speed_a <= 0 || js_time_speed <= 0) {
		printk("\n");
		return -1;
	}

	printk("using ");

	if (js_time_speed > 10000) {
		t = js_time_speed / 1000 + (js_time_speed % 1000 >= 500);
		printk("%d MHz ", t);
	} else {
		t = js_time_speed / 10 + (js_time_speed % 10 >= 5);
		printk("%d.%02d MHz ", t / 100, t % 100);
	}

	if (js_get_time_a != js_get_time) {
		t = js_time_speed_a / 10 + (js_time_speed_a % 10 >= 5);
		printk("%s timer and %d.%02d MHz %s timer.\n",
			name, t / 100, t % 100, name_a);
	} else {
		printk("%s timer.\n", name);
	}

	return 0;
}


/*
 * js_correct() performs correction of raw joystick data.
 */

static int js_correct(int value, struct js_corr *corr)
{
	switch (corr->type) {
		case JS_CORR_NONE:
			break;
		case JS_CORR_BROKEN:
			value = value > corr->coef[0] ? (value < corr->coef[1] ? 0 :
				((corr->coef[3] * (value - corr->coef[1])) >> 14)) :
				((corr->coef[2] * (value - corr->coef[0])) >> 14);
			break;

		default:
			return 0;
	}

	if (value < -32767) return -32767;
	if (value >  32767) return  32767;

	return value;
}

/*
 * js_button() returns value of button number i.
 */

static inline int js_button(int *buttons, int i)
{
	return (buttons[i >> 5] >> (i & 0x1f)) & 1;
}


/*
 * js_add_event() adds an event to the buffer. This requires additional
 * queue post-processing done by js_sync_buff.
 */

static void js_add_event(struct js_dev *jd, __u32 time, __u8 type, __u8 number, __s16 value)
{
	jd->buff[jd->ahead].time = time;
	jd->buff[jd->ahead].type = type;
	jd->buff[jd->ahead].number = number;
	jd->buff[jd->ahead].value = value;
	if (++jd->ahead == JS_BUFF_SIZE) jd->ahead = 0;
}

/*
 * js_flush_data() does the same as js_process_data, except for that it doesn't
 * generate any events - it just copies the data from new to cur.
 */

static void js_flush_data(struct js_dev *jd)
{
	int i;

	for (i = 0; i < ((jd->num_buttons - 1) >> 5) + 1; i++)
		jd->cur.buttons[i] = jd->new.buttons[i];
	for (i = 0; i < jd->num_axes; i++)
		jd->cur.axes[i] = jd->new.axes[i];
}

/*
 * js_process_data() finds changes in button states and axis positions and adds
 * them as events to the buffer.
 */

static void js_process_data(struct js_dev *jd)
{
	int i, t;

	for (i = 0; i < jd->num_buttons; i++)
	if ((t = js_button(jd->new.buttons, i)) != js_button(jd->cur.buttons, i)) {
		js_add_event(jd, jiffies, JS_EVENT_BUTTON, i, t);
		jd->cur.buttons[i >> 5] ^= (1 << (i & 0x1f));
	}

	for (i = 0; i < jd->num_axes; i++) {
		t = js_correct(jd->new.axes[i], &jd->corr[i]);
		if (((jd->corr[i].prec == -1) && t) ||
			((DIFF(jd->new.axes[i], jd->cur.axes[i]) > jd->corr[i].prec) &&
			(t != js_correct(jd->cur.axes[i], &jd->corr[i])))) {
			js_add_event(jd, jiffies, JS_EVENT_AXIS, i, t);
			jd->cur.axes[i] = jd->new.axes[i];
		}
	}
}

/*
 * js_sync_buff() checks for all overflows caused by recent additions to the buffer.
 * These happen only if some process is reading the data too slowly. It
 * wakes up any process waiting for data.
 */

static void js_sync_buff(struct js_dev *jd)
{
	struct js_list *curl = jd->list;

	if (jd->bhead != jd->ahead) {
		if(ROT(jd->bhead, jd->tail, jd->ahead) || (jd->tail == jd->bhead)) {
			while (curl) {
				if (ROT(jd->bhead, curl->tail, jd->ahead) || (curl->tail == jd->bhead)) {
					curl->tail = jd->ahead;
					curl->startup = 0;
				}
				curl = curl->next;
			}
			jd->tail = jd->ahead;
		}
		jd->bhead = jd->ahead;
		wake_up_interruptible(&jd->wait);
	}
}

/*
 * js_do_timer() acts as an interrupt replacement. It reads the data
 * from all ports and then generates events for all devices.
 */

static void js_do_timer(unsigned long data)
{
	struct js_port *curp = js_port;
	struct js_dev *curd = js_dev;
	unsigned long flags;

	while (curp != NULL) {
		curp->read(curp->info, curp->axes, curp->buttons);
		curp = curp->next;
	}

	spin_lock_irqsave(&js_lock, flags);

	while (curd != NULL) {
		if (data) {
			js_process_data(curd);
			js_sync_buff(curd);
		} else {
			js_flush_data(curd);
		}
		curd = curd->next;
	}

	spin_unlock_irqrestore(&js_lock, flags);

	js_timer.expires = jiffies + JS_REFRESH_TIME;
	add_timer(&js_timer);
}

/*
 * js_read() copies one or more entries from jsd[].buff to user
 * space.
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
static ssize_t js_read(struct file *file, char *buf, size_t count, loff_t *ppos)
#else
static int js_read(struct inode *inode, struct file *file, char *buf, int count)
#endif
{
	struct wait_queue wait = { current, NULL };
	struct js_event *buff = (void *) buf;
	struct js_list *curl;
	struct js_dev *jd;
	unsigned long blocks = count / sizeof(struct js_event);
	int written = 0;
	int new_tail, orig_tail;
	int retval = 0;
	unsigned long flags;

	curl = file->private_data;
	jd = curl->dev;
	orig_tail = curl->tail;

/*
 * Check user data.
 */

	if (!blocks)
		return -EINVAL;

/*
 * Lock it.
 */

	spin_lock_irqsave(&js_lock, flags);

/*
 * Handle (non)blocking i/o.
 */
	if (count != sizeof(struct JS_DATA_TYPE)) {

		if (GOF(curl->tail) == jd->bhead && curl->startup == jd->num_axes + jd->num_buttons) {

			add_wait_queue(&jd->wait, &wait);
			current->state = TASK_INTERRUPTIBLE;

			while (GOF(curl->tail) == jd->bhead) {

				if (file->f_flags & O_NONBLOCK) {
					retval = -EAGAIN;
					break;
				}
				if (signal_pending(current)) {
					retval = -ERESTARTSYS;
					break;
				}

				spin_unlock_irqrestore(&js_lock, flags);
				schedule();
				spin_lock_irqsave(&js_lock, flags);

			}

			current->state = TASK_RUNNING;
			remove_wait_queue(&jd->wait, &wait);
		}

		if (retval) {
			spin_unlock_irqrestore(&js_lock, flags);
			return retval;
		}

/*
 * Initial state.
 */

		while (curl->startup < jd->num_axes + jd->num_buttons && written < blocks && !retval) {

			struct js_event tmpevent;

			if (curl->startup < jd->num_buttons) {
				tmpevent.type = JS_EVENT_BUTTON | JS_EVENT_INIT;
				tmpevent.value = js_button(jd->cur.buttons, curl->startup);
				tmpevent.number = curl->startup;
			} else {
				tmpevent.type = JS_EVENT_AXIS | JS_EVENT_INIT;
				tmpevent.value = js_correct(jd->cur.axes[curl->startup - jd->num_buttons],
								&jd->corr[curl->startup - jd->num_buttons]);
				tmpevent.number = curl->startup - jd->num_buttons;
			}

			tmpevent.time = jiffies * (1000/HZ);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
			if (copy_to_user(&buff[written], &tmpevent, sizeof(struct js_event)))
				retval = -EFAULT;
#else
			if (!(retval = verify_area(VERIFY_WRITE, &buff[written], sizeof(struct js_event))))
				memcpy_tofs(&buff[written], &tmpevent, sizeof(struct js_event));
#endif

			curl->startup++;
			written++;
		}

/*
 * Buffer data.
 */

		while ((jd->bhead != (new_tail = GOF(curl->tail))) && (written < blocks) && !retval) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
			if (copy_to_user(&buff[written], &jd->buff[new_tail], sizeof(struct js_event)))
				retval = -EFAULT;
			if (put_user((__u32)(jd->buff[new_tail].time * (1000/HZ)), &buff[written].time))
				retval = -EFAULT;
#else
			if (!(retval = verify_area(VERIFY_WRITE, &buff[written], sizeof(struct js_event)))) {
				memcpy_tofs(&buff[written], &jd->buff[new_tail], sizeof(struct js_event));
				put_user((__u32)(jd->buff[new_tail].time * (1000/HZ)), &buff[written].time);
			}
#endif
			curl->tail = new_tail;
			written++;
		}
	}

	else

/*
 * Handle version 0.x compatibility.
 */

	{
		struct JS_DATA_TYPE data;

		data.buttons = jd->new.buttons[0];
		data.x = jd->num_axes < 1 ? 0 :
			((js_correct(jd->new.axes[0], &jd->corr[0]) / 256) + 128) >> js_comp_glue.JS_CORR.x;
		data.y = jd->num_axes < 2 ? 0 :
			((js_correct(jd->new.axes[1], &jd->corr[1]) / 256) + 128) >> js_comp_glue.JS_CORR.y;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
		retval = copy_to_user(buf, &data, sizeof(struct JS_DATA_TYPE)) ? -EFAULT : 0;
#else
		if (!(retval = verify_area(VERIFY_WRITE, buf, sizeof(struct JS_DATA_TYPE)))) {
			memcpy_tofs(buf, &data, sizeof(struct JS_DATA_TYPE));
		}
#endif

		curl->startup = 0;
		curl->tail = GOB(jd->bhead);
		if (!retval) retval = sizeof(struct JS_DATA_TYPE);
	}

/*
 * Check main tail and move it.
 */

	if (orig_tail == jd->tail) {
		new_tail = curl->tail;
		curl = jd->list;
		while (curl != NULL && curl->tail != jd->tail) {
			if (ROT(jd->bhead, new_tail, curl->tail) ||
				(jd->bhead == curl->tail)) new_tail = curl->tail;
			curl = curl->next;
		}
		if (curl == NULL) jd->tail = new_tail;
	}

	spin_unlock_irqrestore(&js_lock, flags);

	return retval ? retval : written * sizeof(struct js_event);
}

/*
 * js_poll() does select() support.
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)

static unsigned int js_poll(struct file *file, poll_table *wait)
{
	struct js_list *curl = file->private_data;
	unsigned long flags;
	int retval = 0;
	poll_wait(file, &curl->dev->wait, wait);
	spin_lock_irqsave(&js_lock, flags);	
	if (GOF(curl->tail) != curl->dev->bhead ||
		curl->startup < curl->dev->num_axes + curl->dev->num_buttons) retval = POLLIN | POLLRDNORM;
	spin_unlock_irqrestore(&js_lock, flags);
	return retval;
}

#else

static int js_select(struct inode *inode, struct file *file, int sel_type, select_table *wait)
{
	struct js_list *curl = file->private_data;
	if (sel_type == SEL_IN) {
		if (GOF(curl->tail) != curl->dev->bhead) return 1;
		select_wait(&curl->dev->wait, wait);
	}
	return 0;
}

#endif

/*
 * js_ioctl handles misc ioctl calls.
 */

static int js_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct js_list *curl;
	struct js_dev *jd;
	int len;

	curl = file->private_data;
	jd = curl->dev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)

	switch (cmd) {

/*
 * 0.x compatibility
 */

		case JS_SET_CAL:
			return copy_from_user(&js_comp_glue.JS_CORR, (struct JS_DATA_TYPE *) arg,
				sizeof(struct JS_DATA_TYPE)) ? -EFAULT : 0;
		case JS_GET_CAL:
			return copy_to_user((struct JS_DATA_TYPE *) arg, &js_comp_glue.JS_CORR,
				sizeof(struct JS_DATA_TYPE)) ? -EFAULT : 0;
		case JS_SET_TIMEOUT:
			return get_user(js_comp_glue.JS_TIMEOUT, (int *) arg);
		case JS_GET_TIMEOUT:
			return put_user(js_comp_glue.JS_TIMEOUT, (int *) arg);
		case JS_SET_TIMELIMIT:
			return get_user(js_comp_glue.JS_TIMELIMIT, (long *) arg);
		case JS_GET_TIMELIMIT:
			return put_user(js_comp_glue.JS_TIMELIMIT, (long *) arg);
		case JS_SET_ALL:
			return copy_from_user(&js_comp_glue, (struct JS_DATA_SAVE_TYPE *) arg,
						sizeof(struct JS_DATA_SAVE_TYPE)) ? -EFAULT : 0;
		case JS_GET_ALL:
			return copy_to_user((struct JS_DATA_SAVE_TYPE *) arg, &js_comp_glue,
						sizeof(struct JS_DATA_SAVE_TYPE)) ? -EFAULT : 0;

/*
 * 1.x ioctl calls
 */

		case JSIOCGVERSION:
			return put_user(JS_VERSION, (__u32 *) arg);
		case JSIOCGAXES:
			return put_user(jd->num_axes, (__u8 *) arg);
		case JSIOCGBUTTONS:
			return put_user(jd->num_buttons, (__u8 *) arg);
		case JSIOCSCORR:
			return copy_from_user(jd->corr, (struct js_corr *) arg,
						sizeof(struct js_corr) * jd->num_axes) ? -EFAULT : 0;
		case JSIOCGCORR:
			return copy_to_user((struct js_corr *) arg, jd->corr,
						sizeof(struct js_corr) * jd->num_axes) ? -EFAULT : 0;
		default:
			if ((cmd & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) == JSIOCGNAME(0)) {
				len = strlen(jd->name) + 1;
				if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
				if (copy_to_user((char *) arg, jd->name, len)) return -EFAULT;
				return len;
			}
	}

#else

	switch (cmd) {

/*
 * 0.x compatibility
 */

		case JS_SET_CAL:
			if (verify_area(VERIFY_READ, (struct JS_DATA_TYPE *) arg,
				sizeof(struct JS_DATA_TYPE))) return -EFAULT;
			memcpy_fromfs(&js_comp_glue.JS_CORR, (struct JS_DATA_SAVE_TYPE *) arg,
				sizeof(struct JS_DATA_TYPE));
			return 0;
		case JS_GET_CAL:
			if (verify_area(VERIFY_WRITE, (struct JS_DATA_TYPE *) arg,
				sizeof(struct JS_DATA_TYPE))) return -EFAULT;
			memcpy_tofs((struct JS_DATA_SAVE_TYPE *) arg, &js_comp_glue.JS_CORR,
				sizeof(struct JS_DATA_TYPE));
			return 0;
		case JS_SET_TIMEOUT:
			if (verify_area(VERIFY_READ, (int *) arg, sizeof(int))) return -EFAULT;
			js_comp_glue.JS_TIMEOUT = get_user((int *) arg);
			return 0;
		case JS_GET_TIMEOUT:
			if (verify_area(VERIFY_WRITE, (int *) arg, sizeof(int))) return -EFAULT;
			put_user(js_comp_glue.JS_TIMEOUT, (int *) arg);
			return 0;
		case JS_SET_TIMELIMIT:
			if (verify_area(VERIFY_READ, (long *) arg, sizeof(long))) return -EFAULT;
			js_comp_glue.JS_TIMELIMIT = get_user((long *) arg);
			return 0;
		case JS_GET_TIMELIMIT:
			if (verify_area(VERIFY_WRITE, (long *) arg, sizeof(long))) return -EFAULT;
			put_user(js_comp_glue.JS_TIMELIMIT, (long *) arg);
			return 0;
		case JS_SET_ALL:
			if (verify_area(VERIFY_READ, (struct JS_DATA_SAVE_TYPE *) arg,
				sizeof(struct JS_DATA_SAVE_TYPE))) return -EFAULT;
			memcpy_fromfs(&js_comp_glue, (struct JS_DATA_SAVE_TYPE *) arg,
				sizeof(struct JS_DATA_SAVE_TYPE));
			return 0;
		case JS_GET_ALL:
			if (verify_area(VERIFY_WRITE, (struct JS_DATA_SAVE_TYPE *) arg,
				sizeof(struct JS_DATA_SAVE_TYPE))) return -EFAULT;
			memcpy_tofs((struct JS_DATA_SAVE_TYPE *) arg, &js_comp_glue,
				sizeof(struct JS_DATA_SAVE_TYPE));
			return 0;

/*
 * 1.x ioctl calls
 */

		case JSIOCGVERSION:
			if (verify_area(VERIFY_WRITE, (__u32 *) arg, sizeof(__u32))) return -EFAULT;
			put_user(JS_VERSION, (__u32 *) arg);
			return 0;
		case JSIOCGAXES:
			if (verify_area(VERIFY_WRITE, (__u8 *) arg, sizeof(__u8))) return -EFAULT;
			put_user(jd->num_axes, (__u8 *) arg);
			return 0;
		case JSIOCGBUTTONS:
			if (verify_area(VERIFY_WRITE, (__u8 *) arg, sizeof(__u8))) return -EFAULT;
			put_user(jd->num_buttons, (__u8 *) arg);
			return 0;
		case JSIOCSCORR:
			if (verify_area(VERIFY_READ, (struct js_corr *) arg,
				sizeof(struct js_corr) * jd->num_axes)) return -EFAULT;
			memcpy_fromfs(jd->corr, (struct js_corr *) arg,
				sizeof(struct js_corr) * jd->num_axes);
			return 0;
		case JSIOCGCORR:
			if (verify_area(VERIFY_WRITE, (struct js_corr *) arg,
				sizeof(struct js_corr) * jd->num_axes)) return -EFAULT;
			memcpy_tofs((struct js_corr *) arg,
				jd->corr, sizeof(struct js_corr) * jd->num_axes);
			return 0;
		default:
			if ((cmd & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) == JSIOCGNAME(0)) {
				len = strlen(jd->name) + 1;
				if (verify_area(VERIFY_WRITE, (char *) arg, len)) return -EFAULT;
				if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
				memcpy_tofs((char *) arg, jd->name, len);
				return len;
			}
	}

#endif

	return -EINVAL;
}

/*
 * js_open() performs necessary initialization and adds
 * an entry to the linked list.
 */

static int js_open(struct inode *inode, struct file *file)
{
	struct js_list *curl, *new;
	struct js_dev *jd = js_dev;
	int i = MINOR(inode->i_rdev);
	unsigned long flags;
	int result; 

	if (MAJOR(inode->i_rdev) != JOYSTICK_MAJOR)
		return -EINVAL;

	spin_lock_irqsave(&js_lock, flags);

	while (i > 0 && jd != NULL) {
		jd = jd->next;
		i--;
	}

	spin_unlock_irqrestore(&js_lock, flags);

	if (jd == NULL) return -ENODEV;

	if ((result = jd->open(jd))) return result;

	MOD_INC_USE_COUNT;
	if (!js_use_count++) js_do_timer(0);

	if ((new = kmalloc(sizeof(struct js_list), GFP_KERNEL)) != NULL) {

		spin_lock_irqsave(&js_lock, flags);

		curl = jd->list;

		jd->list = new;
		jd->list->next = curl;
		jd->list->dev = jd;
		jd->list->startup = 0;
		jd->list->tail = GOB(jd->bhead);
		file->private_data = jd->list;

		spin_unlock_irqrestore(&js_lock, flags);

	} else {
		result = -ENOMEM;
	}

	return result;
}

/*
 * js_release() removes an entry from list and deallocates memory
 * used by it.
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
static int js_release(struct inode *inode, struct file *file)
#else
static void js_release(struct inode *inode, struct file *file)
#endif
{
	struct js_list *curl = file->private_data;
	struct js_dev *jd = curl->dev;
	struct js_list **curp = &jd->list;
	int new_tail;
	unsigned long flags;

	spin_lock_irqsave(&js_lock, flags);

	while (*curp && (*curp != curl)) curp = &((*curp)->next);
	*curp = (*curp)->next;

	if (jd->list != NULL)
	if (curl->tail == jd->tail) {
		curl = jd->list;
		new_tail = curl->tail;
		while (curl != NULL && curl->tail != jd->tail) {
			if (ROT(jd->bhead, new_tail, curl->tail) ||
			       (jd->bhead == curl->tail)) new_tail = curl->tail;
			curl = curl->next;
		}
		if (!curl) jd->tail = new_tail;
	}

	spin_unlock_irqrestore(&js_lock, flags);

	kfree(file->private_data);

	if (!--js_use_count) del_timer(&js_timer);
	MOD_DEC_USE_COUNT;

	jd->close(jd);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
	return 0;
#endif
}

/*
 * js_dump_mem() dumps all data structures in memory.
 * It's used for debugging only.
 */

#if 0
static void js_dump_mem(void)
{

	struct js_port *curp = js_port;
	struct js_dev *curd = js_dev;
	int i;

	printk(",--- Dumping Devices:\n");
	printk("| js_dev = %x\n", (int) js_dev);

	while (curd != NULL) {
		printk("|  %s-device %x, next %x axes %d, buttons %d, port %x - %#x\n",
			curd->next ? "|":"`",
			(int) curd, (int) curd->next, curd->num_axes, curd->num_buttons, (int) curd->port, curd->port->io);
		curd = curd->next;
	}

	printk(">--- Dumping ports:\n");
	printk("| js_port = %x\n", (int) js_port);

	while (curp != NULL) {
		printk("|  %s-port %x, next %x, io %#x, devices %d\n",
			curp->next ? "|":"`",
			(int) curp, (int) curp->next, curp->io, curp->ndevs);
		for (i = 0; i < curp->ndevs; i++) {
			curd = curp->devs[i];
			if (curd)
			printk("|  %s %s-device %x, next %x axes %d, buttons %d, port %x\n",
				curp->next ? "|":" ", (i < curp->ndevs-1) ? "|":"`",
				(int) curd, (int) curd->next, curd->num_axes, curd->num_buttons, (int) curd->port);
			else
			printk("|  %s %s-device %x, not there\n",
				curp->next ? "|":" ", (i < curp->ndevs-1) ? "|":"`", (int) curd);

		}
		curp = curp->next;
	}

	printk("`--- Done\n");
}
#endif


struct js_port *js_register_port(struct js_port *port,
				void *info, int devs, int infos, js_read_func read)
{
	struct js_port **ptrp = &js_port;
	struct js_port *curp;
	void *all;
	int i;
	unsigned long flags;

	if ((all = kmalloc(sizeof(struct js_port) + 4 * devs * sizeof(void*) + infos, GFP_KERNEL)) == NULL)
		return NULL;

	curp = all;

	curp->next = NULL;
	curp->prev = port;
	curp->read = read;
	curp->ndevs = devs;

	curp->devs = all += sizeof(struct js_port);
	for (i = 0; i < devs; i++) curp->devs[i] = NULL;

	curp->axes = all += devs * sizeof(void*);
	curp->buttons = (void*) all += devs * sizeof(void*);
	curp->corr = all += devs * sizeof(void*);

	if (infos) {
		curp->info = all += devs * sizeof(void*); 
		memcpy(curp->info, info, infos);
	} else {
		curp->info = NULL;
	}

	spin_lock_irqsave(&js_lock, flags);

	while (*ptrp != NULL) ptrp=&((*ptrp)->next);
	*ptrp = curp;

	spin_unlock_irqrestore(&js_lock, flags);

	return curp;
}

struct js_port *js_unregister_port(struct js_port *port)
{
	struct js_port **curp = &js_port;
	struct js_port *prev;
	unsigned long flags;

	spin_lock_irqsave(&js_lock, flags);

	while (*curp != NULL && (*curp != port)) curp = &((*curp)->next);
	*curp = (*curp)->next;

	spin_unlock_irqrestore(&js_lock, flags);

	prev = port->prev;
	kfree(port);

	return prev;
}

int js_register_device(struct js_port *port, int number, int axes, int buttons, char *name,
					js_ops_func open, js_ops_func close)
{
	struct js_dev **ptrd = &js_dev;
	struct js_dev *curd;
	void *all;
	int i = 0;
	unsigned long flags;

	if ((all = kmalloc(sizeof(struct js_dev) + 2 * axes * sizeof(int) +
			2 * (((buttons - 1) >> 5) + 1) * sizeof(int) +
			axes * sizeof(struct js_corr) + strlen(name) + 1, GFP_KERNEL)) == NULL)
		return -1;

	curd = all;

	curd->next = NULL;
	curd->list = NULL;
	curd->port = port;
	curd->wait = NULL;
	curd->open = open;
	curd->close = close;

	curd->ahead = 0;
	curd->bhead = 0;
	curd->tail = JS_BUFF_SIZE - 1;
	curd->num_axes = axes;
	curd->num_buttons = buttons;

	curd->cur.axes = all += sizeof(struct js_dev);
	curd->cur.buttons = all += axes * sizeof(int);
	curd->new.axes = all += (((buttons - 1) >> 5) + 1) * sizeof(int);
	curd->new.buttons = all += axes * sizeof(int);
	curd->corr = all += (((buttons -1 ) >> 5) + 1) * sizeof(int);

	curd->name = all += axes * sizeof(struct js_corr);
	strcpy(curd->name, name);

	port->devs[number] = curd;
	port->axes[number] = curd->new.axes;
	port->buttons[number] = curd->new.buttons;
	port->corr[number] = curd->corr;

	spin_lock_irqsave(&js_lock, flags);

	while (*ptrd != NULL) { ptrd=&(*ptrd)->next; i++; }
	*ptrd = curd;

	spin_unlock_irqrestore(&js_lock, flags);	

	return i;
}

void js_unregister_device(struct js_dev *dev)
{
	struct js_dev **curd = &js_dev;
	unsigned long flags;

	spin_lock_irqsave(&js_lock, flags);

	while (*curd != NULL && (*curd != dev)) curd = &((*curd)->next);
	*curd = (*curd)->next;

	spin_unlock_irqrestore(&js_lock, flags);	

	kfree(dev);
}

/*
 * The operations structure.
 */

static struct file_operations js_fops =
{
	read:		js_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
	poll:		js_poll,
#else
	select:		js_select,
#endif
	ioctl:		js_ioctl,
	open:		js_open,
	release:	js_release,
};

/*
 * js_init() registers the driver and calls the probe function.
 * also initializes some crucial variables.
 */

#ifdef MODULE
int init_module(void)
#else
int __init js_init(void)
#endif
{
	int result;

	js_setup_time();

	if (register_chrdev(JOYSTICK_MAJOR, "js", &js_fops)) {
		printk(KERN_ERR "js: unable to get major %d for joystick\n", JOYSTICK_MAJOR);
		return -EBUSY;
	}

	spin_lock_init(&js_lock);

	init_timer(&js_timer);
	js_timer.function = js_do_timer;
	js_timer.data = 1;

	memset(&js_comp_glue, 0, sizeof(struct JS_DATA_SAVE_TYPE));
	js_comp_glue.JS_TIMEOUT = JS_DEF_TIMEOUT;
	js_comp_glue.JS_TIMELIMIT = JS_DEF_TIMELIMIT;

#ifdef MODULE
	result = 0;
#else
	result = -ENODEV;
#ifdef CONFIG_JOY_LIGHTNING
	if (!js_l4_init()) result = 0;
#endif
#ifdef CONFIG_JOY_SIDEWINDER
	if (!js_sw_init()) result = 0;
#endif
#ifdef CONFIG_JOY_ASSASIN
	if (!js_as_init()) result = 0;
#endif
#ifdef CONFIG_JOY_LOGITECH
	if (!js_lt_init()) result = 0;
#endif
#ifdef CONFIG_JOY_THRUSTMASTER
	if (!js_tm_init()) result = 0;
#endif
#ifdef CONFIG_JOY_GRAVIS
	if (!js_gr_init()) result = 0;
#endif
#ifdef CONFIG_JOY_ANALOG
	if (!js_an_init()) result = 0;
#endif
#ifdef CONFIG_JOY_CONSOLE
	if (!js_console_init()) result = 0;
#endif
#ifdef CONFIG_JOY_DB9
	if (!js_db9_init()) result = 0;
#endif
#ifdef CONFIG_JOY_AMIGA
	if (!js_am_init()) result = 0;
#endif
	if (result) printk(KERN_ERR "js: no joysticks found\n");
#endif

	return result;
}

/*
 * cleanup_module() handles module removal.
 */

#ifdef MODULE
void cleanup_module(void)
{
	del_timer(&js_timer);
	if (unregister_chrdev(JOYSTICK_MAJOR, "js"))
		printk(KERN_ERR "js: can't unregister device\n");
}
#endif

/*
 *  $Id: joystick.c,v 1.2 1997/10/31 19:11:48 mj Exp $
 *
 *  Copyright (C) 1997 Vojtech Pavlik
 */

/*
 *  This is joystick driver for Linux. It supports up to two analog joysticks
 *  on a PC compatible machine. See Documentation/joystick.txt for changelog
 *  and credits.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/major.h>
#include <linux/joystick.h>

#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <asm/param.h>

#define PIT_HZ			1193180L	/* PIT clock is 1.19318 MHz */

#define JS_MAXTIME		PIT_HZ/250	/* timeout for read (4 ms) */

#define JS_BUTTON_PERIOD	HZ/50		/* button valid time (20 ms) */
#define JS_AXIS_MIN_PERIOD	HZ/25		/* axis min valid time (40 ms) */
#define JS_AXIS_MAX_PERIOD	HZ/25*2		/* axis max valid time (80 ms) */

#define JS_FIFO_SIZE    	16		/* number of FIFO entries */
#define JS_BUFF_SIZE		32 		/* output buffer size */
#define JS_RETRIES		4		/* number of retries */ 
#define JS_DEF_PREC		8		/* initial precision for all axes */

#define JS_NUM			2		/* number of joysticks */

#define JS_AXES 		0x0f		/* bit mask for all axes */
#define JS_BUTTONS		0xf0		/* bit mask for all buttons */

#define PIT_MODE 		0x43		/* timer mode port */
#define PIT_DATA 		0x40		/* timer 0 data port */
#define JS_PORT 		0x201		/* joystick port */

#define JS_TRIGGER      	0xff		/* triggers one-shots */
#define PIT_READ_TIMER		0x00		/* to read timer 0 */

#define DELTA(X,Y,Z)	((X)-(Y)+(((X)>=(Y))?0:Z))				/* cyclic delta */
#define DELTA_T(X,Y)	DELTA((X),(Y),(PIT_HZ/HZ))				/* for time measurement */
#define DELTA_TX(X,Y,Z)	DELTA_T((X),((Y)&0xFF)|(((Z)&0xFF)<<8))
#define ROT(A,B,C)	((((A)<(C))&&(((B)>(A))&&((B)<(C))))||(((A)>(C))&&(((B)>(A))||((B)<(C)))))
#define GOF(X)		(((X)==JS_BUFF_SIZE-1)?0:(X)+1)
#define GOFF(X)		(((X)==JS_FIFO_SIZE-1)?0:(X)+1)
#define GOB(X)		((X)?(X)-1:JS_BUFF_SIZE-1)

struct js_data {
	int ahead;
	int bhead;
	int tail;
	struct js_event buff[JS_BUFF_SIZE];
	struct js_list *list;
	struct wait_queue *wait;
	unsigned int exist;
};

struct js_axis {
	int value;
	struct js_corr corr;
};

struct js_list {
	struct js_list *next;			/* next-in-list pointer */
	unsigned long time;			/* when the device was open */
	int tail;				/* a tail for js_buff */
	char startup;
};

struct js_fifo {
	unsigned long time;
	unsigned long event;
};

static struct js_data jsd[JS_NUM];			/* joystick data */
static struct timer_list js_timer;			/* joystick timer */

static unsigned char js_fifo_head = 0;			/* head of the fifo */
static unsigned char js_fifo_tail = JS_FIFO_SIZE - 1;	/* tail of the fifo */
static struct js_fifo js_fifo[JS_FIFO_SIZE];		/* the fifo */

static unsigned char js_last_buttons = 0;		/* last read button state */
static unsigned long js_axis_time = 0;			/* last read axis time */
static unsigned long js_mark_time = 0;

static unsigned char js_axes_exist;			/* all axes that exist */
static unsigned char js_buttons_exist;			/* all buttons that exist */

static struct js_axis js_axis[4];
static unsigned int js_buttons = 0;

MODULE_AUTHOR("Vojtech Pavlik <vojtech@atrey.karlin.mff.cuni.cz>");
MODULE_SUPPORTED_DEVICE("js");
MODULE_PARM(js, "0-1b");

static char js[] = {0, 0};

/*
 * get_pit() returns the immediate state of PIT0. Must be run
 * with interrupts disabled.
 */

static inline int get_pit(void)
{
	int t, flags;
	
	save_flags(flags);
	cli();
	outb(PIT_READ_TIMER, PIT_MODE);
	t = inb(PIT_DATA);
	t |= (int) inb(PIT_DATA) << 8;
	restore_flags(flags);
	return t;
}

/*
 * count_bits() counts set bits in a byte.
 */

static int count_bits(unsigned char c)
{
	int i, t = 0;
	for (i = 0; i < 8; i++)
		if (c & (1 << i)) t++;
	return t;
}

/*
 * js_correct() performs correction of raw joystick data.
 */

static int js_correct(int value, struct js_corr *corr)
{
	int t;

	if (corr->type == JS_CORR_NONE) return value;
	t = value > corr->coef[0] ? (value < corr->coef[1] ? corr->coef[0] : value - corr->coef[1] + corr->coef[0]) : value;
	if (t == corr->coef[0]) return 32768;

	switch (corr->type) {
	case JS_CORR_BROKEN:
		t = t < corr->coef[0] ? ((corr->coef[2] * t) >> 14) + corr->coef[3] :
					((corr->coef[4] * t) >> 14) + corr->coef[5];
		break;
	default: 
		return 0;
	}

	if (t < 0) return 0;
        if (t > 65535) return 65535;

        return t;
}

/*
 * js_compare() compares two close axis values and decides 
 * whether they are "same".
 */

static int js_compare(int x, int y, int prec)
{
	return (x < y + prec) && (y < x + prec); 
}

/*
 * js_probe() probes for joysticks 
 */

inline int js_probe(void)
{
	int t;

	outb(JS_TRIGGER, JS_PORT);
	t = get_pit();
	while (DELTA_T(t, get_pit()) < JS_MAXTIME);
	t = inb(JS_PORT);

	if (js[0] || js[1]) {
		jsd[0].exist = js[0] & ~(t & JS_AXES);
		jsd[1].exist = js[1] & ~(t & JS_AXES);
	} else
	switch (t & JS_AXES) {
		case 0x0c: jsd[0].exist = 0x33; jsd[1].exist = 0x00; break;	/* joystick 0 connected */
		case 0x03: jsd[0].exist = 0x00; jsd[1].exist = 0xcc; break;	/* joystick 1 connected */
		case 0x04: jsd[0].exist = 0xfb; jsd[1].exist = 0x00; break;	/* 3-axis joystick connected */
		case 0x00: jsd[0].exist = 0x33; jsd[1].exist = 0xcc; break;	/* joysticks 0 and 1 connected */
		default:   jsd[0].exist = 0x00; jsd[1].exist = 0x00; return -1;	/* no joysticks */	
	}

	js_axes_exist = (jsd[0].exist | jsd[1].exist) & JS_AXES;
	js_buttons_exist = (jsd[0].exist | jsd[1].exist) & JS_BUTTONS;

	return 0;
}

/* 
 * js_do_timer() controls the action by adding entries to the event
 * fifo each time a button changes its state or axis valid time
 * expires.
 */

static void js_do_timer(unsigned long data)
{
	int t = ~inb(JS_PORT) & js_buttons_exist;
	if ((js_last_buttons != t) && (js_fifo_head != js_fifo_tail)) {
		js_fifo[js_fifo_head].event = js_last_buttons = t;
		js_fifo[js_fifo_head].time = jiffies;
		js_fifo_head++;
		if (js_fifo_head == JS_FIFO_SIZE) js_fifo_head = 0;
		if (!js_mark_time) {
			js_mark_time = jiffies;
			mark_bh(JS_BH);
		}
	} 
	else
 	if ((jiffies > js_axis_time + JS_AXIS_MAX_PERIOD) && !js_mark_time) {
		js_mark_time = jiffies;
		mark_bh(JS_BH);
	}
	js_timer.expires = jiffies + JS_BUTTON_PERIOD;
	add_timer(&js_timer);
}

/*
 * js_do_bh() does the main processing and adds events to output buffers.
 */

static void js_do_bh(void)
{

	int i, j, k;
	unsigned int t;

	if (jiffies > js_axis_time + JS_AXIS_MIN_PERIOD) {

		unsigned int old_axis[4];
		unsigned int t_low, t_high;
		unsigned int flags, joy_state;
		unsigned int t1l, t1h, jsm;
		unsigned char jss;
		unsigned char again;
		unsigned char retries = 0;

		for (i = 0; i < 4; i++)
			old_axis[i] = js_axis[i].value;

		do {
			i = 0;
			again = 0;
			t_low = 0;
			t_high = 0;
			joy_state = JS_AXES;

/*
 * Measure the axes.
 */

			save_flags(flags);
			cli();						/* no interrupts */
			outb(JS_TRIGGER, JS_PORT);			/* trigger one-shots */
			outb(PIT_READ_TIMER, PIT_MODE);			/* read timer */
			t = (t1l = inb(PIT_DATA)) | 
			    (t1h = inb(PIT_DATA)) << 8;	
			restore_flags(flags);

			do {
				jss = inb(JS_PORT);
				if ((jss ^ joy_state) & js_axes_exist) {
					t_low = (t_low << 8) | t1l;
					t_high = (t_high << 8) | t1h;
					joy_state = (joy_state << 8) | jss;
					i++;
				}
				
				cli();
				outb(PIT_READ_TIMER, PIT_MODE);
				t1l = inb(PIT_DATA);
				t1h = inb(PIT_DATA);
				restore_flags(flags);

			} while ((jss & js_axes_exist) && (DELTA_TX(t, t1l, t1h) < JS_MAXTIME));

/*
 * Process the gathered axis data in joy_state.
 */

			joy_state ^= ((joy_state >> 8) | 0xff000000L);  /* More magic */

			for (; i > 0; i--) {
				for (j = 0; j < 4; j++)
				if (joy_state & js_axes_exist & (1 << j)) {
					jsm = js_correct(DELTA_TX(t, t_low, t_high), &js_axis[j].corr);
					if (!js_compare(jsm, js_axis[j].value, js_axis[j].corr.prec)) {
						if (jsm < js_axis[j].value || !retries)
							 js_axis[j].value = jsm;
						again = 1;
					}
				}
				joy_state = joy_state >> 8;
				t_low = t_low >> 8;
				t_high = t_high >> 8;
			}

		} while (retries++ < JS_RETRIES && again); 

/*
 * Check if joystick lost.
 */

		for (i = 0; i < JS_NUM; i++) {

			if (jsd[i].exist && ((jss & jsd[i].exist & JS_AXES) == (jsd[i].exist & JS_AXES))) {
				printk(KERN_WARNING "js%d: joystick lost.\n", i);
				js_buttons_exist &= ~jsd[i].exist;
				js_axes_exist &= ~jsd[i].exist;
				jsd[i].exist = 0;
				wake_up_interruptible(&jsd[i].wait);
			}

			if ((jss & jsd[i].exist & JS_AXES)) {
				printk(KERN_WARNING "js%d: joystick broken. Check cables.\n", i);
			}

		}

/*
 * Put changed axes into output buffer.
 */

		if (retries > 1)
		for (i = 0; i < JS_NUM; i++)
		if (jsd[i].list) {
			k = 0;
			for (j = 0; j < 4; j++)
			if ((1 << j) & jsd[i].exist) {
				if (!js_compare(js_axis[j].value, old_axis[j], js_axis[j].corr.prec)) {
					jsd[i].buff[jsd[i].ahead].time = js_mark_time;
					jsd[i].buff[jsd[i].ahead].type = JS_EVENT_AXIS;
					jsd[i].buff[jsd[i].ahead].number = k;
					jsd[i].buff[jsd[i].ahead].value = js_axis[j].value;
					jsd[i].ahead++;
					if (jsd[i].ahead == JS_BUFF_SIZE) jsd[i].ahead = 0;
				}
				k++;
			}
		}
		js_axis_time = jiffies;
	}
	js_mark_time = 0;

/*
 * And now process the button fifo.
 */

	while (js_fifo_head != (t = GOFF(js_fifo_tail))) {
		for (i = 0; i < JS_NUM; i++)
		if (jsd[i].list) {
			k = 0;
			for (j = 4; j < 8; j++)
			if ((1 << j) & jsd[i].exist) {
				if ((1 << j) & (js_buttons ^ js_fifo[t].event)) {
					jsd[i].buff[jsd[i].ahead].time = js_fifo[t].time;
					jsd[i].buff[jsd[i].ahead].type = JS_EVENT_BUTTON;
					jsd[i].buff[jsd[i].ahead].number = k;
					jsd[i].buff[jsd[i].ahead].value = (js_fifo[t].event >> j) & 1;
					jsd[i].ahead++;
					if (jsd[i].ahead == JS_BUFF_SIZE) jsd[i].ahead = 0;
					}
				k++;
			}
		}
		js_buttons = js_fifo[js_fifo_tail = t].event;
	}

/*
 * Sync ahead with bhead and cut too long tails.
 */
	
	for (i = 0; i < JS_NUM; i++)
	if (jsd[i].list)
	if (jsd[i].bhead != jsd[i].ahead)	{
		if (ROT(jsd[i].bhead, jsd[i].tail, jsd[i].ahead) || (jsd[i].tail == jsd[i].bhead)) {
			struct js_list *curl;
			curl = jsd[i].list;
			while (curl) {
				if (ROT(jsd[i].bhead, curl->tail, jsd[i].ahead) || (curl->tail == jsd[i].bhead)) {
					curl->tail = jsd[i].ahead; 				
					curl->startup = jsd[i].exist;
				}
				curl = curl->next;
			}
			jsd[i].tail = jsd[i].ahead;		
		}
		jsd[i].bhead = jsd[i].ahead;
		wake_up_interruptible(&jsd[i].wait);
	}

}

/*
 * js_lseek() just returns with error.
 */

static loff_t js_lseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/*
 * js_read() copies one or more entries from jsd[].buff to user
 * space.
 */

static ssize_t js_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct wait_queue wait = { current, NULL };
	struct js_list *curl = file->private_data;
	struct js_event *buff = (void *) buf;
	unsigned long blocks = count / sizeof(struct js_event);
	unsigned long i = 0, j;
	int t, u = curl->tail;
        int retval = 0;

/*
 * Check user data.
 */

	if (MAJOR(file->f_dentry->d_inode->i_rdev) != JOYSTICK_MAJOR)
		return -EINVAL;
	if (file->f_pos < 0)
		return -EINVAL;
	if (!blocks)
		return -EINVAL;
	if (!curl)
		return -EINVAL;

	if (minor > JS_NUM)
		return -ENODEV;
	if (!jsd[minor].exist)
		return -ENODEV;

/*
 * Handle (non)blocking i/o.
 */

	if (count != sizeof(struct JS_DATA_TYPE)) {

		if ((GOF(curl->tail) == jsd[minor].ahead && !curl->startup) || (curl->startup && !js_axis_time)) {
			add_wait_queue(&jsd[minor].wait, &wait);
			current->state = TASK_INTERRUPTIBLE;
			while ((GOF(curl->tail) == jsd[minor].ahead && !curl->startup) || (curl->startup && !js_axis_time)) {
				if (file->f_flags & O_NONBLOCK) {
					retval = -EAGAIN;
					break;
				}
				if (signal_pending(current)) {
					retval = -ERESTARTSYS;
					break;
				}
				schedule();
				if (!jsd[minor].exist) {
					retval = -ENODEV;
					break;
				}
			}
			current->state = TASK_RUNNING;
			remove_wait_queue(&jsd[minor].wait, &wait);
		}

		if (retval) return retval;
	
/*
 * Do the i/o.
 */

		if (curl->startup) {
			struct js_event tmpevent;

			t = 0;
			for (j = 0; j < 4 && (i < blocks) && !retval; j++)
			if (jsd[minor].exist & (1 << j)) {
				if (curl->startup & (1 << j)) {
					tmpevent.type = JS_EVENT_AXIS | JS_EVENT_INIT;
					tmpevent.number = t;
					tmpevent.value = js_axis[j].value;
  					if (copy_to_user(&buff[i], &tmpevent, sizeof(struct js_event)))
						retval = -EFAULT;
					if (put_user((__u32)((jiffies - curl->time) * (1000/HZ)), &buff[i].time))
						retval = -EFAULT;
					curl->startup &= ~(1 << j);
					i++;
				}
				t++;	
			}

			t = 0;
			for (j = 4; j < 8 && (i < blocks) && !retval; j++)
			if (jsd[minor].exist & (1 << j)) {
				if (curl->startup & (1 << j)) {
					tmpevent.type = JS_EVENT_BUTTON | JS_EVENT_INIT;
					tmpevent.number = t;
					tmpevent.value = (js_buttons >> j) & 1;
  					if (copy_to_user(&buff[i], &tmpevent, sizeof(struct js_event)))
						retval = -EFAULT;
					if (put_user((__u32)((jiffies - curl->time) * (1000/HZ)), &buff[i].time))
						retval = -EFAULT;
					curl->startup &= ~(1 << j);
					i++;
				}
				t++;	
			}
		}


		while ((jsd[minor].ahead != (t = GOF(curl->tail))) && (i < blocks) && !retval) {
			if (copy_to_user(&buff[i], &jsd[minor].buff[t], sizeof(struct js_event)))
				retval = -EFAULT;
			if (put_user((__u32)((jsd[minor].buff[t].time - curl->time) * (1000/HZ)), &buff[i].time))
				retval = -EFAULT;
			curl->tail = t;
			i++;
		}
	
	}

	else

/*
 * Handle version 0.x compatibility.
 */

	{
		struct JS_DATA_TYPE *bufo = (void *) buf;
		int buttons = 0;

		while (~jsd[minor].exist & (1<<i)) i++;
		copy_to_user(&bufo->x, &js_axis[i].value, sizeof(int));

		i++;
		while (~jsd[minor].exist & (1<<i)) i++;
		copy_to_user(&bufo->y, &js_axis[i].value, sizeof(int));

		i = 0;
		for (j = 4; j < 8; j++)
		if ((1 << j) & jsd[minor].exist)
			buttons |= (!!(js_last_buttons & (1 << j))) << (i++);
		copy_to_user(&bufo->buttons, &buttons, sizeof(int));

		curl->tail = GOB(jsd[minor].ahead);
		retval = sizeof(struct JS_DATA_TYPE);
	}

/*
 * Check main tail and move it.
 */

	if (u == jsd[minor].tail) {
		t = curl->tail;
		curl = jsd[minor].list;
		while (curl && curl->tail != jsd[minor].tail) {
			if (ROT(jsd[minor].ahead, t, curl->tail) ||
				(jsd[minor].ahead == curl->tail)) t = curl->tail;
			curl = curl->next;
		}
		if (!curl) jsd[minor].tail = t;
	}
	
	return retval ? retval : i*sizeof(struct js_event);
}

/*
 * js_poll() does select() support.
 */

static unsigned int js_poll(struct file *file, poll_table *wait)
{
	struct js_list *curl;
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
 	curl = file->private_data;

	poll_wait(&jsd[minor].wait, wait);
	if (GOF(curl->tail) != jsd[minor].ahead) 
		return POLLIN | POLLRDNORM;
        return 0;
}

/*
 * js_ioctl handles misc ioctl calls.
 */

static int js_ioctl(struct inode *inode,
		     struct file *file,
		     unsigned int cmd,
		     unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int i, j;

	if (MAJOR(inode->i_rdev) != JOYSTICK_MAJOR)
		return -EINVAL;
	if (minor > JS_NUM)
		return -ENODEV;
	if (!jsd[minor].exist)
		return -ENODEV;

	switch (cmd) {
	case JSIOCGVERSION:
		if(put_user(JS_VERSION, (__u32 *) arg)) return -EFAULT;
		break;
	case JSIOCGAXES:
		if(put_user(count_bits(jsd[minor].exist & JS_AXES), (__u8 *) arg)) return -EFAULT;
		break;
	case JSIOCGBUTTONS:
		if(put_user(count_bits(jsd[minor].exist & JS_BUTTONS), (__u8 *) arg)) return -EFAULT;
		break;
	case JSIOCSCORR:
		j = 0;
		for (i = 0; i < 4; i++)
		if ((1 << i) & jsd[minor].exist) {
			if (copy_from_user(&js_axis[i].corr, (void *) arg + j * sizeof(struct js_corr),
				sizeof(struct js_corr))) return -EFAULT;
			j++;
		}
		js_axis_time = 0;
		break;
	case JSIOCGCORR:
		j = 0;
		for (i = 0; i < 4; i++)
		if ((1 << i) & jsd[minor].exist) {
			if (copy_to_user((void *) arg + j * sizeof(struct js_corr), &js_axis[i].corr, 
				sizeof(struct js_corr))) return -EFAULT;
			j++;
		}
		break;
	default:
		return -EINVAL;
	}
	
	return 0;
}

/*
 * js_open() performs necessary initialization and adds
 * an entry to the linked list.
 */

static int js_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct js_list *curl;
	int t;

	if (MAJOR(inode->i_rdev) != JOYSTICK_MAJOR)
		return -EINVAL;
	if (minor > JS_NUM)
		return -ENODEV;
	if (!jsd[minor].exist) {
		js_probe();
		if (jsd[minor].exist) printk(KERN_INFO "js%d: %d-axis joystick at %#x\n", 
			minor,  count_bits(jsd[minor].exist & JS_AXES), JS_PORT);
		else return -ENODEV;
	}

	MOD_INC_USE_COUNT;

	if (!jsd[0].list && !jsd[1].list) { 
		js_timer.expires = jiffies + JS_BUTTON_PERIOD;
		add_timer(&js_timer);
	}

	curl = jsd[minor].list;
	jsd[minor].list = kmalloc(sizeof(struct js_list), GFP_KERNEL);
	jsd[minor].list->next = curl;
	jsd[minor].list->startup = jsd[minor].exist;
	jsd[minor].list->tail = t = GOB(jsd[minor].ahead);
	jsd[minor].list->time = jiffies;

	file->private_data = jsd[minor].list;

	return 0;
}

/*
 * js_release() removes an entry from list and deallocates memory
 * used by it.
 */

static int js_release(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct js_list **curp, *curl;
	int t;

	curp = &jsd[minor].list;
	curl = file->private_data;

	while (*curp && (*curp != curl)) curp = &((*curp)->next);
 	*curp = (*curp)->next;

	if (jsd[minor].list) {
		if (curl->tail == jsd[minor].tail) {
			curl = jsd[minor].list;
			t = curl->tail;
			while (curl && curl->tail != jsd[minor].tail) {
				if (ROT(jsd[minor].ahead, t, curl->tail) ||
					(jsd[minor].ahead == curl->tail)) t = curl->tail;
				curl = curl->next;
			}
			if (!curl) jsd[minor].tail = t;
		}
	}

	kfree(file->private_data);
	if (!jsd[0].list && !jsd[1].list) del_timer(&js_timer);

	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * The operations structure.
 */

static struct file_operations js_fops =
{
	js_lseek,		/* js_lseek */
	js_read,		/* js_read */
	NULL,			/* js_write */
	NULL,			/* js_readdir */
	js_poll,		/* js_poll */
	js_ioctl,		/* js_ioctl */
	NULL,			/* js_mmap */
	js_open,		/* js_open */
	js_release,		/* js_release */
	NULL			/* js_sync */
};

/*
 * js_setup() parses kernel command line parametres.
 */

#ifndef MODULE
__initfunc(void js_setup(char *str, int *ints))

{
	js[0] = ((ints[0] > 0) ? ints[1] : 0 );
        js[1] = ((ints[0] > 1) ? ints[2] : 0 );
}
#endif

/*
 * js_init() registres the driver and calls the probe function.
 * also initializes some crucial variables.
 */

#ifdef MODULE
int init_module(void) 
#else
__initfunc(int js_init(void))
#endif
{
	int i;

	if (check_region(JS_PORT, 1)) {
		printk(KERN_ERR "js: port %#x already in use\n", JS_PORT);
		return -EBUSY;
	}

	if (js_probe() < 0) {
		printk(KERN_INFO "js: no joysticks found\n");
		return -ENODEV;
	}

	if (register_chrdev(JOYSTICK_MAJOR, "js", &js_fops)) {
		printk(KERN_ERR "js: unable to get major %d for joystick\n", JOYSTICK_MAJOR);
		return -EBUSY;
	}

	for (i = 0; i < JS_NUM; i++) {
		if (jsd[i].exist) printk(KERN_INFO "js%d: %d-axis joystick at %#x\n",
			 i,  count_bits(jsd[i].exist & JS_AXES), JS_PORT);
		jsd[i].ahead = jsd[i].bhead = 0;
		jsd[i].tail = JS_BUFF_SIZE - 1;
		jsd[i].list = NULL;
		jsd[i].wait = NULL;
		memset(jsd[i].buff, 0, JS_BUFF_SIZE * sizeof(struct js_event));
	}

	for (i = 0; i < 4; i++) {
		js_axis[i].corr.type = JS_CORR_NONE; 
		js_axis[i].corr.prec = JS_DEF_PREC;
	}

	request_region(JS_PORT, 1, "js");
	init_bh(JS_BH, &js_do_bh);
	enable_bh(JS_BH);
	init_timer(&js_timer);
	js_timer.function = js_do_timer;
		
	return 0;
}

/*
 * cleanup_module() handles module removal.
 */

#ifdef MODULE
void cleanup_module(void)
{
	if (MOD_IN_USE)
		printk(KERN_NOTICE "js: device busy, remove delayed\n");
	else {
		del_timer(&js_timer);
		disable_bh(JS_BH);
		if (unregister_chrdev(JOYSTICK_MAJOR, "js"))
			printk(KERN_ERR "js: module cleanup failed\n");
		release_region(JS_PORT, 1);
	}
}
#endif

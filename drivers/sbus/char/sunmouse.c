/* sunmouse.c: Sun mouse driver for the Sparc
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * Parts based on the psaux.c driver written by:
 * Johan Myreen.
 *
 * Dec/19/95 Added SunOS mouse ioctls - miguel.
 * Jan/5/96  Added VUID support, sigio support - miguel.
 * Mar/5/96  Added proper mouse stream support - miguel.
 */

/* The mouse is run off of one of the Zilog serial ports.  On
 * that port is the mouse and the keyboard, each gets a zs channel.
 * The mouse itself is mouse-systems in nature.  So the protocol is:
 *
 * Byte 1) Button state which is bit-encoded as
 *            0x4 == left-button down, else up
 *            0x2 == middle-button down, else up
 *            0x1 == right-button down, else up
 *
 * Byte 2) Delta-x
 * Byte 3) Delta-y
 * Byte 4) Delta-x again
 * Byte 5) Delta-y again
 *
 * One day this driver will have to support more than one mouse in the system.
 *
 * This driver has two modes of operation: the default VUID_NATIVE is
 * set when the device is opened and allows the application to see the
 * mouse character stream as we get it from the serial (for gpm for
 * example).  The second method, VUID_FIRM_EVENT will provide cooked
 * events in Firm_event records.
 * */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/signal.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/vuid_event.h>
#include <linux/random.h>
/* The following keeps track of software state for the Sun
 * mouse.
 */
#define STREAM_SIZE   2048
#define EV_SIZE       (STREAM_SIZE/sizeof (Firm_event))
#define BUTTON_LEFT   4
#define BUTTON_MIDDLE 2
#define BUTTON_RIGHT  1

struct sun_mouse {
	unsigned char transaction[5];  /* Each protocol transaction */
	unsigned char byte;            /* Counter, starts at 0 */
	unsigned char button_state;    /* Current button state */
	unsigned char prev_state;      /* Previous button state */
	int delta_x;                   /* Current delta-x */
	int delta_y;                   /* Current delta-y */
	int present;
	int ready;		       /* set if there if data is available */
	int active;		       /* set if device is open */
        int vuid_mode;	               /* VUID_NATIVE or VUID_FIRM_EVENT */
	struct wait_queue *proc_list;
	struct fasync_struct *fasync;
	
	/* The event/stream queue */
	unsigned int head;
	unsigned int tail;
	union {
		char stream [STREAM_SIZE];
		Firm_event ev [0];
	} queue;
};

static struct sun_mouse sunmouse;
#define gen_events (sunmouse.vuid_mode != VUID_NATIVE)
#define bstate sunmouse.button_state
#define pstate sunmouse.prev_state

extern void mouse_put_char(char ch);

/* #define SMOUSE_DEBUG */

static void
push_event (Firm_event *ev)
{
	int next = (sunmouse.head + 1) % EV_SIZE;
	
	if (next != sunmouse.tail){
		sunmouse.queue.ev [sunmouse.head] = *ev;
		sunmouse.head = next;
	}
}

static int
queue_empty (void)
{
	return sunmouse.head == sunmouse.tail;
}

static Firm_event *
get_from_queue (void)
{
	Firm_event *result;
	
	result = &sunmouse.queue.ev [sunmouse.tail];
	sunmouse.tail = (sunmouse.tail + 1) % EV_SIZE;
	return result;
}

static void
push_char (char c)
{
	int next = (sunmouse.head + 1) % STREAM_SIZE;

	if (next != sunmouse.tail){
		sunmouse.queue.stream [sunmouse.head] = c;
		sunmouse.head = next;
	}
	sunmouse.ready = 1;
	if (sunmouse.fasync)
		kill_fasync (sunmouse.fasync, SIGIO);
	wake_up_interruptible (&sunmouse.proc_list);
}

/* The following is called from the zs driver when bytes are received on
 * the Mouse zs8530 channel.
 */
void
sun_mouse_inbyte(unsigned char byte, unsigned char status)
{
	signed char mvalue;
	int d;
	Firm_event ev;

	add_mouse_randomness (byte);
	if(!sunmouse.active)
		return;

	if (!gen_events){
		push_char (byte);
		return;
	}
	/* Check for framing errors and parity errors */
	/* XXX TODO XXX */

	/* If the mouse sends us a byte from 0x80 to 0x87
	 * we are starting at byte zero in the transaction
	 * protocol.
	 */
	if(byte >= 0x80 && byte <= 0x87)
		sunmouse.byte = 0;

	mvalue = (signed char) byte;
	switch(sunmouse.byte) {
	case 0:
		/* Button state */
		sunmouse.button_state = (~byte) & 0x7;
#ifdef SMOUSE_DEBUG
		printk("B<Left %s, Middle %s, Right %s>",
		       ((sunmouse.button_state & 0x4) ? "DOWN" : "UP"),
		       ((sunmouse.button_state & 0x2) ? "DOWN" : "UP"),
		       ((sunmouse.button_state & 0x1) ? "DOWN" : "UP"));
#endif
		sunmouse.byte++;
		return;
	case 1:
		/* Delta-x 1 */
#ifdef SMOUSE_DEBUG
		printk("DX1<%d>", mvalue);
#endif
		sunmouse.delta_x = mvalue;
		sunmouse.byte++;
		return;
	case 2:
		/* Delta-y 1 */
#ifdef SMOUSE_DEBUG
		printk("DY1<%d>", mvalue);
#endif
		sunmouse.delta_y = mvalue;
		sunmouse.byte++;
		return;
	case 3:
		/* Delta-x 2 */
#ifdef SMOUSE_DEBUG
		printk("DX2<%d>", mvalue);
#endif
		sunmouse.delta_x += mvalue;
		sunmouse.byte++;
		return;
	case 4:
		/* Last byte, Delta-y 2 */
#ifdef SMOUSE_DEBUG
		printk("DY2<%d>", mvalue);
#endif
		sunmouse.delta_y += mvalue;
		sunmouse.byte = 69;  /* Some ridiculous value */
		break;
	case 69:
		/* Until we get the (0x80 -> 0x87) value we aren't
		 * in the middle of a real transaction, so just
		 * return.
		 */
		return;
	default:
		printk("sunmouse: bogon transaction state\n");
		sunmouse.byte = 69;  /* What could cause this? */
		return;
	};
	d = bstate ^ pstate;
	pstate = bstate;
	if (d){
		if (d & BUTTON_LEFT){
			ev.id = MS_LEFT;
			ev.value = bstate & BUTTON_LEFT;
		}
		if (d & BUTTON_RIGHT){
			ev.id = MS_RIGHT;
			ev.value = bstate & BUTTON_RIGHT;
		}
		if (d & BUTTON_MIDDLE){
			ev.id = MS_MIDDLE;
			ev.value = bstate & BUTTON_MIDDLE;
		}
		ev.time = xtime;
		ev.value = ev.value ? VKEY_DOWN : VKEY_UP;
		push_event (&ev);
	}
	if (sunmouse.delta_x){
		ev.id = LOC_X_DELTA;
		ev.time = xtime;
		ev.value = sunmouse.delta_x;
		push_event (&ev);
		sunmouse.delta_x = 0;
	}
	if (sunmouse.delta_y){
		ev.id = LOC_Y_DELTA;
		ev.time = xtime;
		ev.value = sunmouse.delta_y;
		push_event (&ev);
	}
	
        /* We just completed a transaction, wake up whoever is awaiting
	 * this event.
	 */
	sunmouse.ready = 1;
	if (sunmouse.fasync)
		kill_fasync (sunmouse.fasync, SIGIO);
	wake_up_interruptible(&sunmouse.proc_list);
	return;
}

static int
sun_mouse_open(struct inode * inode, struct file * file)
{
	if(!sunmouse.present)
		return -EINVAL;
	if(sunmouse.active)
		return -EBUSY;
	sunmouse.active = 1;
	sunmouse.ready = sunmouse.delta_x = sunmouse.delta_y = 0;
	sunmouse.button_state = 0x80;
	sunmouse.vuid_mode = VUID_NATIVE;
	return 0;
}

static int
sun_mouse_fasync (struct inode *inode, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper (inode, filp, on, &sunmouse.fasync);
	if (retval < 0)
		return retval;
	return 0;
}

static void
sun_mouse_close(struct inode *inode, struct file *file)
{
	sunmouse.active = sunmouse.ready = 0;
	sun_mouse_fasync (inode, file, 0);
}

static int
sun_mouse_write(struct inode *inode, struct file *file, const char *buffer,
		int count)
{
	return -EINVAL;  /* foo on you */
}

static int
sun_mouse_read(struct inode *inode, struct file *file, char *buffer,
	       int count)
{
	struct wait_queue wait = { current, NULL };

	if (queue_empty ()){
		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;
		add_wait_queue (&sunmouse.proc_list, &wait);
		while (queue_empty () && !(current->signal & ~current->blocked)){
			current->state = TASK_INTERRUPTIBLE;
			schedule ();
		}
		current->state = TASK_RUNNING;
		remove_wait_queue (&sunmouse.proc_list, &wait);
	}
	if (gen_events){
		char *p = buffer, *end = buffer+count;
		
		while (p < end && !queue_empty ()){
			*(Firm_event *)p = *get_from_queue ();
			p += sizeof (Firm_event);
		}
		sunmouse.ready = !queue_empty ();
		inode->i_atime = CURRENT_TIME;
		return p-buffer;
	} else {
		int c;
		
		for (c = count; !queue_empty () && c; c--){
			*buffer++ = sunmouse.queue.stream [sunmouse.tail];
			sunmouse.tail = (sunmouse.tail + 1) % STREAM_SIZE;
		}
		sunmouse.ready = !queue_empty ();
		inode->i_atime = CURRENT_TIME;
		return count-c;
	}
	/* Only called if nothing was sent */
	if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	return 0;
}

static int
sun_mouse_select(struct inode *inode, struct file *file, int sel_type,
			    select_table *wait)
{
	if(sel_type != SEL_IN)
		return 0;
	if(sunmouse.ready)
		return 1;
	select_wait(&sunmouse.proc_list, wait);
	return 0;
}
int
sun_mouse_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int i;
	
	switch (cmd){
		/* VUIDGFORMAT - Get input device byte stream format */
	case _IOR('v', 2, int):
		i = verify_area (VERIFY_WRITE, (void *)arg, sizeof (int));
		if (i) return i;
		*(int *)arg = sunmouse.vuid_mode;
		break;

		/* VUIDSFORMAT - Set input device byte stream format*/
	case _IOW('v', 1, int):
		i = verify_area (VERIFY_READ, (void *)arg, sizeof (int));
	        if (i) return i;
		i = *(int *) arg;
		if (i == VUID_NATIVE || i == VUID_FIRM_EVENT){
			sunmouse.vuid_mode = *(int *)arg;
			sunmouse.head = sunmouse.tail = 0;
		} else
			return -EINVAL;
		break;
		
	default:
		printk ("[MOUSE-ioctl: %8.8x]\n", cmd);
		return -1;
	}
	return 0;
}

struct file_operations sun_mouse_fops = {
	NULL,
	sun_mouse_read,
	sun_mouse_write,
	NULL,
	sun_mouse_select,
	sun_mouse_ioctl,
	NULL,
	sun_mouse_open,
	sun_mouse_close,
	NULL,
	sun_mouse_fasync,
};

static struct miscdevice sun_mouse_mouse = {
	SUN_MOUSE_MINOR, "sunmouse", &sun_mouse_fops
};

int
sun_mouse_init(void)
{
	printk("Sun Mouse-Systems mouse driver version 1.00\n");
	sunmouse.present = 1;
	sunmouse.ready = sunmouse.active = 0;
	misc_register (&sun_mouse_mouse);
	sunmouse.delta_x = sunmouse.delta_y = 0;
	sunmouse.button_state = 0x80;
	sunmouse.proc_list = NULL;
	return 0;
}

void
sun_mouse_zsinit(void)
{
	sunmouse.ready = 1;
}

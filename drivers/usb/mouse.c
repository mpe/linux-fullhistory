/*
 * USB HID boot protocol mouse support based on MS BusMouse driver, psaux 
 * driver, and Linus's skeleton USB mouse driver. Fixed up a lot by Linus.
 *
 * Brad Keryan 4/3/1999
 *
 * version 0.20: Linus rewrote read_mouse() to do PS/2 and do it
 * correctly. Events are added together, not queued, to keep the rodent sober.
 *
 * version 0.02: Hmm, the mouse seems drunk because I'm queueing the events.
 * This is wrong: when an application (like X or gpm) reads the mouse device,
 * it wants to find out the mouse's current position, not its recent history.
 * The button thing turned out to be UHCI not flipping data toggle, so half the
 * packets were thrown out.
 *
 * version 0.01: Switched over to busmouse protocol, and changed the minor
 * number to 32 (same as uusbd's hidbp driver). Buttons work more sanely now, 
 * but it still doesn't generate button events unless you move the mouse.
 *
 * version 0.0: Driver emulates a PS/2 mouse, stealing /dev/psaux (sorry, I 
 * know that's not very nice). Moving in the X and Y axes works. Buttons don't
 * work right yet: X sees a lot of MotionNotify/ButtonPress/ButtonRelease 
 * combos when you hold down a button and drag the mouse around. Probably has 
 * some additional bugs on an SMP machine.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/module.h>

#include <asm/spinlock.h>

#include "usb.h"

#define USB_MOUSE_MINOR 32

struct mouse_state {
	unsigned char buttons; /* current button state */
	long dx; /* dx, dy, dz are change since last read */
	long dy; 
	long dz;
	int present; /* this mouse is plugged in */
	int active; /* someone is has this mouse's device open */
	int ready; /* the mouse has changed state since the last read */
	wait_queue_head_t wait; /* for polling */
	struct fasync_struct *fasync;
	/* later, add a list here to support multiple mice */
	/* but we will also need a list of file pointers to identify it */

	/* FIXME: move these to a per-mouse structure */
	struct usb_device *dev;  /* host controller this mouse is on */
	void* irq_handle;  /* host controller's IRQ transfer handle */
	__u8 bEndpointAddress;  /* these are from the endpoint descriptor */
	__u8 bInterval;		/* ...  used when calling usb_request_irq */
};

static struct mouse_state static_mouse_state;

spinlock_t usb_mouse_lock = SPIN_LOCK_UNLOCKED;

static int mouse_irq(int state, void *__buffer, int len, void *dev_id)
{
	signed char *data = __buffer;
	/* finding the mouse is easy when there's only one */
	struct mouse_state *mouse = &static_mouse_state; 

	/* if a mouse moves with no one listening, do we care? no */
	if(!mouse->active)
		return 1;

	/* if the USB mouse sends an interrupt, then something noteworthy
	   must have happened */
	mouse->buttons = data[0] & 0x07;
	mouse->dx += data[1]; /* data[] is signed, so this works */
	mouse->dy -= data[2]; /* y-axis is reversed */
	mouse->dz -= data[3];
	mouse->ready = 1;

	add_mouse_randomness((mouse->buttons << 24) + (mouse->dz << 16 ) + 
				     (mouse->dy << 8) + mouse->dx);

	wake_up_interruptible(&mouse->wait);
	if (mouse->fasync)
		kill_fasync(mouse->fasync, SIGIO);

	return 1;
}

static int fasync_mouse(int fd, struct file *filp, int on)
{
	int retval;
	struct mouse_state *mouse = &static_mouse_state;

	retval = fasync_helper(fd, filp, on, &mouse->fasync);
	if (retval < 0)
		return retval;
	return 0;
}

static int release_mouse(struct inode * inode, struct file * file)
{
	struct mouse_state *mouse = &static_mouse_state;

	fasync_mouse(-1, file, 0);

	MOD_DEC_USE_COUNT;

	if (--mouse->active == 0) {
		/* stop polling the mouse while its not in use */
	    	usb_release_irq(mouse->dev, mouse->irq_handle);
		/* never keep a reference to a released IRQ! */
		mouse->irq_handle = NULL;
	}

	return 0;
}

static int open_mouse(struct inode * inode, struct file * file)
{
	struct mouse_state *mouse = &static_mouse_state;

	if (!mouse->present)
		return -EINVAL;
	if (mouse->active++)
		return 0;
	/* flush state */
	mouse->buttons = mouse->dx = mouse->dy = mouse->dz = 0;

	/* prevent the driver from being unloaded while its in use */
	MOD_INC_USE_COUNT;

	/* start the usb controller's polling of the mouse */
	mouse->irq_handle = usb_request_irq(mouse->dev, usb_rcvctrlpipe(mouse->dev, mouse->bEndpointAddress), mouse_irq, mouse->bInterval, NULL);

	return 0;
}

static ssize_t write_mouse(struct file * file,
       const char * buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

/*
 * Look like a PS/2 mouse, please..
 *
 * The PS/2 protocol is fairly strange, but
 * oh, well, it's at least common..
 */
static ssize_t read_mouse(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
	int retval = 0;
	static int state = 0;
	struct mouse_state *mouse = &static_mouse_state;

	if (count) {
		mouse->ready = 0;
		switch (state) {
		case 0: { /* buttons and sign */
			int buttons = mouse->buttons;
			mouse->buttons = 0;
			if (mouse->dx < 0)
				buttons |= 0x10;
			if (mouse->dy < 0)
				buttons |= 0x20;
			put_user(buttons, buffer);
			buffer++;
			retval++;
			state = 1;
			if (!--count)
				break;
		}
		case 1: { /* dx */
			int dx = mouse->dx;
			mouse->dx = 0;
			put_user(dx, buffer);
			buffer++;
			retval++;
			state = 2;
			if (!--count)
				break;
		}
		case 2:	{ /* dy */
			int dy = mouse->dy;
			mouse->dy = 0;
			put_user(dy, buffer);
			buffer++;
			retval++;
			state = 0;
			if (!--count)
				break;
		}

		/*
		 * SUBTLE:
		 *
		 * The only way to get here is to do a read() of
		 * more than 3 bytes: if you read a byte at a time
		 * you will just ever see states 0-2, for backwards
		 * compatibility.
		 *
		 * So you can think of this as a packet interface,
		 * where you have arbitrary-sized packets, and you
		 * only ever see the first three bytes when you read
		 * them in small chunks.
		 */
		{ /* fallthrough - dz */
			int dz = mouse->dz;
			mouse->dz = 0;
			put_user(dz, buffer);
			buffer++;
			retval++;
			state = 0;
		}
		break;
		}
	}
	return retval;
}

static unsigned int mouse_poll(struct file *file, poll_table * wait)
{
	struct mouse_state *mouse = &static_mouse_state;

	poll_wait(file, &mouse->wait, wait);
	if (mouse->ready)
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations usb_mouse_fops = {
	NULL,		/* mouse_seek */
	read_mouse,
	write_mouse,
	NULL, 		/* mouse_readdir */
	mouse_poll, 	/* mouse_poll */
	NULL, 		/* mouse_ioctl */
	NULL,		/* mouse_mmap */
	open_mouse,
	NULL,		/* flush */
	release_mouse,
	NULL,
	fasync_mouse,
};

static struct miscdevice usb_mouse = {
	USB_MOUSE_MINOR, "USB mouse", &usb_mouse_fops
};

static int mouse_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct mouse_state *mouse = &static_mouse_state;

	/* We don't handle multi-config mice */
	if (dev->descriptor.bNumConfigurations != 1)
		return -1;

	/* We don't handle multi-interface mice */
	if (dev->config[0].bNumInterfaces != 1)
		return -1;

	/* Is it a mouse interface? */
	interface = &dev->config[0].interface[0].altsetting[0];
	if (interface->bInterfaceClass != 3)
		return -1;
	if (interface->bInterfaceSubClass != 1)
		return -1;
	if (interface->bInterfaceProtocol != 2)
		return -1;

	/* Multiple endpoints? What kind of mutant ninja-mouse is this? */
	if (interface->bNumEndpoints != 1)
		return -1;

	endpoint = &interface->endpoint[0];

	/* Output endpoint? Curiousier and curiousier.. */
	if (!(endpoint->bEndpointAddress & 0x80))
		return -1;

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & 3) != 3)
		return -1;

	printk("USB mouse found\n");

	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		printk (KERN_INFO " Failed usb_set_configuration: mouse\n");
		return -1;
	}

	/* these are used to request the irq when the mouse is opened */
	mouse->dev = dev;
	mouse->bEndpointAddress = endpoint->bEndpointAddress;
	mouse->bInterval = endpoint->bInterval;

	mouse->present = 1;
	return 0;
}

static void mouse_disconnect(struct usb_device *dev)
{
	struct mouse_state *mouse = &static_mouse_state;

	/* stop the usb interrupt transfer */
	if (mouse->present) {
	    	usb_release_irq(mouse->dev, mouse->irq_handle);
		/* never keep a reference to a released IRQ! */
		mouse->irq_handle = NULL;
	}

	mouse->irq_handle = NULL;

	/* this might need work */
	mouse->present = 0;
	printk("Mouse disconnected\n");
}

static struct usb_driver mouse_driver = {
	"mouse",
	mouse_probe,
	mouse_disconnect,
	{ NULL, NULL }
};

int usb_mouse_init(void)
{
	struct mouse_state *mouse = &static_mouse_state;

	mouse->present = mouse->active = 0;
	mouse->irq_handle = NULL;
	init_waitqueue_head(&mouse->wait);
	mouse->fasync = NULL;

	misc_register(&usb_mouse);

	usb_register(&mouse_driver);
	printk(KERN_INFO "USB HID boot protocol mouse driver registered.\n");
	return 0;
}

void usb_mouse_cleanup(void)
{
	struct mouse_state *mouse = &static_mouse_state;

	/* stop the usb interrupt transfer */
	if (mouse->present) {
	    	usb_release_irq(mouse->dev, mouse->irq_handle);
		/* never keep a reference to a released IRQ! */
		mouse->irq_handle = NULL;
	}

	/* this, too, probably needs work */
	usb_deregister(&mouse_driver);
	misc_deregister(&usb_mouse);
}

#ifdef MODULE
int init_module(void)
{
	return usb_mouse_init();
}

void cleanup_module(void)
{
	usb_mouse_cleanup();
}
#endif

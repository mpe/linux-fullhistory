/*
 * linux/drivers/char/psaux.c
 *
 * Driver for PS/2 type mouse by Johan Myreen.
 *
 * Supports pointing devices attached to a PS/2 type
 * Keyboard and Auxiliary Device Controller.
 *
 * Corrections in device setup for some laptop mice & trackballs.
 * 02Feb93  (troyer@saifr00.cfsat.Honeywell.COM,mch@wimsey.bc.ca)
 *
 * Changed to prevent keyboard lockups on AST Power Exec.
 * 28Jul93  Brad Bosch - brad@lachman.com
 *
 * Added support for SIGIO. 28Jul95 jem@pandora.pp.fi
 *
 * Rearranged SIGIO support to use code from tty_io.  9Sept95 ctm@ardi.com
 *
 * Modularised 8-Sep-95 Philip Blundell <pjb27@cam.ac.uk>
 *
 * Fixed keyboard lockups at open time
 * 3-Jul-96, 22-Aug-96 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 * Cleanup by Martin Mares, 01-Jun-97 (now uses the new PC kbd include)
 *
 * Renamed misc. name to "psaux",more in keeping with Documentation/devices.txt
 * 13-Jan-1998, Richard Gooch <rgooch@atnf.csiro.au>
 */

/*
 * This really should be part of the pc_kbd driver - they share the same
 * controller, and right now we have ridiculous synchronization problems.
 * Some of the SMP bootup problems may be due to not getting synchronization
 * right.
 *
 * I moved the C&T mouse driver to a file of its own, hopefully that will
 * make it easier to eventually fix this all.
 *
 *		Linus
 */

/* Uncomment the following line if your mouse needs initialization. */

/* #define INITIALIZE_DEVICE */

#include <linux/module.h>
 
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/malloc.h>
#include <linux/miscdevice.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/semaphore.h>

#include <linux/config.h>

#include "pc_keyb.h"

/*
 *	Generic declarations for both PS2 and 82C710
 */

#define PSMOUSE_MINOR      1	       		/* Minor device # for this mouse */
#define AUX_BUF_SIZE	2048

struct aux_queue {
	unsigned long head;
	unsigned long tail;
	struct wait_queue *proc_list;
	struct fasync_struct *fasync;
	unsigned char buf[AUX_BUF_SIZE];
};

static struct aux_queue *queue;
static int aux_count = 0;

static unsigned int get_from_queue(void)
{
	unsigned int result;
	unsigned long flags;

	save_flags(flags);
	cli();
	result = queue->buf[queue->tail];
	queue->tail = (queue->tail + 1) & (AUX_BUF_SIZE-1);
	restore_flags(flags);
	return result;
}


static inline int queue_empty(void)
{
	return queue->head == queue->tail;
}

static int fasync_aux(int fd, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(fd, filp, on, &queue->fasync);
	if (retval < 0)
		return retval;
	return 0;
}

/*
 *	PS/2 Aux Device
 */

#define AUX_INTS_OFF (KBD_MODE_KCC | KBD_MODE_DISABLE_MOUSE | KBD_MODE_SYS | KBD_MODE_KBD_INT)
#define AUX_INTS_ON  (KBD_MODE_KCC | KBD_MODE_SYS | KBD_MODE_MOUSE_INT | KBD_MODE_KBD_INT)

#define MAX_RETRIES	60		/* some aux operations take long time*/
#if defined(__alpha__) && !defined(CONFIG_PCI)
# define AUX_IRQ	9		/* Jensen is odd indeed */
#else
# define AUX_IRQ	12
#endif

/*
 *	Status polling
 */

static int poll_aux_status(void)
{
	int retries=0;

	while ((inb(KBD_STATUS_REG) & (KBD_STAT_IBF | KBD_STAT_OBF)) && retries < MAX_RETRIES) {
 		if ((inb_p(KBD_STATUS_REG) & AUX_STAT_OBF) == AUX_STAT_OBF)
			inb_p(KBD_DATA_REG);
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + (5*HZ + 99) / 100;
		schedule();
		retries++;
	}
	return (retries < MAX_RETRIES);
}

/*
 * Write to aux device
 */

static void aux_write_dev(int val)
{
	poll_aux_status();
	outb_p(KBD_CCMD_WRITE_MOUSE, KBD_CNTL_REG);	    /* Write magic cookie */
	poll_aux_status();
	outb_p(val, KBD_DATA_REG);			    /* Write data */
}

/*
 * Write to device & handle returned ack
 */

#ifdef INITIALIZE_DEVICE
__initfunc(static int aux_write_ack(int val))
{
	aux_write_dev(val);
	poll_aux_status();

	if ((inb(KBD_STATUS_REG) & AUX_STAT_OBF) == AUX_STAT_OBF)
	{
		return (inb(KBD_DATA_REG));
	}
	return 0;
}
#endif /* INITIALIZE_DEVICE */

/*
 * Write aux device command
 */

static void aux_write_cmd(int val)
{
	poll_aux_status();
	outb_p(KBD_CCMD_WRITE_MODE, KBD_CNTL_REG);
	poll_aux_status();
	outb_p(val, KBD_DATA_REG);
}

/*
 * AUX handler critical section start and end.
 * 
 * Only one process can be in the critical section and all keyboard sends are
 * deferred as long as we're inside. This is necessary as we may sleep when
 * waiting for the keyboard controller and other processes / BH's can
 * preempt us. Please note that the input buffer must be flushed when
 * aux_end_atomic() is called and the interrupt is no longer enabled as not
 * doing so might cause the keyboard driver to ignore all incoming keystrokes.
 */

static struct semaphore aux_sema4 = MUTEX;

static inline void aux_start_atomic(void)
{
	down(&aux_sema4);
	disable_bh(KEYBOARD_BH);
}

static inline void aux_end_atomic(void)
{
	enable_bh(KEYBOARD_BH);
	up(&aux_sema4);
}

/*
 * Interrupt from the auxiliary device: a character
 * is waiting in the keyboard/aux controller.
 */

static void aux_interrupt(int cpl, void *dev_id, struct pt_regs * regs)
{
	int head = queue->head;
	int maxhead = (queue->tail-1) & (AUX_BUF_SIZE-1);

	if ((inb(KBD_STATUS_REG) & AUX_STAT_OBF) != AUX_STAT_OBF)
		return;

	add_mouse_randomness(queue->buf[head] = inb(KBD_DATA_REG));
	if (head != maxhead) {
		head++;
		head &= AUX_BUF_SIZE-1;
	}
	queue->head = head;
	if (queue->fasync)
		kill_fasync(queue->fasync, SIGIO);
	wake_up_interruptible(&queue->proc_list);
}

static int release_aux(struct inode * inode, struct file * file)
{
	fasync_aux(-1, file, 0);
	if (--aux_count)
		return 0;
#ifdef CONFIG_VT
	pckbd_read_mask = KBD_STAT_OBF;
#endif
	aux_start_atomic();
	aux_write_cmd(AUX_INTS_OFF);			    /* Disable controller ints */
	poll_aux_status();
	outb_p(KBD_CCMD_MOUSE_DISABLE, KBD_CNTL_REG);	    /* Disable Aux device */
	poll_aux_status();
	aux_end_atomic();
#ifdef CONFIG_MCA
	free_irq(AUX_IRQ, inode);
#else
	free_irq(AUX_IRQ, NULL);
#endif
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Install interrupt handler.
 * Enable auxiliary device.
 */

static int open_aux(struct inode * inode, struct file * file)
{
	aux_start_atomic();
	if (aux_count++) {
		aux_end_atomic();
		return 0;
	}
	if (!poll_aux_status()) {		/* FIXME: Race condition */
		aux_count--;
		aux_end_atomic();
		return -EBUSY;
	}
	queue->head = queue->tail = 0;		/* Flush input queue */
#ifdef CONFIG_MCA
	if (request_irq(AUX_IRQ, aux_interrupt, MCA_bus ? SA_SHIRQ : 0, "PS/2 Mouse", inode)) {
#else
	if (request_irq(AUX_IRQ, aux_interrupt, 0, "PS/2 Mouse", NULL)) {
#endif
		aux_count--;
		aux_end_atomic();
		return -EBUSY;
	}
	MOD_INC_USE_COUNT;
	poll_aux_status();
	outb_p(KBD_CCMD_MOUSE_ENABLE, KBD_CNTL_REG);	    /* Enable Aux */
	aux_write_dev(AUX_ENABLE_DEV);			    /* Enable aux device */
	aux_write_cmd(AUX_INTS_ON);			    /* Enable controller ints */
	poll_aux_status();
	aux_end_atomic();

#ifdef CONFIG_VT
	pckbd_read_mask = AUX_STAT_OBF;
#endif

	return 0;
}

/*
 * Write to the aux device.
 */

static ssize_t write_aux(struct file * file, const char * buffer,
			 size_t count, loff_t *ppos)
{
	ssize_t retval = 0;

	if (count) {
		ssize_t written = 0;

		aux_start_atomic();
		do {
			char c;
			if (!poll_aux_status())
				break;
			outb_p(KBD_CCMD_WRITE_MOUSE, KBD_CNTL_REG);
			if (!poll_aux_status())
				break;
			get_user(c, buffer++);
			outb_p(c, KBD_DATA_REG);
			written++;
		} while (--count);
		aux_end_atomic();
		retval = -EIO;
		if (written) {
			retval = written;
			file->f_dentry->d_inode->i_mtime = CURRENT_TIME;
		}
	}

	return retval;
}

/*
 * Put bytes from input queue to buffer.
 */

static ssize_t read_aux(struct file * file, char * buffer,
			size_t count, loff_t *ppos)
{
	struct wait_queue wait = { current, NULL };
	ssize_t i = count;
	unsigned char c;

	if (queue_empty()) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&queue->proc_list, &wait);
repeat:
		current->state = TASK_INTERRUPTIBLE;
		if (queue_empty() && !signal_pending(current)) {
			schedule();
			goto repeat;
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&queue->proc_list, &wait);
	}
	while (i > 0 && !queue_empty()) {
		c = get_from_queue();
		put_user(c, buffer++);
		i--;
	}
	if (count-i) {
		file->f_dentry->d_inode->i_atime = CURRENT_TIME;
		return count-i;
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

static unsigned int aux_poll(struct file *file, poll_table * wait)
{
	poll_wait(file, &queue->proc_list, wait);
	if (!queue_empty())
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations psaux_fops = {
	NULL,		/* seek */
	read_aux,
	write_aux,
	NULL, 		/* readdir */
	aux_poll,
	NULL, 		/* ioctl */
	NULL,		/* mmap */
	open_aux,
	NULL,		/* flush */
	release_aux,
	NULL,
	fasync_aux,
};

/*
 * Initialize driver.
 */
static struct miscdevice psaux_mouse = {
	PSMOUSE_MINOR, "psaux", &psaux_fops
};

__initfunc(int psaux_init(void))
{
	if (aux_device_present != 0xaa)
		return -EIO;

	printk(KERN_INFO "PS/2 auxiliary pointing device detected -- driver installed.\n");
	misc_register(&psaux_mouse);
	queue = (struct aux_queue *) kmalloc(sizeof(*queue), GFP_KERNEL);
	memset(queue, 0, sizeof(*queue));
	queue->head = queue->tail = 0;
	queue->proc_list = NULL;

	aux_start_atomic();
#ifdef INITIALIZE_DEVICE
	outb_p(KBD_CCMD_MOUSE_ENABLE, KBD_CNTL_REG); /* Enable Aux */
	aux_write_ack(AUX_SET_SAMPLE);
	aux_write_ack(100);			/* 100 samples/sec */
	aux_write_ack(AUX_SET_RES);
	aux_write_ack(3);			/* 8 counts per mm */
	aux_write_ack(AUX_SET_SCALE21);		/* 2:1 scaling */
	poll_aux_status();
#endif /* INITIALIZE_DEVICE */
	outb_p(KBD_CCMD_MOUSE_DISABLE, KBD_CNTL_REG); /* Disable Aux device */
	poll_aux_status();
	outb_p(KBD_CCMD_WRITE_MODE, KBD_CNTL_REG);    /* Disable controller interrupts */
	poll_aux_status();
	outb_p(AUX_INTS_OFF, KBD_DATA_REG);
	poll_aux_status();
	aux_end_atomic();

	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return psaux_init();
}

void cleanup_module(void)
{
	misc_deregister(&psaux_mouse);
	kfree(queue);
}
#endif

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
 * Modified by Johan Myreen (jem@pandora.pp.fi) 04Aug93
 *   to include support for QuickPort mouse.
 *
 * Changed references to "QuickPort" with "82C710" since "QuickPort"
 * is not what this driver is all about -- QuickPort is just a
 * connector type, and this driver is for the mouse port on the Chips
 * & Technologies 82C710 interface chip. 15Nov93 jem@pandora.pp.fi
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
static int aux_ready = 0;
static int aux_count = 0;
static int aux_present = 0;

/*
 *	Shared subroutines
 */

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

static int fasync_aux(struct inode *inode, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(inode, filp, on, &queue->fasync);
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
	aux_ready = 1;
	if (queue->fasync)
		kill_fasync(queue->fasync, SIGIO);
	wake_up_interruptible(&queue->proc_list);
}

static int release_aux(struct inode * inode, struct file * file)
{
	fasync_aux(inode, file, 0);
	if (--aux_count)
		return 0;
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
	if (!aux_present)
		return -ENODEV;
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

	aux_ready = 0;
	return 0;
}

/*
 * Write to the aux device.
 */

static long write_aux(struct inode * inode, struct file * file,
	const char * buffer, unsigned long count)
{
	int retval = 0;

	if (count) {
		int written = 0;

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
			inode->i_mtime = CURRENT_TIME;
		}
	}

	return retval;
}

/*
 *	82C710 Interface
 */

#ifdef CONFIG_82C710_MOUSE

#define QP_DATA         0x310		/* Data Port I/O Address */
#define QP_STATUS       0x311		/* Status Port I/O Address */

#define QP_DEV_IDLE     0x01		/* Device Idle */
#define QP_RX_FULL      0x02		/* Device Char received */
#define QP_TX_IDLE      0x04		/* Device XMIT Idle */
#define QP_RESET        0x08		/* Device Reset */
#define QP_INTS_ON      0x10		/* Device Interrupt On */
#define QP_ERROR_FLAG   0x20		/* Device Error */
#define QP_CLEAR        0x40		/* Device Clear */
#define QP_ENABLE       0x80		/* Device Enable */

#define QP_IRQ          12

static int qp_present = 0;
static int qp_count = 0;
static int qp_data = QP_DATA;
static int qp_status = QP_STATUS;

static int poll_qp_status(void);
static int probe_qp(void);

/*
 * Interrupt handler for the 82C710 mouse port. A character
 * is waiting in the 82C710.
 */

static void qp_interrupt(int cpl, void *dev_id, struct pt_regs * regs)
{
	int head = queue->head;
	int maxhead = (queue->tail-1) & (AUX_BUF_SIZE-1);

	add_mouse_randomness(queue->buf[head] = inb(qp_data));
	if (head != maxhead) {
		head++;
		head &= AUX_BUF_SIZE-1;
	}
	queue->head = head;
	aux_ready = 1;
	if (queue->fasync)
		kill_fasync(queue->fasync, SIGIO);
	wake_up_interruptible(&queue->proc_list);
}

static int release_qp(struct inode * inode, struct file * file)
{
	unsigned char status;

	fasync_aux(inode, file, 0);
	if (!--qp_count) {
		if (!poll_qp_status())
			printk("Warning: Mouse device busy in release_qp()\n");
		status = inb_p(qp_status);
		outb_p(status & ~(QP_ENABLE|QP_INTS_ON), qp_status);
		if (!poll_qp_status())
			printk("Warning: Mouse device busy in release_qp()\n");
		free_irq(QP_IRQ, NULL);
		MOD_DEC_USE_COUNT;
	}
	return 0;
}

/*
 * Install interrupt handler.
 * Enable the device, enable interrupts. 
 */

static int open_qp(struct inode * inode, struct file * file)
{
	unsigned char status;

	if (!qp_present)
		return -EINVAL;

	if (qp_count++)
		return 0;

	if (request_irq(QP_IRQ, qp_interrupt, 0, "PS/2 Mouse", NULL)) {
		qp_count--;
		return -EBUSY;
	}

	status = inb_p(qp_status);
	status |= (QP_ENABLE|QP_RESET);
	outb_p(status, qp_status);
	status &= ~(QP_RESET);
	outb_p(status, qp_status);

	queue->head = queue->tail = 0;          /* Flush input queue */
	status |= QP_INTS_ON;
	outb_p(status, qp_status);              /* Enable interrupts */

	while (!poll_qp_status()) {
		printk("Error: Mouse device busy in open_qp()\n");
		qp_count--;
		status &= ~(QP_ENABLE|QP_INTS_ON);
		outb_p(status, qp_status);
		free_irq(QP_IRQ, NULL);
		return -EBUSY;
	}

	outb_p(AUX_ENABLE_DEV, qp_data);	/* Wake up mouse */
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * Write to the 82C710 mouse device.
 */

static long write_qp(struct inode * inode, struct file * file,
	const char * buffer, unsigned long count)
{
	int i = count;

	while (i--) {
		char c;
		if (!poll_qp_status())
			return -EIO;
		get_user(c, buffer++);
		outb_p(c, qp_data);
	}
	inode->i_mtime = CURRENT_TIME;
	return count;
}

/*
 * Wait for device to send output char and flush any input char.
 */

static int poll_qp_status(void)
{
	int retries=0;

	while ((inb(qp_status)&(QP_RX_FULL|QP_TX_IDLE|QP_DEV_IDLE))
		       != (QP_DEV_IDLE|QP_TX_IDLE)
		       && retries < MAX_RETRIES) {

		if (inb_p(qp_status)&(QP_RX_FULL))
			inb_p(qp_data);
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + (5*HZ + 99) / 100;
		schedule();
		retries++;
	}
	return !(retries==MAX_RETRIES);
}

/*
 * Function to read register in 82C710.
 */

static inline unsigned char read_710(unsigned char index)
{
	outb_p(index, 0x390);			/* Write index */
	return inb_p(0x391);			/* Read the data */
}

/*
 * See if we can find a 82C710 device. Read mouse address.
 */

__initfunc(static int probe_qp(void))
{
	outb_p(0x55, 0x2fa);			/* Any value except 9, ff or 36 */
	outb_p(0xaa, 0x3fa);			/* Inverse of 55 */
	outb_p(0x36, 0x3fa);			/* Address the chip */
	outb_p(0xe4, 0x3fa);			/* 390/4; 390 = config address */
	outb_p(0x1b, 0x2fa);			/* Inverse of e4 */
	if (read_710(0x0f) != 0xe4)		/* Config address found? */
	  return 0;				/* No: no 82C710 here */
	qp_data = read_710(0x0d)*4;		/* Get mouse I/O address */
	qp_status = qp_data+1;
	outb_p(0x0f, 0x390);
	outb_p(0x0f, 0x391);			/* Close config mode */
	return 1;
}

#endif

/*
 *	Generic part continues...
 */

/*
 * Put bytes from input queue to buffer.
 */

static long read_aux(struct inode * inode, struct file * file,
	char * buffer, unsigned long count)
{
	struct wait_queue wait = { current, NULL };
	int i = count;
	unsigned char c;

	if (queue_empty()) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&queue->proc_list, &wait);
repeat:
		current->state = TASK_INTERRUPTIBLE;
		if (queue_empty() && !(current->signal & ~current->blocked)) {
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
	aux_ready = !queue_empty();
	if (count-i) {
		inode->i_atime = CURRENT_TIME;
		return count-i;
	}
	if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	return 0;
}

static unsigned int aux_poll(struct file *file, poll_table * wait)
{
	poll_wait(&queue->proc_list, wait);
	if (aux_ready)
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
	release_aux,
	NULL,
	fasync_aux,
};

/*
 * Initialize driver. First check for a 82C710 chip; if found
 * forget about the Aux port and use the *_qp functions.
 */
static struct miscdevice psaux_mouse = {
	PSMOUSE_MINOR, "ps2aux", &psaux_fops
};

__initfunc(int psaux_init(void))
{
	int qp_found = 0;

#ifdef CONFIG_82C710_MOUSE
	if ((qp_found = probe_qp())) {
		printk(KERN_INFO "82C710 type pointing device detected -- driver installed.\n");
/*		printk("82C710 address = %x (should be 0x310)\n", qp_data); */
		qp_present = 1;
		psaux_fops.write = write_qp;
		psaux_fops.open = open_qp;
		psaux_fops.release = release_qp;
	} else
#endif
	if (aux_device_present == 0xaa) {
		printk(KERN_INFO "PS/2 auxiliary pointing device detected -- driver installed.\n");
	 	aux_present = 1;
#ifdef CONFIG_VT
		kbd_read_mask = AUX_STAT_OBF;
#endif
	} else {
		return -EIO;
	}
	misc_register(&psaux_mouse);
	queue = (struct aux_queue *) kmalloc(sizeof(*queue), GFP_KERNEL);
	memset(queue, 0, sizeof(*queue));
	queue->head = queue->tail = 0;
	queue->proc_list = NULL;
	if (!qp_found) {
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
	}
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

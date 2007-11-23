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
 */

/* Uncomment the following line if your mouse needs initialization. */

/* #define INITIALIZE_DEVICE */

#include <linux/module.h>
 
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/malloc.h>
#include <linux/miscdevice.h>
#include <linux/random.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

#include <linux/config.h>

#define PSMOUSE_MINOR      1            /* minor device # for this mouse */

/* aux controller ports */
#define AUX_INPUT_PORT	0x60		/* Aux device output buffer */
#define AUX_OUTPUT_PORT	0x60		/* Aux device input buffer */
#define AUX_COMMAND	0x64		/* Aux device command buffer */
#define AUX_STATUS	0x64		/* Aux device status reg */

/* aux controller status bits */
#define AUX_OBUF_FULL	0x21		/* output buffer (from device) full */
#define AUX_IBUF_FULL	0x02		/* input buffer (to device) full */

/* aux controller commands */
#define AUX_CMD_WRITE	0x60		/* value to write to controller */
#define AUX_MAGIC_WRITE	0xd4		/* value to send aux device data */

#define AUX_INTS_ON	0x47		/* enable controller interrupts */
#define AUX_INTS_OFF	0x65		/* disable controller interrupts */

#define AUX_DISABLE	0xa7		/* disable aux */
#define AUX_ENABLE	0xa8		/* enable aux */

/* aux device commands */
#define AUX_SET_RES	0xe8		/* set resolution */
#define AUX_SET_SCALE11	0xe6		/* set 1:1 scaling */
#define AUX_SET_SCALE21	0xe7		/* set 2:1 scaling */
#define AUX_GET_SCALE	0xe9		/* get scaling factor */
#define AUX_SET_STREAM	0xea		/* set stream mode */
#define AUX_SET_SAMPLE	0xf3		/* set sample rate */
#define AUX_ENABLE_DEV	0xf4		/* enable aux device */
#define AUX_DISABLE_DEV	0xf5		/* disable aux device */
#define AUX_RESET	0xff		/* reset aux device */

#define MAX_RETRIES	60		/* some aux operations take long time*/
#if defined(__alpha__) && !defined(CONFIG_PCI)
# define AUX_IRQ	9		/* Jensen is odd indeed */
#else
# define AUX_IRQ	12
#endif
#define AUX_BUF_SIZE	2048

/* 82C710 definitions */

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

extern unsigned char aux_device_present;
extern unsigned char kbd_read_mask;	/* from keyboard.c */

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
static int poll_aux_status(void);
static int poll_aux_status_nosleep(void);
static int fasync_aux(struct inode *inode, struct file *filp, int on);

#ifdef CONFIG_82C710_MOUSE
static int qp_present = 0;
static int qp_count = 0;
static int qp_data = QP_DATA;
static int qp_status = QP_STATUS;

static int poll_qp_status(void);
static int probe_qp(void);
#endif


/*
 * Write to aux device
 */

static void aux_write_dev(int val)
{
	poll_aux_status();
	outb_p(AUX_MAGIC_WRITE,AUX_COMMAND);	/* write magic cookie */
	poll_aux_status();
	outb_p(val,AUX_OUTPUT_PORT);		/* write data */
}

/*
 * Write to device & handle returned ack
 */
#if defined INITIALIZE_DEVICE
static int aux_write_ack(int val)
{
	int retries = 0;

	poll_aux_status_nosleep();
	outb_p(AUX_MAGIC_WRITE,AUX_COMMAND);
	poll_aux_status_nosleep();
	outb_p(val,AUX_OUTPUT_PORT);
	poll_aux_status_nosleep();

	if ((inb(AUX_STATUS) & AUX_OBUF_FULL) == AUX_OBUF_FULL)
	{
		return (inb(AUX_INPUT_PORT));
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
	outb_p(AUX_CMD_WRITE,AUX_COMMAND);
	poll_aux_status();
	outb_p(val,AUX_OUTPUT_PORT);
}


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



/*
 * Interrupt from the auxiliary device: a character
 * is waiting in the keyboard/aux controller.
 */

static void aux_interrupt(int cpl, void *dev_id, struct pt_regs * regs)
{
	int head = queue->head;
	int maxhead = (queue->tail-1) & (AUX_BUF_SIZE-1);

	if ((inb(AUX_STATUS) & AUX_OBUF_FULL) != AUX_OBUF_FULL)
		return;

	add_mouse_randomness(queue->buf[head] = inb(AUX_INPUT_PORT));
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

/*
 * Interrupt handler for the 82C710 mouse port. A character
 * is waiting in the 82C710.
 */

#ifdef CONFIG_82C710_MOUSE
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
#endif


static void release_aux(struct inode * inode, struct file * file)
{
	fasync_aux(inode, file, 0);
	if (--aux_count)
		return;
	aux_write_cmd(AUX_INTS_OFF);		/* disable controller ints */
	poll_aux_status();
	outb_p(AUX_DISABLE,AUX_COMMAND);      	/* Disable Aux device */
	poll_aux_status();
	free_irq(AUX_IRQ, NULL);
	MOD_DEC_USE_COUNT;
}

#ifdef CONFIG_82C710_MOUSE
static void release_qp(struct inode * inode, struct file * file)
{
	unsigned char status;

	fasync_aux(inode, file, 0);
	if (--qp_count)
		return;
	if (!poll_qp_status())
		printk("Warning: Mouse device busy in release_qp()\n");
	status = inb_p(qp_status);
	outb_p(status & ~(QP_ENABLE|QP_INTS_ON), qp_status);
	if (!poll_qp_status())
		printk("Warning: Mouse device busy in release_qp()\n");
	free_irq(QP_IRQ, NULL);
	MOD_DEC_USE_COUNT;
}
#endif

static int fasync_aux(struct inode *inode, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(inode, filp, on, &queue->fasync);
	if (retval < 0)
		return retval;
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
	if (aux_count++)
		return 0;
	if (!poll_aux_status()) {
		aux_count--;
		return -EBUSY;
	}
	queue->head = queue->tail = 0;		/* Flush input queue */
	if (request_irq(AUX_IRQ, aux_interrupt, 0, "PS/2 Mouse", NULL)) {
		aux_count--;
		return -EBUSY;
	}
	MOD_INC_USE_COUNT;
	poll_aux_status();
	outb_p(AUX_ENABLE,AUX_COMMAND);		/* Enable Aux */
	aux_write_dev(AUX_ENABLE_DEV);		/* enable aux device */
	aux_write_cmd(AUX_INTS_ON);		/* enable controller ints */
	poll_aux_status();
	aux_ready = 0;
	return 0;
}

#ifdef CONFIG_82C710_MOUSE
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
#endif

/*
 * Write to the aux device.
 */

static int write_aux(struct inode * inode, struct file * file, const char * buffer, int count)
{
	int i = count;

	while (i--) {
		if (!poll_aux_status())
			return -EIO;
		outb_p(AUX_MAGIC_WRITE,AUX_COMMAND);
		if (!poll_aux_status())
			return -EIO;
		outb_p(get_user(buffer++),AUX_OUTPUT_PORT);
	}
	inode->i_mtime = CURRENT_TIME;
	return count;
}


#ifdef CONFIG_82C710_MOUSE
/*
 * Write to the 82C710 mouse device.
 */

static int write_qp(struct inode * inode, struct file * file, const char * buffer, int count)
{
	int i = count;

	while (i--) {
		if (!poll_qp_status())
			return -EIO;
		outb_p(get_user(buffer++), qp_data);
	}
	inode->i_mtime = CURRENT_TIME;
	return count;
}
#endif


/*
 * Put bytes from input queue to buffer.
 */

static int read_aux(struct inode * inode, struct file * file, char * buffer, int count)
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


static int aux_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
	if (sel_type != SEL_IN)
		return 0;
	if (aux_ready)
		return 1;
	select_wait(&queue->proc_list, wait);
	return 0;
}


struct file_operations psaux_fops = {
	NULL,		/* seek */
	read_aux,
	write_aux,
	NULL, 		/* readdir */
	aux_select,
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

int psaux_init(void)
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
		kbd_read_mask = AUX_OBUF_FULL;
	} else {
		return -EIO;
	}
	misc_register(&psaux_mouse);
	queue = (struct aux_queue *) kmalloc(sizeof(*queue), GFP_KERNEL);
	memset(queue, 0, sizeof(*queue));
	queue->head = queue->tail = 0;
	queue->proc_list = NULL;
	if (!qp_found) {
#if defined INITIALIZE_DEVICE
		outb_p(AUX_ENABLE,AUX_COMMAND);		/* Enable Aux */
		aux_write_ack(AUX_SET_SAMPLE);
		aux_write_ack(100);			/* 100 samples/sec */
		aux_write_ack(AUX_SET_RES);
		aux_write_ack(3);			/* 8 counts per mm */
		aux_write_ack(AUX_SET_SCALE21);		/* 2:1 scaling */
		poll_aux_status_nosleep();
#endif /* INITIALIZE_DEVICE */
		outb_p(AUX_DISABLE,AUX_COMMAND);   /* Disable Aux device */
		poll_aux_status_nosleep();
		outb_p(AUX_CMD_WRITE,AUX_COMMAND);
		poll_aux_status_nosleep();             /* Disable interrupts */
		outb_p(AUX_INTS_OFF, AUX_OUTPUT_PORT); /*  on the controller */
	}
	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return psaux_init(); /*?? Bjorn */
}

void cleanup_module(void)
{
	misc_deregister(&psaux_mouse);
}
#endif

static int poll_aux_status(void)
{
	int retries=0;

	while ((inb(AUX_STATUS)&0x03) && retries < MAX_RETRIES) {
 		if ((inb_p(AUX_STATUS) & AUX_OBUF_FULL) == AUX_OBUF_FULL)
			inb_p(AUX_INPUT_PORT);
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + (5*HZ + 99) / 100;
		schedule();
		retries++;
	}
	return !(retries==MAX_RETRIES);
}

static int poll_aux_status_nosleep(void)
{
	int retries = 0;

	while ((inb(AUX_STATUS)&0x03) && retries < 1000000) {
 		if ((inb_p(AUX_STATUS) & AUX_OBUF_FULL) == AUX_OBUF_FULL)
			inb_p(AUX_INPUT_PORT);
		retries++;
	}
	return !(retries == 1000000);
}

#ifdef CONFIG_82C710_MOUSE
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

static int probe_qp(void)
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

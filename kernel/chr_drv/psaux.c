/*
 * linux/kernel/chr_drv/psaux.c
 *
 * Driver for PS/2 type mouse by Johan Myreen.
 *
 * Supports pointing devices attached to a PS/2 type
 * Keyboard and Auxiliary Device Controller.
 *
 */

#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/errno.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

#define AUX_INPUT_PORT	0x60		/* Aux device output buffer */
#define AUX_OUTPUT_PORT	0x60		/* Aux device input buffer */
#define AUX_COMMAND	0x64		/* Aux device command buffer */
#define AUX_STATUS	0x64		/* Aux device status reg */

#define MAX_RETRIES	3
#define AUX_IRQ		12
#define AUX_BUF_SIZE	2048

extern unsigned char aux_device_present;

struct aux_queue {
	unsigned long head;
	unsigned long tail;
	struct wait_queue *proc_list;
	unsigned char buf[AUX_BUF_SIZE];
};

static struct aux_queue *queue;
static int aux_ready = 0;
static int aux_busy = 0;
static int aux_present = 0;

static int poll_status(void);


static unsigned int get_from_queue()
{
	unsigned int result;
	unsigned long flags;

	__asm__ __volatile__ ("pushfl ; popl %0; cli":"=r" (flags));
	result = queue->buf[queue->tail];
	queue->tail = (queue->tail + 1) & (AUX_BUF_SIZE-1);
	__asm__ __volatile__ ("pushl %0 ; popfl"::"r" (flags));
	return result;
}


static inline int queue_empty()
{
	return queue->head == queue->tail;
}


/*
 * Interrupt from the auxiliary device: a character
 * is waiting in the keyboard/aux controller.
 */

static void aux_interrupt(int cpl)
{
	int head = queue->head;
	int maxhead = (queue->tail-1) & (AUX_BUF_SIZE-1);

	queue->buf[head] = inb(AUX_INPUT_PORT);
	if (head != maxhead) {
		head++;
		head &= AUX_BUF_SIZE-1;
	}
	queue->head = head;
	aux_ready = 1;
	wake_up(&queue->proc_list);
}


static void release_aux(struct inode * inode, struct file * file)
{
	poll_status();
	outb_p(0xa7,AUX_COMMAND);      	/* Disable Aux device */
	poll_status();
	outb_p(0x60,AUX_COMMAND);
	poll_status();
	outb_p(0x65,AUX_OUTPUT_PORT);
	free_irq(AUX_IRQ);
	aux_busy = 0;
}


/*
 * Install interrupt handler.
 * Enable auxiliary device.
 */

static int open_aux(struct inode * inode, struct file * file)
{
	if (aux_busy)
		return -EBUSY;
	if (!aux_present)
		return -EINVAL;
	if (!poll_status())
		return -EBUSY;
	aux_busy = 1;
	queue->head = queue->tail = 0;  /* Flush input queue */
	if (request_irq(AUX_IRQ, aux_interrupt))
		return -EBUSY;
	outb_p(0x60,AUX_COMMAND);	/* Write command */
	poll_status();
	outb_p(0x47,AUX_OUTPUT_PORT);	/* Enable AUX and keyb interrupts */
	poll_status();
	outb_p(0xa8,AUX_COMMAND);	/* Enable AUX */
	return 0;
}


/*
 * Write to the aux device.
 */

static int write_aux(struct inode * inode, struct file * file, char * buffer, int count)
{
	int i = count;

	while (i--) {
		if (!poll_status())
			return -EIO;
		outb_p(0xd4,AUX_COMMAND);
		if (!poll_status())
			return -EIO;
		outb_p(get_fs_byte(buffer++),AUX_OUTPUT_PORT);
	}
	inode->i_mtime = CURRENT_TIME;
	return count;
}


/*
 * Put bytes from input queue to buffer.
 */

static int read_aux(struct inode * inode, struct file * file, char * buffer, int count)
{
	int i = count;
	unsigned char c;

	if (queue_empty()) {
		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;
		cli();
		interruptible_sleep_on(&queue->proc_list);
		sti();
	}		
	while (i > 0 && !queue_empty()) {
		c = get_from_queue();
		put_fs_byte(c, buffer++);
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
	open_aux,
	release_aux,
};


long psaux_init(long kmem_start)
{
	if (aux_device_present != 0xaa) {
		printk("No PS/2 type pointing device detected.\n");
		return kmem_start;
	}
	printk("PS/2 type pointing device detected and installed.\n");
	queue = (struct aux_queue *) kmem_start;
	kmem_start += sizeof (struct aux_queue);
	queue->head = queue->tail = 0;
	queue->proc_list = 0;
	aux_present = 1;
	return kmem_start;
}


static int poll_status(void)
{
	int retries=0;

	while ((inb(AUX_STATUS)&0x03) && retries++ < MAX_RETRIES) {
		if (inb_p(AUX_STATUS)&0x01)
			inb_p(AUX_INPUT_PORT);
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + 5;
		schedule();
	}
	return !(retries==MAX_RETRIES);
}

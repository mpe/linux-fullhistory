/*
 *	Linux driver for the PC110 pad
 *
 *	The pad provides triples of data. The first byte has
 *	0x80=bit 8 X, 0x01=bit 7 X, 0x08=bit 8 Y, 0x01=still down
 *	The second byte is bits 0-6 X
 *	The third is bits 0-6 Y
 *
 *	This is read internally and used to synthesize a stream of
 *	triples in the form expected from a PS/2 device.
 *
 *	0.0 1997-05-16 Alan Cox <alan@cymru.net> - Pad reader
 *	0.1 1997-05-19 Robin O'Leary <robin@acm.org> - PS/2 emulation
 *	0.2 1997-06-03 Robin O'Leary <robin@acm.org> - tap gesture
 *	0.3 1997-06-27 Alan Cox <alan@cymru.net> - 2.1 commit
 *	0.4 1997-11-09 Alan Cox <alan@cymru.net> - Single Unix VFS API changes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/ptrace.h>
#include <linux/poll.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>

#include <asm/signal.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "pc110pad.h"


static struct pc110pad_params default_params = {
	PC110PAD_PS2,	/* read mode */
	50 MS,		/* bounce interval */
	200 MS,		/* tap interval */
	10,		/* IRQ */
	0x15E0,		/* I/O port */
};


static struct pc110pad_params current_params;


/* driver/filesystem interface management */
static struct wait_queue *queue;
static struct fasync_struct *asyncptr;
static int active=0;	/* number of concurrent open()s */


/*
 * Utility to reset a timer to go off some time in the future.
 */

static void set_timer_callback(struct timer_list *timer, int ticks)
{
	del_timer(timer);
	timer->expires = jiffies+ticks;
	add_timer(timer);
}


/*
 * Take care of letting any waiting processes know that
 * now would be a good time to do a read().  Called
 * whenever a state transition occurs, real or synthetic.
 */
 
static void wake_readers(void)
{
	wake_up_interruptible(&queue);
	if(asyncptr)
		kill_fasync(asyncptr, SIGIO);
}


/*****************************************************************************/
/*
 * Deal with the messy business of synthesizing button tap and drag
 * events.
 *
 * Exports:
 *	notify_pad_up_down()
 *		Must be called whenever debounced pad up/down state changes.
 *	button_pending
 *		Flag is set whenever read_button() has new values
 *		to return.
 *	read_button()
 *		Obtains the current synthetic mouse button state.
 */

/*
 * These keep track of up/down transitions needed to generate the
 * synthetic mouse button events.  While recent_transition is set,
 * up/down events cause transition_count to increment.  tap_timer
 * turns off the recent_transition flag and may cause some synthetic
 * up/down mouse events to be created by incrementing synthesize_tap.
 */
 
static int button_pending=0;
static int recent_transition=0;
static int transition_count=0;
static int synthesize_tap=0;
static void tap_timeout(unsigned long data);
static struct timer_list tap_timer = { NULL, NULL, 0, 0, tap_timeout };


/*
 * This callback goes off a short time after an up/down transition;
 * before it goes off, transitions will be considered part of a
 * single PS/2 event and counted in transition_count.  Once the
 * timeout occurs the recent_transition flag is cleared and
 * any synthetic mouse up/down events are generated.
 */
 
static void tap_timeout(unsigned long data)
{
	if(!recent_transition)
	{
		printk("pc110pad: tap_timeout but no recent transition!\n");
	}
	if( transition_count==2 || transition_count==4 || transition_count==6 )
	{
		synthesize_tap+=transition_count;
		button_pending = 1;
		wake_readers();
	}
	recent_transition=0;
}


/*
 * Called by the raw pad read routines when a (debounced) up/down
 * transition is detected.
 */
 
void notify_pad_up_down(void)
{
	if(recent_transition)
	{
		transition_count++;
	}
	else
	{
		transition_count=1;
		recent_transition=1;
	}
	set_timer_callback(&tap_timer, current_params.tap_interval);

	/* changes to transition_count can cause reported button to change */
	button_pending = 1;
	wake_readers();
}


static void read_button(int *b)
{
	if(synthesize_tap)
	{
		*b=--synthesize_tap & 1;
	}
	else
	{
		*b=(!recent_transition && transition_count==3);	/* drag */
	}
	button_pending=(synthesize_tap>0);
}


/*****************************************************************************/
/*
 * Read pad absolute co-ordinates and debounced up/down state.
 *
 * Exports:
 *	pad_irq()
 *		Function to be called whenever the pad signals
 *		that it has new data available.
 *	read_raw_pad()
 *		Returns the most current pad state.
 *	xy_pending
 *		Flag is set whenever read_raw_pad() has new values
 *		to return.
 * Imports:
 *	wake_readers()
 *		Called when movement occurs.
 *	notify_pad_up_down()
 *		Called when debounced up/down status changes.
 */

/*
 * These are up/down state and absolute co-ords read directly from pad 
 */

static int raw_data[3];
static int raw_data_count=0;
static int raw_x=0, raw_y=0;	/* most recent absolute co-ords read */
static int raw_down=0;		/* raw up/down state */
static int debounced_down=0;	/* up/down state after debounce processing */
static enum { NO_BOUNCE, JUST_GONE_UP, JUST_GONE_DOWN } bounce=NO_BOUNCE;
				/* set just after an up/down transition */
static int xy_pending=0;	/* set if new data have not yet been read */

/* 
 * Timer goes off a short while after an up/down transition and copies
 * the value of raw_down to debounced_down.
 */
 
static void bounce_timeout(unsigned long data);
static struct timer_list bounce_timer = { NULL, NULL, 0, 0, bounce_timeout };


static void bounce_timeout(unsigned long data)
{
	/*
	 * No further up/down transitions happened within the
	 * bounce period, so treat this as a genuine transition.
	 */
	switch(bounce)
	{
		case NO_BOUNCE:
		{
			/*
			 * Strange; the timer callback should only go off if
			 * we were expecting to do bounce processing!
			 */
			printk("pc110pad, bounce_timeout: bounce flag not set!\n");
			break;
		}
		case JUST_GONE_UP:
		{
			/*
			 * The last up we spotted really was an up, so set
			 * debounced state the same as raw state.
			 */
			bounce=NO_BOUNCE;
			if(debounced_down==raw_down)
			{
				printk("pc110pad, bounce_timeout: raw already debounced!\n");
			}
			debounced_down=raw_down;

			notify_pad_up_down();
			break;
		}
		case JUST_GONE_DOWN:
		{
			/*
			 * We don't debounce down events, but we still time
			 * out soon after one occurs so we can avoid the (x,y)
			 * skittering that sometimes happens.
			 */
			bounce=NO_BOUNCE;
			break;
		}
	}
}


/*
 * Callback when pad's irq goes off; copies values in to raw_* globals;
 * initiates debounce processing.
 */
static void pad_irq(int irq, void *ptr, struct pt_regs *regs)
{

	/* Obtain byte from pad and prime for next byte */
	{
		int value=inb_p(current_params.io);
		int handshake=inb_p(current_params.io+2);
		outb_p(handshake | 1, current_params.io+2);
		outb_p(handshake &~1, current_params.io+2);
		inb_p(0x64);

		raw_data[raw_data_count++]=value;
	}

	if(raw_data_count==3)
	{
		int new_down=raw_data[0]&0x01;
		int new_x=raw_data[1];
		int new_y=raw_data[2];
		if(raw_data[0]&0x10) new_x+=128;
		if(raw_data[0]&0x80) new_x+=256;
		if(raw_data[0]&0x08) new_y+=128;

		if( (raw_x!=new_x) || (raw_y!=new_y) )
		{
			raw_x=new_x;
			raw_y=new_y;
			xy_pending=1;
		}

		if(new_down != raw_down)
		{
			/* Down state has changed.  raw_down always holds
			 * the most recently observed state.
			 */
			raw_down=new_down;

			/* Forget any earlier bounce processing */
			if(bounce)
			{
				del_timer(&bounce_timer);
				bounce=NO_BOUNCE;
			}

			if(new_down)
			{
				if(debounced_down)
				{
					/* pad gone down, but we were reporting
					 * it down anyway because we suspected
					 * (correctly) that the last up was just
					 * a bounce
					 */
				}
				else
				{
					bounce=JUST_GONE_DOWN;
					set_timer_callback(&bounce_timer,
						current_params.bounce_interval);
					/* start new stroke/tap */
					debounced_down=new_down;
					notify_pad_up_down();
				}
			}
			else /* just gone up */
			{
				if(recent_transition)
				{
					/* early bounces are probably part of
					 * a multi-tap gesture, so process
					 * immediately
					 */
					debounced_down=new_down;
					notify_pad_up_down();
				}
				else
				{
					/* don't trust it yet */
					bounce=JUST_GONE_UP;
					set_timer_callback(&bounce_timer,
						current_params.bounce_interval);
				}
			}
		}
		wake_readers();
		raw_data_count=0;
	}
}


static void read_raw_pad(int *down, int *debounced, int *x, int *y)
{
	disable_irq(current_params.irq);
	{
		*down=raw_down;
		*debounced=debounced_down;
		*x=raw_x;
		*y=raw_y;
		xy_pending = 0;
	}
	enable_irq(current_params.irq);
}

/*****************************************************************************/
/*
 * Filesystem interface
 */

/* 
 * Read returns byte triples, so we need to keep track of
 * how much of a triple has been read.  This is shared across
 * all processes which have this device open---not that anything
 * will make much sense in that case.
 */
static int read_bytes[3];
static int read_byte_count=0;



static void sample_raw(int d[3])
{
	d[0]=raw_data[0];
	d[1]=raw_data[1];
	d[2]=raw_data[2];
}


static void sample_rare(int d[3])
{
	int thisd, thisdd, thisx, thisy;

	read_raw_pad(&thisd, &thisdd, &thisx, &thisy);

	d[0]=(thisd?0x80:0)
	   | (thisx/256)<<4
	   | (thisdd?0x08:0)
	   | (thisy/256)
	;
	d[1]=thisx%256;
	d[2]=thisy%256;
}


static void sample_debug(int d[3])
{
	int thisd, thisdd, thisx, thisy;
	int b;
	cli();
	read_raw_pad(&thisd, &thisdd, &thisx, &thisy);
	d[0]=(thisd?0x80:0) | (thisdd?0x40:0) | bounce;
	d[1]=(recent_transition?0x80:0)+transition_count;
	read_button(&b);
	d[2]=(synthesize_tap<<4) | (b?0x01:0);
	sti();
}


static void sample_ps2(int d[3])
{
	static int lastx, lasty, lastd;

	int thisd, thisdd, thisx, thisy;
	int dx, dy, b;

	/*
	 * Obtain the current mouse parameters and limit as appropriate for
	 * the return data format.  Interrupts are only disabled while 
	 * obtaining the parameters, NOT during the puts_fs_byte() calls,
	 * so paging in put_user() does not affect mouse tracking.
	 */
	read_raw_pad(&thisd, &thisdd, &thisx, &thisy);
	read_button(&b);

	/* Now compare with previous readings.  Note that we use the
	 * raw down flag rather than the debounced one.
	 */
	if( (thisd && !lastd) /* new stroke */
	 || (bounce!=NO_BOUNCE) )
	{
		dx=0;
		dy=0;
	}
	else
	{
		dx =  (thisx-lastx);
		dy = -(thisy-lasty);
	}
	lastx=thisx;
	lasty=thisy;
	lastd=thisd;

/*
	d[0]= ((dy<0)?0x20:0)
	    | ((dx<0)?0x10:0)
	    | 0x08
	    | (b? 0x01:0x00)
	;
*/
	d[0]= ((dy<0)?0x20:0)
	    | ((dx<0)?0x10:0)
	    | (b? 0x00:0x08)
	;
	d[1]=dx;
	d[2]=dy;
}



static int fasync_pad(int fd, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(fd, filp, on, &asyncptr);
	if (retval < 0)
		return retval;
	return 0;
}


/*
 * close access to the pad
 */
static int close_pad(struct inode * inode, struct file * file)
{
	fasync_pad(-1, file, 0);
	if (--active)
		return 0;
	outb(0x30, current_params.io+2);	/* switch off digitiser */
	MOD_DEC_USE_COUNT;
	return 0;
}


/*
 * open access to the pad
 */
static int open_pad(struct inode * inode, struct file * file)
{
	if (active++)
		return 0;
	MOD_INC_USE_COUNT;

	cli();
	outb(0x30, current_params.io+2);	/* switch off digitiser */
	pad_irq(0,0,0);		/* read to flush any pending bytes */
	pad_irq(0,0,0);		/* read to flush any pending bytes */
	pad_irq(0,0,0);		/* read to flush any pending bytes */
	outb(0x38, current_params.io+2);	/* switch on digitiser */
	current_params = default_params;
	raw_data_count=0;		/* re-sync input byte counter */
	read_byte_count=0;		/* re-sync output byte counter */
	button_pending=0;
	recent_transition=0;
	transition_count=0;
	synthesize_tap=0;
	del_timer(&bounce_timer);
	del_timer(&tap_timer);
	sti();

	return 0;
}


/*
 * writes are disallowed
 */
static ssize_t write_pad(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}


void new_sample(int d[3])
{
	switch(current_params.mode)
	{
		case PC110PAD_RAW:	sample_raw(d);		break;
		case PC110PAD_RARE:	sample_rare(d);		break;
		case PC110PAD_DEBUG:	sample_debug(d);	break;
		case PC110PAD_PS2:	sample_ps2(d);		break;
	}
}


/*
 * Read pad data.  Currently never blocks.
 */
static ssize_t read_pad(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
	int r;

	for(r=0; r<count; r++)
	{
		if(!read_byte_count)
			new_sample(read_bytes);
		if(put_user(read_bytes[read_byte_count], buffer+r))
			return -EFAULT;
		read_byte_count = (read_byte_count+1)%3;
	}
	return r;
}


/*
 * select for pad input
 */

static unsigned int pad_poll(struct file *file, poll_table * wait)
{
	poll_wait(file, &queue, wait);
    	if(button_pending || xy_pending)
		return POLLIN | POLLRDNORM;
	return 0;
}


static int pad_ioctl(struct inode *inode, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	struct pc110pad_params new;

	if (!inode)
		return -EINVAL;
		
	switch (cmd) {
	case PC110PADIOCGETP:
		new = current_params;
		if(copy_to_user((void *)arg, &new, sizeof(new)))
			return -EFAULT;
		return 0;

	case PC110PADIOCSETP:
		if(copy_from_user(&new, (void *)arg, sizeof(new)))
			return -EFAULT;

		if( (new.mode<PC110PAD_RAW)
		 || (new.mode>PC110PAD_PS2)
		 || (new.bounce_interval<0)
		 || (new.tap_interval<0)
		)
			return -EINVAL;

		current_params.mode	= new.mode;
		current_params.bounce_interval	= new.bounce_interval;
		current_params.tap_interval	= new.tap_interval;
		return 0;
	}
	return -ENOIOCTLCMD;
}


static struct file_operations pad_fops = {
	NULL,		/* pad_seek */
	read_pad,
	write_pad,
	NULL, 		/* pad_readdir */
	pad_poll,
	pad_ioctl,
	NULL,		/* pad_mmap */
	open_pad,
	NULL,		/* flush */
	close_pad,
	NULL,		/* fsync */
	fasync_pad,
	NULL,		/* check_media_change */
	NULL,		/* revalidate */
	NULL		/* lock */
};


static struct miscdevice pc110_pad = {
	PC110PAD_MINOR, "pc110 pad", &pad_fops
};


int pc110pad_init(void)
{
	current_params = default_params;

	if(request_irq(current_params.irq, pad_irq, 0, "pc110pad", 0))
	{
		printk("pc110pad: Unable to get IRQ.\n");
		return -EBUSY;
	}
	if(check_region(current_params.io, 4))
	{
		printk("pc110pad: I/O area in use.\n");
		free_irq(current_params.irq,0);
		return -EBUSY;
	}
	request_region(current_params.io, 4, "pc110pad");
	printk("PC110 digitizer pad at 0x%X, irq %d.\n",
		current_params.io,current_params.irq);
	misc_register(&pc110_pad);
	outb(0x30, current_params.io+2);	/* switch off digitiser */
	
	return 0;
}

#ifdef MODULE

static void pc110pad_unload(void)
{
	outb(0x30, current_params.io+2);	/* switch off digitiser */
	if(current_params.irq)
		free_irq(current_params.irq, 0);
	current_params.irq=0;
	release_region(current_params.io, 4);
	misc_deregister(&pc110_pad);
}



int init_module(void)
{
	return pc110pad_init();
}

void cleanup_module(void)
{
	pc110pad_unload();
}
#endif

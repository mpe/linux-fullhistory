/*
 * Generic parallel printer driver
 *
 * Copyright (C) 1992 by Jim Weigand and Linus Torvalds
 * Copyright (C) 1992,1993 by Michael K. Johnson
 * - Thanks much to Gunter Windau for pointing out to me where the error
 *   checking ought to be.
 * Copyright (C) 1993 by Nigel Gamble (added interrupt code)
 * Copyright (C) 1994 by Alan Cox (Modularised it)
 * LPCAREFUL, LPABORT, LPGETSTATUS added by Chris Metcalf, metcalf@lcs.mit.edu
 * Statistics and support for slow printers by Rob Janssen, rob@knoware.nl
 * "lp=" command line parameters added by Grant Guenther, grant@torque.net
 * lp_read (Status readback) support added by Carsten Gross,
 *                                             carsten@sol.wohnheim.uni-ulm.de
 * Support for parport by Philip Blundell <Philip.Blundell@pobox.com>
 * Parport sharing hacking by Andrea Arcangeli
 * Fixed kernel_(to/from)_user memory copy to check for errors
 * 				by Riccardo Facchetti <fizban@tin.it>
 * Redesigned interrupt handling for handle printers with buggy handshake
 *				by Andrea Arcangeli, 11 May 1998
 * Full efficient handling of printer with buggy irq handshake (now I have
 * understood the meaning of the strange handshake). This is done sending new
 * characters if the interrupt is just happened, even if the printer say to
 * be still BUSY. This is needed at least with Epson Stylus Color. To enable
 * the new TRUST_IRQ mode read the `LP OPTIMIZATION' section below...
 * Fixed the irq on the rising edge of the strobe case.
 * Obsoleted the CAREFUL flag since a printer that doesn' t work with
 * CAREFUL will block a bit after in lp_check_status().
 *				Andrea Arcangeli, 15 Oct 1998
 */

/* This driver should, in theory, work with any parallel port that has an
 * appropriate low-level driver; all I/O is done through the parport
 * abstraction layer.
 *
 * If this driver is built into the kernel, you can configure it using the
 * kernel command-line.  For example:
 *
 *	lp=parport1,none,parport2	(bind lp0 to parport1, disable lp1 and
 *					 bind lp2 to parport2)
 *
 *	lp=auto				(assign lp devices to all ports that
 *				         have printers attached, as determined
 *					 by the IEEE-1284 autoprobe)
 * 
 *	lp=reset			(reset the printer during 
 *					 initialisation)
 *
 *	lp=off				(disable the printer driver entirely)
 *
 * If the driver is loaded as a module, similar functionality is available
 * using module parameters.  The equivalent of the above commands would be:
 *
 *	# insmod lp.o parport=1,none,2
 *
 *	# insmod lp.o parport=auto
 *
 *	# insmod lp.o reset=1
 */

/*
 * LP OPTIMIZATIONS
 *
 * - TRUST_IRQ flag
 * 
 * Epson Stylus Color, HP and many other new printers want the TRUST_IRQ flag
 * set when printing with interrupts. This is a long story. Such printers
 * use a broken handshake (see the timing graph below) when printing with
 * interrupts. The lp driver as default is just able to handle such bogus
 * handshake, but setting such flag cause lp to go faster and probably do
 * what such printers want (even if not documented).
 *
 * NOTE that setting the TRUST_IRQ flag in some printer can cause the irq
 * printing to fail completly. You must try, to know if your printer
 * will handle it. I suggest a graphics printing to force a major flow of
 * characters to the printer for do the test. NOTE also that the TRUST_IRQ
 * flag _should_ be fine everywhere but there is a lot of buggy hardware out
 * there, so I am forced to implement it as a not-default thing.
 * WARNING: before to do the test, be sure to have not played with the
 * `-w' parameter of tunelp!
 *
 * Note also that lp automagically warn you (with a KERN_WARNING) if it
 * detects that you could _try_ to set the TRUST_IRQ flag to speed up the
 * printing and decrease the CPU load.
 *
 * To set the TRUST_IRQ flag you can use this command:
 *
 * tunelp /dev/lp? -T on
 *
 * If you have an old tunelp executable you can (hack and) use this simple
 * C lazy proggy to set the flag in the lp driver:

-------------------------- cut here -------------------------------------
#include <fcntl.h>
#include <sys/ioctl.h>

#define	LPTRUSTIRQ  0x060f

int main(int argc, char **argv)
{
	int fd = open("/dev/lp0", O_RDONLY);
	ioctl(fd, LPTRUSTIRQ, argc - 1);
	if (argc - 1)
		printf("trusting the irq\n");
	else
		printf("untrusting the irq\n");
	return 0;
}
-------------------------- cut here -------------------------------------

 * - LP_WAIT time
 *
 * You can use this setting if your printer is fast enough and/or your
 * machine is slow enough ;-).
 *
 * tunelp /dev/lp? -w 0
 *
 * - LP_CHAR tries
 *
 * If you print with irqs probably you can decrease the CPU load a lot using
 * this setting. This is not the default because the printing is reported to
 * be jerky somewhere...
 *
 * tunelp /dev/lp? -c 1
 *
 *					11 Nov 1998, Andrea Arcangeli
 */

/* COMPATIBILITY WITH OLD KERNELS
 *
 * Under Linux 2.0 and previous versions, lp devices were bound to ports at
 * particular I/O addresses, as follows:
 *
 *	lp0		0x3bc
 *	lp1		0x378
 *	lp2		0x278
 *
 * The new driver, by default, binds lp devices to parport devices as it
 * finds them.  This means that if you only have one port, it will be bound
 * to lp0 regardless of its I/O address.  If you need the old behaviour, you
 * can force it using the parameters described above.
 */

/*
 * The new interrupt handling code take care of the buggy handshake
 * of some HP and Epson printer:
 * ___
 * ACK    _______________    ___________
 *                       |__|
 * ____
 * BUSY   _________              _______
 *                 |____________|
 *
 * I discovered this using the printer scanner that you can find at:
 *
 *	ftp://e-mind.com/pub/linux/pscan/
 *
 *					11 May 98, Andrea Arcangeli
 *
 * My printer scanner run on an Epson Stylus Color show that such printer
 * generates the irq on the _rising_ edge of the STROBE. Now lp handle
 * this case fine too.
 *
 *					15 Oct 1998, Andrea Arcangeli
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/delay.h>

#include <linux/parport.h>
#undef LP_STATS
#include <linux/lp.h>

#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/system.h>

/* if you have more than 3 printers, remember to increase LP_NO */
#define LP_NO 3

struct lp_struct lp_table[LP_NO] =
{
	[0 ... LP_NO-1] = {NULL, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT,
			   NULL,
#ifdef LP_STATS
			   0, 0, {0},
#endif
			   NULL, 0, 0, 0}
};

/* Test if printer is ready */
#define	LP_READY(status)	((status) & LP_PBUSY)
/* Test if the printer is not acking the strobe */
#define	LP_NO_ACKING(status)	((status) & LP_PACK)
/* Test if the printer has error conditions */
#define LP_NO_ERROR(status)					\
	 (((status) & (LP_POUTPA|LP_PSELECD|LP_PERRORP)) ==	\
	 (LP_PSELECD|LP_PERRORP))

#undef LP_DEBUG
#undef LP_READ_DEBUG

/* --- parport support ----------------------------------------- */

static int lp_preempt(void *handle)
{
       struct lp_struct *lps = (struct lp_struct *)handle;

       if (waitqueue_active (&lps->wait_q))
               wake_up_interruptible(&lps->wait_q);

       /* Don't actually release the port now */
       return 1;
}

#define lp_parport_release(x)	do { parport_release(lp_table[(x)].dev); } while (0);
#define lp_parport_claim(x)	do { parport_claim_or_block(lp_table[(x)].dev); } while (0);

/* --- low-level port access ----------------------------------- */

#define r_dtr(x)	(parport_read_data(lp_table[(x)].dev->port))
#define r_str(x)	(parport_read_status(lp_table[(x)].dev->port))
#define w_ctr(x,y)	do { parport_write_control(lp_table[(x)].dev->port, (y)); } while (0)
#define w_dtr(x,y)	do { parport_write_data(lp_table[(x)].dev->port, (y)); } while (0)

static __inline__ void lp_yield (int minor)
{
	if (!parport_yield_blocking (lp_table[minor].dev))
	{
		if (current->need_resched)
			schedule ();
	} else
		lp_table[minor].irq_missed = 1;
}

static __inline__ void lp_schedule(int minor, long timeout)
{
	struct pardevice *dev = lp_table[minor].dev;
	register unsigned long int timeslip = (jiffies - dev->time);
	if ((timeslip > dev->timeslice) && (dev->port->waithead != NULL)) {
		lp_parport_release(minor);
		lp_table[minor].irq_missed = 1;
		schedule_timeout(timeout);
		lp_parport_claim(minor);
	} else
		schedule_timeout(timeout);
}

static int lp_reset(int minor)
{
	int retval;
	lp_parport_claim (minor);
	w_ctr(minor, LP_PSELECP);
	udelay (LP_DELAY);
	w_ctr(minor, LP_PSELECP | LP_PINITP);
	retval = r_str(minor);
	lp_parport_release (minor);
	return retval;
}

#define	lp_wait(minor)	udelay(LP_WAIT(minor))

static inline int lp_char(char lpchar, int minor)
{
	unsigned long count = 0;
#ifdef LP_STATS
	struct lp_stats *stats;
#endif

	if (signal_pending(current))
		return 0;

	for (;;)
	{
		unsigned char status;
		int irq_ok = 0;

		/*
		 * Give a chance to other pardevice to run in the meantime.
		 */
		lp_yield(minor);

		status = r_str(minor);
		if (LP_NO_ERROR(status))
		{
			if (LP_READY(status))
				break;

			/*
			 * This is a crude hack that should be well known
			 * at least by Epson device driver developers. -arca
			 */
			irq_ok = (!LP_POLLED(minor) &&
				  LP_NO_ACKING(status) &&
				  lp_table[minor].irq_detected);
			if ((LP_F(minor) & LP_TRUST_IRQ) && irq_ok)
				break;
		}
		/*
		 * NOTE: if you run with irqs you _must_ use
		 * `tunelp /dev/lp? -c 1' to be rasonable efficient!
		 */
		if (++count == LP_CHAR(minor))
		{
			if (irq_ok)
			{
				static int first_time = 1;
				/*
				 * The printer is using a buggy handshake, so
				 * revert to polling to not overload the
				 * machine and warn the user that its printer
				 * could get optimized trusting the irq. -arca
				 */
				lp_table[minor].irq_missed = 1;
				if (first_time)
				{
					first_time = 0;
					printk(KERN_WARNING "lp%d: the "
					       "printing could be optimized "
					       "using the TRUST_IRQ flag, "
					       "see the top of "
					       "linux/drivers/char/lp.c\n",
					       minor);
				}
			}
			return 0;
		}
	}

	w_dtr(minor, lpchar);

#ifdef LP_STATS
	stats = &LP_STAT(minor);
	stats->chars++;
#endif

	/* must wait before taking strobe high, and after taking strobe
	   low, according spec.  Some printers need it, others don't. */
	lp_wait(minor);

	/* control port takes strobe high */
	if (LP_POLLED(minor))
	{
		w_ctr(minor, LP_PSELECP | LP_PINITP | LP_PSTROBE);
		lp_wait(minor);
		w_ctr(minor, LP_PSELECP | LP_PINITP);
	} else {
		/*
		 * Epson Stylus Color generate the IRQ on the rising edge of
		 * strobe so clean the irq's information before playing with
		 * the strobe. -arca
		 */
		lp_table[minor].irq_detected = 0;
		lp_table[minor].irq_missed = 0;
		/*
		 * Be sure that the CPU doesn' t reorder instructions. -arca
		 */
		mb();
		w_ctr(minor, LP_PSELECP | LP_PINITP | LP_PSTROBE | LP_PINTEN);
		lp_wait(minor);
		w_ctr(minor, LP_PSELECP | LP_PINITP | LP_PINTEN);
	}

	/*
	 * Give to the printer a chance to put BUSY low. Really we could
	 * remove this because we could _guess_ that we are slower to reach
	 * again lp_char() than the printer to put BUSY low, but I' d like
	 * to remove this variable from the function I go solve
	 * when I read bug reports ;-). -arca
	 */
	lp_wait(minor);

#ifdef LP_STATS
	/* update waittime statistics */
	if (count > stats->maxwait) {
#ifdef LP_DEBUG
		printk(KERN_DEBUG "lp%d success after %d counts.\n",
		       minor, count);
#endif
		stats->maxwait = count;
	}
	count *= 256;
	wait = (count > stats->meanwait) ? count - stats->meanwait :
	    stats->meanwait - count;
	stats->meanwait = (255 * stats->meanwait + count + 128) / 256;
	stats->mdev = ((127 * stats->mdev) + wait + 64) / 128;
#endif

	return 1;
}

static void lp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct lp_struct *lp_dev = (struct lp_struct *) dev_id;

	if (waitqueue_active (&lp_dev->wait_q))
		wake_up_interruptible(&lp_dev->wait_q);

	lp_dev->irq_detected = 1;
	lp_dev->irq_missed = 0;
}

static void lp_error(int minor)
{
	if (LP_POLLED(minor) || LP_PREEMPTED(minor)) {
		current->state = TASK_INTERRUPTIBLE;
		lp_parport_release(minor);
		schedule_timeout(LP_TIMEOUT_POLLED);
		lp_parport_claim(minor);
		lp_table[minor].irq_missed = 1;
	}
}

static int lp_check_status(int minor)
{
	unsigned int last = lp_table[minor].last_error;
	unsigned char status = r_str(minor);
	if ((status & LP_POUTPA)) {
		if (last != LP_POUTPA) {
			last = LP_POUTPA;
			printk(KERN_INFO "lp%d out of paper\n", minor);
		}
	} else if (!(status & LP_PSELECD)) {
		if (last != LP_PSELECD) {
			last = LP_PSELECD;
			printk(KERN_INFO "lp%d off-line\n", minor);
		}
	} else if (!(status & LP_PERRORP)) {
		if (last != LP_PERRORP) {
			last = LP_PERRORP;
			printk(KERN_ERR "lp%d on fire!\n", minor);
		}
	}
	else last = 0;

	lp_table[minor].last_error = last;

	if (last != 0) {
		if (LP_F(minor) & LP_ABORT)
			return 1;
		lp_error(minor);
	}

	return 0;
}

static int lp_write_buf(unsigned int minor, const char *buf, int count)
{
	unsigned long copy_size;
	unsigned long total_bytes_written = 0;
	unsigned long bytes_written;
	struct lp_struct *lp = &lp_table[minor];

	if (minor >= LP_NO)
		return -ENXIO;
	if (lp->dev == NULL)
		return -ENXIO;

	lp_table[minor].last_error = 0;
	lp_table[minor].irq_detected = 0;
	lp_table[minor].irq_missed = 1;

	if (LP_POLLED(minor))
		w_ctr(minor, LP_PSELECP | LP_PINITP);
	else
		w_ctr(minor, LP_PSELECP | LP_PINITP | LP_PINTEN);

	do {
		bytes_written = 0;
		copy_size = (count <= LP_BUFFER_SIZE ? count : LP_BUFFER_SIZE);

		if (copy_from_user(lp->lp_buffer, buf, copy_size))
		{
			w_ctr(minor, LP_PSELECP | LP_PINITP);
			return -EFAULT;
		}

		while (copy_size) {
			if (lp_char(lp->lp_buffer[bytes_written], minor)) {
				--copy_size;
				++bytes_written;
#ifdef LP_STATS
				lp->runchars++;
#endif
			} else {
				int rc = total_bytes_written + bytes_written;

#ifdef LP_STATS
				if (lp->runchars > LP_STAT(minor).maxrun)
					LP_STAT(minor).maxrun = lp->runchars;
				LP_STAT(minor).sleeps++;
#endif

				if (signal_pending(current))
				{
					w_ctr(minor, LP_PSELECP | LP_PINITP);
					if (total_bytes_written + bytes_written)
						return total_bytes_written + bytes_written;
					else
						return -EINTR;
				}

#ifdef LP_STATS
				lp->runchars = 0;
#endif

				if (lp_check_status(minor))
				{
					w_ctr(minor, LP_PSELECP | LP_PINITP);
					return rc ? rc : -EIO;
				}

				if (LP_POLLED(minor) ||
				    lp_table[minor].irq_missed)
				{
				lp_polling:
#if defined(LP_DEBUG) && defined(LP_STATS)
					printk(KERN_DEBUG "lp%d sleeping at %d characters for %d jiffies\n", minor, lp->runchars, LP_TIME(minor));
#endif
					current->state = TASK_INTERRUPTIBLE;
					lp_schedule(minor, LP_TIME(minor));
				} else {
					cli();
					if (LP_PREEMPTED(minor))
					{
						/*
						 * We can' t sleep on the interrupt
						 * since another pardevice need the port.
						 * We must check this in a cli() protected
						 * envinroment to avoid parport sharing
						 * starvation.
						 */
						sti();
						goto lp_polling;
					}
					if (!lp_table[minor].irq_detected)
						interruptible_sleep_on_timeout(&lp->wait_q, LP_TIMEOUT_INTERRUPT);
					sti();
				}
			}
		}

		total_bytes_written += bytes_written;
		buf += bytes_written;
		count -= bytes_written;

	} while (count > 0);

	w_ctr(minor, LP_PSELECP | LP_PINITP);
	return total_bytes_written;
}

static ssize_t lp_write(struct file * file, const char * buf,
		        size_t count, loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	ssize_t retv;

#ifdef LP_STATS
	if (jiffies-lp_table[minor].lastcall > LP_TIME(minor))
		lp_table[minor].runchars = 0;

	lp_table[minor].lastcall = jiffies;
#endif

 	/* Claim Parport or sleep until it becomes available
 	 */
 	lp_parport_claim (minor);

	retv = lp_write_buf(minor, buf, count);

 	lp_parport_release (minor);
 	return retv;
}

static long long lp_lseek(struct file * file, long long offset, int origin)
{
	return -ESPIPE;
}

#ifdef CONFIG_PRINTER_READBACK

static int lp_read_nibble(int minor) 
{
	unsigned char i;
	i = r_str(minor)>>3;
	i &= ~8;
	if ((i & 0x10) == 0) i |= 8;
	return (i & 0x0f);
}

static void lp_read_terminate(struct parport *port) {
	parport_write_control(port, (parport_read_control(port) & ~2) | 8);
	/* SelectIN high, AutoFeed low */
	if (parport_wait_peripheral(port, 0x80, 0)) 
		/* timeout, SelectIN high, Autofeed low */
		return;
	parport_write_control(port, parport_read_control(port) | 2);
	/* AutoFeed high */
	parport_wait_peripheral(port, 0x80, 0x80);
	/* no timeout possible, Autofeed low, SelectIN high */
	parport_write_control(port, (parport_read_control(port) & ~2) | 8);
}

/* Status readback confirming to ieee1284 */
static ssize_t lp_read(struct file * file, char * buf,
		       size_t length, loff_t *ppos)
{
	int i;
	unsigned int minor=MINOR(file->f_dentry->d_inode->i_rdev);
	char *temp = buf;
	ssize_t count = 0;
	unsigned char z = 0;
	unsigned char Byte = 0;
	struct parport *port = lp_table[minor].dev->port;

	lp_parport_claim (minor);

	switch (parport_ieee1284_nibble_mode_ok(port, 0))
	{
	case 0:
		/* Handshake failed. */
		lp_read_terminate(port);
		lp_parport_release (minor);
		return -EIO;
	case 1:
		/* No data. */
		lp_read_terminate(port);
		lp_parport_release (minor);
		return 0;
	default:
		/* Data available. */

		/* Hack: Wait 10ms (between events 6 and 7) */
                schedule_timeout((HZ+99)/100);
                break;
	}

	for (i=0; ; i++) {
		parport_frob_control(port, 2, 2); /* AutoFeed high */
		if (parport_wait_peripheral(port, 0x40, 0)) {
#ifdef LP_READ_DEBUG
			/* Some peripherals just time out when they've sent
			   all their data.  */
			printk("%s: read1 timeout.\n", port->name);
#endif
			parport_frob_control(port, 2, 0); /* AutoFeed low */
			break;
		}
		z = lp_read_nibble(minor);
		parport_frob_control(port, 2, 0); /* AutoFeed low */
		if (parport_wait_peripheral(port, 0x40, 0x40)) {
			printk("%s: read2 timeout.\n", port->name);
			break;
		}
		if ((i & 1) != 0) {
			Byte |= (z<<4);
			if (temp) {
				if (__put_user (Byte, temp))
				{
					count = -EFAULT;
					temp = NULL;
				} else {
					temp++;

					if (++count == length)
						temp = NULL;
				}
			}
			/* Does the error line indicate end of data? */
			if ((parport_read_status(port) & LP_PERRORP) == 
			    LP_PERRORP) 
				break;
		} else 
			Byte=z;
	}

	lp_read_terminate(port);

	lp_parport_release (minor);

	return count;
}

#endif

static int lp_open(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	if (minor >= LP_NO)
		return -ENXIO;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENXIO;
	if (test_and_set_bit(LP_BUSY_BIT_POS, &LP_F(minor)))
		return -EBUSY;

	MOD_INC_USE_COUNT;

	/* If ABORTOPEN is set and the printer is offline or out of paper,
	   we may still want to open it to perform ioctl()s.  Therefore we
	   have commandeered O_NONBLOCK, even though it is being used in
	   a non-standard manner.  This is strictly a Linux hack, and
	   should most likely only ever be used by the tunelp application. */
	if ((LP_F(minor) & LP_ABORTOPEN) && !(file->f_flags & O_NONBLOCK)) {
		int status;
		lp_parport_claim (minor);
		status = r_str(minor);
		lp_parport_release (minor);
		if (status & LP_POUTPA) {
			printk(KERN_INFO "lp%d out of paper\n", minor);
			MOD_DEC_USE_COUNT;
			LP_F(minor) &= ~LP_BUSY;
			return -ENOSPC;
		} else if (!(status & LP_PSELECD)) {
			printk(KERN_INFO "lp%d off-line\n", minor);
			MOD_DEC_USE_COUNT;
			LP_F(minor) &= ~LP_BUSY;
			return -EIO;
		} else if (!(status & LP_PERRORP)) {
			printk(KERN_ERR "lp%d printer error\n", minor);
			MOD_DEC_USE_COUNT;
			LP_F(minor) &= ~LP_BUSY;
			return -EIO;
		}
	}
	lp_table[minor].lp_buffer = (char *) kmalloc(LP_BUFFER_SIZE, GFP_KERNEL);
	if (!lp_table[minor].lp_buffer) {
		MOD_DEC_USE_COUNT;
		LP_F(minor) &= ~LP_BUSY;
		return -ENOMEM;
	}
	return 0;
}

static int lp_release(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	kfree_s(lp_table[minor].lp_buffer, LP_BUFFER_SIZE);
	lp_table[minor].lp_buffer = NULL;
	MOD_DEC_USE_COUNT;
	LP_F(minor) &= ~LP_BUSY;
	return 0;
}

static int lp_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int status;
	int retval = 0;

#ifdef LP_DEBUG
	printk(KERN_DEBUG "lp%d ioctl, cmd: 0x%x, arg: 0x%x\n", minor, cmd, arg);
#endif
	if (minor >= LP_NO)
		return -ENODEV;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENODEV;
	switch ( cmd ) {
		case LPTIME:
			LP_TIME(minor) = arg * HZ/100;
			break;
		case LPCHAR:
			LP_CHAR(minor) = arg;
			break;
		case LPABORT:
			if (arg)
				LP_F(minor) |= LP_ABORT;
			else
				LP_F(minor) &= ~LP_ABORT;
			break;
		case LPABORTOPEN:
			if (arg)
				LP_F(minor) |= LP_ABORTOPEN;
			else
				LP_F(minor) &= ~LP_ABORTOPEN;
			break;
#ifdef OBSOLETED
		case LPCAREFUL:
			if (arg)
				LP_F(minor) |= LP_CAREFUL;
			else
				LP_F(minor) &= ~LP_CAREFUL;
			break;
#endif
		case LPTRUSTIRQ:
			if (arg)
				LP_F(minor) |= LP_TRUST_IRQ;
			else
				LP_F(minor) &= ~LP_TRUST_IRQ;
			break;
		case LPWAIT:
			LP_WAIT(minor) = arg;
			break;
		case LPSETIRQ: 
			return -EINVAL;
			break;
		case LPGETIRQ:
			if (copy_to_user((int *) arg, &LP_IRQ(minor),
					sizeof(int)))
				return -EFAULT;
			break;
		case LPGETSTATUS:
			lp_parport_claim(minor);
			status = r_str(minor);
			lp_parport_release(minor);

			if (copy_to_user((int *) arg, &status, sizeof(int)))
				return -EFAULT;
			break;
		case LPRESET:
			lp_reset(minor);
			break;
#ifdef LP_STATS
		case LPGETSTATS:
			if (copy_to_user((int *) arg, &LP_STAT(minor),
					sizeof(struct lp_stats)))
				return -EFAULT;
			if (suser())
				memset(&LP_STAT(minor), 0,
						sizeof(struct lp_stats));
			break;
#endif
 		case LPGETFLAGS:
 			status = LP_F(minor);
			if (copy_to_user((int *) arg, &status, sizeof(int)))
				return -EFAULT;
			break;
		default:
			retval = -EINVAL;
	}
	return retval;
}

static struct file_operations lp_fops = {
	lp_lseek,
#ifdef CONFIG_PRINTER_READBACK
	lp_read,
#else
	NULL,
#endif
	lp_write,
	NULL,		/* lp_readdir */
	NULL,		/* lp_poll */
	lp_ioctl,
	NULL,		/* lp_mmap */
	lp_open,
	NULL,		/* flush */
	lp_release
};

/* --- initialisation code ------------------------------------- */

#ifdef MODULE

static int parport_nr[LP_NO] = { [0 ... LP_NO-1] = LP_PARPORT_UNSPEC };
static char *parport[LP_NO] = { NULL,  };
static int reset = 0;

MODULE_PARM(parport, "1-" __MODULE_STRING(LP_NO) "s");
MODULE_PARM(reset, "i");

#else

static int parport_nr[LP_NO] __initdata = { [0 ... LP_NO-1] = LP_PARPORT_UNSPEC };
static int reset __initdata = 0;

static int parport_ptr = 0;

__initfunc(void lp_setup(char *str, int *ints))
{
	if (!str) {
		if (ints[0] == 0 || ints[1] == 0) {
			/* disable driver on "lp=" or "lp=0" */
			parport_nr[0] = LP_PARPORT_OFF;
		} else {
			printk(KERN_WARNING "warning: 'lp=0x%x' is deprecated, ignored\n", ints[1]);
		}
	} else if (!strncmp(str, "parport", 7)) {
		int n = simple_strtoul(str+7, NULL, 10);
		if (parport_ptr < LP_NO)
			parport_nr[parport_ptr++] = n;
		else
			printk(KERN_INFO "lp: too many ports, %s ignored.\n",
			       str);
	} else if (!strcmp(str, "auto")) {
		parport_nr[0] = LP_PARPORT_AUTO;
	} else if (!strcmp(str, "none")) {
		parport_nr[parport_ptr++] = LP_PARPORT_NONE;
	} else if (!strcmp(str, "reset")) {
		reset = 1;
	}
}

#endif

int lp_register(int nr, struct parport *port)
{
	lp_table[nr].dev = parport_register_device(port, "lp", 
						   lp_preempt, NULL,
						   lp_interrupt, 
						   0,
						   (void *) &lp_table[nr]);
	if (lp_table[nr].dev == NULL)
		return 1;
	lp_table[nr].flags |= LP_EXIST;

	if (reset)
		lp_reset(nr);

	printk(KERN_INFO "lp%d: using %s (%s).\n", nr, port->name, 
	       (port->irq == PARPORT_IRQ_NONE)?"polling":"interrupt-driven");

	return 0;
}

int lp_init(void)
{
	unsigned int count = 0;
	unsigned int i;
	struct parport *port;

	switch (parport_nr[0])
	{
	case LP_PARPORT_OFF:
		return 0;

	case LP_PARPORT_UNSPEC:
	case LP_PARPORT_AUTO:
	        for (port = parport_enumerate(); port; port = port->next) {

			if (parport_nr[0] == LP_PARPORT_AUTO &&
			    port->probe_info.class != PARPORT_CLASS_PRINTER)
				continue;

			if (!lp_register(count, port))
				if (++count == LP_NO)
					break;
		}
		break;

	default:
		for (i = 0; i < LP_NO; i++) {
			if (parport_nr[i] >= 0) {
				char buffer[16];
				sprintf(buffer, "parport%d", parport_nr[i]);
				for (port = parport_enumerate(); port; 
				     port = port->next) {
					if (!strcmp(port->name, buffer)) {
						(void) lp_register(i, port);
						count++;
						break;
					}
				}
			}
		}
		break;
	}

	if (count) {
		if (register_chrdev(LP_MAJOR, "lp", &lp_fops)) {
			printk("lp: unable to get major %d\n", LP_MAJOR);
			return -EIO;
		}
	} else {
		printk(KERN_INFO "lp: driver loaded but no devices found\n");
	}

	return 0;
}

#ifdef MODULE
int init_module(void)
{
	if (parport[0]) {
		/* The user gave some parameters.  Let's see what they were.  */
		if (!strncmp(parport[0], "auto", 4))
			parport_nr[0] = LP_PARPORT_AUTO;
		else {
			int n;
			for (n = 0; n < LP_NO && parport[n]; n++) {
				if (!strncmp(parport[n], "none", 4))
					parport_nr[n] = LP_PARPORT_NONE;
				else {
					char *ep;
					unsigned long r = simple_strtoul(parport[n], &ep, 0);
					if (ep != parport[n]) 
						parport_nr[n] = r;
					else {
						printk(KERN_ERR "lp: bad port specifier `%s'\n", parport[n]);
						return -ENODEV;
					}
				}
			}
		}
	}

	return lp_init();
}

void cleanup_module(void)
{
	unsigned int offset;

	unregister_chrdev(LP_MAJOR, "lp");
	for (offset = 0; offset < LP_NO; offset++) {
		if (lp_table[offset].dev == NULL)
			continue;
		parport_unregister_device(lp_table[offset].dev);
	}
}
#endif

/*
 * PC Watchdog Driver
 * by Ken Hollis (khollis@bitgate.com)
 *
 * Permission granted from Simon Machell (73244.1270@compuserve.com)
 * Written for the Linux Kernel, and GPLed by Ken Hollis
 *
 * 960107	Added request_region routines, modulized the whole thing.
 * 960108	Fixed end-of-file pointer (Thanks to Dan Hollis), added
 *		WD_TIMEOUT define.
 * 960216	Added eof marker on the file, and changed verbose messages.
 * 960716	Made functional and cosmetic changes to the source for
 *		inclusion in Linux 2.0.x kernels, thanks to Alan Cox.
 * 960717	Removed read/seek routines, replaced with ioctl.  Also, added
 *		check_region command due to Alan's suggestion.
 * 960821	Made changes to compile in newer 2.0.x kernels.  Added
 *		"cold reboot sense" entry.
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pcwd.h>

#include <asm/io.h>

#define WD_VER                  "0.50 (08/21/96)"
#define	WD_MINOR		130	/* Minor device number */

#define	WD_TIMEOUT		3	/* 1 1/2 seconds for a timeout */

#define WD_TIMERRESET_PORT1     0x270	/* Reset port - first choice */
#define WD_TIMERRESET_PORT2     0x370	/* Reset port - second choice */
#define WD_CTLSTAT_PORT1        0x271	/* Control port - first choice */
#define WD_CTLSTAT_PORT2        0x371	/* Control port - second choice */
#define	WD_PORT_EXTENT		2	/* Takes up two addresses */

#define WD_WDRST                0x01	/* Previously reset state */
#define WD_T110                 0x02	/* Temperature overheat sense */
#define WD_HRTBT                0x04	/* Heartbeat sense */
#define WD_RLY2                 0x08	/* External relay triggered */
#define WD_SRLY2                0x80	/* Software external relay triggered */

static int current_ctlport, current_readport;
static int is_open, is_eof;

int pcwd_checkcard(void)
{
	int card_dat, prev_card_dat, found = 0, count = 0, done = 0;

	/* As suggested by Alan Cox */
	if (check_region(current_ctlport, WD_PORT_EXTENT)) {
		printk("pcwd: Port 0x%x unavailable.\n", current_ctlport);
		return 0;
	}

	card_dat = 0x00;
	prev_card_dat = 0x00;

	prev_card_dat = inb(current_readport);

	while(count < WD_TIMEOUT) {
#ifdef	DEBUG
		printk("pcwd: Run #%d on port 0x%03x\n", count, current_readport);
#endif

	/* Read the raw card data from the port, and strip off the
	   first 4 bits */

		card_dat = inb_p(current_readport);
		card_dat &= 0x000F;

	/* Sleep 1/2 second (or 500000 microseconds :) */

		udelay(500000L);
		done = 0;

	/* 0x0F usually means that no card data is present, or the card
	   is not installed on this port.  If 0x0F is present here, it's
	   normally safe to assume there's no card at that base address. */

		if (card_dat == 0x0F) {
			count++;
			done = 1;

#ifdef	DEBUG
			printk("pcwd: I show nothing on this port.\n");
#endif
		}

	/* If there's a heart beat in both instances, then this means we
	   found our card.  This also means that either the card was
	   previously reset, or the computer was power-cycled. */

		if ((card_dat & WD_HRTBT) && (prev_card_dat & WD_HRTBT) &&
			(!done)) {
			found = 1;
			done = 1;
#ifdef	DEBUG
			printk("pcwd: I show alternate heart beats.  Card detected.\n");
#endif
			break;
		}

	/* If the card data is exactly the same as the previous card data,
	   it's safe to assume that we should check again.  The manual says
	   that the heart beat will change every second (or the bit will
	   toggle), and this can be used to see if the card is there.  If
	   the card was powered up with a cold boot, then the card will
	   not start blinking until 2.5 minutes after a reboot, so this
	   bit will stay at 1. */

		if ((card_dat == prev_card_dat) && (!done)) {
			count++;
#ifdef	DEBUG
			printk("pcwd: The card data is exactly the same (possibility).\n");
#endif
			done = 1;
		}

	/* If the card data is toggling any bits, this means that the heart
	   beat was detected, or something else about the card is set. */

		if ((card_dat != prev_card_dat) && (!done)) {
			done = 1;
			found = 1;
#ifdef	DEBUG
			printk("pcwd: I show alternate heart beats.  Card detected.\n");
#endif
			break;
		}

	/* Otherwise something else strange happened. */

		if (!done)
			count++;
	}

	return((found) ? 1 : 0);
}

void pcwd_showprevstate(void)
{
	int card_status = 0x0000;

	card_status = inb(current_readport);

	if (card_status & WD_WDRST)
		printk("pcwd: Previous reboot was caused by the card.\n");

	if (card_status & WD_T110)
		printk("pcwd: CPU overheat sense.\n");

	if ((!(card_status & WD_WDRST)) &&
	    (!(card_status & WD_T110)))
		printk("pcwd: Cold boot sense.\n");
}

static int pcwd_return_data(void)
{
	return(inb(current_readport));
}

static int pcwd_write(struct inode *inode, struct file *file, const char *data,
	int len)
{
	int wdrst_stat;

	if (!is_open)
		return -EIO;

#ifdef	DEBUG
	printk("pcwd: write request\n");
#endif

	wdrst_stat = inb_p(current_readport);
	wdrst_stat &= 0x0F;

	wdrst_stat |= WD_WDRST;

	outb_p(wdrst_stat, current_ctlport);

	return(1);
}

static int pcwd_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int i, cdat, rv;

	switch(cmd) {
	default:
		return -ENOIOCTLCMD;

	case PCWD_GETSTAT:
		i = verify_area(VERIFY_WRITE, (void*) arg, sizeof(int));
		if (i)
			return i;
		else {
			cdat = pcwd_return_data();
			rv = 0;

			if (cdat & WD_WDRST)
				rv |= 0x01;

			if (cdat & WD_T110)
				rv |= 0x02;

			put_user(rv, (int *) arg);
			return 0;
		}
		break;

	case PCWD_PING:
		pcwd_write(NULL, NULL, NULL, 1);	/* Is this legal? */
		break;
	}

	return 0;
}

static int pcwd_open(struct inode *ino, struct file *filep)
{
#ifdef	DEBUG
	printk("pcwd: open request\n");
#endif

	MOD_INC_USE_COUNT;
	is_eof = 0;
	return(0);
}

static void pcwd_close(struct inode *ino, struct file *filep)
{
#ifdef	DEBUG
	printk("pcwd: close request\n");
#endif

	MOD_DEC_USE_COUNT;
}

static struct file_operations pcwd_fops = {
	NULL,		/* Seek */
	NULL,		/* Read */
	pcwd_write,	/* Write */
	NULL,		/* Readdir */
	NULL,		/* Select */
	pcwd_ioctl,	/* IOctl */
	NULL,		/* MMAP */
	pcwd_open,	/* Open */
	pcwd_close	/* Close */
};

static struct miscdevice pcwd_miscdev = {
	WD_MINOR,
	"pcwatchdog",
	&pcwd_fops
};
 
#ifdef	MODULE
int init_module(void)
#else
int pcwatchdog_init(void)
#endif
{
#ifdef	DEBUG
	printk("pcwd: Success.\n");
#endif
	printk("pcwd: v%s Ken Hollis (khollis@bitgate.com)\n", WD_VER);

#ifdef	DEBUG
	printk("pcwd: About to perform card autosense loop.\n");
#endif

	is_eof = 0;
	is_open = 0;

	current_ctlport = WD_TIMERRESET_PORT1;
	current_readport = WD_CTLSTAT_PORT1;

	if (!pcwd_checkcard()) {
#ifdef	DEBUG
		printk("pcwd: Trying port 0x370.\n");
#endif

		current_ctlport = WD_TIMERRESET_PORT2;
		current_readport = WD_CTLSTAT_PORT2;

		if (!pcwd_checkcard()) {
			printk("pcwd: No card detected, or wrong port assigned.\n");
			return(-EIO);
		} else
			printk("pcwd: Watchdog Rev.A detected at port 0x370\n");
	} else
		printk("pcwd: Watchdog Rev.A detected at port 0x270\n");

	pcwd_showprevstate();

#ifdef	DEBUG
	printk("pcwd: Requesting region entry\n");
#endif

	request_region(current_ctlport, WD_PORT_EXTENT, "PCWD Rev.A (Berkshire)");

#ifdef	DEBUG
	printk("pcwd: character device creation.\n");
#endif

	misc_register(&pcwd_miscdev);

	return 0;
}

#ifdef	MODULE
void cleanup_module(void)
{
	misc_deregister(&pcwd_miscdev);
	release_region(current_ctlport, 2);
#ifdef	DEBUG
	printk("pcwd: Cleanup successful.\n");
#endif
}
#endif

/*
** TODO:
**
**	Both Revisions:
**	o) Support for revision B of the Watchdog Card
**	o) Implement the rest of the IOCTLs as discussed with Alan Cox
**	o) Implement only card heartbeat reset via IOCTL, not via write
**	o) Faster card detection routines
**	o) /proc device creation
**
**	Revision B functions:
**	o) /dev/temp device creation for temperature device (possibly use
**	   the one from the WDT drivers?)
**	o) Direct Motorola controller chip access via read/write routines
**	o) Autoprobe IO Ports for autodetection (possibly by chip detect?)
*/

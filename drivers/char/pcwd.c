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
 * 960825	Made a few changes to code, deleted some defines and made
 *		typedefs to replace them.  Made heartbeat reset only available
 *		via ioctl, and removed the write routine.
 * 960828	Added new items for PC Watchdog Rev.C card.
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/watchdog.h>
#include <asm/io.h>
#include <asm/uaccess.h>

typedef struct pcwd_ioports {
	int first_port;
	int range;
} IOPS;

/*
** These are the auto-probe addresses available for the Rev.A version of the
** PC Watchdog card.
*/

static IOPS pcwd_ioports[] = {
	{ 0x270, 3 },
	{ 0x350, 3 },
	{ 0x370, 3 },
	{ 0x000, 0 }
};

#ifdef DEBUG
#define dprintk(x)	printk(x)
#else
#define dprintk(x)
#endif

#ifdef CONFIG_PCWD_REV_A
#define CARD_REV	"A"
#define PORT_OFFSET	0
#define PORT_RANGE	2
#define WD_WDRST                0x01	/* Previously reset state */
#define WD_T110                 0x02	/* Temperature overheat sense */
#define WD_HRTBT                0x04	/* Heartbeat sense */
#define WD_RLY2                 0x08	/* External relay triggered */
#define WD_SRLY2                0x80	/* Software external relay triggered */
#endif
#ifdef CONFIG_PCWD_REV_C
#define CARD_REV	"C"
#define PORT_OFFSET	1
#define PORT_RANGE	4
#define WD_WDRST                0x01	/* Previously reset state */
#define WD_T110                 0x04	/* Temperature overheat sense */
#endif

#define WD_VER                  "0.52 (08/28/96)"
#define	WD_MINOR		130	/* Minor device number */

#define	WD_TIMEOUT		3	/* 1 1/2 seconds for a timeout */


static int current_readport;
static int is_open, initial_status, supports_temp, mode_debug;

int pcwd_checkcard(void)
{
	int card_dat, prev_card_dat, found = 0, count = 0, done = 0;

	/* As suggested by Alan Cox */
	if (check_region(current_readport, PORT_RANGE)) {
		printk("pcwd: Port 0x%x unavailable.\n", current_readport);
		return 0;
	}

	card_dat = 0x00;
	prev_card_dat = 0x00;

	prev_card_dat = inb(current_readport);
	if (prev_card_dat == 0xFF) {
		dprintk(("pcwd: No card detected at 0x%03x\n", current_readport));
		return 0;
	}

	while(count < WD_TIMEOUT) {
		dprintk(("pcwd: Run #%d on port 0x%03x\n", count, current_readport));

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

			dprintk(("pcwd: I show nothing on this port.\n"));
		}

	/* If there's a heart beat in both instances, then this means we
	   found our card.  This also means that either the card was
	   previously reset, or the computer was power-cycled. */

		if ((card_dat & WD_HRTBT) && (prev_card_dat & WD_HRTBT) &&
			(!done)) {
			found = 1;
			done = 1;
			
			dprintk(("pcwd: I show alternate heart beats.  Card detected.\n"));
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
			dprintk(("pcwd: The card data is exactly the same (possibility).\n"));
			done = 1;
		}

	/* If the card data is toggling any bits, this means that the heart
	   beat was detected, or something else about the card is set. */

		if ((card_dat != prev_card_dat) && (!done)) {
			done = 1;
			found = 1;
			dprintk(("pcwd: I show alternate heart beats.  Card detected.\n"));
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

	initial_status = card_status = inb(current_readport + PORT_OFFSET);

	if (card_status & WD_WDRST)
		printk("pcwd: Previous reboot was caused by the card.\n");

	if (supports_temp)
		if(card_status & WD_T110)
			printk("pcwd: CPU overheat sense.\n");

	if ((!(card_status & WD_WDRST)) &&
	    (!(card_status & WD_T110)))
		printk("pcwd: Cold boot sense.\n");
}

static void pcwd_send_heartbeat(void)
{
	int wdrst_stat;

	if (!is_open)
		return;

	dprintk(("pcwd: heartbeat\n"));

	wdrst_stat = inb_p(current_readport);
	wdrst_stat &= 0x0F;

	wdrst_stat |= WD_WDRST;

	outb_p(wdrst_stat, current_readport + PORT_OFFSET);
	return(1);
}

static int pcwd_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int i, cdat, rv;
	static struct watchdog_ident ident=
	{
		WDIOF_OVERHEAT|WDIOF_CARDRESET,
#ifdef CONFIG_PCWD_REV_A	
		1,
#else
		3,
#endif				
		"PCWD revision "CARD_REV"."
	};
		
	switch(cmd) {
	default:
		return -ENOIOCTLCMD;

	case WDIOC_GETSUPPORT:
		i = verify_area(VERIFY_WRITE, (void*) arg, sizeof(struct watchdog_info));
		if (i)
			return i;
		else
			return copy_to_user(arg, &ident, sizeof(ident));

	case WDIOC_GETSTATUS:
		i = verify_area(VERIFY_WRITE, (void*) arg, sizeof(int));
		if (i)
			return i;
		else {
			cdat = inb(current_readport);
			rv = 0;

			if (cdat & WD_WDRST)
				rv |= WDIOF_CARDRESET;

			if (cdat & WD_T110)
				rv |= WDIOF_OVERHEAT;

			return put_user(rv, (int *) arg);
		}
		break;

	case WDIOC_GETBOOTSTATUS:
		i = verify_area(VERIFY_WRITE, (void*) arg, sizeof(int));
		if (i)
			return i;
		else {
			int rv;
			rv = 0;

			if (initial_status & WD_WDRST)
				rv |= WDIOF_CARDRESET;

			if (initial_status & WD_T110)
				rv |= WDIOF_OVERHEAT;
			return put_user(rv, (int *) arg);
		}
		break;

	case WDIOC_GETTEMP:
		i = verify_area(VERIFY_WRITE, (void*) arg, sizeof(int));
		if (i)
			return i;
		else {
			int rv;

			rv = 0;
			if ((supports_temp) && (mode_debug == 0)) {
				rv = inb(current_readport);
				return put_user(rv, (int*) arg);
			} else
				return put_user(rv, (int*) arg);
		}

	case WDIOC_KEEPALIVE:
		pcwd_send_heartbeat();
		return 0;
	}

	return 0;
}

static long pcwd_write(struct file *file, struct inode *inode, const char *buf, unsigned long len)
{
	if(len)
	{
		pcwd_send_heartbeat();
		return 1;
	}
}

static int pcwd_open(struct inode *ino, struct file *filep)
{
	dprintk(("pcwd: open request\n"));

	MOD_INC_USE_COUNT;
	return(0);
}

static void pcwd_close(struct inode *ino, struct file *filep)
{
	dprintk(("pcwd: close request\n"));

	MOD_DEC_USE_COUNT;
}

static void get_support(void)
{
#ifdef CONFIG_PCWD_REV_C
	if (inb(current_readport) != 0xF0)
#endif	
		supports_temp = 1;
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
	int i, found = 0;

	dprintk(("pcwd: Success.\n"));
	printk(KERN_INFO "pcwd: v%s Ken Hollis (khollis@bitgate.com)\n", WD_VER);

	dprintk(("pcwd: About to perform card autosense loop.\n"));

	/* Initial variables */
	is_open = 0;
	supports_temp = 0;
	mode_debug = 0;
	initial_status = 0x0000;

	dprintk(("pcwd: Revision " CARD_REV " support defined.\n"));

	for (i = 0; pcwd_ioports[i].first_port != 0; i++) {
		current_readport = pcwd_ioports[i].first_port;

	if (!pcwd_checkcard()) {
		dprintk(("pcwd: Trying port 0x%03x.\n", pcwd_ioports[i].first_port));
		if (pcwd_checkcard()) {
			found = 1;
			break;
		}
	}

	if (!found) {
		printk("pcwd: No card detected.\n");
		return(-EIO);
	}

	is_open = 1;

	get_support();

#ifdef	CONFIG_PCWD_REV_A
	printk("pcwd: PC Watchdog (REV.A) detected at port 0x%03x\n", current_readport);
#endif
#ifdef	CONFIG_PCWD_REV_C
	printk("pcwd: PC Watchdog (REV.C) detected at port 0x%03x -%stemp. support\n",
		current_readport, (supports_temp) ? " Has " : " No ");
#endif

#ifdef	CONFIG_PCWD_SHOW_PREVSTAT
  	pcwd_showprevstate();
#endif
	dprintk(("pcwd: Requesting region entry\n"));

	request_region(current_readport, PORT_RANGE, "PCWD Rev." CARD_REV "(Berkshire)");

	dprintk(("pcwd: character device creation.\n"));

	misc_register(&pcwd_miscdev);

	return 0;
}

#ifdef	MODULE
void cleanup_module(void)
{
	misc_deregister(&pcwd_miscdev);
	release_region(current_readport, PORT_RANGE);

	dprintk(("pcwd: Cleanup successful.\n"));
}
#endif

/*
** TODO:
**
**	Both Revisions:
**	o) Implement the rest of the IOCTLs as discussed with Alan Cox
**	o) Faster card detection routines
**	o) /proc device creation
**
**	Revision B functions:
**	o) /dev/temp device creation for temperature device (possibly use
**	   the one from the WDT drivers?)
**	o) Direct Motorola controller chip access via read/write routines
**	o) Autoprobe IO Ports for autodetection (possibly by chip detect?)
*/

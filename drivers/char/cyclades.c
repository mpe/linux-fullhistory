static char rcsid[] =
"$Revision: 1.36.3.7 $$Date: 1996/04/19 21:06:18 $";
/*
 *  linux/drivers/char/cyclades.c
 *
 * This file contains the driver for the Cyclades Cyclom-Y multiport
 * serial boards.
 *
 * Maintained by Marcio Saito (marcio@cyclades.com) and
 * Randolph Bentson (bentson@grieg.seaslug.org)
 *
 * For Technical support and installation problems, please send e-mail
 * to support@cyclades.com.
 *
 * Much of the design and some of the code came from serial.c
 * which was copyright (C) 1991, 1992  Linus Torvalds.  It was
 * extensively rewritten by Theodore Ts'o, 8/16/92 -- 9/14/92,
 * and then fixed as suggested by Michael K. Johnson 12/12/92.
 *
 * This version does not support shared irq's.
 *
 * This module exports the following rs232 io functions:
 *   int cy_init(void);
 *   int cy_open(struct tty_struct *tty, struct file *filp);
 *
 * $Log: cyclades.c,v $
 * Revision 1.36.3.7  1996/04/19 21:06:18  bentson
 * remove unneeded boot message & fix CLOCAL hardware flow
 * control (Miquel van Smoorenburg <miquels@Q.cistron.nl>);
 * remove unused diagnostic statements; minor 0 is first;
 *
 * Revision 1.36.3.6  1996/03/13 13:21:17  marcio
 * The kernel function vremap (available only in later 1.3.xx kernels)
 * allows the access to memory addresses above the RAM. This revision
 * of the driver supports PCI boards below 1Mb (device id 0x100) and
 * above 1Mb (device id 0x101).
 *
 * Revision 1.36.3.5  1996/03/07 15:20:17  bentson
 * Some global changes to interrupt handling spilled into
 * this driver--mostly unused arguments in system function
 * calls.  Also added change by Marcio Saito which should
 * reduce lost interrupts at startup by fast processors.
 *
 * Revision 1.36.3.4  1995/11/13  20:45:10  bentson
 * Changes by Corey Minyard <minyard@wf-rch.cirr.com> distributed
 * in 1.3.41 kernel to remove a possible race condition, extend
 * some error messages, and let the driver run as a loadable module
 * Change by Alan Wendt <alan@ez0.ezlink.com> to remove a
 * possible race condition.
 * Change by Marcio Saito <marcio@cyclades.com> to fix PCI addressing.
 *
 * Revision 1.36.3.3  1995/11/13  19:44:48  bentson
 * Changes by Linus Torvalds in 1.3.33 kernel distribution
 * required due to reordering of driver initialization.
 * Drivers are now initialized *after* memory management.
 *
 * Revision 1.36.3.2  1995/09/08  22:07:14  bentson
 * remove printk from ISR; fix typo
 *
 * Revision 1.36.3.1  1995/09/01  12:00:42  marcio
 * Minor fixes in the PCI board support. PCI function calls in
 * conditional compilation (CONFIG_PCI). Thanks to Jim Duncan
 * <duncan@okay.com>. "bad serial count" message removed.
 *
 * Revision 1.36.3  1995/08/22  09:19:42  marcio
 * Cyclom-Y/PCI support added. Changes in the cy_init routine and
 * board initialization. Changes in the boot messages. The driver
 * supports up to 4 boards and 64 ports by default.
 *
 * Revision 1.36.1.4  1995/03/29  06:14:14  bentson
 * disambiguate between Cyclom-16Y and Cyclom-32Ye;
 *
 * Revision 1.36.1.3  1995/03/23  22:15:35  bentson
 * add missing break in modem control block in ioctl switch statement
 * (discovered by Michael Edward Chastain <mec@jobe.shell.portal.com>);
 *
 * Revision 1.36.1.2  1995/03/22  19:16:22  bentson
 * make sure CTS flow control is set as soon as possible (thanks
 * to note from David Lambert <lambert@chesapeake.rps.slb.com>);
 *
 * Revision 1.36.1.1  1995/03/13  15:44:43  bentson
 * initialize defaults for receive threshold and stale data timeout;
 * cosmetic changes;
 *
 * Revision 1.36  1995/03/10  23:33:53  bentson
 * added support of chips 4-7 in 32 port Cyclom-Ye;
 * fix cy_interrupt pointer dereference problem
 * (Joe Portman <baron@aa.net>);
 * give better error response if open is attempted on non-existent port
 * (Zachariah Vaum <jchryslr@netcom.com>);
 * correct command timeout (Kenneth Lerman <lerman@@seltd.newnet.com>);
 * conditional compilation for -16Y on systems with fast, noisy bus;
 * comment out diagnostic print function;
 * cleaned up table of base addresses;
 * set receiver time-out period register to correct value,
 * set receive threshold to better default values,
 * set chip timer to more accurate 200 Hz ticking,
 * add code to monitor and modify receive parameters
 * (Rik Faith <faith@cs.unc.edu> Nick Simicich
 * <njs@scifi.emi.net>);
 *
 * Revision 1.35  1994/12/16  13:54:18  steffen
 * additional patch by Marcio Saito for board detection
 * Accidently left out in 1.34
 *
 * Revision 1.34  1994/12/10  12:37:12  steffen
 * This is the corrected version as suggested by Marcio Saito
 *
 * Revision 1.33  1994/12/01  22:41:18  bentson
 * add hooks to support more high speeds directly; add tytso
 * patch regarding CLOCAL wakeups
 *
 * Revision 1.32  1994/11/23  19:50:04  bentson
 * allow direct kernel control of higher signalling rates;
 * look for cards at additional locations
 *
 * Revision 1.31  1994/11/16  04:33:28  bentson
 * ANOTHER fix from Corey Minyard, minyard@wf-rch.cirr.com--
 * a problem in chars_in_buffer has been resolved by some
 * small changes;  this should yield smoother output
 *
 * Revision 1.30  1994/11/16  04:28:05  bentson
 * Fix from Corey Minyard, Internet: minyard@metronet.com,
 * UUCP: minyard@wf-rch.cirr.com, WORK: minyardbnr.ca, to
 * cy_hangup that appears to clear up much (all?) of the
 * DTR glitches; also he's added/cleaned-up diagnostic messages
 *
 * Revision 1.29  1994/11/16  04:16:07  bentson
 * add change proposed by Ralph Sims, ralphs@halcyon.com, to
 * operate higher speeds in same way as other serial ports;
 * add more serial ports (for up to two 16-port muxes).
 *
 * Revision 1.28  1994/11/04  00:13:16  root
 * turn off diagnostic messages
 *
 * Revision 1.27  1994/11/03  23:46:37  root
 * bunch of changes to bring driver into greater conformance
 * with the serial.c driver (looking for missed fixes)
 *
 * Revision 1.26  1994/11/03  22:40:36  root
 * automatic interrupt probing fixed.
 *
 * Revision 1.25  1994/11/03  20:17:02  root
 * start to implement auto-irq
 *
 * Revision 1.24  1994/11/03  18:01:55  root
 * still working on modem signals--trying not to drop DTR
 * during the getty/login processes
 *
 * Revision 1.23  1994/11/03  17:51:36  root
 * extend baud rate support; set receive threshold as function
 * of baud rate; fix some problems with RTS/CTS;
 *
 * Revision 1.22  1994/11/02  18:05:35  root
 * changed arguments to udelay to type long to get
 * delays to be of correct duration
 *
 * Revision 1.21  1994/11/02  17:37:30  root
 * employ udelay (after calibrating loops_per_second earlier
 * in init/main.c) instead of using home-grown delay routines
 *
 * Revision 1.20  1994/11/02  03:11:38  root
 * cy_chars_in_buffer forces a return value of 0 to let
 * login work (don't know why it does); some functions
 * that were returning EFAULT, now executes the code;
 * more work on deciding when to disable xmit interrupts;
 *
 * Revision 1.19  1994/11/01  20:10:14  root
 * define routine to start transmission interrupts (by enabling
 * transmit interrupts); directly enable/disable modem interrupts;
 *
 * Revision 1.18  1994/11/01  18:40:45  bentson
 * Don't always enable transmit interrupts in startup; interrupt on
 * TxMpty instead of TxRdy to help characters get out before shutdown;
 * restructure xmit interrupt to check for chars first and quit if
 * none are ready to go; modem status (MXVRx) is upright, _not_ inverted
 * (to my view);
 *
 * Revision 1.17  1994/10/30  04:39:45  bentson
 * rename serial_driver and callout_driver to cy_serial_driver and
 * cy_callout_driver to avoid linkage interference; initialize
 * info->type to PORT_CIRRUS; ruggedize paranoia test; elide ->port
 * from cyclades_port structure; add paranoia check to cy_close;
 *
 * Revision 1.16  1994/10/30  01:14:33  bentson
 * change major numbers; add some _early_ return statements;
 *
 * Revision 1.15  1994/10/29  06:43:15  bentson
 * final tidying up for clean compile;  enable some error reporting
 *
 * Revision 1.14  1994/10/28  20:30:22  Bentson
 * lots of changes to drag the driver towards the new tty_io
 * structures and operation.  not expected to work, but may
 * compile cleanly.
 *
 * Revision 1.13  1994/07/21  23:08:57  Bentson
 * add some diagnostic cruft; support 24 lines (for testing
 * both -8Y and -16Y cards; be more thorough in servicing all
 * chips during interrupt; add "volatile" a few places to
 * circumvent compiler optimizations; fix base & offset
 * computations in block_til_ready (was causing chip 0 to
 * stop operation)
 *
 * Revision 1.12  1994/07/19  16:42:11  Bentson
 * add some hackery for kernel version 1.1.8; expand
 * error messages; refine timing for delay loops and
 * declare loop params volatile
 *
 * Revision 1.11  1994/06/11  21:53:10  bentson
 * get use of save_car right in transmit interrupt service
 *
 * Revision 1.10.1.1  1994/06/11  21:31:18  bentson
 * add some diagnostic printing; try to fix save_car stuff
 *
 * Revision 1.10  1994/06/11  20:36:08  bentson
 * clean up compiler warnings
 *
 * Revision 1.9  1994/06/11  19:42:46  bentson
 * added a bunch of code to support modem signalling
 *
 * Revision 1.8  1994/06/11  17:57:07  bentson
 * recognize break & parity error
 *
 * Revision 1.7  1994/06/05  05:51:34  bentson
 * Reorder baud table to be monotonic; add cli to CP; discard
 * incoming characters and status if the line isn't open; start to
 * fold code into cy_throttle; start to port get_serial_info,
 * set_serial_info, get_modem_info, set_modem_info, and send_break
 * from serial.c; expand cy_ioctl; relocate and expand config_setup;
 * get flow control characters from tty struct; invalidate ports w/o
 * hardware;
 *
 * Revision 1.6  1994/05/31  18:42:21  bentson
 * add a loop-breaker in the interrupt service routine;
 * note when port is initialized so that it can be shut
 * down under the right conditions; receive works without
 * any obvious errors
 *
 * Revision 1.5  1994/05/30  00:55:02  bentson
 * transmit works without obvious errors
 *
 * Revision 1.4  1994/05/27  18:46:27  bentson
 * incorporated more code from lib_y.c; can now print short
 * strings under interrupt control to port zero; seems to
 * select ports/channels/lines correctly
 *
 * Revision 1.3  1994/05/25  22:12:44  bentson
 * shifting from multi-port on a card to proper multiplexor
 * data structures;  added skeletons of most routines
 *
 * Revision 1.2  1994/05/19  13:21:43  bentson
 * start to crib from other sources
 *
 */

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/cyclades.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#define small_delay(x) for(j=0;j<x;j++)k++;


#define SERIAL_PARANOIA_CHECK
#undef  SERIAL_DEBUG_OPEN
#undef  SERIAL_DEBUG_THROTTLE
#undef  SERIAL_DEBUG_OTHER
#undef  SERIAL_DEBUG_IO
#undef  SERIAL_DEBUG_COUNT
#undef  SERIAL_DEBUG_DTR
#undef  CYCLOM_16Y_HACK
#undef  CYCLOM_ENABLE_MONITORING

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

#define WAKEUP_CHARS 256

#define STD_COM_FLAGS (0)

#define SERIAL_TYPE_NORMAL  1
#define SERIAL_TYPE_CALLOUT 2


DECLARE_TASK_QUEUE(tq_cyclades);

struct tty_driver cy_serial_driver, cy_callout_driver;

static volatile int cy_irq_triggered;
static volatile int cy_triggered;
static int cy_wild_int_mask;
static unsigned char *intr_base_addr;


/* This is the address lockup table. The driver will probe for Cyclom-Y/ISA
   boards at all addresses in here. If you want the driver to probe addresses
   in a different address, add it to this table.
   If the driver is probing some other board and causing problems, remove the
   address from this table.  */

static unsigned char *cy_isa_addresses[] = {
	(unsigned char *) 0xD0000,
	(unsigned char *) 0xD2000,
	(unsigned char *) 0xD4000,
	(unsigned char *) 0xD6000,
	(unsigned char *) 0xD8000,
	(unsigned char *) 0xDA000,
	(unsigned char *) 0xDC000,
	(unsigned char *) 0xDE000,
};
#define	NR_ISA_ADDRESSES	(sizeof(cy_isa_addresses)/sizeof(unsigned char *))

/* This is the per-card data structure containing address, irq, number of
   channels, etc. This driver supports a maximum of NR_CARDS cards. If
   you need to install more boards, change this constant in the definition
   below. No other change is necessary to support more boards. */

#define NR_CARDS	4

static struct cyclades_card cy_card[NR_CARDS];

/* This is the per-channel data structure containing pointers, flags
   and variables for the port. This driver supports a maximum of NR_PORTS.
   If the total number of ports is larger than NR_PORTS, change this
   constant in the definition below. No other change is necessary to
   support more boards/ports. */

#define NR_PORTS        64

static struct cyclades_port cy_port[NR_PORTS];

/*  The Cyclom-Ye has placed the sequential chips in non-sequential
 *  address order.  This look-up table overcomes that problem.
 */
static int cy_chip_offset [] =
    { 0x0000,
      0x0400,
      0x0800,
      0x0C00,
      0x0200,
      0x0600,
      0x0A00,
      0x0E00
    };

/* PCI related definitions */

static unsigned short	cy_pci_nboard = 0;
static unsigned short	cy_isa_nboard = 0;
static unsigned short	cy_nboard = 0;
static unsigned short	cy_pci_dev_id[] = {
				PCI_DEVICE_ID_CYCLOM_Y_Lo,/* PCI below 1Mb */
				PCI_DEVICE_ID_CYCLOM_Y_Hi,/* PCI above 1Mb */
				0			/* end of table */
			};

int			cy_detect_isa(void);
int			cy_detect_pci(void);

static int		cy_next_channel = 0;	/* next minor available */

static int serial_refcount;

static struct tty_struct *serial_table[NR_PORTS];
static struct termios *serial_termios[NR_PORTS];
static struct termios *serial_termios_locked[NR_PORTS];

/* This is the per-irq data structure,
   it maps an irq to the corresponding card */

struct cyclades_card	*IRQ_cards[16];


/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the memcpy_fromfs blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static unsigned char *tmp_buf = 0;
static struct semaphore tmp_buf_sem = MUTEX;

/*
 * This is used to look up the divisor speeds and the timeouts
 * We're normally limited to 15 distinct baud rates.  The extra
 * are accessed via settings in info->flags.
 *         0,     1,     2,     3,     4,     5,     6,     7,     8,     9,
 *        10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
 *                                                  HI            VHI
 */
static int baud_table[] = {
           0,    50,    75,   110,   134,   150,   200,   300,   600,  1200,
        1800,  2400,  4800,  9600, 19200, 38400, 57600, 76800,115200,150000,
        0};

static char baud_co[] = {  /* 25 MHz clock option table */
        /* value =>    00    01   02    03    04 */
        /* divide by    8    32   128   512  2048 */
        0x00,  0x04,  0x04,  0x04,  0x04,  0x04,  0x03,  0x03,  0x03,  0x02,
        0x02,  0x02,  0x01,  0x01,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00};

static char baud_bpr[] = {  /* 25 MHz baud rate period table */
        0x00,  0xf5,  0xa3,  0x6f,  0x5c,  0x51,  0xf5,  0xa3,  0x51,  0xa3,
        0x6d,  0x51,  0xa3,  0x51,  0xa3,  0x51,  0x36,  0x29,  0x1b,  0x15};

static char baud_cor3[] = {  /* receive threshold */
        0x0a,  0x0a,  0x0a,  0x0a,  0x0a,  0x0a,  0x0a,  0x0a,  0x0a,  0x0a,
        0x0a,  0x0a,  0x0a,  0x09,  0x09,  0x08,  0x08,  0x08,  0x08,  0x07};



static void shutdown(struct cyclades_port *);
static int startup (struct cyclades_port *);
static void cy_throttle(struct tty_struct *);
static void cy_unthrottle(struct tty_struct *);
static void config_setup(struct cyclades_port *);
#ifdef CYCLOM_SHOW_STATUS
static void show_status(int);
#endif


static inline int
serial_paranoia_check(struct cyclades_port *info,
			kdev_t device, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
    static const char *badmagic =
	"Warning: bad magic number for serial struct (%s) in %s\n";
    static const char *badinfo =
	"Warning: null cyclades_port for (%s) in %s\n";
    static const char *badrange =
	"Warning: cyclades_port out of range for (%s) in %s\n";

    if (!info) {
	printk(badinfo, kdevname(device), routine);
	return 1;
    }

    if( (long)info < (long)(&cy_port[0])
    || (long)(&cy_port[NR_PORTS]) < (long)info ){
	printk(badrange, kdevname(device), routine);
	return 1;
    }

    if (info->magic != CYCLADES_MAGIC) {
	printk(badmagic, kdevname(device), routine);
	return 1;
    }
#endif
	return 0;
} /* serial_paranoia_check */

/* The following diagnostic routines allow the driver to spew
   information on the screen, even (especially!) during interrupts.
 */
void
SP(char *data){
  unsigned long flags;
    save_flags(flags); cli();
        console_print(data);
    restore_flags(flags);
}
void
CP(char data){
  unsigned long flags;
  char scrn[2];
    save_flags(flags); cli();
        scrn[0] = data;
        scrn[1] = '\0';
        console_print(scrn);
    restore_flags(flags);
}/* CP */

void CP1(int data) { (data<10)?  CP(data+'0'): CP(data+'A'-10); }/* CP1 */
void CP2(int data) { CP1((data>>4) & 0x0f); CP1( data & 0x0f); }/* CP2 */
void CP4(int data) { CP2((data>>8) & 0xff); CP2(data & 0xff); }/* CP4 */
void CP8(long data) { CP4((data>>16) & 0xffff); CP4(data & 0xffff); }/* CP8 */

/* This routine waits up to 1000 micro-seconds for the previous
   command to the Cirrus chip to complete and then issues the
   new command.  An error is returned if the previous command
   didn't finish within the time limit.
 */
u_short
write_cy_cmd(u_char *base_addr, u_char cmd, int index)
{
  unsigned long flags;
  volatile int  i;

    save_flags(flags); cli();
	/* Check to see that the previous command has completed */
	for(i = 0 ; i < 100 ; i++){
	    if (base_addr[CyCCR<<index] == 0){
		break;
	    }
	    udelay(10L);
	}
	/* if the CCR never cleared, the previous command
	    didn't finish within the "reasonable time" */
	if ( i == 100 ) {
	    restore_flags(flags);
	    return (-1);
	}

	/* Issue the new command */
	base_addr[CyCCR<<index] = cmd;
    restore_flags(flags);
    return(0);
} /* write_cy_cmd */


/* cy_start and cy_stop provide software output flow control as a
   function of XON/XOFF, software CTS, and other such stuff. */

static void
cy_stop(struct tty_struct *tty)
{
  struct cyclades_card *cinfo;
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  unsigned char *base_addr;
  int chip,channel,index;
  unsigned long flags;

#ifdef SERIAL_DEBUG_OTHER
    printk("cy_stop ttyC%d\n", info->line); /* */
#endif

    if (serial_paranoia_check(info, tty->device, "cy_stop"))
	return;
	
    cinfo = &cy_card[info->card];
    index = cinfo->bus_index;
    channel = info->line - cinfo->first_line;
    chip = channel>>2;
    channel &= 0x03;
    base_addr = (unsigned char*)
                   (cy_card[info->card].base_addr + (cy_chip_offset[chip]<<index));

    save_flags(flags); cli();
        base_addr[CyCAR<<index] = (u_char)(channel & 0x0003); /* index channel */
        base_addr[CySRER<<index] &= ~CyTxMpty;
    restore_flags(flags);

    return;
} /* cy_stop */

static void
cy_start(struct tty_struct *tty)
{
  struct cyclades_card *cinfo;
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  unsigned char *base_addr;
  int chip,channel,index;
  unsigned long flags;

#ifdef SERIAL_DEBUG_OTHER
    printk("cy_start ttyC%d\n", info->line); /* */
#endif

    if (serial_paranoia_check(info, tty->device, "cy_start"))
	return;
	
    cinfo = &cy_card[info->card];
    index = cinfo->bus_index;
    channel = info->line - cinfo->first_line;
    chip = channel>>2;
    channel &= 0x03;
    base_addr = (unsigned char*)
                   (cy_card[info->card].base_addr + (cy_chip_offset[chip]<<index));

    save_flags(flags); cli();
        base_addr[CyCAR<<index] = (u_char)(channel & 0x0003);
        base_addr[CySRER<<index] |= CyTxMpty;
    restore_flags(flags);

    return;
} /* cy_start */


/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver
 * (also known as the "bottom half").  This can be called any
 * number of times for any channel without harm.
 */
static inline void
cy_sched_event(struct cyclades_port *info, int event)
{
    info->event |= 1 << event; /* remember what kind of event and who */
    queue_task_irq_off(&info->tqueue, &tq_cyclades); /* it belongs to */
    mark_bh(CYCLADES_BH);                       /* then trigger event */
} /* cy_sched_event */


static int probe_ready;

/*
 * This interrupt routine is used
 * while we are probing for submarines.
 */
static void
cy_probe(int irq, void *dev_id, struct pt_regs *regs)
{
  int save_xir, save_car;
  int index = 0;	/* probing interrupts is only for ISA */

    if (!probe_ready) {
	*(intr_base_addr + (Cy_ClrIntr<<index)) = 0;
        return;
    }

    cy_irq_triggered = irq;
    cy_triggered |= 1 << irq;

	if(intr_base_addr[CySVRR<<index] != 0) {
	    save_xir = (u_char) intr_base_addr[CyTIR<<index];
	    save_car = intr_base_addr[CyCAR<<index];
	    if ((save_xir & 0x3) != 0){
		SP("channel ");
		CP2(save_xir);
		SP(" requesting unexpected interrupt\n");
	    }
	    intr_base_addr[CyCAR<<index] = (save_xir & 0x3);
	    intr_base_addr[CySRER<<index] &= ~CyTxMpty;
	    intr_base_addr[CyTIR<<index] = (save_xir & 0x3f);
	    intr_base_addr[CyCAR<<index] = (save_car);
	}
	*(intr_base_addr + (Cy_ClrIntr<<index)) = 0; /* Cy_ClrIntr is 0x1800 */
    return;
} /* cy_probe */

/* The real interrupt service routine is called
   whenever the card wants its hand held--chars
   received, out buffer empty, modem change, etc.
 */
static void
cy_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
  struct tty_struct *tty;
  int status;
  struct cyclades_card *cinfo;
  struct cyclades_port *info;
  volatile unsigned char *base_addr, *card_base_addr;
  int chip;
  int save_xir, channel, save_car;
  char data;
  int char_count;
  int outch;
  int i,j,index;
  int too_many;
  int had_work;
  int mdm_change;
  int mdm_status;

    if((cinfo = IRQ_cards[irq]) == 0){
        return; /* spurious interrupt */
    }

    card_base_addr = (unsigned char *)cinfo->base_addr;
    index = cinfo->bus_index;

    /* This loop checks all chips in the card.  Make a note whenever
       _any_ chip had some work to do, as this is considered an
       indication that there will be more to do.  Only when no chip
       has any work does this outermost loop exit.
     */
    do{
        had_work = 0;
        for ( chip = 0 ; chip < cinfo->num_chips ; chip ++) {
	    base_addr = (unsigned char *)
	                   (cinfo->base_addr + (cy_chip_offset[chip]<<index));
            too_many = 0;
            while ( (status = base_addr[CySVRR<<index]) != 0x00) {
                had_work++;
                /* The purpose of the following test is to ensure that
                   no chip can monopolize the driver.  This forces the
                   chips to be checked in a round-robin fashion (after
                   draining each of a bunch (1000) of characters).
		 */
                if(1000<too_many++){
                    break;
                }
                if (status & CySRReceive) {      /* reception interrupt */
                    /* determine the channel and change to that context */
                    save_xir = (u_char) base_addr[CyRIR<<index];
                    channel = (u_short ) (save_xir & CyIRChannel);
                    i = channel + chip * 4 + cinfo->first_line;
                    info = &cy_port[i];
                    info->last_active = jiffies;
                    save_car = base_addr[CyCAR<<index];
                    base_addr[CyCAR<<index] = save_xir;

                    /* if there is nowhere to put the data, discard it */
                    if(info->tty == 0){
                        j = (base_addr[CyRIVR<<index] & CyIVRMask);
                        if ( j == CyIVRRxEx ) { /* exception */
                            data = base_addr[CyRDSR<<index];
                        } else { /* normal character reception */
                            char_count = base_addr[CyRDCR<<index];
                            while(char_count--){
                                data = base_addr[CyRDSR<<index];
                            }
                        }
                    }else{ /* there is an open port for this data */
                        tty = info->tty;
                        j = (base_addr[CyRIVR<<index] & CyIVRMask);
                        if ( j == CyIVRRxEx ) { /* exception */
                            data = base_addr[CyRDSR<<index];
                            if(data & info->ignore_status_mask){
                                continue;
                            }
                            if (tty->flip.count < TTY_FLIPBUF_SIZE){
				tty->flip.count++;
				if (data & info->read_status_mask){
				    if(data & CyBREAK){
					*tty->flip.flag_buf_ptr++ =
								TTY_BREAK;
					*tty->flip.char_buf_ptr++ =
							base_addr[CyRDSR<<index];
					if (info->flags & ASYNC_SAK){
					    do_SAK(tty);
					}
				    }else if(data & CyFRAME){
					*tty->flip.flag_buf_ptr++ =
								TTY_FRAME;
					*tty->flip.char_buf_ptr++ =
							base_addr[CyRDSR<<index];
				    }else if(data & CyPARITY){
					*tty->flip.flag_buf_ptr++ =
								TTY_PARITY;
					*tty->flip.char_buf_ptr++ =
							base_addr[CyRDSR<<index];
				    }else if(data & CyOVERRUN){
					*tty->flip.flag_buf_ptr++ =
								TTY_OVERRUN;
					*tty->flip.char_buf_ptr++ = 0;
					/* If the flip buffer itself is
					   overflowing, we still loose
					   the next incoming character.
					 */
					if(tty->flip.count < TTY_FLIPBUF_SIZE){
					    tty->flip.count++;
					    *tty->flip.flag_buf_ptr++ =
					                         TTY_NORMAL;
					    *tty->flip.char_buf_ptr++ =
							base_addr[CyRDSR<<index];
					}
				    /* These two conditions may imply */
				    /* a normal read should be done. */
				    /* }else if(data & CyTIMEOUT){ */
				    /* }else if(data & CySPECHAR){ */
				    }else{
					*tty->flip.flag_buf_ptr++ = 0;
					*tty->flip.char_buf_ptr++ = 0;
				    }
				}else{
				    *tty->flip.flag_buf_ptr++ = 0;
				    *tty->flip.char_buf_ptr++ = 0;
				}
                            }else{
                                /* there was a software buffer overrun
				    and nothing could be done about it!!! */
                            }
                        } else { /* normal character reception */
                            /* load # characters available from the chip */
                            char_count = base_addr[CyRDCR<<index];

#ifdef CYCLOM_ENABLE_MONITORING
			    ++info->mon.int_count;
			    info->mon.char_count += char_count;
			    if (char_count > info->mon.char_max)
			       info->mon.char_max = char_count;
			    info->mon.char_last = char_count;
#endif
                            while(char_count--){
				if (tty->flip.count >= TTY_FLIPBUF_SIZE){
                                        break;
                                }
				tty->flip.count++;
                                data = base_addr[CyRDSR<<index];
				*tty->flip.flag_buf_ptr++ = TTY_NORMAL;
				*tty->flip.char_buf_ptr++ = data;
#ifdef CYCLOM_16Y_HACK
				udelay(10L);
#endif
                            }
                        }
                        queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
                    }
                    /* end of service */
                    base_addr[CyRIR<<index] = (save_xir & 0x3f);
                    base_addr[CyCAR<<index] = (save_car);
                }


                if (status & CySRTransmit) {     /* transmission interrupt */
                    /* Since we only get here when the transmit buffer is empty,
                        we know we can always stuff a dozen characters. */

                    /* determine the channel and change to that context */
                    save_xir = (u_char) base_addr[CyTIR<<index];
                    channel = (u_short ) (save_xir & CyIRChannel);
                    i = channel + chip * 4 + cinfo->first_line;
                    save_car = base_addr[CyCAR<<index];
                    base_addr[CyCAR<<index] = save_xir;

                    /* validate the port number (as configured and open) */
                    if( (i < 0) || (NR_PORTS <= i) ){
                        base_addr[CySRER<<index] &= ~CyTxMpty;
                        goto txend;
                    }
                    info = &cy_port[i];
                    info->last_active = jiffies;
                    if(info->tty == 0){
                        base_addr[CySRER<<index] &= ~CyTxMpty;
                        goto txdone;
                    }

                    /* load the on-chip space available for outbound data */
                    char_count = info->xmit_fifo_size;


                    if(info->x_char) { /* send special char */
                        outch = info->x_char;
                        base_addr[CyTDR<<index] = outch;
                        char_count--;
                        info->x_char = 0;
                    }

		    if (info->x_break){
			/*  The Cirrus chip requires the "Embedded Transmit
			    Commands" of start break, delay, and end break
			    sequences to be sent.  The duration of the
			    break is given in TICs, which runs at HZ
			    (typically 100) and the PPR runs at 200 Hz,
			    so the delay is duration * 200/HZ, and thus a
			    break can run from 1/100 sec to about 5/4 sec.
			 */
			base_addr[CyTDR<<index] = 0; /* start break */
			base_addr[CyTDR<<index] = 0x81;
			base_addr[CyTDR<<index] = 0; /* delay a bit */
			base_addr[CyTDR<<index] = 0x82;
			base_addr[CyTDR<<index] = info->x_break*200/HZ;
			base_addr[CyTDR<<index] = 0; /* terminate break */
			base_addr[CyTDR<<index] = 0x83;
			char_count -= 7;
			info->x_break = 0;
		    }

                    while (char_count-- > 0){
                        if (!info->xmit_cnt){
			    base_addr[CySRER<<index] &= ~CyTxMpty;
			    goto txdone;
                        }
			if (info->xmit_buf == 0){
			    base_addr[CySRER<<index] &= ~CyTxMpty;
			    goto txdone;
			}
			if (info->tty->stopped || info->tty->hw_stopped){
			    base_addr[CySRER<<index] &= ~CyTxMpty;
			    goto txdone;
			}
                        /* Because the Embedded Transmit Commands have been
                           enabled, we must check to see if the escape
                           character, NULL, is being sent.  If it is, we
                           must ensure that there is room for it to be
                           doubled in the output stream.  Therefore we
                           no longer advance the pointer when the character
                           is fetched, but rather wait until after the check
                           for a NULL output character. (This is necessary
                           because there may not be room for the two chars
                           needed to send a NULL.
		         */
                        outch = info->xmit_buf[info->xmit_tail];
                        if( outch ){
			    info->xmit_cnt--;
			    info->xmit_tail = (info->xmit_tail + 1)
						      & (PAGE_SIZE - 1);
			    base_addr[CyTDR<<index] = outch;
                        }else{
                            if(char_count > 1){
				info->xmit_cnt--;
				info->xmit_tail = (info->xmit_tail + 1)
							  & (PAGE_SIZE - 1);
				base_addr[CyTDR<<index] = outch;
				base_addr[CyTDR<<index] = 0;
				char_count--;
                            }else{
                            }
                        }
                    }

        txdone:
                    if (info->xmit_cnt < WAKEUP_CHARS) {
                        cy_sched_event(info, Cy_EVENT_WRITE_WAKEUP);
                    }

        txend:
                    /* end of service */
                    base_addr[CyTIR<<index] = (save_xir & 0x3f);
                    base_addr[CyCAR<<index] = (save_car);
                }

                if (status & CySRModem) {        /* modem interrupt */

                    /* determine the channel and change to that context */
                    save_xir = (u_char) base_addr[CyMIR<<index];
                    channel = (u_short ) (save_xir & CyIRChannel);
                    info = &cy_port[channel + chip * 4 + cinfo->first_line];
                    info->last_active = jiffies;
                    save_car = base_addr[CyCAR<<index];
                    base_addr[CyCAR<<index] = save_xir;

                    mdm_change = base_addr[CyMISR<<index];
                    mdm_status = base_addr[CyMSVR1<<index];

                    if(info->tty == 0){ /* nowhere to put the data, ignore it */
                        ;
                    }else{
                        if((mdm_change & CyDCD)
                        && (info->flags & ASYNC_CHECK_CD)){
                            if(mdm_status & CyDCD){
                                cy_sched_event(info, Cy_EVENT_OPEN_WAKEUP);
                            }else if(!((info->flags & ASYNC_CALLOUT_ACTIVE)
                                     &&(info->flags & ASYNC_CALLOUT_NOHUP))){
                                cy_sched_event(info, Cy_EVENT_HANGUP);
                            }
                        }
                        if((mdm_change & CyCTS)
                        && (info->flags & ASYNC_CTS_FLOW)){
                            if(info->tty->hw_stopped){
                                if(mdm_status & CyCTS){
                                    /* !!! cy_start isn't used because... */
                                    info->tty->hw_stopped = 0;
				    base_addr[CySRER<<index] |= CyTxMpty;
				    cy_sched_event(info, Cy_EVENT_WRITE_WAKEUP);
                                }
                            }else{
                                if(!(mdm_status & CyCTS)){
                                    /* !!! cy_stop isn't used because... */
                                    info->tty->hw_stopped = 1;
				    base_addr[CySRER<<index] &= ~CyTxMpty;
                                }
                            }
                        }
                        if(mdm_status & CyDSR){
                        }
                        if(mdm_status & CyRI){
                        }
                    }
                    /* end of service */
                    base_addr[CyMIR<<index] = (save_xir & 0x3f);
                    base_addr[CyCAR<<index] = save_car;
                }
            }          /* end while status != 0 */
        }            /* end loop for chips... */
    } while(had_work);

   /* clear interrupts */
   *(card_base_addr + (Cy_ClrIntr<<index)) = 0; /* Cy_ClrIntr is 0x1800 */

} /* cy_interrupt */

/*
 * This routine is used to handle the "bottom half" processing for the
 * serial driver, known also the "software interrupt" processing.
 * This processing is done at the kernel interrupt level, after the
 * cy_interrupt() has returned, BUT WITH INTERRUPTS TURNED ON.  This
 * is where time-consuming activities which can not be done in the
 * interrupt driver proper are done; the interrupt driver schedules
 * them using cy_sched_event(), and they get done here.
 *
 * This is done through one level of indirection--the task queue.
 * When a hardware interrupt service routine wants service by the
 * driver's bottom half, it enqueues the appropriate tq_struct (one
 * per port) to the tq_cyclades work queue and sets a request flag
 * via mark_bh for processing that queue.  When the time is right,
 * do_cyclades_bh is called (because of the mark_bh) and it requests
 * that the work queue be processed.
 *
 * Although this may seem unwieldy, it gives the system a way to
 * pass an argument (in this case the pointer to the cyclades_port
 * structure) to the bottom half of the driver.  Previous kernels
 * had to poll every port to see if that port needed servicing.
 */
static void
do_cyclades_bh(void)
{
    run_task_queue(&tq_cyclades);
} /* do_cyclades_bh */

static void
do_softint(void *private_)
{
  struct cyclades_port *info = (struct cyclades_port *) private_;
  struct tty_struct    *tty;

    tty = info->tty;
    if (!tty)
	return;

    if (clear_bit(Cy_EVENT_HANGUP, &info->event)) {
	tty_hangup(info->tty);
	wake_up_interruptible(&info->open_wait);
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|
			     ASYNC_CALLOUT_ACTIVE);
    }
    if (clear_bit(Cy_EVENT_OPEN_WAKEUP, &info->event)) {
	wake_up_interruptible(&info->open_wait);
    }
    if (clear_bit(Cy_EVENT_WRITE_WAKEUP, &info->event)) {
	if((tty->flags & (1<< TTY_DO_WRITE_WAKEUP))
	&& tty->ldisc.write_wakeup){
	    (tty->ldisc.write_wakeup)(tty);
	}
	wake_up_interruptible(&tty->write_wait);
    }
} /* do_softint */


/*
 * Grab all interrupts in preparation for doing an automatic irq
 * detection.  dontgrab is a mask of irq's _not_ to grab.  Returns a
 * mask of irq's which were grabbed and should therefore be freed
 * using free_all_interrupts().
 */
static int
grab_all_interrupts(int dontgrab)
{
  int irq_lines = 0;
  int i, mask;
    
    for (i = 0, mask = 1; i < 16; i++, mask <<= 1) {
	if (!(mask & dontgrab)
	&& !request_irq(i, cy_probe, SA_INTERRUPT, "serial probe", NULL)) {
	    irq_lines |= mask;
	}
    }
    return irq_lines;
} /* grab_all_interrupts */

/*
 * Release all interrupts grabbed by grab_all_interrupts
 */
static void
free_all_interrupts(int irq_lines)
{
  int i;
    
    for (i = 0; i < 16; i++) {
	if (irq_lines & (1 << i))
	    free_irq(i,NULL);
    }
} /* free_all_interrupts */

/*
 * This routine returns a bitfield of "wild interrupts".  Basically,
 * any unclaimed interrupts which is flapping around.
 */
static int
check_wild_interrupts(void)
{
  int	i, mask;
  int	wild_interrupts = 0;
  int	irq_lines;
  unsigned long timeout;
  unsigned long flags;
	
    /*Turn on interrupts (they may be off) */
    save_flags(flags); sti();

	irq_lines = grab_all_interrupts(0);
       
	/*
	 * Delay for 0.1 seconds -- we use a busy loop since this may 
	 * occur during the bootup sequence
	 */
	timeout = jiffies+10;
	while (timeout >= jiffies)
	    ;
	
	cy_triggered = 0;	/* Reset after letting things settle */

	timeout = jiffies+10;
	while (timeout >= jiffies)
		;
	
	for (i = 0, mask = 1; i < 16; i++, mask <<= 1) {
	    if ((cy_triggered & (1 << i)) &&
		(irq_lines & (1 << i))) {
		    wild_interrupts |= mask;
	    }
	}
	free_all_interrupts(irq_lines);
    restore_flags(flags);
    return wild_interrupts;
} /* check_wild_interrupts */

/*
 * This routine is called by do_auto_irq(); it attempts to determine
 * which interrupt a serial port is configured to use.  It is not
 * fool-proof, but it works a large part of the time.
 */
static int
get_auto_irq(unsigned char *address)
{
  unsigned long timeout;
  unsigned char *base_addr;
  int		index;

    index = 0;	/* IRQ probing is only for ISA */
    base_addr = address;
    intr_base_addr = address;
	
    /*
     * Enable interrupts and see who answers
     */
    cy_irq_triggered = 0;
    cli();
	base_addr[CyCAR<<index] = 0;
	write_cy_cmd(base_addr,CyCHAN_CTL|CyENB_XMTR,index);
	base_addr[CySRER<<index] |= CyTxMpty;
	probe_ready = 1;
    sti();
    
    timeout = jiffies+2;
    while (timeout >= jiffies) {
	if (cy_irq_triggered)
	    break;
    }
    probe_ready = 0;
    return(cy_irq_triggered);
} /* get_auto_irq */

/*
 * Calls get_auto_irq() multiple times, to make sure we don't get
 * faked out by random interrupts
 */
static int
do_auto_irq(unsigned char *address)
{
  int 			irq_lines = 0;
  int			irq_try_1 = 0, irq_try_2 = 0;
  int			retries;
  unsigned long flags;

    /* Turn on interrupts (they may be off) */
    save_flags(flags); sti();

	probe_ready = 0;

        cy_wild_int_mask = check_wild_interrupts();

	irq_lines = grab_all_interrupts(cy_wild_int_mask);
	
	for (retries = 0; retries < 5; retries++) {
	    if (!irq_try_1)
		irq_try_1 = get_auto_irq(address);
	    if (!irq_try_2)
		irq_try_2 = get_auto_irq(address);
	    if (irq_try_1 && irq_try_2) {
		if (irq_try_1 == irq_try_2)
		    break;
		irq_try_1 = irq_try_2 = 0;
	    }
	}
    restore_flags(flags);
    free_all_interrupts(irq_lines);
    return (irq_try_1 == irq_try_2) ? irq_try_1 : 0;
} /* do_auto_irq */


/* This is called whenever a port becomes active;
   interrupts are enabled and DTR & RTS are turned on.
 */
static int
startup(struct cyclades_port * info)
{
  unsigned long flags;
  unsigned char *base_addr;
  int card,chip,channel,index;

    if (info->flags & ASYNC_INITIALIZED){
	return 0;
    }

    if (!info->type){
	if (info->tty){
	    set_bit(TTY_IO_ERROR, &info->tty->flags);
	}
	return 0;
    }
    if (!info->xmit_buf){
	info->xmit_buf = (unsigned char *) get_free_page (GFP_KERNEL);
	if (!info->xmit_buf){
	    return -ENOMEM;
	}
    }

    config_setup(info);

    card = info->card;
    channel = (info->line) - (cy_card[card].first_line);
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

#ifdef SERIAL_DEBUG_OPEN
    printk("startup card %d, chip %d, channel %d, base_addr %lx",
	 card, chip, channel, (long)base_addr);/**/
#endif

    save_flags(flags); cli();
	base_addr[CyCAR<<index] = (u_char)channel;

	base_addr[CyRTPR<<index] = (info->default_timeout
			     ? info->default_timeout
			     : 0x02); /* 10ms rx timeout */

	write_cy_cmd(base_addr,CyCHAN_CTL|CyENB_RCVR|CyENB_XMTR,index);

	base_addr[CyCAR<<index] = (u_char)channel; /* !!! Is this needed? */
	base_addr[CyMSVR1<<index] = CyRTS;
	base_addr[CyMSVR2<<index] = CyDTR;

#ifdef SERIAL_DEBUG_DTR
        printk("cyc: %d: raising DTR\n", __LINE__);
        printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif

	base_addr[CySRER<<index] |= CyRxData;
	info->flags |= ASYNC_INITIALIZED;

	if (info->tty){
	    clear_bit(TTY_IO_ERROR, &info->tty->flags);
	}
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

    restore_flags(flags);

#ifdef SERIAL_DEBUG_OPEN
    printk(" done\n");
#endif
    return 0;
} /* startup */

void
start_xmit( struct cyclades_port *info )
{
  unsigned long flags;
  unsigned char *base_addr;
  int card,chip,channel,index;

    card = info->card;
    channel = (info->line) - (cy_card[card].first_line);
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

    save_flags(flags); cli();
	base_addr[CyCAR<<index] = channel;
	base_addr[CySRER<<index] |= CyTxMpty;
    restore_flags(flags);
} /* start_xmit */

/*
 * This routine shuts down a serial port; interrupts are disabled,
 * and DTR is dropped if the hangup on close termio flag is on.
 */
static void
shutdown(struct cyclades_port * info)
{
  unsigned long flags;
  unsigned char *base_addr;
  int card,chip,channel,index;

    if (!(info->flags & ASYNC_INITIALIZED)){
	return;
    }

    card = info->card;
    channel = info->line - cy_card[card].first_line;
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

#ifdef SERIAL_DEBUG_OPEN
    printk("shutdown card %d, chip %d, channel %d, base_addr %lx\n",
	    card, chip, channel, (long)base_addr);
#endif

    /* !!! REALLY MUST WAIT FOR LAST CHARACTER TO BE
       SENT BEFORE DROPPING THE LINE !!!  (Perhaps
       set some flag that is read when XMTY happens.)
       Other choices are to delay some fixed interval
       or schedule some later processing.
     */
    save_flags(flags); cli();
	if (info->xmit_buf){
            unsigned char * temp;
            temp = info->xmit_buf;
            info->xmit_buf = 0;
	    free_page((unsigned long) temp);
	}

	base_addr[CyCAR<<index] = (u_char)channel;
	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
	    base_addr[CyMSVR1<<index] = ~CyRTS;
	    base_addr[CyMSVR2<<index] = ~CyDTR;
#ifdef SERIAL_DEBUG_DTR
            printk("cyc: %d: dropping DTR\n", __LINE__);
            printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif
        }
	write_cy_cmd(base_addr,CyCHAN_CTL|CyDIS_RCVR,index);
         /* it may be appropriate to clear _XMIT at
           some later date (after testing)!!! */

	if (info->tty){
	    set_bit(TTY_IO_ERROR, &info->tty->flags);
	}
	info->flags &= ~ASYNC_INITIALIZED;
    restore_flags(flags);

#ifdef SERIAL_DEBUG_OPEN
    printk(" done\n");
#endif
    return;
} /* shutdown */

/*
 * This routine finds or computes the various line characteristics.
 */
static void
config_setup(struct cyclades_port * info)
{
  unsigned long flags;
  unsigned char *base_addr;
  int card,chip,channel,index;
  unsigned cflag;
  int   i;

    if (!info->tty || !info->tty->termios){
        return;
    }
    if (info->line == -1){
        return;
    }
    cflag = info->tty->termios->c_cflag;

    /* baud rate */
    i = cflag & CBAUD;
#ifdef CBAUDEX
/* Starting with kernel 1.1.65, there is direct support for
   higher baud rates.  The following code supports those
   changes.  The conditional aspect allows this driver to be
   used for earlier as well as later kernel versions.  (The
   mapping is slightly different from serial.c because there
   is still the possibility of supporting 75 kbit/sec with
   the Cyclades board.)
 */
    if (i & CBAUDEX) {
	if (i == B57600)
	    i = 16;
	else if(i == B115200) 
	    i = 18;
#ifdef B78600
	else if(i == B78600) 
	    i = 17;
#endif
	else
	    info->tty->termios->c_cflag &= ~CBAUDEX;
    }
#endif
    if (i == 15) {
	    if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
		    i += 1;
	    if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
		    i += 3;
    }
    info->tbpr = baud_bpr[i]; /* Tx BPR */
    info->tco = baud_co[i]; /* Tx CO */
    info->rbpr = baud_bpr[i]; /* Rx BPR */
    info->rco = baud_co[i]; /* Rx CO */
    if (baud_table[i] == 134) {
        info->timeout = (info->xmit_fifo_size*HZ*30/269) + 2;
        /* get it right for 134.5 baud */
    } else if (baud_table[i]) {
        info->timeout = (info->xmit_fifo_size*HZ*15/baud_table[i]) + 2;
        /* this needs to be propagated into the card info */
    } else {
        info->timeout = 0;
    }
    /* By tradition (is it a standard?) a baud rate of zero
       implies the line should be/has been closed.  A bit
       later in this routine such a test is performed. */

    /* byte size and parity */
    info->cor5 = 0;
    info->cor4 = 0;
    info->cor3 = (info->default_threshold
		  ? info->default_threshold
		  : baud_cor3[i]); /* receive threshold */
    info->cor2 = CyETC;
    switch(cflag & CSIZE){
    case CS5:
        info->cor1 = Cy_5_BITS;
        break;
    case CS6:
        info->cor1 = Cy_6_BITS;
        break;
    case CS7:
        info->cor1 = Cy_7_BITS;
        break;
    case CS8:
        info->cor1 = Cy_8_BITS;
        break;
    }
    if(cflag & CSTOPB){
        info->cor1 |= Cy_2_STOP;
    }
    if (cflag & PARENB){
        if (cflag & PARODD){
            info->cor1 |= CyPARITY_O;
        }else{
            info->cor1 |= CyPARITY_E;
        }
    }else{
        info->cor1 |= CyPARITY_NONE;
    }
	
    /* CTS flow control flag */
    if (cflag & CRTSCTS){
	info->flags |= ASYNC_CTS_FLOW;
	info->cor2 |= CyCtsAE;
    }else{
	info->flags &= ~ASYNC_CTS_FLOW;
	info->cor2 &= ~CyCtsAE;
    }
    if (cflag & CLOCAL)
	info->flags &= ~ASYNC_CHECK_CD;
    else
	info->flags |= ASYNC_CHECK_CD;

     /***********************************************
	The hardware option, CyRtsAO, presents RTS when
	the chip has characters to send.  Since most modems
	use RTS as reverse (inbound) flow control, this
	option is not used.  If inbound flow control is
	necessary, DTR can be programmed to provide the
	appropriate signals for use with a non-standard
	cable.  Contact Marcio Saito for details.
     ***********************************************/

    card = info->card;
    channel = (info->line) - (cy_card[card].first_line);
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

    save_flags(flags); cli();
	base_addr[CyCAR<<index] = (u_char)channel;

       /* tx and rx baud rate */

	base_addr[CyTCOR<<index] = info->tco;
	base_addr[CyTBPR<<index] = info->tbpr;
	base_addr[CyRCOR<<index] = info->rco;
	base_addr[CyRBPR<<index] = info->rbpr;

	/* set line characteristics  according configuration */

	base_addr[CySCHR1<<index] = START_CHAR(info->tty);
	base_addr[CySCHR2<<index] = STOP_CHAR(info->tty);
	base_addr[CyCOR1<<index] = info->cor1;
	base_addr[CyCOR2<<index] = info->cor2;
	base_addr[CyCOR3<<index] = info->cor3;
	base_addr[CyCOR4<<index] = info->cor4;
	base_addr[CyCOR5<<index] = info->cor5;

	write_cy_cmd(base_addr,CyCOR_CHANGE|CyCOR1ch|CyCOR2ch|CyCOR3ch,index);

	base_addr[CyCAR<<index] = (u_char)channel; /* !!! Is this needed? */

	base_addr[CyRTPR<<index] = (info->default_timeout
			     ? info->default_timeout
			     : 0x02); /* 10ms rx timeout */

	if (C_CLOCAL(info->tty)) {
	    base_addr[CySRER<<index] |= CyMdmCh; /* without modem intr */
				    /* act on 1->0 modem transitions */
	    base_addr[CyMCOR1<<index] = CyCTS;
				    /* act on 0->1 modem transitions */
	    base_addr[CyMCOR2<<index] = CyCTS;
	} else {
	    base_addr[CySRER<<index] |= CyMdmCh; /* with modem intr */
				    /* act on 1->0 modem transitions */
	    base_addr[CyMCOR1<<index] = CyDSR|CyCTS|CyRI|CyDCD;
				    /* act on 0->1 modem transitions */
	    base_addr[CyMCOR2<<index] = CyDSR|CyCTS|CyRI|CyDCD;
	}

	if(i == 0){ /* baud rate is zero, turn off line */
	    base_addr[CyMSVR2<<index] = ~CyDTR;
#ifdef SERIAL_DEBUG_DTR
            printk("cyc: %d: dropping DTR\n", __LINE__);
            printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif
	}else{
	    base_addr[CyMSVR2<<index] = CyDTR;
#ifdef SERIAL_DEBUG_DTR
            printk("cyc: %d: raising DTR\n", __LINE__);
            printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif
	}

	if (info->tty){
	    clear_bit(TTY_IO_ERROR, &info->tty->flags);
	}

    restore_flags(flags);

} /* config_setup */


static void
cy_put_char(struct tty_struct *tty, unsigned char ch)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  unsigned long flags;

#ifdef SERIAL_DEBUG_IO
    printk("cy_put_char ttyC%d\n", info->line);
#endif

    if (serial_paranoia_check(info, tty->device, "cy_put_char"))
	return;

    if (!tty || !info->xmit_buf)
	return;

    save_flags(flags); cli();
	if (info->xmit_cnt >= PAGE_SIZE - 1) {
	    restore_flags(flags);
	    return;
	}

	info->xmit_buf[info->xmit_head++] = ch;
	info->xmit_head &= PAGE_SIZE - 1;
	info->xmit_cnt++;
    restore_flags(flags);
} /* cy_put_char */


static void
cy_flush_chars(struct tty_struct *tty)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  unsigned long flags;
  unsigned char *base_addr;
  int card,chip,channel,index;
				
#ifdef SERIAL_DEBUG_IO
    printk("cy_flush_chars ttyC%d\n", info->line); /* */
#endif

    if (serial_paranoia_check(info, tty->device, "cy_flush_chars"))
	return;

    if (info->xmit_cnt <= 0 || tty->stopped
    || tty->hw_stopped || !info->xmit_buf)
	return;

    card = info->card;
    channel = info->line - cy_card[card].first_line;
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

    save_flags(flags); cli();
	base_addr[CyCAR<<index] = channel;
	base_addr[CySRER<<index] |= CyTxMpty;
    restore_flags(flags);
} /* cy_flush_chars */


/* This routine gets called when tty_write has put something into
    the write_queue.  If the port is not already transmitting stuff,
    start it off by enabling interrupts.  The interrupt service
    routine will then ensure that the characters are sent.  If the
    port is already active, there is no need to kick it.
 */
static int
cy_write(struct tty_struct * tty, int from_user,
           const unsigned char *buf, int count)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  unsigned long flags;
  int c, total = 0;

#ifdef SERIAL_DEBUG_IO
    printk("cy_write ttyC%d\n", info->line); /* */
#endif

    if (serial_paranoia_check(info, tty->device, "cy_write")){
	return 0;
    }
	
    if (!tty || !info->xmit_buf || !tmp_buf){
        return 0;
    }

    while (1) {
        save_flags(flags); cli();		
	c = MIN(count, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
			   SERIAL_XMIT_SIZE - info->xmit_head));
	if (c <= 0){
	    restore_flags(flags);
	    break;
	}

	if (from_user) {
	    down(&tmp_buf_sem);
	    memcpy_fromfs(tmp_buf, buf, c);
	    c = MIN(c, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
		       SERIAL_XMIT_SIZE - info->xmit_head));
	    memcpy(info->xmit_buf + info->xmit_head, tmp_buf, c);
	    up(&tmp_buf_sem);
	} else
	    memcpy(info->xmit_buf + info->xmit_head, buf, c);
	info->xmit_head = (info->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
	info->xmit_cnt += c;
	restore_flags(flags);
	buf += c;
	count -= c;
	total += c;
    }


    if (info->xmit_cnt
    && !tty->stopped
    && !tty->hw_stopped ) {
        start_xmit(info);
    }
    return total;
} /* cy_write */


static int
cy_write_room(struct tty_struct *tty)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  int	ret;
				
#ifdef SERIAL_DEBUG_IO
    printk("cy_write_room ttyC%d\n", info->line); /* */
#endif

    if (serial_paranoia_check(info, tty->device, "cy_write_room"))
	return 0;
    ret = PAGE_SIZE - info->xmit_cnt - 1;
    if (ret < 0)
	ret = 0;
    return ret;
} /* cy_write_room */


static int
cy_chars_in_buffer(struct tty_struct *tty)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
				
#ifdef SERIAL_DEBUG_IO
    printk("cy_chars_in_buffer ttyC%d %d\n", info->line, info->xmit_cnt); /* */
#endif

    if (serial_paranoia_check(info, tty->device, "cy_chars_in_buffer"))
	return 0;

    return info->xmit_cnt;
} /* cy_chars_in_buffer */


static void
cy_flush_buffer(struct tty_struct *tty)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  unsigned long flags;
				
#ifdef SERIAL_DEBUG_IO
    printk("cy_flush_buffer ttyC%d\n", info->line); /* */
#endif

    if (serial_paranoia_check(info, tty->device, "cy_flush_buffer"))
	return;
    save_flags(flags); cli();
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
    restore_flags(flags);
    wake_up_interruptible(&tty->write_wait);
    if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP))
    && tty->ldisc.write_wakeup)
	(tty->ldisc.write_wakeup)(tty);
} /* cy_flush_buffer */


/* This routine is called by the upper-layer tty layer to signal
   that incoming characters should be throttled or that the
   throttle should be released.
 */
static void
cy_throttle(struct tty_struct * tty)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  unsigned long flags;
  unsigned char *base_addr;
  int card,chip,channel,index;

#ifdef SERIAL_DEBUG_THROTTLE
  char buf[64];
	
    printk("throttle %s: %d....\n", _tty_name(tty, buf),
	   tty->ldisc.chars_in_buffer(tty));
    printk("cy_throttle ttyC%d\n", info->line);
#endif

    if (serial_paranoia_check(info, tty->device, "cy_nthrottle")){
	    return;
    }

    if (I_IXOFF(tty)) {
	info->x_char = STOP_CHAR(tty);
	    /* Should use the "Send Special Character" feature!!! */
    }

    card = info->card;
    channel = info->line - cy_card[card].first_line;
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

    save_flags(flags); cli();
	base_addr[CyCAR<<index] = (u_char)channel;
	base_addr[CyMSVR1<<index] = ~CyRTS;
    restore_flags(flags);

    return;
} /* cy_throttle */


static void
cy_unthrottle(struct tty_struct * tty)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;
  unsigned long flags;
  unsigned char *base_addr;
  int card,chip,channel,index;

#ifdef SERIAL_DEBUG_THROTTLE
  char buf[64];
	
    printk("throttle %s: %d....\n", _tty_name(tty, buf),
	   tty->ldisc.chars_in_buffer(tty));
    printk("cy_unthrottle ttyC%d\n", info->line);
#endif

    if (serial_paranoia_check(info, tty->device, "cy_nthrottle")){
	    return;
    }

    if (I_IXOFF(tty)) {
	info->x_char = START_CHAR(tty);
	/* Should use the "Send Special Character" feature!!! */
    }

    card = info->card;
    channel = info->line - cy_card[card].first_line;
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

    save_flags(flags); cli();
	base_addr[CyCAR<<index] = (u_char)channel;
	base_addr[CyMSVR1<<index] = CyRTS;
    restore_flags(flags);

    return;
} /* cy_unthrottle */

static int
get_serial_info(struct cyclades_port * info,
                           struct serial_struct * retinfo)
{
  struct serial_struct tmp;
  struct cyclades_card *cinfo = &cy_card[info->card];

    if (!retinfo)
            return -EFAULT;
    memset(&tmp, 0, sizeof(tmp));
    tmp.type = info->type;
    tmp.line = info->line;
    tmp.port = info->card * 0x100 + info->line - cinfo->first_line;
    tmp.irq = cinfo->irq;
    tmp.flags = info->flags;
    tmp.baud_base = 0;          /*!!!*/
    tmp.close_delay = info->close_delay;
    tmp.custom_divisor = 0;     /*!!!*/
    tmp.hub6 = 0;               /*!!!*/
    memcpy_tofs(retinfo,&tmp,sizeof(*retinfo));
    return 0;
} /* get_serial_info */

static int
set_serial_info(struct cyclades_port * info,
                           struct serial_struct * new_info)
{
  struct serial_struct new_serial;
  struct cyclades_port old_info;

    if (!new_info)
	    return -EFAULT;
    memcpy_fromfs(&new_serial,new_info,sizeof(new_serial));
    old_info = *info;

    if (!suser()) {
	    if ((new_serial.close_delay != info->close_delay) ||
		((new_serial.flags & ASYNC_FLAGS & ~ASYNC_USR_MASK) !=
		 (info->flags & ASYNC_FLAGS & ~ASYNC_USR_MASK)))
		    return -EPERM;
	    info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			   (new_serial.flags & ASYNC_USR_MASK));
	    goto check_and_exit;
    }


    /*
     * OK, past this point, all the error checking has been done.
     * At this point, we start making changes.....
     */

    info->flags = ((info->flags & ~ASYNC_FLAGS) |
		    (new_serial.flags & ASYNC_FLAGS));
    info->close_delay = new_serial.close_delay;


check_and_exit:
    if (info->flags & ASYNC_INITIALIZED){
	config_setup(info);
	return 0;
    }else{
        return startup(info);
    }
} /* set_serial_info */

static int
get_modem_info(struct cyclades_port * info, unsigned int *value)
{
  int card,chip,channel,index;
  unsigned char *base_addr;
  unsigned long flags;
  unsigned char status;
  unsigned int result;

    card = info->card;
    channel = (info->line) - (cy_card[card].first_line);
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
                   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

    save_flags(flags); cli();
        base_addr[CyCAR<<index] = (u_char)channel;
        status = base_addr[CyMSVR1<<index];
	status |= base_addr[CyMSVR2<<index];
    restore_flags(flags);

    result =  ((status  & CyRTS) ? TIOCM_RTS : 0)
            | ((status  & CyDTR) ? TIOCM_DTR : 0)
            | ((status  & CyDCD) ? TIOCM_CAR : 0)
            | ((status  & CyRI) ? TIOCM_RNG : 0)
            | ((status  & CyDSR) ? TIOCM_DSR : 0)
            | ((status  & CyCTS) ? TIOCM_CTS : 0);
    put_fs_long(result,(unsigned long *) value);
    return 0;
} /* get_modem_info */

static int
set_modem_info(struct cyclades_port * info, unsigned int cmd,
                          unsigned int *value)
{
  int card,chip,channel,index;
  unsigned char *base_addr;
  unsigned long flags;
  unsigned int arg = get_fs_long((unsigned long *) value);

    card = info->card;
    channel = (info->line) - (cy_card[card].first_line);
    chip = channel>>2;
    channel &= 0x03;
    index = cy_card[card].bus_index;
    base_addr = (unsigned char*)
                   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

    switch (cmd) {
    case TIOCMBIS:
	if (arg & TIOCM_RTS){
	    save_flags(flags); cli();
		base_addr[CyCAR<<index] = (u_char)channel;
		base_addr[CyMSVR1<<index] = CyRTS;
	    restore_flags(flags);
	}
	if (arg & TIOCM_DTR){
	    save_flags(flags); cli();
	    base_addr[CyCAR<<index] = (u_char)channel;
	    base_addr[CyMSVR2<<index] = CyDTR;
#ifdef SERIAL_DEBUG_DTR
            printk("cyc: %d: raising DTR\n", __LINE__);
            printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif
	    restore_flags(flags);
	}
	break;
    case TIOCMBIC:
	if (arg & TIOCM_RTS){
	    save_flags(flags); cli();
		base_addr[CyCAR<<index] = (u_char)channel;
		base_addr[CyMSVR1<<index] = ~CyRTS;
	    restore_flags(flags);
	}
	if (arg & TIOCM_DTR){
	    save_flags(flags); cli();
	    base_addr[CyCAR<<index] = (u_char)channel;
	    base_addr[CyMSVR2<<index] = ~CyDTR;
#ifdef SERIAL_DEBUG_DTR
            printk("cyc: %d: dropping DTR\n", __LINE__);
            printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif
	    restore_flags(flags);
	}
	break;
    case TIOCMSET:
	if (arg & TIOCM_RTS){
	    save_flags(flags); cli();
		base_addr[CyCAR<<index] = (u_char)channel;
		base_addr[CyMSVR1<<index] = CyRTS;
	    restore_flags(flags);
	}else{
	    save_flags(flags); cli();
		base_addr[CyCAR<<index] = (u_char)channel;
		base_addr[CyMSVR1<<index] = ~CyRTS;
	    restore_flags(flags);
	}
	if (arg & TIOCM_DTR){
	    save_flags(flags); cli();
	    base_addr[CyCAR<<index] = (u_char)channel;
	    base_addr[CyMSVR2<<index] = CyDTR;
#ifdef SERIAL_DEBUG_DTR
            printk("cyc: %d: raising DTR\n", __LINE__);
            printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif
	    restore_flags(flags);
	}else{
	    save_flags(flags); cli();
	    base_addr[CyCAR<<index] = (u_char)channel;
	    base_addr[CyMSVR2<<index] = ~CyDTR;
#ifdef SERIAL_DEBUG_DTR
            printk("cyc: %d: dropping DTR\n", __LINE__);
            printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif
	    restore_flags(flags);
	}
	break;
    default:
		return -EINVAL;
        }
    return 0;
} /* set_modem_info */

static void
send_break( struct cyclades_port * info, int duration)
{ /* Let the transmit ISR take care of this (since it
     requires stuffing characters into the output stream).
   */
    info->x_break = duration;
    if (!info->xmit_cnt ) {
	start_xmit(info);
    }
} /* send_break */

static int
get_mon_info(struct cyclades_port * info, struct cyclades_monitor * mon)
{

   memcpy_tofs(mon, &info->mon, sizeof(struct cyclades_monitor));
   info->mon.int_count  = 0;
   info->mon.char_count = 0;
   info->mon.char_max   = 0;
   info->mon.char_last  = 0;
   return 0;
}

static int
set_threshold(struct cyclades_port * info, unsigned long value)
{
   unsigned char *base_addr;
   int card,channel,chip,index;
   
   card = info->card;
   channel = info->line - cy_card[card].first_line;
   chip = channel>>2;
   channel &= 0x03;
   index = cy_card[card].bus_index;
   base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

   info->cor3 &= ~CyREC_FIFO;
   info->cor3 |= value & CyREC_FIFO;
   base_addr[CyCOR3<<index] = info->cor3;
   write_cy_cmd(base_addr,CyCOR_CHANGE|CyCOR3ch,index);
   return 0;
}

static int
get_threshold(struct cyclades_port * info, unsigned long *value)
{
   unsigned char *base_addr;
   int card,channel,chip,index;
   unsigned long tmp;
   
   card = info->card;
   channel = info->line - cy_card[card].first_line;
   chip = channel>>2;
   channel &= 0x03;
   index = cy_card[card].bus_index;
   base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

   tmp = base_addr[CyCOR3<<index] & CyREC_FIFO;
   put_fs_long(tmp,value);
   return 0;
}

static int
set_default_threshold(struct cyclades_port * info, unsigned long value)
{
   info->default_threshold = value & 0x0f;
   return 0;
}

static int
get_default_threshold(struct cyclades_port * info, unsigned long *value)
{
   put_fs_long(info->default_threshold,value);
   return 0;
}

static int
set_timeout(struct cyclades_port * info, unsigned long value)
{
   unsigned char *base_addr;
   int card,channel,chip,index;
   
   card = info->card;
   channel = info->line - cy_card[card].first_line;
   chip = channel>>2;
   channel &= 0x03;
   index = cy_card[card].bus_index;
   base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

   base_addr[CyRTPR<<index] = value & 0xff;
   return 0;
}

static int
get_timeout(struct cyclades_port * info, unsigned long *value)
{
   unsigned char *base_addr;
   int card,channel,chip,index;
   unsigned long tmp;
   
   card = info->card;
   channel = info->line - cy_card[card].first_line;
   chip = channel>>2;
   channel &= 0x03;
   index = cy_card[card].bus_index;
   base_addr = (unsigned char*)
		   (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

   tmp = base_addr[CyRTPR<<index];
   put_fs_long(tmp,value);
   return 0;
}

static int
set_default_timeout(struct cyclades_port * info, unsigned long value)
{
   info->default_timeout = value & 0xff;
   return 0;
}

static int
get_default_timeout(struct cyclades_port * info, unsigned long *value)
{
   put_fs_long(info->default_timeout,value);
   return 0;
}

static int
cy_ioctl(struct tty_struct *tty, struct file * file,
            unsigned int cmd, unsigned long arg)
{
  int error;
  struct cyclades_port * info = (struct cyclades_port *)tty->driver_data;
  int ret_val = 0;

#ifdef SERIAL_DEBUG_OTHER
    printk("cy_ioctl ttyC%d, cmd = %x arg = %lx\n", info->line, cmd, arg); /* */
#endif

    switch (cmd) {
        case CYGETMON:
            error = verify_area(VERIFY_WRITE, (void *) arg
                                ,sizeof(struct cyclades_monitor));
            if (error){
                ret_val = error;
                break;
            }
            ret_val = get_mon_info(info, (struct cyclades_monitor *)arg);
	    break;
        case CYGETTHRESH:
            error = verify_area(VERIFY_WRITE, (void *) arg
                                ,sizeof(unsigned long));
            if (error){
                ret_val = error;
                break;
            }
	    ret_val = get_threshold(info, (unsigned long *)arg);
 	    break;
        case CYSETTHRESH:
            ret_val = set_threshold(info, (unsigned long)arg);
	    break;
        case CYGETDEFTHRESH:
            error = verify_area(VERIFY_WRITE, (void *) arg
                                ,sizeof(unsigned long));
            if (error){
                ret_val = error;
                break;
            }
	    ret_val = get_default_threshold(info, (unsigned long *)arg);
 	    break;
        case CYSETDEFTHRESH:
            ret_val = set_default_threshold(info, (unsigned long)arg);
	    break;
        case CYGETTIMEOUT:
            error = verify_area(VERIFY_WRITE, (void *) arg
                                ,sizeof(unsigned long));
            if (error){
                ret_val = error;
                break;
            }
	    ret_val = get_timeout(info, (unsigned long *)arg);
 	    break;
        case CYSETTIMEOUT:
            ret_val = set_timeout(info, (unsigned long)arg);
	    break;
        case CYGETDEFTIMEOUT:
            error = verify_area(VERIFY_WRITE, (void *) arg
                                ,sizeof(unsigned long));
            if (error){
                ret_val = error;
                break;
            }
	    ret_val = get_default_timeout(info, (unsigned long *)arg);
 	    break;
        case CYSETDEFTIMEOUT:
            ret_val = set_default_timeout(info, (unsigned long)arg);
	    break;
        case TCSBRK:    /* SVID version: non-zero arg --> no break */
	    ret_val = tty_check_change(tty);
	    if (ret_val)
		return ret_val;
            tty_wait_until_sent(tty,0);
            if (!arg)
                send_break(info, HZ/4); /* 1/4 second */
            break;
        case TCSBRKP:   /* support for POSIX tcsendbreak() */
	    ret_val = tty_check_change(tty);
	    if (ret_val)
		return ret_val;
            tty_wait_until_sent(tty,0);
            send_break(info, arg ? arg*(HZ/10) : HZ/4);
            break;
        case TIOCMBIS:
        case TIOCMBIC:
        case TIOCMSET:
            ret_val = set_modem_info(info, cmd, (unsigned int *) arg);
            break;

/* The following commands are incompletely implemented!!! */
        case TIOCGSOFTCAR:
            error = verify_area(VERIFY_WRITE, (void *) arg
                                ,sizeof(unsigned int *));
            if (error){
                ret_val = error;
                break;
            }
            put_fs_long(C_CLOCAL(tty) ? 1 : 0,
                        (unsigned long *) arg);
            break;
        case TIOCSSOFTCAR:
            arg = get_fs_long((unsigned long *) arg);
            tty->termios->c_cflag =
                    ((tty->termios->c_cflag & ~CLOCAL) |
                     (arg ? CLOCAL : 0));
            break;
        case TIOCMGET:
            error = verify_area(VERIFY_WRITE, (void *) arg
                                ,sizeof(unsigned int *));
            if (error){
                ret_val = error;
                break;
            }
            ret_val = get_modem_info(info, (unsigned int *) arg);
            break;
        case TIOCGSERIAL:
            error = verify_area(VERIFY_WRITE, (void *) arg
                                ,sizeof(struct serial_struct));
            if (error){
                ret_val = error;
                break;
            }
            ret_val = get_serial_info(info,
                                   (struct serial_struct *) arg);
            break;
        case TIOCSSERIAL:
            ret_val = set_serial_info(info,
                                   (struct serial_struct *) arg);
            break;
        default:
	    ret_val = -ENOIOCTLCMD;
    }

#ifdef SERIAL_DEBUG_OTHER
    printk("cy_ioctl done\n");
#endif

    return ret_val;
} /* cy_ioctl */




static void
cy_set_termios(struct tty_struct *tty, struct termios * old_termios)
{
  struct cyclades_port *info = (struct cyclades_port *)tty->driver_data;

#ifdef SERIAL_DEBUG_OTHER
    printk("cy_set_termios ttyC%d\n", info->line);
#endif

    if (tty->termios->c_cflag == old_termios->c_cflag)
        return;
    config_setup(info);

    if ((old_termios->c_cflag & CRTSCTS) &&
        !(tty->termios->c_cflag & CRTSCTS)) {
            tty->stopped = 0;
            cy_start(tty);
    }
#ifdef tytso_patch_94Nov25_1726
    if (!(old_termios->c_cflag & CLOCAL) &&
        (tty->termios->c_cflag & CLOCAL))
            wake_up_interruptible(&info->open_wait);
#endif

    return;
} /* cy_set_termios */


static void
cy_close(struct tty_struct * tty, struct file * filp)
{
  struct cyclades_port * info = (struct cyclades_port *)tty->driver_data;
  unsigned long flags;

#ifdef SERIAL_DEBUG_OTHER
    printk("cy_close ttyC%d\n", info->line);
#endif

    if (!info
    || serial_paranoia_check(info, tty->device, "cy_close")){
        return;
    }
#ifdef SERIAL_DEBUG_OPEN
    printk("cy_close ttyC%d, count = %d\n", info->line, info->count);
#endif

    save_flags(flags); cli();

    /* If the TTY is being hung up, nothing to do */
    if (tty_hung_up_p(filp)) {
	restore_flags(flags);
	return;
    }
	
    if ((tty->count == 1) && (info->count != 1)) {
	/*
	 * Uh, oh.  tty->count is 1, which means that the tty
	 * structure will be freed.  Info->count should always
	 * be one in these conditions.  If it's greater than
	 * one, we've got real problems, since it means the
	 * serial port won't be shutdown.
	 */
	printk("cy_close: bad serial port count; tty->count is 1, "
	   "info->count is %d\n", info->count);
	info->count = 1;
    }
#ifdef SERIAL_DEBUG_COUNT
    printk("cyc: %d(%d): decrementing count to %d\n", __LINE__, current->pid, info->count - 1);
#endif
    if (--info->count < 0) {
#ifdef SERIAL_DEBUG_COUNT
    printk("cyc: %d: setting count to 0\n", __LINE__);
#endif
	info->count = 0;
    }
    if (info->count)
    {
        MOD_DEC_USE_COUNT;
	restore_flags(flags);
	return;
    }
    info->flags |= ASYNC_CLOSING;
    /*
     * Save the termios structure, since this port may have
     * separate termios for callout and dialin.
     */
    if (info->flags & ASYNC_NORMAL_ACTIVE)
	info->normal_termios = *tty->termios;
    if (info->flags & ASYNC_CALLOUT_ACTIVE)
	info->callout_termios = *tty->termios;
    if (info->flags & ASYNC_INITIALIZED)
	tty_wait_until_sent(tty, 30*HZ); /* 30 seconds timeout */
    shutdown(info);
    if (tty->driver.flush_buffer)
	tty->driver.flush_buffer(tty);
    if (tty->ldisc.flush_buffer)
	tty->ldisc.flush_buffer(tty);
    info->event = 0;
    info->tty = 0;
    if (info->blocked_open) {
	if (info->close_delay) {
	    current->state = TASK_INTERRUPTIBLE;
	    current->timeout = jiffies + info->close_delay;
	    schedule();
	}
	wake_up_interruptible(&info->open_wait);
    }
    info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE|
		     ASYNC_CLOSING);
    wake_up_interruptible(&info->close_wait);

#ifdef SERIAL_DEBUG_OTHER
    printk("cy_close done\n");
#endif

    MOD_DEC_USE_COUNT;
    restore_flags(flags);
    return;
} /* cy_close */

/*
 * cy_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void
cy_hangup(struct tty_struct *tty)
{
  struct cyclades_port * info = (struct cyclades_port *)tty->driver_data;
	
#ifdef SERIAL_DEBUG_OTHER
    printk("cy_hangup ttyC%d\n", info->line); /* */
#endif

    if (serial_paranoia_check(info, tty->device, "cy_hangup"))
	return;
    
    shutdown(info);
    info->event = 0;
    info->count = 0;
#ifdef SERIAL_DEBUG_COUNT
    printk("cyc: %d(%d): setting count to 0\n", __LINE__, current->pid);
#endif
    info->tty = 0;
    info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
    wake_up_interruptible(&info->open_wait);
} /* cy_hangup */



/*
 * ------------------------------------------------------------
 * cy_open() and friends
 * ------------------------------------------------------------
 */

static int
block_til_ready(struct tty_struct *tty, struct file * filp,
                           struct cyclades_port *info)
{
  struct wait_queue wait = { current, NULL };
  struct cyclades_card *cinfo;
  unsigned long flags;
  int chip, channel,index;
  int retval;
  char *base_addr;

    /*
     * If the device is in the middle of being closed, then block
     * until it's done, and then try again.
     */
    if (info->flags & ASYNC_CLOSING) {
	interruptible_sleep_on(&info->close_wait);
	if (info->flags & ASYNC_HUP_NOTIFY){
	    return -EAGAIN;
	}else{
	    return -ERESTARTSYS;
	}
    }

    /*
     * If this is a callout device, then just make sure the normal
     * device isn't being used.
     */
    if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
	if (info->flags & ASYNC_NORMAL_ACTIVE){
	    return -EBUSY;
	}
	if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
	    (info->flags & ASYNC_SESSION_LOCKOUT) &&
	    (info->session != current->session)){
	    return -EBUSY;
	}
	if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
	    (info->flags & ASYNC_PGRP_LOCKOUT) &&
	    (info->pgrp != current->pgrp)){
	    return -EBUSY;
	}
	info->flags |= ASYNC_CALLOUT_ACTIVE;
	return 0;
    }

    /*
     * If non-blocking mode is set, then make the check up front
     * and then exit.
     */
    if (filp->f_flags & O_NONBLOCK) {
	if (info->flags & ASYNC_CALLOUT_ACTIVE){
	    return -EBUSY;
	}
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
    }

    /*
     * Block waiting for the carrier detect and the line to become
     * free (i.e., not in use by the callout).  While we are in
     * this loop, info->count is dropped by one, so that
     * cy_close() knows when to free things.  We restore it upon
     * exit, either normal or abnormal.
     */
    retval = 0;
    add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
    printk("block_til_ready before block: ttyC%d, count = %d\n",
	   info->line, info->count);/**/
#endif
    info->count--;
#ifdef SERIAL_DEBUG_COUNT
    printk("cyc: %d(%d): decrementing count to %d\n", __LINE__, current->pid, info->count);
#endif
    info->blocked_open++;

    cinfo = &cy_card[info->card];
    channel = info->line - cinfo->first_line;
    chip = channel>>2;
    channel &= 0x03;
    index = cinfo->bus_index;
    base_addr = (char *) (cinfo->base_addr + (cy_chip_offset[chip]<<index));

    while (1) {
	save_flags(flags); cli();
	    if (!(info->flags & ASYNC_CALLOUT_ACTIVE)){
		base_addr[CyCAR<<index] = (u_char)channel;
		base_addr[CyMSVR1<<index] = CyRTS;
		base_addr[CyMSVR2<<index] = CyDTR;
#ifdef SERIAL_DEBUG_DTR
                printk("cyc: %d: raising DTR\n", __LINE__);
                printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1<<index], base_addr[CyMSVR2<<index]);
#endif
	    }
	restore_flags(flags);
	current->state = TASK_INTERRUPTIBLE;
	if (tty_hung_up_p(filp)
	|| !(info->flags & ASYNC_INITIALIZED) ){
	    if (info->flags & ASYNC_HUP_NOTIFY) {
		retval = -EAGAIN;
	    }else{
		retval = -ERESTARTSYS;
	    }
	    break;
	}
	save_flags(flags); cli();
	    base_addr[CyCAR<<index] = (u_char)channel;
	    if (!(info->flags & ASYNC_CALLOUT_ACTIVE)
	    && !(info->flags & ASYNC_CLOSING)
	    && (C_CLOCAL(tty)
	        || (base_addr[CyMSVR1<<index] & CyDCD))) {
		    restore_flags(flags);
		    break;
	    }
	restore_flags(flags);
	if (current->signal & ~current->blocked) {
	    retval = -ERESTARTSYS;
	    break;
	}
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready blocking: ttyC%d, count = %d\n",
	       info->line, info->count);/**/
#endif
	schedule();
    }
    current->state = TASK_RUNNING;
    remove_wait_queue(&info->open_wait, &wait);
    if (!tty_hung_up_p(filp)){
	info->count++;
#ifdef SERIAL_DEBUG_COUNT
    printk("cyc: %d(%d): incrementing count to %d\n", __LINE__, current->pid, info->count);
#endif
    }
    info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
    printk("block_til_ready after blocking: ttyC%d, count = %d\n",
	   info->line, info->count);/**/
#endif
    if (retval)
	    return retval;
    info->flags |= ASYNC_NORMAL_ACTIVE;
    return 0;
} /* block_til_ready */

/*
 * This routine is called whenever a serial port is opened.  It
 * performs the serial-specific initialization for the tty structure.
 */
int
cy_open(struct tty_struct *tty, struct file * filp)
{
  struct cyclades_port  *info;
  int retval, line;

    line = MINOR(tty->device) - tty->driver.minor_start;
    if ((line < 0) || (NR_PORTS <= line)){
        return -ENODEV;
    }
    info = &cy_port[line];
    if (info->line < 0){
        return -ENODEV;
    }
#ifdef SERIAL_DEBUG_OTHER
    printk("cy_open ttyC%d\n", info->line); /* */
#endif
    if (serial_paranoia_check(info, tty->device, "cy_open")){
        return -ENODEV;
    }
#ifdef SERIAL_DEBUG_OPEN
    printk("cy_open ttyC%d, count = %d\n", info->line, info->count);/**/
#endif
    info->count++;
#ifdef SERIAL_DEBUG_COUNT
    printk("cyc: %d(%d): incrementing count to %d\n", __LINE__, current->pid, info->count);
#endif
    tty->driver_data = info;
    info->tty = tty;

    if (!tmp_buf) {
	tmp_buf = (unsigned char *) get_free_page(GFP_KERNEL);
	if (!tmp_buf){
	    return -ENOMEM;
        }
    }

    if ((info->count == 1) && (info->flags & ASYNC_SPLIT_TERMIOS)) {
	if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
	    *tty->termios = info->normal_termios;
	else 
	    *tty->termios = info->callout_termios;
    }
    /*
     * Start up serial port
     */
    retval = startup(info);
    if (retval){
	return retval;
    }

    retval = block_til_ready(tty, filp, info);
    if (retval) {
#ifdef SERIAL_DEBUG_OPEN
	printk("cy_open returning after block_til_ready with %d\n",
	       retval);
#endif
	return retval;
    }

    info->session = current->session;
    info->pgrp = current->pgrp;

#ifdef SERIAL_DEBUG_OPEN
    printk("cy_open done\n");/**/
#endif
    MOD_INC_USE_COUNT;
    return 0;
} /* cy_open */



/*
 * ---------------------------------------------------------------------
 * cy_init() and friends
 *
 * cy_init() is called at boot-time to initialize the serial driver.
 * ---------------------------------------------------------------------
 */

/*
 * This routine prints out the appropriate serial driver version
 * number, and identifies which options were configured into this
 * driver.
 */
static void
show_version(void)
{
    printk("Cyclom driver %s\n",rcsid);
} /* show_version */

/* initialize chips on card -- return number of valid
   chips (which is number of ports/4) */
int
cy_init_card(unsigned char *true_base_addr,int index)
{
  unsigned int chip_number;
  unsigned char* base_addr;

    true_base_addr[Cy_HwReset<<index] = 0; /* Cy_HwReset is 0x1400 */
    true_base_addr[Cy_ClrIntr<<index] = 0; /* Cy_ClrIntr is 0x1800 */
    udelay(500L);

    for(chip_number=0; chip_number<CyMaxChipsPerCard; chip_number++){
        base_addr = true_base_addr + (cy_chip_offset[chip_number]<<index);
        udelay(1000L);
        if(base_addr[CyCCR<<index] != 0x00){
            /*************
            printk(" chip #%d at %#6lx is never idle (CCR != 0)\n",
               chip_number, (unsigned long)base_addr);
            *************/
            return chip_number;
        }

        base_addr[CyGFRCR<<index] = 0;
        udelay(10L);

        /* The Cyclom-16Y does not decode address bit 9 and therefore
           cannot distinguish between references to chip 0 and a non-
           existent chip 4.  If the preceding clearing of the supposed
           chip 4 GFRCR register appears at chip 0, there is no chip 4
           and this must be a Cyclom-16Y, not a Cyclom-32Ye.
        */
        if (chip_number == 4
        && *(true_base_addr + (cy_chip_offset[0]<<index) + (CyGFRCR<<index)) == 0){
	    return chip_number;
        }

        base_addr[CyCCR<<index] = CyCHIP_RESET;
        udelay(1000L);

        if(base_addr[CyGFRCR<<index] == 0x00){
	    /*
            printk(" chip #%d at %#6lx is not responding (GFRCR stayed 0)\n",
               chip_number, (unsigned long)base_addr);
	    */
            return chip_number;
        }
        if((0xf0 & base_addr[CyGFRCR<<index]) != 0x40){
	    /*
            printk(" chip #%d at %#6lx is not valid (GFRCR == %#2x)\n",
               chip_number, (unsigned long)base_addr, base_addr[CyGFRCR<<index]);
	    */
            return chip_number;
        }
        base_addr[CyGCR<<index] = CyCH0_SERIAL;
        base_addr[CyPPR<<index] = 244; /* better value than CyCLOCK_25_1MS * 5
                                                  to run clock at 200 Hz */

	/*
        printk(" chip #%d at %#6lx is rev 0x%2x\n",
               chip_number, (unsigned long)base_addr, base_addr[CyGFRCR<<index]);
	*/
    }

    return chip_number;
} /* cy_init_card */

/* The serial driver boot-time initialization code!
    Hardware I/O ports are mapped to character special devices on a
    first found, first allocated manner.  That is, this code searches
    for Cyclom cards in the system.  As each is found, it is probed
    to discover how many chips (and thus how many ports) are present.
    These ports are mapped to the tty ports 32 and upward in monotonic
    fashion.  If an 8-port card is replaced with a 16-port card, the
    port mapping on a following card will shift.

    This approach is different from what is used in the other serial
    device driver because the Cyclom is more properly a multiplexer,
    not just an aggregation of serial ports on one card.

    If there are more cards with more ports than have been statically
    allocated above, a warning is printed and the extra ports are ignored.
 */
int
cy_init(void)
{
  struct cyclades_port	*info;
  struct cyclades_card *cinfo;
  int			board,port,i;

    show_version();

    /* Initialize the tty_driver structure */
    
    memset(&cy_serial_driver, 0, sizeof(struct tty_driver));
    cy_serial_driver.magic = TTY_DRIVER_MAGIC;
    cy_serial_driver.name = "ttyC";
    cy_serial_driver.major = CYCLADES_MAJOR;
    cy_serial_driver.minor_start = 0;
    cy_serial_driver.num = NR_PORTS;
    cy_serial_driver.type = TTY_DRIVER_TYPE_SERIAL;
    cy_serial_driver.subtype = SERIAL_TYPE_NORMAL;
    cy_serial_driver.init_termios = tty_std_termios;
    cy_serial_driver.init_termios.c_cflag =
	    B9600 | CS8 | CREAD | HUPCL | CLOCAL;
    cy_serial_driver.flags = TTY_DRIVER_REAL_RAW;
    cy_serial_driver.refcount = &serial_refcount;
    cy_serial_driver.table = serial_table;
    cy_serial_driver.termios = serial_termios;
    cy_serial_driver.termios_locked = serial_termios_locked;
    cy_serial_driver.open = cy_open;
    cy_serial_driver.close = cy_close;
    cy_serial_driver.write = cy_write;
    cy_serial_driver.put_char = cy_put_char;
    cy_serial_driver.flush_chars = cy_flush_chars;
    cy_serial_driver.write_room = cy_write_room;
    cy_serial_driver.chars_in_buffer = cy_chars_in_buffer;
    cy_serial_driver.flush_buffer = cy_flush_buffer;
    cy_serial_driver.ioctl = cy_ioctl;
    cy_serial_driver.throttle = cy_throttle;
    cy_serial_driver.unthrottle = cy_unthrottle;
    cy_serial_driver.set_termios = cy_set_termios;
    cy_serial_driver.stop = cy_stop;
    cy_serial_driver.start = cy_start;
    cy_serial_driver.hangup = cy_hangup;

    /*
     * The callout device is just like normal device except for
     * major number and the subtype code.
     */
    cy_callout_driver = cy_serial_driver;
    cy_callout_driver.name = "cub";
    cy_callout_driver.major = CYCLADESAUX_MAJOR;
    cy_callout_driver.subtype = SERIAL_TYPE_CALLOUT;

    if (tty_register_driver(&cy_serial_driver))
	    panic("Couldn't register Cyclom serial driver\n");
    if (tty_register_driver(&cy_callout_driver))
	    panic("Couldn't register Cyclom callout driver\n");

    init_bh(CYCLADES_BH, do_cyclades_bh);

    for (i = 0; i < 16; i++) {
	    IRQ_cards[i] = 0;
    }

    for (i = 0; i < NR_CARDS; i++) {
	    /* base_addr=0 indicates board not found */
	    cy_card[i].base_addr = 0;
    }

    /* the code below is responsible to find the boards. Each different
       type of board has its own detection routine. If a board is found,
       the next cy_card structure available is set by the detection
       routine. These functions are responsible for checking the availability
       of cy_card and cy_port data structures and updating the
       cy_next_channel. */

    /* look for isa boards */
    cy_isa_nboard = cy_detect_isa();

    /* look for pci boards */
    cy_pci_nboard = cy_detect_pci();

    cy_nboard = cy_isa_nboard + cy_pci_nboard;

    /* invalidate remaining cy_card structures */
    for (i = 0 ; i < NR_CARDS ; i++) {
	if (cy_card[i].base_addr == 0) {
		cy_card[i].first_line = -1;
	}
    }
    /* invalidate remaining cy_port structures */
    for (i = cy_next_channel ; i < NR_PORTS ; i++) {
	cy_port[i].line = -1;
	cy_port[i].magic = -1;
    }

    /* initialize per-port data structures for each valid board found */
    for (board = 0 ; board < cy_nboard ; board++) {
	    cinfo = &cy_card[board];
	    for (port = cinfo->first_line ;
		 port < cinfo->first_line + 4*cinfo->num_chips ;
		 port++)
	    {
		info = &cy_port[port];
		info->magic = CYCLADES_MAGIC;
		info->type = PORT_CIRRUS;
		info->card = board;
		info->line = port;
		info->flags = STD_COM_FLAGS;
		info->tty = 0;
		info->xmit_fifo_size = 12;
		info->cor1 = CyPARITY_NONE|Cy_1_STOP|Cy_8_BITS;
		info->cor2 = CyETC;
		info->cor3 = 0x08; /* _very_ small receive threshold */
		info->cor4 = 0;
		info->cor5 = 0;
		info->tbpr = baud_bpr[13]; /* Tx BPR */
		info->tco = baud_co[13]; /* Tx CO */
		info->rbpr = baud_bpr[13]; /* Rx BPR */
		info->rco = baud_co[13]; /* Rx CO */
		info->close_delay = 0;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
#ifdef SERIAL_DEBUG_COUNT
    printk("cyc: %d: setting count to 0\n", __LINE__);
#endif
		info->blocked_open = 0;
		info->default_threshold = 0;
		info->default_timeout = 0;
		info->tqueue.routine = do_softint;
		info->tqueue.data = info;
		info->callout_termios =cy_callout_driver.init_termios;
		info->normal_termios = cy_serial_driver.init_termios;
		info->open_wait = 0;
		info->close_wait = 0;
		/* info->session */
		/* info->pgrp */
		info->read_status_mask = CyTIMEOUT| CySPECHAR| CyBREAK
                                       | CyPARITY| CyFRAME| CyOVERRUN;
		/* info->timeout */
	    }
    }
    return 0;
    
} /* cy_init */

#ifdef MODULE
int
init_module(void)
{
   return(cy_init());
}

void
cleanup_module(void)
{
    int i;


    if (tty_unregister_driver(&cy_callout_driver))
	    printk("Couldn't unregister Cyclom callout driver\n");
    if (tty_unregister_driver(&cy_serial_driver))
	    printk("Couldn't unregister Cyclom serial driver\n");

    for (i = 0; i < NR_CARDS; i++) {
        if (cy_card[i].base_addr != 0)
	{
	    free_irq(cy_card[i].irq,NULL);
	}
    }
}
#endif

/*
 * ---------------------------------------------------------------------
 * cy_detect_isa() - Probe for Cyclom-Y/ISA boards.
 * sets global variables and return the number of ISA boards found.
 * ---------------------------------------------------------------------
 */
int
cy_detect_isa()
{
  unsigned int		cy_isa_irq,nboard;
  unsigned char		*cy_isa_address;
  unsigned short	i,j,cy_isa_nchan;

	nboard = 0;

	/* scan the address table probing for Cyclom-Y/ISA boards */
	for (i = 0 ; i < NR_ISA_ADDRESSES ; i++) {
		cy_isa_address = cy_isa_addresses[i];
		if (cy_isa_address  == 0x0000) {
			return(nboard);
		}

		/* probe for CD1400... */
		cy_isa_nchan = 4 * cy_init_card(cy_isa_address,0);
		if (cy_isa_nchan == 0) {
			continue;
		}

		/* find out the board's irq by probing */
		cy_isa_irq = do_auto_irq(cy_isa_address);
		if (cy_isa_irq == 0) {
			printk("Cyclom-Y/ISA found at 0x%x but the IRQ could not be detected.\n",
				(unsigned int) cy_isa_address);
			continue;
		}

		if((cy_next_channel+cy_isa_nchan) > NR_PORTS) {
			printk("Cyclom-Y/ISA found at 0x%x but no more channel structures are available.\n",
				(unsigned int) cy_isa_address);
			return(nboard);
		}
		/* fill the next cy_card structure available */
		for (j = 0 ; j < NR_CARDS ; j++) {
			if (cy_card[j].base_addr == 0)	break;
		}
		if (j == NR_CARDS) {	/* no more cy_cards available */
			printk("Cyclom-Y/ISA found at 0x%x but no more card structures are available.\n",
				(unsigned int) cy_isa_address);
			return(nboard);
		}

		/* allocate IRQ */
		if(request_irq(cy_isa_irq,cy_interrupt,SA_INTERRUPT,"cyclades",NULL))
		{
			printk("Cyclom-Y/ISA found at 0x%x but could not allocate interrupt IRQ#%d.\n",
				(unsigned int) cy_isa_address,cy_isa_irq);
			return(nboard);
		}

		/* set cy_card */
		cy_card[j].base_addr = (int) cy_isa_address;
		cy_card[j].irq = (int) cy_isa_irq;
		cy_card[j].bus_index = 0;
		cy_card[j].first_line = cy_next_channel;
		cy_card[j].num_chips = cy_isa_nchan/4;
	        IRQ_cards[cy_isa_irq] = &cy_card[j];
		nboard++;
			
		/* print message */
		printk("Cyclom-Y/ISA #%d: 0x%x-0x%x, IRQ%d, %d channels starting from port %d.\n",
			j+1,(unsigned int) cy_isa_address,
			(unsigned int)(cy_isa_address + 0x1fff),
			cy_isa_irq,cy_isa_nchan,cy_next_channel);
		cy_next_channel += cy_isa_nchan;
	}
	return(nboard);

}

/*
 * ---------------------------------------------------------------------
 * cy_detect_pci() - Test PCI bus presence and Cyclom-Ye/PCI.
 * sets global variables and return the number of PCI boards found.
 * ---------------------------------------------------------------------
 */
int
cy_detect_pci()
{
#ifdef CONFIG_PCI
  unsigned char		cyy_bus, cyy_dev_fn, cyy_rev_id;
  unsigned long		pci_intr_ctrl;
  unsigned char		cy_pci_irq;
  unsigned int		cy_pci_address, cy_pci_io;
  unsigned short	i,j,cy_pci_nchan;
  unsigned short	device_id,dev_index = 0,board_index = 0;

	if(pcibios_present() == 0) {	/* PCI bus not present */
	 	return(0);
	}
	for (i = 0; i < NR_CARDS; i++) {
		/* look for a Cyclom-Y card by vendor and device id */
		while((device_id = cy_pci_dev_id[dev_index]) != 0) {
			if(pcibios_find_device(PCI_VENDOR_ID_CYCLADES,
					device_id,board_index,
  					&cyy_bus, &cyy_dev_fn) != 0)
			{
				dev_index++;	/* try next device id */
				board_index = 0;
			} else {
				board_index++;
				break;		/* found a board */
			}
  		}
		if (device_id == 0)	break;

		/* read PCI configuration area */
		pcibios_read_config_byte(cyy_bus, cyy_dev_fn,
				 PCI_INTERRUPT_LINE, &cy_pci_irq);
		pcibios_read_config_dword(cyy_bus, cyy_dev_fn,
				  PCI_BASE_ADDRESS_1, &cy_pci_io);
		pcibios_read_config_dword(cyy_bus, cyy_dev_fn,
				  PCI_BASE_ADDRESS_2, &cy_pci_address);
		pcibios_read_config_byte(cyy_bus, cyy_dev_fn,
				  PCI_REVISION_ID, &cyy_rev_id);
		cy_pci_address &= 0xfffffff0;
		if ((ulong)cy_pci_address >= 0x100000) { /* above 1M? */
			cy_pci_address =
			    (unsigned int) vremap(cy_pci_address,0x4000);
		}
		cy_pci_io  &= 0xfffffffc;
		cy_pci_nchan = 4 * cy_init_card((unsigned char *)
						cy_pci_address,1);
		if(cy_pci_nchan == 0) {
			printk("Cyclom-Y PCI host card with no Serial-Modules at 0x%x.\n",
				(unsigned int) cy_pci_address);
			continue;
		}
		if((cy_next_channel+cy_pci_nchan) > NR_PORTS) {
			printk("Cyclom-Y/PCI found at 0x%x but no more channel structures are available.\n",
				(unsigned int) cy_pci_address);
			return(i);
		}
#ifdef CY_PCI_DEBUG
		printk("Cyclom-Ye/PCI #%d (bus=0x0%x, pci_id=0x%x, rev_id=%d).\n",
			i+1,cyy_bus,cyy_dev_fn,cyy_rev_id);
		printk("Cyclom-Ye/PCI: found at 0x%x, IRQ%d, ioaddr = 0x%lx.\n",
			cy_pci_address,(int)cy_pci_irq,cy_pci_io);
#endif
		/* fill the next cy_card structure available */
		for (j = 0 ; j < NR_CARDS ; j++) {
			if (cy_card[j].base_addr == 0)	break;
		}
		if (j == NR_CARDS) {	/* no more cy_cards available */
			printk("Cyclom-Y/PCI found at 0x%x but no more card structures are available.\n",
				(unsigned int) cy_pci_address);
			return(i);
		}

		/* allocate IRQ */
		if(request_irq(cy_pci_irq,cy_interrupt,SA_INTERRUPT,"cyclades",NULL))
		{
			printk("Cyclom-Y/PCI found at 0x%x but could not allocate interrupt IRQ%d.\n",
				(unsigned int) cy_pci_address,cy_pci_irq);
			return(i);
		}

		/* set cy_card */
		cy_card[j].base_addr = (int) cy_pci_address;
		cy_card[j].irq = (int) cy_pci_irq;
		cy_card[j].bus_index = 1;
		cy_card[j].first_line = cy_next_channel;
		cy_card[j].num_chips = cy_pci_nchan/4;
	        IRQ_cards[cy_pci_irq] = &cy_card[j];

		/* enable interrupts in the PCI interface */
		outw(inw(cy_pci_io+0x68)|0x0900,cy_pci_io+0x68);
		pci_intr_ctrl = (unsigned long)(inw(cy_pci_io+0x68) | inw(cy_pci_io+0x6a)<<16);

		/* print message */
		printk("Cyclom-Y/PCI #%d: 0x%x-0x%x, IRQ%d, %d channels starting from port %d.\n",
			j+1,(unsigned int) cy_pci_address,
			(unsigned int)(cy_pci_address + 0x3fff),
			(int)cy_pci_irq,cy_pci_nchan,cy_next_channel);

		cy_next_channel += cy_pci_nchan;
	}
	return(i);
#else
	return(0);
#endif /* ifdef CONFIG_PCI */
}


#ifdef CYCLOM_SHOW_STATUS
static void
show_status(int line_num)
{
  unsigned char *base_addr;
  int card,chip,channel,index;
  struct cyclades_port * info;
  unsigned long flags;

    info = &cy_port[line_num];
    card = info->card;
    index = cy_card[card].bus_index;
    channel = (info->line) - (cy_card[card].first_line);
    chip = channel>>2;
    channel &= 0x03;
    printk("  card %d, chip %d, channel %d\n", card, chip, channel);/**/

    printk(" cy_card\n");
    printk("  irq base_addr num_chips first_line = %d %lx %d %d\n",
           cy_card[card].irq, (long)cy_card[card].base_addr,
           cy_card[card].num_chips, cy_card[card].first_line);

    printk(" cy_port\n");
    printk("  card line flags = %d %d %x\n",
                 info->card, info->line, info->flags);
    printk("  *tty read_status_mask timeout xmit_fifo_size = %lx %x %x %x\n",
                 (long)info->tty, info->read_status_mask,
                 info->timeout, info->xmit_fifo_size);
    printk("  cor1,cor2,cor3,cor4,cor5 = %x %x %x %x %x\n",
             info->cor1, info->cor2, info->cor3, info->cor4, info->cor5);
    printk("  tbpr,tco,rbpr,rco = %d %d %d %d\n",
             info->tbpr, info->tco, info->rbpr, info->rco);
    printk("  close_delay event count = %d %d %d\n",
             info->close_delay, info->event, info->count);
    printk("  x_char blocked_open = %x %x\n",
             info->x_char, info->blocked_open);
    printk("  session pgrp open_wait = %lx %lx %lx\n",
             info->session, info->pgrp, (long)info->open_wait);


    save_flags(flags); cli();

	base_addr = (unsigned char*)
		       (cy_card[card].base_addr + (cy_chip_offset[chip]<<index));

/* Global Registers */

	printk(" CyGFRCR %x\n", base_addr[CyGFRCR<<index]);
	printk(" CyCAR %x\n", base_addr[CyCAR<<index]);
	printk(" CyGCR %x\n", base_addr[CyGCR<<index]);
	printk(" CySVRR %x\n", base_addr[CySVRR<<index]);
	printk(" CyRICR %x\n", base_addr[CyRICR<<index]);
	printk(" CyTICR %x\n", base_addr[CyTICR<<index]);
	printk(" CyMICR %x\n", base_addr[CyMICR<<index]);
	printk(" CyRIR %x\n", base_addr[CyRIR<<index]);
	printk(" CyTIR %x\n", base_addr[CyTIR<<index]);
	printk(" CyMIR %x\n", base_addr[CyMIR<<index]);
	printk(" CyPPR %x\n", base_addr[CyPPR<<index]);

	base_addr[CyCAR<<index] = (u_char)channel;

/* Virtual Registers */

	printk(" CyRIVR %x\n", base_addr[CyRIVR<<index]);
	printk(" CyTIVR %x\n", base_addr[CyTIVR<<index]);
	printk(" CyMIVR %x\n", base_addr[CyMIVR<<index]);
	printk(" CyMISR %x\n", base_addr[CyMISR<<index]);

/* Channel Registers */

	printk(" CyCCR %x\n", base_addr[CyCCR<<index]);
	printk(" CySRER %x\n", base_addr[CySRER<<index]);
	printk(" CyCOR1 %x\n", base_addr[CyCOR1<<index]);
	printk(" CyCOR2 %x\n", base_addr[CyCOR2<<index]);
	printk(" CyCOR3 %x\n", base_addr[CyCOR3<<index]);
	printk(" CyCOR4 %x\n", base_addr[CyCOR4<<index]);
	printk(" CyCOR5 %x\n", base_addr[CyCOR5<<index]);
	printk(" CyCCSR %x\n", base_addr[CyCCSR<<index]);
	printk(" CyRDCR %x\n", base_addr[CyRDCR<<index]);
	printk(" CySCHR1 %x\n", base_addr[CySCHR1<<index]);
	printk(" CySCHR2 %x\n", base_addr[CySCHR2<<index]);
	printk(" CySCHR3 %x\n", base_addr[CySCHR3<<index]);
	printk(" CySCHR4 %x\n", base_addr[CySCHR4<<index]);
	printk(" CySCRL %x\n", base_addr[CySCRL<<index]);
	printk(" CySCRH %x\n", base_addr[CySCRH<<index]);
	printk(" CyLNC %x\n", base_addr[CyLNC<<index]);
	printk(" CyMCOR1 %x\n", base_addr[CyMCOR1<<index]);
	printk(" CyMCOR2 %x\n", base_addr[CyMCOR2<<index]);
	printk(" CyRTPR %x\n", base_addr[CyRTPR<<index]);
	printk(" CyMSVR1 %x\n", base_addr[CyMSVR1<<index]);
	printk(" CyMSVR2 %x\n", base_addr[CyMSVR2<<index]);
	printk(" CyRBPR %x\n", base_addr[CyRBPR<<index]);
	printk(" CyRCOR %x\n", base_addr[CyRCOR<<index]);
	printk(" CyTBPR %x\n", base_addr[CyTBPR<<index]);
	printk(" CyTCOR %x\n", base_addr[CyTCOR<<index]);

    restore_flags(flags);
} /* show_status */
#endif


/* $Id: sunserial.c,v 1.39 1997/04/23 07:45:26 ecd Exp $
 * serial.c: Serial port driver for the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 * Fixes by Pete A. Zaitcev <zaitcev@ipmce.su>.
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/config.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/delay.h>
#include <asm/kdebug.h>

#include "sunserial.h"

static int num_serial = 2; /* sun4/sun4c/sun4m - Two chips on board. */
#define NUM_SERIAL num_serial
#define NUM_CHANNELS (NUM_SERIAL * 2)

#define KEYBOARD_LINE 0x2
#define MOUSE_LINE    0x3

extern struct wait_queue * keypress_wait;

struct sun_zslayout **zs_chips;
struct sun_zschannel **zs_channels;
struct sun_zschannel *zs_conschan;
struct sun_zschannel *zs_mousechan;
struct sun_zschannel *zs_kbdchan;
struct sun_zschannel *zs_kgdbchan;
int *zs_nodes;

struct sun_serial *zs_soft;
struct sun_serial *zs_chain;  /* IRQ servicing chain */
int zilog_irq;

struct tty_struct *zs_ttys;
/** struct tty_struct *zs_constty; **/

/* Console hooks... */
static int zs_cons_chanout = 0;
static int zs_cons_chanin = 0;
static struct l1a_kbd_state l1a_state = { 0, 0 };
struct sun_serial *zs_consinfo = 0;

/* Keyboard defines for L1-A processing... */
#define SUNKBD_RESET   0xff
#define SUNKBD_L1      0x01
#define SUNKBD_UP      0x80
#define SUNKBD_A       0x4d

extern void sunkbd_inchar(unsigned char ch, struct pt_regs *regs);
extern void sun_mouse_inbyte(unsigned char byte);

static unsigned char kgdb_regs[16] = {
	0, 0, 0,                     /* write 0, 1, 2 */
	(Rx8 | RxENAB),              /* write 3 */
	(X16CLK | SB1 | PAR_EVEN),   /* write 4 */
	(DTR | Tx8 | TxENAB),        /* write 5 */
	0, 0, 0,                     /* write 6, 7, 8 */
	(NV),                        /* write 9 */
	(NRZ),                       /* write 10 */
	(TCBR | RCBR),               /* write 11 */
	0, 0,                        /* BRG time constant, write 12 + 13 */
	(BRSRC | BRENAB),            /* write 14 */
	(DCDIE)                      /* write 15 */
};

static unsigned char zscons_regs[16] = {
	0,                           /* write 0 */
	(EXT_INT_ENAB | INT_ALL_Rx), /* write 1 */
	0,                           /* write 2 */
	(Rx8 | RxENAB),              /* write 3 */
	(X16CLK),                    /* write 4 */
	(DTR | Tx8 | TxENAB),        /* write 5 */
	0, 0, 0,                     /* write 6, 7, 8 */
	(NV | MIE),                  /* write 9 */
	(NRZ),                       /* write 10 */
	(TCBR | RCBR),               /* write 11 */
	0, 0,                        /* BRG time constant, write 12 + 13 */
	(BRSRC | BRENAB),            /* write 14 */
	(DCDIE | CTSIE | TxUIE | BRKIE) /* write 15 */
};

#define ZS_CLOCK         4915200   /* Zilog input clock rate */

DECLARE_TASK_QUEUE(tq_serial);

struct tty_driver serial_driver, callout_driver;
static int serial_refcount;

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL	1
#define SERIAL_TYPE_CALLOUT	2
  
/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/* Debugging... DEBUG_INTR is bad to use when one of the zs
 * lines is your console ;(
 */
#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW

#define RS_STROBE_TIME 10
#define RS_ISR_PASS_LIMIT 256

#define _INLINE_ inline

static void change_speed(struct sun_serial *info);

static struct tty_struct **serial_table;
static struct termios **serial_termios;
static struct termios **serial_termios_locked;

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the memcpy_fromfs blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static unsigned char tmp_buf[4096]; /* This is cheating */
static struct semaphore tmp_buf_sem = MUTEX;

static inline int serial_paranoia_check(struct sun_serial *info,
					dev_t device, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct (%d, %d) in %s\n";
	static const char *badinfo =
		"Warning: null sun_serial for (%d, %d) in %s\n";

	if (!info) {
		printk(badinfo, MAJOR(device), MINOR(device), routine);
		return 1;
	}
	if (info->magic != SERIAL_MAGIC) {
		printk(badmagic, MAJOR(device), MINOR(device), routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * This is used to figure out the divisor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 76800, 0 };

/* 
 * Reading and writing Zilog8530 registers.  The delays are to make this
 * driver work on the Sun4 which needs a settling delay after each chip
 * register access, other machines handle this in hardware via auxiliary
 * flip-flops which implement the settle time we do in software.
 */
static inline unsigned char read_zsreg(struct sun_zschannel *channel,
				       unsigned char reg)
{
	unsigned char retval;

	channel->control = reg;
	udelay(5);
	retval = channel->control;
	udelay(5);
	return retval;
}

static inline void write_zsreg(struct sun_zschannel *channel,
			       unsigned char reg, unsigned char value)
{
	channel->control = reg;
	udelay(5);
	channel->control = value;
	udelay(5);
}

static inline void load_zsregs(struct sun_serial *info, unsigned char *regs)
{
	unsigned long flags;
	struct sun_zschannel *channel = info->zs_channel;
	unsigned char stat;
	int i;

	for (i = 0; i < 1000; i++) {
		stat = read_zsreg(channel, R1);
		if (stat & ALL_SNT)
			break;
		udelay(100);
	}
	write_zsreg(channel, R3, 0);
	ZS_CLEARSTAT(channel);
	ZS_CLEARERR(channel);
	ZS_CLEARFIFO(channel);

	/* Load 'em up */
	save_flags(flags); cli();
	if (info->channelA)
		write_zsreg(channel, R9, CHRA);
	else
		write_zsreg(channel, R9, CHRB);
	udelay(20);	/* wait for some old sun4's */
	write_zsreg(channel, R4, regs[R4]);
	write_zsreg(channel, R3, regs[R3] & ~RxENAB);
	write_zsreg(channel, R5, regs[R5] & ~TxENAB);
	write_zsreg(channel, R9, regs[R9] & ~MIE);
	write_zsreg(channel, R10, regs[R10]);
	write_zsreg(channel, R11, regs[R11]);
	write_zsreg(channel, R12, regs[R12]);
	write_zsreg(channel, R13, regs[R13]);
	write_zsreg(channel, R14, regs[R14] & ~BRENAB);
	write_zsreg(channel, R14, regs[R14]);
	write_zsreg(channel, R14, (regs[R14] & ~SNRZI) | BRENAB);
	write_zsreg(channel, R3, regs[R3]);
	write_zsreg(channel, R5, regs[R5]);
	write_zsreg(channel, R15, regs[R15]);
	write_zsreg(channel, R0, RES_EXT_INT);
	write_zsreg(channel, R0, ERR_RES);
	write_zsreg(channel, R1, regs[R1]);
	write_zsreg(channel, R9, regs[R9]);
	restore_flags(flags);
}

static inline void zs_put_char(struct sun_zschannel *channel, char ch)
{
	int loops = 0;

	while((channel->control & Tx_BUF_EMP) == 0 && loops < 10000) {
		loops++;
		udelay(5);
	}
	channel->data = ch;
	udelay(5);
}

/* Sets or clears DTR/RTS on the requested line */
static inline void zs_rtsdtr(struct sun_serial *ss, int set)
{
	unsigned long flags;

	save_flags(flags); cli();
	if(set) {
		ss->curregs[5] |= (RTS | DTR);
		write_zsreg(ss->zs_channel, 5, ss->curregs[5]);
	} else {
		ss->curregs[5] &= ~(RTS | DTR);
		write_zsreg(ss->zs_channel, 5, ss->curregs[5]);
	}
	restore_flags(flags);
	return;
}

static inline void kgdb_chaninit(struct sun_serial *ss, int intson, int bps)
{
	int brg;

	if(intson) {
		kgdb_regs[R1] = INT_ALL_Rx;
		kgdb_regs[R9] |= MIE;
	} else {
		kgdb_regs[R1] = 0;
		kgdb_regs[R9] &= ~MIE;
	}
	brg = BPS_TO_BRG(bps, ZS_CLOCK/16);
	kgdb_regs[R12] = (brg & 255);
	kgdb_regs[R13] = ((brg >> 8) & 255);
	load_zsregs(ss, kgdb_regs);
}

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void rs_stop(struct tty_struct *tty)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_stop"))
		return;
	
	save_flags(flags); cli();
	if (info->curregs[5] & TxENAB) {
		info->curregs[5] &= ~TxENAB;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
	}
	restore_flags(flags);
}

static void rs_start(struct tty_struct *tty)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
	unsigned long flags;
	
	if (serial_paranoia_check(info, tty->device, "rs_start"))
		return;
	
	save_flags(flags); cli();
	if (info->xmit_cnt && info->xmit_buf && !(info->curregs[5] & TxENAB)) {
		info->curregs[5] |= TxENAB;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
	}
	restore_flags(flags);
}

/* Drop into either the boot monitor or kadb upon receiving a break
 * from keyboard/console input.
 */
static void batten_down_hatches(void)
{
	/* If we are doing kadb, we call the debugger
	 * else we just drop into the boot monitor.
	 * Note that we must flush the user windows
	 * first before giving up control.
	 */
	printk("\n");
	flush_user_windows();
	if((((unsigned long)linux_dbvec)>=DEBUG_FIRSTVADDR) &&
	   (((unsigned long)linux_dbvec)<=DEBUG_LASTVADDR))
		sp_enter_debugger();
	else
		prom_cmdline();

	/* XXX We want to notify the keyboard driver that all
	 * XXX keys are in the up state or else weird things
	 * XXX happen...
	 */

	return;
}


/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * Note: rs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * rs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 * 
 * gcc -S -DKERNEL -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer serial.c
 *
 * and look at the resulting assemble code in serial.s.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static _INLINE_ void rs_sched_event(struct sun_serial *info,
				  int event)
{
	info->event |= 1 << event;
	queue_task(&info->tqueue, &tq_serial);
	mark_bh(SERIAL_BH);
}

extern void breakpoint(void);  /* For the KGDB frame character */

static _INLINE_ void receive_chars(struct sun_serial *info, struct pt_regs *regs)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, stat;

	do {
		ch = (info->zs_channel->data) & info->parity_mask;
		udelay(5);

		/* If this is the console keyboard, we need to handle
		 * L1-A's here.
		 */
		if(info->cons_keyb) {
			if(ch == SUNKBD_RESET) {
				l1a_state.kbd_id = 1;
				l1a_state.l1_down = 0;
			} else if(l1a_state.kbd_id) {
				l1a_state.kbd_id = 0;
			} else if(ch == SUNKBD_L1) {
				l1a_state.l1_down = 1;
			} else if(ch == (SUNKBD_L1|SUNKBD_UP)) {
				l1a_state.l1_down = 0;
			} else if(ch == SUNKBD_A && l1a_state.l1_down) {
				/* whee... */
				batten_down_hatches();
				/* Continue execution... */
				l1a_state.l1_down = 0;
				l1a_state.kbd_id = 0;
				return;
			}
			sunkbd_inchar(ch, regs);
			return;
		}
		if(info->cons_mouse) {
			sun_mouse_inbyte(ch);
			return;
		}
		if(info->is_cons) {
			if(ch==0) {
				/* whee, break received */
				batten_down_hatches();
				/* Continue execution... */
				return;
#if 0
			} else if (ch == 1) {
				show_state();
				return;
			} else if (ch == 2) {
				show_buffers();
				return;
#endif
			}
			/* It is a 'keyboard interrupt' ;-) */
			wake_up(&keypress_wait);
		}
		/* Look for kgdb 'stop' character, consult the gdb
		 * documentation for remote target debugging and
		 * arch/sparc/kernel/sparc-stub.c to see how all this works.
		 */
		if((info->kgdb_channel) && (ch =='\003')) {
			breakpoint();
			return;
		}

		if(!tty)
			return;

		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			break;

		tty->flip.count++;
		*tty->flip.flag_buf_ptr++ = 0;
		*tty->flip.char_buf_ptr++ = ch;

		/* Check if we have another character... */
		stat = info->zs_channel->control;
		udelay(5);
		if (!(stat & Rx_CH_AV))
			break;

		/* ... and see if it is clean. */
		stat = read_zsreg(info->zs_channel, R1);
	} while (!(stat & (PAR_ERR | Rx_OVR | CRC_ERR)));

	queue_task(&tty->flip.tqueue, &tq_timer);
}

static _INLINE_ void transmit_chars(struct sun_serial *info)
{
	struct tty_struct *tty = info->tty;

	if (info->x_char) {
		/* Send next char */
		zs_put_char(info->zs_channel, info->x_char);
		info->x_char = 0;
		return;
	}

	if((info->xmit_cnt <= 0) || (tty != 0 && tty->stopped)) {
		/* That's peculiar... */
		info->zs_channel->control = RES_Tx_P;
		udelay(5);
		return;
	}

	/* Send char */
	zs_put_char(info->zs_channel, info->xmit_buf[info->xmit_tail++]);
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
	info->xmit_cnt--;

	if (info->xmit_cnt < WAKEUP_CHARS)
		rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);

	if(info->xmit_cnt <= 0) {
		info->zs_channel->control = RES_Tx_P;
		udelay(5);
	}
}

static _INLINE_ void status_handle(struct sun_serial *info)
{
	unsigned char status;

	/* Get status from Read Register 0 */
	status = info->zs_channel->control;
	udelay(5);
	/* Clear status condition... */
	info->zs_channel->control = RES_EXT_INT;
	udelay(5);

#if 0
	if(status & DCD) {
		if((info->tty->termios->c_cflag & CRTSCTS) &&
		   ((info->curregs[3] & AUTO_ENAB)==0)) {
			info->curregs[3] |= AUTO_ENAB;
			write_zsreg(info->zs_channel, 3, info->curregs[3]);
		}
	} else {
		if((info->curregs[3] & AUTO_ENAB)) {
			info->curregs[3] &= ~AUTO_ENAB;
			write_zsreg(info->zs_channel, 3, info->curregs[3]);
		}
	}
#endif
	/* Whee, if this is console input and this is a
	 * 'break asserted' status change interrupt, call
	 * the boot prom.
	 */
	if((status & BRK_ABRT) && info->break_abort)
		batten_down_hatches();

	/* XXX Whee, put in a buffer somewhere, the status information
	 * XXX whee whee whee... Where does the information go...
	 */
	return;
}

static _INLINE_ void special_receive(struct sun_serial *info)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, stat;

	stat = read_zsreg(info->zs_channel, R1);
	if (stat & (PAR_ERR | Rx_OVR | CRC_ERR)) {
		ch = info->zs_channel->data;
		udelay(5);
	}

	if (!tty)
		goto clear;

	if (tty->flip.count >= TTY_FLIPBUF_SIZE)
		goto done;

	tty->flip.count++;
	if(stat & PAR_ERR)
		*tty->flip.flag_buf_ptr++ = TTY_PARITY;
	else if(stat & Rx_OVR)
		*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
	else if(stat & CRC_ERR)
		*tty->flip.flag_buf_ptr++ = TTY_FRAME;

done:
	queue_task(&tty->flip.tqueue, &tq_timer);
clear:
	info->zs_channel->control = ERR_RES;
	udelay(5);
}


/*
 * This is the serial driver's generic interrupt routine
 */
void rs_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct sun_serial *info;
	unsigned char zs_intreg;
	int i;

	info = (struct sun_serial *)dev_id;
	for (i = 0; i < NUM_SERIAL; i++) {
		zs_intreg = read_zsreg(info->zs_next->zs_channel, 2);
		zs_intreg &= STATUS_MASK;

		/* NOTE: The read register 2, which holds the irq status,
		 *       does so for both channels on each chip.  Although
		 *       the status value itself must be read from the B
		 *       channel and is only valid when read from channel B.
		 *       When read from channel A, read register 2 contains
		 *       the value written to write register 2.
		 */

		/* Channel A -- /dev/ttya or /dev/kbd, could be the console */
		if (zs_intreg == CHA_Rx_AVAIL) {
			receive_chars(info, regs);
			return;
		}
		if(zs_intreg == CHA_Tx_EMPTY) {
			transmit_chars(info);
			return;
		}
		if (zs_intreg == CHA_EXT_STAT) {
			status_handle(info);
			return;
		}
		if (zs_intreg == CHA_SPECIAL) {
			special_receive(info);
			return;
		}

		/* Channel B -- /dev/ttyb or /dev/mouse, could be the console */
		if(zs_intreg == CHB_Rx_AVAIL) {
			receive_chars(info->zs_next, regs);
			return;
		}
		if(zs_intreg == CHB_Tx_EMPTY) {
			transmit_chars(info->zs_next);
			return;
		}
		if (zs_intreg == CHB_EXT_STAT) {
			status_handle(info->zs_next);
			return;
		}

		/* NOTE: The default value for the IRQ status in read register
		 *       2 in channel B is CHB_SPECIAL, so we need to look at
		 *       read register 3 in channel A to check if this is a
		 *       real interrupt, or just the default value.
		 *       Yes... broken hardware...
		 */

		zs_intreg = read_zsreg(info->zs_channel, 3);
		if (zs_intreg & CHBRxIP) {
			special_receive(info->zs_next);
			return;
		}
		info = info->zs_next->zs_next;
	}
}

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * This routine is used to handle the "bottom half" processing for the
 * serial driver, known also the "software interrupt" processing.
 * This processing is done at the kernel interrupt level, after the
 * rs_interrupt() has returned, BUT WITH INTERRUPTS TURNED ON.  This
 * is where time-consuming activities which can not be done in the
 * interrupt driver proper are done; the interrupt driver schedules
 * them using rs_sched_event(), and they get done here.
 */
static void do_serial_bh(void)
{
	run_task_queue(&tq_serial);
}

static void do_softint(void *private_)
{
	struct sun_serial	*info = (struct sun_serial *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	if (clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
	}
}

/*
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (scheduler tqueue) ->
 * 	do_serial_hangup() -> tty->hangup() -> rs_hangup()
 * 
 */
static void do_serial_hangup(void *private_)
{
	struct sun_serial	*info = (struct sun_serial *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;
#ifdef SERIAL_DEBUG_OPEN
	printk("do_serial_hangup<%p: tty-%d\n",
		__builtin_return_address(0), info->line);
#endif

	tty_hangup(tty);
}


/*
 * This subroutine is called when the RS_TIMER goes off.  It is used
 * by the serial driver to handle ports that do not have an interrupt
 * (irq=0).  This doesn't work at all for 16450's, as a sun has a Z8530.
 */
 
static void rs_timer(void)
{
	printk("rs_timer called\n");
	prom_halt();
	return;
}

static int startup(struct sun_serial * info)
{
	unsigned long flags;

	if (info->flags & ZILOG_INITIALIZED)
		return 0;

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *) get_free_page(GFP_KERNEL);
		if (!info->xmit_buf)
			return -ENOMEM;
	}

	save_flags(flags); cli();

#ifdef SERIAL_DEBUG_OPEN
	printk("Starting up tty-%d (irq %d)...\n", info->line, info->irq);
#endif

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */
	ZS_CLEARFIFO(info->zs_channel);
	info->xmit_fifo_size = 1;

	/*
	 * Clear the interrupt registers.
	 */
	info->zs_channel->control = ERR_RES;
	udelay(5);
	info->zs_channel->control = RES_H_IUS;
	udelay(5);

	/*
	 * Now, initialize the Zilog
	 */
	zs_rtsdtr(info, 1);

	/*
	 * Finally, enable sequencing and interrupts
	 */
	info->curregs[1] |= (info->curregs[1] & ~(RxINT_MASK)) |
				(EXT_INT_ENAB | INT_ALL_Rx);
	info->curregs[3] |= (RxENAB | Rx8);
	/* We enable Tx interrupts as needed. */
	info->curregs[5] |= (TxENAB | Tx8);
	info->curregs[9] |= (NV | MIE);
	write_zsreg(info->zs_channel, 3, info->curregs[3]);
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	write_zsreg(info->zs_channel, 9, info->curregs[9]);
	
	/*
	 * And clear the interrupt registers again for luck.
	 */
	info->zs_channel->control = ERR_RES;
	udelay(5);
	info->zs_channel->control = RES_H_IUS;
	udelay(5);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * Set up serial timers...
	 */
#if 0  /* Works well and stops the machine. */
	timer_table[RS_TIMER].expires = jiffies + 2;
	timer_active |= 1 << RS_TIMER;
#endif

	/*
	 * and set the speed of the serial port
	 */
	change_speed(info);

	info->flags |= ZILOG_INITIALIZED;
	restore_flags(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct sun_serial * info)
{
	unsigned long	flags;

	if (!(info->flags & ZILOG_INITIALIZED))
		return;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....", info->line,
	       info->irq);
#endif
	
	save_flags(flags); cli(); /* Disable interrupts */
	
	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	
	info->flags &= ~ZILOG_INITIALIZED;
	restore_flags(flags);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct sun_serial *info)
{
	unsigned short port;
	unsigned cflag;
	int	quot = 0;
	int	i;
	int	brg;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (!(port = info->port))
		return;
	i = cflag & CBAUD;
	if (cflag & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i != 5)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			i = 16;
	}
	if (i == 15) {
		if ((info->flags & ZILOG_SPD_MASK) == ZILOG_SPD_HI)
			i += 1;
		if ((info->flags & ZILOG_SPD_MASK) == ZILOG_SPD_CUST)
			quot = info->custom_divisor;
	}
	if (quot) {
		info->zs_baud = info->baud_base / quot;
		info->clk_divisor = 16;

		info->curregs[4] = X16CLK;
		info->curregs[11] = TCBR | RCBR;
		brg = BPS_TO_BRG(info->zs_baud, ZS_CLOCK/info->clk_divisor);
		info->curregs[12] = (brg & 255);
		info->curregs[13] = ((brg >> 8) & 255);
		info->curregs[14] = BRSRC | BRENAB;
		zs_rtsdtr(info, 1);
	} else if (baud_table[i]) {
		info->zs_baud = baud_table[i];
		info->clk_divisor = 16;

		info->curregs[4] = X16CLK;
		info->curregs[11] = TCBR | RCBR;
		brg = BPS_TO_BRG(info->zs_baud, ZS_CLOCK/info->clk_divisor);
		info->curregs[12] = (brg & 255);
		info->curregs[13] = ((brg >> 8) & 255);
		info->curregs[14] = BRSRC | BRENAB;
		zs_rtsdtr(info, 1);
	} else {
		zs_rtsdtr(info, 0);
		return;
	}

	/* byte size and parity */
	switch (cflag & CSIZE) {
	case CS5:
		info->curregs[3] &= ~(RxN_MASK);
		info->curregs[3] |= Rx5;
		info->curregs[5] &= ~(TxN_MASK);
		info->curregs[5] |= Tx5;
		info->parity_mask = 0x1f;
		break;
	case CS6:
		info->curregs[3] &= ~(RxN_MASK);
		info->curregs[3] |= Rx6;
		info->curregs[5] &= ~(TxN_MASK);
		info->curregs[5] |= Tx6;
		info->parity_mask = 0x3f;
		break;
	case CS7:
		info->curregs[3] &= ~(RxN_MASK);
		info->curregs[3] |= Rx7;
		info->curregs[5] &= ~(TxN_MASK);
		info->curregs[5] |= Tx7;
		info->parity_mask = 0x7f;
		break;
	case CS8:
	default: /* defaults to 8 bits */
		info->curregs[3] &= ~(RxN_MASK);
		info->curregs[3] |= Rx8;
		info->curregs[5] &= ~(TxN_MASK);
		info->curregs[5] |= Tx8;
		info->parity_mask = 0xff;
		break;
	}
	info->curregs[4] &= ~(0x0c);
	if (cflag & CSTOPB) {
		info->curregs[4] |= SB2;
	} else {
		info->curregs[4] |= SB1;
	}
	if (cflag & PARENB) {
		info->curregs[4] |= PAR_ENAB;
	} else {
		info->curregs[4] &= ~PAR_ENAB;
	}
	if (!(cflag & PARODD)) {
		info->curregs[4] |= PAR_EVEN;
	} else {
		info->curregs[4] &= ~PAR_EVEN;
	}

	/* Load up the new values */
	load_zsregs(info, info->curregs);

	return;
}

/* This is for mouse/keyboard output.
 * XXX mouse output??? can we send it commands??? XXX
 */
void kbd_put_char(unsigned char ch)
{
	struct sun_zschannel *chan = zs_kbdchan;
	unsigned long flags;

	if(!chan)
		return;

	save_flags(flags); cli();
	zs_put_char(chan, ch);
	restore_flags(flags);
}

void mouse_put_char(char ch)
{
	struct sun_zschannel *chan = zs_mousechan;
	unsigned long flags;

	if(!chan)
		return;

	save_flags(flags); cli();
	zs_put_char(chan, ch);
	restore_flags(flags);
}


/* This is for console output over ttya/ttyb */
static void rs_put_char(char ch)
{
	struct sun_zschannel *chan = zs_conschan;
	unsigned long flags;

	if(!chan)
		return;

	save_flags(flags); cli();
	zs_put_char(chan, ch);
	restore_flags(flags);
}

/* These are for receiving and sending characters under the kgdb
 * source level kernel debugger.
 */
void putDebugChar(char kgdb_char)
{
	struct sun_zschannel *chan = zs_kgdbchan;

	while((chan->control & Tx_BUF_EMP)==0)
		udelay(5);
	chan->data = kgdb_char;
}

char getDebugChar(void)
{
	struct sun_zschannel *chan = zs_kgdbchan;

	while((chan->control & Rx_CH_AV)==0)
		barrier();
	return chan->data;
}

/*
 * Fair output driver allows a process to speak.
 */
static void rs_fair_output(void)
{
	int left;		/* Output no more than that */
	unsigned long flags;
	struct sun_serial *info = zs_consinfo;
	char c;

	if (info == 0) return;
	if (info->xmit_buf == 0) return;

	save_flags(flags);  cli();
	left = info->xmit_cnt;
	while (left != 0) {
		c = info->xmit_buf[info->xmit_tail];
		info->xmit_tail = (info->xmit_tail+1) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt--;
		restore_flags(flags);

		rs_put_char(c);

		cli();
		left = MIN(info->xmit_cnt, left-1);
	}

	/* Last character is being transmitted now (hopefully). */
	zs_conschan->control = RES_Tx_P;
	udelay(5);

	restore_flags(flags);
	return;
}

/*
 * zs_console_print is registered for printk.
 */
static void zs_console_print(const char *p)
{
	char c;

	while((c=*(p++)) != 0) {
		if(c == '\n')
			rs_put_char('\r');
		rs_put_char(c);
	}

	/* Comment this if you want to have a strict interrupt-driven output */
	rs_fair_output();

	return;
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_flush_chars"))
		return;

	if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !info->xmit_buf)
		return;

	/* Enable transmitter */
	save_flags(flags); cli();
	info->curregs[1] |= TxINT_ENAB|EXT_INT_ENAB;
	write_zsreg(info->zs_channel, 1, info->curregs[1]);
	info->curregs[5] |= TxENAB;
	write_zsreg(info->zs_channel, 5, info->curregs[5]);

	/*
	 * Send a first (bootstrapping) character. A best solution is
	 * to call transmit_chars() here which handles output in a
	 * generic way. Current transmit_chars() not only transmits,
	 * but resets interrupts also what we do not desire here.
	 * XXX Discuss with David.
	 */
	zs_put_char(info->zs_channel, info->xmit_buf[info->xmit_tail++]);
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
	info->xmit_cnt--;

	restore_flags(flags);
}

static int rs_write(struct tty_struct * tty, int from_user,
		    const unsigned char *buf, int count)
{
	int	c, total = 0;
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_write"))
		return 0;

	if (!info || !info->xmit_buf)
		return 0;

	save_flags(flags);
	while (1) {
		cli();		
		c = MIN(count, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				   SERIAL_XMIT_SIZE - info->xmit_head));
		if (c <= 0)
			break;

		if (from_user) {
			down(&tmp_buf_sem);
			copy_from_user(tmp_buf, buf, c);
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

	if (info->xmit_cnt && !tty->stopped && !tty->hw_stopped) {
		/* Enable transmitter */
		info->curregs[1] |= TxINT_ENAB|EXT_INT_ENAB;
		write_zsreg(info->zs_channel, 1, info->curregs[1]);
		info->curregs[5] |= TxENAB;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
	}
#if 1
	if (info->xmit_cnt && !tty->stopped && !tty->hw_stopped) {
		zs_put_char(info->zs_channel,
			    info->xmit_buf[info->xmit_tail++]);
		info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt--;
	}
#endif
	restore_flags(flags);
	return total;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
	int	ret;
				
	if (serial_paranoia_check(info, tty->device, "rs_write_room"))
		return 0;
	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int rs_chars_in_buffer(struct tty_struct *tty)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->device, "rs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->device, "rs_flush_buffer"))
		return;
	cli();
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	sti();
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_throttle(struct tty_struct * tty)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("throttle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "rs_throttle"))
		return;
	
	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);

	/* Turn off RTS line */
	cli();
	info->curregs[5] &= ~RTS;
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	sti();
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "rs_unthrottle"))
		return;
	
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}

	/* Assert RTS line */
	cli();
	info->curregs[5] |= RTS;
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	sti();
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct sun_serial * info,
			   struct serial_struct * retinfo)
{
	struct serial_struct tmp;
  
	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->port;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = info->custom_divisor;
	copy_to_user_ret(retinfo,&tmp,sizeof(*retinfo), -EFAULT);
	return 0;
}

static int set_serial_info(struct sun_serial * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct sun_serial old_info;
	int 			retval = 0;

	if (!new_info || copy_from_user(&new_serial,new_info,sizeof(new_serial)))
		return -EFAULT;
	old_info = *info;

	if (!suser()) {
		if ((new_serial.baud_base != info->baud_base) ||
		    (new_serial.type != info->type) ||
		    (new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ~ZILOG_USR_MASK) !=
		     (info->flags & ~ZILOG_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ZILOG_USR_MASK) |
			       (new_serial.flags & ZILOG_USR_MASK));
		info->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if (info->count > 1)
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->baud_base = new_serial.baud_base;
	info->flags = ((info->flags & ~ZILOG_FLAGS) |
			(new_serial.flags & ZILOG_FLAGS));
	info->custom_divisor = new_serial.custom_divisor;
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;

check_and_exit:
	retval = startup(info);
	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space. 
 */
static int get_lsr_info(struct sun_serial * info, unsigned int *value)
{
	unsigned char status;

	cli();
	status = info->zs_channel->control;
	sti();
	put_user_ret(status,value, -EFAULT);
	return 0;
}

static int get_modem_info(struct sun_serial * info, unsigned int *value)
{
	unsigned char status;
	unsigned int result;

	cli();
	status = info->zs_channel->control;
	sti();
	result =  ((info->curregs[5] & RTS) ? TIOCM_RTS : 0)
		| ((info->curregs[5] & DTR) ? TIOCM_DTR : 0)
		| ((status  & DCD) ? TIOCM_CAR : 0)
		| ((status  & SYNC) ? TIOCM_DSR : 0)
		| ((status  & CTS) ? TIOCM_CTS : 0);
	put_user_ret(result, value, -EFAULT);
	return 0;
}

static int set_modem_info(struct sun_serial * info, unsigned int cmd,
			  unsigned int *value)
{
	unsigned int arg;

	get_user_ret(arg, value, -EFAULT);
	switch (cmd) {
	case TIOCMBIS: 
		if (arg & TIOCM_RTS)
			info->curregs[5] |= RTS;
		if (arg & TIOCM_DTR)
			info->curregs[5] |= DTR;
		break;
	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			info->curregs[5] &= ~RTS;
		if (arg & TIOCM_DTR)
			info->curregs[5] &= ~DTR;
		break;
	case TIOCMSET:
		info->curregs[5] = ((info->curregs[5] & ~(RTS | DTR))
			     | ((arg & TIOCM_RTS) ? RTS : 0)
			     | ((arg & TIOCM_DTR) ? DTR : 0));
		break;
	default:
		return -EINVAL;
	}
	cli();
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	sti();
	return 0;
}

/*
 * This routine sends a break character out the serial port.
 */
static void send_break(	struct sun_serial * info, int duration)
{
	if (!info->port)
		return;
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + duration;
	cli();
	write_zsreg(info->zs_channel, 5, (info->curregs[5] | SND_BRK));
	schedule();
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	sti();
}

static int rs_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	struct sun_serial * info = (struct sun_serial *)tty->driver_data;
	int retval;

	if (serial_paranoia_check(info, tty->device, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD)  &&
	    (cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}
	
	switch (cmd) {
		case TCSBRK:	/* SVID version: non-zero arg --> no break */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (!arg)
				send_break(info, HZ/4);	/* 1/4 second */
			return 0;
		case TCSBRKP:	/* support for POSIX tcsendbreak() */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			send_break(info, arg ? arg*(HZ/10) : HZ/4);
			return 0;
		case TIOCGSOFTCAR:
			put_user_ret(C_CLOCAL(tty) ? 1 : 0,
				     (unsigned long *) arg, -EFAULT);
			return 0;
		case TIOCSSOFTCAR:
			get_user_ret(arg, (unsigned long *) arg, -EFAULT);
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (arg ? CLOCAL : 0));
			return 0;
		case TIOCMGET:
			return get_modem_info(info, (unsigned int *) arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return set_modem_info(info, cmd, (unsigned int *) arg);
		case TIOCGSERIAL:
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERGSTRUCT:
			copy_to_user_ret((struct sun_serial *) arg,
				    info, sizeof(struct sun_serial), -EFAULT);
			return 0;
			
		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct sun_serial *info = (struct sun_serial *)tty->driver_data;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;

	change_speed(info);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}
}

/*
 * ------------------------------------------------------------
 * rs_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * ZILOG structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct sun_serial * info = (struct sun_serial *)tty->driver_data;
	unsigned long flags;

	if (!info || serial_paranoia_check(info, tty->device, "rs_close"))
		return;
	
	save_flags(flags); cli();
	
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
		return;
	}
	
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_close tty-%d, count = %d\n", info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("rs_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk("rs_close: bad serial port count for ttys%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		restore_flags(flags);
		return;
	}
	info->flags |= ZILOG_CLOSING;
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ZILOG_NORMAL_ACTIVE)
		info->normal_termios = *tty->termios;
	if (info->flags & ZILOG_CALLOUT_ACTIVE)
		info->callout_termios = *tty->termios;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ZILOG_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	/** if (!info->iscons) ... **/
	info->curregs[3] &= ~RxENAB;
	write_zsreg(info->zs_channel, 3, info->curregs[3]);
	info->curregs[1] &= ~(RxINT_MASK);
	write_zsreg(info->zs_channel, 1, info->curregs[1]);
	ZS_CLEARFIFO(info->zs_channel);

	shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close)(tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open)
			(tty->ldisc.open)(tty);
	}
	if (info->blocked_open) {
		if (info->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + info->close_delay;
			schedule();
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ZILOG_NORMAL_ACTIVE|ZILOG_CALLOUT_ACTIVE|
			 ZILOG_CLOSING);
	wake_up_interruptible(&info->close_wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_close tty-%d exiting, count = %d\n", info->line, info->count);
#endif
	restore_flags(flags);
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void rs_hangup(struct tty_struct *tty)
{
	struct sun_serial * info = (struct sun_serial *)tty->driver_data;
	
	if (serial_paranoia_check(info, tty->device, "rs_hangup"))
		return;
	
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_hangup<%p: tty-%d, count = %d bye\n",
		__builtin_return_address(0), info->line, info->count);
#endif

	rs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~(ZILOG_NORMAL_ACTIVE|ZILOG_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct sun_serial *info)
{
	struct wait_queue wait = { current, NULL };
	int		retval;
	int		do_clocal = 0;
	unsigned char	r0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & ZILOG_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & ZILOG_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
		if (info->flags & ZILOG_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ZILOG_CALLOUT_ACTIVE) &&
		    (info->flags & ZILOG_SESSION_LOCKOUT) &&
		    (info->session != current->session))
		    return -EBUSY;
		if ((info->flags & ZILOG_CALLOUT_ACTIVE) &&
		    (info->flags & ZILOG_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
		    return -EBUSY;
		info->flags |= ZILOG_CALLOUT_ACTIVE;
		return 0;
	}
	
	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ZILOG_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ZILOG_NORMAL_ACTIVE;
		return 0;
	}

	if (info->flags & ZILOG_CALLOUT_ACTIVE) {
		if (info->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1;
	} else {
		if (tty->termios->c_cflag & CLOCAL)
			do_clocal = 1;
	}
	
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttys%d, count = %d\n",
	       info->line, info->count);
#endif
	cli();
	if(!tty_hung_up_p(filp))
		info->count--;
	sti();
	info->blocked_open++;
	while (1) {
		cli();
		if (!(info->flags & ZILOG_CALLOUT_ACTIVE))
			zs_rtsdtr(info, 1);
		sti();
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ZILOG_INITIALIZED)) {
#ifdef SERIAL_DEBUG_OPEN
			printk("block_til_ready hup-ed: ttys%d, count = %d\n",
				info->line, info->count);
#endif
#ifdef SERIAL_DO_RESTART
			if (info->flags & ZILOG_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}

		cli();
		r0 = read_zsreg(info->zs_channel, R0);
		sti();
		if (!(info->flags & ZILOG_CALLOUT_ACTIVE) &&
		    !(info->flags & ZILOG_CLOSING) &&
		    (do_clocal || (DCD & r0)))
			break;
		if (current->signal & ~current->blocked) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttys%d, count = %d\n",
		       info->line, info->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttys%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ZILOG_NORMAL_ACTIVE;
	return 0;
}	

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its ZILOG structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct sun_serial	*info;
	int 			retval, line;

	line = MINOR(tty->device) - tty->driver.minor_start;
	/* The zilog lines for the mouse/keyboard must be
	 * opened using their respective drivers.
	 */
	if ((line < 0) || (line >= NUM_CHANNELS))
		return -ENODEV;
	if((line == KEYBOARD_LINE) || (line == MOUSE_LINE))
		return -ENODEV;
	info = zs_soft + line;
	/* Is the kgdb running over this line? */
	if (info->kgdb_channel)
		return -ENODEV;
	if (serial_paranoia_check(info, tty->device, "rs_open"))
		return -ENODEV;
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open %s%d, count = %d\n", tty->driver.name, info->line,
	       info->count);
#endif
	if (info->tty != 0 && info->tty != tty) {
		/* Never happen? */
		printk("rs_open %s%d, tty overwrite.\n", tty->driver.name, info->line);
		return -EBUSY;
	}
	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval)
		return retval;

	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("rs_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}

	if ((info->count == 1) && (info->flags & ZILOG_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else 
			*tty->termios = info->callout_termios;
		change_speed(info);
	}

	info->session = current->session;
	info->pgrp = current->pgrp;

#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open ttys%d successful...", info->line);
#endif
	return 0;
}

/* Finally, routines used to initialize the serial driver. */

static void show_serial_version(void)
{
	char *revision = "$Revision: 1.39 $";
	char *version, *p;

	version = strchr(revision, ' ');
	p = strchr(++version, ' ');
	*p = '\0';
	printk("Sparc Zilog8530 serial driver version %s\n", version);
	*p = ' ';
}

/* Probe the PROM for the request zs chip number.
 *
 * Note: The Sun Voyager shows two addresses and two intr for it's
 *       Zilogs, what the second does, I don't know. It does work
 *       with using only the first number of each property.
 */
static struct sun_zslayout *get_zs(int chip)
{
	struct linux_prom_irqs tmp_irq[2];
	unsigned int paddr = 0;
	unsigned int vaddr[2] = { 0, 0 };
	int zsnode, tmpnode, iospace, slave, len, seen, sun4u_irq;
	static int irq = 0;

#if CONFIG_AP1000
        printk("No zs chip\n");
        return NULL;
#endif

	iospace = 0;
	if(chip < 0 || chip >= NUM_SERIAL)
		panic("get_zs bogon zs chip number");

	if(sparc_cpu_model == sun4) {
		/* Grrr, these have to be hardcoded aieee */
		switch(chip) {
		case 0:
			paddr = 0xf1000000;
			break;
		case 1:
			paddr = 0xf0000000;
			break;
		};
		iospace = 0;
		zs_nodes[chip] = 0;
		if(!irq)
			zilog_irq = irq = 12;
		vaddr[0] = (unsigned long)
				sparc_alloc_io(paddr, 0, 8,
					       "Zilog Serial", iospace, 0);
	} else {
		/* Can use the prom for other machine types */
		zsnode = prom_getchild(prom_root_node);
		if (sparc_cpu_model == sun4d) {
			int board, node;
			
			tmpnode = zsnode;
			while (tmpnode && (tmpnode = prom_searchsiblings(tmpnode, "cpu-unit"))) {
				board = prom_getintdefault (tmpnode, "board#", -1);
				if (board == (chip >> 1)) {
					node = prom_getchild(tmpnode);
					if (node && (node = prom_searchsiblings(node, "bootbus"))) {
						zsnode = node;
						break;
					}
				}
				tmpnode = prom_getsibling(tmpnode);
			}
			if (!tmpnode)
				panic ("get_zs: couldn't find board%d's bootbus\n", chip >> 1);
		} else if (sparc_cpu_model == sun4u) {
			tmpnode = prom_searchsiblings(zsnode, "sbus");
			if(tmpnode)
				zsnode = prom_getchild(tmpnode);
		} else {
			tmpnode = prom_searchsiblings(zsnode, "obio");
			if(tmpnode)
				zsnode = prom_getchild(tmpnode);
		}
		if(!zsnode)
			panic("get_zs no zs serial prom node");
		seen = 0;
		while(zsnode) {
			zsnode = prom_searchsiblings(zsnode, "zs");
			slave = prom_getintdefault(zsnode, "slave", -1);
			if((slave == chip) ||
			   (sparc_cpu_model == sun4u && seen == chip)) {
				/* The one we want */
				len = prom_getproperty(zsnode, "address",
						       (void *) vaddr,
						       sizeof(vaddr));
        			if (len % sizeof(unsigned int)) {
					prom_printf("WHOOPS:  proplen for %s "
						"was %d, need multiple of "
						"%d\n", "address", len,
						sizeof(unsigned int));
					panic("zilog: address property");
				}
				zs_nodes[chip] = zsnode;
				if(sparc_cpu_model == sun4u) {
					len = prom_getproperty(zsnode, "interrupts",
							       (char *) &sun4u_irq,
							       sizeof(tmp_irq));
					tmp_irq[0].pri = sun4u_irq;
				} else {
					len = prom_getproperty(zsnode, "intr",
							       (char *) tmp_irq,
							       sizeof(tmp_irq));
					if (len % sizeof(struct linux_prom_irqs)) {
						prom_printf(
						      "WHOOPS:  proplen for %s "
						      "was %d, need multiple of "
						      "%d\n", "address", len,
						      sizeof(struct linux_prom_irqs));
						panic("zilog: address property");
					}
				}
				if(!irq) {
					irq = zilog_irq = tmp_irq[0].pri;
				} else {
					if(tmp_irq[0].pri != irq)
						panic("zilog: bogon irqs");
				}
				break;
			}
			zsnode = prom_getsibling(zsnode);
			seen++;
		}
		if(!zsnode)
			panic("get_zs whee chip not found");
	}
	if(!vaddr[0])
		panic("get_zs whee no serial chip mappable");

	return (struct sun_zslayout *)(unsigned long) vaddr[0];
}

static inline void
init_zscons_termios(struct termios *termios)
{
	char mode[16], buf[16];
	char *mode_prop = "ttyX-mode";
	char *cd_prop = "ttyX-ignore-cd";
	char *dtr_prop = "ttyX-rts-dtr-off";
	char *s;
	int baud, bits, cflag;
	char parity;
	int topnd, nd;
	int channel, stop;
	int carrier = 0;
	int rtsdtr = 1;
	extern int serial_console;

	if (!serial_console)
		return;

	if (serial_console == 1) {
		mode_prop[3] = 'a';
		cd_prop[3] = 'a';
		dtr_prop[3] = 'a';
	} else {
		mode_prop[3] = 'b';
		cd_prop[3] = 'b';
		dtr_prop[3] = 'b';
	}

	topnd = prom_getchild(prom_root_node);
	nd = prom_searchsiblings(topnd, "options");
	if (!nd) {
		strcpy(mode, "9600,8,n,1,-");
		goto no_options;
	}

	if (!prom_node_has_property(nd, mode_prop)) {
		strcpy(mode, "9600,8,n,1,-");
		goto no_options;
	}

	memset(mode, 0, sizeof(mode));
	prom_getstring(nd, mode_prop, mode, sizeof(mode));

	if (prom_node_has_property(nd, cd_prop)) {
		memset(buf, 0, sizeof(buf));
		prom_getstring(nd, cd_prop, buf, sizeof(buf));
		if (!strcmp(buf, "false"))
			carrier = 1;

		/* XXX this is unused below. */
	}

	if (prom_node_has_property(nd, cd_prop)) {
		memset(buf, 0, sizeof(buf));
		prom_getstring(nd, cd_prop, buf, sizeof(buf));
		if (!strcmp(buf, "false"))
			rtsdtr = 0;

		/* XXX this is unused below. */
	}

no_options:
	cflag = CREAD | HUPCL | CLOCAL;

	s = mode;
	baud = simple_strtoul(s, 0, 0);
	s = strchr(s, ',');
	bits = simple_strtoul(++s, 0, 0);
	s = strchr(s, ',');
	parity = *(++s);
	s = strchr(s, ',');
	stop = simple_strtoul(++s, 0, 0);
	s = strchr(s, ',');
	/* XXX handshake is not handled here. */

	for (channel = 0; channel < NUM_CHANNELS; channel++)
		if (zs_soft[channel].is_cons)
			break;

	switch (baud) {
		case 150:
			cflag |= B150;
			break;
		case 300:
			cflag |= B300;
			break;
		case 600:
			cflag |= B600;
			break;
		case 1200:
			cflag |= B1200;
			break;
		case 2400:
			cflag |= B2400;
			break;
		case 4800:
			cflag |= B4800;
			break;
		default:
			baud = 9600;
		case 9600:
			cflag |= B9600;
			break;
		case 19200:
			cflag |= B19200;
			break;
		case 38400:
			cflag |= B38400;
			break;
	}
	zs_soft[channel].zs_baud = baud;

	switch (bits) {
		case 5:
			zscons_regs[3] = Rx5 | RxENAB;
			zscons_regs[5] = Tx5 | TxENAB;
			zs_soft[channel].parity_mask = 0x1f;
			cflag |= CS5;
			break;
		case 6:
			zscons_regs[3] = Rx6 | RxENAB;
			zscons_regs[5] = Tx6 | TxENAB;
			zs_soft[channel].parity_mask = 0x3f;
			cflag |= CS6;
			break;
		case 7:
			zscons_regs[3] = Rx7 | RxENAB;
			zscons_regs[5] = Tx7 | TxENAB;
			zs_soft[channel].parity_mask = 0x7f;
			cflag |= CS7;
			break;
		default:
		case 8:
			zscons_regs[3] = Rx8 | RxENAB;
			zscons_regs[5] = Tx8 | TxENAB;
			zs_soft[channel].parity_mask = 0xff;
			cflag |= CS8;
			break;
	}
	zscons_regs[5] |= DTR;

	switch (parity) {
		case 'o':
			zscons_regs[4] |= PAR_ENAB;
			cflag |= (PARENB | PARODD);
			break;
		case 'e':
			zscons_regs[4] |= (PAR_ENAB | PAR_EVEN);
			cflag |= PARENB;
			break;
		default:
		case 'n':
			break;
	}

	switch (stop) {
		default:
		case 1:
			zscons_regs[4] |= SB1;
			break;
		case 2:
			cflag |= CSTOPB;
			zscons_regs[4] |= SB2;
			break;
	}

	termios->c_cflag = cflag;
}

extern void register_console(void (*proc)(const char *));

static inline void
rs_cons_check(struct sun_serial *ss, int channel)
{
	int i, o, io;
	static int consout_registered = 0;
	static int msg_printed = 0;

	i = o = io = 0;

	/* Is this one of the serial console lines? */
	if((zs_cons_chanout != channel) &&
	   (zs_cons_chanin != channel))
		return;
	zs_conschan = ss->zs_channel;
	zs_consinfo = ss;

	/* Register the console output putchar, if necessary */
	if((zs_cons_chanout == channel)) {
		o = 1;
		/* double whee.. */
		if(!consout_registered) {
			extern void serial_finish_init (void (*)(const char *));

			serial_finish_init (zs_console_print);
			register_console(zs_console_print);
			consout_registered = 1;
		}
	}

	/* If this is console input, we handle the break received
	 * status interrupt on this line to mean prom_halt().
	 */
	if(zs_cons_chanin == channel) {
		ss->break_abort = 1;
		i = 1;
	}
	if(o && i)
		io = 1;

	/* Set flag variable for this port so that it cannot be
	 * opened for other uses by accident.
	 */
	ss->is_cons = 1;

	if(io) {
		if(!msg_printed) {
			printk("zs%d: console I/O\n", ((channel>>1)&1));
			msg_printed = 1;
		}
	} else {
		printk("zs%d: console %s\n", ((channel>>1)&1),
		       (i==1 ? "input" : (o==1 ? "output" : "WEIRD")));
	}
}

extern void keyboard_zsinit(void);
extern void sun_mouse_zsinit(void);

/* This is for the auto baud rate detection in the mouse driver. */
void zs_change_mouse_baud(int newbaud)
{
	int channel = MOUSE_LINE;
	int brg;

	zs_soft[channel].zs_baud = newbaud;
	brg = BPS_TO_BRG(zs_soft[channel].zs_baud,
			 (ZS_CLOCK / zs_soft[channel].clk_divisor));
	write_zsreg(zs_soft[channel].zs_channel, R12, (brg & 0xff));
	write_zsreg(zs_soft[channel].zs_channel, R13, ((brg >> 8) & 0xff));
}

__initfunc(unsigned long sun_serial_setup (unsigned long memory_start))
{
	char *p;
	int i;
	
	if (sparc_cpu_model == sun4d) {
		int node = prom_searchsiblings(prom_getchild(prom_root_node), "boards");
		NUM_SERIAL = 0;
		if (!node)
			panic ("Cannot find out count of boards");
		else
			node = prom_getchild(node);
		while (node && (node = prom_searchsiblings(node, "bif"))) {
			NUM_SERIAL += 2;
			node = prom_getsibling(node);
		}
	}
	p = (char *)((memory_start + 7) & ~7);
	zs_chips = (struct sun_zslayout **)(p);
	i = NUM_SERIAL * sizeof (struct sun_zslayout *);
	zs_channels = (struct sun_zschannel **)(p + i);
	i += NUM_CHANNELS * sizeof (struct sun_zschannel *);
	zs_nodes = (int *)(p + i);
	i += NUM_SERIAL * sizeof (int);
	zs_soft = (struct sun_serial *)(p + i);
	i += NUM_CHANNELS * sizeof (struct sun_serial);
	zs_ttys = (struct tty_struct *)(p + i);
	i += NUM_CHANNELS * sizeof (struct tty_struct);
	serial_table = (struct tty_struct **)(p + i);
	i += NUM_CHANNELS * sizeof (struct tty_struct *);
	serial_termios = (struct termios **)(p + i);
	i += NUM_CHANNELS * sizeof (struct termios *);
	serial_termios_locked = (struct termios **)(p + i);
	i += NUM_CHANNELS * sizeof (struct termios *);
	memset (p, 0, i);
	return (((unsigned long)p) + i + 7) & ~7;
}

__initfunc(int rs_init(void))
{
	int chip, channel, brg, i;
	unsigned long flags;
	struct sun_serial *info;
	char dummy;

#if CONFIG_AP1000
        printk("not doing rs_init()\n");
        return 0;
#endif

	/* Setup base handler, and timer table. */
	init_bh(SERIAL_BH, do_serial_bh);
	timer_table[RS_TIMER].fn = rs_timer;
	timer_table[RS_TIMER].expires = 0;

	show_serial_version();

	/* Initialize the tty_driver structure */
	/* SPARC: Not all of this is exactly right for us. */
	
	memset(&serial_driver, 0, sizeof(struct tty_driver));
	serial_driver.magic = TTY_DRIVER_MAGIC;
	serial_driver.driver_name = "serial";
	serial_driver.name = "ttyS";
	serial_driver.major = TTY_MAJOR;
	serial_driver.minor_start = 64;
	serial_driver.num = NUM_CHANNELS;
	serial_driver.type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver.subtype = SERIAL_TYPE_NORMAL;
	serial_driver.init_termios = tty_std_termios;

	serial_driver.init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver.flags = TTY_DRIVER_REAL_RAW;
	serial_driver.refcount = &serial_refcount;
	serial_driver.table = serial_table;
	serial_driver.termios = serial_termios;
	serial_driver.termios_locked = serial_termios_locked;

	serial_driver.open = rs_open;
	serial_driver.close = rs_close;
	serial_driver.write = rs_write;
	serial_driver.flush_chars = rs_flush_chars;
	serial_driver.write_room = rs_write_room;
	serial_driver.chars_in_buffer = rs_chars_in_buffer;
	serial_driver.flush_buffer = rs_flush_buffer;
	serial_driver.ioctl = rs_ioctl;
	serial_driver.throttle = rs_throttle;
	serial_driver.unthrottle = rs_unthrottle;
	serial_driver.set_termios = rs_set_termios;
	serial_driver.stop = rs_stop;
	serial_driver.start = rs_start;
	serial_driver.hangup = rs_hangup;

	/* I'm too lazy, someone write versions of this for us. -DaveM */
	serial_driver.read_proc = 0;
	serial_driver.proc_entry = 0;

	init_zscons_termios(&serial_driver.init_termios);

	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	callout_driver = serial_driver;
	callout_driver.name = "cua";
	callout_driver.major = TTYAUX_MAJOR;
	callout_driver.subtype = SERIAL_TYPE_CALLOUT;

	if (tty_register_driver(&serial_driver))
		panic("Couldn't register serial driver\n");
	if (tty_register_driver(&callout_driver))
		panic("Couldn't register callout driver\n");
	
	save_flags(flags); cli();

	/* Set up our interrupt linked list */
	zs_chain = &zs_soft[0];
	for(channel = 0; channel < NUM_CHANNELS - 1; channel++)
		zs_soft[channel].zs_next = &zs_soft[channel + 1];
	zs_soft[channel + 1].zs_next = 0;

	/* Initialize Softinfo */
	for(chip = 0; chip < NUM_SERIAL; chip++) {
		/* If we are doing kgdb over one of the channels on
		 * chip zero, kgdb_channel will be set to 1 by the
		 * rs_kgdb_hook() routine below.
		 */
		if(!zs_chips[chip]) {
			zs_chips[chip] = get_zs(chip);
			/* Two channels per chip */
			zs_channels[(chip*2)] = &zs_chips[chip]->channelA;
			zs_channels[(chip*2)+1] = &zs_chips[chip]->channelB;
			zs_soft[(chip*2)].kgdb_channel = 0;
			zs_soft[(chip*2)+1].kgdb_channel = 0;
		}

		/* First, set up channel A on this chip. */
		channel = chip * 2;
		zs_soft[channel].zs_channel = zs_channels[channel];
		zs_soft[channel].change_needed = 0;
		zs_soft[channel].clk_divisor = 16;
		zs_soft[channel].cons_keyb = 0;
		zs_soft[channel].cons_mouse = 0;
		zs_soft[channel].channelA = 1;

		/* Now, channel B */
		channel++;
		zs_soft[channel].zs_channel = zs_channels[channel];
		zs_soft[channel].change_needed = 0;
		zs_soft[channel].clk_divisor = 16;
		zs_soft[channel].cons_keyb = 0;
		zs_soft[channel].cons_mouse = 0;
		zs_soft[channel].channelA = 0;
	}

	/* Initialize Hardware */
	for(channel = 0; channel < NUM_CHANNELS; channel++) {

		/* Hardware reset each chip */
		if (!(channel & 1)) {
			write_zsreg(zs_soft[channel].zs_channel, R9, FHWRES);
			udelay(20);	/* wait for some old sun4's */
			dummy = read_zsreg(zs_soft[channel].zs_channel, R0);
		}

		if(channel == KEYBOARD_LINE) {
			zs_soft[channel].cons_keyb = 1;
			zs_soft[channel].parity_mask = 0xff;
			zs_kbdchan = zs_soft[channel].zs_channel;

			write_zsreg(zs_soft[channel].zs_channel, R4,
				    (PAR_EVEN | X16CLK | SB1));
			write_zsreg(zs_soft[channel].zs_channel, R3, Rx8);
			write_zsreg(zs_soft[channel].zs_channel, R5, Tx8);
			write_zsreg(zs_soft[channel].zs_channel, R9, NV);
			write_zsreg(zs_soft[channel].zs_channel, R10, NRZ);
			write_zsreg(zs_soft[channel].zs_channel, R11,
				    (TCBR | RCBR));
			zs_soft[channel].zs_baud = 1200;
			brg = BPS_TO_BRG(zs_soft[channel].zs_baud,
					 ZS_CLOCK/zs_soft[channel].clk_divisor);
			write_zsreg(zs_soft[channel].zs_channel, R12,
				    (brg & 0xff));
			write_zsreg(zs_soft[channel].zs_channel, R13,
				    ((brg >> 8) & 0xff));
			write_zsreg(zs_soft[channel].zs_channel, R14, BRSRC);

			/* Enable Rx/Tx, IRQs, and inform kbd driver */
			write_zsreg(zs_soft[channel].zs_channel, R14,
				    (BRSRC | BRENAB));
			write_zsreg(zs_soft[channel].zs_channel, R3,
				    (Rx8 | RxENAB));
			write_zsreg(zs_soft[channel].zs_channel, R5,
				    (Tx8 | TxENAB | DTR | RTS));

			write_zsreg(zs_soft[channel].zs_channel, R15,
				    (DCDIE | CTSIE | TxUIE | BRKIE));
			write_zsreg(zs_soft[channel].zs_channel, R0,
				    RES_EXT_INT);
			write_zsreg(zs_soft[channel].zs_channel, R0,
				    RES_EXT_INT);

			write_zsreg(zs_soft[channel].zs_channel, R1,
				    (EXT_INT_ENAB | INT_ALL_Rx));
			write_zsreg(zs_soft[channel].zs_channel, R9,
				    (NV | MIE));
			ZS_CLEARERR(zs_soft[channel].zs_channel);
			ZS_CLEARFIFO(zs_soft[channel].zs_channel);
		} else if(channel == MOUSE_LINE) {
			zs_soft[channel].cons_mouse = 1;
			zs_soft[channel].parity_mask = 0xff;
			zs_mousechan = zs_soft[channel].zs_channel;

			write_zsreg(zs_soft[channel].zs_channel, R4,
				    (PAR_EVEN | X16CLK | SB1));
			write_zsreg(zs_soft[channel].zs_channel, R3, Rx8);
			write_zsreg(zs_soft[channel].zs_channel, R5, Tx8);
			write_zsreg(zs_soft[channel].zs_channel, R9, NV);
			write_zsreg(zs_soft[channel].zs_channel, R10, NRZ);
			write_zsreg(zs_soft[channel].zs_channel, R11,
				    (TCBR | RCBR));

			zs_soft[channel].zs_baud = 4800;
			brg = BPS_TO_BRG(zs_soft[channel].zs_baud,
					 ZS_CLOCK/zs_soft[channel].clk_divisor);
			write_zsreg(zs_soft[channel].zs_channel, R12,
				    (brg & 0xff));
			write_zsreg(zs_soft[channel].zs_channel, R13,
				    ((brg >> 8) & 0xff));
			write_zsreg(zs_soft[channel].zs_channel, R14, BRSRC);

			/* Enable Rx, IRQs, and inform mouse driver */
			write_zsreg(zs_soft[channel].zs_channel, R14,
				    (BRSRC | BRENAB));
			write_zsreg(zs_soft[channel].zs_channel, R3,
				    (Rx8 | RxENAB));
			write_zsreg(zs_soft[channel].zs_channel, R5, Tx8);

			write_zsreg(zs_soft[channel].zs_channel, R15,
				    (DCDIE | CTSIE | TxUIE | BRKIE));
			write_zsreg(zs_soft[channel].zs_channel, R0,
				    RES_EXT_INT);
			write_zsreg(zs_soft[channel].zs_channel, R0,
				    RES_EXT_INT);

			write_zsreg(zs_soft[channel].zs_channel, R1,
				    (EXT_INT_ENAB | INT_ALL_Rx));
			write_zsreg(zs_soft[channel].zs_channel, R9,
				    (NV | MIE));

			sun_mouse_zsinit();
		} else if (zs_soft[channel].is_cons) {
			brg = BPS_TO_BRG(zs_soft[channel].zs_baud,
					 ZS_CLOCK/zs_soft[channel].clk_divisor);
			zscons_regs[12] = brg & 0xff;
			zscons_regs[13] = (brg >> 8) & 0xff;

			memcpy(zs_soft[channel].curregs, zscons_regs, sizeof(zscons_regs));
			load_zsregs(&zs_soft[channel], zscons_regs);

			ZS_CLEARERR(zs_soft[channel].zs_channel);
			ZS_CLEARFIFO(zs_soft[channel].zs_channel);
		} else if (zs_soft[channel].kgdb_channel) {
			/* If this is the kgdb line, enable interrupts because
			 * we now want to receive the 'control-c' character
			 * from the client attached to us asynchronously.
			 */
			zs_soft[channel].parity_mask = 0xff;
        		kgdb_chaninit(&zs_soft[channel], 1,
				      zs_soft[channel].zs_baud);
		} else {
			zs_soft[channel].parity_mask = 0xff;
			write_zsreg(zs_soft[channel].zs_channel, R4,
				    (PAR_EVEN | X16CLK | SB1));
			write_zsreg(zs_soft[channel].zs_channel, R3, Rx8);
			write_zsreg(zs_soft[channel].zs_channel, R5, Tx8);
			write_zsreg(zs_soft[channel].zs_channel, R9, NV);
			write_zsreg(zs_soft[channel].zs_channel, R10, NRZ);
			write_zsreg(zs_soft[channel].zs_channel, R11,
				    (RCBR | TCBR));
			zs_soft[channel].zs_baud = 9600;
			brg = BPS_TO_BRG(zs_soft[channel].zs_baud,
					 ZS_CLOCK/zs_soft[channel].clk_divisor);
			write_zsreg(zs_soft[channel].zs_channel, R12,
				    (brg & 0xff));
			write_zsreg(zs_soft[channel].zs_channel, R13,
				    ((brg >> 8) & 0xff));
			write_zsreg(zs_soft[channel].zs_channel, R14, BRSRC);
			write_zsreg(zs_soft[channel].zs_channel, R14,
				    (BRSRC | BRENAB));
			write_zsreg(zs_soft[channel].zs_channel, R3, Rx8);
			write_zsreg(zs_soft[channel].zs_channel, R5, Tx8);
			write_zsreg(zs_soft[channel].zs_channel, R15, DCDIE);
			write_zsreg(zs_soft[channel].zs_channel, R9, NV | MIE);
			write_zsreg(zs_soft[channel].zs_channel, R0,
				    RES_EXT_INT);
			write_zsreg(zs_soft[channel].zs_channel, R0,
				    RES_EXT_INT);
		}
	}

	for (info = zs_chain, i=0; info; info = info->zs_next, i++) {
		info->magic = SERIAL_MAGIC;
		info->port = (long) info->zs_channel;
		info->line = i;
		info->tty = 0;
		info->irq = zilog_irq;
		info->custom_divisor = 16;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		info->tqueue.routine = do_softint;
		info->tqueue.data = info;
		info->tqueue_hangup.routine = do_serial_hangup;
		info->tqueue_hangup.data = info;
		info->callout_termios = callout_driver.init_termios;
		info->normal_termios = serial_driver.init_termios;
		info->open_wait = 0;
		info->close_wait = 0;
		printk("tty%02d at 0x%04x (irq = %d)", info->line, 
		       info->port, info->irq);
		printk(" is a Zilog8530\n");
	}

	if (request_irq(zilog_irq, rs_interrupt,
			(SA_INTERRUPT | SA_STATIC_ALLOC),
			"Zilog8530", zs_chain))
		panic("Unable to attach zs intr\n");
	restore_flags(flags);

	keyboard_zsinit();
	return 0;
}

/*
 * register_serial and unregister_serial allows for serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
/* SPARC: Unused at this time, just here to make things link. */
int register_serial(struct serial_struct *req)
{
	return -1;
}

void unregister_serial(int line)
{
	return;
}

/* Hooks for running a serial console.  con_init() calls this if the
 * console is being run over one of the ttya/ttyb serial ports.
 * 'chip' should be zero, as chip 1 drives the mouse/keyboard.
 * 'channel' is decoded as 0=TTYA 1=TTYB, note that the channels
 * are addressed backwards, channel B is first, then channel A.
 */
void
rs_cons_hook(int chip, int out, int line)
{
	int channel;

	if(chip)
		panic("rs_cons_hook called with chip not zero");
	if(line != 1 && line != 2)
		panic("rs_cons_hook called with line not ttya or ttyb");
	channel = line - 1;
	if(!zs_chips[chip]) {
		zs_chips[chip] = get_zs(chip);
		/* Two channels per chip */
		zs_channels[(chip*2)] = &zs_chips[chip]->channelA;
		zs_channels[(chip*2)+1] = &zs_chips[chip]->channelB;
	}
	zs_soft[channel].zs_channel = zs_channels[channel];
	zs_soft[channel].change_needed = 0;
	zs_soft[channel].clk_divisor = 16;
	if(out)
		zs_cons_chanout = ((chip * 2) + channel);
	else
		zs_cons_chanin = ((chip * 2) + channel);
	rs_cons_check(&zs_soft[channel], channel);
}

/* This is called at boot time to prime the kgdb serial debugging
 * serial line.  The 'tty_num' argument is 0 for /dev/ttya and 1
 * for /dev/ttyb which is determined in setup_arch() from the
 * boot command line flags.
 */
void
rs_kgdb_hook(int tty_num)
{
	int chip = 0;

	if(!zs_chips[chip]) {
		zs_chips[chip] = get_zs(chip);
		/* Two channels per chip */
		zs_channels[(chip*2)] = &zs_chips[chip]->channelA;
		zs_channels[(chip*2)+1] = &zs_chips[chip]->channelB;
	}
	zs_soft[tty_num].zs_channel = zs_channels[tty_num];
	zs_kgdbchan = zs_soft[tty_num].zs_channel;
	zs_soft[tty_num].change_needed = 0;
	zs_soft[tty_num].clk_divisor = 16;
	zs_soft[tty_num].zs_baud = 9600;
	zs_soft[tty_num].kgdb_channel = 1;     /* This runs kgdb */
	zs_soft[tty_num ^ 1].kgdb_channel = 0; /* This does not */
	/* Turn on transmitter/receiver at 8-bits/char */
        kgdb_chaninit(&zs_soft[tty_num], 0, 9600);
        ZS_CLEARERR(zs_kgdbchan);
        ZS_CLEARFIFO(zs_kgdbchan);
}

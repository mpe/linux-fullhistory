/*
 * mac_SCC.c: m68k version of
 *
 * macserial.c: Serial port driver for Power Macintoshes.
 *		Extended for the 68K mac by Alan Cox.
 *              Rewritten to m68k serial design by Michael Schmitz
 *
 * Derived from drivers/sbus/char/sunserial.c by Paul Mackerras.
 *
 * Copyright (C) 1996 Paul Mackerras (Paul.Mackerras@cs.anu.edu.au)
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/*
 * Design note for the m68k rewrite:
 * The structure of the m68k serial code requires separation of the low-level
 * functions that talk directly to the hardware from the Linux serial driver 
 * code interfacing to the tty layer. The reason for this separation is simply
 * the fact that the m68k serial hardware is, unlike the i386, based on a 
 * variety of chips, and the rs_* serial routines need to be shared.
 *
 * I've tried to make consistent use of the async_struct info populated in the
 * midlevel code, and introduced an async_private struct to hold the Macintosh 
 * SCC internals (this was added to the async_struct for the PowerMac driver).
 * Exception: the console and kgdb hooks still use the zs_soft[] data, and this
 * is still filled in by the probe_sccs() routine, which provides some data 
 * for mac_SCC_init as well. Interrupts are registered in mac_SCC_init, so 
 * the console/kgdb stuff probably won't work before proper serial init, and 
 * I have to rely on keeping info and zs_soft consistent at least for the 
 * console/kgdb port.
 *
 * Update (16-11-97): The SCC interrupt handling was suffering from the problem
 * that the autovector SCC interrupt was registered only once, hence only one
 * async_struct was passed to the interrupt function and only interrupts from 
 * the corresponding channel could be handled (yes, major design flaw).
 * The autovector interrupt is now registered by the main interrupt initfunc, 
 * and uses a handler that will call the registered SCC specific interrupts in 
 * turn. The SCC init has to register these as machspec interrupts now, as is 
 * done for the VIA interrupts elsewhere.
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/config.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <asm/setup.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/macints.h>
#ifndef CONFIG_MAC
#include <asm/prom.h>
#endif
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/hwtest.h>

#include "mac_SCC.h"

/*
 * It would be nice to dynamically allocate everything that
 * depends on NUM_SERIAL, so we could support any number of
 * Z8530s, but for now...
 */
#define NUM_SERIAL	2		/* Max number of ZS chips supported */
#define NUM_CHANNELS	(NUM_SERIAL * 2)	/* 2 channels per chip */

#ifdef CONFIG_MAC
/*
 *	All the Macintosh 68K boxes that have an MMU also have hardware
 *	recovery delays.
 */
#define RECOVERY_DELAY
#else
/* On PowerMacs, the hardware takes care of the SCC recovery time,
   but we need the eieio to make sure that the accesses occur
   in the order we want. */
#define RECOVERY_DELAY	eieio()
#endif

struct mac_zschannel *zs_kgdbchan;
struct mac_zschannel zs_channels[NUM_CHANNELS];

struct m68k_async_struct  zs_soft[NUM_CHANNELS];
struct m68k_async_private zs_soft_private[NUM_CHANNELS];
int zs_channels_found;
struct m68k_async_struct *zs_chain;	/* list of all channels */

struct tty_struct zs_ttys[NUM_CHANNELS];
/** struct tty_struct *zs_constty; **/

/* Console hooks... */
static int zs_cons_chanout = 0;
static int zs_cons_chanin = 0;
struct m68k_async_struct  *zs_consinfo = 0;
struct mac_zschannel *zs_conschan;

static unsigned char kgdb_regs[16] = {
	0, 0, 0,		/* write 0, 1, 2 */
	(Rx8 | RxENABLE),	/* write 3 */
	(X16CLK | SB1 | PAR_EVEN), /* write 4 */
	(Tx8 | TxENAB),		/* write 5 */
	0, 0, 0,		/* write 6, 7, 8 */
	(NV),			/* write 9 */
	(NRZ),			/* write 10 */
	(TCBR | RCBR),		/* write 11 */
	1, 0,			/* 38400 baud divisor, write 12 + 13 */
	(BRENABL),		/* write 14 */
	(DCDIE)			/* write 15 */
};

#define ZS_CLOCK         3686400 	/* Z8530 RTxC input clock rate */

/* Debugging... DEBUG_INTR is bad to use when one of the zs
 * lines is your console ;(
 */
#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW

#define RS_STROBE_TIME 10
#define RS_ISR_PASS_LIMIT 256

#define _INLINE_ inline

static void probe_sccs(void);

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif


/***************************** Prototypes *****************************/

static void SCC_init_port( struct m68k_async_struct *info, int type, int channel );
#if 0
#ifdef MODULE
static void SCC_deinit_port( struct m68k_async_struct *info, int channel );
#endif
#endif

/* FIXME !!! Currently, only autovector interrupt used! */
#if 0
static void SCC_rx_int (int irq, void *data, struct pt_regs *fp);
static void SCC_spcond_int (int irq, void *data, struct pt_regs *fp);
static void SCC_tx_int (int irq, void *data, struct pt_regs *fp);
static void SCC_stat_int (int irq, void *data, struct pt_regs *fp);
static void SCC_ri_int (int irq, void *data, struct pt_regs *fp);
#endif

static int SCC_check_open( struct m68k_async_struct *info, struct tty_struct
                           *tty, struct file *file );
static void SCC_init( struct m68k_async_struct *info );
static void SCC_deinit( struct m68k_async_struct *info, int leave_dtr );
static void SCC_enab_tx_int( struct m68k_async_struct *info, int enab_flag );
static int SCC_check_custom_divisor( struct m68k_async_struct *info, int baud_base,
				    int divisor );
static void SCC_change_speed( struct m68k_async_struct *info );
#if 0
static int SCC_clocksrc( unsigned baud_base, unsigned channel );
#endif
static void SCC_throttle( struct m68k_async_struct *info, int status );
static void SCC_set_break( struct m68k_async_struct *info, int break_flag );
static void SCC_get_serial_info( struct m68k_async_struct *info, struct
				serial_struct *retinfo );
static unsigned int SCC_get_modem_info( struct m68k_async_struct *info );
static int SCC_set_modem_info( struct m68k_async_struct *info, int new_dtr, int
			      new_rts );
static int SCC_ioctl( struct tty_struct *tty, struct file *file, struct
		     m68k_async_struct *info, unsigned int cmd, unsigned long arg );
static void SCC_stop_receive (struct m68k_async_struct *info);
static int SCC_trans_empty (struct m68k_async_struct *info);

/************************* End of Prototypes **************************/


static SERIALSWITCH SCC_switch = {
	SCC_init, SCC_deinit, SCC_enab_tx_int,
	SCC_check_custom_divisor, SCC_change_speed,
	SCC_throttle, SCC_set_break,
	SCC_get_serial_info, SCC_get_modem_info,
	SCC_set_modem_info, SCC_ioctl, SCC_stop_receive, SCC_trans_empty,
	SCC_check_open
};

/*
 * This is used to figure out the divisor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0 };

/* 
 * Reading and writing Z8530 registers.
 */
static inline unsigned char read_zsreg(struct mac_zschannel *channel,
				       unsigned char reg)
{
	unsigned char retval;

	if (reg != 0) {
		*channel->control = reg;
		RECOVERY_DELAY;
	}
	retval = *channel->control;
	RECOVERY_DELAY;
	return retval;
}

static inline void write_zsreg(struct mac_zschannel *channel,
			       unsigned char reg, unsigned char value)
{
	if (reg != 0) {
		*channel->control = reg;
		RECOVERY_DELAY;
	}
	*channel->control = value;
	RECOVERY_DELAY;
	return;
}

static inline unsigned char read_zsdata(struct mac_zschannel *channel)
{
	unsigned char retval;

	retval = *channel->data;
	RECOVERY_DELAY;
	return retval;
}

static inline void write_zsdata(struct mac_zschannel *channel,
				unsigned char value)
{
	*channel->data = value;
	RECOVERY_DELAY;
	return;
}

static inline void load_zsregs(struct mac_zschannel *channel,
			       unsigned char *regs)
{
	ZS_CLEARERR(channel);
	ZS_CLEARFIFO(channel);
	/* Load 'em up */
	write_zsreg(channel, R4, regs[R4]);
	write_zsreg(channel, R10, regs[R10]);
	write_zsreg(channel, R3, regs[R3] & ~RxENABLE);
	write_zsreg(channel, R5, regs[R5] & ~TxENAB);
	write_zsreg(channel, R1, regs[R1]);
	write_zsreg(channel, R9, regs[R9]);
	write_zsreg(channel, R11, regs[R11]);
	write_zsreg(channel, R12, regs[R12]);
	write_zsreg(channel, R13, regs[R13]);
	write_zsreg(channel, R14, regs[R14]);
	write_zsreg(channel, R15, regs[R15]);
	write_zsreg(channel, R3, regs[R3]);
	write_zsreg(channel, R5, regs[R5]);
	return;
}

/* Sets or clears DTR/RTS on the requested line */
static inline void zs_rtsdtr(struct m68k_async_struct *ss, int set)
{
	if (set)
		ss->private->curregs[5] |= (RTS | DTR);
	else
		ss->private->curregs[5] &= ~(RTS | DTR);
	write_zsreg(ss->private->zs_channel, 5, ss->private->curregs[5]);
	return;
}

static inline void kgdb_chaninit(struct m68k_async_struct *ss, int intson, int bps)
{
	int brg;

	if (intson) {
		kgdb_regs[R1] = INT_ALL_Rx;
		kgdb_regs[R9] |= MIE;
	} else {
		kgdb_regs[R1] = 0;
		kgdb_regs[R9] &= ~MIE;
	}
	brg = BPS_TO_BRG(bps, ZS_CLOCK/16);
	kgdb_regs[R12] = brg;
	kgdb_regs[R13] = brg >> 8;
	load_zsregs(ss->private->zs_channel, kgdb_regs);
}

/* Utility routines for the Zilog */
static inline int get_zsbaud(struct m68k_async_struct *ss)
{
	struct mac_zschannel *channel = ss->private->zs_channel;
	int brg;

	/* The baud rate is split up between two 8-bit registers in
	 * what is termed 'BRG time constant' format in my docs for
	 * the chip, it is a function of the clk rate the chip is
	 * receiving which happens to be constant.
	 */
	brg = (read_zsreg(channel, 13) << 8);
	brg |= read_zsreg(channel, 12);
	return BRG_TO_BPS(brg, (ZS_CLOCK/(ss->private->clk_divisor)));
}

/* On receive, this clears errors and the receiver interrupts */
static inline void SCC_recv_clear(struct mac_zschannel *zsc)
{
	write_zsreg(zsc, 0, ERR_RES);
	write_zsreg(zsc, 0, RES_H_IUS); /* XXX this is unnecessary */
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

extern void breakpoint(void);  /* For the KGDB frame character */

static /*_INLINE_*/ void receive_chars(struct m68k_async_struct *info,
				   struct pt_regs *regs)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, stat, flag;

	while ((read_zsreg(info->private->zs_channel, 0) & Rx_CH_AV) != 0) {

		stat = read_zsreg(info->private->zs_channel, R1);
		ch = read_zsdata(info->private->zs_channel);

#ifdef SCC_DEBUG
		printk("mac_SCC: receive_chars stat=%X char=%X \n", stat, ch);
#endif

#if 0	/* KGDB not yet supported */
		/* Look for kgdb 'stop' character, consult the gdb documentation
		 * for remote target debugging and arch/sparc/kernel/sparc-stub.c
		 * to see how all this works.
		 */
		if ((info->kgdb_channel) && (ch =='\003')) {
			breakpoint();
			continue;
		}
#endif

		if (!tty)
			continue;

		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			tty_flip_buffer_push(tty);
		
		if (stat & Rx_OVR) {
			flag = TTY_OVERRUN;
			/* reset the error indication */
			write_zsreg(info->private->zs_channel, 0, ERR_RES);
		} else if (stat & FRM_ERR) {
			/* this error is not sticky */
			flag = TTY_FRAME;
		} else if (stat & PAR_ERR) {
			flag = TTY_PARITY;
			/* reset the error indication */
			write_zsreg(info->private->zs_channel, 0, ERR_RES);
		} else
			flag = 0;

		if (tty->flip.buf_num 
		    && tty->flip.count >= TTY_FLIPBUF_SIZE) {
#ifdef SCC_DEBUG_OVERRUN
			printk("mac_SCC: flip buffer overrun!\n");
#endif
			return;
		}

		if (!tty->flip.buf_num 
		    && tty->flip.count >= 2*TTY_FLIPBUF_SIZE) {
			printk("mac_SCC: double flip buffer overrun!\n");
			return;
		}

		tty->flip.count++;
		*tty->flip.flag_buf_ptr++ = flag;
		*tty->flip.char_buf_ptr++ = ch;
		info->icount.rx++;
		tty_flip_buffer_push(tty);
	}
#if 0
clear_and_exit:
	SCC_recv_clear(info->private->zs_channel);
#endif
}

/* that's SCC_enable_tx_int, basically */

static void transmit_chars(struct m68k_async_struct *info)
{
	if ((read_zsreg(info->private->zs_channel, 0) & Tx_BUF_EMP) == 0)
		return;
	info->private->tx_active = 0;

	if (info->x_char) {
		/* Send next char */
		write_zsdata(info->private->zs_channel, info->x_char);
		info->x_char = 0;
		info->private->tx_active = 1;
		return;
	}

	if ((info->xmit_cnt <= 0) || info->tty->stopped 
	     || info->private->tx_stopped) {
		write_zsreg(info->private->zs_channel, 0, RES_Tx_P);
		return;
	}

	/* Send char */
	write_zsdata(info->private->zs_channel, info->xmit_buf[info->xmit_tail++]);
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
	info->icount.tx++;
	info->xmit_cnt--;
	info->private->tx_active = 1;

	if (info->xmit_cnt < WAKEUP_CHARS)
		rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
}

static /*_INLINE_*/ void status_handle(struct m68k_async_struct *info)
{
	unsigned char status;

	/* Get status from Read Register 0 */
	status = read_zsreg(info->private->zs_channel, 0);

	/* Check for DCD transitions */
	if (((status ^ info->private->read_reg_zero) & DCD) != 0
	    && info->tty && C_CLOCAL(info->tty)) {
		if (status & DCD) {
			wake_up_interruptible(&info->open_wait);
		} else if (!(info->flags & ZILOG_CALLOUT_ACTIVE)) {
			if (info->tty)
				tty_hangup(info->tty);
		}
	}

	/* Check for CTS transitions */
	if (info->tty && C_CRTSCTS(info->tty)) {
		/*
		 * For some reason, on the Power Macintosh,
		 * it seems that the CTS bit is 1 when CTS is
		 * *negated* and 0 when it is asserted.
		 * The DCD bit doesn't seem to be inverted
		 * like this.
		 */
		if ((status & CTS) == 0) {
			if (info->private->tx_stopped) {
				info->private->tx_stopped = 0;
				if (!info->private->tx_active)
					transmit_chars(info);
			}
		} else {
			info->private->tx_stopped = 1;
		}
	}

	/* Clear status condition... */
	write_zsreg(info->private->zs_channel, 0, RES_EXT_INT);
	info->private->read_reg_zero = status;
}

/*
 * This is the serial driver's generic interrupt routine
 */
void mac_SCC_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct m68k_async_struct *info = (struct m68k_async_struct *) dev_id;
	unsigned char zs_intreg;
	int shift;

	/* NOTE: The read register 3, which holds the irq status,
	 *       does so for both channels on each chip.  Although
	 *       the status value itself must be read from the A
	 *       channel and is only valid when read from channel A.
	 *       Yes... broken hardware...
	 */
#define CHAN_IRQMASK (CHBRxIP | CHBTxIP | CHBEXT)

#ifdef SCC_DEBUG
	printk("mac_SCC: interrupt; port: %lx channel: %lx \n", 
		info->port, info->private->zs_channel);
#endif

	if (info->private->zs_chan_a == info->private->zs_channel)
		shift = 3;	/* Channel A */
	else
		shift = 0;	/* Channel B */

	for (;;) {
		zs_intreg = read_zsreg(info->private->zs_chan_a, 3);
#ifdef SCC_DEBUG
		printk("mac_SCC: status %x shift %d shifted %x \n", 
		zs_intreg, shift, zs_intreg >> shift);
#endif
		zs_intreg = zs_intreg >> shift;
		if ((zs_intreg & CHAN_IRQMASK) == 0)
			break;

		if (zs_intreg & CHBRxIP)
			receive_chars(info, regs);
		if (zs_intreg & CHBTxIP)
			transmit_chars(info);
		if (zs_intreg & CHBEXT)
			status_handle(info);
	}
}

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * ------------------------------------------------------------
 */

static void SCC_enab_tx_int( struct m68k_async_struct *info, int enab_flag )
{
	unsigned long flags;

	if (enab_flag) {
#if 0
		save_flags(flags); cli();
		if (info->private->curregs[5] & TxENAB) {
			info->private->curregs[5] &= ~TxENAB;
			info->private->pendregs[5] &= ~TxENAB;
			write_zsreg(info->private->zs_channel, 5, 
				    info->private->curregs[5]);
		}
		restore_flags(flags);
#endif
	/* FIXME: should call transmit_chars here ??? */
		transmit_chars(info);
	} else {
	save_flags(flags); cli();
#if 0
		if (  info->xmit_cnt && info->xmit_buf && 
		    !(info->private->curregs[5] & TxENAB)) {
			info->private->curregs[5] |= TxENAB;
			info->private->pendregs[5] = info->private->curregs[5];
			write_zsreg(info->private->zs_channel, 5, 
				    info->private->curregs[5]);
		}
#else
		if ( info->xmit_cnt && info->xmit_buf && 
		    !info->private->tx_active) {
			transmit_chars(info);
		}
#endif
		restore_flags(flags);	
	}

}

#if 0
/*
 * leftover from original driver ...
 */
static int SCC_startup(struct m68k_async_struct * info)
{
	unsigned long flags;

	save_flags(flags); cli();

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttyS%d (irq %d)...", info->line, info->irq);
#endif

	/*
	 * Clear the receive FIFO.
	 */
	ZS_CLEARFIFO(info->private->zs_channel);
	info->xmit_fifo_size = 1;

	/*
	 * Clear the interrupt registers.
	 */
	write_zsreg(info->private->zs_channel, 0, ERR_RES);
	write_zsreg(info->private->zs_channel, 0, RES_H_IUS);

	/*
	 * Turn on RTS and DTR.
	 */
	zs_rtsdtr(info, 1);

	/*
	 * Finally, enable sequencing and interrupts
	 */
	info->private->curregs[1] = (info->private->curregs[1] & ~0x18) 
				    | (EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB);
	info->private->pendregs[1] = info->private->curregs[1];
	info->private->curregs[3] |= (RxENABLE | Rx8);
	info->private->pendregs[3] = info->private->curregs[3];
	info->private->curregs[5] |= (TxENAB | Tx8);
	info->private->pendregs[5] = info->private->curregs[5];
	info->private->curregs[9] |= (NV | MIE);
	info->private->pendregs[9] = info->private->curregs[9];
	write_zsreg(info->private->zs_channel, 3, info->private->curregs[3]);
	write_zsreg(info->private->zs_channel, 5, info->private->curregs[5]);
	write_zsreg(info->private->zs_channel, 9, info->private->curregs[9]);

	/*
	 * Set the speed of the serial port
	 */
	SCC_change_speed(info);

	/* Save the current value of RR0 */
	info->private->read_reg_zero = read_zsreg(info->private->zs_channel, 0);

	restore_flags(flags);
	return 0;
}
#endif

/* FIXME: are these required ?? */
static int SCC_check_open( struct m68k_async_struct *info, struct tty_struct *tty,
			  struct file *file )
{
	/* check on the basis of info->whatever ?? */
	if (info->private->kgdb_channel || info->private->is_cons)
		return -EBUSY;
	return( 0 );
}

static void SCC_init( struct m68k_async_struct *info )
{
	/* FIXME: init currently done in probe_sccs() */

	/* BUT: startup part needs to be done here! */

#ifdef SCC_DEBUG
	printk("mac_SCC: init, info %lx, info->port %lx  \n", info, info->port);
#endif
	/*
	 * Clear the receive FIFO.
	 */
	ZS_CLEARFIFO(info->private->zs_channel);
	info->xmit_fifo_size = 1;

	/*
	 * Clear the interrupt registers.
	 */
	write_zsreg(info->private->zs_channel, 0, ERR_RES);
	write_zsreg(info->private->zs_channel, 0, RES_H_IUS);

	/*
	 * Turn on RTS and DTR.
	 */
	zs_rtsdtr(info, 1);

	/*
	 * Finally, enable sequencing and interrupts
	 */
	info->private->curregs[1] = (info->private->curregs[1] & ~0x18) 
				    | (EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB);
	info->private->pendregs[1] = info->private->curregs[1];
	info->private->curregs[3] |= (RxENABLE | Rx8);
	info->private->pendregs[3] = info->private->curregs[3];
	info->private->curregs[5] |= (TxENAB | Tx8);
	info->private->pendregs[5] = info->private->curregs[5];
	info->private->curregs[9] |= (NV | MIE);
	info->private->pendregs[9] = info->private->curregs[9];
	write_zsreg(info->private->zs_channel, 3, info->private->curregs[3]);
	write_zsreg(info->private->zs_channel, 5, info->private->curregs[5]);
	write_zsreg(info->private->zs_channel, 9, info->private->curregs[9]);

	/*
	 * Set the speed of the serial port - done in startup() !!
	 */
#if 0
	SCC_change_speed(info);
#endif

	/* Save the current value of RR0 */
	info->private->read_reg_zero = read_zsreg(info->private->zs_channel, 0);

}

static void SCC_init_port( struct m68k_async_struct *info, int type, int channel )
{
	static int got_autovector = 0;

#ifdef SCC_DEBUG
	printk("mac_SCC: init_port, info %x \n", info);
#endif
	info->sw = &SCC_switch;
	info->private = &zs_soft_private[channel];
	info->private->zs_channel = &zs_channels[channel];
	info->irq = IRQ4;
	info->private->clk_divisor = 16;
	info->private->zs_baud = get_zsbaud(info);
	info->port = (int) info->private->zs_channel->control;

	/*
	 * MSch: Extended interrupt scheme:
	 * The generic m68k interrupt code can't use multiple handlers for
	 * the same interrupt source (no chained interrupts).
	 * We have to plug in a 'master' interrupt handler instead, calling 
	 * mac_SCC_interrupt with the proper arguments ...
	 */

	if (!got_autovector) {
		if(sys_request_irq(IRQ4, mac_SCC_handler, 0, "SCC master", info))
			panic("macserial: can't get irq %d", IRQ4);
#ifdef SCC_DEBUG
		printk("mac_SCC: got SCC master interrupt %d, channel %d info %p\n",
			IRQ4, channel, info);
#endif
		got_autovector = 1;
	}

	if (info->private->zs_chan_a == info->private->zs_channel) {
		/* Channel A */
		if (request_irq(IRQ_SCCA, mac_SCC_interrupt, 0, "SCC A", info))
			panic("mac_SCC: can't get irq %d", IRQ_SCCA);
#ifdef SCC_DEBUG
		printk("mac_SCC: got SCC A interrupt %d, channel %d info %p\n",
			IRQ_SCCA, channel, info);
#endif
	} else {
		/* Channel B */
		if (request_irq(IRQ_SCCB, mac_SCC_interrupt, 0, "SCC B", info))
			panic("mac_SCC: can't get irq %d", IRQ_SCCB);
#ifdef SCC_DEBUG
		printk("mac_SCC: got SCC B interrupt %d, channel %d info %p\n", 
			IRQ_SCCB, channel, info);
#endif
	}

	/* If console serial line, then enable interrupts. */
	if (info->private->is_cons) {
		printk("mac_SCC: console line %d; enabling interrupt!\n", info->line);
		write_zsreg(info->private->zs_channel, R1,
			    (EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB));
		write_zsreg(info->private->zs_channel, R9, (NV | MIE));
		write_zsreg(info->private->zs_channel, R10, (NRZ));
		write_zsreg(info->private->zs_channel, R3, (Rx8 | RxENABLE));
		write_zsreg(info->private->zs_channel, R5, (Tx8 | TxENAB));
	}
	/* If this is the kgdb line, enable interrupts because we
	 * now want to receive the 'control-c' character from the
	 * client attached to us asynchronously.
	 */
	if (info->private->kgdb_channel) {
		printk("mac_SCC: kgdb line %d; enabling interrupt!\n", info->line);
		kgdb_chaninit(info, 1, info->private->zs_baud);
	}
	/* Report settings (in m68kserial.c) */
#ifndef CONFIG_MAC
	printk("ttyS%d at 0x%08x (irq = %d)", info->line, 
		       info->port, info->irq);
	printk(" is a Z8530 SCC\n");
#endif

}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void SCC_deinit(struct m68k_async_struct * info, int leave_dtr)
{
	unsigned long	flags;

	save_flags(flags); cli(); /* Disable interrupts */
	
	info->private->pendregs[1] = info->private->curregs[1] = 0;
	write_zsreg(info->private->zs_channel, 1, 0);	/* no interrupts */

	info->private->curregs[3] &= ~RxENABLE;
	info->private->pendregs[3] = info->private->curregs[3];
	write_zsreg(info->private->zs_channel, 3, info->private->curregs[3]);

	info->private->curregs[5] &= ~TxENAB;

	if (!leave_dtr)
		info->private->curregs[5] &= ~(DTR | RTS);
	else
		info->private->curregs[5] &= ~(RTS);

	info->private->pendregs[5] = info->private->curregs[5];
	write_zsreg(info->private->zs_channel, 5, info->private->curregs[5]);

	restore_flags(flags);
}

/* FIXME !!! */
static int SCC_check_custom_divisor( struct m68k_async_struct *info,
				    int baud_base, int divisor )
{
	return 0;
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void SCC_change_speed(struct m68k_async_struct *info)
{
	unsigned short port;
	unsigned cflag;
	int	i;
	int	brg;
	unsigned long flags;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (!(port = info->port))
		return;
	i = cflag & CBAUD;

	if (i == 0 && !(info->flags & ASYNC_SPD_MASK)) {
		/* speed == 0 -> drop DTR */
		save_flags(flags);
		cli();
		info->private->curregs[5] &= ~(DTR | RTS);
		write_zsreg(info->private->zs_channel, 5, info->private->curregs[5]);
		restore_flags(flags);
		return;
	}


	if (i & CBAUDEX) {
		/* XXX CBAUDEX is not obeyed.
		 * It is impossible at a 32bits PPC.  XXX??
		 * But we have to report this to user ... someday.
		 */
		i = B9600;
	}

	save_flags(flags); cli();
	info->private->zs_baud = baud_table[i];
	info->private->clk_divisor = 16;

	info->private->curregs[4] = X16CLK;
	info->private->curregs[11] = TCBR | RCBR;
	brg = BPS_TO_BRG(info->private->zs_baud, 
			 ZS_CLOCK/info->private->clk_divisor);
	info->private->curregs[12] = (brg & 255);
	info->private->curregs[13] = ((brg >> 8) & 255);
	info->private->curregs[14] = BRENABL;

	/* byte size and parity */
	info->private->curregs[3] &= ~RxNBITS_MASK;
	info->private->curregs[5] &= ~TxNBITS_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		info->private->curregs[3] |= Rx5;
		info->private->curregs[5] |= Tx5;
		break;
	case CS6:
		info->private->curregs[3] |= Rx6;
		info->private->curregs[5] |= Tx6;
		break;
	case CS7:
		info->private->curregs[3] |= Rx7;
		info->private->curregs[5] |= Tx7;
		break;
	case CS8:
	default: /* defaults to 8 bits */
		info->private->curregs[3] |= Rx8;
		info->private->curregs[5] |= Tx8;
		break;
	}
	info->private->pendregs[3] = info->private->curregs[3];
	info->private->pendregs[5] = info->private->curregs[5];

	info->private->curregs[4] &= ~(SB_MASK | PAR_ENA | PAR_EVEN);
	if (cflag & CSTOPB) {
		info->private->curregs[4] |= SB2;
	} else {
		info->private->curregs[4] |= SB1;
	}
	if (cflag & PARENB) {
		info->private->curregs[4] |= PAR_ENA;
	}
	if (!(cflag & PARODD)) {
		info->private->curregs[4] |= PAR_EVEN;
	}
	info->private->pendregs[4] = info->private->curregs[4];

	info->private->curregs[15] &= ~(DCDIE | CTSIE);
	if (!(cflag & CLOCAL)) {
		info->private->curregs[15] |= DCDIE;
	}
	if (cflag & CRTSCTS) {
		info->private->curregs[15] |= CTSIE;
		if ((read_zsreg(info->private->zs_channel, 0) & CTS) != 0)
			info->private->tx_stopped = 1;
	} else
		info->private->tx_stopped = 0;
	info->private->pendregs[15] = info->private->curregs[15];

	/* Load up the new values */
	load_zsregs(info->private->zs_channel, info->private->curregs);

	restore_flags(flags);
}

/* This is for console output over ttya/ttyb */
static void SCC_put_char(char ch)
{
	struct mac_zschannel *chan = zs_conschan;
	int loops = 0;
	unsigned long flags;

	if(!chan)
		return;

	save_flags(flags); cli();
	while ((read_zsreg(chan, 0) & Tx_BUF_EMP) == 0 && loops < 10000) {
		loops++;
		udelay(5);
	}
	write_zsdata(chan, ch);
	restore_flags(flags);
}

/* These are for receiving and sending characters under the kgdb
 * source level kernel debugger.
 */
void putDebugChar(char kgdb_char)
{
	struct mac_zschannel *chan = zs_kgdbchan;

	while ((read_zsreg(chan, 0) & Tx_BUF_EMP) == 0)
		udelay(5);
	write_zsdata(chan, kgdb_char);
}

char getDebugChar(void)
{
	struct mac_zschannel *chan = zs_kgdbchan;

	while ((read_zsreg(chan, 0) & Rx_CH_AV) == 0)
		udelay(5);
	return read_zsdata(chan);
}

/*
 * Fair output driver allows a process to speak.
 */
static void SCC_fair_output(void)
{
	int left;		/* Output no more than that */
	unsigned long flags;
	struct m68k_async_struct *info = zs_consinfo;
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

		SCC_put_char(c);

		save_flags(flags);  cli();
		left = MIN(info->xmit_cnt, left-1);
	}

	restore_flags(flags);
	return;
}

/*
 * zs_console_print is registered for printk.
 */
static void zs_console_print(const char *p)
{
	char c;

	while ((c = *(p++)) != 0) {
		if (c == '\n')
			SCC_put_char('\r');
		SCC_put_char(c);
	}

	/* Comment this if you want to have a strict interrupt-driven output */
	SCC_fair_output();
}

/* FIXME: check with SCC_enab_tx_int!! */
#if 0
static void rs_flush_chars(struct tty_struct *tty)
{
	struct m68k_async_struct *info = (struct m68k_async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_flush_chars"))
		return;

	if (info->xmit_cnt <= 0 || tty->stopped || info->private->tx_stopped ||
	    !info->xmit_buf)
		return;

	/* Enable transmitter */
	save_flags(flags); cli();
	transmit_chars(info);
	restore_flags(flags);
}

static int rs_write(struct tty_struct * tty, int from_user,
		    const unsigned char *buf, int count)
{
	int	c, total = 0;
	struct m68k_async_struct *info = (struct m68k_async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_write"))
		return 0;

	if (!tty || !info->xmit_buf)
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
	if (info->xmit_cnt && !tty->stopped && !info->tx_stopped
	    && !info->tx_active)
		transmit_chars(info);
	restore_flags(flags);
	return total;
}
#endif

/*
 * ------------------------------------------------------------
 * rs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void SCC_throttle(struct m68k_async_struct *info, int status)
{
	unsigned long	flags;

	save_flags(flags); 
	cli();

	if (status) {
		/*
		 * Here we want to turn off the RTS line.  On Macintoshes,
		 * we only get the DTR line, which goes to both DTR and
		 * RTS on the modem.  RTS doesn't go out to the serial
		 * port socket.  So you should make sure your modem is
		 * set to ignore DTR if you're using CRTSCTS.
		 */
		info->private->curregs[5] &= ~(DTR | RTS);
		info->private->pendregs[5] &= ~(DTR | RTS);
		write_zsreg(info->private->zs_channel, 5, 
			    info->private->curregs[5]);
	} else {
		/* Assert RTS and DTR lines */
		info->private->curregs[5] |= DTR | RTS;
		info->private->pendregs[5] |= DTR | RTS;
		write_zsreg(info->private->zs_channel, 5, 
			    info->private->curregs[5]);
	}

	restore_flags(flags);

}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static void SCC_get_serial_info(struct m68k_async_struct * info,
			   struct serial_struct * retinfo)
{
	retinfo->baud_base = info->baud_base;
	retinfo->custom_divisor = info->custom_divisor;
}

/* FIXME: set_serial_info needs check_custom_divisor !!! */

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
static int SCC_get_lsr_info(struct m68k_async_struct * info, unsigned int *value)
{
	unsigned char status;

	cli();
	status = read_zsreg(info->private->zs_channel, 0);
	sti();
	return status;
}

static unsigned int SCC_get_modem_info(struct m68k_async_struct *info)
{
	unsigned char control, status;
	unsigned int result;

	cli();
	control = info->private->curregs[5];
	status = read_zsreg(info->private->zs_channel, 0);
	sti();
	result =  ((control & RTS) ? TIOCM_RTS: 0)
		| ((control & DTR) ? TIOCM_DTR: 0)
		| ((status  & DCD) ? TIOCM_CAR: 0)
		| ((status  & CTS) ? 0: TIOCM_CTS);
	return result;
}

/* FIXME: zs_setdtr was used in rs_open ... */

static int SCC_set_modem_info(struct m68k_async_struct *info, 
			      int new_dtr, int new_rts)
{
	unsigned int bits;

	bits = (new_rts ? RTS: 0) + (new_dtr ? DTR: 0);
	info->private->curregs[5] = (info->private->curregs[5] & ~(DTR | RTS)) | bits;
	info->private->pendregs[5] = info->private->curregs[5];
	write_zsreg(info->private->zs_channel, 5, info->private->curregs[5]);
	sti();
	return 0;
}

/*
 * This routine sends a break character out the serial port.
 */
static void SCC_set_break(struct m68k_async_struct * info, int break_flag)
{
	unsigned long	flags;

	save_flags(flags);
	cli();

	if (break_flag) {
		info->private->curregs[5] |= SND_BRK;
		write_zsreg(info->private->zs_channel, 5, 
			    info->private->curregs[5]);
	} else {
		info->private->curregs[5] &= ~SND_BRK;
		write_zsreg(info->private->zs_channel, 5, 
			    info->private->curregs[5]);
	}

	restore_flags(flags);
}

/* FIXME: these have to be enabled in rs_ioctl !! */

static int SCC_ioctl(struct tty_struct *tty, struct file * file,
		    struct m68k_async_struct * info, unsigned int cmd, 
	            unsigned long arg)
{
	int error;

	switch (cmd) {
		case TIOCSERGETLSR: /* Get line status register */
			error = verify_area(VERIFY_WRITE, (void *) arg,
				sizeof(unsigned int));
			if (error)
				return error;
			else
			    return SCC_get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERGSTRUCT:
			error = verify_area(VERIFY_WRITE, (void *) arg,
						sizeof(struct m68k_async_struct));
			if (error)
				return error;
			copy_to_user((struct m68k_async_struct *) arg,
				      info, sizeof(struct m68k_async_struct));
			return 0;
			
		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void SCC_stop_receive (struct m68k_async_struct *info)
{
	/* disable Rx */
	info->private->curregs[3] &= ~RxENABLE;
	info->private->pendregs[3] = info->private->curregs[3];
	write_zsreg(info->private->zs_channel, 3, info->private->curregs[3]);
	/* disable Rx interrupts */
	info->private->curregs[1] &= ~(0x18);	/* disable any rx ints */
	info->private->pendregs[1] = info->private->curregs[1];
	write_zsreg(info->private->zs_channel, 1, info->private->curregs[1]);
	ZS_CLEARFIFO(info->private->zs_channel);
}

static int SCC_trans_empty (struct m68k_async_struct *info)
{
	return  (read_zsreg(info->private->zs_channel, 1) & ALL_SNT) != 0;
}

/* Finally, routines used to initialize the serial driver. */

#ifdef CONFIG_MAC

/*
 *	Mac: use boot_info data; assume 2 channels
 */
 
static void probe_sccs(void)
{
	int n;

#define ZS_CONTROL	0x50F04000
#define ZS_DATA		(ZS_CONTROL+4)
#define ZS_IRQ		5
#define ZS_MOVE		-2
#define ZS_DATA_MOVE	4
#define ZS_CH_A_FIRST	2

	/* last-ditch fixup for NetBSD booter case */
	if (mac_bi_data.sccbase == 0)
		mac_bi_data.sccbase = ZS_CONTROL;

	/* testing: fix up broken 24 bit addresses (ClassicII) */
	if ((mac_bi_data.sccbase & 0x00FFFFFF) == mac_bi_data.sccbase)
		mac_bi_data.sccbase |= 0x50000000;
		
	if ( !hwreg_present((void *)mac_bi_data.sccbase))
	{
		printk(KERN_WARNING "z8530: Serial devices not accessible. Check serial switch.\n");
		return;
	}

	for(n=0;n<2;n++)
	{
#if 0
		zs_channels[n].control = (volatile unsigned char *)
				ZS_CONTROL+ZS_MOVE*n;
		zs_channels[n].data = (volatile unsigned char *)ZS_DATA+ZS_MOVE*n;
#else
		zs_channels[n].control = (volatile unsigned char *) /* 2, 0 */
			(mac_bi_data.sccbase+ZS_CH_A_FIRST)+ZS_MOVE*n;
		zs_channels[n].data = (volatile unsigned char *) /* 6, 4 */
			(mac_bi_data.sccbase+ZS_CH_A_FIRST+ZS_DATA_MOVE)+ZS_MOVE*n;
#endif
		zs_soft[n].private = &zs_soft_private[n];
		zs_soft[n].private->zs_channel = &zs_channels[n];
		zs_soft[n].irq = IRQ4;
#if 0		
		if (request_irq(ch->intrs[0], rs_interrupt, 0,
				"SCC", &zs_soft[n]))
			panic("macserial: can't get irq %d",
			      ch->intrs[0]);
#endif
		if (n & 1)
			zs_soft[n].private->zs_chan_a = &zs_channels[n-1];
		else
			zs_soft[n].private->zs_chan_a = &zs_channels[n];
	}

	zs_channels_found=2;
}

#else

/*
 *	PowerMAC - query the PROM
 */
 
static void show_serial_version(void)
{
	printk("PowerMac Z8530 serial driver version 1.00\n");
}

/* Ask the PROM how many Z8530s we have and initialize their zs_channels */
static void
probe_sccs()
{
	struct device_node *dev, *ch;
	struct m68k_async_struct **pp;
	int n;

	n = 0;
	pp = &zs_chain;
	for (dev = find_devices("escc"); dev != 0; dev = dev->next) {
		if (n >= NUM_CHANNELS) {
			printk("Sorry, can't use %s: no more channels\n",
			       dev->full_name);
			continue;
		}
		for (ch = dev->child; ch != 0; ch = ch->sibling) {
			if (ch->n_addrs < 1 || ch ->n_intrs < 1) {
				printk("Can't use %s: %d addrs %d intrs\n",
				      ch->full_name, ch->n_addrs, ch->n_intrs);
				continue;
			}
			zs_channels[n].control = (volatile unsigned char *)
				ch->addrs[0].address;
			zs_channels[n].data = zs_channels[n].control
				+ ch->addrs[0].size / 2;
			zs_soft[n].private = &zs_soft_private[n];
			zs_soft[n].private->zs_channel = &zs_channels[n];
			zs_soft[n].irq = ch->intrs[0];
			if (request_irq(ch->intrs[0], mac_SCC_interrupt, 0,
					"SCC", &zs_soft[n]))
				panic("macserial: can't get irq %d",
				      ch->intrs[0]);
			/* XXX this assumes the prom puts chan A before B */
			if (n & 1)
				zs_soft[n].private->zs_chan_a = &zs_channels[n-1];
			else
				zs_soft[n].private->zs_chan_a = &zs_channels[n];

			*pp = &zs_soft[n];
			pp = &zs_soft[n].private->zs_next;
			++n;
		}
	}
	*pp = 0;
	zs_channels_found = n;
}

#endif

extern void register_console(void (*proc)(const char *));

static inline void
rs_cons_check(struct m68k_async_struct *ss, int channel)
{
	int i, o, io;
	static int consout_registered = 0;
	static int msg_printed = 0;

	i = o = io = 0;

	/* Is this one of the serial console lines? */
	if ((zs_cons_chanout != channel) &&
	    (zs_cons_chanin != channel))
		return;
	zs_conschan = ss->private->zs_channel;
	zs_consinfo = ss;

	/* Register the console output putchar, if necessary */
	if (zs_cons_chanout == channel) {
		o = 1;
		/* double whee.. */
		if (!consout_registered) {
			register_console(zs_console_print);
			consout_registered = 1;
		}
	}

	if (zs_cons_chanin == channel) {
		i = 1;
	}
	if (o && i)
		io = 1;
	if (ss->private->zs_baud != 9600)
		panic("Console baud rate weirdness");

	/* Set flag variable for this port so that it cannot be
	 * opened for other uses by accident.
	 */
	ss->private->is_cons = 1;

	if (io) {
		if(!msg_printed) {
			printk("zs%d: console I/O\n", ((channel>>1)&1));
			msg_printed = 1;
		}
	} else {
		printk("zs%d: console %s\n", ((channel>>1)&1),
		       (i==1 ? "input" : (o==1 ? "output" : "WEIRD")));
	}

	/* FIXME : register interrupt here??? */
}

volatile int test_done;

/* rs_init inits the driver */
int mac_SCC_init(void)
{
	int channel, line, nr = 0;
	unsigned long flags;
	struct serial_struct req;

	printk("Mac68K Z8530 serial driver version 1.01\n");

	/* SCC present at all? */
	if (!MACH_IS_MAC)
		return( -ENODEV );

	if (zs_chain == 0)
		probe_sccs();

	save_flags(flags);
	cli();

	/* 
	 * FIXME: init of rs_table entry and register_serial now done,
	 * but possible clash of zs_soft[channel] and rs_table[channel]!!
	 * zs_soft initialized in probe_sccs(), some settings copied to
	 * info = &rs_table[channel], which is used by the mid-level code.
	 * The info->private part is shared among both!
	 */

	for (channel = 0; channel < zs_channels_found; ++channel) {
		req.line = channel;
		req.type = SER_SCC_MAC;
		req.port = (int) zs_soft[channel].private->zs_channel->control;

		if ((line = register_serial( &req )) >= 0) {
			SCC_init_port( &rs_table[line], req.type, line );
			++nr;
		}
		else
			printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel A\n", req.line );
	}

	restore_flags(flags);

	return( nr > 0 ? 0 : -ENODEV );
}

/* Hooks for running a serial console.  con_init() calls this if the
 * console is being run over one of the serial ports.
 * 'channel' is decoded as 0=modem 1=printer, 'chip' is ignored.
 */
void
rs_cons_hook(int chip, int out, int channel)
{
	if (zs_chain == 0)
		probe_sccs();
	zs_soft[channel].private->clk_divisor = 16;
	zs_soft[channel].private->zs_baud = get_zsbaud(&zs_soft[channel]);
	rs_cons_check(&zs_soft[channel], channel);
	if (out)
		zs_cons_chanout = channel;
	else
		zs_cons_chanin = channel;

	/* FIXME : register interrupt here??? */
}

/* This is called at boot time to prime the kgdb serial debugging
 * serial line.  The 'tty_num' argument is 0 for /dev/ttyS0 and 1
 * for /dev/ttyS1 which is determined in setup_arch() from the
 * boot command line flags.
 */
void
rs_kgdb_hook(int tty_num)
{
	if (zs_chain == 0)
		probe_sccs();
	zs_kgdbchan = zs_soft[tty_num].private->zs_channel;
	zs_soft[tty_num].private->clk_divisor = 16;
	zs_soft[tty_num].private->zs_baud = get_zsbaud(&zs_soft[tty_num]);
	zs_soft[tty_num].private->kgdb_channel = 1;     /* This runs kgdb */
	zs_soft[tty_num ^ 1].private->kgdb_channel = 0; /* This does not */
	/* Turn on transmitter/receiver at 8-bits/char */
	kgdb_chaninit(&zs_soft[tty_num], 0, 9600);
	ZS_CLEARERR(zs_kgdbchan);
	ZS_CLEARFIFO(zs_kgdbchan);

	/* FIXME : register interrupt here??? */
}


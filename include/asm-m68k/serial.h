/*
 * include/linux/serial.h
 *
 * Copyright (C) 1992 by Theodore Ts'o.
 * 
 * Redistribution of this file is permitted under the terms of the GNU 
 * Public License (GPL)
 */

#ifndef _M68K_SERIAL_H
#define _M68K_SERIAL_H


/* m68k serial port types are numbered from 100 to avoid interference
 * with the PC types (1..4)
 */
#define PORT_UNKNOWN	0
#define PORT_8250	1
#define PORT_16450	2
#define PORT_16550	3
#define PORT_16550A	4
#define PORT_CIRRUS     5
#define PORT_16650V2	7
#define PORT_16750	8

#define SER_SCC_NORM	100	/* standard SCC channel */
#define	SER_SCC_DMA	101	/* SCC channel with DMA support */
#define	SER_MFP_CTRL	102	/* standard MFP port with modem control signals */
#define	SER_MFP_BARE	103	/* MFP port without modem controls */
#define	SER_MIDI	104	/* Atari MIDI */
#define	SER_AMIGA	105	/* Amiga built-in serial port */
#define SER_IOEXT	106	/* Amiga GVP IO-Extender (16c552) */
#define SER_MFC_III	107	/* Amiga BSC Multiface Card III (MC68681) */
#define SER_WHIPPET	108	/* Amiga Hisoft Whippet PCMCIA (16c550B) */
#define SER_SCC_MVME	109	/* MVME162/MVME172 ports */
#define SER_SCC_MAC	110	/* Macintosh SCC channel */
#define SER_HPDCA	111	/* HP DCA serial */
#define SER_SCC_BVME	112	/* BVME6000 ports */

struct serial_struct {
	int	type;
	int	line;
	int	port;
	int	irq;
	int	flags;
	int	xmit_fifo_size;
	int	custom_divisor;
	int	baud_base;
	unsigned short	close_delay;
	char	reserved_char[2];
	int	hub6;
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned short	closing_wait2; /* no longer used... */
	int	reserved[4];
};

/*
 * For the close wait times, 0 means wait forever for serial port to
 * flush its output.  65535 means don't wait at all.
 */
#define ASYNC_CLOSING_WAIT_INF	0
#define ASYNC_CLOSING_WAIT_NONE	65535

/* This function tables does the abstraction from the underlying
 * hardware:
 *
 *   init(): Initialize the port as necessary, set RTS and DTR and
 *      enable interrupts. It does not need to set the speed and other
 *      parameters, because change_speed() is called, too.
 *   deinit(): Stop and shutdown the port (e.g. disable interrupts, ...)
 *   enab_tx_int(): Enable or disable the Tx Buffer Empty interrupt
 *      independently from other interrupt sources. If the int is
 *      enabled, the transmitter should also be restarted, i.e. if there
 *      are any chars to be sent, they should be put into the Tx
 *      register. The real en/disabling of the interrupt may be a no-op
 *      if there is no way to do this or it is too complex. This Tx ints
 *      are just disabled to save some interrupts if the transmitter is
 *      stopped anyway. But the restarting must be implemented!
 *   check_custom_divisor(): Check the given custom divisor for legality
 *      and return 0 if OK, non-zero otherwise.
 *   change_speed(): Set port speed, character size, number of stop
 *      bits and parity from the termios structure. If the user wants
 *      to set the speed with a custom divisor, he is required to
 *      check the baud_base first!
 *   throttle(): Set or clear the RTS line according to 'status'.
 *   set_break(): Set or clear the 'Send a Break' flag.
 *   get_serial_info(): Fill in the baud_base and custom_divisor
 *      fields of a serial_struct. It may also modify other fields, if
 *      needed.
 *   get_modem_info(): Return the status of RTS, DTR, DCD, RI, DSR and CTS.
 *   set_modem_info(): Set the status of RTS and DTR according to
 *      'new_dtr' and 'new_rts', resp. 0 = clear, 1 = set, -1 = don't change
 *   ioctl(): Process any port-specific ioctl's. This pointer may be
 *      NULL, if the port has no own ioctl's.
 *   stop_receive(): Turn off the Rx part of the port, so no more characters
 *      will be received. This is called before shutting the port down.
 *   trans_empty(): Return !=0 if there are no more characters still to be
 *      sent out (Tx buffer register and FIFOs empty)
 *   check_open(): Is called before the port is opened. The driver can check
 *      if that's ok and return an error code, or keep track of the opening
 *      even before init() is called. Use deinit() for matching closing of the
 *      port.
 *
 */

struct m68k_async_struct;

typedef struct {
	void (*init)( struct m68k_async_struct *info );
	void (*deinit)( struct m68k_async_struct *info, int leave_dtr );
	void (*enab_tx_int)( struct m68k_async_struct *info, int enab_flag );
	int  (*check_custom_divisor)( struct m68k_async_struct *info, int baud_base,
				     int divisor );
	void (*change_speed)( struct m68k_async_struct *info );
	void (*throttle)( struct m68k_async_struct *info, int status );
	void (*set_break)( struct m68k_async_struct *info, int break_flag );
	void (*get_serial_info)( struct m68k_async_struct *info,
				struct serial_struct *retinfo );
	unsigned int (*get_modem_info)( struct m68k_async_struct *info );
	int  (*set_modem_info)( struct m68k_async_struct *info, int new_dtr,
			       int new_rts );
	int  (*ioctl)( struct tty_struct *tty, struct file *file,
		      struct m68k_async_struct *info, unsigned int cmd,
		      unsigned long arg );
	void (*stop_receive)( struct m68k_async_struct *info );
	int  (*trans_empty)( struct m68k_async_struct *info );
	int  (*check_open)( struct m68k_async_struct *info, struct tty_struct *tty,
			   struct file *file );
} SERIALSWITCH;

/*
 * Definitions for m68k_async_struct (and serial_struct) flags field
 */
#define ASYNC_HUP_NOTIFY 0x0001 /* Notify getty on hangups and closes 
				   on the callout port */
#define ASYNC_FOURPORT  0x0002	/* Set OU1, OUT2 per AST Fourport settings */
#define ASYNC_SAK	0x0004	/* Secure Attention Key (Orange book) */
#define ASYNC_SPLIT_TERMIOS 0x0008 /* Separate termios for dialin/callout */

#define ASYNC_SPD_MASK	0x1030
#define ASYNC_SPD_HI	0x0010	/* Use 56000 instead of 38400 bps */

#define ASYNC_SPD_VHI	0x0020  /* Use 115200 instead of 38400 bps */
#define ASYNC_SPD_CUST	0x0030  /* Use user-specified divisor */

#define ASYNC_SKIP_TEST	0x0040 /* Skip UART test during autoconfiguration */
#define ASYNC_AUTO_IRQ  0x0080 /* Do automatic IRQ during autoconfiguration */
#define ASYNC_SESSION_LOCKOUT 0x0100 /* Lock out cua opens based on session */
#define ASYNC_PGRP_LOCKOUT    0x0200 /* Lock out cua opens based on pgrp */
#define ASYNC_CALLOUT_NOHUP   0x0400 /* Don't do hangups for cua device */

#define ASYNC_HARDPPS_CD	0x0800	/* Call hardpps when CD goes high  */

#define ASYNC_SPD_SHI	0x1000	/* Use 230400 instead of 38400 bps */
#define ASYNC_SPD_WARP	0x1010	/* Use 460800 instead of 38400 bps */

#define ASYNC_FLAGS	0x1FFF	/* Possible legal async flags */
#define ASYNC_USR_MASK 0x1430	/* Legal flags that non-privileged
				 * users can set or reset */

/* Internal flags used only by drivers/char/m68kserial.c */
#define ASYNC_INITIALIZED	0x80000000 /* Serial port was initialized */
#define ASYNC_CALLOUT_ACTIVE	0x40000000 /* Call out device is active */
#define ASYNC_NORMAL_ACTIVE	0x20000000 /* Normal device is active */
#define ASYNC_BOOT_AUTOCONF	0x10000000 /* Autoconfigure port on bootup */
#define ASYNC_CLOSING		0x08000000 /* Serial port is closing */
#define ASYNC_CTS_FLOW		0x04000000 /* Do CTS flow control */
#define ASYNC_CHECK_CD		0x02000000 /* i.e., CLOCAL */

#define ASYNC_INTERNAL_FLAGS	0xFF000000 /* Internal flags */

/*
 * Serial input interrupt line counters -- external structure
 * Four lines can interrupt: CTS, DSR, RI, DCD
 */
struct serial_icounter_struct {
	int cts, dsr, rng, dcd;
	int rx, tx;
	int frame, overrun, parity, brk;
	int buf_overrun;
	int reserved[9];
};


#ifdef __KERNEL__
/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

#include <linux/termios.h>
#include <linux/tqueue.h>

#include <linux/config.h>	/* for Mac SCC extensions */

#ifdef CONFIG_MAC
#define NUM_ZSREGS    16
struct mac_zschannel {
	volatile unsigned char *control;
	volatile unsigned char *data;
};
struct m68k_async_private;
#endif

struct m68k_async_struct {
	int			magic;
	int			baud_base;
	int			port;
	int			irq;
	int			flags; 		/* defined in tty.h */
	int			hub6;		/* HUB6 plus one */
	int			type;
	struct tty_struct 	*tty;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			xmit_fifo_size;
	int			custom_divisor;
	int			x_char;	/* xon/xoff character */
	int			close_delay;
	unsigned short		closing_wait;
	unsigned short		closing_wait2;
	int			IER; 	/* Interrupt Enable Register */
	int			MCR; 	/* Modem control register */
	int			MCR_noint; /* MCR with interrupts off */
	unsigned long		event;
	unsigned long		last_active;
	int			line;
	int			count;	    /* # of fd on device */
	int			blocked_open; /* # of blocked opens */
	long			session; /* Session of opening process */
	long			pgrp; /* pgrp of opening process */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	struct tq_struct	tqueue;
	struct termios		normal_termios;
	struct termios		callout_termios;
	struct wait_queue	*open_wait;
	struct wait_queue	*close_wait;
	struct wait_queue	*delta_msr_wait;
	struct async_icount	icount;	/* kernel counters for the 4 input interrupts */
	struct m68k_async_struct	*next_port; /* For the linked list */
	struct m68k_async_struct	*prev_port;
	void			*board_base; /* board-base address for use with
						boards carrying several UART's,
						like some Amiga boards. */
	unsigned short		nr_uarts;    /* UART-counter, that indicates
						how many UART's there are on
						the board.  If the board has a
						IRQ-register, this can be used
						to check if any of the uarts,
						on the board has requested an
						interrupt, instead of checking
						IRQ-registers on all UART's */
	SERIALSWITCH		*sw;		/* functions to manage this port */
#ifdef CONFIG_MAC
	struct m68k_async_private	*private;
#endif
};

#ifdef CONFIG_MAC
struct m68k_async_private {
	struct m68k_async_info	*zs_next;	/* For IRQ servicing chain */
	struct mac_zschannel	*zs_channel;	/* Channel registers */
	struct mac_zschannel	*zs_chan_a;	/* A side registers */
	unsigned char		read_reg_zero;

	char			soft_carrier;	/* Use soft carrier on this */
	char			break_abort;	/* console, process brk/abrt */
	char			kgdb_channel;	/* Kgdb running on this channel */
	char			is_cons;	/* Is this our console. */
	unsigned char		tx_active;	/* character being xmitted */
	unsigned char		tx_stopped;	/* output is suspended */

	/* We need to know the current clock divisor
	 * to read the bps rate the chip has currently
	 * loaded.
	 */
	unsigned char		clk_divisor;	/* May be 1, 16, 32, or 64 */
	int			zs_baud;

	/* Current write register values */
	unsigned char		curregs[NUM_ZSREGS];

	/* Values we need to set next opportunity */
	unsigned char		pendregs[NUM_ZSREGS];

	char			change_needed;
};
#endif
#define SERIAL_MAGIC 0x5301

/*
 * The size of the serial xmit buffer is 1 page, or 4096 bytes
 */
#define SERIAL_XMIT_SIZE 4096

/*
 * Events are used to schedule things to happen at timer-interrupt
 * time, instead of at rs interrupt time.
 */
#define RS_EVENT_WRITE_WAKEUP	0

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/* Export to allow PCMCIA to use this - Dave Hinds */
extern int register_serial(struct serial_struct *req);
extern void unregister_serial(int line);
extern struct m68k_async_struct rs_table[];
extern task_queue tq_serial;


/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static __inline__ void rs_sched_event(struct m68k_async_struct *info, int event)
{
	info->event |= 1 << event;
	queue_task(&info->tqueue, &tq_serial);
	mark_bh(SERIAL_BH);
}

static __inline__ void rs_receive_char( struct m68k_async_struct *info,
					    int ch, int err )
{
	struct tty_struct *tty = info->tty;
	
	if (tty->flip.count >= TTY_FLIPBUF_SIZE)
		return;
	tty->flip.count++;
	switch(err) {
	case TTY_BREAK:
		info->icount.brk++;
		if (info->flags & ASYNC_SAK)
			do_SAK(tty);
		break;
	case TTY_PARITY:
		info->icount.parity++;
		break;
	case TTY_OVERRUN:
		info->icount.overrun++;
		break;
	case TTY_FRAME:
		info->icount.frame++;
		break;
	}
	*tty->flip.flag_buf_ptr++ = err;
	*tty->flip.char_buf_ptr++ = ch;
	info->icount.rx++;
	tty_flip_buffer_push(tty);
}

static __inline__ int rs_get_tx_char( struct m68k_async_struct *info )
{
	unsigned char ch;
	
	if (info->x_char) {
		ch = info->x_char;
		info->icount.tx++;
		info->x_char = 0;
		return( ch );
	}

	if (info->xmit_cnt <= 0 || info->tty->stopped || info->tty->hw_stopped)
		return( -1 );

	ch = info->xmit_buf[info->xmit_tail++];
	info->xmit_tail &= SERIAL_XMIT_SIZE - 1;
	info->icount.tx++;
	if (--info->xmit_cnt < WAKEUP_CHARS)
		rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
	return( ch );
}

static __inline__ int rs_no_more_tx( struct m68k_async_struct *info )
{
	return( info->xmit_cnt <= 0 ||
			info->tty->stopped ||
			info->tty->hw_stopped );
}

static __inline__ void rs_dcd_changed( struct m68k_async_struct *info, int dcd )

{
	/* update input line counter */
	info->icount.dcd++;
	wake_up_interruptible(&info->delta_msr_wait);

	if (info->flags & ASYNC_CHECK_CD) {
#if (defined(SERIAL_DEBUG_OPEN) || defined(SERIAL_DEBUG_INTR))
		printk("ttyS%d CD now %s...", info->line,
		       dcd ? "on" : "off");
#endif		
		if (dcd) {
			wake_up_interruptible(&info->open_wait);
		} else if (!((info->flags & ASYNC_CALLOUT_ACTIVE) &&
			     (info->flags & ASYNC_CALLOUT_NOHUP))) {
#ifdef SERIAL_DEBUG_OPEN
			printk("scheduling hangup...");
#endif
			if (info->tty)
				tty_hangup(info->tty);
		}
	}
}


void rs_stop( struct tty_struct *tty );
void rs_start( struct tty_struct *tty );

static __inline__ void rs_check_cts( struct m68k_async_struct *info, int cts )
{
	/* update input line counter */
	info->icount.cts++;
	wake_up_interruptible(&info->delta_msr_wait);
	
	if ((info->flags & ASYNC_CTS_FLOW) && info->tty) {
		if (info->tty->hw_stopped) {
			if (cts) {
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
				printk("CTS tx start...");
#endif
				info->tty->hw_stopped = 0;
				rs_start( info->tty );
				rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
				return;
			}
		} else {
			if (!cts) {
				info->tty->hw_stopped = 1;
				rs_stop( info->tty );
			}
		}
	}
}


#endif /* __KERNEL__ */

#endif /* _M68K_SERIAL_H */

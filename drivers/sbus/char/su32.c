/* $Id: su32.c,v 1.1 1998/09/18 10:45:32 jj Exp $
 * su.c: Small serial driver for keyboard/mouse interface on sparc32/PCI
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * Coypright (C) 1998  Pete Zaitcev   (zaitcev@metabyte.com)
 *
 * This is mainly a variation of drivers/char/serial.c,
 * credits go to authors mentioned therein.
 */

/*
 * Configuration section.
 */
#define SERIAL_PARANOIA_CHECK
#define CONFIG_SERIAL_NOPAUSE_IO	/* Unused on sparc */
#define SERIAL_DO_RESTART

#if 1
/* Normally these defines are controlled by the autoconf.h */

#undef CONFIG_SERIAL_MANY_PORTS
#define CONFIG_SERIAL_SHARE_IRQ		/* Must be enabled for MrCoffee. */
#undef CONFIG_SERIAL_DETECT_IRQ		/* code is removed from su.c */
#undef CONFIG_SERIAL_MULTIPORT
#endif

/* Sanity checks */

#ifdef CONFIG_SERIAL_MULTIPORT
#ifndef CONFIG_SERIAL_SHARE_IRQ
#define CONFIG_SERIAL_SHARE_IRQ
#endif
#endif

/* Set of debugging defines */

#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW
#undef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
#undef SERIAL_DEBUG_THROTTLE

/* */

#define RS_STROBE_TIME (10*HZ)
#define RS_ISR_PASS_LIMIT 256

#ifdef __sparc_v9__
#define IRQ_4M(n)	(n)
#define IRQ_T(info) ((info->flags & ASYNC_SHARE_IRQ) ? SA_SHIRQ : SA_INTERRUPT)
#else
/* 0x20 is sun4m thing, Dave Redman heritage. See arch/sparc/kernel/irq.c. */
#define IRQ_4M(n)	((n)|0x20)
/* Interrupts must be shared on MrCoffee. */
#define IRQ_T(info)	SA_SHIRQ
#endif

#define SERIAL_INLINE
  
#if defined(MODULE) && defined(SERIAL_DEBUG_MCOUNT)
#define DBG_CNT(s) printk("(%s): [%x] refc=%d, serc=%d, ttyc=%d -> %s\n", \
 kdevname(tty->device), (info->flags), serial_refcount,info->count,tty->count,s)
#else
#define DBG_CNT(s)
#endif

/*
 * End of serial driver configuration section.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#ifdef CONFIG_SERIAL_CONSOLE
#include <linux/console.h>
#include <linux/major.h>
#endif
#include <linux/sysrq.h>

#include <asm/system.h>
#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/ebus.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#if 0 /* P3: Needed if we to support /sbin/setserial. */
#include <asm/serial.h>
#endif

#include "sunserial.h"
#include "sunkbd.h"
#include "sunmouse.h"

/* We are on a NS PC87303 clocked with 24.0 MHz, which results
 * in a UART clock of 1.8462 MHz.
 */
#define BAUD_BASE	(1846200 / 16)

#ifdef CONFIG_SERIAL_CONSOLE
extern int serial_console;
static struct console sercons;
int su_serial_console_init(void);
#endif

/*
 * serial.c saves memory when it allocates async_info upon first open.
 * We have parts of state structure together because we do call startup
 * for keyboard and mouse.
 */
struct su_struct {
	int		 magic;
	unsigned long	 port;
	int		 baud_base;
	int		type;			/* Hardware type: e.g. 16550 */
	int		 irq;
	int		 flags;
	int		 line;
	int		 cflag;

	/* XXX Unify. */
	int		 kbd_node;
	int		 ms_node;
	int		 port_node;

	char		 name[16];

	int		xmit_fifo_size;
	int		custom_divisor;
	unsigned short	close_delay;
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned short	closing_wait2; /* no longer used... */

	struct tty_struct 	*tty;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			quot;
	int			x_char;	/* xon/xoff character */
	int			IER; 	/* Interrupt Enable Register */
	int			MCR; 	/* Modem control register */
	unsigned long		event;
	int			blocked_open; /* # of blocked opens */
	long			session; /* Session of opening process */
	long			pgrp; /* pgrp of opening process */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	struct tq_struct	tqueue;
	struct wait_queue	*open_wait;
	struct wait_queue	*close_wait;
	struct wait_queue	*delta_msr_wait;

	struct su_struct	*next_port;
	struct su_struct	*prev_port;

	int			count;
	struct async_icount	icount;
	struct termios		normal_termios, callout_termios;
	unsigned long		last_active;	/* For async_struct, to be */
};

#if 0 /* P3: became unused after surgery. */
/*
 * This is used to figure out the divisor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800, 0 };
#endif

#ifdef SERIAL_INLINE
#define _INLINE_ inline
#endif

static char *serial_name = "Serial driver";
static char *serial_version = "4.25.s1";

static DECLARE_TASK_QUEUE(tq_serial);

static struct tty_driver serial_driver, callout_driver;
static int serial_refcount;

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/*
 * IRQ_timeout		- How long the timeout should be for each IRQ
 * 				should be after the IRQ has been active.
 */
static struct su_struct *IRQ_ports[NR_IRQS];
#ifdef CONFIG_SERIAL_MULTIPORT
static struct rs_multiport_struct rs_multiport[NR_IRQS];
#endif
static int IRQ_timeout[NR_IRQS];

static void autoconfig(struct su_struct *info);
static void change_speed(struct su_struct *info);
static void su_wait_until_sent(struct tty_struct *tty, int timeout);

/*
 * Here we define the default xmit fifo size used for each type of
 * UART
 */
static struct serial_uart_config uart_config[] = {
	{ "unknown", 1, 0 }, 
	{ "8250", 1, 0 }, 
	{ "16450", 1, 0 }, 
	{ "16550", 1, 0 }, 
	{ "16550A", 16, UART_CLEAR_FIFO | UART_USE_FIFO }, 
	{ "cirrus", 1, 0 }, 
	{ "ST16650", 1, UART_CLEAR_FIFO |UART_STARTECH }, 
	{ "ST16650V2", 32, UART_CLEAR_FIFO | UART_USE_FIFO |
		  UART_STARTECH }, 
	{ "TI16750", 64, UART_CLEAR_FIFO | UART_USE_FIFO},
	{ 0, 0}
};


#define NR_PORTS	4

static struct su_struct su_table[NR_PORTS];
static struct tty_struct *serial_table[NR_PORTS];
static struct termios *serial_termios[NR_PORTS];
static struct termios *serial_termios_locked[NR_PORTS];

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the copy_from_user blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static unsigned char *tmp_buf;
static struct semaphore tmp_buf_sem = MUTEX;

static inline int serial_paranoia_check(struct su_struct *info,
					kdev_t device, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct (%s) in %s\n";
	static const char *badinfo =
		"Warning: null su_struct for (%s) in %s\n";

	if (!info) {
		printk(badinfo, kdevname(device), routine);
		return 1;
	}
	if (info->magic != SERIAL_MAGIC) {
		printk(badmagic, kdevname(device), routine);
		return 1;
	}
#endif
	return 0;
}

#ifdef __sparc_v9__

static inline
unsigned int su_inb(struct su_struct *info, unsigned long offset)
{
	return inb(info->port + offset);
}

static inline void
su_outb(struct su_struct *info, unsigned long offset, int value)
{
	outb(value, info->port + offset);
}

#else

static inline
unsigned int su_inb(struct su_struct *info, unsigned long offset)
{
	return (unsigned int)(*(volatile unsigned char *)(info->port + offset));
}

static inline void
su_outb(struct su_struct *info, unsigned long offset, int value)
{
	/*
	 * MrCoffee has weird schematics: IRQ4 & P10(?) pins of SuperIO are
	 * connected with a gate then go to SlavIO. When IRQ4 goes tristated
	 * gate gives logical one. Since we use level triggered interrupts
	 * we have lockup and watchdog reset. We cannot mask IRQ because
	 * keyboard shares IRQ with us (Bob Smelik: I would not hire you).
	 * P3: Assure that OUT2 never goes down.
	 */
	if (offset == UART_MCR) value |= UART_MCR_OUT2;
	*(volatile unsigned char *)(info->port + offset) = value;
}

#endif

#define serial_in(info, off)	su_inb(info, off)
#define serial_inp(info, off)	su_inb(info, off)
#define serial_out(info, off, val)	su_outb(info, off, val)
#define serial_outp(info, off, val)	su_outb(info, off, val)

/*
 * ------------------------------------------------------------
 * su_stop() and su_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void su_stop(struct tty_struct *tty)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "su_stop"))
		return;

	save_flags(flags); cli();
	if (info->IER & UART_IER_THRI) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
	}
	restore_flags(flags);
}

static void su_start(struct tty_struct *tty)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
	unsigned long flags;
	
	if (serial_paranoia_check(info, tty->device, "su_start"))
		return;

	save_flags(flags); cli();
	if (info->xmit_cnt && info->xmit_buf && !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
	}
	restore_flags(flags);
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * su_interrupt().  They were separated out for readability's sake.
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
static _INLINE_ void su_sched_event(struct su_struct *info, int event)
{
	info->event |= 1 << event;
	queue_task(&info->tqueue, &tq_serial);
	mark_bh(SERIAL_BH);
}

static _INLINE_ void receive_chars(struct su_struct *info, int *status,
		struct pt_regs *regs)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch;
	int ignored = 0;
	struct	async_icount *icount;

	icount = &info->icount;
	do {
		ch = serial_inp(info, UART_RX);
		if (info->kbd_node) {
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
		} else {
			sun_mouse_inbyte(ch);
		}
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			break;
		*tty->flip.char_buf_ptr = ch;
		icount->rx++;

#ifdef SERIAL_DEBUG_INTR
		printk("DR%02x:%02x...", ch, *status);
#endif
		*tty->flip.flag_buf_ptr = 0;
		if (*status & (UART_LSR_BI | UART_LSR_PE |
			       UART_LSR_FE | UART_LSR_OE)) {
			/*
			 * For statistics only
			 */
			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				icount->brk++;
			} else if (*status & UART_LSR_PE)
				icount->parity++;
			else if (*status & UART_LSR_FE)
				icount->frame++;
			if (*status & UART_LSR_OE)
				icount->overrun++;

			/*
			 * Now check to see if character should be
			 * ignored, and mask off conditions which
			 * should be ignored.
 */
			if (*status & info->ignore_status_mask) {
				if (++ignored > 100)
					break;
				goto ignore_char;
			}
			*status &= info->read_status_mask;

			if (*status & (UART_LSR_BI)) {
#ifdef SERIAL_DEBUG_INTR
				printk("handling break....");
#endif
				*tty->flip.flag_buf_ptr = TTY_BREAK;
				if (info->flags & ASYNC_SAK)
					do_SAK(tty);
			} else if (*status & UART_LSR_PE)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
			if (*status & UART_LSR_OE) {
				/*
				 * Overrun is special, since it's
				 * reported immediately, and doesn't
				 * affect the current character
				 */
				if (tty->flip.count < TTY_FLIPBUF_SIZE) {
					tty->flip.count++;
					tty->flip.flag_buf_ptr++;
					tty->flip.char_buf_ptr++;
					*tty->flip.flag_buf_ptr = TTY_OVERRUN;
				}
			}
		}
		tty->flip.flag_buf_ptr++;
		tty->flip.char_buf_ptr++;
		tty->flip.count++;
	ignore_char:
		*status = serial_inp(info, UART_LSR);
	} while (*status & UART_LSR_DR);
	tty_flip_buffer_push(tty);
}

static _INLINE_ void transmit_chars(struct su_struct *info, int *intr_done)
{
	int count;

	if (info->x_char) {
		serial_outp(info, UART_TX, info->x_char);
		info->icount.tx++;
		info->x_char = 0;
		if (intr_done)
			*intr_done = 0;
		return;
	}
	if ((info->xmit_cnt <= 0) || info->tty->stopped ||
	    info->tty->hw_stopped) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
		return;
	}

	count = info->xmit_fifo_size;
	do {
		serial_out(info, UART_TX, info->xmit_buf[info->xmit_tail++]);
		info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
		info->icount.tx++;
		if (--info->xmit_cnt <= 0)
			break;
	} while (--count > 0);
	
	if (info->xmit_cnt < WAKEUP_CHARS)
		su_sched_event(info, RS_EVENT_WRITE_WAKEUP);

#ifdef SERIAL_DEBUG_INTR
	printk("THRE...");
#endif
	if (intr_done)
		*intr_done = 0;

	if (info->xmit_cnt <= 0) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
	}
}

static _INLINE_ void check_modem_status(struct su_struct *info)
{
	int	status;
	struct	async_icount *icount;

	status = serial_in(info, UART_MSR);

	if (status & UART_MSR_ANY_DELTA) {
		icount = &info->icount;
		/* update input line counters */
		if (status & UART_MSR_TERI)
			icount->rng++;
		if (status & UART_MSR_DDSR)
			icount->dsr++;
		if (status & UART_MSR_DDCD) {
			icount->dcd++;
#ifdef CONFIG_HARD_PPS
			if ((info->flags & ASYNC_HARDPPS_CD) &&
			    (status & UART_MSR_DCD))
				hardpps();
#endif
	}
		if (status & UART_MSR_DCTS)
			icount->cts++;
		wake_up_interruptible(&info->delta_msr_wait);
	}

	if ((info->flags & ASYNC_CHECK_CD) && (status & UART_MSR_DDCD)) {
#if (defined(SERIAL_DEBUG_OPEN) || defined(SERIAL_DEBUG_INTR))
		printk("ttys%d CD now %s...", info->line,
		       (status & UART_MSR_DCD) ? "on" : "off");
#endif		
		if (status & UART_MSR_DCD)
			wake_up_interruptible(&info->open_wait);
		else if (!((info->flags & ASYNC_CALLOUT_ACTIVE) &&
			   (info->flags & ASYNC_CALLOUT_NOHUP))) {
#ifdef SERIAL_DEBUG_OPEN
			printk("doing serial hangup...");
#endif
			if (info->tty)
				tty_hangup(info->tty);
	}
	}
	if (info->flags & ASYNC_CTS_FLOW) {
		if (info->tty->hw_stopped) {
			if (status & UART_MSR_CTS) {
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
				printk("CTS tx start...");
#endif
				info->tty->hw_stopped = 0;
				info->IER |= UART_IER_THRI;
				serial_out(info, UART_IER, info->IER);
				su_sched_event(info, RS_EVENT_WRITE_WAKEUP);
				return;
			}
		} else {
			if (!(status & UART_MSR_CTS)) {
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
				printk("CTS tx stop...");
#endif
				info->tty->hw_stopped = 1;
				info->IER &= ~UART_IER_THRI;
				serial_out(info, UART_IER, info->IER);
			}
		}
	}
}
	
#ifdef CONFIG_SERIAL_SHARE_IRQ
/*
 * This is the serial driver's generic interrupt routine
	 */
static void su_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	int status;
	struct su_struct *info;
	int pass_counter = 0;
	struct su_struct *end_mark = 0;
#ifdef CONFIG_SERIAL_MULTIPORT
	int first_multi = 0;
	struct su_multiport_struct *multi;
#endif

#ifdef SERIAL_DEBUG_INTR
	printk("su_interrupt(%s)...", __irq_itoa(irq));
#endif

	info = IRQ_ports[irq];
	if (!info)
		return;
	
#ifdef CONFIG_SERIAL_MULTIPORT	
	multi = &rs_multiport[irq];
	if (multi->port_monitor)
		first_multi = inb(multi->port_monitor);
#endif

	do {
		if (!info->tty ||
		    (serial_in(info, UART_IIR) & UART_IIR_NO_INT)) {
			if (!end_mark)
				end_mark = info;
			goto next;
		}
		end_mark = 0;

		info->last_active = jiffies;
	
		status = serial_inp(info, UART_LSR);
#ifdef SERIAL_DEBUG_INTR
		printk("status = %x...", status);
#endif
		if (status & UART_LSR_DR)
			receive_chars(info, &status, regs);
		check_modem_status(info);
		if (status & UART_LSR_THRE)
			transmit_chars(info, 0);

	next:
		info = info->next_port;
		if (!info) {
			info = IRQ_ports[irq];
			if (pass_counter++ > RS_ISR_PASS_LIMIT) {
#if 0
				printk("rs loop break\n");
#endif
				break; 	/* Prevent infinite loops */
			}
			continue;
		}
	} while (end_mark != info);
#ifdef CONFIG_SERIAL_MULTIPORT	
	if (multi->port_monitor)
		printk("rs port monitor (normal) irq %d: 0x%x, 0x%x\n",
		       info->irq, first_multi,
		       inb(multi->port_monitor));
#endif
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}
#endif /* #ifdef CONFIG_SERIAL_SHARE_IRQ */
	

/*
 * This is the serial driver's interrupt routine for a single port
 */
static void su_interrupt_single(int irq, void *dev_id, struct pt_regs * regs)
{
	int status;
	int pass_counter = 0;
	struct su_struct * info;
#ifdef CONFIG_SERIAL_MULTIPORT	
	int first_multi = 0;
	struct rs_multiport_struct *multi;
#endif

#ifdef SERIAL_DEBUG_INTR
	printk("su_interrupt_single(%d) int=%x ...", irq,
			serial_inp(&su_table[0], UART_IIR));
#endif
	
	info = IRQ_ports[irq];
	if (!info || !info->tty)
		return;

#ifdef CONFIG_SERIAL_MULTIPORT	
	multi = &rs_multiport[irq];
	if (multi->port_monitor)
		first_multi = inb(multi->port_monitor);
#endif

	do {
		status = serial_inp(info, UART_LSR);
#ifdef SERIAL_DEBUG_INTR
		printk("status = %x...", status);
#endif
		if (status & UART_LSR_DR)
			receive_chars(info, &status, regs);
		check_modem_status(info);
		if (status & UART_LSR_THRE)
			transmit_chars(info, 0);
		if (pass_counter++ > RS_ISR_PASS_LIMIT) {
#if 0
			printk("su_single loop break.\n");
#endif
			break;
		}
	} while (!(serial_in(info, UART_IIR) & UART_IIR_NO_INT));
	info->last_active = jiffies;
#ifdef CONFIG_SERIAL_MULTIPORT	
	if (multi->port_monitor)
		printk("rs port monitor (single) irq %s: 0x%x, 0x%x\n",
		       __irq_itoa(info->irq), first_multi,
		       inb(multi->port_monitor));
#endif
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}

#ifdef CONFIG_SERIAL_MULTIPORT	
/*
 * This is the serial driver's for multiport boards
 */
static void su_interrupt_multi(int irq, void *dev_id, struct pt_regs * regs)
{
	int status;
	struct su_struct * info;
	int pass_counter = 0;
	int first_multi= 0;
	struct rs_multiport_struct *multi;

#ifdef SERIAL_DEBUG_INTR
	printk("rs_interrupt_multi(%d)...", irq);
#endif

	info = IRQ_ports[irq];
	if (!info)
		return;
	multi = &rs_multiport[irq];
	if (!multi->port1) {
		/* Should never happen */
		printk("rs_interrupt_multi: NULL port1!\n");
		return;
	}
	if (multi->port_monitor)
		first_multi = inb(multi->port_monitor);

	while (1) {
		if (!info->tty ||
		    (serial_in(info, UART_IIR) & UART_IIR_NO_INT))
			goto next;

		info->last_active = jiffies;

		status = serial_inp(info, UART_LSR);
#ifdef SERIAL_DEBUG_INTR
		printk("status = %x...", status);
#endif
		if (status & UART_LSR_DR)
			receive_chars(info, &status, regs);
		check_modem_status(info);
		if (status & UART_LSR_THRE)
			transmit_chars(info, 0);

	next:
		info = info->next_port;
		if (info)
			continue;

		info = IRQ_ports[irq];
		if (pass_counter++ > RS_ISR_PASS_LIMIT) {
#if 1
			printk("rs_multi loop break\n");
#endif
			break; 	/* Prevent infinite loops */
		}
		if (multi->port_monitor)
			printk("rs port monitor irq %d: 0x%x, 0x%x\n",
			       info->irq, first_multi,
			       inb(multi->port_monitor));
		if ((inb(multi->port1) & multi->mask1) != multi->match1)
			continue;
		if (!multi->port2)
			break;
		if ((inb(multi->port2) & multi->mask2) != multi->match2)
			continue;
		if (!multi->port3)
			break;
		if ((inb(multi->port3) & multi->mask3) != multi->match3)
			continue;
		if (!multi->port4)
			break;
		if ((inb(multi->port4) & multi->mask4) == multi->match4)
			continue;
		break;
	} 
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}
#endif

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * This routine is used to handle the "bottom half" processing for the
 * serial driver, known also the "software interrupt" processing.
 * This processing is done at the kernel interrupt level, after the
 * su_interrupt() has returned, BUT WITH INTERRUPTS TURNED ON.  This
 * is where time-consuming activities which can not be done in the
 * interrupt driver proper are done; the interrupt driver schedules
 * them using su_sched_event(), and they get done here.
 */
static void do_serial_bh(void)
{
	run_task_queue(&tq_serial);
}

static void do_softint(void *private_)
{
	struct su_struct	*info = (struct su_struct *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
	}
}

/*
 * This subroutine is called when the RS_TIMER goes off.  It is used
 * by the serial driver to handle ports that do not have an interrupt
 * (irq=0).  This doesn't work very well for 16450's, but gives barely
 * passable results for a 16550A.  (Although at the expense of much
 * CPU overhead).
 */
static void su_timer(void)
{
	static unsigned long last_strobe = 0;
	struct su_struct *info;
	unsigned int	i;

	if ((jiffies - last_strobe) >= RS_STROBE_TIME) {
		for (i=1; i < NR_IRQS; i++) {
			info = IRQ_ports[i];
			if (!info)
				continue;
			cli();
#ifdef CONFIG_SERIAL_SHARE_IRQ
			if (info->next_port) {
				do {
					serial_out(info, UART_IER, 0);
					info->IER |= UART_IER_THRI;
					serial_out(info, UART_IER, info->IER);
					info = info->next_port;
				} while (info);
#ifdef CONFIG_SERIAL_MULTIPORT
				if (rs_multiport[i].port1)
					rs_interrupt_multi(i, NULL, NULL);
				else
#endif
					su_interrupt(i, NULL, NULL);
			} else
#endif /* CONFIG_SERIAL_SHARE_IRQ */
				su_interrupt_single(i, NULL, NULL);
			sti();
		}
	}
	last_strobe = jiffies;
	timer_table[RS_TIMER].expires = jiffies + RS_STROBE_TIME;
	timer_active |= 1 << RS_TIMER;

	if (IRQ_ports[0]) {
		cli();
#ifdef CONFIG_SERIAL_SHARE_IRQ
		su_interrupt(0, NULL, NULL);
#else
		su_interrupt_single(0, NULL, NULL);
#endif
		sti();

		timer_table[RS_TIMER].expires = jiffies + IRQ_timeout[0] - 2;
	}
}

/*
 * ---------------------------------------------------------------
 * Low level utility subroutines for the serial driver:  routines to
 * figure out the appropriate timeout for an interrupt chain, routines
 * to initialize and startup a serial port, and routines to shutdown a
 * serial port.  Useful stuff like that.
 * ---------------------------------------------------------------
 */

/*
 * This routine figures out the correct timeout for a particular IRQ.
 * It uses the smallest timeout of all of the serial ports in a
 * particular interrupt chain.  Now only used for IRQ 0....
 */
static void figure_IRQ_timeout(int irq)
{
	struct su_struct	*info;
	int	timeout = 60*HZ;	/* 60 seconds === a long time :-) */

	info = IRQ_ports[irq];
	if (!info) {
		IRQ_timeout[irq] = 60*HZ;
		return;
	}
	while (info) {
		if (info->timeout < timeout)
			timeout = info->timeout;
		info = info->next_port;
	}
	if (!irq)
		timeout = timeout / 2;
	IRQ_timeout[irq] = timeout ? timeout : 1;
}

static int startup(struct su_struct *info)
{
	unsigned long flags;
	int	retval=0;
	void (*handler)(int, void *, struct pt_regs *);
	unsigned long page;
#ifdef CONFIG_SERIAL_MANY_PORTS
	unsigned short ICP;
#endif

	page = get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	save_flags(flags); cli();

	if (info->flags & ASYNC_INITIALIZED) {
		free_page(page);
		goto errout;
	}

	if (info->port == 0 || info->type == PORT_UNKNOWN) {
		if (info->tty)
			set_bit(TTY_IO_ERROR, &info->tty->flags);
		free_page(page);
		goto errout;
	}
	if (info->xmit_buf)
		free_page(page);
	else
		info->xmit_buf = (unsigned char *) page;

	if (uart_config[info->type].flags & UART_STARTECH) {
		/* Wake up UART */
		serial_outp(info, UART_LCR, 0xBF);
		serial_outp(info, UART_EFR, UART_EFR_ECB);
		serial_outp(info, UART_IER, 0);
		serial_outp(info, UART_EFR, 0);
		serial_outp(info, UART_LCR, 0);
	}

	if (info->type == PORT_16750) {
		/* Wake up UART */
		serial_outp(info, UART_IER, 0);
	}

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */
	if (uart_config[info->type].flags & UART_CLEAR_FIFO)
		serial_outp(info, UART_FCR, (UART_FCR_CLEAR_RCVR |
					     UART_FCR_CLEAR_XMIT));

	/*
	 * At this point there's no way the LSR could still be 0xFF;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (serial_inp(info, UART_LSR) == 0xff) {
		if (capable(CAP_SYS_ADMIN)) {
			if (info->tty)
				set_bit(TTY_IO_ERROR, &info->tty->flags);
		} else
			retval = -ENODEV;
		goto errout;
	}

	/*
	 * Allocate the IRQ if necessary
	 */
	if (info->irq && (!IRQ_ports[info->irq] ||
			  !IRQ_ports[info->irq]->next_port)) {
		if (IRQ_ports[info->irq]) {
#ifdef CONFIG_SERIAL_SHARE_IRQ
			free_irq(IRQ_4M(info->irq), info);
#ifdef CONFIG_SERIAL_MULTIPORT				
			if (rs_multiport[info->irq].port1)
				handler = rs_interrupt_multi;
			else
#endif
				handler = su_interrupt;
#else
			retval = -EBUSY;
			goto errout;
#endif /* CONFIG_SERIAL_SHARE_IRQ */
		} else 
			handler = su_interrupt_single;

		retval = request_irq(IRQ_4M(info->irq), handler, IRQ_T(info),
				     "serial", info);
		if (retval) {
			if (capable(CAP_SYS_ADMIN)) {
				if (info->tty)
					set_bit(TTY_IO_ERROR,
						&info->tty->flags);
				retval = 0;
			}
			goto errout;
		}
	}

	/*
	 * Insert serial port into IRQ chain.
	 */
	info->prev_port = 0;
	info->next_port = IRQ_ports[info->irq];
	if (info->next_port)
		info->next_port->prev_port = info;
	IRQ_ports[info->irq] = info;
	figure_IRQ_timeout(info->irq);

	/*
	 * Clear the interrupt registers.
	 */
     /* (void) serial_inp(info, UART_LSR); */   /* (see above) */
	(void) serial_inp(info, UART_RX);
	(void) serial_inp(info, UART_IIR);
	(void) serial_inp(info, UART_MSR);

	/*
	 * Now, initialize the UART 
	 */
	serial_outp(info, UART_LCR, UART_LCR_WLEN8);	/* reset DLAB */

	info->MCR = 0;
	if (info->tty->termios->c_cflag & CBAUD)
		info->MCR = UART_MCR_DTR | UART_MCR_RTS;
#ifdef CONFIG_SERIAL_MANY_PORTS
	if (info->flags & ASYNC_FOURPORT) {
		if (info->irq == 0)
			info->MCR |= UART_MCR_OUT1;
	} else
#endif
	{
		if (info->irq != 0)
			info->MCR |= UART_MCR_OUT2;
	}
#if defined(__alpha__) && !defined(CONFIG_PCI)
	/*
	 * DEC did something gratutiously wrong....
	 */
	info->MCR |= UART_MCR_OUT1 | UART_MCR_OUT2;
#endif
	serial_outp(info, UART_MCR, info->MCR);

	/*
	 * Finally, enable interrupts
	 */
	info->IER = UART_IER_MSI | UART_IER_RLSI | UART_IER_RDI;
	serial_outp(info, UART_IER, info->IER);	/* enable interrupts */

#ifdef CONFIG_SERIAL_MANY_PORTS
	if (info->flags & ASYNC_FOURPORT) {
		/* Enable interrupts on the AST Fourport board */
		ICP = (info->port & 0xFE0) | 0x01F;
		outb_p(0x80, ICP);
		(void) inb_p(ICP);
	}
#endif

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void)serial_inp(info, UART_LSR);
	(void)serial_inp(info, UART_RX);
	(void)serial_inp(info, UART_IIR);
	(void)serial_inp(info, UART_MSR);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * Set up serial timers...
	 */
	timer_table[RS_TIMER].expires = jiffies + 2*HZ/100;
	timer_active |= 1 << RS_TIMER;

	/*
	 * Set up the tty->alt_speed kludge
	 */
	if (info->tty) {
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			info->tty->alt_speed = 57600;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			info->tty->alt_speed = 115200;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
			info->tty->alt_speed = 230400;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
			info->tty->alt_speed = 460800;
	}

	/*
	 * and set the speed of the serial port
	 */
	change_speed(info);

	info->flags |= ASYNC_INITIALIZED;
	restore_flags(flags);
	return 0;

errout:
	restore_flags(flags);
	return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct su_struct *info)
{
	unsigned long	flags;
	int		retval;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

	save_flags(flags); cli(); /* Disable interrupts */

	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */
	wake_up_interruptible(&info->delta_msr_wait);
	
	/*
	 * First unlink the serial port from the IRQ chain...
	 */
	if (info->next_port)
		info->next_port->prev_port = info->prev_port;
	if (info->prev_port)
		info->prev_port->next_port = info->next_port;
	else
		IRQ_ports[info->irq] = info->next_port;
	figure_IRQ_timeout(info->irq);

	/*
	 * Free the IRQ, if necessary
	 */
	if (info->irq && (!IRQ_ports[info->irq] ||
			  !IRQ_ports[info->irq]->next_port)) {
		if (IRQ_ports[info->irq]) {
			free_irq(IRQ_4M(info->irq), info);
			retval = request_irq(IRQ_4M(info->irq),
			    su_interrupt_single, IRQ_T(info), "serial", info);
			
			if (retval)
				printk("serial shutdown: request_irq: error %d"
				       "  Couldn't reacquire IRQ.\n", retval);
		} else
			free_irq(IRQ_4M(info->irq), info);
	}

	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	info->IER = 0;
	serial_outp(info, UART_IER, 0x00);	/* disable all intrs */
#ifdef CONFIG_SERIAL_MANY_PORTS
	if (info->flags & ASYNC_FOURPORT) {
		/* reset interrupts on the AST Fourport board */
		(void) inb((info->port & 0xFE0) | 0x01F);
		info->MCR |= UART_MCR_OUT1;
	} else
#endif
		info->MCR &= ~UART_MCR_OUT2;
#if defined(__alpha__) && !defined(CONFIG_PCI)
	/*
	 * DEC did something gratutiously wrong....
	 */
	info->MCR |= UART_MCR_OUT1 | UART_MCR_OUT2;
#endif

	/* disable break condition */
	serial_out(info, UART_LCR, serial_inp(info, UART_LCR) & ~UART_LCR_SBC);

	if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
		info->MCR &= ~(UART_MCR_DTR|UART_MCR_RTS);
	serial_outp(info, UART_MCR, info->MCR);

	/* disable FIFO's */	
	serial_outp(info, UART_FCR, (UART_FCR_CLEAR_RCVR |
				     UART_FCR_CLEAR_XMIT));
	(void)serial_in(info, UART_RX);    /* read data port to reset things */

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	if (uart_config[info->type].flags & UART_STARTECH) {
		/* Arrange to enter sleep mode */
		serial_outp(info, UART_LCR, 0xBF);
		serial_outp(info, UART_EFR, UART_EFR_ECB);
		serial_outp(info, UART_IER, UART_IERX_SLEEP);
		serial_outp(info, UART_LCR, 0);
	}
	if (info->type == PORT_16750) {
		/* Arrange to enter sleep mode */
		serial_outp(info, UART_IER, UART_IERX_SLEEP);
	}
	info->flags &= ~ASYNC_INITIALIZED;
	restore_flags(flags);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct su_struct *info)
{
	unsigned short port;
	int	quot = 0, baud_base, baud;
	unsigned cflag, cval, fcr = 0;
	int	bits;
	unsigned long flags;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (!(port = info->port))
		return;

	/* byte size and parity */
	switch (cflag & CSIZE) {
	      case CS5: cval = 0x00; bits = 7; break;
	      case CS6: cval = 0x01; bits = 8; break;
	      case CS7: cval = 0x02; bits = 9; break;
	      case CS8: cval = 0x03; bits = 10; break;
		/* Never happens, but GCC is too dumb to figure it out */
	      default:  cval = 0x00; bits = 7; break;
	}
	if (cflag & CSTOPB) {
		cval |= 0x04;
		bits++;
	}
	if (cflag & PARENB) {
		cval |= UART_LCR_PARITY;
		bits++;
	}
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
#ifdef CMSPAR
	if (cflag & CMSPAR)
		cval |= UART_LCR_SPAR;
#endif

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(info->tty);
	baud_base = info->baud_base;
	if (baud == 38400 &&
	    ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST))
		quot = info->custom_divisor;
	else {
		if (baud == 134)
			/* Special case since 134 is really 134.5 */
			quot = (2*baud_base / 269);
		else if (baud)
			quot = baud_base / baud;
	}
	/* If the quotient is ever zero, default to 9600 bps */
	if (!quot)
		quot = baud_base / 9600;
	info->quot = quot;
	info->timeout = ((info->xmit_fifo_size*HZ*bits*quot) / baud_base);
	info->timeout += HZ/50;		/* Add .02 seconds of slop */

	/* Set up FIFO's */
	if (uart_config[info->type].flags & UART_USE_FIFO) {
		if ((info->baud_base / quot) < 2400)
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
		else
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_8;
	}
	if (info->type == PORT_16750)
		fcr |= UART_FCR7_64BYTE;

	/* CTS flow control flag and modem status interrupts */
	info->IER &= ~UART_IER_MSI;
	if (info->flags & ASYNC_HARDPPS_CD)
		info->IER |= UART_IER_MSI;
	if (cflag & CRTSCTS) {
		info->flags |= ASYNC_CTS_FLOW;
		info->IER |= UART_IER_MSI;
	} else
		info->flags &= ~ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ASYNC_CHECK_CD;
	else {
		info->flags |= ASYNC_CHECK_CD;
		info->IER |= UART_IER_MSI;
	}
	serial_out(info, UART_IER, info->IER);

	/*
	 * Set up parity check flag
	 */
#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	info->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (I_INPCK(info->tty))
		info->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (I_BRKINT(info->tty) || I_PARMRK(info->tty))
		info->read_status_mask |= UART_LSR_BI;

	/*
	 * Characters to ignore
	 */
	info->ignore_status_mask = 0;
	if (I_IGNPAR(info->tty))
		info->ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (I_IGNBRK(info->tty)) {
		info->ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore 
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(info->tty))
			info->ignore_status_mask |= UART_LSR_OE;
	}
	/*
	 * !!! ignore all characters if CREAD is not set
	 */
	if ((cflag & CREAD) == 0)
		info->ignore_status_mask |= UART_LSR_DR;
	save_flags(flags); cli();
	if (uart_config[info->type].flags & UART_STARTECH) {
		serial_outp(info, UART_LCR, 0xBF);
		serial_outp(info, UART_EFR,
			    (cflag & CRTSCTS) ? UART_EFR_CTS : 0);
	}
	serial_outp(info, UART_LCR, cval | UART_LCR_DLAB);	/* set DLAB */
	serial_outp(info, UART_DLL, quot & 0xff);	/* LS of divisor */
	serial_outp(info, UART_DLM, quot >> 8);		/* MS of divisor */
	if (info->type == PORT_16750)
		serial_outp(info, UART_FCR, fcr); 	/* set fcr */
	serial_outp(info, UART_LCR, cval);		/* reset DLAB */
	if (info->type != PORT_16750)
		serial_outp(info, UART_FCR, fcr); 	/* set fcr */
	restore_flags(flags);
	info->quot = quot;
}

static void su_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "su_put_char"))
		return;

	if (!tty || !info->xmit_buf)
		return;

	save_flags(flags); cli();
	if (info->xmit_cnt >= SERIAL_XMIT_SIZE - 1) {
		restore_flags(flags);
		return;
	}

	info->xmit_buf[info->xmit_head++] = ch;
	info->xmit_head &= SERIAL_XMIT_SIZE-1;
	info->xmit_cnt++;
	restore_flags(flags);
}

static void su_put_char_kbd(unsigned char c)
{
	struct su_struct *info;
	int i;
	int lsr;

	for (i = 0, info = su_table; i < NR_PORTS; i++, info++) {
		if (info->kbd_node != 0)
			break;
	}
	if (i >= NR_PORTS) {
		/* XXX P3: I would put a printk here but it may flood. */
		return;
	}

	do {
		lsr = serial_in(info, UART_LSR);
	} while (!(lsr & UART_LSR_THRE));

	/* Send the character out. */
	su_outb(info, UART_TX, c);
}

static void su_change_mouse_baud(int baud)
{
	struct su_struct *info = su_table;
	int i;

	for (i = 0, info = su_table; i < NR_PORTS; i++, info++)
		if (info->kbd_node != 0) break;
	if (i >= NR_PORTS) return;

	info->cflag &= ~(CBAUDEX | CBAUD);
	switch(baud) {
		case 1200:
			info->cflag |= B1200;
			break;
		case 2400:
			info->cflag |= B2400;
			break;
		case 4800:
			info->cflag |= B4800;
			break;
		case 9600:
			info->cflag |= B9600;
			break;
		default:
			printk("su_change_mouse_baud: unknown baud rate %d, "
			       "defaulting to 1200\n", baud);
			info->cflag |= 1200;
			break;
	}
	change_speed(info);
}

static void su_flush_chars(struct tty_struct *tty)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
	unsigned long flags;
				
	if (serial_paranoia_check(info, tty->device, "su_flush_chars"))
		return;

	if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !info->xmit_buf)
		return;

	save_flags(flags); cli();
	info->IER |= UART_IER_THRI;
	serial_out(info, UART_IER, info->IER);
	restore_flags(flags);
}

static int su_write(struct tty_struct * tty, int from_user,
		    const unsigned char *buf, int count)
{
	int	c, ret = 0;
	struct su_struct *info = (struct su_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "su_write"))
		return 0;

	if (!tty || !info->xmit_buf || !tmp_buf)
		return 0;

	save_flags(flags);
	if (from_user) {
		down(&tmp_buf_sem);
		while (1) {
			c = MIN(count,
				MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				    SERIAL_XMIT_SIZE - info->xmit_head));
			if (c <= 0)
				break;

			c -= copy_from_user(tmp_buf, buf, c);
			if (!c) {
				if (!ret)
					ret = -EFAULT;
				break;
			}
			cli();
			c = MIN(c, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				       SERIAL_XMIT_SIZE - info->xmit_head));
			memcpy(info->xmit_buf + info->xmit_head, tmp_buf, c);
			info->xmit_head = ((info->xmit_head + c) &
					   (SERIAL_XMIT_SIZE-1));
			info->xmit_cnt += c;
			restore_flags(flags);
			buf += c;
			count -= c;
			ret += c;
		}
		up(&tmp_buf_sem);
	} else {
		while (1) {
			cli();		
			c = MIN(count,
				MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				    SERIAL_XMIT_SIZE - info->xmit_head));
			if (c <= 0) {
				restore_flags(flags);
				break;
			}
			memcpy(info->xmit_buf + info->xmit_head, buf, c);
			info->xmit_head = ((info->xmit_head + c) &
					   (SERIAL_XMIT_SIZE-1));
			info->xmit_cnt += c;
			restore_flags(flags);
			buf += c;
			count -= c;
			ret += c;
		}
	}
	if (info->xmit_cnt && !tty->stopped && !tty->hw_stopped &&
	    !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
	}
	return ret;
}

static int su_write_room(struct tty_struct *tty)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
	int	ret;

	if (serial_paranoia_check(info, tty->device, "su_write_room"))
		return 0;
	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int su_chars_in_buffer(struct tty_struct *tty)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->device, "su_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void su_flush_buffer(struct tty_struct *tty)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;

	if (serial_paranoia_check(info, tty->device, "su_flush_buffer"))
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
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void su_send_xchar(struct tty_struct *tty, char ch)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;

	if (serial_paranoia_check(info, tty->device, "su_send_char"))
		return;

	info->x_char = ch;
	if (ch) {
		/* Make sure transmit interrupts are on */
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
	}
}

/*
 * ------------------------------------------------------------
 * su_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void su_throttle(struct tty_struct * tty)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("throttle %s: %d....\n", tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "su_throttle"))
		return;
	
	if (I_IXOFF(tty))
		su_send_xchar(tty, STOP_CHAR(tty));

	if (tty->termios->c_cflag & CRTSCTS)
		info->MCR &= ~UART_MCR_RTS;

	cli();
	serial_out(info, UART_MCR, info->MCR);
	sti();
}

static void su_unthrottle(struct tty_struct * tty)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("unthrottle %s: %d....\n", tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "su_unthrottle"))
		return;
	
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			su_send_xchar(tty, START_CHAR(tty));
	}
	if (tty->termios->c_cflag & CRTSCTS)
		info->MCR |= UART_MCR_RTS;
	cli();
	serial_out(info, UART_MCR, info->MCR);
	sti();
}

/*
 * ------------------------------------------------------------
 * su_ioctl() and friends
 * ------------------------------------------------------------
 */

#if 0
static int get_serial_info(struct su_struct * info,
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
	tmp.xmit_fifo_size = info->xmit_fifo_size;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = info->custom_divisor;
	tmp.hub6 = 0;
	if (copy_to_user(retinfo,&tmp,sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int set_serial_info(struct su_struct * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
 	struct serial_state old_state, *state;
	unsigned int		i,change_irq,change_port;
	int 			retval = 0;

	if (copy_from_user(&new_serial,new_info,sizeof(new_serial)))
		return -EFAULT;
	old_state = *state;
  
	change_irq = new_serial.irq != state->irq;
	change_port = (new_serial.port != state->port);
  
	if (!capable(CAP_SYS_ADMIN)) {
		if (change_irq || change_port ||
		    (new_serial.baud_base != state->baud_base) ||
		    (new_serial.type != state->type) ||
		    (new_serial.close_delay != state->close_delay) ||
		    (new_serial.xmit_fifo_size != state->xmit_fifo_size) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (state->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		state->flags = ((state->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		state->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	new_serial.irq = irq_cannonicalize(new_serial.irq);

	if ((new_serial.irq >= NR_IRQS) || (new_serial.port > 0xffff) ||
	    (new_serial.type < PORT_UNKNOWN) || (new_serial.type > PORT_MAX)) {
		return -EINVAL;
	}

	/* Make sure address is not already in use */
	if (new_serial.type) {
		for (i = 0 ; i < NR_PORTS; i++)
			if ((state != &su_table[i]) &&
			    (su_table[i].port == new_serial.port) &&
			    su_table[i].type)
				return -EADDRINUSE;
	}

	if ((change_port || change_irq) && (state->count > 1))
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	state->baud_base = new_serial.baud_base;
	state->flags = ((state->flags & ~ASYNC_FLAGS) |
			(new_serial.flags & ASYNC_FLAGS));
	info->flags = ((state->flags & ~ASYNC_INTERNAL_FLAGS) |
		       (info->flags & ASYNC_INTERNAL_FLAGS));
	state->custom_divisor = new_serial.custom_divisor;
	state->type = new_serial.type;
	state->close_delay = new_serial.close_delay * HZ/100;
	state->closing_wait = new_serial.closing_wait * HZ/100;
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;
	info->xmit_fifo_size = state->xmit_fifo_size =
		new_serial.xmit_fifo_size;

	release_region(state->port,8);
	if (change_port || change_irq) {
		/*
		 * We need to shutdown the serial port at the old
		 * port/irq combination.
		 */
		shutdown(info);
		state->irq = new_serial.irq;
		info->port = state->port = new_serial.port;
		info->hub6 = state->hub6 = new_serial.hub6;
	}
	if (state->type != PORT_UNKNOWN)
		request_region(state->port,8,"serial(set)");

check_and_exit:
	if (!state->port || !state->type)
		return 0;
	if (state->type != old_state.type)
		info->xmit_fifo_size = state->xmit_fifo_size =
			uart_config[state->type].dfl_xmit_fifo_size;
	if (state->flags & ASYNC_INITIALIZED) {
		if (((old_state.flags & ASYNC_SPD_MASK) !=
		     (state->flags & ASYNC_SPD_MASK)) ||
		    (old_state.custom_divisor != state->custom_divisor)) {
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
				info->tty->alt_speed = 57600;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
				info->tty->alt_speed = 115200;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
				info->tty->alt_speed = 230400;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
				info->tty->alt_speed = 460800;
			change_speed(info);
		}
	} else
		retval = startup(info);
	return retval;
}
#endif


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
static int get_lsr_info(struct su_struct * info, unsigned int *value)
{
	unsigned char status;
	unsigned int result;

	cli();
	status = serial_in(info, UART_LSR);
	sti();
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	return put_user(result,value);
}


static int get_modem_info(struct su_struct * info, unsigned int *value)
{
	unsigned char control, status;
	unsigned int result;

	control = info->MCR;
	cli();
	status = serial_in(info, UART_MSR);
	sti();
	result =  ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
		| ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
#ifdef TIOCM_OUT1
		| ((control & UART_MCR_OUT1) ? TIOCM_OUT1 : 0)
		| ((control & UART_MCR_OUT2) ? TIOCM_OUT2 : 0)
#endif
		| ((status  & UART_MSR_DCD) ? TIOCM_CAR : 0)
		| ((status  & UART_MSR_RI) ? TIOCM_RNG : 0)
		| ((status  & UART_MSR_DSR) ? TIOCM_DSR : 0)
		| ((status  & UART_MSR_CTS) ? TIOCM_CTS : 0);
	return put_user(result,value);
}

static int set_modem_info(struct su_struct * info, unsigned int cmd,
			  unsigned int *value)
{
	int error;
	unsigned int arg;

	error = get_user(arg, value);
	if (error)
		return error;
	switch (cmd) {
	case TIOCMBIS: 
		if (arg & TIOCM_RTS)
			info->MCR |= UART_MCR_RTS;
		if (arg & TIOCM_DTR)
			info->MCR |= UART_MCR_DTR;
#ifdef TIOCM_OUT1
		if (arg & TIOCM_OUT1)
			info->MCR |= UART_MCR_OUT1;
		if (arg & TIOCM_OUT2)
			info->MCR |= UART_MCR_OUT2;
#endif
		break;
	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			info->MCR &= ~UART_MCR_RTS;
		if (arg & TIOCM_DTR)
			info->MCR &= ~UART_MCR_DTR;
#ifdef TIOCM_OUT1
		if (arg & TIOCM_OUT1)
			info->MCR &= ~UART_MCR_OUT1;
		if (arg & TIOCM_OUT2)
			info->MCR &= ~UART_MCR_OUT2;
#endif
		break;
	case TIOCMSET:
		info->MCR = ((info->MCR & ~(UART_MCR_RTS |
#ifdef TIOCM_OUT1
					    UART_MCR_OUT1 |
					    UART_MCR_OUT2 |
#endif
					    UART_MCR_DTR))
			     | ((arg & TIOCM_RTS) ? UART_MCR_RTS : 0)
#ifdef TIOCM_OUT1
			     | ((arg & TIOCM_OUT1) ? UART_MCR_OUT1 : 0)
			     | ((arg & TIOCM_OUT2) ? UART_MCR_OUT2 : 0)
#endif
			     | ((arg & TIOCM_DTR) ? UART_MCR_DTR : 0));
		break;
	default:
		return -EINVAL;
	}
	cli();
	serial_out(info, UART_MCR, info->MCR);
	sti();
	return 0;
}

#if 0
static int do_autoconfig(struct su_struct * info)
{
	int			retval;
	
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (info->state->count > 1)
		return -EBUSY;
	
	shutdown(info);

	autoconfig(info->state);
	if ((info->state->flags & ASYNC_AUTO_IRQ) && (info->state->port != 0))
		info->state->irq = detect_uart_irq(info->state);

	retval = startup(info);
	if (retval)
		return retval;
	return 0;
}
#endif

/*
 * su_break() --- routine which turns the break handling on or off
 */
static void su_break(struct tty_struct *tty, int break_state)
{
	struct su_struct * info = (struct su_struct *)tty->driver_data;
	unsigned long flags;
	
	if (serial_paranoia_check(info, tty->device, "su_break"))
		return;

	if (!info->port)
		return;
	save_flags(flags); cli();
	if (break_state == -1)
		serial_out(info, UART_LCR,
			   serial_inp(info, UART_LCR) | UART_LCR_SBC);
	else
		serial_out(info, UART_LCR,
			   serial_inp(info, UART_LCR) & ~UART_LCR_SBC);
	restore_flags(flags);
}

#ifdef CONFIG_SERIAL_MULTIPORT
static int get_multiport_struct(struct su_struct * info,
				struct serial_multiport_struct *retinfo)
{
	struct serial_multiport_struct ret;
	struct rs_multiport_struct *multi;
	
	multi = &rs_multiport[info->state->irq];

	ret.port_monitor = multi->port_monitor;
	
	ret.port1 = multi->port1;
	ret.mask1 = multi->mask1;
	ret.match1 = multi->match1;
	
	ret.port2 = multi->port2;
	ret.mask2 = multi->mask2;
	ret.match2 = multi->match2;
	
	ret.port3 = multi->port3;
	ret.mask3 = multi->mask3;
	ret.match3 = multi->match3;
	
	ret.port4 = multi->port4;
	ret.mask4 = multi->mask4;
	ret.match4 = multi->match4;

	ret.irq = info->state->irq;

	if (copy_to_user(retinfo,&ret,sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int set_multiport_struct(struct su_struct * info,
				struct serial_multiport_struct *in_multi)
{
	struct serial_multiport_struct new_multi;
	struct rs_multiport_struct *multi;
	struct serial_state *state;
	int	was_multi, now_multi;
	int	retval;
	void (*handler)(int, void *, struct pt_regs *);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	state = info->state;
	
	if (copy_from_user(&new_multi, in_multi,
			   sizeof(struct serial_multiport_struct)))
		return -EFAULT;
	
	if (new_multi.irq != state->irq || state->irq == 0 ||
	    !IRQ_ports[state->irq])
		return -EINVAL;

	multi = &rs_multiport[state->irq];
	was_multi = (multi->port1 != 0);
	
	multi->port_monitor = new_multi.port_monitor;
	
	if (multi->port1)
		release_region(multi->port1,1);
	multi->port1 = new_multi.port1;
	multi->mask1 = new_multi.mask1;
	multi->match1 = new_multi.match1;
	if (multi->port1)
		request_region(multi->port1,1,"serial(multiport1)");

	if (multi->port2)
		release_region(multi->port2,1);
	multi->port2 = new_multi.port2;
	multi->mask2 = new_multi.mask2;
	multi->match2 = new_multi.match2;
	if (multi->port2)
		request_region(multi->port2,1,"serial(multiport2)");

	if (multi->port3)
		release_region(multi->port3,1);
	multi->port3 = new_multi.port3;
	multi->mask3 = new_multi.mask3;
	multi->match3 = new_multi.match3;
	if (multi->port3)
		request_region(multi->port3,1,"serial(multiport3)");

	if (multi->port4)
		release_region(multi->port4,1);
	multi->port4 = new_multi.port4;
	multi->mask4 = new_multi.mask4;
	multi->match4 = new_multi.match4;
	if (multi->port4)
		request_region(multi->port4,1,"serial(multiport4)");

	now_multi = (multi->port1 != 0);
	
	if (IRQ_ports[state->irq]->next_port &&
	    (was_multi != now_multi)) {
		free_irq(IRQ_4M(state->irq), info);
		if (now_multi)
			handler = rs_interrupt_multi;
		else
			handler = su_interrupt;

		retval = request_irq(IRQ_4M(state->irq), handler, IRQ_T(info),
				     "serial", info);
		if (retval) {
			printk("Couldn't reallocate serial interrupt "
			       "driver!!\n");
		}
	}

	return 0;
}
#endif

static int su_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	int error;
	struct su_struct * info = (struct su_struct *)tty->driver_data;
	struct async_icount cprev, cnow;	/* kernel counter temps */
	struct serial_icounter_struct *p_cuser;	/* user space */

	if (serial_paranoia_check(info, tty->device, "su_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}

	switch (cmd) {
		case TIOCMGET:
			return get_modem_info(info, (unsigned int *) arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return set_modem_info(info, cmd, (unsigned int *) arg);
#if 0
		case TIOCGSERIAL:
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSERCONFIG:
			return do_autoconfig(info);
#endif

		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, (unsigned int *) arg);

#if 0
		case TIOCSERGSTRUCT:
			if (copy_to_user((struct async_struct *) arg,
					 info, sizeof(struct async_struct)))
				return -EFAULT;
			return 0;
#endif
				
#ifdef CONFIG_SERIAL_MULTIPORT
		case TIOCSERGETMULTI:
			return get_multiport_struct(info,
				       (struct serial_multiport_struct *) arg);
		case TIOCSERSETMULTI:
			return set_multiport_struct(info,
				       (struct serial_multiport_struct *) arg);
#endif
			
		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
 		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		case TIOCMIWAIT:
			cli();
			/* note the counters on entry */
			cprev = info->icount;
			sti();
			while (1) {
				interruptible_sleep_on(&info->delta_msr_wait);
				/* see if a signal did it */
				if (signal_pending(current))
					return -ERESTARTSYS;
				cli();
				cnow = info->icount; /* atomic copy */
				sti();
				if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr && 
				    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
					return -EIO; /* no change => error */
				if ( ((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
				     ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
				     ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
				     ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
					return 0;
				}
				cprev = cnow;
			}
			/* NOTREACHED */

		/* 
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
		case TIOCGICOUNT:
			cli();
			cnow = info->icount;
			sti();
			p_cuser = (struct serial_icounter_struct *) arg;
			error = put_user(cnow.cts, &p_cuser->cts);
			if (error) return error;
			error = put_user(cnow.dsr, &p_cuser->dsr);
			if (error) return error;
			error = put_user(cnow.rng, &p_cuser->rng);
			if (error) return error;
			error = put_user(cnow.dcd, &p_cuser->dcd);
			if (error) return error;
			return 0;

		default:
			return -ENOIOCTLCMD;
		}
	/* return 0; */ /* Trigger warnings is fall through by a chance. */
}

static void su_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;

	if (   (tty->termios->c_cflag == old_termios->c_cflag)
	    && (   RELEVANT_IFLAG(tty->termios->c_iflag) 
		== RELEVANT_IFLAG(old_termios->c_iflag)))
	  return;

	change_speed(info);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) &&
	    !(tty->termios->c_cflag & CBAUD)) {
		info->MCR &= ~(UART_MCR_DTR|UART_MCR_RTS);
		cli();
		serial_out(info, UART_MCR, info->MCR);
		sti();
	}
	
	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) &&
	    (tty->termios->c_cflag & CBAUD)) {
		info->MCR |= UART_MCR_DTR;
		if (!(tty->termios->c_cflag & CRTSCTS) || 
		    !test_bit(TTY_THROTTLED, &tty->flags)) {
			info->MCR |= UART_MCR_RTS;
		}
		cli();
		serial_out(info, UART_MCR, info->MCR);
		sti();
	}
	
	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		su_start(tty);
	}

#if 0
	/*
	 * No need to wake up processes in open wait, since they
	 * sample the CLOCAL flag once, and don't recheck it.
	 * XXX  It's not clear whether the current behavior is correct
	 * or not.  Hence, this may change.....
	 */
	if (!(old_termios->c_cflag & CLOCAL) &&
	    (tty->termios->c_cflag & CLOCAL))
		wake_up_interruptible(&info->open_wait);
#endif
}

/*
 * ------------------------------------------------------------
 * su_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void su_close(struct tty_struct *tty, struct file * filp)
{
	struct su_struct *info = (struct su_struct *)tty->driver_data;
	unsigned long flags;

	if (!info || serial_paranoia_check(info, tty->device, "su_close"))
		return;

	save_flags(flags); cli();
	
	if (tty_hung_up_p(filp)) {
		DBG_CNT("before DEC-hung");
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return;
	}
	
#ifdef SERIAL_DEBUG_OPEN
	printk("su_close ttys%d, count = %d\n", info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("su_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk("su_close: bad serial port count for ttys%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		DBG_CNT("before DEC-2");
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
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	info->IER &= ~UART_IER_RLSI;
	info->read_status_mask &= ~UART_LSR_DR;
	if (info->flags & ASYNC_INITIALIZED) {
		serial_out(info, UART_IER, info->IER);
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		su_wait_until_sent(tty, info->timeout);
	}
	shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
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
	MOD_DEC_USE_COUNT;
	restore_flags(flags);
}

/*
 * su_wait_until_sent() --- wait until the transmitter is empty
 */
static void su_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct su_struct * info = (struct su_struct *)tty->driver_data;
	unsigned long orig_jiffies, char_time;
	int lsr;

	if (serial_paranoia_check(info, tty->device, "su_wait_until_sent"))
		return;

	if (info->type == PORT_UNKNOWN)
		return;

	orig_jiffies = jiffies;
	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 * 
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = (info->timeout - HZ/50) / info->xmit_fifo_size;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout)
	  char_time = MIN(char_time, timeout);
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("In su_wait_until_sent(%d) check=%lu...", timeout, char_time);
	printk("jiff=%lu...", jiffies);
#endif
	while (!((lsr = serial_inp(info, UART_LSR)) & UART_LSR_TEMT)) {
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
		printk("lsr = %d (jiff=%lu)...", lsr, jiffies);
#endif
		current->state = TASK_INTERRUPTIBLE;
		current->counter = 0;	/* make us low-priority */
		current->timeout = jiffies + char_time;
		schedule();
		if (signal_pending(current))
			break;
		if (timeout && ((orig_jiffies + timeout) < jiffies))
			break;
	}
	current->state = TASK_RUNNING;
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("lsr = %d (jiff=%lu)...done\n", lsr, jiffies);
#endif
}

/*
 * su_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void su_hangup(struct tty_struct *tty)
{
	struct su_struct * info = (struct su_struct *)tty->driver_data;

	if (serial_paranoia_check(info, tty->device, "su_hangup"))
		return;

	su_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * su_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct su_struct *info)
{
	struct wait_queue wait = { current, NULL };
	int		retval;
	int		do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		return ((info->flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
		if (info->flags & ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_SESSION_LOCKOUT) &&
		    (info->session != current->session))
		    return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
		    return -EBUSY;
		info->flags |= ASYNC_CALLOUT_ACTIVE;
		return 0;
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (info->flags & ASYNC_CALLOUT_ACTIVE) {
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
	 * su_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttys%d, count = %d\n",
	       info->line, info->count);
#endif
	cli();
	if (!tty_hung_up_p(filp)) 
		info->count--;
	sti();
	info->blocked_open++;
	while (1) {
		cli();
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (tty->termios->c_cflag & CBAUD))
			serial_out(info, UART_MCR,
				   serial_inp(info, UART_MCR) |
				   (UART_MCR_DTR | UART_MCR_RTS));
		sti();
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ASYNC_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    !(info->flags & ASYNC_CLOSING) &&
		    (do_clocal || (serial_in(info, UART_MSR) &
				   UART_MSR_DCD)))
			break;
		if (signal_pending(current)) {
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
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
	 */
static int su_open(struct tty_struct *tty, struct file * filp)
{
	struct su_struct	*info;
	int 			retval, line;
	unsigned long		page;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line < 0) || (line >= NR_PORTS))
		return -ENODEV;
	info = su_table + line;
	if (serial_paranoia_check(info, tty->device, "su_open"))
		return -ENODEV;
	info->count++;

#ifdef SERIAL_DEBUG_OPEN
	printk("su_open %s%d, count = %d\n", tty->driver.name, info->line,
	       info->count);
#endif
	tty->driver_data = info;
	info->tty = tty;
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	if (!tmp_buf) {
		page = get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		if (tmp_buf)
			free_page(page);
		else
			tmp_buf = (unsigned char *) page;
	}

	/*
	 * If the port is the middle of closing, bail out now
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		return ((info->flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
#else
		return -EAGAIN;
#endif
	}
	
	/*
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval)
		return retval;

	MOD_INC_USE_COUNT;
	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("su_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}

	if ((info->count == 1) &&
	    (info->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else 
			*tty->termios = info->callout_termios;
		change_speed(info);
	}
#ifdef CONFIG_SERIAL_CONSOLE
	if (sercons.cflag && sercons.index == line) {
		tty->termios->c_cflag = sercons.cflag;
		sercons.cflag = 0;
	change_speed(info);
	}
#endif
	info->session = current->session;
	info->pgrp = current->pgrp;

#ifdef SERIAL_DEBUG_OPEN
	printk("su_open ttys%d successful...", info->line);
#endif
	return 0;
}

/*
 * /proc fs routines....
 */

static inline int line_info(char *buf, struct su_struct *info)
{
	char	stat_buf[30], control, status;
	int	ret;

	ret = sprintf(buf, "%d: uart:%s port:%X irq:%d",
		      info->line, uart_config[info->type].name, 
		      (int)info->port, info->irq);

	if (info->port == 0 || info->type == PORT_UNKNOWN) {
		ret += sprintf(buf+ret, "\n");
		return ret;
	}

	/*
	 * Figure out the current RS-232 lines
	 */
	cli();
	status = serial_in(info, UART_MSR);
	control = info ? info->MCR : serial_in(info, UART_MCR);
	sti();

	stat_buf[0] = 0;
	stat_buf[1] = 0;
	if (control & UART_MCR_RTS)
		strcat(stat_buf, "|RTS");
	if (status & UART_MSR_CTS)
		strcat(stat_buf, "|CTS");
	if (control & UART_MCR_DTR)
		strcat(stat_buf, "|DTR");
	if (status & UART_MSR_DSR)
		strcat(stat_buf, "|DSR");
	if (status & UART_MSR_DCD)
		strcat(stat_buf, "|CD");
	if (status & UART_MSR_RI)
		strcat(stat_buf, "|RI");

	if (info->quot) {
		ret += sprintf(buf+ret, " baud:%d",
			       info->baud_base / info->quot);
	}

	ret += sprintf(buf+ret, " tx:%d rx:%d",
		      info->icount.tx, info->icount.rx);

	if (info->icount.frame)
		ret += sprintf(buf+ret, " fe:%d", info->icount.frame);

	if (info->icount.parity)
		ret += sprintf(buf+ret, " pe:%d", info->icount.parity);

	if (info->icount.brk)
		ret += sprintf(buf+ret, " brk:%d", info->icount.brk);	

	if (info->icount.overrun)
		ret += sprintf(buf+ret, " oe:%d", info->icount.overrun);

	/*
	 * Last thing is the RS-232 status lines
	 */
	ret += sprintf(buf+ret, " %s\n", stat_buf+1);
	return ret;
}

int su_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	int i, len = 0;
	off_t	begin = 0;

	len += sprintf(page, "serinfo:1.0 driver:%s\n", serial_version);
	for (i = 0; i < NR_PORTS && len < 4000; i++) {
		len += line_info(page + len, &su_table[i]);
		if (len+begin > off+count)
			goto done;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
done:
	if (off >= len+begin)
		return 0;
	*start = page + (begin-off);
	return ((count < begin+len-off) ? count : begin+len-off);
}

/*
 * ---------------------------------------------------------------------
 * su_init() and friends
 *
 * su_init() is called at boot-time to initialize the serial driver.
 * ---------------------------------------------------------------------
 */

/*
 * This routine prints out the appropriate serial driver version
 * number, and identifies which options were configured into this
 * driver.
 */
static _INLINE_ void show_su_version(void)
{
 	printk(KERN_INFO "%s version %s with", serial_name, serial_version);
#ifdef CONFIG_SERIAL_MANY_PORTS
	printk(" MANY_PORTS");
#define SERIAL_OPT
#endif
#ifdef CONFIG_SERIAL_MULTIPORT
	printk(" MULTIPORT");
#define SERIAL_OPT
#endif
#ifdef CONFIG_SERIAL_SHARE_IRQ
	printk(" SHARE_IRQ");
#endif
#define SERIAL_OPT
#ifdef CONFIG_SERIAL_DETECT_IRQ
	printk(" DETECT_IRQ");
#endif
#ifdef SERIAL_OPT
	printk(" enabled\n");
#else
	printk(" no serial options enabled\n");
#endif
#undef SERIAL_OPT
}

/*
 * This routine is called by su_init() to initialize a specific serial
 * port.  It determines what type of UART chip this serial port is
 * using: 8250, 16450, 16550, 16550A.  The important question is
 * whether or not this UART is a 16550A or not, since this will
 * determine whether or not we can use its FIFO features or not.
 */
static void
autoconfig(struct su_struct *info)
{
	unsigned char status1, status2, scratch, scratch2;
#ifdef __sparc_v9__
	struct linux_ebus_device *dev = 0;
	struct linux_ebus *ebus;
#else
	struct linux_prom_registers reg0;
#endif
	unsigned long flags;

#ifdef __sparc_v9__
	for_each_ebus(ebus) {
		for_each_ebusdev(dev, ebus) {
			if (!strncmp(dev->prom_name, "su", 2)) {
				if (dev->prom_node == info->kbd_node)
					goto ebus_done;
				if (dev->prom_node == info->ms_node)
					goto ebus_done;
			}
		}
	}
ebus_done:
	if (!dev)
		return;

	info->port = dev->base_address[0];
	if (check_region(info->port, 8))
		return;

	info->irq = dev->irqs[0];

#ifdef DEBUG_SERIAL_OPEN
	printk("Found 'su' at %016lx IRQ %s\n", dev->base_address[0],
	       __irq_itoa(dev->irqs[0]));
#endif

#else
	if (info->port_node == 0) {
		return;
	}
	if (prom_getproperty(info->port_node, "reg",
	    (char *)&reg0, sizeof(reg0)) == -1) {
		prom_printf("su: no \"reg\" property\n");
		return;
	}
	prom_apply_obio_ranges(&reg0, 1);
	if ((info->port = (unsigned long) sparc_alloc_io(reg0.phys_addr,
	    0, reg0.reg_size, "su-regs", reg0.which_io, 0)) == 0) {
		prom_printf("su: cannot map\n");
		return;
	}
	/*
	 * There is no intr property on MrCoffee, so hardwire it. Krups?
	 */
	info->irq = 13;
#endif

	info->magic = SERIAL_MAGIC;

	save_flags(flags); cli();
	
	/*
	 * Do a simple existence test first; if we fail this, there's
	 * no point trying anything else.
	 *
	 * 0x80 is used as a nonsense port to prevent against false
	 * positives due to ISA bus float.  The assumption is that
	 * 0x80 is a non-existent port; which should be safe since
	 * include/asm/io.h also makes this assumption.
	 */
	scratch = serial_in(info, UART_IER);
	su_outb(info, UART_IER, 0);
	scratch2 = serial_in(info, UART_IER);
	su_outb(info, UART_IER, scratch);
	if (scratch2) {
		restore_flags(flags);
		return;		/* We failed; there's nothing here */
	}

#if 0 /* P3 You will never beleive but SuperIO fails this test in MrCoffee. */
	scratch = serial_in(info, UART_MCR);
	su_outb(info, UART_MCR, UART_MCR_LOOP | scratch);
	scratch2 = serial_in(info, UART_MSR);
	su_outb(info, UART_MCR, UART_MCR_LOOP | 0x0A);
	status1 = serial_in(info, UART_MSR) & 0xF0;
	su_outb(info, UART_MCR, scratch);
	su_outb(info, UART_MSR, scratch2);
	if (status1 != 0x90) {
		restore_flags(flags);
		return;
	} 
#endif

	scratch2 = serial_in(info, UART_LCR);
	su_outb(info, UART_LCR, 0xBF);	/* set up for StarTech test */
	su_outb(info, UART_EFR, 0);	/* EFR is the same as FCR */
	su_outb(info, UART_LCR, 0);
	su_outb(info, UART_FCR, UART_FCR_ENABLE_FIFO);
	scratch = serial_in(info, UART_IIR) >> 6;
	switch (scratch) {
		case 0:
			info->type = PORT_16450;
			break;
		case 1:
			info->type = PORT_UNKNOWN;
			break;
		case 2:
			info->type = PORT_16550;
			break;
		case 3:
			info->type = PORT_16550A;
			break;
	}
	if (info->type == PORT_16550A) {
		/* Check for Startech UART's */
		su_outb(info, UART_LCR, scratch2 | UART_LCR_DLAB);
		if (su_inb(info, UART_EFR) == 0) {
			info->type = PORT_16650;
		} else {
			su_outb(info, UART_LCR, 0xBF);
			if (su_inb(info, UART_EFR) == 0)
				info->type = PORT_16650V2;
		}
	}
	if (info->type == PORT_16550A) {
		/* Check for TI 16750 */
		su_outb(info, UART_LCR, scratch2 | UART_LCR_DLAB);
		su_outb(info, UART_FCR,
			    UART_FCR_ENABLE_FIFO | UART_FCR7_64BYTE);
		scratch = su_inb(info, UART_IIR) >> 5;
		if (scratch == 7) {
			su_outb(info, UART_LCR, 0);
			su_outb(info, UART_FCR, UART_FCR_ENABLE_FIFO);
			scratch = su_inb(info, UART_IIR) >> 5;
			if (scratch == 6)
				info->type = PORT_16750;
		}
		su_outb(info, UART_FCR, UART_FCR_ENABLE_FIFO);
	}
	su_outb(info, UART_LCR, scratch2);
	if (info->type == PORT_16450) {
		scratch = su_inb(info, UART_SCR);
		su_outb(info, UART_SCR, 0xa5);
		status1 = su_inb(info, UART_SCR);
		su_outb(info, UART_SCR, 0x5a);
		status2 = su_inb(info, UART_SCR);
		su_outb(info, UART_SCR, scratch);

		if ((status1 != 0xa5) || (status2 != 0x5a))
			info->type = PORT_8250;
	}
	info->xmit_fifo_size = uart_config[info->type].dfl_xmit_fifo_size;

	if (info->type == PORT_UNKNOWN) {
		restore_flags(flags);
		return;
	}

#ifdef __sparc_v9__
	sprintf(info->name, "su(%s)", info->ms_node ? "mouse" : "kbd");
	request_region(info->port, 8, info->name);
#else
	strcpy(info->name, "su(serial)");
#endif

	/*
	 * Reset the UART.
	 */
	su_outb(info, UART_MCR, 0x00);
	su_outb(info, UART_FCR, (UART_FCR_CLEAR_RCVR |
				     UART_FCR_CLEAR_XMIT));
	su_inb(info, UART_RX);

	restore_flags(flags);
}

/*
 * The serial driver boot-time initialization code!
 */
__initfunc(int su_init(void))
{
	int i;
	struct su_struct *info;
	extern void atomwide_serial_init (void);
	extern void dualsp_serial_init (void);
	
#ifdef CONFIG_ATOMWIDE_SERIAL
	atomwide_serial_init ();
#endif
#ifdef CONFIG_DUALSP_SERIAL
	dualsp_serial_init ();
#endif
	
	init_bh(SERIAL_BH, do_serial_bh);
	timer_table[RS_TIMER].fn = su_timer;
	timer_table[RS_TIMER].expires = 0;

	for (i = 0; i < NR_IRQS; i++) {
		IRQ_ports[i] = 0;
		IRQ_timeout[i] = 0;
#ifdef CONFIG_SERIAL_MULTIPORT
		memset(&rs_multiport[i], 0,
		       sizeof(struct rs_multiport_struct));
#endif
	}
#if 0	/* Must be shared with keyboard on MrCoffee. */
#ifdef CONFIG_SERIAL_CONSOLE
	/*
	 *	The interrupt of the serial console port
	 *	can't be shared.
	 */
	if (sercons.flags & CON_CONSDEV) {
		for(i = 0; i < NR_PORTS; i++)
			if (i != sercons.index &&
			    su_table[i].irq == su_table[sercons.index].irq)
				su_table[i].irq = 0;
	}
#endif
#endif
	show_su_version();

	/* Initialize the tty_driver structure */

	memset(&serial_driver, 0, sizeof(struct tty_driver));
	serial_driver.magic = TTY_DRIVER_MAGIC;
	serial_driver.driver_name = "su";
	serial_driver.name = "ttyS";
	serial_driver.major = TTY_MAJOR;
	serial_driver.minor_start = 64;
	serial_driver.num = NR_PORTS;
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

	serial_driver.open = su_open;
	serial_driver.close = su_close;
	serial_driver.write = su_write;
	serial_driver.put_char = su_put_char;
	serial_driver.flush_chars = su_flush_chars;
	serial_driver.write_room = su_write_room;
	serial_driver.chars_in_buffer = su_chars_in_buffer;
	serial_driver.flush_buffer = su_flush_buffer;
	serial_driver.ioctl = su_ioctl;
	serial_driver.throttle = su_throttle;
	serial_driver.unthrottle = su_unthrottle;
	serial_driver.send_xchar = su_send_xchar;
	serial_driver.set_termios = su_set_termios;
	serial_driver.stop = su_stop;
	serial_driver.start = su_start;
	serial_driver.hangup = su_hangup;
	serial_driver.break_ctl = su_break;
	serial_driver.wait_until_sent = su_wait_until_sent;
	serial_driver.read_proc = su_read_proc;

	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	callout_driver = serial_driver;
	callout_driver.name = "cua";
	callout_driver.major = TTYAUX_MAJOR;
	callout_driver.subtype = SERIAL_TYPE_CALLOUT;
	callout_driver.read_proc = 0;
	callout_driver.proc_entry = 0;

	if (tty_register_driver(&serial_driver))
		panic("Couldn't register regular su\n");
	if (tty_register_driver(&callout_driver))
		panic("Couldn't register callout su\n");

	for (i = 0, info = su_table; i < NR_PORTS; i++, info++) {
		info->magic = SSTATE_MAGIC;
		info->line = i;
		info->type = PORT_UNKNOWN;
		info->baud_base = BAUD_BASE;
		/* info->flags = 0; */
		info->custom_divisor = 0;
		info->close_delay = 5*HZ/10;
		info->closing_wait = 30*HZ;
		info->callout_termios = callout_driver.init_termios;
		info->normal_termios = serial_driver.init_termios;
		info->icount.cts = info->icount.dsr = 
			info->icount.rng = info->icount.dcd = 0;
		info->icount.rx = info->icount.tx = 0;
		info->icount.frame = info->icount.parity = 0;
		info->icount.overrun = info->icount.brk = 0;
		info->tqueue.routine = do_softint;
		info->tqueue.data = info;

		if (info->kbd_node)
			info->cflag = B1200 | CS8 | CREAD;
		else if (info->ms_node)
			info->cflag = B4800 | CS8 | CREAD;
		else
			info->cflag = B9600 | CS8 | CREAD;

		autoconfig(info);
		if (info->type == PORT_UNKNOWN)
			continue;

		printk(KERN_INFO "%s at %16lx (irq = %s) is a %s\n",
		       info->name, info->port, __irq_itoa(info->irq),
		       uart_config[info->type].name);

		/*
		 * We want startup here because we want mouse and keyboard
		 * working without opening. On SPARC console will work
		 * without startup.
		 */
		if (info->kbd_node) {
			startup(info);
			keyboard_zsinit(su_put_char_kbd);
		} else if (info->ms_node) {
		startup(info);
			sun_mouse_zsinit();
	}
	}

	return 0;
}

__initfunc(int su_probe (unsigned long *memory_start))
{
	struct su_struct *info = su_table;
        int node, enode, tnode, sunode;
	int kbnode = 0, msnode = 0;
	int devices = 0;
	char prop[128];
	int len;

	/*
	 * Find su on MrCoffee. We return OK code if find any.
	 * Then su_init finds every one and initializes them.
	 * We do this early because MrCoffee got no aliases.
	 */
	node = prom_getchild(prom_root_node);
	if ((node = prom_searchsiblings(node, "obio")) != 0) {
		if ((sunode = prom_getchild(node)) != 0) {
			if ((sunode = prom_searchsiblings(sunode, "su")) != 0) {
				info->port_node = sunode;
#ifdef CONFIG_SERIAL_CONSOLE
				/*
				 * Console must be initiated after the generic initialization.
				 * sunserial_setinitfunc inverts order, so call this before next one.
				 */
				sunserial_setinitfunc(memory_start, su_serial_console_init);
#endif
        			sunserial_setinitfunc(memory_start, su_init);
				return 0;
			}
		}
	}

	/*
	 * Get the nodes for keyboard and mouse from 'aliases'...
	 */
        node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "aliases");
	if (!node)
		return -ENODEV;

	len = prom_getproperty(node, "keyboard", prop, sizeof(prop));
	if (len > 0) {
		prop[len] = 0;
		kbnode = prom_finddevice(prop);
	}
	if (!kbnode)
		return -ENODEV;

	len = prom_getproperty(node, "mouse", prop, sizeof(prop));
	if (len > 0) {
		prop[len] = 0;
		msnode = prom_finddevice(prop);
	}
	if (!msnode)
		return -ENODEV;

	/*
	 * Find matching EBus nodes...
	 */
        node = prom_getchild(prom_root_node);
	if ((node = prom_searchsiblings(node, "pci")) == 0) {
		return -ENODEV;		/* Plain sparc */
	}

	/*
	 * Check for SUNW,sabre on Ultra 5/10/AXi.
	 */
	len = prom_getproperty(node, "model", prop, sizeof(prop));
	if ((len > 0) && !strncmp(prop, "SUNW,sabre", len)) {
        	node = prom_getchild(node);
		node = prom_searchsiblings(node, "pci");
	}

	/*
	 * For each PCI bus...
	 */
	while (node) {
		enode = prom_getchild(node);
		enode = prom_searchsiblings(enode, "ebus");

		/*
		 * For each EBus on this PCI...
		 */
		while (enode) {
			sunode = prom_getchild(enode);
			tnode = prom_searchsiblings(sunode, "su");
			if (!tnode)
				tnode = prom_searchsiblings(sunode, "su_pnp");
			sunode = tnode;

			/*
			 * For each 'su' on this EBus...
			 */
			while (sunode) {
				/*
				 * Does it match?
				 */
				if (sunode == kbnode) {
					info->kbd_node = sunode;
					++info;
					++devices;
				}
				if (sunode == msnode) {
					info->ms_node = sunode;
					++info;
					++devices;
				}

				/*
				 * Found everything we need?
				 */
				if (devices == NR_PORTS)
					goto found;

				sunode = prom_getsibling(sunode);
				tnode = prom_searchsiblings(sunode, "su");
				if (!tnode)
					tnode = prom_searchsiblings(sunode,
								    "su_pnp");
				sunode = tnode;
			}
			enode = prom_getsibling(enode);
			enode = prom_searchsiblings(enode, "ebus");
		}
		node = prom_getsibling(node);
		node = prom_searchsiblings(node, "pci");
	}
	return -ENODEV;

found:
        sunserial_setinitfunc(memory_start, su_init);
        rs_ops.rs_change_mouse_baud = su_change_mouse_baud;
	sunkbd_setinitfunc(memory_start, sun_kbd_init);
	kbd_ops.compute_shiftstate = sun_compute_shiftstate;
	kbd_ops.setledstate = sun_setledstate;
	kbd_ops.getledstate = sun_getledstate;
	kbd_ops.setkeycode = sun_setkeycode;
	kbd_ops.getkeycode = sun_getkeycode;
#ifdef CONFIG_PCI
	sunkbd_install_keymaps(memory_start, sun_key_maps, sun_keymap_count,
			       sun_func_buf, sun_func_table,
			       sun_funcbufsize, sun_funcbufleft,
			       sun_accent_table, sun_accent_table_size);
#endif
	return 0;
}

#if 0
#ifdef MODULE
int init_module(void)
{
	return su_init();	/* rs_init? su_probe? XXX */
}

void cleanup_module(void) 
{
	unsigned long flags;
	int e1, e2;
	int i;

	/* printk("Unloading %s: version %s\n", serial_name, serial_version); */
	save_flags(flags);
	cli();
	timer_active &= ~(1 << RS_TIMER);
	timer_table[RS_TIMER].fn = NULL;
	timer_table[RS_TIMER].expires = 0;
        remove_bh(SERIAL_BH);
	if ((e1 = tty_unregister_driver(&serial_driver)))
		printk("SERIAL: failed to unregister serial driver (%d)\n",
		       e1);
	if ((e2 = tty_unregister_driver(&callout_driver)))
		printk("SERIAL: failed to unregister callout driver (%d)\n", 
		       e2);
	restore_flags(flags);

	for (i = 0; i < NR_PORTS; i++) {
		if (su_table[i].type != PORT_UNKNOWN)
			release_region(su_table[i].port, 8);
	}
	if (tmp_buf) {
		free_page((unsigned long) tmp_buf);
		tmp_buf = NULL;
	}
}
#endif /* MODULE */
#endif /* deadwood */

/*
 * ------------------------------------------------------------
 * Serial console driver
 * ------------------------------------------------------------
 */
#ifdef CONFIG_SERIAL_CONSOLE

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

/*
 *	Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct su_struct *info)
{
	int lsr;
	unsigned int tmout = 1000000;

	do {
		lsr = su_inb(info, UART_LSR);
		if (--tmout == 0)
			break;
	} while ((lsr & BOTH_EMPTY) != BOTH_EMPTY);
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 */
static void serial_console_write(struct console *co, const char *s,
				unsigned count)
{
	struct su_struct *info;
	int ier;
	unsigned i;

	info = su_table + co->index;
	/*
	 *	First save the IER then disable the interrupts
	 */
	ier = su_inb(info, UART_IER);
	su_outb(info, UART_IER, 0x00);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++, s++) {
		wait_for_xmitr(info);

		/*
		 *	Send the character out.
		 *	If a LF, also do CR...
		 */
		su_outb(info, UART_TX, *s);
		if (*s == 10) {
			wait_for_xmitr(info);
			su_outb(info, UART_TX, 13);
		}
	}

	/*
	 *	Finally, Wait for transmitter & holding register to empty
	 * 	and restore the IER
	 */
	wait_for_xmitr(info);
	su_outb(info, UART_IER, ier);
}

/*
 *	Receive character from the serial port
 */
static int serial_console_wait_key(struct console *co)
{
	struct su_struct *info;
	int ier;
	int lsr;
	int c;

	info = su_table + co->index;

	/*
	 *	First save the IER then disable the interrupts so
	 *	that the real driver for the port does not get the
	 *	character.
	 */
	ier = su_inb(info, UART_IER);
	su_outb(info, UART_IER, 0x00);

	do {
		lsr = su_inb(info, UART_LSR);
	} while (!(lsr & UART_LSR_DR));
	c = su_inb(info, UART_RX);

	/*
	 *	Restore the interrupts
	 */
	su_outb(info, UART_IER, ier);

	return c;
}

static kdev_t serial_console_device(struct console *c)
{
	return MKDEV(TTY_MAJOR, 64 + c->index);
}

/*
 *	Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first su_open()
 *	- initialize the serial port
 *	Return non-zero if we didn't find a serial port.
 */
__initfunc(static int serial_console_setup(struct console *co, char *options))
{
	struct su_struct *info;
	unsigned cval;
	int	baud = 9600;
	int	bits = 8;
	int	parity = 'n';
	int	cflag = CREAD | HUPCL | CLOCAL;
	int	quot = 0;
	char	*s;

	if (options) {
		baud = simple_strtoul(options, NULL, 10);
		s = options;
		while(*s >= '0' && *s <= '9')
			s++;
		if (*s) parity = *s++;
		if (*s) bits   = *s - '0';
	}

	/*
	 *	Now construct a cflag setting.
	 */
	switch(baud) {
		case 1200:
			cflag |= B1200;
			break;
		case 2400:
			cflag |= B2400;
			break;
		case 4800:
			cflag |= B4800;
			break;
		case 19200:
			cflag |= B19200;
			break;
		case 38400:
			cflag |= B38400;
			break;
		case 57600:
			cflag |= B57600;
			break;
		case 115200:
			cflag |= B115200;
			break;
		case 9600:
		default:
			cflag |= B9600;
			break;
	}
	switch(bits) {
		case 7:
			cflag |= CS7;
			break;
		default:
		case 8:
			cflag |= CS8;
			break;
	}
	switch(parity) {
		case 'o': case 'O':
			cflag |= PARODD;
			break;
		case 'e': case 'E':
			cflag |= PARENB;
			break;
	}
	co->cflag = cflag;

	/*
	 *	Divisor, bytesize and parity
	 */
	info = su_table + co->index;
	quot = BAUD_BASE / baud;
	cval = cflag & (CSIZE | CSTOPB);
#if defined(__powerpc__) || defined(__alpha__)
	cval >>= 8;
#else /* !__powerpc__ && !__alpha__ */
	cval >>= 4;
#endif /* !__powerpc__ && !__alpha__ */
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;

	/*
	 *	Disable UART interrupts, set DTR and RTS high
	 *	and set speed.
	 */
	su_outb(info, UART_IER, 0);
	su_outb(info, UART_MCR, UART_MCR_DTR | UART_MCR_RTS);
	su_outb(info, UART_LCR, cval | UART_LCR_DLAB);	/* set DLAB */
	su_outb(info, UART_DLL, quot & 0xff);		/* LS of divisor */
	su_outb(info, UART_DLM, quot >> 8);		/* MS of divisor */
	su_outb(info, UART_LCR, cval);			/* reset DLAB */
	info->quot = quot;

	/*
	 *	If we read 0xff from the LSR, there is no UART here.
	 */
	if (su_inb(info, UART_LSR) == 0xff)
		return -1;

	return 0;
}

static struct console sercons = {
	"ttyS",
	serial_console_write,
	NULL,
	serial_console_device,
	serial_console_wait_key,
	NULL,
	serial_console_setup,
	CON_PRINTBUFFER,
	-1,
	0,
	NULL
};

/*
 *	Register console.
 */
__initfunc(int su_serial_console_init(void))
{
	extern int con_is_present(void);

	if (con_is_present())
		return 0;
	if (serial_console == 0)
		return 0;
	if (su_table[0].port == 0 || su_table[0].port_node == 0)
		return 0;
	sercons.index = 0;
	register_console(&sercons);
	return 0;
}

#endif /* CONFIG_SERIAL_CONSOLE */

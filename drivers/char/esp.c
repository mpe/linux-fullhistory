/*
 *  esp.c - driver for Hayes ESP serial cards
 *
 *  --- Notices from serial.c, upon which this driver is based ---
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Extensively rewritten by Theodore Ts'o, 8/16/92 -- 9/14/92.  Now
 *  much more extensible to support other serial cards based on the
 *  16450/16550A UART's.  Added support for the AST FourPort and the
 *  Accent Async board.  
 *
 *  set_serial_info fixed to set the flags, custom divisor, and uart
 * 	type fields.  Fix suggested by Michael K. Johnson 12/12/92.
 *
 *  11/95: TIOCMIWAIT, TIOCGICOUNT by Angelo Haritsis <ah@doc.ic.ac.uk>
 *
 *  03/96: Modularised by Angelo Haritsis <ah@doc.ic.ac.uk>
 *
 *  rs_set_termios fixed to look also for changes of the input
 *      flags INPCK, BRKINT, PARMRK, IGNPAR and IGNBRK.
 *                                            Bernd Anhäupl 05/17/96.
 *
 * --- End of notices from serial.c ---
 *
 * Support for the ESP serial card by Andrew J. Robinson
 *     <arobinso@nyx.net> (Card detection routine taken from a patch
 *     by Dennis J. Boylan).  Patches to allow use with 2.1.x contributed
 *     by Chris Faylor.
 *
 * This module exports the following rs232 io functions:
 *
 *	int esp_init(void);
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/config.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#include <asm/dma.h>
#include <linux/malloc.h>
#include <asm/uaccess.h>

#include "esp.h"

#define NR_PORTS 64	/* maximum number of ports */
#define NR_PRIMARY 8	/* maximum number of primary ports */

/* The following variables can be set by giving module options */
static int irq[NR_PRIMARY] = {0,0,0,0,0,0,0,0};	/* IRQ for each base port */
static unsigned int divisor[NR_PRIMARY] = {0,0,0,0,0,0,0,0};
	/* custom divisor for each port */
static unsigned int dma = CONFIG_ESP_DMA_CHANNEL; /* DMA channel */
static unsigned int trigger = CONFIG_ESP_TRIGGER_LEVEL; /* FIFO trigger level */
/* END */

static char *dma_buffer;

#define DMA_BUFFER_SZ 1024

#define WAKEUP_CHARS 1024

static char *serial_name = "ESP driver";
static char *serial_version = "1.0";

DECLARE_TASK_QUEUE(tq_esp);

static struct tty_driver esp_driver, esp_callout_driver;
static int serial_refcount;

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL	1
#define SERIAL_TYPE_CALLOUT	2

/*
 * Serial driver configuration section.  Here are the various options:
 *
 * SERIAL_PARANOIA_CHECK
 * 		Check the magic number for the esp_structure where
 * 		ever possible.
 */

#undef SERIAL_PARANOIA_CHECK
#define SERIAL_DO_RESTART

#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW

#define _INLINE_ inline
  
#if defined(MODULE) && defined(SERIAL_DEBUG_MCOUNT)
#define DBG_CNT(s) printk("(%s): [%x] refc=%d, serc=%d, ttyc=%d -> %s\n", \
 kdevname(tty->device), (info->flags), serial_refcount,info->count,tty->count,s)
#else
#define DBG_CNT(s)
#endif

static struct esp_struct *IRQ_ports[16];

static void autoconfig(struct esp_struct * info);
static void change_speed(struct esp_struct *info);
	
/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD ((1843200 / 16) * (1 << ESPC_SCALE))

/* Standard COM flags (except for COM4, because of the 8514 problem) */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

static struct tty_struct *serial_table[NR_PORTS];
static struct termios *serial_termios[NR_PORTS];
static struct termios *serial_termios_locked[NR_PORTS];

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
static unsigned char *tmp_buf = 0;
static struct semaphore tmp_buf_sem = MUTEX;

static inline int serial_paranoia_check(struct esp_struct *info,
					kdev_t device, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct (%s) in %s\n";
	static const char *badinfo =
		"Warning: null esp_struct for (%s) in %s\n";

	if (!info) {
		printk(badinfo, kdevname(device), routine);
		return 1;
	}
	if (info->magic != ESP_MAGIC) {
		printk(badmagic, kdevname(device), routine);
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
	9600, 19200, 38400, 57600, 115200, 230400, 460800, 0 };

static inline unsigned int serial_in(struct esp_struct *info, int offset)
{
	return inb(info->port + offset);
}

static inline void serial_out(struct esp_struct *info, int offset, int value)
{
	outb(value, info->port+offset);
}

static inline int __get_order(unsigned long size)
{
	int order;

	size = (size + PAGE_SIZE -1) >> PAGE_SHIFT;
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);

	return order;
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
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_stop"))
		return;
	
	save_flags(flags); cli();
	if (info->IER & UART_IER_THRI) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
		serial_out(info, UART_ESI_CMD2, info->IER);
	}

	restore_flags(flags);
}

static void rs_start(struct tty_struct *tty)
{
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
	unsigned long flags;
	
	if (serial_paranoia_check(info, tty->device, "rs_start"))
		return;
	
	save_flags(flags); cli();
	if (info->xmit_cnt && info->xmit_buf && !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
		serial_out(info, UART_ESI_CMD2, info->IER);
	}
	restore_flags(flags);
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
static _INLINE_ void rs_sched_event(struct esp_struct *info,
				  int event)
{
	info->event |= 1 << event;
	queue_task_irq_off(&info->tqueue, &tq_esp);
	mark_bh(ESP_BH);
}

static _INLINE_ void receive_chars_dma(struct esp_struct *info, int *dma_bytes,
	int *dma_direction, unsigned int *who_dma)
{
	unsigned int num_chars;

        if (*dma_bytes) {
		info->stat_flags |= STAT_NEED_DMA;
        	return;
	}

	info->stat_flags &= ~(STAT_RX_TIMEOUT | STAT_NEED_DMA);

	serial_out(info, UART_ESI_CMD1, ESI_NO_COMMAND);
	serial_out(info, UART_ESI_CMD1, ESI_GET_RX_AVAIL);
	num_chars = serial_in(info, UART_ESI_STAT1) << 8;
	num_chars |= serial_in(info, UART_ESI_STAT2);

	if (!num_chars)
		return;
        
        *dma_bytes = num_chars;
	*dma_direction = DMA_MODE_READ;
	*who_dma = info->port;
        disable_dma(dma);
        clear_dma_ff(dma);
        set_dma_mode(dma, DMA_MODE_READ);
        set_dma_addr(dma, virt_to_bus(dma_buffer));
        set_dma_count(dma, num_chars);
        enable_dma(dma);
        serial_out(info, UART_ESI_CMD1, ESI_START_DMA_RX);
}

static void do_ttybuf(void *private_)
{
	struct esp_struct	*info = (struct esp_struct *) private_;
	struct tty_struct	*tty;
	int avail_bytes, x_bytes;
	unsigned long int flags;

	save_flags(flags); cli();
	tty = info->tty;

	if (!tty) {
		restore_flags(flags);
		return;
	}

	avail_bytes = TTY_FLIPBUF_SIZE - tty->flip.count;

	if (avail_bytes) {
		if (info->tty_buf->count < avail_bytes)
			x_bytes = info->tty_buf->count;
		else
			x_bytes = avail_bytes;

		tty->flip.count += x_bytes;
		memcpy(tty->flip.char_buf_ptr, info->tty_buf->char_buf,
			x_bytes);
		memcpy(tty->flip.flag_buf_ptr, info->tty_buf->flag_buf,
			x_bytes);
		tty->flip.char_buf_ptr += x_bytes;
		tty->flip.flag_buf_ptr += x_bytes;
		info->tty_buf->count -= x_bytes;
		info->tty_buf->char_buf_ptr -= x_bytes;
		info->tty_buf->flag_buf_ptr -= x_bytes;

		if (info->tty_buf->count) {
			memmove(info->tty_buf->char_buf,
				info->tty_buf->char_buf + x_bytes,
				info->tty_buf->count);
			queue_task_irq_off(&info->tty_buf->tqueue,
						&tq_timer);
		}

		queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
	} else {
		queue_task_irq_off(&info->tty_buf->tqueue, &tq_timer);
	}

	restore_flags(flags);
}

static _INLINE_ void receive_chars_dma_done(struct esp_struct *info, 
	int *dma_bytes, int *dma_direction, unsigned int *who_dma, int status)
{
	struct tty_struct *tty = info->tty;
	int num_bytes, bytes_left, x_bytes;
	struct tty_flip_buffer *buffer;

	if (!(*dma_bytes) || (*who_dma != info->port))
		return;

	disable_dma(dma);
	clear_dma_ff(dma);

	num_bytes = *dma_bytes - get_dma_residue(dma);

	buffer = &(tty->flip);
	bytes_left = num_bytes;

	if (info->tty_buf->count && (tty->flip.count < TTY_FLIPBUF_SIZE))
		do_ttybuf(info);

	while (bytes_left > 0) {
		if ((buffer->count + bytes_left) > TTY_FLIPBUF_SIZE)
			x_bytes = TTY_FLIPBUF_SIZE - buffer->count;
		else
			x_bytes = bytes_left;

		memcpy(buffer->char_buf_ptr,
			 dma_buffer + (num_bytes - bytes_left), x_bytes);
		buffer->char_buf_ptr += x_bytes;
		buffer->count += x_bytes;
		memset(buffer->flag_buf_ptr, 0, x_bytes);
		buffer->flag_buf_ptr += x_bytes;
		bytes_left -= x_bytes;

		if (bytes_left > 0) {
			if (buffer == info->tty_buf)
				break;
			else
				buffer = info->tty_buf;
		}
	}

	if (num_bytes > 0) {
		buffer->flag_buf_ptr--;

		status >>= 8;
		status &= (0x1c & info->read_status_mask);

		if (status & info->ignore_status_mask) {
			buffer->count--;
			buffer->char_buf_ptr--;
			buffer->flag_buf_ptr--;
		} else if (status & 0x10) {
			*buffer->flag_buf_ptr = TTY_BREAK;
			if (info->flags & ASYNC_SAK)
				do_SAK(tty);
		} else if (status & 0x08)
			*buffer->flag_buf_ptr = TTY_FRAME;
		else if (status & 0x04)
			*buffer->flag_buf_ptr = TTY_PARITY;
	
		buffer->flag_buf_ptr++;
		
		if (buffer == info->tty_buf)
			queue_task_irq_off(&info->tty_buf->tqueue, &tq_timer);

		queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
	}

	if (*dma_bytes != num_bytes) {
		*dma_bytes = 0;
		receive_chars_dma(info, dma_bytes, dma_direction, who_dma);
	} else
		*dma_bytes = 0;
}

static _INLINE_ void transmit_chars_dma(struct esp_struct *info, int *dma_bytes,
	int *dma_direction, unsigned int *who_dma)
{
	int count;
	
	if (*dma_bytes) {
		info->stat_flags |= STAT_NEED_DMA;
		return;
	}

	info->stat_flags &= ~STAT_NEED_DMA;

	if ((info->xmit_cnt <= 0) || info->tty->stopped ||
    		info->tty->hw_stopped) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
		serial_out(info, UART_ESI_CMD2, info->IER);
		return;
	}
	
	serial_out(info, UART_ESI_CMD1, ESI_NO_COMMAND);
	serial_out(info, UART_ESI_CMD1, ESI_GET_TX_AVAIL);
	count = serial_in(info, UART_ESI_STAT1) << 8;
	count |= serial_in(info, UART_ESI_STAT2);

	if (!count)
		return;

	count = ((count < info->xmit_cnt) ? count : info->xmit_cnt);

	if (info->xmit_tail + count <= ESP_XMIT_SIZE) {
		memcpy(dma_buffer, &(info->xmit_buf[info->xmit_tail]),
			count);
	} else {
		int i = ESP_XMIT_SIZE - info->xmit_tail;
		memcpy(dma_buffer, &(info->xmit_buf[info->xmit_tail]),
			i);
		memcpy(&(dma_buffer[i]), info->xmit_buf, count - i);
	}

	info->xmit_cnt -= count;
	info->xmit_tail = (info->xmit_tail + count) & (ESP_XMIT_SIZE - 1);

	if (info->xmit_cnt < WAKEUP_CHARS)
		rs_sched_event(info, ESP_EVENT_WRITE_WAKEUP);

#ifdef SERIAL_DEBUG_INTR
		printk("THRE...");
#endif

	if (info->xmit_cnt <= 0) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
		serial_out(info, UART_ESI_CMD2, info->IER);
	}

        *dma_bytes = count;
	*dma_direction = DMA_MODE_WRITE;
	*who_dma = info->port;
        disable_dma(dma);
        clear_dma_ff(dma);
        set_dma_mode(dma, DMA_MODE_WRITE);
        set_dma_addr(dma, virt_to_bus(dma_buffer));
        set_dma_count(dma, count);
        enable_dma(dma);
        serial_out(info, UART_ESI_CMD1, ESI_START_DMA_TX);
}

static _INLINE_ void transmit_chars_dma_done(struct esp_struct *info,
	int *dma_bytes, int *dma_direction, unsigned int *who_dma)
{
	int num_bytes;

	if (!(*dma_bytes) || (*who_dma != info->port))
		return;

	disable_dma(dma);
	clear_dma_ff(dma);

	num_bytes = *dma_bytes - get_dma_residue(dma);

	if (*dma_bytes != num_bytes)
	{
		*dma_bytes -= num_bytes;
		memmove(dma_buffer, dma_buffer + num_bytes, *dma_bytes);
		*dma_direction = DMA_MODE_WRITE;
        	disable_dma(dma);
        	clear_dma_ff(dma);
        	set_dma_mode(dma, DMA_MODE_WRITE);
        	set_dma_addr(dma, virt_to_bus(dma_buffer));
        	set_dma_count(dma, *dma_bytes);
        	enable_dma(dma);
        	serial_out(info, UART_ESI_CMD1, ESI_START_DMA_TX);
	} else
		*dma_bytes = 0;
}

static _INLINE_ void check_modem_status(struct esp_struct *info)
{
	int	status;
	
	serial_out(info, UART_ESI_CMD1, ESI_GET_UART_STAT);
	status = serial_in(info, UART_ESI_STAT2);

	if (status & UART_MSR_ANY_DELTA) {
		/* update input line counters */
		if (status & UART_MSR_TERI)
			info->icount.rng++;
		if (status & UART_MSR_DDSR)
			info->icount.dsr++;
		if (status & UART_MSR_DDCD)
			info->icount.dcd++;
		if (status & UART_MSR_DCTS)
			info->icount.cts++;
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
			printk("scheduling hangup...");
#endif
			queue_task_irq_off(&info->tqueue_hangup,
					   &tq_scheduler);
		}
	}
}

static _INLINE_ void tx_flowed_on(struct esp_struct *info)
{
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
	printk("CTS tx start...");
#endif
	info->tty->hw_stopped = 0;
	info->IER |= UART_IER_THRI;
	serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK); /* set mask */
	serial_out(info, UART_ESI_CMD2, info->IER);
	rs_sched_event(info, ESP_EVENT_WRITE_WAKEUP);
}

static _INLINE_ void tx_flowed_off(struct esp_struct *info)
{
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
	printk("CTS tx stop...");
#endif
	info->tty->hw_stopped = 1;
	info->IER &= ~UART_IER_THRI;
	serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK); /* set mask */
	serial_out(info, UART_ESI_CMD2, info->IER);
}

/*
 * This is the serial driver's interrupt routine for a single port
 */
static void rs_interrupt_single(int irq, void *dev_id, struct pt_regs * regs)
{
	struct esp_struct * info, *stop_port;
	unsigned err_status;
	unsigned int scratch;
	int pre_bytes;
	int check_dma_only = 0;
	static int dma_bytes;
	static int dma_direction;
	static int who_dma;
	
#ifdef SERIAL_DEBUG_INTR
	printk("rs_interrupt_single(%d)...", irq);
#endif
	
	/* This routine will currently check ALL ports when an interrupt */
	/* is received from ANY port */

	stop_port = info = IRQ_ports[irq];

	if (!info)
		return;

	do {
		if (!info->tty || (check_dma_only &&
			!(info->stat_flags & STAT_NEED_DMA))) {
			info = info->next_port;
			continue;
		}

		pre_bytes = dma_bytes;
		err_status = 0;
		scratch = serial_in(info, UART_ESI_SID);
		if (scratch & 0x04) { /* error - check for rx timeout */
			serial_out(info, UART_ESI_CMD1, ESI_GET_ERR_STAT);
			err_status = serial_in(info, UART_ESI_STAT1) << 8;
			err_status |= serial_in(info, UART_ESI_STAT2);

			if (err_status & 0x0100)
				info->stat_flags |= STAT_RX_TIMEOUT;

			if (err_status & 0x2000)	/* UART status */
				check_modem_status(info);

			if (err_status & 0x8000)	/* Start break */
				wake_up_interruptible(&info->break_wait);

			if (err_status & 0x0002)	/* tx off */
				tx_flowed_off(info);

			if (err_status & 0x0004)	/* tx on */
				tx_flowed_on(info);
		}
		
		if ((scratch & 0x88)  || /* DMA completed or timed out */
			(err_status & 0x1c00) /* receive error */)
			if (dma_direction == DMA_MODE_READ)
				receive_chars_dma_done(info, &dma_bytes,
					&dma_direction, &who_dma, err_status);
			else
				transmit_chars_dma_done(info, &dma_bytes,
					&dma_direction, &who_dma);

		if (((scratch & 0x01) ||
			(info->stat_flags & STAT_RX_TIMEOUT)) &&
			(info->IER & UART_IER_RDI))
			receive_chars_dma(info, &dma_bytes, &dma_direction,
				&who_dma);

		if ((scratch & 0x02) && (info->IER & UART_IER_THRI))
			transmit_chars_dma(info, &dma_bytes, &dma_direction,
				&who_dma);

		info->last_active = jiffies;

		if (pre_bytes && !dma_bytes)  /* released DMA */
			stop_port = info;

		info = info->next_port;

		if ((info->irq != irq) || (info == IRQ_ports[irq]))
			check_dma_only = 1;

		if (check_dma_only && dma_bytes)
			info = stop_port;
	} while (info != stop_port);

#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
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
	run_task_queue(&tq_esp);
}

static void do_softint(void *private_)
{
	struct esp_struct	*info = (struct esp_struct *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	if (clear_bit(ESP_EVENT_WRITE_WAKEUP, &info->event)) {
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
 * 	do_serial_hangup() -> tty->hangup() -> esp_hangup()
 * 
 */
static void do_serial_hangup(void *private_)
{
	struct esp_struct	*info = (struct esp_struct *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	tty_hangup(tty);
}

/*
 * ---------------------------------------------------------------
 * Low level utility subroutines for the serial driver:  routines to
 * figure out the appropriate timeout for an interrupt chain, routines
 * to initialize and startup a serial port, and routines to shutdown a
 * serial port.  Useful stuff like that.
 * ---------------------------------------------------------------
 */

static void esp_basic_init(struct esp_struct * info)
{
	/* put ESPC in enhanced mode */
	serial_out(info, UART_ESI_CMD1, ESI_SET_MODE);
	serial_out(info, UART_ESI_CMD2, 0x31);

	/* disable interrupts for now */
	serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
	serial_out(info, UART_ESI_CMD2, 0x00);

	/* set interrupt and DMA channel */
	serial_out(info, UART_ESI_CMD1, ESI_SET_IRQ);
	serial_out(info, UART_ESI_CMD2, (dma << 4) | 0x01);
	serial_out(info, UART_ESI_CMD1, ESI_SET_ENH_IRQ);
	if (info->line % 8)	/* secondary port */
		serial_out(info, UART_ESI_CMD2, 0x0d);	/* shared */
	else if (info->irq == 9)
		serial_out(info, UART_ESI_CMD2, 0x02);
	else
		serial_out(info, UART_ESI_CMD2, info->irq);

	/* set error status mask (check this) */
	serial_out(info, UART_ESI_CMD1, ESI_SET_ERR_MASK);
	serial_out(info, UART_ESI_CMD2, 0xbd);
	serial_out(info, UART_ESI_CMD2, 0x06);

	/* set DMA timeout */
	serial_out(info, UART_ESI_CMD1, ESI_SET_DMA_TMOUT);
	serial_out(info, UART_ESI_CMD2, 0xff);

	/* set FIFO trigger levels */
	serial_out(info, UART_ESI_CMD1, ESI_SET_TRIGGER);
	serial_out(info, UART_ESI_CMD2, trigger / 256);
	serial_out(info, UART_ESI_CMD2, trigger % 256);
	serial_out(info, UART_ESI_CMD2, trigger / 256);
	serial_out(info, UART_ESI_CMD2, trigger % 256);

	/* Set clock scaling */
	serial_out(info, UART_ESI_CMD1, ESI_SET_PRESCALAR);
	serial_out(info, UART_ESI_CMD2, ESPC_SCALE);
}

static int startup(struct esp_struct * info)
{
	unsigned long flags;
	int	retval;
	int next_irq;
	struct esp_struct *next_info = 0;
        unsigned int num_chars;

	save_flags(flags); cli();

	if (info->flags & ASYNC_INITIALIZED) {
		restore_flags(flags);
		return 0;
	}

	if (!info->port || !info->type) {
		if (info->tty)
			set_bit(TTY_IO_ERROR, &info->tty->flags);
		restore_flags(flags);
		return 0;
	}

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *)get_free_page(GFP_KERNEL);
		if (!info->xmit_buf) {
			restore_flags(flags);
			return -ENOMEM;
		}
	}

	if (!info->tty_buf) {
		info->tty_buf = (struct tty_flip_buffer *)kmalloc(
			sizeof(struct tty_flip_buffer), GFP_KERNEL);

		if (!info->tty_buf) {
			free_page((unsigned long) info->xmit_buf);
			info->xmit_buf = 0;
			restore_flags(flags);
			return -ENOMEM;
		}

		memset(info->tty_buf, 0, sizeof(struct tty_flip_buffer));
		info->tty_buf->tqueue.routine = do_ttybuf;
		info->tty_buf->tqueue.data = info;
	} 

	info->tty_buf->count = 0;
	info->tty_buf->char_buf_ptr = info->tty_buf->char_buf;
	info->tty_buf->flag_buf_ptr = info->tty_buf->flag_buf;

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttys%d (irq %d)...", info->line, info->irq);
#endif

	/* Flush the RX buffer.  Using the ESI flush command may cause */
	/* wild interrupts, so read all the data instead. */

	serial_out(info, UART_ESI_CMD1, ESI_NO_COMMAND);
	serial_out(info, UART_ESI_CMD1, ESI_GET_RX_AVAIL);
	num_chars = serial_in(info, UART_ESI_STAT1) << 8;
	num_chars |= serial_in(info, UART_ESI_STAT2);

	while (num_chars > 1) {
		inw(info->port + UART_ESI_RX);
		num_chars -= 2;
	}

	if (num_chars)
		serial_in(info, UART_ESI_RX);

	/* set receive character timeout */
	serial_out(info, UART_ESI_CMD1, ESI_SET_RX_TIMEOUT);
	serial_out(info, UART_ESI_CMD2, 0xff);

	info->stat_flags = 0;

	/*
	 * Allocate the IRQ if necessary
	 */
	if (!IRQ_ports[info->irq]) {
		retval = request_irq(info->irq, rs_interrupt_single,
					SA_INTERRUPT, "esp", NULL);

		if (!retval) {
			int i = 1;

			while ((i < 16) && !IRQ_ports[i])
				i++;	

			if (i == 16) {
				dma_buffer = (char *)__get_dma_pages(GFP_KERNEL,
					__get_order(DMA_BUFFER_SZ));

				if (!dma_buffer)
					retval = -ENOMEM;
				else
					retval = request_dma(dma, "esp");

				if (retval)
					free_irq(info->irq, NULL);
			}
		}

		if (retval) {
			restore_flags(flags);
			if (suser()) {
				if (info->tty)
					set_bit(TTY_IO_ERROR,
						&info->tty->flags);
				return 0;
			} else
				return retval;
		}
	}

	info->MCR = UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2;
	info->MCR_noint = UART_MCR_DTR | UART_MCR_RTS;
#if defined(__alpha__) && !defined(CONFIG_PCI)
	info->MCR |= UART_MCR_OUT1 | UART_MCR_OUT2;
	info->MCR_noint |= UART_MCR_OUT1 | UART_MCR_OUT2;
#endif

	serial_out(info, UART_ESI_CMD1, ESI_WRITE_UART);
	serial_out(info, UART_ESI_CMD2, UART_MCR);
	serial_out(info, UART_ESI_CMD2, info->MCR);
	
	/*
	 * Finally, enable interrupts
	 */
	/* info->IER = UART_IER_MSI | UART_IER_RLSI | UART_IER_RDI; */
	info->IER = UART_IER_RLSI | UART_IER_RDI | UART_IER_DMA_TMOUT |
			UART_IER_DMA_TC;
	serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
	serial_out(info, UART_ESI_CMD2, info->IER);
	
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/* Remove port from "closed" chain */
	if (info->next_port)
		info->next_port->prev_port = info->prev_port;
	if (info->prev_port)
		info->prev_port->next_port = info->next_port;
	else
		IRQ_ports[0] = info->next_port;

	/*
	 * Insert serial port into IRQ chain.
	 */
	next_irq = info->irq;

	do {
		next_info = IRQ_ports[next_irq];
		
		if (++next_irq > 15)
			next_irq = 1;
	} while (!next_info && (next_irq != info->irq));

	if (!next_info) {
		info->next_port = info;
		info->prev_port = info;
	} else {
		info->next_port = next_info;
		info->prev_port = next_info->prev_port;
		next_info->prev_port->next_port = info;
		next_info->prev_port = info;
	}

	IRQ_ports[info->irq] = info;

	/*
	 * set the speed of the serial port
	 */
	change_speed(info);

	info->flags |= ASYNC_INITIALIZED;
	restore_flags(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct esp_struct * info)
{
	unsigned long	flags;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....", info->line,
	       info->irq);
#endif
	
	save_flags(flags); cli(); /* Disable interrupts */

	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */
	wake_up_interruptible(&info->delta_msr_wait);
	wake_up_interruptible(&info->break_wait);
	
	/*
	 * First unlink the serial port from the IRQ chain...
	 */
	info->next_port->prev_port = info->prev_port;
	info->prev_port->next_port = info->next_port;

	if (IRQ_ports[info->irq] == info) {
		if ((info->next_port == info) ||
			(info->next_port->irq != info->irq))
			IRQ_ports[info->irq] = 0;
		else
			IRQ_ports[info->irq] = info->next_port;
	}

	/* Stick it on the "closed" chain */
	info->next_port = IRQ_ports[0];
	if (info->next_port)
		info->next_port->prev_port = info;
	info->prev_port = 0;
	IRQ_ports[0] = info;
	
	/*
	 * Free the IRQ, if necessary
	 */
	if (!IRQ_ports[info->irq]) {
		int i = 1;

		while ((i < 16) && !IRQ_ports[i])
			i++;

		if (i == 16) {
			free_dma(dma);
			free_pages((unsigned int)dma_buffer, 
				__get_order(DMA_BUFFER_SZ));
			dma_buffer = 0;
		}

		free_irq(info->irq, NULL);
	}

	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (info->tty_buf && !info->tty_buf->tqueue.sync) {
		kfree(info->tty_buf);
		info->tty_buf = 0;
	}

	info->IER = 0;
	serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
	serial_out(info, UART_ESI_CMD2, 0x00);

	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
		info->MCR &= ~(UART_MCR_DTR|UART_MCR_RTS);
		info->MCR_noint &= ~(UART_MCR_DTR|UART_MCR_RTS);
	}

	serial_out(info, UART_ESI_CMD1, ESI_WRITE_UART);
	serial_out(info, UART_ESI_CMD2, UART_MCR);
	serial_out(info, UART_ESI_CMD2, info->MCR_noint);

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	
	info->flags &= ~ASYNC_INITIALIZED;
	restore_flags(flags);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct esp_struct *info)
{
	unsigned short port;
	int	quot = 0;
	unsigned cflag,cval;
	int	i;
	unsigned char flow1 = 0, flow2 = 0;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (!(port = info->port))
		return;
	i = cflag & CBAUD;
	if (i & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i < 1 || i > 2) 
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			i += 15;
	}
	if (i == 15) {
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			i += 1;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			i += 2;
                if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
                        i += 3;
                if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
                        i += 4;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST)
			quot = info->custom_divisor;
	}
	if (quot) {
		info->timeout = ((1024*HZ*15*quot) /
				 info->baud_base) + 2;
	} else if (baud_table[i] == 134) {
		quot = (2*info->baud_base / 269);
		info->timeout = (1024*HZ*30/269) + 2;
	} else if (baud_table[i]) {
		quot = info->baud_base / baud_table[i];
		info->timeout = (1024*HZ*15/baud_table[i]) + 2;
	} else {
		quot = 0;
		info->timeout = 0;
	}
	if (quot) {
		info->MCR |= UART_MCR_DTR;
		info->MCR_noint |= UART_MCR_DTR;
		cli();
		serial_out(info, UART_ESI_CMD1, ESI_WRITE_UART);
		serial_out(info, UART_ESI_CMD2, UART_MCR);
		serial_out(info, UART_ESI_CMD2, info->MCR);
		sti();
	} else {
		info->MCR &= ~UART_MCR_DTR;
		info->MCR_noint &= ~UART_MCR_DTR;
		cli();
		serial_out(info, UART_ESI_CMD1, ESI_WRITE_UART);
		serial_out(info, UART_ESI_CMD2, UART_MCR);
		serial_out(info, UART_ESI_CMD2, info->MCR);
		sti();
		return;
	}
	/* byte size and parity */
	switch (cflag & CSIZE) {
	      case CS5: cval = 0x00; break;
	      case CS6: cval = 0x01; break;
	      case CS7: cval = 0x02; break;
	      case CS8: cval = 0x03; break;
	      default:  cval = 0x00; break;	/* too keep GCC shut... */
	}
	if (cflag & CSTOPB) {
		cval |= 0x04;
	}
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
	
	/* CTS flow control flag and modem status interrupts */
	/* info->IER &= ~UART_IER_MSI; */
	if (cflag & CRTSCTS) {
		info->flags |= ASYNC_CTS_FLOW;
		/* info->IER |= UART_IER_MSI; */
		flow1 = 0x04;
		flow2 = 0x10;
	} else
		info->flags &= ~ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ASYNC_CHECK_CD;
	else {
		info->flags |= ASYNC_CHECK_CD;
		/* info->IER |= UART_IER_MSI; */
	}

	/*
	 * Set up parity check flag
	 */
#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	info->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (I_INPCK(info->tty))
		info->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (I_BRKINT(info->tty) || I_PARMRK(info->tty))
		info->read_status_mask |= UART_LSR_BI;
	
	info->ignore_status_mask = 0;
#if 0
	/* This should be safe, but for some broken bits of hardware... */
	if (I_IGNPAR(info->tty)) {
		info->ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
		info->read_status_mask |= UART_LSR_PE | UART_LSR_FE;
	}
#endif
	if (I_IGNBRK(info->tty)) {
		info->ignore_status_mask |= UART_LSR_BI;
		info->read_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore 
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(info->tty)) {
			info->ignore_status_mask |= UART_LSR_OE | \
				UART_LSR_PE | UART_LSR_FE;
			info->read_status_mask |= UART_LSR_OE | \
				UART_LSR_PE | UART_LSR_FE;
		}
	}

	if (I_IXOFF(info->tty))
		flow1 |= 0x81;

	cli();
	/* set baud */
	serial_out(info, UART_ESI_CMD1, ESI_SET_BAUD);
	serial_out(info, UART_ESI_CMD2, quot >> 8);
	serial_out(info, UART_ESI_CMD2, quot & 0xff);

	/* set data bits, parity, etc. */
	serial_out(info, UART_ESI_CMD1, ESI_WRITE_UART);
	serial_out(info, UART_ESI_CMD2, UART_LCR);
	serial_out(info, UART_ESI_CMD2, cval);

	/* Enable flow control */
	serial_out(info, UART_ESI_CMD1, ESI_SET_FLOW_CNTL);
	serial_out(info, UART_ESI_CMD2, flow1);
	serial_out(info, UART_ESI_CMD2, flow2);

	/* set flow control characters (XON/XOFF only) */
	if (I_IXOFF(info->tty)) {
		serial_out(info, UART_ESI_CMD1, ESI_SET_FLOW_CHARS);
		serial_out(info, UART_ESI_CMD2, START_CHAR(info->tty));
		serial_out(info, UART_ESI_CMD2, STOP_CHAR(info->tty));
		serial_out(info, UART_ESI_CMD2, 0x10);
		serial_out(info, UART_ESI_CMD2, 0x21);
		switch (cflag & CSIZE) {
			case CS5:
				serial_out(info, UART_ESI_CMD2, 0x1f);
				break;
			case CS6:
				serial_out(info, UART_ESI_CMD2, 0x3f);
				break;
			case CS7:
			case CS8:
				serial_out(info, UART_ESI_CMD2, 0x7f);
				break;
			default:
				serial_out(info, UART_ESI_CMD2, 0xff);
				break;
		}
	}

	/* Set high/low water */
	serial_out(info, UART_ESI_CMD1, ESI_SET_FLOW_LVL);
	serial_out(info, UART_ESI_CMD2, 0x03);
	serial_out(info, UART_ESI_CMD2, 0xfc);
	serial_out(info, UART_ESI_CMD2, (trigger + 4) / 256);
	serial_out(info, UART_ESI_CMD2, (trigger + 4) % 256);

	sti();
}

static void rs_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_put_char"))
		return;

	if (!tty || !info->xmit_buf)
		return;

	save_flags(flags); cli();
	if (info->xmit_cnt >= ESP_XMIT_SIZE - 1) {
		restore_flags(flags);
		return;
	}

	info->xmit_buf[info->xmit_head++] = ch;
	info->xmit_head &= ESP_XMIT_SIZE-1;
	info->xmit_cnt++;
	restore_flags(flags);
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
	unsigned long flags;
				
	if (serial_paranoia_check(info, tty->device, "rs_flush_chars"))
		return;

	if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !info->xmit_buf)
		return;

	save_flags(flags); cli();
	if (!(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
		serial_out(info, UART_ESI_CMD2, info->IER);
	}
	restore_flags(flags);
}

static int rs_write(struct tty_struct * tty, int from_user,
		    const unsigned char *buf, int count)
{
	int	c, total = 0;
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
	unsigned long flags;
				
	if (serial_paranoia_check(info, tty->device, "rs_write"))
		return 0;

	if (!tty || !info->xmit_buf || !tmp_buf)
		return 0;
	    
	if (from_user)
		down(&tmp_buf_sem);
	save_flags(flags);
	while (1) {
		cli();		
		c = MIN(count, MIN(ESP_XMIT_SIZE - info->xmit_cnt - 1,
				   ESP_XMIT_SIZE - info->xmit_head));
		if (c <= 0)
			break;

		if (from_user) {
			copy_from_user(tmp_buf, buf, c);
			c = MIN(c, MIN(ESP_XMIT_SIZE - info->xmit_cnt - 1,
				       ESP_XMIT_SIZE - info->xmit_head));
			memcpy(info->xmit_buf + info->xmit_head, tmp_buf, c);
		} else
			memcpy(info->xmit_buf + info->xmit_head, buf, c);
		info->xmit_head = (info->xmit_head + c) & (ESP_XMIT_SIZE-1);
		info->xmit_cnt += c;
		restore_flags(flags);
		buf += c;
		count -= c;
		total += c;
	}
	if (from_user)
		up(&tmp_buf_sem);
	if (info->xmit_cnt && !tty->stopped && !tty->hw_stopped &&
	    !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK); /* set mask */
		serial_out(info, UART_ESI_CMD2, info->IER);
	}
	restore_flags(flags);
	return total;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
	int	ret;
				
	if (serial_paranoia_check(info, tty->device, "rs_write_room"))
		return 0;
	ret = ESP_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int rs_chars_in_buffer(struct tty_struct *tty)
{
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->device, "rs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
				
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
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("throttle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "rs_throttle"))
		return;
	
	info->IER &= ~UART_IER_RDI;
	cli();
	serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
	serial_out(info, UART_ESI_CMD2, info->IER);
	serial_out(info, UART_ESI_CMD1, ESI_SET_RX_TIMEOUT);
	serial_out(info, UART_ESI_CMD2, 0x00);
	sti();
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "rs_unthrottle"))
		return;
	
	info->IER |= UART_IER_RDI;
	cli();
	serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
	serial_out(info, UART_ESI_CMD2, info->IER);
	serial_out(info, UART_ESI_CMD1, ESI_SET_RX_TIMEOUT);
	serial_out(info, UART_ESI_CMD2, 0xff);
	sti();
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct esp_struct * info,
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
	tmp.hub6 = 0;
	copy_to_user(retinfo,&tmp,sizeof(*retinfo));
	return 0;
}

static int set_serial_info(struct esp_struct * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct esp_struct old_info;
	unsigned int		change_irq;
	int 			retval = 0;
	struct esp_struct *current_async;
	unsigned long flags = 0;

	if (!new_info)
		return -EFAULT;
	copy_from_user(&new_serial,new_info,sizeof(new_serial));
	old_info = *info;

	if ((info->type != new_serial.type) ||
		(new_serial.hub6) ||
		(info->port != new_serial.port) ||
		(info->baud_base != new_serial.baud_base) ||
		(new_serial.irq > 15) ||
		(new_serial.irq < 1))
		return -EINVAL;

	change_irq = new_serial.irq != info->irq;

	if (change_irq && (info->line % 8))
		return -EINVAL;

	if (!suser()) {
		if (change_irq || 
		    (new_serial.baud_base != info->baud_base) ||
		    (new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (info->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		info->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if (new_serial.irq == 2)
		new_serial.irq = 9;

	if (change_irq) {
		save_flags(flags); cli();

		current_async = info;
		do {
			if ((current_async->line >= info->line) &&
				(current_async->line < (info->line + 8))) {
				if (current_async == info) {
					if (current_async->count > 1) {
						restore_flags(flags);
						return -EBUSY;
					}
				} else {
					restore_flags(flags);
					return -EBUSY;
				}
			}
			
			current_async = current_async->next_port;
		} while (current_async != info);
	}

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->baud_base = new_serial.baud_base;
	info->flags = ((info->flags & ~ASYNC_FLAGS) |
			(new_serial.flags & ASYNC_FLAGS));
	info->custom_divisor = new_serial.custom_divisor;
	info->close_delay = new_serial.close_delay * HZ/100;
	info->closing_wait = new_serial.closing_wait * HZ/100;

	release_region(info->port,8);
	if (change_irq) {
		/*
		 * We need to shutdown the serial port at the old
		 * port/irq combination.
		 */
		shutdown(info);

		current_async = IRQ_ports[0];
		while (current_async != 0) {
			if ((current_async->line >= info->line) &&
				(current_async->line < (info->line + 8)))
				current_async->irq = new_serial.irq;

			current_async = current_async->next_port;
		}

		serial_out(info, UART_ESI_CMD1, ESI_SET_ENH_IRQ);
		if (info->irq == 9)
			serial_out(info, UART_ESI_CMD2, 0x02);
		else
			serial_out(info, UART_ESI_CMD2, info->irq);

		restore_flags(flags);
	}
	if(info->type != PORT_UNKNOWN)
		request_region(info->port,8,"esp(set)");

	
check_and_exit:
	if (!info->port || !info->type)
		return 0;
	if (info->flags & ASYNC_INITIALIZED) {
		if (((old_info.flags & ASYNC_SPD_MASK) !=
		     (info->flags & ASYNC_SPD_MASK)) ||
		    (old_info.custom_divisor != info->custom_divisor))
			change_speed(info);
	} else
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
static int get_lsr_info(struct esp_struct * info, unsigned int *value)
{
	unsigned char status;
	unsigned int result;

	cli();
	serial_out(info, UART_ESI_CMD1, ESI_GET_UART_STAT);
	status = serial_in(info, UART_ESI_STAT1);
	sti();
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	return put_user(result,value);
}


static int get_modem_info(struct esp_struct * info, unsigned int *value)
{
	unsigned char control, status;
	unsigned int result;

	control = info->MCR;
	cli();
	serial_out(info, UART_ESI_CMD1, ESI_GET_UART_STAT);
	status = serial_in(info, UART_ESI_STAT2);
	sti();
	result =  ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
		| ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
		| ((status  & UART_MSR_DCD) ? TIOCM_CAR : 0)
		| ((status  & UART_MSR_RI) ? TIOCM_RNG : 0)
		| ((status  & UART_MSR_DSR) ? TIOCM_DSR : 0)
		| ((status  & UART_MSR_CTS) ? TIOCM_CTS : 0);
	return put_user(result,value);
}

static int set_modem_info(struct esp_struct * info, unsigned int cmd,
			  unsigned int *value)
{
	int error;
	unsigned int arg;

	error = get_user(arg, value);
	if (error)
		return error;

	switch (cmd) {
	case TIOCMBIS: 
		if (arg & TIOCM_RTS) {
			info->MCR |= UART_MCR_RTS;
			info->MCR_noint |= UART_MCR_RTS;
		}
		if (arg & TIOCM_DTR) {
			info->MCR |= UART_MCR_DTR;
			info->MCR_noint |= UART_MCR_DTR;
		}
		break;
	case TIOCMBIC:
		if (arg & TIOCM_RTS) {
			info->MCR &= ~UART_MCR_RTS;
			info->MCR_noint &= ~UART_MCR_RTS;
		}
		if (arg & TIOCM_DTR) {
			info->MCR &= ~UART_MCR_DTR;
			info->MCR_noint &= ~UART_MCR_DTR;
		}
		break;
	case TIOCMSET:
		info->MCR = ((info->MCR & ~(UART_MCR_RTS | UART_MCR_DTR))
			     | ((arg & TIOCM_RTS) ? UART_MCR_RTS : 0)
			     | ((arg & TIOCM_DTR) ? UART_MCR_DTR : 0));
		info->MCR_noint = ((info->MCR_noint
				    & ~(UART_MCR_RTS | UART_MCR_DTR))
				   | ((arg & TIOCM_RTS) ? UART_MCR_RTS : 0)
				   | ((arg & TIOCM_DTR) ? UART_MCR_DTR : 0));
		break;
	default:
		return -EINVAL;
	}
	cli();
	serial_out(info, UART_ESI_CMD1, ESI_WRITE_UART);
	serial_out(info, UART_ESI_CMD2, UART_MCR);
	serial_out(info, UART_ESI_CMD2, info->MCR);
	sti();
	return 0;
}

static int do_autoconfig(struct esp_struct * info)
{
	int			retval;
	
	if (!suser())
		return -EPERM;
	
	if (info->count > 1)
		return -EBUSY;
	
	shutdown(info);

	cli();
	autoconfig(info);
	sti();

	retval = startup(info);
	if (retval)
		return retval;
	return 0;
}


/*
 * This routine sends a break character out the serial port.
 */
static void send_break(	struct esp_struct * info, int duration)
{
	if (!info->port)
		return;
	cli();
	serial_out(info, UART_ESI_CMD1, ESI_ISSUE_BREAK);
	serial_out(info, UART_ESI_CMD2, 0x01);

	interruptible_sleep_on(&info->break_wait);

	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + duration;
	schedule();

	serial_out(info, UART_ESI_CMD1, ESI_ISSUE_BREAK);
	serial_out(info, UART_ESI_CMD2, 0x00);
	sti();
}

static int rs_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	int error;
	struct esp_struct * info = (struct esp_struct *)tty->driver_data;
	int retval;
	struct async_icount cprev, cnow;	/* kernel counter temps */
	struct serial_icounter_struct *p_cuser;	/* user space */

	if (serial_paranoia_check(info, tty->device, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD)  &&
	    (cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
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
			return put_user(C_CLOCAL(tty) ? 1 : 0,
				    (int *) arg);
		case TIOCSSOFTCAR:
			error = get_user(arg, (unsigned int *)arg);
			if (error)
				return error;
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
			error = verify_area(VERIFY_WRITE, (void *) arg,
						sizeof(struct serial_struct));
			if (error)
				return error;
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			error = verify_area(VERIFY_READ, (void *) arg,
						sizeof(struct serial_struct));
			if (error)
				return error;
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSERCONFIG:
			return do_autoconfig(info);

		case TIOCSERGWILD:
			return put_user(0L, (unsigned long *) arg);

		case TIOCSERGETLSR: /* Get line status register */
			    return get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERSWILD:
			if (!suser())
				return -EPERM;
			return 0;

		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
 		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		 case TIOCMIWAIT:
			cli();
			cprev = info->icount;	/* note the counters on entry */
			sti();
			while (1) {
				interruptible_sleep_on(&info->delta_msr_wait);
				/* see if a signal did it */
				if (current->signal & ~current->blocked)
					return -ERESTARTSYS;
				cli();
				cnow = info->icount;	/* atomic copy */
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
			if ((error = put_user(cnow.cts, &p_cuser->cts)))
				return error;
			if ((error = put_user(cnow.dsr, &p_cuser->dsr)))
				return error;
			if ((error = put_user(cnow.rng, &p_cuser->rng)))
				return error;
			if ((error = put_user(cnow.dcd, &p_cuser->dcd)))
				return error;

			return 0;

		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct esp_struct *info = (struct esp_struct *)tty->driver_data;

	if (   (tty->termios->c_cflag == old_termios->c_cflag)
	    && (   RELEVANT_IFLAG(tty->termios->c_iflag) 
		== RELEVANT_IFLAG(old_termios->c_iflag)))
	  return;

	change_speed(info);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
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
 * rs_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct esp_struct * info = (struct esp_struct *)tty->driver_data;
	unsigned long flags;
	unsigned long timeout;

	if (!info || serial_paranoia_check(info, tty->device, "rs_close"))
		return;
	
	save_flags(flags); cli();
	
	if (tty_hung_up_p(filp)) {
		DBG_CNT("before DEC-hung");
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return;
	}
	
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_close ttys%d, count = %d\n", info->line, info->count);
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
	/* info->IER &= ~UART_IER_RLSI; */
	info->IER &= ~UART_IER_RDI;
	info->read_status_mask &= ~UART_LSR_DR;
	if (info->flags & ASYNC_INITIALIZED) {
		serial_out(info, UART_ESI_CMD1, ESI_SET_SRV_MASK);
		serial_out(info, UART_ESI_CMD2, info->IER);

		/* disable receive timeout */
		serial_out(info, UART_ESI_CMD1, ESI_SET_RX_TIMEOUT);
		serial_out(info, UART_ESI_CMD2, 0x00);

		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies+HZ;
		serial_out(info, UART_ESI_CMD1, ESI_NO_COMMAND);
		serial_out(info, UART_ESI_CMD1, ESI_GET_TX_AVAIL);
		while ((serial_in(info, UART_ESI_STAT1) != 0x03) ||
			(serial_in(info, UART_ESI_STAT2) != 0xff)) {
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + info->timeout;
			schedule();
			if (jiffies > timeout)
				break;
			serial_out(info, UART_ESI_CMD1, ESI_NO_COMMAND);
			serial_out(info, UART_ESI_CMD1, ESI_GET_TX_AVAIL);
		}
	}
	shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;

	if (info->tty_buf) {
		while (info->tty_buf->tqueue.sync) {
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + HZ / 10;
			schedule();
		}

		kfree(info->tty_buf);
		info->tty_buf = 0;
	}
			
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
 * esp_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void esp_hangup(struct tty_struct *tty)
{
	struct esp_struct * info = (struct esp_struct *)tty->driver_data;
	
	if (serial_paranoia_check(info, tty->device, "esp_hangup"))
		return;
	
	rs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * esp_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct esp_struct *info)
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
		if (info->flags & ASYNC_HUP_NOTIFY)
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
	if (!tty_hung_up_p(filp)) 
		info->count--;
	sti();
	info->blocked_open++;
	while (1) {
		cli();
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE)) {
			unsigned int scratch;

			serial_out(info, UART_ESI_CMD1, ESI_READ_UART);
			serial_out(info, UART_ESI_CMD2, UART_MCR);
			scratch = serial_in(info, UART_ESI_STAT1);
			serial_out(info, UART_ESI_CMD1, ESI_WRITE_UART);
			serial_out(info, UART_ESI_CMD2, UART_MCR);
			serial_out(info, UART_ESI_CMD2,
				scratch | UART_MCR_DTR | UART_MCR_RTS);
		}
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

		serial_out(info, UART_ESI_CMD1, ESI_GET_UART_STAT);
		if (serial_in(info, UART_ESI_STAT2) & UART_MSR_DCD)
			do_clocal = 1;

		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    !(info->flags & ASYNC_CLOSING) &&
		    (do_clocal))
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
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}	

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int esp_open(struct tty_struct *tty, struct file * filp)
{
	struct esp_struct	*info;
	int 			i, retval, line;
	unsigned long		page;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line < 0) || (line >= NR_PORTS))
		return -ENODEV;

	/* check whether or not the port is on the closed chain */

	info = IRQ_ports[0];

	while (info && (info->line != line))
		info = info->next_port;

	/* if the port is not on the closed chain, look for it on the */
	/* open chain */

	if (!info) {
		i = 1;

		while ((i < 16) && !IRQ_ports[i])
			i++;

		if (i < 16) {
			info = IRQ_ports[i];

			do {
				if (info->line == line)
					break;
				info = info->next_port;
			} while (info != IRQ_ports[i]);
		}
	}

	if (!info || (info->line != line) || 
		serial_paranoia_check(info, tty->device, "esp_open"))
		return -ENODEV;

#ifdef SERIAL_DEBUG_OPEN
	printk("esp_open %s%d, count = %d\n", tty->driver.name, info->line,
	       info->count);
#endif
	MOD_INC_USE_COUNT;
	info->count++;
	tty->driver_data = info;
	info->tty = tty;

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
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval)
		return retval;

	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("esp_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}

	if ((info->count == 1) && (info->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else 
			*tty->termios = info->callout_termios;
		change_speed(info);
	}

	info->session = current->session;
	info->pgrp = current->pgrp;

#ifdef SERIAL_DEBUG_OPEN
	printk("esp_open ttys%d successful...", info->line);
#endif
	return 0;
}

/*
 * ---------------------------------------------------------------------
 * esp_init() and friends
 *
 * esp_init() is called at boot-time to initialize the serial driver.
 * ---------------------------------------------------------------------
 */

/*
 * This routine prints out the appropriate serial driver version
 * number, and identifies which options were configured into this
 * driver.
 */
static void show_serial_version(void)
{
 	printk(KERN_INFO "%s version %s (DMA %u, trigger level %u)\n",
		serial_name, serial_version, dma, trigger);
}

/*
 * This routine is called by esp_init() to initialize a specific serial
 * port.  It determines what type of UART chip this serial port is
 * using: 8250, 16450, 16550, 16550A.  The important question is
 * whether or not this UART is a 16550A or not, since this will
 * determine whether or not we can use its FIFO features or not.
 */
static void autoconfig(struct esp_struct * info)
{
	unsigned char status1, status2, scratch;
	unsigned port = info->port;
	unsigned long flags;

	info->type = PORT_UNKNOWN;
	
	if (!port)
		return;

	save_flags(flags); cli();
	
	/*
	 * Check for ESP card
	 */
	scratch = serial_in(info, UART_ESI_BASE);
	if (scratch == 0xf3) {
		serial_out(info, UART_ESI_CMD1, 0x00);
		serial_out(info, UART_ESI_CMD1, 0x01);
		status1 = serial_in(info, UART_ESI_STAT2);
		status2 = status1 & 0x70;
		if (status2 != 0x20) {
			printk(" Old ESP found at %x\n",info->port);
		} else {
			serial_out(info, UART_ESI_CMD1, 0x02);
			status1 = serial_in(info, UART_ESI_STAT1) & 0x03;
			if (!(info->irq)) {
				if ((status1 == 0x00) || (status1 == 0x02))
					info->irq = 4;
				else
					info->irq = 3;
			}
			info->type = PORT_16550A;
			request_region(port,8,"esp(auto)");

			/* put card in enhanced mode */
			/* this prevents access through */
			/* the "old" IO ports */
			esp_basic_init(info);

			/* clear out MCR */
			serial_out(info, UART_ESI_CMD1, ESI_WRITE_UART);
			serial_out(info, UART_ESI_CMD2, UART_MCR);
			serial_out(info, UART_ESI_CMD2, 0x00);
		}
	}

	restore_flags(flags);
}

/*
 * The serial driver boot-time initialization code!
 */
int esp_init(void)
{
	int i, offset;
	struct esp_struct * info;
	int esp[] = {0x100,0x140,0x180,0x200,0x240,0x280,0x300,0x380};
	
	init_bh(ESP_BH, do_serial_bh);

	for (i = 0; i < 16; i++) {
		IRQ_ports[i] = 0;
	}

	if ((dma != 1) && (dma != 3))
		dma = CONFIG_ESP_DMA_CHANNEL;

	if ((trigger < 1) || (trigger > 1015))
		trigger = 768;
	
	show_serial_version();

	/* Initialize the tty_driver structure */
	
	memset(&esp_driver, 0, sizeof(struct tty_driver));
	esp_driver.magic = TTY_DRIVER_MAGIC;
	esp_driver.name = "ttyP";
	esp_driver.major = ESP_IN_MAJOR;
	esp_driver.minor_start = 0;
	esp_driver.num = NR_PORTS;
	esp_driver.type = TTY_DRIVER_TYPE_SERIAL;
	esp_driver.subtype = SERIAL_TYPE_NORMAL;
	esp_driver.init_termios = tty_std_termios;
	esp_driver.init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	esp_driver.flags = TTY_DRIVER_REAL_RAW;
	esp_driver.refcount = &serial_refcount;
	esp_driver.table = serial_table;
	esp_driver.termios = serial_termios;
	esp_driver.termios_locked = serial_termios_locked;

	esp_driver.open = esp_open;
	esp_driver.close = rs_close;
	esp_driver.write = rs_write;
	esp_driver.put_char = rs_put_char;
	esp_driver.flush_chars = rs_flush_chars;
	esp_driver.write_room = rs_write_room;
	esp_driver.chars_in_buffer = rs_chars_in_buffer;
	esp_driver.flush_buffer = rs_flush_buffer;
	esp_driver.ioctl = rs_ioctl;
	esp_driver.throttle = rs_throttle;
	esp_driver.unthrottle = rs_unthrottle;
	esp_driver.set_termios = rs_set_termios;
	esp_driver.stop = rs_stop;
	esp_driver.start = rs_start;
	esp_driver.hangup = esp_hangup;

	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	esp_callout_driver = esp_driver;
	esp_callout_driver.name = "cup";
	esp_callout_driver.major = ESP_OUT_MAJOR;
	esp_callout_driver.subtype = SERIAL_TYPE_CALLOUT;

	if (tty_register_driver(&esp_driver))
		panic("Couldn't register serial driver\n");
	if (tty_register_driver(&esp_callout_driver))
		panic("Couldn't register callout driver\n");
	
	info = (struct esp_struct *)kmalloc(sizeof(struct esp_struct),
		GFP_KERNEL);
	if (!info)
		panic("Could not allocate memory for device information\n");
	memset((void *)info, 0, sizeof(struct esp_struct));

	i = 0;
	offset = 0;

	do {
		info->port = esp[i] + offset;
		info->baud_base = BASE_BAUD;
		info->custom_divisor = (divisor[i] >> (offset / 2)) & 0xf;
		info->flags = STD_COM_FLAGS;
		if (info->custom_divisor)
			info->flags |= ASYNC_SPD_CUST;
		info->irq = irq[i];

		info->magic = ESP_MAGIC;
		info->line = (i * 8) + (offset / 8);
		info->tty = 0;
		info->type = PORT_UNKNOWN;
		info->close_delay = 5*HZ/10;
		info->closing_wait = 30*HZ;
		info->tqueue.routine = do_softint;
		info->tqueue.data = info;
		info->tqueue_hangup.routine = do_serial_hangup;
		info->tqueue_hangup.data = info;
		info->callout_termios = esp_callout_driver.init_termios;
		info->normal_termios = esp_driver.init_termios;

		if (info->irq == 2)
			info->irq = 9;

		autoconfig(info);
		if (info->type == PORT_UNKNOWN) {
			i++;
			offset = 0;
			continue;
		}
		if (IRQ_ports[0])
			IRQ_ports[0]->prev_port = info;
		info->next_port = IRQ_ports[0];
		info->prev_port = 0;
		IRQ_ports[0] = info;
		printk(KERN_INFO "ttyP%d at 0x%04x (irq = %d) is an ESP ",
			info->line, info->port, info->irq);
		if (info->line % 8)
			printk("secondary port\n");
		else {
			printk("primary port\n");
			irq[i] = info->irq;
		}

		info = (struct esp_struct *)kmalloc(sizeof(struct esp_struct),
			GFP_KERNEL);
		if (!info)
			panic("Could not allocate memory for device information\n");
		memset((void *)info, 0, sizeof(struct esp_struct));

		if (offset == 56) {
			i++;
			offset = 0;
		} else {
			offset += 8;
		}
	} while (i < NR_PRIMARY);

	/* free the last port memory allocation */
	kfree(info);

	return 0;
}

#ifdef MODULE

int init_module(void)
{
	return esp_init();
}

void cleanup_module(void) 
{
	unsigned long flags;
	int e1, e2;
	struct esp_struct *current_async, *temp_async;

	/* printk("Unloading %s: version %s\n", serial_name, serial_version); */
	save_flags(flags);
	cli();
	if ((e1 = tty_unregister_driver(&esp_driver)))
		printk("SERIAL: failed to unregister serial driver (%d)\n",
		       e1);
	if ((e2 = tty_unregister_driver(&esp_callout_driver)))
		printk("SERIAL: failed to unregister callout driver (%d)\n", 
		       e2);
	restore_flags(flags);

	current_async = IRQ_ports[0];
	while (current_async != 0) {
		if (current_async->type != PORT_UNKNOWN)
			release_region(current_async->port, 8);
		current_async = current_async->next_port;
	}

	if (dma_buffer)
		free_pages((unsigned int)dma_buffer,
			__get_order(DMA_BUFFER_SZ));

	if (tmp_buf)
		free_page((unsigned long)tmp_buf);

	/* free the port information */
	current_async = IRQ_ports[0];
	while (current_async != 0) {
		temp_async = current_async->next_port;
		kfree(current_async);
		current_async = temp_async;
	}
}
#endif /* MODULE */

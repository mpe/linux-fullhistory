/* $Id: sh-sci.c,v 1.40 2000/04/15 06:57:29 gniibe Exp $
 *
 *  linux/drivers/char/sh-sci.c
 *
 *  SuperH on-chip serial module support.  (SCI with no FIFO / with FIFO)
 *  Copyright (C) 1999, 2000  Niibe Yutaka
 *  Copyright (C) 2000  Sugioka Toshinobu
 *
 * TTY code is based on sx.c (Specialix SX driver) by:
 *
 *   (C) 1998 R.E.Wolff@BitWizard.nl
 *
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
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/console.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#include <linux/generic_serial.h>
#include "sh-sci.h"

#ifdef CONFIG_DEBUG_KERNEL_WITH_GDB_STUB
static void gdb_detach(void);
#endif

struct sci_port sci_ports[1];

/* Function prototypes */
static void sci_disable_tx_interrupts(void *ptr);
static void sci_enable_tx_interrupts(void *ptr);
static void sci_disable_rx_interrupts(void *ptr);
static void sci_enable_rx_interrupts(void *ptr);
static int  sci_get_CD(void *ptr);
static void sci_shutdown_port(void *ptr);
static int sci_set_real_termios(void *ptr);
static void sci_hungup(void *ptr);
static void sci_close(void *ptr);
static int sci_chars_in_buffer(void *ptr);
static int sci_init_drivers(void);

static struct tty_driver sci_driver, sci_callout_driver;

#define SCI_NPORTS 1
static struct tty_struct *sci_table[SCI_NPORTS] = { NULL, };
static struct termios *sci_termios[2]; /* nomal, locked */

int sci_refcount;
int sci_debug = 0;

#ifdef MODULE
MODULE_PARM(sci_debug, "i");
#endif

static struct real_driver sci_real_driver = {
	sci_disable_tx_interrupts,
	sci_enable_tx_interrupts,
	sci_disable_rx_interrupts,
	sci_enable_rx_interrupts,
	sci_get_CD,
	sci_shutdown_port,
	sci_set_real_termios,
	sci_chars_in_buffer,
	sci_close,
	sci_hungup,
	NULL
};

static void sci_setsignals(struct sci_port *port, int dtr, int rts)
{
	/* This routine is used for seting signals of: DTR, DCD, CTS/RTS */
	/* We use SCIF's hardware for CTS/RTS, so don't need any for that. */
	/* If you have signals for DTR and DCD, please implement here. */
	;
}

static int sci_getsignals(struct sci_port *port)
{
	/* This routine is used for geting signals of: DTR, DCD, DSR, RI,
	   and CTS/RTS */

	return TIOCM_DTR|TIOCM_RTS|TIOCM_DSR;
/*
	(((o_stat & OP_DTR)?TIOCM_DTR:0) |
	 ((o_stat & OP_RTS)?TIOCM_RTS:0) |
	 ((i_stat & IP_CTS)?TIOCM_CTS:0) |
	 ((i_stat & IP_DCD)?TIOCM_CAR:0) |
	 ((i_stat & IP_DSR)?TIOCM_DSR:0) |
	 ((i_stat & IP_RI) ?TIOCM_RNG:0)
*/
}

static void sci_set_baud(struct sci_port *port)
{
	int t;

	switch (port->gs.baud) {
	case 0:
		t = -1;
		break;
	case 2400:
		t = BPS_2400;
		break;
	case 4800:
		t = BPS_4800;
		break;
	case 9600:
		t = BPS_9600;
		break;
	case 19200:
		t = BPS_19200;
		break;
	case 38400:
		t = BPS_38400;
		break;
	default:
		printk(KERN_INFO "sci: unsupported baud rate: %d, use 115200 instead.\n", port->gs.baud);
	case 115200:
		t = BPS_115200;
		break;
	}

	if (t > 0) {
		sci_setsignals (port, 1, -1);
		if(t >= 256) {
			ctrl_out((ctrl_in(SCSMR) & ~3) | 1, SCSMR);
			t >>= 2;
		}
		ctrl_outb(t, SCBRR);
		ctrl_outw(0xa400, RFCR); /* Refresh counter clear */
		while (ctrl_inw(RFCR) < WAIT_RFCR_COUNTER)
			;
	} else {
		sci_setsignals (port, 0, -1);
	}
}

static void sci_set_termios_cflag(struct sci_port *port)
{
	unsigned short status;
	unsigned short smr_val;
#if defined(CONFIG_SH_SCIF_SERIAL)
	unsigned short fcr_val=6; /* TFRST=1, RFRST=1 */
#endif

	do
		status = ctrl_in(SC_SR);
	while (!(status & SCI_TEND));

	port->old_cflag = port->gs.tty->termios->c_cflag;

	ctrl_out(0x00, SCSCR);	/* TE=0, RE=0, CKE1=0 */
#if defined(CONFIG_SH_SCIF_SERIAL)
	ctrl_out(fcr_val, SCFCR);
	fcr_val = 0;
#endif

	smr_val = ctrl_in(SCSMR) & 3;
	if ((port->gs.tty->termios->c_cflag & CSIZE) == CS7)
		smr_val |= 0x40;
	if (C_PARENB(port->gs.tty))
		smr_val |= 0x20;
	if (C_PARODD(port->gs.tty))
		smr_val |= 0x10;
	if (C_CSTOPB(port->gs.tty))
		smr_val |= 0x08;
	ctrl_out(smr_val, SCSMR);

#if defined(CONFIG_SH_SCIF_SERIAL)
#if defined(__sh3__)
	{ /* For SH7709, SH7709A, SH7729 */
		unsigned short data;

		/* We need to set SCPCR to enable RTS/CTS */
		data = ctrl_inw(SCPCR);
		/* Clear out SCP7MD1,0, SCP6MD1,0, SCP4MD1,0*/
		ctrl_outw(data&0x0fcf, SCPCR); 
	}
#endif
	if (C_CRTSCTS(port->gs.tty))
		fcr_val |= 0x08;
	else {
#if defined(__sh3__)
		unsigned short data;

		/* We need to set SCPCR to enable RTS/CTS */
		data = ctrl_inw(SCPCR);
		/* Clear out SCP7MD1,0, SCP4MD1,0,
		   Set SCP6MD1,0 = {01} (output)  */
		ctrl_outw((data&0x0fcf)|0x1000, SCPCR);

		data = ctrl_inb(SCPDR);
		/* Set /RTS2 (bit6) = 0 */
		ctrl_outb(data&0xbf, SCPDR);
#elif defined(__SH4__)
		ctrl_outw(0x0080, SCSPTR); /* Set RTS = 1 */
#endif
	}
	ctrl_out(fcr_val, SCFCR);
#endif

	sci_set_baud(port);
	ctrl_out(SCSCR_INIT, SCSCR);	/* TIE=0,RIE=0,TE=1,RE=1 */
	sci_enable_rx_interrupts(port);
}

static int sci_set_real_termios(void *ptr)
{
	struct sci_port *port = ptr;

	if (port->old_cflag != port->gs.tty->termios->c_cflag)
		sci_set_termios_cflag(port);

	/* Tell line discipline whether we will do input cooking */
	if (I_OTHER(port->gs.tty))
		clear_bit(TTY_HW_COOK_IN, &port->gs.tty->flags);
	else
		set_bit(TTY_HW_COOK_IN, &port->gs.tty->flags);

/* Tell line discipline whether we will do output cooking.
 * If OPOST is set and no other output flags are set then we can do output
 * processing.  Even if only *one* other flag in the O_OTHER group is set
 * we do cooking in software.
 */
	if (O_OPOST(port->gs.tty) && !O_OTHER(port->gs.tty))
		set_bit(TTY_HW_COOK_OUT, &port->gs.tty->flags);
	else
		clear_bit(TTY_HW_COOK_OUT, &port->gs.tty->flags);

	return 0;
}

/* ********************************************************************** *
 *                   the interrupt related routines                       *
 * ********************************************************************** */

static void sci_transmit_chars(struct sci_port *port)
{
	int count, i;
	int txroom;
	unsigned long flags;
	unsigned short status;
	unsigned short ctrl;
	unsigned char c;

	status = ctrl_in(SC_SR);
	if (!(status & SCI_TD_E)) {
		save_and_cli(flags);
		ctrl = ctrl_in(SCSCR);
		if (port->gs.xmit_cnt == 0) {
			ctrl &= ~SCI_CTRL_FLAGS_TIE;
			port->gs.flags &= ~GS_TX_INTEN;
		} else
			ctrl |= SCI_CTRL_FLAGS_TIE;
		ctrl_out(ctrl, SCSCR);
		restore_flags(flags);
		return;
	}

	while (1) {
		count = port->gs.xmit_cnt;
#if defined(CONFIG_SH_SCIF_SERIAL)
		txroom = 16 - (ctrl_inw(SCFDR)>>8);
#else
		txroom = (ctrl_in(SC_SR)&SCI_TD_E)?1:0;
#endif
		if (count > txroom)
			count = txroom;

		/* Don't copy pas the end of the source buffer */
		if (count > SERIAL_XMIT_SIZE - port->gs.xmit_tail)
                	count = SERIAL_XMIT_SIZE - port->gs.xmit_tail;

		/* If for one reason or another, we can't copy more data, we're done! */
		if (count == 0)
			break;

		for (i=0; i<count; i++) {
			c = port->gs.xmit_buf[port->gs.xmit_tail + i];
			ctrl_outb(c, SC_TDR);
		}
		ctrl_out(SCI_TD_E_CLEAR, SC_SR);

		/* Update the kernel buffer end */
		port->gs.xmit_tail = (port->gs.xmit_tail + count) & (SERIAL_XMIT_SIZE-1);

		/* This one last. (this is essential)
		   It would allow others to start putting more data into the buffer! */
		port->gs.xmit_cnt -= count;
	}

	if (port->gs.xmit_cnt <= port->gs.wakeup_chars) {
		if ((port->gs.tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    port->gs.tty->ldisc.write_wakeup)
			(port->gs.tty->ldisc.write_wakeup)(port->gs.tty);
		wake_up_interruptible(&port->gs.tty->write_wait);
	}

	save_and_cli(flags);
	ctrl = ctrl_in(SCSCR);
	if (port->gs.xmit_cnt == 0) {
		ctrl &= ~SCI_CTRL_FLAGS_TIE;
		port->gs.flags &= ~GS_TX_INTEN;
	} else {
#if defined(CONFIG_SH_SCIF_SERIAL)
		ctrl_in(SC_SR); /* Dummy read */
		ctrl_out(SCI_TD_E_CLEAR, SC_SR);
#endif
		ctrl |= SCI_CTRL_FLAGS_TIE;
	}
	ctrl_out(ctrl, SCSCR);
	restore_flags(flags);
}

static inline void sci_receive_chars(struct sci_port *port)
{
	int i, count;
	struct tty_struct *tty;
	int copied=0;
	unsigned short status;

	status = ctrl_in(SC_SR);
	if (!(status & SCI_RD_F))
		return;

	tty = port->gs.tty;
	while (1) {
#if defined(CONFIG_SH_SCIF_SERIAL)
		count = ctrl_inw(SCFDR)&0x001f;
#else
		count = (ctrl_in(SC_SR)&SCI_RD_F)?1:0;
#endif

		/* Don't copy more bytes than there is room for in the buffer */
		if (tty->flip.count + count > TTY_FLIPBUF_SIZE)
			count = TTY_FLIPBUF_SIZE - tty->flip.count;

		/* If for one reason or another, we can't copy more data, we're done! */
		if (count == 0)
			break;

		for (i=0; i<count; i++)
			tty->flip.char_buf_ptr[i] = ctrl_inb(SC_RDR);
		ctrl_in(SC_SR); /* dummy read */
		ctrl_out(SCI_RDRF_CLEAR, SC_SR);

		memset(tty->flip.flag_buf_ptr, TTY_NORMAL, count);

		/* Update the kernel buffer end */
		tty->flip.count += count;
		tty->flip.char_buf_ptr += count;
		tty->flip.flag_buf_ptr += count;

		copied += count;
	}

	if (copied)
		/* Tell the rest of the system the news. New characters! */
		tty_flip_buffer_push(tty);
}

static void sci_rx_interrupt(int irq, void *ptr, struct pt_regs *regs)
{
	struct sci_port *port = ptr;
	unsigned long flags;

	if (port->gs.flags & GS_ACTIVE)
		if (!(port->gs.flags & SCI_RX_THROTTLE)) {
			sci_receive_chars(port);
			return;
		}
	save_and_cli(flags);
	ctrl_out(ctrl_in(SCSCR) & ~SCI_CTRL_FLAGS_RIE, SCSCR);
	restore_flags(flags);
}

static void sci_tx_interrupt(int irq, void *ptr, struct pt_regs *regs)
{
	struct sci_port *port = ptr;

	if (port->gs.flags & GS_ACTIVE)
		sci_transmit_chars(port);
	else {
		unsigned long flags;

		save_and_cli(flags);
		ctrl_out(ctrl_in(SCSCR) & ~SCI_CTRL_FLAGS_TIE, SCSCR);
		restore_flags(flags);
	}
}

static void sci_er_interrupt(int irq, void *ptr, struct pt_regs *regs)
{
	/* Handle errors */
	if (ctrl_in(SC_SR) & SCI_ERRORS)
		ctrl_out(SCI_ERROR_CLEAR, SC_SR);

	/* Kick the transmission */
	sci_tx_interrupt(irq, ptr, regs);
}

/* ********************************************************************** *
 *                Here are the routines that actually                     *
 *              interface with the generic_serial driver                  *
 * ********************************************************************** */

static void sci_disable_tx_interrupts(void *ptr)
{
	unsigned long flags;
	unsigned short ctrl;

	/* Clear TIE (Transmit Interrupt Enable) bit in SCSCR */
	save_and_cli(flags);
	ctrl = ctrl_in(SCSCR);
	ctrl &= ~SCI_CTRL_FLAGS_TIE;
	ctrl_out(ctrl, SCSCR);
	restore_flags(flags);
}

static void sci_enable_tx_interrupts(void *ptr)
{
	struct sci_port *port = ptr; 

	disable_irq(SCI_TXI_IRQ);
	sci_transmit_chars(port);
	enable_irq(SCI_TXI_IRQ);
}

static void sci_disable_rx_interrupts(void * ptr)
{
	unsigned long flags;
	unsigned short ctrl;

	/* Clear RIE (Receive Interrupt Enable) bit in SCSCR */
	save_and_cli(flags);
	ctrl = ctrl_in(SCSCR);
	ctrl &= ~SCI_CTRL_FLAGS_RIE;
	ctrl_out(ctrl, SCSCR);
	restore_flags(flags);
}

static void sci_enable_rx_interrupts(void * ptr)
{
	unsigned long flags;
	unsigned short ctrl;

	/* Set RIE (Receive Interrupt Enable) bit in SCSCR */
	save_and_cli(flags);
	ctrl = ctrl_in(SCSCR);
	ctrl |= SCI_CTRL_FLAGS_RIE;
	ctrl_out(ctrl, SCSCR);
	restore_flags(flags);
}

static int sci_get_CD(void * ptr)
{
	/* If you have signal for CD (Carrier Detect), please change here. */
	return 1;
}

static int sci_chars_in_buffer(void * ptr)
{
#if defined(CONFIG_SH_SCIF_SERIAL)
	return (ctrl_inw(SCFDR) >> 8) + ((ctrl_in(SC_SR) & SCI_TEND)? 0: 1);
#else
	return (ctrl_in(SC_SR) & SCI_TEND)? 0: 1;
#endif
}

static void sci_shutdown_port(void * ptr)
{
	struct sci_port *port = ptr; 

	port->gs.flags &= ~ GS_ACTIVE;
	if (port->gs.tty && port->gs.tty->termios->c_cflag & HUPCL)
		sci_setsignals(port, 0, 0);
}

/* ********************************************************************** *
 *                Here are the routines that actually                     *
 *               interface with the rest of the system                    *
 * ********************************************************************** */

static int sci_open(struct tty_struct * tty, struct file * filp)
{
	struct sci_port *port;
	int retval, line;

	line = MINOR(tty->device) - SCI_MINOR_START;

	if ((line < 0) || (line >= SCI_NPORTS))
		return -ENODEV;

	port = &sci_ports[line];

	tty->driver_data = port;
	port->gs.tty = tty;
	port->gs.count++;

	/*
	 * Start up serial port
	 */
	retval = gs_init_port(&port->gs);
	if (retval) {
		port->gs.count--;
		return retval;
	}

	port->gs.flags |= GS_ACTIVE;
	sci_setsignals(port, 1,1);

	if (port->gs.count == 1) {
		MOD_INC_USE_COUNT;
	}

	retval = block_til_ready(port, filp);

	if (retval) {
		MOD_DEC_USE_COUNT;
		port->gs.count--;
		return retval;
	}

	if ((port->gs.count == 1) && (port->gs.flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = port->gs.normal_termios;
		else 
			*tty->termios = port->gs.callout_termios;
		sci_set_real_termios(port);
	}

	sci_enable_rx_interrupts(port);

	port->gs.session = current->session;
	port->gs.pgrp = current->pgrp;

	return 0;
}

static void sci_hungup(void *ptr)
{
	MOD_DEC_USE_COUNT;
}

static void sci_close(void *ptr)
{
	MOD_DEC_USE_COUNT;
}

static int sci_ioctl(struct tty_struct * tty, struct file * filp, 
                     unsigned int cmd, unsigned long arg)
{
	int rc;
	struct sci_port *port = tty->driver_data;
	int ival;

	rc = 0;
	switch (cmd) {
	case TIOCGSOFTCAR:
		rc = put_user(((tty->termios->c_cflag & CLOCAL) ? 1 : 0),
		              (unsigned int *) arg);
		break;
	case TIOCSSOFTCAR:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		                      sizeof(int))) == 0) {
			get_user(ival, (unsigned int *) arg);
			tty->termios->c_cflag =
				(tty->termios->c_cflag & ~CLOCAL) |
				(ival ? CLOCAL : 0);
		}
		break;
	case TIOCGSERIAL:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		                      sizeof(struct serial_struct))) == 0)
			gs_getserial(&port->gs, (struct serial_struct *) arg);
		break;
	case TIOCSSERIAL:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		                      sizeof(struct serial_struct))) == 0)
			rc = gs_setserial(&port->gs,
					  (struct serial_struct *) arg);
		break;
	case TIOCMGET:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		                      sizeof(unsigned int))) == 0) {
			ival = sci_getsignals(port);
			put_user(ival, (unsigned int *) arg);
		}
		break;
	case TIOCMBIS:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		                      sizeof(unsigned int))) == 0) {
			get_user(ival, (unsigned int *) arg);
			sci_setsignals(port, ((ival & TIOCM_DTR) ? 1 : -1),
			                     ((ival & TIOCM_RTS) ? 1 : -1));
		}
		break;
	case TIOCMBIC:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		                      sizeof(unsigned int))) == 0) {
			get_user(ival, (unsigned int *) arg);
			sci_setsignals(port, ((ival & TIOCM_DTR) ? 0 : -1),
			                     ((ival & TIOCM_RTS) ? 0 : -1));
		}
		break;
	case TIOCMSET:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		                      sizeof(unsigned int))) == 0) {
			get_user(ival, (unsigned int *)arg);
			sci_setsignals(port, ((ival & TIOCM_DTR) ? 1 : 0),
			                     ((ival & TIOCM_RTS) ? 1 : 0));
		}
		break;

	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	return rc;
}

static void sci_throttle(struct tty_struct * tty)
{
	struct sci_port *port = (struct sci_port *)tty->driver_data;

	/* If the port is using any type of input flow
	 * control then throttle the port.
	 */
	if ((tty->termios->c_cflag & CRTSCTS) || (I_IXOFF(tty)) )
		port->gs.flags |= SCI_RX_THROTTLE;
}

static void sci_unthrottle(struct tty_struct * tty)
{
	struct sci_port *port = (struct sci_port *)tty->driver_data;

	/* Always unthrottle even if flow control is not enabled on
	 * this port in case we disabled flow control while the port
	 * was throttled
	 */
	port->gs.flags &= ~SCI_RX_THROTTLE;
	return;
}

/* ********************************************************************** *
 *                    Here are the initialization routines.               *
 * ********************************************************************** */

static int sci_init_drivers(void)
{
	int error;
	struct sci_port *port;

	memset(&sci_driver, 0, sizeof(sci_driver));
	sci_driver.magic = TTY_DRIVER_MAGIC;
	sci_driver.driver_name = "serial";
	sci_driver.name = "ttyS";
	sci_driver.major = TTY_MAJOR;
	sci_driver.minor_start = SCI_MINOR_START;
	sci_driver.num = 1;
	sci_driver.type = TTY_DRIVER_TYPE_SERIAL;
	sci_driver.subtype = SERIAL_TYPE_NORMAL;
	sci_driver.init_termios = tty_std_termios;
	sci_driver.init_termios.c_cflag =
		B115200 | CS8 | CREAD | HUPCL | CLOCAL | CRTSCTS;
	sci_driver.flags = TTY_DRIVER_REAL_RAW;
	sci_driver.refcount = &sci_refcount;
	sci_driver.table = sci_table;
	sci_driver.termios = &sci_termios[0];
	sci_driver.termios_locked = &sci_termios[1];
	sci_termios[0] = sci_termios[1] = NULL;

	sci_driver.open	= sci_open;
	sci_driver.close = gs_close;
	sci_driver.write = gs_write;
	sci_driver.put_char = gs_put_char;
	sci_driver.flush_chars = gs_flush_chars;
	sci_driver.write_room = gs_write_room;
	sci_driver.chars_in_buffer = gs_chars_in_buffer;
	sci_driver.flush_buffer = gs_flush_buffer;
	sci_driver.ioctl = sci_ioctl;
	sci_driver.throttle = sci_throttle;
	sci_driver.unthrottle = sci_unthrottle;
	sci_driver.set_termios = gs_set_termios;
	sci_driver.stop = gs_stop;
	sci_driver.start = gs_start;
	sci_driver.hangup = gs_hangup;

	sci_callout_driver = sci_driver;
	sci_callout_driver.name = "cua";
	sci_callout_driver.major = TTYAUX_MAJOR;
	sci_callout_driver.subtype = SERIAL_TYPE_CALLOUT;

	if ((error = tty_register_driver(&sci_driver))) {
		printk(KERN_ERR "sci: Couldn't register SCI driver, error = %d\n",
		       error);
		return 1;
	}
	if ((error = tty_register_driver(&sci_callout_driver))) {
		tty_unregister_driver(&sci_driver);
		printk(KERN_ERR "sci: Couldn't register SCI callout driver, error = %d\n",
		       error);
		return 1;
	}

	port = &sci_ports[0];
	port->gs.callout_termios = tty_std_termios;
	port->gs.normal_termios	= tty_std_termios;
	port->gs.magic = SCI_MAGIC;
	port->gs.close_delay = HZ/2;
	port->gs.closing_wait = 30 * HZ;
	port->gs.rd = &sci_real_driver;
	init_waitqueue_head(&port->gs.open_wait);
	init_waitqueue_head(&port->gs.close_wait);
	port->old_cflag = 0;

	return 0;
}

int __init sci_init(void)
{
	struct sci_port *port;
	int i;

	for (i=SCI_ERI_IRQ; i<SCI_IRQ_END; i++)
		set_ipr_data(i, SCI_IPR_ADDR, SCI_IPR_POS, SCI_PRIORITY);

	port = &sci_ports[0];

	if (request_irq(SCI_ERI_IRQ, sci_er_interrupt, SA_INTERRUPT,
			"serial", port)) {
		printk(KERN_ERR "sci: Cannot allocate error irq.\n");
		return -ENODEV;
	}
	if (request_irq(SCI_RXI_IRQ, sci_rx_interrupt, SA_INTERRUPT,
			"serial", port)) {
		printk(KERN_ERR "sci: Cannot allocate rx irq.\n");
		return -ENODEV;
	}
	if (request_irq(SCI_TXI_IRQ, sci_tx_interrupt, SA_INTERRUPT,
			"serial", port)) {
		printk(KERN_ERR "sci: Cannot allocate tx irq.\n");
		return -ENODEV;
	}
	/* XXX: How about BRI interrupt?? */

	sci_init_drivers();

#ifdef CONFIG_DEBUG_KERNEL_WITH_GDB_STUB
	gdb_detach();
#endif
	return 0;		/* Return -EIO when not detected */
}

module_init(sci_init);

#ifdef MODULE
#undef func_enter
#undef func_exit

void cleanup_module(void)
{
	int i;

	for (i=SCI_ERI_IRQ; i<SCI_TEI_IRQ; i++) /* XXX: irq_end?? */
		free_irq(i, port);

	tty_unregister_driver(&sci_driver);
	tty_unregister_driver(&sci_callout_driver);
}

#include "generic_serial.c"
#endif

#ifdef CONFIG_SERIAL_CONSOLE
/*
 * ------------------------------------------------------------
 * Serial console driver for SH-3/SH-4 SCI (with no FIFO)
 * ------------------------------------------------------------
 */

static inline void put_char(char c)
{
	unsigned long flags;
	unsigned short status;

	save_and_cli(flags);

	do
		status = ctrl_in(SC_SR);
	while (!(status & SCI_TD_E));

	ctrl_outb(c, SC_TDR);
	ctrl_in(SC_SR);		/* Dummy read */
	ctrl_out(SCI_TD_E_CLEAR, SC_SR);

	restore_flags(flags);
}

#ifdef CONFIG_DEBUG_KERNEL_WITH_GDB_STUB
static int in_gdb = 1;

static inline void handle_error(void)
{				/* Clear error flags */
	ctrl_out(SCI_ERROR_CLEAR, SC_SR);
}

static inline int get_char(void)
{
	unsigned long flags;
	unsigned short status;
	int c;

	save_and_cli(flags);
	do {
		status = ctrl_in(SC_SR);
		if (status & SCI_ERRORS) {
			handle_error();
			continue;
		}
	} while (!(status & SCI_RD_F));
	c = ctrl_inb(SC_RDR);
	ctrl_in(SC_SR);		/* Dummy read */
	ctrl_out(SCI_RDRF_CLEAR, SC_SR);
	restore_flags(flags);

	return c;
}

/* Taken from sh-stub.c of GDB 4.18 */
static const char hexchars[] = "0123456789abcdef";
static char highhex(int  x)
{
	return hexchars[(x >> 4) & 0xf];
}

static char lowhex(int  x)
{
	return hexchars[x & 0xf];
}

static void gdb_detach(void)
{
	asm volatile("trapa	#0xff");

	if (in_gdb == 1) {
		in_gdb = 0;
		get_char();
		put_char('\r');
		put_char('\n');
	}
}
#endif

/* send the packet in buffer.  The host get's one chance to read it.
   This routine does not wait for a positive acknowledge.  */

static void
put_string(const char *buffer, int count)
{
	int i;
	const unsigned char *p = buffer;
#ifdef CONFIG_DEBUG_KERNEL_WITH_GDB_STUB
	int checksum;

if (in_gdb) {
	/*  $<packet info>#<checksum>. */
	do {
		unsigned char c;
		put_char('$');
		put_char('O'); /* 'O'utput to console */
		checksum = 'O';

		for (i=0; i<count; i++) { /* Don't use run length encoding */
			int h, l;

			c = *p++;
			h = highhex(c);
			l = lowhex(c);
			put_char(h);
			put_char(l);
			checksum += h + l;
		}
		put_char('#');
		put_char(highhex(checksum));
		put_char(lowhex(checksum));
	} while  (get_char() != '+');
} else
#endif
	for (i=0; i<count; i++) {
		if (*p == 10)
			put_char('\r');
		put_char(*p++);
	}
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 */
static void serial_console_write(struct console *co, const char *s,
				 unsigned count)
{
	put_string(s, count);
}

/*
 *	Receive character from the serial port
 */
static int serial_console_wait_key(struct console *co)
{
	/* Not implemented yet */
	return 0;
}

static kdev_t serial_console_device(struct console *c)
{
	return MKDEV(TTY_MAJOR, SCI_MINOR_START + c->index);
}

/*
 *	Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first rs_open()
 *	- initialize the serial port
 *	Return non-zero if we didn't find a serial port.
 */
static int __init serial_console_setup(struct console *co, char *options)
{
	int	baud = 115200;
	int	bits = 8;
	int	parity = 'n';
	int	cflag = CREAD | HUPCL | CLOCAL;
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
	switch (baud) {
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
	switch (bits) {
		case 7:
			cflag |= CS7;
			break;
		default:
		case 8:
			cflag |= CS8;
			break;
	}
	switch (parity) {
		case 'o': case 'O':
			cflag |= PARODD;
			break;
		case 'e': case 'E':
			cflag |= PARENB;
			break;
	}
	co->cflag = cflag;

	/* XXX: set baud, char, and parity here. */
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

void __init serial_console_init(void)
{
	register_console(&sercons);
}
#endif /* CONFIG_SERIAL_CONSOLE */

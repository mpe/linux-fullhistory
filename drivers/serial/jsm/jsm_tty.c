/************************************************************************
 * Copyright 2003 Digi International (www.digi.com)
 *
 * Copyright (C) 2004 IBM Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 * Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * Contact Information:
 * Scott H Kilau <Scott_Kilau@digi.com>
 * Wendy Xiong   <wendyx@us.ltcfwd.linux.ibm.com>
 *
 ***********************************************************************/
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/delay.h>	/* For udelay */
#include <linux/pci.h>

#include "jsm.h"

static inline int jsm_get_mstat(struct jsm_channel *ch)
{
	unsigned char mstat;
	unsigned result;

	jsm_printk(IOCTL, INFO, &ch->ch_bd->pci_dev, "start\n");

	mstat = (ch->ch_mostat | ch->ch_mistat);

	result = 0;

	if (mstat & UART_MCR_DTR)
		result |= TIOCM_DTR;
	if (mstat & UART_MCR_RTS)
		result |= TIOCM_RTS;
	if (mstat & UART_MSR_CTS)
		result |= TIOCM_CTS;
	if (mstat & UART_MSR_DSR)
		result |= TIOCM_DSR;
	if (mstat & UART_MSR_RI)
		result |= TIOCM_RI;
	if (mstat & UART_MSR_DCD)
		result |= TIOCM_CD;

	jsm_printk(IOCTL, INFO, &ch->ch_bd->pci_dev, "finish\n");
	return result;
}

static unsigned int jsm_tty_tx_empty(struct uart_port *port)
{
	return TIOCSER_TEMT;
}

/*
 * Return modem signals to ld.
 */
static unsigned int jsm_tty_get_mctrl(struct uart_port *port)
{
	int result;
	struct jsm_channel *channel = (struct jsm_channel *)port;

	jsm_printk(IOCTL, INFO, &channel->ch_bd->pci_dev, "start\n");

	result = jsm_get_mstat(channel);

	if (result < 0)
		return -ENXIO;

	jsm_printk(IOCTL, INFO, &channel->ch_bd->pci_dev, "finish\n");

	return result;
}

/*
 * jsm_set_modem_info()
 *
 * Set modem signals, called by ld.
 */
static void jsm_tty_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct jsm_channel *channel = (struct jsm_channel *)port;

	jsm_printk(IOCTL, INFO, &channel->ch_bd->pci_dev, "start\n");

	if (mctrl & TIOCM_RTS)
		channel->ch_mostat |= UART_MCR_RTS;
	else
		channel->ch_mostat &= ~UART_MCR_RTS;

	if (mctrl & TIOCM_DTR)
		channel->ch_mostat |= UART_MCR_DTR;
	else
		channel->ch_mostat &= ~UART_MCR_DTR;

	channel->ch_bd->bd_ops->assert_modem_signals(channel);

	jsm_printk(IOCTL, INFO, &channel->ch_bd->pci_dev, "finish\n");
	udelay(10);
}

static void jsm_tty_start_tx(struct uart_port *port, unsigned int tty_start)
{
	struct jsm_channel *channel = (struct jsm_channel *)port;

	jsm_printk(IOCTL, INFO, &channel->ch_bd->pci_dev, "start\n");

	channel->ch_flags &= ~(CH_STOP);
	jsm_tty_write(port);

	jsm_printk(IOCTL, INFO, &channel->ch_bd->pci_dev, "finish\n");
}

static void jsm_tty_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	struct jsm_channel *channel = (struct jsm_channel *)port;

	jsm_printk(IOCTL, INFO, &channel->ch_bd->pci_dev, "start\n");

	channel->ch_flags |= (CH_STOP);

	jsm_printk(IOCTL, INFO, &channel->ch_bd->pci_dev, "finish\n");
}

static void jsm_tty_send_xchar(struct uart_port *port, char ch)
{
	unsigned long lock_flags;
	struct jsm_channel *channel = (struct jsm_channel *)port;

	spin_lock_irqsave(&port->lock, lock_flags);
	if (ch == port->info->tty->termios->c_cc[VSTART])
		channel->ch_bd->bd_ops->send_start_character(channel);

	if (ch == port->info->tty->termios->c_cc[VSTOP])
		channel->ch_bd->bd_ops->send_stop_character(channel);
	spin_unlock_irqrestore(&port->lock, lock_flags);
}

static void jsm_tty_stop_rx(struct uart_port *port)
{
	struct jsm_channel *channel = (struct jsm_channel *)port;

	channel->ch_bd->bd_ops->disable_receiver(channel);
}

static void jsm_tty_break(struct uart_port *port, int break_state)
{
	unsigned long lock_flags;
	struct jsm_channel *channel = (struct jsm_channel *)port;

	spin_lock_irqsave(&port->lock, lock_flags);
	if (break_state == -1)
		channel->ch_bd->bd_ops->send_break(channel);
	else
		channel->ch_bd->bd_ops->clear_break(channel, 0);

	spin_unlock_irqrestore(&port->lock, lock_flags);
}

static int jsm_tty_open(struct uart_port *port)
{
	struct jsm_board *brd;
	int rc = 0;
	struct jsm_channel *channel = (struct jsm_channel *)port;

	/* Get board pointer from our array of majors we have allocated */
	brd = channel->ch_bd;

	/*
	 * Allocate channel buffers for read/write/error.
	 * Set flag, so we don't get trounced on.
	 */
	channel->ch_flags |= (CH_OPENING);

	/* Drop locks, as malloc with GFP_KERNEL can sleep */

	if (!channel->ch_rqueue) {
		channel->ch_rqueue = (u8 *) kmalloc(RQUEUESIZE, GFP_KERNEL);
		if (!channel->ch_rqueue) {
			jsm_printk(INIT, ERR, &channel->ch_bd->pci_dev,
				"unable to allocate read queue buf");
			return -ENOMEM;
		}
		memset(channel->ch_rqueue, 0, RQUEUESIZE);
	}
	if (!channel->ch_equeue) {
		channel->ch_equeue = (u8 *) kmalloc(EQUEUESIZE, GFP_KERNEL);
		if (!channel->ch_equeue) {
			jsm_printk(INIT, ERR, &channel->ch_bd->pci_dev,
				"unable to allocate error queue buf");
			return -ENOMEM;
		}
		memset(channel->ch_equeue, 0, EQUEUESIZE);
	}
	if (!channel->ch_wqueue) {
		channel->ch_wqueue = (u8 *) kmalloc(WQUEUESIZE, GFP_KERNEL);
		if (!channel->ch_wqueue) {
			jsm_printk(INIT, ERR, &channel->ch_bd->pci_dev,
				"unable to allocate write queue buf");
			return -ENOMEM;
		}
		memset(channel->ch_wqueue, 0, WQUEUESIZE);
	}

	channel->ch_flags &= ~(CH_OPENING);
	/*
	 * Initialize if neither terminal is open.
	 */
	jsm_printk(OPEN, INFO, &channel->ch_bd->pci_dev,
		"jsm_open: initializing channel in open...\n");

	/*
	 * Flush input queues.
	 */
	channel->ch_r_head = channel->ch_r_tail = 0;
	channel->ch_e_head = channel->ch_e_tail = 0;
	channel->ch_w_head = channel->ch_w_tail = 0;

	brd->bd_ops->flush_uart_write(channel);
	brd->bd_ops->flush_uart_read(channel);

	channel->ch_flags = 0;
	channel->ch_cached_lsr = 0;
	channel->ch_stops_sent = 0;

	channel->ch_c_cflag	= port->info->tty->termios->c_cflag;
	channel->ch_c_iflag	= port->info->tty->termios->c_iflag;
	channel->ch_c_oflag	= port->info->tty->termios->c_oflag;
	channel->ch_c_lflag	= port->info->tty->termios->c_lflag;
	channel->ch_startc = port->info->tty->termios->c_cc[VSTART];
	channel->ch_stopc = port->info->tty->termios->c_cc[VSTOP];

	/* Tell UART to init itself */
	brd->bd_ops->uart_init(channel);

	/*
	 * Run param in case we changed anything
	 */
	brd->bd_ops->param(channel);

	jsm_carrier(channel);

	channel->ch_open_count++;

	jsm_printk(OPEN, INFO, &channel->ch_bd->pci_dev, "finish\n");
	return rc;
}

static void jsm_tty_close(struct uart_port *port)
{
	struct jsm_board *bd;
	struct termios *ts;
	struct jsm_channel *channel = (struct jsm_channel *)port;

	jsm_printk(CLOSE, INFO, &channel->ch_bd->pci_dev, "start\n");

	bd = channel->ch_bd;
	ts = channel->uart_port.info->tty->termios;

	channel->ch_flags &= ~(CH_STOPI);

	channel->ch_open_count--;

	/*
	 * If we have HUPCL set, lower DTR and RTS
	 */
	if (channel->ch_c_cflag & HUPCL) {
		jsm_printk(CLOSE, INFO, &channel->ch_bd->pci_dev,
			"Close. HUPCL set, dropping DTR/RTS\n");

		/* Drop RTS/DTR */
		channel->ch_mostat &= ~(UART_MCR_DTR | UART_MCR_RTS);
		bd->bd_ops->assert_modem_signals(channel);
	}

	channel->ch_old_baud = 0;

	/* Turn off UART interrupts for this port */
	channel->ch_bd->bd_ops->uart_off(channel);

	jsm_printk(CLOSE, INFO, &channel->ch_bd->pci_dev, "finish\n");
}

static void jsm_tty_set_termios(struct uart_port *port,
				 struct termios *termios,
				 struct termios *old_termios)
{
	unsigned long lock_flags;
	struct jsm_channel *channel = (struct jsm_channel *)port;

	spin_lock_irqsave(&port->lock, lock_flags);
	channel->ch_c_cflag	= termios->c_cflag;
	channel->ch_c_iflag	= termios->c_iflag;
	channel->ch_c_oflag	= termios->c_oflag;
	channel->ch_c_lflag	= termios->c_lflag;
	channel->ch_startc	= termios->c_cc[VSTART];
	channel->ch_stopc	= termios->c_cc[VSTOP];

	channel->ch_bd->bd_ops->param(channel);
	jsm_carrier(channel);
	spin_unlock_irqrestore(&port->lock, lock_flags);
}

static const char *jsm_tty_type(struct uart_port *port)
{
	return "jsm";
}

static void jsm_tty_release_port(struct uart_port *port)
{
}

static int jsm_tty_request_port(struct uart_port *port)
{
	return 0;
}

static void jsm_config_port(struct uart_port *port, int flags)
{
	port->type = PORT_JSM;
}

static struct uart_ops jsm_ops = {
	.tx_empty	= jsm_tty_tx_empty,
	.set_mctrl	= jsm_tty_set_mctrl,
	.get_mctrl	= jsm_tty_get_mctrl,
	.stop_tx	= jsm_tty_stop_tx,
	.start_tx	= jsm_tty_start_tx,
	.send_xchar	= jsm_tty_send_xchar,
	.stop_rx	= jsm_tty_stop_rx,
	.break_ctl	= jsm_tty_break,
	.startup	= jsm_tty_open,
	.shutdown	= jsm_tty_close,
	.set_termios	= jsm_tty_set_termios,
	.type		= jsm_tty_type,
	.release_port	= jsm_tty_release_port,
	.request_port	= jsm_tty_request_port,
	.config_port	= jsm_config_port,
};

/*
 * jsm_tty_init()
 *
 * Init the tty subsystem.  Called once per board after board has been
 * downloaded and init'ed.
 */
int jsm_tty_init(struct jsm_board *brd)
{
	int i;
	void __iomem *vaddr;
	struct jsm_channel *ch;

	if (!brd)
		return -ENXIO;

	jsm_printk(INIT, INFO, &brd->pci_dev, "start\n");

	/*
	 * Initialize board structure elements.
	 */

	brd->nasync = brd->maxports;

	/*
	 * Allocate channel memory that might not have been allocated
	 * when the driver was first loaded.
	 */
	for (i = 0; i < brd->nasync; i++) {
		if (!brd->channels[i]) {

			/*
			 * Okay to malloc with GFP_KERNEL, we are not at
			 * interrupt context, and there are no locks held.
			 */
			brd->channels[i] = kmalloc(sizeof(struct jsm_channel), GFP_KERNEL);
			if (!brd->channels[i]) {
				jsm_printk(CORE, ERR, &brd->pci_dev,
					"%s:%d Unable to allocate memory for channel struct\n",
							 __FILE__, __LINE__);
			}
			memset(brd->channels[i], 0, sizeof(struct jsm_channel));
		}
	}

	ch = brd->channels[0];
	vaddr = brd->re_map_membase;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++, ch = brd->channels[i]) {

		if (!brd->channels[i])
			continue;

		spin_lock_init(&ch->ch_lock);

		if (brd->bd_uart_offset == 0x200)
			ch->ch_neo_uart =  vaddr + (brd->bd_uart_offset * i);

		ch->ch_bd = brd;
		ch->ch_portnum = i;

		/* .25 second delay */
		ch->ch_close_delay = 250;

		init_waitqueue_head(&ch->ch_flags_wait);
	}

	jsm_printk(INIT, INFO, &brd->pci_dev, "finish\n");
	return 0;
}

int jsm_uart_port_init(struct jsm_board *brd)
{
	int i;
	struct jsm_channel *ch;

	if (!brd)
		return -ENXIO;

	jsm_printk(INIT, INFO, &brd->pci_dev, "start\n");

	/*
	 * Initialize board structure elements.
	 */

	brd->nasync = brd->maxports;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++, ch = brd->channels[i]) {

		if (!brd->channels[i])
			continue;

		brd->channels[i]->uart_port.irq = brd->irq;
		brd->channels[i]->uart_port.type = PORT_JSM;
		brd->channels[i]->uart_port.iotype = UPIO_MEM;
		brd->channels[i]->uart_port.membase = brd->re_map_membase;
		brd->channels[i]->uart_port.fifosize = 16;
		brd->channels[i]->uart_port.ops = &jsm_ops;
		brd->channels[i]->uart_port.line = brd->channels[i]->ch_portnum + brd->boardnum * 2;
		if (uart_add_one_port (&jsm_uart_driver, &brd->channels[i]->uart_port))
			printk(KERN_INFO "Added device failed\n");
		else
			printk(KERN_INFO "Added device \n");
	}

	jsm_printk(INIT, INFO, &brd->pci_dev, "finish\n");
	return 0;
}

int jsm_remove_uart_port(struct jsm_board *brd)
{
	int i;
	struct jsm_channel *ch;

	if (!brd)
		return -ENXIO;

	jsm_printk(INIT, INFO, &brd->pci_dev, "start\n");

	/*
	 * Initialize board structure elements.
	 */

	brd->nasync = brd->maxports;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++) {

		if (!brd->channels[i])
			continue;

		ch = brd->channels[i];

		uart_remove_one_port(&jsm_uart_driver, &brd->channels[i]->uart_port);
	}

	jsm_printk(INIT, INFO, &brd->pci_dev, "finish\n");
	return 0;
}

void jsm_input(struct jsm_channel *ch)
{
	struct jsm_board *bd;
	struct tty_struct *tp;
	u32 rmask;
	u16 head;
	u16 tail;
	int data_len;
	unsigned long lock_flags;
	int flip_len;
	int len = 0;
	int n = 0;
	char *buf = NULL;
	char *buf2 = NULL;
	int s = 0;
	int i = 0;

	jsm_printk(READ, INFO, &ch->ch_bd->pci_dev, "start\n");

	if (!ch)
		return;

	tp = ch->uart_port.info->tty;

	bd = ch->ch_bd;
	if(!bd)
		return;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	/*
	 *Figure the number of characters in the buffer.
	 *Exit immediately if none.
	 */

	rmask = RQUEUEMASK;

	head = ch->ch_r_head & rmask;
	tail = ch->ch_r_tail & rmask;

	data_len = (head - tail) & rmask;
	if (data_len == 0) {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		return;
	}

	jsm_printk(READ, INFO, &ch->ch_bd->pci_dev, "start\n");

	/*
	 *If the device is not open, or CREAD is off, flush
	 *input data and return immediately.
	 */
	if (!tp ||
		!(tp->termios->c_cflag & CREAD) ) {

		jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
			"input. dropping %d bytes on port %d...\n", data_len, ch->ch_portnum);
		ch->ch_r_head = tail;

		/* Force queue flow control to be released, if needed */
		jsm_check_queue_flow_control(ch);

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		return;
	}

	/*
	 * If we are throttled, simply don't read any data.
	 */
	if (ch->ch_flags & CH_STOPI) {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
			"Port %d throttled, not reading any data. head: %x tail: %x\n",
			ch->ch_portnum, head, tail);
		return;
	}

	jsm_printk(READ, INFO, &ch->ch_bd->pci_dev, "start 2\n");

	/*
	 * If the rxbuf is empty and we are not throttled, put as much
	 * as we can directly into the linux TTY flip buffer.
	 * The jsm_rawreadok case takes advantage of carnal knowledge that
	 * the char_buf and the flag_buf are next to each other and
	 * are each of (2 * TTY_FLIPBUF_SIZE) size.
	 *
	 * NOTE: if(!tty->real_raw), the call to ldisc.receive_buf
	 *actually still uses the flag buffer, so you can't
	 *use it for input data
	 */
	if (jsm_rawreadok) {
		if (tp->real_raw)
			flip_len = MYFLIPLEN;
		else
			flip_len = 2 * TTY_FLIPBUF_SIZE;
	} else
		flip_len = TTY_FLIPBUF_SIZE - tp->flip.count;

	len = min(data_len, flip_len);
	len = min(len, (N_TTY_BUF_SIZE - 1) - tp->read_cnt);

	if (len <= 0) {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		jsm_printk(READ, INFO, &ch->ch_bd->pci_dev, "jsm_input 1\n");
		return;
	}

	/*
	 * If we're bypassing flip buffers on rx, we can blast it
	 * right into the beginning of the buffer.
	 */
	if (jsm_rawreadok) {
		if (tp->real_raw) {
			if (ch->ch_flags & CH_FLIPBUF_IN_USE) {
				jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
					"JSM - FLIPBUF in use. delaying input\n");
				spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
				return;
			}
			ch->ch_flags |= CH_FLIPBUF_IN_USE;
			buf = ch->ch_bd->flipbuf;
			buf2 = NULL;
		} else {
			buf = tp->flip.char_buf;
			buf2 = tp->flip.flag_buf;
		}
	} else {
		buf = tp->flip.char_buf_ptr;
		buf2 = tp->flip.flag_buf_ptr;
	}

	n = len;

	/*
	 * n now contains the most amount of data we can copy,
	 * bounded either by the flip buffer size or the amount
	 * of data the card actually has pending...
	 */
	while (n) {
		s = ((head >= tail) ? head : RQUEUESIZE) - tail;
		s = min(s, n);

		if (s <= 0)
			break;

		memcpy(buf, ch->ch_rqueue + tail, s);

		/* buf2 is only set when port isn't raw */
		if (buf2)
			memcpy(buf2, ch->ch_equeue + tail, s);

		tail += s;
		buf += s;
		if (buf2)
			buf2 += s;
		n -= s;
		/* Flip queue if needed */
		tail &= rmask;
	}

	/*
	 * In high performance mode, we don't have to update
	 * flag_buf or any of the counts or pointers into flip buf.
	 */
	if (!jsm_rawreadok) {
		if (I_PARMRK(tp) || I_BRKINT(tp) || I_INPCK(tp)) {
			for (i = 0; i < len; i++) {
				/*
				 * Give the Linux ld the flags in the
				 * format it likes.
				 */
				if (tp->flip.flag_buf_ptr[i] & UART_LSR_BI)
					tp->flip.flag_buf_ptr[i] = TTY_BREAK;
				else if (tp->flip.flag_buf_ptr[i] & UART_LSR_PE)
					tp->flip.flag_buf_ptr[i] = TTY_PARITY;
				else if (tp->flip.flag_buf_ptr[i] & UART_LSR_FE)
					tp->flip.flag_buf_ptr[i] = TTY_FRAME;
				else
					tp->flip.flag_buf_ptr[i] = TTY_NORMAL;
			}
		} else {
			memset(tp->flip.flag_buf_ptr, 0, len);
		}

		tp->flip.char_buf_ptr += len;
		tp->flip.flag_buf_ptr += len;
		tp->flip.count += len;
	}
	else if (!tp->real_raw) {
		if (I_PARMRK(tp) || I_BRKINT(tp) || I_INPCK(tp)) {
			for (i = 0; i < len; i++) {
				/*
				 * Give the Linux ld the flags in the
				 * format it likes.
				 */
				if (tp->flip.flag_buf_ptr[i] & UART_LSR_BI)
					tp->flip.flag_buf_ptr[i] = TTY_BREAK;
				else if (tp->flip.flag_buf_ptr[i] & UART_LSR_PE)
					tp->flip.flag_buf_ptr[i] = TTY_PARITY;
				else if (tp->flip.flag_buf_ptr[i] & UART_LSR_FE)
					tp->flip.flag_buf_ptr[i] = TTY_FRAME;
				else
					tp->flip.flag_buf_ptr[i] = TTY_NORMAL;
			}
		} else
			memset(tp->flip.flag_buf, 0, len);
	}

	/*
	 * If we're doing raw reads, jam it right into the
	 * line disc bypassing the flip buffers.
	 */
	if (jsm_rawreadok) {
		if (tp->real_raw) {
			ch->ch_r_tail = tail & rmask;
			ch->ch_e_tail = tail & rmask;

			jsm_check_queue_flow_control(ch);

			/* !!! WE *MUST* LET GO OF ALL LOCKS BEFORE CALLING RECEIVE BUF !!! */

			spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

			jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
				"jsm_input. %d real_raw len:%d calling receive_buf for board %d\n",
				__LINE__, len, ch->ch_bd->boardnum);
			tp->ldisc.receive_buf(tp, ch->ch_bd->flipbuf, NULL, len);

			/* Allow use of channel flip buffer again */
			spin_lock_irqsave(&ch->ch_lock, lock_flags);
			ch->ch_flags &= ~CH_FLIPBUF_IN_USE;
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

		} else {
			ch->ch_r_tail = tail & rmask;
			ch->ch_e_tail = tail & rmask;

			jsm_check_queue_flow_control(ch);

			/* !!! WE *MUST* LET GO OF ALL LOCKS BEFORE CALLING RECEIVE BUF !!! */
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

			jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
				"jsm_input. %d not real_raw len:%d calling receive_buf for board %d\n",
				__LINE__, len, ch->ch_bd->boardnum);

			tp->ldisc.receive_buf(tp, tp->flip.char_buf, tp->flip.flag_buf, len);
		}
	} else {
		ch->ch_r_tail = tail & rmask;
		ch->ch_e_tail = tail & rmask;

		jsm_check_queue_flow_control(ch);

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

		jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
			"jsm_input. %d not jsm_read raw okay scheduling flip\n", __LINE__);
		tty_schedule_flip(tp);
	}

	jsm_printk(IOCTL, INFO, &ch->ch_bd->pci_dev, "finish\n");
}

void jsm_carrier(struct jsm_channel *ch)
{
	struct jsm_board *bd;

	int virt_carrier = 0;
	int phys_carrier = 0;

	jsm_printk(CARR, INFO, &ch->ch_bd->pci_dev, "start\n");
	if (!ch)
		return;

	bd = ch->ch_bd;

	if (!bd)
		return;

	if (ch->ch_mistat & UART_MSR_DCD) {
		jsm_printk(CARR, INFO, &ch->ch_bd->pci_dev,
			"mistat: %x D_CD: %x\n", ch->ch_mistat, ch->ch_mistat & UART_MSR_DCD);
		phys_carrier = 1;
	}

	if (ch->ch_c_cflag & CLOCAL)
		virt_carrier = 1;

	jsm_printk(CARR, INFO, &ch->ch_bd->pci_dev,
		"DCD: physical: %d virt: %d\n", phys_carrier, virt_carrier);

	/*
	 * Test for a VIRTUAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_FCAR) == 0) && (virt_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

		jsm_printk(CARR, INFO, &ch->ch_bd->pci_dev,
			"carrier: virt DCD rose\n");

		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);
	}

	/*
	 * Test for a PHYSICAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_CD) == 0) && (phys_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

		jsm_printk(CARR, INFO, &ch->ch_bd->pci_dev,
			"carrier: physical DCD rose\n");

		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);
	}

	/*
	 *  Test for a PHYSICAL transition to low, so long as we aren't
	 *  currently ignoring physical transitions (which is what "virtual
	 *  carrier" indicates).
	 *
	 *  The transition of the virtual carrier to low really doesn't
	 *  matter... it really only means "ignore carrier state", not
	 *  "make pretend that carrier is there".
	 */
	if ((virt_carrier == 0) && ((ch->ch_flags & CH_CD) != 0)
			&& (phys_carrier == 0)) {
		/*
		 *	When carrier drops:
		 *
		 *	Drop carrier on all open units.
		 *
		 *	Flush queues, waking up any task waiting in the
		 *	line discipline.
		 *
		 *	Send a hangup to the control terminal.
		 *
		 *	Enable all select calls.
		 */
		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);
	}

	/*
	 *  Make sure that our cached values reflect the current reality.
	 */
	if (virt_carrier == 1)
		ch->ch_flags |= CH_FCAR;
	else
		ch->ch_flags &= ~CH_FCAR;

	if (phys_carrier == 1)
		ch->ch_flags |= CH_CD;
	else
		ch->ch_flags &= ~CH_CD;
}


void jsm_check_queue_flow_control(struct jsm_channel *ch)
{
	int qleft = 0;

	/* Store how much space we have left in the queue */
	if ((qleft = ch->ch_r_tail - ch->ch_r_head - 1) < 0)
		qleft += RQUEUEMASK + 1;

	/*
	 * Check to see if we should enforce flow control on our queue because
	 * the ld (or user) isn't reading data out of our queue fast enuf.
	 *
	 * NOTE: This is done based on what the current flow control of the
	 * port is set for.
	 *
	 * 1) HWFLOW (RTS) - Turn off the UART's Receive interrupt.
	 *	This will cause the UART's FIFO to back up, and force
	 *	the RTS signal to be dropped.
	 * 2) SWFLOW (IXOFF) - Keep trying to send a stop character to
	 *	the other side, in hopes it will stop sending data to us.
	 * 3) NONE - Nothing we can do.  We will simply drop any extra data
	 *	that gets sent into us when the queue fills up.
	 */
	if (qleft < 256) {
		/* HWFLOW */
		if (ch->ch_c_cflag & CRTSCTS) {
			if(!(ch->ch_flags & CH_RECEIVER_OFF)) {
				ch->ch_bd->bd_ops->disable_receiver(ch);
				ch->ch_flags |= (CH_RECEIVER_OFF);
				jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
					"Internal queue hit hilevel mark (%d)! Turning off interrupts.\n",
					qleft);
			}
		}
		/* SWFLOW */
		else if (ch->ch_c_iflag & IXOFF) {
			if (ch->ch_stops_sent <= MAX_STOPS_SENT) {
				ch->ch_bd->bd_ops->send_stop_character(ch);
				ch->ch_stops_sent++;
				jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
					"Sending stop char! Times sent: %x\n", ch->ch_stops_sent);
			}
		}
	}

	/*
	 * Check to see if we should unenforce flow control because
	 * ld (or user) finally read enuf data out of our queue.
	 *
	 * NOTE: This is done based on what the current flow control of the
	 * port is set for.
	 *
	 * 1) HWFLOW (RTS) - Turn back on the UART's Receive interrupt.
	 *	This will cause the UART's FIFO to raise RTS back up,
	 *	which will allow the other side to start sending data again.
	 * 2) SWFLOW (IXOFF) - Send a start character to
	 *	the other side, so it will start sending data to us again.
	 * 3) NONE - Do nothing. Since we didn't do anything to turn off the
	 *	other side, we don't need to do anything now.
	 */
	if (qleft > (RQUEUESIZE / 2)) {
		/* HWFLOW */
		if (ch->ch_c_cflag & CRTSCTS) {
			if (ch->ch_flags & CH_RECEIVER_OFF) {
				ch->ch_bd->bd_ops->enable_receiver(ch);
				ch->ch_flags &= ~(CH_RECEIVER_OFF);
				jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
					"Internal queue hit lowlevel mark (%d)! Turning on interrupts.\n",
					qleft);
			}
		}
		/* SWFLOW */
		else if (ch->ch_c_iflag & IXOFF && ch->ch_stops_sent) {
			ch->ch_stops_sent = 0;
			ch->ch_bd->bd_ops->send_start_character(ch);
			jsm_printk(READ, INFO, &ch->ch_bd->pci_dev, "Sending start char!\n");
		}
	}
}

/*
 * jsm_tty_write()
 *
 * Take data from the user or kernel and send it out to the FEP.
 * In here exists all the Transparent Print magic as well.
 */
int jsm_tty_write(struct uart_port *port)
{
	int bufcount = 0, n = 0;
	int data_count = 0,data_count1 =0;
	u16 head;
	u16 tail;
	u16 tmask;
	u32 remain;
	int temp_tail = port->info->xmit.tail;
	struct jsm_channel *channel = (struct jsm_channel *)port;

	tmask = WQUEUEMASK;
	head = (channel->ch_w_head) & tmask;
	tail = (channel->ch_w_tail) & tmask;

	if ((bufcount = tail - head - 1) < 0)
		bufcount += WQUEUESIZE;

	n = bufcount;

	n = min(n, 56);
	remain = WQUEUESIZE - head;

	data_count = 0;
	if (n >= remain) {
		n -= remain;
		while ((port->info->xmit.head != temp_tail) &&
		(data_count < remain)) {
			channel->ch_wqueue[head++] =
			port->info->xmit.buf[temp_tail];

			temp_tail++;
			temp_tail &= (UART_XMIT_SIZE - 1);
			data_count++;
		}
		if (data_count == remain) head = 0;
	}

	data_count1 = 0;
	if (n > 0) {
		remain = n;
		while ((port->info->xmit.head != temp_tail) &&
			(data_count1 < remain)) {
			channel->ch_wqueue[head++] =
				port->info->xmit.buf[temp_tail];

			temp_tail++;
			temp_tail &= (UART_XMIT_SIZE - 1);
			data_count1++;

		}
	}

	port->info->xmit.tail = temp_tail;

	data_count += data_count1;
	if (data_count) {
		head &= tmask;
		channel->ch_w_head = head;
	}

	if (data_count) {
		channel->ch_bd->bd_ops->copy_data_from_queue_to_uart(channel);
	}

	return data_count;
}

static ssize_t jsm_driver_version_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", JSM_VERSION);
}
static DRIVER_ATTR(version, S_IRUSR, jsm_driver_version_show, NULL);

static ssize_t jsm_driver_state_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", jsm_driver_state_text[jsm_driver_state]);
}
static DRIVER_ATTR(state, S_IRUSR, jsm_driver_state_show, NULL);

void jsm_create_driver_sysfiles(struct device_driver *driverfs)
{
	driver_create_file(driverfs, &driver_attr_version);
	driver_create_file(driverfs, &driver_attr_state);
}

void jsm_remove_driver_sysfiles(struct device_driver *driverfs)
{
	driver_remove_file(driverfs, &driver_attr_version);
	driver_remove_file(driverfs, &driver_attr_state);
}

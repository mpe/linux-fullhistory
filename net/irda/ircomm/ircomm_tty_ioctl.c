/*********************************************************************
 *                
 * Filename:      ircomm_tty_ioctl.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Jun 10 14:39:09 1999
 * Modified at:   Wed Aug 25 14:11:02 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#include <linux/init.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/serial.h>

#include <asm/segment.h>
#include <asm/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>

#include <net/irda/ircomm_core.h>
#include <net/irda/ircomm_param.h>
#include <net/irda/ircomm_tty_attach.h>
#include <net/irda/ircomm_tty.h>

#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

/*
 * Function ircomm_tty_change_speed (driver)
 *
 *    Change speed of the driver. If the remote device is a DCE, then this
 *    should make it change the speed of its serial port
 */
void ircomm_tty_change_speed(struct ircomm_tty_cb *self)
{
	unsigned cflag, cval;
	int baud;

	if (!self->tty || !self->tty->termios || !self->ircomm)
		return;

	cflag = self->tty->termios->c_cflag;

	/*  byte size and parity */
	switch (cflag & CSIZE) {
	case CS5: cval = IRCOMM_WSIZE_5; break;
	case CS6: cval = IRCOMM_WSIZE_6; break;
	case CS7: cval = IRCOMM_WSIZE_7; break;
	case CS8: cval = IRCOMM_WSIZE_8; break;
	default:  cval = IRCOMM_WSIZE_5; break;
	}
	if (cflag & CSTOPB)
		cval |= IRCOMM_2_STOP_BIT;
	
	if (cflag & PARENB)
		cval |= IRCOMM_PARITY_ENABLE;
	if (!(cflag & PARODD))
		cval |= IRCOMM_PARITY_EVEN;

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(self->tty);
	if (!baud)
		baud = 9600;	/* B0 transition handled in rs_set_termios */

	self->session.data_rate = baud;
	ircomm_param_request(self, IRCOMM_DATA_RATE, FALSE);
	
	/* CTS flow control flag and modem status interrupts */
	if (cflag & CRTSCTS) {
		self->flags |= ASYNC_CTS_FLOW;
		self->session.flow_control |= IRCOMM_RTS_CTS_IN;
	} else
		self->flags &= ~ASYNC_CTS_FLOW;
	
	if (cflag & CLOCAL)
		self->flags &= ~ASYNC_CHECK_CD;
	else
		self->flags |= ASYNC_CHECK_CD;
#if 0	
	/*
	 * Set up parity check flag
	 */

	if (I_INPCK(self->tty))
		driver->read_status_mask |= LSR_FE | LSR_PE;
	if (I_BRKINT(driver->tty) || I_PARMRK(driver->tty))
		driver->read_status_mask |= LSR_BI;
	
	/*
	 * Characters to ignore
	 */
	driver->ignore_status_mask = 0;
	if (I_IGNPAR(driver->tty))
		driver->ignore_status_mask |= LSR_PE | LSR_FE;

	if (I_IGNBRK(self->tty)) {
		self->ignore_status_mask |= LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore 
		 * overruns too. (For real raw support).
		 */
		if (I_IGNPAR(self->tty)) 
			self->ignore_status_mask |= LSR_OE;
	}
#endif
	self->session.data_format = cval;

	ircomm_param_request(self, IRCOMM_DATA_FORMAT, FALSE);
 	ircomm_param_request(self, IRCOMM_FLOW_CONTROL, TRUE);
}

/*
 * Function ircomm_tty_set_termios (tty, old_termios)
 *
 *    This routine allows the tty driver to be notified when device's
 *    termios settings have changed.  Note that a well-designed tty driver
 *    should be prepared to accept the case where old == NULL, and try to
 *    do something rational.
 */
void ircomm_tty_set_termios(struct tty_struct *tty, 
			    struct termios *old_termios)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	unsigned int cflag = tty->termios->c_cflag;
	unsigned long flags;

	if ((cflag == old_termios->c_cflag) && 
	    (RELEVANT_IFLAG(tty->termios->c_iflag) == 
	     RELEVANT_IFLAG(old_termios->c_iflag)))
	{
		return;
	}

	ircomm_tty_change_speed(self);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) &&
	    !(cflag & CBAUD)) {
		self->session.dte &= ~(IRCOMM_DTR|IRCOMM_RTS);
		ircomm_param_request(self, IRCOMM_DTE, TRUE);
	}
	
	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) &&
	    (cflag & CBAUD)) {
		self->session.dte |= IRCOMM_DTR;
		if (!(tty->termios->c_cflag & CRTSCTS) || 
		    !test_bit(TTY_THROTTLED, &tty->flags)) {
			self->session.dte |= IRCOMM_RTS;
		}
		ircomm_param_request(self, IRCOMM_DTE, TRUE);
	}
	
	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) 
	{
		tty->hw_stopped = 0;
		ircomm_tty_start(tty);
	}
}

/*
 * Function ircomm_tty_get_modem_info (self, value)
 *
 *    
 *
 */
static int ircomm_tty_get_modem_info(struct ircomm_tty_cb *self, 
				     unsigned int *value)
{
	unsigned int result;

	DEBUG(1, __FUNCTION__ "()\n");

	result =  ((self->session.dte & IRCOMM_RTS)       ? TIOCM_RTS : 0)
		| ((self->session.dte & IRCOMM_DTR)       ? TIOCM_DTR : 0)
		| ((self->session.dce & IRCOMM_DELTA_CD)  ? TIOCM_CAR : 0)
		| ((self->session.dce & IRCOMM_DELTA_RI)  ? TIOCM_RNG : 0)
		| ((self->session.dce & IRCOMM_DELTA_DSR) ? TIOCM_DSR : 0)
		| ((self->session.dce & IRCOMM_DELTA_CTS) ? TIOCM_CTS : 0);

	return put_user(result, value);
}

/*
 * Function set_modem_info (driver, cmd, value)
 *
 *    
 *
 */
static int ircomm_tty_set_modem_info(struct ircomm_tty_cb *self, 
				     unsigned int cmd, unsigned int *value)
{ 
	unsigned int arg;
	__u8 old_rts, old_dtr;
	int error;

	DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

	error = get_user(arg, value);
	if (error)
		return error;

	old_rts = self->session.dte & IRCOMM_RTS;
	old_dtr = self->session.dte & IRCOMM_DTR;

	switch (cmd) {
	case TIOCMBIS: 
		if (arg & TIOCM_RTS) 
			self->session.dte |= IRCOMM_RTS;
		if (arg & TIOCM_DTR)
			self->session.dte |= IRCOMM_DTR;
		break;
		
	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			self->session.dte &= ~IRCOMM_RTS;
		if (arg & TIOCM_DTR)
 			self->session.dte &= ~IRCOMM_DTR;
 		break;
		
	case TIOCMSET:
 		self->session.dte = 
			((self->session.dte & ~(IRCOMM_RTS | IRCOMM_DTR))
			 | ((arg & TIOCM_RTS) ? IRCOMM_RTS : 0)
			 | ((arg & TIOCM_DTR) ? IRCOMM_DTR : 0));
		break;
		
	default:
		return -EINVAL;
	}
	
	if ((self->session.dte & IRCOMM_RTS) != old_rts)
		self->session.dte |= IRCOMM_DELTA_RTS;

	if ((self->session.dte & IRCOMM_DTR) != old_dtr)
		self->session.dte |= IRCOMM_DELTA_DTR;

	ircomm_param_request(self, IRCOMM_DTE, TRUE);
	
	return 0;
}

/*
 * Function get_serial_info (driver, retinfo)
 *
 *    
 *
 */
static int ircomm_tty_get_serial_info(struct ircomm_tty_cb *self,
				      struct serial_struct *retinfo)
{
	struct serial_struct info;
   
	if (!retinfo)
		return -EFAULT;

	DEBUG(1, __FUNCTION__ "()\n");

	memset(&info, 0, sizeof(info));
	info.line = self->line;
	/* info.flags = self->flags; */
	info.baud_base = self->session.data_rate;
#if 0
	info.close_delay = driver->close_delay;
	info.closing_wait = driver->closing_wait;
#endif
	/* For compatibility  */
 	info.type = PORT_16550A;
 	info.port = 0;
 	info.irq = 0;
	info.xmit_fifo_size = 0;
	info.hub6 = 0;   
#if 0
	info.custom_divisor = driver->custom_divisor;
#endif
	if (copy_to_user(retinfo, &info, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

/*
 * Function set_serial_info (driver, new_info)
 *
 *    
 *
 */
static int ircomm_tty_set_serial_info(struct ircomm_tty_cb *tty,
				      struct serial_struct *new_info)
{
	struct serial_struct new_serial;
	struct ircomm_tty_cb old_driver;

	DEBUG(2, __FUNCTION__ "()\n");
#if 0

	if (copy_from_user(&new_serial,new_info,sizeof(new_serial)))
		return -EFAULT;

	old_driver = *driver;
  
	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.baud_base != driver->comm->data_rate) ||
		    (new_serial.close_delay != driver->close_delay) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (driver->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		driver->flags = ((driver->flags & ~ASYNC_USR_MASK) |
				 (new_serial.flags & ASYNC_USR_MASK));
		driver->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	if (self->session.data_rate != new_serial.baud_base) {
		self->session.data_rate.data_rate = new_serial.baud_base;
		if (driver->comm->state == IRCOMM_CONN)
			ircomm_control_request(driver->comm, DATA_RATE);
	}

	driver->close_delay = new_serial.close_delay * HZ/100;
	driver->closing_wait = new_serial.closing_wait * HZ/100;
	driver->custom_divisor = new_serial.custom_divisor;

	self->flags = ((self->flags & ~ASYNC_FLAGS) |
		       (new_serial.flags & ASYNC_FLAGS));
	self->tty->low_latency = (self->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

 check_and_exit:

	if (self->flags & ASYNC_INITIALIZED) {
		if (((old_driver.flags & ASYNC_SPD_MASK) !=
		     (self->flags & ASYNC_SPD_MASK)) ||
		    (old_driver.custom_divisor != driver->custom_divisor)) {
			if ((driver->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
				driver->tty->alt_speed = 57600;
			if ((driver->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
				driver->tty->alt_speed = 115200;
			if ((driver->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
				driver->tty->alt_speed = 230400;
			if ((driver->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
				driver->tty->alt_speed = 460800;
			ircomm_tty_change_speed(driver);
		}
	}
#endif
	return 0;
}

/*
 * Function ircomm_tty_ioctl (tty, file, cmd, arg)
 *
 *    
 *
 */
int ircomm_tty_ioctl(struct tty_struct *tty, struct file *file, 
		     unsigned int cmd, unsigned long arg)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	int ret = 0;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}

	switch (cmd) {
	case TIOCMGET:
		ret = ircomm_tty_get_modem_info(self, (unsigned int *) arg);
		break;
	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		ret = ircomm_tty_set_modem_info(self, cmd, (unsigned int *) arg);
		break;
	case TIOCGSERIAL:
		ret = ircomm_tty_get_serial_info(self, (struct serial_struct *) arg);
		break;
	case TIOCSSERIAL:
		ret = ircomm_tty_set_serial_info(self, (struct serial_struct *) arg);
		break;
	case TIOCMIWAIT:
		DEBUG(0, "(), TIOCMIWAIT, not impl!\n");
		break;

	case TIOCGICOUNT:
		DEBUG(0, __FUNCTION__ "(), TIOCGICOUNT not impl!\n");
#if 0
		save_flags(flags); cli();
		cnow = driver->icount;
		restore_flags(flags);
		p_cuser = (struct serial_icounter_struct *) arg;
		error = put_user(cnow.cts, &p_cuser->cts);
		if (error) return error;
		error = put_user(cnow.dsr, &p_cuser->dsr);
		if (error) return error;
		error = put_user(cnow.rng, &p_cuser->rng);
		if (error) return error;
		error = put_user(cnow.dcd, &p_cuser->dcd);
		if (error) return error;
		error = put_user(cnow.rx, &p_cuser->rx);
		if (error) return error;
		error = put_user(cnow.tx, &p_cuser->tx);
		if (error) return error;
		error = put_user(cnow.frame, &p_cuser->frame);
		if (error) return error;
		error = put_user(cnow.overrun, &p_cuser->overrun);
		if (error) return error;
		error = put_user(cnow.parity, &p_cuser->parity);
		if (error) return error;
		error = put_user(cnow.brk, &p_cuser->brk);
		if (error) return error;
		error = put_user(cnow.buf_overrun, &p_cuser->buf_overrun);
		if (error) return error;			
#endif		
		return 0;
	default:
		ret = -ENOIOCTLCMD;  /* ioctls which we must ignore */
	}
	return ret;
}




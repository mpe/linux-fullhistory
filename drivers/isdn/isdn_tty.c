/* $Id: isdn_tty.c,v 1.3 1996/02/11 02:12:32 fritz Exp fritz $
 *
 * Linux ISDN subsystem, tty functions and AT-command emulator (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: isdn_tty.c,v $
 * Revision 1.3  1996/02/11 02:12:32  fritz
 * Bugfixes according to similar fixes in standard serial.c of kernel.
 *
 * Revision 1.2  1996/01/22 05:12:25  fritz
 * replaced my_atoi by simple_strtoul
 *
 * Revision 1.1  1996/01/09 04:13:18  fritz
 * Initial revision
 *
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include "isdn_common.h"
#include "isdn_tty.h"

/* Prototypes */

static int  isdn_tty_edit_at(const char *, int, modem_info *, int);
static void isdn_tty_check_esc(const u_char *, u_char, int, int *, int *, int);
static void isdn_tty_modem_reset_regs(atemu *, int);

/* Leave this unchanged unless you know what you do! */
#define MODEM_PARANOIA_CHECK
#define MODEM_DO_RESTART

static char *isdn_ttyname_ttyI = "ttyI";
static char *isdn_ttyname_cui  = "cui";
char *isdn_tty_revision        = "$Revision: 1.3 $";

int isdn_tty_try_read(int i, u_char * buf, int len)
{
        int c;
        struct tty_struct *tty;

	if (i < 0)
		return 0;
	if (dev->mdm.online[i]) {
		if ((tty = dev->mdm.info[i].tty)) {
			if (dev->mdm.info[i].MCR & UART_MCR_RTS) {
				c = TTY_FLIPBUF_SIZE - tty->flip.count - 1;
				if (c >= len) {
					if (len > 1) {
						memcpy(tty->flip.char_buf_ptr, buf, len);
						tty->flip.count += len;
						memset(tty->flip.flag_buf_ptr, 0, len);
						if (dev->mdm.atmodem[i].mdmreg[12] & 128)
							tty->flip.flag_buf_ptr[len - 1] = 0xff;
						tty->flip.flag_buf_ptr += len;
						tty->flip.char_buf_ptr += len;
					} else
						tty_insert_flip_char(tty, buf[0], 0);
					queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
					return 1;
				}
			}
		}
	}
	return 0;
}

void isdn_tty_readmodem(void)
{
	int resched = 0;
	int midx;
	int i;
	int c;
	int r;
	ulong flags;
	struct tty_struct *tty;
	modem_info *info;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if ((midx = dev->m_idx[i]) >= 0)
			if (dev->mdm.online[midx]) {
				save_flags(flags);
				cli();
				r = 0;
				info = &dev->mdm.info[midx];
				if ((tty = info->tty)) {
					if (info->MCR & UART_MCR_RTS) {
						c = TTY_FLIPBUF_SIZE - tty->flip.count - 1;
						if (c > 0) {
							r = isdn_readbchan(info->isdn_driver, info->isdn_channel,
								      tty->flip.char_buf_ptr,
								      tty->flip.flag_buf_ptr, c, 0);
							if (!(dev->mdm.atmodem[midx].mdmreg[12] & 128))
								memset(tty->flip.flag_buf_ptr, 0, r);
							tty->flip.count += r;
							tty->flip.flag_buf_ptr += r;
							tty->flip.char_buf_ptr += r;
							if (r)
								queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
						}
					} else
						r = 1;
				} else
					r = 1;
				restore_flags(flags);
				if (r) {
					dev->mdm.rcvsched[midx] = 0;
					resched = 1;
				} else
					dev->mdm.rcvsched[midx] = 1;
			}
	}
	if (!resched)
		isdn_timer_ctrl(ISDN_TIMER_MODEMREAD, 0);
}

/************************************************************
 *
 * Modem-functions
 *
 * mostly "stolen" from original Linux-serial.c and friends.
 *
 ************************************************************/

static void isdn_tty_dial(char *n, modem_info * info, atemu * m)
{
	isdn_ctrl cmd;
	ulong flags;
	int i;

	save_flags(flags);
	cli();
	i = isdn_get_free_channel(ISDN_USAGE_MODEM, m->mdmreg[14], m->mdmreg[15], -1, -1);
	if (i < 0) {
		restore_flags(flags);
		isdn_tty_modem_result(6, info);
	} else {
		if (strlen(m->msn)) {
			info->isdn_driver = dev->drvmap[i];
			info->isdn_channel = dev->chanmap[i];
			info->drv_index = i;
			dev->m_idx[i] = info->line;
			dev->usage[i] |= ISDN_USAGE_OUTGOING;
			isdn_info_update();
			restore_flags(flags);
			cmd.driver = info->isdn_driver;
			cmd.arg = info->isdn_channel;
			cmd.command = ISDN_CMD_CLREAZ;
			dev->drv[info->isdn_driver]->interface->command(&cmd);
			strcpy(cmd.num, isdn_map_eaz2msn(m->msn, info->isdn_driver));
			cmd.driver = info->isdn_driver;
			cmd.command = ISDN_CMD_SETEAZ;
			dev->drv[info->isdn_driver]->interface->command(&cmd);
			cmd.driver = info->isdn_driver;
			cmd.command = ISDN_CMD_SETL2;
			cmd.arg = info->isdn_channel + (m->mdmreg[14] << 8);
			dev->drv[info->isdn_driver]->interface->command(&cmd);
			cmd.driver = info->isdn_driver;
			cmd.command = ISDN_CMD_SETL3;
			cmd.arg = info->isdn_channel + (m->mdmreg[15] << 8);
			dev->drv[info->isdn_driver]->interface->command(&cmd);
			cmd.driver = info->isdn_driver;
			cmd.arg = info->isdn_channel;
			sprintf(cmd.num, "%s,%s,%d,%d", n, isdn_map_eaz2msn(m->msn, info->isdn_driver),
				m->mdmreg[18], m->mdmreg[19]);
			cmd.command = ISDN_CMD_DIAL;
			dev->mdm.dialing[info->line] = 1;
			strcpy(dev->num[i], n);
			isdn_info_update();
			dev->drv[info->isdn_driver]->interface->command(&cmd);
		} else
			restore_flags(flags);
	}
}

void isdn_tty_modem_hup(modem_info * info)
{
	isdn_ctrl cmd;

	dev->mdm.rcvsched[info->line] = 0;
	dev->mdm.online[info->line] = 0;
	if (info->isdn_driver >= 0) {
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_HANGUP;
		cmd.arg = info->isdn_channel;
		dev->drv[info->isdn_driver]->interface->command(&cmd);
		isdn_all_eaz(info->isdn_driver, info->isdn_channel);
		isdn_free_channel(info->isdn_driver, info->isdn_channel, ISDN_USAGE_MODEM);
	}
	dev->m_idx[info->drv_index] = -1;
	info->isdn_driver = -1;
	info->isdn_channel = -1;
	info->drv_index = -1;
}

static inline int isdn_tty_paranoia_check(modem_info * info, dev_t device, const char *routine)
{
#ifdef MODEM_PARANOIA_CHECK
	if (!info) {
		printk(KERN_WARNING "isdn: null info_struct for (%d, %d) in %s\n",
		       MAJOR(device), MINOR(device), routine);
		return 1;
	}
	if (info->magic != ISDN_ASYNC_MAGIC) {
		printk(KERN_WARNING "isdn: bad magic for modem struct (%d, %d) in %s\n",
		       MAJOR(device), MINOR(device), routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void isdn_tty_change_speed(modem_info * info)
{
	uint cflag, cval, fcr, quot;
	int i;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;

	quot = i = cflag & CBAUD;
	if (i & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i < 1 || i > 2)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			i += 15;
	}
	if (quot) {
		info->MCR |= UART_MCR_DTR;
	} else {
		info->MCR &= ~UART_MCR_DTR;
		isdn_tty_modem_reset_regs(&dev->mdm.atmodem[info->line], 0);
		if (dev->mdm.online[info->line]) {
#ifdef ISDN_DEBUG_MODEM_HUP
			printk(KERN_DEBUG "Mhup in changespeed\n");
#endif
			isdn_tty_modem_hup(info);
			isdn_tty_modem_result(3, info);
		}
		return;
	}
	/* byte size and parity */
	cval = cflag & (CSIZE | CSTOPB);
	cval >>= 4;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
	fcr = 0;

	/* CTS flow control flag and modem status interrupts */
	if (cflag & CRTSCTS) {
		info->flags |= ISDN_ASYNC_CTS_FLOW;
	} else
		info->flags &= ~ISDN_ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ISDN_ASYNC_CHECK_CD;
	else {
		info->flags |= ISDN_ASYNC_CHECK_CD;
	}
}

static int isdn_tty_startup(modem_info * info)
{
	ulong flags;

	if (info->flags & ISDN_ASYNC_INITIALIZED)
		return 0;
	if (!info->type) {
		if (info->tty)
			set_bit(TTY_IO_ERROR, &info->tty->flags);
		return 0;
	}
	save_flags(flags);
	cli();

#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "starting up ttyi%d ...\n", info->line);
#endif
	/*
	 * Now, initialize the UART 
	 */
	info->MCR = UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2;
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	/*
	 * and set the speed of the serial port
	 */
	isdn_tty_change_speed(info);

	info->flags |= ISDN_ASYNC_INITIALIZED;
	dev->mdm.msr[info->line] |= UART_MSR_DSR;
#if FUTURE
	info->send_outstanding = 0;
#endif
	restore_flags(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void isdn_tty_shutdown(modem_info * info)
{
	ulong flags;

	if (!(info->flags & ISDN_ASYNC_INITIALIZED))
		return;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "Shutting down isdnmodem port %d ....\n", info->line);
#endif
	save_flags(flags);
	cli();			/* Disable interrupts */
	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
		info->MCR &= ~(UART_MCR_DTR | UART_MCR_RTS);
		isdn_tty_modem_reset_regs(&dev->mdm.atmodem[info->line], 0);
		if (dev->mdm.online[info->line]) {
#ifdef ISDN_DEBUG_MODEM_HUP
			printk(KERN_DEBUG "Mhup in isdn_tty_shutdown\n");
#endif
			isdn_tty_modem_hup(info);
		}
	}
	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~ISDN_ASYNC_INITIALIZED;
	restore_flags(flags);
}

static int isdn_tty_write(struct tty_struct *tty, int from_user, const u_char * buf, int count)
{
	int c, total = 0;
	modem_info *info = (modem_info *) tty->driver_data;
	ulong flags;
	int i;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_write"))
		return 0;
	if (!tty)
		return 0;
	save_flags(flags);
	cli();
	while (1) {
		c = MIN(count, info->xmit_size - info->xmit_count - 1);
		if (info->isdn_driver >= 0) {
#if 0
			if (info->isdn_driver != 0) {
				printk(KERN_DEBUG "FIDO: Zwei HW-Treiber geladen? Ansonsten ist was faul.\n");
				break;
			}
			int drvidx = info->isdn_driver;
			driver *driv = dev->drv[drvidx];
			i = driv->maxbufsize;
#else
			i = dev->drv[info->isdn_driver]->maxbufsize;
#endif
			c = MIN(c, i);
		}
		if (c <= 0)
			break;
		i = info->line;
		if (dev->mdm.online[i]) {
			isdn_tty_check_esc(buf, dev->mdm.atmodem[i].mdmreg[2], c,
				       &(dev->mdm.atmodem[i].pluscount),
			     &(dev->mdm.atmodem[i].lastplus), from_user);
			if (from_user)
				memcpy_fromfs(&(info->xmit_buf[info->xmit_count]), buf, c);
			else
				memcpy(&(info->xmit_buf[info->xmit_count]), buf, c);
			info->xmit_count += c;
			if (dev->mdm.atmodem[i].mdmreg[13] & 1) {
				char *bufptr;
				int buflen;
#if 0
				printk(KERN_DEBUG "WB1: %d\n", info->xmit_count);
#endif
				bufptr = info->xmit_buf;
				buflen = info->xmit_count;
				if (dev->mdm.atmodem[i].mdmreg[13] & 2) {
					/* Add T.70 simplified header */

#ifdef ISDN_DEBUG_MODEM_DUMP
					isdn_dumppkt("T70pack1:", bufptr, buflen, 40);
#endif
					bufptr -= 4;
					buflen += 4;
					memcpy(bufptr, "\1\0\1\0", 4);
#ifdef ISDN_DEBUG_MODEM_DUMP
					isdn_dumppkt("T70pack2:", bufptr, buflen, 40);
#endif
				}
				if (dev->drv[info->isdn_driver]->interface->
				    writebuf(info->isdn_driver, info->isdn_channel, bufptr, buflen, 0) > 0) {
					info->xmit_count = 0;
					info->xmit_size = dev->mdm.atmodem[i].mdmreg[16] * 16;
#if FUTURE
					info->send_outstanding++;
					dev->mdm.msr[i] &= ~UART_MSR_CTS;
#endif
					if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
					    tty->ldisc.write_wakeup)
						(tty->ldisc.write_wakeup) (tty);
					wake_up_interruptible(&tty->write_wait);
				}
			}
		} else {
			if (dev->mdm.dialing[i]) {
				dev->mdm.dialing[i] = 0;
#ifdef ISDN_DEBUG_MODEM_HUP
				printk(KERN_DEBUG "Mhup in isdn_tty_write\n");
#endif
				isdn_tty_modem_hup(info);
				isdn_tty_modem_result(3, info);
			} else
				c = isdn_tty_edit_at(buf, c, info, from_user);
		}
		buf += c;
		count -= c;
		total += c;
	}
	if (info->xmit_count)
		isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, 1);
	restore_flags(flags);
	return total;
}

static int isdn_tty_write_room(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;
	int ret;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_write_room"))
		return 0;
	if (!dev->mdm.online[info->line])
		return info->xmit_size - 1;
	ret = info->xmit_size - info->xmit_count - 1;
	return (ret < 0) ? 0 : ret;
}

static int isdn_tty_chars_in_buffer(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_chars_in_buffer"))
		return 0;
	if (!dev->mdm.online[info->line])
		return 0;
	return (info->xmit_count);
}

static void isdn_tty_flush_buffer(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;
	uint flags;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_flush_buffer"))
		return;
	save_flags(flags);
	cli();
	info->xmit_count = 0;
	restore_flags(flags);
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup) (tty);
}

static void isdn_tty_flush_chars(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_flush_chars"))
		return;
	if (info->xmit_count > 0)
		isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, 1);
}

/*
 * ------------------------------------------------------------
 * isdn_tty_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void isdn_tty_throttle(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_throttle"))
		return;
	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);
	info->MCR &= ~UART_MCR_RTS;
}

static void isdn_tty_unthrottle(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_unthrottle"))
		return;
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}
	info->MCR |= UART_MCR_RTS;
}

/*
 * ------------------------------------------------------------
 * isdn_tty_ioctl() and friends
 * ------------------------------------------------------------
 */

/*
 * isdn_tty_get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *          is emptied.  On bus types like RS485, the transmitter must
 *          release the bus after transmitting. This must be done when
 *          the transmit shift register is empty, not be done when the
 *          transmit holding register is empty.  This functionality
 *          allows RS485 driver to be written in user space. 
 */
static int isdn_tty_get_lsr_info(modem_info * info, uint * value)
{
	u_char status;
	uint result;
	ulong flags;

	save_flags(flags);
	cli();
	status = dev->mdm.msr[info->line];
	restore_flags(flags);
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	put_fs_long(result, (ulong *) value);
	return 0;
}


static int isdn_tty_get_modem_info(modem_info * info, uint * value)
{
	u_char control, status;
	uint result;
	ulong flags;

	control = info->MCR;
	save_flags(flags);
	cli();
	status = dev->mdm.msr[info->line];
	restore_flags(flags);
	result = ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
	    | ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
	    | ((status & UART_MSR_DCD) ? TIOCM_CAR : 0)
	    | ((status & UART_MSR_RI) ? TIOCM_RNG : 0)
	    | ((status & UART_MSR_DSR) ? TIOCM_DSR : 0)
	    | ((status & UART_MSR_CTS) ? TIOCM_CTS : 0);
	put_fs_long(result, (ulong *) value);
	return 0;
}

static int isdn_tty_set_modem_info(modem_info * info, uint cmd, uint * value)
{
	uint arg = get_fs_long((ulong *) value);

	switch (cmd) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS) {
			info->MCR |= UART_MCR_RTS;
		}
		if (arg & TIOCM_DTR) {
			info->MCR |= UART_MCR_DTR;
		}
		break;
	case TIOCMBIC:
		if (arg & TIOCM_RTS) {
			info->MCR &= ~UART_MCR_RTS;
		}
		if (arg & TIOCM_DTR) {
			info->MCR &= ~UART_MCR_DTR;
			isdn_tty_modem_reset_regs(&dev->mdm.atmodem[info->line], 0);
			if (dev->mdm.online[info->line]) {
#ifdef ISDN_DEBUG_MODEM_HUP
				printk(KERN_DEBUG "Mhup in TIOCMBIC\n");
#endif
				isdn_tty_modem_hup(info);
				isdn_tty_modem_result(3, info);
			}
		}
		break;
	case TIOCMSET:
		info->MCR = ((info->MCR & ~(UART_MCR_RTS | UART_MCR_DTR))
			     | ((arg & TIOCM_RTS) ? UART_MCR_RTS : 0)
			     | ((arg & TIOCM_DTR) ? UART_MCR_DTR : 0));
		if (!(info->MCR & UART_MCR_DTR)) {
			isdn_tty_modem_reset_regs(&dev->mdm.atmodem[info->line], 0);
			if (dev->mdm.online[info->line]) {
#ifdef ISDN_DEBUG_MODEM_HUP
				printk(KERN_DEBUG "Mhup in TIOCMSET\n");
#endif
				isdn_tty_modem_hup(info);
				isdn_tty_modem_result(3, info);
			}
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int isdn_tty_ioctl(struct tty_struct *tty, struct file *file,
		       uint cmd, ulong arg)
{
	modem_info *info = (modem_info *) tty->driver_data;
	int error;
	int retval;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_ioctl"))
		return -ENODEV;
	switch (cmd) {
	case TCSBRK:		/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		return 0;
	case TCSBRKP:		/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		return 0;
	case TIOCGSOFTCAR:
		error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(long));
		if (error)
			return error;
		put_fs_long(C_CLOCAL(tty) ? 1 : 0, (ulong *) arg);
		return 0;
	case TIOCSSOFTCAR:
		arg = get_fs_long((ulong *) arg);
		tty->termios->c_cflag =
		    ((tty->termios->c_cflag & ~CLOCAL) |
		     (arg ? CLOCAL : 0));
		return 0;
	case TIOCMGET:
		error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(uint));
		if (error)
			return error;
		return isdn_tty_get_modem_info(info, (uint *) arg);
	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		return isdn_tty_set_modem_info(info, cmd, (uint *) arg);
	case TIOCSERGETLSR:	/* Get line status register */
		error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(uint));
		if (error)
			return error;
		else
			return isdn_tty_get_lsr_info(info, (uint *) arg);

	case TIOCGSERIAL:
		return -ENOIOCTLCMD;
#if 0
		error = verify_area(VERIFY_WRITE, (void *) arg,
				    sizeof(struct serial_struct));
		if (error)
			return error;
		return get_serial_info(info,
				       (struct serial_struct *) arg);
#endif
	case TIOCSSERIAL:
		return -ENOIOCTLCMD;
#if 0
		return set_serial_info(info,
				       (struct serial_struct *) arg);
#endif
	case TIOCSERCONFIG:
		return -ENOIOCTLCMD;
#if 0
		return do_autoconfig(info);
#endif

	case TIOCSERGWILD:
		return -ENOIOCTLCMD;
#if 0
		error = verify_area(VERIFY_WRITE, (void *) arg,
				    sizeof(int));
		if (error)
			return error;
		put_fs_long(modem_wild_int_mask, (ulong *) arg);
		return 0;
#endif
	case TIOCSERSWILD:
		return -ENOIOCTLCMD;
#if 0
		if (!suser())
			return -EPERM;
		modem_wild_int_mask = get_fs_long((ulong *) arg);
		if (modem_wild_int_mask < 0)
			modem_wild_int_mask = check_wild_interrupts(0);
		return 0;
#endif
	case TIOCSERGSTRUCT:
		return -ENOIOCTLCMD;
#if 0
		error = verify_area(VERIFY_WRITE, (void *) arg,
				    sizeof(modem_info));
		if (error)
			return error;
		memcpy_tofs((modem_info *) arg,
			    info, sizeof(modem_info));
		return 0;
#endif
	default:
#ifdef ISDN_DEBUG_MODEM_IOCTL
		printk(KERN_DEBUG "unsupp. ioctl 0x%08x on ttyi%d\n", cmd, info->line);
#endif
		return -ENOIOCTLCMD;
	}
	return 0;
}

static void isdn_tty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;
	isdn_tty_change_speed(info);
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
	}
}

/*
 * ------------------------------------------------------------
 * isdn_tty_open() and friends
 * ------------------------------------------------------------
 */
static int isdn_tty_block_til_ready(struct tty_struct *tty, struct file *filp, modem_info * info)
{
	struct wait_queue wait = {current, NULL};
	int do_clocal = 0;
	unsigned long flags;
	int retval;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ISDN_ASYNC_CLOSING)) {
		if (info->flags & ISDN_ASYNC_CLOSING)
                        interruptible_sleep_on(&info->close_wait);
#ifdef MODEM_DO_RESTART
		if (info->flags & ISDN_ASYNC_HUP_NOTIFY)
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
	if (tty->driver.subtype == ISDN_SERIAL_TYPE_CALLOUT) {
		if (info->flags & ISDN_ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ISDN_ASYNC_SESSION_LOCKOUT) &&
		    (info->session != current->session))
			return -EBUSY;
		if ((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ISDN_ASYNC_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
			return -EBUSY;
		info->flags |= ISDN_ASYNC_CALLOUT_ACTIVE;
		return 0;
	}
	/*
	 * If non-blocking mode is set, then make the check up front
	 * and then exit.
	 */
	if (filp->f_flags & O_NONBLOCK) {
		if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ISDN_ASYNC_NORMAL_ACTIVE;
		return 0;
	}
	if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) {
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
	 * isdn_tty_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_block_til_ready before block: ttyi%d, count = %d\n",
	       info->line, info->count);
#endif
        save_flags(flags);
        cli();
        if (!(tty_hung_up_p(filp)))
                info->count--;
        restore_flags(flags);
	info->blocked_open++;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ISDN_ASYNC_INITIALIZED)) {
#ifdef MODEM_DO_RESTART
			if (info->flags & ISDN_ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    !(info->flags & ISDN_ASYNC_CLOSING) &&
		    (do_clocal || (
					  dev->mdm.msr[info->line] &
					  UART_MSR_DCD))) {
			break;
		}
		if (current->signal & ~current->blocked) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_block_til_ready blocking: ttyi%d, count = %d\n",
		       info->line, info->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_block_til_ready after blocking: ttyi%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ISDN_ASYNC_NORMAL_ACTIVE;
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int isdn_tty_open(struct tty_struct *tty, struct file *filp)
{
	modem_info *info;
	int retval, line;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if (line < 0 || line > ISDN_MAX_CHANNELS)
		return -ENODEV;
	info = &dev->mdm.info[line];
	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_open"))
		return -ENODEV;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open %s%d, count = %d\n", tty->driver.name,
	       info->line, info->count);
#endif
	info->count++;
	tty->driver_data = info;
	info->tty = tty;
	/*
	 * Start up serial port
	 */
	retval = isdn_tty_startup(info);
	if (retval) {
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_open return after startup\n");
#endif
		return retval;
	}
	retval = isdn_tty_block_til_ready(tty, filp, info);
	if (retval) {
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_open return after isdn_tty_block_til_ready \n");
#endif
		return retval;
	}
	if ((info->count == 1) && (info->flags & ISDN_ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == ISDN_SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else
			*tty->termios = info->callout_termios;
		isdn_tty_change_speed(info);
	}
	info->session = current->session;
	info->pgrp = current->pgrp;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open ttyi%d successful...\n", info->line);
#endif
	dev->modempoll++;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open normal exit\n");
#endif
	return 0;
}

static void isdn_tty_close(struct tty_struct *tty, struct file *filp)
{
	modem_info *info = (modem_info *) tty->driver_data;
	ulong flags;
	ulong timeout;

	if (!info || isdn_tty_paranoia_check(info, tty->device, "isdn_tty_close"))
		return;
	save_flags(flags);
	cli();
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_close return after tty_hung_up_p\n");
#endif
		return;
	}
	dev->modempoll--;
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_ERR "isdn_tty_close: bad port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_ERR "isdn_tty_close: bad port count for ttyi%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_close after info->count != 0\n");
#endif
		return;
	}
	info->flags |= ISDN_ASYNC_CLOSING;
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ISDN_ASYNC_NORMAL_ACTIVE)
		info->normal_termios = *tty->termios;
	if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE)
		info->callout_termios = *tty->termios;

	tty->closing = 1;

	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	if (info->flags & ISDN_ASYNC_INITIALIZED) {
		tty_wait_until_sent(tty, 3000);		/* 30 seconds timeout */
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies + HZ;
		while (!(dev->mdm.mlr[info->line]
			 & UART_LSR_TEMT)) {
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + 20;
			schedule();
			if (jiffies > timeout)
				break;
		}
	}
	isdn_tty_shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	info->tty = 0;
	tty->closing = 0;
#if 00
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close) (tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open)
			(tty->ldisc.open) (tty);
	}
#endif
	if (info->blocked_open) {
		if (info->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + info->close_delay;
			schedule();
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE |
			 ISDN_ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_close normal exit\n");
#endif
}

/*
 * isdn_tty_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void isdn_tty_hangup(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_hangup"))
		return;
	isdn_tty_shutdown(info);
	info->count = 0;
	info->flags &= ~(ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

static void isdn_tty_reset_profile(atemu * m)
{
	m->profile[0] = 0;
	m->profile[1] = 0;
	m->profile[2] = 43;
	m->profile[3] = 13;
	m->profile[4] = 10;
	m->profile[5] = 8;
	m->profile[6] = 3;
	m->profile[7] = 60;
	m->profile[8] = 2;
	m->profile[9] = 6;
	m->profile[10] = 7;
	m->profile[11] = 70;
	m->profile[12] = 0x45;
	m->profile[13] = 0;
	m->profile[14] = ISDN_PROTO_L2_X75I;
	m->profile[15] = ISDN_PROTO_L3_TRANS;
	m->profile[16] = ISDN_SERIAL_XMIT_SIZE / 16;
	m->profile[17] = ISDN_MODEM_WINSIZE;
	m->profile[18] = 7;
	m->profile[19] = 0;
	m->pmsn[0] = '\0';
}

static void isdn_tty_modem_reset_regs(atemu * m, int force)
{
	if ((m->mdmreg[12] & 32) || force) {
		memcpy(m->mdmreg, m->profile, ISDN_MODEM_ANZREG);
		memcpy(m->msn, m->pmsn, ISDN_MSNLEN);
	}
	m->mdmcmdl = 0;
}

static void modem_write_profile(atemu * m)
{
	memcpy(m->profile, m->mdmreg, ISDN_MODEM_ANZREG);
	memcpy(m->pmsn, m->msn, ISDN_MSNLEN);
	if (dev->profd)
		send_sig(SIGIO, dev->profd, 1);
}

int isdn_tty_modem_init(void)
{
	modem *m;
	int i;
	modem_info *info;

	m = &dev->mdm;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		isdn_tty_reset_profile(&(m->atmodem[i]));
		isdn_tty_modem_reset_regs(&(m->atmodem[i]), 1);
	}
	memset(&m->tty_modem, 0, sizeof(struct tty_driver));
	m->tty_modem.magic = TTY_DRIVER_MAGIC;
	m->tty_modem.name = isdn_ttyname_ttyI;
	m->tty_modem.major = ISDN_TTY_MAJOR;
	m->tty_modem.minor_start = 0;
	m->tty_modem.num = ISDN_MAX_CHANNELS;
	m->tty_modem.type = TTY_DRIVER_TYPE_SERIAL;
	m->tty_modem.subtype = ISDN_SERIAL_TYPE_NORMAL;
	m->tty_modem.init_termios = tty_std_termios;
	m->tty_modem.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	m->tty_modem.flags = TTY_DRIVER_REAL_RAW;
	m->tty_modem.refcount = &m->refcount;
	m->tty_modem.table = m->modem_table;
	m->tty_modem.termios = m->modem_termios;
	m->tty_modem.termios_locked = m->modem_termios_locked;
	m->tty_modem.open = isdn_tty_open;
	m->tty_modem.close = isdn_tty_close;
	m->tty_modem.write = isdn_tty_write;
	m->tty_modem.put_char = NULL;
	m->tty_modem.flush_chars = isdn_tty_flush_chars;
	m->tty_modem.write_room = isdn_tty_write_room;
	m->tty_modem.chars_in_buffer = isdn_tty_chars_in_buffer;
	m->tty_modem.flush_buffer = isdn_tty_flush_buffer;
	m->tty_modem.ioctl = isdn_tty_ioctl;
	m->tty_modem.throttle = isdn_tty_throttle;
	m->tty_modem.unthrottle = isdn_tty_unthrottle;
	m->tty_modem.set_termios = isdn_tty_set_termios;
	m->tty_modem.stop = NULL;
	m->tty_modem.start = NULL;
	m->tty_modem.hangup = isdn_tty_hangup;
	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	m->cua_modem = m->tty_modem;
	m->cua_modem.name = isdn_ttyname_cui;
	m->cua_modem.major = ISDN_TTYAUX_MAJOR;
	m->tty_modem.minor_start = 0;
	m->cua_modem.subtype = ISDN_SERIAL_TYPE_CALLOUT;

	if (tty_register_driver(&m->tty_modem)) {
		printk(KERN_WARNING "isdn: Unable to register modem-device\n");
		return -1;
	}
	if (tty_register_driver(&m->cua_modem)) {
		printk(KERN_WARNING "Couldn't register modem-callout-device\n");
		return -2;
	}
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		info = &(m->info[i]);
		info->magic = ISDN_ASYNC_MAGIC;
		info->line = i;
		info->tty = 0;
		info->close_delay = 50;
		info->x_char = 0;
		info->count = 0;
		info->blocked_open = 0;
		info->callout_termios = m->cua_modem.init_termios;
		info->normal_termios = m->tty_modem.init_termios;
		info->open_wait = 0;
		info->close_wait = 0;
		info->type = ISDN_PORT_16550A;
		info->isdn_driver = -1;
		info->isdn_channel = -1;
		info->drv_index = -1;
		info->xmit_size = ISDN_SERIAL_XMIT_SIZE;
		if (!(info->xmit_buf = kmalloc(ISDN_SERIAL_XMIT_SIZE + 5, GFP_KERNEL))) {
			printk(KERN_ERR "Could not allocate modem xmit-buffer\n");
			return -3;
		}
		info->xmit_buf += 4;	/* Make room for T.70 header */
	}
	return 0;
}

/*
 * An incoming call-request has arrived.
 * Search the tty-devices for an appropriate device and bind
 * it to the ISDN-Channel.
 * Return Index to dev->mdm or -1 if none found.
 */
int isdn_tty_find_icall(int di, int ch, char *num)
{
	char *eaz;
	int i;
	int idx;
	int si1;
	int si2;
	char *s;
	char nr[31];
	ulong flags;

	save_flags(flags);
	cli();
	if (num[0] == ',') {
		nr[0] = '0';
		strncpy(&nr[1], num, 29);
		printk(KERN_WARNING "isdn: Incoming call without OAD, assuming '0'\n");
	} else
		strncpy(nr, num, 30);
	s = strtok(nr, ",");
	s = strtok(NULL, ",");
	if (!s) {
		printk(KERN_WARNING "isdn: Incoming callinfo garbled, ignored: %s\n",
		       num);
		restore_flags(flags);
		return -1;
	}
	si1 = (int)simple_strtoul(s,NULL,10);
	s = strtok(NULL, ",");
	if (!s) {
		printk(KERN_WARNING "isdn: Incoming callinfo garbled, ignored: %s\n",
		       num);
		restore_flags(flags);
		return -1;
	}
	si2 = (int)simple_strtoul(s,NULL,10);
	eaz = strtok(NULL, ",");
	if (!eaz) {
		printk(KERN_WARNING "isdn: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	}
#ifdef ISDN_DEBUG_MODEM_ICALL
	printk(KERN_DEBUG "m_fi: eaz=%s si1=%d si2=%d\n", eaz, si1, si2);
#endif
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
#ifdef ISDN_DEBUG_MODEM_ICALL
		printk(KERN_DEBUG "m_fi: i=%d msn=%s mmsn=%s mreg18=%d mreg19=%d\n", i,
		       dev->mdm.atmodem[i].msn, isdn_map_eaz2msn(dev->mdm.atmodem[i].msn, di),
		       dev->mdm.atmodem[i].mdmreg[18], dev->mdm.atmodem[i].mdmreg[19]);
#endif
		if ((!strcmp(isdn_map_eaz2msn(dev->mdm.atmodem[i].msn, di)
			     ,eaz)) &&	/* EAZ is matching   */
		    (dev->mdm.atmodem[i].mdmreg[18] == si1) &&	/* SI1 is matching   */
		    (dev->mdm.atmodem[i].mdmreg[19] == si2)) {	/* SI2 is matching   */
			modem_info *info = &dev->mdm.info[i];
			idx = isdn_dc2minor(di, ch);
#ifdef ISDN_DEBUG_MODEM_ICALL
			printk(KERN_DEBUG "m_fi: match1\n");
			printk(KERN_DEBUG "m_fi: idx=%d flags=%08lx drv=%d ch=%d usg=%d\n", idx,
			       info->flags, info->isdn_driver, info->isdn_channel,
			       dev->usage[idx]);
#endif
			if ((info->flags & ISDN_ASYNC_NORMAL_ACTIVE) &&
			    (info->isdn_driver == -1) &&
			    (info->isdn_channel == -1) &&
			    (USG_NONE(dev->usage[idx]))) {
				info->isdn_driver = di;
				info->isdn_channel = ch;
				info->drv_index = idx;
				dev->m_idx[idx] = info->line;
				dev->usage[idx] &= ISDN_USAGE_EXCLUSIVE;
				dev->usage[idx] |= ISDN_USAGE_MODEM;
				strcpy(dev->num[idx], nr);
				isdn_info_update();
				restore_flags(flags);
				printk(KERN_INFO "isdn_tty: call from %s, -> RING on ttyI%d\n", nr,
				       info->line);
				return info->line;
			}
		}
	}
	printk(KERN_INFO "isdn_tty: call from %s -> %s %s\n", nr, eaz,
	       dev->drv[di]->reject_bus ? "rejected" : "ignored");
	restore_flags(flags);
	return -1;
}

/*********************************************************************
 Modem-Emulator-Routines
 *********************************************************************/

#define cmdchar(c) ((c>' ')&&(c<=0x7f))

/*
 * Put a message from the AT-emulator into receive-buffer of tty,
 * convert CR, LF, and BS to values in modem-registers 3, 4 and 5.
 */
static void isdn_tty_at_cout(char *msg, modem_info * info)
{
	struct tty_struct *tty;
	atemu *m = &(dev->mdm.atmodem[info->line]);
	char *p;
	char c;
	ulong flags;

	if (!msg) {
		printk(KERN_WARNING "isdn: Null-Message in isdn_tty_at_cout\n");
		return;
	}
	save_flags(flags);
	cli();
	tty = info->tty;
	for (p = msg; *p; p++) {
		switch (*p) {
		case '\r':
			c = m->mdmreg[3];
			break;
		case '\n':
			c = m->mdmreg[4];
			break;
		case '\b':
			c = m->mdmreg[5];
			break;
		default:
			c = *p;
		}
		if ((info->flags & ISDN_ASYNC_CLOSING) || (!tty)) {
			restore_flags(flags);
			return;
		}
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			break;
		tty_insert_flip_char(tty, c, 0);
	}
	restore_flags(flags);
	queue_task(&tty->flip.tqueue, &tq_timer);
}

/*
 * Perform ATH Hangup
 */
static void isdn_tty_on_hook(modem_info * info)
{
	if (info->isdn_channel >= 0) {
#ifdef ISDN_DEBUG_MODEM_HUP
		printk(KERN_DEBUG "Mhup in isdn_tty_on_hook\n");
#endif
		isdn_tty_modem_hup(info);
		isdn_tty_modem_result(3, info);
	}
}

static void isdn_tty_off_hook(void)
{
	printk(KERN_DEBUG "isdn_tty_off_hook\n");
}

#define PLUSWAIT1 (HZ/2)	/* 0.5 sec. */
#define PLUSWAIT2 (HZ*3/2)	/* 1.5 sec */

/*
 * Check Buffer for Modem-escape-sequence, activate timer-callback to
 * isdn_tty_modem_escape() if sequence found.
 *
 * Parameters:
 *   p          pointer to databuffer
 *   plus       escape-character
 *   count      length of buffer
 *   pluscount  count of valid escape-characters so far
 *   lastplus   timestamp of last character
 */
static void isdn_tty_check_esc(const u_char * p, u_char plus, int count, int *pluscount,
			   int *lastplus, int from_user)
{
	char cbuf[3];

	if (plus > 127)
		return;
	if (count > 3) {
		p += count - 3;
		count = 3;
		*pluscount = 0;
	}
	if (from_user) {
		memcpy_fromfs(cbuf, p, count);
		p = cbuf;
	}
	while (count > 0) {
		if (*(p++) == plus) {
			if ((*pluscount)++) {
				/* Time since last '+' > 0.5 sec. ? */
				if ((jiffies - *lastplus) > PLUSWAIT1)
					*pluscount = 1;
			} else {
				/* Time since last non-'+' < 1.5 sec. ? */
				if ((jiffies - *lastplus) < PLUSWAIT2)
					*pluscount = 0;
			}
			if ((*pluscount == 3) && (count = 1))
				isdn_timer_ctrl(ISDN_TIMER_MODEMPLUS, 1);
			if (*pluscount > 3)
				*pluscount = 1;
		} else
			*pluscount = 0;
		*lastplus = jiffies;
		count--;
	}
}

/*
 * Return result of AT-emulator to tty-receive-buffer, depending on
 * modem-register 12, bit 0 and 1.
 * For CONNECT-messages also switch to online-mode.
 * For RING-message handle auto-ATA if register 0 != 0
 */
void isdn_tty_modem_result(int code, modem_info * info)
{
	atemu *m = &dev->mdm.atmodem[info->line];
	static char *msg[] =
	{"OK", "CONNECT", "RING", "NO CARRIER", "ERROR",
	 "CONNECT 64000", "NO DIALTONE", "BUSY", "NO ANSWER",
	 "RINGING", "NO MSN/EAZ"};
	ulong flags;
	char s[4];

	switch (code) {
	case 2:
		m->mdmreg[1]++;	/* RING */
		if (m->mdmreg[1] == m->mdmreg[0]) {
			/* Accept incoming call */
			isdn_ctrl cmd;
			m->mdmreg[1] = 0;
			dev->mdm.msr[info->line] &= ~UART_MSR_RI;
			cmd.driver = info->isdn_driver;
			cmd.arg = info->isdn_channel;
			cmd.command = ISDN_CMD_ACCEPTD;
			dev->drv[info->isdn_driver]->interface->command(&cmd);
		}
		break;
	case 3:
		/* NO CARRIER */
		save_flags(flags);
		cli();
		dev->mdm.msr[info->line] &= ~(UART_MSR_DCD | UART_MSR_RI);
		if ((info->flags & ISDN_ASYNC_CLOSING) || (!info->tty)) {
			restore_flags(flags);
			return;
		}
		restore_flags(flags);
		break;
	case 1:
	case 5:
		dev->mdm.online[info->line] = 1;
		break;
	}
	if (m->mdmreg[12] & 1) {
		/* Show results */
		isdn_tty_at_cout("\r\n", info);
		if (m->mdmreg[12] & 2) {
			/* Show numeric results */
			sprintf(s, "%d", code);
			isdn_tty_at_cout(s, info);
		} else {
			if (code == 2) {
				isdn_tty_at_cout("CALLER NUMBER: ", info);
				isdn_tty_at_cout(dev->num[info->drv_index], info);
				isdn_tty_at_cout("\r\n", info);
			}
			isdn_tty_at_cout(msg[code], info);
			if (code == 5) {
				/* Append Protocol to CONNECT message */
				isdn_tty_at_cout((m->mdmreg[14] != 3) ? "/X.75" : "/HDLC", info);
				if (m->mdmreg[13] & 2)
					isdn_tty_at_cout("/T.70", info);
			}
		}
		isdn_tty_at_cout("\r\n", info);
	}
	if (code == 3) {
		save_flags(flags);
		cli();
		if ((info->flags & ISDN_ASYNC_CLOSING) || (!info->tty)) {
			restore_flags(flags);
			return;
		}
		if ((info->flags & ISDN_ASYNC_CHECK_CD) &&
		    (!((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		       (info->flags & ISDN_ASYNC_CALLOUT_NOHUP))))
			tty_hangup(info->tty);
		restore_flags(flags);
	}
}

/*
 * Display a modem-register-value.
 */
static void isdn_tty_show_profile(int ridx, modem_info * info)
{
	char v[6];

	sprintf(v, "%d\r\n", dev->mdm.atmodem[info->line].mdmreg[ridx]);
	isdn_tty_at_cout(v, info);
}

/*
 * Get MSN-string from char-pointer, set pointer to end of number
 */
static void isdn_tty_get_msnstr(char *n, char **p)
{
	while ((*p[0] >= '0' && *p[0] <= '9') || (*p[0] == ','))
		*n++ = *p[0]++;
	*n = '\0';
}

/*
 * Get phone-number from modem-commandbuffer
 */
static void isdn_tty_getdial(char *p, char *q)
{
	int first = 1;

	while (strchr("0123456789,#.*WPTS-", *p) && *p) {
		if ((*p >= '0' && *p <= '9') || ((*p == 'S') && first))
			*q++ = *p;
		p++;
		first = 0;
	}
	*q = 0;
}

/*
 * Parse and perform an AT-command-line.
 *
 * Parameter:
 *   channel   index to line (minor-device)
 */
static void isdn_tty_parse_at(modem_info * info)
{
	atemu *m = &dev->mdm.atmodem[info->line];
	char *p;
	int mreg;
	int mval;
	int i;
	char rb[100];
	char ds[40];
	isdn_ctrl cmd;

#ifdef ISDN_DEBUG_AT
	printk(KERN_DEBUG "AT: '%s'\n", m->mdmcmd);
#endif
	for (p = &m->mdmcmd[2]; *p;) {
		switch (*p) {
		case 'A':
			/* A - Accept incoming call */
			p++;
			if (m->mdmreg[1]) {
#define FIDOBUG
#ifdef FIDOBUG
/* Variables fido... defined temporarily for finding a strange bug */
				driver *fido_drv;
				isdn_if *fido_if;
				int fido_isdn_driver;
				modem_info *fido_modem_info;
				int (*fido_command) (isdn_ctrl *);
#endif
				/* Accept incoming call */
				m->mdmreg[1] = 0;
				dev->mdm.msr[info->line] &= ~UART_MSR_RI;
				cmd.driver = info->isdn_driver;
				cmd.command = ISDN_CMD_SETL2;
				cmd.arg = info->isdn_channel + (m->mdmreg[14] << 8);
				dev->drv[info->isdn_driver]->interface->command(&cmd);
				cmd.driver = info->isdn_driver;
				cmd.command = ISDN_CMD_SETL3;
				cmd.arg = info->isdn_channel + (m->mdmreg[15] << 8);
				dev->drv[info->isdn_driver]->interface->command(&cmd);
				cmd.driver = info->isdn_driver;
				cmd.arg = info->isdn_channel;
				cmd.command = ISDN_CMD_ACCEPTD;
#ifdef FIDOBUG
				fido_modem_info = info;
				fido_isdn_driver = fido_modem_info->isdn_driver;
				fido_drv = dev->drv[fido_isdn_driver];
				fido_if = fido_drv->interface;
				fido_command = fido_if->command;
				fido_command(&cmd);
#else
				dev->drv[info->isdn_driver]->interface->command(&cmd);
#endif
			} else {
				isdn_tty_modem_result(8, info);
				return;
			}
			break;
		case 'D':
			/* D - Dial */
			isdn_tty_getdial(++p, ds);
			p += strlen(p);
			if (!strlen(m->msn))
				isdn_tty_modem_result(10, info);
			else if (strlen(ds))
				isdn_tty_dial(ds, info, m);
			else
				isdn_tty_modem_result(4, info);
			return;
		case 'E':
			/* E - Turn Echo on/off */
			p++;
			switch (*p) {
			case '0':
				p++;
				m->mdmreg[12] &= ~4;
				break;
			case '1':
				p++;
				m->mdmreg[12] |= 4;
				break;
			default:
				isdn_tty_modem_result(4, info);
				return;
			}
			break;
		case 'H':
			/* H - On/Off-hook */
			p++;
			switch (*p) {
			case '0':
				p++;
				isdn_tty_on_hook(info);
				break;
			case '1':
				p++;
				isdn_tty_off_hook();
				break;
			default:
				isdn_tty_on_hook(info);
				break;
			}
			break;
		case 'I':
			/* I - Information */
			p++;
			isdn_tty_at_cout("ISDN for Linux  (c) by Fritz Elfert\r\n", info);
			switch (*p) {
			case '0':
			case '1':
				p++;
				break;
			default:
			}
			break;
		case 'O':
			/* O - Go online */
			p++;
			if (dev->mdm.msr[info->line] & UART_MSR_DCD)	/* if B-Channel is up */
				isdn_tty_modem_result(5, info);
			else
				isdn_tty_modem_result(3, info);
			return;
		case 'Q':
			/* Q - Turn Emulator messages on/off */
			p++;
			switch (*p) {
			case '0':
				p++;
				m->mdmreg[12] |= 1;
				break;
			case '1':
				p++;
				m->mdmreg[12] &= ~1;
				break;
			default:
				isdn_tty_modem_result(4, info);
				return;
			}
			break;
		case 'S':
			/* S - Set/Get Register */
			p++;
			mreg = isdn_getnum(&p);
			if (mreg < 0 || mreg > ISDN_MODEM_ANZREG) {
				isdn_tty_modem_result(4, info);
				return;
			}
			switch (*p) {
			case '=':
				p++;
				mval = isdn_getnum(&p);
				if (mval >= 0 && mval <= 255) {
					if ((mreg == 16) && ((mval * 16) > ISDN_SERIAL_XMIT_SIZE)) {
						isdn_tty_modem_result(4, info);
						return;
					}
					m->mdmreg[mreg] = mval;
				} else {
					isdn_tty_modem_result(4, info);
					return;
				}
				break;
			case '?':
				p++;
				isdn_tty_show_profile(mreg, info);
				return;
				break;
			default:
				isdn_tty_modem_result(4, info);
				return;
			}
			break;
		case 'V':
			/* V - Numeric or ASCII Emulator-messages */
			p++;
			switch (*p) {
			case '0':
				p++;
				m->mdmreg[12] |= 2;
				break;
			case '1':
				p++;
				m->mdmreg[12] &= ~2;
				break;
			default:
				isdn_tty_modem_result(4, info);
				return;
			}
			break;
		case 'Z':
			/* Z - Load Registers from Profile */
			p++;
			isdn_tty_modem_reset_regs(m, 1);
			break;
		case '+':
			p++;
			switch (*p) {
			case 'F':
				break;
			}
			break;
		case '&':
			p++;
			switch (*p) {
			case 'B':
				/* &B - Set Buffersize */
				p++;
				i = isdn_getnum(&p);
				if ((i < 0) || (i > ISDN_SERIAL_XMIT_SIZE)) {
					isdn_tty_modem_result(4, info);
					return;
				}
				m->mdmreg[16] = i / 16;
				break;
			case 'D':
				/* &D - Set DCD-Low-behavior */
				p++;
				switch (isdn_getnum(&p)) {
				case 2:
					m->mdmreg[12] &= ~32;
					break;
				case 3:
					m->mdmreg[12] |= 32;
					break;
				default:
					isdn_tty_modem_result(4, info);
					return;
				}
				break;
			case 'E':
				/* &E -Set EAZ/MSN */
				p++;
				isdn_tty_get_msnstr(m->msn, &p);
				break;
			case 'F':
				/* &F -Set Factory-Defaults */
				p++;
				isdn_tty_reset_profile(m);
				isdn_tty_modem_reset_regs(m, 1);
				break;
			case 'S':
				/* &S - Set Windowsize */
				p++;
				i = isdn_getnum(&p);
				if ((i > 0) && (i < 9))
					m->mdmreg[17] = i;
				else {
					isdn_tty_modem_result(4, info);
					return;
				}
				break;
			case 'V':
				/* &V - Show registers */
				p++;
				for (i = 0; i < ISDN_MODEM_ANZREG; i++) {
					sprintf(rb, "S%d=%d%s", i, m->mdmreg[i], (i == 6) ? "\r\n" : " ");
					isdn_tty_at_cout(rb, info);
				}
				sprintf(rb, "\r\nEAZ/MSN: %s\r\n", strlen(m->msn) ? m->msn : "None");
				isdn_tty_at_cout(rb, info);
				break;
			case 'W':
				/* &W - Write Profile */
				p++;
				switch (*p) {
				case '0':
					p++;
					modem_write_profile(m);
					break;
				default:
					isdn_tty_modem_result(4, info);
					return;
				}
				break;
			case 'X':
				/* &X - Switch to BTX-Mode */
				p++;
				switch (*p) {
				case '0':
					p++;
					m->mdmreg[13] &= ~2;
					break;
				case '1':
					p++;
					m->mdmreg[13] |= 2;
					m->mdmreg[14] = 0;
					m->mdmreg[16] = 7;
					m->mdmreg[18] = 7;
					m->mdmreg[19] = 0;
					break;
				default:
					isdn_tty_modem_result(4, info);
					return;
				}
				break;
			default:
				isdn_tty_modem_result(4, info);
				return;
			}
			break;
		default:
			isdn_tty_modem_result(4, info);
			return;
		}
	}
	isdn_tty_modem_result(0, info);
}

/* Need own toupper() because standard-toupper is not available
 * within modules.
 */
#define my_toupper(c) (((c>='a')&&(c<='z'))?(c&0xdf):c)

/*
 * Perform line-editing of AT-commands
 *
 * Parameters:
 *   p        inputbuffer
 *   count    length of buffer
 *   channel  index to line (minor-device)
 *   user     flag: buffer is in userspace
 */
static int isdn_tty_edit_at(const char *p, int count, modem_info * info, int user)
{
	atemu *m = &dev->mdm.atmodem[info->line];
	int total = 0;
	u_char c;
	char eb[2];
	int cnt;

	for (cnt = count; cnt > 0; p++, cnt--) {
		if (user)
			c = get_fs_byte(p);
		else
			c = *p;
		total++;
		if (c == m->mdmreg[3] || c == m->mdmreg[4]) {
			/* Separator (CR oder LF) */
			m->mdmcmd[m->mdmcmdl] = 0;
			if (m->mdmreg[12] & 4) {
				eb[0] = c;
				eb[1] = 0;
				isdn_tty_at_cout(eb, info);
			}
			if (m->mdmcmdl >= 2)
				isdn_tty_parse_at(info);
			m->mdmcmdl = 0;
			continue;
		}
		if (c == m->mdmreg[5] && m->mdmreg[5] < 128) {
			/* Backspace-Funktion */
			if ((m->mdmcmdl > 2) || (!m->mdmcmdl)) {
				if (m->mdmcmdl)
					m->mdmcmdl--;
				if (m->mdmreg[12] & 4)
					isdn_tty_at_cout("\b", info);
			}
			continue;
		}
		if (cmdchar(c)) {
			if (m->mdmreg[12] & 4) {
				eb[0] = c;
				eb[1] = 0;
				isdn_tty_at_cout(eb, info);
			}
			if (m->mdmcmdl < 255) {
				c = my_toupper(c);
				switch (m->mdmcmdl) {
				case 0:
					if (c == 'A')
						m->mdmcmd[m->mdmcmdl++] = c;
					break;
				case 1:
					if (c == 'T')
						m->mdmcmd[m->mdmcmdl++] = c;
					break;
				default:
					m->mdmcmd[m->mdmcmdl++] = c;
				}
			}
		}
	}
	return total;
}

/*
 * Switch all modem-channels who are online and got a valid
 * escape-sequence 1.5 seconds ago, to command-mode.
 * This function is called every second via timer-interrupt from within 
 * timer-dispatcher isdn_timer_function()
 */
void isdn_tty_modem_escape(void)
{
	int ton = 0;
	int i;
	int midx;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (USG_MODEM(dev->usage[i]))
			if ((midx = dev->m_idx[i]) >= 0)
				if (dev->mdm.online[midx]) {
					ton = 1;
					if ((dev->mdm.atmodem[midx].pluscount == 3) &&
					    ((jiffies - dev->mdm.atmodem[midx].lastplus) > PLUSWAIT2)) {
						dev->mdm.atmodem[midx].pluscount = 0;
						dev->mdm.online[midx] = 0;
						isdn_tty_modem_result(0, &dev->mdm.info[midx]);
					}
				}
	isdn_timer_ctrl(ISDN_TIMER_MODEMPLUS, ton);
}

/*
 * Put a RING-message to all modem-channels who have the RI-bit set.
 * This function is called every second via timer-interrupt from within 
 * timer-dispatcher isdn_timer_function()
 */
void isdn_tty_modem_ring(void)
{
	int ton = 0;
	int i;
	int midx;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (USG_MODEM(dev->usage[i]))
			if ((midx = dev->m_idx[i]) >= 0)
				if (dev->mdm.msr[midx] & UART_MSR_RI) {
					ton = 1;
					isdn_tty_modem_result(2, &dev->mdm.info[midx]);
				}
	isdn_timer_ctrl(ISDN_TIMER_MODEMRING, ton);
}

void isdn_tty_modem_xmit(void)
{
	int ton = 0;
	int i;
	int midx;
	char *bufptr;
	int buflen;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (USG_MODEM(dev->usage[i]))
			if ((midx = dev->m_idx[i]) >= 0)
				if (dev->mdm.online[midx]) {
					modem_info *info = &(dev->mdm.info[midx]);
					ulong flags;

					save_flags(flags);
					cli();
					if (info->xmit_count > 0) {
						struct tty_struct *tty = info->tty;
						ton = 1;
#if 0
						printk(KERN_DEBUG "WB2: %d\n", info->xmit_count);
#endif
						bufptr = info->xmit_buf;
						buflen = info->xmit_count;
						if (dev->mdm.atmodem[midx].mdmreg[13] & 2) {
							/* Add T.70 simplified header */
#ifdef ISDN_DEBUG_MODEM_DUMP
							isdn_dumppkt("T70pack3:", bufptr, buflen, 40);
#endif
							bufptr -= 4;
							buflen += 4;
							memcpy(bufptr, "\1\0\1\0", 4);
#ifdef ISDN_DEBUG_MODEM_DUMP
							isdn_dumppkt("T70pack4:", bufptr, buflen, 40);
#endif
						}
						if (dev->drv[info->isdn_driver]->interface->
						    writebuf(info->isdn_driver, info->isdn_channel, bufptr, buflen, 0) > 0) {
							info->xmit_count = 0;
							info->xmit_size = dev->mdm.atmodem[midx].mdmreg[16] * 16;
#if FUTURE
							info->send_outstanding++;
							dev->mdm.msr[midx] &= ~UART_MSR_CTS;
#endif
							if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
							    tty->ldisc.write_wakeup)
								(tty->ldisc.write_wakeup) (tty);
							wake_up_interruptible(&tty->write_wait);
						}
					}
					restore_flags(flags);
				}
	isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, ton);
}

#if FUTURE
/*
 * A packet has been output successfully.
 * Search the tty-devices for an appropriate device, decrement its
 * counter for outstanding packets, and set CTS if this counter reaches 0.
 */
void isdn_tty_bsent(int drv, int chan)
{
	int i;
	ulong flags;

	save_flags(flags);
	cli();
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		modem_info *info = &dev->mdm.info[i];
		if ((info->isdn_driver == drv) &&
		    (info->isdn_channel == chan) &&
		    (info->send_outstanding)) {
			if (!(--info->send_outstanding))
				dev->mdm.msr[i] |= UART_MSR_CTS;
			restore_flags(flags);
			return;
		}
	}
	restore_flags(flags);
	return;
}
#endif				/* FUTURE */

/*
 *  linux/kernel/serial.c
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
 * This module exports the following rs232 io functions:
 *
 *	long rs_init(long);
 * 	int  rs_open(struct tty_struct * tty, struct file * filp)
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/string.h>
#include <linux/fcntl.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/bitops.h>

/*
 * Serial driver configuration section.  Here are the various options:
 *
 * CONFIG_AUTO_IRQ
 *		Enables automatic IRQ detection.  I've put in some
 * 		fixes to this which should make this work much more
 * 		cleanly than it used to in 0.98pl2-6.  It should be
 * 		much less vulnerable to false IRQ's now.
 * 
 * CONFIG_AST_FOURPORT
 *		Enables support for the AST Fourport serial port.
 * 
 * CONFIG_ACCENT_ASYNC
 *		Enables support for the Accent Async 4 port serial
 * 		port.
 * 
 */
	
#define WAKEUP_CHARS (3*TTY_BUF_SIZE/4)

/*
 * rs_event		- Bitfield of serial lines that events pending
 * 				to be processed at the next clock tick.
 * rs_write_active	- Bitfield of serial lines that are actively
 * 				transmitting (and therefore have a
 * 				write timeout pending, in case the
 * 				THRE interrupt gets lost.)
 *
 * We assume here that int's are 32 bits, so an array of two gives us
 * 64 lines, which is the maximum we can support.
 */
static int rs_event[2];
static int rs_write_active[2];

static struct async_struct *IRQ_ports[16];

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD ( 1843200 / 16 ) 

struct async_struct rs_table[] = {
	{ BASE_BAUD, 0x3F8, 4, 0, },
	{ BASE_BAUD, 0x2F8, 3, 0, },
	{ BASE_BAUD, 0x3E8, 4, 0, },
	{ BASE_BAUD, 0x2E8, 3, 0, },
#ifdef CONFIG_AST_FOURPORT
	{ BASE_BAUD, 0x1A0, 2, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x1A8, 2, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x1B0, 2, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x1B8, 2, ASYNC_FOURPORT },

	{ BASE_BAUD, 0x2A0, 5, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x2A8, 5, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x2B0, 5, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x2B8, 5, ASYNC_FOURPORT },
#else /* CONFIG_AST_FOURPORT */
	{ BASE_BAUD, 0x000, 0 }, 
	{ BASE_BAUD, 0x000, 0 }, 
	{ BASE_BAUD, 0x000, 0 },
	{ BASE_BAUD, 0x000, 0 }, 

	{ BASE_BAUD, 0x000, 0 },
	{ BASE_BAUD, 0x000, 0 }, 
	{ BASE_BAUD, 0x000, 0 },
	{ BASE_BAUD, 0x000, 0 },
#endif /* CONFIG_AST_FOURPORT */
	
#ifdef CONFIG_ACCENT_ASYNC
	{ BASE_BAUD, 0x330, 4, 0 },
	{ BASE_BAUD, 0x338, 4, 0 },
#else /* CONFIG_ACCENT_ASYNC */
	{ BASE_BAUD, 0x000, 0 },
	{ BASE_BAUD, 0x000, 0 },
#endif /* CONFIG_ACCENT_ASYNC */
	{ BASE_BAUD, 0x000, 0 },
	{ BASE_BAUD, 0x000, 0 },

	{ BASE_BAUD, 0x100, 4, 0 },
	{ BASE_BAUD, 0x108, 4, 0 },
	{ BASE_BAUD, 0x110, 4, 0 },
	{ BASE_BAUD, 0x118, 4, 0 },
	{ BASE_BAUD, 0x120, 4, 0 },
	{ BASE_BAUD, 0x128, 4, 0 },
	{ BASE_BAUD, 0x130, 4, 0 },
	{ BASE_BAUD, 0x138, 4, 0 },
	{ BASE_BAUD, 0x140, 4, 0 },
	{ BASE_BAUD, 0x148, 4, 0 },
	{ BASE_BAUD, 0x150, 4, 0 },
	{ BASE_BAUD, 0x158, 4, 0 },
	{ BASE_BAUD, 0x160, 4, 0 },
	{ BASE_BAUD, 0x168, 4, 0 },
	{ BASE_BAUD, 0x170, 4, 0 },
	{ BASE_BAUD, 0x178, 4, 0 },
};

#define NR_PORTS	(sizeof(rs_table)/sizeof(struct async_struct))

/*
 * This is used to figure out the divsor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0 };

static void startup(struct async_struct * info);
static void shutdown(struct async_struct * info);
static void rs_throttle(struct tty_struct * tty, int status);
static void restart_port(struct async_struct *info);
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct async_struct *info);

static inline unsigned int serial_in(struct async_struct *info, int offset)
{
	return inb(info->port + offset);
}

static inline unsigned int serial_inp(struct async_struct *info, int offset)
{
	return inb_p(info->port + offset);
}

static inline void serial_out(struct async_struct *info, int offset, int value)
{
	outb(value, info->port+offset);
}

static inline void serial_outp(struct async_struct *info, int offset,
			       int value)
{
	outb_p(value, info->port+offset);
}

static void send_break(	struct async_struct * info)
{
	if (!info->port)
		return;
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 25;
	serial_out(info, UART_LCR, serial_inp(info, UART_LCR) | UART_LCR_SBC);
	schedule();
	serial_out(info, UART_LCR, serial_inp(info, UART_LCR) & ~UART_LCR_SBC);
}

static inline void rs_sched_event(struct async_struct *info,
				  int event)
{
	info->event |= 1 << event;
	set_bit(info->line, rs_event);
	mark_bh(SERIAL_BH);
}


/*
 * This is the serial driver's generic interrupt routine
 */
static void rs_interrupt(int irq)
{
	unsigned char status;
	struct async_struct * info;
	struct tty_queue * queue;
	int head, tail, count, ch;
	int done;
	
	/*
	 * Just like the LEFT(x) macro, except it uses the loal tail
	 * and head variables.
	 */
#define VLEFT ((tail-head-1)&(TTY_BUF_SIZE-1))
#define IFLAG (info->tty->termios->c_iflag)
#define CFLAG (info->tty->termios->c_cflag)

	info = IRQ_ports[irq];
	done = 1;
	while (info) {
#ifdef SERIAL_INT_DEBUG
		printk("rsint(%d)...", info->line);
#endif
		if (serial_inp(info, UART_IIR) & UART_IIR_NO_INT)
			goto next_loop;
		done = 0;
		
		status = serial_inp(info, UART_LSR);
		if (status & UART_LSR_DR) {
#ifdef SERIAL_INT_DEBUG
			printk("DR...");
#endif
			queue = &info->tty->read_q;
			head = queue->head;
			tail = queue->tail;
			do {
				ch = serial_in(info, UART_RX);
				/*
				 * There must be at least 3 characters
				 * free in the queue; otherwise we punt.
				 */
				if (VLEFT < 3)
					continue;
				if (status & (UART_LSR_BI |
					      UART_LSR_FE |
					      UART_LSR_PE)) {
					if (status & (UART_LSR_BI)) {
						if (info->flags & ASYNC_SAK)
			rs_sched_event(info, RS_EVENT_DO_SAK);
						else if (IFLAG & IGNBRK)
							continue;
						else if (IFLAG & BRKINT) 
			rs_sched_event(info, RS_EVENT_BREAK_INT);
						else
							ch = 0;
					} else if (IFLAG & IGNPAR)
						continue;
					if (IFLAG & PARMRK) {
						queue->buf[head++] = 0xff;
						head &= TTY_BUF_SIZE-1;
						queue->buf[head++] = 0;
						head &= TTY_BUF_SIZE-1;
					} else
						ch = 0;
				} else if ((IFLAG & PARMRK) && (ch == 0xff)) {
					queue->buf[head++] = 0xff;
					head &= TTY_BUF_SIZE-1;
				}
				queue->buf[head++] = ch;
				head &= TTY_BUF_SIZE-1;
			} while ((status = serial_inp(info, UART_LSR)) &
				 UART_LSR_DR);
			queue->head = head;
			if ((VLEFT < RQ_THRESHOLD_LW)
			    && !set_bit(TTY_RQ_THROTTLED, &info->tty->flags)) 
				rs_throttle(info->tty, TTY_THROTTLE_RQ_FULL);
			rs_sched_event(info, RS_EVENT_READ_PROCESS);
		}
		if ((status & UART_LSR_THRE) &&
		    !info->tty->stopped) {
			queue = &info->tty->write_q;
			head = queue->head;
			tail = queue->tail;
			if (head==tail && !info->x_char)
				goto no_xmit;
			if (info->x_char) {
				serial_outp(info, UART_TX, info->x_char);
				info->x_char = 0;
			} else {
				count = info->xmit_fifo_size;
				while (count--) {
					if (tail == head)
						break;
					serial_outp(info, UART_TX,
						    queue->buf[tail++]);
					tail &= TTY_BUF_SIZE-1;
				}
			}
			queue->tail = tail;
			if (VLEFT > WAKEUP_CHARS) {
				rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
				if (info->tty->write_data_cnt) {
					set_bit(info->tty->line,
						&tty_check_write);
					mark_bh(TTY_BH);
				}
			}
			info->timer = jiffies + info->timeout;
			if (info->timer < timer_table[RS_TIMER].expires)
				timer_table[RS_TIMER].expires = info->timer;
			set_bit(info->line, rs_write_active);
			timer_active |= 1 << RS_TIMER;
#ifdef SERIAL_INT_DEBUG
			printk("THRE...");
#endif
		}
	no_xmit:
		status = serial_in(info, UART_MSR);
		
		if (!(CFLAG & CLOCAL) && (status & UART_MSR_DDCD)) {
			if (status & UART_MSR_DCD) {
#ifdef SERIAL_INT_DEBUG
				printk("DCD on...");
#endif
				rs_sched_event(info, RS_EVENT_OPEN_WAKEUP);
			} else {
#ifdef SERIAL_INT_DEBUG
				printk("DCD off...");
#endif
				rs_sched_event(info, RS_EVENT_HUP_PGRP);
			}
		}
		if (CFLAG & CRTSCTS) {
			if (info->tty->stopped) {
				if (status & UART_MSR_CTS) {
					info->tty->stopped = 0;
					restart_port(info);
				}
			} else 
				info->tty->stopped = !(status & UART_MSR_CTS);
		}
	next_loop:
		info = info->next_port;
		if (!info && !done) {
#ifdef SERIAL_INT_DEBUG
			printk("repeating...");
#endif
			info = IRQ_ports[irq];
			done = 1;
		}
	}
}

#ifdef CONFIG_AUTO_IRQ
/*
 * This is the serial driver's interrupt routine while we are probing
 * for submarines.
 */
static volatile int rs_irq_triggered;
static volatile int rs_triggered;

static void rs_probe(int irq)
{
	rs_irq_triggered = irq;
	rs_triggered |= 1 << irq;
	return;
}
#endif

static void do_softint()
{
	int			i;
	struct async_struct	*info;
	
	for (i = 0, info = rs_table; i < NR_PORTS; i++,info++) {
		if (!clear_bit(i, rs_event)) {
			if (!info->tty)	
				continue;
			if (!clear_bit(RS_EVENT_READ_PROCESS, &info->event)) {
				TTY_READ_FLUSH(info->tty);
			}
			if (!clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event)) {
				wake_up_interruptible(&info->tty->write_q.proc_list);
			}
			if (!clear_bit(RS_EVENT_HUP_PGRP, &info->event))
				tty_hangup(info->tty);
			if (!clear_bit(RS_EVENT_BREAK_INT, &info->event)) {
				flush_input(info->tty);
				flush_output(info->tty);
				if (info->tty->pgrp > 0)
					kill_pg(info->tty->pgrp, SIGINT,1);
			}
			if (!clear_bit(RS_EVENT_DO_SAK, &info->event)) {
				do_SAK(info->tty);
			}
			if (!clear_bit(RS_EVENT_OPEN_WAKEUP, &info->event)) {
				wake_up_interruptible(&info->open_wait);
			}
		}
	}
}

/*
 * This subroutine handles all of the timer functionality required for
 * the serial ports.
 */

static void rs_timer(void)
{
	int			i;
	struct async_struct	*info;

	for (i = 0, info = rs_table; i < NR_PORTS; i++,info++) {
		if (test_bit(i, rs_write_active) && (info->timer <= jiffies)) {
			clear_bit(i, rs_write_active);
			rs_write(info->tty);
		}
	}
}

/*
 * Note: this subroutine must be called with the interrupts *off*
 */
static void restart_port(struct async_struct *info)
{
	struct tty_queue * queue;
	int head, tail, count;
	
	if (serial_inp(info, UART_LSR) & UART_LSR_THRE) {
		if (info->x_char) {
			serial_outp(info, UART_TX, info->x_char);
			info->x_char = 0;
		} else {
			queue = &info->tty->write_q;
			head = queue->head;
			tail = queue->tail;
			count = info->xmit_fifo_size;
			while (count--) {
				if (tail == head)
					break;
				serial_outp(info, UART_TX, queue->buf[tail++]);
				tail &= TTY_BUF_SIZE-1;
			}
			queue->tail = tail;
		}
	}
}	

/*
 * This routine gets called when tty_write has put something into
 * the write_queue.  
 */
void rs_write(struct tty_struct * tty)
{
	struct async_struct *info;

	if (!tty || tty->stopped)
		return;
	info = rs_table + DEV_TO_SL(tty->line);
	cli();
	restart_port(info);
	sti();
}

static void rs_throttle(struct tty_struct * tty, int status)
{
	struct async_struct *info;
	unsigned char mcr;

#ifdef notdef
	printk("throttle tty%d: %d (%d, %d)....\n", DEV_TO_SL(tty->line),
	       status, LEFT(&tty->read_q), LEFT(&tty->secondary));
#endif
	switch (status) {
	case TTY_THROTTLE_RQ_FULL:
		info = rs_table + DEV_TO_SL(tty->line);
		if (tty->termios->c_iflag & IXOFF) {
			info->x_char = STOP_CHAR(tty);
		} else {
			mcr = serial_inp(info, UART_MCR);
			mcr &= ~UART_MCR_RTS;
			serial_out(info, UART_MCR, mcr);
		}
		break;
	case TTY_THROTTLE_RQ_AVAIL:
		info = rs_table + DEV_TO_SL(tty->line);
		if (tty->termios->c_iflag & IXOFF) {
			cli();
			if (info->x_char)
				info->x_char = 0;
			else
				info->x_char = START_CHAR(tty);
			sti();
		} else {
			mcr = serial_in(info, UART_MCR);
			mcr |= UART_MCR_RTS;
			serial_out(info, UART_MCR, mcr);
		}
		break;
	}
}

/*
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct async_struct * info;
	int irq, line;

	line = DEV_TO_SL(tty->line);
	if ((line < 0) || (line >= NR_PORTS))
		return;
	info = rs_table + line;
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_close ttys%d, count = %d\n", info->line, info->count);
#endif
	if (--info->count > 0)
		return;
	tty->stopped = 0;		/* Force flush to succeed */
	wait_until_sent(tty);
	clear_bit(line, rs_write_active);
	clear_bit(line, rs_event);
	info->event = 0;
	info->count = 0;
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	if (info->blocked_open) {
		shutdown(info);
		startup(info);
		return;
	}
	if (info->flags & ASYNC_INITIALIZED) {
		shutdown(info);
		irq = info->irq;
		if (irq == 2)
			irq = 9;
		if (irq) {
			if (info->next_port)
				info->next_port->prev_port = info->prev_port;
			if (info->prev_port)
				info->prev_port->next_port = info->next_port;
			else
				IRQ_ports[irq] = info->next_port;
			if (!IRQ_ports[irq])
				free_irq(irq);
		}
	}
	info->tty = 0;
}

static void startup(struct async_struct * info)
{
	unsigned short ICP;
	unsigned long flags;

	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));

	/*
	 * First, clear the FIFO buffers and disable them
	 */
	if (info->type == PORT_16550A)
		serial_outp(info, UART_FCR, UART_FCR_CLEAR_CMD);

	/*
	 * Next, clear the interrupt registers.
	 */
	(void)serial_inp(info, UART_LSR);
	(void)serial_inp(info, UART_RX);
	(void)serial_inp(info, UART_IIR);
	(void)serial_inp(info, UART_MSR);

	/*
	 * Now, initialize the UART 
	 */
	serial_outp(info, UART_LCR, UART_LCR_WLEN8);	/* reset DLAB */
	if (info->flags & ASYNC_FOURPORT) 
		serial_outp(info, UART_MCR, UART_MCR_DTR | UART_MCR_RTS);
	else
		serial_outp(info, UART_MCR,
			    UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
	
	/*
	 * Enable FIFO's if necessary
	 */
	if (info->type == PORT_16550A) {
		serial_outp(info, UART_FCR, UART_FCR_SETUP_CMD);
		info->xmit_fifo_size = 16;
	} else {
		info->xmit_fifo_size = 1;
	}

	/*
	 * Finally, enable interrupts
	 */
	serial_outp(info, UART_IER, 0x0f);	/* enable all intrs */
	if (info->flags & ASYNC_FOURPORT) {
		/* Enable interrupts on the AST Fourport board */
		ICP = (info->port & 0xFE0) | 0x01F;
		outb_p(0x80, ICP);
		(void) inb_p(ICP);
	}

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void)serial_inp(info, UART_LSR);
	(void)serial_inp(info, UART_RX);
	(void)serial_inp(info, UART_IIR);
	(void)serial_inp(info, UART_MSR);

	info->flags |= ASYNC_INITIALIZED;
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}

static void shutdown(struct async_struct * info)
{
	unsigned long flags;

	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	serial_outp(info, UART_IER, 0x00);	/* disable all intrs */
	if (info->tty && !(info->tty->termios->c_cflag & HUPCL))
		serial_outp(info, UART_MCR, UART_MCR_DTR);
	else
		/* reset DTR,RTS,OUT_2 */		
		serial_outp(info, UART_MCR, 0x00);
	serial_outp(info, UART_FCR, UART_FCR_CLEAR_CMD); /* disable FIFO's */
	(void)serial_in(info, UART_RX);    /* read data port to reset things */
	info->flags &= ~ASYNC_INITIALIZED;
	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}

static void change_speed(unsigned int line)
{
	struct async_struct * info;
	unsigned short port;
	int	quot = 0;
	unsigned cflag,cval,mcr;
	int	i;

	if (line >= NR_PORTS)
		return;
	info = rs_table + line;
	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (!(port = info->port))
		return;
	i = cflag & CBAUD;
	if (i == 15) {
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			i += 1;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			i += 2;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST)
			quot = info->custom_divisor;
	}
	if (quot) {
		info->timeout = ((info->xmit_fifo_size*HZ*15*quot) /
				 info->baud_base) + 2;
	} else if (baud_table[i] == 134) {
		quot = (2*info->baud_base / 269);
		info->timeout = (info->xmit_fifo_size*HZ*30/269) + 2;
	} else if (baud_table[i]) {
		quot = info->baud_base / baud_table[i];
		info->timeout = (info->xmit_fifo_size*HZ*15/baud_table[i]) + 2;
	} else {
		quot = 0;
		info->timeout = 0;
	}
	mcr = serial_in(info, UART_MCR);
	if (quot) 
		serial_out(info, UART_MCR, mcr | UART_MCR_DTR);
	else {
		serial_out(info, UART_MCR, mcr & ~UART_MCR_DTR);
		return;
	}
	/* byte size and parity */
	cval = cflag & (CSIZE | CSTOPB);
	cval >>= 4;
	if (cflag & PARENB)
		cval |= 8;
	if (!(cflag & PARODD))
		cval |= 16;
	cli();
	serial_outp(info, UART_LCR, cval | UART_LCR_DLAB);	/* set DLAB */
	serial_outp(info, UART_DLL, quot & 0xff);	/* LS of divisor */
	serial_outp(info, UART_DLM, quot >> 8);		/* MS of divisor */
	serial_outp(info, UART_LCR, cval);		/* reset DLAB */
	sti();
}

static int get_serial_info(struct async_struct * info,
			   struct serial_struct * retinfo)
{
	struct serial_struct tmp;
  
	if (!retinfo)
		return -EFAULT;
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->port;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	memcpy_tofs(retinfo,&tmp,sizeof(*retinfo));
	return 0;
}

static int set_serial_info(struct async_struct * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new;
	unsigned int		irq,check_irq;
	int 			retval;
	struct 			sigaction sa;
	struct async_struct	old_info;

	if (!new_info)
		return -EFAULT;
	memcpy_fromfs(&new,new_info,sizeof(new));

	check_irq = 0;
	old_info = *info;
	if (!suser()) {
		info->flags = ((info->flags & ~ASYNC_SPD_MASK) |
			       (new.flags & ASYNC_SPD_MASK));
		info->custom_divisor = new.custom_divisor;
		new.port = 0;	/* Prevent initialization below */
		goto check_and_exit;
	}

	if ((new.irq > 15) || (new.port > 0xffff) ||
	    (new.type < PORT_UNKNOWN) || (new.type > PORT_MAX)) {
		return -EINVAL;
	}
	
	info->baud_base = new.baud_base;
	info->flags = ((info->flags & ~ASYNC_FLAGS) |
			(new.flags & ASYNC_FLAGS));
	info->custom_divisor = new.custom_divisor;
	info->type = new.type;
	
	if (new.irq == 2)
		new.irq = 9;
	irq = info->irq;
	if (irq == 2)
		irq = 9;
	
	/*
	 * If necessary, first we try to grab the new IRQ for serial
	 * interrupts.  (We have to do this early, since we may get an
	 * error trying to do this.)
	 */
	if (new.port && new.irq && info->type &&
	    ((irq != new.irq) || !(info->flags & ASYNC_INITIALIZED))) {
		if (!IRQ_ports[new.irq]) {
			sa.sa_handler = rs_interrupt;
			sa.sa_flags = (SA_INTERRUPT);
			sa.sa_mask = 0;
			sa.sa_restorer = NULL;
			retval = irqaction(new.irq,&sa);
			if (retval) {
				*info = old_info;
				return retval;
			}
		}
	}

	if ((new.irq != irq) ||
	    (new.port != info->port)) {
		/*
		 * We need to shutdown the serial port at the old
		 * port/irq combination.
		 */
		if (info->flags & ASYNC_INITIALIZED) {
			shutdown(info);
			if (info->next_port)
				info->next_port->prev_port = info->prev_port;
			if (info->prev_port)
				info->prev_port->next_port = info->next_port;
			else
				IRQ_ports[irq] = info->next_port;
			check_irq = irq; /* Check later if we need to */
					 /* free the IRQ */
		}
		info->irq = new.irq;
		info->port = new.port;
	}
	
check_and_exit:
	if (new.port && new.irq && info->type &&
	    !(info->flags & ASYNC_INITIALIZED)) {
		/*
		 * Link the port into the new interrupt chain.
		 */
		info->prev_port = 0;
		info->next_port = IRQ_ports[info->irq];
		if (info->next_port)
			info->next_port->prev_port = info;
		IRQ_ports[info->irq] = info;
		startup(info);
		change_speed(info->line);
	} else if (((old_info.flags & ASYNC_SPD_MASK) !=
		    (info->flags & ASYNC_SPD_MASK)) ||
		   (old_info.custom_divisor != info->custom_divisor))
		change_speed(info->line);

	if (check_irq && !IRQ_ports[check_irq])
		free_irq(check_irq);
	
	return 0;
}

static int get_modem_info(struct async_struct * info, unsigned int *value)
{
	unsigned char control, status;
	unsigned int result;

	control = serial_in(info, UART_MCR);
	status = serial_in(info, UART_MSR);
	result =  ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
		| ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
		| ((status  & UART_MSR_DCD) ? TIOCM_CAR : 0)
		| ((status  & UART_MSR_RI) ? TIOCM_RNG : 0)
		| ((status  & UART_MSR_DSR) ? TIOCM_DSR : 0)
		| ((status  & UART_MSR_CTS) ? TIOCM_CTS : 0);
	put_fs_long(result,(unsigned long *) value);
	return 0;
}

static int set_modem_info(struct async_struct * info, unsigned int cmd,
			  unsigned int *value)
{
	unsigned char control;
	unsigned int arg = get_fs_long((unsigned long *) value);
	
	control = serial_in(info, UART_MCR);

	switch (cmd) {
		case TIOCMBIS:
			if (arg & TIOCM_RTS)
				control |= UART_MCR_RTS;
			if (arg & TIOCM_DTR)
				control |= UART_MCR_DTR;
			break;
		case TIOCMBIC:
			if (arg & TIOCM_RTS)
				control &= ~UART_MCR_RTS;
			if (arg & TIOCM_DTR)
				control &= ~UART_MCR_DTR;
			break;
		case TIOCMSET:
			control = (control & ~0x03)
				| ((arg & TIOCM_RTS) ? UART_MCR_RTS : 0)
				| ((arg & TIOCM_DTR) ? UART_MCR_DTR : 0);
			break;
		default:
			return -EINVAL;
	}
	serial_out(info, UART_MCR, control);
	return 0;
}

static int rs_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned int arg)
{
	int	line;
	struct async_struct * info;

	line = DEV_TO_SL(tty->line);
	if (line < 0 || line >= NR_PORTS)
		return -ENODEV;
	info = rs_table + line;
	
	switch (cmd) {
		case TCSBRK:
			wait_until_sent(tty);
			if (!arg)
				send_break(info);
			return 0;
		case TIOCMGET:
			verify_area((void *) arg,sizeof(unsigned int *));
			return get_modem_info(info, (unsigned int *) arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return set_modem_info(info, cmd, (unsigned int *) arg);
		case TIOCGSERIAL:
			verify_area((void *) arg,sizeof(struct serial_struct));
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		
	default:
		return -EINVAL;
	}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;

	change_speed(DEV_TO_SL(tty->line));
	
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->stopped = 0;
		rs_write(tty);
	}

	if (!(old_termios->c_cflag & CLOCAL) &&
	    (tty->termios->c_cflag & CLOCAL))
		wake_up_interruptible(&rs_table[DEV_TO_SL(tty->line)].open_wait);
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-speicific
 * initalization for the tty structure.
 */
int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct async_struct	*info;
	int 			irq, retval, line;
	struct sigaction	sa;

	line = DEV_TO_SL(tty->line);
	if ((line < 0) || (line >= NR_PORTS))
		return -ENODEV;
	info = rs_table + line;
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open ttys%d, count = %d\n", info->line, info->count);
#endif
	info->count++;
	info->tty = tty;
	
	tty->write = rs_write;
	tty->close = rs_close;
	tty->ioctl = rs_ioctl;
	tty->throttle = rs_throttle;
	tty->set_termios = rs_set_termios;

	if (!(info->flags & ASYNC_INITIALIZED)) {
		if (!info->port || !info->irq || !info->type) {
			set_bit(TTY_IO_ERROR, &tty->flags);
			return 0;
		}
		irq = info->irq;
		if (irq == 2)
			irq = 9;
		if (!IRQ_ports[irq]) {
			sa.sa_handler = rs_interrupt;
			sa.sa_flags = (SA_INTERRUPT);
			sa.sa_mask = 0;
			sa.sa_restorer = NULL;
			retval = irqaction(irq,&sa);
			if (retval)
				return retval;
		}
		/*
		 * Link in port to IRQ chain
		 */
		info->prev_port = 0;
		info->next_port = IRQ_ports[irq];
		if (info->next_port)
			info->next_port->prev_port = info;
		IRQ_ports[irq] = info;
		
		startup(info);
		change_speed(info->line);
	}

	retval = block_til_ready(tty, filp, info);
	if (retval)
		return retval;
	
	return 0;

}

static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct async_struct *info)
{
	struct wait_queue wait = { current, NULL };
	int	retval;
	
	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (MAJOR(filp->f_rdev) == 5) {
		if (info->flags & ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		info->flags |= ASYNC_CALLOUT_ACTIVE;
		return 0;
	}
	
	/*
	 * If non-blocking mode is set, then make the check up front
	 * and then exit.
	 */
	if (filp->f_flags & O_NONBLOCK) {
		if (info->flags & ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
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
	info->count--;
	info->blocked_open++;
	while (1) {
		serial_out(info, UART_MCR,
			   serial_inp(info, UART_MCR) | UART_MCR_DTR);
		current->state = TASK_INTERRUPTIBLE;
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    ((tty->termios->c_cflag & CLOCAL) ||
		     (serial_in(info, UART_MSR) & UART_MSR_DCD)))
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
	info->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttys%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	tty_unhangup(filp);	/* To make sure fops is OK */
	return 0;
}	


static void show_serial_version()
{
	printk("Serial driver version 3.91 with");
#ifdef CONFIG_AST_FOURPORT
	printk(" AST_FOURPORT");
#define SERIAL_OPT
#endif
#ifdef CONFIG_ACCENT_ASYNC
	printk(" ACCENT_ASYNC");
#define SERIAL_OPT
#endif
#ifdef CONFIG_AUTO_IRQ
	printk (" AUTO_IRQ");
#define SERIAL_OPT
#endif
#ifdef SERIAL_OPT
	printk(" enabled\n");
#else
	printk(" no serial options enabled\n");
#endif
#undef SERIAL_OPT
}

#ifdef CONFIG_AUTO_IRQ
static int get_auto_irq(struct async_struct *info)
{
	unsigned char save_MCR, save_IER, save_ICP=0;
	unsigned short ICP=0, port = info->port;
	unsigned long timeout;
	
	/*
	 * Enable interrupts and see who answers
	 */
	rs_irq_triggered = 0;
	save_IER = serial_inp(info, UART_IER);
	save_MCR = serial_inp(info, UART_MCR);
	if (info->flags & ASYNC_FOURPORT)  {
		serial_outp(info, UART_MCR, UART_MCR_DTR | UART_MCR_RTS);
		serial_outp(info, UART_IER, 0x0f);	/* enable all intrs */
		ICP = (port & 0xFE0) | 0x01F;
		save_ICP = inb_p(ICP);
		outb_p(0x80, ICP);
		(void) inb_p(ICP);
	} else {
		serial_outp(info, UART_MCR,
			    UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
		serial_outp(info, UART_IER, 0x0f);	/* enable all intrs */
	}
	/*
	 * Next, clear the interrupt registers.
	 */
	(void)serial_inp(info, UART_LSR);
	(void)serial_inp(info, UART_RX);
	(void)serial_inp(info, UART_IIR);
	(void)serial_inp(info, UART_MSR);
	
	timeout = jiffies+2;
	while (timeout >= jiffies) {
		if (rs_irq_triggered)
			break;
	}
	/*
	 * Now check to see if we got any business, and clean up.
	 */
	serial_outp(info, UART_IER, save_IER);
	serial_outp(info, UART_MCR, save_MCR);
	if (info->flags & ASYNC_FOURPORT)
		outb_p(save_ICP, ICP);
	return(rs_irq_triggered);
}
#endif


static void init(struct async_struct * info)
{
	unsigned char status1, status2, scratch, scratch2;
	unsigned port = info->port;
#ifdef CONFIG_AUTO_IRQ
	int retries;
	
	if (!port)
		return;

	scratch2 = 0;
	for (retries = 0; retries < 5; retries++) {
		if (!scratch)
			scratch = get_auto_irq(info);
		if (!scratch2)
			scratch2 = get_auto_irq(info);
		if (scratch && scratch2) {
			if (scratch == scratch2)
				break;
			scratch = scratch2 = 0;
		}
	}
	if (scratch && (scratch == scratch2))
		info->irq = scratch;
	else {
		info->type = PORT_UNKNOWN;
		return;
	}
	
#else /* CONFIG_AUTO_IRQ */

	if (!port)
		return;
			
	/* 
	 * Check to see if a UART is really there.  
	 */
	scratch = serial_inp(info, UART_MCR);
	serial_outp(info, UART_MCR, UART_MCR_LOOP | scratch);
	scratch2 = serial_inp(info, UART_MSR);
	serial_outp(info, UART_MCR, UART_MCR_LOOP | 0x0A);
	status1 = serial_inp(info, UART_MSR) & 0xF0;
	serial_outp(info, UART_MCR, scratch);
	serial_outp(info, UART_MSR, scratch2);
	if (status1 != 0x90) {
		info->type = PORT_UNKNOWN;
		return;
	}
#endif /* CONFIG_AUTO_IRQ */
	
	outb_p(UART_FCR_ENABLE_FIFO, UART_FCR + port);
	scratch = inb(UART_IIR + port) >> 6;
	info->xmit_fifo_size = 1;
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
			info->xmit_fifo_size = 16;
			break;
	}
	if (info->type == PORT_16450) {
		scratch = inb(UART_SCR + port);
		outb_p(0xa5, UART_SCR + port);
		status1 = inb(UART_SCR + port);
		outb_p(0x5a, UART_SCR + port);
		status2 = inb(UART_SCR + port);
		outb_p(scratch, UART_SCR + port);
		if ((status1 != 0xa5) || (status2 != 0x5a))
			info->type = PORT_8250;
	}
	shutdown(info);
}

long rs_init(long kmem_start)
{
	int i;
	struct async_struct * info;
#ifdef CONFIG_AUTO_IRQ
	int irq_lines = 0;
	struct sigaction sa;
	unsigned long timeout;
	
	/*
	 *  We will be auto probing for irq's, so turn on interrupts now!
	 */
	sti();
	
	rs_triggered = 0;
	sa.sa_handler = rs_probe;
	sa.sa_flags = (SA_INTERRUPT);
	sa.sa_mask = 0;
	sa.sa_restorer = NULL;
#endif	
	timer_table[RS_TIMER].fn = rs_timer;
	timer_table[RS_TIMER].expires = 0;
	
	for (i = 0; i < 16; i++) {
		IRQ_ports[i] = 0;
#ifdef CONFIG_AUTO_IRQ
		if (!irqaction(i, &sa))
			irq_lines |= 1 << i;
#endif
	}
#ifdef CONFIG_AUTO_IRQ
	timeout = jiffies+5;
	while (timeout >= jiffies)
		;
	for (i = 0; i < 16; i++) {
		if ((rs_triggered & (1 << i)) &&
		    (irq_lines & (1 << i))) {
			irq_lines &= ~(1 << i);
			printk("Wild interrupt?  (IRQ %d)\n", i);
			free_irq(i);
		}
	}
#endif
	show_serial_version();
	for (i = 0, info = rs_table; i < NR_PORTS; i++,info++) {
		info->line = i;
		info->tty = 0;
		info->type = PORT_UNKNOWN;
		info->timer = 0;
		info->custom_divisor = 0;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		info->open_wait = 0;
		info->next_port = 0;
		info->prev_port = 0;
		init(info);
		if (info->type == PORT_UNKNOWN)
			continue;
		printk("tty%02d%s at 0x%04x (irq = %d)", info->line, 
		       (info->flags & ASYNC_FOURPORT) ? " FourPort" : "",
		       info->port, info->irq);
		switch (info->type) {
			case PORT_8250:
				printk(" is a 8250\n");
				break;
			case PORT_16450:
				printk(" is a 16450\n");
				break;
			case PORT_16550:
				printk(" is a 16550\n");
				break;
			case PORT_16550A:
				printk(" is a 16550A\n");
				break;
			default:
				printk("\n");
				break;
		}
	}
#ifdef CONFIG_AUTO_IRQ
	/*
	 * Turn interrupts back off, since they were off when we
	 * started this.  See start_kernel() in init/main.c.
	 */
	cli();
	for (i = 0; i < 16; i++) {
		if (irq_lines & (1 << i))
			free_irq(i);
	}
#endif
	bh_base[SERIAL_BH].routine = do_softint;
	memset(&rs_event, 0, sizeof(rs_event));
	memset(&rs_write_active, 0, sizeof(rs_write_active));
	return kmem_start;
}


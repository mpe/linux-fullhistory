/*
 *  linux/kernel/serial.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Extensively rewritten by Theodore Ts'o, 8/16/92 -- 9/14/92.  Now
 *  much more extensible to support other serial cards based on the
 *  16450/16550A UART's.  Added support for the AST FourPort and the
 *  Accent Async board.  We use the async_ISR structure to allow
 *  multiple ports (or boards, if the hardware permits) to share a
 *  single IRQ channel.
 *
 * This module exports the following rs232 io functions:
 *
 *	long rs_init(long);
 * 	int  rs_open(struct tty_struct * tty, struct file * filp)
 * 	void change_speed(unsigned int line)
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/serial.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define WAKEUP_CHARS (3*TTY_BUF_SIZE/4)

/*
 * rs_read_process	- Bitfield of serial lines that have I/O processing
 *				to be done at the next clock tick.
 * rs_write_timeout	- Bitfield of serial lines that have a possible 
 *				write timeout pending.  (In case the THRE
 *				interrupt gets lost.)
 */
static unsigned long rs_read_process = 0; 
static unsigned long rs_write_timeout = 0;

static void UART_ISR_proc(async_ISR ISR, int line);
static void FourPort_ISR_proc(async_ISR ISR, int line);

struct struct_ISR COM1_ISR = { 4, 0x3f8, UART_ISR_proc, 0, };
struct struct_ISR COM2_ISR = { 3, 0x2f8, UART_ISR_proc, 0, };
struct struct_ISR COM3_ISR = { 4, 0x3e8, UART_ISR_proc, 0, };
struct struct_ISR COM4_ISR = { 3, 0x2e8, UART_ISR_proc, 0, };

struct struct_ISR FourPort1_ISR = { 2, 0x1bf, FourPort_ISR_proc, 0, };
struct struct_ISR FourPort2_ISR = { 5, 0x2bf, FourPort_ISR_proc, 0, };

struct struct_ISR Accent3_ISR = { 4, 0x330, UART_ISR_proc, 0, };
struct struct_ISR Accent4_ISR = { 4, 0x338, UART_ISR_proc, 0, };

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD ( 1843200 / 16 ) 

struct async_struct rs_table[] = {
	{ BASE_BAUD, 0x3F8, &COM1_ISR, 0, },
	{ BASE_BAUD, 0x2F8, &COM2_ISR, 0, },
	{ BASE_BAUD, 0x3E8, &COM3_ISR, 0, },
	{ BASE_BAUD, 0x2E8, &COM4_ISR, 0, },
	
	{ BASE_BAUD, 0x1A0, &FourPort1_ISR, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x1A8, &FourPort1_ISR, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x1B0, &FourPort1_ISR, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x1B8, &FourPort1_ISR, ASYNC_FOURPORT | ASYNC_NOSCRATCH },

	{ BASE_BAUD, 0x2A0, &FourPort2_ISR, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x2A8, &FourPort2_ISR, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x2B0, &FourPort2_ISR, ASYNC_FOURPORT },
	{ BASE_BAUD, 0x2B8, &FourPort2_ISR, ASYNC_FOURPORT | ASYNC_NOSCRATCH },

	{ BASE_BAUD, 0x330, &Accent3_ISR, 0 },
	{ BASE_BAUD, 0x338, &Accent4_ISR, 0 },
};

#define NR_PORTS	(sizeof(rs_table)/sizeof(struct async_struct))

static async_ISR IRQ_ISR[16];	/* Used to store the head of the */
				/* ISR linked list chain for each IRQ */

/*
 * This is used to figure out the divsor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 56000, 115200, 0 };

static void send_break(	struct async_struct * info)
{
	unsigned short port;

	if (!(port = info->port))
		return;
	port += UART_LCR;
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 25;
	outb_p(inb_p(port) | UART_LCR_SBC, port);
	schedule();
	outb_p(inb_p(port) & ~UART_LCR_SBC, port);
}

/*
 * There are several races here: we avoid most of them by disabling
 * timer_active for the crucial part of the process.. That's a good
 * idea anyway. 
 *
 * The problem is that we have to output characters /both/ from interrupts
 * and from the normal write: the latter to be sure the interrupts start up
 * again. With serial lines, the interrupts can happen so often that the
 * races actually are noticeable.
 */
static void send_intr(struct async_struct * info)
{
	unsigned short port = info->port;
	int line = info->line;
	struct tty_queue * queue = &info->tty->write_q;
	int c, count = 0;

	if (info->tty->stopped)
		return;

	if (info->type == PORT_16550A)
		count = 16;	
	else
		count = 1;
	
	rs_write_timeout &= ~(1 << line);

	if (inb_p(UART_LSR + info->port) & UART_LSR_THRE) {
		while (count-- && !info->tty->stopped) {
			if (queue->tail == queue->head)
				goto end_send;
			c = queue->buf[queue->tail];
			queue->tail++;
			queue->tail &= TTY_BUF_SIZE-1;
			outb(c, UART_TX + port);
		}
	}
	info->timer = jiffies + info->timeout;
	if (info->timer < timer_table[RS_TIMER].expires)
		timer_table[RS_TIMER].expires = info->timer;
	rs_write_timeout |= 1 << line;
	timer_active |= 1 << RS_TIMER;
end_send:
	if (LEFT(queue) > WAKEUP_CHARS)
		wake_up(&queue->proc_list);
}

static void receive_intr(struct async_struct * info)
{
	unsigned short port = info->port;
	struct tty_queue * queue = &info->tty->read_q;
	int head = queue->head;
	int maxhead = (queue->tail-1) & (TTY_BUF_SIZE-1);
	int count = 0;

	rs_read_process &= ~(1 << info->line);
	do {
		count++;
		queue->buf[head] = inb(UART_TX + port);
		if (head != maxhead) {
			head++;
			head &= TTY_BUF_SIZE-1;
		}
	} while (inb(UART_LSR + port) & UART_LSR_DR);
	queue->head = head;
	rs_read_process |= 1 << info->line;
	timer_table[RS_TIMER].expires = 0;
	timer_active |= 1<<RS_TIMER;
}

static void line_status_intr(struct async_struct * info)
{
	unsigned char status = inb(UART_LSR + info->port);

/*	printk("line status: %02x\n",status);  */
}

static void modem_status_intr(struct async_struct * info)
{
	unsigned char status = inb(UART_MSR + info->port);

	if (!(info->tty->termios->c_cflag & CLOCAL)) {
		if (((status & (UART_MSR_DCD|UART_MSR_DDCD)) == UART_MSR_DDCD)
		    && info->tty->pgrp > 0)
			kill_pg(info->tty->pgrp,SIGHUP,1);

		if (info->tty->termios->c_cflag & CRTSCTS)
			info->tty->stopped = !(status & UART_MSR_CTS);

		if (!info->tty->stopped)
			send_intr(info);
	}
}

static void (*jmp_table[4])(struct async_struct *) = {
	modem_status_intr,
	send_intr,
	receive_intr,
	line_status_intr
};

/*
 * This ISR handles the COM1-4 8250, 16450, and 16550A UART's.  It is
 * also called by the FourPort ISR, since the FourPort also uses the
 * same National Semiconduct UART's, with some interrupt multiplexing
 * thrown in.
 */
static void UART_ISR_proc(async_ISR ISR, int line)
{
	unsigned char ident;
	struct async_struct * info = rs_table + line;

	if (!info || !info->tty || !info->port)
		  return;
	while (1) {
		ident = inb(UART_IIR + info->port) & 7;
		if (ident & 1)
			return;
		ident = ident >> 1;
		jmp_table[ident](info);
	}
}

/*
 * Again, we disable interrupts to be sure there aren't any races:
 * see send_intr for details.
 */
static inline void do_rs_write(struct async_struct * info)
{
	if (!info->tty || !info->port)
		return;
	if (EMPTY(&info->tty->write_q))
		return;
	cli();
	send_intr(info);
	sti();
}

/*
 * Here is the fourport ISR
 */
static void FourPort_ISR_proc(async_ISR ISR, int line)
{
	int		i;
	unsigned char ivec;

	ivec = ~inb(ISR->port) & 0x0F;
	for (i = line; ivec; i++) {
		if (ivec & 1)
			UART_ISR_proc(ISR, i);
		ivec = ivec >> 1;
	}
}

/*
 * This is the serial driver's generic interrupt routine
 */
static void rs_interrupt(int irq)
{
	async_ISR p = IRQ_ISR[irq];

	while (p) {
		(p->ISR_proc)(p, p->line);
		p = p->next_ISR;
	}
}

/*
 * This subroutine handles all of the timer functionality required for
 * the serial ports.
 */

#define END_OF_TIME 0xffffffff
static void rs_timer(void)
{
	unsigned long		mask;
	struct async_struct	*info;
	unsigned long		next_timeout;

	info = rs_table;
	next_timeout = END_OF_TIME;
	for (mask = 1 ; mask ; info++, mask <<= 1) {
		if ((mask > rs_read_process) &&
		    (mask > rs_write_timeout))
			break;
		if (mask & rs_read_process) {
			TTY_READ_FLUSH(info->tty);
			rs_read_process &= ~mask;
		}
		if (mask & rs_write_timeout) {
			if (info->timer > jiffies) {
				rs_write_timeout &= ~mask;
				do_rs_write(info);
			}
			if ((mask & rs_write_timeout) &&
			    (info->timer < next_timeout))
				next_timeout = info->timer;
		}
	}
	if (next_timeout != END_OF_TIME) {
		timer_table[RS_TIMER].expires = next_timeout;
		timer_active |= 1 << RS_TIMER;
	}
}

static void init(struct async_struct * info)
{
	unsigned char status1, status2, scratch, scratch2;
	unsigned short port = info->port;

	/* 
	 * Check to see if a UART is really there.  
	 */
	scratch = inb_p(UART_MCR + port);
	outb_p(UART_MCR_LOOP | scratch, UART_MCR + port);
	scratch2 = inb_p(UART_MSR + port);
	outb_p(UART_MCR_LOOP | 0x0A, UART_MCR + port);
	status1 = inb_p(UART_MSR + port) & 0xF0;
	outb_p(scratch, UART_MCR + port);
	outb_p(scratch, UART_MSR + port);
	if (status1 != 0x90) {
		info->type = PORT_UNKNOWN;
		return;
	}
	
	if (!(info->flags & ASYNC_NOSCRATCH)) {
		scratch = inb(UART_SCR + port);
		outb_p(0xa5, UART_SCR + port);
		status1 = inb(UART_SCR + port);
		outb_p(0x5a, UART_SCR + port);
		status2 = inb(UART_SCR + port);
		outb_p(scratch, UART_SCR + port);
        } else {
	  	status1 = 0xa5;
		status2 = 0x5a;
	}
	if (status1 == 0xa5 && status2 == 0x5a) {
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
	} else
		info->type = PORT_8250;
	change_speed(info->line);
	outb_p(0x00,		UART_IER + port);	/* disable all intrs */
	outb_p(0x00,		UART_MCR + port);  /* reset DTR,RTS,OUT_2 */
	(void)inb(UART_RX + port);     /* read data port to reset things (?) */
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 */
void rs_write(struct tty_struct * tty)
{
	int line = tty->line - 64;

	do_rs_write(rs_table+line);
}

/*
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * ISR from the interrupt chain if necessary, and we free that IRQ if
 * nothing is left in the chain.
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct async_struct * info;
	async_ISR		ISR;
	int irq, line;

	line = DEV_TO_SL(tty->line);
	if ((line < 0) || (line >= NR_PORTS))
		return;
	wait_until_sent(tty);
	info = rs_table + line;
	if (!info->port)
		return;
	info->tty = 0;
	outb_p(0x00, UART_IER + info->port);	/* disable all intrs */
	outb_p(0x00, UART_MCR + info->port);	/* reset DTR, RTS, */
	outb(UART_FCR_CLEAR_CMD, UART_FCR + info->port); /* disable FIFO's */
	ISR = info->ISR;
	irq = ISR->irq;
	if (irq == 2)
		irq = 9;
	if (--ISR->refcnt == 0) {
		if (ISR->next_ISR)
			ISR->next_ISR->prev_ISR = ISR->prev_ISR;
		if (ISR->prev_ISR)
			ISR->prev_ISR->next_ISR = ISR->next_ISR;
		else
			IRQ_ISR[irq] = ISR->next_ISR;
		if (!IRQ_ISR[irq])
			free_irq(irq);
	}
}

static void startup(struct async_struct * info)
{
	unsigned short port = info->port;
	unsigned short ICP;

	/*
	 * First, clear the FIFO buffers and disable them
	 */
	if (info->type == PORT_16550A)
		outb_p(UART_FCR_CLEAR_CMD, UART_FCR + port);

	/*
	 * Next, clear the interrupt registers.
	 */
	(void)inb_p(UART_LSR + port);
	(void)inb_p(UART_RX + port);
	(void)inb_p(UART_IIR + port);
	(void)inb_p(UART_MSR + port);

	/*
	 * Now, initialize the UART 
	 */
	outb_p(UART_LCR_WLEN8, UART_LCR + port);	/* reset DLAB */
	if (info->flags & ASYNC_FOURPORT) 
		outb_p(UART_MCR_DTR | UART_MCR_RTS, 
		       UART_MCR + port);
	else
		outb_p(UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2, 
		       UART_MCR + port);
	
	/*
	 * Enable FIFO's if necessary
	 */
	if (info->type == PORT_16550A) {
		outb_p(UART_FCR_SETUP_CMD, UART_FCR + port);
		info->xmit_fifo_size = 16;
	} else {
		info->xmit_fifo_size = 1;
	}

	/*
	 * Finally, enable interrupts
	 */
	outb_p(0x0f,UART_IER + port);	/* enable all intrs */
	if (info->flags & ASYNC_FOURPORT) {
		/* Enable interrupts on the AST Fourport board */
		ICP = (port & 0xFE0) | 0x01F;
		outb_p(0x80, ICP);
		(void) inb(ICP);
	}
}

void change_speed(unsigned int line)
{
	struct async_struct * info;
	unsigned short port;
	int	quot = 0;
	unsigned cflag,cval;
	int	i;

	if (line >= NR_PORTS)
		return;
	info = rs_table + line;
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
	if (!quot)
		outb(0x00,UART_MCR + port);
	else if (!inb(UART_MCR + port))
		startup(info);
/* byte size and parity */
	cval = cflag & (CSIZE | CSTOPB);
	cval >>= 4;
	if (cflag & PARENB)
		cval |= 8;
	if (!(cflag & PARODD))
		cval |= 16;
	cli();
	outb_p(cval | UART_LCR_DLAB, UART_LCR + port);	/* set DLAB */
	outb_p(quot & 0xff, UART_DLL + port);	/* LS of divisor */
	outb_p(quot >> 8, UART_DLM + port);	/* MS of divisor */
	outb(cval, UART_LCR + port);		/* reset DLAB */
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
	tmp.irq = info->ISR->irq;
/*	tmp.flags = info->flags; */
	memcpy_tofs(retinfo,&tmp,sizeof(*retinfo));
	return 0;
}

static int set_serial_info(struct async_struct * info,
			   struct serial_struct * new_info)
{
	struct serial_struct tmp;
	async_ISR		ISR;
	unsigned int 		new_port;
	unsigned int		irq,new_irq;
	int 			retval;
	struct 			sigaction sa;

	if (!suser())
		return -EPERM;
	if (!new_info)
		return -EFAULT;
	memcpy_fromfs(&tmp,new_info,sizeof(tmp));
	new_port = tmp.port;
	new_irq = tmp.irq;
	if (new_irq > 15 || new_port > 0xffff)
		return -EINVAL;
	if (new_irq == 2)
		new_irq = 9;
	ISR = info->ISR;
	irq = ISR->irq;
	if (irq == 2)
		irq = 9;
	if (irq != new_irq) {
		/*
		 * We need to change the IRQ for this board.  OK, if
		 * necessary, first we try to grab the new IRQ for
		 * serial interrupts.
		 */
		if (!IRQ_ISR[new_irq]) {
			sa.sa_handler = rs_interrupt;
			sa.sa_flags = (SA_INTERRUPT);
			sa.sa_mask = 0;
			sa.sa_restorer = NULL;
			retval = irqaction(irq,&sa);
			if (retval)
				return retval;
		}

		/*
		 * If the new IRQ is OK, now we unlink the ISR from
		 * the existing interrupt chain.
		 */
		if (ISR->next_ISR)
			ISR->next_ISR->prev_ISR = ISR->prev_ISR;
		if (ISR->prev_ISR)
			ISR->prev_ISR->next_ISR = ISR->next_ISR;
		else
			IRQ_ISR[irq] = ISR->next_ISR;
		if (!IRQ_ISR[irq])
			free_irq(irq);

		/*
		 * Now link in the interrupt to the new interrupt chain.
		 */
		ISR->prev_ISR = 0;
		ISR->next_ISR = IRQ_ISR[new_irq];
		if (ISR->next_ISR)
			ISR->next_ISR->prev_ISR = ISR;
		IRQ_ISR[new_irq] = ISR;
	}
	cli();
	if (new_port != info->port) {
		outb_p(0x00, UART_IER + info->port);	/* disable all intrs */
		outb(0x00, UART_MCR + info->port);	/* reset DTR, RTS, */
		info->port = new_port;
		init(info);
		startup(info);
	}
	sti();
	return 0;
}

static int get_modem_info(struct async_struct * info, unsigned int *value)
{
	unsigned port;
	unsigned char control, status;
	unsigned int result;

	port = info->port;
	control = inb(UART_MCR + port);
	status = inb(UART_MSR + port);
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
	unsigned port;
	unsigned char control;
	unsigned int arg = get_fs_long((unsigned long *) value);
	
	port = info->port;
	control = inb(UART_MCR + port);

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
	outb(UART_MCR + port,control);
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

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its interrupt into
 * the ISR chain.   It also performs the serial-speicific
 * initalization for the tty structure.
 */
int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct async_struct	*info;
	async_ISR		ISR;
	int 			irq, retval, line;
	struct sigaction	sa;

	if (!tty)
		return -ENODEV;
	if (tty->count > 1)
		return 0;		/* We've already been initialized */
	line = DEV_TO_SL(tty->line);
	if ((line < 0) || (line >= NR_PORTS))
		return -ENODEV;
	info = rs_table + line;
	if (!info->port)
		return -ENODEV;
	info->tty = tty;
	tty->write = rs_write;
	tty->close = rs_close;
	tty->ioctl = rs_ioctl;
	ISR = info->ISR;
	irq = ISR->irq;
	if (irq == 2)
		irq = 9;
	if (!IRQ_ISR[irq]) {
		sa.sa_handler = rs_interrupt;
		sa.sa_flags = (SA_INTERRUPT);
		sa.sa_mask = 0;
		sa.sa_restorer = NULL;
		retval = irqaction(irq,&sa);
		if (retval)
			return retval;
	}
	if (!ISR->refcnt++) {
		/*
		 * If this is the first time we're using this ISR,
		 * link it in.
		 */
		ISR->prev_ISR = 0;
		ISR->next_ISR = IRQ_ISR[irq];
		if (ISR->next_ISR)
			ISR->next_ISR->prev_ISR = ISR;
		IRQ_ISR[irq] = ISR;
	}
	startup(info);
	return 0;
}

long rs_init(long kmem_start)
{
	int i;
	struct async_struct * info;

	timer_table[RS_TIMER].fn = rs_timer;
	timer_table[RS_TIMER].expires = 0;
	for (i = 0; i < 16; i++) {
		IRQ_ISR[i] = 0;
	}
	for (i = 0, info = rs_table; i < NR_PORTS; i++,info++) {
		info->line = i;
		info->tty = 0;
		info->type = PORT_UNKNOWN;
		info->timer = 0;
		info->custom_divisor = 0;
		if (!info->ISR->line) {
			info->ISR->line = i;
			info->ISR->refcnt = 0;
			info->ISR->next_ISR = 0;
			info->ISR->prev_ISR = 0;
		}
		init(info);
		if (info->type == PORT_UNKNOWN)
			continue;
		printk("ttys%d%s at 0x%04x (irq = %d)", info->line, 
		       (info->flags & ASYNC_FOURPORT) ? " FourPort" : "",
		       info->port, info->ISR->irq);
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
	return kmem_start;
}


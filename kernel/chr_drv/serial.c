/*
 *  linux/kernel/serial.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	long rs_init(long);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tty.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define WAKEUP_CHARS (3*TTY_BUF_SIZE/4)

struct serial_struct serial_table[NR_SERIALS] = {
	{ PORT_UNKNOWN, 0, 0x3F8, 4, NULL},
	{ PORT_UNKNOWN, 1, 0x2F8, 3, NULL},
	{ PORT_UNKNOWN, 2, 0x3E8, 4, NULL},
	{ PORT_UNKNOWN, 3, 0x2E8, 3, NULL},
};

void send_break(unsigned int line)
{
	unsigned short port;
	struct serial_struct * info;

	if (line >= NR_SERIALS)
		return;
	info = serial_table + line;
	if (!(port = info->port))
		return;
	port += 3;
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 25;
	outb_p(inb_p(port) | 0x40,port);
	schedule();
	outb_p(inb_p(port) & 0xbf,port);
}

/*
 * There are several races here: we avoid most of them by disabling timer_active
 * for the crucial part of the process.. That's a good idea anyway.
 *
 * The problem is that we have to output characters /both/ from interrupts
 * and from the normal write: the latter to be sure the interrupts start up
 * again. With serial lines, the interrupts can happen so often that the
 * races actually are noticeable.
 */
static void send_intr(struct serial_struct * info)
{
	unsigned short port = info->port;
	unsigned int timer = SER1_TIMEOUT + info->line;
	struct tty_queue * queue = info->tty->write_q;
	int c, i = 0;

	if (info->tty->stopped) return;

	timer_active &= ~(1 << timer);
	while (inb_p(info->port+5) & 0x20) {
		if (queue->tail == queue->head)
			goto end_send;
		c = queue->buf[queue->tail];
		queue->tail++;
		queue->tail &= TTY_BUF_SIZE-1;
  		outb(c,port);
		if ((info->type != PORT_16550A) || (++i >= 14) || info->tty->stopped)
			break;
	}
	timer_table[timer].expires = jiffies + 10;
	timer_active |= 1 << timer;
end_send:
	if (LEFT(queue) > WAKEUP_CHARS)
		wake_up(&queue->proc_list);
}

static void receive_intr(struct serial_struct * info)
{
	unsigned short port = info->port;
	struct tty_queue * queue = info->tty->read_q;
	int head = queue->head;
	int maxhead = (queue->tail-1) & (TTY_BUF_SIZE-1);

	timer_active &= ~((1<<SER1_TIMER)<<info->line);
	do {
		queue->buf[head] = inb(port);
		if (head != maxhead) {
			head++;
			head &= TTY_BUF_SIZE-1;
		}
	} while (inb(port+5) & 1);
	queue->head = head;
	timer_active |= (1<<SER1_TIMER)<<info->line;
}

static void line_status_intr(struct serial_struct * info)
{
	unsigned char status = inb(info->port+5);

/*	printk("line status: %02x\n",status); */
}

static void modem_status_intr(struct serial_struct * info)
{
	unsigned char status = inb(info->port+6);

	if (!(info->tty->termios.c_cflag & CLOCAL)) {
		if ((status & 0x88) == 0x08 && info->tty->pgrp > 0)
			kill_pg(info->tty->pgrp,SIGHUP,1);

		if (info->tty->termios.c_cflag & CRTSCTS)
			info->tty->stopped = !(status & 0x10);

		if (!info->tty->stopped)
			send_intr(info);
	}
}

static void (*jmp_table[4])(struct serial_struct *) = {
	modem_status_intr,
	send_intr,
	receive_intr,
	line_status_intr
};

static void check_tty(struct serial_struct * info)
{
	unsigned char ident;

	if (!info || !info->tty || !info->port)
		return;
	while (1) {
		ident = inb(info->port+2) & 7;
		if (ident & 1)
			return;
		ident >>= 1;
		if (ident > 3)
			return;
		jmp_table[ident](info);
	}
}

/*
 * Again, we disable interrupts to be sure there aren't any races:
 * see send_intr for details.
 */
static inline void do_rs_write(struct serial_struct * info)
{
	if (!info->tty || !info->port)
		return;
	if (!info->tty->write_q || EMPTY(info->tty->write_q))
		return;
	cli();
	send_intr(info);
	sti();
}

/*
 * IRQ routines: one per line
 */
static void com1_IRQ(int unused)
{
	check_tty(serial_table+0);
}

static void com2_IRQ(int unused)
{
	check_tty(serial_table+1);
}

static void com3_IRQ(int unused)
{
	check_tty(serial_table+2);
}

static void com4_IRQ(int unused)
{
	check_tty(serial_table+3);
}

/*
 * Receive timer routines: one per line
 */
static void com1_timer(void)
{
	TTY_READ_FLUSH(tty_table+64);
}

static void com2_timer(void)
{
	TTY_READ_FLUSH(tty_table+65);
}

static void com3_timer(void)
{
	TTY_READ_FLUSH(tty_table+66);
}

static void com4_timer(void)
{
	TTY_READ_FLUSH(tty_table+67);
}

/*
 * Send timeout routines: one per line
 */
static void com1_timeout(void)
{
	do_rs_write(serial_table);
}

static void com2_timeout(void)
{
	do_rs_write(serial_table + 1);
}

static void com3_timeout(void)
{
	do_rs_write(serial_table + 2);
}

static void com4_timeout(void)
{
	do_rs_write(serial_table + 3);
}

static void init(struct serial_struct * info)
{
	unsigned char status1, status2, scratch;
	unsigned short port = info->port;

	if (inb(port+5) == 0xff) {
		info->type = PORT_UNKNOWN;
		return;
	}
	
	scratch = inb(port+7);
	outb_p(0xa5, port+7);
	status1 = inb(port+7);
	outb_p(0x5a, port+7);
	status2 = inb(port+7);
	if (status1 == 0xa5 && status2 == 0x5a) {
		outb_p(scratch, port+7);
		outb_p(0x01, port+2);
		scratch = inb(port+2) >> 6;
		switch (scratch) {
			case 0:
				info->type = PORT_16450;
				break;
			case 1:
				info->type = PORT_UNKNOWN;
				break;
			case 2:
				info->type = PORT_16550;
				outb_p(0x00, port+2);
				break;
			case 3:
				info->type = PORT_16550A;
				outb_p(0xc7, port+2);
				break;
		}
	} else
		info->type = PORT_8250;
	outb_p(0x80,port+3);	/* set DLAB of line control reg */
	outb_p(0x30,port);	/* LS of divisor (48 -> 2400 bps) */
	outb_p(0x00,port+1);	/* MS of divisor */
	outb_p(0x03,port+3);	/* reset DLAB */
	outb_p(0x00,port+4);	/* reset DTR,RTS, OUT_2 */
	outb_p(0x00,port+1);	/* disable all intrs */
	(void)inb(port);	/* read data port to reset things (?) */
}

void serial_close(unsigned line, struct file * filp)
{
	struct serial_struct * info;
	int irq;

	if (line >= NR_SERIALS)
		return;
	info = serial_table + line;
	if (!info->port)
		return;
	outb(0x00,info->port+4);	/* reset DTR, RTS, */
	irq = info->irq;
	if (irq == 2)
		irq = 9;
	free_irq(irq);
}

static void startup(unsigned short port)
{
	int i;

	outb_p(0x03,port+3);	/* reset DLAB */
	outb_p(0x0b,port+4);	/* set DTR,RTS, OUT_2 */
	outb_p(0x0f,port+1);	/* enable all intrs */
	inb_p(port+2);
	inb_p(port+6);
	inb_p(port+2);
	inb_p(port+5);
	for (i = 0; i < 16 ; i++) {
		inb_p(port+0);
		if (!(inb_p(port+5) & 1))
			break;
	}
	inb_p(port+2);
	inb_p(port+5);
}

void change_speed(unsigned int line)
{
	struct serial_struct * info;
	unsigned short port,quot;
	unsigned cflag,cval;
	static unsigned short quotient[] = {
		0, 2304, 1536, 1047, 857,
		768, 576, 384, 192, 96,
		64, 48, 24, 12, 6, 3
	};

	if (line >= NR_SERIALS)
		return;
	info = serial_table + line;
	cflag = info->tty->termios.c_cflag;
	if (!(port = info->port))
		return;
	quot = quotient[cflag & CBAUD];
	if (!quot)
		outb(0x00,port+4);
	else if (!inb(port+4))
		startup(port);
/* byte size and parity */
	cval = cflag & (CSIZE | CSTOPB);
	cval >>= 4;
	if (cflag & PARENB)
		cval |= 8;
	if (!(cflag & PARODD))
		cval |= 16;
	cli();
	outb_p(cval | 0x80,port+3);	/* set DLAB */
	outb_p(quot & 0xff,port);	/* LS of divisor */
	outb_p(quot >> 8,port+1);	/* MS of divisor */
	outb(cval,port+3);		/* reset DLAB */
	sti();
}

static void (*serial_handler[NR_SERIALS])(int) = {
	com1_IRQ,com2_IRQ,com3_IRQ,com4_IRQ
};

/*
 * this routine enables interrupts on 'line', and disables them for any
 * other serial line that shared the same IRQ. Braindamaged AT hardware.
 */
int serial_open(unsigned line, struct file * filp)
{
	struct serial_struct * info;
	int irq,retval;
	unsigned short port;
	struct sigaction sa;

	sa.sa_handler = serial_handler[line];
	sa.sa_flags = SA_INTERRUPT;
	sa.sa_mask = 0;
	sa.sa_restorer = NULL;
	if (line >= NR_SERIALS)
		return -ENODEV;
	info = serial_table + line;
	if (!(port = info->port))
		return -ENODEV;
	irq = info->irq;
	if (irq == 2)
		irq = 9;
	if (retval = irqaction(irq,&sa))
		return retval;
	startup(port);
	return 0;
}

int get_serial_info(unsigned int line, struct serial_struct * info)
{
	if (line >= NR_SERIALS)
		return -ENODEV;
	if (!info)
		return -EFAULT;
	memcpy_tofs(info,serial_table+line,sizeof(*info));
	return 0;
}

int set_serial_info(unsigned int line, struct serial_struct * info)
{
	struct serial_struct tmp;
	unsigned new_port;
	unsigned irq,new_irq;
	int retval;
	void (*handler)(int) = serial_handler[line];

	if (!suser())
		return -EPERM;
	if (line >= NR_SERIALS)
		return -ENODEV;
	if (!info)
		return -EFAULT;
	memcpy_fromfs(&tmp,info,sizeof(tmp));
	info = serial_table + line;
	if (!(new_port = tmp.port))
		new_port = info->port;
	if (!(new_irq = tmp.irq))
		new_irq = info->irq;
	if (new_irq > 15 || new_port > 0xffff)
		return -EINVAL;
	if (new_irq == 2)
		new_irq = 9;
	irq = info->irq;
	if (irq == 2)
		irq = 9;
	if (irq != new_irq) {
		retval = request_irq(new_irq,handler);
		if (retval)
			return retval;
		info->irq = new_irq;
		free_irq(irq);
	}
	cli();
	if (new_port != info->port) {
		outb(0x00,info->port+4);	/* reset DTR, RTS, */
		info->port = new_port;
		init(info);
		startup(new_port);
	}
	sti();
	return 0;
}

long rs_init(long kmem_start)
{
	int i;
	struct serial_struct * info;

/* SERx_TIMER timers are used for receiving: timeout is always 0 (immediate) */
	timer_table[SER1_TIMER].fn = com1_timer;
	timer_table[SER1_TIMER].expires = 0;
	timer_table[SER2_TIMER].fn = com2_timer;
	timer_table[SER2_TIMER].expires = 0;
	timer_table[SER3_TIMER].fn = com3_timer;
	timer_table[SER3_TIMER].expires = 0;
	timer_table[SER4_TIMER].fn = com4_timer;
	timer_table[SER4_TIMER].expires = 0;
/* SERx_TIMEOUT timers are used for writing: prevent serial lockups */
	timer_table[SER1_TIMEOUT].fn = com1_timeout;
	timer_table[SER1_TIMEOUT].expires = 0;
	timer_table[SER2_TIMEOUT].fn = com2_timeout;
	timer_table[SER2_TIMEOUT].expires = 0;
	timer_table[SER3_TIMEOUT].fn = com3_timeout;
	timer_table[SER3_TIMEOUT].expires = 0;
	timer_table[SER4_TIMEOUT].fn = com4_timeout;
	timer_table[SER4_TIMEOUT].expires = 0;
	for (i = 0, info = serial_table; i < NR_SERIALS; i++,info++) {
		info->tty = (tty_table+64) + i;
		init(info);
		if (info->type == PORT_UNKNOWN)
			continue;
		printk("serial port at 0x%04x (irq = %d)",info->port,info->irq);
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

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 *	void _rs_write(struct tty_struct * tty);
 */
void rs_write(struct tty_struct * tty)
{
	int line = tty - tty_table - 64;

	do_rs_write(serial_table+line);
}

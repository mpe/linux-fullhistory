/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	long rs_init(long);
 * and all interrupts pertaining to serial IO.
 */

#include <signal.h>
#include <errno.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tty.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define WAKEUP_CHARS (3*TTY_BUF_SIZE/4)

/*
 * note that IRQ9 is what many docs call IRQ2 - on the AT hardware
 * the old IRQ2 line has been changed to IRQ9. The serial_table
 * structure considers IRQ2 to be the same as IRQ9.
 */
extern void IRQ9_interrupt(void);
extern void IRQ3_interrupt(void);
extern void IRQ4_interrupt(void);
extern void IRQ5_interrupt(void);

struct serial_struct serial_table[NR_SERIALS] = {
	{ PORT_UNKNOWN, 0, 0x3F8, 4, NULL},
	{ PORT_UNKNOWN, 1, 0x2F8, 3, NULL},
	{ PORT_UNKNOWN, 2, 0x3E8, 4, NULL},
	{ PORT_UNKNOWN, 3, 0x2E8, 3, NULL},
};

static struct serial_struct * irq_info[16] = { NULL, };

static void modem_status_intr(struct serial_struct * info)
{
	unsigned char status = inb(info->port+6);

	if ((status & 0x88) == 0x08 && info->tty->pgrp > 0)
		kill_pg(info->tty->pgrp,SIGHUP,1);
#if 0
	if ((status & 0x10) == 0x10)
		info->tty->stopped = 0;
	else
		info->tty->stopped = 1;
#endif
}

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

	timer_active &= ~(1 << timer);
	do {
		if ((c = GETCH(queue)) < 0)
			return;
		outb(c,port);
		i++;
	} while (info->type == PORT_16550A &&
		  i < 14 && !EMPTY(queue));
	timer_table[timer].expires = jiffies + 10;
	timer_active |= 1 << timer;
	if (LEFT(queue) > WAKEUP_CHARS)
		wake_up(&queue->proc_list);
}

static void receive_intr(struct serial_struct * info)
{
	unsigned short port = info->port;
	struct tty_queue * queue = info->tty->read_q;

	do {
		PUTCH(inb(port),queue);
	} while (inb(port+5) & 1);
	timer_active |= (1<<SER1_TIMER)<<info->line;
}

static void line_status_intr(struct serial_struct * info)
{
	unsigned char status = inb(info->port+5);

/*	printk("line status: %02x\n",status); */
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

void do_IRQ(int irq)
{
	check_tty(irq_info[irq]);
}

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
	if (inb_p(info->port+5) & 0x20)
		send_intr(info);
	else {
		unsigned int timer = SER1_TIMEOUT+info->line;

		timer_table[timer].expires = jiffies + 10;
		timer_active |= 1 << timer;
	}
	sti();
}

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
	if (irq_info[irq] == info) {
		irq_info[irq] = NULL;
		if (irq < 8)
			outb(inb_p(0x21) | (1<<irq),0x21);
		else
			outb(inb_p(0xA1) | (1<<(irq-8)),0xA1);
	}
}

static void startup(unsigned short port)
{
	outb_p(0x03,port+3);	/* reset DLAB */
	outb_p(0x0f,port+4);	/* set DTR,RTS, OUT_2 */
	outb_p(0x0f,port+1);	/* enable all intrs */
	inb_p(port+5);
	inb_p(port+0);
	inb_p(port+6);
	inb(port+2);
}

void change_speed(unsigned int line)
{
	struct serial_struct * info;
	unsigned short port,quot;
	unsigned cflag;
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
	cli();
	outb_p(0x80,port+3);		/* set DLAB */
	outb_p(quot & 0xff,port);	/* LS of divisor */
	outb_p(quot >> 8,port+1);	/* MS of divisor */
	outb(0x03,port+3);		/* reset DLAB */
	sti();
/* set byte size and parity */
	quot = cflag & (CSIZE | CSTOPB);
	quot >>= 4;
	if (cflag & PARENB)
		quot |= 8;
	if (!(cflag & PARODD))
		quot |= 16;
	outb(quot,port+3);
}

/*
 * this routine enables interrupts on 'line', and disables them for any
 * other serial line that shared the same IRQ. Braindamaged AT hardware.
 */
int serial_open(unsigned line, struct file * filp)
{
	struct serial_struct * info;
	int irq;
	unsigned short port;

	if (line >= NR_SERIALS)
		return -ENODEV;
	info = serial_table + line;
	if (!(port = info->port))
		return -ENODEV;
	irq = info->irq;
	if (irq == 2)
		irq = 9;
	if (irq_info[irq] && irq_info[irq] != info)
		return -EBUSY;
	cli();
	startup(port);
	irq_info[irq] = info;
	if (irq < 8)
		outb(inb_p(0x21) & ~(1<<irq),0x21);
	else
		outb(inb_p(0xA1) & ~(1<<(irq-8)),0xA1);
	sti();
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
		if (irq_info[new_irq])
			return -EBUSY;
		cli();
		irq_info[new_irq] = irq_info[irq];
		irq_info[irq] = NULL;
		info->irq = new_irq;
		if (irq < 8)
			outb(inb_p(0x21) | (1<<irq),0x21);
		else
			outb(inb_p(0xA1) | (1<<(irq-8)),0xA1);
		if (new_irq < 8)
			outb(inb_p(0x21) & ~(1<<new_irq),0x21);
		else
			outb(inb_p(0xA1) & ~(1<<(new_irq-8)),0xA1);
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
	set_intr_gate(0x23,IRQ3_interrupt);
	set_intr_gate(0x24,IRQ4_interrupt);
	set_intr_gate(0x25,IRQ5_interrupt);
	set_intr_gate(0x29,IRQ9_interrupt);
	for (i = 0, info = serial_table; i < NR_SERIALS; i++,info++) {
		info->tty = (tty_table+64) + i;
		init(info);
		switch (info->type) {
			case PORT_8250:
				printk("serial port at 0x%04x is a 8250\n", info->port);
				break;
			case PORT_16450:
				printk("serial port at 0x%04x is a 16450\n", info->port);
				break;
			case PORT_16550:
				printk("serial port at 0x%04x is a 16550\n", info->port);
				break;
			case PORT_16550A:
				printk("serial port at 0x%04x is a 16550A\n", info->port);
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

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
 *	void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <signal.h>
 
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tty.h>

#include <asm/system.h>
#include <asm/io.h>

#define WAKEUP_CHARS (3*TTY_BUF_SIZE/4)

extern void IRQ3_interrupt(void);
extern void IRQ4_interrupt(void);

#define PORT_UNKNOWN	0
#define PORT_8250	1
#define PORT_16450	2
#define PORT_16550	3
#define PORT_16550A	4

int port_table[] = {
	PORT_UNKNOWN,
	PORT_UNKNOWN,
	PORT_UNKNOWN,
	PORT_UNKNOWN,
	PORT_UNKNOWN
};
	

static void modem_status_intr(unsigned line, unsigned port, struct tty_struct * tty)
{
	unsigned char status = inb(port+6);

	if ((status & 0x88) == 0x08 && tty->pgrp > 0)
		kill_pg(tty->pgrp,SIGHUP,1);

	if ((status & 0x10) == 0x10)
		tty->stopped = 0;
	else
		tty->stopped = 1;
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
static void send_intr(unsigned line, unsigned port, struct tty_struct * tty)
{
	int c, i = 0;

#define TIMER ((SER1_TIMEOUT-1)+line)
	timer_active &= ~(1 << TIMER);
	if (!tty->stopped) {
		do {
			if ((c = GETCH(tty->write_q)) < 0)
				return;
			outb(c,port);
			i++;
		} while ( port_table[line] == PORT_16550A && \
			  i < 14 && !EMPTY(tty->write_q) && \
			  !tty->stopped);
	}
	timer_table[TIMER].expires = jiffies + 10;
	timer_active |= 1 << TIMER;
	if (LEFT(tty->write_q) > WAKEUP_CHARS)
		wake_up(&tty->write_q->proc_list);
#undef TIMER
}

static void receive_intr(unsigned line, unsigned port, struct tty_struct * tty)
{
	if (FULL(tty->read_q))
		return;

	outb_p((inb(port+4) & 0x0d), port+4);

	do {
		PUTCH(inb(port),tty->read_q);
	} while ((inb(port+5) & 0x01 != 0) && !FULL(tty->read_q));

	outb_p((inb(port+4) | 0x02), port+4);

	timer_active |= (1<<(SER1_TIMER-1))<<line;
}

static void line_status_intr(unsigned line, unsigned port, struct tty_struct * tty)
{
	unsigned char status = inb(port+5);

/*	printk("line status: %02x\n",status); */
}

static void (*jmp_table[4])(unsigned,unsigned,struct tty_struct *) = {
	modem_status_intr,
	send_intr,
	receive_intr,
	line_status_intr
};

static void check_tty(unsigned line,struct tty_struct * tty)
{
	unsigned short port;
	unsigned char ident;

	if (!(port = tty->read_q->data))
		return;
	while (1) {
		ident = inb(port+2) & 7;
		if (ident & 1)
			return;
		ident >>= 1;
		if (ident > 3)
			return;
		jmp_table[ident](line,port,tty);
	}
}

/*
 * IRQ3 normally handles com2 and com4
 */
void do_IRQ3(void)
{
	check_tty(2,tty_table+65);
	check_tty(4,tty_table+67);
}

/*
 * IRQ4 normally handles com1 and com2
 */
void do_IRQ4(void)
{
	check_tty(1,tty_table+64);
	check_tty(3,tty_table+66);	
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
static inline void do_rs_write(unsigned line, struct tty_struct * tty)
{
	int port;

#define TIMER ((SER1_TIMEOUT-1)+line)
	if (!tty || !tty->write_q || EMPTY(tty->write_q))
		return;
	if (!(port = tty->write_q->data))
		return;
	cli();
	if (inb_p(port+5) & 0x20)
		send_intr(line,port,tty);
	else {
		timer_table[TIMER].expires = jiffies + 10;
		timer_active |= 1 << TIMER;
	}
	sti();
#undef TIMER
}

static void com1_timeout(void)
{
	do_rs_write(1,tty_table+64);
}

static void com2_timeout(void)
{
	do_rs_write(2,tty_table+65);
}

static void com3_timeout(void)
{
	do_rs_write(3,tty_table+66);
}

static void com4_timeout(void)
{
	do_rs_write(4,tty_table+67);
}

static void init(int port, int line)
{
	unsigned char status1, status2, scratch;

	if (inb(port+5) == 0xff) {
		port_table[line] = PORT_UNKNOWN;
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
			case 0:  printk("serial port at 0x%04x is a 16450\n", port);
				 port_table[line] = PORT_16450;
				 break;
			case 1:  printk("serial port at 0x%04x is unknown\n", port);
				 port_table[line] = PORT_UNKNOWN;
				 break;
			case 2:  printk("serial port at 0x%04x is a 16550 (FIFO's disabled)\n", port);
				 port_table[line] = PORT_16550;
				 outb_p(0x00, port+2);
				 break;
			case 3:  printk("serial port at 0x%04x is a 16550a (FIFO's enabled)\n", port);
				 port_table[line] = PORT_16550A;
				 outb_p(0xc7, port+2);
				 break;
		}
	} else
		printk("serial port at 0x%04x is a 8250\n", port);
	
	outb_p(0x80,port+3);	/* set DLAB of line control reg */
	outb_p(0x30,port);	/* LS of divisor (48 -> 2400 bps */
	outb_p(0x00,port+1);	/* MS of divisor */
	outb_p(0x03,port+3);	/* reset DLAB */
	outb_p(0x00,port+4);	/* reset DTR,RTS, OUT_2 */
	outb_p(0x0f,port+1);	/* enable all intrs */
	(void)inb(port);	/* read data port to reset things (?) */
}

/*
 * this routine enables interrupts on 'line', and disables them on
 * 'line ^ 2', as they share the same IRQ. Braindamaged AT hardware.
 */
void serial_open(unsigned line)
{
	unsigned short port;
	unsigned short port2;

	if (line>3)
		return;
	port = tty_table[64+line].read_q->data;
	if (!port)
		return;
	port2 = tty_table[64+(line ^ 2)].read_q->data;
	cli();
	if (port2)
		outb_p(0x00,port2+4);
	outb_p(0x03,port+3);	/* reset DLAB */
	outb_p(0x0f,port+4);	/* set DTR,RTS, OUT_2 */
	outb_p(0x0f,port+1);	/* enable all intrs */
	inb_p(port+5);
	inb_p(port+0);
	inb_p(port+6);
	inb(port+2);
	sti();
}

void rs_init(void)
{
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
	init(tty_table[64].read_q->data, 1);
	init(tty_table[65].read_q->data, 2);
	init(tty_table[66].read_q->data, 3);
	init(tty_table[67].read_q->data, 4);
	outb(inb_p(0x21)&0xE7,0x21);
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
	int line = tty - tty_table - 63;

	do_rs_write(line,tty);
}

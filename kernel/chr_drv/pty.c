/*
 *  linux/kernel/chr_drv/pty.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *	pty.c
 *
 * This module implements the pty functions
 *	void mpty_write(struct tty_struct * queue);
 *	void spty_write(struct tty_struct * queue);
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/fcntl.h>

#include <asm/system.h>
#include <asm/io.h>

int pty_open(unsigned int dev, struct file * filp)
{
	struct tty_struct * tty;

	tty = tty_table + dev;
	if (!tty->link)
		return -ENODEV;
	wake_up(&tty->read_q->proc_list);
	if (filp->f_flags & O_NDELAY)
		return 0;
	while (!tty->link->count && !(current->signal & ~current->blocked))
		interruptible_sleep_on(&tty->link->read_q->proc_list);
	if (!tty->link->count)
		return -ERESTARTSYS;
	return 0;
}

void pty_close(unsigned int dev, struct file * filp)
{
	struct tty_struct * tty;

	tty = tty_table + dev;
	wake_up(&tty->read_q->proc_list);
	wake_up(&tty->link->write_q->proc_list);
	if (IS_A_PTY_MASTER(dev)) {
		if (tty->link->pgrp > 0)
			kill_pg(tty->link->pgrp,SIGHUP,1);
	}
}

static inline void pty_copy(struct tty_struct * from, struct tty_struct * to)
{
	int c;

	while (!from->stopped && !EMPTY(from->write_q)) {
		if (FULL(to->read_q)) {
			if (FULL(to->secondary))
				break;
			TTY_READ_FLUSH(to);
			continue;
		}
		c = get_tty_queue(from->write_q);
		put_tty_queue(c,to->read_q);
		if (current->signal & ~current->blocked)
			break;
	}
	TTY_READ_FLUSH(to);
	wake_up(&from->write_q->proc_list);
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It copies the input to the output-queue of it's
 * slave.
 */
void mpty_write(struct tty_struct * tty)
{
	if (tty->link)
		pty_copy(tty,tty->link);
}

void spty_write(struct tty_struct * tty)
{
	if (tty->link)
		pty_copy(tty,tty->link);
}

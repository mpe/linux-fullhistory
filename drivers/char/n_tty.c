/*
 * n_tty.c --- implements the N_TTY line discipline.
 * 
 * This code used to be in tty_io.c, but things are getting hairy
 * enough that it made sense to split things off.  (The N_TTY
 * processing has changed so much that it's hardly recognizable,
 * anyway...)
 *
 * Note that the open routine for N_TTY is guaranteed never to return
 * an error.  This is because Linux will fall back to setting a line
 * to N_TTY if it can not switch to any other line discipline.  
 *
 * Written by Theodore Ts'o, Copyright 1994.
 * 
 * This file also contains code originally written by Linus Torvalds,
 * Copyright 1991, 1992, 1993, and by Julian Cowley, Copyright 1994.
 * 
 * This file may be redistributed under the terms of the GNU Public
 * License.
 */

#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/malloc.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>

#define CONSOLE_DEV MKDEV(TTY_MAJOR,0)

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

/* number of characters left in xmit buffer before select has we have room */
#define WAKEUP_CHARS 256

/*
 * This defines the low- and high-watermarks for throttling and
 * unthrottling the TTY driver.  These watermarks are used for
 * controlling the space in the read buffer.
 */
#define TTY_THRESHOLD_THROTTLE		(N_TTY_BUF_SIZE - 128)
#define TTY_THRESHOLD_UNTHROTTLE 	128

static inline void put_tty_queue(unsigned char c, struct tty_struct *tty)
{
	if (tty->read_cnt < N_TTY_BUF_SIZE) {
		tty->read_buf[tty->read_head] = c;
		tty->read_head = (tty->read_head + 1) & (N_TTY_BUF_SIZE-1);
		tty->read_cnt++;
	}
}

/*
 * Flush the input buffer
 */
void n_tty_flush_buffer(struct tty_struct * tty)
{
	tty->read_head = tty->read_tail = tty->read_cnt = 0;
	tty->canon_head = tty->canon_data = tty->erasing = 0;
	memset(&tty->read_flags, 0, sizeof tty->read_flags);
	
	if (!tty->link)
		return;

	if (tty->driver.unthrottle)
		(tty->driver.unthrottle)(tty);
	if (tty->link->packet) {
		tty->ctrl_status |= TIOCPKT_FLUSHREAD;
		wake_up_interruptible(&tty->link->read_wait);
	}
}

/*
 * Return number of characters buffered to be delivered to user
 */
int n_tty_chars_in_buffer(struct tty_struct *tty)
{
	return tty->read_cnt;
}

/*
 * Perform OPOST processing.  Returns -1 when the output device is
 * full and the character must be retried.
 */
static int opost(unsigned char c, struct tty_struct *tty)
{
	int	space, spaces;

	space = tty->driver.write_room(tty);
	if (!space)
		return -1;

	if (O_OPOST(tty)) {
		switch (c) {
		case '\n':
			if (O_ONLRET(tty))
				tty->column = 0;
			if (O_ONLCR(tty)) {
				if (space < 2)
					return -1;
				tty->driver.put_char(tty, '\r');
				tty->column = 0;
			}
			tty->canon_column = tty->column;
			break;
		case '\r':
			if (O_ONOCR(tty) && tty->column == 0)
				return 0;
			if (O_OCRNL(tty)) {
				c = '\n';
				if (O_ONLRET(tty))
					tty->canon_column = tty->column = 0;
				break;
			}
			tty->canon_column = tty->column = 0;
			break;
		case '\t':
			spaces = 8 - (tty->column & 7);
			if (O_TABDLY(tty) == XTABS) {
				if (space < spaces)
					return -1;
				tty->column += spaces;
				tty->driver.write(tty, 0, "        ", spaces);
				return 0;
			}
			tty->column += spaces;
			break;
		case '\b':
			if (tty->column > 0)
				tty->column--;
			break;
		default:
			if (O_OLCUC(tty))
				c = toupper(c);
			if (!iscntrl(c))
				tty->column++;
			break;
		}
	}
	tty->driver.put_char(tty, c);
	return 0;
}

static inline void put_char(unsigned char c, struct tty_struct *tty)
{
	tty->driver.put_char(tty, c);
}

/* Must be called only when L_ECHO(tty) is true. */

static void echo_char(unsigned char c, struct tty_struct *tty)
{
	if (L_ECHOCTL(tty) && iscntrl(c) && c != '\t') {
		put_char('^', tty);
		put_char(c ^ 0100, tty);
		tty->column += 2;
	} else
		opost(c, tty);
}

static inline void finish_erasing(struct tty_struct *tty)
{
	if (tty->erasing) {
		put_char('/', tty);
		tty->column += 2;
		tty->erasing = 0;
	}
}

static void eraser(unsigned char c, struct tty_struct *tty)
{
	enum { ERASE, WERASE, KILL } kill_type;
	int head, seen_alnums;

	if (tty->read_head == tty->canon_head) {
		/* opost('\a', tty); */		/* what do you think? */
		return;
	}
	if (c == ERASE_CHAR(tty))
		kill_type = ERASE;
	else if (c == WERASE_CHAR(tty))
		kill_type = WERASE;
	else {
		if (!L_ECHO(tty)) {
			tty->read_cnt -= ((tty->read_head - tty->canon_head) &
					  (N_TTY_BUF_SIZE - 1));
			tty->read_head = tty->canon_head;
			return;
		}
		if (!L_ECHOK(tty) || !L_ECHOKE(tty)) {
			tty->read_cnt -= ((tty->read_head - tty->canon_head) &
					  (N_TTY_BUF_SIZE - 1));
			tty->read_head = tty->canon_head;
			finish_erasing(tty);
			echo_char(KILL_CHAR(tty), tty);
			/* Add a newline if ECHOK is on and ECHOKE is off. */
			if (L_ECHOK(tty))
				opost('\n', tty);
			return;
		}
		kill_type = KILL;
	}

	seen_alnums = 0;
	while (tty->read_head != tty->canon_head) {
		head = (tty->read_head - 1) & (N_TTY_BUF_SIZE-1);
		c = tty->read_buf[head];
		if (kill_type == WERASE) {
			/* Equivalent to BSD's ALTWERASE. */
			if (isalnum(c) || c == '_')
				seen_alnums++;
			else if (seen_alnums)
				break;
		}
		tty->read_head = head;
		tty->read_cnt--;
		if (L_ECHO(tty)) {
			if (L_ECHOPRT(tty)) {
				if (!tty->erasing) {
					put_char('\\', tty);
					tty->column++;
					tty->erasing = 1;
				}
				echo_char(c, tty);
			} else if (!L_ECHOE(tty)) {
				echo_char(ERASE_CHAR(tty), tty);
			} else if (c == '\t') {
				unsigned int col = tty->canon_column;
				unsigned long tail = tty->canon_head;

				/* Find the column of the last char. */
				while (tail != tty->read_head) {
					c = tty->read_buf[tail];
					if (c == '\t')
						col = (col | 7) + 1;
					else if (iscntrl(c)) {
						if (L_ECHOCTL(tty))
							col += 2;
					} else
						col++;
					tail = (tail+1) & (N_TTY_BUF_SIZE-1);
				}

				/* Now backup to that column. */
				while (tty->column > col) {
					/* Can't use opost here. */
					put_char('\b', tty);
					tty->column--;
				}
			} else {
				if (iscntrl(c) && L_ECHOCTL(tty)) {
					put_char('\b', tty);
					put_char(' ', tty);
					put_char('\b', tty);
					tty->column--;
				}
				if (!iscntrl(c) || L_ECHOCTL(tty)) {
					put_char('\b', tty);
					put_char(' ', tty);
					put_char('\b', tty);
					tty->column--;
				}
			}
		}
		if (kill_type == ERASE)
			break;
	}
	if (tty->read_head == tty->canon_head)
		finish_erasing(tty);
}

static void isig(int sig, struct tty_struct *tty)
{
	if (tty->pgrp > 0)
		kill_pg(tty->pgrp, sig, 1);
	if (!L_NOFLSH(tty)) {
		n_tty_flush_buffer(tty);
		if (tty->driver.flush_buffer)
			tty->driver.flush_buffer(tty);
	}
}

static inline void n_tty_receive_break(struct tty_struct *tty)
{
	if (I_IGNBRK(tty))
		return;
	if (I_BRKINT(tty)) {
		isig(SIGINT, tty);
		return;
	}
	if (I_PARMRK(tty)) {
		put_tty_queue('\377', tty);
		put_tty_queue('\0', tty);
	}
	put_tty_queue('\0', tty);
	wake_up_interruptible(&tty->read_wait);
}

static inline void n_tty_receive_overrun(struct tty_struct *tty)
{
	char buf[64];

	tty->num_overrun++;
	if (tty->overrun_time < (jiffies - HZ)) {
		printk("%s: %d input overrun(s)\n", _tty_name(tty, buf),
		       tty->num_overrun);
		tty->overrun_time = jiffies;
		tty->num_overrun = 0;
	}
}

static inline void n_tty_receive_parity_error(struct tty_struct *tty,
					      unsigned char c)
{
	if (I_IGNPAR(tty)) {
		return;
	}
	if (I_PARMRK(tty)) {
		put_tty_queue('\377', tty);
		put_tty_queue('\0', tty);
		put_tty_queue(c, tty);
	} else
		put_tty_queue('\0', tty);
	wake_up_interruptible(&tty->read_wait);
}

static inline void n_tty_receive_char(struct tty_struct *tty, unsigned char c)
{
	if (tty->raw) {
		put_tty_queue(c, tty);
		return;
	}
	
	if (tty->stopped && I_IXON(tty) && I_IXANY(tty)) {
		start_tty(tty);
		return;
	}
	
	if (I_ISTRIP(tty))
		c &= 0x7f;
	if (I_IUCLC(tty) && L_IEXTEN(tty))
		c=tolower(c);

	if (tty->closing) {
		if (I_IXON(tty)) {
			if (c == START_CHAR(tty))
				start_tty(tty);
			else if (c == STOP_CHAR(tty))
				stop_tty(tty);
		}
		return;
	}

	/*
	 * If the previous character was LNEXT, or we know that this
	 * character is not one of the characters that we'll have to
	 * handle specially, do shortcut processing to speed things
	 * up.
	 */
	if (!test_bit(c, &tty->process_char_map) || tty->lnext) {
		finish_erasing(tty);
		tty->lnext = 0;
		if (L_ECHO(tty)) {
			if (tty->read_cnt >= N_TTY_BUF_SIZE-1) {
				put_char('\a', tty); /* beep if no space */
				return;
			}
			/* Record the column of first canon char. */
			if (tty->canon_head == tty->read_head)
				tty->canon_column = tty->column;
			echo_char(c, tty);
		}
		if (I_PARMRK(tty) && c == (unsigned char) '\377')
			put_tty_queue(c, tty);
		put_tty_queue(c, tty);
		return;
	}
		
	if (c == '\r') {
		if (I_IGNCR(tty))
			return;
		if (I_ICRNL(tty))
			c = '\n';
	} else if (c == '\n' && I_INLCR(tty))
		c = '\r';
	if (I_IXON(tty)) {
		if (c == START_CHAR(tty)) {
			start_tty(tty);
			return;
		}
		if (c == STOP_CHAR(tty)) {
			stop_tty(tty);
			return;
		}
	}
	if (L_ISIG(tty)) {
		if (c == INTR_CHAR(tty)) {
			isig(SIGINT, tty);
			return;
		}
		if (c == QUIT_CHAR(tty)) {
			isig(SIGQUIT, tty);
			return;
		}
		if (c == SUSP_CHAR(tty)) {
			if (!is_orphaned_pgrp(tty->pgrp))
				isig(SIGTSTP, tty);
			return;
		}
	}
	if (L_ICANON(tty)) {
		if (c == ERASE_CHAR(tty) || c == KILL_CHAR(tty) ||
		    (c == WERASE_CHAR(tty) && L_IEXTEN(tty))) {
			eraser(c, tty);
			return;
		}
		if (c == LNEXT_CHAR(tty) && L_IEXTEN(tty)) {
			tty->lnext = 1;
			if (L_ECHO(tty)) {
				finish_erasing(tty);
				if (L_ECHOCTL(tty)) {
					put_char('^', tty);
					put_char('\b', tty);
				}
			}
			return;
		}
		if (c == REPRINT_CHAR(tty) && L_ECHO(tty) &&
		    L_IEXTEN(tty)) {
			unsigned long tail = tty->canon_head;

			finish_erasing(tty);
			echo_char(c, tty);
			opost('\n', tty);
			while (tail != tty->read_head) {
				echo_char(tty->read_buf[tail], tty);
				tail = (tail+1) & (N_TTY_BUF_SIZE-1);
			}
			return;
		}
		if (c == '\n') {
			if (L_ECHO(tty) || L_ECHONL(tty)) {
				if (tty->read_cnt >= N_TTY_BUF_SIZE-1) {
					put_char('\a', tty);
					return;
				}
				opost('\n', tty);
			}
			goto handle_newline;
		}
		if (c == EOF_CHAR(tty)) {
		        if (tty->canon_head != tty->read_head)
			        set_bit(TTY_PUSH, &tty->flags);
			c = __DISABLED_CHAR;
			goto handle_newline;
		}
		if ((c == EOL_CHAR(tty)) ||
		    (c == EOL2_CHAR(tty) && L_IEXTEN(tty))) {
			/*
			 * XXX are EOL_CHAR and EOL2_CHAR echoed?!?
			 */
			if (L_ECHO(tty)) {
				if (tty->read_cnt >= N_TTY_BUF_SIZE-1) {
					put_char('\a', tty);
					return;
				}
				/* Record the column of first canon char. */
				if (tty->canon_head == tty->read_head)
					tty->canon_column = tty->column;
				echo_char(c, tty);
			}
			/*
			 * XXX does PARMRK doubling happen for
			 * EOL_CHAR and EOL2_CHAR?
			 */
			if (I_PARMRK(tty) && c == (unsigned char) '\377')
				put_tty_queue(c, tty);

		handle_newline:
			set_bit(tty->read_head, &tty->read_flags);
			put_tty_queue(c, tty);
			tty->canon_head = tty->read_head;
			tty->canon_data++;
			if (tty->fasync)
				kill_fasync(tty->fasync, SIGIO);
			if (tty->read_wait)
				wake_up_interruptible(&tty->read_wait);
			return;
		}
	}
	
	finish_erasing(tty);
	if (L_ECHO(tty)) {
		if (tty->read_cnt >= N_TTY_BUF_SIZE-1) {
			put_char('\a', tty); /* beep if no space */
			return;
		}
		if (c == '\n')
			opost('\n', tty);
		else {
			/* Record the column of first canon char. */
			if (tty->canon_head == tty->read_head)
				tty->canon_column = tty->column;
			echo_char(c, tty);
		}
	}

	if (I_PARMRK(tty) && c == (unsigned char) '\377')
		put_tty_queue(c, tty);

	put_tty_queue(c, tty);
}	

static void n_tty_receive_buf(struct tty_struct *tty, unsigned char *cp,
			      char *fp, int count)
{
	unsigned char *p;
	char *f, flags = 0;
	int	i;

	if (!tty->read_buf)
		return;

	if (tty->real_raw) {
		i = MIN(count, MIN(N_TTY_BUF_SIZE - tty->read_cnt,
				   N_TTY_BUF_SIZE - tty->read_head));
		memcpy(tty->read_buf + tty->read_head, cp, i);
		tty->read_head = (tty->read_head + i) & (N_TTY_BUF_SIZE-1);
		tty->read_cnt += i;
		cp += i;
		count -= i;

		i = MIN(count, MIN(N_TTY_BUF_SIZE - tty->read_cnt,
			       N_TTY_BUF_SIZE - tty->read_head));
		memcpy(tty->read_buf + tty->read_head, cp, i);
		tty->read_head = (tty->read_head + i) & (N_TTY_BUF_SIZE-1);
		tty->read_cnt += i;
	} else {
		for (i=count, p = cp, f = fp; i; i--, p++) {
			if (f)
				flags = *f++;
			switch (flags) {
			case TTY_NORMAL:
				n_tty_receive_char(tty, *p);
				break;
			case TTY_BREAK:
				n_tty_receive_break(tty);
				break;
			case TTY_PARITY:
			case TTY_FRAME:
				n_tty_receive_parity_error(tty, *p);
				break;
			case TTY_OVERRUN:
				n_tty_receive_overrun(tty);
				break;
			default:
				printk("%s: unknown flag %d\n", tty_name(tty),
				       flags);
				break;
			}
		}
		if (tty->driver.flush_chars)
			tty->driver.flush_chars(tty);
	}

	if (!tty->icanon && (tty->read_cnt >= tty->minimum_to_wake)) {
		if (tty->fasync)
			kill_fasync(tty->fasync, SIGIO);
		if (tty->read_wait)
			wake_up_interruptible(&tty->read_wait);
	}

	if ((tty->read_cnt >= TTY_THRESHOLD_THROTTLE) &&
	    tty->driver.throttle &&
	    !set_bit(TTY_THROTTLED, &tty->flags))
		tty->driver.throttle(tty);
}

static int n_tty_receive_room(struct tty_struct *tty)
{
	int	left = N_TTY_BUF_SIZE - tty->read_cnt - 1;

	/*
	 * If we are doing input canonicalization, and there are no
	 * pending newlines, let characters through without limit, so
	 * that erase characters will be handled.  Other excess
	 * characters will be beeped.
	 */
	if (tty->icanon && !tty->canon_data)
		return N_TTY_BUF_SIZE;

	if (left > 0)
		return left;
	return 0;
}

int is_ignored(int sig)
{
	return ((current->blocked & (1<<(sig-1))) ||
	        (current->sigaction[sig-1].sa_handler == SIG_IGN));
}

static void n_tty_set_termios(struct tty_struct *tty, struct termios * old)
{
	if (!tty)
		return;
	
	tty->icanon = (L_ICANON(tty) != 0);
	if (I_ISTRIP(tty) || I_IUCLC(tty) || I_IGNCR(tty) ||
	    I_ICRNL(tty) || I_INLCR(tty) || L_ICANON(tty) ||
	    I_IXON(tty) || L_ISIG(tty) || L_ECHO(tty) ||
	    I_PARMRK(tty)) {
		cli();
		memset(tty->process_char_map, 0, 256/32);

		if (I_IGNCR(tty) || I_ICRNL(tty))
			set_bit('\r', &tty->process_char_map);
		if (I_INLCR(tty))
			set_bit('\n', &tty->process_char_map);

		if (L_ICANON(tty)) {
			set_bit(ERASE_CHAR(tty), &tty->process_char_map);
			set_bit(KILL_CHAR(tty), &tty->process_char_map);
			set_bit(EOF_CHAR(tty), &tty->process_char_map);
			set_bit('\n', &tty->process_char_map);
			set_bit(EOL_CHAR(tty), &tty->process_char_map);
			if (L_IEXTEN(tty)) {
				set_bit(WERASE_CHAR(tty),
					&tty->process_char_map);
				set_bit(LNEXT_CHAR(tty),
					&tty->process_char_map);
				set_bit(EOL2_CHAR(tty),
					&tty->process_char_map);
				if (L_ECHO(tty))
					set_bit(REPRINT_CHAR(tty),
						&tty->process_char_map);
			}
		}
		if (I_IXON(tty)) {
			set_bit(START_CHAR(tty), &tty->process_char_map);
			set_bit(STOP_CHAR(tty), &tty->process_char_map);
		}
		if (L_ISIG(tty)) {
			set_bit(INTR_CHAR(tty), &tty->process_char_map);
			set_bit(QUIT_CHAR(tty), &tty->process_char_map);
			set_bit(SUSP_CHAR(tty), &tty->process_char_map);
		}
		clear_bit(__DISABLED_CHAR, &tty->process_char_map);
		sti();
		tty->raw = 0;
		tty->real_raw = 0;
	} else {
		tty->raw = 1;
		if ((I_IGNBRK(tty) || (!I_BRKINT(tty) && !I_PARMRK(tty))) &&
		    (I_IGNPAR(tty) || !I_INPCK(tty)) &&
		    (tty->driver.flags & TTY_DRIVER_REAL_RAW))
			tty->real_raw = 1;
		else
			tty->real_raw = 0;
	}
}

static void n_tty_close(struct tty_struct *tty)
{
	n_tty_flush_buffer(tty);
	if (tty->read_buf) {
		free_page((unsigned long) tty->read_buf);
		tty->read_buf = 0;
	}
}

static int n_tty_open(struct tty_struct *tty)
{
	if (!tty)
		return -EINVAL;

	if (!tty->read_buf) {
		tty->read_buf = (unsigned char *)
			get_free_page(intr_count ? GFP_ATOMIC : GFP_KERNEL);
		if (!tty->read_buf)
			return -ENOMEM;
	}
	memset(tty->read_buf, 0, N_TTY_BUF_SIZE);
	tty->read_head = tty->read_tail = tty->read_cnt = 0;
	memset(tty->read_flags, 0, sizeof(tty->read_flags));
	n_tty_set_termios(tty, 0);
	tty->minimum_to_wake = 1;
	tty->closing = 0;
	return 0;
}

static inline int input_available_p(struct tty_struct *tty, int amt)
{
	if (L_ICANON(tty)) {
		if (tty->canon_data)
			return 1;
	} else if (tty->read_cnt >= (amt ? amt : 1))
		return 1;

	return 0;
}

/*
 * Helper function to speed up read_chan.  It is only called when
 * ICANON is off; it copies characters straight from the tty queue to
 * user space directly.  It can be profitably called twice; once to
 * drain the space from the tail pointer to the (physical) end of the
 * buffer, and once to drain the space from the (physical) beginning of
 * the buffer to head pointer.
 */
static inline void copy_from_read_buf(struct tty_struct *tty,
				      unsigned char **b,
				      unsigned int *nr)

{
	int	n;

	n = MIN(*nr, MIN(tty->read_cnt, N_TTY_BUF_SIZE - tty->read_tail));
	if (!n)
		return;
	memcpy_tofs(*b, &tty->read_buf[tty->read_tail], n);
	tty->read_tail = (tty->read_tail + n) & (N_TTY_BUF_SIZE-1);
	tty->read_cnt -= n;
	*b += n;
	*nr -= n;
}

static int read_chan(struct tty_struct *tty, struct file *file,
		     unsigned char *buf, unsigned int nr)
{
	struct wait_queue wait = { current, NULL };
	int c;
	unsigned char *b = buf;
	int minimum, time;
	int retval = 0;
	int size;

do_it_again:

	if (!tty->read_buf) {
		printk("n_tty_read_chan: called with read_buf == NULL?!?\n");
		return -EIO;
	}

	/* Job control check -- must be done at start and after
	   every sleep (POSIX.1 7.1.1.4). */
	/* NOTE: not yet done after every sleep pending a thorough
	   check of the logic of this change. -- jlc */
	/* don't stop on /dev/console */
	if (file->f_inode->i_rdev != CONSOLE_DEV &&
	    current->tty == tty) {
		if (tty->pgrp <= 0)
			printk("read_chan: tty->pgrp <= 0!\n");
		else if (current->pgrp != tty->pgrp) {
			if (is_ignored(SIGTTIN) ||
			    is_orphaned_pgrp(current->pgrp))
				return -EIO;
			kill_pg(current->pgrp, SIGTTIN, 1);
			return -ERESTARTSYS;
		}
	}

	if (L_ICANON(tty)) {
		minimum = time = 0;
		current->timeout = (unsigned long) -1;
	} else {
		time = (HZ / 10) * TIME_CHAR(tty);
		minimum = MIN_CHAR(tty);
		if (minimum) {
		  	current->timeout = (unsigned long) -1;
			if (time)
				tty->minimum_to_wake = 1;
			else if (!tty->read_wait ||
				 (tty->minimum_to_wake > minimum))
				tty->minimum_to_wake = minimum;
		} else {
			if (time) {
				current->timeout = time + jiffies;
				time = 0;
			} else
				current->timeout = 0;
			tty->minimum_to_wake = minimum = 1;
		}
	}

	add_wait_queue(&tty->read_wait, &wait);
	while (1) {
		/* First test for status change. */
		if (tty->packet && tty->link->ctrl_status) {
			if (b != buf)
				break;
			put_fs_byte(tty->link->ctrl_status, b++);
			tty->link->ctrl_status = 0;
			break;
		}
		/* This statement must be first before checking for input
		   so that any interrupt will set the state back to
		   TASK_RUNNING. */
		current->state = TASK_INTERRUPTIBLE;
		
		if (((minimum - (b - buf)) < tty->minimum_to_wake) &&
		    ((minimum - (b - buf)) >= 1))
			tty->minimum_to_wake = (minimum - (b - buf));
		
		if (!input_available_p(tty, 0)) {
			if (tty->flags & (1 << TTY_SLAVE_CLOSED)) {
				retval = -EIO;
				break;
			}
			if (tty_hung_up_p(file))
				break;
			if (!current->timeout)
				break;
			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				break;
			}
			if (current->signal & ~current->blocked) {
				retval = -ERESTARTSYS;
				break;
			}
			schedule();
			continue;
		}
		current->state = TASK_RUNNING;

		/* Deal with packet mode. */
		if (tty->packet && b == buf) {
			put_fs_byte(TIOCPKT_DATA, b++);
			nr--;
		}

		if (L_ICANON(tty)) {
			while (1) {
				int eol;

				disable_bh(TQUEUE_BH);
				if (!tty->read_cnt) {
					enable_bh(TQUEUE_BH);
					break;
				}
				eol = clear_bit(tty->read_tail,
						&tty->read_flags);
				c = tty->read_buf[tty->read_tail];
				tty->read_tail = ((tty->read_tail+1) &
						  (N_TTY_BUF_SIZE-1));
				tty->read_cnt--;
				enable_bh(TQUEUE_BH);
				if (!eol) {
					put_fs_byte(c, b++);
					if (--nr)
						continue;
					break;
				}
				if (--tty->canon_data < 0) {
					tty->canon_data = 0;
				}
				if (c != __DISABLED_CHAR) {
					put_fs_byte(c, b++);
					nr--;
				}
				break;
			}
		} else {
			disable_bh(TQUEUE_BH);
			copy_from_read_buf(tty, &b, &nr);
			copy_from_read_buf(tty, &b, &nr);
			enable_bh(TQUEUE_BH);
		}

		/* If there is enough space in the read buffer now, let the
		   low-level driver know. */
		if (tty->driver.unthrottle &&
		    (tty->read_cnt <= TTY_THRESHOLD_UNTHROTTLE)
		    && clear_bit(TTY_THROTTLED, &tty->flags))
			tty->driver.unthrottle(tty);

		if (b - buf >= minimum || !nr)
			break;
		if (time)
			current->timeout = time + jiffies;
	}
	remove_wait_queue(&tty->read_wait, &wait);

	if (!tty->read_wait)
		tty->minimum_to_wake = minimum;

	current->state = TASK_RUNNING;
	current->timeout = 0;
	size = b - buf;
	if (size && nr)
	        clear_bit(TTY_PUSH, &tty->flags);
        if (!size && clear_bit(TTY_PUSH, &tty->flags))
                goto do_it_again;
	if (!size && !retval)
	        clear_bit(TTY_PUSH, &tty->flags);
        return (size ? size : retval);
}

static int write_chan(struct tty_struct * tty, struct file * file,
		      unsigned char * buf, unsigned int nr)
{
	struct wait_queue wait = { current, NULL };
	int c;
	unsigned char *b = buf;
	int retval = 0;

	/* Job control check -- must be done at start (POSIX.1 7.1.1.4). */
	if (L_TOSTOP(tty) && file->f_inode->i_rdev != CONSOLE_DEV) {
		retval = tty_check_change(tty);
		if (retval)
			return retval;
	}

	add_wait_queue(&tty->write_wait, &wait);
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		if (current->signal & ~current->blocked) {
			retval = -ERESTARTSYS;
			break;
		}
		if (tty_hung_up_p(file) || (tty->link && !tty->link->count)) {
			retval = -EIO;
			break;
		}
		if (O_OPOST(tty)) {
			while (nr > 0) {
				c = get_fs_byte(b);
				if (opost(c, tty) < 0)
					break;
				b++; nr--;
			}
			if (tty->driver.flush_chars)
				tty->driver.flush_chars(tty);
		} else {
			c = tty->driver.write(tty, 1, b, nr);
			b += c;
			nr -= c;
		}
		if (!nr)
			break;
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&tty->write_wait, &wait);
	return (b - buf) ? b - buf : retval;
}

static int normal_select(struct tty_struct * tty, struct inode * inode,
			 struct file * file, int sel_type, select_table *wait)
{
	switch (sel_type) {
		case SEL_IN:
			if (input_available_p(tty, TIME_CHAR(tty) ? 0 :
					      MIN_CHAR(tty)))
				return 1;
			/* fall through */
		case SEL_EX:
			if (tty->packet && tty->link->ctrl_status)
				return 1;
			if (tty->flags & (1 << TTY_SLAVE_CLOSED))
				return 1;
			if (tty_hung_up_p(file))
				return 1;
			if (!tty->read_wait) {
				if (MIN_CHAR(tty) && !TIME_CHAR(tty))
					tty->minimum_to_wake = MIN_CHAR(tty);
				else
					tty->minimum_to_wake = 1;
			}
			select_wait(&tty->read_wait, wait);
			return 0;
		case SEL_OUT:
			if (tty->driver.chars_in_buffer(tty) < WAKEUP_CHARS)
				return 1;
			select_wait(&tty->write_wait, wait);
			return 0;
	}
	return 0;
}

struct tty_ldisc tty_ldisc_N_TTY = {
	TTY_LDISC_MAGIC,	/* magic */
	0,			/* num */
	0,			/* flags */
	n_tty_open,		/* open */
	n_tty_close,		/* close */
	n_tty_flush_buffer,	/* flush_buffer */
	n_tty_chars_in_buffer,	/* chars_in_buffer */
	read_chan,		/* read */
	write_chan,		/* write */
	n_tty_ioctl,		/* ioctl */
	n_tty_set_termios,	/* set_termios */
	normal_select,		/* select */
	n_tty_receive_buf,	/* receive_buf */
	n_tty_receive_room,	/* receive_room */
	0			/* write_wakeup */
};


/*
 *  linux/drivers/char/tty_ioctl.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 * Modified by Fred N. van Kempen, 01/29/93, to add line disciplines
 * which can be dynamically activated and de-activated by the line
 * discipline handling modules (like SLIP).
 */

#include <linux/types.h>
#include <linux/termios.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/tty.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/segment.h>
#include <asm/system.h>

#undef TTY_DEBUG_WAIT_UNTIL_SENT

#undef	DEBUG
#ifdef DEBUG
# define	PRINTK(x)	printk (x)
#else
# define	PRINTK(x)	/**/
#endif

/*
 * Internal flag options for termios setting behavior
 */
#define TERMIOS_FLUSH	1
#define TERMIOS_WAIT	2
#define TERMIOS_TERMIO	4

void tty_wait_until_sent(struct tty_struct * tty, int timeout)
{
	struct wait_queue wait = { current, NULL };

#ifdef TTY_DEBUG_WAIT_UNTIL_SENT
	printk("%s wait until sent...\n", tty_name(tty));
#endif
	if (!tty->driver.chars_in_buffer ||
	    !tty->driver.chars_in_buffer(tty))
		return;
	add_wait_queue(&tty->write_wait, &wait);
	current->counter = 0;	/* make us low-priority */
	if (timeout)
		current->timeout = timeout + jiffies;
	else
		current->timeout = (unsigned) -1;
	do {
#ifdef TTY_DEBUG_WAIT_UNTIL_SENT
		printk("waiting %s...(%d)\n", tty_name(tty), tty->driver.chars_in_buffer(tty));
#endif
		current->state = TASK_INTERRUPTIBLE;
		if (current->signal & ~current->blocked)
			break;
		if (!tty->driver.chars_in_buffer(tty))
			break;
		schedule();
	} while (current->timeout);
	current->state = TASK_RUNNING;
	remove_wait_queue(&tty->write_wait, &wait);
}

static void unset_locked_termios(struct termios *termios,
				 struct termios *old,
				 struct termios *locked)
{
	int	i;
	
#define NOSET_MASK(x,y,z) (x = ((x) & ~(z)) | ((y) & (z)))

	if (!locked) {
		printk("Warning?!? termios_locked is NULL.\n");
		return;
	}

	NOSET_MASK(termios->c_iflag, old->c_iflag, locked->c_iflag);
	NOSET_MASK(termios->c_oflag, old->c_oflag, locked->c_oflag);
	NOSET_MASK(termios->c_cflag, old->c_cflag, locked->c_cflag);
	NOSET_MASK(termios->c_lflag, old->c_lflag, locked->c_lflag);
	termios->c_line = locked->c_line ? old->c_line : termios->c_line;
	for (i=0; i < NCCS; i++)
		termios->c_cc[i] = locked->c_cc[i] ?
			old->c_cc[i] : termios->c_cc[i];
}

static int set_termios(struct tty_struct * tty, unsigned long arg, int opt)
{
	struct termio tmp_termio;
	struct termios tmp_termios;
	struct termios old_termios = *tty->termios;
	int retval, canon_change;

	retval = tty_check_change(tty);
	if (retval)
		return retval;

	if (opt & TERMIOS_TERMIO) {
		retval = verify_area(VERIFY_READ, (void *) arg, sizeof(struct termio));
		if (retval)
			return retval;
		tmp_termios = *tty->termios;
		memcpy_fromfs(&tmp_termio, (struct termio *) arg,
			      sizeof (struct termio));

#define SET_LOW_BITS(x,y)	((x) = (0xffff0000 & (x)) | (y))
		SET_LOW_BITS(tmp_termios.c_iflag, tmp_termio.c_iflag);
		SET_LOW_BITS(tmp_termios.c_oflag, tmp_termio.c_oflag);
		SET_LOW_BITS(tmp_termios.c_cflag, tmp_termio.c_cflag);
		SET_LOW_BITS(tmp_termios.c_lflag, tmp_termio.c_lflag);
		memcpy(&tmp_termios.c_cc, &tmp_termio.c_cc, NCC);
#undef SET_LOW_BITS
	} else {
		retval = verify_area(VERIFY_READ, (void *) arg, sizeof(struct termios));
		if (retval)
			return retval;
		memcpy_fromfs(&tmp_termios, (struct termios *) arg,
			      sizeof (struct termios));
	}

	if ((opt & TERMIOS_FLUSH) && tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);

	if (opt & TERMIOS_WAIT)
		tty_wait_until_sent(tty, 0);

	cli();
	*tty->termios = tmp_termios;
	unset_locked_termios(tty->termios, &old_termios, tty->termios_locked);
	canon_change = (old_termios.c_lflag ^ tty->termios->c_lflag) & ICANON;
	if (canon_change) {
		memset(&tty->read_flags, 0, sizeof tty->read_flags);
		tty->canon_head = tty->read_tail;
		tty->canon_data = 0;
		tty->erasing = 0;
	}
	sti();
	if (canon_change && !L_ICANON(tty) && tty->read_cnt)
		/* Get characters left over from canonical mode. */
		wake_up_interruptible(&tty->read_wait);

	/* see if packet mode change of state */

	if (tty->link && tty->link->packet) {
		int old_flow = ((old_termios.c_iflag & IXON) &&
				(old_termios.c_cc[VSTOP] == '\023') &&
				(old_termios.c_cc[VSTART] == '\021'));
		int new_flow = (I_IXON(tty) &&
				STOP_CHAR(tty) == '\023' &&
				START_CHAR(tty) == '\021');
		if (old_flow != new_flow) {
			tty->ctrl_status &= ~(TIOCPKT_DOSTOP | TIOCPKT_NOSTOP);
			if (new_flow)
				tty->ctrl_status |= TIOCPKT_DOSTOP;
			else
				tty->ctrl_status |= TIOCPKT_NOSTOP;
			wake_up_interruptible(&tty->link->read_wait);
		}
	}

	if (tty->driver.set_termios)
		(*tty->driver.set_termios)(tty, &old_termios);

	if (tty->ldisc.set_termios)
		(*tty->ldisc.set_termios)(tty, &old_termios);
		
	return 0;
}

static int get_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;

	i = verify_area(VERIFY_WRITE, termio, sizeof (struct termio));
	if (i)
		return i;
	tmp_termio.c_iflag = tty->termios->c_iflag;
	tmp_termio.c_oflag = tty->termios->c_oflag;
	tmp_termio.c_cflag = tty->termios->c_cflag;
	tmp_termio.c_lflag = tty->termios->c_lflag;
	tmp_termio.c_line = tty->termios->c_line;
	for(i=0 ; i < NCC ; i++)
		tmp_termio.c_cc[i] = tty->termios->c_cc[i];
	memcpy_tofs(termio, &tmp_termio, sizeof (struct termio));
	return 0;
}

static unsigned long inq_canon(struct tty_struct * tty)
{
	int nr, head, tail;

	if (!tty->canon_data || !tty->read_buf)
		return 0;
	head = tty->canon_head;
	tail = tty->read_tail;
	nr = (head - tail) & (N_TTY_BUF_SIZE-1);
	/* Skip EOF-chars.. */
	while (head != tail) {
		if (test_bit(tail, &tty->read_flags) &&
		    tty->read_buf[tail] == __DISABLED_CHAR)
			nr--;
		tail = (tail+1) & (N_TTY_BUF_SIZE-1);
	}
	return nr;
}

int n_tty_ioctl(struct tty_struct * tty, struct file * file,
		       unsigned int cmd, unsigned long arg)
{
	struct tty_struct * real_tty;
	int retval;
	int opt = 0;

	if (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver.subtype == PTY_TYPE_MASTER)
		real_tty = tty->link;
	else
		real_tty = tty;

	switch (cmd) {
		case TCGETS:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
					     sizeof (struct termios));
			if (retval)
				return retval;
			memcpy_tofs((struct termios *) arg,
				    real_tty->termios,
				    sizeof (struct termios));
			return 0;
		case TCSETSF:
			opt |= TERMIOS_FLUSH;
		case TCSETSW:
			opt |= TERMIOS_WAIT;
		case TCSETS:
			return set_termios(real_tty, arg, opt);
		case TCGETA:
			return get_termio(real_tty,(struct termio *) arg);
		case TCSETAF:
			opt |= TERMIOS_FLUSH;
		case TCSETAW:
			opt |= TERMIOS_WAIT;
		case TCSETA:
			return set_termios(real_tty, arg, opt|TERMIOS_TERMIO);
		case TCXONC:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			switch (arg) {
			case TCOOFF:
				stop_tty(tty);
				break;
			case TCOON:
				start_tty(tty);
				break;
			case TCIOFF:
				if (STOP_CHAR(tty) != __DISABLED_CHAR)
					tty->driver.write(tty, 0,
							  &STOP_CHAR(tty), 1);
				break;
			case TCION:
				if (START_CHAR(tty) != __DISABLED_CHAR)
					tty->driver.write(tty, 0,
							  &START_CHAR(tty), 1);
				break;
			default:
				return -EINVAL;
			}
			return 0;
		case TCFLSH:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			switch (arg) {
			case TCIFLUSH:
				if (tty->ldisc.flush_buffer)
					tty->ldisc.flush_buffer(tty);
				break;
			case TCIOFLUSH:
				if (tty->ldisc.flush_buffer)
					tty->ldisc.flush_buffer(tty);
				/* fall through */
			case TCOFLUSH:
				if (tty->driver.flush_buffer)
					tty->driver.flush_buffer(tty);
				break;
			default:
				return -EINVAL;
			}
			return 0;
		case TIOCOUTQ:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
					     sizeof (unsigned long));
			if (retval)
				return retval;
			if (tty->driver.chars_in_buffer)
				put_fs_long(tty->driver.chars_in_buffer(tty),
					    (unsigned long *) arg);
			else
				put_fs_long(0, (unsigned long *) arg);
			return 0;
		case TIOCINQ:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
					     sizeof (unsigned long));
			if (retval)
				return retval;
			if (L_ICANON(tty))
				put_fs_long(inq_canon(tty),
					(unsigned long *) arg);
			else
				put_fs_long(tty->read_cnt,
					    (unsigned long *) arg);
			return 0;
		case TIOCGLCKTRMIOS:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
					     sizeof (struct termios));
			if (retval)
				return retval;
			memcpy_tofs((struct termios *) arg,
				    real_tty->termios_locked,
				    sizeof (struct termios));
			return 0;
		case TIOCSLCKTRMIOS:
			if (!suser())
				return -EPERM;
			retval = verify_area(VERIFY_READ, (void *) arg,
					     sizeof (struct termios));
			if (retval)
				return retval;
			memcpy_fromfs(real_tty->termios_locked,
				      (struct termios *) arg,
				      sizeof (struct termios));
			return 0;
		case TIOCPKT:
			if (tty->driver.type != TTY_DRIVER_TYPE_PTY ||
			    tty->driver.subtype != PTY_TYPE_MASTER)
				return -ENOTTY;
			retval = verify_area(VERIFY_READ, (void *) arg,
					     sizeof (unsigned long));
			if (retval)
				return retval;
			if (get_fs_long(arg)) {
				if (!tty->packet) {
					tty->packet = 1;
					tty->link->ctrl_status = 0;
				}
			} else
				tty->packet = 0;
			return 0;
		/* These two ioctl's always return success; even if */
		/* the driver doesn't support them. */
		case TCSBRK: case TCSBRKP:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (!tty->driver.ioctl)
				return 0;
			tty->driver.ioctl(tty, file, cmd, arg);
			return 0;
		default:
			return -ENOIOCTLCMD;
		}
}

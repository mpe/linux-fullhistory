/*
 *  linux/drivers/char/pty.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *	pty.c
 *
 * This module exports the following pty function:
 * 
 * 	int  pty_open(struct tty_struct * tty, struct file * filp);
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>

struct pty_struct {
	int	magic;
	struct wait_queue * open_wait;
};

#define PTY_MAGIC 0x5001

#define PTY_BUF_SIZE PAGE_SIZE/2

/*
 * tmp_buf is used as a temporary buffer by pty_write.  We need to
 * lock it in case the memcpy_fromfs blocks while swapping in a page,
 * and some other program tries to do a pty write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the PTY's, since it significantly saves memory if
 * large numbers of PTY's are open.
 */
static unsigned char *tmp_buf;
static struct semaphore tmp_buf_sem = MUTEX;

struct tty_driver pty_driver, pty_slave_driver;
struct tty_driver old_pty_driver, old_pty_slave_driver;
static int pty_refcount;

static struct tty_struct *pty_table[NR_PTYS];
static struct termios *pty_termios[NR_PTYS];
static struct termios *pty_termios_locked[NR_PTYS];
static struct tty_struct *ttyp_table[NR_PTYS];
static struct termios *ttyp_termios[NR_PTYS];
static struct termios *ttyp_termios_locked[NR_PTYS];
static struct pty_struct pty_state[NR_PTYS];

#define MIN(a,b)	((a) < (b) ? (a) : (b))

static void pty_close(struct tty_struct * tty, struct file * filp)
{
	if (!tty)
		return;
	if (tty->driver.subtype == PTY_TYPE_MASTER) {
		if (tty->count > 1)
			printk("master pty_close: count = %d!!\n", tty->count);
	} else {
		if (tty->count > 2)
			return;
	}
	wake_up_interruptible(&tty->read_wait);
	wake_up_interruptible(&tty->write_wait);
	if (!tty->link)
		return;
	wake_up_interruptible(&tty->link->read_wait);
	wake_up_interruptible(&tty->link->write_wait);
	set_bit(TTY_OTHER_CLOSED, &tty->link->flags);
	if (tty->driver.subtype == PTY_TYPE_MASTER) {
		tty_hangup(tty->link);
		set_bit(TTY_OTHER_CLOSED, &tty->flags);
	}
}

/*
 * The unthrottle routine is called by the line discipline to signal
 * that it can receive more characters.  For PTY's, the TTY_THROTTLED
 * flag is always set, to force the line discipline to always call the
 * unthrottle routine when there are fewer than TTY_THRESHOLD_UNTHROTTLE 
 * characters in the queue.  This is necessary since each time this
 * happens, we need to wake up any sleeping processes that could be
 * (1) trying to send data to the pty, or (2) waiting in wait_until_sent()
 * for the pty buffer to be drained.
 */
static void pty_unthrottle(struct tty_struct * tty)
{
	struct tty_struct *o_tty = tty->link;

	if (!o_tty)
		return;

	if ((o_tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    o_tty->ldisc.write_wakeup)
		(o_tty->ldisc.write_wakeup)(o_tty);
	wake_up_interruptible(&o_tty->write_wait);
	set_bit(TTY_THROTTLED, &tty->flags);
}

static int pty_write(struct tty_struct * tty, int from_user,
		       const unsigned char *buf, int count)
{
	struct tty_struct *to = tty->link;
	int	c=0, n, r;
	char	*temp_buffer;

	if (!to || tty->stopped)
		return 0;
	
	if (from_user) {
		down(&tmp_buf_sem);
		temp_buffer = tmp_buf +
			((tty->driver.subtype-1) * PTY_BUF_SIZE);
		while (count > 0) {
			n = MIN(count, PTY_BUF_SIZE);
			memcpy_fromfs(temp_buffer, buf, n);
			r = to->ldisc.receive_room(to);
			if (r <= 0)
				break;
			n = MIN(n, r);
			to->ldisc.receive_buf(to, temp_buffer, 0, n);
			buf += n;  c+= n;
			count -= n;
		}
		up(&tmp_buf_sem);
	} else {
		c = MIN(count, to->ldisc.receive_room(to));
		to->ldisc.receive_buf(to, buf, 0, c);
	}
	
	return c;
}

static int pty_write_room(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;

	if (!to || tty->stopped)
		return 0;

	return to->ldisc.receive_room(to);
}

static int pty_chars_in_buffer(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;

	if (!to || !to->ldisc.chars_in_buffer)
		return 0;

	return to->ldisc.chars_in_buffer(to);
}

static void pty_flush_buffer(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;
	
	if (!to)
		return;
	
	if (to->ldisc.flush_buffer)
		to->ldisc.flush_buffer(to);
	
	if (to->packet) {
		tty->ctrl_status |= TIOCPKT_FLUSHWRITE;
		wake_up_interruptible(&to->read_wait);
	}
}

int pty_open(struct tty_struct *tty, struct file * filp)
{
#if PTY_SLAVE_WAITS_ON_OPEN
	struct wait_queue wait = { current, NULL };
#endif
	int	retval;
	int	line;
	struct	pty_struct *pty;
	
	if (!tty || !tty->link)
		return -ENODEV;
	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line < 0) || (line >= NR_PTYS))
		return -ENODEV;
	pty = pty_state + line;
	tty->driver_data = pty;

	if (!tmp_buf) {
		unsigned long page = get_free_page(GFP_KERNEL);
		if (!tmp_buf) {
			if (!page)
				return -ENOMEM;
			tmp_buf = (unsigned char *) page;
		} else
			free_page(page);
	}

	clear_bit(TTY_OTHER_CLOSED, &tty->link->flags);
	wake_up_interruptible(&pty->open_wait);
	set_bit(TTY_THROTTLED, &tty->flags);
	if (filp->f_flags & O_NDELAY)
		return 0;
	/*
	 * If we're opening the master pty, just return.  If we're
	 * trying to open the slave pty, then we have to wait for the
	 * master pty to open.
	 */
	if (tty->driver.subtype == PTY_TYPE_MASTER)
		return 0;
	retval = 0;
#if PTY_SLAVE_WAITS_ON_OPEN
	add_wait_queue(&pty->open_wait, &wait);
	while (1) {
		if (current->signal & ~current->blocked) {
			retval = -ERESTARTSYS;
			break;
		}
		/*
		 * Block until the master is open...
		 */
		current->state = TASK_INTERRUPTIBLE;
		if (tty->link->count &&
		    !test_bit(TTY_OTHER_CLOSED, &tty->flags))
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&pty->open_wait, &wait);
#else
	if (!tty->link->count || test_bit(TTY_OTHER_CLOSED, &tty->flags))
		retval = -EPERM;
#endif
	return retval;
}

static void pty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
        tty->termios->c_cflag &= ~(CSIZE | PARENB);
        tty->termios->c_cflag |= (CS8 | CREAD);
}

int pty_init(void)
{
	memset(&pty_state, 0, sizeof(pty_state));
	memset(&pty_driver, 0, sizeof(struct tty_driver));
	pty_driver.magic = TTY_DRIVER_MAGIC;
	pty_driver.name = "pty";
	pty_driver.major = PTY_MASTER_MAJOR;
	pty_driver.minor_start = 0;
	pty_driver.num = NR_PTYS;
	pty_driver.type = TTY_DRIVER_TYPE_PTY;
	pty_driver.subtype = PTY_TYPE_MASTER;
	pty_driver.init_termios = tty_std_termios;
	pty_driver.init_termios.c_iflag = 0;
	pty_driver.init_termios.c_oflag = 0;
	pty_driver.init_termios.c_cflag = B38400 | CS8 | CREAD;
	pty_driver.init_termios.c_lflag = 0;
	pty_driver.flags = TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW;
	pty_driver.refcount = &pty_refcount;
	pty_driver.table = pty_table;
	pty_driver.termios = pty_termios;
	pty_driver.termios_locked = pty_termios_locked;
	pty_driver.other = &pty_slave_driver;

	pty_driver.open = pty_open;
	pty_driver.close = pty_close;
	pty_driver.write = pty_write;
	pty_driver.write_room = pty_write_room;
	pty_driver.flush_buffer = pty_flush_buffer;
	pty_driver.chars_in_buffer = pty_chars_in_buffer;
	pty_driver.unthrottle = pty_unthrottle;
	pty_driver.set_termios = pty_set_termios;

	pty_slave_driver = pty_driver;
	pty_slave_driver.name = "ttyp";
	pty_slave_driver.subtype = PTY_TYPE_SLAVE;
	pty_slave_driver.major = PTY_SLAVE_MAJOR;
	pty_slave_driver.minor_start = 0;
	pty_slave_driver.init_termios = tty_std_termios;
	pty_slave_driver.init_termios.c_cflag = B38400 | CS8 | CREAD;
	pty_slave_driver.table = ttyp_table;
	pty_slave_driver.termios = ttyp_termios;
	pty_slave_driver.termios_locked = ttyp_termios_locked;
	pty_slave_driver.other = &pty_driver;

	old_pty_driver = pty_driver;
	old_pty_driver.major = TTY_MAJOR;
	old_pty_driver.minor_start = 128;
	old_pty_driver.num = (NR_PTYS > 64) ? 64 : NR_PTYS;
	old_pty_driver.other = &old_pty_slave_driver;
	
	old_pty_slave_driver = pty_slave_driver;
	old_pty_slave_driver.major = TTY_MAJOR;
	old_pty_slave_driver.minor_start = 192;
	old_pty_slave_driver.num = (NR_PTYS > 64) ? 64 : NR_PTYS;
	old_pty_slave_driver.other = &old_pty_driver;

	tmp_buf = 0;

	if (tty_register_driver(&pty_driver))
		panic("Couldn't register pty driver");
	if (tty_register_driver(&pty_slave_driver))
		panic("Couldn't register pty slave driver");
	if (tty_register_driver(&old_pty_driver))
		panic("Couldn't register compat pty driver");
	if (tty_register_driver(&old_pty_slave_driver))
		panic("Couldn't register compat pty slave driver");
	
	return 0;
}

/*
 *  linux/kernel/tty_io.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl, who also corrected VMIN = VTIME = 0.
 *
 * Modified by Theodore Ts'o, 9/14/92, to dynamically allocate the
 * tty_struct and tty_queue structures.  Previously there was a array
 * of 256 tty_struct's which was statically allocated, and the
 * tty_queue structures were allocated at boot time.  Both are now
 * dynamically allocated only when the tty is open.
 *
 * Also restructured routines so that there is more of a separation
 * between the high-level tty routines (tty_io.c and tty_ioctl.c) and
 * the low-level tty routines (serial.c, pty.c, console.c).  This
 * makes for cleaner and more compact code.  -TYT, 9/17/92 
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/keyboard.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include "vt_kern.h"

struct tty_struct *tty_table[256];
struct termios *tty_termios[256]; /* We need to keep the termios state */
				  /* around, even when a tty is closed */

/*
 * fg_console is the current virtual console,
 * redirect is the pseudo-tty that console output
 * is redirected to if asked by TIOCCONS.
 */
int fg_console = 0;
struct tty_struct * redirect = NULL;
struct wait_queue * keypress_wait = NULL;

static int initialize_tty_struct(struct tty_struct *tty, int line);
static int tty_read(struct inode *, struct file *, char *, int);
static int tty_write(struct inode *, struct file *, char *, int);
static int tty_select(struct inode *, struct file *, int, select_table *);
static int tty_open(struct inode *, struct file *);
static void tty_release(struct inode *, struct file *);

void put_tty_queue(char c, struct tty_queue * queue)
{
	int head;
	unsigned long flags;

	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	head = (queue->head + 1) & (TTY_BUF_SIZE-1);
	if (head != queue->tail) {
		queue->buf[queue->head] = c;
		queue->head = head;
	}
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}

int get_tty_queue(struct tty_queue * queue)
{
	int result = -1;
	unsigned long flags;

	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	if (queue->tail != queue->head) {
		result = 0xff & queue->buf[queue->tail];
		queue->tail = (queue->tail + 1) & (TTY_BUF_SIZE-1);
	}
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
	return result;
}

void tty_write_flush(struct tty_struct * tty)
{
	if (!tty->write || EMPTY(&tty->write_q))
		return;
	if (set_bit(TTY_WRITE_BUSY,&tty->flags))
		return;
	tty->write(tty);
	if (clear_bit(TTY_WRITE_BUSY,&tty->flags))
		printk("tty_write_flush: bit already cleared\n");
}

void tty_read_flush(struct tty_struct * tty)
{
	if (!tty || EMPTY(&tty->read_q))
		return;
	if (set_bit(TTY_READ_BUSY, &tty->flags))
		return;
	copy_to_cooked(tty);
	if (clear_bit(TTY_READ_BUSY, &tty->flags))
		printk("tty_read_flush: bit already cleared\n");
}

static int hung_up_tty_read(struct inode * inode, struct file * file, char * buf, int count)
{
	return 0;
}

static int hung_up_tty_write(struct inode * inode, struct file * file, char * buf, int count)
{
	return -EIO;
}

static int hung_up_tty_select(struct inode * inode, struct file * filp, int sel_type, select_table * wait)
{
	return 1;
}

static int tty_lseek(struct inode * inode, struct file * file, off_t offset, int orig)
{
	return -EBADF;
}

static struct file_operations tty_fops = {
	tty_lseek,
	tty_read,
	tty_write,
	NULL,		/* tty_readdir */
	tty_select,
	tty_ioctl,
	NULL,		/* tty_mmap */
	tty_open,
	tty_release
};

static struct file_operations hung_up_tty_fops = {
	tty_lseek,
	hung_up_tty_read,
	hung_up_tty_write,
	NULL,		/* hung_up_tty_readdir */
	hung_up_tty_select,
	tty_ioctl,
	NULL,		/* hung_up_tty_mmap */
	tty_open,
	tty_release
};

void tty_hangup(struct tty_struct * tty)
{
	struct file * filp;
	int dev;

	if (!tty)
		return;
	dev = 0x0400 + tty->line;
	filp = file_table + NR_FILE;
	while (filp-- > file_table) {
		if (!filp->f_count)
			continue;
		if (filp->f_rdev != dev)
			continue;
		if (filp->f_op != &tty_fops)
			continue;
		filp->f_op = &hung_up_tty_fops;
	}
	wake_up_interruptible(&tty->secondary.proc_list);
	wake_up_interruptible(&tty->read_q.proc_list);
	wake_up_interruptible(&tty->write_q.proc_list);
	if (tty->session > 0)
		kill_sl(tty->session,SIGHUP,1);
}

static inline int hung_up(struct file * filp)
{
	return filp->f_op == &hung_up_tty_fops;
}

extern int kill_proc(int pid, int sig, int priv);

/*
 * Performs the back end of a vt switch
 */
void complete_change_console(unsigned int new_console)
{
	unsigned char old_vc_mode;

	if (new_console == fg_console || new_console >= NR_CONSOLES)
		return;

	/*
	 * If we're switching, we could be going from KD_GRAPHICS to
	 * KD_TEXT mode or vice versa, which means we need to blank or
	 * unblank the screen later.
	 */
	old_vc_mode = vt_cons[fg_console].vc_mode;
	update_screen(new_console);

	/*
	 * If this new console is under process control, send it a signal
	 * telling it that it has acquired. Also check if it has died and
	 * clean up (similar to logic employed in change_console())
	 */
	if (vt_cons[new_console].vt_mode.mode == VT_PROCESS)
	{
		/*
		 * Send the signal as privileged - kill_proc() will
		 * tell us if the process has gone or something else
		 * is awry
		 */
		if (kill_proc(vt_cons[new_console].vt_pid,
			      vt_cons[new_console].vt_mode.acqsig,
			      1) != 0)
		{
		/*
		 * The controlling process has died, so we revert back to
		 * normal operation. In this case, we'll also change back
		 * to KD_TEXT mode. I'm not sure if this is strictly correct
		 * but it saves the agony when the X server dies and the screen
		 * remains blanked due to KD_GRAPHICS! It would be nice to do
		 * this outside of VT_PROCESS but there is no single process
		 * to account for and tracking tty count may be undesirable.
		 */
			vt_cons[new_console].vc_mode = KD_TEXT;
			clr_vc_kbd_flag(kbd_table + new_console, VC_RAW);
 			vt_cons[new_console].vt_mode.mode = VT_AUTO;
 			vt_cons[new_console].vt_mode.waitv = 0;
 			vt_cons[new_console].vt_mode.relsig = 0;
			vt_cons[new_console].vt_mode.acqsig = 0;
			vt_cons[new_console].vt_mode.frsig = 0;
			vt_cons[new_console].vt_pid = -1;
			vt_cons[new_console].vt_newvt = -1;
		}
	}

	/*
	 * We do this here because the controlling process above may have
	 * gone, and so there is now a new vc_mode
	 */
	if (old_vc_mode != vt_cons[new_console].vc_mode)
	{
		if (vt_cons[new_console].vc_mode == KD_TEXT)
			unblank_screen();
		else
		{
			timer_active &= ~(1<<BLANK_TIMER);
			blank_screen();
		}
	}

	return;
}

/*
 * Performs the front-end of a vt switch
 */
void change_console(unsigned int new_console)
{
	if (new_console == fg_console || new_console >= NR_CONSOLES)
		return;

	/*
	 * If this vt is in process mode, then we need to handshake with
	 * that process before switching. Essentially, we store where that
	 * vt wants to switch to and wait for it to tell us when it's done
	 * (via VT_RELDISP ioctl).
	 *
	 * We also check to see if the controlling process still exists.
	 * If it doesn't, we reset this vt to auto mode and continue.
	 * This is a cheap way to track process control. The worst thing
	 * that can happen is: we send a signal to a process, it dies, and
	 * the switch gets "lost" waiting for a response; hopefully, the
	 * user will try again, we'll detect the process is gone (unless
	 * the user waits just the right amount of time :-) and revert the
	 * vt to auto control.
	 */
	if (vt_cons[fg_console].vt_mode.mode == VT_PROCESS)
	{
		/*
		 * Send the signal as privileged - kill_proc() will
		 * tell us if the process has gone or something else
		 * is awry
		 */
		if (kill_proc(vt_cons[fg_console].vt_pid,
			      vt_cons[fg_console].vt_mode.relsig,
			      1) == 0)
		{
			/*
			 * It worked. Mark the vt to switch to and
			 * return. The process needs to send us a
			 * VT_RELDISP ioctl to complete the switch.
			 */
			vt_cons[fg_console].vt_newvt = new_console;
			return;
		}

		/*
		 * The controlling process has died, so we revert back to
		 * normal operation. In this case, we'll also change back
		 * to KD_TEXT mode. I'm not sure if this is strictly correct
		 * but it saves the agony when the X server dies and the screen
		 * remains blanked due to KD_GRAPHICS! It would be nice to do
		 * this outside of VT_PROCESS but there is no single process
		 * to account for and tracking tty count may be undesirable.
		 */
		vt_cons[fg_console].vc_mode = KD_TEXT;
		clr_vc_kbd_flag(kbd_table + fg_console, VC_RAW);
		vt_cons[fg_console].vt_mode.mode = VT_AUTO;
		vt_cons[fg_console].vt_mode.waitv = 0;
		vt_cons[fg_console].vt_mode.relsig = 0;
		vt_cons[fg_console].vt_mode.acqsig = 0;
		vt_cons[fg_console].vt_mode.frsig = 0;
		vt_cons[fg_console].vt_pid = -1;
		vt_cons[fg_console].vt_newvt = -1;
		/*
		 * Fall through to normal (VT_AUTO) handling of the switch...
		 */
	}

	/*
	 * Ignore all switches in KD_GRAPHICS+VT_AUTO mode
	 */
	if (vt_cons[fg_console].vc_mode == KD_GRAPHICS)
		return;

	complete_change_console(new_console);
}

void wait_for_keypress(void)
{
	interruptible_sleep_on(&keypress_wait);
}

void copy_to_cooked(struct tty_struct * tty)
{
	int c;

	if (!tty) {
		printk("copy_to_cooked: called with NULL tty\n");
		return;
	}
	if (!tty->write) {
		printk("copy_to_cooked: tty %d has null write routine\n",
		       tty->line);
	}
	while (1) {
		/*
		 * Check to see how much room we have left in the
		 * secondary queue.  Send a throttle command or abort
		 * if necessary.
		 */
		c = LEFT(&tty->secondary);
		if (tty->throttle && (c < SQ_THRESHOLD_LW)
		    && !set_bit(TTY_SQ_THROTTLED, &tty->flags))
			tty->throttle(tty, TTY_THROTTLE_SQ_FULL);
		if (c == 0)
			break;
		c = get_tty_queue(&tty->read_q);
		if (c < 0)
			break;
		if (I_STRP(tty))
			c &= 0x7f;
		if (c==13) {
			if (I_CRNL(tty))
				c=10;
			else if (I_NOCR(tty))
				continue;
		} else if (c==10 && I_NLCR(tty))
			c=13;
		if (I_UCLC(tty))
			c=tolower(c);
		if (L_CANON(tty)) {
			if ((KILL_CHAR(tty) != __DISABLED_CHAR) &&
			    (c==KILL_CHAR(tty))) {
				/* deal with killing the input line */
				while(!(EMPTY(&tty->secondary) ||
					(c=LAST(&tty->secondary))==10 ||
					((EOF_CHAR(tty) != __DISABLED_CHAR) &&
					 (c==EOF_CHAR(tty))))) {
					if (L_ECHO(tty)) {
						if (c<32) {
							put_tty_queue(8, &tty->write_q);
							put_tty_queue(' ', &tty->write_q);
							put_tty_queue(8,&tty->write_q);
						}
						put_tty_queue(8,&tty->write_q);
						put_tty_queue(' ',&tty->write_q);
						put_tty_queue(8,&tty->write_q);
					}
					DEC(tty->secondary.head);
				}
				continue;
			}
			if ((ERASE_CHAR(tty) != __DISABLED_CHAR) &&
			    (c==ERASE_CHAR(tty))) {
				if (EMPTY(&tty->secondary) ||
				   (c=LAST(&tty->secondary))==10 ||
				   ((EOF_CHAR(tty) != __DISABLED_CHAR) &&
				    (c==EOF_CHAR(tty))))
					continue;
				if (L_ECHO(tty)) {
					if (c<32) {
						put_tty_queue(8,&tty->write_q);
						put_tty_queue(' ',&tty->write_q);
						put_tty_queue(8,&tty->write_q);
					}
					put_tty_queue(8,&tty->write_q);
					put_tty_queue(32,&tty->write_q);
					put_tty_queue(8,&tty->write_q);
				}
				DEC(tty->secondary.head);
				continue;
			}
		}
		if (I_IXON(tty)) {
			if ((STOP_CHAR(tty) != __DISABLED_CHAR) &&
			    (c==STOP_CHAR(tty))) {
			        tty->status_changed = 1;
				tty->ctrl_status |= TIOCPKT_STOP;
				tty->stopped=1;
				continue;
			}
			if (((I_IXANY(tty)) && tty->stopped) ||
			    ((START_CHAR(tty) != __DISABLED_CHAR) &&
			     (c==START_CHAR(tty)))) {
			        tty->status_changed = 1;
				tty->ctrl_status |= TIOCPKT_START;
				tty->stopped=0;
				continue;
			}
		}
		if (L_ISIG(tty)) {
			if ((INTR_CHAR(tty) != __DISABLED_CHAR) &&
			    (c==INTR_CHAR(tty))) {
				kill_pg(tty->pgrp, SIGINT, 1);
				flush_input(tty);
				continue;
			}
			if ((QUIT_CHAR(tty) != __DISABLED_CHAR) &&
			    (c==QUIT_CHAR(tty))) {
				kill_pg(tty->pgrp, SIGQUIT, 1);
				flush_input(tty);
				continue;
			}
			if ((SUSPEND_CHAR(tty) != __DISABLED_CHAR) &&
			    (c==SUSPEND_CHAR(tty))) {
				if (!is_orphaned_pgrp(tty->pgrp))
					kill_pg(tty->pgrp, SIGTSTP, 1);
				continue;
			}
		}
		if (c==10 || (EOF_CHAR(tty) != __DISABLED_CHAR &&
		    c==EOF_CHAR(tty)))
			tty->secondary.data++;
		if ((c==10) && (L_ECHO(tty) || (L_CANON(tty) && L_ECHONL(tty)))) {
			put_tty_queue(10,&tty->write_q);
			put_tty_queue(13,&tty->write_q);
		} else if (L_ECHO(tty)) {
			if (c<32 && L_ECHOCTL(tty)) {
				put_tty_queue('^',&tty->write_q);
				put_tty_queue(c+64, &tty->write_q);
			} else
				put_tty_queue(c, &tty->write_q);
		}
		put_tty_queue(c, &tty->secondary);
	}
	TTY_WRITE_FLUSH(tty);
	if (!EMPTY(&tty->secondary))
		wake_up_interruptible(&tty->secondary.proc_list);
	if (tty->write_q.proc_list && LEFT(&tty->write_q) > TTY_BUF_SIZE/2)
		wake_up_interruptible(&tty->write_q.proc_list);
	if (tty->throttle && (LEFT(&tty->read_q) >= RQ_THRESHOLD_HW)
	    && !clear_bit(TTY_RQ_THROTTLED, &tty->flags))
		tty->throttle(tty, TTY_THROTTLE_RQ_AVAIL);
	if (tty->throttle && (LEFT(&tty->secondary) >= SQ_THRESHOLD_HW)
	    && !clear_bit(TTY_SQ_THROTTLED, &tty->flags))
		tty->throttle(tty, TTY_THROTTLE_SQ_AVAIL);
}

int is_ignored(int sig)
{
	return ((current->blocked & (1<<(sig-1))) ||
	        (current->sigaction[sig-1].sa_handler == SIG_IGN));
}

static int available_canon_input(struct tty_struct *);
static void __wait_for_canon_input(struct file * file, struct tty_struct *);

static void wait_for_canon_input(struct file * file, struct tty_struct * tty)
{
	if (!available_canon_input(tty)) {
		if (current->signal & ~current->blocked)
			return;
		__wait_for_canon_input(file, tty);
	}
}

static int read_chan(struct tty_struct * tty, struct file * file, char * buf, int nr)
{
	struct wait_queue wait = { current, NULL };
	int c;
	char * b=buf;
	int minimum,time;

	if (L_CANON(tty))
		minimum = time = current->timeout = 0;
	else {
		time = 10L*tty->termios->c_cc[VTIME];
		minimum = tty->termios->c_cc[VMIN];
		if (minimum)
			current->timeout = 0xffffffff;
		else {
			if (time)
				current->timeout = time + jiffies;
			else
				current->timeout = 0;
			time = 0;
			minimum = 1;
		}
	}
	if (file->f_flags & O_NONBLOCK) {
		time = current->timeout = 0;
		if (L_CANON(tty)) {
			if (!available_canon_input(tty))
				return -EAGAIN;
		}
	} else if (L_CANON(tty)) {
		wait_for_canon_input(file, tty);
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
	}
	if (minimum>nr)
		minimum = nr;

	/* deal with packet mode:  First test for status change */
	if (tty->packet && tty->link && tty->link->status_changed) {
		put_fs_byte (tty->link->ctrl_status, b);
		tty->link->status_changed = 0;
		return 1;
	}
	  
	/* now bump the buffer up one. */
	if (tty->packet) {
		put_fs_byte (0,b++);
		nr--;
		/* this really shouldn't happen, but we need to 
		put it here. */
		if (nr == 0)
			return 1;
	}
	add_wait_queue(&tty->secondary.proc_list, &wait);
	while (nr>0) {
		TTY_READ_FLUSH(tty);
		if (tty->link)
			TTY_WRITE_FLUSH(tty->link);
		while (nr > 0 && ((c = get_tty_queue(&tty->secondary)) >= 0)) {
			if ((EOF_CHAR(tty) != __DISABLED_CHAR &&
			     c==EOF_CHAR(tty)) || c==10)
				tty->secondary.data--;
			if ((EOF_CHAR(tty) != __DISABLED_CHAR &&
			     c==EOF_CHAR(tty)) && L_CANON(tty))
				break;
			put_fs_byte(c,b++);
			nr--;
			if (time)
				current->timeout = time+jiffies;
			if (c==10 && L_CANON(tty))
				break;
		};
		wake_up_interruptible(&tty->read_q.proc_list);
		/*
		 * If there is enough space in the secondary queue
		 * now, let the low-level driver know.
		 */
		if (tty->throttle && (LEFT(&tty->secondary) >= SQ_THRESHOLD_HW)
		    && !clear_bit(TTY_SQ_THROTTLED, &tty->flags))
			tty->throttle(tty, TTY_THROTTLE_SQ_AVAIL);
		if (b-buf >= minimum || !current->timeout)
			break;
		if (current->signal & ~current->blocked) 
			break;
		if (tty->link && !tty->link->count)
			break;
		TTY_READ_FLUSH(tty);
		if (tty->link)
			TTY_WRITE_FLUSH(tty->link);
		if (!EMPTY(&tty->secondary))
			continue;
		if (hung_up(file))
			break;
		current->state = TASK_INTERRUPTIBLE;
		if (EMPTY(&tty->secondary))
			schedule();
		current->state = TASK_RUNNING;
	}
	remove_wait_queue(&tty->secondary.proc_list, &wait);
	TTY_READ_FLUSH(tty);
	if (tty->link && tty->link->write)
		TTY_WRITE_FLUSH(tty->link);
	current->timeout = 0;

	/* packet mode sticks in an extra 0.  If that's all we've got,
	   we should count it a zero bytes. */
	if (tty->packet) {
		if ((b-buf) > 1)
			return b-buf;
	} else {
		if (b-buf)
			return b-buf;
	}

	if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	if (file->f_flags & O_NONBLOCK)
		return -EAGAIN;
	return 0;
}

static void __wait_for_canon_input(struct file * file, struct tty_struct * tty)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&tty->secondary.proc_list, &wait);
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		if (available_canon_input(tty))
			break;
		if (current->signal & ~current->blocked)
			break;
		if (hung_up(file))
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&tty->secondary.proc_list, &wait);
}

static int available_canon_input(struct tty_struct * tty)
{
	TTY_READ_FLUSH(tty);
	if (tty->link)
		if (tty->link->count)
			TTY_WRITE_FLUSH(tty->link);
		else
			return 1;
	if (FULL(&tty->read_q))
		return 1;
	if (tty->secondary.data)
		return 1;
	return 0;
}

static int write_chan(struct tty_struct * tty, struct file * file, char * buf, int nr)
{
	struct wait_queue wait = { current, NULL };
	char c, *b=buf;

	if (nr < 0)
		return -EINVAL;
	if (!nr)
		return 0;
	add_wait_queue(&tty->write_q.proc_list, &wait);
	while (nr>0) {
		if (current->signal & ~current->blocked)
			break;
		if (hung_up(file))
			break;
		if (tty->link && !tty->link->count) {
			send_sig(SIGPIPE,current,0);
			break;
		}
		current->state = TASK_INTERRUPTIBLE;
		if (FULL(&tty->write_q)) {
			TTY_WRITE_FLUSH(tty);
			if (FULL(&tty->write_q))
				schedule();
			current->state = TASK_RUNNING;
			continue;
		}
		current->state = TASK_RUNNING;
		while (nr>0 && !FULL(&tty->write_q)) {
			c=get_fs_byte(b);
			if (O_POST(tty)) {
				if (c=='\r' && O_CRNL(tty))
					c='\n';
				else if (c=='\n' && O_NLRET(tty))
					c='\r';
				if (c=='\n' && O_NLCR(tty) &&
				    !set_bit(TTY_CR_PENDING,&tty->flags)) {
					put_tty_queue(13,&tty->write_q);
					continue;
				}
				if (O_LCUC(tty))
					c=toupper(c);
			}
			b++; nr--;
			clear_bit(TTY_CR_PENDING,&tty->flags);
			put_tty_queue(c,&tty->write_q);
		}
		if (need_resched)
			schedule();
	}
	remove_wait_queue(&tty->write_q.proc_list, &wait);
	TTY_WRITE_FLUSH(tty);
	if (b-buf)
		return b-buf;
	if (tty->link && !tty->link->count)
		return -EPIPE;
	if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	return 0;
}

static int tty_read(struct inode * inode, struct file * file, char * buf, int count)
{
	int i, dev;
	struct tty_struct * tty;

	dev = file->f_rdev;
	if (MAJOR(dev) != 4) {
		printk("tty_read: pseudo-major != 4\n");
		return -EINVAL;
	}
	dev = MINOR(dev);
	tty = TTY_TABLE(dev);
	if (!tty)
		return -EIO;
	if (MINOR(inode->i_rdev) && (tty->pgrp > 0) &&
	    (current->tty == dev) &&
	    (tty->pgrp != current->pgrp))
		if (is_ignored(SIGTTIN) || is_orphaned_pgrp(current->pgrp))
			return -EIO;
		else {
			(void) kill_pg(current->pgrp, SIGTTIN, 1);
			return -ERESTARTSYS;
		}
	i = read_chan(tty,file,buf,count);
	if (i > 0)
		inode->i_atime = CURRENT_TIME;
	return i;
}

static int tty_write(struct inode * inode, struct file * file, char * buf, int count)
{
	int dev, i, is_console;
	struct tty_struct * tty;

	dev = file->f_rdev;
	is_console = (inode->i_rdev == 0x0400);
	if (MAJOR(dev) != 4) {
		printk("tty_write: pseudo-major != 4\n");
		return -EINVAL;
	}
	dev = MINOR(dev);
	if (is_console && redirect)
		tty = redirect;
	else
		tty = TTY_TABLE(dev);
	if (!tty || !tty->write)
		return -EIO;
	if (!is_console && L_TOSTOP(tty) && (tty->pgrp > 0) &&
	    (current->tty == dev) && (tty->pgrp != current->pgrp)) {
		if (is_orphaned_pgrp(current->pgrp))
			return -EIO;
		if (!is_ignored(SIGTTOU)) {
			(void) kill_pg(current->pgrp, SIGTTOU, 1);
			return -ERESTARTSYS;
		}
	}
	i = write_chan(tty,file,buf,count);
	if (i > 0)
		inode->i_mtime = CURRENT_TIME;
	return i;
}

/*
 * tty_open and tty_release keep up the tty count that contains the
 * number of opens done on a tty. We cannot use the inode-count, as
 * different inodes might point to the same tty.
 *
 * Open-counting is needed for pty masters, as well as for keeping
 * track of serial lines: DTR is dropped when the last close happens.
 *
 * The termios state of a pty is reset on first open so that
 * settings don't persist across reuse.
 */
static int tty_open(struct inode * inode, struct file * filp)
{
	struct tty_struct *tty, *o_tty;
	int dev, retval;

	dev = inode->i_rdev;
	if (MAJOR(dev) == 5)
		dev = current->tty;
	else
		dev = MINOR(dev);
	if (dev < 0)
		return -ENXIO;
	if (!dev)
		dev = fg_console + 1;
	filp->f_rdev = 0x0400 | dev;
/*
 * There be race-conditions here... Lots of them. Careful now.
 */
	tty = o_tty = NULL;
	tty = tty_table[dev];
	if (!tty) {
		tty = (struct tty_struct *) get_free_page(GFP_KERNEL);
		if (tty_table[dev]) {
			/*
			 * Stop our allocation of tty if race
			 * condition detected.
			 */
			if (tty)
				free_page((unsigned long) tty);
			tty = tty_table[dev];
		} else {
			if (!tty)
				return -ENOMEM;
			retval = initialize_tty_struct(tty, dev);
			if (retval) {
				free_page((unsigned long) tty);
				return retval;
			}
			tty_table[dev] = tty;
		}
	}
	tty->count++;			/* bump count to preserve tty */
	if (IS_A_PTY(dev)) {
		o_tty = tty_table[PTY_OTHER(dev)];
		if (!o_tty) {
			o_tty = (struct tty_struct *) get_free_page(GFP_KERNEL);
			if (tty_table[PTY_OTHER(dev)]) {
				/*
				 * Stop our allocation of o_tty if race
				 * condition detected.
				 */
				free_page((unsigned long) o_tty);
				o_tty = tty_table[PTY_OTHER(dev)];
			} else {
				if (!o_tty) {
					tty->count--;
					return -ENOMEM;
				}
				retval = initialize_tty_struct(o_tty, PTY_OTHER(dev));
				if (retval) {
					tty->count--;
					free_page((unsigned long) o_tty);
					return retval;
				}
				tty_table[PTY_OTHER(dev)] = o_tty;
			}
		}
		tty->link = o_tty;				
		o_tty->link = tty;
	}
	if (IS_A_PTY_MASTER(dev)) {
		if (tty->count > 1) {
			tty->count--;
			return -EAGAIN;
		}
		if (tty->link)
			tty->link->count++;
	} 
	retval = 0;

	/* clean up the packet stuff. */
	tty->status_changed = 0;
	tty->ctrl_status = 0;
	tty->packet = 0;

	if (!(filp->f_flags & O_NOCTTY) &&
	    current->leader &&
	    current->tty<0 &&
	    tty->session==0) {
		current->tty = dev;
		tty->session = current->session;
		tty->pgrp = current->pgrp;
	}
	if (tty->open)
		retval = tty->open(tty, filp);
	else
		retval = -ENODEV;
	if (retval) {
		tty->count--;
		if (IS_A_PTY_MASTER(dev) && tty->link)
			tty->link->count--;
	}
	return retval;
}

/*
 * Note that releasing a pty master also releases the child, so
 * we have to make the redirection checks after that and on both
 * sides of a pty.
 */
static void tty_release(struct inode * inode, struct file * filp)
{
	int dev;
	struct tty_struct * tty;
	unsigned long free_tty_struct;
	struct termios *free_termios;

	dev = filp->f_rdev;
	if (MAJOR(dev) != 4) {
		printk("tty_release: tty pseudo-major != 4\n");
		return;
	}
	dev = MINOR(filp->f_rdev);
	if (!dev)
		dev = fg_console+1;
	tty = tty_table[dev];
	if (!tty) {
		printk("tty_release: tty_table[%d] was NULL\n", dev);
		return;
	}
	if (IS_A_PTY_MASTER(dev) && tty->link)  {
		if (--tty->link->count < 0) {
			printk("tty_release: bad tty slave count (dev = %d): %d\n",
			       dev, tty->count);	
			tty->link->count = 0;
		}
	}
	if (--tty->count < 0) {
		printk("tty_release: bad tty_table[%d]->count: %d\n",
		       dev, tty->count);
		tty->count = 0;
	}
	if (tty->count)
		return;
	if (tty->close)
		tty->close(tty, filp);
	if (tty == redirect)
		redirect = NULL;
	if (tty->link && !tty->link->count && (tty->link == redirect))
		redirect = NULL;
	if (tty->link) {
		if (tty->link->count)
			return;
		/*
		 * Free the tty structure, being careful to avoid race conditions
		 */
		free_tty_struct = (unsigned long) tty_table[PTY_OTHER(dev)];
		tty_table[PTY_OTHER(dev)] = 0;
		free_page(free_tty_struct);
		/*
		 * If this is a PTY, free the termios structure, being
		 * careful to avoid race conditions
		 */
		if (IS_A_PTY(dev)) {
			free_termios = tty_termios[PTY_OTHER(dev)];
			tty_termios[PTY_OTHER(dev)] = 0;
			kfree_s(free_termios, sizeof(struct termios));
		}
	}
	/*
	 * Free the tty structure, being careful to avoid race conditions
	 */
	free_tty_struct = (unsigned long) tty_table[dev];
	tty_table[dev] = 0;	
	free_page(free_tty_struct);
	/*
	 * If this is a PTY, free the termios structure, being careful
	 * to avoid race conditions
	 */
	if (IS_A_PTY(dev)) {
		free_termios = tty_termios[dev];
		tty_termios[dev] = 0;
		kfree_s(free_termios, sizeof(struct termios));
	}
}

static int tty_select(struct inode * inode, struct file * filp, int sel_type, select_table * wait)
{
	int dev;
	struct tty_struct * tty;

	dev = filp->f_rdev;
	if (MAJOR(dev) != 4) {
		printk("tty_select: tty pseudo-major != 4\n");
		return 0;
	}
	dev = MINOR(filp->f_rdev);
	tty = TTY_TABLE(dev);
	if (!tty) {
		printk("tty_select: tty struct for dev %d was NULL\n", dev);
		return 0;
	}
	switch (sel_type) {
		case SEL_IN:
			if (L_CANON(tty)) {
				if (available_canon_input(tty))
					return 1;
			} else if (!EMPTY(&tty->secondary))
				return 1;
			if (tty->link && !tty->link->count)
				return 1;

			/* see if the status byte can be read. */
			if (tty->packet && tty->link &&
			    tty->link->status_changed)
				return 1;

			select_wait(&tty->secondary.proc_list, wait);
			return 0;
		case SEL_OUT:
			if (!FULL(&tty->write_q))
				return 1;
			select_wait(&tty->write_q.proc_list, wait);
			return 0;
		case SEL_EX:
			if (tty->link && !tty->link->count)
				return 1;
			return 0;
	}
	return 0;
}

/*
 * This implements the "Secure Attention Key" ---  the idea is to
 * prevent trojan horses by killing all processes associated with this
 * tty when the user hits the "Secure Attention Key".  Required for
 * super-paranoid applications --- see the Orange Book for more details.
 * 
 * This code could be nicer; ideally it should send a HUP, wait a few
 * seconds, then send a INT, and then a KILL signal.  But you then
 * have to coordinate with the init process, since all processes associated
 * with the current tty must be dead before the new getty is allowed
 * to spawn.
 */
void do_SAK( struct tty_struct *tty)
{
	struct task_struct **p;
	int line = tty->line;
	int session = tty->session;
	int		i;
	struct file	*filp;
	
	flush_input(tty);
	flush_output(tty);
 	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!(*p))
			continue;
		if (((*p)->tty == line) ||
		    ((session > 0) && ((*p)->session == session)))
			send_sig(SIGKILL, *p, 1);
		else {
			for (i=0; i < NR_FILE; i++) {
				filp = (*p)->filp[i];
				if (filp && (filp->f_op == &tty_fops) &&
				    (MINOR(filp->f_rdev) == line)) {
					send_sig(SIGKILL, *p, 1);
					break;
				}
			}
		}
	}
}

/*
 * This subroutine initializes a tty structure.  We have to set up
 * things correctly for each different type of tty.
 */
static int initialize_tty_struct(struct tty_struct *tty, int line)
{
	struct termios *tp = tty_termios[line];

	memset(tty, 0, sizeof(struct tty_struct));
	tty->line = line;
	tty->pgrp = -1;
	tty->winsize.ws_row = 24;
	tty->winsize.ws_col = 80;
	if (!tty_termios[line]) {
		tp = kmalloc(sizeof(struct termios), GFP_KERNEL);
		if (!tty_termios[line]) {
			if (!tp)
				return -ENOMEM;
			memset(tp, 0, sizeof(struct termios));
			memcpy(tp->c_cc, INIT_C_CC, NCCS);
			if (IS_A_CONSOLE(line)) {
				tp->c_iflag = ICRNL | IXON;
				tp->c_oflag = OPOST | ONLCR;
				tp->c_cflag = B38400 | CS8 | CREAD;
				tp->c_lflag = ISIG | ICANON | ECHO |
					ECHOCTL | ECHOKE;
			} else if (IS_A_SERIAL(line)) {
				tp->c_cflag = B2400 | CS8 | CREAD | HUPCL;
			} else if (IS_A_PTY_MASTER(line)) {
				tp->c_cflag = B9600 | CS8 | CREAD;
			} else if (IS_A_PTY_SLAVE(line)) {
				tp->c_iflag = ICRNL | IXON;
				tp->c_oflag = OPOST | ONLCR;
				tp->c_cflag = B38400 | CS8 | CREAD;
				tp->c_lflag = ISIG | ICANON | ECHO |
					ECHOCTL | ECHOKE;
			}
			tty_termios[line] = tp;
		}
	}
	tty->termios = tty_termios[line];
	
	if (IS_A_CONSOLE(line)) {
		tty->open = con_open;
		tty->winsize.ws_row = video_num_lines;
		tty->winsize.ws_col = video_num_columns;
	} else if IS_A_SERIAL(line) {
		tty->open = rs_open;
	} else if IS_A_PTY(line) {
		tty->open = pty_open;
	}
	return 0;
}

long tty_init(long kmem_start)
{
	int i;

	chrdev_fops[4] = &tty_fops;
	chrdev_fops[5] = &tty_fops;
	for (i=0 ; i<256 ; i++) {
		tty_table[i] =  0;
		tty_termios[i] = 0;
	}
	kmem_start = kbd_init(kmem_start);
	kmem_start = con_init(kmem_start);
	kmem_start = rs_init(kmem_start);
	printk("%d virtual consoles\n\r",NR_CONSOLES);
	return kmem_start;
}

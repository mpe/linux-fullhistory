/*
 *  linux/drivers/char/tty_io.c
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
 *
 * Modified by Fred N. van Kempen, 01/29/93, to add line disciplines
 * which can be dynamically activated and de-activated by the line
 * discipline handling modules (like SLIP).
 *
 * NOTE: pay no attention to the line discipline code (yet); its
 * interface is still subject to change in this version...
 * -- TYT, 1/31/92
 *
 * Added functionality to the OPOST tty handling.  No delays, but all
 * other bits should be there.
 *	-- Nick Holloway <alfie@dcs.warwick.ac.uk>, 27th May 1993.
 *
 * Rewrote canonical mode and added more termios flags.
 * 	-- julian@uhunix.uhcc.hawaii.edu (J. Cowley), 13Jan94
 */

#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/config.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include "kbd_kern.h"
#include "vt_kern.h"
#include "selection.h"

#define CONSOLE_DEV MKDEV(TTY_MAJOR,0)
#define TTY_DEV MKDEV(TTYAUX_MAJOR,0)

#undef TTY_DEBUG_HANGUP

#define TTY_PARANOIA_CHECK
#define CHECK_TTY_COUNT

extern void do_blank_screen(int nopowersave);
extern void do_unblank_screen(void);
extern void set_vesa_blanking(const unsigned long arg);

struct termios tty_std_termios;		/* for the benefit of tty drivers  */
struct tty_driver *tty_drivers = NULL;	/* linked list of tty drivers */
struct tty_ldisc ldiscs[NR_LDISCS];	/* line disc dispatch table	*/

/*
 * fg_console is the current virtual console,
 * last_console is the last used one
 * redirect is the pseudo-tty that console output
 * is redirected to if asked by TIOCCONS.
 */
int fg_console = 0;
int last_console = 0;
struct tty_struct * redirect = NULL;
struct wait_queue * keypress_wait = NULL;

static void initialize_tty_struct(struct tty_struct *tty);

static int tty_read(struct inode *, struct file *, char *, int);
static int tty_write(struct inode *, struct file *, char *, int);
static int tty_select(struct inode *, struct file *, int, select_table *);
static int tty_open(struct inode *, struct file *);
static void tty_release(struct inode *, struct file *);
static int tty_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg);
static int tty_fasync(struct inode * inode, struct file * filp, int on);

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

/*
 * These two routines return the name of tty.  tty_name() should NOT
 * be used in interrupt drivers, since it's not re-entrant.  Use
 * _tty_name() instead.
 */
char *_tty_name(struct tty_struct *tty, char *buf)
{
	if (tty)
		sprintf(buf, "%s%d", tty->driver.name,
			MINOR(tty->device) - tty->driver.minor_start +
			tty->driver.name_base);
	else
		strcpy(buf, "NULL tty");
	return buf;
}

char *tty_name(struct tty_struct *tty)
{
	static char buf[64];

	return(_tty_name(tty, buf));
}

inline int tty_paranoia_check(struct tty_struct *tty, dev_t device,
			      const char *routine)
{
#ifdef TTY_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for tty struct (%d, %d) in %s\n";
	static const char *badtty =
		"Warning: null TTY for (%d, %d) in %s\n";

	if (!tty) {
		printk(badtty, MAJOR(device), MINOR(device), routine);
		return 1;
	}
	if (tty->magic != TTY_MAGIC) {
		printk(badmagic, MAJOR(device), MINOR(device), routine);
		return 1;
	}
#endif
	return 0;
}

static int check_tty_count(struct tty_struct *tty, const char *routine)
{
#ifdef CHECK_TTY_COUNT
	struct file *f;
	int i, count = 0;
	
	for (f = first_file, i=0; i<nr_files; i++, f = f->f_next) {
		if (!f->f_count)
			continue;
		if (f->private_data == tty) {
			count++;
		}
	}
	if (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver.subtype == PTY_TYPE_SLAVE &&
	    tty->link && tty->link->count)
		count++;
	if (tty->count != count) {
		printk("Warning: dev (%d, %d) tty->count(%d) != #fd's(%d) in %s\n",
		       MAJOR(tty->device), MINOR(tty->device), tty->count,
		       count, routine);
		return count;
       }	
#endif
	return 0;
}

int tty_register_ldisc(int disc, struct tty_ldisc *new_ldisc)
{
	if (disc < N_TTY || disc >= NR_LDISCS)
		return -EINVAL;
	
	if (new_ldisc) {
		ldiscs[disc] = *new_ldisc;
		ldiscs[disc].flags |= LDISC_FLAG_DEFINED;
		ldiscs[disc].num = disc;
	} else
		memset(&ldiscs[disc], 0, sizeof(struct tty_ldisc));
	
	return 0;
}

/* Set the discipline of a tty line. */
static int tty_set_ldisc(struct tty_struct *tty, int ldisc)
{
	int	retval = 0;
	struct	tty_ldisc o_ldisc;

	if ((ldisc < N_TTY) || (ldisc >= NR_LDISCS) ||
	    !(ldiscs[ldisc].flags & LDISC_FLAG_DEFINED))
		return -EINVAL;

	if (tty->ldisc.num == ldisc)
		return 0;	/* We are already in the desired discipline */
	o_ldisc = tty->ldisc;

	tty_wait_until_sent(tty, 0);
	
	/* Shutdown the current discipline. */
	if (tty->ldisc.close)
		(tty->ldisc.close)(tty);

	/* Now set up the new line discipline. */
	tty->ldisc = ldiscs[ldisc];
	tty->termios->c_line = ldisc;
	if (tty->ldisc.open)
		retval = (tty->ldisc.open)(tty);
	if (retval < 0) {
		tty->ldisc = o_ldisc;
		tty->termios->c_line = tty->ldisc.num;
		if (tty->ldisc.open && (tty->ldisc.open(tty) < 0)) {
			tty->ldisc = ldiscs[N_TTY];
			tty->termios->c_line = N_TTY;
			if (tty->ldisc.open) {
				int r = tty->ldisc.open(tty);

				if (r < 0)
					panic("Couldn't open N_TTY ldisc for "
					      "%s --- error %d.",
					      tty_name(tty), r);
			}
		}
	}
	if (tty->ldisc.num != o_ldisc.num && tty->driver.set_ldisc)
		tty->driver.set_ldisc(tty);
	return retval;
}

/*
 * This routine returns a tty driver structure, given a device number
 */
struct tty_driver *get_tty_driver(dev_t device)
{
	int	major, minor;
	struct tty_driver *p;
	
	minor = MINOR(device);
	major = MAJOR(device);

	for (p = tty_drivers; p; p = p->next) {
		if (p->major != major)
			continue;
		if (minor < p->minor_start)
			continue;
		if (minor >= p->minor_start + p->num)
			continue;
		return p;
	}
	return NULL;
}

/*
 * If we try to write to, or set the state of, a terminal and we're
 * not in the foreground, send a SIGTTOU.  If the signal is blocked or
 * ignored, go ahead and perform the operation.  (POSIX 7.2)
 */
int tty_check_change(struct tty_struct * tty)
{
	if (current->tty != tty)
		return 0;
	if (tty->pgrp <= 0) {
		printk("tty_check_change: tty->pgrp <= 0!\n");
		return 0;
	}
	if (current->pgrp == tty->pgrp)
		return 0;
	if (is_ignored(SIGTTOU))
		return 0;
	if (is_orphaned_pgrp(current->pgrp))
		return -EIO;
	(void) kill_pg(current->pgrp,SIGTTOU,1);
	return -ERESTARTSYS;
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

static int hung_up_tty_ioctl(struct inode * inode, struct file * file,
			     unsigned int cmd, unsigned long arg)
{
	return -EIO;
}

static int tty_lseek(struct inode * inode, struct file * file, off_t offset, int orig)
{
	return -ESPIPE;
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
	tty_release,
	NULL,		/* tty_fsync */
	tty_fasync
};

static struct file_operations hung_up_tty_fops = {
	tty_lseek,
	hung_up_tty_read,
	hung_up_tty_write,
	NULL,		/* hung_up_tty_readdir */
	hung_up_tty_select,
	hung_up_tty_ioctl,
	NULL,		/* hung_up_tty_mmap */
	NULL,		/* hung_up_tty_open */
	tty_release,	/* hung_up_tty_release */
	NULL,		/* hung_up_tty_fsync  */
	NULL		/* hung_up_tty_fasync */
};

void do_tty_hangup(struct tty_struct * tty, struct file_operations *fops)
{
	int i;
	struct file * filp;
	struct task_struct *p;

	if (!tty)
		return;
	check_tty_count(tty, "do_tty_hangup");
	for (filp = first_file, i=0; i<nr_files; i++, filp = filp->f_next) {
		if (!filp->f_count)
			continue;
		if (filp->private_data != tty)
			continue;
		if (filp->f_inode && filp->f_inode->i_rdev == CONSOLE_DEV)
			continue;
		if (filp->f_op != &tty_fops)
			continue;
		tty_fasync(filp->f_inode, filp, 0);
		filp->f_op = fops;
	}
	
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
	wake_up_interruptible(&tty->read_wait);

	/*
	 * Shutdown the current line discipline, and reset it to
	 * N_TTY.
	 */
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close)(tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open) {
			i = (tty->ldisc.open)(tty);
			if (i < 0)
				printk("do_tty_hangup: N_TTY open: error %d\n",
				       -i);
		}
	}
	
 	for_each_task(p) {
		if ((tty->session > 0) && (p->session == tty->session) &&
		    p->leader) {
			send_sig(SIGHUP,p,1);
			send_sig(SIGCONT,p,1);
			if (tty->pgrp > 0)
				p->tty_old_pgrp = tty->pgrp;
		}
		if (p->tty == tty)
			p->tty = NULL;
	}
	tty->flags = 0;
	tty->session = 0;
	tty->pgrp = -1;
	tty->ctrl_status = 0;
	if (tty->driver.flags & TTY_DRIVER_RESET_TERMIOS)
		*tty->termios = tty->driver.init_termios;
	if (tty->driver.hangup)
		(tty->driver.hangup)(tty);
}

void tty_hangup(struct tty_struct * tty)
{
#ifdef TTY_DEBUG_HANGUP
	printk("%s hangup...\n", tty_name(tty));
#endif
	do_tty_hangup(tty, &hung_up_tty_fops);
}

void tty_vhangup(struct tty_struct * tty)
{
#ifdef TTY_DEBUG_HANGUP
	printk("%s vhangup...\n", tty_name(tty));
#endif
	do_tty_hangup(tty, &hung_up_tty_fops);
}

int tty_hung_up_p(struct file * filp)
{
	return (filp->f_op == &hung_up_tty_fops);
}

/*
 * This function is typically called only by the session leader, when
 * it wants to disassociate itself from its controlling tty.
 *
 * It performs the following functions:
 * 	(1)  Sends a SIGHUP and SIGCONT to the foreground process group
 * 	(2)  Clears the tty from being controlling the session
 * 	(3)  Clears the controlling tty for all processes in the
 * 		session group.
 */
void disassociate_ctty(int priv)
{
	struct tty_struct *tty = current->tty;
	struct task_struct *p;

	if (!tty) {
		if (current->tty_old_pgrp) {
			kill_pg(current->tty_old_pgrp, SIGHUP, priv);
			kill_pg(current->tty_old_pgrp, SIGCONT, priv);
		}
		return;
	}
	if (tty->pgrp > 0) {
		kill_pg(tty->pgrp, SIGHUP, priv);
		kill_pg(tty->pgrp, SIGCONT, priv);
	}

	current->tty_old_pgrp = 0;
	tty->session = 0;
	tty->pgrp = -1;

	for_each_task(p)
	  	if (p->session == current->session)
			p->tty = NULL;
}

/*
 * Sometimes we want to wait until a particular VT has been activated. We
 * do it in a very simple manner. Everybody waits on a single queue and
 * get woken up at once. Those that are satisfied go on with their business,
 * while those not ready go back to sleep. Seems overkill to add a wait
 * to each vt just for this - usually this does nothing!
 */
static struct wait_queue *vt_activate_queue = NULL;

/*
 * Sleeps until a vt is activated, or the task is interrupted. Returns
 * 0 if activation, -1 if interrupted.
 */
int vt_waitactive(void)
{
	interruptible_sleep_on(&vt_activate_queue);
	return (current->signal & ~current->blocked) ? -1 : 0;
}

#define vt_wake_waitactive() wake_up(&vt_activate_queue)

void reset_vc(unsigned int new_console)
{
	vt_cons[new_console]->vc_mode = KD_TEXT;
	kbd_table[new_console].kbdmode = VC_XLATE;
	vt_cons[new_console]->vt_mode.mode = VT_AUTO;
	vt_cons[new_console]->vt_mode.waitv = 0;
	vt_cons[new_console]->vt_mode.relsig = 0;
	vt_cons[new_console]->vt_mode.acqsig = 0;
	vt_cons[new_console]->vt_mode.frsig = 0;
	vt_cons[new_console]->vt_pid = -1;
	vt_cons[new_console]->vt_newvt = -1;
}

/*
 * Performs the back end of a vt switch
 */
void complete_change_console(unsigned int new_console)
{
	unsigned char old_vc_mode;

        if (new_console == fg_console)
                return;
        if (!vc_cons_allocated(new_console))
                return;
	last_console = fg_console;

	/*
	 * If we're switching, we could be going from KD_GRAPHICS to
	 * KD_TEXT mode or vice versa, which means we need to blank or
	 * unblank the screen later.
	 */
	old_vc_mode = vt_cons[fg_console]->vc_mode;
	update_screen(new_console);

	/*
	 * If this new console is under process control, send it a signal
	 * telling it that it has acquired. Also check if it has died and
	 * clean up (similar to logic employed in change_console())
	 */
	if (vt_cons[new_console]->vt_mode.mode == VT_PROCESS)
	{
		/*
		 * Send the signal as privileged - kill_proc() will
		 * tell us if the process has gone or something else
		 * is awry
		 */
		if (kill_proc(vt_cons[new_console]->vt_pid,
			      vt_cons[new_console]->vt_mode.acqsig,
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
		        reset_vc(new_console);
		}
	}

	/*
	 * We do this here because the controlling process above may have
	 * gone, and so there is now a new vc_mode
	 */
	if (old_vc_mode != vt_cons[new_console]->vc_mode)
	{
		if (vt_cons[new_console]->vc_mode == KD_TEXT)
			do_unblank_screen();
		else
			do_blank_screen(1);
	}

	/*
	 * Wake anyone waiting for their VT to activate
	 */
	vt_wake_waitactive();
	return;
}

/*
 * Performs the front-end of a vt switch
 */
void change_console(unsigned int new_console)
{
        if (new_console == fg_console)
                return;
        if (!vc_cons_allocated(new_console))
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
	if (vt_cons[fg_console]->vt_mode.mode == VT_PROCESS)
	{
		/*
		 * Send the signal as privileged - kill_proc() will
		 * tell us if the process has gone or something else
		 * is awry
		 */
		if (kill_proc(vt_cons[fg_console]->vt_pid,
			      vt_cons[fg_console]->vt_mode.relsig,
			      1) == 0)
		{
			/*
			 * It worked. Mark the vt to switch to and
			 * return. The process needs to send us a
			 * VT_RELDISP ioctl to complete the switch.
			 */
			vt_cons[fg_console]->vt_newvt = new_console;
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
		reset_vc(fg_console);

		/*
		 * Fall through to normal (VT_AUTO) handling of the switch...
		 */
	}

	/*
	 * Ignore all switches in KD_GRAPHICS+VT_AUTO mode
	 */
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return;

	complete_change_console(new_console);
}

void wait_for_keypress(void)
{
	sleep_on(&keypress_wait);
}

void stop_tty(struct tty_struct *tty)
{
	if (tty->stopped)
		return;
	tty->stopped = 1;
	if (tty->link && tty->link->packet) {
		tty->ctrl_status &= ~TIOCPKT_START;
		tty->ctrl_status |= TIOCPKT_STOP;
		wake_up_interruptible(&tty->link->read_wait);
	}
	if (tty->driver.stop)
		(tty->driver.stop)(tty);
}

void start_tty(struct tty_struct *tty)
{
	if (!tty->stopped)
		return;
	tty->stopped = 0;
	if (tty->link && tty->link->packet) {
		tty->ctrl_status &= ~TIOCPKT_STOP;
		tty->ctrl_status |= TIOCPKT_START;
		wake_up_interruptible(&tty->link->read_wait);
	}
	if (tty->driver.start)
		(tty->driver.start)(tty);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
}

static int tty_read(struct inode * inode, struct file * file, char * buf, int count)
{
	int i;
	struct tty_struct * tty;

	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_read"))
		return -EIO;
	if (!tty || (tty->flags & (1 << TTY_IO_ERROR)))
		return -EIO;

	/* This check not only needs to be done before reading, but also
	   whenever read_chan() gets woken up after sleeping, so I've
	   moved it to there.  This should only be done for the N_TTY
	   line discipline, anyway.  Same goes for write_chan(). -- jlc. */
#if 0
	if ((inode->i_rdev != CONSOLE_DEV) && /* don't stop on /dev/console */
	    (tty->pgrp > 0) &&
	    (current->tty == tty) &&
	    (tty->pgrp != current->pgrp))
		if (is_ignored(SIGTTIN) || is_orphaned_pgrp(current->pgrp))
			return -EIO;
		else {
			(void) kill_pg(current->pgrp, SIGTTIN, 1);
			return -ERESTARTSYS;
		}
#endif
	if (tty->ldisc.read)
		/* XXX casts are for what kernel-wide prototypes should be. */
		i = (tty->ldisc.read)(tty,file,(unsigned char *)buf,(unsigned int)count);
	else
		i = -EIO;
	if (i > 0)
		inode->i_atime = CURRENT_TIME;
	return i;
}

static int tty_write(struct inode * inode, struct file * file, char * buf, int count)
{
	int i, is_console;
	struct tty_struct * tty;

	is_console = (inode->i_rdev == CONSOLE_DEV);

	if (is_console && redirect)
		tty = redirect;
	else
		tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_write"))
		return -EIO;
	if (!tty || !tty->driver.write || (tty->flags & (1 << TTY_IO_ERROR)))
		return -EIO;
#if 0
	if (!is_console && L_TOSTOP(tty) && (tty->pgrp > 0) &&
	    (current->tty == tty) && (tty->pgrp != current->pgrp)) {
		if (is_orphaned_pgrp(current->pgrp))
			return -EIO;
		if (!is_ignored(SIGTTOU)) {
			(void) kill_pg(current->pgrp, SIGTTOU, 1);
			return -ERESTARTSYS;
		}
	}
#endif
	if (tty->ldisc.write)
		/* XXX casts are for what kernel-wide prototypes should be. */
		i = (tty->ldisc.write)(tty,file,(unsigned char *)buf,(unsigned int)count);
	else
		i = -EIO;
	if (i > 0)
		inode->i_mtime = CURRENT_TIME;
	return i;
}

/*
 * This is so ripe with races that you should *really* not touch this
 * unless you know exactly what you are doing. All the changes have to be
 * made atomically, or there may be incorrect pointers all over the place.
 */
static int init_dev(dev_t device, struct tty_struct **ret_tty)
{
	struct tty_struct *tty, **tty_loc, *o_tty, **o_tty_loc;
	struct termios *tp, **tp_loc, *o_tp, **o_tp_loc;
	struct termios *ltp, **ltp_loc, *o_ltp, **o_ltp_loc;
	struct tty_driver *driver;	
	int retval;
	int idx;

	driver = get_tty_driver(device);
	if (!driver)
		return -ENODEV;

	idx = MINOR(device) - driver->minor_start;
	tty = o_tty = NULL;
	tp = o_tp = NULL;
	ltp = o_ltp = NULL;
	o_tty_loc = NULL;
	o_tp_loc = o_ltp_loc = NULL;

	tty_loc = &driver->table[idx];
	tp_loc = &driver->termios[idx];
	ltp_loc = &driver->termios_locked[idx];

repeat:
	retval = -EAGAIN;
	if (driver->type == TTY_DRIVER_TYPE_PTY &&
	    driver->subtype == PTY_TYPE_MASTER &&
	    *tty_loc && (*tty_loc)->count)
		goto end_init;
	retval = -ENOMEM;
	if (!*tty_loc && !tty) {
		if (!(tty = (struct tty_struct*) get_free_page(GFP_KERNEL)))
			goto end_init;
		initialize_tty_struct(tty);
		tty->device = device;
		tty->driver = *driver;
		goto repeat;
	}
	if (!*tp_loc && !tp) {
		tp = (struct termios *) kmalloc(sizeof(struct termios),
						GFP_KERNEL);
		if (!tp)
			goto end_init;
		*tp = driver->init_termios;
		goto repeat;
	}
	if (!*ltp_loc && !ltp) {
		ltp = (struct termios *) kmalloc(sizeof(struct termios),
						 GFP_KERNEL);
		if (!ltp)
			goto end_init;
		memset(ltp, 0, sizeof(struct termios));
		goto repeat;
	}
	if (driver->type == TTY_DRIVER_TYPE_PTY) {
		o_tty_loc = &driver->other->table[idx];
		o_tp_loc = &driver->other->termios[idx];
		o_ltp_loc = &driver->other->termios_locked[idx];

		if (!*o_tty_loc && !o_tty) {
			dev_t 	o_device;
			
			o_tty = (struct tty_struct *)
				get_free_page(GFP_KERNEL);
			if (!o_tty)
				goto end_init;
			o_device = MKDEV(driver->other->major,
					 driver->other->minor_start + idx);
			initialize_tty_struct(o_tty);
			o_tty->device = o_device;
			o_tty->driver = *driver->other;
			goto repeat;
		}
		if (!*o_tp_loc && !o_tp) {
			o_tp = (struct termios *)
				kmalloc(sizeof(struct termios), GFP_KERNEL);
			if (!o_tp)
				goto end_init;
			*o_tp = driver->other->init_termios;
			goto repeat;
		}
		if (!*o_ltp_loc && !o_ltp) {
			o_ltp = (struct termios *)
				kmalloc(sizeof(struct termios), GFP_KERNEL);
			if (!o_ltp)
				goto end_init;
			memset(o_ltp, 0, sizeof(struct termios));
			goto repeat;
		}
		
	}
	/* Now we have allocated all the structures: update all the pointers.. */
	if (!*tp_loc) {
		*tp_loc = tp;
		tp = NULL;
	}
	if (!*ltp_loc) {
		*ltp_loc = ltp;
		ltp = NULL;
	}
	if (!*tty_loc) {
		tty->termios = *tp_loc;
		tty->termios_locked = *ltp_loc;
		*tty_loc = tty;
		(*driver->refcount)++;
		(*tty_loc)->count++;
		if (tty->ldisc.open) {
			retval = (tty->ldisc.open)(tty);
			if (retval < 0) {
				(*tty_loc)->count--;
				tty = NULL;
				goto end_init;
			}
		}
		tty = NULL;
	} else {
		if ((*tty_loc)->flags & (1 << TTY_CLOSING)) {
			printk("Attempt to open closing tty %s.\n",
			       tty_name(*tty_loc));
			printk("Ack!!!!  This should never happen!!\n");
			return -EINVAL;
		}
		(*tty_loc)->count++;
	}
	if (driver->type == TTY_DRIVER_TYPE_PTY) {
		if (!*o_tp_loc) {
			*o_tp_loc = o_tp;
			o_tp = NULL;
		}
		if (!*o_ltp_loc) {
			*o_ltp_loc = o_ltp;
			o_ltp = NULL;
		}
		if (!*o_tty_loc) {
			o_tty->termios = *o_tp_loc;
			o_tty->termios_locked = *o_ltp_loc;
			*o_tty_loc = o_tty;
			(*driver->other->refcount)++;
			if (o_tty->ldisc.open) {
				retval = (o_tty->ldisc.open)(o_tty);
				if (retval < 0) {
					(*tty_loc)->count--;
					o_tty = NULL;
					goto end_init;
				}
			}
			o_tty = NULL;
		}
		(*tty_loc)->link = *o_tty_loc;
		(*o_tty_loc)->link = *tty_loc;
		if (driver->subtype == PTY_TYPE_MASTER)
			(*o_tty_loc)->count++;
	}
	(*tty_loc)->driver = *driver;
	*ret_tty = *tty_loc;
	retval = 0;
end_init:
	if (tty)
		free_page((unsigned long) tty);
	if (o_tty)
		free_page((unsigned long) o_tty);
	if (tp)
		kfree_s(tp, sizeof(struct termios));
	if (o_tp)
		kfree_s(o_tp, sizeof(struct termios));
	if (ltp)
		kfree_s(ltp, sizeof(struct termios));
	if (o_ltp)
		kfree_s(o_ltp, sizeof(struct termios));
	return retval;
}

/*
 * Even releasing the tty structures is a tricky business.. We have
 * to be very careful that the structures are all released at the
 * same time, as interrupts might otherwise get the wrong pointers.
 */
static void release_dev(struct file * filp)
{
	struct tty_struct *tty, *o_tty;
	struct termios *tp, *o_tp, *ltp, *o_ltp;
	struct task_struct **p;
	int	idx;
	
	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_inode->i_rdev, "release_dev"))
		return;

	check_tty_count(tty, "release_dev");

	tty_fasync(filp->f_inode, filp, 0);

	tp = tty->termios;
	ltp = tty->termios_locked;

	idx = MINOR(tty->device) - tty->driver.minor_start;
#ifdef TTY_PARANOIA_CHECK
	if (idx < 0 || idx >= tty->driver.num) {
		printk("release_dev: bad idx when trying to free (%d, %d)\n",
		       MAJOR(tty->device), MINOR(tty->device));
		return;
	}
	if (tty != tty->driver.table[idx]) {
		printk("release_dev: driver.table[%d] not tty for (%d, %d)\n",
		       idx, MAJOR(tty->device), MINOR(tty->device));
		return;
	}
	if (tp != tty->driver.termios[idx]) {
		printk("release_dev: driver.termios[%d] not termios for (%d, %d)\n",
		       idx, MAJOR(tty->device), MINOR(tty->device));
		return;
	}
	if (ltp != tty->driver.termios_locked[idx]) {
		printk("release_dev: driver.termios_locked[%d] not termios_locked for (%d, %d)\n",
		       idx, MAJOR(tty->device), MINOR(tty->device));
		return;
	}
#endif

#ifdef TTY_DEBUG_HANGUP
	printk("release_dev of %s (tty count=%d)...", tty_name(tty),
	       tty->count);
#endif

	o_tty = tty->link;
	o_tp = (o_tty) ? o_tty->termios : NULL;
	o_ltp = (o_tty) ? o_tty->termios_locked : NULL;

#ifdef TTY_PARANOIA_CHECK
	if (tty->driver.other) {
		if (o_tty != tty->driver.other->table[idx]) {
			printk("release_dev: other->table[%d] not o_tty for (%d, %d)\n",
			       idx, MAJOR(tty->device), MINOR(tty->device));
			return;
		}
		if (o_tp != tty->driver.other->termios[idx]) {
			printk("release_dev: other->termios[%d] not o_termios for (%d, %d)\n",
			       idx, MAJOR(tty->device), MINOR(tty->device));
			return;
		}
		if (o_ltp != tty->driver.other->termios_locked[idx]) {
			printk("release_dev: other->termios_locked[%d] not o_termios_locked for (%d, %d)\n",
			       idx, MAJOR(tty->device), MINOR(tty->device));
			return;
		}

		if (o_tty->link != tty) {
			printk("release_dev: bad pty pointers\n");
			return;
		}
	}
#endif
	
	if (tty->driver.close)
		tty->driver.close(tty, filp);
	if (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver.subtype == PTY_TYPE_MASTER) {
		if (--tty->link->count < 0) {
			printk("release_dev: bad pty slave count (%d) for %s\n",
			       tty->count, tty_name(tty));
			tty->link->count = 0;
		}
	}
	if (--tty->count < 0) {
		printk("release_dev: bad tty->count (%d) for %s\n",
		       tty->count, tty_name(tty));
		tty->count = 0;
	}
	if (tty->count)
		return;

	/*
	 * We're committed; at this point, we must not block!
	 */
	if (o_tty) {
		if (o_tty->count)
			return;
		tty->driver.other->table[idx] = NULL;
		tty->driver.other->termios[idx] = NULL;
		kfree_s(o_tp, sizeof(struct termios));
	}
	
#ifdef TTY_DEBUG_HANGUP
	printk("freeing tty structure...");
#endif
	tty->flags |= (1 << TTY_CLOSING);

	/*
	 * Make sure there aren't any processes that still think this
	 * tty is their controlling tty.
	 */
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (*p == 0)
			continue;
		if ((*p)->tty == tty)
			(*p)->tty = NULL;
		if (o_tty && (*p)->tty == o_tty)
			(*p)->tty = NULL;
	}

	/*
	 * Shutdown the current line discipline, and reset it to
	 * N_TTY.
	 */
	if (tty->ldisc.close)
		(tty->ldisc.close)(tty);
	tty->ldisc = ldiscs[N_TTY];
	tty->termios->c_line = N_TTY;
	if (o_tty) {
		if (o_tty->ldisc.close)
			(o_tty->ldisc.close)(o_tty);
		o_tty->ldisc = ldiscs[N_TTY];
	}
	
	tty->driver.table[idx] = NULL;
	if (tty->driver.flags & TTY_DRIVER_RESET_TERMIOS) {
		tty->driver.termios[idx] = NULL;
		kfree_s(tp, sizeof(struct termios));
	}
	if (tty == redirect || o_tty == redirect)
		redirect = NULL;
	/*
	 * Make sure that the tty's task queue isn't activated.  If it
	 * is, take it out of the linked list.
	 */
	cli();
	if (tty->flip.tqueue.sync) {
		struct tq_struct *tq, *prev;

		for (tq=tq_timer, prev=0; tq; prev=tq, tq=tq->next) {
			if (tq == &tty->flip.tqueue) {
				if (prev)
					prev->next = tq->next;
				else
					tq_timer = tq->next;
				break;
			}
		}
	}
	sti();
	tty->magic = 0;
	(*tty->driver.refcount)--;
	free_page((unsigned long) tty);
	filp->private_data = 0;
	if (o_tty) {
		o_tty->magic = 0;
		(*o_tty->driver.refcount)--;
		free_page((unsigned long) o_tty);
	}
}

/*
 * tty_open and tty_release keep up the tty count that contains the
 * number of opens done on a tty. We cannot use the inode-count, as
 * different inodes might point to the same tty.
 *
 * Open-counting is needed for pty masters, as well as for keeping
 * track of serial lines: DTR is dropped when the last close happens.
 * (This is not done solely through tty->count, now.  - Ted 1/27/92)
 *
 * The termios state of a pty is reset on first open so that
 * settings don't persist across reuse.
 */
static int tty_open(struct inode * inode, struct file * filp)
{
	struct tty_struct *tty;
	int minor;
	int noctty, retval;
	dev_t device;

retry_open:
	noctty = filp->f_flags & O_NOCTTY;
	device = inode->i_rdev;
	if (device == TTY_DEV) {
		if (!current->tty)
			return -ENXIO;
		device = current->tty->device;
		/* noctty = 1; */
	}
	if (device == CONSOLE_DEV) {
		device = MKDEV(TTY_MAJOR, fg_console+1);
		noctty = 1;
	}
	minor = MINOR(device);
	
	retval = init_dev(device, &tty);
	if (retval)
		return retval;
	filp->private_data = tty;
	check_tty_count(tty, "tty_open");
	if (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver.subtype == PTY_TYPE_MASTER)
		noctty = 1;
#ifdef TTY_DEBUG_HANGUP
	printk("opening %s...", tty_name(tty));
#endif
	if (tty->driver.open)
		retval = tty->driver.open(tty, filp);
	else
		retval = -ENODEV;

	if (!retval && test_bit(TTY_EXCLUSIVE, &tty->flags) && !suser())
		retval = -EBUSY;

	if (retval) {
#ifdef TTY_DEBUG_HANGUP
		printk("error %d in opening %s...", retval, tty_name(tty));
#endif

		release_dev(filp);
		if (retval != -ERESTARTSYS)
			return retval;
		if (current->signal & ~current->blocked)
			return retval;
		schedule();
		/*
		 * Need to reset f_op in case a hangup happened.
		 */
		filp->f_op = &tty_fops;
		goto retry_open;
	}
	if (!noctty &&
	    current->leader &&
	    !current->tty &&
	    tty->session == 0) {
		current->tty = tty;
		current->tty_old_pgrp = 0;
		tty->session = current->session;
		tty->pgrp = current->pgrp;
	}
	return 0;
}

/*
 * Note that releasing a pty master also releases the child, so
 * we have to make the redirection checks after that and on both
 * sides of a pty.
 */
static void tty_release(struct inode * inode, struct file * filp)
{
	release_dev(filp);
}

static int tty_select(struct inode * inode, struct file * filp, int sel_type, select_table * wait)
{
	struct tty_struct * tty;

	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_select"))
		return 0;

	if (tty->ldisc.select)
		return (tty->ldisc.select)(tty, inode, filp, sel_type, wait);
	return 0;
}

static int tty_fasync(struct inode * inode, struct file * filp, int on)
{
	struct tty_struct * tty;
	struct fasync_struct *fa, *prev;

	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_fasync"))
		return 0;

	for (fa = tty->fasync, prev = 0; fa; prev= fa, fa = fa->fa_next) {
		if (fa->fa_file == filp)
			break;
	}

	if (on) {
		if (fa)
			return 0;
		fa = (struct fasync_struct *)kmalloc(sizeof(struct fasync_struct), GFP_KERNEL);
		if (!fa)
			return -ENOMEM;
		fa->magic = FASYNC_MAGIC;
		fa->fa_file = filp;
		fa->fa_next = tty->fasync;
		tty->fasync = fa;
		if (!tty->read_wait)
			tty->minimum_to_wake = 1;
		if (filp->f_owner == 0) {
			if (tty->pgrp)
				filp->f_owner = -tty->pgrp;
			else
				filp->f_owner = current->pid;
		}
	} else {
		if (!fa)
			return 0;
		if (prev)
			prev->fa_next = fa->fa_next;
		else
			tty->fasync = fa->fa_next;
		kfree_s(fa, sizeof(struct fasync_struct));
		if (!tty->fasync && !tty->read_wait)
			tty->minimum_to_wake = N_TTY_BUF_SIZE;
	}
	return 0;	
}

#if 0
/*
 * XXX does anyone use this anymore?!?
 */
static int do_get_ps_info(unsigned long arg)
{
	struct tstruct {
		int flag;
		int present[NR_TASKS];
		struct task_struct tasks[NR_TASKS];
	};
	struct tstruct *ts = (struct tstruct *)arg;
	struct task_struct **p;
	char *c, *d;
	int i, n = 0;
	
	i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct tstruct));
	if (i)
		return i;
	for (p = &FIRST_TASK ; p <= &LAST_TASK ; p++, n++)
		if (*p)
		{
			c = (char *)(*p);
			d = (char *)(ts->tasks+n);
			for (i=0 ; i<sizeof(struct task_struct) ; i++)
				put_fs_byte(*c++, d++);
			put_fs_long(1, (unsigned long *)(ts->present+n));
		}
		else	
			put_fs_long(0, (unsigned long *)(ts->present+n));
	return(0);			
}
#endif

static int tty_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg)
{
	int	retval;
	struct tty_struct * tty;
	struct tty_struct * real_tty;
	struct winsize tmp_ws;
	pid_t pgrp;
	unsigned char	ch;
	char	mbz = 0;
	
	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_ioctl"))
		return -EINVAL;

	if (tty->driver.type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver.subtype == PTY_TYPE_MASTER)
		real_tty = tty->link;
	else
		real_tty = tty;

	switch (cmd) {
		case TIOCSTI:
			if ((current->tty != tty) && !suser())
				return -EPERM;
			retval = verify_area(VERIFY_READ, (void *) arg, 1);
			if (retval)
				return retval;
			ch = get_fs_byte((char *) arg);
			tty->ldisc.receive_buf(tty, &ch, &mbz, 1);
			return 0;
		case TIOCGWINSZ:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
					     sizeof (struct winsize));
			if (retval)
				return retval;
			memcpy_tofs((struct winsize *) arg, &tty->winsize,
				    sizeof (struct winsize));
			return 0;
		case TIOCSWINSZ:
			retval = verify_area(VERIFY_READ, (void *) arg,
					     sizeof (struct winsize));
			if (retval)
				return retval;			
			memcpy_fromfs(&tmp_ws, (struct winsize *) arg,
				      sizeof (struct winsize));
			if (memcmp(&tmp_ws, &tty->winsize,
				   sizeof(struct winsize))) {
				if (tty->pgrp > 0)
					kill_pg(tty->pgrp, SIGWINCH, 1);
				if ((real_tty->pgrp != tty->pgrp) &&
				    (real_tty->pgrp > 0))
					kill_pg(real_tty->pgrp, SIGWINCH, 1);
			}
			tty->winsize = tmp_ws;
			real_tty->winsize = tmp_ws;
			return 0;
		case TIOCCONS:
			if (tty->driver.type == TTY_DRIVER_TYPE_CONSOLE) {
				if (!suser())
					return -EPERM;
				redirect = NULL;
				return 0;
			}
			if (redirect)
				return -EBUSY;
			redirect = real_tty;
			return 0;
		case FIONBIO:
			retval = verify_area(VERIFY_READ, (void *) arg, sizeof(long));
			if (retval)
				return retval;
			arg = get_fs_long((unsigned long *) arg);
			if (arg)
				file->f_flags |= O_NONBLOCK;
			else
				file->f_flags &= ~O_NONBLOCK;
			return 0;
		case TIOCEXCL:
			set_bit(TTY_EXCLUSIVE, &tty->flags);
			return 0;
		case TIOCNXCL:
			clear_bit(TTY_EXCLUSIVE, &tty->flags);
			return 0;
		case TIOCNOTTY:
			if (current->tty != tty)
				return -ENOTTY;
			if (current->leader)
				disassociate_ctty(0);
			current->tty = NULL;
			return 0;
		case TIOCSCTTY:
			if (current->leader &&
			    (current->session == tty->session))
				return 0;
			/*
			 * The process must be a session leader and
			 * not have a controlling tty already.
			 */
			if (!current->leader || current->tty)
				return -EPERM;
			if (tty->session > 0) {
				/*
				 * This tty is already the controlling
				 * tty for another session group!
				 */
				if ((arg == 1) && suser()) {
					/*
					 * Steal it away
					 */
					struct task_struct *p;

					for_each_task(p)
						if (p->tty == tty)
							p->tty = NULL;
				} else
					return -EPERM;
			}
			current->tty = tty;
			current->tty_old_pgrp = 0;
			tty->session = current->session;
			tty->pgrp = current->pgrp;
			return 0;
		case TIOCGPGRP:
			/*
			 * (tty == real_tty) is a cheap way of
			 * testing if the tty is NOT a master pty.
			 */
			if (tty == real_tty && current->tty != real_tty)
				return -ENOTTY;
			retval = verify_area(VERIFY_WRITE, (void *) arg,
					     sizeof (pid_t));
			if (retval)
				return retval;
			put_fs_long(real_tty->pgrp, (pid_t *) arg);
			return 0;
		case TIOCSPGRP:
			retval = tty_check_change(real_tty);
			if (retval)
				return retval;
			if (!current->tty ||
			    (current->tty != real_tty) ||
			    (real_tty->session != current->session))
				return -ENOTTY;
			pgrp = get_fs_long((pid_t *) arg);
			if (pgrp < 0)
				return -EINVAL;
			if (session_of_pgrp(pgrp) != current->session)
				return -EPERM;
			real_tty->pgrp = pgrp;
			return 0;
		case TIOCGETD:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
					     sizeof (unsigned long));
			if (retval)
				return retval;
			put_fs_long(tty->ldisc.num, (unsigned long *) arg);
			return 0;
		case TIOCSETD:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			arg = get_fs_long((unsigned long *) arg);
			return tty_set_ldisc(tty, arg);
		case TIOCLINUX:
			if (tty->driver.type != TTY_DRIVER_TYPE_CONSOLE)
				return -EINVAL;
			if (current->tty != tty && !suser())
				return -EPERM;
			retval = verify_area(VERIFY_READ, (void *) arg, 1);
			if (retval)
				return retval;
			switch (retval = get_fs_byte((char *)arg))
			{
				case 0:
				case 8:
				case 9:
					printk("TIOCLINUX (0/8/9) ioctl is gone - use /dev/vcs\n");
					return -EINVAL;
#if 0
				case 1:
					printk("Deprecated TIOCLINUX (1) ioctl\n");
					return do_get_ps_info(arg);
#endif
				case 2:
					return set_selection(arg, tty);
				case 3:
					return paste_selection(tty);
				case 4:
					do_unblank_screen();
					return 0;
				case 5:
					return sel_loadlut(arg);
				case 6:
			/*
			 * Make it possible to react to Shift+Mousebutton.
			 * Note that 'shift_state' is an undocumented
			 * kernel-internal variable; programs not closely
			 * related to the kernel should not use this.
			 */
					put_fs_byte(shift_state,arg);
					return 0;
				case 7:
					put_fs_byte(mouse_reporting(),arg);
					return 0;
				case 10:
					set_vesa_blanking(arg);
					return 0;
				default: 
					return -EINVAL;
			}

		case TIOCTTYGSTRUCT:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
						sizeof(struct tty_struct));
			if (retval)
				return retval;
			memcpy_tofs((struct tty_struct *) arg,
				    tty, sizeof(struct tty_struct));
			return 0;
		default:
			if (tty->driver.ioctl) {
				retval = (tty->driver.ioctl)(tty, file,
							     cmd, arg);
				if (retval != -ENOIOCTLCMD)
					return retval;
			}
			if (tty->ldisc.ioctl) {
				retval = (tty->ldisc.ioctl)(tty, file,
							    cmd, arg);
				if (retval != -ENOIOCTLCMD)
					return retval;
			}
			return -EINVAL;
		}
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
#ifdef TTY_SOFT_SAK
	tty_hangup(tty);
#else
	struct task_struct **p;
	int session;
	int		i;
	struct file	*filp;
	
	if (!tty)
		return;
	session  = tty->session;
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
 	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!(*p))
			continue;
		if (((*p)->tty == tty) ||
		    ((session > 0) && ((*p)->session == session)))
			send_sig(SIGKILL, *p, 1);
		else {
			for (i=0; i < NR_OPEN; i++) {
				filp = (*p)->files->fd[i];
				if (filp && (filp->f_op == &tty_fops) &&
				    (filp->private_data == tty)) {
					send_sig(SIGKILL, *p, 1);
					break;
				}
			}
		}
	}
#endif
}

/*
 * This routine is called out of the software interrupt to flush data
 * from the flip buffer to the line discipline.
 */
static void flush_to_ldisc(void *private_)
{
	struct tty_struct *tty = (struct tty_struct *) private_;
	unsigned char	*cp;
	char		*fp;
	int		count;

	if (tty->flip.buf_num) {
		cp = tty->flip.char_buf + TTY_FLIPBUF_SIZE;
		fp = tty->flip.flag_buf + TTY_FLIPBUF_SIZE;
		tty->flip.buf_num = 0;

		cli();
		tty->flip.char_buf_ptr = tty->flip.char_buf;
		tty->flip.flag_buf_ptr = tty->flip.flag_buf;
	} else {
		cp = tty->flip.char_buf;
		fp = tty->flip.flag_buf;
		tty->flip.buf_num = 1;

		cli();
		tty->flip.char_buf_ptr = tty->flip.char_buf + TTY_FLIPBUF_SIZE;
		tty->flip.flag_buf_ptr = tty->flip.flag_buf + TTY_FLIPBUF_SIZE;
	}
	count = tty->flip.count;
	tty->flip.count = 0;
	sti();
	
#if 0
	if (count > tty->max_flip_cnt)
		tty->max_flip_cnt = count;
#endif
	tty->ldisc.receive_buf(tty, cp, fp, count);
}

/*
 * This subroutine initializes a tty structure.
 */
static void initialize_tty_struct(struct tty_struct *tty)
{
	memset(tty, 0, sizeof(struct tty_struct));
	tty->magic = TTY_MAGIC;
	tty->ldisc = ldiscs[N_TTY];
	tty->pgrp = -1;
	tty->flip.char_buf_ptr = tty->flip.char_buf;
	tty->flip.flag_buf_ptr = tty->flip.flag_buf;
	tty->flip.tqueue.routine = flush_to_ldisc;
	tty->flip.tqueue.data = tty;
}

/*
 * The default put_char routine if the driver did not define one.
 */
void tty_default_put_char(struct tty_struct *tty, unsigned char ch)
{
	tty->driver.write(tty, 0, &ch, 1);
}

/*
 * Called by a tty driver to register itself.
 */
int tty_register_driver(struct tty_driver *driver)
{
	int error;

	if (driver->flags & TTY_DRIVER_INSTALLED)
		return 0;

	error = register_chrdev(driver->major, driver->name, &tty_fops);
	if (error < 0)
		return error;
	else if(driver->major == 0)
		driver->major = error;

	if (!driver->put_char)
		driver->put_char = tty_default_put_char;
	
	driver->prev = 0;
	driver->next = tty_drivers;
	if (tty_drivers) tty_drivers->prev = driver;
	tty_drivers = driver;
	return error;
}

/*
 * Called by a tty driver to unregister itself.
 */
int tty_unregister_driver(struct tty_driver *driver)
{
	int	retval;
	struct tty_driver *p;
	int	found = 0;
	char *othername = NULL;
	
	if (*driver->refcount)
		return -EBUSY;

	for (p = tty_drivers; p; p = p->next) {
		if (p == driver)
			found++;
		else if (p->major == driver->major)
			othername = p->name;
	}

	if (othername == NULL) {
		retval = unregister_chrdev(driver->major, driver->name);
		if (retval)
			return retval;
	} else
		register_chrdev(driver->major, othername, &tty_fops);

	if (driver->prev)
		driver->prev->next = driver->next;
	else
		tty_drivers = driver->next;
	
	if (driver->next)
		driver->next->prev = driver->prev;

	return 0;
}


/*
 * Initialize the console device. This is called *early*, so
 * we can't necessarily depend on lots of kernel help here.
 * Just do some early initializations, and do the complex setup
 * later.
 */
long console_init(long kmem_start, long kmem_end)
{
	/* Setup the default TTY line discipline. */
	memset(ldiscs, 0, sizeof(ldiscs));
	(void) tty_register_ldisc(N_TTY, &tty_ldisc_N_TTY);

	/*
	 * Set up the standard termios.  Individual tty drivers may 
	 * deviate from this; this is used as a template.
	 */
	memset(&tty_std_termios, 0, sizeof(struct termios));
	memcpy(tty_std_termios.c_cc, INIT_C_CC, NCCS);
	tty_std_termios.c_iflag = ICRNL | IXON;
	tty_std_termios.c_oflag = OPOST | ONLCR;
	tty_std_termios.c_cflag = B38400 | CS8 | CREAD;
	tty_std_termios.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK |
		ECHOCTL | ECHOKE | IEXTEN;

	/*
	 * set up the console device so that later boot sequences can 
	 * inform about problems etc..
	 */
	return con_init(kmem_start);
}

/*
 * Ok, now we can initialize the rest of the tty devices and can count
 * on memory allocations, interrupts etc..
 */
long tty_init(long kmem_start)
{
	if (sizeof(struct tty_struct) > PAGE_SIZE)
		panic("size of tty structure > PAGE_SIZE!");
	if (register_chrdev(TTY_MAJOR,"tty",&tty_fops))
		panic("unable to get major %d for tty device", TTY_MAJOR);
	if (register_chrdev(TTYAUX_MAJOR,"cua",&tty_fops))
		panic("unable to get major %d for tty device", TTYAUX_MAJOR);

	kmem_start = kbd_init(kmem_start);
	kmem_start = rs_init(kmem_start);
#ifdef CONFIG_CYCLADES
	kmem_start = cy_init(kmem_start);
#endif
	kmem_start = pty_init(kmem_start);
	kmem_start = vcs_init(kmem_start);
	return kmem_start;
}

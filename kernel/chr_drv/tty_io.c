/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl, who also corrected VMIN = VTIME = 0.
 */

#include <linux/ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define ALRMMASK (1<<(SIGALRM-1))

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

#include <sys/kd.h>
#include "vt_kern.h"

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define QUEUES	(3*(MAX_CONSOLES+NR_SERIALS+2*NR_PTYS))
static struct tty_queue tty_queues[QUEUES];
struct tty_struct tty_table[256];

#define con_queues tty_queues
#define rs_queues ((3*MAX_CONSOLES) + tty_queues)
#define mpty_queues ((3*(MAX_CONSOLES+NR_SERIALS)) + tty_queues)
#define spty_queues ((3*(MAX_CONSOLES+NR_SERIALS+NR_PTYS)) + tty_queues)

#define con_table tty_table
#define rs_table (64+tty_table)
#define mpty_table (128+tty_table)
#define spty_table (192+tty_table)

/*
 * fg_console is the current virtual console,
 * redirect is the pseudo-tty that console output
 * is redirected to if asked by TIOCCONS.
 */
int fg_console = 0;
struct tty_struct * redirect = NULL;

/*
 * these are the tables used by the machine code handlers.
 * you can implement virtual consoles.
 */
struct tty_queue * table_list[]={
	con_queues + 0, con_queues + 1,
	rs_queues + 0, rs_queues + 1,
	rs_queues + 3, rs_queues + 4,
	rs_queues + 6, rs_queues + 7,
	rs_queues + 9, rs_queues + 10
};

void change_console(unsigned int new_console)
{
	if (vt_cons[fg_console].vt_mode == KD_GRAPHICS)
		return;
	if (new_console == fg_console || new_console >= NR_CONSOLES)
		return;
	table_list[0] = con_queues + 0 + new_console*3;
	table_list[1] = con_queues + 1 + new_console*3;
	update_screen(new_console);
}

static void sleep_if_empty(struct tty_queue * queue)
{
	cli();
	while (!(current->signal & ~current->blocked) && EMPTY(queue))
		interruptible_sleep_on(&queue->proc_list);
	sti();
}

void wait_for_keypress(void)
{
	sleep_if_empty(tty_table[fg_console].secondary);
}

void copy_to_cooked(struct tty_struct * tty)
{
	int c;

	if (!(tty && tty->write && tty->read_q &&
	    tty->write_q && tty->secondary)) {
		printk("copy_to_cooked: missing queues\n\r");
		return;
	}
	while (1) {
		if (EMPTY(tty->read_q))
			break;
		if (FULL(tty->secondary)) {
			if (tty->secondary->proc_list)
				if (tty->secondary->proc_list != current)
					current->counter = 0;
			break;
		}
		c = GETCH(tty->read_q);
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
			if ((KILL_CHAR(tty) != _POSIX_VDISABLE) &&
			    (c==KILL_CHAR(tty))) {
				/* deal with killing the input line */
				while(!(EMPTY(tty->secondary) ||
					(c=LAST(tty->secondary))==10 ||
					((EOF_CHAR(tty) != _POSIX_VDISABLE) &&
					 (c==EOF_CHAR(tty))))) {
					if (L_ECHO(tty)) {
						if (c<32) {
							PUTCH(8,tty->write_q);
							PUTCH(' ',tty->write_q);
							PUTCH(8,tty->write_q);
						}
						PUTCH(8,tty->write_q);
						PUTCH(' ',tty->write_q);
						PUTCH(8,tty->write_q);
						TTY_WRITE_FLUSH(tty);
					}
					DEC(tty->secondary->head);
				}
				continue;
			}
			if ((ERASE_CHAR(tty) != _POSIX_VDISABLE) &&
			    (c==ERASE_CHAR(tty))) {
				if (EMPTY(tty->secondary) ||
				   (c=LAST(tty->secondary))==10 ||
				   ((EOF_CHAR(tty) != _POSIX_VDISABLE) &&
				    (c==EOF_CHAR(tty))))
					continue;
				if (L_ECHO(tty)) {
					if (c<32) {
						PUTCH(8,tty->write_q);
						PUTCH(' ',tty->write_q);
						PUTCH(8,tty->write_q);
					}
					PUTCH(8,tty->write_q);
					PUTCH(32,tty->write_q);
					PUTCH(8,tty->write_q);
					TTY_WRITE_FLUSH(tty);
				}
				DEC(tty->secondary->head);
				continue;
			}
		}
		if (I_IXON(tty)) {
			if ((STOP_CHAR(tty) != _POSIX_VDISABLE) &&
			    (c==STOP_CHAR(tty))) {
				tty->stopped=1;
				continue;
			}
			if ((START_CHAR(tty) != _POSIX_VDISABLE) &&
			    (c==START_CHAR(tty))) {
				tty->stopped=0;
				TTY_WRITE_FLUSH(tty);
				continue;
			}
		}
		if (L_ISIG(tty)) {
			if ((INTR_CHAR(tty) != _POSIX_VDISABLE) &&
			    (c==INTR_CHAR(tty))) {
				kill_pg(tty->pgrp, SIGINT, 1);
				continue;
			}
			if ((QUIT_CHAR(tty) != _POSIX_VDISABLE) &&
			    (c==QUIT_CHAR(tty))) {
				kill_pg(tty->pgrp, SIGQUIT, 1);
				continue;
			}
			if ((SUSPEND_CHAR(tty) != _POSIX_VDISABLE) &&
			    (c==SUSPEND_CHAR(tty))) {
				if (!is_orphaned_pgrp(tty->pgrp))
					kill_pg(tty->pgrp, SIGTSTP, 1);
				continue;
			}
		}
		if (c==10 || (EOF_CHAR(tty) != _POSIX_VDISABLE &&
		    c==EOF_CHAR(tty)))
			tty->secondary->data++;
		if ((L_ECHO(tty) || L_ECHONL(tty)) && (c==10)) {
			PUTCH(10,tty->write_q);
			PUTCH(13,tty->write_q);
		} else if (L_ECHO(tty)) {
			if (c<32 && L_ECHOCTL(tty)) {
				PUTCH('^',tty->write_q);
				PUTCH(c+64,tty->write_q);
			} else
				PUTCH(c,tty->write_q);
		}
		PUTCH(c,tty->secondary);
		TTY_WRITE_FLUSH(tty);
	}
	TTY_WRITE_FLUSH(tty);
	if (!EMPTY(tty->secondary))
		wake_up(&tty->secondary->proc_list);
	if (LEFT(tty->write_q) > TTY_BUF_SIZE/2)
		wake_up(&tty->write_q->proc_list);
}

int is_ignored(int sig)
{
	return ((current->blocked & (1<<(sig-1))) ||
	        (current->sigaction[sig-1].sa_handler == SIG_IGN));
}

/*
 * Called when we need to send a SIGTTIN or SIGTTOU to our process
 * group
 * 
 * We only request that a system call be restarted if there was if the 
 * default signal handler is being used.  The reason for this is that if
 * a job is catching SIGTTIN or SIGTTOU, the signal handler may not want 
 * the system call to be restarted blindly.  If there is no way to reset the
 * terminal pgrp back to the current pgrp (perhaps because the controlling
 * tty has been released on logout), we don't want to be in an infinite loop
 * while restarting the system call, and have it always generate a SIGTTIN
 * or SIGTTOU.  The default signal handler will cause the process to stop
 * thus avoiding the infinite loop problem.  Presumably the job-control
 * cognizant parent will fix things up before continuging its child process.
 */
int tty_signal(int sig, struct tty_struct *tty)
{
	(void) kill_pg(current->pgrp,sig,1);
	if (current->sigaction[sig-1].sa_handler)
		return -EINTR;		/* We _will_ be interrupted :-) */
	else
		return -ERESTARTSYS;	/* We _will_ be interrupted :-) */
					/* (but restart after we continue) */
}

static int read_chan(unsigned int channel, struct file * file, char * buf, int nr)
{
	struct tty_struct * tty;
	struct tty_struct * other_tty = NULL;
	int c;
	char * b=buf;
	int minimum,time;

	if (channel > 255)
		return -EIO;
	tty = TTY_TABLE(channel);
	if (!(tty->read_q && tty->secondary))
		return -EIO;
	if ((tty->pgrp > 0) &&
	    (current->tty == channel) &&
	    (tty->pgrp != current->pgrp))
		if (is_ignored(SIGTTIN) || is_orphaned_pgrp(current->pgrp))
			return -EIO;
		else
			return(tty_signal(SIGTTIN, tty));
	if (channel & 0x80)
		other_tty = tty_table + (channel ^ 0x40);
	time = 10L*tty->termios.c_cc[VTIME];
	minimum = tty->termios.c_cc[VMIN];
	if (L_CANON(tty)) {
		minimum = nr;
		current->timeout = 0xffffffff;
		time = 0;
	} else if (minimum)
		current->timeout = 0xffffffff;
	else {
		minimum = nr;
		if (time)
			current->timeout = time + jiffies;
		time = 0;
	}
	if (file->f_flags & O_NONBLOCK)
		time = current->timeout = 0;
	if (minimum>nr)
		minimum = nr;
	TTY_READ_FLUSH(tty);
	while (nr>0) {
		if (other_tty && other_tty->write)
			TTY_WRITE_FLUSH(other_tty);
		cli();
		if (EMPTY(tty->secondary) || (L_CANON(tty) &&
		    !FULL(tty->read_q) && !tty->secondary->data)) {
			if (!current->timeout)
				break;
			if (current->signal & ~current->blocked) 
				break;
			if (IS_A_PTY_SLAVE(channel) && C_HUP(other_tty))
				break;
			interruptible_sleep_on(&tty->secondary->proc_list);
			sti();
			TTY_READ_FLUSH(tty);
			continue;
		}
		sti();
		do {
			c = GETCH(tty->secondary);
			if ((EOF_CHAR(tty) != _POSIX_VDISABLE &&
			     c==EOF_CHAR(tty)) || c==10)
				tty->secondary->data--;
			if ((EOF_CHAR(tty) != _POSIX_VDISABLE &&
			     c==EOF_CHAR(tty)) && L_CANON(tty))
				break;
			else {
				put_fs_byte(c,b++);
				if (!--nr)
					break;
			}
			if (c==10 && L_CANON(tty))
				break;
		} while (nr>0 && !EMPTY(tty->secondary));
		wake_up(&tty->read_q->proc_list);
		if (L_CANON(tty) || b-buf >= minimum)
			break;
		if (time)
			current->timeout = time+jiffies;
	}
	sti();
	TTY_READ_FLUSH(tty);
	if (other_tty && other_tty->write)
		TTY_WRITE_FLUSH(other_tty);
	current->timeout = 0;
	if (b-buf)
		return b-buf;
	if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	if (file->f_flags & O_NONBLOCK)
		return -EAGAIN;
	return 0;
}

static int write_chan(unsigned int channel, struct file * file, char * buf, int nr)
{
	static cr_flag=0;
	struct tty_struct * tty;
	char c, *b=buf;

	if (channel > 255)
		return -EIO;
	tty = TTY_TABLE(channel);
	if (L_TOSTOP(tty) && (tty->pgrp > 0) &&
	    (current->tty == channel) && (tty->pgrp != current->pgrp)) {
		if (is_orphaned_pgrp(tty->pgrp))
			return -EIO;
		if (!is_ignored(SIGTTOU))
			return tty_signal(SIGTTOU, tty);
	}
	if (nr < 0)
		return -EINVAL;
	if (!nr)
		return 0;
	if (redirect && tty == TTY_TABLE(0))
		tty = redirect;
	if (!(tty->write_q && tty->write))
		return -EIO;
	while (nr>0) {
		if (current->signal & ~current->blocked)
			break;
		if (FULL(tty->write_q)) {
			TTY_WRITE_FLUSH(tty);
			cli();
			if (FULL(tty->write_q))
				interruptible_sleep_on(&tty->write_q->proc_list);
			sti();
			continue;
		}
		while (nr>0 && !FULL(tty->write_q)) {
			c=get_fs_byte(b);
			if (O_POST(tty)) {
				if (c=='\r' && O_CRNL(tty))
					c='\n';
				else if (c=='\n' && O_NLRET(tty))
					c='\r';
				if (c=='\n' && !cr_flag && O_NLCR(tty)) {
					cr_flag = 1;
					PUTCH(13,tty->write_q);
					continue;
				}
				if (O_LCUC(tty))
					c=toupper(c);
			}
			b++; nr--;
			cr_flag = 0;
			PUTCH(c,tty->write_q);
		}
		if (nr>0)
			schedule();
	}
	TTY_WRITE_FLUSH(tty);
	if (b-buf)
		return b-buf;
	if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	return 0;
}

static int tty_read(struct inode * inode, struct file * file, char * buf, int count)
{
	int i;
	
	i = read_chan(current->tty,file,buf,count);
	if (i > 0)
		inode->i_atime = CURRENT_TIME;
	return i;
}

static int ttyx_read(struct inode * inode, struct file * file, char * buf, int count)
{
	int i;
	
	i = read_chan(MINOR(inode->i_rdev),file,buf,count);
	if (i > 0)
		inode->i_atime = CURRENT_TIME;
	return i;
}

static int tty_write(struct inode * inode, struct file * file, char * buf, int count)
{
	int i;
	
	i = write_chan(current->tty,file,buf,count);
	if (i > 0)
		inode->i_mtime = CURRENT_TIME;
	return i;
}

static int ttyx_write(struct inode * inode, struct file * file, char * buf, int count)
{
	int i;
	
	i = write_chan(MINOR(inode->i_rdev),file,buf,count);
	if (i > 0)
		inode->i_mtime = CURRENT_TIME;
	return i;
}

static int tty_lseek(struct inode * inode, struct file * file, off_t offset, int orig)
{
	return -EBADF;
}

/*
 * tty_open and tty_release keep up the tty count that contains the
 * number of opens done on a tty. We cannot use the inode-count, as
 * different inodes might point to the same tty.
 *
 * Open-counting is needed for pty masters, as well as for keeping
 * track of serial lines: DTR is dropped when the last close happens.
 */
static int tty_open(struct inode * inode, struct file * filp)
{
	struct tty_struct *tty;
	int dev;

	dev = inode->i_rdev;
	if (MAJOR(dev) == 5)
		dev = current->tty;
	else
		dev = MINOR(dev);
	if (dev < 0)
		return -ENODEV;
	tty = TTY_TABLE(dev);
	if (IS_A_PTY_MASTER(dev)) {
		if (tty->count)
			return -EAGAIN;
	}
	tty->count++;
	if (!(filp->f_flags & O_NOCTTY) &&
	    current->leader &&
	    current->tty<0 &&
	    tty->session==0) {
		current->tty = dev;
		tty->session = current->session;
		tty->pgrp = current->pgrp;
	}
	if (IS_A_SERIAL(dev))
		serial_open(dev-64);
	return 0;
}

static void tty_release(struct inode * inode, struct file * filp)
{
	int dev;
	unsigned short port;
	struct tty_struct * tty, * slave;

	dev = inode->i_rdev;
	if (MAJOR(dev) == 5)
		dev = current->tty;
	else
		dev = MINOR(dev);
	if (dev < 0)
		return;
	tty = TTY_TABLE(dev);
	if (--tty->count)
		return;
	if (tty == redirect)
		redirect = NULL;
	if (port = tty->read_q->data)
		outb(0x0c,port+4);	/* reset DTR, RTS, */
	if (IS_A_PTY_MASTER(dev)) {
		slave = tty_table + PTY_OTHER(dev);
		if (slave->pgrp > 0)
			kill_pg(slave->pgrp,SIGHUP,1);
	}
}

static struct file_operations tty_fops = {
	tty_lseek,
	tty_read,
	tty_write,
	NULL,		/* tty_readdir */
	NULL,		/* tty_select */
	tty_ioctl,
	tty_open,
	tty_release
};

static struct file_operations ttyx_fops = {
	tty_lseek,
	ttyx_read,
	ttyx_write,
	NULL,		/* ttyx_readdir */
	NULL,		/* ttyx_select */
	tty_ioctl,	/* ttyx_ioctl */
	tty_open,
	tty_release
};

void tty_init(void)
{
	int i;

	chrdev_fops[4] = &ttyx_fops;
	chrdev_fops[5] = &tty_fops;
	for (i=0 ; i < QUEUES ; i++)
		tty_queues[i] = (struct tty_queue) {0,0,0,0,""};
	rs_queues[0] = (struct tty_queue) {0x3f8,0,0,0,""};
	rs_queues[1] = (struct tty_queue) {0x3f8,0,0,0,""};
	rs_queues[3] = (struct tty_queue) {0x2f8,0,0,0,""};
	rs_queues[4] = (struct tty_queue) {0x2f8,0,0,0,""};
	rs_queues[6] = (struct tty_queue) {0x3e8,0,0,0,""};
	rs_queues[7] = (struct tty_queue) {0x3e8,0,0,0,""};
	rs_queues[9] = (struct tty_queue) {0x2e8,0,0,0,""};
	rs_queues[10] = (struct tty_queue) {0x2e8,0,0,0,""};
	for (i=0 ; i<256 ; i++) {
		tty_table[i] =  (struct tty_struct) {
		 	{0, 0, 0, 0, 0, INIT_C_CC},
			-1, 0, 0, 0, 0, {0,0,0,0},
			NULL, NULL, NULL, NULL
		};
	}
	con_init();
	for (i = 0 ; i<NR_CONSOLES ; i++) {
		con_table[i] = (struct tty_struct) {
		 	{ICRNL,		/* change incoming CR to NL */
			OPOST|ONLCR,	/* change outgoing NL to CRNL */
			B38400 | CS8,
			IXON | ISIG | ICANON | ECHO | ECHOCTL | ECHOKE,
			0,		/* console termio */
			INIT_C_CC},
			-1,		/* initial pgrp */
			0,			/* initial session */
			0,			/* initial stopped */
			0,			/* initial busy */
			0,			/* initial count */
			{video_num_lines,video_num_columns,0,0},
			con_write,
			con_queues+0+i*3,con_queues+1+i*3,con_queues+2+i*3
		};
	}
	for (i = 0 ; i<NR_SERIALS ; i++) {
		rs_table[i] = (struct tty_struct) {
			{0, /* no translation */
			0,  /* no translation */
			B2400 | CS8,
			0,
			0,
			INIT_C_CC},
			-1,
			0,
			0,
			0,
			0,
			{25,80,0,0},
			rs_write,
			rs_queues+0+i*3,rs_queues+1+i*3,rs_queues+2+i*3
		};
	}
	for (i = 0 ; i<NR_PTYS ; i++) {
		mpty_table[i] = (struct tty_struct) {
			{0, /* no translation */
			0,  /* no translation */
			B9600 | CS8,
			0,
			0,
			INIT_C_CC},
			-1,
			0,
			0,
			0,
			0,
			{25,80,0,0},
			mpty_write,
			mpty_queues+0+i*3,mpty_queues+1+i*3,mpty_queues+2+i*3
		};
		spty_table[i] = (struct tty_struct) {
			{0, /* no translation */
			0,  /* no translation */
			B9600 | CS8,
			IXON | ISIG | ICANON,
			0,
			INIT_C_CC},
			-1,
			0,
			0,
			0,
			0,
			{25,80,0,0},
			spty_write,
			spty_queues+0+i*3,spty_queues+1+i*3,spty_queues+2+i*3
		};
	}
	rs_init();
	printk("%d virtual consoles\n\r",NR_CONSOLES);
	printk("%d pty's\n\r",NR_PTYS);
}

#ifndef _LINUX_TTY_H
#define _LINUX_TTY_H

/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 *
 * NOTE! Don't touch this without checking that nothing in rs_io.s or
 * con_io.s breaks. Some constants are hardwired into the system (mainly
 * offsets into 'tty_queue'
 */

#include <linux/termios.h>

#include <asm/system.h>

#define NR_CONSOLES	8
#define NR_SERIALS	4
#define NR_PTYS		4

/*
 * These are set up by the setup-routine at boot-time:
 */

struct screen_info {
	unsigned char  orig_x;
	unsigned char  orig_y;
	unsigned char  unused1[2];
	unsigned short orig_video_page;
	unsigned char  orig_video_mode;
	unsigned char  orig_video_cols;
	unsigned short orig_video_ega_ax;
	unsigned short orig_video_ega_bx;
	unsigned short orig_video_ega_cx;
	unsigned char  orig_video_lines;
};

extern struct screen_info screen_info;

#define ORIG_X			(screen_info.orig_x)
#define ORIG_Y			(screen_info.orig_y)
#define ORIG_VIDEO_PAGE		(screen_info.orig_video_page)
#define ORIG_VIDEO_MODE		(screen_info.orig_video_mode)
#define ORIG_VIDEO_COLS 	(screen_info.orig_video_cols)
#define ORIG_VIDEO_EGA_AX	(screen_info.orig_video_ega_ax)
#define ORIG_VIDEO_EGA_BX	(screen_info.orig_video_ega_bx)
#define ORIG_VIDEO_EGA_CX	(screen_info.orig_video_ega_cx)
#define ORIG_VIDEO_LINES	(screen_info.orig_video_lines)

#define VIDEO_TYPE_MDA		0x10	/* Monochrome Text Display	*/
#define VIDEO_TYPE_CGA		0x11	/* CGA Display 			*/
#define VIDEO_TYPE_EGAM		0x20	/* EGA/VGA in Monochrome Mode	*/
#define VIDEO_TYPE_EGAC		0x21	/* EGA/VGA in Color Mode	*/

/*
 * This character is the same as _POSIX_VDISABLE: it cannot be used as
 * a c_cc[] character, but indicates that a particular special character
 * isn't in use (eg VINTR ahs no character etc)
 */
#define __DISABLED_CHAR '\0'

#define TTY_BUF_SIZE 2048

struct tty_queue {
	unsigned long data;
	unsigned long head;
	unsigned long tail;
	struct wait_queue * proc_list;
	unsigned char buf[TTY_BUF_SIZE];
};

struct serial_struct {
	unsigned short type;
	unsigned short line;
	unsigned short port;
	unsigned short irq;
	struct tty_struct * tty;
};

/*
 * These are the supported serial types.
 */
#define PORT_UNKNOWN	0
#define PORT_8250	1
#define PORT_16450	2
#define PORT_16550	3
#define PORT_16550A	4

#define IS_A_CONSOLE(min)	(((min) & 0xC0) == 0x00)
#define IS_A_SERIAL(min)	(((min) & 0xC0) == 0x40)
#define IS_A_PTY(min)		((min) & 0x80)
#define IS_A_PTY_MASTER(min)	(((min) & 0xC0) == 0x80)
#define IS_A_PTY_SLAVE(min)	(((min) & 0xC0) == 0xC0)
#define PTY_OTHER(min)		((min) ^ 0x40)

#define INC(a) ((a) = ((a)+1) & (TTY_BUF_SIZE-1))
#define DEC(a) ((a) = ((a)-1) & (TTY_BUF_SIZE-1))
#define EMPTY(a) ((a)->head == (a)->tail)
#define LEFT(a) (((a)->tail-(a)->head-1)&(TTY_BUF_SIZE-1))
#define LAST(a) ((a)->buf[(TTY_BUF_SIZE-1)&((a)->head-1)])
#define FULL(a) (!LEFT(a))
#define CHARS(a) (((a)->head-(a)->tail)&(TTY_BUF_SIZE-1))

extern void put_tty_queue(char c, struct tty_queue * queue);
extern int get_tty_queue(struct tty_queue * queue);

#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE])
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])
#define SUSPEND_CHAR(tty) ((tty)->termios.c_cc[VSUSP])

#define _L_FLAG(tty,f)	((tty)->termios.c_lflag & f)
#define _I_FLAG(tty,f)	((tty)->termios.c_iflag & f)
#define _O_FLAG(tty,f)	((tty)->termios.c_oflag & f)

#define L_CANON(tty)	_L_FLAG((tty),ICANON)
#define L_ISIG(tty)	_L_FLAG((tty),ISIG)
#define L_ECHO(tty)	_L_FLAG((tty),ECHO)
#define L_ECHOE(tty)	_L_FLAG((tty),ECHOE)
#define L_ECHOK(tty)	_L_FLAG((tty),ECHOK)
#define L_ECHONL(tty)	_L_FLAG((tty),ECHONL)
#define L_ECHOCTL(tty)	_L_FLAG((tty),ECHOCTL)
#define L_ECHOKE(tty)	_L_FLAG((tty),ECHOKE)
#define L_TOSTOP(tty)	_L_FLAG((tty),TOSTOP)

#define I_UCLC(tty)	_I_FLAG((tty),IUCLC)
#define I_NLCR(tty)	_I_FLAG((tty),INLCR)
#define I_CRNL(tty)	_I_FLAG((tty),ICRNL)
#define I_NOCR(tty)	_I_FLAG((tty),IGNCR)
#define I_IXON(tty)	_I_FLAG((tty),IXON)
#define I_STRP(tty)	_I_FLAG((tty),ISTRIP)

#define O_POST(tty)	_O_FLAG((tty),OPOST)
#define O_NLCR(tty)	_O_FLAG((tty),ONLCR)
#define O_CRNL(tty)	_O_FLAG((tty),OCRNL)
#define O_NLRET(tty)	_O_FLAG((tty),ONLRET)
#define O_LCUC(tty)	_O_FLAG((tty),OLCUC)

#define C_SPEED(tty)	((tty)->termios.c_cflag & CBAUD)
#define C_HUP(tty)	(C_SPEED((tty)) == B0)

struct tty_struct {
	struct termios termios;
	int pgrp;
	int session;
	int stopped;
	int flags;
	int count;
	struct winsize winsize;
	void (*write)(struct tty_struct * tty);
	struct tty_struct *link;
	struct tty_queue *read_q;
	struct tty_queue *write_q;
	struct tty_queue *secondary;
	};

/*
 * so that interrupts won't be able to mess up the
 * queues, copy_to_cooked must be atomic with repect
 * to itself, as must tty->write. These are the flag
 * bit-numbers. Use the set_bit() and clear_bit()
 * macros to make it all atomic.
 */
#define TTY_WRITE_BUSY 0
#define TTY_READ_BUSY 1
#define TTY_CR_PENDING 2

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. Both set_bit and clear_bit return 0
 * if the bit-setting went ok, != 0 if the bit already was set/cleared.
 */
extern inline int set_bit(int nr,int * addr)
{
	char ok;

	__asm__ __volatile__("btsl %1,%2\n\tsetb %0":
		"=q" (ok):"r" (nr),"m" (*(addr)));
	return ok;
}

extern inline int clear_bit(int nr, int * addr)
{
	char ok;

	__asm__ __volatile__("btrl %1,%2\n\tsetnb %0":
		"=q" (ok):"r" (nr),"m" (*(addr)));
	return ok;
}

#define TTY_WRITE_FLUSH(tty) tty_write_flush((tty))
#define TTY_READ_FLUSH(tty) tty_read_flush((tty))

extern void tty_write_flush(struct tty_struct *);
extern void tty_read_flush(struct tty_struct *);

extern struct tty_struct tty_table[];
extern struct serial_struct serial_table[];
extern struct tty_struct * redirect;
extern int fg_console;
extern unsigned long video_num_columns;
extern unsigned long video_num_lines;


#define TTY_TABLE(nr) \
(tty_table + ((nr) ? (((nr) < 64)? (nr)-1:(nr))	: fg_console))

/*	intr=^C		quit=^|		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"

extern long rs_init(long);
extern long lp_init(long);
extern long con_init(long);
extern long tty_init(long);

extern void flush_input(struct tty_struct * tty);
extern void flush_output(struct tty_struct * tty);
extern void wait_until_sent(struct tty_struct * tty);
extern void copy_to_cooked(struct tty_struct * tty);

extern int tty_ioctl(struct inode *, struct file *, unsigned int, unsigned int);
extern int is_orphaned_pgrp(int pgrp);
extern int is_ignored(int sig);
extern int tty_signal(int sig, struct tty_struct *tty);
extern int kill_pg(int pgrp, int sig, int priv);

/* tty write functions */

extern void rs_write(struct tty_struct * tty);
extern void con_write(struct tty_struct * tty);
extern void mpty_write(struct tty_struct * tty);
extern void spty_write(struct tty_struct * tty);

/* serial.c */

extern int  serial_open(unsigned int line, struct file * filp);
extern void serial_close(unsigned int line, struct file * filp);
extern void change_speed(unsigned int line);
extern void send_break(unsigned int line);
extern int get_serial_info(unsigned int, struct serial_struct *);
extern int set_serial_info(unsigned int, struct serial_struct *);

/* pty.c */

extern int  pty_open(unsigned int dev, struct file * filp);
extern void pty_close(unsigned int dev, struct file * filp);

/* console.c */

void update_screen(int new_console);
void blank_screen(void);
void unblank_screen(void);

#endif

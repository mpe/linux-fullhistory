/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 *
 * NOTE! Don't touch this without checking that nothing in rs_io.s or
 * con_io.s breaks. Some constants are hardwired into the system (mainly
 * offsets into 'tty_queue'
 */

#ifndef _TTY_H
#define _TTY_H

#include <asm/system.h>

#define MAX_CONSOLES	8
#define NR_SERIALS	4
#define NR_PTYS		4

extern int NR_CONSOLES;

#include <termios.h>

#define TTY_BUF_SIZE 2048

struct tty_queue {
	unsigned long data;
	unsigned long head;
	unsigned long tail;
	struct task_struct * proc_list;
	unsigned char buf[TTY_BUF_SIZE];
};

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

static inline void PUTCH(char c, struct tty_queue * queue)
{
	int head;

	cli();
	head = (queue->head + 1) & (TTY_BUF_SIZE-1);
	if (head != queue->tail) {
		queue->buf[queue->head] = c;
		queue->head = head;
	}
	sti();
}

static inline int GETCH(struct tty_queue * queue)
{
	int result = -1;

	if (queue->tail != queue->head) {
		result = 0xff & queue->buf[queue->tail];
		queue->tail = (queue->tail + 1) & (TTY_BUF_SIZE-1);
	}
	return result;
}
	
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
	int busy;
	int count;
	struct winsize winsize;
	void (*write)(struct tty_struct * tty);
	struct tty_queue *read_q;
	struct tty_queue *write_q;
	struct tty_queue *secondary;
	};

/*
 * so that interrupts won't be able to mess up the
 * queues, copy_to_cooked must be atomic with repect
 * to itself, as must tty->write.
 */
#define TTY_WRITE_BUSY 1
#define TTY_READ_BUSY 2

#define TTY_WRITE_FLUSH(tty) \
do { \
	cli(); \
	if (!EMPTY((tty)->write_q) && !(TTY_WRITE_BUSY & (tty)->busy)) { \
		(tty)->busy |= TTY_WRITE_BUSY; \
		sti(); \
		(tty)->write((tty)); \
		cli(); \
		(tty)->busy &= ~TTY_WRITE_BUSY; \
	} \
	sti(); \
} while (0)

#define TTY_READ_FLUSH(tty) \
do { \
	cli(); \
	if (!EMPTY((tty)->read_q) && !(TTY_READ_BUSY & (tty)->busy)) { \
		(tty)->busy |= TTY_READ_BUSY; \
		sti(); \
		copy_to_cooked((tty)); \
		cli(); \
		(tty)->busy &= ~TTY_READ_BUSY; \
	} \
	sti(); \
} while (0)

extern struct tty_struct tty_table[];
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

extern void rs_init(void);
extern void lp_init(void);
extern void con_init(void);
extern void tty_init(void);

extern void flush(struct tty_queue * queue);

extern int tty_ioctl(struct inode *, struct file *, unsigned int, unsigned int);
extern int is_orphaned_pgrp(int pgrp);
extern int is_ignored(int sig);
extern int tty_signal(int sig, struct tty_struct *tty);

extern void rs_write(struct tty_struct * tty);
extern void con_write(struct tty_struct * tty);
extern void mpty_write(struct tty_struct * tty);
extern void spty_write(struct tty_struct * tty);

extern void serial_open(unsigned int line);

void copy_to_cooked(struct tty_struct * tty);

void update_screen(int new_console);

int kill_pg(int pgrp, int sig, int priv);
   
#endif

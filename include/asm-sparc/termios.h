/* $Id: termios.h,v 1.13 1996/04/04 12:51:30 davem Exp $ */
#ifndef _SPARC_TERMIOS_H
#define _SPARC_TERMIOS_H

#include <asm/ioctls.h>
#include <asm/termbits.h>

struct sgttyb {
	char	sg_ispeed;
	char	sg_ospeed;
	char	sg_erase;
	char	sg_kill;
	short	sg_flags;
};

struct tchars {
	char	t_intrc;
	char	t_quitc;
	char	t_startc;
	char	t_stopc;
	char	t_eofc;
	char	t_brkc;
};

struct ltchars {
	char	t_suspc;
	char	t_dsuspc;
	char	t_rprntc;
	char	t_flushc;
	char	t_werasc;
	char	t_lnextc;
};

struct sunos_ttysize {
	int st_lines;   /* Lines on the terminal */
	int st_columns; /* Columns on the terminal */
};

/* Used for packet mode */
#define TIOCPKT_DATA		 0
#define TIOCPKT_FLUSHREAD	 1
#define TIOCPKT_FLUSHWRITE	 2
#define TIOCPKT_STOP		 4
#define TIOCPKT_START		 8
#define TIOCPKT_NOSTOP		16
#define TIOCPKT_DOSTOP		32

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

/* line disciplines */
#define N_TTY		0
#define N_SLIP		1
#define N_MOUSE		2
#define N_PPP		3

#ifdef __KERNEL__
/*	intr=^C		quit=^\		erase=del	kill=^U
	eof/vmin=\1	eol/vtime=\0	eol2=\0		sxtc=\0
	start=^Q	stop=^S		susp=^Z		dsusp=^Y
	reprint=^R	discard=^U	werase=^W	lnext=^V
*/
#define INIT_C_CC "\003\034\177\025\001\000\000\000\021\023\032\031\022\025\027\026"

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
extern inline void trans_from_termio(struct termio * termio,
	struct termios * termios)
{
#define SET_LOW_BITS(x,y)	((x) = (0xffff0000 & (x)) | (y))
	SET_LOW_BITS(termios->c_iflag, termio->c_iflag);
	SET_LOW_BITS(termios->c_oflag, termio->c_oflag);
	SET_LOW_BITS(termios->c_cflag, termio->c_cflag);
	SET_LOW_BITS(termios->c_lflag, termio->c_lflag);
#undef SET_LOW_BITS
	memcpy(termios->c_cc, termio->c_cc, NCC);
}

/*
 * Translate a "termios" structure into a "termio". Ugh.
 */
extern inline void trans_to_termio(struct termios * termios,
	struct termio * termio)
{
	termio->c_iflag = termios->c_iflag;
	termio->c_oflag = termios->c_oflag;
	termio->c_cflag = termios->c_cflag;
	termio->c_lflag = termios->c_lflag;
	termio->c_line	= termios->c_line;
	memcpy(termio->c_cc, termios->c_cc, NCC);
}

#endif	/* __KERNEL__ */

#endif /* _SPARC_TERMIOS_H */

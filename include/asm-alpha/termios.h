#ifndef _ALPHA_TERMIOS_H
#define _ALPHA_TERMIOS_H

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

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define NCC 8
struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[NCC];	/* control characters */
};

/*
 * c_cc characters in the termio structure.  Oh, how I love being
 * backwardly compatible.  Notice that character 4 and 5 are
 * interpreted differently depending on whether ICANON is set in
 * c_lflag.  If it's set, they are used as _VEOF and _VEOL, otherwise
 * as _VMIN and V_TIME.  This is for compatibility with OSF/1 (which
 * is compatible with sysV)...
 */
#define _VINTR	0
#define _VQUIT	1
#define _VERASE	2
#define _VKILL	3
#define _VEOF	4
#define _VMIN	4
#define _VEOL	5
#define _VTIME	5
#define _VEOL2	6
#define _VSWTC	7

/* line disciplines */
#define N_TTY		0
#define N_SLIP		1
#define N_MOUSE		2
#define N_PPP		3

#ifdef __KERNEL__
/*	eof=^D		eol=\0		eol2=\0		erase=del
	werase=^W	kill=^U		reprint=^R	sxtc=\0
	intr=^C		quit=^\		susp=^Z		<OSF/1 VDSUSP>
	start=^Q	stop=^S		lnext=^V	discard=^U
	vmin=\1		vtime=\0
*/
#define INIT_C_CC "\004\000\000\177\027\025\022\000\003\034\032\000\021\023\026\025\001\000"

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
	termios->c_cc[VINTR] = termio->c_cc[_VINTR];
	termios->c_cc[VQUIT] = termio->c_cc[_VQUIT];
	termios->c_cc[VERASE]= termio->c_cc[_VERASE];
	termios->c_cc[VKILL] = termio->c_cc[_VKILL];
	termios->c_cc[VEOF]  = termio->c_cc[_VEOF];
	termios->c_cc[VMIN]  = termio->c_cc[_VMIN];
	termios->c_cc[VEOL]  = termio->c_cc[_VEOL];
	termios->c_cc[VTIME] = termio->c_cc[_VTIME];
	termios->c_cc[VEOL2] = termio->c_cc[_VEOL2];
	termios->c_cc[VSWTC] = termio->c_cc[_VSWTC];
}

/*
 * Translate a "termios" structure into a "termio". Ugh.
 *
 * Note the "fun" _VMIN overloading.
 */
extern inline void trans_to_termio(struct termios * termios,
	struct termio * termio)
{
	termio->c_iflag = termios->c_iflag;
	termio->c_oflag = termios->c_oflag;
	termio->c_cflag = termios->c_cflag;
	termio->c_lflag = termios->c_lflag;
	termio->c_line	= termios->c_line;
	termio->c_cc[_VINTR] = termios->c_cc[VINTR];
	termio->c_cc[_VQUIT] = termios->c_cc[VQUIT];
	termio->c_cc[_VERASE]= termios->c_cc[VERASE];
	termio->c_cc[_VKILL] = termios->c_cc[VKILL];
	termio->c_cc[_VEOF]  = termios->c_cc[VEOF];
	termio->c_cc[_VEOL]  = termios->c_cc[VEOL];
	termio->c_cc[_VEOL2] = termios->c_cc[VEOL2];
	termio->c_cc[_VSWTC] = termios->c_cc[VSWTC];
	if (!(termios->c_lflag & ICANON)) {
		termio->c_cc[_VMIN]  = termios->c_cc[VMIN];
		termio->c_cc[_VTIME] = termios->c_cc[VTIME];
	}
}

#endif	/* __KERNEL__ */

#endif	/* _ALPHA_TERMIOS_H */

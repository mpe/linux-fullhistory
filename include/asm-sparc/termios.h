/* $Id: termios.h,v 1.20 1996/10/31 00:59:54 davem Exp $ */
#ifndef _SPARC_TERMIOS_H
#define _SPARC_TERMIOS_H

#include <asm/ioctls.h>
#include <asm/termbits.h>

#if defined(__KERNEL__) || defined(__DEFINE_BSD_TERMIOS)
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
#endif /* __KERNEL__ */

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

/*
 * c_cc characters in the termio structure.  Oh, how I love being
 * backwardly compatible.  Notice that character 4 and 5 are
 * interpreted differently depending on whether ICANON is set in
 * c_lflag.  If it's set, they are used as _VEOF and _VEOL, otherwise
 * as _VMIN and V_TIME.  This is for compatibility with OSF/1 (which
 * is compatible with sysV)...
 */
#define _VMIN	4
#define _VTIME	5


#include <linux/string.h>

/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		eol=\0		eol2=\0		sxtc=\0
	start=^Q	stop=^S		susp=^Z		dsusp=^Y
	reprint=^R	discard=^U	werase=^W	lnext=^V
	vmin=\1         vtime=\0
*/
#define INIT_C_CC "\003\034\177\025\004\000\000\000\021\023\032\031\022\025\027\026\001\000"

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
extern __inline__ void trans_from_termio(struct termio * termio,
					 struct termios * termios)
{
#define SET_LOW_BITS(x,y)	((x) = (0xffff0000 & (x)) | (y))
	SET_LOW_BITS(termios->c_iflag, termio->c_iflag);
	SET_LOW_BITS(termios->c_oflag, termio->c_oflag);
	SET_LOW_BITS(termios->c_cflag, termio->c_cflag);
	SET_LOW_BITS(termios->c_lflag, termio->c_lflag);
#undef SET_LOW_BITS
	memcpy (termios->c_cc, termio->c_cc, NCC);
}

/*
 * Translate a "termios" structure into a "termio". Ugh.
 *
 * Note the "fun" _VMIN overloading.
 */
extern __inline__ void trans_to_termio(struct termios * termios,
				       struct termio * termio)
{
	termio->c_iflag = termios->c_iflag;
	termio->c_oflag = termios->c_oflag;
	termio->c_cflag = termios->c_cflag;
	termio->c_lflag = termios->c_lflag;
	termio->c_line	= termios->c_line;
	memcpy(termio->c_cc, termios->c_cc, NCC);
	if (!(termios->c_lflag & ICANON)) {
		termio->c_cc[_VMIN]  = termios->c_cc[VMIN];
		termio->c_cc[_VTIME] = termios->c_cc[VTIME];
	}
}

/* Note that in this case DEST is a user buffer and thus the checking
 * and this ugly macro to avoid header file problems.
 */
#define termios_to_userland(d, s) \
do { \
	struct termios *dest = (d); \
	struct termios *source = (s); \
	put_user(source->c_iflag, &dest->c_iflag); \
	put_user(source->c_oflag, &dest->c_oflag); \
	put_user(source->c_cflag, &dest->c_cflag); \
	put_user(source->c_lflag, &dest->c_lflag); \
	put_user(source->c_line, &dest->c_line); \
	copy_to_user(dest->c_cc, source->c_cc, NCCS); \
	if (!(source->c_lflag & ICANON)){ \
		put_user(source->c_cc[VMIN], &dest->c_cc[_VMIN]); \
		put_user(source->c_cc[VTIME], &dest->c_cc[_VTIME]); \
	} else { \
		put_user(source->c_cc[VEOF], &dest->c_cc[VEOF]); \
		put_user(source->c_cc[VEOL], &dest->c_cc[VEOL]); \
	} \
} while(0)

/* termios to termios handling SunOS overloading of eof,eol/vmin,vtime
 * In this case we are only working with kernel buffers so direct
 * accesses are ok.
 */
extern __inline__ void termios_from_userland(struct termios * source,
					     struct termios * dest)
{
	dest->c_iflag = source->c_iflag;
	dest->c_oflag = source->c_oflag;
	dest->c_cflag = source->c_cflag;
	dest->c_lflag = source->c_lflag;
	dest->c_line  = source->c_line;
	memcpy(dest->c_cc, source->c_cc, NCCS);
	if (dest->c_lflag & ICANON){
		dest->c_cc [VEOF] = source->c_cc [VEOF];
		dest->c_cc [VEOL] = source->c_cc [VEOL];
	} else {
		dest->c_cc[VMIN]  = source->c_cc[_VMIN];
		dest->c_cc[VTIME] = source->c_cc[_VTIME];
	}
}

#endif	/* __KERNEL__ */

#endif /* _SPARC_TERMIOS_H */

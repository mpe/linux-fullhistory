#ifndef _PPC_TERMIOS_H
#define _PPC_TERMIOS_H

/*
 * Liberally adapted from alpha/termios.h.  In particular, the c_cc[]
 * fields have been reordered so that termio & termios share the
 * common subset in the same order (for brain dead programs that don't
 * know or care about the differences).
 */

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

#define FIOCLEX		_IO('f', 1)
#define FIONCLEX	_IO('f', 2)
#define FIOASYNC	_IOW('f', 125, int)
#define FIONBIO		_IOW('f', 126, int)
#define FIONREAD	_IOR('f', 127, int)
#define TIOCINQ		FIONREAD

#define TIOCGETP	_IOR('t', 8, struct sgttyb)
#define TIOCSETP	_IOW('t', 9, struct sgttyb)
#define TIOCSETN	_IOW('t', 10, struct sgttyb)	/* TIOCSETP wo flush */

#define TIOCSETC	_IOW('t', 17, struct tchars)
#define TIOCGETC	_IOR('t', 18, struct tchars)
#define TCGETS		_IOR('t', 19, struct termios)
#define TCSETS		_IOW('t', 20, struct termios)
#define TCSETSW		_IOW('t', 21, struct termios)
#define TCSETSF		_IOW('t', 22, struct termios)

#define TCGETA		_IOR('t', 23, struct termio)
#define TCSETA		_IOW('t', 24, struct termio)
#define TCSETAW		_IOW('t', 25, struct termio)
#define TCSETAF		_IOW('t', 28, struct termio)

#define TCSBRK		_IO('t', 29)
#define TCXONC		_IO('t', 30)
#define TCFLSH		_IO('t', 31)

#define TIOCSWINSZ	_IOW('t', 103, struct winsize)
#define TIOCGWINSZ	_IOR('t', 104, struct winsize)
#define	TIOCSTART	_IO('t', 110)		/* start output, like ^Q */
#define	TIOCSTOP	_IO('t', 111)		/* stop output, like ^S */
#define TIOCOUTQ        _IOR('t', 115, int)     /* output queue size */

#define TIOCGLTC	_IOR('t', 116, struct ltchars)
#define TIOCSLTC	_IOW('t', 117, struct ltchars)
#define TIOCSPGRP	_IOW('t', 118, int)
#define TIOCGPGRP	_IOR('t', 119, int)

#define TIOCEXCL	0x540C
#define TIOCNXCL	0x540D
#define TIOCSCTTY	0x540E

#define TIOCSTI		0x5412
#define TIOCMGET	0x5415
#define TIOCMBIS	0x5416
#define TIOCMBIC	0x5417
#define TIOCMSET	0x5418
#define TIOCGSOFTCAR	0x5419
#define TIOCSSOFTCAR	0x541A
#define TIOCLINUX	0x541C
#define TIOCCONS	0x541D
#define TIOCGSERIAL	0x541E
#define TIOCSSERIAL	0x541F
#define TIOCPKT		0x5420

#define TIOCNOTTY	0x5422
#define TIOCSETD	0x5423
#define TIOCGETD	0x5424
#define TCSBRKP		0x5425	/* Needed for POSIX tcsendbreak() */
#define TIOCTTYGSTRUCT	0x5426  /* For debugging only */

#define TIOCSERCONFIG	0x5453
#define TIOCSERGWILD	0x5454
#define TIOCSERSWILD	0x5455
#define TIOCGLCKTRMIOS	0x5456
#define TIOCSLCKTRMIOS	0x5457
#define TIOCSERGSTRUCT	0x5458 /* For debugging only */
#define TIOCSERGETLSR   0x5459 /* Get line status register */
#define TIOCSERGETMULTI 0x545A /* Get multiport config  */
#define TIOCSERSETMULTI 0x545B /* Set multiport config */

#define TIOCMIWAIT	0x545C	/* wait for a change on serial input line(s) */
#define TIOCGICOUNT	0x545D	/* read serial port inline interrupt counts */

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

#define NCC 10
struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[NCC];	/* control characters */
};

#define NCCS 19
struct termios {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_cc[NCCS];		/* control characters */
	cc_t c_line;			/* line discipline (== c_cc[19]) */
	int c_ispeed;			/* input speed */
	int c_ospeed;			/* output speed */
};

/* c_cc characters */
#define _VINTR	0
#define _VQUIT	1
#define _VERASE	2
#define _VKILL	3
#define _VEOF	4
#define _VMIN	5
#define _VEOL	6
#define _VTIME	7
#define _VEOL2	8
#define _VSWTC	9

#define VINTR 	0
#define VQUIT 	1
#define VERASE 	2
#define VKILL	3
#define VEOF	4
#define VMIN	5
#define VEOL	6
#define VTIME	7
#define VEOL2	8
#define VSWTC	9

#define VWERASE 	10
#define VREPRINT	11
#define VSUSP 		12
#define VSTART		13
#define VSTOP		14
#define VLNEXT		15
#define VDISCARD	16


#ifdef __KERNEL__
/*	eof=^D		eol=\0		eol2=\0		erase=del
	werase=^W	kill=^U		reprint=^R	sxtc=\0
	intr=^C		quit=^\		susp=^Z		<OSF/1 VDSUSP>
	start=^Q	stop=^S		lnext=^V	discard=^U
	vmin=\1		vtime=\0
#define INIT_C_CC "\004\000\000\177\027\025\022\000\003\034\032\000\021\023\026\025\001\000"
*/

/*                   ^C  ^\ del  ^U  ^D   1   0   0   0   0  ^W  ^R  ^Z  ^Q  ^S  ^V  ^U  */
#define INIT_C_CC "\003\034\177\025\004\001\000\000\000\000\027\022\032\021\023\026\025" 
#endif

/* c_iflag bits */
#define IGNBRK	0000001
#define BRKINT	0000002
#define IGNPAR	0000004
#define PARMRK	0000010
#define INPCK	0000020
#define ISTRIP	0000040
#define INLCR	0000100
#define IGNCR	0000200
#define ICRNL	0000400
#define IXON	0001000
#define IXOFF	0002000
#define IXANY	0004000
#define IUCLC	0010000
#define IMAXBEL	0020000

/* c_oflag bits */
#define OPOST	0000001
#define ONLCR	0000002
#define OLCUC	0000004

#define OCRNL	0000010
#define ONOCR	0000020
#define ONLRET	0000040

#define OFILL	00000100
#define OFDEL	00000200
#define NLDLY	00001400
#define   NL0	00000000
#define   NL1	00000400
#define   NL2	00001000
#define   NL3	00001400
#define TABDLY	00006000
#define   TAB0	00000000
#define   TAB1	00002000
#define   TAB2	00004000
#define   TAB3	00006000
#define CRDLY	00030000
#define   CR0	00000000
#define   CR1	00010000
#define   CR2	00020000
#define   CR3	00030000
#define FFDLY	00040000
#define   FF0	00000000
#define   FF1	00040000
#define BSDLY	00100000
#define   BS0	00000000
#define   BS1	00100000
#define VTDLY	00200000
#define   VT0	00000000
#define   VT1	00200000
#define XTABS	01000000 /* Hmm.. Linux/i386 considers this part of TABDLY.. */

/* c_cflag bit meaning */
#define CBAUD	0000377
#define  B0	0000000		/* hang up */
#define  B50	0000001
#define  B75	0000002
#define  B110	0000003
#define  B134	0000004
#define  B150	0000005
#define  B200	0000006
#define  B300	0000007
#define  B600	0000010
#define  B1200	0000011
#define  B1800	0000012
#define  B2400	0000013
#define  B4800	0000014
#define  B9600	0000015
#define  B19200	0000016
#define  B38400	0000017
#define EXTA B19200
#define EXTB B38400
#define CBAUDEX 0000020
#define  B57600   00020
#define  B115200  00021
#define  B230400  00022
#define  B460800  00023

#define CSIZE	00001400
#define   CS5	00000000
#define   CS6	00000400
#define   CS7	00001000
#define   CS8	00001400

#define CSTOPB	00002000
#define CREAD	00004000
#define PARENB	00010000
#define PARODD	00020000
#define HUPCL	00040000

#define CLOCAL	00100000
#define CRTSCTS	  020000000000		/* flow control */

/* c_lflag bits */
#define ISIG	0x00000080
#define ICANON	0x00000100
#define XCASE	0x00004000
#define ECHO	0x00000008
#define ECHOE	0x00000002
#define ECHOK	0x00000004
#define ECHONL	0x00000010
#define NOFLSH	0x80000000
#define TOSTOP	0x00400000
#define ECHOCTL	0x00000040
#define ECHOPRT	0x00000020
#define ECHOKE	0x00000001
#define FLUSHO	0x00800000
#define PENDIN	0x20000000
#define IEXTEN	0x00000400

/* modem lines */
#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG

/* ioctl (fd, TIOCSERGETLSR, &result) where result may be as below */
#define TIOCSER_TEMT    0x01	/* Transmitter physically empty */


/* tcflow() and TCXONC use these */
#define	TCOOFF		0
#define	TCOON		1
#define	TCIOFF		2
#define	TCION		3

/* tcflush() and TCFLSH use these */
#define	TCIFLUSH	0
#define	TCOFLUSH	1
#define	TCIOFLUSH	2

/* tcsetattr uses these */
#define	TCSANOW		0
#define	TCSADRAIN	1
#define	TCSAFLUSH	2

/* line disciplines */
#define N_TTY		0
#define N_SLIP		1
#define N_MOUSE		2
#define N_PPP		3
#define N_AX25		5

#ifdef __KERNEL__

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
	if (1/*!(termios->c_lflag & ICANON)*/) {
		termio->c_cc[_VMIN]  = termios->c_cc[VMIN];
		termio->c_cc[_VTIME] = termios->c_cc[VTIME];
	}
}

#endif	/* __KERNEL__ */

#endif	/* _PPC_TERMIOS_H */

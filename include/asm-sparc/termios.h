/* $Id: termios.h,v 1.10 1995/11/25 02:33:01 davem Exp $ */
#ifndef _SPARC_TERMIOS_H
#define _SPARC_TERMIOS_H

#include <linux/types.h>

#include <asm/ioctl.h>

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

/* Big T */
#define TCGETA		_IOR('T', 1, struct termio)
#define TCSETA		_IOW('T', 2, struct termio)
#define TCSETAW		_IOW('T', 3, struct termio)
#define TCSETAF		_IOW('T', 4, struct termio)
#define TCSBRK		_IO('T', 5)
#define TCXONC		_IO('T', 6)
#define TCFLSH		_IO('T', 7)
#define TCGETS		_IOR('T', 8, struct termios)
#define TCSETS		_IOW('T', 9, struct termios)
#define TCSETSW		_IOW('T', 10, struct termios)
#define TCSETSF		_IOW('T', 11, struct termios)

/* SCARY Rutgers local SunOS kernel hackery, perhaps I will support it
 * someday.  This is completely bogus, I know...
 */
#define TCGETSTAT       _IO('T', 200) /* Rutgers specific */
#define TCSETSTAT       _IO('T', 201) /* Rutgers specific */

/* Little t */
#define TIOCGETD	_IOR('t', 0, int)
#define TIOCSETD	_IOW('t', 1, int)
#define TIOCHPCL        _IO('t', 2) /* SunOS Specific */
#define TIOCMODG        _IOR('t', 3, int) /* SunOS Specific */
#define TIOCMODS        _IOW('t', 4, int) /* SunOS Specific */
#define TIOCGETP        _IOR('t', 8, struct sgttyb) /* SunOS Specific */
#define TIOCSETP        _IOW('t', 9, struct sgttyb) /* SunOS Specific */
#define TIOCSETN        _IOW('t', 10, struct sgttyb) /* SunOS Specific */
#define TIOCEXCL	_IO('t', 13)
#define TIOCNXCL	_IO('t', 14)
#define TIOCFLUSH       _IOW('t', 16, int) /* SunOS Specific */
#define TIOCSETC        _IOW('t', 17, struct tchars) /* SunOS Specific */
#define TIOCGETC        _IOR('t', 18, struct tchars) /* SunOS Specific */
#define TIOCTCNTL       _IOW('t', 32, int) /* SunOS Specific */
#define TIOCSIGNAL      _IOW('t', 33, int) /* SunOS Specific */
#define TIOCSETX        _IOW('t', 34, int) /* SunOS Specific */
#define TIOCGETX        _IOR('t', 35, int) /* SunOS Specific */
#define TIOCCONS	_IO('t', 36)
#define TIOCSSIZE       _IOW('t', 37, struct sunos_ttysize) /* SunOS Specific */
#define TIOCGSIZE       _IOR('t', 38, struct sunos_ttysize) /* SunOS Specific */
#define TIOCGSOFTCAR	_IOR('t', 100, int)
#define TIOCSSOFTCAR	_IOW('t', 101, int)
#define TIOCUCNTL       _IOW('t', 102, int) /* SunOS Specific */
#define TIOCSWINSZ	_IOW('t', 103, struct winsize)
#define TIOCGWINSZ	_IOR('t', 104, struct winsize)
#define TIOCREMOTE      _IOW('t', 105, int) /* SunOS Specific */
#define TIOCMGET	_IOR('t', 106, int)
#define TIOCMBIC	_IOW('t', 107, int)
#define TIOCMBIS	_IOW('t', 108, int)
#define TIOCMSET	_IOW('t', 109, int)
#define TIOCSTART       _IO('t', 110) /* SunOS Specific */
#define TIOCSTOP        _IO('t', 111) /* SunOS Specific */
#define TIOCPKT		_IOW('t', 112, int)
#define TIOCNOTTY	_IO('t', 113)
#define TIOCSTI		_IOW('t', 114, char)
#define TIOCOUTQ	_IOR('t', 115, int)
#define TIOCGLTC        _IOR('t', 116, struct ltchars) /* SunOS Specific */
#define TIOCSLTC        _IOW('t', 117, struct ltchars) /* SunOS Specific */
/* 118 is the non-posix setpgrp tty ioctl */
/* 119 is the non-posix getpgrp tty ioctl */
#define TIOCCDTR        _IO('t', 120) /* SunOS Specific */
#define TIOCSDTR        _IO('t', 121) /* SunOS Specific */
#define TIOCCBRK        _IO('t', 122) /* SunOS Specific */
#define TIOCSBRK        _IO('t', 123) /* SunOS Specific */
#define TIOCLGET        _IOW('t', 124, int) /* SunOS Specific */
#define TIOCLSET        _IOW('t', 125, int) /* SunOS Specific */
#define TIOCLBIC        _IOW('t', 126, int) /* SunOS Specific */
#define TIOCLBIS        _IOW('t', 127, int) /* SunOS Specific */
#define TIOCISPACE      _IOR('t', 128, int) /* SunOS Specific */
#define TIOCISIZE       _IOR('t', 129, int) /* SunOS Specific */
#define TIOCSPGRP	_IOW('t', 130, int)
#define TIOCGPGRP	_IOR('t', 131, int)
#define TIOCSCTTY	_IO('t', 132)

/* Little f */
#define FIOCLEX		_IO('f', 1)
#define FIONCLEX	_IO('f', 2)
#define FIOASYNC	_IOW('f', 125, int)
#define FIONBIO		_IOW('f', 126, int)
#define FIONREAD	_IOR('f', 127, int)
#define TIOCINQ		FIONREAD

/* Linux specific, no SunOS equivalent. */
#define TIOCLINUX	0x541C
#define TIOCGSERIAL	0x541E
#define TIOCSSERIAL	0x541F
#define TCSBRKP		0x5425
#define TIOCTTYGSTRUCT	0x5426
#define TIOCSERCONFIG	0x5453
#define TIOCSERGWILD	0x5454
#define TIOCSERSWILD	0x5455
#define TIOCGLCKTRMIOS	0x5456
#define TIOCSLCKTRMIOS	0x5457
#define TIOCSERGSTRUCT	0x5458 /* For debugging only */
#define TIOCSERGETLSR   0x5459 /* Get line status register */
#define TIOCSERGETMULTI 0x545A /* Get multiport config  */
#define TIOCSERSETMULTI 0x545B /* Set multiport config */

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

#define NCC 8
struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[NCC];	/* control characters */
};

#define NCCS 17
struct termios {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_line;			/* line discipline */
	cc_t c_cc[NCCS];		/* control characters */
};

/* c_cc characters */
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VEOL     5
#define VEOL2    6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VDSUSP   11  /* SunOS POSIX nicety I do believe... */
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VMIN     VEOF
#define VTIME    VEOL

#ifdef __KERNEL__
/*	intr=^C		quit=^|		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"
#endif

/* c_iflag bits */
#define IGNBRK	0x00000001
#define BRKINT	0x00000002
#define IGNPAR	0x00000004
#define PARMRK	0x00000008
#define INPCK	0x00000010
#define ISTRIP	0x00000020
#define INLCR	0x00000040
#define IGNCR	0x00000080
#define ICRNL	0x00000100
#define IUCLC	0x00000200
#define IXON	0x00000400
#define IXANY	0x00000800
#define IXOFF	0x00001000
#define IMAXBEL	0x00002000

/* c_oflag bits */
#define OPOST	0x00000001
#define OLCUC	0x00000002
#define ONLCR	0x00000004
#define OCRNL	0x00000008
#define ONOCR	0x00000010
#define ONLRET	0x00000020
#define OFILL	0x00000040
#define OFDEL	0x00000080
#define NLDLY	0x00000100
#define   NL0	0x00000000
#define   NL1	0x00000100
#define CRDLY	0x00000600
#define   CR0	0x00000000
#define   CR1	0x00000200
#define   CR2	0x00000400
#define   CR3	0x00000600
#define TABDLY	0x00001800
#define   TAB0	0x00000000
#define   TAB1	0x00000800
#define   TAB2	0x00001000
#define   TAB3	0x00001800
#define   XTABS	0x00001800
#define BSDLY	0x00002000
#define   BS0	0x00000000
#define   BS1	0x00002000
#define VTDLY	0x00004000
#define   VT0	0x00000000
#define   VT1	0x00004000
#define FFDLY	0x00008000
#define   FF0	0x00000000
#define   FF1	0x00008000
#define PAGEOUT 0x00010000  /* SUNOS specific */
#define WRAP    0x00020000  /* SUNOS specific */

/* c_cflag bit meaning */
#define CBAUD	0x0000000f
#define  B0	0x00000000   /* hang up */
#define  B50	0x00000001
#define  B75	0x00000002
#define  B110	0x00000003
#define  B134	0x00000004
#define  B150	0x00000005
#define  B200	0x00000006
#define  B300	0x00000007
#define  B600	0x00000008
#define  B1200	0x00000009
#define  B1800	0x0000000a
#define  B2400	0x0000000b
#define  B4800	0x0000000c
#define  B9600	0x0000000d
#define  B19200	0x0000000e
#define  B38400	0x0000000f
#define EXTA    B19200
#define EXTB    B38400
#define  CSIZE  0x00000030
#define   CS5	0x00000000
#define   CS6	0x00000010
#define   CS7	0x00000020
#define   CS8	0x00000030
#define CSTOPB	0x00000040
#define CREAD	0x00000080
#define PARENB	0x00000100
#define PARODD	0x00000200
#define HUPCL	0x00000400
#define CLOCAL	0x00000800
/* We'll never see these speeds with the Zilogs' but for completeness... */
#define CBAUDEX 0x00010000
#define  B57600  0x00010001
#define  B115200 0x00010002
#define  B230400 0x00010003
#define CIBAUD	  0x000f0000  /* input baud rate (not used) */
#define CRTSCTS	  0x80000000  /* flow control */

/* c_lflag bits */
#define ISIG	0x00000001
#define ICANON	0x00000002
#define XCASE	0x00000004
#define ECHO	0x00000008
#define ECHOE	0x00000010
#define ECHOK	0x00000020
#define ECHONL	0x00000040
#define NOFLSH	0x00000080
#define TOSTOP	0x00000100
#define ECHOCTL	0x00000200
#define ECHOPRT	0x00000400
#define ECHOKE	0x00000800
#define DEFECHO 0x00001000  /* SUNOS thing, what is it? */
#define FLUSHO	0x00002000
#define PENDIN	0x00004000
#define IEXTEN	0x00008000

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

#ifndef __ASM_MIPS_IOCTLS_H
#define __ASM_MIPS_IOCTLS_H

#include <asm/ioctl.h>

#define TCGETA		0x5401
#define TCSETA		0x5402
#define TCSETAW		0x5403
#define TCSETAF		0x5404

#define TCSBRK		0x5405
#define TCXONC		0x5406
#define TCFLSH		0x5407

#define TCGETS		0x540d
#define TCSETS		0x540e
#define TCSETSW		0x540f
#define TCSETSF		0x5410

#define TIOCEXCL	0x740d		/* set exclusive use of tty */
#define TIOCNXCL	0x740e		/* reset exclusive use of tty */
#define TIOCOUTQ	0x7472		/* output queue size */
#define TIOCSTI		0x5472		/* simulate terminal input */
#define TIOCMGET	0x741d		/* get all modem bits */
#define TIOCMBIS	0x741b		/* bis modem bits */
#define TIOCMBIC	0x741c		/* bic modem bits */
#define TIOCMSET	0x741a		/* set all modem bits */
#define TIOCPKT		0x5470		/* pty: set/clear packet mode */
#define		TIOCPKT_DATA		0x00	/* data packet */
#define		TIOCPKT_FLUSHREAD	0x01	/* flush packet */
#define		TIOCPKT_FLUSHWRITE	0x02	/* flush packet */
#define		TIOCPKT_STOP		0x04	/* stop output */
#define		TIOCPKT_START		0x08	/* start output */
#define		TIOCPKT_NOSTOP		0x10	/* no more ^S, ^Q */
#define		TIOCPKT_DOSTOP		0x20	/* now do ^S ^Q */
#if 0
#define		TIOCPKT_IOCTL		0x40	/* state change of pty driver */
#endif
#define TIOCSWINSZ	_IOW('t', 103, struct winsize)	/* set window size */
#define TIOCGWINSZ	_IOR('t', 104, struct winsize)	/* get window size */
#define TIOCNOTTY	0x5471		/* void tty association */
#define TIOCSETD	0x7401
#define TIOCGETD	0x7400

#define FIOCLEX		0x6601
#define FIONCLEX	0x6602		/* these numbers need to be adjusted. */
#define FIOASYNC	0x667d
#define FIONBIO		0x667e

						/* 116-117 compat */
#define TIOCSPGRP	_IOW('t', 118, int)	/* set pgrp of tty */
#define TIOCGPGRP	_IOR('t', 119, int)	/* get pgrp of tty */
#define TIOCCONS	_IOW('t', 120, int)	/* become virtual console */

#define FIONREAD	0x467f
#define TIOCINQ		FIONREAD

#if 0
#define	TIOCSETA	_IOW('t', 20, struct termios) /* set termios struct */
#define	TIOCSETAW	_IOW('t', 21, struct termios) /* drain output, set */
#define	TIOCSETAF	_IOW('t', 22, struct termios) /* drn out, fls in, set */
#define	TIOCGETD	_IOR('t', 26, int)	/* get line discipline */
#define	TIOCSETD	_IOW('t', 27, int)	/* set line discipline */
						/* 127-124 compat */
#endif

/* I hope the range from 0x5480 on is free ... */
#define TIOCSCTTY	0x5480		/* become controlling tty */
#define TIOCGSOFTCAR	0x5481
#define TIOCSSOFTCAR	0x5482
#define TIOCLINUX	0x5483
#define TIOCGSERIAL	0x5484
#define TIOCSSERIAL	0x5485

#define TCSBRKP		0x5486	/* Needed for POSIX tcsendbreak() */
#define TIOCTTYGSTRUCT	0x5487  /* For debugging only */

#define TIOCSERCONFIG	0x5488
#define TIOCSERGWILD	0x5489
#define TIOCSERSWILD	0x548a
#define TIOCGLCKTRMIOS	0x548b
#define TIOCSLCKTRMIOS	0x548c
#define TIOCSERGSTRUCT	0x548d /* For debugging only */
#define TIOCSERGETLSR   0x548e /* Get line status register */
#define TIOCSERGETMULTI 0x548f /* Get multiport config  */
#define TIOCSERSETMULTI 0x5490 /* Set multiport config */

/* ----------------------------------------------------------------------- */

/* c_cc characters */
#define VINTR		 0		/* Interrupt character [ISIG].  */
#define VQUIT		 1		/* Quit character [ISIG].  */
#define VERASE		 2		/* Erase character [ICANON].  */
#define VKILL		 3		/* Kill-line character [ICANON].  */
#define VEOF		 4		/* End-of-file character [ICANON].  */
#define VMIN		VEOF		/* Minimum number of bytes read at once [!ICANON].  */
#define VEOL		 5		/* End-of-line character [ICANON].  */
#define VTIME		VEOL		/* Time-out value (tenths of a second) [!ICANON].  */
#if defined (__USE_BSD) || defined (__KERNEL__)
#define VEOL2		 6		/* Second EOL character [ICANON].  */
/* The next two are guesses ... */
#define VSWTC		 7		/* ??? */
#endif
#define VSWTCH		VSWTC
#define VSTART		 8		/* Start (X-ON) character [IXON, IXOFF].  */
#define VSTOP		 9		/* Stop (X-OFF) character [IXON, IXOFF].  */
#define VSUSP		10		/* Suspend character [ISIG].  */
#if 0
/*
 * VDSUSP is not supported
 */
#if defined (__USE_BSD) || defined (__KERNEL__)
#define VDSUSP		11		/* Delayed suspend character [ISIG].  */
#endif
#endif
#if defined (__USE_BSD) || defined (__KERNEL__)
#define VREPRINT	12		/* Reprint-line character [ICANON].  */
#endif
#if defined (__USE_BSD) || defined (__KERNEL__)
#define VDISCARD	13		/* Discard character [IEXTEN].  */
#define VWERASE		14		/* Word-erase character [ICANON].  */
#define VLNEXT		15		/* Literal-next character [IEXTEN].  */
#endif
/*
 * 17 - 19 are reserved
 */

#ifdef __KERNEL__
/*
 *	intr=^C		quit=^|		erase=del	kill=^U
 *	eof=^D		eol=time=\0	eol2=\0		swtc=\0
 *	start=^Q	stop=^S		susp=^Z		vdsusp=
 *	reprint=^R	discard=^U	werase=^W	lnext=^V
 */
#define INIT_C_CC "\003\034\177\025\004\0\0\0\021\023\032\0\022\017\027\026"
#endif

/* c_iflag bits */
#define IGNBRK	0000001		/* Ignore break condition.  */
#define BRKINT	0000002		/* Signal interrupt on break.  */
#define IGNPAR	0000004		/* Ignore characters with parity errors.  */
#define PARMRK	0000010		/* Mark parity and framing errors.  */
#define INPCK	0000020		/* Enable input parity check.  */
#define ISTRIP	0000040		/* Strip 8th bit off characters.  */
#define INLCR	0000100		/* Map NL to CR on input.  */
#define IGNCR	0000200		/* Ignore CR.  */
#define ICRNL	0000400		/* Map CR to NL on input.  */
#if defined (__USE_BSD) || defined (__KERNEL__)
#define IUCLC	0001000		/* Map upper case to lower case on input.  */
#endif
#define IXON	0002000		/* Enable start/stop output control.  */
#if defined (__USE_BSD) || defined (__KERNEL__)
#define IXANY	0004000		/* Any character will restart after stop.  */
#endif
#define IXOFF	0010000		/* Enable start/stop input control.  */
#if defined (__USE_BSD) || defined (__KERNEL__)
#define IMAXBEL	0020000		/* Ring bell when input queue is full.  */
#endif

/* c_oflag bits */
#define OPOST	0000001		/* Perform output processing.  */
#if defined (__USE_BSD) || defined (__KERNEL__)
#define OLCUC	0000002		/* Map lower case to upper case on output.  */
#define ONLCR	0000004		/* Map NL to CR-NL on output.  */
#define OCRNL	0000010
#define ONOCR	0000020
#define ONLRET	0000040
#define OFILL	0000100
#define OFDEL	0000200
#define NLDLY	0000400
#define   NL0	0000000
#define   NL1	0000400
#define CRDLY	0003000
#define   CR0	0000000
#define   CR1	0001000
#define   CR2	0002000
#define   CR3	0003000
#define TABDLY	0014000
#define   TAB0	0000000
#define   TAB1	0004000
#define   TAB2	0010000
#define   TAB3	0014000
#define   XTABS	0014000
#define BSDLY	0020000
#define   BS0	0000000
#define   BS1	0020000
#define VTDLY	0040000
#define   VT0	0000000
#define   VT1	0040000
#define FFDLY	0100000
#define   FF0	0000000
#define   FF1	0100000
/*
#define PAGEOUT ???
#define WRAP    ???
 */
#endif

/* c_cflag bit meaning */
#define CBAUD	0010017
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
#define CSIZE	0000060		/* Number of bits per byte (mask).  */
#define   CS5	0000000		/* 5 bits per byte.  */
#define   CS6	0000020		/* 6 bits per byte.  */
#define   CS7	0000040		/* 7 bits per byte.  */
#define   CS8	0000060		/* 8 bits per byte.  */
#define CSTOPB	0000100		/* Two stop bits instead of one.  */
#define CREAD	0000200		/* Enable receiver.  */
#define PARENB	0000400		/* Parity enable.  */
#define PARODD	0001000		/* Odd parity instead of even.  */
#define HUPCL	0002000		/* Hang up on last close.  */
#define CLOCAL	0004000		/* Ignore modem status lines.  */
#if defined (__USE_BSD) || defined (__KERNEL__)
#define CBAUDEX 0010000
#define  B57600  0010001
#define  B115200 0010002
#define  B230400 0010003
#define  B460800 0010004
#define CIBAUD	  002003600000	/* input baud rate (not used) */
#define CRTSCTS	  020000000000		/* flow control */
#endif

/* c_lflag bits */
#define ISIG	0000001		/* Enable signals.  */
#define ICANON	0000002		/* Do erase and kill processing.  */
#define XCASE	0000004
#define ECHO	0000010		/* Enable echo.  */
#define ECHOE	0000020		/* Visual erase for ERASE.  */
#define ECHOK	0000040		/* Echo NL after KILL.  */
#define ECHONL	0000100		/* Echo NL even if ECHO is off.  */
#define NOFLSH	0000200		/* Disable flush after interrupt.  */
#define IEXTEN	0000400		/* Enable DISCARD and LNEXT.  */
#if defined (__USE_BSD) || defined (__KERNEL__)
#define ECHOCTL	0001000		/* Echo control characters as ^X.  */
#define ECHOPRT	0002000		/* Hardcopy visual erase.  */
#define ECHOKE	0004000		/* Visual erase for KILL.  */
#endif
#define FLUSHO	0020000
#if defined (__USE_BSD) || defined (__KERNEL__)
#define PENDIN	0040000		/* Retype pending input (state).  */
#endif
#define TOSTOP	0100000		/* Send SIGTTOU for background output.  */
#define ITOSTOP	TOSTOP

/* modem lines */
#define TIOCM_LE	0x001		/* line enable */
#define TIOCM_DTR	0x002		/* data terminal ready */
#define TIOCM_RTS	0x004		/* request to send */
#define TIOCM_ST	0x010		/* secondary transmit */
#define TIOCM_SR	0x020		/* secondary receive */
#define TIOCM_CTS	0x040		/* clear to send */
#define TIOCM_CAR	0x100		/* carrier detect */
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RNG	0x200		/* ring */
#define TIOCM_RI	TIOCM_RNG
#define TIOCM_DSR	0x400		/* data set ready */

/* ioctl (fd, TIOCSERGETLSR, &result) where result may be as below */
#define TIOCSER_TEMT    0x01	/* Transmitter physically empty */

/* tcflow() and TCXONC use these */
#define	TCOOFF		0	/* Suspend output.  */
#define	TCOON		1	/* Restart suspended output.  */
#define	TCIOFF		2	/* Send a STOP character.  */
#define	TCION		3	/* Send a START character.  */

/* tcflush() and TCFLSH use these */
#define	TCIFLUSH	0	/* Discard data received but not yet read.  */
#define	TCOFLUSH	1	/* Discard data written but not yet sent.  */
#define	TCIOFLUSH	2	/* Discard all pending data.  */

/* tcsetattr uses these */
#define	TCSANOW		TCSETS	/* Change immediately.  */
#define	TCSADRAIN	TCSETSW	/* Change when pending output is written.  */
#define	TCSAFLUSH	TCSETSF	/* Flush pending input before changing.  */

/* line disciplines */
#define N_TTY		0
#define N_SLIP		1
#define N_MOUSE		2
#define N_PPP		3

#endif /* __ASM_MIPS_IOCTLS_H */

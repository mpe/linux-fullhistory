#ifndef _ASM_SPARC_IOCTLS_H
#define _ASM_SPARC_IOCTLS_H

#include <asm/ioctl.h>

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

#endif /* !(_ASM_SPARC_IOCTLS_H) */

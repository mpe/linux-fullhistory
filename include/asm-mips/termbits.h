#ifndef __ASM_MIPS_TERMBITS_H
#define __ASM_MIPS_TERMBITS_H

#include <asm/ioctl.h>
#include <asm/ioctls.h>

/*
 * The ABI says nothing about NCC but seems to use NCCS as
 * replacement for it in struct termio
 */
#define NCC	8
#define NCCS	23

struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	char c_line;			/* line discipline */
	unsigned char c_cc[NCCS];	/* control characters */
};

struct termios {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	/*
	 * Seems nonexistent in the ABI, but Linux assumes existence ...
	 */
	cc_t c_line;			/* line discipline */
	cc_t c_cc[NCCS];		/* control characters */
};

#endif /* __ASM_MIPS_TERMBITS_H */

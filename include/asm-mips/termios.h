#ifndef __ASM_MIPS_TERMIOS_H
#define __ASM_MIPS_TERMIOS_H

#include <linux/types.h>
#include <asm/termbits.h>

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

/* ----------------------------------------------------------------------- */

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

#endif /* __ASM_MIPS_TERMIOS_H */

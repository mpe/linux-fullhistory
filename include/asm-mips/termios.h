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
#define user_termio_to_kernel_termios(termios, termio) \
do { \
	unsigned short tmp; \
	get_user(tmp, &(termio)->c_iflag); \
	(termios)->c_iflag = (0xffff0000 & ((termios)->c_iflag)) | tmp; \
	get_user(tmp, &(termio)->c_oflag); \
	(termios)->c_oflag = (0xffff0000 & ((termios)->c_oflag)) | tmp; \
	get_user(tmp, &(termio)->c_cflag); \
	(termios)->c_cflag = (0xffff0000 & ((termios)->c_cflag)) | tmp; \
	get_user(tmp, &(termio)->c_lflag); \
	(termios)->c_lflag = (0xffff0000 & ((termios)->c_lflag)) | tmp; \
	get_user((termios)->c_line, &(termio)->c_line); \
	copy_from_user((termios)->c_cc, (termio)->c_cc, NCC); \
} while(0)

/*
 * Translate a "termios" structure into a "termio". Ugh.
 */
#define kernel_termios_to_user_termio(termio, termios) \
do { \
	put_user((termios)->c_iflag, &(termio)->c_iflag); \
	put_user((termios)->c_oflag, &(termio)->c_oflag); \
	put_user((termios)->c_cflag, &(termio)->c_cflag); \
	put_user((termios)->c_lflag, &(termio)->c_lflag); \
	put_user((termios)->c_line, &(termio)->c_line); \
	copy_to_user((termio)->c_cc, (termios)->c_cc, NCC); \
} while(0)

#define user_termios_to_kernel_termios(k, u) copy_from_user(k, u, sizeof(struct termios))
#define kernel_termios_to_user_termios(u, k) copy_to_user(u, k, sizeof(struct termios))

#endif	/* __KERNEL__ */

#endif /* __ASM_MIPS_TERMIOS_H */

/* $Id: sunkbd.h,v 1.1 1997/08/28 02:23:34 ecd Exp $
 * sunkbd.h: Defines needed by SUN Keyboard drivers
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _SPARC_SUNKBD_H
#define _SPARC_SUNKBD_H 1

/* Keyboard defines for L1-A processing... */
#define SUNKBD_RESET		0xff
#define SUNKBD_L1		0x01
#define SUNKBD_UP		0x80
#define SUNKBD_A		0x4d

struct l1a_kbd_state {
	int kbd_id;
	int l1_down;
};

extern struct l1a_kbd_state l1a_state;

extern void keyboard_zsinit(void (*kbd_put_char)(unsigned char));
extern void sunkbd_inchar(unsigned char, struct pt_regs *);
extern void batten_down_hatches(void);

#endif /* !(_SPARC_SUNKBD_H) */

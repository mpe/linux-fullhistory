/* $Id: sunserial.h,v 1.17 1997/12/19 07:33:12 ecd Exp $
 * sunserial.h: SUN serial driver infrastructure (including keyboards).
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _SPARC_SUNSERIAL_H
#define _SPARC_SUNSERIAL_H 1

#include <linux/config.h>
#include <linux/tty.h>
#include <linux/kd.h>
#include <linux/kbd_kern.h>
#include <linux/console.h>

struct initfunc {
	int		(*init) (void);
	struct initfunc *next;
};

struct sunserial_operations {
	struct initfunc	*rs_init;
	void		(*rs_kgdb_hook) (int);
	void		(*rs_change_mouse_baud) (int);
	int		(*rs_read_proc) (char *, char **, off_t, int, int *, void *);
};

struct sunkbd_operations {
	struct initfunc	*kbd_init;
	void		(*compute_shiftstate) (void);
	void		(*setledstate) (struct kbd_struct *, unsigned int);
	unsigned char	(*getledstate) (void);
	int		(*setkeycode) (unsigned int, unsigned int);
	int		(*getkeycode) (unsigned int);
};

extern struct sunserial_operations rs_ops;
extern struct sunkbd_operations kbd_ops;

extern void sunserial_setinitfunc(unsigned long *, int (*) (void));
extern void sunkbd_setinitfunc(unsigned long *, int (*) (void));

extern int serial_console;
extern void sunserial_console_termios(struct console *);

#ifdef CONFIG_PCI
extern void sunkbd_install_keymaps(unsigned long *, ushort **, unsigned int, char *,
				   char **, int, int, struct kbdiacr *, unsigned int);
#endif

#endif /* !(_SPARC_SUNSERIAL_H) */

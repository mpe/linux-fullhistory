/* $Id: sunserial.h,v 1.13 1997/09/03 11:55:00 ecd Exp $
 * sunserial.h: SUN serial driver infrastructure.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _SPARC_SUNSERIAL_H
#define _SPARC_SUNSERIAL_H 1

#include <linux/tty.h>

struct rs_initfunc {
	int	(*rs_init) (void);
	struct rs_initfunc *next;
};

struct sunserial_operations {
	struct rs_initfunc	*rs_init;
	void	(*rs_cons_hook) (int, int, int);
	void	(*rs_kgdb_hook) (int);
	void	(*rs_change_mouse_baud) (int);
	int	(*rs_read_proc) (char *, char **, off_t, int, int *, void *);
};

/*
 * XXX: Work in progress, don't worry this will go away in a few days. (ecd)
 * 
 * To support multiple keyboards in one binary we have to take care
 * about (at least) the following:
 * 
 * int	shift_state;
 * 
 * char	*func_buf;
 * char	*func_bufptr;
 * int	 funcbufsize;
 * int	 funcbufleft;
 * char	**func_table;
 * 
 * XXX: keymaps need to be handled...
 * 
 * struct kbd_struct	*kbd_table;
 * int	(*kbd_init)(void);
 */

extern struct sunserial_operations rs_ops;
extern void sunserial_setinitfunc(unsigned long *, int (*) (void));

#endif /* !(_SPARC_SUNSERIAL_H) */

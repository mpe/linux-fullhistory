/*
 *  linux/include/linux/console.h
 *
 *  Copyright (C) 1993        Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Changed:
 * 10-Mar-94: Arno Griffioen: Conversion for vt100 emulator port from PC LINUX
 */

#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#define NPAR 16

struct vc_data;

/*
 * this is what the terminal answers to a ESC-Z or csi0c query.
 */
#define VT100ID "\033[?1;2c"
#define VT102ID "\033[?6c"

/* DPC: 1994-04-13 !!! con_putcs is new entry !!! */

struct consw {
	unsigned long (*con_startup)(unsigned long, const char **);
	void   (*con_init)(struct vc_data *);
	int    (*con_deinit)(struct vc_data *);
	int    (*con_clear)(struct vc_data *, int, int, int, int);
	int    (*con_putc)(struct vc_data *, int, int, int);
	int    (*con_putcs)(struct vc_data *, const char *, int, int, int);
	int    (*con_cursor)(struct vc_data *, int);
	int    (*con_scroll)(struct vc_data *, int, int, int, int);
	int    (*con_bmove)(struct vc_data *, int, int, int, int, int, int);
	int    (*con_switch)(struct vc_data *);
	int    (*con_blank)(int);
	int    (*con_get_font)(struct vc_data *, int *, int *, char *);
	int    (*con_set_font)(struct vc_data *, int, int, char *);
	int    (*con_set_palette)(struct vc_data *, unsigned char *);
};

extern struct consw *conswitchp;

/* flag bits */
#define CON_INITED  (1)

/* scroll */
#define SM_UP       (1)
#define SM_DOWN     (2)
#define SM_LEFT     (3)
#define SM_RIGHT    (4)

/* cursor */
#define CM_DRAW     (1)
#define CM_ERASE    (2)
#define CM_MOVE     (3)

struct tty_struct;
int tioclinux(struct tty_struct *tty, unsigned long arg);

/*
 *	Array of consoles built from command line options (console=)
 */
struct console_cmdline
{
	char	name[8];			/* Name of the driver	    */
	int	index;				/* Minor dev. to use	    */
	char	*options;			/* Options for the driver   */
};
#define MAX_CMDLINECONSOLES 8
extern struct console_cmdline console_list[MAX_CMDLINECONSOLES];

/*
 *	The interface for a console, or any other device that
 *	wants to capture console messages (printer driver?)
 */

#define CON_PRINTBUFFER	(1)
#define CON_FIRST	(2)
#define CON_ENABLED	(4)

struct console
{
	char	name[8];
	void	(*write)(struct console *, const char *, unsigned);
	int	(*read)(struct console *, const char *, unsigned);
	kdev_t	(*device)(struct console *);
	int	(*wait_key)(struct console *);
	void	(*unblank)(void);
	int	(*setup)(struct console *, char *);
	short	flags;
	short	index;
	int	cflag;
	struct	 console *next;
};

extern void register_console(struct console *);
extern int unregister_console(struct console *);
extern struct console *console_drivers;

#endif /* linux/console.h */

/* $Id: misc.c,v 1.4 1997/03/04 16:27:11 jj Exp $
 * misc.c:  Miscellaneous prom functions that don't belong
 *          anywhere else.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/openprom.h>
#include <asm/oplib.h>

/* Reset and reboot the machine with the command 'bcommand'. */
void
prom_reboot(char *bcommand)
{
	p1275_cmd ("boot", P1275_ARG(0,P1275_ARG_IN_STRING)|
		           P1275_INOUT(1,0), bcommand);
}

/* Forth evaluate the expression contained in 'fstring'. */
void
prom_feval(char *fstring)
{
	if(!fstring || fstring[0] == 0)
		return;
	p1275_cmd ("interpret", P1275_ARG(0,P1275_ARG_IN_STRING)|
				P1275_INOUT(1,1), fstring);
}

/* We want to do this more nicely some day. */
#ifdef CONFIG_SUN_CONSOLE
extern void console_restore_palette(void);
extern void set_palette(void);
extern int serial_console;
#endif

/* Drop into the prom, with the chance to continue with the 'go'
 * prom command.
 */
void
prom_cmdline(void)
{
	extern void kernel_enter_debugger(void);
	extern void install_obp_ticker(void);
	extern void install_linux_ticker(void);
	unsigned long flags;
    
	kernel_enter_debugger();
#ifdef CONFIG_SUN_CONSOLE
	if(!serial_console)
		console_restore_palette ();
#endif
	install_obp_ticker();
	save_flags(flags); cli();
	p1275_cmd ("enter", P1275_INOUT(0,0));
	restore_flags(flags);
	install_linux_ticker();
#ifdef CONFIG_SUN_CONSOLE
	if(!serial_console)
		set_palette ();
#endif
}

/* Drop into the prom, but completely terminate the program.
 * No chance of continuing.
 */
void
prom_halt(void)
{
	p1275_cmd ("exit", P1275_INOUT(0,0));
}

/* Set prom sync handler to call function 'funcp'. */
void
prom_setsync(sync_func_t funcp)
{
	if(!funcp) return;
	p1275_cmd ("set-callback", P1275_ARG(0,P1275_ARG_IN_FUNCTION)|
				   P1275_INOUT(1,1), funcp);
}

/* Get the idprom and stuff it into buffer 'idbuf'.  Returns the
 * format type.  'num_bytes' is the number of bytes that your idbuf
 * has space for.  Returns 0xff on error.
 */
unsigned char
prom_get_idprom(char *idbuf, int num_bytes)
{
	int len;

	len = prom_getproplen(prom_root_node, "idprom");
	if((len>num_bytes) || (len==-1)) return 0xff;
	if(!prom_getproperty(prom_root_node, "idprom", idbuf, num_bytes))
		return idbuf[0];

	return 0xff;
}

/* Get the major prom version number. */
int
prom_version(void)
{
	return PROM_P1275;
}

/* Get the prom plugin-revision. */
int
prom_getrev(void)
{
	return prom_rev;
}

/* Get the prom firmware print revision. */
int
prom_getprev(void)
{
	return prom_prev;
}

/* $Id: misc.c,v 1.8 1996/04/17 23:03:23 davem Exp $
 * misc.c:  Miscellaneous prom functions that don't belong
 *          anywhere else.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <asm/openprom.h>
#include <asm/oplib.h>

/* Reset and reboot the machine with the command 'bcommand'. */
void
prom_reboot(char *bcommand)
{
	(*(romvec->pv_reboot))(bcommand);
	/* Never get here. */
	return;
}

/* Forth evaluate the expression contained in 'fstring'. */
void
prom_feval(char *fstring)
{
	if(!fstring || fstring[0] == 0) return;
	if(prom_vers == PROM_V0)
		(*(romvec->pv_fortheval.v0_eval))(strlen(fstring), fstring);
	else
		(*(romvec->pv_fortheval.v2_eval))(fstring);
	return;
}

/* We want to do this more nicely some day. */
#if CONFIG_SUN_CONSOLE
extern void console_restore_palette(void);
extern void set_palette(void);
extern int serial_console;
#endif

/* Drop into the prom, with the chance to continue with the 'go'
 * prom command.
 */
void
prom_halt(void)
{
	extern void kernel_enter_debugger(void);
	extern void install_obp_ticker(void);
	extern void install_linux_ticker(void);
    
	kernel_enter_debugger();
#if CONFIG_SUN_CONSOLE
	if(!serial_console)
		console_restore_palette ();
#endif
	install_obp_ticker();
	(*(romvec->pv_abort))();
	install_linux_ticker();
#if CONFIG_SUN_CONSOLE
	if(!serial_console)
		set_palette ();
#endif
	return;
}

/* Drop into the prom, but completely terminate the program.
 * No chance of continuing.
 */
void
prom_die(void)
{
	(*(romvec->pv_halt))();
	/* Never get here. */
	return;
}

typedef void (*sfunc_t)(void);

/* Set prom sync handler to call function 'funcp'. */
void
prom_setsync(sfunc_t funcp)
{
#if CONFIG_AP1000
  printk("not doing setsync\n");
  return;
#endif
	if(!funcp) return;
	*romvec->pv_synchook = funcp;
	return;
}

/* Get the idprom and stuff it into buffer 'idbuf'.  Returns the
 * format type.  'num_bytes' is the number of bytes that your idbuf
 * has space for.  Returns 0xff on error.
 */
unsigned char
prom_getidp(char *idbuf, int num_bytes)
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
	return romvec->pv_romvers;
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

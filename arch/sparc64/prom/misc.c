/* $Id: misc.c,v 1.13 1998/10/13 14:03:49 davem Exp $
 * misc.c:  Miscellaneous prom functions that don't belong
 *          anywhere else.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
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
extern void (*prom_palette)(int);
extern int serial_console;
#endif

/* Drop into the prom, with the chance to continue with the 'go'
 * prom command.
 */
void
prom_cmdline(void)
{
	unsigned long flags;
    
#ifdef CONFIG_SUN_CONSOLE
	if(!serial_console && prom_palette)
		prom_palette (1);
#endif
	__save_and_cli(flags);
	p1275_cmd ("enter", P1275_INOUT(0,0));
	__restore_flags(flags);
#ifdef CONFIG_SUN_CONSOLE
	if(!serial_console && prom_palette)
		prom_palette (0);
#endif
}

/* Drop into the prom, but completely terminate the program.
 * No chance of continuing.
 */
void
prom_halt(void)
{
again:
	p1275_cmd ("exit", P1275_INOUT(0,0));
	goto again; /* PROM is out to get me -DaveM */
}

/* Set prom sync handler to call function 'funcp'. */
void
prom_setcallback(callback_func_t funcp)
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

/* Install Linux trap table so PROM uses that instead of it's own. */
void prom_set_trap_table(unsigned long tba)
{
	p1275_cmd("SUNW,set-trap-table", P1275_INOUT(1, 0), tba);
}

#ifdef __SMP__
void prom_startcpu(int cpunode, unsigned long pc, unsigned long o0)
{
	p1275_cmd("SUNW,start-cpu", P1275_INOUT(3, 0), cpunode, pc, o0);
}
#endif

/* $Id: setup.c,v 1.2 1997/06/30 15:26:24 ralf Exp $
 * setup.c: SGI specific setup, including init of the feature struct.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#ifndef __GOGOGO__
#error "... about to fuckup your Indy?"
#endif
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/reboot.h>
#include <asm/vector.h>
#include <asm/sgialib.h>
#include <asm/sgi.h>
#include <asm/sgimc.h>
#include <asm/sgihpc.h>
#include <asm/sgint23.h>

extern int serial_console; /* in console.c, of course */

extern void sgi_machine_restart(char *command);
extern void sgi_machine_halt(void);
extern void sgi_machine_power_off(void);

struct feature sgi_feature = {
};

static void sgi_irq_setup(void)
{
	sgint_init();
}

#if 0
extern void register_console(void (*proc)(const char *));

static void sgi_print(const char *p)
{
	char c;

	while((c = *p++) != 0) {
		if(c == '\n')
			prom_putchar('\r');
		prom_putchar(c);
	}
}
#endif

void sgi_setup(void)
{
	char *ctype;

	irq_setup = sgi_irq_setup;
	feature = &sgi_feature;

	_machine_restart = sgi_machine_restart;
	_machine_halt = sgi_machine_halt;
	_machine_power_off = sgi_machine_power_off;

	/* register_console(sgi_print); */

	/* Init the INDY HPC I/O controller.  Need to call this before
	 * fucking with the memory controller because it needs to know the
	 * boardID and whether this is a Guiness or a FullHouse machine.
	 */
	sgihpc_init();

	/* Init INDY memory controller. */
	sgimc_init();

	/* ARCS console environment variable is set to "g?" for
	 * graphics console, it is set to "d" for the first serial
	 * line and "d2" for the second serial line.
	 */
	ctype = prom_getenv("console");
	serial_console = 0;
	if(*ctype == 'd') {
		if(*(ctype+1)=='2')
			serial_console = 1;
		else
			serial_console = 2;
		if(!serial_console) {
			prom_printf("Weird console env setting %s\n", ctype);
			prom_printf("Press a key to reboot.\n");
			prom_getchar();
			prom_imode();
		}
	}
}

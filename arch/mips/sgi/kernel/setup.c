/*
 * setup.c: SGI specific setup, including init of the feature struct.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: setup.c,v 1.5 1998/05/01 01:35:19 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/addrspace.h>
#include <asm/bcache.h>
#include <asm/keyboard.h>
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

static volatile struct hpc_keyb *sgi_kh = (struct hpc_keyb *) (KSEG1 + 0x1fbd9800 + 64);

static unsigned char sgi_read_input(void)
{
	return sgi_kh->data;
}

static void sgi_write_output(unsigned char val)
{
	sgi_kh->data = val;
}

static void sgi_write_command(unsigned char val)
{
	sgi_kh->command = val;
}

static unsigned char sgi_read_status(void)
{
	return sgi_kh->command;
}

__initfunc(static void sgi_keyboard_setup(void))
{
	kbd_read_input = sgi_read_input;
	kbd_write_output = sgi_write_output;
	kbd_write_command = sgi_write_command;
	kbd_read_status = sgi_read_status;
}

__initfunc(static void sgi_irq_setup(void))
{
	sgint_init();
}

__initfunc(void sgi_setup(void))
{
	char *ctype;

	irq_setup = sgi_irq_setup;
	feature = &sgi_feature;
	keyboard_setup = sgi_keyboard_setup;

	_machine_restart = sgi_machine_restart;
	_machine_halt = sgi_machine_halt;
	_machine_power_off = sgi_machine_power_off;

	/* Init the INDY HPC I/O controller.  Need to call this before
	 * fucking with the memory controller because it needs to know the
	 * boardID and whether this is a Guiness or a FullHouse machine.
	 */
	sgihpc_init();

	/* Init INDY memory controller. */
	sgimc_init();

	/* Now enable boardcaches, if any. */
	indy_sc_init();

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

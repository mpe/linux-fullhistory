/* $Id: setup.c,v 1.13 1998/09/16 22:50:46 ralf Exp $
 *
 * setup.c: SGI specific setup, including init of the feature struct.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998 Ralf Baechle (ralf@gnu.org)
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/console.h>
#include <linux/sched.h>
#include <linux/mc146818rtc.h>

#include <asm/addrspace.h>
#include <asm/bcache.h>
#include <asm/keyboard.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/sgialib.h>
#include <asm/sgi.h>
#include <asm/sgimc.h>
#include <asm/sgihpc.h>
#include <asm/sgint23.h>

extern int serial_console; /* in sgiserial.c  */

extern struct rtc_ops indy_rtc_ops;
void indy_reboot_setup(void);

static volatile struct hpc_keyb *sgi_kh = (struct hpc_keyb *) (KSEG1 + 0x1fbd9800 + 64);

#define KBD_STAT_IBF		0x02	/* Keyboard input buffer full */

static unsigned char sgi_read_input(void)
{
	return sgi_kh->data;
}

static void sgi_write_output(unsigned char val)
{
	int status;

	do {
		status = sgi_kh->command;
	} while (status & KBD_STAT_IBF);
	sgi_kh->data = val;
}

static void sgi_write_command(unsigned char val)
{
	int status;

	do {
		status = sgi_kh->command;
	} while (status & KBD_STAT_IBF);
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

	request_irq(SGI_KEYBOARD_IRQ, keyboard_interrupt,
	            0, "keyboard", NULL);

	/* Dirty hack, this get's called as a callback from the keyboard
	   driver.  We piggyback the initialization of the front panel
	   button handling on it even though they're technically not
	   related with the keyboard driver in any way.  Doing it from
	   indy_setup wouldn't work since kmalloc isn't initialized yet.  */
	indy_reboot_setup();
}

__initfunc(static void sgi_irq_setup(void))
{
	sgint_init();
}

__initfunc(void sgi_setup(void))
{
#ifdef CONFIG_SERIAL_CONSOLE
	char *ctype;
#endif

	irq_setup = sgi_irq_setup;
	keyboard_setup = sgi_keyboard_setup;

	/* Init the INDY HPC I/O controller.  Need to call this before
	 * fucking with the memory controller because it needs to know the
	 * boardID and whether this is a Guiness or a FullHouse machine.
	 */
	sgihpc_init();

	/* Init INDY memory controller. */
	sgimc_init();

	/* Now enable boardcaches, if any. */
	indy_sc_init();

#ifdef CONFIG_SERIAL_CONSOLE
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
#endif

#ifdef CONFIG_VT
	conswitchp = &newport_con;
#endif
	rtc_ops = &indy_rtc_ops;
}

/* $Id: setup.c,v 1.24 1999/06/12 17:26:15 ulfc Exp $
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
#include <linux/pc_keyb.h>

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
#include <asm/gdb-stub.h>

#ifdef CONFIG_REMOTE_DEBUG
extern void rs_kgdb_hook(int);
extern void breakpoint(void);
#endif

#if defined(CONFIG_SERIAL_CONSOLE) || defined(CONFIG_PROM_CONSOLE)
extern void console_setup(char *, int *);
#endif

extern struct rtc_ops indy_rtc_ops;
void indy_reboot_setup(void);
void sgi_volume_set(unsigned char);

static int remote_debug = 0;

#define sgi_kh ((struct hpc_keyb *) (KSEG1 + 0x1fbd9800 + 64))

#define KBD_STAT_IBF		0x02	/* Keyboard input buffer full */

static void sgi_request_region(void)
{
	/* No I/O ports are being used on the Indy.  */
}

static int sgi_request_irq(void (*handler)(int, void *, struct pt_regs *))
{
	/* Dirty hack, this get's called as a callback from the keyboard
	   driver.  We piggyback the initialization of the front panel
	   button handling on it even though they're technically not
	   related with the keyboard driver in any way.  Doing it from
	   indy_setup wouldn't work since kmalloc isn't initialized yet.  */
	indy_reboot_setup();

	return request_irq(SGI_KEYBOARD_IRQ, handler, 0, "keyboard", NULL);
}

static int sgi_aux_request_irq(void (*handler)(int, void *, struct pt_regs *))
{
	/* Nothing to do, interrupt is shared with the keyboard hw  */
	return 0;
}

static void sgi_aux_free_irq(void)
{
	/* Nothing to do, interrupt is shared with the keyboard hw  */
}

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

struct kbd_ops sgi_kbd_ops = {
	sgi_request_region,
	sgi_request_irq,

	sgi_aux_request_irq,
	sgi_aux_free_irq,

	sgi_read_input,
	sgi_write_output,
	sgi_write_command,
	sgi_read_status
};

__initfunc(static void sgi_irq_setup(void))
{
	sgint_init();

#ifdef CONFIG_REMOTE_DEBUG
	if (remote_debug)
		set_debug_traps();
	breakpoint(); /* you may move this line to whereever you want :-) */
#endif
}

__initfunc(void sgi_setup(void))
{
#ifdef CONFIG_SERIAL_CONSOLE
	char *ctype;
#endif
#ifdef CONFIG_REMOTE_DEBUG
	char *kgdb_ttyd;
#endif


	irq_setup = sgi_irq_setup;

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
	if(*ctype == 'd') {
		if(*(ctype+1)=='2')
			console_setup ("ttyS1", NULL);
		else
			console_setup ("ttyS0", NULL);
	}
#endif

#ifdef CONFIG_REMOTE_DEBUG
	kgdb_ttyd = prom_getcmdline();
	if ((kgdb_ttyd = strstr(kgdb_ttyd, "kgdb=ttyd")) != NULL) {
		int line;
		kgdb_ttyd += strlen("kgdb=ttyd");
		if (*kgdb_ttyd != '1' && *kgdb_ttyd != '2')
			printk("KGDB: Uknown serial line /dev/ttyd%c, "
			       "falling back to /dev/ttyd1\n", *kgdb_ttyd);
		line = *kgdb_ttyd == '2' ? 0 : 1;
		printk("KGDB: Using serial line /dev/ttyd%d for session\n",
		       line ? 1 : 2);
		rs_kgdb_hook(line);

		prom_printf("KGDB: Using serial line /dev/ttyd%d for session, "
			    "please connect your debugger\n", line ? 1 : 2);

		remote_debug = 1;
		/* Breakpoints and stuff are in sgi_irq_setup() */
	}
#endif

#ifdef CONFIG_SGI_PROM_CONSOLE
	console_setup("ttyS0", NULL);
#endif
	  
	sgi_volume_set(simple_strtoul(prom_getenv("volume"), NULL, 10));

#ifdef CONFIG_VT
#ifdef CONFIG_SGI_NEWPORT_CONSOLE
	conswitchp = &newport_con;
#else
	conswitchp = &dummy_con;
#endif
#endif

	rtc_ops = &indy_rtc_ops;
	kbd_ops = &sgi_kbd_ops;
#ifdef CONFIG_PSMOUSE
	aux_device_present = 0xaa;
#endif
#ifdef CONFIG_VIDEO_VINO
	init_vino();
#endif
}

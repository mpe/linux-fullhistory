/* $Id: sunserial.c,v 1.50 1997/09/03 11:54:59 ecd Exp $
 * serial.c: Serial port driver infrastructure for the Sparc.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/serial.h>

#include <asm/oplib.h>

#include "sunserial.h"

static void nop_rs_cons_hook(int chip, int out, int line)
{
	printk("Oops: %s called\n", __FUNCTION__);
}

static void nop_rs_kgdb_hook(int channel)
{
	printk("Oops: %s called\n", __FUNCTION__);
}

static void nop_rs_change_mouse_baud(int baud)
{
	printk("Oops: %s called\n", __FUNCTION__);
}

static int nop_rs_read_proc(char *page, char **start, off_t off, int count,
			    int *eof, void *data)
{
	printk("Oops: %s called\n", __FUNCTION__);
	return 0;
}


struct sunserial_operations rs_ops = {
	0,
	nop_rs_cons_hook,
	nop_rs_kgdb_hook,
	nop_rs_change_mouse_baud,
	nop_rs_read_proc
};

int rs_init(void)
{
	struct rs_initfunc *init;
	int err = -ENODEV;

	init = rs_ops.rs_init;
	while (init) {
		err = init->rs_init();
		init = init->next;
	}
	return err;
}

void rs_cons_hook(int chip, int out, int line)
{
	rs_ops.rs_cons_hook(chip, out, line);
}

void rs_kgdb_hook(int channel)
{
	rs_ops.rs_kgdb_hook(channel);
}

void rs_change_mouse_baud(int baud)
{
	rs_ops.rs_change_mouse_baud(baud);
}

int rs_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	return rs_ops.rs_read_proc(page, start, off, count, eof, data);
}

int register_serial(struct serial_struct *req)
{
	return -1;
}

void unregister_serial(int line)
{
}

void
sunserial_setinitfunc(unsigned long *memory_start, int (*init) (void))
{
	struct rs_initfunc *rs_init;

	*memory_start = (*memory_start + 7) & ~(7);
	rs_init = (struct rs_initfunc *) *memory_start;
	*memory_start += sizeof(struct rs_initfunc);

	rs_init->rs_init = init;
	rs_init->next = rs_ops.rs_init;
	rs_ops.rs_init = rs_init;
}

extern int zs_probe(unsigned long *);
#ifdef CONFIG_SAB82532
extern int sab82532_probe(unsigned long *);
#endif
#ifdef __sparc_v9__
extern int ps2kbd_probe(unsigned long *);
extern int su_probe(unsigned long *);
#endif

unsigned long
sun_serial_setup(unsigned long memory_start)
{
	/* Probe for controllers. */
	if (zs_probe(&memory_start) == 0)
		return memory_start;

#ifdef CONFIG_SAB82532
	sab82532_probe(&memory_start);
#endif
#ifdef __sparc_v9__
	if (ps2kbd_probe(&memory_start) == 0)
		return memory_start;
	if (su_probe(&memory_start) == 0)
		return memory_start;
#endif

	prom_printf("No serial devices found, bailing out.\n");
	prom_halt();
	return memory_start;
}

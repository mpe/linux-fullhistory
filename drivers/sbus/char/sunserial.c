/* $Id: sunserial.c,v 1.67 1998/10/25 03:22:46 jj Exp $
 * serial.c: Serial port driver infrastructure for the Sparc.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/string.h>
#include <linux/kbd_diacr.h>
#include <linux/version.h>
#include <linux/init.h>

#include <asm/oplib.h>

#include "sunserial.h"

int serial_console;

__initfunc(int con_is_present(void))
{
	return serial_console ? 0 : 1;
}

__initfunc(static void
nop_rs_kgdb_hook(int channel))
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
	nop_rs_kgdb_hook,
	nop_rs_change_mouse_baud,
	nop_rs_read_proc
};

int rs_init(void)
{
	struct initfunc *init;
	int err = -ENODEV;

	init = rs_ops.rs_init;
	while (init) {
		err = init->init();
		init = init->next;
	}
	return err;
}

__initfunc(void
rs_kgdb_hook(int channel))
{
	rs_ops.rs_kgdb_hook(channel);
}

__initfunc(long serial_console_init(long kmem_start, long kmem_end))
{
	return kmem_start;
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


static void nop_compute_shiftstate (void)
{
	printk("Oops: %s called\n", __FUNCTION__);
}

static void nop_setledstate (struct kbd_struct *kbd, unsigned int ledstate)
{
	printk("Oops: %s called\n", __FUNCTION__);
}

static unsigned char nop_getledstate (void)
{
	printk("Oops: %s called\n", __FUNCTION__);
	return 0;
}

static int nop_setkeycode (unsigned int scancode, unsigned int keycode)
{
	printk("Oops: %s called\n", __FUNCTION__);
	return -EINVAL;
}

static int nop_getkeycode (unsigned int scancode)
{
	printk("Oops: %s called\n", __FUNCTION__);
	return -EINVAL;
}

struct sunkbd_operations kbd_ops = {
	0,
	nop_compute_shiftstate,
	nop_setledstate,
	nop_getledstate,
	nop_setkeycode,
	nop_getkeycode
};

int kbd_init(void)
{
	struct initfunc *init;
	int err = -ENODEV;

	init = kbd_ops.kbd_init;
	while (init) {
		err = init->init();
		init = init->next;
	}
	return err;
}

void compute_shiftstate (void)
{
	kbd_ops.compute_shiftstate();
}

void setledstate (struct kbd_struct *kbd, unsigned int ledstate)
{
	kbd_ops.setledstate(kbd, ledstate);
}

unsigned char getledstate (void)
{
	return kbd_ops.getledstate();
}

int setkeycode (unsigned int scancode, unsigned int keycode)
{
	return kbd_ops.setkeycode(scancode, keycode);
}

int getkeycode (unsigned int scancode)
{
	return kbd_ops.getkeycode(scancode);
}

void
sunserial_setinitfunc(unsigned long *memory_start, int (*init) (void))
{
	struct initfunc *rs_init;

	*memory_start = (*memory_start + 7) & ~(7);
	rs_init = (struct initfunc *) *memory_start;
	*memory_start += sizeof(struct initfunc);

	rs_init->init = init;
	rs_init->next = rs_ops.rs_init;
	rs_ops.rs_init = rs_init;
}

void
sunserial_console_termios(struct console *con)
{
	char mode[16], buf[16], *s;
	char *mode_prop = "ttyX-mode";
	char *cd_prop = "ttyX-ignore-cd";
	char *dtr_prop = "ttyX-rts-dtr-off";
	int baud, bits, stop, cflag;
	char parity;
	int carrier = 0;
	int rtsdtr = 1;
	int topnd, nd;

	if (!serial_console)
		return;

	if (serial_console == 1) {
		mode_prop[3] = 'a';
		cd_prop[3] = 'a';
		dtr_prop[3] = 'a';
	} else {
		mode_prop[3] = 'b';
		cd_prop[3] = 'b';
		dtr_prop[3] = 'b';
	}

	topnd = prom_getchild(prom_root_node);
	nd = prom_searchsiblings(topnd, "options");
	if (!nd) {
		strcpy(mode, "9600,8,n,1,-");
		goto no_options;
	}

	if (!prom_node_has_property(nd, mode_prop)) {
		strcpy(mode, "9600,8,n,1,-");
		goto no_options;
	}

	memset(mode, 0, sizeof(mode));
	prom_getstring(nd, mode_prop, mode, sizeof(mode));

	if (prom_node_has_property(nd, cd_prop)) {
		memset(buf, 0, sizeof(buf));
		prom_getstring(nd, cd_prop, buf, sizeof(buf));
		if (!strcmp(buf, "false"))
			carrier = 1;

		/* XXX: this is unused below. */
	}

	if (prom_node_has_property(nd, cd_prop)) {
		memset(buf, 0, sizeof(buf));
		prom_getstring(nd, cd_prop, buf, sizeof(buf));
		if (!strcmp(buf, "false"))
			rtsdtr = 0;

		/* XXX: this is unused below. */
	}

no_options:
	cflag = CREAD | HUPCL | CLOCAL;

	s = mode;
	baud = simple_strtoul(s, 0, 0);
	s = strchr(s, ',');
	bits = simple_strtoul(++s, 0, 0);
	s = strchr(s, ',');
	parity = *(++s);
	s = strchr(s, ',');
	stop = simple_strtoul(++s, 0, 0);
	s = strchr(s, ',');
	/* XXX handshake is not handled here. */

	switch (baud) {
		case 150: cflag |= B150; break;
		case 300: cflag |= B300; break;
		case 600: cflag |= B600; break;
		case 1200: cflag |= B1200; break;
		case 2400: cflag |= B2400; break;
		case 4800: cflag |= B4800; break;
		case 9600: cflag |= B9600; break;
		case 19200: cflag |= B19200; break;
		case 38400: cflag |= B38400; break;
		default: cflag |= B9600; break;
	}

	switch (bits) {
		case 5: cflag |= CS5; break;
		case 6: cflag |= CS6; break;
		case 7: cflag |= CS7; break;
		case 8: cflag |= CS8; break;
		default: cflag |= CS8; break;
	}

	switch (parity) {
		case 'o': cflag |= (PARENB | PARODD); break;
		case 'e': cflag |= PARENB; break;
		case 'n': default: break;
	}

	switch (stop) {
		case 2: cflag |= CSTOPB; break;
		case 1: default: break;
	}

	con->cflag = cflag;
}

void
sunkbd_setinitfunc(unsigned long *memory_start, int (*init) (void))
{
	struct initfunc *kbd_init;

	*memory_start = (*memory_start + 7) & ~(7);
	kbd_init = (struct initfunc *) *memory_start;
	*memory_start += sizeof(struct initfunc);

	kbd_init->init = init;
	kbd_init->next = kbd_ops.kbd_init;
	kbd_ops.kbd_init = kbd_init;
}

#ifdef CONFIG_PCI
void
sunkbd_install_keymaps(unsigned long *memory_start,
		       ushort **src_key_maps, unsigned int src_keymap_count,
		       char *src_func_buf, char **src_func_table,
		       int src_funcbufsize, int src_funcbufleft,
		       struct kbdiacr *src_accent_table,
		       unsigned int src_accent_table_size)
{
	extern unsigned int keymap_count;
	int i, j;

	for (i = 0; i < MAX_NR_KEYMAPS; i++) {
		if (src_key_maps[i]) {
			if (!key_maps[i]) {
				key_maps[i] = (ushort *)*memory_start;
				*memory_start += NR_KEYS * sizeof(ushort);
			}
			for (j = 0; j < NR_KEYS; j++)
				key_maps[i][j] = src_key_maps[i][j];
		}
		key_maps[i] = src_key_maps[i];
	}
	keymap_count = src_keymap_count;

	for (i = 0; i < MAX_NR_FUNC; i++)
		func_table[i] = src_func_table[i];
	funcbufptr = src_func_buf;
	funcbufsize = src_funcbufsize;
	funcbufleft = src_funcbufleft;

	for (i = 0; i < MAX_DIACR; i++)
		accent_table[i] = src_accent_table[i];
	accent_table_size = src_accent_table_size;
}
#endif

extern int su_probe(unsigned long *);
extern int zs_probe(unsigned long *);
#ifdef CONFIG_SAB82532
extern int sab82532_probe(unsigned long *);
#endif
#ifdef CONFIG_PCI
extern int ps2kbd_probe(unsigned long *);
#endif

__initfunc(unsigned long
sun_serial_setup(unsigned long memory_start))
{
	int ret = 1;
	
#if defined(CONFIG_PCI) && !defined(__sparc_v9__)
	/*
	 * Probing sequence on sparc differs from sparc64.
	 * Keyboard is probed ahead of su because we want su function
	 * when keyboard is active. su is probed ahead of zs in order to
	 * get console on MrCoffee with fine but disconnected zs.
	 */
	if (!serial_console)
		ps2kbd_probe(&memory_start);
	if (su_probe(&memory_start) == 0)
		return memory_start;
#endif

	if (zs_probe(&memory_start) == 0)
		return memory_start;
		
#ifdef CONFIG_SAB82532
	ret = sab82532_probe(&memory_start);
#endif

#if defined(CONFIG_PCI) && defined(__sparc_v9__)
	/*
	 * Keyboard serial devices.
	 *
	 * Well done, Sun, prom_devopen("/pci@1f,4000/ebus@1/su@14,3083f8")
	 * hangs the machine if no keyboard is connected to the device...
	 * All PCI PROMs seem to do this, I have seen this on the Ultra 450
	 * with version 3.5 PROM, and on the Ultra/AX with 3.1.5 PROM.
	 *
	 * So be very careful not to probe for keyboards if we are on a
	 * serial console.
	 */
	if (!serial_console) {
		if (ps2kbd_probe(&memory_start) == 0)
			return memory_start;
		if (su_probe(&memory_start) == 0)
			return memory_start;
	}
#endif

	if (!ret)
		return memory_start;
		
#ifdef __sparc_v9__
	ret = prom_finddevice("/ssp-serial");
	if (ret && ret != -1) {
		/* Hello, Starfire. Pleased to meet you :) */
		return memory_start;
	}
#endif

	prom_printf("No serial devices found, bailing out.\n");
	prom_halt();
	return memory_start;
}

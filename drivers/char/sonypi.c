/*
 * Sony Programmable I/O Control Device driver for VAIO
 *
 * Copyright (C) 2001-2005 Stelian Pop <stelian@popies.net>
 *
 * Copyright (C) 2005 Narayanan R S <nars@kadamba.org>
 *
 * Copyright (C) 2001-2002 Alc�ve <www.alcove.com>
 *
 * Copyright (C) 2001 Michael Ashley <m.ashley@unsw.edu.au>
 *
 * Copyright (C) 2001 Junichi Morita <jun1m@mars.dti.ne.jp>
 *
 * Copyright (C) 2000 Takaya Kinjo <t-kinjo@tc4.so-net.ne.jp>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/kfifo.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>

#include <linux/sonypi.h>

#define SONYPI_DRIVER_VERSION	 "1.26"

MODULE_AUTHOR("Stelian Pop <stelian@popies.net>");
MODULE_DESCRIPTION("Sony Programmable I/O Control Device driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(SONYPI_DRIVER_VERSION);

static int minor = -1;
module_param(minor, int, 0);
MODULE_PARM_DESC(minor,
		 "minor number of the misc device, default is -1 (automatic)");

static int verbose;		/* = 0 */
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "be verbose, default is 0 (no)");

static int fnkeyinit;		/* = 0 */
module_param(fnkeyinit, int, 0444);
MODULE_PARM_DESC(fnkeyinit,
		 "set this if your Fn keys do not generate any event");

static int camera;		/* = 0 */
module_param(camera, int, 0444);
MODULE_PARM_DESC(camera,
		 "set this if you have a MotionEye camera (PictureBook series)");

static int compat;		/* = 0 */
module_param(compat, int, 0444);
MODULE_PARM_DESC(compat,
		 "set this if you want to enable backward compatibility mode");

static unsigned long mask = 0xffffffff;
module_param(mask, ulong, 0644);
MODULE_PARM_DESC(mask,
		 "set this to the mask of event you want to enable (see doc)");

static int useinput = 1;
module_param(useinput, int, 0444);
MODULE_PARM_DESC(useinput,
		 "set this if you would like sonypi to feed events to the input subsystem");

#define SONYPI_DEVICE_MODEL_TYPE1	1
#define SONYPI_DEVICE_MODEL_TYPE2	2

/* type1 models use those */
#define SONYPI_IRQ_PORT			0x8034
#define SONYPI_IRQ_SHIFT		22
#define SONYPI_BASE			0x50
#define SONYPI_G10A			(SONYPI_BASE+0x14)
#define SONYPI_TYPE1_REGION_SIZE	0x08
#define SONYPI_TYPE1_EVTYPE_OFFSET	0x04

/* type2 series specifics */
#define SONYPI_SIRQ			0x9b
#define SONYPI_SLOB			0x9c
#define SONYPI_SHIB			0x9d
#define SONYPI_TYPE2_REGION_SIZE	0x20
#define SONYPI_TYPE2_EVTYPE_OFFSET	0x12

/* battery / brightness addresses */
#define SONYPI_BAT_FLAGS	0x81
#define SONYPI_LCD_LIGHT	0x96
#define SONYPI_BAT1_PCTRM	0xa0
#define SONYPI_BAT1_LEFT	0xa2
#define SONYPI_BAT1_MAXRT	0xa4
#define SONYPI_BAT2_PCTRM	0xa8
#define SONYPI_BAT2_LEFT	0xaa
#define SONYPI_BAT2_MAXRT	0xac
#define SONYPI_BAT1_MAXTK	0xb0
#define SONYPI_BAT1_FULL	0xb2
#define SONYPI_BAT2_MAXTK	0xb8
#define SONYPI_BAT2_FULL	0xba

/* FAN0 information (reverse engineered from ACPI tables) */
#define SONYPI_FAN0_STATUS	0x93
#define SONYPI_TEMP_STATUS	0xC1

/* ioports used for brightness and type2 events */
#define SONYPI_DATA_IOPORT	0x62
#define SONYPI_CST_IOPORT	0x66

/* The set of possible ioports */
struct sonypi_ioport_list {
	u16	port1;
	u16	port2;
};

static struct sonypi_ioport_list sonypi_type1_ioport_list[] = {
	{ 0x10c0, 0x10c4 },	/* looks like the default on C1Vx */
	{ 0x1080, 0x1084 },
	{ 0x1090, 0x1094 },
	{ 0x10a0, 0x10a4 },
	{ 0x10b0, 0x10b4 },
	{ 0x0, 0x0 }
};

static struct sonypi_ioport_list sonypi_type2_ioport_list[] = {
	{ 0x1080, 0x1084 },
	{ 0x10a0, 0x10a4 },
	{ 0x10c0, 0x10c4 },
	{ 0x10e0, 0x10e4 },
	{ 0x0, 0x0 }
};

/* The set of possible interrupts */
struct sonypi_irq_list {
	u16	irq;
	u16	bits;
};

static struct sonypi_irq_list sonypi_type1_irq_list[] = {
	{ 11, 0x2 },	/* IRQ 11, GO22=0,GO23=1 in AML */
	{ 10, 0x1 },	/* IRQ 10, GO22=1,GO23=0 in AML */
	{  5, 0x0 },	/* IRQ  5, GO22=0,GO23=0 in AML */
	{  0, 0x3 }	/* no IRQ, GO22=1,GO23=1 in AML */
};

static struct sonypi_irq_list sonypi_type2_irq_list[] = {
	{ 11, 0x80 },	/* IRQ 11, 0x80 in SIRQ in AML */
	{ 10, 0x40 },	/* IRQ 10, 0x40 in SIRQ in AML */
	{  9, 0x20 },	/* IRQ  9, 0x20 in SIRQ in AML */
	{  6, 0x10 },	/* IRQ  6, 0x10 in SIRQ in AML */
	{  0, 0x00 }	/* no IRQ, 0x00 in SIRQ in AML */
};

#define SONYPI_CAMERA_BRIGHTNESS		0
#define SONYPI_CAMERA_CONTRAST			1
#define SONYPI_CAMERA_HUE			2
#define SONYPI_CAMERA_COLOR			3
#define SONYPI_CAMERA_SHARPNESS			4

#define SONYPI_CAMERA_PICTURE			5
#define SONYPI_CAMERA_EXPOSURE_MASK		0xC
#define SONYPI_CAMERA_WHITE_BALANCE_MASK	0x3
#define SONYPI_CAMERA_PICTURE_MODE_MASK		0x30
#define SONYPI_CAMERA_MUTE_MASK			0x40

/* the rest don't need a loop until not 0xff */
#define SONYPI_CAMERA_AGC			6
#define SONYPI_CAMERA_AGC_MASK			0x30
#define SONYPI_CAMERA_SHUTTER_MASK 		0x7

#define SONYPI_CAMERA_SHUTDOWN_REQUEST		7
#define SONYPI_CAMERA_CONTROL			0x10

#define SONYPI_CAMERA_STATUS 			7
#define SONYPI_CAMERA_STATUS_READY 		0x2
#define SONYPI_CAMERA_STATUS_POSITION		0x4

#define SONYPI_DIRECTION_BACKWARDS 		0x4

#define SONYPI_CAMERA_REVISION 			8
#define SONYPI_CAMERA_ROMVERSION 		9

/* Event masks */
#define SONYPI_JOGGER_MASK			0x00000001
#define SONYPI_CAPTURE_MASK			0x00000002
#define SONYPI_FNKEY_MASK			0x00000004
#define SONYPI_BLUETOOTH_MASK			0x00000008
#define SONYPI_PKEY_MASK			0x00000010
#define SONYPI_BACK_MASK			0x00000020
#define SONYPI_HELP_MASK			0x00000040
#define SONYPI_LID_MASK				0x00000080
#define SONYPI_ZOOM_MASK			0x00000100
#define SONYPI_THUMBPHRASE_MASK			0x00000200
#define SONYPI_MEYE_MASK			0x00000400
#define SONYPI_MEMORYSTICK_MASK			0x00000800
#define SONYPI_BATTERY_MASK			0x00001000

struct sonypi_event {
	u8	data;
	u8	event;
};

/* The set of possible button release events */
static struct sonypi_event sonypi_releaseev[] = {
	{ 0x00, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0, 0 }
};

/* The set of possible jogger events  */
static struct sonypi_event sonypi_joggerev[] = {
	{ 0x1f, SONYPI_EVENT_JOGDIAL_UP },
	{ 0x01, SONYPI_EVENT_JOGDIAL_DOWN },
	{ 0x5f, SONYPI_EVENT_JOGDIAL_UP_PRESSED },
	{ 0x41, SONYPI_EVENT_JOGDIAL_DOWN_PRESSED },
	{ 0x1e, SONYPI_EVENT_JOGDIAL_FAST_UP },
	{ 0x02, SONYPI_EVENT_JOGDIAL_FAST_DOWN },
	{ 0x5e, SONYPI_EVENT_JOGDIAL_FAST_UP_PRESSED },
	{ 0x42, SONYPI_EVENT_JOGDIAL_FAST_DOWN_PRESSED },
	{ 0x1d, SONYPI_EVENT_JOGDIAL_VFAST_UP },
	{ 0x03, SONYPI_EVENT_JOGDIAL_VFAST_DOWN },
	{ 0x5d, SONYPI_EVENT_JOGDIAL_VFAST_UP_PRESSED },
	{ 0x43, SONYPI_EVENT_JOGDIAL_VFAST_DOWN_PRESSED },
	{ 0x40, SONYPI_EVENT_JOGDIAL_PRESSED },
	{ 0, 0 }
};

/* The set of possible capture button events */
static struct sonypi_event sonypi_captureev[] = {
	{ 0x05, SONYPI_EVENT_CAPTURE_PARTIALPRESSED },
	{ 0x07, SONYPI_EVENT_CAPTURE_PRESSED },
	{ 0x01, SONYPI_EVENT_CAPTURE_PARTIALRELEASED },
	{ 0, 0 }
};

/* The set of possible fnkeys events */
static struct sonypi_event sonypi_fnkeyev[] = {
	{ 0x10, SONYPI_EVENT_FNKEY_ESC },
	{ 0x11, SONYPI_EVENT_FNKEY_F1 },
	{ 0x12, SONYPI_EVENT_FNKEY_F2 },
	{ 0x13, SONYPI_EVENT_FNKEY_F3 },
	{ 0x14, SONYPI_EVENT_FNKEY_F4 },
	{ 0x15, SONYPI_EVENT_FNKEY_F5 },
	{ 0x16, SONYPI_EVENT_FNKEY_F6 },
	{ 0x17, SONYPI_EVENT_FNKEY_F7 },
	{ 0x18, SONYPI_EVENT_FNKEY_F8 },
	{ 0x19, SONYPI_EVENT_FNKEY_F9 },
	{ 0x1a, SONYPI_EVENT_FNKEY_F10 },
	{ 0x1b, SONYPI_EVENT_FNKEY_F11 },
	{ 0x1c, SONYPI_EVENT_FNKEY_F12 },
	{ 0x1f, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x21, SONYPI_EVENT_FNKEY_1 },
	{ 0x22, SONYPI_EVENT_FNKEY_2 },
	{ 0x31, SONYPI_EVENT_FNKEY_D },
	{ 0x32, SONYPI_EVENT_FNKEY_E },
	{ 0x33, SONYPI_EVENT_FNKEY_F },
	{ 0x34, SONYPI_EVENT_FNKEY_S },
	{ 0x35, SONYPI_EVENT_FNKEY_B },
	{ 0x36, SONYPI_EVENT_FNKEY_ONLY },
	{ 0, 0 }
};

/* The set of possible program key events */
static struct sonypi_event sonypi_pkeyev[] = {
	{ 0x01, SONYPI_EVENT_PKEY_P1 },
	{ 0x02, SONYPI_EVENT_PKEY_P2 },
	{ 0x04, SONYPI_EVENT_PKEY_P3 },
	{ 0x5c, SONYPI_EVENT_PKEY_P1 },
	{ 0, 0 }
};

/* The set of possible bluetooth events */
static struct sonypi_event sonypi_blueev[] = {
	{ 0x55, SONYPI_EVENT_BLUETOOTH_PRESSED },
	{ 0x59, SONYPI_EVENT_BLUETOOTH_ON },
	{ 0x5a, SONYPI_EVENT_BLUETOOTH_OFF },
	{ 0, 0 }
};

/* The set of possible back button events */
static struct sonypi_event sonypi_backev[] = {
	{ 0x20, SONYPI_EVENT_BACK_PRESSED },
	{ 0, 0 }
};

/* The set of possible help button events */
static struct sonypi_event sonypi_helpev[] = {
	{ 0x3b, SONYPI_EVENT_HELP_PRESSED },
	{ 0, 0 }
};


/* The set of possible lid events */
static struct sonypi_event sonypi_lidev[] = {
	{ 0x51, SONYPI_EVENT_LID_CLOSED },
	{ 0x50, SONYPI_EVENT_LID_OPENED },
	{ 0, 0 }
};

/* The set of possible zoom events */
static struct sonypi_event sonypi_zoomev[] = {
	{ 0x39, SONYPI_EVENT_ZOOM_PRESSED },
	{ 0, 0 }
};

/* The set of possible thumbphrase events */
static struct sonypi_event sonypi_thumbphraseev[] = {
	{ 0x3a, SONYPI_EVENT_THUMBPHRASE_PRESSED },
	{ 0, 0 }
};

/* The set of possible motioneye camera events */
static struct sonypi_event sonypi_meyeev[] = {
	{ 0x00, SONYPI_EVENT_MEYE_FACE },
	{ 0x01, SONYPI_EVENT_MEYE_OPPOSITE },
	{ 0, 0 }
};

/* The set of possible memorystick events */
static struct sonypi_event sonypi_memorystickev[] = {
	{ 0x53, SONYPI_EVENT_MEMORYSTICK_INSERT },
	{ 0x54, SONYPI_EVENT_MEMORYSTICK_EJECT },
	{ 0, 0 }
};

/* The set of possible battery events */
static struct sonypi_event sonypi_batteryev[] = {
	{ 0x20, SONYPI_EVENT_BATTERY_INSERT },
	{ 0x30, SONYPI_EVENT_BATTERY_REMOVE },
	{ 0, 0 }
};

static struct sonypi_eventtypes {
	int			model;
	u8			data;
	unsigned long		mask;
	struct sonypi_event *	events;
} sonypi_eventtypes[] = {
	{ SONYPI_DEVICE_MODEL_TYPE1, 0, 0xffffffff, sonypi_releaseev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x70, SONYPI_MEYE_MASK, sonypi_meyeev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x30, SONYPI_LID_MASK, sonypi_lidev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x60, SONYPI_CAPTURE_MASK, sonypi_captureev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x10, SONYPI_JOGGER_MASK, sonypi_joggerev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x20, SONYPI_FNKEY_MASK, sonypi_fnkeyev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x30, SONYPI_BLUETOOTH_MASK, sonypi_blueev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x40, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x30, SONYPI_MEMORYSTICK_MASK, sonypi_memorystickev },
	{ SONYPI_DEVICE_MODEL_TYPE1, 0x40, SONYPI_BATTERY_MASK, sonypi_batteryev },

	{ SONYPI_DEVICE_MODEL_TYPE2, 0, 0xffffffff, sonypi_releaseev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x38, SONYPI_LID_MASK, sonypi_lidev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x11, SONYPI_JOGGER_MASK, sonypi_joggerev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x61, SONYPI_CAPTURE_MASK, sonypi_captureev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x21, SONYPI_FNKEY_MASK, sonypi_fnkeyev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x31, SONYPI_BLUETOOTH_MASK, sonypi_blueev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x08, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x11, SONYPI_BACK_MASK, sonypi_backev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x08, SONYPI_HELP_MASK, sonypi_helpev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x21, SONYPI_HELP_MASK, sonypi_helpev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x21, SONYPI_ZOOM_MASK, sonypi_zoomev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x20, SONYPI_THUMBPHRASE_MASK, sonypi_thumbphraseev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x31, SONYPI_MEMORYSTICK_MASK, sonypi_memorystickev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x41, SONYPI_BATTERY_MASK, sonypi_batteryev },
	{ SONYPI_DEVICE_MODEL_TYPE2, 0x31, SONYPI_PKEY_MASK, sonypi_pkeyev },

	{ 0 }
};

#define SONYPI_BUF_SIZE	128

/* The name of the devices for the input device drivers */
#define SONYPI_JOG_INPUTNAME	"Sony Vaio Jogdial"
#define SONYPI_KEY_INPUTNAME	"Sony Vaio Keys"

/* Correspondance table between sonypi events and input layer events */
static struct {
	int sonypiev;
	int inputev;
} sonypi_inputkeys[] = {
	{ SONYPI_EVENT_CAPTURE_PRESSED,	 	KEY_CAMERA },
	{ SONYPI_EVENT_FNKEY_ONLY, 		KEY_FN },
	{ SONYPI_EVENT_FNKEY_ESC, 		KEY_FN_ESC },
	{ SONYPI_EVENT_FNKEY_F1, 		KEY_FN_F1 },
	{ SONYPI_EVENT_FNKEY_F2, 		KEY_FN_F2 },
	{ SONYPI_EVENT_FNKEY_F3, 		KEY_FN_F3 },
	{ SONYPI_EVENT_FNKEY_F4, 		KEY_FN_F4 },
	{ SONYPI_EVENT_FNKEY_F5, 		KEY_FN_F5 },
	{ SONYPI_EVENT_FNKEY_F6, 		KEY_FN_F6 },
	{ SONYPI_EVENT_FNKEY_F7, 		KEY_FN_F7 },
	{ SONYPI_EVENT_FNKEY_F8, 		KEY_FN_F8 },
	{ SONYPI_EVENT_FNKEY_F9,		KEY_FN_F9 },
	{ SONYPI_EVENT_FNKEY_F10,		KEY_FN_F10 },
	{ SONYPI_EVENT_FNKEY_F11, 		KEY_FN_F11 },
	{ SONYPI_EVENT_FNKEY_F12,		KEY_FN_F12 },
	{ SONYPI_EVENT_FNKEY_1, 		KEY_FN_1 },
	{ SONYPI_EVENT_FNKEY_2, 		KEY_FN_2 },
	{ SONYPI_EVENT_FNKEY_D,			KEY_FN_D },
	{ SONYPI_EVENT_FNKEY_E,			KEY_FN_E },
	{ SONYPI_EVENT_FNKEY_F,			KEY_FN_F },
	{ SONYPI_EVENT_FNKEY_S,			KEY_FN_S },
	{ SONYPI_EVENT_FNKEY_B,			KEY_FN_B },
	{ SONYPI_EVENT_BLUETOOTH_PRESSED, 	KEY_BLUE },
	{ SONYPI_EVENT_BLUETOOTH_ON, 		KEY_BLUE },
	{ SONYPI_EVENT_PKEY_P1, 		KEY_PROG1 },
	{ SONYPI_EVENT_PKEY_P2, 		KEY_PROG2 },
	{ SONYPI_EVENT_PKEY_P3, 		KEY_PROG3 },
	{ SONYPI_EVENT_BACK_PRESSED, 		KEY_BACK },
	{ SONYPI_EVENT_HELP_PRESSED, 		KEY_HELP },
	{ SONYPI_EVENT_ZOOM_PRESSED, 		KEY_ZOOM },
	{ SONYPI_EVENT_THUMBPHRASE_PRESSED, 	BTN_THUMB },
	{ 0, 0 },
};

static struct sonypi_device {
	struct pci_dev *dev;
	struct platform_device *pdev;
	u16 irq;
	u16 bits;
	u16 ioport1;
	u16 ioport2;
	u16 region_size;
	u16 evtype_offset;
	int camera_power;
	int bluetooth_power;
	struct semaphore lock;
	struct kfifo *fifo;
	spinlock_t fifo_lock;
	wait_queue_head_t fifo_proc_list;
	struct fasync_struct *fifo_async;
	int open_count;
	int model;
	struct input_dev input_jog_dev;
	struct input_dev input_key_dev;
	struct work_struct input_work;
	struct kfifo *input_fifo;
	spinlock_t input_fifo_lock;
} sonypi_device;

#define ITERATIONS_LONG		10000
#define ITERATIONS_SHORT	10

#define wait_on_command(quiet, command, iterations) { \
	unsigned int n = iterations; \
	while (--n && (command)) \
		udelay(1); \
	if (!n && (verbose || !quiet)) \
		printk(KERN_WARNING "sonypi command failed at %s : %s (line %d)\n", __FILE__, __FUNCTION__, __LINE__); \
}

#ifdef CONFIG_ACPI
#define SONYPI_ACPI_ACTIVE (!acpi_disabled)
#else
#define SONYPI_ACPI_ACTIVE 0
#endif				/* CONFIG_ACPI */

static int sonypi_ec_write(u8 addr, u8 value)
{
#ifdef CONFIG_ACPI_EC
	if (SONYPI_ACPI_ACTIVE)
		return ec_write(addr, value);
#endif
	wait_on_command(1, inb_p(SONYPI_CST_IOPORT) & 3, ITERATIONS_LONG);
	outb_p(0x81, SONYPI_CST_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	outb_p(addr, SONYPI_DATA_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	outb_p(value, SONYPI_DATA_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	return 0;
}

static int sonypi_ec_read(u8 addr, u8 *value)
{
#ifdef CONFIG_ACPI_EC
	if (SONYPI_ACPI_ACTIVE)
		return ec_read(addr, value);
#endif
	wait_on_command(1, inb_p(SONYPI_CST_IOPORT) & 3, ITERATIONS_LONG);
	outb_p(0x80, SONYPI_CST_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	outb_p(addr, SONYPI_DATA_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	*value = inb_p(SONYPI_DATA_IOPORT);
	return 0;
}

static int ec_read16(u8 addr, u16 *value)
{
	u8 val_lb, val_hb;
	if (sonypi_ec_read(addr, &val_lb))
		return -1;
	if (sonypi_ec_read(addr + 1, &val_hb))
		return -1;
	*value = val_lb | (val_hb << 8);
	return 0;
}

/* Initializes the device - this comes from the AML code in the ACPI bios */
static void sonypi_type1_srs(void)
{
	u32 v;

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFFFF0000) | ((u32) sonypi_device.ioport1);
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFFF0FFFF) |
	    (((u32) sonypi_device.ioport1 ^ sonypi_device.ioport2) << 16);
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	v = inl(SONYPI_IRQ_PORT);
	v &= ~(((u32) 0x3) << SONYPI_IRQ_SHIFT);
	v |= (((u32) sonypi_device.bits) << SONYPI_IRQ_SHIFT);
	outl(v, SONYPI_IRQ_PORT);

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFF1FFFFF) | 0x00C00000;
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);
}

static void sonypi_type2_srs(void)
{
	if (sonypi_ec_write(SONYPI_SHIB, (sonypi_device.ioport1 & 0xFF00) >> 8))
		printk(KERN_WARNING "ec_write failed\n");
	if (sonypi_ec_write(SONYPI_SLOB, sonypi_device.ioport1 & 0x00FF))
		printk(KERN_WARNING "ec_write failed\n");
	if (sonypi_ec_write(SONYPI_SIRQ, sonypi_device.bits))
		printk(KERN_WARNING "ec_write failed\n");
	udelay(10);
}

/* Disables the device - this comes from the AML code in the ACPI bios */
static void sonypi_type1_dis(void)
{
	u32 v;

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = v & 0xFF3FFFFF;
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	v = inl(SONYPI_IRQ_PORT);
	v |= (0x3 << SONYPI_IRQ_SHIFT);
	outl(v, SONYPI_IRQ_PORT);
}

static void sonypi_type2_dis(void)
{
	if (sonypi_ec_write(SONYPI_SHIB, 0))
		printk(KERN_WARNING "ec_write failed\n");
	if (sonypi_ec_write(SONYPI_SLOB, 0))
		printk(KERN_WARNING "ec_write failed\n");
	if (sonypi_ec_write(SONYPI_SIRQ, 0))
		printk(KERN_WARNING "ec_write failed\n");
}

static u8 sonypi_call1(u8 dev)
{
	u8 v1, v2;

	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(dev, sonypi_device.ioport2);
	v1 = inb_p(sonypi_device.ioport2);
	v2 = inb_p(sonypi_device.ioport1);
	return v2;
}

static u8 sonypi_call2(u8 dev, u8 fn)
{
	u8 v1;

	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(dev, sonypi_device.ioport2);
	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(fn, sonypi_device.ioport1);
	v1 = inb_p(sonypi_device.ioport1);
	return v1;
}

static u8 sonypi_call3(u8 dev, u8 fn, u8 v)
{
	u8 v1;

	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(dev, sonypi_device.ioport2);
	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(fn, sonypi_device.ioport1);
	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(v, sonypi_device.ioport1);
	v1 = inb_p(sonypi_device.ioport1);
	return v1;
}

#if 0
/* Get brightness, hue etc. Unreliable... */
static u8 sonypi_read(u8 fn)
{
	u8 v1, v2;
	int n = 100;

	while (n--) {
		v1 = sonypi_call2(0x8f, fn);
		v2 = sonypi_call2(0x8f, fn);
		if (v1 == v2 && v1 != 0xff)
			return v1;
	}
	return 0xff;
}
#endif

/* Set brightness, hue etc */
static void sonypi_set(u8 fn, u8 v)
{
	wait_on_command(0, sonypi_call3(0x90, fn, v), ITERATIONS_SHORT);
}

/* Tests if the camera is ready */
static int sonypi_camera_ready(void)
{
	u8 v;

	v = sonypi_call2(0x8f, SONYPI_CAMERA_STATUS);
	return (v != 0xff && (v & SONYPI_CAMERA_STATUS_READY));
}

/* Turns the camera off */
static void sonypi_camera_off(void)
{
	sonypi_set(SONYPI_CAMERA_PICTURE, SONYPI_CAMERA_MUTE_MASK);

	if (!sonypi_device.camera_power)
		return;

	sonypi_call2(0x91, 0);
	sonypi_device.camera_power = 0;
}

/* Turns the camera on */
static void sonypi_camera_on(void)
{
	int i, j;

	if (sonypi_device.camera_power)
		return;

	for (j = 5; j > 0; j--) {

		while (sonypi_call2(0x91, 0x1))
			msleep(10);
		sonypi_call1(0x93);

		for (i = 400; i > 0; i--) {
			if (sonypi_camera_ready())
				break;
			msleep(10);
		}
		if (i)
			break;
	}

	if (j == 0) {
		printk(KERN_WARNING "sonypi: failed to power on camera\n");
		return;
	}

	sonypi_set(0x10, 0x5a);
	sonypi_device.camera_power = 1;
}

/* sets the bluetooth subsystem power state */
static void sonypi_setbluetoothpower(u8 state)
{
	state = !!state;

	if (sonypi_device.bluetooth_power == state)
		return;

	sonypi_call2(0x96, state);
	sonypi_call1(0x82);
	sonypi_device.bluetooth_power = state;
}

static void input_keyrelease(void *data)
{
	struct input_dev *input_dev;
	int key;

	while (1) {
		if (kfifo_get(sonypi_device.input_fifo,
			      (unsigned char *)&input_dev,
			      sizeof(input_dev)) != sizeof(input_dev))
			return;
		if (kfifo_get(sonypi_device.input_fifo,
			      (unsigned char *)&key,
			      sizeof(key)) != sizeof(key))
			return;

		msleep(10);
		input_report_key(input_dev, key, 0);
		input_sync(input_dev);
	}
}

/* Interrupt handler: some event is available */
static irqreturn_t sonypi_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	u8 v1, v2, event = 0;
	int i, j;

	v1 = inb_p(sonypi_device.ioport1);
	v2 = inb_p(sonypi_device.ioport1 + sonypi_device.evtype_offset);

	for (i = 0; sonypi_eventtypes[i].model; i++) {
		if (sonypi_device.model != sonypi_eventtypes[i].model)
			continue;
		if ((v2 & sonypi_eventtypes[i].data) !=
		    sonypi_eventtypes[i].data)
			continue;
		if (!(mask & sonypi_eventtypes[i].mask))
			continue;
		for (j = 0; sonypi_eventtypes[i].events[j].event; j++) {
			if (v1 == sonypi_eventtypes[i].events[j].data) {
				event = sonypi_eventtypes[i].events[j].event;
				goto found;
			}
		}
	}

	if (verbose)
		printk(KERN_WARNING
		       "sonypi: unknown event port1=0x%02x,port2=0x%02x\n",
		       v1, v2);
	/* We need to return IRQ_HANDLED here because there *are*
	 * events belonging to the sonypi device we don't know about,
	 * but we still don't want those to pollute the logs... */
	return IRQ_HANDLED;

found:
	if (verbose > 1)
		printk(KERN_INFO
		       "sonypi: event port1=0x%02x,port2=0x%02x\n", v1, v2);

	if (useinput) {
		struct input_dev *input_jog_dev = &sonypi_device.input_jog_dev;
		struct input_dev *input_key_dev = &sonypi_device.input_key_dev;
		switch (event) {
		case SONYPI_EVENT_JOGDIAL_UP:
		case SONYPI_EVENT_JOGDIAL_UP_PRESSED:
			input_report_rel(input_jog_dev, REL_WHEEL, 1);
			break;
		case SONYPI_EVENT_JOGDIAL_DOWN:
		case SONYPI_EVENT_JOGDIAL_DOWN_PRESSED:
			input_report_rel(input_jog_dev, REL_WHEEL, -1);
			break;
		case SONYPI_EVENT_JOGDIAL_PRESSED: {
			int key = BTN_MIDDLE;
			input_report_key(input_jog_dev, key, 1);
			kfifo_put(sonypi_device.input_fifo,
				  (unsigned char *)&input_jog_dev,
				  sizeof(input_jog_dev));
			kfifo_put(sonypi_device.input_fifo,
				  (unsigned char *)&key, sizeof(key));
			break;
		}
		case SONYPI_EVENT_FNKEY_RELEASED:
			/* Nothing, not all VAIOs generate this event */
			break;
		}
		input_sync(input_jog_dev);

		for (i = 0; sonypi_inputkeys[i].sonypiev; i++) {
			int key;

			if (event != sonypi_inputkeys[i].sonypiev)
				continue;

			key = sonypi_inputkeys[i].inputev;
			input_report_key(input_key_dev, key, 1);
			kfifo_put(sonypi_device.input_fifo,
				  (unsigned char *)&input_key_dev,
				  sizeof(input_key_dev));
			kfifo_put(sonypi_device.input_fifo,
				  (unsigned char *)&key, sizeof(key));
		}
		input_sync(input_key_dev);
		schedule_work(&sonypi_device.input_work);
	}

	kfifo_put(sonypi_device.fifo, (unsigned char *)&event, sizeof(event));
	kill_fasync(&sonypi_device.fifo_async, SIGIO, POLL_IN);
	wake_up_interruptible(&sonypi_device.fifo_proc_list);

	return IRQ_HANDLED;
}

/* External camera command (exported to the motion eye v4l driver) */
int sonypi_camera_command(int command, u8 value)
{
	if (!camera)
		return -EIO;

	down(&sonypi_device.lock);

	switch (command) {
	case SONYPI_COMMAND_SETCAMERA:
		if (value)
			sonypi_camera_on();
		else
			sonypi_camera_off();
		break;
	case SONYPI_COMMAND_SETCAMERABRIGHTNESS:
		sonypi_set(SONYPI_CAMERA_BRIGHTNESS, value);
		break;
	case SONYPI_COMMAND_SETCAMERACONTRAST:
		sonypi_set(SONYPI_CAMERA_CONTRAST, value);
		break;
	case SONYPI_COMMAND_SETCAMERAHUE:
		sonypi_set(SONYPI_CAMERA_HUE, value);
		break;
	case SONYPI_COMMAND_SETCAMERACOLOR:
		sonypi_set(SONYPI_CAMERA_COLOR, value);
		break;
	case SONYPI_COMMAND_SETCAMERASHARPNESS:
		sonypi_set(SONYPI_CAMERA_SHARPNESS, value);
		break;
	case SONYPI_COMMAND_SETCAMERAPICTURE:
		sonypi_set(SONYPI_CAMERA_PICTURE, value);
		break;
	case SONYPI_COMMAND_SETCAMERAAGC:
		sonypi_set(SONYPI_CAMERA_AGC, value);
		break;
	default:
		printk(KERN_ERR "sonypi: sonypi_camera_command invalid: %d\n",
		       command);
		break;
	}
	up(&sonypi_device.lock);
	return 0;
}

EXPORT_SYMBOL(sonypi_camera_command);

static int sonypi_misc_fasync(int fd, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(fd, filp, on, &sonypi_device.fifo_async);
	if (retval < 0)
		return retval;
	return 0;
}

static int sonypi_misc_release(struct inode *inode, struct file *file)
{
	sonypi_misc_fasync(-1, file, 0);
	down(&sonypi_device.lock);
	sonypi_device.open_count--;
	up(&sonypi_device.lock);
	return 0;
}

static int sonypi_misc_open(struct inode *inode, struct file *file)
{
	down(&sonypi_device.lock);
	/* Flush input queue on first open */
	if (!sonypi_device.open_count)
		kfifo_reset(sonypi_device.fifo);
	sonypi_device.open_count++;
	up(&sonypi_device.lock);
	return 0;
}

static ssize_t sonypi_misc_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	ssize_t ret;
	unsigned char c;

	if ((kfifo_len(sonypi_device.fifo) == 0) &&
	    (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	ret = wait_event_interruptible(sonypi_device.fifo_proc_list,
				       kfifo_len(sonypi_device.fifo) != 0);
	if (ret)
		return ret;

	while (ret < count &&
	       (kfifo_get(sonypi_device.fifo, &c, sizeof(c)) == sizeof(c))) {
		if (put_user(c, buf++))
			return -EFAULT;
		ret++;
	}

	if (ret > 0) {
		struct inode *inode = file->f_dentry->d_inode;
		inode->i_atime = current_fs_time(inode->i_sb);
	}

	return ret;
}

static unsigned int sonypi_misc_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &sonypi_device.fifo_proc_list, wait);
	if (kfifo_len(sonypi_device.fifo))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int sonypi_misc_ioctl(struct inode *ip, struct file *fp,
			     unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	u8 val8;
	u16 val16;

	down(&sonypi_device.lock);
	switch (cmd) {
	case SONYPI_IOCGBRT:
		if (sonypi_ec_read(SONYPI_LCD_LIGHT, &val8)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSBRT:
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		if (sonypi_ec_write(SONYPI_LCD_LIGHT, val8))
			ret = -EIO;
		break;
	case SONYPI_IOCGBAT1CAP:
		if (ec_read16(SONYPI_BAT1_FULL, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT1REM:
		if (ec_read16(SONYPI_BAT1_LEFT, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT2CAP:
		if (ec_read16(SONYPI_BAT2_FULL, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT2REM:
		if (ec_read16(SONYPI_BAT2_LEFT, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBATFLAGS:
		if (sonypi_ec_read(SONYPI_BAT_FLAGS, &val8)) {
			ret = -EIO;
			break;
		}
		val8 &= 0x07;
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBLUE:
		val8 = sonypi_device.bluetooth_power;
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSBLUE:
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		sonypi_setbluetoothpower(val8);
		break;
	/* FAN Controls */
	case SONYPI_IOCGFAN:
		if (sonypi_ec_read(SONYPI_FAN0_STATUS, &val8)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user((u8 *)arg, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSFAN:
		if (copy_from_user(&val8, (u8 *)arg, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		if (sonypi_ec_write(SONYPI_FAN0_STATUS, val8))
			ret = -EIO;
		break;
	/* GET Temperature (useful under APM) */
	case SONYPI_IOCGTEMP:
		if (sonypi_ec_read(SONYPI_TEMP_STATUS, &val8)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user((u8 *)arg, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	default:
		ret = -EINVAL;
	}
	up(&sonypi_device.lock);
	return ret;
}

static struct file_operations sonypi_misc_fops = {
	.owner		= THIS_MODULE,
	.read		= sonypi_misc_read,
	.poll		= sonypi_misc_poll,
	.open		= sonypi_misc_open,
	.release	= sonypi_misc_release,
	.fasync		= sonypi_misc_fasync,
	.ioctl		= sonypi_misc_ioctl,
};

static struct miscdevice sonypi_misc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "sonypi",
	.fops		= &sonypi_misc_fops,
};

static void sonypi_enable(unsigned int camera_on)
{
	if (sonypi_device.model == SONYPI_DEVICE_MODEL_TYPE2)
		sonypi_type2_srs();
	else
		sonypi_type1_srs();

	sonypi_call1(0x82);
	sonypi_call2(0x81, 0xff);
	sonypi_call1(compat ? 0x92 : 0x82);

	/* Enable ACPI mode to get Fn key events */
	if (!SONYPI_ACPI_ACTIVE && fnkeyinit)
		outb(0xf0, 0xb2);

	if (camera && camera_on)
		sonypi_camera_on();
}

static int sonypi_disable(void)
{
	sonypi_call2(0x81, 0);	/* make sure we don't get any more events */
	if (camera)
		sonypi_camera_off();

	/* disable ACPI mode */
	if (!SONYPI_ACPI_ACTIVE && fnkeyinit)
		outb(0xf1, 0xb2);

	if (sonypi_device.model == SONYPI_DEVICE_MODEL_TYPE2)
		sonypi_type2_dis();
	else
		sonypi_type1_dis();
	return 0;
}

#ifdef CONFIG_PM
static int old_camera_power;

static int sonypi_suspend(struct device *dev, u32 state, u32 level)
{
	if (level == SUSPEND_DISABLE) {
		old_camera_power = sonypi_device.camera_power;
		sonypi_disable();
	}
	return 0;
}

static int sonypi_resume(struct device *dev, u32 level)
{
	if (level == RESUME_ENABLE)
		sonypi_enable(old_camera_power);
	return 0;
}
#endif

static void sonypi_shutdown(struct device *dev)
{
	sonypi_disable();
}

static struct device_driver sonypi_driver = {
	.name		= "sonypi",
	.bus		= &platform_bus_type,
#ifdef CONFIG_PM
	.suspend	= sonypi_suspend,
	.resume		= sonypi_resume,
#endif
	.shutdown	= sonypi_shutdown,
};

static int __devinit sonypi_probe(void)
{
	int i, ret;
	struct sonypi_ioport_list *ioport_list;
	struct sonypi_irq_list *irq_list;
	struct pci_dev *pcidev;

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_82371AB_3, NULL);

	sonypi_device.dev = pcidev;
	sonypi_device.model = pcidev ?
		SONYPI_DEVICE_MODEL_TYPE1 : SONYPI_DEVICE_MODEL_TYPE2;

	spin_lock_init(&sonypi_device.fifo_lock);
	sonypi_device.fifo = kfifo_alloc(SONYPI_BUF_SIZE, GFP_KERNEL,
					 &sonypi_device.fifo_lock);
	if (IS_ERR(sonypi_device.fifo)) {
		printk(KERN_ERR "sonypi: kfifo_alloc failed\n");
		ret = PTR_ERR(sonypi_device.fifo);
		goto out_fifo;
	}

	init_waitqueue_head(&sonypi_device.fifo_proc_list);
	init_MUTEX(&sonypi_device.lock);
	sonypi_device.bluetooth_power = -1;

	if (pcidev && pci_enable_device(pcidev)) {
		printk(KERN_ERR "sonypi: pci_enable_device failed\n");
		ret = -EIO;
		goto out_pcienable;
	}

	if (minor != -1)
		sonypi_misc_device.minor = minor;
	if ((ret = misc_register(&sonypi_misc_device))) {
		printk(KERN_ERR "sonypi: misc_register failed\n");
		goto out_miscreg;
	}

	if (sonypi_device.model == SONYPI_DEVICE_MODEL_TYPE2) {
		ioport_list = sonypi_type2_ioport_list;
		sonypi_device.region_size = SONYPI_TYPE2_REGION_SIZE;
		sonypi_device.evtype_offset = SONYPI_TYPE2_EVTYPE_OFFSET;
		irq_list = sonypi_type2_irq_list;
	} else {
		ioport_list = sonypi_type1_ioport_list;
		sonypi_device.region_size = SONYPI_TYPE1_REGION_SIZE;
		sonypi_device.evtype_offset = SONYPI_TYPE1_EVTYPE_OFFSET;
		irq_list = sonypi_type1_irq_list;
	}

	for (i = 0; ioport_list[i].port1; i++) {
		if (request_region(ioport_list[i].port1,
				   sonypi_device.region_size,
				   "Sony Programable I/O Device")) {
			/* get the ioport */
			sonypi_device.ioport1 = ioport_list[i].port1;
			sonypi_device.ioport2 = ioport_list[i].port2;
			break;
		}
	}
	if (!sonypi_device.ioport1) {
		printk(KERN_ERR "sonypi: request_region failed\n");
		ret = -ENODEV;
		goto out_reqreg;
	}

	for (i = 0; irq_list[i].irq; i++) {

		sonypi_device.irq = irq_list[i].irq;
		sonypi_device.bits = irq_list[i].bits;

		if (!request_irq(sonypi_device.irq, sonypi_irq,
				 SA_SHIRQ, "sonypi", sonypi_irq))
			break;
	}

	if (!irq_list[i].irq) {
		printk(KERN_ERR "sonypi: request_irq failed\n");
		ret = -ENODEV;
		goto out_reqirq;
	}

	if (useinput) {
		/* Initialize the Input Drivers: jogdial */
		int i;
		sonypi_device.input_jog_dev.evbit[0] =
			BIT(EV_KEY) | BIT(EV_REL);
		sonypi_device.input_jog_dev.keybit[LONG(BTN_MOUSE)] =
			BIT(BTN_MIDDLE);
		sonypi_device.input_jog_dev.relbit[0] = BIT(REL_WHEEL);
		sonypi_device.input_jog_dev.name =
			kmalloc(sizeof(SONYPI_JOG_INPUTNAME), GFP_KERNEL);
		if (!sonypi_device.input_jog_dev.name) {
			printk(KERN_ERR "sonypi: kmalloc failed\n");
			ret = -ENOMEM;
			goto out_inkmallocinput1;
		}
		sprintf(sonypi_device.input_jog_dev.name, SONYPI_JOG_INPUTNAME);
		sonypi_device.input_jog_dev.id.bustype = BUS_ISA;
		sonypi_device.input_jog_dev.id.vendor = PCI_VENDOR_ID_SONY;

		input_register_device(&sonypi_device.input_jog_dev);
		printk(KERN_INFO "%s input method installed.\n",
		       sonypi_device.input_jog_dev.name);

		/* Initialize the Input Drivers: special keys */
		sonypi_device.input_key_dev.evbit[0] = BIT(EV_KEY);
		for (i = 0; sonypi_inputkeys[i].sonypiev; i++)
			if (sonypi_inputkeys[i].inputev)
				set_bit(sonypi_inputkeys[i].inputev,
					sonypi_device.input_key_dev.keybit);
		sonypi_device.input_key_dev.name =
			kmalloc(sizeof(SONYPI_KEY_INPUTNAME), GFP_KERNEL);
		if (!sonypi_device.input_key_dev.name) {
			printk(KERN_ERR "sonypi: kmalloc failed\n");
			ret = -ENOMEM;
			goto out_inkmallocinput2;
		}
		sprintf(sonypi_device.input_key_dev.name, SONYPI_KEY_INPUTNAME);
		sonypi_device.input_key_dev.id.bustype = BUS_ISA;
		sonypi_device.input_key_dev.id.vendor = PCI_VENDOR_ID_SONY;

		input_register_device(&sonypi_device.input_key_dev);
		printk(KERN_INFO "%s input method installed.\n",
		       sonypi_device.input_key_dev.name);

		spin_lock_init(&sonypi_device.input_fifo_lock);
		sonypi_device.input_fifo =
			kfifo_alloc(SONYPI_BUF_SIZE, GFP_KERNEL,
				    &sonypi_device.input_fifo_lock);
		if (IS_ERR(sonypi_device.input_fifo)) {
			printk(KERN_ERR "sonypi: kfifo_alloc failed\n");
			ret = PTR_ERR(sonypi_device.input_fifo);
			goto out_infifo;
		}

		INIT_WORK(&sonypi_device.input_work, input_keyrelease, NULL);
	}

	sonypi_device.pdev = platform_device_register_simple("sonypi", -1,
							     NULL, 0);
	if (IS_ERR(sonypi_device.pdev)) {
		ret = PTR_ERR(sonypi_device.pdev);
		goto out_platformdev;
	}

	sonypi_enable(0);

	printk(KERN_INFO "sonypi: Sony Programmable I/O Controller Driver"
	       "v%s.\n", SONYPI_DRIVER_VERSION);
	printk(KERN_INFO "sonypi: detected %s model, "
	       "verbose = %d, fnkeyinit = %s, camera = %s, "
	       "compat = %s, mask = 0x%08lx, useinput = %s, acpi = %s\n",
	       (sonypi_device.model == SONYPI_DEVICE_MODEL_TYPE1) ?
			"type1" : "type2",
	       verbose,
	       fnkeyinit ? "on" : "off",
	       camera ? "on" : "off",
	       compat ? "on" : "off",
	       mask,
	       useinput ? "on" : "off",
	       SONYPI_ACPI_ACTIVE ? "on" : "off");
	printk(KERN_INFO "sonypi: enabled at irq=%d, port1=0x%x, port2=0x%x\n",
	       sonypi_device.irq,
	       sonypi_device.ioport1, sonypi_device.ioport2);

	if (minor == -1)
		printk(KERN_INFO "sonypi: device allocated minor is %d\n",
		       sonypi_misc_device.minor);

	return 0;

out_platformdev:
	kfifo_free(sonypi_device.input_fifo);
out_infifo:
	input_unregister_device(&sonypi_device.input_key_dev);
	kfree(sonypi_device.input_key_dev.name);
out_inkmallocinput2:
	input_unregister_device(&sonypi_device.input_jog_dev);
	kfree(sonypi_device.input_jog_dev.name);
out_inkmallocinput1:
	free_irq(sonypi_device.irq, sonypi_irq);
out_reqirq:
	release_region(sonypi_device.ioport1, sonypi_device.region_size);
out_reqreg:
	misc_deregister(&sonypi_misc_device);
out_miscreg:
	if (pcidev)
		pci_disable_device(pcidev);
out_pcienable:
	kfifo_free(sonypi_device.fifo);
out_fifo:
	pci_dev_put(sonypi_device.dev);
	return ret;
}

static void __devexit sonypi_remove(void)
{
	sonypi_disable();

	platform_device_unregister(sonypi_device.pdev);

	if (useinput) {
		input_unregister_device(&sonypi_device.input_key_dev);
		kfree(sonypi_device.input_key_dev.name);
		input_unregister_device(&sonypi_device.input_jog_dev);
		kfree(sonypi_device.input_jog_dev.name);
		kfifo_free(sonypi_device.input_fifo);
	}

	free_irq(sonypi_device.irq, sonypi_irq);
	release_region(sonypi_device.ioport1, sonypi_device.region_size);
	misc_deregister(&sonypi_misc_device);
	if (sonypi_device.dev)
		pci_disable_device(sonypi_device.dev);
	kfifo_free(sonypi_device.fifo);
	pci_dev_put(sonypi_device.dev);
	printk(KERN_INFO "sonypi: removed.\n");
}

static struct dmi_system_id __initdata sonypi_dmi_table[] = {
	{
		.ident = "Sony Vaio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PCG-"),
		},
	},
	{
		.ident = "Sony Vaio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-"),
		},
	},
	{ }
};

static int __init sonypi_init(void)
{
	int ret;

	if (!dmi_check_system(sonypi_dmi_table))
		return -ENODEV;

	ret = driver_register(&sonypi_driver);
	if (ret)
		return ret;

	ret = sonypi_probe();
	if (ret)
		driver_unregister(&sonypi_driver);

	return ret;
}

static void __exit sonypi_exit(void)
{
	driver_unregister(&sonypi_driver);
	sonypi_remove();
}

module_init(sonypi_init);
module_exit(sonypi_exit);

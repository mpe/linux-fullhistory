/*
 *  kernel/chr_drv/vt.c
 *
 *  (C) 1992 obz under the linux copyright
 */

#include <errno.h>

#include <sys/types.h>
#include <sys/kd.h>
#include <sys/vt.h>

#include <asm/io.h>
#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>

#include "vt_kern.h"

/*
 * console (vt and kd) routines, as defined by usl svr4 manual
 */

struct vt_info vt_info[MAX_CONSOLES];

extern int NR_CONSOLES;
extern unsigned char kleds;
extern unsigned char kraw;
extern unsigned char ke0;

extern int sys_ioperm(unsigned long from, unsigned long num, int on);
extern void set_leds(void);

/*
 * these are the valid i/o ports we're allowed to change. they map all the
 * video ports
 */
#define GPFIRST 0x3b4
#define GPLAST 0x3df
#define GPNUM (GPLAST - GPFIRST + 1)

/*
 * turns on sound of some freq. 0 turns it off.
 * stolen from console.c, so i'm not sure if its the correct interpretation
 */
static int
kiocsound(unsigned int freq)
{
	if (freq == 0) {
		/* disable counter 2 */
		outb(inb_p(0x61)&0xFC, 0x61);
	}
	else {
		/* enable counter 2 */
		outb_p(inb_p(0x61)|3, 0x61);
		/* set command for counter 2, 2 byte write */
		outb_p(0xB6, 0x43);
		/* select desired HZ */
		outb_p(freq & 0xff, 0x42);
		outb((freq >> 8) & 0xff, 0x42);
	}
	return 0;
}

int
vt_ioctl(struct tty_struct *tty, int dev, int cmd, int arg)
{
	switch (cmd) {
	case KIOCSOUND:
		return kiocsound((unsigned int)arg);

	case KDGKBTYPE:
		verify_area((void *) arg, sizeof(unsigned char));
		put_fs_byte(KB_101, (unsigned char *) arg);
		return 0;

	case KDADDIO:
	case KDDELIO:
		/*
		 * KDADDIO and KDDELIO may be able to add ports beyond what
		 * we reject here, but to be safe...
		 */
		if (arg < GPFIRST || arg > GPLAST)
			return -EINVAL;
		return sys_ioperm(arg, 1, (cmd == KDADDIO)) ? -ENXIO : 0;

	case KDENABIO:
	case KDDISABIO:
		return sys_ioperm(GPFIRST, GPNUM, (cmd == KDENABIO)) ? -ENXIO : 0;

	case KDSETMODE:
		/*
		 * currently, setting the mode from KD_TEXT to KD_GRAPHICS
		 * doesn't do a whole lot. i'm not sure if it should do any
		 * restoration of modes or what...
		 */
		switch (arg) {
		case KD_GRAPHICS:
			break;
		case KD_TEXT0:
		case KD_TEXT1:
			arg = KD_TEXT;
		case KD_TEXT:
			break;
		default:
			return -EINVAL;
		}
		vt_info[fg_console].mode = arg;
		return 0;
	case KDGETMODE:
		verify_area((void *) arg, sizeof(unsigned long));
		put_fs_long(vt_info[fg_console].mode, (unsigned long *) arg);
		return 0;

	case KDMAPDISP:
	case KDUNMAPDISP:
		/*
		 * these work like a combination of mmap and KDENABIO.
		 * this could be easily finished.
		 */
		return -EINVAL;

	case KDSKBMODE:
		if (arg == K_RAW) {
			kraw = 1;
			ke0 = 0;
		}
		else if (arg == K_XLATE) {
			kraw = 0;
		}
		else
			return -EINVAL;
		return 0;
	case KDGKBMODE:
		verify_area((void *) arg, sizeof(unsigned long));
		put_fs_long(kraw ? K_RAW : K_XLATE, (unsigned long *) arg);
		return 0;

	case KDGETLED:
		verify_area((void *) arg, sizeof(unsigned char));
		put_fs_byte((((kleds & 1) ? LED_SCR : 0) |
			     ((kleds & 2) ? LED_NUM : 0) |
			     ((kleds & 4) ? LED_CAP : 0)),
			    (unsigned char *) arg);
		return 0;
	case KDSETLED:
		if (arg & ~7)
			return -EINVAL;
		kleds = (((arg & LED_SCR) ? 1 : 0) |
			 ((arg & LED_NUM) ? 2 : 0) |
			 ((arg & LED_CAP) ? 4 : 0));
		set_leds();
		return 0;

	default:
		return -EINVAL;
	}
}

void
vt_init(void)
{
	int i;

	for (i = 0; i < NR_CONSOLES; ++i) {
		vt_info[i].mode = KD_TEXT;
	}
}

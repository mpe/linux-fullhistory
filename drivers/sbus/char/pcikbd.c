/* $Id: pcikbd.c,v 1.24 1998/11/08 11:15:24 davem Exp $
 * pcikbd.c: Ultra/AX PC keyboard support.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * JavaStation(MrCoffee) support by Pete A. Zaitcev.
 *
 * This code is mainly put together from various places in
 * drivers/char, please refer to these sources for credits
 * to the original authors.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/miscdevice.h>
#include <linux/kbd_ll.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/ebus.h>
#include <asm/oplib.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#ifdef __sparc_v9__
#define	PCI_KB_NAME	"kb_ps2"
#define PCI_MS_NAME	"kdmouse"
#else
#define PCI_KB_NAME	"keyboard"
#define PCI_MS_NAME	"mouse"
#endif

#include "pcikbd.h"
#include "sunserial.h"

#ifndef __sparc_v9__
static int pcikbd_mrcoffee = 0;
#else
#define pcikbd_mrcoffee 0
#endif

static unsigned long pcikbd_iobase = 0;
static unsigned int pcikbd_irq = 0;

/* used only by send_data - set by keyboard_interrupt */
static volatile unsigned char reply_expected = 0;
static volatile unsigned char acknowledge = 0;
static volatile unsigned char resend = 0;

unsigned char pckbd_read_mask = KBD_STAT_OBF;

extern int pcikbd_init(void);
extern void pci_compute_shiftstate(void);
extern int pci_setkeycode(unsigned int, unsigned int);
extern int pci_getkeycode(unsigned int);
extern void pci_setledstate(struct kbd_struct *, unsigned int);
extern unsigned char pci_getledstate(void);

#ifdef __sparc_v9__

static __inline__ unsigned char pcikbd_inb(unsigned long port)
{
	return inb(port);
}

static __inline__ void pcikbd_outb(unsigned char val, unsigned long port)
{
	outb(val, port);
}

#else

static __inline__ unsigned char pcikbd_inb(unsigned long port)
{
	return *(volatile unsigned char *)port;
}

static __inline__ void pcikbd_outb(unsigned char val, unsigned long port)
{
	*(volatile unsigned char *)port = val;
}

#endif

static inline void kb_wait(void)
{
	unsigned long start = jiffies;

	do {
		if(!(pcikbd_inb(pcikbd_iobase + KBD_STATUS_REG) & KBD_STAT_IBF))
			return;
	} while (jiffies - start < KBC_TIMEOUT);
}

/*
 * Translation of escaped scancodes to keycodes.
 * This is now user-settable.
 * The keycodes 1-88,96-111,119 are fairly standard, and
 * should probably not be changed - changing might confuse X.
 * X also interprets scancode 0x5d (KEY_Begin).
 *
 * For 1-88 keycode equals scancode.
 */

#define E0_KPENTER 96
#define E0_RCTRL   97
#define E0_KPSLASH 98
#define E0_PRSCR   99
#define E0_RALT    100
#define E0_BREAK   101  /* (control-pause) */
#define E0_HOME    102
#define E0_UP      103
#define E0_PGUP    104
#define E0_LEFT    105
#define E0_RIGHT   106
#define E0_END     107
#define E0_DOWN    108
#define E0_PGDN    109
#define E0_INS     110
#define E0_DEL     111

#define E1_PAUSE   119

/*
 * The keycodes below are randomly located in 89-95,112-118,120-127.
 * They could be thrown away (and all occurrences below replaced by 0),
 * but that would force many users to use the `setkeycodes' utility, where
 * they needed not before. It does not matter that there are duplicates, as
 * long as no duplication occurs for any single keyboard.
 */
#define SC_LIM 89

#define FOCUS_PF1 85           /* actual code! */
#define FOCUS_PF2 89
#define FOCUS_PF3 90
#define FOCUS_PF4 91
#define FOCUS_PF5 92
#define FOCUS_PF6 93
#define FOCUS_PF7 94
#define FOCUS_PF8 95
#define FOCUS_PF9 120
#define FOCUS_PF10 121
#define FOCUS_PF11 122
#define FOCUS_PF12 123

#define JAP_86     124
/* tfj@olivia.ping.dk:
 * The four keys are located over the numeric keypad, and are
 * labelled A1-A4. It's an rc930 keyboard, from
 * Regnecentralen/RC International, Now ICL.
 * Scancodes: 59, 5a, 5b, 5c.
 */
#define RGN1 124
#define RGN2 125
#define RGN3 126
#define RGN4 127

static unsigned char high_keys[128 - SC_LIM] = {
  RGN1, RGN2, RGN3, RGN4, 0, 0, 0,                   /* 0x59-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,                            /* 0x60-0x67 */
  0, 0, 0, 0, 0, FOCUS_PF11, 0, FOCUS_PF12,          /* 0x68-0x6f */
  0, 0, 0, FOCUS_PF2, FOCUS_PF9, 0, 0, FOCUS_PF3,    /* 0x70-0x77 */
  FOCUS_PF4, FOCUS_PF5, FOCUS_PF6, FOCUS_PF7,        /* 0x78-0x7b */
  FOCUS_PF8, JAP_86, FOCUS_PF10, 0                   /* 0x7c-0x7f */
};

/* BTC */
#define E0_MACRO   112
/* LK450 */
#define E0_F13     113
#define E0_F14     114
#define E0_HELP    115
#define E0_DO      116
#define E0_F17     117
#define E0_KPMINPLUS 118
/*
 * My OmniKey generates e0 4c for  the "OMNI" key and the
 * right alt key does nada. [kkoller@nyx10.cs.du.edu]
 */
#define E0_OK	124
/*
 * New microsoft keyboard is rumoured to have
 * e0 5b (left window button), e0 5c (right window button),
 * e0 5d (menu button). [or: LBANNER, RBANNER, RMENU]
 * [or: Windows_L, Windows_R, TaskMan]
 */
#define E0_MSLW	125
#define E0_MSRW	126
#define E0_MSTM	127

static unsigned char e0_keys[128] = {
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x00-0x07 */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x08-0x0f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x10-0x17 */
  0, 0, 0, 0, E0_KPENTER, E0_RCTRL, 0, 0,	      /* 0x18-0x1f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x20-0x27 */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x28-0x2f */
  0, 0, 0, 0, 0, E0_KPSLASH, 0, E0_PRSCR,	      /* 0x30-0x37 */
  E0_RALT, 0, 0, 0, 0, E0_F13, E0_F14, E0_HELP,	      /* 0x38-0x3f */
  E0_DO, E0_F17, 0, 0, 0, 0, E0_BREAK, E0_HOME,	      /* 0x40-0x47 */
  E0_UP, E0_PGUP, 0, E0_LEFT, E0_OK, E0_RIGHT, E0_KPMINPLUS, E0_END,/* 0x48-0x4f */
  E0_DOWN, E0_PGDN, E0_INS, E0_DEL, 0, 0, 0, 0,	      /* 0x50-0x57 */
  0, 0, 0, E0_MSLW, E0_MSRW, E0_MSTM, 0, 0,	      /* 0x58-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x60-0x67 */
  0, 0, 0, 0, 0, 0, 0, E0_MACRO,		      /* 0x68-0x6f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x70-0x77 */
  0, 0, 0, 0, 0, 0, 0, 0			      /* 0x78-0x7f */
};

/* Simple translation table for the SysRq keys */

#ifdef CONFIG_MAGIC_SYSRQ
unsigned char pcikbd_sysrq_xlate[128] =
	"\000\0331234567890-=\177\t"			/* 0x00 - 0x0f */
	"qwertyuiop[]\r\000as"				/* 0x10 - 0x1f */
	"dfghjkl;'`\000\\zxcv"				/* 0x20 - 0x2f */
	"bnm,./\000*\000 \000\201\202\203\204\205"	/* 0x30 - 0x3f */
	"\206\207\210\211\212\000\000789-456+1"		/* 0x40 - 0x4f */
	"230\177\000\000\213\214\000\000\000\000\000\000\000\000\000\000" /* 0x50 - 0x5f */
	"\r\000/";					/* 0x60 - 0x6f */
#endif

static unsigned int prev_scancode = 0;

int pcikbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	if(scancode < SC_LIM || scancode > 255 || keycode > 127)
		return -EINVAL;
	if(scancode < 128)
		high_keys[scancode - SC_LIM] = keycode;
	else
		e0_keys[scancode - 128] = keycode;
	return 0;
}

int pcikbd_getkeycode(unsigned int scancode)
{
	return
		(scancode < SC_LIM || scancode > 255) ? -EINVAL :
		(scancode < 128) ? high_keys[scancode - SC_LIM] :
		e0_keys[scancode - 128];
}

int do_acknowledge(unsigned char scancode)
{
	if(reply_expected) {
		if(scancode == KBD_REPLY_ACK) {
			acknowledge = 1;
			reply_expected = 0;
			return 0;
		} else if(scancode == KBD_REPLY_RESEND) {
			resend = 1;
			reply_expected = 0;
			return 0;
		}
	}
	if(scancode == 0) {
		prev_scancode = 0;
		return 0;
	}
	return 1;
}

int pcikbd_pretranslate(unsigned char scancode, char raw_mode)
{
	if(scancode == 0xff) {
		prev_scancode = 0;
		return 0;
	}
	if(scancode == 0xe0 || scancode == 0xe1) {
		prev_scancode = scancode;
		return 0;
	}
	return 1;
}

int pcikbd_translate(unsigned char scancode, unsigned char *keycode,
		     char raw_mode)
{
	if(prev_scancode) {
		if(prev_scancode != 0xe0) {
			if(prev_scancode == 0xe1 && scancode == 0x1d) {
				prev_scancode = 0x100;
				return 0;
			} else if(prev_scancode == 0x100 && scancode == 0x45) {
				*keycode = E1_PAUSE;
				prev_scancode = 0;
			} else {
				prev_scancode = 0;
				return 0;
			}
		} else {
			prev_scancode = 0;
			if(scancode == 0x2a || scancode == 0x36)
				return 0;
			if(e0_keys[scancode])
				*keycode = e0_keys[scancode];
			else
				return 0;
		}
	} else if(scancode >= SC_LIM) {
		*keycode = high_keys[scancode - SC_LIM];
		if(!*keycode)
			return 0;

	} else
		*keycode = scancode;
	return 1;
}

char pcikbd_unexpected_up(unsigned char keycode)
{
	if(keycode >= SC_LIM || keycode == 85)
		return 0;
	else
		return 0200;
}

static void
pcikbd_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char status;

	kbd_pt_regs = regs;
	status = pcikbd_inb(pcikbd_iobase + KBD_STATUS_REG);
	do {
		unsigned char scancode;

		if(status & pckbd_read_mask & KBD_STAT_MOUSE_OBF)
			break;
		scancode = pcikbd_inb(pcikbd_iobase + KBD_DATA_REG);
		if((status & KBD_STAT_OBF) && do_acknowledge(scancode))
			handle_scancode(scancode);
		status = pcikbd_inb(pcikbd_iobase + KBD_STATUS_REG);
	} while(status & KBD_STAT_OBF);
	mark_bh(KEYBOARD_BH);
}

static int send_data(unsigned char data)
{
	int retries = 3;
	unsigned long start;

	do {
		kb_wait();
		acknowledge = resend = 0;
		reply_expected = 1;
		pcikbd_outb(data, pcikbd_iobase + KBD_DATA_REG);
		start = jiffies;
		do {
			if(acknowledge)
				return 1;
			if(jiffies - start >= KBD_TIMEOUT)
				return 0;
		} while(!resend);
	} while(retries-- > 0);
	return 0;
}

void pcikbd_leds(unsigned char leds)
{
	if(!send_data(KBD_CMD_SET_LEDS) || !send_data(leds))
		send_data(KBD_CMD_ENABLE);
		
}

__initfunc(static int pcikbd_wait_for_input(void))
{
	int status, data;
	unsigned long start = jiffies;

	do {
		status = pcikbd_inb(pcikbd_iobase + KBD_STATUS_REG);
		if(!(status & KBD_STAT_OBF))
			continue;
		data = pcikbd_inb(pcikbd_iobase + KBD_DATA_REG);
		if(status & (KBD_STAT_GTO | KBD_STAT_PERR))
			continue;
		return (data & 0xff);
	} while(jiffies - start < KBD_INIT_TIMEOUT);
	return -1;
}

__initfunc(static void pcikbd_write(int address, int data))
{
	int status;

	do {
		status = pcikbd_inb(pcikbd_iobase + KBD_STATUS_REG);
	} while (status & KBD_STAT_IBF);
	pcikbd_outb(data, pcikbd_iobase + address);
}

#ifdef __sparc_v9__

static unsigned long pcibeep_iobase = 0;

/* Timer routine to turn off the beep after the interval expires. */
static void pcikbd_kd_nosound(unsigned long __unused)
{
	outl(0, pcibeep_iobase);
}

/*
 * Initiate a keyboard beep. If the frequency is zero, then we stop
 * the beep. Any other frequency will start a monotone beep. The beep
 * will be stopped by a timer after "ticks" jiffies. If ticks is 0,
 * then we do not start a timer.
 */
static void pcikbd_kd_mksound(unsigned int hz, unsigned int ticks)
{
	unsigned long flags;
	static struct timer_list sound_timer = { NULL, NULL, 0, 0,
						 pcikbd_kd_nosound };

	save_flags(flags); cli();
	del_timer(&sound_timer);
	if (hz) {
		outl(1, pcibeep_iobase);
		if (ticks) {
			sound_timer.expires = jiffies + ticks;
			add_timer(&sound_timer);
		}
	} else
		outl(0, pcibeep_iobase);
	restore_flags(flags);
}
#endif

static void nop_kd_mksound(unsigned int hz, unsigned int ticks)
{
}

extern void (*kd_mksound)(unsigned int hz, unsigned int ticks);

__initfunc(static char *do_pcikbd_init_hw(void))
{

	while(pcikbd_wait_for_input() != -1)
		;

	pcikbd_write(KBD_CNTL_REG, KBD_CCMD_SELF_TEST);
	if(pcikbd_wait_for_input() != 0x55)
		return "Keyboard failed self test";

	pcikbd_write(KBD_CNTL_REG, KBD_CCMD_KBD_TEST);
	if(pcikbd_wait_for_input() != 0x00)
		return "Keyboard interface failed self test";

	pcikbd_write(KBD_CNTL_REG, KBD_CCMD_KBD_ENABLE);
	pcikbd_write(KBD_DATA_REG, KBD_CMD_RESET);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Keyboard reset failed, no ACK";
	if(pcikbd_wait_for_input() != KBD_REPLY_POR)
		return "Keyboard reset failed, no ACK";

	pcikbd_write(KBD_DATA_REG, KBD_CMD_DISABLE);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Disable keyboard: no ACK";

	pcikbd_write(KBD_CNTL_REG, KBD_CCMD_WRITE_MODE);
	pcikbd_write(KBD_DATA_REG,
		     (KBD_MODE_KBD_INT | KBD_MODE_SYS |
		      KBD_MODE_DISABLE_MOUSE | KBD_MODE_KCC));
	pcikbd_write(KBD_DATA_REG, KBD_CMD_ENABLE);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Enable keyboard: no ACK";

	pcikbd_write(KBD_DATA_REG, KBD_CMD_SET_RATE);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Set rate: no ACK";
	pcikbd_write(KBD_DATA_REG, 0x00);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Set rate: no ACK";

	return NULL; /* success */
}

__initfunc(void pcikbd_init_hw(void))
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
	struct linux_ebus_child *child;
	char *msg;

	if (pcikbd_mrcoffee) {
		if ((pcikbd_iobase = (unsigned long) sparc_alloc_io(0x71300060,
		    0, 8, "ps2kbd-regs", 0x0, 0)) == 0) {
			prom_printf("pcikbd_init_hw: cannot map\n");
			return;
		}
		pcikbd_irq = 13 | 0x20;
		if (request_irq(pcikbd_irq, &pcikbd_interrupt,
				SA_SHIRQ, "keyboard", (void *)pcikbd_iobase)) {
			printk("8042: cannot register IRQ %x\n", pcikbd_irq);
			return;
		}
		printk("8042(kbd): iobase[%08x] irq[%x]\n",
		    (unsigned)pcikbd_iobase, pcikbd_irq);
	} else {
		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if(!strcmp(edev->prom_name, "8042")) {
					for_each_edevchild(edev, child) {
                                                if (!strcmp(child->prom_name, PCI_KB_NAME))
							goto found;
					}
				}
			}
		}
		printk("pcikbd_init_hw: no 8042 found\n");
		return;

found:
		pcikbd_iobase = child->base_address[0];
		if (check_region(pcikbd_iobase, sizeof(unsigned long))) {
			printk("8042: can't get region %lx, %d\n",
			       pcikbd_iobase, (int)sizeof(unsigned long));
			return;
		}
		request_region(pcikbd_iobase, sizeof(unsigned long), "8042 controller");

		pcikbd_irq = child->irqs[0];
		if (request_irq(pcikbd_irq, &pcikbd_interrupt,
				SA_SHIRQ, "keyboard", (void *)pcikbd_iobase)) {
			printk("8042: cannot register IRQ %s\n",
			       __irq_itoa(pcikbd_irq));
			return;
		}

		printk("8042(kbd) at 0x%lx (irq %s)\n", pcikbd_iobase,
		       __irq_itoa(pcikbd_irq));
	}

	kd_mksound = nop_kd_mksound;

#ifdef __sparc_v9__
	edev = 0;
	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if(!strcmp(edev->prom_name, "beeper"))
				goto ebus_done;
		}
	}
ebus_done:

	/*
	 * XXX: my 3.1.3 PROM does not give me the beeper node for the audio
	 *      auxio register, though I know it is there... (ecd)
	 *
	 * Both JE1 & MrCoffe have no beeper. How about Krups? --zaitcev
	 */
	if (!edev)
		pcibeep_iobase = (pcikbd_iobase & ~(0xffffff)) | 0x722000;
	else
		pcibeep_iobase = edev->base_address[0];

	if (check_region(pcibeep_iobase, sizeof(unsigned int))) {
		printk("8042: can't get region %lx, %d\n",
		       pcibeep_iobase, (int)sizeof(unsigned int));
	} else {
		request_region(pcibeep_iobase, sizeof(unsigned int), "speaker");
		kd_mksound = pcikbd_kd_mksound;
		printk("8042(speaker): iobase[%016lx]%s\n", pcibeep_iobase,
		       edev ? "" : " (forced)");
	}
#endif

	disable_irq(pcikbd_irq);
	msg = do_pcikbd_init_hw();
	enable_irq(pcikbd_irq);

	if(msg)
		printk("8042: keyboard init failure [%s]\n", msg);
}



/*
 * Here begins the Mouse Driver.
 */

static unsigned long pcimouse_iobase = 0;
static unsigned int pcimouse_irq;

#define AUX_BUF_SIZE	2048

struct aux_queue {
	unsigned long head;
	unsigned long tail;
	struct wait_queue *proc_list;
	struct fasync_struct *fasync;
	unsigned char buf[AUX_BUF_SIZE];
};

static struct aux_queue *queue;
static int aux_ready = 0;
static int aux_count = 0;
static int aux_present = 0;

#ifdef __sparc_v9__

static __inline__ unsigned char pcimouse_inb(unsigned long port)
{
	return inb(port);
}

static __inline__ void pcimouse_outb(unsigned char val, unsigned long port)
{
	outb(val, port);
}

#else

static __inline__ unsigned char pcimouse_inb(unsigned long port)
{
	return *(volatile unsigned char *)port;
}

static __inline__ void pcimouse_outb(unsigned char val, unsigned long port)
{
	*(volatile unsigned char *)port = val;
}

#endif

/*
 *	Shared subroutines
 */

static unsigned int get_from_queue(void)
{
	unsigned int result;
	unsigned long flags;

	save_flags(flags);
	cli();
	result = queue->buf[queue->tail];
	queue->tail = (queue->tail + 1) & (AUX_BUF_SIZE-1);
	restore_flags(flags);
	return result;
}


static inline int queue_empty(void)
{
	return queue->head == queue->tail;
}

static int aux_fasync(int fd, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(fd, filp, on, &queue->fasync);
	if (retval < 0)
		return retval;
	return 0;
}

/*
 *	PS/2 Aux Device
 */

#define AUX_INTS_OFF	(KBD_MODE_KCC | KBD_MODE_DISABLE_MOUSE | \
			 KBD_MODE_SYS | KBD_MODE_KBD_INT)

#define AUX_INTS_ON	(KBD_MODE_KCC | KBD_MODE_SYS | \
			 KBD_MODE_MOUSE_INT | KBD_MODE_KBD_INT)

#define MAX_RETRIES	60		/* some aux operations take long time*/

/*
 *	Status polling
 */

static int poll_aux_status(void)
{
	int retries=0;

	while ((pcimouse_inb(pcimouse_iobase + KBD_STATUS_REG) &
		(KBD_STAT_IBF | KBD_STAT_OBF)) && retries < MAX_RETRIES) {
 		if ((pcimouse_inb(pcimouse_iobase + KBD_STATUS_REG) & AUX_STAT_OBF)
		    == AUX_STAT_OBF)
			pcimouse_inb(pcimouse_iobase + KBD_DATA_REG);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout((5*HZ + 99) / 100);
		retries++;
	}
	return (retries < MAX_RETRIES);
}

/*
 * Write to aux device
 */

static void aux_write_dev(int val)
{
	poll_aux_status();
	pcimouse_outb(KBD_CCMD_WRITE_MOUSE, pcimouse_iobase + KBD_CNTL_REG);/* Write magic cookie */
	poll_aux_status();
	pcimouse_outb(val, pcimouse_iobase + KBD_DATA_REG);		 /* Write data */
	udelay(1);
}

/*
 * Write to device & handle returned ack
 */

__initfunc(static int aux_write_ack(int val))
{
	aux_write_dev(val);
	poll_aux_status();

	if ((pcimouse_inb(pcimouse_iobase + KBD_STATUS_REG) & AUX_STAT_OBF) == AUX_STAT_OBF)
		return (pcimouse_inb(pcimouse_iobase + KBD_DATA_REG));
	return 0;
}

/*
 * Write aux device command
 */

static void aux_write_cmd(int val)
{
	poll_aux_status();
	pcimouse_outb(KBD_CCMD_WRITE_MODE, pcimouse_iobase + KBD_CNTL_REG);
	poll_aux_status();
	pcimouse_outb(val, pcimouse_iobase + KBD_DATA_REG);
}

/*
 * AUX handler critical section start and end.
 * 
 * Only one process can be in the critical section and all keyboard sends are
 * deferred as long as we're inside. This is necessary as we may sleep when
 * waiting for the keyboard controller and other processes / BH's can
 * preempt us. Please note that the input buffer must be flushed when
 * aux_end_atomic() is called and the interrupt is no longer enabled as not
 * doing so might cause the keyboard driver to ignore all incoming keystrokes.
 */

static struct semaphore aux_sema4 = MUTEX;

static inline void aux_start_atomic(void)
{
	down(&aux_sema4);
	disable_bh(KEYBOARD_BH);
}

static inline void aux_end_atomic(void)
{
	enable_bh(KEYBOARD_BH);
	up(&aux_sema4);
}

/*
 * Interrupt from the auxiliary device: a character
 * is waiting in the keyboard/aux controller.
 */

void pcimouse_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int head = queue->head;
	int maxhead = (queue->tail-1) & (AUX_BUF_SIZE-1);

	if ((pcimouse_inb(pcimouse_iobase + KBD_STATUS_REG) & AUX_STAT_OBF) != AUX_STAT_OBF)
		return;

	add_mouse_randomness(queue->buf[head] = pcimouse_inb(pcimouse_iobase + KBD_DATA_REG));
	if (head != maxhead) {
		head++;
		head &= AUX_BUF_SIZE-1;
	}
	queue->head = head;
	aux_ready = 1;
	if (queue->fasync)
		kill_fasync(queue->fasync, SIGIO);
	wake_up_interruptible(&queue->proc_list);
}

static int aux_release(struct inode * inode, struct file * file)
{
	aux_fasync(-1, file, 0);
	if (--aux_count)
		return 0;
	aux_start_atomic();

	/* Disable controller ints */
	aux_write_cmd(AUX_INTS_OFF);
	poll_aux_status();

	/* Disable Aux device */
	pcimouse_outb(KBD_CCMD_MOUSE_DISABLE, pcimouse_iobase + KBD_CNTL_REG);
	poll_aux_status();
	aux_end_atomic();

	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Install interrupt handler.
 * Enable auxiliary device.
 */

static int aux_open(struct inode * inode, struct file * file)
{
	if (!aux_present)
		return -ENODEV;

	aux_start_atomic();
	if (aux_count++) {
		aux_end_atomic();
		return 0;
	}
	if (!poll_aux_status()) {		/* FIXME: Race condition */
		aux_count--;
		aux_end_atomic();
		return -EBUSY;
	}
	queue->head = queue->tail = 0;		/* Flush input queue */

	MOD_INC_USE_COUNT;

	poll_aux_status();
	pcimouse_outb(KBD_CCMD_MOUSE_ENABLE, pcimouse_iobase+KBD_CNTL_REG);    /* Enable Aux */
	aux_write_dev(AUX_ENABLE_DEV);			    /* Enable aux device */
	aux_write_cmd(AUX_INTS_ON);			    /* Enable controller ints */
	poll_aux_status();
	aux_end_atomic();

	aux_ready = 0;
	return 0;
}

/*
 * Write to the aux device.
 */

static ssize_t aux_write(struct file * file, const char * buffer,
			 size_t count, loff_t *ppos)
{
	ssize_t retval = 0;

	if (count) {
		ssize_t written = 0;

		aux_start_atomic();
		do {
			char c;
			if (!poll_aux_status())
				break;
			pcimouse_outb(KBD_CCMD_WRITE_MOUSE, pcimouse_iobase + KBD_CNTL_REG);
			if (!poll_aux_status())
				break;
			get_user(c, buffer++);
			pcimouse_outb(c, pcimouse_iobase + KBD_DATA_REG);
			written++;
		} while (--count);
		aux_end_atomic();
		retval = -EIO;
		if (written) {
			retval = written;
			file->f_dentry->d_inode->i_mtime = CURRENT_TIME;
		}
	}

	return retval;
}

/*
 *	Generic part continues...
 */

/*
 * Put bytes from input queue to buffer.
 */

static ssize_t aux_read(struct file * file, char * buffer,
		        size_t count, loff_t *ppos)
{
	struct wait_queue wait = { current, NULL };
	ssize_t i = count;
	unsigned char c;

	if (queue_empty()) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&queue->proc_list, &wait);
repeat:
		current->state = TASK_INTERRUPTIBLE;
		if (queue_empty() && !signal_pending(current)) {
			schedule();
			goto repeat;
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&queue->proc_list, &wait);
	}
	while (i > 0 && !queue_empty()) {
		c = get_from_queue();
		put_user(c, buffer++);
		i--;
	}
	aux_ready = !queue_empty();
	if (count-i) {
		file->f_dentry->d_inode->i_atime = CURRENT_TIME;
		return count-i;
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

static unsigned int aux_poll(struct file *file, poll_table * wait)
{
	poll_wait(file, &queue->proc_list, wait);
	if (aux_ready)
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations psaux_fops = {
	NULL,		/* seek */
	aux_read,
	aux_write,
	NULL, 		/* readdir */
	aux_poll,
	NULL, 		/* ioctl */
	NULL,		/* mmap */
	aux_open,
	NULL,		/* flush */
	aux_release,
	NULL,
	aux_fasync,
};

static struct miscdevice psaux_mouse = {
	PSMOUSE_MINOR, "ps2aux", &psaux_fops
};

__initfunc(int pcimouse_init(void))
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
	struct linux_ebus_child *child;

	if (pcikbd_mrcoffee) {
		if ((pcimouse_iobase = pcikbd_iobase) == 0) {
			printk("pcimouse_init: no 8042 given\n");
			return -ENODEV;
		}
		pcimouse_irq = pcikbd_irq;
	} else {
		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if(!strcmp(edev->prom_name, "8042")) {
					for_each_edevchild(edev, child) {
							if (!strcmp(child->prom_name, PCI_MS_NAME))
							goto found;
					}
				}
			}
		}
		printk("pcimouse_init: no 8042 found\n");
		return -ENODEV;

found:
		pcimouse_iobase = child->base_address[0];
		/*
		 * Just in case the iobases for kbd/mouse ever differ...
		 */
		if (!check_region(pcimouse_iobase, sizeof(unsigned long)))
			request_region(pcimouse_iobase, sizeof(unsigned long),
				       "8042 controller");

		pcimouse_irq = child->irqs[0];
	}

	queue = (struct aux_queue *) kmalloc(sizeof(*queue), GFP_KERNEL);
	memset(queue, 0, sizeof(*queue));
	queue->head = queue->tail = 0;
	queue->proc_list = NULL;

	if (request_irq(pcimouse_irq, &pcimouse_interrupt,
		        SA_SHIRQ, "mouse", (void *)pcimouse_iobase)) {
		printk("8042: Cannot register IRQ %s\n",
		       __irq_itoa(pcimouse_irq));
		return -ENODEV;
	}

	printk("8042(mouse) at %lx (irq %s)\n", pcimouse_iobase,
	       __irq_itoa(pcimouse_irq));

	printk("8042: PS/2 auxiliary pointing device detected.\n");
	aux_present = 1;
	pckbd_read_mask = AUX_STAT_OBF;

	misc_register(&psaux_mouse);
	aux_start_atomic();
	pcimouse_outb(KBD_CCMD_MOUSE_ENABLE, pcimouse_iobase + KBD_CNTL_REG);
	aux_write_ack(AUX_RESET);
	aux_write_ack(AUX_SET_SAMPLE);
	aux_write_ack(100);
	aux_write_ack(AUX_SET_RES);
	aux_write_ack(3);
	aux_write_ack(AUX_SET_SCALE21);
	poll_aux_status();
	pcimouse_outb(KBD_CCMD_MOUSE_DISABLE, pcimouse_iobase + KBD_CNTL_REG);
	poll_aux_status();
	pcimouse_outb(KBD_CCMD_WRITE_MODE, pcimouse_iobase + KBD_CNTL_REG);
	poll_aux_status();
	pcimouse_outb(AUX_INTS_OFF, pcimouse_iobase + KBD_DATA_REG);
	poll_aux_status();
	aux_end_atomic();

	return 0;
}


__initfunc(int ps2kbd_probe(unsigned long *memory_start))
{
	int pnode, enode, node, dnode;
	int kbnode = 0, msnode = 0, bnode = 0;
	int devices = 0;
	char prop[128];
	int len;

#ifndef __sparc_v9__
	/*
	 * MrCoffee has hardware but has no PROM nodes whatsoever.
	 */
	len = prom_getproperty(prom_root_node, "name", prop, sizeof(prop));
	if (len < 0) {
		printk("ps2kbd_probe: no name of root node\n");
		return -ENODEV;
	}
	if (strncmp(prop, "SUNW,JavaStation-1", len) == 0) {
		pcikbd_mrcoffee = 1;	/* Brain damage detected */
		goto found;
	}
#endif
	/*
	 * Get the nodes for keyboard and mouse from aliases on normal systems.
	 */
        node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "aliases");
	if (!node)
		return -ENODEV;

	len = prom_getproperty(node, "keyboard", prop, sizeof(prop));
	if (len > 0) {
		prop[len] = 0;
		kbnode = prom_finddevice(prop);
	}
	if (!kbnode)
		return -ENODEV;

	len = prom_getproperty(node, "mouse", prop, sizeof(prop));
	if (len > 0) {
		prop[len] = 0;
		msnode = prom_finddevice(prop);
	}
	if (!msnode)
		return -ENODEV;

	/*
	 * Find matching EBus nodes...
	 */
        node = prom_getchild(prom_root_node);
	pnode = prom_searchsiblings(node, "pci");

	/*
	 * Check for SUNW,sabre on Ultra5/10/AXi.
	 */
	len = prom_getproperty(pnode, "model", prop, sizeof(prop));
	if ((len > 0) && !strncmp(prop, "SUNW,sabre", len)) {
		pnode = prom_getchild(pnode);
		pnode = prom_searchsiblings(pnode, "pci");
	}

	/*
	 * For each PCI bus...
	 */
	while (pnode) {
		enode = prom_getchild(pnode);
		enode = prom_searchsiblings(enode, "ebus");

		/*
		 * For each EBus on this PCI...
		 */
		while (enode) {
			node = prom_getchild(enode);
			bnode = prom_searchsiblings(node, "beeper");

			node = prom_getchild(enode);
			node = prom_searchsiblings(node, "8042");

			/*
			 * For each '8042' on this EBus...
			 */
			while (node) {
				/*
				 * Does it match?
				 */
				dnode = prom_getchild(node);
				dnode = prom_searchsiblings(dnode, PCI_KB_NAME);
				if (dnode == kbnode) {
					++devices;
				}

				dnode = prom_getchild(node);
				dnode = prom_searchsiblings(dnode, PCI_MS_NAME);
				if (dnode == msnode) {
					++devices;
				}

				/*
				 * Found everything we need?
				 */
				if (devices == 2)
					goto found;

				node = prom_getsibling(node);
				node = prom_searchsiblings(node, "8042");
			}
			enode = prom_getsibling(enode);
			enode = prom_searchsiblings(enode, "ebus");
		}
		pnode = prom_getsibling(pnode);
		pnode = prom_searchsiblings(pnode, "pci");
	}
	return -ENODEV;

found:
        sunkbd_setinitfunc(memory_start, pcimouse_init);
        sunkbd_setinitfunc(memory_start, pcikbd_init);
	kbd_ops.compute_shiftstate = pci_compute_shiftstate;
	kbd_ops.setledstate = pci_setledstate;
	kbd_ops.getledstate = pci_getledstate;
	kbd_ops.setkeycode = pci_setkeycode;
	kbd_ops.getkeycode = pci_getkeycode;
	return 0;
}

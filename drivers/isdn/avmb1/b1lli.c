/*
 * $Id: b1lli.c,v 1.10 1999/04/15 19:49:31 calle Exp $
 * 
 * ISDN lowlevel-module for AVM B1-card.
 * 
 * (c) Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: b1lli.c,v $
 * Revision 1.10  1999/04/15 19:49:31  calle
 * fix fuer die B1-PCI. Jetzt geht z.B. auch IRQ 17 ...
 *
 * Revision 1.9  1999/01/05 18:33:23  he
 * merged remaining 2.2pre{1,2} changes (jiffies and Config)
 *
 * Revision 1.8  1998/10/25 14:39:00  fritz
 * Backported from MIPS (Cobalt).
 *
 * Revision 1.7  1998/03/29 16:06:00  calle
 * changes from 2.0 tree merged.
 *
 * Revision 1.1.2.10  1998/03/20 20:34:41  calle
 * port valid check now only for T1, because of the PCI and PCMCIA cards.
 *
 * Revision 1.1.2.9  1998/03/20 14:38:20  calle
 * capidrv: prepared state machines for suspend/resume/hold
 * capidrv: fix bug in state machine if B1/T1 is out of nccis
 * b1capi: changed some errno returns.
 * b1capi: detect if you try to add same T1 to different io address.
 * b1capi: change number of nccis depending on number of channels.
 * b1lli: cosmetics
 *
 * Revision 1.1.2.8  1998/03/18 17:43:29  calle
 * T1 with fastlink, bugfix for multicontroller support in capidrv.c
 *
 * Revision 1.1.2.7  1998/03/04 17:33:50  calle
 * Changes for T1.
 *
 * Revision 1.1.2.6  1998/02/27 15:40:44  calle
 * T1 running with slow link. bugfix in capi_release.
 *
 * Revision 1.1.2.5  1998/02/13 16:28:28  calle
 * first step for T1
 *
 * Revision 1.6  1998/02/13 07:09:11  calle
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.5  1998/01/31 11:14:41  calle
 * merged changes to 2.0 tree, prepare 2.1.82 to work.
 *
 * Revision 1.4  1997/12/10 20:00:48  calle
 * get changes from 2.0 version
 *
 * Revision 1.1.2.2  1997/11/26 10:46:55  calle
 * prepared for M1 (Mobile) and T1 (PMX) cards.
 * prepared to set configuration after load to support other D-channel
 * protocols, point-to-point and leased lines.
 *
 * Revision 1.3  1997/10/01 09:21:13  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.2  1997/07/13 12:22:42  calle
 * bug fix for more than one controller in connect_req.
 * debugoutput now with contrnr.
 *
 *
 * Revision 1.1  1997/03/04 21:50:28  calle
 * Frirst version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 * 
 */
/* #define FASTLINK_DEBUG */

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/capi.h>
#include <linux/b1lli.h>

#include "compat.h"
#include "capicmd.h"
#include "capiutil.h"

extern int showcapimsgs;

/*
 * LLI Messages to the ISDN-ControllerISDN Controller 
 */

#define	SEND_POLL		0x72	/*
					   * after load <- RECEIVE_POLL 
					 */
#define SEND_INIT		0x11	/*
					   * first message <- RECEIVE_INIT
					   * int32 NumApplications  int32
					   * NumNCCIs int32 BoardNumber 
					 */
#define SEND_REGISTER		0x12	/*
					   * register an application int32
					   * ApplIDId int32 NumMessages
					   * int32 NumB3Connections int32
					   * NumB3Blocks int32 B3Size
					   * 
					   * AnzB3Connection != 0 &&
					   * AnzB3Blocks >= 1 && B3Size >= 1 
					 */
#define SEND_RELEASE		0x14	/*
					   * deregister an application int32 
					   * ApplID 
					 */
#define SEND_MESSAGE		0x15	/*
					   * send capi-message int32 length
					   * capi-data ... 
					 */
#define SEND_DATA_B3_REQ	0x13	/*
					   * send capi-data-message int32
					   * MsgLength capi-data ... int32
					   * B3Length data .... 
					 */

#define SEND_CONFIG		0x21    /*
                                         */

#define SEND_POLLACK		0x73    /* T1 Watchdog */

/*
 * LLI Messages from the ISDN-ControllerISDN Controller 
 */

#define RECEIVE_POLL		0x32	/*
					   * <- after SEND_POLL 
					 */
#define RECEIVE_INIT		0x27	/*
					   * <- after SEND_INIT int32 length
					   * byte total length b1struct board 
					   * driver revision b1struct card
					   * type b1struct reserved b1struct
					   * serial number b1struct driver
					   * capability b1struct d-channel
					   * protocol b1struct CAPI-2.0
					   * profile b1struct capi version 
					 */
#define RECEIVE_MESSAGE		0x21	/*
					   * <- after SEND_MESSAGE int32
					   * AppllID int32 Length capi-data
					   * .... 
					 */
#define RECEIVE_DATA_B3_IND	0x22	/*
					   * received data int32 AppllID
					   * int32 Length capi-data ...
					   * int32 B3Length data ... 
					 */
#define RECEIVE_START		0x23	/*
					   * Handshake 
					 */
#define RECEIVE_STOP		0x24	/*
					   * Handshake 
					 */
#define RECEIVE_NEW_NCCI	0x25	/*
					   * int32 AppllID int32 NCCI int32
					   * WindowSize 
					 */
#define RECEIVE_FREE_NCCI	0x26	/*
					   * int32 AppllID int32 NCCI 
					 */
#define RECEIVE_RELEASE		0x26	/*
					   * int32 AppllID int32 0xffffffff 
					 */
#define RECEIVE_TASK_READY	0x31	/*
					   * int32 tasknr
					   * int32 Length Taskname ...
					 */

#define WRITE_REGISTER		0x00
#define READ_REGISTER		0x01

/*
 * port offsets
 */

#define B1_READ			0x00
#define B1_WRITE		0x01
#define B1_INSTAT		0x02
#define B1_OUTSTAT		0x03
#define B1_RESET		0x10
#define B1_ANALYSE		0x04

/* Hema card T1 */

#define T1_FASTLINK		0x00
#define T1_SLOWLINK		0x08

#define T1_READ			B1_READ
#define T1_WRITE		B1_WRITE
#define T1_INSTAT		B1_INSTAT
#define T1_OUTSTAT		B1_OUTSTAT
#define T1_IRQENABLE		0x05
#define T1_FIFOSTAT		0x06
#define T1_RESETLINK		0x10
#define T1_ANALYSE		0x11
#define T1_IRQMASTER		0x12
#define T1_IDENT		0x17
#define T1_RESETBOARD		0x1f

#define	T1F_IREADY		0x01
#define	T1F_IHALF		0x02
#define	T1F_IFULL		0x04
#define	T1F_IEMPTY		0x08
#define	T1F_IFLAGS		0xF0

#define	T1F_OREADY		0x10
#define	T1F_OHALF		0x20
#define	T1F_OEMPTY		0x40
#define	T1F_OFULL		0x80
#define	T1F_OFLAGS		0xF0

/* there are HEMA cards with 1k and 4k FIFO out */
#define FIFO_OUTBSIZE		256
#define FIFO_INPBSIZE		512

#define HEMA_VERSION_ID		0
#define HEMA_PAL_ID		0

#define B1_STAT0(cardtype)  ((cardtype) == AVM_CARDTYPE_M1 ? 0x81200000l : 0x80A00000l)
#define B1_STAT1(cardtype)  (0x80E00000l)


static inline unsigned char b1outp(unsigned int base,
				   unsigned short offset,
				   unsigned char value)
{
	outb(value, base + offset);
	return inb(base + B1_ANALYSE);
}

static inline void t1outp(unsigned int base,
			  unsigned short offset,
			  unsigned char value)
{
	outb(value, base + offset);
}

static inline unsigned char t1inp(unsigned int base,
			          unsigned short offset)
{
	return inb(base + offset);
}

static inline int B1_isfastlink(unsigned int base)
{
	return (inb(base + T1_IDENT) & ~0x82) == 1;
}
static inline unsigned char B1_fifostatus(unsigned int base)
{
	return inb(base + T1_FIFOSTAT);
}

static inline int B1_rx_full(unsigned int base)
{
	return inb(base + B1_INSTAT) & 0x1;
}

static inline unsigned char B1_get_byte(unsigned int base)
{
	unsigned long i = jiffies + 1 * HZ;	/* maximum wait time 1 sec */
	while (!B1_rx_full(base) && time_before(jiffies, i));
	if (B1_rx_full(base))
		return inb(base + B1_READ);
	printk(KERN_CRIT "b1lli(0x%x): rx not full after 1 second\n", base);
	return 0;
}

static inline unsigned int B1_get_word(unsigned int base)
{
	unsigned int val = 0;
	val |= B1_get_byte(base);
	val |= (B1_get_byte(base) << 8);
	val |= (B1_get_byte(base) << 16);
	val |= (B1_get_byte(base) << 24);
	return val;
}

static inline int B1_tx_empty(unsigned int base)
{
	return inb(base + B1_OUTSTAT) & 0x1;
}

static inline void B1_put_byte(unsigned int base, unsigned char val)
{
	while (!B1_tx_empty(base));
	b1outp(base, B1_WRITE, val);
}

static inline void B1_put_word(unsigned int base, unsigned int val)
{
	B1_put_byte(base, val & 0xff);
	B1_put_byte(base, (val >> 8) & 0xff);
	B1_put_byte(base, (val >> 16) & 0xff);
	B1_put_byte(base, (val >> 24) & 0xff);
}

static inline unsigned int B1_get_slice(unsigned int base,
					unsigned char *dp)
{
	unsigned int len, i;
#ifdef FASTLINK_DEBUG
	unsigned wcnt = 0, bcnt = 0;
#endif

	len = i = B1_get_word(base);
        if (B1_isfastlink(base)) {
		int status;
		while (i > 0) {
			status = B1_fifostatus(base) & (T1F_IREADY|T1F_IHALF);
			if (i >= FIFO_INPBSIZE) status |= T1F_IFULL;

			switch (status) {
				case T1F_IREADY|T1F_IHALF|T1F_IFULL:
					insb(base+B1_READ, dp, FIFO_INPBSIZE);
					dp += FIFO_INPBSIZE;
					i -= FIFO_INPBSIZE;
#ifdef FASTLINK_DEBUG
					wcnt += FIFO_INPBSIZE;
#endif
					break;
				case T1F_IREADY|T1F_IHALF: 
					insb(base+B1_READ,dp, i);
#ifdef FASTLINK_DEBUG
					wcnt += i;
#endif
					dp += i;
					i = 0;
					if (i == 0)
						break;
					/* fall through */
				default:
					*dp++ = B1_get_byte(base);
					i--;
#ifdef FASTLINK_DEBUG
					bcnt++;
#endif
					break;
			}
	    }
#ifdef FASTLINK_DEBUG
	    if (wcnt)
	    printk(KERN_DEBUG "b1lli(0x%x): get_slice l=%d w=%d b=%d\n",
				base, len, wcnt, bcnt);
#endif
	} else {
		while (i-- > 0)
			*dp++ = B1_get_byte(base);
	}
	return len;
}

static inline void B1_put_slice(unsigned int base,
				unsigned char *dp, unsigned int len)
{
	unsigned i = len;
	B1_put_word(base, i);
        if (B1_isfastlink(base)) {
		int status;
		while (i > 0) {
			status = B1_fifostatus(base) & (T1F_OREADY|T1F_OHALF);
			if (i >= FIFO_OUTBSIZE) status |= T1F_OEMPTY;
			switch (status) {
				case T1F_OREADY|T1F_OHALF|T1F_OEMPTY: 
					outsb(base+B1_WRITE, dp, FIFO_OUTBSIZE);
					dp += FIFO_OUTBSIZE;
					i -= FIFO_OUTBSIZE;
					break;
				case T1F_OREADY|T1F_OHALF: 
					outsb(base+B1_WRITE, dp, i);
					dp += i;
					i = 0;
				        break;
				default:
					B1_put_byte(base, *dp++);
					i--;
					break;
			}
		}
	} else {
		while (i-- > 0)
			B1_put_byte(base, *dp++);
	}
}

static void b1_wr_reg(unsigned int base,
                      unsigned int reg,
		      unsigned int value)
{
	B1_put_byte(base, WRITE_REGISTER);
        B1_put_word(base, reg);
        B1_put_word(base, value);
}

static inline unsigned int b1_rd_reg(unsigned int base,
                                     unsigned int reg)
{
	B1_put_byte(base, READ_REGISTER);
        B1_put_word(base, reg);
        return B1_get_word(base);
	
}

static inline void b1_set_test_bit(unsigned int base,
				   int cardtype,
				   int onoff)
{
    b1_wr_reg(base, B1_STAT0(cardtype), onoff ? 0x21 : 0x20);
}

static inline int b1_get_test_bit(unsigned int base,
                                  int cardtype)
{
    return (b1_rd_reg(base, B1_STAT0(cardtype)) & 0x01) != 0;
}

static int irq_table[16] =
{0,
 0,
 0,
 192,				/* irq 3 */
 32,				/* irq 4 */
 160,				/* irq 5 */
 96,				/* irq 6 */
 224,				/* irq 7 */
 0,
 64,				/* irq 9 */
 80,				/* irq 10 */
 208,				/* irq 11 */
 48,				/* irq 12 */
 0,
 0,
 112,				/* irq 15 */
};

static int hema_irq_table[16] =
{0,
 0,
 0,
 0x80,				/* irq 3 */
 0,
 0x90,				/* irq 5 */
 0,
 0xA0,				/* irq 7 */
 0,
 0xB0,				/* irq 9 */
 0xC0,				/* irq 10 */
 0xD0,				/* irq 11 */
 0xE0,				/* irq 12 */
 0,
 0,
 0xF0,				/* irq 15 */
};


int B1_valid_irq(unsigned irq, int cardtype)
{
	switch (cardtype) {
	   default:
	   case AVM_CARDTYPE_M1:
	   case AVM_CARDTYPE_M2:
	   case AVM_CARDTYPE_B1:
	   	return irq_table[irq & 0xf] != 0;
	   case AVM_CARDTYPE_T1:
	   	return hema_irq_table[irq & 0xf] != 0;
	   case AVM_CARDTYPE_B1PCI:
		return 1;
	}
}

int B1_valid_port(unsigned port, int cardtype)
{
   switch (cardtype) {
	   default:
	   case AVM_CARDTYPE_M1:
	   case AVM_CARDTYPE_M2:
	   case AVM_CARDTYPE_B1:
#if 0	/* problem with PCMCIA and PCI cards */
		switch (port) {
			case 0x150:
			case 0x250:
			case 0x300:
			case 0x340:
				return 1;
		}
		return 0;
#else
		return 1;
#endif
	   case AVM_CARDTYPE_B1PCI:
		return 1;
	   case AVM_CARDTYPE_T1:
		return ((port & 0x7) == 0) && ((port & 0x30) != 0x30);
   }
}

void B1_setinterrupt(unsigned int base,
			         unsigned irq, int cardtype)
{
	switch (cardtype) {
	   case AVM_CARDTYPE_T1:
              t1outp(base, B1_INSTAT, 0x00);
              t1outp(base, B1_INSTAT, 0x02);
	      t1outp(base, T1_IRQMASTER, 0x08);
	      break;
	   default:
	   case AVM_CARDTYPE_M1:
	   case AVM_CARDTYPE_M2:
	   case AVM_CARDTYPE_B1:
	      b1outp(base, B1_INSTAT, 0x00);
	      b1outp(base, B1_RESET, irq_table[irq]);
	      b1outp(base, B1_INSTAT, 0x02);
	      break;
	   case AVM_CARDTYPE_B1PCI:
	      b1outp(base, B1_INSTAT, 0x00);
	      b1outp(base, B1_RESET, 0xf0);
	      b1outp(base, B1_INSTAT, 0x02);
	      break;
	 }
}

unsigned char B1_disable_irq(unsigned int base)
{
	return b1outp(base, B1_INSTAT, 0x00);
}

void T1_disable_irq(unsigned int base)
{
      t1outp(base, T1_IRQMASTER, 0x00);
}

void B1_reset(unsigned int base)
{
	b1outp(base, B1_RESET, 0);
	udelay(55 * 2 * 1000);	/* 2 TIC's */

	b1outp(base, B1_RESET, 1);
	udelay(55 * 2 * 1000);	/* 2 TIC's */

	b1outp(base, B1_RESET, 0);
	udelay(55 * 2 * 1000);	/* 2 TIC's */
}

void T1_reset(unsigned int base)
{
        /* reset T1 Controller */
        B1_reset(base);
        /* disable irq on HEMA */
        t1outp(base, B1_INSTAT, 0x00);
        t1outp(base, B1_OUTSTAT, 0x00);
        t1outp(base, T1_IRQMASTER, 0x00);
        /* reset HEMA board configuration */
	t1outp(base, T1_RESETBOARD, 0xf);
}

int B1_detect(unsigned int base, int cardtype)
{
	int onoff, i;

	if (cardtype == AVM_CARDTYPE_T1)
	   return 0;

	/*
	 * Statusregister 0000 00xx 
	 */
	if ((inb(base + B1_INSTAT) & 0xfc)
	    || (inb(base + B1_OUTSTAT) & 0xfc))
		return 1;
	/*
	 * Statusregister 0000 001x 
	 */
	b1outp(base, B1_INSTAT, 0x2);	/* enable irq */
	/* b1outp(base, B1_OUTSTAT, 0x2); */
	if ((inb(base + B1_INSTAT) & 0xfe) != 0x2
	    /* || (inb(base + B1_OUTSTAT) & 0xfe) != 0x2 */)
		return 2;
	/*
	 * Statusregister 0000 000x 
	 */
	b1outp(base, B1_INSTAT, 0x0);	/* disable irq */
	b1outp(base, B1_OUTSTAT, 0x0);
	if ((inb(base + B1_INSTAT) & 0xfe)
	    || (inb(base + B1_OUTSTAT) & 0xfe))
		return 3;
        
	for (onoff = !0, i= 0; i < 10 ; i++) {
		b1_set_test_bit(base, cardtype, onoff);
		if (b1_get_test_bit(base, cardtype) != onoff)
		   return 4;
		onoff = !onoff;
	}

	if (cardtype == AVM_CARDTYPE_M1)
	   return 0;

        if ((b1_rd_reg(base, B1_STAT1(cardtype)) & 0x0f) != 0x01)
	   return 5;

	return 0;
}

int T1_detectandinit(unsigned int base, unsigned irq, int cardnr)
{
	unsigned char cregs[8];
	unsigned char reverse_cardnr;
	unsigned long flags;
	unsigned char dummy;
	int i;

	reverse_cardnr =   ((cardnr & 0x01) << 3) | ((cardnr & 0x02) << 1)
		         | ((cardnr & 0x04) >> 1) | ((cardnr & 0x08) >> 3);
	cregs[0] = (HEMA_VERSION_ID << 4) | (reverse_cardnr & 0xf);
	cregs[1] = 0x00; /* fast & slow link connected to CON1 */
	cregs[2] = 0x05; /* fast link 20MBit, slow link 20 MBit */
	cregs[3] = 0;
	cregs[4] = 0x11; /* zero wait state */
	cregs[5] = hema_irq_table[irq & 0xf];
	cregs[6] = 0;
	cregs[7] = 0;

	save_flags(flags);
	cli();
	/* board reset */
	t1outp(base, T1_RESETBOARD, 0xf);
	udelay(100 * 1000);
	dummy = t1inp(base, T1_FASTLINK+T1_OUTSTAT); /* first read */

	/* write config */
	dummy = (base >> 4) & 0xff;
	for (i=1;i<=0xf;i++) t1outp(base, i, dummy);
	t1outp(base, HEMA_PAL_ID & 0xf, dummy);
	t1outp(base, HEMA_PAL_ID >> 4, cregs[0]);
	for(i=1;i<7;i++) t1outp(base, 0, cregs[i]);
	t1outp(base, ((base >> 4)) & 0x3, cregs[7]);
	restore_flags(flags);

	udelay(100 * 1000);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 0);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 0);
	udelay(10 * 1000);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 1);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 1);
	udelay(100 * 1000);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 0);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 0);
	udelay(10 * 1000);
	t1outp(base, T1_FASTLINK+T1_ANALYSE, 0);
	udelay(5 * 1000);
	t1outp(base, T1_SLOWLINK+T1_ANALYSE, 0);

	if (t1inp(base, T1_FASTLINK+T1_OUTSTAT) != 0x1) /* tx empty */
		return 1;
	if (t1inp(base, T1_FASTLINK+T1_INSTAT) != 0x0) /* rx empty */
		return 2;
	if (t1inp(base, T1_FASTLINK+T1_IRQENABLE) != 0x0)
		return 3;
	if ((t1inp(base, T1_FASTLINK+T1_FIFOSTAT) & 0xf0) != 0x70)
		return 4;
	if ((t1inp(base, T1_FASTLINK+T1_IRQMASTER) & 0x0e) != 0)
		return 5;
	if ((t1inp(base, T1_FASTLINK+T1_IDENT) & 0x7d) != 1)
		return 6;
	if (t1inp(base, T1_SLOWLINK+T1_OUTSTAT) != 0x1) /* tx empty */
		return 7;
	if ((t1inp(base, T1_SLOWLINK+T1_IRQMASTER) & 0x0e) != 0)
		return 8;
	if ((t1inp(base, T1_SLOWLINK+T1_IDENT) & 0x7d) != 0)
		return 9;
        return 0;
}

extern int loaddebug;

int B1_load_t4file(unsigned int base, avmb1_t4file * t4file)
{
	/*
	 * Data is in user space !!!
	 */
	unsigned char buf[256];
	unsigned char *dp;
	int i, left, retval;


	dp = t4file->data;
	left = t4file->len;
	while (left > sizeof(buf)) {
		retval = copy_from_user(buf, dp, sizeof(buf));
		if (retval)
			return -EFAULT;
		if (loaddebug)
			printk(KERN_DEBUG "b1capi: loading: %d bytes ..", sizeof(buf));
		for (i = 0; i < sizeof(buf); i++)
			B1_put_byte(base, buf[i]);
		if (loaddebug)
		   printk("ok\n");
		left -= sizeof(buf);
		dp += sizeof(buf);
	}
	if (left) {
		retval = copy_from_user(buf, dp, left);
		if (retval)
			return -EFAULT;
		if (loaddebug)
			printk(KERN_DEBUG "b1capi: loading: %d bytes ..", left);
		for (i = 0; i < left; i++)
			B1_put_byte(base, buf[i]);
		if (loaddebug)
		   printk("ok\n");
	}
	return 0;
}

int B1_load_config(unsigned int base, avmb1_t4file * config)
{
	/*
	 * Data is in user space !!!
	 */
	unsigned char buf[256];
	unsigned char *dp;
	int i, j, left, retval;


	dp = config->data;
	left = config->len;
	if (left) {
		B1_put_byte(base, SEND_CONFIG);
        	B1_put_word(base, 1);
		B1_put_byte(base, SEND_CONFIG);
        	B1_put_word(base, left);
	}
	while (left > sizeof(buf)) {
		retval = copy_from_user(buf, dp, sizeof(buf));
		if (retval)
			return -EFAULT;
		if (loaddebug)
			printk(KERN_DEBUG "b1capi: conf load: %d bytes ..", sizeof(buf));
		for (i = 0; i < sizeof(buf); ) {
			B1_put_byte(base, SEND_CONFIG);
			for (j=0; j < 4; j++) {
				B1_put_byte(base, buf[i++]);
			}
		}
		if (loaddebug)
		   printk("ok\n");
		left -= sizeof(buf);
		dp += sizeof(buf);
	}
	if (left) {
		retval = copy_from_user(buf, dp, left);
		if (retval)
			return -EFAULT;
		if (loaddebug)
			printk(KERN_DEBUG "b1capi: conf load: %d bytes ..", left);
		for (i = 0; i < left; ) {
			B1_put_byte(base, SEND_CONFIG);
			for (j=0; j < 4; j++) {
				if (i < left)
					B1_put_byte(base, buf[i++]);
				else
					B1_put_byte(base, 0);
			}
		}
		if (loaddebug)
		   printk("ok\n");
	}
	return 0;
}

int B1_loaded(unsigned int base)
{
	int i;
	unsigned char ans;

	if (loaddebug)
		printk(KERN_DEBUG "b1capi: loaded: wait 1 ..\n");
	for (i = jiffies + 10 * HZ; time_before(jiffies, i);) {
		if (B1_tx_empty(base))
			break;
	}
	if (!B1_tx_empty(base)) {
		printk(KERN_ERR "b1lli(0x%x): B1_loaded: timeout tx\n", base);
		return 0;
	}
	B1_put_byte(base, SEND_POLL);
	printk(KERN_DEBUG "b1capi: loaded: wait 2 ..\n");
	for (i = jiffies + 10 * HZ; time_before(jiffies, i);) {
		if (B1_rx_full(base)) {
			if ((ans = B1_get_byte(base)) == RECEIVE_POLL) {
				if (loaddebug)
					printk(KERN_DEBUG "b1capi: loaded: ok\n");
				return 1;
			}
			printk(KERN_ERR "b1lli(0x%x): B1_loaded: got 0x%x ???\n",
				base, ans);
			return 0;
		}
	}
	printk(KERN_ERR "b1lli(0x%x): B1_loaded: timeout rx\n", base);
	return 0;
}

/*
 * ------------------------------------------------------------------- 
 */
static inline void parse_version(avmb1_card * card)
{
	int i, j;
	for (j = 0; j < AVM_MAXVERSION; j++)
		card->version[j] = "\0\0" + 1;
	for (i = 0, j = 0;
	     j < AVM_MAXVERSION && i < card->versionlen;
	     j++, i += card->versionbuf[i] + 1)
		card->version[j] = &card->versionbuf[i + 1];
}
/*
 * ------------------------------------------------------------------- 
 */

void B1_send_init(unsigned int port,
	     unsigned int napps, unsigned int nncci, unsigned int cardnr)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	B1_put_byte(port, SEND_INIT);
	B1_put_word(port, napps);
	B1_put_word(port, nncci);
	B1_put_word(port, cardnr);
	restore_flags(flags);
}

void B1_send_register(unsigned int port,
		      __u16 appid, __u32 nmsg,
		      __u32 nb3conn, __u32 nb3blocks, __u32 b3bsize)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	B1_put_byte(port, SEND_REGISTER);
	B1_put_word(port, appid);
	B1_put_word(port, nmsg);
	B1_put_word(port, nb3conn);
	B1_put_word(port, nb3blocks);
	B1_put_word(port, b3bsize);
	restore_flags(flags);
}

void B1_send_release(unsigned int port,
		     __u16 appid)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	B1_put_byte(port, SEND_RELEASE);
	B1_put_word(port, appid);
	restore_flags(flags);
}

void B1_send_message(unsigned int port, struct sk_buff *skb)
{
	unsigned long flags;
	__u16 len = CAPIMSG_LEN(skb->data);
	__u8 cmd = CAPIMSG_COMMAND(skb->data);
	__u8 subcmd = CAPIMSG_SUBCOMMAND(skb->data);
	__u32 contr = CAPIMSG_CONTROL(skb->data);

	if (CAPICMD(cmd, subcmd) == CAPI_DATA_B3_REQ) {
		__u16 dlen = CAPIMSG_DATALEN(skb->data);

		if (showcapimsgs > 2) {
			if (showcapimsgs & 1) {
				printk(KERN_DEBUG "b1lli: Put [0x%lx] id#%d %s len=%u\n",
				       (unsigned long) contr,
				       CAPIMSG_APPID(skb->data),
				       capi_cmd2str(cmd, subcmd), len);
			} else {
				printk(KERN_DEBUG "b1lli: Put [0x%lx] %s\n",
						(unsigned long) contr,
						capi_message2str(skb->data));
			}

		}
		save_flags(flags);
		cli();
		B1_put_byte(port, SEND_DATA_B3_REQ);
		B1_put_slice(port, skb->data, len);
		B1_put_slice(port, skb->data + len, dlen);
		restore_flags(flags);
	} else {
		if (showcapimsgs) {

			if (showcapimsgs & 1) {
				printk(KERN_DEBUG "b1lli: Put [0x%lx] id#%d %s len=%u\n",
				       (unsigned long) contr,
				       CAPIMSG_APPID(skb->data),
				       capi_cmd2str(cmd, subcmd), len);
			} else {
				printk(KERN_DEBUG "b1lli: Put [0x%lx] %s\n", (unsigned long)contr, capi_message2str(skb->data));
			}
		}
		save_flags(flags);
		cli();
		B1_put_byte(port, SEND_MESSAGE);
		B1_put_slice(port, skb->data, len);
		restore_flags(flags);
	}
	dev_kfree_skb(skb);
}

/*
 * ------------------------------------------------------------------- 
 */

void B1_handle_interrupt(avmb1_card * card)
{
	unsigned char b1cmd;
	struct sk_buff *skb;

	unsigned ApplId;
	unsigned MsgLen;
	unsigned DataB3Len;
	unsigned NCCI;
	unsigned WindowSize;

t1retry:
	if (!B1_rx_full(card->port))
		return;

	b1cmd = B1_get_byte(card->port);

	switch (b1cmd) {

	case RECEIVE_DATA_B3_IND:

		ApplId = (unsigned) B1_get_word(card->port);
		MsgLen = B1_get_slice(card->port, card->msgbuf);
		DataB3Len = B1_get_slice(card->port, card->databuf);

		if (showcapimsgs > 2) {
			__u8 cmd = CAPIMSG_COMMAND(card->msgbuf);
			__u8 subcmd = CAPIMSG_SUBCOMMAND(card->msgbuf);
			__u32 contr = CAPIMSG_CONTROL(card->msgbuf);
			CAPIMSG_SETDATA(card->msgbuf, card->databuf);
			if (showcapimsgs & 1) {
				printk(KERN_DEBUG "b1lli: Got [0x%lx] id#%d %s len=%u/%u\n",
				       (unsigned long) contr,
				       CAPIMSG_APPID(card->msgbuf),
				       capi_cmd2str(cmd, subcmd),
				       MsgLen, DataB3Len);
			} else {
				printk(KERN_DEBUG "b1lli: Got [0x%lx] %s\n", (unsigned long)contr, capi_message2str(card->msgbuf));
			}
		}
		if (!(skb = dev_alloc_skb(DataB3Len + MsgLen))) {
			printk(KERN_ERR "b1lli: incoming packet dropped\n");
		} else {
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			memcpy(skb_put(skb, DataB3Len), card->databuf, DataB3Len);
			CAPIMSG_SETDATA(skb->data, skb->data + MsgLen);
			avmb1_handle_capimsg(card, ApplId, skb);
		}
		break;

	case RECEIVE_MESSAGE:

		ApplId = (unsigned) B1_get_word(card->port);
		MsgLen = B1_get_slice(card->port, card->msgbuf);
		if (showcapimsgs) {
			__u8 cmd = CAPIMSG_COMMAND(card->msgbuf);
			__u8 subcmd = CAPIMSG_SUBCOMMAND(card->msgbuf);
			__u32 contr = CAPIMSG_CONTROL(card->msgbuf);
			if (showcapimsgs & 1) {
				printk(KERN_DEBUG "b1lli: Got [0x%lx] id#%d %s len=%u\n",
				       (unsigned long) contr,
				       CAPIMSG_APPID(card->msgbuf),
				       capi_cmd2str(cmd, subcmd),
				       MsgLen);
			} else {
				printk(KERN_DEBUG "b1lli: Got [0x%lx] %s\n",
						(unsigned long) contr,
						capi_message2str(card->msgbuf));
			}

		}
		if (!(skb = dev_alloc_skb(MsgLen))) {
			printk(KERN_ERR "b1lli: incoming packet dropped\n");
		} else {
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			avmb1_handle_capimsg(card, ApplId, skb);
		}
		break;

	case RECEIVE_NEW_NCCI:

		ApplId = B1_get_word(card->port);
		NCCI = B1_get_word(card->port);
		WindowSize = B1_get_word(card->port);

		if (showcapimsgs)
			printk(KERN_DEBUG "b1lli(0x%x): NEW_NCCI app %u ncci 0x%x\n", card->port, ApplId, NCCI);

		avmb1_handle_new_ncci(card, ApplId, NCCI, WindowSize);

		break;

	case RECEIVE_FREE_NCCI:

		ApplId = B1_get_word(card->port);
		NCCI = B1_get_word(card->port);

		if (showcapimsgs)
			printk(KERN_DEBUG "b1lli(0x%x): FREE_NCCI app %u ncci 0x%x\n", card->port, ApplId, NCCI);

		avmb1_handle_free_ncci(card, ApplId, NCCI);
		break;

	case RECEIVE_START:
                if (card->cardtype == AVM_CARDTYPE_T1) {
	           B1_put_byte(card->port, SEND_POLLACK);
		   /* printk(KERN_DEBUG "b1lli: T1 watchdog\n"); */
                }
		if (card->blocked)
			printk(KERN_DEBUG "b1lli(0x%x): RESTART\n", card->port);
		card->blocked = 0;
		break;

	case RECEIVE_STOP:
		printk(KERN_DEBUG "b1lli(0x%x): STOP\n", card->port);
		card->blocked = 1;
		break;

	case RECEIVE_INIT:

		card->versionlen = B1_get_slice(card->port, card->versionbuf);
		card->cardstate = CARD_ACTIVE;
		parse_version(card);
		printk(KERN_INFO "b1lli(0x%x): %s-card (%s) now active\n",
		       card->port,
		       card->version[VER_CARDTYPE],
		       card->version[VER_DRIVER]);
		avmb1_card_ready(card);
		break;
        case RECEIVE_TASK_READY:
		ApplId = (unsigned) B1_get_word(card->port);
		MsgLen = B1_get_slice(card->port, card->msgbuf);
		card->msgbuf[MsgLen] = 0;
		printk(KERN_INFO "b1lli(0x%x): Task %d \"%s\" ready.\n",
				card->port, ApplId, card->msgbuf);
		break;
	default:
		printk(KERN_ERR "b1lli(0x%x): B1_handle_interrupt: 0x%x ???\n",
				card->port, b1cmd);
		break;
	}
	if (card->cardtype == AVM_CARDTYPE_T1) 
		goto t1retry;
}

/*
 * $Id: b1lli.c,v 1.1 1997/03/04 21:50:28 calle Exp $
 * 
 * ISDN lowlevel-module for AVM B1-card.
 * 
 * (c) Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: b1lli.c,v $
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

/*
 * port offsets
 */

#define B1_READ			0x00
#define B1_WRITE		0x01
#define B1_INSTAT		0x02
#define B1_OUTSTAT		0x03
#define B1_RESET		0x10
#define B1_ANALYSE		0x04



static inline unsigned char b1outp(unsigned short base,
				   unsigned short offset,
				   unsigned char value)
{
	outb(value, base + offset);
	return inb(base + B1_ANALYSE);
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

int B1_valid_irq(unsigned irq)
{
	return irq_table[irq] != 0;
}

unsigned char B1_assign_irq(unsigned short base, unsigned irq)
{
	return b1outp(base, B1_RESET, irq_table[irq]);
}

unsigned char B1_enable_irq(unsigned short base)
{
	return b1outp(base, B1_INSTAT, 0x02);
}

unsigned char B1_disable_irq(unsigned short base)
{
	return b1outp(base, B1_INSTAT, 0x00);
}

void B1_reset(unsigned short base)
{
	b1outp(base, B1_RESET, 0);
	udelay(55 * 2 * 1000);	/* 2 TIC's */

	b1outp(base, B1_RESET, 1);
	udelay(55 * 2 * 1000);	/* 2 TIC's */

	b1outp(base, B1_RESET, 0);
	udelay(55 * 2 * 1000);	/* 2 TIC's */
}

int B1_detect(unsigned short base)
{
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
	b1outp(base, B1_OUTSTAT, 0x2);
	if ((inb(base + B1_INSTAT) & 0xfe) != 0x2
	    || (inb(base + B1_OUTSTAT) & 0xfe) != 0x2)
		return 2;

	/*
	 * Statusregister 0000 000x 
	 */
	b1outp(base, B1_INSTAT, 0x0);	/* disable irq */
	b1outp(base, B1_OUTSTAT, 0x0);
	if ((inb(base + B1_INSTAT) & 0xfe)
	    || (inb(base + B1_OUTSTAT) & 0xfe))
		return 3;

	return 0;
}

static inline int B1_rx_full(unsigned short base)
{
	return inb(base + B1_INSTAT) & 0x1;
}

static inline unsigned char B1_get_byte(unsigned short base)
{
	unsigned long i = jiffies + 5 * HZ;	/* maximum wait time 5 sec */
	while (!B1_rx_full(base) && i > jiffies);
	if (B1_rx_full(base))
		return inb(base + B1_READ);
	printk(KERN_CRIT "b1lli: rx not full after 5 second\n");
	return 0;
}

static inline unsigned int B1_get_word(unsigned short base)
{
	unsigned int val = 0;
	val |= B1_get_byte(base);
	val |= (B1_get_byte(base) << 8);
	val |= (B1_get_byte(base) << 16);
	val |= (B1_get_byte(base) << 24);
	return val;
}

static inline int B1_tx_empty(unsigned short base)
{
	return inb(base + B1_OUTSTAT) & 0x1;
}

static inline void B1_put_byte(unsigned short base, unsigned char val)
{
	while (!B1_tx_empty(base));
	b1outp(base, B1_WRITE, val);
}

static inline void B1_put_word(unsigned short base, unsigned int val)
{
	B1_put_byte(base, val & 0xff);
	B1_put_byte(base, (val >> 8) & 0xff);
	B1_put_byte(base, (val >> 16) & 0xff);
	B1_put_byte(base, (val >> 24) & 0xff);
}

static inline unsigned int B1_get_slice(unsigned short base,
					unsigned char *dp)
{
	unsigned int len, i;

	len = i = B1_get_word(base);
	while (i-- > 0)
		*dp++ = B1_get_byte(base);
	return len;
}

static inline void B1_put_slice(unsigned short base,
				unsigned char *dp, unsigned int len)
{
	B1_put_word(base, len);
	while (len-- > 0)
		B1_put_byte(base, *dp++);
}

extern int loaddebug;

int B1_load_t4file(unsigned short base, avmb1_t4file * t4file)
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

int B1_loaded(unsigned short base)
{
	int i;
	unsigned char ans;

	if (loaddebug)
		printk(KERN_DEBUG "b1capi: loaded: wait 1 ..\n");
	for (i = jiffies + 10 * HZ; i > jiffies;) {
		if (B1_tx_empty(base))
			break;
	}
	if (!B1_tx_empty(base)) {
		printk(KERN_ERR "b1lli: B1_loaded: timeout tx\n");
		return 0;
	}
	B1_put_byte(base, SEND_POLL);
	printk(KERN_DEBUG "b1capi: loaded: wait 2 ..\n");
	for (i = jiffies + 10 * HZ; i > jiffies;) {
		if (B1_rx_full(base)) {
			if ((ans = B1_get_byte(base)) == RECEIVE_POLL) {
				if (loaddebug)
					printk(KERN_DEBUG "b1capi: loaded: ok\n");
				return 1;
			}
			printk(KERN_ERR "b1lli: B1_loaded: got 0x%x ???\n", ans);
			return 0;
		}
	}
	printk(KERN_ERR "b1lli: B1_loaded: timeout rx\n");
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

void B1_send_init(unsigned short port,
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

void B1_send_register(unsigned short port,
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

void B1_send_release(unsigned short port,
		     __u16 appid)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	B1_put_byte(port, SEND_RELEASE);
	B1_put_word(port, appid);
	restore_flags(flags);
}

extern int showcapimsgs;

void B1_send_message(unsigned short port, struct sk_buff *skb)
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
				printk(KERN_DEBUG "b1lli: Put %s\n", capi_message2str(skb->data));
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
				printk(KERN_DEBUG "b1lli: Put %s\n", capi_message2str(skb->data));
			}
		}
		save_flags(flags);
		cli();
		B1_put_byte(port, SEND_MESSAGE);
		B1_put_slice(port, skb->data, len);
		restore_flags(flags);
	}
	dev_kfree_skb(skb, FREE_WRITE);
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
				printk(KERN_DEBUG "b1lli: Got %s\n", capi_message2str(card->msgbuf));
			}
		}
		if (!(skb = dev_alloc_skb(DataB3Len + MsgLen))) {
			printk(KERN_ERR "b1lli: incoming packet dropped\n");
		} else {
			SET_SKB_FREE(skb);
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
				printk(KERN_DEBUG "b1lli: Got %s\n", capi_message2str(card->msgbuf));
			}

		}
		if (!(skb = dev_alloc_skb(MsgLen))) {
			printk(KERN_ERR "b1lli: incoming packet dropped\n");
		} else {
			SET_SKB_FREE(skb);
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			avmb1_handle_capimsg(card, ApplId, skb);
		}
		break;

	case RECEIVE_NEW_NCCI:

		ApplId = B1_get_word(card->port);
		NCCI = B1_get_word(card->port);
		WindowSize = B1_get_word(card->port);

		if (showcapimsgs)
			printk(KERN_DEBUG "b1lli: NEW_NCCI app %u ncci 0x%x\n", ApplId, NCCI);

		avmb1_handle_new_ncci(card, ApplId, NCCI, WindowSize);

		break;

	case RECEIVE_FREE_NCCI:

		ApplId = B1_get_word(card->port);
		NCCI = B1_get_word(card->port);

		if (showcapimsgs)
			printk(KERN_DEBUG "b1lli: FREE_NCCI app %u ncci 0x%x\n", ApplId, NCCI);

		avmb1_handle_free_ncci(card, ApplId, NCCI);
		break;

	case RECEIVE_START:
		if (card->blocked)
			printk(KERN_DEBUG "b1lli: RESTART\n");
		card->blocked = 0;
		break;

	case RECEIVE_STOP:
		printk(KERN_DEBUG "b1lli: STOP\n");
		card->blocked = 1;
		break;

	case RECEIVE_INIT:

		card->versionlen = B1_get_slice(card->port, card->versionbuf);
		card->cardstate = CARD_ACTIVE;
		parse_version(card);
		printk(KERN_INFO "b1lli: %s-card (%s) with %s now active\n",
		       card->version[VER_CARDTYPE],
		       card->version[VER_DRIVER],
		       card->version[VER_PROTO]);
		avmb1_card_ready(card);
		break;
	default:
		printk(KERN_ERR "b1lli: B1_handle_interrupt: 0x%x ???\n", b1cmd);
		break;
	}
}

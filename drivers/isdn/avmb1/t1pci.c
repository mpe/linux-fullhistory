/*
 * $Id: t1pci.c,v 1.2 1999/11/05 16:38:02 calle Exp $
 * 
 * Module for AVM T1 PCI-card.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: t1pci.c,v $
 * Revision 1.2  1999/11/05 16:38:02  calle
 * Cleanups before kernel 2.4:
 * - Changed all messages to use card->name or driver->name instead of
 *   constant string.
 * - Moved some data from struct avmcard into new struct avmctrl_info.
 *   Changed all lowlevel capi driver to match the new structur.
 *
 * Revision 1.1  1999/10/26 15:31:42  calle
 * Added driver for T1-PCI card.
 *
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/capi.h>
#include <asm/io.h>
#include "compat.h"
#include "capicmd.h"
#include "capiutil.h"
#include "capilli.h"
#include "avmcard.h"

static char *revision = "$Revision: 1.2 $";

#undef CONFIG_T1PCI_DEBUG
#undef CONFIG_T1PCI_POLLDEBUG

/* ------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_AVM
#define PCI_VENDOR_ID_AVM	0x1244
#endif

#ifndef PCI_DEVICE_ID_AVM_T1
#define PCI_DEVICE_ID_AVM_T1	0x1200
#endif

/* ------------------------------------------------------------- */

int suppress_pollack = 0;

MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

MODULE_PARM(suppress_pollack, "0-1i");


/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static void t1pci_dispatch_tx(avmcard *card);

/* ------------------------------------------------------------- */

/* S5933 */

#define	AMCC_RXPTR	0x24
#define	AMCC_RXLEN	0x28
#define	AMCC_TXPTR	0x2c
#define	AMCC_TXLEN	0x30

#define	AMCC_INTCSR	0x38
#	define EN_READ_TC_INT		0x00008000L
#	define EN_WRITE_TC_INT		0x00004000L
#	define EN_TX_TC_INT		EN_READ_TC_INT
#	define EN_RX_TC_INT		EN_WRITE_TC_INT
#	define AVM_FLAG			0x30000000L

#	define ANY_S5933_INT		0x00800000L
#	define	READ_TC_INT		0x00080000L
#	define WRITE_TC_INT		0x00040000L
#	define	TX_TC_INT		READ_TC_INT
#	define	RX_TC_INT		WRITE_TC_INT
#	define MASTER_ABORT_INT		0x00100000L
#	define TARGET_ABORT_INT		0x00200000L
#	define BUS_MASTER_INT		0x00200000L
#	define ALL_INT			0x000C0000L

#define	AMCC_MCSR	0x3c
#	define A2P_HI_PRIORITY		0x00000100L
#	define EN_A2P_TRANSFERS		0x00000400L
#	define P2A_HI_PRIORITY		0x00001000L
#	define EN_P2A_TRANSFERS		0x00004000L
#	define RESET_A2P_FLAGS		0x04000000L
#	define RESET_P2A_FLAGS		0x02000000L

/* ------------------------------------------------------------- */

#define t1outmeml(addr, value)	writel(value, addr)
#define t1inmeml(addr)	readl(addr)
#define t1outmemw(addr, value)	writew(value, addr)
#define t1inmemw(addr)	readw(addr)
#define t1outmemb(addr, value)	writeb(value, addr)
#define t1inmemb(addr)	readb(addr)

/* ------------------------------------------------------------- */

static inline int t1pci_tx_empty(unsigned int port)
{
	return inb(port + 0x03) & 0x1;
}

static inline int t1pci_rx_full(unsigned int port)
{
	return inb(port + 0x02) & 0x1;
}

static int t1pci_tolink(avmcard *card, void *buf, unsigned int len)
{
	unsigned long stop = jiffies + 1 * HZ;	/* maximum wait time 1 sec */
	unsigned char *s = (unsigned char *)buf;
	while (len--) {
		while (   !t1pci_tx_empty(card->port)
		       && time_before(jiffies, stop));
		if (!t1pci_tx_empty(card->port)) 
			return -1;
	        t1outp(card->port, 0x01, *s++);
	}
	return 0;
}

static int t1pci_fromlink(avmcard *card, void *buf, unsigned int len)
{
	unsigned long stop = jiffies + 1 * HZ;	/* maximum wait time 1 sec */
	unsigned char *s = (unsigned char *)buf;
	while (len--) {
		while (   !t1pci_rx_full(card->port)
		       && time_before(jiffies, stop));
		if (!t1pci_rx_full(card->port)) 
			return -1;
	        *s++ = t1inp(card->port, 0x00);
	}
	return 0;
}

static int WriteReg(avmcard *card, __u32 reg, __u8 val)
{
	__u8 cmd = 0x00;
	if (   t1pci_tolink(card, &cmd, 1) == 0
	    && t1pci_tolink(card, &reg, 4) == 0) {
		__u32 tmp = val;
		return t1pci_tolink(card, &tmp, 4);
	}
	return -1;
}

static __u8 ReadReg(avmcard *card, __u32 reg)
{
	__u8 cmd = 0x01;
	if (   t1pci_tolink(card, &cmd, 1) == 0
	    && t1pci_tolink(card, &reg, 4) == 0) {
		__u32 tmp;
		if (t1pci_fromlink(card, &tmp, 4) == 0)
			return (__u8)tmp;
	}
	return 0xff;
}

/* ------------------------------------------------------------- */

static inline void _put_byte(void **pp, __u8 val)
{
	__u8 *s = *pp;
	*s++ = val;
	*pp = s;
}

static inline void _put_word(void **pp, __u32 val)
{
	__u8 *s = *pp;
	*s++ = val & 0xff;
	*s++ = (val >> 8) & 0xff;
	*s++ = (val >> 16) & 0xff;
	*s++ = (val >> 24) & 0xff;
	*pp = s;
}

static inline void _put_slice(void **pp, unsigned char *dp, unsigned int len)
{
	unsigned i = len;
	_put_word(pp, i);
	while (i-- > 0)
		_put_byte(pp, *dp++);
}

static inline __u8 _get_byte(void **pp)
{
	__u8 *s = *pp;
	__u8 val;
	val = *s++;
	*pp = s;
	return val;
}

static inline __u32 _get_word(void **pp)
{
	__u8 *s = *pp;
	__u32 val;
	val = *s++;
	val |= (*s++ << 8);
	val |= (*s++ << 16);
	val |= (*s++ << 24);
	*pp = s;
	return val;
}

static inline __u32 _get_slice(void **pp, unsigned char *dp)
{
	unsigned int len, i;

	len = i = _get_word(pp);
	while (i-- > 0) *dp++ = _get_byte(pp);
	return len;
}

/* ------------------------------------------------------------- */

static void t1pci_reset(avmcard *card)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	card->csr = 0x0;
	t1outmeml(card->mbase+AMCC_INTCSR, card->csr);
	t1outmeml(card->mbase+AMCC_MCSR, 0);
	t1outmeml(card->mbase+AMCC_RXLEN, 0);
	t1outmeml(card->mbase+AMCC_TXLEN, 0);

	t1outp(card->port, T1_RESETLINK, 0x00);
	t1outp(card->port, 0x07, 0x00);

	restore_flags(flags);

	t1outmeml(card->mbase+AMCC_MCSR, 0);
	udelay(10 * 1000);
	t1outmeml(card->mbase+AMCC_MCSR, 0x0f000000); /* reset all */
	udelay(10 * 1000);
	t1outmeml(card->mbase+AMCC_MCSR, 0);
	udelay(42 * 1000);

}

/* ------------------------------------------------------------- */

static int t1pci_detect(avmcard *card)
{
	t1outmeml(card->mbase+AMCC_MCSR, 0);
	udelay(10 * 1000);
	t1outmeml(card->mbase+AMCC_MCSR, 0x0f000000); /* reset all */
	udelay(10 * 1000);
	t1outmeml(card->mbase+AMCC_MCSR, 0);
	udelay(42 * 1000);

	t1outmeml(card->mbase+AMCC_RXLEN, 0);
	t1outmeml(card->mbase+AMCC_TXLEN, 0);
	card->csr = 0x0;
	t1outmeml(card->mbase+AMCC_INTCSR, card->csr);

	if (t1inmeml(card->mbase+AMCC_MCSR) != 0x000000E6)
		return 1;

	t1outmeml(card->mbase+AMCC_RXPTR, 0xffffffff);
	t1outmeml(card->mbase+AMCC_TXPTR, 0xffffffff);
	if (   t1inmeml(card->mbase+AMCC_RXPTR) != 0xfffffffc
	    || t1inmeml(card->mbase+AMCC_TXPTR) != 0xfffffffc)
		return 2;

	t1outmeml(card->mbase+AMCC_RXPTR, 0x0);
	t1outmeml(card->mbase+AMCC_TXPTR, 0x0);
	if (   t1inmeml(card->mbase+AMCC_RXPTR) != 0x0
	    || t1inmeml(card->mbase+AMCC_TXPTR) != 0x0)
		return 3;

	t1outp(card->port, T1_RESETLINK, 0x00);
	t1outp(card->port, 0x07, 0x00);
	
	t1outp(card->port, 0x02, 0x02);
	t1outp(card->port, 0x03, 0x02);

	if (   (t1inp(card->port, 0x02) & 0xFE) != 0x02
	    || t1inp(card->port, 0x3) != 0x03)
		return 4;

	t1outp(card->port, 0x02, 0x00);
	t1outp(card->port, 0x03, 0x00);

	if (   (t1inp(card->port, 0x02) & 0xFE) != 0x00
	    || t1inp(card->port, 0x3) != 0x01)
		return 5;

	/* Transputer test */
	
	if (   WriteReg(card, 0x80001000, 0x11) != 0
	    || WriteReg(card, 0x80101000, 0x22) != 0
	    || WriteReg(card, 0x80201000, 0x33) != 0
	    || WriteReg(card, 0x80301000, 0x44) != 0)
		return 6;

	if (   ReadReg(card, 0x80001000) != 0x11
	    || ReadReg(card, 0x80101000) != 0x22
	    || ReadReg(card, 0x80201000) != 0x33
	    || ReadReg(card, 0x80301000) != 0x44)
		return 7;

	if (   WriteReg(card, 0x80001000, 0x55) != 0
	    || WriteReg(card, 0x80101000, 0x66) != 0
	    || WriteReg(card, 0x80201000, 0x77) != 0
	    || WriteReg(card, 0x80301000, 0x88) != 0)
		return 8;

	if (   ReadReg(card, 0x80001000) != 0x55
	    || ReadReg(card, 0x80101000) != 0x66
	    || ReadReg(card, 0x80201000) != 0x77
	    || ReadReg(card, 0x80301000) != 0x88)
		return 9;

	return 0;
}

/* ------------------------------------------------------------- */

static void t1pci_dispatch_tx(avmcard *card)
{
	avmcard_dmainfo *dma = card->dma;
	unsigned long flags;
	struct sk_buff *skb;
	__u8 cmd, subcmd;
	__u16 len;
	__u32 txlen;
	int inint;
	void *p;
	
	save_flags(flags);
	cli();

	inint = card->interrupt;

	if (card->csr & EN_TX_TC_INT) { /* tx busy */
	        restore_flags(flags);
		return;
	}

	skb = skb_dequeue(&dma->send_queue);
	if (!skb) {
#ifdef CONFIG_T1PCI_DEBUG
		printk(KERN_DEBUG "tx(%d): underrun\n", inint);
#endif
	        restore_flags(flags);
		return;
	}

	len = CAPIMSG_LEN(skb->data);

	if (len) {
		cmd = CAPIMSG_COMMAND(skb->data);
		subcmd = CAPIMSG_SUBCOMMAND(skb->data);

		p = dma->sendbuf;

		if (CAPICMD(cmd, subcmd) == CAPI_DATA_B3_REQ) {
			__u16 dlen = CAPIMSG_DATALEN(skb->data);
			_put_byte(&p, SEND_DATA_B3_REQ);
			_put_slice(&p, skb->data, len);
			_put_slice(&p, skb->data + len, dlen);
		} else {
			_put_byte(&p, SEND_MESSAGE);
			_put_slice(&p, skb->data, len);
		}
		txlen = (__u8 *)p - (__u8 *)dma->sendbuf;
#ifdef CONFIG_T1PCI_DEBUG
		printk(KERN_DEBUG "tx(%d): put msg len=%d\n",
				inint, txlen);
#endif
	} else {
		txlen = skb->len-2;
#ifdef CONFIG_T1PCI_POLLDEBUG
		if (skb->data[2] == SEND_POLLACK)
			printk(KERN_INFO "%s: ack to t1\n", card->name);
#endif
#ifdef CONFIG_T1PCI_DEBUG
		printk(KERN_DEBUG "tx(%d): put 0x%x len=%d\n",
				inint, skb->data[2], txlen);
#endif
		memcpy(dma->sendbuf, skb->data+2, skb->len-2);
	}
	txlen = (txlen + 3) & ~3;

	t1outmeml(card->mbase+AMCC_TXPTR, virt_to_phys(dma->sendbuf));
	t1outmeml(card->mbase+AMCC_TXLEN, txlen);

	card->csr |= EN_TX_TC_INT;

	if (!inint)
		t1outmeml(card->mbase+AMCC_INTCSR, card->csr);

	restore_flags(flags);
	dev_kfree_skb(skb);
}

/* ------------------------------------------------------------- */

static void queue_pollack(avmcard *card)
{
	struct sk_buff *skb;
	void *p;

	skb = alloc_skb(3, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_CRIT "%s: no memory, lost poll ack\n",
					card->name);
		return;
	}
	p = skb->data;
	_put_byte(&p, 0);
	_put_byte(&p, 0);
	_put_byte(&p, SEND_POLLACK);
	skb_put(skb, (__u8 *)p - (__u8 *)skb->data);

	skb_queue_tail(&card->dma->send_queue, skb);
	t1pci_dispatch_tx(card);
}

/* ------------------------------------------------------------- */

static void t1pci_handle_rx(avmcard *card)
{
	avmctrl_info *cinfo = &card->ctrlinfo[0];
	avmcard_dmainfo *dma = card->dma;
	struct capi_ctr *ctrl = cinfo->capi_ctrl;
	struct sk_buff *skb;
	void *p = dma->recvbuf+4;
	__u32 ApplId, MsgLen, DataB3Len, NCCI, WindowSize;
	__u8 b1cmd =  _get_byte(&p);

#ifdef CONFIG_T1PCI_DEBUG
	printk(KERN_DEBUG "rx: 0x%x %lu\n", b1cmd, (unsigned long)dma->recvlen);
#endif
	
	switch (b1cmd) {
	case RECEIVE_DATA_B3_IND:

		ApplId = (unsigned) _get_word(&p);
		MsgLen = _get_slice(&p, card->msgbuf);
		DataB3Len = _get_slice(&p, card->databuf);

		if (MsgLen < 30) { /* not CAPI 64Bit */
			memset(card->msgbuf+MsgLen, 0, 30-MsgLen);
			MsgLen = 30;
			CAPIMSG_SETLEN(card->msgbuf, 30);
		}
		if (!(skb = alloc_skb(DataB3Len+MsgLen, GFP_ATOMIC))) {
			printk(KERN_ERR "%s: incoming packet dropped\n",
					card->name);
		} else {
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			memcpy(skb_put(skb, DataB3Len), card->databuf, DataB3Len);
			ctrl->handle_capimsg(ctrl, ApplId, skb);
		}
		break;

	case RECEIVE_MESSAGE:

		ApplId = (unsigned) _get_word(&p);
		MsgLen = _get_slice(&p, card->msgbuf);
		if (!(skb = alloc_skb(MsgLen, GFP_ATOMIC))) {
			printk(KERN_ERR "%s: incoming packet dropped\n",
					card->name);
		} else {
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			ctrl->handle_capimsg(ctrl, ApplId, skb);
		}
		break;

	case RECEIVE_NEW_NCCI:

		ApplId = _get_word(&p);
		NCCI = _get_word(&p);
		WindowSize = _get_word(&p);

		ctrl->new_ncci(ctrl, ApplId, NCCI, WindowSize);

		break;

	case RECEIVE_FREE_NCCI:

		ApplId = _get_word(&p);
		NCCI = _get_word(&p);

		if (NCCI != 0xffffffff)
			ctrl->free_ncci(ctrl, ApplId, NCCI);
		else ctrl->appl_released(ctrl, ApplId);
		break;

	case RECEIVE_START:
#ifdef CONFIG_T1PCI_POLLDEBUG
		printk(KERN_INFO "%s: poll from t1\n", card->name);
#endif
		if (!suppress_pollack)
			queue_pollack(card);
		ctrl->resume_output(ctrl);
		break;

	case RECEIVE_STOP:
		ctrl->suspend_output(ctrl);
		break;

	case RECEIVE_INIT:

		cinfo->versionlen = _get_slice(&p, cinfo->versionbuf);
		b1_parse_version(cinfo);
		printk(KERN_INFO "%s: %s-card (%s) now active\n",
		       card->name,
		       cinfo->version[VER_CARDTYPE],
		       cinfo->version[VER_DRIVER]);
		ctrl->ready(ctrl);
		break;

	case RECEIVE_TASK_READY:
		ApplId = (unsigned) _get_word(&p);
		MsgLen = _get_slice(&p, card->msgbuf);
		card->msgbuf[MsgLen--] = 0;
		while (    MsgLen >= 0
		       && (   card->msgbuf[MsgLen] == '\n'
			   || card->msgbuf[MsgLen] == '\r'))
			card->msgbuf[MsgLen--] = 0;
		printk(KERN_INFO "%s: task %d \"%s\" ready.\n",
				card->name, ApplId, card->msgbuf);
		break;

	case RECEIVE_DEBUGMSG:
		MsgLen = _get_slice(&p, card->msgbuf);
		card->msgbuf[MsgLen--] = 0;
		while (    MsgLen >= 0
		       && (   card->msgbuf[MsgLen] == '\n'
			   || card->msgbuf[MsgLen] == '\r'))
			card->msgbuf[MsgLen--] = 0;
		printk(KERN_INFO "%s: DEBUG: %s\n", card->name, card->msgbuf);
		break;

	default:
		printk(KERN_ERR "%s: t1pci_interrupt: 0x%x ???\n",
				card->name, b1cmd);
		return;
	}
}

/* ------------------------------------------------------------- */

static void t1pci_handle_interrupt(avmcard *card)
{
	__u32 status = t1inmeml(card->mbase+AMCC_INTCSR);
	__u32 newcsr;

	if ((status & ANY_S5933_INT) == 0) 
		return;

        newcsr = card->csr | (status & ALL_INT);
	if (status & TX_TC_INT) newcsr &= ~EN_TX_TC_INT;
	if (status & RX_TC_INT) newcsr &= ~EN_RX_TC_INT;
	t1outmeml(card->mbase+AMCC_INTCSR, newcsr);

	if ((status & RX_TC_INT) != 0) {
		__u8 *recvbuf = card->dma->recvbuf;
		__u32 rxlen;
	   	if (card->dma->recvlen == 0) {
			card->dma->recvlen = *((__u32 *)recvbuf);
			rxlen = (card->dma->recvlen + 3) & ~3;
			t1outmeml(card->mbase+AMCC_RXPTR,
					virt_to_phys(recvbuf+4));
			t1outmeml(card->mbase+AMCC_RXLEN, rxlen);
		} else {
			t1pci_handle_rx(card);
	   		card->dma->recvlen = 0;
			t1outmeml(card->mbase+AMCC_RXPTR, virt_to_phys(recvbuf));
			t1outmeml(card->mbase+AMCC_RXLEN, 4);
		}
	}

	if ((status & TX_TC_INT) != 0) {
		card->csr &= ~EN_TX_TC_INT;
	        t1pci_dispatch_tx(card);
	} else if (card->csr & EN_TX_TC_INT) {
		if (t1inmeml(card->mbase+AMCC_TXLEN) == 0) {
			card->csr &= ~EN_TX_TC_INT;
			t1pci_dispatch_tx(card);
		}
	}
	t1outmeml(card->mbase+AMCC_INTCSR, card->csr);
}

static void t1pci_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
{
	avmcard *card;

	card = (avmcard *) devptr;

	if (!card) {
		printk(KERN_WARNING "t1pci: interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "%s: reentering interrupt hander\n", card->name);
		return;
	}

	card->interrupt = 1;

	t1pci_handle_interrupt(card);

	card->interrupt = 0;
}

/* ------------------------------------------------------------- */

static int t1pci_loaded(avmcard *card)
{
	unsigned long stop;
	unsigned char ans;
	unsigned long tout = 2;
	unsigned int base = card->port;

	for (stop = jiffies + tout * HZ; time_before(jiffies, stop);) {
		if (b1_tx_empty(base))
			break;
	}
	if (!b1_tx_empty(base)) {
		printk(KERN_ERR "%s: t1pci_loaded: tx err, corrupted t4 file ?\n",
				card->name);
		return 0;
	}
	b1_put_byte(base, SEND_POLLACK);
	for (stop = jiffies + tout * HZ; time_before(jiffies, stop);) {
		if (b1_rx_full(base)) {
			if ((ans = b1_get_byte(base)) == RECEIVE_POLLDWORD) {
				return 1;
			}
			printk(KERN_ERR "%s: t1pci_loaded: got 0x%x, firmware not running in dword mode\n", card->name, ans);
			return 0;
		}
	}
	printk(KERN_ERR "%s: t1pci_loaded: firmware not running\n", card->name);
	return 0;
}

/* ------------------------------------------------------------- */

static void t1pci_send_init(avmcard *card)
{
	struct sk_buff *skb;
	void *p;

	skb = alloc_skb(15, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_CRIT "%s: no memory, lost register appl.\n",
					card->name);
		return;
	}
	p = skb->data;
	_put_byte(&p, 0);
	_put_byte(&p, 0);
	_put_byte(&p, SEND_INIT);
	_put_word(&p, AVM_NAPPS);
	_put_word(&p, AVM_NCCI_PER_CHANNEL*30);
	_put_word(&p, card->cardnr - 1);
	skb_put(skb, (__u8 *)p - (__u8 *)skb->data);

	skb_queue_tail(&card->dma->send_queue, skb);
	t1pci_dispatch_tx(card);
}

static int t1pci_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned long flags;
	int retval;

	t1pci_reset(card);

	if ((retval = b1_load_t4file(card, &data->firmware))) {
		t1pci_reset(card);
		printk(KERN_ERR "%s: failed to load t4file!!\n",
					card->name);
		return retval;
	}

	if (data->configuration.len > 0 && data->configuration.data) {
		if ((retval = b1_load_config(card, &data->configuration))) {
			t1pci_reset(card);
			printk(KERN_ERR "%s: failed to load config!!\n",
					card->name);
			return retval;
		}
	}

	if (!t1pci_loaded(card)) {
		t1pci_reset(card);
		printk(KERN_ERR "%s: failed to load t4file.\n", card->name);
		return -EIO;
	}

	save_flags(flags);
	cli();

	card->csr = AVM_FLAG;
	t1outmeml(card->mbase+AMCC_INTCSR, card->csr);
	t1outmeml(card->mbase+AMCC_MCSR,
		EN_A2P_TRANSFERS|EN_P2A_TRANSFERS
		|A2P_HI_PRIORITY|P2A_HI_PRIORITY
		|RESET_A2P_FLAGS|RESET_P2A_FLAGS);
	t1outp(card->port, 0x07, 0x30);
	t1outp(card->port, 0x10, 0xF0);

	card->dma->recvlen = 0;
	t1outmeml(card->mbase+AMCC_RXPTR, virt_to_phys(card->dma->recvbuf));
	t1outmeml(card->mbase+AMCC_RXLEN, 4);
	card->csr |= EN_RX_TC_INT;
	t1outmeml(card->mbase+AMCC_INTCSR, card->csr);
	restore_flags(flags);

        t1pci_send_init(card);

	return 0;
}

void t1pci_reset_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;

 	t1pci_reset(card);

	memset(cinfo->version, 0, sizeof(cinfo->version));
	ctrl->reseted(ctrl);
}

static void t1pci_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;

 	t1pci_reset(card);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	iounmap((void *) (((unsigned long) card->mbase) & PAGE_MASK));
	release_region(card->port, AVMB1_PORTLEN);
	ctrl->driverdata = 0;
	kfree(card->ctrlinfo);
	kfree(card->dma);
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */


void t1pci_register_appl(struct capi_ctr *ctrl,
				__u16 appl,
				capi_register_params *rp)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	struct sk_buff *skb;
	int want = rp->level3cnt;
	int nconn;
	void *p;

	if (want > 0) nconn = want;
	else nconn = ctrl->profile.nbchannel * -want;
	if (nconn == 0) nconn = ctrl->profile.nbchannel;

	skb = alloc_skb(23, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_CRIT "%s: no memory, lost register appl.\n",
					card->name);
		return;
	}
	p = skb->data;
	_put_byte(&p, 0);
	_put_byte(&p, 0);
	_put_byte(&p, SEND_REGISTER);
	_put_word(&p, appl);
	_put_word(&p, 1024 * (nconn+1));
	_put_word(&p, nconn);
	_put_word(&p, rp->datablkcnt);
	_put_word(&p, rp->datablklen);
	skb_put(skb, (__u8 *)p - (__u8 *)skb->data);

	skb_queue_tail(&card->dma->send_queue, skb);
	t1pci_dispatch_tx(card);

	ctrl->appl_registered(ctrl, appl);
}

/* ------------------------------------------------------------- */

void t1pci_release_appl(struct capi_ctr *ctrl, __u16 appl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	struct sk_buff *skb;
	void *p;

	skb = alloc_skb(7, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_CRIT "%s: no memory, lost release appl.\n",
					card->name);
		return;
	}
	p = skb->data;
	_put_byte(&p, 0);
	_put_byte(&p, 0);
	_put_byte(&p, SEND_RELEASE);
	_put_word(&p, appl);

	skb_put(skb, (__u8 *)p - (__u8 *)skb->data);
	skb_queue_tail(&card->dma->send_queue, skb);
	t1pci_dispatch_tx(card);
}

/* ------------------------------------------------------------- */


static void t1pci_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	skb_queue_tail(&card->dma->send_queue, skb);
	t1pci_dispatch_tx(card);
}

/* ------------------------------------------------------------- */

static char *t1pci_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d 0x%lx",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->membase : 0
		);
	return cinfo->infobuf;
}

static int t1pci_read_proc(char *page, char **start, off_t off,
        		int count, int *eof, struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned long flags;
	__u8 flag;
	int len = 0;
	char *s;
	__u32 txaddr, txlen, rxaddr, rxlen, csr;

	len += sprintf(page+len, "%-16s %s\n", "name", card->name);
	len += sprintf(page+len, "%-16s 0x%x\n", "io", card->port);
	len += sprintf(page+len, "%-16s %d\n", "irq", card->irq);
	len += sprintf(page+len, "%-16s 0x%lx\n", "membase", card->membase);
	switch (card->cardtype) {
	case avm_b1isa: s = "B1 ISA"; break;
	case avm_b1pci: s = "B1 PCI"; break;
	case avm_b1pcmcia: s = "B1 PCMCIA"; break;
	case avm_m1: s = "M1"; break;
	case avm_m2: s = "M2"; break;
	case avm_t1isa: s = "T1 ISA (HEMA)"; break;
	case avm_t1pci: s = "T1 PCI"; break;
	case avm_c4: s = "C4"; break;
	default: s = "???"; break;
	}
	len += sprintf(page+len, "%-16s %s\n", "type", s);
	if ((s = cinfo->version[VER_DRIVER]) != 0)
	   len += sprintf(page+len, "%-16s %s\n", "ver_driver", s);
	if ((s = cinfo->version[VER_CARDTYPE]) != 0)
	   len += sprintf(page+len, "%-16s %s\n", "ver_cardtype", s);
	if ((s = cinfo->version[VER_SERIAL]) != 0)
	   len += sprintf(page+len, "%-16s %s\n", "ver_serial", s);

	if (card->cardtype != avm_m1) {
        	flag = ((__u8 *)(ctrl->profile.manu))[3];
        	if (flag)
			len += sprintf(page+len, "%-16s%s%s%s%s%s%s%s\n",
			"protocol",
			(flag & 0x01) ? " DSS1" : "",
			(flag & 0x02) ? " CT1" : "",
			(flag & 0x04) ? " VN3" : "",
			(flag & 0x08) ? " NI1" : "",
			(flag & 0x10) ? " AUSTEL" : "",
			(flag & 0x20) ? " ESS" : "",
			(flag & 0x40) ? " 1TR6" : ""
			);
	}
	if (card->cardtype != avm_m1) {
        	flag = ((__u8 *)(ctrl->profile.manu))[5];
		if (flag)
			len += sprintf(page+len, "%-16s%s%s%s%s\n",
			"linetype",
			(flag & 0x01) ? " point to point" : "",
			(flag & 0x02) ? " point to multipoint" : "",
			(flag & 0x08) ? " leased line without D-channel" : "",
			(flag & 0x04) ? " leased line with D-channel" : ""
			);
	}
	len += sprintf(page+len, "%-16s %s\n", "cardname", cinfo->cardname);

	save_flags(flags);
	cli();

	txaddr = (__u32)phys_to_virt(t1inmeml(card->mbase+0x2c));
	txaddr -= (__u32)card->dma->sendbuf;
	txlen  = t1inmeml(card->mbase+0x30);

	rxaddr = (__u32)phys_to_virt(t1inmeml(card->mbase+0x24));
	rxaddr -= (__u32)card->dma->recvbuf;
	rxlen  = t1inmeml(card->mbase+0x28);

	csr  = t1inmeml(card->mbase+AMCC_INTCSR);

	restore_flags(flags);

        len += sprintf(page+len, "%-16s 0x%lx\n",
				"csr (cached)", (unsigned long)card->csr);
        len += sprintf(page+len, "%-16s 0x%lx\n",
				"csr", (unsigned long)csr);
        len += sprintf(page+len, "%-16s %lu\n",
				"txoff", (unsigned long)txaddr);
        len += sprintf(page+len, "%-16s %lu\n",
				"txlen", (unsigned long)txlen);
        len += sprintf(page+len, "%-16s %lu\n",
				"rxoff", (unsigned long)rxaddr);
        len += sprintf(page+len, "%-16s %lu\n",
				"rxlen", (unsigned long)rxlen);

	if (off+count >= len)
	   *eof = 1;
	if (len < off)
           return 0;
	*start = page + off;
	return ((count < len-off) ? count : len-off);
}

/* ------------------------------------------------------------- */

static int t1pci_add_card(struct capi_driver *driver, struct capicardparams *p)
{
	unsigned long page_offset, base;
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
	card->dma = (avmcard_dmainfo *) kmalloc(sizeof(avmcard_dmainfo), GFP_ATOMIC);
	if (!card->dma) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		kfree(card);
		return -ENOMEM;
	}
	memset(card->dma, 0, sizeof(avmcard_dmainfo));
        cinfo = (avmctrl_info *) kmalloc(sizeof(avmctrl_info), GFP_ATOMIC);
	if (!cinfo) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		kfree(card->dma);
		kfree(card);
		return -ENOMEM;
	}
	memset(cinfo, 0, sizeof(avmctrl_info));
	card->ctrlinfo = cinfo;
	cinfo->card = card;
	sprintf(card->name, "t1pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->membase = p->membase;
	card->cardtype = avm_t1pci;

	if (check_region(card->port, AVMB1_PORTLEN)) {
		printk(KERN_WARNING
		       "%s: ports 0x%03x-0x%03x in use.\n",
		       driver->name, card->port, card->port + AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
		return -EBUSY;
	}

        base = card->membase & PAGE_MASK;
	page_offset = card->membase - base;
	card->mbase = ioremap_nocache(base, page_offset + 64);

	t1pci_reset(card);

	if ((retval = t1pci_detect(card)) != 0) {
		printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
					driver->name, card->port, retval);
                iounmap((void *) (((unsigned long) card->mbase) & PAGE_MASK));
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
		return -EIO;
	}
	t1pci_reset(card);

	request_region(p->port, AVMB1_PORTLEN, card->name);

	retval = request_irq(card->irq, t1pci_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
				driver->name, card->irq);
                iounmap((void *) (((unsigned long) card->mbase) & PAGE_MASK));
		release_region(card->port, AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
		return -EBUSY;
	}

	cinfo->capi_ctrl = di->attach_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "%s: attach controller failed.\n", driver->name);
                iounmap((void *) (((unsigned long) card->mbase) & PAGE_MASK));
		free_irq(card->irq, card);
		release_region(card->port, AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
		return -EBUSY;
	}
	card->cardnr = cinfo->capi_ctrl->cnr;

	skb_queue_head_init(&card->dma->send_queue);

	MOD_INC_USE_COUNT;

	return 0;
}

/* ------------------------------------------------------------- */

static struct capi_driver t1pci_driver = {
    "t1pci",
    "0.0",
    t1pci_load_firmware,
    t1pci_reset_ctr,
    t1pci_remove_ctr,
    t1pci_register_appl,
    t1pci_release_appl,
    t1pci_send_message,

    t1pci_procinfo,
    t1pci_read_proc,
    0,	/* use standard driver_read_proc */

    0, /* no add_card function */
};

#ifdef MODULE
#define t1pci_init init_module
void cleanup_module(void);
#endif

static int ncards = 0;

int t1pci_init(void)
{
	struct capi_driver *driver = &t1pci_driver;
	struct pci_dev *dev = NULL;
	char *p;
	int retval;

	if ((p = strchr(revision, ':'))) {
		strncpy(driver->revision, p + 1, sizeof(driver->revision));
		p = strchr(driver->revision, '$');
		*p = 0;
	}

	printk(KERN_INFO "%s: revision %s\n", driver->name, driver->revision);

        di = attach_capi_driver(driver);

	if (!di) {
		printk(KERN_ERR "%s: failed to attach capi_driver\n",
				driver->name);
		return -EIO;
	}

#ifdef CONFIG_PCI
	if (!pci_present()) {
		printk(KERN_ERR "%s: no PCI bus present\n", driver->name);
    		detach_capi_driver(driver);
		return -EIO;
	}

	while ((dev = pci_find_device(PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_T1, dev))) {
		struct capicardparams param;

		param.port = dev->resource[ 1].start & PCI_BASE_ADDRESS_IO_MASK;
		param.irq = dev->irq;
		param.membase = dev->resource[ 0].start & PCI_BASE_ADDRESS_MEM_MASK;

		printk(KERN_INFO
			"%s: PCI BIOS reports AVM-T1-PCI at i/o %#x, irq %d, mem %#x\n",
			driver->name, param.port, param.irq, param.membase);
		retval = t1pci_add_card(driver, &param);
		if (retval != 0) {
		        printk(KERN_ERR
			"%s: no AVM-T1-PCI at i/o %#x, irq %d detected, mem %#x\n",
			driver->name, param.port, param.irq, param.membase);
#ifdef MODULE
			cleanup_module();
#endif
			return retval;
		}
		ncards++;
	}
	if (ncards) {
		printk(KERN_INFO "%s: %d T1-PCI card(s) detected\n",
				driver->name, ncards);
		return 0;
	}
	printk(KERN_ERR "%s: NO T1-PCI card detected\n", driver->name);
	return -ESRCH;
#else
	printk(KERN_ERR "%s: kernel not compiled with PCI.\n", driver->name);
	return -EIO;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
    detach_capi_driver(&t1pci_driver);
}
#endif

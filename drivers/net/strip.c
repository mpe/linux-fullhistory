/*
 * Copyright 1996 The Board of Trustees of The Leland Stanford
 * Junior University. All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  Stanford University
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * strip.c	This module implements Starmode Radio IP (STRIP)
 *		for kernel-based devices like TTY.  It interfaces between a
 *		raw TTY, and the kernel's INET protocol layers (via DDI).
 *
 * Version:	@(#)strip.c	0.9.1	3/6/95
 *
 * Author:	Stuart Cheshire <cheshire@cs.stanford.edu>
 *
 * Fixes:
 *		Stuart Cheshire:
 *			Original version converted from SLIP driver
 *		Jonathan Stone:
 *			change to 1.3 calling conventions
 *		Stuart Cheshire:
 *			v0.9 12th Feb 1996.
 *			New byte stuffing (2+6 run-length encoding)
 *			New watchdog timer task
 *			New Protocol key (SIP0)
 *			v0.9.1 3rd March 1996
 *			Changed to dynamic device allocation
 */

#include <linux/config.h>
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_strip.h>
#include <net/arp.h>
#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/tcp.h>
#endif

#ifdef MODULE
#define STRIP_VERSION    "0.9.1-STUART.CHESHIRE-MODULAR"
#else
#define STRIP_VERSION    "0.9.1-STUART.CHESHIRE"
#endif

#define STRIP_MTU 1024
#define STRIP_MAGIC 0x5303

/*
 *	Do we still needs all these flags? 
 */

enum 
{
	STR_INUSE = 0,		/* Channel in use	*/
	STR_ESCAPE,			/* ESC received		*/
	STR_ERROR			/* Parity, etc. error	*/
}
STRIP_FLAGS;

struct strip 
{
	int magic;
	/*
	 *	Other useful structures. 
	 */

	/*
	 *	These are pointers to the malloc()ed frame buffers.
	 */

	unsigned char     *rx_buff;		/* buffer for received IP packet*/
	unsigned char     *sx_buff;		/* buffer for received serial data*/
	int                sx_count;		/* received serial data counter */
	unsigned char     *tx_buff;		/* transmitter buffer  */
	unsigned char     *tx_head;		/* pointer to next byte to XMIT */
	int                tx_left;		/* bytes left in XMIT queue     */

	/*
	 *	STRIP interface statistics.
	 */
	 
	unsigned long      rx_packets;		/* inbound frames counter	*/
	unsigned long      tx_packets;		/* outbound frames counter	*/
	unsigned long      rx_errors;		/* Parity, etc. errors		*/
	unsigned long      tx_errors;		/* Planned stuff		*/
	unsigned long      rx_dropped;		/* No memory for skb		*/
	unsigned long      tx_dropped;		/* When MTU change		*/
	unsigned long      rx_over_errors;	/* Frame bigger then STRIP buf. */

	/*
	 *	Internal variables.
	 */
	 
	struct strip      *next;		/* The next struct in the list 	*/
	struct strip     **referrer;		/* The pointer that points to us */
	unsigned char      flags;		/* Flag values/ mode etc	*/
	int                mtu;			/* Our mtu (to spot changes!)	*/
	int                buffsize;		/* Max buffers sizes		*/
	long               watchdog_doprobe;	/* Next time to test the radio	*/
	long               watchdog_doreset;	/* Time to do next reset	*/
	struct timer_list  idle_timer;

	struct tty_struct *tty;			/* ptr to TTY structure		*/
	char               if_name[8];		/* Dynamically generated name	*/
	struct device       dev;		/* Our device structure		*/
};
/************************************************************************/
/* Utility routines for disabling and restoring interrupts		*/

typedef unsigned long InterruptStatus;

extern __inline__ InterruptStatus DisableInterrupts(void)
{
    InterruptStatus x;
    save_flags(x);
    cli();
    return(x);
}

extern __inline__ void RestoreInterrupts(InterruptStatus x)
{
    restore_flags(x);
}

/************************************************************************/
/* Useful structures and definitions					*/

typedef struct {
    __u8 c[32];
} RadioName;

typedef struct {
    __u8 c[ 4];
} MetricomKey;

typedef union {
    __u8 b[ 4];
    __u32 l;
} IPaddr;

static const MetricomKey ProtocolKey = 
{
    {
        "SIP0"
    }
};

enum 
{
    FALSE = 0,
    TRUE = 1
};

#define LONG_TIME 0x7FFFFFFF

typedef struct 
{
	RadioName   name;	/* The address, with delimiters eg. *0000-1164* */
	MetricomKey key;	/* Protocol type */
} STRIP_Header;

typedef struct 
{
	STRIP_Header h;
	__u8 data[4];		/* Placeholder for payload (The IP packet) */
} STRIP_Packet;

/*
 *	STRIP_ENCAP_SIZE of an IP packet is the STRIP header at the front,
 *	byte-stuffing overhead of the payload, plus the CR at the end
 */
 
#define STRIP_ENCAP_SIZE(X) (sizeof(STRIP_Header) + (X)*65L/64L + 2)

/*
 *	Note: A Metricom packet looks like this: *<address>*<key><payload><CR>
 *	eg. *0000-1164*SIP0<payload><CR>
 */

static struct strip *struct_strip_list = NULL;

/************************************************************************/
/* Byte stuffing/unstuffing routines					*/

/* Stuffing scheme:
 * 00    Unused (reserved character)
 * 01-3F Run of 2-64 different characters
 * 40-7F Run of 1-64 different characters plus a single zero at the end
 * 80-BF Run of 1-64 of the same character
 * C0-FF Run of 1-64 zeroes (ASCII 0)
 */

typedef enum 
{
    Stuff_Diff      = 0x00,
    Stuff_DiffZero  = 0x40,
    Stuff_Same      = 0x80,
    Stuff_Zero      = 0xC0,
    Stuff_NoCode    = 0xFF,	/* Special code, meaning no code selected */
    
    Stuff_CodeMask  = 0xC0,
    Stuff_CountMask = 0x3F,
    Stuff_MaxCount  = 0x3F,
    Stuff_Magic     = 0x0D	/* The value we are eliminating */
} StuffingCode;

/* StuffData encodes the data starting at "src" for "length" bytes.
 * It writes it to the buffer pointed to by "dst" (which must be at least
 * as long as 1 + 65/64 of the input length). The output may be up to 1.6%
 * larger than the input for pathological input, but will usually be smaller.
 * StuffData returns the new value of the dst pointer as its result.
 * "code_ptr_ptr" points to a "__u8 *" which is used to hold encoding state
 * between calls, allowing an encoded packet to be incrementally built up
 * from small parts. On the first call, the "__u8 *" pointed to should be
 * initialized to NULL; between subsequent calls the calling routine should
 * leave the value alone and simply pass it back unchanged so that the
 * encoder can recover its current state.
 */

#define StuffData_FinishBlock(X) \
(*code_ptr = (X) ^ Stuff_Magic, code = Stuff_NoCode)

static __u8 *StuffData(__u8 *src, __u32 length, __u8 *dst, __u8 **code_ptr_ptr)
{
	__u8 *end = src + length;
	__u8 *code_ptr = *code_ptr_ptr;
 	__u8 code = Stuff_NoCode, count = 0;
    
	if (!length) 
		return(dst);
    
	if (code_ptr) 
	{
		/*
		 *	Recover state from last call, if applicable 
		 */
		code  = (*code_ptr ^ Stuff_Magic) & Stuff_CodeMask;
		count = (*code_ptr ^ Stuff_Magic) & Stuff_CountMask;
	}

	while (src < end) 
	{
		switch (code) 
		{
			/* Stuff_NoCode: If no current code, select one */
			case Stuff_NoCode:
				/* Record where we're going to put this code */
				code_ptr = dst++;
				count = 0;	/* Reset the count (zero means one instance) */
				/* Tentatively start a new block */
				if (*src == 0) 
				{
					code = Stuff_Zero;
        				src++;
				}
				else
				{
					code = Stuff_Same;
					*dst++ = *src++ ^ Stuff_Magic;
				}
				/* Note: We optimistically assume run of same -- */
				/* which will be fixed later in Stuff_Same */
				/* if it turns out not to be true. */
				break;

			/* Stuff_Zero: We already have at least one zero encoded */
			case Stuff_Zero:
                		/* If another zero, count it, else finish this code block */
                		if (*src == 0) 
                		{
					count++;
					src++;
				}
				else 
				{
					StuffData_FinishBlock(Stuff_Zero + count);
        			}
				break;

			/* Stuff_Same: We already have at least one byte encoded */
			case Stuff_Same:
				/* If another one the same, count it */
				if ((*src ^ Stuff_Magic) == code_ptr[1]) 
				{
					count++;
					src++;
					break;
				}
				/* else, this byte does not match this block. */
        			/* If we already have two or more bytes encoded, */
				/* finish this code block */
				if (count) 
				{
					StuffData_FinishBlock(Stuff_Same + count);
					break;
				}
				/* else, we only have one so far, */
				/* so switch to Stuff_Diff code */
				code = Stuff_Diff;
				/* and fall through to Stuff_Diff case below */
			 /* Stuff_Diff: We have at least two *different* bytes encoded */
        		case Stuff_Diff:
				/* If this is a zero, must encode a Stuff_DiffZero, */
				/* and begin a new block */
				if (*src == 0) 
				{
					StuffData_FinishBlock(Stuff_DiffZero + count);
				}
				/* else, if we have three in a row, it is worth starting */
				/* a Stuff_Same block */
				else if ((*src ^ Stuff_Magic)==dst[-1] && dst[-1]==dst[-2]) 
				{
				/* Back off the last two characters we encoded */
				code += count-2;
				/* Note: "Stuff_Diff + 0" is an illegal code */
				if (code == Stuff_Diff + 0) 
				{
					code = Stuff_Same + 0;
				}
				StuffData_FinishBlock(code);
				code_ptr = dst-2;
				/* dst[-1] already holds the correct value */
				count = 2;		/* 2 means three bytes encoded */
				code = Stuff_Same;
			}
			/* else, another different byte, so add it to the block */
			else 
                	{
                		*dst++ = *src ^ Stuff_Magic;
                		count++;
                	}
                	src++;	/* Consume the byte */
                	break;
		}
		if (count == Stuff_MaxCount) 
		{
			StuffData_FinishBlock(code + count);
		}
	}
	if (code == Stuff_NoCode) 
	{
		*code_ptr_ptr = NULL;
	}
	else 
	{
		*code_ptr_ptr = code_ptr;
		StuffData_FinishBlock(code + count);
	}	
	return(dst);
}

/* UnStuffData decodes the data at "src", up to (but not including) "end".
It writes the decoded data into the buffer pointed to by "dst", up to a
maximum of "dst_length", and returns the new value of "src" so that a
follow-on call can read more data, continuing from where the first left off.

There are three types of results:
1. The source data runs out before extracting "dst_length" bytes:
   UnStuffData returns NULL to indicate failure.
2. The source data produces exactly "dst_length" bytes:
   UnStuffData returns new_src = end to indicate that all bytes were consumed.
3. "dst_length" bytes are extracted, with more remaining.
   UnStuffData returns new_src < end to indicate that there are more bytes
   to be read.

Note: The decoding may be destructive, in that it may alter the source
data in the process of decoding it (this is necessary to allow a follow-on
call to resume correctly). */

static __u8 *UnStuffData(__u8 *src, __u8 *end, __u8 *dst, __u32 dst_length)
{
	__u8 *dst_end = dst + dst_length;
	/* Sanity check */
	if (!src || !end || !dst || !dst_length) 
		return(NULL);
	while (src < end && dst < dst_end) 
	{
		int count = (*src ^ Stuff_Magic) & Stuff_CountMask;
		switch ((*src ^ Stuff_Magic) & Stuff_CodeMask) 
		{
			case Stuff_Diff:
                		if (src+1+count >= end) 
					return(NULL);
		                do 
		                {
					*dst++ = *++src ^ Stuff_Magic;
				}
				while(--count >= 0 && dst < dst_end);
				if (count < 0) 
					src += 1;
				else 
				{
					if (count == 0)
						*src = Stuff_Same ^ Stuff_Magic;
			                else
						*src = (Stuff_Diff + count) ^ Stuff_Magic;
		                }
                		break;
			case Stuff_DiffZero:
				if (src+1+count >= end) 
					return(NULL);
				do 
				{
					*dst++ = *++src ^ Stuff_Magic;
				}
				while(--count >= 0 && dst < dst_end);
				if (count < 0)
					*src = Stuff_Zero ^ Stuff_Magic;
        			else
					*src = (Stuff_DiffZero + count) ^ Stuff_Magic;
				break;
			case Stuff_Same:
				if (src+1 >= end)
     					return(NULL);
				do 
				{
					*dst++ = src[1] ^ Stuff_Magic;
				}
				while(--count >= 0 && dst < dst_end);
				if (count < 0)
					src += 2;
		                else
					*src = (Stuff_Same + count) ^ Stuff_Magic;
				break;
			case Stuff_Zero:
				do 
				{
					*dst++ = 0;
				}
				while(--count >= 0 && dst < dst_end);
				if (count < 0)
					src += 1;
				else
					*src = (Stuff_Zero + count) ^ Stuff_Magic;
		                break;
		}
	}
	if (dst < dst_end) 
		return(NULL);
	else
		return(src);
}

/************************************************************************/
/* General routines for STRIP						*/

/* MTU has been changed by the IP layer. Unfortunately we are not told
 * about this, but we spot it ourselves and fix things up. We could be in
 * an upcall from the tty driver, or in an ip packet queue.
 */

static void strip_changedmtu(struct strip *strip_info)
{
	struct device *dev = &strip_info->dev;
	unsigned char *tbuff, *rbuff, *sbuff, *otbuff, *orbuff, *osbuff;
	int len;
	InterruptStatus intstat;

	len = STRIP_ENCAP_SIZE(dev->mtu);
	if (len < STRIP_ENCAP_SIZE(576)) 
		len = STRIP_ENCAP_SIZE(576);

	tbuff = (unsigned char *) kmalloc (len + 4, GFP_ATOMIC);
	rbuff = (unsigned char *) kmalloc (len + 4, GFP_ATOMIC);
	sbuff = (unsigned char *) kmalloc (len + 4, GFP_ATOMIC);
	if (!tbuff || !rbuff || !sbuff) 
	{
        	printk("%s: unable to grow strip buffers, MTU change cancelled.\n",
        		strip_info->dev.name);
		dev->mtu = strip_info->mtu;
		if (tbuff)
			kfree(tbuff);
		if (rbuff)
			kfree(rbuff);
		if (sbuff)
			kfree(sbuff);
		return;
	}

	intstat = DisableInterrupts();
	otbuff = strip_info->tx_buff; strip_info->tx_buff = tbuff;
	orbuff = strip_info->rx_buff; strip_info->rx_buff = rbuff;
	osbuff = strip_info->sx_buff; strip_info->sx_buff = sbuff;
	if (strip_info->tx_left) 
	{
		if (strip_info->tx_left <= len)
			memcpy(strip_info->tx_buff, strip_info->tx_head, strip_info->tx_left);
		else 
		{
			strip_info->tx_left = 0;
			strip_info->tx_dropped++;
		}
	}
	strip_info->tx_head = strip_info->tx_buff;

	if (strip_info->sx_count) 
	{
		if (strip_info->sx_count <= len)
			memcpy(strip_info->sx_buff, osbuff, strip_info->sx_count);
		else
		{
			strip_info->sx_count = 0;
			strip_info->rx_over_errors++;
			set_bit(STR_ERROR, &strip_info->flags);
		}
	}

	strip_info->mtu      = STRIP_ENCAP_SIZE(dev->mtu);
	strip_info->buffsize = len;

	RestoreInterrupts(intstat);

	if (otbuff != NULL)
		 kfree(otbuff);
	if (orbuff != NULL)
		kfree(orbuff);
	if (osbuff != NULL)
		kfree(osbuff);
}

static void strip_unlock(struct strip *strip_info)
{
	strip_info->idle_timer.expires  = jiffies + 2 * HZ;
	add_timer(&strip_info->idle_timer);
	if (!clear_bit(0, (void *)&strip_info->dev.tbusy))
		printk("%s: trying to unlock already unlocked device!\n",
			strip_info->dev.name);
}

/************************************************************************/
/* Sending routines							*/

static void ResetRadio(struct strip *strip_info)
{	
	static const char InitString[] = "ate0dt**starmode\r**";
	strip_info->watchdog_doprobe = jiffies + 10 * HZ;
	strip_info->watchdog_doreset = jiffies + 1 * HZ;
	strip_info->tty->driver.write(strip_info->tty, 0,
		(char *)InitString, sizeof(InitString)-1);
}

/*
 * Called by the driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */

static void strip_write_some_more(struct tty_struct *tty)
{
	InterruptStatus intstat;
	int num_written;
	struct strip *strip_info = (struct strip *) tty->disc_data;

	/* First make sure we're connected. */
	if (!strip_info || strip_info->magic != STRIP_MAGIC || !strip_info->dev.start)
		return;

	if (strip_info->tx_left > 0) 
	{	/* If some data left, send it */
		/* Must disable interrupts because otherwise the write_wakeup might
		 * happen before we've had a chance to update the tx_left and
		 *  tx_head fields
 		 */
		intstat = DisableInterrupts();
		num_written = tty->driver.write(tty, 0, strip_info->tx_head, strip_info->tx_left);
		strip_info->tx_left -= num_written;
		strip_info->tx_head += num_written;
		RestoreInterrupts(intstat);
	}
	else			/* Else start transmission of another packet */
	{
		tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
		strip_unlock(strip_info);
		mark_bh(NET_BH);
	}
}


/* Encapsulate one IP datagram. */

static unsigned char *strip_stuff(unsigned char *ptr, struct strip *strip_info, struct sk_buff *skb)
{
    __u8         *start;
    __u8         *stuffstate = NULL;
    unsigned char  *icp        = skb->data;
    int             len        = skb->len;
    MetricomAddress haddr;

    if (len > strip_info->mtu) {		/* Sigh, shouldn't occur BUT ... */
        printk("%s: Dropping oversized transmit packet!\n", strip_info->dev.name);
        strip_info->tx_dropped++;
        return(NULL);
    }

    if (!arp_query(haddr.c, skb->raddr, &strip_info->dev)) {
        IPaddr a,b,c;
        a.l = skb->raddr;
        b.l = skb->saddr;
        c.l = skb->daddr;
        printk("%s: Unknown dest %d.%d.%d.%d s=%d.%d.%d.%d d=%d.%d.%d.%d\n",
            strip_info->dev.name,
            a.b[0], a.b[1], a.b[2], a.b[3],
            b.b[0], b.b[1], b.b[2], b.b[3],
            c.b[0], c.b[1], c.b[2], c.b[3]);
        strip_info->tx_dropped++;
        return(NULL);
    }

    *ptr++ = '*';
    ptr[3] = '0' + haddr.s[0] % 10; haddr.s[0] /= 10;
    ptr[2] = '0' + haddr.s[0] % 10; haddr.s[0] /= 10;
    ptr[1] = '0' + haddr.s[0] % 10; haddr.s[0] /= 10;
    ptr[0] = '0' + haddr.s[0] % 10;
    ptr+=4;
    *ptr++ = '-';
    ptr[3] = '0' + haddr.s[1] % 10; haddr.s[1] /= 10;
    ptr[2] = '0' + haddr.s[1] % 10; haddr.s[1] /= 10;
    ptr[1] = '0' + haddr.s[1] % 10; haddr.s[1] /= 10;
    ptr[0] = '0' + haddr.s[1] % 10;
    ptr+=4;
    *ptr++ = '*';
    *ptr++ = ProtocolKey.c[0];				/* Protocol key */
    *ptr++ = ProtocolKey.c[1];
    *ptr++ = ProtocolKey.c[2];
    *ptr++ = ProtocolKey.c[3];

    start = ptr;
    ptr = StuffData(icp, len, ptr, &stuffstate);	/* Make payload */

    *ptr++ = 0x0D;					/* Put on final delimiter */
    return(ptr);
}

/* Encapsulate one IP datagram and stuff into a TTY queue. */
static void strip_send(struct strip *strip_info, struct sk_buff *skb)
{
    unsigned char *ptr;

    /* See if someone has been ifconfigging */
    if (strip_info->mtu != STRIP_ENCAP_SIZE(strip_info->dev.mtu))
        strip_changedmtu(strip_info);

    ptr = strip_info->tx_buff;

    /* If we have a packet, encapsulate it and put it in the buffer */
    if (skb) {
        ptr = strip_stuff(ptr, strip_info, skb);
        /* If error, unlock and return */
        if (!ptr) { strip_unlock(strip_info); return; }
        strip_info->tx_packets++;	/* Count another successful packet */
    }

    /* Set up the strip_info ready to send the data */
    strip_info->tx_head =       strip_info->tx_buff;
    strip_info->tx_left = ptr - strip_info->tx_buff;
    strip_info->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);

    /* If watchdog has expired, reset the radio */
    if ((long)jiffies - strip_info->watchdog_doreset >= 0) {
        printk("%s: No response: Resetting radio.\n", strip_info->dev.name);
        ResetRadio(strip_info);
        /* Note: if there's a packet to send, strip_write_some_more
                 will do it after the reset has finished */
        return;
    }
    
    /* No reset.
     * If it is time for another tickle, tack it on the end of the packet
     */
    if ((long)jiffies - strip_info->watchdog_doprobe >= 0) {
        /* printk("%s: Routine radio test.\n", strip_info->dev.name); */
        *ptr++ = '*';			/* Tickle to make radio protest */
        *ptr++ = '*';
        strip_info->tx_left += 2;
        strip_info->watchdog_doprobe = jiffies + 10 * HZ;
        strip_info->watchdog_doreset = jiffies + 1 * HZ;
    }
    
    /* All ready. Start the transmission */
    strip_write_some_more(strip_info->tty);
}

/* Encapsulate an IP datagram and kick it into a TTY queue. */
static int strip_xmit(struct sk_buff *skb, struct device *dev)
{
    struct strip *strip_info = (struct strip *)(dev->priv);

    if (!dev->start) {
	printk("%s: xmit call when iface is down\n", dev->name);
	return(1);
    }
    if (set_bit(0, (void *) &strip_info->dev.tbusy)) return(1);
    del_timer(&strip_info->idle_timer);
    strip_send(strip_info, skb);
    if (skb) dev_kfree_skb(skb, FREE_WRITE);
    return(0);
}

/* IdleTask periodically calls strip_xmit, so even when we have no IP packets
   to send for an extended period of time, the watchdog processing still gets
   done to ensure that the radio stays in Starmode */

static void strip_IdleTask(unsigned long parameter)
{
	strip_xmit(NULL, (struct device *)parameter);
}

/************************************************************************/
/* Receiving routines							*/

static int strip_receive_room(struct tty_struct *tty)
{
	return 65536;  /* We can handle an infinite amount of data. :-) */
}

/* Send one completely decapsulated IP datagram to the IP layer. */

static void strip_bump(struct strip *strip_info, __u16 packetlen)
{
	int count = sizeof(STRIP_Header) + packetlen;
	struct sk_buff *skb = dev_alloc_skb(count);
	if (skb == NULL) 
	{
		printk("%s: memory squeeze, dropping packet.\n", 
			strip_info->dev.name);
		strip_info->rx_dropped++;
		return;
	}
	skb->dev = &strip_info->dev;
	memcpy(skb_put(skb, count), strip_info->rx_buff, count);
	skb->mac.raw=skb->data;
	skb->protocol = htons(ETH_P_IP);
	netif_rx(skb);
	strip_info->rx_packets++;
}

static void RecvErr(char *msg, struct strip *strip_info)
{
	static const int MAX_RecvErr = 80;
	__u8 *ptr = strip_info->sx_buff;
	__u8 *end = strip_info->sx_buff + strip_info->sx_count;
	__u8 pkt_text[MAX_RecvErr], *p = pkt_text;
	*p++ = '\"';
	while (ptr<end && p < &pkt_text[MAX_RecvErr-4]) 
	{
		if (*ptr == '\\') 
		{
			*p++ = '\\';
			*p++ = '\\';
		}
		else 
		{
			if (*ptr >= 32 && *ptr <= 126) 
				*p++ = *ptr;
			else
			{
				sprintf(p, "\\%02X", *ptr);
				p+= 3;
			}
		}
	        ptr++;
	}
	if (ptr == end)
	        *p++ = '\"';
	*p++ = 0;
	printk("%-13s%s\n", msg, pkt_text);
	set_bit(STR_ERROR, &strip_info->flags);
	strip_info->rx_errors++;
}

static void RecvErr_Message(struct strip *strip_info, __u8 *sendername, __u8 *msg)
{
	static const char ERR_001[] = "ERR_001 Not in StarMode!";
	static const char ERR_002[] = "ERR_002 Remap handle";
	static const char ERR_003[] = "ERR_003 Can't resolve name";
	static const char ERR_004[] = "ERR_004 Name too small or missing";
	static const char ERR_007[] = "ERR_007 Body too big";
	static const char ERR_008[] = "ERR_008 Bad character in name";

	if (!strncmp(msg, ERR_001, sizeof(ERR_001)-1))
		printk("Radio %s is not in StarMode\n", sendername);
	else if (!strncmp(msg, ERR_002, sizeof(ERR_002)-1)) 
	{
#ifdef notyet		/*Kernel doesn't have scanf!*/
		int handle;
		__u8 newname[64];
		sscanf(msg, "ERR_002 Remap handle &%d to name %s", &handle, newname);
		printk("Radio name %s is handle %d\n", newname, handle);
#endif
	}
	else if (!strncmp(msg, ERR_003, sizeof(ERR_003)-1)) 
		printk("Radio name <unspecified> is unknown (\"Can't resolve name\" error)\n");
	else if (!strncmp(msg, ERR_004, sizeof(ERR_004)-1)) 
        	strip_info->watchdog_doreset = jiffies + LONG_TIME;
	 else if (!strncmp(msg, ERR_007, sizeof(ERR_007)-1)) 
	 {
		/*
		 *	Note: This error knocks the radio back into 
		 *	command mode. 
		 */
		printk("Error! Packet size <unspecified> is too big for radio.");
		strip_info->watchdog_doreset = jiffies;		/* Do reset ASAP */
	}
	else if (!strncmp(msg, ERR_008, sizeof(ERR_008)-1)) 
		printk("Name <unspecified> contains illegal character\n");
	else 
		RecvErr("Error Msg:", strip_info);
}

static void process_packet(struct strip *strip_info)
{
    __u8 *ptr = strip_info->sx_buff;
    __u8 *end = strip_info->sx_buff + strip_info->sx_count;
    __u8 *name, *name_end;
    __u16 packetlen;

    /* Ignore empty lines */
    if (strip_info->sx_count == 0) return;

    /* Catch 'OK' responses which show radio has fallen out of starmode */
    if (strip_info->sx_count == 2 && ptr[0] == 'O' && ptr[1] == 'K') {
        printk("%s: Radio is back in AT command mode: Will Reset\n",
            strip_info->dev.name);
        strip_info->watchdog_doreset = jiffies;		/* Do reset ASAP */
        return;
    }

    /* Check for start of address marker, and then skip over it */
    if (*ptr != '*') {
        /* Catch other error messages */
        if (ptr[0] == 'E' && ptr[1] == 'R' && ptr[2] == 'R' && ptr[3] == '_')
            RecvErr_Message(strip_info, NULL, strip_info->sx_buff);
        else RecvErr("No initial *", strip_info);
        return;
    }
    ptr++;

    /* Skip the return address */
    name = ptr;
    while (ptr < end && *ptr != '*') ptr++;

    /* Check for end of address marker, and skip over it */
    if (ptr == end) {
        RecvErr("No second *", strip_info);
        return;
    }
    name_end = ptr++;

    /* Check for STRIP key, and skip over it */
    if (ptr[0] != ProtocolKey.c[0] ||
        ptr[1] != ProtocolKey.c[1] ||
        ptr[2] != ProtocolKey.c[2] ||
        ptr[3] != ProtocolKey.c[3]) {
        if (ptr[0] == 'E' && ptr[1] == 'R' && ptr[2] == 'R' && ptr[3] == '_') { *name_end = 0; RecvErr_Message(strip_info, name, ptr); }
        else RecvErr("Unrecognized protocol key", strip_info);
        return;
    }
    ptr += 4;

    /* Decode start of the IP packet header */
    ptr = UnStuffData(ptr, end, strip_info->rx_buff, 4);
    if (!ptr) {
        RecvErr("Runt packet", strip_info);
        return;
    }

    packetlen = ((__u16)strip_info->rx_buff[2] << 8) | strip_info->rx_buff[3];
/*	printk("Packet %02X.%02X.%02X.%02X\n",
        strip_info->rx_buff[0], strip_info->rx_buff[1],
        strip_info->rx_buff[2], strip_info->rx_buff[3]);
    printk("Got %d byte packet\n", packetlen);*/

    /* Decode remainder of the IP packer */
    ptr = UnStuffData(ptr, end, strip_info->rx_buff+4, packetlen-4);
    if (!ptr) {
        RecvErr("Runt packet", strip_info);
        return;
    }
    strip_bump(strip_info, packetlen);

    /* This turns out to be a mistake. Taking receipt of a valid packet as
     * evidence that the radio is correctly in Starmode (and resetting the
     * watchdog_doreset timer) is wrong.  It turns out that if the radio is
     * in command mode, with character echo on, then the echo of the packet
     * you sent coming back looks like a valid packet and fools this test.
     * We should only accept the "ERR_004 Name too small or missing" message
     * as evidence that the radio is correctly in Starmode.
    strip_info->watchdog_doprobe = jiffies + 10 * HZ;
    strip_info->watchdog_doreset = jiffies + LONG_TIME;
     */
}

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of STRIP data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing.
 */
static void
strip_receive_buf(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
/*	struct timeval tv;*/
    struct strip *strip_info = (struct strip *) tty->disc_data;
    const unsigned char *end = cp + count;

    if (!strip_info || strip_info->magic != STRIP_MAGIC || !strip_info->dev.start)
        return;

    /* Argh! mtu change time! - costs us the packet part received at the change */
    if (strip_info->mtu != STRIP_ENCAP_SIZE(strip_info->dev.mtu))
        strip_changedmtu(strip_info);

/*	do_gettimeofday(&tv);
    printk("**** strip_receive_buf: %3d bytes at %d.%06d\n",
        count, tv.tv_sec % 100, tv.tv_usec);*/
    /* Read the characters out of the buffer */
    while (cp < end) {
        if (fp && *fp++) {
            if (!set_bit(STR_ERROR, &strip_info->flags)) strip_info->rx_errors++;
        }
        else if (*cp == 0x0D) {
            /*printk("Cut a %d byte packet (%d bytes remaining)\n",
                strip_info->sx_count, end-cp-1);*/
            if (!clear_bit(STR_ERROR, &strip_info->flags))
                process_packet(strip_info);
            strip_info->sx_count = 0;
        }
        else if (!test_bit(STR_ERROR, &strip_info->flags) &&
            (strip_info->sx_count > 0 || *cp != 0x0A))
        {
            /* (leading newline characters are ignored) */
            if (strip_info->sx_count < strip_info->buffsize)
                strip_info->sx_buff[strip_info->sx_count++] = *cp;
            else
            {
                set_bit(STR_ERROR, &strip_info->flags);
                strip_info->rx_over_errors++;
            }
        }
        cp++;
    }
}

/************************************************************************/
/* General control routines						*/

/*
 *	 Create the Ethernet MAC header for an arbitrary protocol layer 
 *
 *	saddr=NULL	means use device source address
 *	daddr=NULL	means leave destination address (eg unresolved arp)
 */

static int strip_header(struct sk_buff *skb, struct device *dev, 
	unsigned short type, void *daddr, void *saddr, unsigned len)
{
	return(-dev->hard_header_len);
}

/*
 *	Rebuild the Ethernet MAC header. This is called after an ARP
 *	(or in future other address resolution) has completed on this
 *	sk_buff. We now let ARP fill in the other fields.
 */

/* I think this should return zero if packet is ready to send, */
/* or non-zero if it needs more time to do an address lookup   */

static int strip_rebuild_header(void *buff, struct device *dev, 
	unsigned long dst, struct sk_buff *skb)
{
/*	STRIP_Header *h = (STRIP_Header *)buff;*/

#ifdef CONFIG_INET
	/* I'll use arp_find when I understand it */
	/* Arp find returns zero if if knows the address, or if it doesn't */
	/* know the address it sends an ARP packet and returns non-zero */
	/*return arp_find(eth->h_dest, dst, dev, dev->pa_addr, skb)? 1 : 0;*/
	return(0);
#else
	return(0);
#endif	
}

static int strip_set_dev_mac_address(struct device *dev, void *addr)
{
	memcpy(dev->dev_addr, addr, 7);
	return 0;
}

static struct enet_statistics *strip_get_stats(struct device *dev)
{
	static struct enet_statistics stats;
	struct strip *strip_info = (struct strip *)(dev->priv);

	memset(&stats, 0, sizeof(struct enet_statistics));

	stats.rx_packets     = strip_info->rx_packets;
	stats.tx_packets     = strip_info->tx_packets;
	stats.rx_dropped     = strip_info->rx_dropped;
	stats.tx_dropped     = strip_info->tx_dropped;
	stats.tx_errors      = strip_info->tx_errors;
	stats.rx_errors      = strip_info->rx_errors;
	stats.rx_over_errors = strip_info->rx_over_errors;
	return(&stats);
}

/************************************************************************/
/* Opening and closing							*/

/*
 * Here's the order things happen:
 * When the user runs "slattach -p strip ..."
 *  1. The TTY module calls strip_open
 *  2. strip_open calls strip_alloc
 *  3.                  strip_alloc calls register_netdev
 *  4.                  register_netdev calls strip_dev_init
 *  5. then strip_open finishes setting up the strip_info
 *
 * When the user runs "ifconfig st<x> up address netmask ..."
 *  6. strip_open_low gets called
 *
 * When the user runs "ifconfig st<x> down"
 *  7. strip_close_low gets called
 *
 * When the user kills the slattach process
 *  8. strip_close gets called
 *  9. strip_close calls dev_close
 * 10. if the device is still up, then dev_close calls strip_close_low
 * 11. strip_close calls strip_free
 */

/* Open the low-level part of the STRIP channel. Easy! */

static int strip_open_low(struct device *dev)
{
	struct strip *strip_info = (struct strip *)(dev->priv);
	unsigned long len;

	if (strip_info->tty == NULL) 
		return(-ENODEV);

	/*
	 * Allocate the STRIP frame buffers:
	 *
	 * rbuff	Receive buffer.
	 * tbuff	Transmit buffer.
	 * cbuff        Temporary compression buffer.
	 */

	len = STRIP_ENCAP_SIZE(dev->mtu);
	if (len < STRIP_ENCAP_SIZE(576)) 
		len = STRIP_ENCAP_SIZE(576);
	strip_info->rx_buff = (unsigned char *) kmalloc(len + 4, GFP_KERNEL);
	if (strip_info->rx_buff == NULL) 
		goto norbuff;
	strip_info->sx_buff = (unsigned char *) kmalloc(len + 4, GFP_KERNEL);
	if (strip_info->sx_buff == NULL) 
		goto nosbuff;
	strip_info->tx_buff = (unsigned char *) kmalloc(len + 4, GFP_KERNEL);
	if (strip_info->tx_buff == NULL) 
		goto notbuff;

	strip_info->flags   &= (1 << STR_INUSE); /* Clear ESCAPE & ERROR flags */
	strip_info->mtu	 = STRIP_ENCAP_SIZE(dev->mtu);
	strip_info->buffsize = len;
	strip_info->sx_count = 0;
	strip_info->tx_left  = 0;

	/*
	 *	Needed because address '0' is special 
	 */
	 
	if (dev->pa_addr == 0) 
		dev->pa_addr=ntohl(0xC0A80001);
	dev->tbusy  = 0;
	dev->start  = 1;

	printk("%s: Initializing Radio.\n", strip_info->dev.name);
	ResetRadio(strip_info);
	strip_info->idle_timer.expires  = jiffies + 2 * HZ;
	add_timer(&strip_info->idle_timer);
	return(0);

notbuff:
	kfree(strip_info->sx_buff);
nosbuff:
	kfree(strip_info->rx_buff);
norbuff:
	return(-ENOMEM);
}


/*
 *	Close the low-level part of the STRIP channel. Easy! 
 */
 
static int strip_close_low(struct device *dev)
{
	struct strip *strip_info = (struct strip *)(dev->priv);

	if (strip_info->tty == NULL) 
		return -EBUSY;
	strip_info->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
	dev->tbusy = 1;
	dev->start = 0;
    
	/*
	 *	Free all STRIP frame buffers.
	 */
	if (strip_info->rx_buff) 
	{
		kfree(strip_info->rx_buff);
		strip_info->rx_buff = NULL;
	}
	if (strip_info->sx_buff) 
	{
		kfree(strip_info->sx_buff); 
		strip_info->sx_buff = NULL;
	}
	if (strip_info->tx_buff) 
	{
		kfree(strip_info->tx_buff); 
		strip_info->tx_buff = NULL; 
	}
	del_timer(&strip_info->idle_timer);
	return 0;
}

/* 
 *	This routine is called by DDI when the
 *	(dynamically assigned) device is registered
 */
 
static int strip_dev_init(struct device *dev)
{
	int i;
	
	/*
	 *	Finish setting up the DEVICE info. 
	 */

	dev->trans_start        = 0;
	dev->last_rx            = 0;
	dev->tx_queue_len       = 30; 	/* Drop after 30 frames queued */

	dev->flags              = 0;
	dev->family             = AF_INET;
	dev->metric             = 0;
	dev->mtu                = STRIP_MTU;
	dev->type               = ARPHRD_METRICOM;        /* dtang */
	dev->hard_header_len    = 8; /*sizeof(STRIP_Header);*/
	/*
	 *  dev->priv                 Already holds a pointer to our struct strip 
	 */

	dev->broadcast[0]       = 0;
	dev->dev_addr[0]        = 0;
	dev->addr_len           = sizeof(MetricomAddress);
	dev->pa_addr            = 0;
	dev->pa_brdaddr         = 0;
	dev->pa_mask            = 0;
	dev->pa_alen            = sizeof(unsigned long);

	/*
	 *	Pointer to the interface buffers. 
	 */
	 
	for (i = 0; i < DEV_NUMBUFFS; i++) 
		skb_queue_head_init(&dev->buffs[i]);

	/*
	 *	Pointers to interface service routines. 
	 */

	dev->open               = strip_open_low;
	dev->stop               = strip_close_low;
	dev->hard_start_xmit    = strip_xmit;
	dev->hard_header        = strip_header;
	dev->rebuild_header     = strip_rebuild_header;
	/*  dev->type_trans            unused */
	/*  dev->set_multicast_list   unused */
	dev->set_mac_address    = strip_set_dev_mac_address;
	/*  dev->do_ioctl             unused */
	/*  dev->set_config           unused */
	dev->get_stats          = strip_get_stats;
	return 0;
}

/*
 *	Free a STRIP channel. 
 */
 
static void strip_free(struct strip *strip_info)
{
	*(strip_info->referrer) = strip_info->next;
	if (strip_info->next) 
		strip_info->next->referrer = strip_info->referrer;
	strip_info->magic = 0;
	kfree(strip_info);
}

/* 
 *	Allocate a new free STRIP channel 
 */
 
static struct strip *strip_alloc(void)
{
	int channel_id = 0;
	struct strip **s = &struct_strip_list;
	struct strip *strip_info = (struct strip *)
		kmalloc(sizeof(struct strip), GFP_KERNEL);

	if (!strip_info) 
		return(NULL);	/* If no more memory, return */

	/*
	 *	Clear the allocated memory 
	 */
	 
	memset(strip_info, 0, sizeof(struct strip));

	/*
	 *	Search the list to find where to put our new entry
	 *	(and in the process decide what channel number it is
	 *	going to be) 
	 */
	 
	while (*s && (*s)->dev.base_addr == channel_id) 
	{
		channel_id++;
		s = &(*s)->next;
	}

	/*
	 *	Fill in the link pointers 
	 */
	 
	strip_info->next = *s;
	if (*s) 
		(*s)->referrer = &strip_info->next;
	strip_info->referrer = s;
	*s = strip_info;

	set_bit(STR_INUSE, &strip_info->flags);
	strip_info->magic = STRIP_MAGIC;
	strip_info->tty   = NULL;

	init_timer(&strip_info->idle_timer);
	strip_info->idle_timer.data     = (long)&strip_info->dev;
	strip_info->idle_timer.function = strip_IdleTask;

	sprintf(strip_info->if_name, "st%d", channel_id);
	strip_info->dev.name         = strip_info->if_name;
	strip_info->dev.base_addr    = channel_id;
	strip_info->dev.priv         = (void*)strip_info;
	strip_info->dev.next         = NULL;
	strip_info->dev.init         = strip_dev_init;

	return(strip_info);
}

/*
 *	Open the high-level part of the STRIP channel.
 *	This function is called by the TTY module when the
 *	STRIP line discipline is called for.  Because we are
 *	sure the tty line exists, we only have to link it to
 *	a free STRIP channel...
 */

static int strip_open(struct tty_struct *tty)
{
	struct strip *strip_info = (struct strip *) tty->disc_data;

	/*
	 *	First make sure we're not already connected.
	 */
	
	if (strip_info && strip_info->magic == STRIP_MAGIC) 
		return -EEXIST;

	/*
	 *	OK.  Find a free STRIP channel to use. 
	 */
	 
	if ((strip_info = strip_alloc()) == NULL) 
		return -ENFILE;

	/*
	 *	Register our newly created device so it can be ifconfig'd
	 * strip_dev_init() will be called as a side-effect
	 */
     
	if (register_netdev(&strip_info->dev) != 0) 
	{
		printk("strip: register_netdev() failed.\n");
		strip_free(strip_info);
		return -ENFILE;
	}

	strip_info->tty = tty;
	tty->disc_data = strip_info;
	if (tty->driver.flush_buffer) 
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer) 
		tty->ldisc.flush_buffer(tty);

	/*
	 *	Restore default settings 
	 */
	 
	strip_info->dev.type = ARPHRD_METRICOM;	/* dtang */

	/*
	 *	Set tty options 
	 */

	tty->termios->c_iflag |= IGNBRK |IGNPAR;/* Ignore breaks and parity errors. */
	tty->termios->c_cflag |= CLOCAL;	/* Ignore modem control signals. */
	tty->termios->c_cflag &= ~HUPCL;	/* Don't close on hup */

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
	/*
	 *	Done.  We have linked the TTY line to a channel. 
	 */
	return(strip_info->dev.base_addr);
}

/*
 * Close down a STRIP channel.
 * This means flushing out any pending queues, and then restoring the
 * TTY line discipline to what it was before it got hooked to STRIP
 * (which usually is TTY again).
 */
static void strip_close(struct tty_struct *tty)
{
	struct strip *strip_info = (struct strip *) tty->disc_data;

	/*
	 *	First make sure we're connected. 
	 */
	 
	if (!strip_info || strip_info->magic != STRIP_MAGIC) 
		return;

	dev_close(&strip_info->dev);
	unregister_netdev(&strip_info->dev);
    
	tty->disc_data = 0;
	strip_info->tty = NULL;
	strip_free(strip_info);
	tty->disc_data = NULL;
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}


/************************************************************************/
/* Perform I/O control calls on an active STRIP channel.       		*/

static int strip_ioctl(struct tty_struct *tty, struct file *file, 
	unsigned int cmd, unsigned long arg)
{
	struct strip *strip_info = (struct strip *) tty->disc_data;
	int err;

	/*
	 *	First make sure we're connected. 
	 */
	 
	if (!strip_info || strip_info->magic != STRIP_MAGIC) 
		return -EINVAL;

	switch(cmd) 
	{
		case SIOCGIFNAME:
			err = verify_area(VERIFY_WRITE, (void*)arg, 16);
			if (err)
				return -err;
			memcpy_tofs((void*)arg, strip_info->dev.name, 
				strlen(strip_info->dev.name) + 1);
			return 0;

		case SIOCSIFHWADDR:
        		return -EINVAL;

		/*
		 *	Allow stty to read, but not set, the serial port 
		 */
	 
		case TCGETS:
		case TCGETA:
			return n_tty_ioctl(tty, (struct file *) file, cmd, 
				(unsigned long) arg);

		default:
			return -ENOIOCTLCMD;
	}
}

/************************************************************************/
/* Initialization							*/

/*
 *	Initialize the STRIP driver.
 *	This routine is called at boot time, to bootstrap the multi-channel
 *	STRIP driver
 */

#ifdef MODULE
static
#endif
int strip_init_ctrl_dev(struct device *dummy)
{
	static struct tty_ldisc strip_ldisc;
	int status;
	printk("STRIP: version %s (unlimited channels)\n", STRIP_VERSION);

	/*
	 *	Fill in our line protocol discipline, and register it
	 */
	 
	memset(&strip_ldisc, 0, sizeof(strip_ldisc));
	strip_ldisc.magic	= TTY_LDISC_MAGIC;
	strip_ldisc.flags	= 0;
	strip_ldisc.open	= strip_open;
	strip_ldisc.close	= strip_close;
	strip_ldisc.read	= NULL;
	strip_ldisc.write	= NULL;
	strip_ldisc.ioctl	= strip_ioctl;
	strip_ldisc.select       = NULL;
	strip_ldisc.receive_buf  = strip_receive_buf;
	strip_ldisc.receive_room = strip_receive_room;
	strip_ldisc.write_wakeup = strip_write_some_more;
	status = tty_register_ldisc(N_STRIP, &strip_ldisc);
	if (status != 0) 
	{
		printk("STRIP: can't register line discipline (err = %d)\n", status);
	}

#ifdef MODULE
	 return status;
#else
	/* Return "not found", so that dev_init() will unlink
	 * the placeholder device entry for us.
	 */
	return ENODEV;
#endif
}

/************************************************************************/
/* From here down is only used when compiled as an external module        */

#ifdef MODULE

int init_module(void)
{
    return strip_init_ctrl_dev(0);
}

void cleanup_module(void)
{
	int i;
	while (struct_strip_list) 
		strip_free(struct_strip_list);

	if ((i = tty_register_ldisc(N_STRIP, NULL)))  
		printk("STRIP: can't unregister line discipline (err = %d)\n", i);
}
#endif /* MODULE */

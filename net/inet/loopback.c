/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Pseudo-driver for the loopback interface.
 *
 * Version:	@(#)loopback.c	1.0.4	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include "inet.h"
#include "dev.h"
#include "eth.h"
#include "timer.h"
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "arp.h"


static int
loopback_xmit(struct sk_buff *skb, struct device *dev)
{
  int done;

  DPRINTF((DBG_LOOPB, "loopback_xmit(dev=%X, skb=%X)\n", dev, skb));
  if (skb == NULL || dev == NULL) return(0);

  cli();
  if (dev->tbusy != 0) {
	sti();
	return(1);
  }
  dev->tbusy = 1;
  sti();

  done = dev_rint((unsigned char *)(skb+1), skb->len, 0, dev);
  if (skb->free) kfree_skb(skb, FREE_WRITE);

  while (done != 1) {
	done = dev_rint(NULL, 0, 0, dev);
  }
  dev->tbusy = 0;

  return(0);
}


/* Initialize the rest of the LOOPBACK device. */
int
loopback_init(struct device *dev)
{
  dev->mtu		= 2000;			/* MTU			*/
  dev->tbusy		= 0;
  dev->hard_start_xmit	= loopback_xmit;
  dev->open		= NULL;
#if 1
  dev->hard_header	= eth_header;
  dev->add_arp		= NULL;
  dev->hard_header_len	= ETH_HLEN;		/* 14			*/
  dev->addr_len		= ETH_ALEN;		/* 6			*/
  dev->type		= ARPHRD_ETHER;		/* 0x0001		*/
  dev->type_trans	= eth_type_trans;
  dev->rebuild_header	= eth_rebuild_header;
#else
  dev->hard_header_length = 0;
  dev->add_arp		= NULL;
  dev->addr_len		= 0;
  dev->type		= 0;			/* loopback_type (0)	*/
  dev->hard_header	= NULL;
  dev->type_trans	= NULL;
  dev->rebuild_header	= NULL;
#endif
  dev->queue_xmit	= dev_queue_xmit;

  /* New-style flags. */
  dev->flags		= IFF_LOOPBACK;
  dev->family		= AF_INET;
  dev->pa_addr		= in_aton("127.0.0.1");
  dev->pa_brdaddr	= in_aton("127.255.255.255");
  dev->pa_mask		= in_aton("255.0.0.0");
  dev->pa_alen		= sizeof(unsigned long);

  return(0);
};

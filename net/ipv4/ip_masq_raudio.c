/*
 *		IP_MASQ_RAUDIO  - Real Audio masquerading module
 *
 *
 * Version:	@(#)$Id: ip_masq_raudio.c,v 1.3 1996/05/20 13:24:26 nigel Exp $
 *
 * Author:	Nigel Metheringham
 *		[strongly based on ftp module by Juan Jose Ciarlante & Wouter Gadeyne]
 *		[Real Audio information taken from Progressive Networks firewall docs]
 *
 *
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *
 * Limitations
 *	The IP Masquerading proxies at present do not have access to a processed
 *	data stream.  Hence for a protocol like the Real Audio control protocol,
 *	which depends on knowing where you are in the data stream, you either
 *	to keep a *lot* of state in your proxy, or you cheat and simplify the
 *	problem [needless to say I did the latter].
 *
 *	This proxy only handles data in the first packet.  Everything else is
 *	passed transparently.  This means it should work under all normal
 *	circumstances, but it could be fooled by new data formats or a
 *	malicious application!
 *
 *	At present the "first packet" is defined as a packet starting with
 *	the protocol ID string - "PNA".
 *	When the link is up there appears to be enough control data 
 *	crossing the control link to keep it open even if a long audio
 *	piece is playing.
 *	
 */

#include <linux/module.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/ip_masq.h>

#ifndef DEBUG_CONFIG_IP_MASQ_RAUDIO
#define DEBUG_CONFIG_IP_MASQ_RAUDIO 0
#endif

struct raudio_priv_data {
	/* Associated data connection - setup but not used at present */
	struct	ip_masq *data_conn;
	/* Have we seen and performed setup */
	short	seen_start;
};

static int
masq_raudio_init_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_INC_USE_COUNT;
	if ((ms->app_data = kmalloc(sizeof(struct raudio_priv_data),
				    GFP_ATOMIC)) == NULL) 
		printk(KERN_INFO "RealAudio: No memory for application data\n");
	else 
	{
		struct raudio_priv_data *priv = 
			(struct raudio_priv_data *)ms->app_data;
		priv->seen_start = 0;
		priv->data_conn = NULL;
	}
        return 0;
}

static int
masq_raudio_done_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_DEC_USE_COUNT;
	if (ms->app_data)
		kfree_s(ms->app_data, sizeof(struct raudio_priv_data));
        return 0;
}

int
masq_raudio_out (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff *skb;
	struct iphdr *iph;
	struct tcphdr *th;
	char *p, *data, *data_limit;
	struct ip_masq *n_ms;
	unsigned short version, msg_id, msg_len, udp_port;
	struct raudio_priv_data *priv = 
		(struct raudio_priv_data *)ms->app_data;

	/* Everything running correctly already */
	if (priv && priv->seen_start)
		return 0;

        skb = *skb_p;
	iph = skb->h.iph;
        th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
        data = (char *)&th[1];

        data_limit = skb->h.raw + skb->len - 18;

	/* Check to see if this is the first packet with protocol ID */
	if (memcmp(data, "PNA", 3)) {
#if DEBUG_CONFIG_IP_MASQ_RAUDIO
		printk("RealAudio: not initial protocol packet - ignored\n");
#endif
		return(0);
	}
	data += 3;
	memcpy(&version, data, 2);

#if DEBUG_CONFIG_IP_MASQ_RAUDIO
	printk("RealAudio: initial seen - protocol version %d\n",
	       ntohs(version));
#endif
	if (priv)
		priv->seen_start = 1;

	if (ntohs(version) >= 256)
	{
		printk(KERN_INFO "RealAudio: version (%d) not supported\n",
		       ntohs(version));
		return 0;
	}

	data += 2;
	while (data < data_limit) {
		memcpy(&msg_id, data, 2);
		data += 2;
		memcpy(&msg_len, data, 2);
		data += 2;
#if DEBUG_CONFIG_IP_MASQ_RAUDIO
		printk("RealAudio: msg %d - %d byte\n",
		       ntohs(msg_id), ntohs(msg_len));
#endif
		p = data;
		data += ntohs(msg_len);
		if (data > data_limit)
		{
			printk(KERN_INFO "RealAudio: Packet too short for data\n");
			return 0;
		}
		if (ntohs(msg_id) == 1) {
			/* This is a message detailing the UDP port to be used */
			memcpy(&udp_port, p, 2);
			n_ms = ip_masq_new(dev, IPPROTO_UDP,
					   ms->saddr, udp_port,
					   ms->daddr, 0,
					   IP_MASQ_F_NO_DPORT);
					
			if (n_ms==NULL)
				return 0;

			memcpy(p, &(n_ms->mport), 2);
#if DEBUG_CONFIG_IP_MASQ_RAUDIO
			printk("RealAudio: rewrote UDP port %d -> %d\n",
			       ntohs(udp_port), ntohs(n_ms->mport));
#endif
			ip_masq_set_expire(n_ms, ip_masq_expire->udp_timeout);

			/* Make ref in application data to data connection */
			if (priv)
				priv->data_conn = n_ms;

			/* 
			 * There is nothing else useful we can do
			 * Maybe a development could do more, but for now
			 * we exit gracefully!
			 */
			return 0;

		} else if (ntohs(msg_id) == 0)
			return 0;
	}
	return 0;
}

struct ip_masq_app ip_masq_raudio = {
        NULL,			/* next */
	"RealAudio",	       	/* name */
        0,                      /* type */
        0,                      /* n_attach */
        masq_raudio_init_1,     /* ip_masq_init_1 */
        masq_raudio_done_1,     /* ip_masq_done_1 */
        masq_raudio_out,        /* pkt_out */
        NULL                    /* pkt_in */
};

/*
 * 	ip_masq_raudio initialization
 */

int ip_masq_raudio_init(void)
{
        return register_ip_masq_app(&ip_masq_raudio, IPPROTO_TCP, 7070);
}

/*
 * 	ip_masq_raudio fin.
 */

int ip_masq_raudio_done(void)
{
        return unregister_ip_masq_app(&ip_masq_raudio);
}

#ifdef MODULE

int init_module(void)
{
        if (ip_masq_raudio_init() != 0)
                return -EIO;
        register_symtab(0);
        return 0;
}

void cleanup_module(void)
{
        if (ip_masq_raudio_done() != 0)
                printk("ip_masq_raudio: can't remove module");
}

#endif /* MODULE */

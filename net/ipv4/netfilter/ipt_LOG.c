/*
 * This is a module which is used for logging packets.
 */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/route.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_LOG.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("iptables syslog logging module");

static unsigned int nflog = 1;
module_param(nflog, int, 0400);
MODULE_PARM_DESC(nflog, "register as internal netfilter logging module");
 
#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Use lock to serialize, so printks don't overlap */
static DEFINE_SPINLOCK(log_lock);

/* One level of recursion won't kill us */
static void dump_packet(const struct ipt_log_info *info,
			const struct sk_buff *skb,
			unsigned int iphoff)
{
	struct iphdr _iph, *ih;

	ih = skb_header_pointer(skb, iphoff, sizeof(_iph), &_iph);
	if (ih == NULL) {
		printk("TRUNCATED");
		return;
	}

	/* Important fields:
	 * TOS, len, DF/MF, fragment offset, TTL, src, dst, options. */
	/* Max length: 40 "SRC=255.255.255.255 DST=255.255.255.255 " */
	printk("SRC=%u.%u.%u.%u DST=%u.%u.%u.%u ",
	       NIPQUAD(ih->saddr), NIPQUAD(ih->daddr));

	/* Max length: 46 "LEN=65535 TOS=0xFF PREC=0xFF TTL=255 ID=65535 " */
	printk("LEN=%u TOS=0x%02X PREC=0x%02X TTL=%u ID=%u ",
	       ntohs(ih->tot_len), ih->tos & IPTOS_TOS_MASK,
	       ih->tos & IPTOS_PREC_MASK, ih->ttl, ntohs(ih->id));

	/* Max length: 6 "CE DF MF " */
	if (ntohs(ih->frag_off) & IP_CE)
		printk("CE ");
	if (ntohs(ih->frag_off) & IP_DF)
		printk("DF ");
	if (ntohs(ih->frag_off) & IP_MF)
		printk("MF ");

	/* Max length: 11 "FRAG:65535 " */
	if (ntohs(ih->frag_off) & IP_OFFSET)
		printk("FRAG:%u ", ntohs(ih->frag_off) & IP_OFFSET);

	if ((info->logflags & IPT_LOG_IPOPT)
	    && ih->ihl * 4 > sizeof(struct iphdr)) {
		unsigned char _opt[4 * 15 - sizeof(struct iphdr)], *op;
		unsigned int i, optsize;

		optsize = ih->ihl * 4 - sizeof(struct iphdr);
		op = skb_header_pointer(skb, iphoff+sizeof(_iph),
					optsize, _opt);
		if (op == NULL) {
			printk("TRUNCATED");
			return;
		}

		/* Max length: 127 "OPT (" 15*4*2chars ") " */
		printk("OPT (");
		for (i = 0; i < optsize; i++)
			printk("%02X", op[i]);
		printk(") ");
	}

	switch (ih->protocol) {
	case IPPROTO_TCP: {
		struct tcphdr _tcph, *th;

		/* Max length: 10 "PROTO=TCP " */
		printk("PROTO=TCP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		th = skb_header_pointer(skb, iphoff + ih->ihl * 4,
					sizeof(_tcph), &_tcph);
		if (th == NULL) {
			printk("INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Max length: 20 "SPT=65535 DPT=65535 " */
		printk("SPT=%u DPT=%u ",
		       ntohs(th->source), ntohs(th->dest));
		/* Max length: 30 "SEQ=4294967295 ACK=4294967295 " */
		if (info->logflags & IPT_LOG_TCPSEQ)
			printk("SEQ=%u ACK=%u ",
			       ntohl(th->seq), ntohl(th->ack_seq));
		/* Max length: 13 "WINDOW=65535 " */
		printk("WINDOW=%u ", ntohs(th->window));
		/* Max length: 9 "RES=0x3F " */
		printk("RES=0x%02x ", (u8)(ntohl(tcp_flag_word(th) & TCP_RESERVED_BITS) >> 22));
		/* Max length: 32 "CWR ECE URG ACK PSH RST SYN FIN " */
		if (th->cwr)
			printk("CWR ");
		if (th->ece)
			printk("ECE ");
		if (th->urg)
			printk("URG ");
		if (th->ack)
			printk("ACK ");
		if (th->psh)
			printk("PSH ");
		if (th->rst)
			printk("RST ");
		if (th->syn)
			printk("SYN ");
		if (th->fin)
			printk("FIN ");
		/* Max length: 11 "URGP=65535 " */
		printk("URGP=%u ", ntohs(th->urg_ptr));

		if ((info->logflags & IPT_LOG_TCPOPT)
		    && th->doff * 4 > sizeof(struct tcphdr)) {
			unsigned char _opt[4 * 15 - sizeof(struct tcphdr)];
			unsigned char *op;
			unsigned int i, optsize;

			optsize = th->doff * 4 - sizeof(struct tcphdr);
			op = skb_header_pointer(skb,
						iphoff+ih->ihl*4+sizeof(_tcph),
						optsize, _opt);
			if (op == NULL) {
				printk("TRUNCATED");
				return;
			}

			/* Max length: 127 "OPT (" 15*4*2chars ") " */
			printk("OPT (");
			for (i = 0; i < optsize; i++)
				printk("%02X", op[i]);
			printk(") ");
		}
		break;
	}
	case IPPROTO_UDP: {
		struct udphdr _udph, *uh;

		/* Max length: 10 "PROTO=UDP " */
		printk("PROTO=UDP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		uh = skb_header_pointer(skb, iphoff+ih->ihl*4,
					sizeof(_udph), &_udph);
		if (uh == NULL) {
			printk("INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Max length: 20 "SPT=65535 DPT=65535 " */
		printk("SPT=%u DPT=%u LEN=%u ",
		       ntohs(uh->source), ntohs(uh->dest),
		       ntohs(uh->len));
		break;
	}
	case IPPROTO_ICMP: {
		struct icmphdr _icmph, *ich;
		static size_t required_len[NR_ICMP_TYPES+1]
			= { [ICMP_ECHOREPLY] = 4,
			    [ICMP_DEST_UNREACH]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_SOURCE_QUENCH]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_REDIRECT]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_ECHO] = 4,
			    [ICMP_TIME_EXCEEDED]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_PARAMETERPROB]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_TIMESTAMP] = 20,
			    [ICMP_TIMESTAMPREPLY] = 20,
			    [ICMP_ADDRESS] = 12,
			    [ICMP_ADDRESSREPLY] = 12 };

		/* Max length: 11 "PROTO=ICMP " */
		printk("PROTO=ICMP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ich = skb_header_pointer(skb, iphoff + ih->ihl * 4,
					 sizeof(_icmph), &_icmph);
		if (ich == NULL) {
			printk("INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Max length: 18 "TYPE=255 CODE=255 " */
		printk("TYPE=%u CODE=%u ", ich->type, ich->code);

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		if (ich->type <= NR_ICMP_TYPES
		    && required_len[ich->type]
		    && skb->len-iphoff-ih->ihl*4 < required_len[ich->type]) {
			printk("INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		switch (ich->type) {
		case ICMP_ECHOREPLY:
		case ICMP_ECHO:
			/* Max length: 19 "ID=65535 SEQ=65535 " */
			printk("ID=%u SEQ=%u ",
			       ntohs(ich->un.echo.id),
			       ntohs(ich->un.echo.sequence));
			break;

		case ICMP_PARAMETERPROB:
			/* Max length: 14 "PARAMETER=255 " */
			printk("PARAMETER=%u ",
			       ntohl(ich->un.gateway) >> 24);
			break;
		case ICMP_REDIRECT:
			/* Max length: 24 "GATEWAY=255.255.255.255 " */
			printk("GATEWAY=%u.%u.%u.%u ",
			       NIPQUAD(ich->un.gateway));
			/* Fall through */
		case ICMP_DEST_UNREACH:
		case ICMP_SOURCE_QUENCH:
		case ICMP_TIME_EXCEEDED:
			/* Max length: 3+maxlen */
			if (!iphoff) { /* Only recurse once. */
				printk("[");
				dump_packet(info, skb,
					    iphoff + ih->ihl*4+sizeof(_icmph));
				printk("] ");
			}

			/* Max length: 10 "MTU=65535 " */
			if (ich->type == ICMP_DEST_UNREACH
			    && ich->code == ICMP_FRAG_NEEDED)
				printk("MTU=%u ", ntohs(ich->un.frag.mtu));
		}
		break;
	}
	/* Max Length */
	case IPPROTO_AH: {
		struct ip_auth_hdr _ahdr, *ah;

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;
		
		/* Max length: 9 "PROTO=AH " */
		printk("PROTO=AH ");

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ah = skb_header_pointer(skb, iphoff+ih->ihl*4,
					sizeof(_ahdr), &_ahdr);
		if (ah == NULL) {
			printk("INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Length: 15 "SPI=0xF1234567 " */
		printk("SPI=0x%x ", ntohl(ah->spi));
		break;
	}
	case IPPROTO_ESP: {
		struct ip_esp_hdr _esph, *eh;

		/* Max length: 10 "PROTO=ESP " */
		printk("PROTO=ESP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		eh = skb_header_pointer(skb, iphoff+ih->ihl*4,
					sizeof(_esph), &_esph);
		if (eh == NULL) {
			printk("INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Length: 15 "SPI=0xF1234567 " */
		printk("SPI=0x%x ", ntohl(eh->spi));
		break;
	}
	/* Max length: 10 "PROTO 255 " */
	default:
		printk("PROTO=%u ", ih->protocol);
	}

	/* Max length: 15 "UID=4294967295 " */
 	if ((info->logflags & IPT_LOG_UID) && !iphoff && skb->sk) {
		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket && skb->sk->sk_socket->file)
 			printk("UID=%u ", skb->sk->sk_socket->file->f_uid);
		read_unlock_bh(&skb->sk->sk_callback_lock);
	}

	/* Proto    Max log string length */
	/* IP:      40+46+6+11+127 = 230 */
	/* TCP:     10+max(25,20+30+13+9+32+11+127) = 252 */
	/* UDP:     10+max(25,20) = 35 */
	/* ICMP:    11+max(25, 18+25+max(19,14,24+3+n+10,3+n+10)) = 91+n */
	/* ESP:     10+max(25)+15 = 50 */
	/* AH:      9+max(25)+15 = 49 */
	/* unknown: 10 */

	/* (ICMP allows recursion one level deep) */
	/* maxlen =  IP + ICMP +  IP + max(TCP,UDP,ICMP,unknown) */
	/* maxlen = 230+   91  + 230 + 252 = 803 */
}

static void
ipt_log_packet(unsigned int hooknum,
	       const struct sk_buff *skb,
	       const struct net_device *in,
	       const struct net_device *out,
	       const struct ipt_log_info *loginfo,
	       const char *level_string,
	       const char *prefix)
{
	spin_lock_bh(&log_lock);
	printk(level_string);
	printk("%sIN=%s OUT=%s ",
	       prefix == NULL ? loginfo->prefix : prefix,
	       in ? in->name : "",
	       out ? out->name : "");
#ifdef CONFIG_BRIDGE_NETFILTER
	if (skb->nf_bridge) {
		struct net_device *physindev = skb->nf_bridge->physindev;
		struct net_device *physoutdev = skb->nf_bridge->physoutdev;

		if (physindev && in != physindev)
			printk("PHYSIN=%s ", physindev->name);
		if (physoutdev && out != physoutdev)
			printk("PHYSOUT=%s ", physoutdev->name);
	}
#endif

	if (in && !out) {
		/* MAC logging for input chain only. */
		printk("MAC=");
		if (skb->dev && skb->dev->hard_header_len
		    && skb->mac.raw != (void*)skb->nh.iph) {
			int i;
			unsigned char *p = skb->mac.raw;
			for (i = 0; i < skb->dev->hard_header_len; i++,p++)
				printk("%02x%c", *p,
				       i==skb->dev->hard_header_len - 1
				       ? ' ':':');
		} else
			printk(" ");
	}

	dump_packet(loginfo, skb, 0);
	printk("\n");
	spin_unlock_bh(&log_lock);
}

static unsigned int
ipt_log_target(struct sk_buff **pskb,
	       const struct net_device *in,
	       const struct net_device *out,
	       unsigned int hooknum,
	       const void *targinfo,
	       void *userinfo)
{
	const struct ipt_log_info *loginfo = targinfo;
	char level_string[4] = "< >";

	level_string[1] = '0' + (loginfo->level % 8);
	ipt_log_packet(hooknum, *pskb, in, out, loginfo, level_string, NULL);

	return IPT_CONTINUE;
}

static void
ipt_logfn(unsigned int hooknum,
	  const struct sk_buff *skb,
	  const struct net_device *in,
	  const struct net_device *out,
	  const char *prefix)
{
	struct ipt_log_info loginfo = { 
		.level = 0, 
		.logflags = IPT_LOG_MASK, 
		.prefix = "" 
	};

	ipt_log_packet(hooknum, skb, in, out, &loginfo, KERN_WARNING, prefix);
}

static int ipt_log_checkentry(const char *tablename,
			      const struct ipt_entry *e,
			      void *targinfo,
			      unsigned int targinfosize,
			      unsigned int hook_mask)
{
	const struct ipt_log_info *loginfo = targinfo;

	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_log_info))) {
		DEBUGP("LOG: targinfosize %u != %u\n",
		       targinfosize, IPT_ALIGN(sizeof(struct ipt_log_info)));
		return 0;
	}

	if (loginfo->level >= 8) {
		DEBUGP("LOG: level %u >= 8\n", loginfo->level);
		return 0;
	}

	if (loginfo->prefix[sizeof(loginfo->prefix)-1] != '\0') {
		DEBUGP("LOG: prefix term %i\n",
		       loginfo->prefix[sizeof(loginfo->prefix)-1]);
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_log_reg = {
	.name		= "LOG",
	.target		= ipt_log_target,
	.checkentry	= ipt_log_checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	if (ipt_register_target(&ipt_log_reg))
		return -EINVAL;
	if (nflog)
		nf_log_register(PF_INET, &ipt_logfn);
	
	return 0;
}

static void __exit fini(void)
{
	if (nflog)
		nf_log_unregister(PF_INET, &ipt_logfn);
	ipt_unregister_target(&ipt_log_reg);
}

module_init(init);
module_exit(fini);

/*
 *		IP_MASQ_FTP ftp masquerading module
 *
 *
 * Version:	@(#)ip_masq_ftp.c 0.01   02/05/96
 *
 * Author:	Wouter Gadeyne
 *		
 *
 * Fixes:
 *	Wouter Gadeyne		:	Fixed masquerading support of ftp PORT commands
 * 	Juan Jose Ciarlante	:	Code moved and adapted from ip_fw.c
 *	
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
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

#define DEBUG_CONFIG_IP_MASQ_FTP 0

static int
masq_ftp_init_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_INC_USE_COUNT;
        return 0;
}

static int
masq_ftp_done_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_DEC_USE_COUNT;
        return 0;
}

int
masq_ftp_out (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff *skb;
	struct iphdr *iph;
	struct tcphdr *th;
	char *p, *data, *data_limit;
	unsigned char p1,p2,p3,p4,p5,p6;
	__u32 from;
	__u16 port;
	struct ip_masq *n_ms;
	char buf[24];		/* xxx.xxx.xxx.xxx,ppp,ppp\000 */
        unsigned buf_len;
	int diff;

        skb = *skb_p;
	iph = skb->h.iph;
        th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
        data = (char *)&th[1];

        data_limit = skb->h.raw + skb->len - 18;

	while (data < data_limit)
	{
		if (memcmp(data,"PORT ",5) && memcmp(data,"port ",5))
		{
			data ++;
			continue;
		}
		p = data+5;
 		p1 = simple_strtoul(data+5,&data,10);
		if (*data!=',')
			continue;
		p2 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p3 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p4 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p5 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p6 = simple_strtoul(data+1,&data,10);
		if (*data!='\r' && *data!='\n')
			continue;

		from = (p1<<24) | (p2<<16) | (p3<<8) | p4;
		port = (p5<<8) | p6;
#if DEBUG_CONFIG_IP_MASQ_FTP
		printk("PORT %X:%X detected\n",from,port);
#endif	
		/*
		 * Now update or create an masquerade entry for it
		 */
#if DEBUG_CONFIG_IP_MASQ_FTP
		printk("protocol %d %lX:%X %X:%X\n", iph->protocol, htonl(from), htons(port), iph->daddr, 0);

#endif	
		n_ms = ip_masq_out_get_2(iph->protocol,
					 htonl(from), htons(port),
					 iph->daddr, 0);
		if (n_ms) {
			/* existing masquerade, clear timer */
			ip_masq_set_expire(n_ms,0);
		}
		else {
			n_ms = ip_masq_new(dev, IPPROTO_TCP,
					   htonl(from), htons(port),
					   iph->daddr, 0,
					   IP_MASQ_F_NO_DPORT);
					
			if (n_ms==NULL)
				return 0;
		}

                /*
                 * keep for a bit longer than tcp_fin, caller may not reissue
                 * PORT before tcp_fin_timeout.
                 */
                ip_masq_set_expire(n_ms, ip_masq_expire->tcp_fin_timeout*3);

		/*
		 * Replace the old PORT with the new one
		 */
		from = ntohl(n_ms->maddr);
		port = ntohs(n_ms->mport);
		sprintf(buf,"%d,%d,%d,%d,%d,%d",
			from>>24&255,from>>16&255,from>>8&255,from&255,
			port>>8&255,port&255);
		buf_len = strlen(buf);
#if DEBUG_CONFIG_IP_MASQ_FTP
		printk("new PORT %X:%X\n",from,port);
#endif	

		/*
		 * Calculate required delta-offset to keep TCP happy
		 */
		
		diff = buf_len - (data-p);
		
		/*
		 *	No shift.
		 */
		
		if (diff==0)
		{
			/*
			 * simple case, just replace the old PORT cmd
 			 */
 			memcpy(p,buf,buf_len);
 			return 0;
 		}

                *skb_p = ip_masq_skb_replace(skb, GFP_ATOMIC, p, data-p, buf, buf_len);
                return diff;

	}
	return 0;

}

struct ip_masq_app ip_masq_ftp = {
        NULL,			/* next */
	"ftp",			/* name */
        0,                      /* type */
        0,                      /* n_attach */
        masq_ftp_init_1,        /* ip_masq_init_1 */
        masq_ftp_done_1,        /* ip_masq_done_1 */
        masq_ftp_out,           /* pkt_out */
        NULL                    /* pkt_in */
};

/*
 * 	ip_masq_ftp initialization
 */

int ip_masq_ftp_init(void)
{
        return register_ip_masq_app(&ip_masq_ftp, IPPROTO_TCP, 21);
}

/*
 * 	ip_masq_ftp fin.
 */

int ip_masq_ftp_done(void)
{
        return unregister_ip_masq_app(&ip_masq_ftp);
}

#ifdef MODULE

int init_module(void)
{
        if (ip_masq_ftp_init() != 0)
                return -EIO;
        register_symtab(0);
        return 0;
}

void cleanup_module(void)
{
        if (ip_masq_ftp_done() != 0)
                printk("ip_masq_ftp: can't remove module");
}

#endif /* MODULE */

/* (C) 2001-2002 Magnus Boden <mb@ozaba.mine.nu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Version: 0.0.7
 *
 * Thu 21 Mar 2002 Harald Welte <laforge@gnumonks.org>
 * 	- port to newnat API
 *
 */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_tftp.h>
#include <linux/moduleparam.h>

MODULE_AUTHOR("Magnus Boden <mb@ozaba.mine.nu>");
MODULE_DESCRIPTION("tftp connection tracking helper");
MODULE_LICENSE("GPL");

#define MAX_PORTS 8
static int ports[MAX_PORTS];
static int ports_c;
module_param_array(ports, int, &ports_c, 0400);
MODULE_PARM_DESC(ports, "port numbers of tftp servers");

#if 0
#define DEBUGP(format, args...) printk("%s:%s:" format, \
                                       __FILE__, __FUNCTION__ , ## args)
#else
#define DEBUGP(format, args...)
#endif

unsigned int (*ip_nat_tftp_hook)(struct sk_buff **pskb,
				 enum ip_conntrack_info ctinfo,
				 struct ip_conntrack_expect *exp);
EXPORT_SYMBOL_GPL(ip_nat_tftp_hook);

static int tftp_help(struct sk_buff **pskb,
		     struct ip_conntrack *ct,
		     enum ip_conntrack_info ctinfo)
{
	struct tftphdr _tftph, *tfh;
	struct ip_conntrack_expect *exp;
	unsigned int ret = NF_ACCEPT;

	tfh = skb_header_pointer(*pskb,
				 (*pskb)->nh.iph->ihl*4+sizeof(struct udphdr),
				 sizeof(_tftph), &_tftph);
	if (tfh == NULL)
		return NF_ACCEPT;

	switch (ntohs(tfh->opcode)) {
	/* RRQ and WRQ works the same way */
	case TFTP_OPCODE_READ:
	case TFTP_OPCODE_WRITE:
		DEBUGP("");
		DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
		DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

		exp = ip_conntrack_expect_alloc();
		if (exp == NULL)
			return NF_DROP;

		exp->tuple = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
		exp->mask.src.ip = 0xffffffff;
		exp->mask.dst.ip = 0xffffffff;
		exp->mask.dst.u.udp.port = 0xffff;
		exp->mask.dst.protonum = 0xff;
		exp->expectfn = NULL;
		exp->master = ct;

		DEBUGP("expect: ");
		DUMP_TUPLE(&exp->tuple);
		DUMP_TUPLE(&exp->mask);
		if (ip_nat_tftp_hook)
			ret = ip_nat_tftp_hook(pskb, ctinfo, exp);
		else if (ip_conntrack_expect_related(exp) != 0) {
			ip_conntrack_expect_free(exp);
			ret = NF_DROP;
		}
		break;
	case TFTP_OPCODE_DATA:
	case TFTP_OPCODE_ACK:
		DEBUGP("Data/ACK opcode\n");
		break;
	case TFTP_OPCODE_ERROR:
		DEBUGP("Error opcode\n");
		break;
	default:
		DEBUGP("Unknown opcode\n");
	}
	return NF_ACCEPT;
}

static struct ip_conntrack_helper tftp[MAX_PORTS];
static char tftp_names[MAX_PORTS][10];

static void fini(void)
{
	int i;

	for (i = 0 ; i < ports_c; i++) {
		DEBUGP("unregistering helper for port %d\n",
			ports[i]);
		ip_conntrack_helper_unregister(&tftp[i]);
	} 
}

static int __init init(void)
{
	int i, ret;
	char *tmpname;

	if (ports_c == 0)
		ports[ports_c++] = TFTP_PORT;

	for (i = 0; i < ports_c; i++) {
		/* Create helper structure */
		memset(&tftp[i], 0, sizeof(struct ip_conntrack_helper));

		tftp[i].tuple.dst.protonum = IPPROTO_UDP;
		tftp[i].tuple.src.u.udp.port = htons(ports[i]);
		tftp[i].mask.dst.protonum = 0xFF;
		tftp[i].mask.src.u.udp.port = 0xFFFF;
		tftp[i].max_expected = 1;
		tftp[i].timeout = 5 * 60; /* 5 minutes */
		tftp[i].me = THIS_MODULE;
		tftp[i].help = tftp_help;

		tmpname = &tftp_names[i][0];
		if (ports[i] == TFTP_PORT)
			sprintf(tmpname, "tftp");
		else
			sprintf(tmpname, "tftp-%d", i);
		tftp[i].name = tmpname;

		DEBUGP("port #%d: %d\n", i, ports[i]);

		ret=ip_conntrack_helper_register(&tftp[i]);
		if (ret) {
			printk("ERROR registering helper for port %d\n",
				ports[i]);
			fini();
			return(ret);
		}
	}
	return(0);
}

module_init(init);
module_exit(fini);

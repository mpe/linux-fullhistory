/*
 * sysctl_net_ipv6.c: sysctl interface to net IPV6 subsystem.
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/addrconf.h>


int ipv6_hop_limit = IPV6_DEFAULT_HOPLIMIT;

int ipv6_sysctl_forwarding(ctl_table *ctl, int write, struct file * filp,
			   void *buffer, size_t *lenp)
{
	int val = ipv6_forwarding;
	int retv;

	retv = proc_dointvec(ctl, write, filp, buffer, lenp);

	if (write)
	{
		if (ipv6_forwarding && val == 0) {
			printk(KERN_DEBUG "sysctl: IPv6 forwarding enabled\n");
			ndisc_forwarding_on();
			addrconf_forwarding_on();		       
		}

		if (ipv6_forwarding == 0 && val) {
			ndisc_forwarding_off();
		}
	}
	return retv;
}

ctl_table ipv6_table[] = {
        {NET_IPV6_FORWARDING, "ipv6_forwarding",
         &ipv6_forwarding, sizeof(int), 0644, NULL,
         &ipv6_sysctl_forwarding},

	{NET_IPV6_HOPLIMIT, "ipv6_hop_limit",
         &ipv6_hop_limit, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{0}
};

#ifdef MODULE
static struct ctl_table_header *ipv6_sysctl_header;
static struct ctl_table ipv6_root_table[];
static struct ctl_table ipv6_net_table[];


ctl_table ipv6_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, ipv6_net_table},
        {0}
};

ctl_table ipv6_net_table[] = {
	{NET_IPV6, "ipv6", NULL, 0, 0555, ipv6_table},
        {0}
};

void ipv6_sysctl_register(void)
{
	ipv6_sysctl_header = register_sysctl_table(ipv6_root_table, 0);
}

void ipv6_sysctl_unregister(void)
{
	unregister_sysctl_table(ipv6_sysctl_header);
}

#endif


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

struct ipv6_config ipv6_config =
{
	0,				/* forwarding		*/
	IPV6_DEFAULT_HOPLIMIT,		/* hop limit		*/
	1,				/* accept RAs		*/
	1,				/* accept redirects	*/

	3,				/* nd_max_mcast_solicit	*/
	3,				/* nd_max_ucast_solicit	*/
	RETRANS_TIMER,			/* nd_retrans_time	*/
	RECHABLE_TIME,			/* nd_base_reach_time	*/
	(5 * HZ),			/* nd_delay_probe_time	*/

	1,				/* autoconfiguration	*/
	1,				/* dad transmits	*/
	MAX_RTR_SOLICITATIONS,		/* router solicits	*/
	RTR_SOLICITATION_INTERVAL,	/* rtr solicit interval	*/
	MAX_RTR_SOLICITATION_DELAY,	/* rtr solicit delay	*/

	60*HZ,				/* rt cache timeout	*/
	30*HZ,				/* rt gc period		*/
};

#ifdef CONFIG_SYSCTL

int ipv6_sysctl_forwarding(ctl_table *ctl, int write, struct file * filp,
			   void *buffer, size_t *lenp)
{
	int val = ipv6_config.forwarding;
	int retv;

	retv = proc_dointvec(ctl, write, filp, buffer, lenp);

	if (write) {
		if (ipv6_config.forwarding && val == 0) {
			printk(KERN_DEBUG "sysctl: IPv6 forwarding enabled\n");
			ndisc_forwarding_on();
			addrconf_forwarding_on();		       
		}

		if (ipv6_config.forwarding == 0 && val)
			ndisc_forwarding_off();
	}
	return retv;
}

ctl_table ipv6_table[] = {
        {NET_IPV6_FORWARDING, "forwarding",
         &ipv6_config.forwarding, sizeof(int), 0644, NULL,
         &ipv6_sysctl_forwarding},

	{NET_IPV6_HOPLIMIT, "hop_limit",
         &ipv6_config.hop_limit, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ACCEPT_RA, "accept_ra",
         &ipv6_config.accept_ra, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ACCEPT_REDIRECTS, "accept_redirects",
         &ipv6_config.accept_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ND_MAX_MCAST_SOLICIT, "nd_max_mcast_solicit",
         &ipv6_config.nd_max_mcast_solicit, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ND_MAX_UCAST_SOLICIT, "nd_max_ucast_solicit",
         &ipv6_config.nd_max_ucast_solicit, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ND_RETRANS_TIME, "nd_retrans_time",
         &ipv6_config.nd_retrans_time, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ND_REACHABLE_TIME, "nd_base_reachble_time",
         &ipv6_config.nd_base_reachable_time, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ND_DELAY_PROBE_TIME, "nd_delay_first_probe_time",
         &ipv6_config.nd_delay_probe_time, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_AUTOCONF, "autoconf",
         &ipv6_config.autoconf, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_DAD_TRANSMITS, "dad_transmits",
         &ipv6_config.dad_transmits, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_RTR_SOLICITS, "router_solicitations",
         &ipv6_config.rtr_solicits, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_RTR_SOLICIT_INTERVAL, "router_solicitation_interval",
         &ipv6_config.rtr_solicit_interval, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_RTR_SOLICIT_DELAY, "router_solicitation_delay",
         &ipv6_config.rtr_solicit_delay, sizeof(int), 0644, NULL,
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
#endif	/* MODULE */

#endif /* CONFIG_SYSCTL */




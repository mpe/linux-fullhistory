/*
 * sysctl_net_ipv4.c: sysctl interface to net IPV4 subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipv4 directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/tcp.h>

/*
 *	TCP configuration parameters
 */

#define TCP_PMTU_DISC	0x00000001	/* perform PMTU discovery	  */
#define TCP_CONG_AVOID	0x00000002	/* congestion avoidance algorithm */
#define TCP_DELAY_ACKS	0x00000003	/* delayed ack stategy		  */

#if 0
static int boolean_min = 0;
static int boolean_max = 1;
#endif

/* From arp.c */
extern int sysctl_arp_res_time;
extern int sysctl_arp_dead_res_time;
extern int sysctl_arp_max_tries;
extern int sysctl_arp_timeout;
extern int sysctl_arp_check_interval;
extern int sysctl_arp_confirm_interval;
extern int sysctl_arp_confirm_timeout;

extern int sysctl_tcp_cong_avoidance;
extern int tcp_sysctl_congavoid(ctl_table *ctl, int write, struct file * filp,
				void *buffer, size_t *lenp);

struct ipv4_config ipv4_config = { 1, 1, 1, 1, };
struct ipv4_config ipv4_def_router_config = { 0, 1, 1, 1, 1, 1, 1, };
struct ipv4_config ipv4_def_host_config = { 1, 1, 1, 1, };

int ipv4_sysctl_forwarding(ctl_table *ctl, int write, struct file * filp,
			   void *buffer, size_t *lenp)
{
	int val = IS_ROUTER;
	int ret;

	ret = proc_dointvec(ctl, write, filp, buffer, lenp);

	if (write && IS_ROUTER != val) {
		if (IS_ROUTER)
			ipv4_config = ipv4_def_router_config;
		else
			ipv4_config = ipv4_def_host_config;
		rt_cache_flush(0);
        }
        return ret;
}

ctl_table ipv4_table[] = {
        {NET_IPV4_ARP_RES_TIME, "arp_res_time",
	 &sysctl_arp_res_time, sizeof(int), 0644, NULL, &proc_dointvec},
        {NET_IPV4_ARP_DEAD_RES_TIME, "arp_dead_res_time",
         &sysctl_arp_dead_res_time, sizeof(int), 0644, NULL, &proc_dointvec},
        {NET_IPV4_ARP_MAX_TRIES, "arp_max_tries",
         &sysctl_arp_max_tries, sizeof(int), 0644, NULL, &proc_dointvec},
        {NET_IPV4_ARP_TIMEOUT, "arp_timeout",
         &sysctl_arp_timeout, sizeof(int), 0644, NULL, &proc_dointvec},
        {NET_IPV4_ARP_CHECK_INTERVAL, "arp_check_interval",
         &sysctl_arp_check_interval, sizeof(int), 0644, NULL, &proc_dointvec},
        {NET_IPV4_ARP_CONFIRM_INTERVAL, "arp_confirm_interval",
         &sysctl_arp_confirm_interval, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_ARP_CONFIRM_TIMEOUT, "arp_confirm_timeout",
         &sysctl_arp_confirm_timeout, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_TCP_VEGAS_CONG_AVOID, "tcp_vegas_cong_avoid",
	 &sysctl_tcp_cong_avoidance, sizeof(int), 0644,
	 NULL, &tcp_sysctl_congavoid },
        {NET_IPV4_FORWARDING, "ip_forwarding",
         &ip_statistics.IpForwarding, sizeof(int), 0644, NULL,
         &ipv4_sysctl_forwarding},
        {NET_IPV4_DEFAULT_TTL, "ip_default_ttl",
         &ip_statistics.IpDefaultTTL, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_RFC1812_FILTER, "ip_rfc1812_filter",
         &ipv4_config.rfc1812_filter, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_LOG_MARTIANS, "ip_log_martians",
         &ipv4_config.log_martians, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_SOURCE_ROUTE, "ip_source_route",
         &ipv4_config.source_route, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_ADDRMASK_AGENT, "ip_addrmask_agent",
         &ipv4_config.addrmask_agent, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_BOOTP_AGENT, "ip_bootp_agent",
         &ipv4_config.bootp_agent, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_BOOTP_RELAY, "ip_bootp_relay",
         &ipv4_config.bootp_relay, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_FIB_MODEL, "ip_fib_model",
         &ipv4_config.fib_model, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_NO_PMTU_DISC, "ip_no_pmtu_disc",
         &ipv4_config.no_pmtu_disc, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_ACCEPT_REDIRECTS, "ip_accept_redirects",
         &ipv4_config.accept_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_SECURE_REDIRECTS, "ip_secure_redirects",
         &ipv4_config.secure_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_RFC1620_REDIRECTS, "ip_rfc1620_redirects",
         &ipv4_config.rfc1620_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{0}
};

/*
 * sysctl_net_ipv4.c: sysctl interface to net IPV4 subsystem.
 *
 * $Id: sysctl_net_ipv4.c,v 1.23 1997/12/13 21:52:57 kuznet Exp $
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipv4 directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/config.h>
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

/* From icmp.c */
extern int sysctl_icmp_echo_ignore_all;
extern int sysctl_icmp_echo_ignore_broadcasts;

/* From ip_fragment.c */
extern int sysctl_ipfrag_low_thresh;
extern int sysctl_ipfrag_high_thresh; 
extern int sysctl_ipfrag_time;

/* From ip_output.c */
extern int sysctl_ip_dynaddr;

/* From ip_masq.c */
extern int sysctl_ip_masq_debug;

extern int sysctl_tcp_cong_avoidance;
extern int sysctl_tcp_hoe_retransmits;
extern int sysctl_tcp_sack;
extern int sysctl_tcp_tsack;
extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;
extern int sysctl_tcp_keepalive_time;
extern int sysctl_tcp_keepalive_probes;
extern int sysctl_tcp_max_ka_probes;
extern int sysctl_tcp_retries1;
extern int sysctl_tcp_retries2;
extern int sysctl_tcp_max_delay_acks;
extern int sysctl_tcp_fin_timeout;
extern int sysctl_tcp_syncookies;
extern int sysctl_tcp_syn_retries;
extern int sysctl_tcp_stdurg; 
extern int sysctl_tcp_syn_taildrop; 
extern int sysctl_max_syn_backlog; 

/* From icmp.c */
extern int sysctl_icmp_sourcequench_time; 
extern int sysctl_icmp_destunreach_time;
extern int sysctl_icmp_timeexceed_time;
extern int sysctl_icmp_paramprob_time;
extern int sysctl_icmp_echoreply_time;

int tcp_retr1_max = 255; 

extern int tcp_sysctl_congavoid(ctl_table *ctl, int write, struct file * filp,
				void *buffer, size_t *lenp);

struct ipv4_config ipv4_config = { 1, 1, 1, 0, };

#ifdef CONFIG_SYSCTL

struct ipv4_config ipv4_def_router_config = { 0, 1, 1, 1, 1, 1, 1, };
struct ipv4_config ipv4_def_host_config = { 1, 1, 1, 0, };

static
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

static
int ipv4_sysctl_rtcache_flush(ctl_table *ctl, int write, struct file * filp,
			      void *buffer, size_t *lenp)
{
	if (write) {
		rt_cache_flush(0);
		return 0;
	} else
		return -EINVAL;
}

ctl_table ipv4_table[] = {
        {NET_IPV4_TCP_HOE_RETRANSMITS, "tcp_hoe_retransmits",
         &sysctl_tcp_hoe_retransmits, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_TCP_SACK, "tcp_sack",
         &sysctl_tcp_sack, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_TCP_TSACK, "tcp_tsack",
         &sysctl_tcp_tsack, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_TCP_TIMESTAMPS, "tcp_timestamps",
         &sysctl_tcp_timestamps, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_TCP_WINDOW_SCALING, "tcp_window_scaling",
         &sysctl_tcp_window_scaling, sizeof(int), 0644, NULL,
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
        {NET_IPV4_SEND_REDIRECTS, "ip_send_redirects",
         &ipv4_config.send_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_AUTOCONFIG, "ip_autoconfig",
         &ipv4_config.autoconfig, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_BOOTP_RELAY, "ip_bootp_relay",
         &ipv4_config.bootp_relay, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_PROXY_ARP, "ip_proxy_arp",
         &ipv4_config.proxy_arp, sizeof(int), 0644, NULL,
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
        {NET_IPV4_RTCACHE_FLUSH, "ip_rtcache_flush",
         NULL, sizeof(int), 0644, NULL,
         &ipv4_sysctl_rtcache_flush},
	{NET_IPV4_TCP_SYN_RETRIES, "tcp_syn_retries",
	 &sysctl_tcp_syn_retries, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_IPFRAG_HIGH_THRESH, "ipfrag_high_thresh",
	 &sysctl_ipfrag_high_thresh, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_IPFRAG_LOW_THRESH, "ipfrag_low_thresh",
	 &sysctl_ipfrag_low_thresh, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_IP_DYNADDR, "ip_dynaddr",
	 &sysctl_ip_dynaddr, sizeof(int), 0644, NULL, &proc_dointvec},
#ifdef CONFIG_IP_MASQUERADE
	{NET_IPV4_IP_MASQ_DEBUG, "ip_masq_debug",
	 &sysctl_ip_masq_debug, sizeof(int), 0644, NULL, &proc_dointvec},
#endif
	{NET_IPV4_IPFRAG_TIME, "ipfrag_time",
	 &sysctl_ipfrag_time, sizeof(int), 0644, NULL, &proc_dointvec_jiffies},
	{NET_IPV4_TCP_MAX_KA_PROBES, "tcp_max_ka_probes",
	 &sysctl_tcp_max_ka_probes, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_TCP_KEEPALIVE_TIME, "tcp_keepalive_time",
	 &sysctl_tcp_keepalive_time, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies},
	{NET_IPV4_TCP_KEEPALIVE_PROBES, "tcp_keepalive_probes",
	 &sysctl_tcp_keepalive_probes, sizeof(int), 0644, NULL, 
	 &proc_dointvec},
	{NET_IPV4_TCP_RETRIES1, "tcp_retries1",
	 &sysctl_tcp_retries1, sizeof(int), 0644, NULL, &proc_dointvec_minmax, 
	 &sysctl_intvec, NULL, NULL, &tcp_retr1_max},
	{NET_IPV4_TCP_RETRIES2, "tcp_retries2",
	 &sysctl_tcp_retries2, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_TCP_MAX_DELAY_ACKS, "tcp_max_delay_acks",
	 &sysctl_tcp_max_delay_acks, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_TCP_FIN_TIMEOUT, "tcp_fin_timeout",
	 &sysctl_tcp_fin_timeout, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies},
#ifdef CONFIG_SYN_COOKIES
	{NET_TCP_SYNCOOKIES, "tcp_syncookies",
	 &sysctl_tcp_syncookies, sizeof(int), 0644, NULL, &proc_dointvec},
#endif
	{NET_TCP_STDURG, "tcp_stdurg", &sysctl_tcp_stdurg,
	 sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_TCP_SYN_TAILDROP, "tcp_syn_taildrop", &sysctl_tcp_syn_taildrop,
	 sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_TCP_MAX_SYN_BACKLOG, "tcp_max_syn_backlog", &sysctl_max_syn_backlog,
	 sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_LOCAL_PORT_RANGE, "ip_local_port_range",
	 &sysctl_local_port_range, sizeof(sysctl_local_port_range), 0644, 
	 NULL, &proc_dointvec},
	{NET_IPV4_ICMP_ECHO_IGNORE_ALL, "icmp_echo_ignore_all",
	 &sysctl_icmp_echo_ignore_all, sizeof(int), 0644, NULL,
	 &proc_dointvec},
	{NET_IPV4_ICMP_ECHO_IGNORE_BROADCASTS, "icmp_echo_ignore_broadcasts",
	 &sysctl_icmp_echo_ignore_broadcasts, sizeof(int), 0644, NULL,
	 &proc_dointvec},
	{NET_IPV4_ICMP_SOURCEQUENCH_RATE, "icmp_sourcequench_rate",
	 &sysctl_icmp_sourcequench_time, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_ICMP_DESTUNREACH_RATE, "icmp_destunreach_rate",
	 &sysctl_icmp_destunreach_time, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_ICMP_TIMEEXCEED_RATE, "icmp_timeexceed_rate",
	 &sysctl_icmp_timeexceed_time, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_ICMP_PARAMPROB_RATE, "icmp_paramprob_rate",
	 &sysctl_icmp_paramprob_time, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_IPV4_ICMP_ECHOREPLY_RATE, "icmp_echoreply_rate",
	 &sysctl_icmp_echoreply_time, sizeof(int), 0644, NULL, &proc_dointvec},
	{0}
};

#endif /* CONFIG_SYSCTL */

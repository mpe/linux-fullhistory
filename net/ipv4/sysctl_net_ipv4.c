/* -*- linux-c -*-
 * sysctl_net_ipv4.c: sysctl interface to net IPV4 subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipv4 directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
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
#if 0
	{TCP_PMTU_DISC, "tcp_pmtu_discovery",
	&ipv4_pmtu_discovery, sizeof(int), 644, 
	NULL, &proc_dointvec, &sysctl_intvec_minmax, 
	&boolean_min, &boolean_max},
#endif

	{NET_IPV4_TCP_VEGAS_CONG_AVOID, "tcp_vegas_cong_avoid",
	 &sysctl_tcp_cong_avoidance, sizeof(int), 0644,
	 NULL, &tcp_sysctl_congavoid },
	{0}
};

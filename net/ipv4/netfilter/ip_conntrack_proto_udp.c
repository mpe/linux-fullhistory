#include <linux/types.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <linux/udp.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>

#define UDP_TIMEOUT (60*HZ)

static int udp_pkt_to_tuple(const void *datah, size_t datalen,
			    struct ip_conntrack_tuple *tuple)
{
	const struct udphdr *hdr = datah;

	tuple->src.u.udp.port = hdr->source;
	tuple->dst.u.udp.port = hdr->dest;

	return 1;
}

static int udp_invert_tuple(struct ip_conntrack_tuple *tuple,
			    const struct ip_conntrack_tuple *orig)
{
	tuple->src.u.udp.port = orig->dst.u.udp.port;
	tuple->dst.u.udp.port = orig->src.u.udp.port;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static unsigned int udp_print_tuple(char *buffer,
				    const struct ip_conntrack_tuple *tuple)
{
	return sprintf(buffer, "sport=%hu dport=%hu ",
		       ntohs(tuple->src.u.udp.port),
		       ntohs(tuple->dst.u.udp.port));
}

/* Print out the private part of the conntrack. */
static unsigned int udp_print_conntrack(char *buffer,
					const struct ip_conntrack *conntrack)
{
	return 0;
}

/* Returns verdict for packet, and may modify conntracktype */
static int udp_packet(struct ip_conntrack *conntrack,
		      struct iphdr *iph, size_t len,
		      enum ip_conntrack_info conntrackinfo)
{
	/* Refresh. */
	ip_ct_refresh(conntrack, UDP_TIMEOUT);
	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int udp_new(struct ip_conntrack *conntrack,
		    struct iphdr *iph, size_t len)
{
	return 1;
}

struct ip_conntrack_protocol ip_conntrack_protocol_udp
= { { NULL, NULL }, IPPROTO_UDP, "udp",
    udp_pkt_to_tuple, udp_invert_tuple, udp_print_tuple, udp_print_conntrack,
    udp_packet, udp_new, NULL };

/*
   There is only 1 optname (see setsockopt(2)), IP_FW_MASQ_CTL that
   must be used.
   Funcionality depends on your kernel CONFIG options, here is
   an example you can use to create an ``incoming'' tunnel:

   See "user.c" module under ipmasqadm tree for a generic example
 */
#undef __KERNEL__	/* Makefile lazyness ;) */
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <asm/types.h>          /* For __uXX types */
#include <net/if.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <linux/ip_fw.h>        /* For IP_FW_MASQ_CTL */
#include <linux/ip_masq.h>      /* For specific masq defs */


int create_listening_masq(struct ip_masq_ctl *masq, int proto, u_int32_t src_addr, u_int16_t src_port, u_int32_t dst_addr)
{
	int sockfd;

	sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

	if (sockfd<0) {
		perror("socket(RAW)");
		return -1;
	}

	memset (masq, 0, sizeof (*masq));

	/*
	 *	Want user tunnel control
	 */
	masq->m_target = IP_MASQ_TARGET_USER;

	/*
	 *	Want to insert new
	 */
	masq->m_cmd    = IP_MASQ_CMD_INSERT;

	masq->u.user.protocol	= proto;
	masq->u.user.saddr	= src_addr;
	masq->u.user.sport	= src_port;
	masq->u.user.rt_daddr	= inet_addr("192.168.21.239"); 

	if (setsockopt(sockfd, IPPROTO_IP, 
				IP_FW_MASQ_CTL, (char *)masq, sizeof(*masq)))    {
		perror("setsockopt()");
		return -1;
	}
	/* masq struct now contains tunnel details */
	fprintf(stderr, "PROTO=%d SRC=0x%X:%x - MASQ=0x%X:%x - DST=0x%X:%x\n",
			masq->u.user.protocol,
			ntohl(masq->u.user.saddr), ntohs(masq->u.user.sport),
			ntohl(masq->u.user.maddr), ntohs(masq->u.user.mport),
			ntohl(masq->u.user.daddr), ntohs(masq->u.user.dport));
	return 0;
}

int main(void) {
	struct ip_masq_ctl masq_buf;

	return create_listening_masq(&masq_buf,
			IPPROTO_TCP, 
			inet_addr("192.168.1.4"), 
			htons(23),
			inet_addr("192.168.21.3"));
}


#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>

struct inet6_protocol *inet6_protocol_base = NULL;
struct inet6_protocol *inet6_protos[MAX_INET_PROTOS] = 
{
	NULL
};


struct inet6_protocol *inet6_get_protocol(unsigned char prot)
{
	unsigned char hash;
	struct inet6_protocol *p;

	hash = prot & (MAX_INET_PROTOS - 1);
	for (p = inet6_protos[hash] ; p != NULL; p=p->next) 
	{
		if (p->protocol == prot) 
			return((struct inet6_protocol *) p);
	}
	return(NULL);
}

void inet6_add_protocol(struct inet6_protocol *prot)
{
	unsigned char hash;
	struct inet6_protocol *p2;

	hash = prot->protocol & (MAX_INET_PROTOS - 1);
	prot ->next = inet6_protos[hash];
	inet6_protos[hash] = prot;
	prot->copy = 0;

	/*
	 *	Set the copy bit if we need to. 
	 */
	 
	p2 = (struct inet6_protocol *) prot->next;
	while(p2 != NULL) 
	{
		if (p2->protocol == prot->protocol) 
		{
			prot->copy = 1;
			break;
		}
		p2 = (struct inet6_protocol *) p2->next;
	}
}

/*
 *	Remove a protocol from the hash tables.
 */
 
int inet6_del_protocol(struct inet6_protocol *prot)
{
	struct inet6_protocol *p;
	struct inet6_protocol *lp = NULL;
	unsigned char hash;

	hash = prot->protocol & (MAX_INET_PROTOS - 1);
	if (prot == inet6_protos[hash]) 
	{
		inet6_protos[hash] = (struct inet6_protocol *) inet6_protos[hash]->next;
		return(0);
	}

	p = (struct inet6_protocol *) inet6_protos[hash];
	while(p != NULL) 
	{
		/*
		 * We have to worry if the protocol being deleted is
		 * the last one on the list, then we may need to reset
		 * someone's copied bit.
		 */
		if (p->next != NULL && p->next == prot) 
		{
			/*
			 * if we are the last one with this protocol and
			 * there is a previous one, reset its copy bit.
			 */
			if (p->copy == 0 && lp != NULL) 
				lp->copy = 0;
			p->next = prot->next;
			return(0);
		}
		if (p->next != NULL && p->next->protocol == prot->protocol) 
			lp = p;

		p = (struct inet6_protocol *) p->next;
	}
	return(-1);
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o protocol.o protocol.c"
 * End:
 */

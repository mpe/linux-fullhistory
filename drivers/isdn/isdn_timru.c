/* $Id: isdn_timru.c,v 1.2 1998/03/07 23:17:28 fritz Exp $
 *
 * Linux ISDN subsystem, timeout-rules for network interfaces.
 *
 * Copyright 1997       by Christian Lademann <cal@zls.de>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: isdn_timru.c,v $
 * Revision 1.2  1998/03/07 23:17:28  fritz
 * Added RCS keywords
 * Bugfix: Did not compile without isdn_dumppkt beeing enabled.
 *
 */

/*
02.06.97:cal:
    - ISDN_TIMRU_PACKET_NONE = 0 definiert, die anderen ISDN_TIMRU_PACKET_* -
      Definitionen jeweils inkrementiert

    - isdn_net_recalc_timeout():
      - In der Schleife zum Finden einer passenden Regel wurde in jedem Fall
        die Wildcard-Kette durchsucht. Jetzt nicht mehr.
      - beim Testen einer Bringup-Regel wird der anfaengliche Timeout auf den
        Hangup-Timeout des Devices gesetzt (lp->onhtime).

10.06.97:cal:
    - isdn_net_recalc_timeout(): rule->neg-Handling gesaeubert: eine Regel passt
      genau dann, wenn match(rule) XOR rule->neg und das bei allen Regeltypen.
    - isdn_net_add_rule(): rule->timeout bei BRINGUP immer 1, sonst > 0.
    - alle return(-1), die zu ioctl-Calls zurueckgehen --> return(-EINVAL).
    - div. Leerzeilen geloescht / eingefuegt; alle return's "geklammert".

12.06.97:cal:
    - isdn_net_recalc_timeout(): Falls IP-Masquerading verwendet wird, kann mit
      der neuen Option CONFIG_TIMRU_USE_MASQ der Regel-Match auf die ursprueng-
      lichen Adressen und nicht auf die der Firewall angewendet werden. Dazu ist
      ein Patch in net/ipv4/ip_masq.c notwendig: ip_masq_in_get_2 muss
      exportiert werden.

26.06.97:cal:
    - isdn_net_add_rule(): rule->timeout darf bei BRINGUP >= 0 sein. Damit
      laesst sich folgende Systax erreichen: "Falls Paket passt, starte die
      Verbindung NICHT", wie es im Stand 970602 moeglich war.
    - isdn_net_recalc_timeout(): BRINGUP: initial timeout wird auf den in der
      passenden Regel gefundenen Timeout gesetzt. Ist dieser 0, so wird die
      Verbindung nicht aufgebaut.

16.10.97:cal:
    - isdn_net_recalc_timeout(): beachte Fake-Header, der bei ausgehenden
      SyncPPP-Paketen eingesetzt wird;
      TimRu's "recalc timeout:" - Meldungen in einer Zeile

04.11.97:cal:
    - isdn_net.c, isdn_net_new(): Timeout-Rules nicht mehr automatisch
      alloziieren;
    - isdn_net.c, isdn_net_autohup(): AutoHup auch durchfuehren, wenn keine
      Timeout-Rules alloziiert sind.
*/
/*
TODO:

- Masq-Adressen statt Paketadresse ausgeben, falls die Masq-Adressen verwendet
  werden.

- Masq-Adressen-Verwendung als Option in den Regeln vorsehen

- TCP-Flags als Regel-Optionen

- weitere Verfeinerungen fuer Nicht-TCP/IP-Pakete
*/

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include <linux/if_arp.h>
#include <linux/in.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#ifdef CONFIG_ISDN_TIMRU_USE_MASQ
#ifdef CONFIG_IP_MASQUERADE
#include <net/ip_masq.h>
#endif
#endif
#include "isdn_common.h"
#include "isdn_net.h"
#ifdef CONFIG_ISDN_PPP
#include "isdn_ppp.h"
#endif

#ifdef CONFIG_ISDN_TIMEOUT_RULES

static int
isdn_timru_match(isdn_timeout_rule *this, isdn_timeout_rule *rule);


#define printk_ip(a)	 printk("%ld.%ld.%ld.%ld",(ntohl(a)>>24)&0xFF,\
					      (ntohl(a)>>16)&0xFF,\
					      (ntohl(a)>>8)&0xFF,\
					      (ntohl(a))&0xFF)
#define printk_port(a)	 printk("%d",ntohs(a))


#define	VERBOSE_PRINTK(v, l, p...)	{ \
	if(dev->net_verbose >= (v)) { \
		printk(l ## p); \
	} else { ; } \
}


int
isdn_net_recalc_timeout(int type, int prot, struct device *ndev, void *buf, ulong arg) {
	isdn_net_local		*lp = (isdn_net_local *)ndev->priv;
	struct sk_buff		*skb;
	struct iphdr		*ip;
	struct icmphdr		*icmp;
	struct tcphdr		*tcp;
	struct udphdr		*udp;
#ifdef CONFIG_ISDN_TIMRU_USE_MASQ
#ifdef CONFIG_IP_MASQUERADE
	struct ip_masq		*masq;
	int			m_prot;
	__u32			m_saddr, m_daddr;
	__u16			m_sport, m_dport;
	int			check_for_masq;
#endif
#endif

	isdn_timeout_rule	match_rule;

/*
	char			*cbuf;
*/
	int			ppp_proto, ppp_hdrlen = 0, new_timeout;


	match_rule.type = type;
	match_rule.protfam = ISDN_TIMRU_PROTFAM_WILDCARD;

	if(dev->net_verbose > 4) {
		printk(KERN_DEBUG "recalc_timeout:");
		switch(type) {
		case ISDN_TIMRU_BRINGUP:	printk("BRINGUP, "); break;
		case ISDN_TIMRU_KEEPUP_IN:	printk("KEEPUP_IN, "); break;
		case ISDN_TIMRU_KEEPUP_OUT:	printk("KEEPUP_OUT, "); break;
		default:
			printk("ERROR\n");
			return(-1);
			break;
		}
	}

	switch(prot) {
	case ISDN_TIMRU_PACKET_PPP:
	case ISDN_TIMRU_PACKET_PPP_NO_HEADER:
		if(prot == ISDN_TIMRU_PACKET_PPP) {
			ppp_proto = PPP_PROTOCOL((char *)buf);
/*
			cbuf = (char *)(buf + PPP_HDRLEN);
*/
		} else {
			ppp_proto = (int)arg;
/*
			cbuf = (char *)buf;
*/
		}

		match_rule.protfam = ISDN_TIMRU_PROTFAM_PPP;
		match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_WILDCARD;

		switch(ppp_proto) {
		case PPP_IPCP:
			match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_IPCP;
			VERBOSE_PRINTK(5, "", "PPP/IPCP\n");
			break;

		case PPP_IPXCP:
			match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_IPXCP;
			VERBOSE_PRINTK(5, "", "PPP/IPXCP\n");
			break;

		case PPP_CCP:
			match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_CCP;
			VERBOSE_PRINTK(5, "", "PPP/CCP\n");
			break;

		case PPP_LCP:
			match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_LCP;
			VERBOSE_PRINTK(5, "", "PPP/LCP\n");
			break;

		case PPP_PAP:
			match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_PAP;
			VERBOSE_PRINTK(5, "", "PPP/PAP\n");
			break;

		case PPP_LQR:
			match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_LQR;
			VERBOSE_PRINTK(5, "", "PPP/LQR\n");
			break;

		case PPP_CHAP:
			match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_CHAP;
			VERBOSE_PRINTK(5, "", "PPP/CHAP\n");
			break;

		default:
			match_rule.rule.ppp.protocol = ISDN_TIMRU_PPP_WILDCARD;

			if(dev->net_verbose >= 5) {
				printk("PPP/? (%x)\n", ppp_proto);
#ifdef ISDN_DEBUG_NET_DUMP
				isdn_dumppkt("R:", (u_char *)buf, 40, 40);
#endif
			}
			break;
		}
		break;

	case ISDN_TIMRU_PACKET_SKB:
		skb = (struct sk_buff *)buf;

#ifdef CONFIG_ISDN_PPP
		if((type == ISDN_TIMRU_BRINGUP ||
		type == ISDN_TIMRU_KEEPUP_OUT) &&
		lp->p_encap == ISDN_NET_ENCAP_SYNCPPP) {
			/* jump over fake header. */
			ppp_hdrlen = IPPP_MAX_HEADER;
		}
#endif


		switch(ntohs(skb->protocol)) {
		case ETH_P_IP:
/*
			if(!(ip = skb->ip_hdr))
*/
				ip = (struct iphdr *)(skb->data + ppp_hdrlen);

			match_rule.protfam = ISDN_TIMRU_PROTFAM_IP;
			match_rule.rule.ip.saddr.s_addr = ip->saddr;
			match_rule.rule.ip.daddr.s_addr = ip->daddr;
			match_rule.rule.ip.protocol = ISDN_TIMRU_IP_WILDCARD;

			switch(ip->protocol) {
			case IPPROTO_ICMP:
				if(!(icmp = (struct icmphdr *)((unsigned long *)ip+ip->ihl))) {
					VERBOSE_PRINTK(5, "", "IP/ICMP HDR-ERR\n");
				} else {
					match_rule.rule.ip.protocol = ISDN_TIMRU_IP_ICMP;
					match_rule.rule.ip.pt.type.from = icmp->type;

					if(dev->net_verbose >= 5) {
						printk("IP/ICMP ");
						printk_ip(ip->saddr);
						printk(" --> ");
						printk_ip(ip->daddr);
						printk("/");
						printk_port(icmp->type);
						printk("\n");
					}
				}
				break;
	
			case IPPROTO_IGMP:
				match_rule.rule.ip.protocol = ISDN_TIMRU_IP_WILDCARD;
				VERBOSE_PRINTK(5, "", "IP/IGMP\n");
				break;
	
			case IPPROTO_IPIP:
				match_rule.rule.ip.protocol = ISDN_TIMRU_IP_WILDCARD;
				VERBOSE_PRINTK(5, "", "IP/IPIP\n");
				break;
	
			case IPPROTO_TCP:
				if(!(tcp = (struct tcphdr *)((unsigned long *)ip+ip->ihl))) {
					VERBOSE_PRINTK(5, "", "IP/TCP HDR-ERR\n");
				} else {
					match_rule.rule.ip.protocol = ISDN_TIMRU_IP_TCP;
					match_rule.rule.ip.pt.port.s_from = tcp->source;
					match_rule.rule.ip.pt.port.d_from = tcp->dest;

					if(dev->net_verbose >= 5) {
						printk("IP/TCP ");
						printk_ip(ip->saddr);
						printk("/");
						printk_port(tcp->source);
						printk(" --> ");
						printk_ip(ip->daddr);
						printk("/");
						printk_port(tcp->dest);
						printk("\n");
					}
				}
				break;
	
			case IPPROTO_EGP:
				match_rule.rule.ip.protocol = ISDN_TIMRU_IP_WILDCARD;
				VERBOSE_PRINTK(5, "", "IP/EGP\n");
				break;
	
			case IPPROTO_PUP:
				match_rule.rule.ip.protocol = ISDN_TIMRU_IP_WILDCARD;
				VERBOSE_PRINTK(5, "" "IP/PUP\n");
				break;
	
			case IPPROTO_UDP:
				if(!(udp=(struct udphdr *)((unsigned long *)ip+ip->ihl))) {
					VERBOSE_PRINTK(5, "", "IP/UDP HDR-ERR\n");
				} else {
					match_rule.rule.ip.protocol = ISDN_TIMRU_IP_UDP;
					match_rule.rule.ip.pt.port.s_from = udp->source;
					match_rule.rule.ip.pt.port.d_from = udp->dest;

					if(dev->net_verbose >= 5) {
						printk("IP/UDP ");
						printk_ip(ip->saddr);
						printk("/");
						printk_port(udp->source);
						printk(" --> ");
						printk_ip(ip->daddr);
						printk("/");
						printk_port(udp->dest);
						printk("\n");
					}
				}
				break;

			case IPPROTO_IDP:
				match_rule.rule.ip.protocol = ISDN_TIMRU_IP_WILDCARD;
				VERBOSE_PRINTK(5, "", "IP/IDP\n");
				break;
	
			default:
				match_rule.rule.ip.protocol = ISDN_TIMRU_IP_WILDCARD;
				if(dev->net_verbose >= 5) {
					printk("IP/? (%x)\n", ip->protocol);
#ifdef ISDN_DEBUG_NET_DUMP
					isdn_dumppkt("R:", (u_char *)skb, skb->len, 180);
#endif
				}
				break;
			}
			break;

		case ETH_P_ARP:
			VERBOSE_PRINTK(5, "", "ARP/?\n");
			break;

		case ETH_P_IPX:
			VERBOSE_PRINTK(5, "", "IPX/?\n");
			break;

		case ETH_P_802_2:
			VERBOSE_PRINTK(5, "", "802.2/?\n");
			break;

		case ETH_P_802_3:
			VERBOSE_PRINTK(5, "", "802.3/?\n");
			break;

		default:
			if(dev->net_verbose >= 5) {
				printk("?/? (%x)\n", ntohs(skb->protocol));
#ifdef ISDN_DEBUG_NET_DUMP
				isdn_dumppkt("R:", (u_char *)skb, skb->len, 1800);
#endif
			}
			break;
		}

#ifdef CONFIG_ISDN_TIMRU_USE_MASQ
#ifdef CONFIG_IP_MASQUERADE
		check_for_masq = 0;
		m_saddr = m_daddr = (__u32)0;
		m_sport = m_dport = (__u16)0;
		m_prot = 0;

		switch(match_rule.protfam) {
		case ISDN_TIMRU_PROTFAM_IP:
			m_saddr = match_rule.rule.ip.saddr.s_addr;
			m_daddr = match_rule.rule.ip.daddr.s_addr;

			switch(match_rule.rule.ip.protocol) {
			case ISDN_TIMRU_IP_TCP:
				m_prot = IPPROTO_TCP;
				m_sport = match_rule.rule.ip.pt.port.s_from;
				m_dport = match_rule.rule.ip.pt.port.d_from;
				check_for_masq = 1;
				break;

			case ISDN_TIMRU_IP_UDP:
				m_prot = IPPROTO_UDP;
				m_sport = match_rule.rule.ip.pt.port.s_from;
				m_dport = match_rule.rule.ip.pt.port.d_from;
				check_for_masq = 1;
				break;

#if 0
#ifdef CONFIG_IP_MASQUERADE_ICMP
			case ISDN_TIMRU_IP_ICMP:
				m_sport = match_rule.rule.ip.pt.type.from;
				m_dport = 0;
				check_for_masq = 1;
				break;
#endif
#endif
			}
			break;
		}

		if(check_for_masq) {
			masq = NULL;

			switch(type) {
			case ISDN_TIMRU_BRINGUP:
			case ISDN_TIMRU_KEEPUP_OUT:
				if((masq = ip_masq_in_get_2(m_prot, m_daddr, m_dport, m_saddr, m_sport))) {
					match_rule.rule.ip.saddr.s_addr = m_saddr;
					match_rule.rule.ip.daddr.s_addr = m_daddr;
					switch(m_prot) {
					case IPPROTO_TCP:
					case IPPROTO_UDP:
						match_rule.rule.ip.pt.port.s_from = m_sport;
						match_rule.rule.ip.pt.port.d_from = m_dport;
						break;
					}
				}
				break;

			case ISDN_TIMRU_KEEPUP_IN:
				if((masq = ip_masq_in_get_2(m_prot, m_saddr, m_sport, m_daddr, m_dport))) {
					match_rule.rule.ip.saddr.s_addr = m_daddr;
					match_rule.rule.ip.daddr.s_addr = m_saddr;
					switch(m_prot) {
					case IPPROTO_TCP:
					case IPPROTO_UDP:
						match_rule.rule.ip.pt.port.s_from = m_sport;
						match_rule.rule.ip.pt.port.d_from = m_dport;
						break;
					}
				}
				break;
			}

			if(masq && dev->net_verbose >= 5) {
				printk(KERN_DEBUG "MASQ-TIMRU: ");
				printk_ip(masq->maddr);
				printk("/");
				printk_port(masq->mport);
				printk(": ");
				printk_ip(masq->saddr);
				printk("/");
				printk_port(masq->sport);
				printk(" --> ");
				printk_ip(masq->daddr);
				printk("/");
				printk_port(masq->dport);
				printk("\n");
			}
		}
#endif
#endif
		break;
	}

	new_timeout = lp->onhtime;

	if(prot && lp->timeout_rules) {
		isdn_timeout_rule	*head, *tor;
		int			pf, found_match, i;

		pf = match_rule.protfam;
		found_match = 0;

		while(1) {
			head = tor = lp->timeout_rules->timru[type][pf];
			i = 0;
			while(tor) {
				if((isdn_timru_match(&match_rule, tor) > 0) ^ (tor->neg > 0)) {
					found_match = 1;
					new_timeout = tor->timeout;
				}

				if(found_match) {
#ifdef DEBUG_RULES
					printk(KERN_DEBUG "Rule %d-%d-%d matches\n", type, pf, i);
#endif
					break;
				}

				if(tor->next == head)
					tor = NULL;
				else {
					tor = tor->next;
					i++;
				}
			}

			if(! found_match && pf != ISDN_TIMRU_PROTFAM_WILDCARD)
				pf = ISDN_TIMRU_PROTFAM_WILDCARD;
			else
				break;
		}

		if(! found_match) {
			new_timeout = lp->timeout_rules->defaults[type];
#ifdef DEBUG_RULES
			printk("No rule matches: using default\n");
#endif
		}
	}

	if(type == ISDN_TIMRU_BRINGUP) {
		if(new_timeout > 0) {
			lp->huptimeout = new_timeout;
			lp->huptimer = 0;
		}
	} else {
		if(new_timeout > lp->huptimeout
		|| lp->huptimeout - lp->huptimer < new_timeout) {
			lp->huptimeout = new_timeout;
			lp->huptimer = 0;
		}
	}

	return(new_timeout);
}


static int
isdn_timru_match(isdn_timeout_rule *this, isdn_timeout_rule *rule) {
	if(this->protfam != rule->protfam)
		return(0);

	switch(rule->protfam) {
	case ISDN_TIMRU_PROTFAM_WILDCARD:
		return(1);
		break;

	case ISDN_TIMRU_PROTFAM_PPP:
		if(rule->rule.ppp.protocol == ISDN_TIMRU_PPP_WILDCARD
		|| rule->rule.ppp.protocol == this->rule.ppp.protocol)
			return(1);
		break;

	case ISDN_TIMRU_PROTFAM_IP:
		if((this->rule.ip.saddr.s_addr & rule->rule.ip.smask.s_addr) != rule->rule.ip.saddr.s_addr
		|| (this->rule.ip.daddr.s_addr & rule->rule.ip.dmask.s_addr) != rule->rule.ip.daddr.s_addr)
			return(0);

		if(rule->rule.ip.protocol == ISDN_TIMRU_IP_WILDCARD)
			return(1);

		if(rule->rule.ip.protocol != this->rule.ip.protocol)
			return(0);

		switch(rule->rule.ip.protocol) {
		case ISDN_TIMRU_IP_ICMP:
			if(this->rule.ip.pt.type.from < rule->rule.ip.pt.type.from
			|| this->rule.ip.pt.type.from > rule->rule.ip.pt.type.to)
				return(0);
			break;

		case ISDN_TIMRU_IP_TCP:
		case ISDN_TIMRU_IP_UDP:
			if(this->rule.ip.pt.port.s_from < rule->rule.ip.pt.port.s_from
			|| this->rule.ip.pt.port.s_from > rule->rule.ip.pt.port.s_to)
				return(0);

			if(this->rule.ip.pt.port.d_from < rule->rule.ip.pt.port.d_from
			|| this->rule.ip.pt.port.d_from > rule->rule.ip.pt.port.d_to)
				return(0);

			break;
		}
		break;
	}

	return(1);
}


static int
isdn_timru_rule_equals(isdn_timeout_rule *this, isdn_timeout_rule *rule) {
	if(this->neg != rule->neg
	|| this->protfam != rule->protfam)
		return(0);

	switch(rule->protfam) {
	case ISDN_TIMRU_PROTFAM_PPP:
		if(this->rule.ppp.protocol != rule->rule.ppp.protocol)
			return(0);
		break;

	case ISDN_TIMRU_PROTFAM_IP:
		if(this->rule.ip.protocol != rule->rule.ip.protocol
		|| this->rule.ip.saddr.s_addr != rule->rule.ip.saddr.s_addr
		|| this->rule.ip.smask.s_addr != rule->rule.ip.smask.s_addr
		|| this->rule.ip.daddr.s_addr != rule->rule.ip.daddr.s_addr
		|| this->rule.ip.dmask.s_addr != rule->rule.ip.dmask.s_addr)
			return(0);

		switch(rule->rule.ip.protocol) {
		case ISDN_TIMRU_IP_ICMP:
			if(this->rule.ip.pt.type.from != rule->rule.ip.pt.type.from
			|| this->rule.ip.pt.type.to != rule->rule.ip.pt.type.to)
				return(0);
			break;

		case ISDN_TIMRU_IP_TCP:
		case ISDN_TIMRU_IP_UDP:
			if(this->rule.ip.pt.port.s_from != rule->rule.ip.pt.port.s_from
			|| this->rule.ip.pt.port.s_to != rule->rule.ip.pt.port.s_to
			|| this->rule.ip.pt.port.d_from != rule->rule.ip.pt.port.d_from
			|| this->rule.ip.pt.port.d_to != rule->rule.ip.pt.port.d_to)
				return(0);

			break;
		}
		break;
	}

	return(1);
}


int
isdn_timru_alloc_timeout_rules(struct device *ndev) {
	isdn_net_local	*lp = (isdn_net_local *)ndev->priv;
	int		i, j;
	ulong		flags;

	save_flags(flags);
	cli();
	if(!(lp->timeout_rules = (struct isdn_timeout_rules *)kmalloc(sizeof(struct isdn_timeout_rules), GFP_KERNEL))) {
		restore_flags(flags);
		printk(KERN_WARNING "isdn_timru: failed to allocate memory.\n");
		return(-ENOMEM);
	}

	memset((char *)lp->timeout_rules, 0, sizeof(struct isdn_timeout_rules));

	for(i = 0; i < ISDN_TIMRU_NUM_CHECK; i++) {
		lp->timeout_rules->defaults[i] = lp->onhtime;
		for(j = 0; j < ISDN_TIMRU_NUM_PROTFAM; j++)
			lp->timeout_rules->timru[i][j] = NULL;
	}

	restore_flags(flags);
	return(0);
}


int
isdn_timru_free_timeout_rules(struct device *ndev) {
	isdn_net_local		*lp = (isdn_net_local *)ndev->priv;
	isdn_timeout_rule	*head, *this, *next;
	int			i, j;
	ulong			flags;

	if(!lp->timeout_rules)
		return(-1);

	save_flags(flags);
	cli();
	for(i = 0; i < ISDN_TIMRU_NUM_CHECK; i++)
		for(j = 0; j < ISDN_TIMRU_NUM_CHECK; j++)
			if((head = lp->timeout_rules->timru[i][j])) {
				this = head;
				do {
					next = this->next;
					kfree(this);
				} while(next == head);
			}

	kfree(lp->timeout_rules);
	lp->timeout_rules = NULL;

	restore_flags(flags);
	return(0);
}


int
isdn_timru_add_rule(int where, struct device *ndev, isdn_timeout_rule *rule) {
	isdn_net_local		*lp = (isdn_net_local *)ndev->priv;
	isdn_timeout_rule	**head;
	ulong			flags;
	int			ret;

	if(!lp->timeout_rules)
		if((ret = isdn_timru_alloc_timeout_rules(ndev)))
			return(ret);

	if(rule->timeout < 0)
		return(-EINVAL);

	save_flags(flags);
	cli();

	head = &(lp->timeout_rules->timru[rule->type][rule->protfam]);

	if(! *head)
		rule->next = rule->prev = *head = rule;
	else {
		rule->next = *head;
		rule->prev = (*head)->prev;
		(*head)->prev->next = rule;
		(*head)->prev = rule;

		if(where == 0)	/* add to head of chain */
			*head = rule;
	}

	restore_flags(flags);
	return(0);
}


int
isdn_timru_del_rule(struct device *ndev, isdn_timeout_rule *rule) {
	isdn_net_local			*lp = (isdn_net_local *)ndev->priv;
	isdn_timeout_rule	**head, *this;
	ulong				flags;

	if(!lp->timeout_rules)
		return(-EINVAL);

	save_flags(flags);
	cli();

	head = &(lp->timeout_rules->timru[rule->type][rule->protfam]);

	if(! *head) {
		restore_flags(flags);
		return(-EINVAL);
	}

	this = *head;
	do {
		if(isdn_timru_rule_equals(this, rule)) {
			if(this->next != this) {	/* more than one rule */
				this->prev->next = this->next;
				this->next->prev = this->prev;

				if(this == *head)
					*head = this->next;
			} else
				*head = NULL;

			kfree(this);
			restore_flags(flags);
			return(0);
		} else
			this = this->next;
	} while(this == *head);

	restore_flags(flags);
	return(-EINVAL);
}


int
isdn_timru_set_default(int type, struct device *ndev, int def) {
	isdn_net_local	*lp = (isdn_net_local *)ndev->priv;
	ulong		flags;
	int		ret;

	if(!lp->timeout_rules)
		if((ret = isdn_timru_alloc_timeout_rules(ndev)))
			return(ret);

	if(def < 0)
		return(-EINVAL);

	save_flags(flags);
	cli();

	lp->timeout_rules->defaults[type] = def;

	restore_flags(flags);
	return(0);
}


int
isdn_timru_get_rule(struct device *ndev, isdn_timeout_rule **rule, int i, int j, int k) {
	isdn_net_local			*lp = (isdn_net_local *)ndev->priv;
	isdn_timeout_rule	*head, *this;
	int				l;
	ulong				flags;

	if(!lp->timeout_rules
	|| i < 0 || i > ISDN_TIMRU_NUM_CHECK
	|| j < 0 || j > ISDN_TIMRU_NUM_PROTFAM
	|| k < 0)
		return(-EINVAL);

	save_flags(flags);
	cli();

	if(!(this = head = lp->timeout_rules->timru[i][j])) {
		restore_flags(flags);
		return(-EINVAL);
	}

	for(l = 0; l < k; l++) {
		if(this->next == head) {
			restore_flags(flags);
			return(-EINVAL);
		}
		this = this->next;
	}

	*rule = this;
	restore_flags(flags);
	return(0);
}


int
isdn_timru_get_default(int type, struct device *ndev, int *ret) {
	isdn_net_local	*lp = (isdn_net_local *)ndev->priv;
	ulong		flags;

	if(!lp->timeout_rules)
		return(-EINVAL);

	save_flags(flags);
	cli();

	*ret = lp->timeout_rules->defaults[type];

	restore_flags(flags);
	return(0);
}


int
isdn_timru_ioctl_add_rule(isdn_ioctl_timeout_rule *iorule)
{
	isdn_net_dev		*p = isdn_net_findif(iorule->name);
	isdn_timeout_rule	*r;

	if(p) {
		if(iorule->where < 0) {	/* set default */
			return(isdn_timru_set_default(iorule->type, &p->dev, iorule->defval));
		} else {
			if(!(r = (isdn_timeout_rule *) kmalloc(sizeof(isdn_timeout_rule), GFP_KERNEL)))
				return(-ENOMEM);
			memcpy((char *)r, (char *)&iorule->rule, sizeof(isdn_timeout_rule));
			return(isdn_timru_add_rule(iorule->where, &p->dev, r));
		}
	}
	return(-ENODEV);
}


int
isdn_timru_ioctl_del_rule(isdn_ioctl_timeout_rule *iorule)
{
	isdn_net_dev		*p = isdn_net_findif(iorule->name);
	isdn_timeout_rule	*r;

	if(p) {
		if(!(r = (isdn_timeout_rule *) kmalloc(sizeof(isdn_timeout_rule), GFP_KERNEL)))
			return(-ENOMEM);
		memcpy((char *)r, (char *)&iorule->rule, sizeof(isdn_timeout_rule));
		return(isdn_timru_del_rule(&p->dev, r));
	}
	return(-ENODEV);
}


int
isdn_timru_ioctl_get_rule(isdn_ioctl_timeout_rule *iorule)
{
	int			ret, def;
	isdn_net_dev		*p = isdn_net_findif(iorule->name);
	isdn_timeout_rule	*r;

	if(p) {
		if(iorule->where < 0) {	/* get default */
			if((ret = isdn_timru_get_default(iorule->type, &p->dev, &def)) < 0)
				return(ret);

			iorule->protfam = p->local->huptimer;
			iorule->index = p->local->huptimeout;
			iorule->defval = def;
		} else {
			if(isdn_timru_get_rule(&p->dev, &r, iorule->type, iorule->protfam, iorule->index))
				return(-ENOMEM);

			memcpy((char *)&iorule->rule, (char *)r, sizeof(isdn_timeout_rule));
		}
		return(0);
	}
	return(-ENODEV);
}

#endif

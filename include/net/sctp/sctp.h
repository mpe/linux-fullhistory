/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * The base lksctp header. 
 * 
 * The SCTP reference implementation is free software; 
 * you can redistribute it and/or modify it under the terms of 
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The SCTP reference implementation is distributed in the hope that it 
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 * 
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 * 
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by: 
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#ifndef __net_sctp_h__
#define __net_sctp_h__

/* Header Strategy.
 *    Start getting some control over the header file depencies:
 *       includes
 *       constants
 *       structs
 *       prototypes
 *       macros, externs, and inlines
 * 
 *   Move test_frame specific items out of the kernel headers 
 *   and into the test frame headers.   This is not perfect in any sense
 *   and will continue to evolve.  
 */


#include <linux/config.h>

#ifdef TEST_FRAME
#undef CONFIG_PROC_FS
#undef CONFIG_SCTP_DBG_OBJCNT
#undef CONFIG_SYSCTL
#endif /* TEST_FRAME */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#include <net/ipv6.h>
#include <net/ip6_route.h>
#endif 

#include <asm/uaccess.h>
#include <asm/page.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <net/sctp/structs.h>
#include <net/sctp/constants.h>
#include <net/sctp/sm.h>


/* Set SCTP_DEBUG flag via config if not already set. */
#ifndef SCTP_DEBUG
#ifdef CONFIG_SCTP_DBG_MSG
#define SCTP_DEBUG	1
#else
#define SCTP_DEBUG      0
#endif /* CONFIG_SCTP_DBG */
#endif /* SCTP_DEBUG */

#ifdef CONFIG_IP_SCTP_MODULE
#define SCTP_PROTOSW_FLAG 0
#else /* static! */
#define SCTP_PROTOSW_FLAG INET_PROTOSW_PERMANENT
#endif


/* Certain internal static functions need to be exported when 
 * compiled into the test frame.
 */
#ifndef SCTP_STATIC
#define SCTP_STATIC static
#endif

/* 
 * Function declarations. 
 */

/*
 * sctp_protocol.c 
 */
extern sctp_protocol_t sctp_proto;
extern struct sock *sctp_get_ctl_sock(void);
extern int sctp_copy_local_addr_list(sctp_protocol_t *, sctp_bind_addr_t *,
				     sctp_scope_t, int priority, int flags);
extern sctp_pf_t *sctp_get_pf_specific(int family);
extern void sctp_set_pf_specific(int family, sctp_pf_t *);

/*
 * sctp_socket.c
 */
extern int sctp_backlog_rcv(struct sock *sk, struct sk_buff *skb);
extern int sctp_inet_listen(struct socket *sock, int backlog);
extern void sctp_write_space(struct sock *sk);
extern unsigned int sctp_poll(struct file *file, struct socket *sock,
		poll_table *wait);

/*
 * sctp_primitive.c
 */
extern int sctp_primitive_ASSOCIATE(sctp_association_t *, void *arg);
extern int sctp_primitive_SHUTDOWN(sctp_association_t *, void *arg);
extern int sctp_primitive_ABORT(sctp_association_t *, void *arg);
extern int sctp_primitive_SEND(sctp_association_t *, void *arg);


/*
 * sctp_crc32c.c
 */
extern __u32 count_crc(__u8 *ptr, __u16 count);

/*
 * sctp_input.c
 */
extern int sctp_rcv(struct sk_buff *skb);
extern void sctp_v4_err(struct sk_buff *skb, u32 info);
extern void sctp_hash_established(sctp_association_t *);
extern void __sctp_hash_established(sctp_association_t *);
extern void sctp_unhash_established(sctp_association_t *);
extern void __sctp_unhash_established(sctp_association_t *);
extern void sctp_hash_endpoint(sctp_endpoint_t *);
extern void __sctp_hash_endpoint(sctp_endpoint_t *);
extern void sctp_unhash_endpoint(sctp_endpoint_t *);
extern void __sctp_unhash_endpoint(sctp_endpoint_t *);

/*
 * sctp_hashdriver.c
 */
extern void sctp_hash_digest(const char *secret, const int secret_len,
			     const char *text, const int text_len,
			     __u8 *digest);

/*
 *  Section:  Macros, externs, and inlines
 */


#ifdef TEST_FRAME

#include <test_frame.h>

#else

/* spin lock wrappers. */
#define sctp_spin_lock_irqsave(lock, flags) spin_lock_irqsave(lock, flags)
#define sctp_spin_unlock_irqrestore(lock, flags)  \
       spin_unlock_irqrestore(lock, flags)
#define sctp_local_bh_disable() local_bh_disable()
#define sctp_local_bh_enable() local_bh_enable()
#define sctp_spin_lock(lock) spin_lock(lock)
#define sctp_spin_unlock(lock) spin_unlock(lock)
#define sctp_write_lock(lock) write_lock(lock)
#define sctp_write_unlock(lock) write_unlock(lock)
#define sctp_read_lock(lock) read_lock(lock)
#define sctp_read_unlock(lock) read_unlock(lock)

/* sock lock wrappers. */
#define sctp_lock_sock(sk) lock_sock(sk)
#define sctp_release_sock(sk) release_sock(sk)
#define sctp_bh_lock_sock(sk) bh_lock_sock(sk)
#define sctp_bh_unlock_sock(sk) bh_unlock_sock(sk)
#define SCTP_SOCK_SLEEP_PRE(sk) SOCK_SLEEP_PRE(sk)
#define SCTP_SOCK_SLEEP_POST(sk) SOCK_SLEEP_POST(sk)

/* SCTP SNMP MIB stats handlers */
extern struct sctp_mib sctp_statistics[NR_CPUS * 2];
#define SCTP_INC_STATS(field)		SNMP_INC_STATS(sctp_statistics, field)
#define SCTP_INC_STATS_BH(field)	SNMP_INC_STATS_BH(sctp_statistics, field)
#define SCTP_INC_STATS_USER(field)	SNMP_INC_STATS_USER(sctp_statistics, field)

/* Determine if this is a valid kernel address.  */
static inline int sctp_is_valid_kaddr(unsigned long addr)
{
	struct page *page;

	/* Make sure the address is not in the user address space. */
	if (addr < PAGE_OFFSET)
		return 0;

	page = virt_to_page(addr);

	/* Is this page valid? */
	if (!virt_addr_valid(addr) || PageReserved(page))
		return 0;

	return 1;
}

#endif /* !TEST_FRAME */


/* Print debugging messages.  */
#if SCTP_DEBUG
extern int sctp_debug_flag;
#define SCTP_DEBUG_PRINTK(whatever...) \
	((void) (sctp_debug_flag && printk(KERN_DEBUG whatever)))
#define SCTP_ENABLE_DEBUG { sctp_debug_flag = 1; }
#define SCTP_DISABLE_DEBUG { sctp_debug_flag = 0; }

#define SCTP_ASSERT(expr, str, func) \
	if (!(expr)) { \
		SCTP_DEBUG_PRINTK("Assertion Failed: %s(%s) at %s:%s:%d\n", \
			str, (#expr), __FILE__, __FUNCTION__, __LINE__); \
		func; \
	}

#else	/* SCTP_DEBUG */

#define SCTP_DEBUG_PRINTK(whatever...)
#define SCTP_ENABLE_DEBUG
#define SCTP_DISABLE_DEBUG
#define SCTP_ASSERT(expr, str, func)

#endif /* SCTP_DEBUG */


/*
 * Macros for keeping a global reference of object allocations.
 */
#ifdef CONFIG_SCTP_DBG_OBJCNT

extern atomic_t sctp_dbg_objcnt_sock;
extern atomic_t sctp_dbg_objcnt_ep;
extern atomic_t sctp_dbg_objcnt_assoc;
extern atomic_t sctp_dbg_objcnt_transport;
extern atomic_t sctp_dbg_objcnt_chunk;
extern atomic_t sctp_dbg_objcnt_bind_addr;
extern atomic_t sctp_dbg_objcnt_addr;

/* Macros to atomically increment/decrement objcnt counters.  */
#define SCTP_DBG_OBJCNT_INC(name) \
atomic_inc(&sctp_dbg_objcnt_## name)
#define SCTP_DBG_OBJCNT_DEC(name) \
atomic_dec(&sctp_dbg_objcnt_## name)
#define SCTP_DBG_OBJCNT(name) \
atomic_t sctp_dbg_objcnt_## name = ATOMIC_INIT(0)

/* Macro to help create new entries in in the global array of
 * objcnt counters.
 */
#define SCTP_DBG_OBJCNT_ENTRY(name) \
{.label= #name, .counter= &sctp_dbg_objcnt_## name}

extern void sctp_dbg_objcnt_init(void);
extern void sctp_dbg_objcnt_exit(void);

#else

#define SCTP_DBG_OBJCNT_INC(name)
#define SCTP_DBG_OBJCNT_DEC(name)

static inline void sctp_dbg_objcnt_init(void) { return; }
static inline void sctp_dbg_objcnt_exit(void) { return; }

#endif /* CONFIG_SCTP_DBG_OBJCOUNT */

#if defined CONFIG_SYSCTL
extern void sctp_sysctl_register(void);
extern void sctp_sysctl_unregister(void);
#else
static inline void sctp_sysctl_register(void) { return; }
static inline void sctp_sysctl_unregister(void) { return; }
#endif


#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

extern int sctp_v6_init(void);
extern void sctp_v6_exit(void);

static inline int sctp_ipv6_addr_type(const struct in6_addr *addr)
{
	return ipv6_addr_type((struct in6_addr*) addr);
}

#define SCTP_SAT_LEN (sizeof(sctp_paramhdr_t) + 2 * sizeof(__u16))

/* Note: These V6 macros are obsolescent.  */
/* Use this macro to enclose code fragments which are V6-dependent. */
#define SCTP_V6(m...)	m
#define SCTP_V6_SUPPORT 1

#else /* #ifdef defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE) */

#define sctp_ipv6_addr_type(a) 0
#define SCTP_SAT_LEN (sizeof(sctp_paramhdr_t) + 1 * sizeof(__u16))
#define SCTP_V6(m...) /* Do nothing. */
#undef SCTP_V6_SUPPORT

static inline int sctp_v6_init(void) { return 0; }
static inline void sctp_v6_exit(void) { return; }

#endif /* #ifdef defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE) */


/* Map an association to an assoc_id. */
static inline sctp_assoc_t sctp_assoc2id(const sctp_association_t *asoc)
{
	return (sctp_assoc_t) asoc;
}

/* Look up the association by its id.  */
static inline sctp_association_t *sctp_id2assoc(const struct sock *sk, sctp_assoc_t id)
{
	sctp_association_t *asoc = NULL;

	/* First, verify that this is a kernel address. */
	if (sctp_is_valid_kaddr((unsigned long) id)) {
		sctp_association_t *temp = (sctp_association_t *) id;

		/* Verify that this _is_ an sctp_association_t
		 * data structure and if so, that the socket matches.
		 */
		if ((SCTP_ASSOC_EYECATCHER == temp->eyecatcher) &&
		    (temp->base.sk == sk))
			asoc = temp;
	}

	return asoc;
}

/* A macro to walk a list of skbs.  */
#define sctp_skb_for_each(pos, head, tmp) \
for (pos = (head)->next;\
     tmp = (pos)->next, pos != ((struct sk_buff *)(head));\
     pos = tmp)


/* A helper to append an entire skb list (list) to another (head). */
static inline void sctp_skb_list_tail(struct sk_buff_head *list,
				      struct sk_buff_head *head)
{
	int flags __attribute__ ((unused));

	sctp_spin_lock_irqsave(&head->lock, flags);
	sctp_spin_lock(&list->lock);

	list_splice((struct list_head *)list, (struct list_head *)head->prev);

	head->qlen += list->qlen;
	list->qlen = 0;

	sctp_spin_unlock(&list->lock);
	sctp_spin_unlock_irqrestore(&head->lock, flags);
}

/**
 *	sctp_list_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. The head item is
 *	returned or %NULL if the list is empty.
 */

static inline struct list_head *sctp_list_dequeue(struct list_head *list)
{
	struct list_head *result = NULL;

	if (list->next != list) {
		result = list->next;
		list->next = result->next;
		list->next->prev = list;
		INIT_LIST_HEAD(result);
	}
	return result;
}

/* Calculate the size (in bytes) occupied by the data of an iovec.  */
static inline size_t get_user_iov_size(struct iovec *iov, int iovlen)
{
	size_t retval = 0;

	for (; iovlen > 0; --iovlen) {
		retval += iov->iov_len;
		iov++;
	}

	return retval;
}


/* Round an int up to the next multiple of 4.  */
#define WORD_ROUND(s) (((s)+3)&~3)

/* Make a new instance of type.  */
#define t_new(type, flags)	(type *)kmalloc(sizeof(type), flags)

/* Compare two timevals.  */
#define tv_lt(s, t) \
   (s.tv_sec < t.tv_sec || (s.tv_sec == t.tv_sec && s.tv_usec < t.tv_usec))

/* Stolen from net/profile.h.  Using it from there is more grief than
 * it is worth.
 */
static inline void tv_add(const struct timeval *entered, struct timeval *leaved)
{
	time_t usecs = leaved->tv_usec + entered->tv_usec;
	time_t secs = leaved->tv_sec + entered->tv_sec;

	if (usecs >= 1000000) {
		usecs -= 1000000;
		secs++;
	}
	leaved->tv_sec = secs;
	leaved->tv_usec = usecs;
}


/* External references. */

extern struct proto sctp_prot;
extern struct proc_dir_entry *proc_net_sctp;
extern void sctp_put_port(struct sock *sk);

/* Static inline functions. */

/* Return the SCTP protocol structure. */
static inline sctp_protocol_t *sctp_get_protocol(void)
{
	return &sctp_proto;
}

/* Warning: The following hash functions assume a power of two 'size'. */
/* This is the hash function for the SCTP port hash table. */
static inline int sctp_phashfn(__u16 lport)
{
	sctp_protocol_t *sctp_proto = sctp_get_protocol();
	return (lport & (sctp_proto->port_hashsize - 1));
}

/* This is the hash function for the endpoint hash table. */
static inline int sctp_ep_hashfn(__u16 lport)
{
	sctp_protocol_t *sctp_proto = sctp_get_protocol();
	return (lport & (sctp_proto->ep_hashsize - 1));
}

/* This is the hash function for the association hash table. */
static inline int sctp_assoc_hashfn(__u16 lport, __u16 rport)
{
	sctp_protocol_t *sctp_proto = sctp_get_protocol();
	int h = (lport << 16) + rport;
	h ^= h>>8;
	return (h & (sctp_proto->assoc_hashsize - 1));
}

/* This is the hash function for the association hash table.  This is
 * not used yet, but could be used as a better hash function when
 * we have a vtag.
 */
static inline int sctp_vtag_hashfn(__u16 lport, __u16 rport, __u32 vtag)
{
	sctp_protocol_t *sctp_proto = sctp_get_protocol();
	int h = (lport << 16) + rport;
	h ^= vtag;
	return (h & (sctp_proto->assoc_hashsize-1));
}

/* WARNING: Do not change the layout of the members in sctp_sock! */
struct sctp_sock {
	struct sock	  sk;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct ipv6_pinfo *pinet6;
#endif /* CONFIG_IPV6 || CONFIG_IPV6_MODULE */
	struct inet_opt	  inet;
	struct sctp_opt	  sctp;
};

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
struct sctp6_sock {
	struct sock	  sk;
	struct ipv6_pinfo *pinet6;
	struct inet_opt	  inet;
	struct sctp_opt	  sctp;
	struct ipv6_pinfo inet6;
};
#endif /* CONFIG_IPV6 || CONFIG_IPV6_MODULE */

#define sctp_sk(__sk) (&((struct sctp_sock *)__sk)->sctp)

#endif /* __net_sctp_h__ */

/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */

/* (c) 1999 Paul `Rusty' Russell.  Licenced under the GNU General
   Public Licence. */

#ifdef MODULE
#define __NO_VERSION__
#endif
#include <linux/version.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/brlock.h>
#include <net/checksum.h>
#include <linux/stddef.h>
#include <linux/sysctl.h>

/* This rwlock protects the main hash table, protocol/helper/expected
   registrations, conntrack timers*/
#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_conntrack_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_conntrack_lock)

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DECLARE_RWLOCK(ip_conntrack_lock);

void (*ip_conntrack_destroyed)(struct ip_conntrack *conntrack) = NULL;
static LIST_HEAD(expect_list);
static LIST_HEAD(protocol_list);
static LIST_HEAD(helpers);
unsigned int ip_conntrack_htable_size = 0;
static int ip_conntrack_max = 0;
static atomic_t ip_conntrack_count = ATOMIC_INIT(0);
struct list_head *ip_conntrack_hash;

extern struct ip_conntrack_protocol ip_conntrack_generic_protocol;

static inline int proto_cmpfn(const struct ip_conntrack_protocol *curr,
			      u_int8_t protocol)
{
	return protocol == curr->proto;
}

struct ip_conntrack_protocol *__find_proto(u_int8_t protocol)
{
	struct ip_conntrack_protocol *p;

	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	p = LIST_FIND(&protocol_list, proto_cmpfn,
		      struct ip_conntrack_protocol *, protocol);
	if (!p)
		p = &ip_conntrack_generic_protocol;

	return p;
}

struct ip_conntrack_protocol *find_proto(u_int8_t protocol)
{
	struct ip_conntrack_protocol *p;

	READ_LOCK(&ip_conntrack_lock);
	p = __find_proto(protocol);
	READ_UNLOCK(&ip_conntrack_lock);
	return p;
}

static inline void ip_conntrack_put(struct ip_conntrack *ct)
{
	IP_NF_ASSERT(ct);
	IP_NF_ASSERT(ct->infos[0].master);
	/* nf_conntrack_put wants to go via an info struct, so feed it
           one at random. */
	nf_conntrack_put(&ct->infos[0]);
}

static inline u_int32_t
hash_conntrack(const struct ip_conntrack_tuple *tuple)
{
#if 0
	dump_tuple(tuple);
#endif
#ifdef CONFIG_NETFILTER_DEBUG
	if (tuple->src.pad)
		DEBUGP("Tuple %p has non-zero padding.\n", tuple);
#endif
	/* ntohl because more differences in low bits. */
	/* To ensure that halves of the same connection don't hash
	   clash, we add the source per-proto again. */
	return (ntohl(tuple->src.ip + tuple->dst.ip
		     + tuple->src.u.all + tuple->dst.u.all
		     + tuple->dst.protonum)
		+ ntohs(tuple->src.u.all))
		% ip_conntrack_htable_size;
}

inline int
get_tuple(const struct iphdr *iph, size_t len,
	  struct ip_conntrack_tuple *tuple,
	  struct ip_conntrack_protocol *protocol)
{
	int ret;

	/* Can only happen when extracting tuples from inside ICMP
           packets */
	if (iph->frag_off & htons(IP_OFFSET)) {
		if (net_ratelimit())
			printk("ip_conntrack_core: Frag of proto %u.\n",
			       iph->protocol);
		return 0;
	}
	/* Guarantee 8 protocol bytes: if more wanted, use len param */
	else if (iph->ihl * 4 + 8 > len)
		return 0;

	tuple->src.ip = iph->saddr;
	tuple->src.pad = 0;
	tuple->dst.ip = iph->daddr;
	tuple->dst.protonum = iph->protocol;

	ret = protocol->pkt_to_tuple((u_int32_t *)iph + iph->ihl,
				     len - 4*iph->ihl,
				     tuple);
	return ret;
}

static int
invert_tuple(struct ip_conntrack_tuple *inverse,
	     const struct ip_conntrack_tuple *orig,
	     const struct ip_conntrack_protocol *protocol)
{
	inverse->src.ip = orig->dst.ip;
	inverse->src.pad = 0;
	inverse->dst.ip = orig->src.ip;
	inverse->dst.protonum = orig->dst.protonum;

	return protocol->invert_tuple(inverse, orig);
}

static void
destroy_conntrack(struct nf_conntrack *nfct)
{
	struct ip_conntrack *ct = (struct ip_conntrack *)nfct;

	IP_NF_ASSERT(atomic_read(&nfct->use) == 0);
	IP_NF_ASSERT(!timer_pending(&ct->timeout));

	if (ct->master.master)
		nf_conntrack_put(&ct->master);

	if (ip_conntrack_destroyed)
		ip_conntrack_destroyed(ct);
	kfree(ct);
	atomic_dec(&ip_conntrack_count);
}

static void death_by_timeout(unsigned long ul_conntrack)
{
	struct ip_conntrack *ct = (void *)ul_conntrack;

	WRITE_LOCK(&ip_conntrack_lock);
	/* Remove from both hash lists */
	LIST_DELETE(&ip_conntrack_hash
		    [hash_conntrack(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple)],
		    &ct->tuplehash[IP_CT_DIR_ORIGINAL]);
	LIST_DELETE(&ip_conntrack_hash
		    [hash_conntrack(&ct->tuplehash[IP_CT_DIR_REPLY].tuple)],
		    &ct->tuplehash[IP_CT_DIR_REPLY]);
	/* If our expected is in the list, take it out. */
	if (ct->expected.expectant) {
		IP_NF_ASSERT(list_inlist(&expect_list, &ct->expected));
		IP_NF_ASSERT(ct->expected.expectant == ct);
		LIST_DELETE(&expect_list, &ct->expected);
	}
	WRITE_UNLOCK(&ip_conntrack_lock);
	ip_conntrack_put(ct);
}

static inline int
conntrack_tuple_cmp(const struct ip_conntrack_tuple_hash *i,
		    const struct ip_conntrack_tuple *tuple,
		    const struct ip_conntrack *ignored_conntrack)
{
	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	return i->ctrack != ignored_conntrack
		&& memcmp(tuple, &i->tuple, sizeof(*tuple)) == 0;
}

static struct ip_conntrack_tuple_hash *
__ip_conntrack_find(const struct ip_conntrack_tuple *tuple,
		    const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;

	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	h = LIST_FIND(&ip_conntrack_hash[hash_conntrack(tuple)],
		      conntrack_tuple_cmp,
		      struct ip_conntrack_tuple_hash *,
		      tuple, ignored_conntrack);
	return h;
}

/* Find a connection corresponding to a tuple. */
struct ip_conntrack_tuple_hash *
ip_conntrack_find_get(const struct ip_conntrack_tuple *tuple,
		      const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;

	READ_LOCK(&ip_conntrack_lock);
	h = __ip_conntrack_find(tuple, ignored_conntrack);
	if (h)
		atomic_inc(&h->ctrack->ct_general.use);
	READ_UNLOCK(&ip_conntrack_lock);

	return h;
}

/* Returns true if a connection correspondings to the tuple (required
   for NAT). */
int
ip_conntrack_tuple_taken(const struct ip_conntrack_tuple *tuple,
			 const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;

	READ_LOCK(&ip_conntrack_lock);
	h = __ip_conntrack_find(tuple, ignored_conntrack);
	READ_UNLOCK(&ip_conntrack_lock);

	return h != NULL;
}

/* Returns TRUE if it dealt with ICMP, and filled in skb fields */
int icmp_error_track(struct sk_buff *skb)
{
	const struct iphdr *iph = skb->nh.iph;
	struct icmphdr *hdr = (struct icmphdr *)((u_int32_t *)iph + iph->ihl);
	struct ip_conntrack_tuple innertuple, origtuple;
	struct iphdr *inner = (struct iphdr *)(hdr + 1);
	size_t datalen = skb->len - iph->ihl*4 - sizeof(*hdr);
	struct ip_conntrack_protocol *innerproto;
	struct ip_conntrack_tuple_hash *h;
	enum ip_conntrack_info ctinfo;

	if (iph->protocol != IPPROTO_ICMP)
		return 0;

	if (skb->len < iph->ihl * 4 + sizeof(struct icmphdr)) {
		DEBUGP("icmp_error_track: too short\n");
		return 1;
	}

	if (hdr->type != ICMP_DEST_UNREACH
	    && hdr->type != ICMP_SOURCE_QUENCH
	    && hdr->type != ICMP_TIME_EXCEEDED
	    && hdr->type != ICMP_PARAMETERPROB
	    && hdr->type != ICMP_REDIRECT)
		return 0;

	/* Ignore it if the checksum's bogus. */
	if (ip_compute_csum((unsigned char *)hdr, sizeof(*hdr) + datalen)) {
		DEBUGP("icmp_error_track: bad csum\n");
		return 1;
	}

	innerproto = find_proto(inner->protocol);
	/* Are they talking about one of our connections? */
	if (inner->ihl * 4 + 8 > datalen
	    || !get_tuple(inner, datalen, &origtuple, innerproto)) {
		DEBUGP("icmp_error: ! get_tuple p=%u (%u*4+%u dlen=%u)\n",
		       inner->protocol, inner->ihl, 8,
		       datalen);
		return 1;
	}

	/* Ordinarily, we'd expect the inverted tupleproto, but it's
	   been preserved inside the ICMP. */
	if (!invert_tuple(&innertuple, &origtuple, innerproto)) {
		DEBUGP("icmp_error_track: Can't invert tuple\n");
		return 1;
	}
	h = ip_conntrack_find_get(&innertuple, NULL);
	if (!h) {
		DEBUGP("icmp_error_track: no match\n");
		return 1;
	}

	ctinfo = IP_CT_RELATED;
	if (DIRECTION(h) == IP_CT_DIR_REPLY)
		ctinfo += IP_CT_IS_REPLY;

	/* Update skb to refer to this connection */
	skb->nfct = &h->ctrack->infos[ctinfo];
	return 1;
}

static inline int helper_cmp(const struct ip_conntrack_helper *i,
			     const struct ip_conntrack_tuple *rtuple)
{
	return i->will_help(rtuple);
}

/* Compare all but src per-proto part. */
static int expect_cmp(const struct ip_conntrack_expect *i,
		      const struct ip_conntrack_tuple *tuple)
{
	return (tuple->src.ip == i->tuple.src.ip
		&& tuple->dst.ip == i->tuple.dst.ip
		&& tuple->dst.u.all == i->tuple.dst.u.all
		&& tuple->dst.protonum == i->tuple.dst.protonum);
}

/* Allocate a new conntrack; we set everything up, then grab write
   lock and see if we lost a race.  If we lost it we return 0,
   indicating the controlling code should look again. */
static int
init_conntrack(const struct ip_conntrack_tuple *tuple,
	       struct ip_conntrack_protocol *protocol,
	       struct sk_buff *skb)
{
	struct ip_conntrack *conntrack;
	struct ip_conntrack_tuple repl_tuple;
	size_t hash, repl_hash;
	struct ip_conntrack_expect *expected;
	enum ip_conntrack_info ctinfo;
	int i;

	if (!invert_tuple(&repl_tuple, tuple, protocol)) {
		DEBUGP("Can't invert tuple.\n");
		return 1;
	}

	if(ip_conntrack_max &&
	    (atomic_read(&ip_conntrack_count) >= ip_conntrack_max)) {
		if (net_ratelimit())
			printk(KERN_WARNING "ip_conntrack: maximum limit of %d entries exceeded\n", ip_conntrack_max);
		return 1;
	}

	conntrack = kmalloc(sizeof(struct ip_conntrack), GFP_ATOMIC);
	if (!conntrack) {
		DEBUGP("Can't allocate conntrack.\n");
		return 1;
	}
	hash = hash_conntrack(tuple);
	repl_hash = hash_conntrack(&repl_tuple);

	memset(conntrack, 0, sizeof(struct ip_conntrack));
	atomic_set(&conntrack->ct_general.use, 1);
	conntrack->ct_general.destroy = destroy_conntrack;
	conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple = *tuple;
	conntrack->tuplehash[IP_CT_DIR_ORIGINAL].ctrack = conntrack;
	conntrack->tuplehash[IP_CT_DIR_REPLY].tuple = repl_tuple;
	conntrack->tuplehash[IP_CT_DIR_REPLY].ctrack = conntrack;
	for(i=0; i < IP_CT_NUMBER; i++)
		conntrack->infos[i].master = &conntrack->ct_general;

	if (!protocol->new(conntrack, skb->nh.iph, skb->len)) {
		kfree(conntrack);
		return 1;
	}

	/* Sew in at head of hash list. */
	WRITE_LOCK(&ip_conntrack_lock);
	/* Check noone else beat us in the race... */
	if (__ip_conntrack_find(tuple, NULL)) {
		WRITE_UNLOCK(&ip_conntrack_lock);
		printk("ip_conntrack: Wow someone raced us!\n");
		kfree(conntrack);
		return 0;
	}
	conntrack->helper = LIST_FIND(&helpers, helper_cmp,
				      struct ip_conntrack_helper *,
				      &repl_tuple);
	/* Need finding and deleting of expected ONLY if we win race */
	expected = LIST_FIND(&expect_list, expect_cmp,
			     struct ip_conntrack_expect *, tuple);
	if (expected) {
		/* Welcome, Mr. Bond.  We've been expecting you... */
		conntrack->status = IPS_EXPECTED;
		conntrack->master.master = &expected->expectant->ct_general;
		IP_NF_ASSERT(conntrack->master.master);
		LIST_DELETE(&expect_list, expected);
		expected->expectant = NULL;
		nf_conntrack_get(&conntrack->master);
		ctinfo = IP_CT_RELATED;
	} else {
		ctinfo = IP_CT_NEW;
	}
	list_prepend(&ip_conntrack_hash[hash],
		     &conntrack->tuplehash[IP_CT_DIR_ORIGINAL]);
	list_prepend(&ip_conntrack_hash[repl_hash],
		     &conntrack->tuplehash[IP_CT_DIR_REPLY]);
	WRITE_UNLOCK(&ip_conntrack_lock);

	/* Update skb to refer to this connection */
	skb->nfct = &conntrack->infos[ctinfo];

	atomic_inc(&ip_conntrack_count);
	return 1;
}

static void
resolve_normal_ct(struct sk_buff *skb)
{
	struct ip_conntrack_tuple tuple;
	struct ip_conntrack_tuple_hash *h;
	struct ip_conntrack_protocol *proto;
	enum ip_conntrack_info ctinfo;

	proto = find_proto(skb->nh.iph->protocol);
	if (!get_tuple(skb->nh.iph, skb->len, &tuple, proto))
		return;

	/* Loop around search/insert race */
	do {
		/* look for tuple match */
		h = ip_conntrack_find_get(&tuple, NULL);
		if (!h && init_conntrack(&tuple, proto, skb))
			return;
	} while (!h);

	/* It exists; we have (non-exclusive) reference. */
	if (DIRECTION(h) == IP_CT_DIR_REPLY) {
		ctinfo = IP_CT_ESTABLISHED + IP_CT_IS_REPLY;
		h->ctrack->status |= IPS_SEEN_REPLY;
	} else {
		/* Once we've had two way comms, always ESTABLISHED. */
		if (h->ctrack->status & IPS_SEEN_REPLY) {
			DEBUGP("ip_conntrack_in: normal packet for %p\n",
			       h->ctrack);
		        ctinfo = IP_CT_ESTABLISHED;
		} else if (h->ctrack->status & IPS_EXPECTED) {
			DEBUGP("ip_conntrack_in: related packet for %p\n",
			       h->ctrack);
			ctinfo = IP_CT_RELATED;
		} else {
			DEBUGP("ip_conntrack_in: new packet for %p\n",
			       h->ctrack);
			ctinfo = IP_CT_NEW;
		}
	}
	skb->nfct = &h->ctrack->infos[ctinfo];
}

/* Return conntrack and conntrack_info a given skb */
struct ip_conntrack *
ip_conntrack_get(struct sk_buff *skb, enum ip_conntrack_info *ctinfo)
{
	if (!skb->nfct) {
		/* It may be an icmp error... */
		if (!icmp_error_track(skb))
			resolve_normal_ct(skb);
	}

	if (skb->nfct) {
		struct ip_conntrack *ct
			= (struct ip_conntrack *)skb->nfct->master;

		/* ctinfo is the index of the nfct inside the conntrack */
		*ctinfo = skb->nfct - ct->infos;
		IP_NF_ASSERT(*ctinfo >= 0 && *ctinfo < IP_CT_NUMBER);
		return ct;
	}
	return NULL;
}

/* Netfilter hook itself. */
unsigned int ip_conntrack_in(unsigned int hooknum,
			     struct sk_buff **pskb,
			     const struct net_device *in,
			     const struct net_device *out,
			     int (*okfn)(struct sk_buff *))
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	struct ip_conntrack_protocol *proto;
	int ret;

	/* FIXME: Do this right please. --RR */
	(*pskb)->nfcache |= NFC_UNKNOWN;

	/* Previously seen (loopback)?  Ignore.  Do this before
           fragment check. */
	if ((*pskb)->nfct)
		return NF_ACCEPT;

	/* Gather fragments. */
	if ((*pskb)->nh.iph->frag_off & htons(IP_MF|IP_OFFSET)) {
		*pskb = ip_ct_gather_frags(*pskb);
		if (!*pskb)
			return NF_STOLEN;
	}

	ct = ip_conntrack_get(*pskb, &ctinfo);
	if (!ct)
		/* Not valid part of a connection */
		return NF_ACCEPT;

	proto = find_proto((*pskb)->nh.iph->protocol);
	/* If this is new, this is first time timer will be set */
	ret = proto->packet(ct, (*pskb)->nh.iph, (*pskb)->len, ctinfo);

	if (ret == -1) {
		/* Invalid */
		nf_conntrack_put((*pskb)->nfct);
		(*pskb)->nfct = NULL;
		return NF_ACCEPT;
	}

	if (ret != NF_DROP && ct->helper) {
		ret = ct->helper->help((*pskb)->nh.iph, (*pskb)->len,
				       ct, ctinfo);
		if (ret == -1) {
			/* Invalid */
			nf_conntrack_put((*pskb)->nfct);
			(*pskb)->nfct = NULL;
			return NF_ACCEPT;
		}
	}

	return ret;
}

int invert_tuplepr(struct ip_conntrack_tuple *inverse,
		   const struct ip_conntrack_tuple *orig)
{
	return invert_tuple(inverse, orig, find_proto(orig->dst.protonum));
}

/* Add a related connection. */
int ip_conntrack_expect_related(struct ip_conntrack *related_to,
				const struct ip_conntrack_tuple *tuple)
{
	WRITE_LOCK(&ip_conntrack_lock);
	related_to->expected.tuple = *tuple;

	if (!related_to->expected.expectant) {
		list_prepend(&expect_list, &related_to->expected);
		related_to->expected.expectant = related_to;
	} else {
		IP_NF_ASSERT(list_inlist(&expect_list, &related_to->expected));
		IP_NF_ASSERT(related_to->expected.expectant
				    == related_to);
	}
	WRITE_UNLOCK(&ip_conntrack_lock);

	return 0;
}

/* Alter reply tuple (maybe alter helper).  If it's already taken,
   return 0 and don't do alteration. */
int ip_conntrack_alter_reply(struct ip_conntrack *conntrack,
			     const struct ip_conntrack_tuple *newreply)
{
	unsigned int newindex = hash_conntrack(newreply);

	WRITE_LOCK(&ip_conntrack_lock);
	if (__ip_conntrack_find(newreply, conntrack)) {
		WRITE_UNLOCK(&ip_conntrack_lock);
		return 0;
	}
	DEBUGP("Altering reply tuple of %p to ", conntrack);
	DUMP_TUPLE(newreply);

	LIST_DELETE(&ip_conntrack_hash
		    [hash_conntrack(&conntrack->tuplehash[IP_CT_DIR_REPLY]
				    .tuple)],
		    &conntrack->tuplehash[IP_CT_DIR_REPLY]);
	conntrack->tuplehash[IP_CT_DIR_REPLY].tuple = *newreply;
	list_prepend(&ip_conntrack_hash[newindex],
		     &conntrack->tuplehash[IP_CT_DIR_REPLY]);
	conntrack->helper = LIST_FIND(&helpers, helper_cmp,
				      struct ip_conntrack_helper *,
				      newreply);
	WRITE_UNLOCK(&ip_conntrack_lock);
	return 1;
}

int ip_conntrack_helper_register(struct ip_conntrack_helper *me)
{
	MOD_INC_USE_COUNT;

	WRITE_LOCK(&ip_conntrack_lock);
	list_prepend(&helpers, me);
	WRITE_UNLOCK(&ip_conntrack_lock);

	return 0;
}

static inline int unhelp(struct ip_conntrack_tuple_hash *i,
			 const struct ip_conntrack_helper *me)
{
	if (i->ctrack->helper == me) {
		i->ctrack->helper = NULL;
		/* Get rid of any expected. */
		if (i->ctrack->expected.expectant) {
			IP_NF_ASSERT(i->ctrack->expected.expectant
				     == i->ctrack);
			LIST_DELETE(&expect_list, &i->ctrack->expected);
			i->ctrack->expected.expectant = NULL;
		}
	}
	return 0;
}

void ip_conntrack_helper_unregister(struct ip_conntrack_helper *me)
{
	unsigned int i;

	/* Need write lock here, to delete helper. */
	WRITE_LOCK(&ip_conntrack_lock);
	LIST_DELETE(&helpers, me);

	/* Get rid of expecteds, set helpers to NULL. */
	for (i = 0; i < ip_conntrack_htable_size; i++)
		LIST_FIND_W(&ip_conntrack_hash[i], unhelp,
			    struct ip_conntrack_tuple_hash *, me);
	WRITE_UNLOCK(&ip_conntrack_lock);

	/* Someone could be still looking at the helper in a bh. */
	br_write_lock_bh(BR_NETPROTO_LOCK);
	br_write_unlock_bh(BR_NETPROTO_LOCK);

	MOD_DEC_USE_COUNT;
}

/* Refresh conntrack for this many jiffies: if noone calls this,
   conntrack will vanish with current skb. */
void ip_ct_refresh(struct ip_conntrack *ct, unsigned long extra_jiffies)
{
	WRITE_LOCK(&ip_conntrack_lock);
	/* If this hasn't had a timer before, it's still being set up */
	if (ct->timeout.data == 0) {
		ct->timeout.data = (unsigned long)ct;
		ct->timeout.function = death_by_timeout;
		ct->timeout.expires = jiffies + extra_jiffies;
		atomic_inc(&ct->ct_general.use);
		add_timer(&ct->timeout);
	} else {
		/* Need del_timer for race avoidance (may already be dying). */
		if (del_timer(&ct->timeout)) {
			ct->timeout.expires = jiffies + extra_jiffies;
			add_timer(&ct->timeout);
		}
	}
	WRITE_UNLOCK(&ip_conntrack_lock);
}

/* Returns new sk_buff, or NULL */
struct sk_buff *
ip_ct_gather_frags(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
#ifdef CONFIG_NETFILTER_DEBUG
	unsigned int olddebug = skb->nf_debug;
#endif
	if (sk) sock_hold(sk);
	skb = ip_defrag(skb);
	if (!skb) {
		if (sk) sock_put(sk);
		return skb;
	}
	if (sk) {
		skb_set_owner_w(skb, sk);
		sock_put(sk);
	}

	ip_send_check(skb->nh.iph);
	skb->nfcache |= NFC_ALTERED;
#ifdef CONFIG_NETFILTER_DEBUG
	/* Packet path as if nothing had happened. */
	skb->nf_debug = olddebug;
#endif
	return skb;
}

static inline int
do_kill(const struct ip_conntrack_tuple_hash *i,
	int (*kill)(const struct ip_conntrack *i, void *data),
	void *data)
{
	return kill(i->ctrack, data);
}

/* Bring out ya dead! */
static struct ip_conntrack_tuple_hash *
get_next_corpse(int (*kill)(const struct ip_conntrack *i, void *data),
		void *data)
{
	struct ip_conntrack_tuple_hash *h = NULL;
	unsigned int i;

	READ_LOCK(&ip_conntrack_lock);
	for (i = 0; !h && i < ip_conntrack_htable_size; i++) {
		h = LIST_FIND(&ip_conntrack_hash[i], do_kill,
			      struct ip_conntrack_tuple_hash *, kill, data);
	}
	if (h)
		atomic_inc(&h->ctrack->ct_general.use);
	READ_UNLOCK(&ip_conntrack_lock);

	return h;
}

void
ip_ct_selective_cleanup(int (*kill)(const struct ip_conntrack *i, void *data),
			void *data)
{
	struct ip_conntrack_tuple_hash *h;

	/* This is order n^2, by the way. */
	while ((h = get_next_corpse(kill, data)) != NULL) {
		/* Time to push up daises... */
		if (del_timer(&h->ctrack->timeout))
			death_by_timeout((unsigned long)h->ctrack);
		/* ... else the timer will get him soon. */

		ip_conntrack_put(h->ctrack);
	}
}

/* Fast function for those who don't want to parse /proc (and I don't
   blame them). */
/* Reversing the socket's dst/src point of view gives us the reply
   mapping. */
static int
getorigdst(struct sock *sk, int optval, void *user, int *len)
{
	struct ip_conntrack_tuple_hash *h;
	struct ip_conntrack_tuple tuple = { { sk->rcv_saddr, { sk->sport },
					      0 },
					    { sk->daddr, { sk->dport },
					      IPPROTO_TCP } };

	/* We only do TCP at the moment: is there a better way? */
	if (strcmp(sk->prot->name, "TCP") != 0) {
		DEBUGP("SO_ORIGINAL_DST: Not a TCP socket\n");
		return -ENOPROTOOPT;
	}

	if (*len != sizeof(struct sockaddr_in)) {
		DEBUGP("SO_ORIGINAL_DST: len %u not %u\n",
		       *len, sizeof(struct sockaddr_in));
		return -EINVAL;
	}

	h = ip_conntrack_find_get(&tuple, NULL);
	if (h) {
		struct sockaddr_in sin;

		sin.sin_family = AF_INET;
		sin.sin_port = h->ctrack->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u.tcp.port;
		sin.sin_addr.s_addr = h->ctrack->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.ip;

		DEBUGP("SO_ORIGINAL_DST: %u.%u.%u.%u %u\n",
		       IP_PARTS(sin.sin_addr.s_addr), ntohs(sin.sin_port));
		ip_conntrack_put(h->ctrack);
		if (copy_to_user(user, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		else
			return 0;
	}
	DEBUGP("SO_ORIGINAL_DST: Can't find %u.%u.%u.%u/%u-%u.%u.%u.%u/%u.\n",
	       IP_PARTS(tuple.src.ip), ntohs(tuple.src.u.tcp.port),
	       IP_PARTS(tuple.dst.ip), ntohs(tuple.dst.u.tcp.port));
	return -ENOENT;
}

static struct nf_sockopt_ops so_getorigdst
= { { NULL, NULL }, PF_INET,
    0, 0, NULL, /* Setsockopts */
    SO_ORIGINAL_DST, SO_ORIGINAL_DST+1, &getorigdst,
    0, NULL };

#define NET_IP_CONNTRACK_MAX 2089
#define NET_IP_CONNTRACK_MAX_NAME "ip_conntrack_max"

static struct ctl_table_header *ip_conntrack_sysctl_header;

static ctl_table ip_conntrack_table[] = {
	{ NET_IP_CONNTRACK_MAX, NET_IP_CONNTRACK_MAX_NAME, &ip_conntrack_max,
	  sizeof(ip_conntrack_max), 0644,  NULL, proc_dointvec },
 	{ 0 }
};

static ctl_table ip_conntrack_dir_table[] = {
	{NET_IPV4, "ipv4", NULL, 0, 0555, ip_conntrack_table, 0, 0, 0, 0, 0},
	{ 0 }
};

static ctl_table ip_conntrack_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, ip_conntrack_dir_table, 0, 0, 0, 0, 0},
	{ 0 }
};

static int kill_all(const struct ip_conntrack *i, void *data)
{
	return 1;
}

/* Mishearing the voices in his head, our hero wonders how he's
   supposed to kill the mall. */
void ip_conntrack_cleanup(void)
{
	unregister_sysctl_table(ip_conntrack_sysctl_header);
	ip_ct_selective_cleanup(kill_all, NULL);
	vfree(ip_conntrack_hash);
	nf_unregister_sockopt(&so_getorigdst);
}

int __init ip_conntrack_init(void)
{
	unsigned int i;
	int ret;

	/* Idea from tcp.c: use 1/16384 of memory.  On i386: 32MB
	 * machine has 256 buckets.  1GB machine has 8192 buckets. */
	ip_conntrack_htable_size
		= (((num_physpages << PAGE_SHIFT) / 16384)
		   / sizeof(struct list_head));
	ip_conntrack_max = 8 * ip_conntrack_htable_size;

	printk("ip_conntrack (%u buckets, %d max)\n",
	       ip_conntrack_htable_size, ip_conntrack_max);

	ret = nf_register_sockopt(&so_getorigdst);
	if (ret != 0)
		return ret;

	ip_conntrack_hash = vmalloc(sizeof(struct list_head)
				    * ip_conntrack_htable_size);
	if (!ip_conntrack_hash) {
		nf_unregister_sockopt(&so_getorigdst);
		return -ENOMEM;
	}

	/* Don't NEED lock here, but good form anyway. */
	WRITE_LOCK(&ip_conntrack_lock);
	/* Sew in builtin protocols. */
	list_append(&protocol_list, &ip_conntrack_protocol_tcp);
	list_append(&protocol_list, &ip_conntrack_protocol_udp);
	list_append(&protocol_list, &ip_conntrack_protocol_icmp);
	WRITE_UNLOCK(&ip_conntrack_lock);

	for (i = 0; i < ip_conntrack_htable_size; i++)
		INIT_LIST_HEAD(&ip_conntrack_hash[i]);

/* This is fucking braindead.  There is NO WAY of doing this without
   the CONFIG_SYSCTL unless you don't want to detect errors.
   Grrr... --RR */
#ifdef CONFIG_SYSCTL
	ip_conntrack_sysctl_header
		= register_sysctl_table(ip_conntrack_root_table, 0);
	if (ip_conntrack_sysctl_header == NULL) {
		vfree(ip_conntrack_hash);
		nf_unregister_sockopt(&so_getorigdst);
		return -ENOMEM;
	}
#endif /*CONFIG_SYSCTL*/

	ret = ip_conntrack_protocol_tcp_init();
	if (ret != 0) {
		unregister_sysctl_table(ip_conntrack_sysctl_header);
		vfree(ip_conntrack_hash);
		nf_unregister_sockopt(&so_getorigdst);
	}

	return ret;
}


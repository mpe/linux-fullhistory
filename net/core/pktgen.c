/*
 * Authors:
 * Copyright 2001, 2002 by Robert Olsson <robert.olsson@its.uu.se>
 *                             Uppsala University and
 *                             Swedish University of Agricultural Sciences
 *
 * Alexey Kuznetsov  <kuznet@ms2.inr.ac.ru>
 * Ben Greear <greearb@candelatech.com>
 * Jens L��s <jens.laas@data.slu.se>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * A tool for loading the network with preconfigurated packets.
 * The tool is implemented as a linux module.  Parameters are output 
 * device, delay (to hard_xmit), number of packets, and whether
 * to use multiple SKBs or just the same one.
 * pktgen uses the installed interface's output routine.
 *
 * Additional hacking by:
 *
 * Jens.Laas@data.slu.se
 * Improved by ANK. 010120.
 * Improved by ANK even more. 010212.
 * MAC address typo fixed. 010417 --ro
 * Integrated.  020301 --DaveM
 * Added multiskb option 020301 --DaveM
 * Scaling of results. 020417--sigurdur@linpro.no
 * Significant re-work of the module:
 *   *  Convert to threaded model to more efficiently be able to transmit
 *       and receive on multiple interfaces at once.
 *   *  Converted many counters to __u64 to allow longer runs.
 *   *  Allow configuration of ranges, like min/max IP address, MACs,
 *       and UDP-ports, for both source and destination, and can
 *       set to use a random distribution or sequentially walk the range.
 *   *  Can now change most values after starting.
 *   *  Place 12-byte packet in UDP payload with magic number,
 *       sequence number, and timestamp.
 *   *  Add receiver code that detects dropped pkts, re-ordered pkts, and
 *       latencies (with micro-second) precision.
 *   *  Add IOCTL interface to easily get counters & configuration.
 *   --Ben Greear <greearb@candelatech.com>
 *
 * Renamed multiskb to clone_skb and cleaned up sending core for two distinct 
 * skb modes. A clone_skb=0 mode for Ben "ranges" work and a clone_skb != 0 
 * as a "fastpath" with a configurable number of clones after alloc's.
 * clone_skb=0 means all packets are allocated this also means ranges time 
 * stamps etc can be used. clone_skb=100 means 1 malloc is followed by 100 
 * clones.
 *
 * Also moved to /proc/net/pktgen/ 
 * --ro
 *
 * Sept 10:  Fixed threading/locking.  Lots of bone-headed and more clever
 *    mistakes.  Also merged in DaveM's patch in the -pre6 patch.
 * --Ben Greear <greearb@candelatech.com>
 *
 * Integrated to 2.5.x 021029 --Lucio Maciel (luciomaciel@zipmail.com.br)
 *
 * 
 * 021124 Finished major redesign and rewrite for new functionality.
 * See Documentation/networking/pktgen.txt for how to use this.
 *
 * The new operation:
 * For each CPU one thread/process is created at start. This process checks 
 * for running devices in the if_list and sends packets until count is 0 it 
 * also the thread checks the thread->control which is used for inter-process 
 * communication. controlling process "posts" operations to the threads this 
 * way. The if_lock should be possible to remove when add/rem_device is merged
 * into this too.
 *
 * By design there should only be *one* "controlling" process. In practice 
 * multiple write accesses gives unpredictable result. Understood by "write" 
 * to /proc gives result code thats should be read be the "writer".
 * For pratical use this should be no problem.
 *
 * Note when adding devices to a specific CPU there good idea to also assign 
 * /proc/irq/XX/smp_affinity so TX-interrupts gets bound to the same CPU. 
 * --ro
 *
 * Fix refcount off by one if first packet fails, potential null deref, 
 * memleak 030710- KJP
 *
 * First "ranges" functionality for ipv6 030726 --ro
 *
 * Included flow support. 030802 ANK.
 *
 * Fixed unaligned access on IA-64 Grant Grundler <grundler@parisc-linux.org>
 * 
 * Remove if fix from added Harald Welte <laforge@netfilter.org> 040419
 * ia64 compilation fix from  Aron Griffis <aron@hp.com> 040604
 *
 * New xmit() return, do_div and misc clean up by Stephen Hemminger 
 * <shemminger@osdl.org> 040923
 *
 * Rany Dunlap fixed u64 printk compiler waring 
 *
 * Remove FCS from BW calculation.  Lennert Buytenhek <buytenh@wantstofly.org>
 * New time handling. Lennert Buytenhek <buytenh@wantstofly.org> 041213
 *
 * Corrections from Nikolai Malykh (nmalykh@bilim.com) 
 * Removed unused flags F_SET_SRCMAC & F_SET_SRCIP 041230
 *
 * interruptible_sleep_on_timeout() replaced Nishanth Aravamudan <nacc@us.ibm.com> 
 * 050103
 */
#include <linux/sys.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/proc_fs.h>
#include <linux/wait.h>
#include <net/checksum.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <asm/byteorder.h>
#include <linux/rcupdate.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <asm/div64.h> /* do_div */
#include <asm/timex.h>


#define VERSION  "pktgen v2.61: Packet Generator for packet performance testing.\n"

/* #define PG_DEBUG(a) a */
#define PG_DEBUG(a) 

/* The buckets are exponential in 'width' */
#define LAT_BUCKETS_MAX 32
#define IP_NAME_SZ 32

/* Device flag bits */
#define F_IPSRC_RND   (1<<0)  /* IP-Src Random  */
#define F_IPDST_RND   (1<<1)  /* IP-Dst Random  */
#define F_UDPSRC_RND  (1<<2)  /* UDP-Src Random */
#define F_UDPDST_RND  (1<<3)  /* UDP-Dst Random */
#define F_MACSRC_RND  (1<<4)  /* MAC-Src Random */
#define F_MACDST_RND  (1<<5)  /* MAC-Dst Random */
#define F_TXSIZE_RND  (1<<6)  /* Transmit size is random */
#define F_IPV6        (1<<7)  /* Interface in IPV6 Mode */

/* Thread control flag bits */
#define T_TERMINATE   (1<<0)  
#define T_STOP        (1<<1)  /* Stop run */
#define T_RUN         (1<<2)  /* Start run */
#define T_REMDEV      (1<<3)  /* Remove all devs */

/* Locks */
#define   thread_lock()        spin_lock(&_thread_lock)
#define   thread_unlock()      spin_unlock(&_thread_lock)

/* If lock -- can be removed after some work */
#define   if_lock(t)           spin_lock(&(t->if_lock));
#define   if_unlock(t)           spin_unlock(&(t->if_lock));

/* Used to help with determining the pkts on receive */
#define PKTGEN_MAGIC 0xbe9be955
#define PG_PROC_DIR "pktgen"

#define MAX_CFLOWS  65536

struct flow_state
{
	__u32		cur_daddr;
	int		count;
};

struct pktgen_dev {

	/*
	 * Try to keep frequent/infrequent used vars. separated.
	 */

        char ifname[32];
        struct proc_dir_entry *proc_ent;
        char result[512];
        /* proc file names */
        char fname[80];

        struct pktgen_thread* pg_thread; /* the owner */
        struct pktgen_dev *next; /* Used for chaining in the thread's run-queue */

        int running;  /* if this changes to false, the test will stop */
        
        /* If min != max, then we will either do a linear iteration, or
         * we will do a random selection from within the range.
         */
        __u32 flags;     

        int min_pkt_size;    /* = ETH_ZLEN; */
        int max_pkt_size;    /* = ETH_ZLEN; */
        int nfrags;
        __u32 delay_us;    /* Default delay */
        __u32 delay_ns;
        __u64 count;  /* Default No packets to send */
        __u64 sofar;  /* How many pkts we've sent so far */
        __u64 tx_bytes; /* How many bytes we've transmitted */
        __u64 errors;    /* Errors when trying to transmit, pkts will be re-sent */

        /* runtime counters relating to clone_skb */
        __u64 next_tx_us;          /* timestamp of when to tx next */
        __u32 next_tx_ns;
        
        __u64 allocated_skbs;
        __u32 clone_count;
	int last_ok;           /* Was last skb sent? 
	                        * Or a failed transmit of some sort?  This will keep
                                * sequence numbers in order, for example.
				*/
        __u64 started_at; /* micro-seconds */
        __u64 stopped_at; /* micro-seconds */
        __u64 idle_acc; /* micro-seconds */
        __u32 seq_num;
        
        int clone_skb; /* Use multiple SKBs during packet gen.  If this number
                          * is greater than 1, then that many coppies of the same
                          * packet will be sent before a new packet is allocated.
                          * For instance, if you want to send 1024 identical packets
                          * before creating a new packet, set clone_skb to 1024.
                          */
        
        char dst_min[IP_NAME_SZ]; /* IP, ie 1.2.3.4 */
        char dst_max[IP_NAME_SZ]; /* IP, ie 1.2.3.4 */
        char src_min[IP_NAME_SZ]; /* IP, ie 1.2.3.4 */
        char src_max[IP_NAME_SZ]; /* IP, ie 1.2.3.4 */

	struct in6_addr  in6_saddr;
	struct in6_addr  in6_daddr;
	struct in6_addr  cur_in6_daddr;
	struct in6_addr  cur_in6_saddr;
	/* For ranges */
	struct in6_addr  min_in6_daddr;
	struct in6_addr  max_in6_daddr;
	struct in6_addr  min_in6_saddr;
	struct in6_addr  max_in6_saddr;

        /* If we're doing ranges, random or incremental, then this
         * defines the min/max for those ranges.
         */
        __u32 saddr_min; /* inclusive, source IP address */
        __u32 saddr_max; /* exclusive, source IP address */
        __u32 daddr_min; /* inclusive, dest IP address */
        __u32 daddr_max; /* exclusive, dest IP address */

        __u16 udp_src_min; /* inclusive, source UDP port */
        __u16 udp_src_max; /* exclusive, source UDP port */
        __u16 udp_dst_min; /* inclusive, dest UDP port */
        __u16 udp_dst_max; /* exclusive, dest UDP port */

        __u32 src_mac_count; /* How many MACs to iterate through */
        __u32 dst_mac_count; /* How many MACs to iterate through */
        
        unsigned char dst_mac[6];
        unsigned char src_mac[6];
        
        __u32 cur_dst_mac_offset;
        __u32 cur_src_mac_offset;
        __u32 cur_saddr;
        __u32 cur_daddr;
        __u16 cur_udp_dst;
        __u16 cur_udp_src;
        __u32 cur_pkt_size;
        
        __u8 hh[14];
        /* = { 
           0x00, 0x80, 0xC8, 0x79, 0xB3, 0xCB, 
           
           We fill in SRC address later
           0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
           0x08, 0x00
           };
        */
        __u16 pad; /* pad out the hh struct to an even 16 bytes */

        struct sk_buff* skb; /* skb we are to transmit next, mainly used for when we
                              * are transmitting the same one multiple times
                              */
        struct net_device* odev; /* The out-going device.  Note that the device should
                                  * have it's pg_info pointer pointing back to this
                                  * device.  This will be set when the user specifies
                                  * the out-going device name (not when the inject is
                                  * started as it used to do.)
                                  */
	struct flow_state *flows;
	unsigned cflows;         /* Concurrent flows (config) */
	unsigned lflow;          /* Flow length  (config) */
	unsigned nflows;         /* accumulated flows (stats) */
};

struct pktgen_hdr {
        __u32 pgh_magic;
        __u32 seq_num;
	__u32 tv_sec;
	__u32 tv_usec;
};

struct pktgen_thread {
        spinlock_t if_lock;
        struct pktgen_dev *if_list;           /* All device here */
        struct pktgen_thread* next;
        char name[32];
        char fname[128]; /* name of proc file */
        struct proc_dir_entry *proc_ent;
        char result[512];
        u32 max_before_softirq; /* We'll call do_softirq to prevent starvation. */
        
	/* Field for thread to receive "posted" events terminate, stop ifs etc.*/

        u32 control;
	int pid;
	int cpu;

        wait_queue_head_t queue;
};

#define REMOVE 1
#define FIND   0

/*  This code works around the fact that do_div cannot handle two 64-bit
    numbers, and regular 64-bit division doesn't work on x86 kernels.
    --Ben
*/

#define PG_DIV 0

/* This was emailed to LMKL by: Chris Caputo <ccaputo@alt.net>
 * Function copied/adapted/optimized from:
 *
 *  nemesis.sourceforge.net/browse/lib/static/intmath/ix86/intmath.c.html
 *
 * Copyright 1994, University of Cambridge Computer Laboratory
 * All Rights Reserved.
 *
 */
inline static s64 divremdi3(s64 x, s64 y, int type) 
{
        u64 a = (x < 0) ? -x : x;
        u64 b = (y < 0) ? -y : y;
        u64 res = 0, d = 1;

        if (b > 0) {
                while (b < a) {
                        b <<= 1;
                        d <<= 1;
                }
        }
        
        do {
                if ( a >= b ) {
                        a -= b;
                        res += d;
                }
                b >>= 1;
                d >>= 1;
        }
        while (d);

        if (PG_DIV == type) {
                return (((x ^ y) & (1ll<<63)) == 0) ? res : -(s64)res;
        }
        else {
                return ((x & (1ll<<63)) == 0) ? a : -(s64)a;
        }
}

/* End of hacks to deal with 64-bit math on x86 */

/** Convert to miliseconds */
static inline __u64 tv_to_ms(const struct timeval* tv) 
{
        __u64 ms = tv->tv_usec / 1000;
        ms += (__u64)tv->tv_sec * (__u64)1000;
        return ms;
}


/** Convert to micro-seconds */
static inline __u64 tv_to_us(const struct timeval* tv) 
{
        __u64 us = tv->tv_usec;
        us += (__u64)tv->tv_sec * (__u64)1000000;
        return us;
}

static inline __u64 pg_div(__u64 n, __u32 base) {
        __u64 tmp = n;
        do_div(tmp, base);
        /* printk("pktgen: pg_div, n: %llu  base: %d  rv: %llu\n",
                  n, base, tmp); */
        return tmp;
}

static inline __u64 pg_div64(__u64 n, __u64 base) 
{
        __u64 tmp = n;
/*
 * How do we know if the architectrure we are running on
 * supports division with 64 bit base?
 * 
 */
#if defined(__sparc_v9__) || defined(__powerpc64__) || defined(__alpha__) || defined(__x86_64__) || defined(__ia64__) 

		do_div(tmp, base);
#else
		tmp = divremdi3(n, base, PG_DIV);
#endif
        return tmp;
}

static inline u32 pktgen_random(void)
{
#if 0
	__u32 n;
	get_random_bytes(&n, 4);
	return n;
#else
	return net_random();
#endif
}

static inline __u64 getCurMs(void) 
{
        struct timeval tv;
        do_gettimeofday(&tv);
        return tv_to_ms(&tv);
}

static inline __u64 getCurUs(void) 
{
        struct timeval tv;
        do_gettimeofday(&tv);
        return tv_to_us(&tv);
}

static inline __u64 tv_diff(const struct timeval* a, const struct timeval* b) 
{
        return tv_to_us(a) - tv_to_us(b);
}


/* old include end */

static char version[] __initdata = VERSION;

static ssize_t proc_pgctrl_read(struct file* file, char __user * buf, size_t count, loff_t *ppos);
static ssize_t proc_pgctrl_write(struct file* file, const char __user * buf, size_t count, loff_t *ppos);
static int proc_if_read(char *buf , char **start, off_t offset, int len, int *eof, void *data);

static int proc_thread_read(char *buf , char **start, off_t offset, int len, int *eof, void *data);
static int proc_if_write(struct file *file, const char __user *user_buffer, unsigned long count, void *data);
static int proc_thread_write(struct file *file, const char __user *user_buffer, unsigned long count, void *data);
static int create_proc_dir(void);
static int remove_proc_dir(void);

static int pktgen_remove_device(struct pktgen_thread* t, struct pktgen_dev *i);
static int pktgen_add_device(struct pktgen_thread* t, const char* ifname);
static struct pktgen_thread* pktgen_find_thread(const char* name);
static struct pktgen_dev *pktgen_find_dev(struct pktgen_thread* t, const char* ifname);
static int pktgen_device_event(struct notifier_block *, unsigned long, void *);
static void pktgen_run_all_threads(void);
static void pktgen_stop_all_threads_ifs(void);
static int pktgen_stop_device(struct pktgen_dev *pkt_dev);
static void pktgen_stop(struct pktgen_thread* t);
static void pktgen_clear_counters(struct pktgen_dev *pkt_dev);
static struct pktgen_dev *pktgen_NN_threads(const char* dev_name, int remove);
static unsigned int scan_ip6(const char *s,char ip[16]);
static unsigned int fmt_ip6(char *s,const char ip[16]);

/* Module parameters, defaults. */
static int pg_count_d = 1000; /* 1000 pkts by default */
static int pg_delay_d = 0;
static int pg_clone_skb_d = 0;
static int debug = 0;

static spinlock_t _thread_lock = SPIN_LOCK_UNLOCKED;
static struct pktgen_thread *pktgen_threads = NULL;

static char module_fname[128];
static struct proc_dir_entry *module_proc_ent = NULL;

static struct notifier_block pktgen_notifier_block = {
	.notifier_call = pktgen_device_event,
};

static struct file_operations pktgen_fops = {
        .read     = proc_pgctrl_read,
        .write    = proc_pgctrl_write,
	/*  .ioctl    = pktgen_ioctl, later maybe */
};

/*
 * /proc handling functions 
 *
 */

static struct proc_dir_entry *pg_proc_dir = NULL;
static int proc_pgctrl_read_eof=0;

static ssize_t proc_pgctrl_read(struct file* file, char __user * buf,
                                 size_t count, loff_t *ppos)
{ 
	char data[200];
	int len = 0;

	if(proc_pgctrl_read_eof) {
		proc_pgctrl_read_eof=0;
		len = 0;
		goto out;
	}

	sprintf(data, "%s", VERSION); 

	len = strlen(data);

	if(len > count) {
		len =-EFAULT;
		goto out;
	}  	

	if (copy_to_user(buf, data, len)) {
		len =-EFAULT;
		goto out;
	}  

	*ppos += len;
	proc_pgctrl_read_eof=1; /* EOF next call */

 out:
	return len;
}

static ssize_t proc_pgctrl_write(struct file* file,const char __user * buf,
				 size_t count, loff_t *ppos)
{
	char *data = NULL;
	int err = 0;

        if (!capable(CAP_NET_ADMIN)){
                err = -EPERM;
		goto out;
        }

	data = (void*)vmalloc ((unsigned int)count);

	if(!data) {
		err = -ENOMEM;
		goto out;
	}
	if (copy_from_user(data, buf, count)) {
		err =-EFAULT;
		goto out_free;
	}  
	data[count-1] = 0; /* Make string */

	if (!strcmp(data, "stop")) 
		pktgen_stop_all_threads_ifs();

        else if (!strcmp(data, "start")) 
		pktgen_run_all_threads();

	else 
		printk("pktgen: Unknown command: %s\n", data);

	err = count;

 out_free:
	vfree (data);
 out:
        return err;
}

static int proc_if_read(char *buf , char **start, off_t offset,
                           int len, int *eof, void *data)
{
	char *p;
	int i;
        struct pktgen_dev *pkt_dev = (struct pktgen_dev*)(data);
        __u64 sa;
        __u64 stopped;
        __u64 now = getCurUs();
        
	p = buf;
	p += sprintf(p, "Params: count %llu  min_pkt_size: %u  max_pkt_size: %u\n",
		     (unsigned long long) pkt_dev->count,
		     pkt_dev->min_pkt_size, pkt_dev->max_pkt_size);

	p += sprintf(p, "     frags: %d  delay: %u  clone_skb: %d  ifname: %s\n",
                     pkt_dev->nfrags, 1000*pkt_dev->delay_us+pkt_dev->delay_ns, pkt_dev->clone_skb, pkt_dev->ifname);

	p += sprintf(p, "     flows: %u flowlen: %u\n", pkt_dev->cflows, pkt_dev->lflow);


	if(pkt_dev->flags & F_IPV6) {
		char b1[128], b2[128], b3[128];
		fmt_ip6(b1,  pkt_dev->in6_saddr.s6_addr);
		fmt_ip6(b2,  pkt_dev->min_in6_saddr.s6_addr);
		fmt_ip6(b3,  pkt_dev->max_in6_saddr.s6_addr);
		p += sprintf(p, "     saddr: %s  min_saddr: %s  max_saddr: %s\n", b1, b2, b3);

		fmt_ip6(b1,  pkt_dev->in6_daddr.s6_addr);
		fmt_ip6(b2,  pkt_dev->min_in6_daddr.s6_addr);
		fmt_ip6(b3,  pkt_dev->max_in6_daddr.s6_addr);
		p += sprintf(p, "     daddr: %s  min_daddr: %s  max_daddr: %s\n", b1, b2, b3);

	} 
	else 
		p += sprintf(p, "     dst_min: %s  dst_max: %s\n     src_min: %s  src_max: %s\n",
                     pkt_dev->dst_min, pkt_dev->dst_max, pkt_dev->src_min, pkt_dev->src_max);

        p += sprintf(p, "     src_mac: ");

	if ((pkt_dev->src_mac[0] == 0) && 
	    (pkt_dev->src_mac[1] == 0) && 
	    (pkt_dev->src_mac[2] == 0) && 
	    (pkt_dev->src_mac[3] == 0) && 
	    (pkt_dev->src_mac[4] == 0) && 
	    (pkt_dev->src_mac[5] == 0)) 

		for (i = 0; i < 6; i++) 
			p += sprintf(p, "%02X%s", pkt_dev->odev->dev_addr[i], i == 5 ? "  " : ":");

	else 
		for (i = 0; i < 6; i++) 
			p += sprintf(p, "%02X%s", pkt_dev->src_mac[i], i == 5 ? "  " : ":");

        p += sprintf(p, "dst_mac: ");
	for (i = 0; i < 6; i++) 
		p += sprintf(p, "%02X%s", pkt_dev->dst_mac[i], i == 5 ? "\n" : ":");

        p += sprintf(p, "     udp_src_min: %d  udp_src_max: %d  udp_dst_min: %d  udp_dst_max: %d\n",
                     pkt_dev->udp_src_min, pkt_dev->udp_src_max, pkt_dev->udp_dst_min,
                     pkt_dev->udp_dst_max);

        p += sprintf(p, "     src_mac_count: %d  dst_mac_count: %d \n     Flags: ",
                     pkt_dev->src_mac_count, pkt_dev->dst_mac_count);


        if (pkt_dev->flags &  F_IPV6) 
                p += sprintf(p, "IPV6  ");

        if (pkt_dev->flags &  F_IPSRC_RND) 
                p += sprintf(p, "IPSRC_RND  ");

        if (pkt_dev->flags & F_IPDST_RND) 
                p += sprintf(p, "IPDST_RND  ");
        
        if (pkt_dev->flags & F_TXSIZE_RND) 
                p += sprintf(p, "TXSIZE_RND  ");
        
        if (pkt_dev->flags & F_UDPSRC_RND) 
                p += sprintf(p, "UDPSRC_RND  ");
        
        if (pkt_dev->flags & F_UDPDST_RND) 
                p += sprintf(p, "UDPDST_RND  ");
        
        if (pkt_dev->flags & F_MACSRC_RND) 
                p += sprintf(p, "MACSRC_RND  ");
        
        if (pkt_dev->flags & F_MACDST_RND) 
                p += sprintf(p, "MACDST_RND  ");

        
        p += sprintf(p, "\n");
        
        sa = pkt_dev->started_at;
        stopped = pkt_dev->stopped_at;
        if (pkt_dev->running) 
                stopped = now; /* not really stopped, more like last-running-at */
        
        p += sprintf(p, "Current:\n     pkts-sofar: %llu  errors: %llu\n     started: %lluus  stopped: %lluus idle: %lluus\n",
		     (unsigned long long) pkt_dev->sofar,
		     (unsigned long long) pkt_dev->errors,
		     (unsigned long long) sa,
		     (unsigned long long) stopped, 
		     (unsigned long long) pkt_dev->idle_acc);

        p += sprintf(p, "     seq_num: %d  cur_dst_mac_offset: %d  cur_src_mac_offset: %d\n",
                     pkt_dev->seq_num, pkt_dev->cur_dst_mac_offset, pkt_dev->cur_src_mac_offset);

	if(pkt_dev->flags & F_IPV6) {
		char b1[128], b2[128];
		fmt_ip6(b1,  pkt_dev->cur_in6_daddr.s6_addr);
		fmt_ip6(b2,  pkt_dev->cur_in6_saddr.s6_addr);
		p += sprintf(p, "     cur_saddr: %s  cur_daddr: %s\n", b2, b1);
	} 
	else 
		p += sprintf(p, "     cur_saddr: 0x%x  cur_daddr: 0x%x\n",
                     pkt_dev->cur_saddr, pkt_dev->cur_daddr);


	p += sprintf(p, "     cur_udp_dst: %d  cur_udp_src: %d\n",
                     pkt_dev->cur_udp_dst, pkt_dev->cur_udp_src);

	p += sprintf(p, "     flows: %u\n", pkt_dev->nflows);

	if (pkt_dev->result[0])
		p += sprintf(p, "Result: %s\n", pkt_dev->result);
	else
		p += sprintf(p, "Result: Idle\n");
	*eof = 1;

	return p - buf;
}


static int count_trail_chars(const char __user *user_buffer, unsigned int maxlen)
{
	int i;

	for (i = 0; i < maxlen; i++) {
                char c;
                if (get_user(c, &user_buffer[i]))
                        return -EFAULT;
                switch (c) {
		case '\"':
		case '\n':
		case '\r':
		case '\t':
		case ' ':
		case '=':
			break;
		default:
			goto done;
		};
	}
done:
	return i;
}

static unsigned long num_arg(const char __user *user_buffer, unsigned long maxlen, 
			     unsigned long *num)
{
	int i = 0;
	*num = 0;
  
	for(; i < maxlen; i++) {
                char c;
                if (get_user(c, &user_buffer[i]))
                        return -EFAULT;
                if ((c >= '0') && (c <= '9')) {
			*num *= 10;
			*num += c -'0';
		} else
			break;
	}
	return i;
}

static int strn_len(const char __user *user_buffer, unsigned int maxlen)
{
	int i = 0;

	for(; i < maxlen; i++) {
                char c;
                if (get_user(c, &user_buffer[i]))
                        return -EFAULT;
                switch (c) {
		case '\"':
		case '\n':
		case '\r':
		case '\t':
		case ' ':
			goto done_str;
			break;
		default:
			break;
		};
	}
done_str:

	return i;
}

static int proc_if_write(struct file *file, const char __user *user_buffer,
                            unsigned long count, void *data)
{
	int i = 0, max, len;
	char name[16], valstr[32];
	unsigned long value = 0;
        struct pktgen_dev *pkt_dev = (struct pktgen_dev*)(data);
        char* pg_result = NULL;
        int tmp = 0;
	char buf[128];
        
        pg_result = &(pkt_dev->result[0]);
        
	if (count < 1) {
		printk("pktgen: wrong command format\n");
		return -EINVAL;
	}
  
	max = count - i;
	tmp = count_trail_chars(&user_buffer[i], max);
        if (tmp < 0) { 
		printk("pktgen: illegal format\n");
		return tmp; 
	}
        i += tmp;
        
	/* Read variable name */

	len = strn_len(&user_buffer[i], sizeof(name) - 1);
        if (len < 0) { return len; }
	memset(name, 0, sizeof(name));
	if (copy_from_user(name, &user_buffer[i], len) )
		return -EFAULT;
	i += len;
  
	max = count -i;
	len = count_trail_chars(&user_buffer[i], max);
        if (len < 0) 
                return len;
        
	i += len;

	if (debug) {
                char tb[count + 1];
                if (copy_from_user(tb, user_buffer, count))
			return -EFAULT;
                tb[count] = 0;
		printk("pktgen: %s,%lu  buffer -:%s:-\n", name, count, tb);
        }

	if (!strcmp(name, "min_pkt_size")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		if (value < 14+20+8)
			value = 14+20+8;
                if (value != pkt_dev->min_pkt_size) {
                        pkt_dev->min_pkt_size = value;
                        pkt_dev->cur_pkt_size = value;
                }
		sprintf(pg_result, "OK: min_pkt_size=%u", pkt_dev->min_pkt_size);
		return count;
	}

        if (!strcmp(name, "max_pkt_size")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		if (value < 14+20+8)
			value = 14+20+8;
                if (value != pkt_dev->max_pkt_size) {
                        pkt_dev->max_pkt_size = value;
                        pkt_dev->cur_pkt_size = value;
                }
		sprintf(pg_result, "OK: max_pkt_size=%u", pkt_dev->max_pkt_size);
		return count;
	}

        /* Shortcut for min = max */

	if (!strcmp(name, "pkt_size")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		if (value < 14+20+8)
			value = 14+20+8;
                if (value != pkt_dev->min_pkt_size) {
                        pkt_dev->min_pkt_size = value;
                        pkt_dev->max_pkt_size = value;
                        pkt_dev->cur_pkt_size = value;
                }
		sprintf(pg_result, "OK: pkt_size=%u", pkt_dev->min_pkt_size);
		return count;
	}

        if (!strcmp(name, "debug")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
                debug = value;
		sprintf(pg_result, "OK: debug=%u", debug);
		return count;
	}

        if (!strcmp(name, "frags")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		pkt_dev->nfrags = value;
		sprintf(pg_result, "OK: frags=%u", pkt_dev->nfrags);
		return count;
	}
	if (!strcmp(name, "delay")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		if (value == 0x7FFFFFFF) {
			pkt_dev->delay_us = 0x7FFFFFFF;
			pkt_dev->delay_ns = 0;
		} else {
			pkt_dev->delay_us = value / 1000;
			pkt_dev->delay_ns = value % 1000;
		}
		sprintf(pg_result, "OK: delay=%u", 1000*pkt_dev->delay_us+pkt_dev->delay_ns);
		return count;
	}
 	if (!strcmp(name, "udp_src_min")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
                if (value != pkt_dev->udp_src_min) {
                        pkt_dev->udp_src_min = value;
                        pkt_dev->cur_udp_src = value;
                }       
		sprintf(pg_result, "OK: udp_src_min=%u", pkt_dev->udp_src_min);
		return count;
	}
 	if (!strcmp(name, "udp_dst_min")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
                if (value != pkt_dev->udp_dst_min) {
                        pkt_dev->udp_dst_min = value;
                        pkt_dev->cur_udp_dst = value;
                }
		sprintf(pg_result, "OK: udp_dst_min=%u", pkt_dev->udp_dst_min);
		return count;
	}
 	if (!strcmp(name, "udp_src_max")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
                if (value != pkt_dev->udp_src_max) {
                        pkt_dev->udp_src_max = value;
                        pkt_dev->cur_udp_src = value;
                }
		sprintf(pg_result, "OK: udp_src_max=%u", pkt_dev->udp_src_max);
		return count;
	}
 	if (!strcmp(name, "udp_dst_max")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
                if (value != pkt_dev->udp_dst_max) {
                        pkt_dev->udp_dst_max = value;
                        pkt_dev->cur_udp_dst = value;
                }
		sprintf(pg_result, "OK: udp_dst_max=%u", pkt_dev->udp_dst_max);
		return count;
	}
	if (!strcmp(name, "clone_skb")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
                pkt_dev->clone_skb = value;
	
		sprintf(pg_result, "OK: clone_skb=%d", pkt_dev->clone_skb);
		return count;
	}
	if (!strcmp(name, "count")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		pkt_dev->count = value;
		sprintf(pg_result, "OK: count=%llu",
			(unsigned long long) pkt_dev->count);
		return count;
	}
	if (!strcmp(name, "src_mac_count")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		if (pkt_dev->src_mac_count != value) {
                        pkt_dev->src_mac_count = value;
                        pkt_dev->cur_src_mac_offset = 0;
                }
		sprintf(pg_result, "OK: src_mac_count=%d", pkt_dev->src_mac_count);
		return count;
	}
	if (!strcmp(name, "dst_mac_count")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		if (pkt_dev->dst_mac_count != value) {
                        pkt_dev->dst_mac_count = value;
                        pkt_dev->cur_dst_mac_offset = 0;
                }
		sprintf(pg_result, "OK: dst_mac_count=%d", pkt_dev->dst_mac_count);
		return count;
	}
	if (!strcmp(name, "flag")) {
                char f[32];
                memset(f, 0, 32);
		len = strn_len(&user_buffer[i], sizeof(f) - 1);
                if (len < 0) { return len; }
		if (copy_from_user(f, &user_buffer[i], len))
			return -EFAULT;
		i += len;
                if (strcmp(f, "IPSRC_RND") == 0) 
                        pkt_dev->flags |= F_IPSRC_RND;
                
                else if (strcmp(f, "!IPSRC_RND") == 0) 
                        pkt_dev->flags &= ~F_IPSRC_RND;
                
                else if (strcmp(f, "TXSIZE_RND") == 0) 
                        pkt_dev->flags |= F_TXSIZE_RND;
                
                else if (strcmp(f, "!TXSIZE_RND") == 0) 
                        pkt_dev->flags &= ~F_TXSIZE_RND;
                
                else if (strcmp(f, "IPDST_RND") == 0) 
                        pkt_dev->flags |= F_IPDST_RND;
                
                else if (strcmp(f, "!IPDST_RND") == 0) 
                        pkt_dev->flags &= ~F_IPDST_RND;
                
                else if (strcmp(f, "UDPSRC_RND") == 0) 
                        pkt_dev->flags |= F_UDPSRC_RND;
                
                else if (strcmp(f, "!UDPSRC_RND") == 0) 
                        pkt_dev->flags &= ~F_UDPSRC_RND;
                
                else if (strcmp(f, "UDPDST_RND") == 0) 
                        pkt_dev->flags |= F_UDPDST_RND;
                
                else if (strcmp(f, "!UDPDST_RND") == 0) 
                        pkt_dev->flags &= ~F_UDPDST_RND;
                
                else if (strcmp(f, "MACSRC_RND") == 0) 
                        pkt_dev->flags |= F_MACSRC_RND;
                
                else if (strcmp(f, "!MACSRC_RND") == 0) 
                        pkt_dev->flags &= ~F_MACSRC_RND;
                
                else if (strcmp(f, "MACDST_RND") == 0) 
                        pkt_dev->flags |= F_MACDST_RND;
                
                else if (strcmp(f, "!MACDST_RND") == 0) 
                        pkt_dev->flags &= ~F_MACDST_RND;
                
                else {
                        sprintf(pg_result, "Flag -:%s:- unknown\nAvailable flags, (prepend ! to un-set flag):\n%s",
                                f,
                                "IPSRC_RND, IPDST_RND, TXSIZE_RND, UDPSRC_RND, UDPDST_RND, MACSRC_RND, MACDST_RND\n");
                        return count;
                }
		sprintf(pg_result, "OK: flags=0x%x", pkt_dev->flags);
		return count;
	}
	if (!strcmp(name, "dst_min") || !strcmp(name, "dst")) {
		len = strn_len(&user_buffer[i], sizeof(pkt_dev->dst_min) - 1);
                if (len < 0) { return len; }

                if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
                buf[len] = 0;
                if (strcmp(buf, pkt_dev->dst_min) != 0) {
                        memset(pkt_dev->dst_min, 0, sizeof(pkt_dev->dst_min));
                        strncpy(pkt_dev->dst_min, buf, len);
                        pkt_dev->daddr_min = in_aton(pkt_dev->dst_min);
                        pkt_dev->cur_daddr = pkt_dev->daddr_min;
                }
                if(debug)
                        printk("pktgen: dst_min set to: %s\n", pkt_dev->dst_min);
                i += len;
		sprintf(pg_result, "OK: dst_min=%s", pkt_dev->dst_min);
		return count;
	}
	if (!strcmp(name, "dst_max")) {
		len = strn_len(&user_buffer[i], sizeof(pkt_dev->dst_max) - 1);
                if (len < 0) { return len; }

                if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;

                buf[len] = 0;
                if (strcmp(buf, pkt_dev->dst_max) != 0) {
                        memset(pkt_dev->dst_max, 0, sizeof(pkt_dev->dst_max));
                        strncpy(pkt_dev->dst_max, buf, len);
                        pkt_dev->daddr_max = in_aton(pkt_dev->dst_max);
                        pkt_dev->cur_daddr = pkt_dev->daddr_max;
                }
		if(debug)
			printk("pktgen: dst_max set to: %s\n", pkt_dev->dst_max);
		i += len;
		sprintf(pg_result, "OK: dst_max=%s", pkt_dev->dst_max);
		return count;
	}
	if (!strcmp(name, "dst6")) {
		len = strn_len(&user_buffer[i], sizeof(buf) - 1);
                if (len < 0) return len; 

		pkt_dev->flags |= F_IPV6;

                if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
                buf[len] = 0;

		scan_ip6(buf, pkt_dev->in6_daddr.s6_addr);
		fmt_ip6(buf,  pkt_dev->in6_daddr.s6_addr);

		ipv6_addr_copy(&pkt_dev->cur_in6_daddr, &pkt_dev->in6_daddr);

                if(debug) 
			printk("pktgen: dst6 set to: %s\n", buf);

                i += len;
		sprintf(pg_result, "OK: dst6=%s", buf);
		return count;
	}
	if (!strcmp(name, "dst6_min")) {
		len = strn_len(&user_buffer[i], sizeof(buf) - 1);
                if (len < 0) return len; 

		pkt_dev->flags |= F_IPV6;

                if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
                buf[len] = 0;

		scan_ip6(buf, pkt_dev->min_in6_daddr.s6_addr);
		fmt_ip6(buf,  pkt_dev->min_in6_daddr.s6_addr);

		ipv6_addr_copy(&pkt_dev->cur_in6_daddr, &pkt_dev->min_in6_daddr);
                if(debug) 
			printk("pktgen: dst6_min set to: %s\n", buf);

                i += len;
		sprintf(pg_result, "OK: dst6_min=%s", buf);
		return count;
	}
	if (!strcmp(name, "dst6_max")) {
		len = strn_len(&user_buffer[i], sizeof(buf) - 1);
                if (len < 0) return len; 

		pkt_dev->flags |= F_IPV6;

                if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
                buf[len] = 0;

		scan_ip6(buf, pkt_dev->max_in6_daddr.s6_addr);
		fmt_ip6(buf,  pkt_dev->max_in6_daddr.s6_addr);

                if(debug) 
			printk("pktgen: dst6_max set to: %s\n", buf);

                i += len;
		sprintf(pg_result, "OK: dst6_max=%s", buf);
		return count;
	}
	if (!strcmp(name, "src6")) {
		len = strn_len(&user_buffer[i], sizeof(buf) - 1);
                if (len < 0) return len; 

		pkt_dev->flags |= F_IPV6;

                if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
                buf[len] = 0;

		scan_ip6(buf, pkt_dev->in6_saddr.s6_addr);
		fmt_ip6(buf,  pkt_dev->in6_saddr.s6_addr);

		ipv6_addr_copy(&pkt_dev->cur_in6_saddr, &pkt_dev->in6_saddr);

                if(debug) 
			printk("pktgen: src6 set to: %s\n", buf);
		
                i += len;
		sprintf(pg_result, "OK: src6=%s", buf);
		return count;
	}
	if (!strcmp(name, "src_min")) {
		len = strn_len(&user_buffer[i], sizeof(pkt_dev->src_min) - 1);
                if (len < 0) { return len; }
                if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
                buf[len] = 0;
                if (strcmp(buf, pkt_dev->src_min) != 0) {
                        memset(pkt_dev->src_min, 0, sizeof(pkt_dev->src_min));
                        strncpy(pkt_dev->src_min, buf, len);
                        pkt_dev->saddr_min = in_aton(pkt_dev->src_min);
                        pkt_dev->cur_saddr = pkt_dev->saddr_min;
                }
		if(debug)
			printk("pktgen: src_min set to: %s\n", pkt_dev->src_min);
		i += len;
		sprintf(pg_result, "OK: src_min=%s", pkt_dev->src_min);
		return count;
	}
	if (!strcmp(name, "src_max")) {
		len = strn_len(&user_buffer[i], sizeof(pkt_dev->src_max) - 1);
                if (len < 0) { return len; }
                if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
                buf[len] = 0;
                if (strcmp(buf, pkt_dev->src_max) != 0) {
                        memset(pkt_dev->src_max, 0, sizeof(pkt_dev->src_max));
                        strncpy(pkt_dev->src_max, buf, len);
                        pkt_dev->saddr_max = in_aton(pkt_dev->src_max);
                        pkt_dev->cur_saddr = pkt_dev->saddr_max;
                }
		if(debug)
			printk("pktgen: src_max set to: %s\n", pkt_dev->src_max);
		i += len;
		sprintf(pg_result, "OK: src_max=%s", pkt_dev->src_max);
		return count;
	}
	if (!strcmp(name, "dst_mac")) {
		char *v = valstr;
                unsigned char old_dmac[6];
		unsigned char *m = pkt_dev->dst_mac;
                memcpy(old_dmac, pkt_dev->dst_mac, 6);
                
		len = strn_len(&user_buffer[i], sizeof(valstr) - 1);
                if (len < 0) { return len; }
		memset(valstr, 0, sizeof(valstr));
		if( copy_from_user(valstr, &user_buffer[i], len))
			return -EFAULT;
		i += len;

		for(*m = 0;*v && m < pkt_dev->dst_mac + 6; v++) {
			if (*v >= '0' && *v <= '9') {
				*m *= 16;
				*m += *v - '0';
			}
			if (*v >= 'A' && *v <= 'F') {
				*m *= 16;
				*m += *v - 'A' + 10;
			}
			if (*v >= 'a' && *v <= 'f') {
				*m *= 16;
				*m += *v - 'a' + 10;
			}
			if (*v == ':') {
				m++;
				*m = 0;
			}
		}

		/* Set up Dest MAC */
                if (memcmp(old_dmac, pkt_dev->dst_mac, 6) != 0) 
                        memcpy(&(pkt_dev->hh[0]), pkt_dev->dst_mac, 6);
                
		sprintf(pg_result, "OK: dstmac");
		return count;
	}
	if (!strcmp(name, "src_mac")) {
		char *v = valstr;
		unsigned char *m = pkt_dev->src_mac;

		len = strn_len(&user_buffer[i], sizeof(valstr) - 1);
                if (len < 0) { return len; }
		memset(valstr, 0, sizeof(valstr));
		if( copy_from_user(valstr, &user_buffer[i], len)) 
			return -EFAULT;
		i += len;

		for(*m = 0;*v && m < pkt_dev->src_mac + 6; v++) {
			if (*v >= '0' && *v <= '9') {
				*m *= 16;
				*m += *v - '0';
			}
			if (*v >= 'A' && *v <= 'F') {
				*m *= 16;
				*m += *v - 'A' + 10;
			}
			if (*v >= 'a' && *v <= 'f') {
				*m *= 16;
				*m += *v - 'a' + 10;
			}
			if (*v == ':') {
				m++;
				*m = 0;
			}
		}	  

                sprintf(pg_result, "OK: srcmac");
		return count;
	}

        if (!strcmp(name, "clear_counters")) {
                pktgen_clear_counters(pkt_dev);
                sprintf(pg_result, "OK: Clearing counters.\n");
                return count;
        }

	if (!strcmp(name, "flows")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		if (value > MAX_CFLOWS)
			value = MAX_CFLOWS;

		pkt_dev->cflows = value;
		sprintf(pg_result, "OK: flows=%u", pkt_dev->cflows);
		return count;
	}

	if (!strcmp(name, "flowlen")) {
		len = num_arg(&user_buffer[i], 10, &value);
                if (len < 0) { return len; }
		i += len;
		pkt_dev->lflow = value;
		sprintf(pg_result, "OK: flowlen=%u", pkt_dev->lflow);
		return count;
	}
        
	sprintf(pkt_dev->result, "No such parameter \"%s\"", name);
	return -EINVAL;
}

static int proc_thread_read(char *buf , char **start, off_t offset,
                               int len, int *eof, void *data)
{
	char *p;
        struct pktgen_thread *t = (struct pktgen_thread*)(data);
        struct pktgen_dev *pkt_dev = NULL;


        if (!t) {
                printk("pktgen: ERROR: could not find thread in proc_thread_read\n");
                return -EINVAL;
        }

	p = buf;
	p += sprintf(p, "Name: %s  max_before_softirq: %d\n",
                     t->name, t->max_before_softirq);

        p += sprintf(p, "Running: ");
        
        if_lock(t);
        for(pkt_dev = t->if_list;pkt_dev; pkt_dev = pkt_dev->next) 
		if(pkt_dev->running)
			p += sprintf(p, "%s ", pkt_dev->ifname);
        
        p += sprintf(p, "\nStopped: ");

        for(pkt_dev = t->if_list;pkt_dev; pkt_dev = pkt_dev->next) 
		if(!pkt_dev->running)
			p += sprintf(p, "%s ", pkt_dev->ifname);

	if (t->result[0])
		p += sprintf(p, "\nResult: %s\n", t->result);
	else
		p += sprintf(p, "\nResult: NA\n");

	*eof = 1;

        if_unlock(t);

	return p - buf;
}

static int proc_thread_write(struct file *file, const char __user *user_buffer,
                                unsigned long count, void *data)
{
	int i = 0, max, len, ret;
	char name[40];
        struct pktgen_thread *t;
        char *pg_result;
        unsigned long value = 0;
        
	if (count < 1) {
		//	sprintf(pg_result, "Wrong command format");
		return -EINVAL;
	}
  
	max = count - i;
        len = count_trail_chars(&user_buffer[i], max);
        if (len < 0) 
		return len; 
     
	i += len;
  
	/* Read variable name */

	len = strn_len(&user_buffer[i], sizeof(name) - 1);
        if (len < 0)  
		return len; 
	
	memset(name, 0, sizeof(name));
	if (copy_from_user(name, &user_buffer[i], len))
		return -EFAULT;
	i += len;
  
	max = count -i;
	len = count_trail_chars(&user_buffer[i], max);
        if (len < 0)  
		return len; 
	
	i += len;

	if (debug) 
		printk("pktgen: t=%s, count=%lu\n", name, count);
        

        t = (struct pktgen_thread*)(data);
	if(!t) {
		printk("pktgen: ERROR: No thread\n");
		ret = -EINVAL;
		goto out;
	}

	pg_result = &(t->result[0]);

        if (!strcmp(name, "add_device")) {
                char f[32];
                memset(f, 0, 32);
		len = strn_len(&user_buffer[i], sizeof(f) - 1);
                if (len < 0) { 
			ret = len; 
			goto out;
		}
		if( copy_from_user(f, &user_buffer[i], len) )
			return -EFAULT;
		i += len;
		thread_lock();
                pktgen_add_device(t, f);
		thread_unlock();
                ret = count;
                sprintf(pg_result, "OK: add_device=%s", f);
		goto out;
	}

        if (!strcmp(name, "rem_device_all")) {
		thread_lock();
		t->control |= T_REMDEV;
		thread_unlock();
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/8);  /* Propagate thread->control  */
		ret = count;
                sprintf(pg_result, "OK: rem_device_all");
		goto out;
	}

        if (!strcmp(name, "max_before_softirq")) {
                len = num_arg(&user_buffer[i], 10, &value);
		thread_lock();
                t->max_before_softirq = value;
		thread_unlock();
                ret = count;
                sprintf(pg_result, "OK: max_before_softirq=%lu", value);
		goto out;
	}

	ret = -EINVAL;
 out:

	return ret;
}

static int create_proc_dir(void)
{
        int     len;
        /*  does proc_dir already exists */
        len = strlen(PG_PROC_DIR);

        for (pg_proc_dir = proc_net->subdir; pg_proc_dir; pg_proc_dir=pg_proc_dir->next) {
                if ((pg_proc_dir->namelen == len) &&
		    (! memcmp(pg_proc_dir->name, PG_PROC_DIR, len))) 
                        break;
        }
        
        if (!pg_proc_dir) 
                pg_proc_dir = create_proc_entry(PG_PROC_DIR, S_IFDIR, proc_net);
        
        if (!pg_proc_dir) 
                return -ENODEV;
        
        return 0;
}

static int remove_proc_dir(void)
{
        remove_proc_entry(PG_PROC_DIR, proc_net);
        return 0;
}

/* Think find or remove for NN */
static struct pktgen_dev *__pktgen_NN_threads(const char* ifname, int remove) 
{
	struct pktgen_thread *t;
	struct pktgen_dev *pkt_dev = NULL;

        t = pktgen_threads;
                
	while (t) {
		pkt_dev = pktgen_find_dev(t, ifname);
		if (pkt_dev) {
		                if(remove) { 
				        if_lock(t);
				        pktgen_remove_device(t, pkt_dev);
				        if_unlock(t);
				}
			break;
		}
		t = t->next;
	}
        return pkt_dev;
}

static struct pktgen_dev *pktgen_NN_threads(const char* ifname, int remove) 
{
	struct pktgen_dev *pkt_dev = NULL;
	thread_lock();
	pkt_dev = __pktgen_NN_threads(ifname, remove);
        thread_unlock();
	return pkt_dev;
}

static int pktgen_device_event(struct notifier_block *unused, unsigned long event, void *ptr) 
{
	struct net_device *dev = (struct net_device *)(ptr);

	/* It is OK that we do not hold the group lock right now,
	 * as we run under the RTNL lock.
	 */

	switch (event) {
	case NETDEV_CHANGEADDR:
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
	case NETDEV_UP:
		/* Ignore for now */
		break;
		
	case NETDEV_UNREGISTER:
                pktgen_NN_threads(dev->name, REMOVE);
		break;
	};

	return NOTIFY_DONE;
}

/* Associate pktgen_dev with a device. */

static struct net_device* pktgen_setup_dev(struct pktgen_dev *pkt_dev) {
	struct net_device *odev;

	/* Clean old setups */

	if (pkt_dev->odev) {
		dev_put(pkt_dev->odev);
                pkt_dev->odev = NULL;
        }

	odev = dev_get_by_name(pkt_dev->ifname);

	if (!odev) {
		printk("pktgen: no such netdevice: \"%s\"\n", pkt_dev->ifname);
		goto out;
	}
	if (odev->type != ARPHRD_ETHER) {
		printk("pktgen: not an ethernet device: \"%s\"\n", pkt_dev->ifname);
		goto out_put;
	}
	if (!netif_running(odev)) {
		printk("pktgen: device is down: \"%s\"\n", pkt_dev->ifname);
		goto out_put;
	}
	pkt_dev->odev = odev;
	
        return pkt_dev->odev;

out_put:
	dev_put(odev);
out:
 	return NULL;

}

/* Read pkt_dev from the interface and set up internal pktgen_dev
 * structure to have the right information to create/send packets
 */
static void pktgen_setup_inject(struct pktgen_dev *pkt_dev)
{
	/* Try once more, just in case it works now. */
        if (!pkt_dev->odev) 
                pktgen_setup_dev(pkt_dev);
        
        if (!pkt_dev->odev) {
                printk("pktgen: ERROR: pkt_dev->odev == NULL in setup_inject.\n");
                sprintf(pkt_dev->result, "ERROR: pkt_dev->odev == NULL in setup_inject.\n");
                return;
        }
        
        /* Default to the interface's mac if not explicitly set. */

	if ((pkt_dev->src_mac[0] == 0) && 
	    (pkt_dev->src_mac[1] == 0) && 
	    (pkt_dev->src_mac[2] == 0) && 
	    (pkt_dev->src_mac[3] == 0) && 
	    (pkt_dev->src_mac[4] == 0) && 
	    (pkt_dev->src_mac[5] == 0)) {

	       memcpy(&(pkt_dev->hh[6]), pkt_dev->odev->dev_addr, 6);
       }
        /* Set up Dest MAC */
        memcpy(&(pkt_dev->hh[0]), pkt_dev->dst_mac, 6);

        /* Set up pkt size */
        pkt_dev->cur_pkt_size = pkt_dev->min_pkt_size;
	
	if(pkt_dev->flags & F_IPV6) {
		/*
		 * Skip this automatic address setting until locks or functions 
		 * gets exported
		 */

#ifdef NOTNOW
		int i, set = 0, err=1;
		struct inet6_dev *idev;

		for(i=0; i< IN6_ADDR_HSIZE; i++)
			if(pkt_dev->cur_in6_saddr.s6_addr[i]) {
				set = 1;
				break;
			}

		if(!set) {
			
			/*
			 * Use linklevel address if unconfigured.
			 *
			 * use ipv6_get_lladdr if/when it's get exported
			 */


			read_lock(&addrconf_lock);
			if ((idev = __in6_dev_get(pkt_dev->odev)) != NULL) {
				struct inet6_ifaddr *ifp;

				read_lock_bh(&idev->lock);
				for (ifp=idev->addr_list; ifp; ifp=ifp->if_next) {
					if (ifp->scope == IFA_LINK && !(ifp->flags&IFA_F_TENTATIVE)) {
						ipv6_addr_copy(&pkt_dev->cur_in6_saddr, &ifp->addr);
						err = 0;
						break;
					}
				}
				read_unlock_bh(&idev->lock);
			}
			read_unlock(&addrconf_lock);
			if(err)	printk("pktgen: ERROR: IPv6 link address not availble.\n");
		}
#endif
	} 
	else {
		pkt_dev->saddr_min = 0;
		pkt_dev->saddr_max = 0;
		if (strlen(pkt_dev->src_min) == 0) {
			
			struct in_device *in_dev; 

			rcu_read_lock();
			in_dev = __in_dev_get(pkt_dev->odev);
			if (in_dev) {
				if (in_dev->ifa_list) {
					pkt_dev->saddr_min = in_dev->ifa_list->ifa_address;
					pkt_dev->saddr_max = pkt_dev->saddr_min;
				}
				__in_dev_put(in_dev);	
			}
			rcu_read_unlock();
		}
		else {
			pkt_dev->saddr_min = in_aton(pkt_dev->src_min);
			pkt_dev->saddr_max = in_aton(pkt_dev->src_max);
		}

		pkt_dev->daddr_min = in_aton(pkt_dev->dst_min);
		pkt_dev->daddr_max = in_aton(pkt_dev->dst_max);
	}
        /* Initialize current values. */
        pkt_dev->cur_dst_mac_offset = 0;
        pkt_dev->cur_src_mac_offset = 0;
        pkt_dev->cur_saddr = pkt_dev->saddr_min;
        pkt_dev->cur_daddr = pkt_dev->daddr_min;
        pkt_dev->cur_udp_dst = pkt_dev->udp_dst_min;
        pkt_dev->cur_udp_src = pkt_dev->udp_src_min;
	pkt_dev->nflows = 0;
}

static void spin(struct pktgen_dev *pkt_dev, __u64 spin_until_us)
{
	__u64 start;
	__u64 now;

	start = now = getCurUs();
	printk(KERN_INFO "sleeping for %d\n", (int)(spin_until_us - now));
	while (now < spin_until_us) {
		/* TODO: optimise sleeping behavior */
		if (spin_until_us - now > (1000000/HZ)+1) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		} else if (spin_until_us - now > 100) {
			do_softirq();
			if (!pkt_dev->running)
				return;
			if (need_resched())
				schedule();
		}

		now = getCurUs();
	}

	pkt_dev->idle_acc += now - start;
}


/* Increment/randomize headers according to flags and current values
 * for IP src/dest, UDP src/dst port, MAC-Addr src/dst
 */
static void mod_cur_headers(struct pktgen_dev *pkt_dev) {        
        __u32 imn;
        __u32 imx;
	int  flow = 0;

	if(pkt_dev->cflows)  {
		flow = pktgen_random() % pkt_dev->cflows;
		
		if (pkt_dev->flows[flow].count > pkt_dev->lflow)
			pkt_dev->flows[flow].count = 0;
	}						


	/*  Deal with source MAC */
        if (pkt_dev->src_mac_count > 1) {
                __u32 mc;
                __u32 tmp;

                if (pkt_dev->flags & F_MACSRC_RND) 
                        mc = pktgen_random() % (pkt_dev->src_mac_count);
                else {
                        mc = pkt_dev->cur_src_mac_offset++;
                        if (pkt_dev->cur_src_mac_offset > pkt_dev->src_mac_count) 
                                pkt_dev->cur_src_mac_offset = 0;
                }

                tmp = pkt_dev->src_mac[5] + (mc & 0xFF);
                pkt_dev->hh[11] = tmp;
                tmp = (pkt_dev->src_mac[4] + ((mc >> 8) & 0xFF) + (tmp >> 8));
                pkt_dev->hh[10] = tmp;
                tmp = (pkt_dev->src_mac[3] + ((mc >> 16) & 0xFF) + (tmp >> 8));
                pkt_dev->hh[9] = tmp;
                tmp = (pkt_dev->src_mac[2] + ((mc >> 24) & 0xFF) + (tmp >> 8));
                pkt_dev->hh[8] = tmp;
                tmp = (pkt_dev->src_mac[1] + (tmp >> 8));
                pkt_dev->hh[7] = tmp;        
        }

        /*  Deal with Destination MAC */
        if (pkt_dev->dst_mac_count > 1) {
                __u32 mc;
                __u32 tmp;

                if (pkt_dev->flags & F_MACDST_RND) 
                        mc = pktgen_random() % (pkt_dev->dst_mac_count);

                else {
                        mc = pkt_dev->cur_dst_mac_offset++;
                        if (pkt_dev->cur_dst_mac_offset > pkt_dev->dst_mac_count) {
                                pkt_dev->cur_dst_mac_offset = 0;
                        }
                }

                tmp = pkt_dev->dst_mac[5] + (mc & 0xFF);
                pkt_dev->hh[5] = tmp;
                tmp = (pkt_dev->dst_mac[4] + ((mc >> 8) & 0xFF) + (tmp >> 8));
                pkt_dev->hh[4] = tmp;
                tmp = (pkt_dev->dst_mac[3] + ((mc >> 16) & 0xFF) + (tmp >> 8));
                pkt_dev->hh[3] = tmp;
                tmp = (pkt_dev->dst_mac[2] + ((mc >> 24) & 0xFF) + (tmp >> 8));
                pkt_dev->hh[2] = tmp;
                tmp = (pkt_dev->dst_mac[1] + (tmp >> 8));
                pkt_dev->hh[1] = tmp;        
        }

        if (pkt_dev->udp_src_min < pkt_dev->udp_src_max) {
                if (pkt_dev->flags & F_UDPSRC_RND) 
                        pkt_dev->cur_udp_src = ((pktgen_random() % (pkt_dev->udp_src_max - pkt_dev->udp_src_min)) + pkt_dev->udp_src_min);

                else {
			pkt_dev->cur_udp_src++;
			if (pkt_dev->cur_udp_src >= pkt_dev->udp_src_max)
				pkt_dev->cur_udp_src = pkt_dev->udp_src_min;
                }
        }

        if (pkt_dev->udp_dst_min < pkt_dev->udp_dst_max) {
                if (pkt_dev->flags & F_UDPDST_RND) {
                        pkt_dev->cur_udp_dst = ((pktgen_random() % (pkt_dev->udp_dst_max - pkt_dev->udp_dst_min)) + pkt_dev->udp_dst_min);
                }
                else {
			pkt_dev->cur_udp_dst++;
			if (pkt_dev->cur_udp_dst >= pkt_dev->udp_dst_max) 
				pkt_dev->cur_udp_dst = pkt_dev->udp_dst_min;
                }
        }

	if (!(pkt_dev->flags & F_IPV6)) {

		if ((imn = ntohl(pkt_dev->saddr_min)) < (imx = ntohl(pkt_dev->saddr_max))) {
			__u32 t;
			if (pkt_dev->flags & F_IPSRC_RND) 
				t = ((pktgen_random() % (imx - imn)) + imn);
			else {
				t = ntohl(pkt_dev->cur_saddr);
				t++;
				if (t > imx) {
					t = imn;
				}
			}
			pkt_dev->cur_saddr = htonl(t);
		}
		
		if (pkt_dev->cflows && pkt_dev->flows[flow].count != 0) {
			pkt_dev->cur_daddr = pkt_dev->flows[flow].cur_daddr;
		} else {

			if ((imn = ntohl(pkt_dev->daddr_min)) < (imx = ntohl(pkt_dev->daddr_max))) {
				__u32 t;
				if (pkt_dev->flags & F_IPDST_RND) {

					t = ((pktgen_random() % (imx - imn)) + imn);
					t = htonl(t);

					while( LOOPBACK(t) || MULTICAST(t) || BADCLASS(t) || ZERONET(t) ||  LOCAL_MCAST(t) ) {
						t = ((pktgen_random() % (imx - imn)) + imn);
						t = htonl(t);
					}
					pkt_dev->cur_daddr = t;
				}
				
				else {
					t = ntohl(pkt_dev->cur_daddr);
					t++;
					if (t > imx) {
						t = imn;
					}
					pkt_dev->cur_daddr = htonl(t);
				}
			}
			if(pkt_dev->cflows) {	
				pkt_dev->flows[flow].cur_daddr = pkt_dev->cur_daddr;
				pkt_dev->nflows++;
			}
		}
	}
	else /* IPV6 * */
	{
		if(pkt_dev->min_in6_daddr.s6_addr32[0] == 0 &&
		   pkt_dev->min_in6_daddr.s6_addr32[1] == 0 &&
		   pkt_dev->min_in6_daddr.s6_addr32[2] == 0 &&
		   pkt_dev->min_in6_daddr.s6_addr32[3] == 0);
		else {
			int i;

			/* Only random destinations yet */

			for(i=0; i < 4; i++) {
				pkt_dev->cur_in6_daddr.s6_addr32[i] =
					((pktgen_random() |
					  pkt_dev->min_in6_daddr.s6_addr32[i]) &
					 pkt_dev->max_in6_daddr.s6_addr32[i]);
			}
 		}
	}

        if (pkt_dev->min_pkt_size < pkt_dev->max_pkt_size) {
                __u32 t;
                if (pkt_dev->flags & F_TXSIZE_RND) {
                        t = ((pktgen_random() % (pkt_dev->max_pkt_size - pkt_dev->min_pkt_size))
                             + pkt_dev->min_pkt_size);
                }
                else {
			t = pkt_dev->cur_pkt_size + 1;
			if (t > pkt_dev->max_pkt_size) 
				t = pkt_dev->min_pkt_size;
                }
                pkt_dev->cur_pkt_size = t;
        }

	pkt_dev->flows[flow].count++;
}


static struct sk_buff *fill_packet_ipv4(struct net_device *odev, 
				   struct pktgen_dev *pkt_dev)
{
	struct sk_buff *skb = NULL;
	__u8 *eth;
	struct udphdr *udph;
	int datalen, iplen;
	struct iphdr *iph;
        struct pktgen_hdr *pgh = NULL;
        
	skb = alloc_skb(pkt_dev->cur_pkt_size + 64 + 16, GFP_ATOMIC);
	if (!skb) {
		sprintf(pkt_dev->result, "No memory");
		return NULL;
	}

	skb_reserve(skb, 16);

	/*  Reserve for ethernet and IP header  */
	eth = (__u8 *) skb_push(skb, 14);
	iph = (struct iphdr *)skb_put(skb, sizeof(struct iphdr));
	udph = (struct udphdr *)skb_put(skb, sizeof(struct udphdr));

        /* Update any of the values, used when we're incrementing various
         * fields.
         */
        mod_cur_headers(pkt_dev);

	memcpy(eth, pkt_dev->hh, 12);
	*(u16*)&eth[12] = __constant_htons(ETH_P_IP);

	datalen = pkt_dev->cur_pkt_size - 14 - 20 - 8; /* Eth + IPh + UDPh */
	if (datalen < sizeof(struct pktgen_hdr)) 
		datalen = sizeof(struct pktgen_hdr);
        
	udph->source = htons(pkt_dev->cur_udp_src);
	udph->dest = htons(pkt_dev->cur_udp_dst);
	udph->len = htons(datalen + 8); /* DATA + udphdr */
	udph->check = 0;  /* No checksum */

	iph->ihl = 5;
	iph->version = 4;
	iph->ttl = 32;
	iph->tos = 0;
	iph->protocol = IPPROTO_UDP; /* UDP */
	iph->saddr = pkt_dev->cur_saddr;
	iph->daddr = pkt_dev->cur_daddr;
	iph->frag_off = 0;
	iplen = 20 + 8 + datalen;
	iph->tot_len = htons(iplen);
	iph->check = 0;
	iph->check = ip_fast_csum((void *) iph, iph->ihl);
	skb->protocol = __constant_htons(ETH_P_IP);
	skb->mac.raw = ((u8 *)iph) - 14;
	skb->dev = odev;
	skb->pkt_type = PACKET_HOST;

	if (pkt_dev->nfrags <= 0) 
                pgh = (struct pktgen_hdr *)skb_put(skb, datalen);
	else {
		int frags = pkt_dev->nfrags;
		int i;

                pgh = (struct pktgen_hdr*)(((char*)(udph)) + 8);
                
		if (frags > MAX_SKB_FRAGS)
			frags = MAX_SKB_FRAGS;
		if (datalen > frags*PAGE_SIZE) {
			skb_put(skb, datalen-frags*PAGE_SIZE);
			datalen = frags*PAGE_SIZE;
		}

		i = 0;
		while (datalen > 0) {
			struct page *page = alloc_pages(GFP_KERNEL, 0);
			skb_shinfo(skb)->frags[i].page = page;
			skb_shinfo(skb)->frags[i].page_offset = 0;
			skb_shinfo(skb)->frags[i].size =
				(datalen < PAGE_SIZE ? datalen : PAGE_SIZE);
			datalen -= skb_shinfo(skb)->frags[i].size;
			skb->len += skb_shinfo(skb)->frags[i].size;
			skb->data_len += skb_shinfo(skb)->frags[i].size;
			i++;
			skb_shinfo(skb)->nr_frags = i;
		}

		while (i < frags) {
			int rem;

			if (i == 0)
				break;

			rem = skb_shinfo(skb)->frags[i - 1].size / 2;
			if (rem == 0)
				break;

			skb_shinfo(skb)->frags[i - 1].size -= rem;

			skb_shinfo(skb)->frags[i] = skb_shinfo(skb)->frags[i - 1];
			get_page(skb_shinfo(skb)->frags[i].page);
			skb_shinfo(skb)->frags[i].page = skb_shinfo(skb)->frags[i - 1].page;
			skb_shinfo(skb)->frags[i].page_offset += skb_shinfo(skb)->frags[i - 1].size;
			skb_shinfo(skb)->frags[i].size = rem;
			i++;
			skb_shinfo(skb)->nr_frags = i;
		}
	}

        /* Stamp the time, and sequence number, convert them to network byte order */

        if (pgh) {
              struct timeval timestamp;
	      
	      pgh->pgh_magic = htonl(PKTGEN_MAGIC);
	      pgh->seq_num   = htonl(pkt_dev->seq_num);
	      
	      do_gettimeofday(&timestamp);
	      pgh->tv_sec    = htonl(timestamp.tv_sec);
	      pgh->tv_usec   = htonl(timestamp.tv_usec);
        }
        pkt_dev->seq_num++;
        
	return skb;
}

/*
 * scan_ip6, fmt_ip taken from dietlibc-0.21 
 * Author Felix von Leitner <felix-dietlibc@fefe.de>
 *
 * Slightly modified for kernel. 
 * Should be candidate for net/ipv4/utils.c
 * --ro
 */

static unsigned int scan_ip6(const char *s,char ip[16])
{
	unsigned int i;
	unsigned int len=0;
	unsigned long u;
	char suffix[16];
	unsigned int prefixlen=0;
	unsigned int suffixlen=0;
	__u32 tmp;

	for (i=0; i<16; i++) ip[i]=0;

	for (;;) {
		if (*s == ':') {
			len++;
			if (s[1] == ':') {        /* Found "::", skip to part 2 */
				s+=2;
				len++;
				break;
			}
			s++;
		}
		{
			char *tmp;
			u=simple_strtoul(s,&tmp,16);
			i=tmp-s;
		}

		if (!i) return 0;
		if (prefixlen==12 && s[i]=='.') {

			/* the last 4 bytes may be written as IPv4 address */

			tmp = in_aton(s);
			memcpy((struct in_addr*)(ip+12), &tmp, sizeof(tmp));
			return i+len;
		}
		ip[prefixlen++] = (u >> 8);
		ip[prefixlen++] = (u & 255);
		s += i; len += i;
		if (prefixlen==16)
			return len;
	}

/* part 2, after "::" */
	for (;;) {
		if (*s == ':') {
			if (suffixlen==0)
				break;
			s++;
			len++;
		} else if (suffixlen!=0)
			break;
		{
			char *tmp;
			u=simple_strtol(s,&tmp,16);
			i=tmp-s;
		}
		if (!i) {
			if (*s) len--;
			break;
		}
		if (suffixlen+prefixlen<=12 && s[i]=='.') {
			tmp = in_aton(s);
			memcpy((struct in_addr*)(suffix+suffixlen), &tmp, sizeof(tmp));
			suffixlen+=4;
			len+=strlen(s);
			break;
		}
		suffix[suffixlen++] = (u >> 8);
		suffix[suffixlen++] = (u & 255);
		s += i; len += i;
		if (prefixlen+suffixlen==16)
			break;
	}
	for (i=0; i<suffixlen; i++)
		ip[16-suffixlen+i] = suffix[i];
	return len;
}

static char tohex(char hexdigit) {
	return hexdigit>9?hexdigit+'a'-10:hexdigit+'0';
}

static int fmt_xlong(char* s,unsigned int i) {
	char* bak=s;
	*s=tohex((i>>12)&0xf); if (s!=bak || *s!='0') ++s;
	*s=tohex((i>>8)&0xf); if (s!=bak || *s!='0') ++s;
	*s=tohex((i>>4)&0xf); if (s!=bak || *s!='0') ++s;
	*s=tohex(i&0xf);
	return s-bak+1;
}

static unsigned int fmt_ip6(char *s,const char ip[16]) {
	unsigned int len;
	unsigned int i;
	unsigned int temp;
	unsigned int compressing;
	int j;

	len = 0; compressing = 0;
	for (j=0; j<16; j+=2) {

#ifdef V4MAPPEDPREFIX
		if (j==12 && !memcmp(ip,V4mappedprefix,12)) {
			inet_ntoa_r(*(struct in_addr*)(ip+12),s);
			temp=strlen(s);
			return len+temp;
		}
#endif
		temp = ((unsigned long) (unsigned char) ip[j] << 8) +
			(unsigned long) (unsigned char) ip[j+1];
		if (temp == 0) {
			if (!compressing) {
				compressing=1;
				if (j==0) {
					*s++=':'; ++len;
				}
			}
		} else {
			if (compressing) {
				compressing=0;
				*s++=':'; ++len;
			}
			i = fmt_xlong(s,temp); len += i; s += i;
			if (j<14) {
				*s++ = ':';
				++len;
			}
		}
	}
	if (compressing) {
		*s++=':'; ++len;
	}
	*s=0;
	return len;
}

static struct sk_buff *fill_packet_ipv6(struct net_device *odev, 
				   struct pktgen_dev *pkt_dev)
{
	struct sk_buff *skb = NULL;
	__u8 *eth;
	struct udphdr *udph;
	int datalen;
	struct ipv6hdr *iph;
        struct pktgen_hdr *pgh = NULL;
        
	skb = alloc_skb(pkt_dev->cur_pkt_size + 64 + 16, GFP_ATOMIC);
	if (!skb) {
		sprintf(pkt_dev->result, "No memory");
		return NULL;
	}

	skb_reserve(skb, 16);

	/*  Reserve for ethernet and IP header  */
	eth = (__u8 *) skb_push(skb, 14);
	iph = (struct ipv6hdr *)skb_put(skb, sizeof(struct ipv6hdr));
	udph = (struct udphdr *)skb_put(skb, sizeof(struct udphdr));


        /* Update any of the values, used when we're incrementing various
         * fields.
         */
	mod_cur_headers(pkt_dev);

	
	memcpy(eth, pkt_dev->hh, 12);
	*(u16*)&eth[12] = __constant_htons(ETH_P_IPV6);
	
        
	datalen = pkt_dev->cur_pkt_size-14- 
		sizeof(struct ipv6hdr)-sizeof(struct udphdr); /* Eth + IPh + UDPh */

	if (datalen < sizeof(struct pktgen_hdr)) { 
		datalen = sizeof(struct pktgen_hdr);
		if (net_ratelimit())
			printk(KERN_INFO "pktgen: increased datalen to %d\n", datalen);
	}

	udph->source = htons(pkt_dev->cur_udp_src);
	udph->dest = htons(pkt_dev->cur_udp_dst);
	udph->len = htons(datalen + sizeof(struct udphdr)); 
	udph->check = 0;  /* No checksum */

	 *(u32*)iph = __constant_htonl(0x60000000); /* Version + flow */

	iph->hop_limit = 32;

	iph->payload_len = htons(sizeof(struct udphdr) + datalen);
	iph->nexthdr = IPPROTO_UDP;

	ipv6_addr_copy(&iph->daddr, &pkt_dev->cur_in6_daddr);
	ipv6_addr_copy(&iph->saddr, &pkt_dev->cur_in6_saddr);

	skb->mac.raw = ((u8 *)iph) - 14;
	skb->protocol = __constant_htons(ETH_P_IPV6);
	skb->dev = odev;
	skb->pkt_type = PACKET_HOST;

	if (pkt_dev->nfrags <= 0) 
                pgh = (struct pktgen_hdr *)skb_put(skb, datalen);
	else {
		int frags = pkt_dev->nfrags;
		int i;

                pgh = (struct pktgen_hdr*)(((char*)(udph)) + 8);
                
		if (frags > MAX_SKB_FRAGS)
			frags = MAX_SKB_FRAGS;
		if (datalen > frags*PAGE_SIZE) {
			skb_put(skb, datalen-frags*PAGE_SIZE);
			datalen = frags*PAGE_SIZE;
		}

		i = 0;
		while (datalen > 0) {
			struct page *page = alloc_pages(GFP_KERNEL, 0);
			skb_shinfo(skb)->frags[i].page = page;
			skb_shinfo(skb)->frags[i].page_offset = 0;
			skb_shinfo(skb)->frags[i].size =
				(datalen < PAGE_SIZE ? datalen : PAGE_SIZE);
			datalen -= skb_shinfo(skb)->frags[i].size;
			skb->len += skb_shinfo(skb)->frags[i].size;
			skb->data_len += skb_shinfo(skb)->frags[i].size;
			i++;
			skb_shinfo(skb)->nr_frags = i;
		}

		while (i < frags) {
			int rem;

			if (i == 0)
				break;

			rem = skb_shinfo(skb)->frags[i - 1].size / 2;
			if (rem == 0)
				break;

			skb_shinfo(skb)->frags[i - 1].size -= rem;

			skb_shinfo(skb)->frags[i] = skb_shinfo(skb)->frags[i - 1];
			get_page(skb_shinfo(skb)->frags[i].page);
			skb_shinfo(skb)->frags[i].page = skb_shinfo(skb)->frags[i - 1].page;
			skb_shinfo(skb)->frags[i].page_offset += skb_shinfo(skb)->frags[i - 1].size;
			skb_shinfo(skb)->frags[i].size = rem;
			i++;
			skb_shinfo(skb)->nr_frags = i;
		}
	}

        /* Stamp the time, and sequence number, convert them to network byte order */
	/* should we update cloned packets too ? */
        if (pgh) {
              struct timeval timestamp;
	      
	      pgh->pgh_magic = htonl(PKTGEN_MAGIC);
	      pgh->seq_num   = htonl(pkt_dev->seq_num);
	      
	      do_gettimeofday(&timestamp);
	      pgh->tv_sec    = htonl(timestamp.tv_sec);
	      pgh->tv_usec   = htonl(timestamp.tv_usec);
        }
        pkt_dev->seq_num++;
        
	return skb;
}

static inline struct sk_buff *fill_packet(struct net_device *odev, 
				   struct pktgen_dev *pkt_dev)
{
	if(pkt_dev->flags & F_IPV6) 
		return fill_packet_ipv6(odev, pkt_dev);
	else
		return fill_packet_ipv4(odev, pkt_dev);
}

static void pktgen_clear_counters(struct pktgen_dev *pkt_dev) 
{
        pkt_dev->seq_num = 1;
        pkt_dev->idle_acc = 0;
	pkt_dev->sofar = 0;
        pkt_dev->tx_bytes = 0;
        pkt_dev->errors = 0;
}

/* Set up structure for sending pkts, clear counters */

static void pktgen_run(struct pktgen_thread *t)
{
        struct pktgen_dev *pkt_dev = NULL;
	int started = 0;

	PG_DEBUG(printk("pktgen: entering pktgen_run. %p\n", t));

	if_lock(t);
        for (pkt_dev = t->if_list; pkt_dev; pkt_dev = pkt_dev->next ) {

		/*
		 * setup odev and create initial packet.
		 */
		pktgen_setup_inject(pkt_dev);

		if(pkt_dev->odev) { 
			pktgen_clear_counters(pkt_dev);
			pkt_dev->running = 1; /* Cranke yeself! */
			pkt_dev->skb = NULL;
			pkt_dev->started_at = getCurUs();
			pkt_dev->next_tx_us = getCurUs(); /* Transmit immediately */
			pkt_dev->next_tx_ns = 0;
			
			strcpy(pkt_dev->result, "Starting");
			started++;
		}
		else 
			strcpy(pkt_dev->result, "Error starting");
	}
	if_unlock(t);
	if(started) t->control &= ~(T_STOP);
}

static void pktgen_stop_all_threads_ifs(void)
{
        struct pktgen_thread *t = pktgen_threads;

	PG_DEBUG(printk("pktgen: entering pktgen_stop_all_threads.\n"));

	thread_lock();
	while(t) {
		pktgen_stop(t);
		t = t->next;
	}
       thread_unlock();
}

static int thread_is_running(struct pktgen_thread *t )
{
        struct pktgen_dev *next;
        int res = 0;

        for(next=t->if_list; next; next=next->next) { 
		if(next->running) {
			res = 1;
			break;
		}
        }
        return res;
}

static int pktgen_wait_thread_run(struct pktgen_thread *t )
{
        if_lock(t);

        while(thread_is_running(t)) {

                if_unlock(t);

		msleep_interruptible(100); 

                if (signal_pending(current)) 
                        goto signal;
                if_lock(t);
        }
        if_unlock(t);
        return 1;
 signal:
        return 0;
}

static int pktgen_wait_all_threads_run(void)
{
	struct pktgen_thread *t = pktgen_threads;
	int sig = 1;
	
	while (t) {
		sig = pktgen_wait_thread_run(t);
		if( sig == 0 ) break;
		thread_lock();
		t=t->next;
		thread_unlock();
	}
	if(sig == 0) {
		thread_lock();
		while (t) {
			t->control |= (T_STOP);
			t=t->next;
		}
		thread_unlock();
	}
	return sig;
}

static void pktgen_run_all_threads(void)
{
        struct pktgen_thread *t = pktgen_threads;

	PG_DEBUG(printk("pktgen: entering pktgen_run_all_threads.\n"));

	thread_lock();

	while(t) {
		t->control |= (T_RUN);
		t = t->next;
	}
	thread_unlock();

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ/8);  /* Propagate thread->control  */
			
	pktgen_wait_all_threads_run();
}


static void show_results(struct pktgen_dev *pkt_dev, int nr_frags)
{
       __u64 total_us, bps, mbps, pps, idle;
       char *p = pkt_dev->result;

       total_us = pkt_dev->stopped_at - pkt_dev->started_at;

       idle = pkt_dev->idle_acc;

       p += sprintf(p, "OK: %llu(c%llu+d%llu) usec, %llu (%dbyte,%dfrags)\n",
                    (unsigned long long) total_us, 
		    (unsigned long long)(total_us - idle), 
		    (unsigned long long) idle,
                    (unsigned long long) pkt_dev->sofar, 
		    pkt_dev->cur_pkt_size, nr_frags);

       pps = pkt_dev->sofar * USEC_PER_SEC;

       while ((total_us >> 32) != 0) {
               pps >>= 1;
               total_us >>= 1;
       }

       do_div(pps, total_us);
       
       bps = pps * 8 * pkt_dev->cur_pkt_size;

       mbps = bps;
       do_div(mbps, 1000000);
       p += sprintf(p, "  %llupps %lluMb/sec (%llubps) errors: %llu",
                    (unsigned long long) pps, 
		    (unsigned long long) mbps, 
		    (unsigned long long) bps, 
		    (unsigned long long) pkt_dev->errors);
}
 

/* Set stopped-at timer, remove from running list, do counters & statistics */

static int pktgen_stop_device(struct pktgen_dev *pkt_dev) 
{
	
        if (!pkt_dev->running) {
                printk("pktgen: interface: %s is already stopped\n", pkt_dev->ifname);
                return -EINVAL;
        }

        pkt_dev->stopped_at = getCurUs();
        pkt_dev->running = 0;

	show_results(pkt_dev, skb_shinfo(pkt_dev->skb)->nr_frags);

	if (pkt_dev->skb) 
		kfree_skb(pkt_dev->skb);

	pkt_dev->skb = NULL;
	
        return 0;
}

static struct pktgen_dev *next_to_run(struct pktgen_thread *t )
{
	struct pktgen_dev *next, *best = NULL;
        
	if_lock(t);

	for(next=t->if_list; next ; next=next->next) {
		if(!next->running) continue;
		if(best == NULL) best=next;
		else if ( next->next_tx_us < best->next_tx_us) 
			best =  next;
	}
	if_unlock(t);
        return best;
}

static void pktgen_stop(struct pktgen_thread *t) {
        struct pktgen_dev *next = NULL;

	PG_DEBUG(printk("pktgen: entering pktgen_stop.\n"));

        if_lock(t);

        for(next=t->if_list; next; next=next->next)
                pktgen_stop_device(next);

        if_unlock(t);
}

static void pktgen_rem_all_ifs(struct pktgen_thread *t) 
{
        struct pktgen_dev *cur, *next = NULL;
        
        /* Remove all devices, free mem */
 
        if_lock(t);

        for(cur=t->if_list; cur; cur=next) { 
		next = cur->next;
		pktgen_remove_device(t, cur);
	}

        if_unlock(t);
}

static void pktgen_rem_thread(struct pktgen_thread *t) 
{
        /* Remove from the thread list */

	struct pktgen_thread *tmp = pktgen_threads;

        if (strlen(t->fname))
                remove_proc_entry(t->fname, NULL);

       thread_lock();

	if (tmp == t)
		pktgen_threads = tmp->next;
	else {
		while (tmp) {
			if (tmp->next == t) {
				tmp->next = t->next;
				t->next = NULL;
				break;
			}
			tmp = tmp->next;
		}
	}
        thread_unlock();
}

static __inline__ void pktgen_xmit(struct pktgen_dev *pkt_dev)
{
	struct net_device *odev = NULL;
	__u64 idle_start = 0;
	int ret;

	odev = pkt_dev->odev;
	
	if (pkt_dev->delay_us || pkt_dev->delay_ns) {
		u64 now;

		now = getCurUs();
		if (now < pkt_dev->next_tx_us)
			spin(pkt_dev, pkt_dev->next_tx_us);

		/* This is max DELAY, this has special meaning of
		 * "never transmit"
		 */
		if (pkt_dev->delay_us == 0x7FFFFFFF) {
			pkt_dev->next_tx_us = getCurUs() + pkt_dev->delay_us;
			pkt_dev->next_tx_ns = pkt_dev->delay_ns;
			goto out;
		}
	}
	
	if (netif_queue_stopped(odev) || need_resched()) {
		idle_start = getCurUs();
		
		if (!netif_running(odev)) {
			pktgen_stop_device(pkt_dev);
			goto out;
		}
		if (need_resched()) 
			schedule();
		
		pkt_dev->idle_acc += getCurUs() - idle_start;
		
		if (netif_queue_stopped(odev)) {
			pkt_dev->next_tx_us = getCurUs(); /* TODO */
			pkt_dev->next_tx_ns = 0;
			goto out; /* Try the next interface */
		}
	}
	
	if (pkt_dev->last_ok || !pkt_dev->skb) {
		if ((++pkt_dev->clone_count >= pkt_dev->clone_skb ) || (!pkt_dev->skb)) {
			/* build a new pkt */
			if (pkt_dev->skb) 
				kfree_skb(pkt_dev->skb);
			
			pkt_dev->skb = fill_packet(odev, pkt_dev);
			if (pkt_dev->skb == NULL) {
				printk("pktgen: ERROR: couldn't allocate skb in fill_packet.\n");
				schedule();
				pkt_dev->clone_count--; /* back out increment, OOM */
				goto out;
			}
			pkt_dev->allocated_skbs++;
			pkt_dev->clone_count = 0; /* reset counter */
		}
	}
	
	spin_lock_bh(&odev->xmit_lock);
	if (!netif_queue_stopped(odev)) {

		atomic_inc(&(pkt_dev->skb->users));
retry_now:
		ret = odev->hard_start_xmit(pkt_dev->skb, odev);
		if (likely(ret == NETDEV_TX_OK)) {
			pkt_dev->last_ok = 1;    
			pkt_dev->sofar++;
			pkt_dev->seq_num++;
			pkt_dev->tx_bytes += pkt_dev->cur_pkt_size;
			
		} else if (ret == NETDEV_TX_LOCKED 
			   && (odev->features & NETIF_F_LLTX)) {
			cpu_relax();
			goto retry_now;
		} else {  /* Retry it next time */
			
			atomic_dec(&(pkt_dev->skb->users));
			
			if (debug && net_ratelimit())
				printk(KERN_INFO "pktgen: Hard xmit error\n");
			
			pkt_dev->errors++;
			pkt_dev->last_ok = 0;
		}

		pkt_dev->next_tx_us = getCurUs();
		pkt_dev->next_tx_ns = 0;

		pkt_dev->next_tx_us += pkt_dev->delay_us;
		pkt_dev->next_tx_ns += pkt_dev->delay_ns;

		if (pkt_dev->next_tx_ns > 1000) {
			pkt_dev->next_tx_us++;
			pkt_dev->next_tx_ns -= 1000;
		}
	} 

	else {  /* Retry it next time */
                pkt_dev->last_ok = 0;
                pkt_dev->next_tx_us = getCurUs(); /* TODO */
		pkt_dev->next_tx_ns = 0;
        }

	spin_unlock_bh(&odev->xmit_lock);
	
	/* If pkt_dev->count is zero, then run forever */
	if ((pkt_dev->count != 0) && (pkt_dev->sofar >= pkt_dev->count)) {
		if (atomic_read(&(pkt_dev->skb->users)) != 1) {
			idle_start = getCurUs();
			while (atomic_read(&(pkt_dev->skb->users)) != 1) {
				if (signal_pending(current)) {
					break;
				}
				schedule();
			}
			pkt_dev->idle_acc += getCurUs() - idle_start;
		}
                
		/* Done with this */
		pktgen_stop_device(pkt_dev);
	} 
 out:;
 }

/* 
 * Main loop of the thread goes here
 */

static void pktgen_thread_worker(struct pktgen_thread *t) 
{
	DEFINE_WAIT(wait);
        struct pktgen_dev *pkt_dev = NULL;
	int cpu = t->cpu;
	sigset_t tmpsig;
	u32 max_before_softirq;
        u32 tx_since_softirq = 0;

	daemonize("pktgen/%d", cpu);

        /* Block all signals except SIGKILL, SIGSTOP and SIGTERM */

        spin_lock_irq(&current->sighand->siglock);
        tmpsig = current->blocked;
        siginitsetinv(&current->blocked, 
                      sigmask(SIGKILL) | 
                      sigmask(SIGSTOP)| 
                      sigmask(SIGTERM));

        recalc_sigpending();
        spin_unlock_irq(&current->sighand->siglock);

	/* Migrate to the right CPU */
	set_cpus_allowed(current, cpumask_of_cpu(cpu));
        if (smp_processor_id() != cpu)
                BUG();

	init_waitqueue_head(&t->queue);

	t->control &= ~(T_TERMINATE);
	t->control &= ~(T_RUN);
	t->control &= ~(T_STOP);
	t->control &= ~(T_REMDEV);

        t->pid = current->pid;        

        PG_DEBUG(printk("pktgen: starting pktgen/%d:  pid=%d\n", cpu, current->pid));

	max_before_softirq = t->max_before_softirq;
        
        __set_current_state(TASK_INTERRUPTIBLE);
        mb();

        while (1) {
		
		__set_current_state(TASK_RUNNING);

		/*
		 * Get next dev to xmit -- if any.
		 */

                pkt_dev = next_to_run(t);
                
                if (pkt_dev) {

			pktgen_xmit(pkt_dev);

			/*
			 * We like to stay RUNNING but must also give
			 * others fair share.
			 */

			tx_since_softirq += pkt_dev->last_ok;

			if (tx_since_softirq > max_before_softirq) {
				if (local_softirq_pending())
					do_softirq();
				tx_since_softirq = 0;
			}
		} else {
			prepare_to_wait(&(t->queue), &wait, TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/10);
			finish_wait(&(t->queue), &wait);
		}

                /* 
		 * Back from sleep, either due to the timeout or signal.
		 * We check if we have any "posted" work for us.
		 */

                if (t->control & T_TERMINATE || signal_pending(current)) 
                        /* we received a request to terminate ourself */
                        break;
		

		if(t->control & T_STOP) {
			pktgen_stop(t);
			t->control &= ~(T_STOP);
		}

		if(t->control & T_RUN) {
			pktgen_run(t);
			t->control &= ~(T_RUN);
		}

		if(t->control & T_REMDEV) {
			pktgen_rem_all_ifs(t);
			t->control &= ~(T_REMDEV);
		}

		if (need_resched()) 
			schedule();
        } 

        PG_DEBUG(printk("pktgen: %s stopping all device\n", t->name));
        pktgen_stop(t);

        PG_DEBUG(printk("pktgen: %s removing all device\n", t->name));
        pktgen_rem_all_ifs(t);

        PG_DEBUG(printk("pktgen: %s removing thread.\n", t->name));
        pktgen_rem_thread(t);
}

static struct pktgen_dev *pktgen_find_dev(struct pktgen_thread *t, const char* ifname) 
{
        struct pktgen_dev *pkt_dev = NULL;
        if_lock(t);

        for(pkt_dev=t->if_list; pkt_dev; pkt_dev = pkt_dev->next ) {
                if (strcmp(pkt_dev->ifname, ifname) == 0) {
                        break;
                }
        }

        if_unlock(t);
	PG_DEBUG(printk("pktgen: find_dev(%s) returning %p\n", ifname,pkt_dev));
        return pkt_dev;
}

/* 
 * Adds a dev at front of if_list. 
 */

static int add_dev_to_thread(struct pktgen_thread *t, struct pktgen_dev *pkt_dev) 
{
	int rv = 0;
	
        if_lock(t);

        if (pkt_dev->pg_thread) {
                printk("pktgen: ERROR:  already assigned to a thread.\n");
                rv = -EBUSY;
                goto out;
        }
	pkt_dev->next =t->if_list; t->if_list=pkt_dev;
        pkt_dev->pg_thread = t;
	pkt_dev->running = 0;

 out:
        if_unlock(t);        
        return rv;
}

/* Called under thread lock */

static int pktgen_add_device(struct pktgen_thread *t, const char* ifname) 
{
        struct pktgen_dev *pkt_dev;
	
	/* We don't allow a device to be on several threads */

	if( (pkt_dev = __pktgen_NN_threads(ifname, FIND)) == NULL) {
						   
		pkt_dev = kmalloc(sizeof(struct pktgen_dev), GFP_KERNEL);
                if (!pkt_dev) 
                        return -ENOMEM;

                memset(pkt_dev, 0, sizeof(struct pktgen_dev));

		pkt_dev->flows = vmalloc(MAX_CFLOWS*sizeof(struct flow_state));
		if (pkt_dev->flows == NULL) {
			kfree(pkt_dev);
			return -ENOMEM;
		}
		memset(pkt_dev->flows, 0, MAX_CFLOWS*sizeof(struct flow_state));

		pkt_dev->min_pkt_size = ETH_ZLEN;
                pkt_dev->max_pkt_size = ETH_ZLEN;
                pkt_dev->nfrags = 0;
                pkt_dev->clone_skb = pg_clone_skb_d;
                pkt_dev->delay_us = pg_delay_d / 1000;
                pkt_dev->delay_ns = pg_delay_d % 1000;
                pkt_dev->count = pg_count_d;
                pkt_dev->sofar = 0;
                pkt_dev->udp_src_min = 9; /* sink port */
                pkt_dev->udp_src_max = 9;
                pkt_dev->udp_dst_min = 9;
                pkt_dev->udp_dst_max = 9;

                strncpy(pkt_dev->ifname, ifname, 31);
                sprintf(pkt_dev->fname, "net/%s/%s", PG_PROC_DIR, ifname);

                if (! pktgen_setup_dev(pkt_dev)) {
                        printk("pktgen: ERROR: pktgen_setup_dev failed.\n");
			if (pkt_dev->flows)
				vfree(pkt_dev->flows);
                        kfree(pkt_dev);
                        return -ENODEV;
                }

                pkt_dev->proc_ent = create_proc_entry(pkt_dev->fname, 0600, NULL);
                if (!pkt_dev->proc_ent) {
                        printk("pktgen: cannot create %s procfs entry.\n", pkt_dev->fname);
			if (pkt_dev->flows)
				vfree(pkt_dev->flows);
                        kfree(pkt_dev);
                        return -EINVAL;
                }
                pkt_dev->proc_ent->read_proc = proc_if_read;
                pkt_dev->proc_ent->write_proc = proc_if_write;
                pkt_dev->proc_ent->data = (void*)(pkt_dev);
		pkt_dev->proc_ent->owner = THIS_MODULE;

                return add_dev_to_thread(t, pkt_dev);
        }
        else {
                printk("pktgen: ERROR: interface already used.\n");
                return -EBUSY;
        }
}

static struct pktgen_thread *pktgen_find_thread(const char* name) 
{
        struct pktgen_thread *t = NULL;

       thread_lock();

        t = pktgen_threads;
        while (t) {
                if (strcmp(t->name, name) == 0) 
                        break;

                t = t->next;
        }
        thread_unlock();
        return t;
}

static int pktgen_create_thread(const char* name, int cpu) 
{
        struct pktgen_thread *t = NULL;

        if (strlen(name) > 31) {
                printk("pktgen: ERROR:  Thread name cannot be more than 31 characters.\n");
                return -EINVAL;
        }
        
        if (pktgen_find_thread(name)) {
                printk("pktgen: ERROR: thread: %s already exists\n", name);
                return -EINVAL;
        }

        t = (struct pktgen_thread*)(kmalloc(sizeof(struct pktgen_thread), GFP_KERNEL));
        if (!t) {
                printk("pktgen: ERROR: out of memory, can't create new thread.\n");
                return -ENOMEM;
        }

        memset(t, 0, sizeof(struct pktgen_thread));
        strcpy(t->name, name);
        spin_lock_init(&t->if_lock);
	t->cpu = cpu;
        
        sprintf(t->fname, "net/%s/%s", PG_PROC_DIR, t->name);
        t->proc_ent = create_proc_entry(t->fname, 0600, NULL);
        if (!t->proc_ent) {
                printk("pktgen: cannot create %s procfs entry.\n", t->fname);
                kfree(t);
                return -EINVAL;
        }
        t->proc_ent->read_proc = proc_thread_read;
        t->proc_ent->write_proc = proc_thread_write;
        t->proc_ent->data = (void*)(t);
        t->proc_ent->owner = THIS_MODULE;

        t->next = pktgen_threads;
        pktgen_threads = t;

	if (kernel_thread((void *) pktgen_thread_worker, (void *) t, 
			  CLONE_FS | CLONE_FILES | CLONE_SIGHAND) < 0)
		printk("pktgen: kernel_thread() failed for cpu %d\n", t->cpu);

	return 0;
}

/* 
 * Removes a device from the thread if_list. 
 */
static void _rem_dev_from_if_list(struct pktgen_thread *t, struct pktgen_dev *pkt_dev) 
{
	struct pktgen_dev *i, *prev = NULL;

	i = t->if_list;

	while(i) {
		if(i == pkt_dev) {
			if(prev) prev->next = i->next;
			else t->if_list = NULL;
			break;
		}
		prev = i;
		i=i->next;
	}
}

static int pktgen_remove_device(struct pktgen_thread *t, struct pktgen_dev *pkt_dev) 
{

	PG_DEBUG(printk("pktgen: remove_device pkt_dev=%p\n", pkt_dev));

        if (pkt_dev->running) { 
                printk("pktgen:WARNING: trying to remove a running interface, stopping it now.\n");
                pktgen_stop_device(pkt_dev);
        }
        
        /* Dis-associate from the interface */

	if (pkt_dev->odev) {
		dev_put(pkt_dev->odev);
                pkt_dev->odev = NULL;
        }
        
	/* And update the thread if_list */

	_rem_dev_from_if_list(t, pkt_dev);

        /* Clean up proc file system */

        if (strlen(pkt_dev->fname)) 
                remove_proc_entry(pkt_dev->fname, NULL);

	if (pkt_dev->flows)
		vfree(pkt_dev->flows);
	kfree(pkt_dev);
        return 0;
}

static int __init pg_init(void) 
{
	int cpu;
	printk(version);

        module_fname[0] = 0;

	create_proc_dir();

        sprintf(module_fname, "net/%s/pgctrl", PG_PROC_DIR);
        module_proc_ent = create_proc_entry(module_fname, 0600, NULL);
        if (!module_proc_ent) {
                printk("pktgen: ERROR: cannot create %s procfs entry.\n", module_fname);
                return -EINVAL;
        }

        module_proc_ent->proc_fops =  &pktgen_fops;
        module_proc_ent->data = NULL;

	/* Register us to receive netdevice events */
	register_netdevice_notifier(&pktgen_notifier_block);
        
	for (cpu = 0; cpu < NR_CPUS ; cpu++) {
		char buf[30];

		if (!cpu_online(cpu))
			continue;

                sprintf(buf, "kpktgend_%i", cpu);
                pktgen_create_thread(buf, cpu);
        }
        return 0;        
}

static void __exit pg_cleanup(void)
{
	wait_queue_head_t queue;
	init_waitqueue_head(&queue);

        /* Stop all interfaces & threads */        

        while (pktgen_threads) {
                struct pktgen_thread *t = pktgen_threads;
                pktgen_threads->control |= (T_TERMINATE);

		wait_event_interruptible_timeout(queue, (t != pktgen_threads), HZ);
        }

        /* Un-register us from receiving netdevice events */
	unregister_netdevice_notifier(&pktgen_notifier_block);

        /* Clean up proc file system */

        remove_proc_entry(module_fname, NULL);
        
	remove_proc_dir();
}


module_init(pg_init);
module_exit(pg_cleanup);

MODULE_AUTHOR("Robert Olsson <robert.olsson@its.uu.se");
MODULE_DESCRIPTION("Packet Generator tool");
MODULE_LICENSE("GPL");
module_param(pg_count_d, int, 0);
module_param(pg_delay_d, int, 0);
module_param(pg_clone_skb_d, int, 0);
module_param(debug, int, 0);

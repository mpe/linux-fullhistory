/*
 *	Linux NET3 Bridge Support
 *
 *	Originally by John Hayes (Network Plumbing).
 *	Minor hacks to get it to run with 1.3.x by Alan Cox <Alan.Cox@linux.org>
 *	More hacks to be able to switch protocols on and off by Christoph Lameter
 *	<clameter@debian.org>
 *	Software and more Documentation for the bridge is available from ftp.debian.org
 *	in the bridge package or at ftp.fuller.edu/Linux/bridge
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Fixes:
 *	Yury Shevchuk	:	Bridge with non bridging ports
 *	Jean-Rene Peulve: jr.peulve@aix.pacwan.net 		Jan/Feb 98
 *			support Linux 2.0
 *			Handle Receive config bpdu
 *			kick mark_bh to send Spanning Tree pdus
 *			bridgeId comparison using htonl()
 *			make STP interoperable with other vendors
 *			wrong test in root_selection()
 *			add more STP debug info 
 *			some performance improvments
 *			do not clear bridgeId.mac  while setting priority
 *			do not reset port priority when starting bridge
 *			make port priority from user value and port number
 *			maintains user port state out of device state
 *			broacast/multicast storm limitation
 *			forwarding statistics
 *			stop br_tick when bridge is turn off
 *			add local MACs in avl_tree to forward up stack
 *			fake receive on right port for IP/ARP 
 *			ages tree even if packet does not cross bridge
 *			add BRCMD_DISPLAY_FDB (ioctl for now)
 *
 *	Alan Cox:	Merged Jean-Rene's stuff, reformatted stuff a bit
 *			so blame me first if its broken ;)
 *
 *	Todo:
 *		Don't bring up devices automatically. Start ports disabled
 *	and use a netlink notifier so a daemon can maintain the bridge
 *	port group (could we also do multiple groups ????).
 *		A nice /proc file interface.
 *		Put the path costs in the port info and devices.
 *		Put the bridge port number in the device structure for speed.
 *		Bridge SNMP stats.
 *	
 */
 
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/version.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <net/br.h>
#include <linux/proc_fs.h>

#ifndef min
#define min(a, b) (((a) <= (b)) ? (a) : (b))
#endif

static void transmit_config(int port_no);
static int root_bridge(void);
static int supersedes_port_info(int port_no, Config_bpdu *config);
static void record_config_information(int port_no, Config_bpdu *config);
static void record_config_timeout_values(Config_bpdu *config);
static void config_bpdu_generation(void);
static int designated_port(int port_no);
static void reply(int port_no);
static void transmit_tcn(void);
static void configuration_update(void);
static void root_selection(void);
static void designated_port_selection(void);
static void become_designated_port(int port_no);
static void port_state_selection(void);
static void make_forwarding(int port_no);
static void topology_change_detection(void);
static void topology_change_acknowledged(void);
static void acknowledge_topology_change(int port_no);
static void make_blocking(int port_no);
static void set_port_state(int port_no, int state);
static void received_config_bpdu(int port_no, Config_bpdu *config);
static void received_tcn_bpdu(int port_no, Tcn_bpdu *tcn);
static void hello_timer_expiry(void);
static void message_age_timer_expiry(int port_no);
static void forward_delay_timer_expiry(int port_no);
static int designated_for_some_port(void);
static void tcn_timer_expiry(void);
static void topology_change_timer_expiry(void);
static void hold_timer_expiry(int port_no);
static void br_init_port(int port_no);
static void enable_port(int port_no);
static void disable_port(int port_no);
static void set_bridge_priority(bridge_id_t *new_bridge_id);
static void set_port_priority(int port_no);
static void set_path_cost(int port_no, unsigned short path_cost);
static void start_hello_timer(void);
static void stop_hello_timer(void);
static int hello_timer_expired(void);
static void start_tcn_timer(void);
static void stop_tcn_timer(void);
static int tcn_timer_expired(void);
static void start_topology_change_timer(void);
static void stop_topology_change_timer(void);
static int topology_change_timer_expired(void);
static void start_message_age_timer(int port_no, unsigned short message_age);
static void stop_message_age_timer(int port_no);
static int message_age_timer_expired(int port_no);
static void start_forward_delay_timer(int port_no);
static void stop_forward_delay_timer(int port_no);
static int forward_delay_timer_expired(int port_no);
static void start_hold_timer(int port_no);
static void stop_hold_timer(int port_no);
static int hold_timer_expired(int port_no);
static int br_device_event(struct notifier_block *dnot, unsigned long event, void *ptr);
static void br_tick(unsigned long arg);
static int br_forward(struct sk_buff *skb, int port);	/* 3.7 */
static int br_port_cost(struct device *dev);	/* 4.10.2 */
static void br_bpdu(struct sk_buff *skb, int port); /* consumes skb */
static int br_cmp(unsigned int *a, unsigned int *b);
static int send_tcn_bpdu(int port_no, Tcn_bpdu *bpdu);
static int send_config_bpdu(int port_no, Config_bpdu *config_bpdu);
static int find_port(struct device *dev);
static void br_add_local_mac(unsigned char *mac);
static int br_flood(struct sk_buff *skb, int port);
static int br_drop(struct sk_buff *skb);
static int br_learn(struct sk_buff *skb, int port);	/* 3.8 */

static unsigned char bridge_ula[ETH_ALEN] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };
static Bridge_data     bridge_info;			  /* (4.5.3)	 */
Port_data       port_info[All_ports];		  /* (4.5.5)	 */

/* JRP: fdb cache 1/port save kmalloc/kfree on every frame */
struct fdb	*newfdb[All_ports];
int allocated_fdb_cnt = 0;

/* broacast/multicast storm limitation */
int max_mcast_per_period = MAX_MCAST_PER_PERIOD;
int mcast_hold_time	 = MCAST_HOLD_TIME;

/* JRP: next two bpdu are copied to skbuff so we need only 1 of each */
static Config_bpdu	config_bpdu;
static Tcn_bpdu		tcn_bpdu;
static unsigned char	port_priority[All_ports];
static unsigned char	user_port_state[All_ports];

static Timer    hello_timer;			  /* (4.5.4.1)	 */
static Timer    tcn_timer;			  /* (4.5.4.2)	 */
static Timer    topology_change_timer;		  /* (4.5.4.3)	 */
static Timer    message_age_timer[All_ports];	  /* (4.5.6.1)	 */
static Timer    forward_delay_timer[All_ports];	  /* (4.5.6.2)	 */
static Timer    hold_timer[All_ports];		  /* (4.5.6.3)	 */

/* entries timeout after this many seconds */
unsigned int fdb_aging_time = FDB_TIMEOUT; 

struct br_stat br_stats;
#define br_stats_cnt br_stats.packet_cnts

static struct timer_list tl; /* for 1 second timer... */

/*
 * the following structure is required so that we receive
 * event notifications when network devices are enabled and
 * disabled (ifconfig up and down).
 */
static struct notifier_block br_dev_notifier={
	br_device_event,
	NULL,
	0
};

/*
 * Implementation of Protocol specific bridging
 *
 * The protocols to be bridged or not to be bridged are stored in a hashed array. This is the old type
 * of unlinked hash array where one simply takes the next cell if the one the hash function points to
 * is occupied.
 */

#define BR_PROTOCOL_HASH(x) (x % BR_MAX_PROTOCOLS)

/* Checks if that protocol type is to be bridged */

int br_protocol_ok(unsigned short protocol)
{
	unsigned x;
	
	/* See if protocol statistics are to be kept */
	if (br_stats.flags & BR_PROT_STATS)
	{
		for(x=0;x<BR_MAX_PROT_STATS && br_stats.prot_id[x]!=protocol && br_stats.prot_id[x];x++);
		if (x<BR_MAX_PROT_STATS)
		{
			br_stats.prot_id[x]=protocol;br_stats.prot_counter[x]++;
		}
	}

	for (x=BR_PROTOCOL_HASH(protocol); br_stats.protocols[x]!=0;) 
	{
		if (br_stats.protocols[x]==protocol)
			return !br_stats.policy;
		x++;
		if (x==BR_MAX_PROTOCOLS)
			x=0;
	}
	return br_stats.policy;
}

/* Add a protocol to be handled opposite to the standard policy of the bridge */

static int br_add_exempt_protocol(unsigned short p)
{
	unsigned x;
	if (p == 0) return -EINVAL;
	if (br_stats.exempt_protocols > BR_MAX_PROTOCOLS-2) return -EXFULL;
	for (x=BR_PROTOCOL_HASH(p);br_stats.protocols[x]!=0;) {
		if (br_stats.protocols[x]==p) return 0;	/* Attempt to add the protocol a second time */
		x++;
		if (x==BR_MAX_PROTOCOLS) x=0;
	}
	br_stats.protocols[x]=p;
	br_stats.exempt_protocols++;
	return 0;
}

/* Valid Policies are 0=No Protocols bridged 1=Bridge all protocols */
static int br_set_policy(int policy)
{
	if (policy>1) return -EINVAL;
	br_stats.policy=policy;
	/* Policy change means initializing the exempt table */
	memset(br_stats.protocols,0,sizeof(br_stats.protocols));
	br_stats.exempt_protocols = 0;
	return 0;
}


/** Elements of Procedure (4.6) **/

/*
 * this section of code was graciously borrowed from the IEEE 802.1d
 * specification section 4.9.1 starting on pg 69.  It has been
 * modified somewhat to fit within our framework and structure.  It
 * implements the spanning tree algorithm that is the heart of the
 * 802.1d bridging protocol.
 */

static void transmit_config(int port_no)	  /* (4.6.1)	 */
{
	if (hold_timer[port_no].active) {	  /* (4.6.1.3.1)	 */
		port_info[port_no].config_pending = TRUE;	/* (4.6.1.3.1)	 */
	} else {				  /* (4.6.1.3.2)	 */
		config_bpdu.type = BPDU_TYPE_CONFIG;
		config_bpdu.root_id = bridge_info.designated_root;
		/* (4.6.1.3.2(1)) */
		config_bpdu.root_path_cost = bridge_info.root_path_cost;
		/* (4.6.1.3.2(2)) */
		config_bpdu.bridge_id = bridge_info.bridge_id;
		/* (4.6.1.3.2(3)) */
		config_bpdu.port_id = port_info[port_no].port_id;
		/*
		 * (4.6.1.3.2(4))
		 */
		if (root_bridge()) {
			config_bpdu.message_age = Zero;	/* (4.6.1.3.2(5)) */
		} else {
			config_bpdu.message_age
				= message_age_timer[bridge_info.root_port].value
				+ Message_age_increment;	/* (4.6.1.3.2(6)) */
		}

		config_bpdu.max_age = bridge_info.max_age;/* (4.6.1.3.2(7)) */
		config_bpdu.hello_time = bridge_info.hello_time;
		config_bpdu.forward_delay = bridge_info.forward_delay;
		config_bpdu.top_change_ack = 
			port_info[port_no].top_change_ack;
							/* (4.6.1.3.2(8)) */
		port_info[port_no].top_change_ack = 0;

		config_bpdu.top_change = 
			bridge_info.top_change; 	/* (4.6.1.3.2(9)) */

		send_config_bpdu(port_no, &config_bpdu);
		port_info[port_no].config_pending = FALSE;	/* (4.6.1.3.2(10)) */
		start_hold_timer(port_no);	  /* (4.6.1.3.2(11)) */
	}
/* JRP: we want the frame to be xmitted even if no other traffic.
 *	net_bh() will do a dev_transmit() that kicks all devices
 */
	mark_bh(NET_BH);
}

static int root_bridge(void)
{
	return (br_cmp(bridge_info.designated_root.BRIDGE_ID,
		 bridge_info.bridge_id.BRIDGE_ID)?FALSE:TRUE);
}

static int supersedes_port_info(int port_no, Config_bpdu *config)	  /* (4.6.2.2)	 */
{
	return (
		(br_cmp(config->root_id.BRIDGE_ID,
		 port_info[port_no].designated_root.BRIDGE_ID) < 0)	/* (4.6.2.2.1)	 */ 
		||
		((br_cmp(config->root_id.BRIDGE_ID,
		  port_info[port_no].designated_root.BRIDGE_ID) == 0
		  )
		 &&
		 ((config->root_path_cost
		   < port_info[port_no].designated_cost	/* (4.6.2.2.2)	 */
		   )
		  ||
		  ((config->root_path_cost
		    == port_info[port_no].designated_cost
		    )
		   &&
		   ((br_cmp(config->bridge_id.BRIDGE_ID,
		     port_info[port_no].designated_bridge.BRIDGE_ID) < 0	/* (4.6.2.2.3)    */
		     )
		    ||
		    ((br_cmp(config->bridge_id.BRIDGE_ID,
		      port_info[port_no].designated_bridge.BRIDGE_ID) == 0
		      )				  /* (4.6.2.2.4)	 */
		     &&
		     ((br_cmp(config->bridge_id.BRIDGE_ID,
			bridge_info.bridge_id.BRIDGE_ID) != 0
		       )			  /* (4.6.2.2.4(1)) */
		      ||
		      (config->port_id <=
		       port_info[port_no].designated_port
		       )			  /* (4.6.2.2.4(2)) */
		      ))))))
		);
}

static void record_config_information(int port_no, Config_bpdu *config)	  /* (4.6.2)	 */
{
	port_info[port_no].designated_root = config->root_id;	/* (4.6.2.3.1)   */
	port_info[port_no].designated_cost = config->root_path_cost;
	port_info[port_no].designated_bridge = config->bridge_id;
	port_info[port_no].designated_port = config->port_id;
	start_message_age_timer(port_no, config->message_age);	/* (4.6.2.3.2)   */
}

static void record_config_timeout_values(Config_bpdu *config)		  /* (4.6.3)	 */
{
	bridge_info.max_age = config->max_age;	  /* (4.6.3.3)	 */
	bridge_info.hello_time = config->hello_time;
	bridge_info.forward_delay = config->forward_delay;
	bridge_info.top_change = config->top_change;
}

static void config_bpdu_generation(void)
{						  /* (4.6.4)	 */
	int             port_no;
	for (port_no = One; port_no <= No_of_ports; port_no++) {	/* (4.6.4.3) */
		if (designated_port(port_no)	  /* (4.6.4.3)	 */
				&&
				(port_info[port_no].state != Disabled)
			) {
			transmit_config(port_no); /* (4.6.4.3)	 */
		}				  /* (4.6.1.2)	 */
	}
}

static int designated_port(int port_no)
{
	return ((br_cmp(port_info[port_no].designated_bridge.BRIDGE_ID,
		 bridge_info.bridge_id.BRIDGE_ID) == 0
		 )
		&&
		(port_info[port_no].designated_port
		 == port_info[port_no].port_id
		 )
		);
}

static void reply(int port_no)					  /* (4.6.5)	 */
{
	transmit_config(port_no);		  /* (4.6.5.3)	 */
}

static void transmit_tcn(void)
{						  /* (4.6.6)	 */
	int             port_no;

	port_no = bridge_info.root_port;
	tcn_bpdu.type = BPDU_TYPE_TOPO_CHANGE;
	send_tcn_bpdu(port_no, &tcn_bpdu);	/* (4.6.6.3)     */
}

static void configuration_update(void)	/* (4.6.7) */
{
	root_selection();			  /* (4.6.7.3.1)	 */
	/* (4.6.8.2)	 */
	designated_port_selection();		  /* (4.6.7.3.2)	 */
	/* (4.6.9.2)	 */
}

static void root_selection(void)
{						  /* (4.6.8) */
	int             root_port;
	int             port_no;
	root_port = No_port;
	for (port_no = One; port_no <= No_of_ports; port_no++) {	/* (4.6.8.3.1) */
		if (((!designated_port(port_no))
		     &&
		     (port_info[port_no].state != Disabled)
		     &&
		(br_cmp(port_info[port_no].designated_root.BRIDGE_ID,
			bridge_info.bridge_id.BRIDGE_ID) < 0)
			)
				&&
				((root_port == No_port)
				 ||
				 (br_cmp(port_info[port_no].designated_root.BRIDGE_ID,
				  port_info[root_port].designated_root.BRIDGE_ID) < 0
				  )
				 ||
				 ((br_cmp(port_info[port_no].designated_root.BRIDGE_ID,
				   port_info[root_port].designated_root.BRIDGE_ID) == 0
				   )
				  &&
				  (((port_info[port_no].designated_cost
				     + port_info[port_no].path_cost
				     )
				    <
				    (port_info[root_port].designated_cost
				     + port_info[root_port].path_cost
				     )		  /* (4.6.8.3.1(2)) */
				    )
				   ||
				   (((port_info[port_no].designated_cost
				      + port_info[port_no].path_cost
				      )
				     ==
				     (port_info[root_port].designated_cost
				      + port_info[root_port].path_cost
				      )
				     )
				    &&
				    ((br_cmp(port_info[port_no].designated_bridge.BRIDGE_ID,
				    port_info[root_port].designated_bridge.BRIDGE_ID) < 0
				      )		  /* (4.6.8.3.1(3)) */
				     ||
				     ((br_cmp(port_info[port_no].designated_bridge.BRIDGE_ID,
				   port_info[root_port].designated_bridge.BRIDGE_ID) == 0
				       )
				      &&
				      ((port_info[port_no].designated_port
				      < port_info[root_port].designated_port
					)	  /* (4.6.8.3.1(4)) */
				       ||
				       ((port_info[port_no].designated_port
/* JRP: was missing an "=" ! */	      == port_info[root_port].designated_port
					 )
					&&
					(port_info[port_no].port_id
					 < port_info[root_port].port_id
					 )	  /* (4.6.8.3.1(5)) */
					))))))))) {
			root_port = port_no;
		}
	}
	bridge_info.root_port = root_port;	  /* (4.6.8.3.1)	 */

	if (root_port == No_port) {		  /* (4.6.8.3.2)	 */
#ifdef DEBUG_STP
		if (br_stats.flags & BR_DEBUG)
			printk(KERN_DEBUG "root_selection: becomes root\n");
#endif
		bridge_info.designated_root = bridge_info.bridge_id;
		/* (4.6.8.3.2(1)) */
		bridge_info.root_path_cost = Zero;/* (4.6.8.3.2(2)) */
	} else {				  /* (4.6.8.3.3)	 */
		bridge_info.designated_root = port_info[root_port].designated_root;
		/* (4.6.8.3.3(1)) */
		bridge_info.root_path_cost = (port_info[root_port].designated_cost
					    + port_info[root_port].path_cost
			);			  /* (4.6.8.3.3(2)) */
	}
}

static void designated_port_selection(void)
{						  /* (4.6.9)	 */
	int             port_no;

	for (port_no = One; port_no <= No_of_ports; port_no++) {	/* (4.6.9.3)	 */
		if(port_info[port_no].state == Disabled)
			continue;
		if (designated_port(port_no)	  /* (4.6.9.3.1)	 */
				||
				(
				 br_cmp(port_info[port_no].designated_root.BRIDGE_ID,
				 bridge_info.designated_root.BRIDGE_ID) != 0
				 )
				||
				(bridge_info.root_path_cost
				 < port_info[port_no].designated_cost
				 )		  /* (4.6.9.3.3)	 */
				||
				((bridge_info.root_path_cost
				  == port_info[port_no].designated_cost
				  )
				 &&
				 ((br_cmp(bridge_info.bridge_id.BRIDGE_ID,
				   port_info[port_no].designated_bridge.BRIDGE_ID) < 0
				   )		  /* (4.6.9.3.4)	 */
				  ||
				  ((br_cmp(bridge_info.bridge_id.BRIDGE_ID,
				    port_info[port_no].designated_bridge.BRIDGE_ID) == 0
				    )
				   &&
				   (port_info[port_no].port_id
				    <= port_info[port_no].designated_port
				    )		  /* (4.6.9.3.5)	 */
				   )))) {
			become_designated_port(port_no);	/* (4.6.10.3.2.2) */
		}
	}
}

static void become_designated_port(int port_no)
{						  /* (4.6.10)	 */

	/* (4.6.10.3.1) */
	port_info[port_no].designated_root = bridge_info.designated_root;
	/* (4.6.10.3.2) */
	port_info[port_no].designated_cost = bridge_info.root_path_cost;
	/* (4.6.10.3.3) */
	port_info[port_no].designated_bridge = bridge_info.bridge_id;
	/* (4.6.10.3.4) */
	port_info[port_no].designated_port = port_info[port_no].port_id;
}

static void port_state_selection(void)
{						  /* (4.6.11) */
	int             port_no;
	char		*state_str;
	for (port_no = One; port_no <= No_of_ports; port_no++) {

		if(port_info[port_no].state == Disabled)
			continue;
		if (port_no == bridge_info.root_port) {	/* (4.6.11.3.1) */
			state_str = "root";
			port_info[port_no].config_pending = FALSE;	/* (4.6.11.3.1(1)) */
			port_info[port_no].top_change_ack = 0;
			make_forwarding(port_no); /* (4.6.11.3.1(2)) */
		} else if (designated_port(port_no)) {	/* (4.6.11.3.2) */
			state_str = "designated";
			stop_message_age_timer(port_no);	/* (4.6.11.3.2(1)) */
			make_forwarding(port_no); /* (4.6.11.3.2(2)) */
		} else {			  /* (4.6.11.3.3) */
			state_str = "blocking";
			port_info[port_no].config_pending = FALSE;	/* (4.6.11.3.3(1)) */
			port_info[port_no].top_change_ack = 0;
			make_blocking(port_no);	  /* (4.6.11.3.3(2)) */
		}
#ifdef DEBUG_STP
		if (br_stats.flags & BR_DEBUG)
			printk(KERN_DEBUG "port_state_selection: becomes %s port %d\n",
				state_str, port_no);
#endif
		
	}

}

static void make_forwarding(int port_no)
{						  /* (4.6.12) */
	if (port_info[port_no].state == Blocking) {	/* (4.6.12.3) */
		set_port_state(port_no, Listening);	/* (4.6.12.3.1) */
		start_forward_delay_timer(port_no);	/* (4.6.12.3.2) */
	}
}

static void topology_change_detection(void)
{						  /* (4.6.14)       */
#ifdef DEBUG_STP
	if ((br_stats.flags & BR_DEBUG)
	    && (bridge_info.top_change_detected == 0))
		printk(KERN_DEBUG "topology_change_detected\n");
#endif
	if (root_bridge()) {			  /* (4.6.14.3.1)   */
		bridge_info.top_change = 1;
		start_topology_change_timer();	  /* (4.6.14.3.1(2)) */
	} else if (!(bridge_info.top_change_detected)) {
		transmit_tcn();			  /* (4.6.14.3.2(1)) */
		start_tcn_timer();		  /* (4.6.14.3.2(2)) */
	}
	bridge_info.top_change_detected = 1;	/* (4.6.14.3.3) */
}

static void topology_change_acknowledged(void)
{						  /* (4.6.15) */
#ifdef DEBUG_STP
	if (br_stats.flags & BR_DEBUG)
		printk(KERN_DEBUG "topology_change_acked\n");
#endif
	bridge_info.top_change_detected = 0;	/* (4.6.15.3.1) */
	stop_tcn_timer();			  /* (4.6.15.3.2) */
}

static void acknowledge_topology_change(int port_no)
{						  /* (4.6.16) */
	port_info[port_no].top_change_ack = 1;
	transmit_config(port_no);		  /* (4.6.16.3.2) */
}

static void make_blocking(int port_no)				  /* (4.6.13)	 */
{

	if ((port_info[port_no].state != Disabled)
			&&
			(port_info[port_no].state != Blocking)
	/* (4.6.13.3)	 */
		) {
		if ((port_info[port_no].state == Forwarding)
				||
				(port_info[port_no].state == Learning)
			) {
			topology_change_detection();	/* (4.6.13.3.1) */
			/* (4.6.14.2.3)	 */
		}
		set_port_state(port_no, Blocking);/* (4.6.13.3.2) */
		stop_forward_delay_timer(port_no);/* (4.6.13.3.3) */
	}
}

static void set_port_state(int port_no, int state)
{
	port_info[port_no].state = state;
}

static void received_config_bpdu(int port_no, Config_bpdu *config)		  /* (4.7.1)	 */
{
	int root;

	root = root_bridge();
	if (port_info[port_no].state != Disabled) {

#ifdef DEBUG_STP
		if (br_stats.flags & BR_DEBUG)
			printk(KERN_DEBUG "received_config_bpdu: port %d\n",
				port_no);
#endif
		if (supersedes_port_info(port_no, config)) {	/* (4.7.1.1)	 *//* (4.
								 * 6.2.2)	 */
			record_config_information(port_no, config);	/* (4.7.1.1.1)	 */
			/* (4.6.2.2)	 */
			configuration_update();	  /* (4.7.1.1.2)	 */
			/* (4.6.7.2.1)	 */
			port_state_selection();	  /* (4.7.1.1.3)	 */
			/* (4.6.11.2.1)	 */
			if ((!root_bridge()) && root) {	/* (4.7.1.1.4)	 */
				stop_hello_timer();
				if (bridge_info.top_change_detected) {	/* (4.7.1.1.5 */
					stop_topology_change_timer();
					transmit_tcn();	/* (4.6.6.1)	 */
					start_tcn_timer();
				}
			}
			if (port_no == bridge_info.root_port) {
				record_config_timeout_values(config);	/* (4.7.1.1.6)	 */
				/* (4.6.3.2)	 */
				config_bpdu_generation();	/* (4.6.4.2.1)	 */
				if (config->top_change_ack) {	/* (4.7.1.1.7)    */
					topology_change_acknowledged();	/* (4.6.15.2)	 */
				}
			}
		} else if (designated_port(port_no)) {	/* (4.7.1.2)	 */
			reply(port_no);		  /* (4.7.1.2.1)	 */
			/* (4.6.5.2)	 */
		}
	}
}

static void received_tcn_bpdu(int port_no, Tcn_bpdu *tcn)			  /* (4.7.2)	 */
{
	if (port_info[port_no].state != Disabled) {
#ifdef DEBUG_STP
		if (br_stats.flags & BR_DEBUG)
			printk(KERN_DEBUG "received_tcn_bpdu: port %d\n",
				port_no);
#endif
		if (designated_port(port_no)) {
			topology_change_detection();	/* (4.7.2.1)	 */
			/* (4.6.14.2.1)	 */
			acknowledge_topology_change(port_no);	/* (4.7.2.2)	 */
		}				  /* (4.6.16.2)	 */
	}
}

static void hello_timer_expiry(void)
{						  /* (4.7.3)	 */
	config_bpdu_generation();		  /* (4.6.4.2.2)	 */
	start_hello_timer();
}

static void message_age_timer_expiry(int port_no) /* (4.7.4)	 */
{
	int root;
	root = root_bridge();

#ifdef DEBUG_STP
	if (br_stats.flags & BR_DEBUG)
		printk(KERN_DEBUG "message_age_timer_expiry: port %d\n",
			port_no);
#endif
	become_designated_port(port_no);	  /* (4.7.4.1)	 */
	/* (4.6.10.2.1)	 */
	configuration_update();			  /* (4.7.4.2)	 */
	/* (4.6.7.2.2)	 */
	port_state_selection();			  /* (4.7.4.3)	 */
	/* (4.6.11.2.2)	 */
	if ((root_bridge()) && (!root)) {	  /* (4.7.4.4)	 */

		bridge_info.max_age = bridge_info.bridge_max_age;	/* (4.7.4.4.1)    */
		bridge_info.hello_time = bridge_info.bridge_hello_time;
		bridge_info.forward_delay = bridge_info.bridge_forward_delay;
		topology_change_detection();	  /* (4.7.4.4.2)	 */
		/* (4.6.14.2.4)	 */
		stop_tcn_timer();		  /* (4.7.4.4.3)	 */
		config_bpdu_generation();	  /* (4.7.4.4.4)	 */
		/* (4.6.4.4.3)	 */
		start_hello_timer();
	}
}

static void forward_delay_timer_expiry(int port_no)	/* (4.7.5)	 */
{
	if (port_info[port_no].state == Listening) 
	{						/* (4.7.5.1)	 */
		set_port_state(port_no, Learning);	/* (4.7.5.1.1)	 */
		start_forward_delay_timer(port_no);	/* (4.7.5.1.2)	 */
	}
	else if (port_info[port_no].state == Learning) 
	{
							/* (4.7.5.2) */
		set_port_state(port_no, Forwarding);	/* (4.7.5.2.1) */
		if (designated_for_some_port()) 
		{ 					/* (4.7.5.2.2) */
			topology_change_detection();	/* (4.6.14.2.2) */

		}
	}
}

static int designated_for_some_port(void)
{
	int port_no;

	for (port_no = One; port_no <= No_of_ports; port_no++) 
	{
		if(port_info[port_no].state == Disabled)
			continue;
		if ((br_cmp(port_info[port_no].designated_bridge.BRIDGE_ID,
				bridge_info.bridge_id.BRIDGE_ID) == 0)) 
		{
			return (TRUE);
		}
	}
	return (FALSE);
}

static void tcn_timer_expiry(void)
{						  /* (4.7.6)	 */
	transmit_tcn();				  /* (4.7.6.1)	 */
	start_tcn_timer();			  /* (4.7.6.2)	 */
}

static void topology_change_timer_expiry(void)
{						  /* (4.7.7)	 */
	bridge_info.top_change_detected = 0;	/* (4.7.7.1) */
	bridge_info.top_change = 0;
	  /* (4.7.7.2)	 */
}

static void hold_timer_expiry(int port_no)	  /* (4.7.8)	 */
{
	if (port_info[port_no].config_pending) 
	{
		transmit_config(port_no);	  /* (4.7.8.1)	 */
	}					  /* (4.6.1.2.3)	 */
}

/* Vova Oksman: Write the buffer (contents of the Bridge table) */
/* to a PROCfs file                                             */
int br_tree_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int size;
	int len=0;
	off_t pos=0;
	char* pbuffer;

	if(0==offset)
	{
		/* first time write the header */
		size = sprintf(buffer,"%s","MAC address           Device     Flags     Age (sec.)\n");
		len=size;
	}

	pbuffer=&buffer[len];
	sprintf_avl(&pbuffer,NULL,&pos,&len,offset,length);

	*start = buffer+len-(pos-offset);	/* Start of wanted data */
	len = pos-offset;			/* Start slop */
	if (len>length)
		len = length;			/* Ending slop */

	return len;
}
#ifdef CONFIG_PROC_FS
struct proc_dir_entry proc_net_bridge= {
	PROC_NET_BRIDGE, 6, "bridge",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	br_tree_get_info
};
#endif
__initfunc(void br_init(void))
{						  /* (4.8.1)	 */
	int port_no;

	printk(KERN_INFO "NET4: Ethernet Bridge 005 for NET4.0\n");

	/*
	 * Form initial topology change time.
	 * The topology change timer is only used if this is the root bridge.
	 */
	
	bridge_info.topology_change_time = BRIDGE_MAX_AGE + BRIDGE_FORWARD_DELAY;       /* (4.5.3.13) */

	bridge_info.designated_root = bridge_info.bridge_id;	/* (4.8.1.1)	 */
	bridge_info.root_path_cost = Zero;
	bridge_info.root_port = No_port;
#ifdef DEBUG_STP
	printk(KERN_INFO "br_init: becomes root\n");
#endif

	bridge_info.bridge_max_age = BRIDGE_MAX_AGE;
	bridge_info.bridge_hello_time = BRIDGE_HELLO_TIME;
	bridge_info.bridge_forward_delay = BRIDGE_FORWARD_DELAY;
	bridge_info.hold_time = HOLD_TIME;

	bridge_info.max_age = bridge_info.bridge_max_age;	/* (4.8.1.2)	 */
	bridge_info.hello_time = bridge_info.bridge_hello_time;
	bridge_info.forward_delay = bridge_info.bridge_forward_delay;

	bridge_info.top_change_detected = 0;
	bridge_info.top_change = 0;
	stop_tcn_timer();
	stop_topology_change_timer();
	memset(newfdb, 0, sizeof(newfdb));
	for (port_no = One; port_no <= No_of_ports; port_no++) {	/* (4.8.1.4) */
		/* initial state = Enable */
		user_port_state[port_no] = ~Disabled;
		port_priority[port_no] = 128;
		br_init_port(port_no);
		disable_port(port_no);
	}
#if 0 /* JRP: We are not UP ! Wait for the start command */
	port_state_selection();			  /* (4.8.1.5)	 */
	config_bpdu_generation();		  /* (4.8.1.6)	 */
	/* initialize system timer */
	tl.expires = jiffies+HZ;	/* 1 second */
	tl.function = br_tick;
	add_timer(&tl);
#endif	

	register_netdevice_notifier(&br_dev_notifier);
	br_stats.flags = 0; /*BR_UP | BR_DEBUG*/;	/* enable bridge */
	br_stats.policy = BR_ACCEPT;			/* Enable bridge to accpet all protocols */
	br_stats.exempt_protocols = 0;
	/*start_hello_timer();*/
	/* Vova Oksman: register the function for the PROCfs "bridge" file */
#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_bridge);
#endif
}

static inline unsigned short make_port_id(int port_no)
{
	 return (port_priority[port_no] << 8) | port_no;
}

static void br_init_port(int port_no)
{
	port_info[port_no].port_id = make_port_id(port_no);
	become_designated_port(port_no);	  /* (4.8.1.4.1) */
	set_port_state(port_no, Blocking);	  /* (4.8.1.4.2)    */
	port_info[port_no].top_change_ack = 0;
	port_info[port_no].config_pending = FALSE;/* (4.8.1.4.4)	 */
	stop_message_age_timer(port_no);	  /* (4.8.1.4.5)	 */
	stop_forward_delay_timer(port_no);	  /* (4.8.1.4.6)	 */
	stop_hold_timer(port_no);		  /* (4.8.1.4.7)	 */
}

static void enable_port(int port_no)				  /* (4.8.2)	 */
{
	br_init_port(port_no);
	port_state_selection();			  /* (4.8.2.7)	 */
}						  /* */

static void disable_port(int port_no)				  /* (4.8.3)	 */
{
	int         root;

	root = root_bridge();
	become_designated_port(port_no);	  /* (4.8.3.1)	 */
	set_port_state(port_no, Disabled);	  /* (4.8.3.2)	 */
	port_info[port_no].top_change_ack = 0;
	port_info[port_no].config_pending = FALSE;/* (4.8.3.4)	 */
	stop_message_age_timer(port_no);	  /* (4.8.3.5)	 */
	stop_forward_delay_timer(port_no);	  /* (4.8.3.6)	 */
	configuration_update();
	port_state_selection();			  /* (4.8.3.7)	 */
	if ((root_bridge()) && (!root)) {	  /* (4.8.3.8)	 */
		bridge_info.max_age = bridge_info.bridge_max_age;	/* (4.8.3.8.1)    */
		bridge_info.hello_time = bridge_info.bridge_hello_time;
		bridge_info.forward_delay = bridge_info.bridge_forward_delay;
		topology_change_detection();	  /* (4.8.3.8.2)    */
		stop_tcn_timer();		  /* (4.8.3.8.3)    */
		config_bpdu_generation();	  /* (4.8.3.8.4)    */
		start_hello_timer();
	}
}


static void set_bridge_priority(bridge_id_t *new_bridge_id)
                                 		  /* (4.8.4)	 */
{

	int root;
	int port_no;
	root = root_bridge();
	for (port_no = One; port_no <= No_of_ports; port_no++) {	/* (4.8.4.2) */
		if(port_info[port_no].state == Disabled)
			continue;
		if (designated_port(port_no)) {
			port_info[port_no].designated_bridge = *new_bridge_id;
		}
	}

	bridge_info.bridge_id = *new_bridge_id;	  /* (4.8.4.3)	 */
	configuration_update();			  /* (4.8.4.4)	 */
	port_state_selection();			  /* (4.8.4.5)	 */
	if ((root_bridge()) && (!root)) {	  /* (4.8.4.6)	 */
		bridge_info.max_age = bridge_info.bridge_max_age;	/* (4.8.4.6.1)    */
		bridge_info.hello_time = bridge_info.bridge_hello_time;
		bridge_info.forward_delay = bridge_info.bridge_forward_delay;
		topology_change_detection();	  /* (4.8.4.6.2)    */
		stop_tcn_timer();		  /* (4.8.4.6.3)    */
		config_bpdu_generation(),	  /* (4.8.4.6.4)    */
		start_hello_timer();
	}
}

static void set_port_priority(int port_no)
                                  		  /* (4.8.5)	 */
{int new_port_id = make_port_id(port_no);

	if (designated_port(port_no)) {		  /* (4.8.5.2)	 */
		port_info[port_no].designated_port = new_port_id;
	}
	port_info[port_no].port_id = new_port_id; /* (4.8.5.3)	 */
	if ((br_cmp(bridge_info.bridge_id.BRIDGE_ID,
	     port_info[port_no].designated_bridge.BRIDGE_ID) == 0
	     )
			&&
			(port_info[port_no].port_id
			 < port_info[port_no].designated_port

			 )
		) 
	{
		become_designated_port(port_no);  /* (4.8.5.4.1) */
		port_state_selection();		  /* (4.8.5.4.2) */
	}
}

static void set_path_cost(int port_no, unsigned short path_cost)
                                                  /* (4.8.6)	 */
{
	port_info[port_no].path_cost = path_cost; /* (4.8.6.1)	 */
	configuration_update();			  /* (4.8.6.2)	 */
	port_state_selection();			  /* (4.8.6.3)	 */
}

static void br_tick(unsigned long arg)
{
	int port_no;

	if(!(br_stats.flags & BR_UP))
		return;			 /* JRP: we have been shot down */

	if (hello_timer_expired())
		hello_timer_expiry();

	if (tcn_timer_expired())
		tcn_timer_expiry();

	if (topology_change_timer_expired())
		topology_change_timer_expiry();

	for (port_no = One; port_no <= No_of_ports; port_no++) 
	{
		if(port_info[port_no].state == Disabled)
			continue;

		if (forward_delay_timer_expired(port_no)) 
			forward_delay_timer_expiry(port_no);

		if (message_age_timer_expired(port_no))
			message_age_timer_expiry(port_no);

		if (hold_timer_expired(port_no))
			hold_timer_expiry(port_no);
	}
	/* call me again sometime... */
	tl.expires = jiffies+HZ;	/* 1 second */
	tl.function = br_tick;
	add_timer(&tl);
}

static void start_hello_timer(void)
{
	hello_timer.value = 0;
	hello_timer.active = TRUE;
}

static void stop_hello_timer(void)
{
	hello_timer.active = FALSE;
}

static int hello_timer_expired(void)
{
	if (hello_timer.active && (++hello_timer.value >= bridge_info.hello_time)) 
	{
		hello_timer.active = FALSE;
		return (TRUE);
	}
	return (FALSE);
}

static void start_tcn_timer(void)
{
	tcn_timer.value = 0;
	tcn_timer.active = TRUE;
}

static void stop_tcn_timer(void)
{
	tcn_timer.active = FALSE;
}

static int tcn_timer_expired(void)
{
	if (tcn_timer.active && (++tcn_timer.value >= bridge_info.bridge_hello_time)) 
	{
		tcn_timer.active = FALSE;
		return (TRUE);
	}
	return (FALSE);

}

static void start_topology_change_timer(void)
{
	topology_change_timer.value = 0;
	topology_change_timer.active = TRUE;
}

static void stop_topology_change_timer(void)
{
	topology_change_timer.active = FALSE;
}

static int topology_change_timer_expired(void)
{
	if (topology_change_timer.active
		&& (++topology_change_timer.value >= bridge_info.topology_change_time )) 
	{
		topology_change_timer.active = FALSE;
		return (TRUE);
	}
	return (FALSE);
}

static void start_message_age_timer(int port_no, unsigned short message_age)
{
	message_age_timer[port_no].value = message_age;
	message_age_timer[port_no].active = TRUE;
}

static void stop_message_age_timer(int port_no)
{
	message_age_timer[port_no].active = FALSE;
}

static int message_age_timer_expired(int port_no)
{
	if (message_age_timer[port_no].active && (++message_age_timer[port_no].value >= bridge_info.max_age)) 
	{
		message_age_timer[port_no].active = FALSE;
		return (TRUE);
	}
	return (FALSE);
}

static void start_forward_delay_timer(int port_no)
{
	forward_delay_timer[port_no].value = 0;
	forward_delay_timer[port_no].active = TRUE;
}

static void stop_forward_delay_timer(int port_no)
{
	forward_delay_timer[port_no].active = FALSE;
}

static int forward_delay_timer_expired(int port_no)
{
	if (forward_delay_timer[port_no].active && (++forward_delay_timer[port_no].value >= bridge_info.forward_delay)) 
	{
		forward_delay_timer[port_no].active = FALSE;
		return (TRUE);
	}
	return (FALSE);
}

static void start_hold_timer(int port_no)
{
	hold_timer[port_no].value = 0;
	hold_timer[port_no].active = TRUE;
}

static void stop_hold_timer(int port_no)
{
	hold_timer[port_no].active = FALSE;
}

static int hold_timer_expired(int port_no)
{
	if (hold_timer[port_no].active &&
		   (++hold_timer[port_no].value >= bridge_info.hold_time)) 
	{
		hold_timer[port_no].active = FALSE;
		return (TRUE);
	}
	return (FALSE);

}

static struct sk_buff *alloc_bridge_skb(int port_no, int pdu_size, char *pdu_name)
{
	struct sk_buff *skb;
	struct device *dev = port_info[port_no].dev;
	struct ethhdr *eth;
	int size = dev->hard_header_len + BRIDGE_LLC1_HS + pdu_size;
	unsigned char *llc_buffer;
	int pad_size = 60 - size; 
 
	size = 60;	/* minimum Ethernet frame - CRC */

	if (port_info[port_no].state == Disabled) 
	{
		printk(KERN_DEBUG "send_%s_bpdu: port %i not valid\n", pdu_name, port_no);
		return NULL;
	}

  	skb = alloc_skb(size, GFP_ATOMIC);
  	if (skb == NULL) 
 	{
 		printk(KERN_DEBUG "send_%s_bpdu: no skb available\n", pdu_name);
 		return NULL;
 	}
  	skb->dev = dev;
 	skb->mac.raw = skb->nh.raw = skb_put(skb,size);
 	memset(skb->nh.raw + 60 - pad_size, 0xa5, pad_size);
  	eth = skb->mac.ethernet;
  	memcpy(eth->h_dest, bridge_ula, ETH_ALEN);
  	memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
 
  	if (br_stats.flags & BR_DEBUG)
 		printk("send_%s_bpdu: port %i src %02x:%02x:%02x:%02x:%02x:%02x\n",
 			pdu_name,
  			port_no,
  			eth->h_source[0],
  			eth->h_source[1],
  			eth->h_source[2],
  			eth->h_source[3],
  			eth->h_source[4],
 			eth->h_source[5]);
#if 0
 /* 8038 is used in older DEC spanning tree protocol which uses a
  * different pdu layout as well
  */
 	eth->h_proto = htons(0x8038);
#endif
	eth->h_proto = htons(pdu_size + BRIDGE_LLC1_HS);
  
  	skb->nh.raw += skb->dev->hard_header_len;
 	llc_buffer = skb->nh.raw;
 	*llc_buffer++ = BRIDGE_LLC1_DSAP;
 	*llc_buffer++ = BRIDGE_LLC1_SSAP;
 	*llc_buffer++ = BRIDGE_LLC1_CTRL;
 	/* set nh.raw to where the bpdu starts */
 	skb->nh.raw += BRIDGE_LLC1_HS;
  
 	/* mark that we've been here... */
  	skb->pkt_bridged = IS_BRIDGED;
 	return skb;
}
 
static int send_config_bpdu(int port_no, Config_bpdu *config_bpdu)
{
	struct sk_buff *skb;
	
	/*
	 *	Create and send the message
	 */
	 
 	skb = alloc_bridge_skb(port_no, BRIDGE_BPDU_8021_CONFIG_SIZE,
				"config");
	if (skb == NULL)
		return(-1);

	/* copy fields before "flags" */
	memcpy(skb->nh.raw, config_bpdu, BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET);

	/* build the "flags" field */
	*(skb->nh.raw+BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET) = 0;
	if (config_bpdu->top_change_ack)
		*(skb->nh.raw+BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET) |= 0x80;
	if (config_bpdu->top_change)
		*(skb->nh.raw+BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET) |= 0x01;

	config_bpdu_hton(config_bpdu);
	/* copy the rest */
	memcpy(skb->nh.raw+BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET+1,
		 (char*)&(config_bpdu->root_id),
		 BRIDGE_BPDU_8021_CONFIG_SIZE-1-BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET);

 	dev_queue_xmit(skb);
  	return(0);
}
  
static int send_tcn_bpdu(int port_no, Tcn_bpdu *bpdu)
{
	struct sk_buff *skb;
 	
	skb = alloc_bridge_skb(port_no, sizeof(Tcn_bpdu), "tcn");
	if (skb == NULL)
  		return(-1);
  
  	memcpy(skb->nh.raw, bpdu, sizeof(Tcn_bpdu));
  
 	dev_queue_xmit(skb);
  	return(0);
}

static int br_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
	struct device *dev = ptr;
	int i;

	/* check for loopback devices */
	if (dev->flags & IFF_LOOPBACK)
		return(NOTIFY_DONE);

	switch (event) 
	{
		case NETDEV_DOWN:
			if (br_stats.flags & BR_DEBUG)
				printk(KERN_DEBUG "br_device_event: NETDEV_DOWN...\n");
			/* find our device and mark it down */
			for (i = One; i <= No_of_ports; i++) 
			{
				if (port_info[i].dev == dev) 
				{
					disable_port(i);
					return NOTIFY_DONE;
					break;
				}
			}
			break;
		case NETDEV_UP:
			if (br_stats.flags & BR_DEBUG)
				printk(KERN_DEBUG "br_device_event: NETDEV_UP...\n");
			/* Only handle ethernet ports */
			if(dev->type!=ARPHRD_ETHER && dev->type!=ARPHRD_LOOPBACK)
				return NOTIFY_DONE;
			/* look up an unused device and enable it */
			for (i = One; i <= No_of_ports; i++) 
			{
				if (port_info[i].dev == NULL || port_info[i].dev == dev) 
				{
					port_info[i].dev = dev;
					port_info[i].port_id = i;
					/* set bridge addr from 1st device addr */
					if (((htonl(bridge_info.bridge_id.BRIDGE_ID[0])&0xffff) == 0) &&
						(bridge_info.bridge_id.BRIDGE_ID[1] == 0)) 
					{
						memcpy(bridge_info.bridge_id.BRIDGE_ID_ULA, dev->dev_addr, 6);
						if(bridge_info.bridge_id.BRIDGE_PRIORITY == 0)
							bridge_info.bridge_id.BRIDGE_PRIORITY = htons(32768);
						set_bridge_priority(&bridge_info.bridge_id);
					}
					br_add_local_mac(dev->dev_addr);
					if((br_stats.flags & BR_UP) &&
				   		(user_port_state[i] != Disabled)) 
				   	{
				   		/* don't start if user said so */
							enable_port(i);
						set_path_cost(i, br_port_cost(dev));
						set_port_priority(i); 
						make_forwarding(i);
					}
					return NOTIFY_DONE;
					break;
				}
			}
			break;
	}
	return NOTIFY_DONE;
}

/*
 * following routine is called when a frame is received
 * from an interface, it returns 1 when it consumes the
 * frame, 0 when it does not
 */

int br_receive_frame(struct sk_buff *skb)	/* 3.5 */
{
	int port;
	Port_data  *p;
	struct ethhdr *eth;
	
	/* sanity */
	if (!skb) {
		printk(KERN_CRIT "br_receive_frame: no skb!\n");
		return(1);
	}

	skb->pkt_bridged = IS_BRIDGED;

	/* check for loopback */
	if (skb->dev->flags & IFF_LOOPBACK)
		return 0 ;

	port = find_port(skb->dev);
	
	if(!port)
		return 0;
	
	skb->nh.raw = skb->mac.raw;
	eth = skb->mac.ethernet;
	p = &port_info[port];
 
 	if(p->state == Disabled) 
 	{
 		/* We are here if BR_UP even if this port is Disabled.
 		 * Send everything up
 		 */
 		skb->pkt_type = PACKET_HOST;
 		++br_stats_cnt.port_disable_up_stack;
 		return(0);	/* pass frame up our stack (this will */
 				/* happen in net_bh() in dev.c) */
 	}
 
 	/* Here only if not disable.
 	 * Remark: only frames going up will show up in NIT (tcpdump)
 	 */

	/* JRP: even if port is Blocking we need to process the Spanning Tree
	 * frames to keep the port in that state
	 */
	if (memcmp(eth->h_dest, bridge_ula, ETH_ALEN) == 0) 
	{
		++br_stats_cnt.rcv_bpdu;
		br_bpdu(skb, port); /* br_bpdu consumes skb */
		return(1);
	}
	switch (p->state) 
	{
		case Learning:
			if(br_learn(skb, port)) 
			{	/* 3.8 */
				++br_stats_cnt.drop_multicast;
				return br_drop(skb);
			}
			/* fall through */
		case Listening:
			/* fall through */
		case Blocking:
			++br_stats_cnt.notForwarding;
			return(br_drop(skb));	
		/*
		case Disabled: is now handled before this switch !
		Keep the break to allow GCC to use a jmp table.
		 */
			break;
		case Forwarding:
			if(br_learn(skb, port)) {	/* 3.8 */
				++br_stats_cnt.drop_multicast;
				return br_drop(skb);
			}
			/* Now this frame came from one of bridged
			   ports this means we should attempt to forward it.
			   JRP: local addresses are now in the AVL tree,
			   br_forward will pass frames up if it matches
			   one of our local MACs or if it is a multicast
			   group address.
			   br_forward() will not consume the frame if this
			   is the case */
			return(br_forward(skb, port));
		default:
			printk(KERN_DEBUG "br_receive_frame: port [%i] unknown state [%i]\n",
				port, p->state);
			++br_stats_cnt.unknown_state;
			return(br_drop(skb));	/* discard frame */
	}
}

/*
 * the following routine is called to transmit frames from the host
 * stack.  it returns 1 when it consumes the frame and
 * 0 when it does not.
 */

int br_tx_frame(struct sk_buff *skb)	/* 3.5 */
{
	int port;
	struct ethhdr *eth;
	
	/* sanity */
	if (!skb) 
	{
		printk(KERN_CRIT "br_tx_frame: no skb!\n");
		return(0);
	}
	
	if (!skb->dev)
	{
		printk(KERN_CRIT "br_tx_frame: no dev!\n");
		return(0);
	}
	
	/* check for loopback */
	if (skb->dev->flags & IFF_LOOPBACK)
		return(0);

	/* if bridging is not enabled on the port we are going to send
           to, we have nothing to do with this frame, hands off */
	if (((port=find_port(skb->dev))==0)||(port_info[port].state==Disabled)) {
		++br_stats_cnt.port_disable;
		return(0);
	}
	++br_stats_cnt.port_not_disable;
	skb->mac.raw = skb->nh.raw = skb->data;
	eth = skb->mac.ethernet;
	port = 0;	/* an impossible port (locally generated) */	
	if (br_stats.flags & BR_DEBUG)
		printk("br_tx_fr : port %i src %02x:%02x:%02x:%02x:%02x:%02x"
	  		" dest %02x:%02x:%02x:%02x:%02x:%02x\n", 
			port,
			eth->h_source[0],
			eth->h_source[1],
			eth->h_source[2],
			eth->h_source[3],
			eth->h_source[4],
			eth->h_source[5],
			eth->h_dest[0],
			eth->h_dest[1],
			eth->h_dest[2],
			eth->h_dest[3],
			eth->h_dest[4],
			eth->h_dest[5]);
	return(br_forward(skb, port));
}

static void br_add_local_mac(unsigned char *mac)
{
	struct fdb *f;
	f = (struct fdb *)kmalloc(sizeof(struct fdb), GFP_ATOMIC);
	if (!f) 
	{
		printk(KERN_CRIT "br_add_local_mac: unable to malloc fdb\n");
		return;
	}
	f->port = 0;	/* dest port == 0 =>local */
	memcpy(f->ula, mac, 6);
	f->timer = 0;	/* will not aged anyway */
	f->flags = 0;	/* not valid => br_forward special route */
	/*
	 * add entity to AVL tree.  If entity already
	 * exists in the tree, update the fields with
	 * what we have here.
  	 */
	if (br_avl_insert(f) != NULL) 
	{
		/* Already in */
		kfree(f);
	}
}

/* Avoid broadcast loop by limiting the number of broacast frames per
 * period. The idea is to limit this per source
 * returns: 0 if limit is not reached
 *          1 if frame should be dropped
 */

static inline int mcast_quench(struct fdb *f)
{
	if(f->mcast_count++ == 0) /* first time */
		f->mcast_timer = jiffies;
	else {
		if(f->mcast_count > max_mcast_per_period) {
			if(time_after(jiffies, f->mcast_timer + mcast_hold_time))
				f->mcast_count = 0;
			else	return 1;
		}
	}
	return 0;
}

/*
 * this routine returns 0 when it learns (or updates) from the
 * frame, and 1 if we must dropped the frame.
 */

static int br_learn(struct sk_buff *skb, int port)	/* 3.8 */
{
	struct fdb *f, *oldfdb;
	Port_data  *p = &port_info[port];
	struct ethhdr *eth = skb->mac.ethernet;

	/* JRP: no reason to check port state again. We are called by
	 * br_receive_frame() only when in Learning or Forwarding
	 * Remark: code not realigned yet to keep diffs smaller
	 */

	/* don't keep group addresses in the tree */
	if (eth->h_source[0] & 0x01)
		return 0;

	if((f= newfdb[port]) == NULL) 
	{
		newfdb[port] = f = (struct fdb *)kmalloc(sizeof(struct fdb), GFP_ATOMIC);
		if (!f) 
		{
			printk(KERN_DEBUG "br_learn: unable to malloc fdb\n");
			return(-1); /* this drop the frame */
		}
	}
	f->port = port;	/* source port */
	memcpy(f->ula, eth->h_source, 6);
	f->timer = CURRENT_TIME;
	f->flags = FDB_ENT_VALID;
	/*
	 * add entity to AVL tree.  If entity already
	 * exists in the tree, update the fields with
	 * what we have here.
	 */
	if ((oldfdb = br_avl_insert(f))) 
	{
		/* update if !NULL */
		if((eth->h_dest[0] & 0x01) &&  /* multicast */ mcast_quench(oldfdb))
			return 1;
		return 0;
	}
	newfdb[port] = NULL;	/* force kmalloc next time */
	f->mcast_count = 0;
	/* add to head of port chain */
	f->fdb_next = p->fdb;
	p->fdb = f;
	allocated_fdb_cnt++;
	return 0;
}

/* JRP: always called under br_receive_frame(). No need for Q protection. */

void requeue_fdb(struct fdb *node, int new_port)
{
	Port_data *p = &port_info[node->port];

	/* dequeue */
	if(p->fdb == node)
		p->fdb = node->fdb_next;
	else 
	{
		struct fdb *prev;

		for(prev = p->fdb; prev; prev = prev->fdb_next)
			if (prev->fdb_next == node)
				break;

		if(prev != NULL)
			prev->fdb_next = node->fdb_next;
		else 
		{
			/*	Forget about this update. */
			printk(KERN_ERR "br:requeue_fdb\n");
			return;
		}
	}
	/* enqueue */
	node->port = new_port;
	node->fdb_next = port_info[new_port].fdb;
	port_info[new_port].fdb = node;
}

/*
 * this routine always consumes the frame
 */

static int br_drop(struct sk_buff *skb)
{
	kfree_skb(skb);
	return(1);
}

/*
 * this routine always consumes the frame
 */

static int br_dev_drop(struct sk_buff *skb)
{
	dev_kfree_skb(skb);
	return(1);
}

/*
 * Forward the frame SKB to proper port[s].  PORT is the port that the
 * frame has come from; we will not send the frame back there.  PORT == 0
 * means we have been called from br_tx_fr(), not from br_receive_frame().
 *
 * this routine returns 1 if it consumes the frame, 0
 * if not...
 */

static int br_forward(struct sk_buff *skb, int port)	/* 3.7 */
{
	struct fdb *f;
	
	/*
   	 * flood all ports with frames destined for a group
	 * address.  If frame came from above, drop it,
	 * otherwise it will be handled in br_receive_frame()
	 * Multicast frames will also need to be seen
	 * by our upper layers.
	 */	
	if (skb->mac.ethernet->h_dest[0] & 0x01) 
	{
		/* group address */
		br_flood(skb, port);
		/*
		 *	External groups are fed out via the normal source
		 *	This probably should be dropped since the flood will
		 *	have sent it anyway.
		 */
		if (port == 0) 
		{
			/* Locally generated */
			++br_stats_cnt.local_multicast;
			return(br_dev_drop(skb));
		}
		++br_stats_cnt.forwarded_multicast;
		return(0);
	}
	else 
	{
		/* unicast frame, locate port to forward to */
		f = br_avl_find_addr(skb->mac.ethernet->h_dest);
		/*
		 *	Send flood and drop.
		 */
		if (!f || !(f->flags & FDB_ENT_VALID)) 
		{
	 		if(f && (f->port == 0)) 
	 		{
				skb->pkt_type = PACKET_HOST;
				++br_stats_cnt.forwarded_unicast_up_stack;
				return(0);
			}
			/* not found or too old; flood all ports */
			++br_stats_cnt.flood_unicast;
			br_flood(skb, port);
			return(br_dev_drop(skb));
		}
		/*
		 *	Sending
		 */
		if (f->port!=port && port_info[f->port].state == Forwarding) 
		{
			/* Has entry expired? */
			if (f->timer + fdb_aging_time < CURRENT_TIME) 
			{
				/* timer expired, invalidate entry */
				f->flags &= ~FDB_ENT_VALID;
				if (br_stats.flags & BR_DEBUG)
					printk("fdb entry expired...\n");
				/*
				 *	Send flood and drop original
				 */
				++br_stats_cnt.aged_flood_unicast;
				br_flood(skb, port);
				return(br_dev_drop(skb));
			}
			++br_stats_cnt.forwarded_unicast;
			/* mark that's we've been here... */
			skb->pkt_bridged = IS_BRIDGED;
			
			/* reset the skb->ip pointer */	
			skb->nh.raw = skb->data + ETH_HLEN;

			/*
			 *	Send the buffer out.
			 */
			 
			skb->dev=port_info[f->port].dev;
			 
			/*
			 *	We send this still locked
			 */
			skb->priority = 1;
			dev_queue_xmit(skb);
			return(1);	/* skb has been consumed */
		}
		else 
		{
			/* JRP: Needs to aged entry as well, if topology changes
			 * the entry would not age. Got this while swapping
			 * two cables !
			 *
			 *	Has entry expired?
			 */
			 
			if (f->timer + fdb_aging_time < CURRENT_TIME) 
			{
				/* timer expired, invalidate entry */
				f->flags &= ~FDB_ENT_VALID;
				if (br_stats.flags & BR_DEBUG)
					printk("fdb entry expired...\n");
				++br_stats_cnt.drop_same_port_aged;
			}
			else ++br_stats_cnt.drop_same_port;
			/*
			 *	Arrived on the right port, we discard
			 */
			return(br_dev_drop(skb));
		}
	}
}

/*
 * this routine sends a copy of the frame to all forwarding ports
 * with the exception of the port given.  This routine never
 * consumes the original frame.
 */
	
static int br_flood(struct sk_buff *skb, int port)
{
	int i;
	struct sk_buff *nskb;

	for (i = One; i <= No_of_ports; i++) 
	{
		if (i == port)	/* don't send back where we got it */
			continue;
		if (port_info[i].state == Forwarding) 
		{
			nskb = skb_clone(skb, GFP_ATOMIC);
			if(nskb==NULL)
				continue;
			/* mark that's we've been here... */
			nskb->pkt_bridged = IS_BRIDGED;
			/* Send to each port in turn */
			nskb->dev= port_info[i].dev;
			/* To get here we must have done ARP already,
			   or have a received valid MAC header */
			
/*			printk("Flood to port %d\n",i);*/
			nskb->nh.raw = nskb->data + ETH_HLEN;
#if LINUX_VERSION_CODE >= 0x20100
			nskb->priority = 1;
			dev_queue_xmit(nskb);
#else
			dev_queue_xmit(nskb,nskb->dev,1);
#endif
		}
	}
	return(0);
}

static int find_port(struct device *dev)
{
	int i;

	for (i = One; i <= No_of_ports; i++)
		if (port_info[i].dev == dev)
			return(i);
	return(0);
}

/*
 *	FIXME: This needs to come from the device structs, eg for
 *	10,100,1Gbit ethernet.
 */
 
static int br_port_cost(struct device *dev)	/* 4.10.2 */
{
	if (strncmp(dev->name, "eth", 3) == 0)	/* ethernet */
		return(100);
	if (strncmp(dev->name, "plip",4) == 0) /* plip */
		return (1600);
	return(100);	/* default */
}

/*
 * this routine always consumes the skb 
 */

static void br_bpdu(struct sk_buff *skb, int port) /* consumes skb */
{
	char *bufp = skb->data + ETH_HLEN;
	Tcn_bpdu *bpdu = (Tcn_bpdu *) (bufp + BRIDGE_LLC1_HS);
	Config_bpdu rcv_bpdu;

	if((*bufp++ == BRIDGE_LLC1_DSAP) && (*bufp++ == BRIDGE_LLC1_SSAP) &&
		(*bufp++ == BRIDGE_LLC1_CTRL) &&
		(bpdu->protocol_id == BRIDGE_BPDU_8021_PROTOCOL_ID) &&
		(bpdu->protocol_version_id == BRIDGE_BPDU_8021_PROTOCOL_VERSION_ID)) 
	{
  
  		switch (bpdu->type) 
  		{
	  		case BPDU_TYPE_CONFIG:
 				/* realign for portability to RISC */
 				memcpy((char*)&rcv_bpdu, bufp,
 					BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET);
 				bufp+= BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET;
 				rcv_bpdu.top_change_ack =
 					(*bufp & TOPOLOGY_CHANGE_ACK) != 0;
 				rcv_bpdu.top_change =
 					(*bufp & TOPOLOGY_CHANGE) != 0;
 				bufp++;
 				memcpy((char*)&rcv_bpdu.root_id, bufp,
 					BRIDGE_BPDU_8021_CONFIG_SIZE-1
        	                         -BRIDGE_BPDU_8021_CONFIG_FLAG_OFFSET);
 				config_bpdu_ntoh(&rcv_bpdu);
 				received_config_bpdu(port, &rcv_bpdu);
 				break;
 			
			case BPDU_TYPE_TOPO_CHANGE:
				received_tcn_bpdu(port, bpdu);
				break;
			default:
				printk(KERN_DEBUG "br_bpdu: received unknown bpdu, type = %i\n", bpdu->type);
			/* break; */
		}
	}
	br_drop(skb);
}

struct fdb_info *get_fdb_info(int user_buf_size, int *copied,int *notcopied)
{
	int fdb_size, i, built = 0;
	struct fdb_info *fdbi, *fdbis;

	*copied = user_buf_size - sizeof(struct fdb_info_hdr);
	*copied /= sizeof(struct fdb_info);
	*copied = min(*copied, allocated_fdb_cnt);
	*notcopied = allocated_fdb_cnt - *copied;
	if(*copied == 0)
		return NULL;
	fdb_size = *copied * sizeof(struct fdb_info);
	fdbis = kmalloc(fdb_size, GFP_KERNEL);
	if(fdbis == NULL)
		return NULL;
	fdbi = fdbis;

	for(i=One; i<=No_of_ports;i++)
	{
		struct fdb *fdb;

		cli();
		fdb = port_info[i].fdb;
		while(fdb) 
		{
			memcpy(fdbi->ula, fdb->ula, ETH_ALEN);
			fdbi->port = fdb->port;
			fdbi->flags = fdb->flags;
			fdbi->timer = fdb->timer;
			fdbi++;
			if(++built == *copied) 
			{
				sti();
				return fdbis;
			}
			fdb = fdb->fdb_next;
		}
		sti();
	}
	printk(KERN_DEBUG "get_fdb_info: built=%d\n", built);
	return fdbis;
}

int br_ioctl(unsigned int cmd, void *arg)
{
	int err, i;
	struct br_cf bcf;
	bridge_id_t new_id;

	switch(cmd)
	{
		case SIOCGIFBR:	/* get bridging control blocks */
			memcpy(&br_stats.bridge_data, &bridge_info, sizeof(Bridge_data));
			memcpy(&br_stats.port_data, &port_info, sizeof(Port_data)*No_of_ports);

			err = copy_to_user(arg, &br_stats, sizeof(struct br_stat));
			if (err)
			{
				err = -EFAULT;
			}
			return err;
		case SIOCSIFBR:
			err = copy_from_user(&bcf, arg, sizeof(struct br_cf));
			if (err)
				return -EFAULT; 
			if (bcf.cmd != BRCMD_DISPLAY_FDB && !suser())
				return -EPERM;
			switch (bcf.cmd) 
			{
				case BRCMD_BRIDGE_ENABLE:
					if (br_stats.flags & BR_UP)
						return(-EALREADY);	
					printk(KERN_DEBUG "br: enabling bridging function\n");
					br_stats.flags |= BR_UP;	/* enable bridge */
					for(i=One;i<=No_of_ports; i++)
					{
						/* don't start if user said so */
						if((user_port_state[i] != Disabled)
							&& port_info[i].dev) 
						{
							enable_port(i);
						}
					}
					port_state_selection();	  /* (4.8.1.5)	 */
					config_bpdu_generation();  /* (4.8.1.6)	 */
					/* initialize system timer */
					tl.expires = jiffies+HZ;	/* 1 second */
					tl.function = br_tick;
					add_timer(&tl);
					start_hello_timer();
					break;
				case BRCMD_BRIDGE_DISABLE:
					if (!(br_stats.flags & BR_UP))
						return(-EALREADY);	
					printk(KERN_DEBUG "br: disabling bridging function\n");
					br_stats.flags &= ~BR_UP;	/* disable bridge */
					stop_hello_timer();
					for (i = One; i <= No_of_ports; i++)
						if (port_info[i].state != Disabled)
							disable_port(i);
					break;
				case BRCMD_PORT_ENABLE:
					if (port_info[bcf.arg1].dev == 0)
						return(-EINVAL);
					if (user_port_state[bcf.arg1] != Disabled)
						return(-EALREADY);
					printk(KERN_DEBUG "br: enabling port %i\n",bcf.arg1);
					user_port_state[bcf.arg1] = ~Disabled;
					if(br_stats.flags & BR_UP)
						enable_port(bcf.arg1);
					break;
				case BRCMD_PORT_DISABLE:
					if (port_info[bcf.arg1].dev == 0)
						return(-EINVAL);
					if (user_port_state[bcf.arg1] == Disabled)
						return(-EALREADY);
					printk(KERN_DEBUG "br: disabling port %i\n",bcf.arg1);
					user_port_state[bcf.arg1] = Disabled;
					if(br_stats.flags & BR_UP)
						disable_port(bcf.arg1);
					break;
				case BRCMD_SET_BRIDGE_PRIORITY:
					new_id = bridge_info.bridge_id;
					new_id.BRIDGE_PRIORITY = htons(bcf.arg1);
					set_bridge_priority(&new_id);
					break;
				case BRCMD_SET_PORT_PRIORITY:
					if((port_info[bcf.arg1].dev == 0)
					    || (bcf.arg2 & ~0xff))
						return(-EINVAL);
					port_priority[bcf.arg1] = bcf.arg2;
					set_port_priority(bcf.arg1);
					break;
				case BRCMD_SET_PATH_COST:
					if (port_info[bcf.arg1].dev == 0)
						return(-EINVAL);
					set_path_cost(bcf.arg1, bcf.arg2);
					break;
				case BRCMD_ENABLE_DEBUG:
					br_stats.flags |= BR_DEBUG;
					break;
				case BRCMD_DISABLE_DEBUG:
					br_stats.flags &= ~BR_DEBUG;
					break;
				case BRCMD_SET_POLICY:
					return br_set_policy(bcf.arg1);
				case BRCMD_EXEMPT_PROTOCOL:
					return br_add_exempt_protocol(bcf.arg1);
				case BRCMD_ENABLE_PROT_STATS:
					br_stats.flags |= BR_PROT_STATS;
					break;
				case BRCMD_DISABLE_PROT_STATS:
					br_stats.flags &= ~BR_PROT_STATS;
					break;
				case BRCMD_ZERO_PROT_STATS:
					memset(&br_stats.prot_id,0,sizeof(br_stats.prot_id));
					memset(&br_stats.prot_counter,0,sizeof(br_stats.prot_counter));
					break;
				case BRCMD_DISPLAY_FDB:
				{
					struct fdb_info_hdr *user_buf = (void*) bcf.arg1;
					struct fdb_info *u_fdbs, *fdbis;
					int copied, notcopied;
					u32 j = CURRENT_TIME;

					if(bcf.arg2<sizeof(struct fdb_info_hdr))
						return -EINVAL;
					put_user(j, &user_buf->cmd_time);
					if(allocated_fdb_cnt == 0) 
					{
						put_user(0, &user_buf->copied);
						put_user(0, &user_buf->not_copied);
						return 0;
					}
					fdbis = get_fdb_info(bcf.arg2, &copied, &notcopied);
					put_user(copied, &user_buf->copied);
					put_user(notcopied, &user_buf->not_copied);
					if(!fdbis)
						return -ENOMEM;
					u_fdbs = (struct fdb_info *) (user_buf+1);
					err = copy_to_user(u_fdbs, fdbis, copied*sizeof(struct fdb_info));
					kfree(fdbis);
					if (err)
					{
						err = -EFAULT;
					}
					return err;
				}
				default:
					return -EINVAL;
			}
			return(0);
		default:
			return -EINVAL;
	}
	/*NOTREACHED*/
	return 0;
}

static int br_cmp(unsigned int *a, unsigned int *b)
{
	int i;	
	for (i=0; i<2; i++) 
	{
		/* JRP: compares prty then MAC address in memory byte order
		 * OK optimizer does htonl() only once per long !
		 */
		if (htonl(a[i]) < htonl(b[i]))
			return(-1);
		if (htonl(a[i]) > htonl(b[i]))
			return(1);
	}
	return(0);
}

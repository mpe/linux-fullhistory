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
 *	Fixes:
 *		Yury Shevchuk	:	Bridge with non bridging ports
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
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <net/br.h>

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
static void set_port_priority(int port_no, unsigned short new_port_id);
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
static void br_bpdu(struct sk_buff *skb); /* consumes skb */
static int br_cmp(unsigned int *a, unsigned int *b);
static int send_tcn_bpdu(int port_no, Tcn_bpdu *bpdu);
static int send_config_bpdu(int port_no, Config_bpdu *config_bpdu);
static int find_port(struct device *dev);
static int br_flood(struct sk_buff *skb, int port);
static int br_drop(struct sk_buff *skb);
static int br_learn(struct sk_buff *skb, int port);	/* 3.8 */

static unsigned char bridge_ula[ETH_ALEN] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };
static Bridge_data     bridge_info;			  /* (4.5.3)	 */
Port_data       port_info[All_ports];		  /* (4.5.5)	 */
Config_bpdu     config_bpdu[All_ports];
Tcn_bpdu        tcn_bpdu[All_ports];
static Timer    hello_timer;			  /* (4.5.4.1)	 */
static Timer    tcn_timer;			  /* (4.5.4.2)	 */
static Timer    topology_change_timer;		  /* (4.5.4.3)	 */
static Timer    message_age_timer[All_ports];	  /* (4.5.6.1)	 */
static Timer    forward_delay_timer[All_ports];	  /* (4.5.6.2)	 */
static Timer    hold_timer[All_ports];		  /* (4.5.6.3)	 */

/* entries timeout after this many seconds */
unsigned int fdb_aging_time = FDB_TIMEOUT; 

struct br_stat br_stats;

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
	{ for(x=0;x<BR_MAX_PROT_STATS &&
	     br_stats.prot_id[x]!=protocol && br_stats.prot_id[x];x++) ;
	  if (x<BR_MAX_PROT_STATS)
	  { br_stats.prot_id[x]=protocol;br_stats.prot_counter[x]++;
	  }
	}

	for (x=BR_PROTOCOL_HASH(protocol); br_stats.protocols[x]!=0;) {
		if (br_stats.protocols[x]==protocol) return !br_stats.policy;
		x++;
		if (x==BR_MAX_PROTOCOLS) x=0;
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
 * modified somewhat to fit within out framework and structure.  It
 * implements the spanning tree algorithm that is the heart of the
 * 802.1d bridging protocol.
 */

static void transmit_config(int port_no)	  /* (4.6.1)	 */
{
	if (hold_timer[port_no].active) {	  /* (4.6.1.3.1)	 */
		port_info[port_no].config_pending = TRUE;	/* (4.6.1.3.1)	 */
	} else {				  /* (4.6.1.3.2)	 */
		config_bpdu[port_no].type = BPDU_TYPE_CONFIG;
		config_bpdu[port_no].root_id = bridge_info.designated_root;
		/* (4.6.1.3.2(1)) */
		config_bpdu[port_no].root_path_cost = bridge_info.root_path_cost;
		/* (4.6.1.3.2(2)) */
		config_bpdu[port_no].bridge_id = bridge_info.bridge_id;
		/* (4.6.1.3.2(3)) */
		config_bpdu[port_no].port_id = port_info[port_no].port_id;
		/*
		 * (4.6.1.3.2(4))
		 */
		if (root_bridge()) {
			config_bpdu[port_no].message_age = Zero;	/* (4.6.1.3.2(5)) */
		} else {
			config_bpdu[port_no].message_age
				= message_age_timer[bridge_info.root_port].value
				+ Message_age_increment;	/* (4.6.1.3.2(6)) */
		}

		config_bpdu[port_no].max_age = bridge_info.max_age;	/* (4.6.1.3.2(7)) */
		config_bpdu[port_no].hello_time = bridge_info.hello_time;
		config_bpdu[port_no].forward_delay = bridge_info.forward_delay;
		config_bpdu[port_no].flags = 0;
		config_bpdu[port_no].flags |=
			port_info[port_no].top_change_ack ? TOPOLOGY_CHANGE_ACK : 0;
		/* (4.6.1.3.2(8)) */
		port_info[port_no].top_change_ack = 0;
		/* (4.6.1.3.2(8)) */
		config_bpdu[port_no].flags |=
			bridge_info.top_change ? TOPOLOGY_CHANGE : 0;
		/* (4.6.1.3.2(9)) */

		send_config_bpdu(port_no, &config_bpdu[port_no]);
		port_info[port_no].config_pending = FALSE;	/* (4.6.1.3.2(10)) */
		start_hold_timer(port_no);	  /* (4.6.1.3.2(11)) */
	}
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
	if (config->flags & TOPOLOGY_CHANGE)
		bridge_info.top_change = 1;
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
	tcn_bpdu[port_no].type = BPDU_TYPE_TOPO_CHANGE;
	send_tcn_bpdu(port_no, &tcn_bpdu[bridge_info.root_port]);	/* (4.6.6.3)     */
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
				      = port_info[root_port].designated_port
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
	for (port_no = One; port_no <= No_of_ports; port_no++) {
		if (port_no == bridge_info.root_port) {	/* (4.6.11.3.1) */
			port_info[port_no].config_pending = FALSE;	/* (4.6.11.3~1(1)) */
			port_info[port_no].top_change_ack = 0;
			make_forwarding(port_no); /* (4.6.11.3.1(2)) */
		} else if (designated_port(port_no)) {	/* (4.6.11.3.2) */
			stop_message_age_timer(port_no);	/* (4.6.11.3.2(1)) */
			make_forwarding(port_no); /* (4.6.11.3.2(2)) */
		} else {			  /* (4.6.11.3.3) */
			port_info[port_no].config_pending = FALSE;	/* (4.6.11.3.3(1)) */
			port_info[port_no].top_change_ack = 0;
			make_blocking(port_no);	  /* (4.6.11.3.3(2)) */
		}
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
	if (root_bridge()) {			  /* (4.6.14.3.1)   */
		bridge_info.top_change = 1;
		start_topology_change_timer();	  /* (4.6.14.3.1(2)) */
	} else if (!(bridge_info.top_change_detected)) {
		transmit_tcn();			  /* (4.6.14.3.2(1)) */
		start_tcn_timer();		  /* (4.6.14.3.2(2)) */
	}
	bridge_info.top_change = 1;
}

static void topology_change_acknowledged(void)
{						  /* (4.6.15) */
	bridge_info.top_change_detected = 0;
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
	int         root;

	root = root_bridge();
	if (port_info[port_no].state != Disabled) {
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
				if (bridge_info.top_change_detected) {	/* (4.7.1.1.5~ */
					stop_topology_change_timer();
					transmit_tcn();	/* (4.6.6.1)	 */
					start_tcn_timer();
				}
			}
			if (port_no == bridge_info.root_port) {
				record_config_timeout_values(config);	/* (4.7.1.1.6)	 */
				/* (4.6.3.2)	 */
				config_bpdu_generation();	/* (4.6.4.2.1)	 */
				if (config->flags & TOPOLOGY_CHANGE_ACK) {	/* (4.7.1.1.7)    */
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
	int         root;
	root = root_bridge();

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
	if (port_info[port_no].state == Listening) {	/* (4.7.5.1)	 */
		set_port_state(port_no, Learning);	/* (4.7.5.1.1)	 */
		start_forward_delay_timer(port_no);	/* (4.7.5.1.2)	 */
	} else if (port_info[port_no].state == Learning) {	/* (4.7.5.2) */
		set_port_state(port_no, Forwarding);	/* (4.7.5.2.1) */
		if (designated_for_some_port()) { /* (4.7.5.2.2) */
			topology_change_detection();	/* (4.6.14.2.2) */

		}
	}
}

static int designated_for_some_port(void)
{
	int             port_no;


	for (port_no = One; port_no <= No_of_ports; port_no++) {
		if ((br_cmp(port_info[port_no].designated_bridge.BRIDGE_ID,
				bridge_info.bridge_id.BRIDGE_ID) == 0)
			) {
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
	bridge_info.top_change_detected = 0;
	bridge_info.top_change = 0;
	  /* (4.7.7.2)	 */
}

static void hold_timer_expiry(int port_no)	  /* (4.7.8)	 */
{
	if (port_info[port_no].config_pending) {
		transmit_config(port_no);	  /* (4.7.8.1)	 */
	}					  /* (4.6.1.2.3)	 */
}

__initfunc(void br_init(void))
{						  /* (4.8.1)	 */
	int             port_no;

	printk(KERN_INFO "Ethernet Bridge 003 for NET3.037 (Linux 2.1)\n");
	bridge_info.designated_root = bridge_info.bridge_id;	/* (4.8.1.1)	 */
	bridge_info.root_path_cost = Zero;
	bridge_info.root_port = No_port;

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
	for (port_no = One; port_no <= No_of_ports; port_no++) {	/* (4.8.1.4) */
		br_init_port(port_no);
		disable_port(port_no);
	}
	port_state_selection();			  /* (4.8.1.5)	 */
	config_bpdu_generation();		  /* (4.8.1.6)	 */
	
	/* initialize system timer */
	tl.expires = jiffies+HZ;	/* 1 second */
	tl.function = br_tick;
	add_timer(&tl);

	register_netdevice_notifier(&br_dev_notifier);
	br_stats.flags = 0; /*BR_UP | BR_DEBUG*/;	/* enable bridge */
	br_stats.policy = BR_ACCEPT;			/* Enable bridge to accpet all protocols */
	br_stats.exempt_protocols = 0;
	/*start_hello_timer();*/
}

static void br_init_port(int port_no)
{
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

	int         root;
	int             port_no;
	root = root_bridge();
	for (port_no = One; port_no <= No_of_ports; port_no++) {	/* (4.8.4.2) */
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

static void set_port_priority(int port_no, unsigned short new_port_id)
                                  		  /* (4.8.5)	 */
{
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
		) {
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
	int             port_no;

	if (hello_timer_expired()) {
		hello_timer_expiry();
	}
	if (tcn_timer_expired()) {
		tcn_timer_expiry();
	}
	if (topology_change_timer_expired()) {
		topology_change_timer_expiry();
	}
	for (port_no = One; port_no <= No_of_ports; port_no++) {
		if (forward_delay_timer_expired(port_no)) {
			forward_delay_timer_expiry(port_no);
		}
		if (message_age_timer_expired(port_no)) {
			message_age_timer_expiry(port_no);
		}
		if (hold_timer_expired(port_no)) {
			hold_timer_expiry(port_no);
		}
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
	if (hello_timer.active && (++hello_timer.value >= bridge_info.hello_time)) {
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
	if (tcn_timer.active && (++tcn_timer.value >=
				 bridge_info.bridge_hello_time)) {
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
			&& (++topology_change_timer.value
			    >= bridge_info.topology_change_time
			    )) {
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
	if (message_age_timer[port_no].active &&
	      (++message_age_timer[port_no].value >= bridge_info.max_age)) {
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
		if (forward_delay_timer[port_no].active &&
				(++forward_delay_timer[port_no].value >= bridge_info.forward_delay)) {
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
		   (++hold_timer[port_no].value >= bridge_info.hold_time)) {
		hold_timer[port_no].active = FALSE;
		return (TRUE);
	}
	return (FALSE);

}

static int send_config_bpdu(int port_no, Config_bpdu *config_bpdu)
{
struct sk_buff *skb;
struct device *dev = port_info[port_no].dev;
int size;
unsigned long flags;
struct ethhdr *eth;
	
	if (port_info[port_no].state == Disabled) {
		printk(KERN_DEBUG "send_config_bpdu: port %i not valid\n",port_no);
		return(-1);
		}
	if (br_stats.flags & BR_DEBUG)
		printk("send_config_bpdu: ");
	/*
	 * create and send the message
	 */
	size = dev->hard_header_len + sizeof(Config_bpdu);
	skb = alloc_skb(size, GFP_ATOMIC);
	if (skb == NULL) {
		printk(KERN_DEBUG "send_config_bpdu: no skb available\n");
		return(-1);
		}
	skb->dev = dev;
	skb->mac.raw = skb->h.raw = skb_put(skb, size);
	eth = skb->mac.ethernet;
	memcpy(eth->h_dest, bridge_ula, ETH_ALEN);
	memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
	if (br_stats.flags & BR_DEBUG)
		printk("port %i src %02x:%02x:%02x:%02x:%02x:%02x\
			dest %02x:%02x:%02x:%02x:%02x:%02x\n", 
			port_no,
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
	eth->h_proto = htons(0x8038);

	skb->h.raw += skb->dev->hard_header_len;
	memcpy(skb->h.raw, config_bpdu, sizeof(Config_bpdu));

	/* won't get bridged again... */
	skb->pkt_bridged = IS_BRIDGED;
	skb->arp = 1;	/* do not resolve... */
	save_flags(flags);
	cli();
	skb_queue_tail(dev->buffs, skb);
	restore_flags(flags);
	return(0);
}

static int send_tcn_bpdu(int port_no, Tcn_bpdu *bpdu)
{
struct sk_buff *skb;
struct device *dev = port_info[port_no].dev;
int size;
unsigned long flags;
struct ethhdr *eth;
	
	if (port_info[port_no].state == Disabled) {
		printk(KERN_DEBUG "send_tcn_bpdu: port %i not valid\n",port_no);
		return(-1);
		}
	if (br_stats.flags & BR_DEBUG)
		printk("send_tcn_bpdu: ");
	size = sizeof(Tcn_bpdu) + dev->hard_header_len;
	skb = alloc_skb(size, GFP_ATOMIC);
	if (skb == NULL) {
		printk(KERN_DEBUG "send_tcn_bpdu: no skb available\n");
		return(-1);
		}
	skb->dev = dev;
	skb->mac.raw = skb->h.raw = skb_put(skb,size);
	eth = skb->mac.ethernet;
	memcpy(eth->h_dest, bridge_ula, ETH_ALEN);
	memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
	if (br_stats.flags & BR_DEBUG)
		printk("port %i src %02x:%02x:%02x:%02x:%02x:%02x\
			dest %02x:%02x:%02x:%02x:%02x:%02x\n", 
			port_no,
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
	eth->h_proto = htons(0x8038);

	skb->h.raw += skb->dev->hard_header_len;
	memcpy(skb->h.raw, bpdu, sizeof(Tcn_bpdu));

	/* mark that we've been here... */
	skb->pkt_bridged = IS_BRIDGED;
	skb->arp = 1;	/* do not resolve... */
	save_flags(flags);
	cli();
	skb_queue_tail(dev->buffs, skb);
	restore_flags(flags);
	return(0);
}

static int br_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
	struct device *dev = ptr;
	int i;

	/* check for loopback devices */
	if (dev->flags & IFF_LOOPBACK)
		return(NOTIFY_DONE);

	switch (event) {
	case NETDEV_DOWN:
		if (br_stats.flags & BR_DEBUG)
			printk("br_device_event: NETDEV_DOWN...\n");
		/* find our device and mark it down */
		for (i = One; i <= No_of_ports; i++) {
			if (port_info[i].dev == dev) {
				disable_port(i);
				return NOTIFY_DONE;
				break;
			}
		}
		break;
	case NETDEV_UP:
		if (br_stats.flags & BR_DEBUG)
			printk("br_device_event: NETDEV_UP...\n");
		/* Only handle ethernet ports */
		if(dev->type!=ARPHRD_ETHER && dev->type!=ARPHRD_LOOPBACK)
			return NOTIFY_DONE;
		/* look up an unused device and enable it */
		for (i = One; i <= No_of_ports; i++) {
			if ((port_info[i].dev == (struct device *)0) ||
				(port_info[i].dev == dev)) {
				port_info[i].dev = dev;
				enable_port(i);
				set_path_cost(i, br_port_cost(dev));
				set_port_priority(i, 128); 
				port_info[i].port_id = i;
				/* set bridge addr from 1st device addr */
				if ((bridge_info.bridge_id.BRIDGE_ID[0] == 0) &&
						(bridge_info.bridge_id.BRIDGE_ID[1] == 0)) {
					memcpy(bridge_info.bridge_id.BRIDGE_ID_ULA, dev->dev_addr, 6);
					bridge_info.bridge_id.BRIDGE_PRIORITY = port_info[i].port_id;
					set_bridge_priority(&bridge_info.bridge_id);
				}	
				make_forwarding(i);
				return NOTIFY_DONE;
				break;
			}
		}
		break;
#if 0
	default:
		printk("br_device_event: unknown event [%x]\n",
			(unsigned int)event);
#endif			
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
	struct ethhdr *eth;
	
	if (br_stats.flags & BR_DEBUG)
		printk("br_receive_frame: ");
	/* sanity */
	if (!skb) {
		printk(KERN_CRIT "br_receive_frame: no skb!\n");
		return(1);
	}

	skb->pkt_bridged = IS_BRIDGED;

	/* check for loopback */
	if (skb->dev->flags & IFF_LOOPBACK)
		return(0);

	port = find_port(skb->dev);
	
	skb->arp = 1;		/* Received frame so it is resolved */
	skb->h.raw = skb->mac.raw;
	eth = skb->mac.ethernet;
	if (br_stats.flags & BR_DEBUG)
		printk("port %i src %02x:%02x:%02x:%02x:%02x:%02x\
			dest %02x:%02x:%02x:%02x:%02x:%02x\n", 
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

	if (!port) {
		if(br_stats.flags&BR_DEBUG)
			printk("\nbr_receive_frame: no port!\n");
		return(0);
	}

	switch (port_info[port].state) 
	{
		case Learning:
			(void) br_learn(skb, port);	/* 3.8 */
			/* fall through */
		case Listening:
			/* process BPDUs */
			if (memcmp(eth->h_dest, bridge_ula, 6) == 0) {
				br_bpdu(skb);
				return(1); /* br_bpdu consumes skb */
			}
			/* fall through */
		case Blocking:
			/* fall through */
		case Disabled:
			/* should drop frames, but for now, we let
			 * them get passed up to the next higher layer
			return(br_drop(skb));	
			 */
			return(0);	/* pass frame up stack */
			break;
		case Forwarding:
			(void) br_learn(skb, port);	/* 3.8 */
			/* process BPDUs */
			if (memcmp(eth->h_dest, bridge_ula, 
					ETH_ALEN) == 0) 
			{
				/*printk("frame bpdu processor for me!!!\n");*/
				br_bpdu(skb);
				return(1); /* br_bpdu consumes skb */
			}
			/* is frame for me? */	
			if (memcmp(eth->h_dest, 
					port_info[port].dev->dev_addr, 
					ETH_ALEN) == 0) 
			{
				/* Packet is for us */
				skb->pkt_type = PACKET_HOST;
				return(0);	/* pass frame up our stack (this will */
						/* happen in net_bh() in dev.c) */
			}
			/* Now this frame came from one of bridged
			   ports, and it appears to be not for me;
			   this means we should attempt to forward it.
			   But actually this frame can still be for me
			   [as well] if it is destined to one of our
			   multicast groups.  br_forward() will not
			   consume the frame if this is the case */
			return(br_forward(skb, port));
		default:
			printk(KERN_DEBUG "br_receive_frame: port [%i] unknown state [%i]\n",
				port, port_info[port].state);
			return(0);	/* pass frame up stack? */
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
	if (! find_port(skb->dev))
		return(0);

	skb->mac.raw = skb->h.raw = skb->data;
	eth = skb->mac.ethernet;
	port = 0;	/* an impossible port */	
	if (br_stats.flags & BR_DEBUG)
		printk("br_tx_fr : port %i src %02x:%02x:%02x:%02x:%02x:%02x\
	  		dest %02x:%02x:%02x:%02x:%02x:%02x\n", 
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

/*
 * this routine returns 0 when it learns (or updates) from the
 * frame, and -1 if the frame is simply discarded due to port
 * state or lack of resources...
 */

static int br_learn(struct sk_buff *skb, int port)	/* 3.8 */
{
	struct fdb *f;

	switch (port_info[port].state) {
		case Listening:
		case Blocking:
		case Disabled:
		default:
			return(-1);
			/* break; */
		case Learning:
		case Forwarding:
			/* don't keep group addresses in the tree */
			if (skb->mac.ethernet->h_source[0] & 0x01)
				return(-1);
			
			f = (struct fdb *)kmalloc(sizeof(struct fdb), 
				GFP_ATOMIC);

			if (!f) {
				printk(KERN_DEBUG "br_learn: unable to malloc fdb\n");
				return(-1);
			}
			f->port = port;	/* source port */
			memcpy(f->ula, skb->mac.ethernet->h_source, 6);
			f->timer = CURRENT_TIME;
			f->flags = FDB_ENT_VALID;
			/*
			 * add entity to AVL tree.  If entity already
			 * exists in the tree, update the fields with
			 * what we have here.
		  	 */
			if (br_avl_insert(f) == 0) { /* update */
				kfree(f);
				return(0);
			}
			/* add to head of port chain */
			f->fdb_next = port_info[port].fdb;
			port_info[port].fdb = f;
			return(0);
			/* break */
	}
}

/*
 * this routine always consumes the frame
 */

static int br_drop(struct sk_buff *skb)
{
	kfree_skb(skb, 0);
	return(1);
}

/*
 * this routine always consumes the frame
 */

static int br_dev_drop(struct sk_buff *skb)
{
	dev_kfree_skb(skb, 0);
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
		if (port == 0) 			/* locally generated */
			return(br_dev_drop(skb));
		return(0);
	} else {
		/* locate port to forward to */
		f = br_avl_find_addr(skb->mac.ethernet->h_dest);
		/*
		 *	Send flood and drop.
		 */
		if (!f || !(f->flags & FDB_ENT_VALID)) {
		 	/* not found; flood all ports */
			br_flood(skb, port);
			return(br_dev_drop(skb));
		}
		/*
		 *	Sending
		 */
		if (f->port!=port && port_info[f->port].state == Forwarding) {
			/* has entry expired? */
			if (f->timer + fdb_aging_time < CURRENT_TIME) {
				/* timer expired, invalidate entry */
				f->flags &= ~FDB_ENT_VALID;
				if (br_stats.flags & BR_DEBUG)
					printk("fdb entry expired...\n");
				/*
				 *	Send flood and drop original
				 */
				br_flood(skb, port);
				return(br_dev_drop(skb));
			}
			/* mark that's we've been here... */
			skb->pkt_bridged = IS_BRIDGED;
			
			/* reset the skb->ip pointer */	
			skb->h.raw = skb->data + ETH_HLEN;

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
		} else {
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
		if (i == port)
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
			nskb->arp = 1;
			
/*			printk("Flood to port %d\n",i);*/
			nskb->h.raw = nskb->data + ETH_HLEN;
			nskb->priority = 1;
			dev_queue_xmit(nskb);
		}
	}
	return(0);
}

static int find_port(struct device *dev)
{
	int i;

	for (i = One; i <= No_of_ports; i++)
		if ((port_info[i].dev == dev) && 
			(port_info[i].state != Disabled))
			return(i);
	return(0);
}

static int br_port_cost(struct device *dev)	/* 4.10.2 */
{
	if (strncmp(dev->name, "eth", 3) == 0)	/* ethernet */
		return(100);
	if (strncmp(dev->name, "wic", 3) == 0)	/* wic */
		return(1600);
	if (strncmp(dev->name, "plip",4) == 0) /* plip */
		return (1600);
	return(100);	/* default */
}

/*
 * this routine always consumes the skb 
 */

static void br_bpdu(struct sk_buff *skb) /* consumes skb */
{
	Tcn_bpdu *bpdu;
	int port;

	port = find_port(skb->dev);
	if (port == 0) {	/* unknown port */
		br_drop(skb);
		return;
	}
		
	bpdu = (Tcn_bpdu *) (skb->data + ETH_HLEN);
	switch (bpdu->type) {
		case BPDU_TYPE_CONFIG:
			received_config_bpdu(port, (Config_bpdu *)bpdu);
			break;
		case BPDU_TYPE_TOPO_CHANGE:
			received_tcn_bpdu(port, bpdu);
			break;
		default:
			printk(KERN_DEBUG "br_bpdu: received unknown bpdu, type = %i\n",
				bpdu->type);
			/* break; */
	}
	br_drop(skb);
}

int br_ioctl(unsigned int cmd, void *arg)
{
	int err;
	struct br_cf bcf;

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
			if (!suser())
				return -EPERM;
			err = copy_from_user(&bcf, arg, sizeof(struct br_cf));
			if (err)
				return -EFAULT; 
			switch (bcf.cmd) {
				case BRCMD_BRIDGE_ENABLE:
					if (br_stats.flags & BR_UP)
						return(-EALREADY);	
					printk(KERN_DEBUG "br: enabling bridging function\n");
					br_stats.flags |= BR_UP;	/* enable bridge */
					start_hello_timer();
					break;
				case BRCMD_BRIDGE_DISABLE:
					if (!(br_stats.flags & BR_UP))
						return(-EALREADY);	
					printk(KERN_DEBUG "br: disabling bridging function\n");
					br_stats.flags &= ~BR_UP;	/* disable bridge */
					stop_hello_timer();
#if 0					
					for (i = One; i <= No_of_ports; i++)
						if (port_info[i].state != Disabled)
							disable_port(i);
#endif							
					break;
				case BRCMD_PORT_ENABLE:
					if (port_info[bcf.arg1].dev == 0)
						return(-EINVAL);
					if (port_info[bcf.arg1].state != Disabled)
						return(-EALREADY);
					printk(KERN_DEBUG "br: enabling port %i\n",bcf.arg1);
					enable_port(bcf.arg1);
					break;
				case BRCMD_PORT_DISABLE:
					if (port_info[bcf.arg1].dev == 0)
						return(-EINVAL);
					if (port_info[bcf.arg1].state == Disabled)
						return(-EALREADY);
					printk(KERN_DEBUG "br: disabling port %i\n",bcf.arg1);
					disable_port(bcf.arg1);
					break;
				case BRCMD_SET_BRIDGE_PRIORITY:
					set_bridge_priority((bridge_id_t *)&bcf.arg1);
					break;
				case BRCMD_SET_PORT_PRIORITY:
					if (port_info[bcf.arg1].dev == 0)
						return(-EINVAL);
					set_port_priority(bcf.arg1, bcf.arg2);
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
		if (a[i] == b[i])
			continue;
		if (a[i] < b[i])
			return(1);
		if (a[i] > b[i])
			return(-1);
	}
	return(0);
}

/*
 *	NetBEUI data structures
 */
 
#ifndef __NET_NETBEUI_H
#define __NET_NETBEUI_H


#define NB_NAME_LEN	16

/*
 *	Used to keep lists of netbeui sessions
 */
 
struct nb_ses
{
	struct nb_ses *next;
	struct nb_nam *name;
	struct nb_link *parent;	/* Owner link */
	struct sock *sk;
};

/*
 *	A netbeui link
 */
 
struct nb_link
{
	struct  llc llc;	/* Must be first */
	u8	mac[6];		/* Mac address of remote */
	struct device *dev;	/* Device we heard him on */
	struct nb_ses *sessions;/* Netbeui sessions on this LLC link */
	struct wait_queue *wait;/* Wait queue for this netbios LLC */
	int	busy;		/* Track the LLC busy state */
	int	state;		/* Link state */
#define NETBEUI_OPEN		1	/* Up and going */
#define NETBEUI_CONNWAIT	2	/* Waiting to come up */
#define NETBEUI_DISCWAIT	3	/* Waiting to drop and recover */
#define NETBEUI_DEADWAIT	4	/* Trying to die */
};


/*
 *	Netbios name defence list
 */

struct nb_name
{
	struct nb_name *next;	/*	Chain 		*/
	struct device *dev;	/*	Device 		*/
	char name[NB_NAME_LEN];	/* 	Object Name	*/
	int state;		/* 	Name State	*/
#define NB_NAME_ACQUIRE		1	/* We are trying to get a name */
#define NB_NAME_COLLIDE		2	/* Name collided - we failed */
#define NB_OURS			3	/* We own the name	*/
#define NB_NAME_OTHER		4	/* Name found - owned by other */
#define NB_NAME_GET		5	/* Trying to allocate a name */
#define NB_STATE		7	/* State bits */
#define NB_NAME_GROUP		8	/* Group name bit */
	int ours;			/* We own this name */
	int users;			/* Number of nb_ses's to this name */
	struct timer_list	timer;	/* Our timer */
	int timer_mode;			/* Timer mode */
#define NB_TIMER_ACQUIRE	1	/* Expiry means we got our name */
#define NB_TIMER_COLLIDE	2	/* Expire a collided record */
#define NB_TIMER_DROP		3	/* Drop a learned record */	
};


/*
 *	LLC link manager
 */
 
extern struct nb_link *netbeui_find_link(u8 macaddr);
extern struct nb_link *netbeui_create_link(u8 macaddr);
extern int netbeui_destroy_link(u8 macaddr);

/*
 *	Namespace manager
 */
 
extern struct nb_name *netbeui_find_name(char *name);
extern struct nb_name *netbeui_add_name(char *name, int ours);
extern struct nb_name *netbeui_lookup_name(char *name);
extern int nb_delete_name(struct nb_name *name);

/*
 *	NetBEUI Protocol items
 */

#define ADD_GROUP_NAME_QUERY	0x00
#define ADD_NAME_QUERY		0x01
#define NAME_IN_CONFLICT	0x02
#define STATUS_QUERY		0x03
#define TERMINATE_TRACE		0x07
#define DATAGRAM		0x08
#define DATAGRAM_BROADCAST	0x09
#define NAME_QUERY		0x0A
#define ADD_NAME_RESPONSE	0x0D
#define NAME_RECOGNIZED		0x0E
#define STATUS_RESPONSE		0x0F
#define TERMINATE_TRACE2	0x13
#define DATA_ACK		0x14
#define DATA_FIRST_MIDDLE	0x15
#define DATA_ONLY_LAST		0x16
#define SESSION_CONFIRM		0x17
#define SESSION_END		0x18
#define SESSION_INITIALIZE	0x19
#define NO_RECEIVE		0x1A
#define RECEIVE_OUTSTANDING	0x1B
#define RECEIVE_CONTINUE	0x1C
#define SESSION_ALIVE		0x1F

#define NB_TRANSMIT_COUNT	6
#define NB_TRANSMIT_TIMEOUT	(HZ/2)

#define NB_DESCRIM_1		0xEF
#define NB_DESCRIM_2		0xFF

struct nb_dgram_pkt
{
	__u16	length;
	__u8	descrim1;
	__u8	descrim2;
	__u8	command;
	__u8	option1;
	__u16	option2;
	__u16	tx_seq;
	__u16	rx_seq;
	__u8	dest[NB_NAME_LEN];
	__u8	src[NB_NAME_LEN];
};

struct nb_sess_pkt
{
	__u16	length;
	__u8	descrim1;
	__u8	descrim2;
	__u8	command;
	__u8	option1;
	__u16	option2;
	__u16	tx_seq;
	__u16	rx_seq;
	__u8	dnum;
	__u8	snum;
};

#define NO_SEQ	0

#endif

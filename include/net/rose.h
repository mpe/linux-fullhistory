/*
 *	Declarations of Rose type objects.
 *
 *	Jonathan Naylor G4KLX	25/8/96
 */
 
#ifndef _ROSE_H
#define _ROSE_H 
#include <linux/rose.h>

#define	ROSE_ADDR_LEN			5

#define	ROSE_MIN_LEN			3

#define	GFI				0x10
#define	Q_BIT				0x80
#define	D_BIT				0x40
#define	M_BIT				0x10
 
#define	ROSE_CALL_REQUEST		0x0B
#define	ROSE_CALL_ACCEPTED		0x0F
#define	ROSE_CLEAR_REQUEST		0x13
#define	ROSE_CLEAR_CONFIRMATION		0x17
#define	ROSE_DATA			0x00
#define	ROSE_INTERRUPT			0x23
#define	ROSE_INTERRUPT_CONFIRMATION	0x27
#define	ROSE_RR				0x01
#define	ROSE_RNR			0x05
#define	ROSE_REJ			0x09
#define	ROSE_RESET_REQUEST		0x1B
#define	ROSE_RESET_CONFIRMATION		0x1F
#define	ROSE_REGISTRATION_REQUEST	0xF3
#define	ROSE_REGISTRATION_CONFIRMATION	0xF7
#define	ROSE_RESTART_REQUEST		0xFB
#define	ROSE_RESTART_CONFIRMATION	0xFF
#define	ROSE_DIAGNOSTIC			0xF1
#define	ROSE_ILLEGAL			0xFD

/* Define Link State constants. */

#define ROSE_STATE_0		0		/* Ready */
#define ROSE_STATE_1		1		/* Awaiting Call Accepted */
#define ROSE_STATE_2		2		/* Awaiting Clear Confirmation */
#define ROSE_STATE_3		3		/* Data Transfer */
#define	ROSE_STATE_4		4		/* Awaiting Reset Confirmation */

#define ROSE_DEFAULT_T0		(180 * PR_SLOWHZ)	/* Default T10 T20 value */
#define ROSE_DEFAULT_T1		(200 * PR_SLOWHZ)	/* Default T11 T21 value */
#define ROSE_DEFAULT_T2		(180 * PR_SLOWHZ)	/* Default T12 T22 value */
#define	ROSE_DEFAULT_T3		(180 * PR_SLOWHZ)	/* Default T13 T23 value */
#define	ROSE_DEFAULT_IDLE	(20 * 60 * PR_SLOWHZ)	/* Default No Activity value */
#define	ROSE_DEFAULT_WINDOW	2			/* Default Window Size	*/
#define ROSE_MODULUS 		8
#define ROSE_MAX_WINDOW_SIZE	7			/* Maximum Window Allowable */
#define	ROSE_PACLEN		128			/* Default Packet Length */

#define	FAC_NATIONAL		0x00
#define	FAC_CCITT		0x0F

#define	FAC_NATIONAL_RAND	0x7F
#define	FAC_NATIONAL_FLAGS	0x3F
#define	FAC_NATIONAL_DEST_DIGI	0xE9
#define	FAC_NATIONAL_SRC_DIGI	0xEB

#define	FAC_CCITT_DEST_NSAP	0xC9
#define	FAC_CCITT_SRC_NSAP	0xCB

struct rose_neigh {
	struct rose_neigh *next;
	ax25_address      callsign;
	ax25_digi         *digipeat;
	struct device     *dev;
	unsigned short    count;
	unsigned int      number;
	int               restarted;
	struct sk_buff_head queue;
	unsigned short    t0, t0timer;
	struct timer_list timer;
};

struct rose_node {
	struct rose_node  *next;
	rose_address      address;
	unsigned char     which;
	unsigned char     count;
	struct rose_neigh *neighbour[3];
};

struct rose_route {
	struct rose_route *next;
	unsigned int	  lci1,    lci2;
	struct rose_neigh *neigh1, *neigh2;
	unsigned int      rand;
};

typedef struct {
	rose_address		source_addr,   dest_addr;
	ax25_address		source_call,   dest_call;
	unsigned char		source_ndigis, dest_ndigis;
	ax25_address		source_digi,   dest_digi;
	struct rose_neigh	*neighbour;
	struct device		*device;
	unsigned int		lci, rand;
	unsigned char		state, condition, hdrincl;
	unsigned short		vs, vr, va, vl;
	unsigned short		timer;
	unsigned short		t1, t2, t3, idle;
	unsigned short		fraglen;
	struct sk_buff_head	ack_queue;
	struct sk_buff_head	frag_queue;
	struct sock		*sk;		/* Backlink to socket */
} rose_cb;

/* af_rose.c */
extern int  sysctl_rose_restart_request_timeout;
extern int  sysctl_rose_call_request_timeout;
extern int  sysctl_rose_reset_request_timeout;
extern int  sysctl_rose_clear_request_timeout;
extern int  sysctl_rose_no_activity_timeout;
extern int  sysctl_rose_routing_control;
extern int  rosecmp(rose_address *, rose_address *);
extern char *rose2asc(rose_address *);
extern struct sock *rose_find_socket(unsigned int, struct device *);
extern unsigned int rose_new_lci(struct device *);
extern int  rose_rx_call_request(struct sk_buff *, struct device *, struct rose_neigh *, unsigned int);
extern void rose_destroy_socket(struct sock *);

/* rose_dev.c */
extern int  rose_rx_ip(struct sk_buff *, struct device *);
extern int  rose_init(struct device *);

#include <net/rosecall.h>

/* rose_in.c */
extern int  rose_process_rx_frame(struct sock *, struct sk_buff *);

/* rose_link.c */
extern void rose_link_rx_restart(struct sk_buff *, struct rose_neigh *, unsigned short);
extern void rose_transmit_restart_request(struct rose_neigh *);
extern void rose_transmit_restart_confirmation(struct rose_neigh *);
extern void rose_transmit_diagnostic(struct rose_neigh *, unsigned char);
extern void rose_transmit_clear_request(struct rose_neigh *, unsigned int, unsigned char);
extern void rose_transmit_link(struct sk_buff *, struct rose_neigh *);

/* rose_out.c */
extern void rose_output(struct sock *, struct sk_buff *);
extern void rose_kick(struct sock *);
extern void rose_enquiry_response(struct sock *);
extern void rose_check_iframes_acked(struct sock *, unsigned short);

/* rose_route.c */
extern void rose_rt_device_down(struct device *);
extern void rose_link_device_down(struct device *);
extern struct device *rose_dev_first(void);
extern struct device *rose_dev_get(rose_address *);
extern struct device *rose_ax25_dev_get(char *);
extern struct rose_neigh *rose_get_neigh(rose_address *);
extern int  rose_rt_ioctl(unsigned int, void *);
extern void rose_link_failed(ax25_address *, struct device *);
extern int  rose_route_frame(struct sk_buff *, ax25_cb *);
extern int  rose_nodes_get_info(char *, char **, off_t, int, int);
extern int  rose_neigh_get_info(char *, char **, off_t, int, int);
extern int  rose_routes_get_info(char *, char **, off_t, int, int);
extern void rose_rt_free(void);

/* rose_subr.c */
extern void rose_clear_queues(struct sock *);
extern void rose_frames_acked(struct sock *, unsigned short);
extern void rose_requeue_frames(struct sock *);
extern int  rose_validate_nr(struct sock *, unsigned short);
extern void rose_write_internal(struct sock *, int);
extern int  rose_decode(struct sk_buff *, int *, int *, int *, int *, int *);
extern int  rose_parse_facilities(struct sk_buff *, rose_cb *);
extern int  rose_create_facilities(unsigned char *, rose_cb *);

/* rose_timer.c */
extern void rose_set_timer(struct sock *);

/* sysctl_net_rose.c */
extern void rose_register_sysctl(void);
extern void rose_unregister_sysctl(void);

#endif

/*
 *	Declarations of AX.25 type objects.
 *
 *	Alan Cox (GW4PTS) 	10/11/93
 */
 
#ifndef _AX25_H
#define _AX25_H 
#include <linux/ax25.h>
 
#define AX25_P_IP	0xCC
#define AX25_P_ARP	0xCD
#define AX25_P_TEXT 	0xF0
#define AX25_P_NETROM 	0xCF

#define LAPB_UI	0x03
#define LAPB_C	0x80
#define LAPB_E	0x01

#define SSID_SPARE	0x60		/* Unused bits (DAMA bit and spare must be 1) */

#define AX25_REPEATED	0x80

#define	ACK_PENDING_CONDITION		0x01
#define	REJECT_CONDITION		0x02
#define	PEER_RX_BUSY_CONDITION		0x04
#define	OWN_RX_BUSY_CONDITION		0x08

#ifndef _LINUX_NETDEVICE_H
#include <linux/netdevice.h>
#endif

/*
 * These headers are taken from the KA9Q package by Phil Karn. These specific
 * files have been placed under the GPL (not the whole package) by Phil.
 *
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 dated June, 1991.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave., Cambridge, MA 02139, USA.
 */

/* Upper sub-layer (LAPB) definitions */

/* Control field templates */
#define	I	0x00	/* Information frames */
#define	S	0x01	/* Supervisory frames */
#define	RR	0x01	/* Receiver ready */
#define	RNR	0x05	/* Receiver not ready */
#define	REJ	0x09	/* Reject */
#define	U	0x03	/* Unnumbered frames */
#define	SABM	0x2f	/* Set Asynchronous Balanced Mode */
#define	SABME	0x6f	/* Set Asynchronous Balanced Mode Extended */
#define	DISC	0x43	/* Disconnect */
#define	DM	0x0f	/* Disconnected mode */
#define	UA	0x63	/* Unnumbered acknowledge */
#define	FRMR	0x87	/* Frame reject */
#define	UI	0x03	/* Unnumbered information */
#define	PF	0x10	/* Poll/final bit */

#define ILLEGAL	0x100	/* Impossible to be a real frame type */

#define	MMASK	7	/* Mask for modulo-8 sequence numbers */

/* AX25 L2 C-bit */

#define C_COMMAND	1	/* C_ otherwise it clashes with the de600 defines (sigh)) */
#define C_RESPONSE	2

/* Define Link State constants. */

#define AX25_STATE_0	0
#define AX25_STATE_1	1
#define AX25_STATE_2	2
#define AX25_STATE_3	3
#define AX25_STATE_4	4

#define PR_SLOWHZ	10			/*  Run timing at 1/10 second - gives us better resolution for 56kbit links */
#define DEFAULT_T1	(10  * PR_SLOWHZ)	/*  Outstanding frames - 10 seconds */
#define DEFAULT_T2	(3   * PR_SLOWHZ)	/*  Response delay     - 3 seconds */
#define DEFAULT_T3	(300 * PR_SLOWHZ)	/*  Idle supervision   - 300 seconds */
#define DEFAULT_N2	10			/*  Number of retries */
#define	DEFAULT_WINDOW	2			/*  Default window size	*/
#define MODULUS 	8
#define MAX_WINDOW_SIZE 7			/*  Maximum window allowable */

typedef struct ax25_uid_assoc {
	struct ax25_uid_assoc *next;
	uid_t uid;
	ax25_address call;
} ax25_uid_assoc;

typedef struct {
	ax25_address calls[6];
	unsigned char repeated[6];
	unsigned char ndigi;
	char lastrepeat;
} ax25_digi;

typedef struct ax25_cb {
	struct ax25_cb		*next;
	ax25_address		source_addr, dest_addr;
	struct device		*device;
	unsigned char		state;
	unsigned short		vs, vr, va;
	unsigned char		condition, backoff;
	unsigned char		n2, n2count;
	unsigned short		t1, t2, t3, rtt;
	unsigned short		t1timer, t2timer, t3timer;
	ax25_digi		*digipeat;
	struct sk_buff_head	write_queue;
	struct sk_buff_head	ack_queue;
	unsigned char		window;
	struct timer_list	timer;
	struct sock		*sk;		/* Backlink to socket */
} ax25_cb;

/* ax25.c */
extern char *ax2asc(ax25_address *);
extern int  ax25cmp(ax25_address *, ax25_address *);
extern int  ax25_send_frame(struct sk_buff *, ax25_address *, ax25_address *, struct device *);
extern int  ax25_rcv(struct sk_buff *,struct device *,struct packet_type *);
extern void ax25_destroy_socket(ax25_cb *);
extern struct device *ax25rtr_get_dev(ax25_address *);
extern int  ax25_encapsulate(struct sk_buff *, struct device *, unsigned short,
	void *, void *, unsigned int);
extern int  ax25_rebuild_header(unsigned char *, struct device *, unsigned long, struct sk_buff *);
extern int  ax25_get_info(char *, char **, off_t, int);
extern ax25_uid_assoc *ax25_uid_list;
extern int  ax25_uid_policy;
extern ax25_address *ax25_findbyuid(uid_t);

#include "ax25call.h"

/* ax25_in.c */
extern int  ax25_process_rx_frame(ax25_cb *, struct sk_buff *, int);

/* ax25_out.c */
extern int  ax25_output(ax25_cb *, struct sk_buff *);
extern void ax25_kick(ax25_cb *);
extern void ax25_transmit_buffer(ax25_cb *, struct sk_buff *, int);
extern void ax25_nr_error_recovery(ax25_cb *);
extern void ax25_establish_data_link(ax25_cb *);
extern void ax25_transmit_enquiry(ax25_cb *);
extern void ax25_enquiry_response(ax25_cb *);
extern void ax25_check_iframes_acked(ax25_cb *, unsigned short);
extern void ax25_check_need_response(ax25_cb *, int, int);

/* ax25_route.c */
extern void ax25_rt_rx_frame(ax25_address *, struct device *);
extern int  ax25_rt_get_info(char *, char **, off_t, int);
extern int  ax25_cs_get_info(char *, char **, off_t, int);
extern int  ax25_rt_autobind(ax25_cb *, ax25_address *);
extern void ax25_rt_device_down(struct device *);
extern void ax25_ip_mode_set(ax25_address *, struct device *, char);
extern char ax25_ip_mode_get(ax25_address *, struct device *);

/* ax25_subr.c */
extern void ax25_clear_tx_queue(ax25_cb *);
extern void ax25_frames_acked(ax25_cb *, unsigned short);
extern int  ax25_validate_nr(ax25_cb *, unsigned short);
extern int  ax25_decode(unsigned char *);
extern void ax25_send_control(ax25_cb *, int, int);
extern unsigned short ax25_calculate_t1(ax25_cb *);
extern void ax25_calculate_rtt(ax25_cb *);
extern unsigned char *ax25_parse_addr(unsigned char *, int, ax25_address *,
	ax25_address *, ax25_digi *, int *);
extern int  build_ax25_addr(unsigned char *, ax25_address *, ax25_address *,
	ax25_digi *, int);
extern int  size_ax25_addr(ax25_digi *);
extern void ax25_digi_invert(ax25_digi *, ax25_digi *);
extern void ax25_return_dm(struct device *, ax25_address *, ax25_address *, ax25_digi *);

/* ax25_timer */
extern void ax25_set_timer(ax25_cb *);

#endif

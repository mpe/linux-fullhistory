#ifndef _LAPB_H
#define _LAPB_H 
#include <linux/lapb.h>

#define LAPB_SLOWHZ	10		/* Run timing at 1/10 second */

#define	LAPB_HEADER_LEN	20		/* LAPB over Ethernet + a bit more */

#define	LAPB_ACK_PENDING_CONDITION	0x01
#define	LAPB_REJECT_CONDITION		0x02
#define	LAPB_PEER_RX_BUSY_CONDITION	0x04

/* Control field templates */
#define	LAPB_I		0x00	/* Information frames */
#define	LAPB_S		0x01	/* Supervisory frames */
#define	LAPB_U		0x03	/* Unnumbered frames */

#define	LAPB_RR		0x01	/* Receiver ready */
#define	LAPB_RNR	0x05	/* Receiver not ready */
#define	LAPB_REJ	0x09	/* Reject */

#define	LAPB_SABM	0x2F	/* Set Asynchronous Balanced Mode */
#define	LAPB_SABME	0x6F	/* Set Asynchronous Balanced Mode Extended */
#define	LAPB_DISC	0x43	/* Disconnect */
#define	LAPB_DM		0x0F	/* Disconnected mode */
#define	LAPB_UA		0x63	/* Unnumbered acknowledge */
#define	LAPB_FRMR	0x87	/* Frame reject */

#define LAPB_ILLEGAL	0x100	/* Impossible to be a real frame type */

#define	LAPB_SPF	0x10	/* Poll/final bit for standard LAPB */
#define	LAPB_EPF	0x01	/* Poll/final bit for extended LAPB */

#define	LAPB_POLLOFF	0
#define	LAPB_POLLON	1

/* LAPB C-bit */
#define LAPB_COMMAND	1
#define LAPB_RESPONSE	2

#define	LAPB_ADDR_A	0x03
#define	LAPB_ADDR_B	0x01
#define	LAPB_ADDR_C	0x0F
#define	LAPB_ADDR_D	0x07

/* Define Link State constants. */
#define	LAPB_STATE_0	0		/* Disconnected State		*/
#define	LAPB_STATE_1	1		/* Awaiting Connection State	*/
#define	LAPB_STATE_2	2		/* Awaiting Disconnection State	*/
#define	LAPB_STATE_3	3		/* Data Transfer State		*/
#define	LAPB_STATE_4	4		/* Frame Reject State		*/

#define	LAPB_DEFAULT_MODE		(LAPB_STANDARD | LAPB_SLP | LAPB_DTE)
#define	LAPB_DEFAULT_WINDOW		7			/* Window=7 */
#define	LAPB_DEFAULT_T1			(5 * LAPB_SLOWHZ)	/* T1=5s    */
#define	LAPB_DEFAULT_T2			(1 * LAPB_SLOWHZ)	/* T2=1s    */
#define	LAPB_DEFAULT_N2			20			/* N2=20    */

#define	LAPB_SMODULUS	8
#define	LAPB_EMODULUS	128

typedef struct lapb_cb {
	struct lapb_cb		*next;
	void			*token;
	unsigned int		mode;
	unsigned char		state;
	unsigned short		vs, vr, va;
	unsigned char		condition;
	unsigned short		n2, n2count;
	unsigned short		t1, t2;
	unsigned short		t1timer, t2timer;
	struct sk_buff_head	input_queue;
	struct sk_buff_head	write_queue;
	struct sk_buff_head	ack_queue;
	unsigned char		window;
	struct timer_list	timer;
	struct lapb_register_struct callbacks;
} lapb_cb;

/* lapb_iface.c */
extern void lapb_connect_confirmation(lapb_cb *, int);
extern void lapb_connect_indication(lapb_cb *, int);
extern void lapb_disconnect_confirmation(lapb_cb *, int);
extern void lapb_disconnect_indication(lapb_cb *, int);
extern int  lapb_data_indication(lapb_cb *, struct sk_buff *);
extern int  lapb_data_transmit(lapb_cb *, struct sk_buff *);

/* lapb_in.c */
extern void lapb_data_input(lapb_cb *, struct sk_buff *);

/* lapb_out.c */
extern void lapb_kick(lapb_cb *);
extern void lapb_transmit_buffer(lapb_cb *, struct sk_buff *, int);
extern void lapb_establish_data_link(lapb_cb *);
extern void lapb_enquiry_response(lapb_cb *);
extern void lapb_timeout_response(lapb_cb *);
extern void lapb_check_iframes_acked(lapb_cb *, unsigned short);
extern void lapb_check_need_response(lapb_cb *, int, int);

/* lapb_subr.c */
extern void lapb_clear_queues(lapb_cb *);
extern void lapb_frames_acked(lapb_cb *, unsigned short);
extern void lapb_requeue_frames(lapb_cb *);
extern int  lapb_validate_nr(lapb_cb *, unsigned short);
extern int  lapb_decode(lapb_cb *, struct sk_buff *, int *, int *, int *, int *);
extern void lapb_send_control(lapb_cb *, int, int, int);

/* lapb_timer.c */
extern void lapb_set_timer(lapb_cb *);

/*
 * Debug levels.
 *	0 = Off
 *	1 = State Changes
 *	2 = Packets I/O and State Changes
 *	3 = Hex dumps, Packets I/O and State Changes.
 */
#define	LAPB_DEBUG	0

#endif

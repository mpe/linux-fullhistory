/* arequipa.h - Arequipa interface definitions */
 
/* Written 1996-1998 by Jean-Michel Pittet and Werner Almesberger, EPFL ICA */
 

#ifndef _LINUX_AREQUIPA_H
#define _LINUX_AREQUIPA_H

#include <linux/atmioc.h>


enum arequipa_msg_type { amt_invalid,amt_close,amt_sync };

struct arequipa_msg {
	enum arequipa_msg_type type;
	void *ptr;
};


#define AREQUIPA_PRESET		_IO('a',ATMIOC_AREQUIPA)
#define AREQUIPA_INCOMING	_IO('a',ATMIOC_AREQUIPA+1)
#define AREQUIPA_EXPECT		_IO('a',ATMIOC_AREQUIPA+2)
#define AREQUIPA_CLOSE		_IO('a',ATMIOC_AREQUIPA+3)
#define AREQUIPA_CTRL		_IO('a',ATMIOC_AREQUIPA+4)
/* #define AREQUIPA_CLS3RD	removed */
#define AREQUIPA_SYNCREQ	_IO('a',ATMIOC_AREQUIPA+6)
/* #define AREQUIPA_SYNCACK	removed */
#define AREQUIPA_WORK		_IO('a',ATMIOC_AREQUIPA+8)
#define AREQUIPA_RENEGOTIATE	_IO('a',ATMIOC_AREQUIPA+9)


#ifdef __KERNEL__

#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/atm.h>
#include <net/sock.h>


extern struct atm_vcc *aqd; /* for net/atm/proc.c */
/* extern struct rtable *arequipa_rt; - not needed; we use a local dcl instead*/
extern struct net_device *arequipa_dev;

int atm_init_arequipa(void);
int arequipa_attach(struct socket *lower,struct sock *upper,
    unsigned long generation);

int arequipa_preset(struct socket *lower,struct sock *upper);
int arequipa_expect(struct sock *upper,int on,int kmalloc_flags);
int arequipa_incoming(struct socket *lower);
int arequipa_close(struct sock *upper);
int arequipa_renegotiate(struct sock *upper,struct atm_qos *u_qos);
void arequipa_synchronize(void);
void arequipa_work(void);

int arequipad_attach(struct atm_vcc *vcc);


#endif /* __KERNEL__ */

#endif

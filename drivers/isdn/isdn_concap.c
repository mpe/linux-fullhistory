/* $Id: isdn_concap.c,v 1.2 1998/01/31 22:49:21 keil Exp $
 
 * Stuff to support the concap_proto by isdn4linux. isdn4linux - specific
 * stuff goes here. Stuff that depends only on the concap protocol goes to
 * another -- protocol specific -- source file.
 *
 * $Log: isdn_concap.c,v $
 * Revision 1.2  1998/01/31 22:49:21  keil
 * correct comments
 *
 * Revision 1.1  1998/01/31 22:27:57  keil
 * New files from Henner Eisen for X.25 support
 *
 */


#include <linux/isdn.h>
#include "isdn_x25iface.h"
#include "isdn_net.h"
#include <linux/concap.h>
#include "isdn_concap.h"

/* The declaration of this (or a plublic variant thereof) should really go
   in linux/isdn.h. But we really need it here (and isdn_ppp, like us, also
   refers to that private function currently owned by isdn_net.c) */
extern int isdn_net_force_dial_lp(isdn_net_local *);


/* The following set of device service operations are for encapsulation
   protocols that require for reliable datalink sematics. That means:

   - before any data is to be submitted the connection must explicitly
     be set up.
   - after the successful set up of the connection is signalled the
     connection is considered to be reliably up.

   Auto-dialing ist not compatible with this requirements. Thus, auto-dialing 
   is completely bypassed.

   It might be possible to implement a (non standardized) datalink protocol
   that provides a reliable data link service while using some auto dialing
   mechanism. Such a protocol would need an auxiliary channel (i.e. user-user-
   signaling on the D-channel) while the B-channel is down.
   */


int isdn_concap_dl_data_req(struct concap_proto *concap, struct sk_buff *skb)
{
	int tmp;
	struct device *ndev = concap -> net_dev;
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;

	IX25DEBUG( "isdn_concap_dl_data_req: %s \n", concap->net_dev->name);
	lp->huptimer = 0;
	tmp=isdn_net_send_skb(ndev, lp, skb);
	IX25DEBUG( "isdn_concap_dl_data_req: %s : isdn_net_send_skb returned %d\n", concap -> net_dev -> name, tmp);
	return tmp;
}


int isdn_concap_dl_connect_req(struct concap_proto *concap)
{
	struct device *ndev = concap -> net_dev;
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
	int ret;
	IX25DEBUG( "isdn_concap_dl_connect_req: %s \n", ndev -> name);

	/* dial ... */
	ret = isdn_net_force_dial_lp( lp );
	if ( ret ) IX25DEBUG("dialing failed\n");
	return 0;
}

int isdn_concap_dl_disconn_req(struct concap_proto *concap)
{
	IX25DEBUG( "isdn_concap_dl_disconn_req: %s \n", concap -> net_dev -> name);

	isdn_net_hangup( concap -> net_dev );
	return 0;
}

struct concap_device_ops isdn_concap_reliable_dl_dops = {
	&isdn_concap_dl_data_req,
	&isdn_concap_dl_connect_req,
	&isdn_concap_dl_disconn_req
};

struct concap_device_ops isdn_concap_demand_dial_dops = {
	NULL, /* set this first entry to something like &isdn_net_start_xmit,
		 but the entry part of the current isdn_net_start_xmit must be
		 separated first. */
	/* no connection control for demand dial semantics */
	NULL,
	NULL,
};

/* The following should better go into a dedicated source file such that
   this sourcefile does not need to include any protocol specific header
   files. For now:
   */
struct concap_proto * isdn_concap_new( int encap )
{
	switch ( encap ) {
	case ISDN_NET_ENCAP_X25IFACE:
		return isdn_x25iface_proto_new();
	}
	return NULL;
}

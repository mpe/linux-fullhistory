/*
 *	NET3:	802.2 LLC supervisor for the netbeui protocols. 
 *
 *	The basic aim is to provide a self managing link layer supervisor
 *	for netbeui. It creates and destroys the 802.2 virtual connections
 *	as needed, and copes with the various races when a link goes down
 *	just as its requested etc.
 *
 *	The upper layers are presented with the notion of an nb_link which
 *	is a potentially shared object that represents a logical path 
 *	between two hosts. Each nb_link has usage counts and users can
 *	treat it as if its their own.
 */
 
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/datalink.h>
#include <net/p8022.h>
#include <net/psnap.h>
#include <net/sock.h>
#include <net/llc.h>
#include <net/netbeui.h>


/*
 *	When this routine is called the netbeui layer has decided to
 *	drop the link. There is a tiny risk that we might reuse the
 *	link after we decide. Thus before we blast the link into little
 *	tiny pieces we must check....
 */
 
static void netbeui_do_destroy(struct nb_link *nb)
{
	/*
	 *	Are we wanted again. Bring it back. Sigh, wish people
	 *	would make up their minds 8)
	 */
	if(nb->users>0)
	{
		nb->state=NETBEUI_CONNWAIT;
		llc_connect_request(&nb->llc);
		return;
	}
	/*
	 *	Blam.... into oblivion it goes
	 */
	
	llc_unregister(&nb->llc);
	netbeui_free_link(nb);
}

/*
 *	Handle netbeui events. Basically that means keep it up when it
 *	should be up, down when it should be down and handle all the data.
 */

static void netbeui_event(llcptr llc)
{
	struct nb_link *nb=(struct nb_link *)llc;
	
	/*
	 *	See what has occured
	 */
	 

	/*
	 *	Connect completion confirmation
	 */
	 
	if(llc->llc_callbacks&LLC_CONN_CONFIRM)
	{
		/*
		 *	Link up if desired. Otherwise try frantically
		 *	to close it.
		 */
		if(nb->state!=NETBEUI_DEADWAIT)
		{
			/*
			 *	Wake pending writers
			 */
			nb->state=NETBEUI_OPEN;
			netbeui_wakeup(nb);
		}
		else
			llc_disconnect_request(llc);
	}
	
	/*
	 *	Data is passed to the upper netbeui layer
	 */

	if(llc->llc_callbacks&LLC_DATA_INDIC)
	{
		netbeu_rcv_stream(llc,llc->inc_skb);
		/*
		 *	Frame free is controlled by our stream processor
		 */
		return;
	}

	/*
	 *	We got disconnected
	 */
	 
	if(llc->llc_callbacks&LLC_DISC_INDICATION)
	{
		if(nb->state==NETBEUI_DEADWAIT)
		{
			netbeui_do_destroy(nb);
			return;
		}
		if(nb->state==NETBEUI_DISCWAIT)
		{
			llc_connect_request(llc);
			nb->state=NETBEUI_CONNWAIT;
		}
	}
	
	/*
	 *	Miscellaneous burps
	 */
	
	if(llc->llc_callbacks&(LLC_RESET_INDIC_LOC|LLC_RESET_INDIC_REM|
					LLC_RST_CONFIRM))
	{
		/*
		 *	Reset. 
		 *	Q: Is tearing the link down the right answer ?
		 *
		 *	For now we just carry on
		 */
	}

	/*
	 *	Track link busy status
	 */
	 
	if(llc->llc_callbacks&LLC_REMOTE_BUSY)
		nb->busy=1;	/* Send no more for a bit */
	if(llc->llc_callbacks&LLC_REMOTE_NOTBUSY)
	{
		/* Coming unbusy may wake sending threads */
		nb->busy=0;
		netbeui_wakeup(nb);
	}		
	/*
	 *	UI frames are passed to the upper netbeui layer.
	 */
	if(llc->llc_callbacks&LLC_UI_DATA)
	{
		netbeui_rcv_dgram(llc,llc->inc_skb);
		return;
	}

	/* We ignore TST, XID, FRMR stuff */
	/* FIXME: We need to free frames here once I fix the callback! */
	if(llc->inc_skb)
		kfree_skb(skb, FREE_READ);
}

/*
 *	Netbeui has created a new logical link. As a result we will
 *	need to find or create a suitable 802.2 LLC session and join
 *	it.
 */

struct nb_link *netbeui_create_channel(struct device *dev, u8 *remote_mac, int pri)
{
	struct nb_link *nb=netbeui_find_channel(dev,remote_mac);
	if(nb)
	{
		if(nb->state==NETBEUI_DEADWAIT)
		{
			/*
			 *	We had commenced a final shutdown. We
			 *	cannot abort that (we sent the packet) but
			 *	we can shift the mode to DISCWAIT. That will
			 *	cause the disconnect event to bounce us
			 *	back into connected state.
			 */
			nb->state==NETBEUI_DISCWAIT;
		}
		nb->users++;
		return nb;
	}
	nb=netbeui_alloc_link(pri);
	if(nb==NULL)
		return NULL;
	
	/*
	 *	Internal book keeping
	 */
	 
	nb->dev=dev;
	nb->users=1;
	nb->busy=0;
	nb->wakeup=NULL;
	nb->state=NETBEUI_CONNWAIT;
	memcpy(nb->remote_mac, remote_mac, ETH_ALEN);
	
	/*
	 *	Now try and attach an LLC.
	 */
	
	if(register_cl2llc_client(&nb->llc,dev->name,netbeui_event,
		remote_mac, NETBEUI_SAP, NETBEUI_SAP)<0)
	{
		netbeui_free_link(nb);
		return NULL;
	}
	
	/*
	 *	Commence connection establishment.
	 */
	 
	llc_connect_request(&nb->llc);
	
	/*
	 *	Done
	 */

	nb->next=nb_link_list;
	nb_link_list=nb;
	 
	return nb;
}

/*
 *	A logical netbeui channel has died. If the channel has no
 *	further users we commence shutdown.
 */
	
int netbeui_delete_channel(struct nb_link *nb)
{
	nb->users--;
	
	/*
	 *	FIXME: Must remove ourselves from the nb_link chain when
	 *	we add that bit
	 */
	 
	if(nb->users)
		return 0;
		
	/*
	 *	Ensure we drop soon. The disconnect confirm will let
	 *	us fix the deletion. If someone wants the link at
	 *	the wrong moment nothing bad will occur. The create
	 *	or the do_destroy will sort it.
	 */

	nb->state = NETBEUI_DEADWAIT;
	llc_disconnect_request(lp);
	return 0;
}



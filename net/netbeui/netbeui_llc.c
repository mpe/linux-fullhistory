/*
 *	Maintain 802.2 LLC logical channels being used by NetBEUI
 */



void netbeui_disc_indication(llcptr llc)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	if(nb->users>0)
		llc_connect_request(&nb->llc);
}

void netbeui_disc_confirm(llcptr llc)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	if(nb->users>0)
		llc_connect_request(&nb->llc);
	else
	{
		netbeui_destroy_channel(nb);
	}
}

void netbeui_connect_confirm(llcptr llc)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	nb->status=SS_CONNECTED;
	wakeup(&nb->wait_queue);
}

void netbeui_connect_indication(llcptr llc)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	nb->status=SS_CONNECTED;
	wakeup(&nb->wait_queue);
}

void netbeui_reset_confirm(llcptr llc)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	/* Good question .. */
}

void netbeui_reset_indication(llcptr llc, char lr)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	printk("netbeui: 802.2LLC reset.\n");
	/* Good question too */
}
	 
void netbeui_data_indication(llcptr llc, struct sk_buff *skb)
{
	netbeui_rcv_seq(LLC_TO_NB(llc),skb);
}

int netbeui_unit_data_indication(llcptr llc, struct sk_buff *skb)
{
	return netbeui_rcv_dgram(LLC_TO_NB(llc),skb);
}

int netbeui_xid_indication(llcptr llc, int ll, char *data)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	/* No action needed */
}

int netbeui_test_indication(llcptr llc, int ll, char *data)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	/* No action needed */
}

void netbeui_report_status(llcptr llc, char status)
{
	struct nb_link *nb=LLC_TO_NB(llc);
	switch(status)
	{
		case FRMR_RECEIVED:
		case FRMR_SENT:
			printk("netbeui: FRMR event %d\n",status);
			break;	/* FRMR's - shouldnt occur - debug log */
		case REMOTE_BUSY:
			nb->busy=1;
			break;
		case REMOTE_NOT_BUSY:
			nb->busy=0;
			wakeup(&nb->wakeup);
			break;
		default:
			printk("LLC passed netbeui bogus state %d\n",status);
			break;
	}
}

struct llc_ops netbeui_ops=
{
	netbeui_data_indication,		/* Sequenced frame */
	netbeui_unit_data_indication,		/* Datagrams */
	netbeui_connect_indication,		/* They called us */
	netbeui_connect_confirm,		/* We called them, they OK'd */
	netbeui_data_connect_indication,	/* Erm ?????? */
	netbeui_data_connect_confirm,		/* Erm ?????? */
	netbeui_disc_indication,		/* They closed */
	netbeui_disc_confirm,			/* We closed they OK'd */
	netbeui_reset_confirm,			/* Our reset worked */
	netbeui_reset_indication,		/* They reset on us */
	netbeui_xid_indication,			/* An XID frame */
	netbeui_test_indication,		/* A TEST frame */
	netbeui_report_status			/* Link state change */
};

/*
 *	Create a new outgoing session
 */
 
struct nb_link *netbeui_create_channel(struct device *dev, u8 *remote_mac, int pri)
{
	struct nb_link *nb=netbeui_find_channel(dev,remote_mac);
	if(nb)
	{
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
	init_timer(&nb->timer);
	nb->timer.function=netbeui_link_timer;
	nb->users=1;
	nb->busy=0;
	nb->wakeup=NULL;
	nb->status=SS_CONNECTING;
	memcpy(nb->remote_mac, remote_mac, ETH_ALEN);
	
	/*
	 *	Now try and attach an LLC.
	 */
	
	if(register_cl2llc_client(&nb->llc,dev->name,&nebeui_llcops,
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
	
int netbeui_delete_channel(struct nb_link *nb)
{
	nb->users--;
	if(nb->users)
		return 0;
		
	llc_disconnect_request(lp);
	/*
	 *	Ensure we drop soon. The disconnect confirm will let
	 *	us fix the deletion
	 */
	nb->state = SS_DISCONNECTING;
	nb->timer.expires=jiffies+NB_DROP_TIMEOUT;
	add_timer(&nb->timer);
	return 0;
}



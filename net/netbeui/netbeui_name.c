/*
 *	NetBIOS name handler
 */

/*
 *	You must hold the netbios name lock before using these.
 */
  
struct nb_name *nb_name_find(struct device *dev,const char * name)
{
	struct nb_name *nb=nb_name_list;
	while(nb!=NULL)
	{
		if((dev==NULL || dev==nb->dev) && 
			strncmp(name,nb->name, NB_NAME_LEN)==0)
			return nb;
		nb=nb->next;
	}
	return NULL;
}

int nb_name_add(struct device *dev, const char *name, int ours, int pri)
{
	struct nb_name *nb=kmalloc(sizeof(*nb), pri);
	if(nb==NULL)
		return NULL;
	nb->dev=dev;
	strncpy(nb->name,name,NB_NAME_LEN);
	nb->name[NB_NAME_LEN-1]=0;
	nb->next=nb_name_list;
	nb->ours=ours;
	nb_name_list=nb;
}

void nb_name_delete(struct nb_name *nb)
{
	struct nb_name *i=&nb_name_list;
	while((*i)!=NULL)
	{
		if(*i==nb)
		{
			*i=nb->next;
			kfree_s(nb,sizeof(*nb));
			return;
		}
		i=&((*i)->next);
	}
	printk(KERN_ERR "nb_name_delete: bad name pointer!\n");
}

/*
 *	NETBIOS name handlers
 */

static void nb_defend(struct device *dev, const char *name)
{
	struct sk_buff *nskb=nb_alloc_skb(NB_CONTROL_LEN, GFP_ATOMIC);
	if(nskb==NULL)
		return;
	/* Build a name defence packet */
	nskb->dev = dev;
	nskb->priority = TC_PRIO_CONTROL;
	dev_queue_xmit(nskb);
}

void netbeui_heard_name(struct device *dev, struct sk_buff *skb)
{
	struct nb_name *nb;
	name=...
	
	if((nb=nb_name_find(dev,name))!=NULL)
	{
		/*
		 *	If we own the name then defend it
		 */
		if(nb->our && !nb->state==NB_ACQUIRE)
			nb_defend(dev,name);
		/*
		 *	A name has been resolved. Wake up pending
		 *	connectors.
		 */
		if(nb->state==NB_QUERY)
		{
			nb->state=NB_OTHER;
			nb_complete(nb,skb);
		}
	}
	kfree_skb(skb);
	return 0;
}

/*
 *	Handle incoming name defences
 */
 
void netbeui_name_defence(struct dev *dev, struct sk_buff *skb)
{
	struct nb_name *name;
	name=
	
	if((nb=nb_name_find(dev,name))!=NULL)
	{
		if(nb->ours)
		{
			/*	
			 *	We wanted it, we got told its used
			 */
			if(nb->state==NB_ACQUIRE)
			{
				/*
				 *	Fill in the record for its true
				 *	owner. Set the state first as
				 *	nb_complete may well delete the
				 *	record.
				 */
				nb->state=NB_OTHER;
				nb_complete(nb,skb);
				nb_wakeup();
			}
			/*
			 *	We own it we got told its used. This is
			 *	a deep cack even that can only occur when
			 *	a bridge comes back and the net was split.
			 *	Make sure both sides lose.
			 */
			if(nb->state==NB_OURS || nb->state==NB_COLLIDE)
			{
				nb->state=NR_COLLIDE;
				nb_wakeup();
				/*
				 *	Kill the other copy too
				 */
				nb_defend(dev,name);	
				/*
				 *	Timer expiry will delete our
				 *	record.
				 */	
				nb_start_timer(nb, NB_TIME_COLLIDED);
			}		
		}
	}
	kfree_skb(skb);
}

void netbeui_name_query(struct dev *dev, struct sk_buff *skb)
{
	char *name=...
	struct nb_name *nb=nb_find_name(dev,name);
	
	if(nb!=NULL && nb->ours)
	{
		struct sk_buff *nskb=nb_alloc_skb(NB_CONTROL_LEN, GFP_ATOMIC);
		if(nskb!=NULL)
		{
			/* Build a name reply packet */
			nskb->dev = dev;
			nskb->priority = TC_PRIO_CONTROL;
			dev_queue_xmit(nskb);
		}
	}
	kfree_skb(skb);
}


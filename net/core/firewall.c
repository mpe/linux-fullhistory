/*
 *	Generic loadable firewalls. At the moment only IP will actually
 *	use these, but people can add the others as they are needed.
 *
 *	Authors:	Dave Bonn (for IP)
 *	much hacked by:	Alan Cox
 */
 
#include <linux/skbuff.h>
#include <linux/firewall.h>

static int firewall_lock=0;
static int firewall_policy[NPROTO];
static struct firewall_ops *firewall_chain[NPROTO];

/*
 *	Register a firewall
 */
 
int register_firewall(int pf, struct firewall_ops *fw)
{
	struct firewall_ops **p;
	
	if(pf<0||pf>=NPROTO)
		return -EINVAL;
	if(pf!=PF_INET)
		return -ENOPROTOOPT;
	
	/*
	 *	Don't allow two people to adjust at once.
	 */
	 
	while(firewall_lock)
		schedule();
	firewall_lock=1;
	
	p=&firewall_chain[pf];
	
	while(*p)
	{
		if(fw->fw_priority > (*p)->fw_priority)
			break;
		p=&((*p)->next);
	}

	fw->next=*p;
	/*
	 *	We need to set p atomically in case someone runs down the list
	 *	at the wrong moment. This saves locking it 
	 */
	 
	xchg(p,fw);

	/*
	 *	And release the sleep lock
	 */

	firewall_lock=0;
	return 0;
}

/*
 *	Unregister a firewall
 */

int unregister_firewall(int pf, struct firewall_ops *fw)
{
	struct firewall_ops **nl;
	
	if(pf<0||pf>=NPROTO)
		return -EINVAL;
	if(pf!=PF_INET)
		return -ENOPROTOOPT;
	
	/*
	 *	Don't allow two people to adjust at once.
	 */
	 
	while(firewall_lock)
		schedule();
	firewall_lock=1;

	nl=&firewall_chain[pf];
	
	while(*nl!=NULL)
	{
		if(*nl==fw)
		{
			struct firewall_ops *f=fw->next;
			xchg(nl,f);
			firewall_lock=0;
			return 0;
		}			
		nl=&((*nl)->next);
	}
	firewall_lock=0;
	return -ENOENT;
}

int call_fw_firewall(int pf, struct sk_buff *skb, void *phdr)
{
	struct firewall_ops *fw=firewall_chain[pf];
	int result=firewall_policy[pf];
	
	while(fw!=NULL)
	{
		int rc=fw->fw_forward(fw,pf,skb,phdr);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	/* alan, is this right? */
	return result;
}

/*
 *	Actual invocation of the chains
 */
 
int call_in_firewall(int pf, struct sk_buff *skb, void *phdr)
{
	struct firewall_ops *fw=firewall_chain[pf];
	int result=firewall_policy[pf];
	
	while(fw!=NULL)
	{
		int rc=fw->fw_input(fw,pf,skb,phdr);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	/* alan, is this right? */
	return result;
}

int call_out_firewall(int pf, struct sk_buff *skb, void *phdr)
{
	struct firewall_ops *fw=firewall_chain[pf];
	int result=firewall_policy[pf];
	
	while(fw!=NULL)
	{
		int rc=fw->fw_output(fw,pf,skb,phdr);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	/* alan, is this right? */
	return result;
}

void fwchain_init(void)
{
	int i;
	for(i=0;i<NPROTO;i++)
		firewall_policy[i]=FW_ACCEPT;
}

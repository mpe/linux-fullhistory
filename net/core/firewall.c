/*
 *	Generic loadable firewalls. At the moment only IP will actually
 *	use these, but people can add the others as they are needed.
 *
 *	Authors:	Dave Bonn (for IP)
 *	much hacked by:	Alan Cox
 */

#include <linux/module.h> 
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

	
	/*
	 * We need to use a memory barrier to make sure that this
	 * works correctly even in SMP with weakly ordered writes.
	 *
	 * This is atomic wrt interrupts (and generally walking the
	 * chain), but not wrt itself (so you can't call this from
	 * an interrupt. Not that you'd want to).
	 */
	fw->next=*p;
	mb();
	*p = fw;

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
			*nl = f;
			firewall_lock=0;
			return 0;
		}			
		nl=&((*nl)->next);
	}
	firewall_lock=0;
	return -ENOENT;
}

int call_fw_firewall(int pf, struct device *dev, void *phdr, void *arg)
{
	struct firewall_ops *fw=firewall_chain[pf];
	
	while(fw!=NULL)
	{
		int rc=fw->fw_forward(fw,pf,dev,phdr,arg);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	return firewall_policy[pf];
}

/*
 *	Actual invocation of the chains
 */
 
int call_in_firewall(int pf, struct device *dev, void *phdr, void *arg)
{
	struct firewall_ops *fw=firewall_chain[pf];
	
	while(fw!=NULL)
	{
		int rc=fw->fw_input(fw,pf,dev,phdr,arg);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	return firewall_policy[pf];
}

int call_out_firewall(int pf, struct device *dev, void *phdr, void *arg)
{
	struct firewall_ops *fw=firewall_chain[pf];
	
	while(fw!=NULL)
	{
		int rc=fw->fw_output(fw,pf,dev,phdr,arg);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	/* alan, is this right? */
	return firewall_policy[pf];
}

static struct symbol_table firewall_syms = {
#include <linux/symtab_begin.h>
	X(register_firewall),
	X(unregister_firewall),
	X(call_in_firewall),
	X(call_out_firewall),
	X(call_fw_firewall),
#include <linux/symtab_end.h>
};

void fwchain_init(void)
{
	int i;
	for(i=0;i<NPROTO;i++)
		firewall_policy[i]=FW_ACCEPT;
	register_symtab(&firewall_syms);
}

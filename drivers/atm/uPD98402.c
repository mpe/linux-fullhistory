/* drivers/atm/uPD98402.c - NEC uPD98402 (PHY) declarations */
 
/* Written 1995-1998 by Werner Almesberger, EPFL LRC/ICA */
 

#include <linux/module.h>
#include <linux/sched.h> /* for jiffies */
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/atmdev.h>
#include <linux/sonet.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#include "uPD98402.h"


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


struct uPD98402_priv {
	struct sonet_stats sonet_stats;	/* link diagnostics */
	unsigned char framing;		/* SONET/SDH framing */
};


#define PRIV(dev) ((struct uPD98402_priv *) dev->phy_data)

#define PUT(val,reg) dev->ops->phy_put(dev,val,uPD98402_##reg)
#define GET(reg) dev->ops->phy_get(dev,uPD98402_##reg)


static int fetch_stats(struct atm_dev *dev,struct sonet_stats *arg,int zero)
{
	unsigned long flags;
 	int error;

	error = 0;
	save_flags(flags);
	cli();
	PRIV(dev)->sonet_stats.uncorr_hcs += GET(HECCT);
	if (arg)
		error = copy_to_user(arg,&PRIV(dev)->sonet_stats,
		    sizeof(struct sonet_stats));
	if (zero && !error) {
		memset(&PRIV(dev)->sonet_stats,0,sizeof(struct sonet_stats));
		PRIV(dev)->sonet_stats.corr_hcs = -1;
		PRIV(dev)->sonet_stats.tx_cells = -1;
		PRIV(dev)->sonet_stats.rx_cells = -1;
	}
	restore_flags(flags);
	return error ? -EFAULT : 0;
}


static int set_framing(struct atm_dev *dev,unsigned char framing)
{
	static const unsigned char sonet[] = { 1,2,3,0 };
	static const unsigned char sdh[] = { 1,0,0,2 };
	const char *set;
	unsigned long flags;
 
	switch (framing) {
		case SONET_FRAME_SONET:
			set = sonet;
			break;
		case SONET_FRAME_SDH:
			set = sdh;
			break;
		default:
			return -EINVAL;
	}
	save_flags(flags);
	cli();
	PUT(set[0],C11T);
	PUT(set[1],C12T);
	PUT(set[2],C13T);
	PUT((GET(MDR) & ~uPD98402_MDR_SS_MASK) | (set[3] <<
	    uPD98402_MDR_SS_SHIFT),MDR);
	restore_flags(flags);
	return 0;
}


static int get_sense(struct atm_dev *dev,u8 *arg)
{
	unsigned long flags;
 	int error;

	save_flags(flags);
	cli();
	error = put_user(GET(C11R),arg) || put_user(GET(C12R),arg+1) ||
	    put_user(GET(C13R),arg+2);
	restore_flags(flags);
	error = error || put_user(0xff,arg+3) || put_user(0xff,arg+4) ||
	    put_user(0xff,arg+5);
	return error ? -EFAULT : 0;
}


static int uPD98402_ioctl(struct atm_dev *dev,unsigned int cmd,void *arg)
{
	switch (cmd) {

		case SONET_GETSTATZ:
                case SONET_GETSTAT:
			return fetch_stats(dev,(struct sonet_stats *) arg,
			    cmd == SONET_GETSTATZ);
		case SONET_SETFRAMING:
			return set_framing(dev,(int) arg);
		case SONET_GETFRAMING:
			return put_user(PRIV(dev)->framing,(int *) arg) ?
			    -EFAULT : 0;
		case SONET_GETFRSENSE:
			return get_sense(dev,arg);
		default:
		    return -EINVAL;
	}
}


static void stat_event(struct atm_dev *dev)
{
	unsigned char events;

	events = GET(PCR);
	if (events & uPD98402_PFM_PFEB)
		if ((PRIV(dev)->sonet_stats.path_febe += GET(PFECB)) < 0)
			PRIV(dev)->sonet_stats.path_febe = LONG_MAX;
	if (events & uPD98402_PFM_LFEB)
		if ((PRIV(dev)->sonet_stats.line_febe += GET(LECCT)) < 0)
			PRIV(dev)->sonet_stats.line_febe = LONG_MAX;
	if (events & uPD98402_PFM_B3E)
		if ((PRIV(dev)->sonet_stats.path_bip += GET(B3ECT)) < 0)
			PRIV(dev)->sonet_stats.path_bip = LONG_MAX;
	if (events & uPD98402_PFM_B2E)
		if ((PRIV(dev)->sonet_stats.line_bip += GET(B2ECT)) < 0)
			PRIV(dev)->sonet_stats.line_bip = LONG_MAX;
	if (events & uPD98402_PFM_B1E)
		if ((PRIV(dev)->sonet_stats.section_bip += GET(B1ECT)) < 0)
			PRIV(dev)->sonet_stats.section_bip = LONG_MAX;
}


static void uPD98402_int(struct atm_dev *dev)
{
	static unsigned long silence = 0;
	unsigned char reason;

	while ((reason = GET(PICR))) {
		if (reason & uPD98402_INT_LOS)
			printk(KERN_NOTICE "%s(itf %d): signal lost\n",
			    dev->type,dev->number);
		if (reason & uPD98402_INT_PFM) stat_event(dev);
		if (reason & uPD98402_INT_PCO) {
			(void) GET(PCOCR); /* clear interrupt cause */
			PRIV(dev)->sonet_stats.uncorr_hcs += GET(HECCT);
		}
		if ((reason & uPD98402_INT_RFO) && 
		    (time_after(jiffies, silence) || silence == 0)) {
			printk(KERN_WARNING "%s(itf %d): uPD98402 receive "
			    "FIFO overflow\n",dev->type,dev->number);
			silence = (jiffies+HZ/2)|1;
		}
	}
}


static int uPD98402_start(struct atm_dev *dev)
{
DPRINTK("phy_start\n");
	if (!(PRIV(dev) = kmalloc(sizeof(struct uPD98402_priv),GFP_KERNEL)))
		return -ENOMEM;
	memset(&PRIV(dev)->sonet_stats,0,sizeof(struct sonet_stats));
	(void) GET(PCR); /* clear performance events */
	PUT(uPD98402_PFM_FJ,PCMR); /* ignore frequency adj */
	(void) GET(PCOCR); /* clear overflows */
	PUT(~uPD98402_PCO_HECC,PCOMR);
	(void) GET(PICR); /* clear interrupts */
	PUT(~(uPD98402_INT_PFM | uPD98402_INT_ALM | uPD98402_INT_RFO |
	  uPD98402_INT_LOS),PIMR); /* enable them */
	(void) fetch_stats(dev,NULL,1); /* clear kernel counters */
	return 0;
}



static const struct atmphy_ops uPD98402_ops = {
	uPD98402_start,
	uPD98402_ioctl,	/* no ioctl yet */
	uPD98402_int
};


__initfunc(int uPD98402_init(struct atm_dev *dev))
{
DPRINTK("phy_init\n");
	dev->phy = &uPD98402_ops;
	return 0;
}


#ifdef MODULE

EXPORT_SYMBOL(uPD98402_init);

 
int init_module(void)
{
	MOD_INC_USE_COUNT;
	return 0;
}


void cleanup_module(void)
{
	/* Nay */
}
 
#endif

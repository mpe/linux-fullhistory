/* drivers/atm/suni.c - PMC SUNI (PHY) driver */
 
/* Written 1995-1998 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/atmdev.h>
#include <linux/sonet.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/atm_suni.h>
#include <asm/system.h>
#include <asm/param.h>
#include <asm/uaccess.h>

#include "suni.h"


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


struct suni_priv {
	struct sonet_stats sonet_stats; /* link diagnostics */
	unsigned char loop_mode;        /* loopback mode */
	struct atm_dev *dev;		/* device back-pointer */
	struct suni_priv *next;		/* next SUNI */
};


#define PRIV(dev) ((struct suni_priv *) dev->phy_data)

#define PUT(val,reg) dev->ops->phy_put(dev,val,SUNI_##reg)
#define GET(reg) dev->ops->phy_get(dev,SUNI_##reg)
#define REG_CHANGE(mask,shift,value,reg) \
  PUT((GET(reg) & ~(mask)) | ((value) << (shift)),reg)


static struct timer_list poll_timer = { NULL, NULL, 0L, 0L, NULL };
static int start_timer = 1;
static struct suni_priv *sunis = NULL;


static void suni_hz(unsigned long dummy)
{
	struct suni_priv *walk;
	struct atm_dev *dev;
	struct sonet_stats *stats;

	for (walk = sunis; walk; walk = walk->next) {
		dev = walk->dev;
		stats = &walk->sonet_stats;
		PUT(0,MRI); /* latch counters */
		udelay(1);
		stats->section_bip += (GET(RSOP_SBL) & 0xff) |
		    ((GET(RSOP_SBM) & 0xff) << 8);
		if (stats->section_bip < 0) stats->section_bip = LONG_MAX;
		stats->line_bip += (GET(RLOP_LBL) & 0xff) |
		    ((GET(RLOP_LB) & 0xff) << 8) |
		    ((GET(RLOP_LBM) & 0xf) << 16);
		if (stats->line_bip < 0) stats->line_bip = LONG_MAX;
		stats->path_bip += (GET(RPOP_PBL) & 0xff) |
		    ((GET(RPOP_PBM) & 0xff) << 8);
		if (stats->path_bip < 0) stats->path_bip = LONG_MAX;
		stats->line_febe += (GET(RLOP_LFL) & 0xff) |
		    ((GET(RLOP_LF) & 0xff) << 8) |
		    ((GET(RLOP_LFM) & 0xf) << 16);
		if (stats->line_febe < 0) stats->line_febe = LONG_MAX;
		stats->path_febe += (GET(RPOP_PFL) & 0xff) |
		    ((GET(RPOP_PFM) & 0xff) << 8);
		if (stats->path_febe < 0) stats->path_febe = LONG_MAX;
		stats->corr_hcs += GET(RACP_CHEC) & 0xff;
		if (stats->corr_hcs < 0) stats->corr_hcs = LONG_MAX;
		stats->uncorr_hcs += GET(RACP_UHEC) & 0xff;
		if (stats->uncorr_hcs < 0) stats->uncorr_hcs = LONG_MAX;
		stats->rx_cells += (GET(RACP_RCCL) & 0xff) |
		    ((GET(RACP_RCC) & 0xff) << 8) |
		    ((GET(RACP_RCCM) & 7) << 16);
		if (stats->rx_cells < 0) stats->rx_cells = LONG_MAX;
		stats->tx_cells += (GET(TACP_TCCL) & 0xff) |
		    ((GET(TACP_TCC) & 0xff) << 8) |
		    ((GET(TACP_TCCM) & 7) << 16);
		if (stats->tx_cells < 0) stats->tx_cells = LONG_MAX;
	}
	if (!start_timer) {
		del_timer(&poll_timer);
		poll_timer.expires = jiffies+HZ;
		add_timer(&poll_timer);
	}
}


static int fetch_stats(struct atm_dev *dev,struct sonet_stats *arg,int zero)
{
	unsigned long flags;
	int error;

	error = 0;
	save_flags(flags);
	cli();
	if (arg)
		error = copy_to_user(arg,&PRIV(dev)->sonet_stats,
		    sizeof(struct sonet_stats));
	if (zero && !error)
		memset(&PRIV(dev)->sonet_stats,0,sizeof(struct sonet_stats));
	restore_flags(flags);
	return error ? -EFAULT : 0;
}


#define HANDLE_FLAG(flag,reg,bit) \
  if (todo & flag) { \
    if (set) PUT(GET(reg) | bit,reg); \
    else PUT(GET(reg) & ~bit,reg); \
    todo &= ~flag; \
  }


static int change_diag(struct atm_dev *dev,void *arg,int set)
{
	int todo;

	if (get_user(todo,(int *) arg)) return -EFAULT;
	HANDLE_FLAG(SONET_INS_SBIP,TSOP_DIAG,SUNI_TSOP_DIAG_DBIP8);
	HANDLE_FLAG(SONET_INS_LBIP,TLOP_DIAG,SUNI_TLOP_DIAG_DBIP);
	HANDLE_FLAG(SONET_INS_PBIP,TPOP_CD,SUNI_TPOP_DIAG_DB3);
	HANDLE_FLAG(SONET_INS_FRAME,RSOP_CIE,SUNI_RSOP_CIE_FOOF);
	HANDLE_FLAG(SONET_INS_LAIS,TSOP_CTRL,SUNI_TSOP_CTRL_LAIS);
	HANDLE_FLAG(SONET_INS_PAIS,TPOP_CD,SUNI_TPOP_DIAG_PAIS);
	HANDLE_FLAG(SONET_INS_LOS,TSOP_DIAG,SUNI_TSOP_DIAG_DLOS);
	HANDLE_FLAG(SONET_INS_HCS,TACP_CS,SUNI_TACP_CS_DHCS);
	return put_user(todo,(int *) arg) ? -EFAULT : 0;
}


#undef HANDLE_FLAG


static int get_diag(struct atm_dev *dev,void *arg)
{
	int set;

	set = 0;
	if (GET(TSOP_DIAG) & SUNI_TSOP_DIAG_DBIP8) set |= SONET_INS_SBIP;
	if (GET(TLOP_DIAG) & SUNI_TLOP_DIAG_DBIP) set |= SONET_INS_LBIP;
	if (GET(TPOP_CD) & SUNI_TPOP_DIAG_DB3) set |= SONET_INS_PBIP;
	/* SONET_INS_FRAME is one-shot only */
	if (GET(TSOP_CTRL) & SUNI_TSOP_CTRL_LAIS) set |= SONET_INS_LAIS;
	if (GET(TPOP_CD) & SUNI_TPOP_DIAG_PAIS) set |= SONET_INS_PAIS;
	if (GET(TSOP_DIAG) & SUNI_TSOP_DIAG_DLOS) set |= SONET_INS_LOS;
	if (GET(TACP_CS) & SUNI_TACP_CS_DHCS) set |= SONET_INS_HCS;
	return put_user(set,(int *) arg) ? -EFAULT : 0;
}


static int suni_ioctl(struct atm_dev *dev,unsigned int cmd,void *arg)
{
	switch (cmd) {
		case SONET_GETSTATZ:
		case SONET_GETSTAT:
			return fetch_stats(dev,(struct sonet_stats *) arg,
			    cmd == SONET_GETSTATZ);
		case SONET_SETDIAG:
			return change_diag(dev,arg,1);
		case SONET_CLRDIAG:
			return change_diag(dev,arg,0);
		case SONET_GETDIAG:
			return get_diag(dev,arg);
		case SONET_SETFRAMING:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			if (arg != SONET_FRAME_SONET) return -EINVAL;
			return 0;
		case SONET_GETFRAMING:
			return put_user(SONET_FRAME_SONET,(int *) arg) ?
			    -EFAULT : 0;
		case SONET_GETFRSENSE:
			return -EINVAL;
		case SUNI_SETLOOP:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			if ((int) arg < 0 || (int) arg > SUNI_LM_LOOP)
				return -EINVAL;
			PUT((GET(MCT) & ~(SUNI_MCT_DLE | SUNI_MCT_LLE)) |
			    ((int) arg == SUNI_LM_DIAG ? SUNI_MCT_DLE : 0) |
			    ((int) arg == SUNI_LM_LOOP ? SUNI_MCT_LLE : 0),MCT);
			PRIV(dev)->loop_mode = (int) arg;
			return 0;
		case SUNI_GETLOOP:
			return put_user(PRIV(dev)->loop_mode,(int *) arg) ?
			    -EFAULT : 0;
		default:
			return -EINVAL;
	}
}


static void poll_los(struct atm_dev *dev)
{
	dev->signal = GET(RSOP_SIS) & SUNI_RSOP_SIS_LOSV ? ATM_PHY_SIG_LOST :
	  ATM_PHY_SIG_FOUND;
}


static void suni_int(struct atm_dev *dev)
{
	poll_los(dev);
	printk(KERN_NOTICE "%s(itf %d): signal %s\n",dev->type,dev->number,
	    dev->signal == ATM_PHY_SIG_LOST ?  "lost" : "detected again");
}


static int suni_start(struct atm_dev *dev)
{
	unsigned long flags;

	if (!(PRIV(dev) = kmalloc(sizeof(struct suni_priv),GFP_KERNEL)))
		return -ENOMEM;
	PRIV(dev)->dev = dev;
	save_flags(flags);
	cli();
	PRIV(dev)->next = sunis;
	sunis = PRIV(dev);
	restore_flags(flags);
	memset(&PRIV(dev)->sonet_stats,0,sizeof(struct sonet_stats));
	PUT(GET(RSOP_CIE) | SUNI_RSOP_CIE_LOSE,RSOP_CIE);
		/* interrupt on loss of signal */
	poll_los(dev); /* ... and clear SUNI interrupts */
	if (dev->signal == ATM_PHY_SIG_LOST)
		printk(KERN_WARNING "%s(itf %d): no signal\n",dev->type,
		    dev->number);
	PRIV(dev)->loop_mode = SUNI_LM_NONE;
	suni_hz(0); /* clear SUNI counters */
	(void) fetch_stats(dev,NULL,1); /* clear kernel counters */
	cli();
	if (!start_timer) restore_flags(flags);
	else {
		start_timer = 0;
		restore_flags(flags);
		/*init_timer(&poll_timer);*/
		poll_timer.expires = jiffies+HZ;
		poll_timer.function = suni_hz;
#if 0
printk(KERN_DEBUG "[u] p=0x%lx,n=0x%lx\n",(unsigned long) poll_timer.prev,
    (unsigned long) poll_timer.next);
#endif
		add_timer(&poll_timer);
	}
	return 0;
}


static const struct atmphy_ops suni_ops = {
	suni_start,
	suni_ioctl,
	suni_int
};


__initfunc(int suni_init(struct atm_dev *dev))
{
	unsigned char mri;

	mri = GET(MRI); /* reset SUNI */
	PUT(mri | SUNI_MRI_RESET,MRI);
	PUT(mri,MRI);
	PUT(0,MT); /* disable all tests */
	REG_CHANGE(SUNI_TPOP_APM_S,SUNI_TPOP_APM_S_SHIFT,SUNI_TPOP_S_SONET,
	    TPOP_APM); /* use SONET */
	REG_CHANGE(SUNI_TACP_IUCHP_CLP,0,SUNI_TACP_IUCHP_CLP,
	    TACP_IUCHP); /* idle cells */
	PUT(SUNI_IDLE_PATTERN,TACP_IUCPOP);
	dev->phy = &suni_ops;
	return 0;
}


#ifdef MODULE

EXPORT_SYMBOL(suni_init);


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

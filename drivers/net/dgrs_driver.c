/*
 *	Digi RightSwitch SE-X loadable device driver for Linux
 *
 *	The RightSwitch is a 4 (EISA) or 6 (PCI) port etherswitch and
 *	a NIC on an internal board.
 *
 *	Author: Rick Richardson, rick@dgii.com, rick_richardson@dgii.com
 *	Derived from the SVR4.2 (UnixWare) driver for the same card.
 *
 *	Copyright 1995-1996 Digi International Inc.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 *
 *	For information on purchasing a RightSwitch SE-4 or SE-6
 *	board, please contact Digi's sales department at 1-612-912-3444
 *	or 1-800-DIGIBRD.  Outside the U.S., please check our Web page
 *	at http://www.dgii.com for sales offices worldwide.
 *
 */

static char *version = "$Id: dgrs.c,v 1.8 1996/04/18 03:11:14 rick Exp $";

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/types.h>

/*
 *	DGRS include files
 */
typedef unsigned char uchar;
typedef unsigned int bool;
#define vol volatile

#include "dgrs.h"
#include "dgrs_es4h.h"
#include "dgrs_plx9060.h"
#include "dgrs_i82596.h"
#include "dgrs_ether.h"
#include "dgrs_asstruct.h"
#include "dgrs_bcomm.h"

/*
 *	Firmware.  Compiled separately for local compilation,
 *	but #included for Linux distribution.
 */
#ifndef NOFW
	#include "dgrs_firmware.c"
#else
	extern int	dgrs_firmnum;
	extern char	dgrs_firmver[];
	extern char	dgrs_firmdate[];
	extern uchar	dgrs_code[];
	extern int	dgrs_ncode;
#endif

/*
 *	Linux out*() is backwards from all other operating systems
 */
#define	OUTB(ADDR, VAL)	outb(VAL, ADDR)
#define	OUTW(ADDR, VAL)	outw(VAL, ADDR)
#define	OUTL(ADDR, VAL)	outl(VAL, ADDR)

/*
 *	Macros to convert switch to host and host to switch addresses
 *	(assumes a local variable priv points to board dependent struct)
 */
#define	S2H(A)	( ((unsigned long)(A)&0x00ffffff) + priv->vmem )
#define	H2S(A)	( ((char *) (A) - priv->vmem) + 0xA3000000 )

/*
 *	Convert a switch address to a "safe" address for use with the
 *	PLX 9060 DMA registers and the associated HW kludge that allows
 *	for host access of the DMA registers.
 */
#define	S2DMA(A)	( (unsigned long)(A) & 0x00ffffff)

/*
 *	"Space.c" variables, now settable from module interface
 *	Use the name below, minus the "dgrs_" prefix.  See init_module().
 */
int	dgrs_debug = 1;
int	dgrs_dma = 1;
int	dgrs_spantree = -1;
int	dgrs_hashexpire = -1;
uchar	dgrs_ipaddr[4] = { 0xff, 0xff, 0xff, 0xff};
long	dgrs_ipxnet = -1;

/*
 *	Chain of device structures
 */
#ifdef MODULE
	static struct device *dgrs_root_dev = NULL;
#endif

/*
 *	Private per-board data structure (dev->priv)
 */
typedef struct
{
	/*
	 *	Stuff for generic ethercard I/F
	 */
	char			devname[8];	/* "ethN" string */
	struct device		*next_dev;
	struct enet_statistics	stats;

	/*
	 *	DGRS specific data
	 */
	char		*vmem;

        struct bios_comm *bcomm;        /* Firmware BIOS comm structure */
        PORT            *port;          /* Ptr to PORT[0] struct in VM */
        I596_SCB        *scbp;          /* Ptr to SCB struct in VM */
        I596_RFD        *rfdp;          /* Current RFD list */
        I596_RBD        *rbdp;          /* Current RBD list */

        int             intrcnt;        /* Count of interrupts */

        /*
         *      SE-4 (EISA) board variables
         */
        uchar		is_reg;		/* EISA: Value for ES4H_IS reg */

        /*
         *      SE-6 (PCI) board variables
         *
         *      The PLX "expansion rom" space is used for DMA register
         *      access from the host on the SE-6.  These are the physical
         *      and virtual addresses of that space.
         */
        ulong		plxreg;		/* Phys address of PLX chip */
        char            *vplxreg;	/* Virtual address of PLX chip */
        ulong		plxdma;		/* Phys addr of PLX "expansion rom" */
        ulong volatile  *vplxdma;	/* Virtual addr of "expansion rom" */
        int             use_dma;        /* Flag: use DMA */
	DMACHAIN	*dmadesc_s;	/* area for DMA chains (SW addr.) */
	DMACHAIN	*dmadesc_h;	/* area for DMA chains (Host Virtual) */

} DGRS_PRIV;


/*
 *	reset or un-reset the IDT processor
 */
static void
proc_reset(struct device *dev, int reset)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;

	if (priv->plxreg)
	{
		ulong		val;
		val = inl(dev->base_addr + PLX_MISC_CSR);
		if (reset)
			val |= SE6_RESET;
		else
			val &= ~SE6_RESET;
		OUTL(dev->base_addr + PLX_MISC_CSR, val);
	}
	else
	{
		OUTB(dev->base_addr + ES4H_PC, reset ? ES4H_PC_RESET : 0);
	}
}

/*
 *	See if the board supports bus master DMA
 */
static int
check_board_dma(struct device *dev)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;
	ulong	x;

	/*
	 *	If Space.c says not to use DMA, or if it's not a PLX based
	 *	PCI board, or if the expansion ROM space is not PCI
	 *	configured, then return false.
	 */
	if (!dgrs_dma || !priv->plxreg || !priv->plxdma)
		return (0);

	/*
	 *	Set the local address remap register of the "expansion rom"
	 *	area to 0x80000000 so that we can use it to access the DMA
	 *	registers from the host side.
	 */
	OUTL(dev->base_addr + PLX_ROM_BASE_ADDR, 0x80000000);

	/*
	 * Set the PCI region descriptor to:
	 *      Space 0:
	 *              disable read-prefetch
	 *              enable READY
	 *              enable BURST
	 *              0 internal wait states
	 *      Expansion ROM: (used for host DMA register access)
	 *              disable read-prefetch
	 *              enable READY
	 *              disable BURST
	 *              0 internal wait states
	 */
	OUTL(dev->base_addr + PLX_BUS_REGION, 0x49430343);

	/*
	 *	Now map the DMA registers into our virtual space
	 */
	priv->vplxdma = (ulong *) vremap (priv->plxdma, 256);
	if (!priv->vplxdma)
	{
		printk("%s: can't vremap() the DMA regs", dev->name);
		return (0);
	}

	/*
	 *	Now test to see if we can access the DMA registers
	 *	If we write -1 and get back 1FFF, then we accessed the
	 *	DMA register.  Otherwise, we probably have an old board
	 *	and wrote into regular RAM.
	 */
	priv->vplxdma[PLX_DMA0_MODE/4] = 0xFFFFFFFF;
	x = priv->vplxdma[PLX_DMA0_MODE/4];
	if (x != 0x00001FFF)
		return (0);

	return (1);
}

/*
 *	Initiate DMA using PLX part on PCI board.  Spin the
 *	processor until completed.  All addresses are physical!
 *
 *	If pciaddr is NULL, then it's a chaining DMA, and lcladdr is
 *	the address of the first DMA descriptor in the chain.
 *
 *	If pciaddr is not NULL, then it's a single DMA.
 *
 *	In either case, "lcladdr" must have been fixed up to make
 *	sure the MSB isn't set using the S2DMA macro before passing
 *	the address to this routine.
 */
static int
do_plx_dma(
	struct device *dev,
	ulong pciaddr,
	ulong lcladdr,
	int len,
	int to_host
)
{
        int     	i;
        ulong   	csr;
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;

	if (pciaddr)
	{
		/*
		 *	Do a single, non-chain DMA
		 */
		priv->vplxdma[PLX_DMA0_PCI_ADDR/4] = pciaddr;
		priv->vplxdma[PLX_DMA0_LCL_ADDR/4] = lcladdr;
		priv->vplxdma[PLX_DMA0_SIZE/4] = len;
		priv->vplxdma[PLX_DMA0_DESCRIPTOR/4] = to_host
					? PLX_DMA_DESC_TO_HOST
					: PLX_DMA_DESC_TO_BOARD;
		priv->vplxdma[PLX_DMA0_MODE/4] =
					  PLX_DMA_MODE_WIDTH32
					| PLX_DMA_MODE_WAITSTATES(0)
					| PLX_DMA_MODE_READY
					| PLX_DMA_MODE_NOBTERM
					| PLX_DMA_MODE_BURST
					| PLX_DMA_MODE_NOCHAIN;
	}
	else
	{
		/*
		 *	Do a chaining DMA
		 */
		priv->vplxdma[PLX_DMA0_MODE/4] =
					  PLX_DMA_MODE_WIDTH32
					| PLX_DMA_MODE_WAITSTATES(0)
					| PLX_DMA_MODE_READY
					| PLX_DMA_MODE_NOBTERM
					| PLX_DMA_MODE_BURST
					| PLX_DMA_MODE_CHAIN;
		priv->vplxdma[PLX_DMA0_DESCRIPTOR/4] = lcladdr;
	}

	priv->vplxdma[PLX_DMA_CSR/4] =
				PLX_DMA_CSR_0_ENABLE | PLX_DMA_CSR_0_START;

        /*
	 *	Wait for DMA to complete
	 */
        for (i = 0; i < 1000000; ++i)
        {
		/*
		 *	Spin the host CPU for 1 usec, so we don't thrash
		 *	the PCI bus while the PLX 9060 is doing DMA.
		 */
		udelay(1);

		csr = (volatile) priv->vplxdma[PLX_DMA_CSR/4];

                if (csr & PLX_DMA_CSR_0_DONE)
                        break;
        }

        if ( ! (csr & PLX_DMA_CSR_0_DONE) )
        {
		printk("%s: DMA done never occurred. DMA disabled.", dev->name);
		priv->use_dma = 0;
                return 1;
        }
        return 0;
}

/*
 *	Process a received frame
 */
void
dgrs_rcv_frame(
	struct device	*dev,
	DGRS_PRIV	*priv,
	I596_CB		*cbp
)
{
	int		len;
	I596_TBD	*tbdp;
	struct sk_buff	*skb;
	uchar		*putp;
	uchar		*p;

	if (0) printk("%s: rcv len=%ld", dev->name, cbp->xmit.count);

	/*
	 *	Allocate a message block big enough to hold the whole frame
	 */
	len = cbp->xmit.count;
	if ((skb = dev_alloc_skb(len+5)) == NULL)
	{
		printk("%s: dev_alloc_skb failed for rcv buffer", dev->name);
		++priv->stats.rx_dropped;
		/* discarding the frame */
		goto out;
	}
	skb->dev = dev;
	skb_reserve(skb, 2);	/* Align IP header */

again:
	putp = p = skb_put(skb, len);

	/*
	 *	There are three modes here for doing the packet copy.
	 *	If we have DMA, and the packet is "long", we use the
	 *	chaining mode of DMA.  If it's shorter, we use single
	 *	DMA's.  Otherwise, we use memcpy().
	 */
	if (priv->use_dma && priv->dmadesc_h && len > 64)
	{
		/*
		 *	If we can use DMA and it's a long frame, copy it using
		 *	DMA chaining.
		 */
		DMACHAIN	*ddp_h;	/* Host virtual DMA desc. pointer */
		DMACHAIN	*ddp_s;	/* Switch physical DMA desc. pointer */
		uchar		*phys_p;

		/*
		 *	Get the physical address of the STREAMS buffer.
		 *	NOTE: allocb() guarantees that the whole buffer
		 *	is in a single page if the length < 4096.
		 */
		phys_p = (uchar *) virt_to_phys(putp);

		ddp_h = priv->dmadesc_h;
		ddp_s = priv->dmadesc_s;
		tbdp = (I596_TBD *) S2H(cbp->xmit.tbdp);
		for (;;)
		{
			int	count;
			int	amt;

			count = tbdp->count;
			amt = count & 0x3fff;
			if (amt == 0)
				break; /* For safety */
			if ( (p-putp) >= len)
			{
				printk("%s: cbp = %x", dev->name, H2S(cbp));
				proc_reset(dev, 1);	/* Freeze IDT */
				break; /* For Safety */
			}

			ddp_h->pciaddr = (ulong) phys_p;
			ddp_h->lcladdr = S2DMA(tbdp->buf);
			ddp_h->len = amt;

			phys_p += amt;
			p += amt;

			if (count & I596_TBD_EOF)
			{
				ddp_h->next = PLX_DMA_DESC_TO_HOST
						| PLX_DMA_DESC_EOC;
				++ddp_h;
				break;
			}
			else
			{
				++ddp_s;
				ddp_h->next = PLX_DMA_DESC_TO_HOST
						| (ulong) ddp_s;
				tbdp = (I596_TBD *) S2H(tbdp->next);
				++ddp_h;
			}
		}
		if (ddp_h - priv->dmadesc_h)
		{
			int	rc;

			rc = do_plx_dma(dev,
				0, (ulong) priv->dmadesc_s, len, 0);
			if (rc)
			{
				printk("%s: Chained DMA failure\n", dev->name);
				goto again;
			}
		}
	}
	else if (priv->use_dma)
	{
		/*
		 *	If we can use DMA and it's a shorter frame, copy it
		 *	using single DMA transfers.
		 */
		uchar		*phys_p;

		/*
		 *	Get the physical address of the STREAMS buffer.
		 *	NOTE: allocb() guarantees that the whole buffer
		 *	is in a single page if the length < 4096.
		 */
		phys_p = (uchar *) virt_to_phys(putp);

		tbdp = (I596_TBD *) S2H(cbp->xmit.tbdp);
		for (;;)
		{
			int	count;
			int	amt;
			int	rc;

			count = tbdp->count;
			amt = count & 0x3fff;
			if (amt == 0)
				break; /* For safety */
			if ( (p-putp) >= len)
			{
				printk("%s: cbp = %x", dev->name, H2S(cbp));
				proc_reset(dev, 1);	/* Freeze IDT */
				break; /* For Safety */
			}
			rc = do_plx_dma(dev, (ulong) phys_p,
						S2DMA(tbdp->buf), amt, 1);
			if (rc)
			{
				memcpy(p, S2H(tbdp->buf), amt);
				printk("%s: Single DMA failed\n", dev->name);
			}
			phys_p += amt;
			p += amt;
			if (count & I596_TBD_EOF)
				break;
			tbdp = (I596_TBD *) S2H(tbdp->next);
		}
	}
	else
	{
		/*
		 *	Otherwise, copy it piece by piece using memcpy()
		 */
		tbdp = (I596_TBD *) S2H(cbp->xmit.tbdp);
		for (;;)
		{
			int	count;
			int	amt;

			count = tbdp->count;
			amt = count & 0x3fff;
			if (amt == 0)
				break; /* For safety */
			if ( (p-putp) >= len)
			{
				printk("%s: cbp = %x", dev->name, H2S(cbp));
				proc_reset(dev, 1);	/* Freeze IDT */
				break; /* For Safety */
			}
			memcpy(p, S2H(tbdp->buf), amt);
			p += amt;
			if (count & I596_TBD_EOF)
				break;
			tbdp = (I596_TBD *) S2H(tbdp->next);
		}
	}

	/*
	 *	Pass the frame to upper half
	 */
	skb->protocol = eth_type_trans(skb, dev);
	netif_rx(skb);
	++priv->stats.rx_packets;

out:
	cbp->xmit.status = I596_CB_STATUS_C | I596_CB_STATUS_OK;
}

/*
 *	Start transmission of a frame
 *
 *	The interface to the board is simple: we pretend that we are
 *	a fifth 82596 ethernet controller 'receiving' data, and copy the
 *	data into the same structures that a real 82596 would.  This way,
 *	the board firmware handles the host 'port' the same as any other.
 *
 *	NOTE: we do not use Bus master DMA for this routine.  Turns out
 *	that it is not needed.  Slave writes over the PCI bus are about
 *	as fast as DMA, due to the fact that the PLX part can do burst
 *	writes.  The same is not true for data being read from the board.
 */
static int
dgrs_start_xmit(struct sk_buff *skb, struct device *dev)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;
	I596_RBD	*rbdp;
	int		count;
	int		i, len, amt;
#	define		mymin(A,B)	( (A) < (B) ? (A) : (B) )

	if (dgrs_debug > 1) printk("%s: xmit len=%ld\n", dev->name, skb->len);

	dev->trans_start = jiffies;
	dev->tbusy = 0;

	if (priv->rfdp->cmd & I596_RFD_EL)
	{	/* Out of RFD's */
		if (0) printk("%s: NO RFD's", dev->name);
		goto no_resources;
	}

	rbdp = priv->rbdp;
	count = 0;
	priv->rfdp->rbdp = (I596_RBD *) H2S(rbdp);

	i = 0; len = skb->len;
	for (;;)
	{
		if (rbdp->size & I596_RBD_EL)
		{	/* Out of RBD's */
			if (0) printk("%s: NO RBD's", dev->name);
			goto no_resources;
		}

		amt = mymin(len, rbdp->size - count);
		memcpy( (char *) S2H(rbdp->buf) + count, skb->data + i, amt);
		i += amt;
		count += amt;
		len -= amt;
		if (len == 0)
		{
			if (skb->len < 60)
				rbdp->count = 60 | I596_RBD_EOF;
			else
				rbdp->count = count | I596_RBD_EOF;
			rbdp = (I596_RBD *) S2H(rbdp->next);
			goto frame_done;
		}
		else if (count < 32)
		{
			/* More data to come, but we used less than 32
			 * bytes of this RBD.  Keep filling this RBD.
			 */
			{}	/* Yes, we do nothing here */
		}
		else
		{
			rbdp->count = count;
			rbdp = (I596_RBD *) S2H(rbdp->next);
			count = 0;
		}
	}

frame_done:
	priv->rbdp = rbdp;
	priv->rfdp->status = I596_RFD_C | I596_RFD_OK;
	priv->rfdp = (I596_RFD *) S2H(priv->rfdp->next);

	++priv->stats.tx_packets;

	dev_kfree_skb (skb, FREE_WRITE);
	return (0);

no_resources:
	priv->scbp->status |= I596_SCB_RNR;	/* simulate I82596 */
	return (-EAGAIN);
}

/*
 *	Open the interface
 */
static int
dgrs_open( struct device *dev )
{
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	#ifdef MODULE
		MOD_INC_USE_COUNT;
	#endif

	return (0);
}

/*
 *	Close the interface
 */
static int
dgrs_close( struct device *dev )
{
	dev->start = 0;
	dev->tbusy = 1;

	#ifdef MODULE
		MOD_DEC_USE_COUNT;
	#endif

	return (0);
}

/*
 *	Get statistics
 */
static struct enet_statistics *
dgrs_get_stats( struct device *dev )
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;

	return (&priv->stats);
}

/*
 *	Set multicast list and/or promiscuous mode
 */
static void
dgrs_set_multicast_list( struct device *dev)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;

	priv->port->is_promisc = (dev->flags & IFF_PROMISC) ? 1 : 0;
}

/*
 *	Unique ioctl's
 */
static int
dgrs_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;
	DGRS_IOCTL	ioc;
	int		i, rc;

	rc = verify_area(VERIFY_WRITE, ifr->ifr_data, sizeof(DGRS_IOCTL));
	if (rc) return (rc);
	if (cmd != DGRSIOCTL) return -EINVAL;

	memcpy_fromfs(&ioc, ifr->ifr_data, sizeof(DGRS_IOCTL));

	switch (ioc.cmd)
	{
	case DGRS_GETMEM:
		if (ioc.len != sizeof(ulong))
			return -EINVAL;
		rc = verify_area(VERIFY_WRITE, (void *) ioc.data, ioc.len);
		if (rc) return (rc);
		memcpy_tofs(ioc.data, &dev->mem_start, ioc.len);
		return (0);
	case DGRS_SETFILTER:
		rc = verify_area(VERIFY_READ, (void *) ioc.data, ioc.len);
		if (rc) return (rc);
		if (ioc.port > priv->bcomm->bc_nports)
			return -EINVAL;
		if (ioc.filter >= NFILTERS)
			return -EINVAL;
		if (ioc.len > priv->bcomm->bc_filter_area_len)
			return -EINVAL;

		/* Wait for old command to finish */
		for (i = 0; i < 1000; ++i)
		{
			if ( (volatile) priv->bcomm->bc_filter_cmd <= 0 )
				break;
			udelay(1);
		}
		if (i >= 1000)
			return -EIO;

		priv->bcomm->bc_filter_port = ioc.port;
		priv->bcomm->bc_filter_num = ioc.filter;
		priv->bcomm->bc_filter_len = ioc.len;
		
		if (ioc.len)
		{
			memcpy_fromfs(S2H(priv->bcomm->bc_filter_area),
					ioc.data, ioc.len);
			priv->bcomm->bc_filter_cmd = BC_FILTER_SET;
		}
		else
			priv->bcomm->bc_filter_cmd = BC_FILTER_CLR;
		return(0);
	default:
		return -EOPNOTSUPP;
	}
}

/*
 *	Process interrupts
 */
static void
dgrs_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device	*dev = (struct device *) dev_id;
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;
	I596_CB		*cbp;
	int		cmd;
	
	++priv->intrcnt;
	if (1) ++priv->bcomm->bc_cnt[4];
	if (0) printk("%s: interrupt: irq %d", dev->name, irq);

	/*
	 *	Get 596 command
	 */
	cmd = priv->scbp->cmd;

	/*
	 *	See if RU has been restarted
	 */
	if ( (cmd & I596_SCB_RUC) == I596_SCB_RUC_START)
	{
		if (0) printk("%s: RUC start", dev->name);
		priv->rfdp = (I596_RFD *) S2H(priv->scbp->rfdp);
		priv->rbdp = (I596_RBD *) S2H(priv->rfdp->rbdp);
		dev->tbusy = 0;	/* tell upper half */
		priv->scbp->status &= ~(I596_SCB_RNR|I596_SCB_RUS);
		/* if (bd->flags & TX_QUEUED)
			DL_sched(bd, bdd); */
	}

	/*
	 *	See if any CU commands to process
	 */
	if ( (cmd & I596_SCB_CUC) != I596_SCB_CUC_START)
	{
		priv->scbp->cmd = 0;	/* Ignore all other commands */
		goto ack_intr;
	}
	priv->scbp->status &= ~(I596_SCB_CNA|I596_SCB_CUS);

	/*
	 *	Process a command
	 */
	cbp = (I596_CB *) S2H(priv->scbp->cbp);
	priv->scbp->cmd = 0;	/* Safe to clear the command */
	for (;;)
	{
		switch (cbp->nop.cmd & I596_CB_CMD)
		{
		case I596_CB_CMD_XMIT:
			dgrs_rcv_frame(dev, priv, cbp);
			break;
		default:
			cbp->nop.status = I596_CB_STATUS_C | I596_CB_STATUS_OK;
			break;
		}
		if (cbp->nop.cmd & I596_CB_CMD_EL)
			break;
		cbp = (I596_CB *) S2H(cbp->nop.next);
	}
	priv->scbp->status |= I596_SCB_CNA;

	/*
	 * Ack the interrupt
	 */
ack_intr:
	if (priv->plxreg)
		OUTL(dev->base_addr + PLX_LCL2PCI_DOORBELL, 1);
}

/*
 *	Download the board firmware
 */
static int
dgrs_download(struct device *dev)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;
	int		is;
	int		i;

	static int	iv2is[16] = {
				0, 0, 0, ES4H_IS_INT3,
				0, ES4H_IS_INT5, 0, ES4H_IS_INT7,
				0, 0, ES4H_IS_INT10, ES4H_IS_INT11,
				ES4H_IS_INT12, 0, 0, ES4H_IS_INT15 };

	/*
	 * Map in the dual port memory
	 */
	priv->vmem = vremap(dev->mem_start, 2048*1024);
	if (!priv->vmem)
	{
		printk("%s: cannot map in board memory\n", dev->name);
		return -ENXIO;
	}

	/*
	 *	Hold the processor and configure the board addresses
	 */
	if (priv->plxreg)
	{	/* PCI bus */
		proc_reset(dev, 1);
	}
	else
	{	/* EISA bus */
		is = iv2is[dev->irq & 0x0f];
		if (!is)
		{
			printk("%s: Illegal IRQ %d\n", dev->name, dev->irq);
			return -ENXIO;
		}
		OUTB(dev->base_addr + ES4H_AS_31_24,
			(uchar) (dev->mem_start >> 24) );
		OUTB(dev->base_addr + ES4H_AS_23_16,
			(uchar) (dev->mem_start >> 16) );
		priv->is_reg = ES4H_IS_LINEAR | is |
			((uchar) (dev->mem_start >> 8) & ES4H_IS_AS15);
		OUTB(dev->base_addr + ES4H_IS, priv->is_reg);
		OUTB(dev->base_addr + ES4H_EC, ES4H_EC_ENABLE);
		OUTB(dev->base_addr + ES4H_PC, ES4H_PC_RESET);
		OUTB(dev->base_addr + ES4H_MW, ES4H_MW_ENABLE | 0x00);
	}

	/*
	 *	See if we can do DMA on the SE-6
	 */
	priv->use_dma = check_board_dma(dev);
	if (priv->use_dma)
		printk("%s: Bus Master DMA is enabled.\n", dev->name);

	/*
	 * Load and verify the code at the desired address
	 */
	memcpy(priv->vmem, dgrs_code, dgrs_ncode);	/* Load code */
	if (memcmp(priv->vmem, dgrs_code, dgrs_ncode))
	{
		vfree(priv->vmem);
		priv->vmem = NULL;
		printk("%s: download compare failed\n", dev->name);
		return -ENXIO;
	}

	/*
	 * Configurables
	 */
	priv->bcomm = (struct bios_comm *) (priv->vmem + 0x0100);
	priv->bcomm->bc_nowait = 1;	/* Tell board to make printf not wait */
	priv->bcomm->bc_host = 1;	/* Tell board there is a host port */
	priv->bcomm->bc_squelch = 0;	/* Flag from Space.c */
	priv->bcomm->bc_150ohm = 0;	/* Flag from Space.c */

	priv->bcomm->bc_spew = 0;	/* Debug flag from Space.c */
	priv->bcomm->bc_maxrfd = 0;	/* Debug flag from Space.c */
	priv->bcomm->bc_maxrbd = 0;	/* Debug flag from Space.c */

	/*
	 * Request memory space on board for DMA chains
	 */
	if (priv->use_dma)
		priv->bcomm->bc_hostarea_len = (2048/64) * 16;

	/*
	 * NVRAM configurables from Space.c
	 */
	priv->bcomm->bc_spantree = dgrs_spantree;
	priv->bcomm->bc_hashexpire = dgrs_hashexpire;
	memcpy(priv->bcomm->bc_ipaddr, dgrs_ipaddr, 4);
	memcpy(priv->bcomm->bc_ipxnet, &dgrs_ipxnet, 4);

	/*
	 * Release processor, wait 5 seconds for board to initialize
	 */
	proc_reset(dev, 0);

	for (i = jiffies + 5 * HZ; i > jiffies; )
	{
		if (priv->bcomm->bc_status >= BC_RUN)
			break;
	}

	if (priv->bcomm->bc_status < BC_RUN)
	{
		printk("%s: board not operating", dev->name);
		return -ENXIO;
	}

	priv->port = (PORT *) S2H(priv->bcomm->bc_port);
	priv->scbp = (I596_SCB *) S2H(priv->port->scbp);
	#if 0	/* These two methods are identical, but the 2nd is better */
		priv->rfdp = (I596_RFD *) S2H(priv->port->rfd_head);
		priv->rbdp = (I596_RBD *) S2H(priv->port->rbd_head);
	#else
		priv->rfdp = (I596_RFD *) S2H(priv->scbp->rfdp);
		priv->rbdp = (I596_RBD *) S2H(priv->rfdp->rbdp);
	#endif

	priv->scbp->status = I596_SCB_CNA;	/* CU is idle */

	/*
	 *	Get switch physical and host virtual pointers to DMA
	 *	chaining area.  NOTE: the MSB of the switch physical
	 *	address *must* be turned off.  Otherwise, the HW kludge
	 *	that allows host access of the PLX DMA registers will
	 *	erroneously select the PLX registers.
	 */
	priv->dmadesc_s = (DMACHAIN *) S2DMA(priv->bcomm->bc_hostarea);
	if (priv->dmadesc_s)
		priv->dmadesc_h = (DMACHAIN *) S2H(priv->dmadesc_s);
	else
		priv->dmadesc_h = NULL;

	/*
	 *	Enable board interrupts
	 */
	if (priv->plxreg)
	{	/* PCI bus */
		OUTL(dev->base_addr + PLX_INT_CSR,
			inl(dev->base_addr + PLX_INT_CSR)
			| PLX_PCI_DOORBELL_IE);	/* Enable intr to host */
		OUTL(dev->base_addr + PLX_LCL2PCI_DOORBELL, 1);
	}
	else
	{	/* EISA bus */
	}

	return (0);
}

/*
 *	Probe (init) a board
 */
int
dgrs_probe1(struct device *dev)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;
	int		i;
	int		rc;

	printk("%s: Digi RightSwitch at io=%lx mem=%lx irq=%d plx=%lx dma=%lx ",
		dev->name, dev->base_addr, dev->mem_start, dev->irq,
		priv->plxreg, priv->plxdma);

	/*
	 *	Download the firmware and light the processor
	 */
	rc = dgrs_download(dev);
	if (rc)
	{
		printk("\n");
		return rc;
	}

	/*
	 * Get ether address of board
	 */
	memcpy(dev->dev_addr, priv->port->ethaddr, 6);
	for (i = 0; i < 6; ++i)
		printk("%c%2.2x", i ? ':' : ' ', dev->dev_addr[i]);
	printk("\n");

	if (dev->dev_addr[0] & 1)
	{
		printk("%s: Illegal Ethernet Address", dev->name);
		return (-ENXIO);
	}

	/*
	 *	ACK outstanding interrupts, hook the interrupt,
	 *	and verify that we are getting interrupts from the board.
	 */
	if (priv->plxreg)
		OUTL(dev->base_addr + PLX_LCL2PCI_DOORBELL, 1);
	rc = request_irq(dev->irq, &dgrs_intr, 0, "RightSwitch", dev);
	if (rc)
		return (rc);

	priv->intrcnt = 0;
	for (i = jiffies + 2*HZ + HZ/2; i > jiffies; )
		if (priv->intrcnt >= 2)
			break;
	if (priv->intrcnt < 2)
	{
		printk("%s: Not interrupting on IRQ %d (%d)",
				dev->name, dev->irq, priv->intrcnt);
		return (-ENXIO);
	}

	/*
	 *	Register the /proc/ioports information...
	 */
	request_region(dev->base_addr, 256, "RightSwitch");

	/*
	 *	Entry points...
	 */
	dev->open = &dgrs_open;
	dev->stop = &dgrs_close;
	dev->get_stats = &dgrs_get_stats;
	dev->hard_start_xmit = &dgrs_start_xmit;
	dev->set_multicast_list = &dgrs_set_multicast_list;
	dev->do_ioctl = &dgrs_ioctl;

	return (0);
}

static int
dgrs_found_device(
	struct device	*dev,
	int		io,
	ulong		mem,
	int		irq,
	ulong		plxreg,
	ulong		plxdma
)
{
	DGRS_PRIV	*priv;

	#ifdef MODULE
	{
		/* Allocate and fill new device structure. */
		int dev_size = sizeof(struct device) + sizeof(DGRS_PRIV);

		dev = (struct device *) kmalloc(dev_size, GFP_KERNEL);
		memset(dev, 0, dev_size);
		dev->priv = ((void *)dev) + sizeof(struct device);
		priv = (DGRS_PRIV *)dev->priv;

		dev->name = priv->devname; /* An empty string. */
		dev->base_addr = io;
		dev->mem_start = mem;
		dev->mem_end = mem + 2048 * 1024 - 1;
		dev->irq = irq;
		priv->plxreg = plxreg;
		priv->plxdma = plxdma;
		priv->vplxdma = NULL;

		dev->init = dgrs_probe1;

		ether_setup(dev);
		priv->next_dev = dgrs_root_dev;
		dgrs_root_dev = dev;
		if (register_netdev(dev) != 0)
			return -EIO;
	}
	#else
	{
		if (dev)
		{
			dev->priv = kmalloc(sizeof (DGRS_PRIV), GFP_KERNEL);
			memset(dev->priv, 0, sizeof (DGRS_PRIV));
		}
		dev = init_etherdev(dev, sizeof(DGRS_PRIV));
		priv = (DGRS_PRIV *)dev->priv;

		dev->base_addr = io;
		dev->mem_start = mem;
		dev->mem_end = mem + 2048 * 1024;
		dev->irq = irq;
		priv->plxreg = plxreg;
		priv->plxdma = plxdma;
		priv->vplxdma = NULL;

		dgrs_probe1(dev);
	}
	#endif

	return (0);
}

/*
 *	Scan for all boards
 */
static int
dgrs_scan(struct device *dev)
{
	int	cards_found = 0;
	uint	io;
	uint	mem;
	uint	irq;
	uint	plxreg;
	uint	plxdma;

	/*
	 *	First, check for PCI boards
	 */
	if (pcibios_present())
	{
		int pci_index = 0;

		for (; pci_index < 8; pci_index++)
		{
			uchar	pci_bus, pci_device_fn;
			uchar	pci_irq;
			uchar	pci_latency;
			ushort	pci_command;

			if (pcibios_find_device(SE6_PCI_VENDOR_ID,
							SE6_PCI_DEVICE_ID,
							pci_index, &pci_bus,
							&pci_device_fn))
					break;

			pcibios_read_config_byte(pci_bus, pci_device_fn,
					PCI_INTERRUPT_LINE, &pci_irq);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
					PCI_BASE_ADDRESS_0, &plxreg);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
					PCI_BASE_ADDRESS_1, &io);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
					PCI_BASE_ADDRESS_2, &mem);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
					0x30, &plxdma);
			irq = pci_irq;
			plxreg &= ~15;
			io &= ~3;
			mem &= ~15;
			plxdma &= ~15;

			/*
			 * On some BIOSES, the PLX "expansion rom" (used for DMA)
			 * address comes up as "0".  This is probably because
			 * the BIOS doesn't see a valid 55 AA ROM signature at
			 * the "ROM" start and zeroes the address.  To get
			 * around this problem the SE-6 is configured to ask
			 * for 4 MB of space for the dual port memory.  We then
			 * must set its range back to 2 MB, and use the upper
			 * half for DMA register access
			 */
			OUTL(io + PLX_SPACE0_RANGE, 0xFFE00000L);
			if (plxdma == 0)
				plxdma = mem + (2048L * 1024L);
			pcibios_write_config_dword(pci_bus, pci_device_fn,
					0x30, plxdma + 1);
			pcibios_read_config_dword(pci_bus, pci_device_fn,
					0x30, &plxdma);
			plxdma &= ~15;

			/*
			 * Get and check the bus-master and latency values.
			 * Some PCI BIOSes fail to set the master-enable bit,
			 * and the latency timer must be set to the maximum
			 * value to avoid data corruption that occurs when the
			 * timer expires during a transfer.  Yes, it's a bug.
			 */
			pcibios_read_config_word(pci_bus, pci_device_fn,
						 PCI_COMMAND, &pci_command);
			if ( ! (pci_command & PCI_COMMAND_MASTER))
			{
				printk("  Setting the PCI Master Bit!\n");
				pci_command |= PCI_COMMAND_MASTER;
				pcibios_write_config_word(pci_bus,
						pci_device_fn,
						PCI_COMMAND, pci_command);
			}
			pcibios_read_config_byte(pci_bus, pci_device_fn,
					 PCI_LATENCY_TIMER, &pci_latency);
			if (pci_latency != 255)
			{
				printk("  Overriding PCI latency timer: "
					"was %d, now is 255.\n", pci_latency);
				pcibios_write_config_byte(pci_bus,
						pci_device_fn,
						PCI_LATENCY_TIMER, 255);
			}

			dgrs_found_device(dev, io, mem, irq, plxreg, plxdma);

			dev = 0;
			cards_found++;
		}
	}

	/*
	 *	Second, check for EISA boards
	 */
	if (EISA_bus)
	{
		static int      is2iv[8] = { 0, 3, 5, 7, 10, 11, 12, 15 };

		for (io = 0x1000; io < 0x9000; io += 0x1000)
		{
			if (inb(io+ES4H_MANUFmsb) != 0x10
				|| inb(io+ES4H_MANUFlsb) != 0x49
				|| inb(io+ES4H_PRODUCT) != ES4H_PRODUCT_CODE)
				continue;

			if ( ! (inb(io+ES4H_EC) & ES4H_EC_ENABLE) )
				continue; /* Not EISA configured */

			mem = (inb(io+ES4H_AS_31_24) << 24)
				+ (inb(io+ES4H_AS_23_16) << 16);

			irq = is2iv[ inb(io+ES4H_IS) & ES4H_IS_INTMASK ];
			
			dgrs_found_device(dev, io, mem, irq, 0L, 0L);

			dev = 0;
			++cards_found;
		}
	}

	return cards_found;
}

/*
 *	Module/driver initialization points.  Two ways, depending on
 *	whether we are a module or statically linked, ala Don Becker's
 *	3c59x driver.
 */

#ifdef MODULE

/*
 *	Variables that can be overridden from command line
 */
static int	debug = -1;
static int	dma = -1;
static int	hashexpire = -1;
static int	spantree = -1;
static int	ipaddr[4] = { -1 };
static long	ipxnet = -1;

int
init_module(void)
{
	int	cards_found;

	/*
	 *	Command line variable overrides
	 *		debug=NNN
	 *		dma=0/1
	 *		spantree=0/1
	 *		hashexpire=NNN
	 *		ipaddr=A,B,C,D
	 *		ipxnet=NNN
	 */
	if (debug >= 0)
		dgrs_debug = debug;
	if (dma >= 0)
		dgrs_dma = dma;
	if (hashexpire >= 0)
		dgrs_hashexpire = hashexpire;
	if (spantree >= 0)
		dgrs_spantree = spantree;
	if (ipaddr[0] != -1)
	{
		int	i;

		for (i = 0; i < 4; ++i)
			dgrs_ipaddr[i] = ipaddr[i];
	}
	if (ipxnet != -1)
		dgrs_ipxnet = htonl( ipxnet );
		
	if (dgrs_debug)
	{
		printk("dgrs: SW=%s FW=Build %d %s\n",
			version, dgrs_firmnum, dgrs_firmdate);
	}
	
	/*
	 *	Find and configure all the cards
	 */
	dgrs_root_dev = NULL;
	cards_found = dgrs_scan(0);

	return cards_found ? 0 : -ENODEV;
}

void
cleanup_module(void)
{
        while (dgrs_root_dev)
	{
		struct device	*next_dev;
		DGRS_PRIV	*priv;

		priv = (DGRS_PRIV *) dgrs_root_dev->priv;
                next_dev = priv->next_dev;
                unregister_netdev(dgrs_root_dev);

		proc_reset(dgrs_root_dev, 1);

		if (priv->vmem)
			vfree(priv->vmem);
		if (priv->vplxdma)
			vfree((uchar *) priv->vplxdma);

		release_region(dgrs_root_dev->base_addr, 256);

		free_irq(dgrs_root_dev->irq, dgrs_root_dev);

                kfree(dgrs_root_dev);
                dgrs_root_dev = next_dev;
        }
}

#else

int
dgrs_probe(struct device *dev)
{
	int	cards_found;

	cards_found = dgrs_scan(dev);
	if (dgrs_debug && cards_found)
		printk("dgrs: SW=%s FW=Build %d %s\n",
			version, dgrs_firmnum, dgrs_firmdate);
	return cards_found ? 0 : -ENODEV;
}
#endif

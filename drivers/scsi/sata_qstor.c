/*
 *  sata_qstor.c - Pacific Digital Corporation QStor SATA
 *
 *  Maintained by:  Mark Lord <mlord@pobox.com>
 *
 *  Copyright 2005 Pacific Digital Corporation.
 *  (OSL/GPL code release authorized by Jalil Fadavi).
 *
 *  The contents of this file are subject to the Open
 *  Software License version 1.1 that can be found at
 *  http://www.opensource.org/licenses/osl-1.1.txt and is included herein
 *  by reference.
 *
 *  Alternatively, the contents of this file may be used under the terms
 *  of the GNU General Public License version 2 (the "GPL") as distributed
 *  in the kernel source COPYING file, in which case the provisions of
 *  the GPL are applicable instead of the above.  If you wish to allow
 *  the use of your version of this file only under the terms of the
 *  GPL and not to allow others to use your version of this file under
 *  the OSL, indicate your decision by deleting the provisions above and
 *  replace them with the notice and other provisions required by the GPL.
 *  If you do not delete the provisions above, a recipient may use your
 *  version of this file under either the OSL or the GPL.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <asm/io.h>
#include <linux/libata.h>

#define DRV_NAME	"sata_qstor"
#define DRV_VERSION	"0.04"

enum {
	QS_PORTS		= 4,
	QS_MAX_PRD		= LIBATA_MAX_PRD,
	QS_CPB_ORDER		= 6,
	QS_CPB_BYTES		= (1 << QS_CPB_ORDER),
	QS_PRD_BYTES		= QS_MAX_PRD * 16,
	QS_PKT_BYTES		= QS_CPB_BYTES + QS_PRD_BYTES,

	QS_DMA_BOUNDARY		= ~0UL,

	/* global register offsets */
	QS_HCF_CNFG3		= 0x0003, /* host configuration offset */
	QS_HID_HPHY		= 0x0004, /* host physical interface info */
	QS_HCT_CTRL		= 0x00e4, /* global interrupt mask offset */
	QS_HST_SFF		= 0x0100, /* host status fifo offset */
	QS_HVS_SERD3		= 0x0393, /* PHY enable offset */

	/* global control bits */
	QS_HPHY_64BIT		= (1 << 1), /* 64-bit bus detected */
	QS_CNFG3_GSRST		= 0x01,     /* global chip reset */
	QS_SERD3_PHY_ENA	= 0xf0,     /* PHY detection ENAble*/

	/* per-channel register offsets */
	QS_CCF_CPBA		= 0x0710, /* chan CPB base address */
	QS_CCF_CSEP		= 0x0718, /* chan CPB separation factor */
	QS_CFC_HUFT		= 0x0800, /* host upstream fifo threshold */
	QS_CFC_HDFT		= 0x0804, /* host downstream fifo threshold */
	QS_CFC_DUFT		= 0x0808, /* dev upstream fifo threshold */
	QS_CFC_DDFT		= 0x080c, /* dev downstream fifo threshold */
	QS_CCT_CTR0		= 0x0900, /* chan control-0 offset */
	QS_CCT_CTR1		= 0x0901, /* chan control-1 offset */
	QS_CCT_CFF		= 0x0a00, /* chan command fifo offset */

	/* channel control bits */
	QS_CTR0_REG		= (1 << 1),   /* register mode (vs. pkt mode) */
	QS_CTR0_CLER		= (1 << 2),   /* clear channel errors */
	QS_CTR1_RDEV		= (1 << 1),   /* sata phy/comms reset */
	QS_CTR1_RCHN		= (1 << 4),   /* reset channel logic */
	QS_CCF_RUN_PKT		= 0x107,      /* RUN a new dma PKT */

	/* pkt sub-field headers */
	QS_HCB_HDR		= 0x01,   /* Host Control Block header */
	QS_DCB_HDR		= 0x02,   /* Device Control Block header */

	/* pkt HCB flag bits */
	QS_HF_DIRO		= (1 << 0),   /* data DIRection Out */
	QS_HF_DAT		= (1 << 3),   /* DATa pkt */
	QS_HF_IEN		= (1 << 4),   /* Interrupt ENable */
	QS_HF_VLD		= (1 << 5),   /* VaLiD pkt */

	/* pkt DCB flag bits */
	QS_DF_PORD		= (1 << 2),   /* Pio OR Dma */
	QS_DF_ELBA		= (1 << 3),   /* Extended LBA (lba48) */

	/* PCI device IDs */
	board_2068_idx		= 0,	/* QStor 4-port SATA/RAID */
};

typedef enum { qs_state_idle, qs_state_pkt, qs_state_mmio } qs_state_t;

struct qs_port_priv {
	u8			*pkt;
	dma_addr_t		pkt_dma;
	qs_state_t		state;
};

static u32 qs_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void qs_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static int qs_ata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static irqreturn_t qs_intr (int irq, void *dev_instance, struct pt_regs *regs);
static int qs_port_start(struct ata_port *ap);
static void qs_host_stop(struct ata_host_set *host_set);
static void qs_port_stop(struct ata_port *ap);
static void qs_phy_reset(struct ata_port *ap);
static void qs_qc_prep(struct ata_queued_cmd *qc);
static int qs_qc_issue(struct ata_queued_cmd *qc);
static int qs_check_atapi_dma(struct ata_queued_cmd *qc);
static void qs_bmdma_stop(struct ata_port *ap);
static u8 qs_bmdma_status(struct ata_port *ap);
static void qs_irq_clear(struct ata_port *ap);
static void qs_eng_timeout(struct ata_port *ap);

static Scsi_Host_Template qs_ata_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= QS_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	//FIXME .use_clustering		= ATA_SHT_USE_CLUSTERING,
	.use_clustering		= ENABLE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= QS_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations qs_ata_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.check_atapi_dma	= qs_check_atapi_dma,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,
	.phy_reset		= qs_phy_reset,
	.qc_prep		= qs_qc_prep,
	.qc_issue		= qs_qc_issue,
	.eng_timeout		= qs_eng_timeout,
	.irq_handler		= qs_intr,
	.irq_clear		= qs_irq_clear,
	.scr_read		= qs_scr_read,
	.scr_write		= qs_scr_write,
	.port_start		= qs_port_start,
	.port_stop		= qs_port_stop,
	.host_stop		= qs_host_stop,
	.bmdma_stop		= qs_bmdma_stop,
	.bmdma_status		= qs_bmdma_status,
};

static struct ata_port_info qs_port_info[] = {
	/* board_2068_idx */
	{
		.sht		= &qs_ata_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_SATA_RESET |
				  //FIXME ATA_FLAG_SRST |
				  ATA_FLAG_MMIO,
		.pio_mask	= 0x10, /* pio4 */
		.udma_mask	= 0x7f, /* udma0-6 */
		.port_ops	= &qs_ata_ops,
	},
};

static struct pci_device_id qs_ata_pci_tbl[] = {
	{ PCI_VENDOR_ID_PDC, 0x2068, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2068_idx },

	{ }	/* terminate list */
};

static struct pci_driver qs_ata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= qs_ata_pci_tbl,
	.probe			= qs_ata_init_one,
	.remove			= ata_pci_remove_one,
};

static int qs_check_atapi_dma(struct ata_queued_cmd *qc)
{
	return 1;	/* ATAPI DMA not supported */
}

static void qs_bmdma_stop(struct ata_port *ap)
{
	/* nothing */
}

static u8 qs_bmdma_status(struct ata_port *ap)
{
	return 0;
}

static void qs_irq_clear(struct ata_port *ap)
{
	/* nothing */
}

static inline void qs_enter_reg_mode(struct ata_port *ap)
{
	u8 __iomem *chan = ap->host_set->mmio_base + (ap->port_no * 0x4000);

	writeb(QS_CTR0_REG, chan + QS_CCT_CTR0);
	readb(chan + QS_CCT_CTR0);        /* flush */
}

static inline void qs_reset_channel_logic(struct ata_port *ap)
{
	u8 __iomem *chan = ap->host_set->mmio_base + (ap->port_no * 0x4000);

	writeb(QS_CTR1_RCHN, chan + QS_CCT_CTR1);
	readb(chan + QS_CCT_CTR0);        /* flush */
	qs_enter_reg_mode(ap);
}

static void qs_phy_reset(struct ata_port *ap)
{
	struct qs_port_priv *pp = ap->private_data;

	pp->state = qs_state_idle;
	qs_reset_channel_logic(ap);
	sata_phy_reset(ap);
}

static void qs_eng_timeout(struct ata_port *ap)
{
	struct qs_port_priv *pp = ap->private_data;

	if (pp->state != qs_state_idle) /* healthy paranoia */
		pp->state = qs_state_mmio;
	qs_reset_channel_logic(ap);
	ata_eng_timeout(ap);
}

static u32 qs_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return ~0U;
	return readl((void __iomem *)(ap->ioaddr.scr_addr + (sc_reg * 8)));
}

static void qs_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;
	writel(val, (void __iomem *)(ap->ioaddr.scr_addr + (sc_reg * 8)));
}

static void qs_fill_sg(struct ata_queued_cmd *qc)
{
	struct scatterlist *sg = qc->sg;
	struct ata_port *ap = qc->ap;
	struct qs_port_priv *pp = ap->private_data;
	unsigned int nelem;
	u8 *prd = pp->pkt + QS_CPB_BYTES;

	assert(sg != NULL);
	assert(qc->n_elem > 0);

	for (nelem = 0; nelem < qc->n_elem; nelem++,sg++) {
		u64 addr;
		u32 len;

		addr = sg_dma_address(sg);
		*(__le64 *)prd = cpu_to_le64(addr);
		prd += sizeof(u64);

		len = sg_dma_len(sg);
		*(__le32 *)prd = cpu_to_le32(len);
		prd += sizeof(u64);

		VPRINTK("PRD[%u] = (0x%llX, 0x%X)\n", nelem,
					(unsigned long long)addr, len);
	}
}

static void qs_qc_prep(struct ata_queued_cmd *qc)
{
	struct qs_port_priv *pp = qc->ap->private_data;
	u8 dflags = QS_DF_PORD, *buf = pp->pkt;
	u8 hflags = QS_HF_DAT | QS_HF_IEN | QS_HF_VLD;
	u64 addr;

	VPRINTK("ENTER\n");

	qs_enter_reg_mode(qc->ap);
	if (qc->tf.protocol != ATA_PROT_DMA) {
		ata_qc_prep(qc);
		return;
	}

	qs_fill_sg(qc);

	if ((qc->tf.flags & ATA_TFLAG_WRITE))
		hflags |= QS_HF_DIRO;
	if ((qc->tf.flags & ATA_TFLAG_LBA48))
		dflags |= QS_DF_ELBA;

	/* host control block (HCB) */
	buf[ 0] = QS_HCB_HDR;
	buf[ 1] = hflags;
	*(__le32 *)(&buf[ 4]) = cpu_to_le32(qc->nsect * ATA_SECT_SIZE);
	*(__le32 *)(&buf[ 8]) = cpu_to_le32(qc->n_elem);
	addr = ((u64)pp->pkt_dma) + QS_CPB_BYTES;
	*(__le64 *)(&buf[16]) = cpu_to_le64(addr);

	/* device control block (DCB) */
	buf[24] = QS_DCB_HDR;
	buf[28] = dflags;

	/* frame information structure (FIS) */
	ata_tf_to_fis(&qc->tf, &buf[32], 0);
}

static inline void qs_packet_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 __iomem *chan = ap->host_set->mmio_base + (ap->port_no * 0x4000);

	VPRINTK("ENTER, ap %p\n", ap);

	writeb(QS_CTR0_CLER, chan + QS_CCT_CTR0);
	wmb();                             /* flush PRDs and pkt to memory */
	writel(QS_CCF_RUN_PKT, chan + QS_CCT_CFF);
	readl(chan + QS_CCT_CFF);          /* flush */
}

static int qs_qc_issue(struct ata_queued_cmd *qc)
{
	struct qs_port_priv *pp = qc->ap->private_data;

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:

		pp->state = qs_state_pkt;
		qs_packet_start(qc);
		return 0;

	case ATA_PROT_ATAPI_DMA:
		BUG();
		break;

	default:
		break;
	}

	pp->state = qs_state_mmio;
	return ata_qc_issue_prot(qc);
}

static inline unsigned int qs_intr_pkt(struct ata_host_set *host_set)
{
	unsigned int handled = 0;
	u8 sFFE;
	u8 __iomem *mmio_base = host_set->mmio_base;

	do {
		u32 sff0 = readl(mmio_base + QS_HST_SFF);
		u32 sff1 = readl(mmio_base + QS_HST_SFF + 4);
		u8 sEVLD = (sff1 >> 30) & 0x01;	/* valid flag */
		sFFE  = sff1 >> 31;		/* empty flag */

		if (sEVLD) {
			u8 sDST = sff0 >> 16;	/* dev status */
			u8 sHST = sff1 & 0x3f;	/* host status */
			unsigned int port_no = (sff1 >> 8) & 0x03;
			struct ata_port *ap = host_set->ports[port_no];

			DPRINTK("SFF=%08x%08x: sCHAN=%u sHST=%d sDST=%02x\n",
					sff1, sff0, port_no, sHST, sDST);
			handled = 1;
			if (ap && (!(ap->flags & ATA_FLAG_PORT_DISABLED))) {
				struct ata_queued_cmd *qc;
				struct qs_port_priv *pp = ap->private_data;
				if (!pp || pp->state != qs_state_pkt)
					continue;
				qc = ata_qc_from_tag(ap, ap->active_tag);
				if (qc && (!(qc->tf.ctl & ATA_NIEN))) {
					switch (sHST) {
					case 0: /* sucessful CPB */
					case 3: /* device error */
						pp->state = qs_state_idle;
						qs_enter_reg_mode(qc->ap);
						ata_qc_complete(qc, sDST);
						break;
					default:
						break;
					}
				}
			}
		}
	} while (!sFFE);
	return handled;
}

static inline unsigned int qs_intr_mmio(struct ata_host_set *host_set)
{
	unsigned int handled = 0, port_no;

	for (port_no = 0; port_no < host_set->n_ports; ++port_no) {
		struct ata_port *ap;
		ap = host_set->ports[port_no];
		if (ap && (!(ap->flags & ATA_FLAG_PORT_DISABLED))) {
			struct ata_queued_cmd *qc;
			struct qs_port_priv *pp = ap->private_data;
			if (!pp || pp->state != qs_state_mmio)
				continue;
			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.ctl & ATA_NIEN))) {

				/* check main status, clearing INTRQ */
				u8 status = ata_chk_status(ap);
				if ((status & ATA_BUSY))
					continue;
				DPRINTK("ata%u: protocol %d (dev_stat 0x%X)\n",
					ap->id, qc->tf.protocol, status);
		
				/* complete taskfile transaction */
				pp->state = qs_state_idle;
				ata_qc_complete(qc, status);
				handled = 1;
			}
		}
	}
	return handled;
}

static irqreturn_t qs_intr(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned int handled = 0;

	VPRINTK("ENTER\n");

	spin_lock(&host_set->lock);
	handled  = qs_intr_pkt(host_set) | qs_intr_mmio(host_set);
	spin_unlock(&host_set->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static void qs_ata_setup_port(struct ata_ioports *port, unsigned long base)
{
	port->cmd_addr		=
	port->data_addr		= base + 0x400;
	port->error_addr	=
	port->feature_addr	= base + 0x408; /* hob_feature = 0x409 */
	port->nsect_addr	= base + 0x410; /* hob_nsect   = 0x411 */
	port->lbal_addr		= base + 0x418; /* hob_lbal    = 0x419 */
	port->lbam_addr		= base + 0x420; /* hob_lbam    = 0x421 */
	port->lbah_addr		= base + 0x428; /* hob_lbah    = 0x429 */
	port->device_addr	= base + 0x430;
	port->status_addr	=
	port->command_addr	= base + 0x438;
	port->altstatus_addr	=
	port->ctl_addr		= base + 0x440;
	port->scr_addr		= base + 0xc00;
}

static int qs_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct qs_port_priv *pp;
	void __iomem *mmio_base = ap->host_set->mmio_base;
	void __iomem *chan = mmio_base + (ap->port_no * 0x4000);
	u64 addr;
	int rc;

	rc = ata_port_start(ap);
	if (rc)
		return rc;
	qs_enter_reg_mode(ap);
	pp = kcalloc(1, sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		rc = -ENOMEM;
		goto err_out;
	}
	pp->pkt = dma_alloc_coherent(dev, QS_PKT_BYTES, &pp->pkt_dma,
								GFP_KERNEL);
	if (!pp->pkt) {
		rc = -ENOMEM;
		goto err_out_kfree;
	}
	memset(pp->pkt, 0, QS_PKT_BYTES);
	ap->private_data = pp;

	addr = (u64)pp->pkt_dma;
	writel((u32) addr,        chan + QS_CCF_CPBA);
	writel((u32)(addr >> 32), chan + QS_CCF_CPBA + 4);
	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}

static void qs_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct qs_port_priv *pp = ap->private_data;

	if (pp != NULL) {
		ap->private_data = NULL;
		if (pp->pkt != NULL)
			dma_free_coherent(dev, QS_PKT_BYTES, pp->pkt,
								pp->pkt_dma);
		kfree(pp);
	}
	ata_port_stop(ap);
}

static void qs_host_stop(struct ata_host_set *host_set)
{
	void __iomem *mmio_base = host_set->mmio_base;

	writeb(0, mmio_base + QS_HCT_CTRL); /* disable host interrupts */
	writeb(QS_CNFG3_GSRST, mmio_base + QS_HCF_CNFG3); /* global reset */
}

static void qs_host_init(unsigned int chip_id, struct ata_probe_ent *pe)
{
	void __iomem *mmio_base = pe->mmio_base;
	unsigned int port_no;

	writeb(0, mmio_base + QS_HCT_CTRL); /* disable host interrupts */
	writeb(QS_CNFG3_GSRST, mmio_base + QS_HCF_CNFG3); /* global reset */

	/* reset each channel in turn */
	for (port_no = 0; port_no < pe->n_ports; ++port_no) {
		u8 __iomem *chan = mmio_base + (port_no * 0x4000);
		writeb(QS_CTR1_RDEV|QS_CTR1_RCHN, chan + QS_CCT_CTR1);
		writeb(QS_CTR0_REG, chan + QS_CCT_CTR0);
		readb(chan + QS_CCT_CTR0);        /* flush */
	}
	writeb(QS_SERD3_PHY_ENA, mmio_base + QS_HVS_SERD3); /* enable phy */

	for (port_no = 0; port_no < pe->n_ports; ++port_no) {
		u8 __iomem *chan = mmio_base + (port_no * 0x4000);
		/* set FIFO depths to same settings as Windows driver */
		writew(32, chan + QS_CFC_HUFT);
		writew(32, chan + QS_CFC_HDFT);
		writew(10, chan + QS_CFC_DUFT);
		writew( 8, chan + QS_CFC_DDFT);
		/* set CPB size in bytes, as a power of two */
		writeb(QS_CPB_ORDER,    chan + QS_CCF_CSEP);
	}
	writeb(1, mmio_base + QS_HCT_CTRL); /* enable host interrupts */
}

/*
 * The QStor understands 64-bit buses, and uses 64-bit fields
 * for DMA pointers regardless of bus width.  We just have to
 * make sure our DMA masks are set appropriately for whatever
 * bridge lies between us and the QStor, and then the DMA mapping
 * code will ensure we only ever "see" appropriate buffer addresses.
 * If we're 32-bit limited somewhere, then our 64-bit fields will
 * just end up with zeros in the upper 32-bits, without any special
 * logic required outside of this routine (below).
 */
static int qs_set_dma_masks(struct pci_dev *pdev, void __iomem *mmio_base)
{
	u32 bus_info = readl(mmio_base + QS_HID_HPHY);
	int rc, have_64bit_bus = (bus_info & QS_HPHY_64BIT);

	if (have_64bit_bus &&
	    !pci_set_dma_mask(pdev, DMA_64BIT_MASK)) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
		if (rc) {
			rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
			if (rc) {
				printk(KERN_ERR DRV_NAME
					"(%s): 64-bit DMA enable failed\n",
					pci_name(pdev));
				return rc;
			}
		}
	} else {
		rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc) {
			printk(KERN_ERR DRV_NAME
				"(%s): 32-bit DMA enable failed\n",
				pci_name(pdev));
			return rc;
		}
		rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc) {
			printk(KERN_ERR DRV_NAME
				"(%s): 32-bit consistent DMA enable failed\n",
				pci_name(pdev));
			return rc;
		}
	}
	return 0;
}

static int qs_ata_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	void __iomem *mmio_base;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	int rc, port_no;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

	if ((pci_resource_flags(pdev, 4) & IORESOURCE_MEM) == 0) {
		rc = -ENODEV;
		goto err_out_regions;
	}

	mmio_base = ioremap(pci_resource_start(pdev, 4),
		            pci_resource_len(pdev, 4));
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	rc = qs_set_dma_masks(pdev, mmio_base);
	if (rc)
		goto err_out_iounmap;

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_iounmap;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	probe_ent->sht		= qs_port_info[board_idx].sht;
	probe_ent->host_flags	= qs_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= qs_port_info[board_idx].pio_mask;
	probe_ent->mwdma_mask	= qs_port_info[board_idx].mwdma_mask;
	probe_ent->udma_mask	= qs_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= qs_port_info[board_idx].port_ops;

	probe_ent->irq		= pdev->irq;
	probe_ent->irq_flags	= SA_SHIRQ;
	probe_ent->mmio_base	= mmio_base;
	probe_ent->n_ports	= QS_PORTS;

	for (port_no = 0; port_no < probe_ent->n_ports; ++port_no) {
		unsigned long chan = (unsigned long)mmio_base +
							(port_no * 0x4000);
		qs_ata_setup_port(&probe_ent->port[port_no], chan);
	}

	pci_set_master(pdev);

	/* initialize adapter */
	qs_host_init(board_idx, probe_ent);

	rc = ata_device_add(probe_ent);
	kfree(probe_ent);
	if (rc != QS_PORTS)
		goto err_out_iounmap;
	return 0;

err_out_iounmap:
	iounmap(mmio_base);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}

static int __init qs_ata_init(void)
{
	return pci_module_init(&qs_ata_pci_driver);
}

static void __exit qs_ata_exit(void)
{
	pci_unregister_driver(&qs_ata_pci_driver);
}

MODULE_AUTHOR("Mark Lord");
MODULE_DESCRIPTION("Pacific Digital Corporation QStor SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, qs_ata_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(qs_ata_init);
module_exit(qs_ata_exit);

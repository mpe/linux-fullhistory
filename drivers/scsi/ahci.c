/*
 *  ahci.c - AHCI SATA support
 *
 *  Copyright 2004 Red Hat, Inc.
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
 * Version 1.0 of the AHCI specification:
 * http://www.intel.com/technology/serialata/pdf/rev1_0.pdf
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
#include <linux/libata.h>
#include <asm/io.h>

#define DRV_NAME	"ahci"
#define DRV_VERSION	"1.00"


enum {
	AHCI_PCI_BAR		= 5,
	AHCI_MAX_SG		= 168, /* hardware max is 64K */
	AHCI_DMA_BOUNDARY	= 0xffffffff,
	AHCI_USE_CLUSTERING	= 0,
	AHCI_CMD_SLOT_SZ	= 32 * 32,
	AHCI_RX_FIS_SZ		= 256,
	AHCI_CMD_TBL_HDR	= 0x80,
	AHCI_CMD_TBL_SZ		= AHCI_CMD_TBL_HDR + (AHCI_MAX_SG * 16),
	AHCI_PORT_PRIV_DMA_SZ	= AHCI_CMD_SLOT_SZ + AHCI_CMD_TBL_SZ +
				  AHCI_RX_FIS_SZ,
	AHCI_IRQ_ON_SG		= (1 << 31),
	AHCI_CMD_ATAPI		= (1 << 5),
	AHCI_CMD_WRITE		= (1 << 6),

	RX_FIS_D2H_REG		= 0x40,	/* offset of D2H Register FIS data */

	board_ahci		= 0,

	/* global controller registers */
	HOST_CAP		= 0x00, /* host capabilities */
	HOST_CTL		= 0x04, /* global host control */
	HOST_IRQ_STAT		= 0x08, /* interrupt status */
	HOST_PORTS_IMPL		= 0x0c, /* bitmap of implemented ports */
	HOST_VERSION		= 0x10, /* AHCI spec. version compliancy */

	/* HOST_CTL bits */
	HOST_RESET		= (1 << 0),  /* reset controller; self-clear */
	HOST_IRQ_EN		= (1 << 1),  /* global IRQ enable */
	HOST_AHCI_EN		= (1 << 31), /* AHCI enabled */

	/* HOST_CAP bits */
	HOST_CAP_64		= (1 << 31), /* PCI DAC (64-bit DMA) support */

	/* registers for each SATA port */
	PORT_LST_ADDR		= 0x00, /* command list DMA addr */
	PORT_LST_ADDR_HI	= 0x04, /* command list DMA addr hi */
	PORT_FIS_ADDR		= 0x08, /* FIS rx buf addr */
	PORT_FIS_ADDR_HI	= 0x0c, /* FIS rx buf addr hi */
	PORT_IRQ_STAT		= 0x10, /* interrupt status */
	PORT_IRQ_MASK		= 0x14, /* interrupt enable/disable mask */
	PORT_CMD		= 0x18, /* port command */
	PORT_TFDATA		= 0x20,	/* taskfile data */
	PORT_SIG		= 0x24,	/* device TF signature */
	PORT_CMD_ISSUE		= 0x38, /* command issue */
	PORT_SCR		= 0x28, /* SATA phy register block */
	PORT_SCR_STAT		= 0x28, /* SATA phy register: SStatus */
	PORT_SCR_CTL		= 0x2c, /* SATA phy register: SControl */
	PORT_SCR_ERR		= 0x30, /* SATA phy register: SError */
	PORT_SCR_ACT		= 0x34, /* SATA phy register: SActive */

	/* PORT_IRQ_{STAT,MASK} bits */
	PORT_IRQ_COLD_PRES	= (1 << 31), /* cold presence detect */
	PORT_IRQ_TF_ERR		= (1 << 30), /* task file error */
	PORT_IRQ_HBUS_ERR	= (1 << 29), /* host bus fatal error */
	PORT_IRQ_HBUS_DATA_ERR	= (1 << 28), /* host bus data error */
	PORT_IRQ_IF_ERR		= (1 << 27), /* interface fatal error */
	PORT_IRQ_IF_NONFATAL	= (1 << 26), /* interface non-fatal error */
	PORT_IRQ_OVERFLOW	= (1 << 24), /* xfer exhausted available S/G */
	PORT_IRQ_BAD_PMP	= (1 << 23), /* incorrect port multiplier */

	PORT_IRQ_PHYRDY		= (1 << 22), /* PhyRdy changed */
	PORT_IRQ_DEV_ILCK	= (1 << 7), /* device interlock */
	PORT_IRQ_CONNECT	= (1 << 6), /* port connect change status */
	PORT_IRQ_SG_DONE	= (1 << 5), /* descriptor processed */
	PORT_IRQ_UNK_FIS	= (1 << 4), /* unknown FIS rx'd */
	PORT_IRQ_SDB_FIS	= (1 << 3), /* Set Device Bits FIS rx'd */
	PORT_IRQ_DMAS_FIS	= (1 << 2), /* DMA Setup FIS rx'd */
	PORT_IRQ_PIOS_FIS	= (1 << 1), /* PIO Setup FIS rx'd */
	PORT_IRQ_D2H_REG_FIS	= (1 << 0), /* D2H Register FIS rx'd */

	PORT_IRQ_FATAL		= PORT_IRQ_TF_ERR |
				  PORT_IRQ_HBUS_ERR |
				  PORT_IRQ_HBUS_DATA_ERR |
				  PORT_IRQ_IF_ERR,
	DEF_PORT_IRQ		= PORT_IRQ_FATAL | PORT_IRQ_PHYRDY |
				  PORT_IRQ_CONNECT | PORT_IRQ_SG_DONE |
				  PORT_IRQ_UNK_FIS | PORT_IRQ_SDB_FIS |
				  PORT_IRQ_DMAS_FIS | PORT_IRQ_PIOS_FIS |
				  PORT_IRQ_D2H_REG_FIS,

	/* PORT_CMD bits */
	PORT_CMD_LIST_ON	= (1 << 15), /* cmd list DMA engine running */
	PORT_CMD_FIS_ON		= (1 << 14), /* FIS DMA engine running */
	PORT_CMD_FIS_RX		= (1 << 4), /* Enable FIS receive DMA engine */
	PORT_CMD_POWER_ON	= (1 << 2), /* Power up device */
	PORT_CMD_SPIN_UP	= (1 << 1), /* Spin up device */
	PORT_CMD_START		= (1 << 0), /* Enable port DMA engine */

	PORT_CMD_ICC_ACTIVE	= (0x1 << 28), /* Put i/f in active state */
	PORT_CMD_ICC_PARTIAL	= (0x2 << 28), /* Put i/f in partial state */
	PORT_CMD_ICC_SLUMBER	= (0x6 << 28), /* Put i/f in slumber state */
};

struct ahci_cmd_hdr {
	u32			opts;
	u32			status;
	u32			tbl_addr;
	u32			tbl_addr_hi;
	u32			reserved[4];
};

struct ahci_sg {
	u32			addr;
	u32			addr_hi;
	u32			reserved;
	u32			flags_size;
};

struct ahci_host_priv {
	unsigned long		flags;
	u32			cap;	/* cache of HOST_CAP register */
	u32			port_map; /* cache of HOST_PORTS_IMPL reg */
};

struct ahci_port_priv {
	struct ahci_cmd_hdr	*cmd_slot;
	dma_addr_t		cmd_slot_dma;
	void			*cmd_tbl;
	dma_addr_t		cmd_tbl_dma;
	struct ahci_sg		*cmd_tbl_sg;
	void			*rx_fis;
	dma_addr_t		rx_fis_dma;
};

static u32 ahci_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void ahci_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static int ahci_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static int ahci_qc_issue(struct ata_queued_cmd *qc);
static irqreturn_t ahci_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static void ahci_phy_reset(struct ata_port *ap);
static void ahci_irq_clear(struct ata_port *ap);
static void ahci_eng_timeout(struct ata_port *ap);
static int ahci_port_start(struct ata_port *ap);
static void ahci_port_stop(struct ata_port *ap);
static void ahci_host_stop(struct ata_host_set *host_set);
static void ahci_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
static void ahci_qc_prep(struct ata_queued_cmd *qc);
static u8 ahci_check_status(struct ata_port *ap);
static u8 ahci_check_err(struct ata_port *ap);
static inline int ahci_host_intr(struct ata_port *ap, struct ata_queued_cmd *qc);

static Scsi_Host_Template ahci_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= AHCI_MAX_SG,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= AHCI_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= AHCI_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
	.ordered_flush		= 1,
};

static struct ata_port_operations ahci_ops = {
	.port_disable		= ata_port_disable,

	.check_status		= ahci_check_status,
	.check_altstatus	= ahci_check_status,
	.check_err		= ahci_check_err,
	.dev_select		= ata_noop_dev_select,

	.tf_read		= ahci_tf_read,

	.phy_reset		= ahci_phy_reset,

	.qc_prep		= ahci_qc_prep,
	.qc_issue		= ahci_qc_issue,

	.eng_timeout		= ahci_eng_timeout,

	.irq_handler		= ahci_interrupt,
	.irq_clear		= ahci_irq_clear,

	.scr_read		= ahci_scr_read,
	.scr_write		= ahci_scr_write,

	.port_start		= ahci_port_start,
	.port_stop		= ahci_port_stop,
	.host_stop		= ahci_host_stop,
};

static struct ata_port_info ahci_port_info[] = {
	/* board_ahci */
	{
		.sht		= &ahci_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_SATA_RESET | ATA_FLAG_MMIO |
				  ATA_FLAG_PIO_DMA,
		.pio_mask	= 0x03, /* pio3-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &ahci_ops,
	},
};

static struct pci_device_id ahci_pci_tbl[] = {
	{ PCI_VENDOR_ID_INTEL, 0x2652, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH6 */
	{ PCI_VENDOR_ID_INTEL, 0x2653, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH6M */
	{ PCI_VENDOR_ID_INTEL, 0x27c1, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH7 */
	{ PCI_VENDOR_ID_INTEL, 0x27c5, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH7M */
	{ PCI_VENDOR_ID_INTEL, 0x27c3, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH7R */
	{ PCI_VENDOR_ID_AL, 0x5288, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ULi M5288 */
	{ }	/* terminate list */
};


static struct pci_driver ahci_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= ahci_pci_tbl,
	.probe			= ahci_init_one,
	.remove			= ata_pci_remove_one,
};


static inline unsigned long ahci_port_base_ul (unsigned long base, unsigned int port)
{
	return base + 0x100 + (port * 0x80);
}

static inline void *ahci_port_base (void *base, unsigned int port)
{
	return (void *) ahci_port_base_ul((unsigned long)base, port);
}

static void ahci_host_stop(struct ata_host_set *host_set)
{
	struct ahci_host_priv *hpriv = host_set->private_data;
	kfree(hpriv);
}

static int ahci_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct ahci_host_priv *hpriv = ap->host_set->private_data;
	struct ahci_port_priv *pp;
	int rc;
	void *mem, *mmio = ap->host_set->mmio_base;
	void *port_mmio = ahci_port_base(mmio, ap->port_no);
	dma_addr_t mem_dma;

	rc = ata_port_start(ap);
	if (rc)
		return rc;

	pp = kmalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		rc = -ENOMEM;
		goto err_out;
	}
	memset(pp, 0, sizeof(*pp));

	mem = dma_alloc_coherent(dev, AHCI_PORT_PRIV_DMA_SZ, &mem_dma, GFP_KERNEL);
	if (!mem) {
		rc = -ENOMEM;
		goto err_out_kfree;
	}
	memset(mem, 0, AHCI_PORT_PRIV_DMA_SZ);

	/*
	 * First item in chunk of DMA memory: 32-slot command table,
	 * 32 bytes each in size
	 */
	pp->cmd_slot = mem;
	pp->cmd_slot_dma = mem_dma;

	mem += AHCI_CMD_SLOT_SZ;
	mem_dma += AHCI_CMD_SLOT_SZ;

	/*
	 * Second item: Received-FIS area
	 */
	pp->rx_fis = mem;
	pp->rx_fis_dma = mem_dma;

	mem += AHCI_RX_FIS_SZ;
	mem_dma += AHCI_RX_FIS_SZ;

	/*
	 * Third item: data area for storing a single command
	 * and its scatter-gather table
	 */
	pp->cmd_tbl = mem;
	pp->cmd_tbl_dma = mem_dma;

	pp->cmd_tbl_sg = mem + AHCI_CMD_TBL_HDR;

	ap->private_data = pp;

	if (hpriv->cap & HOST_CAP_64)
		writel((pp->cmd_slot_dma >> 16) >> 16, port_mmio + PORT_LST_ADDR_HI);
	writel(pp->cmd_slot_dma & 0xffffffff, port_mmio + PORT_LST_ADDR);
	readl(port_mmio + PORT_LST_ADDR); /* flush */

	if (hpriv->cap & HOST_CAP_64)
		writel((pp->rx_fis_dma >> 16) >> 16, port_mmio + PORT_FIS_ADDR_HI);
	writel(pp->rx_fis_dma & 0xffffffff, port_mmio + PORT_FIS_ADDR);
	readl(port_mmio + PORT_FIS_ADDR); /* flush */

	writel(PORT_CMD_ICC_ACTIVE | PORT_CMD_FIS_RX |
	       PORT_CMD_POWER_ON | PORT_CMD_SPIN_UP |
	       PORT_CMD_START, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD); /* flush */

	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}


static void ahci_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct ahci_port_priv *pp = ap->private_data;
	void *mmio = ap->host_set->mmio_base;
	void *port_mmio = ahci_port_base(mmio, ap->port_no);
	u32 tmp;

	tmp = readl(port_mmio + PORT_CMD);
	tmp &= ~(PORT_CMD_START | PORT_CMD_FIS_RX);
	writel(tmp, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD); /* flush */

	/* spec says 500 msecs for each PORT_CMD_{START,FIS_RX} bit, so
	 * this is slightly incorrect.
	 */
	msleep(500);

	ap->private_data = NULL;
	dma_free_coherent(dev, AHCI_PORT_PRIV_DMA_SZ,
			  pp->cmd_slot, pp->cmd_slot_dma);
	kfree(pp);
	ata_port_stop(ap);
}

static u32 ahci_scr_read (struct ata_port *ap, unsigned int sc_reg_in)
{
	unsigned int sc_reg;

	switch (sc_reg_in) {
	case SCR_STATUS:	sc_reg = 0; break;
	case SCR_CONTROL:	sc_reg = 1; break;
	case SCR_ERROR:		sc_reg = 2; break;
	case SCR_ACTIVE:	sc_reg = 3; break;
	default:
		return 0xffffffffU;
	}

	return readl((void *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void ahci_scr_write (struct ata_port *ap, unsigned int sc_reg_in,
			       u32 val)
{
	unsigned int sc_reg;

	switch (sc_reg_in) {
	case SCR_STATUS:	sc_reg = 0; break;
	case SCR_CONTROL:	sc_reg = 1; break;
	case SCR_ERROR:		sc_reg = 2; break;
	case SCR_ACTIVE:	sc_reg = 3; break;
	default:
		return;
	}

	writel(val, (void *) ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void ahci_phy_reset(struct ata_port *ap)
{
	void __iomem *port_mmio = (void __iomem *) ap->ioaddr.cmd_addr;
	struct ata_taskfile tf;
	struct ata_device *dev = &ap->device[0];
	u32 tmp;

	__sata_phy_reset(ap);

	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;

	tmp = readl(port_mmio + PORT_SIG);
	tf.lbah		= (tmp >> 24)	& 0xff;
	tf.lbam		= (tmp >> 16)	& 0xff;
	tf.lbal		= (tmp >> 8)	& 0xff;
	tf.nsect	= (tmp)		& 0xff;

	dev->class = ata_dev_classify(&tf);
	if (!ata_dev_present(dev))
		ata_port_disable(ap);
}

static u8 ahci_check_status(struct ata_port *ap)
{
	void *mmio = (void *) ap->ioaddr.cmd_addr;

	return readl(mmio + PORT_TFDATA) & 0xFF;
}

static u8 ahci_check_err(struct ata_port *ap)
{
	void *mmio = (void *) ap->ioaddr.cmd_addr;

	return (readl(mmio + PORT_TFDATA) >> 8) & 0xFF;
}

static void ahci_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ahci_port_priv *pp = ap->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;

	ata_tf_from_fis(d2h_fis, tf);
}

static void ahci_fill_sg(struct ata_queued_cmd *qc)
{
	struct ahci_port_priv *pp = qc->ap->private_data;
	unsigned int i;

	VPRINTK("ENTER\n");

	/*
	 * Next, the S/G list.
	 */
	for (i = 0; i < qc->n_elem; i++) {
		u32 sg_len;
		dma_addr_t addr;

		addr = sg_dma_address(&qc->sg[i]);
		sg_len = sg_dma_len(&qc->sg[i]);

		pp->cmd_tbl_sg[i].addr = cpu_to_le32(addr & 0xffffffff);
		pp->cmd_tbl_sg[i].addr_hi = cpu_to_le32((addr >> 16) >> 16);
		pp->cmd_tbl_sg[i].flags_size = cpu_to_le32(sg_len - 1);
	}
}

static void ahci_qc_prep(struct ata_queued_cmd *qc)
{
	struct ahci_port_priv *pp = qc->ap->private_data;
	u32 opts;
	const u32 cmd_fis_len = 5; /* five dwords */

	/*
	 * Fill in command slot information (currently only one slot,
	 * slot 0, is currently since we don't do queueing)
	 */

	opts = (qc->n_elem << 16) | cmd_fis_len;
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		opts |= AHCI_CMD_WRITE;

	switch (qc->tf.protocol) {
	case ATA_PROT_ATAPI:
	case ATA_PROT_ATAPI_NODATA:
	case ATA_PROT_ATAPI_DMA:
		opts |= AHCI_CMD_ATAPI;
		break;

	default:
		/* do nothing */
		break;
	}

	pp->cmd_slot[0].opts = cpu_to_le32(opts);
	pp->cmd_slot[0].status = 0;
	pp->cmd_slot[0].tbl_addr = cpu_to_le32(pp->cmd_tbl_dma & 0xffffffff);
	pp->cmd_slot[0].tbl_addr_hi = cpu_to_le32((pp->cmd_tbl_dma >> 16) >> 16);

	/*
	 * Fill in command table information.  First, the header,
	 * a SATA Register - Host to Device command FIS.
	 */
	ata_tf_to_fis(&qc->tf, pp->cmd_tbl, 0);

	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;

	ahci_fill_sg(qc);
}

static void ahci_intr_error(struct ata_port *ap, u32 irq_stat)
{
	void *mmio = ap->host_set->mmio_base;
	void *port_mmio = ahci_port_base(mmio, ap->port_no);
	u32 tmp;
	int work;

	/* stop DMA */
	tmp = readl(port_mmio + PORT_CMD);
	tmp &= ~PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);

	/* wait for engine to stop.  TODO: this could be
	 * as long as 500 msec
	 */
	work = 1000;
	while (work-- > 0) {
		tmp = readl(port_mmio + PORT_CMD);
		if ((tmp & PORT_CMD_LIST_ON) == 0)
			break;
		udelay(10);
	}

	/* clear SATA phy error, if any */
	tmp = readl(port_mmio + PORT_SCR_ERR);
	writel(tmp, port_mmio + PORT_SCR_ERR);

	/* if DRQ/BSY is set, device needs to be reset.
	 * if so, issue COMRESET
	 */
	tmp = readl(port_mmio + PORT_TFDATA);
	if (tmp & (ATA_BUSY | ATA_DRQ)) {
		writel(0x301, port_mmio + PORT_SCR_CTL);
		readl(port_mmio + PORT_SCR_CTL); /* flush */
		udelay(10);
		writel(0x300, port_mmio + PORT_SCR_CTL);
		readl(port_mmio + PORT_SCR_CTL); /* flush */
	}

	/* re-start DMA */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD); /* flush */

	printk(KERN_WARNING "ata%u: error occurred, port reset\n", ap->id);
}

static void ahci_eng_timeout(struct ata_port *ap)
{
	void *mmio = ap->host_set->mmio_base;
	void *port_mmio = ahci_port_base(mmio, ap->port_no);
	struct ata_queued_cmd *qc;

	DPRINTK("ENTER\n");

	ahci_intr_error(ap, readl(port_mmio + PORT_IRQ_STAT));

	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (!qc) {
		printk(KERN_ERR "ata%u: BUG: timeout without command\n",
		       ap->id);
	} else {
		/* hack alert!  We cannot use the supplied completion
	 	 * function from inside the ->eh_strategy_handler() thread.
	 	 * libata is the only user of ->eh_strategy_handler() in
	 	 * any kernel, so the default scsi_done() assumes it is
	 	 * not being called from the SCSI EH.
	 	 */
		qc->scsidone = scsi_finish_command;
		ata_qc_complete(qc, ATA_ERR);
	}

}

static inline int ahci_host_intr(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	void *mmio = ap->host_set->mmio_base;
	void *port_mmio = ahci_port_base(mmio, ap->port_no);
	u32 status, serr, ci;

	serr = readl(port_mmio + PORT_SCR_ERR);
	writel(serr, port_mmio + PORT_SCR_ERR);

	status = readl(port_mmio + PORT_IRQ_STAT);
	writel(status, port_mmio + PORT_IRQ_STAT);

	ci = readl(port_mmio + PORT_CMD_ISSUE);
	if (likely((ci & 0x1) == 0)) {
		if (qc) {
			ata_qc_complete(qc, 0);
			qc = NULL;
		}
	}

	if (status & PORT_IRQ_FATAL) {
		ahci_intr_error(ap, status);
		if (qc)
			ata_qc_complete(qc, ATA_ERR);
	}

	return 1;
}

static void ahci_irq_clear(struct ata_port *ap)
{
	/* TODO */
}

static irqreturn_t ahci_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct ahci_host_priv *hpriv;
	unsigned int i, handled = 0;
	void *mmio;
	u32 irq_stat, irq_ack = 0;

	VPRINTK("ENTER\n");

	hpriv = host_set->private_data;
	mmio = host_set->mmio_base;

	/* sigh.  0xffffffff is a valid return from h/w */
	irq_stat = readl(mmio + HOST_IRQ_STAT);
	irq_stat &= hpriv->port_map;
	if (!irq_stat)
		return IRQ_NONE;

        spin_lock(&host_set->lock);

        for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap;
		u32 tmp;

		VPRINTK("port %u\n", i);
		ap = host_set->ports[i];
		tmp = irq_stat & (1 << i);
		if (tmp && ap) {
			struct ata_queued_cmd *qc;
			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (ahci_host_intr(ap, qc))
				irq_ack |= (1 << i);
		}
	}

	if (irq_ack) {
		writel(irq_ack, mmio + HOST_IRQ_STAT);
		handled = 1;
	}

        spin_unlock(&host_set->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static int ahci_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void *port_mmio = (void *) ap->ioaddr.cmd_addr;

	writel(1, port_mmio + PORT_SCR_ACT);
	readl(port_mmio + PORT_SCR_ACT);	/* flush */

	writel(1, port_mmio + PORT_CMD_ISSUE);
	readl(port_mmio + PORT_CMD_ISSUE);	/* flush */

	return 0;
}

static void ahci_setup_port(struct ata_ioports *port, unsigned long base,
			    unsigned int port_idx)
{
	VPRINTK("ENTER, base==0x%lx, port_idx %u\n", base, port_idx);
	base = ahci_port_base_ul(base, port_idx);
	VPRINTK("base now==0x%lx\n", base);

	port->cmd_addr		= base;
	port->scr_addr		= base + PORT_SCR;

	VPRINTK("EXIT\n");
}

static int ahci_host_init(struct ata_probe_ent *probe_ent)
{
	struct ahci_host_priv *hpriv = probe_ent->private_data;
	struct pci_dev *pdev = to_pci_dev(probe_ent->dev);
	void __iomem *mmio = probe_ent->mmio_base;
	u32 tmp, cap_save;
	u16 tmp16;
	unsigned int i, j, using_dac;
	int rc;
	void __iomem *port_mmio;

	cap_save = readl(mmio + HOST_CAP);
	cap_save &= ( (1<<28) | (1<<17) );
	cap_save |= (1 << 27);

	/* global controller reset */
	tmp = readl(mmio + HOST_CTL);
	if ((tmp & HOST_RESET) == 0) {
		writel(tmp | HOST_RESET, mmio + HOST_CTL);
		readl(mmio + HOST_CTL); /* flush */
	}

	/* reset must complete within 1 second, or
	 * the hardware should be considered fried.
	 */
	ssleep(1);

	tmp = readl(mmio + HOST_CTL);
	if (tmp & HOST_RESET) {
		printk(KERN_ERR DRV_NAME "(%s): controller reset failed (0x%x)\n",
			pci_name(pdev), tmp);
		return -EIO;
	}

	writel(HOST_AHCI_EN, mmio + HOST_CTL);
	(void) readl(mmio + HOST_CTL);	/* flush */
	writel(cap_save, mmio + HOST_CAP);
	writel(0xf, mmio + HOST_PORTS_IMPL);
	(void) readl(mmio + HOST_PORTS_IMPL);	/* flush */

	pci_read_config_word(pdev, 0x92, &tmp16);
	tmp16 |= 0xf;
	pci_write_config_word(pdev, 0x92, tmp16);

	hpriv->cap = readl(mmio + HOST_CAP);
	hpriv->port_map = readl(mmio + HOST_PORTS_IMPL);
	probe_ent->n_ports = (hpriv->cap & 0x1f) + 1;

	VPRINTK("cap 0x%x  port_map 0x%x  n_ports %d\n",
		hpriv->cap, hpriv->port_map, probe_ent->n_ports);

	using_dac = hpriv->cap & HOST_CAP_64;
	if (using_dac &&
	    !pci_set_dma_mask(pdev, DMA_64BIT_MASK)) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
		if (rc) {
			rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
			if (rc) {
				printk(KERN_ERR DRV_NAME "(%s): 64-bit DMA enable failed\n",
					pci_name(pdev));
				return rc;
			}
		}

		hpriv->flags |= HOST_CAP_64;
	} else {
		rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc) {
			printk(KERN_ERR DRV_NAME "(%s): 32-bit DMA enable failed\n",
				pci_name(pdev));
			return rc;
		}
		rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc) {
			printk(KERN_ERR DRV_NAME "(%s): 32-bit consistent DMA enable failed\n",
				pci_name(pdev));
			return rc;
		}
	}

	for (i = 0; i < probe_ent->n_ports; i++) {
#if 0 /* BIOSen initialize this incorrectly */
		if (!(hpriv->port_map & (1 << i)))
			continue;
#endif

		port_mmio = ahci_port_base(mmio, i);
		VPRINTK("mmio %p  port_mmio %p\n", mmio, port_mmio);

		ahci_setup_port(&probe_ent->port[i],
				(unsigned long) mmio, i);

		/* make sure port is not active */
		tmp = readl(port_mmio + PORT_CMD);
		VPRINTK("PORT_CMD 0x%x\n", tmp);
		if (tmp & (PORT_CMD_LIST_ON | PORT_CMD_FIS_ON |
			   PORT_CMD_FIS_RX | PORT_CMD_START)) {
			tmp &= ~(PORT_CMD_LIST_ON | PORT_CMD_FIS_ON |
				 PORT_CMD_FIS_RX | PORT_CMD_START);
			writel(tmp, port_mmio + PORT_CMD);
			readl(port_mmio + PORT_CMD); /* flush */

			/* spec says 500 msecs for each bit, so
			 * this is slightly incorrect.
			 */
			msleep(500);
		}

		writel(PORT_CMD_SPIN_UP, port_mmio + PORT_CMD);

		j = 0;
		while (j < 100) {
			msleep(10);
			tmp = readl(port_mmio + PORT_SCR_STAT);
			if ((tmp & 0xf) == 0x3)
				break;
			j++;
		}

		tmp = readl(port_mmio + PORT_SCR_ERR);
		VPRINTK("PORT_SCR_ERR 0x%x\n", tmp);
		writel(tmp, port_mmio + PORT_SCR_ERR);

		/* ack any pending irq events for this port */
		tmp = readl(port_mmio + PORT_IRQ_STAT);
		VPRINTK("PORT_IRQ_STAT 0x%x\n", tmp);
		if (tmp)
			writel(tmp, port_mmio + PORT_IRQ_STAT);

		writel(1 << i, mmio + HOST_IRQ_STAT);

		/* set irq mask (enables interrupts) */
		writel(DEF_PORT_IRQ, port_mmio + PORT_IRQ_MASK);
	}

	tmp = readl(mmio + HOST_CTL);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
	writel(tmp | HOST_IRQ_EN, mmio + HOST_CTL);
	tmp = readl(mmio + HOST_CTL);
	VPRINTK("HOST_CTL 0x%x\n", tmp);

	pci_set_master(pdev);

	return 0;
}

/* move to PCI layer, integrate w/ MSI stuff */
static void pci_enable_intx(struct pci_dev *pdev)
{
	u16 pci_command;

	pci_read_config_word(pdev, PCI_COMMAND, &pci_command);
	if (pci_command & PCI_COMMAND_INTX_DISABLE) {
		pci_command &= ~PCI_COMMAND_INTX_DISABLE;
		pci_write_config_word(pdev, PCI_COMMAND, pci_command);
	}
}

static void ahci_print_info(struct ata_probe_ent *probe_ent)
{
	struct ahci_host_priv *hpriv = probe_ent->private_data;
	struct pci_dev *pdev = to_pci_dev(probe_ent->dev);
	void *mmio = probe_ent->mmio_base;
	u32 vers, cap, impl, speed;
	const char *speed_s;
	u16 cc;
	const char *scc_s;

	vers = readl(mmio + HOST_VERSION);
	cap = hpriv->cap;
	impl = hpriv->port_map;

	speed = (cap >> 20) & 0xf;
	if (speed == 1)
		speed_s = "1.5";
	else if (speed == 2)
		speed_s = "3";
	else
		speed_s = "?";

	pci_read_config_word(pdev, 0x0a, &cc);
	if (cc == 0x0101)
		scc_s = "IDE";
	else if (cc == 0x0106)
		scc_s = "SATA";
	else if (cc == 0x0104)
		scc_s = "RAID";
	else
		scc_s = "unknown";

	printk(KERN_INFO DRV_NAME "(%s) AHCI %02x%02x.%02x%02x "
		"%u slots %u ports %s Gbps 0x%x impl %s mode\n"
	       	,
	       	pci_name(pdev),

	       	(vers >> 24) & 0xff,
	       	(vers >> 16) & 0xff,
	       	(vers >> 8) & 0xff,
	       	vers & 0xff,

		((cap >> 8) & 0x1f) + 1,
		(cap & 0x1f) + 1,
		speed_s,
		impl,
		scc_s);

	printk(KERN_INFO DRV_NAME "(%s) flags: "
	       	"%s%s%s%s%s%s"
	       	"%s%s%s%s%s%s%s\n"
	       	,
	       	pci_name(pdev),

		cap & (1 << 31) ? "64bit " : "",
		cap & (1 << 30) ? "ncq " : "",
		cap & (1 << 28) ? "ilck " : "",
		cap & (1 << 27) ? "stag " : "",
		cap & (1 << 26) ? "pm " : "",
		cap & (1 << 25) ? "led " : "",

		cap & (1 << 24) ? "clo " : "",
		cap & (1 << 19) ? "nz " : "",
		cap & (1 << 18) ? "only " : "",
		cap & (1 << 17) ? "pmp " : "",
		cap & (1 << 15) ? "pio " : "",
		cap & (1 << 14) ? "slum " : "",
		cap & (1 << 13) ? "part " : ""
		);
}

static int ahci_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	struct ahci_host_priv *hpriv;
	unsigned long base;
	void *mmio_base;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	int pci_dev_busy = 0;
	int rc;

	VPRINTK("ENTER\n");

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	pci_enable_intx(pdev);

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	mmio_base = ioremap(pci_resource_start(pdev, AHCI_PCI_BAR),
		            pci_resource_len(pdev, AHCI_PCI_BAR));
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}
	base = (unsigned long) mmio_base;

	hpriv = kmalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		rc = -ENOMEM;
		goto err_out_iounmap;
	}
	memset(hpriv, 0, sizeof(*hpriv));

	probe_ent->sht		= ahci_port_info[board_idx].sht;
	probe_ent->host_flags	= ahci_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= ahci_port_info[board_idx].pio_mask;
	probe_ent->udma_mask	= ahci_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= ahci_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;
	probe_ent->private_data = hpriv;

	/* initialize adapter */
	rc = ahci_host_init(probe_ent);
	if (rc)
		goto err_out_hpriv;

	ahci_print_info(probe_ent);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_hpriv:
	kfree(hpriv);
err_out_iounmap:
	iounmap(mmio_base);
err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
	return rc;
}


static int __init ahci_init(void)
{
	return pci_module_init(&ahci_pci_driver);
}


static void __exit ahci_exit(void)
{
	pci_unregister_driver(&ahci_pci_driver);
}


MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("AHCI SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ahci_pci_tbl);

module_init(ahci_init);
module_exit(ahci_exit);

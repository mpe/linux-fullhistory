/*
 *  sata_uli.c - ULi Electronics SATA
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"sata_uli"
#define DRV_VERSION	"0.5"

enum {
	uli_5289		= 0,
	uli_5287		= 1,
	uli_5281		= 2,

	/* PCI configuration registers */
	ULI5287_BASE		= 0x90, /* sata0 phy SCR registers */
	ULI5287_OFFS		= 0x10, /* offset from sata0->sata1 phy regs */
	ULI5281_BASE		= 0x60, /* sata0 phy SCR  registers */
	ULI5281_OFFS		= 0x60, /* offset from sata0->sata1 phy regs */
};

static int uli_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static u32 uli_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void uli_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);

static struct pci_device_id uli_pci_tbl[] = {
	{ PCI_VENDOR_ID_AL, 0x5289, PCI_ANY_ID, PCI_ANY_ID, 0, 0, uli_5289 },
	{ PCI_VENDOR_ID_AL, 0x5287, PCI_ANY_ID, PCI_ANY_ID, 0, 0, uli_5287 },
	{ PCI_VENDOR_ID_AL, 0x5281, PCI_ANY_ID, PCI_ANY_ID, 0, 0, uli_5281 },
	{ }	/* terminate list */
};


static struct pci_driver uli_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= uli_pci_tbl,
	.probe			= uli_init_one,
	.remove			= ata_pci_remove_one,
};

static Scsi_Host_Template uli_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations uli_ops = {
	.port_disable		= ata_port_disable,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.phy_reset		= sata_phy_reset,

	.bmdma_setup            = ata_bmdma_setup,
	.bmdma_start            = ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,

	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,

	.scr_read		= uli_scr_read,
	.scr_write		= uli_scr_write,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
};

static struct ata_port_info uli_port_info = {
	.sht            = &uli_sht,
	.host_flags     = ATA_FLAG_SATA | ATA_FLAG_SATA_RESET |
			  ATA_FLAG_NO_LEGACY,
	.pio_mask       = 0x03,		//support pio mode 4 (FIXME)
	.udma_mask      = 0x7f,		//support udma mode 6
	.port_ops       = &uli_ops,
};


MODULE_AUTHOR("Peer Chen");
MODULE_DESCRIPTION("low-level driver for ULi Electronics SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, uli_pci_tbl);
MODULE_VERSION(DRV_VERSION);

static unsigned int get_scr_cfg_addr(struct ata_port *ap, unsigned int sc_reg)
{
	return ap->ioaddr.scr_addr + (4 * sc_reg);
}

static u32 uli_scr_cfg_read (struct ata_port *ap, unsigned int sc_reg)
{
	struct pci_dev *pdev = to_pci_dev(ap->host_set->dev);
	unsigned int cfg_addr = get_scr_cfg_addr(ap, sc_reg);
	u32 val;

	pci_read_config_dword(pdev, cfg_addr, &val);
	return val;
}

static void uli_scr_cfg_write (struct ata_port *ap, unsigned int scr, u32 val)
{
	struct pci_dev *pdev = to_pci_dev(ap->host_set->dev);
	unsigned int cfg_addr = get_scr_cfg_addr(ap, scr);

	pci_write_config_dword(pdev, cfg_addr, val);
}

static u32 uli_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;

	return uli_scr_cfg_read(ap, sc_reg);
}

static void uli_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)	//SCR_CONTROL=2, SCR_ERROR=1, SCR_STATUS=0
		return;

	uli_scr_cfg_write(ap, sc_reg, val);
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

static int uli_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct ata_probe_ent *probe_ent;
	struct ata_port_info *ppi;
	int rc;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	int pci_dev_busy = 0;

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	ppi = &uli_port_info;
	probe_ent = ata_pci_init_native_mode(pdev, &ppi);
	if (!probe_ent) {
		rc = -ENOMEM;
		goto err_out_regions;
	}
	
	switch (board_idx) {
	case uli_5287:
		probe_ent->port[0].scr_addr = ULI5287_BASE;
		probe_ent->port[1].scr_addr = ULI5287_BASE + ULI5287_OFFS;
       		probe_ent->n_ports = 4;

       		probe_ent->port[2].cmd_addr = pci_resource_start(pdev, 0) + 8;
		probe_ent->port[2].altstatus_addr =
		probe_ent->port[2].ctl_addr =
			(pci_resource_start(pdev, 1) | ATA_PCI_CTL_OFS) + 4;
		probe_ent->port[2].bmdma_addr = pci_resource_start(pdev, 4) + 16;
		probe_ent->port[2].scr_addr = ULI5287_BASE + ULI5287_OFFS*4;

		probe_ent->port[3].cmd_addr = pci_resource_start(pdev, 2) + 8;
		probe_ent->port[3].altstatus_addr =
		probe_ent->port[3].ctl_addr =
			(pci_resource_start(pdev, 3) | ATA_PCI_CTL_OFS) + 4;
		probe_ent->port[3].bmdma_addr = pci_resource_start(pdev, 4) + 24;
		probe_ent->port[3].scr_addr = ULI5287_BASE + ULI5287_OFFS*5;

		ata_std_ports(&probe_ent->port[2]);
		ata_std_ports(&probe_ent->port[3]);
		break;

	case uli_5289:
		probe_ent->port[0].scr_addr = ULI5287_BASE;
		probe_ent->port[1].scr_addr = ULI5287_BASE + ULI5287_OFFS;
		break;

	case uli_5281:
		probe_ent->port[0].scr_addr = ULI5281_BASE;
		probe_ent->port[1].scr_addr = ULI5281_BASE + ULI5281_OFFS;
		break;

	default:
		BUG();
		break;
	}

	pci_set_master(pdev);
	pci_enable_intx(pdev);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_regions:
	pci_release_regions(pdev);

err_out:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
	return rc;

}

static int __init uli_init(void)
{
	return pci_module_init(&uli_pci_driver);
}

static void __exit uli_exit(void)
{
	pci_unregister_driver(&uli_pci_driver);
}


module_init(uli_init);
module_exit(uli_exit);

/*
 * BK Id: SCCS/s.chrp_pci.c 1.22 09/08/01 15:47:42 paulus
 */
/*
 * CHRP pci routines.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/prom.h>
#include <asm/gg2.h>
#include <asm/machdep.h>
#include <asm/sections.h>
#include <asm/pci-bridge.h>

#include "open_pic.h"
#include "pci.h"

/* LongTrail */
unsigned long gg2_pci_config_base;

#define pci_config_addr(dev, offset) \
(gg2_pci_config_base | ((dev->bus->number)<<16) | ((dev->devfn)<<8) | (offset))

volatile struct Hydra *Hydra = NULL;

/*
 * The VLSI Golden Gate II has only 512K of PCI configuration space, so we
 * limit the bus number to 3 bits
 */

#define cfg_read(val, addr, type, op)	*val = op((type)(addr))
#define cfg_write(val, addr, type, op)	op((type *)(addr), (val))

#define cfg_read_bad(val, size)		*val = bad_##size;
#define cfg_write_bad(val, size)

#define bad_byte	0xff
#define bad_word	0xffff
#define bad_dword	0xffffffffU

#define GG2_PCI_OP(rw, size, type, op)					    \
int __chrp gg2_##rw##_config_##size(struct pci_dev *dev, int off, type val) \
{									    \
	if (dev->bus->number > 7) {					    \
		cfg_##rw##_bad(val, size)				    \
		return PCIBIOS_DEVICE_NOT_FOUND;			    \
	}								    \
	cfg_##rw(val, pci_config_addr(dev, off), type, op);		    \
	return PCIBIOS_SUCCESSFUL;					    \
}

GG2_PCI_OP(read, byte, u8 *, in_8)
GG2_PCI_OP(read, word, u16 *, in_le16)
GG2_PCI_OP(read, dword, u32 *, in_le32)
GG2_PCI_OP(write, byte, u8, out_8)
GG2_PCI_OP(write, word, u16, out_le16)
GG2_PCI_OP(write, dword, u32, out_le32)

static struct pci_ops gg2_pci_ops =
{
	gg2_read_config_byte,
	gg2_read_config_word,
	gg2_read_config_dword,
	gg2_write_config_byte,
	gg2_write_config_word,
	gg2_write_config_dword
};

/*
 * Access functions for PCI config space on IBM "python" host bridges.
 */
#define PYTHON_CFA(b, d, o)	(0x80 | ((b) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))

#define PYTHON_PCI_OP(rw, size, type, op, mask)			    	     \
int __chrp								     \
python_##rw##_config_##size(struct pci_dev *dev, int offset, type val) 	     \
{									     \
	struct pci_controller *hose = dev->sysdata;			     \
									     \
	out_be32(hose->cfg_addr,					     \
		 PYTHON_CFA(dev->bus->number, dev->devfn, offset));	     \
	cfg_##rw(val, hose->cfg_data + (offset & mask), type, op);   	     \
	return PCIBIOS_SUCCESSFUL;					     \
}

PYTHON_PCI_OP(read, byte, u8 *, in_8, 3)
PYTHON_PCI_OP(read, word, u16 *, in_le16, 2)
PYTHON_PCI_OP(read, dword, u32 *, in_le32, 0)
PYTHON_PCI_OP(write, byte, u8, out_8, 3)
PYTHON_PCI_OP(write, word, u16, out_le16, 2)
PYTHON_PCI_OP(write, dword, u32, out_le32, 0)

static struct pci_ops python_pci_ops =
{
	python_read_config_byte,
	python_read_config_word,
	python_read_config_dword,
	python_write_config_byte,
	python_write_config_word,
	python_write_config_dword
};

/*
 * Access functions for PCI config space using RTAS calls.
 */
#define RTAS_PCI_READ_OP(size, type, nbytes)			    	  \
int __chrp								  \
rtas_read_config_##size(struct pci_dev *dev, int offset, type val) 	  \
{									  \
	unsigned long addr = (offset & 0xff) | ((dev->devfn & 0xff) << 8) \
		| ((dev->bus->number & 0xff) << 16);			  \
	unsigned long ret = ~0UL;					  \
	int rval;							  \
									  \
	rval = call_rtas("read-pci-config", 2, 2, &ret, addr, nbytes);	  \
	*val = ret;							  \
	return rval? PCIBIOS_DEVICE_NOT_FOUND: PCIBIOS_SUCCESSFUL;    	  \
}

#define RTAS_PCI_WRITE_OP(size, type, nbytes)				  \
int __chrp								  \
rtas_write_config_##size(struct pci_dev *dev, int offset, type val)	  \
{									  \
	unsigned long addr = (offset & 0xff) | ((dev->devfn & 0xff) << 8) \
		| ((dev->bus->number & 0xff) << 16);			  \
	int rval;							  \
									  \
	rval = call_rtas("write-pci-config", 3, 1, NULL,		  \
			 addr, nbytes, (ulong)val);			  \
	return rval? PCIBIOS_DEVICE_NOT_FOUND: PCIBIOS_SUCCESSFUL;	  \
}

RTAS_PCI_READ_OP(byte, u8 *, 1)
RTAS_PCI_READ_OP(word, u16 *, 2)
RTAS_PCI_READ_OP(dword, u32 *, 4)
RTAS_PCI_WRITE_OP(byte, u8, 1)
RTAS_PCI_WRITE_OP(word, u16, 2)
RTAS_PCI_WRITE_OP(dword, u32, 4)

static struct pci_ops rtas_pci_ops =
{
	rtas_read_config_byte,
	rtas_read_config_word,
	rtas_read_config_dword,
	rtas_write_config_byte,
	rtas_write_config_word,
	rtas_write_config_dword
};

    /*
     *  Temporary fixes for PCI devices. These should be replaced by OF query
     *  code -- Geert
     */

static u_char hydra_openpic_initsenses[] __initdata = {
    1,	/* HYDRA_INT_SIO */
    0,	/* HYDRA_INT_SCSI_DMA */
    0,	/* HYDRA_INT_SCCA_TX_DMA */
    0,	/* HYDRA_INT_SCCA_RX_DMA */
    0,	/* HYDRA_INT_SCCB_TX_DMA */
    0,	/* HYDRA_INT_SCCB_RX_DMA */
    1,	/* HYDRA_INT_SCSI */
    1,	/* HYDRA_INT_SCCA */
    1,	/* HYDRA_INT_SCCB */
    1,	/* HYDRA_INT_VIA */
    1,	/* HYDRA_INT_ADB */
    0,	/* HYDRA_INT_ADB_NMI */
    	/* all others are 1 (= default) */
};

int __init
hydra_init(void)
{
	struct device_node *np;

	np = find_devices("mac-io");
	if (np == NULL || np->n_addrs == 0) {
		printk(KERN_WARNING "Warning: no mac-io found\n");
		return 0;
	}
	Hydra = ioremap(np->addrs[0].address, np->addrs[0].size);
	printk("Hydra Mac I/O at %x\n", np->addrs[0].address);
	out_le32(&Hydra->Feature_Control, (HYDRA_FC_SCC_CELL_EN |
					   HYDRA_FC_SCSI_CELL_EN |
					   HYDRA_FC_SCCA_ENABLE |
					   HYDRA_FC_SCCB_ENABLE |
					   HYDRA_FC_ARB_BYPASS |
					   HYDRA_FC_MPIC_ENABLE |
					   HYDRA_FC_SLOW_SCC_PCLK |
					   HYDRA_FC_MPIC_IS_MASTER));
	OpenPIC_Addr = &Hydra->OpenPIC;
	OpenPIC_InitSenses = hydra_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(hydra_openpic_initsenses);
	return 1;
}

void __init
chrp_pcibios_fixup(void)
{
	struct pci_dev *dev;
	struct device_node *np;

	/* PCI interrupts are controlled by the OpenPIC */
	pci_for_each_dev(dev) {
		np = pci_device_to_OF_node(dev);
		if ((np != 0) && (np->n_intrs > 0) && (np->intrs[0].line != 0))
			dev->irq = np->intrs[0].line;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}
}

void __init
chrp_find_bridges(void)
{
	struct device_node *dev;
	int *bus_range;
	int len, index = -1;
	struct pci_controller *hose;
	volatile unsigned char *cfg;
	unsigned int *dma;
	char *model, *machine;
	int is_longtrail = 0, is_mot = 0;
	struct device_node *root = find_path_device("/");
#ifdef CONFIG_POWER3
	unsigned int *opprop = (unsigned int *)
		get_property(root, "platform-open-pic", NULL);
	int i;
#endif

	/*
	 * The PCI host bridge nodes on some machines don't have
	 * properties to adequately identify them, so we have to
	 * look at what sort of machine this is as well.
	 */
	machine = get_property(root, "model", NULL);
	if (machine != NULL) {
		is_longtrail = strncmp(machine, "IBM,LongTrail", 13) == 0;
		is_mot = strncmp(machine, "MOT", 3) == 0;
	}
	for (dev = root->child; dev != NULL; dev = dev->sibling) {
		if (dev->type == NULL || strcmp(dev->type, "pci") != 0)
			continue;
		++index;
		/* The GG2 bridge on the LongTrail doesn't have an address */
		if (dev->n_addrs < 1 && !is_longtrail) {
			printk(KERN_WARNING "Can't use %s: no address\n",
			       dev->full_name);
			continue;
		}
		bus_range = (int *) get_property(dev, "bus-range", &len);
		if (bus_range == NULL || len < 2 * sizeof(int)) {
			printk(KERN_WARNING "Can't get bus-range for %s\n",
				dev->full_name);
			continue;
		}
		if (bus_range[1] == bus_range[0])
			printk(KERN_INFO "PCI bus %d", bus_range[0]);
		else
			printk(KERN_INFO "PCI buses %d..%d",
			       bus_range[0], bus_range[1]);
		printk(" controlled by %s", dev->type);
		if (dev->n_addrs > 0)
			printk(" at %x", dev->addrs[0].address);
		printk("\n");

		hose = pcibios_alloc_controller();
		if (!hose) {
			printk("Can't allocate PCI controller structure for %s\n",
				dev->full_name);
			continue;
		}
		hose->arch_data = dev;
		hose->first_busno = bus_range[0];
		hose->last_busno = bus_range[1];

		model = get_property(dev, "model", NULL);
		if (model == NULL)
			model = "<none>";
		if (device_is_compatible(dev, "IBM,python")) {
			hose->ops = &python_pci_ops;
			cfg = ioremap(dev->addrs[0].address + 0xf8000, 0x20);
			hose->cfg_addr = (volatile unsigned int *) cfg;
			hose->cfg_data = cfg + 0x10;
		} else if (is_mot
			   || strncmp(model, "Motorola, Grackle", 17) == 0) {
			setup_grackle(hose);
		} else if (is_longtrail) {
			hose->ops = &gg2_pci_ops;
			gg2_pci_config_base = (unsigned long)
				ioremap(GG2_PCI_CONFIG_BASE, 0x80000);
		} else {
			printk("No methods for %s (model %s), using RTAS\n",
			       dev->full_name, model);
			hose->ops = &rtas_pci_ops;
		}

		pci_process_bridge_OF_ranges(hose, dev, index == 0);

#ifdef CONFIG_POWER3
		if (opprop != NULL) {
			i = prom_n_addr_cells(root) * (index + 2) - 1;
			openpic_setup_ISU(index, opprop[i]);
		}
#endif /* CONFIG_POWER3 */

		/* check the first bridge for a property that we can
		   use to set pci_dram_offset */
		dma = (unsigned int *)
			get_property(dev, "ibm,dma-ranges", &len);
		if (index == 0 && dma != NULL && len >= 6 * sizeof(*dma)) {
			pci_dram_offset = dma[2] - dma[3];
			printk("pci_dram_offset = %lx\n", pci_dram_offset);
		}
	}

	ppc_md.pcibios_fixup = chrp_pcibios_fixup;
}

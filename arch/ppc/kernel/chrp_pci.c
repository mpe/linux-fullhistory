/*
 * CHRP pci routines.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/openpic.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hydra.h>

/* LongTrail */
#define pci_config_addr(bus, dev, offset) \
	(0xfec00000 | ((bus)<<16) | ((dev)<<8) | (offset))

int chrp_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned char *val)
{
    if (bus > 7) {
	*val = 0xff;
	return PCIBIOS_DEVICE_NOT_FOUND;
    }
    *val = in_8((unsigned char *)pci_config_addr(bus, dev_fn, offset));
    if (offset == PCI_INTERRUPT_LINE) {
	/* PCI interrupts are controlled by the OpenPIC */
	if (*val)
	    *val = openpic_to_irq(*val);
    }
    return PCIBIOS_SUCCESSFUL;
}

int chrp_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned short *val)
{
    if (bus > 7) {
	*val = 0xffff;
	return PCIBIOS_DEVICE_NOT_FOUND;
    }
    *val = in_le16((unsigned short *)pci_config_addr(bus, dev_fn, offset));
    return PCIBIOS_SUCCESSFUL;
}

int chrp_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned int *val)
{
    if (bus > 7) {
	*val = 0xffffffff;
	return PCIBIOS_DEVICE_NOT_FOUND;
    }
    *val = in_le32((unsigned int *)pci_config_addr(bus, dev_fn, offset));
    return PCIBIOS_SUCCESSFUL;
}

int chrp_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned char val)
{
    if (bus > 7)
	return PCIBIOS_DEVICE_NOT_FOUND;
    out_8((unsigned char *)pci_config_addr(bus, dev_fn, offset), val);
    return PCIBIOS_SUCCESSFUL;
}

int chrp_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned short val)
{
    if (bus > 7)
	return PCIBIOS_DEVICE_NOT_FOUND;
    out_le16((unsigned short *)pci_config_addr(bus, dev_fn, offset), val);
    return PCIBIOS_SUCCESSFUL;
}

int chrp_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned int val)
{
    if (bus > 7)
	return PCIBIOS_DEVICE_NOT_FOUND;
    out_le32((unsigned int *)pci_config_addr(bus, dev_fn, offset), val);
    return PCIBIOS_SUCCESSFUL;
}

int chrp_pcibios_find_device(unsigned short vendor, unsigned short dev_id,
			     unsigned short index, unsigned char *bus_ptr,
			     unsigned char *dev_fn_ptr)
{
    int num, devfn;
    unsigned int x, vendev;

    if (vendor == 0xffff)
	return PCIBIOS_BAD_VENDOR_ID;
    vendev = (dev_id << 16) + vendor;
    num = 0;
    for (devfn = 0;  devfn < 32;  devfn++) {
	chrp_pcibios_read_config_dword(0, devfn<<3, PCI_VENDOR_ID, &x);
	if (x == vendev) {
	    if (index == num) {
		*bus_ptr = 0;
		*dev_fn_ptr = devfn<<3;
		return PCIBIOS_SUCCESSFUL;
	    }
	    ++num;
	}
    }
    return PCIBIOS_DEVICE_NOT_FOUND;
}

int chrp_pcibios_find_class(unsigned int class_code, unsigned short index,
			    unsigned char *bus_ptr, unsigned char *dev_fn_ptr)
{
    int devnr, x, num;

    num = 0;
    for (devnr = 0;  devnr < 32;  devnr++) {
	chrp_pcibios_read_config_dword(0, devnr<<3, PCI_CLASS_REVISION, &x);
	if ((x>>8) == class_code) {
	    if (index == num) {
		*bus_ptr = 0;
		*dev_fn_ptr = devnr<<3;
		return PCIBIOS_SUCCESSFUL;
	    }
	    ++num;
	}
    }
    return PCIBIOS_DEVICE_NOT_FOUND;
}

__initfunc(volatile struct Hydra *find_hydra(void))
{
    u_char bus, dev;
    volatile struct Hydra *hydra = 0;
    if (chrp_pcibios_find_device(PCI_VENDOR_ID_APPLE,
				 PCI_DEVICE_ID_APPLE_HYDRA, 0, &bus, &dev)
	== PCIBIOS_SUCCESSFUL)
	chrp_pcibios_read_config_dword(bus, dev, PCI_BASE_ADDRESS_0,
				       (unsigned int *)&hydra);
    return hydra;
}

__initfunc(void hydra_post_openpic_init(void))
{
    openpic_set_sense(HYDRA_INT_SCSI_DMA, 0);
    openpic_set_sense(HYDRA_INT_SCCA_TX_DMA, 0);
    openpic_set_sense(HYDRA_INT_SCCA_RX_DMA, 0);
    openpic_set_sense(HYDRA_INT_SCCB_TX_DMA, 0);
    openpic_set_sense(HYDRA_INT_SCCB_RX_DMA, 0);
    openpic_set_sense(HYDRA_INT_SCSI, 1);
    openpic_set_sense(HYDRA_INT_SCCA, 1);
    openpic_set_sense(HYDRA_INT_SCCB, 1);
    openpic_set_sense(HYDRA_INT_VIA, 1);
    openpic_set_sense(HYDRA_INT_ADB, 1);
    openpic_set_sense(HYDRA_INT_ADB_NMI, 0);
}

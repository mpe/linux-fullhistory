/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SNI specific PCI support for RM200/RM300.
 */
#include <linux/config.h>
#include <linux/bios32.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/pci.h>
#include <asm/sni.h>

#ifdef CONFIG_PCI

extern inline u32 mkaddr(unsigned char bus, unsigned char dev_fn,
                         unsigned char where)
{
	return ((bus    & 0xff) << 0x10) |
	       ((dev_fn & 0xff) << 0x08) |
	       (where  & 0xfc);
}

static unsigned long sni_rm200_pcibios_fixup (unsigned long memory_start,
                                              unsigned long memory_end)
{
	/*
	 * TODO: Fix PCI_INTERRUPT_LINE register for onboard cards.
	 * Take care of RM300 revision D boards for where the network
	 * slot became an ordinary PCI slot.
	 */
	return memory_start;
}

/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int sni_rm200_pcibios_read_config_byte (unsigned char bus,
                                               unsigned char dev_fn,
                                               unsigned char where,
                                               unsigned char *val)
{
	u32 res;

	*(volatile u32 *)PCIMT_CONFIG_ADDRESS = mkaddr(bus, dev_fn, where);
	res = *(volatile u32 *)PCIMT_CONFIG_DATA;
	res = (le32_to_cpu(res) >> ((where & 3) << 3)) & 0xff;
	*val = res;

	return PCIBIOS_SUCCESSFUL;
}

static int sni_rm200_pcibios_read_config_word (unsigned char bus,
                                               unsigned char dev_fn,
                                               unsigned char where,
                                               unsigned short *val)
{
	u32 res;

	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	*(volatile u32 *)PCIMT_CONFIG_ADDRESS = mkaddr(bus, dev_fn, where);
	res = *(volatile u32 *)PCIMT_CONFIG_DATA;
	res = (le32_to_cpu(res) >> ((where & 3) << 3)) & 0xffff;
	*val = res;

	return PCIBIOS_SUCCESSFUL;
}

static int sni_rm200_pcibios_read_config_dword (unsigned char bus,
                                                unsigned char dev_fn,
                                                unsigned char where,
                                                unsigned int *val)
{
	u32 res;

	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	*(volatile u32 *)PCIMT_CONFIG_ADDRESS = mkaddr(bus, dev_fn, where);
	res = *(volatile u32 *)PCIMT_CONFIG_DATA;
	res = le32_to_cpu(res);
	*val = res;

	return PCIBIOS_SUCCESSFUL;
}

static int sni_rm200_pcibios_write_config_byte (unsigned char bus,
                                                unsigned char dev_fn,
                                                unsigned char where,
                                                unsigned char val)
{
	/* To do */
	return PCIBIOS_SUCCESSFUL;
}

static int sni_rm200_pcibios_write_config_word (unsigned char bus,
                                                unsigned char dev_fn,
                                                unsigned char where,
                                                unsigned short val)
{
	/* To do */
	return PCIBIOS_SUCCESSFUL;
}

static int sni_rm200_pcibios_write_config_dword (unsigned char bus,
                                                 unsigned char dev_fn,
                                                 unsigned char where,
                                                 unsigned int val)
{
	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	*(volatile u32 *)PCIMT_CONFIG_ADDRESS = mkaddr(bus, dev_fn, where);
	*(volatile u32 *)PCIMT_CONFIG_DATA = le32_to_cpu(val);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops sni_pci_ops = {
	sni_rm200_pcibios_fixup,
	sni_rm200_pcibios_read_config_byte,
	sni_rm200_pcibios_read_config_word,
	sni_rm200_pcibios_read_config_dword,
	sni_rm200_pcibios_write_config_byte,
	sni_rm200_pcibios_write_config_word,
	sni_rm200_pcibios_write_config_dword
};

#endif /* CONFIG_PCI */

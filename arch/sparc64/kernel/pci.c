/* $Id: pci.c,v 1.1 1999/08/30 10:00:42 davem Exp $
 * pci.c: UltraSparc PCI controller support.
 *
 * Copyright (C) 1997, 1998, 1999 David S. Miller (davem@redhat.com)
 * Copyright (C) 1998, 1999 Eddie C. Dost   (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pbm.h>
#include <asm/irq.h>
#include <asm/ebus.h>

/* This is the base address of IOMMU translated space
 * on PCI bus segments.  This means that any PCI address
 * greater than or equal this value will be translated
 * into a physical address and transferred on the system
 * bus.  Any address less than it will be considered a
 * PCI peer-to-peer transaction.  For example, given a
 * value of 0x80000000:
 *
 *	If a PCI device bus masters the address 0x80001000
 *	then then PCI controller's IOMMU will take the offset
 *	portion (0x1000) and translate that into a physical
 *	address.  The data transfer will be performed to/from
 *	that physical address on the system bus.
 *
 *	If the PCI device bus masters the address 0x00002000
 *	then the PCI controller will not do anything, it will
 *	proceed as a PCI peer-to-peer transfer.
 *
 * This all stems from the fact that PCI drivers in Linux are
 * not conscious of DMA mappings to the extent they need to
 * be for systems like Sparc64 and Alpha which have IOMMU's.
 * Plans are already in the works to fix this.  So what we do
 * at the moment is linearly map all of physical ram with the
 * IOMMU in consistant mode, thus providing the illusion of
 * a simplistic linear DMA mapping scheme to the drivers.
 * This, while it works, is bad for performance (streaming
 * DMA is not used) and limits the amount of total memory we
 * can support (2GB on Psycho, 512MB on Sabre/APB).
 */
unsigned long pci_dvma_offset = 0x00000000UL;

/* Given a valid PCI dma address (ie. >= pci_dvma_addset) this
 * mask will give you the offset into the DMA space of a
 * PCI bus.
 */
unsigned long pci_dvma_mask   = 0xffffffffUL;

unsigned long pci_dvma_v2p_hash[PCI_DVMA_HASHSZ];
unsigned long pci_dvma_p2v_hash[PCI_DVMA_HASHSZ];

#ifndef CONFIG_PCI
/* A "nop" PCI implementation. */
int pcibios_present(void) { return 0; }
asmlinkage int sys_pciconfig_read(unsigned long bus, unsigned long dfn,
				  unsigned long off, unsigned long len,
				  unsigned char *buf)
{
	return 0;
}
asmlinkage int sys_pciconfig_write(unsigned long bus, unsigned long dfn,
				   unsigned long off, unsigned long len,
				   unsigned char *buf)
{
	return 0;
}
#else

/* List of all PCI controllers found in the system. */
spinlock_t pci_controller_lock = SPIN_LOCK_UNLOCKED;
struct pci_controller_info *pci_controller_root = NULL;

/* Each PCI controller found gets a unique index. */
int pci_num_controllers = 0;

/* Given an 8-bit PCI bus number, this yields the
 * controlling PBM module info.
 *
 * Some explanation is in order here.  The Linux APIs for
 * the PCI subsystem require that the configuration space
 * types are enough to signify PCI configuration space
 * accesses correctly.  This gives us 8-bits for the bus
 * number, however we have multiple PCI controllers on
 * UltraSparc systems.
 *
 * So what we do is give the PCI busses under each controller
 * a unique portion of the 8-bit PCI bus number space.
 * Therefore one can obtain the controller from the bus
 * number.  For example, say PSYCHO PBM-A a subordinate bus
 * space of 0 to 4, and PBM-B has a space of 0 to 2.  PBM-A
 * will use 0 to 4, and PBM-B will use 5 to 7.
 */
struct pci_pbm_info *pci_bus2pbm[256];
unsigned char pci_highest_busnum = 0;

/* At boot time the user can give the kernel a command
 * line option which controls if and how PCI devices
 * are reordered at PCI bus probing time.
 */
int pci_device_reorder = 0;

spinlock_t pci_poke_lock = SPIN_LOCK_UNLOCKED;
volatile int pci_poke_in_progress;
volatile int pci_poke_faulted;

/* Probe for all PCI controllers in the system. */
extern void sabre_init(int);
extern void psycho_init(int);

static struct {
	char *model_name;
	void (*init)(int);
} pci_controller_table[] = {
	{ "SUNW,sabre", sabre_init },
	{ "SUNW,psycho", psycho_init }
};
#define PCI_NUM_CONTROLLER_TYPES (sizeof(pci_controller_table) / \
				  sizeof(pci_controller_table[0]))

static void pci_controller_init(char *model_name, int namelen, int node)
{
	int i;

	for (i = 0; i < PCI_NUM_CONTROLLER_TYPES; i++) {
		if (!strncmp(model_name,
			     pci_controller_table[i].model_name,
			     namelen)) {
			pci_controller_table[i].init(node);
			return;
		}
	}
	printk("PCI: Warning unknown controller, model name [%s]\n",
	       model_name);
	printk("PCI: Ignoring controller...\n");
}

/* Find each controller in the system, attach and initialize
 * software state structure for each and link into the
 * pci_controller_root.  Setup the controller enough such
 * that bus scanning can be done.
 */
static void pci_controller_probe(void)
{
	char namebuf[16];
	int node;

	printk("PCI: Probing for controllers.\n");
	node = prom_getchild(prom_root_node);
	while ((node = prom_searchsiblings(node, "pci")) != 0) {
		int len;

		len = prom_getproperty(node, "model",
				       namebuf, sizeof(namebuf));
		if (len > 0)
			pci_controller_init(namebuf, len, node);
		node = prom_getsibling(node);
		if (!node)
			break;
	}
}

static void pci_scan_each_controller_bus(void)
{
	struct pci_controller_info *p;
	unsigned long flags;

	spin_lock_irqsave(&pci_controller_lock, flags);
	for (p = pci_controller_root; p; p = p->next)
		p->scan_bus(p);
	spin_unlock_irqrestore(&pci_controller_lock, flags);
}

/* Reorder the pci_dev chain, so that onboard devices come first
 * and then come the pluggable cards.
 */
static void __init pci_reorder_devs(void)
{
	struct pci_dev **pci_onboard = &pci_devices;
	struct pci_dev **pci_tail = &pci_devices;
	struct pci_dev *pdev = pci_devices, *pci_other = NULL;

	while (pdev) {
		if (pdev->irq && (__irq_ino(pdev->irq) & 0x20)) {
			if (pci_other) {
				*pci_onboard = pdev;
				pci_onboard = &pdev->next;
				pdev = pdev->next;
				*pci_onboard = pci_other;
				*pci_tail = pdev;
				continue;
			} else
				pci_onboard = &pdev->next;
		} else if (!pci_other)
			pci_other = pdev;
		pci_tail = &pdev->next;
		pdev = pdev->next;
	}
}

void __init pcibios_init(void)
{
	pci_controller_probe();
	if (pci_controller_root == NULL)
		return;

	pci_scan_each_controller_bus();

	if (pci_device_reorder)
		pci_reorder_devs();

	ebus_init();
}

struct pci_fixup pcibios_fixups[] = {
	{ 0 }
};

void pcibios_fixup_bus(struct pci_bus *pbus)
{
}

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "onboardfirst")) {
		pci_device_reorder = 1;
		return NULL;
	}
	if (!strcmp(str, "noreorder")) {
		pci_device_reorder = 0;
		return NULL;
	}
	return str;
}

asmlinkage int sys_pciconfig_read(unsigned long bus,
				  unsigned long dfn,
				  unsigned long off,
				  unsigned long len,
				  unsigned char *buf)
{
	struct pci_dev *dev;
	u8 byte;
	u16 word;
	u32 dword;
	int err = 0;

	if(!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dev = pci_find_slot(bus, dfn);
	if (!dev) {
		/* Xfree86 is such a turd, it does not check the
		 * return value and just relies on the buffer being
		 * set to all 1's to mean "device not present".
		 */
		switch(len) {
		case 1:
			put_user(0xff, (unsigned char *)buf);
			break;
		case 2:
			put_user(0xffff, (unsigned short *)buf);
			break;
		case 4:
			put_user(0xffffffff, (unsigned int *)buf);
			break;
		default:
			err = -EINVAL;
			break;
		};
		goto out;
	}

	lock_kernel();
	switch(len) {
	case 1:
		pci_read_config_byte(dev, off, &byte);
		put_user(byte, (unsigned char *)buf);
		break;
	case 2:
		pci_read_config_word(dev, off, &word);
		put_user(word, (unsigned short *)buf);
		break;
	case 4:
		pci_read_config_dword(dev, off, &dword);
		put_user(dword, (unsigned int *)buf);
		break;

	default:
		err = -EINVAL;
		break;
	};
	unlock_kernel();
out:
	return err;
}

asmlinkage int sys_pciconfig_write(unsigned long bus,
				   unsigned long dfn,
				   unsigned long off,
				   unsigned long len,
				   unsigned char *buf)
{
	struct pci_dev *dev;
	u8 byte;
	u16 word;
	u32 dword;
	int err = 0;

	if(!capable(CAP_SYS_ADMIN))
		return -EPERM;
	dev = pci_find_slot(bus, dfn);
	if (!dev) {
		/* See commentary above about Xfree86 */
		goto out;
	}

	lock_kernel();
	switch(len) {
	case 1:
		err = get_user(byte, (u8 *)buf);
		if(err)
			break;
		pci_write_config_byte(dev, off, byte);
		break;

	case 2:
		err = get_user(word, (u16 *)buf);
		if(err)
			break;
		pci_write_config_byte(dev, off, word);
		break;

	case 4:
		err = get_user(dword, (u32 *)buf);
		if(err)
			break;
		pci_write_config_byte(dev, off, dword);
		break;

	default:
		err = -EINVAL;
		break;

	};
	unlock_kernel();

out:
	return err;
}

#endif /* !(CONFIG_PCI) */

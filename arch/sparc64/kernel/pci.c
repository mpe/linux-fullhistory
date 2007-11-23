/* $Id: pci.c,v 1.16 2000/03/01 02:53:33 davem Exp $
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

unsigned long pci_memspace_mask = 0xffffffffUL;

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
	{ "pci108e,a000", sabre_init },
	{ "SUNW,psycho", psycho_init },
	{ "pci108e,8000", psycho_init }
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
		else {
			len = prom_getproperty(node, "compatible",
					       namebuf, sizeof(namebuf));
			if (len > 0)
				pci_controller_init(namebuf, len, node);
		}
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
	struct list_head *pci_onboard = &pci_devices;
	struct list_head *walk = pci_onboard->next;

	while (walk != pci_onboard) {
		struct pci_dev *pdev = pci_dev_g(walk);
		struct list_head *walk_next = walk->next;

		if (pdev->irq && (__irq_ino(pdev->irq) & 0x20)) {
			list_del(walk);
			list_add(walk, pci_onboard);
		}

		walk = walk_next;
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

void pcibios_update_resource(struct pci_dev *pdev, struct resource *res1,
			     struct resource *res2, int index)
{
}

void pcibios_update_irq(struct pci_dev *pdev, int irq)
{
}

unsigned long resource_fixup(struct pci_dev *pdev, struct resource *res,
			     unsigned long start, unsigned long size)
{
	return start;
}

void pcibios_fixup_pbus_ranges(struct pci_bus *pbus,
			       struct pbus_set_ranges_data *pranges)
{
}

void pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
}

int pcibios_enable_device(struct pci_dev *pdev)
{
	return 0;
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

/* $Id: pci.c,v 1.36 2001/10/06 00:38:25 davem Exp $
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
#include <asm/isa.h>

unsigned long pci_memspace_mask = 0xffffffffUL;

#ifndef CONFIG_PCI
/* A "nop" PCI implementation. */
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
volatile int pci_poke_cpu = -1;
volatile int pci_poke_faulted;

/* Probe for all PCI controllers in the system. */
extern void sabre_init(int, char *);
extern void psycho_init(int, char *);
extern void schizo_init(int, char *);

static struct {
	char *model_name;
	void (*init)(int, char *);
} pci_controller_table[] = {
	{ "SUNW,sabre", sabre_init },
	{ "pci108e,a000", sabre_init },
	{ "pci108e,a001", sabre_init },
	{ "SUNW,psycho", psycho_init },
	{ "pci108e,8000", psycho_init },
	{ "SUNW,schizo", schizo_init },
	{ "pci108e,8001", schizo_init }
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
			pci_controller_table[i].init(node, model_name);
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

extern void rs_init(void);
extern void clock_probe(void);
extern void power_init(void);

void __init pcibios_init(void)
{
	pci_controller_probe();
	if (pci_controller_root == NULL)
		return;

	pci_scan_each_controller_bus();

	if (pci_device_reorder)
		pci_reorder_devs();

	isa_init();
	ebus_init();
	rs_init();
	clock_probe();
	power_init();
}

struct pci_fixup pcibios_fixups[] = {
	{ 0 }
};

void pcibios_fixup_bus(struct pci_bus *pbus)
{
	struct pci_pbm_info *pbm = pbus->sysdata;

	/* Generic PCI bus probing sets these to point at
	 * &io{port,mem}_resouce which is wrong for us.
	 */
	pbus->resource[0] = &pbm->io_space;
	pbus->resource[1] = &pbm->mem_space;
}

/* NOTE: This can get called before we've fixed up pdev->sysdata. */
int pci_claim_resource(struct pci_dev *pdev, int resource)
{
	struct pci_pbm_info *pbm = pci_bus2pbm[pdev->bus->number];
	struct resource *res = &pdev->resource[resource];
	struct resource *root;

	if (!pbm)
		return -EINVAL;

	if (res->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	else
		root = &pbm->mem_space;

	pbm->parent->resource_adjust(pdev, res, root);

	return request_resource(root, res);
}

int pci_assign_resource(struct pci_dev *pdev, int resource)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = pcp->pbm;
	struct resource *res = &pdev->resource[resource];
	struct resource *root;
	unsigned long min, max, size, align;
	int err;

	if (res->flags & IORESOURCE_IO) {
		root = &pbm->io_space;
		min = root->start + 0x400UL;
		max = root->end;
	} else {
		root = &pbm->mem_space;
		min = root->start;
		max = min + 0x80000000UL;
	}

	size = res->end - res->start;
	align = size + 1;

	err = allocate_resource(root, res, size + 1, min, max, align, NULL, NULL);
	if (err < 0) {
		printk("PCI: Failed to allocate resource %d for %s\n",
		       resource, pdev->name);
	} else {
		pbm->parent->base_address_update(pdev, resource);
	}

	return err;
}

void pcibios_update_resource(struct pci_dev *pdev, struct resource *res1,
			     struct resource *res2, int index)
{
}

void pcibios_update_irq(struct pci_dev *pdev, int irq)
{
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

/* Platform support for /proc/bus/pci/X/Y mmap()s. */

/* If the user uses a host-bridge as the PCI device, he may use
 * this to perform a raw mmap() of the I/O or MEM space behind
 * that controller.
 *
 * This can be useful for execution of x86 PCI bios initialization code
 * on a PCI card, like the xfree86 int10 stuff does.
 */
static int __pci_mmap_make_offset_bus(struct pci_dev *pdev, struct vm_area_struct *vma,
				      enum pci_mmap_state mmap_state)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm;
	struct pci_controller_info *p;
	unsigned long space_size, user_offset, user_size;

	if (!pcp)
		return -ENXIO;
	pbm = pcp->pbm;
	if (!pbm)
		return -ENXIO;

	p = pbm->parent;
	if (p->pbms_same_domain) {
		unsigned long lowest, highest;

		lowest = ~0UL; highest = 0UL;
		if (mmap_state == pci_mmap_io) {
			if (p->pbm_A.io_space.flags) {
				lowest = p->pbm_A.io_space.start;
				highest = p->pbm_A.io_space.end + 1;
			}
			if (p->pbm_B.io_space.flags) {
				if (lowest > p->pbm_B.io_space.start)
					lowest = p->pbm_B.io_space.start;
				if (highest < p->pbm_B.io_space.end + 1)
					highest = p->pbm_B.io_space.end + 1;
			}
			space_size = highest - lowest;
		} else {
			if (p->pbm_A.mem_space.flags) {
				lowest = p->pbm_A.mem_space.start;
				highest = p->pbm_A.mem_space.end + 1;
			}
			if (p->pbm_B.mem_space.flags) {
				if (lowest > p->pbm_B.mem_space.start)
					lowest = p->pbm_B.mem_space.start;
				if (highest < p->pbm_B.mem_space.end + 1)
					highest = p->pbm_B.mem_space.end + 1;
			}
			space_size = highest - lowest;
		}
	} else {
		if (mmap_state == pci_mmap_io) {
			space_size = (pbm->io_space.end -
				      pbm->io_space.start) + 1;
		} else {
			space_size = (pbm->mem_space.end -
				      pbm->mem_space.start) + 1;
		}
	}

	/* Make sure the request is in range. */
	user_offset = vma->vm_pgoff << PAGE_SHIFT;
	user_size = vma->vm_end - vma->vm_start;

	if (user_offset >= space_size ||
	    (user_offset + user_size) > space_size)
		return -EINVAL;

	if (p->pbms_same_domain) {
		unsigned long lowest = ~0UL;

		if (mmap_state == pci_mmap_io) {
			if (p->pbm_A.io_space.flags)
				lowest = p->pbm_A.io_space.start;
			if (p->pbm_B.io_space.flags &&
			    lowest > p->pbm_B.io_space.start)
				lowest = p->pbm_B.io_space.start;
		} else {
			if (p->pbm_A.mem_space.flags)
				lowest = p->pbm_A.mem_space.start;
			if (p->pbm_B.mem_space.flags &&
			    lowest > p->pbm_B.mem_space.start)
				lowest = p->pbm_B.mem_space.start;
		}
		vma->vm_pgoff = (lowest + user_offset) >> PAGE_SHIFT;
	} else {
		if (mmap_state == pci_mmap_io) {
			vma->vm_pgoff = (pbm->io_space.start +
					 user_offset) >> PAGE_SHIFT;
		} else {
			vma->vm_pgoff = (pbm->mem_space.start +
					 user_offset) >> PAGE_SHIFT;
		}
	}

	return 0;
}

/* Adjust vm_pgoff of VMA such that it is the physical page offset corresponding
 * to the 32-bit pci bus offset for DEV requested by the user.
 *
 * Basically, the user finds the base address for his device which he wishes
 * to mmap.  They read the 32-bit value from the config space base register,
 * add whatever PAGE_SIZE multiple offset they wish, and feed this into the
 * offset parameter of mmap on /proc/bus/pci/XXX for that device.
 *
 * Returns negative error code on failure, zero on success.
 */
static int __pci_mmap_make_offset(struct pci_dev *dev, struct vm_area_struct *vma,
				  enum pci_mmap_state mmap_state)
{
	unsigned long user_offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long user32 = user_offset & 0xffffffffUL;
	unsigned long largest_base, this_base, addr32;
	int i;

	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_HOST)
		return __pci_mmap_make_offset_bus(dev, vma, mmap_state);

	/* Figure out which base address this is for. */
	largest_base = 0UL;
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		struct resource *rp = &dev->resource[i];

		/* Active? */
		if (!rp->flags)
			continue;

		/* Same type? */
		if (i == PCI_ROM_RESOURCE) {
			if (mmap_state != pci_mmap_mem)
				continue;
		} else {
			if ((mmap_state == pci_mmap_io &&
			     (rp->flags & IORESOURCE_IO) == 0) ||
			    (mmap_state == pci_mmap_mem &&
			     (rp->flags & IORESOURCE_MEM) == 0))
				continue;
		}

		this_base = rp->start;

		addr32 = (this_base & PAGE_MASK) & 0xffffffffUL;

		if (mmap_state == pci_mmap_io)
			addr32 &= 0xffffff;

		if (addr32 <= user32 && this_base > largest_base)
			largest_base = this_base;
	}

	if (largest_base == 0UL)
		return -EINVAL;

	/* Now construct the final physical address. */
	if (mmap_state == pci_mmap_io)
		vma->vm_pgoff = (((largest_base & ~0xffffffUL) | user32) >> PAGE_SHIFT);
	else
		vma->vm_pgoff = (((largest_base & ~0xffffffffUL) | user32) >> PAGE_SHIFT);

	return 0;
}

/* Set vm_flags of VMA, as appropriate for this architecture, for a pci device
 * mapping.
 */
static void __pci_mmap_set_flags(struct pci_dev *dev, struct vm_area_struct *vma,
					    enum pci_mmap_state mmap_state)
{
	vma->vm_flags |= (VM_SHM | VM_LOCKED);
}

/* Set vm_page_prot of VMA, as appropriate for this architecture, for a pci
 * device mapping.
 */
static void __pci_mmap_set_pgprot(struct pci_dev *dev, struct vm_area_struct *vma,
					     enum pci_mmap_state mmap_state)
{
	/* Our io_remap_page_range takes care of this, do nothing. */
}

extern int io_remap_page_range(unsigned long from, unsigned long offset,
			       unsigned long size, pgprot_t prot, int space);

/* Perform the actual remap of the pages for a PCI device mapping, as appropriate
 * for this architecture.  The region in the process to map is described by vm_start
 * and vm_end members of VMA, the base physical address is found in vm_pgoff.
 * The pci device structure is provided so that architectures may make mapping
 * decisions on a per-device or per-bus basis.
 *
 * Returns a negative error code on failure, zero on success.
 */
int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state,
			int write_combine)
{
	int ret;

	ret = __pci_mmap_make_offset(dev, vma, mmap_state);
	if (ret < 0)
		return ret;

	__pci_mmap_set_flags(dev, vma, mmap_state);
	__pci_mmap_set_pgprot(dev, vma, mmap_state);

	ret = io_remap_page_range(vma->vm_start,
				  (vma->vm_pgoff << PAGE_SHIFT |
				   (write_combine ? 0x1UL : 0x0UL)),
				  vma->vm_end - vma->vm_start, vma->vm_page_prot, 0);
	if (ret)
		return ret;

	vma->vm_flags |= VM_IO;
	return 0;
}

/* Return the index of the PCI controller for device PDEV. */

int pci_controller_num(struct pci_dev *pdev)
{
	struct pcidev_cookie *cookie = pdev->sysdata;
	int ret;

	if (cookie != NULL) {
		struct pci_pbm_info *pbm = cookie->pbm;
		if (pbm == NULL || pbm->parent == NULL) {
			ret = -ENXIO;
		} else {
			struct pci_controller_info *p = pbm->parent;

			ret = p->index;
			if (p->pbms_same_domain == 0)
				ret = ((ret << 1) +
				       ((pbm == &pbm->parent->pbm_B) ? 1 : 0));
		}
	} else {
		ret = -ENXIO;
	}

	return ret;
}

#endif /* !(CONFIG_PCI) */

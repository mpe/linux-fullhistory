/* $Id: psycho.c,v 1.5 1997/08/15 06:44:18 davem Exp $
 * psycho.c: Ultra/AX U2P PCI controller support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caipfs.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/ebus.h>
#include <asm/sbus.h> /* for sanity check... */

#ifndef CONFIG_PCI

int pcibios_present(void)
{
	return 0;
}

asmlinkage int sys_pciconfig_read(unsigned long bus,
				  unsigned long dfn,
				  unsigned long off,
				  unsigned long len,
				  unsigned char *buf)
{
	return 0;
}

asmlinkage int sys_pciconfig_write(unsigned long bus,
				   unsigned long dfn,
				   unsigned long off,
				   unsigned long len,
				   unsigned char *buf)
{
	return 0;
}

#else

#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/oplib.h>
#include <asm/pbm.h>
#include <asm/uaccess.h>

struct linux_psycho *psycho_root = NULL;

/* This is used to make the scan_bus in the generic PCI code be
 * a nop, as we need to control the actual bus probing sequence.
 * After that we leave it on of course.
 */
static int pci_probe_enable = 0;

static inline unsigned long long_align(unsigned long addr)
{
	return ((addr + (sizeof(unsigned long) - 1)) &
		~(sizeof(unsigned long) - 1));
}

extern void prom_pbm_ranges_init(int node, struct linux_pbm_info *pbm);

unsigned long pcibios_init(unsigned long memory_start, unsigned long memory_end)
{
	struct linux_prom64_registers pr_regs[3];
	char namebuf[128];
	u32 portid;
	int node;

	/* prom_printf("PSYCHO: Probing for controllers.\n"); */
	printk("PSYCHO: Probing for controllers.\n");

	memory_start = long_align(memory_start);
	node = prom_getchild(prom_root_node);
	while((node = prom_searchsiblings(node, "pci")) != 0) {
		struct linux_psycho *psycho = (struct linux_psycho *)memory_start;
		struct linux_psycho *search;
		struct linux_pbm_info *pbm = NULL;
		u32 busrange[2];
		int err, is_pbm_a;

		portid = prom_getintdefault(node, "upa-portid", 0xff);
		for(search = psycho_root; search; search = search->next) {
			if(search->upa_portid == portid) {
				psycho = search;

				/* This represents _this_ instance, so it's
				 * which ever one does _not_ have the prom node
				 * info filled in yet.
				 */
				is_pbm_a = (psycho->pbm_A.prom_node == 0);
				goto other_pbm;
			}
		}

		memory_start = long_align(memory_start + sizeof(struct linux_psycho));

		memset(psycho, 0, sizeof(*psycho));

		psycho->next = psycho_root;
		psycho_root = psycho;

		psycho->upa_portid = portid;

		/* Map in PSYCHO register set and report the presence of this PSYCHO. */
		err = prom_getproperty(node, "reg",
				       (char *)&pr_regs[0], sizeof(pr_regs));
		if(err == 0 || err == -1) {
			prom_printf("PSYCHO: Error, cannot get U2P registers "
				    "from PROM.\n");
			prom_halt();
		}

		/* Third REG in property is base of entire PSYCHO register space. */
		psycho->psycho_regs = sparc_alloc_io((pr_regs[2].phys_addr & 0xffffffff),
						     NULL, sizeof(struct psycho_regs),
						     "PSYCHO Registers",
						     (pr_regs[2].phys_addr >> 32), 0);
		if(psycho->psycho_regs == NULL) {
			prom_printf("PSYCHO: Error, cannot map PSYCHO "
				    "main registers.\n");
			prom_halt();
		}

		prom_printf("PSYCHO: Found controller, main regs at %p\n",
			    psycho->psycho_regs);
		printk("PSYCHO: Found controller, main regs at %p\n",
		       psycho->psycho_regs);

		/* Now map in PCI config space for entire PSYCHO. */
		psycho->pci_config_space =
			sparc_alloc_io(((pr_regs[2].phys_addr & 0xffffffff)+0x01000000),
				       NULL, 0x01000000,
				       "PCI Config Space",
				       (pr_regs[2].phys_addr >> 32), 0);
		if(psycho->pci_config_space == NULL) {
			prom_printf("PSYCHO: Error, cannot map PCI config space.\n");
			prom_halt();
		}

		/* Finally map in I/O space for both PBM's.  This is essentially
		 * backwards compatability for non-conformant PCI cards which
		 * do not map themselves into the PCI memory space.
		 */
		psycho->pbm_B.pbm_IO = __va(pr_regs[2].phys_addr + 0x02000000UL);
		psycho->pbm_A.pbm_IO = __va(pr_regs[2].phys_addr + 0x02010000UL);

		/* Now record MEM space for both PBM's.
		 *
		 * XXX Eddie, these can be reversed if BOOT_BUS pin is clear, is
		 * XXX there some way to find out what value of BOOT_BUS pin is?
		 */
		psycho->pbm_B.pbm_mem = __va(pr_regs[2].phys_addr + 0x180000000UL);
		psycho->pbm_A.pbm_mem = __va(pr_regs[2].phys_addr + 0x100000000UL);

		/* Report some more info. */
		prom_printf("PSYCHO: PCI config space at %p\n",
			    psycho->pci_config_space);
		prom_printf("PSYCHO: PBM A I/O space at %p, PBM B I/O at %p\n",
			    psycho->pbm_A.pbm_IO, psycho->pbm_B.pbm_IO);
		prom_printf("PSYCHO: PBM A MEM at %p, PBM B MEM at %p\n");

		printk("PSYCHO: PCI config space at %p\n", psycho->pci_config_space);
		printk("PSYCHO: PBM A I/O space at %p, PBM B I/O at %p\n",
		       psycho->pbm_A.pbm_IO, psycho->pbm_B.pbm_IO);

		is_pbm_a = ((pr_regs[0].phys_addr & 0x6000) == 0x2000);

	other_pbm:
		if(is_pbm_a)
			pbm = &psycho->pbm_A;
		else
			pbm = &psycho->pbm_B;

		pbm->parent = psycho;
		pbm->prom_node = node;

		prom_getstring(node, "name", namebuf, sizeof(namebuf));
		strcpy(pbm->prom_name, namebuf);

		/* Now the ranges. */
		prom_pbm_ranges_init(node, pbm);

		/* Finally grab the pci bus root array for this pbm after
		 * having found the bus range existing under it.
		 */
		err = prom_getproperty(node, "bus-range",
				       (char *)&busrange[0], sizeof(busrange));
		if(err == 0 || err == -1) {
			prom_printf("PSYCHO: Error, cannot get PCI bus range.\n");
			prom_halt();
		}
		pbm->pci_first_busno = busrange[0];
		pbm->pci_last_busno = busrange[1];
		memset(&pbm->pci_bus, 0, sizeof(struct pci_bus));

		node = prom_getsibling(node);
		if(!node)
			break;
	}

	/* Last minute sanity check. */
	if(psycho_root == NULL && SBus_chain == NULL) {
		prom_printf("Fatal error, neither SBUS nor PCI bus found.\n");
		prom_halt();
	}

	return memory_start;
}

int pcibios_present(void)
{
	return psycho_root != NULL;
}

int pcibios_find_device (unsigned short vendor, unsigned short device_id,
			 unsigned short index, unsigned char *bus,
			 unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->vendor == vendor && dev->device == device_id) {
			if (curr == index) {
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++curr;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

int pcibios_find_class (unsigned int class_code, unsigned short index,
			unsigned char *bus, unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->class == class_code) {
			if (curr == index) {
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++curr;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static void pbm_probe(struct linux_pbm_info *pbm, unsigned long *mstart)
{
	struct pci_bus *pbus = &pbm->pci_bus;

	/* PSYCHO PBM's include child PCI bridges in bus-range property,
	 * but we don't scan each of those ourselves, Linux generic PCI
	 * probing code will find child bridges and link them into this
	 * pbm's root PCI device hierarchy.
	 */
	pbus->number = pbm->pci_first_busno;
	pbus->sysdata = pbm;
	pbus->subordinate = pci_scan_bus(pbus, mstart);
}

static void fill_in_pbm_cookies(struct linux_pbm_info *pbm)
{
	struct pci_bus *pbtmp, *pbus = &pbm->pci_bus;
	struct pci_dev *pdev;

	for(pbtmp = pbus->children; pbtmp; pbtmp = pbtmp->children)
		pbtmp->sysdata = pbm;

	for( ; pbus; pbus = pbus->children)
		for(pdev = pbus->devices; pdev; pdev = pdev->sibling)
			pdev->sysdata = pbm;
}

static void fixup_pci_dev(struct pci_dev *pdev,
			  struct pci_bus *pbus,
			  struct linux_pbm_info *pbm)
{
	struct linux_prom_pci_registers pregs[PROMREG_MAX];
	int node;
#if 0
	int nregs, busno = pbus->number;
#endif

	node = prom_getchild(pbm->prom_node);
	while(node) {
		u32 devfn;
		int err, nregs;

		err = prom_getproperty(node, "reg", (char *)&pregs[0], sizeof(pregs));
		if(err == 0 || err == -1) {
			prom_printf("fixup_pci_dev: No PCI device reg property?!?!\n");
			prom_halt();
		}
		nregs = (err / sizeof(struct linux_prom_pci_registers));

		devfn = (pregs[0].phys_hi >> 8) & 0xff;
		if(devfn == pdev->devfn) {

			return;
		}

		node = prom_getsibling(node);
	}

	prom_printf("fixup_pci_dev: Cannot find prom node for PCI device\n");
	prom_halt();
}

static void fixup_pci_bus(struct pci_bus *pbus, struct linux_pbm_info *pbm)
{
	struct pci_dev *pdev;

	for(pdev = pbus->devices; pdev; pdev = pdev->sibling)
		fixup_pci_dev(pdev, pbus, pbm);

	for(pbus = pbus->children; pbus; pbus = pbus->children)
		fixup_pci_bus(pbus, pbm);
}

static void fixup_addr_irq(struct linux_pbm_info *pbm)
{
	struct pci_bus *pbus = &pbm->pci_bus;

	/* Work through top level devices (not bridges, those and their
	 * devices are handled specially in the next loop).
	 */
	fixup_pci_bus(pbus, pbm);
}

/* Walk all PCI devices probes, fixing up base registers and IRQ registers.
 * We use OBP for most of this work.
 */
static void psycho_final_fixup(struct linux_psycho *psycho)
{
	/* First, walk all PCI devices found.  For each device, and
	 * PCI bridge which is not one of the PSYCHO PBM's, fill in the
	 * sysdata with a pointer to the PBM.
	 */
	fill_in_pbm_cookies(&psycho->pbm_A);
	fill_in_pbm_cookies(&psycho->pbm_B);

	/* Second, fixup base address registers and IRQ lines... */
	fixup_addr_irq(&psycho->pbm_A);
	fixup_addr_irq(&psycho->pbm_B);
}

unsigned long pcibios_fixup(unsigned long memory_start, unsigned long memory_end)
{
	struct linux_psycho *psycho = psycho_root;

	pci_probe_enable = 1;

	/* XXX Really this should be per-PSYCHO, but the config space
	 * XXX reads and writes give us no way to know which PSYCHO
	 * XXX in which the config space reads should occur.
	 * XXX
	 * XXX Further thought says that we cannot change this generic
	 * XXX interface, else we'd break xfree86 and other parts of the
	 * XXX kernel (but whats more important is breaking userland for
	 * XXX the ix86/Alpha/etc. people).  So we should define our own
	 * XXX internal extension initially, we can compile our own user
	 * XXX apps that need to get at PCI configuration space.
	 */

	/* Probe busses under PBM A. */
	pbm_probe(&psycho->pbm_A, &memory_start);

	/* Probe busses under PBM B. */
	pbm_probe(&psycho->pbm_B, &memory_start);


	psycho_final_fixup(psycho);

	return ebus_init(memory_start, memory_end);
}

/* "PCI: The emerging standard..." 8-( */
volatile int pci_poke_in_progress = 0;
volatile int pci_poke_faulted = 0;

/* XXX Current PCI support code is broken, it assumes one master PCI config
 * XXX space exists, on Ultra we can have many of them, especially with
 * XXX 'dual-pci' boards on Sunfire/Starfire/Wildfire.
 */
static char *pci_mkaddr(unsigned char bus, unsigned char device_fn,
			unsigned char where)
{
	unsigned long ret = (unsigned long) psycho_root->pci_config_space;

	ret |= (1 << 24);
	ret |= ((bus & 0xff) << 16);
	ret |= ((device_fn & 0xff) << 8);
	ret |= (where & 0xfc);
	return (unsigned char *)ret;
}

static inline int out_of_range(unsigned char bus, unsigned char device_fn)
{
	return ((bus == 0 && PCI_SLOT(device_fn) > 4) ||
		(bus == 1 && PCI_SLOT(device_fn) > 6) ||
		(pci_probe_enable == 0));
}

int pcibios_read_config_byte (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned char *value)
{
	unsigned char *addr = pci_mkaddr(bus, device_fn, where);
	unsigned int word, trapped;

	*value = 0xff;

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduwa [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (word)
			     : "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	trapped = pci_poke_faulted;
	pci_poke_faulted = 0;
	if(!trapped) {
		switch(where & 3) {
		case 0:
			*value = word & 0xff;
			break;
		case 1:
			*value = (word >> 8) & 0xff;
			break;
		case 2:
			*value = (word >> 16) & 0xff;
			break;
		case 3:
			*value = (word >> 24) & 0xff;
			break;
		};
	}
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_word (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned short *value)
{
	unsigned short *addr = (unsigned short *)pci_mkaddr(bus, device_fn, where);
	unsigned int word, trapped;

	*value = 0xffff;

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	/* XXX Check no-probe-list conflicts here. XXX */

	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduwa [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (word)
			     : "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	trapped = pci_poke_faulted;
	pci_poke_faulted = 0;
	if(!trapped) {
		switch(where & 3) {
		case 0:
			*value = word & 0xffff;
			break;
		case 2:
			*value = (word >> 16) & 0xffff;
			break;
		default:
			prom_printf("pcibios_read_config_word: misaligned "
				    "reg [%x]\n", where);
			prom_halt();
		};
	}
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_dword (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned int *value)
{
	unsigned int *addr = (unsigned int *)pci_mkaddr(bus, device_fn, where);
	unsigned int word, trapped;

	*value = 0xffffffff;

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	/* XXX Check no-probe-list conflicts here. XXX */

	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduwa [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (word)
			     : "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	trapped = pci_poke_faulted;
	pci_poke_faulted = 0;
	if(!trapped)
		*value = word;
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_byte (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned char value)
{
	unsigned char *addr = pci_mkaddr(bus, device_fn, where);

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	/* XXX Check no-probe-list conflicts here. XXX */

	pci_poke_in_progress = 1;

	/* Endianness doesn't matter but we have to get the memory
	 * barriers in there so...
	 */
	__asm__ __volatile__("membar #Sync\n\t"
			     "stba %0, [%1] %2\n\t"
			     "membar #Sync\n\t"
			     : /* no outputs */
			     : "r" (value), "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;

	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_word (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned short value)
{
	unsigned short *addr = (unsigned short *)pci_mkaddr(bus, device_fn, where);

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	/* XXX Check no-probe-list conflicts here. XXX */

	pci_poke_in_progress = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stha %0, [%1] %2\n\t"
			     "membar #Sync\n\t"
			     : /* no outputs */
			     : "r" (value), "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_dword (unsigned char bus, unsigned char device_fn,
				unsigned char where, unsigned int value)
{
	unsigned int *addr = (unsigned int *)pci_mkaddr(bus, device_fn, where);

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	/* XXX Check no-probe-list conflicts here. XXX */

	pci_poke_in_progress = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stwa %0, [%1] %2\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (value), "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	return PCIBIOS_SUCCESSFUL;
}

asmlinkage int sys_pciconfig_read(unsigned long bus,
				  unsigned long dfn,
				  unsigned long off,
				  unsigned long len,
				  unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	int err = 0;

	lock_kernel();
	switch(len) {
	case 1:
		pcibios_read_config_byte(bus, dfn, off, &ubyte);
		put_user(ubyte, buf);
		break;
	case 2:
		pcibios_read_config_word(bus, dfn, off, &ushort);
		put_user(ushort, buf);
		break;
	case 4:
		pcibios_read_config_dword(bus, dfn, off, &uint);
		put_user(uint, buf);
		break;

	default:
		err = -EINVAL;
		break;
	};
	unlock_kernel();

	return err;
}

asmlinkage int sys_pciconfig_write(unsigned long bus,
				   unsigned long dfn,
				   unsigned long off,
				   unsigned long len,
				   unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	int err = 0;

	lock_kernel();
	switch(len) {
	case 1:
		err = get_user(ubyte, (unsigned char *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, ubyte);
		break;

	case 2:
		err = get_user(ushort, (unsigned short *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, ushort);
		break;

	case 4:
		err = get_user(uint, (unsigned int *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, uint);
		break;

	default:
		err = -EINVAL;
		break;

	};
	unlock_kernel();

	return err;
}

#endif

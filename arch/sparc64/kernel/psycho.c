/* $Id: psycho.c,v 1.66 1998/11/02 22:27:45 davem Exp $
 * psycho.c: Ultra/AX U2P PCI controller support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caipfs.rutgers.edu)
 * Copyright (C) 1998 Eddie C. Dost   (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/malloc.h>

#include <asm/ebus.h>
#include <asm/sbus.h> /* for sanity check... */
#include <asm/irq.h>
#include <asm/io.h>

#undef PROM_DEBUG
#undef FIXUP_REGS_DEBUG
#undef FIXUP_IRQ_DEBUG
#undef FIXUP_VMA_DEBUG
#undef PCI_COOKIE_DEBUG

#ifdef PROM_DEBUG
#define dprintf	prom_printf
#else
#define dprintf printk
#endif


unsigned long pci_dvma_offset = 0x00000000UL;
unsigned long pci_dvma_mask = 0xffffffffUL;

unsigned long pci_dvma_v2p_hash[PCI_DVMA_HASHSZ];
unsigned long pci_dvma_p2v_hash[PCI_DVMA_HASHSZ];


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
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/oplib.h>
#include <asm/pbm.h>
#include <asm/apb.h>
#include <asm/uaccess.h>

struct linux_psycho *psycho_root = NULL;
int linux_num_psycho = 0;
static struct linux_pbm_info *bus2pbm[256];

static int pbm_read_config_byte(struct linux_pbm_info *pbm,
				unsigned char bus, unsigned char devfn,
				unsigned char where, unsigned char *value);
static int pbm_read_config_word(struct linux_pbm_info *pbm,
				unsigned char bus, unsigned char devfn,
				unsigned char where, unsigned short *value);
static int pbm_read_config_dword(struct linux_pbm_info *pbm,
				 unsigned char bus, unsigned char devfn,
				 unsigned char where, unsigned int *value);
static int pbm_write_config_byte(struct linux_pbm_info *pbm,
				 unsigned char bus, unsigned char devfn,
				 unsigned char where, unsigned char value);
static int pbm_write_config_word(struct linux_pbm_info *pbm,
				 unsigned char bus, unsigned char devfn,
				 unsigned char where, unsigned short value);
static int pbm_write_config_dword(struct linux_pbm_info *pbm,
				  unsigned char bus, unsigned char devfn,
				  unsigned char where, unsigned int value);

/* This is used to make the scan_bus in the generic PCI code be
 * a nop, as we need to control the actual bus probing sequence.
 * After that we leave it on of course.
 */
static int pci_probe_enable = 0;

static __inline__ void set_dvma_hash(unsigned long paddr, unsigned long daddr)
{
	unsigned long dvma_addr = pci_dvma_offset + daddr;
	unsigned long vaddr = (unsigned long)__va(paddr);

	pci_dvma_v2p_hash[pci_dvma_ahashfn(paddr)] = dvma_addr - vaddr;
	pci_dvma_p2v_hash[pci_dvma_ahashfn(dvma_addr)] = vaddr - dvma_addr;
}

__initfunc(static void psycho_iommu_init(struct linux_psycho *psycho, int tsbsize))
{
	struct linux_mlist_p1275 *mlist;
	unsigned long tsbbase;
	unsigned long control, i, n;
	unsigned long *iopte;
	unsigned long order;

	/*
	 * Invalidate TLB Entries.
	 */
	control = psycho->psycho_regs->iommu_control;
	control |= IOMMU_CTRL_DENAB;
	psycho->psycho_regs->iommu_control = control;
	for(i = 0; i < 16; i++) {
		psycho->psycho_regs->iommu_data[i] = 0;
	}
	control &= ~(IOMMU_CTRL_DENAB);
	psycho->psycho_regs->iommu_control = control;

	for(order = 0;; order++) {
		if((PAGE_SIZE << order) >= ((tsbsize * 1024) * 8))
			break;
	}
	tsbbase = __get_free_pages(GFP_DMA, order);
	iopte = (unsigned long *)tsbbase;

	memset(pci_dvma_v2p_hash, 0, sizeof(pci_dvma_v2p_hash));
	memset(pci_dvma_p2v_hash, 0, sizeof(pci_dvma_p2v_hash));

	n = 0;
	mlist = *prom_meminfo()->p1275_totphys;
	while (mlist) {
		unsigned long paddr = mlist->start_adr;

		for (i = 0; i < (mlist->num_bytes >> 16); i++) {

			*iopte = (IOPTE_VALID | IOPTE_64K |
				  IOPTE_CACHE | IOPTE_WRITE);
			*iopte |= paddr;

			if (!(n & 0xff))
				set_dvma_hash(paddr, (n << 16));
						  
			if (++n > (tsbsize * 1024))
				goto out;

			paddr += (1 << 16);
			iopte++;
		}

		mlist = mlist->theres_more;
	}
out:
	if (mlist)
		printk("WARNING: not all physical memory mapped in IOMMU\n");

	psycho->psycho_regs->iommu_tsbbase = __pa(tsbbase);

	control = psycho->psycho_regs->iommu_control;
	control &= ~(IOMMU_CTRL_TSBSZ);
	control |= (IOMMU_CTRL_TBWSZ | IOMMU_CTRL_ENAB);
	switch(tsbsize) {
	case 8:
		pci_dvma_mask = 0x1fffffffUL;
		control |= IOMMU_TSBSZ_8K;
		break;
	case 16:
		pci_dvma_mask = 0x3fffffffUL;
		control |= IOMMU_TSBSZ_16K;
		break;
	case 32:
		pci_dvma_mask = 0x7fffffffUL;
		control |= IOMMU_TSBSZ_32K;
		break;
	default:
		prom_printf("iommu_init: Illegal TSB size %d\n", tsbsize);
		prom_halt();
		break;
	}
	psycho->psycho_regs->iommu_control = control;
}

extern void prom_pbm_ranges_init(int node, struct linux_pbm_info *pbm);
extern void prom_pbm_intmap_init(int node, struct linux_pbm_info *pbm);

/*
 * Poor man's PCI...
 */
__initfunc(void sabre_init(int pnode))
{
	struct linux_prom64_registers pr_regs[2];
	struct linux_psycho *sabre;
	unsigned long ctrl;
	int tsbsize, node, err;
	u32 busrange[2];
	u32 vdma[2];
	u32 portid;
	int bus;

	sabre = kmalloc(sizeof(struct linux_psycho), GFP_ATOMIC);

	portid = prom_getintdefault(pnode, "upa-portid", 0xff);

	memset(sabre, 0, sizeof(*sabre));

	sabre->next = psycho_root;
	psycho_root = sabre;

	sabre->upa_portid = portid;
	sabre->index = linux_num_psycho++;

	/*
	 * Map in SABRE register set and report the presence of this SABRE.
	 */
	err = prom_getproperty(pnode, "reg",
			       (char *)&pr_regs[0], sizeof(pr_regs));
	if(err == 0 || err == -1) {
		prom_printf("SABRE: Error, cannot get U2P registers "
			    "from PROM.\n");
		prom_halt();
	}

	/*
	 * First REG in property is base of entire SABRE register space.
	 */
	sabre->psycho_regs =
			sparc_alloc_io((pr_regs[0].phys_addr & 0xffffffff),
				       NULL, sizeof(struct psycho_regs),
				       "SABRE Registers",
				       (pr_regs[0].phys_addr >> 32), 0);
	if(sabre->psycho_regs == NULL) {
		prom_printf("SABRE: Error, cannot map SABRE main registers.\n");
		prom_halt();
	}

	printk("PCI: Found SABRE, main regs at %p\n", sabre->psycho_regs);
#ifdef PROM_DEBUG
	dprintf("PCI: Found SABRE, main regs at %p\n", sabre->psycho_regs);
#endif

	ctrl = sabre->psycho_regs->pci_a_control;
	ctrl = (1UL << 36) | (1UL << 34) | (1UL << 21) | (1UL << 8) | 0x0fUL;
	sabre->psycho_regs->pci_a_control = ctrl;

	/* Now map in PCI config space for entire SABRE. */
	sabre->pci_config_space =
			sparc_alloc_io(((pr_regs[0].phys_addr & 0xffffffff)
								+ 0x01000000),
				       NULL, 0x01000000,
				       "PCI Config Space",
				       (pr_regs[0].phys_addr >> 32), 0);
	if(sabre->pci_config_space == NULL) {
		prom_printf("SABRE: Error, cannot map PCI config space.\n");
		prom_halt();
	}

	/* Report some more info. */
	printk("SABRE: PCI config space at %p\n", sabre->pci_config_space);
#ifdef PROM_DEBUG
	dprintf("SABRE: PCI config space at %p\n", sabre->pci_config_space);
#endif

	err = prom_getproperty(pnode, "virtual-dma",
			       (char *)&vdma[0], sizeof(vdma));
	if(err == 0 || err == -1) {
		prom_printf("SABRE: Error, cannot get virtual-dma property "
			    "from PROM.\n");
		prom_halt();
	}

	switch(vdma[1]) {
		case 0x20000000:
			tsbsize = 8;
			break;
		case 0x40000000:
			tsbsize = 16;
			break;
		case 0x80000000:
			tsbsize = 32;
			break;
		default:
			prom_printf("SABRE: strange virtual-dma size.\n");
			prom_halt();
	}

	pci_dvma_offset = vdma[0];
	psycho_iommu_init(sabre, tsbsize);

	printk("SABRE: DVMA at %08x [%08x]\n", vdma[0], vdma[1]);
#ifdef PROM_DEBUG
	dprintf("SABRE: DVMA at %08x [%08x]\n", vdma[0], vdma[1]);
#endif

	err = prom_getproperty(pnode, "bus-range",
				       (char *)&busrange[0], sizeof(busrange));
	if(err == 0 || err == -1) {
		prom_printf("SIMBA: Error, cannot get PCI bus-range "
			    " from PROM.\n");
		prom_halt();
	}

	sabre->pci_first_busno = busrange[0];
	sabre->pci_last_busno = busrange[1];
	sabre->pci_bus = &pci_root;

	/*
	 * Handle config space reads through any Simba on APB.
	 */
	for (bus = sabre->pci_first_busno; bus <= sabre->pci_last_busno; bus++)
		bus2pbm[bus] = &sabre->pbm_A;

	/*
	 * Look for APB underneath.
	 */
	node = prom_getchild(pnode);
	while ((node = prom_searchsiblings(node, "pci"))) {
		struct linux_pbm_info *pbm;
		char namebuf[128];

		err = prom_getproperty(node, "model", namebuf, sizeof(namebuf));
		if ((err <= 0) || strncmp(namebuf, "SUNW,simba", err))
			goto next_pci;

		err = prom_getproperty(node, "bus-range",
				       (char *)&busrange[0], sizeof(busrange));
		if(err == 0 || err == -1) {
			prom_printf("SIMBA: Error, cannot get PCI bus-range "
				    " from PROM.\n");
			prom_halt();
		}

		if (busrange[0] == 1)
			pbm = &sabre->pbm_B;
		else
			pbm = &sabre->pbm_A;

		pbm->parent = sabre;
		pbm->IO_assignments = NULL;
		pbm->MEM_assignments = NULL;
		pbm->prom_node = node;

		prom_getstring(node, "name", namebuf, sizeof(namebuf));
		strcpy(pbm->prom_name, namebuf);

		/* Now the ranges. */
		prom_pbm_ranges_init(pnode, pbm);
		prom_pbm_intmap_init(node, pbm);

		pbm->pci_first_busno = busrange[0];
		pbm->pci_last_busno = busrange[1];
		memset(&pbm->pci_bus, 0, sizeof(struct pci_bus));

		for (bus = pbm->pci_first_busno;
		     bus <= pbm->pci_last_busno; bus++)
			bus2pbm[bus] = pbm;

	next_pci:
		node = prom_getsibling(node);
		if (!node)
			break;
	}
}

static __inline__ int
apb_present(struct linux_psycho *psycho)
{
	return psycho->pci_bus ? 1 : 0;
}

__initfunc(void pcibios_init(void))
{
	struct linux_prom64_registers pr_regs[3];
	struct linux_psycho *psycho;
	char namebuf[128];
	u32 portid;
	int node;

	printk("PCI: Probing for controllers.\n");
#ifdef PROM_DEBUG
	dprintf("PCI: Probing for controllers.\n");
#endif

	node = prom_getchild(prom_root_node);
	while((node = prom_searchsiblings(node, "pci")) != 0) {
		struct linux_psycho *search;
		struct linux_pbm_info *pbm = NULL;
		u32 busrange[2];
		int err, is_pbm_a;

		err = prom_getproperty(node, "model", namebuf, sizeof(namebuf));
		if ((err > 0) && !strncmp(namebuf, "SUNW,sabre", err)) {
			sabre_init(node);
			goto next_pci;
		}

		psycho = kmalloc(sizeof(struct linux_psycho), GFP_ATOMIC);

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

		memset(psycho, 0, sizeof(*psycho));

		psycho->next = psycho_root;
		psycho_root = psycho;

		psycho->upa_portid = portid;
		psycho->index = linux_num_psycho++;

		/*
		 * Map in PSYCHO register set and report the presence
		 * of this PSYCHO.
		 */
		err = prom_getproperty(node, "reg",
				       (char *)&pr_regs[0], sizeof(pr_regs));
		if(err == 0 || err == -1) {
			prom_printf("PSYCHO: Error, cannot get U2P registers "
				    "from PROM.\n");
			prom_halt();
		}

		/*
		 * Third REG in property is base of entire PSYCHO
		 * register space.
		 */
		psycho->psycho_regs =
			sparc_alloc_io((pr_regs[2].phys_addr & 0xffffffff),
				       NULL, sizeof(struct psycho_regs),
				       "PSYCHO Registers",
				       (pr_regs[2].phys_addr >> 32), 0);
		if(psycho->psycho_regs == NULL) {
			prom_printf("PSYCHO: Error, cannot map PSYCHO "
				    "main registers.\n");
			prom_halt();
		}

		printk("PCI: Found PSYCHO, main regs at %p\n",
		       psycho->psycho_regs);
#ifdef PROM_DEBUG
		dprintf("PCI: Found PSYCHO, main regs at %p\n",
			psycho->psycho_regs);
#endif

		psycho->psycho_regs->irq_retry = 0xff;

		/* Now map in PCI config space for entire PSYCHO. */
		psycho->pci_config_space =
			sparc_alloc_io(((pr_regs[2].phys_addr & 0xffffffff)
								+ 0x01000000),
				       NULL, 0x01000000,
				       "PCI Config Space",
				       (pr_regs[2].phys_addr >> 32), 0);
		if(psycho->pci_config_space == NULL) {
			prom_printf("PSYCHO: Error, cannot map PCI config space.\n");
			prom_halt();
		}

		/* Report some more info. */
		printk("PSYCHO: PCI config space at %p\n",
		       psycho->pci_config_space);
#ifdef PROM_DEBUG
		dprintf("PSYCHO: PCI config space at %p\n",
			psycho->pci_config_space);
#endif

		pci_dvma_offset = 0x80000000UL;
		psycho_iommu_init(psycho, 32);

		is_pbm_a = ((pr_regs[0].phys_addr & 0x6000) == 0x2000);

		/* Enable arbitration for all PCI slots. */
		psycho->psycho_regs->pci_a_control |= 0x3f;
		psycho->psycho_regs->pci_b_control |= 0x3f;

	other_pbm:
		if(is_pbm_a)
			pbm = &psycho->pbm_A;
		else
			pbm = &psycho->pbm_B;

		pbm->parent = psycho;
		pbm->IO_assignments = NULL;
		pbm->MEM_assignments = NULL;
		pbm->prom_node = node;

		prom_getstring(node, "name", namebuf, sizeof(namebuf));
		strcpy(pbm->prom_name, namebuf);

		/* Now the ranges. */
		prom_pbm_ranges_init(node, pbm);
		prom_pbm_intmap_init(node, pbm);

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

	next_pci:
		node = prom_getsibling(node);
		if(!node)
			break;
	}
}

int pcibios_present(void)
{
	return psycho_root != NULL;
}

static inline struct pci_vma *pci_find_vma(struct linux_pbm_info *pbm,
					   unsigned long start,
					   unsigned int offset, int io)
{
	struct pci_vma *vp = (io ? pbm->IO_assignments : pbm->MEM_assignments);

	while (vp) {
		if (offset && (vp->offset != offset))
			goto next;
		if (vp->end >= start)
			break;
	next:
		vp = vp->next;
	}
	return vp;
}

static inline void pci_add_vma(struct linux_pbm_info *pbm, struct pci_vma *new, int io)
{
	struct pci_vma *vp = (io ? pbm->IO_assignments : pbm->MEM_assignments);

	if(!vp) {
		new->next = NULL;
		if(io)
			pbm->IO_assignments = new;
		else
			pbm->MEM_assignments = new;
	} else {
		struct pci_vma *prev = NULL;

		while(vp && (vp->end < new->end)) {
			prev = vp;
			vp = vp->next;
		}
		new->next = vp;
		if(!prev) {
			if(io)
				pbm->IO_assignments = new;
			else
				pbm->MEM_assignments = new;
		} else {
			prev->next = new;
		}

		/* Check for programming errors. */
		if(vp &&
		   ((vp->start >= new->start && vp->start < new->end) ||
		    (vp->end >= new->start && vp->end < new->end))) {
			prom_printf("pci_add_vma: Wheee, overlapping %s PCI vma's\n",
				    io ? "IO" : "MEM");
			prom_printf("pci_add_vma: vp[%016lx:%016lx] "
				    "new[%016lx:%016lx]\n",
				    vp->start, vp->end,
				    new->start, new->end);
		}
	}
}

static inline struct pci_vma *pci_vma_alloc(void)
{
	return kmalloc(sizeof(struct pci_vma), GFP_ATOMIC);
}

static inline struct pcidev_cookie *pci_devcookie_alloc(void)
{
	return kmalloc(sizeof(struct pcidev_cookie), GFP_ATOMIC);
}


__initfunc(static void
pbm_reconfigure_bridges(struct linux_pbm_info *pbm, unsigned char bus))
{
	unsigned int devfn, l, class;
	unsigned char hdr_type = 0;

	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) == 0) {
			pbm_read_config_byte(pbm, bus, devfn,
					     PCI_HEADER_TYPE, &hdr_type);
		} else if (!(hdr_type & 0x80)) {
			/* not a multi-function device */
			continue;
		}

		/* Check if there is anything here. */
		pbm_read_config_dword(pbm, bus, devfn, PCI_VENDOR_ID, &l);
		if (l == 0xffffffff || l == 0x00000000) {
			hdr_type = 0;
			continue;
		}

		/* See if this is a bridge device. */
		pbm_read_config_dword(pbm, bus, devfn,
				      PCI_CLASS_REVISION, &class);

		if ((class >> 16) == PCI_CLASS_BRIDGE_PCI) {
			unsigned int buses;

			pbm_read_config_dword(pbm, bus, devfn,
					      PCI_PRIMARY_BUS, &buses);

			/*
			 * First reconfigure everything underneath the bridge.
			 */
			pbm_reconfigure_bridges(pbm, (buses >> 8) & 0xff);

			/*
			 * Unconfigure this bridges bus numbers,
			 * pci_scan_bus() will fix this up properly.
			 */
			buses &= 0xff000000;
			pbm_write_config_dword(pbm, bus, devfn,
					       PCI_PRIMARY_BUS, buses);
		}
	}
}

__initfunc(static void pbm_fixup_busno(struct linux_pbm_info *pbm, unsigned char bus))
{
	unsigned int nbus;

	/*
	 * First, reconfigure all bridge devices underneath this pbm.
	 */
	pbm_reconfigure_bridges(pbm, pbm->pci_first_busno);

	/*
	 * Now reconfigure the pbm to it's new bus number and set up
	 * our bus2pbm mapping for this pbm.
	 */
	nbus = pbm->pci_last_busno - pbm->pci_first_busno;

	pbm_write_config_byte(pbm, pbm->pci_first_busno, 0, 0x40, bus);

	pbm->pci_first_busno = bus;
	pbm_write_config_byte(pbm, bus, 0, 0x41, 0xff);

	do {
		bus2pbm[bus++] = pbm;
	} while (nbus--);
}


__initfunc(static void apb_init(struct linux_psycho *sabre))
{
	struct pci_dev *pdev;
	unsigned short stmp;
	unsigned int itmp;

	for(pdev = pci_devices; pdev; pdev = pdev->next) {
		if(pdev->vendor == PCI_VENDOR_ID_SUN &&
		   pdev->device == PCI_DEVICE_ID_SUN_SABRE) {
			/* Increase latency timer on top level bridge. */
			pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xf8);
			break;
		}
	}
	for (pdev = sabre->pci_bus->devices; pdev; pdev = pdev->sibling) {
		if (pdev->vendor == PCI_VENDOR_ID_SUN &&
		    pdev->device == PCI_DEVICE_ID_SUN_SIMBA) {

			pci_read_config_word(pdev, PCI_COMMAND, &stmp);
			stmp |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY |
				PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY |
				PCI_COMMAND_IO;
			pci_write_config_word(pdev, PCI_COMMAND, stmp);

			pci_write_config_word(pdev, PCI_STATUS, 0xffff);
			pci_write_config_word(pdev, PCI_SEC_STATUS, 0xffff);

			pci_read_config_word(pdev, PCI_BRIDGE_CONTROL, &stmp);
			stmp = PCI_BRIDGE_CTL_MASTER_ABORT |
			       PCI_BRIDGE_CTL_SERR |
			       PCI_BRIDGE_CTL_PARITY;
			pci_write_config_word(pdev, PCI_BRIDGE_CONTROL, stmp);

			pci_read_config_dword(pdev, APB_PCI_CONTROL_HIGH, &itmp);
			itmp = APB_PCI_CTL_HIGH_SERR |
			       APB_PCI_CTL_HIGH_ARBITER_EN;
			pci_write_config_dword(pdev, APB_PCI_CONTROL_HIGH, itmp);

			pci_read_config_dword(pdev, APB_PCI_CONTROL_LOW, &itmp);
			itmp = APB_PCI_CTL_LOW_ARB_PARK |
			       APB_PCI_CTL_LOW_ERRINT_EN | 0x0f;
			pci_write_config_dword(pdev, APB_PCI_CONTROL_LOW, itmp);

			/*
			 * Setup Registers for Guaranteed Completion.
			 */
			pci_write_config_byte(pdev, APB_PRIMARY_MASTER_RETRY_LIMIT, 0);
			pci_write_config_byte(pdev, APB_SECONDARY_MASTER_RETRY_LIMIT, 0);
			pci_write_config_byte(pdev, APB_PIO_TARGET_RETRY_LIMIT, 0x80);
			pci_write_config_byte(pdev, APB_PIO_TARGET_LATENCY_TIMER, 0);
			pci_write_config_byte(pdev, APB_DMA_TARGET_RETRY_LIMIT, 0x80);
			pci_write_config_byte(pdev, APB_DMA_TARGET_LATENCY_TIMER, 0);

			/* Increase primary latency timer. */
			pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xf8);
		}
	}
}

__initfunc(static void sabre_probe(struct linux_psycho *sabre))
{
	struct pci_bus *pbus = sabre->pci_bus;
	static unsigned char busno = 0;

	pbus->number = pbus->secondary = busno;
	pbus->sysdata = sabre;

	pbus->subordinate = pci_scan_bus(pbus);
	busno = pbus->subordinate + 1;

	for(pbus = pbus->children; pbus; pbus = pbus->next) {
		if (pbus->number == sabre->pbm_A.pci_first_busno)
			memcpy(&sabre->pbm_A.pci_bus, pbus, sizeof(*pbus));
		if (pbus->number == sabre->pbm_B.pci_first_busno)
			memcpy(&sabre->pbm_B.pci_bus, pbus, sizeof(*pbus));
	}

	apb_init(sabre);
}


__initfunc(static void pbm_probe(struct linux_pbm_info *pbm))
{
	static struct pci_bus *pchain = NULL;
	struct pci_bus *pbus = &pbm->pci_bus;
	static unsigned char busno = 0;

	/* PSYCHO PBM's include child PCI bridges in bus-range property,
	 * but we don't scan each of those ourselves, Linux generic PCI
	 * probing code will find child bridges and link them into this
	 * pbm's root PCI device hierarchy.
	 */

	pbus->number = pbus->secondary = busno;
	pbus->sysdata = pbm;

	pbm_fixup_busno(pbm, busno);

	pbus->subordinate = pci_scan_bus(pbus);

	/*
	 * Set the maximum subordinate bus of this pbm.
	 */
	pbm->pci_last_busno = pbus->subordinate;
	pbm_write_config_byte(pbm, busno, 0, 0x41, pbm->pci_last_busno);

	busno = pbus->subordinate + 1;

	/*
	 * Fixup the chain of primary PCI busses.
	 */
	if (pchain) {
		pchain->next = &pbm->pci_bus;
		pchain = pchain->next;
	} else {
		pchain = &pci_root;
		memcpy(pchain, &pbm->pci_bus, sizeof(pci_root));
	}
}

__initfunc(static int pdev_to_pnode_sibtraverse(struct linux_pbm_info *pbm,
						struct pci_dev *pdev,
						int pnode))
{
	struct linux_prom_pci_registers pregs[PROMREG_MAX];
	int node;
	int err;

	node = prom_getchild(pnode);
	while (node) {

		err = prom_getproperty(node, "reg", (char *)&pregs[0], sizeof(pregs));
		if(err != 0 && err != -1) {
			u32 devfn = (pregs[0].phys_hi >> 8) & 0xff;

			if(devfn == pdev->devfn)
				return node; /* Match */
		}

		node = prom_getsibling(node);
	}
	return 0;
}

__initfunc(static void pdev_cookie_fillin(struct linux_pbm_info *pbm,
					  struct pci_dev *pdev, int pnode))
{
	struct pcidev_cookie *pcp;
	int node;

	node = pdev_to_pnode_sibtraverse(pbm, pdev, pnode);
	if(node == 0)
		node = -1;
	pcp = pci_devcookie_alloc();
	pcp->pbm = pbm;
	pcp->prom_node = node;
	pdev->sysdata = pcp;
#ifdef PCI_COOKIE_DEBUG
	dprintf("pdev_cookie_fillin: pdev [%02x.%02x]: pbm %p, node %x\n",
		pdev->bus->number, pdev->devfn, pbm, node);
#endif
}

__initfunc(static void fill_in_pbm_cookies(struct pci_bus *pbus,
					   struct linux_pbm_info *pbm,
					   int node))
{
	struct pci_dev *pdev;

	pbus->sysdata = pbm;

#ifdef PCI_COOKIE_DEBUG
	dprintf("fill_in_pbm_cookies: pbus [%02x]: pbm %p\n",
		pbus->number, pbm);
#endif

	for(pdev = pbus->devices; pdev; pdev = pdev->sibling)
		pdev_cookie_fillin(pbm, pdev, node);

	for(pbus = pbus->children; pbus; pbus = pbus->next) {
		struct pcidev_cookie *pcp = pbus->self->sysdata;
		fill_in_pbm_cookies(pbus, pbm, pcp->prom_node);
	}
}

__initfunc(static void sabre_cookie_fillin(struct linux_psycho *sabre))
{
	struct pci_bus *pbus = sabre->pci_bus;

	for(pbus = pbus->children; pbus; pbus = pbus->next) {
		if (pbus->number == sabre->pbm_A.pci_first_busno)
			pdev_cookie_fillin(&sabre->pbm_A, pbus->self,
					   sabre->pbm_A.prom_node);
		else if (pbus->number == sabre->pbm_B.pci_first_busno)
			pdev_cookie_fillin(&sabre->pbm_B, pbus->self,
					   sabre->pbm_B.prom_node);
	}
}

/* Walk PROM device tree under PBM, looking for 'assigned-address'
 * properties, and recording them in pci_vma's linked in via
 * PBM->assignments.
 */
__initfunc(static int gimme_ebus_assignments(int node, struct linux_prom_pci_registers *aregs))
{
	struct linux_prom_ebus_ranges erng[PROMREG_MAX];
	int err, iter;

	err = prom_getproperty(node, "ranges", (char *)&erng[0], sizeof(erng));
	if(err == 0 || err == -1) {
		prom_printf("EBUS: fatal error, no range property.\n");
		prom_halt();
	}
	err = (err / sizeof(struct linux_prom_ebus_ranges));
	for(iter = 0; iter < err; iter++) {
		struct linux_prom_ebus_ranges *ep = &erng[iter];
		struct linux_prom_pci_registers *ap = &aregs[iter];

		ap->phys_hi = ep->parent_phys_hi;
		ap->phys_mid = ep->parent_phys_mid;
		ap->phys_lo = ep->parent_phys_lo;

		ap->size_hi = 0;
		ap->size_lo = ep->size;
	}
	return err;
}

__initfunc(static void assignment_process(struct linux_pbm_info *pbm, int node))
{
	struct linux_prom_pci_registers aregs[PROMREG_MAX];
	char pname[256];
	int err, iter, numa;

	err = prom_getproperty(node, "name", (char *)&pname[0], sizeof(pname));
	if (err > 0)
		pname[err] = 0;
#ifdef FIXUP_VMA_DEBUG
	dprintf("%s: %s\n", __FUNCTION__, err > 0 ? pname : "???");
#endif
	if(strcmp(pname, "ebus") == 0) {
		numa = gimme_ebus_assignments(node, &aregs[0]);
	} else {
		err = prom_getproperty(node, "assigned-addresses",
				       (char *)&aregs[0], sizeof(aregs));

		/* No assignments, nothing to do. */
		if(err == 0 || err == -1)
			return;

		numa = (err / sizeof(struct linux_prom_pci_registers));
	}

	for(iter = 0; iter < numa; iter++) {
		struct linux_prom_pci_registers *ap = &aregs[iter];
		struct pci_vma *vp;
		int space, breg, io;

		space = (ap->phys_hi >> 24) & 3;
		if(space != 1 && space != 2)
			continue;
		io = (space == 1);

		breg = (ap->phys_hi & 0xff);

		vp = pci_vma_alloc();

		/* XXX Means we don't support > 32-bit range of
		 * XXX PCI MEM space, PSYCHO/PBM does not support it
		 * XXX either due to it's layout so...
		 */
		vp->start = ap->phys_lo;
		vp->end = vp->start + ap->size_lo - 1;
		vp->offset = (ap->phys_hi & 0xffffff);

		pci_add_vma(pbm, vp, io);

#ifdef FIXUP_VMA_DEBUG
		dprintf("%s: BaseReg %02x", pname, breg);
		dprintf(" %s vma [%08x,%08x]\n",
			io ? "I/O" : breg == PCI_ROM_ADDRESS ? "ROM" : "MEM", vp->start, vp->end);
#endif
	}
}

__initfunc(static void assignment_walk_siblings(struct linux_pbm_info *pbm, int node))
{
	while(node) {
		int child = prom_getchild(node);
		if(child)
			assignment_walk_siblings(pbm, child);

		assignment_process(pbm, node);

		node = prom_getsibling(node);
	}
}

static inline void record_assignments(struct linux_pbm_info *pbm)
{
	struct pci_vma *vp;

	if (apb_present(pbm->parent)) {
		/*
		 * Disallow anything that is not in our IO/MEM map on SIMBA.
		 */
		struct pci_bus *pbus = pbm->parent->pci_bus;
		struct pci_dev *pdev;
		unsigned char map;
		int bit;

		for (pdev = pbus->devices; pdev; pdev = pdev->sibling) {
			struct pcidev_cookie *pcp = pdev->sysdata;
			if (!pcp)
				continue;
			if (pcp->pbm == pbm)
				break;
		}

		if (!pdev) {
			prom_printf("record_assignments: no pdev for PBM\n");
			prom_halt();
		}

		pci_read_config_byte(pdev, APB_IO_ADDRESS_MAP, &map);
#ifdef FIXUP_VMA_DEBUG
		dprintf("%s: IO   %02x\n", __FUNCTION__, map);
#endif
		for (bit = 0; bit < 8; bit++) {
			if (!(map & (1 << bit))) {
				vp = pci_vma_alloc();
				vp->start = (bit << 21);
				vp->end = vp->start + (1 << 21) - 1;
				vp->offset = 0;
				pci_add_vma(pbm, vp, 1);
#ifdef FIXUP_VMA_DEBUG
				dprintf("%s: IO   prealloc vma [%08x,%08x]\n",
					__FUNCTION__, vp->start, vp->end);
#endif
			}
		}
		pci_read_config_byte(pdev, APB_MEM_ADDRESS_MAP, &map);
#ifdef FIXUP_VMA_DEBUG
		dprintf("%s: MEM  %02x\n", __FUNCTION__, map);
#endif
		for (bit = 0; bit < 8; bit++) {
			if (!(map & (1 << bit))) {
				vp = pci_vma_alloc();
				vp->start = (bit << 29);
				vp->end = vp->start + (1 << 29) - 1;
				vp->offset = 0;
				pci_add_vma(pbm, vp, 0);
#ifdef FIXUP_VMA_DEBUG
				dprintf("%s: MEM  prealloc vma [%08x,%08x]\n",
					__FUNCTION__, vp->start, vp->end);
#endif
			}
		}
	}

	assignment_walk_siblings(pbm, prom_getchild(pbm->prom_node));

	/*
	 * Protect ISA IO space from being used.
	 */
	vp = pci_find_vma(pbm, 0, 0, 1);
	if (!vp || 0x400 <= vp->start) {
		vp = pci_vma_alloc();
		vp->start = 0;
		vp->end = vp->start + 0x400 - 1;
		vp->offset = 0;
		pci_add_vma(pbm, vp, 1);
	}

#ifdef FIXUP_VMA_DEBUG
	dprintf("PROM IO assignments for PBM %s:\n",
		pbm == &pbm->parent->pbm_A ? "A" : "B");
	vp = pbm->IO_assignments;
	while (vp) {
		dprintf("  [%08x,%08x] (%s)\n", vp->start, vp->end,
			vp->offset ? "Register" : "Unmapped");
		vp = vp->next;
	}
	dprintf("PROM MEM assignments for PBM %s:\n",
		pbm == &pbm->parent->pbm_A ? "A" : "B");
	vp = pbm->MEM_assignments;
	while (vp) {
		dprintf("  [%08x,%08x] (%s)\n", vp->start, vp->end,
			vp->offset ? "Register" : "Unmapped");
		vp = vp->next;
	}
#endif
}

__initfunc(static void fixup_regs(struct pci_dev *pdev,
				  struct linux_pbm_info *pbm,
				  struct linux_prom_pci_registers *pregs,
				  int nregs,
				  struct linux_prom_pci_registers *assigned,
				  int numaa))
{
	int preg, rng;
	int IO_seen = 0;
	int MEM_seen = 0;

	for(preg = 0; preg < nregs; preg++) {
		struct linux_prom_pci_registers *ap = NULL;
		int bustype = (pregs[preg].phys_hi >> 24) & 0x3;
		int bsreg, brindex;
		unsigned int rtmp;
		u64 pci_addr;

		if(bustype == 0) {
			/* Config space cookie, nothing to do. */
			if(preg != 0)
				printk("%s %02x.%02x [%04x,%04x]: "
				       "strange, config space not 0\n",
				       __FUNCTION__,
				       pdev->bus->number, pdev->devfn,
				       pdev->vendor, pdev->device);
			continue;
		} else if(bustype == 3) {
			/* XXX add support for this... */
			printk("%s %02x.%02x [%04x,%04x]: "
			       "Warning, ignoring 64-bit PCI memory space, "
			       "tell Eddie C. Dost (ecd@skynet.be).\n",
			       __FUNCTION__,
			       pdev->bus->number, pdev->devfn,
			       pdev->vendor, pdev->device);
			continue;
		}

		bsreg = (pregs[preg].phys_hi & 0xff);

		/* Sanity */
		if((bsreg < PCI_BASE_ADDRESS_0) ||
		   ((bsreg > (PCI_BASE_ADDRESS_5 + 4)) && (bsreg != PCI_ROM_ADDRESS)) ||
		   (bsreg & 3)) {
			printk("%s %02x.%02x [%04x:%04x]: "
			       "Warning, ignoring bogus basereg [%x]\n",
			       __FUNCTION__, pdev->bus->number, pdev->devfn,
			       pdev->vendor, pdev->device, bsreg);
			printk("  PROM reg: %08x.%08x.%08x %08x.%08x\n",
			       pregs[preg].phys_hi, pregs[preg].phys_mid,
			       pregs[preg].phys_lo, pregs[preg].size_hi,
			       pregs[preg].size_lo);
			continue;
		}

		brindex = (bsreg - PCI_BASE_ADDRESS_0) >> 2;
		if(numaa) {
			int r;

			for(r = 0; r < numaa; r++) {
				int abreg;

				abreg = (assigned[r].phys_hi & 0xff);
				if(abreg == bsreg) {
					ap = &assigned[r];
					break;
				}
			}
		}

		/* Now construct UPA physical address. */
		pci_addr  = (((u64)pregs[preg].phys_mid) << 32UL);
		pci_addr |= (((u64)pregs[preg].phys_lo));

		if(ap) {
			pci_addr += ((u64)ap->phys_lo);
			pci_addr += (((u64)ap->phys_mid) << 32UL);
		}

		/* Final step, apply PBM range. */
		for(rng = 0; rng < pbm->num_pbm_ranges; rng++) {
			struct linux_prom_pci_ranges *rp = &pbm->pbm_ranges[rng];
			int space = (rp->child_phys_hi >> 24) & 3;

			if(space == bustype) {
				pci_addr += ((u64)rp->parent_phys_lo);
				pci_addr += (((u64)rp->parent_phys_hi) << 32UL);
				break;
			}
		}
		if(rng == pbm->num_pbm_ranges) {
			/* AIEEE */
			prom_printf("fixup_doit: YIEEE, cannot find PBM ranges\n");
		}
		if (bsreg == PCI_ROM_ADDRESS) {
			pdev->rom_address = (unsigned long)__va(pci_addr);
			pdev->rom_address |= 1;
			/*
			 * Enable access to the ROM.
			 */
			pci_read_config_dword(pdev, PCI_ROM_ADDRESS, &rtmp);
			pci_write_config_dword(pdev, PCI_ROM_ADDRESS, rtmp | 1);
		} else
			pdev->base_address[brindex] = (unsigned long)__va(pci_addr);

		/* Preserve I/O space bit. */
		if(bustype == 0x1) {
			pdev->base_address[brindex] |= 1;
			IO_seen = 1;
		} else {
			MEM_seen = 1;
		}
	}

	/* Now handle assignments PROM did not take care of. */
	if(nregs) {
		unsigned int rtmp, ridx;
		unsigned int offset, base;
		struct pci_vma *vp;
		u64 pci_addr;
		int breg;

		for(breg = PCI_BASE_ADDRESS_0; breg <= PCI_BASE_ADDRESS_5; breg += 4) {
			int io;

			ridx = ((breg - PCI_BASE_ADDRESS_0) >> 2);
			base = (unsigned int)pdev->base_address[ridx];

			if(pdev->base_address[ridx] > PAGE_OFFSET)
				continue;

			io = (base & PCI_BASE_ADDRESS_SPACE)==PCI_BASE_ADDRESS_SPACE_IO;
			base &= ~((io ?
				   PCI_BASE_ADDRESS_IO_MASK :
				   PCI_BASE_ADDRESS_MEM_MASK));
			offset = (pdev->bus->number << 16) | (pdev->devfn << 8) | breg;
			vp = pci_find_vma(pbm, base, offset, io);
			if(!vp || vp->start > base) {
				unsigned int size, new_base;

				pci_read_config_dword(pdev, breg, &rtmp);
				pci_write_config_dword(pdev, breg, 0xffffffff);
				pci_read_config_dword(pdev, breg, &size);
				if(io)
					size &= ~1;
				size = (~(size) + 1);
				if(!size)
					continue;

				new_base = 0;
				for(vp = pci_find_vma(pbm, new_base, 0, io); ;
				    vp = vp->next) {
					if(!vp || new_base + size <= vp->start)
						break;
					new_base = (vp->end + (size - 1)) & ~(size-1);
				}
				if(vp && (new_base + size > vp->start)) {
					prom_printf("PCI: Impossible full %s space.\n",
						    (io ? "IO" : "MEM"));
					prom_halt();
				}
				vp = pci_vma_alloc();
				vp->start = new_base;
				vp->end = vp->start + size - 1;
				vp->offset = offset;

				pci_add_vma(pbm, vp, io);

#ifdef FIXUP_VMA_DEBUG
				dprintf("%02x.%02x.%x: BaseReg %02x",
					pdev->bus->number,
					PCI_SLOT(pdev->devfn),
					PCI_FUNC(pdev->devfn),
					breg);
				dprintf(" %s vma [%08x,%08x]\n",
					io ? "I/O" : breg == PCI_ROM_ADDRESS ? "ROM" : "MEM", vp->start, vp->end);
#endif
				rtmp = new_base;
				pci_read_config_dword(pdev, breg, &base);
				if(io)
					rtmp |= (base & ~PCI_BASE_ADDRESS_IO_MASK);
				else
					rtmp |= (base & ~PCI_BASE_ADDRESS_MEM_MASK);
				pci_write_config_dword(pdev, breg, rtmp);

				/* Apply PBM ranges and update pci_dev. */
				pci_addr = new_base;
				for(rng = 0; rng < pbm->num_pbm_ranges; rng++) {
					struct linux_prom_pci_ranges *rp;
					int rspace;

					rp = &pbm->pbm_ranges[rng];
					rspace = (rp->child_phys_hi >> 24) & 3;
					if(io && rspace != 1)
						continue;
					else if(!io && rspace != 2)
						continue;
					pci_addr += ((u64)rp->parent_phys_lo);
					pci_addr += (((u64)rp->parent_phys_hi)<<32UL);
					break;
				}
				if(rng == pbm->num_pbm_ranges) {
					/* AIEEE */
					prom_printf("fixup_doit: YIEEE, cannot find "
						    "PBM ranges\n");
				}
				pdev->base_address[ridx] = (unsigned long)__va(pci_addr);

				/* Preserve I/O space bit. */
				if(io) {
					pdev->base_address[ridx] |= 1;
					IO_seen = 1;
				} else {
					MEM_seen = 1;
				}
			}
		}

		/*
		 * Handle PCI_ROM_ADDRESS.
		 */
		breg = PCI_ROM_ADDRESS;
		base = (unsigned int)pdev->rom_address;

		if(pdev->rom_address > PAGE_OFFSET)
			goto rom_address_done;

		base &= PCI_ROM_ADDRESS_MASK;
		offset = (pdev->bus->number << 16) | (pdev->devfn << 8) | breg;
		vp = pci_find_vma(pbm, base, offset, 0);
		if(!vp || vp->start > base) {
			unsigned int size, new_base;

			pci_read_config_dword(pdev, breg, &rtmp);
			pci_write_config_dword(pdev, breg, 0xffffffff);
			pci_read_config_dword(pdev, breg, &size);
			size &= ~1;
			size = (~(size) + 1);
			if(!size)
				goto rom_address_done;

			new_base = 0;
			for(vp = pci_find_vma(pbm, new_base, 0, 0); ; vp = vp->next) {
				if(!vp || new_base + size <= vp->start)
					break;
				new_base = (vp->end + (size - 1)) & ~(size-1);
			}
			if(vp && (new_base + size > vp->start)) {
				prom_printf("PCI: Impossible full MEM space.\n");
				prom_halt();
			}
			vp = pci_vma_alloc();
			vp->start = new_base;
			vp->end = vp->start + size - 1;
			vp->offset = offset;

			pci_add_vma(pbm, vp, 0);

#ifdef FIXUP_VMA_DEBUG
			dprintf("%02x.%02x.%x: BaseReg %02x",
				pdev->bus->number,
				PCI_SLOT(pdev->devfn),
				PCI_FUNC(pdev->devfn),
				breg);
			dprintf(" %s vma [%08x,%08x]\n",
				"ROM", vp->start, vp->end);
#endif

			rtmp = new_base;
			pci_read_config_dword(pdev, breg, &base);
			rtmp |= (base & ~PCI_ROM_ADDRESS_MASK);
			pci_write_config_dword(pdev, breg, rtmp);

			/* Apply PBM ranges and update pci_dev. */
			pci_addr = new_base;
			for(rng = 0; rng < pbm->num_pbm_ranges; rng++) {
				struct linux_prom_pci_ranges *rp;
				int rspace;

				rp = &pbm->pbm_ranges[rng];
				rspace = (rp->child_phys_hi >> 24) & 3;
				if(rspace != 2)
					continue;
				pci_addr += ((u64)rp->parent_phys_lo);
				pci_addr += (((u64)rp->parent_phys_hi)<<32UL);
				break;
			}
			if(rng == pbm->num_pbm_ranges) {
				/* AIEEE */
				prom_printf("fixup_doit: YIEEE, cannot find "
					    "PBM ranges\n");
			}
			pdev->rom_address = (unsigned long)__va(pci_addr);

			pdev->rom_address |= (base & ~PCI_ROM_ADDRESS_MASK);
			MEM_seen = 1;
		}
	rom_address_done:

	}
	if(IO_seen || MEM_seen) {
		unsigned int l;

		pci_read_config_dword(pdev, PCI_COMMAND, &l);
#ifdef FIXUP_REGS_DEBUG
		dprintf("[");
#endif
		if(IO_seen) {
#ifdef FIXUP_REGS_DEBUG
			dprintf("IO ");
#endif
			l |= PCI_COMMAND_IO;
		}
		if(MEM_seen) {
#ifdef FIXUP_REGS_DEBUG
			dprintf("MEM");
#endif
			l |= PCI_COMMAND_MEMORY;
		}
#ifdef FIXUP_REGS_DEBUG
		dprintf("]");
#endif
		pci_write_config_dword(pdev, PCI_COMMAND, l);
	}

#ifdef FIXUP_REGS_DEBUG
	dprintf("REG_FIXUP[%04x,%04x]: ", pdev->vendor, pdev->device);
	for(preg = 0; preg < 6; preg++) {
		if(pdev->base_address[preg] != 0)
			dprintf("%d[%016lx] ", preg, pdev->base_address[preg]);
	}
	dprintf("\n");
#endif
}

#define imap_offset(__member) \
	((unsigned long)(&(((struct psycho_regs *)0)->__member)))

__initfunc(static unsigned long psycho_pcislot_imap_offset(unsigned long ino))
{
	unsigned int bus, slot;

	bus = (ino & 0x10) >> 4;
	slot = (ino & 0x0c) >> 2;

	if(bus == 0) {
		switch(slot) {
		case 0:
			return imap_offset(imap_a_slot0);
		case 1:
			return imap_offset(imap_a_slot1);
		case 2:
			return imap_offset(imap_a_slot2);
		case 3:
			return imap_offset(imap_a_slot3);
		default:
			prom_printf("pcislot_imap: IMPOSSIBLE [%d:%d]\n",
				    bus, slot);
			prom_halt();
		}
	} else {
		switch(slot) {
		case 0:
			return imap_offset(imap_b_slot0);
		case 1:
			return imap_offset(imap_b_slot1);
		case 2:
			return imap_offset(imap_b_slot2);
		case 3:
			return imap_offset(imap_b_slot3);
		default:
			prom_printf("pcislot_imap: IMPOSSIBLE [%d:%d]\n",
				    bus, slot);
			prom_halt();
		}
	}
}

/* Exported for EBUS probing layer. */
__initfunc(unsigned int psycho_irq_build(struct linux_pbm_info *pbm,
					 struct pci_dev *pdev,
					 unsigned int ino))
{
	unsigned long imap_off;
	int need_dma_sync = 0;

	ino &= PSYCHO_IMAP_INO;

	/* Compute IMAP register offset, generic IRQ layer figures out
	 * the ICLR register address as this is simple given the 32-bit
	 * irq number and IMAP register address.
	 */
	if((ino & 0x20) == 0)
		imap_off = psycho_pcislot_imap_offset(ino);
	else {
		switch(ino) {
		case 0x20:
			/* Onboard SCSI. */
			imap_off = imap_offset(imap_scsi);
			break;

		case 0x21:
			/* Onboard Ethernet (ie. CheerIO/HME) */
			imap_off = imap_offset(imap_eth);
			break;

		case 0x22:
			/* Onboard Parallel Port */
			imap_off = imap_offset(imap_bpp);
			break;

		case 0x23:
			/* Audio Record */
			imap_off = imap_offset(imap_au_rec);
			break;

		case 0x24:
			/* Audio Play */
			imap_off = imap_offset(imap_au_play);
			break;

		case 0x25:
			/* Power Fail */
			imap_off = imap_offset(imap_pfail);
			break;

		case 0x26:
			/* Onboard KBD/MOUSE/SERIAL */
			imap_off = imap_offset(imap_kms);
			break;

		case 0x27:
			/* Floppy (ie. fdthree) */
			imap_off = imap_offset(imap_flpy);
			break;

		case 0x28:
			/* Spare HW INT */
			imap_off = imap_offset(imap_shw);
			break;

		case 0x29:
			/* Onboard Keyboard (only) */
			imap_off = imap_offset(imap_kbd);
			break;

		case 0x2a:
			/* Onboard Mouse (only) */
			imap_off = imap_offset(imap_ms);
			break;

		case 0x2b:
			/* Onboard Serial (only) */
			imap_off = imap_offset(imap_ser);
			break;

		case 0x32:
			/* Power Management */
			imap_off = imap_offset(imap_pmgmt);
			break;

		default:
			/* We don't expect anything else.
			 */
			prom_printf("psycho_irq_build: Wacky INO [%x]\n", ino);
			prom_halt();
		};
	}
	imap_off -= imap_offset(imap_a_slot0);

	if (apb_present(pbm->parent) && (pdev->bus->number != pbm->pci_first_busno)) {
		need_dma_sync = 1;
	}

	return psycho_build_irq(pbm->parent, imap_off, ino, need_dma_sync);
}

__initfunc(static int pbm_intmap_match(struct linux_pbm_info *pbm,
				       struct pci_dev *pdev,
				       struct linux_prom_pci_registers *preg,
				       unsigned int *interrupt))
{
	struct linux_prom_pci_registers ppreg;
	unsigned int hi, mid, lo, irq;
	int i;

	if (!pbm->num_pbm_intmap)
		return 0;

	/*
	 * Underneath a bridge, use register of parent bridge.
	 */
	if (pdev->bus->number != pbm->pci_first_busno) {
		struct pcidev_cookie *pcp = pdev->bus->self->sysdata;
		int node;

		if (!pcp)
			goto out;

		node = pcp->prom_node;

		i = prom_getproperty(node, "reg", (char*)&ppreg, sizeof(ppreg));
		if(i == 0 || i == -1)
			goto out;

		/* Use low slot number bits of child as IRQ line. */
		*interrupt = ((pdev->devfn >> 3) & 3) + 1;

		preg = &ppreg;
	}

	hi = preg->phys_hi & pbm->pbm_intmask.phys_hi;
	mid = preg->phys_mid & pbm->pbm_intmask.phys_mid;
	lo = preg->phys_lo & pbm->pbm_intmask.phys_lo;
	irq = *interrupt & pbm->pbm_intmask.interrupt;
#ifdef FIXUP_IRQ_DEBUG
	dprintf("intmap_match: [%02x.%02x.%x] key: [%08x.%08x.%08x.%08x] ",
		pdev->bus->number, pdev->devfn >> 3, pdev->devfn & 7,
		hi, mid, lo, irq);
#endif
	for (i = 0; i < pbm->num_pbm_intmap; i++) {
		if ((pbm->pbm_intmap[i].phys_hi == hi) &&
		    (pbm->pbm_intmap[i].phys_mid == mid) &&
		    (pbm->pbm_intmap[i].phys_lo == lo) &&
		    (pbm->pbm_intmap[i].interrupt == irq)) {
#ifdef FIXUP_IRQ_DEBUG
			dprintf("irq: [%08x]", pbm->pbm_intmap[i].cinterrupt);
#endif
			*interrupt = pbm->pbm_intmap[i].cinterrupt;
			return 1;
		}
	}

out:
	prom_printf("pbm_intmap_match: bus %02x, devfn %02x: ",
		    pdev->bus->number, pdev->devfn);
	prom_printf("IRQ [%08x.%08x.%08x.%08x] not found in interrupt-map\n",
		    preg->phys_hi, preg->phys_mid, preg->phys_lo, *interrupt);
	prom_halt();
}

__initfunc(static void fixup_irq(struct pci_dev *pdev,
				 struct linux_pbm_info *pbm,
				 struct linux_prom_pci_registers *preg,
				 int node))
{
	unsigned int prom_irq, portid = pbm->parent->upa_portid;
	unsigned char pci_irq_line = pdev->irq;
	int err;

#ifdef FIXUP_IRQ_DEBUG
	dprintf("fixup_irq[%04x:%04x]: ", pdev->vendor, pdev->device);
#endif
	err = prom_getproperty(node, "interrupts", (void *)&prom_irq, sizeof(prom_irq));
	if(err == 0 || err == -1) {
#ifdef FIXUP_IRQ_DEBUG
		dprintf("No interrupts property.\n");
#endif
		pdev->irq = 0;
		return;
	}

	/* See if fully specified already (ie. for onboard devices like hme) */
	if(((prom_irq & PSYCHO_IMAP_IGN) >> 6) == pbm->parent->upa_portid) {
		pdev->irq = psycho_irq_build(pbm, pdev, prom_irq);
#ifdef FIXUP_IRQ_DEBUG
		dprintf("fully specified prom_irq[%x] pdev->irq[%x]",
		        prom_irq, pdev->irq);
#endif
	/* See if onboard device interrupt (i.e. bit 5 set) */
	} else if((prom_irq & PSYCHO_IMAP_INO) & 0x20) {
		pdev->irq = psycho_irq_build(pbm, pdev,
					     (pbm->parent->upa_portid << 6)
					     | prom_irq);
#ifdef FIXUP_IRQ_DEBUG
		dprintf("partially specified prom_irq[%x] pdev->irq[%x]",
		        prom_irq, pdev->irq);
#endif
	/* See if we find a matching interrupt-map entry. */
	} else if (pbm_intmap_match(pbm, pdev, preg, &prom_irq)) {
		pdev->irq = psycho_irq_build(pbm, pdev,
					     (pbm->parent->upa_portid << 6)
					     | prom_irq);
#ifdef FIXUP_IRQ_DEBUG
		dprintf("interrupt-map specified: prom_irq[%x] pdev->irq[%x]",
			prom_irq, pdev->irq);
#endif
	} else {
		unsigned int bus, slot, line;

		bus = (pbm == &pbm->parent->pbm_B) ? (1 << 4) : 0;
		line = (pci_irq_line) & 3;

		/* Slot determination is only slightly complex.  Handle
		 * the easy case first.
		 */
		if(pdev->bus->number == pbm->pci_first_busno) {
			if(pbm == &pbm->parent->pbm_A)
				slot = (pdev->devfn >> 3) - 1;
			else
				slot = (pdev->devfn >> 3) - 2;
		} else {
			/* Underneath a bridge, use slot number of parent
			 * bridge.
			 */
			if(pbm == &pbm->parent->pbm_A)
				slot = (pdev->bus->self->devfn >> 3) - 1;
			else
				slot = (pdev->bus->self->devfn >> 3) - 2;

			/* Use low slot number bits of child as IRQ line. */
			line = (pdev->devfn >> 3) & 0x03;
		}
		slot = (slot << 2);

		pdev->irq = psycho_irq_build(pbm, pdev,
					     (((portid << 6) & PSYCHO_IMAP_IGN)
					     | (bus | slot | line)));

#ifdef FIXUP_IRQ_DEBUG
		do {
			unsigned char iline, ipin;

			pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &ipin);
			pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &iline);
			dprintf("FIXED portid[%x] bus[%x] slot[%x] line[%x] "
				"irq[%x] iline[%x] ipin[%x] prom_irq[%x]",
			        portid, bus>>4, slot>>2, line, pdev->irq,
			        iline, ipin, prom_irq);
		} while(0);
#endif
	}

	/*
	 * Write the INO to config space PCI_INTERRUPT_LINE.
	 */
	pci_write_config_byte(pdev, PCI_INTERRUPT_LINE,
			      pdev->irq & PCI_IRQ_INO);

#ifdef FIXUP_IRQ_DEBUG
	dprintf("\n");
#endif
}

__initfunc(static void fixup_doit(struct pci_dev *pdev,
				  struct linux_pbm_info *pbm,
				  struct linux_prom_pci_registers *pregs,
				  int nregs,
				  int node))
{
	struct linux_prom_pci_registers assigned[PROMREG_MAX];
	int numaa, err;

	/* Get assigned addresses, if any. */
	err = prom_getproperty(node, "assigned-addresses",
			       (char *)&assigned[0], sizeof(assigned));
	if(err == 0 || err == -1)
		numaa = 0;
	else
		numaa = (err / sizeof(struct linux_prom_pci_registers));

	/* First, scan and fixup base registers. */
	fixup_regs(pdev, pbm, pregs, nregs, &assigned[0], numaa);

	/* Next, fixup interrupt numbers. */
	fixup_irq(pdev, pbm, &pregs[0], node);
}

__initfunc(static void fixup_pci_dev(struct pci_dev *pdev,
				     struct pci_bus *pbus,
				     struct linux_pbm_info *pbm))
{
	struct linux_prom_pci_registers pregs[PROMREG_MAX];
	struct pcidev_cookie *pcp = pdev->sysdata;
	int node, nregs, err;

	/* If this is a PCI bridge, we must program it. */
	if(pdev->class >> 8 == PCI_CLASS_BRIDGE_PCI) {
		unsigned short cmd;

		/* First, enable bus mastering. */
		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MASTER;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);

		/* Now, set cache line size to 64-bytes. */
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, 64);
	}

	/* Ignore if this is one of the PBM's, EBUS, or a
	 * sub-bridge underneath the PBM.  We only need to fixup
	 * true devices.
	 */
	if((pdev->class >> 8 == PCI_CLASS_BRIDGE_PCI) ||
	   (pdev->class >> 8 == PCI_CLASS_BRIDGE_HOST) ||
	   (pdev->class >> 8 == PCI_CLASS_BRIDGE_OTHER) ||
	   (pcp == NULL)) {
		/*
		 * Prevent access to PCI_ROM_ADDRESS, in case present
		 * as we don't fixup the address.
		 */
		if (pdev->rom_address) {
			pci_write_config_dword(pdev, PCI_ROM_ADDRESS, 0);
			pdev->rom_address = 0;
		}
		return;
	}

	node = pcp->prom_node;

	err = prom_getproperty(node, "reg", (char *)&pregs[0], sizeof(pregs));
	if(err == 0 || err == -1) {
		prom_printf("Cannot find REG for pci_dev\n");
		prom_halt();
	}

	nregs = (err / sizeof(pregs[0]));

	fixup_doit(pdev, pbm, &pregs[0], nregs, node);

	/* Enable bus mastering on IDE interfaces. */
	if ((pdev->class >> 8 == PCI_CLASS_STORAGE_IDE)
	    && (pdev->class & 0x80)) {
		unsigned short cmd;

		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MASTER;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
	}
}

__initfunc(static void fixup_pci_bus(struct pci_bus *pbus, struct linux_pbm_info *pbm))
{
	struct pci_dev *pdev;

	for(pdev = pbus->devices; pdev; pdev = pdev->sibling)
		fixup_pci_dev(pdev, pbus, pbm);

	for(pbus = pbus->children; pbus; pbus = pbus->next)
		fixup_pci_bus(pbus, pbm);
}

__initfunc(static void fixup_addr_irq(struct linux_pbm_info *pbm))
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
__initfunc(static void psycho_final_fixup(struct linux_psycho *psycho))
{
	/* Second, fixup base address registers and IRQ lines... */
	if (psycho->pbm_A.parent)
		fixup_addr_irq(&psycho->pbm_A);
	if (psycho->pbm_B.parent)
		fixup_addr_irq(&psycho->pbm_B);
}

__initfunc(void pcibios_fixup(void))
{
	struct linux_psycho *psycho;

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

	for (psycho = psycho_root; psycho; psycho = psycho->next) {
		/* Probe bus on builtin PCI. */
		if (apb_present(psycho))
			sabre_probe(psycho);
		else {
			/* Probe busses under PBM B. */
			pbm_probe(&psycho->pbm_B);

			/* Probe busses under PBM A. */
			pbm_probe(&psycho->pbm_A);
		}
	}

	/* Walk all PCI devices found.  For each device, and
	 * PCI bridge which is not one of the PSYCHO PBM's, fill in the
	 * sysdata with a pointer to the PBM (for pci_bus's) or
	 * a pci_dev cookie (PBM+PROM_NODE, for pci_dev's).
	 */
	for (psycho = psycho_root; psycho; psycho = psycho->next) {
		if (apb_present(psycho))
			sabre_cookie_fillin(psycho);

		fill_in_pbm_cookies(&psycho->pbm_A.pci_bus,
				    &psycho->pbm_A,
				    psycho->pbm_A.prom_node);
		fill_in_pbm_cookies(&psycho->pbm_B.pci_bus,
				    &psycho->pbm_B,
				    psycho->pbm_B.prom_node);

		/* See what OBP has taken care of already. */
		record_assignments(&psycho->pbm_A);
		record_assignments(&psycho->pbm_B);

		/* Now, fix it all up. */
		psycho_final_fixup(psycho);
	}

	return ebus_init();
}

/* "PCI: The emerging standard..." 8-( */
volatile int pci_poke_in_progress = 0;
volatile int pci_poke_faulted = 0;

/* XXX Current PCI support code is broken, it assumes one master PCI config
 * XXX space exists, on Ultra we can have many of them, especially with
 * XXX 'dual-pci' boards on Sunfire/Starfire/Wildfire.
 */
static void *
pci_mkaddr(struct linux_pbm_info *pbm, unsigned char bus,
	   unsigned char devfn, unsigned char where)
{
	unsigned long ret;

	if (!pbm)
		return NULL;

	ret = (unsigned long) pbm->parent->pci_config_space;

	ret |= (1 << 24);
	ret |= (bus << 16);
	ret |= (devfn << 8);
	ret |= where;

	return (void *)ret;
}

static inline int
out_of_range(struct linux_pbm_info *pbm, unsigned char bus, unsigned char devfn)
{
	return ((pbm->parent == 0) ||
		((pbm == &pbm->parent->pbm_B) && (bus == pbm->pci_first_busno) && PCI_SLOT(devfn) > 8) ||
		((pbm == &pbm->parent->pbm_A) && (bus == pbm->pci_first_busno) && PCI_SLOT(devfn) > 8) ||
		(pci_probe_enable == 0));
}

static inline int
sabre_out_of_range(unsigned char devfn)
{
	return ((PCI_SLOT(devfn) == 0) && (PCI_FUNC(devfn) > 0)) ||
	       ((PCI_SLOT(devfn) == 1) && (PCI_FUNC(devfn) > 1)) ||
	       (PCI_SLOT(devfn) > 1);
}

static int
sabre_read_config_byte(struct linux_pbm_info *pbm,
		       unsigned char bus, unsigned char devfn,
		       unsigned char where, unsigned char *value)
{
	if (bus)
		return pbm_read_config_byte(pbm, bus, devfn, where, value);

	if (sabre_out_of_range(devfn)) {
		*value = 0xff;
		return PCIBIOS_SUCCESSFUL;
	}

	if (where < 8) {
		unsigned short tmp;

		pbm_read_config_word(pbm, bus, devfn, where & ~1, &tmp);
		if (where & 1)
			*value = tmp >> 8;
		else
			*value = tmp & 0xff;
		return PCIBIOS_SUCCESSFUL;
	} else
		return pbm_read_config_byte(pbm, bus, devfn, where, value);
}

static int
sabre_read_config_word(struct linux_pbm_info *pbm,
		       unsigned char bus, unsigned char devfn,
		       unsigned char where, unsigned short *value)
{
	if (bus)
		return pbm_read_config_word(pbm, bus, devfn, where, value);

	if (sabre_out_of_range(devfn)) {
		*value = 0xffff;
		return PCIBIOS_SUCCESSFUL;
	}

	if (where < 8)
		return pbm_read_config_word(pbm, bus, devfn, where, value);
	else {
		unsigned char tmp;

		pbm_read_config_byte(pbm, bus, devfn, where, &tmp);
		*value = tmp;
		pbm_read_config_byte(pbm, bus, devfn, where + 1, &tmp);
		*value |= tmp << 8;
		return PCIBIOS_SUCCESSFUL;
	}
}

static int
sabre_read_config_dword(struct linux_pbm_info *pbm,
			unsigned char bus, unsigned char devfn,
			unsigned char where, unsigned int *value)
{
	unsigned short tmp;

	if (bus)
		return pbm_read_config_dword(pbm, bus, devfn, where, value);

	if (sabre_out_of_range(devfn)) {
		*value = 0xffffffff;
		return PCIBIOS_SUCCESSFUL;
	}

	sabre_read_config_word(pbm, bus, devfn, where, &tmp);
	*value = tmp;
	sabre_read_config_word(pbm, bus, devfn, where + 2, &tmp);
	*value |= tmp << 16;
	return PCIBIOS_SUCCESSFUL;
}

static int
sabre_write_config_byte(struct linux_pbm_info *pbm,
			unsigned char bus, unsigned char devfn,
			unsigned char where, unsigned char value)
{
	if (bus)
		return pbm_write_config_byte(pbm, bus, devfn, where, value);

	if (sabre_out_of_range(devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where < 8) {
		unsigned short tmp;

		pbm_read_config_word(pbm, bus, devfn, where & ~1, &tmp);
		if (where & 1) {
			value &= 0x00ff;
			value |= tmp << 8;
		} else {
			value &= 0xff00;
			value |= tmp;
		}
		return pbm_write_config_word(pbm, bus, devfn, where & ~1, tmp);
	} else
		return pbm_write_config_byte(pbm, bus, devfn, where, value);
}

static int
sabre_write_config_word(struct linux_pbm_info *pbm,
			unsigned char bus, unsigned char devfn,
			unsigned char where, unsigned short value)
{
	if (bus)
		return pbm_write_config_word(pbm, bus, devfn, where, value);

	if (sabre_out_of_range(devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where < 8)
		return pbm_write_config_word(pbm, bus, devfn, where, value);
	else {
		pbm_write_config_byte(pbm, bus, devfn, where, value & 0xff);
		pbm_write_config_byte(pbm, bus, devfn, where + 1, value >> 8);
		return PCIBIOS_SUCCESSFUL;
	}
}

static int
sabre_write_config_dword(struct linux_pbm_info *pbm,
			 unsigned char bus, unsigned char devfn,
			 unsigned char where, unsigned int value)
{
	if (bus)
		return pbm_write_config_dword(pbm, bus, devfn, where, value);

	if (sabre_out_of_range(devfn))
		return PCIBIOS_SUCCESSFUL;

	sabre_write_config_word(pbm, bus, devfn, where, value & 0xffff);
	sabre_write_config_word(pbm, bus, devfn, where + 2, value >> 16);
	return PCIBIOS_SUCCESSFUL;
}

static int
pbm_read_config_byte(struct linux_pbm_info *pbm,
		     unsigned char bus, unsigned char devfn,
		     unsigned char where, unsigned char *value)
{
	unsigned char *addr = pci_mkaddr(pbm, bus, devfn, where);
	unsigned int trapped;
	unsigned char byte;

	*value = 0xff;

	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduba [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (byte)
			     : "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	trapped = pci_poke_faulted;
	pci_poke_faulted = 0;
	if(!trapped)
		*value = byte;
	return PCIBIOS_SUCCESSFUL;
}

static int
pbm_read_config_word(struct linux_pbm_info *pbm,
		     unsigned char bus, unsigned char devfn,
		     unsigned char where, unsigned short *value)
{
	unsigned short *addr = pci_mkaddr(pbm, bus, devfn, where);
	unsigned int trapped;
	unsigned short word;

	*value = 0xffff;

	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where & 0x01) {
		printk("pcibios_read_config_word: misaligned reg [%x]\n",
		       where);
		return PCIBIOS_SUCCESSFUL;
	}

	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduha [%1] %2, %0\n\t"
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

static int
pbm_read_config_dword(struct linux_pbm_info *pbm,
		      unsigned char bus, unsigned char devfn,
		      unsigned char where, unsigned int *value)
{
	unsigned int *addr = pci_mkaddr(pbm, bus, devfn, where);
	unsigned int word, trapped;

	*value = 0xffffffff;

	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where & 0x03) {
		printk("pcibios_read_config_dword: misaligned reg [%x]\n",
		       where);
		return PCIBIOS_SUCCESSFUL;
	}

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

static int
pbm_write_config_byte(struct linux_pbm_info *pbm,
		      unsigned char bus, unsigned char devfn,
		      unsigned char where, unsigned char value)
{
	unsigned char *addr = pci_mkaddr(pbm, bus, devfn, where);

	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

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

static int
pbm_write_config_word(struct linux_pbm_info *pbm,
		      unsigned char bus, unsigned char devfn,
		      unsigned char where, unsigned short value)
{
	unsigned short *addr = pci_mkaddr(pbm, bus, devfn, where);

	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where & 0x01) {
		printk("pcibios_write_config_word: misaligned reg [%x]\n",
		       where);
		return PCIBIOS_SUCCESSFUL;
	}

	pci_poke_in_progress = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stha %0, [%1] %2\n\t"
			     "membar #Sync\n\t"
			     : /* no outputs */
			     : "r" (value), "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	return PCIBIOS_SUCCESSFUL;
}

static int
pbm_write_config_dword(struct linux_pbm_info *pbm,
		       unsigned char bus, unsigned char devfn,
		       unsigned char where, unsigned int value)
{
	unsigned int *addr = pci_mkaddr(pbm, bus, devfn, where);

	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where & 0x03) {
		printk("pcibios_write_config_dword: misaligned reg [%x]\n",
		       where);
		return PCIBIOS_SUCCESSFUL;
	}

	pci_poke_in_progress = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stwa %0, [%1] %2\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (value), "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_byte (unsigned char bus, unsigned char devfn,
			      unsigned char where, unsigned char *value)
{
	struct linux_pbm_info *pbm = bus2pbm[bus];

	if (pbm && pbm->parent && apb_present(pbm->parent))
		return sabre_read_config_byte(pbm, bus, devfn, where, value);
	return pbm_read_config_byte(pbm, bus, devfn, where, value);
}

int pcibios_read_config_word (unsigned char bus, unsigned char devfn,
			      unsigned char where, unsigned short *value)
{
	struct linux_pbm_info *pbm = bus2pbm[bus];

	if (pbm && pbm->parent && apb_present(pbm->parent))
		return sabre_read_config_word(pbm, bus, devfn, where, value);
	return pbm_read_config_word(pbm, bus, devfn, where, value);
}

int pcibios_read_config_dword (unsigned char bus, unsigned char devfn,
			       unsigned char where, unsigned int *value)
{
	struct linux_pbm_info *pbm = bus2pbm[bus];

	if (pbm && pbm->parent && apb_present(pbm->parent))
		return sabre_read_config_dword(pbm, bus, devfn, where, value);
	return pbm_read_config_dword(pbm, bus, devfn, where, value);
}

int pcibios_write_config_byte (unsigned char bus, unsigned char devfn,
			       unsigned char where, unsigned char value)
{
	struct linux_pbm_info *pbm = bus2pbm[bus];

	if (pbm && pbm->parent && apb_present(pbm->parent))
		return sabre_write_config_byte(pbm, bus, devfn, where, value);
	return pbm_write_config_byte(pbm, bus, devfn, where, value);
}

int pcibios_write_config_word (unsigned char bus, unsigned char devfn,
			       unsigned char where, unsigned short value)
{
	struct linux_pbm_info *pbm = bus2pbm[bus];

	if (pbm && pbm->parent && apb_present(pbm->parent))
		return sabre_write_config_word(pbm, bus, devfn, where, value);
	return pbm_write_config_word(bus2pbm[bus], bus, devfn, where, value);
}

int pcibios_write_config_dword (unsigned char bus, unsigned char devfn,
			        unsigned char where, unsigned int value)
{
	struct linux_pbm_info *pbm = bus2pbm[bus];

	if (pbm && pbm->parent && apb_present(pbm->parent))
		return sabre_write_config_dword(pbm, bus, devfn, where, value);
	return pbm_write_config_dword(bus2pbm[bus], bus, devfn, where, value);
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

	if(!capable(CAP_SYS_ADMIN))
		return -EPERM;

	lock_kernel();
	switch(len) {
	case 1:
		pcibios_read_config_byte(bus, dfn, off, &ubyte);
		put_user(ubyte, (unsigned char *)buf);
		break;
	case 2:
		pcibios_read_config_word(bus, dfn, off, &ushort);
		put_user(ushort, (unsigned short *)buf);
		break;
	case 4:
		pcibios_read_config_dword(bus, dfn, off, &uint);
		put_user(uint, (unsigned int *)buf);
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

	if(!capable(CAP_SYS_ADMIN))
		return -EPERM;

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

__initfunc(void pcibios_fixup_bus(struct pci_bus *bus))
{
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}

#endif

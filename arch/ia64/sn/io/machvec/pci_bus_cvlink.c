/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/pci/pci_bus_cvlink.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/simulator.h>

extern int bridge_rev_b_data_check_disable;

vertex_hdl_t busnum_to_pcibr_vhdl[MAX_PCI_XWIDGET];
nasid_t busnum_to_nid[MAX_PCI_XWIDGET];
void * busnum_to_atedmamaps[MAX_PCI_XWIDGET];
unsigned char num_bridges;
static int done_probing;
extern irqpda_t *irqpdaindr;

static int pci_bus_map_create(struct pcibr_list_s *softlistp, moduleid_t io_moduleid);
vertex_hdl_t devfn_to_vertex(unsigned char busnum, unsigned int devfn);

extern void register_pcibr_intr(int irq, pcibr_intr_t intr);

static void sn_dma_flush_init(unsigned long start, unsigned long end, int idx, int pin, int slot);
extern int cbrick_type_get_nasid(nasid_t);
extern void ioconfig_bus_new_entries(void);
extern void ioconfig_get_busnum(char *, int *);
extern int iomoduleid_get(nasid_t);
extern int pcibr_widget_to_bus(vertex_hdl_t);
extern int isIO9(int);

#define IS_OPUS(nasid) (cbrick_type_get_nasid(nasid) == MODULE_OPUSBRICK)
#define IS_ALTIX(nasid) (cbrick_type_get_nasid(nasid) == MODULE_CBRICK)

/*
 * Init the provider asic for a given device
 */

static inline void __init
set_pci_provider(struct sn_device_sysdata *device_sysdata)
{
	pciio_info_t pciio_info = pciio_info_get(device_sysdata->vhdl);

	device_sysdata->pci_provider = pciio_info_pops_get(pciio_info);
}

/*
 * pci_bus_cvlink_init() - To be called once during initialization before 
 *	SGI IO Infrastructure init is called.
 */
int
pci_bus_cvlink_init(void)
{

	extern int ioconfig_bus_init(void);

	memset(busnum_to_pcibr_vhdl, 0x0, sizeof(vertex_hdl_t) * MAX_PCI_XWIDGET);
	memset(busnum_to_nid, 0x0, sizeof(nasid_t) * MAX_PCI_XWIDGET);

	memset(busnum_to_atedmamaps, 0x0, sizeof(void *) * MAX_PCI_XWIDGET);

	num_bridges = 0;

	return ioconfig_bus_init();
}

/*
 * pci_bus_to_vertex() - Given a logical Linux Bus Number returns the associated 
 *	pci bus vertex from the SGI IO Infrastructure.
 */
static inline vertex_hdl_t
pci_bus_to_vertex(unsigned char busnum)
{

	vertex_hdl_t	pci_bus = NULL;


	/*
	 * First get the xwidget vertex.
	 */
	pci_bus = busnum_to_pcibr_vhdl[busnum];
	return(pci_bus);
}

/*
 * devfn_to_vertex() - returns the vertex of the device given the bus, slot, 
 *	and function numbers.
 */
vertex_hdl_t
devfn_to_vertex(unsigned char busnum, unsigned int devfn)
{

	int slot = 0;
	int func = 0;
	char	name[16];
	vertex_hdl_t  pci_bus = NULL;
	vertex_hdl_t	device_vertex = (vertex_hdl_t)NULL;

	/*
	 * Go get the pci bus vertex.
	 */
	pci_bus = pci_bus_to_vertex(busnum);
	if (!pci_bus) {
		/*
		 * During probing, the Linux pci code invents non-existent
		 * bus numbers and pci_dev structures and tries to access
		 * them to determine existence. Don't crib during probing.
		 */
		if (done_probing)
			printk("devfn_to_vertex: Invalid bus number %d given.\n", busnum);
		return(NULL);
	}


	/*
	 * Go get the slot&function vertex.
	 * Should call pciio_slot_func_to_name() when ready.
	 */
	slot = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/*
	 * For a NON Multi-function card the name of the device looks like:
	 * ../pci/1, ../pci/2 ..
	 */
	if (func == 0) {
        	sprintf(name, "%d", slot);
		if (hwgraph_traverse(pci_bus, name, &device_vertex) == 
			GRAPH_SUCCESS) {
			if (device_vertex) {
				return(device_vertex);
			}
		}
	}
			
	/*
	 * This maybe a multifunction card.  It's names look like:
	 * ../pci/1a, ../pci/1b, etc.
	 */
	sprintf(name, "%d%c", slot, 'a'+func);
	if (hwgraph_traverse(pci_bus, name, &device_vertex) != GRAPH_SUCCESS) {
		if (!device_vertex) {
			return(NULL);
		}
	}

	return(device_vertex);
}

struct sn_flush_nasid_entry flush_nasid_list[MAX_NASIDS];

/* Initialize the data structures for flushing write buffers after a PIO read.
 * The theory is: 
 * Take an unused int. pin and associate it with a pin that is in use.
 * After a PIO read, force an interrupt on the unused pin, forcing a write buffer flush
 * on the in use pin.  This will prevent the race condition between PIO read responses and 
 * DMA writes.
 */
static void
sn_dma_flush_init(unsigned long start, unsigned long end, int idx, int pin, int slot)
{
	nasid_t nasid; 
	unsigned long dnasid;
	int wid_num;
	int bus;
	struct sn_flush_device_list *p;
	void *b;
	int bwin;
	int i;

	nasid = NASID_GET(start);
	wid_num = SWIN_WIDGETNUM(start);
	bus = (start >> 23) & 0x1;
	bwin = BWIN_WINDOWNUM(start);

	if (flush_nasid_list[nasid].widget_p == NULL) {
		flush_nasid_list[nasid].widget_p = (struct sn_flush_device_list **)kmalloc((HUB_WIDGET_ID_MAX+1) *
			sizeof(struct sn_flush_device_list *), GFP_KERNEL);
		if (!flush_nasid_list[nasid].widget_p) {
			printk(KERN_WARNING "sn_dma_flush_init: Cannot allocate memory for nasid list\n");
			return;
		}
		memset(flush_nasid_list[nasid].widget_p, 0, (HUB_WIDGET_ID_MAX+1) * sizeof(struct sn_flush_device_list *));
	}
	if (bwin > 0) {
		int itte_index = bwin - 1;
		unsigned long itte;

		itte = HUB_L(IIO_ITTE_GET(nasid, itte_index));
		flush_nasid_list[nasid].iio_itte[bwin] = itte;
		wid_num = (itte >> IIO_ITTE_WIDGET_SHIFT) & 
			  IIO_ITTE_WIDGET_MASK;
		bus = itte & IIO_ITTE_OFFSET_MASK;
		if (bus == 0x4 || bus == 0x8) {
			bus = 0;
		} else {
			bus = 1;
		}
	}

	/* if it's IO9, bus 1, we don't care about slots 1 and 4.  This is
	 * because these are the IOC4 slots and we don't flush them.
	 */
	if (isIO9(nasid) && bus == 0 && (slot == 1 || slot == 4)) {
		return;
	}
	if (flush_nasid_list[nasid].widget_p[wid_num] == NULL) {
		flush_nasid_list[nasid].widget_p[wid_num] = (struct sn_flush_device_list *)kmalloc(
			DEV_PER_WIDGET * sizeof (struct sn_flush_device_list), GFP_KERNEL);
		if (!flush_nasid_list[nasid].widget_p[wid_num]) {
			printk(KERN_WARNING "sn_dma_flush_init: Cannot allocate memory for nasid sub-list\n");
			return;
		}
		memset(flush_nasid_list[nasid].widget_p[wid_num], 0, 
			DEV_PER_WIDGET * sizeof (struct sn_flush_device_list));
		p = &flush_nasid_list[nasid].widget_p[wid_num][0];
		for (i=0; i<DEV_PER_WIDGET;i++) {
			p->bus = -1;
			p->pin = -1;
			p->slot = -1;
			p++;
		}
	}

	p = &flush_nasid_list[nasid].widget_p[wid_num][0];
	for (i=0;i<DEV_PER_WIDGET; i++) {
		if (p->pin == pin && p->bus == bus && p->slot == slot) break;
		if (p->pin < 0) {
			p->pin = pin;
			p->bus = bus;
			p->slot = slot;
			break;
		}
		p++;
	}

	for (i=0; i<PCI_ROM_RESOURCE; i++) {
		if (p->bar_list[i].start == 0) {
			p->bar_list[i].start = start;
			p->bar_list[i].end = end;
			break;
		}
	}
	b = (void *)(NODE_SWIN_BASE(nasid, wid_num) | (bus << 23) );

	/* If it's IO9, then slot 2 maps to slot 7 and slot 6 maps to slot 8.
	 * To see this is non-trivial.  By drawing pictures and reading manuals and talking
	 * to HW guys, we can see that on IO9 bus 1, slots 7 and 8 are always unused.
	 * Further, since we short-circuit slots  1, 3, and 4 above, we only have to worry
	 * about the case when there is a card in slot 2.  A multifunction card will appear
	 * to be in slot 6 (from an interrupt point of view) also.  That's the  most we'll
	 * have to worry about.  A four function card will overload the interrupt lines in
	 * slot 2 and 6.  
	 * We also need to special case the 12160 device in slot 3.  Fortunately, we have
	 * a spare intr. line for pin 4, so we'll use that for the 12160.
	 * All other buses have slot 3 and 4 and slots 7 and 8 unused.  Since we can only
	 * see slots 1 and 2 and slots 5 and 6 coming through here for those buses (this
	 * is true only on Pxbricks with 2 physical slots per bus), we just need to add
	 * 2 to the slot number to find an unused slot.
	 * We have convinced ourselves that we will never see a case where two different cards
	 * in two different slots will ever share an interrupt line, so there is no need to
	 * special case this.
	 */

	if (isIO9(nasid) && ( (IS_ALTIX(nasid) && wid_num == 0xc)
				|| (IS_OPUS(nasid) && wid_num == 0xf) )
				&& bus == 0) {
		if (slot == 2) {
			p->force_int_addr = (unsigned long)pcireg_bridge_force_always_addr_get(b, 6);
			pcireg_bridge_intr_device_bit_set(b, (1<<18));
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			pcireg_bridge_intr_addr_set(b, 6, ((virt_to_phys(&p->flush_addr) & 0xfffffffff) |
						    (dnasid << 36) | (0xfUL << 48)));
		} else  if (slot == 3) { /* 12160 SCSI device in IO9 */
			p->force_int_addr = (unsigned long)pcireg_bridge_force_always_addr_get(b, 4);
			pcireg_bridge_intr_device_bit_set(b, (2<<12));
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			pcireg_bridge_intr_addr_set(b, 4, ((virt_to_phys(&p->flush_addr) & 0xfffffffff) |
						    (dnasid << 36) | (0xfUL << 48)));
		} else { /* slot == 6 */
			p->force_int_addr = (unsigned long)pcireg_bridge_force_always_addr_get(b, 7);
			pcireg_bridge_intr_device_bit_set(b, (5<<21));
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			pcireg_bridge_intr_addr_set(b, 7, ((virt_to_phys(&p->flush_addr) & 0xfffffffff) |
						    (dnasid << 36) | (0xfUL << 48)));
		}
	} else {
		p->force_int_addr = (unsigned long)pcireg_bridge_force_always_addr_get(b, (pin +2));
		pcireg_bridge_intr_device_bit_set(b, ((slot - 1) << ( pin * 3)));
		dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
		pcireg_bridge_intr_addr_set(b, (pin + 2), ((virt_to_phys(&p->flush_addr) & 0xfffffffff) |
						    (dnasid << 36) | (0xfUL << 48)));
	}
}

/*
 * sn_pci_fixup() - This routine is called when platform_pci_fixup() is 
 *	invoked at the end of pcibios_init() to link the Linux pci 
 *	infrastructure to SGI IO Infrasturcture - ia64/kernel/pci.c
 *
 *	Other platform specific fixup can also be done here.
 */
static void __init
sn_pci_fixup(int arg)
{
	struct list_head *ln;
	struct pci_bus *pci_bus = NULL;
	struct pci_dev *device_dev = NULL;
	struct sn_widget_sysdata *widget_sysdata;
	struct sn_device_sysdata *device_sysdata;
	pcibr_intr_t intr_handle;
	pciio_provider_t *pci_provider;
	vertex_hdl_t device_vertex;
	pciio_intr_line_t lines;
	extern int numnodes;
	int cnode;

	if (arg == 0) {
#ifdef CONFIG_PROC_FS
		extern void register_sn_procfs(void);
#endif
		extern void sgi_master_io_infr_init(void);
		extern void sn_init_cpei_timer(void);
		
		sgi_master_io_infr_init();
		
		for (cnode = 0; cnode < numnodes; cnode++) {
			extern void intr_init_vecblk(cnodeid_t);
			intr_init_vecblk(cnode);
		} 

		sn_init_cpei_timer();

#ifdef CONFIG_PROC_FS
		register_sn_procfs();
#endif
		return;
	}


	done_probing = 1;

	/*
	 * Initialize the pci bus vertex in the pci_bus struct.
	 */
	for( ln = pci_root_buses.next; ln != &pci_root_buses; ln = ln->next) {
		pci_bus = pci_bus_b(ln);
		widget_sysdata = kmalloc(sizeof(struct sn_widget_sysdata), 
					GFP_KERNEL);
		if (!widget_sysdata) {
			printk(KERN_WARNING "sn_pci_fixup(): Unable to "
			       "allocate memory for widget_sysdata\n");
			return;
		}			
		widget_sysdata->vhdl = pci_bus_to_vertex(pci_bus->number);
		pci_bus->sysdata = (void *)widget_sysdata;
	}

	/*
 	 * set the root start and end so that drivers calling check_region()
	 * won't see a conflict
	 */

#ifdef CONFIG_IA64_SGI_SN_SIM
	if (! IS_RUNNING_ON_SIMULATOR()) {
		ioport_resource.start  = 0xc000000000000000;
		ioport_resource.end =    0xcfffffffffffffff;
	}
#endif

	/*
	 * Set the root start and end for Mem Resource.
	 */
	iomem_resource.start = 0;
	iomem_resource.end = 0xffffffffffffffff;

	/*
	 * Initialize the device vertex in the pci_dev struct.
	 */
	while ((device_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, device_dev)) != NULL) {
		unsigned int irq;
		int idx;
		u16 cmd;
		vertex_hdl_t vhdl;
		unsigned long size;
		extern int bit_pos_to_irq(int);

		/* Set the device vertex */

		device_sysdata = kmalloc(sizeof(struct sn_device_sysdata),
					 GFP_KERNEL);
		if (!device_sysdata) {
			printk(KERN_WARNING "sn_pci_fixup: Cannot allocate memory for device sysdata\n");
			return;
		}

		device_sysdata->vhdl = devfn_to_vertex(device_dev->bus->number, device_dev->devfn);
		device_dev->sysdata = (void *) device_sysdata;
		set_pci_provider(device_sysdata);

		pci_read_config_word(device_dev, PCI_COMMAND, &cmd);

		/*
		 * Set the resources address correctly.  The assumption here 
		 * is that the addresses in the resource structure has been
		 * read from the card and it was set in the card by our
		 * Infrastructure ..
		 */
		vhdl = device_sysdata->vhdl;
		/* Allocate the IORESOURCE_IO space first */
		for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
			unsigned long start, end, addr;

			if (!(device_dev->resource[idx].flags & IORESOURCE_IO))
				continue; 
			
			start = device_dev->resource[idx].start;
			end = device_dev->resource[idx].end;
			size = end - start;
			if (!size)
				continue; 
			
			addr = (unsigned long)pciio_pio_addr(vhdl, 0, 
					PCIIO_SPACE_WIN(idx), 0, size, 0, 0);
			if (!addr) {
				device_dev->resource[idx].start = 0;
				device_dev->resource[idx].end = 0;
				printk("sn_pci_fixup(): pio map failure for "
				    "%s bar%d\n", device_dev->slot_name, idx);
			} else {
				addr |= __IA64_UNCACHED_OFFSET;
				device_dev->resource[idx].start = addr;
				device_dev->resource[idx].end = addr + size;
			}	

			if (device_dev->resource[idx].flags & IORESOURCE_IO) 
				cmd |= PCI_COMMAND_IO; 
		} 

		/* Allocate the IORESOURCE_MEM space next */
		for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
			unsigned long start, end, addr;

			if ((device_dev->resource[idx].flags & IORESOURCE_IO))
				continue; 

			start = device_dev->resource[idx].start;
			end = device_dev->resource[idx].end;
			size = end - start;
			if (!size)
				continue; 

			addr = (unsigned long)pciio_pio_addr(vhdl, 0, 
					PCIIO_SPACE_WIN(idx), 0, size, 0, 0);
			if (!addr) {
				device_dev->resource[idx].start = 0;
				device_dev->resource[idx].end = 0;
				printk("sn_pci_fixup(): pio map failure for "
				    "%s bar%d\n", device_dev->slot_name, idx);
			} else {
				addr |= __IA64_UNCACHED_OFFSET;
				device_dev->resource[idx].start = addr;
				device_dev->resource[idx].end = addr + size;
			}	

			if (device_dev->resource[idx].flags & IORESOURCE_MEM)
				cmd |= PCI_COMMAND_MEMORY;
		}

		/*
		 * Update the Command Word on the Card.
		 */
		cmd |= PCI_COMMAND_MASTER; /* If the device doesn't support */
					   /* bit gets dropped .. no harm */
		pci_write_config_word(device_dev, PCI_COMMAND, cmd);

		pci_read_config_byte(device_dev, PCI_INTERRUPT_PIN,
				     (unsigned char *)&lines);
		device_sysdata = (struct sn_device_sysdata *)device_dev->sysdata;
		device_vertex = device_sysdata->vhdl;
		pci_provider = device_sysdata->pci_provider;
 
		irqpdaindr->curr = device_dev;
		intr_handle = (pci_provider->intr_alloc)(device_vertex, NULL, lines, device_vertex);

		irq = intr_handle->bi_irq;
		irqpdaindr->device_dev[irq] = device_dev;
		(pci_provider->intr_connect)(intr_handle, (intr_func_t)0, (intr_arg_t)0);
		device_dev->irq = irq;
		register_pcibr_intr(irq, intr_handle);

		for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
			int ibits = intr_handle->bi_ibits;
			int i;

			size = device_dev->resource[idx].end -
				device_dev->resource[idx].start;
			if (size == 0)
				continue;

			for (i=0; i<8; i++) {
				if (ibits & (1 << i) ) {
					sn_dma_flush_init(device_dev->resource[idx].start, 
							device_dev->resource[idx].end,
							idx,
							i,
							PCI_SLOT(device_dev->devfn));
				}
			}
		}

	}
}

/*
 * linux_bus_cvlink() Creates a link between the Linux PCI Bus number 
 *	to the actual hardware component that it represents:
 *	/dev/hw/linux/busnum/0 -> ../../../hw/module/001c01/slab/0/Ibrick/xtalk/15/pci
 *
 *	The bus vertex, when called to devfs_generate_path() returns:
 *		hw/module/001c01/slab/0/Ibrick/xtalk/15/pci
 *		hw/module/001c01/slab/1/Pbrick/xtalk/12/pci-x/0
 *		hw/module/001c01/slab/1/Pbrick/xtalk/12/pci-x/1
 */
void
linux_bus_cvlink(void)
{
	char name[8];
	int index;
	
	for (index=0; index < MAX_PCI_XWIDGET; index++) {
		if (!busnum_to_pcibr_vhdl[index])
			continue;

		sprintf(name, "%x", index);
		(void) hwgraph_edge_add(linux_busnum, busnum_to_pcibr_vhdl[index], 
				name);
	}
}

/*
 * pci_bus_map_create() - Called by pci_bus_to_hcl_cvlink() to finish the job.
 *
 *	Linux PCI Bus numbers are assigned from lowest module_id numbers
 *	(rack/slot etc.)
 */
static int 
pci_bus_map_create(struct pcibr_list_s *softlistp, moduleid_t moduleid)
{
	
	int basebus_num, bus_number;
	vertex_hdl_t pci_bus = softlistp->bl_vhdl;
	char moduleid_str[16];

	memset(moduleid_str, 0, 16);
	format_module_id(moduleid_str, moduleid, MODULE_FORMAT_BRIEF);
        (void) ioconfig_get_busnum((char *)moduleid_str, &basebus_num);

	/*
	 * Assign the correct bus number and also the nasid of this 
	 * pci Xwidget.
	 */
	bus_number = basebus_num + pcibr_widget_to_bus(pci_bus);
#ifdef DEBUG
	{
	char hwpath[MAXDEVNAME] = "\0";
	extern int hwgraph_vertex_name_get(vertex_hdl_t, char *, uint);

	pcibr_soft_t pcibr_soft = softlistp->bl_soft;
	hwgraph_vertex_name_get(pci_bus, hwpath, MAXDEVNAME);
	printk("%s:\n\tbus_num %d, basebus_num %d, brick_bus %d, "
		"bus_vhdl 0x%lx, brick_type %d\n", hwpath, bus_number,
		basebus_num, pcibr_widget_to_bus(pci_bus),
		(uint64_t)pci_bus, pcibr_soft->bs_bricktype);
	}
#endif
	busnum_to_pcibr_vhdl[bus_number] = pci_bus;

	/*
	 * Pre assign DMA maps needed for 32 Bits Page Map DMA.
	 */
	busnum_to_atedmamaps[bus_number] = (void *) vmalloc(
			sizeof(struct pcibr_dmamap_s)*MAX_ATE_MAPS);
	if (busnum_to_atedmamaps[bus_number] <= 0) {
		printk("pci_bus_map_create: Cannot allocate memory for ate maps\n");
		return -1;
	}
	memset(busnum_to_atedmamaps[bus_number], 0x0, 
			sizeof(struct pcibr_dmamap_s) * MAX_ATE_MAPS);
	return(0);
}

/*
 * pci_bus_to_hcl_cvlink() - This routine is called after SGI IO Infrastructure 
 *      initialization has completed to set up the mappings between PCI BRIDGE
 *      ASIC and logical pci bus numbers. 
 *
 *      Must be called before pci_init() is invoked.
 */
int
pci_bus_to_hcl_cvlink(void) 
{
	int i;
	extern pcibr_list_p pcibr_list;

	for (i = 0; i < nummodules; i++) {
		struct pcibr_list_s *softlistp = pcibr_list;
		struct pcibr_list_s *first_in_list = NULL;
		struct pcibr_list_s *last_in_list = NULL;

		/* Walk the list of pcibr_soft structs looking for matches */
		while (softlistp) {
			struct pcibr_soft_s *pcibr_soft = softlistp->bl_soft;
			moduleid_t moduleid;
			
			/* Is this PCI bus associated with this moduleid? */
			moduleid = NODE_MODULEID(
			    NASID_TO_COMPACT_NODEID(pcibr_soft->bs_nasid));
			if (modules[i]->id == moduleid) {
				struct pcibr_list_s *new_element;

				new_element = kmalloc(sizeof (struct pcibr_soft_s), GFP_KERNEL);
				if (new_element == NULL) {
					printk("%s: Couldn't allocate memory\n",__FUNCTION__);
					return -ENOMEM;
				}
				new_element->bl_soft = softlistp->bl_soft;
				new_element->bl_vhdl = softlistp->bl_vhdl;
				new_element->bl_next = NULL;

				/* list empty so just put it on the list */
				if (first_in_list == NULL) {
					first_in_list = new_element;
					last_in_list = new_element;
					softlistp = softlistp->bl_next;
					continue;
				}

				/* 
 				 * BASEIO IObricks attached to a module have 
				 * a higher priority than non BASEIO IOBricks 
				 * when it comes to persistant pci bus
				 * numbering, so put them on the front of the
				 * list.
				 */
				if (isIO9(pcibr_soft->bs_nasid)) {
					new_element->bl_next = first_in_list;
					first_in_list = new_element;
				} else {
					last_in_list->bl_next = new_element;
					last_in_list = new_element;
				}
			}
			softlistp = softlistp->bl_next;
		}
				
		/* 
		 * We now have a list of all the pci bridges associated with
		 * the module_id, modules[i].  Call pci_bus_map_create() for
		 * each pci bridge
		 */
		softlistp = first_in_list;
		while (softlistp) {
			moduleid_t iobrick;
			struct pcibr_list_s *next = softlistp->bl_next;
			iobrick = iomoduleid_get(softlistp->bl_soft->bs_nasid);
			pci_bus_map_create(softlistp, iobrick);
			kfree(softlistp);
			softlistp = next;
		}
	}

	/*
	 * Create the Linux PCI bus number vertex link.
	 */
	(void)linux_bus_cvlink();
	(void)ioconfig_bus_new_entries();

	return(0);
}

/*
 * Ugly hack to get PCI setup until we have a proper ACPI namespace.
 */
extern struct pci_ops sn_pci_ops;
int __init
sn_pci_init (void)
{
#	define PCI_BUSES_TO_SCAN 256
	int i = 0;
	struct pci_controller *controller;

	if (!ia64_platform_is("sn2") || IS_RUNNING_ON_SIMULATOR())
		return 0;

	/*
	 * This is needed to avoid bounce limit checks in the blk layer
	 */
	ia64_max_iommu_merge_mask = ~PAGE_MASK;

	/*
	 * set pci_raw_ops, etc.
	 */
	sn_pci_fixup(0);

	controller = kmalloc(sizeof(struct pci_controller), GFP_KERNEL);
	if (controller) {
		memset(controller, 0, sizeof(struct pci_controller));
		/* just allocate some devices and fill in the pci_dev structs */
		for (i = 0; i < PCI_BUSES_TO_SCAN; i++)
			pci_scan_bus(i, &sn_pci_ops, controller);
	}

	/*
	 * actually find devices and fill in hwgraph structs
	 */
	sn_pci_fixup(1);

	return 0;
}

subsys_initcall(sn_pci_init);

/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <asm/sn/types.h>
#include <asm/sn/hack.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iobus.h>
#include <asm/sn/iograph.h>
#include <asm/param.h>
#include <asm/sn/pio.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/addrs.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/agent.h>
#include <asm/sn/intr.h>
#include <asm/sn/xtalk/xtalkaddrs.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/io.h>

#include <asm/sn/pci/pciio.h>
// #include <sys/ql.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
extern int bridge_rev_b_data_check_disable;
#include <asm/sn/pci/pci_bus_cvlink.h>

#define MAX_PCI_XWIDGET 256
devfs_handle_t busnum_to_pcibr_vhdl[MAX_PCI_XWIDGET];
nasid_t busnum_to_nid[MAX_PCI_XWIDGET];
void * busnum_to_atedmamaps[MAX_PCI_XWIDGET];
unsigned char num_bridges;
static int done_probing = 0;

static int pci_bus_map_create(devfs_handle_t xtalk);
devfs_handle_t devfn_to_vertex(unsigned char busnum, unsigned int devfn);

#define SN1_IOPORTS_UNIT 256
#define MAX_IOPORTS 0xffff
#define MAX_IOPORTS_CHUNKS (MAX_IOPORTS / SN1_IOPORTS_UNIT)
struct ioports_to_tlbs_s ioports_to_tlbs[MAX_IOPORTS_CHUNKS];
unsigned long sn1_allocate_ioports(unsigned long pci_address);



/*
 * pci_bus_cvlink_init() - To be called once during initialization before 
 *	SGI IO Infrastructure init is called.
 */
void
pci_bus_cvlink_init(void)
{
	memset(busnum_to_pcibr_vhdl, 0x0, sizeof(devfs_handle_t) * MAX_PCI_XWIDGET);
	memset(busnum_to_nid, 0x0, sizeof(nasid_t) * MAX_PCI_XWIDGET);

	memset(busnum_to_atedmamaps, 0x0, sizeof(void *) * MAX_PCI_XWIDGET);

	memset(ioports_to_tlbs, 0x0, sizeof(ioports_to_tlbs));

	num_bridges = 0;
}

/*
 * pci_bus_to_vertex() - Given a logical Linux Bus Number returns the associated 
 *	pci bus vertex from the SGI IO Infrastructure.
 */
devfs_handle_t
pci_bus_to_vertex(unsigned char busnum)
{

	devfs_handle_t	pci_bus = NULL;


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
devfs_handle_t
devfn_to_vertex(unsigned char busnum, unsigned int devfn)
{

	int slot = 0;
	int func = 0;
	char	name[16];
	devfs_handle_t  pci_bus = NULL;
	devfs_handle_t	device_vertex = NULL;

	/*
	 * Go get the pci bus vertex.
	 */
	pci_bus = pci_bus_to_vertex(busnum);
	if (!pci_bus) {
		/*
		 * During probing, the Linux pci code invents non existant
		 * bus numbers and pci_dev structures and tries to access
		 * them to determine existance. Don't crib during probing.
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

	if (func == 0)
        	sprintf(name, "%d", slot);
	else
		sprintf(name, "%d%c", slot, 'a'+func);

	if (hwgraph_traverse(pci_bus, name, &device_vertex) != GRAPH_SUCCESS) {
		if (!device_vertex) {
			return(NULL);
		}
	}

	return(device_vertex);
}

/*
 * Most drivers currently do not properly tell the arch specific pci dma
 * interfaces whether they can handle A64. Here is where we privately
 * keep track of this.
 */
static void __init
set_sn1_pci64(struct pci_dev *dev)
{
	unsigned short vendor = dev->vendor;
	unsigned short device = dev->device;

	if (vendor == PCI_VENDOR_ID_QLOGIC) {
		if ((device == PCI_DEVICE_ID_QLOGIC_ISP2100) ||
				(device == PCI_DEVICE_ID_QLOGIC_ISP2200)) {
			SET_PCIA64(dev);
			return;
		}
	}

	if (vendor == PCI_VENDOR_ID_SGI) {
		if (device == PCI_DEVICE_ID_SGI_IOC3) {
			SET_PCIA64(dev);
			return;
		}
	}

}

/*
 * sn1_allocate_ioports() - This routine provides the allocation and 
 *	mappings between Linux style IOPORTs management.
 *
 *	For simplicity sake, SN1 will allocate IOPORTs in chunks of 
 *	256bytes .. irrespective of what the card desires.  This may 
 *	have to change when we understand how to deal with legacy ioports 
 *	which are hardcoded in some drivers e.g. SVGA.
 *
 *	Ofcourse, the SN1 IO Infrastructure has no concept of IOPORT numbers.
 *	It will remain so.  The IO Infrastructure will continue to map 
 *	IO Resource just like IRIX.  When this is done, we map IOPORT 
 *	chunks to these resources.  The Linux drivers will see and use real 
 *	IOPORT numbers.  The various IOPORT access macros e.g. inb/outb etc. 
 *	does the munging of these IOPORT numbers to make a Uncache Virtual 
 *	Address.  This address via the tlb entries generates the PCI Address 
 *	allocated by the SN1 IO Infrastructure Layer.
 */
static unsigned long sn1_ioport_num = 0x100; /* Reserve room for Legacy stuff */
unsigned long
sn1_allocate_ioports(unsigned long pci_address)
{
	
	unsigned long ioport_index;

	/*
	 * Just some idiot checking ..
	 */
	if ( sn1_ioport_num > 0xffff ) {
		printk("sn1_allocate_ioports: No more IO PORTS available\n");
		return(-1);
	}

	/*
	 * See Section 4.1.1.5 of Intel IA-64 Acrchitecture Software Developer's
	 * Manual for details.
	 */
	ioport_index = sn1_ioport_num / SN1_IOPORTS_UNIT;
	ioports_to_tlbs[ioport_index].ppn = pci_address;
	ioports_to_tlbs[ioport_index].p = 1; /* Present Bit */
	ioports_to_tlbs[ioport_index].ma = 5; /* Memory Attributes */
	ioports_to_tlbs[ioport_index].a = 0; /* Set Data Access Bit Fault */
	ioports_to_tlbs[ioport_index].d = 0; /* Dirty Bit */
	ioports_to_tlbs[ioport_index].pl = 3;/* Privilege Level - All levels can R/W*/
	ioports_to_tlbs[ioport_index].ar = 2; /* Access Rights - R/W only*/
	ioports_to_tlbs[ioport_index].ed = 0; /* Exception Deferral Bit */
	ioports_to_tlbs[ioport_index].ig = 0; /* Ignored */

	printk("sn1_allocate_ioports: ioport_index 0x%x ioports_to_tlbs 0x%p\n", ioport_index, ioports_to_tlbs[ioport_index].ppn);
	
	sn1_ioport_num += SN1_IOPORTS_UNIT;

	return(sn1_ioport_num - SN1_IOPORTS_UNIT);
}

/*
 * sn1_pci_fixup() - This routine is called when platform_pci_fixup() is 
 *	invoked at the end of pcibios_init() to link the Linux pci 
 *	infrastructure to SGI IO Infrasturcture - ia64/kernel/pci.c
 *
 *	Other platform specific fixup can also be done here.
 */
void
sn1_pci_fixup(int arg)
{
	struct list_head *ln;
	struct pci_bus *pci_bus = NULL;
	struct pci_dev *device_dev = NULL;
	struct sn1_widget_sysdata *widget_sysdata;
	struct sn1_device_sysdata *device_sysdata;
	unsigned long ioport;
	pciio_intr_t intr_handle;
	int cpuid, bit;
	devfs_handle_t *device_vertex;
	pciio_intr_line_t lines;
	extern void sn1_pci_find_bios(void);


unsigned long   res;

	if (arg == 0) {
		sn1_pci_find_bios();
		return;
	}

#if 0
{
        devfs_handle_t  bridge_vhdl = pci_bus_to_vertex(0);
        pcibr_soft_t    pcibr_soft = (pcibr_soft_t) hwgraph_fastinfo_get(bridge_vhdl);
	bridge_t        *bridge = pcibr_soft->bs_base;
        printk("pci_fixup_ioc3: Before devreg fixup\n");
        printk("pci_fixup_ioc3: Devreg 0 0x%x\n", bridge->b_device[0].reg);
        printk("pci_fixup_ioc3: Devreg 1 0x%x\n", bridge->b_device[1].reg);
        printk("pci_fixup_ioc3: Devreg 2 0x%x\n", bridge->b_device[2].reg);
        printk("pci_fixup_ioc3: Devreg 3 0x%x\n", bridge->b_device[3].reg);
        printk("pci_fixup_ioc3: Devreg 4 0x%x\n", bridge->b_device[4].reg);
        printk("pci_fixup_ioc3: Devreg 5 0x%x\n", bridge->b_device[5].reg);
        printk("pci_fixup_ioc3: Devreg 6 0x%x\n", bridge->b_device[6].reg);
        printk("pci_fixup_ioc3: Devreg 7 0x%x\n", bridge->b_device[7].reg);
}
#endif
	done_probing = 1;

	if ( IS_RUNNING_ON_SIMULATOR() ) {
		printk("sn1_pci_fixup not supported on simulator.\n");
		return;
	}

#ifdef REAL_HARDWARE

	/*
	 * Initialize the pci bus vertex in the pci_bus struct.
	 */
	for( ln = pci_root_buses.next; ln != &pci_root_buses; ln = ln->next) {
		pci_bus = pci_bus_b(ln);
		widget_sysdata = kmalloc(sizeof(struct sn1_widget_sysdata), 
					GFP_KERNEL);
		widget_sysdata->vhdl = pci_bus_to_vertex(pci_bus->number);
		pci_bus->sysdata = (void *)widget_sysdata;
	}

	/*
 	 * set the root start and end so that drivers calling check_region()
	 * won't see a conflict
	 */
	ioport_resource.start |= IO_SWIZ_BASE;
	ioport_resource.end |= (HSPEC_SWIZ_BASE-1);
	/*
	 * Initialize the device vertex in the pci_dev struct.
	 */
	pci_for_each_dev(device_dev) {
		unsigned int irq;
		int idx;
		u16 cmd;
		devfs_handle_t vhdl;
		unsigned long size;

		if (device_dev->vendor == PCI_VENDOR_ID_SGI &&
				device_dev->device == PCI_DEVICE_ID_SGI_IOC3) {
			extern void pci_fixup_ioc3(struct pci_dev *d);
			pci_fixup_ioc3(device_dev);
		}

		/* Set the device vertex */

		device_sysdata = kmalloc(sizeof(struct sn1_device_sysdata),
					GFP_KERNEL);
		device_sysdata->vhdl = devfn_to_vertex(device_dev->bus->number, device_dev->devfn);
		device_sysdata->isa64 = 0;
		device_dev->sysdata = (void *) device_sysdata;
		set_sn1_pci64(device_dev);
		pci_read_config_word(device_dev, PCI_COMMAND, &cmd);

		/*
		 * Set the resources address correctly.  The assumption here 
		 * is that the addresses in the resource structure has been
		 * read from the card and it was set in the card by our
		 * Infrastructure ..
		 */
		vhdl = device_sysdata->vhdl;
		for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
			size = 0;
			size = device_dev->resource[idx].end -
				device_dev->resource[idx].start;
			if (size) {
				res = 0;
				res = pciio_config_get(vhdl, (unsigned) PCI_BASE_ADDRESS_0 + idx, 4);
				device_dev->resource[idx].start = (unsigned long)pciio_pio_addr(vhdl, 0, PCIIO_SPACE_WIN(idx), 0, size, 0, PCIIO_BYTE_STREAM);

/* printk("sn1_pci_fixup: Mapped Address = 0x%p size = 0x%x\n", device_dev->resource[idx].start, size); */
			}
			else
				continue;

			device_dev->resource[idx].end = 
				device_dev->resource[idx].start + size;

			/*
			 * Adjust the addresses to go to the SWIZZLE ..
			 */
			device_dev->resource[idx].start = 
				device_dev->resource[idx].start & 0xfffff7ffffffffff;
			device_dev->resource[idx].end = 
				device_dev->resource[idx].end & 0xfffff7ffffffffff;
			res = 0;
			res = pciio_config_get(vhdl, (unsigned) PCI_BASE_ADDRESS_0 + idx, 4);
			if (device_dev->resource[idx].flags & IORESOURCE_IO) {
				cmd |= PCI_COMMAND_IO;
				ioport = sn1_allocate_ioports(device_dev->resource[idx].start);
				/* device_dev->resource[idx].start = ioport; */
				/* device_dev->resource[idx].end = ioport + SN1_IOPORTS_UNIT */
			}
			else if (device_dev->resource[idx].flags & IORESOURCE_MEM)
				cmd |= PCI_COMMAND_MEMORY;
		}
		/*
		 * Now handle the ROM resource ..
		 */
		size = device_dev->resource[PCI_ROM_RESOURCE].end -
			device_dev->resource[PCI_ROM_RESOURCE].start;
		device_dev->resource[PCI_ROM_RESOURCE].start =
			(unsigned long) pciio_pio_addr(vhdl, 0, PCIIO_SPACE_ROM, 0, 
				size, 0, PCIIO_BYTE_STREAM);
		device_dev->resource[PCI_ROM_RESOURCE].end =
			device_dev->resource[PCI_ROM_RESOURCE].start + size;

                /*
                 * go through synergy swizzled space
                 */
		device_dev->resource[PCI_ROM_RESOURCE].start &= 0xfffff7ffffffffffUL;
		device_dev->resource[PCI_ROM_RESOURCE].end   &= 0xfffff7ffffffffffUL;

		/*
		 * Update the Command Word on the Card.
		 */
		cmd |= PCI_COMMAND_MASTER; /* If the device doesn't support */
					   /* bit gets dropped .. no harm */
		pci_write_config_word(device_dev, PCI_COMMAND, cmd);

		pci_read_config_byte(device_dev, PCI_INTERRUPT_PIN, &lines);
#ifdef BRINGUP
		if (device_dev->vendor == PCI_VENDOR_ID_SGI &&
			device_dev->device == PCI_DEVICE_ID_SGI_IOC3 ) {
				lines = 1;
		}

#endif
 
		device_sysdata = (struct sn1_device_sysdata *)device_dev->sysdata;
		device_vertex = device_sysdata->vhdl;
 
		intr_handle = pciio_intr_alloc(device_vertex, NULL, lines, device_vertex);

		bit = intr_handle->pi_irq;
		cpuid = intr_handle->pi_cpu;
		irq = bit_pos_to_irq(bit);
		irq = irq + (cpuid << 8);
		pciio_intr_connect(intr_handle, NULL, NULL, NULL);
		device_dev->irq = irq;

	}
#endif	/* REAL_HARDWARE */
#if 0

{
        devfs_handle_t  bridge_vhdl = pci_bus_to_vertex(0);
        pcibr_soft_t    pcibr_soft = (pcibr_soft_t) hwgraph_fastinfo_get(bridge_vhdl);
        bridge_t        *bridge = pcibr_soft->bs_base;

        printk("pci_fixup_ioc3: Before devreg fixup\n");
        printk("pci_fixup_ioc3: Devreg 0 0x%x\n", bridge->b_device[0].reg);
        printk("pci_fixup_ioc3: Devreg 1 0x%x\n", bridge->b_device[1].reg);
        printk("pci_fixup_ioc3: Devreg 2 0x%x\n", bridge->b_device[2].reg);
        printk("pci_fixup_ioc3: Devreg 3 0x%x\n", bridge->b_device[3].reg);
        printk("pci_fixup_ioc3: Devreg 4 0x%x\n", bridge->b_device[4].reg);
        printk("pci_fixup_ioc3: Devreg 5 0x%x\n", bridge->b_device[5].reg);
        printk("pci_fixup_ioc3: Devreg 6 0x%x\n", bridge->b_device[6].reg);
        printk("pci_fixup_ioc3: Devreg 7 0x%x\n", bridge->b_device[7].reg);
}
#endif

}

/*
 * pci_bus_map_create() - Called by pci_bus_to_hcl_cvlink() to finish the job.
 *
 *	Linux PCI Bus numbers are assigned from lowest module_id numbers
 *	(rack/slot etc.) starting from HUB_WIDGET_ID_MAX down to 
 *	HUB_WIDGET_ID_MIN:
 *		widgetnum 15 gets lower Bus Number than widgetnum 14 etc.
 *
 *	Given 2 modules 001c01 and 001c02 we get the following mappings:
 *		001c01, widgetnum 15 = Bus number 0
 *		001c01, widgetnum 14 = Bus number 1
 *		001c02, widgetnum 15 = Bus number 3
 *		001c02, widgetnum 14 = Bus number 4
 *		etc.
 *
 * The rational for starting Bus Number 0 with Widget number 15 is because 
 * the system boot disks are always connected via Widget 15 Slot 0 of the 
 * I-brick.  Linux creates /dev/sd* devices(naming) strating from Bus Number 0 
 * Therefore, /dev/sda1 will be the first disk, on Widget 15 of the lowest 
 * module id(Master Cnode) of the system.
 *	
 */
static int 
pci_bus_map_create(devfs_handle_t xtalk)
{

	devfs_handle_t master_node_vertex = NULL;
	devfs_handle_t xwidget = NULL;
	devfs_handle_t pci_bus = NULL;
	hubinfo_t hubinfo = NULL;
	xwidgetnum_t widgetnum;
	char pathname[128];
	graph_error_t rv;

	/*
	 * Loop throught this vertex and get the Xwidgets ..
	 */
	for (widgetnum = HUB_WIDGET_ID_MAX; widgetnum >= HUB_WIDGET_ID_MIN; widgetnum--) {
        {
                int pos;
                char dname[256];
                pos = devfs_generate_path(xtalk, dname, 256);
                printk("%s : path= %s\n", __FUNCTION__, &dname[pos]);
        }

		sprintf(pathname, "%d", widgetnum);
		xwidget = NULL;
		
		/*
		 * Example - /hw/module/001c16/Pbrick/xtalk/8 is the xwidget
		 *	     /hw/module/001c16/Pbrick/xtalk/8/pci/1 is device
		 */
		rv = hwgraph_traverse(xtalk, pathname, &xwidget);
		if ( (rv != GRAPH_SUCCESS) ) {
			if (!xwidget)
				continue;
		}

		sprintf(pathname, "%d/"EDGE_LBL_PCI, widgetnum);
		pci_bus = NULL;
		if (hwgraph_traverse(xtalk, pathname, &pci_bus) != GRAPH_SUCCESS)
			if (!pci_bus)
				continue;

		/*
		 * Assign the correct bus number and also the nasid of this 
		 * pci Xwidget.
		 * 
		 * Should not be any race here ...
		 */
		num_bridges++;
		busnum_to_pcibr_vhdl[num_bridges - 1] = pci_bus;

		/*
		 * Get the master node and from there get the NASID.
		 */
		master_node_vertex = device_master_get(xwidget);
		if (!master_node_vertex) {
			printk("WARNING: pci_bus_map_create: Unable to get .master for vertex 0x%p\n", xwidget);
		}
	
		hubinfo_get(master_node_vertex, &hubinfo);
		if (!hubinfo) {
			printk("WARNING: pci_bus_map_create: Unable to get hubinfo for master node vertex 0x%p\n", master_node_vertex);
			return(1);
		} else {
			busnum_to_nid[num_bridges - 1] = hubinfo->h_nasid;
		}

		/*
		 * Pre assign DMA maps needed for 32 Bits Page Map DMA.
		 */
		busnum_to_atedmamaps[num_bridges - 1] = (void *) kmalloc(
			sizeof(struct sn1_dma_maps_s) * 512, GFP_KERNEL);
		if (!busnum_to_atedmamaps[num_bridges - 1])
			printk("WARNING: pci_bus_map_create: Unable to precreate ATE DMA Maps for busnum %d vertex 0x%p\n", num_bridges - 1, xwidget);

		memset(busnum_to_atedmamaps[num_bridges - 1], 0x0, 
			sizeof(struct sn1_dma_maps_s) * 512);

	}

        return(0);
}

/*
 * pci_bus_to_hcl_cvlink() - This routine is called after SGI IO Infrastructure   
 *      initialization has completed to set up the mappings between Xbridge
 *      and logical pci bus numbers.  We also set up the NASID for each of these
 *      xbridges.
 *
 *      Must be called before pci_init() is invoked.
 */
int
pci_bus_to_hcl_cvlink(void) 
{

	devfs_handle_t devfs_hdl = NULL;
	devfs_handle_t module_comp = NULL;
	devfs_handle_t node = NULL;
	devfs_handle_t xtalk = NULL;
	graph_vertex_place_t placeptr = EDGE_PLACE_WANT_REAL_EDGES;
	int rv = 0;
	char name[256];
	int master_iobrick;
	moduleid_t iobrick_id;
	int i;

	/*
	 * Iterate throught each xtalk links in the system ..
	 * /hw/module/001c01/node/xtalk/ 8|9|10|11|12|13|14|15 
	 *
	 * /hw/module/001c01/node/xtalk/15 -> /hw/module/001c01/Ibrick/xtalk/15
	 *
	 * What if it is not pci?
	 */
	devfs_hdl = hwgraph_path_to_vertex("/dev/hw/module");

	/*
	 * To provide consistent(not persistent) device naming, we need to start 
	 * bus number allocation from the C-Brick with the lowest module id e.g. 001c01 
	 * with an attached I-Brick.  Find the master_iobrick.
	 */
	master_iobrick = -1;
	for (i = 0; i < nummodules; i++) {
		moduleid_t iobrick_id; 
		iobrick_id = iobrick_module_get(&modules[i]->elsc);
		if (iobrick_id > 0) { /* Valid module id */
			if (MODULE_GET_BTYPE(iobrick_id) == MODULE_IBRICK) {
				master_iobrick = i;
				break;
			}
		}
	}

	/*
	 * The master_iobrick gets bus 0 and 1.
	 */
	if (master_iobrick >= 0) {
		memset(name, 0, 256);
		format_module_id(name, modules[master_iobrick]->id, MODULE_FORMAT_BRIEF);
		strcat(name, "/node/xtalk");
		xtalk = NULL;
		rv = hwgraph_edge_get(devfs_hdl, name, &xtalk);
		pci_bus_map_create(xtalk);
	}
		
	/*
	 * Now go do the rest of the modules, starting from the C-Brick with the lowest 
	 * module id, remembering to skip the master_iobrick, which was done above.
	 */
	for (i = 0; i < nummodules; i++) {
		if (i == master_iobrick) {
			continue; /* Did the master_iobrick already. */
		}

		memset(name, 0, 256);
		format_module_id(name, modules[i]->id, MODULE_FORMAT_BRIEF);
		strcat(name, "/node/xtalk");
		xtalk = NULL;
		rv = hwgraph_edge_get(devfs_hdl, name, &xtalk);
		pci_bus_map_create(xtalk);
	}

	return(0);
}

/*
 * sgi_pci_intr_support -
 */
int
sgi_pci_intr_support (unsigned int requested_irq, device_desc_t *dev_desc,
	devfs_handle_t *bus_vertex, pciio_intr_line_t *lines,
	devfs_handle_t *device_vertex)

{

	unsigned int bus;
	unsigned int devfn;
	struct pci_dev *pci_dev;
	unsigned char intr_pin = 0;
	struct sn1_widget_sysdata *widget_sysdata;
	struct sn1_device_sysdata *device_sysdata;

	if (!dev_desc || !bus_vertex || !device_vertex) {
		printk("WARNING: sgi_pci_intr_support: Invalid parameter dev_desc 0x%p, bus_vertex 0x%p, device_vertex 0x%p\n", dev_desc, bus_vertex, device_vertex);
		return(-1);
	}

	devfn = (requested_irq >> 8) & 0xff;
	bus = (requested_irq >> 16) & 0xffff;
	pci_dev = pci_find_slot(bus, devfn);
	widget_sysdata = (struct sn1_widget_sysdata *)pci_dev->bus->sysdata;
	*bus_vertex = widget_sysdata->vhdl;
	device_sysdata = (struct sn1_device_sysdata *)pci_dev->sysdata;
	*device_vertex = device_sysdata->vhdl;
#if 0
	{
		int pos;
		char dname[256];
		pos = devfs_generate_path(*device_vertex, dname, 256);
		printk("%s : path= %s pos %d\n", __FUNCTION__, &dname[pos], pos);
	}
#endif /* BRINGUP */


	/*
	 * Get the Interrupt PIN.
	 */
	pci_read_config_byte(pci_dev, PCI_INTERRUPT_PIN, &intr_pin);
	*lines = (pciio_intr_line_t)intr_pin;

#ifdef BRINGUP
	/*
	 * ioc3 can't decode the PCI_INTERRUPT_PIN field of its config
	 * space so we have to set it here
	 */
	if (pci_dev->vendor == PCI_VENDOR_ID_SGI &&
	    pci_dev->device == PCI_DEVICE_ID_SGI_IOC3 ) {
		*lines = 1;
	}
#endif /* BRINGUP */

	/* Not supported currently */
	*dev_desc = NULL;
	return(0);

}

/* devtree.c: Build a copy of the prom device tree in kernel
 *            memory for easier access and cleaner interface.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>
#include <asm/oplib.h>

/* Add more as appropriate. */
enum bus_t {
	OBIO_BUS,
	SBUS_BUS,
	PCI_BUS,
	PMEM_BUS,
	CPU_BUS,
};

struct sdevmapping {
	unsigned long physpage;
	int mapsz;
	enum bus_t where;
};

/* limitation of sparc arch. */
#define NUM_SPARC_IRQS    15

struct sdev_irqs {
	int level;
	int vector; /* For vme/sbus irq sharing methinks. */
};

struct sparcdev {
	struct sparcdev *next;
	struct sparcdev *prev;
	int node;
	char *name;
	int num_mappings;
	struct sdevmapping *maps;
	int num_irqs;
	struct sdev_irqs irqinfo[NUM_SPARC_IRQS];
};

struct sparcbus {
	struct sparcbus *next;
	enum bus_t type;
	struct sparcdev *device_list;
};

/* Add more as appropriate. */
struct sparcbus obiobus_info = { 0, OBIO_BUS, { 0, 0}, };
struct sparcbus sbusbus_info = { 0, SBUS_BUS, { 0, 0}, };
struct sparcbus pcibus_info = { 0, PCI_BUS, { 0, 0}, };
struct sparcbus pmembus_info = { 0, PMEM_BUS, { 0, 0}, };
struct sparcbus cpubus_info = { 0, CPU_BUS, { 0, 0}, };

struct sparcbus *sparcbus_list = 0;

/* This is called at boot time to build the prom device tree. */
int prom_build_devtree(unsigned long start_mem, unsigned long end_mem)
{
}

/* Search the bus device list for a device which matches one of the
 * names in NAME_VECTOR which is an array or NUM_NAMES strings, given
 * the passed BUSTYPE.  Return ptr to the matching sparcdev structure
 * or NULL if no matches found.
 */
struct sparcdev *prom_find_dev_on_bus(bus_t bustype, char **name_vector, int num_names)
{
	struct sparcdev *sdp;
	struct sparcbus *thebus;
	int niter;

	if(!num_names)
		return 0;

	if(!sparcbus_list) {
		prom_printf("prom_find_dev_on_bus: Device list not initted yet!\n");
		prom_halt();
	}

	while(thebus = sparcbus_list; thebus; thebus = thebus->next)
		if(thebus->type == bustype)
			break;
	if(!thebus || !thebus->device_list)
		return 0;

	for(sdp = thebus->device_list; sdp; sdp = sdp->next) {
		for(niter = 0; niter < num_names; niter++)
			if(!strcmp(sdp->name, name_vector[niter]))
				break;
	}
	return sdp;
}

/* Continue searching on a device list, starting at START_DEV for the next
 * instance whose name matches one of the elements of NAME_VECTOR which is
 * of length NUM_NAMES.
 */
struct sparcdev *prom_find_next_dev(struct sparcdev *start_dev, char **name_vector, int num_names)
{
	struct sparcdev *sdp;
	int niter;

	if(!start_dev->next || !num_names)
		return 0;
	for(sdp = start_dev->next; sdp; sdp = sdp->next) {
		for(niter = 0; niter < num_names; niter++)
			if(!strcmp(sdp->name, name_vector[niter]))
				break;
	}
	return sdp;
}

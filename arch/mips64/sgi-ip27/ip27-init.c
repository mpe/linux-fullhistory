#include <linux/kernel.h>
#include <linux/mmzone.h>	/* for numnodes */
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/klconfig.h>
#include <asm/ioc3.h>
#include <asm/mipsregs.h>
#include <asm/sn/gda.h>

typedef unsigned long cpumask_t;	/* into asm/sn/types.h */
typedef unsigned long cpuid_t;

#define	CPUMASK_CLRALL(p)	(p) = 0
#define CPUMASK_SETB(p, bit)	(p) |= 1 << (bit)

cpumask_t	boot_cpumask;
hubreg_t	region_mask = 0;
static int	fine_mode = 0;

cnodeid_t	nasid_to_compact_node[MAX_NASIDS];
nasid_t		compact_to_nasid_node[MAX_COMPACT_NODES];
cnodeid_t	cpuid_to_compact_node[MAXCPUS];

hubreg_t get_region(cnodeid_t cnode)
{
	if (fine_mode)
		return COMPACT_TO_NASID_NODEID(cnode) >> NASID_TO_FINEREG_SHFT;
	else
		return COMPACT_TO_NASID_NODEID(cnode) >> NASID_TO_COARSEREG_SHFT;
}

static void gen_region_mask(hubreg_t *region_mask, int maxnodes)
{
	cnodeid_t cnode;

	(*region_mask) = 0;
	for (cnode = 0; cnode < maxnodes; cnode++) {
		(*region_mask) |= 1ULL << get_region(cnode);
	}
}

int is_fine_dirmode(void)
{
	return (((LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_REGIONSIZE_MASK)
		>> NSRI_REGIONSIZE_SHFT) & REGIONSIZE_FINE);
}

lboard_t * find_lboard_real(lboard_t *start, unsigned char brd_type)
{
	/* Search all boards stored on this node. */
	while (start) {
		if (start->brd_type == brd_type)
			return start;
		start = KLCF_NEXT(start);
	}
	/* Didn't find it. */
	return (lboard_t *)NULL;
}

klinfo_t *find_component(lboard_t *brd, klinfo_t *kli, unsigned char struct_type)
{
	int index, j;

	if (kli == (klinfo_t *)NULL) {
		index = 0;
	} else {
		for (j = 0; j < KLCF_NUM_COMPS(brd); j++)
			if (kli == KLCF_COMP(brd, j))
				break;
		index = j;
		if (index == KLCF_NUM_COMPS(brd)) {
			printk("find_component: Bad pointer: 0x%p\n", kli);
			return (klinfo_t *)NULL;
		}
		index++;		/* next component */
	}

	for (; index < KLCF_NUM_COMPS(brd); index++) {
		kli = KLCF_COMP(brd, index);
		if (KLCF_COMP_TYPE(kli) == struct_type)
			return kli;
	}

	/* Didn't find it. */
	return (klinfo_t *)NULL;
}

klinfo_t *find_first_component(lboard_t *brd, unsigned char struct_type)
{
	return find_component(brd, (klinfo_t *)NULL, struct_type);
}

nasid_t get_actual_nasid(lboard_t *brd)
{
	klhub_t *hub;

	if (!brd)
		return INVALID_NASID;

	/* find out if we are a completely disabled brd. */
	hub  = (klhub_t *)find_first_component(brd, KLSTRUCT_HUB);
	if (!hub)
		return INVALID_NASID;
	if (!(hub->hub_info.flags & KLINFO_ENABLE))	/* disabled node brd */
		return hub->hub_info.physid;
	else
		return brd->brd_nasid;
}

int do_cpumask(cnodeid_t cnode, nasid_t nasid, cpumask_t *boot_cpumask, 
							int *highest)
{
	lboard_t *brd;
	klcpu_t *acpu;
	int cpus_found = 0;
	cpuid_t cpuid;

	brd = find_lboard_real((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_IP27);

	do {
		acpu = (klcpu_t *)find_first_component(brd, KLSTRUCT_CPU);
		while (acpu) {
			cpuid = acpu->cpu_info.virtid;
			/* cnode is not valid for completely disabled brds */
			if (get_actual_nasid(brd) == brd->brd_nasid)
				cpuid_to_compact_node[cpuid] = cnode;
			if (cpuid > *highest)
				*highest = cpuid;
			/* Only let it join in if it's marked enabled */
			if (acpu->cpu_info.flags & KLINFO_ENABLE) {
				CPUMASK_SETB(*boot_cpumask, cpuid);
				cpus_found++;
			}
			acpu = (klcpu_t *)find_component(brd, (klinfo_t *)acpu, 
								KLSTRUCT_CPU);
		}
		brd = KLCF_NEXT(brd);
		if (brd)
			brd = find_lboard_real(brd,KLTYPE_IP27);
		else
			break;
	} while (brd);

	return cpus_found;
}

cpuid_t cpu_node_probe(cpumask_t *boot_cpumask, int *numnodes)
{
	int i, cpus = 0, highest = 0;
	gda_t *gdap = GDA;
	nasid_t nasid;

	/*
	 * Initialize the arrays to invalid nodeid (-1)
	 */
	for (i = 0; i < MAX_COMPACT_NODES; i++)
		compact_to_nasid_node[i] = INVALID_NASID;
	for (i = 0; i < MAX_NASIDS; i++)
		nasid_to_compact_node[i] = INVALID_CNODEID;
	for (i = 0; i < MAXCPUS; i++)
		cpuid_to_compact_node[i] = INVALID_CNODEID;

	*numnodes = 0;
	for (i = 0; i < MAX_COMPACT_NODES; i++) {
		if ((nasid = gdap->g_nasidtable[i]) == INVALID_NASID) {
			break;
		} else {
			compact_to_nasid_node[i] = nasid;
			nasid_to_compact_node[nasid] = i;
			(*numnodes)++;
			cpus += do_cpumask(i, nasid, boot_cpumask, &highest);
		}
	}

	/*
	 * Cpus are numbered in order of cnodes. Currently, disabled
	 * cpus are not numbered.
	 */

	return(highest + 1);
}

void mlreset (void)
{
	int i, maxcpus;

	fine_mode = is_fine_dirmode();

	/*
	 * Probe for all CPUs - this creates the cpumask and
	 * sets up the mapping tables.
	 */
	CPUMASK_CLRALL(boot_cpumask);
	maxcpus = cpu_node_probe(&boot_cpumask, &numnodes);

	gen_region_mask(&region_mask, numnodes);

	/*
	 * Set all nodes' calias sizes to 8k
	 */
	for (i = 0; i < numnodes; i++) {
		nasid_t nasid;

		nasid = COMPACT_TO_NASID_NODEID(i);

		/*
		 * Always have node 0 in the region mask, otherwise
		 * CALIAS accesses get exceptions since the hub
		 * thinks it is a node 0 address.
		 */
		REMOTE_HUB_S(nasid, PI_REGION_PRESENT, (region_mask | 1));
		REMOTE_HUB_S(nasid, PI_CALIAS_SIZE, PI_CALIAS_SIZE_0);

#ifdef LATER
		/*
		 * Set up all hubs to have a big window pointing at
		 * widget 0. Memory mode, widget 0, offset 0
		 */
		REMOTE_HUB_S(nasid, IIO_ITTE(SWIN0_BIGWIN),
			((HUB_PIO_MAP_TO_MEM << IIO_ITTE_IOSP_SHIFT) |
			(0 << IIO_ITTE_WIDGET_SHIFT)));
#endif
	}
}


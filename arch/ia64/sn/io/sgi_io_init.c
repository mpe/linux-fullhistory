/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/pci/pciba.h>
#include <linux/smp.h>

extern void mlreset(int );
extern int init_hcl(void);
extern void klgraph_hack_init(void);
extern void hubspc_init(void);
extern void pciio_init(void);
extern void pcibr_init(void);
extern void xtalk_init(void);
extern void xbow_init(void);
extern void xbmon_init(void);
extern void pciiox_init(void);
extern void usrpci_init(void);
extern void ioc3_init(void);
extern void initialize_io(void);
#if defined(CONFIG_IA64_SGI_SN1)
extern void intr_clear_all(nasid_t);
#endif
extern void klhwg_add_all_modules(devfs_handle_t);
extern void klhwg_add_all_nodes(devfs_handle_t);

void sn_mp_setup(void);
extern devfs_handle_t hwgraph_root;
extern void io_module_init(void);
extern void pci_bus_cvlink_init(void);
extern void temp_hack(void);

extern int pci_bus_to_hcl_cvlink(void);

/* #define DEBUG_IO_INIT */
#ifdef DEBUG_IO_INIT
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG_IO_INIT */

/*
 * per_hub_init
 *
 * 	This code is executed once for each Hub chip.
 */
static void
per_hub_init(cnodeid_t cnode)
{
	nasid_t		nasid;
	nodepda_t	*npdap;
	ii_icmr_u_t	ii_icmr;
	ii_ibcr_u_t	ii_ibcr;

	nasid = COMPACT_TO_NASID_NODEID(cnode);

	ASSERT(nasid != INVALID_NASID);
	ASSERT(NASID_TO_COMPACT_NODEID(nasid) == cnode);

	npdap = NODEPDA(cnode);

#if defined(CONFIG_IA64_SGI_SN1)
	/* initialize per-node synergy perf instrumentation */
	npdap->synergy_perf_enabled = 0; /* off by default */
	npdap->synergy_perf_lock = SPIN_LOCK_UNLOCKED;
	npdap->synergy_perf_freq = SYNERGY_PERF_FREQ_DEFAULT;
	npdap->synergy_inactive_intervals = 0;
	npdap->synergy_active_intervals = 0;
	npdap->synergy_perf_data = NULL;
	npdap->synergy_perf_first = NULL;
#endif /* CONFIG_IA64_SGI_SN1 */


	/*
	 * Set the total number of CRBs that can be used.
	 */
	ii_icmr.ii_icmr_regval= 0x0;
	ii_icmr.ii_icmr_fld_s.i_c_cnt = 0xF;
	REMOTE_HUB_S(nasid, IIO_ICMR, ii_icmr.ii_icmr_regval);

	/*
	 * Set the number of CRBs that both of the BTEs combined
	 * can use minus 1.
	 */
	ii_ibcr.ii_ibcr_regval= 0x0;
	ii_ibcr.ii_ibcr_fld_s.i_count = 0x8;
	REMOTE_HUB_S(nasid, IIO_IBCR, ii_ibcr.ii_ibcr_regval);

	/*
	 * Set CRB timeout to be 10ms.
	 */
	REMOTE_HUB_S(nasid, IIO_ICTP, 0x1000 );
	REMOTE_HUB_S(nasid, IIO_ICTO, 0xff);


#if defined(CONFIG_IA64_SGI_SN1)
	/* Reserve all of the hardwired interrupt levels. */
	intr_reserve_hardwired(cnode);
#endif

	/* Initialize error interrupts for this hub. */
	hub_error_init(cnode);
}

/*
 * This routine is responsible for the setup of all the IRIX hwgraph style
 * stuff that's been pulled into linux.  It's called by sn1_pci_find_bios which
 * is called just before the generic Linux PCI layer does its probing (by 
 * platform_pci_fixup aka sn1_pci_fixup).
 *
 * It is very IMPORTANT that this call is only made by the Master CPU!
 *
 */

void
sgi_master_io_infr_init(void)
{
	int cnode;

	/*
	 * Do any early init stuff .. einit_tbl[] etc.
	 */
	DBG("--> sgi_master_io_infr_init: calling init_hcl().\n");
	init_hcl(); /* Sets up the hwgraph compatibility layer with devfs */

	/*
	 * initialize the Linux PCI to xwidget vertexes ..
	 */
	DBG("--> sgi_master_io_infr_init: calling pci_bus_cvlink_init().\n");
	pci_bus_cvlink_init();

#ifdef BRINGUP
#ifdef CONFIG_IA64_SGI_SN1
	/*
	 * Hack to provide statically initialzed klgraph entries.
	 */
	DBG("--> sgi_master_io_infr_init: calling klgraph_hack_init()\n");
	klgraph_hack_init();
#endif /* CONFIG_IA64_SGI_SN1 */
#endif /* BRINGUP */

	/*
	 * This is the Master CPU.  Emulate mlsetup and main.c in Irix.
	 */
	DBG("--> sgi_master_io_infr_init: calling mlreset(0).\n");
	mlreset(0); /* Master .. */

	/*
	 * allowboot() is called by kern/os/main.c in main()
	 * Emulate allowboot() ...
	 *   per_cpu_init() - only need per_hub_init()
	 *   cpu_io_setup() - Nothing to do.
	 * 
	 */
	DBG("--> sgi_master_io_infr_init: calling sn_mp_setup().\n");
	sn_mp_setup();

	DBG("--> sgi_master_io_infr_init: calling per_hub_init(0).\n");
	for (cnode = 0; cnode < numnodes; cnode++) {
		per_hub_init(cnode);
	}

	/* We can do headless hub cnodes here .. */

	/*
	 * io_init[] stuff.
	 *
	 * Get SGI IO Infrastructure drivers to init and register with 
	 * each other etc.
	 */

	DBG("--> sgi_master_io_infr_init: calling hubspc_init()\n");
	hubspc_init();

	DBG("--> sgi_master_io_infr_init: calling pciio_init()\n");
	pciio_init();

	DBG("--> sgi_master_io_infr_init: calling pcibr_init()\n");
	pcibr_init();

	DBG("--> sgi_master_io_infr_init: calling xtalk_init()\n");
	xtalk_init();

	DBG("--> sgi_master_io_infr_init: calling xbow_init()\n");
	xbow_init();

	DBG("--> sgi_master_io_infr_init: calling xbmon_init()\n");
	xbmon_init();

	DBG("--> sgi_master_io_infr_init: calling pciiox_init()\n");
	pciiox_init();

	DBG("--> sgi_master_io_infr_init: calling usrpci_init()\n");
	usrpci_init();

	DBG("--> sgi_master_io_infr_init: calling ioc3_init()\n");
	ioc3_init();

	/*
	 *
	 * Our IO Infrastructure drivers are in place .. 
	 * Initialize the whole IO Infrastructure .. xwidget/device probes.
	 *
	 */
	DBG("--> sgi_master_io_infr_init: Start Probe and IO Initialization\n");
	initialize_io();

	DBG("--> sgi_master_io_infr_init: Setting up SGI IO Links for Linux PCI\n");
	pci_bus_to_hcl_cvlink();

#ifdef CONFIG_PCIBA
	DBG("--> sgi_master_io_infr_init: calling pciba_init()\n");
	pciba_init();
#endif

	DBG("--> Leave sgi_master_io_infr_init: DONE setting up SGI Links for PCI\n");
}

/*
 * sgi_slave_io_infr_init - This routine must be called on all cpus except 
 * the Master CPU.
 */
void
sgi_slave_io_infr_init(void)
{
	/* Emulate cboot() .. */
	mlreset(1); /* This is a slave cpu */

	// per_hub_init(0); /* Need to get and send in actual cnode number */

	/* Done */
}

/*
 * One-time setup for MP SN.
 * Allocate per-node data, slurp prom klconfig information and
 * convert it to hwgraph information.
 */
void
sn_mp_setup(void)
{
	cnodeid_t	cnode;
	cpuid_t		cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		/* Skip holes in CPU space */
		if (cpu_enabled(cpu)) {
			init_platform_pda(cpu);
		}
	}

	/*
	 * Initialize platform-dependent vertices in the hwgraph:
	 *	module
	 *	node
	 *	cpu
	 *	memory
	 *	slot
	 *	hub
	 *	router
	 *	xbow
	 */

	DBG("sn_mp_io_setup: calling io_module_init()\n");
	io_module_init(); /* Use to be called module_init() .. */

	DBG("sn_mp_setup: calling klhwg_add_all_modules()\n");
	klhwg_add_all_modules(hwgraph_root);
	DBG("sn_mp_setup: calling klhwg_add_all_nodes()\n");
	klhwg_add_all_nodes(hwgraph_root);


	for (cnode = 0; cnode < numnodes; cnode++) {

		/*
		 * This routine clears the Hub's Interrupt registers.
		 */
		/*
		 * We need to move this intr_clear_all() routine 
		 * from SN/intr.c to a more appropriate file.
		 * Talk to Al Mayer.
		 */
#if defined(CONFIG_IA64_SGI_SN1)
                intr_clear_all(COMPACT_TO_NASID_NODEID(cnode));
#endif
		/* now init the hub */
	//	per_hub_init(cnode);

	}

#if defined(CONFIG_IA64_SGI_SN1)
	synergy_perf_init();
#endif

}

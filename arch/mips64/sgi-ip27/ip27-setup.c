/* $Id: ip27-setup.c,v 1.7 2000/03/07 15:45:29 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI IP27 specific setup.
 *
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silcon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/klconfig.h>
#include <asm/ioc3.h>
#include <asm/mipsregs.h>
#include <asm/sn/arch.h>
#include <asm/sn/sn_private.h>
#include <asm/pci/bridge.h>
#include <asm/paccess.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/sn0/hubio.h>

/* Check against user dumbness.  */
#ifdef CONFIG_VT
#error CONFIG_VT not allowed for IP27.
#endif

#undef DEBUG_SETUP
#ifdef DEBUG_SETUP
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif


/*
 * get_nasid() returns the physical node id number of the caller.
 */
nasid_t
get_nasid(void)
{
	return (nasid_t)((LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_NODEID_MASK)
	                 >> NSRI_NODEID_SHFT);
}

/* Extracted from the IOC3 meta driver.  FIXME.  */
static inline void ioc3_sio_init(void)
{
	struct ioc3 *ioc3;
	nasid_t nid;
        long loops;

	nid = get_nasid();
	ioc3 = (struct ioc3 *) KL_CONFIG_CH_CONS_INFO(nid)->memory_base;

	ioc3->sscr_a = 0;			/* PIO mode for uarta.  */
	ioc3->sscr_b = 0;			/* PIO mode for uartb.  */
	ioc3->sio_iec = ~0;
	ioc3->sio_ies = (SIO_IR_SA_INT | SIO_IR_SB_INT);

	loops=1000000; while(loops--);
	ioc3->sregs.uarta.iu_fcr = 0;
	ioc3->sregs.uartb.iu_fcr = 0;
	loops=1000000; while(loops--);
}

static inline void ioc3_eth_init(void)
{
	struct ioc3 *ioc3;
	nasid_t nid;

	nid = get_nasid();
	ioc3 = (struct ioc3 *) KL_CONFIG_CH_CONS_INFO(nid)->memory_base;

	ioc3->eier = 0;
}

/* Try to catch kernel missconfigurations and give user an indication what
   option to select.  */
static void __init verify_mode(void)
{
	int n_mode;

	n_mode = LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_MORENODES_MASK;
	printk("Machine is in %c mode.\n", n_mode ? 'N' : 'M');
#ifdef CONFIG_SGI_SN0_N_MODE
	if (!n_mode)
		panic("Kernel compiled for M mode.");
#else
	if (n_mode)
		panic("Kernel compiled for N mode.");
#endif
}

#define XBOW_WIDGET_PART_NUM    0x0
#define XXBOW_WIDGET_PART_NUM   0xd000          /* Xbridge */
#define BASE_XBOW_PORT  	8     /* Lowest external port */

static void __init pcibr_setup(void)
{
	int 			i;
	bridge_t 		*bridge; 
	volatile u64 		hubreg;
	nasid_t	 		nasid;
	xwidget_part_num_t	partnum;
	widgetreg_t 		widget_id;

	num_bridges = 0;
	/*
	 * find what's on our local node
	 */
	nasid = 0;
	hubreg = LOCAL_HUB_L(IIO_LLP_CSR);
	if (hubreg & IIO_LLP_CSR_IS_UP) {
		/* link is up */
		widget_id = *(volatile widgetreg_t *)
                        (RAW_NODE_SWIN_BASE(nasid, 0x0) + WIDGET_ID);
		partnum = XWIDGET_PART_NUM(widget_id);
		printk("pcibr_setup(): found partnum= 0x%x ", partnum);
		if (partnum == BRIDGE_WIDGET_PART_NUM) {
			/*
			 * found direct connected bridge so must be Origin200
			 */
			printk("...is bridge\n");
			num_bridges = 1;
        		bus_to_wid[0] = 0x8;
			bus_to_nid[0] = 0;
		}
		if (partnum == XBOW_WIDGET_PART_NUM) {
			lboard_t *brd;
			klxbow_t *xbow_p;
			/*
			 * found xbow, so may have multiple bridges
			 * need to probe xbow
			 */
			printk("...is xbow\n");

			if ((brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid),
                                   KLTYPE_MIDPLANE8)) == NULL)
				printk("argh\n");
			else
				printk("brd= 0x%x\n", brd);
			if ((xbow_p = (klxbow_t *)
			     find_component(brd, NULL, KLSTRUCT_XBOW)) == NULL)
				printk("argh\n");
			else {
			   for (i=HUB_WIDGET_ID_MIN; i<=HUB_WIDGET_ID_MAX; i++) {
				if (!XBOW_PORT_IS_ENABLED(xbow_p, i))
					continue;
				if (XBOW_PORT_TYPE_IO(xbow_p, i)) {
				   widget_id = *(volatile widgetreg_t *)
                        		   (RAW_NODE_SWIN_BASE(nasid, i) + WIDGET_ID);
				   partnum = XWIDGET_PART_NUM(widget_id);
				   if (partnum == BRIDGE_WIDGET_PART_NUM) {
					printk("widget 0x%x is a bridge\n", i);
					bus_to_wid[num_bridges] = i;
					bus_to_nid[num_bridges] = nasid;
					num_bridges++;
				   }
				}
			   }
			}
		}
		if (partnum == XXBOW_WIDGET_PART_NUM) {
			/*
			 * found xbridge, assume ibrick for now 
			 */
			printk("...is xbridge\n");
        		bus_to_wid[0] = 0xb;
        		bus_to_wid[1] = 0xe;
        		bus_to_wid[2] = 0xf;

        		bus_to_nid[0] = 0;
        		bus_to_nid[1] = 0;
        		bus_to_nid[2] = 0;

			num_bridges = 3;
		}
	}
	/*
         * set bridge registers
         */
	for (i=0; i<num_bridges; i++) {
		DBG("pcibr_setup: bus= %d  bus_to_wid[%2d]= %d  bus_to_nid[%2d]= %d\n",
                        i, i, bus_to_wid[i], i, bus_to_nid[i]);

		/*
		 * point to this bridge
		 */
		bridge = (bridge_t *) NODE_SWIN_BASE(bus_to_nid[i],bus_to_wid[i]);
		/*
	 	 * Clear all pending interrupts.
	 	 */
		bridge->b_int_rst_stat = (BRIDGE_IRR_ALL_CLR);
		/*
	 	 * Until otherwise set up, assume all interrupts are from slot 0
	 	 */
		bridge->b_int_device = (u32) 0x0;
		/*
	 	 * swap pio's to pci mem and io space (big windows)
	 	 */
		bridge->b_wid_control |= BRIDGE_CTRL_IO_SWAP;
		bridge->b_wid_control |= BRIDGE_CTRL_MEM_SWAP;

		bridge->b_wid_tflush;     /* wait until Bridge PIO complete */
	}
}

void __init ip27_setup(void)
{
	nasid_t nid;
	hubreg_t p, e;

	/*
	 * hub_rtc init and cpu clock intr enabled for later calibrate_delay.
	 */
	DBG("ip27_setup(): Entered.\n");
	per_cpu_init();
	nid = get_nasid();
	printk("IP27: Running on node %d.\n", nid);

	p = LOCAL_HUB_L(PI_CPU_PRESENT_A) & 1;
	e = LOCAL_HUB_L(PI_CPU_ENABLE_A) & 1;
	printk("Node %d has %s primary CPU%s.\n", nid,
	       p ? "a" : "no",
	       e ? ", CPU is running" : "");

	p = LOCAL_HUB_L(PI_CPU_PRESENT_B) & 1;
	e = LOCAL_HUB_L(PI_CPU_ENABLE_B) & 1;
	printk("Node %d has %s secondary CPU%s.\n", nid,
	       p ? "a" : "no",
	       e ? ", CPU is running" : "");

	verify_mode();
	ioc3_sio_init();
	ioc3_eth_init();

	DBG("ip27_setup(): calling pcibr_setup\n");
	/* set some bridge registers */
	pcibr_setup();
	DBG("ip27_setup(): Exit.\n");
}

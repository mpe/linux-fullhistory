/* 
 * Set these options for all host adapters.
 * 	- Memory mapped IO does not work on x86 because of cache
 *	  problems.
 *	- Test 1 does a bus mastering test, which will help
 *	  weed out brain damaged main boards.
 */


#define PERM_OPTIONS (OPTION_IO_MAPPED|OPTION_DEBUG_TEST1)

/*
 * Define SCSI_MALLOC to use scsi_malloc instead of kmalloc.  Other than
 * preventing deadlock, I'm not sure why we'd want to do this.
 */

#define SCSI_MALLOC

/*
 * Sponsored by 
 *	iX Multiuser Multitasking Magazine
 *	Hannover, Germany
 *	hm@ix.de
 *
 * Copyright 1993, 1994, 1995 Drew Eckhardt
 *      Visionary Computing 
 *      (Unix and Linux consulting and custom programming)
 *      drew@Colorado.EDU
 *	+1 (303) 786-7975
 *
 * TolerANT and SCSI SCRIPTS are registered trademarks of NCR Corporation.
 * 
 * For more information, please consult 
 *
 *
 * NCR 53C700/53C700-66
 * SCSI I/O Processor
 * Data Manual
 *
 * NCR53C710 
 * SCSI I/O Processor
 * Programmer's Guide
 *
 * NCR 53C810
 * PCI-SCSI I/O Processor
 * Data Manual
 *
 * NCR 53C810/53C820
 * PCI-SCSI I/O Processor Design In Guide
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * +1 (719) 578-3400
 *
 * Toll free literature number
 * +1 (800) 334-5454
 *
 * PCI BIOS Specification Revision
 * PCI Local Bus Specification
 * PCI System Design Guide
 *
 * PCI Special Interest Group
 * M/S HF3-15A
 * 5200 N.E. Elam Young Parkway
 * Hillsboro, Oregon 97124-6497
 * +1 (503) 696-2000 
 * +1 (800) 433-5177
 */

/*
 * Design issues : 
 * The cumulative latency needed to propagate a read/write request 
 * through the filesystem, buffer cache, driver stacks, SCSI host, and 
 * SCSI device is ultimately the limiting factor in throughput once we 
 * have a sufficiently fast host adapter.
 *  
 * So, to maximize performance we want to keep the ratio of latency to data 
 * transfer time to a minimum by
 * 1.  Minimizing the total number of commands sent (typical command latency
 *	including drive and busmastering host overhead is as high as 4.5ms)
 *	to transfer a given amount of data.  
 *
 *      This is accomplished by placing no arbitrary limit on the number
 *	of scatter/gather buffers supported, since we can transfer 1K
 *	per scatter/gather buffer without Eric's cluster patches, 
 *	4K with.  
 *
 * 2.  Minimizing the number of fatal interrupts serviced, since
 * 	fatal interrupts halt the SCSI I/O processor.  Basically,
 *	this means offloading the practical maximum amount of processing 
 *	to the SCSI chip.
 * 
 *	On the NCR53c810/820,  this is accomplished by using 
 *		interrupt-on-the-fly signals with the DSA address as a 
 *		parameter when commands complete, and only handling fatal 
 *		errors and SDTR / WDTR 	messages in the host code.
 *
 *	On the NCR53c710/720, interrupts are generated as on the NCR53c8x0,
 *		only the lack of a interrupt-on-the-fly facility complicates
 *		things.  
 *		
 * 	On the NCR53c700 and NCR53c700-66, operations that were done via 
 *		indirect, table mode on the more advanced chips have
 *		been replaced by calls through a jump table which 
 *		acts as a surrogate for the DSA.  Unfortunately, this 
 * 		means that we must service an interrupt for each 
 *		disconnect/reconnect.
 * 
 * 3.  Eliminating latency by pipelining operations at the different levels.
 * 	
 *	This driver allows a configurable number of commands to be enqueued
 *	for each target/lun combination (experimentally, I have discovered
 *	that two seems to work best) and will ultimately allow for 
 *	SCSI-II tagged queueing.
 * 	
 *
 * Architecture : 
 * This driver is built around two queues of commands waiting to 
 * be executed - the Linux issue queue, and the shared Linux/NCR  
 * queue which are manipulated by the NCR53c7xx_queue_command and 
 * NCR53c7x0_intr routines.
 *
 * When the higher level routines pass a SCSI request down to 
 * NCR53c7xx_queue_command, it looks to see if that target/lun 
 * is currently busy. If not, the command is inserted into the 
 * shared Linux/NCR queue, otherwise it is inserted into the Linux 
 * queue.
 *
 * As commands are completed, the interrupt routine is triggered,
 * looks for commands in the linked list of completed commands with
 * valid status, removes these commands from the list, calls 
 * the done routine, and flags their target/luns as not busy.
 *
 * Due to limitations in the intelligence of the NCR chips, certain
 * concessions are made.  In many cases, it is easier to dynamically 
 * generate/fixup code rather than calculate on the NCR at run time.  
 * So, code is generated or fixed up for
 *
 * - Handling data transfers, using a variable number of MOVE instructions
 *	interspersed with CALL MSG_IN, WHEN MSGIN instructions.
 *
 * 	The DATAIN and DATAOUT routines	are separate, so that an incorrect
 *	direction can be trapped, and space isn't wasted. 
 *
 *	It may turn out that we're better off using some sort 
 *	of table indirect instruction in a loop with a variable
 *	sized table on the NCR53c710 and newer chips.
 *
 * - Checking for reselection (NCR53c710 and better)
 *
 * - Handling the details of SCSI context switches (NCR53c710 and better),
 *	such as reprogramming appropriate synchronous parameters, 
 *	removing the dsa structure from the NCR's queue of outstanding
 *	commands, etc.
 *
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include "53c7,8xx.h"
#include "constants.h"
#include "sd.h"
#include<linux/stat.h>

struct proc_dir_entry proc_scsi_ncr53c7xx = {
    PROC_SCSI_NCR53C7xx, 9, "ncr53c7xx",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};

static void abnormal_finished (struct NCR53c7x0_cmd *cmd, int result);
static int NCR53c8xx_run_tests (struct Scsi_Host *host);
static int NCR53c8xx_script_len;
static int NCR53c8xx_dsa_len;
static void NCR53c7x0_intr(int irq, struct pt_regs * regs);
static int ncr_halt (struct Scsi_Host *host);
static void intr_phase_mismatch (struct Scsi_Host *host, struct NCR53c7x0_cmd 
    *cmd);
static void intr_dma (struct Scsi_Host *host, struct NCR53c7x0_cmd *cmd);
static void print_dsa (struct Scsi_Host *host, u32 *dsa);
static int print_insn (struct Scsi_Host *host, u32 *insn,
    const char *prefix, int kernel);

static void NCR53c8xx_dsa_fixup (struct NCR53c7x0_cmd *cmd);
static void NCR53c8x0_init_fixup (struct Scsi_Host *host);
static int NCR53c8x0_dstat_sir_intr (struct Scsi_Host *host, struct 
    NCR53c7x0_cmd *cmd);
static void NCR53c8x0_soft_reset (struct Scsi_Host *host);

static int perm_options = PERM_OPTIONS;

static struct Scsi_Host *first_host = NULL;	/* Head of list of NCR boards */
static Scsi_Host_Template *the_template = NULL;	


/*
 * TODO : 
 *
 * 1.  Implement single step / trace code?
 * 
 * 2.  The initial code has been tested on the NCR53c810.  I don't 
 *     have access to NCR53c700, 700-66 (Forex boards), NCR53c710
 *     (NCR Pentium systems), NCR53c720, or NCR53c820 boards to finish
 *     development on those platforms.
 *
 *     NCR53c820/720 - need to add wide transfer support, including WDTR 
 *     		negotiation, programming of wide transfer capabilities
 *		on reselection and table indirect selection.
 *
 *     NCR53c720/710 - need to add fatal interrupt or GEN code for 
 *		command completion signaling.   Need to take care of 
 *		ADD WITH CARRY instructions since carry is unimplemented.
 *		Also need to modify all SDID, SCID, etc. registers,
 *		and table indirect select code since these use bit
 *		fielded (ie 1<<target) instead of binary encoded
 *		target ids.  Also, SCNTL3 is _not_ automatically
 *		programmed on selection, so we need to add more code.
 * 
 *     NCR53c700/700-66 - need to add code to refix addresses on 
 *		every nexus change, eliminate all table indirect code.
 *
 * 3.  The NCR53c7x0 series is very popular on other platforms that 
 *     could be running Linux - ie, some high performance AMIGA SCSI 
 *     boards use it.  
 *	
 *     So, I should include #ifdef'd code so that it is 
 *     compatible with these systems.
 *	
 *     Specifically, the little Endian assumptions I made in my 
 *     bit fields need to change, and if the NCR doesn't see memory
 *     the right way, we need to provide options to reverse words
 *     when the scripts are relocated.
 *
 * 4.  Implement code to include page table entries for the 
 *     area occupied by memory mapped boards so we don't have 
 *     to use the potentially slower I/O accesses.
 */

/* 
 * XXX - note that my assembler was modified so that internally,
 * the names used can take a prefix, so that there is no conflict
 * between multiple copies of the same script assembled with 
 * different defines.
 *
 *
 * Allow for simultaneous existence of multiple SCSI scripts so we 
 * can have a single driver binary for all of the family.
 *
 * - one for NCR53c700 and NCR53c700-66 chips	(not yet supported)
 * - one for NCR53c710 and NCR53c720 chips	(not yet supported)
 * - one for NCR53c810 and NCR53c820 chips 	(only the NCR53c810 is
 *	currently supported)
 *
 * For the very similar chips, we should probably hack the fixup code
 * and interrupt code so that it works everywhere, but I suspect the 
 * NCR53c700 is going to need it's own fixup routine.
 */

/*
 * Use to translate between device IDs of various types.
 */

struct pci_chip {
    unsigned short pci_device_id;
    int chip;
    int min_revision;
    int max_revision;
};

static struct pci_chip pci_chip_ids[] = { 
    {PCI_DEVICE_ID_NCR_53C810, 810, 1, 1}, 
    {PCI_DEVICE_ID_NCR_53C815, 815, 2, 3},
    {PCI_DEVICE_ID_NCR_53C820, 820, -1, -1},
    {PCI_DEVICE_ID_NCR_53C825, 825, -1, -1}
};

#define NPCI_CHIP_IDS (sizeof (pci_chip_ids) / sizeof(pci_chip_ids[0]))


/* Forced detection and autoprobe code for various hardware */

static struct override {
    int chip;	/* 700, 70066, 710, 720, 810, 820 */
    int board;	/* Any special board level gunk */
    unsigned pci:1;
    union {
	struct {
	    int base;	/* Memory address - indicates memory mapped regs */
	    int io_port;/* I/O port address - indicates I/O mapped regs */
    	    int irq;	/* IRQ line */		
    	    int dma;	/* DMA channel 		- often none */
	} normal;
	struct {
	    int bus;
	    int device;
	    int function;
	} pci;
    } data;
    int options;
} overrides [4] = {{0,},};
static int commandline_current = 0;
static int no_overrides = 0;

#if 0
#define OVERRIDE_LIMIT (sizeof(overrides) / sizeof(struct override))
#else
#define OVERRIDE_LIMIT commandline_current
#endif

/*
 * Function : static internal_setup(int board, int chip, char *str, int *ints)
 *
 * Purpose : LILO command line initialization of the overrides array,
 * 
 * Inputs : board - currently, unsupported.  chip - 700, 70066, 710, 720
 * 	810, 815, 820, 825, although currently only the NCR53c810 is 
 *	supported.
 * 
 */

static void internal_setup(int board, int chip, char *str, int *ints) {
    unsigned char pci;		/* Specifies a PCI override, with bus, device,
				   function */

    pci = (str && !strcmp (str, "pci")) ? 1 : 0;
    
/*
 * Override syntaxes are as follows : 
 * ncr53c700,ncr53c700-66,ncr53c710,ncr53c720=mem,io,irq,dma
 * ncr53c810,ncr53c820,ncr53c825=mem,io,irq or pci,bus,device,function
 */

    if (commandline_current < OVERRIDE_LIMIT) {
	overrides[commandline_current].pci = pci ? 1 : 0;
	if (!pci) {
	    overrides[commandline_current].data.normal.base = ints[1];
	    overrides[commandline_current].data.normal.io_port = ints[2];
	    overrides[commandline_current].data.normal.irq = ints[3];
    	    overrides[commandline_current].data.normal.dma = (ints[0] >= 4) ?
    	    	ints[4] : DMA_NONE;
    	    overrides[commandline_current].options = (ints[0] >= 5) ?
    	    	ints[5] : 0;
	} else {
	    overrides[commandline_current].data.pci.bus = ints[1];
	    overrides[commandline_current].data.pci.device = ints[2];
	    overrides[commandline_current].data.pci.function = ints[3];
    	    overrides[commandline_current].options = (ints[0] >= 4) ?
    	    	ints[4] : 0;
	}
	overrides[commandline_current].board = board;
	overrides[commandline_current].chip = chip;
	++commandline_current;
    	++no_overrides;
    } else {
	printk ("53c7,7x0.c:internal_setup() : too many overrides\n");
    }
}

/*
 * XXX - we might want to implement a single override function
 *       with a chip type field, revamp the command line configuration,
 * 	 etc.
 */

#define setup_wrapper(x) 				\
void ncr53c##x##_setup (char *str, int *ints) {		\
    internal_setup (BOARD_GENERIC, x, str, ints);	\
}

setup_wrapper(700)
setup_wrapper(70066)
setup_wrapper(710)
setup_wrapper(720)
setup_wrapper(810)
setup_wrapper(815)
setup_wrapper(820)
setup_wrapper(825)

/* 
 * Function : static int NCR53c7x0_init (struct Scsi_Host *host)
 *
 * Purpose :  initialize the internal structures for a given SCSI host
 *
 * Inputs : host - pointer to this host adapter's structure/ 
 *
 * Preconditions : when this function is called, the chip_type 
 * 	field of the hostdata structure MUST have been set.
 */

static int 
NCR53c7x0_init (struct Scsi_Host *host) {
    NCR53c7x0_local_declare();
    /* unsigned char tmp; */
    int i, j, ccf;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    struct Scsi_Host *search;
    NCR53c7x0_local_setup(host);

    switch (hostdata->chip) {
    case 810:
    case 815:
    case 820:
    case 825:
    	hostdata->dstat_sir_intr = NCR53c8x0_dstat_sir_intr;
    	hostdata->init_save_regs = NULL;
    	hostdata->dsa_fixup = NCR53c8xx_dsa_fixup;
    	hostdata->init_fixup = NCR53c8x0_init_fixup;
    	hostdata->soft_reset = NCR53c8x0_soft_reset;
	hostdata->run_tests = NCR53c8xx_run_tests;
/* Is the SCSI clock ever anything else on these chips? */
	hostdata->scsi_clock = 40000000;
    	break;
    default:
	printk ("scsi%d : chip type of %d is not supported yet, detaching.\n",
	    host->host_no, hostdata->chip);
	scsi_unregister (host);
	return -1;
    }

    /* Assign constants accessed by NCR */
    hostdata->NCR53c7xx_zero = 0;			
    hostdata->NCR53c7xx_msg_reject = MESSAGE_REJECT;
    hostdata->NCR53c7xx_msg_abort = ABORT;
    hostdata->NCR53c7xx_msg_nop = NOP;

    /*
     * Set up an interrupt handler if we aren't already sharing an IRQ
     * with another board.
     */

    for (search = first_host; search && ((search->hostt != the_template) ||
	(search->irq != host->irq)); search=search->next);

    if (!search) {
	if (request_irq(host->irq, NCR53c7x0_intr, SA_INTERRUPT, "53c7,8xx")) {
	    printk("scsi%d : IRQ%d not free, detaching\n", 
		host->host_no, host->irq);
	    scsi_unregister (host);
	    return -1;
	} 
    } else {
	printk("scsi%d : using interrupt handler previously installed for scsi%d\n",
	    host->host_no, search->host_no);
    }

    printk ("scsi%d : using %s mapped access\n", host->host_no, 
	(hostdata->options & OPTION_MEMORY_MAPPED) ? "memory" : 
	 "io");

    hostdata->dmode = (hostdata->chip == 700 || hostdata->chip == 70066) ? 
	DMODE_REG_00 : DMODE_REG_10;
    hostdata->istat = ((hostdata->chip / 100) == 8) ? 
    	ISTAT_REG_800 : ISTAT_REG_700;

/* Only the ISTAT register is readable when the NCR is running, so make 
   sure it's halted. */
    ncr_halt(host);

/* 
 * XXX - the NCR53c700 uses bitfielded registers for SCID, SDID, etc,
 *	as does the 710 with one bit per SCSI ID.  Conversely, the NCR
 * 	uses a normal, 3 bit binary representation of these values.
 *
 * Get the rest of the NCR documentation, and FIND OUT where the change
 * was.
 */
#if 0
    tmp = hostdata->this_id_mask = NCR53c7x0_read8(SCID_REG);
    for (host->this_id = 0; tmp != 1; tmp >>=1, ++host->this_id);
#else
    host->this_id = NCR53c7x0_read8(SCID_REG) & 7;
    hostdata->this_id_mask = 1 << host->this_id;
#endif

    printk("scsi%d : using initiator ID %d\n", host->host_no,
    	host->this_id);

    /*
     * Save important registers to allow a soft reset.
     */

    if ((hostdata->chip / 100) == 8) {
    /* 
     * CTEST4 controls burst mode disable.
     */
	hostdata->saved_ctest4 = NCR53c7x0_read8(CTEST4_REG_800) & 
    	    CTEST4_800_SAVE;
    } else {
    /*
     * CTEST7 controls cache snooping, burst mode, and support for 
     * external differential drivers.
     */
	hostdata->saved_ctest7 = NCR53c7x0_read8(CTEST7_REG) & CTEST7_SAVE;
    }

    /*
     * On NCR53c700 series chips, DCNTL controls the SCSI clock divisor,
     * on 800 series chips, it allows for a totem-pole IRQ driver.
     */
    hostdata->saved_dcntl = NCR53c7x0_read8(DCNTL_REG);
    
    if ((hostdata->chip / 100) == 8)
	printk ("scsi%d : using %s interrupts\n", host->host_no,
	    (hostdata->saved_dcntl & DCNTL_800_IRQM) ? "edge triggered" :
	    "level active");

    /*
     * DMODE controls DMA burst length, and on 700 series chips,
     * 286 mode and bus width  
     */
    hostdata->saved_dmode = NCR53c7x0_read8(hostdata->dmode);

    /* 
     * Now that burst length and enabled/disabled status is known, 
     * clue the user in on it.
     */
   
    if ((hostdata->chip / 100) == 8) {
	if (hostdata->saved_ctest4 & CTEST4_800_BDIS) {
	    printk ("scsi%d : burst mode disabled\n", host->host_no);
	} else {
	    switch (hostdata->saved_dmode & DMODE_BL_MASK) {
	    case DMODE_BL_2: i = 2; break;
	    case DMODE_BL_4: i = 4; break;
	    case DMODE_BL_8: i = 8; break;
	    case DMODE_BL_16: i = 16; break;
	     default: i = 0;
	    }
	    printk ("scsi%d : burst length %d\n", host->host_no, i);
	}
    }

    /*
     * On NCR53c810 and NCR53c820 chips, SCNTL3 contails the synchronous
     * and normal clock conversion factors.
     */
    if (hostdata->chip / 100 == 8)  {
	hostdata->saved_scntl3 = NCR53c7x0_read8(SCNTL3_REG_800);
	ccf = hostdata->saved_scntl3 & SCNTL3_800_CCF_MASK;
    } else
    	ccf = 0;

    /*
     * If we don't have a SCSI clock programmed, pick one on the upper
     * bound of that allowed by NCR so that our transfers err on the 
     * slow side, since transfer period must be >= the agreed 
     * appon period.
     */

    if (!hostdata->scsi_clock) 
	switch(ccf) {
	case 1: hostdata->scsi_clock = 25000000; break;	/* Divide by 1.0 */
	case 2: hostdata->scsi_clock = 37500000; break; /* Divide by 1.5 */
	case 3: hostdata->scsi_clock = 50000000; break; /* Divide by 2.0 */
	case 0: 					/* Divide by 3.0 */
 	case 4:	hostdata->scsi_clock = 66000000; break; 
	default: 
	    printk ("scsi%d : clock conversion factor %d unknown.\n"
		    "         synchronous transfers disabled\n",
		    host->host_no, ccf);
	    hostdata->options &= ~OPTION_SYNCHRONOUS;
	    hostdata->scsi_clock = 0; 
	}

    printk ("scsi%d : using %dMHz SCSI clock\n", host->host_no, 
	hostdata->scsi_clock / 1000000);
    /*
     * Initialize per-target structures, including busy flags and 
     * synchronous transfer parameters.
     */

    for (i = 0; i < 8; ++i) {
    	hostdata->cmd_allocated[i] = 0;
    	for (j = 0; j < 8; ++j)
	    hostdata->busy[i][j] = 0;
	/* 
	 * NCR53c700 and NCR53c700-66 chips lack the DSA and use a 
	 * different architecture.  For chips using the DSA architecture,
	 * initialize the per-target synchronous parameters. 
	 */
	if (hostdata->chip != 700 && hostdata->chip != 70066) {
	    hostdata->sync[i].select_indirect |= (i << 16); 
	    /* XXX - program SCSI script for immediate return */ 
	    hostdata->sync[i].script[0] = (DCMD_TYPE_TCI|DCMD_TCI_OP_RETURN) << 24 | 
		DBC_TCI_TRUE;
	    switch (hostdata->chip) {
	    /* Clock divisor */
    	    case 825:
	    case 820:
		/* Fall through to 810 */
    	    case 815:
	    case 810:
		hostdata->sync[i].select_indirect |= (hostdata->saved_scntl3) << 24;
		break;
	    default:
	    }
    	}
    }

    hostdata->issue_queue = hostdata->running_list = 
    	hostdata->finished_queue = NULL;
    hostdata->issue_dsa_head = 0;
    hostdata->issue_dsa_tail = NULL;

    if (hostdata->init_save_regs)
    	hostdata->init_save_regs (host);
    if (hostdata->init_fixup)
    	hostdata->init_fixup (host);

    if (!the_template) {
	the_template = host->hostt;
	first_host = host;
    }

    hostdata->idle = 1;

    /* 
     * Linux SCSI drivers have always been plagued with initialization 
     * problems - some didn't work with the BIOS disabled since they expected
     * initialization from it, some didn't work when the networking code
     * was enabled and registers got scrambled, etc.
     *
     * To avoid problems like this, in the future, we will do a soft 
     * reset on the SCSI chip, taking it back to a sane state.
     */

    hostdata->soft_reset (host);

    hostdata->debug_count_limit = -1;
    hostdata->intrs = -1;
    hostdata->expecting_iid = 0;
    hostdata->expecting_sto = 0;

    if ((hostdata->run_tests && hostdata->run_tests(host) == -1) ||
	(hostdata->options & OPTION_DEBUG_TESTS_ONLY)) {
    	/* XXX Should disable interrupts, etc. here */
	scsi_unregister (host);
    	return -1;
    } else 
    	return 0;
}

/* 
 * Function : static int normal_init(Scsi_Host_Template *tpnt, int board, 
 *	int chip, int base, int io_port, int irq, int dma, int pcivalid,
 *	unsigned char pci_bus, unsigned char pci_device_fn,
 *	int options);
 *
 * Purpose : initializes a NCR53c7,8x0 based on base addresses,
 *	IRQ, and DMA channel.	
 *	
 *	Useful where a new NCR chip is backwards compatible with
 *	a supported chip, but the DEVICE ID has changed so it 
 *	doesn't show up when the autoprobe does a pcibios_find_device.
 *
 * Inputs : tpnt - Template for this SCSI adapter, board - board level
 *	product, chip - 810, 820, or 825, bus - PCI bus, device_fn -
 *	device and function encoding as used by PCI BIOS calls.
 * 
 * Returns : 0 on success, -1 on failure.
 *
 */

static int normal_init (Scsi_Host_Template *tpnt, int board, int chip, 
    u32 base, int io_port, int irq, int dma, int pci_valid, 
    unsigned char pci_bus, unsigned char pci_device_fn, int options) {
    struct Scsi_Host *instance;
    struct NCR53c7x0_hostdata *hostdata;
    char chip_str[80];
    int script_len = 0, dsa_len = 0, size = 0, max_cmd_size = 0;
    int ok = 0;

    
    options |= perm_options;

    switch (chip) {
    case 825:
    case 820:
    case 815:
    case 810:
	script_len = NCR53c8xx_script_len;
    	dsa_len = NCR53c8xx_dsa_len;
    	options |= OPTION_INTFLY;
    	sprintf (chip_str, "NCR53c%d", chip);
    	break;
    default:
    	printk("scsi-ncr53c7,8xx : unsupported SCSI chip %d\n", chip);
    	return -1;
    }

    printk("scsi-ncr53c7,8xx : %s at memory 0x%x, io 0x%x, irq %d",
    	chip_str, base, io_port, irq);
    if (dma == DMA_NONE)
    	printk("\n");
    else 
    	printk(", dma %d\n", dma);

    if ((chip / 100 == 8) && !pci_valid) 
	printk ("scsi-ncr53c7,8xx : for better reliability and performance, please use the\n" 
		"        PCI override instead.\n"
		"	 Syntax : ncr53c8{10,15,20,25}=pci,<bus>,<device>,<function>\n"
		"                 <bus> and <device> are usually 0.\n");

    if (options & OPTION_DEBUG_PROBE_ONLY) {
    	printk ("scsi-ncr53c7,8xx : probe only enabled, aborting initialization\n");
    	return -1;
    }

    max_cmd_size = sizeof(struct NCR53c7x0_cmd) + dsa_len +
    	/* Size of dynamic part of command structure : */
	2 * /* Worst case : we don't know if we need DATA IN or DATA out */
		( 2 * /* Current instructions per scatter/gather segment */ 
		  tpnt->sg_tablesize + 
		  3 /* Current startup / termination required per phase */
		) *
	8 /* Each instruction is eight bytes */;
    /* Note that alignment will be guaranteed, since we put the command
       allocated at probe time after the fixed-up SCSI script, which 
       consists of 32 bit words, aligned on a 32 bit boundary. */ 

    /* Allocate fixed part of hostdata, dynamic part to hold appropriate
       SCSI SCRIPT(tm) plus a single, maximum-sized NCR53c7x0_cmd structure.

       We need a NCR53c7x0_cmd structure for scan_scsis() when we are 
       not loaded as a module, and when we're loaded as a module, we 
       can't use a non-dynamically allocated structure because modules
       are vmalloc()'d, which can allow structures to cross page 
       boundaries and breaks our physical/virtual address assumptions
       for DMA.

       So, we stick it past the end of our hostdata structure.

       ASSUMPTION : 
       	 Regardless of how many simultaneous SCSI commands we allow,
	 the probe code only executes a _single_ instruction at a time,
	 so we only need one here, and don't need to allocate NCR53c7x0_cmd
	 structures for each target until we are no longer in scan_scsis
	 and kmalloc() has become functional (memory_init() happens 
	 after all device driver initialization).
    */

    size = sizeof(struct NCR53c7x0_hostdata) + script_len + max_cmd_size;

    instance = scsi_register (tpnt, size);
    if (!instance)
	return -1;


    /* FIXME : if we ever support an ISA NCR53c7xx based board, we
       need to check if the chip is running in a 16 bit mode, and if so 
       unregister it if it is past the 16M (0x1000000) mark */
   	
    hostdata = (struct NCR53c7x0_hostdata *) 
    	instance->hostdata;
    hostdata->size = size;
    hostdata->script_count = script_len / sizeof(u32);
    hostdata = (struct NCR53c7x0_hostdata *) instance->hostdata;
    hostdata->board = board;
    hostdata->chip = chip;
    if ((hostdata->pci_valid = pci_valid)) {
	hostdata->pci_bus = pci_bus;
	hostdata->pci_device_fn = pci_device_fn;
    }

    /*
     * Being memory mapped is more desirable, since 
     *
     * - Memory accesses may be faster.
     *
     * - The destination and source address spaces are the same for 
     *	 all instructions, meaning we don't have to twiddle dmode or 
     *	 any other registers.
     *
     * So, we try for memory mapped, and if we don't get it,
     * we go for port mapped, and that failing we tell the user
     * it can't work.
     */

    if (base) {
	instance->base = (unsigned char*) (unsigned long) base;
	/* Check for forced I/O mapping */
    	if (!(options & OPTION_IO_MAPPED)) {
	    options |= OPTION_MEMORY_MAPPED;
	    ok = 1;
	}
    } else {
	options &= ~OPTION_MEMORY_MAPPED;
    }

    if (io_port) {
	instance->io_port = io_port;
	options |= OPTION_IO_MAPPED;
	ok = 1;
    } else {
	options &= ~OPTION_IO_MAPPED;
    }

    if (!ok) {
	printk ("scsi%d : not initializing, no I/O or memory mapping known \n",
	    instance->host_no);
	scsi_unregister (instance);
	return -1;
    }
    instance->irq = irq;
    instance->dma_channel = dma;

    hostdata->options = options;
    hostdata->dsa_size = dsa_len;
    hostdata->max_cmd_size = max_cmd_size;
    hostdata->num_cmds = 1;
    /* Initialize single command */
    hostdata->free = (struct NCR53c7x0_cmd *) 
	(hostdata->script + hostdata->script_count);
    hostdata->free->real = (void *) hostdata->free;
    hostdata->free->size = max_cmd_size;
    hostdata->free->free = NULL;
    hostdata->free->next = NULL;


    return NCR53c7x0_init(instance);
}


/* 
 * Function : static int pci_init(Scsi_Host_Template *tpnt, int board, 
 *	int chip, int bus, int device_fn, int options)
 *
 * Purpose : initializes a NCR53c800 family based on the PCI
 *	bus, device, and function location of it.  Allows 
 * 	reprogramming of latency timer and determining addresses
 *	and whether bus mastering, etc. are OK.
 *	
 *	Useful where a new NCR chip is backwards compatible with
 *	a supported chip, but the DEVICE ID has changed so it 
 *	doesn't show up when the autoprobe does a pcibios_find_device.
 *
 * Inputs : tpnt - Template for this SCSI adapter, board - board level
 *	product, chip - 810, 820, or 825, bus - PCI bus, device_fn -
 *	device and function encoding as used by PCI BIOS calls.
 * 
 * Returns : 0 on success, -1 on failure.
 *
 */

static int ncr_pci_init (Scsi_Host_Template *tpnt, int board, int chip, 
    unsigned char bus, unsigned char device_fn, int options) {
    unsigned short vendor_id, device_id, command;
    u32 base;
    int io_port; 
    unsigned char irq, revision;
    int error, expected_chip;
    int expected_id = -1, max_revision = -1, min_revision = -1;
    int i;

    printk("scsi-ncr53c7,8xx : at PCI bus %d, device %d,  function %d\n",
	bus, (int) (device_fn & 0xf8) >> 3, 
    	(int) device_fn & 7);

    if (!pcibios_present) {
	printk("scsi-ncr53c7,8xx : not initializing due to lack of PCI BIOS,\n"
	       "        try using memory, port, irq override instead.\n");
	return -1;
    }

    if ((error = pcibios_read_config_word (bus, device_fn, PCI_VENDOR_ID, 
	&vendor_id)) ||
	(error = pcibios_read_config_word (bus, device_fn, PCI_DEVICE_ID, 
	    &device_id)) ||
	(error = pcibios_read_config_word (bus, device_fn, PCI_COMMAND, 
	    &command)) ||
	(error = pcibios_read_config_dword (bus, device_fn, 
	    PCI_BASE_ADDRESS_0, (int *) &io_port)) || 
	(error = pcibios_read_config_dword (bus, device_fn, 
	    PCI_BASE_ADDRESS_1, (int *) &base)) ||
	(error = pcibios_read_config_byte (bus, device_fn, PCI_CLASS_REVISION,
	    &revision)) ||
	(error = pcibios_read_config_byte (bus, device_fn, PCI_INTERRUPT_LINE,
	    &irq))) {
	printk ("scsi-ncr53c7,8xx : error %s not initializing due to error reading configuration space\n"
		"        perhaps you specified an incorrect PCI bus, device, or function.\n"
		, pci_strbioserr(error));
	return -1;
    }

    /* If any one ever clones the NCR chips, this will have to change */

    if (vendor_id != PCI_VENDOR_ID_NCR) {
	printk ("scsi-ncr53c7,8xx : not initializing, 0x%04x is not NCR vendor ID\n",
	    (int) vendor_id);
	return -1;
    }


    /* 
     * Bit 0 is the address space indicator and must be one for I/O
     * space mappings, bit 1 is reserved, discard them after checking
     * that they have the correct value of 1.
     */

    if (command & PCI_COMMAND_IO) { 
	if ((io_port & 3) != 1) {
	    printk ("scsi-ncr53c7,8xx : disabling I/O mapping since base address 0 (0x%x)\n"
    	    	    "        bits 0..1 indicate a non-IO mapping\n", io_port);
	    io_port = 0;
	} else
	    io_port &= PCI_BASE_ADDRESS_IO_MASK;
    } else {
	    io_port = 0;
    }

    if (command & PCI_COMMAND_MEMORY) {
	if ((base & PCI_BASE_ADDRESS_SPACE) != PCI_BASE_ADDRESS_SPACE_MEMORY) {
	    printk("scsi-ncr53c7,8xx : disabling memory mapping since base address 1\n"
		   "        contains a non-memory mapping\n");
	    base = 0;
	} else 
	    base &= PCI_BASE_ADDRESS_MEM_MASK;
    } else {
	    base = 0;
    }
	
    if (!io_port && !base) {
	printk ("scsi-ncr53c7,8xx : not initializing, both I/O and memory mappings disabled\n");
	return -1;
    }
	
    if (!(command & PCI_COMMAND_MASTER)) {
	printk ("scsi-ncr53c7,8xx : not initializing, BUS MASTERING was disabled\n");
	return -1;
    }

    for (i = 0; i < NPCI_CHIP_IDS; ++i) {
	if (device_id == pci_chip_ids[i].pci_device_id) {
	    max_revision = pci_chip_ids[i].max_revision;
	    min_revision = pci_chip_ids[i].min_revision;
	    expected_chip = pci_chip_ids[i].chip;
	}
	if (chip == pci_chip_ids[i].chip)
	    expected_id = pci_chip_ids[i].pci_device_id;
    }

    if (chip && device_id != expected_id) 
	printk ("scsi-ncr53c7,8xx : warning : device id of 0x%04x doesn't\n"
		"                   match expected 0x%04x\n",
	    (unsigned int) device_id, (unsigned int) expected_id );
    
    if (max_revision != -1 && revision > max_revision) 
	printk ("scsi-ncr53c7,8xx : warning : revision of %d is greater than %d.\n",
	    (int) revision, max_revision);
    else if (min_revision != -1 && revision < min_revision)
	printk ("scsi-ncr53c7,8xx : warning : revision of %d is less than %d.\n",
	    (int) revision, min_revision);

    return normal_init (tpnt, board, chip, (int) base, io_port, 
	(int) irq, DMA_NONE, 1, bus, device_fn, options);
}


/* 
 * Function : int NCR53c7xx_detect(Scsi_Host_Template *tpnt)
 *
 * Purpose : detects and initializes NCR53c7,8x0 SCSI chips
 *	that were autoprobed, overridden on the LILO command line, 
 *	or specified at compile time.
 *
 * Inputs : tpnt - template for this SCSI adapter
 * 
 * Returns : number of host adapters detected
 *
 */

int NCR53c7xx_detect(Scsi_Host_Template *tpnt) {
    int i;
    int current_override;
    int count;			/* Number of boards detected */
    unsigned char pci_bus, pci_device_fn;
    static short pci_index=0;	/* Device index to PCI BIOS calls */

    tpnt->proc_dir = &proc_scsi_ncr53c7xx;

    for (current_override = count = 0; current_override < OVERRIDE_LIMIT; 
	 ++current_override) {
	 if (overrides[current_override].pci ? 
	    !ncr_pci_init (tpnt, overrides[current_override].board,
		overrides[current_override].chip,
		(unsigned char) overrides[current_override].data.pci.bus,
		(((overrides[current_override].data.pci.device
		<< 3) & 0xf8)|(overrides[current_override].data.pci.function & 
		7)), overrides[current_override].options):
	    !normal_init (tpnt, overrides[current_override].board, 
		overrides[current_override].chip, 
		overrides[current_override].data.normal.base, 
		overrides[current_override].data.normal.io_port,
		overrides[current_override].data.normal.irq,
		overrides[current_override].data.normal.dma,
		0 /* PCI data invalid */, 0 /* PCI bus place holder */,  
		0 /* PCI device_function place holder */,
    	    	overrides[current_override].options)) {
    	    ++count;
	} 
    }

    if (pcibios_present()) {
	for (i = 0; i < NPCI_CHIP_IDS; ++i) 
	    for (pci_index = 0;
		!pcibios_find_device (PCI_VENDOR_ID_NCR, 
		    pci_chip_ids[i].pci_device_id, pci_index, &pci_bus, 
		    &pci_device_fn) && 
		!ncr_pci_init (tpnt, BOARD_GENERIC, pci_chip_ids[i].chip, 
		    pci_bus, pci_device_fn, /* no options */ 0); 
		++count, ++pci_index);
    }
    return count;
}

/* NCR53c810 and NCR53c820 script handling code */

#include "53c8xx_d.h"
static int NCR53c8xx_script_len = sizeof (SCRIPT);
static int NCR53c8xx_dsa_len = A_dsa_end + Ent_dsa_zero - Ent_dsa_code_template;

/* 
 * Function : static void NCR53c8x0_init_fixup (struct Scsi_Host *host)
 *
 * Purpose :  copy and fixup the SCSI SCRIPTS(tm) code for this device.
 *
 * Inputs : host - pointer to this host adapter's structure
 *
 */

static void 
NCR53c8x0_init_fixup (struct Scsi_Host *host) {
    NCR53c7x0_local_declare();
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    unsigned char tmp;
    int i, ncr_to_memory, memory_to_ncr, ncr_to_ncr;
    u32 base;
    NCR53c7x0_local_setup(host);



    /* XXX - NOTE : this code MUST be made endian aware */
    /*  Copy code into buffer that was allocated at detection time.  */
    memcpy ((void *) hostdata->script, (void *) SCRIPT, 
	sizeof(SCRIPT));
    /* Fixup labels */
    for (i = 0; i < PATCHES; ++i) 
	hostdata->script[LABELPATCHES[i]] +=
	    virt_to_bus(hostdata->script);
    /* Fixup addresses of constants that used to be EXTERNAL */

    patch_abs_32 (hostdata->script, 0, NCR53c7xx_msg_abort, 
    	virt_to_bus(&hostdata->NCR53c7xx_msg_abort));
    patch_abs_32 (hostdata->script, 0, NCR53c7xx_msg_reject, 
    	virt_to_bus(&hostdata->NCR53c7xx_msg_reject));
    patch_abs_32 (hostdata->script, 0, NCR53c7xx_zero, 
    	virt_to_bus(&hostdata->NCR53c7xx_zero));
    patch_abs_32 (hostdata->script, 0, NCR53c7xx_sink, 
    	virt_to_bus(&hostdata->NCR53c7xx_sink));

    /* Fixup references to external variables: */
    for (i = 0; i < EXTERNAL_PATCHES_LEN; ++i)
	hostdata->script[EXTERNAL_PATCHES[i].offset] +=
	  virt_to_bus(EXTERNAL_PATCHES[i].address);

    /* 
     * Fixup absolutes set at boot-time.
     * 
     * All Absolute variables suffixed with "dsa_" and "int_"
     * are constants, and need no fixup provided the assembler has done 
     * it for us (I don't know what the "real" NCR assembler does in 
     * this case, my assembler does the right magic).
     */

    /*
     * Just for the hell of it, preserve the settings of 
     * Burst Length and Enable Read Line bits from the DMODE 
     * register.  Make sure SCRIPTS start automagically.
     */

    tmp = NCR53c7x0_read8(DMODE_REG_10);
    tmp &= (DMODE_800_ERL | DMODE_BL_MASK);

    if (!(hostdata->options & OPTION_MEMORY_MAPPED)) {
    	base = (u32) host->io_port;
    	memory_to_ncr = tmp|DMODE_800_DIOM;
    	ncr_to_memory = tmp|DMODE_800_SIOM;
    	ncr_to_ncr = tmp|DMODE_800_DIOM|DMODE_800_SIOM;
    } else {
    	base = virt_to_phys(host->base);
    	ncr_to_ncr = memory_to_ncr = ncr_to_memory = tmp;
    }

    patch_abs_32 (hostdata->script, 0, addr_scratch, base + SCRATCHA_REG_800);
    patch_abs_32 (hostdata->script, 0, addr_sfbr, base + SFBR_REG);
    patch_abs_32 (hostdata->script, 0, addr_temp, base + TEMP_REG);

    /*
     * I needed some variables in the script to be accessible to 
     * both the NCR chip and the host processor. For these variables,
     * I made the arbitrary decision to store them directly in the 
     * hostdata structure rather than in the RELATIVE area of the 
     * SCRIPTS.
     */


    patch_abs_rwri_data (hostdata->script, 0, dmode_memory_to_memory, tmp);
    patch_abs_rwri_data (hostdata->script, 0, dmode_memory_to_ncr, memory_to_ncr);
    patch_abs_rwri_data (hostdata->script, 0, dmode_ncr_to_memory, ncr_to_memory);
    patch_abs_rwri_data (hostdata->script, 0, dmode_ncr_to_ncr, ncr_to_ncr);

    patch_abs_32 (hostdata->script, 0, issue_dsa_head,
		  virt_to_bus((void*)&hostdata->issue_dsa_head));
    patch_abs_32 (hostdata->script, 0, msg_buf,
		  virt_to_bus((void*)&hostdata->msg_buf));
    patch_abs_32 (hostdata->script, 0, reconnect_dsa_head,
		  virt_to_bus((void*)&hostdata->reconnect_dsa_head));
    patch_abs_32 (hostdata->script, 0, reselected_identify,
		  virt_to_bus((void*)&hostdata->reselected_identify));
    patch_abs_32 (hostdata->script, 0, reselected_tag,
		  virt_to_bus((void*)&hostdata->reselected_tag));

    patch_abs_32 (hostdata->script, 0, test_dest,
		  virt_to_bus((void*)&hostdata->test_dest));
    patch_abs_32 (hostdata->script, 0, test_src, virt_to_bus(&hostdata->test_source));


    /*
     * Make sure the NCR and Linux code agree on the location of 
     * certain fields.
     */

/* 
 * XXX - for cleanness, E_* fields should be type u32 *
 * and should reflect the _relocated_ addresses.  Change this.
 */
    hostdata->E_accept_message = Ent_accept_message;
    hostdata->E_command_complete = Ent_command_complete;		
    hostdata->E_debug_break = Ent_debug_break;	
    hostdata->E_dsa_code_template = Ent_dsa_code_template;
    hostdata->E_dsa_code_template_end = Ent_dsa_code_template_end;
    hostdata->E_initiator_abort = Ent_initiator_abort;
    hostdata->E_msg_in = Ent_msg_in;
    hostdata->E_other_transfer = Ent_other_transfer;
    hostdata->E_reject_message = Ent_reject_message;
    hostdata->E_respond_message = Ent_respond_message;
    hostdata->E_schedule = Ent_schedule;			
    hostdata->E_select = Ent_select;
    hostdata->E_select_msgout = Ent_select_msgout;
    hostdata->E_target_abort = Ent_target_abort;
#ifdef Ent_test_0
    hostdata->E_test_0 = Ent_test_0;
#endif
    hostdata->E_test_1 = Ent_test_1;
    hostdata->E_test_2 = Ent_test_2;
#ifdef Ent_test_3
    hostdata->E_test_3 = Ent_test_3;
#endif

    hostdata->dsa_cmdout = A_dsa_cmdout;
    hostdata->dsa_cmnd = A_dsa_cmnd;
    hostdata->dsa_datain = A_dsa_datain;
    hostdata->dsa_dataout = A_dsa_dataout;
    hostdata->dsa_end = A_dsa_end;			
    hostdata->dsa_msgin = A_dsa_msgin;
    hostdata->dsa_msgout = A_dsa_msgout;
    hostdata->dsa_msgout_other = A_dsa_msgout_other;
    hostdata->dsa_next = A_dsa_next;
    hostdata->dsa_select = A_dsa_select;
    hostdata->dsa_start = Ent_dsa_code_template - Ent_dsa_zero;
    hostdata->dsa_status = A_dsa_status;

    /* sanity check */
    if (A_dsa_fields_start != Ent_dsa_code_template_end - 
    	Ent_dsa_zero) 
    	printk("scsi%d : NCR dsa_fields start is %d not %d\n",
    	    host->host_no, A_dsa_fields_start, Ent_dsa_code_template_end - 
    	    Ent_dsa_zero);

    printk("scsi%d : NCR code relocated to 0x%p\n", host->host_no,
	hostdata->script);
}

/*
 * Function : static int NCR53c8xx_run_tests (struct Scsi_Host *host)
 *
 * Purpose : run various verification tests on the NCR chip, 
 *	including interrupt generation, and proper bus mastering
 * 	operation.
 * 
 * Inputs : host - a properly initialized Scsi_Host structure
 *
 * Preconditions : the NCR chip must be in a halted state.
 *
 * Returns : 0 if all tests were successful, -1 on error.
 * 
 */

static int NCR53c8xx_run_tests (struct Scsi_Host *host) {
    NCR53c7x0_local_declare();
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    unsigned long timeout;
    u32 start;
    int failed, i;
    unsigned long flags;
    NCR53c7x0_local_setup(host);

    /* The NCR chip _must_ be idle to run the test scripts */

    save_flags(flags);
    cli();
    if (!hostdata->idle) {
	printk ("scsi%d : chip not idle, aborting tests\n", host->host_no);
	restore_flags(flags);
	return -1;
    }

    /* 
     * Check for functional interrupts, this could work as an
     * autoprobe routine.
     */

    if (hostdata->issue_dsa_head) {
	printk ("scsi%d : hostdata->issue_dsa_head corrupt before test 1\n",
	    host->host_no);
	hostdata->issue_dsa_head = 0;
    }
	
    if (hostdata->options & OPTION_DEBUG_TEST1) {
	hostdata->idle = 0;
	hostdata->test_running = 1;
	hostdata->test_completed = -1;
	hostdata->test_dest = 0;
	hostdata->test_source = 0xdeadbeef;
	start = virt_to_bus(hostdata->script) + hostdata->E_test_1;
    	hostdata->state = STATE_RUNNING;
	printk ("scsi%d : test 1", host->host_no);
	NCR53c7x0_write32 (DSP_REG, start);
	mb();
	printk (" started\n");
	sti();

	timeout = jiffies + 5 * HZ / 10;	/* arbitrary */
	while ((hostdata->test_completed == -1) && jiffies < timeout)
		barrier();

	failed = 1;
	if (hostdata->test_completed == -1)
	    printk ("scsi%d : driver test 1 timed out%s\n",host->host_no ,
		(hostdata->test_dest == 0xdeadbeef) ? 
		    " due to lost interrupt.\n"
		    "         Please verify that the correct IRQ is being used for your board,\n"
		    "         and that the motherboard IRQ jumpering matches the PCI setup on\n"
		    "         PCI systems.\n"
		    "         If you are using a NCR53c810 board in a PCI system, you should\n" 
		    "         also verify that the board is jumpered to use PCI INTA, since\n"
		    "         most PCI motherboards lack support for INTB, INTC, and INTD.\n"
		    : "");
	else if (hostdata->test_completed != 1) 
	    printk ("scsi%d : test 1 bad interrupt value (%d)\n", host->host_no,
		hostdata->test_completed);
	else 
	    failed = (hostdata->test_dest != 0xdeadbeef);

	if (hostdata->test_dest != 0xdeadbeef) {
	    printk ("scsi%d : driver test 1 read 0x%x instead of 0xdeadbeef indicating a\n"
		    "        probable cache invalidation problem.  Please configure caching\n"
		    "        as write-through or disabled\n",
		host->host_no, hostdata->test_dest);
	}

	if (failed) {
	    printk ("scsi%d : DSP = 0x%x (script at 0x%p, start at 0x%x)\n",
		host->host_no, NCR53c7x0_read32(DSP_REG),
		hostdata->script, start);
	    printk ("scsi%d : DSPS = 0x%x\n", host->host_no,
		NCR53c7x0_read32(DSPS_REG));
	    restore_flags(flags);
	    return -1;
	}
    	hostdata->test_running = 0;
    }

    if (hostdata->issue_dsa_head) {
	printk ("scsi%d : hostdata->issue_dsa_head corrupt after test 1\n",
	    host->host_no);
	hostdata->issue_dsa_head = 0;
    }

    if (hostdata->options & OPTION_DEBUG_TEST2) {
	u32 dsa[48];
    	unsigned char identify = IDENTIFY(0, 0);
	unsigned char cmd[6];
	unsigned char data[36];
    	unsigned char status = 0xff;
    	unsigned char msg = 0xff;

    	cmd[0] = INQUIRY;
    	cmd[1] = cmd[2] = cmd[3] = cmd[5] = 0;
    	cmd[4] = sizeof(data); 

    	dsa[2] = 1;
    	dsa[3] = virt_to_bus(&identify);
    	dsa[4] = 6;
    	dsa[5] = virt_to_bus(&cmd);
    	dsa[6] = sizeof(data);
    	dsa[7] = virt_to_bus(&data);
    	dsa[8] = 1;
    	dsa[9] = virt_to_bus(&status);
    	dsa[10] = 1;
    	dsa[11] = virt_to_bus(&msg);

	for (i = 0; i < 3; ++i) {
	    cli();
	    if (!hostdata->idle) {
		printk ("scsi%d : chip not idle, aborting tests\n", host->host_no);
		restore_flags(flags);
		return -1;
	    }

	    /*	     SCNTL3         SDID	*/
	    dsa[0] = (0x33 << 24) | (i << 16)  ;
	    hostdata->idle = 0;
	    hostdata->test_running = 2;
	    hostdata->test_completed = -1;
	    start = virt_to_bus(hostdata->script) + hostdata->E_test_2;
	    hostdata->state = STATE_RUNNING;
	    NCR53c7x0_write32 (DSA_REG, virt_to_bus(dsa));
	    NCR53c7x0_write32 (DSP_REG, start);
	    mb();
	    sti();

	    timeout = jiffies + 5 * HZ;	/* arbitrary */
	    while ((hostdata->test_completed == -1) && jiffies < timeout)
	    	barrier();
	    NCR53c7x0_write32 (DSA_REG, 0);
	    mb();

	    if (hostdata->test_completed == 2) {
		data[35] = 0;
		printk ("scsi%d : test 2 INQUIRY to target %d, lun 0 : %s\n",
		    host->host_no, i, data + 8);
		printk ("scsi%d : status ", host->host_no);
		print_status (status);
		printk ("\nscsi%d : message ", host->host_no);
		print_msg (&msg);
		printk ("\n");
	    } else if (hostdata->test_completed == 3) {
		printk("scsi%d : test 2 no connection with target %d\n",
		    host->host_no, i);
		if (!hostdata->idle) {
		    printk("scsi%d : not idle\n", host->host_no);
		    restore_flags(flags);
		    return -1;
		}
	    } else if (hostdata->test_completed == -1) {
		printk ("scsi%d : test 2 timed out\n", host->host_no);
		restore_flags(flags);
		return -1;
	    } 
	    hostdata->test_running = 0;
	    if (hostdata->issue_dsa_head) {
		printk ("scsi%d : hostdata->issue_dsa_head corrupt after test 2 id %d\n",
		    host->host_no, i);
		hostdata->issue_dsa_head = 0;
	}
	}
    }

    restore_flags(flags);
    return 0;
}

/*
 * Function : static void NCR53c8xx_dsa_fixup (struct NCR53c7x0_cmd *cmd)
 *
 * Purpose : copy the NCR53c8xx dsa structure into cmd's dsa buffer,
 * 	performing all necessary relocation.
 *
 * Inputs : cmd, a NCR53c7x0_cmd structure with a dsa area large
 *	enough to hold the NCR53c8xx dsa.
 */

static void NCR53c8xx_dsa_fixup (struct NCR53c7x0_cmd *cmd) {
    Scsi_Cmnd *c = cmd->cmd;
    struct Scsi_Host *host = c->host;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
    	host->hostdata;
    int i;

    memcpy (cmd->dsa, hostdata->script + (hostdata->E_dsa_code_template / 4),
    	hostdata->E_dsa_code_template_end - hostdata->E_dsa_code_template);

    patch_abs_32 (cmd->dsa, Ent_dsa_code_template / sizeof(u32),
    	dsa_temp_jump_resume, virt_to_bus(cmd->dsa) + 
    	Ent_dsa_jump_resume - Ent_dsa_zero);
    patch_abs_rwri_data (cmd->dsa, Ent_dsa_code_template / sizeof(u32),
    	dsa_temp_lun, c->lun);
    patch_abs_32 (cmd->dsa, Ent_dsa_code_template / sizeof(u32),
    	dsa_temp_dsa_next, virt_to_bus(cmd->dsa) + A_dsa_next);
    patch_abs_32 (cmd->dsa, Ent_dsa_code_template / sizeof(u32),
    	dsa_temp_sync, hostdata->sync[c->target].select_indirect);
    patch_abs_rwri_data (cmd->dsa, Ent_dsa_code_template / sizeof(u32),
    	dsa_temp_target, c->target);
}

/*
 * Function : static void abnormal_finished (struct NCR53c7x0_cmd *cmd, int
 *	result)
 *
 * Purpose : mark SCSI command as finished, OR'ing the host portion 
 *	of the result word into the result field of the corresponding
 *	Scsi_Cmnd structure, and removing it from the internal queues.
 *
 * Inputs : cmd - command, result - entire result field
 *
 * Preconditions : the 	NCR chip should be in a halted state when 
 *	abnormal_finished is run, since it modifies structures which
 *	the NCR expects to have exclusive access to.
 */

static void abnormal_finished (struct NCR53c7x0_cmd *cmd, int result) {
    Scsi_Cmnd *c = cmd->cmd;
    struct Scsi_Host *host = c->host;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
    	host->hostdata;
    unsigned long flags;
    volatile u32 *prev, search;
    int i;

    save_flags(flags);
    cli();
    for (i = 0; i < 2; ++i) {
    	for (search = (i ? hostdata->issue_dsa_head :
    	    	hostdata->reconnect_dsa_head), prev = (i ? 
    	    	&hostdata->issue_dsa_head : &hostdata->reconnect_dsa_head);
    	     search && ((char*)bus_to_virt(search) + hostdata->dsa_start) != (char *) cmd->dsa;
    	     prev = (u32*) ((char*)bus_to_virt(search) + hostdata->dsa_next),
    	    	search = *prev);

    	if (search)
    	    *prev = *(u32*) ((char*)bus_to_virt(search) + hostdata->dsa_next);
    }

    if (cmd->prev)
    	cmd->prev->next = cmd->next;

    if (cmd->next)
    	cmd->next->prev = cmd->prev;

    if (hostdata->running_list == cmd)
    	hostdata->running_list = cmd->next;

    cmd->next = hostdata->free;
    hostdata->free = cmd;

    c->host_scribble = NULL;
    c->result = result;
    c->scsi_done(c);

    restore_flags(flags);
}

/* 
 * Function : static void intr_break (struct Scsi_Host *host,
 * 	struct NCR53c7x0_cmd *cmd)
 *
 * Purpose :  Handler for breakpoint interrupts from a SCSI script
 *
 * Inputs : host - pointer to this host adapter's structure,
 * 	cmd - pointer to the command (if any) dsa was pointing 
 * 	to.
 *
 */

static void intr_break (struct Scsi_Host *host, struct 
    NCR53c7x0_cmd *cmd) {
    NCR53c7x0_local_declare();
    struct NCR53c7x0_break *bp;
#if 0
    Scsi_Cmnd *c = cmd ? cmd->cmd : NULL;
#endif
    u32 *dsp;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;		
    unsigned long flags;
    NCR53c7x0_local_setup(host);

    /*
     * Find the break point corresponding to this address, and 
     * dump the appropriate debugging information to standard 
     * output.  
     */

    save_flags(flags);
    cli();
    dsp = (u32 *) bus_to_virt(NCR53c7x0_read32(DSP_REG));
    for (bp = hostdata->breakpoints; bp && bp->address != dsp; 
    	bp = bp->next);
    if (!bp) 
    	panic("scsi%d : break point interrupt from %p with no breakpoint!",
    	    host->host_no, dsp);

    /*
     * Configure the NCR chip for manual start mode, so that we can 
     * point the DSP register at the instruction that follows the 
     * INT int_debug_break instruction.
     */

    NCR53c7x0_write8 (hostdata->dmode, 
	NCR53c7x0_read8(hostdata->dmode)|DMODE_MAN);
    mb();

    /*
     * And update the DSP register, using the size of the old 
     * instruction in bytes.
     */

     restore_flags(flags);
}

/*
 * Function : static int asynchronous (struct Scsi_Host *host, int target)
 *
 * Purpose : reprogram between the selected SCSI Host adapter and target 
 *      (assumed to be currently connected) for asynchronous transfers.
 *
 * Inputs : host - SCSI host structure, target - numeric target ID.
 *
 * Preconditions : the NCR chip should be in one of the halted states
 */
    
static int asynchronous (struct Scsi_Host *host, int target) {
    NCR53c7x0_local_declare();
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    NCR53c7x0_local_setup(host);

    if ((hostdata->chip / 100) == 8) {
	hostdata->sync[target].select_indirect = (hostdata->saved_scntl3 << 24)
	    | (target << 16);
/* Fill in script here */
    } else if ((hostdata->chip != 700) && (hostdata->chip != 70066)) {
	hostdata->sync[target].select_indirect = (1 << (target & 7)) << 16;
    }

/* 
 * Halted implies connected, when resetting we shouldn't change the 
 * current parameters but must reset all targets to asynchronous.
 */

    if (hostdata->state == STATE_HALTED) {
	if ((hostdata->chip / 100) == 8) {
	    NCR53c7x0_write8 (SCNTL3_REG_800, hostdata->saved_scntl3);
	}
    /* Offset = 0, transfer period = divide SCLK by 4 */
	NCR53c7x0_write8 (SXFER_REG, 0);
	mb();
    }
    return 0;
}

/* 
 * XXX - do we want to go out of our way (ie, add extra code to selection
 * 	in the NCR53c710/NCR53c720 script) to reprogram the synchronous
 * 	conversion bits, or can we be content in just setting the 
 * 	sxfer bits?
 */

/* Table for NCR53c8xx synchronous values */
static const struct {
    int div;
    unsigned char scf;
    unsigned char tp;
} syncs[] = {
/*	div	scf	tp	div	scf	tp	div	scf	tp */
    {	40,	1,	0}, {	50,	1,	1}, {	60,	1,	2}, 
    {	70,	1,	3}, {	75,	2,	1}, {	80,	1,	4},
    {	90,	1,	5}, {	100,	1,	6}, {	105,	2,	3},
    {	110,	1,	7}, {	120,	2,	4}, {	135,	2,	5},
    {	140,	3,	3}, {	150,	2,	6}, {	160,	3,	4},
    {	165,	2,	7}, {	180,	3,	5}, {	200,	3,	6},
    {	210,	4,	3}, {	220,	3,	7}, {	240,	4,	4},
    {	270,	4,	5}, {	300,	4,	6}, {	330,	4,	7}
};

/*
 * Function : static void synchronous (struct Scsi_Host *host, int target, 
 *	char *msg)
 *
 * Purpose : reprogram transfers between the selected SCSI initiator and 
 *	target for synchronous SCSI transfers such that the synchronous 
 *	offset is less than that requested and period at least as long 
 *	as that requested.  Also modify *msg such that it contains 
 *	an appropriate response. 
 *
 * Inputs : host - NCR53c7,8xx SCSI host, target - number SCSI target id,
 *	msg - synchronous transfer request.
 */


static void synchronous (struct Scsi_Host *host, int target, char *msg) {
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    int desire, divisor, i, limit;
    u32 *script;
    unsigned char scntl3, sxfer;
   
/* Scale divisor by 10 to accommodate fractions */ 
    desire = 1000000000L / (msg[3] * 4);
    divisor = desire / (hostdata->scsi_clock / 10);

    if (msg[4] > 8)
	msg[4] = 8;

    printk("scsi%d : optimal synchronous divisor of %d.%01d\n", host->host_no,
	divisor / 10, divisor % 10);

    limit = (sizeof(syncs) / sizeof(syncs[0])) - 1;
    for (i = 0; (i < limit) && (divisor < syncs[i + 1].div); ++i);

    printk("scsi%d : selected synchronous divisor of %d.%01d\n", host->host_no,
	syncs[i].div / 10, syncs[i].div % 10);

    msg[3] = (1000000000 / divisor / 10 / 4);

    scntl3 = (hostdata->chip / 100 == 8) ? ((hostdata->saved_scntl3 & 
	~SCNTL3_800_SCF_MASK) | (syncs[i].scf << SCNTL3_800_SCF_SHIFT)) : 0;
    sxfer = (msg[4] << SXFER_MO_SHIFT) | ((syncs[i].tp) << SXFER_TP_SHIFT);

    if ((hostdata->chip != 700) && (hostdata->chip != 70066)) {
	hostdata->sync[target].select_indirect = (scntl3 << 24) | (target << 16) | 
		(sxfer << 8);

	script = (u32*) hostdata->sync[target].script;

	/* XXX - add NCR53c7x0 code to reprogram SCF bits if we want to */
	if ((hostdata->chip / 100) == 8) {
	    script[0] = ((DCMD_TYPE_RWRI | DCMD_RWRI_OPC_MODIFY |
		DCMD_RWRI_OP_MOVE) << 24) |
		(SCNTL3_REG_800 << 16) | (scntl3 << 8);
	    script[1] = 0;
	    script += 2;
	}

	script[0] = ((DCMD_TYPE_RWRI | DCMD_RWRI_OPC_MODIFY |
	    DCMD_RWRI_OP_MOVE) << 24) |
		(SXFER_REG << 16) | (sxfer << 8);
	script[1] = 0;
	script += 2;

	script[0] = ((DCMD_TYPE_TCI|DCMD_TCI_OP_RETURN) << 24) | DBC_TCI_TRUE;
	script[1] = 0;
	script += 2;
    }
}

/* 
 * Function : static int NCR53c8x0_dstat_sir_intr (struct Scsi_Host *host,
 * 	struct NCR53c7x0_cmd *cmd)
 *
 * Purpose :  Handler for INT generated instructions for the 
 * 	NCR53c810/820 SCSI SCRIPT
 *
 * Inputs : host - pointer to this host adapter's structure,
 * 	cmd - pointer to the command (if any) dsa was pointing 
 * 	to.
 *
 */

static int NCR53c8x0_dstat_sir_intr (struct Scsi_Host *host, struct 
    NCR53c7x0_cmd *cmd) {
    NCR53c7x0_local_declare();
    Scsi_Cmnd *c = cmd ? cmd->cmd : NULL;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;		
    u32 dsps,*dsp;	/* Argument of the INT instruction */
    NCR53c7x0_local_setup(host);
    dsps = NCR53c7x0_read32(DSPS_REG);
    dsp = bus_to_virt(NCR53c7x0_read32(DSP_REG));

    if (hostdata->options & OPTION_DEBUG_INTR) 
	printk ("scsi%d : DSPS = 0x%x\n", host->host_no, dsps);

    switch (dsps) {
    case A_int_msg_1:
	printk ("scsi%d : message", host->host_no);
	if (cmd) 
	    printk (" from target %d lun %d", c->target, c->lun);
	print_msg ((unsigned char *) hostdata->msg_buf);
	printk("\n");
	switch (hostdata->msg_buf[0]) {
	/* 
	 * Unless we've initiated synchronous negotiation, I don't
	 * think that this should happen.
	 */
	case MESSAGE_REJECT:
	    hostdata->dsp = hostdata->script + hostdata->E_accept_message /
		sizeof(u32);
	    hostdata->dsp_changed = 1;
	    break;
	case INITIATE_RECOVERY:
	    printk ("scsi%d : extended contingent allegiance not supported yet, rejecting\n",
		host->host_no);
	    hostdata->dsp = hostdata->script + hostdata->E_reject_message /
		sizeof(u32);
	    hostdata->dsp_changed = 1;
	}
	return SPECIFIC_INT_NOTHING;
    case A_int_msg_sdtr:
	if (cmd) {
	    printk ("scsi%d : target %d %s synchronous transfer period %dns, offset%d\n",
		host->host_no, c->target, (cmd->flags & CMD_FLAG_SDTR) ? "accepting" :
		"requesting", hostdata->msg_buf[3] * 4, hostdata->msg_buf[4]);
	/* 
	 * Initiator initiated, won't happen unless synchronous 
	 * 	transfers are enabled.  If we get a SDTR message in
	 * 	response to our SDTR, we should program our parameters
	 * 	such that 
	 *		offset <= requested offset
	 *		period >= requested period		 	
   	 */
	    if (cmd->flags & CMD_FLAG_SDTR) {
		cmd->flags &= ~CMD_FLAG_SDTR; 
		synchronous (host, c->target, (unsigned char *)
		    hostdata->msg_buf);
		hostdata->dsp = hostdata->script + hostdata->E_accept_message /
		    sizeof(u32);
		hostdata->dsp_changed = 1;
		return SPECIFIC_INT_NOTHING;
	    } else {
		if (hostdata->options & OPTION_SYNCHRONOUS)  {
		    cmd->flags |= CMD_FLAG_DID_SDTR;
		    synchronous (host, c->target, (unsigned char *)
			hostdata->msg_buf);
		} else {
		    hostdata->msg_buf[4] = 0;		/* 0 offset = async */
		}

		patch_dsa_32 (cmd->dsa, dsa_msgout_other, 0, 5);
		patch_dsa_32 (cmd->dsa, dsa_msgout_other, 1, 
		    virt_to_bus((void*)hostdata->msg_buf));
		hostdata->dsp = hostdata->script + 
		hostdata->E_respond_message / sizeof(u32);
		hostdata->dsp_changed = 1;
	    }

	    if (hostdata->msg_buf[4]) {
		int Hz = 1000000000 / (hostdata->msg_buf[3] * 4);
		printk ("scsi%d : setting target %d to %d.%02dMhz %s SCSI%s\n"
			"         period = %dns, max offset = %d\n",
			host->host_no, c->target, Hz / 1000000, Hz % 1000000,
			((hostdata->msg_buf[3] < 200) ? "FAST " : 
			"synchronous") ,
			((hostdata->msg_buf[3] < 200) ? "-II" : ""),
			(int) hostdata->msg_buf[3] * 4, (int) 
			hostdata->msg_buf[4]);
	    } else {
		printk ("scsi%d : setting target %d to asynchronous SCSI\n",
		    host->host_no, c->target);
	    }
	    return SPECIFIC_INT_NOTHING;
	}
	/* Fall through to abort */
    case A_int_msg_wdtr:
	hostdata->dsp = hostdata->script + hostdata->E_reject_message /
	    sizeof(u32);
	hostdata->dsp_changed = 1;
	return SPECIFIC_INT_NOTHING;
    case A_int_err_unexpected_phase:
	if (hostdata->options & OPTION_DEBUG_INTR) 
	    printk ("scsi%d : unexpected phase\n", host->host_no);
	return SPECIFIC_INT_ABORT;
    case A_int_err_selected:
	printk ("scsi%d : selected by target %d\n", host->host_no,
	    (int) NCR53c7x0_read8(SSID_REG_800) &7);
	hostdata->dsp = hostdata->script + hostdata->E_target_abort / 
    	    sizeof(u32);
	hostdata->dsp_changed = 1;
	return SPECIFIC_INT_NOTHING;
    case A_int_err_unexpected_reselect:
	printk ("scsi%d : unexpected reselect by target %d\n", host->host_no,
	    (int) NCR53c7x0_read8(SSID_REG_800));
	hostdata->dsp = hostdata->script + hostdata->E_initiator_abort /
    	    sizeof(u32);
	hostdata->dsp_changed = 1;
	return SPECIFIC_INT_NOTHING;
/*
 * Since contingent allegiance conditions are cleared by the next 
 * command issued to a target, we must issue a REQUEST SENSE 
 * command after receiving a CHECK CONDITION status, before
 * another command is issued.
 * 
 * Since this NCR53c7x0_cmd will be freed after use, we don't 
 * care if we step on the various fields, so modify a few things.
 */
    case A_int_err_check_condition: 
#if 0
	if (hostdata->options & OPTION_DEBUG_INTR) 
#endif
	    printk ("scsi%d : CHECK CONDITION\n", host->host_no);
	if (!c) {
	    printk("scsi%d : CHECK CONDITION with no SCSI command\n",
		host->host_no);
	    return SPECIFIC_INT_PANIC;
	}

/*
 * When a contingent allegiance condition is created, the target 
 * reverts to asynchronous transfers.
 */

	asynchronous (host, c->target);

	/* 
	 * Use normal one-byte selection message, with no attempts to 
    	 * reestablish synchronous or wide messages since this may
    	 * be the crux of our problem.
	 *
	 * XXX - once SCSI-II tagged queuing is implemented, we'll
	 * 	have to set this up so that the rest of the DSA
	 *	agrees with this being an untagged queue'd command.
	 */

    	patch_dsa_32 (cmd->dsa, dsa_msgout, 0, 1);

    	/* 
    	 * Modify the table indirect for COMMAND OUT phase, since 
    	 * Request Sense is a six byte command.
    	 */

    	patch_dsa_32 (cmd->dsa, dsa_cmdout, 0, 6);

	c->cmnd[0] = REQUEST_SENSE;
	c->cmnd[1] &= 0xe0;	/* Zero all but LUN */
	c->cmnd[2] = 0;
	c->cmnd[3] = 0;
	c->cmnd[4] = sizeof(c->sense_buffer);
	c->cmnd[5] = 0; 

	/*
	 * Disable dataout phase, and program datain to transfer to the 
	 * sense buffer, and add a jump to other_transfer after the 
    	 * command so overflow/underrun conditions are detected.
	 */

    	patch_dsa_32 (cmd->dsa, dsa_dataout, 0, hostdata->E_other_transfer);
    	patch_dsa_32 (cmd->dsa, dsa_datain, 0, virt_to_bus(cmd->data_transfer_start));
    	cmd->data_transfer_start[0] = (((DCMD_TYPE_BMI | DCMD_BMI_OP_MOVE_I | 
    	    DCMD_BMI_IO)) << 24) | sizeof(c->sense_buffer);
    	cmd->data_transfer_start[1] = virt_to_bus(c->sense_buffer);

	cmd->data_transfer_start[2] = ((DCMD_TYPE_TCI | DCMD_TCI_OP_JUMP) 
    	    << 24) | DBC_TCI_TRUE;
	cmd->data_transfer_start[3] = hostdata->E_other_transfer;

    	/*
    	 * Currently, this command is flagged as completed, ie 
    	 * it has valid status and message data.  Reflag it as
    	 * incomplete.  Q - need to do something so that original
	 * status, etc are used.
    	 */

	cmd->cmd->result = 0xffff;		

	/* 
	 * Restart command as a REQUEST SENSE.
	 */
	hostdata->dsp = hostdata->script + hostdata->E_select /
	    sizeof(u32);
	hostdata->dsp_changed = 1;
	return SPECIFIC_INT_NOTHING;
    case A_int_debug_break:
	return SPECIFIC_INT_BREAK;
    case A_int_norm_aborted:
	hostdata->dsp = hostdata->script + hostdata->E_schedule / 
		sizeof(u32);
	hostdata->dsp_changed = 1;
	if (cmd)
	    abnormal_finished (cmd, DID_ERROR << 16);
	return SPECIFIC_INT_NOTHING;
    case A_int_test_1:
    case A_int_test_2:
	hostdata->idle = 1;
	hostdata->test_completed = (dsps - A_int_test_1) / 0x00010000 + 1;
	if (hostdata->options & OPTION_DEBUG_INTR)
	    printk("scsi%d : test %d complete\n", host->host_no,
		hostdata->test_completed);
	return SPECIFIC_INT_NOTHING;
#ifdef A_int_debug_scheduled
    case A_int_debug_scheduled:
	if (hostdata->options & (OPTION_DEBUG_SCRIPT|OPTION_DEBUG_INTR)) {
	    printk("scsi%d : new I/O 0x%x scheduled\n", host->host_no,
	    	NCR53c7x0_read32(DSA_REG));
	}
	return SPECIFIC_INT_RESTART;
#endif
#ifdef A_int_debug_idle
    case A_int_debug_idle:
	if (hostdata->options & (OPTION_DEBUG_SCRIPT|OPTION_DEBUG_INTR)) {
	    printk("scsi%d : idle\n", host->host_no);
	}
	return SPECIFIC_INT_RESTART;
#endif
#ifdef A_int_debug_cmd
    case A_int_debug_cmd:
	if (hostdata->options & (OPTION_DEBUG_SCRIPT|OPTION_DEBUG_INTR)) {
	    printk("scsi%d : command sent\n");
	}
    return SPECIFIC_INT_RESTART;
#endif
#ifdef A_int_debug_dsa_loaded
    case A_int_debug_dsa_loaded:
	if (hostdata->options & (OPTION_DEBUG_SCRIPT|OPTION_DEBUG_INTR)) {
	    printk("scsi%d : DSA loaded with 0x%x\n", host->host_no,
		NCR53c7x0_read32(DSA_REG));
	}
	return SPECIFIC_INT_RESTART; 
#endif
#ifdef A_int_debug_reselected
    case A_int_debug_reselected:
	if (hostdata->options & (OPTION_DEBUG_SCRIPT|OPTION_DEBUG_INTR)) {
	    printk("scsi%d : reselected by target %d lun %d\n",
		host->host_no, (int) NCR53c7x0_read8(SSID_REG_800), 
		(int) hostdata->reselected_identify & 7);
	}
    return SPECIFIC_INT_RESTART;
#endif
#ifdef A_int_debug_head
    case A_int_debug_head:
	if (hostdata->options & (OPTION_DEBUG_SCRIPT|OPTION_DEBUG_INTR)) {
	    printk("scsi%d : issue_dsa_head now 0x%x\n",
		host->host_no, hostdata->issue_dsa_head);
	}
    return SPECIFIC_INT_RESTART;
#endif
    default:
	if ((dsps & 0xff000000) == 0x03000000) {
	     printk ("scsi%d : misc debug interrupt 0x%x\n",
		host->host_no, dsps);
	    return SPECIFIC_INT_RESTART;
	}

	printk ("scsi%d : unknown user interrupt 0x%x\n", 
	    host->host_no, (unsigned) dsps);
	return SPECIFIC_INT_PANIC;
    }
}

/* 
 * XXX - the stock NCR assembler won't output the scriptu.h file,
 * which undefine's all #define'd CPP symbols from the script.h
 * file, which will create problems if you use multiple scripts
 * with the same  symbol names.
 *
 * If you insist on using NCR's assembler, you could generate
 * scriptu.h from script.h using something like 
 *
 * grep #define script.h | \
 * sed 's/#define[ 	][ 	]*\([_a-zA-Z][_a-zA-Z0-9]*\).*$/#undefine \1/' \
 * > scriptu.h
 */

#include "53c8xx_u.h"

/* XXX - add alternate script handling code here */


#ifdef NCR_DEBUG
/*
 * Debugging without a debugger is no fun. So, I've provided 
 * a debugging interface in the NCR53c7x0 driver.  To avoid
 * kernel cruft, there's just enough here to act as an interface
 * to a user level debugger (aka, GDB).
 *
 *
 * The following restrictions apply to debugger commands : 
 * 1.  The command must be terminated by a newline.
 * 2.  Command length must be less than 80 bytes including the 
 * 	newline.
 * 3.  The entire command must be written with one system call.
 */

static const char debugger_help = 
"bc <addr> 			- clear breakpoint\n"
"bl				- list breakpoints\n"
"bs <addr>			- set breakpoint\n"
"g				- start\n" 				
"h				- halt\n"
"?				- this message\n"
"i				- info\n"
"mp <addr> <size> 		- print memory\n"
"ms <addr> <size> <value>	- store memory\n"
"rp <num> <size>		- print register\n"
"rs <num> <size> <value> 	- store register\n"
"s				- single step\n"
"tb				- begin trace \n"
"te				- end trace\n";

/*
 * Whenever we change a break point, we should probably 
 * set the NCR up so that it is in a single step mode.
 */

static int debugger_fn_bc (struct Scsi_Host *host, struct debugger_token *token,
    u32 args[]) {
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	instance->hostdata;
    struct NCR53c7x0_break *bp, **prev;
    unsigned long flags;
    save_flags(flags);
    cli();
    for (bp = (struct NCR53c7x0_break *) instance->breakpoints,
	    prev = (struct NCR53c7x0_break **) &instance->breakpoints;
	    bp; prev = (struct NCR53c7x0_break **) &(bp->next),
	    bp = (struct NCR53c7x0_break *) bp->next);

    if (!bp) {
	restore_flags(flags);
	return -EIO;
    }

    /* 
     * XXX - we need to insure that the processor is halted 
     * here in order to prevent a race condition.
     */
    
    memcpy ((void *) bp->addr, (void *) bp->old, sizeof(bp->old));
    if (prev)
	*prev = bp->next;

    restore_flags(flags);
    return 0;
}


static int debugger_fn_bl (struct Scsi_Host *host, struct debugger_token *token,
    u32 args[]) {
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    struct NCR53c7x0_break *bp;
    char buf[80];
    size_t len;
    unsigned long flags;
    /* 
     * XXX - we need to insure that the processor is halted 
     * here in order to prevent a race condition.  So, if the 
     * processor isn't halted, print an error message and continue.
     */

    sprintf (buf, "scsi%d : bp : warning : processor not halted\b",
	host->host_no);
    debugger_kernel_write (host, buf, strlen(buf));

    save_flags(flags);
    cli();
    for (bp = (struct NCR53c7x0_break *) host->breakpoints;
	    bp; bp = (struct NCR53c7x0_break *) bp->next); {
	    sprintf (buf, "scsi%d : bp : success : at %08x, replaces %08x %08x",
		bp->addr, bp->old[0], bp->old[1]);
	    len = strlen(buf);
	    if ((bp->old[0] & (DCMD_TYPE_MASK << 24)) ==
		(DCMD_TYPE_MMI << 24)) {
		sprintf(buf + len, "%08x\n", * (u32 *) bp->addr);
	    } else {
		sprintf(buf + len, "\n");
	    }
	    len = strlen(buf);
	    debugger_kernel_write (host, buf, len);
    }
    restore_flags(flags);
    return 0;
}

static int debugger_fn_bs (struct Scsi_Host *host, struct debugger_token *token,
    u32 args[]) {
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    struct NCR53c7x0_break *bp;
    char buf[80];
    size_t len;
    unsigned long flags;

    save_flags(flags);
    cli();

    if (hostdata->state != STATE_HALTED) {
	sprintf (buf, "scsi%d : bs : failure : NCR not halted\n", host->host_no);
	debugger_kernel_write (host, buf, strlen(buf));
	restore_flags(flags);
	return -1;
    }

    if (!(bp = kmalloc (sizeof (struct NCR53c7x0_break)))) {
	printk ("scsi%d : kmalloc(%d) of breakpoint structure failed, try again\n",
	    host->host_no, sizeof(struct NCR53c7x0_break));
	restore_flags(flags);
	return -1;
    }

    bp->address = (u32 *) args[0];
    memcpy ((void *) bp->old_instruction, (void *) bp->address, 8);
    bp->old_size = (((bp->old_instruction[0] >> 24) & DCMD_TYPE_MASK) ==
	DCMD_TYPE_MMI ? 3 : 2);
    bp->next = hostdata->breakpoints;
    hostdata->breakpoints = bp->next;
    memcpy ((void *) bp->address, (void *) hostdata->E_debug_break, 8);
    
    restore_flags(flags);
    return 0;
}

#define TOKEN(name,nargs) {#name, nargs, debugger_fn_##name}
static const struct debugger_token {
    char *name;
    int numargs;
    int (*fn)(struct debugger_token *token, u32 args[]);
} debugger_tokens[] = {
    TOKEN(bc,1), TOKEN(bl,0), TOKEN(bs,1), TOKEN(g,0), TOKEN(halt,0),
    {DT_help, "?", 0} , TOKEN(h,0), TOKEN(i,0), TOKEN(mp,2), 
    TOKEN(ms,3), TOKEN(rp,2), TOKEN(rs,2), TOKEN(s,0), TOKEN(tb,0), TOKEN(te,0)
};

#define NDT sizeof(debugger_tokens / sizeof(struct debugger_token))

static struct Scsi_Host * inode_to_host (struct inode *inode) {$
    int dev;
    struct Scsi_Host *tmp;
    for (dev = MINOR(inode->rdev), host = first_host;
	(host->hostt == the_template); --dev, host = host->next)
	if (!dev) return host;
    return NULL;
}


static debugger_user_write (struct inode *inode,struct file *filp,
    char *buf,int count) {
    struct Scsi_Host *host;			/* This SCSI host */
    struct NCR53c7x0_hostadata *hostdata;	
    char input_buf[80], 			/* Kernel space copy of buf */
	*ptr;					/* Pointer to argument list */
    u32 args[3];				/* Arguments */
    int i, j, error, len;

    if (!(host = inode_to_host(inode)))
	return -ENXIO;

    hostdata = (struct NCR53c7x0_hostdata *) host->hostdata;

    if (error = verify_area(VERIFY_READ,buf,count))
	return error;

    if (count > 80) 
	return -EIO;

    memcpy_from_fs(input_buf, buf, count);

    if (input_buf[count - 1] != '\n')
	return -EIO;

    input_buf[count - 1]=0;

    for (i = 0; i < NDT; ++i) {
	len = strlen (debugger_tokens[i].name);
	if (!strncmp(input_buf, debugger_tokens[i].name, len)) 
	    break;
    };

    if (i == NDT) 
	return -EIO;

    for (ptr = input_buf + len, j = 0; j < debugger_tokens[i].nargs && *ptr;) {
	if (*ptr == ' ' || *ptr == '\t') {
	    ++ptr; 
	} else if (isdigit(*ptr)) {
	    args[j++] = simple_strtoul (ptr, &ptr, 0);
	} else {
	    return -EIO;
	} 
    }

    if (j != debugger_tokens[i].nargs)
	return -EIO;

    return count;
} 

static debugger_user_read (struct inode *inode,struct file *filp,
    char *buf,int count) {
    struct Scsi_Host *instance;
    
}

static debugger_kernel_write (struct Scsi_Host *host, char *buf, size_t
    buflen) {
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    int copy, left;
    unsigned long flags;
    
    save_flags(flags);
    cli();
    while (buflen) {
	left = (hostdata->debug_buf + hostdata->debug_size - 1) -
	    hostdata->debug_write;
	copy = (buflen <= left) ? buflen : left;
	memcpy (hostdata->debug_write, buf, copy);
	buf += copy;
	buflen -= copy;
	hostdata->debug_count += copy;
	if ((hostdata->debug_write += copy) == 
	    (hostdata->debug_buf + hostdata->debug_size))
	    hosdata->debug_write = hostdata->debug_buf;
    }
    restore_flags(flags);
}

#endif /* def NCRDEBUG */

/* 
 * Function : static void NCR538xx_soft_reset (struct Scsi_Host *host)
 *
 * Purpose :  perform a soft reset of the NCR53c8xx chip
 *
 * Inputs : host - pointer to this host adapter's structure
 *
 * Preconditions : NCR53c7x0_init must have been called for this 
 *      host.
 * 
 */

static void 
NCR53c8x0_soft_reset (struct Scsi_Host *host) {
    NCR53c7x0_local_declare();
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    NCR53c7x0_local_setup(host);


    /*
     * Do a soft reset of the chip so that everything is 
     * reinitialized to the power-on state.
     *
     * Basically follow the procedure outlined in the NCR53c700
     * data manual under Chapter Six, How to Use, Steps Necessary to
     * Start SCRIPTS, with the exception of actually starting the 
     * script and setting up the synchronous transfer gunk.
     */

    NCR53c7x0_write8(ISTAT_REG_800, ISTAT_10_SRST);
    mb();
    NCR53c7x0_write8(ISTAT_REG_800, 0);
    mb();
    NCR53c7x0_write8(hostdata->dmode, hostdata->saved_dmode & ~DMODE_MAN);


    /* 
     * Respond to reselection by targets and use our _initiator_ SCSI ID 
     * for arbitration. If notyet, also respond to SCSI selection.
     *
     * XXX - Note : we must reprogram this when reselecting as 
     *	a target.
     */

#ifdef notyet
    NCR53c7x0_write8(SCID_REG, (host->this_id & 7)|SCID_800_RRE|SCID_800_SRE);
#else
    NCR53c7x0_write8(SCID_REG, (host->this_id & 7)|SCID_800_RRE);
#endif
    NCR53c7x0_write8(RESPID_REG_800, hostdata->this_id_mask);

    /*
     * Use a maximum (1.6) second handshake to handshake timeout,
     * and SCSI recommended .5s selection timeout.
     */

    /*
     * The new gcc won't recognize preprocessing directives
     * within macro args.
     */
#if 0
    NCR53c7x0_write8(STIME0_REG_800, 
    	((14 << STIME0_800_SEL_SHIFT) & STIME0_800_SEL_MASK) 
/* Disable HTH interrupt */
	| ((15 << STIME0_800_HTH_SHIFT) & STIME0_800_HTH_MASK));
#else
    NCR53c7x0_write8(STIME0_REG_800, 
    	((14 << STIME0_800_SEL_SHIFT) & STIME0_800_SEL_MASK));
#endif



    /*
     * Enable all interrupts, except parity which we only want when
     * the user requests it.
     */

    NCR53c7x0_write8(DIEN_REG, DIEN_800_MDPE | DIEN_800_BF |
		DIEN_ABRT | DIEN_SSI | DIEN_SIR | DIEN_800_IID);

    
    NCR53c7x0_write8(SIEN0_REG_800, ((hostdata->options & OPTION_PARITY) ?
	    SIEN_PAR : 0) | SIEN_RST | SIEN_UDC | SIEN_SGE | SIEN_MA);
    NCR53c7x0_write8(SIEN1_REG_800, SIEN1_800_STO | SIEN1_800_HTH);

    /* 
     * Use saved clock frequency divisor and scripts loaded in 16 bit
     * mode flags from the saved dcntl.
     */

    NCR53c7x0_write8(DCNTL_REG, hostdata->saved_dcntl);
    NCR53c7x0_write8(CTEST4_REG_800, hostdata->saved_ctest4);

    /* Enable active negation */
    NCR53c7x0_write8(STEST3_REG_800, STEST3_800_TE);

    mb();
}

/*
 * Function static struct NCR53c7x0_cmd *create_cmd (Scsi_Cmnd *cmd) 
 *
 * Purpose : If we have not already allocated enough NCR53c7x0_cmd
 *	structures to satisfy any allowable number of simultaneous 
 *	commands for this host; do so (using either scsi_malloc()
 *	or kmalloc() depending on configuration), and add them to the 
 *	hostdata free list.  Take the first structure off the free list, 
 *	initialize it based on the Scsi_Cmnd structure passed in, 
 *	including dsa and Linux field initialization, and dsa code relocation.
 *
 * Inputs : cmd - SCSI command
 *
 * Returns : NCR53c7x0_cmd structure corresponding to cmd,
 *	NULL on failure.
 */

static struct NCR53c7x0_cmd *
create_cmd (Scsi_Cmnd *cmd) {
    NCR53c7x0_local_declare();
    struct Scsi_Host *host = cmd->host;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;	
    struct NCR53c7x0_cmd *tmp = NULL; 	/* NCR53c7x0_cmd structure for this command */
    int datain,  		/* Number of instructions per phase */
	dataout;
    int data_transfer_instructions, /* Count of dynamic instructions */
    	i;			/* Counter */
    u32 *cmd_datain,		/* Address of datain/dataout code */
	*cmd_dataout;		/* Incremented as we assemble */
#ifdef notyet
    void *real;			/* Real address */
    int size;			/* Size of *tmp */
    int alignment;		/* Alignment adjustment (0 - sizeof(long)-1) */
#endif
    unsigned long flags;
    NCR53c7x0_local_setup(cmd->host);

/* FIXME : when we start doing multiple simultaneous commands per LUN, 
   we will need to either
    	- Do an attach_slave() and detach_slave() the right way (allocate
    	  memory in attach_slave() as we do in scsi_register).
    	- Make sure this code works
    with the former being cleaner.  At the same time, we can also go with
    a per-device host_scribble, and introduce a NCR53c7x0_device structure
    to replace the messy fixed length arrays we're starting to use. */

#ifdef notyet

    if (hostdata->num_commands < host->can_queue &&
    	!in_scan_scsis && 
    	!(hostdata->cmd_allocated[cmd->target] & (1 << cmd->lun))) {
    	for (i = host->hostt->cmd_per_lun - 1; i >= 0  --i) {
#ifdef SCSI_MALLOC
    /* scsi_malloc must allocate with a 512 byte granularity, but always
       returns buffers which are aligned on a 512 boundary */
	    size = (hostdata->max_cmd_size + 511) / 512 * 512;
	    tmp = (struct NCR53c7x0_cmd *) scsi_malloc (size);
	    if (!tmp)
		break;
	    tmp->real = (void *) tmp; 
#else
    /* kmalloc() can allocate any size, but historically has returned 
       unaligned addresses, so we need to allow for alignment */
	    size = hostdata->max_cmd_size + sizeof(void*);
	    real = kmalloc (size, GFP_ATOMIC);
	    alignment = sizeof(void*) - (((unsigned) real) & (sizeof(void*)-1));
	    tmp = (struct NCR53c7x0_cmd *) (((char *) real) + alignment);
	    if (!tmp)
		break;
	    tmp->real = real;
#endif /* def SCSI_MALLOC */
    	    tmp->size = size;			
	    /* Insert all but last into list */
	    if (i > 0) {
		tmp->next = hostdata->free;
		hostdata->free = tmp;
	    }
	}
    }
#endif /* def notyet */
    if (!tmp) {
    	save_flags(flags);
    	cli();
    	tmp = (struct NCR53c7x0_cmd *) hostdata->free;
	if (tmp) {
    	    hostdata->free = tmp->next;
	    restore_flags(flags);
    	} else {
    	    restore_flags(flags);
    	    return NULL;
    	}
    }

    /*
     * Decide whether we need to generate commands for DATA IN,
     * DATA OUT, neither, or both based on the SCSI command 
     */

    switch (cmd->cmnd[0]) {
    /* These commands do DATA IN */
    case INQUIRY:
    case MODE_SENSE:
    case READ_6:
    case READ_10:
    case READ_CAPACITY:
    case REQUEST_SENSE:
	datain = 2 * (cmd->use_sg ? cmd->use_sg : 1) + 3;
    	dataout = 0;
	break;
    /* These commands do DATA OUT */
    case MODE_SELECT: 
    case WRITE_6:
    case WRITE_10:
#if 0
	printk("scsi%d : command is ", host->host_no);
	print_command(cmd->cmnd);
#endif
#if 0
	printk ("scsi%d : %d scatter/gather segments\n", host->host_no,
	    cmd->use_sg);
#endif
    	datain = 0;
	dataout = 2 * (cmd->use_sg ? cmd->use_sg : 1) + 3;
#if 0
	hostdata->options |= OPTION_DEBUG_INTR;
#endif
	break;
    /* 
     * These commands do no data transfer, we should force an
     * interrupt if a data phase is attempted on them.
     */
    case START_STOP:
    case TEST_UNIT_READY:
    	datain = dataout = 0;
	break;
    /*
     * We don't know about these commands, so generate code to handle
     * both DATA IN and DATA OUT phases.
     */
    default:
	datain = dataout = 2 * (cmd->use_sg ? cmd->use_sg : 1) + 3;
    }

    /* 
     * For each data phase implemented, we need a JUMP instruction
     * to return control to other_transfer.  We also need a MOVE
     * and a CALL instruction for each scatter/gather segment.
     */

    data_transfer_instructions = datain + dataout;

    /*
     * When we perform a request sense, we overwrite various things,
     * including the data transfer code.  Make sure we have enough
     * space to do that.
     */

    if (data_transfer_instructions < 2)
    	data_transfer_instructions = 2;

    /*
     * Initialize Linux specific fields.
     */

    tmp->cmd = cmd;
    tmp->next = NULL;
    tmp->prev = NULL;

    /* 
     * Calculate addresses of dynamic code to fill in DSA
     */

    tmp->data_transfer_start = tmp->dsa + (hostdata->dsa_end - 
    	hostdata->dsa_start) / sizeof(u32);
    tmp->data_transfer_end = tmp->data_transfer_start + 
    	2 * data_transfer_instructions;

    cmd_datain = datain ? tmp->data_transfer_start : NULL;
    cmd_dataout = dataout ? (datain ? cmd_datain + 2 * datain : tmp->
    	data_transfer_start) : NULL;

    /*
     * Fill in the NCR53c7x0_cmd structure as follows
     * dsa, with fixed up DSA code
     * datain code
     * dataout code
     */

    /* Copy template code into dsa and perform all necessary fixups */
    if (hostdata->dsa_fixup)
    	hostdata->dsa_fixup(tmp);

    patch_dsa_32(tmp->dsa, dsa_next, 0, 0);
    patch_dsa_32(tmp->dsa, dsa_cmnd, 0, virt_to_bus(cmd));
    patch_dsa_32(tmp->dsa, dsa_select, 0, hostdata->sync[cmd->target].
    	select_indirect);
    /*
     * XXX - we need to figure this size based on whether
     * or not we'll be using any additional messages.
     */
    patch_dsa_32(tmp->dsa, dsa_msgout, 0, 1);
#if 0
    tmp->select[0] = IDENTIFY (1, cmd->lun);
#else
    tmp->select[0] = IDENTIFY (0, cmd->lun);
#endif
    patch_dsa_32(tmp->dsa, dsa_msgout, 1, virt_to_bus(tmp->select));
    patch_dsa_32(tmp->dsa, dsa_cmdout, 0, cmd->cmd_len);
    patch_dsa_32(tmp->dsa, dsa_cmdout, 1, virt_to_bus(cmd->cmnd));
    patch_dsa_32(tmp->dsa, dsa_dataout, 0, cmd_dataout ?
    	virt_to_bus(cmd_dataout) : virt_to_bus(hostdata->script) + hostdata->E_other_transfer);
    patch_dsa_32(tmp->dsa, dsa_datain, 0, cmd_datain ?
    	virt_to_bus(cmd_datain) : virt_to_bus(hostdata->script) + hostdata->E_other_transfer);
    /* 
     * XXX - need to make endian aware, should use separate variables
     * for both status and message bytes.
     */
    patch_dsa_32(tmp->dsa, dsa_msgin, 0, 1);
    patch_dsa_32(tmp->dsa, dsa_msgin, 1, virt_to_bus(&cmd->result) + 1);
    patch_dsa_32(tmp->dsa, dsa_status, 0, 1);
    patch_dsa_32(tmp->dsa, dsa_status, 1, virt_to_bus(&cmd->result));
    patch_dsa_32(tmp->dsa, dsa_msgout_other, 0, 1);
    patch_dsa_32(tmp->dsa, dsa_msgout_other, 1, 
    	virt_to_bus(&hostdata->NCR53c7xx_msg_nop));

    
    /*
     * Generate code for zero or more of the DATA IN, DATA OUT phases 
     * in the format 
     *
     * MOVE first buffer length, first buffer address, WHEN phase
     * CALL msgin, WHEN MSG_IN 
     * ...
     * MOVE last buffer length, last buffer address, WHEN phase
     * JUMP other_transfer
     */

/* See if we're getting to data transfer */
#if 0
    if (datain) {
	cmd_datain[0] = 0x98080000;
	cmd_datain[1] = 0x03ffd00d;
	cmd_datain += 2;
    }
#endif

/* 
 * XXX - I'm undecided whether all of this nonsense is faster
 * in the long run, or whether I should just go and implement a loop
 * on the NCR chip using table indirect mode?
 *
 * In any case, this is how it _must_ be done for 53c700/700-66 chips,
 * so this stays even when we come up with something better.
 *
 * When we're limited to 1 simultaneous command, no overlapping processing,
 * we're seeing 630K/sec, with 7% CPU usage on a slow Syquest 45M
 * drive.
 *
 * Not bad, not good. We'll see.
 */

    for (i = 0; cmd->use_sg ? (i < cmd->use_sg) : !i; cmd_datain += 4, 
	cmd_dataout += 4, ++i) {
	u32 buf = cmd->use_sg ?
	    virt_to_bus(((struct scatterlist *)cmd->buffer)[i].address) :
	    virt_to_bus(cmd->request_buffer);
	u32 count = cmd->use_sg ?
	    ((struct scatterlist *)cmd->buffer)[i].length :
	    cmd->request_bufflen;

	if (datain) {
	    cmd_datain[0] = ((DCMD_TYPE_BMI | DCMD_BMI_OP_MOVE_I | DCMD_BMI_IO) 
    	    	<< 24) | count;
	    cmd_datain[1] = buf;
	    cmd_datain[2] = ((DCMD_TYPE_TCI | DCMD_TCI_OP_CALL | 
		DCMD_TCI_CD | DCMD_TCI_IO | DCMD_TCI_MSG) << 24) | 
		DBC_TCI_WAIT_FOR_VALID | DBC_TCI_COMPARE_PHASE | DBC_TCI_TRUE;
	    cmd_datain[3] = virt_to_bus(hostdata->script) +
		hostdata->E_msg_in;
#if 0
	    print_insn (host, cmd_datain, "dynamic ", 1);
	    print_insn (host, cmd_datain + 2, "dynamic ", 1);
#endif
	}
	if (dataout) {
	    cmd_dataout[0] = ((DCMD_TYPE_BMI | DCMD_BMI_OP_MOVE_I) << 24) 
		| count;
	    cmd_dataout[1] = buf;
	    cmd_dataout[2] = ((DCMD_TYPE_TCI | DCMD_TCI_OP_CALL | 
		DCMD_TCI_CD | DCMD_TCI_IO | DCMD_TCI_MSG) << 24) | 
		DBC_TCI_WAIT_FOR_VALID | DBC_TCI_COMPARE_PHASE | DBC_TCI_TRUE;
	    cmd_dataout[3] = virt_to_bus(hostdata->script) +
		hostdata->E_msg_in;
#if 0
	    print_insn (host, cmd_dataout, "dynamic ", 1);
	    print_insn (host, cmd_dataout + 2, "dynamic ", 1);
#endif
	}
    }

    /*
     * Install JUMP instructions after the data transfer routines to return
     * control to the do_other_transfer routines.
     */
  
    
    if (datain) {
	cmd_datain[0] = ((DCMD_TYPE_TCI | DCMD_TCI_OP_JUMP) << 24) |
    	    DBC_TCI_TRUE;
	cmd_datain[1] = virt_to_bus(hostdata->script) +
	    hostdata->E_other_transfer;
#if 0
	print_insn (host, cmd_datain, "dynamic jump ", 1);
#endif
	cmd_datain += 2; 
    }
#if 0
    if (datain) {
	cmd_datain[0] = 0x98080000;
	cmd_datain[1] = 0x03ffdeed;
	cmd_datain += 2;
    }
#endif


    if (dataout) {
	cmd_dataout[0] = ((DCMD_TYPE_TCI | DCMD_TCI_OP_JUMP) << 24) |
    	    DBC_TCI_TRUE;
	cmd_dataout[1] = virt_to_bus(hostdata->script) +
	    hostdata->E_other_transfer;
#if 0
	print_insn (host, cmd_dataout, "dynamic jump ", 1);
#endif
	cmd_dataout += 2;
    }


    return tmp;
}
    
/* 
 * Function : int NCR53c7xx_queue_command (Scsi_Cmnd *cmd,
 *      void (*done)(Scsi_Cmnd *)) 
 *
 * Purpose :  enqueues a SCSI command
 *
 * Inputs : cmd - SCSI command, done - function called on completion, with
 *      a pointer to the command descriptor.
 * 
 * Returns : 0
 *
 * Side effects : 
 *      cmd is added to the per instance issue_queue, with minor 
 *      twiddling done to the host specific fields of cmd.  If the 
 *      main coroutine is not running, it is restarted.
 *
 */

int NCR53c7xx_queue_command (Scsi_Cmnd *cmd, void (* done)(Scsi_Cmnd *)) {
    NCR53c7x0_local_declare();
    struct NCR53c7x0_cmd *tmp;
    struct Scsi_Host *host = cmd->host;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    unsigned long flags;
    unsigned char target_was_busy;
    NCR53c7x0_local_setup(host);
    
    if (((hostdata->options & (OPTION_DEBUG_INIT_ONLY|OPTION_DEBUG_PROBE_ONLY)) ||
	((hostdata->options & OPTION_DEBUG_TARGET_LIMIT) &&
	!(hostdata->debug_lun_limit[cmd->target] & (1 << cmd->lun)))) ||
	cmd->target > 7) {
	printk("scsi%d : disabled target %d lun %d\n", host->host_no,
	    cmd->target, cmd->lun);
	cmd->result = (DID_BAD_TARGET << 16);
	done(cmd);
	return 0;
    }

    if (hostdata->options & OPTION_DEBUG_NCOMMANDS_LIMIT) {
	if (hostdata->debug_count_limit == 0) {
	    printk("scsi%d : maximum commands exceeded\n", host->host_no);
	    cmd->result = (DID_BAD_TARGET << 16);
	    done(cmd);
	    return 0;
	} else if (hostdata->debug_count_limit != -1) 
	    --hostdata->debug_count_limit;
    }
   
    if (hostdata->options & OPTION_DEBUG_READ_ONLY) {
	switch (cmd->cmnd[0]) {
	case WRITE_6:
	case WRITE_10:
	    printk("scsi%d : WRITE attempted with NO_WRITE debugging flag set\n",
		host->host_no);
	    cmd->result = (DID_BAD_TARGET << 16);
	    done(cmd);
	    return 0;
	}
    }

    cmd->scsi_done = done;
    cmd->result = 0xffff;		/* The NCR will overwrite message
					   and status with valid data */
    
    cmd->host_scribble = (unsigned char *) tmp = create_cmd (cmd);

    /*
     * On NCR53c710 and better chips, we have two issue queues : 
     * The queue maintained by the Linux driver, and the queue 
     * maintained by the NCR chip.
     * 
     * The Linux queue includes commands which have been generated,
     * but may be unable to execute because the device is busy, 
     * where as the NCR queue contains commands to issue as soon
     * as BUS FREE is detected.
     *
     * NCR53c700 and NCR53c700-66 chips use only the Linux driver
     * queue. 
     * 
     * So, insert into the Linux queue if the device is busy or 
     * we are running on an old chip, otherwise insert directly into
     * the NCR queue.
     */
    
    /*
     * REQUEST sense commands need to be executed before all other 
     * commands since any command will clear the contingent allegiance 
     * condition that exists and the sense data is only guaranteed to be 
     * valid while the condition exists.
     */

    save_flags(flags);
    cli();

    /* 
     * Consider a target busy if there are _any_ commands running
     * on it.  
     * XXX - Once we do SCSI-II tagged queuing, we want to use 
     *     a different definition of busy.
     */
    	
    target_was_busy = hostdata->busy[cmd->target][cmd->lun]
#ifdef LUN_BUSY
	++
#endif
; 

    if (!(hostdata->options & OPTION_700)  &&
	!target_was_busy) {
	unsigned char *dsa = ((unsigned char *) tmp->dsa)
		- hostdata->dsa_start;	
    	    	/* dsa start is negative, so subtraction is used */
#if 0	
	printk("scsi%d : new dsa is 0x%p\n", host->host_no, dsa);
#endif

    	if (hostdata->running_list)
    	    hostdata->running_list->prev = tmp;

    	tmp->next = (struct NCR53c7x0_cmd*) hostdata->running_list;

	if (!hostdata->running_list)
	    hostdata->running_list = (struct NCR53c7x0_cmd*) tmp;
    	

	if (hostdata->idle) {
	    hostdata->idle = 0;
    	    hostdata->state = STATE_RUNNING;
	    NCR53c7x0_write32 (DSP_REG,  virt_to_bus(hostdata->script) +
		hostdata->E_schedule);
	    mb();
	}

/* XXX - make function */
	for (;;) {
	    /* 
	     * If the NCR doesn't have any commands waiting in its
	     * issue queue, then we simply create a new issue queue,
	     * and signal the NCR that we have more commands.
	     */
		
	    if (!hostdata->issue_dsa_head) {
#if 0
		printk ("scsi%d : no issue queue\n", host->host_no);
#endif
    	    	hostdata->issue_dsa_tail = (u32 *) dsa;
		hostdata->issue_dsa_head = virt_to_bus(dsa);
    	    	NCR53c7x0_write8(hostdata->istat, 
    	    	    NCR53c7x0_read8(hostdata->istat) | ISTAT_10_SIGP);
		mb();
		break;
	    /*
	     * Otherwise, we blindly perform an atomic write 
	     * to the next pointer of the last command we 
	     * placed in that queue.
	     *
	     * Looks like it doesn't work, but I think it does - 
 	     */
 	    } else {
		printk ("scsi%d : existing issue queue\n", host->host_no);
		hostdata->issue_dsa_tail[hostdata->dsa_next/sizeof(u32)]
		  = virt_to_bus(dsa);
		hostdata->issue_dsa_tail = (u32 *) dsa;
	    /*
	     * After which, one of two things will happen : 
	     * The NCR will have scheduled a command, either this
	     * one, or the next one.  In this case, we successfully
	     * added our command to the queue.
	     *
	     * The NCR will have written the hostdata->issue_dsa_head
	     * pointer with the NULL pointer terminating the list,
	     * in which case we were too late.  If this happens,
	     * we restart
	     */
		if (hostdata->issue_dsa_head)
		    break;
	    }
	}
/* XXX - end */
    } else {
#if 1
	printk ("scsi%d : using issue_queue instead of issue_dsa_head!\n",
    	    host->host_no);
#endif
	for (tmp = (struct NCR53c7x0_cmd *) hostdata->issue_queue; 
    	    tmp->next; tmp = (struct NCR53c7x0_cmd *) tmp->next);
    	tmp->next = tmp;
    }
    restore_flags(flags);
    return 0;
}


int fix_pointers (u32 dsa) {
    return 0;
}

/*
 * Function : static void intr_scsi (struct Scsi_Host *host, 
 * 	struct NCR53c7x0_cmd *cmd)
 *
 * Purpose : handle all SCSI interrupts, indicated by the setting 
 * 	of the SIP bit in the ISTAT register.
 *
 * Inputs : host, cmd - host and NCR command causing the interrupt, cmd
 * 	may be NULL.
 */

static void intr_scsi (struct Scsi_Host *host, struct NCR53c7x0_cmd *cmd) {
    NCR53c7x0_local_declare();
    struct NCR53c7x0_hostdata *hostdata = 
    	(struct NCR53c7x0_hostdata *) host->hostdata;
    unsigned char sstat0_sist0, sist1, 		/* Registers */
	    fatal; 				/* Did a fatal interrupt 
						   occur ? */
    int is_8xx_chip;
    NCR53c7x0_local_setup(host);

    fatal = 0;
  
    is_8xx_chip = ((unsigned) (hostdata->chip - 800)) < 100;
    if (is_8xx_chip) {
    	sstat0_sist0 = NCR53c7x0_read8(SIST0_REG_800);
	udelay(1);
    	sist1 = NCR53c7x0_read8(SIST1_REG_800);
    } else {
    	sstat0_sist0 = NCR53c7x0_read8(SSTAT0_REG);
    	sist1 = 0;
    }

    if (hostdata->options & OPTION_DEBUG_INTR) 
	printk ("scsi%d : SIST0 0x%0x, SIST1 0x%0x\n", host->host_no,
	    sstat0_sist0, sist1);

    /* selection timeout */
    if ((is_8xx_chip && (sist1 & SIST1_800_STO)) ||
	(!is_8xx_chip && (sstat0_sist0 & SSTAT0_700_STO))) {
	fatal = 1;
	if (hostdata->options & OPTION_DEBUG_INTR) {
	    printk ("scsi%d : Selection Timeout\n", host->host_no);
    	    if (cmd) {
    	    	printk("scsi%d : target %d, lun %d, command ",
    	    	    host->host_no, cmd->cmd->target, cmd->cmd->lun);
    	    	print_command (cmd->cmd->cmnd);
		printk("scsi%d : dsp = 0x%x\n", host->host_no,
		    (unsigned) NCR53c7x0_read32(DSP_REG));
    	    } else {
    	    	printk("scsi%d : no command\n", host->host_no);
    	    }
    	}
/*
 * XXX - question : how do we want to handle the Illegal Instruction
 * 	interrupt, which may occur before or after the Selection Timeout
 * 	interrupt?
 */

	if (1) {
	    hostdata->idle = 1;
	    hostdata->expecting_sto = 0;

	    if (hostdata->test_running) {
		hostdata->test_running = 0;
		hostdata->test_completed = 3;
	    } else if (cmd) {
		abnormal_finished(cmd, DID_BAD_TARGET << 16);
	    }
#if 0	    
	    hostdata->intrs = 0;
#endif
	}
    } 
    
    if (sstat0_sist0 & SSTAT0_UDC) {
	fatal = 1;
	if (cmd) {
	    printk("scsi%d : target %d lun %d unexpected disconnect\n",
		host->host_no, cmd->cmd->target, cmd->cmd->lun);
	    abnormal_finished(cmd, DID_ERROR << 16);
	}
	hostdata->dsp = hostdata->script + hostdata->E_schedule / 
	    sizeof(u32);
	hostdata->dsp_changed = 1;
    /* SCSI PARITY error */
    } 

    if (sstat0_sist0 & SSTAT0_PAR) {
	fatal = 1;
	if (cmd && cmd->cmd) {
	    printk("scsi%d : target %d lun %d parity error.\n",
		host->host_no, cmd->cmd->target, cmd->cmd->lun);
	    abnormal_finished (cmd, DID_PARITY << 16); 
	} else
	    printk("scsi%d : parity error\n", host->host_no);
	/* Should send message out, parity error */

	/* XXX - Reduce synchronous transfer rate! */
	hostdata->dsp = hostdata->script + hostdata->E_initiator_abort /
    	    sizeof(u32);
	hostdata->dsp_changed = 1; 
    /* SCSI GROSS error */
    } 

    if (sstat0_sist0 & SSTAT0_SGE) {
	fatal = 1;
	printk("scsi%d : gross error\n", host->host_no);
	/* XXX Reduce synchronous transfer rate! */
	hostdata->dsp = hostdata->script + hostdata->E_initiator_abort /
    	    sizeof(u32);
	hostdata->dsp_changed = 1;
    /* Phase mismatch */
    } 

    if (sstat0_sist0 & SSTAT0_MA) {
	fatal = 1;
	if (hostdata->options & OPTION_DEBUG_INTR)
	    printk ("scsi%d : SSTAT0_MA\n", host->host_no);
	intr_phase_mismatch (host, cmd);
    }

#if 1
/*
 * If a fatal SCSI interrupt occurs, we must insure that the DMA and
 * SCSI FIFOs were flushed.
 */

    if (fatal) {
	if (!hostdata->dstat_valid) {
	    hostdata->dstat = NCR53c7x0_read8(DSTAT_REG);
	    hostdata->dstat_valid = 1;
	}

/* XXX - code check for 700/800 chips */
	if (!(hostdata->dstat & DSTAT_DFE)) {
	    printk ("scsi%d : DMA FIFO not empty\n", host->host_no);
#if 0
    	    if (NCR53c7x0_read8 (CTEST2_REG_800) & CTEST2_800_DDIR) {
    	    	NCR53c7x0_write8 (CTEST3_REG_800, CTEST3_800_FLF);
		mb();
    	    	while (!((hostdata->dstat = NCR53c7x0_read8(DSTAT_REG)) &
    	    	    DSTAT_DFE));
    	    } else 
#endif
	    {
    	    	NCR53c7x0_write8 (CTEST3_REG_800, CTEST3_800_CLF);
		mb();
    	    	while (NCR53c7x0_read8 (CTEST3_REG_800) & CTEST3_800_CLF);
    	    }
    	}

	NCR53c7x0_write8 (STEST3_REG_800, STEST3_800_CSF);
	mb();
	while (NCR53c7x0_read8 (STEST3_REG_800) & STEST3_800_CSF);
    }
#endif
}

/*
 * Function : static void NCR53c7x0_intr (int irq, struct pt_regs * regs)
 *
 * Purpose : handle NCR53c7x0 interrupts for all NCR devices sharing
 *	the same IRQ line.  
 * 
 * Inputs : Since we're using the SA_INTERRUPT interrupt handler
 *	semantics, irq indicates the interrupt which invoked 
 *	this handler.  
 */

static void NCR53c7x0_intr (int irq, struct pt_regs * regs) {
    NCR53c7x0_local_declare();
    struct Scsi_Host *host;			/* Host we are looking at */
    unsigned char istat; 			/* Values of interrupt regs */
    struct NCR53c7x0_hostdata *hostdata;	/* host->hostdata */
    struct NCR53c7x0_cmd *cmd,			/* command which halted */
	**cmd_prev_ptr;
    u32 *dsa;					/* DSA */
    int done = 1;				/* Indicates when handler 
						   should terminate */
    int interrupted = 0;			/* This HA generated 
						   an interrupt */
    unsigned long flags;

#ifdef NCR_DEBUG
    char buf[80];				/* Debugging sprintf buffer */
    size_t buflen;				/* Length of same */
#endif

#if 0
    printk("interrupt %d received\n", irq);
#endif

    do {
	done = 1;
	for (host = first_host; host; host = hostdata->next) {
    	    NCR53c7x0_local_setup(host);

	    hostdata = (struct NCR53c7x0_hostdata *) host->hostdata;
	    hostdata->dsp_changed = 0;
	    interrupted = 0;


	    do {
		int is_8xx_chip;

		hostdata->dstat_valid = 0;
		interrupted = 0;
		/*
		 * Only read istat once, since reading it again will unstack
		 * interrupts.
		 */
		istat = NCR53c7x0_read8(hostdata->istat);

		/*
		 * INTFLY interrupts are used by the NCR53c720, NCR53c810,
		 * and NCR53c820 to signify completion of a command.  Since 
		 * the SCSI processor continues running, we can't just look
		 * at the contents of the DSA register and continue running.
		 */
/* XXX - this is getting big, and should move to intr_intfly() */
		is_8xx_chip = ((unsigned) (hostdata->chip - 800)) < 100;
		if ((hostdata->options & OPTION_INTFLY) && 
		    (is_8xx_chip && (istat & ISTAT_800_INTF))) {
		    char search_found = 0;	/* Got at least one ? */
		    done = 0;
		    interrupted = 1;

		    /* 
		     * Clear the INTF bit by writing a one.  This reset operation 
		     * is self-clearing.
		     */
		    NCR53c7x0_write8(hostdata->istat, istat|ISTAT_800_INTF);
		    mb();

		    if (hostdata->options & OPTION_DEBUG_INTR)
			printk ("scsi%d : INTFLY\n", host->host_no); 

		    /*
		     * Traverse our list of running commands, and look
		     * for those with valid (non-0xff ff) status and message
		     * bytes encoded in the result which signify command
		     * completion.
		     */


		    save_flags(flags);
		    cli();
restart:
		    for (cmd_prev_ptr = (struct NCR53c7x0_cmd **) 
			 &(hostdata->running_list), cmd = 
			 (struct NCR53c7x0_cmd *) hostdata->running_list; cmd ;
			 cmd_prev_ptr = (struct NCR53c7x0_cmd **) &(cmd->next), 
    	    	    	 cmd = (struct NCR53c7x0_cmd *) cmd->next) {
			Scsi_Cmnd *tmp;

			if (!cmd) {
			    printk("scsi%d : very weird.\n", host->host_no);
			    break;
			}

			if (!(tmp = cmd->cmd)) {
			    printk("scsi%d : weird.  NCR53c7x0_cmd has no Scsi_Cmnd\n",
				host->host_no);
				continue;
			}
#if 0
			printk ("scsi%d : looking at result of 0x%x\n",
			    host->host_no, cmd->cmd->result);
#endif
		
			if (((tmp->result & 0xff) == 0xff) ||
			    ((tmp->result & 0xff00) == 0xff00))
			    continue;

			search_found = 1;

			/* Important - remove from list _before_ done is called */
			/* XXX - SLL.  Seems like DLL is unnecessary */
			if (cmd->prev)
			    cmd->prev->next = cmd->next;
			if (cmd_prev_ptr)
			    *cmd_prev_ptr = (struct NCR53c7x0_cmd *) cmd->next;

#ifdef LUN_BUSY
			/* Check for next command for target, add to issue queue */
			if (--hostdata->busy[tmp->target][tmp->lun]) {
			}
#endif


    	    	    	cmd->next = hostdata->free;
    	    	    	hostdata->free = cmd;

    	    	    	tmp->host_scribble = NULL;

			if (hostdata->options & OPTION_DEBUG_INTR) {
			    printk ("scsi%d : command complete : pid %lu, id %d,lun %d result 0x%x ", 
				host->host_no, tmp->pid, tmp->target, tmp->lun, tmp->result);
			    print_command (tmp->cmnd);
			}

			
#if 0
			hostdata->options &= ~OPTION_DEBUG_INTR;
#endif
			tmp->scsi_done(tmp);
			goto restart;

		    }
		    restore_flags(flags);

		    if (!search_found)  {
			printk ("scsi%d : WARNING : INTFLY with no completed commands.\n",
			    host->host_no);
		    }
		}

		if (istat & (ISTAT_SIP|ISTAT_DIP)) {
		    done = 0;
		    interrupted = 1;
    	    	    hostdata->state = STATE_HALTED;
		    /*
		     * NCR53c700 and NCR53c700-66 change the current SCSI
		     * process, hostdata->current_cmd, in the Linux driver so
		     * cmd = hostdata->current_cmd.
		     *
		     * With other chips, we must look through the commands
		     * executing and find the command structure which 
		     * corresponds to the DSA register.
		     */

		    if (hostdata->options & OPTION_700) {
			cmd = (struct NCR53c7x0_cmd *) hostdata->current_cmd;
		    } else {
			dsa = bus_to_virt(NCR53c7x0_read32(DSA_REG));
			for (cmd = (struct NCR53c7x0_cmd *) 
			    hostdata->running_list; cmd &&
    	    	    	    (dsa + (hostdata->dsa_start / sizeof(u32))) != 
    	    	    	    	cmd->dsa;
			    cmd = (struct NCR53c7x0_cmd *)(cmd->next));
		    }
		    if (hostdata->options & OPTION_DEBUG_INTR) {
			if (cmd) {
			    printk("scsi%d : interrupt for pid %lu, id %d, lun %d ", 
				host->host_no, cmd->cmd->pid, (int) cmd->cmd->target,
				(int) cmd->cmd->lun);
			    print_command (cmd->cmd->cmnd);
			} else {
			    printk("scsi%d : no active command\n", host->host_no);
			}
		    }

		    if (istat & ISTAT_SIP) {
			if (hostdata->options & OPTION_DEBUG_INTR) 
			    printk ("scsi%d : ISTAT_SIP\n", host->host_no);
			intr_scsi (host, cmd);
		    }
		
		    if (istat & ISTAT_DIP) {
			if (hostdata->options & OPTION_DEBUG_INTR) 
			    printk ("scsi%d : ISTAT_DIP\n", host->host_no);
			intr_dma (host, cmd);
		    }

		    if (!hostdata->dstat_valid) {
			hostdata->dstat = NCR53c7x0_read8(DSTAT_REG);
			hostdata->dstat_valid = 1;
		    }

#if 1
	    /* XXX - code check for 700/800 chips */
		    if (!(hostdata->dstat & DSTAT_DFE)) {
			printk ("scsi%d : DMA FIFO not empty\n", host->host_no);
	    #if 0
			if (NCR53c7x0_read8 (CTEST2_REG_800) & CTEST2_800_DDIR) {
			    NCR53c7x0_write8 (CTEST3_REG_800, CTEST3_800_FLF);
			    mb();
			    while (!((hostdata->dstat = NCR53c7x0_read8(DSTAT_REG)) &
				DSTAT_DFE));
			} else 
	    #endif
			{
			    NCR53c7x0_write8 (CTEST3_REG_800, CTEST3_800_CLF);
			    mb();
			    while (NCR53c7x0_read8 (CTEST3_REG_800) & CTEST3_800_CLF);
			}
		    }
#endif
		}
	    } while (interrupted);



	    if (hostdata->intrs != -1)
		hostdata->intrs++;
#if 0
	    if (hostdata->intrs > 4) {
		printk("scsi%d : too many interrupts, halting", host->host_no);
		hostdata->idle = 1;
		hostdata->options |= OPTION_DEBUG_INIT_ONLY;
		panic("dying...\n");
	    }
#endif

	    if (!hostdata->idle && hostdata->state == STATE_HALTED) {
		if (!hostdata->dsp_changed) {
		    hostdata->dsp = bus_to_virt(NCR53c7x0_read32(DSP_REG));
		}
			
#if 0
		printk("scsi%d : new dsp is 0x%p\n", host->host_no, 
		    hostdata->dsp);
#endif
		
		hostdata->state = STATE_RUNNING;
		NCR53c7x0_write32 (DSP_REG, virt_to_bus(hostdata->dsp));
		mb();
	    }
	}
    } while (!done);
}


/* 
 * Function : static int abort_connected (struct Scsi_Host *host)
 *
 * Purpose : Assuming that the NCR SCSI processor is currently 
 * 	halted, break the currently established nexus.  Clean
 *	up of the NCR53c7x0_cmd and Scsi_Cmnd structures should
 *	be done on receipt of the abort interrupt.
 *
 * Inputs : host - SCSI host
 *
 */

static int 
abort_connected (struct Scsi_Host *host) {
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;

    hostdata->dsp = hostdata->script + hostdata->E_initiator_abort /
	sizeof(u32);
    hostdata->dsp_changed = 1;
    printk ("scsi%d : DANGER : abort_connected() called \n",
	host->host_no);
/* XXX - need to flag the command as aborted after the abort_connected
 	 code runs 
 */
    return 0;
}


/* 
 * Function : static void intr_phase_mismatch (struct Scsi_Host *host, 
 *	struct NCR53c7x0_cmd *cmd)
 *
 * Purpose : Handle phase mismatch interrupts
 *
 * Inputs : host, cmd - host and NCR command causing the interrupt, cmd
 * 	may be NULL.
 *
 * Side effects : The abort_connected() routine is called or the NCR chip 
 *	is restarted, jumping to the command_complete entry point, or 
 *	patching the address and transfer count of the current instruction 
 *	and calling the msg_in entry point as appropriate.
 *
 */

static void intr_phase_mismatch (struct Scsi_Host *host, struct NCR53c7x0_cmd
    *cmd) {
    NCR53c7x0_local_declare();
    u32 dbc_dcmd, *dsp, *dsp_next;
    unsigned char dcmd, sbcl;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
    	host->hostdata;
    const char *phase;
    NCR53c7x0_local_setup(host);

    if (!cmd) {
	printk ("scsi%d : phase mismatch interrupt occurred with no current command.\n",
	    host->host_no);
	abort_connected(host);
	return;
    }

    /*
     * Corrective action is based on where in the SCSI SCRIPT(tm) the error 
     * occurred, as well as which SCSI phase we are currently in.
     */

    dsp_next = bus_to_virt(NCR53c7x0_read32(DSP_REG));

    /*
     * Like other processors, the NCR adjusts the DSP pointer before
     * instruction decode.  Set the DSP address back to what it should
     * be for this instruction based on its size (2 or 3 words).
     */

    dbc_dcmd = NCR53c7x0_read32(DBC_REG);
    dcmd = (dbc_dcmd & 0xff000000) >> 24;
    dsp = dsp_next - NCR53c7x0_insn_size(dcmd);
    
    /*
     * Read new SCSI phase from the SBCL lines.
     *
     * Note that since all of our code uses a WHEN conditional instead of an 
     * IF conditional, we don't need to wait for a valid REQ.
     */
    sbcl = NCR53c7x0_read8(SBCL_REG);
    switch (sbcl) {
    case SBCL_PHASE_DATAIN:
	phase = "DATAIN";
	break;
    case SBCL_PHASE_DATAOUT:
	phase = "DATAOUT";
	break;
    case SBCL_PHASE_MSGIN:
	phase = "MSGIN";
	break;
    case SBCL_PHASE_MSGOUT:
	phase = "MSGOUT";
	break;
    case SBCL_PHASE_CMDOUT:
	phase = "CMDOUT";
	break;
    case SBCL_PHASE_STATIN:
	phase = "STATUSIN";
	break;
    default:
	phase = "unknown";
	break;
    }


    /*
     * The way the SCSI SCRIPTS(tm) are architected, recoverable phase
     * mismatches should only occur in the data transfer routines, or
     * when a command is being aborted.  
     */
    if (dsp >= cmd->data_transfer_start && dsp < cmd->data_transfer_end) {

	/*
	 * There are three instructions used in our data transfer routines with
	 * a phase conditional on them
	 *
	 * 1.  MOVE count, address, WHEN DATA_IN
	 * 2.  MOVE count, address, WHEN DATA_OUT
	 * 3.  CALL msg_in, WHEN MSG_IN.
	 */
	switch (sbcl & SBCL_PHASE_MASK) {
	/*
	 * 1.  STATUS phase : pass control to command_complete as if 
	 *     a JUMP instruction was executed.  No patches are made.
	 */
	case SBCL_PHASE_STATIN:
	    if (hostdata->options & OPTION_DEBUG_INTR) 
		printk ("scsi%d : new phase = STATIN\n", host->host_no);
	    hostdata->dsp = hostdata->script + hostdata->E_command_complete /
    	    	sizeof(u32);
	    hostdata->dsp_changed = 1;
	    return;
	/*
	 * 2.  MSGIN phase : pass control to msg_in as if a CALL
	 *     instruction was executed.  Patch current instruction.
	 */
/* 
 * XXX - This is buggy.
 */
	case SBCL_PHASE_MSGIN:
	    if (hostdata->options & OPTION_DEBUG_INTR) 
		printk ("scsi%d  : new phase = MSGIN\n", host->host_no);
	    if ((dcmd & (DCMD_TYPE_MASK|DCMD_BMI_OP_MASK|DCMD_BMI_INDIRECT|
		    DCMD_BMI_MSG|DCMD_BMI_CD)) == (DCMD_TYPE_BMI|
		    DCMD_BMI_OP_MOVE_I)) {
		dsp[0] = dbc_dcmd;
		dsp[1] = NCR53c7x0_read32(DNAD_REG);
		NCR53c7x0_write32(TEMP_REG, virt_to_bus(dsp));
		mb();
		hostdata->dsp = hostdata->script + hostdata->E_msg_in /
    	    	    sizeof(u32);
		hostdata->dsp_changed = 1;
	    } else {
		printk("scsi%d : unexpected MSGIN in dynamic NCR code, dcmd=0x%x.\n",
		    host->host_no, dcmd);
		print_insn (host, dsp, "", 1);
		print_insn (host, dsp_next, "", 1);
		abort_connected (host);
	    }
	    return;
	/*
	 * MSGOUT phase - shouldn't happen, because we haven't 
	 *		asserted ATN.
	 * CMDOUT phase - shouldn't happen, since we've already
	 * 		sent a valid command.
	 * DATAIN/DATAOUT - other one shouldn't happen, since 
	 * 		SCSI commands can ONLY have one or the other.
	 *
	 * So, we abort the command if one of these things happens.
	 */
	default:
	    printk ("scsi%d : unexpected phase %s in data routine\n",
		host->host_no, phase);
	    abort_connected(host);
	} 
    /*
     * Any other phase mismatches abort the currently executing command.
     */
    } else {
	printk ("scsi%d : unexpected phase %s at dsp = 0x%p\n",
	    host->host_no, phase, dsp);
	print_insn (host, dsp, "", 1);
	print_insn (host, dsp_next, "", 1);
	abort_connected(host);
    }
}

/*
 * Function : static void intr_dma (struct Scsi_Host *host, 
 * 	struct NCR53c7x0_cmd *cmd)
 *
 * Purpose : handle all DMA interrupts, indicated by the setting 
 * 	of the DIP bit in the ISTAT register.
 *
 * Inputs : host, cmd - host and NCR command causing the interrupt, cmd
 * 	may be NULL.
 */

static void intr_dma (struct Scsi_Host *host, struct NCR53c7x0_cmd *cmd) {
    NCR53c7x0_local_declare();
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    unsigned char dstat;	/* DSTAT */	
    u32 *dsp,
	*next_dsp,		/* Current dsp */
    	*dsa,
	dbc_dcmd;		/* DCMD (high eight bits) + DBC */
    int tmp;
    unsigned long flags;
    NCR53c7x0_local_setup(host);

    if (!hostdata->dstat_valid) {
	hostdata->dstat = NCR53c7x0_read8(DSTAT_REG);
	hostdata->dstat_valid = 1;
    }
    
    dstat = hostdata->dstat;
    
    if (hostdata->options & OPTION_DEBUG_INTR)
	printk("scsi%d : DSTAT=0x%x\n", host->host_no, (int) dstat);

    dbc_dcmd = NCR53c7x0_read32 (DBC_REG);
    next_dsp = bus_to_virt(NCR53c7x0_read32(DSP_REG));
    dsp = next_dsp - NCR53c7x0_insn_size ((dbc_dcmd >> 24) & 0xff);
/* XXX - check chip type */
    dsa = bus_to_virt(NCR53c7x0_read32(DSA_REG));

    /*
     * DSTAT_ABRT is the aborted interrupt.  This is set whenever the 
     * SCSI chip is aborted.  
     * 
     * With NCR53c700 and NCR53c700-66 style chips, we should only 
     * get this when the chip is currently running the accept 
     * reselect/select code and we have set the abort bit in the 
     * ISTAT register.
     *
     */
    
    if (dstat & DSTAT_ABRT) {
#if 0
	/* XXX - add code here to deal with normal abort */
	if ((hostdata->options & OPTION_700) && (hostdata->state ==
	    STATE_ABORTING) {
	} else 
#endif
	{
	    printk("scsi%d : unexpected abort interrupt at\n" 
		   "         ", host->host_no);
	    print_insn (host, dsp, "s ", 1);
	    panic(" ");
	}
    }

    /*
     * DSTAT_SSI is the single step interrupt.  Should be generated 
     * whenever we have single stepped or are tracing.
     */

    if (dstat & DSTAT_SSI) {
	if (hostdata->options & OPTION_DEBUG_TRACE) {
	} else if (hostdata->options & OPTION_DEBUG_SINGLE) {
	    print_insn (host, dsp, "s ", 0);
	    save_flags(flags);
	    cli();
/* XXX - should we do this, or can we get away with writing dsp? */

	    NCR53c7x0_write8 (DCNTL_REG, (NCR53c7x0_read8(DCNTL_REG) & 
    	    	~DCNTL_SSM) | DCNTL_STD);
	    mb();
	    restore_flags(flags);
	} else {
	    printk("scsi%d : unexpected single step interrupt at\n"
		   "         ", host->host_no);
	    print_insn (host, dsp, "", 1);
	    panic("         mail drew@colorad.edu\n");
    	}
    }

    /*
     * DSTAT_IID / DSTAT_OPC (same bit, same meaning, only the name 
     * is different) is generated whenever an illegal instruction is 
     * encountered.  
     * 
     * XXX - we may want to emulate INTFLY here, so we can use 
     *    the same SCSI SCRIPT (tm) for NCR53c710 through NCR53c810  
     *	  chips once we remove the ADD WITH CARRY instructions.
     */

    if (dstat & DSTAT_OPC) {
    /* 
     * Ascertain if this IID interrupts occurred before or after a STO 
     * interrupt.  Since the interrupt handling code now leaves 
     * DSP unmodified until _after_ all stacked interrupts have been
     * processed, reading the DSP returns the original DSP register.
     * This means that if dsp lies between the select code, and 
     * message out following the selection code (where the IID interrupt
     * would have to have occurred by due to the implicit wait for REQ),
     * we have an IID interrupt resulting from a STO condition and 
     * can ignore it.
     */

	if (((dsp >= (hostdata->script + hostdata->E_select / sizeof(u32))) &&
	    (dsp <= (hostdata->script + hostdata->E_select_msgout / 
    	    sizeof(u32) + 8))) || (hostdata->test_running == 2)) {
	    if (hostdata->options & OPTION_DEBUG_INTR) 
		printk ("scsi%d : ignoring DSTAT_IID for SSTAT_STO\n",
		    host->host_no);
	    if (hostdata->expecting_iid) {
		hostdata->expecting_iid = 0;
		hostdata->idle = 1;
		if (hostdata->test_running == 2) {
		    hostdata->test_running = 0;
		    hostdata->test_completed = 3;
		} else if (cmd) 
			abnormal_finished (cmd, DID_BAD_TARGET << 16);
	    } else {
		hostdata->expecting_sto = 1;
	    }
	} else {
	    printk("scsi%d : illegal instruction ", host->host_no);
	    print_insn (host, dsp, "", 1);
	    printk("scsi%d : DSP=0x%p, DCMD|DBC=0x%x, DSA=0x%p\n"
	       "         DSPS=0x%x, TEMP=0x%x, DMODE=0x%x,\n" 
	       "         DNAD=0x%x\n",
	     host->host_no, dsp, dbc_dcmd,
	     dsa, NCR53c7x0_read32(DSPS_REG),
	     NCR53c7x0_read32(TEMP_REG), NCR53c7x0_read8(hostdata->dmode),
	     NCR53c7x0_read32(DNAD_REG));
	    panic("         mail drew@Colorado.EDU\n");
	}
    }

    /* 
     * DSTAT_BF are bus fault errors, generated when the chip has 
     * attempted to access an illegal address.
     */
    
    if (dstat & DSTAT_800_BF) {
	printk("scsi%d : BUS FAULT, DSP=0x%p, DCMD|DBC=0x%x, DSA=0x%p\n"
	       "         DSPS=0x%x, TEMP=0x%x, DMODE=0x%x\n", 
	     host->host_no, dsp, NCR53c7x0_read32(DBC_REG),
	     dsa, NCR53c7x0_read32(DSPS_REG),
	     NCR53c7x0_read32(TEMP_REG), NCR53c7x0_read8(hostdata->dmode));
	print_dsa (host, dsa);
	printk("scsi%d : DSP->\n", host->host_no);
	print_insn(host, dsp, "", 1);
	print_insn(host, next_dsp, "", 1);
#if 0
	panic("          mail drew@Colorado.EDU\n");
#else
	hostdata->idle = 1;
	hostdata->options |= OPTION_DEBUG_INIT_ONLY;
#endif
    }
	

    /* 
     * DSTAT_SIR interrupts are generated by the execution of 
     * the INT instruction.  Since the exact values available 
     * are determined entirely by the SCSI script running, 
     * and are local to a particular script, a unique handler
     * is called for each script.
     */

    if (dstat & DSTAT_SIR) {
	if (hostdata->options & OPTION_DEBUG_INTR)
	    printk ("scsi%d : DSTAT_SIR\n", host->host_no);
	switch ((tmp = hostdata->dstat_sir_intr (host, cmd))) {
	case SPECIFIC_INT_NOTHING:
	case SPECIFIC_INT_RESTART:
	    break;
	case SPECIFIC_INT_ABORT:
	    abort_connected(host);
	    break;
	case SPECIFIC_INT_PANIC:
	    printk("scsi%d : failure at ", host->host_no);
	    print_insn (host, dsp, "", 1);
	    panic("          dstat_sir_intr() returned SPECIFIC_INT_PANIC\n");
	    break;
	case SPECIFIC_INT_BREAK:
	    intr_break (host, cmd);
	    break;
	default:
	    printk("scsi%d : failure at ", host->host_no);
	    print_insn (host, dsp, "", 1);
	    panic("          dstat_sir_intr() returned unknown value %d\n", 
		tmp);
	}
    } 

/* All DMA interrupts are fatal.  Flush SCSI queue */
    NCR53c7x0_write8 (STEST3_REG_800, STEST3_800_CSF);
    mb();
    while (NCR53c7x0_read8 (STEST3_REG_800) & STEST3_800_CSF);
}

/*
 * Function : static int print_insn (struct Scsi_Host *host, 
 * 	u32 *insn, int kernel)
 *
 * Purpose : print numeric representation of the instruction pointed
 * 	to by insn to the debugging or kernel message buffer
 *	as appropriate.  
 *
 * 	If desired, a user level program can interpret this 
 * 	information.
 *
 * Inputs : host, insn - host, pointer to instruction, prefix - 
 *	string to prepend, kernel - use printk instead of debugging buffer.
 *
 * Returns : size, in ints, of instruction printed.
 */

static int print_insn (struct Scsi_Host *host, u32 *insn,
    const char *prefix, int kernel) {
    char buf[80], 		/* Temporary buffer and pointer */
	*tmp;			
    unsigned char dcmd;		/* dcmd register for *insn */
    int size;

    dcmd = (insn[0] >> 24) & 0xff;
    sprintf(buf, "%s%p : 0x%08x 0x%08x", (prefix ? prefix : ""), 
	insn, insn[0], insn[1]);
    tmp = buf + strlen(buf);
    if ((dcmd & DCMD_TYPE_MASK) == DCMD_TYPE_MMI)  {
	sprintf (tmp, " 0x%08x\n", insn[2]);
	size = 3;
    } else {
	sprintf (tmp, "\n");
	size = 2;
    }

    if (kernel) 
	printk ("%s", buf);
#ifdef NCR_DEBUG
    else {
	size_t len = strlen(buf);
	debugger_kernel_write(host, buf, len);
    }
#endif
    return size;
}

/*
 * Function : int NCR53c7xx_abort (Scsi_Cmnd *cmd)
 * 
 * Purpose : Abort an errant SCSI command, doing all necessary
 *	cleanup of the issue_queue, running_list, shared Linux/NCR
 *	dsa issue and reconnect queues.
 *
 * Inputs : cmd - command to abort, code - entire result field
 *
 * Returns : 0 on success, -1 on failure.
 */

int NCR53c7xx_abort (Scsi_Cmnd *cmd) {
    struct Scsi_Host *host = cmd->host;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *) 
	host->hostdata;
    unsigned long flags;
    volatile struct NCR53c7x0_cmd *curr, **prev;
    save_flags(flags);
    cli();

/*
 * The command could be hiding in the issue_queue.  This would be very
 * nice, as commands can't be moved from the high level driver's issue queue 
 * into the shared queue until an interrupt routine is serviced, and this
 * moving is atomic.  
 *
 * If this is the case, we don't have to worry about anything - we simply
 * pull the command out of the old queue, and call it aborted.
 */

    for (curr = (volatile struct NCR53c7x0_cmd *) hostdata->issue_queue, 
	 prev = (volatile struct NCR53c7x0_cmd **) &(hostdata->issue_queue);
	 curr && curr->cmd != cmd; prev = (volatile struct NCR53c7x0_cmd **)
	 &(curr->next), curr = (volatile struct NCR53c7x0_cmd *) curr->next);

    if (curr) {
	*prev = (struct NCR53c7x0_cmd *) curr->next;
/* XXX - get rid of DLL ? */
	if (curr->prev)
	    curr->prev->next = curr->next;

    	curr->next = hostdata->free;
    	hostdata->free = curr;

	cmd->result = 0;
	cmd->scsi_done(cmd);
	restore_flags(flags);
	return SCSI_ABORT_SUCCESS;
    }

/* 
 * That failing, the command could be in our list of already executing 
 * commands.  If this is the case, drastic measures are called for.  
 */ 

    for (curr = (volatile struct NCR53c7x0_cmd *) hostdata->running_list, 
    	 prev = (volatile struct NCR53c7x0_cmd **) &(hostdata->running_list);
	 curr && curr->cmd != cmd; prev = (volatile struct NCR53c7x0_cmd **) 
	 &(curr->next), curr = (volatile struct NCR53c7x0_cmd *) curr->next);

    if (curr) {
	restore_flags(flags);
	printk ("scsi%d : DANGER : command in running list, can not abort.\n",
	    cmd->host->host_no);
	return SCSI_ABORT_SNOOZE;
    }


/* 
 * And if we couldn't find it in any of our queues, it must have been 
 * a dropped interrupt.
 */

    curr = (struct NCR53c7x0_cmd *) cmd->host_scribble;
    curr->next = hostdata->free;
    hostdata->free = curr;

    if (((cmd->result & 0xff00) == 0xff00) ||
	((cmd->result & 0xff) == 0xff)) {
	printk ("scsi%d : did this command ever run?\n", host->host_no);
    } else {
	printk ("scsi%d : probably lost INTFLY, normal completion\n", 
	    host->host_no);
    }
    cmd->scsi_done(cmd);
    restore_flags(flags);
    return SCSI_ABORT_SNOOZE;
}

/*
 * Function : int NCR53c7xx_reset (Scsi_Cmnd *cmd) 
 * 
 * Purpose : perform a hard reset of the SCSI bus and NCR
 * 	chip.
 *
 * Inputs : cmd - command which caused the SCSI RESET
 *
 * Returns : 0 on success.
 */

int
NCR53c7xx_reset (Scsi_Cmnd *cmd) {
    NCR53c7x0_local_declare();
    unsigned long flags;
    int found;
    struct NCR53c7x0_cmd * c;
    Scsi_Cmnd *tmp;
    struct Scsi_Host *host = cmd->host;
    struct NCR53c7x0_hostdata *hostdata = host ? 
    (struct NCR53c7x0_hostdata *) host->hostdata : NULL;
    NCR53c7x0_local_setup(host);

    save_flags(flags);
    ncr_halt (host);
    NCR53c7x0_write8(SCNTL1_REG, SCNTL1_RST);
    mb();
    udelay(25);	/* Minimum amount of time to assert RST */
    NCR53c7x0_write8(SCNTL1_REG, SCNTL1_RST);
    mb();
    for (c = (struct NCR53c7x0_cmd *) hostdata->running_list, found = 0; c; 
    	c = (struct NCR53c7x0_cmd *) c->next)  {
	tmp = c->cmd;
    	c->next = hostdata->free;
    	hostdata->free = c;

	if (tmp == cmd)
	    found = 1; 
    	tmp->result = DID_RESET << 16;
	tmp->scsi_done(tmp);
    }
    if (!found) {
    	c = (struct NCR53c7x0_cmd *) cmd->host_scribble;
    	if (c) {
    	    c->next = hostdata->free;
    	    hostdata->free = c;
    	}
    	cmd->result = DID_RESET << 16;
    	cmd->scsi_done(cmd);
    }
    restore_flags(flags);
    return SCSI_RESET_SUCCESS;
}

/*
 * The NCR SDMS bios follows Annex A of the SCSI-CAM draft, and 
 * therefore shares the scsicam_bios_param function.
 */

static void print_dsa (struct Scsi_Host *host, u32 *dsa) {
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    int i, len;
    char *ptr;

    printk("scsi%d : dsa at 0x%p\n"
	    "        + %d : dsa_msgout length = %d, data = 0x%x\n" ,
    	    host->host_no, dsa, hostdata->dsa_msgout,
    	    dsa[hostdata->dsa_msgout / sizeof(u32)],
	    dsa[hostdata->dsa_msgout / sizeof(u32) + 1]);

    for (i = dsa[hostdata->dsa_msgout / sizeof(u32)],
	ptr = bus_to_virt(dsa[hostdata->dsa_msgout / sizeof(u32) + 1]); i > 0;
	ptr += len, i -= len) {
	printk("               ");
	len = print_msg (ptr);
	printk("\n");
    }
}

/*
 * Function : static int shutdown (struct Scsi_Host *host)
 * 
 * Purpose : does a clean (we hope) shutdown of the NCR SCSI 
 *	chip.  Use prior to dumping core, unloading the NCR driver,
 *	etc.
 * 
 * Returns : 0 on success
 */
#ifdef MODULE
static int 
shutdown (struct Scsi_Host *host) {
    NCR53c7x0_local_declare();
    unsigned long flags;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    NCR53c7x0_local_setup(host);
    save_flags (flags);
    cli();
    ncr_halt (host);
    hostdata->soft_reset(host);
/* 
 * For now, we take the simplest solution : reset the SCSI bus. Eventually,
 * - If a command is connected, kill it with an ABORT message
 * - If commands are disconnected, connect to each target/LUN and 
 *	do a ABORT, followed by a SOFT reset, followed by a hard 
 *	reset.  
 */
    NCR53c7x0_write8(SCNTL1_REG, SCNTL1_RST);
    mb();
    udelay(25);	/* Minimum amount of time to assert RST */
    NCR53c7x0_write8(SCNTL1_REG, SCNTL1_RST);
    mb();
    restore_flags (flags);
    return 0;
}
#endif


/*
 * Function : static int ncr_halt (struct Scsi_Host *host)
 * 
 * Purpose : halts the SCSI SCRIPTS(tm) processor on the NCR chip
 *
 * Inputs : host - SCSI chip to halt
 *
 * Returns : 0 on success
 */

static int 
ncr_halt (struct Scsi_Host *host) {
    NCR53c7x0_local_declare();
    unsigned long flags;
    unsigned char istat, tmp;
    struct NCR53c7x0_hostdata *hostdata = (struct NCR53c7x0_hostdata *)
	host->hostdata;
    NCR53c7x0_local_setup(host);

    save_flags(flags);
    cli();
    NCR53c7x0_write8(hostdata->istat, ISTAT_ABRT);
    mb();
    /* Eat interrupts until we find what we're looking for */
    for (;;) {
	istat = NCR53c7x0_read8 (hostdata->istat);
	if (istat & ISTAT_SIP) {
	    if ((hostdata->chip / 100) == 8) {
		tmp = NCR53c7x0_read8(SIST0_REG_800);
		udelay(1);
		tmp = NCR53c7x0_read8(SIST1_REG_800);
	    } else {
		tmp = NCR53c7x0_read8(SSTAT0_REG);
	    }
	} else if (istat & ISTAT_DIP) {
	    NCR53c7x0_write8(hostdata->istat, 0);
	    mb();
	    tmp = NCR53c7x0_read8(DSTAT_REG);
	    if (tmp & DSTAT_ABRT)
		break;
	    else
		panic("scsi%d: could not halt NCR chip\n", host->host_no);
	}
    }
    hostdata->state = STATE_HALTED;
    restore_flags(flags);
    return 0;
}

#ifdef MODULE
int NCR53c7x0_release(struct Scsi_Host *host) {
    shutdown (host);
/* FIXME : need to recursively free tpnt structure */
    if (host->irq != IRQ_NONE)
	{
	    int irq_count;
	    struct Scsi_Host *tmp;
	    for (irq_count = 0, tmp = first_host; tmp; tmp = tmp->next)
		if (tmp->hostt == the_template && tmp->irq == host->irq)
		    ++irq_count;
	    if (irq_count == 1)
		free_irq(host->irq);
	}
    if (host->dma_channel != DMA_NONE)
	free_dma(host->dma_channel);
    return 1;
}
Scsi_Host_Template driver_template = NCR53c7xx;
#include "scsi_module.c"
#endif /* def MODULE */

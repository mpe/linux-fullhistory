/*
 * Sun3 SCSI stuff by Erik Verbruggen (erik@bigmama.xtdnet.nl)
 *
 * Adapted from mac_scsinew.c:
 */
/*
 * Generic Macintosh NCR5380 driver
 *
 * Copyright 1998, Michael Schmitz <mschmitz@lbl.gov>
 *
 * derived in part from:
 */
/*
 * Generic Generic NCR5380 driver
 *
 * Copyright 1995, Russell King
 *
 * ALPHA RELEASE 1.
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */


/*
 * This is from mac_scsi.h, but hey, maybe this is usefull for Sun3 too! :)
 *
 * Options :
 *
 * PARITY - enable parity checking.  Not supported.
 *
 * SCSI2 - enable support for SCSI-II tagged queueing.  Untested.
 *
 * USLEEP - enable support for devices that don't disconnect.  Untested.
 */

/*
 * $Log: mac_NCR5380.c,v $
 */

#define AUTOSENSE
#if 0
#define PSEUDO_DMA
#endif

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/ctype.h>
#include <linux/delay.h>

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blk.h>

#include <asm/io.h>
#include <asm/system.h>

#include <asm/sun3ints.h>

#include "scsi.h"
#include "hosts.h"
#include "sun3_scsi.h"
#include "NCR5380.h"
#include "constants.h"

#if 0
#define NDEBUG (NDEBUG_INTR | NDEBUG_PSEUDO_DMA | NDEBUG_ARBITRATION | NDEBUG_SELECTION | NDEBUG_RESELECTION)
#define NCR_TIMEOUT 100
#else
#define NDEBUG (NDEBUG_ABORT)
#endif

#define USE_WRAPPER
#define RESET_BOOT
#define DRIVER_SETUP

/*
 * BUG can be used to trigger a strange code-size related hang on 2.1 kernels
 */
#ifdef BUG
#undef RESET_BOOT
#undef DRIVER_SETUP
#endif

#define	ENABLE_IRQ()	sun3_enable_irq( IRQ_SUN3_SCSI ); 
#define	DISABLE_IRQ()	sun3_enable_irq( IRQ_SUN3_SCSI );

/* extern void via_scsi_clear(void); */

static void scsi_sun3_intr(int irq, void *dummy, struct pt_regs *fp);
static char sun3scsi_read(struct Scsi_Host *instance, int reg);
static void sun3scsi_write(struct Scsi_Host *instance, int reg, int value);

static int setup_can_queue = -1;
static int setup_cmd_per_lun = -1;
static int setup_sg_tablesize = -1;
#ifdef SUPPORT_TAGS
static int setup_use_tagged_queuing = -1;
#endif
static int setup_hostid = -1;

static int polled_scsi_on = 0;

#define	AFTER_RESET_DELAY	(HZ/2)

static volatile unsigned char *sun3_scsi_regp = IOBASE_SUN3_SCSI;
/*
static volatile unsigned char *sun3_scsi_drq  = NULL;
static volatile unsigned char *sun3_scsi_nodrq = NULL;
*/

/*
 * Function : sun3_scsi_setup(char *str, int *ints)
 *
 * Purpose : booter command line initialization of the overrides array,
 *
 * Inputs : str - unused, ints - array of integer parameters with ints[0]
 *	equal to the number of ints.
 *
 * TODO: make it actually work!
 *
 */

void sun3_scsi_setup(char *str, int *ints) {
	printk("sun3_scsi_setup() called\n");
	setup_can_queue = -1;
	setup_cmd_per_lun = -1;
	setup_sg_tablesize = -1;
	setup_hostid = -1;
#ifdef SUPPORT_TAGS
	setup_use_tagged_queuing = -1;
#endif
	printk("sun3_scsi_setup() done\n");
}

/*
 * XXX: status debug
 */
static struct Scsi_Host *default_instance;

/*
 * Function : int sun3scsi_detect(Scsi_Host_Template * tpnt)
 *
 * Purpose : initializes mac NCR5380 driver based on the
 *	command line / compile time port and irq definitions.
 *
 * Inputs : tpnt - template for this SCSI adapter.
 *
 * Returns : 1 if a host adapter was found, 0 if not.
 *
 */
 
int sun3scsi_detect(Scsi_Host_Template * tpnt)
{
	unsigned long ioaddr, iopte;
	unsigned short *ioptr;
	int count = 0;
	static int called = 0;
	struct Scsi_Host *instance;

	if(called)
		return 0;

printk("sun3scsi_detect(0x%p)\n",tpnt);

	tpnt->proc_name = "Sun3 5380 SCSI"; /* Could you spell "ewww..."? */

	/* setup variables */
	tpnt->can_queue =
		(setup_can_queue > 0) ? setup_can_queue : CAN_QUEUE;
	tpnt->cmd_per_lun =
		(setup_cmd_per_lun > 0) ? setup_cmd_per_lun : CMD_PER_LUN;
	tpnt->sg_tablesize = 
		(setup_sg_tablesize >= 0) ? setup_sg_tablesize : SG_TABLESIZE;

	if (setup_hostid >= 0)
		tpnt->this_id = setup_hostid;
	else {
		/* use 7 as default */
		tpnt->this_id = 7;
	}

	/* Taken from Sammy's lance driver: */
        /* IOBASE_SUN3_SCSI can be found within the IO pmeg with some effort */
        for(ioaddr = 0xfe00000; ioaddr < (0xfe00000 + SUN3_PMEG_SIZE);
            ioaddr += SUN3_PTE_SIZE) {

                iopte = sun3_get_pte(ioaddr);
                if(!(iopte & SUN3_PAGE_TYPE_IO)) /* this an io page? */
                        continue;

                if(((iopte & SUN3_PAGE_PGNUM_MASK) << PAGE_SHIFT) ==
                   IOBASE_SUN3_SCSI) {
                        count = 1;
printk("Found ioaddr in pmeg\n");
                        break;
                }
        }

	if(!count) {
		printk("No Sun3 NCR5380 found!\n");
		return 0;
	}

	sun3_scsi_regp = ioaddr;

	/* doing some stuff like resetting DVMA: */
	ioptr = ioaddr;
        *(ioptr+8) = 0;
	udelay(10);
        *(ioptr+9) = 0;
	udelay(10);
        *(ioptr+12) = 0;
	udelay(10);
        *(ioptr+12) = 0x7;
	udelay(10);
	printk("SCSI status reg = %x\n", *(ioptr+12));
	udelay(10);
        *(ioptr+13) = 0;
	udelay(10);

#ifdef SUPPORT_TAGS
	if (setup_use_tagged_queuing < 0)
		setup_use_tagged_queuing = DEFAULT_USE_TAGGED_QUEUING;
#endif

	instance = scsi_register (tpnt, sizeof(struct NCR5380_hostdata));
	default_instance = instance;

/*
	if (macintosh_config->ident == MAC_MODEL_IIFX) {
		mac_scsi_regp  = via1_regp+0x8000;
		mac_scsi_drq   = via1_regp+0x6000;
		mac_scsi_nodrq = via1_regp+0x12000;
	} else {
		mac_scsi_regp  = via1_regp+0x10000;
		mac_scsi_drq   = via1_regp+0x6000;
		mac_scsi_nodrq = via1_regp+0x12000;
	}
*/

        instance->io_port = (unsigned long) ioaddr;
	instance->irq = IRQ_SUN3_SCSI;

	NCR5380_init(instance, 0);

	instance->n_io_port = 32;

        ((struct NCR5380_hostdata *)instance->hostdata)->ctrl = 0;

	if (instance->irq != IRQ_NONE)
		if (sun3_request_irq(instance->irq, sun3scsi_intr,
		                0, "Sun3SCSI-5380", NULL)) {
			printk("scsi%d: IRQ%d not free, interrupts disabled\n",
			       instance->host_no, instance->irq);
			instance->irq = IRQ_NONE;
		}

	printk("scsi%d: generic 5380 at port %lX irq", instance->host_no, instance->io_port);
	if (instance->irq == IRQ_NONE)
		printk ("s disabled");
	else
		printk (" %d", instance->irq);
	printk(" options CAN_QUEUE=%d CMD_PER_LUN=%d release=%d",
	       instance->can_queue, instance->cmd_per_lun,
	       SUN3SCSI_PUBLIC_RELEASE);
	printk("\nscsi%d:", instance->host_no);
	NCR5380_print_options(instance);
	printk("\n");

	called = 1;
	return 1;
}

int sun3scsi_release (struct Scsi_Host *shpnt)
{
	if (shpnt->irq != IRQ_NONE)
		free_irq (shpnt->irq, NULL);

	return 0;
}

#ifdef RESET_BOOT
/*
 * Our 'bus reset on boot' function
 */

static void sun3_scsi_reset_boot(struct Scsi_Host *instance)
{
	unsigned long end;

	NCR5380_local_declare();
	NCR5380_setup(instance);
	
	/*
	 * Do a SCSI reset to clean up the bus during initialization. No
	 * messing with the queues, interrupts, or locks necessary here.
	 */

	printk( "Sun3 SCSI: resetting the SCSI bus..." );

	/* switch off SCSI IRQ - catch an interrupt without IRQ bit set else */
       	sun3_disable_irq( IRQ_SUN3_SCSI );

	/* get in phase */
	NCR5380_write( TARGET_COMMAND_REG,
		      PHASE_SR_TO_TCR( NCR5380_read(STATUS_REG) ));

	/* assert RST */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_RST );

	/* The min. reset hold time is 25us, so 40us should be enough */
	udelay( 50 );

	/* reset RST and interrupt */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE );
	NCR5380_read( RESET_PARITY_INTERRUPT_REG );

	for( end = jiffies + AFTER_RESET_DELAY; jiffies < end; )
		barrier();

	/* switch on SCSI IRQ again */
       	sun3_enable_irq( IRQ_SUN3_SCSI );

	printk( " done\n" );
}
#endif

const char * sun3scsi_info (struct Scsi_Host *spnt) {
    return "";
}


/*
 * NCR 5380 register access functions
 */

static char sun3scsi_read(struct Scsi_Host *instance, int reg)
{
/*
printk("sun3scsi_read(instance=0x%p, reg=0x%x): @0x%p= %d\n",instance,reg,sun3_scsi_regp,sun3_scsi_regp[reg]);
*/
	return( sun3_scsi_regp[reg] );
}

static void sun3scsi_write(struct Scsi_Host *instance, int reg, int value)
{
/*
	printk("sun3scsi_write(instance=0x%p, reg=0x%x, value=0x%x)\n", instance, reg, value);
*/
	sun3_scsi_regp[reg] = value;
}

#include "NCR5380.c"

/*
 * Debug stuff - to be called on NMI, or sysrq key. Use at your own risk; 
 * reentering NCR5380_print_status seems to have ugly side effects
 */

void sun3_sun3_debug (void)
{
	unsigned long flags;
	NCR5380_local_declare();

	if (default_instance) {
			save_flags(flags);
			cli();
			NCR5380_print_status(default_instance);
			restore_flags(flags);
	}
#if 0
	polled_scsi_on = 1;
#endif
}
/*
 * Helper function for interrupt trouble. More ugly side effects here.
 */

void scsi_sun3_polled (void)
{
	unsigned long flags;
	NCR5380_local_declare();
	struct Scsi_Host *instance;
	
	instance = default_instance;
	NCR5380_setup(instance);
	if(NCR5380_read(BUS_AND_STATUS_REG)&BASR_IRQ)
	{
		printk("SCSI poll\n");
		save_flags(flags);
		cli();
		sun3scsi_intr(IRQ_SUN3_SCSI, instance, NULL);
		restore_flags(flags);
	}
}


#ifdef MODULE

Scsi_Host_Template driver_template = SUN3_NCR5380;

#include "scsi_module.c"
#endif

/*
 *  scsi.c Copyright (C) 1992 Drew Eckhardt
 *         Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *
 *  generic mid-level SCSI driver
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Bug correction thanks go to :
 *      Rik Faith <faith@cs.unc.edu>
 *      Tommy Thorn <tthorn>
 *      Thomas Wuensche <tw@fgb1.fgb.mw.tu-muenchen.de>
 *
 *  Modified by Eric Youngdale eric@andante.jic.com or ericy@gnu.ai.mit.edu to
 *  add scatter-gather, multiple outstanding request, and other
 *  enhancements.
 *
 *  Native multichannel, wide scsi, /proc/scsi and hot plugging
 *  support added by Michael Neuffer <mike@i-connect.net>
 *
 *  Added request_module("scsi_hostadapter") for kerneld:
 *  (Put an "alias scsi_hostadapter your_hostadapter" in /etc/conf.modules)
 *  Bjorn Ekwall  <bj0rn@blox.se>
 *  (changed to kmod)
 *
 *  Major improvements to the timeout, abort, and reset processing,
 *  as well as performance modifications for large queue depths by
 *  Leonard N. Zubkoff <lnz@dandelion.com>
 *
 *  Converted cli() code to spinlocks, Ingo Molnar
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>

#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/spinlock.h>

#include "scsi.h"
#include "hosts.h"
#include "constants.h"

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#undef USE_STATIC_SCSI_MEMORY

/*
static const char RCSid[] = "$Header: /vger/u4/cvs/linux/drivers/scsi/scsi.c,v 1.38 1997/01/19 23:07:18 davem Exp $";
*/

/*
 * Definitions and constants.
 */
#define INTERNAL_ERROR (panic ("Internal error in file %s, line %d.\n", __FILE__, __LINE__))

/*
 * PAGE_SIZE must be a multiple of the sector size (512).  True
 * for all reasonably recent architectures (even the VAX...).
 */
#define SECTOR_SIZE		512
#define SECTORS_PER_PAGE	(PAGE_SIZE/SECTOR_SIZE)

#if SECTORS_PER_PAGE <= 8
 typedef unsigned char	FreeSectorBitmap;
#elif SECTORS_PER_PAGE <= 32
 typedef unsigned int	FreeSectorBitmap;
#else
# error You lose.
#endif

#define MIN_RESET_DELAY (2*HZ)

/* Do not call reset on error if we just did a reset within 15 sec. */
#define MIN_RESET_PERIOD (15*HZ)

/* The following devices are known not to tolerate a lun != 0 scan for
 * one reason or another.  Some will respond to all luns, others will
 * lock up.
 */

#define BLIST_NOLUN     0x01
#define BLIST_FORCELUN  0x02
#define BLIST_BORKEN    0x04
#define BLIST_KEY       0x08
#define BLIST_SINGLELUN 0x10
#define BLIST_NOTQ	0x20
#define BLIST_SPARSELUN 0x40

/*
 * Data declarations.
 */
unsigned long             scsi_pid = 0;
Scsi_Cmnd               * last_cmnd = NULL;
/* Command groups 3 and 4 are reserved and should never be used.  */
const unsigned char       scsi_command_size[8] = { 6, 10, 10, 12, 
                                                   12, 12, 10, 10 };
static unsigned long      serial_number = 0;
static Scsi_Cmnd        * scsi_bh_queue_head = NULL;
static Scsi_Cmnd	* scsi_bh_queue_tail = NULL;
static FreeSectorBitmap * dma_malloc_freelist = NULL;
static int                need_isa_bounce_buffers;
static unsigned int       dma_sectors = 0;
unsigned int              scsi_dma_free_sectors = 0;
unsigned int              scsi_need_isa_buffer = 0;
static unsigned char   ** dma_malloc_pages = NULL;

/*
 * Note - the initial logging level can be set here to log events at boot time.
 * After the system is up, you may enable logging via the /proc interface.
 */
unsigned int              scsi_logging_level = 0;

volatile struct Scsi_Host * host_active = NULL;

#if CONFIG_PROC_FS
/* 
 * This is the pointer to the /proc/scsi code.
 * It is only initialized to !=0 if the scsi code is present
 */
struct proc_dir_entry proc_scsi_scsi = {
    PROC_SCSI_SCSI, 4, "scsi",
    S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0, 0,
    NULL,
    NULL, NULL,
    NULL, NULL, NULL
};
#endif


const char *const scsi_device_types[MAX_SCSI_DEVICE_CODE] =
{
    "Direct-Access    ",
    "Sequential-Access",
    "Printer          ",
    "Processor        ",
    "WORM             ",
    "CD-ROM           ",
    "Scanner          ",
    "Optical Device   ",
    "Medium Changer   ",
    "Communications   "
};

/* 
 * Function prototypes.
 */
static void resize_dma_pool(void);
static void print_inquiry(unsigned char *data);
extern void scsi_times_out (Scsi_Cmnd * SCpnt);
static int  scan_scsis_single (int channel,int dev,int lun,int * max_scsi_dev ,
                 int * sparse_lun, Scsi_Device ** SDpnt, Scsi_Cmnd * SCpnt,
                 struct Scsi_Host *shpnt, char * scsi_result);
void        scsi_build_commandblocks(Scsi_Device * SDpnt);

/*
 * These are the interface to the old error handling code.  It should go away
 * someday soon.
 */
extern void scsi_old_done (Scsi_Cmnd *SCpnt);
extern void scsi_old_times_out (Scsi_Cmnd * SCpnt);

#if CONFIG_PROC_FS
extern int (* dispatch_scsi_info_ptr)(int ino, char *buffer, char **start,
				      off_t offset, int length, int inout);
extern int dispatch_scsi_info(int ino, char *buffer, char **start,
			      off_t offset, int length, int inout);
#endif

#define SCSI_BLOCK(DEVICE, HOST)                                                \
                ((HOST->block && host_active && HOST != host_active)            \
		  || ((HOST)->can_queue && HOST->host_busy >= HOST->can_queue)    \
                  || ((HOST)->host_blocked)                                       \
                  || ((DEVICE) != NULL && (DEVICE)->device_blocked) )

static void scsi_dump_status(int level);


struct dev_info{
    const char * vendor;
    const char * model;
    const char * revision; /* Latest revision known to be bad.  Not used yet */
    unsigned flags;
};

/*
 * This is what was previously known as the blacklist.  The concept
 * has been expanded so that we can specify other types of things we
 * need to be aware of.
 */
static struct dev_info device_list[] =
{
{"Aashima","IMAGERY 2400SP","1.03",BLIST_NOLUN},/* Locks up if polled for lun != 0 */
{"CHINON","CD-ROM CDS-431","H42", BLIST_NOLUN}, /* Locks up if polled for lun != 0 */
{"CHINON","CD-ROM CDS-535","Q14", BLIST_NOLUN}, /* Locks up if polled for lun != 0 */
{"DENON","DRD-25X","V", BLIST_NOLUN},           /* Locks up if probed for lun != 0 */
{"HITACHI","DK312C","CM81", BLIST_NOLUN},       /* Responds to all lun - dtg */
{"HITACHI","DK314C","CR21" , BLIST_NOLUN},      /* responds to all lun */
{"IMS", "CDD521/10","2.06", BLIST_NOLUN},       /* Locks-up when LUN>0 polled. */
{"MAXTOR","XT-3280","PR02", BLIST_NOLUN},       /* Locks-up when LUN>0 polled. */
{"MAXTOR","XT-4380S","B3C", BLIST_NOLUN},       /* Locks-up when LUN>0 polled. */
{"MAXTOR","MXT-1240S","I1.2", BLIST_NOLUN},     /* Locks up when LUN>0 polled */
{"MAXTOR","XT-4170S","B5A", BLIST_NOLUN},       /* Locks-up sometimes when LUN>0 polled. */
{"MAXTOR","XT-8760S","B7B", BLIST_NOLUN},       /* guess what? */
{"MEDIAVIS","RENO CD-ROMX2A","2.03",BLIST_NOLUN},/*Responds to all lun */
{"MICROP", "4110", "*", BLIST_NOTQ},		/* Buggy Tagged Queuing */
{"NEC","CD-ROM DRIVE:841","1.0", BLIST_NOLUN},  /* Locks-up when LUN>0 polled. */
{"PHILIPS", "PCA80SC", "V4-2", BLIST_NOLUN},    /* Responds to all lun */
{"RODIME","RO3000S","2.33", BLIST_NOLUN},       /* Locks up if polled for lun != 0 */
{"SANYO", "CRD-250S", "1.20", BLIST_NOLUN},     /* causes failed REQUEST SENSE on lun 1
						 * for aha152x controller, which causes
						 * SCSI code to reset bus.*/
{"SEAGATE", "ST157N", "\004|j", BLIST_NOLUN},   /* causes failed REQUEST SENSE on lun 1
						 * for aha152x controller, which causes
						 * SCSI code to reset bus.*/
{"SEAGATE", "ST296","921", BLIST_NOLUN},        /* Responds to all lun */
{"SEAGATE","ST1581","6538",BLIST_NOLUN},	/* Responds to all lun */
{"SONY","CD-ROM CDU-541","4.3d", BLIST_NOLUN},
{"SONY","CD-ROM CDU-55S","1.0i", BLIST_NOLUN},
{"SONY","CD-ROM CDU-561","1.7x", BLIST_NOLUN},
{"TANDBERG","TDC 3600","U07", BLIST_NOLUN},     /* Locks up if polled for lun != 0 */
{"TEAC","CD-R55S","1.0H", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
{"TEAC","CD-ROM","1.06", BLIST_NOLUN},          /* causes failed REQUEST SENSE on lun 1
						 * for seagate controller, which causes
						 * SCSI code to reset bus.*/
{"TEXEL","CD-ROM","1.06", BLIST_NOLUN},         /* causes failed REQUEST SENSE on lun 1
						 * for seagate controller, which causes
						 * SCSI code to reset bus.*/
{"QUANTUM","LPS525S","3110", BLIST_NOLUN},      /* Locks sometimes if polled for lun != 0 */
{"QUANTUM","PD1225S","3110", BLIST_NOLUN},      /* Locks sometimes if polled for lun != 0 */
{"MEDIAVIS","CDR-H93MV","1.31", BLIST_NOLUN},   /* Locks up if polled for lun != 0 */
{"SANKYO", "CP525","6.64", BLIST_NOLUN},        /* causes failed REQ SENSE, extra reset */
{"HP", "C1750A", "3226", BLIST_NOLUN},          /* scanjet iic */
{"HP", "C1790A", "", BLIST_NOLUN},              /* scanjet iip */
{"HP", "C2500A", "", BLIST_NOLUN},              /* scanjet iicx */
{"YAMAHA", "CDR102", "1.00", BLIST_NOLUN},	/* extra reset */

/*
 * Other types of devices that have special flags.
 */
{"SONY","CD-ROM CDU-8001","*", BLIST_BORKEN},
{"TEXEL","CD-ROM","1.06", BLIST_BORKEN},
{"IOMEGA","Io20S         *F","*", BLIST_KEY},
{"INSITE","Floptical   F*8I","*", BLIST_KEY},
{"INSITE","I325VM","*", BLIST_KEY},
{"NRC","MBR-7","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"NRC","MBR-7.4","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"NAKAMICH","MJ-4.8S","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"NAKAMICH","MJ-5.16S","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"PIONEER","CD-ROM DRM-600","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"PIONEER","CD-ROM DRM-602X","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"PIONEER","CD-ROM DRM-604X","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"EMULEX","MD21/S2     ESDI","*", BLIST_SINGLELUN},
{"CANON","IPUBJD","*", BLIST_SPARSELUN},
{"nCipher","Fastness Crypto","*", BLIST_FORCELUN},
{"MATSHITA","PD","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"YAMAHA","CDR100","1.00", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
{"YAMAHA","CDR102","1.00", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
{"iomega","jaz 1GB","J.86", BLIST_NOTQ | BLIST_NOLUN},
{"IBM","DPES-","*", BLIST_NOTQ | BLIST_NOLUN},
{"WDIGTL","WDE","*", BLIST_NOTQ | BLIST_NOLUN},
/*
 * Must be at end of list...
 */
{NULL, NULL, NULL}
};

static int get_device_flags(unsigned char * response_data){
    int i = 0;
    unsigned char * pnt;
    for(i=0; 1; i++){
	if(device_list[i].vendor == NULL) return 0;
	pnt = &response_data[8];
	while(*pnt && *pnt == ' ') pnt++;
	if(memcmp(device_list[i].vendor, pnt,
		  strlen(device_list[i].vendor))) continue;
	pnt = &response_data[16];
	while(*pnt && *pnt == ' ') pnt++;
	if(memcmp(device_list[i].model, pnt,
		  strlen(device_list[i].model))) continue;
	return device_list[i].flags;
    }
    return 0;
}

/*
 * Function:    scsi_make_blocked_list
 *
 * Purpose:     Build linked list of hosts that require blocking.
 *
 * Arguments:   None.
 *
 * Returns:     Nothing
 *
 * Notes:       Blocking is sort of a hack that is used to prevent more than one
 *              host adapter from being active at one time.  This is used in cases
 *              where the ISA bus becomes unreliable if you have more than one
 *              host adapter really pumping data through.
 *
 *              We spent a lot of time examining the problem, and I *believe* that
 *              the problem is bus related as opposed to being a driver bug.
 *
 *              The blocked list is used as part of the synchronization object
 *              that we use to ensure that only one host is active at one time.
 *              I (ERY) would like to make this go away someday, but this would
 *              require that we have a recursive mutex object.
 */
void 
scsi_make_blocked_list(void)  
{
    int block_count = 0, index;
    struct Scsi_Host * sh[128], * shpnt;

    /*
     * Create a circular linked list from the scsi hosts which have
     * the "wish_block" field in the Scsi_Host structure set.
     * The blocked list should include all the scsi hosts using ISA DMA.
     * In some systems, using two dma channels simultaneously causes
     * unpredictable results.
     * Among the scsi hosts in the blocked list, only one host at a time
     * is allowed to have active commands queued. The transition from
     * one active host to the next one is allowed only when host_busy == 0
     * for the active host (which implies host_busy == 0 for all the hosts
     * in the list). Moreover for block devices the transition to a new
     * active host is allowed only when a request is completed, since a
     * block device request can be divided into multiple scsi commands
     * (when there are few sg lists or clustering is disabled).
     *
     * (DB, 4 Feb 1995)
     */

   
    host_active = NULL;

    for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next) {

#if 0
	/*
	 * Is this is a candidate for the blocked list?
	 * Useful to put into the blocked list all the hosts whose driver
	 * does not know about the host->block feature.
	 */
	if (shpnt->unchecked_isa_dma) shpnt->wish_block = 1;
#endif

	if (shpnt->wish_block) sh[block_count++] = shpnt;
    }

    if (block_count == 1) sh[0]->block = NULL;

    else if (block_count > 1) {
	
	for(index = 0; index < block_count - 1; index++) {
	    sh[index]->block = sh[index + 1];
	    printk("scsi%d : added to blocked host list.\n",
		   sh[index]->host_no);
	}

	sh[block_count - 1]->block = sh[0];
	printk("scsi%d : added to blocked host list.\n",
	       sh[index]->host_no);
    }

}

static void scan_scsis_done (Scsi_Cmnd * SCpnt)
{

    SCSI_LOG_MLCOMPLETE(1,printk ("scan_scsis_done(%p, %06x)\n", SCpnt->host, SCpnt->result));
    SCpnt->request.rq_status = RQ_SCSI_DONE;

    if (SCpnt->request.sem != NULL)
	up(SCpnt->request.sem);
}

__initfunc(void scsi_logging_setup(char *str, int *ints))
{
    if (ints[0] != 1) {
	printk("scsi_logging_setup : usage scsi_logging_level=n "
               "(n should be 0 or non-zero)\n");
    } else {
	scsi_logging_level = (ints[1])? ~0 : 0;
    }
}

#ifdef CONFIG_SCSI_MULTI_LUN
static int max_scsi_luns = 8;
#else
static int max_scsi_luns = 1;
#endif

__initfunc(void scsi_luns_setup(char *str, int *ints))
{
    if (ints[0] != 1)
	printk("scsi_luns_setup : usage max_scsi_luns=n (n should be between 1 and 8)\n");
    else
	max_scsi_luns = ints[1];
}

/*
 *  Detecting SCSI devices :
 *  We scan all present host adapter's busses,  from ID 0 to ID (max_id).
 *  We use the INQUIRY command, determine device type, and pass the ID /
 *  lun address of all sequential devices to the tape driver, all random
 *  devices to the disk driver.
 */
static void scan_scsis (struct Scsi_Host *shpnt, 
                        unchar hardcoded,
                        unchar hchannel, 
                        unchar hid, 
                        unchar hlun)
{
  int             channel;
  int             dev;
  int             lun;
  int             max_dev_lun;
  Scsi_Cmnd     * SCpnt;
  unsigned char * scsi_result;
  unsigned char   scsi_result0[256];
  Scsi_Device   * SDpnt;
  Scsi_Device   * SDtail;
  int             sparse_lun;

  SCpnt = (Scsi_Cmnd *) scsi_init_malloc (sizeof (Scsi_Cmnd), GFP_ATOMIC | GFP_DMA);
  memset (SCpnt, 0, sizeof (Scsi_Cmnd));

  SDpnt = (Scsi_Device *) scsi_init_malloc (sizeof (Scsi_Device), GFP_ATOMIC);
  memset (SDpnt, 0, sizeof (Scsi_Device));


  /* Make sure we have something that is valid for DMA purposes */
  scsi_result = ( ( !shpnt->unchecked_isa_dma )
                 ? &scsi_result0[0] : scsi_init_malloc (512, GFP_DMA));

  if (scsi_result == NULL) 
  {
      printk ("Unable to obtain scsi_result buffer\n");
      goto leave;
  }

  /*
   * We must chain ourself in the host_queue, so commands can time out 
   */
  SCpnt->next = NULL;
  SDpnt->device_queue = SCpnt;
  SDpnt->host = shpnt;
  SDpnt->online = TRUE;

  /*
   * Next, hook the device to the host in question.
   */
  SDpnt->prev = NULL;
  SDpnt->next = NULL;
  if( shpnt->host_queue != NULL )
  {
      SDtail = shpnt->host_queue;
      while( SDtail->next != NULL )
          SDtail = SDtail->next;

      SDtail->next = SDpnt;
      SDpnt->prev = SDtail;
  }
  else
  {
      shpnt->host_queue = SDpnt;
  }

  /*
   * We need to increment the counter for this one device so we can track when
   * things are quiet.
   */
  atomic_inc(&shpnt->host_active); 

  if (hardcoded == 1) {
    Scsi_Device *oldSDpnt=SDpnt;
    struct Scsi_Device_Template * sdtpnt;
    channel = hchannel;
    if(channel > shpnt->max_channel) goto leave;
    dev = hid;
    if(dev >= shpnt->max_id) goto leave;
    lun = hlun;
    if(lun >= shpnt->max_lun) goto leave;
    scan_scsis_single (channel, dev, lun, &max_dev_lun, &sparse_lun,
		       &SDpnt, SCpnt, shpnt, scsi_result);
    if(SDpnt!=oldSDpnt) {

	/* it could happen the blockdevice hasn't yet been inited */
    for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
        if(sdtpnt->init && sdtpnt->dev_noticed) (*sdtpnt->init)();

            oldSDpnt->scsi_request_fn = NULL;
            for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
                if(sdtpnt->attach) {
		  (*sdtpnt->attach)(oldSDpnt);
                  if(oldSDpnt->attached) scsi_build_commandblocks(oldSDpnt);}
	    resize_dma_pool();

        for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next) {
            if(sdtpnt->finish && sdtpnt->nr_dev)
                {(*sdtpnt->finish)();}
	}
    }

  }
  else {
    /* Actual LUN. PC ordering is 0->n IBM/spec ordering is n->0 */
    int order_dev;
    
    for (channel = 0; channel <= shpnt->max_channel; channel++) {
      for (dev = 0; dev < shpnt->max_id; ++dev) {
        if( shpnt->reverse_ordering)
        	/* Shift to scanning 15,14,13... or 7,6,5,4, */
        	order_dev = shpnt->max_id-dev-1;
        else
        	order_dev = dev;
        	
        if (shpnt->this_id != order_dev) {

          /*
           * We need the for so our continue, etc. work fine. We put this in
           * a variable so that we can override it during the scan if we
           * detect a device *KNOWN* to have multiple logical units.
           */
          max_dev_lun = (max_scsi_luns < shpnt->max_lun ?
                         max_scsi_luns : shpnt->max_lun);
	  sparse_lun = 0;
          for (lun = 0; lun < max_dev_lun; ++lun) {
            if (!scan_scsis_single (channel, order_dev, lun, &max_dev_lun,
				    &sparse_lun, &SDpnt, SCpnt, shpnt,
				    scsi_result)
		&& !sparse_lun)
              break; /* break means don't probe further for luns!=0 */
          }                     /* for lun ends */
        }                       /* if this_id != id ends */
      }                         /* for dev ends */
    }                           /* for channel ends */
  } 				/* if/else hardcoded */

  /*
   * We need to decrement the counter for this one device
   * so we know when everything is quiet.
   */
  atomic_dec(&shpnt->host_active); 

  leave:

  {/* Unchain SCpnt from host_queue */
      Scsi_Device *prev, *next;
      Scsi_Device * dqptr;
      
      for(dqptr = shpnt->host_queue; dqptr != SDpnt; dqptr = dqptr->next) 
          continue;
      if(dqptr) 
      {
          prev = dqptr->prev;
          next = dqptr->next;
          if(prev)
              prev->next = next;
          else
              shpnt->host_queue = next;
          if(next) next->prev = prev;
      }
  }

     /* Last device block does not exist.  Free memory. */
    if (SDpnt != NULL)
      scsi_init_free ((char *) SDpnt, sizeof (Scsi_Device));

    if (SCpnt != NULL)
      scsi_init_free ((char *) SCpnt, sizeof (Scsi_Cmnd));

    /* If we allocated a buffer so we could do DMA, free it now */
    if (scsi_result != &scsi_result0[0] && scsi_result != NULL)
    {
        scsi_init_free (scsi_result, 512);
    }

    {
        Scsi_Device * sdev;
        Scsi_Cmnd   * scmd;

        SCSI_LOG_SCAN_BUS(4,printk("Host status for host %p:\n", shpnt));
        for(sdev = shpnt->host_queue; sdev; sdev = sdev->next)
        {
            SCSI_LOG_SCAN_BUS(4,printk("Device %d %p: ", sdev->id, sdev));
            for(scmd=sdev->device_queue; scmd; scmd = scmd->next)
            {
                SCSI_LOG_SCAN_BUS(4,printk("%p ", scmd));
            }
            SCSI_LOG_SCAN_BUS(4,printk("\n"));
        }
    }
}

/*
 * The worker for scan_scsis.
 * Returning 0 means Please don't ask further for lun!=0, 1 means OK go on.
 * Global variables used : scsi_devices(linked list)
 */
int scan_scsis_single (int channel, int dev, int lun, int *max_dev_lun,
    int *sparse_lun, Scsi_Device **SDpnt2, Scsi_Cmnd * SCpnt,
    struct Scsi_Host * shpnt, char *scsi_result)
{
  unsigned char scsi_cmd[12];
  struct Scsi_Device_Template *sdtpnt;
  Scsi_Device * SDtail, *SDpnt=*SDpnt2;
  int bflags, type=-1;

  SDpnt->host = shpnt;
  SDpnt->id = dev;
  SDpnt->lun = lun;
  SDpnt->channel = channel;
  SDpnt->online = TRUE;

  /* Some low level driver could use device->type (DB) */
  SDpnt->type = -1;

  /*
   * Assume that the device will have handshaking problems, and then fix this
   * field later if it turns out it doesn't
   */
  SDpnt->borken = 1;
  SDpnt->was_reset = 0;
  SDpnt->expecting_cc_ua = 0;

  scsi_cmd[0] = TEST_UNIT_READY;
  scsi_cmd[1] = lun << 5;
  scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[4] = scsi_cmd[5] = 0;

  SCpnt->host = SDpnt->host;
  SCpnt->device = SDpnt;
  SCpnt->target = SDpnt->id;
  SCpnt->lun = SDpnt->lun;
  SCpnt->channel = SDpnt->channel;
  {
    struct semaphore sem = MUTEX_LOCKED;
    SCpnt->request.sem = &sem;
    SCpnt->request.rq_status = RQ_SCSI_BUSY;
    spin_lock_irq(&io_request_lock);
    scsi_do_cmd (SCpnt, (void *) scsi_cmd,
                 (void *) scsi_result,
                 256, scan_scsis_done, SCSI_TIMEOUT + 4 * HZ, 5);
    spin_unlock_irq(&io_request_lock);
    down (&sem);
    SCpnt->request.sem = NULL;
  }

  SCSI_LOG_SCAN_BUS(3,  printk ("scsi: scan_scsis_single id %d lun %d. Return code 0x%08x\n",
          dev, lun, SCpnt->result));
  SCSI_LOG_SCAN_BUS(3,print_driverbyte(SCpnt->result));
  SCSI_LOG_SCAN_BUS(3,print_hostbyte(SCpnt->result));
  SCSI_LOG_SCAN_BUS(3,printk("\n"));

  if (SCpnt->result) {
    if (((driver_byte (SCpnt->result) & DRIVER_SENSE) ||
         (status_byte (SCpnt->result) & CHECK_CONDITION)) &&
        ((SCpnt->sense_buffer[0] & 0x70) >> 4) == 7) {
      if (((SCpnt->sense_buffer[2] & 0xf) != NOT_READY) &&
          ((SCpnt->sense_buffer[2] & 0xf) != UNIT_ATTENTION) &&
          ((SCpnt->sense_buffer[2] & 0xf) != ILLEGAL_REQUEST || lun > 0))
        return 1;
    }
    else
      return 0;
  }

  SCSI_LOG_SCAN_BUS(3,printk ("scsi: performing INQUIRY\n"));
  /*
   * Build an INQUIRY command block.
   */
  scsi_cmd[0] = INQUIRY;
  scsi_cmd[1] = (lun << 5) & 0xe0;
  scsi_cmd[2] = 0;
  scsi_cmd[3] = 0;
  scsi_cmd[4] = 255;
  scsi_cmd[5] = 0;
  SCpnt->cmd_len = 0;
  {
    struct semaphore sem = MUTEX_LOCKED;
    SCpnt->request.sem = &sem;
    SCpnt->request.rq_status = RQ_SCSI_BUSY;
    spin_lock_irq(&io_request_lock);
    scsi_do_cmd (SCpnt, (void *) scsi_cmd,
                 (void *) scsi_result,
                 256, scan_scsis_done, SCSI_TIMEOUT, 3);
    spin_unlock_irq(&io_request_lock);
    down (&sem);
    SCpnt->request.sem = NULL;
  }

  SCSI_LOG_SCAN_BUS(3,printk ("scsi: INQUIRY %s with code 0x%x\n",
                              SCpnt->result ? "failed" : "successful", SCpnt->result));

  if (SCpnt->result)
    return 0;     /* assume no peripheral if any sort of error */

  /*
   * Check the peripheral qualifier field - this tells us whether LUNS
   * are supported here or not.
   */
  if( (scsi_result[0] >> 5) == 3 )
    {
      return 0;     /* assume no peripheral if any sort of error */
    }

  /*
   * It would seem some TOSHIBA CDROM gets things wrong
   */
  if (!strncmp (scsi_result + 8, "TOSHIBA", 7) &&
      !strncmp (scsi_result + 16, "CD-ROM", 6) &&
      scsi_result[0] == TYPE_DISK) {
    scsi_result[0] = TYPE_ROM;
    scsi_result[1] |= 0x80;     /* removable */
  }

  memcpy (SDpnt->vendor, scsi_result + 8, 8);
  memcpy (SDpnt->model, scsi_result + 16, 16);
  memcpy (SDpnt->rev, scsi_result + 32, 4);

  SDpnt->removable = (0x80 & scsi_result[1]) >> 7;
  SDpnt->online = TRUE;
  SDpnt->lockable = SDpnt->removable;
  SDpnt->changed = 0;
  SDpnt->access_count = 0;
  SDpnt->busy = 0;
  SDpnt->has_cmdblocks = 0;
  /*
   * Currently, all sequential devices are assumed to be tapes, all random
   * devices disk, with the appropriate read only flags set for ROM / WORM
   * treated as RO.
   */
  switch (type = (scsi_result[0] & 0x1f)) {
  case TYPE_TAPE:
  case TYPE_DISK:
  case TYPE_MOD:
  case TYPE_PROCESSOR:
  case TYPE_SCANNER:
  case TYPE_MEDIUM_CHANGER:
    SDpnt->writeable = 1;
    break;
  case TYPE_WORM:
  case TYPE_ROM:
    SDpnt->writeable = 0;
    break;
  default:
    printk ("scsi: unknown type %d\n", type);
  }

  SDpnt->device_blocked = FALSE;
  SDpnt->device_busy = 0;
  SDpnt->single_lun = 0;
  SDpnt->soft_reset =
    (scsi_result[7] & 1) && ((scsi_result[3] & 7) == 2);
  SDpnt->random = (type == TYPE_TAPE) ? 0 : 1;
  SDpnt->type = (type & 0x1f);

  print_inquiry (scsi_result);

  for (sdtpnt = scsi_devicelist; sdtpnt;
       sdtpnt = sdtpnt->next)
    if (sdtpnt->detect)
      SDpnt->attached +=
        (*sdtpnt->detect) (SDpnt);

  SDpnt->scsi_level = scsi_result[2] & 0x07;
  if (SDpnt->scsi_level >= 2 ||
      (SDpnt->scsi_level == 1 &&
       (scsi_result[3] & 0x0f) == 1))
    SDpnt->scsi_level++;

  /*
   * Accommodate drivers that want to sleep when they should be in a polling
   * loop.
   */
  SDpnt->disconnect = 0;

  /*
   * Get any flags for this device.
   */
  bflags = get_device_flags (scsi_result);

  /*
   * Set the tagged_queue flag for SCSI-II devices that purport to support
   * tagged queuing in the INQUIRY data.
   */
  SDpnt->tagged_queue = 0;
  if ((SDpnt->scsi_level >= SCSI_2) &&
      (scsi_result[7] & 2) &&
      !(bflags & BLIST_NOTQ)) {
    SDpnt->tagged_supported = 1;
    SDpnt->current_tag = 0;
  }

  /*
   * Some revisions of the Texel CD ROM drives have handshaking problems when
   * used with the Seagate controllers.  Before we know what type of device
   * we're talking to, we assume it's borken and then change it here if it
   * turns out that it isn't a TEXEL drive.
   */
  if ((bflags & BLIST_BORKEN) == 0)
    SDpnt->borken = 0;

  /*
   * If we want to only allow I/O to one of the luns attached to this device
   * at a time, then we set this flag.
   */
  if (bflags & BLIST_SINGLELUN)
    SDpnt->single_lun = 1;

  /*
   * These devices need this "key" to unlock the devices so we can use it
   */
  if ((bflags & BLIST_KEY) != 0) {
    printk ("Unlocked floptical drive.\n");
    SDpnt->lockable = 0;
    scsi_cmd[0] = MODE_SENSE;
    scsi_cmd[1] = (lun << 5) & 0xe0;
    scsi_cmd[2] = 0x2e;
    scsi_cmd[3] = 0;
    scsi_cmd[4] = 0x2a;
    scsi_cmd[5] = 0;
    SCpnt->cmd_len = 0;
    {
      struct semaphore sem = MUTEX_LOCKED;
      SCpnt->request.rq_status = RQ_SCSI_BUSY;
      SCpnt->request.sem = &sem;
      spin_lock_irq(&io_request_lock);
      scsi_do_cmd (SCpnt, (void *) scsi_cmd,
                   (void *) scsi_result, 0x2a,
                   scan_scsis_done, SCSI_TIMEOUT, 3);
      spin_unlock_irq(&io_request_lock);
      down (&sem);
      SCpnt->request.sem = NULL;
    }
  }

  /*
   * Detach the command from the device. It was just a temporary to be used while
   * scanning the bus - the real ones will be allocated later.
   */
  SDpnt->device_queue = NULL;

  /*
   * This device was already hooked up to the host in question,
   * so at this point we just let go of it and it should be fine.  We do need to
   * allocate a new one and attach it to the host so that we can further scan the bus.
   */
  SDpnt = (Scsi_Device *) scsi_init_malloc (sizeof (Scsi_Device), GFP_ATOMIC);
  *SDpnt2=SDpnt;
  if (!SDpnt)
  {
      printk ("scsi: scan_scsis_single: Cannot malloc\n");
      return 0;
  }

  memset (SDpnt, 0, sizeof (Scsi_Device));

  /*
   * And hook up our command block to the new device we will be testing
   * for.
   */
  SDpnt->device_queue = SCpnt;
  SDpnt->online = TRUE;

  /*
   * Since we just found one device, there had damn well better be one in the list
   * already.
   */
  if( shpnt->host_queue == NULL )
      panic("scan_scsis_single: Host queue == NULL\n");

  SDtail = shpnt->host_queue;
  while (SDtail->next)
  {
      SDtail = SDtail->next;
  }

  /* Add this device to the linked list at the end */
  SDtail->next = SDpnt;
  SDpnt->prev = SDtail;
  SDpnt->next = NULL;

  /*
   * Some scsi devices cannot be polled for lun != 0 due to firmware bugs
   */
  if (bflags & BLIST_NOLUN)
    return 0;                   /* break; */

  /*
   * If this device is known to support sparse multiple units, override the
   * other settings, and scan all of them.
   */
  if (bflags & BLIST_SPARSELUN) {
    *max_dev_lun = 8;
    *sparse_lun = 1;
    return 1;
  }

  /*
   * If this device is known to support multiple units, override the other
   * settings, and scan all of them.
   */
  if (bflags & BLIST_FORCELUN) {
    *max_dev_lun = 8;
    return 1;
  }
  /*
   * We assume the device can't handle lun!=0 if: - it reports scsi-0 (ANSI
   * SCSI Revision 0) (old drives like MAXTOR XT-3280) or - it reports scsi-1
   * (ANSI SCSI Revision 1) and Response Data Format 0
   */
  if (((scsi_result[2] & 0x07) == 0)
      ||
      ((scsi_result[2] & 0x07) == 1 &&
       (scsi_result[3] & 0x0f) == 0))
    return 0;
  return 1;
}

/*
 *  Flag bits for the internal_timeout array
 */
#define NORMAL_TIMEOUT 0
#define IN_ABORT  1
#define IN_RESET  2
#define IN_RESET2 4
#define IN_RESET3 8


/* This function takes a quick look at a request, and decides if it
 * can be queued now, or if there would be a stall while waiting for
 * something else to finish.  This routine assumes that interrupts are
 * turned off when entering the routine.  It is the responsibility
 * of the calling code to ensure that this is the case.
 */

Scsi_Cmnd * scsi_request_queueable (struct request * req, Scsi_Device * device)
{
    Scsi_Cmnd * SCpnt = NULL;
    int tablesize;
    Scsi_Cmnd * found = NULL;
    struct buffer_head * bh, *bhp;

    if (!device)
	panic ("No device passed to scsi_request_queueable().\n");

    if (req && req->rq_status == RQ_INACTIVE)
	panic("Inactive in scsi_request_queueable");

    /*
     * Look for a free command block.  If we have been instructed not to queue
     * multiple commands to multi-lun devices, then check to see what else is
     * going for this device first.
     */

    if (!device->single_lun) {
	SCpnt = device->device_queue;
	while(SCpnt){
	    if(SCpnt->request.rq_status == RQ_INACTIVE) break;
	    SCpnt = SCpnt->next;
	}
    } else {
	SCpnt = device->device_queue;
	while(SCpnt){
	    if(SCpnt->channel == device->channel
                && SCpnt->target == device->id) {
		if (SCpnt->lun == device->lun) {
		    if(found == NULL
		       && SCpnt->request.rq_status == RQ_INACTIVE)
		    {
			found=SCpnt;
		    }
		}
		if(SCpnt->request.rq_status != RQ_INACTIVE) {
		    /*
		     * I think that we should really limit things to one
		     * outstanding command per device - this is what tends
                     * to trip up buggy firmware.
		     */
		    return NULL;
		}
	    }
	    SCpnt = SCpnt->next;
	}
	SCpnt = found;
    }

    if (!SCpnt) return NULL;

    if (SCSI_BLOCK(device, device->host)) return NULL;

    if (req) {
	memcpy(&SCpnt->request, req, sizeof(struct request));
	tablesize = device->host->sg_tablesize;
	bhp = bh = req->bh;
	if(!tablesize) bh = NULL;
	/* Take a quick look through the table to see how big it is.
	 * We already have our copy of req, so we can mess with that
	 * if we want to.
	 */
	while(req->nr_sectors && bh){
	    bhp = bhp->b_reqnext;
	    if(!bhp || !CONTIGUOUS_BUFFERS(bh,bhp)) tablesize--;
	    req->nr_sectors -= bh->b_size >> 9;
	    req->sector += bh->b_size >> 9;
	    if(!tablesize) break;
	    bh = bhp;
	}
	if(req->nr_sectors && bh && bh->b_reqnext){  /* Any leftovers? */
	    SCpnt->request.bhtail = bh;
	    req->bh = bh->b_reqnext; /* Divide request */
	    bh->b_reqnext = NULL;
	    bh = req->bh;

	    /* Now reset things so that req looks OK */
	    SCpnt->request.nr_sectors -= req->nr_sectors;
	    req->current_nr_sectors = bh->b_size >> 9;
	    req->buffer = bh->b_data;
	    SCpnt->request.sem = NULL; /* Wait until whole thing done */
	} else {
	    req->rq_status = RQ_INACTIVE;
	    wake_up(&wait_for_request);
	}
    } else {
	SCpnt->request.rq_status = RQ_SCSI_BUSY;  /* Busy, but no request */
	SCpnt->request.sem = NULL;   /* And no one is waiting for the device
				      * either */
    }

    atomic_inc(&SCpnt->host->host_active); 
    SCSI_LOG_MLQUEUE(5, printk("Activating command for device %d (%d)\n", SCpnt->target,
                               atomic_read(&SCpnt->host->host_active)));
    SCpnt->use_sg = 0;               /* Reset the scatter-gather flag */
    SCpnt->old_use_sg  = 0;
    SCpnt->transfersize = 0;
    SCpnt->underflow = 0;
    SCpnt->cmd_len = 0;

/* Since not everyone seems to set the device info correctly
 * before Scsi_Cmnd gets send out to scsi_do_command, we do it here.
 */
    SCpnt->channel = device->channel;
    SCpnt->lun = device->lun;
    SCpnt->target = device->id;
    SCpnt->state = SCSI_STATE_INITIALIZING;
    SCpnt->owner = SCSI_OWNER_HIGHLEVEL;

    return SCpnt;
}

/* This function returns a structure pointer that will be valid for
 * the device.  The wait parameter tells us whether we should wait for
 * the unit to become free or not.  We are also able to tell this routine
 * not to return a descriptor if the host is unable to accept any more
 * commands for the time being.  We need to keep in mind that there is no
 * guarantee that the host remain not busy.  Keep in mind the
 * scsi_request_queueable function also knows the internal allocation scheme
 * of the packets for each device
 */

Scsi_Cmnd * scsi_allocate_device (struct request ** reqp, Scsi_Device * device,
			     int wait)
{
    kdev_t dev;
    struct request * req = NULL;
    int tablesize;
    struct buffer_head * bh, *bhp;
    struct Scsi_Host * host;
    Scsi_Cmnd * SCpnt = NULL;
    Scsi_Cmnd * SCwait = NULL;
    Scsi_Cmnd * found = NULL;

    if (!device)
	panic ("No device passed to scsi_allocate_device().\n");

    if (reqp) req = *reqp;

    /* See if this request has already been queued by an interrupt routine */
    if (req) {
	if(req->rq_status == RQ_INACTIVE) return NULL;
	dev = req->rq_dev;
    } else
        dev = 0;		/* unused */

    host = device->host;

    if (in_interrupt() && SCSI_BLOCK(device, host)) return NULL;

    while (1==1){
	if (!device->single_lun) {
	    SCpnt = device->device_queue;
	    while(SCpnt){
		SCwait = SCpnt;
		if(SCpnt->request.rq_status == RQ_INACTIVE) break;
		SCpnt = SCpnt->next;
	    }
	} else {
	    SCpnt = device->device_queue;
	    while(SCpnt){
		if(SCpnt->channel == device->channel
                   && SCpnt->target == device->id) {
		    if (SCpnt->lun == device->lun) {
			SCwait = SCpnt;
			if(found == NULL
			   && SCpnt->request.rq_status == RQ_INACTIVE)
			{
			    found=SCpnt;
			}
		    }
		    if(SCpnt->request.rq_status != RQ_INACTIVE) {
			/*
			 * I think that we should really limit things to one
			 * outstanding command per device - this is what tends
                         * to trip up buggy firmware.
			 */
			found = NULL;
			break;
		    }
		}
		SCpnt = SCpnt->next;
	    }
	    SCpnt = found;
	}

	/* See if this request has already been queued by an interrupt routine
	 */
	if (req && (req->rq_status == RQ_INACTIVE || req->rq_dev != dev)) {
	    return NULL;
	}
	if (!SCpnt || SCpnt->request.rq_status != RQ_INACTIVE)	/* Might have changed */
	{
		if (wait && SCwait && SCwait->request.rq_status != RQ_INACTIVE){
			spin_unlock(&io_request_lock);		/* FIXME!!!! */
 			sleep_on(&device->device_wait);
 			spin_lock_irq(&io_request_lock);	/* FIXME!!!! */
	 	} else {
	 		if (!wait) return NULL;
 			if (!SCwait) {
	 			printk("Attempt to allocate device channel %d,"
                                       " target %d, lun %d\n", device->channel,
                                       device->id, device->lun);
 				panic("No device found in scsi_allocate_device\n");
	 		}
 		}
	} else {
	    if (req) {
		memcpy(&SCpnt->request, req, sizeof(struct request));
		tablesize = device->host->sg_tablesize;
		bhp = bh = req->bh;
		if(!tablesize) bh = NULL;
		/* Take a quick look through the table to see how big it is.
		 * We already have our copy of req, so we can mess with that
		 * if we want to.
		 */
		while(req->nr_sectors && bh){
		    bhp = bhp->b_reqnext;
		    if(!bhp || !CONTIGUOUS_BUFFERS(bh,bhp)) tablesize--;
		    req->nr_sectors -= bh->b_size >> 9;
		    req->sector += bh->b_size >> 9;
		    if(!tablesize) break;
		    bh = bhp;
		}
		if(req->nr_sectors && bh && bh->b_reqnext){/* Any leftovers? */
		    SCpnt->request.bhtail = bh;
		    req->bh = bh->b_reqnext; /* Divide request */
		    bh->b_reqnext = NULL;
		    bh = req->bh;
		    /* Now reset things so that req looks OK */
		    SCpnt->request.nr_sectors -= req->nr_sectors;
		    req->current_nr_sectors = bh->b_size >> 9;
		    req->buffer = bh->b_data;
		    SCpnt->request.sem = NULL; /* Wait until whole thing done*/
		}
		else
		{
		    req->rq_status = RQ_INACTIVE;
		    *reqp = req->next;
		    wake_up(&wait_for_request);
		}
	    } else {
		SCpnt->request.rq_status = RQ_SCSI_BUSY;
		SCpnt->request.sem = NULL;   /* And no one is waiting for this
					      * to complete */
	    }
            atomic_inc(&SCpnt->host->host_active); 
            SCSI_LOG_MLQUEUE(5, printk("Activating command for device %d (%d)\n", 
                                       SCpnt->target,
                                       atomic_read(&SCpnt->host->host_active)));
	    break;
	}
    }

    SCpnt->use_sg = 0;            /* Reset the scatter-gather flag */
    SCpnt->old_use_sg  = 0;
    SCpnt->transfersize = 0;      /* No default transfer size */
    SCpnt->cmd_len = 0;

    SCpnt->underflow = 0;         /* Do not flag underflow conditions */

    /* Since not everyone seems to set the device info correctly
     * before Scsi_Cmnd gets send out to scsi_do_command, we do it here.
     * FIXME(eric) This doesn't make any sense.
     */
    SCpnt->channel = device->channel;
    SCpnt->lun = device->lun;
    SCpnt->target = device->id;
    SCpnt->state = SCSI_STATE_INITIALIZING;
    SCpnt->owner = SCSI_OWNER_HIGHLEVEL;

    return SCpnt;
}

/*
 * Function:    scsi_release_command
 *
 * Purpose:     Release a command block.
 *
 * Arguments:   SCpnt - command block we are releasing.
 *
 * Notes:       The command block can no longer be used by the caller once
 *              this funciton is called.  This is in effect the inverse
 *              of scsi_allocate_device/scsi_request_queueable.
 */
void
scsi_release_command(Scsi_Cmnd * SCpnt)
{
  SCpnt->request.rq_status = RQ_INACTIVE;
  SCpnt->state = SCSI_STATE_UNUSED;
  SCpnt->owner = SCSI_OWNER_NOBODY;
  atomic_dec(&SCpnt->host->host_active); 

  SCSI_LOG_MLQUEUE(5, printk("Deactivating command for device %d (active=%d, failed=%d)\n", 
                             SCpnt->target,
                             atomic_read(&SCpnt->host->host_active),
                             SCpnt->host->host_failed));
  if( SCpnt->host->host_failed != 0 )
    {
      SCSI_LOG_ERROR_RECOVERY(5, printk("Error handler thread %d %d\n", 
                                 SCpnt->host->in_recovery,
                                 SCpnt->host->eh_active));
    }

  /*
   * If the host is having troubles, then look to see if this was the last
   * command that might have failed.  If so, wake up the error handler.
   */
  if( SCpnt->host->in_recovery
      && !SCpnt->host->eh_active
      && SCpnt->host->host_busy == SCpnt->host->host_failed )
  {
      SCSI_LOG_ERROR_RECOVERY(5, printk("Waking error handler thread (%d)\n",
                                 atomic_read(&SCpnt->host->eh_wait->count)));
      up(SCpnt->host->eh_wait);
  }
}

/*
 * This is inline because we have stack problemes if we recurse to deeply.
 */

inline int internal_cmnd (Scsi_Cmnd * SCpnt)
{
#ifdef DEBUG_DELAY
    unsigned long clock;
#endif
    struct Scsi_Host   * host;
    int                  rtn = 0;
    unsigned long        timeout;

#if DEBUG
    unsigned long *ret = 0;
#ifdef __mips__
    __asm__ __volatile__ ("move\t%0,$31":"=r"(ret));
#else
   ret =  __builtin_return_address(0);
#endif
#endif

    host = SCpnt->host;

    /* Assign a unique nonzero serial_number. */
    if (++serial_number == 0) serial_number = 1;
    SCpnt->serial_number = serial_number;

    /*
     * We will wait MIN_RESET_DELAY clock ticks after the last reset so
     * we can avoid the drive not being ready.
     */
    timeout = host->last_reset + MIN_RESET_DELAY;

    if (host->resetting && time_before(jiffies, timeout)) {
	int ticks_remaining = timeout - jiffies;
	/*
	 * NOTE: This may be executed from within an interrupt
	 * handler!  This is bad, but for now, it'll do.  The irq
	 * level of the interrupt handler has been masked out by the
	 * platform dependent interrupt handling code already, so the
	 * sti() here will not cause another call to the SCSI host's
	 * interrupt handler (assuming there is one irq-level per
	 * host).
	 */
	spin_unlock_irq(&io_request_lock);
	while (--ticks_remaining >= 0) mdelay(1+999/HZ);
	host->resetting = 0;
	spin_lock_irq(&io_request_lock);
    }

    if( host->hostt->use_new_eh_code )
      {
        scsi_add_timer(SCpnt, SCpnt->timeout_per_command, scsi_times_out);
      }
    else
      {
        scsi_add_timer(SCpnt, SCpnt->timeout_per_command, 
                            scsi_old_times_out);
      }

    /*
     * We will use a queued command if possible, otherwise we will emulate the
     * queuing and calling of completion function ourselves.
     */
    SCSI_LOG_MLQUEUE(3,printk("internal_cmnd (host = %d, channel = %d, target = %d, "
	   "command = %p, buffer = %p, \nbufflen = %d, done = %p)\n",
	   SCpnt->host->host_no, SCpnt->channel, SCpnt->target, SCpnt->cmnd,
	   SCpnt->buffer, SCpnt->bufflen, SCpnt->done));

    SCpnt->state = SCSI_STATE_QUEUED;
    SCpnt->owner = SCSI_OWNER_LOWLEVEL;
    if (host->can_queue)
    {
	SCSI_LOG_MLQUEUE(3,printk("queuecommand : routine at %p\n",
                                  host->hostt->queuecommand));
        /*
         * Use the old error handling code if we haven't converted the driver
         * to use the new one yet.  Note - only the new queuecommand variant
         * passes a meaningful return value.
         */
        if( host->hostt->use_new_eh_code )
          {
            rtn = host->hostt->queuecommand (SCpnt, scsi_done);
            if( rtn != 0 )
            {
                scsi_mlqueue_insert(SCpnt, SCSI_MLQUEUE_HOST_BUSY);
            }
          }
        else
          {
            host->hostt->queuecommand (SCpnt, scsi_old_done);
          }
    }
    else
    {
	int temp;

	SCSI_LOG_MLQUEUE(3,printk("command() :  routine at %p\n", host->hostt->command));
	temp = host->hostt->command (SCpnt);
	SCpnt->result = temp;
#ifdef DEBUG_DELAY
	clock = jiffies + 4 * HZ;
	spin_unlock_irq(&io_request_lock);
	while (time_before(jiffies, clock)) barrier();
	spin_lock_irq(&io_request_lock);
	printk("done(host = %d, result = %04x) : routine at %p\n",
	       host->host_no, temp, host->hostt->command);
#endif
        if( host->hostt->use_new_eh_code )
          {
            scsi_done(SCpnt);
          }
        else
          {
            scsi_old_done(SCpnt);
          }
    }
    SCSI_LOG_MLQUEUE(3,printk("leaving internal_cmnd()\n"));
    return rtn;
}

/*
 * scsi_do_cmd sends all the commands out to the low-level driver.  It
 * handles the specifics required for each low level driver - ie queued
 * or non queued.  It also prevents conflicts when different high level
 * drivers go for the same host at the same time.
 */

void scsi_do_cmd (Scsi_Cmnd * SCpnt, const void *cmnd ,
		  void *buffer, unsigned bufflen, void (*done)(Scsi_Cmnd *),
		  int timeout, int retries)
{
    struct Scsi_Host * host = SCpnt->host;
    Scsi_Device      * device = SCpnt->device;

    SCpnt->owner = SCSI_OWNER_MIDLEVEL;

SCSI_LOG_MLQUEUE(4,
    {
	int i;
	int target = SCpnt->target;
	printk ("scsi_do_cmd (host = %d, channel = %d target = %d, "
		"buffer =%p, bufflen = %d, done = %p, timeout = %d, "
		"retries = %d)\n"
		"command : " , host->host_no, SCpnt->channel, target, buffer,
		bufflen, done, timeout, retries);
	for (i = 0; i < 10; ++i)
	    printk ("%02x  ", ((unsigned char *) cmnd)[i]);
	printk("\n");
    });

    if (!host)
    {
	panic ("Invalid or not present host.\n");
    }


    /*
     * We must prevent reentrancy to the lowlevel host driver.  This prevents
     * it - we enter a loop until the host we want to talk to is not busy.
     * Race conditions are prevented, as interrupts are disabled in between the
     * time we check for the host being not busy, and the time we mark it busy
     * ourselves.
     */

    SCpnt->pid = scsi_pid++;

    while (SCSI_BLOCK((Scsi_Device *) NULL, host)) {
    	spin_unlock(&io_request_lock);	/* FIXME!!! */
	SCSI_SLEEP(&host->host_wait, SCSI_BLOCK((Scsi_Device *) NULL, host));
    	spin_lock_irq(&io_request_lock);	/* FIXME!!! */
    }

    if (host->block) host_active = host;

    host->host_busy++;
    device->device_busy++;

    /*
     * Our own function scsi_done (which marks the host as not busy, disables
     * the timeout counter, etc) will be called by us or by the
     * scsi_hosts[host].queuecommand() function needs to also call
     * the completion function for the high level driver.
     */

    memcpy ((void *) SCpnt->data_cmnd , (const void *) cmnd, 12);
    SCpnt->reset_chain = NULL;
    SCpnt->serial_number = 0;
    SCpnt->serial_number_at_timeout = 0;
    SCpnt->bufflen = bufflen;
    SCpnt->buffer = buffer;
    SCpnt->flags = 0;
    SCpnt->retries = 0;
    SCpnt->allowed = retries;
    SCpnt->done = done;
    SCpnt->timeout_per_command = timeout;

    memcpy ((void *) SCpnt->cmnd , (const void *) cmnd, 12);
    /* Zero the sense buffer.  Some host adapters automatically request
     * sense on error.  0 is not a valid sense code.
     */
    memset ((void *) SCpnt->sense_buffer, 0, sizeof SCpnt->sense_buffer);
    SCpnt->request_buffer = buffer;
    SCpnt->request_bufflen = bufflen;
    SCpnt->old_use_sg = SCpnt->use_sg;
    if (SCpnt->cmd_len == 0)
	SCpnt->cmd_len = COMMAND_SIZE(SCpnt->cmnd[0]);
    SCpnt->old_cmd_len = SCpnt->cmd_len;

    /* Start the timer ticking.  */

    SCpnt->internal_timeout = NORMAL_TIMEOUT;
    SCpnt->abort_reason = 0;
    SCpnt->result = 0;
    internal_cmnd (SCpnt);

    SCSI_LOG_MLQUEUE(3,printk ("Leaving scsi_do_cmd()\n"));
}

/* This function is the mid-level interrupt routine, which decides how
 *  to handle error conditions.  Each invocation of this function must
 *  do one and *only* one of the following:
 *
 *      1) Insert command in BH queue.
 *      2) Activate error handler for host.
 *
 * FIXME(eric) - I am concerned about stack overflow (still).  An interrupt could
 * come while we are processing the bottom queue, which would cause another command
 * to be stuffed onto the bottom queue, and it would in turn be processed as that
 * interrupt handler is returning.  Given a sufficiently steady rate of returning
 * commands, this could cause the stack to overflow.  I am not sure what is the most
 * appropriate solution here - we should probably keep a depth count, and not process
 * any commands while we still have a bottom handler active higher in the stack.
 *
 * There is currently code in the bottom half handler to monitor recursion in the bottom
 * handler and report if it ever happens.  If this becomes a problem, it won't be hard to
 * engineer something to deal with it so that only the outer layer ever does any real
 * processing.
 */
void
scsi_done (Scsi_Cmnd * SCpnt)
{

  /*
   * We don't have to worry about this one timing out any more.
   */
  scsi_delete_timer(SCpnt);

  /* Set the serial numbers back to zero */
  SCpnt->serial_number = 0;

  /*
   * First, see whether this command already timed out.  If so, we ignore
   * the response.  We treat it as if the command never finished.
   *
   * Since serial_number is now 0, the error handler cound detect this
   * situation and avoid to call the the low level driver abort routine.
   * (DB)
   */
  if( SCpnt->state == SCSI_STATE_TIMEOUT )
    {
      SCSI_LOG_MLCOMPLETE(1,printk("Ignoring completion of %p due to timeout status", SCpnt));
      return;
    }

  SCpnt->serial_number_at_timeout = 0;
  SCpnt->state = SCSI_STATE_BHQUEUE;
  SCpnt->owner = SCSI_OWNER_BH_HANDLER;
  SCpnt->bh_next = NULL;

  /*
   * Next, put this command in the BH queue.
   * 
   * We need a spinlock here, or compare and exchange if we can reorder incoming
   * Scsi_Cmnds, as it happens pretty often scsi_done is called multiple times
   * before bh is serviced. -jj
   *
   * We already have the io_request_lock here, since we are called from the
   * interrupt handler or the error handler. (DB)
   *
   */
  if (!scsi_bh_queue_head) {
  	scsi_bh_queue_head = SCpnt;
  	scsi_bh_queue_tail = SCpnt;
  } else {
  	scsi_bh_queue_tail->bh_next = SCpnt;
  	scsi_bh_queue_tail = SCpnt;
  }

  /*
   * Mark the bottom half handler to be run.
   */
  mark_bh(SCSI_BH);
}

/*
 * Procedure:   scsi_bottom_half_handler
 *
 * Purpose:     Called after we have finished processing interrupts, it
 *              performs post-interrupt handling for commands that may
 *              have completed.
 *
 * Notes:       This is called with all interrupts enabled.  This should reduce
 *              interrupt latency, stack depth, and reentrancy of the low-level
 *              drivers.
 *
 * The io_request_lock is required in all the routine. There was a subtle
 * race condition when scsi_done is called after a command has already
 * timed out but before the time out is processed by the error handler.
 * (DB)
 */
void scsi_bottom_half_handler(void)
{
  Scsi_Cmnd        * SCpnt;
  Scsi_Cmnd        * SCnext;
  unsigned long      flags;
  
  spin_lock_irqsave(&io_request_lock, flags);

  while(1==1)
  {
      SCpnt = scsi_bh_queue_head;
      scsi_bh_queue_head = NULL;
      
      if( SCpnt == NULL ) {
          spin_unlock_irqrestore(&io_request_lock, flags);
          return;
          }
      
      SCnext = SCpnt->bh_next;
      
      for(; SCpnt; SCpnt = SCnext)
      {
          SCnext = SCpnt->bh_next;
          
          switch( scsi_decide_disposition(SCpnt) )
          {
          case SUCCESS:
              /*
               * Add to BH queue.
               */
              SCSI_LOG_MLCOMPLETE(3,printk("Command finished %d %d 0x%x\n", SCpnt->host->host_busy,
                     SCpnt->host->host_failed,
                     SCpnt->result));
              
              scsi_finish_command(SCpnt);
              break;
          case NEEDS_RETRY:
              /*
               * We only come in here if we want to retry a command.  The
               * test to see whether the command should be retried should be
               * keeping track of the number of tries, so we don't end up looping,
               * of course.
               */
              SCSI_LOG_MLCOMPLETE(3,printk("Command needs retry %d %d 0x%x\n", SCpnt->host->host_busy,
                     SCpnt->host->host_failed, SCpnt->result));
              
              scsi_retry_command(SCpnt);
              break;
          case ADD_TO_MLQUEUE:
              /* 
               * This typically happens for a QUEUE_FULL message -
               * typically only when the queue depth is only
               * approximate for a given device.  Adding a command
               * to the queue for the device will prevent further commands
               * from being sent to the device, so we shouldn't end up
               * with tons of things being sent down that shouldn't be.
               */
              scsi_mlqueue_insert(SCpnt, SCSI_MLQUEUE_DEVICE_BUSY);
              break;
          default:
              /*
               * Here we have a fatal error of some sort.  Turn it over to
               * the error handler.
               */
              SCSI_LOG_MLCOMPLETE(3,printk("Command failed %p %x active=%d busy=%d failed=%d\n", 
                                           SCpnt, SCpnt->result,
                                           atomic_read(&SCpnt->host->host_active),
                                           SCpnt->host->host_busy,
                                           SCpnt->host->host_failed));
              
              /*
               * Dump the sense information too.
               */
              if ((status_byte (SCpnt->result) & CHECK_CONDITION) != 0)
              {
                  SCSI_LOG_MLCOMPLETE(3,print_sense("bh",SCpnt));
              }


              if( SCpnt->host->eh_wait != NULL )
              {
                  SCpnt->host->host_failed++;
                  SCpnt->owner = SCSI_OWNER_ERROR_HANDLER;
                  SCpnt->state = SCSI_STATE_FAILED;
                  SCpnt->host->in_recovery = 1;
                  /*
                   * If the host is having troubles, then look to see if this was the last
                   * command that might have failed.  If so, wake up the error handler.
                   */
                  if( SCpnt->host->host_busy == SCpnt->host->host_failed )
                  {
                    SCSI_LOG_ERROR_RECOVERY(5, printk("Waking error handler thread (%d)\n",
                                                      atomic_read(&SCpnt->host->eh_wait->count)));
                      up(SCpnt->host->eh_wait);
                  }
              }
              else
              {
                  /*
                   * We only get here if the error recovery thread has died.
                   */
                  scsi_finish_command(SCpnt);
              }
          }
      } /* for(; SCpnt...) */

  } /* while(1==1) */

  spin_unlock_irqrestore(&io_request_lock, flags);

}

/*
 * Function:    scsi_retry_command
 *
 * Purpose:     Send a command back to the low level to be retried.
 *
 * Notes:       This command is always executed in the context of the
 *              bottom half handler, or the error handler thread. Low
 *              level drivers should not become re-entrant as a result of
 *              this.
 */
int
scsi_retry_command(Scsi_Cmnd * SCpnt)
{
  memcpy ((void *) SCpnt->cmnd,  (void*) SCpnt->data_cmnd,
          sizeof(SCpnt->data_cmnd));
  SCpnt->request_buffer = SCpnt->buffer;
  SCpnt->request_bufflen = SCpnt->bufflen;
  SCpnt->use_sg = SCpnt->old_use_sg;
  SCpnt->cmd_len = SCpnt->old_cmd_len;
  SCpnt->result = 0;
  memset ((void *) SCpnt->sense_buffer, 0, sizeof SCpnt->sense_buffer);
  return internal_cmnd (SCpnt);
}

/*
 * Function:    scsi_finish_command
 *
 * Purpose:     Pass command off to upper layer for finishing of I/O
 *              request, waking processes that are waiting on results,
 *              etc.
 */
void
scsi_finish_command(Scsi_Cmnd * SCpnt)
{
    struct Scsi_Host * host;
    Scsi_Device * device;

    host = SCpnt->host;
    device = SCpnt->device;

    host->host_busy--; /* Indicate that we are free */
    device->device_busy--; /* Decrement device usage counter. */
  
    if (host->block && host->host_busy == 0) 
    {
        host_active = NULL;
        
        /* For block devices "wake_up" is done in end_scsi_request */
        if (!SCSI_BLK_MAJOR(MAJOR(SCpnt->request.rq_dev))) {
            struct Scsi_Host * next;
            
            for (next = host->block; next != host; next = next->block)
                wake_up(&next->host_wait);
        }
        
    }
  
    /*
     * Now try and drain the mid-level queue if any commands have been
     * inserted.  Check to see whether the queue even has anything in
     * it first, as otherwise this is useless overhead.
     */
    if( SCpnt->host->pending_commands != NULL )
    {
        scsi_mlqueue_finish(SCpnt->host, SCpnt->device);
    }

    wake_up(&host->host_wait);
    
    /*
     * If we have valid sense information, then some kind of recovery
     * must have taken place.  Make a note of this.
     */
    if( scsi_sense_valid(SCpnt) )
    {
        SCpnt->result |= (DRIVER_SENSE << 24);
    }
    
    SCSI_LOG_MLCOMPLETE(3,printk("Notifying upper driver of completion for device %d %x\n",
                                     SCpnt->device->id, SCpnt->result));
    
    SCpnt->owner = SCSI_OWNER_HIGHLEVEL;
    SCpnt->state = SCSI_STATE_FINISHED;

    /* We can get here with use_sg=0, causing a panic in the upper level (DB) */
    SCpnt->use_sg = SCpnt->old_use_sg;

    SCpnt->done (SCpnt);
}

#ifdef CONFIG_MODULES
static int scsi_register_host(Scsi_Host_Template *);
static void scsi_unregister_host(Scsi_Host_Template *);
#endif

void *scsi_malloc(unsigned int len)
{
    unsigned int nbits, mask;
    int i, j;
    if(len % SECTOR_SIZE != 0 || len > PAGE_SIZE)
	return NULL;

    nbits = len >> 9;
    mask = (1 << nbits) - 1;

    for(i=0;i < dma_sectors / SECTORS_PER_PAGE; i++)
	for(j=0; j<=SECTORS_PER_PAGE - nbits; j++){
	    if ((dma_malloc_freelist[i] & (mask << j)) == 0){
		dma_malloc_freelist[i] |= (mask << j);
		scsi_dma_free_sectors -= nbits;
#ifdef DEBUG
                SCSI_LOG_MLQUEUE(3,printk("SMalloc: %d %p [From:%p]\n",len, dma_malloc_pages[i] + (j << 9)));
		printk("SMalloc: %d %p [From:%p]\n",len, dma_malloc_pages[i] + (j << 9));
#endif
		return (void *) ((unsigned long) dma_malloc_pages[i] + (j << 9));
	    }
	}
    return NULL;  /* Nope.  No more */
}

int scsi_free(void *obj, unsigned int len)
{
    unsigned int page, sector, nbits, mask;

#ifdef DEBUG
    unsigned long ret = 0;

#ifdef __mips__
    __asm__ __volatile__ ("move\t%0,$31":"=r"(ret));
#else
   ret = __builtin_return_address(0);
#endif
    printk("scsi_free %p %d\n",obj, len);
    SCSI_LOG_MLQUEUE(3,printk("SFree: %p %d\n",obj, len));
#endif

    for (page = 0; page < dma_sectors / SECTORS_PER_PAGE; page++) {
        unsigned long page_addr = (unsigned long) dma_malloc_pages[page];
        if ((unsigned long) obj >= page_addr &&
	    (unsigned long) obj <  page_addr + PAGE_SIZE)
	{
	    sector = (((unsigned long) obj) - page_addr) >> 9;

            nbits = len >> 9;
            mask = (1 << nbits) - 1;

            if ((mask << sector) >= (1 << SECTORS_PER_PAGE))
                panic ("scsi_free:Bad memory alignment");

            if((dma_malloc_freelist[page] &
                (mask << sector)) != (mask<<sector)){
#ifdef DEBUG
		printk("scsi_free(obj=%p, len=%d) called from %08lx\n",
                       obj, len, ret);
#endif
                panic("scsi_free:Trying to free unused memory");
            }
            scsi_dma_free_sectors += nbits;
            dma_malloc_freelist[page] &= ~(mask << sector);
            return 0;
	}
    }
    panic("scsi_free:Bad offset");
}


int scsi_loadable_module_flag; /* Set after we scan builtin drivers */

void * scsi_init_malloc(unsigned int size, int gfp_mask)
{
    void * retval;

    /*
     * For buffers used by the DMA pool, we assume page aligned 
     * structures.
     */
    if ((size % PAGE_SIZE) == 0) {
	int order, a_size;
	for (order = 0, a_size = PAGE_SIZE;
             a_size < size; order++, a_size <<= 1)
            ;
        retval = (void *) __get_free_pages(gfp_mask | GFP_DMA, order);
    } else
        retval = kmalloc(size, gfp_mask);

    if (retval)
	memset(retval, 0, size);
    return retval;
}


void scsi_init_free(char * ptr, unsigned int size)
{
    /*
     * We need this special code here because the DMA pool assumes
     * page aligned data.  Besides, it is wasteful to allocate
     * page sized chunks with kmalloc.
     */
    if ((size % PAGE_SIZE) == 0) {
    	int order, a_size;

	for (order = 0, a_size = PAGE_SIZE;
	     a_size < size; order++, a_size <<= 1)
	    ;
	free_pages((unsigned long)ptr, order);
    } else
	kfree(ptr);
}

void scsi_build_commandblocks(Scsi_Device * SDpnt)
{
    struct Scsi_Host *host = SDpnt->host;
    int j;
    Scsi_Cmnd * SCpnt;

    if (SDpnt->queue_depth == 0)
        SDpnt->queue_depth = host->cmd_per_lun;
    SDpnt->device_queue = NULL;

    for(j=0;j<SDpnt->queue_depth;j++){
      SCpnt = (Scsi_Cmnd *)
              scsi_init_malloc(sizeof(Scsi_Cmnd),
                               GFP_ATOMIC |
                               (host->unchecked_isa_dma ? GFP_DMA : 0));
        memset(&SCpnt->eh_timeout, 0, sizeof(SCpnt->eh_timeout));
	SCpnt->host                      = host;
	SCpnt->device                    = SDpnt;
	SCpnt->target                    = SDpnt->id;
	SCpnt->lun                       = SDpnt->lun;
	SCpnt->channel                   = SDpnt->channel;
	SCpnt->request.rq_status         = RQ_INACTIVE;
        SCpnt->host_wait                 = FALSE;
        SCpnt->device_wait               = FALSE;
	SCpnt->use_sg                    = 0;
	SCpnt->old_use_sg                = 0;
	SCpnt->old_cmd_len               = 0;
	SCpnt->underflow                 = 0;
	SCpnt->transfersize              = 0;
	SCpnt->serial_number             = 0;
	SCpnt->serial_number_at_timeout  = 0;
	SCpnt->host_scribble             = NULL;
	SCpnt->next                      = SDpnt->device_queue;
	SDpnt->device_queue              = SCpnt;
        SCpnt->state                     = SCSI_STATE_UNUSED;
        SCpnt->owner                     = SCSI_OWNER_NOBODY;
    }
    SDpnt->has_cmdblocks = 1;
}

#ifndef MODULE /* { */
/*
 * scsi_dev_init() is our initialization routine, which in turn calls host
 * initialization, bus scanning, and sd/st initialization routines.
 * This is only used at boot time.
 */
__initfunc(int scsi_dev_init(void))
{
    Scsi_Device * SDpnt;
    struct Scsi_Host * shpnt;
    struct Scsi_Device_Template * sdtpnt;
#ifdef FOO_ON_YOU
    return;
#endif

    /* Yes we're here... */
#if CONFIG_PROC_FS
    dispatch_scsi_info_ptr = dispatch_scsi_info;
#endif

    /* Init a few things so we can "malloc" memory. */
    scsi_loadable_module_flag = 0;

    /* Register the /proc/scsi/scsi entry */
#if CONFIG_PROC_FS
    proc_scsi_register(0, &proc_scsi_scsi);
#endif

    /* initialize all hosts */
    scsi_init();

    /*
     * This is where the processing takes place for most everything
     * when commands are completed.  Until we do this, we will not be able
     * to queue any commands.
     */
    init_bh(SCSI_BH, scsi_bottom_half_handler);

    for (shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next) {
	scan_scsis(shpnt,0,0,0,0);           /* scan for scsi devices */
	if (shpnt->select_queue_depths != NULL)
	    (shpnt->select_queue_depths)(shpnt, shpnt->host_queue);
    }

    printk("scsi : detected ");
    for (sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	if (sdtpnt->dev_noticed && sdtpnt->name)
	    printk("%d SCSI %s%s ", sdtpnt->dev_noticed, sdtpnt->name,
		   (sdtpnt->dev_noticed != 1) ? "s" : "");
    printk("total.\n");

    for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	if(sdtpnt->init && sdtpnt->dev_noticed) (*sdtpnt->init)();

    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        for(SDpnt = shpnt->host_queue; SDpnt; SDpnt = SDpnt->next) 
        {
            /* SDpnt->scsi_request_fn = NULL; */
            for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
                if(sdtpnt->attach) (*sdtpnt->attach)(SDpnt);
            if(SDpnt->attached) scsi_build_commandblocks(SDpnt);
        }
    }
    
    /*
     * This should build the DMA pool.
     */
    resize_dma_pool();

    /*
     * OK, now we finish the initialization by doing spin-up, read
     * capacity, etc, etc
     */
    for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	if(sdtpnt->finish && sdtpnt->nr_dev)
	    (*sdtpnt->finish)();

    scsi_loadable_module_flag = 1;

    return 0;
}
#endif /* MODULE */   /* } */

static void print_inquiry(unsigned char *data)
{
    int i;

    printk("  Vendor: ");
    for (i = 8; i < 16; i++)
    {
	if (data[i] >= 0x20 && i < data[4] + 5)
	    printk("%c", data[i]);
	else
	    printk(" ");
    }

    printk("  Model: ");
    for (i = 16; i < 32; i++)
    {
	if (data[i] >= 0x20 && i < data[4] + 5)
	    printk("%c", data[i]);
	else
	    printk(" ");
    }

    printk("  Rev: ");
    for (i = 32; i < 36; i++)
    {
	if (data[i] >= 0x20 && i < data[4] + 5)
	    printk("%c", data[i]);
	else
	    printk(" ");
    }

    printk("\n");

    i = data[0] & 0x1f;

    printk("  Type:   %s ",
	   i < MAX_SCSI_DEVICE_CODE ? scsi_device_types[i] : "Unknown          " );
    printk("                 ANSI SCSI revision: %02x", data[2] & 0x07);
    if ((data[2] & 0x07) == 1 && (data[3] & 0x0f) == 1)
	printk(" CCS\n");
    else
	printk("\n");
}


#ifdef CONFIG_PROC_FS
int scsi_proc_info(char *buffer, char **start, off_t offset, int length,
		    int hostno, int inout)
{
    Scsi_Cmnd *SCpnt;
    struct Scsi_Device_Template *SDTpnt;
    Scsi_Device *scd;
    struct Scsi_Host *HBA_ptr;
    char *p;
    int   host, channel, id, lun;
    int	  size, len = 0;
    off_t begin = 0;
    off_t pos = 0;

    if(inout == 0) {
        /*
         * First, see if there are any attached devices or not.
         */
	for (HBA_ptr = scsi_hostlist; HBA_ptr; HBA_ptr = HBA_ptr->next)
        {
            if( HBA_ptr->host_queue != NULL )
            {
                break;
            }
        }
	size = sprintf(buffer+len,"Attached devices: %s\n", (HBA_ptr)?"":"none");
	len += size;
	pos = begin + len;
	for (HBA_ptr = scsi_hostlist; HBA_ptr; HBA_ptr = HBA_ptr->next)
        {
#if 0
	    size += sprintf(buffer+len,"scsi%2d: %s\n", (int) HBA_ptr->host_no,
	                    HBA_ptr->hostt->procname);
	    len += size;
	    pos = begin + len;
#endif
	    for(scd = HBA_ptr->host_queue; scd; scd = scd->next) 
            {
                proc_print_scsidevice(scd, buffer, &size, len);
                len += size;
                pos = begin + len;
                
                if (pos < offset) {
                    len = 0;
                    begin = pos;
                }
                if (pos > offset + length)
                    goto stop_output;
	    }
	}

    stop_output:
	*start=buffer+(offset-begin);   /* Start of wanted data */
	len-=(offset-begin);	        /* Start slop */
	if(len>length)
	    len = length;		/* Ending slop */
	return (len);
    }

    if(!buffer || length < 11 || strncmp("scsi", buffer, 4))
	return(-EINVAL);

    /*
     * Usage: echo "scsi dump #N" > /proc/scsi/scsi
     * to dump status of all scsi commands.  The number is used to specify the level
     * of detail in the dump.
     */
    if(!strncmp("dump", buffer + 5, 4)) 
    {
        unsigned int level;

	p = buffer + 10;

        if( *p == '\0' )
            return (-EINVAL);

        level = simple_strtoul(p, NULL, 0);
        scsi_dump_status(level);
    }
    /*
     * Usage: echo "scsi log token #N" > /proc/scsi/scsi
     * where token is one of [error,scan,mlqueue,mlcomplete,llqueue,
     * llcomplete,hlqueue,hlcomplete]
     */
#if CONFIG_SCSI_LOGGING /* { */

    if(!strncmp("log", buffer + 5, 3)) 
    {
        char * token;
        unsigned int level;

	p = buffer + 9;
        token = p;
        while(*p != ' ' && *p != '\t' && *p != '\0')
        {
            p++;
        }

        if( *p == '\0' )
        {
            if( strncmp(token, "all", 3) == 0 )
            {
                /*
                 * Turn on absolutely everything.
                 */
                scsi_logging_level = ~0;
            }
            else if( strncmp(token, "none", 4) == 0 )
            {
                /*
                 * Turn off absolutely everything.
                 */
                scsi_logging_level = 0;
            }
            else
            {
                return (-EINVAL);
            }
        }
        else
        {
            *p++ = '\0';
            
            level = simple_strtoul(p, NULL, 0);
            
            /*
             * Now figure out what to do with it.
             */
            if( strcmp(token, "error") == 0 )
            {
                SCSI_SET_ERROR_RECOVERY_LOGGING(level);
            }
            else if( strcmp(token, "timeout") == 0 )
            {
                SCSI_SET_TIMEOUT_LOGGING(level);
            }
            else if( strcmp(token, "scan") == 0 )
            {
                SCSI_SET_SCAN_BUS_LOGGING(level);
            }
            else if( strcmp(token, "mlqueue") == 0 )
            {
                SCSI_SET_MLQUEUE_LOGGING(level);
            }
            else if( strcmp(token, "mlcomplete") == 0 )
            {
                SCSI_SET_MLCOMPLETE_LOGGING(level);
            }
            else if( strcmp(token, "llqueue") == 0 )
            {
                SCSI_SET_LLQUEUE_LOGGING(level);
            }
            else if( strcmp(token, "llcomplete") == 0 )
            {
                SCSI_SET_LLCOMPLETE_LOGGING(level);
            }
            else if( strcmp(token, "hlqueue") == 0 )
            {
                SCSI_SET_HLQUEUE_LOGGING(level);
            }
            else if( strcmp(token, "hlcomplete") == 0 )
            {
                SCSI_SET_HLCOMPLETE_LOGGING(level);
            }
            else if( strcmp(token, "ioctl") == 0 )
            {
                SCSI_SET_IOCTL_LOGGING(level);
            }
            else
            {
                return (-EINVAL);
            }
        }

        printk("scsi logging level set to 0x%8.8x\n", scsi_logging_level);
    }
#endif /* CONFIG_SCSI_LOGGING */ /* } */

    /*
     * Usage: echo "scsi add-single-device 0 1 2 3" >/proc/scsi/scsi
     * with  "0 1 2 3" replaced by your "Host Channel Id Lun".
     * Consider this feature BETA.
     *     CAUTION: This is not for hotplugging your peripherals. As
     *     SCSI was not designed for this you could damage your
     *     hardware !
     * However perhaps it is legal to switch on an
     * already connected device. It is perhaps not
     * guaranteed this device doesn't corrupt an ongoing data transfer.
     */
    if(!strncmp("add-single-device", buffer + 5, 17)) {
	p = buffer + 23;

        host    = simple_strtoul(p, &p, 0);
        channel = simple_strtoul(p+1, &p, 0);
        id      = simple_strtoul(p+1, &p, 0);
        lun     = simple_strtoul(p+1, &p, 0);

	printk("scsi singledevice %d %d %d %d\n", host, channel,
			id, lun);

        for(HBA_ptr = scsi_hostlist; HBA_ptr; HBA_ptr = HBA_ptr->next)
        {
            if( HBA_ptr->host_no == host )
            {
                break;
            }
        }
	if(!HBA_ptr)
	    return(-ENXIO);

        for(scd = HBA_ptr->host_queue; scd; scd = scd->next) 
        {
            if((scd->channel == channel
                && scd->id == id
                && scd->lun == lun))
            {
                break;
            }
        }

	if(scd)
	    return(-ENOSYS);  /* We do not yet support unplugging */

	scan_scsis (HBA_ptr, 1, channel, id, lun);

        /* FIXME (DB) This assumes that the queue_depth routines can be used
           in this context as well, while they were all designed to be
           called only once after the detect routine. (DB) */
	if (HBA_ptr->select_queue_depths != NULL)
		(HBA_ptr->select_queue_depths)(HBA_ptr, HBA_ptr->host_queue);

	return(length);

    }

    /*
     * Usage: echo "scsi remove-single-device 0 1 2 3" >/proc/scsi/scsi
     * with  "0 1 2 3" replaced by your "Host Channel Id Lun".
     *
     * Consider this feature pre-BETA.
     *
     *     CAUTION: This is not for hotplugging your peripherals. As
     *     SCSI was not designed for this you could damage your
     *     hardware and thoroughly confuse the SCSI subsystem.
     *
     */
    else if(!strncmp("remove-single-device", buffer + 5, 20)) {
        p = buffer + 26;

        host    = simple_strtoul(p, &p, 0);
        channel = simple_strtoul(p+1, &p, 0);
        id      = simple_strtoul(p+1, &p, 0);
        lun     = simple_strtoul(p+1, &p, 0);


        for(HBA_ptr = scsi_hostlist; HBA_ptr; HBA_ptr = HBA_ptr->next)
        {
            if( HBA_ptr->host_no == host )
            {
                break;
            }
        }
	if(!HBA_ptr)
	    return(-ENODEV);

        for(scd = HBA_ptr->host_queue; scd; scd = scd->next) 
        {
            if((scd->channel == channel
                && scd->id == id
                && scd->lun == lun))
            {
                break;
            }
        }

        if(scd == NULL)
            return(-ENODEV);  /* there is no such device attached */

        if(scd->access_count)
            return(-EBUSY);

        SDTpnt = scsi_devicelist;
        while(SDTpnt != NULL) {
            if(SDTpnt->detach) (*SDTpnt->detach)(scd);
            SDTpnt = SDTpnt->next;
        }

        if(scd->attached == 0) {
            /*
             * Nobody is using this device any more.
             * Free all of the command structures.
             */
            for(SCpnt=scd->device_queue; SCpnt; SCpnt = SCpnt->next)
            {
                scd->device_queue = SCpnt->next;
                scsi_init_free((char *) SCpnt, sizeof(*SCpnt));
            }
            /* Now we can remove the device structure */
            if( scd->next != NULL )
                scd->next->prev = scd->prev;

            if( scd->prev != NULL )
                scd->prev->next = scd->next;

            if( HBA_ptr->host_queue == scd )
            {
                HBA_ptr->host_queue = scd->next;
            }

            scsi_init_free((char *) scd, sizeof(Scsi_Device));
        } else {
            return(-EBUSY);
        }
        return(0);
    }
    return(-EINVAL);
}
#endif

/*
 * Go through the device list and recompute the most appropriate size
 * for the dma pool.  Then grab more memory (as required).
 */
static void resize_dma_pool(void)
{
    int i;
    unsigned long size;
    struct Scsi_Host * shpnt;
    struct Scsi_Host * host = NULL;
    Scsi_Device * SDpnt;
    FreeSectorBitmap * new_dma_malloc_freelist = NULL;
    unsigned int new_dma_sectors = 0;
    unsigned int new_need_isa_buffer = 0;
    unsigned char ** new_dma_malloc_pages = NULL;

    if( !scsi_hostlist )
    {
	/*
	 * Free up the DMA pool.
	 */
	if( scsi_dma_free_sectors != dma_sectors )
	    panic("SCSI DMA pool memory leak %d %d\n",scsi_dma_free_sectors,dma_sectors);

	for(i=0; i < dma_sectors / SECTORS_PER_PAGE; i++)
	    scsi_init_free(dma_malloc_pages[i], PAGE_SIZE);
	if (dma_malloc_pages)
	    scsi_init_free((char *) dma_malloc_pages,
                           (dma_sectors / SECTORS_PER_PAGE)*sizeof(*dma_malloc_pages));
	dma_malloc_pages = NULL;
	if (dma_malloc_freelist)
	    scsi_init_free((char *) dma_malloc_freelist,
                           (dma_sectors / SECTORS_PER_PAGE)*sizeof(*dma_malloc_freelist));
	dma_malloc_freelist = NULL;
	dma_sectors = 0;
	scsi_dma_free_sectors = 0;
	return;
    }
    /* Next, check to see if we need to extend the DMA buffer pool */

    new_dma_sectors = 2*SECTORS_PER_PAGE;		/* Base value we use */

    if (__pa(high_memory)-1 > ISA_DMA_THRESHOLD)
	need_isa_bounce_buffers = 1;
    else
	need_isa_bounce_buffers = 0;

    if (scsi_devicelist)
	for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next)
	    new_dma_sectors += SECTORS_PER_PAGE;	/* Increment for each host */

    for (host = scsi_hostlist; host; host = host->next)
    {
        for (SDpnt=host->host_queue; SDpnt; SDpnt = SDpnt->next) 
        {
            /*
             * sd and sr drivers allocate scatterlists.
             * sr drivers may allocate for each command 1x2048 or 2x1024 extra
             * buffers for 2k sector size and 1k fs.
             * sg driver allocates buffers < 4k.
             * st driver does not need buffers from the dma pool.
             * estimate 4k buffer/command for devices of unknown type (should panic).
             */
            if (SDpnt->type == TYPE_WORM || SDpnt->type == TYPE_ROM ||
                SDpnt->type == TYPE_DISK || SDpnt->type == TYPE_MOD) {
                new_dma_sectors += ((host->sg_tablesize *
                                     sizeof(struct scatterlist) + 511) >> 9) *
                    SDpnt->queue_depth;
                if (SDpnt->type == TYPE_WORM || SDpnt->type == TYPE_ROM)
                    new_dma_sectors += (2048 >> 9) * SDpnt->queue_depth;
            }
            else if (SDpnt->type == TYPE_SCANNER ||
                     SDpnt->type == TYPE_PROCESSOR ||
                     SDpnt->type == TYPE_MEDIUM_CHANGER) {
                new_dma_sectors += (4096 >> 9) * SDpnt->queue_depth;
            }
            else {
                if (SDpnt->type != TYPE_TAPE) {
                    printk("resize_dma_pool: unknown device type %d\n", SDpnt->type);
                    new_dma_sectors += (4096 >> 9) * SDpnt->queue_depth;
                }
            }
            
            if(host->unchecked_isa_dma &&
               need_isa_bounce_buffers &&
               SDpnt->type != TYPE_TAPE) {
                new_dma_sectors += (PAGE_SIZE >> 9) * host->sg_tablesize *
                    SDpnt->queue_depth;
                new_need_isa_buffer++;
            }
        }
    }

#ifdef DEBUG_INIT
    printk("resize_dma_pool: needed dma sectors = %d\n", new_dma_sectors);
#endif

    /* limit DMA memory to 32MB: */
    new_dma_sectors = (new_dma_sectors + 15) & 0xfff0;

    /*
     * We never shrink the buffers - this leads to
     * race conditions that I would rather not even think
     * about right now.
     */
    if( new_dma_sectors < dma_sectors )
	new_dma_sectors = dma_sectors;

    if (new_dma_sectors)
    {
        size = (new_dma_sectors / SECTORS_PER_PAGE)*sizeof(FreeSectorBitmap);
	new_dma_malloc_freelist = (FreeSectorBitmap *) scsi_init_malloc(size, GFP_ATOMIC);
	memset(new_dma_malloc_freelist, 0, size);

        size = (new_dma_sectors / SECTORS_PER_PAGE)*sizeof(*new_dma_malloc_pages);
	new_dma_malloc_pages = (unsigned char **) scsi_init_malloc(size, GFP_ATOMIC);
	memset(new_dma_malloc_pages, 0, size);
    }

    /*
     * If we need more buffers, expand the list.
     */
    if( new_dma_sectors > dma_sectors ) { 
	for(i=dma_sectors / SECTORS_PER_PAGE; i< new_dma_sectors / SECTORS_PER_PAGE; i++)
	    new_dma_malloc_pages[i] = (unsigned char *)
	        scsi_init_malloc(PAGE_SIZE, GFP_ATOMIC | GFP_DMA);
    }

    /* When we dick with the actual DMA list, we need to
     * protect things
     */
    if (dma_malloc_freelist)
    {
        size = (dma_sectors / SECTORS_PER_PAGE)*sizeof(FreeSectorBitmap);
	memcpy(new_dma_malloc_freelist, dma_malloc_freelist, size);
	scsi_init_free((char *) dma_malloc_freelist, size);
    }
    dma_malloc_freelist = new_dma_malloc_freelist;

    if (dma_malloc_pages)
    {
        size = (dma_sectors / SECTORS_PER_PAGE)*sizeof(*dma_malloc_pages);
	memcpy(new_dma_malloc_pages, dma_malloc_pages, size);
	scsi_init_free((char *) dma_malloc_pages, size);
    }

    scsi_dma_free_sectors += new_dma_sectors - dma_sectors;
    dma_malloc_pages = new_dma_malloc_pages;
    dma_sectors = new_dma_sectors;
    scsi_need_isa_buffer = new_need_isa_buffer;

#ifdef DEBUG_INIT
    printk("resize_dma_pool: dma free sectors   = %d\n", scsi_dma_free_sectors);
    printk("resize_dma_pool: dma sectors        = %d\n", dma_sectors);
    printk("resize_dma_pool: need isa buffers   = %d\n", scsi_need_isa_buffer);
#endif
}

#ifdef CONFIG_MODULES		/* a big #ifdef block... */

/*
 * This entry point should be called by a loadable module if it is trying
 * add a low level scsi driver to the system.
 */
static int scsi_register_host(Scsi_Host_Template * tpnt)
{
    int pcount;
    struct Scsi_Host * shpnt;
    Scsi_Device * SDpnt;
    struct Scsi_Device_Template * sdtpnt;
    const char * name;
    unsigned long flags;

    if (tpnt->next || !tpnt->detect) return 1;/* Must be already loaded, or
					       * no detect routine available
					       */
    pcount = next_scsi_host;

    /* The detect routine must carefully spinunlock/spinlock if 
       it enables interrupts, since all interrupt handlers do 
       spinlock as well.
       All lame drivers are going to fail due to the following 
       spinlock. For the time beeing let's use it only for drivers 
       using the new scsi code. NOTE: the detect routine could
       redefine the value tpnt->use_new_eh_code. (DB, 13 May 1998) */

    if (tpnt->use_new_eh_code) {
       spin_lock_irqsave(&io_request_lock, flags);
       tpnt->present = tpnt->detect(tpnt);
       spin_unlock_irqrestore(&io_request_lock, flags);
       }
    else
       tpnt->present = tpnt->detect(tpnt);

    if (tpnt->present)
    {
	if(pcount == next_scsi_host) 
        {
	    if(tpnt->present > 1) 
            {
		printk("Failure to register low-level scsi driver");
		scsi_unregister_host(tpnt);
		return 1;
	    }
	    /* 
             * The low-level driver failed to register a driver.  We
	     *  can do this now.
	     */
	    scsi_register(tpnt,0);
	}
	tpnt->next = scsi_hosts; /* Add to the linked list */
	scsi_hosts = tpnt;

	/* Add the new driver to /proc/scsi */
#if CONFIG_PROC_FS
	build_proc_dir_entries(tpnt);
#endif


        /*
         * Add the kernel threads for each host adapter that will
         * handle error correction.
         */
        for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next)
        {
            if( shpnt->hostt == tpnt && shpnt->hostt->use_new_eh_code )
            {
                struct semaphore sem = MUTEX_LOCKED;
                
                shpnt->eh_notify = &sem;
                kernel_thread((int (*)(void *))scsi_error_handler, 
                              (void *) shpnt, 0);
                
                /*
                 * Now wait for the kernel error thread to initialize itself
                 * as it might be needed when we scan the bus.
                 */
                down (&sem);
                shpnt->eh_notify = NULL;
            }
        }
        
	for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next)
        {
	    if(shpnt->hostt == tpnt)
	    {
		if(tpnt->info)
                {
		    name = tpnt->info(shpnt);
                }
		else
                {
		    name = tpnt->name;
                }
		printk ("scsi%d : %s\n", /* And print a little message */
			shpnt->host_no, name);
	    }
        }

	printk ("scsi : %d host%s.\n", next_scsi_host,
		(next_scsi_host == 1) ? "" : "s");

	scsi_make_blocked_list();

	/* The next step is to call scan_scsis here.  This generates the
	 * Scsi_Devices entries
	 */
	for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next)
        {
	    if(shpnt->hostt == tpnt) 
            {
                scan_scsis(shpnt,0,0,0,0);
                if (shpnt->select_queue_depths != NULL)
                {
                    (shpnt->select_queue_depths)(shpnt, shpnt->host_queue);
                }
	    }
        }

	for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
        {
	    if(sdtpnt->init && sdtpnt->dev_noticed) (*sdtpnt->init)();
        }

	/*
         * Next we create the Scsi_Cmnd structures for this host 
         */
        for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
        {
            for(SDpnt = shpnt->host_queue; SDpnt; SDpnt = SDpnt->next)
                if(SDpnt->host->hostt == tpnt)
                {
                    for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
                        if(sdtpnt->attach) (*sdtpnt->attach)(SDpnt);
                    if(SDpnt->attached) scsi_build_commandblocks(SDpnt);
                }
        }

	/*
	 * Now that we have all of the devices, resize the DMA pool,
	 * as required.  */
	resize_dma_pool();


	/* This does any final handling that is required. */
	for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
        {
	    if(sdtpnt->finish && sdtpnt->nr_dev)
            {
		(*sdtpnt->finish)();
            }
        }
    }

#if defined(USE_STATIC_SCSI_MEMORY)
    printk ("SCSI memory: total %ldKb, used %ldKb, free %ldKb.\n",
	    (scsi_memory_upper_value - scsi_memory_lower_value) / 1024,
	    (scsi_init_memory_start - scsi_memory_lower_value) / 1024,
	    (scsi_memory_upper_value - scsi_init_memory_start) / 1024);
#endif

    MOD_INC_USE_COUNT;
    return 0;
}

/*
 * Similarly, this entry point should be called by a loadable module if it
 * is trying to remove a low level scsi driver from the system.
 *
 * Note - there is a fatal flaw in the deregister module function.
 * There is no way to return a code that says 'I cannot be unloaded now'.
 * The system relies entirely upon usage counts that are maintained,
 * and the assumption is that if the usage count is 0, then the module
 * can be unloaded.
 */
static void scsi_unregister_host(Scsi_Host_Template * tpnt)
{
    int                           online_status;
    int                           pcount;
    Scsi_Cmnd                   * SCpnt;
    Scsi_Device                 * SDpnt;
    Scsi_Device                 * SDpnt1;
    struct Scsi_Device_Template * sdtpnt;
    struct Scsi_Host            * sh1;
    struct Scsi_Host            * shpnt;
    Scsi_Host_Template          * SHT;
    Scsi_Host_Template          * SHTp;

    /*
     * First verify that this host adapter is completely free with no pending
     * commands 
     */
    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        for(SDpnt = shpnt->host_queue; SDpnt; 
            SDpnt = SDpnt->next)
        {
            if(SDpnt->host->hostt == tpnt 
               && SDpnt->host->hostt->module
               && GET_USE_COUNT(SDpnt->host->hostt->module)) return;
            /* 
             * FIXME(eric) - We need to find a way to notify the
             * low level driver that we are shutting down - via the
             * special device entry that still needs to get added. 
             *
             * Is detach interface below good enough for this?
             */
        }
    }

    /*
     * FIXME(eric) put a spinlock on this.  We force all of the devices offline
     * to help prevent race conditions where other hosts/processors could try and
     * get in and queue a command.
     */
    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        for(SDpnt = shpnt->host_queue; SDpnt; 
            SDpnt = SDpnt->next)
        {
            if(SDpnt->host->hostt == tpnt )
                SDpnt->online = FALSE;

        }
    }

    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
	if (shpnt->hostt != tpnt)
        {
            continue;
        }

        for(SDpnt = shpnt->host_queue; SDpnt; 
            SDpnt = SDpnt->next)
        {
            /*
             * Loop over all of the commands associated with the device.  If any of
             * them are busy, then set the state back to inactive and bail.
             */
            for(SCpnt = SDpnt->device_queue; SCpnt; 
                SCpnt = SCpnt->next)
	    {
                online_status = SDpnt->online;
                SDpnt->online = FALSE;
	        if(SCpnt->request.rq_status != RQ_INACTIVE) 
                {
                    printk("SCSI device not inactive - rq_status=%d, target=%d, pid=%ld, state=%d, owner=%d.\n",
                           SCpnt->request.rq_status, SCpnt->target, SCpnt->pid,
                           SCpnt->state, SCpnt->owner);
                    for(SDpnt1 = shpnt->host_queue; SDpnt1; 
                        SDpnt1 = SDpnt1->next)
                      {
                        for(SCpnt = SDpnt1->device_queue; SCpnt; 
                            SCpnt = SCpnt->next)
                          if(SCpnt->request.rq_status == RQ_SCSI_DISCONNECTING)
			    SCpnt->request.rq_status = RQ_INACTIVE;
                      }
                    SDpnt->online = online_status;
		    printk("Device busy???\n");
		    return;
	        }
                /*
                 * No, this device is really free.  Mark it as such, and
                 * continue on.
                 */
                SCpnt->state = SCSI_STATE_DISCONNECTING;
	        SCpnt->request.rq_status = RQ_SCSI_DISCONNECTING;  /* Mark as busy */
	    }
        }
    }
    /* Next we detach the high level drivers from the Scsi_Device structures */

    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        if(shpnt->hostt != tpnt)
        {
            continue;
        }

        for(SDpnt = shpnt->host_queue; SDpnt; 
            SDpnt = SDpnt->next)
        {
	    for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
              if(sdtpnt->detach) (*sdtpnt->detach)(SDpnt);

            /* If something still attached, punt */
            if (SDpnt->attached) 
            {
                printk("Attached usage count = %d\n", SDpnt->attached);
                return;
            }
	}
    }

    /*
     * Next, kill the kernel error recovery thread for this host.
     */
    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        if(   shpnt->hostt == tpnt 
              && shpnt->hostt->use_new_eh_code
              && shpnt->ehandler != NULL )
        {
            struct semaphore sem = MUTEX_LOCKED;
            
            shpnt->eh_notify = &sem;
            send_sig(SIGKILL, shpnt->ehandler, 1);
            down(&sem);
            shpnt->eh_notify = NULL;
        }
    }

    /* Next we free up the Scsi_Cmnd structures for this host */

    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
	if(shpnt->hostt != tpnt)
        {
            continue;
        }

        for(SDpnt = shpnt->host_queue; SDpnt; 
            SDpnt = shpnt->host_queue)
        {
            while (SDpnt->device_queue)
            {
                SCpnt = SDpnt->device_queue->next;
                scsi_init_free((char *) SDpnt->device_queue, sizeof(Scsi_Cmnd));
                SDpnt->device_queue = SCpnt;
            }
            SDpnt->has_cmdblocks = 0;

            /* Next free up the Scsi_Device structures for this host */
            shpnt->host_queue = SDpnt->next;
	    scsi_init_free((char *) SDpnt, sizeof (Scsi_Device));

        }
    }

    /* Next we go through and remove the instances of the individual hosts
     * that were detected */

    for(shpnt = scsi_hostlist; shpnt; shpnt = sh1) 
    {
	sh1 = shpnt->next;
	if(shpnt->hostt == tpnt) {
	    if(shpnt->loaded_as_module) {
		pcount = next_scsi_host;
	        /* Remove the /proc/scsi directory entry */
#if CONFIG_PROC_FS
	        proc_scsi_unregister(tpnt->proc_dir,
	                             shpnt->host_no + PROC_SCSI_FILE);
#endif
		if(tpnt->release)
		    (*tpnt->release)(shpnt);
		else {
		    /* This is the default case for the release function.
		     * It should do the right thing for most correctly
		     * written host adapters.
		     */
		    if (shpnt->irq) free_irq(shpnt->irq, NULL);
		    if (shpnt->dma_channel != 0xff) free_dma(shpnt->dma_channel);
		    if (shpnt->io_port && shpnt->n_io_port)
			release_region(shpnt->io_port, shpnt->n_io_port);
		}
		if(pcount == next_scsi_host) scsi_unregister(shpnt);
		tpnt->present--;
	    }
	}
    }

    /*
     * If there are absolutely no more hosts left, it is safe
     * to completely nuke the DMA pool.  The resize operation will
     * do the right thing and free everything.
     */
    if( !scsi_hosts )
	resize_dma_pool();

    printk ("scsi : %d host%s.\n", next_scsi_host,
	    (next_scsi_host == 1) ? "" : "s");

#if defined(USE_STATIC_SCSI_MEMORY)
    printk ("SCSI memory: total %ldKb, used %ldKb, free %ldKb.\n",
	    (scsi_memory_upper_value - scsi_memory_lower_value) / 1024,
	    (scsi_init_memory_start - scsi_memory_lower_value) / 1024,
	    (scsi_memory_upper_value - scsi_init_memory_start) / 1024);
#endif

    scsi_make_blocked_list();

    /* There were some hosts that were loaded at boot time, so we cannot
       do any more than this */
    if (tpnt->present) return;

    /* OK, this is the very last step.  Remove this host adapter from the
       linked list. */
    for(SHTp=NULL, SHT=scsi_hosts; SHT; SHTp=SHT, SHT=SHT->next)
	if(SHT == tpnt) {
	    if(SHTp)
		SHTp->next = SHT->next;
	    else
		scsi_hosts = SHT->next;
	    SHT->next = NULL;
	    break;
	}

    /* Rebuild the /proc/scsi directory entries */
#if CONFIG_PROC_FS
    proc_scsi_unregister(tpnt->proc_dir, tpnt->proc_dir->low_ino);
#endif
    MOD_DEC_USE_COUNT;
}

/*
 * This entry point should be called by a loadable module if it is trying
 * add a high level scsi driver to the system.
 */
static int scsi_register_device_module(struct Scsi_Device_Template * tpnt)
{
    Scsi_Device      * SDpnt;
    struct Scsi_Host * shpnt;

    if (tpnt->next) return 1;

    scsi_register_device(tpnt);
    /*
     * First scan the devices that we know about, and see if we notice them.
     */

    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        for(SDpnt = shpnt->host_queue; SDpnt; 
            SDpnt = SDpnt->next)
        {
            if(tpnt->detect) SDpnt->attached += (*tpnt->detect)(SDpnt);
        }
    }

    /*
     * If any of the devices would match this driver, then perform the
     * init function.
     */
    if(tpnt->init && tpnt->dev_noticed)
	if ((*tpnt->init)()) return 1;

    /*
     * Now actually connect the devices to the new driver.
     */
    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        for(SDpnt = shpnt->host_queue; SDpnt; 
            SDpnt = SDpnt->next)
        {
            if(tpnt->attach)  (*tpnt->attach)(SDpnt);
            /*
             * If this driver attached to the device, and don't have any
             * command blocks for this device, allocate some.
             */
            if(SDpnt->attached && SDpnt->has_cmdblocks == 0)
            {
                SDpnt->online = TRUE;
                scsi_build_commandblocks(SDpnt);
            }
        }
    }

    /*
     * This does any final handling that is required.
     */
    if(tpnt->finish && tpnt->nr_dev)  (*tpnt->finish)();
    resize_dma_pool();
    MOD_INC_USE_COUNT;
    return 0;
}

static int scsi_unregister_device(struct Scsi_Device_Template * tpnt)
{
    Scsi_Device * SDpnt;
    Scsi_Cmnd * SCpnt;
    struct Scsi_Host * shpnt;
    struct Scsi_Device_Template * spnt;
    struct Scsi_Device_Template * prev_spnt;

    /*
     * If we are busy, this is not going to fly.
     */
    if(GET_USE_COUNT(tpnt->module) != 0) return 0;

    /*
     * Next, detach the devices from the driver.
     */

    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        for(SDpnt = shpnt->host_queue; SDpnt; 
            SDpnt = SDpnt->next)
        {
            if(tpnt->detach) (*tpnt->detach)(SDpnt);
	    if(SDpnt->attached == 0)
	    {
                SDpnt->online = FALSE;

                /*
	         * Nobody is using this device any more.  Free all of the
	         * command structures.
	         */
	        for(SCpnt = SDpnt->device_queue; SCpnt; 
                    SCpnt = SCpnt->next)
	        {
		    if(SCpnt == SDpnt->device_queue)
			SDpnt->device_queue = SCpnt->next;
		    scsi_init_free((char *) SCpnt, sizeof(*SCpnt));
	        }
	        SDpnt->has_cmdblocks = 0;
	    }
        }
    }
    /*
     * Extract the template from the linked list.
     */
    spnt = scsi_devicelist;
    prev_spnt = NULL;
    while(spnt != tpnt)
    {
	prev_spnt = spnt;
	spnt = spnt->next;
    }
    if(prev_spnt == NULL)
	scsi_devicelist = tpnt->next;
    else
	prev_spnt->next = spnt->next;

    MOD_DEC_USE_COUNT;
    /*
     * Final cleanup for the driver is done in the driver sources in the
     * cleanup function.
     */
    return 0;
}


int scsi_register_module(int module_type, void * ptr)
{
    switch(module_type)
    {
    case MODULE_SCSI_HA:
	return scsi_register_host((Scsi_Host_Template *) ptr);
        
	/* Load upper level device handler of some kind */
    case MODULE_SCSI_DEV:
#ifdef CONFIG_KMOD
	if (scsi_hosts == NULL)
            request_module("scsi_hostadapter");
#endif
	return scsi_register_device_module((struct Scsi_Device_Template *) ptr);
	/* The rest of these are not yet implemented */
        
	/* Load constants.o */
    case MODULE_SCSI_CONST:
        
	/* Load specialized ioctl handler for some device.  Intended for
	 * cdroms that have non-SCSI2 audio command sets. */
    case MODULE_SCSI_IOCTL:
        
    default:
	return 1;
    }
}

void scsi_unregister_module(int module_type, void * ptr)
{
    switch(module_type) 
    {
    case MODULE_SCSI_HA:
	scsi_unregister_host((Scsi_Host_Template *) ptr);
	break;
    case MODULE_SCSI_DEV:
	scsi_unregister_device((struct Scsi_Device_Template *) ptr);
	break;
	/* The rest of these are not yet implemented. */
    case MODULE_SCSI_CONST:
    case MODULE_SCSI_IOCTL:
	break;
    default:
    }
    return;
}

#endif		/* CONFIG_MODULES */

/*
 * Function:    scsi_dump_status
 *
 * Purpose:     Brain dump of scsi system, used for problem solving.
 *
 * Arguments:   level - used to indicate level of detail.
 *
 * Notes:       The level isn't used at all yet, but we need to find some way
 *              of sensibly logging varying degrees of information.  A quick one-line
 *              display of each command, plus the status would be most useful.
 *
 *              This does depend upon CONFIG_SCSI_LOGGING - I do want some way of turning
 *              it all off if the user wants a lean and mean kernel.  It would probably
 *              also be useful to allow the user to specify one single host to be dumped.
 *              A second argument to the function would be useful for that purpose.
 *
 *              FIXME - some formatting of the output into tables would be very handy.
 */
static void
scsi_dump_status(int level)
{
#if CONFIG_PROC_FS
#if CONFIG_SCSI_LOGGING /* { */
    int i;
    struct Scsi_Host * shpnt;
    Scsi_Cmnd * SCpnt;
    Scsi_Device * SDpnt;
    printk("Dump of scsi host parameters:\n");
    i = 0;
    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        printk(" %d %d %d : %d %p\n",
               shpnt->host_failed,
               shpnt->host_busy,
               atomic_read(&shpnt->host_active),
               shpnt->host_blocked,
               shpnt->pending_commands);

    }

    printk("\n\n");
    printk("Dump of scsi command parameters:\n");
    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
        printk("h:c:t:l (dev sect nsect cnumsec sg) (ret all flg) (to/cmd to ito) cmd snse result\n");
	for(SDpnt=shpnt->host_queue; SDpnt; SDpnt = SDpnt->next)
	{
            for(SCpnt=SDpnt->device_queue; SCpnt; SCpnt = SCpnt->next)
            {
                /*  (0) h:c:t:l (dev sect nsect cnumsec sg) (ret all flg) (to/cmd to ito) cmd snse result %d %x      */
                printk("(%3d) %2d:%1d:%2d:%2d (%6s %4ld %4ld %4ld %4x %1d) (%1d %1d 0x%2x) (%4d %4d %4d) 0x%2.2x 0x%2.2x 0x%8.8x\n",
                       i++, 

                       SCpnt->host->host_no,
                       SCpnt->channel,
                       SCpnt->target,
                       SCpnt->lun,

                       kdevname(SCpnt->request.rq_dev),
                       SCpnt->request.sector,
                       SCpnt->request.nr_sectors,
                       SCpnt->request.current_nr_sectors,
                       SCpnt->request.rq_status,
                       SCpnt->use_sg,

                       SCpnt->retries,
                       SCpnt->allowed,
                       SCpnt->flags,

                       SCpnt->timeout_per_command,
                       SCpnt->timeout,
                       SCpnt->internal_timeout,

                       SCpnt->cmnd[0],
                       SCpnt->sense_buffer[2],
                       SCpnt->result);
            }
        }
    }

    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
	for(SDpnt=shpnt->host_queue; SDpnt; SDpnt = SDpnt->next)
	{
            /* Now dump the request lists for each block device */
            printk("Dump of pending block device requests\n");
            for(i=0; i<MAX_BLKDEV; i++)
            {
                if(blk_dev[i].current_request)
                {
                    struct request * req;
                    printk("%d: ", i);
                    req = blk_dev[i].current_request;
                    while(req) 
                    {
                        printk("(%s %d %ld %ld %ld) ",
                               kdevname(req->rq_dev),
                               req->cmd,
                               req->sector,
                               req->nr_sectors,
                               req->current_nr_sectors);
                        req = req->next;
                    }
                    printk("\n");
                }
            }
        }
    }
    printk("wait_for_request = %p\n", wait_for_request);
#endif /* CONFIG_SCSI_LOGGING */ /* } */
#endif /* CONFIG_PROC_FS */
}

#ifdef MODULE

int init_module(void) 
{
    unsigned long size;

    /*
     * This makes /proc/scsi visible.
     */
#if CONFIG_PROC_FS
    dispatch_scsi_info_ptr = dispatch_scsi_info;
#endif

    scsi_loadable_module_flag = 1;

    /* Register the /proc/scsi/scsi entry */
#if CONFIG_PROC_FS
    proc_scsi_register(0, &proc_scsi_scsi);
#endif


    dma_sectors = PAGE_SIZE / SECTOR_SIZE;
    scsi_dma_free_sectors= dma_sectors;
    /*
     * Set up a minimal DMA buffer list - this will be used during scan_scsis
     * in some cases.
     */

    /* One bit per sector to indicate free/busy */
    size = (dma_sectors / SECTORS_PER_PAGE)*sizeof(FreeSectorBitmap);
    dma_malloc_freelist = (unsigned char *) scsi_init_malloc(size, GFP_ATOMIC);
    memset(dma_malloc_freelist, 0, size);

    /* One pointer per page for the page list */
    dma_malloc_pages = (unsigned char **)
	scsi_init_malloc((dma_sectors / SECTORS_PER_PAGE)*sizeof(*dma_malloc_pages), GFP_ATOMIC);
    dma_malloc_pages[0] = (unsigned char *)
	scsi_init_malloc(PAGE_SIZE, GFP_ATOMIC | GFP_DMA);

    /*
     * This is where the processing takes place for most everything
     * when commands are completed.
     */
    init_bh(SCSI_BH, scsi_bottom_half_handler);

    return 0;
}

void cleanup_module( void)
{
    remove_bh(SCSI_BH);

#if CONFIG_PROC_FS
    proc_scsi_unregister(0, PROC_SCSI_SCSI);

    /* No, we're not here anymore. Don't show the /proc/scsi files. */
    dispatch_scsi_info_ptr = 0L;
#endif

    /*
     * Free up the DMA pool.
     */
    resize_dma_pool();

}
#endif /* MODULE */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

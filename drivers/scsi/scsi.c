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
 *  Modified by Eric Youngdale ericy@cais.com to
 *  add scatter-gather, multiple outstanding request, and other
 *  enhancements.
 *
 *  Native multichannel and wide scsi support added 
 *  by Michael Neuffer neuffer@goofy.zdv.uni-mainz.de
 */
#ifdef MODULE
/*
 * Don't import our own symbols, as this would severely mess up our
 * symbol tables.
 */
#define _SCSI_SYMS_VER_
#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <asm/system.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <linux/ioport.h>
#include <linux/kernel.h>

#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"
#include "constants.h"

#undef USE_STATIC_SCSI_MEMORY

/*
static const char RCSid[] = "$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/scsi.c,v 1.5 1993/09/24 12:45:18 drew Exp drew $";
*/

/* Command groups 3 and 4 are reserved and should never be used.  */
const unsigned char scsi_command_size[8] = { 6, 10, 10, 12, 12, 12, 10, 10 };

#define INTERNAL_ERROR (panic ("Internal error in file %s, line %d.\n", __FILE__, __LINE__))

static void scsi_done (Scsi_Cmnd *SCpnt);
static int update_timeout (Scsi_Cmnd *, int);
static void print_inquiry(unsigned char *data);
static void scsi_times_out (Scsi_Cmnd * SCpnt, int pid);

static int time_start;
static int time_elapsed;
static volatile struct Scsi_Host * host_active = NULL;
#define SCSI_BLOCK(HOST) ((HOST->block && host_active && HOST != host_active) \
			  || (HOST->can_queue && HOST->host_busy >= HOST->can_queue))

#define MAX_SCSI_DEVICE_CODE 10
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
 * global variables :
 * scsi_devices an array of these specifying the address for each
 * (host, id, LUN)
 */

Scsi_Device * scsi_devices = NULL;

/* Process ID of SCSI commands */
unsigned long scsi_pid = 0;

static unsigned char generic_sense[6] = {REQUEST_SENSE, 0,0,0, 255, 0};

/* This variable is merely a hook so that we can debug the kernel with gdb. */
Scsi_Cmnd * last_cmnd = NULL;

/* This is the pointer to the /proc/scsi code. 
 * It is only initialized to !=0 if the scsi code is present 
 */ 
extern int (* dispatch_scsi_info_ptr)(int ino, char *buffer, char **start, 
                                      off_t offset, int length, int inout); 
extern int dispatch_scsi_info(int ino, char *buffer, char **start, 
                              off_t offset, int length, int inout); 

extern void proc_print_scsidevice(Scsi_Device *scd, char *buffer, 
                                  int *size, int len);

/*
 *  As the scsi do command functions are intelligent, and may need to
 *  redo a command, we need to keep track of the last command
 *  executed on each one.
 */

#define WAS_RESET       0x01
#define WAS_TIMEDOUT    0x02
#define WAS_SENSE       0x04
#define IS_RESETTING    0x08
#define IS_ABORTING     0x10
#define ASKED_FOR_SENSE 0x20

/*
 *  This is the number  of clock ticks we should wait before we time out
 *  and abort the command.  This is for  where the scsi.c module generates
 *  the command, not where it originates from a higher level, in which
 *  case the timeout is specified there.
 *
 *  ABORT_TIMEOUT and RESET_TIMEOUT are the timeouts for RESET and ABORT
 *  respectively.
 */

#ifdef DEBUG_TIMEOUT
static void scsi_dump_status(void);
#endif


#ifdef DEBUG
    #define SCSI_TIMEOUT (5*HZ)
#else
    #define SCSI_TIMEOUT (1*HZ)
#endif

#ifdef DEBUG
    #define SENSE_TIMEOUT SCSI_TIMEOUT
    #define ABORT_TIMEOUT SCSI_TIMEOUT
    #define RESET_TIMEOUT SCSI_TIMEOUT
#else
    #define SENSE_TIMEOUT (5*HZ/10)
    #define RESET_TIMEOUT (5*HZ/10)
    #define ABORT_TIMEOUT (5*HZ/10)
#endif

#define MIN_RESET_DELAY (1*HZ)

/* Do not call reset on error if we just did a reset within 10 sec. */
#define MIN_RESET_PERIOD (10*HZ)

/* The following devices are known not to tolerate a lun != 0 scan for
 * one reason or another.  Some will respond to all luns, others will
 * lock up. 
 */

#define BLIST_NOLUN     0x01
#define BLIST_FORCELUN  0x02
#define BLIST_BORKEN    0x04
#define BLIST_KEY       0x08
#define BLIST_SINGLELUN 0x10

struct dev_info{
    const char * vendor;
    const char * model;
    const char * revision; /* Latest revision known to be bad.  Not used yet */
    unsigned flags;
};

/*
 * This is what was previously known as the blacklist.  The concept
 * has been expanded so that we can specify other 
 * The blacklist is used to determine which devices cannot tolerate a
 * lun probe because of buggy firmware.  Far too many for my liking,
 * which is why the default is now for there to be no lun scan. 
 */
static struct dev_info device_list[] =
{
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
{"NEC","CD-ROM DRIVE:841","1.0", BLIST_NOLUN},  /* Locks-up when LUN>0 polled. */
{"RODIME","RO3000S","2.33", BLIST_NOLUN},       /* Locks up if polled for lun != 0 */
{"SEAGATE", "ST157N", "\004|j", BLIST_NOLUN},   /* causes failed REQUEST SENSE on lun 1 
                                                 * for aha152x controller, which causes 
                                                 * SCSI code to reset bus.*/
{"SEAGATE", "ST296","921", BLIST_NOLUN},        /* Responds to all lun */
{"SONY","CD-ROM CDU-541","4.3d", BLIST_NOLUN},
{"SONY","CD-ROM CDU-55S","1.0i", BLIST_NOLUN},
{"SONY","CD-ROM CDU-561","1.7x", BLIST_NOLUN},
{"TANDBERG","TDC 3600","U07", BLIST_NOLUN},     /* Locks up if polled for lun != 0 */
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

/*
 * Other types of devices that have special flags.
 */
{"SONY","CD-ROM CDU-8001","*", BLIST_BORKEN},
{"TEXEL","CD-ROM","1.06", BLIST_BORKEN},
{"INSITE","Floptical   F*8I","*", BLIST_KEY},
{"INSITE","I325VM","*", BLIST_KEY},
{"PIONEER","CD-ROMDRM-602X","*", BLIST_FORCELUN | BLIST_SINGLELUN},
{"PIONEER","CD-ROMDRM-604X","*", BLIST_FORCELUN | BLIST_SINGLELUN},
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
 *  As the actual SCSI command runs in the background, we must set up a
 *  flag that tells scan_scsis() when the result it has is valid.
 *  scan_scsis can set the_result to -1, and watch for it to become the
 *  actual return code for that call.  the scan_scsis_done function() is
 *  our user specified completion function that is passed on to the
 *  scsi_do_cmd() function.
 */

volatile int in_scan_scsis = 0;
static int the_result;

void scsi_make_blocked_list(void)  {
    int block_count = 0, index;
    unsigned int flags;
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
    
    save_flags(flags);
    cli();
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
    
    restore_flags(flags);
}

static void scan_scsis_done (Scsi_Cmnd * SCpnt)
{
    
#ifdef DEBUG
    printk ("scan_scsis_done(%p, %06x)\n", SCpnt->host, SCpnt->result);
#endif
    SCpnt->request.dev = 0xfffe;
    
    if (SCpnt->request.sem != NULL)
	up(SCpnt->request.sem);
}

#ifdef CONFIG_SCSI_MULTI_LUN
static int max_scsi_luns = 8;
#else
static int max_scsi_luns = 1;
#endif

void scsi_luns_setup(char *str, int *ints) {
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

void scan_scsis (struct Scsi_Host * shpnt, unchar hardcoded, 
		 unchar hchannel, unchar hid, unchar hlun)
{
    int dev, lun, type, channel;
    unsigned char scsi_cmd [12];
    unsigned char scsi_result0 [256];
    unsigned char * scsi_result;
    Scsi_Device * SDpnt, *SDtail;
    struct Scsi_Device_Template * sdtpnt;
    int                 bflags;
    int                 max_dev_lun = 0;
    Scsi_Cmnd  *SCpnt;
    
    ++in_scan_scsis;
    lun = 0;
    type = -1;
    SCpnt = (Scsi_Cmnd *) scsi_init_malloc(sizeof(Scsi_Cmnd), GFP_ATOMIC|GFP_DMA);
    SDpnt = (Scsi_Device *) scsi_init_malloc(sizeof (Scsi_Device), GFP_ATOMIC);
    SDtail = scsi_devices;
    
    if(scsi_devices) while(SDtail->next) SDtail = SDtail->next;
    
    /* Make sure we have something that is valid for DMA purposes */
    scsi_result = ((current->pid == 0  || !shpnt->unchecked_isa_dma)
		   ?  &scsi_result0[0] : scsi_malloc(512));
    
    if(scsi_result == NULL) {
	printk("Unable to obtain scsi_result buffer\n");
	goto leave;
    }
    
    shpnt->host_queue = SCpnt; /* We need this so that commands can time out */

    if(hardcoded == 1) {
	channel = hchannel;
	dev = hid;
	lun = hlun;
	goto crude; /* Anyone remember good ol' BASIC ?  :-) */
    }

    for (channel = 0; channel <= shpnt->max_channel; channel++)
    {
	for (dev = 0; dev < shpnt->max_id; ++dev) {
	    if (shpnt->this_id != dev) {
		
		/*
		 * We need the for so our continue, etc. work fine.
                 * We put this in a variable so that we can override
                 * it during the scan if we detect a device *KNOWN*
                 * to have multiple logical units.
		 */
                max_dev_lun = (max_scsi_luns < shpnt->max_lun ? 
                               max_scsi_luns : shpnt->max_lun);

		for (lun = 0; lun < max_dev_lun; ++lun)
		{
		crude:
		    memset(SDpnt, 0, sizeof(Scsi_Device));
		    SDpnt->host = shpnt;
		    SDpnt->id = dev;
		    SDpnt->lun = lun;
		    SDpnt->channel = channel;

		    /* Some low level driver could use device->type (DB) */
		    SDpnt->type = -1;
		    /*
		     * Assume that the device will have handshaking problems, 
		     * and then fix this field later if it turns out it doesn't
		     */
		    SDpnt->borken = 1;
		    
		    scsi_cmd[0] = TEST_UNIT_READY;
		    scsi_cmd[1] = lun << 5;
		    scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[4] = scsi_cmd[5] = 0;
		    
		    memset(SCpnt, 0,  sizeof(Scsi_Cmnd));
		    SCpnt->host = SDpnt->host;
		    SCpnt->device = SDpnt;
		    SCpnt->target = SDpnt->id;
		    SCpnt->lun = SDpnt->lun;
		    SCpnt->channel = SDpnt->channel;

		    /* Used for mutex if loading devices after boot */
		    SCpnt->request.sem = NULL;
		    
		    SCpnt->request.dev = 0xffff; /* Mark not busy */
		    
		    scsi_do_cmd (SCpnt, (void *)  scsi_cmd, 
				 (void *) scsi_result,
				 256,  scan_scsis_done, SCSI_TIMEOUT + 4 * HZ, 5);
		    
		    /* 
		     * Wait for command to finish. Use simple wait if we are 
		     * booting, else do it right and use a mutex 
		     */
		    
		    if (current->pid == 0)
			while (SCpnt->request.dev != 0xfffe) 
			    barrier();
		    else if (SCpnt->request.dev != 0xfffe) {
			struct semaphore sem = MUTEX_LOCKED;
			
			SCpnt->request.sem = &sem;
			down(&sem);
			
			/* Hmm.. Have to ask about this one */
			while (SCpnt->request.dev != 0xfffe) schedule();
		    }
		    
#if defined(DEBUG) || defined(DEBUG_INIT)
		    printk("scsi: scan SCSIS id %d lun %d\n", dev, lun);
		    printk("scsi: return code %08x\n", SCpnt->result);
#endif

		    if(SCpnt->result) {
			if (((driver_byte(SCpnt->result) & DRIVER_SENSE) ||
			     (status_byte(SCpnt->result) & CHECK_CONDITION)) &&
			    ((SCpnt->sense_buffer[0] & 0x70) >> 4) == 7) {
			    if (SCpnt->sense_buffer[2] &0xe0)
				continue; /* No devices here... */
			    if(((SCpnt->sense_buffer[2] & 0xf) != NOT_READY) &&
			       ((SCpnt->sense_buffer[2] & 0xf) != UNIT_ATTENTION))
				continue;
			}
			else
			    break;
		    }
		    
#if defined (DEBUG) || defined(DEBUG_INIT)
		    printk("scsi: performing INQUIRY\n");
#endif

		    /*
		     * Build an INQUIRY command block.
		     */
		    scsi_cmd[0] = INQUIRY;
		    scsi_cmd[1] = (lun << 5) & 0xe0;
		    scsi_cmd[2] = 0;
		    scsi_cmd[3] = 0;
		    scsi_cmd[4] = 255;
		    scsi_cmd[5] = 0;
		    
		    SCpnt->request.dev = 0xffff; /* Mark not busy */
		    SCpnt->cmd_len = 0;

		    scsi_do_cmd (SCpnt, (void *)  scsi_cmd, 
				 (void *) scsi_result,
				 256,  scan_scsis_done, SCSI_TIMEOUT, 3);
		    
		    if (current->pid == 0)
			while (SCpnt->request.dev != 0xfffe) 
			    barrier();
		    else if (SCpnt->request.dev != 0xfffe) {
			struct semaphore sem = MUTEX_LOCKED;
			
			SCpnt->request.sem = &sem;
			down(&sem);
			
			/* Hmm.. Have to ask about this one */
			while (SCpnt->request.dev != 0xfffe) schedule();
		    }
		    
		    the_result = SCpnt->result;
		    
#if defined(DEBUG) || defined(DEBUG_INIT)
		    if (!the_result)
			printk("scsi: INQUIRY successful\n");
		    else
			printk("scsi: INQUIRY failed with code %08x\n", the_result);
#endif
		    
		    if(the_result) break;
		    
		    /* skip other luns on this device */
		    
		    if (!the_result)
		    {
			/* It would seem some TOSHIBA CDROM 
			 * gets things wrong 
			 */
			if (!strncmp(scsi_result+8,"TOSHIBA",7) &&
			    !strncmp(scsi_result+16,"CD-ROM",6) &&
			    scsi_result[0] == TYPE_DISK) {
			    scsi_result[0] = TYPE_ROM;
			    scsi_result[1] |= 0x80;  /* removable */
			}
			
			if (!strncmp(scsi_result+8,"NEC",3)) {
			    if (!strncmp(scsi_result+16,"CD-ROM DRIVE:84 ",16) || 
				!strncmp(scsi_result+16,"CD-ROM DRIVE:25",15))
				SDpnt->manufacturer = SCSI_MAN_NEC_OLDCDR;
			    else
				SDpnt->manufacturer = SCSI_MAN_NEC;
			} else if (!strncmp(scsi_result+8,"TOSHIBA",7))
			    SDpnt->manufacturer = SCSI_MAN_TOSHIBA;
                        else if (!strncmp(scsi_result+8,"SONY",4))
                            SDpnt->manufacturer = SCSI_MAN_SONY;
			else
			    SDpnt->manufacturer = SCSI_MAN_UNKNOWN;
			
			memcpy(SDpnt->vendor, scsi_result+8, 8);
			memcpy(SDpnt->model, scsi_result+16, 16);
			memcpy(SDpnt->rev, scsi_result+32, 4);
 
			SDpnt->removable = (0x80 & scsi_result[1]) >> 7;
			SDpnt->lockable = SDpnt->removable;
			SDpnt->changed = 0;
			SDpnt->access_count = 0;
			SDpnt->busy = 0;
                        SDpnt->has_cmdblocks = 0;
			/*
			 * Currently, all sequential devices are assumed to be
			 * tapes, all random devices disk, with the appropriate
			 * read only flags set for ROM / WORM treated as RO.
			 */
			
			switch (type = (scsi_result[0] & 0x1f))
			{
			case TYPE_TAPE :
			case TYPE_DISK :
			case TYPE_MOD :
			case TYPE_PROCESSOR :
			case TYPE_SCANNER :
			    SDpnt->writeable = 1;
			    break;
			case TYPE_WORM :
			case TYPE_ROM :
			    SDpnt->writeable = 0;
			    break;
			default :
#if 0
#ifdef DEBUG
			    printk("scsi: unknown type %d\n", type);
			    print_inquiry(scsi_result);
#endif
			    type = -1;
#endif
			}
			
                        SDpnt->single_lun = 0;
			SDpnt->soft_reset =
			    (scsi_result[7] & 1) && ((scsi_result[3] &7) == 2);
			SDpnt->random = (type == TYPE_TAPE) ? 0 : 1;
			SDpnt->type = (type & 0x1f);
			
			if (type != -1)
			{
			    print_inquiry(scsi_result);
			    
			    for(sdtpnt = scsi_devicelist; sdtpnt; 
				sdtpnt = sdtpnt->next)
				if(sdtpnt->detect) SDpnt->attached +=
				    (*sdtpnt->detect)(SDpnt);
			    
			    SDpnt->scsi_level = scsi_result[2] & 0x07;
			    if (SDpnt->scsi_level >= 2 ||
				(SDpnt->scsi_level == 1 &&
				 (scsi_result[3] & 0x0f) == 1))
				SDpnt->scsi_level++;
			    /*
			     * Set the tagged_queue flag for SCSI-II devices 
			     * that purport to support
			     * tagged queuing in the INQUIRY data.
			     */
			    
			    SDpnt->tagged_queue = 0;
			    
			    if ((SDpnt->scsi_level >= SCSI_2) &&
				(scsi_result[7] & 2)) {
				SDpnt->tagged_supported = 1;
				SDpnt->current_tag = 0;
			    }
			    
			    /*
			     * Accommodate drivers that want to sleep when 
			     * they should be in a polling loop.
			     */

			    SDpnt->disconnect = 0;

                            /*
                             * Get any flags for this device.
                             */
                            bflags = get_device_flags(scsi_result);

			    
			    /*
			     * Some revisions of the Texel CD ROM drives have 
			     * handshaking problems when used with the Seagate
			     * controllers.  Before we know what type of device
			     * we're talking to, we assume it's borken and then
			     * change it here if it turns out that it isn't
			     * a TEXEL drive.
			     */
                            if( (bflags & BLIST_BORKEN) == 0 )
                            {
				SDpnt->borken = 0;
                            }
			    
			    
			    /* These devices need this "key" to unlock the
			     * devices so we can use it 
			     */
                            if( (bflags & BLIST_KEY) != 0 ) {
				printk("Unlocked floptical drive.\n");
				SDpnt->lockable = 0;
				scsi_cmd[0] = MODE_SENSE;
				scsi_cmd[1] = (lun << 5) & 0xe0;
				scsi_cmd[2] = 0x2e;
				scsi_cmd[3] = 0;
				scsi_cmd[4] = 0x2a;
				scsi_cmd[5] = 0;
				
				SCpnt->request.dev = 0xffff; /* Mark not busy */
				SCpnt->cmd_len = 0;
				
				scsi_do_cmd (SCpnt, (void *)  scsi_cmd,
					     (void *) scsi_result, 0x2a,  
					     scan_scsis_done, SCSI_TIMEOUT, 3);
				
				if (current->pid == 0)
				    while (SCpnt->request.dev != 0xfffe);
				else if (SCpnt->request.dev != 0xfffe) {
				    struct semaphore sem = MUTEX_LOCKED;
				    
				    SCpnt->request.sem = &sem;
				    down(&sem);
				    
				    /* Hmm.. Have to ask about this one */
				    while (SCpnt->request.dev != 0xfffe) 
					schedule();
				}
			    }
			    /* Add this device to the linked list at the end */
			    if(SDtail)
				SDtail->next = SDpnt;
			    else
				scsi_devices = SDpnt;
			    SDtail = SDpnt;
			    
			    SDpnt = (Scsi_Device *) scsi_init_malloc(sizeof (Scsi_Device), GFP_ATOMIC);
			    /* Some scsi devices cannot be polled for lun != 0
			     * due to firmware bugs 
			     */
			    if(bflags & BLIST_NOLUN) break;

                            /*
                             * If we want to only allow I/O to one of the luns
                             * attached to this device at a time, then we set this
                             * flag.
                             */
                            if(bflags & BLIST_SINGLELUN)
                            {
                                SDpnt->single_lun = 1;
                            }

                            /*
                             * If this device is known to support multiple units, override
                             * the other settings, and scan all of them.
                             */
                            if(bflags & BLIST_FORCELUN)
                            {
                                /*
                                 * We probably want to make this a variable, but this
                                 * will do for now.
                                 */
                                max_dev_lun = 8;
                            }
 

			    /* Old drives like the MAXTOR XT-3280 say vers=0 */
			    if ((scsi_result[2] & 0x07) == 0)
				break;
			    /* Some scsi-1 peripherals do not handle lun != 0.
			     * I am assuming that scsi-2 peripherals do better 
			     */
			    if((scsi_result[2] & 0x07) == 1 &&
			       (scsi_result[3] & 0x0f) == 0) break;
			}
		    }       /* if result == DID_OK ends */

                    /*
                     * This might screw us up with multi-lun devices, but the user can
                     * scan for them too.
                     */
		    if(hardcoded == 1)
			goto leave;
		} /* for lun ends */
	    } /* if this_id != id ends */
	} /* for dev ends */
    } /* for channel ends */
    
 leave:
    shpnt->host_queue = NULL;  /* No longer needed here */
    
    
    /* Last device block does not exist.  Free memory. */
    if(SDpnt != NULL)
	scsi_init_free((char *) SDpnt, sizeof(Scsi_Device));
    
    if(SCpnt != NULL)
	scsi_init_free((char *) SCpnt, sizeof(Scsi_Cmnd));
    
    /* If we allocated a buffer so we could do DMA, free it now */
    if (scsi_result != &scsi_result0[0] && scsi_result != NULL) 
	scsi_free(scsi_result, 512);
    
    in_scan_scsis = 0;
}       /* scan_scsis  ends */

/*
 *  Flag bits for the internal_timeout array
 */

#define NORMAL_TIMEOUT 0
#define IN_ABORT 1
#define IN_RESET 2

/*
 * This is our time out function, called when the timer expires for a
 * given host adapter.  It will attempt to abort the currently executing
 * command, that failing perform a kernel panic.
 */

static void scsi_times_out (Scsi_Cmnd * SCpnt, int pid)
{
    
    switch (SCpnt->internal_timeout & (IN_ABORT | IN_RESET))
    {
    case NORMAL_TIMEOUT:
	if (!in_scan_scsis) {
#ifdef DEBUG_TIMEOUT
	    scsi_dump_status();
#endif
	}
	
	if (!scsi_abort (SCpnt, DID_TIME_OUT, pid))
	    return;
    case IN_ABORT:
	printk("SCSI host %d abort() timed out - resetting\n",
	       SCpnt->host->host_no);
	if (!scsi_reset (SCpnt))
	    return;
    case IN_RESET:
    case (IN_ABORT | IN_RESET):
	/* This might be controversial, but if there is a bus hang,
	 * you might conceivably want the machine up and running
	 * esp if you have an ide disk. 
	 */
	printk("Unable to reset scsi host %d - ", SCpnt->host->host_no);
	printk("probably a SCSI bus hang.\n");
	return;
	
    default:
	INTERNAL_ERROR;
    }
    
}


/* This function takes a quick look at a request, and decides if it
 * can be queued now, or if there would be a stall while waiting for
 * something else to finish.  This routine assumes that interrupts are
 * turned off when entering the routine.  It is the responsibility
 * of the calling code to ensure that this is the case. 
 */

Scsi_Cmnd * request_queueable (struct request * req, Scsi_Device * device)
{
    Scsi_Cmnd * SCpnt = NULL;
    int tablesize;
    Scsi_Cmnd * found = NULL;
    struct buffer_head * bh, *bhp;
    
    if (!device)
	panic ("No device passed to request_queueable().\n");
    
    if (req && req->dev <= 0)
	panic("Invalid device in request_queueable");
    
    SCpnt =  device->host->host_queue;

    /*
     * Look for a free command block.  If we have been instructed not to queue
     * multiple commands to multi-lun devices, then check to see what else is going on
     * for this device first.
     */
      
    SCpnt = device->host->host_queue;
    if (!device->single_lun) {
        while(SCpnt){
            if(SCpnt->target == device->id &&
               SCpnt->lun == device->lun) {
                if(SCpnt->request.dev < 0) break;
            }
            SCpnt = SCpnt->next;
        }
    } else {
        while(SCpnt){
            if(SCpnt->target == device->id) {
                if (SCpnt->lun == device->lun) {
                    if(found == NULL 
                       && SCpnt->request.dev < 0) 
                    {
                        found=SCpnt;
                    }
                } 
                if(SCpnt->request.dev >= 0) {
                    /*
                     * I think that we should really limit things to one
                     * outstanding command per device - this is what tends to trip
                     * up buggy firmware.
                     */
                    return NULL;
                }
            }
            SCpnt = SCpnt->next;
        }
        SCpnt = found;
    }
    
    if (!SCpnt) return NULL;
    
    if (SCSI_BLOCK(device->host)) return NULL;
    
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
	    req->dev = -1;
	    wake_up(&wait_for_request);
	}
    } else {
	SCpnt->request.dev = 0xffff; /* Busy, but no request */
	SCpnt->request.sem = NULL;   /* And no one is waiting for the device 
				      * either */
    }
    
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

    return SCpnt;
}

/* This function returns a structure pointer that will be valid for
 * the device.  The wait parameter tells us whether we should wait for
 * the unit to become free or not.  We are also able to tell this routine
 * not to return a descriptor if the host is unable to accept any more
 * commands for the time being.  We need to keep in mind that there is no
 * guarantee that the host remain not busy.  Keep in mind the
 * request_queueable function also knows the internal allocation scheme
 * of the packets for each device 
 */

Scsi_Cmnd * allocate_device (struct request ** reqp, Scsi_Device * device,
			     int wait)
{
    int dev = -1;
    struct request * req = NULL;
    int tablesize;
    unsigned int flags;
    struct buffer_head * bh, *bhp;
    struct Scsi_Host * host;
    Scsi_Cmnd * SCpnt = NULL;
    Scsi_Cmnd * SCwait = NULL;
    Scsi_Cmnd * found = NULL;
    
    if (!device)
	panic ("No device passed to allocate_device().\n");
    
    if (reqp) req = *reqp;
    
    /* See if this request has already been queued by an interrupt routine */
    if (req && (dev = req->dev) <= 0) return NULL;
    
    host = device->host;
    
    if (intr_count && SCSI_BLOCK(host)) return NULL;
    
    while (1==1){
        SCpnt = device->host->host_queue;
        if (!device->single_lun) {
            while(SCpnt){
                if(SCpnt->target == device->id &&
                   SCpnt->lun == device->lun) {
                   SCwait = SCpnt;
                    if(SCpnt->request.dev < 0) break;
                }
                SCpnt = SCpnt->next;
            }
        } else {
            while(SCpnt){
                if(SCpnt->target == device->id) {
                    if (SCpnt->lun == device->lun) {
                        SCwait = SCpnt;
                        if(found == NULL 
                           && SCpnt->request.dev < 0) 
                        {
                            found=SCpnt;
                        }
                    } 
                    if(SCpnt->request.dev >= 0) {
                        /*
                         * I think that we should really limit things to one
                         * outstanding command per device - this is what tends to trip
                         * up buggy firmware.
                         */
                        found = NULL;
                        break;
                    }
                }
                SCpnt = SCpnt->next;
            }
            SCpnt = found;
        }

	save_flags(flags);
	cli();
	/* See if this request has already been queued by an interrupt routine
	 */
	if (req && ((req->dev < 0) || (req->dev != dev))) {
	    restore_flags(flags);
	    return NULL;
	}
	if (!SCpnt || SCpnt->request.dev >= 0)  /* Might have changed */
	{
	    restore_flags(flags);
	    if(!wait) return NULL;
	    if (!SCwait) {
		printk("Attempt to allocate device channel %d, target %d, "
		       "lun %d\n", device->channel, device->id, device->lun);
		panic("No device found in allocate_device\n");
	    }
	    SCSI_SLEEP(&device->device_wait,
		       (SCwait->request.dev > 0));
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
		    req->dev = -1;
		    *reqp = req->next;
		    wake_up(&wait_for_request);
		}
	    } else {
		SCpnt->request.dev = 0xffff; /* Busy */
		SCpnt->request.sem = NULL;   /* And no one is waiting for this 
					      * to complete */
	    }
	    restore_flags(flags);
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
     */ 
    SCpnt->channel = device->channel;
    SCpnt->lun = device->lun;
    SCpnt->target = device->id;

    return SCpnt;
}

/*
 * This is inline because we have stack problemes if we recurse to deeply.
 */

inline void internal_cmnd (Scsi_Cmnd * SCpnt)
{
    int temp;
    struct Scsi_Host * host;
    unsigned int flags;
#ifdef DEBUG_DELAY
    int clock;
#endif
    
    host = SCpnt->host;
    
    /*
     * We will wait MIN_RESET_DELAY clock ticks after the last reset so
     * we can avoid the drive not being ready.
     */
    save_flags(flags);
    sti();
    temp = host->last_reset + MIN_RESET_DELAY;
    while (jiffies < temp);
    restore_flags(flags);
    
    update_timeout(SCpnt, SCpnt->timeout_per_command);
    
    /*
     * We will use a queued command if possible, otherwise we will emulate the
     * queuing and calling of completion function ourselves.
     */
#ifdef DEBUG
    printk("internal_cmnd (host = %d, channel = %d, target = %d, "
	   "command = %p, buffer = %p, \nbufflen = %d, done = %p)\n", 
	   SCpnt->host->host_no, SCpnt->channel, SCpnt->target, SCpnt->cmnd, 
	   SCpnt->buffer, SCpnt->bufflen, SCpnt->done);
#endif
    
    if (host->can_queue)
    {
#ifdef DEBUG
	printk("queuecommand : routine at %p\n",
	       host->hostt->queuecommand);
#endif
	/* This locking tries to prevent all sorts of races between
	 * queuecommand and the interrupt code.  In effect,
	 * we are only allowed to be in queuecommand once at
	 * any given time, and we can only be in the interrupt
	 * handler and the queuecommand function at the same time
	 * when queuecommand is called while servicing the
	 * interrupt. 
	 */
	
	if(!intr_count && SCpnt->host->irq)
	    disable_irq(SCpnt->host->irq);
	
	host->hostt->queuecommand (SCpnt, scsi_done);
	
	if(!intr_count && SCpnt->host->irq)
	    enable_irq(SCpnt->host->irq);
    }
    else
    {
	
#ifdef DEBUG
	printk("command() :  routine at %p\n", host->hostt->command);
#endif
	temp=host->hostt->command (SCpnt);
	SCpnt->result = temp;
#ifdef DEBUG_DELAY
	clock = jiffies + 4 * HZ;
	while (jiffies < clock);
	printk("done(host = %d, result = %04x) : routine at %08x\n", 
	       host->host_no, temp);
#endif
	scsi_done(SCpnt);
    }
#ifdef DEBUG
    printk("leaving internal_cmnd()\n");
#endif
}

static void scsi_request_sense (Scsi_Cmnd * SCpnt)
{
    unsigned int flags;
    
    save_flags(flags);
    cli();
    SCpnt->flags |= WAS_SENSE | ASKED_FOR_SENSE;
    update_timeout(SCpnt, SENSE_TIMEOUT);
    restore_flags(flags);
    
    
    memcpy ((void *) SCpnt->cmnd , (void *) generic_sense, 
	    sizeof(generic_sense));
    
    SCpnt->cmnd[1] = SCpnt->lun << 5;
    SCpnt->cmnd[4] = sizeof(SCpnt->sense_buffer);
    
    SCpnt->request_buffer = &SCpnt->sense_buffer;
    SCpnt->request_bufflen = sizeof(SCpnt->sense_buffer);
    SCpnt->use_sg = 0;
    SCpnt->cmd_len = COMMAND_SIZE(SCpnt->cmnd[0]);
    internal_cmnd (SCpnt);
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
    unsigned long flags;
    struct Scsi_Host * host = SCpnt->host;
    
#ifdef DEBUG
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
    }
#endif
    
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

    save_flags(flags);
    cli();
    SCpnt->pid = scsi_pid++;
    
    while (SCSI_BLOCK(host)) {
	restore_flags(flags);
	SCSI_SLEEP(&host->host_wait, SCSI_BLOCK(host));
	cli();
    }
    
    if (host->block) host_active = host;
    
    host->host_busy++;
    restore_flags(flags);
    
    /*
     * Our own function scsi_done (which marks the host as not busy, disables
     * the timeout counter, etc) will be called by us or by the
     * scsi_hosts[host].queuecommand() function needs to also call
     * the completion function for the high level driver.
     */
    
    memcpy ((void *) SCpnt->data_cmnd , (const void *) cmnd, 12);
#if 0
    SCpnt->host = host;
    SCpnt->channel = channel;
    SCpnt->target = target;
    SCpnt->lun = (SCpnt->data_cmnd[1] >> 5);
#endif
    SCpnt->bufflen = bufflen;
    SCpnt->buffer = buffer;
    SCpnt->flags=0;
    SCpnt->retries=0;
    SCpnt->allowed=retries;
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

    SCpnt->internal_timeout = 0;
    SCpnt->abort_reason = 0;
    internal_cmnd (SCpnt);

#ifdef DEBUG
    printk ("Leaving scsi_do_cmd()\n");
#endif
}



/*
 * The scsi_done() function disables the timeout timer for the scsi host,
 * marks the host as not busy, and calls the user specified completion
 * function for that host's current command.
 */

static void reset (Scsi_Cmnd * SCpnt)
{
#ifdef DEBUG
    printk("scsi: reset(%d, channel %d)\n", SCpnt->host->host_no, 
	   SCpnt->channel);
#endif

    SCpnt->flags |= (WAS_RESET | IS_RESETTING);
    scsi_reset(SCpnt);

#ifdef DEBUG
    printk("performing request sense\n");
#endif

#if 0  /* FIXME - remove this when done */
    if(SCpnt->flags & NEEDS_JUMPSTART) {
	SCpnt->flags &= ~NEEDS_JUMPSTART;
	scsi_request_sense (SCpnt);
    }
#endif
}



static int check_sense (Scsi_Cmnd * SCpnt)
{
    /* If there is no sense information, request it.  If we have already
     * requested it, there is no point in asking again - the firmware must
     * be confused. 
     */
    if (((SCpnt->sense_buffer[0] & 0x70) >> 4) != 7) {
	if(!(SCpnt->flags & ASKED_FOR_SENSE))
	    return SUGGEST_SENSE;
	else
	    return SUGGEST_RETRY;
    }
    
    SCpnt->flags &= ~ASKED_FOR_SENSE;
    
#ifdef DEBUG_INIT
    printk("scsi%d, channel%d : ", SCpnt->host->host_no, SCpnt->channel);
    print_sense("", SCpnt);
    printk("\n");
#endif
    if (SCpnt->sense_buffer[2] & 0xe0)
	return SUGGEST_ABORT;
    
    switch (SCpnt->sense_buffer[2] & 0xf)
    {
    case NO_SENSE:
	return 0;
    case RECOVERED_ERROR:
	return SUGGEST_IS_OK;
	
    case ABORTED_COMMAND:
	return SUGGEST_RETRY;
    case NOT_READY:
    case UNIT_ATTENTION:
	return SUGGEST_ABORT;
	
    /* these three are not supported */
    case COPY_ABORTED:
    case VOLUME_OVERFLOW:
    case MISCOMPARE:
	
    case MEDIUM_ERROR:
	return SUGGEST_REMAP;
    case BLANK_CHECK:
    case DATA_PROTECT:
    case HARDWARE_ERROR:
    case ILLEGAL_REQUEST:
    default:
	return SUGGEST_ABORT;
    }
}

/* This function is the mid-level interrupt routine, which decides how
 *  to handle error conditions.  Each invocation of this function must
 *  do one and *only* one of the following:
 *
 *  (1) Call last_cmnd[host].done.  This is done for fatal errors and
 *      normal completion, and indicates that the handling for this
 *      request is complete.
 *  (2) Call internal_cmnd to requeue the command.  This will result in
 *      scsi_done being called again when the retry is complete.
 *  (3) Call scsi_request_sense.  This asks the host adapter/drive for
 *      more information about the error condition.  When the information
 *      is available, scsi_done will be called again.
 *  (4) Call reset().  This is sort of a last resort, and the idea is that
 *      this may kick things loose and get the drive working again.  reset()
 *      automatically calls scsi_request_sense, and thus scsi_done will be
 *      called again once the reset is complete.
 *
 *      If none of the above actions are taken, the drive in question
 *      will hang. If more than one of the above actions are taken by
 *      scsi_done, then unpredictable behavior will result.
 */
static void scsi_done (Scsi_Cmnd * SCpnt)
{
    int status=0;
    int exit=0;
    int checked;
    int oldto;
    struct Scsi_Host * host = SCpnt->host;
    int result = SCpnt->result;
    oldto = update_timeout(SCpnt, 0);
    
#ifdef DEBUG_TIMEOUT
    if(result) printk("Non-zero result in scsi_done %x %d:%d\n",
		      result, SCpnt->target, SCpnt->lun);
#endif
    
    /* If we requested an abort, (and we got it) then fix up the return
     *  status to say why 
     */
    if(host_byte(result) == DID_ABORT && SCpnt->abort_reason)
	SCpnt->result = result = (result & 0xff00ffff) |
	    (SCpnt->abort_reason << 16);


#define FINISHED 0
#define MAYREDO  1
#define REDO     3
#define PENDING  4

#ifdef DEBUG
    printk("In scsi_done(host = %d, result = %06x)\n", host->host_no, result);
#endif

    if(SCpnt->flags & WAS_SENSE)
    {
	SCpnt->use_sg = SCpnt->old_use_sg;
	SCpnt->cmd_len = SCpnt->old_cmd_len;
    }

    switch (host_byte(result))
    {
    case DID_OK:
	if (status_byte(result) && (SCpnt->flags & WAS_SENSE))
	    /* Failed to obtain sense information */
	{
	    SCpnt->flags &= ~WAS_SENSE;
	    SCpnt->internal_timeout &= ~SENSE_TIMEOUT;
	    
	    if (!(SCpnt->flags & WAS_RESET))
	    {
		printk("scsi%d : channel %d target %d lun %d request sense"
		       " failed, performing reset.\n",
		       SCpnt->host->host_no, SCpnt->channel, SCpnt->target, 
		       SCpnt->lun);
		reset(SCpnt);
		return;
	    }
	    else
	    {
		exit = (DRIVER_HARD | SUGGEST_ABORT);
		status = FINISHED;
	    }
	}
	else switch(msg_byte(result))
	{
	case COMMAND_COMPLETE:
	    switch (status_byte(result))
	    {
	    case GOOD:
		if (SCpnt->flags & WAS_SENSE)
		{
#ifdef DEBUG
		    printk ("In scsi_done, GOOD status, COMMAND COMPLETE, parsing sense information.\n");
#endif
		    SCpnt->flags &= ~WAS_SENSE;
		    SCpnt->internal_timeout &= ~SENSE_TIMEOUT;
		    
		    switch (checked = check_sense(SCpnt))
		    {
		    case SUGGEST_SENSE:
		    case 0:
#ifdef DEBUG
			printk("NO SENSE.  status = REDO\n");
#endif
			update_timeout(SCpnt, oldto);
			status = REDO;
			break;
		    case SUGGEST_IS_OK:
			break;
		    case SUGGEST_REMAP:
		    case SUGGEST_RETRY:
#ifdef DEBUG
			printk("SENSE SUGGEST REMAP or SUGGEST RETRY - status = MAYREDO\n");
#endif
			status = MAYREDO;
			exit = DRIVER_SENSE | SUGGEST_RETRY;
			break;
		    case SUGGEST_ABORT:
#ifdef DEBUG
			printk("SENSE SUGGEST ABORT - status = FINISHED");
#endif
			status = FINISHED;
			exit =  DRIVER_SENSE | SUGGEST_ABORT;
			break;
		    default:
			printk ("Internal error %s %d \n", __FILE__,
				__LINE__);
		    }
		}
		else
		{
#ifdef DEBUG
		    printk("COMMAND COMPLETE message returned, status = FINISHED. \n");
#endif
		    exit =  DRIVER_OK;
		    status = FINISHED;
		}
		break;
		
	    case CHECK_CONDITION:
		switch (check_sense(SCpnt))
		{
		case 0:
		    update_timeout(SCpnt, oldto);
		    status = REDO;
		    break;
		case SUGGEST_REMAP:
		case SUGGEST_RETRY:
		    status = MAYREDO;
		    exit = DRIVER_SENSE | SUGGEST_RETRY;
		    break;
		case SUGGEST_ABORT:
		    status = FINISHED;
		    exit =  DRIVER_SENSE | SUGGEST_ABORT;
		    break;
		case SUGGEST_SENSE:
		    scsi_request_sense (SCpnt);
		    status = PENDING;
		    break;
		}
		break;
		
	    case CONDITION_GOOD:
	    case INTERMEDIATE_GOOD:
	    case INTERMEDIATE_C_GOOD:
		break;
		
	    case BUSY:
		update_timeout(SCpnt, oldto);
		status = REDO;
		break;
		
	    case RESERVATION_CONFLICT:
		printk("scsi%d, channel %d : RESERVATION CONFLICT performing"
		       " reset.\n", SCpnt->host->host_no, SCpnt->channel);
		reset(SCpnt);
		return;
#if 0
		exit = DRIVER_SOFT | SUGGEST_ABORT;
		status = MAYREDO;
		break;
#endif
	    default:
		printk ("Internal error %s %d \n"
			"status byte = %d \n", __FILE__,
			__LINE__, status_byte(result));
		
	    }
	    break;
	default:
	    panic("scsi: unsupported message byte %d received\n", 
		  msg_byte(result));
	}
	break;
    case DID_TIME_OUT:
#ifdef DEBUG
	printk("Host returned DID_TIME_OUT - ");
#endif
	
	if (SCpnt->flags & WAS_TIMEDOUT)
	{
#ifdef DEBUG
	    printk("Aborting\n");
#endif
	    exit = (DRIVER_TIMEOUT | SUGGEST_ABORT);
	}
	else
	{
#ifdef DEBUG
	    printk ("Retrying.\n");
#endif
	    SCpnt->flags  |= WAS_TIMEDOUT;
	    SCpnt->internal_timeout &= ~IN_ABORT;
	    status = REDO;
	}
	break;
    case DID_BUS_BUSY:
    case DID_PARITY:
	status = REDO;
	break;
    case DID_NO_CONNECT:
#ifdef DEBUG
	printk("Couldn't connect.\n");
#endif
	exit  = (DRIVER_HARD | SUGGEST_ABORT);
	break;
    case DID_ERROR:
	status = MAYREDO;
	exit = (DRIVER_HARD | SUGGEST_ABORT);
	break;
    case DID_BAD_TARGET:
    case DID_ABORT:
	exit = (DRIVER_INVALID | SUGGEST_ABORT);
	break;
    case DID_RESET:
	if (SCpnt->flags & IS_RESETTING)
	{
	    SCpnt->flags &= ~IS_RESETTING;
	    status = REDO;
	    break;
	}
	
	if(msg_byte(result) == GOOD &&
	   status_byte(result) == CHECK_CONDITION) {
	    switch (check_sense(SCpnt)) {
	    case 0:
		update_timeout(SCpnt, oldto);
		status = REDO;
		break;
	    case SUGGEST_REMAP:
	    case SUGGEST_RETRY:
		status = MAYREDO;
		exit = DRIVER_SENSE | SUGGEST_RETRY;
		break;
	    case SUGGEST_ABORT:
		status = FINISHED;
		exit =  DRIVER_SENSE | SUGGEST_ABORT;
		break;
	    case SUGGEST_SENSE:
		scsi_request_sense (SCpnt);
		status = PENDING;
		break;
	    }
	} else {
	    status=REDO;
	    exit = SUGGEST_RETRY;
	}
	break;
    default :
	exit = (DRIVER_ERROR | SUGGEST_DIE);
    }
    
    switch (status)
    {
    case FINISHED:
    case PENDING:
	break;
    case MAYREDO:
#ifdef DEBUG
	printk("In MAYREDO, allowing %d retries, have %d\n",
	       SCpnt->allowed, SCpnt->retries);
#endif
	if ((++SCpnt->retries) < SCpnt->allowed)
	{
	    if ((SCpnt->retries >= (SCpnt->allowed >> 1))
		&& !(jiffies < SCpnt->host->last_reset + MIN_RESET_PERIOD)
		&& !(SCpnt->flags & WAS_RESET))
	    {
		printk("scsi%d channel %d : resetting for second half of retries.\n",
		       SCpnt->host->host_no, SCpnt->channel);
		reset(SCpnt);
		break;
	    }
	    
	}
	else
	{
	    status = FINISHED;
	    break;
	}
	/* fall through to REDO */
	
    case REDO:
	
	if (SCpnt->flags & WAS_SENSE)
	    scsi_request_sense(SCpnt);
	else
	{
	    memcpy ((void *) SCpnt->cmnd,
		    (void*) SCpnt->data_cmnd,
		    sizeof(SCpnt->data_cmnd));
	    SCpnt->request_buffer = SCpnt->buffer;
	    SCpnt->request_bufflen = SCpnt->bufflen;
	    SCpnt->use_sg = SCpnt->old_use_sg;
	    SCpnt->cmd_len = SCpnt->old_cmd_len;
	    internal_cmnd (SCpnt);
	}
	break;
    default:
	INTERNAL_ERROR;
    }
    
    if (status == FINISHED) {
#ifdef DEBUG
	printk("Calling done function - at address %p\n", SCpnt->done);
#endif
	host->host_busy--; /* Indicate that we are free */
	
	if (host->block && host->host_busy == 0) {
	    host_active = NULL;
	    
	    /* For block devices "wake_up" is done in end_scsi_request */
	    if (MAJOR(SCpnt->request.dev) != SCSI_DISK_MAJOR &&
		MAJOR(SCpnt->request.dev) != SCSI_CDROM_MAJOR) {
		struct Scsi_Host * next;
		
		for (next = host->block; next != host; next = next->block)
		    wake_up(&next->host_wait);
	    }
	    
	}
	
	wake_up(&host->host_wait);
	SCpnt->result = result | ((exit & 0xff) << 24);
	SCpnt->use_sg = SCpnt->old_use_sg;
	SCpnt->cmd_len = SCpnt->old_cmd_len;
	SCpnt->done (SCpnt);
    }
    
#undef FINISHED
#undef REDO
#undef MAYREDO
#undef PENDING
}

/*
 * The scsi_abort function interfaces with the abort() function of the host
 * we are aborting, and causes the current command to not complete.  The
 * caller should deal with any error messages or status returned on the
 * next call.
 * 
 * This will not be called reentrantly for a given host.
 */

/*
 * Since we're nice guys and specified that abort() and reset()
 * can be non-reentrant.  The internal_timeout flags are used for
 * this.
 */


int scsi_abort (Scsi_Cmnd * SCpnt, int why, int pid)
{
    int oldto;
    unsigned long flags;
    struct Scsi_Host * host = SCpnt->host;
    
    while(1)
    {
	save_flags(flags);
	cli();
	
	/*
	 * Protect against races here.  If the command is done, or we are
	 * on a different command forget it.
	 */
	if (SCpnt->request.dev == -1 || pid != SCpnt->pid) {
	    restore_flags(flags);
	    return 0;
	}

	if (SCpnt->internal_timeout & IN_ABORT)
	{
	    restore_flags(flags);
	    while (SCpnt->internal_timeout & IN_ABORT)
		barrier();
	}
	else
	{
	    SCpnt->internal_timeout |= IN_ABORT;
	    oldto = update_timeout(SCpnt, ABORT_TIMEOUT);
	    
	    if ((SCpnt->flags & IS_RESETTING) &&
		SCpnt->device->soft_reset) {
		/* OK, this command must have died when we did the
		 *  reset.  The device itself must have lied. 
		 */
		printk("Stale command on %d %d:%d appears to have died when"
		       " the bus was reset\n", 
		       SCpnt->channel, SCpnt->target, SCpnt->lun);
	    }
	    
	    restore_flags(flags);
	    if (!host->host_busy) {
		SCpnt->internal_timeout &= ~IN_ABORT;
		update_timeout(SCpnt, oldto);
		return 0;
	    }
	    printk("scsi : aborting command due to timeout : pid %lu, scsi%d,"
		   " channel %d, id %d, lun %d ",
		   SCpnt->pid, SCpnt->host->host_no, (int) SCpnt->channel, 
		   (int) SCpnt->target, (int) SCpnt->lun);
	    print_command (SCpnt->cmnd);
	    if (SCpnt->request.dev == -1 || pid != SCpnt->pid)
		return 0;
	    SCpnt->abort_reason = why;
	    switch(host->hostt->abort(SCpnt)) {
		/* We do not know how to abort.  Try waiting another
		 * time increment and see if this helps. Set the
		 * WAS_TIMEDOUT flag set so we do not try this twice
		 */
	    case SCSI_ABORT_BUSY: /* Tough call - returning 1 from
				   * this is too severe 
				   */
	    case SCSI_ABORT_SNOOZE:
		if(why == DID_TIME_OUT) {
		    save_flags(flags);
		    cli();
		    SCpnt->internal_timeout &= ~IN_ABORT;
		    if(SCpnt->flags & WAS_TIMEDOUT) {
			restore_flags(flags);
			return 1; /* Indicate we cannot handle this.
				   * We drop down into the reset handler
				   * and try again 
				   */
		    } else {
			SCpnt->flags |= WAS_TIMEDOUT;
			oldto = SCpnt->timeout_per_command;
			update_timeout(SCpnt, oldto);
		    }
		    restore_flags(flags);
		}
		return 0;
	    case SCSI_ABORT_PENDING:
		if(why != DID_TIME_OUT) {
		    save_flags(flags);
		    cli();
		    update_timeout(SCpnt, oldto);
		    restore_flags(flags);
		}
		return 0;
	    case SCSI_ABORT_SUCCESS:
		/* We should have already aborted this one.  No
		 * need to adjust timeout 
		 */
	    case SCSI_ABORT_NOT_RUNNING:
		SCpnt->internal_timeout &= ~IN_ABORT;
		update_timeout(SCpnt, 0);
		return 0;
	    case SCSI_ABORT_ERROR:
	    default:
		SCpnt->internal_timeout &= ~IN_ABORT;
		return 1;
	    }
	}
    }
}

int scsi_reset (Scsi_Cmnd * SCpnt)
{
    int temp, oldto;
    unsigned long flags;
    Scsi_Cmnd * SCpnt1;
    struct Scsi_Host * host = SCpnt->host;

#ifdef DEBUG
    printk("Danger Will Robinson! - SCSI bus for host %d is being reset.\n",
	   host->host_no);
#endif
    while (1) {
	save_flags(flags);
	cli();
	if (SCpnt->internal_timeout & IN_RESET)
	{
	    restore_flags(flags);
	    while (SCpnt->internal_timeout & IN_RESET)
		barrier();
	}
	else
	{
	    SCpnt->internal_timeout |= IN_RESET;
	    oldto = update_timeout(SCpnt, RESET_TIMEOUT);
	    
	    if (host->host_busy)
	    {
		restore_flags(flags);
		SCpnt1 = host->host_queue;
		while(SCpnt1) {
		    if (SCpnt1->request.dev > 0) {
#if 0
			if (!(SCpnt1->flags & IS_RESETTING) &&
			    !(SCpnt1->internal_timeout & IN_ABORT))
			    scsi_abort(SCpnt1, DID_RESET, SCpnt->pid);
#endif
			SCpnt1->flags |= IS_RESETTING;
		    }
		    SCpnt1 = SCpnt1->next;
		}
		
		host->last_reset = jiffies;
		temp = host->hostt->reset(SCpnt);
		host->last_reset = jiffies;
	    }
	    else
	    {
		if (!host->block) host->host_busy++;
		restore_flags(flags);
		host->last_reset = jiffies;
		temp = host->hostt->reset(SCpnt);
		host->last_reset = jiffies;
		if (!host->block) host->host_busy--;
	    }
	    
#ifdef DEBUG
	    printk("scsi reset function returned %d\n", temp);
#endif
	    switch(temp) {
	    case SCSI_RESET_SUCCESS:
		save_flags(flags);
		cli();
		SCpnt->internal_timeout &= ~IN_RESET;
		update_timeout(SCpnt, oldto);
		restore_flags(flags);
		return 0;
	    case SCSI_RESET_PENDING:
		return 0;
	    case SCSI_RESET_PUNT:
	    case SCSI_RESET_WAKEUP:
		SCpnt->internal_timeout &= ~IN_RESET;
		scsi_request_sense (SCpnt);
		return 0;
	    case SCSI_RESET_SNOOZE:
		/* In this case, we set the timeout field to 0
		 * so that this command does not time out any more,
		 * and we return 1 so that we get a message on the
		 * screen. 
		 */
		save_flags(flags);
		cli();
		SCpnt->internal_timeout &= ~IN_RESET;
		update_timeout(SCpnt, 0);
		restore_flags(flags);
		/* If you snooze, you lose... */
	    case SCSI_RESET_ERROR:
	    default:
		return 1;
	    }
	    
	    return temp;
	}
    }
}


static void scsi_main_timeout(void)
{
    /*
     * We must not enter update_timeout with a timeout condition still pending.
     */
    
    int timed_out, pid;
    unsigned long flags;
    struct Scsi_Host * host;
    Scsi_Cmnd * SCpnt = NULL;
    
    do {
	save_flags(flags);
	cli();
	
	update_timeout(NULL, 0);
	/*
	 * Find all timers such that they have 0 or negative (shouldn't happen)
	 * time remaining on them.
	 */
	
	timed_out = 0;
	for(host = scsi_hostlist; host; host = host->next) {
	    for(SCpnt = host->host_queue; SCpnt; SCpnt = SCpnt->next)
		if (SCpnt->timeout == -1)
		{
		    SCpnt->timeout = 0;
		    pid = SCpnt->pid;
		    restore_flags(flags);
		    scsi_times_out(SCpnt, pid);
		    ++timed_out;
		    save_flags(flags);
		    cli();
		}
	}
    } while (timed_out);
    restore_flags(flags);
}

/*
 * The strategy is to cause the timer code to call scsi_times_out()
 * when the soonest timeout is pending.
 * The arguments are used when we are queueing a new command, because
 * we do not want to subtract the time used from this time, but when we
 * set the timer, we want to take this value into account.
 */

static int update_timeout(Scsi_Cmnd * SCset, int timeout)
{
    unsigned int least, used;
    unsigned int oldto;
    unsigned long flags;
    struct Scsi_Host * host;
    Scsi_Cmnd * SCpnt = NULL;

    save_flags(flags);
    cli();

    /*
     * Figure out how much time has passed since the last time the timeouts
     * were updated
     */
    used = (time_start) ? (jiffies - time_start) : 0;

    /*
     * Find out what is due to timeout soonest, and adjust all timeouts for
     * the amount of time that has passed since the last time we called
     * update_timeout.
     */

    oldto = 0;
    
    if(SCset){
	oldto = SCset->timeout - used;
	SCset->timeout = timeout + used;
    }

    least = 0xffffffff;
    
    for(host = scsi_hostlist; host; host = host->next)
	for(SCpnt = host->host_queue; SCpnt; SCpnt = SCpnt->next)
	    if (SCpnt->timeout > 0) {
		SCpnt->timeout -= used;
		if(SCpnt->timeout <= 0) SCpnt->timeout = -1;
		if(SCpnt->timeout > 0 && SCpnt->timeout < least)
		    least = SCpnt->timeout;
	    }
    
    /*
     * If something is due to timeout again, then we will set the next timeout
     * interrupt to occur.  Otherwise, timeouts are disabled.
     */
    
    if (least != 0xffffffff)
    {
	time_start = jiffies;
	timer_table[SCSI_TIMER].expires = (time_elapsed = least) + jiffies;
	timer_active |= 1 << SCSI_TIMER;
    }
    else
    {
	timer_table[SCSI_TIMER].expires = time_start = time_elapsed = 0;
	timer_active &= ~(1 << SCSI_TIMER);
    }
    restore_flags(flags);
    return oldto;
}


static unsigned char * dma_malloc_freelist = NULL;
static int scsi_need_isa_bounce_buffers;
static unsigned int dma_sectors = 0;
unsigned int dma_free_sectors = 0;
unsigned int need_isa_buffer = 0;
static unsigned char ** dma_malloc_pages = NULL;
#define MALLOC_PAGEBITS 12

static int scsi_register_host(Scsi_Host_Template *);
static void scsi_unregister_host(Scsi_Host_Template *);

void *scsi_malloc(unsigned int len)
{
    unsigned int nbits, mask;
    unsigned long flags;
    int i, j;
    if((len & 0x1ff) || len > (1<<MALLOC_PAGEBITS))
	return NULL;
    
    save_flags(flags);
    cli();
    nbits = len >> 9;
    mask = (1 << nbits) - 1;
    
    for(i=0;i < (dma_sectors >> (MALLOC_PAGEBITS - 9)); i++)
	for(j=0; j<=(sizeof(*dma_malloc_freelist) * 8) - nbits; j++){
	    if ((dma_malloc_freelist[i] & (mask << j)) == 0){
		dma_malloc_freelist[i] |= (mask << j);
		restore_flags(flags);
		dma_free_sectors -= nbits;
#ifdef DEBUG
		printk("SMalloc: %d %p ",len, dma_malloc_pages[i] + (j << 9));
#endif
		return (void *) ((unsigned long) dma_malloc_pages[i] + (j << 9));
	    }
	}
    restore_flags(flags);
    return NULL;  /* Nope.  No more */
}

int scsi_free(void *obj, unsigned int len)
{
    int page, sector, nbits, mask;
    long offset;
    unsigned long flags;
    
#ifdef DEBUG
    printk("Sfree %p %d\n",obj, len);
#endif
    
    offset = -1;
    for (page = 0; page < (dma_sectors >> 3); page++)
	if ((unsigned long) obj >= (unsigned long) dma_malloc_pages[page] &&
	    (unsigned long) obj < (unsigned long) dma_malloc_pages[page] 
	    + (1 << MALLOC_PAGEBITS))
	{
	    offset = ((unsigned long) obj) - ((unsigned long)dma_malloc_pages[page]);
	    break;
	}
    
    if (page == (dma_sectors >> 3)) panic("Bad offset");
    sector = offset >> 9;
    if(sector >= dma_sectors) panic ("Bad page");
    
    sector = (offset >> 9) & (sizeof(*dma_malloc_freelist) * 8 - 1);
    nbits = len >> 9;
    mask = (1 << nbits) - 1;
    
    if ((mask << sector) > 0xffff) panic ("Bad memory alignment");
    
    save_flags(flags);
    cli();
    if((dma_malloc_freelist[page] & (mask << sector)) != (mask<<sector))
	panic("Trying to free unused memory");
    
    dma_free_sectors += nbits;
    dma_malloc_freelist[page] &= ~(mask << sector);
    restore_flags(flags);
    return 0;
}

/*
 * These are special functions that can be used to obtain memory at boot time.
 * They act line a malloc function, but they simply take memory from the pool 
 */

static unsigned long scsi_init_memory_start = 0;
static unsigned long scsi_memory_lower_value = 0;
static unsigned long scsi_memory_upper_value = 0;
int scsi_loadable_module_flag; /* Set after we scan builtin drivers */

void * scsi_init_malloc(unsigned int size, int priority)
{
    unsigned long retval;
    
    /* Use the statically allocated memory instead of kmalloc  (DB) */
#if defined(USE_STATIC_SCSI_MEMORY)
    if(scsi_loadable_module_flag && !(priority & GFP_DMA))
#else
	if(scsi_loadable_module_flag)
#endif
	{
	    /*
	     * For buffers used by the DMA pool, we assume page aligned 
	     * structures.
	     */
	    if(size == PAGE_SIZE)
		retval = (unsigned long) __get_dma_pages(priority & GFP_LEVEL_MASK, 0);
	    else
		retval = (unsigned long) kmalloc(size, priority);
	} else {
	    /*
	     * Keep all memory aligned on 16-byte boundaries. Some host 
	     * adaptors (e.g. BusLogic BT-445S) require DMA buffers to be 
	     * aligned that way.
	     */
	    size = (size + 15) & ~15;
	    
	    if(scsi_loadable_module_flag &&
	       (scsi_init_memory_start + size) > scsi_memory_upper_value) {
		retval = 0;
		printk("scsi_init_malloc: no more statically allocated memory.\n");
	    }
	    else {
		retval = scsi_init_memory_start;
		scsi_init_memory_start += size;
	    }
	}
    memset((void *) retval, 0, size);
    return (void *) retval;
}


void scsi_init_free(char * ptr, unsigned int size)
{ 
    /* We need to compare addresses to see whether this was kmalloc'd or not */
    
    if((unsigned long) ptr >= scsi_init_memory_start ||
       (unsigned long) ptr <  scsi_memory_lower_value) {
	/*
	 * We need this special code here because the DMA pool assumes
	 * page aligned data.  Besides, it is wasteful to allocate
	 * page sized chunks with kmalloc.
	 */
	if(size == PAGE_SIZE)
	    free_pages((unsigned long)ptr, 0);
	else
	    kfree(ptr);
    } else {
	/* Use the same alignment as scsi_init_malloc() */
	size = (size + 15) & ~15;
	
	if(((unsigned long) ptr) + size == scsi_init_memory_start)
	    scsi_init_memory_start = (unsigned long) ptr;
    }
}

void scsi_build_commandblocks(Scsi_Device * SDpnt)
{
    int j;
    Scsi_Cmnd * SCpnt;
    struct Scsi_Host * host = NULL;
    
    for(j=0;j<SDpnt->host->cmd_per_lun;j++){
	SCpnt = (Scsi_Cmnd *) scsi_init_malloc(sizeof(Scsi_Cmnd), GFP_ATOMIC);
	SCpnt->host = SDpnt->host;
	SCpnt->device = SDpnt;
	SCpnt->target = SDpnt->id;
	SCpnt->lun = SDpnt->lun;
	SCpnt->channel = SDpnt->channel;
	SCpnt->request.dev = -1; /* Mark not busy */
	SCpnt->use_sg = 0;
	SCpnt->old_use_sg = 0;
	SCpnt->old_cmd_len = 0;
	SCpnt->timeout = 0;
	SCpnt->underflow = 0;
	SCpnt->transfersize = 0;
	SCpnt->host_scribble = NULL;
	host = SDpnt->host;
	if(host->host_queue)
	    host->host_queue->prev = SCpnt;
	SCpnt->next = host->host_queue;
	SCpnt->prev = NULL;
	host->host_queue = SCpnt;
    }
    SDpnt->has_cmdblocks = 1;
}

/*
 * scsi_dev_init() is our initialization routine, which in turn calls host
 * initialization, bus scanning, and sd/st initialization routines.  It
 * should be called from main().
 */

unsigned long scsi_dev_init (unsigned long memory_start, unsigned long memory_end)
{
    struct Scsi_Host * host = NULL;
    Scsi_Device * SDpnt;
    struct Scsi_Host * shpnt;
    struct Scsi_Device_Template * sdtpnt;
    int i;
#ifdef FOO_ON_YOU
    return;
#endif

    /* Yes we're here... */
    dispatch_scsi_info_ptr = dispatch_scsi_info;

    /* Init a few things so we can "malloc" memory. */
    scsi_loadable_module_flag = 0;
    
    /* Align everything on 16-byte boundaries. */
    scsi_init_memory_start = (memory_start + 15) & ~ 15;
    scsi_memory_lower_value = scsi_init_memory_start;
    
    timer_table[SCSI_TIMER].fn = scsi_main_timeout;
    timer_table[SCSI_TIMER].expires = 0;

    /* initialize all hosts */
    scsi_init();

    scsi_devices = (Scsi_Device *) NULL;

    for (shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
	scan_scsis(shpnt,0,0,0,0);           /* scan for scsi devices */

    printk("scsi : detected ");
    for (sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	if (sdtpnt->dev_noticed && sdtpnt->name)
	    printk("%d SCSI %s%s ", sdtpnt->dev_noticed, sdtpnt->name,
		   (sdtpnt->dev_noticed != 1) ? "s" : "");
    printk("total.\n");
    
    for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	if(sdtpnt->init && sdtpnt->dev_noticed) (*sdtpnt->init)();

    for (SDpnt=scsi_devices; SDpnt; SDpnt = SDpnt->next) {
	SDpnt->scsi_request_fn = NULL;
	for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	    if(sdtpnt->attach) (*sdtpnt->attach)(SDpnt);
	if(SDpnt->attached) scsi_build_commandblocks(SDpnt);
    }
    

    if (scsi_devicelist)
	dma_sectors = 16;  /* Base value we use */
    
    if (memory_end-1 > ISA_DMA_THRESHOLD)
	scsi_need_isa_bounce_buffers = 1;
    else
	scsi_need_isa_bounce_buffers = 0;
    
    for (SDpnt=scsi_devices; SDpnt; SDpnt = SDpnt->next) {
	host = SDpnt->host;
	
	if(SDpnt->type != TYPE_TAPE)
	    dma_sectors += ((host->sg_tablesize *
			     sizeof(struct scatterlist) + 511) >> 9) *
				 host->cmd_per_lun;
	
	if(host->unchecked_isa_dma &&
	   memory_end - 1 > ISA_DMA_THRESHOLD &&
	   SDpnt->type != TYPE_TAPE) {
	    dma_sectors += (PAGE_SIZE >> 9) * host->sg_tablesize *
		host->cmd_per_lun;
	    need_isa_buffer++;
	}
    }
    
    dma_sectors = (dma_sectors + 15) & 0xfff0;
    dma_free_sectors = dma_sectors;  /* This must be a multiple of 16 */
    
    dma_malloc_freelist = (unsigned char *)
	scsi_init_malloc(dma_sectors >> 3, GFP_ATOMIC);
    memset(dma_malloc_freelist, 0, dma_sectors >> 3);
    
    dma_malloc_pages = (unsigned char **)
	scsi_init_malloc(dma_sectors >> 1, GFP_ATOMIC);
    memset(dma_malloc_pages, 0, dma_sectors >> 1);
    
    for(i=0; i< dma_sectors >> 3; i++)
	dma_malloc_pages[i] = (unsigned char *)
	    scsi_init_malloc(PAGE_SIZE, GFP_ATOMIC | GFP_DMA);
    
    /* OK, now we finish the initialization by doing spin-up, read
     * capacity, etc, etc 
     */
    for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	if(sdtpnt->finish && sdtpnt->nr_dev)
	    (*sdtpnt->finish)();
    
    scsi_loadable_module_flag = 1;
    
    /* This allocates statically some extra memory to be used for modules,
     * until the kmalloc problem is fixed (DB) 
     */
    
#if defined(USE_STATIC_SCSI_MEMORY)
    scsi_memory_upper_value = scsi_init_memory_start + 256 * 1024;
    printk ("SCSI memory: total %ldKb, used %ldKb, free %ldKb.\n",
	    (scsi_memory_upper_value - scsi_memory_lower_value) / 1024,
	    (scsi_init_memory_start - scsi_memory_lower_value) / 1024,
	    (scsi_memory_upper_value - scsi_init_memory_start) / 1024);
    return scsi_memory_upper_value;
#else
    return scsi_init_memory_start;
#endif
}

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
    Scsi_Device *scd;
    struct Scsi_Host *HBA_ptr;
    int  parameter[4];
    char *p;
    int	  size, len = 0;
    off_t begin = 0;
    off_t pos = 0;

    scd = scsi_devices;
    HBA_ptr = scsi_hostlist;

    if(inout == 0) { 
        size = sprintf(buffer+len,"Attached devices: %s\n", (scd)?"":"none");
        len += size; 
        pos = begin + len;
        while (HBA_ptr) {
#if 0
            size += sprintf(buffer+len,"scsi%2d: %s\n", (int) HBA_ptr->host_no, HBA_ptr->hostt->procname);
            len += size; 
            pos = begin + len;
#endif
            scd = scsi_devices;
            while (scd) {
                if (scd->host == HBA_ptr) {
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
                scd = scd->next;
            }
	    HBA_ptr = HBA_ptr->next;
        }
        
    stop_output:
        *start=buffer+(offset-begin);   /* Start of wanted data */
        len-=(offset-begin);	    /* Start slop */
        if(len>length)
            len = length;		    /* Ending slop */
        return (len);     
    }

    if(!buffer || length < 25 || strncmp("scsi", buffer, 4))
	return(-EINVAL);

    if(!strncmp("singledevice", buffer + 5, 12)) {
	p = buffer + 17;

	parameter[0] = simple_strtoul(p , &p, 0);
	parameter[1] = simple_strtoul(p , &p, 0);
	parameter[2] = simple_strtoul(p , &p, 0);
	parameter[3] = simple_strtoul(p , &p, 0);

	while(scd && scd->host->host_no != parameter[0] 
	      && scd->channel != parameter[1] 
	      && scd->id != parameter[2] 
	      && scd->lun != parameter[3]) {
	    scd = scd->next;
	}
	if(scd)
	    return(-ENOSYS);  /* We do not yet support unplugging */
	while(HBA_ptr && HBA_ptr->host_no != parameter[0])
	    HBA_ptr = HBA_ptr->next;

	if(!HBA_ptr)
	    return(-ENXIO);

	scan_scsis (HBA_ptr, 1, parameter[1], parameter[2], parameter[3]);
	return(0);
    }
    return(-EINVAL);
}
#endif


/*
 * This entry point should be called by a loadable module if it is trying
 * add a low level scsi driver to the system.
 */
static int scsi_register_host(Scsi_Host_Template * tpnt)
{
    int pcount;
    struct Scsi_Host * shpnt;
    struct Scsi_Host * host = NULL;
    unsigned long flags;
    Scsi_Device * SDpnt;
    struct Scsi_Device_Template * sdtpnt;
    int i;
    const char * name;
    
    if (tpnt->next || !tpnt->detect) return 1;/* Must be already loaded, or
					       * no detect routine available 
					       */
    pcount = next_scsi_host;
    if ((tpnt->present = tpnt->detect(tpnt)))
    {
	if(pcount == next_scsi_host) {
	    if(tpnt->present > 1) {
		printk("Failure to register low-level scsi driver");
		scsi_unregister_host(tpnt);
		return 1;
	    }
	    /* The low-level driver failed to register a driver.  We
	     *  can do this now. 
	     */
	    scsi_register(tpnt,0);
	}
	tpnt->next = scsi_hosts; /* Add to the linked list */
	scsi_hosts = tpnt;
	
	/* Add the new driver to /proc/scsi */
#if CONFIG_PROC_FS 
	build_proc_dir_entries();
#endif
	
	for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next)
	    if(shpnt->hostt == tpnt)
	    {
		if(tpnt->info)
		    name = tpnt->info(shpnt);
		else
		    name = tpnt->name;
		printk ("scsi%d : %s\n", /* And print a little message */
			shpnt->host_no, name);
	    }
	
	printk ("scsi : %d host%s.\n", next_scsi_host,
		(next_scsi_host == 1) ? "" : "s");
	
	scsi_make_blocked_list();
	
	/* The next step is to call scan_scsis here.  This generates the
	 * Scsi_Devices entries 
	 */
	
	for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next)
	    if(shpnt->hostt == tpnt) scan_scsis(shpnt,0,0,0,0);
	
	for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	    if(sdtpnt->init && sdtpnt->dev_noticed) (*sdtpnt->init)();
	
	/* Next we create the Scsi_Cmnd structures for this host */
	
	for(SDpnt = scsi_devices; SDpnt; SDpnt = SDpnt->next)
	    if(SDpnt->host->hostt == tpnt)
	    {
		for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
		    if(sdtpnt->attach) (*sdtpnt->attach)(SDpnt);
		if(SDpnt->attached) scsi_build_commandblocks(SDpnt);
	    }
	
	/* Next, check to see if we need to extend the DMA buffer pool */
    {
	unsigned char * new_dma_malloc_freelist = NULL;
	unsigned int new_dma_sectors = 0;
	unsigned int new_need_isa_buffer = 0;
	unsigned char ** new_dma_malloc_pages = NULL;
	
	new_dma_sectors = 16;  /* Base value we use */
	if (scsi_devicelist)
	    for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next)
		new_dma_sectors += 8;  /* Increment for each host */
	
	for (SDpnt=scsi_devices; SDpnt; SDpnt = SDpnt->next) {
	    host = SDpnt->host;
	    
	    if(SDpnt->type != TYPE_TAPE)
		new_dma_sectors += ((host->sg_tablesize *
				     sizeof(struct scatterlist) + 511) >> 9) *
					 host->cmd_per_lun;
	    
	    if(host->unchecked_isa_dma &&
	       scsi_need_isa_bounce_buffers &&
	       SDpnt->type != TYPE_TAPE) {
		new_dma_sectors += (PAGE_SIZE >> 9) * host->sg_tablesize *
		    host->cmd_per_lun;
		new_need_isa_buffer++;
	    }
	}
	
	new_dma_sectors = (new_dma_sectors + 15) & 0xfff0;
	
	/*
	 * We never shrink the buffers - this leads to
	 * race conditions that I would rather not even think
	 * about right now.
	 */
	if( new_dma_sectors < dma_sectors )
	    new_dma_sectors = dma_sectors;
	
	new_dma_malloc_freelist = (unsigned char *)
	    scsi_init_malloc(new_dma_sectors >> 3, GFP_ATOMIC);
	memset(new_dma_malloc_freelist, 0, new_dma_sectors >> 3);
	
	new_dma_malloc_pages = (unsigned char **)
	    scsi_init_malloc(new_dma_sectors >> 1, GFP_ATOMIC);
	memset(new_dma_malloc_pages, 0, new_dma_sectors >> 1);
	
	/*
	 * If we need more buffers, expand the list.
	 */
	if( new_dma_sectors > dma_sectors ) { 
	    for(i=dma_sectors >> 3; i< new_dma_sectors >> 3; i++)
		new_dma_malloc_pages[i] = (unsigned char *)
	      scsi_init_malloc(PAGE_SIZE, GFP_ATOMIC | GFP_DMA);
	}
	
	/* When we dick with the actual DMA list, we need to 
	 * protect things 
	 */
	save_flags(flags);
	cli();
	memcpy(new_dma_malloc_freelist, dma_malloc_freelist, dma_sectors >> 3);
	scsi_init_free(dma_malloc_freelist, dma_sectors>>3);
	dma_malloc_freelist = new_dma_malloc_freelist;
	
	memcpy(new_dma_malloc_pages, dma_malloc_pages, dma_sectors >> 1);
	scsi_init_free((char *) dma_malloc_pages, dma_sectors>>1);
	
	dma_free_sectors += new_dma_sectors - dma_sectors;
	dma_malloc_pages = new_dma_malloc_pages;
	dma_sectors = new_dma_sectors;
	need_isa_buffer = new_need_isa_buffer;
	restore_flags(flags);
    }
	
	/* This does any final handling that is required. */
	for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	    if(sdtpnt->finish && sdtpnt->nr_dev)
		(*sdtpnt->finish)();
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
 */
static void scsi_unregister_host(Scsi_Host_Template * tpnt)
{
    Scsi_Host_Template * SHT, *SHTp;
    Scsi_Device *sdpnt, * sdppnt, * sdpnt1;
    Scsi_Cmnd * SCpnt;
    unsigned long flags;
    struct Scsi_Device_Template * sdtpnt;
    struct Scsi_Host * shpnt, *sh1;
    int pcount;
    
    /* First verify that this host adapter is completely free with no pending
     * commands */
    
    for(sdpnt = scsi_devices; sdpnt; sdpnt = sdpnt->next)
	if(sdpnt->host->hostt == tpnt && sdpnt->host->hostt->usage_count
	   && *sdpnt->host->hostt->usage_count) return;
    
    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
    {
	if (shpnt->hostt != tpnt) continue;
	for(SCpnt = shpnt->host_queue; SCpnt; SCpnt = SCpnt->next)
	{
	    save_flags(flags);
	    cli();
	    if(SCpnt->request.dev != -1) {
		restore_flags(flags);
		for(SCpnt = shpnt->host_queue; SCpnt; SCpnt = SCpnt->next)
		    if(SCpnt->request.dev == 0xffe0) SCpnt->request.dev = -1;
		printk("Device busy???\n");
		return;
	    }
	    SCpnt->request.dev = 0xffe0;  /* Mark as busy */
	    restore_flags(flags);
	}
    }
    /* Next we detach the high level drivers from the Scsi_Device structures */
    
    for(sdpnt = scsi_devices; sdpnt; sdpnt = sdpnt->next)
	if(sdpnt->host->hostt == tpnt)
	{
	    for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
		if(sdtpnt->detach) (*sdtpnt->detach)(sdpnt);
	    /* If something still attached, punt */
	    if (sdpnt->attached) {
		printk("Attached usage count = %d\n", sdpnt->attached);
		return;
	    }
	}
    
    /* Next we free up the Scsi_Cmnd structures for this host */
    
    for(sdpnt = scsi_devices; sdpnt; sdpnt = sdpnt->next)
	if(sdpnt->host->hostt == tpnt)
	    while (sdpnt->host->host_queue) {
		SCpnt = sdpnt->host->host_queue->next;
		scsi_init_free((char *) sdpnt->host->host_queue, sizeof(Scsi_Cmnd));
		sdpnt->host->host_queue = SCpnt;
		if (SCpnt) SCpnt->prev = NULL;
                sdpnt->has_cmdblocks = 0;
	    }
    
    /* Next free up the Scsi_Device structures for this host */
    
    sdppnt = NULL;
    for(sdpnt = scsi_devices; sdpnt; sdpnt = sdpnt1)
    {
	sdpnt1 = sdpnt->next;
	if (sdpnt->host->hostt == tpnt) {
	    if (sdppnt)
		sdppnt->next = sdpnt->next;
	    else
		scsi_devices = sdpnt->next;
	    scsi_init_free((char *) sdpnt, sizeof (Scsi_Device));
	} else
	    sdppnt = sdpnt;
    }
    
    /* Next we go through and remove the instances of the individual hosts
     * that were detected */
    
    shpnt = scsi_hostlist;
    while(shpnt) {
	sh1 = shpnt->next;
	if(shpnt->hostt == tpnt) {
	    if(shpnt->loaded_as_module) {
		pcount = next_scsi_host;
		if(tpnt->release)
		    (*tpnt->release)(shpnt);
		else {
		    /* This is the default case for the release function.  
		     * It should do the right thing for most correctly 
		     * written host adapters. 
		     */
		    if (shpnt->irq) free_irq(shpnt->irq);
		    if (shpnt->dma_channel != 0xff) free_dma(shpnt->dma_channel);
		    if (shpnt->io_port && shpnt->n_io_port)
			release_region(shpnt->io_port, shpnt->n_io_port);
		}
		if(pcount == next_scsi_host) scsi_unregister(shpnt);
		tpnt->present--;
	    }
	}
	shpnt = sh1;
    }
    
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
    build_proc_dir_entries();
#endif
    MOD_DEC_USE_COUNT;
}

/*
 * This entry point should be called by a loadable module if it is trying
 * add a high level scsi driver to the system.
 */
static int scsi_register_device_module(struct Scsi_Device_Template * tpnt)
{
    Scsi_Device * SDpnt;
    
    if (tpnt->next) return 1;
    
    scsi_register_device(tpnt);
    /*
     * First scan the devices that we know about, and see if we notice them.
     */
    
    for(SDpnt = scsi_devices; SDpnt; SDpnt = SDpnt->next)
	if(tpnt->detect) SDpnt->attached += (*tpnt->detect)(SDpnt);
    
    /*
     * If any of the devices would match this driver, then perform the
     * init function.
     */
    if(tpnt->init && tpnt->dev_noticed) (*tpnt->init)();
    
    /*
     * Now actually connect the devices to the new driver.
     */
    for(SDpnt = scsi_devices; SDpnt; SDpnt = SDpnt->next)
    {
	if(tpnt->attach)  (*tpnt->attach)(SDpnt);
	/*
	 * If this driver attached to the device, and we no longer
	 * have anything attached, release the scso command blocks.
	 */
	if(SDpnt->attached && SDpnt->has_cmdblocks == 0)
	    scsi_build_commandblocks(SDpnt);
    }
    
    /*
     * This does any final handling that is required. 
     */
    if(tpnt->finish && tpnt->nr_dev)  (*tpnt->finish)();
    MOD_INC_USE_COUNT;
    return 0;
}

static int scsi_unregister_device(struct Scsi_Device_Template * tpnt)
{
    Scsi_Device * SDpnt;
    Scsi_Cmnd * SCpnt;
    struct Scsi_Device_Template * spnt;
    struct Scsi_Device_Template * prev_spnt;
    
    /*
     * If we are busy, this is not going to fly.
     */
    if( *tpnt->usage_count != 0) return 0;
    /*
     * Next, detach the devices from the driver.
     */
    
    for(SDpnt = scsi_devices; SDpnt; SDpnt = SDpnt->next)
    {
	if(tpnt->detach) (*tpnt->detach)(SDpnt);
	if(SDpnt->attached == 0)
	{
	    /*
	     * Nobody is using this device any more.  Free all of the
	     * command structures.
	     */
	    for(SCpnt = SDpnt->host->host_queue; SCpnt; SCpnt = SCpnt->next)
	    {
		if(SCpnt->device == SDpnt)
		{
		    if(SCpnt->prev != NULL)
			SCpnt->prev->next = SCpnt->next;
		    if(SCpnt->next != NULL)
			SCpnt->next->prev = SCpnt->prev;
		    if(SCpnt == SDpnt->host->host_queue)
			SDpnt->host->host_queue = SCpnt->next;
		    scsi_init_free((char *) SCpnt, sizeof(*SCpnt));
		}
	    }
            SDpnt->has_cmdblocks = 0;
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
    switch(module_type){
    case MODULE_SCSI_HA:
	return scsi_register_host((Scsi_Host_Template *) ptr);
	
	/* Load upper level device handler of some kind */
    case MODULE_SCSI_DEV:
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
    switch(module_type) {
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

#ifdef DEBUG_TIMEOUT
static void
scsi_dump_status(void)
{
    int i;
    struct Scsi_Host * shpnt;
    Scsi_Cmnd * SCpnt;
    printk("Dump of scsi parameters:\n");
    i = 0;
    for(shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
	for(SCpnt=shpnt->host_queue; SCpnt; SCpnt = SCpnt->next)
	{
	    /*  (0) 0:0:0:0 (802 123434 8 8 0) (3 3 2) (%d %d %d) %d %x      */
	    printk("(%d) %d:%d:%d:%d (%4.4x %ld %ld %ld %ld) (%d %d %x) (%d %d %d) %x %x %x\n",
		   i++, SCpnt->host->host_no,
		   SCpnt->channel,
		   SCpnt->target,
		   SCpnt->lun,
		   SCpnt->request.dev,
		   SCpnt->request.sector,
		   SCpnt->request.nr_sectors,
		   SCpnt->request.current_nr_sectors,
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
    printk("wait_for_request = %p\n", wait_for_request);
    /* Now dump the request lists for each block device */
    printk("Dump of pending block device requests\n");
    for(i=0; i<MAX_BLKDEV; i++)
	if(blk_dev[i].current_request)
	{
	    struct request * req;
	    printk("%d: ", i);
	    req = blk_dev[i].current_request;
	    while(req) {
		printk("(%x %d %ld %ld %ld) ",
		       req->dev,
		       req->cmd,
		       req->sector,
		       req->nr_sectors,
		       req->current_nr_sectors);
		req = req->next;
	    }
	    printk("\n");
	}
}
#endif

#ifdef MODULE


char kernel_version[] = UTS_RELEASE;

extern struct symbol_table scsi_symbol_table;

int init_module(void) {

#if CONFIG_PROC_FS 
    /*
     * This will initialize the directory so that we do not have
     * random crap in there.
     */
    build_proc_dir_entries();
#endif

    /*
     * This makes the real /proc/scsi visible.
     */
    dispatch_scsi_info_ptr = dispatch_scsi_info;

    timer_table[SCSI_TIMER].fn = scsi_main_timeout;
    timer_table[SCSI_TIMER].expires = 0;
    register_symtab(&scsi_symbol_table);
    scsi_loadable_module_flag = 1;
    
    dma_sectors = PAGE_SIZE / 512;
    /*
     * Set up a minimal DMA buffer list - this will be used during scan_scsis
     * in some cases.
     */
    
    /* One bit per sector to indicate free/busy */
    dma_malloc_freelist = (unsigned char *)
	scsi_init_malloc(dma_sectors >> 3, GFP_ATOMIC);
    memset(dma_malloc_freelist, 0, dma_sectors >> 3);
    
    /* One pointer per page for the page list */
    dma_malloc_pages = (unsigned char **)
	scsi_init_malloc(dma_sectors >> 1, GFP_ATOMIC);
    dma_malloc_pages[0] = (unsigned char *)
	scsi_init_malloc(PAGE_SIZE, GFP_ATOMIC | GFP_DMA);
    return 0;
}

void cleanup_module( void) 
{
    int i;
    
    if (MOD_IN_USE) {
	printk(KERN_INFO __FILE__ ": module is in use, remove rejected\n");
	return;
    }
    
    /* No, we're not here anymore. Don't show the /proc/scsi files. */
    dispatch_scsi_info_ptr = 0L;

    /*
     * Free up the DMA pool.
     */
    for(i=0; i < dma_sectors >> 3; i++)
	scsi_init_free(dma_malloc_pages[i], PAGE_SIZE);
    scsi_init_free((char *) dma_malloc_pages, dma_sectors>>1);
    scsi_init_free(dma_malloc_freelist, dma_sectors>>3);
    
    timer_table[SCSI_TIMER].fn = NULL;
    timer_table[SCSI_TIMER].expires = 0;
    /*
     * Supposedly you just do this, and the last symtab registered will
     * be removed.  If someone else registered a symtab, this could
     * blow up in our faces.  FIXME.
     */
    register_symtab(0);
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

/*
 *	scsi.c Copyright (C) 1992 Drew Eckhardt 
 *	       Copyright (C) 1993, 1994 Eric Youngdale
 *
 *	generic mid-level SCSI driver by
 *		Drew Eckhardt 
 *
 *	<drew@colorado.edu>
 *
 *	Bug correction thanks go to : 
 *		Rik Faith <faith@cs.unc.edu>
 *		Tommy Thorn <tthorn>
 *		Thomas Wuensche <tw@fgb1.fgb.mw.tu-muenchen.de>
 * 
 *       Modified by Eric Youngdale ericy@cais.com to
 *       add scatter-gather, multiple outstanding request, and other
 *       enhancements.
 */

#include <asm/system.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <asm/irq.h>

#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"
#include "constants.h"

/*
static const char RCSid[] = "$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/scsi.c,v 1.5 1993/09/24 12:45:18 drew Exp drew $";
*/

/* Command groups 3 and 4 are reserved and should never be used.  */
const unsigned char scsi_command_size[8] = { 6, 10, 10, 12, 12, 12, 10, 10 };

#define INTERNAL_ERROR (panic ("Internal error in file %s, line %d.\n", __FILE__, __LINE__))

static void scsi_done (Scsi_Cmnd *SCpnt);
static int update_timeout (Scsi_Cmnd *, int);
static void print_inquiry(unsigned char *data);
static void scsi_times_out (Scsi_Cmnd * SCpnt);

static int time_start;
static int time_elapsed;

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
	global variables : 
	scsi_devices an array of these specifying the address for each 
	(host, id, LUN)
*/

Scsi_Device * scsi_devices = NULL;

/* Process ID of SCSI commands */
unsigned long scsi_pid = 0;

static unsigned char generic_sense[6] = {REQUEST_SENSE, 0,0,0, 255, 0};

/* This variable is merely a hook so that we can debug the kernel with gdb. */
Scsi_Cmnd * last_cmnd = NULL;

/*
 *	As the scsi do command functions are intelligent, and may need to 
 *	redo a command, we need to keep track of the last command 
 *	executed on each one.
 */

#define WAS_RESET 	0x01
#define WAS_TIMEDOUT 	0x02
#define WAS_SENSE	0x04
#define IS_RESETTING	0x08
#define IS_ABORTING	0x10
#define ASKED_FOR_SENSE 0x20

/*
 *	This is the number  of clock ticks we should wait before we time out 
 *	and abort the command.  This is for  where the scsi.c module generates 
 *	the command, not where it originates from a higher level, in which
 *	case the timeout is specified there.
 *
 *	ABORT_TIMEOUT and RESET_TIMEOUT are the timeouts for RESET and ABORT
 *	respectively.
 */

#ifdef DEBUG_TIMEOUT
static void scsi_dump_status(void);
#endif


#ifdef DEBUG
	#define SCSI_TIMEOUT 500
#else
	#define SCSI_TIMEOUT 100
#endif

#ifdef DEBUG
	#define SENSE_TIMEOUT SCSI_TIMEOUT
	#define ABORT_TIMEOUT SCSI_TIMEOUT
	#define RESET_TIMEOUT SCSI_TIMEOUT
#else
	#define SENSE_TIMEOUT 50
	#define RESET_TIMEOUT 50
	#define ABORT_TIMEOUT 50
	#define MIN_RESET_DELAY 100
#endif

/* The following devices are known not to tolerate a lun != 0 scan for
   one reason or another.  Some will respond to all luns, others will
   lock up. */

     struct blist{
       char * vendor;
       char * model;
       char * revision; /* Latest revision known to be bad.  Not used yet */
     };

static struct blist blacklist[] = 
{
   {"CHINON","CD-ROM CDS-431","H42"},  /* Locks up if polled for lun != 0 */
   {"CHINON","CD-ROM CDS-535","Q14"}, /* Lockup if polled for lun != 0 */
   {"DENON","DRD-25X","V"},   /* A cdrom that locks up when probed at lun != 0 */
   {"IMS", "CDD521/10","2.06"},   /* Locks-up when LUN>0 polled. */
   {"MAXTOR","XT-3280","PR02"},   /* Locks-up when LUN>0 polled. */
   {"MAXTOR","XT-4380S","B3C"},   /* Locks-up when LUN>0 polled. */
   {"MAXTOR","MXT-1240S","I1.2"}, /* Locks up when LUN > 0 polled */
   {"MAXTOR","XT-4170S","B5A"},   /* Locks-up sometimes when LUN>0 polled. */
   {"MAXTOR","XT-8760S","B7B"},   /* guess what? */
   {"NEC","CD-ROM DRIVE:841","1.0"},  /* Locks-up when LUN>0 polled. */
   {"RODIME","RO3000S","2.33"},  /* Locks up if polled for lun != 0 */
   {"SEAGATE", "ST157N", "\004|j"}, /* causes failed REQUEST SENSE on lun 1 for aha152x
				     * controller, which causes SCSI code to reset bus.*/
   {"SEAGATE", "ST296","921"},   /* Responds to all lun */
   {"SONY","CD-ROM CDU-541","4.3d"},
   {"TANDBERG","TDC 3600","U07"},  /* Locks up if polled for lun != 0 */
   {"TEAC","CD-ROM","1.06"},	/* causes failed REQUEST SENSE on lun 1 for seagate
				 * controller, which causes SCSI code to reset bus.*/
   {"TEXEL","CD-ROM","1.06"},   /* causes failed REQUEST SENSE on lun 1 for seagate
				 * controller, which causes SCSI code to reset bus.*/
   {"QUANTUM","LPS525S","3110"},/* Locks sometimes if polled for lun != 0 */
   {"QUANTUM","PD1225S","3110"},/* Locks sometimes if polled for lun != 0 */
   {"MEDIAVIS","CDR-H93MV","1.31"},  /* Locks up if polled for lun != 0 */
   {NULL, NULL, NULL}};	

static int blacklisted(unsigned char * response_data){
  int i = 0;
  unsigned char * pnt;
  for(i=0; 1; i++){
    if(blacklist[i].vendor == NULL) return 0;
    pnt = &response_data[8];
    while(*pnt && *pnt == ' ') pnt++;
    if(memcmp(blacklist[i].vendor, pnt,
	       strlen(blacklist[i].vendor))) continue;
    pnt = &response_data[16];
    while(*pnt && *pnt == ' ') pnt++;
    if(memcmp(blacklist[i].model, pnt,
	       strlen(blacklist[i].model))) continue;
    return 1;
  };	
};

/*
 *	As the actual SCSI command runs in the background, we must set up a 
 *	flag that tells scan_scsis() when the result it has is valid.  
 *	scan_scsis can set the_result to -1, and watch for it to become the 
 *	actual return code for that call.  the scan_scsis_done function() is 
 *	our user specified completion function that is passed on to the  
 *	scsi_do_cmd() function.
 */

volatile int in_scan_scsis = 0;
static int the_result;
static void scan_scsis_done (Scsi_Cmnd * SCpnt)
	{
	
#ifdef DEBUG
	printk ("scan_scsis_done(%d, %06x)\n", SCpnt->host, SCpnt->result);
#endif	
	SCpnt->request.dev = 0xfffe;
	}

#ifdef NO_MULTI_LUN
static int max_scsi_luns = 1;
#else
static int max_scsi_luns = 8;
#endif

void scsi_luns_setup(char *str, int *ints) {
    if (ints[0] != 1) 
	printk("scsi_luns_setup : usage max_scsi_luns=n (n should be between 1 and 8)\n");
    else
        max_scsi_luns = ints[1];
}

/*
 *	Detecting SCSI devices :	
 *	We scan all present host adapter's busses,  from ID 0 to ID 6.  
 *	We use the INQUIRY command, determine device type, and pass the ID / 
 *	lun address of all sequential devices to the tape driver, all random 
 *	devices to the disk driver.
 */

static void scan_scsis (struct Scsi_Host * shpnt)
{
  int dev, lun, type;
  unsigned char scsi_cmd [12];
  unsigned char scsi_result [256];
  Scsi_Device * SDpnt, *SDtail;
  struct Scsi_Device_Template * sdtpnt;
  Scsi_Cmnd  SCmd;
  
  ++in_scan_scsis;
  lun = 0;
  type = -1;
  SCmd.next = NULL;
  SCmd.prev = NULL;
  SDpnt = (Scsi_Device *) scsi_init_malloc(sizeof (Scsi_Device));
  SDtail = scsi_devices;
  if(scsi_devices) {
    while(SDtail->next) SDtail = SDtail->next;
  }

  shpnt->host_queue = &SCmd;  /* We need this so that
					 commands can time out */
  for (dev = 0; dev < 8; ++dev)
    if (shpnt->this_id != dev)
/*
 * We need the for so our continue, etc. work fine.
 */

      for (lun = 0; lun < max_scsi_luns; ++lun)
	{
	  SDpnt->host = shpnt;
	  SDpnt->id = dev;
	  SDpnt->lun = lun;
	  SDpnt->device_wait = NULL;
	  SDpnt->next = NULL;
	  SDpnt->attached = 0;
/*
 * Assume that the device will have handshaking problems, and then 
 * fix this field later if it turns out it doesn't.
 */
	  SDpnt->borken = 1;
	  
	  scsi_cmd[0] = TEST_UNIT_READY;
	  scsi_cmd[1] = lun << 5;
	  scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[5] = 0;
	  scsi_cmd[4] = 0;
	  
	  SCmd.host = shpnt;
	  SCmd.target = dev;
	  SCmd.lun = lun;

	  SCmd.request.dev = 0xffff; /* Mark not busy */
	  SCmd.use_sg  = 0;
	  SCmd.old_use_sg  = 0;
	  SCmd.transfersize = 0;
	  SCmd.underflow = 0;
	  
	  scsi_do_cmd (&SCmd,
		       (void *)  scsi_cmd, (void *) 
		       scsi_result, 256,  scan_scsis_done, 
		       SCSI_TIMEOUT + 400, 5);
	  
	  while (SCmd.request.dev != 0xfffe);
#if defined(DEBUG) || defined(DEBUG_INIT)
	  printk("scsi: scan SCSIS id %d lun %d\n", dev, lun);
	  printk("scsi: return code %08x\n", SCmd.result);
#endif
	  
	  
	  if(SCmd.result) {
	    if ((driver_byte(SCmd.result)  & DRIVER_SENSE) &&
		((SCmd.sense_buffer[0] & 0x70) >> 4) == 7) {
	      if (SCmd.sense_buffer[2] &0xe0)
		continue; /* No devices here... */
	      if(((SCmd.sense_buffer[2] & 0xf) != NOT_READY) &&
		 ((SCmd.sense_buffer[2] & 0xf) != UNIT_ATTENTION))
		continue;
	    }
	    else
	      break;
	  };
	  
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
	  
	  SCmd.request.dev = 0xffff; /* Mark not busy */
	  
	  scsi_do_cmd (&SCmd,
		       (void *)  scsi_cmd, (void *) 
		       scsi_result, 256,  scan_scsis_done, 
		       SCSI_TIMEOUT, 3);
	  
	  while (SCmd.request.dev != 0xfffe);
	  
	  the_result = SCmd.result;
	  
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
		/* It would seem some TOSHIBA CD-ROM gets things wrong */
		if (!strncmp(scsi_result+8,"TOSHIBA",7) &&
		    !strncmp(scsi_result+16,"CD-ROM",6) &&
		    scsi_result[0] == TYPE_DISK) {
			scsi_result[0] = TYPE_ROM;
			scsi_result[1] |= 0x80;  /* removable */
		}
	      SDpnt->removable = (0x80 & 
				  scsi_result[1]) >> 7;
	      SDpnt->lockable = SDpnt->removable;
	      SDpnt->changed = 0;
	      SDpnt->access_count = 0;
	      SDpnt->busy = 0;
/* 
 *	Currently, all sequential devices are assumed to be tapes,
 *	all random devices disk, with the appropriate read only 
 *	flags set for ROM / WORM treated as RO.
 */ 

	      switch (type = scsi_result[0])
		{
		case TYPE_TAPE :
		case TYPE_DISK :
		case TYPE_MOD :
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
	      
	      SDpnt->soft_reset = 
		(scsi_result[7] & 1) && ((scsi_result[3] & 7) == 2);
	      SDpnt->random = (type == TYPE_TAPE) ? 0 : 1;
	      SDpnt->type = type;
	      
	      if (type != -1)
		{
		  print_inquiry(scsi_result);

		  for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
		    if(sdtpnt->detect) SDpnt->attached +=
		      (*sdtpnt->detect)(SDpnt);

		  SDpnt->scsi_level = scsi_result[2] & 0x07;
		  if (SDpnt->scsi_level >= 2 ||
		      (SDpnt->scsi_level == 1 &&
		       (scsi_result[3] & 0x0f) == 1))
		    SDpnt->scsi_level++;
/* 
 * Set the tagged_queue flag for SCSI-II devices that purport to support
 * tagged queuing in the INQUIRY data.
 */
		  
		  SDpnt->tagged_queue = 0;
		  
		  if ((SDpnt->scsi_level >= SCSI_2) &&
		      (scsi_result[7] & 2)) {
		    SDpnt->tagged_supported = 1;
		    SDpnt->current_tag = 0;
		  }
		  
/*
 * Accommodate drivers that want to sleep when they should be in a polling
 * loop.
 */

		  SDpnt->disconnect = 0;

/*
 * Some revisions of the Texel CD ROM drives have handshaking
 * problems when used with the Seagate controllers.  Before we
 * know what type of device we're talking to, we assume it's 
 * borken and then change it here if it turns out that it isn't
 * a TEXEL drive.
 */

		  if(strncmp("TEXEL", (char *) &scsi_result[8], 5) != 0 ||
		     strncmp("CD-ROM", (char *) &scsi_result[16], 6) != 0 
/* 
 * XXX 1.06 has problems, some one should figure out the others too so
 * ALL TEXEL drives don't suffer in performance, especially when I finish
 * integrating my seagate patches which do multiple I_T_L nexuses.
 */

#ifdef notyet
		     || (strncmp("1.06", (char *) &scsi_result[[, 4) != 0)))
#endif
				 )
		    SDpnt->borken = 0;
		  
		  
		  /* These devices need this "key" to unlock the device
		     so we can use it */
		  if(memcmp("INSITE", &scsi_result[8], 6) == 0 &&
		     (memcmp("Floptical   F*8I", &scsi_result[16], 16) == 0
		      || memcmp("I325VM", &scsi_result[16], 6) == 0)) {
		    printk("Unlocked floptical drive.\n");
		    SDpnt->lockable = 0;
		    scsi_cmd[0] = MODE_SENSE;
		    scsi_cmd[1] = (lun << 5) & 0xe0;
		    scsi_cmd[2] = 0x2e;
		    scsi_cmd[3] = 0;
		    scsi_cmd[4] = 0x2a;
		    scsi_cmd[5] = 0;
		    
		    SCmd.request.dev = 0xffff; /* Mark not busy */
		    
		    scsi_do_cmd (&SCmd,
				 (void *)  scsi_cmd, (void *) 
				 scsi_result, 0x2a,  scan_scsis_done, 
				 SCSI_TIMEOUT, 3);
		    
		    while (SCmd.request.dev != 0xfffe);
		  };
		  /* Add this device to the linked list at the end */
		  if(SDtail)
		    SDtail->next = SDpnt;
		  else
		    scsi_devices = SDpnt;
		  SDtail = SDpnt;

		  SDpnt = (Scsi_Device *) scsi_init_malloc(sizeof (Scsi_Device));
		  /* Some scsi devices cannot be polled for lun != 0
		     due to firmware bugs */
		  if(blacklisted(scsi_result)) break;
		  /* Old drives like the MAXTOR XT-3280 say vers=0 */
		  if ((scsi_result[2] & 0x07) == 0)
		    break;
		  /* Some scsi-1 peripherals do not handle lun != 0.
		     I am assuming that scsi-2 peripherals do better */
		  if((scsi_result[2] & 0x07) == 1 && 
		     (scsi_result[3] & 0x0f) == 0) break;
		}
	    }       /* if result == DID_OK ends */
	}       /* for lun ends */
  shpnt->host_queue = NULL;  /* No longer needed here */
  
  printk("scsi : detected ");
  for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
    if(sdtpnt->dev_noticed && sdtpnt->name)
      printk("%d SCSI %s%s ", sdtpnt->dev_noticed, sdtpnt->name,
	     (sdtpnt->dev_noticed != 1) ? "s" : "");

  printk("total.\n");
  
  /* Last device block does not exist.  Free memory. */
  scsi_init_free((char *) SDpnt, sizeof(Scsi_Device));
  
  in_scan_scsis = 0;
}       /* scan_scsis  ends */

/*
 *	Flag bits for the internal_timeout array 
 */

#define NORMAL_TIMEOUT 0
#define IN_ABORT 1
#define IN_RESET 2
/*
	This is our time out function, called when the timer expires for a 
	given host adapter.  It will attempt to abort the currently executing 
	command, that failing perform a kernel panic.
*/ 

static void scsi_times_out (Scsi_Cmnd * SCpnt)
	{
	
 	switch (SCpnt->internal_timeout & (IN_ABORT | IN_RESET))
		{
		case NORMAL_TIMEOUT:
			if (!in_scan_scsis) {
			      printk("scsi : aborting command due to timeout : pid %lu, scsi%d, id %d, lun %d ",
				SCpnt->pid, SCpnt->host->host_no, (int) SCpnt->target, (int) 
				SCpnt->lun);
				print_command (SCpnt->cmnd);
#ifdef DEBUG_TIMEOUT
			  scsi_dump_status();
#endif
			}
			
			if (!scsi_abort	(SCpnt, DID_TIME_OUT))
				return;				
		case IN_ABORT:
			printk("SCSI host %d abort() timed out - reseting\n",
				SCpnt->host->host_no);
			if (!scsi_reset (SCpnt)) 
				return;
		case IN_RESET:
		case (IN_ABORT | IN_RESET):
		  /* This might be controversial, but if there is a bus hang,
		     you might conceivably want the machine up and running
		     esp if you have an ide disk. */
			printk("Unable to reset scsi host %d - ",SCpnt->host->host_no);
			printk("probably a SCSI bus hang.\n");
			return;

		default:
			INTERNAL_ERROR;
		}
					
	}


/* This function takes a quick look at a request, and decides if it
can be queued now, or if there would be a stall while waiting for
something else to finish.  This routine assumes that interrupts are
turned off when entering the routine.  It is the responsibility
of the calling code to ensure that this is the case. */

Scsi_Cmnd * request_queueable (struct request * req, Scsi_Device * device)
{
  Scsi_Cmnd * SCpnt = NULL;
  int tablesize;
  struct buffer_head * bh, *bhp;

  if (!device)
    panic ("No device passed to allocate_device().\n");
  
  if (req && req->dev <= 0)
    panic("Invalid device in allocate_device");
  
  SCpnt =  device->host->host_queue;
    while(SCpnt){
      if(SCpnt->target == device->id &&
	 SCpnt->lun == device->lun)
	if(SCpnt->request.dev < 0) break;
      SCpnt = SCpnt->next;
    };

  if (!SCpnt) return NULL;

  if (device->host->can_queue
      && device->host->host_busy >= device->host->can_queue) return NULL;

  if (req) {
    memcpy(&SCpnt->request, req, sizeof(struct request));
    tablesize = device->host->sg_tablesize;
    bhp = bh = req->bh;
    if(!tablesize) bh = NULL;
    /* Take a quick look through the table to see how big it is.  We already
       have our copy of req, so we can mess with that if we want to.  */
    while(req->nr_sectors && bh){
	    bhp = bhp->b_reqnext;
	    if(!bhp || !CONTIGUOUS_BUFFERS(bh,bhp)) tablesize--;
	    req->nr_sectors -= bh->b_size >> 9;
	    req->sector += bh->b_size >> 9;
	    if(!tablesize) break;
	    bh = bhp;
    };
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
    };      
  } else {
    SCpnt->request.dev = 0xffff; /* Busy, but no request */
    SCpnt->request.sem = NULL;  /* And no one is waiting for the device either */
  };

  SCpnt->use_sg = 0;  /* Reset the scatter-gather flag */
  SCpnt->old_use_sg  = 0;
  SCpnt->transfersize = 0;
  SCpnt->underflow = 0;
  return SCpnt;
}

/* This function returns a structure pointer that will be valid for
the device.  The wait parameter tells us whether we should wait for
the unit to become free or not.  We are also able to tell this routine
not to return a descriptor if the host is unable to accept any more
commands for the time being.  We need to keep in mind that there is no
guarantee that the host remain not busy.  Keep in mind the
request_queueable function also knows the internal allocation scheme
of the packets for each device */

Scsi_Cmnd * allocate_device (struct request ** reqp, Scsi_Device * device,
			     int wait)
{
  int dev = -1;
  struct request * req = NULL;
  int tablesize;
  struct buffer_head * bh, *bhp;
  struct Scsi_Host * host;
  Scsi_Cmnd * SCpnt = NULL;
  Scsi_Cmnd * SCwait = NULL;

  if (!device)
    panic ("No device passed to allocate_device().\n");
  
  if (reqp) req = *reqp;

    /* See if this request has already been queued by an interrupt routine */
  if (req && (dev = req->dev) <= 0) return NULL;
  
  host = device->host;
  
  while (1==1){
    SCpnt = host->host_queue;
    while(SCpnt){
      if(SCpnt->target == device->id &&
	 SCpnt->lun == device->lun) {
	SCwait = SCpnt;
	if(SCpnt->request.dev < 0) break;
      };
      SCpnt = SCpnt->next;
    };
    cli();
    /* See if this request has already been queued by an interrupt routine */
    if (req && ((req->dev < 0) || (req->dev != dev))) {
      sti();
      return NULL;
    };
    if (!SCpnt || SCpnt->request.dev >= 0)  /* Might have changed */
      {
	sti();
	if(!wait) return NULL;
	if (!SCwait) {
	  printk("Attempt to allocate device target %d, lun %d\n",
		 device->id ,device->lun);
	  panic("No device found in allocate_device\n");
	};
	SCSI_SLEEP(&device->device_wait, 
		   (SCwait->request.dev > 0));
      } else {
	if (req) {
	  memcpy(&SCpnt->request, req, sizeof(struct request));
	  tablesize = device->host->sg_tablesize;
	  bhp = bh = req->bh;
	  if(!tablesize) bh = NULL;
	  /* Take a quick look through the table to see how big it is.  We already
	     have our copy of req, so we can mess with that if we want to.  */
	  while(req->nr_sectors && bh){
	    bhp = bhp->b_reqnext;
	    if(!bhp || !CONTIGUOUS_BUFFERS(bh,bhp)) tablesize--;
	    req->nr_sectors -= bh->b_size >> 9;
	    req->sector += bh->b_size >> 9;
	    if(!tablesize) break;
	    bh = bhp;
	  };
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
	  }
	  else 
	    {
	      req->dev = -1;
	      *reqp = req->next;
	      wake_up(&wait_for_request);
	    };
	} else {
	  SCpnt->request.dev = 0xffff; /* Busy */
	  SCpnt->request.sem = NULL;  /* And no one is waiting for this to complete */
	};
	sti();
	break;
      };
  };

  SCpnt->use_sg = 0;  /* Reset the scatter-gather flag */
  SCpnt->old_use_sg  = 0;
  SCpnt->transfersize = 0;      /* No default transfer size */
  SCpnt->underflow = 0;         /* Do not flag underflow conditions */
  return SCpnt;
}

/*
	This is inline because we have stack problemes if we recurse to deeply.
*/
			 
inline void internal_cmnd (Scsi_Cmnd * SCpnt)
	{
	int temp;
	struct Scsi_Host * host;
#ifdef DEBUG_DELAY	
	int clock;
#endif

	if ((unsigned long) &SCpnt < current->kernel_stack_page)
	  panic("Kernel stack overflow.");

	host = SCpnt->host;

/*
	We will wait MIN_RESET_DELAY clock ticks after the last reset so 
	we can avoid the drive not being ready.
*/ 
temp = host->last_reset;
while (jiffies < temp);

update_timeout(SCpnt, SCpnt->timeout_per_command);

/*
	We will use a queued command if possible, otherwise we will emulate the
	queuing and calling of completion function ourselves. 
*/
#ifdef DEBUG
	printk("internal_cmnd (host = %d, target = %d, command = %08x, buffer =  %08x, \n"
		"bufflen = %d, done = %08x)\n", SCpnt->host->host_no, SCpnt->target, SCpnt->cmnd, SCpnt->buffer, SCpnt->bufflen, SCpnt->done);
#endif

        if (host->can_queue)
		{
#ifdef DEBUG
	printk("queuecommand : routine at %08x\n", 
		host->hostt->queuecommand);
#endif
		  /* This locking tries to prevent all sorts of races between
		     queuecommand and the interrupt code.  In effect,
		     we are only allowed to be in queuecommand once at
		     any given time, and we can only be in the interrupt
		     handler and the queuecommand function at the same time
		     when queuecommand is called while servicing the
		     interrupt. */

		if(!intr_count && SCpnt->host->irq)
		  disable_irq(SCpnt->host->irq);

                host->hostt->queuecommand (SCpnt, scsi_done);

		if(!intr_count && SCpnt->host->irq)
		  enable_irq(SCpnt->host->irq);
		}
	else
		{

#ifdef DEBUG
	printk("command() :  routine at %08x\n", host->hostt->command);
#endif
		temp=host->hostt->command (SCpnt);
	        SCpnt->result = temp;
#ifdef DEBUG_DELAY
	clock = jiffies + 400;
	while (jiffies < clock);
	printk("done(host = %d, result = %04x) : routine at %08x\n", host->host_no, temp, done);
#endif
	        scsi_done(SCpnt);
		}	
#ifdef DEBUG
	printk("leaving internal_cmnd()\n");
#endif
	}	

static void scsi_request_sense (Scsi_Cmnd * SCpnt)
	{
	cli();
	SCpnt->flags |= WAS_SENSE | ASKED_FOR_SENSE;
	update_timeout(SCpnt, SENSE_TIMEOUT);
	sti();
	

	memcpy ((void *) SCpnt->cmnd , (void *) generic_sense,
		sizeof(generic_sense));

	SCpnt->cmnd[1] = SCpnt->lun << 5;	
	SCpnt->cmnd[4] = sizeof(SCpnt->sense_buffer);

	SCpnt->request_buffer = &SCpnt->sense_buffer;
	SCpnt->request_bufflen = sizeof(SCpnt->sense_buffer);
	SCpnt->use_sg = 0;
	internal_cmnd (SCpnt);
	SCpnt->use_sg = SCpnt->old_use_sg;
	}



/*
	scsi_do_cmd sends all the commands out to the low-level driver.  It 
	handles the specifics required for each low level driver - ie queued 
	or non queued.  It also prevents conflicts when different high level 
	drivers go for the same host at the same time.
*/

void scsi_do_cmd (Scsi_Cmnd * SCpnt, const void *cmnd , 
		  void *buffer, unsigned bufflen, void (*done)(Scsi_Cmnd *),
		  int timeout, int retries 
		   )
        {
	struct Scsi_Host * host = SCpnt->host;

#ifdef DEBUG
	{
	int i;	
	int target = SCpnt->target;
	printk ("scsi_do_cmd (host = %d, target = %d, buffer =%08x, "
		"bufflen = %d, done = %08x, timeout = %d, retries = %d)\n"
		"command : " , host->host_no, target, buffer, bufflen, done, timeout, retries);
	for (i = 0; i < 10; ++i)
		printk ("%02x  ", ((unsigned char *) cmnd)[i]); 
	printk("\n");
      };
#endif
	
	if (!host)
		{
		panic ("Invalid or not present host.\n");
		}

	
/*
	We must prevent reentrancy to the lowlevel host driver.  This prevents 
	it - we enter a loop until the host we want to talk to is not busy.   
	Race conditions are prevented, as interrupts are disabled in between the
	time we check for the host being not busy, and the time we mark it busy
	ourselves.
*/

	SCpnt->pid = scsi_pid++; 

	while (1==1){
	  cli();
	  if (host->can_queue
	      && host->host_busy >= host->can_queue)
	    {
	      sti();
	      SCSI_SLEEP(&host->host_wait, 
			 (host->host_busy >= host->can_queue));
	    } else {
	      host->host_busy++;
	      sti();
	      break;
	    };
      };
/*
	Our own function scsi_done (which marks the host as not busy, disables 
	the timeout counter, etc) will be called by us or by the 
	scsi_hosts[host].queuecommand() function needs to also call
	the completion function for the high level driver.

*/

	memcpy ((void *) SCpnt->data_cmnd , (void *) cmnd, 12);
#if 0
	SCpnt->host = host;
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
				
	memcpy ((void *) SCpnt->cmnd , (void *) cmnd, 12);
	/* Zero the sense buffer.  Some host adapters automatically request
	   sense on error.  0 is not a valid sense code.  */
	memset ((void *) SCpnt->sense_buffer, 0, sizeof SCpnt->sense_buffer);
	SCpnt->request_buffer = buffer;
	SCpnt->request_bufflen = bufflen;
	SCpnt->old_use_sg = SCpnt->use_sg;

	/* Start the timer ticking.  */

	SCpnt->internal_timeout = 0;
	SCpnt->abort_reason = 0;
	internal_cmnd (SCpnt);

#ifdef DEBUG
	printk ("Leaving scsi_do_cmd()\n");
#endif
        }



/*
	The scsi_done() function disables the timeout timer for the scsi host, 
	marks the host as not busy, and calls the user specified completion 
	function for that host's current command.
*/

static void reset (Scsi_Cmnd * SCpnt)
{
#ifdef DEBUG
	printk("scsi: reset(%d)\n", SCpnt->host->host_no);
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
	};
#endif
}
	
	

static int check_sense (Scsi_Cmnd * SCpnt)
	{
  /* If there is no sense information, request it.  If we have already
     requested it, there is no point in asking again - the firmware must be
     confused. */
  if (((SCpnt->sense_buffer[0] & 0x70) >> 4) != 7) {
    if(!(SCpnt->flags & ASKED_FOR_SENSE))
      return SUGGEST_SENSE;
    else
      return SUGGEST_RETRY;
      }
  
  SCpnt->flags &= ~ASKED_FOR_SENSE;

#ifdef DEBUG_INIT
	printk("scsi%d : ", SCpnt->host->host_no);
	print_sense("", SCpnt);
	printk("\n");
#endif
	        if (SCpnt->sense_buffer[2] &0xe0)
		  return SUGGEST_ABORT;

		switch (SCpnt->sense_buffer[2] & 0xf)
		{
		case NO_SENSE:
			return 0;
		case RECOVERED_ERROR:
			if (SCpnt->device->type == TYPE_TAPE)
			  return SUGGEST_IS_OK;
			else
			  return 0;

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
 * will hang. If more than one of the above actions are taken by
 * scsi_done, then unpredictable behavior will result.
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
	   status to say why */
	if(host_byte(result) == DID_ABORT && SCpnt->abort_reason)
	  SCpnt->result = result = (result & 0xff00ffff) | 
	    (SCpnt->abort_reason << 16);


#define FINISHED 0
#define MAYREDO  1
#define REDO	 3
#define PENDING  4

#ifdef DEBUG
	printk("In scsi_done(host = %d, result = %06x)\n", host->host_no, result);
#endif
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
				printk("scsi%d : target %d lun %d request sense failed, performing reset.\n", 
					SCpnt->host->host_no, SCpnt->target, SCpnt->lun);
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
				printk("scsi%d : RESERVATION CONFLICT performing reset.\n", 
					SCpnt->host->host_no);
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
				panic("scsi: unsupported message byte %d received\n", msg_byte(result)); 
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
			    && !(SCpnt->flags & WAS_RESET))
			        {
					printk("scsi%d : reseting for second half of retries.\n",
						SCpnt->host->host_no);
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
			    internal_cmnd (SCpnt);
			  };
			break;	
		default: 
			INTERNAL_ERROR;
		}

	if (status == FINISHED) 
		{
		#ifdef DEBUG
			printk("Calling done function - at address %08x\n", SCpnt->done);
		#endif
		host->host_busy--; /* Indicate that we are free */
		wake_up(&host->host_wait);
		SCpnt->result = result | ((exit & 0xff) << 24);
		SCpnt->use_sg = SCpnt->old_use_sg;
		SCpnt->done (SCpnt);
		}


#undef FINISHED
#undef REDO
#undef MAYREDO
#undef PENDING
	}

/*
	The scsi_abort function interfaces with the abort() function of the host
	we are aborting, and causes the current command to not complete.  The 
	caller should deal with any error messages or status returned on the 
	next call.
	
	This will not be called reentrantly for a given host.
*/
	
/*
	Since we're nice guys and specified that abort() and reset()
	can be non-reentrant.  The internal_timeout flags are used for
	this.
*/


int scsi_abort (Scsi_Cmnd * SCpnt, int why)
	{
	int oldto;
	struct Scsi_Host * host = SCpnt->host;
	
	while(1)	
		{
		cli();
		if (SCpnt->internal_timeout & IN_ABORT) 
			{
			sti();
			while (SCpnt->internal_timeout & IN_ABORT);
			}
		else
			{	
			SCpnt->internal_timeout |= IN_ABORT;
			oldto = update_timeout(SCpnt, ABORT_TIMEOUT);

			if ((SCpnt->flags & IS_RESETTING) && 
			    SCpnt->device->soft_reset) {
			  /* OK, this command must have died when we did the
			     reset.  The device itself must have lied. */
			  printk("Stale command on %d:%d appears to have died when"
				 " the bus was reset\n", SCpnt->target, SCpnt->lun);
			}
			
			sti();
			if (!host->host_busy) {
			  SCpnt->internal_timeout &= ~IN_ABORT;
			  update_timeout(SCpnt, oldto);
			  return 0;
			}
			SCpnt->abort_reason = why;
			switch(host->hostt->abort(SCpnt)) {
			  /* We do not know how to abort.  Try waiting another
			     time increment and see if this helps. Set the
			     WAS_TIMEDOUT flag set so we do not try this twice
			     */
			case SCSI_ABORT_BUSY: /* Tough call - returning 1 from
						 this is too severe */
			case SCSI_ABORT_SNOOZE:
			  if(why == DID_TIME_OUT) {
			    cli();
			    SCpnt->internal_timeout &= ~IN_ABORT;
			    if(SCpnt->flags & WAS_TIMEDOUT) {
			      sti();
			      return 1; /* Indicate we cannot handle this.
					   We drop down into the reset handler
					   and try again */
			    } else {
			      SCpnt->flags |= WAS_TIMEDOUT;
			      oldto = SCpnt->timeout_per_command;
			      update_timeout(SCpnt, oldto);
			    }
			    sti();
			  }
			  return 0;
			case SCSI_ABORT_PENDING:
			  if(why != DID_TIME_OUT) {
			    cli();
			    update_timeout(SCpnt, oldto);
			    sti();
			  }
			  return 0;
			case SCSI_ABORT_SUCCESS:
			  /* We should have already aborted this one.  No
			     need to adjust timeout */
			case SCSI_ABORT_NOT_RUNNING:
			  SCpnt->internal_timeout &= ~IN_ABORT;
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
	Scsi_Cmnd * SCpnt1;
	struct Scsi_Host * host = SCpnt->host;
	
#ifdef DEBUG
	printk("Danger Will Robinson! - SCSI bus for host %d is being reset.\n",host->host_no);
#endif
	while (1) {
		cli();	
		if (SCpnt->internal_timeout & IN_RESET)
			{
			sti();
			while (SCpnt->internal_timeout & IN_RESET);
			}
		else
			{
			SCpnt->internal_timeout |= IN_RESET;
			oldto = update_timeout(SCpnt, RESET_TIMEOUT);	
					
			if (host->host_busy)
				{	
				sti();
				SCpnt1 = host->host_queue;
				while(SCpnt1) {
				  if (SCpnt1->request.dev > 0) {
#if 0				  
				    if (!(SCpnt1->flags & IS_RESETTING) && 
				      !(SCpnt1->internal_timeout & IN_ABORT))
				    scsi_abort(SCpnt1, DID_RESET);
#endif
				    SCpnt1->flags |= IS_RESETTING;
				  }
				  SCpnt1 = SCpnt1->next;
				};

				temp = host->hostt->reset(SCpnt);	
				}				
			else
				{
				host->host_busy++;
	
				sti();
				temp = host->hostt->reset(SCpnt);
				host->last_reset = jiffies;
				host->host_busy--;
				}

#ifdef DEBUG
			printk("scsi reset function returned %d\n", temp);
#endif
			switch(temp) {
			case SCSI_RESET_SUCCESS:
			  cli();
			  SCpnt->internal_timeout &= ~IN_RESET;
			  update_timeout(SCpnt, oldto);
			  sti();
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
			     so that this command does not time out any more,
			     and we return 1 so that we get a message on the
			     screen. */
			  cli();
			  SCpnt->internal_timeout &= ~IN_RESET;
			  update_timeout(SCpnt, 0);
			  sti();
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
		We must not enter update_timeout with a timeout condition still pending.
	*/

	int timed_out;
	struct Scsi_Host * host;
	Scsi_Cmnd * SCpnt = NULL;

	do 	{	
		cli();

		update_timeout(NULL, 0);
	/*
		Find all timers such that they have 0 or negative (shouldn't happen)
		time remaining on them.
	*/
			
		timed_out = 0;
		for(host = scsi_hostlist; host; host = host->next) {
		  for(SCpnt = host->host_queue; SCpnt; SCpnt = SCpnt->next)
		    if (SCpnt->timeout == -1)
		      {
			sti();
			SCpnt->timeout = 0;
			scsi_times_out(SCpnt);
			++timed_out; 
			cli();
		      }
		};
	      } while (timed_out);	
	sti();
      }

/*
	The strategy is to cause the timer code to call scsi_times_out()
	when the soonest timeout is pending.  
	The arguments are used when we are queueing a new command, because
	we do not want to subtract the time used from this time, but when we
	set the timer, we want to take this value into account.
*/
	
static int update_timeout(Scsi_Cmnd * SCset, int timeout)
	{
	unsigned int least, used;
	unsigned int oldto;
	struct Scsi_Host * host;
	Scsi_Cmnd * SCpnt = NULL;

	cli();

/* 
	Figure out how much time has passed since the last time the timeouts 
   	were updated 
*/
	used = (time_start) ? (jiffies - time_start) : 0;

/*
	Find out what is due to timeout soonest, and adjust all timeouts for
	the amount of time that has passed since the last time we called 
	update_timeout. 
*/
	
	oldto = 0;

	if(SCset){
	  oldto = SCset->timeout - used;
	  SCset->timeout = timeout + used;
	};

	least = 0xffffffff;

	for(host = scsi_hostlist; host; host = host->next)
	  for(SCpnt = host->host_queue; SCpnt; SCpnt = SCpnt->next)
	    if (SCpnt->timeout > 0) {
	      SCpnt->timeout -= used;
	      if(SCpnt->timeout <= 0) SCpnt->timeout = -1;
	      if(SCpnt->timeout > 0 && SCpnt->timeout < least)
		least = SCpnt->timeout;
	    };

/*
	If something is due to timeout again, then we will set the next timeout 
	interrupt to occur.  Otherwise, timeouts are disabled.
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
	sti();
	return oldto;
	}		


static unsigned short * dma_malloc_freelist = NULL;
static unsigned int dma_sectors = 0;
unsigned int dma_free_sectors = 0;
unsigned int need_isa_buffer = 0;
static unsigned char * dma_malloc_buffer = NULL;

void *scsi_malloc(unsigned int len)
{
  unsigned int nbits, mask;
  int i, j;
  if((len & 0x1ff) || len > 8192)
    return NULL;
  
  cli();
  nbits = len >> 9;
  mask = (1 << nbits) - 1;
  
  for(i=0;i < (dma_sectors >> 4); i++)
    for(j=0; j<17-nbits; j++){
      if ((dma_malloc_freelist[i] & (mask << j)) == 0){
	dma_malloc_freelist[i] |= (mask << j);
	sti();
	dma_free_sectors -= nbits;
#ifdef DEBUG
	printk("SMalloc: %d %x ",len, dma_malloc_buffer + (i << 13) + (j << 9));
#endif
	return (void *) ((unsigned long) dma_malloc_buffer + (i << 13) + (j << 9));
      };
    };
  sti();
  return NULL;  /* Nope.  No more */
}

int scsi_free(void *obj, unsigned int len)
{
  int offset;
  int page, sector, nbits, mask;

#ifdef DEBUG
  printk("Sfree %x %d\n",obj, len);
#endif

  offset = ((int) obj) - ((int) dma_malloc_buffer);

  if (offset < 0) panic("Bad offset");
  page = offset >> 13;
  sector = offset >> 9;
  if(sector >= dma_sectors) panic ("Bad page");

  sector = (offset >> 9) & 15;
  nbits = len >> 9;
  mask = (1 << nbits) - 1;

  if ((mask << sector) > 0xffff) panic ("Bad memory alignment");

  cli();
  if(dma_malloc_freelist[page] & (mask << sector) != (mask<<sector))
    panic("Trying to free unused memory");

  dma_free_sectors += nbits;
  dma_malloc_freelist[page] &= ~(mask << sector);
  sti();
  return 0;
}


/* These are special functions that can be used to obtain memory at boot time.
   They act line a malloc function, but they simply take memory from the
   pool */

static unsigned int scsi_init_memory_start = 0;
int scsi_loadable_module_flag; /* Set after we scan builtin drivers */

void * scsi_init_malloc(unsigned int size)
{
  unsigned int retval;
  if(scsi_loadable_module_flag) {
    retval = (unsigned int) kmalloc(size, GFP_ATOMIC);
  } else {
    retval = scsi_init_memory_start;
    scsi_init_memory_start += size;
  }
  return (void *) retval;
}


void scsi_init_free(char * ptr, unsigned int size)
{ /* FIXME - not right.  We need to compare addresses to see whether this was
     kmalloc'd or not */
  if((unsigned int) ptr < scsi_loadable_module_flag) {
    kfree(ptr);
  } else {
    if(((unsigned int) ptr) + size == scsi_init_memory_start)
      scsi_init_memory_start = (unsigned int) ptr;
  }
}

/*
	scsi_dev_init() is our initialization routine, which in turn calls host 
	initialization, bus scanning, and sd/st initialization routines.  It 
	should be called from main().
*/

unsigned long scsi_dev_init (unsigned long memory_start,unsigned long memory_end)
	{
	struct Scsi_Host * host = NULL;
	Scsi_Device * SDpnt;
	struct Scsi_Host * shpnt;
	struct Scsi_Device_Template * sdtpnt;
	Scsi_Cmnd * SCpnt;
#ifdef FOO_ON_YOU
	return;
#endif	

	/* Init a few things so we can "malloc" memory. */
	scsi_loadable_module_flag = 0;
	scsi_init_memory_start = memory_start;

	timer_table[SCSI_TIMER].fn = scsi_main_timeout;
	timer_table[SCSI_TIMER].expires = 0;

	/* initialize all hosts */
	scsi_init(); 
				
	scsi_devices = (Scsi_Device *) NULL;

	for (shpnt = scsi_hostlist; shpnt; shpnt = shpnt->next)
	  scan_scsis(shpnt);           /* scan for scsi devices */

	for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	  if(sdtpnt->init && sdtpnt->dev_noticed) (*sdtpnt->init)();

	for (SDpnt=scsi_devices; SDpnt; SDpnt = SDpnt->next) {
	  int j;
	  SDpnt->scsi_request_fn = NULL;
	  for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	    if(sdtpnt->attach) (*sdtpnt->attach)(SDpnt);
	  if(SDpnt->type != -1){
	    for(j=0;j<SDpnt->host->hostt->cmd_per_lun;j++){
	      SCpnt = (Scsi_Cmnd *) scsi_init_malloc(sizeof(Scsi_Cmnd));
	      SCpnt->host = SDpnt->host;
	      SCpnt->device = SDpnt;
	      SCpnt->target = SDpnt->id;
	      SCpnt->lun = SDpnt->lun;
	      SCpnt->request.dev = -1; /* Mark not busy */
	      SCpnt->use_sg = 0;
	      SCpnt->old_use_sg = 0;
              SCpnt->underflow = 0;
              SCpnt->transfersize = 0;
	      SCpnt->host_scribble = NULL;
	      host = SDpnt->host;
	      if(host->host_queue)
		host->host_queue->prev = SCpnt;
	      SCpnt->next = host->host_queue;
	      SCpnt->prev = NULL;
	      host->host_queue = SCpnt;
	    };
	  };
	};

	if (scsi_devicelist)
	  dma_sectors = 16;  /* Base value we use */

	for (SDpnt=scsi_devices; SDpnt; SDpnt = SDpnt->next) {
	  host = SDpnt->host;
	  
	  if(SDpnt->type != TYPE_TAPE)
	    dma_sectors += ((host->sg_tablesize * 
			     sizeof(struct scatterlist) + 511) >> 9) * 
			       host->hostt->cmd_per_lun;
	  
	  if(host->unchecked_isa_dma &&
	     memory_end - 1 > ISA_DMA_THRESHOLD &&
	     SDpnt->type != TYPE_TAPE) {
	    dma_sectors += (PAGE_SIZE >> 9) * host->sg_tablesize *
	      host->hostt->cmd_per_lun;
	    need_isa_buffer++;
	  };
	};

	dma_sectors = (dma_sectors + 15) & 0xfff0;
	dma_free_sectors = dma_sectors;  /* This must be a multiple of 16 */

	scsi_init_memory_start = (scsi_init_memory_start + 3) & 0xfffffffc;
	dma_malloc_freelist = (unsigned short *) 
	  scsi_init_malloc(dma_sectors >> 3);
	memset(dma_malloc_freelist, 0, dma_sectors >> 3);

	/* Some host adapters require buffers to be word aligned */
	if(scsi_init_memory_start & 1) scsi_init_memory_start++;

	dma_malloc_buffer = (unsigned char *) 
	  scsi_init_malloc(dma_sectors << 9);
	
	/* OK, now we finish the initialization by doing spin-up, read
	   capacity, etc, etc */
	for(sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
	  if(sdtpnt->finish && sdtpnt->nr_dev)
	    (*sdtpnt->finish)();
	
	scsi_loadable_module_flag = 1;
	return scsi_init_memory_start;
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

#ifdef DEBUG_TIMEOUT
static void 
scsi_dump_status(void)
{
  int i, i1;
  Scsi_Host * shpnt;
  Scsi_Cmnd * SCpnt;
  printk("Dump of scsi parameters:\n");
  for(shpnt = scsi_hosts; shpnt; shpnt = shpnt->next)
    for(SCpnt=shpnt->host_queue; SCpnt; SCpnt = SCpnt->next)
      {
	/*  (0) 0:0:0 (802 123434 8 8 0) (3 3 2) (%d %d %d) %d %x      */
	printk("(%d) %d:%d:%d (%4.4x %d %d %d %d) (%d %d %x) (%d %d %d) %x %x %x\n",
	       i, SCpnt->host->host_no,
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
      };
  printk("wait_for_request = %x\n", wait_for_request);
  /* Now dump the request lists for each block device */
  printk("Dump of pending block device requests\n");
  for(i=0; i<MAX_BLKDEV; i++)
    if(blk_dev[i].current_request)
      {
	struct request * req;
	printk("%d: ", i);
	req = blk_dev[i].current_request;
	while(req) {
	  printk("(%x %d %d %d %d) ",
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



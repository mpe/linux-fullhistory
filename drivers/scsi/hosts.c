/*
 *	hosts.c Copyright (C) 1992 Drew Eckhardt
 *	        Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *
 *	mid to lowlevel SCSI driver interface
 *		Initial versions: Drew Eckhardt
 *		Subsequent revisions: Eric Youngdale
 *
 *	<drew@colorado.edu>
 */


/*
 *	This file contains the medium level SCSI
 *	host interface initialization, as well as the scsi_hosts array of SCSI
 *	hosts currently present in the system.
 */

#include <linux/config.h>
#include "../block/blk.h"
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>

#include "scsi.h"

#ifndef NULL
#define NULL 0L
#endif

#define HOSTS_C

#include "hosts.h"

#ifdef CONFIG_SCSI_AHA152X
#include "aha152x.h"
#endif

#ifdef CONFIG_SCSI_AHA1542
#include "aha1542.h"
#endif

#ifdef CONFIG_SCSI_AHA1740
#include "aha1740.h"
#endif

#ifdef CONFIG_SCSI_AHA274X
#include "aha274x.h"
#endif

#ifdef CONFIG_SCSI_BUSLOGIC
#include "buslogic.h"
#endif

#ifdef CONFIG_SCSI_EATA_DMA
#include "eata_dma.h"
#endif

#ifdef CONFIG_SCSI_U14_34F
#include "u14-34f.h"
#endif

#ifdef CONFIG_SCSI_FUTURE_DOMAIN
#include "fdomain.h"
#endif

#ifdef CONFIG_SCSI_GENERIC_NCR5380
#include "g_NCR5380.h"
#endif

#ifdef CONFIG_SCSI_IN2000
#include "in2000.h"
#endif

#ifdef CONFIG_SCSI_PAS16
#include "pas16.h"
#endif

#ifdef CONFIG_SCSI_QLOGIC
#include "qlogic.h"
#endif

#ifdef CONFIG_SCSI_SEAGATE
#include "seagate.h"
#endif

#ifdef CONFIG_SCSI_T128
#include "t128.h"
#endif

#ifdef CONFIG_SCSI_NCR53C7xx
#include "53c7,8xx.h"
#endif

#ifdef CONFIG_SCSI_ULTRASTOR
#include "ultrastor.h"
#endif

#ifdef CONFIG_SCSI_7000FASST
#include "wd7000.h"
#endif

#ifdef CONFIG_SCSI_EATA
#include "eata.h"
#endif

#ifdef CONFIG_SCSI_DEBUG
#include "scsi_debug.h"
#endif

/*
static const char RCSid[] = "$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/hosts.c,v 1.3 1993/09/24 12:21:00 drew Exp drew $";
*/

/*
 *	The scsi host entries should be in the order you wish the
 *	cards to be detected.  A driver may appear more than once IFF
 *	it can deal with being detected (and therefore initialized)
 *	with more than one simultaneous host number, can handle being
 *	reentrant, etc.
 *
 *	They may appear in any order, as each SCSI host  is told which host number it is
 *	during detection.
 */

/* This is a placeholder for controllers that are not configured into
   the system - we do this to ensure that the controller numbering is
   always consistent, no matter how the kernel is configured. */

#define NO_CONTROLLER {NULL, NULL, NULL, NULL, NULL, NULL, NULL, \
	        NULL, NULL, 0, 0, 0, 0, 0, 0}

/*
 *	When figure is run, we don't want to link to any object code.  Since
 *	the macro for each host will contain function pointers, we cannot
 *	use it and instead must use a "blank" that does no such
 *	idiocy.
 */

Scsi_Host_Template * scsi_hosts = NULL;

static Scsi_Host_Template builtin_scsi_hosts[] =
	{
#ifdef CONFIG_SCSI_U14_34F
	ULTRASTOR_14_34F,
#endif
#ifdef CONFIG_SCSI_ULTRASTOR
	ULTRASTOR_14F,
#endif
#ifdef CONFIG_SCSI_AHA152X
	AHA152X,
#endif
/* Buslogic must come before aha1542.c */
#ifdef CONFIG_SCSI_BUSLOGIC
	BUSLOGIC,
#endif
#ifdef CONFIG_SCSI_AHA1542
	AHA1542,
#endif
#ifdef CONFIG_SCSI_AHA1740
	AHA1740,
#endif
#ifdef CONFIG_SCSI_AHA274X
	AHA274X,
#endif
#ifdef CONFIG_SCSI_FUTURE_DOMAIN
	FDOMAIN_16X0,
#endif
#ifdef CONFIG_SCSI_IN2000
	IN2000,
#endif
#ifdef CONFIG_SCSI_GENERIC_NCR5380
	GENERIC_NCR5380,
#endif
#ifdef CONFIG_SCSI_QLOGIC
	QLOGIC,
#endif
#ifdef CONFIG_SCSI_PAS16
	MV_PAS16,
#endif
#ifdef CONFIG_SCSI_SEAGATE
	SEAGATE_ST0X,
#endif
#ifdef CONFIG_SCSI_T128
	TRANTOR_T128,
#endif
#ifdef CONFIG_SCSI_NCR53C7xx
	NCR53c7xx,
#endif
#ifdef CONFIG_SCSI_EATA_DMA
	EATA_DMA,
#endif
#ifdef CONFIG_SCSI_7000FASST
	WD7000,
#endif
#ifdef CONFIG_SCSI_EATA
	EATA,
#endif
#ifdef CONFIG_SCSI_DEBUG
	SCSI_DEBUG,
#endif
	};

#define MAX_SCSI_HOSTS (sizeof(builtin_scsi_hosts) / sizeof(Scsi_Host_Template))

/*
 *	Our semaphores and timeout counters, where size depends on MAX_SCSI_HOSTS here.
 */

struct Scsi_Host * scsi_hostlist = NULL;
struct Scsi_Device_Template * scsi_devicelist;

int max_scsi_hosts = 0;
int next_scsi_host = 0;

void
scsi_unregister(struct Scsi_Host * sh){
	struct Scsi_Host * shpnt;

	if(scsi_hostlist == sh)
		scsi_hostlist = sh->next;
	else {
		shpnt = scsi_hostlist;
		while(shpnt->next != sh) shpnt = shpnt->next;
		shpnt->next = shpnt->next->next;
	};

        /* If we are removing the last host registered, it is safe to reuse
           its host number (this avoids "holes" at boot time) (DB) */
        if (max_scsi_hosts == next_scsi_host && !scsi_loadable_module_flag)
           max_scsi_hosts--;

	next_scsi_host--;
	scsi_init_free((char *) sh, sizeof(struct Scsi_Host) + sh->extra_bytes);
}

/* We call this when we come across a new host adapter. We only do this
   once we are 100% sure that we want to use this host adapter -  it is a
   pain to reverse this, so we try and avoid it */

struct Scsi_Host * scsi_register(Scsi_Host_Template * tpnt, int j){
	struct Scsi_Host * retval, *shpnt;
	retval = (struct Scsi_Host *)scsi_init_malloc(sizeof(struct Scsi_Host) + j,
						      (tpnt->unchecked_isa_dma && j ? GFP_DMA : 0) | GFP_ATOMIC);
	retval->host_busy = 0;
	retval->block = NULL;
	retval->wish_block = 0;
	if(j > 0xffff) panic("Too many extra bytes requested\n");
	retval->extra_bytes = j;
	retval->loaded_as_module = scsi_loadable_module_flag;
	retval->host_no = max_scsi_hosts++; /* never reuse host_no (DB) */
	next_scsi_host++;
	retval->host_queue = NULL;
	retval->host_wait = NULL;
	retval->last_reset = 0;
	retval->irq = 0;
	retval->dma_channel = 0xff;
	retval->io_port = 0;
	retval->forbidden_addr = 0;
	retval->forbidden_size = 0;
	retval->hostt = tpnt;
	retval->next = NULL;
#ifdef DEBUG
	printk("Register %x %x: %d\n", (int)retval, (int)retval->hostt, j);
#endif

	/* The next four are the default values which can be overridden
	   if need be */
	retval->this_id = tpnt->this_id;
	retval->can_queue = tpnt->can_queue;
	retval->sg_tablesize = tpnt->sg_tablesize;
	retval->cmd_per_lun = tpnt->cmd_per_lun;
	retval->unchecked_isa_dma = tpnt->unchecked_isa_dma;

	if(!scsi_hostlist)
		scsi_hostlist = retval;
	else
	{
		shpnt = scsi_hostlist;
		while(shpnt->next) shpnt = shpnt->next;
		shpnt->next = retval;
	}

	return retval;
}

int
scsi_register_device(struct Scsi_Device_Template * sdpnt)
{
  if(sdpnt->next) panic("Device already registered");
  sdpnt->next = scsi_devicelist;
  scsi_devicelist = sdpnt;
  return 0;
}

unsigned int scsi_init()
{
	static int called = 0;
	int i, pcount;
	Scsi_Host_Template * tpnt;
	struct Scsi_Host * shpnt;
	const char * name;

	if(called) return 0;

	called = 1;
	for (tpnt = &builtin_scsi_hosts[0], i = 0; i < MAX_SCSI_HOSTS; ++i, tpnt++)
	{
		/*
		 * Initialize our semaphores.  -1 is interpreted to mean
		 * "inactive" - where as 0 will indicate a time out condition.
		 */

		pcount = next_scsi_host;
		if ((tpnt->detect) &&
		    (tpnt->present =
		     tpnt->detect(tpnt)))
		{
			/* The only time this should come up is when people use
			   some kind of patched driver of some kind or another. */
			if(pcount == next_scsi_host) {
				if(tpnt->present > 1)
					panic("Failure to register low-level scsi driver");
				/* The low-level driver failed to register a driver.  We
 				   can do this now. */
				scsi_register(tpnt,0);
			};
			tpnt->next = scsi_hosts;
			scsi_hosts = tpnt;
		}
	}


	for(shpnt=scsi_hostlist; shpnt; shpnt = shpnt->next)
	    {
	      if(shpnt->hostt->info)
		name = shpnt->hostt->info(shpnt);
	      else
		name = shpnt->hostt->name;
	      printk ("scsi%d : %s\n", /* And print a little message */
		      shpnt->host_no, name);
	    }

	printk ("scsi : %d host%s.\n", next_scsi_host,
		(next_scsi_host == 1) ? "" : "s");

	scsi_make_blocked_list();

	/* Now attach the high level drivers */
#ifdef CONFIG_BLK_DEV_SD
	scsi_register_device(&sd_template);
#endif
#ifdef CONFIG_BLK_DEV_SR
	scsi_register_device(&sr_template);
#endif
#ifdef CONFIG_CHR_DEV_ST
	scsi_register_device(&st_template);
#endif
#ifdef CONFIG_CHR_DEV_SG
	scsi_register_device(&sg_template);
#endif

#if 0      
	max_scsi_hosts = next_scsi_host;
#endif
	return 0;
}


void scsi_mem_init(unsigned long memory_end)
{
    struct Scsi_Host *Host;
    long High8, Low24;
    for (Host = scsi_hostlist; Host != NULL; Host = Host->next) {
	if (Host->forbidden_addr > 0 && Host->forbidden_size > 0) {
	    for (High8 = 1<<24; High8 < memory_end; High8 += 1<<24) {
		for (Low24 = Host->forbidden_addr;
		     Low24 < Host->forbidden_addr + Host->forbidden_size;
		     Low24 += PAGE_SIZE) {
		    unsigned long ForbiddenAddress = High8 + Low24;
		    if (ForbiddenAddress >= memory_end) goto next_host;
		    mem_map[MAP_NR(ForbiddenAddress)] = MAP_PAGE_RESERVED;
		}
	    }
	}
      next_host:
	continue;
    }
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */

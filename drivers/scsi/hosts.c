/*
 *  hosts.c Copyright (C) 1992 Drew Eckhardt
 *          Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *
 *  mid to lowlevel SCSI driver interface
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 */


/*
 *  This file contains the medium level SCSI
 *  host interface initialization, as well as the scsi_hosts array of SCSI
 *  hosts currently present in the system.
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/config.h>
#include <linux/blk.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include "scsi.h"

#ifndef NULL
#define NULL 0L
#endif

#define HOSTS_C

#include "hosts.h"

#if defined(CONFIG_A4000T_SCSI) || \
    defined(CONFIG_WARPENGINE_SCSI) || \
    defined(CONFIG_A4091_SCSI) || \
    defined (CONFIG_GVP_TURBO_SCSI) || \
    defined (CONFIG_BLZ603E)
#define AMIGA7XXCONFIG
#endif

#ifdef AMIGA7XXCONFIG
#include "amiga7xx.h"
#endif

#ifdef CONFIG_MVME16x_SCSI
#include "mvme16x.h"
#endif

#ifdef CONFIG_BVME6000_SCSI
#include "bvme6000.h"
#endif

#ifdef CONFIG_A3000_SCSI
#include "a3000.h"
#endif

#ifdef CONFIG_A2091_SCSI
#include "a2091.h"
#endif

#ifdef CONFIG_GVP11_SCSI
#include "gvp11.h"
#endif

#ifdef CONFIG_CYBERSTORM_SCSI
#include "cyberstorm.h"
#endif

#ifdef CONFIG_CYBERSTORMII_SCSI
#include "cyberstormII.h"
#endif

#ifdef CONFIG_BLZ2060_SCSI
#include "blz2060.h"
#endif

#ifdef CONFIG_BLZ1230_SCSI
#include "blz1230.h"
#endif

#ifdef CONFIG_FASTLANE_SCSI
#include "fastlane.h"
#endif

#ifdef CONFIG_ATARI_SCSI
#include "atari_scsi.h"
#endif

#ifdef CONFIG_MAC_SCSI_OLD
#include "mac_scsi.h"
#endif

#ifdef CONFIG_MAC_SCSI
#include "mac_scsinew.h"
#endif

#ifdef CONFIG_SCSI_MAC_ESP
#include "mac_esp.h"
#endif

#ifdef CONFIG_SCSI_ADVANSYS
#include "advansys.h"
#endif

#ifdef CONFIG_SCSI_AHA152X
#include "aha152x.h"
#endif

#ifdef CONFIG_SCSI_AHA1542
#include "aha1542.h"
#endif

#ifdef CONFIG_SCSI_AHA1740
#include "aha1740.h"
#endif

#ifdef CONFIG_SCSI_AIC7XXX
#include "aic7xxx.h"
#endif

#ifdef CONFIG_SCSI_BUSLOGIC
#include "BusLogic.h"
#endif

#ifdef CONFIG_SCSI_EATA_DMA
#include "eata_dma.h"
#endif

#ifdef CONFIG_SCSI_EATA_PIO
#include "eata_pio.h"
#endif

#ifdef CONFIG_SCSI_U14_34F
#include "u14-34f.h"
#endif

#ifdef CONFIG_SCSI_FD_MCS
#include "fd_mcs.h"
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

#ifdef CONFIG_SCSI_QLOGIC_FAS
#include "qlogicfas.h"
#endif

#ifdef CONFIG_SCSI_QLOGIC_ISP
#include "qlogicisp.h"
#endif

#ifdef CONFIG_SCSI_QLOGIC_FC
#include "qlogicfc.h"
#endif

#ifdef CONFIG_SCSI_SEAGATE
#include "seagate.h"
#endif

#ifdef CONFIG_SCSI_T128
#include "t128.h"
#endif

#ifdef CONFIG_SCSI_DTC3280
#include "dtc.h"
#endif

#ifdef CONFIG_SCSI_NCR53C7xx
#include "53c7,8xx.h"
#endif

#ifdef CONFIG_SCSI_NCR53C8XX
#include "ncr53c8xx.h"
#endif

#ifdef CONFIG_SCSI_ULTRASTOR
#include "ultrastor.h"
#endif

#ifdef CONFIG_SCSI_7000FASST
#include "wd7000.h"
#endif

#ifdef CONFIG_SCSI_MCA_53C9X
#include "mca_53c9x.h"
#endif

#ifdef CONFIG_SCSI_IBMMCA
#include "ibmmca.h"
#endif

#ifdef CONFIG_SCSI_EATA
#include "eata.h"
#endif

#ifdef CONFIG_SCSI_NCR53C406A
#include "NCR53c406a.h"
#endif

#ifdef CONFIG_SCSI_DC390T
#include "dc390.h"
#endif

#ifdef CONFIG_SCSI_AM53C974
#include "AM53C974.h"
#endif

#ifdef CONFIG_SCSI_MEGARAID
#include "megaraid.h"
#endif

#ifdef CONFIG_SCSI_ACARD
#include "atp870u.h"
#endif

#ifdef CONFIG_SCSI_SUNESP
#include "esp.h"
#endif

#ifdef CONFIG_SCSI_QLOGICPTI
#include "qlogicpti.h"
#endif

#ifdef CONFIG_BLK_DEV_IDESCSI
#include "ide-scsi.h"
#endif

#ifdef CONFIG_SCSI_MESH
#include "mesh.h"
#endif

#ifdef CONFIG_SCSI_MAC53C94
#include "mac53c94.h"
#endif

#ifdef CONFIG_SCSI_GDTH
#include "gdth.h"
#endif

#ifdef CONFIG_SCSI_PCI2000
#include "pci2000.h"
#endif

#ifdef CONFIG_SCSI_PCI2220I
#include "pci2220i.h"
#endif

#ifdef CONFIG_SCSI_PSI240I
#include "psi240i.h"
#endif

#ifdef CONFIG_SCSI_PLUTO
#include "pluto.h"
#endif

#ifdef CONFIG_SCSI_INITIO
#include "ini9100u.h"
#endif

#ifdef CONFIG_SCSI_DEBUG
#include "scsi_debug.h"
#endif

#ifdef CONFIG_SCSI_ACORNSCSI_3
#include "../acorn/scsi/acornscsi.h"
#endif

#ifdef CONFIG_SCSI_CUMANA_1
#include "../acorn/scsi/cumana_1.h"
#endif

#ifdef CONFIG_SCSI_CUMANA_2
#include "../acorn/scsi/cumana_2.h"
#endif

#ifdef CONFIG_SCSI_ECOSCSI
#include "../acorn/scsi/ecoscsi.h"
#endif

#ifdef CONFIG_SCSI_OAK1
#include "../acorn/scsi/oak.h"
#endif

#ifdef CONFIG_SCSI_POWERTECSCSI
#include "../acorn/scsi/powertec.h"
#endif

#ifdef CONFIG_JAZZ_ESP
#include "jazz_esp.h"
#endif

/*
 * Moved ppa driver to the end of the probe list
 * since it is a removable host adapter.
 * This means the parallel ZIP drive will not bump
 * the order of the /dev/sd devices - campbell@torque.net
 */
#ifdef CONFIG_SCSI_PPA
#include "ppa.h"
#endif

#ifdef CONFIG_SCSI_IMM
#include "imm.h"
#endif

/*
static const char RCSid[] = "$Header: /vger/u4/cvs/linux/drivers/scsi/hosts.c,v 1.20 1996/12/12 19:18:32 davem Exp $";
*/

/*
 *  The scsi host entries should be in the order you wish the
 *  cards to be detected.  A driver may appear more than once IFF
 *  it can deal with being detected (and therefore initialized)
 *  with more than one simultaneous host number, can handle being
 *  reentrant, etc.
 *
 *  They may appear in any order, as each SCSI host is told which host 
 *  number it is during detection.
 */

/* This is a placeholder for controllers that are not configured into
 * the system - we do this to ensure that the controller numbering is
 * always consistent, no matter how the kernel is configured. */

#define NO_CONTROLLER {NULL, NULL, NULL, NULL, NULL, NULL, NULL, \
			   NULL, NULL, 0, 0, 0, 0, 0, 0}

/*
 *  When figure is run, we don't want to link to any object code.  Since
 *  the macro for each host will contain function pointers, we cannot
 *  use it and instead must use a "blank" that does no such
 *  idiocy.
 */

Scsi_Host_Template * scsi_hosts = NULL;

static Scsi_Host_Template builtin_scsi_hosts[] =
{
#ifdef CONFIG_AMIGA
#ifdef AMIGA7XXCONFIG
	AMIGA7XX_SCSI,
#endif
#ifdef CONFIG_A3000_SCSI
	A3000_SCSI,
#endif
#ifdef CONFIG_A2091_SCSI
	A2091_SCSI,
#endif
#ifdef CONFIG_GVP11_SCSI
	GVP11_SCSI,
#endif
#ifdef CONFIG_CYBERSTORM_SCSI
	SCSI_CYBERSTORM,
#endif
#ifdef CONFIG_CYBERSTORMII_SCSI
	SCSI_CYBERSTORMII,
#endif
#ifdef CONFIG_BLZ2060_SCSI
	SCSI_BLZ2060,
#endif
#ifdef CONFIG_BLZ1230_SCSI
	SCSI_BLZ1230,
#endif
#ifdef CONFIG_FASTLANE_SCSI
	SCSI_FASTLANE,
#endif
#endif

#ifdef CONFIG_ATARI
#ifdef CONFIG_ATARI_SCSI
	ATARI_SCSI,
#endif
#endif

#ifdef CONFIG_MAC
#ifdef CONFIG_MAC_SCSI_OLD
	MAC_SCSI,
#endif
#ifdef CONFIG_SCSI_MAC_ESP
        SCSI_MAC_ESP,
#endif
#ifdef CONFIG_MAC_SCSI
	MAC_NCR5380,
#endif
#endif

#ifdef CONFIG_MVME16x_SCSI
	MVME16x_SCSI,
#endif
#ifdef CONFIG_BVME6000_SCSI
	BVME6000_SCSI,
#endif
#ifdef CONFIG_SCSI_ADVANSYS
	ADVANSYS,
#endif

#ifdef CONFIG_SCSI_PCI2000
        PCI2000,
#endif
#ifdef CONFIG_SCSI_PCI2220I
        PCI2220I,
#endif
#ifdef CONFIG_SCSI_PSI240I
        PSI240I,
#endif

/* BusLogic must come before aha1542.c */
#ifdef CONFIG_SCSI_BUSLOGIC
    BUSLOGIC,
#endif
#ifdef CONFIG_SCSI_U14_34F
    ULTRASTOR_14_34F,
#endif
#ifdef CONFIG_SCSI_ULTRASTOR
    ULTRASTOR_14F,
#endif
#ifdef CONFIG_SCSI_AHA152X
    AHA152X,
#endif
#ifdef CONFIG_SCSI_AHA1542
    AHA1542,
#endif
#ifdef CONFIG_SCSI_AHA1740
    AHA1740,
#endif
#ifdef CONFIG_SCSI_AIC7XXX
    AIC7XXX,
#endif
#ifdef CONFIG_FD_MCS
   FD_MCS,
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
#ifdef CONFIG_SCSI_NCR53C406A	/* 53C406A should come before QLOGIC */
    NCR53c406a,
#endif
#ifdef CONFIG_SCSI_QLOGIC_FAS
    QLOGICFAS,
#endif
#ifdef CONFIG_SCSI_QLOGIC_ISP
    QLOGICISP,
#endif
#ifdef CONFIG_SCSI_QLOGIC_FC
    QLOGICFC,
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
#ifdef CONFIG_SCSI_DTC3280
        DTC3x80,
#endif
#ifdef CONFIG_SCSI_NCR53C7xx
    NCR53c7xx,
#endif
#ifdef CONFIG_SCSI_NCR53C8XX
    NCR53C8XX,
#endif
#ifdef CONFIG_SCSI_EATA_DMA
    EATA_DMA,
#endif
#ifdef CONFIG_SCSI_EATA_PIO
    EATA_PIO,
#endif
#ifdef CONFIG_SCSI_7000FASST
    WD7000,
#endif
#ifdef CONFIG_SCSI_MCA_53C9X
    MCA_53C9X,
#endif
#ifdef CONFIG_SCSI_IBMMCA
    IBMMCA,
#endif
#ifdef CONFIG_SCSI_EATA
    EATA,
#endif
#ifdef CONFIG_SCSI_DC390T
    DC390_T,
#endif
#ifdef CONFIG_SCSI_AM53C974
    AM53C974,
#endif
#ifdef CONFIG_SCSI_MEGARAID
    MEGARAID,
#endif
#ifdef CONFIG_SCSI_ACARD
    ATP870U,
#endif
#ifdef CONFIG_SCSI_SUNESP
    SCSI_SPARC_ESP,
#endif
#ifdef CONFIG_SCSI_GDTH
    GDTH,
#endif
#ifdef CONFIG_SCSI_INITIO
    INI9100U,
#endif
#ifdef CONFIG_SCSI_QLOGICPTI
    QLOGICPTI,
#endif
#ifdef CONFIG_BLK_DEV_IDESCSI
    IDESCSI,
#endif
#ifdef CONFIG_SCSI_MESH
    SCSI_MESH,
#endif
#ifdef CONFIG_SCSI_MAC53C94
    SCSI_MAC53C94,
#endif
#ifdef CONFIG_SCSI_PLUTO
    PLUTO,
#endif
#ifdef CONFIG_ARCH_ACORN
#ifdef CONFIG_SCSI_ACORNSCSI_3
    ACORNSCSI_3,
#endif
#ifdef CONFIG_SCSI_CUMANA_1
    CUMANA_NCR5380,
#endif
#ifdef CONFIG_SCSI_CUMANA_2
    CUMANA_FAS216,
#endif
#ifdef CONFIG_SCSI_ECOSCSI
    ECOSCSI_NCR5380,
#endif
#ifdef CONFIG_SCSI_OAK1
    OAK_NCR5380,
#endif
#ifdef CONFIG_SCSI_POWERTECSCSI
    POWERTECSCSI,
#endif
#endif
/* "Removable host adapters" below this line (Parallel Port/USB/other) */
#ifdef CONFIG_SCSI_PPA
    PPA,
#endif
#ifdef CONFIG_SCSI_IMM
    IMM,
#endif
#ifdef CONFIG_SCSI_DEBUG
    SCSI_DEBUG,
#endif
};

#define MAX_SCSI_HOSTS (sizeof(builtin_scsi_hosts) / sizeof(Scsi_Host_Template))


/*
 *  Our semaphores and timeout counters, where size depends on 
 *      MAX_SCSI_HOSTS here.
 */

struct Scsi_Host * scsi_hostlist = NULL;
struct Scsi_Device_Template * scsi_devicelist = NULL;

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
    }
    
    /* If we are removing the last host registered, it is safe to reuse
     * its host number (this avoids "holes" at boot time) (DB) 
     * It is also safe to reuse those of numbers directly below which have
     * been released earlier (to avoid some holes in numbering).
     */
    if(sh->host_no == max_scsi_hosts - 1) {
	while(--max_scsi_hosts >= next_scsi_host) {
	    shpnt = scsi_hostlist;
	    while(shpnt && shpnt->host_no != max_scsi_hosts - 1)
		shpnt = shpnt->next;
	    if(shpnt)
		break;
	}
    }
    next_scsi_host--;
    scsi_init_free((char *) sh, sizeof(struct Scsi_Host) + sh->extra_bytes);
}

/* We call this when we come across a new host adapter. We only do this
 * once we are 100% sure that we want to use this host adapter -  it is a
 * pain to reverse this, so we try to avoid it 
 */

struct Scsi_Host * scsi_register(Scsi_Host_Template * tpnt, int j){
    struct Scsi_Host * retval, *shpnt;
    retval = (struct Scsi_Host *)scsi_init_malloc(sizeof(struct Scsi_Host) + j,
						  (tpnt->unchecked_isa_dma && j ? GFP_DMA : 0) | GFP_ATOMIC);
    atomic_set(&retval->host_active,0);
    retval->host_busy = 0;
    retval->host_failed = 0;
    retval->block = NULL;
    retval->wish_block = 0;
    if(j > 0xffff) panic("Too many extra bytes requested\n");
    retval->extra_bytes = j;
    retval->loaded_as_module = scsi_loadable_module_flag;
    retval->host_no = max_scsi_hosts++; /* never reuse host_no (DB) */
    next_scsi_host++;
    retval->host_queue = NULL;
    retval->host_wait = NULL;
    retval->resetting = 0;
    retval->last_reset = 0;
    retval->irq = 0;
    retval->dma_channel = 0xff;

    /* These three are default values which can be overridden */
    retval->max_channel = 0; 
    retval->max_id = 8;      
    retval->max_lun = 8;

    retval->unique_id = 0;
    retval->io_port = 0;
    retval->hostt = tpnt;
    retval->next = NULL;
    retval->in_recovery = 0;
    retval->ehandler = NULL;    /* Initial value until the thing starts up. */
    retval->eh_notify   = NULL;    /* Who we notify when we exit. */

    /*
     * Initialize the fields used for mid-level queueing.
     */
    retval->pending_commands = NULL;
    retval->host_busy = FALSE;

#ifdef DEBUG
    printk("Register %x %x: %d\n", (int)retval, (int)retval->hostt, j);
#endif

    /* The next six are the default values which can be overridden
     * if need be */
    retval->this_id = tpnt->this_id;
    retval->can_queue = tpnt->can_queue;
    retval->sg_tablesize = tpnt->sg_tablesize;
    retval->cmd_per_lun = tpnt->cmd_per_lun;
    retval->unchecked_isa_dma = tpnt->unchecked_isa_dma;
    retval->use_clustering = tpnt->use_clustering;   

    retval->select_queue_depths = NULL;

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

/*
 * Why is this a separate function?  Because the kernel_thread code
 * effectively does a fork, and there is a builtin exit() call when
 * the child returns.   The difficulty is that scsi_init() is
 * marked __initfunc(), which means the memory is unmapped after bootup
 * is complete, which means that the thread's exit() call gets wiped.
 *
 * The lesson is to *NEVER*, *NEVER* call kernel_thread() from an
 * __initfunc() function, if that function could ever return.
 */
static void launch_error_handler_thread(struct Scsi_Host * shpnt)
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

__initfunc(unsigned int scsi_init(void))
{
    static int called = 0;
    int i, pcount;
    unsigned long flags;
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
	if (tpnt->detect) {

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

           }

	if (tpnt->detect && tpnt->present)
	{
	    /* The only time this should come up is when people use
	     * some kind of patched driver of some kind or another. */
	    if(pcount == next_scsi_host) {
		if(tpnt->present > 1)
		    panic("Failure to register low-level scsi driver");
		/* The low-level driver failed to register a driver.  We
		 * can do this now. */
		scsi_register(tpnt,0);
	    }
	    tpnt->next = scsi_hosts;
	    scsi_hosts = tpnt;

            /* Add the driver to /proc/scsi */
#if CONFIG_PROC_FS 
            build_proc_dir_entries(tpnt);    
#endif
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

        /*
         * Now start the error recovery thread for the host.
         */
        if( shpnt->hostt->use_new_eh_code )
        {
            launch_error_handler_thread(shpnt);
        }
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

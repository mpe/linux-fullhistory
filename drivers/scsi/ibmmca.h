/* 
 * Low Level Driver for the IBM Microchannel SCSI Subsystem
 * (Headerfile, see README.ibmmca for description of the IBM MCA SCSI-driver
 * For use under the GNU public license within the Linux-kernel project.
 */

#ifndef _IBMMCA_H
#define _IBMMCA_H

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

/* Note to the Linux-toplevel-maintainers: 
 * This file contains the unified header for all available Linux-distributions.
 * For reasons of maintenance, it is recommended to keep this unmodified to
 * be downward compatible until I no longer get any requests from people 
 * using older kernel releases on their PS/2 machines. (23 Apr 2000, M.Lang) */

/* Common forward declarations for all Linux-versions: */

/* Interfaces to the midlevel Linux SCSI driver */
extern int ibmmca_proc_info (char *, char **, off_t, int, int, int);
extern int ibmmca_detect (Scsi_Host_Template *);
extern int ibmmca_release (struct Scsi_Host *);
extern int ibmmca_command (Scsi_Cmnd *);
extern int ibmmca_queuecommand (Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
extern int ibmmca_abort (Scsi_Cmnd *);
extern int ibmmca_reset (Scsi_Cmnd *, unsigned int);
extern int ibmmca_biosparam (Disk *, kdev_t, int *);

/*structure for /proc filesystem */
extern struct proc_dir_entry proc_scsi_ibmmca;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,75)
/* Stuff for Linux >= 2.1.75: */
/*
 * 2/8/98
 * Note to maintainer of IBMMCA.  Do not change this initializer back to
 * the old format.  Please ask eric@andante.jic.com if you have any questions
 * about this, but it will break things in the future.
 */
/*initialization for Scsi_host_template type (Linux >= 2.1.75 && < 2.3.27) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,27)
#define IBMMCA {                                                      \
          proc_dir:       &proc_scsi_ibmmca,    /*proc_dir*/          \
	  proc_info:	  ibmmca_proc_info,     /*proc info fn*/      \
          name:           "IBM SCSI-Subsystem", /*name*/              \
          detect:         ibmmca_detect,        /*detect fn*/         \
          release:        ibmmca_release,       /*release fn*/        \
          command:        ibmmca_command,       /*command fn*/        \
          queuecommand:   ibmmca_queuecommand,  /*queuecommand fn*/   \
          abort:          ibmmca_abort,         /*abort fn*/          \
          reset:          ibmmca_reset,         /*reset fn*/          \
          bios_param:     ibmmca_biosparam,     /*bios fn*/           \
          can_queue:      16,                   /*can_queue*/         \
          this_id:        7,                    /*set by detect*/     \
          sg_tablesize:   16,                   /*sg_tablesize*/      \
          cmd_per_lun:    1,                    /*cmd_per_lun*/       \
          unchecked_isa_dma: 0,                 /*32-Bit Busmaster */ \
          use_clustering: ENABLE_CLUSTERING     /*use_clustering*/    \
          }
#else
#define IBMMCA {                                                      \
          proc_name:      "ibmmca",             /*proc_name*/         \
	  proc_info:	  ibmmca_proc_info,     /*proc info fn*/      \
          name:           "IBM SCSI-Subsystem", /*name*/              \
          detect:         ibmmca_detect,        /*detect fn*/         \
          release:        ibmmca_release,       /*release fn*/        \
          command:        ibmmca_command,       /*command fn*/        \
          queuecommand:   ibmmca_queuecommand,  /*queuecommand fn*/   \
          abort:          ibmmca_abort,         /*abort fn*/          \
          reset:          ibmmca_reset,         /*reset fn*/          \
          bios_param:     ibmmca_biosparam,     /*bios fn*/           \
          can_queue:      16,                   /*can_queue*/         \
          this_id:        7,                    /*set by detect*/     \
          sg_tablesize:   16,                   /*sg_tablesize*/      \
          cmd_per_lun:    1,                    /*cmd_per_lun*/       \
          unchecked_isa_dma: 0,                 /*32-Bit Busmaster */ \
          use_clustering: ENABLE_CLUSTERING     /*use_clustering*/    \
          }
#endif
#else
/* Stuff for Linux < 2.1.75: */

/*initialization for Scsi_host_template type (Linux < 2.1.75) */
#define IBMMCA {                                      \
          NULL,                 /*next*/              \
          NULL,                 /*usage_count*/       \
          &proc_scsi_ibmmca,    /*proc_dir*/          \
          ibmmca_proc_info,     /*proc info fn*/      \
          "IBM SCSI-Subsystem", /*name*/              \
          ibmmca_detect,        /*detect fn*/         \
          ibmmca_release,       /*release fn*/        \
          NULL,                 /*info fn*/           \
          ibmmca_command,       /*command fn*/        \
          ibmmca_queuecommand,  /*queuecommand fn*/   \
          ibmmca_abort,         /*abort fn*/          \
          ibmmca_reset,         /*reset fn*/          \
          NULL,                 /*slave_attach fn*/   \
          ibmmca_biosparam,     /*bios fn*/           \
          16,                   /*can_queue*/         \
          7,                    /*set by detect*/     \
          16,                   /*sg_tablesize*/      \
          1,                    /*cmd_per_lun*/       \
          0,                    /*present*/           \
          0,                    /*unchecked_isa_dma*/ \
          ENABLE_CLUSTERING     /*use_clustering*/    \
        }
#endif /* kernelversion selection */

#endif /* _IBMMCA_H */

#ifndef _IBMMCA_H
#define _IBMMCA_H

/* 
 * Low Level Driver for the IBM Microchannel SCSI Subsystem
 */

/*services provided to the higher level of Linux SCSI driver */
int ibmmca_proc_info (char *, char **, off_t, int, int, int);
int ibmmca_detect (Scsi_Host_Template *);
int ibmmca_release (struct Scsi_Host *);
int ibmmca_command (Scsi_Cmnd *);
int ibmmca_queuecommand (Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int ibmmca_abort (Scsi_Cmnd *);
int ibmmca_reset (Scsi_Cmnd *, unsigned int);
int ibmmca_biosparam (Disk *, kdev_t, int *);

/*structure for /proc filesystem */
extern struct proc_dir_entry proc_scsi_ibmmca;

/*initialization for Scsi_host_template type */
/*
 * 2/8/98
 * Note to maintainer of IBMMCA.  Do not change this initializer back to
 * the old format.  Please ask eric@andante.jic.com if you have any questions
 * about this, but it will break things in the future.
 */
#define IBMMCA {						      \
          proc_dir:       &proc_scsi_ibmmca,    /*proc_dir*/          \
	  proc_info:	  ibmmca_proc_info,     /*proc info fn*/      \
          name:           "IBMMCA",             /*name*/              \
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
          use_clustering: ENABLE_CLUSTERING     /*use_clustering*/    \
          }

#endif /* _IBMMCA_H */



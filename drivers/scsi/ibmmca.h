
#ifndef _IBMMCA_H
#define _IBMMCA_H

/*
 * Low Level Driver for the IBM Microchannel SCSI Subsystem
 */

/*services provided to the higher level of Linux SCSI driver */
int ibmmca_detect (Scsi_Host_Template *);
int ibmmca_command (Scsi_Cmnd *);
int ibmmca_queuecommand (Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int ibmmca_abort (Scsi_Cmnd *);
int ibmmca_reset (Scsi_Cmnd *);
int ibmmca_biosparam (Disk *, kdev_t, int *);

/*structure for /proc filesystem */
extern struct proc_dir_entry proc_scsi_ibmmca;

/*initialization for Scsi_host_template type */
#define IBMMCA {                                      \
          NULL,                 /*next*/              \
          NULL,                 /*module*/	      \
          &proc_scsi_ibmmca,    /*proc_dir*/          \
          NULL,                 /*proc info fn*/      \
          "IBMMCA",             /*name*/              \
          ibmmca_detect,        /*detect fn*/         \
          NULL,                 /*release fn*/        \
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

#endif /* _IBMMCA_H */

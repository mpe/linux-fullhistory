/*
 *   u14-34f.h - used by the low-level driver for UltraStor 14F/34F
 */
#ifndef _U14_34F_H
#define _U14_34F_H

#include <scsi/scsicam.h>
#include <linux/version.h>

int u14_34f_detect(Scsi_Host_Template *);
int u14_34f_release(struct Scsi_Host *);
int u14_34f_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int u14_34f_abort(Scsi_Cmnd *);
int u14_34f_old_abort(Scsi_Cmnd *);
int u14_34f_reset(Scsi_Cmnd *);
int u14_34f_old_reset(Scsi_Cmnd *, unsigned int);
int u14_34f_biosparam(Disk *, kdev_t, int *);

#define U14_34F_VERSION "4.33.00"

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

#if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,101)

#define ULTRASTOR_14_34F {                                                   \
                name:         "UltraStor 14F/34F rev. " U14_34F_VERSION " ", \
                detect:                  u14_34f_detect,                     \
                release:                 u14_34f_release,                    \
                queuecommand:            u14_34f_queuecommand,               \
                abort:                   u14_34f_old_abort,                  \
                reset:                   u14_34f_old_reset,                  \
                eh_abort_handler:        u14_34f_abort,                      \
                eh_device_reset_handler: NULL,                               \
                eh_bus_reset_handler:    NULL,                               \
                eh_host_reset_handler:   u14_34f_reset,                      \
                bios_param:              u14_34f_biosparam,                  \
                this_id:                 7,                                  \
                unchecked_isa_dma:       1,                                  \
                use_clustering:          ENABLE_CLUSTERING,                  \
                use_new_eh_code:         1    /* Enable new error code */    \
                }

#else /* Use old scsi code */

#define ULTRASTOR_14_34F {                                                   \
                name:         "UltraStor 14F/34F rev. " U14_34F_VERSION " ", \
                detect:                  u14_34f_detect,                     \
                release:                 u14_34f_release,                    \
                queuecommand:            u14_34f_queuecommand,               \
                abort:                   u14_34f_old_abort,                  \
                reset:                   u14_34f_old_reset,                  \
                bios_param:              u14_34f_biosparam,                  \
                this_id:                 7,                                  \
                unchecked_isa_dma:       1,                                  \
                use_clustering:          ENABLE_CLUSTERING                   \
                }

#endif

#endif

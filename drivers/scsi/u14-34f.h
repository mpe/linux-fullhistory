/*
 *   u14-34f.h - used by the low-level driver for UltraStor 14F/34F
 */
#ifndef _U14_34F_H
#define _U14_34F_H

int u14_34f_detect(Scsi_Host_Template *);
int u14_34f_release(struct Scsi_Host *);
int u14_34f_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int u14_34f_abort(Scsi_Cmnd *);
int u14_34f_reset(Scsi_Cmnd *, unsigned int);
int u14_34f_biosparam(Disk *, kdev_t, int *);

#define U14_34F_VERSION "3.11.00"

#define ULTRASTOR_14_34F {							   \
		name:              "UltraStor 14F/34F rev. " U14_34F_VERSION " ",  \
		detect:            u14_34f_detect,                                 \
		release:           u14_34f_release,                                \
		queuecommand:      u14_34f_queuecommand,                           \
		abort:             u14_34f_abort,                                  \
		reset:             u14_34f_reset,                                  \
		bios_param:        u14_34f_biosparam,                              \
		this_id:           7,   /* this_id, reset by detect */             \
		unchecked_isa_dma: 1,   /* unchecked isa dma, reset by detect */   \
		use_clustering:    ENABLE_CLUSTERING                               \
		}
#endif

#ifndef MVME16x_SCSI_H
#define MVME16x_SCSI_H

#include <linux/types.h>

int mvme16x_scsi_detect(Scsi_Host_Template *);
const char *NCR53c7x0_info(void);
int NCR53c7xx_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int NCR53c7xx_abort(Scsi_Cmnd *);
int NCR53c7x0_release (Scsi_Host_Template *);
int NCR53c7xx_reset(Scsi_Cmnd *, unsigned int);
void NCR53c7x0_intr(int irq, void *dev_id, struct pt_regs * regs);

#ifndef NULL
#define NULL 0
#endif

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 3
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 24
#endif

#if defined(HOSTS_C) || defined(MODULE)
#include <scsi/scsicam.h>

extern struct proc_dir_entry proc_scsi_mvme16x;

#define MVME16x_SCSI {/* next */                NULL,            \
		      /* usage_count */         NULL,	         \
		      /* proc_dir_entry */      NULL, \
		      /* proc_info */           NULL,            \
		      /* name */                "MVME16x SCSI", \
		      /* detect */              mvme16x_scsi_detect,    \
		      /* release */             NULL,   \
		      /* info */                NULL,	         \
		      /* command */             NULL,            \
		      /* queuecommand */        NCR53c7xx_queue_command, \
		      /* abort */               NCR53c7xx_abort,   \
		      /* reset */               NCR53c7xx_reset,   \
		      /* slave_attach */        NULL,            \
		      /* bios_param */          NULL /*scsicam_bios_param*/, \
		      /* can_queue */           24,       \
		      /* this_id */             7,               \
		      /* sg_tablesize */        127,          \
		      /* cmd_per_lun */	        3,     \
		      /* present */             0,               \
		      /* unchecked_isa_dma */   0,               \
		      /* use_clustering */      DISABLE_CLUSTERING }
#endif
#endif /* MVME16x_SCSI_H */

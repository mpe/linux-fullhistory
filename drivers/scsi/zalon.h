#ifndef ZALON7XX_H
#define ZALON7XX_H

#include <linux/types.h>

#include "sym53c8xx_defs.h"

extern int zalon7xx_detect(Scsi_Host_Template *);

#include <scsi/scsicam.h>

extern struct proc_dir_entry proc_scsi_zalon7xx;

/* borrowed from drivers/scsi/ncr53c8xx.h */
int zalon7xx_detect(Scsi_Host_Template *tpnt);
const char *ncr53c8xx_info(struct Scsi_Host *host);
int ncr53c8xx_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));

#ifdef MODULE
int zalon7xx_release(struct Scsi_Host *);
#else
#define zalon7xx_release NULL
#endif

#define GSC_SCSI_ZALON_OFFSET 0x800

#define IO_MODULE_EIM		(1*4)
#define IO_MODULE_DC_ADATA	(2*4)
#define IO_MODULE_II_CDATA	(3*4)
#define IO_MODULE_IO_COMMAND	(12*4)
#define IO_MODULE_IO_STATUS	(13*4)

#define IOSTATUS_RY             0x40
#define IOSTATUS_FE             0x80
#define IOIIDATA_SMINT5L        0x40000000
#define IOIIDATA_MINT5EN        0x20000000
#define IOIIDATA_PACKEN         0x10000000
#define IOIIDATA_PREFETCHEN     0x08000000
#define IOIIDATA_IOII           0x00000020

#define CMD_RESET		5

#endif /* ZALON7XX_H */

/*
 *        eata.h - used by the low-level driver for EATA/DMA SCSI host adapters.
 */
#ifndef _EATA_H
#define _EATA_H

#include <scsi/scsicam.h>

int eata2x_detect(Scsi_Host_Template *);
int eata2x_release(struct Scsi_Host *);
int eata2x_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int eata2x_abort(Scsi_Cmnd *);
int eata2x_reset(Scsi_Cmnd *, unsigned int);

#define EATA_VERSION "4.02.00"

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

#if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,88)

#define EATA {                                                               \
                name:              "EATA/DMA 2.0x rev. " EATA_VERSION " ",   \
                detect:            eata2x_detect,                            \
                release:           eata2x_release,                           \
                queuecommand:      eata2x_queuecommand,                      \
                abort:             eata2x_abort,                             \
                reset:             eata2x_reset,                             \
                bios_param:        scsicam_bios_param,                       \
                this_id:           7,                                        \
                unchecked_isa_dma: 1,                                        \
                use_clustering:    ENABLE_CLUSTERING,                        \
                use_new_eh_code: 1    /* Enable new error code */            \
             }

#else /* Use old scsi code */

#define EATA {                                                               \
                name:              "EATA/DMA 2.0x rev. " EATA_VERSION " ",   \
                detect:            eata2x_detect,                            \
                release:           eata2x_release,                           \
                queuecommand:      eata2x_queuecommand,                      \
                abort:             eata2x_abort,                             \
                reset:             eata2x_reset,                             \
                bios_param:        scsicam_bios_param,                       \
                this_id:           7,                                        \
                unchecked_isa_dma: 1,                                        \
                use_clustering:    ENABLE_CLUSTERING                         \
             }

#endif

#endif

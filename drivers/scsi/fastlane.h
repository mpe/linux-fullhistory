/* fastlane.h: Defines and structures for the Fastlane SCSI driver.
 *
 * Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 */

#include "NCR53C9x.h"

#ifndef FASTLANE_H
#define FASTLANE_H

/* The controller registers can be found in the Z2 config area at these
 * offsets:
 */
#define FASTLANE_ESP_ADDR 0x1000001
#define FASTLANE_DMA_ADDR 0x1000041


/* The Fastlane DMA interface */
struct fastlane_dma_registers {
	volatile unsigned char cond_reg;	/* DMA status  (ro) [0x0000] */
#define ctrl_reg  cond_reg			/* DMA control (wo) [0x0000] */
	unsigned char dmapad1[0x3f];
	volatile unsigned char clear_strobe;    /* DMA clear   (wo) [0x0040] */
};


/* DMA status bits */
#define FASTLANE_DMA_MINT  0x80
#define FASTLANE_DMA_IACT  0x40
#define FASTLANE_DMA_CREQ  0x20

/* DMA control bits */
#define FASTLANE_DMA_FCODE 0xa0
#define FASTLANE_DMA_MASK  0xf3
#define FASTLANE_DMA_LED   0x10	/* HD led control 1 = on */
#define FASTLANE_DMA_WRITE 0x08 /* 1 = write */
#define FASTLANE_DMA_ENABLE 0x04 /* Enable DMA */
#define FASTLANE_DMA_EDI   0x02	/* Enable DMA IRQ ? */
#define FASTLANE_DMA_ESI   0x01	/* Enable SCSI IRQ */

extern int fastlane_esp_detect(struct SHT *);
extern const char *esp_info(struct Scsi_Host *);
extern int esp_queue(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
extern int esp_command(Scsi_Cmnd *);
extern int esp_abort(Scsi_Cmnd *);
extern int esp_reset(Scsi_Cmnd *, unsigned int);
extern int esp_proc_info(char *buffer, char **start, off_t offset, int length,
			 int hostno, int inout);

#define SCSI_FASTLANE {                                                               \
/* struct SHT *next */                                         NULL,                   \
/* long *usage_count */                                        NULL,                   \
/* struct proc_dir_entry *proc_dir */                          &proc_scsi_esp,         \
/* int (*proc_info)(char *, char **, off_t, int, int, int) */  &esp_proc_info,                   \
/* const char *name */                                         "Fastlane SCSI", \
/* int detect(struct SHT *) */                                 fastlane_esp_detect,  \
/* int release(struct Scsi_Host *) */                          NULL,                   \
/* const char *info(struct Scsi_Host *) */                     esp_info,               \
/* int command(Scsi_Cmnd *) */                                 esp_command,            \
/* int queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *)) */ esp_queue,              \
/* int abort(Scsi_Cmnd *) */                                   esp_abort,              \
/* int reset(Scsi_Cmnd *) */                                   esp_reset,              \
/* int slave_attach(int, int) */                               NULL,                   \
/* int bios_param(Disk *, kdev_t, int[]) */                    NULL,                   \
/* int can_queue */                                            7,                      \
/* int this_id */                                              7,                      \
/* short unsigned int sg_tablesize */                          SG_ALL,                 \
/* short cmd_per_lun */                                        1,                      \
/* unsigned char present */                                    0,                      \
/* unsigned unchecked_isa_dma:1 */                             0,                      \
/* unsigned use_clustering:1 */                                DISABLE_CLUSTERING, }

#endif /* FASTLANE_H */

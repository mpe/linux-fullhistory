/* blz2060.h: Defines and structures for the Blizzard 2060 SCSI driver.
 *
 * Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 *
 * This file is based on cyber_esp.h (hence the occasional reference to
 * CyberStorm).
 */

#include "NCR53C9x.h"

#ifndef BLZ2060_H
#define BLZ2060_H

/* The controller registers can be found in the Z2 config area at these
 * offsets:
 */
#define BLZ2060_ESP_ADDR 0x1ff00
#define BLZ2060_DMA_ADDR 0x1ffe0


/* The Blizzard 2060 DMA interface
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Only two things can be programmed in the Blizzard DMA:
 *  1) The data direction is controlled by the status of bit 31 (1 = write)
 *  2) The source/dest address (word aligned, shifted one right) in bits 30-0
 *
 * Figure out interrupt status by reading the ESP status byte.
 */
struct blz2060_dma_registers {
	volatile unsigned char dma_led_ctrl;	/* DMA led control   [0x000] */
	unsigned char dmapad1[0x0f];
	volatile unsigned char dma_addr0; 	/* DMA address (MSB) [0x010] */
	unsigned char dmapad2[0x03];
	volatile unsigned char dma_addr1; 	/* DMA address       [0x014] */
	unsigned char dmapad3[0x03];
	volatile unsigned char dma_addr2; 	/* DMA address       [0x018] */
	unsigned char dmapad4[0x03];
	volatile unsigned char dma_addr3; 	/* DMA address (LSB) [0x01c] */
};

#define BLZ2060_DMA_WRITE 0x80000000

/* DMA control bits */
#define BLZ2060_DMA_LED    0x02		/* HD led control 1 = off */

extern int blz2060_esp_detect(struct SHT *);
extern const char *esp_info(struct Scsi_Host *);
extern int esp_queue(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
extern int esp_command(Scsi_Cmnd *);
extern int esp_abort(Scsi_Cmnd *);
extern int esp_reset(Scsi_Cmnd *, unsigned int);
extern int esp_proc_info(char *buffer, char **start, off_t offset, int length,
			 int hostno, int inout);

#define SCSI_BLZ2060 {                                                               \
/* struct SHT *next */                                         NULL,                   \
/* long *usage_count */                                        NULL,                   \
/* struct proc_dir_entry *proc_dir */                          &proc_scsi_esp,         \
/* int (*proc_info)(char *, char **, off_t, int, int, int) */  &esp_proc_info,                   \
/* const char *name */                                         "Blizzard2060 SCSI", \
/* int detect(struct SHT *) */                                 blz2060_esp_detect,             \
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

#endif /* BLZ2060_H */

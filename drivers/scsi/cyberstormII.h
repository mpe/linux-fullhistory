/* cyberstormII.h: Defines and structures for the CyberStorm SCSI Mk II driver.
 *
 * Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 */

#include "NCR53C9x.h"

#ifndef CYBERII_ESP_H
#define CYBERII_ESP_H

/* The controller registers can be found in the Z2 config area at these
 * offsets:
 */
#define CYBERII_ESP_ADDR 0x1ff03
#define CYBERII_DMA_ADDR 0x1ff43


/* The CyberStorm II DMA interface */
struct cyberII_dma_registers {
	volatile unsigned char cond_reg;        /* DMA cond    (ro)  [0x000] */
#define ctrl_reg  cond_reg			/* DMA control (wo)  [0x000] */
	unsigned char dmapad4[0x3f];
	volatile unsigned char dma_addr0;	/* DMA address (MSB) [0x040] */
	unsigned char dmapad1[3];
	volatile unsigned char dma_addr1;	/* DMA address       [0x044] */
	unsigned char dmapad2[3];
	volatile unsigned char dma_addr2;	/* DMA address       [0x048] */
	unsigned char dmapad3[3];
	volatile unsigned char dma_addr3;	/* DMA address (LSB) [0x04c] */
};

/* DMA control bits */
#define CYBERII_DMA_LED    0x02	/* HD led control 1 = on */


extern int cyberII_esp_detect(struct SHT *);
extern const char *esp_info(struct Scsi_Host *);
extern int esp_queue(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
extern int esp_command(Scsi_Cmnd *);
extern int esp_abort(Scsi_Cmnd *);
extern int esp_reset(Scsi_Cmnd *, unsigned int);
extern int esp_proc_info(char *buffer, char **start, off_t offset, int length,
			 int hostno, int inout);

#define SCSI_CYBERSTORMII {                                                               \
/* struct SHT *next */                                         NULL,                   \
/* long *usage_count */                                        NULL,                   \
/* struct proc_dir_entry *proc_dir */                          &proc_scsi_esp,         \
/* int (*proc_info)(char *, char **, off_t, int, int, int) */  &esp_proc_info,                   \
/* const char *name */                                         "CyberStorm Mk II SCSI", \
/* int detect(struct SHT *) */                                 cyberII_esp_detect,  \
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

#endif /* CYBERII_ESP_H */


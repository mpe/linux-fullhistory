#ifndef _LINUX_AMIHDREG_H
#define _LINUX_AMIHDREG_H

/*
 * This file contains some defines for the Amiga IDE hd controller.
 * Various sources. Check out some definitions (see comments with
 * a ques).
 */

#define IDE_DISABLE_IRQ  0x02
#define IDE_ENABLE_IRQ   0x00
 
/* Bases of the hard drive controller */
#define HD_BASE_A4000   0xdd2020
#define HD_BASE_A1200   0xda0000

/* Offsets from one of the above bases */
#define AMI_HD_ERROR	(0x06)		/* see err-bits */
#define AMI_HD_NSECTOR	(0x0a)		/* nr of sectors to read/write */
#define AMI_HD_SECTOR	(0x0e)		/* starting sector */
#define AMI_HD_LCYL	(0x12)		/* starting cylinder */
#define AMI_HD_HCYL	(0x16)		/* high byte of starting cyl */
#define AMI_HD_SELECT	(0x1a)		/* 101dhhhh , d=drive, hhhh=head */
#define AMI_HD_STATUS	(0x1e)		/* see status-bits */
#define AMI_HD_CMD	(0x101a)

/* These are at different offsets from the base */
#define HD_A4000_IRQ	(0xdd3020)	/* MSB = 1, Harddisk is source of interrupt */
#define HD_A1200_IRQ	(0xda9000)	/* MSB = 1, Harddisk is source of interrupt */

#endif

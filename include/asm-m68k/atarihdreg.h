#ifndef _LINUX_ATAHDREG_H
#define _LINUX_ATAHDREG_H

/*
 * This file contains some defines for the Falcon IDE hd controller.
 * Various sources. Check out some definitions (see comments with
 * a ques).
 */

#define ATA_HD_BASE	0xfff00000

#define ATA_HD_DATA	0x00	/* _CTL when writing */
#define ATA_HD_ERROR	0x05	/* see err-bits */
#define ATA_HD_NSECTOR	0x09	/* nr of sectors to read/write */
#define ATA_HD_SECTOR	0x0d	/* starting sector */
#define ATA_HD_LCYL	0x11	/* starting cylinder */
#define ATA_HD_HCYL	0x15	/* high byte of starting cyl */
#define ATA_HD_CURRENT	0x19	/* 101dhhhh , d=drive, hhhh=head */
#define ATA_HD_STATUS	0x1d	/* see status-bits */

#define ATA_HD_CMD	0x39
#define ATA_HD_ALTSTATUS 0x39	/* same as HD_STATUS but doesn't clear irq */

#endif	/* _LINUX_ATAHDREG_H */

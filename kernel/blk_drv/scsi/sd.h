/*
 *	sd.h Copyright (C) 1992 Drew Eckhardt 
 *	SCSI disk driver header file by
 *		Drew Eckhardt 
 *
 *	<drew@colorado.edu>
 */
#ifndef _SD_H
	#define _SD_H
/*
	$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/sd.h,v 1.1 1992/04/24 18:01:50 root Exp root $
*/

#ifndef _SCSI_H
#include "scsi.h"
#endif

#define MAX_SD 2

typedef struct partition {
	unsigned char boot_ind;		/* 0x80 - active (unused) */
	unsigned char head;		/* ? */
	unsigned char sector;		/* ? */
	unsigned char cyl;		/* ? */
	unsigned char sys_ind;		/* ? */
	unsigned char end_head;		/* ? */
	unsigned char end_sector;	/* ? */
	unsigned char end_cyl;		/* ? */
	unsigned int start_sect;	/* starting sector counting from 0 */
	unsigned int nr_sects;		/* nr of sectors in partition */
} Partition;

extern int NR_SD;

extern Partition scsi_disks[MAX_SD << 4] ;


typedef struct {
		unsigned capacity;		/* size in blocks */
		unsigned sector_size;		/* size in bytes */
		Scsi_Device  *device;		
		unsigned char sector_bit_size;	/* sector_size = 2 to the  bit size power */
		unsigned char sector_bit_shift;	/* power of 2 sectors per FS block */
		unsigned ten:1;			/* support ten byte read / write */
		unsigned remap:1;		/* support remapping */
		unsigned use:1;			/* after the initial inquiry, is 
						   the device still supported ? */
		} Scsi_Disk;
	
extern Scsi_Disk rscsi_disks[MAX_SD];

void sd_init(void);

#define HOST (rscsi_disks[DEVICE_NR(CURRENT->dev)].device->host_no)
#define ID (rscsi_disks[DEVICE_NR(CURRENT->dev)].device->id)
#define LUN (rscsi_disks[DEVICE_NR(CURRENT->dev)].device->lun)
#endif

/* -*-linux-c-*-
 *
 * vendor-specific code for SCSI CD-ROM's goes here.
 *
 * This is needed becauce most of the new features (multisession and
 * the like) are to new to be included into the SCSI-II standard (to
 * be exact: there is'nt anything in my draft copy).
 *
 *   Gerd Knorr <kraxel@cs.tu-berlin.de> 
 */

#include <linux/errno.h>
#include <linux/string.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>

#include <linux/cdrom.h>
#include <linux/ucdrom.h>
#include "sr.h"

/* here are some constants to sort the vendors into groups */

#define VENDOR_CAN_NOT_HANDLE  1   /* don't know how to handle */
#define VENDOR_NEC             2
#define VENDOR_TOSHIBA         3
#define VENDOR_SONY_LIKE       4   /* much drives are Sony compatible */
#define VENDOR_HP              5

#define DEBUG

void
sr_vendor_init(int minor)
{
	char *vendor = scsi_CDs[minor].device->vendor;
	char *model  = scsi_CDs[minor].device->model;
		
	if (!strncmp (vendor, "NEC", 3)) {
		scsi_CDs[minor].vendor = VENDOR_NEC;
		if (!strncmp (model,"CD-ROM DRIVE:25", 15) ||
		    !strncmp (model,"CD-ROM DRIVE:36", 15)  ||
		    !strncmp (model,"CD-ROM DRIVE:83", 15)  ||
		    !strncmp (model,"CD-ROM DRIVE:84 ",16))
			/* these can't handle multisession, may hang */
			scsi_CDs[minor].cdi.mask |= CDC_MULTI_SESSION;

	} else if (!strncmp (vendor, "TOSHIBA", 7)) {
		scsi_CDs[minor].vendor = VENDOR_TOSHIBA;
		
	} else if (!strncmp (vendor, "HP", 2)) {
		scsi_CDs[minor].vendor = VENDOR_HP;
		
	} else {
		/* most drives can handled like sony ones, so we take
		 * it as default */
		scsi_CDs[minor].vendor = VENDOR_SONY_LIKE;
	}
}



/*
 * support for XA/multisession-CD's
 * 
 *   - NEC:     Detection and support of multisession CD's.
 *     
 *   - TOSHIBA: Detection and support of multisession CD's.
 *              Some XA-Sector tweaking, required for older drives.
 *
 *   - SONY:	Detection and support of multisession CD's.
 *              added by Thomas Quinot <operator@melchior.cuivre.fdn.fr>
 *
 *   - PIONEER, HITACHI, PLEXTOR, MATSHITA: known to work with SONY code.
 *
 *   - HP:	Much like SONY, but a little different... (Thomas)
 *              HP-Writers only ???
 */

#define BCD_TO_BIN(x)    ((((int)x & 0xf0) >> 4)*10 + ((int)x & 0x0f))

int sr_cd_check(struct cdrom_device_info *cdi)
{
	unsigned long   sector,min,sec,frame;
	unsigned char   *buffer;    /* the buffer for the ioctl */
	unsigned char   cmd[12];    /* the scsi-command */
	int             rc,is_xa,no_multi,minor;

	minor = MINOR(cdi->dev);
	
	buffer = (unsigned char *) scsi_malloc(512);
	if(!buffer) return -ENOMEM;
	
	sector   = 0;         /* the multisession sector offset goes here  */
	is_xa    = 0;         /* flag: the CD uses XA-Sectors              */
	no_multi = 0;         /* flag: the drive can't handle multisession */
	rc       = 0;
    
	switch(scsi_CDs[minor].vendor) {
	
	case VENDOR_NEC:
		memset(cmd,0,12);
		cmd[0] = 0xde;
		cmd[1] = (scsi_CDs[minor].device->lun << 5) | 0x03;
		cmd[2] = 0xb0;
		rc = sr_do_ioctl(minor, cmd, buffer, 0x16);
		if (rc != 0)
			break;
		if (buffer[14] != 0 && buffer[14] != 0xb0) {
			printk(KERN_INFO "sr (nec): Hmm, seems the cdrom doesn't support multisession CD's\n");
			no_multi = 1;
			break;
		}
		min    = BCD_TO_BIN(buffer[15]);
		sec    = BCD_TO_BIN(buffer[16]);
		frame  = BCD_TO_BIN(buffer[17]);
		sector = min*CD_SECS*CD_FRAMES + sec*CD_FRAMES + frame;
		is_xa  = (buffer[14] == 0xb0);
		break;
		
	case VENDOR_TOSHIBA:
		/* we request some disc information (is it a XA-CD ?,
		 * where starts the last session ?) */
		memset(cmd,0,12);
		cmd[0] = 0xc7;
		cmd[1] = (scsi_CDs[minor].device->lun << 5) | 3;
		rc = sr_do_ioctl(minor, cmd, buffer, 4);
		if (rc == 0x28000002 &&
		    !scsi_ioctl(scsi_CDs[minor].device,
				SCSI_IOCTL_TEST_UNIT_READY, NULL)) {
			printk(KERN_INFO "sr (toshiba): Hmm, seems the drive doesn't support multisession CD's\n");
		    no_multi = 1;
		    break;
		}
		if (rc != 0)
			break;
		min    = BCD_TO_BIN(buffer[1]);
		sec    = BCD_TO_BIN(buffer[2]);
		frame  = BCD_TO_BIN(buffer[3]);
		sector = min*CD_SECS*CD_FRAMES + sec*CD_FRAMES + frame;
		if (sector)
			sector -= CD_BLOCK_OFFSET;
		is_xa  = (buffer[0] == 0x20);
#if 0
		/* this is required for some CD's:
		 *   - Enhanced-CD (Hardware tells wrong XA-flag)
		 *   - these broken non-XA multisession CD's
		 */
		if (is_xa == 0 && sector != 0) {
			printk(KERN_WARNING "Warning: multisession offset "
			       "found, setting XA-flag\n");
			is_xa = 1;
		}
#endif
		/* now the XA-Sector tweaking: set_density... */
		memset(cmd,0,12);
		cmd[0] = MODE_SELECT;
		cmd[1] = (scsi_CDs[minor].device->lun << 5)
			| (1 << 4);
		cmd[4] = 12;
		memset(buffer,0,12);
		buffer[ 3] = 0x08;
		buffer[ 4] = 0x83;
		buffer[10] = 0x08;
		rc = sr_do_ioctl(minor, cmd, buffer, 12);
		if (rc != 0) {
			break;
		}
#if 0
		/* shoult'nt be required any more */
		scsi_CDs[minor].needs_sector_size = 1;
#endif
		break;

	case VENDOR_HP:
		cmd[0] = READ_TOC;
		cmd[1] = (scsi_CDs[minor].device->lun << 5);
		cmd[8] = 0x04;
		cmd[9] = 0x40;
		rc = sr_do_ioctl(minor, cmd, buffer, 12);	
		if (rc != 0) {
			break;
		}
		if ((rc = buffer[2]) == 0) {
			printk (KERN_WARNING
				"sr (hp): No finished session\n");
			break;
		}

		cmd[0] = READ_TOC; /* Read TOC */
		cmd[1] = (scsi_CDs[minor].device->lun << 5);
		cmd[6] = rc & 0x7f;  /* number of last session */
		cmd[8] = 0x0c;
		cmd[9] = 0x40;
		rc = sr_do_ioctl(minor, cmd, buffer, 12);	
		if (rc != 0) {
			break;
		}

#undef STRICT_HP
#ifdef STRICT_HP
		sector = buffer[11] + (buffer[10] << 8) + (buffer[9] << 16);
		/* HP documentation states that Logical Start Address is
		   returned as three (!) bytes, and that buffer[8] is
		   reserved. This is strange, because a LBA usually is
		   4 bytes long. */
#else
		sector = buffer[11] + (buffer[10] << 8) +
			(buffer[9] << 16) + (buffer[8] << 24);
#endif
		is_xa = !!sector;
		break;

	case VENDOR_SONY_LIKE:
		/* Thomas QUINOT <thomas@melchior.cuivre.fdn.fr> */
#ifdef DEBUG
		printk(KERN_DEBUG
		       "sr: use \"Sony group\" multisession code\n");
#endif
		memset(cmd,0,12);
		cmd[0] = READ_TOC;
		cmd[1] = (scsi_CDs[minor].device->lun << 5);
		cmd[8] = 12;
		cmd[9] = 0x40;
		rc = sr_do_ioctl(minor, cmd, buffer, 12);	
		if (rc != 0) {
			break;
		}
		if ((buffer[0] << 8) + buffer[1] != 0x0a) {
			printk(KERN_INFO "sr (sony): Hmm, seems the drive doesn't support multisession CD's\n");
			no_multi = 1;
			break;
		}
		sector = buffer[11] + (buffer[10] << 8) +
			(buffer[9] << 16) + (buffer[8] << 24);
		if (buffer[6] <= 1) {
			/* ignore sector offsets from first track */
			sector = 0;
		}
		is_xa = !!sector;
		break;
		
	case VENDOR_CAN_NOT_HANDLE:
		sector = 0;
		no_multi = 1;
		break;

	default:
		/* should not happen */
		printk(KERN_WARNING
		       "sr: unknown vendor code (%i), not initialized ?\n",
		       scsi_CDs[minor].vendor);
		sector = 0;
		no_multi = 1;
		break;
	}
    
#ifdef DEBUG
	if (sector)
		printk(KERN_DEBUG
		       "sr: multisession CD detected, offset: %lu\n",sector);
#endif

	scsi_CDs[minor].ms_offset = sector;
	scsi_CDs[minor].xa_flag = is_xa;
	if (no_multi)
		cdi->mask |= CDC_MULTI_SESSION;
	
	scsi_free(buffer, 512);

	return rc;
}

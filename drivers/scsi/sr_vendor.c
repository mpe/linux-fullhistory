/* -*-linux-c-*-
 *
 * vendor-specific code for SCSI CD-ROM's goes here.
 *
 * This is needed becauce most of the new features (multisession and
 * the like) are to new to be included into the SCSI-II standard (to
 * be exact: there is'nt anything in my draft copy).
 *
 *   Gerd Knorr <kraxel@cs.tu-berlin.de> 
 *
 * --------------------------------------------------------------------------
 *
 * support for XA/multisession-CD's
 * 
 *   - NEC:     Detection and support of multisession CD's.
 *     
 *   - TOSHIBA: Detection and support of multisession CD's.
 *              Some XA-Sector tweaking, required for older drives.
 *
 *   - SONY:	Detection and support of multisession CD's.
 *              added by Thomas Quinot <thomas@cuivre.freenix.fr>
 *
 *   - PIONEER, HITACHI, PLEXTOR, MATSHITA, TEAC, PHILIPS:
 *		Known to work with SONY code.
 *
 *   - HP:	Much like SONY, but a little different... (Thomas)
 *              HP-Writers only ??? Maybe other CD-Writers work with this too ?
 *		HP 6020 writers now supported.
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
#define VENDOR_HP_4020         5   /* HP 4xxx writers, others too ?? */
#define VENDOR_HP_6020         6   /* HP 6020 writers */

#define VENDOR_ID (scsi_CDs[minor].vendor)

#if 0
#define DEBUG
#endif

void
sr_vendor_init(int minor)
{
	char *vendor = scsi_CDs[minor].device->vendor;
	char *model  = scsi_CDs[minor].device->model;
		
	if ((!strncmp(vendor,"HP",2) || !strncmp(vendor,"PHILIPS",7)) &&
	    scsi_CDs[minor].device->type == TYPE_WORM) {
		if (!strncmp(model,"CD-Writer 6020",14))
                    VENDOR_ID = VENDOR_HP_6020;
                else
                    VENDOR_ID = VENDOR_HP_4020;

	} else if (!strncmp (vendor, "NEC", 3)) {
		VENDOR_ID = VENDOR_NEC;
		if (!strncmp (model,"CD-ROM DRIVE:25", 15)  ||
		    !strncmp (model,"CD-ROM DRIVE:36", 15)  ||
		    !strncmp (model,"CD-ROM DRIVE:83", 15)  ||
		    !strncmp (model,"CD-ROM DRIVE:84 ",16))
			/* these can't handle multisession, may hang */
			scsi_CDs[minor].cdi.mask |= CDC_MULTI_SESSION;

	} else if (!strncmp (vendor, "TOSHIBA", 7)) {
		VENDOR_ID = VENDOR_TOSHIBA;
		
	} else {
		/* most drives can handled like Sony ones, so we take
		 * it as default */
		VENDOR_ID = VENDOR_SONY_LIKE;
#ifdef DEBUG
		printk(KERN_DEBUG
		       "sr: using \"Sony group\" multisession code\n");
#endif
	}
}


/* small handy function for switching block length using MODE SELECT,
 * used by sr_read_sector() */

static int
set_density_and_blocklength(int minor, unsigned char *buffer,
			    int density, int blocklength)
{
	unsigned char		cmd[12];    /* the scsi-command */
	struct ccs_modesel_head	*modesel;
	int			rc;

	memset(cmd,0,12);
	cmd[0] = MODE_SELECT;
	cmd[1] = (scsi_CDs[minor].device->lun << 5) | (1 << 4);
	cmd[4] = 12;
	modesel = (struct ccs_modesel_head*)buffer;
	memset(modesel,0,sizeof(*modesel));
	modesel->block_desc_length = 0x08;
	modesel->density           = density;
	modesel->block_length_med  = (blocklength >> 8 ) & 0xff;
	modesel->block_length_lo   =  blocklength        & 0xff;
	rc = sr_do_ioctl(minor, cmd, buffer, sizeof(*modesel));
#ifdef DEBUG
	if (rc)
		printk("sr: switching blocklength to %d bytes failed\n",
		       blocklength);
#endif
	return rc;
}


/* read a sector with other than 2048 bytes length 
 * dest is assumed to be allocated with scsi_malloc
 *
 * XXX maybe we have to do some locking here.
 */

int
sr_read_sector(int minor, int lba, int blksize, unsigned char *dest)
{
	unsigned char   *buffer;    /* the buffer for the ioctl */
	unsigned char   cmd[12];    /* the scsi-command */
	int             rc, density;

	density = (VENDOR_ID == VENDOR_TOSHIBA) ? 0x83 : 0;

	buffer = (unsigned char *) scsi_malloc(512);
	if (!buffer) return -ENOMEM;

	rc = set_density_and_blocklength(minor, buffer, density, blksize);
	if (!rc) {
		memset(cmd,0,12);
		cmd[0] = READ_10;
		cmd[1] = (scsi_CDs[minor].device->lun << 5);
		cmd[2] = (unsigned char)(lba >> 24) & 0xff;
		cmd[3] = (unsigned char)(lba >> 16) & 0xff;
		cmd[4] = (unsigned char)(lba >>  8) & 0xff;
		cmd[5] = (unsigned char) lba        & 0xff;
		cmd[8] = 1;
		rc = sr_do_ioctl(minor, cmd, dest, blksize);
		set_density_and_blocklength(minor, buffer, density, 2048);
	}
	
	scsi_free(buffer, 512);
	return rc;
}


/* This function gets called after a media change. Checks if the CD is
   multisession, asks for offset etc. */

#define BCD_TO_BIN(x)    ((((int)x & 0xf0) >> 4)*10 + ((int)x & 0x0f))

int sr_cd_check(struct cdrom_device_info *cdi)
{
	unsigned long   sector,min,sec,frame;
	unsigned char   *buffer;     /* the buffer for the ioctl */
	unsigned char   *raw_sector; 
	unsigned char   cmd[12];     /* the scsi-command */
	int             rc,is_xa,no_multi,minor;

	minor = MINOR(cdi->dev);
	if (scsi_CDs[minor].cdi.mask & CDC_MULTI_SESSION)
		return 0;
	
	buffer = (unsigned char *) scsi_malloc(512);
	if(!buffer) return -ENOMEM;
	
	sector   = 0;         /* the multisession sector offset goes here  */
	is_xa    = 0;         /* flag: the CD uses XA-Sectors              */
	no_multi = 0;         /* flag: the drive can't handle multisession */
	rc       = 0;
    
	switch(VENDOR_ID) {
	
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
		break;

	case VENDOR_HP_4020:
		/* Fallthrough */
	case VENDOR_HP_6020:
		cmd[0] = READ_TOC;
		cmd[1] = (scsi_CDs[minor].device->lun << 5);
		cmd[8] = (VENDOR_ID == VENDOR_HP_4020) ?
			0x04 : 0x0c;
		cmd[9] = 0x40;
		rc = sr_do_ioctl(minor, cmd, buffer,
		    (VENDOR_ID == VENDOR_HP_4020) ? 0x04 : 0x0c);
		if (rc != 0) {
			break;
		}
		if ((rc = buffer[2]) == 0) {
			printk (KERN_WARNING
				"sr (hp): No finished session\n");
			break;
		}

		if (VENDOR_ID == VENDOR_HP_4020) {
		    cmd[0] = READ_TOC; /* Read TOC */
		    cmd[1] = (scsi_CDs[minor].device->lun << 5);
		    cmd[6] = rc & 0x7f;  /* number of last session */
		    cmd[8] = 0x0c;
		    cmd[9] = 0x40;
		    rc = sr_do_ioctl(minor, cmd, buffer, 12);	
		    if (rc != 0) {
			    break;
		    }
		}

		sector = buffer[11] + (buffer[10] << 8) +
			(buffer[9] << 16) + (buffer[8] << 24);
		break;

	case VENDOR_SONY_LIKE:
		memset(cmd,0,12);
		cmd[0] = READ_TOC;
		cmd[1] = (scsi_CDs[minor].device->lun << 5);
		cmd[8] = 12;
		cmd[9] = 0x40;
		rc = sr_do_ioctl(minor, cmd, buffer, 12);	
		if (rc != 0) {
			break;
		}
		if ((buffer[0] << 8) + buffer[1] < 0x0a) {
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
		break;
		
	case VENDOR_CAN_NOT_HANDLE:
		sector = 0;
		no_multi = 1;
		break;

	default:
		/* should not happen */
		printk(KERN_WARNING
		       "sr: unknown vendor code (%i), not initialized ?\n",
		       VENDOR_ID);
		sector = 0;
		no_multi = 1;
		break;
	}
   
	scsi_CDs[minor].xa_flag = 0;
	if (CDS_AUDIO != sr_disk_status(cdi)) { 
	    /* read a sector in raw mode to check the sector format */
	    raw_sector = (unsigned char *) scsi_malloc(2048+512);
	    if (!buffer) return -ENOMEM;
	    if (0 == sr_read_sector(minor,sector+16,CD_FRAMESIZE_RAW1,
				    raw_sector)){
		is_xa = (raw_sector[3] == 0x02);
		if (sector > 0 && !is_xa)
			printk(KERN_INFO "sr: broken CD found: It is "
			       "multisession, but has'nt XA sectors\n");
	    } else {
		/* read a raw sector failed for some reason. */
		is_xa = (sector > 0);
	    }
	    scsi_free(raw_sector, 2048+512);
	}
#ifdef DEBUG
	else printk("sr: audio CD found\n");
#endif

	scsi_CDs[minor].ms_offset = sector;
	scsi_CDs[minor].xa_flag = is_xa;
	if (no_multi)
		cdi->mask |= CDC_MULTI_SESSION;

#ifdef DEBUG
	printk(KERN_DEBUG
	       "sr: multisession offset=%lu, XA=%s\n",
	       sector,is_xa ? "yes" : "no");
#endif

	scsi_free(buffer, 512);
	return rc;
}

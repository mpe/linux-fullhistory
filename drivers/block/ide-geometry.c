/*
 * linux/drivers/block/ide-geometry.c
 */
#include <linux/ide.h>

#include <asm/io.h>

extern ide_drive_t * get_info_ptr(kdev_t);
extern unsigned long current_capacity (ide_drive_t *);

/*
 * We query CMOS about hard disks : it could be that we have a SCSI/ESDI/etc
 * controller that is BIOS compatible with ST-506, and thus showing up in our
 * BIOS table, but not register compatible, and therefore not present in CMOS.
 *
 * Furthermore, we will assume that our ST-506 drives <if any> are the primary
 * drives in the system -- the ones reflected as drive 1 or 2.  The first
 * drive is stored in the high nibble of CMOS byte 0x12, the second in the low
 * nibble.  This will be either a 4 bit drive type or 0xf indicating use byte
 * 0x19 for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.  A non-zero value
 * means we have an AT controller hard disk for that drive.
 *
 * Of course, there is no guarantee that either drive is actually on the
 * "primary" IDE interface, but we don't bother trying to sort that out here.
 * If a drive is not actually on the primary interface, then these parameters
 * will be ignored.  This results in the user having to supply the logical
 * drive geometry as a boot parameter for each drive not on the primary i/f.
 */
/*
 * The only "perfect" way to handle this would be to modify the setup.[cS] code
 * to do BIOS calls Int13h/Fn08h and Int13h/Fn48h to get all of the drive info
 * for us during initialization.  I have the necessary docs -- any takers?  -ml
 */
/*
 * I did this, but it doesnt work - there is no reasonable way to find the
 * correspondence between the BIOS numbering of the disks and the Linux
 * numbering. -aeb
 *
 * The code below is bad. One of the problems is that drives 1 and 2
 * may be SCSI disks (even when IDE disks are present), so that
 * the geometry we read here from BIOS is attributed to the wrong disks.
 * Consequently, also the "drive->present = 1" below is a mistake.
 *
 * Eventually the entire routine below should be removed.
 */
void probe_cmos_for_drives (ide_hwif_t *hwif)
{
#ifdef __i386__
	extern struct drive_info_struct drive_info;
	byte cmos_disks, *BIOS = (byte *) &drive_info;
	int unit;

#ifdef CONFIG_BLK_DEV_PDC4030
	if (hwif->chipset == ide_pdc4030 && hwif->channel != 0)
		return;
#endif /* CONFIG_BLK_DEV_PDC4030 */
	outb_p(0x12,0x70);		/* specify CMOS address 0x12 */
	cmos_disks = inb_p(0x71);	/* read the data from 0x12 */
	/* Extract drive geometry from CMOS+BIOS if not already setup */
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		if ((cmos_disks & (0xf0 >> (unit*4))) &&
		    !drive->present && !drive->nobios) {
			drive->cyl   = drive->bios_cyl  = *(unsigned short *)BIOS;
			drive->head  = drive->bios_head = *(BIOS+2);
			drive->sect  = drive->bios_sect = *(BIOS+14);
			drive->ctl   = *(BIOS+8);
#if 0
			drive->present = 1;
#endif
		}
		BIOS += 16;
	}
#endif
}


/*
 * If heads is nonzero: find a translation with this many heads and S=63.
 * Otherwise: find out how OnTrack Disk Manager would translate the disk.
 */
static void
ontrack(ide_drive_t *drive, int heads, int *c, int *h, int *s) {
	static const byte dm_head_vals[] = {4, 8, 16, 32, 64, 128, 255, 0};
	const byte *headp = dm_head_vals;
	unsigned long total, tracks;

	/*
	 * The specs say: take geometry as obtained from Identify,
	 * compute total capacity C*H*S from that, and truncate to
	 * 1024*255*63. Now take S=63, H the first in the sequence
	 * 4, 8, 16, 32, 64, 128, 255 such that 63*H*1024 >= total.
	 */
	total = DRIVER(drive)->capacity(drive);

	*s = 63;

	if (heads) {
		*h = heads;
		*c = total / (63 * heads);
		return;
	}

#if 0
	while (63 * headp[0] * 1024 < total && headp[1] != 0)
		 headp++;
	*h = headp[0];
	*c = total / (63 * headp[0]);
#else
	/* The code below differs in two aspects:
	   (i) It will not produce geometries like C/H/S = 1024/64/63
		because of the `>='. This follows OnTracks text (which
		claims that 512 <= C <= 1023), but not OnTracks code.
	   (ii) It starts dividing by 63, so that a rounding down occurs.
		For example, with C=11159, H=10, S=37 we find total=4128830
		and DM would make C=512, H=128, S=63, but we make 1024/64/63
		if `>=' is replaced by `>'.
	   The reason we use this code is mainly that we have done so for
	   a long time without getting complaints.
	*/

	tracks = total / 63;
	while (*c >= 1024) {
		*h = *headp;
		*c = tracks / *h;
		if (*++headp == 0)
			break;
	}
#endif
}

/*
 * This routine is called from the partition-table code in pt/msdos.c.
 * It has two tasks:
 * (i) to handle Ontrack DiskManager by offsetting everything by 63 sectors,
 *  or to handle EZdrive by remapping sector 0 to sector 1.
 * (ii) to invent a translated geometry.
 * Part (i) is suppressed if the user specifies the "noremap" option
 * on the command line.
 * Part (ii) is suppressed if the user specifies an explicit geometry.
 *
 * The ptheads parameter is either 0 or tells about the number of
 * heads shown by the end of the first nonempty partition.
 * If this is either 16, 32, 64, 128, 240 or 255 we'll believe it.
 *
 * The xparm parameter has the following meaning:
 *	 0 = convert to CHS with fewer than 1024 cyls
 *	     using the same method as Ontrack DiskManager.
 *	 1 = same as "0", plus offset everything by 63 sectors.
 *	-1 = similar to "0", plus redirect sector 0 to sector 1.
 *	 2 = convert to a CHS geometry with "ptheads" heads.
 *
 * Returns 0 if the translation was not possible, if the device was not 
 * an IDE disk drive, or if a geometry was "forced" on the commandline.
 * Returns 1 if the geometry translation was successful.
 */
int ide_xlate_1024 (kdev_t i_rdev, int xparm, int ptheads, const char *msg)
{
	ide_drive_t *drive;
	const char *msg1 = "";
	int heads = 0;
	int c, h, s;
	int transl = 1;		/* try translation */
	int ret = 0;

	drive = get_info_ptr(i_rdev);
	if (!drive)
		return 0;

	/* remap? */
	if (drive->remap_0_to_1 != 2) {
		if (xparm == 1) {		/* DM */
			drive->sect0 = 63;
			msg1 = " [remap +63]";
			ret = 1;
		} else if (xparm == -1) {	/* EZ-Drive */
			if (drive->remap_0_to_1 == 0) {
				drive->remap_0_to_1 = 1;
				msg1 = " [remap 0->1]";
				ret = 1;
			}
		}
	}

	/* There used to be code here that assigned drive->id->CHS
	   to drive->CHS and that to drive->bios_CHS. However,
	   some disks have id->C/H/S = 4092/16/63 but are larger than 2.1 GB.
	   In such cases that code was wrong.  Moreover,
	   there seems to be no reason to do any of these things. */

	/* translate? */
	if (drive->forced_geom)
		transl = 0;

	/* does ptheads look reasonable? */
	if (ptheads == 32 || ptheads == 64 || ptheads == 128 ||
	    ptheads == 240 || ptheads == 255)
		heads = ptheads;

	if (xparm == 2) {
		if (!heads ||
		   (drive->bios_head >= heads && drive->bios_sect == 63))
			transl = 0;
	}
	if (xparm == -1) {
		if (drive->bios_head > 16)
			transl = 0;     /* we already have a translation */
	}

	if (transl) {
		ontrack(drive, heads, &c, &h, &s);
		drive->bios_cyl = c;
		drive->bios_head = h;
		drive->bios_sect = s;
		ret = 1;
	}

	drive->part[0].nr_sects = current_capacity(drive);

	if (ret)
		printk("%s%s [%d/%d/%d]", msg, msg1,
		       drive->bios_cyl, drive->bios_head, drive->bios_sect);
	return ret;
}

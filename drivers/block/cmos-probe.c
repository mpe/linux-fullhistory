/*
 *  linux/drivers/block/cmos-probe.c	Version 1.00  August 16, 1999
 *
 *  Copyright (C) 1994-1999  Linus Torvalds & authors (see below)
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/ide.h>


#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

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
 *
 * The only "perfect" way to handle this would be to modify the setup.[cS] code
 * to do BIOS calls Int13h/Fn08h and Int13h/Fn48h to get all of the drive info
 * for us during initialization.  I have the necessary docs -- any takers?  -ml
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
		if ((cmos_disks & (0xf0 >> (unit*4))) && !drive->present && !drive->nobios) {
			drive->cyl   = drive->bios_cyl  = *(unsigned short *)BIOS;
			drive->head  = drive->bios_head = *(BIOS+2);
			drive->sect  = drive->bios_sect = *(BIOS+14);
			drive->ctl   = *(BIOS+8);
			drive->present = 1;
		}
		BIOS += 16;
	}
#endif
}

/*
 *  linux/drivers/block/proc_ide.c	Version 1.01	December 12, 1997
 *
 *  Copyright (C) 1997-1998	Mark Lord
 */

/*
 * This is the /proc/ide/ filesystem implementation.
 *
 * The major reason this exists is to provide sufficient access
 * to driver and config data, such that user-mode programs can
 * be developed to handle chipset tuning for most PCI interfaces.
 * This should provide better utilities, and less kernel bloat.
 *
 * The entire pci config space for a PCI interface chipset can be
 * retrieved by just reading it.  e.g.    "cat /proc/ide3/pci"
 *
 * To modify registers, do something like:
 *   echo "40:88" >/proc/ide/ide3/pci
 * That expression writes 0x88 to pci config register 0x40
 * on the chip which controls ide3.  Multiple tuples can be issued,
 * and the writes will be completed as an atomic set:
 *   echo "40:88 41:35 42:00 43:00" >/proc/ide/ide3/pci
 * All numbers must be pairs of ascii hex digits.
 *
 * Also useful, "cat /proc/ide0/hda/identify" will issue an IDENTIFY
 * (or PACKET_IDENTIFY) command to /dev/hda, and then dump out the
 * returned data as 256 16-bit words.  The "hdparm" utility will
 * be updated someday soon to use this mechanism.
 *
 * Feel free to develop and distribute fancy GUI configuration
 * utilities for you favorite PCI chipsets.  I'll be working on
 * one for the Promise 20246 someday soon.  -ml
 *
 */

#include <linux/config.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/ctype.h>
#include "ide.h"

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

/*
 * Standard exit stuff:
 */
#define PROC_IDE_READ_RETURN(page,start,off,count,eof,len) \
{					\
	len -= off;			\
	if (len < count) {		\
		*eof = 1;		\
		if (len <= 0)		\
			return 0;	\
	} else				\
		len = count;		\
	*start = page + off;		\
	return len;			\
}


#ifdef CONFIG_PCI

static int ide_getxdigit(char c)
{
	int digit;
	if (isdigit(c))
		digit = c - '0';
	else if (isxdigit(c))
		digit = tolower(c) - 'a' + 10;
	else
		digit = -1;
	return digit;
}


static int xx_xx_parse_error (const char *start, unsigned long maxlen)
{
	char errbuf[7];
	int i, len = MIN(6, maxlen);
	for (i = 0; i < len; ++i) {
		char c = start[i];
		if (!c || c == '\n')
			c = '\0';
		else if (iscntrl(c))
			c = '?';
		errbuf[i] = c;
	}
	errbuf[i] = '\0';
	printk("proc_ide: error: expected 'xx:xx', but got '%s'\n", errbuf);
	return -EINVAL;
}

static int proc_ide_write_pci
	(struct file *file, const char *buffer, unsigned long count, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	int		for_real = 0;
	unsigned long	n, flags;
	const char	*start;

	if (!suser())
		return -EACCES;
	/*
	 * Skip over leading whitespace
	 */
	while (count && isspace(*buffer)) {
		--count;
		++buffer;
	}
	/*
	 * Do one full pass to verify all parameters,
	 * then do another to actually write the pci regs.
	 */
	save_flags(flags);
	do {
		const char *p = buffer;
		n = count;
		if (for_real) {
			unsigned long timeout = jiffies + (3 * HZ);
			cli();	/* ensure all PCI writes are done together */
			while (((ide_hwgroup_t *)(hwif->hwgroup))->active || (hwif->mate && ((ide_hwgroup_t *)(hwif->mate->hwgroup))->active)) {
				sti();
				if (0 < (signed long)(timeout - jiffies)) {
					printk("/proc/ide/%s/pci: channel(s) busy, cannot write\n", hwif->name);
					return -EBUSY;
				}
				cli();
			}
		}
		while (n) {
			int d1, d2, rc;
			byte reg, val;
			start = p;
#if 0
			printk("loop(%d): n=%ld, input=%.5s\n", for_real, n, p);
#endif
			if (n < 5)
				goto parse_error;
			if (0 > (d1 = ide_getxdigit(*p++)) || 0 > (d2 = ide_getxdigit(*p++)))
				goto parse_error;
			reg = (d1 << 4) | d2;
			if (*p++ != ':')
				goto parse_error;
			if (0 > (d1 = ide_getxdigit(*p++)) || 0 > (d2 = ide_getxdigit(*p++)))
				goto parse_error;
			val = (d1 << 4) | d2;
			if (n > 5 && !isspace(*p))
				goto parse_error;
			n -= 5;
			while (n && isspace(*p)) {
				--n;
				++p;
			}
			if (for_real) {
#if 0
				printk("proc_ide_write_pci: reg=0x%02x, val=0x%02x\n", reg, val);
#endif
				rc = pcibios_write_config_byte(hwif->pci_bus, hwif->pci_fn, reg, val);
				if (rc) {
					restore_flags(flags);
					printk("proc_ide_write_pci: error writing bus %d fn %d reg 0x%02x value 0x%02x\n",
						hwif->pci_bus, hwif->pci_fn, reg, val);
					printk("proc_ide_write_pci: %s\n", pcibios_strerror(rc));
					return -EIO;
				}
			}
		}
	} while (!for_real++);
	restore_flags(flags);
	return count;
parse_error:
	restore_flags(flags);
	return xx_xx_parse_error(start, n);
}

static int proc_ide_read_pci
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	char		*out = page;
	int		len, reg = 0;

	out += sprintf(out, "Bus %d Function %d Vendor %04x Device %04x Channel %d\n",
		hwif->pci_bus, hwif->pci_fn, hwif->pci_devid.vid, hwif->pci_devid.did, hwif->channel);
	do {
		byte val;
		int rc = pcibios_read_config_byte(hwif->pci_bus, hwif->pci_fn, reg, &val);
		if (rc) {
			printk("proc_ide_read_pci: error reading bus %d fn %d reg 0x%02x\n",
				hwif->pci_bus, hwif->pci_fn, reg);
			printk("proc_ide_read_pci: %s\n", pcibios_strerror(rc));
			return -EIO;
			out += sprintf(out, "??%c", (++reg & 0xf) ? ' ' : '\n');
		} else
			out += sprintf(out, "%02x%c", val, (++reg & 0xf) ? ' ' : '\n');
	} while (reg < 0x100);
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_imodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;
	const char	*vids, *dids;

	vids = pci_strvendor(hwif->pci_devid.vid);
	dids = pci_strdev(hwif->pci_devid.vid, hwif->pci_devid.did);
	len = sprintf(page,"%s: %s\n", vids ? vids : "(none)", dids ? dids : "(none)");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}
#endif	/* CONFIG_PCI */

static int proc_ide_read_type
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;
	const char	*name;

	switch (hwif->chipset) {
		case ide_unknown:	name = "(none)";	break;
		case ide_generic:	name = "generic";	break;
		case ide_pci:		name = "pci";		break;
		case ide_cmd640:	name = "cmd640";	break;
		case ide_dtc2278:	name = "dtc2278";	break;
		case ide_ali14xx:	name = "ali14xx";	break;
		case ide_qd6580:	name = "qd6580";	break;
		case ide_umc8672:	name = "umc8672";	break;
		case ide_ht6560b:	name = "ht6560b";	break;
		case ide_pdc4030:	name = "pdc4030";	break;
		case ide_rz1000:	name = "rz1000";	break;
		case ide_trm290:	name = "trm290";	break;
		case ide_4drives:	name = "4drives";	break;
		default:		name = "(unknown)";	break;
	}
	len = sprintf(page, "%s\n", name);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_mate
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;

	len = sprintf(page, "%s\n", hwif->mate->name);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_channel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;

	page[0] = hwif->channel ? '1' : '0';
	page[1] = '\n';
	len = 2;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_get_identify (ide_drive_t *drive, byte *buf)
{
	struct request rq;
	byte *end;

	ide_init_drive_cmd(&rq);
	rq.buffer = buf;
	*buf++ = (drive->media == ide_disk) ? WIN_IDENTIFY : WIN_PIDENTIFY;
	*buf++ = 0;
	*buf++ = 0;
	*buf++ = 1;
	end = buf + (SECTOR_WORDS * 4);
	while (buf != end)
		*buf++ = 0;	/* pre-zero it, in case identify fails */
	(void) ide_do_drive_cmd(drive, &rq, ide_wait);
	return 0;
}

static int proc_ide_read_identify
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;

	if (!proc_ide_get_identify(drive, page)) {
		unsigned short *val = ((unsigned short *)page) + 2;
		char *out = ((char *)val) + (SECTOR_WORDS * 4);
		page = out;
		do {
			out += sprintf(out, "%04x%c", le16_to_cpu(*val), (++i & 7) ? ' ' : '\n');
			val += 1;
		} while (i < (SECTOR_WORDS * 2));
		len = out - page;
	}
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_settings
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	out += sprintf(out,"multcount    %i\n", drive->mult_count);
	out += sprintf(out,"io_32bit     %i\n", drive->io_32bit);
	out += sprintf(out,"unmaskirq    %i\n", drive->unmask);
	out += sprintf(out,"using_dma    %i\n", drive->using_dma);
	out += sprintf(out,"nowerr       %i\n", drive->bad_wstat == BAD_R_STAT);
	out += sprintf(out,"keepsettings %i\n", drive->keep_settings);
	out += sprintf(out,"nice         %i/%i/%i\n", drive->nice0, drive->nice1, drive->nice2);
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

int proc_ide_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	int		len;

	len = sprintf(page,"%li\n", ((ide_driver_t *)drive->driver)->capacity(drive));
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

int proc_ide_read_geometry
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	out += sprintf(out,"physical     %hi/%hi/%hi\n", drive->cyl, drive->head, drive->sect);
	out += sprintf(out,"logical      %hi/%hi/%hi\n", drive->bios_cyl, drive->bios_head, drive->bios_sect);
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_dmodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	struct hd_driveid *id = drive->id;
	int		len;

	len = sprintf(page, "%.40s\n", (id && id->model[0]) ? (char *)id->model : "(none)");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_driver
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_driver_t	*driver = (ide_driver_t *) drive->driver;
	int		len;

	if (!driver)
		len = sprintf(page, "(none)\n");
	else
		len = sprintf(page, "%s version %s\n", driver->name, driver->version);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_media
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	const char	*media;
	int		len;

	switch (drive->media) {
		case ide_disk:	media = "disk\n";
				break;
		case ide_cdrom:	media = "cdrom\n";
				break;
		case ide_tape:	media = "tape\n";
				break;
		case ide_floppy:media = "floppy\n";
				break;
		default:	media = "UNKNOWN\n";
				break;
	}
	strcpy(page,media);
	len = strlen(media);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}


static ide_proc_entry_t generic_drive_entries[] = {
	{ "capacity", proc_ide_read_capacity, NULL },
	{ "driver", proc_ide_read_driver, NULL },
	{ "identify", proc_ide_read_identify, NULL },
	{ "media", proc_ide_read_media, NULL },
	{ "model", proc_ide_read_dmodel, NULL },
	{ "settings", proc_ide_read_settings, NULL },
	{ NULL, NULL, NULL }
};

void ide_add_proc_entries(ide_drive_t *drive, ide_proc_entry_t *p)
{
	struct proc_dir_entry *ent;

	if (!drive->proc || !p)
		return;
	while (p->name != NULL) {
		ent = create_proc_entry(p->name, 0, drive->proc);
		if (!ent) return;
		ent->data = drive;
		ent->read_proc = p->read_proc;
		ent->write_proc = p->write_proc;
		p++;
	}
}

void ide_remove_proc_entries(ide_drive_t *drive, ide_proc_entry_t *p)
{
	if (!drive->proc || !p)
		return;
	while (p->name != NULL) {
		remove_proc_entry(p->name, drive->proc);
		p++;
	}
}

static void create_proc_ide_drives (ide_hwif_t *hwif, struct proc_dir_entry *parent)
{
	int	d;

	for (d = 0; d < MAX_DRIVES; d++) {
		ide_drive_t *drive = &hwif->drives[d];

		if (!drive->present)
			continue;
		drive->proc = create_proc_entry(drive->name, S_IFDIR, parent);
		if (drive->proc)
			ide_add_proc_entries(drive, generic_drive_entries);
	}
}

static void create_proc_ide_interfaces (struct proc_dir_entry *parent)
{
	int	h;
	struct proc_dir_entry *hwif_ent, *ent;

	for (h = 0; h < MAX_HWIFS; h++) {
		ide_hwif_t *hwif = &ide_hwifs[h];

		if (!hwif->present)
			continue;
		hwif_ent = create_proc_entry(hwif->name, S_IFDIR, parent);
		if (!hwif_ent) return;
#ifdef CONFIG_PCI
		if (!IDE_PCI_DEVID_EQ(hwif->pci_devid, IDE_PCI_DEVID_NULL)) {
			ent = create_proc_entry("pci", 0, hwif_ent);
			if (!ent) return;
			ent->data = hwif;
			ent->read_proc  = proc_ide_read_pci;
			ent->write_proc = proc_ide_write_pci;;

			ent = create_proc_entry("model", 0, hwif_ent);
			if (!ent) return;
			ent->data = hwif;
			ent->read_proc  = proc_ide_read_imodel;
		}
#endif	/* CONFIG_PCI */
		ent = create_proc_entry("channel", 0, hwif_ent);
		if (!ent) return;
		ent->data = hwif;
		ent->read_proc  = proc_ide_read_channel;

		if (hwif->mate && hwif->mate->present) {
			ent = create_proc_entry("mate", 0, hwif_ent);
			if (!ent) return;
			ent->data = hwif;
			ent->read_proc  = proc_ide_read_mate;
		}

		ent = create_proc_entry("type", 0, hwif_ent);
		if (!ent) return;
		ent->data = hwif;
		ent->read_proc  = proc_ide_read_type;

		create_proc_ide_drives(hwif, hwif_ent);
	}
}

void proc_ide_init(void)
{
	struct proc_dir_entry *ent;
	ent = create_proc_entry("ide", S_IFDIR, 0);
	if (!ent) return;
	create_proc_ide_interfaces(ent);
}

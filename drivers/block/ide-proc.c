/*
 *  linux/drivers/block/ide-proc.c	Version 1.03	January   2, 1998
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
 * retrieved by just reading it.  e.g.    "cat /proc/ide3/config"
 *
 * To modify registers *safely*, do something like:
 *   echo "P40:88" >/proc/ide/ide3/config
 * That expression writes 0x88 to pci config register 0x40
 * on the chip which controls ide3.  Multiple tuples can be issued,
 * and the writes will be completed as an atomic set:
 *   echo "P40:88 P41:35 P42:00 P43:00" >/proc/ide/ide3/config
 *
 * All numbers must be specified using pairs of ascii hex digits.
 * It is important to note that these writes will be performed
 * after waiting for the IDE controller (both interfaces)
 * to be completely idle, to ensure no corruption of I/O in progress.
 *
 * Non-PCI registers can also be written, using "R" in place of "P"
 * in the above examples.  The size of the port transfer is determined
 * by the number of pairs of hex digits given for the data.  If a two
 * digit value is given, the write will be a byte operation; if four
 * digits are used, the write will be performed as a 16-bit operation;
 * and if eight digits are specified, a 32-bit "dword" write will be
 * performed.  Odd numbers of digits are not permitted.
 *
 * If there is an error *anywhere* in the string of registers/data
 * then *none* of the writes will be performed.
 *
 * Drive/Driver settings can be retrieved by reading the drive's
 * "settings" files.  e.g.    "cat /proc/ide0/hda/settings"
 * To write a new value "val" into a specific setting "name", use:
 *   echo "name:val" >/proc/ide/ide0/hda/settings
 *
 * Also useful, "cat /proc/ide0/hda/[identify, smart_values,
 * smart_thresholds, capabilities]" will issue an IDENTIFY /
 * PACKET_IDENTIFY / SMART_READ_VALUES / SMART_READ_THRESHOLDS /
 * SENSE CAPABILITIES command to /dev/hda, and then dump out the
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
#include <asm/io.h>
#include "ide.h"

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

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

static int ide_getdigit(char c)
{
	int digit;
	if (isdigit(c))
		digit = c - '0';
	else
		digit = -1;
	return digit;
}

static int xx_xx_parse_error (const char *data, unsigned long len, const char *msg)
{
	char errbuf[16];
	int i;
	if (len >= sizeof(errbuf))
		len = sizeof(errbuf) - 1;
	for (i = 0; i < len; ++i) {
		char c = data[i];
		if (!c || c == '\n')
			c = '\0';
		else if (iscntrl(c))
			c = '?';
		errbuf[i] = c;
	}
	errbuf[i] = '\0';
	printk("proc_ide: error: %s: '%s'\n", msg, errbuf);
	return -EINVAL;
}

static int proc_ide_write_config
	(struct file *file, const char *buffer, unsigned long count, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	int		for_real = 0;
	unsigned long	startn = 0, n, flags;
	const char	*start = NULL, *msg = NULL;

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
		const char *p;
		if (for_real) {
			unsigned long timeout = jiffies + (3 * HZ);
			ide_hwgroup_t *mygroup = (ide_hwgroup_t *)(hwif->hwgroup);
			ide_hwgroup_t *mategroup = NULL;
			if (hwif->mate && hwif->mate->hwgroup)
				mategroup = (ide_hwgroup_t *)(hwif->mate->hwgroup);
			cli();	/* ensure all PCI writes are done together */
			while (mygroup->active || (mategroup && mategroup->active)) {
				restore_flags(flags);
				if (0 < (signed long)(jiffies - timeout)) {
					printk("/proc/ide/%s/pci: channel(s) busy, cannot write\n", hwif->name);
					return -EBUSY;
				}
				cli();
			}
		}
		p = buffer;
		n = count;
		while (n > 0) {
			int d, digits;
			unsigned int reg = 0, val = 0, is_pci;
			start = p;
			startn = n--;
			switch (*p++) {
				case 'R':	is_pci = 0;
						break;
				case 'P':	is_pci = 1;
						break;
				default:	msg = "expected 'R' or 'P'";
						goto parse_error;
			}
			digits = 0;
			while (n > 0 && (d = ide_getxdigit(*p)) >= 0) {
				reg = (reg << 4) | d;
				--n;
				++p;
				++digits;
			}
			if (!digits || (digits > 4) || (is_pci && reg > 0xff)) {
				msg = "bad/missing register number";
				goto parse_error;
			}
			if (--n < 0 || *p++ != ':') {
				msg = "missing ':'";
				goto parse_error;
			}
			digits = 0;
			while (n > 0 && (d = ide_getxdigit(*p)) >= 0) {
				val = (val << 4) | d;
				--n;
				++p;
				++digits;
			}
			if (digits != 2 && digits != 4 && digits != 8) {
				msg = "bad data, 2/4/8 digits required";
				goto parse_error;
			}
			if (n > 0 && !isspace(*p)) {
				msg = "expected whitespace after data";
				goto parse_error;
			}
			while (n > 0 && isspace(*p)) {
				--n;
				++p;
			}
			if (is_pci && (reg & ((digits >> 1) - 1))) {
				msg = "misaligned access";
				goto parse_error;
			}
			if (for_real) {
#if 0
				printk("proc_ide_write_config: type=%c, reg=0x%x, val=0x%x, digits=%d\n", is_pci ? 'PCI' : 'non-PCI', reg, val, digits);
#endif
				if (is_pci) {
					int rc = 0;
					switch (digits) {
						case 2:	msg = "byte";
							rc = pcibios_write_config_byte(hwif->pci_bus, hwif->pci_fn, reg, val);
							break;
						case 4:	msg = "word";
							rc = pcibios_write_config_word(hwif->pci_bus, hwif->pci_fn, reg, val);
							break;
						case 8:	msg = "dword";
							rc = pcibios_write_config_dword(hwif->pci_bus, hwif->pci_fn, reg, val);
							break;
					}
					if (rc) {
						restore_flags(flags);
						printk("proc_ide_write_config: error writing %s at bus %d fn %d reg 0x%x value 0x%x\n",
							msg, hwif->pci_bus, hwif->pci_fn, reg, val);
						printk("proc_ide_write_config: %s\n", pcibios_strerror(rc));
						return -EIO;
					}
				} else {	/* not pci */
					switch (digits) {
						case 2:	outb(val, reg);
							break;
						case 4:	outw(val, reg);
							break;
						case 8:	outl(val, reg);
							break;
					}
				}
			}
		}
	} while (!for_real++);
	restore_flags(flags);
	return count;
parse_error:
	restore_flags(flags);
	printk("parse error\n");
	return xx_xx_parse_error(start, startn, msg);
}

static int proc_ide_read_drivers
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char		*out = page;
	int		len;
	ide_module_t	*p = ide_modules;
	ide_driver_t	*driver;

	while (p) {
		driver = (ide_driver_t *) p->info;
		if (p->type == IDE_DRIVER_MODULE && driver)
			out += sprintf(out, "%s version %s\n", driver->name, driver->version);
		p = p->next;
	}
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_config
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	char		*out = page;
	int		len, reg = 0;

	out += sprintf(out, "pci bus %d function %d vendor %04x device %04x channel %d\n",
		hwif->pci_bus, hwif->pci_fn, hwif->pci_devid.vid, hwif->pci_devid.did, hwif->channel);
	do {
		byte val;
		int rc = pcibios_read_config_byte(hwif->pci_bus, hwif->pci_fn, reg, &val);
		if (rc) {
			printk("proc_ide_read_config: error reading bus %d fn %d reg 0x%02x\n",
				hwif->pci_bus, hwif->pci_fn, reg);
			printk("proc_ide_read_config: %s\n", pcibios_strerror(rc));
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

static int proc_ide_get_identify(ide_drive_t *drive, byte *buf)
{
	return ide_wait_cmd(drive, (drive->media == ide_disk) ? WIN_IDENTIFY : WIN_PIDENTIFY, 0, 0, 1, buf);
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
	ide_settings_t	*setting = (ide_settings_t *) drive->settings;
	char		*out = page;
	int		len, rc, mul_factor, div_factor;

	out += sprintf(out, "name\t\t\tvalue\t\tmin\t\tmax\t\tmode\n");
	out += sprintf(out, "----\t\t\t-----\t\t---\t\t---\t\t----\n");
	while(setting) {
		mul_factor = setting->mul_factor;
		div_factor = setting->div_factor;
		out += sprintf(out, "%-24s", setting->name);
		if ((rc = ide_read_setting(drive, setting)) >= 0)
			out += sprintf(out, "%-16d", rc * mul_factor / div_factor);
		else
			out += sprintf(out, "%-16s", "write-only");
		out += sprintf(out, "%-16d%-16d", (setting->min * mul_factor + div_factor - 1) / div_factor, setting->max * mul_factor / div_factor);
		if (setting->rw & SETTING_READ)
			out += sprintf(out, "r");
		if (setting->rw & SETTING_WRITE)
			out += sprintf(out, "w");
		out += sprintf(out, "\n");
		setting = setting->next;
	}
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

#define MAX_LEN	30

static int proc_ide_write_settings
	(struct file *file, const char *buffer, unsigned long count, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_hwif_t	*hwif = HWIF(drive);
	char		name[MAX_LEN + 1];
	int		for_real = 0, len;
	unsigned long	n, flags;
	const char	*start = NULL;
	ide_settings_t	*setting;

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
		const char *p;
		if (for_real) {
			unsigned long timeout = jiffies + (3 * HZ);
			ide_hwgroup_t *mygroup = (ide_hwgroup_t *)(hwif->hwgroup);
			ide_hwgroup_t *mategroup = NULL;
			if (hwif->mate && hwif->mate->hwgroup)
				mategroup = (ide_hwgroup_t *)(hwif->mate->hwgroup);
			cli();	/* ensure all PCI writes are done together */
			while (mygroup->active || (mategroup && mategroup->active)) {
				restore_flags(flags);
				if (0 < (signed long)(jiffies - timeout)) {
					printk("/proc/ide/%s/pci: channel(s) busy, cannot write\n", hwif->name);
					return -EBUSY;
				}
				cli();
			}
		}
		p = buffer;
		n = count;
		while (n > 0) {
			int d, digits;
			unsigned int val = 0;
			start = p;

			while (n > 0 && *p != ':') {
				--n;
				p++;
			}
			if (*p != ':')
				goto parse_error;
			len = IDE_MIN(p - start, MAX_LEN);
			strncpy(name, start, IDE_MIN(len, MAX_LEN));
			name[len] = 0;

			if (n > 0) {
				--n;
				p++;
			} else
				goto parse_error;
			
			digits = 0;
			while (n > 0 && (d = ide_getdigit(*p)) >= 0) {
				val = (val * 10) + d;
				--n;
				++p;
				++digits;
			}
			if (n > 0 && !isspace(*p))
				goto parse_error;
			while (n > 0 && isspace(*p)) {
				--n;
				++p;
			}
			setting = ide_find_setting_by_name(drive, name);
			if (!setting)
				goto parse_error;

			if (for_real)
				ide_write_setting(drive, setting, val * setting->div_factor / setting->mul_factor);
		}
	} while (!for_real++);
	restore_flags(flags);
	return count;
parse_error:
	restore_flags(flags);
	printk("proc_ide_write_settings(): parse error\n");
	return -EINVAL;
}

int proc_ide_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_driver_t    *driver = (ide_driver_t *) drive->driver;
	int		len;

	if (!driver)
	    len = sprintf(page, "(none)\n");
        else
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

static int proc_ide_write_driver
	(struct file *file, const char *buffer, unsigned long count, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;

	if (!suser())
		return -EACCES;
	if (ide_replace_subdriver(drive, buffer))
		return -EINVAL;
	return count;
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
	{ "driver", proc_ide_read_driver, proc_ide_write_driver },
	{ "identify", proc_ide_read_identify, NULL },
	{ "media", proc_ide_read_media, NULL },
	{ "model", proc_ide_read_dmodel, NULL },
	{ "settings", proc_ide_read_settings, proc_ide_write_settings },
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

static int proc_ide_readlink(struct proc_dir_entry *de, char *page)
{
	int n = (de->name[2] - 'a') / 2;
	return sprintf(page, "ide%d/%s", n, de->name);
}

static void create_proc_ide_drives (ide_hwif_t *hwif, struct proc_dir_entry *parent, struct proc_dir_entry *root)
{
	int	d;
	struct proc_dir_entry *ent;

	for (d = 0; d < MAX_DRIVES; d++) {
		ide_drive_t *drive = &hwif->drives[d];

		if (!drive->present)
			continue;
		drive->proc = create_proc_entry(drive->name, S_IFDIR, parent);
		if (drive->proc)
			ide_add_proc_entries(drive, generic_drive_entries);

		ent = create_proc_entry(drive->name, S_IFLNK | S_IRUGO | S_IWUGO | S_IXUGO, root);
		if (!ent) return;
		ent->data = drive;
		ent->readlink_proc = proc_ide_readlink;
		ent->nlink = 1;
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
			ent = create_proc_entry("config", 0, hwif_ent);
			if (!ent) return;
			ent->data = hwif;
			ent->read_proc  = proc_ide_read_config;
			ent->write_proc = proc_ide_write_config;;

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

		create_proc_ide_drives(hwif, hwif_ent, parent);
	}
}

void proc_ide_init(void)
{
	struct proc_dir_entry *root, *ent;
	root = create_proc_entry("ide", S_IFDIR, 0);
	if (!root) return;
	create_proc_ide_interfaces(root);

	ent = create_proc_entry("drivers", 0, root);
	if (!ent) return;
	ent->read_proc  = proc_ide_read_drivers;
}

/*
 *	$Id: proc.c,v 1.13 1998/05/12 07:36:07 mj Exp $
 *
 *	Procfs interface for the PCI bus.
 *
 *	Copyright (c) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/byteorder.h>

#define PCI_CFG_SPACE_SIZE 256

static loff_t
proc_bus_pci_lseek(struct file *file, loff_t off, int whence)
{
	loff_t new;

	switch (whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	case 2:
		new = PCI_CFG_SPACE_SIZE + off;
		break;
	default:
		return -EINVAL;
	}
	if (new < 0 || new > PCI_CFG_SPACE_SIZE)
		return -EINVAL;
	return (file->f_pos = new);
}

static ssize_t
proc_bus_pci_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	struct inode *ino = file->f_dentry->d_inode;
	struct proc_dir_entry *dp = ino->u.generic_ip;
	struct pci_dev *dev = dp->data;
	int pos = *ppos;
	int cnt, size;

	/*
	 * Normal users can read only the standardized portion of the
	 * configuration space as several chips lock up when trying to read
	 * undefined locations (think of Intel PIIX4 as a typical example).
	 */

	if (capable(CAP_SYS_ADMIN))
		size = PCI_CFG_SPACE_SIZE;
	else if (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
		size = 128;
	else
		size = 64;

	if (pos >= size)
		return 0;
	if (nbytes >= size)
		nbytes = size;
	if (pos + nbytes > size)
		nbytes = size - pos;
	cnt = nbytes;

	if (!access_ok(VERIFY_WRITE, buf, cnt))
		return -EINVAL;

	if ((pos & 1) && cnt) {
		unsigned char val;
		pci_read_config_byte(dev, pos, &val);
		__put_user(val, buf);
		buf++;
		pos++;
		cnt--;
	}

	if ((pos & 3) && cnt > 2) {
		unsigned short val;
		pci_read_config_word(dev, pos, &val);
		__put_user(cpu_to_le16(val), (unsigned short *) buf);
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	while (cnt >= 4) {
		unsigned int val;
		pci_read_config_dword(dev, pos, &val);
		__put_user(cpu_to_le32(val), (unsigned int *) buf);
		buf += 4;
		pos += 4;
		cnt -= 4;
	}

	if (cnt >= 2) {
		unsigned short val;
		pci_read_config_word(dev, pos, &val);
		__put_user(cpu_to_le16(val), (unsigned short *) buf);
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	if (cnt) {
		unsigned char val;
		pci_read_config_byte(dev, pos, &val);
		__put_user(val, buf);
		buf++;
		pos++;
		cnt--;
	}

	*ppos = pos;
	return nbytes;
}

static ssize_t
proc_bus_pci_write(struct file *file, const char *buf, size_t nbytes, loff_t *ppos)
{
	struct inode *ino = file->f_dentry->d_inode;
	struct proc_dir_entry *dp = ino->u.generic_ip;
	struct pci_dev *dev = dp->data;
	int pos = *ppos;
	int cnt;

	if (pos >= PCI_CFG_SPACE_SIZE)
		return 0;
	if (nbytes >= PCI_CFG_SPACE_SIZE)
		nbytes = PCI_CFG_SPACE_SIZE;
	if (pos + nbytes > PCI_CFG_SPACE_SIZE)
		nbytes = PCI_CFG_SPACE_SIZE - pos;
	cnt = nbytes;

	if (!access_ok(VERIFY_READ, buf, cnt))
		return -EINVAL;

	if ((pos & 1) && cnt) {
		unsigned char val;
		__get_user(val, buf);
		pci_write_config_byte(dev, pos, val);
		buf++;
		pos++;
		cnt--;
	}

	if ((pos & 3) && cnt > 2) {
		unsigned short val;
		__get_user(val, (unsigned short *) buf);
		pci_write_config_word(dev, pos, le16_to_cpu(val));
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	while (cnt >= 4) {
		unsigned int val;
		__get_user(val, (unsigned int *) buf);
		pci_write_config_dword(dev, pos, le32_to_cpu(val));
		buf += 4;
		pos += 4;
		cnt -= 4;
	}

	if (cnt >= 2) {
		unsigned short val;
		__get_user(val, (unsigned short *) buf);
		pci_write_config_word(dev, pos, le16_to_cpu(val));
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	if (cnt) {
		unsigned char val;
		__get_user(val, buf);
		pci_write_config_byte(dev, pos, val);
		buf++;
		pos++;
		cnt--;
	}

	*ppos = pos;
	return nbytes;
}

static struct file_operations proc_bus_pci_operations = {
	proc_bus_pci_lseek,
	proc_bus_pci_read,
	proc_bus_pci_write,
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
	NULL,		/* flush */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

static struct inode_operations proc_bus_pci_inode_operations = {
	&proc_bus_pci_operations, /* default base directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

#if BITS_PER_LONG == 32
#define LONG_FORMAT "\t%08lx"
#else
#define LONG_FORMAT "\t%16lx"
#endif

static int
get_pci_dev_info(char *buf, char **start, off_t pos, int count)
{
	struct pci_dev *dev = pci_devices;
	off_t at = 0;
	int len, i, cnt;

	cnt = 0;
	while (dev && count > cnt) {
		len = sprintf(buf, "%02x%02x\t%04x%04x\t%x",
			dev->bus->number,
			dev->devfn,
			dev->vendor,
			dev->device,
			dev->irq);
		/* Here should be 7 and not PCI_NUM_RESOURCES as we need to preserve compatibility */
		for(i=0; i<7; i++)
			len += sprintf(buf+len, LONG_FORMAT,
				       dev->resource[i].start | (dev->resource[i].flags & PCI_REGION_FLAG_MASK));
		for(i=0; i<7; i++)
			len += sprintf(buf+len, LONG_FORMAT, dev->resource[i].start < dev->resource[i].end ?
				       dev->resource[i].end - dev->resource[i].start + 1 : 0);
		buf[len++] = '\n';
		at += len;
		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else
				cnt += len;
			buf += len;
		}
		dev = dev->next;
	}
	return (count > cnt) ? cnt : count;
}

static struct proc_dir_entry *proc_bus_pci_dir;

int pci_proc_attach_device(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct proc_dir_entry *de, *e;
	char name[16];

	if (!(de = bus->procdir)) {
		sprintf(name, "%02x", bus->number);
		de = bus->procdir = create_proc_entry(name, S_IFDIR, proc_bus_pci_dir);
		if (!de)
			return -ENOMEM;
	}
	sprintf(name, "%02x.%x", PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	e = dev->procent = create_proc_entry(name, S_IFREG | S_IRUGO | S_IWUSR, de);
	if (!e)
		return -ENOMEM;
	e->ops = &proc_bus_pci_inode_operations;
	e->data = dev;
	e->size = PCI_CFG_SPACE_SIZE;
	return 0;
}

int pci_proc_detach_device(struct pci_dev *dev)
{
	struct proc_dir_entry *e;

	if ((e = dev->procent)) {
		if (e->count)
			return -EBUSY;
		remove_proc_entry(e->name, dev->bus->procdir);
		dev->procent = NULL;
	}
	return 0;
}

void __init proc_bus_pci_add(struct pci_bus *bus)
{
	while (bus) {
		struct pci_dev *dev;

		for(dev = bus->devices; dev; dev = dev->sibling)
			pci_proc_attach_device(dev);
		if (bus->children)
			proc_bus_pci_add(bus->children);
		bus = bus->next;
	}
}

/*
 *  Backward compatible /proc/pci interface.
 */

static const char *pci_strclass (unsigned int class)
{
	switch (class >> 8) {
	      case PCI_CLASS_NOT_DEFINED:		return "Non-VGA device";
	      case PCI_CLASS_NOT_DEFINED_VGA:		return "VGA compatible device";

	      case PCI_CLASS_STORAGE_SCSI:		return "SCSI storage controller";
	      case PCI_CLASS_STORAGE_IDE:		return "IDE interface";
	      case PCI_CLASS_STORAGE_FLOPPY:		return "Floppy disk controller";
	      case PCI_CLASS_STORAGE_IPI:		return "IPI bus controller";
	      case PCI_CLASS_STORAGE_RAID:		return "RAID bus controller";
	      case PCI_CLASS_STORAGE_OTHER:		return "Unknown mass storage controller";

	      case PCI_CLASS_NETWORK_ETHERNET:		return "Ethernet controller";
	      case PCI_CLASS_NETWORK_TOKEN_RING:	return "Token ring network controller";
	      case PCI_CLASS_NETWORK_FDDI:		return "FDDI network controller";
	      case PCI_CLASS_NETWORK_ATM:		return "ATM network controller";
	      case PCI_CLASS_NETWORK_OTHER:		return "Network controller";

	      case PCI_CLASS_DISPLAY_VGA:		return "VGA compatible controller";
	      case PCI_CLASS_DISPLAY_XGA:		return "XGA compatible controller";
	      case PCI_CLASS_DISPLAY_OTHER:		return "Display controller";

	      case PCI_CLASS_MULTIMEDIA_VIDEO:		return "Multimedia video controller";
	      case PCI_CLASS_MULTIMEDIA_AUDIO:		return "Multimedia audio controller";
	      case PCI_CLASS_MULTIMEDIA_OTHER:		return "Multimedia controller";

	      case PCI_CLASS_MEMORY_RAM:		return "RAM memory";
	      case PCI_CLASS_MEMORY_FLASH:		return "FLASH memory";
	      case PCI_CLASS_MEMORY_OTHER:		return "Memory";

	      case PCI_CLASS_BRIDGE_HOST:		return "Host bridge";
	      case PCI_CLASS_BRIDGE_ISA:		return "ISA bridge";
	      case PCI_CLASS_BRIDGE_EISA:		return "EISA bridge";
	      case PCI_CLASS_BRIDGE_MC:			return "MicroChannel bridge";
	      case PCI_CLASS_BRIDGE_PCI:		return "PCI bridge";
	      case PCI_CLASS_BRIDGE_PCMCIA:		return "PCMCIA bridge";
	      case PCI_CLASS_BRIDGE_NUBUS:		return "NuBus bridge";
	      case PCI_CLASS_BRIDGE_CARDBUS:		return "CardBus bridge";
	      case PCI_CLASS_BRIDGE_OTHER:		return "Bridge";

	      case PCI_CLASS_COMMUNICATION_SERIAL:	return "Serial controller";
	      case PCI_CLASS_COMMUNICATION_PARALLEL:	return "Parallel controller";
	      case PCI_CLASS_COMMUNICATION_OTHER:	return "Communication controller";

	      case PCI_CLASS_SYSTEM_PIC:		return "PIC";
	      case PCI_CLASS_SYSTEM_DMA:		return "DMA controller";
	      case PCI_CLASS_SYSTEM_TIMER:		return "Timer";
	      case PCI_CLASS_SYSTEM_RTC:		return "RTC";
	      case PCI_CLASS_SYSTEM_OTHER:		return "System peripheral";

	      case PCI_CLASS_INPUT_KEYBOARD:		return "Keyboard controller";
	      case PCI_CLASS_INPUT_PEN:			return "Digitizer Pen";
	      case PCI_CLASS_INPUT_MOUSE:		return "Mouse controller";
	      case PCI_CLASS_INPUT_OTHER:		return "Input device controller";

	      case PCI_CLASS_DOCKING_GENERIC:		return "Generic Docking Station";
	      case PCI_CLASS_DOCKING_OTHER:		return "Docking Station";

	      case PCI_CLASS_PROCESSOR_386:		return "386";
	      case PCI_CLASS_PROCESSOR_486:		return "486";
	      case PCI_CLASS_PROCESSOR_PENTIUM:		return "Pentium";
	      case PCI_CLASS_PROCESSOR_ALPHA:		return "Alpha";
	      case PCI_CLASS_PROCESSOR_POWERPC:		return "Power PC";
	      case PCI_CLASS_PROCESSOR_CO:		return "Co-processor";

	      case PCI_CLASS_SERIAL_FIREWIRE:		return "FireWire (IEEE 1394)";
	      case PCI_CLASS_SERIAL_ACCESS:		return "ACCESS Bus";
	      case PCI_CLASS_SERIAL_SSA:		return "SSA";
	      case PCI_CLASS_SERIAL_USB:		return "USB Controller";
	      case PCI_CLASS_SERIAL_FIBER:		return "Fiber Channel";

	      case PCI_CLASS_HOT_SWAP_CONTROLLER:	return "Hot Swap Controller";

	      default:					return "Unknown class";
	}
}

/*
 * Convert some of the configuration space registers of the device at
 * address (bus,devfn) into a string (possibly several lines each).
 * The configuration string is stored starting at buf[len].  If the
 * string would exceed the size of the buffer (SIZE), 0 is returned.
 */
static int sprint_dev_config(struct pci_dev *dev, char *buf, int size)
{
	unsigned int class_rev;
	unsigned char latency, min_gnt, max_lat;
	int reg, len = 0;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	pci_read_config_byte (dev, PCI_LATENCY_TIMER, &latency);
	pci_read_config_byte (dev, PCI_MIN_GNT, &min_gnt);
	pci_read_config_byte (dev, PCI_MAX_LAT, &max_lat);
	if (len + 160 > size)
		return -1;
	len += sprintf(buf + len, "  Bus %2d, device %3d, function %2d:\n",
		       dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	len += sprintf(buf + len, "    %s: %s (rev %d).\n",
		       pci_strclass(class_rev >> 8),
		       dev->name,
		       class_rev & 0xff);

	if (dev->irq) {
		if (len + 40 > size)
			return -1;
		len += sprintf(buf + len, "      IRQ %d.\n", dev->irq);
	}

	if (latency || min_gnt || max_lat) {
		if (len + 80 > size)
			return -1;
		len += sprintf(buf + len, "      Master Capable.  ");
		if (latency)
		  len += sprintf(buf + len, "Latency=%d.  ", latency);
		else
		  len += sprintf(buf + len, "No bursts.  ");
		if (min_gnt)
		  len += sprintf(buf + len, "Min Gnt=%d.", min_gnt);
		if (max_lat)
		  len += sprintf(buf + len, "Max Lat=%d.", max_lat);
		len += sprintf(buf + len, "\n");
	}

	for (reg = 0; reg < 6; reg++) {
		struct resource *res = dev->resource + reg;
		unsigned long base, end, flags;

		if (len + 40 > size)
			return -1;
		base = res->start;
		end = res->end;
		flags = res->flags;
		if (!end)
			continue;

		if (flags & PCI_BASE_ADDRESS_SPACE_IO) {
			len += sprintf(buf + len,
				       "      I/O at 0x%lx [0x%lx].\n",
				       base, end);
		} else {
			const char *pref, *type = "unknown";

			if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH)
				pref = "P";
			else
				pref = "Non-p";
			switch (flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
			      case PCI_BASE_ADDRESS_MEM_TYPE_32:
				type = "32 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				type = "20 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_64:
				type = "64 bit"; break;
			}
			len += sprintf(buf + len,
				       "      %srefetchable %s memory at "
				       "0x%lx [0x%lx].\n", pref, type,
				       base,
				       end);
		}
	}

	return len;
}

/*
 * Return list of PCI devices as a character string for /proc/pci.
 * BUF is a buffer that is PAGE_SIZE bytes long.
 */
static int pci_read_proc(char *buf, char **start, off_t off,
				int count, int *eof, void *data)
{
	int nprinted, len, begin = 0;
	struct pci_dev *dev;

	len   = sprintf(buf, "PCI devices found:\n");

	for (dev = pci_devices; dev; dev = dev->next) {
		nprinted = sprint_dev_config(dev, buf + len, count - len);
		if (nprinted < 0)
			break;
		len += nprinted;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
		if (len+begin >= off+count)
			break;
	}
	if (!dev || len+begin < off)
		*eof = 1;
	off -= begin;
	*start = buf + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
		len = 0;
	return len;
}

static int __init pci_proc_init(void)
{
	if (pci_present()) {
		proc_bus_pci_dir = create_proc_entry("pci", S_IFDIR, proc_bus);
		create_proc_info_entry("devices",0, proc_bus_pci_dir,
					get_pci_dev_info);
		proc_bus_pci_add(pci_root);
		create_proc_read_entry("pci", 0, NULL, pci_read_proc, NULL);
	}
	return 0;
}

__initcall(pci_proc_init);

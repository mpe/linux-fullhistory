/*
 *	$Id: oldproc.c,v 1.24 1998/10/11 15:13:04 mj Exp $
 *
 *	Backward-compatible procfs interface for PCI.
 *
 *	Copyright 1993, 1994, 1995, 1997 Drew Eckhardt, Frederic Potter,
 *	David Mosberger-Tang, Martin Mares
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <asm/page.h>

struct pci_device_info {
	unsigned short device;
	unsigned short seen;
	const char *name;
};

struct pci_vendor_info {
	unsigned short vendor;
	unsigned short nr;
	const char *name;
	struct pci_device_info *devices;
};

/*
 * This is ridiculous, but we want the strings in
 * the .init section so that they don't take up
 * real memory.. Parse the same file multiple times
 * to get all the info.
 */
#define VENDOR( vendor, name )		static const char __vendorstr_##vendor[] __initdata = name;
#define ENDVENDOR()
#define DEVICE( vendor, device, name ) 	static const char __devicestr_##vendor##device[] __initdata = name;
#include "devlist.h"


#define VENDOR( vendor, name )		static struct pci_device_info __devices_##vendor[] __initdata = {
#define ENDVENDOR()			};
#define DEVICE( vendor, device, name )	{ PCI_DEVICE_ID_##device, 0, __devicestr_##vendor##device },
#include "devlist.h"

static const struct pci_vendor_info __initdata pci_vendor_list[] = {
#define VENDOR( vendor, name )		{ PCI_VENDOR_ID_##vendor, sizeof(__devices_##vendor) / sizeof(struct pci_device_info), name, __devices_##vendor },
#define ENDVENDOR()
#define DEVICE( vendor, device, name )
#include "devlist.h"
};

#define VENDORS (sizeof(pci_vendor_list)/sizeof(struct pci_vendor_info))

void __init pci_namedevice(struct pci_dev *dev)
{
	const struct pci_vendor_info *vendor_p = pci_vendor_list;
	int i = VENDORS;
	
	do {
		if (vendor_p->vendor == dev->vendor)
			goto match_vendor;
		vendor_p++;
	} while (--i);

	/* Coulding find either the vendor nor the device */
	sprintf(dev->name, "PCI<%d:%04x> %04x:%04x", dev->bus->number, dev->devfn, dev->vendor, dev->device);
	return;

	match_vendor: {
		struct pci_device_info *device_p = vendor_p->devices;
		int i = vendor_p->nr;

		while (i > 0) {
			if (device_p->device == dev->device)
				goto match_device;
			device_p++;
			i--;
		}

		/* Ok, found the vendor, but unknown device */
		sprintf(dev->name, "PCI<%d:%04x> %04x:%04x (%s)", dev->bus->number, dev->devfn, dev->vendor, dev->device, vendor_p->name);
		return;

		/* Full match */
		match_device: {
			char *n = dev->name + sprintf(dev->name, "%s %s", vendor_p->name, device_p->name);
			int nr = device_p->seen + 1;
			device_p->seen = nr;
			if (nr > 1)
				sprintf(n, " (#%d)", nr);
		}
	}
}

#ifdef CONFIG_PROC_FS

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
	unsigned int class_rev, bus, devfn;
	unsigned short vendor, device, status;
	unsigned char bist, latency, min_gnt, max_lat;
	int reg, len = 0;
	const char *str;

	bus   = dev->bus->number;
	devfn = dev->devfn;

	pcibios_read_config_dword(bus, devfn, PCI_CLASS_REVISION, &class_rev);
	pcibios_read_config_word (bus, devfn, PCI_VENDOR_ID, &vendor);
	pcibios_read_config_word (bus, devfn, PCI_DEVICE_ID, &device);
	pcibios_read_config_word (bus, devfn, PCI_STATUS, &status);
	pcibios_read_config_byte (bus, devfn, PCI_BIST, &bist);
	pcibios_read_config_byte (bus, devfn, PCI_LATENCY_TIMER, &latency);
	pcibios_read_config_byte (bus, devfn, PCI_MIN_GNT, &min_gnt);
	pcibios_read_config_byte (bus, devfn, PCI_MAX_LAT, &max_lat);
	if (len + 80 > size) {
		return -1;
	}
	len += sprintf(buf + len, "  Bus %2d, device %3d, function %2d:\n",
		       bus, PCI_SLOT(devfn), PCI_FUNC(devfn));

	if (len + 80 > size) {
		return -1;
	}
	len += sprintf(buf + len, "    %s: %s (rev %d).\n      ",
		       pci_strclass(class_rev >> 8),
		       dev->name,
		       class_rev & 0xff);

	switch (status & PCI_STATUS_DEVSEL_MASK) {
	      case PCI_STATUS_DEVSEL_FAST:   str = "Fast devsel.  "; break;
	      case PCI_STATUS_DEVSEL_MEDIUM: str = "Medium devsel.  "; break;
	      case PCI_STATUS_DEVSEL_SLOW:   str = "Slow devsel.  "; break;
	      default:			     str = "Unknown devsel.  ";
	}
	if (len + strlen(str) > size) {
		return -1;
	}
	len += sprintf(buf + len, str);

	if (status & PCI_STATUS_FAST_BACK) {
#		define fast_b2b_capable	"Fast back-to-back capable.  "
		if (len + strlen(fast_b2b_capable) > size) {
			return -1;
		}
		len += sprintf(buf + len, fast_b2b_capable);
#		undef fast_b2b_capable
	}

	if (bist & PCI_BIST_CAPABLE) {
#		define BIST_capable	"BIST capable.  "
		if (len + strlen(BIST_capable) > size) {
			return -1;
		}
		len += sprintf(buf + len, BIST_capable);
#		undef BIST_capable
	}

	if (dev->irq) {
		if (len + 40 > size) {
			return -1;
		}
		len += sprintf(buf + len, "IRQ %d.  ", dev->irq);
	}

	if (dev->master) {
		if (len + 80 > size) {
			return -1;
		}
		len += sprintf(buf + len, "Master Capable.  ");
		if (latency)
		  len += sprintf(buf + len, "Latency=%d.  ", latency);
		else
		  len += sprintf(buf + len, "No bursts.  ");
		if (min_gnt)
		  len += sprintf(buf + len, "Min Gnt=%d.", min_gnt);
		if (max_lat)
		  len += sprintf(buf + len, "Max Lat=%d.", max_lat);
	}

	for (reg = 0; reg < 6; reg++) {
		struct resource *res = dev->resource + reg;
		unsigned long base, end, flags;

		if (len + 40 > size) {
			return -1;
		}
		base = res->start;
		end = res->end;
		flags = res->flags;
		if (!flags)
			continue;

		if (flags & PCI_BASE_ADDRESS_SPACE_IO) {
			len += sprintf(buf + len,
				       "\n      I/O at 0x%lx [0x%lx].",
				       base, end);
		} else {
			const char *pref, *type = "unknown";

			if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				pref = "P";
			} else {
				pref = "Non-p";
			}
			switch (flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
			      case PCI_BASE_ADDRESS_MEM_TYPE_32:
				type = "32 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				type = "20 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_64:
				type = "64 bit"; break;
			}
			len += sprintf(buf + len,
				       "\n      %srefetchable %s memory at "
				       "0x%lx [0x%lx].", pref, type,
				       base,
				       end);
		}
	}

	len += sprintf(buf + len, "\n");
	return len;
}


/*
 * Return list of PCI devices as a character string for /proc/pci.
 * BUF is a buffer that is PAGE_SIZE bytes long.
 */
int get_pci_list(char *buf)
{
	int nprinted, len, size;
	struct pci_dev *dev;
	static int complained = 0;
#	define MSG "\nwarning: page-size limit reached!\n"

	if (!complained) {
		complained++;
		printk(KERN_INFO "%s uses obsolete /proc/pci interface\n",
			current->comm);
	}

	/* reserve same for truncation warning message: */
	size  = PAGE_SIZE - (strlen(MSG) + 1);
	len   = sprintf(buf, "PCI devices found:\n");

	for (dev = pci_devices; dev; dev = dev->next) {
		nprinted = sprint_dev_config(dev, buf + len, size - len);
		if (nprinted < 0) {
			return len + sprintf(buf + len, MSG);
		}
		len += nprinted;
	}
	return len;
}

static struct proc_dir_entry proc_old_pci = {
	PROC_PCI, 3, "pci",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};

void __init proc_old_pci_init(void)
{
	proc_register(&proc_root, &proc_old_pci);
}

#endif /* CONFIG_PROC_FS */

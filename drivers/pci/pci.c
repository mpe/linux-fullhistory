/*
 * drivers/pci/pci.c
 *
 * PCI services that are built on top of the BIOS32 service.
 *
 * Copyright 1993, 1994, 1995 Drew Eckhardt, Frederic Potter,
 *	David Mosberger-Tang
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/string.h>

#include <asm/page.h>

struct pci_bus pci_root;
struct pci_dev *pci_devices = 0;


/* 
 * The bridge_id field is an offset of an item into the array
 * BRIDGE_MAPPING_TYPE. 0xff indicates that the device is not a PCI
 * bridge, or that we don't know for the moment how to configure it.
 * I'm trying to do my best so that the kernel stays small.  Different
 * chipset can have same optimization structure. i486 and pentium
 * chipsets from the same manufacturer usually have the same
 * structure.
 */
#define DEVICE(vid,did,name) \
  {PCI_VENDOR_ID_##vid, PCI_DEVICE_ID_##did, (name), 0xff}

#define BRIDGE(vid,did,name,bridge) \
  {PCI_VENDOR_ID_##vid, PCI_DEVICE_ID_##did, (name), (bridge)}

struct pci_dev_info dev_info[] = {
	DEVICE( NCR,		NCR_53C810,	"53c810"),
	DEVICE( NCR,		NCR_53C815,	"53c815"),
	DEVICE( NCR,		NCR_53C820,	"53c820"),
	DEVICE( NCR,		NCR_53C825,	"53c825"),
	DEVICE( ADAPTEC,	ADAPTEC_2940,	"2940"),
	DEVICE( ADAPTEC,	ADAPTEC_294x,	"294x"),
	DEVICE( ADAPTEC,	ADAPTEC_7850,	"AIC-7850"),
	DEVICE( DPT,		DPT,		"SmartCache/Raid"),
	DEVICE( S3,		S3_864_1,	"Vision 864-P"),
	DEVICE( S3,		S3_864_2,	"Vision 864-P"),
	DEVICE( S3,		S3_868,	"Vision 868"),
	DEVICE( S3,		S3_928,		"Vision 928-P"),
	DEVICE( S3,		S3_964_1,	"Vision 964-P"),
	DEVICE( S3,		S3_964_2,	"Vision 964-P"),
	DEVICE( S3,		S3_811,		"Trio32/Trio64"),
	DEVICE( S3,		S3_968,		"Vision 968"),
	DEVICE( OPTI,		OPTI_82C822,	"82C822"),
	DEVICE( OPTI,		OPTI_82C621,	"82C621"),
	DEVICE( OPTI,		OPTI_82C557,	"82C557"),
	DEVICE( OPTI,		OPTI_82C558,	"82C558"),
	BRIDGE( UMC,		UMC_UM8881F,	"UM8881F",		0x02),
	BRIDGE( UMC,		UMC_UM8891A,	"UM8891A", 		0x01),
	DEVICE( UMC,		UMC_UM8886F,	"UM8886F"),
	DEVICE( UMC,		UMC_UM8886A,	"UM8886A"),
	DEVICE( UMC,		UMC_UM8673F,	"UM8673F"),
	DEVICE( DEC,		DEC_TULIP,	"DC21040"),
	DEVICE( DEC,		DEC_TULIP_FAST,	"DC21140"),
	DEVICE( DEC,		DEC_TULIP_PLUS,	"DC21041"),
	DEVICE( DEC,		DEC_FDDI,	"DEFPA"),
	DEVICE( DEC,		DEC_BRD,	"DC21050"),
	DEVICE( MATROX,		MATROX_MGA_2,	"Atlas PX2085"),
	DEVICE( MATROX,		MATROX_MGA_IMP,	"MGA Impression"),
	DEVICE( INTEL,		INTEL_82378,	"82378IB"),
	BRIDGE( INTEL,		INTEL_82424,	"82424ZX Saturn",	0x00),
	DEVICE( INTEL,		INTEL_82375,	"82375EB"),
	BRIDGE( INTEL,		INTEL_82434,	"82434LX Mercury/Neptune", 0x00),
	DEVICE( INTEL,		INTEL_82430,	"82430ZX Aries"),
	DEVICE( INTEL,		INTEL_82437,	"82437 Triton"),
	DEVICE( INTEL,		INTEL_82371,	"82471 Triton"),
	DEVICE( INTEL,		INTEL_82438,	"82438"),
	DEVICE( INTEL,		INTEL_7116,	"SAA7116"),
	DEVICE( INTEL,		INTEL_82865,	"82865"),
	DEVICE( SMC,		SMC_37C665,	"FDC 37C665"),
	DEVICE( SMC,		SMC_37C922,	"FDC 37C922"),
	DEVICE( ATI,		ATI_M32,	"Mach 32"),
	DEVICE( ATI,		ATI_M64,	"Mach 64"),
	DEVICE( WEITEK,		WEITEK_P9000,	"P9000"),
	DEVICE( WEITEK,		WEITEK_P9100,	"P9100"),
	DEVICE( CIRRUS,		CIRRUS_5430,	"GD 5430"),
	DEVICE( CIRRUS,		CIRRUS_5434_4,	"GD 5434"),
	DEVICE( CIRRUS,		CIRRUS_5434_8,	"GD 5434"),
	DEVICE( CIRRUS,		CIRRUS_6729,	"CL 6729"),
	DEVICE( CIRRUS,		CIRRUS_7542,	"CL 7542"),
	DEVICE( BUSLOGIC,	BUSLOGIC_946C,	"946C"),
	DEVICE( BUSLOGIC,	BUSLOGIC_946C_2,"946C"),
	DEVICE( N9,		N9_I128,	"Imagine 128"),
	DEVICE( AI,		AI_M1435,	"M1435"),
	DEVICE( AL,		AL_M1445,	"M1445"),
	DEVICE( AL,		AL_M1449,	"M1449"),
	DEVICE( AL,		AL_M1451,	"M1451"),
	DEVICE( AL,		AL_M1461,	"M1461"),
	DEVICE( AL,		AL_M4803,	"M4803"),
	DEVICE( TSENG,		TSENG_W32P_2,	"ET4000W32P"),
	DEVICE( TSENG,		TSENG_W32P_b,	"ET4000W32P rev B"),
	DEVICE( TSENG,		TSENG_W32P_c,	"ET4000W32P rev C"),
	DEVICE( TSENG,		TSENG_W32P_d,	"ET4000W32P rev D"),
	DEVICE( CMD,		CMD_640,	"640A"),
	DEVICE( VISION,		VISION_QD8500,	"QD-8500"),
	DEVICE( VISION,		VISION_QD8580,	"QD-8580"),
	DEVICE( AMD,		AMD_LANCE,	"79C970"),
	DEVICE( AMD,		AMD_SCSI,	"53C974"),
	DEVICE( VLSI,		VLSI_82C593,	"82C593-FC1"),
	DEVICE( VLSI,		VLSI_82C592,	"82C592-FC1"),
	DEVICE( ADL,		ADL_2301,	"2301"),
	DEVICE( SYMPHONY,	SYMPHONY_101,	"82C101"),
	DEVICE( TRIDENT,	TRIDENT_9420,	"TG 9420"),
	DEVICE( TRIDENT,	TRIDENT_9440,	"TG 9440"),
	DEVICE( CONTAQ,		CONTAQ_82C599,	"82C599"),
	DEVICE( NS,		NS_87410,	"87410"),
	DEVICE( VIA,		VIA_82C505,	"VT 82C505"),
	DEVICE( VIA,		VIA_82C576,	"VT 82C576 3V"),
	DEVICE( VIA,		VIA_82C561,	"VT 82C561"),
	DEVICE( SI,		SI_496,		"85C496"),
	DEVICE( SI,		SI_501,		"85C501"),
	DEVICE( SI,		SI_503,		"85C503"),
	DEVICE( SI,		SI_601,		"85C601"),
	DEVICE( LEADTEK,	LEADTEK_805,	"S3 805"),
	DEVICE( IMS,		IMS_8849,	"8849"),
	DEVICE( ZEINET,		ZEINET_1221,	"1221"),
	DEVICE( EF,		EF_ATM,		"155P-MF1"),
	DEVICE( HER,		HER_STING,	"Stingray"),
  	DEVICE( ATRONICS,	ATRONICS_2015,	"IDE-2015PL"),
	DEVICE( CT,		CT_65545,	"65545"),
	DEVICE( FD,		FD_36C70,	"TMC-18C30"),
	DEVICE( WINBOND,	WINBOND_83769,	"W83769F"),
	DEVICE( 3COM,		3COM_3C590,	"3C590 10bT"),
	DEVICE( 3COM,		3COM_3C595TX,	"3C595 100bTX"),
	DEVICE( 3COM,		3COM_3C595T4,	"3C595 100bT4"),
	DEVICE( 3COM,		3COM_3C595MII,	"3C595 100b-MII"),
	DEVICE( PROMISE,	PROMISE_5300,	"DC5030"),
	DEVICE( QLOGIC,		QLOGIC_ISP1020,	"ISP1020"),
	DEVICE( QLOGIC,		QLOGIC_ISP1022,	"ISP1022"),
	DEVICE( X,		X_AGX016,	"ITT AGX016"),
	DEVICE( VORTEX,		VORTEX_GDT,	"GDT 6000b"),
	DEVICE( HP,		HP_J2585A,	"J2585A"),
	DEVICE( MUTECH,		MUTECH_MV1000,	"MV-1000"),
	DEVICE( TEKRAM,		TEKRAM_DC290,	"DC-290")
};


#ifdef CONFIG_PCI_OPTIMIZE

/*
 * An item of this structure has the following meaning:
 * for each optimization, the register address, the mask
 * and value to write to turn it on.
 * There are 5 optimizations for the moment:
 * Cache L2 write back best than write through
 * Posted Write for CPU to PCI enable
 * Posted Write for CPU to MEMORY enable
 * Posted Write for PCI to MEMORY enable
 * PCI Burst enable
 *
 * Half of the bios I've meet don't allow you to turn that on, and you
 * can gain more than 15% on graphic accesses using those
 * optimizations...
 */
struct optimization_type {
	const char	*type;
	const char	*off;
	const char	*on;
} bridge_optimization[] = {
	{"Cache L2",			"write trough",	"write back"},
	{"CPU-PCI posted write",	"off",		"on"},
	{"CPU-Memory posted write",	"off",		"on"},
	{"PCI-Memory posted write",	"off",		"on"},
	{"PCI burst",			"off",		"on"}
};

#define NUM_OPTIMIZATIONS \
	(sizeof(bridge_optimization) / sizeof(bridge_optimization[0]))

struct bridge_mapping_type {
	unsigned char	addr;	/* config space address */
	unsigned char	mask;
	unsigned char	value;
} bridge_mapping[] = {
	/*
	 * Intel Neptune/Mercury/Saturn:
	 *	If the internal cache is write back,
	 *	the L2 cache must be write through!
	 *	I've to check out how to control that
	 *	for the moment, we won't touch the cache
	 */
	{0x0	,0x02	,0x02	},
	{0x53	,0x02	,0x02	},
	{0x53	,0x01	,0x01	},
	{0x54	,0x01	,0x01	},
	{0x54	,0x02	,0x02	},

	/*
	 * UMC 8891A Pentium chipset:
	 *	Why did you think UMC was cheaper ??
	 */
	{0x50	,0x10	,0x00	},
	{0x51	,0x40	,0x40	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},

	/*
	 * UMC UM8881F
	 *	This is a dummy entry for my tests.
	 *	I have this chipset and no docs....
	 */
	{0x0	,0x1	,0x1	},
	{0x0	,0x2	,0x0	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	}
};

#endif /* CONFIG_PCI_OPTIMIZE */


/*
 * If performance ever becomes important, device_info[] could be
 * sorted by key and this could be replaced by a binary search.
 */
struct pci_dev_info *pci_lookup_dev(unsigned int vendor, unsigned int dev)
{
	int i;

	for (i = 0; i < sizeof(dev_info)/sizeof(dev_info[0]); ++i) {
		if (dev_info[i].vendor == vendor &&
		    dev_info[i].device == dev)
		{
			return &dev_info[i];
		}
	}
	return 0;
}


const char *pci_strbioserr (int error)
{
	switch (error) {
	      case PCIBIOS_SUCCESSFUL:		return "SUCCESSFUL";
	      case PCIBIOS_FUNC_NOT_SUPPORTED:	return "FUNC_NOT_SUPPORTED";
	      case PCIBIOS_BAD_VENDOR_ID:	return "SUCCESSFUL";
	      case PCIBIOS_DEVICE_NOT_FOUND:	return "DEVICE_NOT_FOUND";
	      case PCIBIOS_BAD_REGISTER_NUMBER:	return "BAD_REGISTER_NUMBER";
	      case PCIBIOS_SET_FAILED:		return "SET_FAILED";
	      case PCIBIOS_BUFFER_TOO_SMALL:	return "BUFFER_TOO_SMALL";
	      default:				return "Unknown error status";
	}
}


const char *pci_strclass (unsigned int class)
{
	switch (class >> 8) {
	      case PCI_CLASS_NOT_DEFINED:		return "Non-VGA device";
	      case PCI_CLASS_NOT_DEFINED_VGA:		return "VGA compatible device";

	      case PCI_CLASS_STORAGE_SCSI:		return "SCSI storage controller";
	      case PCI_CLASS_STORAGE_IDE:		return "IDE controller";
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
	      case PCI_CLASS_SERIAL_FIBER:		return "Fiber Channel";

	      default:					return "Unknown class";
	}
}


const char *pci_strvendor(unsigned int vendor)
{
	switch (vendor) {
	      case PCI_VENDOR_ID_NCR:		return "NCR";
	      case PCI_VENDOR_ID_ADAPTEC:	return "Adaptec";
	      case PCI_VENDOR_ID_DPT:		return "DPT";
	      case PCI_VENDOR_ID_S3:		return "S3 Inc.";
	      case PCI_VENDOR_ID_OPTI:		return "OPTI";
	      case PCI_VENDOR_ID_UMC:		return "UMC";
	      case PCI_VENDOR_ID_DEC:		return "DEC";
	      case PCI_VENDOR_ID_MATROX:	return "Matrox";
	      case PCI_VENDOR_ID_INTEL:		return "Intel";
	      case PCI_VENDOR_ID_SMC:		return "SMC";
	      case PCI_VENDOR_ID_ATI:		return "ATI";
	      case PCI_VENDOR_ID_WEITEK:	return "Weitek";
	      case PCI_VENDOR_ID_CIRRUS:	return "Cirrus Logic";
	      case PCI_VENDOR_ID_BUSLOGIC:	return "Bus Logic";
	      case PCI_VENDOR_ID_N9:		return "Number Nine";
	      case PCI_VENDOR_ID_AI:		return "Acer Incorporated";
	      case PCI_VENDOR_ID_AL:		return "Acer Labs";
	      case PCI_VENDOR_ID_TSENG:		return "Tseng'Lab";
	      case PCI_VENDOR_ID_CMD:		return "CMD";
	      case PCI_VENDOR_ID_VISION:	return "Vision";
	      case PCI_VENDOR_ID_AMD:		return "AMD";
	      case PCI_VENDOR_ID_VLSI:		return "VLSI";
	      case PCI_VENDOR_ID_ADL:		return "Advance Logic";
	      case PCI_VENDOR_ID_SYMPHONY:	return "Symphony";
	      case PCI_VENDOR_ID_TRIDENT:	return "Trident";
	      case PCI_VENDOR_ID_CONTAQ:	return "Contaq";
	      case PCI_VENDOR_ID_NS:		return "NS";
	      case PCI_VENDOR_ID_VIA:		return "VIA Technologies";
	      case PCI_VENDOR_ID_SI:		return "Silicon Integrated Systems";
	      case PCI_VENDOR_ID_LEADTEK:	return "Leadtek Research";
	      case PCI_VENDOR_ID_IMS:		return "IMS";
	      case PCI_VENDOR_ID_ZEINET:	return "ZeiNet";
	      case PCI_VENDOR_ID_EF:		return "Efficient Networks";
	      case PCI_VENDOR_ID_HER:		return "Hercules";
	      case PCI_VENDOR_ID_ATRONICS:	return "Atronics";
	      case PCI_VENDOR_ID_CT:		return "Chips & Technologies";
	      case PCI_VENDOR_ID_FD:		return "Future Domain";
	      case PCI_VENDOR_ID_WINBOND:	return "Winbond";
	      case PCI_VENDOR_ID_3COM:		return "3Com";
	      case PCI_VENDOR_ID_PROMISE:	return "Promise Technology";
	      case PCI_VENDOR_ID_QLOGIC:	return "Q Logic";
	      case PCI_VENDOR_ID_X:		return "X TECHNOLOGY";
	      case PCI_VENDOR_ID_ACC:		return "ACC MICROELECTRONICS";
	      case PCI_VENDOR_ID_VORTEX:	return "VORTEX";
	      case PCI_VENDOR_ID_HP:		return "Hewlett Packard";
	      default:				return "Unknown vendor";
	}
}


const char *pci_strdev(unsigned int vendor, unsigned int device) 
{
	struct pci_dev_info *info;

	info = 	pci_lookup_dev(vendor, device);
	return info ? info->name : "Unknown device";
}



/*
 * Turn on/off PCI bridge optimization. This should allow benchmarking.
 */
static void burst_bridge(unsigned char bus, unsigned char devfn,
			 unsigned char pos, int turn_on)
{
#ifdef CONFIG_PCI_OPTIMIZE
	struct bridge_mapping_type *bmap;
	unsigned char val;
	int i;

	pos *= NUM_OPTIMIZATIONS;
	printk("PCI bridge optimization.\n");
	for (i = 0; i < NUM_OPTIMIZATIONS; i++) {
		printk("    %s: ", bridge_optimization[i].type);
		bmap = &bridge_mapping[pos + i];
		if (!bmap->addr) {
			printk("Not supported.");
		} else {
			pcibios_read_config_byte(bus, devfn, bmap->addr, &val);
			if ((val & bmap->mask) == bmap->value) {
				printk("%s.", bridge_optimization[i].on);
				if (!turn_on) {
					pcibios_write_config_byte(bus, devfn,
								  bmap->addr,
								  (val | bmap->mask)
								  - bmap->value);
					printk("Changed!  Now %s.", bridge_optimization[i].off);
				}
			} else {
				printk("%s.", bridge_optimization[i].off);
				if (turn_on) {
					pcibios_write_config_byte(bus, devfn,
								  bmap->addr,
								  (val & (0xff - bmap->mask))
								  + bmap->value);
					printk("Changed!  Now %s.", bridge_optimization[i].on);
				}
			}
		}
		printk("\n");
	}
#endif /* CONFIG_PCI_OPTIMIZE */
}


/*
 * Convert some of the configuration space registers of the device at
 * address (bus,devfn) into a string (possibly several lines each).
 * The configuration string is stored starting at buf[len].  If the
 * string would exceed the size of the buffer (SIZE), 0 is returned.
 */
static int sprint_dev_config(struct pci_dev *dev, char *buf, int size)
{
	unsigned long base;
	unsigned int l, class_rev, bus, devfn;
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
	len += sprintf(buf + len, "    %s: %s %s (rev %d).\n      ",
		       pci_strclass(class_rev >> 8), pci_strvendor(vendor),
		       pci_strdev(vendor, device), class_rev & 0xff);

	if (!pci_lookup_dev(vendor, device)) {
		len += sprintf(buf + len,
			       "Vendor id=%x. Device id=%x.\n      ",
			       vendor, device);
	}

	str = 0;	/* to keep gcc shut... */
	switch (status & PCI_STATUS_DEVSEL_MASK) {
	      case PCI_STATUS_DEVSEL_FAST:   str = "Fast devsel.  "; break;
	      case PCI_STATUS_DEVSEL_MEDIUM: str = "Medium devsel.  "; break;
	      case PCI_STATUS_DEVSEL_SLOW:   str = "Slow devsel.  "; break;
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

	for (reg = PCI_BASE_ADDRESS_0; reg <= PCI_BASE_ADDRESS_5; reg += 4) {
		if (len + 40 > size) {
			return -1;
		}
		pcibios_read_config_dword(bus, devfn, reg, &l);
		base = l;
		if (!base) {
			continue;
		}

		if (base & PCI_BASE_ADDRESS_SPACE_IO) {
			len += sprintf(buf + len,
				       "\n      I/O at 0x%lx.",
				       base & PCI_BASE_ADDRESS_IO_MASK);
		} else {
			const char *pref, *type = "unknown";

			if (base & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				pref = "P";
			} else {
				pref = "Non-p";
			}
			switch (base & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
			      case PCI_BASE_ADDRESS_MEM_TYPE_32:
				type = "32 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				type = "20 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_64:
				type = "64 bit";
				/* read top 32 bit address of base addr: */
				reg += 4;
				pcibios_read_config_dword(bus, devfn, reg, &l);
				base |= ((u64) l) << 32;
				break;
			}
			len += sprintf(buf + len,
				       "\n      %srefetchable %s memory at "
				       "0x%lx.", pref, type,
				       base & PCI_BASE_ADDRESS_MEM_MASK);
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
#	define MSG "\nwarning: page-size limit reached!\n"

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


/*
 * pci_malloc() returns initialized memory of size SIZE.  Can be
 * used only while pci_init() is active.
 */
static void *pci_malloc(long size, unsigned long *mem_startp)
{
	void *mem;

#ifdef DEBUG
	printk("...pci_malloc(size=%ld,mem=%p)", size, *mem_startp);
#endif
	mem = (void*) *mem_startp;
	*mem_startp += (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
	memset(mem, 0, size);
	return mem;
}


static unsigned int scan_bus(struct pci_bus *bus, unsigned long *mem_startp)
{
	unsigned int devfn, l, max;
	unsigned char cmd, tmp, hdr_type = 0;
	struct pci_dev_info *info;
	struct pci_dev *dev;
	struct pci_bus *child;

#ifdef DEBUG
	printk("...scan_bus(busno=%d,mem=%p)\n", bus->number, *mem_startp);
#endif

	max = bus->secondary;
	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) == 0) {
			pcibios_read_config_byte(bus->number, devfn,
						 PCI_HEADER_TYPE, &hdr_type);
		} else if (!(hdr_type & 0x80)) {
			/* not a multi-function device */
			continue;
		}

		pcibios_read_config_dword(bus->number, devfn, PCI_VENDOR_ID,
					  &l);
		/* some broken boards return 0 if a slot is empty: */
		if (l == 0xffffffff || l == 0x00000000) {
			hdr_type = 0;
			continue;
		}

		dev = pci_malloc(sizeof(*dev), mem_startp);
		dev->bus = bus;
		/*
		 * Put it into the simple chain of devices on this
		 * bus.  It is used to find devices once everything is
		 * set up.
		 */
		dev->next = pci_devices;
		pci_devices = dev;

		dev->devfn  = devfn;
		dev->vendor = l & 0xffff;
		dev->device = (l >> 16) & 0xffff;

		/*
		 * Check to see if we now about this device and report
		 * a message at boot time.  This is the only way to
		 * learn about new hardware...
		 */
		info = pci_lookup_dev(dev->vendor, dev->device);
		if (!info) {
			printk("Warning : Unknown PCI device.  Please read include/linux/pci.h \n");
		} else {
			/* Some BIOS' are lazy. Let's do their job: */
			if (info->bridge_type != 0xff) {
				burst_bridge(bus->number, devfn,
					     info->bridge_type, 1);
			}
		}

		/* non-destructively determine if device can be a master: */
		pcibios_read_config_byte(bus->number, devfn, PCI_COMMAND,
					 &cmd);
		pcibios_write_config_byte(bus->number, devfn, PCI_COMMAND,
					  cmd | PCI_COMMAND_MASTER);
		pcibios_read_config_byte(bus->number, devfn, PCI_COMMAND,
					 &tmp);
		dev->master = ((tmp & PCI_COMMAND_MASTER) != 0);
		pcibios_write_config_byte(bus->number, devfn, PCI_COMMAND,
					  cmd);

		/* read irq level (may be changed during pcibios_fixup()): */
		pcibios_read_config_byte(bus->number, devfn,
					 PCI_INTERRUPT_LINE, &dev->irq);

		/* check to see if this device is a PCI-PCI bridge: */
		pcibios_read_config_dword(bus->number, devfn,
					  PCI_CLASS_REVISION, &l);
		l = l >> 8;			/* upper 3 bytes */
		dev->class = l;
		/*
		 * Now insert it into the list of devices held
		 * by the parent bus.
		 */
		dev->sibling = bus->devices;
		bus->devices = dev;

		if (dev->class >> 8 == PCI_CLASS_BRIDGE_PCI) {
			unsigned int buses;
			unsigned short cr;

			/*
			 * Insert it into the tree of buses.
			 */
			child = pci_malloc(sizeof(*child), mem_startp);
			child->next   = bus->children;
			bus->children = child;
			child->self = dev;
			child->parent = bus;

			/*
			 * Set up the primary, secondary and subordinate
			 * bus numbers.
			 */
			child->number = child->secondary = ++max;
			child->primary = bus->secondary;
			child->subordinate = 0xff;
			/*
			 * Clear all status bits and turn off memory,
			 * I/O and master enables.
			 */
			pcibios_read_config_word(bus->number, devfn,
						  PCI_COMMAND, &cr);
			pcibios_write_config_word(bus->number, devfn,
						  PCI_COMMAND, 0x0000);
			pcibios_write_config_word(bus->number, devfn,
						  PCI_STATUS, 0xffff);
			/*
			 * Configure the bus numbers for this bridge:
			 */
			pcibios_read_config_dword(bus->number, devfn, 0x18,
						  &buses);
			buses &= 0xff000000;
			buses |= (((unsigned int)(child->primary)     <<  0) |
				  ((unsigned int)(child->secondary)   <<  8) |
				  ((unsigned int)(child->subordinate) << 16));
			pcibios_write_config_dword(bus->number, devfn, 0x18,
						   buses);
			/*
			 * Now we can scan all subordinate buses:
			 */
			max = scan_bus(child, mem_startp);
			/*
			 * Set the subordinate bus number to its real
			 * value:
			 */
			child->subordinate = max;
			buses = (buses & 0xff00ffff)
			  | ((unsigned int)(child->subordinate) << 16);
			pcibios_write_config_dword(bus->number, devfn, 0x18,
						   buses);
			pcibios_write_config_word(bus->number, devfn,
						  PCI_COMMAND, cr);
		}
	}
	/*
	 * We've scanned the bus and so we know all about what's on
	 * the other side of any bridges that may be on this bus plus
	 * any devices.
	 *
	 * Return how far we've got finding sub-buses.
	 */
	return max;
}


unsigned long pci_init (unsigned long mem_start, unsigned long mem_end)
{
	mem_start = pcibios_init(mem_start, mem_end);

	if (!pcibios_present()) {
		printk("pci_init: no BIOS32 detected\n");
		return mem_start;
	}

	printk("Probing PCI hardware.\n");

	memset(&pci_root, 0, sizeof(pci_root));
	pci_root.subordinate = scan_bus(&pci_root, &mem_start);

	/* give BIOS a chance to apply platform specific fixes: */
	mem_start = pcibios_fixup(mem_start, mem_end);

#ifdef DEBUG
	{
		int len = get_pci_list(mem_start);
		if (len) {
			((char*)mem_start)[len] = '\0';
			printk("%s\n", mem_start);
		}
	}
#endif
	return mem_start;
}

/*
 * bios32.c - BIOS32, PCI BIOS functions.
 *
 * Sponsored by
 *	iX Multiuser Multitasking Magazine
 *	Hannover, Germany
 *	hm@ix.de
 *
 * Copyright 1993, 1994 Drew Eckhardt
 *      Visionary Computing
 *      (Unix and Linux consulting and custom programming)
 *      Drew@Colorado.EDU
 *      +1 (303) 786-7975
 *
 * For more information, please consult
 *
 * PCI BIOS Specification Revision
 * PCI Local Bus Specification
 * PCI System Design Guide
 *
 * PCI Special Interest Group
 * M/S HF3-15A
 * 5200 N.E. Elam Young Parkway
 * Hillsboro, Oregon 97124-6497
 * +1 (503) 696-2000
 * +1 (800) 433-5177
 *
 * Manuals are $25 each or $50 for all three, plus $7 shipping
 * within the United States, $35 abroad.
 *
 *
 * CHANGELOG :
 * Jun 17, 1994 : Modified to accommodate the broken pre-PCI BIOS SPECIFICATION
 *	Revision 2.0 present on <thys@dennis.ee.up.ac.za>'s ASUS mainboard.
 *
 * Jan 5,  1995 : Modified to probe PCI hardware at boot time by Frederic
 *     Potter, potter@cao-vlsi.ibp.fr
 *
 * Jan 10, 1995 : Modified to store the information about configured pci
 *      devices into a list, which can be accessed via /proc/pci by
 *      Curtis Varner, cvarner@cs.ucr.edu
 *
 * Jan 12, 1995 : CPU-PCI bridge optimization support by Frederic Potter.
 *	Alpha version. Intel & UMC chipset support only. See pci.h for more.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#include <asm/segment.h>

#define PCIBIOS_PCI_FUNCTION_ID 	0xb1XX
#define PCIBIOS_PCI_BIOS_PRESENT 	0xb101
#define PCIBIOS_FIND_PCI_DEVICE		0xb102
#define PCIBIOS_FIND_PCI_CLASS_CODE	0xb103
#define PCIBIOS_GENERATE_SPECIAL_CYCLE	0xb106
#define PCIBIOS_READ_CONFIG_BYTE	0xb108
#define PCIBIOS_READ_CONFIG_WORD	0xb109
#define PCIBIOS_READ_CONFIG_DWORD	0xb10a
#define PCIBIOS_WRITE_CONFIG_BYTE	0xb10b
#define PCIBIOS_WRITE_CONFIG_WORD	0xb10c
#define PCIBIOS_WRITE_CONFIG_DWORD	0xb10d

#ifdef CONFIG_PCI
extern void add_pci_resource(unsigned char, unsigned char);
#endif

/* BIOS32 signature: "_32_" */
#define BIOS32_SIGNATURE	(('_' << 0) + ('3' << 8) + ('2' << 16) + ('_' << 24))

/* PCI signature: "PCI " */
#define PCI_SIGNATURE		(('P' << 0) + ('C' << 8) + ('I' << 16) + (' ' << 24))

/* PCI service signature: "$PCI" */
#define PCI_SERVICE		(('$' << 0) + ('P' << 8) + ('C' << 16) + ('I' << 24))

/*
 * This is the standard structure used to identify the entry point
 * to the BIOS32 Service Directory, as documented in
 * 	Standard BIOS 32-bit Service Directory Proposal
 * 	Revision 0.4 May 24, 1993
 * 	Phoenix Technologies Ltd.
 *	Norwood, MA
 * and the PCI BIOS specification.
 */

#ifdef CONFIG_PCI
typedef struct pci_resource_t
{
   unsigned char bus;
   unsigned char dev_fn;
   struct pci_resource_t *next;
} pci_resource_t;
#endif

union bios32 {
	struct {
		unsigned long signature;	/* _32_ */
		unsigned long entry;		/* 32 bit physical address */
		unsigned char revision;		/* Revision level, 0 */
		unsigned char length;		/* Length in paragraphs should be 01 */
		unsigned char checksum;		/* All bytes must add up to zero */
		unsigned char reserved[5]; 	/* Must be zero */
	} fields;
	char chars[16];
};

/*
 * Physical address of the service directory.  I don't know if we're
 * allowed to have more than one of these or not, so just in case
 * we'll make bios32_init() take a memory start parameter and store
 * the array there.
 */

static unsigned long bios32_entry = 0;
static struct {
	unsigned long address;
	unsigned short segment;
} bios32_indirect = { 0, KERNEL_CS };

#ifdef CONFIG_PCI
/*
 * Returns the entry point for the given service, NULL on error
 */

#define PCI_LIST_SIZE 32

static pci_resource_t pci_list = { 0, 0, NULL };
static int pci_index = 0;
static pci_resource_t pci_table[PCI_LIST_SIZE];

static struct	pci_class_type pci_class[PCI_CLASS_NUM] = PCI_CLASS_TYPE;
static struct	pci_vendor_type	pci_vendor[PCI_VENDOR_NUM] = PCI_VENDOR_TYPE;
static struct	pci_device_type	pci_device[PCI_DEVICE_NUM] = PCI_DEVICE_TYPE;

#ifdef CONFIG_PCI_OPTIMIZE
static	struct bridge_mapping_type bridge_mapping[5*BRIDGE_MAPPING_NUM] = BRIDGE_MAPPING_TYPE;
static	struct optimisation_type optimisation[OPTIMISATION_NUM] = OPTIMISATION_TYPE;
#endif

static unsigned long bios32_service(unsigned long service)
{
	unsigned char return_code;	/* %al */
	unsigned long address;		/* %ebx */
	unsigned long length;		/* %ecx */
	unsigned long entry;		/* %edx */

	__asm__("lcall (%%edi)"
		: "=a" (return_code),
		  "=b" (address),
		  "=c" (length),
		  "=d" (entry)
		: "0" (service),
		  "1" (0),
		  "D" (&bios32_indirect));

	switch (return_code) {
		case 0:
			return address + entry;
		case 0x80:	/* Not present */
			printk("bios32_service(%ld) : not present\n", service);
			return 0;
		default: /* Shouldn't happen */
			printk("bios32_service(%ld) : returned 0x%x, mail drew@colorado.edu\n",
				service, return_code);
			return 0;
	}
}

static long pcibios_entry = 0;
static struct {
	unsigned long address;
	unsigned short segment;
} pci_indirect = { 0, KERNEL_CS };

void NCR53c810_test(void);

static unsigned long pcibios_init(unsigned long memory_start, unsigned long memory_end)
{
	unsigned long signature;
	unsigned char present_status;
	unsigned char major_revision;
	unsigned char minor_revision;
	int pack;

	if ((pcibios_entry = bios32_service(PCI_SERVICE))) {
		pci_indirect.address = pcibios_entry;

		__asm__("lcall (%%edi)\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:\tshl $8, %%eax\n\t"
			"movw %%bx, %%ax"
			: "=d" (signature),
			  "=a" (pack)
			: "1" (PCIBIOS_PCI_BIOS_PRESENT),
			  "D" (&pci_indirect)
			: "bx", "cx");

		present_status = (pack >> 16) & 0xff;
		major_revision = (pack >> 8) & 0xff;
		minor_revision = pack & 0xff;
		if (present_status || (signature != PCI_SIGNATURE)) {
			printk ("pcibios_init : %s : BIOS32 Service Directory says PCI BIOS is present,\n"
				"	but PCI_BIOS_PRESENT subfunction fails with present status of 0x%x\n"
				"	and signature of 0x%08lx (%c%c%c%c).  mail drew@Colorado.EDU\n",
				(signature == PCI_SIGNATURE) ?  "WARNING" : "ERROR",
				present_status, signature,
				(char) (signature >>  0), (char) (signature >>  8),
				(char) (signature >> 16), (char) (signature >> 24));

			if (signature != PCI_SIGNATURE)
				pcibios_entry = 0;
		}
		if (pcibios_entry) {
			printk ("pcibios_init : PCI BIOS revision %x.%02x entry at 0x%lx\n",
				major_revision, minor_revision, pcibios_entry);
		}
	}

#if 0
	NCR53c810_test();
#endif
	return memory_start;
}

int pcibios_present(void)
{
	return pcibios_entry ? 1 : 0;
}

int pcibios_find_class (unsigned long class_code, unsigned short index,
	unsigned char *bus, unsigned char *device_fn)
{
	unsigned long bx;
	unsigned long ret;

	__asm__ ("lcall (%%edi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=b" (bx),
		  "=a" (ret)
		: "1" (PCIBIOS_FIND_PCI_CLASS_CODE),
		  "c" (class_code),
		  "S" ((int) index),
		  "D" (&pci_indirect));
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}


int pcibios_find_device (unsigned short vendor, unsigned short device_id,
	unsigned short index, unsigned char *bus, unsigned char *device_fn)
{
	unsigned short bx;
	unsigned short ret;

	__asm__("lcall (%%edi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=b" (bx),
		  "=a" (ret)
		: "1" (PCIBIOS_FIND_PCI_DEVICE),
		  "c" (device_id),
		  "d" (vendor),
		  "S" ((int) index),
		  "D" (&pci_indirect));
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}

int pcibios_read_config_byte(unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_BYTE),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

int pcibios_read_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_WORD),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

int pcibios_read_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned long *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_DWORD),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

int pcibios_write_config_byte (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_BYTE),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

int pcibios_write_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_WORD),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

int pcibios_write_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned long value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_DWORD),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

void NCR53c810_test(void)
{
	unsigned char bus, device_fn;
	unsigned short index;
	int ret;
	unsigned char row, col;
	unsigned long val;

	for (index = 0; index < 4; ++index) {
		ret = pcibios_find_device (
			(unsigned short) PCI_VENDOR_ID_NCR,
			(unsigned short) PCI_DEVICE_ID_NCR_53C810,
			index, &bus, &device_fn);
		if (ret)
			break;
		printk ("ncr53c810 : at PCI bus %d, device %d, function %d.",
			bus, ((device_fn & 0xf8) >> 3), (device_fn & 7));
		for (row = 0; row < 0x3c; row += 0x10) {
			printk ("\n	reg 0x%02x	", row);
			for (col = 0; col < 0x10; col += 4) {
			if (!(ret = pcibios_read_config_dword (bus, device_fn, row+col, &val)))
				printk ("0x%08lx	  ", val);
			else
				printk ("error 0x%02x ", ret);
			}
		}
		printk ("\n");
	}
}

char *pcibios_strerror (int error)
{
	static char buf[80];

	switch (error) {
		case PCIBIOS_SUCCESSFUL:
			return "SUCCESSFUL";

		case PCIBIOS_FUNC_NOT_SUPPORTED:
			return "FUNC_NOT_SUPPORTED";

		case PCIBIOS_BAD_VENDOR_ID:
			return "SUCCESSFUL";

		case PCIBIOS_DEVICE_NOT_FOUND:
			return "DEVICE_NOT_FOUND";

		case PCIBIOS_BAD_REGISTER_NUMBER:
			return "BAD_REGISTER_NUMBER";

		default:
			sprintf (buf, "UNKNOWN RETURN 0x%x", error);
			return buf;
	}
}


/* Recognize multi-function device */

int multi_function(unsigned char bus,unsigned char dev_fn)
{
	unsigned char header;
		pcibios_read_config_byte(
			bus, dev_fn, (unsigned char) PCI_HEADER_TYPE, &header);
		return (header&7==7);
}

/* Returns Interrupt register */

int interrupt_decode(unsigned char bus,unsigned char dev_fn)
{
	unsigned char interrupt;
		pcibios_read_config_byte(
			bus, dev_fn, (unsigned char) PCI_INTERRUPT_LINE, &interrupt);
		if (interrupt>16) return 0;
		return interrupt;
}

/* probe for being bist capable */

int bist_probe(unsigned char bus,unsigned char dev_fn)
{
	unsigned char   	bist;
		pcibios_read_config_byte(
			bus, dev_fn, (unsigned char) PCI_BIST, &bist);
		return (bist & PCI_BIST_CAPABLE !=0);
}


/* Get the chip revision */

int revision_decode(unsigned char bus,unsigned char dev_fn)
{
	unsigned char   	revision;
       		pcibios_read_config_byte(
			bus, dev_fn, (unsigned char) PCI_CLASS_REVISION, &revision);
		return (int) revision;
}



/* Gives the Class code using the 16 higher bits */
/* of the PCI_CLASS_REVISION configuration register */

int class_decode(unsigned char bus,unsigned char dev_fn)
{
	int 			i;
	unsigned long   	class;
		pcibios_read_config_dword(
			bus, dev_fn, (unsigned char) PCI_CLASS_REVISION, &class);
		class=class >> 16;
		for (i=0;i<PCI_CLASS_NUM-1;i++)
			if (class==pci_class[i].class_id) break;
		return i;
}


int device_decode(unsigned char bus,unsigned char dev_fn,unsigned short vendor_num)
{
	int 			i;
	unsigned short   	device;

	pcibios_read_config_word(
		bus, dev_fn, (unsigned char) PCI_DEVICE_ID, &device);
	for (i=0;i<PCI_DEVICE_NUM;i++)
		if ((device==pci_device[i].device_id)
		 && (pci_vendor[vendor_num].vendor_id==pci_device[i].vendor_id)) return i;
	return 0x10000 + (int) device;
}


int vendor_decode(unsigned char bus,unsigned char dev_fn)
{
	int			i;
	unsigned short   	vendor;

	pcibios_read_config_word(
		bus, dev_fn, (unsigned char) PCI_VENDOR_ID, &vendor);
	for (i=0;i<PCI_VENDOR_NUM;i++)
		if (vendor==pci_vendor[i].vendor_id) return i;
	return 0x10000 + (int) vendor;
}

#ifdef CONFIG_PCI_OPTIMIZE

/* Turn on/off PCI bridge optimisation. This should allow benchmarking. */

void burst_bridge(unsigned char bus,unsigned char dev_fn,unsigned char pos, int turn_on)
{
	int i;
	unsigned char val;

	pos*=OPTIMISATION_NUM;
	printk("PCI bridge optimisation.\n");
	for (i=0;i<OPTIMISATION_NUM;i++)
	{
		printk("    %s : ",optimisation[i].type);
		if (bridge_mapping[pos+i].address==0) printk("Not supported.");
		else {
			pcibios_read_config_byte(
				bus, dev_fn, bridge_mapping[pos+i].address, &val);
			if ((val & bridge_mapping[pos+i].mask)==bridge_mapping[pos+i].value) 
			{
				printk("%s.",optimisation[i].on);
				if (turn_on==0) 
				{
				pcibios_write_config_byte(
					bus, dev_fn, bridge_mapping[pos+i].address,
					(val | bridge_mapping[pos+i].mask) -
					bridge_mapping[pos+i].value);
				printk("Changed! now %s.",optimisation[i].off);
				}
			} else {
				printk("%s.",optimisation[i].off);
				if (turn_on==1) 
				{
				pcibios_write_config_byte(
					bus, dev_fn, bridge_mapping[pos+i].address,
					(val & (0xff-bridge_mapping[pos+i].mask)) +
					bridge_mapping[pos+i].value);
				printk("Changed! now %s.",optimisation[i].on);
				}
			}
		}
		printk("\n");
	}
}

#endif

/* In future version in case we detect a PCI to PCI bridge, we will go
for a recursive device search*/

void probe_devices(unsigned char bus)
{
	unsigned long	res;
	unsigned char	dev_fn;

/* For a mysterious reason, my PC crash if I try to probe device 31 function 7  */
/* (i.e. dev_fn=0xff) It can be a bug in my BIOS, or I haven't understood all about */
/* PCI */


		for (dev_fn=0x0;dev_fn<0xff;dev_fn++) {
			pcibios_read_config_dword(
				bus, dev_fn, (unsigned char) PCI_CLASS_REVISION, &res);

/* First we won't try to talk to non_present chip */
/* Second, we get rid of non multi-function device that seems to be lazy  */
/* and not fully decode the function number */

			if ((res!=0xffffffff) &&
				(((dev_fn & 7) == 0) || multi_function(bus,dev_fn))) {
				add_pci_resource( bus, dev_fn);
			}
		}
}


void probe_pci(void)
{
	if (pcibios_present()==0) {
		printk("ProbePci PCI bios not detected.\n");
		return;
	}
	printk( "Probing PCI hardware.\n");
	probe_devices(0);
}


/*
 * Function to add a resource to the pci list...
 */
void add_pci_resource(unsigned char bus, unsigned char dev_fn)
{
	pci_resource_t* new_pci;
	pci_resource_t* temp;
	int vendor_id, device_id;
#ifdef CONFIG_PCI_OPTIMIZE
	unsigned char bridge_id;
#endif
	/*
	 * Verify if we know about this chip. If not, print Vendor & Device id
	 * + ask for report.
	 */
	vendor_id=vendor_decode(bus,dev_fn);
	device_id=device_decode(bus,dev_fn,vendor_id);
	if ((device_id & 0x10000)==0x10000)
	{
		printk("Unknown PCI device. PCI Vendor id=%x. PCI Device id=%x.\n",
			vendor_id & 0xffff,device_id & 0xffff);
		printk("PLEASE MAIL POTTER@CAO-VLSI.IBP.FR your hardware description and /proc/pci.\n");
		return;
	}
	/*
	 * If the PCI agent is a known bridge, then configure it.
	 */
#ifdef CONFIG_PCI_OPTIMIZE

	bridge_id=pci_device[device_id].bridge_id;
	if (bridge_id != 0xff)
	{
		burst_bridge(bus,dev_fn,bridge_id,1); /* Burst bridge */
	}
#endif
	/*
	 * Request and verify allocation of kernel RAM
	 */
	if(pci_index > PCI_LIST_SIZE-1)
	{
		printk("PCI resource list full.\n");
		return;
	}


	new_pci = &pci_table[pci_index];
	pci_index++;

	/*
	 * Enter the new node into the list....
	 *
	 */
	if(pci_list.next != NULL)
	{
		for(temp = pci_list.next; (temp->next); temp = temp->next)
			/* nothing */;

		temp->next = new_pci;
	}
	else
		pci_list.next = new_pci;

	/*
	 * Set the information for the node
	 */
	new_pci->next = NULL;
	new_pci->bus = bus;
	new_pci->dev_fn = dev_fn;

	return;
}


int get_pci_list(char* buf)
{
	int pr, length;
	pci_resource_t* temp = pci_list.next;

	pr = sprintf(buf, "PCI devices found :\n"); 
	for (length = pr ; (temp) && (length<4000); temp = temp->next)
	{
		pr=vendor_decode(temp->bus,temp->dev_fn);

		length += sprintf(buf+length, "Bus %2d Device %3d Function %2d.\n",
			(int)temp->bus,
			(int)((temp->dev_fn & 0xf8) >> 3),
			(int) (temp->dev_fn & 7));

		length += sprintf(buf+length, "    %s : %s %s (rev %d). ",
			pci_class[class_decode(temp->bus, temp->dev_fn)].class_name,
			pci_vendor[pr].vendor_name,
			pci_device[device_decode(temp->bus, temp->dev_fn, pr)].device_name,
			revision_decode(temp->bus, temp->dev_fn));

		if (bist_probe(temp->bus, temp->dev_fn))
			length += sprintf(buf+length, "BIST capable. ");

		if ((pr = interrupt_decode(temp->bus, temp->dev_fn)) != 0)
			length += sprintf(buf+length, "8259's interrupt %d.", pr);

		length += sprintf(buf+length, "\n");
	}

	if (temp)
		length += sprintf(buf+length, "4K limit reached!\n");

	return length;
}


#endif

unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	union bios32 *check;
	unsigned char sum;
	int i, length;

	/*
	 * Follow the standard procedure for locating the BIOS32 Service
	 * directory by scanning the permissible address range from
	 * 0xe0000 through 0xfffff for a valid BIOS32 structure.
	 *
	 * The PCI BIOS doesn't seem to work too well on many machines,
	 * so we disable this unless it's really needed (NCR SCSI driver)
	 */

	for (check = (union bios32 *) 0xe0000; check <= (union bios32 *) 0xffff0; ++check) {
		if (check->fields.signature != BIOS32_SIGNATURE)
			continue;
		length = check->fields.length * 16;
		if (!length)
			continue;
		sum = 0;
		for (i = 0; i < length ; ++i)
			sum += check->chars[i];
		if (sum != 0)
			continue;
		if (check->fields.revision != 0) {
			printk("bios32_init : unsupported revision %d at 0x%p, mail drew@colorado.edu\n",
				check->fields.revision, check);
			continue;
		}
		printk ("bios32_init : BIOS32 Service Directory structure at 0x%p\n", check);
		if (!bios32_entry) {
			if (check->fields.entry >= 0x100000) {
				printk("bios32_init: entry in high memory, unable to access\n");
			} else {
				bios32_indirect.address = bios32_entry = check->fields.entry;
				printk ("bios32_init : BIOS32 Service Directory entry at 0x%lx\n", bios32_entry);
			}
		} else {
			printk ("bios32_init : multiple entries, mail drew@colorado.edu\n");
			/*
			 * Jeremy Fitzhardinge reports at least one PCI BIOS
			 * with two different service directories, and as both
			 * worked for him, we'll just mention the fact, and
			 * not actually disallow it..
			 */
#if 0
			return memory_start;
#endif
		}
	}
#ifdef CONFIG_PCI
	if (bios32_entry) {
		memory_start = pcibios_init (memory_start, memory_end);
		probe_pci();
	}
#endif
	return memory_start;
}




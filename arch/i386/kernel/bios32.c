/*
 * bios32.c - BIOS32, PCI BIOS functions.
 *
 * $Id: bios32.c,v 1.12 1997/06/26 13:33:46 mj Exp $
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
 *	Alpha version. Intel & UMC chipset support only.
 *
 * Apr 16, 1995 : Source merge with the DEC Alpha PCI support. Most of the code
 *	moved to drivers/pci/pci.c.
 *
 * Dec 7, 1996  : Added support for direct configuration access of boards
 *      with Intel compatible access schemes (tsbogend@alpha.franken.de)
 *
 * Feb 3, 1997  : Set internal functions to static, save/restore flags
 *	avoid dead locks reading broken PCI BIOS, werner@suse.de 
 *
 * Apr 26, 1997 : Fixed case when there is BIOS32, but not PCI BIOS
 *	(mj@atrey.karlin.mff.cuni.cz)
 *
 * May 7,  1997 : Added some missing cli()'s. [mj]
 * 
 * Jun 20, 1997 : Corrected problems in "conf1" type accesses.
 *      (paubert@iram.es)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

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

#ifdef CONFIG_PCI
/*
 * Physical address of the service directory.  I don't know if we're
 * allowed to have more than one of these or not, so just in case
 * we'll make pcibios_present() take a memory start parameter and store
 * the array there.
 */

static unsigned long bios32_entry = 0;
static struct {
	unsigned long address;
	unsigned short segment;
} bios32_indirect = { 0, KERNEL_CS };


/*
 * function table for accessing PCI configuration space
 */
struct pci_access {
    int (*find_device)(unsigned short, unsigned short, unsigned short, unsigned char *, unsigned char *);
    int (*find_class)(unsigned int, unsigned short, unsigned char *, unsigned char *);
    int (*read_config_byte)(unsigned char, unsigned char, unsigned char, unsigned char *);
    int (*read_config_word)(unsigned char, unsigned char, unsigned char, unsigned short *);
    int (*read_config_dword)(unsigned char, unsigned char, unsigned char, unsigned int *);
    int (*write_config_byte)(unsigned char, unsigned char, unsigned char, unsigned char);
    int (*write_config_word)(unsigned char, unsigned char, unsigned char, unsigned short);
    int (*write_config_dword)(unsigned char, unsigned char, unsigned char, unsigned int);
};

/*
 * pointer to selected PCI access function table
 */
static struct pci_access *access_pci = NULL;



/*
 * Returns the entry point for the given service, NULL on error
 */

static unsigned long bios32_service(unsigned long service)
{
	unsigned char return_code;	/* %al */
	unsigned long address;		/* %ebx */
	unsigned long length;		/* %ecx */
	unsigned long entry;		/* %edx */
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%edi)"
		: "=a" (return_code),
		  "=b" (address),
		  "=c" (length),
		  "=d" (entry)
		: "0" (service),
		  "1" (0),
		  "D" (&bios32_indirect));
	restore_flags(flags);

	switch (return_code) {
		case 0:
			return address + entry;
		case 0x80:	/* Not present */
			printk("bios32_service(0x%lx) : not present\n", service);
			return 0;
		default: /* Shouldn't happen */
			printk("bios32_service(0x%lx) : returned 0x%x, mail drew@colorado.edu\n",
				service, return_code);
			return 0;
	}
}

static long pcibios_entry = 0;
static struct {
	unsigned long address;
	unsigned short segment;
} pci_indirect = { 0, KERNEL_CS };


__initfunc(static int check_pcibios(void))
{
	unsigned long signature;
	unsigned char present_status;
	unsigned char major_revision;
	unsigned char minor_revision;
	unsigned long flags;
	int pack;

	if ((pcibios_entry = bios32_service(PCI_SERVICE))) {
		pci_indirect.address = pcibios_entry | PAGE_OFFSET;

		save_flags(flags); cli();
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
		restore_flags(flags);

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
			return 1;
		}
	}
	return 0;
}


static int pci_bios_find_class (unsigned int class_code, unsigned short index,
	unsigned char *bus, unsigned char *device_fn)
{
	unsigned long bx;
	unsigned long ret;
	unsigned long flags;

	save_flags(flags); cli();
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
	restore_flags(flags);
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}


static int pci_bios_find_device (unsigned short vendor, unsigned short device_id,
	unsigned short index, unsigned char *bus, unsigned char *device_fn)
{
	unsigned short bx;
	unsigned short ret;
	unsigned long flags;

	save_flags(flags); cli();
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
	restore_flags(flags);
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_read_config_byte(unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
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
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_read_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
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
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_read_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
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
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_byte (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
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
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
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
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
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
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

/*
 * function table for BIOS32 access
 */
static struct pci_access pci_bios_access = {
      pci_bios_find_device,
      pci_bios_find_class,
      pci_bios_read_config_byte,
      pci_bios_read_config_word,
      pci_bios_read_config_dword,
      pci_bios_write_config_byte,
      pci_bios_write_config_word,
      pci_bios_write_config_dword
};



/*
 * Given the vendor and device ids, find the n'th instance of that device
 * in the system.  
 */
static int pci_direct_find_device (unsigned short vendor, unsigned short device_id,
			   unsigned short index, unsigned char *bus,
			   unsigned char *devfn)
{
    unsigned int curr = 0;
    struct pci_dev *dev;
    unsigned long flags;

    save_flags(flags);
    for (dev = pci_devices; dev; dev = dev->next) {
	if (dev->vendor == vendor && dev->device == device_id) {
	    if (curr == index) {
		*devfn = dev->devfn;
		*bus = dev->bus->number;
		restore_flags(flags);
		return PCIBIOS_SUCCESSFUL;
	    }
	    ++curr;
	}
    }
    restore_flags(flags);
    return PCIBIOS_DEVICE_NOT_FOUND;
}


/*
 * Given the class, find the n'th instance of that device
 * in the system.
 */
static int pci_direct_find_class (unsigned int class_code, unsigned short index,
			  unsigned char *bus, unsigned char *devfn)
{
    unsigned int curr = 0;
    struct pci_dev *dev;
    unsigned long flags;

    save_flags(flags); cli();
    for (dev = pci_devices; dev; dev = dev->next) {
	if (dev->class == class_code) {
	    if (curr == index) {
		*devfn = dev->devfn;
		*bus = dev->bus->number;
		restore_flags(flags);
		return PCIBIOS_SUCCESSFUL;
	    }
	    ++curr;
	}
    }
    restore_flags(flags);
    return PCIBIOS_DEVICE_NOT_FOUND;
}

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */
#define CONFIG_CMD(bus, device_fn, where)   (0x80000000 | (bus << 16) | (device_fn << 8) | (where & ~3))

static int pci_conf1_read_config_byte(unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned char *value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    *value = inb(0xCFC + (where&3));
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_read_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short *value)
{
    unsigned long flags;

    if (where&1) return PCIBIOS_BAD_REGISTER_NUMBER;
    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);    
    *value = inw(0xCFC + (where&2));
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_read_config_dword (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned int *value)
{
    unsigned long flags;

    if (where&3) return PCIBIOS_BAD_REGISTER_NUMBER;
    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    *value = inl(0xCFC);
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_write_config_byte (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned char value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);    
    outb(value, 0xCFC + (where&3));
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_word (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned short value)
{
    unsigned long flags;

    if (where&1) return PCIBIOS_BAD_REGISTER_NUMBER;
    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    outw(value, 0xCFC + (where&2));
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_dword (unsigned char bus, unsigned char device_fn, 
				  unsigned char where, unsigned int value)
{
    unsigned long flags;

    if (where&3) return PCIBIOS_BAD_REGISTER_NUMBER;
    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    outl(value, 0xCFC);
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

#undef CONFIG_CMD

/*
 * functiontable for type 1
 */
static struct pci_access pci_direct_conf1 = {
      pci_direct_find_device,
      pci_direct_find_class,
      pci_conf1_read_config_byte,
      pci_conf1_read_config_word,
      pci_conf1_read_config_dword,
      pci_conf1_write_config_byte,
      pci_conf1_write_config_word,
      pci_conf1_write_config_dword
};

/*
 * Functions for accessing PCI configuration space with type 2 accesses
 */
#define IOADDR(devfn, where)   ((0xC000 | ((devfn & 0x78) << 5)) + where)
#define FUNC(devfn)            (((devfn & 7) << 1) | 0xf0)

static int pci_conf2_read_config_byte(unsigned char bus, unsigned char device_fn, 
			       unsigned char where, unsigned char *value)
{
    unsigned long flags;

    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inb(IOADDR(device_fn,where));
    outb (0, 0xCF8);
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_word (unsigned char bus, unsigned char device_fn, 
				unsigned char where, unsigned short *value)
{
    unsigned long flags;

    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inw(IOADDR(device_fn,where));
    outb (0, 0xCF8);
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_dword (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned int *value)
{
    unsigned long flags;

    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inl (IOADDR(device_fn,where));    
    outb (0, 0xCF8);    
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_byte (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned char value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outb (value, IOADDR(device_fn,where));
    outb (0, 0xCF8);    
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_word (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned short value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outw (value, IOADDR(device_fn,where));
    outb (0, 0xCF8);    
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_dword (unsigned char bus, unsigned char device_fn, 
				  unsigned char where, unsigned int value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outl (value, IOADDR(device_fn,where));    
    outb (0, 0xCF8);    
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

#undef IOADDR
#undef FUNC

/*
 * functiontable for type 2
 */
static struct pci_access pci_direct_conf2 = {
      pci_direct_find_device,
      pci_direct_find_class,
      pci_conf2_read_config_byte,
      pci_conf2_read_config_word,
      pci_conf2_read_config_dword,
      pci_conf2_write_config_byte,
      pci_conf2_write_config_word,
      pci_conf2_write_config_dword
};


__initfunc(static struct pci_access *check_direct_pci(void))
{
    unsigned int tmp;
    unsigned long flags;

    save_flags(flags); cli();

    /*
     * check if configuration type 1 works
     */
    outb (0x01, 0xCFB);
    tmp = inl (0xCF8);
    outl (0x80000000, 0xCF8);
    if (inl (0xCF8) == 0x80000000) {
	outl (tmp, 0xCF8);
	restore_flags(flags);
	printk("pcibios_init: Using configuration type 1\n");
	return &pci_direct_conf1;
    }
    outl (tmp, 0xCF8);

    /*
     * check if configuration type 2 works
     */
    outb (0x00, 0xCFB);
    outb (0x00, 0xCF8);
    outb (0x00, 0xCFA);
    if (inb (0xCF8) == 0x00 && inb (0xCFB) == 0x00) {
	restore_flags(flags);
	printk("pcibios_init: Using configuration type 2\n");
	return &pci_direct_conf2;
    }
    restore_flags(flags);
    printk("pcibios_init: Not supported chipset for direct PCI access !\n");
    return NULL;
}


/*
 * access defined pcibios functions via
 * the function table
 */

int pcibios_present(void)
{
	return access_pci ? 1 : 0;
}

int pcibios_find_class (unsigned int class_code, unsigned short index,
	unsigned char *bus, unsigned char *device_fn)
{
   if (access_pci && access_pci->find_class)
      return access_pci->find_class(class_code, index, bus, device_fn);
    
    return PCIBIOS_FUNC_NOT_SUPPORTED;
}

int pcibios_find_device (unsigned short vendor, unsigned short device_id,
	unsigned short index, unsigned char *bus, unsigned char *device_fn)
{
    if (access_pci && access_pci->find_device)
      return access_pci->find_device(vendor, device_id, index, bus, device_fn);
    
    return PCIBIOS_FUNC_NOT_SUPPORTED;
}

int pcibios_read_config_byte (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char *value)
{
    if (access_pci && access_pci->read_config_byte)
      return access_pci->read_config_byte(bus, device_fn, where, value);
    
    return PCIBIOS_FUNC_NOT_SUPPORTED;
}

int pcibios_read_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short *value)
{
    if (access_pci && access_pci->read_config_word)
      return access_pci->read_config_word(bus, device_fn, where, value);
    
    return PCIBIOS_FUNC_NOT_SUPPORTED;
}

int pcibios_read_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int *value)
{
    if (access_pci && access_pci->read_config_dword)
      return access_pci->read_config_dword(bus, device_fn, where, value);
    
    return PCIBIOS_FUNC_NOT_SUPPORTED;
}

int pcibios_write_config_byte (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char value)
{
    if (access_pci && access_pci->write_config_byte)
      return access_pci->write_config_byte(bus, device_fn, where, value);
    
    return PCIBIOS_FUNC_NOT_SUPPORTED;
}

int pcibios_write_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short value)
{
    if (access_pci && access_pci->write_config_word)
      return access_pci->write_config_word(bus, device_fn, where, value);
    
    return PCIBIOS_FUNC_NOT_SUPPORTED;    
}

int pcibios_write_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int value)
{
    if (access_pci && access_pci->write_config_dword)
      return access_pci->write_config_dword(bus, device_fn, where, value);
    
    return PCIBIOS_FUNC_NOT_SUPPORTED;
}

const char *pcibios_strerror (int error)
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

                case PCIBIOS_SET_FAILED:          
			return "SET_FAILED";

                case PCIBIOS_BUFFER_TOO_SMALL:    
			return "BUFFER_TOO_SMALL";

		default:
			sprintf (buf, "UNKNOWN RETURN 0x%x", error);
			return buf;
	}
}


__initfunc(unsigned long pcibios_fixup(unsigned long mem_start, unsigned long mem_end))
{
    return mem_start;
}

#endif

__initfunc(unsigned long pcibios_init(unsigned long memory_start, unsigned long memory_end))
{
#ifdef CONFIG_PCI
	union bios32 *check;
	unsigned char sum;
	int i, length;

	/*
	 * Follow the standard procedure for locating the BIOS32 Service
	 * directory by scanning the permissible address range from
	 * 0xe0000 through 0xfffff for a valid BIOS32 structure.
	 *
	 */

	for (check = (union bios32 *) __va(0xe0000);
	     check <= (union bios32 *) __va(0xffff0);
	     ++check) {
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
			printk("pcibios_init : unsupported revision %d at 0x%p, mail drew@colorado.edu\n",
				check->fields.revision, check);
			continue;
		}
		printk ("pcibios_init : BIOS32 Service Directory structure at 0x%p\n", check);
		if (!bios32_entry) {
			if (check->fields.entry >= 0x100000) {
				printk("pcibios_init: entry in high memory, trying direct PCI access\n");
 				access_pci = check_direct_pci();
			} else {
				bios32_entry = check->fields.entry;
				printk ("pcibios_init : BIOS32 Service Directory entry at 0x%lx\n", bios32_entry);
				bios32_indirect.address = bios32_entry + PAGE_OFFSET;
			}
		}
	}
	if (bios32_entry && check_pcibios())
 		access_pci = &pci_bios_access;
#endif
	return memory_start;
}

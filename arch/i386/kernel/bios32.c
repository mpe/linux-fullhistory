/*
 * bios32.c - Low-Level PCI Access
 *
 * $Id: bios32.c,v 1.48 1998/09/26 08:06:55 mj Exp $
 *
 * Copyright 1993, 1994 Drew Eckhardt
 *      Visionary Computing
 *      (Unix and Linux consulting and custom programming)
 *      Drew@Colorado.EDU
 *      +1 (303) 786-7975
 *
 * Drew's work was sponsored by:
 *	iX Multiuser Multitasking Magazine
 *	Hannover, Germany
 *	hm@ix.de
 *
 * Copyright 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 * For more information, please consult the following manuals (look at
 * http://www.pcisig.com/ for how to get them):
 *
 * PCI BIOS Specification
 * PCI Local Bus Specification
 * PCI to PCI Bridge Specification
 * PCI System Design Guide
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
 *
 * Aug 2,  1997 : Split to PCI BIOS handling and direct PCI access parts
 *	and cleaned it up...     Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 * Feb 6,  1998 : No longer using BIOS to find devices and device classes. [mj]
 *
 * May 1,  1998 : Support for peer host bridges. [mj]
 *
 * Jun 19, 1998 : Changed to use spinlocks, so that PCI configuration space
 *	can be accessed from interrupts even on SMP systems. [mj]
 *
 * August  1998 : Better support for peer host bridges and more paranoid
 *	checks for direct hardware access. Ugh, this file starts to look as
 *	a large gallery of common hardware bug workarounds (watch the comments)
 *	-- the PCI specs themselves are sane, but most implementors should be
 *	hit hard with \hammer scaled \magstep5. [mj]
 *
 * Jan 23, 1999 : More improvements to peer host bridge logic. i450NX fixup. [mj]
 *
 * Feb 8,  1999 : Added UM8886BF I/O address fixup. [mj]
 *
 * August  1999 : New resource management and configuration access stuff. [mj]
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/irq.h>

#include <asm/page.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/spinlock.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#define PCI_PROBE_BIOS 1
#define PCI_PROBE_CONF1 2
#define PCI_PROBE_CONF2 4
#define PCI_NO_SORT 0x100
#define PCI_BIOS_SORT 0x200
#define PCI_NO_CHECKS 0x400
#define PCI_NO_PEER_FIXUP 0x800
#define PCI_ASSIGN_ROMS 0x1000

static unsigned int pci_probe = PCI_PROBE_BIOS | PCI_PROBE_CONF1 | PCI_PROBE_CONF2;

/*
 * Direct access to PCI hardware...
 */

#ifdef CONFIG_PCI_DIRECT

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */

#define CONFIG_CMD(dev, where)   (0x80000000 | (dev->bus->number << 16) | (dev->devfn << 8) | (where & ~3))

static int pci_conf1_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);
    *value = inb(0xCFC + (where&3));
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);    
    *value = inw(0xCFC + (where&2));
    return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);
    *value = inl(0xCFC);
    return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);    
    outb(value, 0xCFC + (where&3));
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_word(struct pci_dev *dev, int where, u16 value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);
    outw(value, 0xCFC + (where&2));
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);
    outl(value, 0xCFC);
    return PCIBIOS_SUCCESSFUL;
}

#undef CONFIG_CMD

static struct pci_ops pci_direct_conf1 = {
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

#define IOADDR(devfn, where)	((0xC000 | ((devfn & 0x78) << 5)) + where)
#define FUNC(devfn)		(((devfn & 7) << 1) | 0xf0)
#define SET(dev)		if (dev->devfn) return PCIBIOS_DEVICE_NOT_FOUND;		\
				outb(FUNC(dev->devfn), 0xCF8);					\
				outb(dev->bus->number, 0xCFA);

static int pci_conf2_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
    SET(dev);
    *value = inb(IOADDR(dev->devfn,where));
    outb (0, 0xCF8);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
    SET(dev);
    *value = inw(IOADDR(dev->devfn,where));
    outb (0, 0xCF8);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
    SET(dev);
    *value = inl (IOADDR(dev->devfn,where));    
    outb (0, 0xCF8);    
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
    SET(dev);
    outb (value, IOADDR(dev->devfn,where));
    outb (0, 0xCF8);    
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_word(struct pci_dev *dev, int where, u16 value)
{
    SET(dev);
    outw (value, IOADDR(dev->devfn,where));
    outb (0, 0xCF8);    
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
    SET(dev);
    outl (value, IOADDR(dev->devfn,where));    
    outb (0, 0xCF8);    
    return PCIBIOS_SUCCESSFUL;
}

#undef SET
#undef IOADDR
#undef FUNC

static struct pci_ops pci_direct_conf2 = {
      pci_conf2_read_config_byte,
      pci_conf2_read_config_word,
      pci_conf2_read_config_dword,
      pci_conf2_write_config_byte,
      pci_conf2_write_config_word,
      pci_conf2_write_config_dword
};

/*
 * Before we decide to use direct hardware access mechanisms, we try to do some
 * trivial checks to ensure it at least _seems_ to be working -- we just test
 * whether bus 00 contains a host bridge (this is similar to checking
 * techniques used in XFree86, but ours should be more reliable since we
 * attempt to make use of direct access hints provided by the PCI BIOS).
 *
 * This should be close to trivial, but it isn't, because there are buggy
 * chipsets (yes, you guessed it, by Intel and Compaq) that have no class ID.
 */
static int __init pci_sanity_check(struct pci_ops *o)
{
	u16 x;
	struct pci_bus bus;		/* Fake bus and device */
	struct pci_dev dev;

#ifdef CONFIG_VISWS
	return 1;       /* Lithium PCI Bridges are non-standard */
#endif

	if (pci_probe & PCI_NO_CHECKS)
		return 1;
	bus.number = 0;
	dev.bus = &bus;
	for(dev.devfn=0; dev.devfn < 0x100; dev.devfn++)
		if ((!o->read_word(&dev, PCI_CLASS_DEVICE, &x) &&
		     (x == PCI_CLASS_BRIDGE_HOST || x == PCI_CLASS_DISPLAY_VGA)) ||
		    (!o->read_word(&dev, PCI_VENDOR_ID, &x) &&
		     (x == PCI_VENDOR_ID_INTEL || x == PCI_VENDOR_ID_COMPAQ)))
			return 1;
	DBG("PCI: Sanity check failed\n");
	return 0;
}

static struct pci_ops * __init pci_check_direct(void)
{
	unsigned int tmp;
	unsigned long flags;

	__save_flags(flags); __cli();

	/*
	 * Check if configuration type 1 works.
	 */
	if (pci_probe & PCI_PROBE_CONF1) {
		outb (0x01, 0xCFB);
		tmp = inl (0xCF8);
		outl (0x80000000, 0xCF8);
		if (inl (0xCF8) == 0x80000000 &&
		    pci_sanity_check(&pci_direct_conf1)) {
			outl (tmp, 0xCF8);
			__restore_flags(flags);
			printk("PCI: Using configuration type 1\n");
			return &pci_direct_conf1;
		}
		outl (tmp, 0xCF8);
	}

	/*
	 * Check if configuration type 2 works.
	 */
	if (pci_probe & PCI_PROBE_CONF2) {
		outb (0x00, 0xCFB);
		outb (0x00, 0xCF8);
		outb (0x00, 0xCFA);
		if (inb (0xCF8) == 0x00 && inb (0xCFA) == 0x00 &&
		    pci_sanity_check(&pci_direct_conf2)) {
			__restore_flags(flags);
			printk("PCI: Using configuration type 2\n");
			return &pci_direct_conf2;
		}
	}

	__restore_flags(flags);
	return NULL;
}

#endif

/*
 * BIOS32 and PCI BIOS handling.
 */

#ifdef CONFIG_PCI_BIOS

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

/* PCI BIOS hardware mechanism flags */
#define PCIBIOS_HW_TYPE1		0x01
#define PCIBIOS_HW_TYPE2		0x02
#define PCIBIOS_HW_TYPE1_SPEC		0x10
#define PCIBIOS_HW_TYPE2_SPEC		0x20

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

/*
 * Physical address of the service directory.  I don't know if we're
 * allowed to have more than one of these or not, so just in case
 * we'll make pcibios_present() take a memory start parameter and store
 * the array there.
 */

static struct {
	unsigned long address;
	unsigned short segment;
} bios32_indirect = { 0, __KERNEL_CS };

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

	__save_flags(flags); __cli();
	__asm__("lcall (%%edi)"
		: "=a" (return_code),
		  "=b" (address),
		  "=c" (length),
		  "=d" (entry)
		: "0" (service),
		  "1" (0),
		  "D" (&bios32_indirect));
	__restore_flags(flags);

	switch (return_code) {
		case 0:
			return address + entry;
		case 0x80:	/* Not present */
			printk("bios32_service(0x%lx): not present\n", service);
			return 0;
		default: /* Shouldn't happen */
			printk("bios32_service(0x%lx): returned 0x%x, report to <mj@ucw.cz>.\n",
				service, return_code);
			return 0;
	}
}

static struct {
	unsigned long address;
	unsigned short segment;
} pci_indirect = { 0, __KERNEL_CS };

static int pci_bios_present;

static int __init check_pcibios(void)
{
	u32 signature, eax, ebx, ecx;
	u8 status, major_ver, minor_ver, hw_mech, last_bus;
	unsigned long flags, pcibios_entry;

	if ((pcibios_entry = bios32_service(PCI_SERVICE))) {
		pci_indirect.address = pcibios_entry + PAGE_OFFSET;

		__save_flags(flags); __cli();
		__asm__(
			"lcall (%%edi)\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=d" (signature),
			  "=a" (eax),
			  "=b" (ebx),
			  "=c" (ecx)
			: "1" (PCIBIOS_PCI_BIOS_PRESENT),
			  "D" (&pci_indirect)
			: "memory");
		__restore_flags(flags);

		status = (eax >> 8) & 0xff;
		hw_mech = eax & 0xff;
		major_ver = (ebx >> 8) & 0xff;
		minor_ver = ebx & 0xff;
		last_bus = ecx & 0xff;
		DBG("PCI: BIOS probe returned s=%02x hw=%02x ver=%02x.%02x l=%02x\n",
			status, hw_mech, major_ver, minor_ver, last_bus);
		if (status || signature != PCI_SIGNATURE) {
			printk (KERN_ERR "PCI: BIOS BUG #%x[%08x] found, report to <mj@ucw.cz>\n",
				status, signature);
			return 0;
		}
		printk("PCI: PCI BIOS revision %x.%02x entry at 0x%lx\n",
			major_ver, minor_ver, pcibios_entry);
#ifdef CONFIG_PCI_DIRECT
		if (!(hw_mech & PCIBIOS_HW_TYPE1))
			pci_probe &= ~PCI_PROBE_CONF1;
		if (!(hw_mech & PCIBIOS_HW_TYPE2))
			pci_probe &= ~PCI_PROBE_CONF2;
#endif
		return 1;
	}
	return 0;
}

#if 0	/* Not used */

static int pci_bios_find_class (unsigned int class_code, unsigned short index,
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

#endif

static int __init pci_bios_find_device (unsigned short vendor, unsigned short device_id,
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

static int pci_bios_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

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

static int pci_bios_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

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

static int pci_bios_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

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

static int pci_bios_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

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

static int pci_bios_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

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

static int pci_bios_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

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

/*
 * Function table for BIOS32 access
 */

static struct pci_ops pci_bios_access = {
      pci_bios_read_config_byte,
      pci_bios_read_config_word,
      pci_bios_read_config_dword,
      pci_bios_write_config_byte,
      pci_bios_write_config_word,
      pci_bios_write_config_dword
};

/*
 * Try to find PCI BIOS.
 */

static struct pci_ops * __init pci_find_bios(void)
{
	union bios32 *check;
	unsigned char sum;
	int i, length;

	/*
	 * Follow the standard procedure for locating the BIOS32 Service
	 * directory by scanning the permissible address range from
	 * 0xe0000 through 0xfffff for a valid BIOS32 structure.
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
			printk("PCI: unsupported BIOS32 revision %d at 0x%p, report to <mj@ucw.cz>\n",
				check->fields.revision, check);
			continue;
		}
		DBG("PCI: BIOS32 Service Directory structure at 0x%p\n", check);
		if (check->fields.entry >= 0x100000) {
			printk("PCI: BIOS32 entry (0x%p) in high memory, cannot use.\n", check);
			return NULL;
		} else {
			unsigned long bios32_entry = check->fields.entry;
			DBG("PCI: BIOS32 Service Directory entry at 0x%lx\n", bios32_entry);
			bios32_indirect.address = bios32_entry + PAGE_OFFSET;
			if (check_pcibios())
				return &pci_bios_access;
		}
		break;	/* Hopefully more than one BIOS32 cannot happen... */
	}

	return NULL;
}

/*
 * Sort the device list according to PCI BIOS. Nasty hack, but since some
 * fool forgot to define the `correct' device order in the PCI BIOS specs
 * and we want to be (possibly bug-to-bug ;-]) compatible with older kernels
 * which used BIOS ordering, we are bound to do this...
 */

static void __init pcibios_sort(void)
{
	struct pci_dev *dev = pci_devices;
	struct pci_dev **last = &pci_devices;
	struct pci_dev *d, **dd, *e;
	int idx;
	unsigned char bus, devfn;

	DBG("PCI: Sorting device list...\n");
	while ((e = dev)) {
		idx = 0;
		while (pci_bios_find_device(e->vendor, e->device, idx, &bus, &devfn) == PCIBIOS_SUCCESSFUL) {
			idx++;
			for(dd=&dev; (d = *dd); dd = &d->next) {
				if (d->bus->number == bus && d->devfn == devfn) {
					*dd = d->next;
					*last = d;
					last = &d->next;
					break;
				}
			}
			if (!d) {
				printk("PCI: BIOS reporting unknown device %02x:%02x\n", bus, devfn);
				/*
				 * We must not continue scanning as several buggy BIOSes
				 * return garbage after the last device. Grr.
				 */
				break;
			}
		}
		if (e == dev) {
			printk("PCI: Device %02x:%02x not found by BIOS\n",
				dev->bus->number, dev->devfn);
			d = dev;
			dev = dev->next;
			*last = d;
			last = &d->next;
		}
	}
	*last = NULL;
}

#endif

/*
 * Several BIOS'es forget to assign addresses to I/O ranges. Try to fix it.
 */

static void __init pcibios_fixup_io_addr(struct pci_dev *dev, int idx)
{
	unsigned int reg = PCI_BASE_ADDRESS_0 + 4*idx;
	struct resource *r = &dev->resource[idx];
	unsigned int size = r->end - r->start + 1;

	if (((dev->class >> 8) == PCI_CLASS_STORAGE_IDE && idx < 4) ||
	     (dev->class >> 8) == PCI_CLASS_DISPLAY_VGA) {
		/*
		 * In case the BIOS didn't assign an address 0--3 to an IDE
		 * controller, we don't try to fix it as it means "use default
		 * addresses" at least with several broken chips and the IDE
		 * driver needs the original settings to recognize which devices
		 * correspond to the primary controller.
		 *
		 * We don't assign VGA I/O ranges as well.
		 */
		return;
	}
	/*
	 * We need to avoid collisions with `mirrored' VGA ports and other strange
	 * ISA hardware, so we always want the addresses kilobyte aligned.
	 */
	if (!size || size > 256) {
		printk(KERN_ERR "PCI: Cannot assign I/O space to device %s, %d bytes are too much.\n", dev->name, size);
		return;
	} else {
		u32 try;

		if (allocate_resource(&ioport_resource, r, size, 0x1000, ~0, 1024)) {
			printk(KERN_ERR "PCI: Unable to find free %d bytes of I/O space for device %s.\n", size, dev->name);
			return;
		}
		printk("PCI: Assigning I/O space %04lx-%04lx to device %s\n", r->start, r->end, dev->name);
		pci_write_config_dword(dev, reg, r->start | PCI_BASE_ADDRESS_SPACE_IO);
		pci_read_config_dword(dev, reg, &try);
		if ((try & PCI_BASE_ADDRESS_IO_MASK) != r->start) {
			r->start = 0;
			pci_write_config_dword(dev, reg, 0);
			printk(KERN_ERR "PCI: I/O address setup failed, got %04x\n", try);
		}
	}
}

/*
 * Assign address to expansion ROM. This is a highly experimental feature
 * and you must enable it by "pci=rom". It's even not guaranteed to work
 * with all cards since the PCI specs allow address decoders to be shared
 * between the ROM space and one of the standard regions (sigh!).
 */
static void __init pcibios_fixup_rom_addr(struct pci_dev *dev)
{
	int reg = (dev->hdr_type == 1) ? PCI_ROM_ADDRESS1 : PCI_ROM_ADDRESS;
	struct resource *r = &dev->resource[PCI_ROM_RESOURCE];
	unsigned long rom_size = r->end - r->start + 1;

	if (allocate_resource(&iomem_resource, r, rom_size, 0xf0000000, ~0, rom_size) < 0) {
		printk(KERN_ERR "PCI: Unable to find free space for expansion ROM of device %s (0x%lx bytes)\n",
		       dev->name, rom_size);
		r->start = 0;
		r->end = rom_size - 1;
	} else {
		DBG("PCI: Assigned address %08lx to expansion ROM of %s (0x%lx bytes)\n", r->start, dev->name, rom_size);
		pci_write_config_dword(dev, reg, r->start | PCI_ROM_ADDRESS_ENABLE);
		r->flags |= PCI_ROM_ADDRESS_ENABLE;
	}
}

/*
 * Several buggy motherboards address only 16 devices and mirror
 * them to next 16 IDs. We try to detect this `feature' on all
 * primary busses (those containing host bridges as they are
 * expected to be unique) and remove the ghost devices.
 */

static void __init pcibios_fixup_ghosts(struct pci_bus *b)
{
	struct pci_dev *d, *e, **z;
	int mirror = PCI_DEVFN(16,0);
	int seen_host_bridge = 0;
	int i;

	DBG("PCI: Scanning for ghost devices on bus %d\n", b->number);
	for(d=b->devices; d && d->devfn < mirror; d=d->sibling) {
		if ((d->class >> 8) == PCI_CLASS_BRIDGE_HOST)
			seen_host_bridge++;
		for(e=d->next; e; e=e->sibling) {
			if (e->devfn != d->devfn + mirror ||
			    e->vendor != d->vendor ||
			    e->device != d->device ||
			    e->class != d->class)
				continue;
			for(i=0; i<PCI_NUM_RESOURCES; i++)
				if (e->resource[i].start != d->resource[i].start ||
				    e->resource[i].end != d->resource[i].end ||
				    e->resource[i].flags != d->resource[i].flags)
					continue;
			break;
		}
		if (!e)
			return;
	}
	if (!seen_host_bridge)
		return;
	printk("PCI: Ignoring ghost devices on bus %d\n", b->number);
	for(e=b->devices; e->sibling != d; e=e->sibling);
	e->sibling = NULL;
	for(z=&pci_devices; (d=*z);)
		if (d->bus == b && d->devfn >= mirror) {
			*z = d->next;
			kfree_s(d, sizeof(*d));
		} else
			z = &d->next;
}

/*
 * In case there are peer host bridges, scan bus behind each of them.
 * Although several sources claim that the host bridges should have
 * header type 1 and be assigned a bus number as for PCI2PCI bridges,
 * the reality doesn't pass this test and the bus number is usually
 * set by BIOS to the first free value.
 */
static void __init pcibios_fixup_peer_bridges(void)
{
	struct pci_bus *b = pci_root;
	int n, cnt=-1;
	struct pci_dev *d;
	struct pci_ops *ops = pci_root->ops;

#ifdef CONFIG_VISWS
	pci_scan_bus(1, ops, NULL);
	return;
#endif

#ifdef CONFIG_PCI_DIRECT
	/*
	 * Don't search for peer host bridges if we use config type 2
	 * since it reads bogus values for non-existent busses and
	 * chipsets supporting multiple primary busses use conf1 anyway.
	 */
	if (ops == &pci_direct_conf2)
		return;
#endif

	for(d=b->devices; d; d=d->sibling)
		if ((d->class >> 8) == PCI_CLASS_BRIDGE_HOST)
			cnt++;
	n = b->subordinate + 1;
	while (n <= 0xff) {
		int found = 0;
		u16 l;
		struct pci_bus bus;
		struct pci_dev dev;
		bus.number = n;
		bus.ops = ops;
		dev.bus = &bus;
		for(dev.devfn=0; dev.devfn<256; dev.devfn += 8)
			if (!pci_read_config_word(&dev, PCI_VENDOR_ID, &l) &&
			    l != 0x0000 && l != 0xffff) {
#ifdef CONFIG_PCI_BIOS
				if (pci_bios_present) {
					int err, idx = 0;
					u8 bios_bus, bios_dfn;
					u16 d;
					pci_read_config_word(&dev, PCI_DEVICE_ID, &d);
					DBG("BIOS test for %02x:%02x (%04x:%04x)\n", n, dev.devfn, l, d);
					while (!(err = pci_bios_find_device(l, d, idx, &bios_bus, &bios_dfn)) &&
					       (bios_bus != n || bios_dfn != dev.devfn))
						idx++;
					if (err)
						break;
				}
#endif
				DBG("Found device at %02x:%02x\n", n, dev.devfn);
				found++;
				if (!pci_read_config_word(&dev, PCI_CLASS_DEVICE, &l) &&
				    l == PCI_CLASS_BRIDGE_HOST)
					cnt++;
			}
		if (cnt-- <= 0)
			break;
		if (found) {
			printk("PCI: Discovered primary peer bus %02x\n", n);
			b = pci_scan_bus(n, ops, NULL);
			n = b->subordinate;
		}
		n++;
	}
}

/*
 * Exceptions for specific devices. Usually work-arounds for fatal design flaws.
 */

static void __init pci_fixup_i450nx(struct pci_dev *d)
{
	/*
	 * i450NX -- Find and scan all secondary buses on all PXB's.
	 */
	int pxb, reg;
	u8 busno, suba, subb;
	printk("PCI: Searching for i450NX host bridges on %s\n", d->name);
	reg = 0xd0;
	for(pxb=0; pxb<2; pxb++) {
		pci_read_config_byte(d, reg++, &busno);
		pci_read_config_byte(d, reg++, &suba);
		pci_read_config_byte(d, reg++, &subb);
		DBG("i450NX PXB %d: %02x/%02x/%02x\n", pxb, busno, suba, subb);
		if (busno)
			pci_scan_bus(busno, pci_root->ops, NULL);	/* Bus A */
		if (suba < subb)
			pci_scan_bus(suba+1, pci_root->ops, NULL);	/* Bus B */
	}
	pci_probe |= PCI_NO_PEER_FIXUP;
}

static void __init pci_fixup_i440bx(struct pci_dev *d)
{
#if 0							    /* Temporarily disabled   FIXME */
	/*
	 * i440BX/ZX -- Occupy the AGP bridge windows.
	 */
	u16 a, b;
	u8 u, v;
	pci_read_config_byte(d, 0x1c, &u);
	pci_read_config_byte(d, 0x1d, &v);
	if (v >= u) {
		a = u<<8;
		b = ((v-u)<<8) + 0x100;
		occupy_region(a, a+b, b, 1, &d->dev);
	}
	for (u = 0; u < 2; u++) {
		pci_read_config_word(d, 0x20+(u*4), &a);
		pci_read_config_word(d, 0x22+(u*4), &b);
		if (b >= a) {
			u32 m = a<<16;
			u32 n = ((b-a)<<16) + 0x100000;
			occupy_mem_region(m, m+n, n, 1, &d->dev);
		}
	}
#endif
}

static void __init pci_fixup_umc_ide(struct pci_dev *d)
{
	/*
	 * UM8886BF IDE controller sets region type bits incorrectly,
	 * therefore they look like memory despite of them being I/O.
	 */
	int i;

	printk("PCI: Fixing base address flags for device %s\n", d->name);
	for(i=0; i<4; i++)
		d->resource[i].flags |= PCI_BASE_ADDRESS_SPACE_IO;
}

struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82451NX,	pci_fixup_i450nx },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82443BX_1,	pci_fixup_i440bx },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_UMC,	PCI_DEVICE_ID_UMC_UM8886BF,	pci_fixup_umc_ide },
	{ 0 }
};

/*
 * Allocate resources for all PCI devices. We need to do that before
 * we try to fix up anything.
 */

static void __init pcibios_claim_resources(void)
{
	struct pci_dev *dev;
	int idx;

	for (dev=pci_devices; dev; dev=dev->next)
		for (idx = 0; idx < PCI_NUM_RESOURCES; idx++) {
			struct resource *r = &dev->resource[idx];
			if (!r->start)
				continue;
			if (request_resource((r->flags & PCI_BASE_ADDRESS_SPACE_IO) ? &ioport_resource : &iomem_resource, r) < 0)
				printk(KERN_ERR "PCI: Address space collision on region %d of device %s\n", idx, dev->name);
				/* We probably should disable the region, shouldn't we? */
		}
}

/*
 * Fix base addresses, I/O and memory enables and IRQ's (mostly work-arounds
 * for buggy PCI BIOS'es :-[).
 */

extern int skip_ioapic_setup;

static void __init pcibios_fixup_devices(void)
{
	struct pci_dev *dev;
	int i, has_io, has_mem;
	unsigned short cmd;

	for(dev = pci_devices; dev; dev=dev->next) {
		/*
		 * There are buggy BIOSes that forget to enable I/O and memory
		 * access to PCI devices. We try to fix this, but we need to
		 * be sure that the BIOS didn't forget to assign an address
		 * to the device. [mj]
		 */
		has_io = has_mem = 0;
		for(i=0; i<6; i++) {
			struct resource *r = &dev->resource[i];
			if (r->flags & PCI_BASE_ADDRESS_SPACE_IO) {
				has_io = 1;
				if (!r->start || r->start == PCI_BASE_ADDRESS_IO_MASK)
					pcibios_fixup_io_addr(dev, i);
			} else if (r->start)
				has_mem = 1;
		}
		/*
		 * Don't enable VGA-compatible cards since they have
		 * fixed I/O and memory space.
		 * 
		 * Don't enabled disabled IDE interfaces either because
		 * some BIOSes may reallocate the same address when they
		 * find that no devices are attached. 
		 */
		if (((dev->class >> 8) != PCI_CLASS_DISPLAY_VGA) &&
		    ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE)) {
			pci_read_config_word(dev, PCI_COMMAND, &cmd);
			if (has_io && !(cmd & PCI_COMMAND_IO)) {
				printk("PCI: Enabling I/O for device %s\n", dev->name);
				cmd |= PCI_COMMAND_IO;
				pci_write_config_word(dev, PCI_COMMAND, cmd);
			}
			if (has_mem && !(cmd & PCI_COMMAND_MEMORY)) {
				printk("PCI: Enabling memory for device %s\n", dev->name);
				cmd |= PCI_COMMAND_MEMORY;
				pci_write_config_word(dev, PCI_COMMAND, cmd);
			}
		}
		/*
		 * Assign address to expansion ROM if requested.
		 */
		if ((pci_probe & PCI_ASSIGN_ROMS) && dev->resource[PCI_ROM_RESOURCE].end)
			pcibios_fixup_rom_addr(dev);
#if defined(CONFIG_X86_IO_APIC)
		/*
		 * Recalculate IRQ numbers if we use the I/O APIC
		 */
		if(!skip_ioapic_setup)
		{
			int irq;
			unsigned char pin;

			pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
			if (pin) {
				pin--;		/* interrupt pins are numbered starting from 1 */
				irq = IO_APIC_get_PCI_irq_vector(dev->bus->number, PCI_SLOT(dev->devfn), pin);
				if (irq < 0 && dev->bus->parent) { /* go back to the bridge */
					struct pci_dev * bridge = dev->bus->self;

					pin = (pin + PCI_SLOT(dev->devfn)) % 4;
					irq = IO_APIC_get_PCI_irq_vector(bridge->bus->number, 
							PCI_SLOT(bridge->devfn), pin);
					if (irq >= 0)
						printk(KERN_WARNING "PCI: using PPB(B%d,I%d,P%d) to get irq %d\n", 
							bridge->bus->number, PCI_SLOT(bridge->devfn), pin, irq);
				}
				if (irq >= 0) {
					printk("PCI->APIC IRQ transform: (B%d,I%d,P%d) -> %d\n",
						dev->bus->number, PCI_SLOT(dev->devfn), pin, irq);
					dev->irq = irq;
				}
			}
		}
#endif
		/*
		 * Fix out-of-range IRQ numbers
		 */
		if (dev->irq >= NR_IRQS)
			dev->irq = 0;
	}
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __init pcibios_fixup_bus(struct pci_bus *b)
{
	pcibios_fixup_ghosts(b);
}

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space, but we still keep BIOS order of cards to be
 * compatible with 2.0.X. This should go away some day.
 */

void __init pcibios_init(void)
{
	struct pci_ops *bios = NULL;
	struct pci_ops *dir = NULL;
	struct pci_ops *ops;

#ifdef CONFIG_PCI_BIOS
	if ((pci_probe & PCI_PROBE_BIOS) && ((bios = pci_find_bios()))) {
		pci_probe |= PCI_BIOS_SORT;
		pci_bios_present = 1;
	}
#endif
#ifdef CONFIG_PCI_DIRECT
	if (pci_probe & (PCI_PROBE_CONF1 | PCI_PROBE_CONF2))
		dir = pci_check_direct();
#endif
	if (dir)
		ops = dir;
	else if (bios)
		ops = bios;
	else {
		printk("PCI: No PCI bus detected\n");
		return;
	}

	printk("PCI: Probing PCI hardware\n");
	pci_scan_bus(0, ops, NULL);

	if (!(pci_probe & PCI_NO_PEER_FIXUP))
		pcibios_fixup_peer_bridges();
	pcibios_claim_resources();
	pcibios_fixup_devices();

#ifdef CONFIG_PCI_BIOS
	if ((pci_probe & PCI_BIOS_SORT) && !(pci_probe & PCI_NO_SORT))
		pcibios_sort();
#endif
}

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	}
#ifdef CONFIG_PCI_BIOS
	else if (!strcmp(str, "bios")) {
		pci_probe = PCI_PROBE_BIOS;
		return NULL;
	} else if (!strcmp(str, "nobios")) {
		pci_probe &= ~PCI_PROBE_BIOS;
		return NULL;
	} else if (!strcmp(str, "nosort")) {
		pci_probe |= PCI_NO_SORT;
		return NULL;
	}
#endif
#ifdef CONFIG_PCI_DIRECT
	else if (!strcmp(str, "conf1")) {
		pci_probe = PCI_PROBE_CONF1 | PCI_NO_CHECKS;
		return NULL;
	}
	else if (!strcmp(str, "conf2")) {
		pci_probe = PCI_PROBE_CONF2 | PCI_NO_CHECKS;
		return NULL;
	}
#endif
	else if (!strcmp(str, "nopeer")) {
		pci_probe |= PCI_NO_PEER_FIXUP;
		return NULL;
	} else if (!strcmp(str, "rom")) {
		pci_probe |= PCI_ASSIGN_ROMS;
		return NULL;
	}
	return str;
}

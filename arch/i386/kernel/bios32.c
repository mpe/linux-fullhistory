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
 * Copyright 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
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
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>

#include <asm/page.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/spinlock.h>

#include "irq.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */

spinlock_t pci_lock = SPIN_LOCK_UNLOCKED;

/*
 * Generic PCI access -- indirect calls according to detected HW.
 */

struct pci_access {
    int pci_present;
    int (*read_config_byte)(unsigned char, unsigned char, unsigned char, unsigned char *);
    int (*read_config_word)(unsigned char, unsigned char, unsigned char, unsigned short *);
    int (*read_config_dword)(unsigned char, unsigned char, unsigned char, unsigned int *);
    int (*write_config_byte)(unsigned char, unsigned char, unsigned char, unsigned char);
    int (*write_config_word)(unsigned char, unsigned char, unsigned char, unsigned short);
    int (*write_config_dword)(unsigned char, unsigned char, unsigned char, unsigned int);
};

static int pci_stub(void)
{
	return PCIBIOS_FUNC_NOT_SUPPORTED;
}

static struct pci_access pci_access_none = {
	0,		   		/* No PCI present */
	(void *) pci_stub,
	(void *) pci_stub,
	(void *) pci_stub,
	(void *) pci_stub,
	(void *) pci_stub,
	(void *) pci_stub
};

static struct pci_access *access_pci = &pci_access_none;

int pcibios_present(void)
{
	return access_pci->pci_present;
}

#define PCI_byte_BAD 0
#define PCI_word_BAD (pos & 1)
#define PCI_dword_BAD (pos & 3)

#define PCI_STUB(rw,size,type) \
int pcibios_##rw##_config_##size (u8 bus, u8 dfn, u8 pos, type value) \
{									\
	int res;							\
	unsigned long flags;						\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irqsave(&pci_lock, flags);				\
	res = access_pci->rw##_config_##size(bus, dfn, pos, value);	\
	spin_unlock_irqrestore(&pci_lock, flags);			\
	return res;							\
}

PCI_STUB(read, byte, u8 *)
PCI_STUB(read, word, u16 *)
PCI_STUB(read, dword, u32 *)
PCI_STUB(write, byte, u8)
PCI_STUB(write, word, u16)
PCI_STUB(write, dword, u32)

#define PCI_PROBE_BIOS 1
#define PCI_PROBE_CONF1 2
#define PCI_PROBE_CONF2 4
#define PCI_NO_SORT 0x100
#define PCI_BIOS_SORT 0x200
#define PCI_NO_CHECKS 0x400

static unsigned int pci_probe = PCI_PROBE_BIOS | PCI_PROBE_CONF1 | PCI_PROBE_CONF2;

/*
 * Direct access to PCI hardware...
 */

#ifdef CONFIG_PCI_DIRECT

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */

#define CONFIG_CMD(bus, device_fn, where)   (0x80000000 | (bus << 16) | (device_fn << 8) | (where & ~3))

static int pci_conf1_read_config_byte(unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned char *value)
{
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    *value = inb(0xCFC + (where&3));
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_read_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short *value)
{
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);    
    *value = inw(0xCFC + (where&2));
    return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_read_config_dword (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned int *value)
{
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    *value = inl(0xCFC);
    return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_write_config_byte (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned char value)
{
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);    
    outb(value, 0xCFC + (where&3));
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_word (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned short value)
{
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    outw(value, 0xCFC + (where&2));
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_dword (unsigned char bus, unsigned char device_fn, 
				  unsigned char where, unsigned int value)
{
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    outl(value, 0xCFC);
    return PCIBIOS_SUCCESSFUL;
}

#undef CONFIG_CMD

static struct pci_access pci_direct_conf1 = {
      1,
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
    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inb(IOADDR(device_fn,where));
    outb (0, 0xCF8);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_word (unsigned char bus, unsigned char device_fn, 
				unsigned char where, unsigned short *value)
{
    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inw(IOADDR(device_fn,where));
    outb (0, 0xCF8);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_dword (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned int *value)
{
    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inl (IOADDR(device_fn,where));    
    outb (0, 0xCF8);    
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_byte (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned char value)
{
    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outb (value, IOADDR(device_fn,where));
    outb (0, 0xCF8);    
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_word (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned short value)
{
    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outw (value, IOADDR(device_fn,where));
    outb (0, 0xCF8);    
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_dword (unsigned char bus, unsigned char device_fn, 
				  unsigned char where, unsigned int value)
{
    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outl (value, IOADDR(device_fn,where));    
    outb (0, 0xCF8);    
    return PCIBIOS_SUCCESSFUL;
}

#undef IOADDR
#undef FUNC

static struct pci_access pci_direct_conf2 = {
      1,
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
__initfunc(int pci_sanity_check(struct pci_access *a))
{
	u16 dfn, x;

#ifdef CONFIG_VISWS
	return 1;       /* Lithium PCI Bridges are non-standard */
#endif

	if (pci_probe & PCI_NO_CHECKS)
		return 1;
	for(dfn=0; dfn < 0x100; dfn++)
		if ((!a->read_config_word(0, dfn, PCI_CLASS_DEVICE, &x) &&
		     (x == PCI_CLASS_BRIDGE_HOST || x == PCI_CLASS_DISPLAY_VGA)) ||
		    (!a->read_config_word(0, dfn, PCI_VENDOR_ID, &x) &&
		     (x == PCI_VENDOR_ID_INTEL || x == PCI_VENDOR_ID_COMPAQ)))
			return 1;
	DBG("PCI: Sanity check failed\n");
	return 0;
}

__initfunc(static struct pci_access *pci_check_direct(void))
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

	spin_lock_irqsave(&pci_lock, flags);
	__asm__("lcall (%%edi)"
		: "=a" (return_code),
		  "=b" (address),
		  "=c" (length),
		  "=d" (entry)
		: "0" (service),
		  "1" (0),
		  "D" (&bios32_indirect));
	spin_unlock_irqrestore(&pci_lock, flags);

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

__initfunc(static int check_pcibios(void))
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

__initfunc(static int pci_bios_find_device (unsigned short vendor, unsigned short device_id,
	unsigned short index, unsigned char *bus, unsigned char *device_fn))
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

static int pci_bios_read_config_byte(unsigned char bus,
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

static int pci_bios_read_config_word (unsigned char bus,
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

static int pci_bios_read_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int *value)
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

static int pci_bios_write_config_byte (unsigned char bus,
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

static int pci_bios_write_config_word (unsigned char bus,
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

static int pci_bios_write_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int value)
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

/*
 * Function table for BIOS32 access
 */

static struct pci_access pci_bios_access = {
      1,
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

__initfunc(static struct pci_access *pci_find_bios(void))
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

__initfunc(void pcibios_sort(void))
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
 * Several BIOS'es forget to assign addresses to I/O ranges.
 * We try to fix it here, expecting there are free addresses
 * starting with 0x5800. Ugly, but until we come with better
 * resource management, it's the only simple solution.
 */

static int pci_last_io_addr __initdata = 0x5800;

__initfunc(void pcibios_fixup_io_addr(struct pci_dev *dev, int idx))
{
	unsigned short cmd;
	unsigned int reg = PCI_BASE_ADDRESS_0 + 4*idx;
	unsigned int size, addr, try;
	unsigned int bus = dev->bus->number;
	unsigned int devfn = dev->devfn;

	if (!pci_last_io_addr) {
		printk("PCI: Unassigned I/O space for %02x:%02x\n", bus, devfn);
		return;
	}
	if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE && idx < 4) {
		/*
		 * In case the BIOS didn't assign an address 0--3 to an IDE
		 * controller, we don't try to fix it as it means "use default
		 * addresses" at least with several broken chips and the IDE
		 * driver needs the original settings to recognize which devices
		 * correspond to the primary controller.
		 */
		return;
	}
	pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
	pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd & ~PCI_COMMAND_IO);
	pcibios_write_config_dword(bus, devfn, reg, ~0);
	pcibios_read_config_dword(bus, devfn, reg, &size);
	size = (~(size & PCI_BASE_ADDRESS_IO_MASK) & 0xffff) + 1;
	addr = 0;
	if (!size || size > 0x100)
		printk("PCI: Unable to handle I/O allocation for %02x:%02x (%04x), tell <mj@ucw.cz>\n", bus, devfn, size);
	else {
		do {
			addr = (pci_last_io_addr + size - 1) & ~(size-1);
			pci_last_io_addr = addr + size;
		} while (check_region(addr, size));
		printk("PCI: Assigning I/O space %04x-%04x to device %02x:%02x\n", addr, addr+size-1, bus, devfn);
		pcibios_write_config_dword(bus, devfn, reg, addr | PCI_BASE_ADDRESS_SPACE_IO);
		pcibios_read_config_dword(bus, devfn, reg, &try);
		if ((try & PCI_BASE_ADDRESS_IO_MASK) != addr) {
			addr = 0;
			printk("PCI: Address setup failed, got %04x\n", try);
		} else
			dev->base_address[idx] = try;
	}
	if (!addr) {
		pcibios_write_config_dword(bus, devfn, reg, 0);
		dev->base_address[idx] = 0;
	}
	pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
}

/*
 * Several buggy motherboards address only 16 devices and mirror
 * them to next 16 IDs. We try to detect this `feature' on all
 * primary busses (those containing host bridges as they are
 * expected to be unique) and remove the ghost devices.
 */

__initfunc(void pcibios_fixup_ghosts(struct pci_bus *b))
{
	struct pci_dev *d, *e, **z;
	int mirror = PCI_DEVFN(16,0);
	int seen_host_bridge = 0;

	DBG("PCI: Scanning for ghost devices on bus %d\n", b->number);
	for(d=b->devices; d && d->devfn < mirror; d=d->sibling) {
		if ((d->class >> 8) == PCI_CLASS_BRIDGE_HOST)
			seen_host_bridge++;
		for(e=d->next; e; e=e->sibling)
			if (e->devfn == d->devfn + mirror &&
			    e->vendor == d->vendor &&
			    e->device == d->device &&
			    e->class == d->class &&
			    !memcmp(e->base_address, d->base_address, sizeof(e->base_address)))
				break;
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
__initfunc(void pcibios_fixup_peer_bridges(void))
{
	struct pci_bus *b = &pci_root;
	int i, n, cnt=-1;
	struct pci_dev *d;

#ifdef CONFIG_PCI_DIRECT
	/*
	 * Don't search for peer host bridges if we use config type 2
	 * since it reads bogus values for non-existent busses and
	 * chipsets supporting multiple primary busses use conf1 anyway.
	 */
	if (access_pci == &pci_direct_conf2)
		return;
#endif
	for(d=b->devices; d; d=d->sibling)
		if ((d->class >> 8) == PCI_CLASS_BRIDGE_HOST)
			cnt++;
	n = b->subordinate + 1;
	while (n <= 0xff) {
		int found = 0;
		u16 l;
		for(i=0; i<256; i += 8)
			if (!pcibios_read_config_word(n, i, PCI_VENDOR_ID, &l) &&
			    l != 0x0000 && l != 0xffff) {
				DBG("Found device at %02x:%02x\n", n, i);
				found++;
				if (!pcibios_read_config_word(n, i, PCI_CLASS_DEVICE, &l) &&
				    l == PCI_CLASS_BRIDGE_HOST)
					cnt++;
			}
		if (cnt-- <= 0)
			break;
		if (found) {
			printk("PCI: Discovered primary peer bus %02x\n", n);
			b = kmalloc(sizeof(*b), GFP_KERNEL);
			memset(b, 0, sizeof(*b));
			b->next = pci_root.next;
			pci_root.next = b;
			b->number = b->secondary = n;
			b->subordinate = 0xff;
			b->subordinate = pci_scan_bus(b);
			n = b->subordinate;
		}
		n++;
	}
}

/*
 * Fix base addresses, I/O and memory enables and IRQ's (mostly work-arounds
 * for buggy PCI BIOS'es :-[).
 */

__initfunc(void pcibios_fixup_devices(void))
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
			unsigned long a = dev->base_address[i];
			if (a & PCI_BASE_ADDRESS_SPACE_IO) {
				has_io = 1;
				a &= PCI_BASE_ADDRESS_IO_MASK;
				if (!a || a == PCI_BASE_ADDRESS_IO_MASK)
					pcibios_fixup_io_addr(dev, i);
			} else if (a & PCI_BASE_ADDRESS_MEM_MASK)
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
				printk("PCI: Enabling I/O for device %02x:%02x\n",
					dev->bus->number, dev->devfn);
				cmd |= PCI_COMMAND_IO;
				pci_write_config_word(dev, PCI_COMMAND, cmd);
			}
			if (has_mem && !(cmd & PCI_COMMAND_MEMORY)) {
				printk("PCI: Enabling memory for device %02x:%02x\n",
					dev->bus->number, dev->devfn);
				cmd |= PCI_COMMAND_MEMORY;
				pci_write_config_word(dev, PCI_COMMAND, cmd);
			}
		}
#if defined(CONFIG_X86_IO_APIC)
		/*
		 * Recalculate IRQ numbers if we use the I/O APIC
		 */
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
 * Arch-dependent fixups.
 */

__initfunc(void pcibios_fixup(void))
{
	pcibios_fixup_peer_bridges();
	pcibios_fixup_devices();

#ifdef CONFIG_PCI_BIOS
	if ((pci_probe & PCI_BIOS_SORT) && !(pci_probe & PCI_NO_SORT))
		pcibios_sort();
#endif
}

__initfunc(void pcibios_fixup_bus(struct pci_bus *b))
{
	pcibios_fixup_ghosts(b);
}

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space, but we still keep BIOS order of cards to be
 * compatible with 2.0.X. This should go away in 2.3.
 */

__initfunc(void pcibios_init(void))
{
	struct pci_access *bios = NULL;
	struct pci_access *dir = NULL;

#ifdef CONFIG_PCI_BIOS
	if ((pci_probe & PCI_PROBE_BIOS) && ((bios = pci_find_bios())))
		pci_probe |= PCI_BIOS_SORT;
#endif
#ifdef CONFIG_PCI_DIRECT
	if (pci_probe & (PCI_PROBE_CONF1 | PCI_PROBE_CONF2))
		dir = pci_check_direct();
#endif
	if (dir)
		access_pci = dir;
	else if (bios)
		access_pci = bios;
}

#if !defined(CONFIG_PCI_BIOS) && !defined(CONFIG_PCI_DIRECT)
#error PCI configured with neither PCI BIOS or PCI direct access support.
#endif

__initfunc(char *pcibios_setup(char *str))
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	} else if (!strncmp(str, "io=", 3)) {
		char *p;
		unsigned int x = simple_strtoul(str+3, &p, 16);
		if (p && *p)
			return str;
		pci_last_io_addr = x;
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
	return str;
}

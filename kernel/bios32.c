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
 * Jun 17, 1994 : Modified to accomodate the broken pre-PCI BIOS SPECIFICATION
 *	Revision 2.0 present on <thys@dennis.ee.up.ac.za>'s ASUS mainboard.
 */

#include <linux/kernel.h>
#include <linux/segment.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#define PCIBIOS_PCI_FUNCTION_ID 	0xb1
#define PCIBIOS_PCI_BIOS_PRESENT 	0x01
#define PCIBIOS_FIND_PCI_DEVICE		0x02
#define PCIBIOS_FIND_PCI_CLASS_CODE	0x03
#define PCIBIOS_GENERATE_SPECIAL_CYCLE	0x06
#define PCIBIOS_READ_CONFIG_BYTE	0x08		
#define PCIBIOS_READ_CONFIG_WORD	0x09
#define PCIBIOS_READ_CONFIG_DWORD	0x0a
#define PCIBIOS_WRITE_CONFIG_BYTE	0x0b
#define PCIBIOS_WRITE_CONFIG_WORD	0x0c
#define PCIBIOS_WRITE_CONFIG_DWORD	0x0d


union signature {
    char chars[4];
    unsigned long scalar;
};

/*
 * This is the standard structure used to identify the entry point 
 * to the BIOS32 Service Directory, as documented in 
 * 	Standard BIOS 32-bit Service Directory Proposal
 * 	Revision 0.4 May 24, 1993
 * 	Phoenix Technologies Ltd.
 *	Norwood, MA
 * and the PCI BIOS specfication.
 */

static union signature bios32_signature = {"_32_",};

union bios32 {
    struct {
	union signature signature;	/* _32_ */
	long entry;			/* 32 bit physical address */
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

static long bios32_entry = 0;
static unsigned char bios32_indirect[6]; 

static long pcibios_init (long, long);
 
long bios32_init (long memory_start, long memory_end) {
    union bios32 *check;
    unsigned char sum;
    int i, length;

    /*
     * Follow the standard procedure for locating the BIOS32 Serivce
     * directory by scanning the permissable address range from 
     * 0xe0000 through 0xfffff for a valid BIOS32 structure.
     */

    for (check = (union bios32 *) 0xe0000; check <= (union bios32 *) 0xffff0; 
	++check) {
	if (check->fields.signature.scalar == bios32_signature.scalar) {
	    for (i = sum = 0, length = check->fields.length * 16;
		i < check->fields.length * 16; ++i) 
		sum += check->chars[i];
	    if (sum != 0)
		continue;
	    if (check->fields.revision != 0) {
		printk ("bios32_init : unsupported revision %d at 0x%x, mail drew@colorado.edu\n",
		    check->fields.revision, (unsigned) check);
		continue;
	    }
	    printk ("bios32_init : BIOS32 Service Directory structure at 0x%x\n", 
		(unsigned) check);
	    if (!bios32_entry) {
		*((long *) bios32_indirect) = bios32_entry = check->fields.entry;
		*((short *) (bios32_indirect + 4)) = KERNEL_CS;
		printk ("bios32_init : BIOS32 Service Directory entry at 0x%x\n", 
		    (unsigned) bios32_entry);
	    } else 
		printk ("bios32_init : multiple entries, mail drew@colorado.edu\n");
	}
    }
    if (bios32_entry) {
	memory_start = pcibios_init (memory_start, memory_end);
    }
    return memory_start;
}

/* 
 * Returns the entry point for the given service, NULL on error 
 */

static long bios32_service (long service) {
    unsigned char return_code;	/* %al */
    long address;		/* %ebx */
    long length;		/* %ecx */
    long entry;			/* %edx */

    __asm__ ("
	movl $0, %%ebx
	lcall (%%edi)
" 
	: "=al" (return_code), "=ebx" (address), "=ecx" (length), "=edx" (entry)
    	: "eax" (service), "D" (bios32_indirect) 
	: "ebx"
    );

    switch (return_code) {
    case 0:	
	return address + entry;
    case 0x80:	/* Not present */
	printk ("bios32_service(%ld) : not present\n", service);
	return 0;
    default: /* Shouldn't happen */
	printk ("bios32_service(%ld) : returned 0x%x, mail drew@colorado.edu\n",
	    service, (int) return_code );
	return 0;
    }

}

static union signature pci_signature = {"PCI ",};
static union signature pci_service = {"$PCI",};
static long pcibios_entry = 0;
static unsigned char pci_indirect[6];

void NCR53c810_test(void);

static long pcibios_init (long memory_start, long memory_end) {
    union signature signature;
    unsigned char present_status;
    unsigned char major_revision;
    unsigned char minor_revision;
    int pack;

    if ((pcibios_entry = bios32_service (pci_service.scalar))) {
	*((long *) pci_indirect) = pcibios_entry;
	*((short *) (pci_indirect + 4)) = KERNEL_CS;
	__asm__ ("
	    movw $0xb101, %%eax
	    lcall (%%edi)
	    jc 1f
	    xor %%ah, %%ah
1:
	    shl $8, %%eax;
	    movw %%bx, %%ax;
" 		
	    : "=edx" (signature.scalar), "=eax" (pack)
	    : "D" (pci_indirect) 
	    : "cx"
	);

	present_status = (pack >> 16) & 0xff;
	major_revision = (pack >> 8) & 0xff;
	minor_revision = pack & 0xff;
	if (present_status || (signature.scalar != pci_signature.scalar)) {
	    printk ("pcibios_init : %s : BIOS32 Service Directory says PCI BIOS is present,\n"
		    "    but PCI_BIOS_PRESENT subfunction fails with present status of 0x%x\n"
		    "    and signature of 0x%08lx (%c%c%c%c).  mail drew@Colorado.EDU\n", 
		    (signature.scalar == pci_signature.scalar) ?  "WARNING" : "ERROR",
		    (int) present_status, signature.scalar , signature.chars[0],
		    signature.chars[1], signature.chars[2], signature.chars[3]);

	    if (signature.scalar != pci_signature.scalar) 
		pcibios_entry = 0;
	} 
	if (pcibios_entry) {
	    printk ("pcibios_init : PCI BIOS revision %x.%02x entry at 0x%lx\n",
		(int) major_revision, (int) minor_revision, pcibios_entry);
	}
    }

#if 0
    NCR53c810_test(); 
#endif
    return memory_start;
}

int pcibios_present (void) {
    return pcibios_entry ? 1 : 0;
}

int pcibios_find_class_code (unsigned long class_code, unsigned short index, 
	unsigned char *bus, unsigned char *device_fn) {
	unsigned short bx;
	unsigned short ret;
	__asm__ ("
	    movw $0xb103, %%ax
	    lcall (%%edi)
	    jc 1f
	    xor %%ah, %%ah
1:
" 
	    : "=bx" (bx), "=ax" (ret)
	    : "cx" (class_code), "S" ((int) index), "D" (pci_indirect)
	);
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}


int pcibios_find_device (unsigned short vendor, unsigned short device_id, 
	unsigned short index, unsigned char *bus, unsigned char *device_fn) {
	unsigned short bx;
	unsigned short ret;
	__asm__ ("
	    movw $0xb102, %%ax
	    lcall (%%edi)
	    jc 1f
	    xor %%ah, %%ah
1:
" 
	    : "=bx" (bx), "=ax" (ret)
	    : "cx" (device_id), "dx" (vendor), "S" ((int) index), "D" (pci_indirect)
	);
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}

int pcibios_read_config_byte (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned char *value) {
    unsigned long ret;
    unsigned short bx = (bus << 8) | device_fn;
    __asm__ ("
	movw $0xb108, %%ax
	lcall (%%esi)
	jc 1f
	xor %%ah, %%ah
1:
"  
    	: "=cl" (*value), "=eax" (ret)  
    	: "bx" (bx), "D" ((long) where), "S" (pci_indirect)
    );
    return (int) (ret & 0xff00) >> 8;
}

int pcibios_read_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short *value) {
    unsigned short ret;
    unsigned short bx = (bus << 8) | device_fn;
    __asm__ ("
	movw $0xb109, %%ax
	lcall (%%esi)
	jc 1f
	xor %%ah, %%ah
1:
"  
    	: "=cx" (*value), "=ax" (ret)  
    	: "bx" (bx), "D" ((long) where), "S" (pci_indirect)
    );
    return (int) (ret & 0xff00) >> 8;
}

int pcibios_read_config_dword (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned long *value) {
    unsigned short ret;
    unsigned short bx = (bus << 8) | device_fn;
    __asm__ ("
	movw $0xb10a, %%ax
	lcall (%%esi)
	jc 1f
	xor %%ah, %%ah
1:
"  
    	: "=ecx" (*value), "=ax" (ret)  
    	: "bx" (bx), "D" ((long) where), "S" (pci_indirect)
    );
    return (int) (ret & 0xff00) >> 8;
}

int pcibios_write_config_byte (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned char value) {
    unsigned short ret;
    unsigned short bx = (bus << 8) | device_fn;
    __asm__ ("
	movw $0xb108, %%ax
	lcall (%%esi)
	jc 1f
	xor %%ah, %%ah
1:
"  
    	: "=ax" (ret)  
    	: "cl" (value), "bx" (bx), "D" ((long) where), "S" (pci_indirect)
    );
    return (int) (ret & 0xff00) >> 8;
}

int pcibios_write_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short value) {
    unsigned short ret;
    unsigned short bx = (bus << 8) | device_fn;
    __asm__ ("
	movw $0xb109, %%ax
	lcall (%%esi)
	jc 1f
	xor %%ah, %%ah
1:
"  
    	: "=ax" (ret)  
    	: "cx" (value), "bx" (bx), "D" ((long) where), "S" (pci_indirect)
    );
    return (int) (ret & 0xff00) >> 8;
}

int pcibios_write_config_dword (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned long value) {
    unsigned short ret;
    unsigned short bx = (bus << 8) | device_fn;
    __asm__ ("
	movw $0xb10a, %%ax
	lcall (%%esi)
	jc 1f
	xor %%ah, %%ah
1:
"  
    	: "=ax" (ret)  
    	: "ecx" (value), "bx" (bx), "D" ((long) where), "S" (pci_indirect)
    );
    return (int) (ret & 0xff00) >> 8;
}

void NCR53c810_test(void) {
    unsigned char bus, device_fn;
    unsigned short index;
    int ret;
    unsigned char row, col;
    unsigned long val;

    for (index = 0; index < 4; ++index) {
	ret = pcibios_find_device ((unsigned short) PCI_VENDOR_ID_NCR, (unsigned short)
		PCI_DEVICE_ID_NCR_53C810, index, &bus, &device_fn);
	if (ret) 
	    break;
	printk ("ncr53c810 : at PCI bus %d, device %d, function %d.",
	    (int) bus, (int) ((device_fn & 0xf8) >> 3), (int) (device_fn & 7));
	for (row = 0; row < 0x3c; row += 0x10) {
	    printk ("\n    reg 0x%02x    ", row);
	    for (col = 0; col < 0x10; col += 4) {
		if (!(ret = pcibios_read_config_dword (bus, device_fn, row+col, &val))) 
		    printk ("0x%08lx      ", val);
		else
		    printk ("error 0x%02x ", ret);
	    }
	}
	printk ("\n");
    }
}

char *pcibios_strerror (int error) {
    static char buf[80];
    switch (error) {
    case PCIBIOS_SUCCESFUL:
        return "SUCCESFUL";
    case PCIBIOS_FUNC_NOT_SUPPORTED:
        return "FUNC_NOT_SUPPORTED";
    case PCIBIOS_BAD_VENDOR_ID:
        return "BAD_VENDOR_ID";
    case PCIBIOS_DEVICE_NOT_FOUND:
        return "DEVICE_NOT_FOUND";
    case PCIBIOS_BAD_REGISTER_NUMBER:
        return "BAD_REGISTER_NUMBER";
    default:
	sprintf (buf, "UNKNOWN RETURN 0x%x", error);
	return buf;
    }
}

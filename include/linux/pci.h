/*
 * PCI defines and function prototypes
 * Copyright 1994, Drew Eckhardt
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
 */

#ifndef PCI_H
#define PCI_H

/* Configuration method #1 */
#define PCI_CONFIG1_ADDRESS_REG  0xcf8
#define PCI_CONFIG1_ENABLE 0x80000000
#define PCI_CONFIG1_TUPPLE (bus, device, function, register)	\
        (PCI_CONFIG1_ENABLE | ((bus) << 16) & 0xff0000 |	\
        ((device) << 11) & 0xf800 | ((function) << 8) & 0x700 | \
        ((register) << 2) & 0xfc)
#define PCI_CONFIG1_DATA_REG     0xcfc

/* Configuration method #2, deprecated */
#define PCI_CONFIG2_ENABLE_REG	0xcf8
#define PCI_CONFIG2_ENABLE	0xf0
#define PCI_CONFIG2_TUPPLE (function)				\
	(PCI_CONFIG2_ENABLE | ((function) << 1) & 0xe)
#define PCI_CONFIG2_FORWARD_REG	0xcfa

/*
 * Under PCI, each device has 256 bytes of configuration address space,
 * of which the first 64 bytes is standardized as follows : 
 */

#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND		0x04	/* 16 bits */
#define  PCI_COMMAND_IO		0x1	/* Enable response in I/O space */
#define  PCI_COMMAND_MEMORY	0x2	/* Enable response in I/O space */
#define  PCI_COMMAND_MASTER	0x4	/* Enable bus mastering */
#define  PCI_COMMAND_SPECIAL	0x8	/* Enable response to special cycles */
#define  PCI_COMMAND_INVALIDATE	0x10	/* Use memory write and invalidate */
#define  PCI_COMMAND_VGA_PALETTE	0x20	/* Enable palette snooping */
#define  PCI_COMMAND_PARITY	0x40	/* Enable parity checking */
#define  PCI_COMMAND_WAIT 	0x80	/* Enable address/data stepping */
#define  PCI_COMMAND_SERR	0x100	/* Enable SERR */
#define  PCI_COMMAND_FAST_BACK	0x200	/* Enable back-to-back writes */

#define PCI_STATUS		0x06	/* 16 bits */
#define  PCI_STATUS_FAST_BACK	0x80	/* Accept fast-back to back */
#define  PCI_STATUS_PARITY	0x100	/* Detected parity error */
#define  PCI_STATUS_DEVSEL_MASK	0x600	/* DEVSEL timing */
#define  PCI_STATUS_DEVSEL_FAST	0x000	
#define  PCI_STATUS_DEVSEL_MEDIUM 0x200
#define  PCI_STATUS_DEVESEL_SLOW 0x400
#define  PCI_STATUS_SIG_TARGET_ABORT 0x800 /* Set on target abort */
#define  PCI_STATUS_REC_TARGET_ABORT 0x1000 /* Master ack of " */
#define  PCI_STATUS_REC_MASTER_ABORT 0x2000 /* Set on master abort */
#define  PCI_STATUS_SIG_SYSTEM_ERROR 0x4000 /* Set when we drive SERR */
#define  PCI_STATUS_DETECTED_PARITY 0x8000 /* Set on parity error */

#define PCI_CLASS_REVISION	0x08	/* High 24 bits are class, low 8
					   revision */
#define PCI_CACHE_LINE_SIZE	0x0c	/* 8 bits */
#define PCI_LATENCY_TIMER	0x0d	/* 8 bits */
#define PCI_HEADER_TYPE		0x0e	/* 8 bits */
#define PCI_BIST		0x0f	/* 8 bits */
#define PCI_BIST_CODE_MASK	0x0f	/* Return result */
#define PCI_BIST_START		0x40	/* 1 to start BIST, 2 secs or less */
#define PCI_BIST_CAPABLE	0x80	/* 1 if BIST capable */

/*
 * Base adddresses specify locations in memory or I/O space.
 * Decoded size can be determined by writing a value of 
 * 0xffffffff to the register, and reading it back.  Only 
 * 1 bits are decoded.
 */

#define PCI_BASE_ADDRESS_0	0x10	/* 32 bits */
#define PCI_BASE_ADDRESS_1	0x14	/* 32 bits */
#define PCI_BASE_ADDRESS_2	0x18	/* 32 bits */
#define PCI_BASE_ADDRESS_3	0x1c	/* 32 bits */
#define PCI_BASE_ADDRESS_4	0x20	/* 32 bits */
#define PCI_BASE_ADDRESS_5	0x24	/* 32 bits */
#define  PCI_BASE_ADDRESS_SPACE	0x01	/* 0 = memory, 1 = I/O */
#define  PCI_BASE_ADDRESS_SPACE_IO 0x01
#define  PCI_BASE_ADDRESS_SPACE_MEMORY 0x00
#define  PCI_BASE_ADDRESS_MEM_TYPE_MASK 0x06
#define  PCI_BASE_ADDRESS_MEM_TYPE_32	0x00	/* 32 bit address */
#define  PCI_BASE_ADDRESS_MEM_TYPE_1M	0x02	/* Below 1M */
#define  PCI_BASE_ADDRESS_MEM_TYPE_64	0x04	/* 64 bit address */
#define  PCI_BASE_ADDRESS_MEM_MASK	~7
#define  PCI_BASE_ADDRESS_IO_MASK	~3
/* bit 1 is reserved if address_space = 1 */

/* 0x28-0x2f are reserved */
#define PCI_ROM_ADDRESS		0x30	/* 32 bits */
#define  PCI_ROM_ADDRESS_ENABLE	0x01	/* Write 1 to enable ROM,
					   bits 31..11 are address,
					   10..2 are reserved */
/* 0x34-0x3b are reserved */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_MAX_LAT		0x3f	/* 8 bits */

#define PCI_VENDOR_ID_NCR		0x1000
#define PCI_DEVICE_ID_NCR_53C810	0x0001
#define PCI_DEVICE_ID_NCR_53C820	0x0002
#define PCI_DEVICE_ID_NCR_53C825	0x0003

/* PCI BIOS */

extern int pcibios_present (void);

#define PCIBIOS_SUCCESFUL		0x00
#define PCIBIOS_FUNC_NOT_SUPPORTED	0x81
#define PCIBIOS_BAD_VENDOR_ID		0x83
#define PCIBIOS_DEVICE_NOT_FOUND	0x86
#define PCIBIOS_BAD_REGISTER_NUMBER	0x87

/*
 * The PCIBIOS calls all bit-field the device_function variable such that 
 * the bit fielding matches that of the bl register used in the actual
 * calls.
 */

extern int pcibios_find_class (unsigned long class_code, unsigned short index, 
    unsigned char *bus, unsigned char *device_fn);
extern int pcibios_find_device (unsigned short vendor, unsigned short device_id, 
    unsigned short index, unsigned char *bus, unsigned char *device_fn);
extern int pcibios_read_config_byte (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned char *value);
extern int pcibios_read_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short *value);
extern int pcibios_read_config_dword (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned long *value);
extern char *pcibios_strerror (int error);
extern int pcibios_write_config_byte (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned char value);
extern int pcibios_write_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short value);
extern pcibios_write_config_dword (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned long value);
#endif /* ndef PCI_H */

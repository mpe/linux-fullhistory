/*
 * BIOS32, PCI BIOS functions and defines
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

#ifndef BIOS32_H
#define BIOS32_H

extern int pcibios_present (void);
unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end);

#define PCIBIOS_SUCCESSFUL		0x00
#define PCIBIOS_FUNC_NOT_SUPPORTED	0x81
#define PCIBIOS_BAD_VENDOR_ID		0x83
#define PCIBIOS_DEVICE_NOT_FOUND	0x86
#define PCIBIOS_BAD_REGISTER_NUMBER	0x87

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
extern int pcibios_present (void);
extern int pcibios_write_config_byte (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned char value);
extern int pcibios_write_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short value);
extern pcibios_write_config_dword (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned long value);
extern char *pcibios_strerror (int error);
#endif /* ndef BIOS32_H */

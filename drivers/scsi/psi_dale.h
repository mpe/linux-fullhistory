/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2220I device driver for Linux.
 *
 * psi_dalei.h - Linux Host Driver for PCI-2220i EIDE Adapters
 *
 * Copyright (c) 1997-1999 Perceptive Solutions, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * Technical updates and product information at:
 *  http://www.psidisk.com
 *
 * Please send questions, comments, bug reports to:
 *  tech@psidisk.com Technical Support
 *
 ****************************************************************************/

/************************************************/
/*		Dale PCI setup							*/
/************************************************/
#define	VENDOR_PSI			0x1256
#define	DEVICE_DALE_1		0x4401		/* 'D1' */

/************************************************/
/*		Misc konstants							*/
/************************************************/
#define	DALE_MAXDRIVES			4
#define	SECTORSXFER				8
#define	BYTES_PER_SECTOR		512
#define	DEFAULT_TIMING_MODE		5

/************************************************/
/*		EEPROM locations						*/
/************************************************/
#define	DALE_FLASH_PAGE_SIZE	128				// number of bytes per page
#define	DALE_FLASH_SIZE			65536L

#define	DALE_FLASH_BIOS			0x00080000L		// BIOS base address
#define	DALE_FLASH_SETUP		0x00088000L		// SETUP PROGRAM base address offset from BIOS
#define	DALE_FLASH_RAID			0x00088400L		// RAID signature storage
#define	DALE_FLASH_FACTORY		0x00089000L		// FACTORY data base address offset from BIOS

#define	DALE_FLASH_BIOS_SIZE	32768U			// size of FLASH BIOS REGION

/************************************************/
/*		DALE Register address offsets			*/
/************************************************/
#define	REG_DATA						0x80
#define	REG_ERROR						0x84
#define	REG_SECTOR_COUNT				0x88
#define	REG_LBA_0						0x8C
#define	REG_LBA_8						0x90
#define	REG_LBA_16						0x94
#define	REG_LBA_24						0x98
#define	REG_STAT_CMD					0x9C
#define	REG_STAT_SEL					0xA0
#define	REG_FAIL						0xB0
#define	REG_ALT_STAT					0xB8
#define	REG_DRIVE_ADRS					0xBC

#define	DALE_DATA_SLOW					0x00040000L
#define	DALE_DATA_MODE2					0x00040000L
#define	DALE_DATA_MODE3					0x00050000L
#define	DALE_DATA_MODE4					0x00060000L
#define	DALE_DATA_MODE4P				0x00070000L

#define RTR_LOCAL_RANGE					0x000
#define RTR_LOCAL_REMAP					0x004
#define RTR_EXP_RANGE					0x010
#define RTR_EXP_REMAP					0x014
#define RTR_REGIONS						0x018
#define RTR_DM_MASK						0x01C
#define RTR_DM_LOCAL_BASE				0x020
#define RTR_DM_IO_BASE					0x024
#define RTR_DM_PCI_REMAP				0x028
#define RTR_DM_IO_CONFIG				0x02C
#define RTR_MAILBOX						0x040
#define RTR_LOCAL_DOORBELL				0x060
#define RTR_PCI_DOORBELL				0x064
#define RTR_INT_CONTROL_STATUS 			0x068
#define RTR_EEPROM_CONTROL_STATUS		0x06C

#define RTL_DMA0_MODE					0x00
#define RTL_DMA0_PCI_ADDR				0x04
#define RTL_DMA0_LOCAL_ADDR				0x08
#define RTL_DMA0_COUNT					0x0C
#define RTL_DMA0_DESC_PTR				0x10
#define RTL_DMA1_MODE					0x14
#define RTL_DMA1_PCI_ADDR				0x18
#define RTL_DMA1_LOCAL_ADDR				0x1C
#define RTL_DMA1_COUNT					0x20
#define RTL_DMA1_DESC_PTR				0x24
#define RTL_DMA_COMMAND_STATUS			0x28
#define RTL_DMA_ARB0					0x2C
#define RTL_DMA_ARB1					0x30

/************************************************/
/*		Dale Scratchpad locations				*/
/************************************************/
#define	DALE_CHANNEL_DEVICE_0			0		// device channel locations
#define	DALE_CHANNEL_DEVICE_1			1
#define	DALE_CHANNEL_DEVICE_2			2
#define	DALE_CHANNEL_DEVICE_3			3

#define	DALE_SCRATH_DEVICE_0			4		// device type codes
#define	DALE_SCRATH_DEVICE_1			5
#define DALE_SCRATH_DEVICE_2			6
#define	DALE_SCRATH_DEVICE_3			7

#define	DALE_RAID_0_STATUS				8
#define DALE_RAID_1_STATUS				9

#define	DALE_TIMING_MODE				12		// bus master timing mode (2, 3, 4, 5)
#define	DALE_NUM_DRIVES					13		// number of addressable drives on this board
#define	DALE_RAID_ON					14 		// RAID status On
#define	DALE_LAST_ERROR					15		// Last error code from BIOS

/************************************************/
/*		Dale cable select bits					*/
/************************************************/
#define	SEL_NONE						0x00
#define	SEL_1							0x01
#define	SEL_2							0x02

/************************************************/
/*		Programmable Interrupt Controller		*/
/************************************************/
#define	PIC1							0x20		// first 8259 base port address
#define	PIC2							0xA0		// second 8259 base port address
#define	INT_OCW1						1			// Operation Control Word 1: IRQ mask
#define	EOI								0x20		// non-specific end-of-interrupt

/************************************************/
/*		Device/Geometry controls				*/
/************************************************/
#define GEOMETRY_NONE	 			0x0				// No device
#define GEOMETRY_SET				0x1				// Geometry set
#define	GEOMETRY_LBA				0x2				// Geometry set in default LBA mode
#define	GEOMETRY_PHOENIX			0x3				// Geometry set in Pheonix BIOS compatibility mode

#define	DEVICE_NONE					0x0				// No device present
#define	DEVICE_INACTIVE				0x1				// device present but not registered active
#define	DEVICE_ATAPI				0x2				// ATAPI device (CD_ROM, Tape, Etc...)
#define	DEVICE_DASD_NONLBA			0x3				// Non LBA incompatible device
#define	DEVICE_DASD_LBA				0x4				// LBA compatible device

/************************************************/
/*		Setup Structure Definitions				*/
/************************************************/
typedef struct		// device setup parameters
	{
	UCHAR	geometryControl;	// geometry control flags
	UCHAR	device;				// device code
	USHORT	sectors;			// number of sectors per track
	USHORT	heads;				// number of heads
	USHORT	cylinders;			// number of cylinders for this device
	ULONG	blocks;				// number of blocks on device
	ULONG	realCapacity;		// number of real blocks on this device for drive changed testing
	} SETUP_DEVICE, *PSETUP_DEVICE;

typedef struct		// master setup structure
	{
	USHORT			startupDelay;
	BOOL			promptBIOS;
	BOOL			fastFormat;
	BOOL			shareInterrupt;
	BOOL			rebootRebuil;
	USHORT			timingMode;
	USHORT			spare5;
	USHORT			spare6;
	SETUP_DEVICE	setupDevice[4];
	}	SETUP, *PSETUP;

/************************************************/
/*		RAID Structure Definitions				*/
/************************************************/
typedef	struct
	{
	UCHAR	signature;			// 0x55 our mirror signature
	UCHAR	status;				// current status bits
	UCHAR	pairIdentifier;		// unique identifier for pair
	ULONG	reconstructPoint;	// recontruction point for hot reconstruct
	}	DISK_MIRROR;

typedef struct	DEVICE_RAID1
	{
	long		TotalSectors;
	DISK_MIRROR DiskRaid1;
	}	DEVICE_RAID1, *PDEVICE_RAID1;

#define	DISK_MIRROR_POSITION	0x01A8
#define	SIGNATURE				0x55

#define	MASK_SERIAL_NUMBER	0x0FFE			// mask for serial number matching
#define	MASK_SERIAL_UNIT	0x0001			// mask for unit portion of serial number

// Status bits
#define	UCBF_MIRRORED		0x0010								// drive has a pair
#define	UCBF_MATCHED		0x0020								// drive pair is matched
#define	UCBF_SURVIVOR		0x0040								// this unit is a survivor of a pair
#define	UCBF_REBUILD		0x0080								// rebuild in progress on this device

// SCSI controls for RAID
#define	SC_MY_RAID			0xBF			// our special CDB command byte for Win95... interface
#define	MY_SCSI_QUERY1		0x32			// byte 1 subcommand to query driver for RAID 1 informatation
#define	MY_SCSI_REBUILD		0x40			// byte 1 subcommand to reconstruct a mirrored pair
#define MY_SCSI_DEMOFAIL	0x54			// byte 1 subcommand for RAID failure demonstration
#define	MY_SCSI_ALARMMUTE	0x60			// byte 1 subcommand to mute any alarm currently on

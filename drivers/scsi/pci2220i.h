/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2220I device driver for Linux.
 *
 * pci2220i.h - Linux Host Driver for PCI-2220i EIDE Adapters
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
#ifndef _PCI2220I_H
#define _PCI2220I_H

#ifndef	PSI_EIDE_SCSIOP
#define	PSI_EIDE_SCSIOP	1

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif 
#define	LINUXVERSION(v,p,s)    (((v)<<16) + ((p)<<8) + (s))

/************************************************/
/*		Some defines that we like 				*/
/************************************************/
#define	CHAR		char
#define	UCHAR		unsigned char
#define	SHORT		short
#define	USHORT		unsigned short
#define	BOOL		unsigned short
#define	LONG		long
#define	ULONG		unsigned long
#define	VOID		void

#include "psi_dale.h"

/************************************************/
/*		Timeout konstants		 				*/
/************************************************/
#define	TIMEOUT_READY				100			// 100 mSec
#define	TIMEOUT_DRQ					300			// 300 mSec
#define	TIMEOUT_DATA				(3 * HZ)	// 3 seconds

/************************************************/
/*		Misc. macros			 				*/
/************************************************/
#define ANY2SCSI(up, p)					\
((UCHAR *)up)[0] = (((ULONG)(p)) >> 8);	\
((UCHAR *)up)[1] = ((ULONG)(p));

#define SCSI2LONG(up)					\
( (((long)*(((UCHAR *)up))) << 16)		\
+ (((long)(((UCHAR *)up)[1])) << 8)		\
+ ((long)(((UCHAR *)up)[2])) )

#define XANY2SCSI(up, p)				\
((UCHAR *)up)[0] = ((long)(p)) >> 24;	\
((UCHAR *)up)[1] = ((long)(p)) >> 16;	\
((UCHAR *)up)[2] = ((long)(p)) >> 8;	\
((UCHAR *)up)[3] = ((long)(p));

#define XSCSI2LONG(up)					\
( (((long)(((UCHAR *)up)[0])) << 24)	\
+ (((long)(((UCHAR *)up)[1])) << 16)	\
+ (((long)(((UCHAR *)up)[2])) <<  8)	\
+ ((long)(((UCHAR *)up)[3])) )

#define	SelectSpigot(padapter,spigot)	outb_p (spigot, padapter->regStatSel)
#define WriteCommand(padapter,cmd)		outb_p (cmd, padapter->regStatCmd)

/************************************************/
/*		SCSI CDB operation codes 				*/
/************************************************/
#define SCSIOP_TEST_UNIT_READY		0x00
#define SCSIOP_REZERO_UNIT			0x01
#define SCSIOP_REWIND				0x01
#define SCSIOP_REQUEST_BLOCK_ADDR	0x02
#define SCSIOP_REQUEST_SENSE		0x03
#define SCSIOP_FORMAT_UNIT			0x04
#define SCSIOP_READ_BLOCK_LIMITS	0x05
#define SCSIOP_REASSIGN_BLOCKS		0x07
#define SCSIOP_READ6				0x08
#define SCSIOP_RECEIVE				0x08
#define SCSIOP_WRITE6				0x0A
#define SCSIOP_PRINT				0x0A
#define SCSIOP_SEND					0x0A
#define SCSIOP_SEEK6				0x0B
#define SCSIOP_TRACK_SELECT			0x0B
#define SCSIOP_SLEW_PRINT			0x0B
#define SCSIOP_SEEK_BLOCK			0x0C
#define SCSIOP_PARTITION			0x0D
#define SCSIOP_READ_REVERSE			0x0F
#define SCSIOP_WRITE_FILEMARKS		0x10
#define SCSIOP_FLUSH_BUFFER			0x10
#define SCSIOP_SPACE				0x11
#define SCSIOP_INQUIRY				0x12
#define SCSIOP_VERIFY6				0x13
#define SCSIOP_RECOVER_BUF_DATA		0x14
#define SCSIOP_MODE_SELECT			0x15
#define SCSIOP_RESERVE_UNIT			0x16
#define SCSIOP_RELEASE_UNIT			0x17
#define SCSIOP_COPY					0x18
#define SCSIOP_ERASE				0x19
#define SCSIOP_MODE_SENSE			0x1A
#define SCSIOP_START_STOP_UNIT		0x1B
#define SCSIOP_STOP_PRINT			0x1B
#define SCSIOP_LOAD_UNLOAD			0x1B
#define SCSIOP_RECEIVE_DIAGNOSTIC	0x1C
#define SCSIOP_SEND_DIAGNOSTIC		0x1D
#define SCSIOP_MEDIUM_REMOVAL		0x1E
#define SCSIOP_READ_CAPACITY		0x25
#define SCSIOP_READ					0x28
#define SCSIOP_WRITE				0x2A
#define SCSIOP_SEEK					0x2B
#define SCSIOP_LOCATE				0x2B
#define SCSIOP_WRITE_VERIFY			0x2E
#define SCSIOP_VERIFY				0x2F
#define SCSIOP_SEARCH_DATA_HIGH		0x30
#define SCSIOP_SEARCH_DATA_EQUAL	0x31
#define SCSIOP_SEARCH_DATA_LOW		0x32
#define SCSIOP_SET_LIMITS			0x33
#define SCSIOP_READ_POSITION		0x34
#define SCSIOP_SYNCHRONIZE_CACHE	0x35
#define SCSIOP_COMPARE				0x39
#define SCSIOP_COPY_COMPARE			0x3A
#define SCSIOP_WRITE_DATA_BUFF		0x3B
#define SCSIOP_READ_DATA_BUFF		0x3C
#define SCSIOP_CHANGE_DEFINITION	0x40
#define SCSIOP_READ_SUB_CHANNEL		0x42
#define SCSIOP_READ_TOC				0x43
#define SCSIOP_READ_HEADER			0x44
#define SCSIOP_PLAY_AUDIO			0x45
#define SCSIOP_PLAY_AUDIO_MSF		0x47
#define SCSIOP_PLAY_TRACK_INDEX		0x48
#define SCSIOP_PLAY_TRACK_RELATIVE	0x49
#define SCSIOP_PAUSE_RESUME			0x4B
#define SCSIOP_LOG_SELECT			0x4C
#define SCSIOP_LOG_SENSE			0x4D
#define SCSIOP_MODE_SELECT10		0x55
#define SCSIOP_MODE_SENSE10			0x5A
#define SCSIOP_LOAD_UNLOAD_SLOT		0xA6
#define SCSIOP_MECHANISM_STATUS		0xBD
#define SCSIOP_READ_CD				0xBE

// IDE command definitions
#define IDE_COMMAND_ATAPI_RESET		0x08
#define IDE_COMMAND_READ			0x20
#define IDE_COMMAND_WRITE			0x30
#define IDE_COMMAND_RECALIBRATE		0x10
#define IDE_COMMAND_SEEK			0x70
#define IDE_COMMAND_SET_PARAMETERS	0x91
#define IDE_COMMAND_VERIFY			0x40
#define IDE_COMMAND_ATAPI_PACKET	0xA0
#define IDE_COMMAND_ATAPI_IDENTIFY	0xA1
#define	IDE_CMD_READ_MULTIPLE		0xC4
#define	IDE_CMD_WRITE_MULTIPLE		0xC5
#define	IDE_CMD_SET_MULTIPLE		0xC6
#define IDE_COMMAND_IDENTIFY		0xEC

// IDE status definitions
#define IDE_STATUS_ERROR			0x01
#define IDE_STATUS_INDEX			0x02
#define IDE_STATUS_CORRECTED_ERROR	0x04
#define IDE_STATUS_DRQ				0x08
#define IDE_STATUS_DSC				0x10
#define	IDE_STATUS_WRITE_FAULT		0x20
#define IDE_STATUS_DRDY				0x40
#define IDE_STATUS_BUSY				0x80

// IDE error definitions
#define	IDE_ERROR_AMNF				0x01
#define	IDE_ERROR_TKONF				0x02
#define	IDE_ERROR_ABRT				0x04
#define	IDE_ERROR_MCR				0x08
#define	IDE_ERROR_IDFN				0x10
#define	IDE_ERROR_MC				0x20
#define	IDE_ERROR_UNC				0x40
#define	IDE_ERROR_BBK				0x80

// SCSI read capacity structure
typedef	struct _READ_CAPACITY_DATA
	{
	ULONG blks;				/* total blocks (converted to little endian) */
	ULONG blksiz;			/* size of each (converted to little endian) */
	}	READ_CAPACITY_DATA, *PREAD_CAPACITY_DATA;

// SCSI inquiry data
typedef struct _INQUIRYDATA
	{
	UCHAR DeviceType			:5;
	UCHAR DeviceTypeQualifier	:3;
	UCHAR DeviceTypeModifier	:7;
	UCHAR RemovableMedia		:1;
    UCHAR Versions;
    UCHAR ResponseDataFormat;
    UCHAR AdditionalLength;
    UCHAR Reserved[2];
	UCHAR SoftReset				:1;
	UCHAR CommandQueue			:1;
	UCHAR Reserved2				:1;
	UCHAR LinkedCommands		:1;
	UCHAR Synchronous			:1;
	UCHAR Wide16Bit				:1;
	UCHAR Wide32Bit				:1;
	UCHAR RelativeAddressing	:1;
    UCHAR VendorId[8];
    UCHAR ProductId[16];
    UCHAR ProductRevisionLevel[4];
    UCHAR VendorSpecific[20];
    UCHAR Reserved3[40];
	}	INQUIRYDATA, *PINQUIRYDATA;

// IDE IDENTIFY data
#pragma pack (1)
#pragma align 1
typedef struct _IDENTIFY_DATA
	{
    USHORT	GeneralConfiguration;		//  0
    USHORT	NumberOfCylinders;			//  1
    USHORT	Reserved1;					//  2
    USHORT	NumberOfHeads;				//  3
    USHORT	UnformattedBytesPerTrack;	//  4
    USHORT	UnformattedBytesPerSector;	//  5
    USHORT	SectorsPerTrack;			//  6
	USHORT	NumBytesISG;				//  7 Byte Len - inter-sector gap
	USHORT	NumBytesSync;				//  8          - sync field
	USHORT	NumWordsVUS;				//  9 Len - Vendor Unique Info
    USHORT	SerialNumber[10];			// 10
    USHORT	BufferType;					// 20
    USHORT	BufferSectorSize;			// 21
    USHORT	NumberOfEccBytes;			// 22
    USHORT	FirmwareRevision[4];		// 23
    USHORT	ModelNumber[20];			// 27
	USHORT	NumSectorsPerInt	:8;		// 47 Multiple Mode - Sec/Blk
	USHORT	Reserved2			:8;		// 47
	USHORT	DoubleWordMode;				// 48 flag for double word mode capable
	USHORT	VendorUnique1		:8;		// 49
	USHORT	SupportDMA			:1;		// 49 DMA supported
	USHORT	SupportLBA			:1;		// 49 LBA supported
	USHORT	SupportIORDYDisable	:1;		// 49 IORDY can be disabled
	USHORT	SupportIORDY		:1;		// 49 IORDY supported
	USHORT	ReservedPseudoDMA	:1;		// 49 reserved for pseudo DMA mode support
	USHORT	Reserved3			:3;		// 49
	USHORT	Reserved4;					// 50
	USHORT	Reserved5			:8;		// 51 Transfer Cycle Timing - PIO
	USHORT	PIOCycleTime		:8;		// 51 Transfer Cycle Timing - PIO
	USHORT	Reserved6			:8;		// 52                       - DMA
	USHORT	DMACycleTime		:8;		// 52                       - DMA
	USHORT	Valid_54_58			:1;		// 53 words 54 - 58 are vaild
	USHORT	Valid_64_70			:1;		// 53 words 64 - 70 are valid
	USHORT	Reserved7			:14;	// 53
	USHORT	LogNumCyl;					// 54 Current Translation - Num Cyl
	USHORT	LogNumHeads;				// 55                       Num Heads
	USHORT	LogSectorsPerTrack;			// 56                       Sec/Trk
	ULONG	LogTotalSectors;			// 57                       Total Sec
	USHORT	CurrentNumSecPerInt	:8;		// 59 current setting for number of sectors per interrupt
	USHORT	ValidNumSecPerInt	:1;		// 59 Current setting is valid for number of sectors per interrupt
	USHORT	Reserved8			:7;		// 59
	ULONG	LBATotalSectors;			// 60 LBA Mode - Sectors
	USHORT	DMASWordFlags;				// 62
	USHORT	DMAMWordFlags;				// 63
	USHORT	AdvancedPIOSupport  :8;		// 64 Flow control PIO transfer modes supported
	USHORT	Reserved9			:8;		// 64
	USHORT	MinMultiDMACycle;			// 65 minimum multiword DMA transfer cycle time per word
	USHORT	RecomendDMACycle;			// 66 Manufacturer's recommende multiword DMA transfer cycle time
	USHORT	MinPIOCycleWithoutFlow;		// 67 Minimum PIO transfer cycle time without flow control
	USHORT	MinPIOCylceWithFlow;		// 68 Minimum PIO transfer cycle time with IORDY flow control
	USHORT	ReservedSpace[256-69];		// 69
	}	IDENTIFY_DATA, *PIDENTIFY_DATA;
#pragma pack ()
#pragma align 0
#endif	// PSI_EIDE_SCSIOP

// function prototypes
int Pci2220i_Detect			(Scsi_Host_Template *tpnt);
int Pci2220i_Command		(Scsi_Cmnd *SCpnt);
int Pci2220i_QueueCommand	(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *));
int Pci2220i_Abort			(Scsi_Cmnd *SCpnt);
int Pci2220i_Reset			(Scsi_Cmnd *SCpnt, unsigned int flags);
int Pci2220i_Release		(struct Scsi_Host *pshost);
int Pci2220i_BiosParam		(Disk *disk, kdev_t dev, int geom[]);

#ifndef NULL
	#define NULL 0
#endif

#define PCI2220I {															\
		next:						NULL,									\
		module:						NULL,									\
		proc_name:					"pci2220i",					\
		proc_info:					NULL,	/* let's not bloat the kernel */\
		name:						"PCI-2220I EIDE Disk Controller",		\
		detect:						Pci2220i_Detect,						\
		release:					Pci2220i_Release,						\
		info:						NULL,	/* let's not bloat the kernel */\
		command:					Pci2220i_Command,						\
		queuecommand:				Pci2220i_QueueCommand,					\
		eh_strategy_handler:		NULL,									\
		eh_abort_handler:			NULL,									\
		eh_device_reset_handler:	NULL,									\
		eh_bus_reset_handler:		NULL,									\
		eh_host_reset_handler:		NULL,									\
		abort:						Pci2220i_Abort,							\
		reset:						Pci2220i_Reset,							\
		slave_attach:				NULL,									\
		bios_param:					Pci2220i_BiosParam,						\
		can_queue:					1,										\
		this_id:					-1,										\
		sg_tablesize:				SG_NONE,								\
		cmd_per_lun:				1,										\
		present:					0,										\
		unchecked_isa_dma:			0,										\
		use_clustering:				DISABLE_CLUSTERING,						\
		use_new_eh_code:			0										\
		}
#endif

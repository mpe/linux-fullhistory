/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2220I device driver for Linux.
 *
 * pci2220i.c - Linux Host Driver for PCI-2220I EIDE RAID Adapters
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
 *
 *	Revisions 1.10		Mar-26-1999
 *		- Updated driver for RAID and hot reconstruct support.
 *
 *	Revisions 1.11		Mar-26-1999
 *		- Fixed spinlock and PCI configuration.
 *
 ****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/kdev_t.h>
#include <linux/blk.h>
#include <linux/timer.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/io.h>
#include "scsi.h"
#include "hosts.h"
#include "pci2220i.h"

#if LINUX_VERSION_CODE >= LINUXVERSION(2,1,95)
#include <asm/spinlock.h>
#endif
#if LINUX_VERSION_CODE < LINUXVERSION(2,1,93)
#include <linux/bios32.h>
#endif

#define	PCI2220I_VERSION		"1.11"
//#define	READ_CMD				IDE_COMMAND_READ
//#define	WRITE_CMD				IDE_COMMAND_WRITE
//#define	MAX_BUS_MASTER_BLOCKS	1		// This is the maximum we can bus master
#define	READ_CMD				IDE_CMD_READ_MULTIPLE
#define	WRITE_CMD				IDE_CMD_WRITE_MULTIPLE
#define	MAX_BUS_MASTER_BLOCKS	SECTORSXFER		// This is the maximum we can bus master


struct proc_dir_entry Proc_Scsi_Pci2220i =
	{ PROC_SCSI_PCI2220I, 8, "pci2220i", S_IFDIR | S_IRUGO | S_IXUGO, 2 };

//#define DEBUG 1

#ifdef DEBUG
#define DEB(x) x
#define STOP_HERE()	{int st;for(st=0;st<100;st++){st=1;}}
#else
#define DEB(x)
#define STOP_HERE()
#endif

#define MAXADAPTER 4					// Increase this and the sizes of the arrays below, if you need more.


typedef struct
	{
	UCHAR		   	device;				// device code
	UCHAR			byte6;				// device select register image
	UCHAR			spigot;				// spigot number
	UCHAR			sparebyte;			// placeholder
	USHORT			sectors;			// number of sectors per track
	USHORT			heads;				// number of heads
	USHORT			cylinders;			// number of cylinders for this device
	USHORT			spareword;			// placeholder
	ULONG			blocks;				// number of blocks on device
	DISK_MIRROR		DiskMirror[2];		// RAID status and control
	ULONG			lastsectorlba[2];	// last addressable sector on the drive
	USHORT			raid;				// RAID active flag
	USHORT			mirrorRecon;
	UCHAR			hotRecon;
	USHORT			reconCount;
	}	OUR_DEVICE, *POUR_DEVICE;	

typedef struct
	{
	USHORT		 regDmaDesc;			// address of the DMA discriptor register for direction of transfer
	USHORT		 regDmaCmdStat;			// Byte #1 of DMA command status register
	USHORT		 regDmaAddrPci;			// 32 bit register for PCI address of DMA
	USHORT		 regDmaAddrLoc;			// 32 bit register for local bus address of DMA
	USHORT		 regDmaCount;			// 32 bit register for DMA transfer count
	USHORT		 regDmaMode;			// 32 bit register for DMA mode control
	USHORT		 regRemap;				// 32 bit local space remap
	USHORT		 regDesc;				// 32 bit local region descriptor
	USHORT		 regRange;				// 32 bit local range
	USHORT		 regIrqControl;			// 16 bit Interrupt enable/disable and status
	USHORT		 regScratchPad;			// scratch pad I/O base address
	USHORT		 regBase;				// Base I/O register for data space
	USHORT		 regData;				// data register I/O address
	USHORT		 regError;				// error register I/O address
	USHORT		 regSectCount;			// sector count register I/O address
	USHORT		 regLba0;				// least significant byte of LBA
	USHORT		 regLba8;				// next least significant byte of LBA
	USHORT		 regLba16;				// next most significan byte of LBA
	USHORT		 regLba24;				// head and most 4 significant bits of LBA
	USHORT		 regStatCmd;			// status on read and command on write register
	USHORT		 regStatSel;			// board status on read and spigot select on write register
	USHORT		 regFail;				// fail bits control register
	USHORT		 regAltStat;			// alternate status and drive control register
	USHORT		 basePort;				// PLX base I/O port
	USHORT		 timingMode;			// timing mode currently set for adapter
	USHORT		 timingPIO;				// TRUE if PIO timing is active
	ULONG		 timingAddress;			// address to use on adapter for current timing mode
	ULONG		 irqOwned;				// owned IRQ or zero if shared
	OUR_DEVICE	 device[DALE_MAXDRIVES];
	DISK_MIRROR	*raidData[8];
	ULONG		 startSector;
	USHORT		 sectorCount;
	UCHAR		 cmd;
	Scsi_Cmnd	*SCpnt;
	VOID		*buffer;
	POUR_DEVICE	 pdev;					// current device opearating on
	USHORT		 expectingIRQ;
	USHORT		 reconIsStarting;		// indicate hot reconstruct is starting
	USHORT		 reconOn;				// Hot reconstruct is to be done.
	USHORT		 reconPhase;			// Hot reconstruct operation is in progress.
	ULONG		 reconSize;
	USHORT		 demoFail;				// flag for RAID failure demonstration
	USHORT		 survivor;
	USHORT		 failinprog;
	struct timer_list	reconTimer;	
	struct timer_list	timer;
	UCHAR		*kBuffer;
	}	ADAPTER2220I, *PADAPTER2220I;

#define HOSTDATA(host) ((PADAPTER2220I)&host->hostdata)

#define	RECON_PHASE_READY		0x01
#define	RECON_PHASE_COPY		0x02
#define	RECON_PHASE_UPDATE		0x03
#define	RECON_PHASE_LAST		0x04
#define	RECON_PHASE_END			0x07	
#define	RECON_PHASE_MARKING		0x80
#define	RECON_PHASE_FAILOVER	0xFF

static struct	Scsi_Host 	   *PsiHost[MAXADAPTER] = {NULL,};  // One for each adapter
static			int				NumAdapters = 0;
static			SETUP			DaleSetup;
static			DISK_MIRROR		DiskMirror[2];
static			ULONG			ModeArray[] = {DALE_DATA_MODE2, DALE_DATA_MODE3, DALE_DATA_MODE4, DALE_DATA_MODE4P};

static void ReconTimerExpiry (unsigned long data);

/****************************************************************
 *	Name:	MuteAlarm	:LOCAL
 *
 *	Description:	Mute the audible alarm.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static void MuteAlarm (PADAPTER2220I padapter)
	{
	UCHAR	old;

	old = (inb_p (padapter->regStatSel) >> 3) | (inb_p (padapter->regStatSel) & 0x83);
	outb_p (old | 0x40, padapter->regFail);
	}
/****************************************************************
 *	Name:	WaitReady	:LOCAL
 *
 *	Description:	Wait for device ready.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WaitReady (PADAPTER2220I padapter)
	{
	ULONG	z;
	UCHAR	status;

	for ( z = 0;  z < (TIMEOUT_READY * 4);  z++ )
		{
		status = inb_p (padapter->regStatCmd);
		if ( (status & (IDE_STATUS_DRDY | IDE_STATUS_BUSY)) == IDE_STATUS_DRDY )
			return 0;
		udelay (250);
		}
	return status;
	}
/****************************************************************
 *	Name:	WaitReadyReset	:LOCAL
 *
 *	Description:	Wait for device ready.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WaitReadyReset (PADAPTER2220I padapter)
	{
	ULONG	z;
	UCHAR	status;

	for ( z = 0;  z < (250 * 4);  z++ )				// wait up to 1/4 second
		{
		status = inb_p (padapter->regStatCmd);
		if ( (status & (IDE_STATUS_DRDY | IDE_STATUS_BUSY)) == IDE_STATUS_DRDY )
			{
			DEB (printk ("\nPCI2220I:  Reset took %ld mSec to be ready", z / 4));
			return 0;
			}
		udelay (250);
		}
	DEB (printk ("\nPCI2220I:  Reset took more than 1 Second to come ready, Disk Failure"));
	return status;
	}
/****************************************************************
 *	Name:	WaitDrq	:LOCAL
 *
 *	Description:	Wait for device ready for data transfer.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WaitDrq (PADAPTER2220I padapter)
	{
	ULONG	z;
	UCHAR	status;

	for ( z = 0;  z < (TIMEOUT_DRQ * 4);  z++ )
		{
		status = inb_p (padapter->regStatCmd);
		if ( status & IDE_STATUS_DRQ )
			return 0;
		udelay (250);
		}
	return status;
	}
/****************************************************************
 *	Name:	HardReset	:LOCAL
 *
 *	Description:	Wait for device ready for data transfer.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *					spigot	 - Spigot number.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int HardReset (PADAPTER2220I padapter, POUR_DEVICE pdev, UCHAR spigot)
	{
	SelectSpigot (padapter, spigot | 0x80);
	
	outb_p (0x0E, padapter->regAltStat);					// reset the suvivor
	udelay (100);											// wait a little	
	outb_p (0x08, padapter->regAltStat);					// clear the reset
	udelay (100);
	outb_p (0xA0, padapter->regLba24);						//Specify drive

	outb_p (pdev->byte6, padapter->regLba24);				// select the drive
	if ( WaitReadyReset (padapter) )
		return TRUE;
	outb_p (SECTORSXFER, padapter->regSectCount);
	WriteCommand (padapter, IDE_CMD_SET_MULTIPLE);	
	if ( WaitReady (padapter) )
		return TRUE;
	return FALSE;
	}
/****************************************************************
 *	Name:	BusMaster	:LOCAL
 *
 *	Description:	Do a bus master I/O.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					datain	 - TRUE if data read.
 *					irq		 - TRUE if bus master interrupt expected.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static void BusMaster (PADAPTER2220I padapter, UCHAR datain, UCHAR irq)
	{
	ULONG zl;
	
	outl (padapter->timingAddress, padapter->regDmaAddrLoc);
	outl (virt_to_bus (padapter->buffer), padapter->regDmaAddrPci);
	zl = (padapter->sectorCount > MAX_BUS_MASTER_BLOCKS) ? MAX_BUS_MASTER_BLOCKS : padapter->sectorCount;
	padapter->sectorCount -= zl;
	zl *= (ULONG)BYTES_PER_SECTOR;
	padapter->buffer += zl;
	outl (zl, padapter->regDmaCount);
	if ( datain )
		{
		outb_p (8, padapter->regDmaDesc);						// read operation
		if ( irq && !padapter->sectorCount )
			outb_p (5, padapter->regDmaMode);					// interrupt on
		else
			outb_p (1, padapter->regDmaMode);					// no interrupt
		}
	else
		{
		outb_p (0, padapter->regDmaDesc);						// write operation
		outb_p (1, padapter->regDmaMode);						// no interrupt
		}
	outb_p (0x03, padapter->regDmaCmdStat);						// kick the DMA engine in gear
	}
/****************************************************************
 *	Name:	WriteData	:LOCAL
 *
 *	Description:	Write data to device.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WriteData (PADAPTER2220I padapter)
	{
	ULONG	zl;
	
	if ( !WaitDrq (padapter) )
		{
		if ( padapter->timingPIO )
			{
			zl = (padapter->sectorCount > MAX_BUS_MASTER_BLOCKS) ? MAX_BUS_MASTER_BLOCKS : padapter->sectorCount;
			outsw (padapter->regData, padapter->buffer, zl * (BYTES_PER_SECTOR / 2));
			padapter->sectorCount -= zl;
			padapter->buffer += zl * BYTES_PER_SECTOR;
			}
		else
			BusMaster (padapter, 0, 0);
		return 0;
		}
	padapter->cmd = 0;												// null out the command byte
	return 1;
	}
/****************************************************************
 *	Name:	WriteDataBoth	:LOCAL
 *
 *	Description:	Write data to device.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WriteDataBoth (PADAPTER2220I padapter)
	{
	ULONG	zl;
	UCHAR	status0, status1;

	SelectSpigot (padapter, 1);
	status0 = WaitDrq (padapter);
	if ( !status0 )
		{
		SelectSpigot (padapter, 2);
		status1 = WaitDrq (padapter);
		if ( !status1 )
			{
			SelectSpigot (padapter, 3);
			if ( padapter->timingPIO )
				{
				zl = (padapter->sectorCount > MAX_BUS_MASTER_BLOCKS) ? MAX_BUS_MASTER_BLOCKS : padapter->sectorCount;
				outsw (padapter->regData, padapter->buffer, zl * (BYTES_PER_SECTOR / 2));
				padapter->sectorCount -= zl;
				padapter->buffer += zl * BYTES_PER_SECTOR;
				}
			else
				BusMaster (padapter, 0, 0);
			return 0;
			}
		}
	padapter->cmd = 0;												// null out the command byte
	if ( status0 )
		return 1;
	return 2;
	}
/****************************************************************
 *	Name:	IdeCmd	:LOCAL
 *
 *	Description:	Process an IDE command.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *
 *	Returns:		Zero if no error or status register contents on error.
 *
 ****************************************************************/
static UCHAR IdeCmd (PADAPTER2220I padapter, POUR_DEVICE pdev)
	{
	UCHAR	status;

	SelectSpigot (padapter, pdev->spigot);							// select the spigot
	outb_p (pdev->byte6 | ((UCHAR *)(&padapter->startSector))[3], padapter->regLba24);			// select the drive
	status = WaitReady (padapter);
	if ( !status )
		{
		outb_p (padapter->sectorCount, padapter->regSectCount);
		outb_p (((UCHAR *)(&padapter->startSector))[0], padapter->regLba0);
		outb_p (((UCHAR *)(&padapter->startSector))[1], padapter->regLba8);
		outb_p (((UCHAR *)(&padapter->startSector))[2], padapter->regLba16);
		padapter->expectingIRQ = TRUE;
		WriteCommand (padapter, padapter->cmd);
		return 0;
		}

	padapter->cmd = 0;									// null out the command byte
	return status;
	}
/****************************************************************
 *	Name:	IdeCmdBoth	:LOCAL
 *
 *	Description:	Process an IDE command to both drivers.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		Zero if no error or spigot of error.
 *
 ****************************************************************/
static UCHAR IdeCmdBoth (PADAPTER2220I padapter)
	{
	UCHAR	status0;
	UCHAR	status1;

	SelectSpigot (padapter, 3);										// select the spigots
	outb_p (padapter->pdev->byte6 | ((UCHAR *)(&padapter->startSector))[3], padapter->regLba24);// select the drive
	SelectSpigot (padapter, 1);
	status0 = WaitReady (padapter);
	if ( !status0 )
		{
		SelectSpigot (padapter, 2);
		status1 = WaitReady (padapter);
		if ( !status1 )
			{
			SelectSpigot (padapter, 3);
			outb_p (padapter->sectorCount, padapter->regSectCount);
			outb_p (((UCHAR *)(&padapter->startSector))[0], padapter->regLba0);
			outb_p (((UCHAR *)(&padapter->startSector))[1], padapter->regLba8);
			outb_p (((UCHAR *)(&padapter->startSector))[2], padapter->regLba16);
			padapter->expectingIRQ = TRUE;
			WriteCommand (padapter, padapter->cmd);
			return 0;
			}
		}
	padapter->cmd = 0;									// null out the command byte
	if ( status0 )
		return 1;
	return 2;
	}
/****************************************************************
 *	Name:	OpDone	:LOCAL
 *
 *	Description:	Complete an operatoin done sequence.
 *
 *	Parameters:		padapter - Pointer to host data block.
 *					spigot	 - Spigot select code.
 *					device	 - Device byte code.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void OpDone (PADAPTER2220I padapter, ULONG result)
	{
	Scsi_Cmnd	   *SCpnt = padapter->SCpnt;
	
	if ( padapter->reconPhase )
		{
		padapter->reconPhase = 0;
		if ( padapter->SCpnt )
			{
			Pci2220i_QueueCommand (SCpnt, SCpnt->scsi_done);
			}
		else
			{
			if ( padapter->reconOn )
				{
				ReconTimerExpiry ((unsigned long)padapter);
				}
			}
		}
	else
		{
		padapter->cmd = 0;
		padapter->SCpnt = NULL;	
		SCpnt->result = result;
		SCpnt->scsi_done (SCpnt);
		if ( padapter->reconOn && !padapter->reconTimer.data )
			{
			padapter->reconTimer.expires = jiffies + (HZ / 4);	// start in 1/4 second
			padapter->reconTimer.data = (unsigned long)padapter;
			add_timer (&padapter->reconTimer);
			}
		}
	}
/****************************************************************
 *	Name:	InlineIdentify	:LOCAL
 *
 *	Description:	Do an intline inquiry on a drive.
 *
 *	Parameters:		padapter - Pointer to host data block.
 *					spigot	 - Spigot select code.
 *					device	 - Device byte code.
 *
 *	Returns:		Last addressable sector or zero if none.
 *
 ****************************************************************/
static ULONG InlineIdentify (PADAPTER2220I padapter, UCHAR spigot, UCHAR device)
	{
	PIDENTIFY_DATA	pid = (PIDENTIFY_DATA)padapter->kBuffer;

	SelectSpigot (padapter, spigot | 0x80);						// select the spigot
	outb_p (device << 4, padapter->regLba24);				// select the drive
	if ( WaitReady (padapter) )
		return 0;
	WriteCommand (padapter, IDE_COMMAND_IDENTIFY);	
	if ( WaitDrq (padapter) )
		return 0;
	insw (padapter->regData, padapter->kBuffer, sizeof (IDENTIFY_DATA) >> 1);
	return (pid->LBATotalSectors - 1);
	}
/****************************************************************
 *	Name:	InlineReadSignature	:LOCAL
 *
 *	Description:	Do an inline read RAID sigature.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *					index	 - index of data to read.
 *
 *	Returns:		Zero if no error or status register contents on error.
 *
 ****************************************************************/
static UCHAR InlineReadSignature (PADAPTER2220I padapter, POUR_DEVICE pdev, int index)
	{
	UCHAR	status;
	UCHAR	spigot = 1 << index;
	ULONG	zl = pdev->lastsectorlba[index];

	SelectSpigot (padapter, spigot | 0x80);				// select the spigot without interrupts
	outb_p (pdev->byte6 | ((UCHAR *)&zl)[3], padapter->regLba24);		
	status = WaitReady (padapter);
	if ( !status )
		{
		outb_p (((UCHAR *)&zl)[2], padapter->regLba16);
		outb_p (((UCHAR *)&zl)[1], padapter->regLba8); 
		outb_p (((UCHAR *)&zl)[0], padapter->regLba0);
		outb_p (1, padapter->regSectCount);
		WriteCommand (padapter, IDE_COMMAND_READ);
		status = WaitDrq (padapter);
		if ( !status )
			{
			insw (padapter->regData, padapter->kBuffer, BYTES_PER_SECTOR / 2);
			((ULONG *)(&pdev->DiskMirror[index]))[0] = ((ULONG *)(&padapter->kBuffer[DISK_MIRROR_POSITION]))[0];
			((ULONG *)(&pdev->DiskMirror[index]))[1] = ((ULONG *)(&padapter->kBuffer[DISK_MIRROR_POSITION]))[1];
			// some drives assert DRQ before IRQ so let's make sure we clear the IRQ
			WaitReady (padapter);
			return 0;			
			}
		}
	return status;
	}
/****************************************************************
 *	Name:	DecodeError	:LOCAL
 *
 *	Description:	Decode and process device errors.
 *
 *	Parameters:		padapter - Pointer to adapter data.
 *					status - Status register code.
 *
 *	Returns:		The driver status code.
 *
 ****************************************************************/
static ULONG DecodeError (PADAPTER2220I	padapter, UCHAR status)
	{
	UCHAR			error;

	padapter->expectingIRQ = 0;
	if ( status & IDE_STATUS_WRITE_FAULT )
		{
		return DID_PARITY << 16;
		}
	if ( status & IDE_STATUS_BUSY )
		return DID_BUS_BUSY << 16;

	error = inb_p (padapter->regError);
	DEB(printk ("\npci2220i error register: %x", error));
	switch ( error )
		{
		case IDE_ERROR_AMNF:
		case IDE_ERROR_TKONF:
		case IDE_ERROR_ABRT:
		case IDE_ERROR_IDFN:
		case IDE_ERROR_UNC:
		case IDE_ERROR_BBK:
		default:
			return DID_ERROR << 16;
		}
	return DID_ERROR << 16;
	}
/****************************************************************
 *	Name:	StartTimer	:LOCAL
 *
 *	Description:	Start the timer.
 *
 *	Parameters:		ipadapter - Pointer adapter data structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void StartTimer (PADAPTER2220I padapter)
	{
	padapter->timer.expires = jiffies + TIMEOUT_DATA;
	add_timer (&padapter->timer);
	}
/****************************************************************
 *	Name:	WriteSignature	:LOCAL
 *
 *	Description:	Start the timer.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to our device.
 *					spigot	 - Selected spigot.
 *					index	 - index of mirror signature on device.
 *
 *	Returns:		TRUE on any error.
 *
 ****************************************************************/
static int WriteSignature (PADAPTER2220I padapter, POUR_DEVICE pdev, UCHAR spigot, int index)
	{
	ULONG	zl;

	SelectSpigot (padapter, spigot);
	zl = pdev->lastsectorlba[index];
	outb_p (pdev->byte6 | ((UCHAR *)&zl)[3], padapter->regLba24);		
	outb_p (((UCHAR *)&zl)[2], padapter->regLba16);
	outb_p (((UCHAR *)&zl)[1], padapter->regLba8);
	outb_p (((UCHAR *)&zl)[0], padapter->regLba0);
	outb_p (1, padapter->regSectCount);

	WriteCommand (padapter, IDE_COMMAND_WRITE);	
	if ( WaitDrq (padapter) )
		return TRUE;
	StartTimer (padapter);	
	padapter->expectingIRQ = TRUE;
	
	((ULONG *)(&padapter->kBuffer[DISK_MIRROR_POSITION]))[0] = ((ULONG *)(&pdev->DiskMirror[index]))[0];
	((ULONG *)(&padapter->kBuffer[DISK_MIRROR_POSITION]))[1] = ((ULONG *)(&pdev->DiskMirror[index]))[1];
	outsw (padapter->regData, padapter->kBuffer, BYTES_PER_SECTOR / 2);
	return FALSE;
	}
/*******************************************************************************************************
 *	Name:			InitFailover
 *
 *	Description:	This is the beginning of the failover routine
 *
 *	Parameters:		SCpnt	 - Pointer to SCSI command structure.
 *					padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to our device.
 *	
 *	Returns:		TRUE on error.
 *
 ******************************************************************************************************/
static int InitFailover (PADAPTER2220I padapter, POUR_DEVICE pdev)
	{
	UCHAR			 spigot;
	
	DEB (printk ("\npci2220i:  Initialize failover process - survivor = %d", padapter->survivor));
	pdev->raid = FALSE;									//initializes system for non raid mode
	pdev->hotRecon = 0;
	padapter->reconOn = FALSE;
	spigot = (padapter->survivor) ? 2 : 1;	

	if ( pdev->DiskMirror[padapter->survivor].status & UCBF_REBUILD )
		return (TRUE); 	

	if ( HardReset (padapter, pdev, spigot) )
		return TRUE;

	outb_p (0x3C | spigot, padapter->regFail);			// sound alarm and set fail light		
	pdev->DiskMirror[padapter->survivor].status = UCBF_MIRRORED | UCBF_SURVIVOR;	//clear present status
	
	if ( WriteSignature (padapter, pdev, spigot, padapter->survivor) )
		return TRUE;
	padapter->failinprog = TRUE;
	return FALSE;
	}
/****************************************************************
 *	Name:	TimerExpiry	:LOCAL
 *
 *	Description:	Timer expiry routine.
 *
 *	Parameters:		data - Pointer adapter data structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void TimerExpiry (unsigned long data)
	{
	PADAPTER2220I	padapter = (PADAPTER2220I)data;
	POUR_DEVICE		pdev = padapter->pdev;
	UCHAR			status = IDE_STATUS_BUSY;
	UCHAR			temp, temp1;
#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    int					flags;
#else /* version >= v2.1.95 */
    unsigned long		flags;
#endif /* version >= v2.1.95 */

#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    /* Disable interrupts, if they aren't already disabled. */
    save_flags (flags);
    cli ();
#else /* version >= v2.1.95 */
    /*
     * Disable interrupts, if they aren't already disabled and acquire
     * the I/O spinlock.
     */
    spin_lock_irqsave (&io_request_lock, flags);
#endif /* version >= v2.1.95 */
	DEB (printk ("\nPCI2220I: Timeout expired "));

	if ( padapter->failinprog )
		{
		DEB (printk ("in failover process"));
		OpDone (padapter, DecodeError (padapter, inb_p (padapter->regStatCmd)));
		goto timerExpiryDone;
		}
	
	while ( padapter->reconPhase )
		{
		DEB (printk ("in recon phase %X", padapter->reconPhase));
		switch ( padapter->reconPhase )
			{
			case RECON_PHASE_MARKING:
			case RECON_PHASE_LAST:
				padapter->survivor = (pdev->spigot ^ 3) >> 1;
				DEB (printk ("\npci2220i: FAILURE 1"));
				if ( InitFailover (padapter, pdev) )
					OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;
			
			case RECON_PHASE_READY:
				OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;

			case RECON_PHASE_COPY:
				padapter->survivor = (pdev->spigot) >> 1;
				DEB (printk ("\npci2220i: FAILURE 2"));
				DEB (printk ("\n       spig/stat = %X", inb_p (padapter->regStatSel));
				if ( InitFailover (padapter, pdev) )
					OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;

			case RECON_PHASE_UPDATE:
				padapter->survivor = (pdev->spigot) >> 1;
				DEB (printk ("\npci2220i: FAILURE 3")));
				if ( InitFailover (padapter, pdev) )
					OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;

			case RECON_PHASE_END:
				padapter->survivor = (pdev->spigot) >> 1;
				DEB (printk ("\npci2220i: FAILURE 4"));
				if ( InitFailover (padapter, pdev) )
					OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;
			
			default:
				goto timerExpiryDone;
			}
		}
	
	while ( padapter->cmd )
		{
		outb_p (0x08, padapter->regDmaCmdStat);					// cancel interrupt from DMA engine
		if ( pdev->raid )
			{
			if ( padapter->cmd == WRITE_CMD )
				{
				DEB (printk ("in RAID write operation"));
				if ( inb_p (padapter->regStatSel) & 1 )
					{
					SelectSpigot (padapter, 0x81 ); // Masking the interrupt during spigot select
					temp = inb_p (padapter->regStatCmd);
					}
				else
					temp = IDE_STATUS_BUSY;

				if ( inb (padapter->regStatSel) & 2 )
					{
					SelectSpigot (padapter, 0x82 ); // Masking the interrupt during spigot select
					temp1 = inb_p (padapter->regStatCmd);
					}
				else
					temp1 = IDE_STATUS_BUSY;
			
				if ( (temp & IDE_STATUS_BUSY) || (temp1 & IDE_STATUS_BUSY) )
					{
	 				if ( (temp & IDE_STATUS_BUSY) && (temp1 & IDE_STATUS_BUSY) ) 
						{
						status = temp;
						break;
						}		
					else	
						{
						if (temp & IDE_STATUS_BUSY)
							padapter->survivor = 1;
						else
							padapter->survivor = 0;
						DEB (printk ("\npci2220i: FAILURE 5"));
						if ( InitFailover (padapter, pdev) )
							{
							status = inb_p (padapter->regStatCmd);
							break;
							}
						goto timerExpiryDone;
						}
					}
				}
			else
				{
				DEB (printk ("in RAID read operation"));
				padapter->survivor = (pdev->spigot ^ 3) >> 1;
				DEB (printk ("\npci2220i: FAILURE 6"));
				if ( InitFailover (padapter, pdev) )
					{
					status = inb_p (padapter->regStatCmd);
					break;
					}
				goto timerExpiryDone;
				}
			}
		else
			{
			DEB (printk ("in I/O operation"));
			status = inb_p (padapter->regStatCmd);
			}
		break;
		}
	
	OpDone (padapter, DecodeError (padapter, status));

timerExpiryDone:;
#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    /*
     * Restore the original flags which will enable interrupts
     * if and only if they were enabled on entry.
     */
    restore_flags (flags);
#else /* version >= v2.1.95 */
    /*
     * Release the I/O spinlock and restore the original flags
     * which will enable interrupts if and only if they were
     * enabled on entry.
     */
    spin_unlock_irqrestore (&io_request_lock, flags);
#endif /* version >= v2.1.95 */
	}
/****************************************************************
 *	Name:			SetReconstruct	:LOCAL
 *
 *	Description:	Set the reconstruct up.
 *
 *	Parameters:		pdev	- Pointer to device structure.
 *					index	- Mirror index number.
 *
 *	Returns:		Number of sectors on new disk required.
 *
 ****************************************************************/
static LONG SetReconstruct (POUR_DEVICE pdev, int index)
	{
	pdev->DiskMirror[index].status = UCBF_MIRRORED;							// setup the flags
	pdev->DiskMirror[index ^ 1].status = UCBF_MIRRORED | UCBF_REBUILD;
	pdev->DiskMirror[index ^ 1].reconstructPoint = 0;						// start the reconstruct
	pdev->reconCount = 1990;												// mark target drive early
	pdev->hotRecon = 1 >> index;
	return pdev->DiskMirror[index].reconstructPoint;
	}
/****************************************************************
 *	Name:	ReconTimerExpiry	:LOCAL
 *
 *	Description:	Reconstruct timer expiry routine.
 *
 *	Parameters:		data - Pointer adapter data structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void ReconTimerExpiry (unsigned long data)
	{
	PADAPTER2220I	padapter;
	POUR_DEVICE		pdev;
	ULONG			testsize = 0;
	PIDENTIFY_DATA	pid;
	USHORT			minmode;
	ULONG			zl;
	UCHAR			zc;
#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    int				flags;
#else /* version >= v2.1.95 */
    unsigned long	flags;
#endif /* version >= v2.1.95 */

#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    /* Disable interrupts, if they aren't already disabled. */
    save_flags (flags);
    cli ();
#else /* version >= v2.1.95 */
    /*
     * Disable interrupts, if they aren't already disabled and acquire
     * the I/O spinlock.
     */
    spin_lock_irqsave (&io_request_lock, flags);
#endif /* version >= v2.1.95 */

	padapter = (PADAPTER2220I)data;
	if ( padapter->SCpnt )
		goto reconTimerExpiry;

	pdev = padapter->device;
	pid = (PIDENTIFY_DATA)padapter->kBuffer;
	padapter->reconTimer.data = 0;
	padapter->pdev = pdev;
	if ( padapter->reconIsStarting )
		{
		padapter->reconIsStarting = FALSE;
		padapter->reconOn = FALSE;
		pdev->hotRecon = FALSE;

		if ( (pdev->DiskMirror[0].signature == SIGNATURE) && (pdev->DiskMirror[1].signature == SIGNATURE) &&
			 (pdev->DiskMirror[0].pairIdentifier == (pdev->DiskMirror[1].pairIdentifier ^ 1)) )
			{
			if ( (pdev->DiskMirror[0].status & UCBF_MATCHED) && (pdev->DiskMirror[1].status & UCBF_MATCHED) )
				{
				goto reconTimerExpiry;
				}

			if ( pdev->DiskMirror[0].status & UCBF_SURVIVOR )				// is first drive survivor?
				testsize = SetReconstruct (pdev, 0);
			else
				if ( pdev->DiskMirror[1].status & UCBF_SURVIVOR )			// is second drive survivor?
					testsize = SetReconstruct (pdev, 1);

			if ( (pdev->DiskMirror[0].status & UCBF_REBUILD) || (pdev->DiskMirror[1].status & UCBF_REBUILD) )
				{
				if ( pdev->DiskMirror[0].status & UCBF_REBUILD )
					{
					pdev->hotRecon = 1;
					pdev->mirrorRecon = 0;
					}
				else
					{
					pdev->hotRecon = 2;
					pdev->mirrorRecon = 1;
					}
				}
			}

		if ( !pdev->hotRecon )
			goto reconTimerExpiry;

		zc = ((inb_p (padapter->regStatSel) >> 3) | inb_p (padapter->regStatSel)) & 0x83;		// mute the alarm
		outb_p (zc | pdev->hotRecon | 0x40, padapter->regFail);

		while ( 1 )
			{
			if ( HardReset (padapter, pdev, pdev->hotRecon) )
				{
				DEB (printk ("\npci2220i: sub 1"));
				break;
				}

			pdev->lastsectorlba[pdev->mirrorRecon] = InlineIdentify (padapter, pdev->hotRecon, 0);

			if ( pdev->lastsectorlba[pdev->mirrorRecon] < testsize )
				{
				DEB (printk ("\npci2220i: sub 2 %ld %ld", pdev->lastsectorlba[pdev->mirrorRecon], testsize));
				break;
				}

	        // test LBA and multiper sector transfer compatability
			if (!pid->SupportLBA || (pid->NumSectorsPerInt < SECTORSXFER) || !pid->Valid_64_70 )
				{
				DEB (printk ("\npci2220i: sub 3"));
				break;
				}

	        // test PIO/bus matering mode compatability
			if ( (pid->MinPIOCycleWithoutFlow > 240) && !pid->SupportIORDYDisable && !padapter->timingPIO )
				{
				DEB (printk ("\npci2220i: sub 4"));
				break;
				}

			if ( pid->MinPIOCycleWithoutFlow <= 120 )	// setup timing mode of drive
				minmode = 5;
			else
				{
				if ( pid->MinPIOCylceWithFlow <= 150 )
					minmode = 4;
				else
					{
					if ( pid->MinPIOCylceWithFlow <= 180 )
						minmode = 3;
					else
						{
						if ( pid->MinPIOCylceWithFlow <= 240 )
							minmode = 2;
						else
							{
							DEB (printk ("\npci2220i: sub 5"));
							break;
							}
						}
					}
				}

			if ( padapter->timingMode > minmode )									// set minimum timing mode
				padapter->timingMode = minmode;
			if ( padapter->timingMode >= 2 )
				padapter->timingAddress	= ModeArray[padapter->timingMode - 2];
			else
				padapter->timingPIO = TRUE;

			padapter->reconOn = TRUE;
			break;
			}

		if ( !padapter->reconOn )
			{		
			pdev->hotRecon = FALSE;
			padapter->survivor = pdev->mirrorRecon ^ 1;
			padapter->reconPhase = RECON_PHASE_FAILOVER;
				DEB (printk ("\npci2220i: FAILURE 7"));
			InitFailover (padapter, pdev);
			goto reconTimerExpiry;
			}

		pdev->raid = TRUE;
	
		if ( WriteSignature (padapter, pdev, pdev->spigot, pdev->mirrorRecon ^ 1) )
			goto reconTimerExpiry;
		padapter->reconPhase = RECON_PHASE_MARKING;
		goto reconTimerExpiry;
		}

	//**********************************
	// reconstruct copy starts here	
	//**********************************
	if ( pdev->reconCount++ > 2000 )
		{
		pdev->reconCount = 0;
		if ( WriteSignature (padapter, pdev, pdev->hotRecon, pdev->mirrorRecon) )
			{
			padapter->survivor = pdev->mirrorRecon ^ 1;
			padapter->reconPhase = RECON_PHASE_FAILOVER;
				DEB (printk ("\npci2220i: FAILURE 8"));
			InitFailover (padapter, pdev);
			goto reconTimerExpiry;
			}
		padapter->reconPhase = RECON_PHASE_UPDATE;
		goto reconTimerExpiry;
		}

	zl = pdev->DiskMirror[pdev->mirrorRecon].reconstructPoint;	
	padapter->reconSize = pdev->DiskMirror[pdev->mirrorRecon ^ 1].reconstructPoint - zl;
	if ( padapter->reconSize > MAX_BUS_MASTER_BLOCKS )
		padapter->reconSize = MAX_BUS_MASTER_BLOCKS;

	if ( padapter->reconSize )
		{
		SelectSpigot (padapter, 3);										// select the spigots
		outb_p (pdev->byte6 | ((UCHAR *)(&zl))[3], padapter->regLba24);// select the drive
		SelectSpigot (padapter, pdev->spigot);
		if ( WaitReady (padapter) )
			goto reconTimerExpiry;

		SelectSpigot (padapter, pdev->hotRecon);
		if ( WaitReady (padapter) )
			{
			padapter->survivor = pdev->mirrorRecon ^ 1;
			padapter->reconPhase = RECON_PHASE_FAILOVER;
				DEB (printk ("\npci2220i: FAILURE 9"));
			InitFailover (padapter, pdev);
			goto reconTimerExpiry;
			}
	
		SelectSpigot (padapter, 3);
		outb_p (padapter->reconSize & 0xFF, padapter->regSectCount);
		outb_p (((UCHAR *)(&zl))[0], padapter->regLba0);
		outb_p (((UCHAR *)(&zl))[1], padapter->regLba8);
		outb_p (((UCHAR *)(&zl))[2], padapter->regLba16);
		padapter->expectingIRQ = TRUE;
		padapter->reconPhase = RECON_PHASE_READY;
		SelectSpigot (padapter, pdev->hotRecon);
		WriteCommand (padapter, WRITE_CMD);
		StartTimer (padapter);
		SelectSpigot (padapter, pdev->spigot);
		WriteCommand (padapter, READ_CMD);
		goto reconTimerExpiry;
		}

	pdev->DiskMirror[pdev->mirrorRecon].status = UCBF_MIRRORED | UCBF_MATCHED;
	pdev->DiskMirror[pdev->mirrorRecon ^ 1].status = UCBF_MIRRORED | UCBF_MATCHED;
	if ( WriteSignature (padapter, pdev, pdev->spigot, pdev->mirrorRecon ^ 1) )
		goto reconTimerExpiry;
	padapter->reconPhase = RECON_PHASE_LAST;

reconTimerExpiry:;
#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    /*
     * Restore the original flags which will enable interrupts
     * if and only if they were enabled on entry.
     */
    restore_flags (flags);
#else /* version >= v2.1.95 */
    /*
     * Release the I/O spinlock and restore the original flags
     * which will enable interrupts if and only if they were
     * enabled on entry.
     */
    spin_unlock_irqrestore (&io_request_lock, flags);
#endif /* version >= v2.1.95 */
	}
/****************************************************************
 *	Name:	Irq_Handler	:LOCAL
 *
 *	Description:	Interrupt handler.
 *
 *	Parameters:		irq		- Hardware IRQ number.
 *					dev_id	-
 *					regs	-
 *
 *	Returns:		TRUE if drive is not ready in time.
 *
 ****************************************************************/
static void Irq_Handler (int irq, void *dev_id, struct pt_regs *regs)
	{
	struct Scsi_Host   *shost = NULL;	// Pointer to host data block
	PADAPTER2220I		padapter;		// Pointer to adapter control structure
	POUR_DEVICE			pdev;
	Scsi_Cmnd		   *SCpnt;
	UCHAR				status;
	UCHAR				status1;
	int					z;
	ULONG				zl;
#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    int					flags;
#else /* version >= v2.1.95 */
    unsigned long		flags;
#endif /* version >= v2.1.95 */

#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    /* Disable interrupts, if they aren't already disabled. */
    save_flags (flags);
    cli ();
#else /* version >= v2.1.95 */
    /*
     * Disable interrupts, if they aren't already disabled and acquire
     * the I/O spinlock.
     */
    spin_lock_irqsave (&io_request_lock, flags);
#endif /* version >= v2.1.95 */

//	DEB (printk ("\npci2220i recieved interrupt\n"));

	for ( z = 0; z < NumAdapters;  z++ )								// scan for interrupt to process
		{
		if ( PsiHost[z]->irq == (UCHAR)(irq & 0xFF) )
			{
			if ( inw_p (HOSTDATA(PsiHost[z])->regIrqControl) & 0x8000 )
				{
				shost = PsiHost[z];
				break;
				}
			}
		}

	if ( !shost )
		{
		DEB (printk ("\npci2220i: not my interrupt"));
		goto irq_return;
		}

	padapter = HOSTDATA(shost);
	pdev = padapter->pdev;
	SCpnt = padapter->SCpnt;

	if ( !padapter->expectingIRQ || !(SCpnt || padapter->reconPhase) )
		{
		DEB(printk ("\npci2220i Unsolicited interrupt\n"));
		STOP_HERE ();
		goto irq_return;
		}
	padapter->expectingIRQ = 0;
	outb_p (0x08, padapter->regDmaCmdStat);									// cancel interrupt from DMA engine

	if ( padapter->failinprog )
		{
		DEB (printk ("\npci2220i interrupt failover complete"));
		padapter->failinprog = FALSE;
		status = inb_p (padapter->regStatCmd);								// read the device status
		if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
			{
			DEB (printk ("\npci2220i: interrupt failover error from drive %X", status));
			padapter->cmd = 0;
			}
		else
			{
			DEB (printk ("\npci2220i: restarting failed opertation."));
			pdev->spigot = (padapter->survivor) ? 2 : 1;
			del_timer (&padapter->timer);
			if ( padapter->reconPhase )
				OpDone (padapter, DID_OK << 16);
			else
				Pci2220i_QueueCommand (SCpnt, SCpnt->scsi_done);
			goto irq_return;		
			}
		}

	if ( padapter->reconPhase )
		{
		switch ( padapter->reconPhase )
			{
			case RECON_PHASE_MARKING:
			case RECON_PHASE_LAST:
				status = inb_p (padapter->regStatCmd);						// read the device status
				del_timer (&padapter->timer);
				if ( padapter->reconPhase == RECON_PHASE_LAST )
					{
					if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
						{
						padapter->survivor = (pdev->spigot ^ 3) >> 1;
						DEB (printk ("\npci2220i: FAILURE 10"));
						if ( InitFailover (padapter, pdev) )
							OpDone (padapter, DecodeError (padapter, status));
						goto irq_return;
						}
					if ( WriteSignature (padapter, pdev, pdev->hotRecon, pdev->mirrorRecon) )
						{
						padapter->survivor = (pdev->spigot) >> 1;
						DEB (printk ("\npci2220i: FAILURE 11"));
						if ( InitFailover (padapter, pdev) )
							OpDone (padapter, DecodeError (padapter, status));
						goto irq_return;
						}
					padapter->reconPhase = RECON_PHASE_END;	
					goto irq_return;
					}
				OpDone (padapter, DID_OK << 16);
				goto irq_return;

			case RECON_PHASE_READY:
				status = inb_p (padapter->regStatCmd);						// read the device status
				if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{
					del_timer (&padapter->timer);
					OpDone (padapter, DecodeError (padapter, status));
					goto irq_return;
					}
				SelectSpigot (padapter, pdev->hotRecon);
				if ( WaitDrq (padapter) )
					{
					del_timer (&padapter->timer);
					padapter->survivor = (pdev->spigot) >> 1;
				DEB (printk ("\npci2220i: FAILURE 12"));
					if ( InitFailover (padapter, pdev) )
						OpDone (padapter, DecodeError (padapter, status));
					goto irq_return;
					}
				SelectSpigot (padapter, pdev->spigot | 0x40);
				padapter->reconPhase = RECON_PHASE_COPY;
				padapter->expectingIRQ = TRUE;
				if ( padapter->timingPIO )
					{
					insw (padapter->regData, padapter->kBuffer, padapter->reconSize * (BYTES_PER_SECTOR / 2));
					}
				else
					{
					outl (padapter->timingAddress, padapter->regDmaAddrLoc);
					outl (virt_to_bus (padapter->kBuffer), padapter->regDmaAddrPci);
					outl (padapter->reconSize * BYTES_PER_SECTOR, padapter->regDmaCount);
					outb_p (8, padapter->regDmaDesc);						// read operation
					outb_p (1, padapter->regDmaMode);						// no interrupt
					outb_p (0x03, padapter->regDmaCmdStat);					// kick the DMA engine in gear
					}
				goto irq_return;

			case RECON_PHASE_COPY:
				pdev->DiskMirror[pdev->mirrorRecon].reconstructPoint += padapter->reconSize;

			case RECON_PHASE_UPDATE:
				SelectSpigot (padapter, pdev->hotRecon | 0x80);
				status = inb_p (padapter->regStatCmd);						// read the device status
				del_timer (&padapter->timer);
				if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{
					padapter->survivor = (pdev->spigot) >> 1;
					DEB (printk ("\npci2220i: FAILURE 13"));
					if ( InitFailover (padapter, pdev) )
						OpDone (padapter, DecodeError (padapter, status));
					goto irq_return;
					}
				OpDone (padapter, DID_OK << 16);
				goto irq_return;

			case RECON_PHASE_END:
				status = inb_p (padapter->regStatCmd);						// read the device status
				del_timer (&padapter->timer);
				if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{
					padapter->survivor = (pdev->spigot) >> 1;
				DEB (printk ("\npci2220i: FAILURE 14"));
					if ( InitFailover (padapter, pdev) )
						OpDone (padapter, DecodeError (padapter, status));
					goto irq_return;
					}
				padapter->reconOn = FALSE;
				pdev->hotRecon = 0;
				OpDone (padapter, DID_OK << 16);
				goto irq_return;

			default:
				goto irq_return;
			}
		}
		
	switch ( padapter->cmd )												// decide how to handle the interrupt
		{
		case READ_CMD:
			if ( padapter->sectorCount )
				{
				status = inb_p (padapter->regStatCmd);						// read the device status
				if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{
					if ( pdev->raid )
						{
						padapter->survivor = (pdev->spigot ^ 3) >> 1;
						del_timer (&padapter->timer);
				DEB (printk ("\npci2220i: FAILURE 15"));
						if ( !InitFailover (padapter, pdev) )
							goto irq_return;
						}
					break;	
					}
				if ( padapter->timingPIO )
					{
					zl = (padapter->sectorCount > MAX_BUS_MASTER_BLOCKS) ? MAX_BUS_MASTER_BLOCKS : padapter->sectorCount;
					insw (padapter->regData, padapter->buffer, zl * (BYTES_PER_SECTOR / 2));
					padapter->sectorCount -= zl;
					padapter->buffer += zl * BYTES_PER_SECTOR;
					if ( !padapter->sectorCount )
						{
						status = 0;
						break;
						}
					}
				else
					BusMaster (padapter, 1, 1);
				padapter->expectingIRQ = TRUE;
				goto irq_return;
				}
			status = 0;
			break;

		case WRITE_CMD:
			SelectSpigot (padapter, pdev->spigot | 0x80);				
			status = inb_p (padapter->regStatCmd);								// read the device status
			if ( pdev->raid )
				{
				SelectSpigot (padapter, (pdev->spigot ^ 3) | 0x80);				
				status1 = inb_p (padapter->regStatCmd);							// read the device status
				}
			else
				status1 = 0;
		
			if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
				{	
				if ( pdev->raid && !(status1 & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT)) )
					{
					padapter->survivor = (pdev->spigot ^ 3) >> 1;
					del_timer (&padapter->timer);
				SelectSpigot (padapter, pdev->spigot | 0x80);
				DEB (printk ("\npci2220i: FAILURE 16  status = %X  error = %X", status, inb_p (padapter->regError)));
					if ( !InitFailover (padapter, pdev) )
						goto irq_return;
					}
				break;
				}
			if ( pdev->raid )
				{
				if ( status1 & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{	
					padapter->survivor = pdev->spigot >> 1;
					del_timer (&padapter->timer);
				DEB (printk ("\npci2220i: FAILURE 17  status = %X  error = %X", status1, inb_p (padapter->regError)));
					if ( !InitFailover (padapter, pdev) )
						goto irq_return;
					status = status1;
					break;
					}
				if ( padapter->sectorCount )
					{
					status = WriteDataBoth (padapter);
					if ( status )
						{
						padapter->survivor = (status ^ 3) >> 1;
						del_timer (&padapter->timer);
				DEB (printk ("\npci2220i: FAILURE 18"));
						if ( !InitFailover (padapter, pdev) )
							goto irq_return;
						SelectSpigot (padapter, status | 0x80);				
						status = inb_p (padapter->regStatCmd);								// read the device status
						break;
						}
					padapter->expectingIRQ = TRUE;
					goto irq_return;
					}
				status = 0;
				break;
				}
			if ( padapter->sectorCount )	
				{	
				SelectSpigot (padapter, pdev->spigot);
				status = WriteData (padapter);
				if ( status )
					break;
				padapter->expectingIRQ = TRUE;
				goto irq_return;
				}
			status = 0;
			break;

		case IDE_COMMAND_IDENTIFY:
			{
			PINQUIRYDATA	pinquiryData  = SCpnt->request_buffer;
			PIDENTIFY_DATA	pid = (PIDENTIFY_DATA)padapter->kBuffer;

			status = inb_p (padapter->regStatCmd);
			if ( status & IDE_STATUS_DRQ )
				{
				insw (padapter->regData, pid, sizeof (IDENTIFY_DATA) >> 1);

				memset (pinquiryData, 0, SCpnt->request_bufflen);		// Zero INQUIRY data structure.
				pinquiryData->DeviceType = 0;
				pinquiryData->Versions = 2;
				pinquiryData->AdditionalLength = 35 - 4;

				// Fill in vendor identification fields.
				for ( z = 0;  z < 20;  z += 2 )
					{
					pinquiryData->VendorId[z]	  = ((UCHAR *)pid->ModelNumber)[z + 1];
					pinquiryData->VendorId[z + 1] = ((UCHAR *)pid->ModelNumber)[z];
					}

				// Initialize unused portion of product id.
				for ( z = 0;  z < 4;  z++ )
					pinquiryData->ProductId[12 + z] = ' ';

				// Move firmware revision from IDENTIFY data to
				// product revision in INQUIRY data.
				for ( z = 0;  z < 4;  z += 2 )
					{
					pinquiryData->ProductRevisionLevel[z]	 = ((UCHAR *)pid->FirmwareRevision)[z + 1];
					pinquiryData->ProductRevisionLevel[z + 1] = ((UCHAR *)pid->FirmwareRevision)[z];
					}
				if ( pdev == padapter->device )
					*((USHORT *)(&pinquiryData->VendorSpecific)) = DEVICE_DALE_1;
				
				status = 0;
				}
			break;
			}

		default:
			status = 0;
			break;
		}

	del_timer (&padapter->timer);
	if ( status )
		{
		DEB (printk ("\npci2220i Interupt hanlder return error"));
		zl = DecodeError (padapter, status);
		}
	else
		zl = DID_OK << 16;

	OpDone (padapter, zl);
irq_return:;
#if LINUX_VERSION_CODE < LINUXVERSION(2,1,95)
    /*
     * Restore the original flags which will enable interrupts
     * if and only if they were enabled on entry.
     */
    restore_flags (flags);
#else /* version >= v2.1.95 */
    /*
     * Release the I/O spinlock and restore the original flags
     * which will enable interrupts if and only if they were
     * enabled on entry.
     */
    spin_unlock_irqrestore (&io_request_lock, flags);
#endif /* version >= v2.1.95 */
	}
/****************************************************************
 *	Name:	Pci2220i_QueueCommand
 *
 *	Description:	Process a queued command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *					done  - Pointer to done function to call.
 *
 *	Returns:		Status code.
 *
 ****************************************************************/
int Pci2220i_QueueCommand (Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
	{
	UCHAR		   *cdb = (UCHAR *)SCpnt->cmnd;					// Pointer to SCSI CDB
	PADAPTER2220I	padapter = HOSTDATA(SCpnt->host);			// Pointer to adapter control structure
	POUR_DEVICE		pdev	 = &padapter->device[SCpnt->target];// Pointer to device information
	UCHAR			rc;											// command return code
	int				z; 
	PDEVICE_RAID1	pdr;

	SCpnt->scsi_done = done;
	padapter->buffer = SCpnt->request_buffer;
	padapter->SCpnt = SCpnt;  									// Save this command data
	if ( !done )
		{
		printk("pci2220i_queuecommand: %02X: done can't be NULL\n", *cdb);
		return 0;
		}
	
	if ( padapter->reconPhase )
		return 0;
	if ( padapter->reconTimer.data )
		{
		del_timer (&padapter->reconTimer);
		padapter->reconTimer.data = 0;
		}
		
	if ( !pdev->device || SCpnt->lun )
		{
		OpDone (padapter, DID_BAD_TARGET << 16);
		return 0;
		}

	
	switch ( *cdb )
		{
		case SCSIOP_INQUIRY:   					// inquiry CDB
			{
			if ( cdb[2] == SC_MY_RAID )
				{
				switch ( cdb[3] ) 
					{
					case MY_SCSI_REBUILD:
						padapter->reconOn = padapter->reconIsStarting = TRUE;
						OpDone (padapter, DID_OK << 16);
						break;
					case MY_SCSI_ALARMMUTE:
						MuteAlarm (padapter);
						OpDone (padapter, DID_OK << 16);
						break;
					case MY_SCSI_DEMOFAIL:
						padapter->demoFail = TRUE;				
						OpDone (padapter, DID_OK << 16);
						break;
					default:
						z = cdb[5];				// get index
						pdr = (PDEVICE_RAID1)SCpnt->request_buffer;
						if ( padapter->raidData[z] )
							{
							memcpy (&pdr->DiskRaid1, padapter->raidData[z], sizeof (DISK_MIRROR));
							pdr->TotalSectors = padapter->device[0].blocks;
							}
						else
							memset (pdr, 0, sizeof (DEVICE_RAID1));
						OpDone (padapter, DID_OK << 16);
						break;
					}	
				return 0;
				}
			padapter->cmd = IDE_COMMAND_IDENTIFY;
			break;
			}

		case SCSIOP_TEST_UNIT_READY:			// test unit ready CDB
			OpDone (padapter, DID_OK << 16);
			return 0;
		case SCSIOP_READ_CAPACITY:			  	// read capctiy CDB
			{
			PREAD_CAPACITY_DATA	pdata = (PREAD_CAPACITY_DATA)SCpnt->request_buffer;

			pdata->blksiz = 0x20000;
			XANY2SCSI ((UCHAR *)&pdata->blks, pdev->blocks);
			OpDone (padapter, DID_OK << 16);
			return 0;
			}
		case SCSIOP_VERIFY:						// verify CDB
			padapter->startSector = XSCSI2LONG (&cdb[2]);
			padapter->sectorCount = (UCHAR)((USHORT)cdb[8] | ((USHORT)cdb[7] << 8));
			padapter->cmd = IDE_COMMAND_VERIFY;
			break;
		case SCSIOP_READ:						// read10 CDB
			padapter->startSector = XSCSI2LONG (&cdb[2]);
			padapter->sectorCount = (USHORT)cdb[8] | ((USHORT)cdb[7] << 8);
			padapter->cmd = READ_CMD;
			break;
		case SCSIOP_READ6:						// read6  CDB
			padapter->startSector = SCSI2LONG (&cdb[1]);
			padapter->sectorCount = cdb[4];
			padapter->cmd = READ_CMD;
			break;
		case SCSIOP_WRITE:						// write10 CDB
			padapter->startSector = XSCSI2LONG (&cdb[2]);
			padapter->sectorCount = (USHORT)cdb[8] | ((USHORT)cdb[7] << 8);
			padapter->cmd = WRITE_CMD;
			break;
		case SCSIOP_WRITE6:						// write6  CDB
			padapter->startSector = SCSI2LONG (&cdb[1]);
			padapter->sectorCount = cdb[4];
			padapter->cmd = WRITE_CMD;
			break;
		default:
			DEB (printk ("pci2220i_queuecommand: Unsupported command %02X\n", *cdb));
			OpDone (padapter, DID_ERROR << 16);
			return 0;
		}

	if ( padapter->reconPhase )
		return 0;
	
	padapter->pdev = pdev;

	while ( padapter->demoFail )
		{
		padapter->demoFail = FALSE;
		if ( !pdev->raid || 
			 (pdev->DiskMirror[0].status & UCBF_SURVIVOR) || 
			 (pdev->DiskMirror[1].status & UCBF_SURVIVOR) )
			{
			break;
			}
		if ( pdev->DiskMirror[0].status & UCBF_REBUILD )
			padapter->survivor = 1;
		else
			padapter->survivor = 0;
				DEB (printk ("\npci2220i: FAILURE 19"));
		if ( InitFailover (padapter, pdev ) )
			break;
		return 0;
		}

	StartTimer (padapter);
	if ( pdev->raid && (padapter->cmd == WRITE_CMD) )
		{
		rc = IdeCmdBoth (padapter);
		if ( !rc )
			rc = WriteDataBoth (padapter);
		if ( rc )
			{
			del_timer (&padapter->timer);
			padapter->expectingIRQ = 0;
			padapter->survivor = (rc ^ 3) >> 1;
				DEB (printk ("\npci2220i: FAILURE 20"));
			if ( InitFailover (padapter, pdev) )
				{
				OpDone (padapter, DID_ERROR << 16);
				return 0;
				}
			}
		}
	else
		{
		rc = IdeCmd (padapter, pdev);
		if ( (padapter->cmd == WRITE_CMD) && !rc )
			rc = WriteData (padapter);
		if ( rc )
			{
			del_timer (&padapter->timer);
			padapter->expectingIRQ = 0;
			if ( pdev->raid )
				{
				padapter->survivor = (pdev->spigot ^ 3) >> 1;
				DEB (printk ("\npci2220i: FAILURE 21"));
				if ( !InitFailover (padapter, pdev) )
					return 0;
				}
			OpDone (padapter, DID_ERROR << 16);
			return 0;
			}
		}
	return 0;
	}

static void internal_done(Scsi_Cmnd *SCpnt)
	{
	SCpnt->SCp.Status++;
	}
/****************************************************************
 *	Name:	Pci2220i_Command
 *
 *	Description:	Process a command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Status code.
 *
 ****************************************************************/
int Pci2220i_Command (Scsi_Cmnd *SCpnt)
	{
	Pci2220i_QueueCommand (SCpnt, internal_done);
    SCpnt->SCp.Status = 0;
	while (!SCpnt->SCp.Status)
		barrier ();
	return SCpnt->result;
	}
/****************************************************************
 *	Name:			ReadFlash
 *
 *	Description:	Read information from controller Flash memory.
 *
 *	Parameters:		padapter - Pointer to host interface data structure.
 *					pdata	 - Pointer to data structures.
 *					base	 - base address in Flash.
 *					length	 - lenght of data space in bytes.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
VOID ReadFlash (PADAPTER2220I padapter, VOID *pdata, ULONG base, ULONG length)
	{
	ULONG	 oldremap;
	UCHAR	 olddesc;
	ULONG	 z;
	UCHAR	*pd = (UCHAR *)pdata;

	oldremap = inl (padapter->regRemap);									// save values to restore later
	olddesc  = inb_p (padapter->regDesc);

	outl (base | 1, padapter->regRemap);									// remap to Flash space as specified
	outb_p (0x40, padapter->regDesc);										// describe remap region as 8 bit
	for ( z = 0;  z < length;  z++)											// get "length" data count
		*pd++ = inb_p (padapter->regBase + z);								// read in the data

	outl (oldremap, padapter->regRemap);									// restore remap register values
	outb_p (olddesc, padapter->regDesc);
	}
/****************************************************************
 *	Name:	Pci2220i_Detect
 *
 *	Description:	Detect and initialize our boards.
 *
 *	Parameters:		tpnt - Pointer to SCSI host template structure.
 *
 *	Returns:		Number of adapters installed.
 *
 ****************************************************************/
int Pci2220i_Detect (Scsi_Host_Template *tpnt)
	{
	int					found = 0;
	int					installed = 0;
	struct Scsi_Host   *pshost;
	PADAPTER2220I	    padapter;
	int					unit;
	int					z;
	USHORT				zs;
	USHORT				raidon = FALSE;
	int					setirq;
	UCHAR				spigot1 = FALSE;
	UCHAR				spigot2 = FALSE;
#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
	struct pci_dev	   *pdev = NULL;
#else
	UCHAR				pci_bus, pci_device_fn;
#endif

#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
	if ( !pci_present () )
#else
	if ( !pcibios_present () )
#endif
		{
		printk ("pci2220i: PCI BIOS not present\n");
		return 0;
		}

#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
	while ( (pdev = pci_find_device (VENDOR_PSI, DEVICE_DALE_1, pdev)) != NULL )
#else
	while ( !pcibios_find_device (VENDOR_PSI, DEVICE_DALE_1, found, &pci_bus, &pci_device_fn) )
#endif
		{
		pshost = scsi_register (tpnt, sizeof(ADAPTER2220I));
		padapter = HOSTDATA(pshost);
		memset (padapter, 0, sizeof (ADAPTER2220I));

		zs = pdev->resource[1].start;
		padapter->basePort = zs;
		padapter->regRemap		= zs + RTR_LOCAL_REMAP;				// 32 bit local space remap
		padapter->regDesc		= zs + RTR_REGIONS;	  				// 32 bit local region descriptor
		padapter->regRange		= zs + RTR_LOCAL_RANGE;				// 32 bit local range
		padapter->regIrqControl	= zs + RTR_INT_CONTROL_STATUS;		// 16 bit interupt control and status
		padapter->regScratchPad	= zs + RTR_MAILBOX;	  				// 16 byte scratchpad I/O base address

		zs = pdev->resource[2].start;
		padapter->regBase		= zs;
		padapter->regData		= zs + REG_DATA;					// data register I/O address
		padapter->regError		= zs + REG_ERROR;					// error register I/O address
		padapter->regSectCount	= zs + REG_SECTOR_COUNT;			// sector count register I/O address
		padapter->regLba0		= zs + REG_LBA_0;					// least significant byte of LBA
		padapter->regLba8		= zs + REG_LBA_8;					// next least significant byte of LBA
		padapter->regLba16		= zs + REG_LBA_16;					// next most significan byte of LBA
		padapter->regLba24		= zs + REG_LBA_24;					// head and most 4 significant bits of LBA
		padapter->regStatCmd	= zs + REG_STAT_CMD;				// status on read and command on write register
		padapter->regStatSel	= zs + REG_STAT_SEL;				// board status on read and spigot select on write register
		padapter->regFail		= zs + REG_FAIL;
		padapter->regAltStat	= zs + REG_ALT_STAT;

		padapter->regDmaDesc	= zs + RTL_DMA1_DESC_PTR;			// address of the DMA discriptor register for direction of transfer
		padapter->regDmaCmdStat	= zs + RTL_DMA_COMMAND_STATUS + 1;	// Byte #1 of DMA command status register
		padapter->regDmaAddrPci	= zs + RTL_DMA1_PCI_ADDR;			// 32 bit register for PCI address of DMA
		padapter->regDmaAddrLoc	= zs + RTL_DMA1_LOCAL_ADDR;			// 32 bit register for local bus address of DMA
		padapter->regDmaCount	= zs + RTL_DMA1_COUNT;				// 32 bit register for DMA transfer count
		padapter->regDmaMode	= zs + RTL_DMA1_MODE + 1;			// 32 bit register for DMA mode control

		if ( !inb_p (padapter->regScratchPad + DALE_NUM_DRIVES) )	// if no devices on this board
			goto unregister;

#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
		pshost->irq = pdev->irq;
#else
		pcibios_read_config_byte (pci_bus, pci_device_fn, PCI_INTERRUPT_LINE, &pshost->irq);
#endif
		setirq = 1;
		for ( z = 0;  z < installed;  z++ )							// scan for shared interrupts
			{
			if ( PsiHost[z]->irq == pshost->irq )					// if shared then, don't posses
				setirq = 0;
			}
		if ( setirq )												// if not shared, posses
			{
			if ( request_irq (pshost->irq, Irq_Handler, SA_SHIRQ, "pci2220i", padapter) < 0 )
				{
				if ( request_irq (pshost->irq, Irq_Handler, SA_INTERRUPT | SA_SHIRQ, "pci2220i", padapter) < 0 )
					{
					printk ("Unable to allocate IRQ for PCI-2220I controller.\n");
					goto unregister;
					}
				}
			padapter->irqOwned = pshost->irq;						// set IRQ as owned
			}
		padapter->kBuffer = kmalloc (SECTORSXFER * BYTES_PER_SECTOR, GFP_DMA | GFP_ATOMIC);
		if ( !padapter->kBuffer )
			{
			printk ("Unable to allocate DMA buffer for PCI-2220I controller.\n");
#if LINUX_VERSION_CODE < LINUXVERSION(1,3,70)
			free_irq (pshost->irq);
#else /* version >= v1.3.70 */
			free_irq (pshost->irq, padapter);
#endif /* version >= v1.3.70 */
			goto unregister;
			}
		PsiHost[installed]	= pshost;								// save SCSI_HOST pointer

		pshost->io_port		= padapter->basePort;
		pshost->n_io_port	= 0xFF;
		pshost->unique_id	= padapter->regBase;
		pshost->max_id		= 4;

		outb_p (0x01, padapter->regRange);							// fix our range register because other drivers want to tromp on it

		padapter->timingMode = inb_p (padapter->regScratchPad + DALE_TIMING_MODE);
		if ( padapter->timingMode >= 2 )
			padapter->timingAddress	= ModeArray[padapter->timingMode - 2];
		else
			padapter->timingPIO = TRUE;
			
		ReadFlash (padapter, &DaleSetup, DALE_FLASH_SETUP, sizeof (SETUP));
		for ( z = 0;  z < inb_p (padapter->regScratchPad + DALE_NUM_DRIVES);  ++z )
			{
			unit = inb_p (padapter->regScratchPad + DALE_CHANNEL_DEVICE_0 + z) & 0x0F;
			padapter->device[z].device	 = inb_p (padapter->regScratchPad + DALE_SCRATH_DEVICE_0 + unit);
			padapter->device[z].byte6	 = (UCHAR)(((unit & 1) << 4) | 0xE0);
			padapter->device[z].spigot	 = (UCHAR)(1 << (unit >> 1));
			padapter->device[z].sectors	 = DaleSetup.setupDevice[unit].sectors;
			padapter->device[z].heads	 = DaleSetup.setupDevice[unit].heads;
			padapter->device[z].cylinders = DaleSetup.setupDevice[unit].cylinders;
			padapter->device[z].blocks	 = DaleSetup.setupDevice[unit].blocks;

			if ( !z )
				{
				ReadFlash (padapter, &DiskMirror, DALE_FLASH_RAID, sizeof (DiskMirror));
				DiskMirror[0].status = inb_p (padapter->regScratchPad + DALE_RAID_0_STATUS);		
				DiskMirror[1].status = inb_p (padapter->regScratchPad + DALE_RAID_1_STATUS);		
				if ( (DiskMirror[0].signature == SIGNATURE) && (DiskMirror[1].signature == SIGNATURE) &&
				     (DiskMirror[0].pairIdentifier == (DiskMirror[1].pairIdentifier ^ 1)) )
					{			 
					raidon = TRUE;
					}	

				memcpy (padapter->device[z].DiskMirror, DiskMirror, sizeof (DiskMirror));
				padapter->raidData[0] = &padapter->device[z].DiskMirror[0];
				padapter->raidData[2] = &padapter->device[z].DiskMirror[1];
				
				if ( raidon )
					{
					padapter->device[z].lastsectorlba[0] = InlineIdentify (padapter, 1, 0);
					padapter->device[z].lastsectorlba[1] = InlineIdentify (padapter, 2, 0);
						
					if ( !(DiskMirror[1].status & UCBF_SURVIVOR) && padapter->device[z].lastsectorlba[0] )
						spigot1 = TRUE;
					if ( !(DiskMirror[0].status & UCBF_SURVIVOR) && padapter->device[z].lastsectorlba[1] )
						spigot2 = TRUE;
					if ( DiskMirror[0].status & UCBF_SURVIVOR & DiskMirror[1].status & UCBF_SURVIVOR )
						spigot1 = TRUE;

					if ( spigot1 && (DiskMirror[0].status & UCBF_REBUILD) )
						InlineReadSignature (padapter, &padapter->device[z], 0);
					if ( spigot2 && (DiskMirror[1].status & UCBF_REBUILD) )
						InlineReadSignature (padapter, &padapter->device[z], 1);

					if ( spigot1 && spigot2 )
						{
						padapter->device[z].raid = 1;
						if ( DiskMirror[0].status & UCBF_REBUILD )
							padapter->device[z].spigot = 2;
						else
							padapter->device[z].spigot = 1;
						if ( (DiskMirror[0].status & UCBF_REBUILD) || (DiskMirror[1].status & UCBF_REBUILD) )
							{
							padapter->reconOn = padapter->reconIsStarting = TRUE;
							}
						}
					else
						{
						if ( spigot1 )
							{
							if ( DiskMirror[0].status & UCBF_REBUILD )
								goto unregister;
							DiskMirror[0].status = UCBF_MIRRORED | UCBF_SURVIVOR;
							padapter->device[z].spigot = 1;
							}
						else
							{
							if ( DiskMirror[1].status & UCBF_REBUILD )
								goto unregister;
							DiskMirror[1].status = UCBF_MIRRORED | UCBF_SURVIVOR;
							padapter->device[z].spigot = 2;
							}
						if ( DaleSetup.rebootRebuil )
							padapter->reconOn = padapter->reconIsStarting = TRUE;
						}
				
					break;
					}
				}
			}
			
		init_timer (&padapter->timer);
		padapter->timer.function = TimerExpiry;
		padapter->timer.data = (unsigned long)padapter;
		init_timer (&padapter->reconTimer);
		padapter->reconTimer.function = ReconTimerExpiry;
		padapter->reconTimer.data = (unsigned long)padapter;
		printk("\nPCI-2220I EIDE CONTROLLER: at I/O = %X/%X  IRQ = %d\n", padapter->basePort, padapter->regBase, pshost->irq);
		printk("Version %s, Compiled %s %s\n\n", PCI2220I_VERSION, __DATE__, __TIME__);
		found++;
		if ( ++installed < MAXADAPTER )
			continue;
		break;;
unregister:;
		scsi_unregister (pshost);
		found++;
		}
	
	NumAdapters = installed;
	return installed;
	}
/****************************************************************
 *	Name:	Pci2220i_Abort
 *
 *	Description:	Process the Abort command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Allways snooze.
 *
 ****************************************************************/
int Pci2220i_Abort (Scsi_Cmnd *SCpnt)
	{
	return SCSI_ABORT_SNOOZE;
	}
/****************************************************************
 *	Name:	Pci2220i_Reset
 *
 *	Description:	Process the Reset command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *					flags - Flags about the reset command
 *
 *	Returns:		No active command at this time, so this means
 *					that each time we got some kind of response the
 *					last time through.  Tell the mid-level code to
 *					request sense information in order to decide what
 *					to do next.
 *
 ****************************************************************/
int Pci2220i_Reset (Scsi_Cmnd *SCpnt, unsigned int reset_flags)
	{
	return SCSI_RESET_PUNT;
	}
/****************************************************************
 *	Name:	Pci2220i_Release
 *
 *	Description:	Release resources allocated for a single each adapter.
 *
 *	Parameters:		pshost - Pointer to SCSI command structure.
 *
 *	Returns:		zero.
 *
 ****************************************************************/
int Pci2220i_Release (struct Scsi_Host *pshost)
	{
    PADAPTER2220I	padapter = HOSTDATA (pshost);

	if ( padapter->reconOn )
		{
		padapter->reconOn = FALSE;						// shut down the hot reconstruct
		if ( padapter->reconPhase )
			udelay (300000);
		if ( padapter->reconTimer.data )				// is the timer running?
			{
			del_timer (&padapter->reconTimer);
			padapter->reconTimer.data = 0;
			}
		}

	// save RAID status on the board
	outb_p (DiskMirror[0].status, padapter->regScratchPad + DALE_RAID_0_STATUS);		
	outb_p (DiskMirror[1].status, padapter->regScratchPad + DALE_RAID_1_STATUS);		

	if ( padapter->irqOwned )
#if LINUX_VERSION_CODE < LINUXVERSION(1,3,70)
		free_irq (pshost->irq);
#else /* version >= v1.3.70 */
		free_irq (pshost->irq, padapter);
#endif /* version >= v1.3.70 */
    release_region (pshost->io_port, pshost->n_io_port);
	kfree (padapter->kBuffer);
    scsi_unregister(pshost);
    return 0;
	}

#include "sd.h"

/****************************************************************
 *	Name:	Pci2220i_BiosParam
 *
 *	Description:	Process the biosparam request from the SCSI manager to
 *					return C/H/S data.
 *
 *	Parameters:		disk - Pointer to SCSI disk structure.
 *					dev	 - Major/minor number from kernel.
 *					geom - Pointer to integer array to place geometry data.
 *
 *	Returns:		zero.
 *
 ****************************************************************/
int Pci2220i_BiosParam (Scsi_Disk *disk, kdev_t dev, int geom[])
	{
	POUR_DEVICE	pdev;

	pdev = &(HOSTDATA(disk->device->host)->device[disk->device->id]);

	geom[0] = pdev->heads;
	geom[1] = pdev->sectors;
	geom[2] = pdev->cylinders;
	return 0;
	}


#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = PCI2220I;

#include "scsi_module.c"
#endif

/*+M*************************************************************************
 * Perceptive Solutions, Inc. PCI-2000 device driver proc support for Linux.
 *
 * Copyright (c) 1997 Perceptive Solutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	File Name:		pci2220i.c
 *
 *	Description:	SCSI driver for the PCI2220I EIDE interface card.
 *
 *-M*************************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/spinlock.h>
#include <asm/io.h>
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"

#include "pci2220i.h"
#include "psi_dale.h"

#include<linux/stat.h>

struct proc_dir_entry Proc_Scsi_Pci2220i =
	{ PROC_SCSI_PCI2220I, 7, "pci2220i", S_IFDIR | S_IRUGO | S_IXUGO, 2 };

//#define DEBUG 1

#ifdef DEBUG
#define DEB(x) x
#define STOP_HERE	{int st;for(st=0;st<100;st++){st=1;}}
#else
#define DEB(x)
#define STOP_HERE
#endif

#define MAXADAPTER 4	/* Increase this and the sizes of the arrays below, if you need more. */

#define	MAX_BUS_MASTER_BLOCKS	1		// This is the maximum we can bus master for (1024 bytes)

#define	PORT_DATA				0
#define	PORT_ERROR				1
#define	PORT_SECTOR_COUNT		2
#define	PORT_LBA_0				3
#define	PORT_LBA_8				4
#define	PORT_LBA_16				5
#define	PORT_LBA_24				6
#define	PORT_STAT_CMD			7
#define	PORT_STAT_SEL			8
#define	PORT_FAIL				9
#define	PORT_ALT_STAT		   	10

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
	}	OUR_DEVICE, *POUR_DEVICE;

typedef struct
	{
	USHORT		 ports[12];
	USHORT		 regDmaDesc;					// address of the DMA discriptor register for direction of transfer
	USHORT		 regDmaCmdStat;					// Byte #1 of DMA command status register
	USHORT		 regDmaAddrPci;					// 32 bit register for PCI address of DMA
	USHORT		 regDmaAddrLoc;					// 32 bit register for local bus address of DMA
	USHORT		 regDmaCount;					// 32 bit register for DMA transfer count
	USHORT		 regDmaMode;						// 32 bit register for DMA mode control
	USHORT		 regRemap;						// 32 bit local space remap
	USHORT		 regDesc;						// 32 bit local region descriptor
	USHORT		 regRange;						// 32 bit local range
	USHORT		 regIrqControl;					// 16 bit Interrupt enable/disable and status
	USHORT		 regScratchPad;					// scratch pad I/O base address
	USHORT		 regBase;						// Base I/O register for data space
	USHORT		 basePort;						// PLX base I/O port
	USHORT		 timingMode;					// timing mode currently set for adapter
	ULONG		 timingAddress;					// address to use on adapter for current timing mode
	OUR_DEVICE	 device[4];
	IDE_STRUCT	 ide;
	ULONG		 startSector;
	USHORT		 sectorCount;
	Scsi_Cmnd	*SCpnt;
	VOID		*buffer;
	USHORT		 expectingIRQ;
	USHORT		 readPhase;
	}	ADAPTER2220I, *PADAPTER2220I;

#define HOSTDATA(host) ((PADAPTER2220I)&host->hostdata)


static struct	Scsi_Host 	   *PsiHost[MAXADAPTER] = {NULL,};  // One for each adapter
static			int				NumAdapters = 0;
static			IDENTIFY_DATA	identifyData;
static			SETUP			DaleSetup;

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
	ULONG	timer;
	USHORT *pports = padapter->ports;

	timer = jiffies + TIMEOUT_DRQ;								// calculate the timeout value
	do  {
		if ( inb_p (pports[PORT_STAT_CMD]) & IDE_STATUS_DRQ )
			{
			outb_p (0, padapter->regDmaDesc);							// write operation
			outl (padapter->timingAddress, padapter->regDmaAddrLoc);
			outl (virt_to_bus (padapter->buffer), padapter->regDmaAddrPci);
			outl ((ULONG)padapter->ide.ide.ide[2] * (ULONG)512, padapter->regDmaCount);
			outb_p (1, padapter->regDmaMode);							// interrupts off
			outb_p (0x03, padapter->regDmaCmdStat);						// kick the DMA engine in gear
			return 0;
			}
		}	while ( timer > jiffies );									// test for timeout

	padapter->ide.ide.ides.cmd = 0;									// null out the command byte
	return 1;
	}
/****************************************************************
 *	Name:	IdeCmd	:LOCAL
 *
 *	Description:	Process a queued command from the SCSI manager.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		Zero if no error or status register contents on error.
 *
 ****************************************************************/
static UCHAR IdeCmd (PADAPTER2220I padapter)
	{
	ULONG	timer;
	USHORT *pports = padapter->ports;
	UCHAR	status;

	outb_p (padapter->ide.ide.ides.spigot, pports[PORT_STAT_SEL]);	// select the spigot
	outb_p (padapter->ide.ide.ide[6], pports[PORT_LBA_24]);			// select the drive
	timer = jiffies + TIMEOUT_READY;							// calculate the timeout value
	DEB(printk ("\npci2220i Issueing new command: 0x%X",padapter->ide.ide.ides.cmd));
	do  {
		status = inb_p (padapter->ports[PORT_STAT_CMD]);
		if ( status & IDE_STATUS_DRDY )
			{
			outb_p (padapter->ide.ide.ide[2], pports[PORT_SECTOR_COUNT]);
			outb_p (padapter->ide.ide.ide[3], pports[PORT_LBA_0]);
			outb_p (padapter->ide.ide.ide[4], pports[PORT_LBA_8]);
			outb_p (padapter->ide.ide.ide[5], pports[PORT_LBA_16]);
			padapter->expectingIRQ = 1;
			outb_p (padapter->ide.ide.ide[7], pports[PORT_STAT_CMD]);

			if ( padapter->ide.ide.ides.cmd == IDE_CMD_WRITE_MULTIPLE )
				return (WriteData (padapter));
			return 0;
			}
		}	while ( timer > jiffies );									// test for timeout

	padapter->ide.ide.ides.cmd = 0;									// null out the command byte
	return status;
	}
/****************************************************************
 *	Name:	SetupTransfer	:LOCAL
 *
 *	Description:	Setup a data transfer command.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					drive	 - Drive/head register upper nibble only.
 *
 *	Returns:		TRUE if no data to transfer.
 *
 ****************************************************************/
static int SetupTransfer (PADAPTER2220I padapter, UCHAR drive)
	{
	if ( padapter->sectorCount )
		{
		*(ULONG *)padapter->ide.ide.ides.lba = padapter->startSector;
		padapter->ide.ide.ide[6] |= drive;
//		padapter->ide.ide.ides.sectors = ( padapter->sectorCount > SECTORSXFER ) ? SECTORSXFER : padapter->sectorCount;
		padapter->ide.ide.ides.sectors = ( padapter->sectorCount > MAX_BUS_MASTER_BLOCKS ) ? MAX_BUS_MASTER_BLOCKS : padapter->sectorCount;
		padapter->sectorCount -= padapter->ide.ide.ides.sectors;	// bump the start and count for next xfer
		padapter->startSector += padapter->ide.ide.ides.sectors;
		return 0;
		}
	else
		{
		padapter->ide.ide.ides.cmd = 0;								// null out the command byte
		padapter->SCpnt = NULL;
		return 1;
		}
	}
/****************************************************************
 *	Name:	DecodeError	:LOCAL
 *
 *	Description:	Decode and process device errors.
 *
 *	Parameters:		pshost - Pointer to host data block.
 *					status - Status register code.
 *
 *	Returns:		The driver status code.
 *
 ****************************************************************/
static ULONG DecodeError (struct Scsi_Host *pshost, UCHAR status)
	{
	PADAPTER2220I	padapter = HOSTDATA(pshost);
	UCHAR			error;

	padapter->expectingIRQ = 0;
	padapter->SCpnt = NULL;
	if ( status & IDE_STATUS_WRITE_FAULT )
		{
		return DID_PARITY << 16;
		}
	if ( status & IDE_STATUS_BUSY )
		return DID_BUS_BUSY << 16;

	error = inb_p (padapter->ports[PORT_ERROR]);
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
	USHORT		 	   *pports;			// I/O port array
	Scsi_Cmnd		   *SCpnt;
	UCHAR				status;
	int					z;

//	DEB(printk ("\npci2220i recieved interrupt\n"));

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
		return;
		}

	padapter = HOSTDATA(shost);
	pports = padapter->ports;
	SCpnt = padapter->SCpnt;

	if ( !padapter->expectingIRQ )
		{
		DEB(printk ("\npci2220i Unsolicited interrupt\n"));
		return;
		}
	padapter->expectingIRQ = 0;

	status = inb_p (padapter->ports[PORT_STAT_CMD]);					// read the device status
	if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
		goto irqerror;

	switch ( padapter->ide.ide.ides.cmd )								// decide how to handle the interrupt
		{
		case IDE_CMD_READ_MULTIPLE:
			if ( padapter->readPhase == 1 )								// is this a bus master channel complete?
				{
				DEB(printk ("\npci2220i processing read interrupt cleanup"));
				outb_p (0x08, padapter->regDmaCmdStat);					// cancel interrupt from DMA engine
				padapter->buffer += padapter->ide.ide.ides.sectors * 512;
				if ( SetupTransfer (padapter, padapter->ide.ide.ide[6] & 0xF0) )
					{
					SCpnt->result = DID_OK << 16;
					padapter->SCpnt = NULL;
					SCpnt->scsi_done (SCpnt);
					return;
					}
				padapter->readPhase = 0;
				if ( !(status = IdeCmd (padapter)) )
					{
					DEB (printk ("\npci2220i interrupt complete, waiting for another"));
					return;
					}
				}
			if ( status & IDE_STATUS_DRQ )
				{
				DEB(printk ("\npci2220i processing read interrupt start bus master cycle"));
				outb_p (8, padapter->regDmaDesc); 				   		// read operation
				padapter->readPhase = 1;
				padapter->expectingIRQ = 1;
				outl   (padapter->timingAddress, padapter->regDmaAddrLoc);
				outl   (virt_to_bus (padapter->buffer), padapter->regDmaAddrPci);
				outl   ((ULONG)padapter->ide.ide.ides.sectors * (ULONG)512, padapter->regDmaCount);
				outb_p (5, padapter->regDmaMode);				   		// interrupt enable/disable
				outb_p (0x03, padapter->regDmaCmdStat);			   		// kick the DMA engine in gear
				return;
				}
			break;

		case IDE_CMD_WRITE_MULTIPLE:
			DEB(printk ("\npci2220i processing write interrupt cleanup"));
			padapter->buffer += padapter->ide.ide.ides.sectors * 512;
			if ( SetupTransfer (padapter, padapter->ide.ide.ide[6] & 0xF0) )
				{
				SCpnt->result = DID_OK << 16;
				padapter->SCpnt = NULL;
				SCpnt->scsi_done (SCpnt);
				return;
				}
			if ( !(status = IdeCmd (padapter)) )
				{
				DEB (printk ("\npci2220i interrupt complete, waiting for another"));
				return;
				}
			break;

		case IDE_COMMAND_IDENTIFY:
			{
			PINQUIRYDATA	pinquiryData  = SCpnt->request_buffer;

			DEB(printk ("\npci2220i processing verify interrupt cleanup"));
			if ( status & IDE_STATUS_DRQ )
				{
				insw (pports[PORT_DATA], &identifyData, sizeof (identifyData) >> 1);

				memset (pinquiryData, 0, SCpnt->request_bufflen);		// Zero INQUIRY data structure.
				pinquiryData->DeviceType = 0;
				pinquiryData->Versions = 2;
				pinquiryData->AdditionalLength = 35 - 4;

				// Fill in vendor identification fields.
				for ( z = 0;  z < 20;  z += 2 )
					{
					pinquiryData->VendorId[z]	  = ((UCHAR *)identifyData.ModelNumber)[z + 1];
					pinquiryData->VendorId[z + 1] = ((UCHAR *)identifyData.ModelNumber)[z];
					}

				// Initialize unused portion of product id.
				for ( z = 0;  z < 4;  z++ )
					pinquiryData->ProductId[12 + z] = ' ';

				// Move firmware revision from IDENTIFY data to
				// product revision in INQUIRY data.
				for ( z = 0;  z < 4;  z += 2 )
					{
					pinquiryData->ProductRevisionLevel[z]	 = ((UCHAR *)identifyData.FirmwareRevision)[z + 1];
					pinquiryData->ProductRevisionLevel[z + 1] = ((UCHAR *)identifyData.FirmwareRevision)[z];
					}

				SCpnt->result = DID_OK << 16;
				padapter->SCpnt = NULL;
				SCpnt->scsi_done (SCpnt);
				return;
				}
			break;
			}

		default:
			DEB(printk ("\npci2220i no real process here!"));
			SCpnt->result = DID_OK << 16;
			padapter->SCpnt = NULL;
			SCpnt->scsi_done (SCpnt);
			return;
		}

irqerror:;
	DEB(printk ("\npci2220i error  Device Status: %X\n", status));
	SCpnt->result = DecodeError (shost, status);
	SCpnt->scsi_done (SCpnt);
	}
static void do_Irq_Handler (int irq, void *dev_id, struct pt_regs *regs)
	{
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock, flags);
	Irq_Handler(irq, dev_id, regs);
	spin_unlock_irqrestore(&io_request_lock, flags);
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

	SCpnt->scsi_done = done;
	padapter->ide.ide.ides.spigot = pdev->spigot;
	padapter->buffer = SCpnt->request_buffer;
	if (done)
		{
		if ( !pdev->device )
			{
			SCpnt->result = DID_BAD_TARGET << 16;
			done (SCpnt);
			return 0;
			}
		}
	else
		{
		printk("pci2220i_queuecommand: %02X: done can't be NULL\n", *cdb);
		return 0;
		}

	DEB (if(*cdb) printk ("\nCDB: %X-  %X %X %X %X %X %X %X %X %X %X ", SCpnt->cmd_len, cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9]));
	switch ( *cdb )
		{
		case SCSIOP_INQUIRY:   					// inquiry CDB
			{
			padapter->ide.ide.ide[6] = pdev->byte6;
			padapter->ide.ide.ides.cmd = IDE_COMMAND_IDENTIFY;
			break;
			}

		case SCSIOP_TEST_UNIT_READY:			// test unit ready CDB
			SCpnt->result = DID_OK << 16;
			done (SCpnt);
			return 0;

		case SCSIOP_READ_CAPACITY:			  	// read capctiy CDB
			{
			PREAD_CAPACITY_DATA	pdata = (PREAD_CAPACITY_DATA)SCpnt->request_buffer;

			pdata->blksiz = 0x20000;
			XANY2SCSI ((UCHAR *)&pdata->blks, pdev->blocks);
			SCpnt->result = DID_OK << 16;
			done (SCpnt);
			return 0;
			}

		case SCSIOP_VERIFY:						// verify CDB
			*(ULONG *)padapter->ide.ide.ides.lba = XSCSI2LONG (&cdb[2]);
			padapter->ide.ide.ide[6] |= pdev->byte6;
			padapter->ide.ide.ide[2] = (UCHAR)((USHORT)cdb[8] | ((USHORT)cdb[7] << 8));
			padapter->ide.ide.ides.cmd = IDE_COMMAND_VERIFY;
			break;

		case SCSIOP_READ:						// read10 CDB
			padapter->startSector = XSCSI2LONG (&cdb[2]);
			padapter->sectorCount = (USHORT)cdb[8] | ((USHORT)cdb[7] << 8);
			SetupTransfer (padapter, pdev->byte6);
			padapter->ide.ide.ides.cmd = IDE_CMD_READ_MULTIPLE;
			padapter->readPhase = 0;
			break;

		case SCSIOP_READ6:						// read6  CDB
			padapter->startSector = SCSI2LONG (&cdb[1]);
			padapter->sectorCount = cdb[4];
			SetupTransfer (padapter, pdev->byte6);
			padapter->ide.ide.ides.cmd = IDE_CMD_READ_MULTIPLE;
			padapter->readPhase = 0;
			break;

		case SCSIOP_WRITE:						// write10 CDB
			padapter->startSector = XSCSI2LONG (&cdb[2]);
			padapter->sectorCount = (USHORT)cdb[8] | ((USHORT)cdb[7] << 8);
			SetupTransfer (padapter, pdev->byte6);
			padapter->ide.ide.ides.cmd = IDE_CMD_WRITE_MULTIPLE;
			break;
		case SCSIOP_WRITE6:						// write6  CDB
			padapter->startSector = SCSI2LONG (&cdb[1]);
			padapter->sectorCount = cdb[4];
			SetupTransfer (padapter, pdev->byte6);
			padapter->ide.ide.ides.cmd = IDE_CMD_WRITE_MULTIPLE;
			break;

		default:
			DEB (printk ("pci2220i_queuecommand: Unsupported command %02X\n", *cdb));
			SCpnt->result = DID_ERROR << 16;
			done (SCpnt);
			return 0;
		}

	padapter->SCpnt = SCpnt;  									// Save this command data

	rc = IdeCmd (padapter);
	if ( rc )
		{
		padapter->expectingIRQ = 0;
		DEB (printk ("pci2220i_queuecommand: %02X, %02X: Device failed to respond for command\n", *cdb, padapter->ide.ide.ides.cmd));
		SCpnt->result = DID_ERROR << 16;
		done (SCpnt);
		return 0;
		}
	if ( padapter->ide.ide.ides.cmd == IDE_CMD_WRITE_MULTIPLE )
		{
		if ( WriteData (padapter) )
			{
			padapter->expectingIRQ = 0;
			DEB (printk ("pci2220i_queuecommand: %02X, %02X: Device failed to accept data\n", *cdb, padapter->ide.ide.ides.cmd));
			SCpnt->result = DID_ERROR << 16;
			done (SCpnt);
			return 0;
			}
		}
	DEB (printk("  now waiting for initial interrupt "));
	return 0;
	}

static void internal_done(Scsi_Cmnd * SCpnt)
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
	DEB(printk("pci2220i_command: ..calling pci2220i_queuecommand\n"));

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
 *	Parameters:		hostdata - Pointer to host interface data structure.
 *					pdata	 - Pointer to data structures.
 *					base	 - base address in Flash.
 *					length	 - lenght of data space in bytes.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
VOID ReadFlash (PADAPTER2220I hostdata, VOID *pdata, ULONG base, ULONG length)
	{
	ULONG	 oldremap;
	UCHAR	 olddesc;
	ULONG	 z;
	UCHAR	*pd = (UCHAR *)pdata;

	oldremap = inl (hostdata->regRemap);									// save values to restore later
	olddesc  = inb_p (hostdata->regDesc);

	outl (base | 1, hostdata->regRemap);									// remap to Flash space as specified
	outb_p (0x40, hostdata->regDesc);										// describe remap region as 8 bit
	for ( z = 0;  z < length;  z++)											// get "length" data count
		*pd++ = inb_p (hostdata->regBase + z);								// read in the data

	outl (oldremap, hostdata->regRemap);									// restore remap register values
	outb_p (olddesc, hostdata->regDesc);
	}

/****************************************************************
 *	Name:	Pci2220i_Detect
 *
 *	Description:	Detect and initialize our boards.
 *
 *	Parameters:		tpnt - Pointer to SCSI host template structure.
 *
 *	Returns:		Number of adapters found.
 *
 ****************************************************************/
int Pci2220i_Detect (Scsi_Host_Template *tpnt)
	{
	struct pci_dev	   *pdev = NULL;
	struct Scsi_Host   *pshost;
	PADAPTER2220I	    hostdata;
	ULONG				modearray[] = {DALE_DATA_MODE2, DALE_DATA_MODE3, DALE_DATA_MODE4, DALE_DATA_MODE4P};
	int					unit;
	int					z;
	int					setirq;

	if ( pci_present () )
		while ((pdev = pci_find_device(VENDOR_PSI, DEVICE_DALE_1, pdev)))
			{
			pshost = scsi_register (tpnt, sizeof(ADAPTER2220I));
			hostdata = HOSTDATA(pshost);

			hostdata->basePort = pdev->base_address[1] & PCI_BASE_ADDRESS_IO_MASK;
			DEB (printk ("\nBase Regs = %#04X", hostdata->basePort));
			hostdata->regRemap		= hostdata->basePort + RTR_LOCAL_REMAP;				// 32 bit local space remap
			DEB (printk (" %#04X", hostdata->regRemap));
			hostdata->regDesc		= hostdata->basePort + RTR_REGIONS;	  				// 32 bit local region descriptor
			DEB (printk (" %#04X", hostdata->regDesc));
			hostdata->regRange		= hostdata->basePort + RTR_LOCAL_RANGE;				// 32 bit local range
			DEB (printk (" %#04X", hostdata->regRange));
			hostdata->regIrqControl	= hostdata->basePort + RTR_INT_CONTROL_STATUS;		// 16 bit interupt control and status
			DEB (printk (" %#04X", hostdata->regIrqControl));
			hostdata->regScratchPad	= hostdata->basePort + RTR_MAILBOX;	  				// 16 byte scratchpad I/O base address
			DEB (printk (" %#04X", hostdata->regScratchPad));

			hostdata->regBase = pdev->base_address[2] & PCI_BASE_ADDRESS_IO_MASK;
			for ( z = 0;  z < 9;  z++ )													// build regester address array
				hostdata->ports[z] = hostdata->regBase + 0x80 + (z * 4);
			hostdata->ports[PORT_FAIL] = hostdata->regBase + REG_FAIL;
			hostdata->ports[PORT_ALT_STAT] = hostdata->regBase + REG_ALT_STAT;
			DEB (printk ("\nPorts ="));
			DEB (for (z=0;z<11;z++) printk(" %#04X", hostdata->ports[z]););

			hostdata->regDmaDesc	= hostdata->regBase + RTL_DMA1_DESC_PTR;			// address of the DMA discriptor register for direction of transfer
			DEB (printk ("\nDMA Regs = %#04X", hostdata->regDmaDesc));
			hostdata->regDmaCmdStat	= hostdata->regBase + RTL_DMA_COMMAND_STATUS + 1;	// Byte #1 of DMA command status register
			DEB (printk (" %#04X", hostdata->regDmaCmdStat));
			hostdata->regDmaAddrPci	= hostdata->regBase + RTL_DMA1_PCI_ADDR;			// 32 bit register for PCI address of DMA
			DEB (printk (" %#04X", hostdata->regDmaAddrPci));
			hostdata->regDmaAddrLoc	= hostdata->regBase + RTL_DMA1_LOCAL_ADDR;			// 32 bit register for local bus address of DMA
			DEB (printk (" %#04X", hostdata->regDmaAddrLoc));
			hostdata->regDmaCount	= hostdata->regBase + RTL_DMA1_COUNT;				// 32 bit register for DMA transfer count
			DEB (printk (" %#04X", hostdata->regDmaCount));
			hostdata->regDmaMode	= hostdata->regBase + RTL_DMA1_MODE + 1;			// 32 bit register for DMA mode control
			DEB (printk (" %#04X", hostdata->regDmaMode));

			if ( !inb_p (hostdata->regScratchPad + DALE_NUM_DRIVES) )					// if no devices on this board
				goto unregister;

			pshost->irq = pdev->irq;
			setirq = 1;
			for ( z = 0;  z < NumAdapters;  z++ )										// scan for shared interrupts
				{
				if ( PsiHost[z]->irq == pshost->irq )									// if shared then, don't posses
					setirq = 0;
				}
			if ( setirq )																// if not shared, posses
				{
				if ( request_irq (pshost->irq, do_Irq_Handler, 0, "pci2220i", NULL) )
					{
					printk ("Unable to allocate IRQ for PSI-2220I controller.\n");
					goto unregister;
					}
				}
			PsiHost[NumAdapters]	= pshost;											// save SCSI_HOST pointer

			pshost->unique_id	= hostdata->regBase;
			pshost->max_id		= 4;

			outb_p (0x01, hostdata->regRange);											// fix our range register because other drivers want to tromp on it

			hostdata->timingMode	= inb_p (hostdata->regScratchPad + DALE_TIMING_MODE);
			hostdata->timingAddress	= modearray[hostdata->timingMode - 2];
			ReadFlash (hostdata, &DaleSetup, DALE_FLASH_SETUP, sizeof (SETUP));

			for ( z = 0;  z < inb_p (hostdata->regScratchPad + DALE_NUM_DRIVES);  ++z )
				{
				unit = inb_p (hostdata->regScratchPad + DALE_CHANNEL_DEVICE_0 + z) & 0x0F;
				hostdata->device[unit].device	 = inb_p (hostdata->regScratchPad + DALE_SCRATH_DEVICE_0 + unit);
				hostdata->device[unit].byte6	 = (UCHAR)(((unit & 1) << 4) | 0xE0);
				hostdata->device[unit].spigot	 = (UCHAR)(1 << (unit >> 1));
				hostdata->device[unit].sectors	 = DaleSetup.setupDevice[unit].sectors;
				hostdata->device[unit].heads	 = DaleSetup.setupDevice[unit].heads;
				hostdata->device[unit].cylinders = DaleSetup.setupDevice[unit].cylinders;
				hostdata->device[unit].blocks	 = DaleSetup.setupDevice[unit].blocks;
				DEB (printk ("\nHOSTDATA->device    = %X", hostdata->device[unit].device));
				DEB (printk ("\n          byte6     = %X", hostdata->device[unit].byte6));
				DEB (printk ("\n          spigot    = %X", hostdata->device[unit].spigot));
				DEB (printk ("\n          sectors   = %X", hostdata->device[unit].sectors));
				DEB (printk ("\n          heads     = %X", hostdata->device[unit].heads));
				DEB (printk ("\n          cylinders = %X", hostdata->device[unit].cylinders));
				DEB (printk ("\n          blocks    = %lX", hostdata->device[unit].blocks));
				}

			printk("\nPSI-2220I EIDE CONTROLLER: at I/O = %X/%X  IRQ = %d\n", hostdata->basePort, hostdata->regBase, pshost->irq);
			printk("(C) 1997 Perceptive Solutions, Inc. All rights reserved\n\n");
			continue;
unregister:
			scsi_unregister (pshost);
			NumAdapters++;
			}
	return NumAdapters;
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
	DEB (printk ("pci2220i_abort\n"));
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

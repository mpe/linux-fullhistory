/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2000 device driver for Linux.
 *
 * pci2000.c - Linux Host Driver for PCI-2000 IntelliCache SCSI Adapters
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
 *	Revisions	1.10	Jan-21-1999
 *		- Fixed sign on message to reflect proper controller name.
 *		- Added support for RAID status monitoring and control.
 *
 *  Revisions	1.11	Mar-22-1999
 *		- Fixed control timeout to not lock up the entire system if
 *		  controller goes offline completely.
 *
 *	Revisions 1.12		Mar-26-1999
 *		- Fixed spinlock and PCI configuration.
 *
 ****************************************************************************/
#define PCI2000_VERSION		"1.12"

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
#include <asm/io.h>
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <linux/stat.h>

#include "pci2000.h"
#include "psi_roy.h"

#if LINUX_VERSION_CODE >= LINUXVERSION(2,1,95)
#include <asm/spinlock.h>
#endif
#if LINUX_VERSION_CODE < LINUXVERSION(2,1,93)
#include <linux/bios32.h>
#endif

//#define DEBUG 1

#ifdef DEBUG
#define DEB(x) x
#define STOP_HERE	{int st;for(st=0;st<100;st++){st=1;}}
#else
#define DEB(x)
#define STOP_HERE
#endif

typedef struct
	{
	ULONG		address;
	ULONG		length;
	}	SCATGATH, *PSCATGATH;

typedef struct
	{
	Scsi_Cmnd	*SCpnt;
	SCATGATH	 scatGath[16];
	UCHAR		 tag;
	}	DEV2000, *PDEV2000;

typedef struct
	{
	USHORT		 basePort;
	USHORT		 mb0;
	USHORT		 mb1;
	USHORT		 mb2;
	USHORT		 mb3;
	USHORT		 mb4;
	USHORT		 cmd;
	USHORT		 tag;
	ULONG		 irqOwned;
	DEV2000	 	 dev[MAX_BUS][MAX_UNITS];
	}	ADAPTER2000, *PADAPTER2000;

#define HOSTDATA(host) ((PADAPTER2000)&host->hostdata)


static struct	Scsi_Host 	   *PsiHost[MAXADAPTER] = {NULL,};  // One for each adapter
static			int				NumAdapters = 0;
/****************************************************************
 *	Name:			WaitReady	:LOCAL
 *
 *	Description:	Wait for controller ready.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE on not ready.
 *
 ****************************************************************/
static int WaitReady (PADAPTER2000 padapter)
	{
	ULONG	z;

	for ( z = 0;  z < (TIMEOUT_COMMAND * 4);  z++ )
		{
		if ( !inb_p (padapter->cmd) )
			return FALSE;
		udelay (250);
		};								
	return TRUE;
	}
/****************************************************************
 *	Name:			WaitReadyLong	:LOCAL
 *
 *	Description:	Wait for controller ready.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE on not ready.
 *
 ****************************************************************/
static int WaitReadyLong (PADAPTER2000 padapter)
	{
	ULONG	z;

	for ( z = 0;  z < (5000 * 4);  z++ )
		{
		if ( !inb_p (padapter->cmd) )
			return FALSE;
		udelay (250);
		};								
	return TRUE;
	}
/****************************************************************
 *	Name:	OpDone	:LOCAL
 *
 *	Description:	Clean up operation and issue done to caller.
 *
 *	Parameters:		SCpnt	- Pointer to SCSI command structure.
 *					status	- Caller status.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void OpDone (Scsi_Cmnd *SCpnt, ULONG status)
	{
	SCpnt->result = status;
	SCpnt->scsi_done (SCpnt);
	}
/****************************************************************
 *	Name:	Command		:LOCAL
 *
 *	Description:	Issue queued command to the PCI-2000.
 *
 *	Parameters:		padapter - Pointer to adapter information structure.
 *					cmd		 - PCI-2000 command byte.
 *
 *	Returns:		Non-zero command tag if operation is accepted.
 *
 ****************************************************************/
static UCHAR Command (PADAPTER2000 padapter, UCHAR cmd)
	{
	outb_p (cmd, padapter->cmd);
	if ( WaitReady (padapter) )
		return 0;

	if ( inw_p (padapter->mb0) )
		return 0;

	return inb_p (padapter->mb1);
	}
/****************************************************************
 *	Name:	BuildSgList		:LOCAL
 *
 *	Description:	Build the scatter gather list for controller.
 *
 *	Parameters:		SCpnt	 - Pointer to SCSI command structure.
 *					padapter - Pointer to adapter information structure.
 *					pdev	 - Pointer to adapter device structure.
 *
 *	Returns:		Non-zero in not scatter gather.
 *
 ****************************************************************/
static int BuildSgList (Scsi_Cmnd *SCpnt, PADAPTER2000 padapter, PDEV2000 pdev)
	{
	int	z;

	if ( SCpnt->use_sg )
		{
		for ( z = 0;  z < SCpnt->use_sg;  z++ )
			{
			pdev->scatGath[z].address = virt_to_bus (((struct scatterlist *)SCpnt->request_buffer)[z].address);
			pdev->scatGath[z].length = ((struct scatterlist *)SCpnt->request_buffer)[z].length;
			}
		outl (virt_to_bus (pdev->scatGath), padapter->mb2);
		outl ((SCpnt->use_sg << 24) | SCpnt->request_bufflen, padapter->mb3);
		return FALSE;
		}
	outl (virt_to_bus (SCpnt->request_buffer), padapter->mb2);
	outl (SCpnt->request_bufflen, padapter->mb3);
	return TRUE;
	}
/*********************************************************************
 *	Name:	PsiRaidCmd
 *
 *	Description:	Execute a simple command.
 *
 *	Parameters:		padapter - Pointer to adapter control structure.
 *					cmd		 - Roy command byte.
 *
 *	Returns:		Return error status.
 *
 ********************************************************************/
static int PsiRaidCmd (PADAPTER2000 padapter, char cmd)
	{
	if ( WaitReady (padapter) )						// test for command register ready
		return DID_TIME_OUT;
	outb_p (cmd, padapter->cmd);					// issue command
	if ( WaitReadyLong (padapter) )					// wait for adapter ready
		return DID_TIME_OUT;
	return DID_OK;
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
	PADAPTER2000		padapter;		// Pointer to adapter control structure
	PDEV2000			pdev;
	Scsi_Cmnd		   *SCpnt;
	UCHAR				tag = 0;
	UCHAR				tag0;
	ULONG				error;
	int					pun;
	int					bus;
	int					z;
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

	DEB(printk ("\npci2000 recieved interrupt "));
	for ( z = 0; z < NumAdapters;  z++ )										// scan for interrupt to process
		{
		if ( PsiHost[z]->irq == (UCHAR)(irq & 0xFF) )
			{
			tag = inb_p (HOSTDATA(PsiHost[z])->tag);
			if (  tag )
				{
				shost = PsiHost[z];
				break;
				}
			}
		}

	if ( !shost )
		{
		DEB (printk ("\npci2000: not my interrupt"));
		goto irq_return;
		}

	padapter = HOSTDATA(shost);

	tag0 = tag & 0x7F;															// mask off the error bit
	for ( bus = 0;  bus < MAX_BUS;  bus++ )										// scan the busses
    	{
		for ( pun = 0;  pun < MAX_UNITS;  pun++ )								// scan the targets
    		{
			pdev = &padapter->dev[bus][pun];
			if ( !pdev->tag )
    			continue;
			if ( pdev->tag == tag0 )											// is this it?
				{
				pdev->tag = 0;
				SCpnt = pdev->SCpnt;
				goto irqProceed;
    			}
			}
    	}

	outb_p (0xFF, padapter->tag);												// clear the op interrupt
	outb_p (CMD_DONE, padapter->cmd);											// complete the op
	goto irq_return;;																		// done, but, with what?

irqProceed:;
	if ( tag & ERR08_TAGGED )												// is there an error here?
		{
		if ( WaitReady (padapter) )
			{
			OpDone (SCpnt, DID_TIME_OUT << 16);
			goto irq_return;;
			}

		outb_p (tag0, padapter->mb0);										// get real error code
		outb_p (CMD_ERROR, padapter->cmd);
		if ( WaitReady (padapter) )											// wait for controller to suck up the op
			{
			OpDone (SCpnt, DID_TIME_OUT << 16);
			goto irq_return;;
			}

		error = inl (padapter->mb0);										// get error data
		outb_p (0xFF, padapter->tag);										// clear the op interrupt
		outb_p (CMD_DONE, padapter->cmd);									// complete the op

		DEB (printk ("status: %lX ", error));
		if ( error == 0x00020002 )											// is this error a check condition?
			{
			if ( bus )														// are we doint SCSI commands?
				{
				OpDone (SCpnt, (DID_OK << 16) | 2);
				goto irq_return;;
				}
			if ( *SCpnt->cmnd == SCSIOP_TEST_UNIT_READY )
				OpDone (SCpnt, (DRIVER_SENSE << 24) | (DID_OK << 16) | 2);	// test caller we have sense data too
			else
				OpDone (SCpnt, DID_ERROR << 16);
			goto irq_return;;
			}
		OpDone (SCpnt, DID_ERROR << 16);
		goto irq_return;;
		}

	outb_p (0xFF, padapter->tag);											// clear the op interrupt
	outb_p (CMD_DONE, padapter->cmd);										// complete the op
	OpDone (SCpnt, DID_OK << 16);

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
 *	Name:	Pci2000_QueueCommand
 *
 *	Description:	Process a queued command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *					done  - Pointer to done function to call.
 *
 *	Returns:		Status code.
 *
 ****************************************************************/
int Pci2000_QueueCommand (Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
	{
	UCHAR		   *cdb = (UCHAR *)SCpnt->cmnd;					// Pointer to SCSI CDB
	PADAPTER2000	padapter = HOSTDATA(SCpnt->host);			// Pointer to adapter control structure
	int				rc		 = -1;								// command return code
	UCHAR			bus		 = SCpnt->channel;
	UCHAR			pun		 = SCpnt->target;
	UCHAR			lun		 = SCpnt->lun;
	UCHAR			cmd;
	PDEV2000		pdev	 = &padapter->dev[bus][pun];

	if ( !done )
		{
		printk("pci2000_queuecommand: %02X: done can't be NULL\n", *cdb);
		return 0;
		}

	SCpnt->scsi_done = done;
	pdev->SCpnt = SCpnt;  									// Save this command data

	if ( WaitReady (padapter) )
		{
		rc = DID_ERROR;
		goto finished;
		}

	outw_p (pun | (lun << 8), padapter->mb0);

	if ( bus )
		{
		DEB (if(*cdb) printk ("\nCDB: %X-  %X %X %X %X %X %X %X %X %X %X ", SCpnt->cmd_len, cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9]));
		DEB (if(*cdb) printk ("\ntimeout_per_command: %d, timeout_total: %d, timeout: %d, internal_timout: %d", SCpnt->timeout_per_command,
							  SCpnt->timeout_total, SCpnt->timeout, SCpnt->internal_timeout));
		outl (SCpnt->timeout_per_command, padapter->mb1);
		outb_p (CMD_SCSI_TIMEOUT, padapter->cmd);
		if ( WaitReady (padapter) )
			{
			rc = DID_ERROR;
			goto finished;
			}

		outw_p (pun | (lun << 8), padapter->mb0);
		outw_p (SCpnt->cmd_len << 8, padapter->mb0 + 2);
		outl (virt_to_bus (cdb), padapter->mb1);
		if ( BuildSgList (SCpnt, padapter, pdev) )
			cmd = CMD_SCSI_THRU;
		else
			cmd = CMD_SCSI_THRU_SG;
		if ( (pdev->tag = Command (padapter, cmd)) == 0 )
			rc = DID_TIME_OUT;
		goto finished;
		}
	else
		{
		if ( lun )
			{
			rc = DID_BAD_TARGET;
			goto finished;
			}
		}

	switch ( *cdb )
		{
		case SCSIOP_INQUIRY:   					// inquiry CDB
			if ( cdb[2] == SC_MY_RAID )
				{
				switch ( cdb[3] ) 
					{
					case MY_SCSI_REBUILD:
						OpDone (SCpnt, PsiRaidCmd (padapter, CMD_RAID_REBUILD) << 16);
						return 0;
					case MY_SCSI_ALARMMUTE:
						OpDone (SCpnt, PsiRaidCmd (padapter, CMD_RAID_MUTE) << 16);
						return 0;
					case MY_SCSI_DEMOFAIL:
						OpDone (SCpnt, PsiRaidCmd (padapter, CMD_RAID_FAIL) << 16);
						return 0;
					default:
						if ( SCpnt->use_sg )
							{
							rc = DID_ERROR;
							goto finished;
							}
						else
							outl (virt_to_bus (SCpnt->request_buffer), padapter->mb2);
						outl (cdb[5], padapter->mb0);
						outl (cdb[3], padapter->mb3);
						cmd = CMD_DASD_RAID_RQ;
						break;
					}
				break;
				}
			
			if ( SCpnt->use_sg )
				{
				outl (virt_to_bus (((struct scatterlist *)(SCpnt->request_buffer))->address), padapter->mb2);
				}
			else
				{
				outl (virt_to_bus (SCpnt->request_buffer), padapter->mb2);
				}
			outl (SCpnt->request_bufflen, padapter->mb3);
			cmd = CMD_DASD_SCSI_INQ;
			break;

		case SCSIOP_TEST_UNIT_READY:			// test unit ready CDB
			outl (virt_to_bus (SCpnt->sense_buffer), padapter->mb2);
			outl (sizeof (SCpnt->sense_buffer), padapter->mb3);
			cmd = CMD_TEST_READY;
			break;

		case SCSIOP_READ_CAPACITY:			  	// read capctiy CDB
			if ( SCpnt->use_sg )
				{
				outl (virt_to_bus (((struct scatterlist *)(SCpnt->request_buffer))->address), padapter->mb2);
				}
			else
				{
				outl (virt_to_bus (SCpnt->request_buffer), padapter->mb2);
				}
			outl (8, padapter->mb3);
			cmd = CMD_DASD_CAP;
			break;
		case SCSIOP_VERIFY:						// verify CDB
			outw_p ((USHORT)cdb[8] | ((USHORT)cdb[7] << 8), padapter->mb0 + 2);
			outl (XSCSI2LONG (&cdb[2]), padapter->mb1);
			cmd = CMD_READ_SG;
			break;
		case SCSIOP_READ:						// read10 CDB
			outw_p ((USHORT)cdb[8] | ((USHORT)cdb[7] << 8), padapter->mb0 + 2);
			outl (XSCSI2LONG (&cdb[2]), padapter->mb1);
			if ( BuildSgList (SCpnt, padapter, pdev) )
				cmd = CMD_READ;
			else
				cmd = CMD_READ_SG;
			break;
		case SCSIOP_READ6:						// read6  CDB
			outw_p (cdb[4], padapter->mb0 + 2);
			outl ((SCSI2LONG (&cdb[1])) & 0x001FFFFF, padapter->mb1);
			if ( BuildSgList (SCpnt, padapter, pdev) )
				cmd = CMD_READ;
			else
				cmd = CMD_READ_SG;
			break;
		case SCSIOP_WRITE:						// write10 CDB
			outw_p ((USHORT)cdb[8] | ((USHORT)cdb[7] << 8), padapter->mb0 + 2);
			outl (XSCSI2LONG (&cdb[2]), padapter->mb1);
			if ( BuildSgList (SCpnt, padapter, pdev) )
				cmd = CMD_WRITE;
			else
				cmd = CMD_WRITE_SG;
			break;
		case SCSIOP_WRITE6:						// write6  CDB
			outw_p (cdb[4], padapter->mb0 + 2);
			outl ((SCSI2LONG (&cdb[1])) & 0x001FFFFF, padapter->mb1);
			if ( BuildSgList (SCpnt, padapter, pdev) )
				cmd = CMD_WRITE;
			else
				cmd = CMD_WRITE_SG;
			break;
		case SCSIOP_START_STOP_UNIT:
			cmd = CMD_EJECT_MEDIA;
			break;
		case SCSIOP_MEDIUM_REMOVAL:
			switch ( cdb[4] )
				{
				case 0:
					cmd = CMD_UNLOCK_DOOR;
					break;
				case 1:
					cmd = CMD_LOCK_DOOR;
					break;
				default:
					cmd = 0;
					break;
				}
			if ( cmd )
				break;
		default:
			DEB (printk ("pci2000_queuecommand: Unsupported command %02X\n", *cdb));
			OpDone (SCpnt, DID_ERROR << 16);
			return 0;
		}

	if ( (pdev->tag = Command (padapter, cmd)) == 0 )
		rc = DID_TIME_OUT;
finished:;
	if ( rc != -1 )
		OpDone (SCpnt, rc << 16);
	return 0;
	}
/****************************************************************
 *	Name:	internal_done :LOCAL
 *
 *	Description:	Done handler for non-queued commands
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void internal_done (Scsi_Cmnd * SCpnt)
	{
	SCpnt->SCp.Status++;
	}
/****************************************************************
 *	Name:	Pci2000_Command
 *
 *	Description:	Process a command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Status code.
 *
 ****************************************************************/
int Pci2000_Command (Scsi_Cmnd *SCpnt)
	{
	DEB(printk("pci2000_command: ..calling pci2000_queuecommand\n"));

	Pci2000_QueueCommand (SCpnt, internal_done);

    SCpnt->SCp.Status = 0;
	while (!SCpnt->SCp.Status)
		barrier ();
	return SCpnt->result;
	}
/****************************************************************
 *	Name:	Pci2000_Detect
 *
 *	Description:	Detect and initialize our boards.
 *
 *	Parameters:		tpnt - Pointer to SCSI host template structure.
 *
 *	Returns:		Number of adapters installed.
 *
 ****************************************************************/
int Pci2000_Detect (Scsi_Host_Template *tpnt)
	{
	int					found = 0;
	int					installed = 0;
	struct Scsi_Host   *pshost;
	PADAPTER2000	    padapter;
	int					z, zz;
	int					setirq;
#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
	struct pci_dev	   *pdev = NULL;
#else
	UCHAR	pci_bus, pci_device_fn;
#endif

#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
	if ( !pci_present () )
#else
	if ( !pcibios_present () )
#endif
		{
		printk ("pci2000: PCI BIOS not present\n");
		return 0;
		}

#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
	while ( (pdev = pci_find_device (VENDOR_PSI, DEVICE_ROY_1, pdev)) != NULL )
#else
	while ( !pcibios_find_device (VENDOR_PSI, DEVICE_ROY_1, found, &pci_bus, &pci_device_fn) )
#endif
		{
		pshost = scsi_register (tpnt, sizeof(ADAPTER2000));
		padapter = HOSTDATA(pshost);

#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
		padapter->basePort = pci_resource_start (pdev, 1);
#else
		pcibios_read_config_word (pci_bus, pci_device_fn, PCI_BASE_ADDRESS_1, &padapter->basePort);
		padapter->basePort &= 0xFFFE;
#endif
		DEB (printk ("\nBase Regs = %#04X", padapter->basePort));			// get the base I/O port address
		padapter->mb0	= padapter->basePort + RTR_MAILBOX;		   			// get the 32 bit mail boxes
		padapter->mb1	= padapter->basePort + RTR_MAILBOX + 4;
		padapter->mb2	= padapter->basePort + RTR_MAILBOX + 8;
		padapter->mb3	= padapter->basePort + RTR_MAILBOX + 12;
		padapter->mb4	= padapter->basePort + RTR_MAILBOX + 16;
		padapter->cmd	= padapter->basePort + RTR_LOCAL_DOORBELL;			// command register
		padapter->tag	= padapter->basePort + RTR_PCI_DOORBELL;			// tag/response register

		if ( WaitReady (padapter) )
			goto unregister;
		outb_p (0x84, padapter->mb0);
		outb_p (CMD_SPECIFY, padapter->cmd);
		if ( WaitReady (padapter) )
			goto unregister;

#if LINUX_VERSION_CODE > LINUXVERSION(2,1,92)
		pshost->irq = pdev->irq;
#else
		pcibios_read_config_byte (pci_bus, pci_device_fn, PCI_INTERRUPT_LINE, &pshost->irq);
#endif
		setirq = 1;
		padapter->irqOwned = 0;
		for ( z = 0;  z < installed;  z++ )									// scan for shared interrupts
			{
			if ( PsiHost[z]->irq == pshost->irq )							// if shared then, don't posses
				setirq = 0;
			}
		if ( setirq )												// if not shared, posses
			{
			if ( request_irq (pshost->irq, Irq_Handler, SA_SHIRQ, "pci2000", padapter) < 0 )
				{
				if ( request_irq (pshost->irq, Irq_Handler, SA_INTERRUPT | SA_SHIRQ, "pci2000", padapter) < 0 )
					{
					printk ("Unable to allocate IRQ for PCI-2000 controller.\n");
					goto unregister;
					}
				}
			padapter->irqOwned = pshost->irq;						// set IRQ as owned
			}
		PsiHost[installed]	= pshost;										// save SCSI_HOST pointer

		pshost->io_port		= padapter->basePort;
		pshost->n_io_port	= 0xFF;
		pshost->unique_id	= padapter->basePort;
		pshost->max_id		= 16;
		pshost->max_channel	= 1;

		for ( zz = 0;  zz < MAX_BUS;  zz++ )
			for ( z = 0; z < MAX_UNITS;  z++ )
				padapter->dev[zz][z].tag = 0;
			
		printk("\nPSI-2000 Intelligent Storage SCSI CONTROLLER: at I/O = %X  IRQ = %d\n", padapter->basePort, pshost->irq);
		printk("Version %s, Compiled %s %s\n\n", PCI2000_VERSION,  __DATE__, __TIME__);
		found++;
		if ( ++installed < MAXADAPTER )
			continue;
		break;
unregister:;
		scsi_unregister (pshost);
		found++;
		}
	NumAdapters = installed;
	return installed;
	}
/****************************************************************
 *	Name:	Pci2000_Abort
 *
 *	Description:	Process the Abort command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Allways snooze.
 *
 ****************************************************************/
int Pci2000_Abort (Scsi_Cmnd *SCpnt)
	{
	DEB (printk ("pci2000_abort\n"));
	return SCSI_ABORT_SNOOZE;
	}
/****************************************************************
 *	Name:	Pci2000_Reset
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
int Pci2000_Reset (Scsi_Cmnd *SCpnt, unsigned int reset_flags)
	{
	return SCSI_RESET_PUNT;
	}
/****************************************************************
 *	Name:	Pci2000_Release
 *
 *	Description:	Release resources allocated for a single each adapter.
 *
 *	Parameters:		pshost - Pointer to SCSI command structure.
 *
 *	Returns:		zero.
 *
 ****************************************************************/
int Pci2000_Release (struct Scsi_Host *pshost)
	{
    PADAPTER2000	padapter = HOSTDATA (pshost);

	if ( padapter->irqOwned )
#if LINUX_VERSION_CODE < LINUXVERSION(1,3,70)
	    free_irq (pshost->irq);
#else /* version >= v1.3.70 */
		free_irq (pshost->irq, padapter);
#endif /* version >= v1.3.70 */
    release_region (pshost->io_port, pshost->n_io_port);
    scsi_unregister(pshost);
    return 0;
	}

#include "sd.h"
/****************************************************************
 *	Name:	Pci2000_BiosParam
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
int Pci2000_BiosParam (Scsi_Disk *disk, kdev_t dev, int geom[])
	{
	PADAPTER2000	    padapter;

	padapter = HOSTDATA(disk->device->host);

	if ( WaitReady (padapter) )
		return 0;
	outb_p (disk->device->id, padapter->mb0);
	outb_p (CMD_GET_PARMS, padapter->cmd);
	if ( WaitReady (padapter) )
		return 0;

	geom[0] = inb_p (padapter->mb2 + 3);
	geom[1] = inb_p (padapter->mb2 + 2);
	geom[2] = inw_p (padapter->mb2);
	return 0;
	}


#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = PCI2000;

#include "scsi_module.c"
#endif

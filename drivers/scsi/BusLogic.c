/*

  Linux Driver for BusLogic MultiMaster SCSI Host Adapters

  Copyright 1995 by Leonard N. Zubkoff <lnz@dandelion.com>

  This program is free software; you may redistribute and/or modify it under
  the terms of the GNU General Public License Version 2 as published by the
  Free Software Foundation, provided that none of the source code or runtime
  copyright notices are removed or modified.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for complete details.

  The author respectfully requests that any modifications to this software be
  sent directly to him for evaluation and testing.

  Special thanks to Wayne Yen and Alex Win of BusLogic, whose advice has been
  invaluable, to David Gentzel, for writing the original Linux BusLogic driver,
  and to Paul Gortmaker, for being such a dedicated test site.

*/


#define BusLogic_DriverVersion		"2.0.4"
#define BusLogic_DriverDate		"5 June 1996"


#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/system.h>
#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#include "BusLogic.h"


/*
  BusLogic_CommandLineEntryCount is a count of the number of "BusLogic="
  entries provided on the Linux Kernel Command Line.
*/

static int
  BusLogic_CommandLineEntryCount =	0;


/*
  BusLogic_CommandLineEntries is an array of Command Line Entry structures
  representing the "BusLogic=" entries provided on the Linux Kernel Command
  Line.
*/

static BusLogic_CommandLineEntry_T
  BusLogic_CommandLineEntries[BusLogic_MaxHostAdapters];


/*
  BusLogic_ProbeOptions is a bit mask of Probe Options to be applied
  across all Host Adapters.
*/

static int
  BusLogic_ProbeOptions =		0;


/*
  BusLogic_GlobalOptions is a bit mask of Global Options to be applied
  across all Host Adapters.
*/

static int
  BusLogic_GlobalOptions =		0;


/*
  BusLogic_RegisteredHostAdapters is a linked list of all the registered
  BusLogic Host Adapters.
*/

static BusLogic_HostAdapter_T
  *BusLogic_RegisteredHostAdapters =	NULL;


/*
  BusLogic_StandardAddresses is the list of standard ISA I/O Addresses at
  which BusLogic Host Adapters may potentially be found.
*/

static unsigned int
  BusLogic_IO_StandardAddresses[] =
    { 0x330, 0x334, 0x230, 0x234, 0x130, 0x134, 0 };


/*
  BusLogic_IO_AddressProbeList is the list of I/O Addresses to be probed for
  potential BusLogic Host Adapters.  It is initialized by interrogating the
  PCI Configuration Space on PCI machines as well as from the list of
  standard BusLogic I/O Addresses.
*/

static unsigned int
  BusLogic_IO_AddressProbeList[BusLogic_IO_MaxProbeAddresses+1] =   { 0 };


/*
  BusLogic_IRQ_UsageCount stores a count of the number of Host Adapters using
  a given IRQ Channel, which is necessary to support PCI, EISA, or MCA shared
  interrupts.
*/

static int
  BusLogic_IRQ_UsageCount[NR_IRQS] =	{ 0 };


/*
  BusLogic_CommandFailureReason holds a string identifying the reason why a
  call to BusLogic_Command failed.  It is only non-NULL when BusLogic_Command
  returns a failure code.
*/

static char
  *BusLogic_CommandFailureReason;


/*
  BusLogic_ProcDirectoryEntry is the BusLogic /proc/scsi directory entry.
*/

static struct proc_dir_entry
  BusLogic_ProcDirectoryEntry =
    { PROC_SCSI_BUSLOGIC, 8, "BusLogic", S_IFDIR | S_IRUGO | S_IXUGO, 2 };


/*
  BusLogic_AnnounceDriver announces the Driver Version and Date, Author's
  Name, Copyright Notice, and Contact Address.
*/

static void BusLogic_AnnounceDriver(void)
{
  static boolean DriverAnnouncementPrinted = false;
  if (DriverAnnouncementPrinted) return;
  printk("scsi: ***** BusLogic SCSI Driver Version "
	 BusLogic_DriverVersion " of " BusLogic_DriverDate " *****\n");
  printk("scsi: Copyright 1995 by Leonard N. Zubkoff <lnz@dandelion.com>\n");
  DriverAnnouncementPrinted = true;
}


/*
  BusLogic_DriverInfo returns the Board Name to identify this SCSI Driver
  and Host Adapter.
*/

const char *BusLogic_DriverInfo(SCSI_Host_T *Host)
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Host->hostdata;
  return HostAdapter->BoardName;
}


/*
  BusLogic_RegisterHostAdapter adds Host Adapter to the list of registered
  BusLogic Host Adapters.
*/

static void BusLogic_RegisterHostAdapter(BusLogic_HostAdapter_T *HostAdapter)
{
  HostAdapter->Next = NULL;
  if (BusLogic_RegisteredHostAdapters != NULL)
    {
      BusLogic_HostAdapter_T *LastHostAdapter = BusLogic_RegisteredHostAdapters;
      BusLogic_HostAdapter_T *NextHostAdapter;
      while ((NextHostAdapter = LastHostAdapter->Next) != NULL)
	LastHostAdapter = NextHostAdapter;
      LastHostAdapter->Next = HostAdapter;
    }
  else BusLogic_RegisteredHostAdapters = HostAdapter;
}


/*
  BusLogic_UnregisterHostAdapter removes Host Adapter from the list of
  registered BusLogic Host Adapters.
*/

static void BusLogic_UnregisterHostAdapter(BusLogic_HostAdapter_T *HostAdapter)
{
  if (BusLogic_RegisteredHostAdapters != HostAdapter)
    {
      BusLogic_HostAdapter_T *LastHostAdapter = BusLogic_RegisteredHostAdapters;
      while (LastHostAdapter != NULL && LastHostAdapter->Next != HostAdapter)
	LastHostAdapter = LastHostAdapter->Next;
      if (LastHostAdapter != NULL)
	LastHostAdapter->Next = HostAdapter->Next;
    }
  else BusLogic_RegisteredHostAdapters = HostAdapter->Next;
  HostAdapter->Next = NULL;
}


/*
  BusLogic_CreateMailboxes allocates the Outgoing and Incoming Mailboxes for
  Host Adapter.
*/

static boolean BusLogic_CreateMailboxes(BusLogic_HostAdapter_T *HostAdapter)
{
  HostAdapter->FirstOutgoingMailbox =
    (BusLogic_OutgoingMailbox_T *)
      scsi_init_malloc(HostAdapter->MailboxCount
		       * (sizeof(BusLogic_OutgoingMailbox_T)
			  + sizeof(BusLogic_IncomingMailbox_T)),
		       (HostAdapter->BounceBuffersRequired
			? GFP_ATOMIC | GFP_DMA
			: GFP_ATOMIC));
  if (HostAdapter->FirstOutgoingMailbox == NULL)
    {
      printk("scsi%d: UNABLE TO ALLOCATE MAILBOXES - DETACHING\n",
	     HostAdapter->HostNumber);
      return false;
    }
  HostAdapter->LastOutgoingMailbox =
    HostAdapter->FirstOutgoingMailbox + HostAdapter->MailboxCount - 1;
  HostAdapter->FirstIncomingMailbox =
    (BusLogic_IncomingMailbox_T *) (HostAdapter->LastOutgoingMailbox + 1);
  HostAdapter->LastIncomingMailbox =
    HostAdapter->FirstIncomingMailbox + HostAdapter->MailboxCount - 1;
  return true;
}


/*
  BusLogic_DestroyMailboxes deallocates the Outgoing and Incoming Mailboxes
  for Host Adapter.
*/

static void BusLogic_DestroyMailboxes(BusLogic_HostAdapter_T *HostAdapter)
{
  if (HostAdapter->FirstOutgoingMailbox == NULL) return;
  scsi_init_free((char *) HostAdapter->FirstOutgoingMailbox,
		 HostAdapter->MailboxCount
		 * (sizeof(BusLogic_OutgoingMailbox_T)
		    + sizeof(BusLogic_IncomingMailbox_T)));
}


/*
  BusLogic_CreateCCBs allocates the initial Command Control Blocks (CCBs)
  for Host Adapter.
*/

static boolean BusLogic_CreateCCBs(BusLogic_HostAdapter_T *HostAdapter)
{
  int i;
  for (i = 0; i < HostAdapter->InitialCCBs; i++)
    {
      BusLogic_CCB_T *CCB = (BusLogic_CCB_T *)
	scsi_init_malloc(sizeof(BusLogic_CCB_T),
			 (HostAdapter->BounceBuffersRequired
			  ? GFP_ATOMIC | GFP_DMA
			  : GFP_ATOMIC));
      if (CCB == NULL)
	{
	  printk("scsi%d: UNABLE TO ALLOCATE CCB %d - DETACHING\n",
		 HostAdapter->HostNumber, i);
	  return false;
	}
      memset(CCB, 0, sizeof(BusLogic_CCB_T));
      CCB->HostAdapter = HostAdapter;
      CCB->Status = BusLogic_CCB_Free;
      CCB->Next = HostAdapter->Free_CCBs;
      CCB->NextAll = HostAdapter->All_CCBs;
      HostAdapter->Free_CCBs = CCB;
      HostAdapter->All_CCBs = CCB;
    }
  return true;
}


/*
  BusLogic_DestroyCCBs deallocates the CCBs for Host Adapter.
*/

static void BusLogic_DestroyCCBs(BusLogic_HostAdapter_T *HostAdapter)
{
  BusLogic_CCB_T *NextCCB = HostAdapter->All_CCBs, *CCB;
  HostAdapter->All_CCBs = NULL;
  HostAdapter->Free_CCBs = NULL;
  while ((CCB = NextCCB) != NULL)
    {
      NextCCB = CCB->NextAll;
      scsi_init_free((char *) CCB, sizeof(BusLogic_CCB_T));
    }
}


/*
  BusLogic_AllocateCCB allocates a CCB from the Host Adapter's free list,
  allocating more memory from the Kernel if necessary.  The Host Adapter's
  Lock should have already been acquired by the caller.
*/

static BusLogic_CCB_T *BusLogic_AllocateCCB(BusLogic_HostAdapter_T *HostAdapter)
{
  static unsigned long SerialNumber = 0;
  BusLogic_CCB_T *CCB;
  int Allocated;
  CCB = HostAdapter->Free_CCBs;
  if (CCB != NULL)
    {
      CCB->SerialNumber = ++SerialNumber;
      HostAdapter->Free_CCBs = CCB->Next;
      CCB->Next = NULL;
      return CCB;
    }
  for (Allocated = 0; Allocated < HostAdapter->IncrementalCCBs; Allocated++)
    {
      CCB = (BusLogic_CCB_T *)
	scsi_init_malloc(sizeof(BusLogic_CCB_T),
			 (HostAdapter->BounceBuffersRequired
			  ? GFP_ATOMIC | GFP_DMA
			  : GFP_ATOMIC));
      if (CCB == NULL) break;
      memset(CCB, 0, sizeof(BusLogic_CCB_T));
      CCB->HostAdapter = HostAdapter;
      CCB->Status = BusLogic_CCB_Free;
      CCB->Next = HostAdapter->Free_CCBs;
      CCB->NextAll = HostAdapter->All_CCBs;
      HostAdapter->Free_CCBs = CCB;
      HostAdapter->All_CCBs = CCB;
    }
  CCB = HostAdapter->Free_CCBs;
  if (CCB == NULL)
    {
      printk("scsi%d: Failed to allocate additional CCBs\n",
	     HostAdapter->HostNumber);
      return NULL;
    }
  printk("scsi%d: Allocated %d additional CCBs\n",
	 HostAdapter->HostNumber, Allocated);
  CCB->SerialNumber = ++SerialNumber;
  HostAdapter->Free_CCBs = CCB->Next;
  CCB->Next = NULL;
  return CCB;
}


/*
  BusLogic_DeallocateCCB deallocates a CCB, returning it to the Host Adapter's
  free list.  The Host Adapter's Lock should have already been acquired by the
  caller.
*/

static void BusLogic_DeallocateCCB(BusLogic_CCB_T *CCB)
{
  BusLogic_HostAdapter_T *HostAdapter = CCB->HostAdapter;
  CCB->Command = NULL;
  CCB->Status = BusLogic_CCB_Free;
  CCB->Next = HostAdapter->Free_CCBs;
  HostAdapter->Free_CCBs = CCB;
}


/*
  BusLogic_Command sends the command OperationCode to HostAdapter, optionally
  providing ParameterLength bytes of ParameterData and receiving at most
  ReplyLength bytes of ReplyData; any excess reply data is received but
  discarded.

  On success, this function returns the number of reply bytes read from
  the Host Adapter (including any discarded data); on failure, it returns
  -1 if the command was invalid, or -2 if a timeout occurred.

  This function is only called during board detection and initialization, so
  performance and latency are not critical, and exclusive access to the Host
  Adapter hardware is assumed.  Once the board and driver are initialized, the
  only Host Adapter command that is issued is the single byte Execute Mailbox
  Command operation code , which does not require waiting for the Host Adapter
  Ready bit to be set in the Status Register.
*/

static int BusLogic_Command(BusLogic_HostAdapter_T *HostAdapter,
			    BusLogic_OperationCode_T OperationCode,
			    void *ParameterData,
			    int ParameterLength,
			    void *ReplyData,
			    int ReplyLength)
{
  unsigned char *ParameterPointer = (unsigned char *) ParameterData;
  unsigned char *ReplyPointer = (unsigned char *) ReplyData;
  unsigned char StatusRegister = 0, InterruptRegister;
  int ReplyBytes = 0, TimeoutCounter;
  /*
    Clear out the Reply Data if provided.
  */
  if (ReplyLength > 0)
    memset(ReplyData, 0, ReplyLength);
  /*
    Wait for the Host Adapter Ready bit to be set and the Command/Parameter
    Register Busy bit to be reset in the Status Register.
  */
  TimeoutCounter = loops_per_sec >> 3;
  while (--TimeoutCounter >= 0)
    {
      StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
      if ((StatusRegister & BusLogic_HostAdapterReady) &&
	  !(StatusRegister & BusLogic_CommandParameterRegisterBusy))
	break;
    }
  BusLogic_CommandFailureReason = "Timeout waiting for Host Adapter Ready";
  if (TimeoutCounter < 0) return -2;
  /*
    Write the OperationCode to the Command/Parameter Register.
  */
  HostAdapter->HostAdapterCommandCompleted = false;
  BusLogic_WriteCommandParameterRegister(HostAdapter, OperationCode);
  /*
    Write any additional Parameter Bytes.
  */
  TimeoutCounter = 10000;
  while (ParameterLength > 0 && --TimeoutCounter >= 0)
    {
      /*
	Wait 100 microseconds to give the Host Adapter enough time to determine
	whether the last value written to the Command/Parameter Register was
	valid or not.  If the Command Complete bit is set in the Interrupt
	Register, then the Command Invalid bit in the Status Register will be
	reset if the Operation Code or Parameter was valid and the command
	has completed, or set if the Operation Code or Parameter was invalid.
	If the Data In Register Ready bit is set in the Status Register, then
	the Operation Code was valid, and data is waiting to be read back
	from the Host Adapter.  Otherwise, wait for the Command/Parameter
	Register Busy bit in the Status Register to be reset.
      */
      udelay(100);
      InterruptRegister = BusLogic_ReadInterruptRegister(HostAdapter);
      StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
      if (InterruptRegister & BusLogic_CommandComplete) break;
      if (HostAdapter->HostAdapterCommandCompleted) break;
      if (StatusRegister & BusLogic_DataInRegisterReady) break;
      if (StatusRegister & BusLogic_CommandParameterRegisterBusy) continue;
      BusLogic_WriteCommandParameterRegister(HostAdapter, *ParameterPointer++);
      ParameterLength--;
    }
  BusLogic_CommandFailureReason = "Timeout waiting for Parameter Acceptance";
  if (TimeoutCounter < 0) return -2;
  /*
    The Modify I/O Address command does not cause a Command Complete Interrupt.
  */
  if (OperationCode == BusLogic_ModifyIOAddress)
    {
      StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
      BusLogic_CommandFailureReason = "Modify I/O Address Invalid";
      if (StatusRegister & BusLogic_CommandInvalid) return -1;
      BusLogic_CommandFailureReason = NULL;
      return 0;
    }
  /*
    Select an appropriate timeout value for awaiting command completion.
  */
  switch (OperationCode)
    {
    case BusLogic_InquireInstalledDevicesID0to7:
    case BusLogic_InquireInstalledDevicesID8to15:
    case BusLogic_InquireDevices:
      /* Approximately 60 seconds. */
      TimeoutCounter = loops_per_sec << 2;
      break;
    default:
      /* Approximately 1 second. */
      TimeoutCounter = loops_per_sec >> 4;
      break;
    }
  /*
    Receive any Reply Bytes, waiting for either the Command Complete bit to
    be set in the Interrupt Register, or for the Interrupt Handler to set the
    Host Adapter Command Completed bit in the Host Adapter structure.
  */
  while (--TimeoutCounter >= 0)
    {
      InterruptRegister = BusLogic_ReadInterruptRegister(HostAdapter);
      StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
      if (InterruptRegister & BusLogic_CommandComplete) break;
      if (HostAdapter->HostAdapterCommandCompleted) break;
      if (StatusRegister & BusLogic_DataInRegisterReady)
	if (++ReplyBytes <= ReplyLength)
	  *ReplyPointer++ = BusLogic_ReadDataInRegister(HostAdapter);
	else BusLogic_ReadDataInRegister(HostAdapter);
      if (OperationCode == BusLogic_FetchHostAdapterLocalRAM &&
	  (StatusRegister & BusLogic_HostAdapterReady)) break;
    }
  BusLogic_CommandFailureReason = "Timeout waiting for Command Complete";
  if (TimeoutCounter < 0) return -2;
  /*
    If testing Command Complete Interrupts, wait a short while in case the
    loop immediately above terminated due to the Command Complete bit being
    set in the Interrupt Register, but the interrupt hasn't actually been
    processed yet.  Otherwise, acknowledging the interrupt here could prevent
    the interrupt test from succeeding.
  */
  if (OperationCode == BusLogic_TestCommandCompleteInterrupt)
    udelay(10000);
  /*
    Clear any pending Command Complete Interrupt.
  */
  BusLogic_WriteControlRegister(HostAdapter, BusLogic_InterruptReset);
  if (BusLogic_GlobalOptions & BusLogic_TraceConfiguration)
    if (OperationCode != BusLogic_TestCommandCompleteInterrupt)
      {
	int i;
	printk("BusLogic_Command(%02X) Status = %02X: %2d ==> %2d:",
	       OperationCode, StatusRegister, ReplyLength, ReplyBytes);
	if (ReplyLength > ReplyBytes) ReplyLength = ReplyBytes;
	for (i = 0; i < ReplyLength; i++)
	  printk(" %02X", ((unsigned char *) ReplyData)[i]);
	printk("\n");
      }
  /*
    Process Command Invalid conditions.
  */
  if (StatusRegister & BusLogic_CommandInvalid)
    {
      /*
	Some early BusLogic Host Adapters may not recover properly from
	a Command Invalid condition, so if this appears to be the case,
	a Soft Reset is issued to the Host Adapter.  Potentially invalid
	commands are never attempted after Mailbox Initialization is
	performed, so there should be no Host Adapter state lost by a
	Soft Reset in response to a Command Invalid condition.
      */
      udelay(1000);
      StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
      if (StatusRegister != (BusLogic_HostAdapterReady |
			     BusLogic_InitializationRequired))
	{
	  BusLogic_WriteControlRegister(HostAdapter, BusLogic_SoftReset);
	  udelay(1000);
	}
      BusLogic_CommandFailureReason = "Command Invalid";
      return -1;
    }
  /*
    Handle Excess Parameters Supplied conditions.
  */
  BusLogic_CommandFailureReason = "Excess Parameters Supplied";
  if (ParameterLength > 0) return -1;
  /*
    Indicate the command completed successfully.
  */
  BusLogic_CommandFailureReason = NULL;
  return ReplyBytes;
}


/*
  BusLogic_InitializeAddressProbeList initializes the list of I/O Addresses
  to be probed for potential BusLogic SCSI Host Adapters by interrogating the
  PCI Configuration Space on PCI machines as well as from the list of standard
  BusLogic I/O Addresses.
*/

static void BusLogic_InitializeAddressProbeList(void)
{
  int ProbeAddressCount = 0, StandardAddressIndex = 0;
  /*
    If BusLogic_Setup has provided an I/O Address probe list, do not override
    the Kernel Command Line specifications.
  */
  if (BusLogic_IO_AddressProbeList[0] != 0) return;
#ifdef CONFIG_PCI
  /*
    Interrogate PCI Configuration Space for any BusLogic SCSI Host Adapters.
  */
  if (pcibios_present())
    {
      unsigned int BusDeviceFunction[BusLogic_IO_MaxProbeAddresses];
      unsigned short Index = 0, VendorID, DeviceID;
      boolean NonIncreasingScanningOrder = false;
      unsigned char Bus, DeviceFunction;
      unsigned int BaseAddress0;
      while (pcibios_find_class(PCI_CLASS_STORAGE_SCSI<<8, Index++,
				&Bus, &DeviceFunction) == 0)
	if (pcibios_read_config_word(Bus, DeviceFunction,
				     PCI_VENDOR_ID, &VendorID) == 0 &&
	    VendorID == PCI_VENDOR_ID_BUSLOGIC &&
	    pcibios_read_config_word(Bus, DeviceFunction,
				     PCI_DEVICE_ID, &DeviceID) == 0 &&
	    (DeviceID == PCI_DEVICE_ID_BUSLOGIC_946C ||
	     DeviceID == PCI_DEVICE_ID_BUSLOGIC_946C_2) &&
	    pcibios_read_config_dword(Bus, DeviceFunction,
				      PCI_BASE_ADDRESS_0, &BaseAddress0) == 0 &&
	    (BaseAddress0 & PCI_BASE_ADDRESS_SPACE) ==
	      PCI_BASE_ADDRESS_SPACE_IO)
	  {
	    BusLogic_IO_AddressProbeList[ProbeAddressCount] =
	      BaseAddress0 & PCI_BASE_ADDRESS_IO_MASK;
	    BusDeviceFunction[ProbeAddressCount] = (Bus << 8) | DeviceFunction;
	    if (ProbeAddressCount > 0 &&
		BusDeviceFunction[ProbeAddressCount] <
		  BusDeviceFunction[ProbeAddressCount-1])
	      NonIncreasingScanningOrder = true;
	    ProbeAddressCount++;
	  }
      /*
	If there are multiple BusLogic PCI SCSI Host Adapters present and if
	they are enumerated by the PCI BIOS in an order other than by strictly
	increasing Bus Number and Device Number, then interrogate the setting
	of the AutoSCSI "Use Bus And Device # For PCI Scanning Seq." option.
	If it is ON, and if the first enumeratedBusLogic Host Adapter is a
	BT-948/958/958D, then sort the PCI Host Adapter I/O Addresses by
	increasing Bus Number and Device Number so that the Host Adapters are
	recognized in the same order by the Linux kernel as by the Host
	Adapter's BIOS.
      */
      if (ProbeAddressCount > 1 && NonIncreasingScanningOrder &&
	  !(BusLogic_ProbeOptions & BusLogic_NoSortPCI))
	{
	  BusLogic_HostAdapter_T HostAdapterPrototype;
	  BusLogic_HostAdapter_T *HostAdapter = &HostAdapterPrototype;
	  BusLogic_FetchHostAdapterLocalRAMRequest_T
	    FetchHostAdapterLocalRAMRequest;
	  BusLogic_AutoSCSIByte45_T AutoSCSIByte45;
	  BusLogic_BoardID_T BoardID;
	  HostAdapter->IO_Address = BusLogic_IO_AddressProbeList[0];
	  FetchHostAdapterLocalRAMRequest.ByteOffset =
	    BusLogic_AutoSCSI_BaseOffset + 45;
	  FetchHostAdapterLocalRAMRequest.ByteCount = sizeof(AutoSCSIByte45);
	  AutoSCSIByte45.ForceBusDeviceScanningOrder = false;
	  BusLogic_Command(HostAdapter,
			   BusLogic_FetchHostAdapterLocalRAM,
			   &FetchHostAdapterLocalRAMRequest,
			   sizeof(FetchHostAdapterLocalRAMRequest),
			   &AutoSCSIByte45, sizeof(AutoSCSIByte45));
	  BoardID.FirmwareVersion1stDigit = '\0';
	  BusLogic_Command(HostAdapter, BusLogic_InquireBoardID,
			   NULL, 0, &BoardID, sizeof(BoardID));
	  if (BoardID.FirmwareVersion1stDigit == '5' &&
	      AutoSCSIByte45.ForceBusDeviceScanningOrder)
	    {
	      /*
		Sort the I/O Addresses such that the corresponding
		PCI devices are in ascending order by Bus Number and
		Device Number.
	      */
	      int LastInterchange = ProbeAddressCount-1, Bound, j;
	      while (LastInterchange > 0)
		{
		  Bound = LastInterchange;
		  LastInterchange = 0;
		  for (j = 0; j < Bound; j++)
		    if (BusDeviceFunction[j] > BusDeviceFunction[j+1])
		      {
			unsigned int Temp;
			Temp = BusDeviceFunction[j];
			BusDeviceFunction[j] = BusDeviceFunction[j+1];
			BusDeviceFunction[j+1] = Temp;
			Temp = BusLogic_IO_AddressProbeList[j];
			BusLogic_IO_AddressProbeList[j] =
			  BusLogic_IO_AddressProbeList[j+1];
			BusLogic_IO_AddressProbeList[j+1] = Temp;
			LastInterchange = j;
		      }
		}
	    }
	}
    }
#endif
  /*
    Append the list of standard BusLogic ISA I/O Addresses.
  */
  if (!(BusLogic_ProbeOptions & BusLogic_NoProbeISA))
    while (ProbeAddressCount < BusLogic_IO_MaxProbeAddresses &&
	   BusLogic_IO_StandardAddresses[StandardAddressIndex] > 0)
      BusLogic_IO_AddressProbeList[ProbeAddressCount++] =
	BusLogic_IO_StandardAddresses[StandardAddressIndex++];
  BusLogic_IO_AddressProbeList[ProbeAddressCount] = 0;
}


/*
  BusLogic_Failure prints a standardized error message, and then returns false.
*/

static boolean BusLogic_Failure(BusLogic_HostAdapter_T *HostAdapter,
				char *ErrorMessage)
{
  BusLogic_AnnounceDriver();
  printk("While configuring BusLogic Host Adapter at I/O Address 0x%X:\n",
	 HostAdapter->IO_Address);
  printk("%s FAILED - DETACHING\n", ErrorMessage);
  if (BusLogic_CommandFailureReason != NULL)
    printk("ADDITIONAL FAILURE INFO - %s\n", BusLogic_CommandFailureReason);
  return false;
}


/*
  BusLogic_ProbeHostAdapter probes for a BusLogic Host Adapter.
*/

static boolean BusLogic_ProbeHostAdapter(BusLogic_HostAdapter_T *HostAdapter)
{
  boolean TraceProbe = (BusLogic_GlobalOptions & BusLogic_TraceProbe);
  unsigned char StatusRegister, GeometryRegister;
  /*
    Read the Status Register to test if there is an I/O port that responds.  A
    nonexistent I/O port will return 0xFF, in which case there is definitely no
    BusLogic Host Adapter at this base I/O Address.
  */
  StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
  if (TraceProbe)
    printk("BusLogic_Probe(0x%X): Status 0x%02X\n",
	   HostAdapter->IO_Address, StatusRegister);
  if (StatusRegister == 0xFF) return false;
  /*
    Read the undocumented BusLogic Geometry Register to test if there is an I/O
    port that responds.  Adaptec Host Adapters do not implement the Geometry
    Register, so this test helps serve to avoid incorrectly recognizing an
    Adaptec 1542A or 1542B as a BusLogic.  Unfortunately, the Adaptec 1542C
    series does respond to the Geometry Register I/O port, but it will be
    rejected later when the Inquire Extended Setup Information command is
    issued in BusLogic_CheckHostAdapter.  The AMI FastDisk Host Adapter is a
    BusLogic clone that implements the same interface as earlier BusLogic
    boards, including the undocumented commands, and is therefore supported by
    this driver.  However, the AMI FastDisk always returns 0x00 upon reading
    the Geometry Register, so the extended translation option should always be
    left disabled on the AMI FastDisk.
  */
  GeometryRegister = BusLogic_ReadGeometryRegister(HostAdapter);
  if (TraceProbe)
    printk("BusLogic_Probe(0x%X): Geometry 0x%02X\n",
	   HostAdapter->IO_Address, GeometryRegister);
  if (GeometryRegister == 0xFF) return false;
  /*
    Indicate the Host Adapter Probe completed successfully.
  */
  return true;
}


/*
  BusLogic_HardResetHostAdapter issues a Hard Reset to the Host Adapter,
  and waits for Host Adapter Diagnostics to complete.
*/

static boolean BusLogic_HardResetHostAdapter(BusLogic_HostAdapter_T
					     *HostAdapter)
{
  boolean TraceHardReset = (BusLogic_GlobalOptions & BusLogic_TraceHardReset);
  int TimeoutCounter = loops_per_sec >> 2;
  unsigned char StatusRegister = 0;
  /*
    Issue a Hard Reset Command to the Host Adapter.  The Host Adapter should
    respond by setting Diagnostic Active in the Status Register.
  */
  BusLogic_WriteControlRegister(HostAdapter, BusLogic_HardReset);
  /*
    Wait until Diagnostic Active is set in the Status Register.
  */
  while (--TimeoutCounter >= 0)
    {
      StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
      if ((StatusRegister & BusLogic_DiagnosticActive)) break;
    }
  if (TraceHardReset)
    printk("BusLogic_HardReset(0x%X): Diagnostic Active, Status 0x%02X\n",
	   HostAdapter->IO_Address, StatusRegister);
  if (TimeoutCounter < 0) return false;
  /*
    Wait 100 microseconds to allow completion of any initial diagnostic
    activity which might leave the contents of the Status Register
    unpredictable.
  */
  udelay(100);
  /*
    Wait until Diagnostic Active is reset in the Status Register.
  */
  while (--TimeoutCounter >= 0)
    {
      StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
      if (!(StatusRegister & BusLogic_DiagnosticActive)) break;
    }
  if (TraceHardReset)
    printk("BusLogic_HardReset(0x%X): Diagnostic Completed, Status 0x%02X\n",
	   HostAdapter->IO_Address, StatusRegister);
  if (TimeoutCounter < 0) return false;
  /*
    Wait until at least one of the Diagnostic Failure, Host Adapter Ready,
    or Data In Register Ready bits is set in the Status Register.
  */
  while (--TimeoutCounter >= 0)
    {
      StatusRegister = BusLogic_ReadStatusRegister(HostAdapter);
      if (StatusRegister & (BusLogic_DiagnosticFailure |
			    BusLogic_HostAdapterReady |
			    BusLogic_DataInRegisterReady))
	break;
    }
  if (TraceHardReset)
    printk("BusLogic_HardReset(0x%X): Host Adapter Ready, Status 0x%02X\n",
	   HostAdapter->IO_Address, StatusRegister);
  if (TimeoutCounter < 0) return false;
  /*
    If Diagnostic Failure is set or Host Adapter Ready is reset, then an
    error occurred during the Host Adapter diagnostics.  If Data In Register
    Ready is set, then there is an Error Code available.
  */
  if ((StatusRegister & BusLogic_DiagnosticFailure) ||
      !(StatusRegister & BusLogic_HostAdapterReady))
    {
      BusLogic_CommandFailureReason = NULL;
      BusLogic_Failure(HostAdapter, "HARD RESET DIAGNOSTICS");
      printk("HOST ADAPTER STATUS REGISTER = %02X\n", StatusRegister);
      if (StatusRegister & BusLogic_DataInRegisterReady)
	{
	  unsigned char ErrorCode = BusLogic_ReadDataInRegister(HostAdapter);
	  printk("HOST ADAPTER ERROR CODE = %d\n", ErrorCode);
	}
      return false;
    }
  /*
    Indicate the Host Adapter Hard Reset completed successfully.
  */
  return true;
}


/*
  BusLogic_CheckHostAdapter checks to be sure this really is a BusLogic
  Host Adapter.
*/

static boolean BusLogic_CheckHostAdapter(BusLogic_HostAdapter_T *HostAdapter)
{
  BusLogic_ExtendedSetupInformation_T ExtendedSetupInformation;
  BusLogic_RequestedReplyLength_T RequestedReplyLength;
  unsigned long ProcessorFlags;
  int Result;
  /*
    Issue the Inquire Extended Setup Information command.  Only genuine
    BusLogic Host Adapters and true clones support this command.  Adaptec 1542C
    series Host Adapters that respond to the Geometry Register I/O port will
    fail this command.  Interrupts must be disabled around the call to
    BusLogic_Command since a Command Complete interrupt could occur if the IRQ
    Channel was previously enabled for another BusLogic Host Adapter sharing
    the same IRQ Channel.
  */
  save_flags(ProcessorFlags);
  cli();
  RequestedReplyLength = sizeof(ExtendedSetupInformation);
  Result = BusLogic_Command(HostAdapter,
			    BusLogic_InquireExtendedSetupInformation,
			    &RequestedReplyLength, sizeof(RequestedReplyLength),
			    &ExtendedSetupInformation,
			    sizeof(ExtendedSetupInformation));
  restore_flags(ProcessorFlags);
  if (BusLogic_GlobalOptions & BusLogic_TraceProbe)
    printk("BusLogic_Check(0x%X): Result %d\n",
	   HostAdapter->IO_Address, Result);
  return (Result == sizeof(ExtendedSetupInformation));
}


/*
  BusLogic_ReadHostAdapterConfiguration reads the Configuration Information
  from Host Adapter.
*/

static boolean BusLogic_ReadHostAdapterConfiguration(BusLogic_HostAdapter_T
						     *HostAdapter)
{
  BusLogic_BoardID_T BoardID;
  BusLogic_Configuration_T Configuration;
  BusLogic_SetupInformation_T SetupInformation;
  BusLogic_ExtendedSetupInformation_T ExtendedSetupInformation;
  BusLogic_BoardModelNumber_T BoardModelNumber;
  BusLogic_FirmwareVersion3rdDigit_T FirmwareVersion3rdDigit;
  BusLogic_FirmwareVersionLetter_T FirmwareVersionLetter;
  BusLogic_GenericIOPortInformation_T GenericIOPortInformation;
  BusLogic_FetchHostAdapterLocalRAMRequest_T FetchHostAdapterLocalRAMRequest;
  BusLogic_AutoSCSIByte15_T AutoSCSIByte15;
  BusLogic_RequestedReplyLength_T RequestedReplyLength;
  unsigned char GeometryRegister, *TargetPointer, Character;
  unsigned short AllTargetsMask, DisconnectPermitted;
  unsigned short TaggedQueuingPermitted, TaggedQueuingPermittedDefault;
  boolean CommonErrorRecovery;
  int TargetID, i;
  /*
    Issue the Inquire Board ID command.
  */
  if (BusLogic_Command(HostAdapter, BusLogic_InquireBoardID, NULL, 0,
		       &BoardID, sizeof(BoardID)) != sizeof(BoardID))
    return BusLogic_Failure(HostAdapter, "INQUIRE BOARD ID");
  /*
    Issue the Inquire Configuration command.
  */
  if (BusLogic_Command(HostAdapter, BusLogic_InquireConfiguration, NULL, 0,
		       &Configuration, sizeof(Configuration))
      != sizeof(Configuration))
    return BusLogic_Failure(HostAdapter, "INQUIRE CONFIGURATION");
  /*
    Issue the Inquire Setup Information command.
  */
  RequestedReplyLength = sizeof(SetupInformation);
  if (BusLogic_Command(HostAdapter, BusLogic_InquireSetupInformation,
		       &RequestedReplyLength, sizeof(RequestedReplyLength),
		       &SetupInformation, sizeof(SetupInformation))
      != sizeof(SetupInformation))
    return BusLogic_Failure(HostAdapter, "INQUIRE SETUP INFORMATION");
  /*
    Issue the Inquire Extended Setup Information command.
  */
  RequestedReplyLength = sizeof(ExtendedSetupInformation);
  if (BusLogic_Command(HostAdapter, BusLogic_InquireExtendedSetupInformation,
		       &RequestedReplyLength, sizeof(RequestedReplyLength),
		       &ExtendedSetupInformation,
		       sizeof(ExtendedSetupInformation))
      != sizeof(ExtendedSetupInformation))
    return BusLogic_Failure(HostAdapter, "INQUIRE EXTENDED SETUP INFORMATION");
  /*
    Issue the Inquire Board Model Number command.
  */
  if (!(BoardID.FirmwareVersion1stDigit == '2' &&
	ExtendedSetupInformation.BusType == 'A'))
    {
      RequestedReplyLength = sizeof(BoardModelNumber);
      if (BusLogic_Command(HostAdapter, BusLogic_InquireBoardModelNumber,
			   &RequestedReplyLength, sizeof(RequestedReplyLength),
			   &BoardModelNumber, sizeof(BoardModelNumber))
	  != sizeof(BoardModelNumber))
	return BusLogic_Failure(HostAdapter, "INQUIRE BOARD MODEL NUMBER");
    }
  else strcpy(BoardModelNumber, "542B");
  /*
    Issue the Inquire Firmware Version 3rd Digit command.
  */
  if (BusLogic_Command(HostAdapter, BusLogic_InquireFirmwareVersion3rdDigit,
		       NULL, 0, &FirmwareVersion3rdDigit,
		       sizeof(FirmwareVersion3rdDigit))
      != sizeof(FirmwareVersion3rdDigit))
    return BusLogic_Failure(HostAdapter, "INQUIRE FIRMWARE 3RD DIGIT");
  /*
    BusLogic Host Adapters can be identified by their model number and
    the major version number of their firmware as follows:

    5.xx	BusLogic "W" Series Host Adapters:
		  BT-948/958/958D
    4.xx	BusLogic "C" Series Host Adapters:
		  BT-946C/956C/956CD/747C/757C/757CD/445C/545C/540CF
    3.xx	BusLogic "S" Series Host Adapters:
		  BT-747S/747D/757S/757D/445S/545S/542D
		  BT-542B/742A (revision H)
    2.xx	BusLogic "A" Series Host Adapters:
		  BT-542B/742A (revision G and below)
    0.xx	AMI FastDisk VLB/EISA BusLogic Clone Host Adapter
  */
  /*
    Save the Model Name and Board Name in the Host Adapter structure.
  */
  TargetPointer = HostAdapter->ModelName;
  *TargetPointer++ = 'B';
  *TargetPointer++ = 'T';
  *TargetPointer++ = '-';
  for (i = 0; i < sizeof(BoardModelNumber); i++)
    {
      Character = BoardModelNumber[i];
      if (Character == ' ' || Character == '\0') break;
      *TargetPointer++ = Character;
    }
  *TargetPointer++ = '\0';
  strcpy(HostAdapter->BoardName, "BusLogic ");
  strcat(HostAdapter->BoardName, HostAdapter->ModelName);
  strcpy(HostAdapter->InterruptLabel, HostAdapter->BoardName);
  /*
    Save the Firmware Version in the Host Adapter structure.
  */
  TargetPointer = HostAdapter->FirmwareVersion;
  *TargetPointer++ = BoardID.FirmwareVersion1stDigit;
  *TargetPointer++ = '.';
  *TargetPointer++ = BoardID.FirmwareVersion2ndDigit;
  if (FirmwareVersion3rdDigit != ' ' && FirmwareVersion3rdDigit != '\0')
    *TargetPointer++ = FirmwareVersion3rdDigit;
  *TargetPointer = '\0';
  /*
    Issue the Inquire Firmware Version Letter command.
  */
  if (strcmp(HostAdapter->FirmwareVersion, "3.3") >= 0)
    {
      if (BusLogic_Command(HostAdapter, BusLogic_InquireFirmwareVersionLetter,
			   NULL, 0, &FirmwareVersionLetter,
			   sizeof(FirmwareVersionLetter))
	  != sizeof(FirmwareVersionLetter))
	return BusLogic_Failure(HostAdapter, "INQUIRE FIRMWARE VERSION LETTER");
      if (FirmwareVersionLetter != ' ' && FirmwareVersionLetter != '\0')
	*TargetPointer++ = FirmwareVersionLetter;
      *TargetPointer = '\0';
    }
  /*
    Issue the Inquire Generic I/O Port Information command to read the
    IRQ Channel from all PCI Host Adapters, and the Termination Information
    from "W" Series Host Adapters.
  */
  if (HostAdapter->ModelName[3] == '9' &&
      strcmp(HostAdapter->FirmwareVersion, "4.25") >= 0)
    {
      if (BusLogic_Command(HostAdapter,
			   BusLogic_InquireGenericIOPortInformation,
			   NULL, 0, &GenericIOPortInformation,
			   sizeof(GenericIOPortInformation))
	  != sizeof(GenericIOPortInformation))
	return BusLogic_Failure(HostAdapter,
				"INQUIRE GENERIC I/O PORT INFORMATION");
      /*
	Save the IRQ Channel in the Host Adapter structure.
      */
      HostAdapter->IRQ_Channel = GenericIOPortInformation.PCIAssignedIRQChannel;
      /*
	Save the Termination Information in the Host Adapter structure.
      */
      if (HostAdapter->FirmwareVersion[0] == '5' &&
	  GenericIOPortInformation.Valid)
	{
	  HostAdapter->TerminationInfoValid = true;
	  HostAdapter->LowByteTerminated =
	    GenericIOPortInformation.LowByteTerminated;
	  HostAdapter->HighByteTerminated =
	    GenericIOPortInformation.HighByteTerminated;
	}
    }
  /*
    Issue the Fetch Host Adapter Local RAM command to read the Termination
    Information from the AutoSCSI area of "C" Series Host Adapters.
  */
  if (HostAdapter->FirmwareVersion[0] == '4')
    {
      FetchHostAdapterLocalRAMRequest.ByteOffset =
	BusLogic_AutoSCSI_BaseOffset + 15;
      FetchHostAdapterLocalRAMRequest.ByteCount = sizeof(AutoSCSIByte15);
      if (BusLogic_Command(HostAdapter,
			   BusLogic_FetchHostAdapterLocalRAM,
			   &FetchHostAdapterLocalRAMRequest,
			   sizeof(FetchHostAdapterLocalRAMRequest),
			   &AutoSCSIByte15, sizeof(AutoSCSIByte15))
	  != sizeof(AutoSCSIByte15))
	return BusLogic_Failure(HostAdapter, "FETCH HOST ADAPTER LOCAL RAM");
      /*
	Save the Termination Information in the Host Adapter structure.
      */
      HostAdapter->TerminationInfoValid = true;
      HostAdapter->LowByteTerminated = AutoSCSIByte15.LowByteTerminated;
      HostAdapter->HighByteTerminated = AutoSCSIByte15.HighByteTerminated;
    }
  /*
    Determine the IRQ Channel and save it in the Host Adapter structure.
  */
  if (HostAdapter->IRQ_Channel == 0)
    {
      if (Configuration.IRQ_Channel9)
	HostAdapter->IRQ_Channel = 9;
      else if (Configuration.IRQ_Channel10)
	HostAdapter->IRQ_Channel = 10;
      else if (Configuration.IRQ_Channel11)
	HostAdapter->IRQ_Channel = 11;
      else if (Configuration.IRQ_Channel12)
	HostAdapter->IRQ_Channel = 12;
      else if (Configuration.IRQ_Channel14)
	HostAdapter->IRQ_Channel = 14;
      else if (Configuration.IRQ_Channel15)
	HostAdapter->IRQ_Channel = 15;
    }
  /*
    Save the Host Adapter SCSI ID in the Host Adapter structure.
  */
  HostAdapter->SCSI_ID = Configuration.HostAdapterID;
  /*
    Save the Synchronous Initiation flag and SCSI Parity Checking flag
    in the Host Adapter structure.
  */
  HostAdapter->SynchronousInitiation =
    SetupInformation.SynchronousInitiationEnabled;
  HostAdapter->ParityChecking = SetupInformation.ParityCheckEnabled;
  /*
    Determine the Bus Type and save it in the Host Adapter structure,
    and determine and save the DMA Channel for ISA Host Adapters.
  */
  switch (HostAdapter->ModelName[3])
    {
    case '4':
      HostAdapter->BusType = BusLogic_VESA_Bus;
      break;
    case '5':
      HostAdapter->BusType = BusLogic_ISA_Bus;
      if (Configuration.DMA_Channel5)
	HostAdapter->DMA_Channel = 5;
      else if (Configuration.DMA_Channel6)
	HostAdapter->DMA_Channel = 6;
      else if (Configuration.DMA_Channel7)
	HostAdapter->DMA_Channel = 7;
      break;
    case '6':
      HostAdapter->BusType = BusLogic_MCA_Bus;
      break;
    case '7':
      HostAdapter->BusType = BusLogic_EISA_Bus;
      break;
    case '9':
      HostAdapter->BusType = BusLogic_PCI_Bus;
      break;
    }
  /*
    Determine whether Extended Translation is enabled and save it in
    the Host Adapter structure.
  */
  GeometryRegister = BusLogic_ReadGeometryRegister(HostAdapter);
  if (GeometryRegister & BusLogic_ExtendedTranslationEnabled)
    HostAdapter->ExtendedTranslation = true;
  /*
    Save the Disconnect/Reconnect Permitted flag bits in the Host Adapter
    structure.  The Disconnect Permitted information is only valid on "W" and
    "C" Series boards, but Disconnect/Reconnect is always permitted on "S" and
    "A" Series boards.
  */
  if (HostAdapter->FirmwareVersion[0] >= '4')
    HostAdapter->DisconnectPermitted =
      (SetupInformation.DisconnectPermittedID8to15 << 8)
      | SetupInformation.DisconnectPermittedID0to7;
  else HostAdapter->DisconnectPermitted = 0xFF;
  /*
    Save the Scatter Gather Limits, Level Sensitive Interrupts flag, Wide
    SCSI flag, Differential SCSI flag, Automatic Configuration flag, and
    Ultra SCSI flag in the Host Adapter structure.
  */
  HostAdapter->HostAdapterScatterGatherLimit =
    ExtendedSetupInformation.ScatterGatherLimit;
  HostAdapter->DriverScatterGatherLimit =
    HostAdapter->HostAdapterScatterGatherLimit;
  if (HostAdapter->HostAdapterScatterGatherLimit > BusLogic_ScatterGatherLimit)
    HostAdapter->DriverScatterGatherLimit = BusLogic_ScatterGatherLimit;
  if (ExtendedSetupInformation.Misc.LevelSensitiveInterrupts)
    HostAdapter->LevelSensitiveInterrupts = true;
  HostAdapter->HostWideSCSI = ExtendedSetupInformation.HostWideSCSI;
  HostAdapter->HostDifferentialSCSI =
    ExtendedSetupInformation.HostDifferentialSCSI;
  HostAdapter->HostAutomaticConfiguration =
    ExtendedSetupInformation.HostAutomaticConfiguration;
  HostAdapter->HostUltraSCSI = ExtendedSetupInformation.HostUltraSCSI;
  /*
    Determine the Host Adapter BIOS Address if the BIOS is enabled and
    save it in the Host Adapter structure.  The BIOS is disabled if the
    BIOS_Address is 0.
  */
  HostAdapter->BIOS_Address = ExtendedSetupInformation.BIOS_Address << 12;
  /*
    ISA Host Adapters require Bounce Buffers if there is more than 16MB memory.
  */
  if (HostAdapter->BusType == BusLogic_ISA_Bus && high_memory > MAX_DMA_ADDRESS)
    HostAdapter->BounceBuffersRequired = true;
  /*
    BusLogic BT-445S Host Adapters prior to board revision E have a hardware
    bug whereby when the BIOS is enabled, transfers to/from the same address
    range the BIOS occupies modulo 16MB are handled incorrectly.  Only properly
    functioning BT-445S boards have firmware version 3.37, so we require that
    ISA Bounce Buffers be used for the buggy BT-445S models if there is more
    than 16MB memory.
  */
  if (HostAdapter->BIOS_Address > 0 &&
      strcmp(HostAdapter->ModelName, "BT-445S") == 0 &&
      strcmp(HostAdapter->FirmwareVersion, "3.37") < 0 &&
      high_memory > MAX_DMA_ADDRESS)
    HostAdapter->BounceBuffersRequired = true;
  /*
    Determine the maximum number of Target IDs and Logical Units supported by
    this driver for Wide and Narrow Host Adapters.
  */
  if (HostAdapter->HostWideSCSI)
    {
      HostAdapter->MaxTargetDevices = 16;
      HostAdapter->MaxLogicalUnits = 64;
    }
  else
    {
      HostAdapter->MaxTargetDevices = 8;
      HostAdapter->MaxLogicalUnits = 8;
    }
  /*
    Select appropriate values for the Mailbox Count, Initial CCBs, and
    Incremental CCBs variables based on whether or not Strict Round Robin Mode
    is supported.  If Strict Round Robin Mode is supported, then there is no
    performance degradation in using the maximum possible number of Outgoing
    and Incoming Mailboxes and allowing the Tagged and Untagged Queue Depths to
    determine the actual utilization.  If Strict Round Robin Mode is not
    supported, then the Host Adapter must scan all the Outgoing Mailboxes
    whenever an Outgoing Mailbox entry is made, which can cause a substantial
    performance penalty.  The Host Adapters actually have room to store the
    following number of CCBs internally; that is, they can internally queue and
    manage this many active commands on the SCSI bus simultaneously.
    Performance measurements demonstrate that the Mailbox Count should be set
    to the maximum possible, rather than the internal CCB capacity, as it is
    more efficient to have the queued commands waiting in Outgoing Mailboxes if
    necessary than to block the process in the higher levels of the SCSI
    Subsystem.

	192	  BT-948/958/958D
	100	  BT-946C/956C/956CD/747C/757C/757CD/445C
	 50	  BT-545C/540CF
	 30	  BT-747S/747D/757S/757D/445S/545S/542D/542B/742A
  */
  if (strcmp(HostAdapter->FirmwareVersion, "3.31") >= 0)
    {
      HostAdapter->StrictRoundRobinModeSupported = true;
      HostAdapter->MailboxCount = 255;
      HostAdapter->InitialCCBs = 64;
      HostAdapter->IncrementalCCBs = 32;
    }
  else
    {
      HostAdapter->StrictRoundRobinModeSupported = false;
      HostAdapter->MailboxCount = 32;
      HostAdapter->InitialCCBs = 32;
      HostAdapter->IncrementalCCBs = 4;
    }
  if (HostAdapter->FirmwareVersion[0] == '5')
    HostAdapter->TotalQueueDepth = 192;
  else if (HostAdapter->FirmwareVersion[0] == '4')
    HostAdapter->TotalQueueDepth =
      (HostAdapter->BusType != BusLogic_ISA_Bus ? 100 : 50);
  else HostAdapter->TotalQueueDepth = 30;
  /*
    Select an appropriate value for the Tagged Queue Depth either from a
    Command Line Entry, or based on whether this Host Adapter requires that
    ISA Bounce Buffers be used.  The Tagged Queue Depth is left at 0 for
    automatic determination in BusLogic_SelectQueueDepths.  Initialize the
    Untagged Queue Depth.
  */
  if (HostAdapter->CommandLineEntry != NULL &&
      HostAdapter->CommandLineEntry->TaggedQueueDepth > 0)
    HostAdapter->TaggedQueueDepth =
      HostAdapter->CommandLineEntry->TaggedQueueDepth;
  else if (HostAdapter->BounceBuffersRequired)
    HostAdapter->TaggedQueueDepth = BusLogic_TaggedQueueDepth_BB;
  else HostAdapter->TaggedQueueDepth = 0;
  HostAdapter->UntaggedQueueDepth = BusLogic_UntaggedQueueDepth;
  if (HostAdapter->UntaggedQueueDepth > HostAdapter->TaggedQueueDepth &&
      HostAdapter->TaggedQueueDepth > 0)
    HostAdapter->UntaggedQueueDepth = HostAdapter->TaggedQueueDepth;
  /*
    Select an appropriate value for Bus Settle Time either from a Command
    Line Entry, or from BusLogic_DefaultBusSettleTime.
  */
  if (HostAdapter->CommandLineEntry != NULL &&
      HostAdapter->CommandLineEntry->BusSettleTime > 0)
    HostAdapter->BusSettleTime = HostAdapter->CommandLineEntry->BusSettleTime;
  else HostAdapter->BusSettleTime = BusLogic_DefaultBusSettleTime;
  /*
    Select an appropriate value for Local Options from a Command Line Entry.
  */
  if (HostAdapter->CommandLineEntry != NULL)
    HostAdapter->LocalOptions = HostAdapter->CommandLineEntry->LocalOptions;
  /*
    Select appropriate values for the Error Recovery Strategy array either from
    a Command Line Entry, or using BusLogic_ErrorRecovery_Default.
  */
  if (HostAdapter->CommandLineEntry != NULL)
    memcpy(HostAdapter->ErrorRecoveryStrategy,
	   HostAdapter->CommandLineEntry->ErrorRecoveryStrategy,
	   sizeof(HostAdapter->ErrorRecoveryStrategy));
  else memset(HostAdapter->ErrorRecoveryStrategy,
	      BusLogic_ErrorRecovery_Default,
	      sizeof(HostAdapter->ErrorRecoveryStrategy));
  /*
    Tagged Queuing support is available and operates properly on all "W" Series
    boards, on "C" Series boards with firmware version 4.22 and above, and on
    "S" Series boards with firmware version 3.35 and above.  Tagged Queuing is
    disabled by default when the Tagged Queue Depth is 1 since queuing multiple
    commands is not possible.
  */
  TaggedQueuingPermittedDefault = 0;
  if (HostAdapter->TaggedQueueDepth != 1)
    switch (HostAdapter->FirmwareVersion[0])
      {
      case '5':
	TaggedQueuingPermittedDefault = 0xFFFF;
	break;
      case '4':
	if (strcmp(HostAdapter->FirmwareVersion, "4.22") >= 0)
	  TaggedQueuingPermittedDefault = 0xFFFF;
	break;
      case '3':
	if (strcmp(HostAdapter->FirmwareVersion, "3.35") >= 0)
	  TaggedQueuingPermittedDefault = 0xFFFF;
	break;
      }
  /*
    Tagged Queuing is only useful if Disconnect/Reconnect is permitted.
    Therefore, mask the Tagged Queuing Permitted Default bits with the
    Disconnect/Reconnect Permitted bits.
  */
  TaggedQueuingPermittedDefault &= HostAdapter->DisconnectPermitted;
  /*
    Combine the default Tagged Queuing Permitted bits with any Command
    Line Entry Tagged Queuing specification.
  */
  if (HostAdapter->CommandLineEntry != NULL)
    HostAdapter->TaggedQueuingPermitted =
      (HostAdapter->CommandLineEntry->TaggedQueuingPermitted &
       HostAdapter->CommandLineEntry->TaggedQueuingPermittedMask) |
      (TaggedQueuingPermittedDefault &
       ~HostAdapter->CommandLineEntry->TaggedQueuingPermittedMask);
  else HostAdapter->TaggedQueuingPermitted = TaggedQueuingPermittedDefault;
  /*
    Announce the Host Adapter Configuration.
  */
  printk("scsi%d: Configuring BusLogic Model %s %s%s%s%s SCSI Host Adapter\n",
	 HostAdapter->HostNumber, HostAdapter->ModelName,
	 BusLogic_BusNames[HostAdapter->BusType],
	 (HostAdapter->HostWideSCSI ? " Wide" : ""),
	 (HostAdapter->HostDifferentialSCSI ? " Differential" : ""),
	 (HostAdapter->HostUltraSCSI ? " Ultra" : ""));
  printk("scsi%d:   Firmware Version: %s, I/O Address: 0x%X, "
	 "IRQ Channel: %d/%s\n",
	 HostAdapter->HostNumber, HostAdapter->FirmwareVersion,
	 HostAdapter->IO_Address, HostAdapter->IRQ_Channel,
	 (HostAdapter->LevelSensitiveInterrupts ? "Level" : "Edge"));
  printk("scsi%d:   DMA Channel: ", HostAdapter->HostNumber);
  if (HostAdapter->DMA_Channel > 0)
    printk("%d, ", HostAdapter->DMA_Channel);
  else printk("None, ");
  if (HostAdapter->BIOS_Address > 0)
    printk("BIOS Address: 0x%X, ", HostAdapter->BIOS_Address);
  else printk("BIOS Address: None, ");
  printk("Host Adapter SCSI ID: %d\n", HostAdapter->SCSI_ID);
  printk("scsi%d:   Scatter/Gather Limit: %d of %d segments, "
	 "Parity Checking: %s\n", HostAdapter->HostNumber,
	 HostAdapter->DriverScatterGatherLimit,
	 HostAdapter->HostAdapterScatterGatherLimit,
	 (HostAdapter->ParityChecking ? "Enabled" : "Disabled"));
  printk("scsi%d:   Synchronous Initiation: %s, "
	 "Extended Disk Translation: %s\n", HostAdapter->HostNumber,
	 (HostAdapter->SynchronousInitiation ? "Enabled" : "Disabled"),
	 (HostAdapter->ExtendedTranslation ? "Enabled" : "Disabled"));
  AllTargetsMask = (1 << HostAdapter->MaxTargetDevices) - 1;
  DisconnectPermitted = HostAdapter->DisconnectPermitted & AllTargetsMask;
  printk("scsi%d:   Disconnect/Reconnect: ", HostAdapter->HostNumber);
  if (DisconnectPermitted == 0)
    printk("Disabled");
  else if (DisconnectPermitted == AllTargetsMask)
    printk("Enabled");
  else
    for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
      printk("%c", (DisconnectPermitted & (1 << TargetID)) ? 'Y' : 'N');
  printk(", Tagged Queuing: ");
  TaggedQueuingPermitted =
    HostAdapter->TaggedQueuingPermitted & AllTargetsMask;
  if (TaggedQueuingPermitted == 0)
    printk("Disabled");
  else if (TaggedQueuingPermitted == AllTargetsMask)
    printk("Enabled");
  else
    for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
      printk("%c", (TaggedQueuingPermitted & (1 << TargetID)) ? 'Y' : 'N');
  printk("\n");
  printk("scsi%d:   Total Queue Depth: %d, Mailboxes: %d, Initial CCBs: %d\n",
	 HostAdapter->HostNumber, HostAdapter->TotalQueueDepth,
	 HostAdapter->MailboxCount, HostAdapter->InitialCCBs);
  printk("scsi%d:   Tagged Queue Depth: ", HostAdapter->HostNumber);
  if (HostAdapter->TaggedQueueDepth > 0)
    printk("%d", HostAdapter->TaggedQueueDepth);
  else printk("Automatic");
  printk(", Untagged Queue Depth: %d\n", HostAdapter->UntaggedQueueDepth);
  if (HostAdapter->TerminationInfoValid)
    if (HostAdapter->HostWideSCSI)
      printk("scsi%d:   Host Adapter SCSI Bus Termination (Low/High): %s/%s\n",
	     HostAdapter->HostNumber,
	     (HostAdapter->LowByteTerminated ? "Enabled" : "Disabled"),
	     (HostAdapter->HighByteTerminated ? "Enabled" : "Disabled"));
    else printk("scsi%d:   Host Adapter SCSI Bus Termination: %s\n",
		HostAdapter->HostNumber,
		(HostAdapter->LowByteTerminated ? "Enabled" : "Disabled"));
  CommonErrorRecovery = true;
  for (TargetID = 1; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    if (HostAdapter->ErrorRecoveryStrategy[TargetID] !=
	HostAdapter->ErrorRecoveryStrategy[0])
      {
	CommonErrorRecovery = false;
	break;
      }
  printk("scsi%d:   Error Recovery Strategy: ", HostAdapter->HostNumber);
  if (CommonErrorRecovery)
    printk("%s", BusLogic_ErrorRecoveryStrategyNames[
		   HostAdapter->ErrorRecoveryStrategy[0]]);
  else
    for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
      printk("%s", BusLogic_ErrorRecoveryStrategyLetters[
		     HostAdapter->ErrorRecoveryStrategy[TargetID]]);
  printk("\n");
  /*
    Indicate reading the Host Adapter Configuration completed successfully.
  */
  return true;
}


/*
  BusLogic_AcquireResources acquires the system resources necessary to use
  Host Adapter.
*/

static boolean BusLogic_AcquireResources(BusLogic_HostAdapter_T *HostAdapter)
{
  if (HostAdapter->IRQ_Channel == 0)
    {
      printk("scsi%d: NO INTERRUPT CHANNEL ASSIGNED - DETACHING\n",
	     HostAdapter->HostNumber);
      return false;
    }
  /*
    Acquire exclusive or shared access to the IRQ Channel.  A usage count is
    maintained so that PCI, EISA, or MCA shared interrupts can be supported.
  */
  if (BusLogic_IRQ_UsageCount[HostAdapter->IRQ_Channel]++ == 0)
    {
      if (request_irq(HostAdapter->IRQ_Channel, BusLogic_InterruptHandler,
		      SA_INTERRUPT | SA_SHIRQ,
		      HostAdapter->InterruptLabel, NULL) < 0)
	{
	  BusLogic_IRQ_UsageCount[HostAdapter->IRQ_Channel]--;
	  printk("scsi%d: UNABLE TO ACQUIRE IRQ CHANNEL %d - DETACHING\n",
		 HostAdapter->HostNumber, HostAdapter->IRQ_Channel);
	  return false;
	}
    }
  else
    {
      BusLogic_HostAdapter_T *FirstHostAdapter =
	BusLogic_RegisteredHostAdapters;
      while (FirstHostAdapter != NULL)
	{
	  if (FirstHostAdapter->IRQ_Channel == HostAdapter->IRQ_Channel)
	    {
	      if (strlen(FirstHostAdapter->InterruptLabel) + 11
		  < sizeof(FirstHostAdapter->InterruptLabel))
		{
		  strcat(FirstHostAdapter->InterruptLabel, " + ");
		  strcat(FirstHostAdapter->InterruptLabel,
			 HostAdapter->ModelName);
		}
	      break;
	    }
	  FirstHostAdapter = FirstHostAdapter->Next;
	}
    }
  HostAdapter->IRQ_ChannelAcquired = true;
  /*
    Acquire exclusive access to the DMA Channel.
  */
  if (HostAdapter->DMA_Channel > 0)
    {
      if (request_dma(HostAdapter->DMA_Channel, HostAdapter->BoardName) < 0)
	{
	  printk("scsi%d: UNABLE TO ACQUIRE DMA CHANNEL %d - DETACHING\n",
		 HostAdapter->HostNumber, HostAdapter->DMA_Channel);
	  return false;
	}
      set_dma_mode(HostAdapter->DMA_Channel, DMA_MODE_CASCADE);
      enable_dma(HostAdapter->DMA_Channel);
      HostAdapter->DMA_ChannelAcquired = true;
    }
  /*
    Indicate the System Resource Acquisition completed successfully,
  */
  return true;
}


/*
  BusLogic_ReleaseResources releases any system resources previously acquired
  by BusLogic_AcquireResources.
*/

static void BusLogic_ReleaseResources(BusLogic_HostAdapter_T *HostAdapter)
{
  /*
    Release exclusive or shared access to the IRQ Channel.
  */
  if (HostAdapter->IRQ_ChannelAcquired)
    if (--BusLogic_IRQ_UsageCount[HostAdapter->IRQ_Channel] == 0)
      free_irq(HostAdapter->IRQ_Channel, NULL);
  /*
    Release exclusive access to the DMA Channel.
  */
  if (HostAdapter->DMA_ChannelAcquired)
    free_dma(HostAdapter->DMA_Channel);
}


/*
  BusLogic_TestInterrupts tests for proper functioning of the Host Adapter
  Interrupt Register and that interrupts generated by the Host Adapter are
  getting through to the Interrupt Handler.  A large proportion of initial
  problems with installing PCI Host Adapters are due to configuration problems
  where either the Host Adapter or Motherboard is configured incorrectly, and
  interrupts do not get through as a result.
*/

static boolean BusLogic_TestInterrupts(BusLogic_HostAdapter_T *HostAdapter)
{
  unsigned int InitialInterruptCount, FinalInterruptCount;
  int TestCount = 5, i;
  InitialInterruptCount = kstat.interrupts[HostAdapter->IRQ_Channel];
  /*
    Issue the Test Command Complete Interrupt commands.
  */
  for (i = 0; i < TestCount; i++)
    BusLogic_Command(HostAdapter, BusLogic_TestCommandCompleteInterrupt,
		     NULL, 0, NULL, 0);
  /*
    Verify that BusLogic_InterruptHandler was called at least TestCount times.
    Shared IRQ Channels could cause more than TestCount interrupts to occur,
    but there should never be fewer than TestCount.
  */
  FinalInterruptCount = kstat.interrupts[HostAdapter->IRQ_Channel];
  if (FinalInterruptCount < InitialInterruptCount + TestCount)
    {
      BusLogic_Failure(HostAdapter, "HOST ADAPTER INTERRUPT TEST");
      printk("\n\
Interrupts are not getting through from the Host Adapter to the BusLogic\n\
Driver Interrupt Handler. The most likely cause is that either the Host\n\
Adapter or Motherboard is configured incorrectly.  Please check the Host\n\
Adapter configuration with AutoSCSI or by examining any dip switch and\n\
jumper settings on the Host Adapter, and verify that no other device is\n\
attempting to use the same IRQ Channel.  For PCI Host Adapters, it may also\n\
be necessary to investigate and manually set the PCI interrupt assignments\n\
and edge/level interrupt type selection in the BIOS Setup Program or with\n\
Motherboard jumpers.\n\n");
      return false;
    }
  /*
    Indicate the Host Adapter Interrupt Test completed successfully.
  */
  return true;
}


/*
  BusLogic_InitializeHostAdapter initializes Host Adapter.  This is the only
  function called during SCSI Host Adapter detection which modifies the state
  of the Host Adapter from its initial power on or hard reset state.
*/

static boolean BusLogic_InitializeHostAdapter(BusLogic_HostAdapter_T
					      *HostAdapter)
{
  BusLogic_ExtendedMailboxRequest_T ExtendedMailboxRequest;
  BusLogic_RoundRobinModeRequest_T RoundRobinModeRequest;
  BusLogic_WideModeCCBRequest_T WideModeCCBRequest;
  BusLogic_ModifyIOAddressRequest_T ModifyIOAddressRequest;
  int TargetID;
  /*
    Initialize the Bus Device Reset Pending CCB, Tagged Queuing Active,
    Command Successful Flag, Active Command Count, and Total Command Count
    for each Target Device.
  */
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    HostAdapter->BusDeviceResetPendingCCB[TargetID] = NULL;
  memset(HostAdapter->TaggedQueuingActive, false,
	 sizeof(HostAdapter->TaggedQueuingActive));
  memset(HostAdapter->CommandSuccessfulFlag, false,
	 sizeof(HostAdapter->CommandSuccessfulFlag));
  memset(HostAdapter->ActiveCommandCount, 0,
	 sizeof(HostAdapter->ActiveCommandCount));
  memset(HostAdapter->TotalCommandCount, 0,
	 sizeof(HostAdapter->TotalCommandCount));
  /*
    Initialize the Outgoing and Incoming Mailbox structures.
  */
  memset(HostAdapter->FirstOutgoingMailbox, 0,
	 HostAdapter->MailboxCount * sizeof(BusLogic_OutgoingMailbox_T));
  memset(HostAdapter->FirstIncomingMailbox, 0,
	 HostAdapter->MailboxCount * sizeof(BusLogic_IncomingMailbox_T));
  /*
    Initialize the pointers to the Next Mailboxes.
  */
  HostAdapter->NextOutgoingMailbox = HostAdapter->FirstOutgoingMailbox;
  HostAdapter->NextIncomingMailbox = HostAdapter->FirstIncomingMailbox;
  /*
    Initialize the Host Adapter's Pointer to the Outgoing/Incoming Mailboxes.
  */
  ExtendedMailboxRequest.MailboxCount = HostAdapter->MailboxCount;
  ExtendedMailboxRequest.BaseMailboxAddress =
    Virtual_to_Bus(HostAdapter->FirstOutgoingMailbox);
  if (BusLogic_Command(HostAdapter, BusLogic_InitializeExtendedMailbox,
		       &ExtendedMailboxRequest,
		       sizeof(ExtendedMailboxRequest), NULL, 0) < 0)
    return BusLogic_Failure(HostAdapter, "MAILBOX INITIALIZATION");
  /*
    Enable Strict Round Robin Mode if supported by the Host Adapter.  In
    Strict Round Robin Mode, the Host Adapter only looks at the next Outgoing
    Mailbox for each new command, rather than scanning through all the
    Outgoing Mailboxes to find any that have new commands in them.  Strict
    Round Robin Mode is significantly more efficient.
  */
  if (HostAdapter->StrictRoundRobinModeSupported)
    {
      RoundRobinModeRequest = BusLogic_StrictRoundRobinMode;
      if (BusLogic_Command(HostAdapter, BusLogic_EnableStrictRoundRobinMode,
			   &RoundRobinModeRequest,
			   sizeof(RoundRobinModeRequest), NULL, 0) < 0)
	return BusLogic_Failure(HostAdapter, "ENABLE STRICT ROUND ROBIN MODE");
    }
  /*
    For Wide SCSI Host Adapters, issue the Enable Wide Mode CCB command to
    allow more than 8 Logical Units per Target Device to be supported.
  */
  if (HostAdapter->HostWideSCSI)
    {
      WideModeCCBRequest = BusLogic_WideModeCCB;
      if (BusLogic_Command(HostAdapter, BusLogic_EnableWideModeCCB,
			   &WideModeCCBRequest,
			   sizeof(WideModeCCBRequest), NULL, 0) < 0)
	return BusLogic_Failure(HostAdapter, "ENABLE WIDE MODE CCB");
    }
  /*
    For PCI Host Adapters being accessed through the PCI compliant I/O
    Address, disable the ISA compatible I/O Address to avoid detecting the
    same Host Adapter at both I/O Addresses.
  */
  if (HostAdapter->BusType == BusLogic_PCI_Bus)
    {
      int Index;
      for (Index = 0; BusLogic_IO_StandardAddresses[Index] > 0; Index++)
	if (HostAdapter->IO_Address == BusLogic_IO_StandardAddresses[Index])
	  break;
      if (BusLogic_IO_StandardAddresses[Index] == 0)
	{
	  ModifyIOAddressRequest = BusLogic_ModifyIO_Disable;
	  if (BusLogic_Command(HostAdapter, BusLogic_ModifyIOAddress,
			       &ModifyIOAddressRequest,
			       sizeof(ModifyIOAddressRequest), NULL, 0) < 0)
	    return BusLogic_Failure(HostAdapter, "MODIFY I/O ADDRESS");
	}
    }
  /*
    Announce Successful Initialization.
  */
  printk("scsi%d: *** %s Initialized Successfully ***\n",
	 HostAdapter->HostNumber, HostAdapter->BoardName);
  /*
    Indicate the Host Adapter Initialization completed successfully.
  */
  return true;
}


/*
  BusLogic_InquireTargetDevices inquires about the Target Devices accessible
  through Host Adapter and reports on the results.
*/

static boolean BusLogic_InquireTargetDevices(BusLogic_HostAdapter_T
					     *HostAdapter)
{
  BusLogic_InstalledDevices_T InstalledDevices;
  BusLogic_InstalledDevices8_T InstalledDevicesID0to7;
  BusLogic_SetupInformation_T SetupInformation;
  BusLogic_SynchronousPeriod_T SynchronousPeriod;
  BusLogic_RequestedReplyLength_T RequestedReplyLength;
  int TargetDevicesFound = 0, TargetID;
  /*
    Wait a few seconds between the Host Adapter Hard Reset which initiates
    a SCSI Bus Reset and issuing any SCSI Commands.  Some SCSI devices get
    confused if they receive SCSI Commands too soon after a SCSI Bus Reset.
  */
  BusLogic_Delay(HostAdapter->BusSettleTime);
  /*
    Inhibit the Target Devices Inquiry if requested.
  */
  if (HostAdapter->LocalOptions & BusLogic_InhibitTargetInquiry)
    {
      printk("scsi%d:   Target Device Inquiry Inhibited\n",
	     HostAdapter->HostNumber);
      return true;
    }
  /*
    Issue the Inquire Devices command for boards with firmware version 4.25 or
    later, or the Inquire Installed Devices ID 0 to 7 command for older boards.
    This is necessary to force Synchronous Transfer Negotiation so that the
    Inquire Setup Information and Inquire Synchronous Period commands will
    return valid data.  The Inquire Devices command is preferable to Inquire
    Installed Devices ID 0 to 7 since it only probes Logical Unit 0 of each
    Target Device.
  */
  if (strcmp(HostAdapter->FirmwareVersion, "4.25") >= 0)
    {
      if (BusLogic_Command(HostAdapter, BusLogic_InquireDevices, NULL, 0,
			   &InstalledDevices, sizeof(InstalledDevices))
	  != sizeof(InstalledDevices))
	return BusLogic_Failure(HostAdapter, "INQUIRE DEVICES");
    }
  else
    {
      if (BusLogic_Command(HostAdapter, BusLogic_InquireInstalledDevicesID0to7,
			   NULL, 0, &InstalledDevicesID0to7,
			   sizeof(InstalledDevicesID0to7))
	  != sizeof(InstalledDevicesID0to7))
	return BusLogic_Failure(HostAdapter,
				"INQUIRE INSTALLED DEVICES ID 0 TO 7");
      InstalledDevices = 0;
      for (TargetID = 0; TargetID < 8; TargetID++)
	if (InstalledDevicesID0to7[TargetID] != 0)
	  InstalledDevices |= (1 << TargetID);
    }
  /*
    Issue the Inquire Setup Information command.
  */
  RequestedReplyLength = sizeof(SetupInformation);
  if (BusLogic_Command(HostAdapter, BusLogic_InquireSetupInformation,
		       &RequestedReplyLength, sizeof(RequestedReplyLength),
		       &SetupInformation, sizeof(SetupInformation))
      != sizeof(SetupInformation))
    return BusLogic_Failure(HostAdapter, "INQUIRE SETUP INFORMATION");
  /*
    Issue the Inquire Synchronous Period command.
  */
  if (HostAdapter->FirmwareVersion[0] >= '3')
    {
      RequestedReplyLength = sizeof(SynchronousPeriod);
      if (BusLogic_Command(HostAdapter, BusLogic_InquireSynchronousPeriod,
			   &RequestedReplyLength, sizeof(RequestedReplyLength),
			   &SynchronousPeriod, sizeof(SynchronousPeriod))
	  != sizeof(SynchronousPeriod))
	return BusLogic_Failure(HostAdapter, "INQUIRE SYNCHRONOUS PERIOD");
    }
  else
    for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
      if (SetupInformation.SynchronousValuesID0to7[TargetID].Offset > 0)
	SynchronousPeriod[TargetID] =
	  20 + 5 * SetupInformation.SynchronousValuesID0to7[TargetID]
				   .TransferPeriod;
      else SynchronousPeriod[TargetID] = 0;
  /*
    Save the Installed Devices, Synchronous Values, and Synchronous Period
    information in the Host Adapter structure.
  */
  HostAdapter->InstalledDevices = InstalledDevices;
  memcpy(HostAdapter->SynchronousValues,
	 SetupInformation.SynchronousValuesID0to7,
	 sizeof(BusLogic_SynchronousValues8_T));
  if (HostAdapter->HostWideSCSI)
    memcpy(&HostAdapter->SynchronousValues[8],
	   SetupInformation.SynchronousValuesID8to15,
	   sizeof(BusLogic_SynchronousValues8_T));
  memcpy(HostAdapter->SynchronousPeriod, SynchronousPeriod,
	 sizeof(BusLogic_SynchronousPeriod_T));
  /*
    Report on the Target Devices found.
  */
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    if (HostAdapter->InstalledDevices & (1 << TargetID))
      {
	int SynchronousPeriod = HostAdapter->SynchronousPeriod[TargetID];
	if (SynchronousPeriod > 10)
	  {
	    int SynchronousTransferRate = 100000000 / SynchronousPeriod;
	    int RoundedSynchronousTransferRate =
	      (SynchronousTransferRate + 5000) / 10000;
	    printk("scsi%d:   Target %d: Synchronous at "
		   "%d.%02d mega-transfers/second, offset %d\n",
		   HostAdapter->HostNumber, TargetID,
		   RoundedSynchronousTransferRate / 100,
		   RoundedSynchronousTransferRate % 100,
		   HostAdapter->SynchronousValues[TargetID].Offset);
	  }
	else if (SynchronousPeriod > 0)
	  {
	    int SynchronousTransferRate = 100000000 / SynchronousPeriod;
	    int RoundedSynchronousTransferRate =
	      (SynchronousTransferRate + 50000) / 100000;
	    printk("scsi%d:   Target %d: Synchronous at "
		   "%d.%01d mega-transfers/second, offset %d\n",
		   HostAdapter->HostNumber, TargetID,
		   RoundedSynchronousTransferRate / 10,
		   RoundedSynchronousTransferRate % 10,
		   HostAdapter->SynchronousValues[TargetID].Offset);
	  }
	else printk("scsi%d:   Target %d: Asynchronous\n",
		    HostAdapter->HostNumber, TargetID);
	TargetDevicesFound++;
      }
  if (TargetDevicesFound == 0)
    printk("scsi%d:   No Target Devices Found\n", HostAdapter->HostNumber);
  /*
    Indicate the Target Device Inquiry completed successfully.
  */
  return true;
}


/*
  BusLogic_InitializeHostStructure initializes the fields in the SCSI Host
  structure.  The base, io_port, n_io_ports, irq, and dma_channel fields in the
  SCSI Host structure are intentionally left uninitialized, as this driver
  handles acquisition and release of these resources explicitly, as well as
  ensuring exclusive access to the Host Adapter hardware and data structures
  through explicit acquisition and release of the Host Adapter's Lock.
*/

static void BusLogic_InitializeHostStructure(BusLogic_HostAdapter_T
					       *HostAdapter,
					     SCSI_Host_T *Host)
{
  Host->max_id = HostAdapter->MaxTargetDevices;
  Host->max_lun = HostAdapter->MaxLogicalUnits;
  Host->max_channel = 0;
  Host->unique_id = HostAdapter->IO_Address;
  Host->this_id = HostAdapter->SCSI_ID;
  Host->can_queue = HostAdapter->MailboxCount;
  Host->sg_tablesize = HostAdapter->DriverScatterGatherLimit;
  Host->unchecked_isa_dma = HostAdapter->BounceBuffersRequired;
  Host->cmd_per_lun = HostAdapter->UntaggedQueueDepth;
}


/*
  BusLogic_SelectQueueDepths selects Queue Depths for each Target Device
  based on the Host Adapter's Total Queue Depth and the number, type, speed,
  and capabilities of the Target Devices.
*/

static void BusLogic_SelectQueueDepths(SCSI_Host_T *Host,
				       SCSI_Device_T *DeviceList)
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Host->hostdata;
  int TaggedQueueDepth = HostAdapter->TaggedQueueDepth;
  int UntaggedQueueDepth = HostAdapter->UntaggedQueueDepth;
  int TaggedDeviceCount = 0, UntaggedDeviceCount = 0;
  SCSI_Device_T *Device;
  for (Device = DeviceList; Device != NULL; Device = Device->next)
    if (Device->host == Host)
      {
	if (Device->tagged_supported &&
	    (HostAdapter->TaggedQueuingPermitted & (1 << Device->id)))
	  TaggedDeviceCount++;
	else UntaggedDeviceCount++;
      }
  if (TaggedQueueDepth == 0 && TaggedDeviceCount > 0)
    {
      TaggedQueueDepth =
	1 + ((HostAdapter->TotalQueueDepth
	      - UntaggedDeviceCount * UntaggedQueueDepth)
	     / TaggedDeviceCount);
      if (TaggedQueueDepth > BusLogic_PreferredTaggedQueueDepth)
	TaggedQueueDepth = BusLogic_PreferredTaggedQueueDepth;
    }
  for (Device = DeviceList; Device != NULL; Device = Device->next)
    if (Device->host == Host)
      {
	if (Device->tagged_supported &&
	    (HostAdapter->TaggedQueuingPermitted & (1 << Device->id)))
	  Device->queue_depth = TaggedQueueDepth;
	else Device->queue_depth = UntaggedQueueDepth;
	if (BusLogic_GlobalOptions & BusLogic_TraceQueueDepths)
	  printk("scsi%d: Setting Queue Depth for Target %d to %d\n",
		 HostAdapter->HostNumber, Device->id, Device->queue_depth);
      }
}


/*
  BusLogic_DetectHostAdapter probes for BusLogic Host Adapters at the standard
  I/O Addresses where they may be located, initializing, registering, and
  reporting the configuration of each BusLogic Host Adapter it finds.  It
  returns the number of BusLogic Host Adapters successfully initialized and
  registered.
*/

int BusLogic_DetectHostAdapter(SCSI_Host_Template_T *HostTemplate)
{
  int BusLogicHostAdapterCount = 0, CommandLineEntryIndex = 0;
  int AddressProbeIndex = 0;
  if (BusLogic_ProbeOptions & BusLogic_NoProbe) return 0;
  BusLogic_InitializeAddressProbeList();
  while (BusLogic_IO_AddressProbeList[AddressProbeIndex] > 0)
    {
      BusLogic_HostAdapter_T HostAdapterPrototype;
      BusLogic_HostAdapter_T *HostAdapter = &HostAdapterPrototype;
      SCSI_Host_T *Host;
      memset(HostAdapter, 0, sizeof(BusLogic_HostAdapter_T));
      HostAdapter->IO_Address =
	BusLogic_IO_AddressProbeList[AddressProbeIndex++];
      /*
	Initialize the Command Line Entry field if an explicit I/O Address
	was specified.
      */
      if (CommandLineEntryIndex < BusLogic_CommandLineEntryCount &&
	  BusLogic_CommandLineEntries[CommandLineEntryIndex].IO_Address ==
	  HostAdapter->IO_Address)
	HostAdapter->CommandLineEntry =
	  &BusLogic_CommandLineEntries[CommandLineEntryIndex++];
      /*
	Check whether the I/O Address range is already in use.
      */
      if (check_region(HostAdapter->IO_Address, BusLogic_IO_PortCount) < 0)
	continue;
      /*
	Probe the Host Adapter.  If unsuccessful, abort further initialization.
      */
      if (!BusLogic_ProbeHostAdapter(HostAdapter)) continue;
      /*
	Hard Reset the Host Adapter.  If unsuccessful, abort further
	initialization.
      */
      if (!BusLogic_HardResetHostAdapter(HostAdapter)) continue;
      /*
	Check the Host Adapter.  If unsuccessful, abort further initialization.
      */
      if (!BusLogic_CheckHostAdapter(HostAdapter)) continue;
      /*
	Initialize the Command Line Entry field if an explicit I/O Address
	was not specified.
      */
      if (CommandLineEntryIndex < BusLogic_CommandLineEntryCount &&
	  BusLogic_CommandLineEntries[CommandLineEntryIndex].IO_Address == 0)
	HostAdapter->CommandLineEntry =
	  &BusLogic_CommandLineEntries[CommandLineEntryIndex++];
      /*
	Announce the Driver Version and Date, Author's Name, Copyright Notice,
	and Contact Address.
      */
      BusLogic_AnnounceDriver();
      /*
	Register usage of the I/O Address range.  From this point onward, any
	failure will be assumed to be due to a problem with the Host Adapter,
	rather than due to having mistakenly identified this port as belonging
	to a BusLogic Host Adapter.  The I/O Address range will not be
	released, thereby preventing it from being incorrectly identified as
	any other type of Host Adapter.
      */
      request_region(HostAdapter->IO_Address, BusLogic_IO_PortCount,
		     "BusLogic");
      /*
	Register the SCSI Host structure.
      */
      HostTemplate->proc_dir = &BusLogic_ProcDirectoryEntry;
      Host = scsi_register(HostTemplate, sizeof(BusLogic_HostAdapter_T));
      HostAdapter = (BusLogic_HostAdapter_T *) Host->hostdata;
      memcpy(HostAdapter, &HostAdapterPrototype,
	     sizeof(BusLogic_HostAdapter_T));
      HostAdapter->SCSI_Host = Host;
      HostAdapter->HostNumber = Host->host_no;
      Host->select_queue_depths = BusLogic_SelectQueueDepths;
      /*
	Add Host Adapter to the end of the list of registered BusLogic
	Host Adapters.  In order for Command Complete Interrupts to be
	properly dismissed by BusLogic_InterruptHandler, the Host Adapter
	must be registered.  This must be done before the IRQ Channel is
	acquired, and in a shared IRQ Channel environment, must be done
	before any Command Complete Interrupts occur, since the IRQ Channel
	may have already been acquired by a previous BusLogic Host Adapter.
      */
      BusLogic_RegisterHostAdapter(HostAdapter);
      /*
	Read the Host Adapter Configuration, Acquire the System Resources
	necessary to use Host Adapter and initialize the fields in the SCSI
	Host structure, then Test Interrupts, Create the Mailboxes and CCBs,
	Initialize the Host Adapter, and finally Inquire about the Target
	Devices.
      */
      if (BusLogic_ReadHostAdapterConfiguration(HostAdapter) &&
	  BusLogic_AcquireResources(HostAdapter) &&
	  BusLogic_TestInterrupts(HostAdapter) &&
	  BusLogic_CreateMailboxes(HostAdapter) &&
	  BusLogic_CreateCCBs(HostAdapter) &&
	  BusLogic_InitializeHostAdapter(HostAdapter) &&
	  BusLogic_InquireTargetDevices(HostAdapter))
	{
	  /*
	    Initialization has been completed successfully.  Release and
	    re-register usage of the I/O Address range so that the Model
	    Name of the Host Adapter will appear, and initialize the SCSI
	    Host structure.
	  */
	  release_region(HostAdapter->IO_Address, BusLogic_IO_PortCount);
	  request_region(HostAdapter->IO_Address, BusLogic_IO_PortCount,
			 HostAdapter->BoardName);
	  BusLogic_InitializeHostStructure(HostAdapter, Host);
	  BusLogicHostAdapterCount++;
	}
      else
	{
	  /*
	    An error occurred during Host Adapter Configuration Querying,
	    Resource Acquisition, Interrupt Testing, CCB Creation, Host Adapter
	    Initialization, or Target Device Inquiry, so remove Host Adapter
	    from the list of registered BusLogic Host Adapters, destroy the
	    CCBs and Mailboxes, Release the System Resources, and Unregister
	    the SCSI Host.
	  */
	  BusLogic_DestroyCCBs(HostAdapter);
	  BusLogic_DestroyMailboxes(HostAdapter);
	  BusLogic_ReleaseResources(HostAdapter);
	  BusLogic_UnregisterHostAdapter(HostAdapter);
	  scsi_unregister(Host);
	}
    }
  return BusLogicHostAdapterCount;
}


/*
  BusLogic_ReleaseHostAdapter releases all resources previously acquired to
  support a specific Host Adapter, including the I/O Address range, and
  unregisters the BusLogic Host Adapter.
*/

int BusLogic_ReleaseHostAdapter(SCSI_Host_T *Host)
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Host->hostdata;
  /*
    Destroy the CCBs and Mailboxes, and release any system resources acquired
    to support Host Adapter.
  */
  BusLogic_DestroyCCBs(HostAdapter);
  BusLogic_DestroyMailboxes(HostAdapter);
  BusLogic_ReleaseResources(HostAdapter);
  /*
    Release usage of the I/O Address range.
  */
  release_region(HostAdapter->IO_Address, BusLogic_IO_PortCount);
  /*
    Remove Host Adapter from the list of registered BusLogic Host Adapters.
  */
  BusLogic_UnregisterHostAdapter(HostAdapter);
  return 0;
}


/*
  BusLogic_ComputeResultCode computes a SCSI Subsystem Result Code from
  the Host Adapter Status and Target Device Status.
*/

static int BusLogic_ComputeResultCode(BusLogic_HostAdapterStatus_T
					HostAdapterStatus,
				      BusLogic_TargetDeviceStatus_T
					TargetDeviceStatus)
{
  int HostStatus;
  switch (HostAdapterStatus)
    {
    case BusLogic_CommandCompletedNormally:
    case BusLogic_LinkedCommandCompleted:
    case BusLogic_LinkedCommandCompletedWithFlag:
      HostStatus = DID_OK;
      break;
    case BusLogic_SCSISelectionTimeout:
      HostStatus = DID_TIME_OUT;
      break;
    case BusLogic_InvalidOutgoingMailboxActionCode:
    case BusLogic_InvalidCommandOperationCode:
    case BusLogic_InvalidCommandParameter:
      printk("BusLogic: BusLogic Driver Protocol Error 0x%02X\n",
	     HostAdapterStatus);
    case BusLogic_DataOverUnderRun:
    case BusLogic_UnexpectedBusFree:
    case BusLogic_LinkedCCBhasInvalidLUN:
    case BusLogic_AutoRequestSenseFailed:
    case BusLogic_TaggedQueuingMessageRejected:
    case BusLogic_UnsupportedMessageReceived:
    case BusLogic_HostAdapterHardwareFailed:
    case BusLogic_TargetDeviceReconnectedImproperly:
    case BusLogic_AbortQueueGenerated:
    case BusLogic_HostAdapterSoftwareError:
    case BusLogic_HostAdapterHardwareTimeoutError:
    case BusLogic_SCSIParityErrorDetected:
      HostStatus = DID_ERROR;
      break;
    case BusLogic_InvalidBusPhaseRequested:
    case BusLogic_TargetFailedResponseToATN:
    case BusLogic_HostAdapterAssertedRST:
    case BusLogic_OtherDeviceAssertedRST:
    case BusLogic_HostAdapterAssertedBusDeviceReset:
      HostStatus = DID_RESET;
      break;
    default:
      printk("BusLogic: unknown Host Adapter Status 0x%02X\n",
	     HostAdapterStatus);
      HostStatus = DID_ERROR;
      break;
    }
  return (HostStatus << 16) | TargetDeviceStatus;
}


/*
  BusLogic_InterruptHandler handles hardware interrupts from BusLogic Host
  Adapters.  To simplify handling shared IRQ Channels, all installed BusLogic
  Host Adapters are scanned whenever any one of them signals a hardware
  interrupt.
*/

static void BusLogic_InterruptHandler(int IRQ_Channel,
				      void *DeviceIdentifier,
				      Registers_T *InterruptRegisters)
{
  BusLogic_CCB_T *FirstCompletedCCB = NULL, *LastCompletedCCB = NULL;
  BusLogic_HostAdapter_T *HostAdapter;
  int HostAdapterResetRequestedCount = 0;
  BusLogic_Lock_T Lock;
  /*
    Iterate over the installed BusLogic Host Adapters accepting any Incoming
    Mailbox entries and saving the completed CCBs for processing.  This
    interrupt handler is installed as a fast interrupt, so interrupts are
    disabled when the interrupt handler is entered.
  */
  for (HostAdapter = BusLogic_RegisteredHostAdapters;
       HostAdapter != NULL;
       HostAdapter = HostAdapter->Next)
    {
      unsigned char InterruptRegister;
      /*
	Acquire exclusive access to Host Adapter.
      */
      BusLogic_AcquireHostAdapterLockID(HostAdapter, &Lock);
      /*
	Read the Host Adapter Interrupt Register.
      */
      InterruptRegister = BusLogic_ReadInterruptRegister(HostAdapter);
      if (InterruptRegister & BusLogic_InterruptValid)
	{
	  /*
	    Acknowledge the interrupt and reset the Host Adapter
	    Interrupt Register.
	  */
	  BusLogic_WriteControlRegister(HostAdapter, BusLogic_InterruptReset);
	  /*
	    Process valid SCSI Reset State and Incoming Mailbox Loaded
	    Interrupts.  Command Complete Interrupts are noted, and
	    Outgoing Mailbox Available Interrupts are ignored, as they
	    are never enabled.
	  */
	  if (InterruptRegister & BusLogic_SCSIResetState)
	    {
	      HostAdapter->HostAdapterResetRequested = true;
	      HostAdapterResetRequestedCount++;
	    }
	  else if (InterruptRegister & BusLogic_IncomingMailboxLoaded)
	    {
	      /*
		Scan through the Incoming Mailboxes in Strict Round Robin
		fashion, saving any completed CCBs for further processing.
		It is essential that for each CCB and SCSI Command issued,
		command completion processing is performed exactly once.
		Therefore, only Incoming Mailboxes with completion code
		Command Completed Without Error, Command Completed With
		Error, or Command Aborted At Host Request are saved for
		completion processing.  When an Incoming Mailbox has a
		completion code of Aborted Command Not Found, the CCB had
		already completed or been aborted before the current Abort
		request was processed, and so completion processing has
		already occurred and no further action should be taken.
	      */
	      BusLogic_IncomingMailbox_T *NextIncomingMailbox =
		HostAdapter->NextIncomingMailbox;
	      BusLogic_CompletionCode_T MailboxCompletionCode;
	      while ((MailboxCompletionCode =
		      NextIncomingMailbox->CompletionCode) !=
		     BusLogic_IncomingMailboxFree)
		{
		  BusLogic_CCB_T *CCB =
		    (BusLogic_CCB_T *) Bus_to_Virtual(NextIncomingMailbox->CCB);
		  if (MailboxCompletionCode != BusLogic_AbortedCommandNotFound)
		    if (CCB->Status == BusLogic_CCB_Active ||
			CCB->Status == BusLogic_CCB_Reset)
		      {
			/*
			  Mark this CCB as completed and add it to the end
			  of the list of completed CCBs.
			*/
			CCB->Status = BusLogic_CCB_Completed;
			CCB->MailboxCompletionCode = MailboxCompletionCode;
			CCB->Next = NULL;
			if (FirstCompletedCCB == NULL)
			  {
			    FirstCompletedCCB = CCB;
			    LastCompletedCCB = CCB;
			  }
			else
			  {
			    LastCompletedCCB->Next = CCB;
			    LastCompletedCCB = CCB;
			  }
			HostAdapter->ActiveCommandCount[CCB->TargetID]--;
		      }
		    else
		      {
			/*
			  If a CCB ever appears in an Incoming Mailbox and
			  is not marked as status Active or Reset, then there
			  is most likely a bug in the Host Adapter firmware.
			*/
			printk("scsi%d: Illegal CCB #%ld status %d in "
			       "Incoming Mailbox\n", HostAdapter->HostNumber,
			       CCB->SerialNumber, CCB->Status);
		      }
		  else printk("scsi%d: Aborted CCB #%ld to Target %d "
			      "Not Found\n", HostAdapter->HostNumber,
			      CCB->SerialNumber, CCB->TargetID);
		  NextIncomingMailbox->CompletionCode =
		    BusLogic_IncomingMailboxFree;
		  if (++NextIncomingMailbox > HostAdapter->LastIncomingMailbox)
		    NextIncomingMailbox = HostAdapter->FirstIncomingMailbox;
		}
	      HostAdapter->NextIncomingMailbox = NextIncomingMailbox;
	    }
	  else if (InterruptRegister & BusLogic_CommandComplete)
	    HostAdapter->HostAdapterCommandCompleted = true;
	}
      /*
	Release exclusive access to Host Adapter.
      */
      BusLogic_ReleaseHostAdapterLockID(HostAdapter, &Lock);
    }
  /*
    Iterate over the completed CCBs setting the SCSI Command Result Codes,
    deallocating the CCBs, and calling the Completion Routines.
  */
  while (FirstCompletedCCB != NULL)
    {
      BusLogic_CCB_T *CCB = FirstCompletedCCB;
      SCSI_Command_T *Command = CCB->Command;
      FirstCompletedCCB = FirstCompletedCCB->Next;
      HostAdapter = CCB->HostAdapter;
      /*
	Acquire exclusive access to Host Adapter.
      */
      BusLogic_AcquireHostAdapterLockID(HostAdapter, &Lock);
      /*
	Process the Completed CCB.
      */
      if (CCB->Opcode == BusLogic_BusDeviceReset)
	{
	  int TargetID = CCB->TargetID;
	  printk("scsi%d: Bus Device Reset CCB #%ld to Target %d Completed\n",
		 HostAdapter->HostNumber, CCB->SerialNumber, TargetID);
	  HostAdapter->TotalCommandCount[TargetID] = 0;
	  HostAdapter->TaggedQueuingActive[TargetID] = false;
	  /*
	    Place CCB back on the Host Adapter's free list.
	  */
	  BusLogic_DeallocateCCB(CCB);
	  /*
	    Bus Device Reset CCBs have the Command field non-NULL only when a
	    Bus Device Reset was requested for a Command that did not have a
	    currently active CCB in the Host Adapter (i.e., a Synchronous
	    Bus Device Reset), and hence would not have its Completion Routine
	    called otherwise.
	  */
	  while (Command != NULL)
	    {
	      SCSI_Command_T *NextCommand = Command->reset_chain;
	      Command->reset_chain = NULL;
	      Command->result = DID_RESET << 16;
	      Command->scsi_done(Command);
	      Command = NextCommand;
	    }
	  /*
	    Iterate over the CCBs for this Host Adapter performing completion
	    processing for any CCBs marked as Reset for this Target.
	  */
	  for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
	    if (CCB->Status == BusLogic_CCB_Reset && CCB->TargetID == TargetID)
	      {
		Command = CCB->Command;
		BusLogic_DeallocateCCB(CCB);
		HostAdapter->ActiveCommandCount[TargetID]--;
		Command->result = DID_RESET << 16;
		Command->scsi_done(Command);
	      }
	  HostAdapter->BusDeviceResetPendingCCB[TargetID] = NULL;
	}
      else
	{
	  /*
	    Translate the Mailbox Completion Code, Host Adapter Status, and
	    Target Device Status into a SCSI Subsystem Result Code.
	  */
	  switch (CCB->MailboxCompletionCode)
	    {
	    case BusLogic_IncomingMailboxFree:
	    case BusLogic_AbortedCommandNotFound:
	      printk("scsi%d: CCB #%ld to Target %d Impossible State\n",
		     HostAdapter->HostNumber, CCB->SerialNumber, CCB->TargetID);
	      break;
	    case BusLogic_CommandCompletedWithoutError:
	      HostAdapter->CommandSuccessfulFlag[CCB->TargetID] = true;
	      Command->result = DID_OK << 16;
	      break;
	    case BusLogic_CommandAbortedAtHostRequest:
	      printk("scsi%d: CCB #%ld to Target %d Aborted\n",
		     HostAdapter->HostNumber, CCB->SerialNumber, CCB->TargetID);
	      Command->result = DID_ABORT << 16;
	      break;
	    case BusLogic_CommandCompletedWithError:
	      Command->result =
		BusLogic_ComputeResultCode(CCB->HostAdapterStatus,
					   CCB->TargetDeviceStatus);
	      if (BusLogic_GlobalOptions & BusLogic_TraceErrors)
		if (CCB->HostAdapterStatus != BusLogic_SCSISelectionTimeout)
		  {
		    int i;
		    printk("scsi%d: CCB #%ld Target %d: Result %X "
			   "Host Adapter Status %02X Target Status %02X\n",
			   HostAdapter->HostNumber, CCB->SerialNumber,
			   CCB->TargetID, Command->result,
			   CCB->HostAdapterStatus, CCB->TargetDeviceStatus);
		    printk("scsi%d: CDB   ", HostAdapter->HostNumber);
		    for (i = 0; i < CCB->CDB_Length; i++)
		      printk(" %02X", CCB->CDB[i]);
		    printk("\n");
		    printk("scsi%d: Sense ", HostAdapter->HostNumber);
		    for (i = 0; i < CCB->SenseDataLength; i++)
		      printk(" %02X", Command->sense_buffer[i]);
		    printk("\n");
		  }
	      break;
	    }
	  /*
	    Place CCB back on the Host Adapter's free list.
	  */
	  BusLogic_DeallocateCCB(CCB);
	  /*
	    Call the SCSI Command Completion Routine.
	  */
	  Command->scsi_done(Command);
	}
      /*
	Release exclusive access to Host Adapter.
      */
      BusLogic_ReleaseHostAdapterLockID(HostAdapter, &Lock);
    }
  /*
    Iterate over the Host Adapters performing any requested Host Adapter Resets.
  */
  if (HostAdapterResetRequestedCount == 0) return;
  for (HostAdapter = BusLogic_RegisteredHostAdapters;
       HostAdapter != NULL;
       HostAdapter = HostAdapter->Next)
    if (HostAdapter->HostAdapterResetRequested)
      {
	BusLogic_ResetHostAdapter(HostAdapter, NULL, 0);
	HostAdapter->HostAdapterResetRequested = false;
	scsi_mark_host_reset(HostAdapter->SCSI_Host);
      }
}


/*
  BusLogic_WriteOutgoingMailbox places CCB and Action Code into an Outgoing
  Mailbox for execution by Host Adapter.  The Host Adapter's Lock should have
  already been acquired by the caller.
*/

static boolean BusLogic_WriteOutgoingMailbox(BusLogic_HostAdapter_T
					       *HostAdapter,
					     BusLogic_ActionCode_T ActionCode,
					     BusLogic_CCB_T *CCB)
{
  BusLogic_OutgoingMailbox_T *NextOutgoingMailbox;
  NextOutgoingMailbox = HostAdapter->NextOutgoingMailbox;
  if (NextOutgoingMailbox->ActionCode == BusLogic_OutgoingMailboxFree)
    {
      CCB->Status = BusLogic_CCB_Active;
      /*
	The CCB field must be written before the Action Code field since
	the Host Adapter is operating asynchronously and the locking code
	does not protect against simultaneous access by the Host Adapter.
      */
      NextOutgoingMailbox->CCB = Virtual_to_Bus(CCB);
      NextOutgoingMailbox->ActionCode = ActionCode;
      BusLogic_StartMailboxCommand(HostAdapter);
      if (++NextOutgoingMailbox > HostAdapter->LastOutgoingMailbox)
	NextOutgoingMailbox = HostAdapter->FirstOutgoingMailbox;
      HostAdapter->NextOutgoingMailbox = NextOutgoingMailbox;
      if (ActionCode == BusLogic_MailboxStartCommand)
	HostAdapter->ActiveCommandCount[CCB->TargetID]++;
      return true;
    }
  return false;
}


/*
  BusLogic_QueueCommand creates a CCB for Command and places it into an
  Outgoing Mailbox for execution by the associated Host Adapter.
*/

int BusLogic_QueueCommand(SCSI_Command_T *Command,
			  void (*CompletionRoutine)(SCSI_Command_T *))
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Command->host->hostdata;
  unsigned char *CDB = Command->cmnd;
  int CDB_Length = Command->cmd_len;
  int TargetID = Command->target;
  int LogicalUnit = Command->lun;
  void *BufferPointer = Command->request_buffer;
  int BufferLength = Command->request_bufflen;
  int SegmentCount = Command->use_sg;
  BusLogic_Lock_T Lock;
  BusLogic_CCB_T *CCB;
  /*
    SCSI REQUEST_SENSE commands will be executed automatically by the Host
    Adapter for any errors, so they should not be executed explicitly unless
    the Sense Data is zero indicating that no error occurred.
  */
  if (CDB[0] == REQUEST_SENSE && Command->sense_buffer[0] != 0)
    {
      Command->result = DID_OK << 16;
      CompletionRoutine(Command);
      return 0;
    }
  /*
    Acquire exclusive access to Host Adapter.
  */
  BusLogic_AcquireHostAdapterLock(HostAdapter, &Lock);
  /*
    Allocate a CCB from the Host Adapter's free list.  In the unlikely event
    that there are none available and memory allocation fails, wait 1 second
    and try again.  If that fails, the Host Adapter is probably hung so we
    signal an error as a Host Adapter Hard Reset should be initiated soon.
  */
  CCB = BusLogic_AllocateCCB(HostAdapter);
  if (CCB == NULL)
    {
      BusLogic_Delay(1);
      CCB = BusLogic_AllocateCCB(HostAdapter);
      if (CCB == NULL)
	{
	  Command->result = DID_ERROR << 16;
	  CompletionRoutine(Command);
	  goto Done;
	}
    }
  /*
    Initialize the fields in the BusLogic Command Control Block (CCB).
  */
  if (SegmentCount == 0)
    {
      CCB->Opcode = BusLogic_InitiatorCCB;
      CCB->DataLength = BufferLength;
      CCB->DataPointer = Virtual_to_Bus(BufferPointer);
    }
  else
    {
      SCSI_ScatterList_T *ScatterList = (SCSI_ScatterList_T *) BufferPointer;
      int Segment;
      CCB->Opcode = BusLogic_InitiatorCCB_ScatterGather;
      CCB->DataLength = SegmentCount * sizeof(BusLogic_ScatterGatherSegment_T);
      CCB->DataPointer = Virtual_to_Bus(CCB->ScatterGatherList);
      for (Segment = 0; Segment < SegmentCount; Segment++)
	{
	  CCB->ScatterGatherList[Segment].SegmentByteCount =
	    ScatterList[Segment].length;
	  CCB->ScatterGatherList[Segment].SegmentDataPointer =
	    Virtual_to_Bus(ScatterList[Segment].address);
	}
    }
  switch (CDB[0])
    {
    case READ_6:
    case READ_10:
      CCB->DataDirection = BusLogic_DataInLengthChecked;
      break;
    case WRITE_6:
    case WRITE_10:
      CCB->DataDirection = BusLogic_DataOutLengthChecked;
      break;
    default:
      CCB->DataDirection = BusLogic_UncheckedDataTransfer;
      break;
    }
  CCB->CDB_Length = CDB_Length;
  CCB->SenseDataLength = sizeof(Command->sense_buffer);
  CCB->HostAdapterStatus = 0;
  CCB->TargetDeviceStatus = 0;
  CCB->TargetID = TargetID;
  CCB->LogicalUnit = LogicalUnit;
  /*
    For Wide SCSI Host Adapters, Wide Mode CCBs are used to support more than
    8 Logical Units per Target, and this requires setting the overloaded
    TagEnable field to Logical Unit bit 5.
  */
  if (HostAdapter->HostWideSCSI)
    {
      CCB->TagEnable = LogicalUnit >> 5;
      CCB->WideModeTagEnable = false;
    }
  else CCB->TagEnable = false;
  /*
    BusLogic recommends that after a Reset the first couple of commands that
    are sent to a Target Device be sent in a non Tagged Queue fashion so that
    the Host Adapter and Target Device can establish Synchronous and Wide
    Transfer before Queue Tag messages can interfere with the Synchronous and
    Wide Negotiation message.  By waiting to enable Tagged Queuing until after
    the first BusLogic_PreferredQueueDepth commands have been sent, it is
    assured that after a Reset any pending commands are resent before Tagged
    Queuing is enabled and that the Tagged Queuing message will not occur while
    the partition table is being printed.
  */
  if (HostAdapter->TotalCommandCount[TargetID]++ ==
        BusLogic_PreferredTaggedQueueDepth &&
      (HostAdapter->TaggedQueuingPermitted & (1 << TargetID)) &&
      Command->device->tagged_supported)
    {
      HostAdapter->TaggedQueuingActive[TargetID] = true;
      printk("scsi%d: Tagged Queuing now active for Target %d\n",
	     HostAdapter->HostNumber, TargetID);
    }
  if (HostAdapter->TaggedQueuingActive[TargetID])
    {
      BusLogic_QueueTag_T QueueTag = BusLogic_SimpleQueueTag;
      /*
	When using Tagged Queuing with Simple Queue Tags, it appears that disk
	drive controllers do not guarantee that a queued command will not
	remain in a disconnected state indefinitely if commands that read or
	write nearer the head position continue to arrive without interruption.
	Therefore, for each Target Device this driver keeps track of the last
	time either the queue was empty or an Ordered Queue Tag was issued.  If
	more than 5 seconds (half the 10 second disk timeout) have elapsed
	since this last sequence point, this command will be issued with an
	Ordered Queue Tag rather than a Simple Queue Tag, which forces the
	Target Device to complete all previously queued commands before this
	command may be executed.
      */
      if (HostAdapter->ActiveCommandCount[TargetID] == 0)
	HostAdapter->LastSequencePoint[TargetID] = jiffies;
      else if (jiffies - HostAdapter->LastSequencePoint[TargetID] > 5*HZ)
	{
	  HostAdapter->LastSequencePoint[TargetID] = jiffies;
	  QueueTag = BusLogic_OrderedQueueTag;
	}
      if (HostAdapter->HostWideSCSI)
	{
	  CCB->WideModeTagEnable = true;
	  CCB->WideModeQueueTag = QueueTag;
	}
      else
	{
	  CCB->TagEnable = true;
	  CCB->QueueTag = QueueTag;
	}
    }
  memcpy(CCB->CDB, CDB, CDB_Length);
  CCB->SenseDataPointer = Virtual_to_Bus(&Command->sense_buffer);
  CCB->Command = Command;
  Command->scsi_done = CompletionRoutine;
  /*
    Place the CCB in an Outgoing Mailbox.  The higher levels of the SCSI
    Subsystem should not attempt to queue more commands than can be placed in
    Outgoing Mailboxes, so there should always be one free.  In the unlikely
    event that there are none available, wait 1 second and try again.  If
    that fails, the Host Adapter is probably hung so we signal an error as
    a Host Adapter Hard Reset should be initiated soon.
  */
  if (!BusLogic_WriteOutgoingMailbox(HostAdapter,
				     BusLogic_MailboxStartCommand, CCB))
    {
      printk("scsi%d: cannot write Outgoing Mailbox - Pausing for 1 second\n",
	     HostAdapter->HostNumber);
      BusLogic_Delay(1);
      if (!BusLogic_WriteOutgoingMailbox(HostAdapter,
					 BusLogic_MailboxStartCommand, CCB))
	{
	  printk("scsi%d: still cannot write Outgoing Mailbox - "
		 "Host Adapter Dead?\n", HostAdapter->HostNumber);
	  BusLogic_DeallocateCCB(CCB);
	  Command->result = DID_ERROR << 16;
	  Command->scsi_done(Command);
	}
    }
  /*
    Release exclusive access to Host Adapter.
  */
Done:
  BusLogic_ReleaseHostAdapterLock(HostAdapter, &Lock);
  return 0;
}


/*
  BusLogic_AbortCommand aborts Command if possible.
*/

int BusLogic_AbortCommand(SCSI_Command_T *Command)
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Command->host->hostdata;
  int TargetID = Command->target;
  BusLogic_Lock_T Lock;
  BusLogic_CCB_T *CCB;
  int Result;
  /*
    Acquire exclusive access to Host Adapter.
  */
  BusLogic_AcquireHostAdapterLock(HostAdapter, &Lock);
  /*
    If this Command has already completed, then no Abort is necessary.
  */
  if (Command->serial_number != Command->serial_number_at_timeout)
    {
      printk("scsi%d: Unable to Abort Command to Target %d - "
	     "Already Completed\n", HostAdapter->HostNumber, TargetID);
      Result = SCSI_ABORT_NOT_RUNNING;
      goto Done;
    }
  /*
    Attempt to find an Active CCB for this Command.  If no Active CCB for this
    Command is found, then no Abort is necessary.
  */
  for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
    if (CCB->Command == Command) break;
  if (CCB == NULL)
    {
      printk("scsi%d: Unable to Abort Command to Target %d - No CCB Found\n",
	     HostAdapter->HostNumber, TargetID);
      Result = SCSI_ABORT_NOT_RUNNING;
      goto Done;
    }
  else if (CCB->Status == BusLogic_CCB_Completed)
    {
      printk("scsi%d: Unable to Abort Command to Target %d - CCB Completed\n",
	     HostAdapter->HostNumber, TargetID);
      Result = SCSI_ABORT_NOT_RUNNING;
      goto Done;
    }
  else if (CCB->Status == BusLogic_CCB_Reset)
    {
      printk("scsi%d: Unable to Abort Command to Target %d - CCB Reset\n",
	     HostAdapter->HostNumber, TargetID);
      Result = SCSI_ABORT_NOT_RUNNING;
      goto Done;
    }
  /*
    Attempt to Abort this CCB.  Firmware versions prior to 5.xx do not generate
    Abort Tag messages, but only generate the non-tagged Abort message.  Since
    non-tagged commands are not sent by the Host Adapter until the queue of
    outstanding tagged commands has completed, and the Abort message is treated
    as a non-tagged command, it is effectively impossible to abort commands
    when Tagged Queuing is active.  Firmware version 5.xx does generate Abort
    Tag messages, so it is possible to abort commands when Tagged Queuing is
    active.
  */
  if (HostAdapter->TaggedQueuingActive[TargetID] &&
      HostAdapter->FirmwareVersion[0] < '5')
    {
      printk("scsi%d: Unable to Abort CCB #%ld to Target %d - "
	     "Abort Tag Not Supported\n", HostAdapter->HostNumber,
	     CCB->SerialNumber, TargetID);
      Result = SCSI_ABORT_SNOOZE;
    }
  else if (BusLogic_WriteOutgoingMailbox(HostAdapter,
					 BusLogic_MailboxAbortCommand, CCB))
    {
      printk("scsi%d: Aborting CCB #%ld to Target %d\n",
	     HostAdapter->HostNumber, CCB->SerialNumber, TargetID);
      Result = SCSI_ABORT_PENDING;
    }
  else
    {
      printk("scsi%d: Unable to Abort CCB #%ld to Target %d - "
	     "No Outgoing Mailboxes\n", HostAdapter->HostNumber,
	     CCB->SerialNumber, TargetID);
      Result = SCSI_ABORT_BUSY;
    }
  /*
    Release exclusive access to Host Adapter.
  */
Done:
  BusLogic_ReleaseHostAdapterLock(HostAdapter, &Lock);
  return Result;
}


/*
  BusLogic_ResetHostAdapter resets Host Adapter if possible, marking all
  currently executing SCSI Commands as having been Reset.
*/

static int BusLogic_ResetHostAdapter(BusLogic_HostAdapter_T *HostAdapter,
				     SCSI_Command_T *Command,
				     unsigned int ResetFlags)
{
  BusLogic_Lock_T Lock;
  BusLogic_CCB_T *CCB;
  int TargetID, Result;
  /*
    Acquire exclusive access to Host Adapter.
  */
  BusLogic_AcquireHostAdapterLock(HostAdapter, &Lock);
  /*
    If this is an Asynchronous Reset and this Command has already completed,
    then no Reset is necessary.
  */
  if (ResetFlags & SCSI_RESET_ASYNCHRONOUS)
    {
      TargetID = Command->target;
      if (Command->serial_number != Command->serial_number_at_timeout)
	{
	  printk("scsi%d: Unable to Reset Command to Target %d - "
		 "Already Completed or Reset\n",
		 HostAdapter->HostNumber, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
      }
      for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
	if (CCB->Command == Command) break;
      if (CCB == NULL)
	{
	  printk("scsi%d: Unable to Reset Command to Target %d - "
		 "No CCB Found\n", HostAdapter->HostNumber, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      else if (CCB->Status == BusLogic_CCB_Completed)
	{
	  printk("scsi%d: Unable to Reset Command to Target %d - "
		 "CCB Completed\n", HostAdapter->HostNumber, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      else if (CCB->Status == BusLogic_CCB_Reset &&
	       HostAdapter->BusDeviceResetPendingCCB[TargetID] == NULL)
	{
	  printk("scsi%d: Unable to Reset Command to Target %d - "
		 "Reset Pending\n", HostAdapter->HostNumber, TargetID);
	  Result = SCSI_RESET_PENDING;
	  goto Done;
	}
    }
  if (Command == NULL)
    printk("scsi%d: Resetting %s due to SCSI Reset State Interrupt\n",
	   HostAdapter->HostNumber, HostAdapter->BoardName);
  else printk("scsi%d: Resetting %s due to Target %d\n",
	      HostAdapter->HostNumber, HostAdapter->BoardName, Command->target);
  /*
    Attempt to Reset and Reinitialize the Host Adapter.
  */
  if (!(BusLogic_HardResetHostAdapter(HostAdapter) &&
	BusLogic_InitializeHostAdapter(HostAdapter)))
    {
      printk("scsi%d: Resetting %s Failed\n",
	      HostAdapter->HostNumber, HostAdapter->BoardName);
      Result = SCSI_RESET_ERROR;
      goto Done;
    }
  /*
    Mark all currently executing CCBs as having been Reset.
  */
  for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
    if (CCB->Status == BusLogic_CCB_Active)
      CCB->Status = BusLogic_CCB_Reset;
  /*
    Wait a few seconds between the Host Adapter Hard Reset which initiates
    a SCSI Bus Reset and issuing any SCSI Commands.  Some SCSI devices get
    confused if they receive SCSI Commands too soon after a SCSI Bus Reset.
    Note that a timer interrupt may occur here, but all active CCBs have
    already been marked Reset and so a reentrant call will return Pending.
  */
  BusLogic_Delay(HostAdapter->BusSettleTime);
  /*
    If this is a Synchronous Reset, perform completion processing for
    the Command being Reset.
  */
  if (ResetFlags & SCSI_RESET_SYNCHRONOUS)
    {
      Command->result = DID_RESET << 16;
      Command->scsi_done(Command);
    }
  /*
    Perform completion processing for all CCBs marked as Reset.
  */
  for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
    if (CCB->Status == BusLogic_CCB_Reset)
      {
	Command = CCB->Command;
	BusLogic_DeallocateCCB(CCB);
	while (Command != NULL)
	  {
	    SCSI_Command_T *NextCommand = Command->reset_chain;
	    Command->reset_chain = NULL;
	    Command->result = DID_RESET << 16;
	    Command->scsi_done(Command);
	    Command = NextCommand;
	  }
      }
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    HostAdapter->LastResetTime[TargetID] = jiffies;
  Result = SCSI_RESET_SUCCESS | SCSI_RESET_HOST_RESET;
  /*
    Release exclusive access to Host Adapter.
  */
Done:
  BusLogic_ReleaseHostAdapterLock(HostAdapter, &Lock);
  return Result;
}


/*
  BusLogic_SendBusDeviceReset sends a Bus Device Reset to the Target
  Device associated with Command.
*/

static int BusLogic_SendBusDeviceReset(BusLogic_HostAdapter_T *HostAdapter,
				       SCSI_Command_T *Command,
				       unsigned int ResetFlags)
{
  int TargetID = Command->target;
  BusLogic_Lock_T Lock;
  BusLogic_CCB_T *CCB;
  int Result = -1;
  /*
    Acquire exclusive access to Host Adapter.
  */
  BusLogic_AcquireHostAdapterLock(HostAdapter, &Lock);
  /*
    If this is an Asynchronous Reset and this Command has already completed,
    then no Reset is necessary.
  */
  if (ResetFlags & SCSI_RESET_ASYNCHRONOUS)
    {
      if (Command->serial_number != Command->serial_number_at_timeout)
	{
	  printk("scsi%d: Unable to Reset Command to Target %d - "
		 "Already Completed\n", HostAdapter->HostNumber, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
	if (CCB->Command == Command) break;
      if (CCB == NULL)
	{
	  printk("scsi%d: Unable to Reset Command to Target %d - "
		 "No CCB Found\n", HostAdapter->HostNumber, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      else if (CCB->Status == BusLogic_CCB_Completed)
	{
	  printk("scsi%d: Unable to Reset Command to Target %d - "
		 "CCB Completed\n", HostAdapter->HostNumber, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      else if (CCB->Status == BusLogic_CCB_Reset)
	{
	  printk("scsi%d: Unable to Reset Command to Target %d - "
		 "Reset Pending\n", HostAdapter->HostNumber, TargetID);
	  Result = SCSI_RESET_PENDING;
	  goto Done;
	}
      else if (HostAdapter->BusDeviceResetPendingCCB[TargetID] != NULL)
	{
	  printk("scsi%d: Bus Device Reset already pending to Target %d\n",
		 HostAdapter->HostNumber, TargetID);
	  goto Done;
	}
    }
  /*
    If this is a Synchronous Reset and a Bus Device Reset is already pending
    for this Target Device, do not send a second one.  Add this Command to
    the list of Commands for which completion processing must be performed
    when the Bus Device Reset CCB completes.
  */
  if (ResetFlags & SCSI_RESET_SYNCHRONOUS)
    if ((CCB = HostAdapter->BusDeviceResetPendingCCB[TargetID]) != NULL)
      {
	Command->reset_chain = CCB->Command;
	CCB->Command = Command;
	printk("scsi%d: Unable to Reset Command to Target %d - "
	       "Reset Pending\n", HostAdapter->HostNumber, TargetID);
	Result = SCSI_RESET_PENDING;
	goto Done;
      }
  /*
    Firmware versions prior to 5.xx treat a Bus Device Reset as a non-tagged
    command.  Since non-tagged commands are not sent by the Host Adapter until
    the queue of outstanding tagged commands has completed, it is effectively
    impossible to send a Bus Device Reset while there are tagged commands
    outstanding.  Therefore, in that case a full Host Adapter Hard Reset and
    SCSI Bus Reset must be done.
  */
  if (HostAdapter->TaggedQueuingActive[TargetID] &&
      HostAdapter->ActiveCommandCount[TargetID] > 0 &&
      HostAdapter->FirmwareVersion[0] < '5')
    goto Done;
  /*
    Allocate a CCB from the Host Adapter's free list.  In the unlikely event
    that there are none available and memory allocation fails, attempt a full
    Host Adapter Hard Reset and SCSI Bus Reset.
  */
  CCB = BusLogic_AllocateCCB(HostAdapter);
  if (CCB == NULL) goto Done;
  printk("scsi%d: Sending Bus Device Reset CCB #%ld to Target %d\n",
	 HostAdapter->HostNumber, CCB->SerialNumber, TargetID);
  CCB->Opcode = BusLogic_BusDeviceReset;
  CCB->TargetID = TargetID;
  /*
    For Synchronous Resets, arrange for the interrupt handler to perform
    completion processing for the Command being Reset.
  */
  if (ResetFlags & SCSI_RESET_SYNCHRONOUS)
    {
      Command->reset_chain = NULL;
      CCB->Command = Command;
    }
  /*
    Attempt to write an Outgoing Mailbox with the Bus Device Reset CCB.
    If sending a Bus Device Reset is impossible, attempt a full Host
    Adapter Hard Reset and SCSI Bus Reset.
  */
  if (!(BusLogic_WriteOutgoingMailbox(HostAdapter,
				      BusLogic_MailboxStartCommand, CCB)))
    {
      printk("scsi%d: cannot write Outgoing Mailbox for Bus Device Reset\n",
	     HostAdapter->HostNumber);
      BusLogic_DeallocateCCB(CCB);
      goto Done;
    }
  /*
    If there is a currently executing CCB in the Host Adapter for this Command
    (i.e. this is an Asynchronous Reset), then an Incoming Mailbox entry may be
    made with a completion code of BusLogic_HostAdapterAssertedBusDeviceReset.
    If there is no active CCB for this Command (i.e. this is a Synchronous
    Reset), then the Bus Device Reset CCB's Command field will have been set
    to the Command so that the interrupt for the completion of the Bus Device
    Reset can call the Completion Routine for the Command.  On successful
    execution of a Bus Device Reset, older firmware versions did return the
    pending CCBs with the appropriate completion code, but more recent firmware
    versions only return the Bus Device Reset CCB itself.  This driver handles
    both cases by marking all the currently executing CCBs to this Target
    Device as Reset.  When the Bus Device Reset CCB is processed by the
    interrupt handler, any remaining CCBs marked as Reset will have completion
    processing performed.
  */
  HostAdapter->BusDeviceResetPendingCCB[TargetID] = CCB;
  HostAdapter->LastResetTime[TargetID] = jiffies;
  for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
    if (CCB->Status == BusLogic_CCB_Active && CCB->TargetID == TargetID)
      CCB->Status = BusLogic_CCB_Reset;
  Result = SCSI_RESET_PENDING;
  /*
    If a Bus Device Reset was not possible for some reason, force a full
    Host Adapter Hard Reset and SCSI Bus Reset.
  */
Done:
  if (Result < 0)
    Result = BusLogic_ResetHostAdapter(HostAdapter, Command, ResetFlags);
  /*
    Release exclusive access to Host Adapter.
  */
  BusLogic_ReleaseHostAdapterLock(HostAdapter, &Lock);
  return Result;
}


/*
  BusLogic_ResetCommand takes appropriate action to reset Command.
*/

int BusLogic_ResetCommand(SCSI_Command_T *Command, unsigned int ResetFlags)
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Command->host->hostdata;
  int TargetID = Command->target;
  int ErrorRecoveryStrategy = HostAdapter->ErrorRecoveryStrategy[TargetID];
  /*
    Disable Tagged Queuing if it is active for this Target Device and if
    it has been less than 10 minutes since the last reset occurred, or since
    the system was initialized if no prior resets have occurred.
  */
  if (HostAdapter->TaggedQueuingActive[TargetID] &&
      jiffies - HostAdapter->LastResetTime[TargetID] < 10*60*HZ)
    {
      HostAdapter->TaggedQueuingPermitted &= ~(1 << TargetID);
      HostAdapter->TaggedQueuingActive[TargetID] = false;
      printk("scsi%d: Tagged Queuing now disabled for Target %d\n",
	     HostAdapter->HostNumber, TargetID);
    }
  if (ErrorRecoveryStrategy == BusLogic_ErrorRecovery_Default)
    if (ResetFlags & SCSI_RESET_SUGGEST_HOST_RESET)
      ErrorRecoveryStrategy = BusLogic_ErrorRecovery_HardReset;
    else if (ResetFlags & SCSI_RESET_SUGGEST_BUS_RESET)
      ErrorRecoveryStrategy = BusLogic_ErrorRecovery_HardReset;
    else ErrorRecoveryStrategy = BusLogic_ErrorRecovery_BusDeviceReset;
  switch (ErrorRecoveryStrategy)
    {
    case BusLogic_ErrorRecovery_HardReset:
      return BusLogic_ResetHostAdapter(HostAdapter, Command, ResetFlags);
    case BusLogic_ErrorRecovery_BusDeviceReset:
      /*
	The Bus Device Reset Error Recovery Strategy only graduates to a Hard
	Reset when no commands have completed successfully since the last Bus
	Device Reset and it has been at least 100 milliseconds.  This prevents
	a sequence of commands that all timeout together from immediately
	forcing a Hard Reset before the Bus Device Reset has had a chance to
	clear the error condition.
      */
      if (HostAdapter->CommandSuccessfulFlag[TargetID] ||
	  jiffies - HostAdapter->LastResetTime[TargetID] < HZ/10)
	{
	  HostAdapter->CommandSuccessfulFlag[TargetID] = false;
	  return BusLogic_SendBusDeviceReset(HostAdapter, Command, ResetFlags);
	}
      else return BusLogic_ResetHostAdapter(HostAdapter, Command, ResetFlags);
    }
  printk("scsi%d: Error Recovery for Target %d Suppressed\n",
	 HostAdapter->HostNumber, TargetID);
  return SCSI_RESET_PUNT;
}


/*
  BusLogic_BIOSDiskParameters returns the Heads/Sectors/Cylinders BIOS Disk
  Parameters for Disk.  The default disk geometry is 64 heads, 32 sectors,
  and the appropriate number of cylinders so as not to exceed drive capacity.
  In order for disks equal to or larger than 1 GB to be addressable by the
  BIOS without exceeding the BIOS limitation of 1024 cylinders, Extended
  Translation may be enabled in AutoSCSI on "W" and "C" Series boards or by a
  dip switch setting on older boards.  With Extended Translation enabled,
  drives between 1 GB inclusive and 2 GB exclusive are given a disk geometry
  of 128 heads and 32 sectors, and drives above 2 GB inclusive are given a
  disk geometry of 255 heads and 63 sectors.  However, if the BIOS detects
  that the Extended Translation setting does not match the geometry in the
  partition table, then the translation inferred from the partition table
  will be used by the BIOS, and a warning may be displayed.
*/

int BusLogic_BIOSDiskParameters(SCSI_Disk_T *Disk, KernelDevice_T Device,
				int *Parameters)
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Disk->device->host->hostdata;
  BIOS_DiskParameters_T *DiskParameters = (BIOS_DiskParameters_T *) Parameters;
  struct buffer_head *BufferHead;
  if (HostAdapter->ExtendedTranslation &&
      Disk->capacity >= 2*1024*1024 /* 1 GB in 512 byte sectors */)
    if (Disk->capacity >= 4*1024*1024 /* 2 GB in 512 byte sectors */)
      {
	DiskParameters->Heads = 255;
	DiskParameters->Sectors = 63;
      }
    else
      {
	DiskParameters->Heads = 128;
	DiskParameters->Sectors = 32;
      }
  else
    {
      DiskParameters->Heads = 64;
      DiskParameters->Sectors = 32;
    }
  DiskParameters->Cylinders =
    Disk->capacity / (DiskParameters->Heads * DiskParameters->Sectors);
  /*
    Attempt to read the first 1024 bytes from the disk device.
  */
  BufferHead = bread(MKDEV(MAJOR(Device), MINOR(Device) & ~0x0F), 0, 1024);
  if (BufferHead == NULL) return 0;
  /*
    If the boot sector partition table flag is valid, search for a partition
    table entry whose end_head matches one of the standard BusLogic geometry
    translations (64/32, 128/32, or 255/63).
  */
  if (*(unsigned short *) (BufferHead->b_data + 0x1FE) == 0xAA55)
    {
      struct partition *PartitionEntry =
	(struct partition *) (BufferHead->b_data + 0x1BE);
      int SavedCylinders = DiskParameters->Cylinders, PartitionNumber;
      for (PartitionNumber = 0; PartitionNumber < 4; PartitionNumber++)
	{
	  if (PartitionEntry->end_head == 64-1)
	    {
	      DiskParameters->Heads = 64;
	      DiskParameters->Sectors = 32;
	      break;
	    }
	  else if (PartitionEntry->end_head == 128-1)
	    {
	      DiskParameters->Heads = 128;
	      DiskParameters->Sectors = 32;
	      break;
	    }
	  else if (PartitionEntry->end_head == 255-1)
	    {
	      DiskParameters->Heads = 255;
	      DiskParameters->Sectors = 63;
	      break;
	    }
	  PartitionEntry++;
	}
      DiskParameters->Cylinders =
	Disk->capacity / (DiskParameters->Heads * DiskParameters->Sectors);
      if (SavedCylinders != DiskParameters->Cylinders)
	printk("scsi%d: Warning: Extended Translation Setting "
	       "(> 1GB Switch) does not match\n"
	       "scsi%d: Partition Table - Adopting %d/%d Geometry "
	       "from Partition Table\n",
	       HostAdapter->HostNumber, HostAdapter->HostNumber,
	       DiskParameters->Heads, DiskParameters->Sectors);
    }
  brelse(BufferHead);
  return 0;
}


/*
  BusLogic_Setup handles processing of Kernel Command Line Arguments.

  For the BusLogic driver, a Kernel command line entry comprises the driver
  identifier "BusLogic=" optionally followed by a comma-separated sequence of
  integers and then optionally followed by a comma-separated sequence of
  strings.  Each command line entry applies to one BusLogic Host Adapter.
  Multiple command line entries may be used in systems which contain multiple
  BusLogic Host Adapters.

  The first integer specified is the I/O Address at which the Host Adapter is
  located.  If unspecified, it defaults to 0 which means to apply this entry to
  the first BusLogic Host Adapter found during the default probe sequence.  If
  any I/O Address parameters are provided on the command line, then the default
  probe sequence is omitted.

  The second integer specified is the Tagged Queue Depth to use for Target
  Devices that support Tagged Queuing.  The Queue Depth is the number of SCSI
  commands that are allowed to be concurrently presented for execution.  If
  unspecified, it defaults to 0 which means to use a value determined
  automatically based on the Host Adapter's Total Queue Depth and the number,
  type, speed, and capabilities of the detected Target Devices.  For Host
  Adapters that require ISA Bounce Buffers, the Tagged Queue Depth is
  automatically set to BusLogic_TaggedQueueDepth_BB to avoid excessive
  preallocation of DMA Bounce Buffer memory.  Target Devices that do not
  support Tagged Queuing use a Queue Depth of BusLogic_UntaggedQueueDepth.

  The third integer specified is the Bus Settle Time in seconds.  This is
  the amount of time to wait between a Host Adapter Hard Reset which initiates
  a SCSI Bus Reset and issuing any SCSI Commands.  If unspecified, it defaults
  to 0 which means to use the value of BusLogic_DefaultBusSettleTime.

  The fourth integer specified is the Local Options.  If unspecified, it
  defaults to 0.  Note that Local Options are only applied to a specific Host
  Adapter.

  The fifth integer specified is the Global Options.  If unspecified, it
  defaults to 0.  Note that Global Options are applied across all Host
  Adapters.

  The string options are used to provide control over Tagged Queuing, Error
  Recovery, and Host Adapter Probing.

  The Tagged Queuing specification begins with "TQ:" and allows for explicitly
  specifying whether Tagged Queuing is permitted on Target Devices that support
  it.  The following specification options are available:

  TQ:Default		Tagged Queuing will be permitted based on the firmware
			version of the BusLogic Host Adapter and based on
			whether the Tagged Queue Depth value allows queuing
			multiple commands.

  TQ:Enable		Tagged Queuing will be enabled for all Target Devices
			on this Host Adapter overriding any limitation that
			would otherwise be imposed based on the Host Adapter
			firmware version.

  TQ:Disable		Tagged Queuing will be disabled for all Target Devices
			on this Host Adapter.

  TQ:<Per-Target-Spec>	Tagged Queuing will be controlled individually for each
			Target Device.  <Per-Target-Spec> is a sequence of "Y",
			"N", and "X" characters.  "Y" enabled Tagged Queuing,
			"N" disables Tagged Queuing, and "X" accepts the
			default based on the firmware version.  The first
			character refers to Target Device 0, the second to
			Target Device 1, and so on; if the sequence of "Y",
			"N", and "X" characters does not cover all the Target
			Devices, unspecified characters are assumed to be "X".

  Note that explicitly requesting Tagged Queuing may lead to problems; this
  facility is provided primarily to allow disabling Tagged Queuing on Target
  Devices that do not implement it correctly.

  The Error Recovery Strategy specification begins with "ER:" and allows for
  explicitly specifying the Error Recovery action to be performed when
  ResetCommand is called due to a SCSI Command failing to complete
  successfully.  The following specification options are available:

  ER:Default		Error Recovery will select between the Hard Reset and
			Bus Device Reset options based on the recommendation
			of the SCSI Subsystem.

  ER:HardReset		Error Recovery will initiate a Host Adapter Hard Reset
			which also causes a SCSI Bus Reset.

  ER:BusDeviceReset	Error Recovery will send a Bus Device Reset message to
			the individual Target Device causing the error.  If
			Error Recovery is again initiated for this Target
			Device and no SCSI Command to this Target Device has
			completed successfully since the Bus Device Reset
			message was sent, then a Hard Reset will be attempted.

  ER:None		Error Recovery will be suppressed.  This option should
			only be selected if a SCSI Bus Reset or Bus Device
			Reset will cause the Target Device to fail completely
			and unrecoverably.

  ER:<Per-Target-Spec>	Error Recovery will be controlled individually for each
			Target Device.  <Per-Target-Spec> is a sequence of "D",
			"H", "B", and "N" characters.  "D" selects Default, "H"
			selects Hard Reset, "B" selects Bus Device Reset, and
			"N" selects None.  The first character refers to Target
			Device 0, the second to Target Device 1, and so on; if
			the sequence of "D", "H", "B", and "N" characters does
			not cover all the possible Target Devices, unspecified
			characters are assumed to be "D".

  The Host Adapter Probing specification comprises the following strings:

  NoProbe		No probing of any kind is to be performed, and hence
			no BusLogic Host Adapters will be detected.

  NoProbeISA		No probing of the standard ISA I/O Addresses will
			be done, and hence only PCI Host Adapters will be
			detected.

  NoSortPCI		PCI Host Adapters will be enumerated in the order
			provided by the PCI BIOS, ignoring any setting of
			the AutoSCSI "Use Bus And Device # For PCI Scanning
			Seq." option.
*/

void BusLogic_Setup(char *Strings, int *Integers)
{
  BusLogic_CommandLineEntry_T *CommandLineEntry =
    &BusLogic_CommandLineEntries[BusLogic_CommandLineEntryCount++];
  static int ProbeListIndex = 0;
  int IntegerCount = Integers[0];
  int TargetID, i;
  CommandLineEntry->IO_Address = 0;
  CommandLineEntry->TaggedQueueDepth = 0;
  CommandLineEntry->BusSettleTime = 0;
  CommandLineEntry->LocalOptions = 0;
  CommandLineEntry->TaggedQueuingPermitted = 0;
  CommandLineEntry->TaggedQueuingPermittedMask = 0;
  memset(CommandLineEntry->ErrorRecoveryStrategy,
	 BusLogic_ErrorRecovery_Default,
	 sizeof(CommandLineEntry->ErrorRecoveryStrategy));
  if (IntegerCount > 5)
    printk("BusLogic: Unexpected Command Line Integers ignored\n");
  if (IntegerCount >= 1)
    {
      unsigned int IO_Address = Integers[1];
      if (IO_Address > 0)
	{
	  for (i = 0; ; i++)
	    if (BusLogic_IO_StandardAddresses[i] == 0)
	      {
		printk("BusLogic: Invalid Command Line Entry "
		       "(illegal I/O Address 0x%X)\n", IO_Address);
		return;
	      }
	    else if (i < ProbeListIndex &&
		     IO_Address == BusLogic_IO_AddressProbeList[i])
	      {
		printk("BusLogic: Invalid Command Line Entry "
		       "(duplicate I/O Address 0x%X)\n", IO_Address);
		return;
	      }
	    else if (IO_Address >= 0x400 ||
		     IO_Address == BusLogic_IO_StandardAddresses[i]) break;
	  BusLogic_IO_AddressProbeList[ProbeListIndex++] = IO_Address;
	  BusLogic_IO_AddressProbeList[ProbeListIndex] = 0;
	}
      CommandLineEntry->IO_Address = IO_Address;
    }
  if (IntegerCount >= 2)
    {
      unsigned short TaggedQueueDepth = Integers[2];
      if (TaggedQueueDepth > BusLogic_MaxTaggedQueueDepth)
	{
	  printk("BusLogic: Invalid Command Line Entry "
		 "(illegal Tagged Queue Depth %d)\n", TaggedQueueDepth);
	  return;
	}
      CommandLineEntry->TaggedQueueDepth = TaggedQueueDepth;
    }
  if (IntegerCount >= 3)
    CommandLineEntry->BusSettleTime = Integers[3];
  if (IntegerCount >= 4)
    CommandLineEntry->LocalOptions = Integers[4];
  if (IntegerCount >= 5)
    BusLogic_GlobalOptions |= Integers[5];
  if (!(BusLogic_CommandLineEntryCount == 0 || ProbeListIndex == 0 ||
	BusLogic_CommandLineEntryCount == ProbeListIndex))
    {
      printk("BusLogic: Invalid Command Line Entry "
	     "(all or no I/O Addresses must be specified)\n");
      return;
    }
  if (Strings == NULL) return;
  while (*Strings != '\0')
    {
      if (strncmp(Strings, "TQ:", 3) == 0)
	{
	  Strings += 3;
	  if (strncmp(Strings, "Default", 7) == 0)
	    Strings += 7;
	  else if (strncmp(Strings, "Enable", 6) == 0)
	    {
	      Strings += 6;
	      CommandLineEntry->TaggedQueuingPermitted = 0xFFFF;
	      CommandLineEntry->TaggedQueuingPermittedMask = 0xFFFF;
	    }
	  else if (strncmp(Strings, "Disable", 7) == 0)
	    {
	      Strings += 7;
	      CommandLineEntry->TaggedQueuingPermitted = 0x0000;
	      CommandLineEntry->TaggedQueuingPermittedMask = 0xFFFF;
	    }
	  else
	    for (TargetID = 0; TargetID < BusLogic_MaxTargetDevices; TargetID++)
	      switch (*Strings++)
		{
		case 'Y':
		  CommandLineEntry->TaggedQueuingPermitted |= 1 << TargetID;
		  CommandLineEntry->TaggedQueuingPermittedMask |= 1 << TargetID;
		  break;
		case 'N':
		  CommandLineEntry->TaggedQueuingPermittedMask |= 1 << TargetID;
		  break;
		case 'X':
		  break;
		default:
		  Strings--;
		  TargetID = BusLogic_MaxTargetDevices;
		  break;
		}
	}
      else if (strncmp(Strings, "ER:", 3) == 0)
	{
	  Strings += 3;
	  if (strncmp(Strings, "Default", 7) == 0)
	    Strings += 7;
	  else if (strncmp(Strings, "HardReset", 9) == 0)
	    {
	      Strings += 9;
	      memset(CommandLineEntry->ErrorRecoveryStrategy,
		     BusLogic_ErrorRecovery_HardReset,
		     sizeof(CommandLineEntry->ErrorRecoveryStrategy));
	    }
	  else if (strncmp(Strings, "BusDeviceReset", 14) == 0)
	    {
	      Strings += 14;
	      memset(CommandLineEntry->ErrorRecoveryStrategy,
		     BusLogic_ErrorRecovery_BusDeviceReset,
		     sizeof(CommandLineEntry->ErrorRecoveryStrategy));
	    }
	  else if (strncmp(Strings, "None", 4) == 0)
	    {
	      Strings += 4;
	      memset(CommandLineEntry->ErrorRecoveryStrategy,
		     BusLogic_ErrorRecovery_None,
		     sizeof(CommandLineEntry->ErrorRecoveryStrategy));
	    }
	  else
	    for (TargetID = 0; TargetID < BusLogic_MaxTargetDevices; TargetID++)
	      switch (*Strings++)
		{
		case 'D':
		  CommandLineEntry->ErrorRecoveryStrategy[TargetID] =
		    BusLogic_ErrorRecovery_Default;
		  break;
		case 'H':
		  CommandLineEntry->ErrorRecoveryStrategy[TargetID] =
		    BusLogic_ErrorRecovery_HardReset;
		  break;
		case 'B':
		  CommandLineEntry->ErrorRecoveryStrategy[TargetID] =
		    BusLogic_ErrorRecovery_BusDeviceReset;
		  break;
		case 'N':
		  CommandLineEntry->ErrorRecoveryStrategy[TargetID] =
		    BusLogic_ErrorRecovery_None;
		  break;
		default:
		  Strings--;
		  TargetID = BusLogic_MaxTargetDevices;
		  break;
		}
	}
      else if (strcmp(Strings, "NoProbe") == 0 ||
	       strcmp(Strings, "noprobe") == 0)
	{
	  Strings += 7;
	  BusLogic_ProbeOptions |= BusLogic_NoProbe;
	}
      else if (strncmp(Strings, "NoProbeISA", 10) == 0)
	{
	  Strings += 10;
	  BusLogic_ProbeOptions |= BusLogic_NoProbeISA;
	}
      else if (strncmp(Strings, "NoSortPCI", 9) == 0)
	{
	  Strings += 9;
	  BusLogic_ProbeOptions |= BusLogic_NoSortPCI;
	}
      else
	{
	  printk("BusLogic: Unexpected Command Line String '%s' ignored\n",
		 Strings);
	  break;
	}
      if (*Strings == ',') Strings++;
    }
}


/*
  Include Module support if requested.
*/

#ifdef MODULE

SCSI_Host_Template_T driver_template = BUSLOGIC;

#include "scsi_module.c"

#endif

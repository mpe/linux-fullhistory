/*

  Linux Driver for BusLogic MultiMaster and FlashPoint SCSI Host Adapters

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

  Special thanks to Wayne Yen, Jin-Lon Hon, and Alex Win of BusLogic, whose
  advice has been invaluable, to David Gentzel, for writing the original Linux
  BusLogic driver, and to Paul Gortmaker, for being such a dedicated test site.

  Finally, special thanks to Mylex/BusLogic for making the FlashPoint SCCB
  Manager available as freely redistributable source code.

*/


#define BusLogic_DriverVersion		"2.0.9"
#define BusLogic_DriverDate		"29 March 1997"


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
#include <linux/init.h>
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
  BusLogic_ProbeOptions is a set of Probe Options to be applied across
  all BusLogic Host Adapters.
*/

static BusLogic_ProbeOptions_T
  BusLogic_ProbeOptions =		{ 0 };


/*
  BusLogic_GlobalOptions is a set of Global Options to be applied across
  all BusLogic Host Adapters.
*/

static BusLogic_GlobalOptions_T
  BusLogic_GlobalOptions =		{ 0 };


/*
  BusLogic_RegisteredHostAdapters is an array of linked lists of all the
  registered BusLogic Host Adapters, indexed by IRQ Channel.
*/

static BusLogic_HostAdapter_T
  *BusLogic_RegisteredHostAdapters[NR_IRQS] =	{ NULL };


/*
  BusLogic_ProbeInfoCount is the numbers of entries in BusLogic_ProbeInfoList.
*/

static int
  BusLogic_ProbeInfoCount =		0;


/*
  BusLogic_ProbeInfoList is the list of I/O Addresses and Bus Probe Information
  to be checked for potential BusLogic Host Adapters.  It is initialized by
  interrogating the PCI Configuration Space on PCI machines as well as from the
  list of standard BusLogic I/O Addresses.
*/

static BusLogic_ProbeInfo_T
  BusLogic_ProbeInfoList[BusLogic_MaxHostAdapters] =	{ { 0 } };


/*
  BusLogic_CommandFailureReason holds a string identifying the reason why a
  call to BusLogic_Command failed.  It is only non-NULL when BusLogic_Command
  returns a failure code.
*/

static char
  *BusLogic_CommandFailureReason;


/*
  BusLogic_FirstCompletedCCB and BusLogic_LastCompletedCCB are pointers
  to the first and last CCBs that are queued for completion processing.
*/

static BusLogic_CCB_T
  *BusLogic_FirstCompletedCCB =		NULL,
  *BusLogic_LastCompletedCCB =		NULL;


/*
  BusLogic_ProcDirectoryEntry is the BusLogic /proc/scsi directory entry.
*/

PROC_DirectoryEntry_T
  BusLogic_ProcDirectoryEntry =
    { PROC_SCSI_BUSLOGIC, 8, "BusLogic", S_IFDIR | S_IRUGO | S_IXUGO, 2 };


/*
  BusLogic_AnnounceDriver announces the Driver Version and Date, Author's
  Name, Copyright Notice, and Electronic Mail Address.
*/

static void BusLogic_AnnounceDriver(BusLogic_HostAdapter_T *HostAdapter)
{
  BusLogic_Announce("***** BusLogic SCSI Driver Version "
		    BusLogic_DriverVersion " of "
		    BusLogic_DriverDate " *****\n", HostAdapter);
  BusLogic_Announce("Copyright 1995 by Leonard N. Zubkoff "
		    "<lnz@dandelion.com>\n", HostAdapter);
}


/*
  BusLogic_DriverInfo returns the Host Adapter Name to identify this SCSI
  Driver and Host Adapter.
*/

const char *BusLogic_DriverInfo(SCSI_Host_T *Host)
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Host->hostdata;
  return HostAdapter->FullModelName;
}


/*
  BusLogic_RegisterHostAdapter adds Host Adapter to the list of registered
  BusLogic Host Adapters.
*/

static void BusLogic_RegisterHostAdapter(BusLogic_HostAdapter_T *HostAdapter)
{
  HostAdapter->Next = NULL;
  if (BusLogic_RegisteredHostAdapters[HostAdapter->IRQ_Channel] != NULL)
    {
      BusLogic_HostAdapter_T *LastHostAdapter =
	BusLogic_RegisteredHostAdapters[HostAdapter->IRQ_Channel];
      BusLogic_HostAdapter_T *NextHostAdapter;
      while ((NextHostAdapter = LastHostAdapter->Next) != NULL)
	LastHostAdapter = NextHostAdapter;
      LastHostAdapter->Next = HostAdapter;
    }
  else BusLogic_RegisteredHostAdapters[HostAdapter->IRQ_Channel] = HostAdapter;
}


/*
  BusLogic_UnregisterHostAdapter removes Host Adapter from the list of
  registered BusLogic Host Adapters.
*/

static void BusLogic_UnregisterHostAdapter(BusLogic_HostAdapter_T *HostAdapter)
{
  if (BusLogic_RegisteredHostAdapters[HostAdapter->IRQ_Channel] != HostAdapter)
    {
      BusLogic_HostAdapter_T *LastHostAdapter =
	BusLogic_RegisteredHostAdapters[HostAdapter->IRQ_Channel];
      while (LastHostAdapter != NULL && LastHostAdapter->Next != HostAdapter)
	LastHostAdapter = LastHostAdapter->Next;
      if (LastHostAdapter != NULL)
	LastHostAdapter->Next = HostAdapter->Next;
    }
  else BusLogic_RegisteredHostAdapters[HostAdapter->IRQ_Channel] =
	 HostAdapter->Next;
  HostAdapter->Next = NULL;
}


/*
  BusLogic_CreateMailboxes allocates the Outgoing and Incoming Mailboxes for
  Host Adapter.
*/

__initfunc(static boolean
BusLogic_CreateMailboxes(BusLogic_HostAdapter_T *HostAdapter))
{
  /*
    FlashPoint Host Adapters do not use Outgoing and Incoming Mailboxes.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter)) return true;
  /*
    Allocate space for the Outgoing and Incoming Mailboxes.
  */
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
      BusLogic_Error("UNABLE TO ALLOCATE MAILBOXES - DETACHING\n",
		     HostAdapter, HostAdapter->HostNumber);
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
  BusLogic_CreateCCB allocates and initializes a single Command Control
  Block (CCB) for Host Adapter, and adds it to Host Adapter's free list.
*/

static boolean BusLogic_CreateCCB(BusLogic_HostAdapter_T *HostAdapter)
{
  BusLogic_CCB_T *CCB = (BusLogic_CCB_T *)
    scsi_init_malloc(sizeof(BusLogic_CCB_T),
		     (HostAdapter->BounceBuffersRequired
		      ? GFP_ATOMIC | GFP_DMA
		      : GFP_ATOMIC));
  if (CCB == NULL) return false;
  memset(CCB, 0, sizeof(BusLogic_CCB_T));
  CCB->HostAdapter = HostAdapter;
  CCB->Status = BusLogic_CCB_Free;
  if (BusLogic_FlashPointHostAdapterP(HostAdapter))
    {
      CCB->CallbackFunction = BusLogic_QueueCompletedCCB;
      CCB->BaseAddress = HostAdapter->IO_Address;
    }
  CCB->Next = HostAdapter->Free_CCBs;
  CCB->NextAll = HostAdapter->All_CCBs;
  HostAdapter->Free_CCBs = CCB;
  HostAdapter->All_CCBs = CCB;
  HostAdapter->AllocatedCCBs++;
  return true;
}


/*
  BusLogic_CreateInitialCCBs allocates the initial CCBs for Host Adapter.
*/

__initfunc(static boolean
BusLogic_CreateInitialCCBs(BusLogic_HostAdapter_T *HostAdapter))
{
  int Allocated;
  for (Allocated = 0; Allocated < HostAdapter->InitialCCBs; Allocated++)
    if (!BusLogic_CreateCCB(HostAdapter))
      {
	BusLogic_Error("UNABLE TO ALLOCATE CCB %d - DETACHING\n",
		       HostAdapter, Allocated);
	return false;
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
  BusLogic_CreateAdditionalCCBs allocates Additional CCBs for Host Adapter.  If
  allocation fails and there are no remaining CCBs available, the Driver Queue
  Depth is decreased to a known safe value to avoid potential deadlocks when
  multiple host adapters share the same IRQ Channel.
*/

static void BusLogic_CreateAdditionalCCBs(BusLogic_HostAdapter_T *HostAdapter,
					  int AdditionalCCBs,
					  boolean SuccessMessageP)
{
  int Allocated;
  if (AdditionalCCBs <= 0) return;
  for (Allocated = 0; Allocated < AdditionalCCBs; Allocated++)
    if (!BusLogic_CreateCCB(HostAdapter)) break;
  if (Allocated > 0 && SuccessMessageP)
    BusLogic_Notice("Allocated %d additional CCBs (total now %d)\n",
		    HostAdapter, Allocated, HostAdapter->AllocatedCCBs);
  if (Allocated > 0) return;
  BusLogic_Notice("Failed to allocate additional CCBs\n", HostAdapter);
  HostAdapter->DriverQueueDepth =
    HostAdapter->AllocatedCCBs - (HostAdapter->MaxTargetDevices - 1);
  HostAdapter->SCSI_Host->can_queue = HostAdapter->DriverQueueDepth;
}


/*
  BusLogic_AllocateCCB allocates a CCB from Host Adapter's free list,
  allocating more memory from the Kernel if necessary.  The Host Adapter's
  Lock should already have been acquired by the caller.
*/

static BusLogic_CCB_T *BusLogic_AllocateCCB(BusLogic_HostAdapter_T
					    *HostAdapter)
{
  static unsigned long SerialNumber = 0;
  BusLogic_CCB_T *CCB;
  CCB = HostAdapter->Free_CCBs;
  if (CCB != NULL)
    {
      CCB->SerialNumber = ++SerialNumber;
      HostAdapter->Free_CCBs = CCB->Next;
      CCB->Next = NULL;
      if (HostAdapter->Free_CCBs == NULL)
	BusLogic_CreateAdditionalCCBs(HostAdapter,
				      HostAdapter->IncrementalCCBs,
				      true);
      return CCB;
    }
  BusLogic_CreateAdditionalCCBs(HostAdapter,
				HostAdapter->IncrementalCCBs,
				true);
  CCB = HostAdapter->Free_CCBs;
  if (CCB == NULL) return NULL;
  CCB->SerialNumber = ++SerialNumber;
  HostAdapter->Free_CCBs = CCB->Next;
  CCB->Next = NULL;
  return CCB;
}


/*
  BusLogic_DeallocateCCB deallocates a CCB, returning it to the Host Adapter's
  free list.  The Host Adapter's Lock should already have been acquired by the
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
  BusLogic_CreateTargetDeviceStatistics creates the Target Device Statistics
  structure for Host Adapter.
*/

__initfunc(static boolean
BusLogic_CreateTargetDeviceStatistics(BusLogic_HostAdapter_T *HostAdapter))
{
  HostAdapter->TargetDeviceStatistics =
    (BusLogic_TargetDeviceStatistics_T *)
      scsi_init_malloc(HostAdapter->MaxTargetDevices
		       * sizeof(BusLogic_TargetDeviceStatistics_T),
		       GFP_ATOMIC);
  if (HostAdapter->TargetDeviceStatistics == NULL)
    {
      BusLogic_Error("UNABLE TO ALLOCATE TARGET DEVICE STATISTICS - "
		     "DETACHING\n", HostAdapter, HostAdapter->HostNumber);
      return false;
    }
  memset(HostAdapter->TargetDeviceStatistics, 0,
	 HostAdapter->MaxTargetDevices
	 * sizeof(BusLogic_TargetDeviceStatistics_T));
  return true;
}


/*
  BusLogic_DestroyTargetDeviceStatistics destroys the Target Device Statistics
  structure for Host Adapter.
*/

static void BusLogic_DestroyTargetDeviceStatistics(BusLogic_HostAdapter_T
						   *HostAdapter)
{
  if (HostAdapter->TargetDeviceStatistics == NULL) return;
  scsi_init_free((char *) HostAdapter->TargetDeviceStatistics,
		 HostAdapter->MaxTargetDevices
		 * sizeof(BusLogic_TargetDeviceStatistics_T));
}


/*
  BusLogic_Command sends the command OperationCode to HostAdapter, optionally
  providing ParameterLength bytes of ParameterData and receiving at most
  ReplyLength bytes of ReplyData; any excess reply data is received but
  discarded.

  On success, this function returns the number of reply bytes read from
  the Host Adapter (including any discarded data); on failure, it returns
  -1 if the command was invalid, or -2 if a timeout occurred.

  BusLogic_Command is called exclusively during host adapter detection and
  initialization, so performance and latency are not critical, and exclusive
  access to the Host Adapter hardware is assumed.  Once the host adapter and
  driver are initialized, the only Host Adapter command that is issued is the
  single byte Execute Mailbox Command operation code, which does not require
  waiting for the Host Adapter Ready bit to be set in the Status Register.
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
  BusLogic_StatusRegister_T StatusRegister;
  BusLogic_InterruptRegister_T InterruptRegister;
  unsigned long ProcessorFlags = 0;
  int ReplyBytes = 0, Result;
  long TimeoutCounter;
  /*
    Clear out the Reply Data if provided.
  */
  if (ReplyLength > 0)
    memset(ReplyData, 0, ReplyLength);
  /*
    If the IRQ Channel has not yet been acquired, then interrupts must be
    disabled while issuing host adapter commands since a Command Complete
    interrupt could occur if the IRQ Channel was previously enabled by another
    BusLogic Host Adapter or other driver sharing the same IRQ Channel.
  */
  if (!HostAdapter->IRQ_ChannelAcquired)
    {
      save_flags(ProcessorFlags);
      cli();
    }
  /*
    Wait for the Host Adapter Ready bit to be set and the Command/Parameter
    Register Busy bit to be reset in the Status Register.
  */
  TimeoutCounter = loops_per_sec >> 3;
  while (--TimeoutCounter >= 0)
    {
      StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
      if (StatusRegister.Bits.HostAdapterReady &&
	  !StatusRegister.Bits.CommandParameterRegisterBusy)
	break;
    }
  if (TimeoutCounter < 0)
    {
      BusLogic_CommandFailureReason = "Timeout waiting for Host Adapter Ready";
      Result = -2;
      goto Done;
    }
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
      InterruptRegister.All = BusLogic_ReadInterruptRegister(HostAdapter);
      StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
      if (InterruptRegister.Bits.CommandComplete) break;
      if (HostAdapter->HostAdapterCommandCompleted) break;
      if (StatusRegister.Bits.DataInRegisterReady) break;
      if (StatusRegister.Bits.CommandParameterRegisterBusy) continue;
      BusLogic_WriteCommandParameterRegister(HostAdapter, *ParameterPointer++);
      ParameterLength--;
    }
  if (TimeoutCounter < 0)
    {
      BusLogic_CommandFailureReason =
	"Timeout waiting for Parameter Acceptance";
      Result = -2;
      goto Done;
    }
  /*
    The Modify I/O Address command does not cause a Command Complete Interrupt.
  */
  if (OperationCode == BusLogic_ModifyIOAddress)
    {
      StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
      if (StatusRegister.Bits.CommandInvalid)
	{
	  BusLogic_CommandFailureReason = "Modify I/O Address Invalid";
	  Result = -1;
	  goto Done;
	}
      if (BusLogic_GlobalOptions.Bits.TraceConfiguration)
	BusLogic_Notice("BusLogic_Command(%02X) Status = %02X: "
			"(Modify I/O Address)\n", HostAdapter,
			OperationCode, StatusRegister.All);
      Result = 0;
      goto Done;
    }
  /*
    Select an appropriate timeout value for awaiting command completion.
  */
  switch (OperationCode)
    {
    case BusLogic_InquireInstalledDevicesID0to7:
    case BusLogic_InquireInstalledDevicesID8to15:
    case BusLogic_InquireTargetDevices:
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
      InterruptRegister.All = BusLogic_ReadInterruptRegister(HostAdapter);
      StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
      if (InterruptRegister.Bits.CommandComplete) break;
      if (HostAdapter->HostAdapterCommandCompleted) break;
      if (StatusRegister.Bits.DataInRegisterReady)
	if (++ReplyBytes <= ReplyLength)
	  *ReplyPointer++ = BusLogic_ReadDataInRegister(HostAdapter);
	else BusLogic_ReadDataInRegister(HostAdapter);
      if (OperationCode == BusLogic_FetchHostAdapterLocalRAM &&
	  StatusRegister.Bits.HostAdapterReady) break;
    }
  if (TimeoutCounter < 0)
    {
      BusLogic_CommandFailureReason = "Timeout waiting for Command Complete";
      Result = -2;
      goto Done;
    }
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
  BusLogic_InterruptReset(HostAdapter);
  /*
    Provide tracing information if requested.
  */
  if (BusLogic_GlobalOptions.Bits.TraceConfiguration)
    if (OperationCode != BusLogic_TestCommandCompleteInterrupt)
      {
	int i;
	BusLogic_Notice("BusLogic_Command(%02X) Status = %02X: %2d ==> %2d:",
			HostAdapter, OperationCode,
			StatusRegister.All, ReplyLength, ReplyBytes);
	if (ReplyLength > ReplyBytes) ReplyLength = ReplyBytes;
	for (i = 0; i < ReplyLength; i++)
	  BusLogic_Notice(" %02X", HostAdapter,
			  ((unsigned char *) ReplyData)[i]);
	BusLogic_Notice("\n", HostAdapter);
      }
  /*
    Process Command Invalid conditions.
  */
  if (StatusRegister.Bits.CommandInvalid)
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
      StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
      if (StatusRegister.Bits.CommandInvalid ||
	  StatusRegister.Bits.Reserved ||
	  StatusRegister.Bits.DataInRegisterReady ||
	  StatusRegister.Bits.CommandParameterRegisterBusy ||
	  !StatusRegister.Bits.HostAdapterReady ||
	  !StatusRegister.Bits.InitializationRequired ||
	  StatusRegister.Bits.DiagnosticActive ||
	  StatusRegister.Bits.DiagnosticFailure)
	{
	  BusLogic_SoftReset(HostAdapter);
	  udelay(1000);
	}
      BusLogic_CommandFailureReason = "Command Invalid";
      Result = -1;
      goto Done;
    }
  /*
    Handle Excess Parameters Supplied conditions.
  */
  if (ParameterLength > 0)
    {
      BusLogic_CommandFailureReason = "Excess Parameters Supplied";
      Result = -1;
      goto Done;
    }
  /*
    Indicate the command completed successfully.
  */
  BusLogic_CommandFailureReason = NULL;
  Result = ReplyBytes;
  /*
    Restore the interrupt status if necessary and return.
  */
Done:
  if (!HostAdapter->IRQ_ChannelAcquired)
    restore_flags(ProcessorFlags);
  return Result;
}


/*
  BusLogic_InitializeProbeInfoListISA initializes the list of I/O Address and
  Bus Probe Information to be checked for potential BusLogic SCSI Host Adapters
  only from the list of standard BusLogic MultiMaster ISA I/O Addresses.
*/

static inline void BusLogic_InitializeProbeInfoListISA(void)
{
  int StandardAddressIndex;
  /*
    If BusLogic_Setup has provided an I/O Address probe list, do not override
    the Kernel Command Line specifications.
  */
  if (BusLogic_ProbeInfoCount > 0) return;
  /*
    If a Kernel Command Line specification has requested that ISA Bus Probes
    be inhibited, do not proceed further.
  */
  if (BusLogic_ProbeOptions.Bits.NoProbeISA) return;
  /*
    Append the list of standard BusLogic MultiMaster ISA I/O Addresses.
  */
  StandardAddressIndex = 0;
  while (BusLogic_ProbeInfoCount < BusLogic_MaxHostAdapters &&
	 StandardAddressIndex < BusLogic_ISA_StandardAddressesCount)
    {
      BusLogic_ProbeInfo_T *ProbeInfo =
	&BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount++];
      ProbeInfo->IO_Address =
	BusLogic_ISA_StandardAddresses[StandardAddressIndex++];
      ProbeInfo->HostAdapterType = BusLogic_MultiMaster;
      ProbeInfo->HostAdapterBusType = BusLogic_ISA_Bus;
    }
}


#ifdef CONFIG_PCI


/*
  BusLogic_SortProbeInfo sorts a section of BusLogic_ProbeInfoList in order
  of increasing PCI Bus and Device Number.
*/

__initfunc(static void
BusLogic_SortProbeInfo(BusLogic_ProbeInfo_T *ProbeInfoList,
		       int ProbeInfoCount))
{
  int LastInterchange = ProbeInfoCount-1, Bound, j;
  while (LastInterchange > 0)
    {
      Bound = LastInterchange;
      LastInterchange = 0;
      for (j = 0; j < Bound; j++)
	{
	  BusLogic_ProbeInfo_T *ProbeInfo1 = &ProbeInfoList[j];
	  BusLogic_ProbeInfo_T *ProbeInfo2 = &ProbeInfoList[j+1];
	  if (ProbeInfo1->Bus > ProbeInfo2->Bus ||
	      (ProbeInfo1->Bus == ProbeInfo2->Bus &&
	       (ProbeInfo1->Device > ProbeInfo2->Device)))
	    {
	      BusLogic_ProbeInfo_T TempProbeInfo;
	      memcpy(&TempProbeInfo, ProbeInfo1, sizeof(BusLogic_ProbeInfo_T));
	      memcpy(ProbeInfo1, ProbeInfo2, sizeof(BusLogic_ProbeInfo_T));
	      memcpy(ProbeInfo2, &TempProbeInfo, sizeof(BusLogic_ProbeInfo_T));
	      LastInterchange = j;
	    }
	}
    }
}


/*
  BusLogic_InitializeMultiMasterProbeInfo initializes the list of I/O Address
  and Bus Probe Information to be checked for potential BusLogic MultiMaster
  SCSI Host Adapters by interrogating the PCI Configuration Space on PCI
  machines as well as from the list of standard BusLogic MultiMaster ISA
  I/O Addresses.  It returns the number of PCI MultiMaster Host Adapters found.
*/

__initfunc(static int BusLogic_InitializeMultiMasterProbeInfo(void))
{
  boolean StandardAddressSeen[BusLogic_ISA_StandardAddressesCount];
  BusLogic_ProbeInfo_T *PrimaryProbeInfo =
    &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount];
  int NonPrimaryPCIMultiMasterIndex = BusLogic_ProbeInfoCount;
  int NonPrimaryPCIMultiMasterCount = 0, PCIMultiMasterCount = 0;
  boolean ForceBusDeviceScanningOrder = false;
  boolean ForceBusDeviceScanningOrderChecked = false;
  unsigned char Bus, DeviceFunction, IRQ_Channel;
  unsigned int BaseAddress0, BaseAddress1;
  BusLogic_IO_Address_T IO_Address;
  BusLogic_PCI_Address_T PCI_Address;
  unsigned short Index = 0;
  int StandardAddressIndex, i;
  if (BusLogic_ProbeInfoCount >= BusLogic_MaxHostAdapters)
    return 0;
  BusLogic_ProbeInfoCount++;
  for (i = 0; i < BusLogic_ISA_StandardAddressesCount; i++)
    StandardAddressSeen[i] = false;
  /*
    Iterate over the MultiMaster PCI Host Adapters.  For each enumerated host
    adapter, determine whether its ISA Compatible I/O Port is enabled and if
    so, whether it is assigned the Primary I/O Address.  A host adapter that is
    assigned the Primary I/O Address will always be the preferred boot device.
    The MultiMaster BIOS will first recognize a host adapter at the Primary I/O
    Address, then any other PCI host adapters, and finally any host adapters
    located at the remaining standard ISA I/O Addresses.  When a PCI host
    adapter is found with its ISA Compatible I/O Port enabled, a command is
    issued to disable the ISA Compatible I/O Port, and it is noted that the
    particular standard ISA I/O Address need not be probed.
  */
  PrimaryProbeInfo->IO_Address = 0;
  while (pcibios_find_device(PCI_VENDOR_ID_BUSLOGIC,
			     PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER,
			     Index++, &Bus, &DeviceFunction) == 0)
    if (pcibios_read_config_dword(Bus, DeviceFunction,
				  PCI_BASE_ADDRESS_0, &BaseAddress0) == 0 &&
	pcibios_read_config_dword(Bus, DeviceFunction,
				  PCI_BASE_ADDRESS_1, &BaseAddress1) == 0 &&
	pcibios_read_config_byte(Bus, DeviceFunction,
				 PCI_INTERRUPT_LINE, &IRQ_Channel) == 0)
      {
	BusLogic_HostAdapter_T HostAdapterPrototype;
	BusLogic_HostAdapter_T *HostAdapter = &HostAdapterPrototype;
	BusLogic_PCIHostAdapterInformation_T PCIHostAdapterInformation;
	BusLogic_ModifyIOAddressRequest_T ModifyIOAddressRequest;
	unsigned char Device = DeviceFunction >> 3;
	IO_Address = BaseAddress0 & PCI_BASE_ADDRESS_IO_MASK;
	PCI_Address = BaseAddress1 & PCI_BASE_ADDRESS_MEM_MASK;
	if ((BaseAddress0 & PCI_BASE_ADDRESS_SPACE)
	    != PCI_BASE_ADDRESS_SPACE_IO)
	  {
	    BusLogic_Error("BusLogic: Base Address0 0x%X not I/O for "
			   "MultiMaster Host Adapter\n", NULL, BaseAddress0);
	    BusLogic_Error("at PCI Bus %d Device %d I/O Address 0x%X\n",
			   NULL, Bus, Device, IO_Address);
	    continue;
	  }
	if ((BaseAddress1 & PCI_BASE_ADDRESS_SPACE)
	    != PCI_BASE_ADDRESS_SPACE_MEMORY)
	  {
	    BusLogic_Error("BusLogic: Base Address1 0x%X not Memory for "
			   "MultiMaster Host Adapter\n", NULL, BaseAddress1);
	    BusLogic_Error("at PCI Bus %d Device %d PCI Address 0x%X\n",
			   NULL, Bus, Device, PCI_Address);
	    continue;
	  }
	if (IRQ_Channel == 0 || IRQ_Channel >= NR_IRQS)
	  {
	    BusLogic_Error("BusLogic: IRQ Channel %d illegal for "
			   "MultiMaster Host Adapter\n", NULL, IRQ_Channel);
	    BusLogic_Error("at PCI Bus %d Device %d I/O Address 0x%X\n",
			   NULL, Bus, Device, IO_Address);
	    continue;
	  }
	if (BusLogic_GlobalOptions.Bits.TraceProbe)
	  {
	    BusLogic_Notice("BusLogic: PCI MultiMaster Host Adapter "
			    "detected at\n", NULL);
	    BusLogic_Notice("BusLogic: PCI Bus %d Device %d I/O Address "
			    "0x%X PCI Address 0x%X\n", NULL,
			    Bus, Device, IO_Address, PCI_Address);
	  }
	/*
	  Issue the Inquire PCI Host Adapter Information command to determine
	  the ISA Compatible I/O Port.  If the ISA Compatible I/O Port is
	  known and enabled, note that the particular Standard ISA I/O
	  Address need not be probed.
	*/
	HostAdapter->IO_Address = IO_Address;
	if (BusLogic_Command(HostAdapter,
			     BusLogic_InquirePCIHostAdapterInformation,
			     NULL, 0, &PCIHostAdapterInformation,
			     sizeof(PCIHostAdapterInformation))
	    == sizeof(PCIHostAdapterInformation))
	  {
	    if (PCIHostAdapterInformation.ISACompatibleIOPort <
		BusLogic_ISA_StandardAddressesCount)
	      StandardAddressSeen[PCIHostAdapterInformation
				  .ISACompatibleIOPort] = true;
	  }
	else PCIHostAdapterInformation.ISACompatibleIOPort =
	       BusLogic_IO_Disable;
	/*
	  Issue the Modify I/O Address command to disable the ISA Compatible
	  I/O Port.
	*/
	ModifyIOAddressRequest = BusLogic_IO_Disable;
	BusLogic_Command(HostAdapter, BusLogic_ModifyIOAddress,
			 &ModifyIOAddressRequest,
			 sizeof(ModifyIOAddressRequest), NULL, 0);
	/*
	  For the first MultiMaster Host Adapter enumerated, issue the Fetch
	  Host Adapter Local RAM command to read byte 45 of the AutoSCSI area,
	  for the setting of the "Use Bus And Device # For PCI Scanning Seq."
	  option.  Issue the Inquire Board ID command since this option is
	  only valid for the BT-948/958/958D.
	*/
	if (!ForceBusDeviceScanningOrderChecked)
	  {
	    BusLogic_FetchHostAdapterLocalRAMRequest_T
	      FetchHostAdapterLocalRAMRequest;
	    BusLogic_AutoSCSIByte45_T AutoSCSIByte45;
	    BusLogic_BoardID_T BoardID;
	    FetchHostAdapterLocalRAMRequest.ByteOffset =
	      BusLogic_AutoSCSI_BaseOffset + 45;
	    FetchHostAdapterLocalRAMRequest.ByteCount =
	      sizeof(AutoSCSIByte45);
	    BusLogic_Command(HostAdapter,
			     BusLogic_FetchHostAdapterLocalRAM,
			     &FetchHostAdapterLocalRAMRequest,
			     sizeof(FetchHostAdapterLocalRAMRequest),
			     &AutoSCSIByte45, sizeof(AutoSCSIByte45));
	    BusLogic_Command(HostAdapter, BusLogic_InquireBoardID,
			     NULL, 0, &BoardID, sizeof(BoardID));
	    if (BoardID.FirmwareVersion1stDigit == '5')
	      ForceBusDeviceScanningOrder =
		AutoSCSIByte45.ForceBusDeviceScanningOrder;
	    ForceBusDeviceScanningOrderChecked = true;
	  }
	/*
	  Determine whether this MultiMaster Host Adapter has its ISA
	  Compatible I/O Port enabled and is assigned the Primary I/O Address.
	  If it does, then it is the Primary MultiMaster Host Adapter and must
	  be recognized first.  If it does not, then it is added to the list
	  for probing after any Primary MultiMaster Host Adapter is probed.
	*/
	if (PCIHostAdapterInformation.ISACompatibleIOPort == BusLogic_IO_330)
	  {
	    PrimaryProbeInfo->IO_Address = IO_Address;
	    PrimaryProbeInfo->PCI_Address = PCI_Address;
	    PrimaryProbeInfo->HostAdapterType = BusLogic_MultiMaster;
	    PrimaryProbeInfo->HostAdapterBusType = BusLogic_PCI_Bus;
	    PrimaryProbeInfo->Bus = Bus;
	    PrimaryProbeInfo->Device = Device;
	    PrimaryProbeInfo->IRQ_Channel = IRQ_Channel;
	    PCIMultiMasterCount++;
	  }
	else if (BusLogic_ProbeInfoCount < BusLogic_MaxHostAdapters)
	  {
	    BusLogic_ProbeInfo_T *ProbeInfo =
	      &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount++];
	    ProbeInfo->IO_Address = IO_Address;
	    ProbeInfo->PCI_Address = PCI_Address;
	    ProbeInfo->HostAdapterType = BusLogic_MultiMaster;
	    ProbeInfo->HostAdapterBusType = BusLogic_PCI_Bus;
	    ProbeInfo->Bus = Bus;
	    ProbeInfo->Device = Device;
	    ProbeInfo->IRQ_Channel = IRQ_Channel;
	    NonPrimaryPCIMultiMasterCount++;
	    PCIMultiMasterCount++;
	  }
	else BusLogic_Warning("BusLogic: Too many Host Adapters "
			      "detected\n", NULL);
      }
  /*
    If the AutoSCSI "Use Bus And Device # For PCI Scanning Seq." option is ON
    for the first enumerated MultiMaster Host Adapter, and if that host adapter
    is a BT-948/958/958D, then the MultiMaster BIOS will recognize MultiMaster
    Host Adapters in the order of increasing PCI Bus and Device Number.  In
    that case, sort the probe information into the same order the BIOS uses.
    If this option is OFF, then the MultiMaster BIOS will recognize MultiMaster
    Host Adapters in the order they are enumerated by the PCI BIOS, and hence
    no sorting is necessary.
  */
  if (ForceBusDeviceScanningOrder)
    BusLogic_SortProbeInfo(&BusLogic_ProbeInfoList[
			      NonPrimaryPCIMultiMasterIndex],
			   NonPrimaryPCIMultiMasterCount);
  /*
    If no PCI MultiMaster Host Adapter is assigned the Primary I/O Address,
    then the Primary I/O Address must be probed explicitly before any PCI
    host adapters are probed.
  */
  if (PrimaryProbeInfo->IO_Address == 0 &&
      !BusLogic_ProbeOptions.Bits.NoProbeISA)
    {
      PrimaryProbeInfo->IO_Address = BusLogic_ISA_StandardAddresses[0];
      PrimaryProbeInfo->HostAdapterType = BusLogic_MultiMaster;
      PrimaryProbeInfo->HostAdapterBusType = BusLogic_ISA_Bus;
    }
  /*
    Append the list of standard BusLogic MultiMaster ISA I/O Addresses,
    omitting the Primary I/O Address which has already been handled.
  */
  if (!BusLogic_ProbeOptions.Bits.NoProbeISA)
    for (StandardAddressIndex = 1;
	 StandardAddressIndex < BusLogic_ISA_StandardAddressesCount;
	 StandardAddressIndex++)
      if (!StandardAddressSeen[StandardAddressIndex] &&
	  BusLogic_ProbeInfoCount < BusLogic_MaxHostAdapters)
	{
	  BusLogic_ProbeInfo_T *ProbeInfo =
	    &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount++];
	  ProbeInfo->IO_Address =
	    BusLogic_ISA_StandardAddresses[StandardAddressIndex];
	  ProbeInfo->HostAdapterType = BusLogic_MultiMaster;
	  ProbeInfo->HostAdapterBusType = BusLogic_ISA_Bus;
	}
  return PCIMultiMasterCount;
}


/*
  BusLogic_InitializeFlashPointProbeInfo initializes the list of I/O Address
  and Bus Probe Information to be checked for potential BusLogic FlashPoint
  Host Adapters by interrogating the PCI Configuration Space.  It returns the
  number of FlashPoint Host Adapters found.
*/

__initfunc(static int BusLogic_InitializeFlashPointProbeInfo(void))
{
  int FlashPointIndex = BusLogic_ProbeInfoCount, FlashPointCount = 0;
  unsigned char Bus, DeviceFunction, IRQ_Channel;
  unsigned int BaseAddress0, BaseAddress1;
  BusLogic_IO_Address_T IO_Address;
  BusLogic_PCI_Address_T PCI_Address;
  unsigned short Index = 0;
  /*
    Interrogate PCI Configuration Space for any FlashPoint Host Adapters.
  */
  while (pcibios_find_device(PCI_VENDOR_ID_BUSLOGIC,
			     PCI_DEVICE_ID_BUSLOGIC_FLASHPOINT,
			     Index++, &Bus, &DeviceFunction) == 0)
    if (pcibios_read_config_dword(Bus, DeviceFunction,
				  PCI_BASE_ADDRESS_0, &BaseAddress0) == 0 &&
	pcibios_read_config_dword(Bus, DeviceFunction,
				  PCI_BASE_ADDRESS_1, &BaseAddress1) == 0 &&
	pcibios_read_config_byte(Bus, DeviceFunction,
				 PCI_INTERRUPT_LINE, &IRQ_Channel) == 0)
      {
	unsigned char Device = DeviceFunction >> 3;
	IO_Address = BaseAddress0 & PCI_BASE_ADDRESS_IO_MASK;
	PCI_Address = BaseAddress1 & PCI_BASE_ADDRESS_MEM_MASK;
#ifndef CONFIG_SCSI_OMIT_FLASHPOINT
	if ((BaseAddress0 & PCI_BASE_ADDRESS_SPACE)
	    != PCI_BASE_ADDRESS_SPACE_IO)
	  {
	    BusLogic_Error("BusLogic: Base Address0 0x%X not I/O for "
			   "FlashPoint Host Adapter\n", NULL, BaseAddress0);
	    BusLogic_Error("at PCI Bus %d Device %d I/O Address 0x%X\n",
			   NULL, Bus, Device, IO_Address);
	    continue;
	  }
	if ((BaseAddress1 & PCI_BASE_ADDRESS_SPACE)
	    != PCI_BASE_ADDRESS_SPACE_MEMORY)
	  {
	    BusLogic_Error("BusLogic: Base Address1 0x%X not Memory for "
			   "FlashPoint Host Adapter\n", NULL, BaseAddress1);
	    BusLogic_Error("at PCI Bus %d Device %d PCI Address 0x%X\n",
			   NULL, Bus, Device, PCI_Address);
	    continue;
	  }
	if (IRQ_Channel == 0 || IRQ_Channel >= NR_IRQS)
	  {
	    BusLogic_Error("BusLogic: IRQ Channel %d illegal for "
			   "FlashPoint Host Adapter\n", NULL, IRQ_Channel);
	    BusLogic_Error("at PCI Bus %d Device %d I/O Address 0x%X\n",
			   NULL, Bus, Device, IO_Address);
	    continue;
	  }
	if (BusLogic_GlobalOptions.Bits.TraceProbe)
	  {
	    BusLogic_Notice("BusLogic: FlashPoint Host Adapter "
			    "detected at\n", NULL);
	    BusLogic_Notice("BusLogic: PCI Bus %d Device %d I/O Address "
			    "0x%X PCI Address 0x%X\n", NULL,
			    Bus, Device, IO_Address, PCI_Address);
	  }
	if (BusLogic_ProbeInfoCount < BusLogic_MaxHostAdapters)
	  {
	    BusLogic_ProbeInfo_T *ProbeInfo =
	      &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount++];
	    ProbeInfo->IO_Address = IO_Address;
	    ProbeInfo->PCI_Address = PCI_Address;
	    ProbeInfo->HostAdapterType = BusLogic_FlashPoint;
	    ProbeInfo->HostAdapterBusType = BusLogic_PCI_Bus;
	    ProbeInfo->Bus = Bus;
	    ProbeInfo->Device = Device;
	    ProbeInfo->IRQ_Channel = IRQ_Channel;
	    FlashPointCount++;
	  }
	else BusLogic_Warning("BusLogic: Too many Host Adapters "
			      "detected\n", NULL);
#else
	BusLogic_Error("BusLogic: FlashPoint Host Adapter detected at "
		       "PCI Bus %d Device %d\n", NULL, Bus, Device);
	BusLogic_Error("BusLogic: I/O Address 0x%X PCI Address 0x%X, "
		       "but FlashPoint\n", NULL, IO_Address, PCI_Address);
	BusLogic_Error("BusLogic: support was omitted in this kernel "
		       "configuration.\n", NULL);
#endif
      }
  /*
    The FlashPoint BIOS will scan for FlashPoint Host Adapters in the order of
    increasing PCI Bus and Device Number, so sort the probe information into
    the same order the BIOS uses.
  */
  BusLogic_SortProbeInfo(&BusLogic_ProbeInfoList[FlashPointIndex],
			 FlashPointCount);
  return FlashPointCount;
}


/*
  BusLogic_InitializeProbeInfoList initializes the list of I/O Address and Bus
  Probe Information to be checked for potential BusLogic SCSI Host Adapters by
  interrogating the PCI Configuration Space on PCI machines as well as from the
  list of standard BusLogic MultiMaster ISA I/O Addresses.  By default, if both
  FlashPoint and PCI MultiMaster Host Adapters are present, this driver will
  probe for FlashPoint Host Adapters first unless the BIOS primary disk is
  controlled by the first PCI MultiMaster Host Adapter, in which case
  MultiMaster Host Adapters will be probed first.  The Kernel Command Line
  options "MultiMasterFirst" and "FlashPointFirst" can be used to force a
  particular probe order.
*/

static inline void BusLogic_InitializeProbeInfoList(void)
{
  /*
    If BusLogic_Setup has provided an I/O Address probe list, do not override
    the Kernel Command Line specifications.
  */
  if (BusLogic_ProbeInfoCount > 0) return;
  /*
    If a PCI BIOS is present, interrogate it for MultiMaster and FlashPoint
    Host Adapters; otherwise, default to the standard ISA MultiMaster probe.
  */
  if (!BusLogic_ProbeOptions.Bits.NoProbePCI && pcibios_present())
    {
      if (BusLogic_ProbeOptions.Bits.ProbeMultiMasterFirst)
	{
	  BusLogic_InitializeMultiMasterProbeInfo();
	  BusLogic_InitializeFlashPointProbeInfo();
	}
      else if (BusLogic_ProbeOptions.Bits.ProbeFlashPointFirst)
	{
	  BusLogic_InitializeFlashPointProbeInfo();
	  BusLogic_InitializeMultiMasterProbeInfo();
	}
      else
	{
	  int FlashPointCount = BusLogic_InitializeFlashPointProbeInfo();
	  int PCIMultiMasterCount = BusLogic_InitializeMultiMasterProbeInfo();
	  if (FlashPointCount > 0 && PCIMultiMasterCount > 0)
	    {
	      BusLogic_ProbeInfo_T *ProbeInfo =
		&BusLogic_ProbeInfoList[FlashPointCount];
	      BusLogic_HostAdapter_T HostAdapterPrototype;
	      BusLogic_HostAdapter_T *HostAdapter = &HostAdapterPrototype;
	      BusLogic_FetchHostAdapterLocalRAMRequest_T
		FetchHostAdapterLocalRAMRequest;
	      BusLogic_BIOSDriveMapByte_T Drive0MapByte;
	      while (ProbeInfo->HostAdapterBusType != BusLogic_PCI_Bus)
		ProbeInfo++;
	      HostAdapter->IO_Address = ProbeInfo->IO_Address;
	      FetchHostAdapterLocalRAMRequest.ByteOffset =
		BusLogic_BIOS_BaseOffset + BusLogic_BIOS_DriveMapOffset + 0;
	      FetchHostAdapterLocalRAMRequest.ByteCount =
		sizeof(Drive0MapByte);
	      BusLogic_Command(HostAdapter,
			       BusLogic_FetchHostAdapterLocalRAM,
			       &FetchHostAdapterLocalRAMRequest,
			       sizeof(FetchHostAdapterLocalRAMRequest),
			       &Drive0MapByte, sizeof(Drive0MapByte));
	      /*
		If the Map Byte for BIOS Drive 0 indicates that BIOS Drive 0
		is controlled by this PCI MultiMaster Host Adapter, then
		reverse the probe order so that MultiMaster Host Adapters are
		probed before FlashPoint Host Adapters.
	      */
	      if (Drive0MapByte.DiskGeometry !=
		  BusLogic_BIOS_Disk_Not_Installed)
		{
		  BusLogic_ProbeInfo_T
		    SavedProbeInfo[BusLogic_MaxHostAdapters];
		  int MultiMasterCount =
		    BusLogic_ProbeInfoCount - FlashPointCount;
		  memcpy(SavedProbeInfo,
			 BusLogic_ProbeInfoList,
			 sizeof(BusLogic_ProbeInfoList));
		  memcpy(&BusLogic_ProbeInfoList[0],
			 &SavedProbeInfo[FlashPointCount],
			 MultiMasterCount * sizeof(BusLogic_ProbeInfo_T));
		  memcpy(&BusLogic_ProbeInfoList[MultiMasterCount],
			 &SavedProbeInfo[0],
			 FlashPointCount * sizeof(BusLogic_ProbeInfo_T));
		}
	    }
	}
    }
  else BusLogic_InitializeProbeInfoListISA();
}


#endif  /* CONFIG_PCI */


/*
  BusLogic_Failure prints a standardized error message, and then returns false.
*/

static boolean BusLogic_Failure(BusLogic_HostAdapter_T *HostAdapter,
				char *ErrorMessage)
{
  BusLogic_AnnounceDriver(HostAdapter);
  if (HostAdapter->HostAdapterBusType == BusLogic_PCI_Bus)
    BusLogic_Error("While configuring BusLogic PCI Host Adapter at\n"
		   "Bus %d Device %d I/O Address 0x%X PCI Address 0x%X:\n",
		   HostAdapter, HostAdapter->Bus, HostAdapter->Device,
		   HostAdapter->IO_Address, HostAdapter->PCI_Address);
  else BusLogic_Error("While configuring BusLogic Host Adapter at "
		      "I/O Address 0x%X:\n", HostAdapter,
		      HostAdapter->IO_Address);
  BusLogic_Error("%s FAILED - DETACHING\n", HostAdapter, ErrorMessage);
  if (BusLogic_CommandFailureReason != NULL)
    BusLogic_Error("ADDITIONAL FAILURE INFO - %s\n", HostAdapter,
		   BusLogic_CommandFailureReason);
  return false;
}


/*
  BusLogic_ProbeHostAdapter probes for a BusLogic Host Adapter.
*/

__initfunc(static boolean
BusLogic_ProbeHostAdapter(BusLogic_HostAdapter_T *HostAdapter))
{
  BusLogic_StatusRegister_T StatusRegister;
  BusLogic_InterruptRegister_T InterruptRegister;
  BusLogic_GeometryRegister_T GeometryRegister;
  /*
    FlashPoint Host Adapters are Probed by the FlashPoint SCCB Manager.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter))
    {
      FlashPoint_Info_T *FlashPointInfo = (FlashPoint_Info_T *)
	scsi_init_malloc(sizeof(FlashPoint_Info_T), GFP_ATOMIC);
      if (FlashPointInfo == NULL)
	return BusLogic_Failure(HostAdapter, "ALLOCATING FLASHPOINT INFO");
      FlashPointInfo->BaseAddress = HostAdapter->IO_Address;
      FlashPointInfo->IRQ_Channel = HostAdapter->IRQ_Channel;
      FlashPointInfo->Present = false;
      if (!(FlashPoint_ProbeHostAdapter(FlashPointInfo) == 0 &&
	    FlashPointInfo->Present))
	{
	  scsi_init_free((char *) FlashPointInfo, sizeof(FlashPoint_Info_T));
	  BusLogic_Error("BusLogic: FlashPoint Host Adapter detected at "
			 "PCI Bus %d Device %d\n", HostAdapter,
			 HostAdapter->Bus, HostAdapter->Device);
	  BusLogic_Error("BusLogic: I/O Address 0x%X PCI Address 0x%X, "
			 "but FlashPoint\n", HostAdapter,
			 HostAdapter->IO_Address, HostAdapter->PCI_Address);
	  BusLogic_Error("BusLogic: Probe Function failed to validate it.\n",
			 HostAdapter);
	  return false;
	}
      HostAdapter->FlashPointInfo = FlashPointInfo;
      if (BusLogic_GlobalOptions.Bits.TraceProbe)
	BusLogic_Notice("BusLogic_Probe(0x%X): FlashPoint Found\n",
			HostAdapter, HostAdapter->IO_Address);
      /*
	Indicate the Host Adapter Probe completed successfully.
      */
      return true;
    }
  /*
    Read the Status, Interrupt, and Geometry Registers to test if there are I/O
    ports that respond, and to check the values to determine if they are from a
    BusLogic Host Adapter.  A nonexistent I/O port will return 0xFF, in which
    case there is definitely no BusLogic Host Adapter at this base I/O Address.
    The test here is a subset of that used by the BusLogic Host Adapter BIOS.
  */
  StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
  InterruptRegister.All = BusLogic_ReadInterruptRegister(HostAdapter);
  GeometryRegister.All = BusLogic_ReadGeometryRegister(HostAdapter);
  if (BusLogic_GlobalOptions.Bits.TraceProbe)
    BusLogic_Notice("BusLogic_Probe(0x%X): Status 0x%02X, Interrupt 0x%02X, "
		    "Geometry 0x%02X\n", HostAdapter,
		    HostAdapter->IO_Address, StatusRegister.All,
		    InterruptRegister.All, GeometryRegister.All);
  if (StatusRegister.All == 0 ||
      StatusRegister.Bits.DiagnosticActive ||
      StatusRegister.Bits.CommandParameterRegisterBusy ||
      StatusRegister.Bits.Reserved ||
      StatusRegister.Bits.CommandInvalid ||
      InterruptRegister.Bits.Reserved != 0)
    return false;
  /*
    Check the undocumented Geometry Register to test if there is an I/O port
    that responded.  Adaptec Host Adapters do not implement the Geometry
    Register, so this test helps serve to avoid incorrectly recognizing an
    Adaptec 1542A or 1542B as a BusLogic.  Unfortunately, the Adaptec 1542C
    series does respond to the Geometry Register I/O port, but it will be
    rejected later when the Inquire Extended Setup Information command is
    issued in BusLogic_CheckHostAdapter.  The AMI FastDisk Host Adapter is a
    BusLogic clone that implements the same interface as earlier BusLogic
    Host Adapters, including the undocumented commands, and is therefore
    supported by this driver.  However, the AMI FastDisk always returns 0x00
    upon reading the Geometry Register, so the extended translation option
    should always be left disabled on the AMI FastDisk.
  */
  if (GeometryRegister.All == 0xFF) return false;
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
  BusLogic_StatusRegister_T StatusRegister;
  int TimeoutCounter = loops_per_sec;
  /*
    FlashPoint Host Adapters are Hard Reset by the FlashPoint SCCB Manager.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter))
    {
      HostAdapter->FlashPointInfo->ReportDataUnderrun = true;
      HostAdapter->CardHandle =
	FlashPoint_HardResetHostAdapter(HostAdapter->FlashPointInfo);
      if (HostAdapter->CardHandle == FlashPoint_BadCardHandle) return false;
      /*
	Indicate the Host Adapter Hard Reset completed successfully.
      */
      return true;
    }
  /*
    Issue a Hard Reset Command to the Host Adapter.  The Host Adapter should
    respond by setting Diagnostic Active in the Status Register.
  */
  BusLogic_HardReset(HostAdapter);
  /*
    Wait until Diagnostic Active is set in the Status Register.
  */
  while (--TimeoutCounter >= 0)
    {
      StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
      if (StatusRegister.Bits.DiagnosticActive) break;
    }
  if (BusLogic_GlobalOptions.Bits.TraceHardReset)
    BusLogic_Notice("BusLogic_HardReset(0x%X): Diagnostic Active, "
		    "Status 0x%02X\n", HostAdapter,
		    HostAdapter->IO_Address, StatusRegister.All);
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
      StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
      if (!StatusRegister.Bits.DiagnosticActive) break;
    }
  if (BusLogic_GlobalOptions.Bits.TraceHardReset)
    BusLogic_Notice("BusLogic_HardReset(0x%X): Diagnostic Completed, "
		    "Status 0x%02X\n", HostAdapter,
		    HostAdapter->IO_Address, StatusRegister.All);
  if (TimeoutCounter < 0) return false;
  /*
    Wait until at least one of the Diagnostic Failure, Host Adapter Ready,
    or Data In Register Ready bits is set in the Status Register.
  */
  while (--TimeoutCounter >= 0)
    {
      StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
      if (StatusRegister.Bits.DiagnosticFailure ||
	  StatusRegister.Bits.HostAdapterReady ||
	  StatusRegister.Bits.DataInRegisterReady)
	break;
    }
  if (BusLogic_GlobalOptions.Bits.TraceHardReset)
    BusLogic_Notice("BusLogic_HardReset(0x%X): Host Adapter Ready, "
		    "Status 0x%02X\n", HostAdapter,
		    HostAdapter->IO_Address, StatusRegister.All);
  if (TimeoutCounter < 0) return false;
  /*
    If Diagnostic Failure is set or Host Adapter Ready is reset, then an
    error occurred during the Host Adapter diagnostics.  If Data In Register
    Ready is set, then there is an Error Code available.
  */
  if (StatusRegister.Bits.DiagnosticFailure ||
      !StatusRegister.Bits.HostAdapterReady)
    {
      BusLogic_CommandFailureReason = NULL;
      BusLogic_Failure(HostAdapter, "HARD RESET DIAGNOSTICS");
      BusLogic_Error("HOST ADAPTER STATUS REGISTER = %02X\n",
		     HostAdapter, StatusRegister.All);
      if (StatusRegister.Bits.DataInRegisterReady)
	{
	  unsigned char ErrorCode = BusLogic_ReadDataInRegister(HostAdapter);
	  BusLogic_Error("HOST ADAPTER ERROR CODE = %d\n",
			 HostAdapter, ErrorCode);
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
  Host Adapter.  It also determines the IRQ Channel for non-PCI Host Adapters.
*/

__initfunc(static boolean
BusLogic_CheckHostAdapter(BusLogic_HostAdapter_T *HostAdapter))
{
  BusLogic_Configuration_T Configuration;
  BusLogic_ExtendedSetupInformation_T ExtendedSetupInformation;
  BusLogic_RequestedReplyLength_T RequestedReplyLength;
  boolean Result = true;
  /*
    FlashPoint Host Adapters do not require this protection.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter)) return true;
  /*
    Issue the Inquire Configuration command if the IRQ Channel is unknown.
  */
  if (HostAdapter->IRQ_Channel == 0)
    if (BusLogic_Command(HostAdapter, BusLogic_InquireConfiguration,
			 NULL, 0, &Configuration, sizeof(Configuration))
	== sizeof(Configuration))
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
	else Result = false;
      }
    else Result = false;
  /*
    Issue the Inquire Extended Setup Information command.  Only genuine
    BusLogic Host Adapters and true clones support this command.  Adaptec 1542C
    series Host Adapters that respond to the Geometry Register I/O port will
    fail this command.
  */
  RequestedReplyLength = sizeof(ExtendedSetupInformation);
  if (BusLogic_Command(HostAdapter,
		       BusLogic_InquireExtendedSetupInformation,
		       &RequestedReplyLength,
		       sizeof(RequestedReplyLength),
		       &ExtendedSetupInformation,
		       sizeof(ExtendedSetupInformation))
      != sizeof(ExtendedSetupInformation))
    Result = false;
  /*
    Provide tracing information if requested and return.
  */
  if (BusLogic_GlobalOptions.Bits.TraceProbe)
    BusLogic_Notice("BusLogic_Check(0x%X): MultiMaster %s\n", HostAdapter,
		    HostAdapter->IO_Address, (Result ? "Found" : "Not Found"));
  return Result;
}


/*
  BusLogic_ReadHostAdapterConfiguration reads the Configuration Information
  from Host Adapter and initializes the Host Adapter structure.
*/

__initfunc(static boolean
BusLogic_ReadHostAdapterConfiguration(BusLogic_HostAdapter_T *HostAdapter))
{
  BusLogic_BoardID_T BoardID;
  BusLogic_Configuration_T Configuration;
  BusLogic_SetupInformation_T SetupInformation;
  BusLogic_ExtendedSetupInformation_T ExtendedSetupInformation;
  BusLogic_HostAdapterModelNumber_T HostAdapterModelNumber;
  BusLogic_FirmwareVersion3rdDigit_T FirmwareVersion3rdDigit;
  BusLogic_FirmwareVersionLetter_T FirmwareVersionLetter;
  BusLogic_PCIHostAdapterInformation_T PCIHostAdapterInformation;
  BusLogic_FetchHostAdapterLocalRAMRequest_T FetchHostAdapterLocalRAMRequest;
  BusLogic_AutoSCSIData_T AutoSCSIData;
  BusLogic_GeometryRegister_T GeometryRegister;
  BusLogic_RequestedReplyLength_T RequestedReplyLength;
  unsigned char *TargetPointer, Character;
  int i;
  /*
    Configuration Information for FlashPoint Host Adapters is provided in the
    FlashPoint_Info structure by the FlashPoint SCCB Manager's Probe Function.
    Initialize fields in the Host Adapter structure from the FlashPoint_Info
    structure.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter))
    {
      FlashPoint_Info_T *FlashPointInfo = HostAdapter->FlashPointInfo;
      TargetPointer = HostAdapter->ModelName;
      *TargetPointer++ = 'B';
      *TargetPointer++ = 'T';
      *TargetPointer++ = '-';
      for (i = 0; i < sizeof(FlashPointInfo->ModelNumber); i++)
	*TargetPointer++ = FlashPointInfo->ModelNumber[i];
      *TargetPointer++ = '\0';
      strcpy(HostAdapter->FirmwareVersion, FlashPoint_FirmwareVersion);
      HostAdapter->SCSI_ID = FlashPointInfo->SCSI_ID;
      HostAdapter->ExtendedTranslationEnabled =
	FlashPointInfo->ExtendedTranslationEnabled;
      HostAdapter->ParityCheckingEnabled =
	FlashPointInfo->ParityCheckingEnabled;
      HostAdapter->BusResetEnabled = !FlashPointInfo->HostSoftReset;
      HostAdapter->LevelSensitiveInterrupt = true;
      HostAdapter->HostWideSCSI = FlashPointInfo->HostWideSCSI;
      HostAdapter->HostDifferentialSCSI = false;
      HostAdapter->HostSupportsSCAM = true;
      HostAdapter->HostUltraSCSI = true;
      HostAdapter->ExtendedLUNSupport = true;
      HostAdapter->TerminationInfoValid = true;
      HostAdapter->LowByteTerminated = FlashPointInfo->LowByteTerminated;
      HostAdapter->HighByteTerminated = FlashPointInfo->HighByteTerminated;
      HostAdapter->SCAM_Enabled = FlashPointInfo->SCAM_Enabled;
      HostAdapter->SCAM_Level2 = FlashPointInfo->SCAM_Level2;
      HostAdapter->DriverScatterGatherLimit = BusLogic_ScatterGatherLimit;
      HostAdapter->MaxTargetDevices = (HostAdapter->HostWideSCSI ? 16 : 8);
      HostAdapter->MaxLogicalUnits = 32;
      HostAdapter->InitialCCBs = 64;
      HostAdapter->IncrementalCCBs = 16;
      HostAdapter->DriverQueueDepth = 255;
      HostAdapter->HostAdapterQueueDepth = HostAdapter->DriverQueueDepth;
      HostAdapter->SynchronousPermitted = FlashPointInfo->SynchronousPermitted;
      HostAdapter->FastPermitted = FlashPointInfo->FastPermitted;
      HostAdapter->UltraPermitted = FlashPointInfo->UltraPermitted;
      HostAdapter->WidePermitted = FlashPointInfo->WidePermitted;
      HostAdapter->DisconnectPermitted = FlashPointInfo->DisconnectPermitted;
      HostAdapter->TaggedQueuingPermitted = 0xFFFF;
      goto Common;
    }
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
    Issue the Inquire Firmware Version 3rd Digit command.
  */
  FirmwareVersion3rdDigit = '\0';
  if (BoardID.FirmwareVersion1stDigit > '0')
    if (BusLogic_Command(HostAdapter, BusLogic_InquireFirmwareVersion3rdDigit,
			 NULL, 0, &FirmwareVersion3rdDigit,
			 sizeof(FirmwareVersion3rdDigit))
	!= sizeof(FirmwareVersion3rdDigit))
      return BusLogic_Failure(HostAdapter, "INQUIRE FIRMWARE 3RD DIGIT");
  /*
    Issue the Inquire Host Adapter Model Number command.
  */
  if (ExtendedSetupInformation.BusType == 'A' &&
      BoardID.FirmwareVersion1stDigit == '2')
    /* BusLogic BT-542B ISA 2.xx */
    strcpy(HostAdapterModelNumber, "542B");
  else if (ExtendedSetupInformation.BusType == 'E' &&
	   BoardID.FirmwareVersion1stDigit == '2' &&
	   (BoardID.FirmwareVersion2ndDigit <= '1' ||
	    (BoardID.FirmwareVersion2ndDigit == '2' &&
	     FirmwareVersion3rdDigit == '0')))
    /* BusLogic BT-742A EISA 2.1x or 2.20 */
    strcpy(HostAdapterModelNumber, "742A");
  else if (ExtendedSetupInformation.BusType == 'E' &&
	   BoardID.FirmwareVersion1stDigit == '0')
    /* AMI FastDisk EISA Series 441 0.x */
    strcpy(HostAdapterModelNumber, "747A");
  else
    {
      RequestedReplyLength = sizeof(HostAdapterModelNumber);
      if (BusLogic_Command(HostAdapter, BusLogic_InquireHostAdapterModelNumber,
			   &RequestedReplyLength, sizeof(RequestedReplyLength),
			   &HostAdapterModelNumber,
			   sizeof(HostAdapterModelNumber))
	  != sizeof(HostAdapterModelNumber))
	return BusLogic_Failure(HostAdapter,
				"INQUIRE HOST ADAPTER MODEL NUMBER");
    }
  /*
    BusLogic MultiMaster Host Adapters can be identified by their model number
    and the major version number of their firmware as follows:

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
    Save the Model Name and Host Adapter Name in the Host Adapter structure.
  */
  TargetPointer = HostAdapter->ModelName;
  *TargetPointer++ = 'B';
  *TargetPointer++ = 'T';
  *TargetPointer++ = '-';
  for (i = 0; i < sizeof(HostAdapterModelNumber); i++)
    {
      Character = HostAdapterModelNumber[i];
      if (Character == ' ' || Character == '\0') break;
      *TargetPointer++ = Character;
    }
  *TargetPointer++ = '\0';
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
	return BusLogic_Failure(HostAdapter,
				"INQUIRE FIRMWARE VERSION LETTER");
      if (FirmwareVersionLetter != ' ' && FirmwareVersionLetter != '\0')
	*TargetPointer++ = FirmwareVersionLetter;
      *TargetPointer = '\0';
    }
  /*
    Save the Host Adapter SCSI ID in the Host Adapter structure.
  */
  HostAdapter->SCSI_ID = Configuration.HostAdapterID;
  /*
    Determine the Bus Type and save it in the Host Adapter structure,
    and determine and save the DMA Channel for ISA Host Adapters.
  */
  HostAdapter->HostAdapterBusType =
    BusLogic_HostAdapterBusTypes[HostAdapter->ModelName[3] - '4'];
  if (HostAdapter->HostAdapterBusType == BusLogic_ISA_Bus)
    if (Configuration.DMA_Channel5)
      HostAdapter->DMA_Channel = 5;
    else if (Configuration.DMA_Channel6)
      HostAdapter->DMA_Channel = 6;
    else if (Configuration.DMA_Channel7)
      HostAdapter->DMA_Channel = 7;
  /*
    Determine whether Extended Translation is enabled and save it in
    the Host Adapter structure.
  */
  GeometryRegister.All = BusLogic_ReadGeometryRegister(HostAdapter);
  HostAdapter->ExtendedTranslationEnabled =
    GeometryRegister.Bits.ExtendedTranslationEnabled;
  /*
    Save the Scatter Gather Limits, Level Sensitive Interrupt flag, Wide
    SCSI flag, Differential SCSI flag, SCAM Supported flag, and
    Ultra SCSI flag in the Host Adapter structure.
  */
  HostAdapter->HostAdapterScatterGatherLimit =
    ExtendedSetupInformation.ScatterGatherLimit;
  HostAdapter->DriverScatterGatherLimit =
    HostAdapter->HostAdapterScatterGatherLimit;
  if (HostAdapter->HostAdapterScatterGatherLimit > BusLogic_ScatterGatherLimit)
    HostAdapter->DriverScatterGatherLimit = BusLogic_ScatterGatherLimit;
  if (ExtendedSetupInformation.Misc.LevelSensitiveInterrupt)
    HostAdapter->LevelSensitiveInterrupt = true;
  HostAdapter->HostWideSCSI = ExtendedSetupInformation.HostWideSCSI;
  HostAdapter->HostDifferentialSCSI =
    ExtendedSetupInformation.HostDifferentialSCSI;
  HostAdapter->HostSupportsSCAM = ExtendedSetupInformation.HostSupportsSCAM;
  HostAdapter->HostUltraSCSI = ExtendedSetupInformation.HostUltraSCSI;
  /*
    Determine whether Extended LUN Format CCBs are supported and save the
    information in the Host Adapter structure.
  */
  if (HostAdapter->FirmwareVersion[0] == '5' ||
      (HostAdapter->FirmwareVersion[0] == '4' && HostAdapter->HostWideSCSI))
    HostAdapter->ExtendedLUNSupport = true;
  /*
    Issue the Inquire PCI Host Adapter Information command to read the
    Termination Information from "W" series MultiMaster Host Adapters.
  */
  if (HostAdapter->FirmwareVersion[0] == '5')
    {
      if (BusLogic_Command(HostAdapter,
			   BusLogic_InquirePCIHostAdapterInformation,
			   NULL, 0, &PCIHostAdapterInformation,
			   sizeof(PCIHostAdapterInformation))
	  != sizeof(PCIHostAdapterInformation))
	return BusLogic_Failure(HostAdapter,
				"INQUIRE PCI HOST ADAPTER INFORMATION");
      /*
	Save the Termination Information in the Host Adapter structure.
      */
      if (PCIHostAdapterInformation.GenericInfoValid)
	{
	  HostAdapter->TerminationInfoValid = true;
	  HostAdapter->LowByteTerminated =
	    PCIHostAdapterInformation.LowByteTerminated;
	  HostAdapter->HighByteTerminated =
	    PCIHostAdapterInformation.HighByteTerminated;
	}
    }
  /*
    Issue the Fetch Host Adapter Local RAM command to read the AutoSCSI data
    from "W" and "C" series MultiMaster Host Adapters.
  */
  if (HostAdapter->FirmwareVersion[0] >= '4')
    {
      FetchHostAdapterLocalRAMRequest.ByteOffset =
	BusLogic_AutoSCSI_BaseOffset;
      FetchHostAdapterLocalRAMRequest.ByteCount = sizeof(AutoSCSIData);
      if (BusLogic_Command(HostAdapter,
			   BusLogic_FetchHostAdapterLocalRAM,
			   &FetchHostAdapterLocalRAMRequest,
			   sizeof(FetchHostAdapterLocalRAMRequest),
			   &AutoSCSIData, sizeof(AutoSCSIData))
	  != sizeof(AutoSCSIData))
	return BusLogic_Failure(HostAdapter, "FETCH HOST ADAPTER LOCAL RAM");
      /*
	Save the Parity Checking Enabled, Bus Reset Enabled, and Termination
	Information in the Host Adapter structure.
      */
      HostAdapter->ParityCheckingEnabled = AutoSCSIData.ParityCheckingEnabled;
      HostAdapter->BusResetEnabled = AutoSCSIData.BusResetEnabled;
      if (HostAdapter->FirmwareVersion[0] == '4')
	{
	  HostAdapter->TerminationInfoValid = true;
	  HostAdapter->LowByteTerminated = AutoSCSIData.LowByteTerminated;
	  HostAdapter->HighByteTerminated = AutoSCSIData.HighByteTerminated;
	}
      /*
	Save the Wide Permitted, Fast Permitted, Synchronous Permitted,
	Disconnect Permitted, Ultra Permitted, and SCAM Information in the
	Host Adapter structure.
      */
      HostAdapter->WidePermitted = AutoSCSIData.WidePermitted;
      HostAdapter->FastPermitted = AutoSCSIData.FastPermitted;
      HostAdapter->SynchronousPermitted =
	AutoSCSIData.SynchronousPermitted;
      HostAdapter->DisconnectPermitted =
	AutoSCSIData.DisconnectPermitted;
      if (HostAdapter->HostUltraSCSI)
	HostAdapter->UltraPermitted = AutoSCSIData.UltraPermitted;
      if (HostAdapter->HostSupportsSCAM)
	{
	  HostAdapter->SCAM_Enabled = AutoSCSIData.SCAM_Enabled;
	  HostAdapter->SCAM_Level2 = AutoSCSIData.SCAM_Level2;
	}
    }
  /*
    Initialize fields in the Host Adapter structure for "S" and "A" series
    MultiMaster Host Adapters.
  */
  if (HostAdapter->FirmwareVersion[0] < '4')
    {
      if (SetupInformation.SynchronousInitiationEnabled)
	{
	  HostAdapter->SynchronousPermitted = 0xFF;
	  if (HostAdapter->HostAdapterBusType == BusLogic_EISA_Bus)
	    {
	      if (ExtendedSetupInformation.Misc.FastOnEISA)
		HostAdapter->FastPermitted = 0xFF;
	      if (strcmp(HostAdapter->ModelName, "BT-757") == 0)
		HostAdapter->WidePermitted = 0xFF;
	    }
	}
      HostAdapter->DisconnectPermitted = 0xFF;
      HostAdapter->ParityCheckingEnabled =
	SetupInformation.ParityCheckingEnabled;
      HostAdapter->BusResetEnabled = true;
    }
  /*
    Determine the maximum number of Target IDs and Logical Units supported by
    this driver for Wide and Narrow Host Adapters.
  */
  HostAdapter->MaxTargetDevices = (HostAdapter->HostWideSCSI ? 16 : 8);
  HostAdapter->MaxLogicalUnits = (HostAdapter->ExtendedLUNSupport ? 32 : 8);
  /*
    Select appropriate values for the Driver Queue Depth, Mailbox Count,
    Initial CCBs, and Incremental CCBs variables based on whether or not Strict
    Round Robin Mode is supported.  If Strict Round Robin Mode is supported,
    then there is no performance degradation in using the maximum possible
    number of Outgoing and Incoming Mailboxes and allowing the Tagged and
    Untagged Queue Depths to determine the actual utilization.  If Strict Round
    Robin Mode is not supported, then the Host Adapter must scan all the
    Outgoing Mailboxes whenever an Outgoing Mailbox entry is made, which can
    cause a substantial performance penalty.  The host adapters actually have
    room to store the following number of CCBs internally; that is, they can
    internally queue and manage this many active commands on the SCSI bus
    simultaneously.  Performance measurements demonstrate that the Driver Queue
    Depth should be set to the Mailbox Count, rather than the Host Adapter
    Queue Depth (internal CCB capacity), as it is more efficient to have the
    queued commands waiting in Outgoing Mailboxes if necessary than to block
    the process in the higher levels of the SCSI Subsystem.

	192	  BT-948/958/958D
	100	  BT-946C/956C/956CD/747C/757C/757CD/445C
	 50	  BT-545C/540CF
	 30	  BT-747S/747D/757S/757D/445S/545S/542D/542B/742A
  */
  if (HostAdapter->FirmwareVersion[0] == '5')
    HostAdapter->HostAdapterQueueDepth = 192;
  else if (HostAdapter->FirmwareVersion[0] == '4')
    HostAdapter->HostAdapterQueueDepth =
      (HostAdapter->HostAdapterBusType != BusLogic_ISA_Bus ? 100 : 50);
  else HostAdapter->HostAdapterQueueDepth = 30;
  if (strcmp(HostAdapter->FirmwareVersion, "3.31") >= 0)
    {
      HostAdapter->StrictRoundRobinModeSupport = true;
      HostAdapter->MailboxCount = 255;
      HostAdapter->InitialCCBs = 64;
      HostAdapter->IncrementalCCBs = 16;
    }
  else
    {
      HostAdapter->StrictRoundRobinModeSupport = false;
      HostAdapter->MailboxCount = 32;
      HostAdapter->InitialCCBs = 32;
      HostAdapter->IncrementalCCBs = 8;
    }
  HostAdapter->DriverQueueDepth = HostAdapter->MailboxCount;
  /*
    Tagged Queuing support is available and operates properly on all "W" series
    MultiMaster Host Adapters, on "C" series MultiMaster Host Adapters with
    firmware version 4.22 and above, and on "S" series MultiMaster Host
    Adapters with firmware version 3.35 and above.
  */
  HostAdapter->TaggedQueuingPermitted = 0;
  switch (HostAdapter->FirmwareVersion[0])
    {
    case '5':
      HostAdapter->TaggedQueuingPermitted = 0xFFFF;
      break;
    case '4':
      if (strcmp(HostAdapter->FirmwareVersion, "4.22") >= 0)
	HostAdapter->TaggedQueuingPermitted = 0xFFFF;
      break;
    case '3':
      if (strcmp(HostAdapter->FirmwareVersion, "3.35") >= 0)
	HostAdapter->TaggedQueuingPermitted = 0xFFFF;
      break;
    }
  /*
    Determine the Host Adapter BIOS Address if the BIOS is enabled and
    save it in the Host Adapter structure.  The BIOS is disabled if the
    BIOS_Address is 0.
  */
  HostAdapter->BIOS_Address = ExtendedSetupInformation.BIOS_Address << 12;
  /*
    ISA Host Adapters require Bounce Buffers if there is more than 16MB memory.
  */
  if (HostAdapter->HostAdapterBusType == BusLogic_ISA_Bus &&
      (void *) high_memory > (void *) MAX_DMA_ADDRESS)
    HostAdapter->BounceBuffersRequired = true;
  /*
    BusLogic BT-445S Host Adapters prior to board revision E have a hardware
    bug whereby when the BIOS is enabled, transfers to/from the same address
    range the BIOS occupies modulo 16MB are handled incorrectly.  Only properly
    functioning BT-445S Host Adapters have firmware version 3.37, so require
    that ISA Bounce Buffers be used for the buggy BT-445S models if there is
    more than 16MB memory.
  */
  if (HostAdapter->BIOS_Address > 0 &&
      strcmp(HostAdapter->ModelName, "BT-445S") == 0 &&
      strcmp(HostAdapter->FirmwareVersion, "3.37") < 0 &&
      (void *) high_memory > (void *) MAX_DMA_ADDRESS)
    HostAdapter->BounceBuffersRequired = true;
  /*
    Initialize parameters common to MultiMaster and FlashPoint Host Adapters.
  */
Common:
  /*
    Initialize the Host Adapter Name and Interrupt Label fields from the
    Model Name.
  */
  strcpy(HostAdapter->FullModelName, "BusLogic ");
  strcat(HostAdapter->FullModelName, HostAdapter->ModelName);
  strcpy(HostAdapter->InterruptLabel, HostAdapter->FullModelName);
  /*
    Select an appropriate value for the Tagged Queue Depth either from a
    Command Line Entry, or based on whether this Host Adapter requires that ISA
    Bounce Buffers be used.  The Tagged Queue Depth is left at 0 for automatic
    determination in BusLogic_SelectQueueDepths.  Initialize the Untagged Queue
    Depth.  Tagged Queuing is disabled by default when the Tagged Queue Depth
    is 1 since queuing multiple commands is not possible.
  */
  if (HostAdapter->CommandLineEntry != NULL &&
      HostAdapter->CommandLineEntry->TaggedQueueDepth > 0)
    HostAdapter->TaggedQueueDepth =
      HostAdapter->CommandLineEntry->TaggedQueueDepth;
  else if (HostAdapter->BounceBuffersRequired)
    HostAdapter->TaggedQueueDepth = BusLogic_TaggedQueueDepthBounceBuffers;
  else HostAdapter->TaggedQueueDepth = BusLogic_TaggedQueueDepthAutomatic;
  HostAdapter->UntaggedQueueDepth = BusLogic_UntaggedQueueDepth;
  if (HostAdapter->UntaggedQueueDepth > HostAdapter->TaggedQueueDepth &&
      HostAdapter->TaggedQueueDepth > 0)
    HostAdapter->UntaggedQueueDepth = HostAdapter->TaggedQueueDepth;
  if (HostAdapter->TaggedQueueDepth == 1)
    HostAdapter->TaggedQueuingPermitted = 0;
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
    Tagged Queuing is only allowed if Disconnect/Reconnect is permitted.
    Therefore, mask the Tagged Queuing Permitted Default bits with the
    Disconnect/Reconnect Permitted bits.
  */
  HostAdapter->TaggedQueuingPermitted &= HostAdapter->DisconnectPermitted;
  /*
    Combine the default Tagged Queuing Permitted bits with any Command
    Line Entry Tagged Queuing specification.
  */
  if (HostAdapter->CommandLineEntry != NULL)
    HostAdapter->TaggedQueuingPermitted =
      (HostAdapter->CommandLineEntry->TaggedQueuingPermitted &
       HostAdapter->CommandLineEntry->TaggedQueuingPermittedMask) |
      (HostAdapter->TaggedQueuingPermitted &
       ~HostAdapter->CommandLineEntry->TaggedQueuingPermittedMask);
  /*
    Indicate reading the Host Adapter Configuration completed successfully.
  */
  return true;
}


/*
  BusLogic_ReportHostAdapterConfiguration reports the configuration of
  Host Adapter.
*/

__initfunc(static boolean
BusLogic_ReportHostAdapterConfiguration(BusLogic_HostAdapter_T *HostAdapter))
{
  unsigned short AllTargetsMask = (1 << HostAdapter->MaxTargetDevices) - 1;
  unsigned short SynchronousPermitted, FastPermitted;
  unsigned short UltraPermitted, WidePermitted;
  unsigned short DisconnectPermitted, TaggedQueuingPermitted;
  boolean CommonSynchronousNegotiation, CommonErrorRecovery;
  char SynchronousString[BusLogic_MaxTargetDevices+1];
  char WideString[BusLogic_MaxTargetDevices+1];
  char DisconnectString[BusLogic_MaxTargetDevices+1];
  char TaggedQueuingString[BusLogic_MaxTargetDevices+1];
  char ErrorRecoveryString[BusLogic_MaxTargetDevices+1];
  char *SynchronousMessage = SynchronousString;
  char *WideMessage = WideString;
  char *DisconnectMessage = DisconnectString;
  char *TaggedQueuingMessage = TaggedQueuingString;
  char *ErrorRecoveryMessage = ErrorRecoveryString;
  int TargetID;
  BusLogic_Info("Configuring BusLogic Model %s %s%s%s%s SCSI Host Adapter\n",
		HostAdapter, HostAdapter->ModelName,
		BusLogic_HostAdapterBusNames[HostAdapter->HostAdapterBusType],
		(HostAdapter->HostWideSCSI ? " Wide" : ""),
		(HostAdapter->HostDifferentialSCSI ? " Differential" : ""),
		(HostAdapter->HostUltraSCSI ? " Ultra" : ""));
  BusLogic_Info("  Firmware Version: %s, I/O Address: 0x%X, "
		"IRQ Channel: %d/%s\n", HostAdapter,
		HostAdapter->FirmwareVersion,
		HostAdapter->IO_Address, HostAdapter->IRQ_Channel,
		(HostAdapter->LevelSensitiveInterrupt ? "Level" : "Edge"));
  if (HostAdapter->HostAdapterBusType != BusLogic_PCI_Bus)
    {
      BusLogic_Info("  DMA Channel: ", HostAdapter);
      if (HostAdapter->DMA_Channel > 0)
	BusLogic_Info("%d, ", HostAdapter, HostAdapter->DMA_Channel);
      else BusLogic_Info("None, ", HostAdapter);
      if (HostAdapter->BIOS_Address > 0)
	BusLogic_Info("BIOS Address: 0x%X, ", HostAdapter,
		      HostAdapter->BIOS_Address);
      else BusLogic_Info("BIOS Address: None, ", HostAdapter);
    }
  else
    {
      BusLogic_Info("  PCI Bus: %d, Device: %d, Address: ",
		    HostAdapter, HostAdapter->Bus, HostAdapter->Device);
      if (HostAdapter->PCI_Address > 0)
	BusLogic_Info("0x%X, ", HostAdapter, HostAdapter->PCI_Address);
      else BusLogic_Info("Unassigned, ", HostAdapter);
    }
  BusLogic_Info("Host Adapter SCSI ID: %d\n", HostAdapter,
		HostAdapter->SCSI_ID);
  BusLogic_Info("  Parity Checking: %s, Extended Translation: %s\n",
		HostAdapter,
		(HostAdapter->ParityCheckingEnabled
		 ? "Enabled" : "Disabled"),
		(HostAdapter->ExtendedTranslationEnabled
		 ? "Enabled" : "Disabled"));
  AllTargetsMask &= ~(1 << HostAdapter->SCSI_ID);
  SynchronousPermitted = HostAdapter->SynchronousPermitted & AllTargetsMask;
  FastPermitted = HostAdapter->FastPermitted & AllTargetsMask;
  UltraPermitted = HostAdapter->UltraPermitted & AllTargetsMask;
  if ((BusLogic_MultiMasterHostAdapterP(HostAdapter) &&
       (HostAdapter->FirmwareVersion[0] >= '4' ||
	HostAdapter->HostAdapterBusType == BusLogic_EISA_Bus)) ||
      BusLogic_FlashPointHostAdapterP(HostAdapter))
    {
      CommonSynchronousNegotiation = false;
      if (SynchronousPermitted == 0)
	{
	  SynchronousMessage = "Disabled";
	  CommonSynchronousNegotiation = true;
	}
      else if (SynchronousPermitted == AllTargetsMask)
	if (FastPermitted == 0)
	  {
	    SynchronousMessage = "Slow";
	    CommonSynchronousNegotiation = true;
	  }
	else if (FastPermitted == AllTargetsMask)
	  if (UltraPermitted == 0)
	    {
	      SynchronousMessage = "Fast";
	      CommonSynchronousNegotiation = true;
	    }
	  else if (UltraPermitted == AllTargetsMask)
	    {
	      SynchronousMessage = "Ultra";
	      CommonSynchronousNegotiation = true;
	    }
      if (!CommonSynchronousNegotiation)
	{
	  for (TargetID = 0;
	       TargetID < HostAdapter->MaxTargetDevices;
	       TargetID++)
	    SynchronousString[TargetID] =
	      ((!(SynchronousPermitted & (1 << TargetID))) ? 'N' :
	       (!(FastPermitted & (1 << TargetID)) ? 'S' :
		(!(UltraPermitted & (1 << TargetID)) ? 'F' : 'U')));
	  SynchronousString[HostAdapter->SCSI_ID] = '#';
	  SynchronousString[HostAdapter->MaxTargetDevices] = '\0';
	}
    }
  else SynchronousMessage =
	 (SynchronousPermitted == 0 ? "Disabled" : "Enabled");
  WidePermitted = HostAdapter->WidePermitted & AllTargetsMask;
  if (WidePermitted == 0)
    WideMessage = "Disabled";
  else if (WidePermitted == AllTargetsMask)
    WideMessage = "Enabled";
  else
    {
      for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
	 WideString[TargetID] =
	   ((WidePermitted & (1 << TargetID)) ? 'Y' : 'N');
      WideString[HostAdapter->SCSI_ID] = '#';
      WideString[HostAdapter->MaxTargetDevices] = '\0';
    }
  DisconnectPermitted = HostAdapter->DisconnectPermitted & AllTargetsMask;
  if (DisconnectPermitted == 0)
    DisconnectMessage = "Disabled";
  else if (DisconnectPermitted == AllTargetsMask)
    DisconnectMessage = "Enabled";
  else
    {
      for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
	DisconnectString[TargetID] =
	  ((DisconnectPermitted & (1 << TargetID)) ? 'Y' : 'N');
      DisconnectString[HostAdapter->SCSI_ID] = '#';
      DisconnectString[HostAdapter->MaxTargetDevices] = '\0';
    }
  TaggedQueuingPermitted =
    HostAdapter->TaggedQueuingPermitted & AllTargetsMask;
  if (TaggedQueuingPermitted == 0)
    TaggedQueuingMessage = "Disabled";
  else if (TaggedQueuingPermitted == AllTargetsMask)
    TaggedQueuingMessage = "Enabled";
  else
    {
      for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
	TaggedQueuingString[TargetID] =
	  ((TaggedQueuingPermitted & (1 << TargetID)) ? 'Y' : 'N');
      TaggedQueuingString[HostAdapter->SCSI_ID] = '#';
      TaggedQueuingString[HostAdapter->MaxTargetDevices] = '\0';
    }
  BusLogic_Info("  Synchronous Negotiation: %s, Wide Negotiation: %s\n",
		HostAdapter, SynchronousMessage, WideMessage);
  BusLogic_Info("  Disconnect/Reconnect: %s, Tagged Queuing: %s\n",
		HostAdapter, DisconnectMessage, TaggedQueuingMessage);
  if (BusLogic_MultiMasterHostAdapterP(HostAdapter))
    {
      BusLogic_Info("  Scatter/Gather Limit: %d of %d segments, "
		    "Mailboxes: %d\n", HostAdapter,
		    HostAdapter->DriverScatterGatherLimit,
		    HostAdapter->HostAdapterScatterGatherLimit,
		    HostAdapter->MailboxCount);
      BusLogic_Info("  Driver Queue Depth: %d, "
		    "Host Adapter Queue Depth: %d\n",
		    HostAdapter, HostAdapter->DriverQueueDepth,
		    HostAdapter->HostAdapterQueueDepth);
    }
  else BusLogic_Info("  Driver Queue Depth: %d, "
		     "Scatter/Gather Limit: %d segments\n",
		     HostAdapter, HostAdapter->DriverQueueDepth,
		     HostAdapter->DriverScatterGatherLimit);
  BusLogic_Info("  Tagged Queue Depth: ", HostAdapter);
  if (HostAdapter->TaggedQueueDepth > 0)
    BusLogic_Info("%d", HostAdapter, HostAdapter->TaggedQueueDepth);
  else BusLogic_Info("Automatic", HostAdapter);
  BusLogic_Info(", Untagged Queue Depth: %d\n", HostAdapter,
		HostAdapter->UntaggedQueueDepth);
  CommonErrorRecovery = true;
  for (TargetID = 1; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    if (HostAdapter->ErrorRecoveryStrategy[TargetID] !=
	HostAdapter->ErrorRecoveryStrategy[0])
      {
	CommonErrorRecovery = false;
	break;
      }
  if (CommonErrorRecovery)
    ErrorRecoveryMessage =
      BusLogic_ErrorRecoveryStrategyNames[
	HostAdapter->ErrorRecoveryStrategy[0]];
  else
    {
      for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
	ErrorRecoveryString[TargetID] =
	  BusLogic_ErrorRecoveryStrategyLetters[
	    HostAdapter->ErrorRecoveryStrategy[TargetID]];
      ErrorRecoveryString[HostAdapter->SCSI_ID] = '#';
      ErrorRecoveryString[HostAdapter->MaxTargetDevices] = '\0';
    }
  BusLogic_Info("  Error Recovery Strategy: %s, SCSI Bus Reset: %s\n",
		HostAdapter, ErrorRecoveryMessage,
		(HostAdapter->BusResetEnabled ? "Enabled" : "Disabled"));
  if (HostAdapter->TerminationInfoValid)
    {
      if (HostAdapter->HostWideSCSI)
	BusLogic_Info("  SCSI Bus Termination: %s", HostAdapter,
		      (HostAdapter->LowByteTerminated
		       ? (HostAdapter->HighByteTerminated
			  ? "Both Enabled" : "Low Enabled")
		       : (HostAdapter->HighByteTerminated
			  ? "High Enabled" : "Both Disabled")));
      else BusLogic_Info("  SCSI Bus Termination: %s", HostAdapter,
			 (HostAdapter->LowByteTerminated ?
			  "Enabled" : "Disabled"));
      if (HostAdapter->HostSupportsSCAM)
	BusLogic_Info(", SCAM: %s", HostAdapter,
		      (HostAdapter->SCAM_Enabled
		       ? (HostAdapter->SCAM_Level2
			  ? "Enabled, Level 2" : "Enabled, Level 1")
		       : "Disabled"));
      BusLogic_Info("\n", HostAdapter);
    }
  /*
    Indicate reporting the Host Adapter configuration completed successfully.
  */
  return true;
}


/*
  BusLogic_AcquireResources acquires the system resources necessary to use
  Host Adapter.
*/

__initfunc(static boolean
BusLogic_AcquireResources(BusLogic_HostAdapter_T *HostAdapter))
{
  BusLogic_HostAdapter_T *FirstHostAdapter =
    BusLogic_RegisteredHostAdapters[HostAdapter->IRQ_Channel];
  if (HostAdapter->IRQ_Channel == 0)
    {
      BusLogic_Error("NO LEGAL INTERRUPT CHANNEL ASSIGNED - DETACHING\n",
		     HostAdapter);
      return false;
    }
  /*
    Acquire exclusive or shared access to the IRQ Channel if necessary.
  */
  if (FirstHostAdapter->Next == NULL)
    {
      if (request_irq(HostAdapter->IRQ_Channel, BusLogic_InterruptHandler,
		      SA_INTERRUPT | SA_SHIRQ,
		      HostAdapter->InterruptLabel, NULL) < 0)
	{
	  BusLogic_Error("UNABLE TO ACQUIRE IRQ CHANNEL %d - DETACHING\n",
			 HostAdapter, HostAdapter->IRQ_Channel);
	  return false;
	}
    }
  else if (strlen(FirstHostAdapter->InterruptLabel) + 11
	   < sizeof(FirstHostAdapter->InterruptLabel))
    {
      strcat(FirstHostAdapter->InterruptLabel, " + ");
      strcat(FirstHostAdapter->InterruptLabel, HostAdapter->ModelName);
    }
  HostAdapter->IRQ_ChannelAcquired = true;
  /*
    Acquire exclusive access to the DMA Channel.
  */
  if (HostAdapter->DMA_Channel > 0)
    {
      if (request_dma(HostAdapter->DMA_Channel,
		      HostAdapter->FullModelName) < 0)
	{
	  BusLogic_Error("UNABLE TO ACQUIRE DMA CHANNEL %d - DETACHING\n",
			 HostAdapter, HostAdapter->DMA_Channel);
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
  BusLogic_HostAdapter_T *FirstHostAdapter =
    BusLogic_RegisteredHostAdapters[HostAdapter->IRQ_Channel];
  /*
    Release exclusive or shared access to the IRQ Channel.
  */
  if (HostAdapter->IRQ_ChannelAcquired)
    if (FirstHostAdapter->Next == NULL)
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

__initfunc(static boolean
BusLogic_TestInterrupts(BusLogic_HostAdapter_T *HostAdapter))
{
  unsigned int InitialInterruptCount, FinalInterruptCount;
  int TestCount = 5, i;
  /*
    FlashPoint Host Adapters do not provide for an interrupt test.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter)) return true;
  /*
    Inhibit the Interrupt Test if requested.
  */
  if (HostAdapter->LocalOptions.Bits.InhibitInterruptTest) return true;
  /*
    Issue the Test Command Complete Interrupt commands.
  */
  InitialInterruptCount = kstat.interrupts[HostAdapter->IRQ_Channel];
  for (i = 0; i < TestCount; i++)
    BusLogic_Command(HostAdapter, BusLogic_TestCommandCompleteInterrupt,
		     NULL, 0, NULL, 0);
  FinalInterruptCount = kstat.interrupts[HostAdapter->IRQ_Channel];
  /*
    Verify that BusLogic_InterruptHandler was called at least TestCount
    times.  Shared IRQ Channels could cause more than TestCount interrupts to
    occur, but there should never be fewer than TestCount, unless one or more
    interrupts were lost.
  */
  if (FinalInterruptCount < InitialInterruptCount + TestCount)
    return BusLogic_Failure(HostAdapter, "HOST ADAPTER INTERRUPT TEST");
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
  BusLogic_SetCCBFormatRequest_T SetCCBFormatRequest;
  int TargetID;
  /*
    Initialize the Bus Device Reset Pending CCB, Tagged Queuing Active,
    Command Successful Flag, Active Commands, and Commands Since Reset
    for each Target Device.
  */
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    HostAdapter->BusDeviceResetPendingCCB[TargetID] = NULL;
  memset(HostAdapter->TaggedQueuingActive, false,
	 sizeof(HostAdapter->TaggedQueuingActive));
  memset(HostAdapter->CommandSuccessfulFlag, false,
	 sizeof(HostAdapter->CommandSuccessfulFlag));
  memset(HostAdapter->ActiveCommands, 0,
	 sizeof(HostAdapter->ActiveCommands));
  memset(HostAdapter->CommandsSinceReset, 0,
	 sizeof(HostAdapter->CommandsSinceReset));
  /*
    FlashPoint Host Adapters do not use Outgoing and Incoming Mailboxes.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter)) goto Done;
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
  if (HostAdapter->StrictRoundRobinModeSupport)
    {
      RoundRobinModeRequest = BusLogic_StrictRoundRobinMode;
      if (BusLogic_Command(HostAdapter, BusLogic_EnableStrictRoundRobinMode,
			   &RoundRobinModeRequest,
			   sizeof(RoundRobinModeRequest), NULL, 0) < 0)
	return BusLogic_Failure(HostAdapter, "ENABLE STRICT ROUND ROBIN MODE");
    }
  /*
    For Host Adapters that support Extended LUN Format CCBs, issue the Set CCB
    Format command to allow 32 Logical Units per Target Device.
  */
  if (HostAdapter->ExtendedLUNSupport)
    {
      SetCCBFormatRequest = BusLogic_ExtendedLUNFormatCCB;
      if (BusLogic_Command(HostAdapter, BusLogic_SetCCBFormat,
			   &SetCCBFormatRequest, sizeof(SetCCBFormatRequest),
			   NULL, 0) < 0)
	return BusLogic_Failure(HostAdapter, "SET CCB FORMAT");
    }
  /*
    Announce Successful Initialization.
  */
Done:
  if (HostAdapter->HostAdapterInitialized)
    BusLogic_Warning("*** %s Initialized Successfully ***\n",
		     HostAdapter, HostAdapter->FullModelName);
  else BusLogic_Info("*** %s Initialized Successfully ***\n",
		     HostAdapter, HostAdapter->FullModelName);
  HostAdapter->HostAdapterInitialized = true;
  /*
    Indicate the Host Adapter Initialization completed successfully.
  */
  return true;
}


/*
  BusLogic_TargetDeviceInquiry inquires about the Target Devices accessible
  through Host Adapter and reports on the results.
*/

static boolean BusLogic_TargetDeviceInquiry(BusLogic_HostAdapter_T
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
    FlashPoint Host Adapters do not provide for Target Device Inquiry.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter)) return true;
  /*
    Inhibit the Target Devices Inquiry if requested.
  */
  if (HostAdapter->LocalOptions.Bits.InhibitTargetInquiry)
    {
      BusLogic_Info("  Target Device Inquiry Inhibited\n", HostAdapter);
      return true;
    }
  /*
    Issue the Inquire Target Devices command for host adapters with firmware
    version 4.25 or later, or the Inquire Installed Devices ID 0 to 7 command
    for older host adapters.  This is necessary to force Synchronous Transfer
    Negotiation so that the Inquire Setup Information and Inquire Synchronous
    Period commands will return valid data.  The Inquire Target Devices command
    is preferable to Inquire Installed Devices ID 0 to 7 since it only probes
    Logical Unit 0 of each Target Device.
  */
  if (strcmp(HostAdapter->FirmwareVersion, "4.25") >= 0)
    {
      if (BusLogic_Command(HostAdapter, BusLogic_InquireTargetDevices, NULL, 0,
			   &InstalledDevices, sizeof(InstalledDevices))
	  != sizeof(InstalledDevices))
	return BusLogic_Failure(HostAdapter, "INQUIRE TARGET DEVICES");
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
	    BusLogic_Info("  Target %d: Synchronous at "
			  "%d.%02d mega-transfers/second, offset %d\n",
			  HostAdapter, TargetID,
			  RoundedSynchronousTransferRate / 100,
			  RoundedSynchronousTransferRate % 100,
			  HostAdapter->SynchronousValues[TargetID].Offset);
	  }
	else if (SynchronousPeriod > 0)
	  {
	    int SynchronousTransferRate = 100000000 / SynchronousPeriod;
	    int RoundedSynchronousTransferRate =
	      (SynchronousTransferRate + 50000) / 100000;
	    BusLogic_Info("  Target %d: Synchronous at "
			  "%d.%01d mega-transfers/second, offset %d\n",
			  HostAdapter, TargetID,
			  RoundedSynchronousTransferRate / 10,
			  RoundedSynchronousTransferRate % 10,
			  HostAdapter->SynchronousValues[TargetID].Offset);
	  }
	else BusLogic_Info("  Target %d: Asynchronous\n",
			   HostAdapter, TargetID);
	TargetDevicesFound++;
      }
  if (TargetDevicesFound == 0)
    BusLogic_Info("  No Target Devices Found\n", HostAdapter);
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

__initfunc(static void
BusLogic_InitializeHostStructure(BusLogic_HostAdapter_T
				 *HostAdapter,
				 SCSI_Host_T *Host))
{
  Host->max_id = HostAdapter->MaxTargetDevices;
  Host->max_lun = HostAdapter->MaxLogicalUnits;
  Host->max_channel = 0;
  Host->unique_id = HostAdapter->IO_Address;
  Host->this_id = HostAdapter->SCSI_ID;
  Host->can_queue = HostAdapter->DriverQueueDepth;
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
  int DesiredCCBs = HostAdapter->MaxTargetDevices - 1;
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
	1 + ((HostAdapter->HostAdapterQueueDepth
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
	HostAdapter->QueueDepth[Device->id] = Device->queue_depth;
	DesiredCCBs += Device->queue_depth;
      }
  BusLogic_CreateAdditionalCCBs(HostAdapter,
				DesiredCCBs - HostAdapter->AllocatedCCBs,
				false);
}


/*
  BusLogic_DetectHostAdapter probes for BusLogic Host Adapters at the standard
  I/O Addresses where they may be located, initializing, registering, and
  reporting the configuration of each BusLogic Host Adapter it finds.  It
  returns the number of BusLogic Host Adapters successfully initialized and
  registered.
*/

__initfunc(int BusLogic_DetectHostAdapter(SCSI_Host_Template_T *HostTemplate))
{
  int BusLogicHostAdapterCount = 0, CommandLineEntryIndex = 0, ProbeIndex;
  char *MessageBuffer = NULL;
  if (BusLogic_ProbeOptions.Bits.NoProbe) return 0;
  BusLogic_InitializeProbeInfoList();
  for (ProbeIndex = 0; ProbeIndex < BusLogic_ProbeInfoCount; ProbeIndex++)
    {
      BusLogic_ProbeInfo_T *ProbeInfo = &BusLogic_ProbeInfoList[ProbeIndex];
      BusLogic_HostAdapter_T HostAdapterPrototype;
      BusLogic_HostAdapter_T *HostAdapter = &HostAdapterPrototype;
      SCSI_Host_T *Host;
      if (ProbeInfo->IO_Address == 0) continue;
      memset(HostAdapter, 0, sizeof(BusLogic_HostAdapter_T));
      HostAdapter->IO_Address = ProbeInfo->IO_Address;
      HostAdapter->PCI_Address = ProbeInfo->PCI_Address;
      HostAdapter->HostAdapterType = ProbeInfo->HostAdapterType;
      HostAdapter->HostAdapterBusType = ProbeInfo->HostAdapterBusType;
      HostAdapter->Bus = ProbeInfo->Bus;
      HostAdapter->Device = ProbeInfo->Device;
      HostAdapter->IRQ_Channel = ProbeInfo->IRQ_Channel;
      HostAdapter->AddressCount =
	BusLogic_HostAdapter_AddressCount[HostAdapter->HostAdapterType];
      if (MessageBuffer == NULL)
	MessageBuffer =
	  scsi_init_malloc(BusLogic_MessageBufferSize, GFP_ATOMIC);
      if (MessageBuffer == NULL)
	{
	  BusLogic_Error("BusLogic: Unable to allocate Message Buffer\n",
			 HostAdapter);
	  return BusLogicHostAdapterCount;
	}
      HostAdapter->MessageBuffer = MessageBuffer;
      /*
	If an explicit I/O Address was specified, Initialize the Command Line
	Entry field and inhibit the check for whether the I/O Address range is
	already in use.
      */
      if (CommandLineEntryIndex < BusLogic_CommandLineEntryCount &&
	  BusLogic_CommandLineEntries[CommandLineEntryIndex].IO_Address ==
	  HostAdapter->IO_Address)
	HostAdapter->CommandLineEntry =
	  &BusLogic_CommandLineEntries[CommandLineEntryIndex++];
      else if (check_region(HostAdapter->IO_Address,
			    HostAdapter->AddressCount) < 0)
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
      BusLogic_AnnounceDriver(HostAdapter);
      /*
	Register usage of the I/O Address range.  From this point onward, any
	failure will be assumed to be due to a problem with the Host Adapter,
	rather than due to having mistakenly identified this port as belonging
	to a BusLogic Host Adapter.  The I/O Address range will not be
	released, thereby preventing it from being incorrectly identified as
	any other type of Host Adapter.
      */
      request_region(HostAdapter->IO_Address, HostAdapter->AddressCount,
		     "BusLogic");
      /*
	Register the SCSI Host structure.
      */
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
	must be registered.
      */
      BusLogic_RegisterHostAdapter(HostAdapter);
      /*
	Read the Host Adapter Configuration, Configure the Host Adapter,
	Acquire the System Resources necessary to use the Host Adapter,
	then Test Interrupts, Create the Mailboxes, Initial CCBs, and
	Target Device Statistics, Initialize the Host Adapter, and
	finally perform Target Device Inquiry.
      */
      if (BusLogic_ReadHostAdapterConfiguration(HostAdapter) &&
	  BusLogic_ReportHostAdapterConfiguration(HostAdapter) &&
	  BusLogic_AcquireResources(HostAdapter) &&
	  BusLogic_TestInterrupts(HostAdapter) &&
	  BusLogic_CreateMailboxes(HostAdapter) &&
	  BusLogic_CreateInitialCCBs(HostAdapter) &&
	  BusLogic_CreateTargetDeviceStatistics(HostAdapter) &&
	  BusLogic_InitializeHostAdapter(HostAdapter) &&
	  BusLogic_TargetDeviceInquiry(HostAdapter))
	{
	  /*
	    Initialization has been completed successfully.  Release and
	    re-register usage of the I/O Address range so that the Model
	    Name of the Host Adapter will appear, and initialize the SCSI
	    Host structure.
	  */
	  MessageBuffer = NULL;
	  release_region(HostAdapter->IO_Address,
			 HostAdapter->AddressCount);
	  request_region(HostAdapter->IO_Address,
			 HostAdapter->AddressCount,
			 HostAdapter->FullModelName);
	  BusLogic_InitializeHostStructure(HostAdapter, Host);
	  BusLogicHostAdapterCount++;
	}
      else
	{
	  /*
	    An error occurred during Host Adapter Configuration Querying,
	    Host Adapter Configuration, Resource Acquisition, Interrupt
	    Testing, CCB Creation, Host Adapter Initialization, or Target
	    Device Inquiry, so remove Host Adapter from the list of
	    registered BusLogic Host Adapters, destroy the Target Device
	    Statistics, CCBs, and Mailboxes, Release the System Resources,
	    and Unregister the SCSI Host.
	  */
	  BusLogic_DestroyTargetDeviceStatistics(HostAdapter);
	  BusLogic_DestroyCCBs(HostAdapter);
	  BusLogic_DestroyMailboxes(HostAdapter);
	  BusLogic_ReleaseResources(HostAdapter);
	  BusLogic_UnregisterHostAdapter(HostAdapter);
	  scsi_unregister(Host);
	}
    }
  if (MessageBuffer != NULL)
    scsi_init_free(MessageBuffer, BusLogic_MessageBufferSize);
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
    FlashPoint Host Adapters must also be released by the FlashPoint
    SCCB Manager.
  */
  if (BusLogic_FlashPointHostAdapterP(HostAdapter))
    {
      FlashPoint_ReleaseHostAdapter(HostAdapter->CardHandle);
      scsi_init_free((char *) HostAdapter->FlashPointInfo,
		     sizeof(FlashPoint_Info_T));
    }
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
  release_region(HostAdapter->IO_Address, HostAdapter->AddressCount);
  /*
    Remove Host Adapter from the list of registered BusLogic Host Adapters.
  */
  BusLogic_UnregisterHostAdapter(HostAdapter);
  return 0;
}


/*
  BusLogic_QueueCompletedCCB queues CCB for completion processing.
*/

static void BusLogic_QueueCompletedCCB(BusLogic_CCB_T *CCB)
{
  CCB->Status = BusLogic_CCB_Completed;
  CCB->Next = NULL;
  if (BusLogic_FirstCompletedCCB == NULL)
    {
      BusLogic_FirstCompletedCCB = CCB;
      BusLogic_LastCompletedCCB = CCB;
    }
  else
    {
      BusLogic_LastCompletedCCB->Next = CCB;
      BusLogic_LastCompletedCCB = CCB;
    }
  CCB->HostAdapter->ActiveCommands[CCB->TargetID]--;
}


/*
  BusLogic_ComputeResultCode computes a SCSI Subsystem Result Code from
  the Host Adapter Status and Target Device Status.
*/

static int BusLogic_ComputeResultCode(BusLogic_HostAdapter_T *HostAdapter,
				      BusLogic_HostAdapterStatus_T
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
      BusLogic_Warning("BusLogic Driver Protocol Error 0x%02X\n",
		       HostAdapter, HostAdapterStatus);
    case BusLogic_DataUnderRun:
    case BusLogic_DataOverRun:
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
      BusLogic_Warning("Unknown Host Adapter Status 0x%02X\n",
		       HostAdapter, HostAdapterStatus);
      HostStatus = DID_ERROR;
      break;
    }
  return (HostStatus << 16) | TargetDeviceStatus;
}


/*
  BusLogic_ScanIncomingMailboxes scans the Incoming Mailboxes saving any
  Incoming Mailbox entries for completion processing.
*/

static void BusLogic_ScanIncomingMailboxes(BusLogic_HostAdapter_T *HostAdapter)
{
  /*
    Scan through the Incoming Mailboxes in Strict Round Robin fashion, saving
    any completed CCBs for further processing.  It is essential that for each
    CCB and SCSI Command issued, command completion processing is performed
    exactly once.  Therefore, only Incoming Mailboxes with completion code
    Command Completed Without Error, Command Completed With Error, or Command
    Aborted At Host Request are saved for completion processing.  When an
    Incoming Mailbox has a completion code of Aborted Command Not Found, the
    CCB had already completed or been aborted before the current Abort request
    was processed, and so completion processing has already occurred and no
    further action should be taken.
  */
  BusLogic_IncomingMailbox_T *NextIncomingMailbox =
    HostAdapter->NextIncomingMailbox;
  BusLogic_CompletionCode_T CompletionCode;
  while ((CompletionCode = NextIncomingMailbox->CompletionCode) !=
	 BusLogic_IncomingMailboxFree)
    {
      BusLogic_CCB_T *CCB = (BusLogic_CCB_T *)
	Bus_to_Virtual(NextIncomingMailbox->CCB);
      if (CompletionCode != BusLogic_AbortedCommandNotFound)
	if (CCB->Status == BusLogic_CCB_Active ||
	    CCB->Status == BusLogic_CCB_Reset)
	  {
	    /*
	      Save the Completion Code for this CCB and queue the CCB
	      for completion processing.
	    */
	    CCB->CompletionCode = CompletionCode;
	    BusLogic_QueueCompletedCCB(CCB);
	  }
	else
	  {
	    /*
	      If a CCB ever appears in an Incoming Mailbox and is not marked as
	      status Active or Reset, then there is most likely a bug in the
	      Host Adapter firmware.
	    */
	    BusLogic_Warning("Illegal CCB #%ld status %d in "
			     "Incoming Mailbox\n", HostAdapter,
			     CCB->SerialNumber, CCB->Status);
	  }
      else BusLogic_Warning("Aborted CCB #%ld to Target %d Not Found\n",
			    HostAdapter, CCB->SerialNumber, CCB->TargetID);
      NextIncomingMailbox->CompletionCode = BusLogic_IncomingMailboxFree;
      if (++NextIncomingMailbox > HostAdapter->LastIncomingMailbox)
	NextIncomingMailbox = HostAdapter->FirstIncomingMailbox;
    }
  HostAdapter->NextIncomingMailbox = NextIncomingMailbox;
}


/*
  BusLogic_ProcessCompletedCCBs iterates over the completed CCBs setting
  the SCSI Command Result Codes, deallocating the CCBs, and calling the
  SCSI Subsystem Completion Routines.  Interrupts should already have been
  disabled by the caller.
*/

static void BusLogic_ProcessCompletedCCBs(void)
{
  static boolean ProcessCompletedCCBsActive = false;
  if (ProcessCompletedCCBsActive) return;
  ProcessCompletedCCBsActive = true;
  while (BusLogic_FirstCompletedCCB != NULL)
    {
      BusLogic_CCB_T *CCB = BusLogic_FirstCompletedCCB;
      SCSI_Command_T *Command = CCB->Command;
      BusLogic_HostAdapter_T *HostAdapter = CCB->HostAdapter;
      BusLogic_FirstCompletedCCB = CCB->Next;
      if (BusLogic_FirstCompletedCCB == NULL)
	BusLogic_LastCompletedCCB = NULL;
      /*
	Process the Completed CCB.
      */
      if (CCB->Opcode == BusLogic_BusDeviceReset)
	{
	  int TargetID = CCB->TargetID;
	  BusLogic_Warning("Bus Device Reset CCB #%ld to Target "
			   "%d Completed\n", HostAdapter,
			   CCB->SerialNumber, TargetID);
	  BusLogic_IncrementErrorCounter(
	    &HostAdapter->TargetDeviceStatistics[TargetID]
			  .BusDeviceResetsCompleted);
	  HostAdapter->CommandsSinceReset[TargetID] = 0;
	  HostAdapter->TaggedQueuingActive[TargetID] = false;
	  HostAdapter->LastResetCompleted[TargetID] = jiffies;
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
		HostAdapter->ActiveCommands[TargetID]--;
		Command->result = DID_RESET << 16;
		Command->scsi_done(Command);
	      }
	  HostAdapter->BusDeviceResetPendingCCB[TargetID] = NULL;
	}
      else
	{
	  /*
	    Translate the Completion Code, Host Adapter Status, and Target
	    Device Status into a SCSI Subsystem Result Code.
	  */
	  switch (CCB->CompletionCode)
	    {
	    case BusLogic_IncomingMailboxFree:
	    case BusLogic_AbortedCommandNotFound:
	    case BusLogic_InvalidCCB:
	      BusLogic_Warning("CCB #%ld to Target %d Impossible State\n",
			       HostAdapter, CCB->SerialNumber, CCB->TargetID);
	      break;
	    case BusLogic_CommandCompletedWithoutError:
	      HostAdapter->TargetDeviceStatistics[CCB->TargetID]
			   .CommandsCompleted++;
	      HostAdapter->CommandSuccessfulFlag[CCB->TargetID] = true;
	      Command->result = DID_OK << 16;
	      break;
	    case BusLogic_CommandAbortedAtHostRequest:
	      BusLogic_Warning("CCB #%ld to Target %d Aborted\n",
			       HostAdapter, CCB->SerialNumber, CCB->TargetID);
	      BusLogic_IncrementErrorCounter(
		&HostAdapter->TargetDeviceStatistics[CCB->TargetID]
			      .CommandAbortsCompleted);
	      Command->result = DID_ABORT << 16;
	      break;
	    case BusLogic_CommandCompletedWithError:
	      Command->result =
		BusLogic_ComputeResultCode(HostAdapter,
					   CCB->HostAdapterStatus,
					   CCB->TargetDeviceStatus);
	      if (CCB->HostAdapterStatus != BusLogic_SCSISelectionTimeout)
		{
		  HostAdapter->TargetDeviceStatistics[CCB->TargetID]
			       .CommandsCompleted++;
		  if (BusLogic_GlobalOptions.Bits.TraceErrors)
		      {
			int i;
			BusLogic_Notice("CCB #%ld Target %d: Result %X Host "
					"Adapter Status %02X "
					"Target Status %02X\n",
					HostAdapter, CCB->SerialNumber,
					CCB->TargetID, Command->result,
					CCB->HostAdapterStatus,
					CCB->TargetDeviceStatus);
			BusLogic_Notice("CDB   ", HostAdapter);
			for (i = 0; i < CCB->CDB_Length; i++)
			  BusLogic_Notice(" %02X", HostAdapter, CCB->CDB[i]);
			BusLogic_Notice("\n", HostAdapter);
			BusLogic_Notice("Sense ", HostAdapter);
			for (i = 0; i < CCB->SenseDataLength; i++)
			  BusLogic_Notice(" %02X", HostAdapter,
					  Command->sense_buffer[i]);
			BusLogic_Notice("\n", HostAdapter);
		      }
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
    }
  ProcessCompletedCCBsActive = false;
}


/*
  BusLogic_InterruptHandler handles hardware interrupts from BusLogic Host
  Adapters.
*/

static void BusLogic_InterruptHandler(int IRQ_Channel,
				      void *DeviceIdentifier,
				      Registers_T *InterruptRegisters)
{
  BusLogic_HostAdapter_T *FirstHostAdapter =
    BusLogic_RegisteredHostAdapters[IRQ_Channel];
  boolean HostAdapterResetRequested = false;
  BusLogic_HostAdapter_T *HostAdapter;
  BusLogic_Lock_T Lock;
  /*
    Iterate over the installed BusLogic Host Adapters accepting any Incoming
    Mailbox entries and saving the completed CCBs for processing.  This
    interrupt handler is installed as a fast interrupt, so interrupts are
    disabled when the interrupt handler is entered.
  */
  for (HostAdapter = FirstHostAdapter;
       HostAdapter != NULL;
       HostAdapter = HostAdapter->Next)
    {
      /*
	Acquire exclusive access to Host Adapter.
      */
      BusLogic_AcquireHostAdapterLockID(HostAdapter, &Lock);
      /*
	Handle Interrupts appropriately for each Host Adapter type.
      */
      if (BusLogic_MultiMasterHostAdapterP(HostAdapter))
	{
	  BusLogic_InterruptRegister_T InterruptRegister;
	  /*
	    Read the Host Adapter Interrupt Register.
	  */
	  InterruptRegister.All = BusLogic_ReadInterruptRegister(HostAdapter);
	  if (InterruptRegister.Bits.InterruptValid)
	    {
	      /*
		Acknowledge the interrupt and reset the Host Adapter
		Interrupt Register.
	      */
	      BusLogic_InterruptReset(HostAdapter);
	      /*
		Process valid External SCSI Bus Reset and Incoming Mailbox
		Loaded Interrupts.  Command Complete Interrupts are noted,
		and Outgoing Mailbox Available Interrupts are ignored, as
		they are never enabled.
	      */
	      if (InterruptRegister.Bits.ExternalBusReset)
		{
		  HostAdapter->HostAdapterResetRequested = true;
		  HostAdapterResetRequested = true;
		}
	      else if (InterruptRegister.Bits.IncomingMailboxLoaded)
		BusLogic_ScanIncomingMailboxes(HostAdapter);
	      else if (InterruptRegister.Bits.CommandComplete)
		HostAdapter->HostAdapterCommandCompleted = true;
	    }
	}
      else
	{
	  /*
	    Check if there is a pending interrupt for this Host Adapter.
	  */
	  if (FlashPoint_InterruptPending(HostAdapter->CardHandle))
	    if (FlashPoint_HandleInterrupt(HostAdapter->CardHandle)
		== FlashPoint_ExternalBusReset)
	      {
		HostAdapter->HostAdapterResetRequested = true;
		HostAdapterResetRequested = true;
	      }
	}
      /*
	Release exclusive access to Host Adapter.
      */
      BusLogic_ReleaseHostAdapterLockID(HostAdapter, &Lock);
    }
  /*
    Process any completed CCBs.
  */
  if (BusLogic_FirstCompletedCCB != NULL)
    BusLogic_ProcessCompletedCCBs();
  /*
    Iterate over the Host Adapters performing any requested
    Host Adapter Resets.
  */
  if (HostAdapterResetRequested)
    for (HostAdapter = FirstHostAdapter;
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
  Mailbox for execution by Host Adapter.  The Host Adapter's Lock should
  already have been acquired by the caller.
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
	{
	  HostAdapter->ActiveCommands[CCB->TargetID]++;
	  if (CCB->Opcode != BusLogic_BusDeviceReset)
	    HostAdapter->TargetDeviceStatistics[CCB->TargetID]
			 .CommandsAttempted++;
	}
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
  BusLogic_TargetDeviceStatistics_T *TargetDeviceStatistics =
    HostAdapter->TargetDeviceStatistics;
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
    and try again.  If that fails, the Host Adapter is probably hung so signal
    an error as a Host Adapter Hard Reset should be initiated soon.
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
#ifndef CONFIG_SCSI_OMIT_FLASHPOINT
      if (BusLogic_MultiMasterHostAdapterP(HostAdapter))
	CCB->DataPointer = Virtual_to_Bus(CCB->ScatterGatherList);
      else CCB->DataPointer = (BusLogic_BusAddress_T) CCB->ScatterGatherList;
#else
      CCB->DataPointer = Virtual_to_Bus(CCB->ScatterGatherList);
#endif
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
      TargetDeviceStatistics[TargetID].ReadCommands++;
      BusLogic_IncrementByteCounter(
	&TargetDeviceStatistics[TargetID].TotalBytesRead, BufferLength);
      BusLogic_IncrementSizeBucket(
	TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets,
	BufferLength);
      break;
    case WRITE_6:
    case WRITE_10:
      CCB->DataDirection = BusLogic_DataOutLengthChecked;
      TargetDeviceStatistics[TargetID].WriteCommands++;
      BusLogic_IncrementByteCounter(
	&TargetDeviceStatistics[TargetID].TotalBytesWritten, BufferLength);
      BusLogic_IncrementSizeBucket(
	TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets,
	BufferLength);
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
  CCB->TagEnable = false;
  CCB->LegacyTagEnable = false;
  /*
    BusLogic recommends that after a Reset the first couple of commands that
    are sent to a Target Device be sent in a non Tagged Queue fashion so that
    the Host Adapter and Target Device can establish Synchronous and Wide
    Transfer before Queue Tag messages can interfere with the Synchronous and
    Wide Negotiation messages.  By waiting to enable Tagged Queuing until after
    the first BusLogic_MaxTaggedQueueDepth commands have been queued, it is
    assured that after a Reset any pending commands are requeued before Tagged
    Queuing is enabled and that the Tagged Queuing message will not occur while
    the partition table is being printed.  In addition, some devices do not
    properly handle the transition from non-tagged to tagged commands, so it is
    necessary to wait until there are no pending commands for a target device
    before queuing tagged commands.
  */
  HostAdapter->TaggedQueuingSupported[TargetID] =
    Command->device->tagged_supported;
  if (HostAdapter->CommandsSinceReset[TargetID]++ >=
	BusLogic_MaxTaggedQueueDepth &&
      !HostAdapter->TaggedQueuingActive[TargetID] &&
      HostAdapter->ActiveCommands[TargetID] == 0 &&
      HostAdapter->TaggedQueuingSupported[TargetID] &&
      (HostAdapter->TaggedQueuingPermitted & (1 << TargetID)))
    {
      HostAdapter->TaggedQueuingActive[TargetID] = true;
      BusLogic_Notice("Tagged Queuing now active for Target %d\n",
		      HostAdapter, TargetID);
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
	more than 3 seconds (one fifth of the 15 second disk timeout) have
	elapsed since this last sequence point, this command will be issued
	with an Ordered Queue Tag rather than a Simple Queue Tag, which forces
	the Target Device to complete all previously queued commands before
	this command may be executed.
      */
      if (HostAdapter->ActiveCommands[TargetID] == 0)
	HostAdapter->LastSequencePoint[TargetID] = jiffies;
      else if (jiffies - HostAdapter->LastSequencePoint[TargetID] > 3*HZ)
	{
	  HostAdapter->LastSequencePoint[TargetID] = jiffies;
	  QueueTag = BusLogic_OrderedQueueTag;
	}
      if (HostAdapter->ExtendedLUNSupport)
	{
	  CCB->TagEnable = true;
	  CCB->QueueTag = QueueTag;
	}
      else
	{
	  CCB->LegacyTagEnable = true;
	  CCB->LegacyQueueTag = QueueTag;
	}
    }
  memcpy(CCB->CDB, CDB, CDB_Length);
  CCB->SenseDataPointer = Virtual_to_Bus(&Command->sense_buffer);
  CCB->Command = Command;
  Command->scsi_done = CompletionRoutine;
  if (BusLogic_MultiMasterHostAdapterP(HostAdapter))
    {
      /*
	Place the CCB in an Outgoing Mailbox.  The higher levels of the SCSI
	Subsystem should not attempt to queue more commands than can be placed
	in Outgoing Mailboxes, so there should always be one free.  In the
	unlikely event that there are none available, wait 1 second and try
	again.  If that fails, the Host Adapter is probably hung so signal an
	error as a Host Adapter Hard Reset should be initiated soon.
      */
      if (!BusLogic_WriteOutgoingMailbox(
	     HostAdapter, BusLogic_MailboxStartCommand, CCB))
	{
	  BusLogic_Warning("Unable to write Outgoing Mailbox - "
			   "Pausing for 1 second\n", HostAdapter);
	  BusLogic_Delay(1);
	  if (!BusLogic_WriteOutgoingMailbox(
		 HostAdapter, BusLogic_MailboxStartCommand, CCB))
	    {
	      BusLogic_Warning("Still unable to write Outgoing Mailbox - "
			       "Host Adapter Dead?\n", HostAdapter);
	      BusLogic_DeallocateCCB(CCB);
	      Command->result = DID_ERROR << 16;
	      Command->scsi_done(Command);
	    }
	}
    }
  else
    {
      /*
	Call the FlashPoint SCCB Manager to start execution of the CCB.
      */
      CCB->Status = BusLogic_CCB_Active;
      HostAdapter->ActiveCommands[TargetID]++;
      TargetDeviceStatistics[TargetID].CommandsAttempted++;
      FlashPoint_StartCCB(HostAdapter->CardHandle, CCB);
      /*
	The Command may have already completed and BusLogic_QueueCompletedCCB
	been called, or it may still be pending.
      */
      if (CCB->Status == BusLogic_CCB_Completed)
	BusLogic_ProcessCompletedCCBs();
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
  BusLogic_IncrementErrorCounter(
    &HostAdapter->TargetDeviceStatistics[TargetID].CommandAbortsRequested);
  /*
    Acquire exclusive access to Host Adapter.
  */
  BusLogic_AcquireHostAdapterLock(HostAdapter, &Lock);
  /*
    If this Command has already completed, then no Abort is necessary.
  */
  if (Command->serial_number != Command->serial_number_at_timeout)
    {
      BusLogic_Warning("Unable to Abort Command to Target %d - "
		       "Already Completed\n", HostAdapter, TargetID);
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
      BusLogic_Warning("Unable to Abort Command to Target %d - "
		       "No CCB Found\n", HostAdapter, TargetID);
      Result = SCSI_ABORT_NOT_RUNNING;
      goto Done;
    }
  else if (CCB->Status == BusLogic_CCB_Completed)
    {
      BusLogic_Warning("Unable to Abort Command to Target %d - "
		       "CCB Completed\n", HostAdapter, TargetID);
      Result = SCSI_ABORT_NOT_RUNNING;
      goto Done;
    }
  else if (CCB->Status == BusLogic_CCB_Reset)
    {
      BusLogic_Warning("Unable to Abort Command to Target %d - "
		       "CCB Reset\n", HostAdapter, TargetID);
      Result = SCSI_ABORT_PENDING;
      goto Done;
    }
  if (BusLogic_MultiMasterHostAdapterP(HostAdapter))
    {
      /*
	Attempt to Abort this CCB.  MultiMaster Firmware versions prior to 5.xx
	do not generate Abort Tag messages, but only generate the non-tagged
	Abort message.  Since non-tagged commands are not sent by the Host
	Adapter until the queue of outstanding tagged commands has completed,
	and the Abort message is treated as a non-tagged command, it is
	effectively impossible to abort commands when Tagged Queuing is active.
	Firmware version 5.xx does generate Abort Tag messages, so it is
	possible to abort commands when Tagged Queuing is active.
      */
      if (HostAdapter->TaggedQueuingActive[TargetID] &&
	  HostAdapter->FirmwareVersion[0] < '5')
	{
	  BusLogic_Warning("Unable to Abort CCB #%ld to Target %d - "
			   "Abort Tag Not Supported\n",
			   HostAdapter, CCB->SerialNumber, TargetID);
	  Result = SCSI_ABORT_SNOOZE;
	}
      else if (BusLogic_WriteOutgoingMailbox(
		 HostAdapter, BusLogic_MailboxAbortCommand, CCB))
	{
	  BusLogic_Warning("Aborting CCB #%ld to Target %d\n",
			   HostAdapter, CCB->SerialNumber, TargetID);
	  BusLogic_IncrementErrorCounter(
	    &HostAdapter->TargetDeviceStatistics[TargetID]
			  .CommandAbortsAttempted);
	  Result = SCSI_ABORT_PENDING;
	}
      else
	{
	  BusLogic_Warning("Unable to Abort CCB #%ld to Target %d - "
			   "No Outgoing Mailboxes\n",
			    HostAdapter, CCB->SerialNumber, TargetID);
	  Result = SCSI_ABORT_BUSY;
	}
    }
  else
    {
      /*
	Call the FlashPoint SCCB Manager to abort execution of the CCB.
      */
      BusLogic_Warning("Aborting CCB #%ld to Target %d\n",
		       HostAdapter, CCB->SerialNumber, TargetID);
      BusLogic_IncrementErrorCounter(
	&HostAdapter->TargetDeviceStatistics[TargetID].CommandAbortsAttempted);
      FlashPoint_AbortCCB(HostAdapter->CardHandle, CCB);
      /*
	The Abort may have already been completed and
	BusLogic_QueueCompletedCCB been called, or it
	may still be pending.
      */
      Result = SCSI_ABORT_PENDING;
      if (CCB->Status == BusLogic_CCB_Completed)
	{
	  BusLogic_ProcessCompletedCCBs();
	  Result = SCSI_ABORT_SUCCESS;
	}
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
  if (Command == NULL)
    BusLogic_IncrementErrorCounter(&HostAdapter->ExternalHostAdapterResets);
  else BusLogic_IncrementErrorCounter(
	 &HostAdapter->TargetDeviceStatistics[Command->target]
		       .HostAdapterResetsRequested);
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
	  BusLogic_Warning("Unable to Reset Command to Target %d - "
			   "Already Completed or Reset\n",
			   HostAdapter, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
      }
      for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
	if (CCB->Command == Command) break;
      if (CCB == NULL)
	{
	  BusLogic_Warning("Unable to Reset Command to Target %d - "
			   "No CCB Found\n", HostAdapter, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      else if (CCB->Status == BusLogic_CCB_Completed)
	{
	  BusLogic_Warning("Unable to Reset Command to Target %d - "
			   "CCB Completed\n", HostAdapter, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      else if (CCB->Status == BusLogic_CCB_Reset &&
	       HostAdapter->BusDeviceResetPendingCCB[TargetID] == NULL)
	{
	  BusLogic_Warning("Unable to Reset Command to Target %d - "
			   "Reset Pending\n", HostAdapter, TargetID);
	  Result = SCSI_RESET_PENDING;
	  goto Done;
	}
    }
  if (Command == NULL)
    BusLogic_Warning("Resetting %s due to External SCSI Bus Reset\n",
		     HostAdapter, HostAdapter->FullModelName);
  else
    {
      BusLogic_Warning("Resetting %s due to Target %d\n", HostAdapter,
		       HostAdapter->FullModelName, Command->target);
      BusLogic_IncrementErrorCounter(
	&HostAdapter->TargetDeviceStatistics[Command->target]
		      .HostAdapterResetsAttempted);
    }
  /*
    Attempt to Reset and Reinitialize the Host Adapter.
  */
  if (!(BusLogic_HardResetHostAdapter(HostAdapter) &&
	BusLogic_InitializeHostAdapter(HostAdapter)))
    {
      BusLogic_Error("Resetting %s Failed\n", HostAdapter,
		     HostAdapter->FullModelName);
      Result = SCSI_RESET_ERROR;
      goto Done;
    }
  if (Command != NULL)
    BusLogic_IncrementErrorCounter(
      &HostAdapter->TargetDeviceStatistics[Command->target]
		    .HostAdapterResetsCompleted);
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
    {
      HostAdapter->LastResetAttempted[TargetID] = jiffies;
      HostAdapter->LastResetCompleted[TargetID] = jiffies;
    }
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
  BusLogic_CCB_T *CCB, *XCCB;
  BusLogic_Lock_T Lock;
  int Result = -1;
  BusLogic_IncrementErrorCounter(
    &HostAdapter->TargetDeviceStatistics[TargetID].BusDeviceResetsRequested);
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
	  BusLogic_Warning("Unable to Reset Command to Target %d - "
			   "Already Completed\n", HostAdapter, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
	if (CCB->Command == Command) break;
      if (CCB == NULL)
	{
	  BusLogic_Warning("Unable to Reset Command to Target %d - "
			   "No CCB Found\n", HostAdapter, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      else if (CCB->Status == BusLogic_CCB_Completed)
	{
	  BusLogic_Warning("Unable to Reset Command to Target %d - "
			   "CCB Completed\n", HostAdapter, TargetID);
	  Result = SCSI_RESET_NOT_RUNNING;
	  goto Done;
	}
      else if (CCB->Status == BusLogic_CCB_Reset)
	{
	  BusLogic_Warning("Unable to Reset Command to Target %d - "
			   "Reset Pending\n", HostAdapter, TargetID);
	  Result = SCSI_RESET_PENDING;
	  goto Done;
	}
      else if (HostAdapter->BusDeviceResetPendingCCB[TargetID] != NULL)
	{
	  BusLogic_Warning("Bus Device Reset already pending to Target %d\n",
			   HostAdapter, TargetID);
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
	BusLogic_Warning("Unable to Reset Command to Target %d - "
			 "Reset Pending\n", HostAdapter, TargetID);
	Result = SCSI_RESET_PENDING;
	goto Done;
      }
  if (BusLogic_MultiMasterHostAdapterP(HostAdapter))
    {
      /*
	MultiMaster Firmware versions prior to 5.xx treat a Bus Device Reset as
	a non-tagged command.  Since non-tagged commands are not sent by the
	Host Adapter until the queue of outstanding tagged commands has
	completed, it is effectively impossible to send a Bus Device Reset
	while there are tagged commands outstanding.  Therefore, in that case a
	full Host Adapter Hard Reset and SCSI Bus Reset must be done.
      */
      if (HostAdapter->TaggedQueuingActive[TargetID] &&
	  HostAdapter->ActiveCommands[TargetID] > 0 &&
	  HostAdapter->FirmwareVersion[0] < '5')
	goto Done;
    }
  /*
    Allocate a CCB from the Host Adapter's free list.  In the unlikely event
    that there are none available and memory allocation fails, attempt a full
    Host Adapter Hard Reset and SCSI Bus Reset.
  */
  CCB = BusLogic_AllocateCCB(HostAdapter);
  if (CCB == NULL) goto Done;
  BusLogic_Warning("Sending Bus Device Reset CCB #%ld to Target %d\n",
		   HostAdapter, CCB->SerialNumber, TargetID);
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
  if (BusLogic_MultiMasterHostAdapterP(HostAdapter))
    {
      /*
	Attempt to write an Outgoing Mailbox with the Bus Device Reset CCB.
	If sending a Bus Device Reset is impossible, attempt a full Host
	Adapter Hard Reset and SCSI Bus Reset.
      */
      if (!(BusLogic_WriteOutgoingMailbox(
	      HostAdapter, BusLogic_MailboxStartCommand, CCB)))
	{
	  BusLogic_Warning("Unable to write Outgoing Mailbox for "
			   "Bus Device Reset\n", HostAdapter);
	  BusLogic_DeallocateCCB(CCB);
	  goto Done;
	}
    }
  else
    {
      /*
	Call the FlashPoint SCCB Manager to start execution of the CCB.
      */
      CCB->Status = BusLogic_CCB_Active;
      HostAdapter->ActiveCommands[TargetID]++;
      FlashPoint_StartCCB(HostAdapter->CardHandle, CCB);
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
  BusLogic_IncrementErrorCounter(
    &HostAdapter->TargetDeviceStatistics[TargetID].BusDeviceResetsAttempted);
  HostAdapter->BusDeviceResetPendingCCB[TargetID] = CCB;
  HostAdapter->LastResetAttempted[TargetID] = jiffies;
  for (XCCB = HostAdapter->All_CCBs; XCCB != NULL; XCCB = XCCB->NextAll)
    if (XCCB->Status == BusLogic_CCB_Active && XCCB->TargetID == TargetID)
      XCCB->Status = BusLogic_CCB_Reset;
  /*
    FlashPoint Host Adapters may have already completed the Bus Device
    Reset and BusLogic_QueueCompletedCCB been called, or it may still be
    pending.
  */
  Result = SCSI_RESET_PENDING;
  if (BusLogic_FlashPointHostAdapterP(HostAdapter))
    if (CCB->Status == BusLogic_CCB_Completed)
      {
	BusLogic_ProcessCompletedCCBs();
	Result = SCSI_RESET_SUCCESS;
      }
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
  BusLogic_ErrorRecoveryStrategy_T
    ErrorRecoveryStrategy = HostAdapter->ErrorRecoveryStrategy[TargetID];
  /*
    Disable Tagged Queuing if it is active for this Target Device and if
    it has been less than 10 minutes since the last reset occurred, or since
    the system was initialized if no prior resets have occurred.
  */
  if (HostAdapter->TaggedQueuingActive[TargetID] &&
      jiffies - HostAdapter->LastResetCompleted[TargetID] < 10*60*HZ)
    {
      HostAdapter->TaggedQueuingPermitted &= ~(1 << TargetID);
      HostAdapter->TaggedQueuingActive[TargetID] = false;
      BusLogic_Warning("Tagged Queuing now disabled for Target %d\n",
		       HostAdapter, TargetID);
    }
  switch (ErrorRecoveryStrategy)
    {
    case BusLogic_ErrorRecovery_Default:
      if (ResetFlags & SCSI_RESET_SUGGEST_HOST_RESET)
	return BusLogic_ResetHostAdapter(HostAdapter, Command, ResetFlags);
      else if (ResetFlags & SCSI_RESET_SUGGEST_BUS_RESET)
	return BusLogic_ResetHostAdapter(HostAdapter, Command, ResetFlags);
      /* Fall through to Bus Device Reset case. */
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
	  jiffies - HostAdapter->LastResetAttempted[TargetID] < HZ/10)
	{
	  HostAdapter->CommandSuccessfulFlag[TargetID] = false;
	  return BusLogic_SendBusDeviceReset(HostAdapter, Command, ResetFlags);
	}
      /* Fall through to Hard Reset case. */
    case BusLogic_ErrorRecovery_HardReset:
      return BusLogic_ResetHostAdapter(HostAdapter, Command, ResetFlags);
    case BusLogic_ErrorRecovery_None:
      BusLogic_Warning("Error Recovery for Target %d Suppressed\n",
		       HostAdapter, TargetID);
      break;
    }
  return SCSI_RESET_PUNT;
}


/*
  BusLogic_BIOSDiskParameters returns the Heads/Sectors/Cylinders BIOS Disk
  Parameters for Disk.  The default disk geometry is 64 heads, 32 sectors, and
  the appropriate number of cylinders so as not to exceed drive capacity.  In
  order for disks equal to or larger than 1 GB to be addressable by the BIOS
  without exceeding the BIOS limitation of 1024 cylinders, Extended Translation
  may be enabled in AutoSCSI on FlashPoint Host Adapters and on "W" and "C"
  series MultiMaster Host Adapters, or by a dip switch setting on "S" and "A"
  series MultiMaster Host Adapters.  With Extended Translation enabled, drives
  between 1 GB inclusive and 2 GB exclusive are given a disk geometry of 128
  heads and 32 sectors, and drives above 2 GB inclusive are given a disk
  geometry of 255 heads and 63 sectors.  However, if the BIOS detects that the
  Extended Translation setting does not match the geometry in the partition
  table, then the translation inferred from the partition table will be used by
  the BIOS, and a warning may be displayed.
*/

int BusLogic_BIOSDiskParameters(SCSI_Disk_T *Disk, KernelDevice_T Device,
				int *Parameters)
{
  BusLogic_HostAdapter_T *HostAdapter =
    (BusLogic_HostAdapter_T *) Disk->device->host->hostdata;
  BIOS_DiskParameters_T *DiskParameters = (BIOS_DiskParameters_T *) Parameters;
  struct buffer_head *BufferHead;
  if (HostAdapter->ExtendedTranslationEnabled &&
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
	{
	  BusLogic_Warning("Warning: Extended Translation Setting "
			   "(> 1GB Switch) does not match\n", HostAdapter);
	  BusLogic_Warning("Partition Table - Adopting %d/%d Geometry "
			   "from Partition Table\n", HostAdapter,
			   DiskParameters->Heads, DiskParameters->Sectors);
	}
    }
  brelse(BufferHead);
  return 0;
}


/*
  BugLogic_ProcDirectoryInfo implements /proc/scsi/BusLogic/<N>.
*/

int BusLogic_ProcDirectoryInfo(char *ProcBuffer, char **StartPointer,
			       off_t Offset, int BytesAvailable,
			       int HostNumber, int WriteFlag)
{
  BusLogic_HostAdapter_T *HostAdapter;
  BusLogic_TargetDeviceStatistics_T *TargetDeviceStatistics;
  int IRQ_Channel, TargetID, Length;
  char *Buffer;
  if (WriteFlag) return 0;
  for (IRQ_Channel = 0; IRQ_Channel < NR_IRQS; IRQ_Channel++)
    {
      HostAdapter = BusLogic_RegisteredHostAdapters[IRQ_Channel];
      while (HostAdapter != NULL)
	{
	  if (HostAdapter->HostNumber == HostNumber) break;
	  HostAdapter = HostAdapter->Next;
	}
      if (HostAdapter != NULL) break;
    }
  if (HostAdapter == NULL) return -1;
  TargetDeviceStatistics = HostAdapter->TargetDeviceStatistics;
  Buffer = HostAdapter->MessageBuffer;
  Length = HostAdapter->MessageBufferLength;
  Length += sprintf(&Buffer[Length], "\n\
Current Driver Queue Depth:	%d\n\
Currently Allocated CCBs:	%d\n",
		    HostAdapter->DriverQueueDepth,
		    HostAdapter->AllocatedCCBs);
  Length += sprintf(&Buffer[Length], "\n\n\
			   DATA TRANSFER STATISTICS\n\
\n\
Target	Tagged Queuing	Queue Depth  Commands Attempted	 Commands Completed\n\
======	==============	===========  ==================	 ==================\n");
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    if (TargetDeviceStatistics[TargetID].CommandsCompleted > 0)
      {
	Length +=
	  sprintf(&Buffer[Length], "  %2d	%s", TargetID,
		  (HostAdapter->TaggedQueuingSupported[TargetID]
		   ? (HostAdapter->TaggedQueuingActive[TargetID]
		      ? "    Active"
		      : (HostAdapter->TaggedQueuingPermitted & (1 << TargetID)
			 ? "  Permitted" : "   Disabled"))
		   : "Not Supported"));
	Length += sprintf(&Buffer[Length],
			  "	    %3d		 %9u	     %9u\n",
			  HostAdapter->QueueDepth[TargetID],
			  TargetDeviceStatistics[TargetID].CommandsAttempted,
			  TargetDeviceStatistics[TargetID].CommandsCompleted);
      }
  Length += sprintf(&Buffer[Length], "\n\
Target  Read Commands  Write Commands   Total Bytes Read    Total Bytes Written\n\
======  =============  ==============  ===================  ===================\n");
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    if (TargetDeviceStatistics[TargetID].CommandsCompleted > 0)
      {
	Length +=
	  sprintf(&Buffer[Length], "  %2d	  %9u	 %9u", TargetID,
		  TargetDeviceStatistics[TargetID].ReadCommands,
		  TargetDeviceStatistics[TargetID].WriteCommands);
	if (TargetDeviceStatistics[TargetID].TotalBytesRead.Billions > 0)
	  Length +=
	    sprintf(&Buffer[Length], "     %9u%09u",
		    TargetDeviceStatistics[TargetID].TotalBytesRead.Billions,
		    TargetDeviceStatistics[TargetID].TotalBytesRead.Units);
	else
	  Length +=
	    sprintf(&Buffer[Length], "		%9u",
		    TargetDeviceStatistics[TargetID].TotalBytesRead.Units);
	if (TargetDeviceStatistics[TargetID].TotalBytesWritten.Billions > 0)
	  Length +=
	    sprintf(&Buffer[Length], "   %9u%09u\n",
		    TargetDeviceStatistics[TargetID].TotalBytesWritten.Billions,
		    TargetDeviceStatistics[TargetID].TotalBytesWritten.Units);
	else
	  Length +=
	    sprintf(&Buffer[Length], "	     %9u\n",
		    TargetDeviceStatistics[TargetID].TotalBytesWritten.Units);
      }
  Length += sprintf(&Buffer[Length], "\n\
Target  Command    0-1KB      1-2KB      2-4KB      4-8KB     8-16KB\n\
======  =======  =========  =========  =========  =========  =========\n");
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    if (TargetDeviceStatistics[TargetID].CommandsCompleted > 0)
      {
	Length +=
	  sprintf(&Buffer[Length],
		  "  %2d	 Read	 %9u  %9u  %9u  %9u  %9u\n", TargetID,
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[0],
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[1],
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[2],
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[3],
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[4]);
	Length +=
	  sprintf(&Buffer[Length],
		  "  %2d	 Write	 %9u  %9u  %9u  %9u  %9u\n", TargetID,
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[0],
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[1],
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[2],
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[3],
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[4]);
      }
  Length += sprintf(&Buffer[Length], "\n\
Target  Command   16-32KB    32-64KB   64-128KB   128-256KB   256KB+\n\
======  =======  =========  =========  =========  =========  =========\n");
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    if (TargetDeviceStatistics[TargetID].CommandsCompleted > 0)
      {
	Length +=
	  sprintf(&Buffer[Length],
		  "  %2d	 Read	 %9u  %9u  %9u  %9u  %9u\n", TargetID,
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[5],
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[6],
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[7],
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[8],
		  TargetDeviceStatistics[TargetID].ReadCommandSizeBuckets[9]);
	Length +=
	  sprintf(&Buffer[Length],
		  "  %2d	 Write	 %9u  %9u  %9u  %9u  %9u\n", TargetID,
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[5],
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[6],
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[7],
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[8],
		  TargetDeviceStatistics[TargetID].WriteCommandSizeBuckets[9]);
      }
  Length += sprintf(&Buffer[Length], "\n\n\
			   ERROR RECOVERY STATISTICS\n\
\n\
	  Command Aborts      Bus Device Resets	  Host Adapter Resets\n\
Target	Requested Completed  Requested Completed  Requested Completed\n\
  ID	\\\\\\\\ Attempted ////  \\\\\\\\ Attempted ////  \\\\\\\\ Attempted ////\n\
======	 ===== ===== =====    ===== ===== =====	   ===== ===== =====\n");
  for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
    if (TargetDeviceStatistics[TargetID].CommandsCompleted > 0)
      Length +=
	sprintf(&Buffer[Length], "\
  %2d	 %5d %5d %5d    %5d %5d %5d	   %5d %5d %5d\n", TargetID,
		TargetDeviceStatistics[TargetID].CommandAbortsRequested,
		TargetDeviceStatistics[TargetID].CommandAbortsAttempted,
		TargetDeviceStatistics[TargetID].CommandAbortsCompleted,
		TargetDeviceStatistics[TargetID].BusDeviceResetsRequested,
		TargetDeviceStatistics[TargetID].BusDeviceResetsAttempted,
		TargetDeviceStatistics[TargetID].BusDeviceResetsCompleted,
		TargetDeviceStatistics[TargetID].HostAdapterResetsRequested,
		TargetDeviceStatistics[TargetID].HostAdapterResetsAttempted,
		TargetDeviceStatistics[TargetID].HostAdapterResetsCompleted);
  Length += sprintf(&Buffer[Length], "\nExternal Host Adapter Resets: %d\n",
		    HostAdapter->ExternalHostAdapterResets);
  if (Length >= BusLogic_MessageBufferSize)
    BusLogic_Error("Message Buffer length %d exceeds size %d\n",
		   HostAdapter, Length, BusLogic_MessageBufferSize);
  if ((Length -= Offset) <= 0) return 0;
  if (Length >= BytesAvailable) Length = BytesAvailable;
  *StartPointer = &HostAdapter->MessageBuffer[Offset];
  return Length;
}


/*
  BusLogic_Message prints Driver Messages.
*/

static void BusLogic_Message(BusLogic_MessageLevel_T MessageLevel,
			     char *Format,
			     BusLogic_HostAdapter_T *HostAdapter,
			     ...)
{
  static char Buffer[BusLogic_LineBufferSize];
  static boolean BeginningOfLine = true;
  va_list Arguments;
  int Length = 0;
  va_start(Arguments, HostAdapter);
  Length = vsprintf(Buffer, Format, Arguments);
  va_end(Arguments);
  if (MessageLevel == BusLogic_AnnounceLevel)
    {
      static int AnnouncementLines = 0;
      strcpy(&HostAdapter->MessageBuffer[HostAdapter->MessageBufferLength],
	     Buffer);
      HostAdapter->MessageBufferLength += Length;
      if (++AnnouncementLines <= 2)
	printk("%sscsi: %s", BusLogic_MessageLevelMap[MessageLevel], Buffer);
    }
  else if (MessageLevel == BusLogic_InfoLevel)
    {
      strcpy(&HostAdapter->MessageBuffer[HostAdapter->MessageBufferLength],
	     Buffer);
      HostAdapter->MessageBufferLength += Length;
      if (BeginningOfLine)
	printk("%sscsi%d: %s", BusLogic_MessageLevelMap[MessageLevel],
	       HostAdapter->HostNumber, Buffer);
      else printk("%s", Buffer);
    }
  else
    {
      if (BeginningOfLine)
	if (HostAdapter != NULL && HostAdapter->HostAdapterInitialized)
	  printk("%sscsi%d: %s", BusLogic_MessageLevelMap[MessageLevel],
		 HostAdapter->HostNumber, Buffer);
	else printk("%s%s", BusLogic_MessageLevelMap[MessageLevel], Buffer);
      else printk("%s", Buffer);
    }
  BeginningOfLine = (Buffer[Length-1] == '\n');
}


/*
  BusLogic_Setup handles processing of Kernel Command Line Arguments.

  For the BusLogic driver, a Kernel Command Line Entry comprises the driver
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
  automatically set to BusLogic_TaggedQueueDepthBounceBuffers to avoid
  excessive preallocation of DMA Bounce Buffer memory.  Target Devices that do
  not support Tagged Queuing use a Queue Depth of BusLogic_UntaggedQueueDepth.

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
			be done, and hence only PCI MultiMaster and FlashPoint
			Host Adapters will be detected.

  NoProbePCI		No interrogation of PCI Configuration Space will be
			made, and hence only ISA Multimaster Host Adapters
			will be detected, as well as PCI Multimaster Host
			Adapters that have their ISA Compatible I/O Port
			set to "Primary" or "Alternate".

  NoSortPCI		PCI MultiMaster Host Adapters will be enumerated in
			the order provided by the PCI BIOS, ignoring any
			setting of the AutoSCSI "Use Bus And Device # For PCI
			Scanning Seq." option.

  MultiMasterFirst	By default, if both FlashPoint and PCI MultiMaster
			Host Adapters are present, this driver will probe for
			FlashPoint Host Adapters first unless the BIOS primary
			disk is controlled by the first PCI MultiMaster Host
			Adapter, in which case MultiMaster Host Adapters will
			be probed first.  This option forces MultiMaster Host
			Adapters to be probed first.

  FlashPointFirst	By default, if both FlashPoint and PCI MultiMaster
			Host Adapters are present, this driver will probe for
			FlashPoint Host Adapters first unless the BIOS primary
			disk is controlled by the first PCI MultiMaster Host
			Adapter, in which case MultiMaster Host Adapters will
			be probed first.  This option forces FlashPoint Host
			Adapters to be probed first.

  Debug			Sets all the tracing bits in BusLogic_GlobalOptions.

*/

void BusLogic_Setup(char *Strings, int *Integers)
{
  BusLogic_CommandLineEntry_T *CommandLineEntry =
    &BusLogic_CommandLineEntries[BusLogic_CommandLineEntryCount++];
  int IntegerCount = Integers[0];
  int TargetID, i;
  CommandLineEntry->IO_Address = 0;
  CommandLineEntry->TaggedQueueDepth = 0;
  CommandLineEntry->BusSettleTime = 0;
  CommandLineEntry->TaggedQueuingPermitted = 0;
  CommandLineEntry->TaggedQueuingPermittedMask = 0;
  CommandLineEntry->LocalOptions.All = 0;
  memset(CommandLineEntry->ErrorRecoveryStrategy,
	 BusLogic_ErrorRecovery_Default,
	 sizeof(CommandLineEntry->ErrorRecoveryStrategy));
  if (IntegerCount > 5)
    BusLogic_Error("BusLogic: Unexpected Command Line Integers "
		   "ignored\n", NULL);
  if (IntegerCount >= 1)
    {
      BusLogic_IO_Address_T IO_Address = Integers[1];
      if (IO_Address > 0)
	{
	  BusLogic_ProbeInfo_T *ProbeInfo;
	  for (i = 0; ; i++)
	    if (BusLogic_ISA_StandardAddresses[i] == 0)
	      {
		BusLogic_Error("BusLogic: Invalid Command Line Entry "
			       "(illegal I/O Address 0x%X)\n",
			       NULL, IO_Address);
		return;
	      }
	    else if (i < BusLogic_ProbeInfoCount &&
		     IO_Address == BusLogic_ProbeInfoList[i].IO_Address)
	      {
		BusLogic_Error("BusLogic: Invalid Command Line Entry "
			       "(duplicate I/O Address 0x%X)\n",
			       NULL, IO_Address);
		return;
	      }
	    else if (IO_Address >= 0x400 ||
		     IO_Address == BusLogic_ISA_StandardAddresses[i]) break;
	  ProbeInfo = &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount++];
	  ProbeInfo->HostAdapterType = BusLogic_MultiMaster;
	  ProbeInfo->HostAdapterBusType = BusLogic_ISA_Bus;
	}
      CommandLineEntry->IO_Address = IO_Address;
    }
  if (IntegerCount >= 2)
    {
      unsigned short TaggedQueueDepth = Integers[2];
      if (TaggedQueueDepth > BusLogic_MaxTaggedQueueDepth)
	{
	  BusLogic_Error("BusLogic: Invalid Command Line Entry "
			 "(illegal Tagged Queue Depth %d)\n",
			 NULL, TaggedQueueDepth);
	  return;
	}
      CommandLineEntry->TaggedQueueDepth = TaggedQueueDepth;
    }
  if (IntegerCount >= 3)
    CommandLineEntry->BusSettleTime = Integers[3];
  if (IntegerCount >= 4)
    CommandLineEntry->LocalOptions.All = Integers[4];
  if (IntegerCount >= 5)
    BusLogic_GlobalOptions.All |= Integers[5];
  if (!(BusLogic_CommandLineEntryCount == 0 ||
	BusLogic_ProbeInfoCount == 0 ||
	BusLogic_CommandLineEntryCount == BusLogic_ProbeInfoCount))
    {
      BusLogic_Error("BusLogic: Invalid Command Line Entry "
		     "(all or no I/O Addresses must be specified)\n", NULL);
      return;
    }
  if (Strings == NULL) return;
  while (*Strings != '\0')
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
	BusLogic_ProbeOptions.Bits.NoProbe = true;
      }
    else if (strncmp(Strings, "NoProbeISA", 10) == 0)
      {
	Strings += 10;
	BusLogic_ProbeOptions.Bits.NoProbeISA = true;
      }
    else if (strncmp(Strings, "NoProbePCI", 10) == 0)
      {
	Strings += 10;
	BusLogic_ProbeOptions.Bits.NoProbePCI = true;
      }
    else if (strncmp(Strings, "NoSortPCI", 9) == 0)
      {
	Strings += 9;
	BusLogic_ProbeOptions.Bits.NoSortPCI = true;
      }
    else if (strncmp(Strings, "MultiMasterFirst", 16) == 0)
      {
	Strings += 16;
	BusLogic_ProbeOptions.Bits.ProbeMultiMasterFirst = true;
      }
    else if (strncmp(Strings, "FlashPointFirst", 15) == 0)
      {
	Strings += 15;
	BusLogic_ProbeOptions.Bits.ProbeFlashPointFirst = true;
      }
    else if (strncmp(Strings, "Debug", 5) == 0)
      {
	Strings += 5;
	BusLogic_GlobalOptions.Bits.TraceProbe = true;
	BusLogic_GlobalOptions.Bits.TraceHardReset = true;
	BusLogic_GlobalOptions.Bits.TraceConfiguration = true;
	BusLogic_GlobalOptions.Bits.TraceErrors = true;
      }
    else if (*Strings == ',')
      Strings++;
    else
      {
	BusLogic_Error("BusLogic: Unexpected Command Line String '%s' "
		       "ignored\n", NULL, Strings);
	break;
      }
}


/*
  Include Module support if requested.
*/

#ifdef MODULE

SCSI_Host_Template_T driver_template = BUSLOGIC;

#include "scsi_module.c"

#endif

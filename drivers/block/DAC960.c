/*

  Linux Driver for Mylex DAC960 and DAC1100 PCI RAID Controllers

  Copyright 1998-1999 by Leonard N. Zubkoff <lnz@dandelion.com>

  This program is free software; you may redistribute and/or modify it under
  the terms of the GNU General Public License Version 2 as published by the
  Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for complete details.

  The author respectfully requests that any modifications to this software be
  sent directly to him for evaluation and testing.

*/


#define DAC960_DriverVersion			"2.3.4"
#define DAC960_DriverDate			"23 September 1999"


#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/blk.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>
#include <linux/reboot.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include "DAC960.h"


/*
  DAC960_ControllerCount is the number of DAC960 Controllers detected.
*/

static int
  DAC960_ControllerCount =			0;


/*
  DAC960_ActiveControllerCount is the number of Active DAC960 Controllers
  detected.
*/

static int
  DAC960_ActiveControllerCount =		0;


/*
  DAC960_Controllers is an array of pointers to the DAC960 Controller
  structures.
*/

static DAC960_Controller_T
  *DAC960_Controllers[DAC960_MaxControllers] =	{ NULL };


/*
  DAC960_FileOperations is the File Operations structure for DAC960 Logical
  Disk Devices.
  Leonard, no offence, but _where_ did this C dialect come from?
*/

static struct block_device_operations DAC960_FileOperations = { 
	open:		DAC960_Open,
	release:	DAC960_Release,
	ioctl:		DAC960_IOCTL,
};

/*
  DAC960_ProcDirectoryEntry is the DAC960 /proc/driver/rd directory entry.
*/

static PROC_DirectoryEntry_T *
  DAC960_ProcDirectoryEntry = NULL;


/*
  DAC960_NotifierBlock is the Notifier Block structure for DAC960 Driver.
*/

static NotifierBlock_T
  DAC960_NotifierBlock =    { DAC960_Finalize, NULL, 0 };


/*
  DAC960_AnnounceDriver announces the Driver Version and Date, Author's Name,
  Copyright Notice, and Electronic Mail Address.
*/

static void DAC960_AnnounceDriver(DAC960_Controller_T *Controller)
{
  DAC960_Announce("***** DAC960 RAID Driver Version "
		  DAC960_DriverVersion " of "
		  DAC960_DriverDate " *****\n", Controller);
  DAC960_Announce("Copyright 1998-1999 by Leonard N. Zubkoff "
		  "<lnz@dandelion.com>\n", Controller);
}


/*
  DAC960_Failure prints a standardized error message, and then returns false.
*/

static boolean DAC960_Failure(DAC960_Controller_T *Controller,
			      char *ErrorMessage)
{
  DAC960_Error("While configuring DAC960 PCI RAID Controller at\n",
	       Controller);
  if (Controller->IO_Address == 0)
    DAC960_Error("PCI Bus %d Device %d Function %d I/O Address N/A "
		 "PCI Address 0x%X\n", Controller,
		 Controller->Bus, Controller->Device,
		 Controller->Function, Controller->PCI_Address);
  else DAC960_Error("PCI Bus %d Device %d Function %d I/O Address "
		    "0x%X PCI Address 0x%X\n", Controller,
		    Controller->Bus, Controller->Device,
		    Controller->Function, Controller->IO_Address,
		    Controller->PCI_Address);
  DAC960_Error("%s FAILED - DETACHING\n", Controller, ErrorMessage);
  return false;
}


/*
  DAC960_ClearCommand clears critical fields of Command.
*/

static inline void DAC960_ClearCommand(DAC960_Command_T *Command)
{
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  CommandMailbox->Words[0] = 0;
  CommandMailbox->Words[1] = 0;
  CommandMailbox->Words[2] = 0;
  CommandMailbox->Words[3] = 0;
  Command->CommandStatus = 0;
}


/*
  DAC960_AllocateCommand allocates a Command structure from Controller's
  free list.
*/

static inline DAC960_Command_T *DAC960_AllocateCommand(DAC960_Controller_T
						       *Controller)
{
  DAC960_Command_T *Command = Controller->FreeCommands;
  if (Command == NULL) return NULL;
  Controller->FreeCommands = Command->Next;
  Command->Next = NULL;
  return Command;
}


/*
  DAC960_DeallocateCommand deallocates Command, returning it to Controller's
  free list.
*/

static inline void DAC960_DeallocateCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  Command->Next = Controller->FreeCommands;
  Controller->FreeCommands = Command;
}


/*
  DAC960_QueueCommand queues Command.
*/

static void DAC960_QueueCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  DAC960_CommandMailbox_T *NextCommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command - Controller->Commands;
  switch (Controller->ControllerType)
    {
    case DAC960_V5_Controller:
      NextCommandMailbox = Controller->NextCommandMailbox;
      DAC960_V5_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
      if (Controller->PreviousCommandMailbox1->Words[0] == 0 ||
	  Controller->PreviousCommandMailbox2->Words[0] == 0)
	{
	  if (Controller->DualModeMemoryMailboxInterface)
	    DAC960_V5_MemoryMailboxNewCommand(ControllerBaseAddress);
	  else DAC960_V5_HardwareMailboxNewCommand(ControllerBaseAddress);
	}
      Controller->PreviousCommandMailbox2 = Controller->PreviousCommandMailbox1;
      Controller->PreviousCommandMailbox1 = NextCommandMailbox;
      if (++NextCommandMailbox > Controller->LastCommandMailbox)
	NextCommandMailbox = Controller->FirstCommandMailbox;
      Controller->NextCommandMailbox = NextCommandMailbox;
      break;
    case DAC960_V4_Controller:
      NextCommandMailbox = Controller->NextCommandMailbox;
      DAC960_V4_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
      if (Controller->PreviousCommandMailbox1->Words[0] == 0 ||
	  Controller->PreviousCommandMailbox2->Words[0] == 0)
	{
	  if (Controller->DualModeMemoryMailboxInterface)
	    DAC960_V4_MemoryMailboxNewCommand(ControllerBaseAddress);
	  else DAC960_V4_HardwareMailboxNewCommand(ControllerBaseAddress);
	}
      Controller->PreviousCommandMailbox2 = Controller->PreviousCommandMailbox1;
      Controller->PreviousCommandMailbox1 = NextCommandMailbox;
      if (++NextCommandMailbox > Controller->LastCommandMailbox)
	NextCommandMailbox = Controller->FirstCommandMailbox;
      Controller->NextCommandMailbox = NextCommandMailbox;
      break;
    case DAC960_V3_Controller:
      while (DAC960_V3_MailboxFullP(ControllerBaseAddress))
	udelay(1);
      DAC960_V3_WriteCommandMailbox(ControllerBaseAddress, CommandMailbox);
      DAC960_V3_NewCommand(ControllerBaseAddress);
      break;
    }
}


/*
  DAC960_ExecuteCommand executes Command and waits for completion.  It
  returns true on success and false on failure.
*/

static boolean DAC960_ExecuteCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DECLARE_MUTEX_LOCKED(Semaphore);
  unsigned long ProcessorFlags;
  Command->Semaphore = &Semaphore;
  DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
  DAC960_QueueCommand(Command);
  DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
  if (!in_interrupt())
    down(&Semaphore);
  return Command->CommandStatus == DAC960_NormalCompletion;
}


/*
  DAC960_ExecuteType3 executes a DAC960 Type 3 Command and waits for
  completion.  It returns true on success and false on failure.
*/

static boolean DAC960_ExecuteType3(DAC960_Controller_T *Controller,
				   DAC960_CommandOpcode_T CommandOpcode,
				   void *DataPointer)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  boolean Result;
  DAC960_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->Type3.CommandOpcode = CommandOpcode;
  CommandMailbox->Type3.BusAddress = Virtual_to_Bus(DataPointer);
  Result = DAC960_ExecuteCommand(Command);
  DAC960_DeallocateCommand(Command);
  return Result;
}


/*
  DAC960_ExecuteType3D executes a DAC960 Type 3D Command and waits for
  completion.  It returns true on success and false on failure.
*/

static boolean DAC960_ExecuteType3D(DAC960_Controller_T *Controller,
				    DAC960_CommandOpcode_T CommandOpcode,
				    unsigned char Channel,
				    unsigned char TargetID,
				    void *DataPointer)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  boolean Result;
  DAC960_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->Type3D.CommandOpcode = CommandOpcode;
  CommandMailbox->Type3D.Channel = Channel;
  CommandMailbox->Type3D.TargetID = TargetID;
  CommandMailbox->Type3D.BusAddress = Virtual_to_Bus(DataPointer);
  Result = DAC960_ExecuteCommand(Command);
  DAC960_DeallocateCommand(Command);
  return Result;
}


/*
  DAC960_EnableMemoryMailboxInterface enables the Memory Mailbox Interface.
*/

static boolean DAC960_EnableMemoryMailboxInterface(DAC960_Controller_T
						   *Controller)
{
  void *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_CommandMailbox_T *CommandMailboxesMemory;
  DAC960_StatusMailbox_T *StatusMailboxesMemory;
  DAC960_CommandMailbox_T CommandMailbox;
  DAC960_CommandStatus_T CommandStatus;
  void *SavedMemoryMailboxesAddress = NULL;
  short NextCommandMailboxIndex = 0;
  short NextStatusMailboxIndex = 0;
  int TimeoutCounter = 1000000, i;
  if (Controller->ControllerType == DAC960_V5_Controller)
    DAC960_V5_RestoreMemoryMailboxInfo(Controller,
				       &SavedMemoryMailboxesAddress,
				       &NextCommandMailboxIndex,
				       &NextStatusMailboxIndex);
  else DAC960_V4_RestoreMemoryMailboxInfo(Controller,
					  &SavedMemoryMailboxesAddress,
					  &NextCommandMailboxIndex,
					  &NextStatusMailboxIndex);
  if (SavedMemoryMailboxesAddress == NULL)
    CommandMailboxesMemory =
      (DAC960_CommandMailbox_T *) __get_free_pages(GFP_KERNEL, 1);
  else CommandMailboxesMemory = SavedMemoryMailboxesAddress;
  memset(CommandMailboxesMemory, 0, PAGE_SIZE << 1);
  Controller->FirstCommandMailbox = CommandMailboxesMemory;
  CommandMailboxesMemory += DAC960_CommandMailboxCount - 1;
  Controller->LastCommandMailbox = CommandMailboxesMemory;
  Controller->NextCommandMailbox =
    &Controller->FirstCommandMailbox[NextCommandMailboxIndex];
  if (--NextCommandMailboxIndex < 0)
    NextCommandMailboxIndex = DAC960_CommandMailboxCount - 1;
  Controller->PreviousCommandMailbox1 =
    &Controller->FirstCommandMailbox[NextCommandMailboxIndex];
  if (--NextCommandMailboxIndex < 0)
    NextCommandMailboxIndex = DAC960_CommandMailboxCount - 1;
  Controller->PreviousCommandMailbox2 =
    &Controller->FirstCommandMailbox[NextCommandMailboxIndex];
  StatusMailboxesMemory =
    (DAC960_StatusMailbox_T *) (CommandMailboxesMemory + 1);
  Controller->FirstStatusMailbox = StatusMailboxesMemory;
  StatusMailboxesMemory += DAC960_StatusMailboxCount - 1;
  Controller->LastStatusMailbox = StatusMailboxesMemory;
  Controller->NextStatusMailbox =
    &Controller->FirstStatusMailbox[NextStatusMailboxIndex];
  if (SavedMemoryMailboxesAddress != NULL) return true;
  /* Enable the Memory Mailbox Interface. */
  Controller->DualModeMemoryMailboxInterface = true;
  CommandMailbox.TypeX.CommandOpcode = 0x2B;
  CommandMailbox.TypeX.CommandIdentifier = 0;
  CommandMailbox.TypeX.CommandOpcode2 = 0x14;
  CommandMailbox.TypeX.CommandMailboxesBusAddress =
    Virtual_to_Bus(Controller->FirstCommandMailbox);
  CommandMailbox.TypeX.StatusMailboxesBusAddress =
    Virtual_to_Bus(Controller->FirstStatusMailbox);
  for (i = 0; i < 2; i++)
    switch (Controller->ControllerType)
      {
      case DAC960_V5_Controller:
	while (--TimeoutCounter >= 0)
	  {
	    if (DAC960_V5_HardwareMailboxEmptyP(ControllerBaseAddress))
	      break;
	    udelay(10);
	  }
	if (TimeoutCounter < 0) return false;
	DAC960_V5_WriteHardwareMailbox(ControllerBaseAddress, &CommandMailbox);
	DAC960_V5_HardwareMailboxNewCommand(ControllerBaseAddress);
	while (--TimeoutCounter >= 0)
	  {
	    if (DAC960_V5_HardwareMailboxStatusAvailableP(
		  ControllerBaseAddress))
	      break;
	    udelay(10);
	  }
	if (TimeoutCounter < 0) return false;
	CommandStatus = DAC960_V5_ReadStatusRegister(ControllerBaseAddress);
	DAC960_V5_AcknowledgeHardwareMailboxInterrupt(ControllerBaseAddress);
	DAC960_V5_AcknowledgeHardwareMailboxStatus(ControllerBaseAddress);
	if (CommandStatus == DAC960_NormalCompletion) return true;
	Controller->DualModeMemoryMailboxInterface = false;
	CommandMailbox.TypeX.CommandOpcode2 = 0x10;
	break;
      case DAC960_V4_Controller:
	while (--TimeoutCounter >= 0)
	  {
	    if (!DAC960_V4_HardwareMailboxFullP(ControllerBaseAddress))
	      break;
	    udelay(10);
	  }
	if (TimeoutCounter < 0) return false;
	DAC960_V4_WriteHardwareMailbox(ControllerBaseAddress, &CommandMailbox);
	DAC960_V4_HardwareMailboxNewCommand(ControllerBaseAddress);
	while (--TimeoutCounter >= 0)
	  {
	    if (DAC960_V4_HardwareMailboxStatusAvailableP(
		  ControllerBaseAddress))
	      break;
	    udelay(10);
	  }
	if (TimeoutCounter < 0) return false;
	CommandStatus = DAC960_V4_ReadStatusRegister(ControllerBaseAddress);
	DAC960_V4_AcknowledgeHardwareMailboxInterrupt(ControllerBaseAddress);
	DAC960_V4_AcknowledgeHardwareMailboxStatus(ControllerBaseAddress);
	if (CommandStatus == DAC960_NormalCompletion) return true;
	Controller->DualModeMemoryMailboxInterface = false;
	CommandMailbox.TypeX.CommandOpcode2 = 0x10;
	break;
      default:
	break;
      }
  return false;
}


/*
  DAC960_DetectControllers detects DAC960 PCI RAID Controllers by interrogating
  the PCI Configuration Space for Controller Type.
*/

static void DAC960_DetectControllers(DAC960_ControllerType_T ControllerType)
{
  unsigned short VendorID = 0, DeviceID = 0;
  unsigned int MemoryWindowSize = 0;
  PCI_Device_T *PCI_Device = NULL;
  switch (ControllerType)
    {
    case DAC960_V5_Controller:
      VendorID = PCI_VENDOR_ID_DEC;
      DeviceID = PCI_DEVICE_ID_DEC_21285;
      MemoryWindowSize = DAC960_V5_RegisterWindowSize;
      break;
    case DAC960_V4_Controller:
      VendorID = PCI_VENDOR_ID_MYLEX;
      DeviceID = PCI_DEVICE_ID_MYLEX_DAC960P_V4;
      MemoryWindowSize = DAC960_V4_RegisterWindowSize;
      break;
    case DAC960_V3_Controller:
      VendorID = PCI_VENDOR_ID_MYLEX;
      DeviceID = PCI_DEVICE_ID_MYLEX_DAC960P_V3;
      MemoryWindowSize = DAC960_V3_RegisterWindowSize;
      break;
    }
  while ((PCI_Device = pci_find_device(VendorID, DeviceID, PCI_Device)) != NULL)
    {
      DAC960_Controller_T *Controller = (DAC960_Controller_T *)
	kmalloc(sizeof(DAC960_Controller_T), GFP_ATOMIC);
      DAC960_IO_Address_T IO_Address = 0;
      DAC960_PCI_Address_T PCI_Address = 0;
      unsigned char Bus = PCI_Device->bus->number;
      unsigned char DeviceFunction = PCI_Device->devfn;
      unsigned char Device = DeviceFunction >> 3;
      unsigned char Function = DeviceFunction & 0x7;
      unsigned int IRQ_Channel = PCI_Device->irq;
      unsigned long BaseAddress0 = pci_resource_start (PCI_Device, 0);
      unsigned long BaseAddress1 = pci_resource_start (PCI_Device, 1);
      unsigned short SubsystemVendorID, SubsystemDeviceID;
      int CommandIdentifier;

      if (pci_enable_device(PCI_Device))
	  goto Ignore;

      SubsystemVendorID = PCI_Device->subsystem_vendor;
      SubsystemDeviceID = PCI_Device->subsystem_device;
      switch (ControllerType)
	{
	case DAC960_V5_Controller:
	  if (!(SubsystemVendorID == PCI_VENDOR_ID_MYLEX &&
		SubsystemDeviceID == PCI_DEVICE_ID_MYLEX_DAC960P_V5))
	    goto Ignore;
	  PCI_Address = BaseAddress0;
	  break;
	case DAC960_V4_Controller:
	  PCI_Address = BaseAddress0;
	  break;
	case DAC960_V3_Controller:
	  IO_Address = BaseAddress0;
	  PCI_Address = BaseAddress1;
	  break;
	}
      if (DAC960_ControllerCount == DAC960_MaxControllers)
	{
	  DAC960_Error("More than %d DAC960 Controllers detected - "
		       "ignoring from Controller at\n",
		       NULL, DAC960_MaxControllers);
	  goto Ignore;
	}
      if (Controller == NULL)
	{
	  DAC960_Error("Unable to allocate Controller structure for "
		       "Controller at\n", NULL);
	  goto Ignore;
	}
      memset(Controller, 0, sizeof(DAC960_Controller_T));
      init_waitqueue_head(&Controller->CommandWaitQueue);
      Controller->ControllerNumber = DAC960_ControllerCount;
      DAC960_Controllers[DAC960_ControllerCount++] = Controller;
      DAC960_AnnounceDriver(Controller);
      Controller->ControllerType = ControllerType;
      Controller->IO_Address = IO_Address;
      Controller->PCI_Address = PCI_Address;
      Controller->Bus = Bus;
      Controller->Device = Device;
      Controller->Function = Function;
      sprintf(Controller->ControllerName, "c%d", Controller->ControllerNumber);
      /*
	Acquire shared access to the IRQ Channel.
      */
      if (IRQ_Channel == 0)
	{
	  DAC960_Error("IRQ Channel %d illegal for Controller at\n",
		       Controller, IRQ_Channel);
	  goto Failure;
	}
      strcpy(Controller->FullModelName, "DAC960");
      if (request_irq(IRQ_Channel, DAC960_InterruptHandler,
		      SA_SHIRQ, Controller->FullModelName, Controller) < 0)
	{
	  DAC960_Error("Unable to acquire IRQ Channel %d for Controller at\n",
		       Controller, IRQ_Channel);
	  goto Failure;
	}
      Controller->IRQ_Channel = IRQ_Channel;
      /*
	Map the Controller Register Window.
      */
      if (MemoryWindowSize < PAGE_SIZE)
	MemoryWindowSize = PAGE_SIZE;
      Controller->MemoryMappedAddress =
	ioremap_nocache(PCI_Address & PAGE_MASK, MemoryWindowSize);
      Controller->BaseAddress =
	Controller->MemoryMappedAddress + (PCI_Address & ~PAGE_MASK);
      if (Controller->MemoryMappedAddress == NULL)
	{
	  DAC960_Error("Unable to map Controller Register Window for "
		       "Controller at\n", Controller);
	  goto Failure;
	}
      switch (ControllerType)
	{
	case DAC960_V5_Controller:
	  DAC960_V5_DisableInterrupts(Controller->BaseAddress);
	  if (!DAC960_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to Enable Memory Mailbox Interface "
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_V5_EnableInterrupts(Controller->BaseAddress);
	  break;
	case DAC960_V4_Controller:
	  DAC960_V4_DisableInterrupts(Controller->BaseAddress);
	  if (!DAC960_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to Enable Memory Mailbox Interface "
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_V4_EnableInterrupts(Controller->BaseAddress);
	  break;
	case DAC960_V3_Controller:
	  request_region(Controller->IO_Address, 0x80,
			 Controller->FullModelName);
	  DAC960_V3_EnableInterrupts(Controller->BaseAddress);
	  break;
	}
      DAC960_ActiveControllerCount++;
      for (CommandIdentifier = 0;
	   CommandIdentifier < DAC960_MaxChannels;
	   CommandIdentifier++)
	{
	  Controller->Commands[CommandIdentifier].Controller = Controller;
	  Controller->Commands[CommandIdentifier].Next =
	    Controller->FreeCommands;
	  Controller->FreeCommands = &Controller->Commands[CommandIdentifier];
	}
      continue;
    Failure:
      if (IO_Address == 0)
	DAC960_Error("PCI Bus %d Device %d Function %d I/O Address N/A "
		     "PCI Address 0x%X\n", Controller,
		     Bus, Device, Function, PCI_Address);
      else DAC960_Error("PCI Bus %d Device %d Function %d I/O Address "
			"0x%X PCI Address 0x%X\n", Controller,
			Bus, Device, Function, IO_Address, PCI_Address);
      if (Controller == NULL) break;
      if (Controller->IRQ_Channel > 0)
	free_irq(IRQ_Channel, Controller);
      if (Controller->MemoryMappedAddress != NULL)
	iounmap(Controller->MemoryMappedAddress);
      DAC960_Controllers[Controller->ControllerNumber] = NULL;
    Ignore:
      kfree(Controller);
    }
}


/*
  DAC960_ReadControllerConfiguration reads the Configuration Information
  from Controller and initializes the Controller structure.
*/

static boolean DAC960_ReadControllerConfiguration(DAC960_Controller_T
						  *Controller)
{
  DAC960_Enquiry2_T Enquiry2;
  DAC960_Config2_T Config2;
  int LogicalDriveNumber, Channel, TargetID;
  if (!DAC960_ExecuteType3(Controller, DAC960_Enquiry,
			   &Controller->Enquiry[0]))
    return DAC960_Failure(Controller, "ENQUIRY");
  if (!DAC960_ExecuteType3(Controller, DAC960_Enquiry2, &Enquiry2))
    return DAC960_Failure(Controller, "ENQUIRY2");
  if (!DAC960_ExecuteType3(Controller, DAC960_ReadConfig2, &Config2))
    return DAC960_Failure(Controller, "READ CONFIG2");
  if (!DAC960_ExecuteType3(Controller, DAC960_GetLogicalDriveInformation,
			   &Controller->LogicalDriveInformation[0]))
    return DAC960_Failure(Controller, "GET LOGICAL DRIVE INFORMATION");
  for (Channel = 0; Channel < Enquiry2.ActualChannels; Channel++)
    for (TargetID = 0; TargetID < DAC960_MaxTargets; TargetID++)
      if (!DAC960_ExecuteType3D(Controller, DAC960_GetDeviceState,
				Channel, TargetID,
				&Controller->DeviceState[0][Channel][TargetID]))
	return DAC960_Failure(Controller, "GET DEVICE STATE");
  /*
    Initialize the Controller Model Name and Full Model Name fields.
  */
  switch (Enquiry2.HardwareID.SubModel)
    {
    case DAC960_P_PD_PU:
      if (Enquiry2.SCSICapability.BusSpeed == DAC960_Ultra)
	strcpy(Controller->ModelName, "DAC960PU");
      else strcpy(Controller->ModelName, "DAC960PD");
      break;
    case DAC960_PL:
      strcpy(Controller->ModelName, "DAC960PL");
      break;
    case DAC960_PG:
      strcpy(Controller->ModelName, "DAC960PG");
      break;
    case DAC960_PJ:
      strcpy(Controller->ModelName, "DAC960PJ");
      break;
    case DAC960_PR:
      strcpy(Controller->ModelName, "DAC960PR");
      break;
    case DAC960_PT:
      strcpy(Controller->ModelName, "DAC960PT");
      break;
    case DAC960_PTL0:
      strcpy(Controller->ModelName, "DAC960PTL0");
      break;
    case DAC960_PRL:
      strcpy(Controller->ModelName, "DAC960PRL");
      break;
    case DAC960_PTL1:
      strcpy(Controller->ModelName, "DAC960PTL1");
      break;
    case DAC1164_P:
      strcpy(Controller->ModelName, "DAC1164P");
      break;
    default:
      return DAC960_Failure(Controller, "MODEL VERIFICATION");
    }
  strcpy(Controller->FullModelName, "Mylex ");
  strcat(Controller->FullModelName, Controller->ModelName);
  /*
    Initialize the Controller Firmware Version field and verify that it
    is a supported firmware version.  The supported firmware versions are:

    DAC1164P		    5.06 and above
    DAC960PTL/PRL/PJ/PG	    4.06 and above
    DAC960PU/PD/PL	    3.51 and above
  */
  sprintf(Controller->FirmwareVersion, "%d.%02d-%c-%02d",
	  Enquiry2.FirmwareID.MajorVersion, Enquiry2.FirmwareID.MinorVersion,
	  Enquiry2.FirmwareID.FirmwareType, Enquiry2.FirmwareID.TurnID);
  if (!((Controller->FirmwareVersion[0] == '5' &&
	 strcmp(Controller->FirmwareVersion, "5.06") >= 0) ||
	(Controller->FirmwareVersion[0] == '4' &&
	 strcmp(Controller->FirmwareVersion, "4.06") >= 0) ||
	(Controller->FirmwareVersion[0] == '3' &&
	 strcmp(Controller->FirmwareVersion, "3.51") >= 0)))
    {
      DAC960_Failure(Controller, "FIRMWARE VERSION VERIFICATION");
      DAC960_Error("Firmware Version = '%s'\n", Controller,
		   Controller->FirmwareVersion);
      return false;
    }
  /*
    Initialize the Controller Channels, Memory Size, and SAF-TE Enclosure
    Management Enabled fields.
  */
  Controller->Channels = Enquiry2.ActualChannels;
  Controller->MemorySize = Enquiry2.MemorySize >> 20;
  Controller->SAFTE_EnclosureManagementEnabled =
    Enquiry2.FaultManagementType == DAC960_SAFTE;
  /*
    Initialize the Controller Queue Depth, Driver Queue Depth, Logical Drive
    Count, Maximum Blocks per Command, and Maximum Scatter/Gather Segments.
    The Driver Queue Depth must be at most one less than the Controller Queue
    Depth to allow for an automatic drive rebuild operation.
  */
  Controller->ControllerQueueDepth = Controller->Enquiry[0].MaxCommands;
  Controller->DriverQueueDepth = Controller->ControllerQueueDepth - 1;
  Controller->LogicalDriveCount = Controller->Enquiry[0].NumberOfLogicalDrives;
  Controller->MaxBlocksPerCommand = Enquiry2.MaxBlocksPerCommand;
  Controller->MaxScatterGatherSegments = Enquiry2.MaxScatterGatherEntries;
  /*
    Initialize the Stripe Size, Segment Size, and Geometry Translation.
  */
  Controller->StripeSize = Config2.BlocksPerStripe * Config2.BlockFactor
			   >> (10 - DAC960_BlockSizeBits);
  Controller->SegmentSize = Config2.BlocksPerCacheLine * Config2.BlockFactor
			    >> (10 - DAC960_BlockSizeBits);
  switch (Config2.DriveGeometry)
    {
    case DAC960_Geometry_128_32:
      Controller->GeometryTranslationHeads = 128;
      Controller->GeometryTranslationSectors = 32;
      break;
    case DAC960_Geometry_255_63:
      Controller->GeometryTranslationHeads = 255;
      Controller->GeometryTranslationSectors = 63;
      break;
    default:
      return DAC960_Failure(Controller, "CONFIG2 DRIVE GEOMETRY");
    }
  /*
    Initialize the Logical Drive Initial State.
  */
  for (LogicalDriveNumber = 0;
       LogicalDriveNumber < Controller->LogicalDriveCount;
       LogicalDriveNumber++)
    Controller->LogicalDriveInitialState[LogicalDriveNumber] =
      Controller->LogicalDriveInformation[0]
		  [LogicalDriveNumber].LogicalDriveState;
  Controller->LastRebuildStatus = DAC960_NoRebuildOrCheckInProgress;
  return true;
}


/*
  DAC960_ReportControllerConfiguration reports the Configuration Information of
  Controller.
*/

static boolean DAC960_ReportControllerConfiguration(DAC960_Controller_T
						    *Controller)
{
  DAC960_Info("Configuring Mylex %s PCI RAID Controller\n",
	      Controller, Controller->ModelName);
  DAC960_Info("  Firmware Version: %s, Channels: %d, Memory Size: %dMB\n",
	      Controller, Controller->FirmwareVersion,
	      Controller->Channels, Controller->MemorySize);
  DAC960_Info("  PCI Bus: %d, Device: %d, Function: %d, I/O Address: ",
	      Controller, Controller->Bus,
	      Controller->Device, Controller->Function);
  if (Controller->IO_Address == 0)
    DAC960_Info("Unassigned\n", Controller);
  else DAC960_Info("0x%X\n", Controller, Controller->IO_Address);
  DAC960_Info("  PCI Address: 0x%X mapped at 0x%lX, IRQ Channel: %d\n",
	      Controller, Controller->PCI_Address,
	      (unsigned long) Controller->BaseAddress,
	      Controller->IRQ_Channel);
  DAC960_Info("  Controller Queue Depth: %d, "
	      "Maximum Blocks per Command: %d\n",
	      Controller, Controller->ControllerQueueDepth,
	      Controller->MaxBlocksPerCommand);
  DAC960_Info("  Driver Queue Depth: %d, "
	      "Maximum Scatter/Gather Segments: %d\n",
	      Controller, Controller->DriverQueueDepth,
	      Controller->MaxScatterGatherSegments);
  DAC960_Info("  Stripe Size: %dKB, Segment Size: %dKB, "
	      "BIOS Geometry: %d/%d\n", Controller,
	      Controller->StripeSize,
	      Controller->SegmentSize,
	      Controller->GeometryTranslationHeads,
	      Controller->GeometryTranslationSectors);
  if (Controller->SAFTE_EnclosureManagementEnabled)
    DAC960_Info("  SAF-TE Enclosure Management Enabled\n", Controller);
  return true;
}


/*
  DAC960_ReadDeviceConfiguration reads the Device Configuration Information by
  requesting the SCSI Inquiry and SCSI Inquiry Unit Serial Number information
  for each device connected to Controller.
*/

static boolean DAC960_ReadDeviceConfiguration(DAC960_Controller_T *Controller)
{
  DAC960_DCDB_T DCDBs[DAC960_MaxChannels], *DCDB;
  Semaphore_T Semaphores[DAC960_MaxChannels], *Semaphore;
  unsigned long ProcessorFlags;
  int Channel, TargetID;
  for (TargetID = 0; TargetID < DAC960_MaxTargets; TargetID++)
    {
      for (Channel = 0; Channel < Controller->Channels; Channel++)
	{
	  DAC960_Command_T *Command = &Controller->Commands[Channel];
	  DAC960_SCSI_Inquiry_T *InquiryStandardData =
	    &Controller->InquiryStandardData[Channel][TargetID];
	  InquiryStandardData->PeripheralDeviceType = 0x1F;
	  Semaphore = &Semaphores[Channel];
	  init_MUTEX_LOCKED(Semaphore);
	  DCDB = &DCDBs[Channel];
	  DAC960_ClearCommand(Command);
	  Command->CommandType = DAC960_ImmediateCommand;
	  Command->Semaphore = Semaphore;
	  Command->CommandMailbox.Type3.CommandOpcode = DAC960_DCDB;
	  Command->CommandMailbox.Type3.BusAddress = Virtual_to_Bus(DCDB);
	  DCDB->Channel = Channel;
	  DCDB->TargetID = TargetID;
	  DCDB->Direction = DAC960_DCDB_DataTransferDeviceToSystem;
	  DCDB->EarlyStatus = false;
	  DCDB->Timeout = DAC960_DCDB_Timeout_10_seconds;
	  DCDB->NoAutomaticRequestSense = false;
	  DCDB->DisconnectPermitted = true;
	  DCDB->TransferLength = sizeof(DAC960_SCSI_Inquiry_T);
	  DCDB->BusAddress = Virtual_to_Bus(InquiryStandardData);
	  DCDB->CDBLength = 6;
	  DCDB->TransferLengthHigh4 = 0;
	  DCDB->SenseLength = sizeof(DCDB->SenseData);
	  DCDB->CDB[0] = 0x12; /* INQUIRY */
	  DCDB->CDB[1] = 0; /* EVPD = 0 */
	  DCDB->CDB[2] = 0; /* Page Code */
	  DCDB->CDB[3] = 0; /* Reserved */
	  DCDB->CDB[4] = sizeof(DAC960_SCSI_Inquiry_T);
	  DCDB->CDB[5] = 0; /* Control */
	  DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
	  DAC960_QueueCommand(Command);
	  DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
	}
      for (Channel = 0; Channel < Controller->Channels; Channel++)
	{
	  DAC960_Command_T *Command = &Controller->Commands[Channel];
	  DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
	    &Controller->InquiryUnitSerialNumber[Channel][TargetID];
	  InquiryUnitSerialNumber->PeripheralDeviceType = 0x1F;
	  Semaphore = &Semaphores[Channel];
	  down(Semaphore);
	  if (Command->CommandStatus != DAC960_NormalCompletion) continue;
	  Command->Semaphore = Semaphore;
	  DCDB = &DCDBs[Channel];
	  DCDB->TransferLength = sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
	  DCDB->BusAddress = Virtual_to_Bus(InquiryUnitSerialNumber);
	  DCDB->SenseLength = sizeof(DCDB->SenseData);
	  DCDB->CDB[0] = 0x12; /* INQUIRY */
	  DCDB->CDB[1] = 1; /* EVPD = 1 */
	  DCDB->CDB[2] = 0x80; /* Page Code */
	  DCDB->CDB[3] = 0; /* Reserved */
	  DCDB->CDB[4] = sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
	  DCDB->CDB[5] = 0; /* Control */
	  DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
	  DAC960_QueueCommand(Command);
	  DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
	  down(Semaphore);
	}
    }
  return true; 
}


/*
  DAC960_ReportDeviceConfiguration reports the Device Configuration Information
  of Controller.
*/

static boolean DAC960_ReportDeviceConfiguration(DAC960_Controller_T *Controller)
{
  int LogicalDriveNumber, Channel, TargetID;
  DAC960_Info("  Physical Devices:\n", Controller);
  for (Channel = 0; Channel < Controller->Channels; Channel++)
    for (TargetID = 0; TargetID < DAC960_MaxTargets; TargetID++)
      {
	DAC960_SCSI_Inquiry_T *InquiryStandardData =
	  &Controller->InquiryStandardData[Channel][TargetID];
	DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
	  &Controller->InquiryUnitSerialNumber[Channel][TargetID];
	DAC960_DeviceState_T *DeviceState =
	  &Controller->DeviceState[Controller->DeviceStateIndex]
				  [Channel][TargetID];
	DAC960_ErrorTable_T *ErrorTable =
	  &Controller->ErrorTable[Controller->ErrorTableIndex];
	DAC960_ErrorTableEntry_T *ErrorEntry =
	  &ErrorTable->ErrorTableEntries[Channel][TargetID];
	char Vendor[1+sizeof(InquiryStandardData->VendorIdentification)];
	char Model[1+sizeof(InquiryStandardData->ProductIdentification)];
	char Revision[1+sizeof(InquiryStandardData->ProductRevisionLevel)];
	char SerialNumber[1+sizeof(InquiryUnitSerialNumber
				   ->ProductSerialNumber)];
	int i;
	if (InquiryStandardData->PeripheralDeviceType == 0x1F) continue;
	for (i = 0; i < sizeof(Vendor)-1; i++)
	  {
	    unsigned char VendorCharacter =
	      InquiryStandardData->VendorIdentification[i];
	    Vendor[i] = (VendorCharacter >= ' ' && VendorCharacter <= '~'
			 ? VendorCharacter : ' ');
	  }
	Vendor[sizeof(Vendor)-1] = '\0';
	for (i = 0; i < sizeof(Model)-1; i++)
	  {
	    unsigned char ModelCharacter =
	      InquiryStandardData->ProductIdentification[i];
	    Model[i] = (ModelCharacter >= ' ' && ModelCharacter <= '~'
			? ModelCharacter : ' ');
	  }
	Model[sizeof(Model)-1] = '\0';
	for (i = 0; i < sizeof(Revision)-1; i++)
	  {
	    unsigned char RevisionCharacter =
	      InquiryStandardData->ProductRevisionLevel[i];
	    Revision[i] = (RevisionCharacter >= ' ' && RevisionCharacter <= '~'
			   ? RevisionCharacter : ' ');
	  }
	Revision[sizeof(Revision)-1] = '\0';
	DAC960_Info("    %d:%d%s Vendor: %s  Model: %s  Revision: %s\n",
		    Controller, Channel, TargetID, (TargetID < 10 ? " " : ""),
		    Vendor, Model, Revision);
	if (InquiryUnitSerialNumber->PeripheralDeviceType != 0x1F)
	  {
	    int SerialNumberLength = InquiryUnitSerialNumber->PageLength;
	    if (SerialNumberLength >
		sizeof(InquiryUnitSerialNumber->ProductSerialNumber))
	      SerialNumberLength =
		sizeof(InquiryUnitSerialNumber->ProductSerialNumber);
	    for (i = 0; i < SerialNumberLength; i++)
	      {
		unsigned char SerialNumberCharacter =
		  InquiryUnitSerialNumber->ProductSerialNumber[i];
		SerialNumber[i] =
		  (SerialNumberCharacter >= ' ' && SerialNumberCharacter <= '~'
		   ? SerialNumberCharacter : ' ');
	      }
	    SerialNumber[SerialNumberLength] = '\0';
	    DAC960_Info("         Serial Number: %s\n",
			Controller, SerialNumber);
	  }
	if (DeviceState->Present && DeviceState->DeviceType == DAC960_DiskType)
	  {
	    if (Controller->DeviceResetCount[Channel][TargetID] > 0)
	      DAC960_Info("         Disk Status: %s, %d blocks, %d resets\n",
			  Controller,
			  (DeviceState->DeviceState == DAC960_Device_Dead
			   ? "Dead"
			   : DeviceState->DeviceState == DAC960_Device_WriteOnly
			   ? "Write-Only"
			   : DeviceState->DeviceState == DAC960_Device_Online
			   ? "Online" : "Standby"),
			  DeviceState->DiskSize,
			  Controller->DeviceResetCount[Channel][TargetID]);
	    else
	      DAC960_Info("         Disk Status: %s, %d blocks\n", Controller,
			  (DeviceState->DeviceState == DAC960_Device_Dead
			   ? "Dead"
			   : DeviceState->DeviceState == DAC960_Device_WriteOnly
			   ? "Write-Only"
			   : DeviceState->DeviceState == DAC960_Device_Online
			   ? "Online" : "Standby"),
			  DeviceState->DiskSize);
	  }
	if (ErrorEntry->ParityErrorCount > 0 ||
	    ErrorEntry->SoftErrorCount > 0 ||
	    ErrorEntry->HardErrorCount > 0 ||
	    ErrorEntry->MiscErrorCount > 0)
	  DAC960_Info("         Errors - Parity: %d, Soft: %d, "
		      "Hard: %d, Misc: %d\n", Controller,
		      ErrorEntry->ParityErrorCount,
		      ErrorEntry->SoftErrorCount,
		      ErrorEntry->HardErrorCount,
		      ErrorEntry->MiscErrorCount);
      }
  DAC960_Info("  Logical Drives:\n", Controller);
  for (LogicalDriveNumber = 0;
       LogicalDriveNumber < Controller->LogicalDriveCount;
       LogicalDriveNumber++)
    {
      DAC960_LogicalDriveInformation_T *LogicalDriveInformation =
	&Controller->LogicalDriveInformation
	   [Controller->LogicalDriveInformationIndex][LogicalDriveNumber];
      DAC960_Info("    /dev/rd/c%dd%d: RAID-%d, %s, %d blocks, %s\n",
		  Controller, Controller->ControllerNumber, LogicalDriveNumber,
		  LogicalDriveInformation->RAIDLevel,
		  (LogicalDriveInformation->LogicalDriveState ==
		     DAC960_LogicalDrive_Online
		   ? "Online"
		   : LogicalDriveInformation->LogicalDriveState ==
		     DAC960_LogicalDrive_Critical
		     ? "Critical" : "Offline"),
		  LogicalDriveInformation->LogicalDriveSize,
		  (LogicalDriveInformation->WriteBack
		   ? "Write Back" : "Write Thru"));
    }
  return true;
}


static inline int DAC_new_segment(request_queue_t *q, struct request *req,
				  int __max_segments)
{
	int max_segments;
	DAC960_Controller_T * Controller = q->queuedata;

	max_segments = Controller->MaxSegmentsPerRequest[MINOR(req->rq_dev)];
	if (__max_segments < max_segments)
		max_segments = __max_segments;

	if (req->nr_segments < max_segments) {
		req->nr_segments++;
		q->elevator.nr_segments++;
		return 1;
	}
	return 0;
}

static int DAC_back_merge_fn(request_queue_t *q, struct request *req, 
			     struct buffer_head *bh, int __max_segments)
{
	if (req->bhtail->b_data + req->bhtail->b_size == bh->b_data)
		return 1;
	return DAC_new_segment(q, req, __max_segments);
}

static int DAC_front_merge_fn(request_queue_t *q, struct request *req, 
			      struct buffer_head *bh, int __max_segments)
{
	if (bh->b_data + bh->b_size == req->bh->b_data)
		return 1;
	return DAC_new_segment(q, req, __max_segments);
}

static int DAC_merge_requests_fn(request_queue_t *q,
				 struct request *req,
				 struct request *next,
				 int __max_segments)
{
	int max_segments;
	DAC960_Controller_T * Controller = q->queuedata;
	int total_segments = req->nr_segments + next->nr_segments;
	int same_segment;

	max_segments = Controller->MaxSegmentsPerRequest[MINOR(req->rq_dev)];
	if (__max_segments < max_segments)
		max_segments = __max_segments;

	same_segment = 0;
	if (req->bhtail->b_data + req->bhtail->b_size == next->bh->b_data)
	{
		total_segments--;
		same_segment = 1;
	}
    
	if (total_segments > max_segments)
		return 0;

	q->elevator.nr_segments -= same_segment;
	req->nr_segments = total_segments;
	return 1;
}

/*
  DAC960_RegisterBlockDevice registers the Block Device structures
  associated with Controller.
*/

static boolean DAC960_RegisterBlockDevice(DAC960_Controller_T *Controller)
{
  request_queue_t * q;

  static void (*RequestFunctions[DAC960_MaxControllers])(request_queue_t *) =
    { DAC960_RequestFunction0, DAC960_RequestFunction1,
      DAC960_RequestFunction2, DAC960_RequestFunction3,
      DAC960_RequestFunction4, DAC960_RequestFunction5,
      DAC960_RequestFunction6, DAC960_RequestFunction7 };
  int MajorNumber = DAC960_MAJOR + Controller->ControllerNumber;
  GenericDiskInfo_T *GenericDiskInfo;
  int MinorNumber;
  /*
    Register the Block Device Major Number for this DAC960 Controller.
  */
  if (devfs_register_blkdev(MajorNumber, "dac960", &DAC960_FileOperations) < 0)
    {
      DAC960_Error("UNABLE TO ACQUIRE MAJOR NUMBER %d - DETACHING\n",
		   Controller, MajorNumber);
      return false;
    }
  /*
    Initialize the I/O Request Function.
  */
  q = BLK_DEFAULT_QUEUE(MajorNumber);
  blk_init_queue(q, RequestFunctions[Controller->ControllerNumber]);
  blk_queue_headactive(q, 0);
  q->back_merge_fn = DAC_back_merge_fn;
  q->front_merge_fn = DAC_front_merge_fn;
  q->merge_requests_fn = DAC_merge_requests_fn;
  q->queuedata = (void *) Controller;

  /*
    Initialize the Disk Partitions array, Partition Sizes array, Block Sizes
    array, Max Sectors per Request array, and Max Segments per Request array.
  */
  for (MinorNumber = 0; MinorNumber < DAC960_MinorCount; MinorNumber++)
    {
      Controller->BlockSizes[MinorNumber] = BLOCK_SIZE;
      Controller->MaxSectorsPerRequest[MinorNumber] =
	Controller->MaxBlocksPerCommand;
      Controller->MaxSegmentsPerRequest[MinorNumber] =
	Controller->MaxScatterGatherSegments;
    }
  Controller->GenericDiskInfo.part = Controller->DiskPartitions;
  Controller->GenericDiskInfo.sizes = Controller->PartitionSizes;
  blksize_size[MajorNumber] = Controller->BlockSizes;
  max_sectors[MajorNumber] = Controller->MaxSectorsPerRequest;
  /*
    Initialize Read Ahead to 128 sectors.
  */
  read_ahead[MajorNumber] = 128;
  /*
    Complete initialization of the Generic Disk Information structure.
  */
  Controller->GenericDiskInfo.major = MajorNumber;
  Controller->GenericDiskInfo.major_name = "dac960";
  Controller->GenericDiskInfo.minor_shift = DAC960_MaxPartitionsBits;
  Controller->GenericDiskInfo.max_p = DAC960_MaxPartitions;
  Controller->GenericDiskInfo.nr_real = Controller->LogicalDriveCount;
  Controller->GenericDiskInfo.real_devices = Controller;
  Controller->GenericDiskInfo.next = NULL;
  Controller->GenericDiskInfo.fops = &DAC960_FileOperations;
  /*
    Install the Generic Disk Information structure at the end of the list.
  */
  if ((GenericDiskInfo = gendisk_head) != NULL)
    {
      while (GenericDiskInfo->next != NULL)
	GenericDiskInfo = GenericDiskInfo->next;
      GenericDiskInfo->next = &Controller->GenericDiskInfo;
    }
  else gendisk_head = &Controller->GenericDiskInfo;
  /*
    Indicate the Block Device Registration completed successfully,
  */
  return true;
}


/*
  DAC960_UnregisterBlockDevice unregisters the Block Device structures
  associated with Controller.
*/

static void DAC960_UnregisterBlockDevice(DAC960_Controller_T *Controller)
{
  int MajorNumber = DAC960_MAJOR + Controller->ControllerNumber;
  /*
    Unregister the Block Device Major Number for this DAC960 Controller.
  */
  devfs_unregister_blkdev(MajorNumber, "dac960");
  /*
    Remove the I/O Request Function.
  */
  blk_cleanup_queue(BLK_DEFAULT_QUEUE(MajorNumber));
  /*
    Remove the Disk Partitions array, Partition Sizes array, Block Sizes
    array, Max Sectors per Request array, and Max Segments per Request array.
  */
  Controller->GenericDiskInfo.part = NULL;
  Controller->GenericDiskInfo.sizes = NULL;
  blk_size[MajorNumber] = NULL;
  blksize_size[MajorNumber] = NULL;
  max_sectors[MajorNumber] = NULL;
  /*
    Remove the Generic Disk Information structure from the list.
  */
  if (gendisk_head != &Controller->GenericDiskInfo)
    {
      GenericDiskInfo_T *GenericDiskInfo = gendisk_head;
      while (GenericDiskInfo != NULL &&
	     GenericDiskInfo->next != &Controller->GenericDiskInfo)
	GenericDiskInfo = GenericDiskInfo->next;
      if (GenericDiskInfo != NULL)
	GenericDiskInfo->next = GenericDiskInfo->next->next;
    }
  else gendisk_head = Controller->GenericDiskInfo.next;
}


/*
  DAC960_InitializeController initializes Controller.
*/

static void DAC960_InitializeController(DAC960_Controller_T *Controller)
{
  if (DAC960_ReadControllerConfiguration(Controller) &&
      DAC960_ReportControllerConfiguration(Controller) &&
      DAC960_ReadDeviceConfiguration(Controller) &&
      DAC960_ReportDeviceConfiguration(Controller) &&
      DAC960_RegisterBlockDevice(Controller))
    {
      /*
	Initialize the Command structures.
      */
      DAC960_Command_T *Commands = Controller->Commands;
      int CommandIdentifier;
      Controller->FreeCommands = NULL;
      for (CommandIdentifier = 0;
	   CommandIdentifier < Controller->DriverQueueDepth;
	   CommandIdentifier++)
	{
	  Commands[CommandIdentifier].Controller = Controller;
	  Commands[CommandIdentifier].Next = Controller->FreeCommands;
	  Controller->FreeCommands = &Commands[CommandIdentifier];
	}
      /*
	Initialize the Monitoring Timer.
      */
      init_timer(&Controller->MonitoringTimer);
      Controller->MonitoringTimer.expires =
	jiffies + DAC960_MonitoringTimerInterval;
      Controller->MonitoringTimer.data = (unsigned long) Controller;
      Controller->MonitoringTimer.function = DAC960_MonitoringTimerFunction;
      add_timer(&Controller->MonitoringTimer);
      Controller->ControllerInitialized = true;
      DAC960_InitializeGenericDiskInfo(&Controller->GenericDiskInfo);
    }
  else DAC960_FinalizeController(Controller);
}


/*
  DAC960_FinalizeController finalizes Controller.
*/

static void DAC960_FinalizeController(DAC960_Controller_T *Controller)
{
  if (Controller->ControllerInitialized)
    {
      del_timer(&Controller->MonitoringTimer);
      DAC960_Notice("Flushing Cache...", Controller);
      DAC960_ExecuteType3(Controller, DAC960_Flush, NULL);
      DAC960_Notice("done\n", Controller);
      switch (Controller->ControllerType)
	{
	case DAC960_V5_Controller:
	  if (!Controller->DualModeMemoryMailboxInterface)
	    DAC960_V5_SaveMemoryMailboxInfo(Controller);
	  break;
	case DAC960_V4_Controller:
	  if (!Controller->DualModeMemoryMailboxInterface)
	    DAC960_V4_SaveMemoryMailboxInfo(Controller);
	  break;
	case DAC960_V3_Controller:
	  break;
	}
    }
  free_irq(Controller->IRQ_Channel, Controller);
  iounmap(Controller->MemoryMappedAddress);
  if (Controller->IO_Address > 0)
    release_region(Controller->IO_Address, 0x80);
  DAC960_UnregisterBlockDevice(Controller);
  DAC960_Controllers[Controller->ControllerNumber] = NULL;
  kfree(Controller);
}


/*
  DAC960_Initialize initializes the DAC960 Driver.
*/

void DAC960_Initialize(void)
{
  int ControllerNumber;
  DAC960_DetectControllers(DAC960_V5_Controller);
  DAC960_DetectControllers(DAC960_V4_Controller);
  DAC960_DetectControllers(DAC960_V3_Controller);
  if (DAC960_ActiveControllerCount == 0) return;
  for (ControllerNumber = 0;
       ControllerNumber < DAC960_ControllerCount;
       ControllerNumber++)
    if (DAC960_Controllers[ControllerNumber] != NULL)
      DAC960_InitializeController(DAC960_Controllers[ControllerNumber]);
  DAC960_CreateProcEntries();
  register_reboot_notifier(&DAC960_NotifierBlock);
}


/*
  DAC960_Finalize finalizes the DAC960 Driver.
*/

static int DAC960_Finalize(NotifierBlock_T *NotifierBlock,
			   unsigned long Event,
			   void *Buffer)
{
  int ControllerNumber;
  if (!(Event == SYS_RESTART || Event == SYS_HALT || Event == SYS_POWER_OFF))
    return NOTIFY_DONE;
  if (DAC960_ActiveControllerCount == 0) return NOTIFY_OK;
  for (ControllerNumber = 0;
       ControllerNumber < DAC960_ControllerCount;
       ControllerNumber++)
    if (DAC960_Controllers[ControllerNumber] != NULL)
      DAC960_FinalizeController(DAC960_Controllers[ControllerNumber]);
  DAC960_DestroyProcEntries();
  unregister_reboot_notifier(&DAC960_NotifierBlock);
  return NOTIFY_OK;
}


/*
  DAC960_ProcessRequest attempts to remove one I/O Request from Controller's
  I/O Request Queue and queues it to the Controller.  WaitForCommand is true if
  this function should wait for a Command to become available if necessary.
  This function returns true if an I/O Request was queued and false otherwise.
*/

static boolean DAC960_ProcessRequest(DAC960_Controller_T *Controller,
				     boolean WaitForCommand)
{
  struct list_head * queue_head;
  IO_Request_T *Request;
  DAC960_Command_T *Command;
  char *RequestBuffer;

  queue_head = &blk_dev[DAC960_MAJOR + Controller->ControllerNumber].request_queue.queue_head;
  while (true)
    {
      if (list_empty(queue_head)) return false;
      Request = blkdev_entry_next_request(queue_head);
      if (Request->rq_status == RQ_INACTIVE) return false;
      Command = DAC960_AllocateCommand(Controller);
      if (Command != NULL) break;
      if (!WaitForCommand) return false;
      spin_unlock(&io_request_lock);
      sleep_on(&Controller->CommandWaitQueue);
      spin_lock_irq(&io_request_lock);
    }
  DAC960_ClearCommand(Command);
  if (Request->cmd == READ)
    Command->CommandType = DAC960_ReadCommand;
  else Command->CommandType = DAC960_WriteCommand;
  Command->Semaphore = Request->sem;
  Command->LogicalDriveNumber = DAC960_LogicalDriveNumber(Request->rq_dev);
  Command->BlockNumber =
    Request->sector
    + Controller->GenericDiskInfo.part[MINOR(Request->rq_dev)].start_sect;
  Command->BlockCount = Request->nr_sectors;
  Command->SegmentCount = Request->nr_segments;
  Command->BufferHeader = Request->bh;
  RequestBuffer = Request->buffer;
  Request->rq_status = RQ_INACTIVE;
  blkdev_dequeue_request(Request);
  wake_up(&wait_for_request);
  if (Command->SegmentCount == 1)
    {
      DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
      if (Command->CommandType == DAC960_ReadCommand)
	CommandMailbox->Type5.CommandOpcode = DAC960_Read;
      else CommandMailbox->Type5.CommandOpcode = DAC960_Write;
      CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
      CommandMailbox->Type5.LD.LogicalDriveNumber = Command->LogicalDriveNumber;
      CommandMailbox->Type5.LogicalBlockAddress = Command->BlockNumber;
      CommandMailbox->Type5.BusAddress = Virtual_to_Bus(RequestBuffer);
    }
  else
    {
      DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
      DAC960_ScatterGatherSegment_T
	*ScatterGatherList = Command->ScatterGatherList;
      BufferHeader_T *BufferHeader = Command->BufferHeader;
      char *LastDataEndPointer = NULL;
      int SegmentNumber = 0;
      if (Command->CommandType == DAC960_ReadCommand)
	CommandMailbox->Type5.CommandOpcode = DAC960_ReadWithOldScatterGather;
      else
	CommandMailbox->Type5.CommandOpcode = DAC960_WriteWithOldScatterGather;
      CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
      CommandMailbox->Type5.LD.LogicalDriveNumber = Command->LogicalDriveNumber;
      CommandMailbox->Type5.LogicalBlockAddress = Command->BlockNumber;
      CommandMailbox->Type5.BusAddress = Virtual_to_Bus(ScatterGatherList);
      CommandMailbox->Type5.ScatterGatherCount = Command->SegmentCount;
      while (BufferHeader != NULL)
	{
	  if (BufferHeader->b_data == LastDataEndPointer)
	    {
	      ScatterGatherList[SegmentNumber-1].SegmentByteCount +=
		BufferHeader->b_size;
	      LastDataEndPointer += BufferHeader->b_size;
	    }
	  else
	    {
	      ScatterGatherList[SegmentNumber].SegmentDataPointer =
		Virtual_to_Bus(BufferHeader->b_data);
	      ScatterGatherList[SegmentNumber].SegmentByteCount =
		BufferHeader->b_size;
	      LastDataEndPointer = BufferHeader->b_data + BufferHeader->b_size;
	      if (SegmentNumber++ > Controller->MaxScatterGatherSegments)
		panic("DAC960: Scatter/Gather Segment Overflow\n");
	    }
	  BufferHeader = BufferHeader->b_reqnext;
	}
      if (SegmentNumber != Command->SegmentCount)
	panic("DAC960: SegmentNumber != SegmentCount\n");
    }
  DAC960_QueueCommand(Command);
  return true;
}


/*
  DAC960_ProcessRequests attempts to remove as many I/O Requests as possible
  from Controller's I/O Request Queue and queue them to the Controller.
*/

static inline void DAC960_ProcessRequests(DAC960_Controller_T *Controller)
{
  int Counter = 0;
  while (DAC960_ProcessRequest(Controller, Counter++ == 0)) ;
}


/*
  DAC960_RequestFunction0 is the I/O Request Function for DAC960 Controller 0.
*/

static void DAC960_RequestFunction0(request_queue_t * q)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[0];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction1 is the I/O Request Function for DAC960 Controller 1.
*/

static void DAC960_RequestFunction1(request_queue_t * q)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[1];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction2 is the I/O Request Function for DAC960 Controller 2.
*/

static void DAC960_RequestFunction2(request_queue_t * q)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[2];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction3 is the I/O Request Function for DAC960 Controller 3.
*/

static void DAC960_RequestFunction3(request_queue_t * q)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[3];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction4 is the I/O Request Function for DAC960 Controller 4.
*/

static void DAC960_RequestFunction4(request_queue_t * q)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[4];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction5 is the I/O Request Function for DAC960 Controller 5.
*/

static void DAC960_RequestFunction5(request_queue_t * q)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[5];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction6 is the I/O Request Function for DAC960 Controller 6.
*/

static void DAC960_RequestFunction6(request_queue_t * q)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[6];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction7 is the I/O Request Function for DAC960 Controller 7.
*/

static void DAC960_RequestFunction7(request_queue_t * q)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[7];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_ReadWriteError prints an appropriate error message for Command when
  an error occurs on a Read or Write operation.
*/

static void DAC960_ReadWriteError(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  char *CommandName = "UNKNOWN";
  switch (Command->CommandType)
    {
    case DAC960_ReadCommand:
    case DAC960_ReadRetryCommand:
      CommandName = "READ";
      break;
    case DAC960_WriteCommand:
    case DAC960_WriteRetryCommand:
      CommandName = "WRITE";
      break;
    case DAC960_MonitoringCommand:
    case DAC960_ImmediateCommand:
    case DAC960_QueuedCommand:
      break;
    }
  switch (Command->CommandStatus)
    {
    case DAC960_IrrecoverableDataError:
      DAC960_Error("Irrecoverable Data Error on %s:\n",
		   Controller, CommandName);
      break;
    case DAC960_LogicalDriveNonexistentOrOffline:
      DAC960_Error("Logical Drive Nonexistent or Offline on %s:\n",
		   Controller, CommandName);
      break;
    case DAC960_AccessBeyondEndOfLogicalDrive:
      DAC960_Error("Attempt to Access Beyond End of Logical Drive "
		   "on %s:\n", Controller, CommandName);
      break;
    case DAC960_BadDataEncountered:
      DAC960_Error("Bad Data Encountered on %s:\n", Controller, CommandName);
      break;
    default:
      DAC960_Error("Unexpected Error Status %04X on %s:\n",
		   Controller, Command->CommandStatus, CommandName);
      break;
    }
  DAC960_Error("  /dev/rd/c%dd%d:   absolute blocks %d..%d\n",
	       Controller, Controller->ControllerNumber,
	       Command->LogicalDriveNumber, Command->BlockNumber,
	       Command->BlockNumber + Command->BlockCount - 1);
  if (DAC960_PartitionNumber(Command->BufferHeader->b_rdev) > 0)
    DAC960_Error("  /dev/rd/c%dd%dp%d: relative blocks %d..%d\n",
		 Controller, Controller->ControllerNumber,
		 Command->LogicalDriveNumber,
		 DAC960_PartitionNumber(Command->BufferHeader->b_rdev),
		 Command->BufferHeader->b_rsector,
		 Command->BufferHeader->b_rsector + Command->BlockCount - 1);
}


/*
  DAC960_ProcessCompletedBuffer performs completion processing for an
  individual Buffer.
*/

static inline void DAC960_ProcessCompletedBuffer(BufferHeader_T *BufferHeader,
						 boolean SuccessfulIO)
{
  BufferHeader->b_end_io(BufferHeader, SuccessfulIO);
}


/*
  DAC960_ProcessCompletedCommand performs completion processing for Command.
*/

static void DAC960_ProcessCompletedCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_CommandType_T CommandType = Command->CommandType;
  DAC960_CommandOpcode_T CommandOpcode =
    Command->CommandMailbox.Common.CommandOpcode;
  DAC960_CommandStatus_T CommandStatus = Command->CommandStatus;
  BufferHeader_T *BufferHeader = Command->BufferHeader;
  if (CommandType == DAC960_ReadCommand ||
      CommandType == DAC960_WriteCommand)
    {
      if (CommandStatus == DAC960_NormalCompletion)
	{
	  /*
	    Perform completion processing for all buffers in this I/O Request.
	  */
	  while (BufferHeader != NULL)
	    {
	      BufferHeader_T *NextBufferHeader = BufferHeader->b_reqnext;
	      BufferHeader->b_reqnext = NULL;
	      DAC960_ProcessCompletedBuffer(BufferHeader, true);
	      BufferHeader = NextBufferHeader;
	    }
	  /*
	    Wake up requestor for swap file paging requests.
	  */
	  if (Command->Semaphore != NULL)
	    {
	      up(Command->Semaphore);
	      Command->Semaphore = NULL;
	    }
	  add_blkdev_randomness(DAC960_MAJOR + Controller->ControllerNumber);
	}
      else if ((CommandStatus == DAC960_IrrecoverableDataError ||
		CommandStatus == DAC960_BadDataEncountered) &&
	       BufferHeader != NULL &&
	       BufferHeader->b_reqnext != NULL)
	{
	  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
	  if (CommandType == DAC960_ReadCommand)
	    {
	      Command->CommandType = DAC960_ReadRetryCommand;
	      CommandMailbox->Type5.CommandOpcode = DAC960_Read;
	    }
	  else
	    {
	      Command->CommandType = DAC960_WriteRetryCommand;
	      CommandMailbox->Type5.CommandOpcode = DAC960_Write;
	    }
	  Command->BlockCount = BufferHeader->b_size >> DAC960_BlockSizeBits;
	  CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
	  CommandMailbox->Type5.BusAddress =
	    Virtual_to_Bus(BufferHeader->b_data);
	  DAC960_QueueCommand(Command);
	  return;
	}
      else
	{
	  if (CommandStatus != DAC960_LogicalDriveNonexistentOrOffline)
	    DAC960_ReadWriteError(Command);
	  /*
	    Perform completion processing for all buffers in this I/O Request.
	  */
	  while (BufferHeader != NULL)
	    {
	      BufferHeader_T *NextBufferHeader = BufferHeader->b_reqnext;
	      BufferHeader->b_reqnext = NULL;
	      DAC960_ProcessCompletedBuffer(BufferHeader, false);
	      BufferHeader = NextBufferHeader;
	    }
	  /*
	    Wake up requestor for swap file paging requests.
	  */
	  if (Command->Semaphore != NULL)
	    {
	      up(Command->Semaphore);
	      Command->Semaphore = NULL;
	    }
	}
    }
  else if (CommandType == DAC960_ReadRetryCommand ||
	   CommandType == DAC960_WriteRetryCommand)
    {
      BufferHeader_T *NextBufferHeader = BufferHeader->b_reqnext;
      BufferHeader->b_reqnext = NULL;
      /*
	Perform completion processing for this single buffer.
      */
      if (CommandStatus == DAC960_NormalCompletion)
	DAC960_ProcessCompletedBuffer(BufferHeader, true);
      else
	{
	  if (CommandStatus != DAC960_LogicalDriveNonexistentOrOffline)
	    DAC960_ReadWriteError(Command);
	  DAC960_ProcessCompletedBuffer(BufferHeader, false);
	}
      if (NextBufferHeader != NULL)
	{
	  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
	  Command->BlockNumber +=
	    BufferHeader->b_size >> DAC960_BlockSizeBits;
	  Command->BlockCount =
	    NextBufferHeader->b_size >> DAC960_BlockSizeBits;
	  Command->BufferHeader = NextBufferHeader;
	  CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
	  CommandMailbox->Type5.LogicalBlockAddress = Command->BlockNumber;
	  CommandMailbox->Type5.BusAddress =
	    Virtual_to_Bus(NextBufferHeader->b_data);
	  DAC960_QueueCommand(Command);
	  return;
	}
    }
  else if (CommandType == DAC960_MonitoringCommand ||
	   CommandOpcode == DAC960_Enquiry ||
	   CommandOpcode == DAC960_GetRebuildProgress)
    {
      if (CommandType != DAC960_MonitoringCommand)
	{
	  if (CommandOpcode == DAC960_Enquiry)
	    memcpy(&Controller->Enquiry[Controller->EnquiryIndex ^ 1],
		   Bus_to_Virtual(Command->CommandMailbox.Type3.BusAddress),
		   sizeof(DAC960_Enquiry_T));
	  else if (CommandOpcode == DAC960_GetRebuildProgress)
	    memcpy(&Controller->RebuildProgress,
		   Bus_to_Virtual(Command->CommandMailbox.Type3.BusAddress),
		   sizeof(DAC960_RebuildProgress_T));
	}
      if (CommandOpcode == DAC960_Enquiry)
	{
	  DAC960_Enquiry_T *OldEnquiry =
	    &Controller->Enquiry[Controller->EnquiryIndex];
	  DAC960_Enquiry_T *NewEnquiry =
	    &Controller->Enquiry[Controller->EnquiryIndex ^= 1];
	  unsigned int OldCriticalLogicalDriveCount =
	    OldEnquiry->CriticalLogicalDriveCount;
	  unsigned int NewCriticalLogicalDriveCount =
	    NewEnquiry->CriticalLogicalDriveCount;
	  if (NewEnquiry->StatusFlags.DeferredWriteError !=
	      OldEnquiry->StatusFlags.DeferredWriteError)
	    DAC960_Critical("Deferred Write Error Flag is now %s\n", Controller,
			    (NewEnquiry->StatusFlags.DeferredWriteError
			     ? "TRUE" : "FALSE"));
	  if ((NewCriticalLogicalDriveCount > 0 ||
	       NewCriticalLogicalDriveCount != OldCriticalLogicalDriveCount) ||
	      (NewEnquiry->OfflineLogicalDriveCount > 0 ||
	       NewEnquiry->OfflineLogicalDriveCount !=
	       OldEnquiry->OfflineLogicalDriveCount) ||
	      (NewEnquiry->DeadDriveCount > 0 ||
	       NewEnquiry->DeadDriveCount !=
	       OldEnquiry->DeadDriveCount) ||
	      (NewEnquiry->EventLogSequenceNumber !=
	       OldEnquiry->EventLogSequenceNumber) ||
	      Controller->MonitoringTimerCount == 0 ||
	      (jiffies - Controller->SecondaryMonitoringTime
	       >= DAC960_SecondaryMonitoringInterval))
	    {
	      Controller->NeedLogicalDriveInformation = true;
	      Controller->NewEventLogSequenceNumber =
		NewEnquiry->EventLogSequenceNumber;
	      Controller->NeedErrorTableInformation = true;
	      Controller->NeedDeviceStateInformation = true;
	      Controller->DeviceStateChannel = 0;
	      Controller->DeviceStateTargetID = -1;
	      Controller->SecondaryMonitoringTime = jiffies;
	    }
	  if (NewEnquiry->RebuildFlag == DAC960_StandbyRebuildInProgress ||
	      NewEnquiry->RebuildFlag == DAC960_BackgroundRebuildInProgress ||
	      OldEnquiry->RebuildFlag == DAC960_StandbyRebuildInProgress ||
	      OldEnquiry->RebuildFlag == DAC960_BackgroundRebuildInProgress)
	    Controller->NeedRebuildProgress = true;
	  if (OldEnquiry->RebuildFlag == DAC960_BackgroundCheckInProgress)
	    switch (NewEnquiry->RebuildFlag)
	      {
	      case DAC960_NoStandbyRebuildOrCheckInProgress:
		DAC960_Progress("Consistency Check Completed Successfully\n",
				Controller);
		break;
	      case DAC960_StandbyRebuildInProgress:
	      case DAC960_BackgroundRebuildInProgress:
		break;
	      case DAC960_BackgroundCheckInProgress:
		Controller->NeedConsistencyCheckProgress = true;
		break;
	      case DAC960_StandbyRebuildCompletedWithError:
		DAC960_Progress("Consistency Check Completed with Error\n",
				Controller);
		break;
	      case DAC960_BackgroundRebuildOrCheckFailed_DriveFailed:
		DAC960_Progress("Consistency Check Failed - "
				"Physical Drive Failed\n", Controller);
		break;
	      case DAC960_BackgroundRebuildOrCheckFailed_LogicalDriveFailed:
		DAC960_Progress("Consistency Check Failed - "
				"Logical Drive Failed\n", Controller);
		break;
	      case DAC960_BackgroundRebuildOrCheckFailed_OtherCauses:
		DAC960_Progress("Consistency Check Failed - Other Causes\n",
				Controller);
		break;
	      case DAC960_BackgroundRebuildOrCheckSuccessfullyTerminated:
		DAC960_Progress("Consistency Check Successfully Terminated\n",
				Controller);
		break;
	      }
	  else if (NewEnquiry->RebuildFlag == DAC960_BackgroundCheckInProgress)
	    Controller->NeedConsistencyCheckProgress = true;
	}
      else if (CommandOpcode == DAC960_PerformEventLogOperation)
	{
	  static char
	    *DAC960_EventMessages[] =
	       { "killed because write recovery failed",
		 "killed because of SCSI bus reset failure",
		 "killed because of double check condition",
		 "killed because it was removed",
		 "killed because of gross error on SCSI chip",
		 "killed because of bad tag returned from drive",
		 "killed because of timeout on SCSI command",
		 "killed because of reset SCSI command issued from system",
		 "killed because busy or parity error count exceeded limit",
		 "killed because of 'kill drive' command from system",
		 "killed because of selection timeout",
		 "killed due to SCSI phase sequence error",
		 "killed due to unknown status" };
	  DAC960_EventLogEntry_T *EventLogEntry = &Controller->EventLogEntry;
	  if (EventLogEntry->SequenceNumber ==
	      Controller->OldEventLogSequenceNumber)
	    {
	      unsigned char SenseKey = EventLogEntry->SenseKey;
	      unsigned char AdditionalSenseCode =
		EventLogEntry->AdditionalSenseCode;
	      unsigned char AdditionalSenseCodeQualifier =
		EventLogEntry->AdditionalSenseCodeQualifier;
	      if (SenseKey == 9 &&
		  AdditionalSenseCode == 0x80 &&
		  AdditionalSenseCodeQualifier <
		  sizeof(DAC960_EventMessages) / sizeof(char *))
		DAC960_Critical("Physical Drive %d:%d %s\n", Controller,
				EventLogEntry->Channel,
				EventLogEntry->TargetID,
				DAC960_EventMessages[
				  AdditionalSenseCodeQualifier]);
	      else if (SenseKey == 6 && AdditionalSenseCode == 0x29)
		{
		  if (Controller->MonitoringTimerCount > 0)
		    Controller->DeviceResetCount[EventLogEntry->Channel]
						[EventLogEntry->TargetID]++;
		}
	      else if (!(SenseKey == 0 ||
			 (SenseKey == 2 &&
			  AdditionalSenseCode == 0x04 &&
			  (AdditionalSenseCodeQualifier == 0x01 ||
			   AdditionalSenseCodeQualifier == 0x02))))
		{
		  DAC960_Critical("Physical Drive %d:%d Error Log: "
				  "Sense Key = %d, ASC = %02X, ASCQ = %02X\n",
				  Controller,
				  EventLogEntry->Channel,
				  EventLogEntry->TargetID,
				  SenseKey,
				  AdditionalSenseCode,
				  AdditionalSenseCodeQualifier);
		  DAC960_Critical("Physical Drive %d:%d Error Log: "
				  "Information = %02X%02X%02X%02X "
				  "%02X%02X%02X%02X\n",
				  Controller,
				  EventLogEntry->Channel,
				  EventLogEntry->TargetID,
				  EventLogEntry->Information[0],
				  EventLogEntry->Information[1],
				  EventLogEntry->Information[2],
				  EventLogEntry->Information[3],
				  EventLogEntry->CommandSpecificInformation[0],
				  EventLogEntry->CommandSpecificInformation[1],
				  EventLogEntry->CommandSpecificInformation[2],
				  EventLogEntry->CommandSpecificInformation[3]);
		}
	    }
	  Controller->OldEventLogSequenceNumber++;
	}
      else if (CommandOpcode == DAC960_GetErrorTable)
	{
	  DAC960_ErrorTable_T *OldErrorTable =
	    &Controller->ErrorTable[Controller->ErrorTableIndex];
	  DAC960_ErrorTable_T *NewErrorTable =
	    &Controller->ErrorTable[Controller->ErrorTableIndex ^= 1];
	  int Channel, TargetID;
	  for (Channel = 0; Channel < Controller->Channels; Channel++)
	    for (TargetID = 0; TargetID < DAC960_MaxTargets; TargetID++)
	      {
		DAC960_ErrorTableEntry_T *NewErrorEntry =
		  &NewErrorTable->ErrorTableEntries[Channel][TargetID];
		DAC960_ErrorTableEntry_T *OldErrorEntry =
		  &OldErrorTable->ErrorTableEntries[Channel][TargetID];
		if ((NewErrorEntry->ParityErrorCount !=
		     OldErrorEntry->ParityErrorCount) ||
		    (NewErrorEntry->SoftErrorCount !=
		     OldErrorEntry->SoftErrorCount) ||
		    (NewErrorEntry->HardErrorCount !=
		     OldErrorEntry->HardErrorCount) ||
		    (NewErrorEntry->MiscErrorCount !=
		     OldErrorEntry->MiscErrorCount))
		  DAC960_Critical("Physical Drive %d:%d Errors: "
				  "Parity = %d, Soft = %d, "
				  "Hard = %d, Misc = %d\n",
				  Controller, Channel, TargetID,
				  NewErrorEntry->ParityErrorCount,
				  NewErrorEntry->SoftErrorCount,
				  NewErrorEntry->HardErrorCount,
				  NewErrorEntry->MiscErrorCount);
	      }
	}
      else if (CommandOpcode == DAC960_GetDeviceState)
	{
	  DAC960_DeviceState_T *OldDeviceState =
	    &Controller->DeviceState[Controller->DeviceStateIndex]
				    [Controller->DeviceStateChannel]
				    [Controller->DeviceStateTargetID];
	  DAC960_DeviceState_T *NewDeviceState =
	    &Controller->DeviceState[Controller->DeviceStateIndex ^ 1]
				    [Controller->DeviceStateChannel]
				    [Controller->DeviceStateTargetID];
	  if (NewDeviceState->DeviceState != OldDeviceState->DeviceState)
	    DAC960_Critical("Physical Drive %d:%d is now %s\n", Controller,
			    Controller->DeviceStateChannel,
			    Controller->DeviceStateTargetID,
			    (NewDeviceState->DeviceState == DAC960_Device_Dead
			     ? "DEAD"
			     : NewDeviceState->DeviceState
			       == DAC960_Device_WriteOnly
			       ? "WRITE-ONLY"
			       : NewDeviceState->DeviceState
				 == DAC960_Device_Online
				 ? "ONLINE" : "STANDBY"));
	  if (OldDeviceState->DeviceState == DAC960_Device_Dead &&
	      NewDeviceState->DeviceState != DAC960_Device_Dead)
	    {
	      Controller->NeedDeviceInquiryInformation = true;
	      Controller->NeedDeviceSerialNumberInformation = true;
	    }
	}
      else if (CommandOpcode == DAC960_GetLogicalDriveInformation)
	{
	  int LogicalDriveNumber;
	  for (LogicalDriveNumber = 0;
	       LogicalDriveNumber < Controller->LogicalDriveCount;
	       LogicalDriveNumber++)
	    {
	      DAC960_LogicalDriveInformation_T *OldLogicalDriveInformation =
		&Controller->LogicalDriveInformation
			     [Controller->LogicalDriveInformationIndex]
			     [LogicalDriveNumber];
	      DAC960_LogicalDriveInformation_T *NewLogicalDriveInformation =
		&Controller->LogicalDriveInformation
			     [Controller->LogicalDriveInformationIndex ^ 1]
			     [LogicalDriveNumber];
	      if (NewLogicalDriveInformation->LogicalDriveState !=
		  OldLogicalDriveInformation->LogicalDriveState)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"is now %s\n", Controller,
				LogicalDriveNumber,
				Controller->ControllerNumber,
				LogicalDriveNumber,
				(NewLogicalDriveInformation->LogicalDriveState
				 == DAC960_LogicalDrive_Online
				 ? "ONLINE"
				 : NewLogicalDriveInformation->LogicalDriveState
				 == DAC960_LogicalDrive_Critical
				 ? "CRITICAL" : "OFFLINE"));
	      if (NewLogicalDriveInformation->WriteBack !=
		  OldLogicalDriveInformation->WriteBack)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"is now %s\n", Controller,
				LogicalDriveNumber,
				Controller->ControllerNumber,
				LogicalDriveNumber,
				(NewLogicalDriveInformation->WriteBack
				 ? "WRITE BACK" : "WRITE THRU"));
	    }
	  Controller->LogicalDriveInformationIndex ^= 1;
	}
      else if (CommandOpcode == DAC960_GetRebuildProgress)
	{
	  unsigned int LogicalDriveNumber =
	    Controller->RebuildProgress.LogicalDriveNumber;
	  unsigned int LogicalDriveSize =
	    Controller->RebuildProgress.LogicalDriveSize;
	  unsigned int BlocksCompleted =
	    LogicalDriveSize - Controller->RebuildProgress.RemainingBlocks;
	  switch (CommandStatus)
	    {
	    case DAC960_NormalCompletion:
	      Controller->EphemeralProgressMessage = true;
	      DAC960_Progress("Rebuild in Progress: "
			      "Logical Drive %d (/dev/rd/c%dd%d) "
			      "%d%% completed\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber,
			      (100 * (BlocksCompleted >> 7))
			      / (LogicalDriveSize >> 7));
	      Controller->EphemeralProgressMessage = false;
	      break;
	    case DAC960_RebuildFailed_LogicalDriveFailure:
	      DAC960_Progress("Rebuild Failed due to "
			      "Logical Drive Failure\n", Controller);
	      break;
	    case DAC960_RebuildFailed_BadBlocksOnOther:
	      DAC960_Progress("Rebuild Failed due to "
			      "Bad Blocks on Other Drives\n", Controller);
	      break;
	    case DAC960_RebuildFailed_NewDriveFailed:
	      DAC960_Progress("Rebuild Failed due to "
			      "Failure of Drive Being Rebuilt\n", Controller);
	      break;
	    case DAC960_NoRebuildOrCheckInProgress:
	      if (Controller->LastRebuildStatus != DAC960_NormalCompletion)
		break;
	    case DAC960_RebuildSuccessful:
	      DAC960_Progress("Rebuild Completed Successfully\n", Controller);
	      break;
	    }
	  Controller->LastRebuildStatus = CommandStatus;
	}
      else if (CommandOpcode == DAC960_RebuildStat)
	{
	  unsigned int LogicalDriveNumber =
	    Controller->RebuildProgress.LogicalDriveNumber;
	  unsigned int LogicalDriveSize =
	    Controller->RebuildProgress.LogicalDriveSize;
	  unsigned int BlocksCompleted =
	    LogicalDriveSize - Controller->RebuildProgress.RemainingBlocks;
	  if (CommandStatus == DAC960_NormalCompletion)
	    {
	      Controller->EphemeralProgressMessage = true;
	      DAC960_Progress("Consistency Check in Progress: "
			      "Logical Drive %d (/dev/rd/c%dd%d) "
			      "%d%% completed\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber,
			      (100 * (BlocksCompleted >> 7))
			      / (LogicalDriveSize >> 7));
	      Controller->EphemeralProgressMessage = false;
	    }
	}
    }
  if (CommandType == DAC960_MonitoringCommand)
    {
      if (Controller->NewEventLogSequenceNumber
	  - Controller->OldEventLogSequenceNumber > 0)
	{
	  Command->CommandMailbox.Type3E.CommandOpcode =
	    DAC960_PerformEventLogOperation;
	  Command->CommandMailbox.Type3E.OperationType =
	    DAC960_GetEventLogEntry;
	  Command->CommandMailbox.Type3E.OperationQualifier = 1;
	  Command->CommandMailbox.Type3E.SequenceNumber =
	    Controller->OldEventLogSequenceNumber;
	  Command->CommandMailbox.Type3E.BusAddress =
	    Virtual_to_Bus(&Controller->EventLogEntry);
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->NeedErrorTableInformation)
	{
	  Controller->NeedErrorTableInformation = false;
	  Command->CommandMailbox.Type3.CommandOpcode = DAC960_GetErrorTable;
	  Command->CommandMailbox.Type3.BusAddress =
	    Virtual_to_Bus(
	      &Controller->ErrorTable[Controller->ErrorTableIndex ^ 1]);
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->NeedRebuildProgress && 
	  Controller->Enquiry[Controller->EnquiryIndex]
		      .CriticalLogicalDriveCount <
	  Controller->Enquiry[Controller->EnquiryIndex ^ 1]
		      .CriticalLogicalDriveCount)
	{
	  Controller->NeedRebuildProgress = false;
	  Command->CommandMailbox.Type3.CommandOpcode =
	    DAC960_GetRebuildProgress;
	  Command->CommandMailbox.Type3.BusAddress =
	    Virtual_to_Bus(&Controller->RebuildProgress);
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->NeedDeviceStateInformation)
	{
	  if (Controller->NeedDeviceInquiryInformation)
	    {
	      DAC960_DCDB_T *DCDB = &Controller->MonitoringDCDB;
	      DAC960_SCSI_Inquiry_T *InquiryStandardData =
		&Controller->InquiryStandardData
			       [Controller->DeviceStateChannel]
			       [Controller->DeviceStateTargetID];
	      InquiryStandardData->PeripheralDeviceType = 0x1F;
	      Command->CommandMailbox.Type3.CommandOpcode = DAC960_DCDB;
	      Command->CommandMailbox.Type3.BusAddress = Virtual_to_Bus(DCDB);
	      DCDB->Channel = Controller->DeviceStateChannel;
	      DCDB->TargetID = Controller->DeviceStateTargetID;
	      DCDB->Direction = DAC960_DCDB_DataTransferDeviceToSystem;
	      DCDB->EarlyStatus = false;
	      DCDB->Timeout = DAC960_DCDB_Timeout_10_seconds;
	      DCDB->NoAutomaticRequestSense = false;
	      DCDB->DisconnectPermitted = true;
	      DCDB->TransferLength = sizeof(DAC960_SCSI_Inquiry_T);
	      DCDB->BusAddress = Virtual_to_Bus(InquiryStandardData);
	      DCDB->CDBLength = 6;
	      DCDB->TransferLengthHigh4 = 0;
	      DCDB->SenseLength = sizeof(DCDB->SenseData);
	      DCDB->CDB[0] = 0x12; /* INQUIRY */
	      DCDB->CDB[1] = 0; /* EVPD = 0 */
	      DCDB->CDB[2] = 0; /* Page Code */
	      DCDB->CDB[3] = 0; /* Reserved */
	      DCDB->CDB[4] = sizeof(DAC960_SCSI_Inquiry_T);
	      DCDB->CDB[5] = 0; /* Control */
	      DAC960_QueueCommand(Command);
	      Controller->NeedDeviceInquiryInformation = false;
	      return;
	    }
	  if (Controller->NeedDeviceSerialNumberInformation)
	    {
	      DAC960_DCDB_T *DCDB = &Controller->MonitoringDCDB;
	      DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
		&Controller->InquiryUnitSerialNumber
			       [Controller->DeviceStateChannel]
			       [Controller->DeviceStateTargetID];
	      InquiryUnitSerialNumber->PeripheralDeviceType = 0x1F;
	      Command->CommandMailbox.Type3.CommandOpcode = DAC960_DCDB;
	      Command->CommandMailbox.Type3.BusAddress = Virtual_to_Bus(DCDB);
	      DCDB->Channel = Controller->DeviceStateChannel;
	      DCDB->TargetID = Controller->DeviceStateTargetID;
	      DCDB->Direction = DAC960_DCDB_DataTransferDeviceToSystem;
	      DCDB->EarlyStatus = false;
	      DCDB->Timeout = DAC960_DCDB_Timeout_10_seconds;
	      DCDB->NoAutomaticRequestSense = false;
	      DCDB->DisconnectPermitted = true;
	      DCDB->TransferLength =
		sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
	      DCDB->BusAddress = Virtual_to_Bus(InquiryUnitSerialNumber);
	      DCDB->CDBLength = 6;
	      DCDB->TransferLengthHigh4 = 0;
	      DCDB->SenseLength = sizeof(DCDB->SenseData);
	      DCDB->CDB[0] = 0x12; /* INQUIRY */
	      DCDB->CDB[1] = 1; /* EVPD = 1 */
	      DCDB->CDB[2] = 0x80; /* Page Code */
	      DCDB->CDB[3] = 0; /* Reserved */
	      DCDB->CDB[4] = sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
	      DCDB->CDB[5] = 0; /* Control */
	      DAC960_QueueCommand(Command);
	      Controller->NeedDeviceSerialNumberInformation = false;
	      return;
	    }
	  if (++Controller->DeviceStateTargetID == DAC960_MaxTargets)
	    {
	      Controller->DeviceStateChannel++;
	      Controller->DeviceStateTargetID = 0;
	    }
	  while (Controller->DeviceStateChannel < Controller->Channels)
	    {
	      DAC960_DeviceState_T *OldDeviceState =
		&Controller->DeviceState[Controller->DeviceStateIndex]
					[Controller->DeviceStateChannel]
					[Controller->DeviceStateTargetID];
	      if (OldDeviceState->Present &&
		  OldDeviceState->DeviceType == DAC960_DiskType)
		{
		  Command->CommandMailbox.Type3D.CommandOpcode =
		    DAC960_GetDeviceState;
		  Command->CommandMailbox.Type3D.Channel =
		    Controller->DeviceStateChannel;
		  Command->CommandMailbox.Type3D.TargetID =
		    Controller->DeviceStateTargetID;
		  Command->CommandMailbox.Type3D.BusAddress =
		    Virtual_to_Bus(&Controller->DeviceState
				      [Controller->DeviceStateIndex ^ 1]
				      [Controller->DeviceStateChannel]
				      [Controller->DeviceStateTargetID]);
		  DAC960_QueueCommand(Command);
		  return;
		}
	      if (++Controller->DeviceStateTargetID == DAC960_MaxTargets)
		{
		  Controller->DeviceStateChannel++;
		  Controller->DeviceStateTargetID = 0;
		}
	    }
	  Controller->NeedDeviceStateInformation = false;
	  Controller->DeviceStateIndex ^= 1;
	}
      if (Controller->NeedLogicalDriveInformation)
	{
	  Controller->NeedLogicalDriveInformation = false;
	  Command->CommandMailbox.Type3.CommandOpcode =
	    DAC960_GetLogicalDriveInformation;
	  Command->CommandMailbox.Type3.BusAddress =
	    Virtual_to_Bus(
	      &Controller->LogicalDriveInformation
			   [Controller->LogicalDriveInformationIndex ^ 1]);
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->NeedRebuildProgress)
	{
	  Controller->NeedRebuildProgress = false;
	  Command->CommandMailbox.Type3.CommandOpcode =
	    DAC960_GetRebuildProgress;
	  Command->CommandMailbox.Type3.BusAddress =
	    Virtual_to_Bus(&Controller->RebuildProgress);
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->NeedConsistencyCheckProgress)
	{
	  Controller->NeedConsistencyCheckProgress = false;
	  Command->CommandMailbox.Type3.CommandOpcode = DAC960_RebuildStat;
	  Command->CommandMailbox.Type3.BusAddress =
	    Virtual_to_Bus(&Controller->RebuildProgress);
	  DAC960_QueueCommand(Command);
	  return;
	}
      Controller->MonitoringTimerCount++;
      Controller->MonitoringTimer.expires =
	jiffies + DAC960_MonitoringTimerInterval;
      add_timer(&Controller->MonitoringTimer);
    }
  if (CommandType == DAC960_ImmediateCommand)
    {
      up(Command->Semaphore);
      Command->Semaphore = NULL;
      return;
    }
  if (CommandType == DAC960_QueuedCommand)
    {
      DAC960_KernelCommand_T *KernelCommand = Command->KernelCommand;
      KernelCommand->CommandStatus = CommandStatus;
      Command->KernelCommand = NULL;
      if (CommandOpcode == DAC960_DCDB)
	Controller->DirectCommandActive[KernelCommand->DCDB->Channel]
				       [KernelCommand->DCDB->TargetID] = false;
      DAC960_DeallocateCommand(Command);
      KernelCommand->CompletionFunction(KernelCommand);
      return;
    }
  /*
    Queue a Status Monitoring Command to the Controller using the just
    completed Command if one was deferred previously due to lack of a
    free Command when the Monitoring Timer Function was called.
  */
  if (Controller->MonitoringCommandDeferred)
    {
      Controller->MonitoringCommandDeferred = false;
      DAC960_QueueMonitoringCommand(Command);
      return;
    }
  /*
    Deallocate the Command, and wake up any processes waiting on a free Command.
  */
  DAC960_DeallocateCommand(Command);
  wake_up(&Controller->CommandWaitQueue);
}


/*
  DAC960_InterruptHandler handles hardware interrupts from DAC960 Controllers.
*/

static void DAC960_InterruptHandler(int IRQ_Channel,
				    void *DeviceIdentifier,
				    Registers_T *InterruptRegisters)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) DeviceIdentifier;
  void *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_StatusMailbox_T *NextStatusMailbox;
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockIH(Controller, &ProcessorFlags);
  /*
    Process Hardware Interrupts for Controller.
  */
  switch (Controller->ControllerType)
    {
    case DAC960_V5_Controller:
      DAC960_V5_AcknowledgeInterrupt(ControllerBaseAddress);
      NextStatusMailbox = Controller->NextStatusMailbox;
      while (NextStatusMailbox->Fields.Valid)
	{
	  DAC960_CommandIdentifier_T CommandIdentifier =
	    NextStatusMailbox->Fields.CommandIdentifier;
	  DAC960_Command_T *Command = &Controller->Commands[CommandIdentifier];
	  Command->CommandStatus = NextStatusMailbox->Fields.CommandStatus;
	  NextStatusMailbox->Word = 0;
	  if (++NextStatusMailbox > Controller->LastStatusMailbox)
	    NextStatusMailbox = Controller->FirstStatusMailbox;
	  DAC960_ProcessCompletedCommand(Command);
	}
      Controller->NextStatusMailbox = NextStatusMailbox;
      break;
    case DAC960_V4_Controller:
      DAC960_V4_AcknowledgeInterrupt(ControllerBaseAddress);
      NextStatusMailbox = Controller->NextStatusMailbox;
      while (NextStatusMailbox->Fields.Valid)
	{
	  DAC960_CommandIdentifier_T CommandIdentifier =
	    NextStatusMailbox->Fields.CommandIdentifier;
	  DAC960_Command_T *Command = &Controller->Commands[CommandIdentifier];
	  Command->CommandStatus = NextStatusMailbox->Fields.CommandStatus;
	  NextStatusMailbox->Word = 0;
	  if (++NextStatusMailbox > Controller->LastStatusMailbox)
	    NextStatusMailbox = Controller->FirstStatusMailbox;
	  DAC960_ProcessCompletedCommand(Command);
	}
      Controller->NextStatusMailbox = NextStatusMailbox;
      break;
    case DAC960_V3_Controller:
      while (DAC960_V3_StatusAvailableP(ControllerBaseAddress))
	{
	  DAC960_CommandIdentifier_T CommandIdentifier =
	    DAC960_V3_ReadStatusCommandIdentifier(ControllerBaseAddress);
	  DAC960_Command_T *Command = &Controller->Commands[CommandIdentifier];
	  Command->CommandStatus =
	    DAC960_V3_ReadStatusRegister(ControllerBaseAddress);
	  DAC960_V3_AcknowledgeInterrupt(ControllerBaseAddress);
	  DAC960_V3_AcknowledgeStatus(ControllerBaseAddress);
	  DAC960_ProcessCompletedCommand(Command);
	}
      break;
    }
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  while (DAC960_ProcessRequest(Controller, false)) ;
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockIH(Controller, &ProcessorFlags);
}


/*
  DAC960_QueueMonitoringCommand queues a Monitoring Command to Controller.
*/

static void DAC960_QueueMonitoringCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  DAC960_ClearCommand(Command);
  Command->CommandType = DAC960_MonitoringCommand;
  CommandMailbox->Type3.CommandOpcode = DAC960_Enquiry;
  CommandMailbox->Type3.BusAddress =
    Virtual_to_Bus(&Controller->Enquiry[Controller->EnquiryIndex ^ 1]);
  DAC960_QueueCommand(Command);
}


/*
  DAC960_MonitoringTimerFunction is the timer function for monitoring
  the status of DAC960 Controllers.
*/

static void DAC960_MonitoringTimerFunction(unsigned long TimerData)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) TimerData;
  DAC960_Command_T *Command;
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
  /*
    Queue a Status Monitoring Command to Controller.
  */
  Command = DAC960_AllocateCommand(Controller);
  if (Command != NULL)
    DAC960_QueueMonitoringCommand(Command);
  else Controller->MonitoringCommandDeferred = true;
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
}


/*
  DAC960_Open is the Device Open Function for the DAC960 Driver.
*/

static int DAC960_Open(Inode_T *Inode, File_T *File)
{
  int ControllerNumber = DAC960_ControllerNumber(Inode->i_rdev);
  int LogicalDriveNumber = DAC960_LogicalDriveNumber(Inode->i_rdev);
  DAC960_Controller_T *Controller;
  if (ControllerNumber == 0 && LogicalDriveNumber == 0 &&
      (File->f_flags & O_NONBLOCK))
    goto ModuleOnly;
  if (ControllerNumber < 0 || ControllerNumber > DAC960_ControllerCount - 1)
    return -ENXIO;
  Controller = DAC960_Controllers[ControllerNumber];
  if (Controller == NULL ||
      LogicalDriveNumber > Controller->LogicalDriveCount - 1)
    return -ENXIO;
  if (Controller->LogicalDriveInformation
		  [Controller->LogicalDriveInformationIndex]
		  [LogicalDriveNumber].LogicalDriveState
      == DAC960_LogicalDrive_Offline)
    return -ENXIO;
  if (Controller->LogicalDriveInitialState[LogicalDriveNumber]
      == DAC960_LogicalDrive_Offline)
    {
      Controller->LogicalDriveInitialState[LogicalDriveNumber] =
	DAC960_LogicalDrive_Online;
      DAC960_InitializeGenericDiskInfo(&Controller->GenericDiskInfo);
    }
  if (Controller->GenericDiskInfo.sizes[MINOR(Inode->i_rdev)] == 0)
    return -ENXIO;
  /*
    Increment Controller and Logical Drive Usage Counts.
  */
  Controller->ControllerUsageCount++;
  Controller->LogicalDriveUsageCount[LogicalDriveNumber]++;
 ModuleOnly:
  MOD_INC_USE_COUNT;
  return 0;
}


/*
  DAC960_Release is the Device Release Function for the DAC960 Driver.
*/

static int DAC960_Release(Inode_T *Inode, File_T *File)
{
  int ControllerNumber = DAC960_ControllerNumber(Inode->i_rdev);
  int LogicalDriveNumber = DAC960_LogicalDriveNumber(Inode->i_rdev);
  DAC960_Controller_T *Controller = DAC960_Controllers[ControllerNumber];
  if (ControllerNumber == 0 && LogicalDriveNumber == 0 &&
      File != NULL && (File->f_flags & O_NONBLOCK))
    goto ModuleOnly;
  /*
    Decrement the Logical Drive and Controller Usage Counts.
  */
  Controller->LogicalDriveUsageCount[LogicalDriveNumber]--;
  Controller->ControllerUsageCount--;
 ModuleOnly:
  MOD_DEC_USE_COUNT;
  return 0;
}


/*
  DAC960_IOCTL is the Device IOCTL Function for the DAC960 Driver.
*/

static int DAC960_IOCTL(Inode_T *Inode, File_T *File,
			unsigned int Request, unsigned long Argument)
{
  int ControllerNumber = DAC960_ControllerNumber(Inode->i_rdev);
  int LogicalDriveNumber = DAC960_LogicalDriveNumber(Inode->i_rdev);
  DiskGeometry_T Geometry, *UserGeometry;
  DAC960_Controller_T *Controller;
  int PartitionNumber;
  if (File->f_flags & O_NONBLOCK)
    return DAC960_UserIOCTL(Inode, File, Request, Argument);
  if (ControllerNumber < 0 || ControllerNumber > DAC960_ControllerCount - 1)
    return -ENXIO;
  Controller = DAC960_Controllers[ControllerNumber];
  if (Controller == NULL ||
      LogicalDriveNumber > Controller->LogicalDriveCount - 1)
    return -ENXIO;
  switch (Request)
    {
    case HDIO_GETGEO:
      /* Get BIOS Disk Geometry. */
      UserGeometry = (DiskGeometry_T *) Argument;
      if (UserGeometry == NULL) return -EINVAL;
      Geometry.heads = Controller->GeometryTranslationHeads;
      Geometry.sectors = Controller->GeometryTranslationSectors;
      Geometry.cylinders =
	Controller->LogicalDriveInformation
		    [Controller->LogicalDriveInformationIndex]
		    [LogicalDriveNumber].LogicalDriveSize
	/ (Controller->GeometryTranslationHeads *
	   Controller->GeometryTranslationSectors);
      Geometry.start = Controller->GenericDiskInfo.part[MINOR(Inode->i_rdev)]
						  .start_sect;
      return copy_to_user(UserGeometry, &Geometry, sizeof(DiskGeometry_T));
    case BLKGETSIZE:
      /* Get Device Size. */
      if ((long *) Argument == NULL) return -EINVAL;
      return put_user(Controller->GenericDiskInfo.part[MINOR(Inode->i_rdev)]
						 .nr_sects,
		      (long *) Argument);
    case BLKRAGET:
      /* Get Read-Ahead. */
      if ((long *) Argument == NULL) return -EINVAL;
      return put_user(read_ahead[MAJOR(Inode->i_rdev)], (long *) Argument);
    case BLKRASET:
      /* Set Read-Ahead. */
      if (!capable(CAP_SYS_ADMIN)) return -EACCES;
      if (Argument > 256) return -EINVAL;
      read_ahead[MAJOR(Inode->i_rdev)] = Argument;
      return 0;
    case BLKFLSBUF:
      /* Flush Buffers. */
      if (!capable(CAP_SYS_ADMIN)) return -EACCES;
      fsync_dev(Inode->i_rdev);
      invalidate_buffers(Inode->i_rdev);
      return 0;
    case BLKRRPART:
      /* Re-Read Partition Table. */
      if (!capable(CAP_SYS_ADMIN)) return -EACCES;
      if (Controller->LogicalDriveUsageCount[LogicalDriveNumber] > 1)
	return -EBUSY;
      for (PartitionNumber = 0;
	   PartitionNumber < DAC960_MaxPartitions;
	   PartitionNumber++)
	{
	  KernelDevice_T Device = DAC960_KernelDevice(ControllerNumber,
						      LogicalDriveNumber,
						      PartitionNumber);
	  int MinorNumber = DAC960_MinorNumber(LogicalDriveNumber,
					       PartitionNumber);
	  SuperBlock_T *SuperBlock = get_super(Device);
	  if (Controller->GenericDiskInfo.part[MinorNumber].nr_sects == 0)
	    continue;
	  /*
	    Flush all changes and invalidate buffered state.
	  */
	  sync_dev(Device);
	  if (SuperBlock != NULL)
	    invalidate_inodes(SuperBlock);
	  invalidate_buffers(Device);
	  /*
	    Clear existing partition sizes.
	  */
	  if (PartitionNumber > 0)
	    {
	      Controller->GenericDiskInfo.part[MinorNumber].start_sect = 0;
	      Controller->GenericDiskInfo.part[MinorNumber].nr_sects = 0;
	    }
	  /*
	    Reset the Block Size so that the partition table can be read.
	  */
	  set_blocksize(Device, BLOCK_SIZE);
	}
      /*
       * Leonard, I'll tie you, draw around you a pentagram
       * and read this file. Aloud. 
       */
      grok_partitions(
	&Controller->GenericDiskInfo, LogicalDriveNumber, DAC960_MaxPartitions,
	Controller->LogicalDriveInformation[Controller->LogicalDriveInformationIndex][LogicalDriveNumber].LogicalDriveSize);
      return 0;
    }
  return -EINVAL;
}


/*
  DAC960_UserIOCTL is the User IOCTL Function for the DAC960 Driver.
*/

static int DAC960_UserIOCTL(Inode_T *Inode, File_T *File,
			    unsigned int Request, unsigned long Argument)
{
  int ErrorCode;
  if (!capable(CAP_SYS_ADMIN)) return -EACCES;
  switch (Request)
    {
    case DAC960_IOCTL_GET_CONTROLLER_COUNT:
      return DAC960_ControllerCount;
    case DAC960_IOCTL_GET_CONTROLLER_INFO:
      {
	DAC960_ControllerInfo_T *UserSpaceControllerInfo =
	  (DAC960_ControllerInfo_T *) Argument;
	DAC960_ControllerInfo_T ControllerInfo;
	DAC960_Controller_T *Controller;
	int ControllerNumber;
	if (UserSpaceControllerInfo == NULL) return -EINVAL;
	ErrorCode = get_user(ControllerNumber,
			     &UserSpaceControllerInfo->ControllerNumber);
	if (ErrorCode != 0) return ErrorCode;
	if (ControllerNumber < 0 ||
	    ControllerNumber > DAC960_ControllerCount - 1)
	  return -ENXIO;
	Controller = DAC960_Controllers[ControllerNumber];
	if (Controller == NULL)
	  return -ENXIO;
	memset(&ControllerInfo, 0, sizeof(DAC960_ControllerInfo_T));
	ControllerInfo.ControllerNumber = ControllerNumber;
	ControllerInfo.PCI_Bus = Controller->Bus;
	ControllerInfo.PCI_Device = Controller->Device;
	ControllerInfo.PCI_Function = Controller->Function;
	ControllerInfo.IRQ_Channel = Controller->IRQ_Channel;
	ControllerInfo.Channels = Controller->Channels;
	ControllerInfo.PCI_Address = Controller->PCI_Address;
	strcpy(ControllerInfo.ModelName, Controller->ModelName);
	strcpy(ControllerInfo.FirmwareVersion, Controller->FirmwareVersion);
	return copy_to_user(UserSpaceControllerInfo, &ControllerInfo,
			    sizeof(DAC960_ControllerInfo_T));
      }
    case DAC960_IOCTL_EXECUTE_COMMAND:
      {
	DAC960_UserCommand_T *UserSpaceUserCommand =
	  (DAC960_UserCommand_T *) Argument;
	DAC960_UserCommand_T UserCommand;
	DAC960_Controller_T *Controller;
	DAC960_Command_T *Command = NULL;
	DAC960_CommandOpcode_T CommandOpcode;
	DAC960_CommandStatus_T CommandStatus;
	DAC960_DCDB_T DCDB;
	ProcessorFlags_T ProcessorFlags;
	int ControllerNumber, DataTransferLength;
	unsigned char *DataTransferBuffer = NULL;
	if (UserSpaceUserCommand == NULL) return -EINVAL;
	ErrorCode = copy_from_user(&UserCommand, UserSpaceUserCommand,
				   sizeof(DAC960_UserCommand_T));
	if (ErrorCode != 0) goto Failure;
	ControllerNumber = UserCommand.ControllerNumber;
	if (ControllerNumber < 0 ||
	    ControllerNumber > DAC960_ControllerCount - 1)
	  return -ENXIO;
	Controller = DAC960_Controllers[ControllerNumber];
	if (Controller == NULL)
	  return -ENXIO;
	CommandOpcode = UserCommand.CommandMailbox.Common.CommandOpcode;
	DataTransferLength = UserCommand.DataTransferLength;
	if (CommandOpcode & 0x80) return -EINVAL;
	if (CommandOpcode == DAC960_DCDB)
	  {
	    ErrorCode =
	      copy_from_user(&DCDB, UserCommand.DCDB, sizeof(DAC960_DCDB_T));
	    if (ErrorCode != 0) goto Failure;
	    if (!((DataTransferLength == 0 &&
		   DCDB.Direction == DAC960_DCDB_NoDataTransfer) ||
		  (DataTransferLength > 0 &&
		   DCDB.Direction == DAC960_DCDB_DataTransferDeviceToSystem) ||
		  (DataTransferLength < 0 &&
		   DCDB.Direction == DAC960_DCDB_DataTransferSystemToDevice)))
	      return -EINVAL;
	    if (((DCDB.TransferLengthHigh4 << 16) | DCDB.TransferLength)
		!= abs(DataTransferLength))
	      return -EINVAL;
	  }
	if (DataTransferLength > 0)
	  {
	    DataTransferBuffer = kmalloc(DataTransferLength, GFP_KERNEL);
	    if (DataTransferBuffer == NULL) return -ENOMEM;
	    memset(DataTransferBuffer, 0, DataTransferLength);
	  }
	else if (DataTransferLength < 0)
	  {
	    DataTransferBuffer = kmalloc(-DataTransferLength, GFP_KERNEL);
	    if (DataTransferBuffer == NULL) return -ENOMEM;
	    ErrorCode = copy_from_user(DataTransferBuffer,
				       UserCommand.DataTransferBuffer,
				       -DataTransferLength);
	    if (ErrorCode != 0) goto Failure;
	  }
	if (CommandOpcode == DAC960_DCDB)
	  {
	    while (true)
	      {
		DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
		if (!Controller->DirectCommandActive[DCDB.Channel]
						    [DCDB.TargetID])
		  Command = DAC960_AllocateCommand(Controller);
		if (Command != NULL)
		  Controller->DirectCommandActive[DCDB.Channel]
						 [DCDB.TargetID] = true;
		DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
		if (Command != NULL) break;
		sleep_on(&Controller->CommandWaitQueue);
	      }
	    DAC960_ClearCommand(Command);
	    Command->CommandType = DAC960_ImmediateCommand;
	    memcpy(&Command->CommandMailbox, &UserCommand.CommandMailbox,
		   sizeof(DAC960_CommandMailbox_T));
	    Command->CommandMailbox.Type3.BusAddress = Virtual_to_Bus(&DCDB);
	    DCDB.BusAddress = Virtual_to_Bus(DataTransferBuffer);
	  }
	else
	  {
	    while (true)
	      {
		DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
		Command = DAC960_AllocateCommand(Controller);
		DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
		if (Command != NULL) break;
		sleep_on(&Controller->CommandWaitQueue);
	      }
	    DAC960_ClearCommand(Command);
	    Command->CommandType = DAC960_ImmediateCommand;
	    memcpy(&Command->CommandMailbox, &UserCommand.CommandMailbox,
		   sizeof(DAC960_CommandMailbox_T));
	    if (DataTransferBuffer != NULL)
	      Command->CommandMailbox.Type3.BusAddress =
		Virtual_to_Bus(DataTransferBuffer);
	  }
	DAC960_ExecuteCommand(Command);
	CommandStatus = Command->CommandStatus;
	DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
	DAC960_DeallocateCommand(Command);
	DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
	if (CommandStatus == DAC960_NormalCompletion &&
	    DataTransferLength > 0)
	  {
	    ErrorCode = copy_to_user(UserCommand.DataTransferBuffer,
				     DataTransferBuffer, DataTransferLength);
	    if (ErrorCode != 0) goto Failure;
	  }
	if (CommandOpcode == DAC960_DCDB)
	  {
	    Controller->DirectCommandActive[DCDB.Channel]
					   [DCDB.TargetID] = false;
	    ErrorCode =
	      copy_to_user(UserCommand.DCDB, &DCDB, sizeof(DAC960_DCDB_T));
	    if (ErrorCode != 0) goto Failure;
	  }
	ErrorCode = CommandStatus;
      Failure:
	if (DataTransferBuffer != NULL)
	  kfree(DataTransferBuffer);
	return ErrorCode;
      }
    }
  return -EINVAL;
}


/*
  DAC960_KernelIOCTL is the Kernel IOCTL Function for the DAC960 Driver.
*/

int DAC960_KernelIOCTL(unsigned int Request, void *Argument)
{
  switch (Request)
    {
    case DAC960_IOCTL_GET_CONTROLLER_COUNT:
      return DAC960_ControllerCount;
    case DAC960_IOCTL_GET_CONTROLLER_INFO:
      {
	DAC960_ControllerInfo_T *ControllerInfo =
	  (DAC960_ControllerInfo_T *) Argument;
	DAC960_Controller_T *Controller;
	int ControllerNumber;
	if (ControllerInfo == NULL) return -EINVAL;
	ControllerNumber = ControllerInfo->ControllerNumber;
	if (ControllerNumber < 0 ||
	    ControllerNumber > DAC960_ControllerCount - 1)
	  return -ENXIO;
	Controller = DAC960_Controllers[ControllerNumber];
	if (Controller == NULL)
	  return -ENXIO;
	memset(ControllerInfo, 0, sizeof(DAC960_ControllerInfo_T));
	ControllerInfo->ControllerNumber = ControllerNumber;
	ControllerInfo->PCI_Bus = Controller->Bus;
	ControllerInfo->PCI_Device = Controller->Device;
	ControllerInfo->PCI_Function = Controller->Function;
	ControllerInfo->IRQ_Channel = Controller->IRQ_Channel;
	ControllerInfo->Channels = Controller->Channels;
	ControllerInfo->PCI_Address = Controller->PCI_Address;
	strcpy(ControllerInfo->ModelName, Controller->ModelName);
	strcpy(ControllerInfo->FirmwareVersion, Controller->FirmwareVersion);
	return 0;
      }
    case DAC960_IOCTL_EXECUTE_COMMAND:
      {
	DAC960_KernelCommand_T *KernelCommand =
	  (DAC960_KernelCommand_T *) Argument;
	DAC960_Controller_T *Controller;
	DAC960_Command_T *Command = NULL;
	DAC960_CommandOpcode_T CommandOpcode;
	DAC960_DCDB_T *DCDB = NULL;
	ProcessorFlags_T ProcessorFlags;
	int ControllerNumber, DataTransferLength;
	unsigned char *DataTransferBuffer = NULL;
	if (KernelCommand == NULL) return -EINVAL;
	ControllerNumber = KernelCommand->ControllerNumber;
	if (ControllerNumber < 0 ||
	    ControllerNumber > DAC960_ControllerCount - 1)
	  return -ENXIO;
	Controller = DAC960_Controllers[ControllerNumber];
	if (Controller == NULL)
	  return -ENXIO;
	CommandOpcode = KernelCommand->CommandMailbox.Common.CommandOpcode;
	DataTransferLength = KernelCommand->DataTransferLength;
	DataTransferBuffer = KernelCommand->DataTransferBuffer;
	if (CommandOpcode & 0x80) return -EINVAL;
	if (CommandOpcode == DAC960_DCDB)
	  {
	    DCDB = KernelCommand->DCDB;
	    if (!((DataTransferLength == 0 &&
		   DCDB->Direction == DAC960_DCDB_NoDataTransfer) ||
		  (DataTransferLength > 0 &&
		   DCDB->Direction == DAC960_DCDB_DataTransferDeviceToSystem) ||
		  (DataTransferLength < 0 &&
		   DCDB->Direction == DAC960_DCDB_DataTransferSystemToDevice)))
	      return -EINVAL;
	    if (((DCDB->TransferLengthHigh4 << 16) | DCDB->TransferLength)
		!= abs(DataTransferLength))
	      return -EINVAL;
	  }
	if (DataTransferLength != 0 && DataTransferBuffer == NULL)
	  return -EINVAL;
	if (DataTransferLength > 0)
	  memset(DataTransferBuffer, 0, DataTransferLength);
	if (CommandOpcode == DAC960_DCDB)
	  {
	    DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
	    if (!Controller->DirectCommandActive[DCDB->Channel]
						[DCDB->TargetID])
	      Command = DAC960_AllocateCommand(Controller);
	    if (Command == NULL)
	      {
		DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
		return -EBUSY;
	      }
	    else Controller->DirectCommandActive[DCDB->Channel]
						[DCDB->TargetID] = true;
	    DAC960_ClearCommand(Command);
	    Command->CommandType = DAC960_QueuedCommand;
	    memcpy(&Command->CommandMailbox, &KernelCommand->CommandMailbox,
		   sizeof(DAC960_CommandMailbox_T));
	    Command->CommandMailbox.Type3.BusAddress = Virtual_to_Bus(DCDB);
	    Command->KernelCommand = KernelCommand;
	    DCDB->BusAddress = Virtual_to_Bus(DataTransferBuffer);
	    DAC960_QueueCommand(Command);
	    DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
	  }
	else
	  {
	    DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
	    Command = DAC960_AllocateCommand(Controller);
	    if (Command == NULL)
	      {
		DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
		return -EBUSY;
	      }
	    DAC960_ClearCommand(Command);
	    Command->CommandType = DAC960_QueuedCommand;
	    memcpy(&Command->CommandMailbox, &KernelCommand->CommandMailbox,
		   sizeof(DAC960_CommandMailbox_T));
	    if (DataTransferBuffer != NULL)
	      Command->CommandMailbox.Type3.BusAddress =
		Virtual_to_Bus(DataTransferBuffer);
	    Command->KernelCommand = KernelCommand;
	    DAC960_QueueCommand(Command);
	    DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
	  }
	return 0;
      }
    }
  return -EINVAL;
}


/*
  DAC960_GenericDiskInit is the Generic Disk Information Initialization
  Function for the DAC960 Driver.
*/

static void DAC960_InitializeGenericDiskInfo(GenericDiskInfo_T *GenericDiskInfo)
{
  DAC960_Controller_T *Controller =
    (DAC960_Controller_T *) GenericDiskInfo->real_devices;
  DAC960_LogicalDriveInformation_T *LogicalDriveInformation =
    Controller->LogicalDriveInformation
		[Controller->LogicalDriveInformationIndex];
  int LogicalDriveNumber;
  for (LogicalDriveNumber = 0;
       LogicalDriveNumber < Controller->LogicalDriveCount;
       LogicalDriveNumber++)
	register_disk(GenericDiskInfo, MKDEV(GenericDiskInfo->major,
				       LogicalDriveNumber*DAC960_MaxPartitions),
		DAC960_MaxPartitions, &DAC960_FileOperations,
		LogicalDriveInformation[LogicalDriveNumber].LogicalDriveSize);
}


/*
  DAC960_Message prints Driver Messages.
*/

static void DAC960_Message(DAC960_MessageLevel_T MessageLevel,
			   char *Format,
			   DAC960_Controller_T *Controller,
			   ...)
{
  static char Buffer[DAC960_LineBufferSize];
  static boolean BeginningOfLine = true;
  va_list Arguments;
  int Length = 0;
  va_start(Arguments, Controller);
  Length = vsprintf(Buffer, Format, Arguments);
  va_end(Arguments);
  if (Controller == NULL)
    printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
	   DAC960_ControllerCount, Buffer);
  else if (MessageLevel == DAC960_AnnounceLevel ||
	   MessageLevel == DAC960_InfoLevel)
    {
      if (!Controller->ControllerInitialized)
	{
	  strcpy(&Controller->InitialStatusBuffer[
		    Controller->InitialStatusLength], Buffer);
	  Controller->InitialStatusLength += Length;
	  if (MessageLevel == DAC960_AnnounceLevel)
	    {
	      static int AnnouncementLines = 0;
	      if (++AnnouncementLines <= 2)
		printk("%sDAC960: %s", DAC960_MessageLevelMap[MessageLevel],
		       Buffer);
	    }
	  else
	    {
	      if (BeginningOfLine)
		{
		  if (Buffer[0] != '\n' || Length > 1)
		    printk("%sDAC960#%d: %s",
			   DAC960_MessageLevelMap[MessageLevel],
			   Controller->ControllerNumber, Buffer);
		}
	      else printk("%s", Buffer);
	    }
	}
      else
	{
	  strcpy(&Controller->CurrentStatusBuffer[
		    Controller->CurrentStatusLength], Buffer);
	  Controller->CurrentStatusLength += Length;
	}
    }
  else if (MessageLevel == DAC960_ProgressLevel)
    {
      strcpy(Controller->RebuildProgressBuffer, Buffer);
      Controller->RebuildProgressLength = Length;
      if (Controller->EphemeralProgressMessage)
	{
	  if (jiffies - Controller->LastProgressReportTime
	      >= DAC960_ProgressReportingInterval)
	    {
	      printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
		     Controller->ControllerNumber, Buffer);
	      Controller->LastProgressReportTime = jiffies;
	    }
	}
      else printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
		  Controller->ControllerNumber, Buffer);
    }
  else if (MessageLevel == DAC960_UserCriticalLevel)
    {
      strcpy(&Controller->UserStatusBuffer[Controller->UserStatusLength],
	     Buffer);
      Controller->UserStatusLength += Length;
      if (Buffer[0] != '\n' || Length > 1)
	printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
	       Controller->ControllerNumber, Buffer);
    }
  else
    {
      if (BeginningOfLine)
	printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
	       Controller->ControllerNumber, Buffer);
      else printk("%s", Buffer);
    }
  BeginningOfLine = (Buffer[Length-1] == '\n');
}


/*
  DAC960_ParsePhysicalDrive parses spaces followed by a Physical Drive
  Channel:TargetID specification from a User Command string.  It updates
  Channel and TargetID and returns true on success and returns false otherwise.
*/

static boolean DAC960_ParsePhysicalDrive(DAC960_Controller_T *Controller,
					 char *UserCommandString,
					 unsigned char *Channel,
					 unsigned char *TargetID)
{
  char *NewUserCommandString = UserCommandString;
  unsigned long XChannel, XTargetID;
  while (*UserCommandString == ' ') UserCommandString++;
  if (UserCommandString == NewUserCommandString)
    return false;
  XChannel = simple_strtoul(UserCommandString, &NewUserCommandString, 10);
  if (NewUserCommandString == UserCommandString ||
      *NewUserCommandString != ':' ||
      XChannel >= Controller->Channels)
    return false;
  UserCommandString = ++NewUserCommandString;
  XTargetID = simple_strtoul(UserCommandString, &NewUserCommandString, 10);
  if (NewUserCommandString == UserCommandString ||
      *NewUserCommandString != '\0' ||
      XTargetID >= DAC960_MaxTargets)
    return false;
  *Channel = XChannel;
  *TargetID = XTargetID;
  return true;
}


/*
  DAC960_ParseLogicalDrive parses spaces followed by a Logical Drive Number
  specification from a User Command string.  It updates LogicalDriveNumber and
  returns true on success and returns false otherwise.
*/

static boolean DAC960_ParseLogicalDrive(DAC960_Controller_T *Controller,
					char *UserCommandString,
					unsigned char *LogicalDriveNumber)
{
  char *NewUserCommandString = UserCommandString;
  unsigned long XLogicalDriveNumber;
  while (*UserCommandString == ' ') UserCommandString++;
  if (UserCommandString == NewUserCommandString)
    return false;
  XLogicalDriveNumber =
    simple_strtoul(UserCommandString, &NewUserCommandString, 10);
  if (NewUserCommandString == UserCommandString ||
      *NewUserCommandString != '\0' ||
      XLogicalDriveNumber >= Controller->LogicalDriveCount)
    return false;
  *LogicalDriveNumber = XLogicalDriveNumber;
  return true;
}


/*
  DAC960_SetDeviceState sets the Device State for a Physical Drive.
*/

static void DAC960_SetDeviceState(DAC960_Controller_T *Controller,
				  DAC960_Command_T *Command,
				  unsigned char Channel,
				  unsigned char TargetID,
				  DAC960_PhysicalDeviceState_T DeviceState,
				  const char *DeviceStateString)
{
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  CommandMailbox->Type3D.CommandOpcode = DAC960_StartDevice;
  CommandMailbox->Type3D.Channel = Channel;
  CommandMailbox->Type3D.TargetID = TargetID;
  CommandMailbox->Type3D.DeviceState = DeviceState;
  CommandMailbox->Type3D.Modifier = 0;
  DAC960_ExecuteCommand(Command);
  switch (Command->CommandStatus)
    {
    case DAC960_NormalCompletion:
      DAC960_UserCritical("%s of Physical Drive %d:%d Succeeded\n", Controller,
			  DeviceStateString, Channel, TargetID);
      break;
    case DAC960_UnableToStartDevice:
      DAC960_UserCritical("%s of Physical Drive %d:%d Failed - "
			  "Unable to Start Device\n", Controller,
			  DeviceStateString, Channel, TargetID);
      break;
    case DAC960_NoDeviceAtAddress:
      DAC960_UserCritical("%s of Physical Drive %d:%d Failed - "
			  "No Device at Address\n", Controller,
			  DeviceStateString, Channel, TargetID);
      break;
    case DAC960_InvalidChannelOrTargetOrModifier:
      DAC960_UserCritical("%s of Physical Drive %d:%d Failed - "
			  "Invalid Channel or Target or Modifier\n",
			  Controller, DeviceStateString, Channel, TargetID);
      break;
    case DAC960_ChannelBusy:
      DAC960_UserCritical("%s of Physical Drive %d:%d Failed - "
			  "Channel Busy\n", Controller,
			  DeviceStateString, Channel, TargetID);
      break;
    default:
      DAC960_UserCritical("%s of Physical Drive %d:%d Failed - "
			  "Unexpected Status %04X\n", Controller,
			  DeviceStateString, Channel, TargetID,
			  Command->CommandStatus);
      break;
    }
}


/*
  DAC960_ExecuteUserCommand executes a User Command.
*/

static boolean DAC960_ExecuteUserCommand(DAC960_Controller_T *Controller,
					 char *UserCommand)
{
  DAC960_Command_T *Command;
  DAC960_CommandMailbox_T *CommandMailbox;
  ProcessorFlags_T ProcessorFlags;
  unsigned char Channel, TargetID, LogicalDriveNumber;
  while (true)
    {
      DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
      Command = DAC960_AllocateCommand(Controller);
      DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
      if (Command != NULL) break;
      sleep_on(&Controller->CommandWaitQueue);
    }
  Controller->UserStatusLength = 0;
  DAC960_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox = &Command->CommandMailbox;
  if (strcmp(UserCommand, "flush-cache") == 0)
    {
      CommandMailbox->Type3.CommandOpcode = DAC960_Flush;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Cache Flush Completed\n", Controller);
    }
  else if (strncmp(UserCommand, "kill", 4) == 0 &&
	   DAC960_ParsePhysicalDrive(Controller, &UserCommand[4],
				     &Channel, &TargetID))
    {
      DAC960_DeviceState_T *DeviceState =
	&Controller->DeviceState[Controller->DeviceStateIndex]
				[Channel][TargetID];
      if (DeviceState->Present &&
	  DeviceState->DeviceType == DAC960_DiskType &&
	  DeviceState->DeviceState != DAC960_Device_Dead)
	DAC960_SetDeviceState(Controller, Command, Channel, TargetID,
			      DAC960_Device_Dead, "Kill");
      else DAC960_UserCritical("Kill of Physical Drive %d:%d Illegal\n",
			       Controller, Channel, TargetID);
    }
  else if (strncmp(UserCommand, "make-online", 11) == 0 &&
	   DAC960_ParsePhysicalDrive(Controller, &UserCommand[11],
				     &Channel, &TargetID))
    {
      DAC960_DeviceState_T *DeviceState =
	&Controller->DeviceState[Controller->DeviceStateIndex]
				[Channel][TargetID];
      if (DeviceState->Present &&
	  DeviceState->DeviceType == DAC960_DiskType &&
	  DeviceState->DeviceState == DAC960_Device_Dead)
	DAC960_SetDeviceState(Controller, Command, Channel, TargetID,
			      DAC960_Device_Online, "Make Online");
      else DAC960_UserCritical("Make Online of Physical Drive %d:%d Illegal\n",
			       Controller, Channel, TargetID);

    }
  else if (strncmp(UserCommand, "make-standby", 12) == 0 &&
	   DAC960_ParsePhysicalDrive(Controller, &UserCommand[12],
				     &Channel, &TargetID))
    {
      DAC960_DeviceState_T *DeviceState =
	&Controller->DeviceState[Controller->DeviceStateIndex]
				[Channel][TargetID];
      if (DeviceState->Present &&
	  DeviceState->DeviceType == DAC960_DiskType &&
	  DeviceState->DeviceState == DAC960_Device_Dead)
	DAC960_SetDeviceState(Controller, Command, Channel, TargetID,
			      DAC960_Device_Standby, "Make Standby");
      else DAC960_UserCritical("Make Standby of Physical Drive %d:%d Illegal\n",
			       Controller, Channel, TargetID);
    }
  else if (strncmp(UserCommand, "rebuild", 7) == 0 &&
	   DAC960_ParsePhysicalDrive(Controller, &UserCommand[7],
				     &Channel, &TargetID))
    {
      CommandMailbox->Type3D.CommandOpcode = DAC960_RebuildAsync;
      CommandMailbox->Type3D.Channel = Channel;
      CommandMailbox->Type3D.TargetID = TargetID;
      DAC960_ExecuteCommand(Command);
      switch (Command->CommandStatus)
	{
	case DAC960_NormalCompletion:
	  DAC960_UserCritical("Rebuild of Physical Drive %d:%d Initiated\n",
			      Controller, Channel, TargetID);
	  break;
	case DAC960_AttemptToRebuildOnlineDrive:
	  DAC960_UserCritical("Rebuild of Physical Drive %d:%d Failed - "
			      "Attempt to Rebuild Online or "
			      "Unresponsive Drive\n",
			      Controller, Channel, TargetID);
	  break;
	case DAC960_NewDiskFailedDuringRebuild:
	  DAC960_UserCritical("Rebuild of Physical Drive %d:%d Failed - "
			      "New Disk Failed During Rebuild\n",
			      Controller, Channel, TargetID);
	  break;
	case DAC960_InvalidDeviceAddress:
	  DAC960_UserCritical("Rebuild of Physical Drive %d:%d Failed - "
			      "Invalid Device Address\n",
			      Controller, Channel, TargetID);
	  break;
	case DAC960_RebuildOrCheckAlreadyInProgress:
	  DAC960_UserCritical("Rebuild of Physical Drive %d:%d Failed - "
			      "Rebuild or Consistency Check Already "
			      "in Progress\n", Controller, Channel, TargetID);
	  break;
	default:
	  DAC960_UserCritical("Rebuild of Physical Drive %d:%d Failed - "
			      "Unexpected Status %04X\n", Controller,
			      Channel, TargetID, Command->CommandStatus);
	  break;
	}
    }
  else if (strncmp(UserCommand, "check-consistency", 17) == 0 &&
	   DAC960_ParseLogicalDrive(Controller, &UserCommand[17],
				    &LogicalDriveNumber))
    {
      CommandMailbox->Type3C.CommandOpcode = DAC960_CheckConsistencyAsync;
      CommandMailbox->Type3C.LogicalDriveNumber = LogicalDriveNumber;
      CommandMailbox->Type3C.AutoRestore = true;
      DAC960_ExecuteCommand(Command);
      switch (Command->CommandStatus)
	{
	case DAC960_NormalCompletion:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Initiated\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	  break;
	case DAC960_DependentDiskIsDead:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Failed - "
			      "Dependent Physical Drive is DEAD\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	  break;
	case DAC960_InvalidOrNonredundantLogicalDrive:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Failed - "
			      "Invalid or Nonredundant Logical Drive\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	  break;
	case DAC960_RebuildOrCheckAlreadyInProgress:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Failed - Rebuild or "
			      "Consistency Check Already in Progress\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	  break;
	default:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Failed - "
			      "Unexpected Status %04X\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber, Command->CommandStatus);
	  break;
	}
    }
  else if (strcmp(UserCommand, "cancel-rebuild") == 0 ||
	   strcmp(UserCommand, "cancel-consistency-check") == 0)
    {
      unsigned char OldRebuildRateConstant;
      CommandMailbox->Type3R.CommandOpcode = DAC960_RebuildControl;
      CommandMailbox->Type3R.RebuildRateConstant = 0xFF;
      CommandMailbox->Type3R.BusAddress =
	Virtual_to_Bus(&OldRebuildRateConstant);
      DAC960_ExecuteCommand(Command);
      switch (Command->CommandStatus)
	{
	case DAC960_NormalCompletion:
	  DAC960_UserCritical("Rebuild or Consistency Check Cancelled\n",
			      Controller);
	  break;
	default:
	  DAC960_UserCritical("Cancellation of Rebuild or "
			      "Consistency Check Failed - "
			      "Unexpected Status %04X\n",
			      Controller, Command->CommandStatus);
	  break;
	}
    }
  else DAC960_UserCritical("Illegal User Command: '%s'\n",
			   Controller, UserCommand);
  DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
  DAC960_DeallocateCommand(Command);
  DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
  return true;
}


/*
  DAC960_ProcReadStatus implements reading /proc/rd/status.
*/

static int DAC960_ProcReadStatus(char *Page, char **Start, off_t Offset,
				 int Count, int *EOF, void *Data)
{
  char *StatusMessage = "OK\n";
  int ControllerNumber, BytesAvailable;
  for (ControllerNumber = 0;
       ControllerNumber < DAC960_ControllerCount;
       ControllerNumber++)
    {
      DAC960_Controller_T *Controller = DAC960_Controllers[ControllerNumber];
      DAC960_Enquiry_T *Enquiry;
      if (Controller == NULL) continue;
      Enquiry = &Controller->Enquiry[Controller->EnquiryIndex];
      if (Enquiry->CriticalLogicalDriveCount > 0 ||
	  Enquiry->OfflineLogicalDriveCount > 0 ||
	  Enquiry->DeadDriveCount > 0)
	{
	  StatusMessage = "ALERT\n";
	  break;
	}
    }
  BytesAvailable = strlen(StatusMessage) - Offset;
  if (Count >= BytesAvailable)
    {
      Count = BytesAvailable;
      *EOF = true;
    }
  if (Count <= 0) return 0;
  *Start = Page;
  memcpy(Page, &StatusMessage[Offset], Count);
  return Count;
}


/*
  DAC960_ProcReadInitialStatus implements reading /proc/rd/cN/initial_status.
*/

static int DAC960_ProcReadInitialStatus(char *Page, char **Start, off_t Offset,
					int Count, int *EOF, void *Data)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) Data;
  int BytesAvailable = Controller->InitialStatusLength - Offset;
  if (Count >= BytesAvailable)
    {
      Count = BytesAvailable;
      *EOF = true;
    }
  if (Count <= 0) return 0;
  *Start = Page;
  memcpy(Page, &Controller->InitialStatusBuffer[Offset], Count);
  return Count;
}


/*
  DAC960_ProcReadCurrentStatus implements reading /proc/rd/cN/current_status.
*/

static int DAC960_ProcReadCurrentStatus(char *Page, char **Start, off_t Offset,
					int Count, int *EOF, void *Data)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) Data;
  int BytesAvailable;
  if (jiffies != Controller->LastCurrentStatusTime)
    {
      Controller->CurrentStatusLength = 0;
      DAC960_AnnounceDriver(Controller);
      DAC960_ReportControllerConfiguration(Controller);
      DAC960_ReportDeviceConfiguration(Controller);
      Controller->CurrentStatusBuffer[Controller->CurrentStatusLength++] = ' ';
      Controller->CurrentStatusBuffer[Controller->CurrentStatusLength++] = ' ';
      if (Controller->RebuildProgressLength > 0)
	{
	  strcpy(&Controller->CurrentStatusBuffer
			      [Controller->CurrentStatusLength],
		 Controller->RebuildProgressBuffer);
	  Controller->CurrentStatusLength += Controller->RebuildProgressLength;
	}
      else
	{
	  char *StatusMessage = "No Rebuild or Consistency Check in Progress\n";
	  strcpy(&Controller->CurrentStatusBuffer
			      [Controller->CurrentStatusLength],
		 StatusMessage);
	  Controller->CurrentStatusLength += strlen(StatusMessage);
	}
      Controller->LastCurrentStatusTime = jiffies;
    }
  BytesAvailable = Controller->CurrentStatusLength - Offset;
  if (Count >= BytesAvailable)
    {
      Count = BytesAvailable;
      *EOF = true;
    }
  if (Count <= 0) return 0;
  *Start = Page;
  memcpy(Page, &Controller->CurrentStatusBuffer[Offset], Count);
  return Count;
}


/*
  DAC960_ProcReadUserCommand implements reading /proc/rd/cN/user_command.
*/

static int DAC960_ProcReadUserCommand(char *Page, char **Start, off_t Offset,
				      int Count, int *EOF, void *Data)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) Data;
  int BytesAvailable = Controller->UserStatusLength - Offset;
  if (Count >= BytesAvailable)
    {
      Count = BytesAvailable;
      *EOF = true;
    }
  if (Count <= 0) return 0;
  *Start = Page;
  memcpy(Page, &Controller->UserStatusBuffer[Offset], Count);
  return Count;
}


/*
  DAC960_ProcWriteUserCommand implements writing /proc/rd/cN/user_command.
*/

static int DAC960_ProcWriteUserCommand(File_T *File, const char *Buffer,
				       unsigned long Count, void *Data)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) Data;
  char CommandBuffer[80];
  int Length;
  if (Count > sizeof(CommandBuffer)-1) return -EINVAL;
  copy_from_user(CommandBuffer, Buffer, Count);
  CommandBuffer[Count] = '\0';
  Length = strlen(CommandBuffer);
  if (CommandBuffer[Length-1] == '\n')
    CommandBuffer[--Length] = '\0';
  return (DAC960_ExecuteUserCommand(Controller, CommandBuffer)
	  ? Count : -EBUSY);
}


/*
  DAC960_CreateProcEntries creates the /proc/driver/rd/... entries
  for the DAC960 Driver.
*/

static void DAC960_CreateProcEntries(void)
{
  static PROC_DirectoryEntry_T *StatusProcEntry;
  int ControllerNumber;
  DAC960_ProcDirectoryEntry = proc_mkdir("driver/rd", NULL);
  StatusProcEntry = create_proc_read_entry("status", 0,
					   DAC960_ProcDirectoryEntry,
					   DAC960_ProcReadStatus, NULL);
  for (ControllerNumber = 0;
       ControllerNumber < DAC960_ControllerCount;
       ControllerNumber++)
    {
      DAC960_Controller_T *Controller = DAC960_Controllers[ControllerNumber];
      PROC_DirectoryEntry_T *ControllerProcEntry;
      PROC_DirectoryEntry_T *UserCommandProcEntry;
      if (Controller == NULL) continue;
      ControllerProcEntry = proc_mkdir(Controller->ControllerName,
					DAC960_ProcDirectoryEntry);
      create_proc_read_entry("initial_status",0,ControllerProcEntry,
			     DAC960_ProcReadInitialStatus, Controller);
      create_proc_read_entry("current_status",0,ControllerProcEntry,
			     DAC960_ProcReadCurrentStatus, Controller);
      UserCommandProcEntry =
		create_proc_read_entry("user_command", S_IWUSR|S_IRUSR,
					ControllerProcEntry,
					DAC960_ProcReadUserCommand, Controller);
      UserCommandProcEntry->write_proc = DAC960_ProcWriteUserCommand;
    }
}


/*
  DAC960_DestroyProcEntries destroys the /proc/rd/... entries for the DAC960
  Driver.
*/

static void DAC960_DestroyProcEntries(void)
{
  /* FIXME */
  remove_proc_entry("driver/rd", NULL);
}


/*
  Include Module support if requested.
*/

#ifdef MODULE


int init_module(void)
{
  int ControllerNumber;
  DAC960_Initialize();
  if (DAC960_ActiveControllerCount == 0) return -1;
  for (ControllerNumber = 0;
       ControllerNumber < DAC960_ControllerCount;
       ControllerNumber++)
    {
      DAC960_Controller_T *Controller = DAC960_Controllers[ControllerNumber];
      if (Controller == NULL) continue;
      DAC960_InitializeGenericDiskInfo(&Controller->GenericDiskInfo);
    }
  return 0;
}


void cleanup_module(void)
{
  DAC960_Finalize(&DAC960_NotifierBlock, SYS_RESTART, NULL);
}


#endif

/*
 * Low Level Driver for the IBM Microchannel SCSI Subsystem
 *
 * Copyright (c) 1995 Strom Systems, Inc. under the terms of the GNU 
 * General Public License. Written by Martin Kolinek, December 1995.
 */

/* Update history:
   Jan 15 1996:  First public release.

   Jan 23 1996:  Scrapped code which reassigned scsi devices to logical
   device numbers. Instead, the existing assignment (created
   when the machine is powered-up or rebooted) is used. 
   A side effect is that the upper layer of Linux SCSI 
   device driver gets bogus scsi ids (this is benign), 
   and also the hard disks are ordered under Linux the 
   same way as they are under dos (i.e., C: disk is sda, 
   D: disk is sdb, etc.).

   I think that the CD-ROM is now detected only if a CD is 
   inside CD_ROM while Linux boots. This can be fixed later,
   once the driver works on all types of PS/2's.

   Feb 7 1996:   Modified biosparam function. Fixed the CD-ROM detection. 
   For now, devices other than harddisk and CD_ROM are 
   ignored. Temporarily modified abort() function 
   to behave like reset(). 

   Mar 31 1996:  The integrated scsi subsystem is correctly found
   in PS/2 models 56,57, but not in model 76. Therefore
   the ibmmca_scsi_setup() function has been added today.
   This function allows the user to force detection of
   scsi subsystem. The kernel option has format
   ibmmcascsi=n
   where n is the scsi_id (pun) of the subsystem. Most
   likely, n is 7. 

   Aug 21 1996:  Modified the code which maps ldns to (pun,0).  It was
   insufficient for those of us with CD-ROM changers.
   - Chris Beauregard
 */

#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/blk.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mca.h>
#include <asm/system.h>
#include <asm/io.h>
#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "ibmmca.h"

/*--------------------------------------------------------------------*/

/*
   Driver Description

   (A) Subsystem Detection
   This is done in find_adapter() function and is easy, since
   the information about MCA integrated subsystems and plug-in 
   adapters is readily available in structure *mca_info.

   In case you have more than one SCSI subsystem, only one can be used,
   and its I/O port addresses should be standard 0x3540-7. To use more
   than one adapter, the sharing of interrupt 14 would have to be 
   supported in Linux (it is not at present, but could be, since MCA 
   interrupts are level-triggered).

   (B) Physical Units, Logical Units, and Logical Devices
   There can be up to 56 devices on SCSI bus (besides the adapter):
   there are up to 7 "physical units" (each identified by physical unit 
   number or pun, also called the scsi id, this is the number you select
   with hardware jumpers), and each physical unit can have up to 8 
   "logical units" (each identified by logical unit number, or lun, 
   between 0 and 7). 

   Typically the adapter has pun=7, so puns of other physical units
   are between 0 and 6. Almost all physical units have only one   
   logical unit, with lun=0. A CD-ROM jukebox would be an example of 
   a physical unit with more than one logical unit.

   The embedded microprocessor of IBM SCSI subsystem hides the complex
   two-dimensional (pun,lun) organization from the operating system.
   When the machine is powered-up (or rebooted, I am not sure), the 
   embedded microprocessor checks, on it own, all 56 possible (pun,lun) 
   combinations, and first 15 devices found are assigned into a 
   one-dimensional array of so-called "logical devices", identified by 
   "logical device numbers" or ldn. The last ldn=15 is reserved for 
   the subsystem itself. 

   One consequence of information hiding is that the real (pun,lun)    
   numbers are also hidden. Therefore this driver takes the following
   approach: It checks the ldn's (0 to 6) to find out which ldn's
   have devices assigned. This is done by function check_devices() and
   device_exists(). The interrupt handler has a special paragraph of code
   (see local_checking_phase_flag) to assist in the checking. Assume, for
   example, that three logical devices were found assigned at ldn 0, 1, 2.
   These are presented to the upper layer of Linux SCSI driver
   as devices with bogus (pun, lun) equal to (0,0), (1,0), (2,0). 
   On the other hand, if the upper layer issues a command to device
   say (4,0), this driver returns DID_NO_CONNECT error.

   That last paragraph is no longer correct, but is left for
   historical purposes.  It limited the number of devices to 7, far
   fewer than the 15 that it could use.  Now it just maps
   ldn -> (ldn/8,ldn%8).  We end up with a real mishmash of puns
   and luns, but it all seems to work. - Chris Beaurgard

   (C) Regular Processing 
   Only three functions get involved: ibmmca_queuecommand(), issue_cmd(),
   and interrupt_handler().

   The upper layer issues a scsi command by calling function 
   ibmmca_queuecommand(). This function fills a "subsystem control block"
   (scb) and calls a local function issue_cmd(), which writes a scb 
   command into subsystem I/O ports. Once the scb command is carried out, 
   interrupt_handler() is invoked.

   (D) Abort, Reset.
   These are implemented with busy waiting for interrupt to arrive.
   The abort does not worked well for me, so I instead call the 
   ibmmca_reset() from the ibmmca_abort() function.

   (E) Disk Geometry
   The ibmmca_biosparams() function should return same disk geometry 
   as bios. This is needed for fdisk, etc. The returned geometry is 
   certainly correct for disk smaller than 1 gigabyte, but I am not 
   100% sure that it is correct for larger disks.

   (F) Kernel Boot Option 
   The function ibmmca_scsi_setup() is called if option ibmmcascsi=n 
   is passed to the kernel. See file linux/init/main.c for details.
 */

/*--------------------------------------------------------------------*/

/* Here are the values and structures specific for the subsystem. 
 * The source of information is "Update for the PS/2 Hardware 
 * Interface Technical Reference, Common Interfaces", September 1991, 
 * part number 04G3281, available in the U.S. for $21.75 at 
 * 1-800-IBM-PCTB, elsewhere call your local friendly IBM 
 * representative.
 * In addition to SCSI subsystem, this update contains fairly detailed 
 * (at hardware register level) sections on diskette  controller,
 * keyboard controller, serial port controller, VGA, and XGA.
 */

/*addresses of hardware registers on the subsystem */
#define IM_CMD_REG   0x3540	/*Command Interface, (4 bytes long) */
#define IM_ATTN_REG  0x3544	/*Attention (1 byte) */
#define IM_CTR_REG   0x3545	/*Basic Control (1 byte) */
#define IM_INTR_REG  0x3546	/*Interrupt Status (1 byte, read only) */
#define IM_STAT_REG  0x3547	/*Basic Status (1 byte, read only) */

/*requests going into the upper nibble of the Attention register */
/*note: the lower nibble specifies the device(0-14), or subsystem(15) */
#define IM_IMM_CMD   0x10	/*immediate command */
#define IM_SCB       0x30	/*Subsystem Control Block command */
#define IM_LONG_SCB  0x40	/*long Subsystem Control Block command */
#define IM_EOI       0xe0	/*end-of-interrupt request */

/*values for bits 7,1,0 of Basic Control reg. (bits 6-2 reserved) */
#define IM_HW_RESET     0x80	/*hardware reset */
#define IM_ENABLE_DMA   0x02	/*enable subsystem's busmaster DMA */
#define IM_ENABLE_INTR  0x01	/*enable interrupts to the system */

/*to interpret the upper nibble of Interrupt Status register */
/*note: the lower nibble specifies the device(0-14), or subsystem(15) */
#define IM_SCB_CMD_COMPLETED               0x10
#define IM_SCB_CMD_COMPLETED_WITH_RETRIES  0x50
#define IM_ADAPTER_HW_FAILURE              0x70
#define IM_IMMEDIATE_CMD_COMPLETED         0xa0
#define IM_CMD_COMPLETED_WITH_FAILURE      0xc0
#define IM_CMD_ERROR                       0xe0
#define IM_SOFTWARE_SEQUENCING_ERROR       0xf0

/*to interpret bits 3-0 of Basic Status register (bits 7-4 reserved) */
#define IM_CMD_REG_FULL   0x08
#define IM_CMD_REG_EMPTY  0x04
#define IM_INTR_REQUEST   0x02
#define IM_BUSY           0x01

/*immediate commands (word written into low 2 bytes of command reg) */
#define IM_RESET_IMM_CMD        0x0400
#define IM_FORMAT_PREP_IMM_CMD  0x0417
#define IM_FEATURE_CTR_IMM_CMD  0x040c
#define IM_DMA_PACING_IMM_CMD   0x040d
#define IM_ASSIGN_IMM_CMD       0x040e
#define IM_ABORT_IMM_CMD        0x040f

/*SCB (Subsystem Control Block) structure */
struct im_scb
  {
    unsigned short command;	/*command word (read, etc.) */
    unsigned short enable;	/*enable word, modifies cmd */
    union
      {
	unsigned long log_blk_adr;	/*block address on SCSI device */
	unsigned char scsi_cmd_length;	/*6,10,12, for other scsi cmd */
      }
    u1;
    unsigned long sys_buf_adr;	/*physical system memory adr */
    unsigned long sys_buf_length;	/*size of sys mem buffer */
    unsigned long tsb_adr;	/*Termination Status Block adr */
    unsigned long scb_chain_adr;	/*optional SCB chain address */
    union
      {
	struct
	  {
	    unsigned short count;	/*block count, on SCSI device */
	    unsigned short length;	/*block length, on SCSI device */
	  }
	blk;
	unsigned char scsi_command[12];		/*other scsi command */
      }
    u2;
  };

/*structure scatter-gather element (for list of system memory areas) */
struct im_sge
  {
    void *address;
    unsigned long byte_length;
  };

/*values for SCB command word */
#define IM_NO_SYNCHRONOUS      0x0040	/*flag for any command */
#define IM_NO_DISCONNECT       0x0080	/*flag for any command */
#define IM_READ_DATA_CMD       0x1c01
#define IM_WRITE_DATA_CMD      0x1c02
#define IM_READ_VERIFY_CMD     0x1c03
#define IM_WRITE_VERIFY_CMD    0x1c04
#define IM_REQUEST_SENSE_CMD   0x1c08
#define IM_READ_CAPACITY_CMD   0x1c09
#define IM_DEVICE_INQUIRY_CMD  0x1c0b
#define IM_OTHER_SCSI_CMD_CMD  0x241f

/*values to set bits in the enable word of SCB */
#define IM_READ_CONTROL              0x8000
#define IM_REPORT_TSB_ONLY_ON_ERROR  0x4000
#define IM_RETRY_ENABLE              0x2000
#define IM_POINTER_TO_LIST           0x1000
#define IM_SUPRESS_EXCEPTION_SHORT   0x0400
#define IM_CHAIN_ON_NO_ERROR         0x0001

/*TSB (Termination Status Block) structure */
struct im_tsb
  {
    unsigned short end_status;
    unsigned short reserved1;
    unsigned long residual_byte_count;
    unsigned long sg_list_element_adr;
    unsigned short status_length;
    unsigned char dev_status;
    unsigned char cmd_status;
    unsigned char dev_error;
    unsigned char cmd_error;
    unsigned short reserved2;
    unsigned short reserved3;
    unsigned short low_of_last_scb_adr;
    unsigned short high_of_last_scb_adr;
  };

/*subsystem uses interrupt request level 14 */
#define IM_IRQ  14

/*PS2 disk led is turned on/off by bits 6,7 of system control port */
#define PS2_SYS_CTR  0x92
#define PS2_DISK_LED_ON()   outb(inb(PS2_SYS_CTR) | 0xc0, PS2_SYS_CTR)
#define PS2_DISK_LED_OFF()  outb(inb(PS2_SYS_CTR) & 0x3f, PS2_SYS_CTR)

/*--------------------------------------------------------------------*/

/*list of supported subsystems */
struct subsys_list_struct
  {
    unsigned short mca_id;
    char *description;
  };
struct subsys_list_struct subsys_list[] =
{
  {0x8efc, "IBM Fast SCSI-2 Adapter"},
  {0x8efd, "IBM 7568 Industrial Computer SCSI Adapter w/cache"},
  {0x8ef8, "IBM Expansion Unit SCSI Controller"},
  {0x8eff, "IBM SCSI Adapter w/Cache"},
  {0x8efe, "IBM SCSI Adapter"},
};

/*for /proc filesystem */
struct proc_dir_entry proc_scsi_ibmmca =
{
  PROC_SCSI_IBMMCA, 6, "ibmmca",
  S_IFDIR | S_IRUGO | S_IXUGO, 2
};

/*max number of logical devices (can be up to 15) */
#define MAX_LOG_DEV  15

/*local data for a logical device */
struct logical_device
  {
    struct im_scb scb;
    struct im_tsb tsb;
    struct im_sge sge[16];
    Scsi_Cmnd *cmd;
    int is_disk;
    int block_length;
  };
static struct logical_device ld[MAX_LOG_DEV];

/*if this is nonzero, ibmmcascsi option has been passed to the kernel */
static int setup_called = 0;

/*scsi id (physical unit number) of subsystem (set during detect) */
static int subsystem_pun;

/*array to convert (pun, lun) into logical device number */
static unsigned char get_ldn[8][8];

/*counter of concurrent disk read/writes, to turn on/off disk led */
static int disk_rw_in_progress;

/*used only when checking logical devices */
static int local_checking_phase_flag;
static int got_interrupt;
static int stat_result;

/*reset status and its values (used only when doing reset) */
static int reset_status;
#define IM_RESET_NOT_IN_PROGRESS   0
#define IM_RESET_IN_PROGRESS       1
#define IM_RESET_FINISHED_OK       2
#define IM_RESET_FINISHED_FAIL     3

/*local functions */
static void interrupt_handler (int irq, void *dev_id,
			       struct pt_regs *regs);
static void issue_cmd (unsigned long cmd_reg, unsigned char attn_reg);
static void internal_done (Scsi_Cmnd * cmd);
static int find_subsystem (void);
static void check_devices (void);
static int device_exists (int ldn, int *is_disk, int *block_length);

/*--------------------------------------------------------------------*/

static void 
interrupt_handler (int irq, void *dev_id,
		   struct pt_regs *regs)
{
  /*get command result and logical device */
  unsigned int intr_reg = inb (IM_INTR_REG);
  unsigned int cmd_result = intr_reg & 0xf0;
  unsigned int ldn = intr_reg & 0x0f;

  if (!inb (IM_STAT_REG) & IM_INTR_REQUEST)
    {
      printk ("ibmmca SCSI: spurious/shared interrupt?\n");
      return;
    }

  /*must wait for attention reg not busy, then send EOI to subsystem */
  while (1)
    {
      cli ();
      if (!(inb (IM_STAT_REG) & IM_BUSY))
	break;
      sti ();
    }
  outb (IM_EOI | ldn, IM_ATTN_REG);
  sti ();

  /*these should never happen (hw fails, or a local programming bug) */
  if (cmd_result == IM_ADAPTER_HW_FAILURE)
    panic ("IBM MCA SCSI: subsystem hardware failure.\n");
  if (cmd_result == IM_CMD_ERROR)
    panic ("IBM MCA SCSI: command error.\n");
  if (cmd_result == IM_SOFTWARE_SEQUENCING_ERROR)
    panic ("IBM MCA SCSI: software sequencing error.\n");

  /*only for local checking phase */
  if (local_checking_phase_flag)
    {
      stat_result = cmd_result;
      got_interrupt = 1;
      return;
    }

  /*handling of commands coming from upper level of scsi driver */
  else
    {
      Scsi_Cmnd *cmd;

      /*verify ldn, and may handle rare reset immediate command */
      if (ldn >= MAX_LOG_DEV)
	{
	  if (ldn == 0xf && reset_status == IM_RESET_IN_PROGRESS)
	    {
	      if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE)
		{
		  reset_status = IM_RESET_FINISHED_FAIL;
		}
	      else
		{
		  /*reset disk led counter, turn of disk led */
		  disk_rw_in_progress = 0;
		  PS2_DISK_LED_OFF ();
		  reset_status = IM_RESET_FINISHED_OK;
		}
	      return;
	    }
	  else
	    panic ("IBM MCA SCSI: invalid logical device number.\n");
	}

      /*if no command structure, just return, else clear cmd */
      cmd = ld[ldn].cmd;
      if (!cmd)
	return;
      ld[ldn].cmd = 0;

      /* printk("cmd=%02x ireg=%02x ds=%02x cs=%02x de=%02x ce=%02x\n", 
         cmd->cmnd[0], intr_reg, 
         ld[ldn].tsb.dev_status, ld[ldn].tsb.cmd_status, 
         ld[ldn].tsb.dev_error, ld[ldn].tsb.cmd_error); */

      /*if this is end of disk read/write, may turn off PS/2 disk led */
      if (ld[ldn].is_disk)
	{
	  switch (cmd->cmnd[0])
	    {
	    case READ_6:
	    case WRITE_6:
	    case READ_10:
	    case WRITE_10:
	      if (--disk_rw_in_progress == 0)
		PS2_DISK_LED_OFF ();
	    }
	}

      /*write device status into cmd->result, and call done function */
      if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE)
	cmd->result = ld[ldn].tsb.dev_status & 0x1e;
      else
	cmd->result = 0;
      (cmd->scsi_done) (cmd);
    }
}

/*--------------------------------------------------------------------*/

static void 
issue_cmd (unsigned long cmd_reg, unsigned char attn_reg)
{
  /*must wait for attention reg not busy */
  while (1)
    {
      cli ();
      if (!(inb (IM_STAT_REG) & IM_BUSY))
	break;
      sti ();
    }

  /*write registers and enable system interrupts */
  outl (cmd_reg, IM_CMD_REG);
  outb (attn_reg, IM_ATTN_REG);
  sti ();
}

/*--------------------------------------------------------------------*/

static void 
internal_done (Scsi_Cmnd * cmd)
{
  cmd->SCp.Status++;
}

/*--------------------------------------------------------------------*/

static int
ibmmca_getinfo (char *buf, int slot, void *dev)
{
  int len = 0;

  len += sprintf (buf + len, "Subsystem PUN: %d\n", subsystem_pun);
  len += sprintf (buf + len, "Detected at boot: %s\n",
		  setup_called ? "No" : "Yes");
  return len;
}

static int 
find_subsystem (void)
{
  int j, list_size, slot;
  unsigned char pos2, pos3;

  /*if this is not MCA machine, return "nothing found" */
  if (!MCA_bus)
    return 0;

  /*if ibmmcascsi setup option was passed to kernel, return "found" */
  if (setup_called)
    {
      printk ("IBM MCA SCSI: forced detection, scsi id=%d.\n",
	      subsystem_pun);
      return 1;
    }

  /*first look for the SCSI integrated on the motherboard */
  pos2 = mca_read_stored_pos (MCA_INTEGSCSI, 2);
  if ((pos2 & 1) == 0)
    {
      pos3 = mca_read_stored_pos (MCA_INTEGSCSI, 3);
      subsystem_pun = (pos3 & 0xe0) >> 5;
      printk ("IBM MCA SCSI: integrated SCSI found, scsi id=%d.\n",
	      subsystem_pun);
      mca_set_adapter_name (MCA_INTEGSCSI, "PS/2 Integrated SCSI");
      mca_set_adapter_procfn (MCA_INTEGSCSI, (MCA_ProcFn) ibmmca_getinfo,
			      NULL);
      return 1;
    }

  list_size = sizeof (subsys_list) / sizeof (struct subsys_list_struct);
  for (j = 0; j < list_size; j += 1)
    {
      if ((slot = mca_find_adapter (subsys_list[j].mca_id, 0)) != MCA_NOTFOUND)
	{
	  pos3 = mca_read_stored_pos (slot, 3);
	  subsystem_pun = (pos3 & 0xe0) >> 5;
	  printk ("IBM MCA SCSI: %s found in slot %d, scsi id=%d.\n",
		  subsys_list[j].description, slot + 1, subsystem_pun);

	  mca_set_adapter_name (slot, subsys_list[j].description);
	  mca_set_adapter_procfn (slot, (MCA_ProcFn) ibmmca_getinfo,
				  NULL);

	  return 1;
	}
    }

  /*return "nothing found" */
  return 0;
}

/*--------------------------------------------------------------------*/

static void 
check_devices (void)
{
  int is_disk, block_length;
  int ldn;
  int num_ldn = 0;

  /*check ldn's from 0 to MAX_LOG_DEV to find which devices exist */
  for (ldn = 0; ldn < MAX_LOG_DEV; ldn++)
    {
      if (device_exists (ldn, &is_disk, &block_length))
	{
	  printk ("IBM MCA SCSI: logical device found at ldn=%d.\n", ldn);
	  ld[ldn].is_disk = is_disk;
	  ld[ldn].block_length = block_length;
	  get_ldn[num_ldn / 8][num_ldn % 8] = ldn;
	  num_ldn++;
	}
    }

  return;
}

/*--------------------------------------------------------------------*/

static int 
device_exists (int ldn, int *is_disk, int *block_length)
{
  struct im_scb scb;
  struct im_tsb tsb;
  unsigned char buf[256];
  int retries;

  for (retries = 0; retries < 3; retries++)
    {
      /*fill scb with inquiry command */
      scb.command = IM_DEVICE_INQUIRY_CMD;
      scb.enable = IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
      /* I think this virt_to_bus is needed.. ??? AC */
      scb.sys_buf_adr = virt_to_bus((unsigned long) buf);
      scb.sys_buf_length = 255;
      scb.tsb_adr = virt_to_bus((unsigned long) &tsb);

      /*issue scb to passed ldn, and busy wait for interrupt */
      got_interrupt = 0;
      issue_cmd (virt_to_bus((unsigned long)) &scb, IM_SCB | ldn);
      while (!got_interrupt)
	barrier ();

      /*if command succesful, break */
      if (stat_result == IM_SCB_CMD_COMPLETED)
	break;
    }

  /*if all three retries failed, return "no device at this ldn" */
  if (retries >= 3)
    return 0;

  /*if device is CD_ROM, assume block size 2048 and return */
  if (buf[0] == TYPE_ROM)
    {
      *is_disk = 0;
      *block_length = 2048;
      return 1;
    }

  /*if device is disk, use "read capacity" to find its block size */
  if (buf[0] == TYPE_DISK)
    {
      *is_disk = 1;

      for (retries = 0; retries < 3; retries++)
	{
	  /*fill scb with read capacity command */
	  scb.command = IM_READ_CAPACITY_CMD;
	  scb.enable = IM_READ_CONTROL;
	  scb.sys_buf_adr = virt_to_bus((unsigned long) buf);
	  scb.sys_buf_length = 8;
	  scb.tsb_adr = virt_to_bus((unsigned long) &tsb);

	  /*issue scb to passed ldn, and busy wait for interrupt */
	  got_interrupt = 0;
	  issue_cmd (virt_to_bus((unsigned long) &scb), IM_SCB | ldn);
	  while (!got_interrupt)
	    barrier ();

	  /*if got capacity, get block length and return one device found */
	  if (stat_result == IM_SCB_CMD_COMPLETED)
	    {
	      *block_length = buf[7] + (buf[6] << 8) + (buf[5] << 16) + (buf[4] << 24);
	      return 1;
	    }
	}

      /*if all three retries failed, return "no device at this ldn" */
      if (retries >= 3)
	return 0;
    }

  /*for now, ignore tape and other devices - return 0 */
  return 0;
}

/*--------------------------------------------------------------------*/

void 
ibmmca_scsi_setup (char *str, int *ints)
{
  /*verify that one value (between 0 and 7) was specified */
  if (setup_called++ || ints[0] != 1 || ints[1] < 0 || ints[1] >= 8)
    {
      printk ("IBM MCA SCSI: usage: ibmmcascsi=<ADAPTER_SCSI_ID>\n");
      return;
    }
  subsystem_pun = ints[1];
}

/*--------------------------------------------------------------------*/

int 
ibmmca_detect (Scsi_Host_Template * template)
{
  /*initialize local data */
  memset (ld, 0, sizeof ld);
  memset (get_ldn, 0xff, sizeof get_ldn);
  disk_rw_in_progress = 0;
  reset_status = IM_RESET_NOT_IN_PROGRESS;

  /*search for the subsystem, return 0 if not found */
  if (!find_subsystem ())
    return 0;

  /*get interrupt request level */
  if (request_irq (IM_IRQ, interrupt_handler, SA_SHIRQ, "ibmmca", 0))
    {
      printk ("IBM MCA SCSI: Unable to get IRQ %d.\n", IM_IRQ);
      return 0;
    }

  /*check which logical devices exist */
  local_checking_phase_flag = 1;
  check_devices ();
  local_checking_phase_flag = 0;

  /*if got here, one ibm mca subsystem has been detected */
  return 1;
}

/*--------------------------------------------------------------------*/

int 
ibmmca_command (Scsi_Cmnd * cmd)
{
  ibmmca_queuecommand (cmd, internal_done);
  cmd->SCp.Status = 0;
  while (!cmd->SCp.Status)
    barrier ();
  return cmd->result;
}

/*--------------------------------------------------------------------*/

int 
ibmmca_queuecommand (Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
  unsigned int ldn;
  unsigned int scsi_cmd;
  struct im_scb *scb;

  /*if (target,lun) unassigned, return error */
  ldn = get_ldn[cmd->target][cmd->lun];
  if (ldn >= MAX_LOG_DEV)
    {
      cmd->result = DID_NO_CONNECT << 16;
      done (cmd);
      return 0;
    }

  /*verify there is no command already in progress for this log dev */
  if (ld[ldn].cmd)
    panic ("IBM MCA SCSI: cmd already in progress for this ldn.\n");

  /*save done in cmd, and save cmd for the interrupt handler */
  cmd->scsi_done = done;
  ld[ldn].cmd = cmd;

  /*fill scb information independent of the scsi command */
  scb = &(ld[ldn].scb);
  scb->enable = IM_REPORT_TSB_ONLY_ON_ERROR;
  scb->tsb_adr = virt_to_bus((unsigned long) &(ld[ldn].tsb));
  if (cmd->use_sg)
    {
      int i = cmd->use_sg;
      struct scatterlist *sl = (struct scatterlist *) cmd->request_buffer;
      if (i > 16)
	panic ("IBM MCA SCSI: scatter-gather list too long.\n");
      while (--i >= 0)
	{
	  ld[ldn].sge[i].address = virt_to_bus(sl[i].address);
	  ld[ldn].sge[i].byte_length = sl[i].length;
	}
      scb->enable |= IM_POINTER_TO_LIST;
      scb->sys_buf_adr = virt_to_bus((unsigned long) &(ld[ldn].sge[0]));
      scb->sys_buf_length = cmd->use_sg * sizeof (struct im_sge);
    }
  else
    {
      scb->sys_buf_adr = virt_to_bus((unsigned long) cmd->request_buffer);
      scb->sys_buf_length = cmd->request_bufflen;
    }

  /*fill scb information dependent on scsi command */
  scsi_cmd = cmd->cmnd[0];
  /* printk("issue scsi cmd=%02x to ldn=%d\n", scsi_cmd, ldn); */
  switch (scsi_cmd)
    {
    case READ_6:
    case WRITE_6:
    case READ_10:
    case WRITE_10:
      if (scsi_cmd == READ_6 || scsi_cmd == READ_10)
	{
	  scb->command = IM_READ_DATA_CMD;
	  scb->enable |= IM_READ_CONTROL;
	}
      else
	{
	  scb->command = IM_WRITE_DATA_CMD;
	}
      if (scsi_cmd == READ_6 || scsi_cmd == WRITE_6)
	{
	  scb->u1.log_blk_adr = (((unsigned) cmd->cmnd[3]) << 0) |
	    (((unsigned) cmd->cmnd[2]) << 8) |
	    ((((unsigned) cmd->cmnd[1]) & 0x1f) << 16);
	  scb->u2.blk.count = (unsigned) cmd->cmnd[4];
	}
      else
	{
	  scb->u1.log_blk_adr = (((unsigned) cmd->cmnd[5]) << 0) |
	    (((unsigned) cmd->cmnd[4]) << 8) |
	    (((unsigned) cmd->cmnd[3]) << 16) |
	    (((unsigned) cmd->cmnd[2]) << 24);
	  scb->u2.blk.count = (((unsigned) cmd->cmnd[8]) << 0) |
	    (((unsigned) cmd->cmnd[7]) << 8);
	}
      scb->u2.blk.length = ld[ldn].block_length;
      if (ld[ldn].is_disk)
	{
	  if (++disk_rw_in_progress == 1)
	    PS2_DISK_LED_ON ();
	}
      break;

    case INQUIRY:
      scb->command = IM_DEVICE_INQUIRY_CMD;
      scb->enable |= IM_READ_CONTROL |
	IM_SUPRESS_EXCEPTION_SHORT;
      break;

    case READ_CAPACITY:
      scb->command = IM_READ_CAPACITY_CMD;
      scb->enable |= IM_READ_CONTROL;
      /*the length of system memory buffer must be exactly 8 bytes */
      if (scb->sys_buf_length >= 8)
	scb->sys_buf_length = 8;
      break;

    case REQUEST_SENSE:
      scb->command = IM_REQUEST_SENSE_CMD;
      scb->enable |= IM_READ_CONTROL;
      break;

    default:
      scb->command = IM_OTHER_SCSI_CMD_CMD;
      scb->enable |= IM_READ_CONTROL |
	IM_SUPRESS_EXCEPTION_SHORT;
      scb->u1.scsi_cmd_length = cmd->cmd_len;
      memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
      break;
    }

  /*issue scb command, and return */
  issue_cmd (virt_to_bus((unsigned long) scb), IM_SCB | ldn);
  return 0;
}

/*--------------------------------------------------------------------*/

int 
ibmmca_abort (Scsi_Cmnd * cmd)
{
/*do a reset instead, since abort does not work well for me at present */
  return ibmmca_reset (cmd);

#if 0
  unsigned int ldn;
  void (*saved_done) (Scsi_Cmnd *);

  /*get logical device number, and disable system interrupts */
  printk ("IBM MCA SCSI: sending abort to device id=%d lun=%d.\n",
	  cmd->target, cmd->lun);
  ldn = get_ldn[cmd->target][cmd->lun];
  cli ();

  /*if cmd for this ldn has already finished, no need to abort */
  if (!ld[ldn].cmd)
    {
      sti ();
      return SCSI_ABORT_NOT_RUNNING;
    }

  /* Clear ld.cmd, save done function, install internal done, 
   * send abort immediate command (this enables sys. interrupts), 
   * and wait until the interrupt arrives. 
   */
  ld[ldn].cmd = 0;
  saved_done = cmd->scsi_done;
  cmd->scsi_done = internal_done;
  cmd->SCp.Status = 0;
  issue_cmd (IM_ABORT_IMM_CMD, IM_IMM_CMD | ldn);
  while (!cmd->SCp.Status)
    barrier ();

  /*if abort went well, call saved done, then return success or error */
  if (cmd->result == 0)
    {
      cmd->result |= DID_ABORT << 16;
      saved_done (cmd);
      return SCSI_ABORT_SUCCESS;
    }
  else
    return SCSI_ABORT_ERROR;
#endif
}

/*--------------------------------------------------------------------*/

int 
ibmmca_reset (Scsi_Cmnd * cmd)
{
  /*issue reset immediate command to subsystem, and wait for interrupt */
  printk ("IBM MCA SCSI: resetting all devices.\n");
  cli ();
  reset_status = IM_RESET_IN_PROGRESS;
  issue_cmd (IM_RESET_IMM_CMD, IM_IMM_CMD | 0xf);
  while (reset_status == IM_RESET_IN_PROGRESS)
    barrier ();

  /*if reset failed, just return error */
  if (reset_status == IM_RESET_FINISHED_FAIL)
    return SCSI_RESET_ERROR;

  /*so reset finished ok - call outstanding done's, and return success */
  printk ("IBM MCA SCSI: reset finished well.\n");
  {
    int i;
    for (i = 0; i < MAX_LOG_DEV; i++)
      {
	Scsi_Cmnd *cmd = ld[i].cmd;
	if (cmd && cmd->scsi_done)
	  {
	    ld[i].cmd = 0;
	    cmd->result = DID_RESET;
	    (cmd->scsi_done) (cmd);
	  }
      }
  }
  return SCSI_RESET_SUCCESS;
}

/*--------------------------------------------------------------------*/

int 
ibmmca_biosparam (Disk * disk, kdev_t dev, int *info)
{
  info[0] = 64;
  info[1] = 32;
  info[2] = disk->capacity / (info[0] * info[1]);
  if (info[2] >= 1024)
    {
      info[0] = 128;
      info[1] = 63;
      info[2] = disk->capacity / (info[0] * info[1]);
      if (info[2] >= 1024)
	{
	  info[0] = 255;
	  info[1] = 63;
	  info[2] = disk->capacity / (info[0] * info[1]);
	  if (info[2] >= 1024)
	    info[2] = 1023;
	}
    }
  return 0;
}

/*--------------------------------------------------------------------*/

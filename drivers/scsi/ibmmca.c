/*
 * Low Level Driver for the IBM Microchannel SCSI Subsystem
 *
 * Copyright (c) 1995 Strom Systems, Inc. under the terms of the GNU 
 * General Public License. Written by Martin Kolinek, December 1995.
 * Further development by: Chris Beauregard, Klaus Kudielka, Michael Lang
 * See the file README.ibmmca for a detailed description of this driver,
 * the commandline arguments and the history of its development.
 * See the WWW-page: http://www.uni-mainz.de/~langm000/linux.html for latest
 * updates and info.
 */

/******************* HEADER FILE INCLUDES ************************************/
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

/* choose adaption for Kernellevel */
#define local_LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/blk.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mca.h>
#include <asm/system.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "ibmmca.h"

#include <linux/config.h>		/* for CONFIG_SCSI_IBMMCA etc. */

/******************* LOCAL DEFINES *******************************************/

#ifndef mdelay
#define mdelay(a)    udelay((a) * 1000)
#endif

/*--------------------------------------------------------------------*/

/* current version of this driver-source: */
#define IBMMCA_SCSI_DRIVER_VERSION "3.1e"

/*--------------------------------------------------------------------*/

/* driver configuration */
#define IM_MAX_HOSTS      8             /* maximum number of host adapters */
#define IM_RESET_DELAY    60            /* seconds allowed for a reset */

/* driver debugging - #undef all for normal operation */

/* if defined: count interrupts and ignore this special one: */
#undef  IM_DEBUG_TIMEOUT  50
#define TIMEOUT_PUN   0
#define TIMEOUT_LUN   0
/* verbose interrupt: */
#undef  IM_DEBUG_INT
/* verbose queuecommand: */
#undef  IM_DEBUG_CMD
/* verbose queucommand for specific SCSI-device type: */
#undef  IM_DEBUG_CMD_SPEC_DEV
/* verbose device probing */
#undef  IM_DEBUG_PROBE

/* device type that shall be displayed on syslog (only during debugging): */
#define IM_DEBUG_CMD_DEVICE   TYPE_TAPE

/* relative addresses of hardware registers on a subsystem */
#define IM_CMD_REG(hi)   (hosts[(hi)]->io_port)   /*Command Interface, (4 bytes long) */
#define IM_ATTN_REG(hi)  (hosts[(hi)]->io_port+4) /*Attention (1 byte) */
#define IM_CTR_REG(hi)   (hosts[(hi)]->io_port+5) /*Basic Control (1 byte) */
#define IM_INTR_REG(hi)  (hosts[(hi)]->io_port+6) /*Interrupt Status (1 byte, r/o) */
#define IM_STAT_REG(hi)  (hosts[(hi)]->io_port+7) /*Basic Status (1 byte, read only) */

/* basic I/O-port of first adapter */
#define IM_IO_PORT   0x3540
/* maximum number of hosts that can be found */
#define IM_N_IO_PORT 8

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
#define IM_FEATURE_CTR_IMM_CMD  0x040c
#define IM_DMA_PACING_IMM_CMD   0x040d
#define IM_ASSIGN_IMM_CMD       0x040e
#define IM_ABORT_IMM_CMD        0x040f
#define IM_FORMAT_PREP_IMM_CMD  0x0417

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

/* unused, but supported, SCB commands */
#define IM_GET_COMMAND_COMPLETE_STATUS_CMD   0x1c07 /* command status */
#define IM_GET_POS_INFO_CMD                  0x1c0a /* returns neat stuff */
#define IM_READ_PREFETCH_CMD                 0x1c31 /* caching controller only */
#define IM_FOMAT_UNIT_CMD                    0x1c16 /* format unit */
#define IM_REASSIGN_BLOCK_CMD                0x1c18 /* in case of error */

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

/*--------------------------------------------------------------------*/
/*
	The model 95 doesn't have a standard activity light.  Instead it
	has a row of LEDs on the front.  We use the last one as the activity
	indicator if we think we're on a model 95.  I suspect the model id
	check will be either too narrow or too general, and some machines
	won't have an activity indicator.  Oh well...

	The regular PS/2 disk led is turned on/off by bits 6,7 of system
	control port.
*/

/* LED display-port (actually, last LED on display) */
#define MOD95_LED_PORT	   0x108
/* system-control-register of PS/2s with diskindicator */
#define PS2_SYS_CTR        0x92

/* The SCSI-ID(!) of the accessed SCSI-device is shown on PS/2-95 machines' LED
   displays. ldn is no longer displayed here, because the ldn mapping is now 
   done dynamically and the ldn <-> pun,lun maps can be looked-up at boottime 
   or during uptime in /proc/scsi/ibmmca/<host_no> in case of trouble, 
   interest, debugging or just for having fun. The left number gives the
   host-adapter number and the right shows the accessed SCSI-ID. */

/* use_display is set by the ibmmcascsi=display command line arg */
static int use_display = 0;
/* use_adisplay is set by ibmmcascsi=adisplay, which offers a higher
 * level of displayed luxus on PS/2 95 (really fancy! :-))) */
static int use_adisplay = 0;

#define PS2_DISK_LED_ON(ad,id) {\
	if( use_display ) { outb((char)(id+48), MOD95_LED_PORT ); \
        outb((char)(ad+48), MOD95_LED_PORT+1); } \
        else if( use_adisplay ) { if (id<7) outb((char)(id+48), \
	MOD95_LED_PORT+1+id); outb((char)(ad+48), MOD95_LED_PORT); } \
	else outb(inb(PS2_SYS_CTR) | 0xc0, PS2_SYS_CTR); \
}

/* bug fixed, Dec 15, 1997, where | was replaced by & here */
#define PS2_DISK_LED_OFF() {\
	if( use_display ) { outb( ' ', MOD95_LED_PORT ); \
        outb(' ', MOD95_LED_PORT+1); } \
        if ( use_adisplay ) { outb(' ',MOD95_LED_PORT ); \
	outb(' ',MOD95_LED_PORT+1); outb(' ',MOD95_LED_PORT+2); \
        outb(' ',MOD95_LED_PORT+3); outb(' ',MOD95_LED_PORT+4); \
        outb(' ',MOD95_LED_PORT+5); outb(' ',MOD95_LED_PORT+6); \
	outb(' ',MOD95_LED_PORT+7); } \
	else outb(inb(PS2_SYS_CTR) & 0x3f, PS2_SYS_CTR); \
}

/*--------------------------------------------------------------------*/

/*list of supported subsystems */
struct subsys_list_struct
  {
    unsigned short mca_id;
    char *description;
  };

/* List of possible IBM-SCSI-adapters */
struct subsys_list_struct subsys_list[] =
{
  {0x8efc, "IBM Fast SCSI-2 Adapter"}, /* special = 0 */
  {0x8efd, "IBM 7568 Industrial Computer SCSI Adapter w/cache"}, /* special = 1 */
  {0x8ef8, "IBM Expansion Unit SCSI Controller"},/* special = 2 */
  {0x8eff, "IBM SCSI Adapter w/Cache"}, /* special = 3 */
  {0x8efe, "IBM SCSI Adapter"}, /* special = 4 */
};

/*for /proc filesystem */
struct proc_dir_entry proc_scsi_ibmmca =
{
  PROC_SCSI_IBMMCA, 6, "ibmmca",
  S_IFDIR | S_IRUGO | S_IXUGO, 2,
  0, 0, 0, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL
};

/* Max number of logical devices (can be up from 0 to 14).  15 is the address
of the adapter itself. */
#define MAX_LOG_DEV  15

/*local data for a logical device */
struct logical_device
  {
    struct im_scb scb; /* SCSI-subsystem-control-block structure */
    struct im_tsb tsb; /* SCSI command complete status block structure */
    struct im_sge sge[16]; /* scatter gather list structure */
    unsigned char buf[256]; /* SCSI command return data buffer */
    Scsi_Cmnd *cmd;  /* SCSI-command that is currently in progress */

    int device_type; /* type of the SCSI-device. See include/scsi/scsi.h
		        for interpretation of the possible values */
    int block_length;/* blocksize of a particular logical SCSI-device */
  };

/* statistics of the driver during operations (for proc_info) */
struct Driver_Statistics
   {
      /* SCSI statistics on the adapter */
      int ldn_access[MAX_LOG_DEV+1];         /* total accesses on a ldn */
      int ldn_read_access[MAX_LOG_DEV+1];    /* total read-access on a ldn */
      int ldn_write_access[MAX_LOG_DEV+1];   /* total write-access on a ldn */
      int ldn_inquiry_access[MAX_LOG_DEV+1]; /* total inquiries on a ldn */
      int ldn_modeselect_access[MAX_LOG_DEV+1]; /* total mode selects on ldn */
      int total_accesses;                    /* total accesses on all ldns */
      int total_interrupts;                  /* total interrupts (should be
						same as total_accesses) */
      int total_errors;                      /* command completed with error */
      /* dynamical assignment statistics */
      int total_scsi_devices;                /* number of physical pun,lun */
      int dyn_flag;                          /* flag showing dynamical mode */
      int dynamical_assignments;             /* number of remappings of ldns */
      int ldn_assignments[MAX_LOG_DEV+1];    /* number of remappings of each
					        ldn */
   };

/* data structure for each host adapter */
struct ibmmca_hostdata
{
   /* array of logical devices: */
   struct logical_device _ld[MAX_LOG_DEV+1];
   /* array to convert (pun, lun) into logical device number: */
   unsigned char _get_ldn[8][8];
   /*array that contains the information about the physical SCSI-devices
    attached to this host adapter: */
   unsigned char _get_scsi[8][8];
   /* used only when checking logical devices: */
   int _local_checking_phase_flag;
   /* report received interrupt: */
   int _got_interrupt;
   /* report termination-status of SCSI-command: */
   int _stat_result;
   /* reset status (used only when doing reset): */
   int _reset_status;
   /* code of the last SCSI command (needed for panic info): */
   int _last_scsi_command[MAX_LOG_DEV+1];
   /* identifier of the last SCSI-command type */
   int _last_scsi_type[MAX_LOG_DEV+1];
   /* Counter that points on the next reassignable ldn for dynamical
    remapping. The default value is 7, that is the first reassignable
    number in the list at boottime: */
   int _next_ldn;
   /* Statistics-structure for this IBM-SCSI-host: */
   struct Driver_Statistics _IBM_DS;
   /* This hostadapters pos-registers pos2 and pos3 */
   unsigned _pos2, _pos3;
   /* assign a special variable, that contains dedicated info about the
    adaptertype */
   int _special;
};

/* macros to access host data structure */
#define subsystem_pun(hi) (hosts[(hi)]->this_id)
#define ld(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_ld)
#define get_ldn(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_get_ldn)
#define get_scsi(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_get_scsi)
#define local_checking_phase_flag(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_local_checking_phase_flag)
#define got_interrupt(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_got_interrupt)
#define stat_result(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_stat_result)
#define reset_status(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_reset_status)
#define last_scsi_command(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_last_scsi_command)
#define last_scsi_type(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_last_scsi_type)
#define next_ldn(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_next_ldn)
#define IBM_DS(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_IBM_DS)
#define special(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_special)
#define pos2(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_pos2)
#define pos3(hi) (((struct ibmmca_hostdata *) hosts[(hi)]->hostdata)->_pos3)

/* Define a arbitrary number as subsystem-marker-type. This number is, as
   described in the ANSI-SCSI-standard, not occupied by other device-types. */
#define TYPE_IBM_SCSI_ADAPTER   0x2F

/* Define 0xFF for no device type, because this type is not defined within
   the ANSI-SCSI-standard, therefore, it can be used and should not cause any
   harm. */
#define TYPE_NO_DEVICE          0xFF

/* define medium-changer. If this is not defined previously, e.g. Linux
   2.0.x, define this type here. */
#ifndef TYPE_MEDIUM_CHANGER
#define TYPE_MEDIUM_CHANGER     0x08
#endif

/* define possible operations for the immediate_assign command */
#define SET_LDN        0
#define REMOVE_LDN     1

/* ldn which is used to probe the SCSI devices */
#define PROBE_LDN      0

/* reset status flag contents */
#define IM_RESET_NOT_IN_PROGRESS         0
#define IM_RESET_IN_PROGRESS             1
#define IM_RESET_FINISHED_OK             2
#define IM_RESET_FINISHED_FAIL           3
#define IM_RESET_NOT_IN_PROGRESS_NO_INT  4
#define IM_RESET_FINISHED_OK_NO_INT      5

/* special flags for hostdata structure */
#define FORCED_DETECTION         100
#define INTEGRATED_SCSI          101

/* define undefined SCSI-command */
#define NO_SCSI                  0xffff

/*-----------------------------------------------------------------------*/

/* if this is nonzero, ibmmcascsi option has been passed to the kernel */
static int io_port[IM_MAX_HOSTS] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int scsi_id[IM_MAX_HOSTS] = { 7, 7, 7, 7, 7, 7, 7, 7 };


/*counter of concurrent disk read/writes, to turn on/off disk led */
static int disk_rw_in_progress = 0;

/* spinlock handling to avoid command clash while in operation */
spinlock_t info_lock  = SPIN_LOCK_UNLOCKED;
spinlock_t proc_lock  = SPIN_LOCK_UNLOCKED;
spinlock_t abort_lock = SPIN_LOCK_UNLOCKED;
spinlock_t reset_lock = SPIN_LOCK_UNLOCKED;
spinlock_t issue_lock = SPIN_LOCK_UNLOCKED;
spinlock_t intr_lock  = SPIN_LOCK_UNLOCKED;

/* host information */
static int found = 0;
static struct Scsi_Host *hosts[IM_MAX_HOSTS+1] = { NULL };
static unsigned int pos[8]; /* whole pos register-line */
/* Taking into account the additions, made by ZP Gu.
 * This selects now the preset value from the configfile and
 * offers the 'normal' commandline option to be accepted */
#ifdef CONFIG_IBMMCA_SCSI_ORDER_STANDARD
static char ibm_ansi_order = 1;
#else
static char ibm_ansi_order = 0;
#endif

/*-----------------------------------------------------------------------*/

/******************* FUNCTIONS IN FORWARD DECLARATION ************************/

static void interrupt_handler (int, void *, struct pt_regs *);
static void do_interrupt_handler (int, void *, struct pt_regs *);
static void issue_cmd (int, unsigned long, unsigned char);
static void internal_done (Scsi_Cmnd * cmd);
static void check_devices (int);
static int immediate_assign(int, unsigned int, unsigned int, unsigned int,
                            unsigned int);
#ifdef CONFIG_IBMMCA_SCSI_DEV_RESET
static int immediate_reset(int, unsigned int);
#endif
static int device_inquiry(int, int);
static int read_capacity(int, int);
static char *ti_p(int);
static char *ti_l(int);
static int device_exists (int, int, int *, int *);
static struct Scsi_Host *ibmmca_register(Scsi_Host_Template *,
					 int, int, char *);

/* local functions needed for proc_info */
static int ldn_access_load(int, int);
static int ldn_access_total_read_write(int);

static int bypass_controller = 0;   /* bypass integrated SCSI-cmd set flag */
/*--------------------------------------------------------------------*/

/******************* LOCAL FUNCTIONS IMPLEMENTATION *************************/

/* newer Kernels need the spinlock interrupt handler */
static void do_interrupt_handler (int irq, void *dev_id, struct pt_regs *regs)
{
  unsigned long flags;

  spin_lock_irqsave(&io_request_lock, flags);
  interrupt_handler(irq, dev_id, regs);
  spin_unlock_irqrestore(&io_request_lock, flags);
  return;
}

static void interrupt_handler (int irq, void *dev_id, struct pt_regs *regs)
{
   int host_index;
   unsigned int intr_reg;
   unsigned int cmd_result;
   unsigned int ldn;
   static unsigned long flags;
   Scsi_Cmnd *cmd;
   int errorflag;
   int interror;

   host_index=0; /* make sure, host_index is 0, else this won't work and
		    never dare to ask, what happens, if an interrupt-handler
		    does not work :-((( .... */

   /* search for one adapter-response on shared interrupt */
   while (hosts[host_index]
	  && !(inb(IM_STAT_REG(host_index)) & IM_INTR_REQUEST))
     host_index++;

   /* return if some other device on this IRQ caused the interrupt */
   if (!hosts[host_index]) return;

   /* the reset-function already did all the job, even ints got
      renabled on the subsystem, so just return */
   if ((reset_status(host_index) == IM_RESET_NOT_IN_PROGRESS_NO_INT)||
       (reset_status(host_index) == IM_RESET_FINISHED_OK_NO_INT))
     {
	reset_status(host_index) = IM_RESET_NOT_IN_PROGRESS;
	return;
     }

   /*get command result and logical device */
   intr_reg = inb (IM_INTR_REG(host_index));
   cmd_result = intr_reg & 0xf0;
   ldn = intr_reg & 0x0f;

   /*must wait for attention reg not busy, then send EOI to subsystem */
   while (1)
     {
	spin_lock_irqsave(&intr_lock, flags);
	if (!(inb (IM_STAT_REG(host_index)) & IM_BUSY))
	  break;
	spin_unlock_irqrestore(&intr_lock, flags);
     }
   outb (IM_EOI | ldn, IM_ATTN_REG(host_index));
   /* get the last_scsi_command here */
   interror = last_scsi_command(host_index)[ldn];
   spin_unlock_irqrestore(&intr_lock, flags);
   errorflag = 0; /* no errors by default */
   /*these should never happen (hw fails, or a local programming bug) */
   if (cmd_result == IM_ADAPTER_HW_FAILURE)
     {
	printk("\n");
	printk("IBM MCA SCSI: ERROR - subsystem hardware failure!\n");
	printk("              Last SCSI-command=0x%X, ldn=%d, host=%d.\n",
	       last_scsi_command(host_index)[ldn],ldn,host_index);
	errorflag = 1;
     }
   if (cmd_result == IM_SOFTWARE_SEQUENCING_ERROR)
     {
	printk("\n");
	printk("IBM MCA SCSI: ERROR - software sequencing error!\n");
	printk("              Last SCSI-command=0x%X, ldn=%d, host=%d.\n",
	       last_scsi_command(host_index)[ldn],ldn,host_index);
	errorflag = 1;
     }
   if (cmd_result == IM_CMD_ERROR)
     {
	printk("\n");
	printk("IBM MCA SCSI: ERROR - command error!\n");
	printk("              Last SCSI-command=0x%X, ldn=%d, host=%d.\n",
	       last_scsi_command(host_index)[ldn],ldn,host_index);
	errorflag = 1;
     }
   if (errorflag)
     { /* if errors appear, enter this section to give detailed info */
	printk("IBM MCA SCSI: Subsystem Error-Status follows:\n");
	printk("              Command Type................: %x\n",
	       last_scsi_type(host_index)[ldn]);
	printk("              Attention Register..........: %x\n",
	       inb (IM_ATTN_REG(host_index)));
        printk("              Basic Control Register......: %x\n",
	       inb (IM_CTR_REG(host_index)));
	printk("              Interrupt Status Register...: %x\n",
	       intr_reg);
	printk("              Basic Status Register.......: %x\n",
	       inb (IM_STAT_REG(host_index)));
	if ((last_scsi_type(host_index)[ldn]==IM_SCB)||
	    (last_scsi_type(host_index)[ldn]==IM_LONG_SCB))
	  {
	     printk("              SCB End Status Word.........: %x\n",
		    ld(host_index)[ldn].tsb.end_status);
	     printk("              Command Status..............: %x\n",
		    ld(host_index)[ldn].tsb.cmd_status);
	     printk("              Device Status...............: %x\n",
		    ld(host_index)[ldn].tsb.dev_status);
	     printk("              Command Error...............: %x\n",
		    ld(host_index)[ldn].tsb.cmd_error);
	     printk("              Device Error................: %x\n",
		    ld(host_index)[ldn].tsb.dev_error);
	     printk("              Last SCB Address (LSW)......: %x\n",
		    ld(host_index)[ldn].tsb.low_of_last_scb_adr);
	     printk("              Last SCB Address (MSW)......: %x\n",
		    ld(host_index)[ldn].tsb.high_of_last_scb_adr);
	  }
	printk("              Send report to the maintainer.\n");
	panic("IBM MCA SCSI: Fatal errormessage from the subsystem!\n");
     }

   /* if no panic appeared, increase the interrupt-counter */
   IBM_DS(host_index).total_interrupts++;

   /*only for local checking phase */
   if (local_checking_phase_flag(host_index))
     {
	stat_result(host_index) = cmd_result;
	got_interrupt(host_index) = 1;
	reset_status(host_index) = IM_RESET_FINISHED_OK;
	last_scsi_command(host_index)[ldn] = NO_SCSI;
	return;
     }
   /*handling of commands coming from upper level of scsi driver */
   else
     {
	if (last_scsi_type(host_index)[ldn] == IM_IMM_CMD)
	  {
	     /*verify ldn, and may handle rare reset immediate command */
	     if ((reset_status(host_index) == IM_RESET_IN_PROGRESS)&&
		 (last_scsi_command(host_index)[ldn] == IM_RESET_IMM_CMD))
	       {
		  if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE)
		    {
		       disk_rw_in_progress = 0;
		       PS2_DISK_LED_OFF ();
		       reset_status(host_index) = IM_RESET_FINISHED_FAIL;
		    }
		  else
		    {
		       /*reset disk led counter, turn off disk led */
		       disk_rw_in_progress = 0;
		       PS2_DISK_LED_OFF ();
		       reset_status(host_index) = IM_RESET_FINISHED_OK;
		    }
		  stat_result(host_index) = cmd_result;
		  last_scsi_command(host_index)[ldn] = NO_SCSI;
		  return;
	       }
	     else if (last_scsi_command(host_index)[ldn] == IM_ABORT_IMM_CMD)
	       { /* react on SCSI abort command */
#ifdef IM_DEBUG_PROBE
		  printk("IBM MCA SCSI: Interrupt from SCSI-abort.\n");
#endif
		  disk_rw_in_progress = 0;
		  PS2_DISK_LED_OFF();
		  cmd = ld(host_index)[ldn].cmd;
		  if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE)
		    cmd->result = DID_NO_CONNECT << 16;
		  else
		    cmd->result = DID_ABORT << 16;
		  stat_result(host_index) = cmd_result;
		  last_scsi_command(host_index)[ldn] = NO_SCSI;
		  if (cmd->scsi_done)
		    (cmd->scsi_done) (cmd); /* should be the internal_done */
		  return;
	       }
	     else
	       {
		  disk_rw_in_progress = 0;
		  PS2_DISK_LED_OFF ();
		  reset_status(host_index) = IM_RESET_FINISHED_OK;
		  stat_result(host_index) = cmd_result;
		  last_scsi_command(host_index)[ldn] = NO_SCSI;
		  return;
	       }
	  }
	last_scsi_command(host_index)[ldn] = NO_SCSI;
	cmd = ld(host_index)[ldn].cmd;
#ifdef IM_DEBUG_TIMEOUT
	if (cmd)
	  {
	     if ((cmd->target == TIMEOUT_PUN)&&(cmd->lun == TIMEOUT_LUN))
	       {
		  printk("IBM MCA SCSI: Ignoring interrupt from pun=%x, lun=%x.\n",
			 cmd->target, cmd->lun);
		  return;
	       }
	  }
#endif
	/*if no command structure, just return, else clear cmd */
	if (!cmd)
	  return;
	ld(host_index)[ldn].cmd = NULL;

#ifdef IM_DEBUG_INT
	printk("cmd=%02x ireg=%02x ds=%02x cs=%02x de=%02x ce=%02x\n",
	       cmd->cmnd[0], intr_reg,
	       ld(host_index)[ldn].tsb.dev_status,
	       ld(host_index)[ldn].tsb.cmd_status,
	       ld(host_index)[ldn].tsb.dev_error,
	       ld(host_index)[ldn].tsb.cmd_error);
#endif

	/*if this is end of media read/write, may turn off PS/2 disk led */
	if ((ld(host_index)[ldn].device_type!=TYPE_NO_LUN)&&
	    (ld(host_index)[ldn].device_type!=TYPE_NO_DEVICE))
	  { /* only access this, if there was a valid device addressed */
	     switch (cmd->cmnd[0])
	       {
		case READ_6:
		case WRITE_6:
		case READ_10:
		case WRITE_10:
		case READ_12:
		case WRITE_12:
		  if (--disk_rw_in_progress == 0)
		    PS2_DISK_LED_OFF ();
	       }
	  }

	/* IBM describes the status-mask to be 0x1e, but this is not conform
	 * with SCSI-defintion, I suppose, it is a printing error in the
	 * technical reference and assume as mask 0x3e. (ML) */
	cmd->result = (ld(host_index)[ldn].tsb.dev_status & 0x3e);
	/* write device status into cmd->result, and call done function */
	if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE)
	  IBM_DS(host_index).total_errors++;
	if (interror == NO_SCSI) /* unexpected interrupt :-( */
	  cmd->result |= DID_BAD_INTR << 16;
	else
	  cmd->result |= DID_OK << 16;
	(cmd->scsi_done) (cmd);
     }
   if (interror == NO_SCSI)
     printk("IBM MCA SCSI: WARNING - Interrupt from non-pending SCSI-command!\n");
   return;
}

/*--------------------------------------------------------------------*/

static void issue_cmd (int host_index, unsigned long cmd_reg,
		       unsigned char attn_reg)
{
   static unsigned long flags;
   /* must wait for attention reg not busy */
   while (1)
     {
	spin_lock_irqsave(&issue_lock, flags);
	if (!(inb (IM_STAT_REG(host_index)) & IM_BUSY))
	  break;
	spin_unlock_irqrestore(&issue_lock, flags);
     }
   /*write registers and enable system interrupts */
   outl (cmd_reg, IM_CMD_REG(host_index));
   outb (attn_reg, IM_ATTN_REG(host_index));
   spin_unlock_irqrestore(&issue_lock, flags);
}

/*--------------------------------------------------------------------*/

static void internal_done (Scsi_Cmnd * cmd)
{
   cmd->SCp.Status++;
}

/*--------------------------------------------------------------------*/

/* SCSI-SCB-command for device_inquiry */
static int device_inquiry(int host_index, int ldn)
{
   int retries;
   Scsi_Cmnd cmd;
   struct im_scb *scb;
   struct im_tsb *tsb;
   unsigned char *buf;

   scb = &(ld(host_index)[ldn].scb);
   tsb = &(ld(host_index)[ldn].tsb);
   buf = (unsigned char *)(&(ld(host_index)[ldn].buf));
   ld(host_index)[ldn].tsb.dev_status = 0; /* prepare stusblock */

   if (bypass_controller)
     { /* fill the commonly known field for device-inquiry SCSI cmnd */
	cmd.cmd_len = 6;
        memset (&(cmd.cmnd), 0x0, sizeof(char) * cmd.cmd_len);
	cmd.cmnd[0] = INQUIRY; /* device inquiry */
	cmd.cmnd[4] = 0xff; /* return buffer size = 255 */
     }
   for (retries = 0; retries < 3; retries++)
     {
	if (bypass_controller)
	  { /* bypass the hardware integrated command set */
	     scb->command = IM_OTHER_SCSI_CMD_CMD;
	     scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
	     scb->u1.scsi_cmd_length = cmd.cmd_len;
	     memcpy (scb->u2.scsi_command, &(cmd.cmnd), cmd.cmd_len);
	     last_scsi_command(host_index)[ldn] = INQUIRY;
	     last_scsi_type(host_index)[ldn] = IM_SCB;
	  }
	else
	  {
	     /*fill scb with inquiry command */
	     scb->command = IM_DEVICE_INQUIRY_CMD;
	     scb->enable = IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
	     last_scsi_command(host_index)[ldn] = IM_DEVICE_INQUIRY_CMD;
	     last_scsi_type(host_index)[ldn] = IM_SCB;
	  }
	scb->sys_buf_adr = virt_to_bus(buf);
	scb->sys_buf_length = 0xff; /* maximum bufferlength gives max info */
	scb->tsb_adr = virt_to_bus(tsb);

	/*issue scb to passed ldn, and busy wait for interrupt */
	got_interrupt(host_index) = 0;
	issue_cmd (host_index, virt_to_bus(scb), IM_SCB | ldn);
	while (!got_interrupt(host_index))
	  barrier ();

	/*if command succesful, break */
	if ((stat_result(host_index) == IM_SCB_CMD_COMPLETED)||
	    (stat_result(host_index) == IM_SCB_CMD_COMPLETED_WITH_RETRIES))
	  {
	     return 1;
	  }
     }

   /*if all three retries failed, return "no device at this ldn" */
   if (retries >= 3)
     return 0;
   else
     return 1;
}

static int read_capacity(int host_index, int ldn)
{
   int retries;
   Scsi_Cmnd cmd;
   struct im_scb *scb;
   struct im_tsb *tsb;
   unsigned char *buf;

   scb = &(ld(host_index)[ldn].scb);
   tsb = &(ld(host_index)[ldn].tsb);
   buf = (unsigned char *)(&(ld(host_index)[ldn].buf));
   ld(host_index)[ldn].tsb.dev_status = 0;

   if (bypass_controller)
     { /* read capacity in commonly known default SCSI-format */
	cmd.cmd_len = 10;
        memset (&(cmd.cmnd), 0x0, sizeof(char) * cmd.cmd_len);
	cmd.cmnd[0] = READ_CAPACITY; /* read capacity */
     }
   for (retries = 0; retries < 3; retries++)
     {
	/*fill scb with read capacity command */
	if (bypass_controller)
	  { /* bypass the SCSI-command */
	     scb->command = IM_OTHER_SCSI_CMD_CMD;
	     scb->enable |= IM_READ_CONTROL;
	     scb->u1.scsi_cmd_length = cmd.cmd_len;
	     memcpy (scb->u2.scsi_command, &(cmd.cmnd), cmd.cmd_len);
	     last_scsi_command(host_index)[ldn] = READ_CAPACITY;
	     last_scsi_type(host_index)[ldn] = IM_SCB;
	  }
	else
	  {
	     scb->command = IM_READ_CAPACITY_CMD;
	     scb->enable = IM_READ_CONTROL;
	     last_scsi_command(host_index)[ldn] = IM_READ_CAPACITY_CMD;
	     last_scsi_type(host_index)[ldn] = IM_SCB;
	  }
	scb->sys_buf_adr = virt_to_bus(buf);
	scb->sys_buf_length = 8;
	scb->tsb_adr = virt_to_bus(tsb);

	/*issue scb to passed ldn, and busy wait for interrupt */
	got_interrupt(host_index) = 0;
	issue_cmd (host_index, virt_to_bus(scb), IM_SCB | ldn);
	while (!got_interrupt(host_index))
	  barrier ();

	     /*if got capacity, get block length and return one device found */
	if ((stat_result(host_index) == IM_SCB_CMD_COMPLETED)||
	    (stat_result(host_index) == IM_SCB_CMD_COMPLETED_WITH_RETRIES))
	  {
	     return 1;
	  }
     }
   /*if all three retries failed, return "no device at this ldn" */
   if (retries >= 3)
     return 0;
   else
     return 1;
}

/* SCSI-immediate-command for assign. This functions maps/unmaps specific
 ldn-numbers on SCSI (PUN,LUN). It is needed for presetting of the
 subsystem and for dynamical remapping od ldns. */
static int immediate_assign(int host_index, unsigned int pun,
                            unsigned int lun, unsigned int ldn,
                            unsigned int operation)
{
   int retries;
   unsigned long imm_command;

   for (retries=0; retries<3; retries ++)
     {
        imm_command = inl(IM_CMD_REG(host_index));
        imm_command &= (unsigned long)(0xF8000000); /* keep reserved bits */
        imm_command |= (unsigned long)(IM_ASSIGN_IMM_CMD);
        imm_command |= (unsigned long)((lun & 7) << 24);
        imm_command |= (unsigned long)((operation & 1) << 23);
        imm_command |= (unsigned long)((pun & 7) << 20);
        imm_command |= (unsigned long)((ldn & 15) << 16);

	last_scsi_command(host_index)[0xf] = IM_ASSIGN_IMM_CMD;
	last_scsi_type(host_index)[0xf] = IM_IMM_CMD;
        got_interrupt(host_index) = 0;
        issue_cmd (host_index, (unsigned long)(imm_command), IM_IMM_CMD | 0xf);
        while (!got_interrupt(host_index))
	  barrier ();

        /*if command succesful, break */
	if (stat_result(host_index) == IM_IMMEDIATE_CMD_COMPLETED)
	  {
	     return 1;
	  }
     }

   if (retries >= 3)
     return 0;
   else
     return 1;
}

#ifdef CONFIG_IBMMCA_SCSI_DEV_RESET
static int immediate_reset(int host_index, unsigned int ldn)
{
   int retries;
   int ticks;
   unsigned long imm_command;

   for (retries=0; retries<3; retries ++)
     {
        imm_command = inl(IM_CMD_REG(host_index));
        imm_command &= (unsigned long)(0xFFFF0000); /* keep reserved bits */
        imm_command |= (unsigned long)(IM_RESET_IMM_CMD);
	last_scsi_command(host_index)[ldn] = IM_RESET_IMM_CMD;
	last_scsi_type(host_index)[ldn] = IM_IMM_CMD;

	got_interrupt(host_index) = 0;
	reset_status(host_index) = IM_RESET_IN_PROGRESS;
	issue_cmd (host_index, (unsigned long)(imm_command), IM_IMM_CMD | ldn);
	ticks = IM_RESET_DELAY*HZ;
	while (reset_status(host_index) == IM_RESET_IN_PROGRESS && --ticks)
	  {
	     mdelay(1+999/HZ);
	     barrier();
	  }
	/* if reset did not complete, just claim */
	if (!ticks)
	  {
	     printk("IBM MCA SCSI: reset did not complete within %d seconds.\n",
		    IM_RESET_DELAY);
	     reset_status(host_index) = IM_RESET_FINISHED_OK;
	     /* did not work, finish */
	     return 1;
	  }
        /*if command succesful, break */
	if (stat_result(host_index) == IM_IMMEDIATE_CMD_COMPLETED)
	  {
	     return 1;
	  }
     }

   if (retries >= 3)
     return 0;
   else
     return 1;
}
#endif

/* type-interpreter for physical device numbers */
static char *ti_p(int value)
{
   switch (value)
     {
      case TYPE_IBM_SCSI_ADAPTER: return("A"); break;
      case TYPE_DISK:             return("D"); break;
      case TYPE_TAPE:             return("T"); break;
      case TYPE_PROCESSOR:        return("P"); break;
      case TYPE_WORM:             return("W"); break;
      case TYPE_ROM:              return("R"); break;
      case TYPE_SCANNER:          return("S"); break;
      case TYPE_MOD:              return("M"); break;
      case TYPE_MEDIUM_CHANGER:   return("C"); break;
      case TYPE_NO_LUN:           return("+"); break; /* show NO_LUN */
      case TYPE_NO_DEVICE:
      default:                    return("-"); break;
     }
   return("-");
}

/* interpreter for logical device numbers (ldn) */
static char *ti_l(int value)
{
   const char hex[16] = "0123456789abcdef";
   static char answer[2];

   answer[1] = (char)(0x0);
   if (value<=MAX_LOG_DEV)
     answer[0] = hex[value];
   else
     answer[0] = '-';

   return (char *)&answer;
}

/*
 The following routine probes the SCSI-devices in four steps:
 1. The current ldn -> pun,lun mapping is removed on the SCSI-adapter.
 2. ldn 0 is used to go through all possible combinations of pun,lun and
 a device_inquiry is done to fiddle out whether there is a device
 responding or not. This physical map is stored in get_scsi[][].
 3. The 15 available ldns (0-14) are mapped to existing pun,lun.
 If there are more devices than ldns, it stops at 14 for the boot
 time. Dynamical remapping will be done in ibmmca_queuecommand.
 4. If there are less than 15 valid pun,lun, the remaining ldns are
 mapped to NON-existing pun,lun to satisfy the adapter. Information
 about pun,lun -> ldn is stored as before in get_ldn[][].
 This method leads to the result, that the SCSI-pun,lun shown to Linux
 mid-level- and higher-level-drivers is exactly corresponding to the
 physical reality on the SCSI-bus. Therefore, it is possible that users
 of older releases of this driver have to rewrite their fstab-file, because
 the /dev/sdXXX could have changed due to the right pun,lun report, now.
 The assignment of ALL ldns avoids dynamical remapping by the adapter
 itself.
 */
static void check_devices (int host_index)
{
   int id, lun, ldn, ticks;
   int count_devices; /* local counter for connected device */

   /* assign default values to certain variables */

   ticks = 0;
   count_devices = 0;
   IBM_DS(host_index).dyn_flag = 0; /* normally no need for dynamical ldn management */
   IBM_DS(host_index).total_errors = 0; /* set errorcounter to 0 */
   next_ldn(host_index) = 7; /* next ldn to be assigned is 7, because 0-6 is 'hardwired'*/
   for (ldn=0; ldn<=MAX_LOG_DEV; ldn++)
     {
	last_scsi_command(host_index)[ldn] = NO_SCSI; /* emptify last SCSI-command storage */
	last_scsi_type(host_index)[ldn] = 0;
     }

   /* initialize the very important driver-informational arrays/structs */
   memset (ld(host_index), 0,
	   sizeof(ld(host_index)));
   memset (get_ldn(host_index), TYPE_NO_DEVICE,
	   sizeof(get_ldn(host_index))); /* this is essential ! */
   memset (get_scsi(host_index), TYPE_NO_DEVICE,
	   sizeof(get_scsi(host_index))); /* this is essential ! */

   for (lun=0; lun<8; lun++) /* mark the adapter at its pun on all luns*/
     {
	get_scsi(host_index)[subsystem_pun(host_index)][lun] = TYPE_IBM_SCSI_ADAPTER;
	get_ldn(host_index)[subsystem_pun(host_index)][lun] = MAX_LOG_DEV; /* make sure, the subsystem
								ldn is active for all
								luns. */
     }

   /* STEP 1: */
#ifdef IM_DEBUG_PROBE
   printk("IBM MCA SCSI: Current SCSI-host index: %d\n",host_index);
#endif
   printk("IBM MCA SCSI: Removing default logical SCSI-device mapping.");
   for (ldn=0; ldn<MAX_LOG_DEV; ldn++)
     {
#ifdef IM_DEBUG_PROBE
	printk(".");
#endif
	immediate_assign(host_index,0,0,ldn,REMOVE_LDN); /* remove ldn (wherever)*/
     }

   lun = 0; /* default lun is 0 */

   /* STEP 2: */
   printk("\nIBM MCA SCSI: Probing SCSI-devices.");
   for (id=0; id<8; id++)
#ifdef CONFIG_SCSI_MULTI_LUN
     for (lun=0; lun<8; lun++)
#endif
     {
#ifdef IM_DEBUG_PROBE
	printk(".");
#endif
	if (id != subsystem_pun(host_index))
	  {            /* if pun is not the adapter: */
	     /*set ldn=0 to pun,lun*/
	     immediate_assign(host_index,id,lun,PROBE_LDN,SET_LDN);
	     if (device_inquiry(host_index, PROBE_LDN)) /* probe device */
	       {
		  get_scsi(host_index)[id][lun]=
		    (unsigned char)(ld(host_index)[PROBE_LDN].buf[0]);
		  /* entry, even for NO_LUN */
		  if (ld(host_index)[PROBE_LDN].buf[0] != TYPE_NO_LUN)
		    count_devices++; /* a existing device is found */
	       }
	     /* remove ldn */
	     immediate_assign(host_index,id,lun,PROBE_LDN,REMOVE_LDN);
	  }
     }

   /* STEP 3: */
   printk("\nIBM MCA SCSI: Mapping SCSI-devices.");

   ldn = 0;
   lun = 0;

#ifdef CONFIG_SCSI_MULTI_LUN
   for (lun=0; lun<8 && ldn<MAX_LOG_DEV; lun++)
#endif
     for (id=0; id<8 && ldn<MAX_LOG_DEV; id++)
     {
#ifdef IM_DEBUG_PROBE
	printk(".");
#endif
	if (id != subsystem_pun(host_index))
	  {
	     if (get_scsi(host_index)[id][lun] != TYPE_NO_LUN &&
		 get_scsi(host_index)[id][lun] != TYPE_NO_DEVICE)
	       {
		  /* Only map if accepted type. Always enter for
		   lun == 0 to get no gaps into ldn-mapping for ldn<7. */
		  immediate_assign(host_index,id,lun,ldn,SET_LDN);
		  get_ldn(host_index)[id][lun]=ldn; /* map ldn */
		  if (device_exists (host_index, ldn,
				     &ld(host_index)[ldn].block_length,
				     &ld(host_index)[ldn].device_type))
		    {
#ifdef CONFIG_IBMMCA_SCSI_DEV_RESET
		       printk("resetting device at ldn=%x ... ",ldn);
		       immediate_reset(host_index,ldn);
#endif
		       ldn++;
		    }
		  else
		    {
		       /* device vanished, probably because we don't know how to
			* handle it or because it has problems */
		       if (lun > 0)
			 {
			    /* remove mapping */
			    get_ldn(host_index)[id][lun]=TYPE_NO_DEVICE;
			    immediate_assign(host_index,0,0,ldn,REMOVE_LDN);
			 }
		       else ldn++;
		    }
	       }
	     else if (lun == 0)
	       {
		  /* map lun == 0, even if no device exists */
		  immediate_assign(host_index,id,lun,ldn,SET_LDN);
		  get_ldn(host_index)[id][lun]=ldn; /* map ldn */
		  ldn++;
	       }
	  }
     }

   /* STEP 4: */

   /* map remaining ldns to non-existing devices */
   for (lun=1; lun<8 && ldn<MAX_LOG_DEV; lun++)
     for (id=0; id<8 && ldn<MAX_LOG_DEV; id++)
     {
	if (get_scsi(host_index)[id][lun] == TYPE_NO_LUN ||
	    get_scsi(host_index)[id][lun] == TYPE_NO_DEVICE)
	  {
	     /* Map remaining ldns only to NON-existing pun,lun
	      combinations to make sure an inquiry will fail. 
	      For MULTI_LUN, it is needed to avoid adapter autonome
	      SCSI-remapping. */
	     immediate_assign(host_index,id,lun,ldn,SET_LDN);
	     get_ldn(host_index)[id][lun]=ldn;
	     ldn++;
	  }
     }

   printk("\n");
   if (ibm_ansi_order)
     printk("IBM MCA SCSI: Device order: IBM/ANSI (pun=7 is first).\n");
   else
     printk("IBM MCA SCSI: Device order: New Industry Standard (pun=0 is first).\n");

#ifdef IM_DEBUG_PROBE
   /* Show the physical and logical mapping during boot. */
   printk("IBM MCA SCSI: Determined SCSI-device-mapping:\n");
   printk("    Physical SCSI-Device Map               Logical SCSI-Device Map\n");
   printk("ID\\LUN  0  1  2  3  4  5  6  7       ID\\LUN  0  1  2  3  4  5  6  7\n");
   for (id=0; id<8; id++)
     {
        printk("%2d     ",id);
	for (lun=0; lun<8; lun++)
	  printk("%2s ",ti_p(get_scsi(host_index)[id][lun]));
	printk("      %2d     ",id);
	for (lun=0; lun<8; lun++)
	  printk("%2s ",ti_l(get_ldn(host_index)[id][lun]));
	printk("\n");
     }
#endif

   /* assign total number of found SCSI-devices to the statistics struct */
   IBM_DS(host_index).total_scsi_devices = count_devices;

   /* decide for output in /proc-filesystem, if the configuration of
    SCSI-devices makes dynamical reassignment of devices necessary */
   if (count_devices>=MAX_LOG_DEV)
     IBM_DS(host_index).dyn_flag = 1; /* dynamical assignment is necessary */
   else
     IBM_DS(host_index).dyn_flag = 0; /* dynamical assignment is not necessary */

   /* If no SCSI-devices are assigned, return 1 in order to cause message. */
   if (ldn == 0)
     printk("IBM MCA SCSI: Warning: No SCSI-devices found/assigned!\n");

   /* reset the counters for statistics on the current adapter */
   IBM_DS(host_index).total_accesses = 0;
   IBM_DS(host_index).total_interrupts = 0;
   IBM_DS(host_index).dynamical_assignments = 0;
   memset (IBM_DS(host_index).ldn_access, 0x0,
	   sizeof (IBM_DS(host_index).ldn_access));
   memset (IBM_DS(host_index).ldn_read_access, 0x0,
	   sizeof (IBM_DS(host_index).ldn_read_access));
   memset (IBM_DS(host_index).ldn_write_access, 0x0,
	   sizeof (IBM_DS(host_index).ldn_write_access));
   memset (IBM_DS(host_index).ldn_inquiry_access, 0x0,
	   sizeof (IBM_DS(host_index).ldn_inquiry_access));
   memset (IBM_DS(host_index).ldn_modeselect_access, 0x0,
	   sizeof (IBM_DS(host_index).ldn_modeselect_access));
   memset (IBM_DS(host_index).ldn_assignments, 0x0,
	   sizeof (IBM_DS(host_index).ldn_assignments));

   return;
}

/*--------------------------------------------------------------------*/

static int device_exists (int host_index, int ldn, int *block_length,
			  int *device_type)
{
   unsigned char *buf;

   /* if no valid device found, return immediately with 0 */
   if (!(device_inquiry(host_index, ldn)))
     return 0;

   buf = (unsigned char *)(&(ld(host_index)[ldn].buf));

   /*if device is CD_ROM, assume block size 2048 and return */
   if (*buf == TYPE_ROM)
     {
	*device_type = TYPE_ROM;
	*block_length = 2048; /* (standard blocksize for yellow-/red-book) */
	return 1;
     }

   if (*buf == TYPE_WORM) /* CD-burner, WORM, Linux handles this as CD-ROM
			     therefore, the block_length is also 2048. */
     {
	*device_type = TYPE_WORM;
	*block_length = 2048;
	return 1;
     }

   /* if device is disk, use "read capacity" to find its block size */
   if (*buf == TYPE_DISK)
     {
	*device_type = TYPE_DISK;
        if (read_capacity( host_index, ldn))
	  {
	     *block_length = *(buf+7) + (*(buf+6) << 8) +
	                    (*(buf+5) << 16) + (*(buf+4) << 24);
	     return 1;
	  }
	else
	  return 0;
     }

   /* if this is a magneto-optical drive, treat it like a harddisk */
   if (*buf == TYPE_MOD)
     {
	*device_type = TYPE_MOD;
        if (read_capacity( host_index, ldn))
	  {
	     *block_length = *(buf+7) + (*(buf+6) << 8) +
	                    (*(buf+5) << 16) + (*(buf+4) << 24);
	     return 1;
	  }
	else
	  return 0;
     }

   if (*buf == TYPE_TAPE) /* TAPE-device found */
     {
	*device_type = TYPE_TAPE;
	*block_length = 0; /* not in use (setting by mt and mtst in op.) */
	return 1;
     }

   if (*buf == TYPE_PROCESSOR) /* HP-Scanners, diverse SCSI-processing units*/
     {
	*device_type = TYPE_PROCESSOR;
	*block_length = 0; /* they set their stuff on drivers */
	return 1;
     }

   if (*buf == TYPE_SCANNER) /* other SCSI-scanners */
     {
	*device_type = TYPE_SCANNER;
	*block_length = 0; /* they set their stuff on drivers */
	return 1;
     }

   if (*buf == TYPE_MEDIUM_CHANGER) /* Medium-Changer */
     {
	*device_type = TYPE_MEDIUM_CHANGER;
	*block_length = 0; /* One never knows, what to expect on a medium
			    changer device. */
	return 1;
     }

   /* Up to now, no SCSI-devices that are known up to kernel 2.1.31 are
      ignored! MO-drives are now supported and treated as harddisk. */
   return 0;
}

/*--------------------------------------------------------------------*/

#ifdef CONFIG_SCSI_IBMMCA

void ibmmca_scsi_setup (char *str, int *ints)
{
   int i, j, io_base, id_base;
   char *token;

   io_base = 0;
   id_base = 0;

   if (str)
     {
	token = strtok(str,",");
	j = 0;
	while (token)
	  {
	     if (!strcmp(token,"display"))
	       {
		  use_display = 1;
	       }
	     if (!strcmp(token,"adisplay"))
	       {
		  use_adisplay = 1;
	       }
	     if (!strcmp(token,"bypass"))
	       {
		  bypass_controller = 1;
	       }
	     if (!strcmp(token,"normal"))
	       {
		  ibm_ansi_order = 0;
	       }
	     if (!strcmp(token,"ansi"))
	       {
		  ibm_ansi_order = 1;
	       }
	     if ( (*token == '-') || (isdigit(*token)) )
	       {
		  if (!(j%2) && (io_base < IM_MAX_HOSTS))
		    {
		       io_port[io_base++] = simple_strtoul(token,NULL,0);
		    }
		  if ((j%2) && (id_base < IM_MAX_HOSTS))
		    {
		       scsi_id[id_base++] = simple_strtoul(token,NULL,0);
		    }
		  j++;
	       }
	     token = strtok(NULL,",");
	  }
     }
   else if (ints)
     {
	for (i = 0; i < IM_MAX_HOSTS && 2*i+2 < ints[0]; i++)
	  {
	     io_port[i] = ints[2*i+2];
	     scsi_id[i] = ints[2*i+2];
	  }
     }
   return;
}

#endif

/*--------------------------------------------------------------------*/

static int ibmmca_getinfo (char *buf, int slot, void *dev)
{
   struct Scsi_Host *shpnt;
   int len, special;
   unsigned int pos2, pos3;
   static unsigned long flags;

   spin_lock_irqsave(&info_lock, flags);

   shpnt = dev; /* assign host-structure to local pointer */
   len = 0; /* set filled text-buffer index to 0 */
   /* get the _special contents of the hostdata structure */
   special = ((struct ibmmca_hostdata *)shpnt->hostdata)->_special;
   pos2 = ((struct ibmmca_hostdata *)shpnt->hostdata)->_pos2;
   pos3 = ((struct ibmmca_hostdata *)shpnt->hostdata)->_pos3;

   if (special == FORCED_DETECTION) /* forced detection */
     {
	len += sprintf (buf + len, "Adapter cathegory: forced detected\n");
	len += sprintf(buf + len, "***************************************\n");
	len += sprintf(buf + len, "***  Forced detected SCSI Adapter   ***\n");
	len += sprintf(buf + len, "***  No chip-information available  ***\n");
	len += sprintf(buf + len, "***************************************\n");
     }
   else if (special == INTEGRATED_SCSI)
     { /* if the integrated subsystem has been found automatically: */
	len += sprintf (buf + len, "Adapter cathegory: integrated\n");
	len += sprintf (buf + len, "Chip revision level: %d\n",
			((pos2 & 0xf0) >> 4));
	len += sprintf (buf + len, "Chip status: %s\n",
			(pos2 & 1) ? "enabled" : "disabled");
	len += sprintf (buf + len, "8 kByte NVRAM status: %s\n",
			(pos2 & 2) ? "locked" : "accessible");
     }
   else if ((special>=0)&&
	   (special<(sizeof(subsys_list)/sizeof(struct subsys_list_struct))))
     { /* if the subsystem is a slot adapter */
	len += sprintf (buf + len, "Adapter cathegory: slot-card\n");
	len += sprintf (buf + len, "Chip revision level: %d\n",
			((pos2 & 0xf0) >> 4));
	len += sprintf (buf + len, "Chip status: %s\n",
			(pos2 & 1) ? "enabled" : "disabled");
	len += sprintf (buf + len, "Port offset: 0x%x\n",
			((pos2 & 0x0e) << 2));
     }
   else
     {
	len += sprintf (buf + len, "Adapter cathegory: unknown\n");
     }
   /* common subsystem information to write to the slotn file */
   len += sprintf (buf + len, "Subsystem PUN: %d\n", shpnt->this_id);
   len += sprintf (buf + len, "I/O base address range: 0x%x-0x%x",
		   (unsigned int)(shpnt->io_port), 
		   (unsigned int)(shpnt->io_port+7));
   /* Now make sure, the bufferlength is devideable by 4 to avoid
    * paging problems of the buffer. */
   while ( len % sizeof( int ) != ( sizeof ( int ) - 1 ) )
     {
	len += sprintf (buf + len, " ");
     }
   len += sprintf (buf + len, "\n");

   spin_unlock_irqrestore(&info_lock, flags);
   return len;
}

int ibmmca_detect (Scsi_Host_Template * scsi_template)
{
   struct Scsi_Host *shpnt;
   int port, id, i, j, list_size, slot;

   found = 0; /* make absolutely sure, that found is set to 0 */

   /* if this is not MCA machine, return "nothing found" */
   if (!MCA_bus)
     {
	printk("IBM MCA SCSI: No Microchannel-bus support present -> Aborting.\n");
	return 0;
     }
   else
     printk("IBM MCA SCSI: Version %s\n",IBMMCA_SCSI_DRIVER_VERSION);

   /* get interrupt request level */
   if (request_irq (IM_IRQ, do_interrupt_handler, SA_SHIRQ, "ibmmcascsi",
		    hosts))
     {
	printk("IBM MCA SCSI: Unable to get shared IRQ %d.\n", IM_IRQ);
	return 0;
     }

   /* if ibmmcascsi setup option was passed to kernel, return "found" */
   for (i = 0; i < IM_MAX_HOSTS; i++)
     if (io_port[i] > 0 && scsi_id[i] >= 0 && scsi_id[i] < 8)
     {
	printk("IBM MCA SCSI: forced detected SCSI Adapter, io=0x%x, scsi id=%d.\n",
	       io_port[i], scsi_id[i]);
	if ((shpnt = ibmmca_register(scsi_template, io_port[i], scsi_id[i],
		     "forced detected SCSI Adapter")))
	  {
	     ((struct ibmmca_hostdata *)shpnt->hostdata)->_pos2 = 0;
	     ((struct ibmmca_hostdata *)shpnt->hostdata)->_pos3 = 0;
	     ((struct ibmmca_hostdata *)shpnt->hostdata)->_special =
	       FORCED_DETECTION;
	     mca_set_adapter_name(MCA_INTEGSCSI, "forced detected SCSI Adapter");
	     mca_set_adapter_procfn(MCA_INTEGSCSI, (MCA_ProcFn) ibmmca_getinfo,
				    shpnt);
	     mca_mark_as_used(MCA_INTEGSCSI);
	  }
     }
   if (found) return found;

   /* The POS2-register of all PS/2 model SCSI-subsystems has the following
    * interpretation of bits:
    *                             Bit 7 - 4 : Chip Revision ID (Release)
    *                             Bit 3 - 2 : Reserved
    *                             Bit 1     : 8k NVRAM Disabled
    *                             Bit 0     : Chip Enable (EN-Signal)
    * The POS3-register is interpreted as follows:
    *                             Bit 7 - 5 : SCSI ID
    *                             Bit 4     : Reserved = 0
    *                             Bit 3 - 0 : Reserved = 0
    * (taken from "IBM, PS/2 Hardware Interface Technical Reference, Common
    * Interfaces (1991)").
    * In short words, this means, that IBM PS/2 machines only support
    * 1 single subsystem by default. The slot-adapters must have another
    * configuration on pos2. Here, one has to assume the following
    * things for POS2-register:
    *                             Bit 7 - 4 : Chip Revision ID (Release)
    *                             Bit 3 - 1 : port offset factor
    *                             Bit 0     : Chip Enable (EN-Signal)
    * As I found a patch here, setting the IO-registers to 0x3540 forced,
    * as there was a 0x05 in POS2 on a model 56, I assume, that the
    * port 0x3540 must be fix for integrated SCSI-controllers.
    * Ok, this discovery leads to the following implementation: (M.Lang) */

   /* first look for the IBM SCSI integrated subsystem on the motherboard */
   for (j=0;j<8;j++) /* read the pos-information */
     pos[j] = mca_read_stored_pos(MCA_INTEGSCSI,j);
   /* pos2 = pos3 = 0xff if there is no integrated SCSI-subsystem present  */
   if (( pos[2] != 0xff) || (pos[3] != 0xff ))
     {
	if ((pos[2] & 1) == 1) /* is the subsystem chip enabled ? */
	  {
	     port = IM_IO_PORT;
	  }
	else
	  { /* if disabled, no IRQs will be generated, as the chip won't
	     * listen to the incomming commands and will do really nothing,
	     * except for listening to the pos-register settings. If this
	     * happens, I need to hugely think about it, as one has to
	     * write something to the MCA-Bus pos register in order to
	     * enable the chip. Normally, IBM-SCSI won't pass the POST,
	     * when the chip is disabled (see IBM tech. ref.). */
	     port = IM_IO_PORT; /* anyway, set the portnumber and warn */
	     printk("IBM MCA SCSI: WARNING - Your SCSI-subsystem is disabled!\n");
	     printk("              SCSI-operations may not work.\n");
	  }
	id = (pos[3] & 0xe0) >> 5; /* this is correct and represents the PUN */

	/* give detailed information on the subsystem. This helps me
	 * additionally during debugging and analyzing bug-reports. */
	printk("IBM MCA SCSI: IBM Integrated SCSI Controller found, io=0x%x, scsi id=%d,\n",
	       port, id);
	printk("              chip rev.=%d, 8K NVRAM=%s, subsystem=%s\n",
	       ((pos[2] & 0xf0) >> 4), (pos[2] & 2) ? "locked" : "accessible",
	       (pos[2] & 1) ? "enabled." : "disabled.");

	/* register the found integrated SCSI-subsystem */
	if ((shpnt = ibmmca_register(scsi_template, port, id,
		     "IBM Integrated SCSI Controller")))
	  {
	     ((struct ibmmca_hostdata *)shpnt->hostdata)->_pos2 = pos[2];
	     ((struct ibmmca_hostdata *)shpnt->hostdata)->_pos3 = pos[3];
	     ((struct ibmmca_hostdata *)shpnt->hostdata)->_special =
	       INTEGRATED_SCSI;
	     mca_set_adapter_name(MCA_INTEGSCSI, "IBM Integrated SCSI Controller");
	     mca_set_adapter_procfn(MCA_INTEGSCSI, (MCA_ProcFn) ibmmca_getinfo,
				    shpnt);
	     mca_mark_as_used(MCA_INTEGSCSI);
	  }
     }

   /* now look for other adapters in MCA slots, */
   /* determine the number of known IBM-SCSI-subsystem types */
   /* see the pos[2] dependence to get the adapter port-offset. */
   list_size = sizeof(subsys_list) / sizeof(struct subsys_list_struct);
   for (i = 0; i < list_size; i++)
     { /* scan each slot for a fitting adapter id */
	slot = 0; /* start at slot 0 */
	while ((slot = mca_find_adapter(subsys_list[i].mca_id, slot))
	       != MCA_NOTFOUND)
	  { /* scan through all slots */
	     for (j=0;j<8;j++) /* read the pos-information */
	       pos[j] = mca_read_stored_pos(slot, j);
	     if ((pos[2] & 1) == 1) /* is the subsystem chip enabled ? */
	       { /* (explanations see above) */
		  port = IM_IO_PORT + ((pos[2] & 0x0e) << 2);
	       }
	     else
	       { /* anyway, set the portnumber and warn */
		  port = IM_IO_PORT + ((pos[2] & 0x0e) << 2);
		  printk("IBM MCA SCSI: WARNING - Your SCSI-subsystem is disabled!\n");
		  printk("              SCSI-operations may not work.\n");
	       }
	     id = (pos[3] & 0xe0) >> 5; /* get subsystem PUN */
	     printk("IBM MCA SCSI: %s found in slot %d, io=0x%x, scsi id=%d,\n",
		     subsys_list[i].description, slot + 1, port, id);
	     printk("              chip rev.=%d, port-offset=0x%x, subsystem=%s\n",
		    ((pos[2] & 0xf0) >> 4),
		    ((pos[2] & 0x0e) << 2),
		    (pos[2] & 1) ? "enabled." : "disabled.");

	     /* register the hostadapter */
	     if ((shpnt = ibmmca_register(scsi_template, port, id,
		          subsys_list[i].description)))
	       {
		  ((struct ibmmca_hostdata *)shpnt->hostdata)->_pos2 = pos[2];
		  ((struct ibmmca_hostdata *)shpnt->hostdata)->_pos3 = pos[3];
		  ((struct ibmmca_hostdata *)shpnt->hostdata)->_special = i;

		  mca_set_adapter_name (slot, subsys_list[i].description);
		  mca_set_adapter_procfn (slot, (MCA_ProcFn) ibmmca_getinfo,
					  shpnt);
		  mca_mark_as_used(slot);
	       }
	     slot++; /* advance to next slot */
	  } /* advance to next adapter id in the list of IBM-SCSI-subsystems*/
     }

   if (!found)
     { /* maybe ESDI, or other producers' SCSI-hosts */
	free_irq (IM_IRQ, hosts);
	printk("IBM MCA SCSI: No IBM SCSI-subsystem adapter attached.\n");
     }
   return found; /* return the number of found SCSI hosts. Should be 1 or 0. */
}

static struct Scsi_Host *
ibmmca_register(Scsi_Host_Template * scsi_template, int port, int id,
		char *hostname)
{
   struct Scsi_Host *shpnt;
   int i, j;
   unsigned int ctrl;

   /* check I/O region */
   if (check_region(port, IM_N_IO_PORT))
     {
	printk("IBM MCA SCSI: Unable to get I/O region 0x%x-0x%x (%d ports).\n",
	       port, port + IM_N_IO_PORT - 1, IM_N_IO_PORT);
	return NULL;
     }

   /* register host */
   shpnt = scsi_register(scsi_template, sizeof(struct ibmmca_hostdata));
   if (!shpnt)
     {
	printk("IBM MCA SCSI: Unable to register host.\n");
	return NULL;
     }

   /* request I/O region */
   request_region(port, IM_N_IO_PORT, hostname);

   hosts[found] = shpnt; /* add new found hostadapter to the list */
   shpnt->irq = IM_IRQ; /* assign necessary stuff for the adapter */
   shpnt->io_port = port;
   shpnt->n_io_port = IM_N_IO_PORT;
   shpnt->this_id = id;
   /* now, the SCSI-subsystem is connected to Linux */

   ctrl = (unsigned int)(inb(IM_CTR_REG(found))); /* get control-register status */
#ifdef IM_DEBUG_PROBE
   printk("IBM MCA SCSI: Control Register contents: %x, status: %x\n",
	  ctrl,inb(IM_STAT_REG(found)));
   printk("IBM MCA SCSI: This adapters' POS-registers: ");
   for (i=0;i<8;i++)
     printk("%x ",pos[i]);
   printk("\n");
   if (bypass_controller)
     printk("IBM MCA SCSI: Subsystem SCSI-commands get bypassed.\n");
#endif

   reset_status(found) = IM_RESET_NOT_IN_PROGRESS;

   for (i = 0; i < 8; i++) /* reset the tables */
     for (j = 0; j < 8; j++)
     get_ldn(found)[i][j] = MAX_LOG_DEV;

   /* check which logical devices exist */
   local_checking_phase_flag(found) = 1;
   check_devices(found); /* call by value, using the global variable hosts*/
   local_checking_phase_flag(found) = 0;

   found++; /* now increase index to be prepared for next found subsystem */
   /* an ibm mca subsystem has been detected */
   return shpnt;
}

/*--------------------------------------------------------------------*/

int ibmmca_command (Scsi_Cmnd * cmd)
{
  ibmmca_queuecommand (cmd, internal_done);
  cmd->SCp.Status = 0;
  while (!cmd->SCp.Status)
    barrier ();
  return cmd->result;
}

/*--------------------------------------------------------------------*/

int ibmmca_release(struct Scsi_Host *shpnt)
{
  release_region(shpnt->io_port, shpnt->n_io_port);
  if (!(--found))
    free_irq(shpnt->irq, hosts);
  return 0;
}

/*--------------------------------------------------------------------*/

/* The following routine is the SCSI command queue. The old edition is
   now improved by dynamical reassignment of ldn numbers that are
   currently not assigned. The mechanism works in a way, that first
   the physical structure is checked. If at a certain pun,lun a device
   should be present, the routine proceeds to the ldn check from
   get_ldn. An answer of 0xff would show-up, that the aimed device is
   currently not assigned any ldn. At this point, the dynamical
   remapping algorithm is called. It works in a way, that it goes in
   cyclic order through the ldns from 7 to 14. If a ldn is assigned,
   it takes 8 dynamical reassignment calls, until a device looses its
   ldn again. With this method it is assured, that while doing
   intense I/O between up to eight devices, no dynamical remapping is
   done there. ldns 0 through 6(!) are left untouched, which means, that
   puns 0 through 7(!) on lun=0 are always accessible without remapping.
   These ldns are statically assigned by this driver. The subsystem always
   occupies at least one pun, therefore 7 ldns (at lun=0) for other devices
   are sufficient. (The adapter uses always ldn=15, at whatever pun it is.) */
int ibmmca_queuecommand (Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
   unsigned int ldn;
   unsigned int scsi_cmd;
   struct im_scb *scb;
   struct Scsi_Host *shpnt;
   int current_ldn;
   int id,lun;
   int target;
   int host_index;

   if (ibm_ansi_order)
     target = 6 - cmd->target;
   else
     target = cmd->target;

   shpnt = cmd->host;

   /* search for the right hostadapter */
   for (host_index = 0; hosts[host_index] && hosts[host_index]->host_no != shpnt->host_no; host_index++);

   if (!hosts[host_index])
     { /* invalid hostadapter descriptor address */
	cmd->result = DID_NO_CONNECT << 16;
	done (cmd);
	return 0;
     }

   /*if (target,lun) is NO LUN or not existing at all, return error */
   if ((get_scsi(host_index)[target][cmd->lun] == TYPE_NO_LUN)||
       (get_scsi(host_index)[target][cmd->lun] == TYPE_NO_DEVICE))
     {
	cmd->result = DID_NO_CONNECT << 16;
	done (cmd);
	return 0;
     }

   /*if (target,lun) unassigned, do further checks... */
   ldn = get_ldn(host_index)[target][cmd->lun];
   if (ldn >= MAX_LOG_DEV) /* on invalid ldn do special stuff */
     {
	if (ldn > MAX_LOG_DEV) /* dynamical remapping if ldn unassigned */
	  {
	     current_ldn = next_ldn(host_index); /* stop-value for one circle */
	     while (ld(host_index)[next_ldn(host_index)].cmd) /* search for a occupied, but not in */
	       {                      /* command-processing ldn. */
		  next_ldn(host_index)++;
		  if (next_ldn(host_index)>=MAX_LOG_DEV)
		    next_ldn(host_index) = 7;
		  if (current_ldn == next_ldn(host_index)) /* One circle done ? */
		    {         /* no non-processing ldn found */
		       printk("IBM MCA SCSI: Cannot assign SCSI-device dynamically!\n");
		       printk("              On ldn 7-14 SCSI-commands everywhere in progress.\n");
		       printk("              Reporting DID_NO_CONNECT for device (%d,%d).\n",
			      target, cmd->lun);
		       cmd->result = DID_NO_CONNECT << 16;/* return no connect*/
		       done (cmd);
		       return 0;
		    }
	       }

	     /* unmap non-processing ldn */
	     for (id=0; id<8; id ++)
	       for (lun=0; lun<8; lun++)
	       {
		  if (get_ldn(host_index)[id][lun] == next_ldn(host_index))
		    {
		       get_ldn(host_index)[id][lun] = TYPE_NO_DEVICE;
		       /* unmap entry */
		    }
	       }
	     /* set reduced interrupt_handler-mode for checking */
	     local_checking_phase_flag(host_index) = 1;
	     /* unassign found ldn (pun,lun does not matter for remove) */
	     immediate_assign(host_index,0,0,next_ldn(host_index),REMOVE_LDN);
	     /* assign found ldn to aimed pun,lun */
	     immediate_assign(host_index,target,cmd->lun,next_ldn(host_index),SET_LDN);
	     /* map found ldn to pun,lun */
	     get_ldn(host_index)[target][cmd->lun] = next_ldn(host_index);
	     /* change ldn to the right value, that is now next_ldn */
	     ldn = next_ldn(host_index);
	     /* get device information for ld[ldn] */
	     if (device_exists (host_index, ldn,
				&ld(host_index)[ldn].block_length,
				&ld(host_index)[ldn].device_type))
	       {
		  ld(host_index)[ldn].cmd = 0; /* To prevent panic set 0, because
						devices that were not assigned,
						should have nothing in progress. */

		  /* increase assignment counters for statistics in /proc */
		  IBM_DS(host_index).dynamical_assignments++;
		  IBM_DS(host_index).ldn_assignments[ldn]++;
	       }
	     else
	       /* panic here, because a device, found at boottime has
		vanished */
	       panic("IBM MCA SCSI: ldn=0x%x, SCSI-device on (%d,%d) vanished!\n",
		     ldn, target, cmd->lun);

	     /* set back to normal interrupt_handling */
	     local_checking_phase_flag(host_index) = 0;

	     /* Information on syslog terminal */
	     printk("IBM MCA SCSI: ldn=0x%x dynamically reassigned to (%d,%d).\n",
		    ldn, target, cmd->lun);

	     /* increase next_ldn for next dynamical assignment */
	     next_ldn(host_index)++;
	     if (next_ldn(host_index)>=MAX_LOG_DEV)
	       next_ldn(host_index) = 7;
	  }
	else
	  {  /* wall against Linux accesses to the subsystem adapter */
	     cmd->result = DID_BAD_TARGET << 16;
	     done (cmd);
	     return 0;
	  }
     }

   /*verify there is no command already in progress for this log dev */
   if (ld(host_index)[ldn].cmd)
     panic ("IBM MCA SCSI: cmd already in progress for this ldn.\n");

   /*save done in cmd, and save cmd for the interrupt handler */
   cmd->scsi_done = done;
   ld(host_index)[ldn].cmd = cmd;

   /*fill scb information independent of the scsi command */
   scb = &(ld(host_index)[ldn].scb);
   ld(host_index)[ldn].tsb.dev_status = 0;
   scb->enable = IM_REPORT_TSB_ONLY_ON_ERROR;
   scb->tsb_adr = virt_to_bus(&(ld(host_index)[ldn].tsb));
   if (cmd->use_sg)
     {
	int i = cmd->use_sg;
	struct scatterlist *sl = (struct scatterlist *) cmd->request_buffer;
	if (i > 16)
	  panic ("IBM MCA SCSI: scatter-gather list too long.\n");
	while (--i >= 0)
	  {
	     ld(host_index)[ldn].sge[i].address = (void *) virt_to_bus(sl[i].address);
	     ld(host_index)[ldn].sge[i].byte_length = sl[i].length;
	  }
	scb->enable |= IM_POINTER_TO_LIST;
	scb->sys_buf_adr = virt_to_bus(&(ld(host_index)[ldn].sge[0]));
	scb->sys_buf_length = cmd->use_sg * sizeof (struct im_sge);
     }
   else
     {
	scb->sys_buf_adr = virt_to_bus(cmd->request_buffer);
	scb->sys_buf_length = cmd->request_bufflen;
     }

   /*fill scb information dependent on scsi command */
   scsi_cmd = cmd->cmnd[0];

#ifdef IM_DEBUG_CMD
   printk("issue scsi cmd=%02x to ldn=%d\n", scsi_cmd, ldn);
#endif

   /* for specific device-type debugging: */
#ifdef IM_DEBUG_CMD_SPEC_DEV
   if (ld(host_index)[ldn].device_type==IM_DEBUG_CMD_DEVICE)
     printk("(SCSI-device-type=0x%x) issue scsi cmd=%02x to ldn=%d\n",
	    ld(host_index)[ldn].device_type, scsi_cmd, ldn);
#endif

   /* for possible panics store current command */
   last_scsi_command(host_index)[ldn] = scsi_cmd;
   last_scsi_type(host_index)[ldn] = IM_SCB;

   /* update statistical info */
   IBM_DS(host_index).total_accesses++;
   IBM_DS(host_index).ldn_access[ldn]++;

   switch (scsi_cmd)
     {
      case READ_6:
      case WRITE_6:
      case READ_10:
      case WRITE_10:
      case READ_12:
      case WRITE_12:
	/* statistics for proc_info */
	if ((scsi_cmd == READ_6)||(scsi_cmd == READ_10)||(scsi_cmd == READ_12))
	  IBM_DS(host_index).ldn_read_access[ldn]++; /* increase READ-access on ldn stat. */
	else if ((scsi_cmd == WRITE_6)||(scsi_cmd == WRITE_10)||
		 (scsi_cmd == WRITE_12))
	  IBM_DS(host_index).ldn_write_access[ldn]++; /* increase write-count on ldn stat.*/

	/* Distinguish between disk and other devices. Only disks (that are the
	   most frequently accessed devices) should be supported by the
         IBM-SCSI-Subsystem commands. */
	switch (ld(host_index)[ldn].device_type)
	  {
	   case TYPE_DISK: /* for harddisks enter here ... */
	   case TYPE_MOD:  /* ... try it also for MO-drives (send flames as */
	                   /*     you like, if this won't work.) */
	     if (scsi_cmd == READ_6 || scsi_cmd == READ_10 ||
		 scsi_cmd == READ_12)
	       { /* read command preparations */
		  if (bypass_controller)
		    {
		       scb->command = IM_OTHER_SCSI_CMD_CMD;
		       scb->enable |= IM_READ_CONTROL;
		       scb->u1.scsi_cmd_length = cmd->cmd_len;
		       memcpy(scb->u2.scsi_command,cmd->cmnd,cmd->cmd_len);
		    }
		  else
		    {
		       scb->command = IM_READ_DATA_CMD;
		       scb->enable |= IM_READ_CONTROL;
		    }
	       }
	     else
	       { /* write command preparations */
		  if (bypass_controller)
		    {
		       scb->command = IM_OTHER_SCSI_CMD_CMD;
		       scb->u1.scsi_cmd_length = cmd->cmd_len;
		       memcpy(scb->u2.scsi_command,cmd->cmnd,cmd->cmd_len);
		    }
		  else
		    {
		       scb->command = IM_WRITE_DATA_CMD;
		    }
	       }

	     if (!bypass_controller)
	       {
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
		  scb->u2.blk.length = ld(host_index)[ldn].block_length;
	       }
	     if (++disk_rw_in_progress == 1)
	       PS2_DISK_LED_ON (shpnt->host_no, target);
	     break;

	     /* for other devices, enter here. Other types are not known by
	      Linux! TYPE_NO_LUN is forbidden as valid device. */
	   case TYPE_ROM:
	   case TYPE_TAPE:
	   case TYPE_PROCESSOR:
	   case TYPE_WORM:
	   case TYPE_SCANNER:
	   case TYPE_MEDIUM_CHANGER:

	     /* If there is a sequential-device, IBM recommends to use
	      IM_OTHER_SCSI_CMD_CMD instead of subsystem READ/WRITE.
	      Good/modern CD-ROM-drives are capable of
	      reading sequential AND random-access. This leads to the problem,
	      that random-accesses are covered by the subsystem, but
	      sequentials are not, as like for tape-drives. Therefore, it is
	      the easiest way to use IM_OTHER_SCSI_CMD_CMD for all read-ops
	      on CD-ROM-drives in order not to run into timing problems and
	      to have a stable state. In addition, data-access on CD-ROMs
	      works faster like that. Strange, but obvious. */

	     scb->command = IM_OTHER_SCSI_CMD_CMD;
	     if (scsi_cmd == READ_6 || scsi_cmd == READ_10 ||
		 scsi_cmd == READ_12) /* enable READ */
	       {
		  scb->enable |= IM_READ_CONTROL;
	       }

	     scb->u1.scsi_cmd_length = cmd->cmd_len;
	     memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);

	     /* Read/write on this non-disk devices is also displayworthy,
	      so flash-up the LED/display. */
	     if (++disk_rw_in_progress == 1)
	       PS2_DISK_LED_ON (shpnt->host_no, target);
	     break;
	  }
	break;
      case INQUIRY:
	IBM_DS(host_index).ldn_inquiry_access[ldn]++;
	if (bypass_controller)
	  {
	     scb->command = IM_OTHER_SCSI_CMD_CMD;
	     scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
	     scb->u1.scsi_cmd_length = cmd->cmd_len;
	     memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
	  }
	else
	  {
	     scb->command = IM_DEVICE_INQUIRY_CMD;
	     scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
	  }
	break;

      case READ_CAPACITY:
	/* the length of system memory buffer must be exactly 8 bytes */
	if (scb->sys_buf_length > 8)
	  scb->sys_buf_length = 8;
	if (bypass_controller)
	  {
	     scb->command = IM_OTHER_SCSI_CMD_CMD;
	     scb->enable |= IM_READ_CONTROL;
	     scb->u1.scsi_cmd_length = cmd->cmd_len;
	     memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
	  }
	else
	  {
	     scb->command = IM_READ_CAPACITY_CMD;
	     scb->enable |= IM_READ_CONTROL;
	  }
	break;

	/* Commands that need read-only-mode (system <- device): */
      case REQUEST_SENSE:
	if (bypass_controller)
	  {
	     scb->command = IM_OTHER_SCSI_CMD_CMD;
	     scb->enable |= IM_READ_CONTROL;
	     scb->u1.scsi_cmd_length = cmd->cmd_len;
	     memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
	  }
	else
	  {
	     scb->command = IM_REQUEST_SENSE_CMD;
	     scb->enable |= IM_READ_CONTROL;
	  }
	break;

	/* Commands that need write-only-mode (system -> device): */
      case MODE_SELECT:
      case MODE_SELECT_10:
	IBM_DS(host_index).ldn_modeselect_access[ldn]++;
	scb->command = IM_OTHER_SCSI_CMD_CMD;
	scb->enable |= IM_SUPRESS_EXCEPTION_SHORT; /*Select needs WRITE-enabled*/
	scb->u1.scsi_cmd_length = cmd->cmd_len;
	memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
	break;

	/* For other commands, read-only is useful. Most other commands are
	 running without an input-data-block. */
      default:
	scb->command = IM_OTHER_SCSI_CMD_CMD;
	scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
	scb->u1.scsi_cmd_length = cmd->cmd_len;
	memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
	break;
     }

   /*issue scb command, and return */
   issue_cmd (host_index, virt_to_bus(scb), IM_SCB | ldn);
   return 0;
}

/*--------------------------------------------------------------------*/

int ibmmca_abort (Scsi_Cmnd * cmd)
{
   /* Abort does not work, as the adapter never generates an interrupt on
    * whatever situation is simulated, even when really pending commands
    * are running on the adapters' hardware ! */

   struct Scsi_Host *shpnt;
   unsigned int ldn;
   void (*saved_done) (Scsi_Cmnd *);
   int target;
   int host_index;
   static unsigned long flags;
   unsigned long imm_command;

   /* return SCSI_ABORT_SNOOZE ; */

   spin_lock_irqsave(&abort_lock, flags);
   if (ibm_ansi_order)
     target = 6 - cmd->target;
   else
     target = cmd->target;

   shpnt = cmd->host;

   /* search for the right hostadapter */
   for (host_index = 0; hosts[host_index] && hosts[host_index]->host_no != shpnt->host_no; host_index++);

   if (!hosts[host_index])
     { /* invalid hostadapter descriptor address */
	cmd->result = DID_NO_CONNECT << 16;
	if (cmd->scsi_done)
	  (cmd->done) (cmd);
	return SCSI_ABORT_SNOOZE;
     }

   /*get logical device number, and disable system interrupts */
   printk ("IBM MCA SCSI: Sending abort to device pun=%d, lun=%d.\n",
	   target, cmd->lun);
   ldn = get_ldn(host_index)[target][cmd->lun];

   /*if cmd for this ldn has already finished, no need to abort */
   if (!ld(host_index)[ldn].cmd)
     {
	spin_unlock_irqrestore(&abort_lock, flags);
	return SCSI_ABORT_NOT_RUNNING;
     }

   /* Clear ld.cmd, save done function, install internal done,
    * send abort immediate command (this enables sys. interrupts),
    * and wait until the interrupt arrives.
    */
   saved_done = cmd->scsi_done;
   cmd->scsi_done = internal_done;
   cmd->SCp.Status = 0;
   last_scsi_command(host_index)[ldn] = IM_ABORT_IMM_CMD;
   last_scsi_type(host_index)[ldn] = IM_IMM_CMD;
   imm_command = inl(IM_CMD_REG(host_index));
   imm_command &= (unsigned long)(0xffff0000); /* mask reserved stuff */
   imm_command |= (unsigned long)(IM_ABORT_IMM_CMD);
   /* must wait for attention reg not busy */
   while (1)
     {
	if (!(inb (IM_STAT_REG(host_index)) & IM_BUSY))
	  break;
	spin_unlock_irqrestore(&abort_lock, flags);

	spin_lock_irqsave(&abort_lock, flags);
     }
   /*write registers and enable system interrupts */
   outl (imm_command, IM_CMD_REG(host_index));
   outb (IM_IMM_CMD | ldn, IM_ATTN_REG(host_index));
   spin_unlock_irqrestore(&abort_lock, flags);

#ifdef IM_DEBUG_PROBE
	printk("IBM MCA SCSI: Abort submitted, waiting for adapter response...\n");
#endif
   while (!cmd->SCp.Status)
     barrier ();
   cmd->scsi_done = saved_done;
   /*if abort went well, call saved done, then return success or error */
   if (cmd->result == (DID_ABORT << 16))
     {
	cmd->result |= DID_ABORT << 16;
	if (cmd->scsi_done)
	  (cmd->scsi_done) (cmd);
	ld(host_index)[ldn].cmd = NULL;
#ifdef IM_DEBUG_PROBE
	printk("IBM MCA SCSI: Abort finished with success.\n");
#endif
	return SCSI_ABORT_SUCCESS;
     }
   else
     {
	cmd->result |= DID_NO_CONNECT << 16;
	if (cmd->scsi_done)
	  (cmd->scsi_done) (cmd);
	ld(host_index)[ldn].cmd = NULL;
#ifdef IM_DEBUG_PROBE
	printk("IBM MCA SCSI: Abort failed.\n");
#endif
	return SCSI_ABORT_ERROR;
     }
}

/*--------------------------------------------------------------------*/

int ibmmca_reset (Scsi_Cmnd * cmd, unsigned int reset_flags)
{
   struct Scsi_Host *shpnt;
   Scsi_Cmnd *cmd_aid;
   int ticks,i;
   int host_index;
   static unsigned long flags;
   unsigned long imm_command;

   spin_lock_irqsave(&reset_lock, flags);
   ticks = IM_RESET_DELAY*HZ;
   shpnt = cmd->host;
   /* search for the right hostadapter */
   for (host_index = 0; hosts[host_index] && hosts[host_index]->host_no != shpnt->host_no; host_index++);

   if (!hosts[host_index])
     { /* invalid hostadapter descriptor address */
	if (!local_checking_phase_flag(host_index))
	  {
	     cmd->result = DID_NO_CONNECT << 16;
	     if (cmd->scsi_done)
	       (cmd->done) (cmd);
	  }
	return SCSI_ABORT_SNOOZE;
     }

   if (local_checking_phase_flag(host_index))
     {
	printk("IBM MCA SCSI: unable to reset while checking devices.\n");
	spin_unlock_irqrestore(&reset_lock, flags);
	return SCSI_RESET_SNOOZE;
     }

   /* issue reset immediate command to subsystem, and wait for interrupt */
   printk("IBM MCA SCSI: resetting all devices.\n");
   reset_status(host_index) = IM_RESET_IN_PROGRESS;
   last_scsi_command(host_index)[0xf] = IM_RESET_IMM_CMD;
   last_scsi_type(host_index)[0xf] = IM_IMM_CMD;
   imm_command = inl(IM_CMD_REG(host_index));
   imm_command &= (unsigned long)(0xffff0000); /* mask reserved stuff */
   imm_command |= (unsigned long)(IM_RESET_IMM_CMD);
   /* must wait for attention reg not busy */
   while (1)
     {
	if (!(inb (IM_STAT_REG(host_index)) & IM_BUSY))
	  break;
	spin_unlock_irqrestore(&reset_lock, flags);
	spin_lock_irqsave(&reset_lock, flags);
     }
   /*write registers and enable system interrupts */
   outl (imm_command, IM_CMD_REG(host_index));
   outb (IM_IMM_CMD | 0xf, IM_ATTN_REG(host_index));
   /* wait for interrupt finished or intr_stat register to be set, as the
    * interrupt will not be executed, while we are in here! */
   while (reset_status(host_index) == IM_RESET_IN_PROGRESS && --ticks
	  && ((inb(IM_INTR_REG(host_index)) & 0x8f)!=0x8f)) {
      mdelay(1+999/HZ);
      barrier();
   }
   /* if reset did not complete, just return an error*/
   if (!ticks) {
      printk("IBM MCA SCSI: reset did not complete within %d seconds.\n",
	     IM_RESET_DELAY);
      reset_status(host_index) = IM_RESET_FINISHED_FAIL;
      spin_unlock_irqrestore(&reset_lock, flags);
      return SCSI_RESET_ERROR;
   }

   if ((inb(IM_INTR_REG(host_index)) & 0x8f)==0x8f)
     { /* analysis done by this routine and not by the intr-routine */
	if (inb(IM_INTR_REG(host_index))==0xaf)
	  reset_status(host_index) = IM_RESET_FINISHED_OK_NO_INT;
	else if (inb(IM_INTR_REG(host_index))==0xcf)
	  reset_status(host_index) = IM_RESET_FINISHED_FAIL;
	else /* failed, 4get it */
	  reset_status(host_index) = IM_RESET_NOT_IN_PROGRESS_NO_INT;
	outb (IM_EOI | 0xf, IM_ATTN_REG(host_index));
     }

   /* if reset failed, just return an error */
   if (reset_status(host_index) == IM_RESET_FINISHED_FAIL) {
      printk("IBM MCA SCSI: reset failed.\n");
      spin_unlock_irqrestore(&reset_lock, flags);
      return SCSI_RESET_ERROR;
   }

   /* so reset finished ok - call outstanding done's, and return success */
   printk ("IBM MCA SCSI: Reset completed without known error.\n");
   spin_unlock_irqrestore(&reset_lock, flags);
   for (i = 0; i < MAX_LOG_DEV; i++)
     {
	cmd_aid = ld(host_index)[i].cmd;
	if (cmd_aid && cmd_aid->scsi_done)
	  {
	     ld(host_index)[i].cmd = NULL;
	     cmd_aid->result = DID_RESET << 16;
	     (cmd_aid->scsi_done) (cmd_aid);
	  }
     }
   return SCSI_RESET_SUCCESS;
}

/*--------------------------------------------------------------------*/

int ibmmca_biosparam (Disk * disk, kdev_t dev, int *info)
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

/* calculate percentage of total accesses on a ldn */
static int ldn_access_load(int host_index, int ldn)
{
   if (IBM_DS(host_index).total_accesses == 0) return (0);
   if (IBM_DS(host_index).ldn_access[ldn] == 0) return (0);
   return (IBM_DS(host_index).ldn_access[ldn] * 100) / IBM_DS(host_index).total_accesses;
}

/* calculate total amount of r/w-accesses */
static int ldn_access_total_read_write(int host_index)
{
   int a;
   int i;

   a = 0;
   for (i=0; i<=MAX_LOG_DEV; i++)
     a+=IBM_DS(host_index).ldn_read_access[i]+IBM_DS(host_index).ldn_write_access[i];
   return(a);
}

static int ldn_access_total_inquiry(int host_index)
{
   int a;
   int i;

   a = 0;
   for (i=0; i<=MAX_LOG_DEV; i++)
     a+=IBM_DS(host_index).ldn_inquiry_access[i];
   return(a);
}

static int ldn_access_total_modeselect(int host_index)
{
   int a;
   int i;

   a = 0;
   for (i=0; i<=MAX_LOG_DEV; i++)
     a+=IBM_DS(host_index).ldn_modeselect_access[i];
   return(a);
}

/* routine to display info in the proc-fs-structure (a deluxe feature) */
int ibmmca_proc_info (char *buffer, char **start, off_t offset, int length,
		      int hostno, int inout)
{
   int len=0;
   int i,id,lun,host_index;
   struct Scsi_Host *shpnt;
   unsigned long flags;

   spin_lock_irqsave(&proc_lock, flags);

   for (i = 0; hosts[i] && hosts[i]->host_no != hostno; i++);
   shpnt = hosts[i];
   host_index = i;
   if (!shpnt) {
       len += sprintf(buffer+len, "\nCan't find adapter for host number %d\n", hostno);
       return len;
   }

   len += sprintf(buffer+len, "\n             IBM-SCSI-Subsystem-Linux-Driver, Version %s\n\n\n",
		  IBMMCA_SCSI_DRIVER_VERSION);
   len += sprintf(buffer+len, " SCSI Access-Statistics:\n");
   len += sprintf(buffer+len, "               Device Scanning Order....: %s\n",
		  (ibm_ansi_order) ? "IBM/ANSI" : "New Industry Standard");
#ifdef CONFIG_SCSI_MULTI_LUN
   len += sprintf(buffer+len, "               Multiple LUN probing.....: Yes\n");
#else
   len += sprintf(buffer+len, "               Multiple LUN probing.....: No\n");
#endif
   len += sprintf(buffer+len, "               This Hostnumber..........: %d\n",
		  hostno);
   len += sprintf(buffer+len, "               Base I/O-Port............: 0x%x\n",
		  (unsigned int)(IM_CMD_REG(host_index)));
   len += sprintf(buffer+len, "               (Shared) IRQ.............: %d\n",
		  IM_IRQ);
   len += sprintf(buffer+len, "               SCSI-command set used....: %s\n",
		  (bypass_controller) ? "software" : "hardware integrated");
   len += sprintf(buffer+len, "               Total Interrupts.........: %d\n",
		  IBM_DS(host_index).total_interrupts);
   len += sprintf(buffer+len, "               Total SCSI Accesses......: %d\n",
		  IBM_DS(host_index).total_accesses);
   len += sprintf(buffer+len, "                 Total SCSI READ/WRITE..: %d\n",
		  ldn_access_total_read_write(host_index));
   len += sprintf(buffer+len, "                 Total SCSI Inquiries...: %d\n",
		  ldn_access_total_inquiry(host_index));
   len += sprintf(buffer+len, "                 Total SCSI Modeselects.: %d\n",
		  ldn_access_total_modeselect(host_index));
   len += sprintf(buffer+len, "                 Total SCSI other cmds..: %d\n",
		  IBM_DS(host_index).total_accesses - ldn_access_total_read_write(host_index)
		  - ldn_access_total_modeselect(host_index)
		  - ldn_access_total_inquiry(host_index));
   len += sprintf(buffer+len, "               Total SCSI command fails.: %d\n\n",
		  IBM_DS(host_index).total_errors);
   len += sprintf(buffer+len, " Logical-Device-Number (LDN) Access-Statistics:\n");
   len += sprintf(buffer+len, "         LDN | Accesses [%%] |   READ    |   WRITE   | ASSIGNMENTS\n");
   len += sprintf(buffer+len, "        -----|--------------|-----------|-----------|--------------\n");
   for (i=0; i<=MAX_LOG_DEV; i++)
      len += sprintf(buffer+len, "         %2X  |    %3d       |  %8d |  %8d | %8d\n",
		     i, ldn_access_load(host_index, i), IBM_DS(host_index).ldn_read_access[i],
		     IBM_DS(host_index).ldn_write_access[i], IBM_DS(host_index).ldn_assignments[i]);
   len += sprintf(buffer+len, "        -----------------------------------------------------------\n\n");

   len += sprintf(buffer+len, " Dynamical-LDN-Assignment-Statistics:\n");
   len += sprintf(buffer+len, "               Number of physical SCSI-devices..: %d (+ Adapter)\n",
		  IBM_DS(host_index).total_scsi_devices);
   len += sprintf(buffer+len, "               Dynamical Assignment necessaray..: %s\n", 
		  IBM_DS(host_index).dyn_flag ? "Yes" : "No ");
   len += sprintf(buffer+len, "               Next LDN to be assigned..........: 0x%x\n",
		  next_ldn(host_index));
   len += sprintf(buffer+len, "               Dynamical assignments done yet...: %d\n",
		  IBM_DS(host_index).dynamical_assignments);

   len += sprintf(buffer+len, "\n Current SCSI-Device-Mapping:\n");
   len += sprintf(buffer+len, "        Physical SCSI-Device Map               Logical SCSI-Device Map\n");
   len += sprintf(buffer+len, "    ID\\LUN  0  1  2  3  4  5  6  7       ID\\LUN  0  1  2  3  4  5  6  7\n");
   for (id=0; id<=7; id++)
     {
	len += sprintf(buffer+len, "    %2d     ",id);
	for (lun=0; lun<8; lun++)
	  len += sprintf(buffer+len,"%2s ",ti_p(get_scsi(host_index)[id][lun]));

	len += sprintf(buffer+len, "      %2d     ",id);
	for (lun=0; lun<8; lun++)
	  len += sprintf(buffer+len,"%2s ",ti_l(get_ldn(host_index)[id][lun]));
	len += sprintf(buffer+len,"\n");
     }

   len += sprintf(buffer+len, "(A = IBM-Subsystem, D = Harddisk, T = Tapedrive, P = Processor, W = WORM,\n");
   len += sprintf(buffer+len, " R = CD-ROM, S = Scanner, M = MO-Drive, C = Medium-Changer, + = unprovided LUN,\n");
   len += sprintf(buffer+len, " - = nothing found, nothing assigned or unprobed LUN)\n\n");

   *start = buffer + offset;
   len -= offset;
   if (len > length)
     len = length;
   spin_unlock_irqrestore(&proc_lock, flags);
   return len;
}

#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = IBMMCA;

#include "scsi_module.c"

/*
 *	Module parameters
 */

MODULE_PARM(io_port, "1-" __MODULE_STRING(IM_MAX_HOSTS) "i");
MODULE_PARM(scsi_id, "1-" __MODULE_STRING(IM_MAX_HOSTS) "i");
MODULE_PARM(display, "1i");
MODULE_PARM(adisplay, "1i");
MODULE_PARM(bypass, "1i");
MODULE_PARM(normal, "1i");
MODULE_PARM(ansi, "1i");
#endif

/*--------------------------------------------------------------------*/

/*****************************************************************************/
/* ips.h -- driver for the IBM ServeRAID adapter                             */
/*                                                                           */
/* Written By: Keith Mitchell, IBM Corporation                               */
/*                                                                           */
/* Copyright (C) 1999 IBM Corporation                                        */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* NO WARRANTY                                                               */
/* THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR        */
/* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT      */
/* LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,      */
/* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is    */
/* solely responsible for determining the appropriateness of using and       */
/* distributing the Program and assumes all risks associated with its        */
/* exercise of rights under this Agreement, including but not limited to     */
/* the risks and costs of program errors, damage to or loss of data,         */
/* programs or equipment, and unavailability or interruption of operations.  */
/*                                                                           */
/* DISCLAIMER OF LIABILITY                                                   */
/* NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY   */
/* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL        */
/* DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND   */
/* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR     */
/* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE    */
/* USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED  */
/* HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES             */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/* Bugs/Comments/Suggestions should be mailed to:                            */
/*      ipslinux@us.ibm.com                                                  */
/*                                                                           */
/*****************************************************************************/

#ifndef _IPS_H_
   #define _IPS_H_

   #include <linux/config.h>
   #include <asm/uaccess.h>
   #include <asm/io.h>

   /* Prototypes */
   extern int ips_detect(Scsi_Host_Template *);
   extern int ips_release(struct Scsi_Host *);
   extern int ips_abort(Scsi_Cmnd *);
   extern int ips_reset(Scsi_Cmnd *, unsigned int);
   extern int ips_eh_abort(Scsi_Cmnd *);
   extern int ips_eh_reset(Scsi_Cmnd *);
   extern int ips_queue(Scsi_Cmnd *, void (*) (Scsi_Cmnd *));
   extern int ips_biosparam(Disk *, kdev_t, int *);
   extern const char * ips_info(struct Scsi_Host *);
   extern void do_ipsintr(int, void *, struct pt_regs *);

   /*
    * Some handy macros
    */
   #ifndef LinuxVersionCode
      #define LinuxVersionCode(x,y,z)   (((x)<<16)+((y)<<8)+(z))
   #endif

   #define HA(x)                       ((ips_ha_t *) x->hostdata)
   #define IPS_COMMAND_ID(ha, scb)     (int) (scb - ha->scbs)
   #define VIRT_TO_BUS(x)              (unsigned int)virt_to_bus((void *) x)

   #define UDELAY udelay
   #define MDELAY mdelay

   #define verify_area_20(t,a,sz)               (0) /* success */
   #define PUT_USER                             put_user
   #define __PUT_USER                           __put_user
   #define PUT_USER_RET                         put_user_ret
   #define GET_USER                             get_user
   #define __GET_USER                           __get_user
   #define GET_USER_RET                         get_user_ret

/*
 * Adapter address map equates
 */
   #define HISR                 0x08    /* Host Interrupt Status Reg   */
   #define CCSAR                0x10    /* Cmd Channel System Addr Reg */
   #define CCCR                 0x14    /* Cmd Channel Control Reg     */
   #define SQHR                 0x20    /* Status Q Head Reg           */
   #define SQTR                 0x24    /* Status Q Tail Reg           */
   #define SQER                 0x28    /* Status Q End Reg            */
   #define SQSR                 0x2C    /* Status Q Start Reg          */
   #define SCPR                 0x05    /* Subsystem control port reg  */
   #define ISPR                 0x06    /* interrupt status port reg   */
   #define CBSP                 0x07    /* CBSP register               */

/*
 * Adapter register bit equates
 */
   #define GHI                  0x04    /* HISR General Host Interrupt */
   #define SQO                  0x02    /* HISR Status Q Overflow      */
   #define SCE                  0x01    /* HISR Status Channel Enqueue */
   #define SEMAPHORE            0x08    /* CCCR Semaphore Bit          */
   #define ILE                  0x10    /* CCCR ILE Bit                */
   #define START_COMMAND        0x101A  /* CCCR Start Command Channel  */
   #define START_STOP_BIT       0x0002  /* CCCR Start/Stop Bit         */
   #define RST                  0x80    /* SCPR Reset Bit              */
   #define EBM                  0x02    /* SCPR Enable Bus Master      */
   #define EI                   0x80    /* HISR Enable Interrupts      */
   #define OP                   0x01    /* OP bit in CBSP              */

/*
 * Adapter Command ID Equates
 */
   #define GET_LOGICAL_DRIVE_INFO               0x19
   #define GET_SUBSYS_PARAM                     0x40
   #define READ_NVRAM_CONFIGURATION             0x38
   #define RW_NVRAM_PAGE                        0xBC
   #define IPS_READ                             0x02
   #define IPS_WRITE                            0x03
   #define ENQUIRY                              0x05
   #define FLUSH_CACHE                          0x0A
   #define NORM_STATE                           0x00
   #define READ_SCATTER_GATHER                  0x82
   #define WRITE_SCATTER_GATHER                 0x83
   #define DIRECT_CDB                           0x04
   #define DIRECT_CDB_SCATTER_GATHER            0x84
   #define CONFIG_SYNC                          0x58
   #define POCL                                 0x30
   #define GET_ERASE_ERROR_TABLE                0x17
   #define RESET_CHANNEL                        0x1A
   #define CSL                                  0xFF
   #define ADAPT_RESET                          0xFF

/*
 * Adapter Equates
 */
   #define IPS_MAX_ADAPTERS                     16
   #define IPS_MAX_IOCTL                        1
   #define IPS_MAX_IOCTL_QUEUE                  8
   #define IPS_MAX_QUEUE                        128
   #define IPS_BLKSIZE                          512
   #define MAX_SG_ELEMENTS                      17
   #define MAX_LOGICAL_DRIVES                   8
   #define MAX_CHANNELS                         3
   #define MAX_TARGETS                          15
   #define MAX_CHUNKS                           16
   #define MAX_CMDS                             64
   #define IPS_MAX_XFER                         0x10000
   #define COMP_MODE_HEADS                      128
   #define COMP_MODE_SECTORS                    32
   #define NORM_MODE_HEADS                      254
   #define NORM_MODE_SECTORS                    63
   #define NVRAM_PAGE5_SIGNATURE                0xFFDDBB99
   #define MAX_POST_BYTES                       0x02
   #define MAX_CONFIG_BYTES                     0x02
   #define GOOD_POST_BASIC_STATUS               0x80
   #define SEMAPHORE_TIMEOUT                    2000
   #define IPS_INTR_OFF                         0
   #define IPS_INTR_ON                          1
   #define IPS_ADAPTER_ID                       0xF
   #define IPS_VENDORID                         0x1014
   #define IPS_DEVICEID                         0x002E
   #define TIMEOUT_10                           0x10
   #define TIMEOUT_60                           0x20
   #define TIMEOUT_20M                          0x30
   #define STATUS_SIZE                          4
   #define STATUS_Q_SIZE                        (MAX_CMDS+1) * STATUS_SIZE
   #define ONE_MSEC                             1
   #define ONE_SEC                              1000

/*
 * Adapter Basic Status Codes
 */
   #define BASIC_STATUS_MASK            0xFF
   #define GSC_STATUS_MASK              0x0F
   #define SSUCCESS                     0x00
   #define RECOVERED_ERROR              0x01
   #define IPS_CHECK_CONDITION          0x02
   #define INVAL_OPCO                   0x03
   #define INVAL_CMD_BLK                0x04
   #define INVAL_PARM_BLK               0x05
   #define IPS_BUSY                     0x08
   #define ADAPT_HARDWARE_ERROR         0x09
   #define ADAPT_FIRMWARE_ERROR         0x0A
   #define CMD_CMPLT_WERROR             0x0C
   #define LOG_DRV_ERROR                0x0D
   #define CMD_TIMEOUT                  0x0E
   #define PHYS_DRV_ERROR               0x0F

/*
 * Adapter Extended Status Equates
 */
   #define SELECTION_TIMEOUT                    0xF0
   #define DATA_OVER_UNDER_RUN                  0xF2
   #define EXT_HOST_RESET                       0xF7
   #define EXT_DEVICE_RESET                     0xF8
   #define EXT_RECOVERY                         0xFC
   #define EXT_CHECK_CONDITION                  0xFF

/*
 * Operating System Defines
 */
   #define OS_WINDOWS_NT                        0x01
   #define OS_NETWARE                           0x02
   #define OS_OPENSERVER                        0x03
   #define OS_UNIXWARE                          0x04
   #define OS_SOLARIS                           0x05
   #define OS_OS2                               0x06
   #define OS_LINUX                             0x07
   #define OS_FREEBSD                           0x08

/*
 * Adapter Command/Status Packet Definitions
 */
   #define IPS_SUCCESS                  0x01 /* Successfully completed       */
   #define IPS_SUCCESS_IMM              0x02 /* Success - Immediately        */
   #define IPS_FAILURE                  0x04 /* Completed with Error         */

/*
 * Logical Drive Equates
 */
   #define OFF_LINE             0x02
   #define OKAY                 0x03
   #define FREE                 0x00
   #define SYS                  0x06
   #define CRS                  0x24

/*
 * DCDB Table Equates
 */
   #define NO_DISCONNECT                0x00
   #define DISCONNECT_ALLOWED           0x80
   #define NO_AUTO_REQUEST_SENSE        0x40
   #define IPS_DATA_IN                  0x01
   #define IPS_DATA_OUT                 0x02
   #define TRANSFER_64K                 0x08
   #define NOTIMEOUT                    0x00
   #define TIMEOUT10                    0x10
   #define TIMEOUT60                    0x20
   #define TIMEOUT20M                   0x30

/*
 * Host adapter Flags (bit numbers)
 */
   #define IPS_IN_INTR                  0
   #define IPS_IN_ABORT                 1
   #define IPS_IN_RESET                 2

/*
 * SCB Flags
 */
   #define SCB_ACTIVE                   0x00001
   #define SCB_WAITING                  0x00002

/*
 * Passthru stuff
 */
   #define COPPUSRCMD                  (('C'<<8) | 65)
   #define IPS_NUMCTRLS                (('C'<<8) | 68)
   #define IPS_CTRLINFO                (('C'<<8) | 69)

/*
 * Scsi_Host Template
 */
 #define IPS {                            \
    next : NULL,                          \
    module : NULL,                        \
    proc_info : NULL,                     \
    name : NULL,                          \
    detect : ips_detect,                  \
    release : ips_release,                \
    info : ips_info,                      \
    command : NULL,                       \
    queuecommand : ips_queue,             \
    eh_strategy_handler : NULL,           \
    eh_abort_handler : ips_eh_abort,      \
    eh_device_reset_handler : NULL,       \
    eh_bus_reset_handler : NULL,          \
    eh_host_reset_handler : ips_eh_reset, \
    abort : ips_abort,                    \
    reset : ips_reset,                    \
    slave_attach : NULL,                  \
    bios_param : ips_biosparam,           \
    can_queue : 0,                        \
    this_id: -1,                          \
    sg_tablesize : MAX_SG_ELEMENTS,       \
    cmd_per_lun: 16,                      \
    present : 0,                          \
    unchecked_isa_dma : 0,                \
    use_clustering : ENABLE_CLUSTERING,   \
    use_new_eh_code : 1                   \
 }

/*
 * IBM PCI Raid Command Formats
 */
typedef struct {
   u8        op_code;
   u8        command_id;
   u8        log_drv;
   u8        sg_count;
   u32       lba;
   u32       sg_addr;
   u16       sector_count;
   u16       reserved;
   u32       ccsar;
   u32       cccr;
} BASIC_IO_CMD, *PBASIC_IO_CMD;

typedef struct {
   u8        op_code;
   u8        command_id;
   u16       reserved;
   u32       reserved2;
   u32       buffer_addr;
   u32       reserved3;
   u32       ccsar;
   u32       cccr;
} LOGICAL_INFO, *PLOGICAL_INFO;

typedef struct {
   u8        op_code;
   u8        command_id;
   u8        reserved;
   u8        reserved2;
   u32       reserved3;
   u32       buffer_addr;
   u32       reserved4;
} IOCTL_INFO, *PIOCTL_INFO;

typedef struct {
   u8        op_code;
   u8        command_id;
   u16       reserved;
   u32       reserved2;
   u32       dcdb_address;
   u32       reserved3;
   u32       ccsar;
   u32       cccr;
} DCDB_CMD, *PDCDB_CMD;

typedef struct {
   u8        op_code;
   u8        command_id;
   u8        channel;
   u8        source_target;
   u32       reserved;
   u32       reserved2;
   u32       reserved3;
   u32       ccsar;
   u32       cccr;
} CONFIG_SYNC_CMD, *PCONFIG_SYNC_CMD;

typedef struct {
   u8        op_code;
   u8        command_id;
   u8        log_drv;
   u8        control;
   u32       reserved;
   u32       reserved2;
   u32       reserved3;
   u32       ccsar;
   u32       cccr;
} UNLOCK_STRIPE_CMD, *PUNLOCK_STRIPE_CMD;

typedef struct {
   u8        op_code;
   u8        command_id;
   u8        reserved;
   u8        state;
   u32       reserved2;
   u32       reserved3;
   u32       reserved4;
   u32       ccsar;
   u32       cccr;
} FLUSH_CACHE_CMD, *PFLUSH_CACHE_CMD;

typedef struct {
   u8        op_code;
   u8        command_id;
   u8        reserved;
   u8        desc;
   u32       reserved2;
   u32       buffer_addr;
   u32       reserved3;
   u32       ccsar;
   u32       cccr;
} STATUS_CMD, *PSTATUS_CMD;

typedef struct {
   u8        op_code;
   u8        command_id;
   u8        page;
   u8        write;
   u32       reserved;
   u32       buffer_addr;
   u32       reserved2;
   u32       ccsar;
   u32       cccr;
} NVRAM_CMD, *PNVRAM_CMD;

typedef union {
   BASIC_IO_CMD      basic_io;
   LOGICAL_INFO      logical_info;
   IOCTL_INFO        ioctl_info;
   DCDB_CMD          dcdb;
   CONFIG_SYNC_CMD   config_sync;
   UNLOCK_STRIPE_CMD unlock_stripe;
   FLUSH_CACHE_CMD   flush_cache;
   STATUS_CMD        status;
   NVRAM_CMD         nvram;
} HOST_COMMAND, *PHOST_COMMAND;

typedef struct {
   u8         logical_id;
   u8         reserved;
   u8         raid_level;
   u8         state;
   u32        sector_count;
} DRIVE_INFO, *PDRIVE_INFO;

typedef struct {
   u8         no_of_log_drive;
   u8         reserved[3];
   DRIVE_INFO drive_info[MAX_LOGICAL_DRIVES];
} LOGICAL_DRIVE_INFO, *PLOGICAL_DRIVE_INFO;

typedef struct {
   u8        ha_num;
   u8        bus_num;
   u8        id;
   u8        device_type;
   u32       data_len;
   u32       data_ptr;
   u8        scsi_cdb[12];
   u32       data_counter;
   u32       block_size;
} NON_DISK_DEVICE_INFO, *PNON_DISK_DEVICE_INFO;

typedef struct {
   u8         device_address;
   u8         cmd_attribute;
   u16        transfer_length;
   u32        buffer_pointer;
   u8         cdb_length;
   u8         sense_length;
   u8         sg_count;
   u8         reserved;
   u8         scsi_cdb[12];
   u8         sense_info[64];
   u8         scsi_status;
   u8         reserved2[3];
} DCDB_TABLE, *PDCDB_TABLE;

typedef struct {
   volatile u8      reserved;
   volatile u8      command_id;
   volatile u8      basic_status;
   volatile u8      extended_status;
} STATUS, *PSTATUS;

typedef struct {
   STATUS               status[MAX_CMDS + 1];
   volatile PSTATUS     p_status_start;
   volatile PSTATUS     p_status_end;
   volatile PSTATUS     p_status_tail;
   volatile u32         hw_status_start;
   volatile u32         hw_status_tail;
   LOGICAL_DRIVE_INFO   logical_drive_info;
} ADAPTER_AREA, *PADAPTER_AREA;

typedef struct {
   u8        ucLogDriveCount;
   u8        ucMiscFlag;
   u8        ucSLTFlag;
   u8        ucBSTFlag;
   u8        ucPwrChgCnt;
   u8        ucWrongAdrCnt;
   u8        ucUnidentCnt;
   u8        ucNVramDevChgCnt;
   u8        CodeBlkVersion[8];
   u8        BootBlkVersion[8];
   u32       ulDriveSize[MAX_LOGICAL_DRIVES];
   u8        ucConcurrentCmdCount;
   u8        ucMaxPhysicalDevices;
   u16       usFlashRepgmCount;
   u8        ucDefunctDiskCount;
   u8        ucRebuildFlag;
   u8        ucOfflineLogDrvCount;
   u8        ucCriticalDrvCount;
   u16       usConfigUpdateCount;
   u8        ucBlkFlag;
   u8        reserved;
   u16       usAddrDeadDisk[MAX_CHANNELS * MAX_TARGETS];
} ENQCMD, *PENQCMD;

typedef struct {
   u8        ucInitiator;
   u8        ucParameters;
   u8        ucMiscFlag;
   u8        ucState;
   u32       ulBlockCount;
   u8        ucDeviceId[28];
} DEVSTATE, *PDEVSTATE;

typedef struct {
   u8        ucChn;
   u8        ucTgt;
   u16       ucReserved;
   u32       ulStartSect;
   u32       ulNoOfSects;
} CHUNK, *PCHUNK;

typedef struct {
   u16       ucUserField;
   u8        ucState;
   u8        ucRaidCacheParam;
   u8        ucNoOfChunkUnits;
   u8        ucStripeSize;
   u8        ucParams;
   u8        ucReserved;
   u32       ulLogDrvSize;
   CHUNK     chunk[MAX_CHUNKS];
} LOGICAL_DRIVE, *PLOGICAL_DRIVE;

typedef struct {
   u8        board_disc[8];
   u8        processor[8];
   u8        ucNoChanType;
   u8        ucNoHostIntType;
   u8        ucCompression;
   u8        ucNvramType;
   u32       ulNvramSize;
} HARDWARE_DISC, *PHARDWARE_DISC;

typedef struct {
   u8             ucLogDriveCount;
   u8             ucDateD;
   u8             ucDateM;
   u8             ucDateY;
   u8             init_id[4];
   u8             host_id[12];
   u8             time_sign[8];

   struct {
      u32         usCfgDrvUpdateCnt:16;
      u32         ConcurDrvStartCnt:4;
      u32         StartupDelay:4;
      u32         auto_rearrange:1;
      u32         cd_boot:1;
      u32         cluster:1;
      u32         reserved:5;
   } UserOpt;

   u16            user_field;
   u8             ucRebuildRate;
   u8             ucReserve;
   HARDWARE_DISC  hardware_disc;
   LOGICAL_DRIVE  logical_drive[MAX_LOGICAL_DRIVES];
   DEVSTATE       dev[MAX_CHANNELS][MAX_TARGETS+1];
   u8             reserved[512];

} CONFCMD, *PCONFCMD;

typedef struct {
   u32        signature;
   u8         reserved;
   u8         adapter_slot;
   u16        adapter_type;
   u8         bios_high[4];
   u8         bios_low[4];
   u16        reserved2;
   u8         reserved3;
   u8         operating_system;
   u8         driver_high[4];
   u8         driver_low[4];
   u8         reserved4[100];
} NVRAM_PAGE5, *PNVRAM_PAGE5;

typedef struct _SUBSYS_PARAM {
   u32        param[128];
} SUBSYS_PARAM, *PSUBSYS_PARAM;

/*
 * Inquiry Data Format
 */
typedef struct {
   u8        DeviceType:5;
   u8        DeviceTypeQualifier:3;
   u8        DeviceTypeModifier:7;
   u8        RemoveableMedia:1;
   u8        Versions;
   u8        ResponseDataFormat;
   u8        AdditionalLength;
   u16       Reserved;
   u8        SoftReset:1;
   u8        CommandQueue:1;
   u8        Reserved2:1;
   u8        LinkedCommands:1;
   u8        Synchronous:1;
   u8        Wide16Bit:1;
   u8        Wide32Bit:1;
   u8        RelativeAddressing:1;
   u8        VendorId[8];
   u8        ProductId[16];
   u8        ProductRevisionLevel[4];
   u8        VendorSpecific[20];
   u8        Reserved3[40];
} IPS_INQUIRYDATA, *IPS_PINQUIRYDATA;

/*
 * Read Capacity Data Format
 */
typedef struct {
   u32       lba;
   u32       len;
} CAPACITY_T;

/*
 * Sense Data Format
 */
typedef struct {
   u8        pg_pc:6;       /* Page Code                    */
   u8        pg_res1:2;     /* Reserved                     */
   u8        pg_len;        /* Page Length                  */
   u16       pg_trk_z;      /* Tracks per zone              */
   u16       pg_asec_z;     /* Alternate sectors per zone   */
   u16       pg_atrk_z;     /* Alternate tracks per zone    */
   u16       pg_atrk_v;     /* Alternate tracks per volume  */
   u16       pg_sec_t;      /* Sectors per track            */
   u16       pg_bytes_s;    /* Bytes per physical sectors   */
   u16       pg_intl;       /* Interleave                   */
   u16       pg_trkskew;    /* Track skew factor            */
   u16       pg_cylskew;    /* Cylinder Skew Factor         */
   u32       pg_res2:27;    /* Reserved                     */
   u32       pg_ins:1;      /* Inhibit Slave                */
   u32       pg_surf:1;     /* Allocate Surface Sectors     */
   u32       pg_rmb:1;      /* Removeable                   */
   u32       pg_hsec:1;     /* Hard sector formatting       */
   u32       pg_ssec:1;     /* Soft sector formatting       */
} DADF_T;

typedef struct {
   u8        pg_pc:6;        /* Page Code                     */
   u8        pg_res1:2;      /* Reserved                      */
   u8        pg_len;         /* Page Length                   */
   u16       pg_cylu;        /* Number of cylinders (upper)   */
   u8        pg_cyll;        /* Number of cylinders (lower)   */
   u8        pg_head;        /* Number of heads               */
   u16       pg_wrpcompu;    /* Write precomp (upper)         */
   u32       pg_wrpcompl:8;  /* Write precomp (lower)         */
   u32       pg_redwrcur:24; /* Reduced write current         */
   u32       pg_drstep:16;   /* Drive step rate               */
   u32       pg_landu:16;    /* Landing zone cylinder (upper) */
   u32       pg_landl:8;     /* Landing zone cylinder (lower) */
   u32       pg_res2:24;     /* Reserved                      */
} RDDG_T;

struct blk_desc {
   u8       bd_dencode;
   u8       bd_nblks1;
   u8       bd_nblks2;
   u8       bd_nblks3;
   u8       bd_res;
   u8       bd_blen1;
   u8       bd_blen2;
   u8       bd_blen3;
};

typedef struct {
   u8       plh_len;   /* Data length             */
   u8       plh_type;  /* Medium type             */
   u8       plh_res:7; /* Reserved                */
   u8       plh_wp:1;  /* Write protect           */
   u8       plh_bdl;   /* Block descriptor length */
} SENSE_PLH_T;

typedef struct {
   SENSE_PLH_T     plh;
   struct blk_desc blk_desc;

   union {
      DADF_T        pg3;
      RDDG_T        pg4;
   } pdata;
} ips_mdata_t;

/*
 * Scatter Gather list format
 */
typedef struct ips_sglist {
   u32       address;
   u32       length;
} SG_LIST, *PSG_LIST;

typedef struct _INFOSTR {
   char *buffer;
   int   length;
   int   offset;
   int   pos;
} INFOSTR;

/*
 * Status Info
 */
typedef struct ips_stat {
   u32       residue_len;
   u32       scb_addr;
} ips_stat_t;

/*
 * SCB Queue Format
 */
typedef struct ips_scb_queue {
   struct ips_scb *head;
   struct ips_scb *tail;
   unsigned int    count;
} ips_scb_queue_t;

/*
 * Wait queue_format
 */
typedef struct ips_wait_queue {
   Scsi_Cmnd      *head;
   Scsi_Cmnd      *tail;
   unsigned int    count;
} ips_wait_queue_t;

typedef struct ips_ha {
   u8                 ha_id[MAX_CHANNELS+1];
   u32                io_addr;            /* Base I/O address           */
   u8                 irq;                /* IRQ for adapter            */
   u8                 ntargets;           /* Number of targets          */
   u8                 nbus;               /* Number of buses            */
   u8                 nlun;               /* Number of Luns             */
   u16                ad_type;            /* Adapter type               */
   u16                host_num;           /* Adapter number             */
   u32                max_xfer;           /* Maximum Xfer size          */
   u32                max_cmds;           /* Max concurrent commands    */
   u32                num_ioctl;          /* Number of Ioctls           */
   ips_stat_t         sp;                 /* Status packer pointer      */
   struct ips_scb    *scbs;               /* Array of all CCBS          */
   struct ips_scb    *scb_freelist;       /* SCB free list              */
   ips_wait_queue_t   scb_waitlist;       /* Pending SCB list           */
   ips_wait_queue_t   copp_waitlist;      /* Pending PT list            */
   ips_scb_queue_t    scb_activelist;     /* Active SCB list            */
   BASIC_IO_CMD      *dummy;              /* dummy command              */
   ADAPTER_AREA      *adapt;              /* Adapter status area        */
   ENQCMD            *enq;                /* Adapter Enquiry data       */
   CONFCMD           *conf;               /* Adapter config data        */
   NVRAM_PAGE5       *nvram;              /* NVRAM page 5 data          */
   SUBSYS_PARAM      *subsys;             /* Subsystem parameters       */
   u32                cmd_in_progress;    /* Current command in progress*/
   u32                flags;              /* HA flags                   */
   u8                 waitflag;           /* are we waiting for cmd     */
   u8                 active;
   u32                reserved:16;        /* reserved space             */
   wait_queue_head_t  copp_queue;         /* passthru sync queue        */

   #if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,0)
   spinlock_t         scb_lock;
   spinlock_t         copp_lock;
   #endif
} ips_ha_t;

typedef void (*scb_callback) (ips_ha_t *, struct ips_scb *);

/*
 * SCB Format
 */
typedef struct ips_scb {
   HOST_COMMAND      cmd;
   DCDB_TABLE        dcdb;
   u8                target_id;
   u8                bus;
   u8                lun;
   u8                cdb[12];
   u32               scb_busaddr;
   u32               data_busaddr;
   u32               timeout;
   u8                basic_status;
   u8                extended_status;
   u16               breakup;
   u32               data_len;
   u32               sg_len;
   u32               flags;
   u32               op_code;
   SG_LIST          *sg_list;
   Scsi_Cmnd        *scsi_cmd;
   struct ips_scb   *q_next;
   scb_callback      callback;
} ips_scb_t;

/*
 * Passthru Command Format
 */
typedef struct {
   u8         CoppID[4];
   u32        CoppCmd;
   u32        PtBuffer;
   u8        *CmdBuffer;
   u32        CmdBSize;
   ips_scb_t  CoppCP;
   u32        TimeOut;
   u8         BasicStatus;
   u8         ExtendedStatus;
   u16        reserved;
} ips_passthru_t;

#endif



/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

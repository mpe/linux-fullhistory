#ifndef __MEGARAID_H__
#define __MEGARAID_H__

#include <linux/version.h>

#define IN_ISR                  0x80000000L
#define NO_INTR                 0x40000000L
#define IN_TIMEOUT              0x20000000L
#define PENDING                 0x10000000L
#define BOARD_QUARTZ            0x08000000L

#define SCB_ACTIVE 0x1
#define SCB_WAITQ  0x2
#define SCB_ISSUED 0x4

#define SCB_FREE                -1
#define SCB_RESET               -2
#define SCB_ABORT               -3
#define SCB_LOCKED              -4

#define MEGA_CMD_TIMEOUT        10

#define MAX_SGLIST              20
#define MAX_COMMANDS            254

#define MAX_LOGICAL_DRIVES      8
#define MAX_CHANNEL             5
#define MAX_TARGET              15
#define MAX_PHYSICAL_DRIVES     MAX_CHANNEL*MAX_TARGET

#define INQUIRY_DATA_SIZE       0x24
#define MAX_CDB_LEN             0x0A
#define MAX_REQ_SENSE_LEN       0x20

#define INTR_VALID              0x40

/* Mailbox commands */
#define MEGA_MBOXCMD_LREAD      0x01
#define MEGA_MBOXCMD_LWRITE     0x02
#define MEGA_MBOXCMD_PASSTHRU   0x03
#define MEGA_MBOXCMD_ADAPTERINQ 0x05

/* Offsets into Mailbox */
#define COMMAND_PORT       0x00
#define COMMAND_ID_PORT    0x01
#define SG_LIST_PORT0      0x08
#define SG_LIST_PORT1      0x09
#define SG_LIST_PORT2      0x0a
#define SG_LIST_PORT3      0x0b
#define SG_ELEMENT_PORT    0x0d
#define NO_FIRED_PORT      0x0f

/* I/O Port offsets */
#define I_CMD_PORT         0x00
#define I_ACK_PORT         0x00
#define I_TOGGLE_PORT      0x01
#define INTR_PORT          0x0a

#define MAILBOX_SIZE       70
#define MBOX_BUSY_PORT     0x00
#define MBOX_PORT0         0x04
#define MBOX_PORT1         0x05
#define MBOX_PORT2         0x06
#define MBOX_PORT3         0x07
#define ENABLE_MBOX_REGION 0x0B

/* I/O Port Values */
#define ISSUE_BYTE         0x10
#define ACK_BYTE           0x08
#define ENABLE_INTR_BYTE   0xc0
#define DISABLE_INTR_BYTE  0x00
#define VALID_INTR_BYTE    0x40
#define MBOX_BUSY_BYTE     0x10
#define ENABLE_MBOX_BYTE   0x00

/* Setup some port macros here */
#define WRITE_MAILBOX(base,offset,value)   *(base+offset)=value
#define READ_MAILBOX(base,offset)          *(base+offset)

#define WRITE_PORT(base,offset,value)      outb_p(value,base+offset)
#define READ_PORT(base,offset)             inb_p(base+offset)

#define ISSUE_COMMAND(base)   WRITE_PORT(base,I_CMD_PORT,ISSUE_BYTE)
#define CLEAR_INTR(base)      WRITE_PORT(base,I_ACK_PORT,ACK_BYTE)
#define ENABLE_INTR(base)     WRITE_PORT(base,I_TOGGLE_PORT,ENABLE_INTR_BYTE)
#define DISABLE_INTR(base)    WRITE_PORT(base,I_TOGGLE_PORT,DISABLE_INTR_BYTE)

/* Define AMI's PCI codes */
#undef PCI_VENDOR_ID_AMI
#undef PCI_DEVICE_ID_AMI_MEGARAID

#ifndef PCI_VENDOR_ID_AMI
#define PCI_VENDOR_ID_AMI          0x101E
#define PCI_DEVICE_ID_AMI_MEGARAID 0x9010
#endif

#define PCI_CONF_BASE_ADDR_OFFSET  0x10
#define PCI_CONF_IRQ_OFFSET        0x3c

#if LINUX_VERSION_CODE < 0x20100
#define MEGARAID \
  { NULL,                               /* Next                      */\
    NULL,                               /* Usage Count Pointer       */\
    NULL,                               /* /proc Directory Entry     */\
    megaraid_proc_info,                 /* /proc Info Function       */\
    "MegaRAID",                         /* Driver Name               */\
    megaraid_detect,                    /* Detect Host Adapter       */\
    megaraid_release,                   /* Release Host Adapter      */\
    megaraid_info,                      /* Driver Info Function      */\
    megaraid_command,                   /* Command Function          */\
    megaraid_queue,                     /* Queue Command Function    */\
    megaraid_abort,                     /* Abort Command Function    */\
    megaraid_reset,                     /* Reset Command Function    */\
    NULL,                               /* Slave Attach Function     */\
    megaraid_biosparam,                 /* Disk BIOS Parameters      */\
    1,                                  /* # of cmds that can be\
                                           outstanding at any time */\
    7,                                  /* HBA Target ID             */\
    MAX_SGLIST,                         /* Scatter/Gather Table Size */\
    1,                                  /* SCSI Commands per LUN     */\
    0,                                  /* Present                   */\
    0,                                  /* Default Unchecked ISA DMA */\
    ENABLE_CLUSTERING }                 /* Enable Clustering         */
#else
#define MEGARAID \
  {\
    name:            "MegaRAID",               /* Driver Name               */\
    proc_info:        megaraid_proc_info,      /* /proc driver info         */\
    detect:           megaraid_detect,         /* Detect Host Adapter       */\
    release:          megaraid_release,        /* Release Host Adapter      */\
    info:             megaraid_info,           /* Driver Info Function      */\
    command:          megaraid_command,        /* Command Function          */\
    queuecommand:     megaraid_queue,          /* Queue Command Function    */\
    abort:            megaraid_abort,          /* Abort Command Function    */\
    reset:            megaraid_reset,          /* Reset Command Function    */\
    bios_param:       megaraid_biosparam,      /* Disk BIOS Parameters      */\
    can_queue:        255,                     /* Can Queue                 */\
    this_id:          7,                       /* HBA Target ID             */\
    sg_tablesize:     MAX_SGLIST,              /* Scatter/Gather Table Size */\
    cmd_per_lun:      1,                       /* SCSI Commands per LUN     */\
    present:          0,                       /* Present                   */\
    unchecked_isa_dma:0,                       /* Default Unchecked ISA DMA */\
    use_clustering:   ENABLE_CLUSTERING       /* Enable Clustering         */\
  }
#endif

/* Structures */
typedef struct _mega_ADP_INFO
{
  u_char    MaxConcCmds;
  u_char    RbldRate;
  u_char    MaxTargPerChan;
  u_char    ChanPresent;
  u_char    FwVer[4];
  u_short   AgeOfFlash;
  u_char    ChipSet;
  u_char    DRAMSize;
  u_char    CacheFlushInterval;
  u_char    BiosVer[4];
  u_char    resvd[7];
} mega_ADP_INFO;

typedef struct _mega_LDRV_INFO
{
  u_char   NumLDrv;
  u_char   resvd[3];
  u_long   LDrvSize[MAX_LOGICAL_DRIVES];
  u_char   LDrvProp[MAX_LOGICAL_DRIVES];
  u_char   LDrvState[MAX_LOGICAL_DRIVES];
} mega_LDRV_INFO;

typedef struct _mega_PDRV_INFO
{
  u_char   PDrvState[MAX_PHYSICAL_DRIVES];
  u_char   resvd;
} mega_PDRV_INFO;

// RAID inquiry: Mailbox command 0x5
typedef struct _mega_RAIDINQ
{
  mega_ADP_INFO    AdpInfo;
  mega_LDRV_INFO   LogdrvInfo;
  mega_PDRV_INFO   PhysdrvInfo;
} mega_RAIDINQ;

// Passthrough command: Mailbox command 0x3
typedef struct mega_passthru
{
  u_char            timeout:3;              /* 0=6sec/1=60sec/2=10min/3=3hrs */
  u_char            ars:1;
  u_char            reserved:3;
  u_char            islogical:1;
  u_char            logdrv;                 /* if islogical == 1 */
  u_char            channel;                /* if islogical == 0 */
  u_char            target;                 /* if islogical == 0 */
  u_char            queuetag;               /* unused */
  u_char            queueaction;            /* unused */
  u_char            cdb[MAX_CDB_LEN];
  u_char            cdblen;
  u_char            reqsenselen;
  u_char            reqsensearea[MAX_REQ_SENSE_LEN];
  u_char            numsgelements;
  u_char            scsistatus;
  u_long            dataxferaddr;
  u_long            dataxferlen;
} mega_passthru;

typedef struct _mega_mailbox
{
  /* 0x0 */ u_char    cmd;
  /* 0x1 */ u_char    cmdid;
  /* 0x2 */ u_short   numsectors;
  /* 0x4 */ u_long    lba;
  /* 0x8 */ u_long    xferaddr;
  /* 0xC */ u_char    logdrv;
  /* 0xD */ u_char    numsgelements;
  /* 0xE */ u_char    resvd;
  /* 0xF */ u_char    busy;
  /* 0x10*/ u_char    numstatus;
  /* 0x11*/ u_char    status;
  /* 0x12*/ u_char    completed[46];
            u_char    mraid_poll;
            u_char    mraid_ack;
            u_char    pad[16];
} mega_mailbox;

typedef struct _mega_sglist
{
  u_long     address;
  u_long     length;
} mega_sglist;

/* Queued command data */
typedef struct _mega_scb mega_scb;

struct _mega_scb
{
  int             idx;
  u_long          flag;
  Scsi_Cmnd      *SCpnt;
  u_char          mboxData[16];
  mega_passthru   pthru;
  mega_sglist    *sgList;
  mega_scb       *next;
};

/* Per-controller data */
typedef struct _mega_host_config
{
  u_char               numldrv;
  u_long               flag;
  u_long               base;

  struct tq_struct     megaTq;

  /* Host adapter parameters */
  u_char               fwVer[7];
  u_char               biosVer[7];

  struct Scsi_Host     *host;

  /* The following must be DMA-able!! */
  volatile mega_mailbox *mbox;
  volatile mega_mailbox mailbox;
  volatile u_char       mega_buffer[2*1024L];

  u_char                max_cmds;
  mega_scb              scbList[MAX_COMMANDS];
} mega_host_config;

extern struct proc_dir_entry proc_scsi_megaraid;

const char *megaraid_info( struct Scsi_Host * );
int        megaraid_detect( Scsi_Host_Template * );
int        megaraid_release(struct Scsi_Host *);
int        megaraid_command( Scsi_Cmnd * );
int        megaraid_abort( Scsi_Cmnd * );
int        megaraid_reset( Scsi_Cmnd *, unsigned int); 
int        megaraid_queue( Scsi_Cmnd *, void (*done)(Scsi_Cmnd *) );
int        megaraid_biosparam( Disk *, kdev_t, int * );
int        megaraid_proc_info( char *buffer, char **start, off_t offset,
			       int length, int hostno, int inout );

#endif

#ifndef _GDTH_H
#define _GDTH_H

/*
 * Header file for the GDT ISA/EISA/PCI Disk Array Controller driver for Linux
 * 
 * gdth.h Copyright (C) 1995-97 ICP vortex Computersysteme GmbH, Achim Leubner
 * See gdth.c for further informations and 
 * below for supported controller types
 *
 * <achim@vortex.de>
 *
 * $Id: gdth.h,v 1.9 1997/11/04 09:55:42 achim Exp $
 */

#include <linux/version.h>
#include <linux/types.h>

#ifndef NULL
#define NULL 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* defines, macros */

/* driver version */
#define GDTH_VERSION_STR        "1.02"
#define GDTH_VERSION            1
#define GDTH_SUBVERSION         2

/* protocol version */
#define PROTOCOL_VERSION        1

/* controller classes */
#define GDT_ISA         0x01                    /* ISA controller */
#define GDT_EISA        0x02                    /* EISA controller */
#define GDT_PCI         0x03                    /* PCI controller */
#define GDT_PCINEW      0x04                    /* new PCI controller */
#define GDT_PCIMPR      0x05                    /* PCI MPR controller */
/* GDT_EISA, controller subtypes EISA */
#define GDT3_ID         0x0130941c              /* GDT3000/3020 */
#define GDT3A_ID        0x0230941c              /* GDT3000A/3020A/3050A */
#define GDT3B_ID        0x0330941c              /* GDT3000B/3010A */
/* GDT_ISA */
#define GDT2_ID         0x0120941c              /* GDT2000/2020 */
/* vendor ID, device IDs (PCI) */
/* these defines should already exist in <linux/pci.h> */
#ifndef PCI_VENDOR_ID_VORTEX
#define PCI_VENDOR_ID_VORTEX            0x1119  /* PCI controller vendor ID */
#endif
#ifndef PCI_DEVICE_ID_VORTEX_GDT60x0
/* GDT_PCI */
#define PCI_DEVICE_ID_VORTEX_GDT60x0    0       /* GDT6000/6020/6050 */
#define PCI_DEVICE_ID_VORTEX_GDT6000B   1       /* GDT6000B/6010 */
/* GDT_PCINEW */
#define PCI_DEVICE_ID_VORTEX_GDT6x10    2       /* GDT6110/6510 */
#define PCI_DEVICE_ID_VORTEX_GDT6x20    3       /* GDT6120/6520 */
#define PCI_DEVICE_ID_VORTEX_GDT6530    4       /* GDT6530 */
#define PCI_DEVICE_ID_VORTEX_GDT6550    5       /* GDT6550 */
/* GDT_PCINEW, wide/ultra SCSI controllers */
#define PCI_DEVICE_ID_VORTEX_GDT6x17    6       /* GDT6117/6517 */
#define PCI_DEVICE_ID_VORTEX_GDT6x27    7       /* GDT6127/6527 */
#define PCI_DEVICE_ID_VORTEX_GDT6537    8       /* GDT6537 */
#define PCI_DEVICE_ID_VORTEX_GDT6557    9       /* GDT6557/6557-ECC */
/* GDT_PCINEW, wide SCSI controllers */
#define PCI_DEVICE_ID_VORTEX_GDT6x15    10      /* GDT6115/6515 */
#define PCI_DEVICE_ID_VORTEX_GDT6x25    11      /* GDT6125/6525 */
#define PCI_DEVICE_ID_VORTEX_GDT6535    12      /* GDT6535 */
#define PCI_DEVICE_ID_VORTEX_GDT6555    13      /* GDT6555/6555-ECC */
#endif

#ifndef PCI_DEVICE_ID_VORTEX_GDT6x17RP
/* GDT_MPR, RP series, wide/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x17RP  0x100   /* GDT6117RP/GDT6517RP */
#define PCI_DEVICE_ID_VORTEX_GDT6x27RP  0x101   /* GDT6127RP/GDT6527RP */
#define PCI_DEVICE_ID_VORTEX_GDT6537RP  0x102   /* GDT6537RP */
#define PCI_DEVICE_ID_VORTEX_GDT6557RP  0x103   /* GDT6557RP */
/* GDT_MPR, RP series, narrow/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x11RP  0x104   /* GDT6111RP/GDT6511RP */
#define PCI_DEVICE_ID_VORTEX_GDT6x21RP  0x105   /* GDT6121RP/GDT6521RP */
/* GDT_MPR, RP1 series, wide/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x17RP1 0x110   /* GDT6117RP1/GDT6517RP1 */
#define PCI_DEVICE_ID_VORTEX_GDT6x27RP1 0x111   /* GDT6127RP1/GDT6527RP1 */
#define PCI_DEVICE_ID_VORTEX_GDT6537RP1 0x112   /* GDT6537RP1 */
#define PCI_DEVICE_ID_VORTEX_GDT6557RP1 0x113   /* GDT6557RP1 */
/* GDT_MPR, RP1 series, narrow/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x11RP1 0x114   /* GDT6111RP1/GDT6511RP1 */
#define PCI_DEVICE_ID_VORTEX_GDT6x21RP1 0x115   /* GDT6121RP1/GDT6521RP1 */
/* GDT_MPR, RP2 series, wide/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x17RP2 0x120   /* GDT6117RP2/GDT6517RP2 */
#define PCI_DEVICE_ID_VORTEX_GDT6x27RP2 0x121   /* GDT6127RP2/GDT6527RP2 */
#define PCI_DEVICE_ID_VORTEX_GDT6537RP2 0x122   /* GDT6537RP2 */
#define PCI_DEVICE_ID_VORTEX_GDT6557RP2 0x123   /* GDT6557RP2 */
/* GDT_MPR, RP2 series, narrow/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x11RP2 0x124   /* GDT6111RP2/GDT6511RP2 */
#define PCI_DEVICE_ID_VORTEX_GDT6x21RP2 0x125   /* GDT6121RP2/GDT6521RP2 */
#endif

/* limits */
#define GDTH_SCRATCH    4096                    /* 4KB scratch buffer */
#define GDTH_MAXCMDS    124
#define GDTH_MAXC_P_L   16                      /* max. cmds per lun */
#define MAXOFFSETS      128
#define MAXHA           8
#define MAXID           8
#define MAXLUN          8
#define MAXBUS          5
#define MAX_HDRIVES     35                      /* max. host drive count */
#define MAX_EVENTS      100                     /* event buffer count */
#define MAXCYLS         1024
#define HEADS           64
#define SECS            32                      /* mapping 64*32 */
#define MEDHEADS        127
#define MEDSECS         63                      /* mapping 127*63 */
#define BIGHEADS        255
#define BIGSECS         63                      /* mapping 255*63 */

/* special command ptr. */
#define UNUSED_CMND     ((Scsi_Cmnd *)-1)
#define INTERNAL_CMND   ((Scsi_Cmnd *)-2)
#define SCREEN_CMND     ((Scsi_Cmnd *)-3)
#define SPECIAL_SCP(p)  (p==UNUSED_CMND || p==INTERNAL_CMND || p==SCREEN_CMND)

/* device types */
#define EMPTY_DTYP      0
#define CACHE_DTYP      1
#define RAW_DTYP        2
#define SIOP_DTYP       3                       /* the SCSI processor */

/* controller services */
#define SCSIRAWSERVICE  3
#define CACHESERVICE    9
#define SCREENSERVICE   11

/* screenservice defines */
#define MSG_INV_HANDLE  -1                      /* special message handle */
#define MSGLEN          16                      /* size of message text */
#define MSG_SIZE        34                      /* size of message structure */
#define MSG_REQUEST     0                       /* async. event: message */

/* cacheservice defines */
#define SECTOR_SIZE     0x200                   /* always 512 bytes per sector */

/* DPMEM constants */
#define IC_HEADER_BYTES 48
#define IC_QUEUE_BYTES  4
#define DPMEM_COMMAND_OFFSET    IC_HEADER_BYTES+IC_QUEUE_BYTES*MAXOFFSETS

/* service commands */
#define GDT_INIT        0                       /* service initialization */
#define GDT_READ        1                       /* read command */
#define GDT_WRITE       2                       /* write command */
#define GDT_INFO        3                       /* information about devices */
#define GDT_FLUSH       4                       /* flush dirty cache buffers */
#define GDT_IOCTL       5                       /* ioctl command */
#define GDT_DEVTYPE     9                       /* additional information */
#define GDT_MOUNT       10                      /* mount cache device */
#define GDT_UNMOUNT     11                      /* unmount cache device */
#define GDT_SET_FEAT    12                      /* set feat. (scatter/gather) */
#define GDT_GET_FEAT    13                      /* get features */
#define GDT_RESERVE     14                      /* reserve dev. to raw service */
#define GDT_WRITE_THR   16                      /* write through */
#define GDT_EXT_INFO    18                      /* extended info */
#define GDT_RESET       19                      /* controller reset */

/* IOCTL command defines */
#define SCSI_CHAN_CNT   5                       /* subfunctions */
#define L_CTRL_PATTERN  0x20000000L
#define CACHE_INFO      4
#define CACHE_CONFIG    5
#define IO_CHANNEL      0x00020000L             /* channels */
#define INVALID_CHANNEL 0x0000ffffL     

/* IOCTLs */
#define GDTIOCTL_MASK       ('J'<<8)
#define GDTIOCTL_GENERAL    (GDTIOCTL_MASK | 0) /* general IOCTL */
#define GDTIOCTL_DRVERS     (GDTIOCTL_MASK | 1) /* get driver version */
#define GDTIOCTL_CTRTYPE    (GDTIOCTL_MASK | 2) /* get controller type */
#define GDTIOCTL_CTRCNT     (GDTIOCTL_MASK | 5) /* get controller count */
#define GDTIOCTL_LOCKDRV    (GDTIOCTL_MASK | 6) /* lock host drive */
#define GDTIOCTL_LOCKCHN    (GDTIOCTL_MASK | 7) /* lock channel */
#define GDTIOCTL_EVENT      (GDTIOCTL_MASK | 8) /* read controller events */

/* service errors */
#define S_OK            1                       /* no error */
#define S_BSY           7                       /* controller busy */
#define S_RAW_SCSI      12                      /* raw serv.: target error */
#define S_RAW_ILL       0xff                    /* raw serv.: illegal */

/* timeout values */
#define INIT_RETRIES    10000                   /* 10000 * 1ms = 10s */
#define INIT_TIMEOUT    100000                  /* 1000 * 1ms = 1s */
#define POLL_TIMEOUT    10000                   /* 10000 * 1ms = 10s */

/* priorities */
#define DEFAULT_PRI     0x20
#define IOCTL_PRI       0x10

/* data directions */
#define DATA_IN         0x01000000L             /* data from target */
#define DATA_OUT        0x00000000L             /* data to target */

/* BMIC registers (EISA controllers) */
#define ID0REG          0x0c80                  /* board ID */
#define EINTENABREG     0x0c89                  /* interrupt enable */
#define SEMA0REG        0x0c8a                  /* command semaphore */
#define SEMA1REG        0x0c8b                  /* status semaphore */
#define LDOORREG        0x0c8d                  /* local doorbell */
#define EDENABREG       0x0c8e                  /* EISA system doorbell enable */
#define EDOORREG        0x0c8f                  /* EISA system doorbell */
#define MAILBOXREG      0x0c90                  /* mailbox reg. (16 bytes) */
#define EISAREG         0x0cc0                  /* EISA configuration */

/* other defines */
#define LINUX_OS        8                       /* used for cache optim. */
#define SCATTER_GATHER  1                       /* s/g feature */
#define GDTH_MAXSG      32                      /* max. s/g elements */
#define SECS32          0x1f                    /* round capacity */
#define BIOS_ID_OFFS    0x10                    /* offset contr. ID in ISABIOS */
#define LOCALBOARD      0                       /* board node always 0 */
#define ASYNCINDEX      0                       /* cmd index async. event */
#define SPEZINDEX       1                       /* cmd index unknown service */
#define GDT_WR_THROUGH  0x100                   /* WRITE_THROUGH supported */

/* typedefs */

#pragma pack(1)

typedef struct {
    char        buffer[GDTH_SCRATCH];           /* scratch buffer */
} gdth_scratch_str;

/* screenservice message */
typedef struct {                               
    ulong       msg_handle;                     /* message handle */
    ulong       msg_len;                        /* size of message */
    ulong       msg_alen;                       /* answer length */
    unchar      msg_answer;                     /* answer flag */
    unchar      msg_ext;                        /* more messages */
    unchar      msg_reserved[2];
    char        msg_text[MSGLEN+2];             /* the message text */
} gdth_msg_str;

/* get channel count IOCTL */
typedef struct {
    ulong       channel_no;                     /* number of channel */
    ulong       drive_cnt;                      /* number of drives */
    unchar      siop_id;                        /* SCSI processor ID */
    unchar      siop_state;                     /* SCSI processor state */ 
} gdth_getch_str;

/* cache info/config IOCTL */
typedef struct {
    ulong       version;                        /* firmware version */
    ushort      state;                          /* cache state (on/off) */
    ushort      strategy;                       /* cache strategy */
    ushort      write_back;                     /* write back state (on/off) */
    ushort      block_size;                     /* cache block size */
} gdth_cpar_str;

typedef struct {
    ulong       csize;                          /* cache size */
    ulong       read_cnt;                       /* read/write counter */
    ulong       write_cnt;
    ulong       tr_hits;                        /* hits */
    ulong       sec_hits;
    ulong       sec_miss;                       /* misses */
} gdth_cstat_str;

typedef struct {
    gdth_cpar_str   cpar;
    gdth_cstat_str  cstat;
} gdth_cinfo_str;

/* scatter/gather element */
typedef struct {
    ulong       sg_ptr;                         /* address */
    ulong       sg_len;                         /* length */
} gdth_sg_str;

/* command structure */
typedef struct {
    ulong       BoardNode;                      /* board node (always 0) */
    ulong       CommandIndex;                   /* command number */
    ushort      OpCode;                         /* the command (READ,..) */
    union {
        struct {
            ushort      DeviceNo;               /* number of cache drive */
            ulong       BlockNo;                /* block number */
            ulong       BlockCnt;               /* block count */
            ulong       DestAddr;               /* dest. addr. (if s/g: -1) */
            ulong       sg_canz;                /* s/g element count */
            gdth_sg_str sg_lst[GDTH_MAXSG];     /* s/g list */
        } cache;                                /* cache service cmd. str. */
        struct {
            ushort      param_size;             /* size of p_param buffer */
            ulong       subfunc;                /* IOCTL function */
            ulong       channel;                /* device */
            ulong       p_param;                /* buffer */
        } ioctl;                                /* IOCTL command structure */
        struct {
            ushort      reserved;
            ulong       msg_handle;             /* message handle */
            ulong       msg_addr;               /* message buffer address */
        } screen;                               /* screen service cmd. str. */
        struct {
            ushort      reserved;
            ulong       direction;              /* data direction */
            ulong       mdisc_time;             /* disc. time (0: no timeout)*/
            ulong       mcon_time;              /* connect time(0: no to.) */
            ulong       sdata;                  /* dest. addr. (if s/g: -1) */
            ulong       sdlen;                  /* data length (bytes) */
            ulong       clen;                   /* SCSI cmd. length(6,10,12) */
            unchar      cmd[12];                /* SCSI command */
            unchar      target;                 /* target ID */
            unchar      lun;                    /* LUN */
            unchar      bus;                    /* SCSI bus number */
            unchar      priority;               /* only 0 used */
            ulong       sense_len;              /* sense data length */
            ulong       sense_data;             /* sense data addr. */
            struct raw  *link_p;                /* linked cmds (not supp.) */
            ulong       sg_ranz;                /* s/g element count */
            gdth_sg_str sg_lst[GDTH_MAXSG];     /* s/g list */
        } raw;                                  /* raw service cmd. struct. */
    } u;
    /* additional variables */
    unchar      Service;                        /* controller service */
    ushort      Status;                         /* command result */
    ulong       Info;                           /* additional information */
    Scsi_Cmnd   *RequestBuffer;                 /* request buffer */
} gdth_cmd_str;

/* controller event structure */
#define ES_ASYNC    1
#define ES_DRIVER   2
#define ES_TEST     3
#define ES_SYNC     4
typedef struct {
    ushort                  size;               /* size of structure */
    union {
        char                stream[16];
        struct {
            ushort          ionode;
            ushort          service;
            ulong           index;
        } driver;
        struct {
            ushort          ionode;
            ushort          service;
            ushort          status;
            ulong           info;
            unchar          scsi_coord[3];
        } async;
        struct {
            ushort          ionode;
            ushort          service;
            ushort          status;
            ulong           info;
            ushort          hostdrive;
            unchar          scsi_coord[3];
            unchar          sense_key;
        } sync;
        struct {
            ulong           l1, l2, l3, l4;
        } test;
    } eu;
} gdth_evt_data;

typedef struct {
    ulong           first_stamp;
    ulong           last_stamp;
    ushort          same_count;
    ushort          event_source;
    ushort          event_idx;
    unchar          application;
    unchar          reserved;
    gdth_evt_data   event_data;
} gdth_evt_str;


/* DPRAM structures */

/* interface area ISA/PCI */
typedef struct {
    unchar              S_Cmd_Indx;             /* special command */
    unchar volatile     S_Status;               /* status special command */
    ushort              reserved1;
    ulong               S_Info[4];              /* add. info special command */
    unchar volatile     Sema0;                  /* command semaphore */
    unchar              reserved2[3];
    unchar              Cmd_Index;              /* command number */
    unchar              reserved3[3];
    ushort volatile     Status;                 /* command status */
    ushort              Service;                /* service(for async.events) */
    ulong               Info[2];                /* additional info */
    struct {
        ushort          offset;                 /* command offs. in the DPRAM*/
        ushort          serv_id;                /* service */
    } comm_queue[MAXOFFSETS];                   /* command queue */
    ulong               bios_reserved[2];
    unchar              gdt_dpr_cmd[1];         /* commands */
} gdt_dpr_if;

/* SRAM structure PCI controllers */
typedef struct {
    ulong       magic;                          /* controller ID from BIOS */
    ushort      need_deinit;                    /* switch betw. BIOS/driver */
    unchar      switch_support;                 /* see need_deinit */
    unchar      padding[9];
    unchar      os_used[16];                    /* OS code per service */
    unchar      unused[28];
    unchar      fw_magic;                       /* contr. ID from firmware */
} gdt_pci_sram;

/* SRAM structure EISA controllers (but NOT GDT3000/3020) */
typedef struct {
    unchar      os_used[16];                    /* OS code per service */
    ushort      need_deinit;                    /* switch betw. BIOS/driver */
    unchar      switch_support;                 /* see need_deinit */
    unchar      padding;
} gdt_eisa_sram;


/* DPRAM ISA controllers */
typedef struct {
    union {
        struct {
            unchar      bios_used[0x3c00-32];   /* 15KB - 32Bytes BIOS */
            ulong       magic;                  /* controller (EISA) ID */
            ushort      need_deinit;            /* switch betw. BIOS/driver */
            unchar      switch_support;         /* see need_deinit */
            unchar      padding[9];
            unchar      os_used[16];            /* OS code per service */
        } dp_sram;
        unchar          bios_area[0x4000];      /* 16KB reserved for BIOS */
    } bu;
    union {
        gdt_dpr_if      ic;                     /* interface area */
        unchar          if_area[0x3000];        /* 12KB for interface */
    } u;
    struct {
        unchar          memlock;                /* write protection DPRAM */
        unchar          event;                  /* release event */
        unchar          irqen;                  /* board interrupts enable */
        unchar          irqdel;                 /* acknowledge board int. */
        unchar volatile Sema1;                  /* status semaphore */
        unchar          rq;                     /* IRQ/DRQ configuration */
    } io;
} gdt2_dpram_str;

/* DPRAM PCI controllers */
typedef struct {
    union {
        gdt_dpr_if      ic;                     /* interface area */
        unchar          if_area[0xff0-sizeof(gdt_pci_sram)];
    } u;
    gdt_pci_sram        gdt6sr;                 /* SRAM structure */
    struct {
        unchar          unused0[1];
        unchar volatile Sema1;                  /* command semaphore */
        unchar          unused1[3];
        unchar          irqen;                  /* board interrupts enable */
        unchar          unused2[2];
        unchar          event;                  /* release event */
        unchar          unused3[3];
        unchar          irqdel;                 /* acknowledge board int. */
        unchar          unused4[3];
    } io;
} gdt6_dpram_str;

/* PLX register structure (new PCI controllers) */
typedef struct {
    unchar              cfg_reg;        /* DPRAM cfg.(2:below 1MB,0:anywhere)*/
    unchar              unused1[0x3f];
    unchar volatile     sema0_reg;              /* command semaphore */
    unchar volatile     sema1_reg;              /* status semaphore */
    unchar              unused2[2];
    ushort volatile     status;                 /* command status */
    ushort              service;                /* service */
    ulong               info[2];                /* additional info */
    unchar              unused3[0x10];
    unchar              ldoor_reg;              /* PCI to local doorbell */
    unchar              unused4[3];
    unchar volatile     edoor_reg;              /* local to PCI doorbell */
    unchar              unused5[3];
    unchar              control0;               /* control0 register(unused) */
    unchar              control1;               /* board interrupts enable */
    unchar              unused6[0x16];
} gdt6c_plx_regs;

/* DPRAM new PCI controllers */
typedef struct {
    union {
        gdt_dpr_if      ic;                     /* interface area */
        unchar          if_area[0x4000-sizeof(gdt_pci_sram)];
    } u;
    gdt_pci_sram        gdt6sr;                 /* SRAM structure */
} gdt6c_dpram_str;

/* i960 register structure (PCI MPR controllers) */
typedef struct {
    unchar              unused1[16];
    unchar volatile     sema0_reg;              /* command semaphore */
    unchar              unused2;
    unchar volatile     sema1_reg;              /* status semaphore */
    unchar              unused3;
    ushort volatile     status;                 /* command status */
    ushort              service;                /* service */
    ulong               info[2];                /* additional info */
    unchar              ldoor_reg;              /* PCI to local doorbell */
    unchar              unused4[11];
    unchar volatile     edoor_reg;              /* local to PCI doorbell */
    unchar              unused5[7];
    unchar              edoor_en_reg;           /* board interrupts enable */
    unchar              unused6[27];
    ulong               unused7[1004];          /* size: 4 KB */
} gdt6m_i960_regs;

/* DPRAM PCI MPR controllers */
typedef struct {
    gdt6m_i960_regs     i960r;                  /* 4KB i960 registers */
    union {
        gdt_dpr_if      ic;                     /* interface area */
        unchar          if_area[0x3000-sizeof(gdt_pci_sram)];
    } u;
    gdt_pci_sram        gdt6sr;                 /* SRAM structure */
} gdt6m_dpram_str;


/* PCI resources */
typedef struct {
    ushort      device_id;                      /* device ID (0,..,9) */
    unchar      bus;                            /* PCI bus */
    unchar      device_fn;                      /* PCI device/function no. */
    ulong       dpmem;                          /* DPRAM address */
    ulong       io;                             /* IO address */
    ulong       io_mm;                          /* IO address mem. mapped */
    ulong       bios;                           /* BIOS address */
    unchar      irq;                            /* IRQ */
} gdth_pci_str;


/* controller information structure */
typedef struct {
    unchar              bus_cnt;                /* SCSI bus count */
    unchar              type;                   /* controller class */
    ushort              raw_feat;               /* feat. raw service (s/g,..) */
    ulong               stype;                  /* controller subtype */
    ushort              cache_feat;             /* feat. cache serv. (s/g,..) */
    ushort		bmic;			/* BMIC address (EISA) */
    void               	*brd;	                /* DPRAM address */
    ulong               brd_phys;               /* slot number/BIOS address */
    gdt6c_plx_regs      *plx;                   /* PLX regs (new PCI contr.) */
    gdth_cmd_str        *pccb;                  /* address command structure */
    gdth_scratch_str    *pscratch;
    unchar              irq;                    /* IRQ */
    unchar              drq;                    /* DRQ (ISA controllers) */
    ushort              status;                 /* command status */
    ulong               info;
    ulong               info2;                  /* additional info */
    Scsi_Cmnd           *req_first;             /* top of request queue */
    struct {
        unchar          type;                   /* device type */
        unchar          heads;                  /* mapping */
        unchar          secs;
        unchar          lock;                   /* drive locked ? (hot plug) */
        ushort          hostdrive;              /* host drive number */
        ushort          devtype;                /* further information */
        ulong           size;                   /* capacity */
    } id[MAXBUS][MAXID];         
    ushort              cmd_cnt;                /* command count in DPRAM */
    ushort              cmd_len;                /* length of actual command */
    ushort              cmd_offs_dpmem;         /* actual offset in DPRAM */
    ushort              ic_all_size;            /* sizeof DPRAM interf. area */
    unchar              reserved;
    unchar              mode;                   /* information from /proc */
    ushort              param_size;
    gdth_cpar_str       cpar;                   /* controller cache par. */
} gdth_ha_str;

/* structure for scsi_register(), SCSI bus != 0 */
typedef struct {
    ushort      hanum;
    ushort      busnum;
} gdth_num_str;

/* structure for scsi_register() */
typedef struct {
    gdth_num_str        numext;                 /* must be the first element */
    gdth_ha_str         haext;
    gdth_cmd_str        cmdext;
    gdth_scratch_str    dmaext;
} gdth_ext_str;


/* INQUIRY data format */
typedef struct {
    unchar      type_qual;
    unchar      modif_rmb;
    unchar      version;
    unchar      resp_aenc;
    unchar      add_length;
    unchar      reserved1;
    unchar      reserved2;
    unchar      misc;
    unchar      vendor[8];
    unchar      product[16];
    unchar      revision[4];
} gdth_inq_data;

/* READ_CAPACITY data format */
typedef struct {
    ulong       last_block_no;
    ulong       block_length;
} gdth_rdcap_data;

/* REQUEST_SENSE data format */
typedef struct {
    unchar      errorcode;
    unchar      segno;
    unchar      key;
    ulong       info;
    unchar      add_length;
    ulong       cmd_info;
    unchar      adsc;
    unchar      adsq;
    unchar      fruc;
    unchar      key_spec[3];
} gdth_sense_data;

/* MODE_SENSE data format */
typedef struct {
    struct {
        unchar  data_length;
        unchar  med_type;
        unchar  dev_par;
        unchar  bd_length;
    } hd;
    struct {
        unchar  dens_code;
        unchar  block_count[3];
        unchar  reserved;
        unchar  block_length[3];
    } bd;
} gdth_modep_data;

typedef struct {
    ulong       b[10];                          /* 32 bit compiler ! */
} gdth_stackframe;

#pragma pack()

/* function prototyping */

int gdth_detect(Scsi_Host_Template *);
int gdth_release(struct Scsi_Host *);
int gdth_command(Scsi_Cmnd *);
int gdth_queuecommand(Scsi_Cmnd *,void (*done)(Scsi_Cmnd *));
int gdth_abort(Scsi_Cmnd *);
#if LINUX_VERSION_CODE >= 0x010346
int gdth_reset(Scsi_Cmnd *, unsigned int reset_flags);
#else
int gdth_reset(Scsi_Cmnd *);
#endif
const char *gdth_info(struct Scsi_Host *);


int gdth_bios_param(Disk *,kdev_t,int *);
extern struct proc_dir_entry proc_scsi_gdth;
int gdth_proc_info(char *,char **,off_t,int,int,int);
#define GDTH { proc_dir:          &proc_scsi_gdth,                     \
               proc_info:         gdth_proc_info,                      \
               name:              "GDT SCSI Disk Array Controller",    \
               detect:            gdth_detect,                         \
               release:           gdth_release,                        \
               info:              gdth_info,                           \
               command:           gdth_command,                        \
               queuecommand:      gdth_queuecommand,                   \
               abort:             gdth_abort,                          \
               reset:             gdth_reset,                          \
               bios_param:        gdth_bios_param,                     \
               can_queue:         GDTH_MAXCMDS,                        \
               this_id:           -1,                                  \
               sg_tablesize:      GDTH_MAXSG,                          \
               cmd_per_lun:       GDTH_MAXC_P_L,                       \
               unchecked_isa_dma: 1,                                   \
               use_clustering:    ENABLE_CLUSTERING}

#endif


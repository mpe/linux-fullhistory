/* qlogicpti.h: Performance Technologies QlogicISP sbus card defines.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 */

#ifndef _QLOGICPTI_H
#define _QLOGICPTI_H

struct qlogicpti_regs {
	/* SBUS control registers. */
	volatile unsigned short  sbus_idlow;      /* SBUS ID, low bytes             */
	volatile unsigned short  sbus_idhi;       /* SBUS ID, high bytes            */
	volatile unsigned short  sbus_cfg0;       /* SBUS Config reg zero           */
	volatile unsigned short  sbus_cfg1;       /* SBUS Config reg one            */
	volatile unsigned short  sbus_ctrl;       /* SBUS Control reg               */
	volatile unsigned short  sbus_stat;       /* SBUS Status reg                */
	volatile unsigned short  sbus_semaphore;  /* SBUS Semaphore, p/v this...    */
	unsigned char _unused0[18];               /* Reserved...                    */

	/* Command DVMA control registers. */
	volatile unsigned short  cmd_dma_cfg;     /* CMD DVMA Config reg            */
	volatile unsigned short  cmd_dma_ctrl;    /* CMD DVMA Control reg           */
	volatile unsigned short  cmd_dma_stat;    /* CMD DVMA Status reg            */
	volatile unsigned short  cmd_dma_fstat;   /* CMD DVMA FIFO Status reg       */
	volatile unsigned short  cmd_dma_cnt;     /* CMD DVMA Counter reg           */
	unsigned short _unused1;                  /* Reserved...                    */
	volatile unsigned short  cmd_dma_alow;    /* CMD DVMA Address low bytes     */
	volatile unsigned short  cmd_dma_ahi;     /* CMD DVMA Address high bytes    */
	unsigned char _unused2[16];               /* Reserved...                    */

	/* Data DVMA control registers. */
	volatile unsigned short  data_dma_cfg;    /* DATA DVMA Config reg           */
	volatile unsigned short  data_dma_ctrl;   /* DATA DVMA Control reg          */
	volatile unsigned short  data_dma_stat;   /* DATA DVMA Status reg           */
	volatile unsigned short  data_dma_fstat;  /* DATA DVMA FIFO Status reg      */
	volatile unsigned short  data_dma_clo;    /* DATA DVMA Counter low bytes    */
	volatile unsigned short  data_dma_chi;    /* DATA DVMA Counter high bytes   */
	volatile unsigned short  data_dma_alow;   /* DATA DVMA Address low bytes    */
	volatile unsigned short  data_dma_ahi;    /* DATA DVMA Address high bytes   */
	unsigned char _unused3[16];               /* Reserved...                    */

	/* Data FIFO registers. */
	volatile unsigned short  fcmd;            /* FIFO Command port              */
	volatile unsigned short  fdata;           /* FIFO Data port                 */
	unsigned char _unused4[28];               /* Reserved...                    */

	/* Mailboxen. */
	volatile unsigned short  mbox0;           /* MailBOX 0                      */
	volatile unsigned short  mbox1;           /* MailBOX 1                      */
	volatile unsigned short  mbox2;           /* MailBOX 2                      */
	volatile unsigned short  mbox3;           /* MailBOX 3                      */
	volatile unsigned short  mbox4;           /* MailBOX 4                      */
	volatile unsigned short  mbox5;           /* MailBOX 5                      */
	unsigned char _unused5[372];              /* Reserved...                    */

	/* Scsi processor registers. */
	volatile unsigned short  cpu_id;          /* PART ID                        */
	volatile unsigned short  cpu_cfg1;        /* Config reg 1                   */
	volatile unsigned short  cpu_cfg2;        /* Config reg 2                   */
	volatile unsigned short  cpu_cfg3;        /* Config reg 3                   */
	unsigned char _unused6[4];                /* Reserved...                    */
	volatile unsigned short  cpu_pc;          /* Program Counter                */
	unsigned short _unused7;                  /* Reserved...                    */
	volatile unsigned short  cpu_rpc;         /* Return Program Counter         */
	unsigned short _unused8;                  /* Reserved...                    */
	volatile unsigned short  cpu_cmd;         /* Command                        */
	unsigned short _unused9;                  /* Reserved...                    */
	volatile unsigned short  cpu_irq;         /* IRQ status                     */
	unsigned short _unused10;                 /* Reserved...                    */
	volatile unsigned short  cpu_seq;         /* Sequence reg                   */
	volatile unsigned short  cpu_gerr;        /* Gross Error reg (ESP lineage?) */
	volatile unsigned short  cpu_exc;         /* Enable Exception reg           */
	unsigned short _unused11;                 /* Reserved...                    */
	volatile unsigned short  cpu_oride;       /* Override reg                   */
	unsigned short _unused12;                 /* Reserved...                    */
	volatile unsigned short  cpu_lbase;       /* Literal Base reg               */
	unsigned short _unused13;                 /* Reserved...                    */
	volatile unsigned short  cpu_uflags;      /* User Flags reg                 */
	unsigned short _unused14;                 /* Reserved...                    */
	volatile unsigned short  cpu_uexc;        /* User Exception reg             */
	unsigned short _unused15;                 /* Reserved...                    */
	volatile unsigned short  cpu_bkpt;        /* Breakpoint reg                 */
	unsigned short _unused16[5];              /* Reserved...                    */
	volatile unsigned short  cpu_sid;         /* SCSI ID reg                    */
	volatile unsigned short  cpu_dcfg1;       /* Device Config 1                */
	volatile unsigned short  cpu_dcfg2;       /* Device Config 2                */
	unsigned short _unused17;                 /* Reserved...                    */
	volatile unsigned short  cpu_pptr;        /* Phase Pointer                  */
	unsigned short _unused18;                 /* Reserved...                    */
	volatile unsigned short  cpu_bptr;        /* Buffer Pointer                 */
	unsigned short _unused19;                 /* Reserved...                    */
	volatile unsigned short  cpu_bcnt;        /* Buffer Counter                 */
	volatile unsigned short  cpu_buf;         /* Buffer itself                  */
	volatile unsigned short  cpu_bbyte;       /* Buffer Byte                    */
	volatile unsigned short  cpu_bword;       /* Buffer Word                    */
	unsigned short _unused20;                 /* Reserved...                    */
	volatile unsigned short  cpu_fifo;        /* FIFO                           */
	volatile unsigned short  cpu_fstat;       /* FIFO Status                    */
	volatile unsigned short  cpu_ftop;        /* Top of FIFO                    */
	volatile unsigned short  cpu_fbottom;     /* Bottom of FIFO                 */
	unsigned short _unused21;                 /* Reserved...                    */
	volatile unsigned short  cpu_treg;        /* Transfer reg                   */
	unsigned short _unused22;                 /* Reserved...                    */
	volatile unsigned short  cpu_clo;         /* Transfer Count low bytes       */
	volatile unsigned short  cpu_chi;         /* Transfer Count high bytes      */
	volatile unsigned short  cpu_cntlo;       /* Transfer Counter low bytes     */
	volatile unsigned short  cpu_cnthi;       /* Transfer Counter low bytes     */
	volatile unsigned short  cpu_adata;       /* Arbitration Data               */
	volatile unsigned short  cpu_pctrl;       /* Pin Control                    */
	volatile unsigned short  cpu_pdata;       /* Pin Data                       */
	volatile unsigned short  cpu_pdiff;       /* Differential Pins              */
	unsigned char _unused23[392];             /* Reserved...                    */

	/* RISC processor registers. */
	volatile unsigned short  risc_a;          /* Accumulator                    */
	volatile unsigned short  risc_r[15];      /* General Purpose Registers      */
	volatile unsigned short  risc_psr;        /* Processor Status Register      */
	volatile unsigned short  risc_ivec;       /* Interrupt Vector               */
	volatile unsigned short  risc_pcr;        /* Processor Control Register     */
	volatile unsigned short  risc_raddr0;     /* RAM Addr reg 0                 */
	volatile unsigned short  risc_raddr1;     /* RAM Addr reg 1                 */
	volatile unsigned short  risc_lcr;        /* Loop Counter reg               */
	volatile unsigned short  risc_pc;         /* Program Counter                */
	volatile unsigned short  risc_mtreg;      /* Memory Timing reg              */
	volatile unsigned short  risc_embreg;     /* External Memory Boundry reg    */
	volatile unsigned short  risc_sp;         /* Stack Pointer                  */
	volatile unsigned short  risc_hrev;       /* Hardware Revision              */
	unsigned char _unused24[10];              /* Reserved...                    */

	/* Generic control/command registers. */
	volatile unsigned short  hcctrl;          /* Host cmd/control reg           */
	volatile unsigned short  pbkpt0;          /* Processor Breakpoint 0         */
	volatile unsigned short  pbkpt1;          /* Processor Breakpoint 1         */
	volatile unsigned short  tcntrl;          /* Test Control reg               */
	volatile unsigned short  tmreg;           /* Test Mode reg                  */
};

#define MAX_TARGETS	16
#define MAX_LUNS	8

/* With the qlogic interface, every queue slot can hold a SCSI
 * command with up to 4 scatter/gather entries.  If we need more
 * than 4 entries, continuation entries can be used that hold
 * another 7 entries each.  Unlike for other drivers, this means
 * that the maximum number of scatter/gather entries we can
 * support at any given time is a function of the number of queue
 * slots available.  That is, host->can_queue and host->sg_tablesize
 * are dynamic and _not_ independent.  This all works fine because
 * requests are queued serially and the scatter/gather limit is
 * determined for each queue request anew.
 */
#define QLOGICISP_REQ_QUEUE_LEN	255	/* must be power of two - 1 */
#define QLOGICISP_MAX_SG(ql)	(4 + ((ql) > 0) ? 7*((ql) - 1) : 0)

#ifndef NULL
#define NULL (0)
#endif

int qlogicpti_detect(Scsi_Host_Template *);
int qlogicpti_release(struct Scsi_Host *);
const char * qlogicpti_info(struct Scsi_Host *);
int qlogicpti_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
int qlogicpti_abort(Scsi_Cmnd *);
int qlogicpti_reset(Scsi_Cmnd *, unsigned int);

extern struct proc_dir_entry proc_scsi_qlogicpti;

/* mailbox command complete status codes */
#define MBOX_COMMAND_COMPLETE		0x4000
#define INVALID_COMMAND			0x4001
#define HOST_INTERFACE_ERROR		0x4002
#define TEST_FAILED			0x4003
#define COMMAND_ERROR			0x4005
#define COMMAND_PARAM_ERROR		0x4006

/* async event status codes */
#define ASYNC_SCSI_BUS_RESET		0x8001
#define SYSTEM_ERROR			0x8002
#define REQUEST_TRANSFER_ERROR		0x8003
#define RESPONSE_TRANSFER_ERROR		0x8004
#define REQUEST_QUEUE_WAKEUP		0x8005
#define EXECUTION_TIMEOUT_RESET		0x8006

/* Am I fucking pedantic or what? */
struct Entry_header {
#ifdef __BIG_ENDIAN
	u_char	entry_cnt;
	u_char	entry_type;
	u_char	flags;
	u_char	sys_def_1;
#else /* __LITTLE_ENDIAN */
	u_char	entry_type;
	u_char	entry_cnt;
	u_char	sys_def_1;
	u_char	flags;
#endif
};

/* entry header type commands */
#define ENTRY_COMMAND		1
#define ENTRY_CONTINUATION	2
#define ENTRY_STATUS		3
#define ENTRY_MARKER		4
#define ENTRY_EXTENDED_COMMAND	5

/* entry header flag definitions */
#define EFLAG_CONTINUATION	1
#define EFLAG_BUSY		2
#define EFLAG_BAD_HEADER	4
#define EFLAG_BAD_PAYLOAD	8

struct dataseg {
	u_int			d_base;
	u_int			d_count;
};

struct Command_Entry {
	struct Entry_header	hdr;
	u_int			handle;
#ifdef __BIG_ENDIAN
	u_char			target_id;
	u_char			target_lun;
#else /* __LITTLE_ENDIAN */
	u_char			target_lun;
	u_char			target_id;
#endif
	u_short			cdb_length;
	u_short			control_flags;
	u_short			rsvd;
	u_short			time_out;
	u_short			segment_cnt;
	u_char			cdb[12];
	struct dataseg		dataseg[4];
};

/* command entry control flag definitions */
#define CFLAG_NODISC		0x01
#define CFLAG_HEAD_TAG		0x02
#define CFLAG_ORDERED_TAG	0x04
#define CFLAG_SIMPLE_TAG	0x08
#define CFLAG_TAR_RTN		0x10
#define CFLAG_READ		0x20
#define CFLAG_WRITE		0x40

struct Ext_Command_Entry {
	struct Entry_header	hdr;
	u_int			handle;
#ifdef __BIG_ENDIAN
	u_char			target_id;
	u_char			target_lun;
#else /* __LITTLE_ENDIAN */
	u_char			target_lun;
	u_char			target_id;
#endif
	u_short			cdb_length;
	u_short			control_flags;
	u_short			rsvd;
	u_short			time_out;
	u_short			segment_cnt;
	u_char			cdb[44];
};

struct Continuation_Entry {
	struct Entry_header	hdr;
	u_int			reserved;
	struct dataseg		dataseg[7];
};

struct Marker_Entry {
	struct Entry_header	hdr;
	u_int			reserved;
#ifdef __BIG_ENDIAN
	u_char			target_id;
	u_char			target_lun;
#else /* __LITTLE_ENDIAN */
	u_char			target_lun;
	u_char			target_id;
#endif
#ifdef __BIG_ENDIAN
	u_char			rsvd;
	u_char			modifier;
#else /* __LITTLE_ENDIAN */
	u_char			modifier;
	u_char			rsvd;
#endif
	u_char			rsvds[52];
};

/* marker entry modifier definitions */
#define SYNC_DEVICE	0
#define SYNC_TARGET	1
#define SYNC_ALL	2

struct Status_Entry {
	struct Entry_header	hdr;
	u_int			handle;
	u_short			scsi_status;
	u_short			completion_status;
	u_short			state_flags;
	u_short			status_flags;
	u_short			time;
	u_short			req_sense_len;
	u_int			residual;
	u_char			rsvd[8];
	u_char			req_sense_data[32];
};

/* status entry completion status definitions */
#define CS_COMPLETE			0x0000
#define CS_INCOMPLETE			0x0001
#define CS_DMA_ERROR			0x0002
#define CS_TRANSPORT_ERROR		0x0003
#define CS_RESET_OCCURRED		0x0004
#define CS_ABORTED			0x0005
#define CS_TIMEOUT			0x0006
#define CS_DATA_OVERRUN			0x0007
#define CS_COMMAND_OVERRUN		0x0008
#define CS_STATUS_OVERRUN		0x0009
#define CS_BAD_MESSAGE			0x000a
#define CS_NO_MESSAGE_OUT		0x000b
#define CS_EXT_ID_FAILED		0x000c
#define CS_IDE_MSG_FAILED		0x000d
#define CS_ABORT_MSG_FAILED		0x000e
#define CS_REJECT_MSG_FAILED		0x000f
#define CS_NOP_MSG_FAILED		0x0010
#define CS_PARITY_ERROR_MSG_FAILED	0x0011
#define CS_DEVICE_RESET_MSG_FAILED	0x0012
#define CS_ID_MSG_FAILED		0x0013
#define CS_UNEXP_BUS_FREE		0x0014
#define CS_DATA_UNDERRUN		0x0015
#define CS_BUS_RESET			0x001c

/* status entry state flag definitions */
#define SF_GOT_BUS			0x0100
#define SF_GOT_TARGET			0x0200
#define SF_SENT_CDB			0x0400
#define SF_TRANSFERRED_DATA		0x0800
#define SF_GOT_STATUS			0x1000
#define SF_GOT_SENSE			0x2000

/* status entry status flag definitions */
#define STF_DISCONNECT			0x0001
#define STF_SYNCHRONOUS			0x0002
#define STF_PARITY_ERROR		0x0004
#define STF_BUS_RESET			0x0008
#define STF_DEVICE_RESET		0x0010
#define STF_ABORTED			0x0020
#define STF_TIMEOUT			0x0040
#define STF_NEGOTIATION			0x0080

/* mailbox commands */
#define MBOX_NO_OP			0x0000
#define MBOX_LOAD_RAM			0x0001
#define MBOX_EXEC_FIRMWARE		0x0002
#define MBOX_DUMP_RAM			0x0003
#define MBOX_WRITE_RAM_WORD		0x0004
#define MBOX_READ_RAM_WORD		0x0005
#define MBOX_MAILBOX_REG_TEST		0x0006
#define MBOX_VERIFY_CHECKSUM		0x0007
#define MBOX_ABOUT_FIRMWARE		0x0008
#define MBOX_CHECK_FIRMWARE		0x000e
#define MBOX_INIT_REQ_QUEUE		0x0010
#define MBOX_INIT_RES_QUEUE		0x0011
#define MBOX_EXECUTE_IOCB		0x0012
#define MBOX_WAKE_UP			0x0013
#define MBOX_STOP_FIRMWARE		0x0014
#define MBOX_ABORT			0x0015
#define MBOX_ABORT_DEVICE		0x0016
#define MBOX_ABORT_TARGET		0x0017
#define MBOX_BUS_RESET			0x0018
#define MBOX_STOP_QUEUE			0x0019
#define MBOX_START_QUEUE		0x001a
#define MBOX_SINGLE_STEP_QUEUE		0x001b
#define MBOX_ABORT_QUEUE		0x001c
#define MBOX_GET_DEV_QUEUE_STATUS	0x001d
#define MBOX_GET_FIRMWARE_STATUS	0x001f
#define MBOX_GET_INIT_SCSI_ID		0x0020
#define MBOX_GET_SELECT_TIMEOUT		0x0021
#define MBOX_GET_RETRY_COUNT		0x0022
#define MBOX_GET_TAG_AGE_LIMIT		0x0023
#define MBOX_GET_CLOCK_RATE		0x0024
#define MBOX_GET_ACT_NEG_STATE		0x0025
#define MBOX_GET_ASYNC_DATA_SETUP_TIME	0x0026
#define MBOX_GET_SBUS_PARAMS		0x0027
#define MBOX_GET_TARGET_PARAMS		0x0028
#define MBOX_GET_DEV_QUEUE_PARAMS	0x0029
#define MBOX_SET_INIT_SCSI_ID		0x0030
#define MBOX_SET_SELECT_TIMEOUT		0x0031
#define MBOX_SET_RETRY_COUNT		0x0032
#define MBOX_SET_TAG_AGE_LIMIT		0x0033
#define MBOX_SET_CLOCK_RATE		0x0034
#define MBOX_SET_ACTIVE_NEG_STATE	0x0035
#define MBOX_SET_ASYNC_DATA_SETUP_TIME	0x0036
#define MBOX_SET_SBUS_CONTROL_PARAMS	0x0037
#define MBOX_SET_TARGET_PARAMS		0x0038
#define MBOX_SET_DEV_QUEUE_PARAMS	0x0039

struct host_param {
	u_short		initiator_scsi_id;
	u_short		bus_reset_delay;
	u_short		retry_count;
	u_short		retry_delay;
	u_short		async_data_setup_time;
	u_short		req_ack_active_negation;
	u_short		data_line_active_negation;
	u_short		data_dma_burst_enable;
	u_short		command_dma_burst_enable;
	u_short		tag_aging;
	u_short		selection_timeout;
	u_short		max_queue_depth;
};

/*
 * Device Flags:
 *
 * Bit  Name
 * ---------
 *  7   Disconnect Privilege
 *  6   Parity Checking
 *  5   Wide Data Transfers
 *  4   Synchronous Data Transfers
 *  3   Tagged Queuing
 *  2   Automatic Request Sense
 *  1   Stop Queue on Check Condition
 *  0   Renegotiate on Error
 */

struct dev_param {
	u_short		device_flags;
	u_short		execution_throttle;
	u_short		synchronous_period;
	u_short		synchronous_offset;
	u_short		device_enable;
	u_short		reserved; /* pad */
};

/*
 * The result queue can be quite a bit smaller since continuation entries
 * do not show up there:
 */
#define RES_QUEUE_LEN		255	/* Must be power of two - 1 */
#define QUEUE_ENTRY_LEN		64

#define NEXT_REQ_PTR(wheee)   (((wheee) + 1) & QLOGICISP_REQ_QUEUE_LEN)
#define NEXT_RES_PTR(wheee)   (((wheee) + 1) & RES_QUEUE_LEN)
#define PREV_REQ_PTR(wheee)   (((wheee) - 1) & QLOGICISP_REQ_QUEUE_LEN)
#define PREV_RES_PTR(wheee)   (((wheee) - 1) & RES_QUEUE_LEN)

struct pti_queue_entry {
	char __opaque[QUEUE_ENTRY_LEN];
};

/* Software state for the driver. */
struct qlogicpti {
	/* These are the hot elements in the cache, so they come first. */
	struct qlogicpti         *next;                 /* Next active adapter        */
	struct qlogicpti_regs    *qregs;                /* Adapter registers          */
	u_int	                  req_in_ptr;		/* index of next request slot */
	u_int	                  res_out_ptr;		/* index of next result slot  */
	struct pti_queue_entry   *res_cpu;              /* Ptr to RESPONSE bufs (CPU) */
	__u32                     res_dvma;             /* Ptr to RESPONSE bufs (DVMA)*/
	struct pti_queue_entry   *req_cpu;              /* Ptr to REQUEST bufs (CPU)  */
	__u32                     req_dvma;             /* Ptr to REQUEST bufs (DVMA) */
	int                       cmd_count[MAX_TARGETS];
	unsigned long             tag_ages[MAX_TARGETS];
	long	                  send_marker;		/* must we send a marker?     */

	/* The rest of the elements are unimportant for performance. */
	u_char	                  fware_majrev, fware_minrev;
	struct Scsi_Host         *qhost;
	struct linux_sbus_device *qdev;
	int                       qpti_id;
	int                       scsi_id;
	int                       prom_node;
	char                      prom_name[64];
	int                       irq;
	char                      differential, ultra;
	unsigned char             bursts;
	struct	host_param        host_param;
	struct	dev_param         dev_param[MAX_TARGETS];

	volatile unsigned char   *sreg;
#define SREG_TPOWER               0x80   /* State of termpwr           */
#define SREG_FUSE                 0x40   /* State of on board fuse     */
#define SREG_PDISAB               0x20   /* Disable state for power on */
#define SREG_DSENSE               0x10   /* Sense for differential     */
#define SREG_IMASK                0x0c   /* Interrupt level            */
#define SREG_SPMASK               0x03   /* Mask for switch pack       */
	unsigned char             swsreg;

#if 0
	char	res[RES_QUEUE_LEN+1][QUEUE_ENTRY_LEN];
	char	req[QLOGICISP_REQ_QUEUE_LEN+1][QUEUE_ENTRY_LEN];
#endif
};

/* How to twiddle them bits... */

/* SBUS config register zero. */
#define SBUS_CFG0_HREVMASK      0x000f      /* To get the revision              */

/* SBUS config register one. */
#define SBUS_CFG1_EPAR          0x0100      /* Enable parity checking           */
#define SBUS_CFG1_FMASK         0x00f0      /* Forth code cycle mask            */
#define SBUS_CFG1_BENAB         0x0004      /* Burst dvma enable                */
#define SBUS_CFG1_B64           0x0003      /* Enable 64byte bursts             */
#define SBUS_CFG1_B32           0x0002      /* Enable 32byte bursts             */
#define SBUS_CFG1_B16           0x0001      /* Enable 16byte bursts             */
#define SBUS_CFG1_B8            0x0008      /* Enable 8byte bursts              */

/* SBUS control register */
#define SBUS_CTRL_EDIRQ         0x0020      /* Enable Data DVMA Interrupts      */
#define SBUS_CTRL_ECIRQ         0x0010      /* Enable Command DVMA Interrupts   */
#define SBUS_CTRL_ESIRQ         0x0008      /* Enable SCSI Processor Interrupts */
#define SBUS_CTRL_ERIRQ         0x0004      /* Enable RISC Processor Interrupts */
#define SBUS_CTRL_GENAB         0x0002      /* Global Interrupt Enable          */
#define SBUS_CTRL_RESET         0x0001      /* Soft Reset                       */

/* SBUS status register */
#define SBUS_STAT_DINT          0x0020      /* Data DVMA IRQ pending            */
#define SBUS_STAT_CINT          0x0010      /* Command DVMA IRQ pending         */
#define SBUS_STAT_SINT          0x0008      /* SCSI Processor IRQ pending       */
#define SBUS_STAT_RINT          0x0004      /* RISC Processor IRQ pending       */
#define SBUS_STAT_GINT          0x0002      /* Global IRQ pending               */

/* SBUS semaphore register */
#define SBUS_SEMAPHORE_STAT     0x0002      /* Semaphore status bit             */
#define SBUS_SEMAPHORE_LCK      0x0001      /* Semaphore lock bit               */

/* DVMA config register */
#define DMA_CFG_DVMAENAB        0x0008      /* Enable scsi cpu --> dma data     */
#define DMA_CFG_EIRQ            0x0004      /* Enable interrupts to risc cpu    */
#define DMA_CFG_EBURST          0x0002      /* Enable sbus dvma bursts          */
#define DMA_CFG_DIRECTION       0x0001      /* DMA direction (0=fifo->ram)      */

/* DVMA control register */
#define DMA_CTRL_CSUSPEND       0x0010      /* DMA channel suspend              */
#define DMA_CTRL_CCLEAR         0x0008      /* DMA channel clear and reset      */
#define DMA_CTRL_FCLEAR         0x0004      /* DMA fifo clear                   */
#define DMA_CTRL_CIRQ           0x0002      /* DMA irq clear                    */
#define DMA_CTRL_DMASTART       0x0001      /* DMA transfer start               */

/* DVMA status register */
#define DMA_STAT_PFULL          0x00c0      /* Pipe full                        */
#define DMA_STAT_PORUN          0x0080      /* Pipe overrun                     */
#define DMA_STAT_PSTG1          0x0040      /* Pipe has stage 1 loaded          */
#define DMA_STAT_PEMPTY         0x0000      /* Pipe is empty                    */
#define DMA_STAT_CSUSP          0x0030      /* Channel suspended                */
#define DMA_STAT_CTRAN          0x0020      /* Channel transfer in progress     */
#define DMA_STAT_CACTIVE        0x0010      /* Channel active                   */
#define DMA_STAT_CIDLE          0x0000      /* Channel idle                     */
#define DMA_STAT_SPAR           0x0008      /* SBUS parity error                */
#define DMA_STAT_SERR           0x0004      /* SBUS dma error                   */
#define DMA_STAT_TCNT           0x0002      /* Terminal count expired           */
#define DMA_STAT_IRQ            0x0001      /* DMA interrupt                    */

/* DVMA FIFO status register */
#define DMA_FSTAT_ORUN          0x0200      /* FIFO overrun                     */
#define DMA_FSTAT_URUN          0x0100      /* FIFO underrun                    */
#define DMA_FSTAT_CMASK         0x007f      /* FIFO count mask                  */

/* SCSI processor config 1 register */
#define CPU_CFG1_ASTIME         0xf000      /* Asynchronous setup time mask     */
#define CPU_CFG1_STUNIT         0x0000      /* Selection time unit              */
#define CPU_CFG1_STIMEO         0x0600      /* Selection timeout value          */
#define CPU_CFG1_CFACT          0x00e0      /* Clock factor                     */
#define CPU_CFG1_SID            0x000f      /* SCSI ID                          */

/* SCSI processor config 2 register */
#define CPU_CFG2_FDISAB         0x0040      /* SCSI filter disable              */
#define CPU_CFG2_ERAPUPS        0x0020      /* Req/Ack pullup enable            */
#define CPU_CFG2_EDPUPS         0x0010      /* Data active pullup enable        */
#define CPU_CFG2_ECAUTO         0x0008      /* Autoload device config enable    */
#define CPU_CFG2_ERESEL         0x0002      /* Enable reselections              */
#define CPU_CFG2_ESEL           0x0001      /* Enable selections                */

/* SCSI processor interrupt register */
#define CPU_IRQ_PERR            0x8000      /* Parity error                     */
#define CPU_IRQ_GERR            0x4000      /* Gross error                      */
#define CPU_IRQ_FABORT          0x2000      /* Function abort                   */
#define CPU_IRQ_CFAIL           0x1000      /* Condition failed                 */
#define CPU_IRQ_FEMPTY          0x0800      /* FIFO empty                       */
#define CPU_IRQ_BCZ             0x0400      /* Byte counter is zero             */
#define CPU_IRQ_XZ              0x0200      /* Transfer counter is zero         */
#define CPU_IRQ_IRQ             0x0080      /* SCSI processor interrupt pending */
#define CPU_IRQ_CRUN            0x0040      /* Command is running               */
#define CPU_IRQ_RCODE           0x000f      /* Return code for interrupt        */

/* SCSI processor gross error register */
#define CPU_GERR_ONZ            0x0040      /* Offset not zero                  */
#define CPU_GERR_OUF            0x0020      /* Offset underflowed               */
#define CPU_GERR_OOF            0x0010      /* Offset overflowed                */
#define CPU_GERR_FUF            0x0008      /* FIFO underflowed                 */
#define CPU_GERR_FOF            0x0004      /* FIFO overflowed                  */
#define CPU_GERR_WERR           0x0002      /* Error on write                   */
#define CPU_GERR_ILL            0x0001      /* Instruction was illegal          */

/* SCSI processor exception register */
#define CPU_EXC_UZERO           0x8000      /* User exception zero              */
#define CPU_EXC_UONE            0x4000      /* User exception one               */
#define CPU_EXC_BFREE           0x0200      /* Bus free                         */
#define CPU_EXC_TATN            0x0100      /* ATN in target mode               */
#define CPU_EXC_RESEL           0x0080      /* Reselect exception               */
#define CPU_EXC_SEL             0x0040      /* Selection exception              */
#define CPU_EXC_ARB             0x0020      /* Arbitration exception            */
#define CPU_EXC_GERR            0x0010      /* Gross error exception            */
#define CPU_EXC_RESET           0x0008      /* Bus reset exception              */

/* SCSI processor override register */
#define CPU_ORIDE_ETRIG         0x8000      /* External trigger enable          */
#define CPU_ORIDE_STEP          0x4000      /* Single step mode enable          */
#define CPU_ORIDE_BKPT          0x2000      /* Breakpoint reg enable            */
#define CPU_ORIDE_PWRITE        0x1000      /* SCSI pin write enable            */
#define CPU_ORIDE_OFORCE        0x0800      /* Force outputs on                 */
#define CPU_ORIDE_LBACK         0x0400      /* SCSI loopback enable             */
#define CPU_ORIDE_PTEST         0x0200      /* Parity test enable               */
#define CPU_ORIDE_TENAB         0x0100      /* SCSI pins tristate enable        */
#define CPU_ORIDE_TPINS         0x0080      /* SCSI pins enable                 */
#define CPU_ORIDE_FRESET        0x0008      /* FIFO reset                       */
#define CPU_ORIDE_CTERM         0x0004      /* Command terminate                */
#define CPU_ORIDE_RREG          0x0002      /* Reset SCSI processor regs        */
#define CPU_ORIDE_RMOD          0x0001      /* Reset SCSI processor module      */

/* SCSI processor commands */
#define CPU_CMD_BRESET          0x300b      /* Reset SCSI bus                   */

/* SCSI processor user exception register */
#define CPU_UEXC_ONE            0x0002      /* User exception one               */
#define CPU_UEXC_ZERO           0x0001      /* User exception zero              */

/* SCSI processor SCSI id register */
#define CPU_SID_RSEID           0x0f00      /* Who is sel/resel'ing us          */
#define CPU_SID_SID             0x000f      /* Selection ID                     */

/* SCSI processor device config 1 */
#define CPU_DCFG1_SHOLD         0x7000      /* Sync data hold                   */
#define CPU_DCFG1_SSETUP        0x0f00      /* Sync data setup                  */
#define CPU_DCFG1_SOFF          0x000f      /* Sync data offset                 */

/* SCSI processor device config 2 */
#define CPU_DCFG2_FMASK         0xf000      /* Device flags                     */
#define CPU_DCFG2_EWIDE         0x0400      /* WIDE enable                      */
#define CPU_DCFG2_EPAR          0x0200      /* Parity enable                    */
#define CPU_DCFG2_EBLK          0x0100      /* Block mode transfer enable       */
#define CPU_DCFG2_AMASK         0x0007      /* Assertion period mask            */

/* SCSI processor phase pointer register */
#define CPU_PPTR_STAT           0x1000      /* Status buf offset                */
#define CPU_PPTR_MSGIN          0x0700      /* Msg-in buf offset                */
#define CPU_PPTR_COM            0x00f0      /* Cmd buf offset                   */
#define CPU_PPTR_MSGOUT         0x0007      /* Msg-out buf offset               */

/* SCSI processor fifo status register */
#define CPU_FSTAT_TFULL         0x8000      /* Top residue full                 */
#define CPU_FSTAT_ARES          0x4000      /* Odd residue for wide transfer    */
#define CPU_FSTAT_CMSK          0x001c      /* FIFO count mask                  */
#define CPU_FSTAT_BRES          0x0001      /* Bottom residue full              */

/* SCSI processor pin control register */
#define CPU_PCTRL_PVALID        0x8000      /* Phase bits are valid             */
#define CPU_PCTRL_PHI           0x0400      /* Parity bit high                  */
#define CPU_PCTRL_PLO           0x0200      /* Parity bit low                   */
#define CPU_PCTRL_REQ           0x0100      /* REQ bus signal                   */
#define CPU_PCTRL_ACK           0x0080      /* ACK bus signal                   */
#define CPU_PCTRL_RST           0x0040      /* RST bus signal                   */
#define CPU_PCTRL_BSY           0x0020      /* BSY bus signal                   */
#define CPU_PCTRL_SEL           0x0010      /* SEL bus signal                   */
#define CPU_PCTRL_ATN           0x0008      /* ATN bus signal                   */
#define CPU_PCTRL_MSG           0x0004      /* MSG bus signal                   */
#define CPU_PCTRL_CD            0x0002      /* CD bus signal                    */
#define CPU_PCTRL_IO            0x0001      /* IO bus signal                    */

/* SCSI processor differential pins register */
#define CPU_PDIFF_SENSE         0x0200      /* Differential sense               */
#define CPU_PDIFF_MODE          0x0100      /* Differential mode                */
#define CPU_PDIFF_OENAB         0x0080      /* Outputs enable                   */
#define CPU_PDIFF_PMASK         0x007c      /* Differential control pins        */
#define CPU_PDIFF_TGT           0x0002      /* Target mode enable               */
#define CPU_PDIFF_INIT          0x0001      /* Initiator mode enable            */

/* RISC processor status register */
#define RISC_PSR_FTRUE          0x8000      /* Force true                       */
#define RISC_PSR_LCD            0x4000      /* Loop counter shows done status   */
#define RISC_PSR_RIRQ           0x2000      /* RISC irq status                  */
#define RISC_PSR_TOFLOW         0x1000      /* Timer overflow (rollover)        */
#define RISC_PSR_AOFLOW         0x0800      /* Arithmetic overflow              */
#define RISC_PSR_AMSB           0x0400      /* Arithmetic big endian            */
#define RISC_PSR_ACARRY         0x0200      /* Arithmetic carry                 */
#define RISC_PSR_AZERO          0x0100      /* Arithmetic zero                  */
#define RISC_PSR_ULTRA          0x0020      /* Ultra mode                       */
#define RISC_PSR_DIRQ           0x0010      /* DVMA interrupt                   */
#define RISC_PSR_SIRQ           0x0008      /* SCSI processor interrupt         */
#define RISC_PSR_HIRQ           0x0004      /* Host interrupt                   */
#define RISC_PSR_IPEND          0x0002      /* Interrupt pending                */
#define RISC_PSR_FFALSE         0x0001      /* Force false                      */

/* RISC processor memory timing register */
#define RISC_MTREG_P1DFLT       0x1200      /* Default read/write timing, pg1   */
#define RISC_MTREG_P0DFLT       0x0012      /* Default read/write timing, pg0   */
#define RISC_MTREG_P1ULTRA      0x2300      /* Ultra-mode rw timing, pg1        */
#define RISC_MTREG_P0ULTRA      0x0023      /* Ultra-mode rw timing, pg0        */

/* Host command/ctrl register */
#define HCCTRL_NOP              0x0000      /* CMD: No operation                */
#define HCCTRL_RESET            0x1000      /* CMD: Reset RISC cpu              */
#define HCCTRL_PAUSE            0x2000      /* CMD: Pause RISC cpu              */
#define HCCTRL_REL              0x3000      /* CMD: Release paused RISC cpu     */
#define HCCTRL_STEP             0x4000      /* CMD: Single step RISC cpu        */
#define HCCTRL_SHIRQ            0x5000      /* CMD: Set host irq                */
#define HCCTRL_CHIRQ            0x6000      /* CMD: Clear host irq              */
#define HCCTRL_CRIRQ            0x7000      /* CMD: Clear RISC cpu irq          */
#define HCCTRL_BKPT             0x8000      /* CMD: Breakpoint enables change   */
#define HCCTRL_TMODE            0xf000      /* CMD: Enable test mode            */
#define HCCTRL_HIRQ             0x0080      /* Host IRQ pending                 */
#define HCCTRL_RRIP             0x0040      /* RISC cpu reset in happening now  */
#define HCCTRL_RPAUSED          0x0020      /* RISC cpu is paused now           */
#define HCCTRL_EBENAB           0x0010      /* External breakpoint enable       */
#define HCCTRL_B1ENAB           0x0008      /* Breakpoint 1 enable              */
#define HCCTRL_B0ENAB           0x0004      /* Breakpoint 0 enable              */

#define QLOGICPTI {						   \
	detect:		qlogicpti_detect,			   \
	release:	qlogicpti_release,			   \
	info:		qlogicpti_info,				   \
	queuecommand:	qlogicpti_queuecommand,			   \
	abort:		qlogicpti_abort,			   \
	reset:		qlogicpti_reset,			   \
	can_queue:	QLOGICISP_REQ_QUEUE_LEN,		   \
	this_id:	7,					   \
	sg_tablesize:	QLOGICISP_MAX_SG(QLOGICISP_REQ_QUEUE_LEN), \
	cmd_per_lun:	1,					   \
	use_clustering:	DISABLE_CLUSTERING,			   \
	use_new_eh_code: 0					   \
}

/* For our interrupt engine. */
#define for_each_qlogicpti(qp) \
        for((qp) = qptichain; (qp); (qp) = (qp)->next)

#endif /* !(_QLOGICPTI_H) */

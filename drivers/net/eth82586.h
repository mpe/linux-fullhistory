/*
 * eth82586.h: Intel EtherExpress defines
 *
 * Written 1995 by John Sullivan
 * See eexpress.c for further details
 * documentation and usage to do.
 */

/*
 * EtherExpress card register addresses
 * as offsets from the base IO region (dev->base_addr)
 */

#define DATAPORT 0x0000
#define WRITE_PTR 0x0002
#define READ_PTR 0x0004
#define SIGNAL_CA 0x0006
#define SET_IRQ 0x0007
#define SM_PTR 0x0008
#define MEM_Ctrl 0x000b
#define MEM_Page_Ctrl 0x000c
#define Config 0x000d
#define EEPROM_Ctrl 0x000e
#define ID_PORT 0x000f

/*
 * offset to shadowed memory, 0 <= x <= 31. We don't use this yet,
 * but may in the future. Is shadow memory access any faster than
 * dataport access?
 */
#define SM_ADDR(x) (0x4000+((x&0x10)<<10)+(x&0xf))

/* Always mirrors eexp-memory at 0x0008-0x000f */
#define SCB_STATUS 0xc008
#define SCB_CMD 0xc00a
#define SCB_CBL 0xc00c
#define SCB_RFA 0xc00e



/*
 * card register defines
 */

/* SET_IRQ */
#define SIRQ_en 0x08
#define SIRQ_dis 0x00

/* Config */
#define set_loopback outb(inb(ioaddr+Config)|0x02,ioaddr+Config)
#define clear_loopback outb(inb(ioaddr+Config)&0xfd,ioaddr+Config)

/* EEPROM_Ctrl */
#define EC_Clk 0x01
#define EC_CS  0x02
#define EC_Wr  0x04
#define EC_Rd  0x08
#define ASIC_RST 0x40
#define i586_RST  0x80

#define eeprom_delay() { int _i = 40; while (--_i>0) { __SLOW_DOWN_IO; }}

/*
 * i82586 Memory Configuration
 */

/* (System Configuration Pointer) System start up block, read after 586_RST */
#define SCP_START 0xfff6


/* Intermediate System Configuration Pointer */
#define ISCP_START 0x0000
/* System Command Block */
#define SCB_START 0x0008

/*
 * Start of buffer region. If we have 64k memory, eexp_hw_probe() may raise
 * NUM_TX_BUFS. RX_BUF_END is set to the end of memory, and all space between
 * the transmit buffer region and end of memory used for as many receive buffers
 * as we can fit. See eexp_hw_[(rx)(tx)]init().
 */
#define TX_BUF_START 0x0100
#define TX_BUF_SIZE ((24+ETH_FRAME_LEN+31)&~0x1f)
#define RX_BUF_SIZE ((32+ETH_FRAME_LEN+31)&~0x1f)



/*
 * SCB defines
 */

/* these functions take the SCB status word and test the relevant status bit */
#define SCB_complete(s) ((s&0x8000)!=0)
#define SCB_rxdframe(s) ((s&0x4000)!=0)
#define SCB_CUdead(s) ((s&0x2000)!=0)
#define SCB_RUdead(s) ((s&0x1000)!=0)
#define SCB_ack(s) (s & 0xf000)

/* Command unit status: 0=idle, 1=suspended, 2=active */
#define SCB_CUstat(s) ((s&0x0300)>>8)

/* Receive unit status: 0=idle, 1=suspended, 2=out of resources, 4=ready */
#define SCB_RUstat(s) ((s&0x0070)>>4)

/* SCB commands */
#define SCB_CUnop     0x0000
#define SCB_CUstart   0x0100
#define SCB_CUresume  0x0200
#define SCB_CUsuspend 0x0300
#define SCB_CUabort   0x0400

/* ? */
#define SCB_resetchip 0x0080

#define SCB_RUnop     0x0000
#define SCB_RUstart   0x0010
#define SCB_RUresume  0x0020
#define SCB_RUsuspend 0x0030
#define SCB_RUabort   0x0040


/*
 * Command block defines
 */

#define Stat_Done(s)   ((s&0x8000)!=0)
#define Stat_Busy(s)   ((s&0x4000)!=0)
#define Stat_OK(s)     ((s&0x2000)!=0)
#define Stat_Abort(s)  ((s&0x1000)!=0)
#define Stat_STFail    ((s&0x0800)!=0)
#define Stat_TNoCar(s) ((s&0x0400)!=0)
#define Stat_TNoCTS(s) ((s&0x0200)!=0)
#define Stat_TNoDMA(s) ((s&0x0100)!=0)
#define Stat_TDefer(s) ((s&0x0080)!=0)
#define Stat_TColl(s)  ((s&0x0040)!=0)
#define Stat_TXColl(s) ((s&0x0020)!=0)
#define Stat_NoColl(s) (s&0x000f)

/* Cmd_END will end AFTER the command if this is the first
 * command block after an SCB_CUstart, but BEFORE the command
 * for all subsequent commands. Best strategy is to place
 * Cmd_INT on the last command in the sequence, followed by a
 * dummy Cmd_Nop with Cmd_END after this.
 */
#define Cmd_END     0x8000
#define Cmd_SUS     0x4000
#define Cmd_INT     0x2000

#define Cmd_Nop     0x0000
#define Cmd_SetAddr 0x0001
#define Cmd_Config  0x0002
#define Cmd_MCast   0x0003
#define Cmd_Xmit    0x0004
#define Cmd_TDR     0x0005
#define Cmd_Dump    0x0006
#define Cmd_Diag    0x0007


/*
 * Frame Descriptor (Receive block) defines
 */

#define FD_Done(s)  ((s&0x8000)!=0)
#define FD_Busy(s)  ((s&0x4000)!=0)
#define FD_OK(s)    ((s&0x2000)!=0)

#define FD_CRC(s)   ((s&0x0800)!=0)
#define FD_Align(s) ((s&0x0400)!=0)
#define FD_Resrc(s) ((s&0x0200)!=0)
#define FD_DMA(s)   ((s&0x0100)!=0)
#define FD_Short(s) ((s&0x0080)!=0)
#define FD_NoEOF(s) ((s&0x0040)!=0)

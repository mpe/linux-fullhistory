#ifndef _ACENIC_H_
#define _ACENIC_H_

#if ((BITS_PER_LONG != 32) && (BITS_PER_LONG != 64))
#error "BITS_PER_LONG not defined or not valid"
#endif


struct ace_regs {

	u32	pad0[16];	/* PCI control registers */

	u32	HostCtrl;	/* 0x40 */
	u32	LocalCtrl;

	u32	pad1[2];

	u32	MiscCfg;	/* 0x50 */

	u32	pad2[2];

	u32	PciState;

	u32	pad3[2];	/* 0x60 */

	u32	WinBase;
	u32	WinData;

	u32	pad4[12];	/* 0x70 */

	u32	DmaWriteState;	/* 0xa0 */
	u32	pad5[3];
	u32	DmaReadState;	/* 0xb0 */

	u32	pad6[26];

	u32	AssistState;

	u32	pad7[8];	/* 0x120 */

	u32	CpuCtrl;	/* 0x140 */
	u32	Pc;

	u32	pad8[3];

	u32	SramAddr;	/* 0x154 */
	u32	SramData;

	u32	pad9[49];

	u32	MacRxState;	/* 0x220 */

	u32	pad10[7];

	u32	CpuBCtrl;	/* 0x240 */
	u32	PcB;

	u32	pad11[3];

	u32	SramBAddr;	/* 0x254 */
	u32	SramBData;

	u32	pad12[105];

	u32	pad13[32];	/* 0x400 */
	u32	Stats[32];

	u32	Mb0Hi;		/* 0x500 */
	u32	Mb0Lo;
	u32	Mb1Hi;
	u32	CmdPrd;
	u32	Mb2Hi;
	u32	TxPrd;
	u32	Mb3Hi;
	u32	Mb3Lo;
	u32	Mb4Hi;
	u32	Mb4Lo;
	u32	Mb5Hi;
	u32	Mb5Lo;
	u32	Mb6Hi;
	u32	Mb6Lo;
	u32	Mb7Hi;
	u32	Mb7Lo;
	u32	Mb8Hi;
	u32	Mb8Lo;
	u32	Mb9Hi;
	u32	Mb9Lo;
	u32	MbAHi;
	u32	MbALo;
	u32	MbBHi;
	u32	MbBLo;
	u32	MbCHi;
	u32	MbCLo;
	u32	MbDHi;
	u32	MbDLo;
	u32	MbEHi;
	u32	MbELo;
	u32	MbFHi;
	u32	MbFLo;

	u32	pad14[32];

	u32	MacAddrHi;	/* 0x600 */
	u32	MacAddrLo;
	u32	InfoPtrHi;
	u32	InfoPtrLo;
	u32	MultiCastHi;	/* 0x610 */
	u32	MultiCastLo;
	u32	ModeStat;
	u32	DmaReadCfg;
	u32	DmaWriteCfg;	/* 0x620 */
	u32	pad15;
	u32	EvtCsm;
	u32	CmdCsm;
	u32	TuneRxCoalTicks;/* 0x630 */
	u32	TuneTxCoalTicks;
	u32	TuneStatTicks;
	u32	TuneMaxTxDesc;
	u32	TuneMaxRxDesc;	/* 0x640 */
	u32	TuneTrace;
	u32	TuneLink;
	u32	TuneFastLink;
	u32	TracePtr;	/* 0x650 */
	u32	TraceStrt;
	u32	TraceLen;
	u32	IfIdx;
	u32	IfMtu;		/* 0x660 */
	u32	MaskInt;
	u32	LnkState;
	u32	FastLnkState;
	u32	pad16[4];	/* 0x670 */
	u32	RxRetCsm;	/* 0x680 */

	u32	pad17[31];

	u32	CmdRng[64];	/* 0x700 */
	u32	Window[0x200];
};

#define ACE_WINDOW_SIZE	0x800

#define ACE_JUMBO_MTU 9000
#define ACE_STD_MTU 1500

#define ACE_TRACE_SIZE 0x8000

/*
 * Host control register bits.
 */
	
#define IN_INT		0x01
#define CLR_INT		0x02
#define BYTE_SWAP	0x10
#define WORD_SWAP	0x20
#define MASK_INTS	0x40

/*
 * Local control register bits.
 */

#define EEPROM_DATA_IN		0x800000
#define EEPROM_DATA_OUT		0x400000
#define EEPROM_WRITE_ENABLE	0x200000
#define EEPROM_CLK_OUT		0x100000

#define EEPROM_BASE		0xa0000000

#define EEPROM_WRITE_SELECT	0xa0
#define EEPROM_READ_SELECT	0xa1

#define SRAM_BANK_512K		0x200


/*
 * Misc Config bits
 */

#define SYNC_SRAM_TIMING	0x100000


/*
 * CPU state bits.
 */

#define CPU_RESET		0x01
#define CPU_TRACE		0x02
#define CPU_PROM_FAILED		0x10
#define CPU_HALT		0x00010000
#define CPU_HALTED		0xffff0000


/*
 * PCI State bits.
 */

#define DMA_READ_MAX_4		0x04
#define DMA_READ_MAX_16		0x08
#define DMA_READ_MAX_32		0x0c
#define DMA_READ_MAX_64		0x10
#define DMA_READ_MAX_128	0x14
#define DMA_READ_MAX_256	0x18
#define DMA_READ_MAX_1K		0x1c
#define DMA_WRITE_MAX_4		0x20
#define DMA_WRITE_MAX_16	0x40
#define DMA_WRITE_MAX_32	0x60
#define DMA_WRITE_MAX_64	0x80
#define DMA_WRITE_MAX_128	0xa0
#define DMA_WRITE_MAX_256	0xc0
#define DMA_WRITE_MAX_1K	0xe0
#define MEM_READ_MULTIPLE	0x00020000
#define DMA_WRITE_ALL_ALIGN	0x00800000
#define READ_CMD_MEM		0x06000000
#define WRITE_CMD_MEM		0x70000000


/*
 * Mode status
 */

#define ACE_BYTE_SWAP_DATA	0x10
#define ACE_WARN		0x08
#define ACE_WORD_SWAP		0x04
#define ACE_FATAL		0x40000000


/*
 * DMA config
 */

#define DMA_THRESH_8W		0x80;


/*
 * Tuning parameters
 */

#define TICKS_PER_SEC		1000000


/*
 * Link bits
 */

#define LNK_PREF		0x00008000
#define LNK_10MB		0x00010000
#define LNK_100MB		0x00020000
#define LNK_1000MB		0x00040000
#define LNK_FULL_DUPLEX		0x00080000
#define LNK_HALF_DUPLEX		0x00100000
#define LNK_TX_FLOW_CTL_Y	0x00200000
#define LNK_NEG_ADVANCED	0x00400000
#define LNK_RX_FLOW_CTL_Y	0x00800000
#define LNK_NIC			0x01000000
#define LNK_JAM			0x02000000
#define LNK_JUMBO		0x04000000
#define LNK_ALTEON		0x08000000
#define LNK_NEG_FCTL		0x10000000
#define LNK_NEGOTIATE		0x20000000
#define LNK_ENABLE		0x40000000
#define LNK_UP			0x80000000


/*
 * Event definitions
 */

#define EVT_RING_ENTRIES	256
#define EVT_RING_SIZE	(EVT_RING_ENTRIES * sizeof(struct event))

struct event {
#ifdef __LITTLE_ENDIAN
	u32	idx:12;
	u32	code:12;
	u32	evt:8;
#else
	u32	evt:8;
	u32	code:12;
	u32	idx:12;
#endif
	u32     pad;
};


/*
 * Events
 */

#define E_FW_RUNNING		0x01
#define E_STATS_UPDATED		0x04

#define E_STATS_UPDATE		0x04

#define E_LNK_STATE		0x06
#define E_C_LINK_UP		0x01
#define E_C_LINK_DOWN		0x02

#define E_ERROR			0x07
#define E_C_ERR_INVAL_CMD	0x01
#define E_C_ERR_UNIMP_CMD	0x02
#define E_C_ERR_BAD_CFG		0x03

#define E_MCAST_LIST		0x08
#define E_C_MCAST_ADDR_ADD	0x01
#define E_C_MCAST_ADDR_DEL	0x02

#define E_RESET_JUMBO_RNG	0x09


/*
 * Commands
 */

#define CMD_RING_ENTRIES	64

struct cmd {
#ifdef __LITTLE_ENDIAN
	u32	idx:12;
	u32	code:12;
	u32	evt:8;
#else
	u32	evt:8;
	u32	code:12;
	u32	idx:12;
#endif
};


#define C_HOST_STATE		0x01
#define C_C_STACK_UP		0x01
#define C_C_STACK_DOWN		0x02

#define C_FDR_FILTERING		0x02
#define C_C_FDR_FILT_ENABLE	0x01
#define C_C_FDR_FILT_DISABLE	0x02

#define C_SET_RX_PRD_IDX	0x03
#define C_UPDATE_STATS		0x04
#define C_RESET_JUMBO_RNG	0x05
#define C_ADD_MULTICAST_ADDR	0x08
#define C_DEL_MULTICAST_ADDR	0x09

#define C_SET_PROMISC_MODE	0x0a
#define C_C_PROMISC_ENABLE	0x01
#define C_C_PROMISC_DISABLE	0x02

#define C_LNK_NEGOTIATION	0x0b
#define C_SET_MAC_ADDR		0x0c
#define C_CLEAR_PROFILE		0x0d

#define C_SET_MULTICAST_MODE	0x0e
#define C_C_MCAST_ENABLE	0x01
#define C_C_MCAST_DISABLE	0x02

#define C_CLEAR_STATS		0x0f
#define C_SET_RX_JUMBO_PRD_IDX	0x10
#define C_REFRESH_STATS		0x11


/*
 * Descriptor types.
 */

#define DESC_TX			0x01
#define DESC_RX			0x02
#define DESC_END		0x04
#define DESC_MORE		0x08

/*
 * RX control block flags
 */

#define RX_TCP_UDP_SUM		0x01
#define RX_IP_SUM		0x02
#define RX_SPLIT_HDRS		0x04
#define RX_NO_PSEUDO_HDR_SUM	0x08

/*
 * Descriptor flags
 */

#define JUMBO_FLAG		0x10

/*
 * TX ring
 */

#define TX_RING_ENTRIES	128
#define TX_RING_SIZE	(TX_RING_ENTRIES * sizeof(struct tx_desc))
#define TX_RING_BASE	0x3800

struct tx_desc{
#if (BITS_PER_LONG == 64)
	u64	addr;
#else
        u32	zero;
	u32	addr;
#endif
#if __LITTLE_ENDIAN
	u16	flags;
	u16	size;
#else
	u16	size;
	u16	flags;
#endif
	u32	nic_addr;
};


#define RX_STD_RING_ENTRIES	512
#define RX_STD_RING_SIZE	(RX_STD_RING_ENTRIES * sizeof(struct rx_desc))

#define RX_JUMBO_RING_ENTRIES	256
#define RX_JUMBO_RING_SIZE	(RX_JUMBO_RING_ENTRIES *sizeof(struct rx_desc))

#define RX_RETURN_RING_ENTRIES	(2 * RX_STD_RING_ENTRIES)
#define RX_RETURN_RING_SIZE	(RX_RETURN_RING_ENTRIES * \
				 sizeof(struct rx_desc))

#define RX_RING_THRESH		32

struct rx_desc{
#if (BITS_PER_LONG == 64)
	u64	addr;
#else
        u32	zero;
	u32	addr;
#endif
#ifdef __LITTLE_ENDIAN
	u16	size;
	u16	idx;
#else
	u16	idx;
	u16	size;
#endif
#ifdef __LITTLE_ENDIAN
	u16	flags;
	u16	type;
#else
	u16	type;
	u16	flags;
#endif
#ifdef __LITTLE_ENDIAN
	u16	tcp_udp_csum;
	u16	ip_csum;
#else
	u16	ip_csum;
	u16	tcp_udp_csum;
#endif
#ifdef __LITTLE_ENDIAN
	u16	reserved;
	u16	err_flags;
#else
	u16	err_flags;
	u16	reserved;
#endif
	u32	nic_addr;
	u32	pad[1];
};


/*
 * This struct is shared with the NIC firmware.
 */
struct ring_ctrl {
#if (BITS_PER_LONG == 64)
	u64	rngptr;
#else
	u32	zero;
	u32	rngptr;
#endif
#ifdef __LITTLE_ENDIAN
	u16	flags;
	u16	max_len;
#else
	u16	max_len;
	u16	flags;
#endif
	u32	pad;
};


struct ace_mac_stats {
	u32 excess_colls;
	u32 coll_1;
	u32 coll_2;
	u32 coll_3;
	u32 coll_4;
	u32 coll_5;
	u32 coll_6;
	u32 coll_7;
	u32 coll_8;
	u32 coll_9;
	u32 coll_10;
	u32 coll_11;
	u32 coll_12;
	u32 coll_13;
	u32 coll_14;
	u32 coll_15;
	u32 late_coll;
	u32 defers;
	u32 crc_err;
	u32 underrun;
	u32 crs_err;
	u32 pad[3];
	u32 drop_ula;
	u32 drop_mc;
	u32 drop_fc;
	u32 drop_space;
	u32 coll;
	u32 kept_bc;
	u32 kept_mc;
	u32 kept_uc;
};


struct ace_info {
	union {
		u32 stats[256];
	} s;
	struct ring_ctrl	evt_ctrl;
	struct ring_ctrl	cmd_ctrl;
	struct ring_ctrl	tx_ctrl;
	struct ring_ctrl	rx_std_ctrl;
	struct ring_ctrl	rx_jumbo_ctrl;
	struct ring_ctrl	rx_return_ctrl;
#if (BITS_PER_LONG == 64)
	u64	evt_prd_ptr;
	u64	rx_ret_prd_ptr;
	u64	tx_csm_ptr;
	u64	stats2_ptr;
#else
	u32	evt_prd_ptr_hi;
	u32	evt_prd_ptr;
	u32	rx_ret_prd_ptr_hi;
	u32	rx_ret_prd_ptr;
	u32	tx_csm_ptr_hi;
	u32	tx_csm_ptr;
	u32	stats2_ptr_hi;
	u32	stats2_ptr;
#endif
};

/*
 * Struct private for the AceNIC.
 */

struct ace_private
{
	struct ace_regs		*regs;		/* register base */
	volatile __u32		*sgt;
	struct sk_buff		*pkt_buf;	/* Receive buffer */
/*
 * The send ring is located in the shared memory window
 */
	struct tx_desc		*tx_ring;
	struct rx_desc		rx_std_ring[RX_STD_RING_ENTRIES];
	struct rx_desc		rx_jumbo_ring[RX_JUMBO_RING_ENTRIES];
	struct rx_desc		rx_return_ring[RX_RETURN_RING_ENTRIES];
	struct event		evt_ring[EVT_RING_ENTRIES];
	struct ace_info		*info;
	struct sk_buff		*tx_skbuff[TX_RING_ENTRIES];
	struct sk_buff		*rx_std_skbuff[RX_STD_RING_ENTRIES];
	struct sk_buff		*rx_jumbo_skbuff[RX_JUMBO_RING_ENTRIES];
	spinlock_t		lock;
	struct timer_list	timer;
	u32			cur_rx, tx_prd;
	u32			dirty_rx, tx_ret_csm, dirty_event;
	u32			rx_std_skbprd, rx_jumbo_skbprd;
	u32			tx_full;
	volatile u32		evt_prd
				__attribute__ ((aligned (L1_CACHE_BYTES)));
	volatile u32		rx_ret_prd
				__attribute__ ((aligned (L1_CACHE_BYTES)));
	volatile u32		tx_csm
				__attribute__ ((aligned (L1_CACHE_BYTES)));
	struct device		*next
				__attribute__ ((aligned (L1_CACHE_BYTES)));
	unsigned char		*trace_buf;
	int			fw_running, fw_up, jumbo, promisc;
	int			version;
	int			flags;
	u16			vendor;
	u16			pci_command;
	struct pci_dev		*pdev;
#if 0
	u8			pci_bus;
	u8			pci_dev_fun;
#endif
	char			name[24];
	struct net_device_stats stats;
};

/*
 * Prototypes
 */
static int ace_init(struct device *dev, int board_idx);
static int ace_load_std_rx_ring(struct device *dev);
static int ace_load_jumbo_rx_ring(struct device *dev);
static int ace_flush_jumbo_rx_ring(struct device *dev);
static void ace_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static int ace_open(struct device *dev);
static int ace_start_xmit(struct sk_buff *skb, struct device *dev);
static int ace_close(struct device *dev);
static void ace_timer(unsigned long data);
static void ace_dump_trace(struct ace_private *ap);
static void ace_set_multicast_list(struct device *dev);
static int ace_change_mtu(struct device *dev, int new_mtu);
static int ace_set_mac_addr(struct device *dev, void *p);
static struct net_device_stats *ace_get_stats(struct device *dev);
static u8 read_eeprom_byte(struct ace_regs *regs, unsigned long offset);

#endif /* _ACENIC_H_ */

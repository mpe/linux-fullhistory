/* myri_sbus.h: Defines for MyriCOM MyriNET SBUS card driver.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _MYRI_SBUS_H
#define _MYRI_SBUS_H

struct lanai_regs {
	volatile unsigned int	ipf0;		/* Context zero state registers.*/
	volatile unsigned int	cur0;
	volatile unsigned int	prev0;
	volatile unsigned int	data0;
	volatile unsigned int	dpf0;
	volatile unsigned int	ipf1;		/* Context one state registers.	*/
	volatile unsigned int	cur1;
	volatile unsigned int	prev1;
	volatile unsigned int	data1;
	volatile unsigned int	dpf1;
	volatile unsigned int	istat;		/* Interrupt status.		*/
	volatile unsigned int	eimask;		/* External IRQ mask.		*/
	volatile unsigned int	itimer;		/* IRQ timer.			*/
	volatile unsigned int	rtc;		/* Real Time Clock		*/
	volatile unsigned int	csum;		/* Checksum.			*/
	volatile unsigned int	dma_xaddr;	/* SBUS DMA external address.	*/
	volatile unsigned int	dma_laddr;	/* SBUS DMA local address.	*/
	volatile unsigned int	dma_ctr;	/* SBUS DMA counter.		*/
	volatile unsigned int	rx_dmaptr;	/* Receive DMA pointer.		*/
	volatile unsigned int	rx_dmalim;	/* Receive DMA limit.		*/
	volatile unsigned int	tx_dmaptr;	/* Transmit DMA pointer.	*/
	volatile unsigned int	tx_dmalim;	/* Transmit DMA limit.		*/
	volatile unsigned int	tx_dmalimt;	/* Transmit DMA limit w/tail.	*/
	unsigned int	_unused0;
	volatile unsigned char	rbyte;		/* Receive byte.		*/
	unsigned char	_unused1[3];
	volatile unsigned short	rhalf;		/* Receive half-word.		*/
	unsigned char	_unused2[2];
	volatile unsigned int	rword;		/* Receive word.		*/
	volatile unsigned int	salign;		/* Send align.			*/
	volatile unsigned int	ss_sendbyte;	/* SingleSend send-byte.	*/
	volatile unsigned int	ss_sendhalf;	/* SingleSend send-halfword.	*/
	volatile unsigned int	ss_sendword;	/* SingleSend send-word.	*/
	volatile unsigned int	ss_sendt;	/* SingleSend special.		*/
	volatile unsigned int	dma_dir;	/* DMA direction.		*/
	volatile unsigned int	dma_stat;	/* DMA status.			*/
	volatile unsigned int	timeo;		/* Timeout register.		*/
	volatile unsigned int	myrinet;	/* XXX MAGIC myricom thing	*/
	volatile unsigned int	hwdebug;	/* Hardware debugging reg.	*/
	volatile unsigned int	leds;		/* LED control.			*/
	volatile unsigned int	vers;		/* Version register.		*/
	volatile unsigned int	link_on;	/* Link activation reg.		*/
	unsigned int _unused3[0x17];
	volatile unsigned int	cval;		/* Clock value register.	*/
};

/* Interrupt status bits. */
#define ISTAT_DEBUG	0x80000000
#define ISTAT_HOST	0x40000000
#define ISTAT_LAN7	0x00800000
#define ISTAT_LAN6	0x00400000
#define ISTAT_LAN5	0x00200000
#define ISTAT_LAN4	0x00100000
#define ISTAT_LAN3	0x00080000
#define ISTAT_LAN2	0x00040000
#define ISTAT_LAN1	0x00020000
#define ISTAT_LAN0	0x00010000
#define ISTAT_WRDY	0x00008000
#define ISTAT_HRDY	0x00004000
#define ISTAT_SRDY	0x00002000
#define ISTAT_LINK	0x00001000
#define ISTAT_FRES	0x00000800
#define ISTAT_NRES	0x00000800
#define ISTAT_WAKE	0x00000400
#define ISTAT_OB2	0x00000200
#define ISTAT_OB1	0x00000100
#define ISTAT_TAIL	0x00000080
#define ISTAT_WDOG	0x00000040
#define ISTAT_TIME	0x00000020
#define ISTAT_DMA	0x00000010
#define ISTAT_SEND	0x00000008
#define ISTAT_BUF	0x00000004
#define ISTAT_RECV	0x00000002
#define ISTAT_BRDY	0x00000001

struct myri_regs {
	volatile unsigned int	reset_off;
	volatile unsigned int	reset_on;
	volatile unsigned int	irq_off;
	volatile unsigned int	irq_on;
	volatile unsigned int	wakeup_off;
	volatile unsigned int	wakeup_on;
	volatile unsigned int	irq_read;
	unsigned int _unused[0xfff9];
	volatile unsigned short	local_mem[0x10800];
};

/* Shared memory interrupt mask. */
#define SHMEM_IMASK_RX		0x00000002
#define SHMEM_IMASK_TX		0x00000001

/* Just to make things readable. */
#define KERNEL_CHANNEL		0

/* The size of this must be >= 129 bytes. */
struct myri_eeprom {
	unsigned int		cval;
	unsigned short		cpuvers;
	unsigned char		id[6];
	unsigned int		ramsz;
	unsigned char		fvers[32];
	unsigned char		mvers[16];
	unsigned short		dlval;
	unsigned short		brd_type;
	unsigned short		bus_type;
	unsigned short		prod_code;
	unsigned int		serial_num;
	unsigned short		_reserved[24];
	unsigned int		_unused[2];
};

/* EEPROM bus types, only SBUS is valid in this driver. */
#define BUS_TYPE_SBUS		1

/* EEPROM CPU revisions. */
#define CPUVERS_2_3		0x0203
#define CPUVERS_3_0		0x0300
#define CPUVERS_3_1		0x0301
#define CPUVERS_3_2		0x0302
#define CPUVERS_4_0		0x0400
#define CPUVERS_4_1		0x0401
#define CPUVERS_4_2		0x0402
#define CPUVERS_5_0		0x0500

struct myri_control {
	volatile unsigned short	ctrl;
	volatile unsigned short	irqlvl;
};

/* Global control register defines. */
#define CONTROL_ROFF		0x8000	/* Reset OFF.		*/
#define CONTROL_RON		0x4000	/* Reset ON.		*/
#define CONTROL_EIRQ		0x2000	/* Enable IRQ's.	*/
#define CONTROL_DIRQ		0x1000	/* Disable IRQ's.	*/
#define CONTROL_WON		0x0800	/* Wake-up ON.		*/

#define MYRI_SCATTER_ENTRIES	8
#define MYRI_GATHER_ENTRIES	16

struct myri_sglist {
	unsigned int addr;
	unsigned int len;
};

struct myri_rxd {
	struct myri_sglist myri_scatters[MYRI_SCATTER_ENTRIES];	/* DMA scatter list.*/
	unsigned int csum;				/* HW computed checksum.    */
	unsigned int ctx;
	unsigned int num_sg;				/* Total scatter entries.   */
};

struct myri_txd {
	struct myri_sglist myri_gathers[MYRI_GATHER_ENTRIES]; /* DMA scatter list.  */
	unsigned int num_sg;				/* Total scatter entries.   */
	unsigned short addr[4];				/* XXX address              */
	unsigned int chan;
	unsigned int len;				/* Total length of packet.  */
	unsigned int csum_off;				/* Where data to csum is.   */
	unsigned int csum_field;			/* Where csum goes in pkt.  */
};

#define MYRINET_MTU        8432
#define RX_ALLOC_SIZE      8448
#define MYRI_PAD_LEN       2
#define RX_COPY_THRESHOLD  256

/* These numbers are cast in stone, new firmware is needed if
 * you want to change them.
 */
#define TX_RING_MAXSIZE    16
#define RX_RING_MAXSIZE    16

#define TX_RING_SIZE       16
#define RX_RING_SIZE       16

/* GRRR... */
static __inline__ int NEXT_RX(int num)
{
	if(++num > RX_RING_SIZE)
		num = 0;
	return num;
}

static __inline__ int PREV_RX(int num)
{
	if(--num < 0)
		num = RX_RING_SIZE;
	return num;
}

#define NEXT_TX(num)	(((num) + 1) & (TX_RING_SIZE - 1))
#define PREV_TX(num)	(((num) - 1) & (TX_RING_SIZE - 1))

#define TX_BUFFS_AVAIL(head, tail)		\
	((head) <= (tail) ?			\
	 (head) + (TX_RING_SIZE - 1) - (tail) :	\
	 (head) - (tail) - 1)

struct sendq {
	unsigned int	tail;
	unsigned int	head;
	unsigned int	hdebug;
	unsigned int	mdebug;
	struct myri_txd	myri_txd[TX_RING_MAXSIZE];
};

struct recvq {
	unsigned int	head;
	unsigned int	tail;
	unsigned int	hdebug;
	unsigned int	mdebug;
	struct myri_rxd	myri_rxd[RX_RING_MAXSIZE + 1];
};

#define MYRI_MLIST_SIZE 8

struct mclist {
	unsigned int maxlen;
	unsigned int len;
	unsigned int cache;
	struct pair {
		unsigned char addr[8];
		unsigned int  val;
	} mc_pairs[MYRI_MLIST_SIZE];
	unsigned char bcast_addr[8];
};

struct myri_channel {
	unsigned int	state;		/* State of the channel.	*/
	unsigned int	busy;		/* Channel is busy.		*/
	struct sendq	sendq;		/* Device tx queue.		*/
	struct recvq	recvq;		/* Device rx queue.		*/
	struct recvq	recvqa;		/* Device rx queue acked.	*/
	unsigned int	rbytes;		/* Receive bytes.		*/
	unsigned int	sbytes;		/* Send bytes.			*/
	unsigned int	rmsgs;		/* Receive messages.		*/
	unsigned int	smsgs;		/* Send messages.		*/
	struct mclist	mclist;		/* Device multicast list.	*/
};

/* Values for per-channel state. */
#define STATE_WFH	0		/* Waiting for HOST.		*/
#define STATE_WFN	1		/* Waiting for NET.		*/
#define STATE_READY	2		/* Ready.			*/

struct myri_shmem {
	unsigned char	addr[8];	/* Board's address.		*/
	unsigned int	nchan;		/* Number of channels.		*/
	unsigned int	burst;		/* SBUS dma burst enable.	*/
	unsigned int	shakedown;	/* DarkkkkStarrr Crashesss...	*/
	unsigned int	send;		/* Send wanted.			*/
	unsigned int	imask;		/* Interrupt enable mask.	*/
	unsigned int	mlevel;		/* Map level.			*/
	unsigned int	debug[4];	/* Misc. debug areas.		*/
	struct myri_channel channel;	/* Only one channel on a host.	*/
};

struct myri_eth {
	/* These are frequently accessed, keep together
	 * to obtain good cache hit rates.
	 */
	struct myri_shmem		*shmem;		/* Shared data structures.    */
	struct myri_control		*cregs;		/* Control register space.    */
	struct recvq			*rqack;		/* Where we ack rx's.         */
	struct recvq			*rq;		/* Where we put buffers.      */
	struct sendq			*sq;		/* Where we stuff tx's.       */
	struct device			*dev;		/* Linux/NET dev struct.      */
	int				tx_old;		/* To speed up tx cleaning.   */
	struct lanai_regs		*lregs;		/* Quick ptr to LANAI regs.   */
	struct sk_buff	       *rx_skbs[RX_RING_SIZE+1];/* RX skb's                   */
	struct sk_buff	       *tx_skbs[TX_RING_SIZE];  /* TX skb's                   */
	struct net_device_stats		enet_stats;	/* Interface stats.           */

	/* These are less frequently accessed. */
	struct myri_regs		*regs;          /* MyriCOM register space.    */
	unsigned short			*lanai;		/* View 2 of register space.  */
	unsigned int			*lanai3;	/* View 3 of register space.  */
	unsigned int			myri_bursts;	/* SBUS bursts.               */
	struct myri_eeprom		eeprom;		/* Local copy of EEPROM.      */
	unsigned int			reg_size;	/* Size of register space.    */
	unsigned int			shmem_base;	/* Offset to shared ram.      */
	struct linux_sbus_device	*myri_sbus_dev;	/* Our SBUS device struct.    */
	struct myri_eth			*next_module;	/* Next in adapter chain.     */
};

/* We use this to acquire receive skb's that we can DMA directly into. */
#define ALIGNED_RX_SKB_ADDR(addr) \
        ((((unsigned long)(addr) + (64 - 1)) & ~(64 - 1)) - (unsigned long)(addr))
static inline struct sk_buff *myri_alloc_skb(unsigned int length, int gfp_flags)
{
	struct sk_buff *skb;

	skb = alloc_skb(length + 64, gfp_flags);
	if(skb) {
		int offset = ALIGNED_RX_SKB_ADDR(skb->data);

		if(offset)
			skb_reserve(skb, offset);
	}
	return skb;
}

#endif /* !(_MYRI_SBUS_H) */

/* Generic NS8390 register definitions. */
/* This file is part of Donald Becker's 8390 drivers, and is distributed
   under the same license.
   Some of these names and comments are from the Crynwr packet drivers. */

#ifndef e8390_h
#define e8390_h

#define TX_2X_PAGES 12
#define TX_1X_PAGES 6
#define TX_PAGES (ei_status.pingpong ? TX_2X_PAGES : TX_1X_PAGES)

#define ETHER_ADDR_LEN 6

/* From 8390.c */
void ei_interrupt(int reg_ptr);
/* From auto_irq.c */
extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);

/* Most of these entries should be in 'struct device' (or most of the
   things in there should be here!) */
/* You have one of these per-board */
struct ei_device {
  char *name;
  void (*reset_8390)(struct device *);
  void (*block_output)(struct device *, int, const unsigned char *, int);
  int  (*block_input)(struct device *, int, char *, int);
  int open:1;
  int word16:1;	/* We have the 16-bit (vs 8-bit) version of the card. */
  int txing:1;			/* Transmit Active */
  int dmaing:2;			/* Remote DMA Active */
  int irqlock:1;		/* 8390's intrs disabled when '1'. */
  int pingpong:1;		/* Using the ping-pong driver */
  unsigned char tx_start_page, rx_start_page, stop_page;
  unsigned char current_page;	/* Read pointer in buffer  */
  unsigned char interface_num;	/* Net port (AUI, 10bT.) to use. */
  unsigned char txqueue;	/* Tx Packet buffer queue length. */
  unsigned char in_interrupt;
  short tx1, tx2;		/* Packet lengths for ping-pong tx. */
  short lasttx;			/* Alpha version consistency check. */
  /* The statistics: these are returned from the ioctl() as a block.  */
  int tx_packets;
  int tx_errors;
  int rx_packets;
  int soft_rx_errors;
  int soft_rx_err_bits;
  int missed_packets;
  int rx_overruns;
  int rx_overrun_packets;
};

#define ei_status (*(struct ei_device *)(dev->priv))

/* Some generic ethernet register configurations. */
#define E8390_TX_IRQ_MASK 0xa	/* For register EN0_ISR */
#define E8390_RX_IRQ_MASK  0x5
#define E8390_RXCONFIG 0x4	/* EN0_RXCR: broadcasts, no multicast,errors */
#define E8390_RXOFF 0x20	/* EN0_RXCR: Accept no packets */
#define E8390_TXCONFIG 0x00	/* EN0_TXCR: Normal transmit mode */
#define E8390_TXOFF 0x02	/* EN0_TXCR: Transmitter off */

/*  Register accessed at EN_CMD, the 8390 base addr.  */
#define E8390_STOP	0x01	/* Stop and reset the chip */
#define E8390_START	0x02	/* Start the chip, clear reset */
#define E8390_TRANS	0x04	/* Transmit a frame */
#define E8390_RREAD	0x08	/* Remote read */
#define E8390_RWRITE	0x10	/* Remote write  */
#define E8390_NODMA	0x20	/* Remote DMA */
#define E8390_PAGE0	0x00	/* Select page chip registers */
#define E8390_PAGE1	0x40	/* using the two high-order bits */
#define E8390_PAGE2	0x80	/* Page 3 is invalid. */

#define E8390_CMD	0x00	/* The command register (for all pages) */
/* Page 0 register offsets. */
#define EN0_CLDALO	0x01	/* Low byte of current local dma addr  RD */
#define EN0_STARTPG	0x01	/* Starting page of ring bfr WR */
#define EN0_CLDAHI	0x02	/* High byte of current local dma addr  RD */
#define EN0_STOPPG	0x02	/* Ending page +1 of ring bfr WR */
#define EN0_BOUNDARY	0x03	/* Boundary page of ring bfr RD WR */
#define EN0_TSR		0x04	/* Transmit status reg RD */
#define EN0_TPSR	0x04	/* Transmit starting page WR */
#define EN0_NCR		0x05	/* Number of collision reg RD */
#define EN0_TCNTLO	0x05	/* Low  byte of tx byte count WR */
#define EN0_FIFO	0x06	/* FIFO RD */
#define EN0_TCNTHI	0x06	/* High byte of tx byte count WR */
#define EN0_ISR		0x07	/* Interrupt status reg RD WR */
#define EN0_CRDALO	0x08	/* low byte of current remote dma address RD */
#define EN0_RSARLO	0x08	/* Remote start address reg 0 */
#define EN0_CRDAHI	0x09	/* high byte, current remote dma address RD */
#define EN0_RSARHI	0x09	/* Remote start address reg 1 */
#define EN0_RCNTLO	0x0a	/* Remote byte count reg WR */
#define EN0_RCNTHI	0x0b	/* Remote byte count reg WR */
#define EN0_RSR		0x0c	/* rx status reg RD */
#define EN0_RXCR	0x0c	/* RX configuration reg WR */
#define EN0_TXCR	0x0d	/* TX configuration reg WR */
#define EN0_COUNTER0	0x0d	/* Rcv alignment error counter RD */
#define EN0_DCFG	0x0e	/* Data configuration reg WR */
#define EN0_COUNTER1	0x0e	/* Rcv CRC error counter RD */
#define EN0_IMR		0x0f	/* Interrupt mask reg WR */
#define EN0_COUNTER2	0x0f	/* Rcv missed frame error counter RD */

/* Bits in EN0_ISR - Interrupt status register */
#define ENISR_RX	0x01	/* Receiver, no error */
#define ENISR_TX	0x02	/* Transmitter, no error */
#define ENISR_RX_ERR	0x04	/* Receiver, with error */
#define ENISR_TX_ERR	0x08	/* Transmitter, with error */
#define ENISR_OVER	0x10	/* Receiver overwrote the ring */
#define ENISR_COUNTERS	0x20	/* Counters need emptying */
#define ENISR_RDC	0x40	/* remote dma complete */
#define ENISR_RESET	0x80	/* Reset completed */
#define ENISR_ALL	0x3f	/* Interrupts we will enable */

/* Bits in EN0_DCFG - Data config register */
#define ENDCFG_WTS	0x01	/* word transfer mode selection */

/* Page 1 register offsets. */
#define EN1_PHYS   0x01	/* This board's physical enet addr RD WR */
#define EN1_CURPAG 0x07	/* Current memory page RD WR */
#define EN1_MULT   0x08	/* Multicast filter mask array (8 bytes) RD WR */

/* Bits in received packet status byte and EN0_RSR*/
#define ENRSR_RXOK	0x01	/* Received a good packet */
#define ENRSR_CRC	0x02	/* CRC error */
#define ENRSR_FAE	0x04	/* frame alignment error */
#define ENRSR_FO	0x08	/* FIFO overrun */
#define ENRSR_MPA	0x10	/* missed pkt */
#define ENRSR_PHY	0x20	/* physical/multicase address */
#define ENRSR_DIS	0x40	/* receiver disable. set in monitor mode */
#define ENRSR_DEF	0x80	/* deferring */

/* Transmitted packet status, EN0_TSR. */
#define ENTSR_PTX	0x01	/* Packet transmitted without error */
/* The other bits in the TX status register mean:
   0x02	The transmit wasn't deferred.
   0x04	The transmit collided at least once.
   0x08 The transmit collided 16 times, and was deferred.
   0x10	The carrier sense was lost (from the ethernet transceiver)
   0x20 A "FIFO underrun" (internal error) occured during transmit.
   0x40	The collision detect "heartbeat" signal was lost.
   0x80 There was an out-of-window collision.
   */

/* The per-packet-header format. */
struct e8390_pkt_hdr {
  unsigned char status; /* status */
  unsigned char next;   /* pointer to next packet. */
  unsigned short count; /* header + packet lenght in bytes */
};
#endif /* e8390_h */

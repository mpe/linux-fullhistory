
#define DMA_BUFF_SIZE 2200

/* Network statistics, with the same names as 'struct enet_statistics'. */
#define netstats enet_statistics

#define ON 1
#define OFF 0


/* Register offset info, specific to the PI
 * E.g., to read the data port on channel A, use
 *  inportb(pichan[dev].base + CHANA + DATA)
 */
#define CHANB   0   /* Base of channel B regs */
#define CHANA   2   /* Base of channel A regs */

/* 8530 ports on each channel */
#define CTL 0
#define DATA    1

#define DMAEN   0x4 /* Offset off DMA Enable register */

/* Timer chip offsets */
#define TMR0    0x8 /* Offset of timer 0 register */
#define TMR1    0x9 /* Offset of timer 1 register */
#define TMR2    0xA /* Offset of timer 2 register */
#define TMRCMD  0xB /* Offset of timer command register */

/* Timer chip equates */
#define SC0 0x00 /* Select counter 0 */
#define SC1 0x40 /* Select counter 1 */
#define SC2 0x80 /* Select counter 2 */
#define CLATCH  0x00 /* Counter latching operation */
#define MSB 0x20 /* Read/load MSB only */
#define LSB 0x10 /* Read/load LSB only */
#define LSB_MSB 0x30 /* Read/load LSB, then MSB */
#define MODE0   0x00 /* Interrupt on terminal count */
#define MODE1   0x02 /* Programmable one shot */
#define MODE2   0x04 /* Rate generator */
#define MODE3   0x06 /* Square wave rate generator */
#define MODE4   0x08 /* Software triggered strobe */
#define MODE5   0x0a /* Hardware triggered strobe */
#define BCD 0x01 /* BCD counter */

/* DMA controller registers */
#define DMA_STAT    8   /* DMA controller status register */
#define DMA_CMD     8   /* DMA controller command register */
#define DMA_MASK        10  /* DMA controller mask register */
#define DMA_MODE        11  /* DMA controller mode register */
#define DMA_RESETFF 12  /* DMA controller first/last flip flop  */
/* DMA data */
#define DMA_DISABLE (0x04)  /* Disable channel n */
#define DMA_ENABLE  (0x00)  /* Enable channel n */
/* Single transfers, incr. address, auto init, writes, ch. n */
#define DMA_RX_MODE (0x54)
/* Single transfers, incr. address, no auto init, reads, ch. n */
#define DMA_TX_MODE (0x48)

#define SINGLE 3686400
#define DOUBLE 7372800

#define SIOCGPIPARAM		0x5000	/* get PI parameters */
#define SIOCSPIPARAM		0x5001	/* set */
#define SIOCGPIBAUD		0x5002	/* get only baud rate */
#define SIOCSPIBAUD		0x5003	
#define SIOCGPIDMA		0x5004	/* get only DMA */
#define SIOCSPIDMA		0x5005	
#define SIOCGPIIRQ		0x5006	/* get only IRQ */
#define SIOCSPIIRQ		0x5007	

struct pi_req  {
    int cmd;
    int speed;
    int clockmode;
    int txdelay;
    unsigned char persist;
    int slotime; 
    int squeldelay;
    int dmachan;    
    int irq;    
};

#ifdef __KERNEL__

/* Information that needs to be kept for each channel. */
struct pi_local {
    struct netstats stats; /* %%%dp*/
    long open_time;             /* Useless example local info. */
    unsigned long xtal; 

    struct mbuf *rcvbuf;/* Buffer for current rx packet */
    struct mbuf *rxdmabuf1; /* DMA rx buffer */
    struct mbuf *rxdmabuf2; /* DMA rx buffer */

    int bufsiz;         /* Size of rcvbuf */
    char *rcp;          /* Pointer into rcvbuf */

    struct sk_buff_head sndq;  /* Packets awaiting transmission */
    int sndcnt;         /* Number of packets on sndq */
    struct sk_buff *sndbuf;	/* Current buffer being transmitted */
    char *txdmabuf;     /* Transmit DMA buffer */
	char *txptr;		/* Used by B port tx */
	int txcnt;			
    char tstate;        /* Transmitter state */
#define IDLE    0       /* Transmitter off, no data pending */
#define ACTIVE  1       /* Transmitter on, sending data */
#define UNDERRUN 2      /* Transmitter on, flushing CRC */
#define FLAGOUT 3       /* CRC sent - attempt to start next frame */
#define DEFER 4         /* Receive Active - DEFER Transmit */
#define ST_TXDELAY 5    /* Sending leading flags */
#define CRCOUT 6
    char rstate;        /* Set when !DCD goes to 0 (TRUE) */
/* Normal state is ACTIVE if Receive enabled */
#define RXERROR 2       /* Error -- Aborting current Frame */
#define RXABORT 3       /* ABORT sequence detected */
#define TOOBIG 4        /* too large a frame to store */
    int dev;            /* Device number */
    int base;       /* Base of I/O registers */
    int cardbase;     /* Base address of card */
    int stata;        /* address of Channel A status regs */
    int statb;        /* address of Channel B status regs */
    int speed;        /* Line speed, bps */
    int clockmode;    /* tapr 9600 modem clocking option */
    int txdelay;      /* Transmit Delay 10 ms/cnt */
    unsigned char persist;       /* Persistence (0-255) as a % */
    int slotime;      /* Delay to wait on persistence hit */
    int squeldelay;   /* Delay after XMTR OFF for squelch tail */
    struct iface *iface;    /* Associated interface */
    int dmachan;           /* DMA channel for this port */
};

#endif

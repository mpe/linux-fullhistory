/*
 * pt.h: Linux device driver for the Gracilis PackeTwin
 * Copyright (C) 1995 Craig Small VK2XLZ (vk2xlz@vk2xlz.ampr.org.)
 *
 * Please read the notice appearing at the top of the file pt.c
 */
#define DMA_BUFF_SIZE 2200

/* Network statistics, with the same names as 'struct enet_statistics'. */
#define netstats enet_statistics

#define ON 1
#define OFF 0


/* Register offset info, specific to the PT
 * E.g., to read the data port on channel A, use
 *  inportb(pichan[dev].base + CHANA + DATA)
 */
#define CHANB   0   /* Base of channel B regs */
#define CHANA   2   /* Base of channel A regs */

/* 8530 ports on each channel */
#define CTL 0
#define DATA    1

#define DMAEN   0x8 /* Offset off DMA Enable register */

/* Timer chip offsets */
#define TMR0    0x4 /* Offset of timer 0 register */
#define TMR1    0x5 /* Offset of timer 1 register */
#define TMR2    0x6 /* Offset of timer 2 register */
#define TMRCMD  0x7 /* Offset of timer command register */
#define INT_REG	0x8
#define TMR1CLR 0x9
#define TMR2CLR 0xa

/* Interrupt register equates */
#define PT_SCC_MSK	0x1
#define PT_TMR1_MSK	0x2
#define PT_TMR2_MSK	0x4

/* Serial/interrupt register equates */
#define PT_DTRA_ON	0x1
#define PT_DTRB_ON	0x2
#define PT_EXTCLKA	0x4
#define PT_EXTCLKB	0x8
#define PT_LOOPA_ON	0x10
#define PT_LOOPB_ON	0x20
#define PT_EI		0x80

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

/* Write registers */
#define DMA_CFG		0x08
#define SERIAL_CFG	0x09
#define INT_CFG		0x09	/* shares with serial config */
#define DMA_CLR_FF	0x0a

#define SINGLE 3686400
#define DOUBLE 7372800
#define XTAL   ((long) 6144000L)

#define SIOCGPIPARAM		0x5000	/* get PI parameters */
#define SIOCSPIPARAM		0x5001	/* set */
#define SIOCGPIBAUD		0x5002	/* get only baud rate */
#define SIOCSPIBAUD		0x5003	
#define SIOCGPIDMA		0x5004	/* get only DMA */
#define SIOCSPIDMA		0x5005	
#define SIOCGPIIRQ		0x5006	/* get only IRQ */
#define SIOCSPIIRQ		0x5007	

struct pt_req  {
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

/* SCC Interrupt vectors, if we have set 'status low' */
#define CHBTxIV		0x00
#define CHBEXTIV	0x02
#define CHBRxIV		0x04
#define CHBSRCIV	0x06
#define CHATxIV		0x08
#define CHAEXTIV	0x0a
#define CHARxIV		0x0c
#define CHASRCIV	0x0e


#ifdef __KERNEL__

/* Information that needs to be kept for each channel. */
struct pt_local {
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
    struct sk_buff *sndbuf;/* Current buffer being transmitted */
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
    char saved_RR0;	/* The saved version of RR) that we compare with */
    int nrzi;			/* Do we use NRZI (or NRZ) */
};

#endif

/* $Id: scc.h,v 1.26 1996/10/09 16:35:56 jreuter Exp jreuter $ */

#ifndef	_SCC_H
#define	_SCC_H

#include <linux/if_ether.h>

/* selection of hardware types */

#define PA0HZP		0x00	/* hardware type for PA0HZP SCC card and compatible */
#define EAGLE         	0x01    /* hardware type for EAGLE card */
#define PC100		0x02	/* hardware type for PC100 card */
#define PRIMUS		0x04	/* hardware type for PRIMUS-PC (DG9BL) card */
#define DRSI		0x08	/* hardware type for DRSI PC*Packet card */
#define BAYCOM		0x10	/* hardware type for BayCom (U)SCC */

/* Paranoia check... */

#define SCC_PARANOIA_CHECK	/* tell the user if something is going wrong */

/* DEV ioctl() commands */

#define SIOCSCCRESERVED (SIOCDEVPRIVATE+0)
#define SIOCSCCCFG	(SIOCDEVPRIVATE+1)
#define SIOCSCCINI	(SIOCDEVPRIVATE+2)
#define SIOCSCCCHANINI	(SIOCDEVPRIVATE+3)
#define SIOCSCCSMEM	(SIOCDEVPRIVATE+4)
#define SIOCSCCGKISS	(SIOCDEVPRIVATE+5)
#define SIOCSCCSKISS	(SIOCDEVPRIVATE+6)
#define SIOCSCCGSTAT	(SIOCDEVPRIVATE+7)

/* magic number */

#define SCC_MAGIC	0x8530		/* ;-) */

/* KISS state machine */

#define	KISS_IDLE	0
#define KISS_DATA	1
#define KISS_ESCAPE	2
#define KISS_RXFRAME	3

/* Device parameter control (from WAMPES) */

#define	PARAM_TXDELAY	1
#define	PARAM_PERSIST	2
#define	PARAM_SLOTTIME	3
#define	PARAM_TXTAIL	4
#define	PARAM_FULLDUP	5
#define PARAM_SOFTDCD	6	/* was: PARAM_HW */
#define PARAM_MUTE	7	/* ??? */
#define PARAM_DTR       8
#define PARAM_RTS	9
#define PARAM_SPEED     10
#define PARAM_ENDDELAY	11	/* ??? */
#define PARAM_GROUP     12
#define PARAM_IDLE      13
#define PARAM_MIN       14
#define	PARAM_MAXKEY	15
#define PARAM_WAIT      16
#define PARAM_MAXDEFER	17
#define PARAM_TX        18
#define PARAM_HWEVENT	31
#define PARAM_RETURN	255	/* reset kiss mode */

/* fulldup parameter */

#define KISS_DUPLEX_HALF	0	/* normal CSMA operation */
#define KISS_DUPLEX_FULL	1	/* fullduplex, key down trx after transmission */
#define KISS_DUPLEX_LINK	2	/* fullduplex, key down trx after 'idletime' sec */
#define KISS_DUPLEX_OPTIMA	3	/* fullduplex, let the protocol layer control the hw */

/* misc. parameters */

#define TIMER_OFF	65535U	/* to switch off timers */
#define NO_SUCH_PARAM	65534U	/* param not implemented */

/* HWEVENT parameter */

#define HWEV_DCD_ON	0
#define HWEV_DCD_OFF	1
#define HWEV_ALL_SENT	2

/* channel grouping */

#define RXGROUP		0x100	/* if set, only tx when all channels clear */
#define TXGROUP		0x200	/* if set, don't transmit simultaneously */

/* Tx/Rx clock sources */

#define CLK_DPLL	0	/* normal halfduplex operation */
#define CLK_EXTERNAL	1	/* external clocking (G3RUH/DF9IC modems) */
#define CLK_DIVIDER	2	/* Rx = DPLL, Tx = divider (fullduplex with */
				/* modems without clock regeneration */

/* Tx state */

#define TXS_IDLE	0	/* Transmitter off, no data pending */
#define TXS_BUSY	1	/* waiting for permission to send / tailtime */
#define TXS_ACTIVE	2	/* Transmitter on, sending data */
#define TXS_NEWFRAME	3	/* reset CRC and send (next) frame */
#define TXS_IDLE2	4	/* Transmitter on, no data pending */
#define TXS_WAIT	5	/* Waiting for Mintime to expire */
#define TXS_TIMEOUT	6	/* We had a transmission timeout */

#define TX_ON		1	/* command for scc_key_trx() */
#define TX_OFF		0	/* dto */

/* Vector masks in RR2B */

#define VECTOR_MASK	0x06
#define TXINT		0x00
#define EXINT		0x02
#define RXINT		0x04
#define SPINT		0x06

typedef unsigned long io_port;	/* type definition for an 'io port address' */

#ifdef SCC_DELAY
#define Inb(port)	inb_p(port)
#define Outb(port, val)	outb_p(val, port)
#else
#define Inb(port)	inb(port)
#define Outb(port, val)	outb(val, port)
#endif

#define TIMER_OFF 65535U

/* SCC channel control structure for KISS */

struct scc_kiss {
	unsigned char txdelay;		/* Transmit Delay 10 ms/cnt */
	unsigned char persist;		/* Persistence (0-255) as a % */
	unsigned char slottime;		/* Delay to wait on persistence hit */
	unsigned char tailtime;		/* Delay after last byte written */
	unsigned char fulldup;		/* Full Duplex mode 0=CSMA 1=DUP 2=ALWAYS KEYED */
	unsigned char waittime;		/* Waittime before any transmit attempt */
	unsigned int  maxkeyup;		/* Maximum time to transmit (seconds) */
	unsigned char mintime;		/* Minimal offtime after MAXKEYUP timeout (seconds) */
	unsigned int  idletime;		/* Maximum idle time in ALWAYS KEYED mode (seconds) */
	unsigned int  maxdefer;		/* Timer for CSMA channel busy limit */
	unsigned char tx_inhibit;	/* Transmit is not allowed when set */	
	unsigned char group;		/* Group ID for AX.25 TX interlocking */
	unsigned char mode;		/* 'normal' or 'hwctrl' mode (unused) */
	unsigned char softdcd;		/* Use DPLL instead of DCD pin for carrier detect */
};


/* SCC statistical information */

struct scc_stat {
        long rxints;            /* Receiver interrupts */
        long txints;            /* Transmitter interrupts */
        long exints;            /* External/status interrupts */
        long spints;            /* Special receiver interrupts */

        long txframes;          /* Packets sent */
        long rxframes;          /* Number of Frames Actually Received */
        long rxerrs;            /* CRC Errors */
        long txerrs;		/* KISS errors */
        
	unsigned int nospace;	/* "Out of buffers" */
	unsigned int rx_over;	/* Receiver Overruns */
	unsigned int tx_under;	/* Transmitter Underruns */

	unsigned int tx_state;	/* Transmitter state */
	int tx_queued;		/* tx frames enqueued */

	unsigned int maxqueue;	/* allocated tx_buffers */
	unsigned int bufsize;	/* used buffersize */
};


struct scc_modem {
	long speed;		/* Line speed, bps */
	char clocksrc;		/* 0 = DPLL, 1 = external, 2 = divider */
	char nrz;		/* NRZ instead of NRZI */	
};

struct scc_kiss_cmd {
	int  	 command;	/* one of the KISS-Commands defined above */
	unsigned param;		/* KISS-Param */
};

struct scc_hw_config {
	io_port data_a;		/* data port channel A */
	io_port ctrl_a;		/* control port channel A */
	io_port data_b;		/* data port channel B */
	io_port ctrl_b;		/* control port channel B */
	io_port vector_latch;	/* INTACK-Latch (#) */
	io_port	special;	/* special function port */

	int	irq;		/* irq */
	long	clock;		/* clock */
	char	option;		/* command for function port */

	char brand;		/* hardware type */
	char escc;		/* use ext. features of a 8580/85180/85280 */
};

/* (#) only one INTACK latch allowed. */


struct scc_mem_config {
	unsigned int dummy;
	unsigned int bufsize;
};


/* SCC channel structure */

struct scc_channel {
	int magic;			/* magic word */
	
	int init;			/* channel exists? */

	struct device *dev;		/* link to device control structure */
	struct enet_statistics dev_stat;/* device statistics */

	char brand;			/* manufacturer of the board */
	long clock;			/* used clock */
	
	io_port ctrl;			/* I/O address of CONTROL register */
	io_port	data;			/* I/O address of DATA register */
	io_port special;		/* I/O address of special function port */
	int irq;			/* Number of Interrupt */
	
	char option;
	char enhanced;			/* Enhanced SCC support */

	unsigned char wreg[16]; 	/* Copy of last written value in WRx */
	unsigned char status;		/* Copy of R0 at last external interrupt */

        struct scc_kiss kiss;		/* control structure for KISS params */
        struct scc_stat stat;		/* statistical information */
        struct scc_modem modem; 	/* modem information */
        
        struct sk_buff *tx_next_buff;	/* next tx buffer */
        struct sk_buff *rx_buff;	/* pointer to frame currently received */
        struct sk_buff *tx_buff;	/* pointer to frame currently transmitted */

	/* Timer */

	struct timer_list tx_t;		/* tx timer for this channel */
	struct timer_list tx_wdog;	/* tx watchdogs */
};

int scc_init(void);
#endif

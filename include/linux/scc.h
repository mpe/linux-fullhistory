/* $Id: scc.h,v 1.15 1995/11/16 20:19:26 jreuter Exp jreuter $ */

#ifndef	_SCC_H
#define	_SCC_H

/* selection of hardware types */

#define PA0HZP		0x00	/* hardware type for PA0HZP SCC card and compatible */
#define EAGLE         	0x01    /* hardware type for EAGLE card */
#define PC100		0x02	/* hardware type for PC100 card */
#define PRIMUS		0x04	/* hardware type for PRIMUS-PC (DG9BL) card */
#define DRSI		0x08	/* hardware type for DRSI PC*Packet card */
#define BAYCOM		0x10	/* hardware type for BayCom (U)SCC */

/* Paranoia check... */

#define SCC_PARANOIA_CHECK	/* tell the user if something is going wrong */

/* ioctl() commands */

#define TIOCSCCCFG	0x2200		/* set hardware parameters */
#define TIOCSCCINI	0x2201		/* init driver */
#define TIOCCHANINI	0x2202		/* init channel */

#define TIOCCHANMEM	0x2210		/* adjust buffer pools */

#define TIOCGKISS	0x2282		/* get kiss parameter */
#define TIOCSKISS	0x2283		/* set kiss parameter */

#define TIOCSCCSTAT	0x2284		/* get scc status */


/* magic number */

#define SCC_MAGIC	0x8530		/* ;-) */

/* KISS protocol flags */
#define FEND	192
#define FESC	219
#define TFEND	220
#define TFESC	221

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
#define PARAM_SLIP	19
#define PARAM_RETURN	255	/* reset kiss mode */

#define TIMER_OFF	65535U	/* to switch off timers */
#define NO_SUCH_PARAM	65534U	/* param not implemented */

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

#define TX_ON		1	/* command for scc_key_trx() */
#define TX_OFF		0	/* dto */

/* Buffer management */

#define BT_RECEIVE  1		/* buffer allocated by receive */
#define BT_TRANSMIT 2		/* buffer allocated by transmit */

#define NULLBUF  (struct mbuf *)0
#define NULLBUFP (struct mbuf **)0


typedef unsigned short io_port;	/* type definition for an 'io port address' */
typedef unsigned short ioaddr;  /* old def */

#ifdef SCC_DELAY
#define Inb(port)	inb_p(port)
#define Outb(port, val)	outb_p(val, port)
#else
#define Inb(port)	inb(port)
#define Outb(port, val)	outb(val, port)
#endif

/* some nasty macros (esp. Expired) */

#define TIMER_STOPPED 65535U
#define Running(k) (scc->k != TIMER_STOPPED)
#define Expired(k) (scc->k != TIMER_STOPPED) && (!(scc->k) || (--(scc->k) == 0))
#define Stop_Timer(k) scc->k = TIMER_STOPPED


/* Basic message buffer structure */

struct mbuf {
	struct mbuf *next;	/* Link to next buffer */
	struct mbuf *prev;	/* Link to previous buffer */
	
	int cnt;		/* Number of bytes stored in buffer */
	unsigned char *rw_ptr;	/* read-write pointer */
	unsigned char data[0];	/* anchor for allocated buffer */
};
	
/* SCC channel control structure for KISS */

struct scc_kiss {
	unsigned char txdelay;		/* Transmit Delay 10 ms/cnt */
	unsigned char persist;		/* Persistence (0-255) as a % */
	unsigned char slottime;		/* Delay to wait on persistence hit */
	unsigned char tailtime;		/* Delay after XMTR OFF */
	unsigned char fulldup;		/* Full Duplex mode 0=CSMA 1=DUP 2=ALWAYS KEYED */
	unsigned char waittime;		/* Waittime before any transmit attempt */
	unsigned int  maxkeyup;		/* Maximum time to transmit (seconds) */
	unsigned char mintime;		/* Minimal offtime after MAXKEYUP timeout */
	unsigned int  idletime;		/* Maximum idle time in ALWAYS KEYED mode (seconds) */
	unsigned int  maxdefer;		/* Timer for CSMA channel busy limit */
	unsigned char tx_inhibit;	/* Transmit is not allowed when set */	
	unsigned char group;		/* group ID for AX.25 TX interlocking */
	unsigned char not_slip;		/* set to zero: use SLIP instead of KISS */
	unsigned char softdcd;		/* use DPLL instead of DCD pin for carrier detect */
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
	
	char tx_kiss_state;	/* state of the kiss interpreter */
	char rx_kiss_state;	/* state of the kiss encoder */
	
	int tx_queued;		/* tx frames enqueued */
	int rx_queued; 		/* rx frames enqueued */
	
	unsigned int rxbuffers;	/* allocated rx_buffers */
	unsigned int txbuffers;	/* allocated tx_buffers */
	unsigned int bufsize;	/* used buffersize */
};


struct scc_modem {
	long speed;		/* Line speed, bps */
	char clocksrc;		/* 0 = DPLL, 1 = external, 2 = divider */
	char nrz;		/* NRZ instead of NRZI */	
};

struct ioctl_command {
	int  	 command;	/* one of the KISS-Commands defined above */
	unsigned param;		/* KISS-Param */
};

/* currently unused */

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

struct scc_mem_config {
	unsigned int rxbuffers;
	unsigned int txbuffers;
	unsigned int bufsize;
};

/* (#) only one INTACK latch allowed. */
	

/* SCC channel structure */

struct scc_channel {
	int magic;		/* magic word */
	
	int init;		/* channel exists? */
	struct tty_struct *tty; /* link to tty control structure */
	char tty_opened;	/* No. of open() calls... */
	char throttled;		/* driver is throttled  */
		
	char brand;		/* manufacturer of the board */
	long clock;		/* used clock */
	
	io_port ctrl;		/* I/O address of CONTROL register */
	io_port	data;		/* I/O address of DATA register */
	io_port special;	/* I/O address of special function port */
	
	char option;
	char enhanced;		/* Enhanced SCC support */

	unsigned char wreg[16]; /* Copy of last written value in WRx */
	unsigned char status;	/* Copy of R0 at last external interrupt */

        struct scc_kiss kiss;	/* control structure for KISS params */
        struct scc_stat stat;	/* statistical information */
        struct scc_modem modem; /* modem information */
        
        struct mbuf *rx_buffer_pool; /* free buffers for rx/tx frames are */
        struct mbuf *tx_buffer_pool; /* linked in these ring chains */
        
        struct mbuf *rx_queue;	/* chain of received frames */
        struct mbuf *tx_queue;	/* chain of frames due to transmit */
        struct mbuf *rx_bp;	/* pointer to frame currently received */
        struct mbuf *tx_bp;	/* pointer to frame currently transmitted */
        
        struct mbuf *kiss_decode_bp; /* frame we are receiving from tty */
        struct mbuf *kiss_encode_bp; /* frame we are sending to tty */
	
	/* Timer */
	
	struct timer_list tx_t;	/* tx timer for this channel */
	struct timer_list rx_t; /* rx timer */

	/* rx timer counters */
	
	unsigned int t_dwait;	/* wait time (DWAIT) */
	unsigned int t_slot;	/* channel sample frequency */
	unsigned int t_txdel;	/* TX delay */
	unsigned int t_tail;	/* tail time */
	unsigned int t_maxk;	/* max. key up */
	unsigned int t_min;	/* minimal key up */
	unsigned int t_idle;	/* */
	unsigned int t_mbusy;	/* time until defer if channel busy */	 	
};


/* 8530 Serial Communications Controller Register definitions */
#define	FLAG	0x7e

/* Write Register 0 */
#define	R0	0		/* Register selects */
#define	R1	1
#define	R2	2
#define	R3	3
#define	R4	4
#define	R5	5
#define	R6	6
#define	R7	7
#define	R8	8
#define	R9	9
#define	R10	10
#define	R11	11
#define	R12	12
#define	R13	13
#define	R14	14
#define	R15	15

#define	NULLCODE	0	/* Null Code */
#define	POINT_HIGH	0x8	/* Select upper half of registers */
#define	RES_EXT_INT	0x10	/* Reset Ext. Status Interrupts */
#define	SEND_ABORT	0x18	/* HDLC Abort */
#define	RES_RxINT_FC	0x20	/* Reset RxINT on First Character */
#define	RES_Tx_P	0x28	/* Reset TxINT Pending */
#define	ERR_RES		0x30	/* Error Reset */
#define	RES_H_IUS	0x38	/* Reset highest IUS */

#define	RES_Rx_CRC	0x40	/* Reset Rx CRC Checker */
#define	RES_Tx_CRC	0x80	/* Reset Tx CRC Checker */
#define	RES_EOM_L	0xC0	/* Reset EOM latch */

/* Write Register 1 */

#define	EXT_INT_ENAB	0x1	/* Ext Int Enable */
#define	TxINT_ENAB	0x2	/* Tx Int Enable */
#define	PAR_SPEC	0x4	/* Parity is special condition */

#define	RxINT_DISAB	0	/* Rx Int Disable */
#define	RxINT_FCERR	0x8	/* Rx Int on First Character Only or Error */
#define	INT_ALL_Rx	0x10	/* Int on all Rx Characters or error */
#define	INT_ERR_Rx	0x18	/* Int on error only */

#define	WT_RDY_RT	0x20	/* Wait/Ready on R/T */
#define	WT_FN_RDYFN	0x40	/* Wait/FN/Ready FN */
#define	WT_RDY_ENAB	0x80	/* Wait/Ready Enable */

/* Write Register 2 (Interrupt Vector) */

/* Write Register 3 */

#define	RxENABLE	0x1	/* Rx Enable */
#define	SYNC_L_INH	0x2	/* Sync Character Load Inhibit */
#define	ADD_SM		0x4	/* Address Search Mode (SDLC) */
#define	RxCRC_ENAB	0x8	/* Rx CRC Enable */
#define	ENT_HM		0x10	/* Enter Hunt Mode */
#define	AUTO_ENAB	0x20	/* Auto Enables */
#define	Rx5		0x0	/* Rx 5 Bits/Character */
#define	Rx7		0x40	/* Rx 7 Bits/Character */
#define	Rx6		0x80	/* Rx 6 Bits/Character */
#define	Rx8		0xc0	/* Rx 8 Bits/Character */

/* Write Register 4 */

#define	PAR_ENA		0x1	/* Parity Enable */
#define	PAR_EVEN	0x2	/* Parity Even/Odd* */

#define	SYNC_ENAB	0	/* Sync Modes Enable */
#define	SB1		0x4	/* 1 stop bit/char */
#define	SB15		0x8	/* 1.5 stop bits/char */
#define	SB2		0xc	/* 2 stop bits/char */

#define	MONSYNC		0	/* 8 Bit Sync character */
#define	BISYNC		0x10	/* 16 bit sync character */
#define	SDLC		0x20	/* SDLC Mode (01111110 Sync Flag) */
#define	EXTSYNC		0x30	/* External Sync Mode */

#define	X1CLK		0x0	/* x1 clock mode */
#define	X16CLK		0x40	/* x16 clock mode */
#define	X32CLK		0x80	/* x32 clock mode */
#define	X64CLK		0xC0	/* x64 clock mode */

/* Write Register 5 */

#define	TxCRC_ENAB	0x1	/* Tx CRC Enable */
#define	RTS		0x2	/* RTS */
#define	SDLC_CRC	0x4	/* SDLC/CRC-16 */
#define	TxENAB		0x8	/* Tx Enable */
#define	SND_BRK		0x10	/* Send Break */
#define	Tx5		0x0	/* Tx 5 bits (or less)/character */
#define	Tx7		0x20	/* Tx 7 bits/character */
#define	Tx6		0x40	/* Tx 6 bits/character */
#define	Tx8		0x60	/* Tx 8 bits/character */
#define	DTR		0x80	/* DTR */

/* Write Register 6 (Sync bits 0-7/SDLC Address Field) */

/* Write Register 7 (Sync bits 8-15/SDLC 01111110) */

/* Write Register 8 (transmit buffer) */

/* Write Register 9 (Master interrupt control) */
#define	VIS	1	/* Vector Includes Status */
#define	NV	2	/* No Vector */
#define	DLC	4	/* Disable Lower Chain */
#define	MIE	8	/* Master Interrupt Enable */
#define	STATHI	0x10	/* Status high */
#define	NORESET	0	/* No reset on write to R9 */
#define	CHRB	0x40	/* Reset channel B */
#define	CHRA	0x80	/* Reset channel A */
#define	FHWRES	0xc0	/* Force hardware reset */

/* Write Register 10 (misc control bits) */
#define	BIT6	1	/* 6 bit/8bit sync */
#define	LOOPMODE 2	/* SDLC Loop mode */
#define	ABUNDER	4	/* Abort/flag on SDLC xmit underrun */
#define	MARKIDLE 8	/* Mark/flag on idle */
#define	GAOP	0x10	/* Go active on poll */
#define	NRZ	0	/* NRZ mode */
#define	NRZI	0x20	/* NRZI mode */
#define	FM1	0x40	/* FM1 (transition = 1) */
#define	FM0	0x60	/* FM0 (transition = 0) */
#define	CRCPS	0x80	/* CRC Preset I/O */

/* Write Register 11 (Clock Mode control) */
#define	TRxCXT	0	/* TRxC = Xtal output */
#define	TRxCTC	1	/* TRxC = Transmit clock */
#define	TRxCBR	2	/* TRxC = BR Generator Output */
#define	TRxCDP	3	/* TRxC = DPLL output */
#define	TRxCOI	4	/* TRxC O/I */
#define	TCRTxCP	0	/* Transmit clock = RTxC pin */
#define	TCTRxCP	8	/* Transmit clock = TRxC pin */
#define	TCBR	0x10	/* Transmit clock = BR Generator output */
#define	TCDPLL	0x18	/* Transmit clock = DPLL output */
#define	RCRTxCP	0	/* Receive clock = RTxC pin */
#define	RCTRxCP	0x20	/* Receive clock = TRxC pin */
#define	RCBR	0x40	/* Receive clock = BR Generator output */
#define	RCDPLL	0x60	/* Receive clock = DPLL output */
#define	RTxCX	0x80	/* RTxC Xtal/No Xtal */

/* Write Register 12 (lower byte of baud rate generator time constant) */

/* Write Register 13 (upper byte of baud rate generator time constant) */

/* Write Register 14 (Misc control bits) */
#define	BRENABL	1	/* Baud rate generator enable */
#define	BRSRC	2	/* Baud rate generator source */
#define	DTRREQ	4	/* DTR/Request function */
#define	AUTOECHO 8	/* Auto Echo */
#define	LOOPBAK	0x10	/* Local loopback */
#define	SEARCH	0x20	/* Enter search mode */
#define	RMC	0x40	/* Reset missing clock */
#define	DISDPLL	0x60	/* Disable DPLL */
#define	SSBR	0x80	/* Set DPLL source = BR generator */
#define	SSRTxC	0xa0	/* Set DPLL source = RTxC */
#define	SFMM	0xc0	/* Set FM mode */
#define	SNRZI	0xe0	/* Set NRZI mode */

/* Write Register 15 (external/status interrupt control) */
#define	ZCIE	2	/* Zero count IE */
#define	DCDIE	8	/* DCD IE */
#define	SYNCIE	0x10	/* Sync/hunt IE */
#define	CTSIE	0x20	/* CTS IE */
#define	TxUIE	0x40	/* Tx Underrun/EOM IE */
#define	BRKIE	0x80	/* Break/Abort IE */


/* Read Register 0 */
#define	Rx_CH_AV	0x1	/* Rx Character Available */
#define	ZCOUNT		0x2	/* Zero count */
#define	Tx_BUF_EMP	0x4	/* Tx Buffer empty */
#define	DCD		0x8	/* DCD */
#define	SYNC_HUNT	0x10	/* Sync/hunt */
#define	CTS		0x20	/* CTS */
#define	TxEOM		0x40	/* Tx underrun */
#define	BRK_ABRT	0x80	/* Break/Abort */

/* Read Register 1 */
#define	ALL_SNT		0x1	/* All sent */
/* Residue Data for 8 Rx bits/char programmed */
#define	RES3		0x8	/* 0/3 */
#define	RES4		0x4	/* 0/4 */
#define	RES5		0xc	/* 0/5 */
#define	RES6		0x2	/* 0/6 */
#define	RES7		0xa	/* 0/7 */
#define	RES8		0x6	/* 0/8 */
#define	RES18		0xe	/* 1/8 */
#define	RES28		0x0	/* 2/8 */
/* Special Rx Condition Interrupts */
#define	PAR_ERR		0x10	/* Parity error */
#define	Rx_OVR		0x20	/* Rx Overrun Error */
#define	CRC_ERR		0x40	/* CRC/Framing Error */
#define	END_FR		0x80	/* End of Frame (SDLC) */

/* Read Register 2 (channel B only) - Interrupt vector */

#define VECTOR_MASK	0x06

#define TXINT   0x00
#define EXINT   0x02
#define RXINT   0x04
#define SPINT   0x06


/* Read Register 3 (interrupt pending register) ch a only */
#define	CHBEXT	0x1		/* Channel B Ext/Stat IP */
#define	CHBTxIP	0x2		/* Channel B Tx IP */
#define	CHBRxIP	0x4		/* Channel B Rx IP */
#define	CHAEXT	0x8		/* Channel A Ext/Stat IP */
#define	CHATxIP	0x10		/* Channel A Tx IP */
#define	CHARxIP	0x20		/* Channel A Rx IP */

/* Read Register 8 (receive data register) */

/* Read Register 10  (misc status bits) */
#define	ONLOOP	2		/* On loop */
#define	LOOPSEND 0x10		/* Loop sending */
#define	CLK2MIS	0x40		/* Two clocks missing */
#define	CLK1MIS	0x80		/* One clock missing */

/* Read Register 12 (lower byte of baud rate generator constant) */

/* Read Register 13 (upper byte of baud rate generator constant) */

/* Read Register 15 (value of WR 15) */


/* 8536 register definitions */

#define CIO_MICR	0x00	/* Master interrupt control register */
#define CIO_MCCR	0x01	/* Master configuration control register */
#define CIO_CTMS1	0x1c	/* Counter/timer mode specification #1 */
#define CIO_CTMS2	0x1d	/* Counter/timer mode specification #2 */
#define CIO_CTMS3	0x1e	/* Counter/timer mode specification #3 */
#define CIO_IVR 	0x04	/* Interrupt vector register */

#define CIO_CSR1	0x0a	/* Command and status register CTC #1 */
#define CIO_CSR2	0x0b	/* Command and status register CTC #2 */
#define CIO_CSR3	0x0c	/* Command and status register CTC #3 */

#define CIO_CT1MSB	0x16	/* CTC #1 Timer constant - MSB */
#define CIO_CT1LSB	0x17	/* CTC #1 Timer constant - LSB */
#define CIO_CT2MSB	0x18	/* CTC #2 Timer constant - MSB */
#define CIO_CT2LSB	0x19	/* CTC #2 Timer constant - LSB */
#define CIO_CT3MSB	0x1a	/* CTC #3 Timer constant - MSB */
#define CIO_CT3LSB	0x1b	/* CTC #3 Timer constant - LSB */
#define CIO_PDCA	0x23	/* Port A data direction control */
#define CIO_PDCB	0x2b	/* Port B data direction control */

#define CIO_GCB 	0x04	/* CTC Gate command bit */
#define CIO_TCB 	0x02	/* CTC Trigger command bit */
#define CIO_IE		0xc0	/* CTC Interrupt enable (set) */
#define CIO_CIP 	0x20	/* CTC Clear interrupt pending */
#define CIO_IP		0x20	/* CTC Interrupt pending */


/* 8580/85180/85280 Enhanced SCC register definitions */

/* Write Register 7' (SDLC/HDLC Programmable Enhancements) */
#define AUTOTXF	0x01		/* Auto Tx Flag */
#define AUTOEOM 0x02		/* Auto EOM Latch Reset */
#define AUTORTS	0x04		/* Auto RTS */
#define TXDNRZI 0x08		/* TxD Pulled High in SDLC NRZI mode */
#define FASTDTR 0x10		/* Fast DTR/REQ Mode */
#define CRCCBCR	0x20		/* CRC Check Bytes Completely Received */
#define EXTRDEN	0x40		/* Extended Read Enabled */

/* Write Register 15 (external/status interrupt control) */
#define SHDLCE	1		/* SDLC/HDLC Enhancements Enable */
#define FIFOE	4		/* FIFO Enable */

/* Read Register 6 (frame status FIFO) */
#define BCLSB	0xff		/* LSB of 14 bits count */

/* Read Register 7 (frame status FIFO) */
#define BCMSB	0x3f		/* MSB of 14 bits count */
#define FDA	0x40		/* FIFO Data Available Status */
#define FOY	0x80		/* FIFO Overflow Status */

#endif	/* _SCC_H */

/* global functions */

#ifdef PREV_LINUX_1_3_33
extern long scc_init(long kmem_start);
#else
extern int scc_init(void);
#endif

struct cyclades_card {
    int base_addr;
    int irq;
    int num_chips; /* implies card type, 0 if card is absent */
    int first_line; /* line number of first channel of first chip on card */
};

struct cyclades_chip {
  int filler;
};

struct cyclades_monitor {
        unsigned long           int_count;
        unsigned long           char_count;
        unsigned long           char_max;
        unsigned long           char_last;
};

/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

struct cyclades_port {
	int                     magic;
	int                     type;
	int			card;
	int			line;
	int			flags; 		/* defined in tty.h */
	struct tty_struct 	*tty;
	int			read_status_mask;
	int			timeout;
	int			xmit_fifo_size;
	int                     cor1,cor2,cor3,cor4,cor5;
	int                     tbpr,tco,rbpr,rco;
	int			ignore_status_mask;
	int			close_delay;
	int			IER; 	/* Interrupt Enable Register */
	int			event;
	unsigned long		last_active;
	int			count;	/* # of fd on device */
	int                     x_char; /* to be pushed out ASAP */
	int                     x_break;
	int			blocked_open; /* # of blocked opens */
	long			session; /* Session of opening process */
	long			pgrp; /* pgrp of opening process */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
        int                     default_threshold;
        int                     default_timeout;
	struct tq_struct	tqueue;
	struct termios		normal_termios;
	struct termios		callout_termios;
	struct wait_queue	*open_wait;
	struct wait_queue	*close_wait;
        struct cyclades_monitor mon;
};

#define CYCLADES_MAGIC  0x4359

#define CYGETMON                0x435901
#define CYGETTHRESH             0x435902
#define CYSETTHRESH             0x435903
#define CYGETDEFTHRESH          0x435904
#define CYSETDEFTHRESH          0x435905
#define CYGETTIMEOUT            0x435906
#define CYSETTIMEOUT            0x435907
#define CYGETDEFTIMEOUT         0x435908
#define CYSETDEFTIMEOUT         0x435909

/*
 * Events are used to schedule things to happen at timer-interrupt
 * time, instead of at cy interrupt time.
 */
#define Cy_EVENT_READ_PROCESS	0
#define Cy_EVENT_WRITE_WAKEUP	1
#define Cy_EVENT_HANGUP		2
#define Cy_EVENT_BREAK		3
#define Cy_EVENT_OPEN_WAKEUP	4



#define CyMaxChipsPerCard 8

/**** CD1400 registers ****/

#define CyRegSize  0x0400
#define Cy_HwReset 0x1400
#define Cy_ClrIntr 0x1800

/* Global Registers */

#define CyGFRCR		(0x40*2)
#define      CyRevE		(44)
#define CyCAR		(0x68*2)
#define      CyCHAN_0		(0x00)
#define      CyCHAN_1		(0x01)
#define      CyCHAN_2		(0x02)
#define      CyCHAN_3		(0x03)
#define CyGCR		(0x4B*2)
#define      CyCH0_SERIAL	(0x00)
#define      CyCH0_PARALLEL	(0x80)
#define CySVRR		(0x67*2)
#define      CySRModem		(0x04)
#define      CySRTransmit	(0x02)
#define      CySRReceive	(0x01)
#define CyRICR		(0x44*2)
#define CyTICR		(0x45*2)
#define CyMICR		(0x46*2)
#define      CyICR0		(0x00)
#define      CyICR1		(0x01)
#define      CyICR2		(0x02)
#define      CyICR3		(0x03)
#define CyRIR		(0x6B*2)
#define CyTIR		(0x6A*2)
#define CyMIR		(0x69*2)
#define      CyIRDirEq		(0x80)
#define      CyIRBusy		(0x40)
#define      CyIRUnfair		(0x20)
#define      CyIRContext	(0x1C)
#define      CyIRChannel	(0x03)
#define CyPPR 		(0x7E*2)
#define      CyCLOCK_20_1MS	(0x27)
#define      CyCLOCK_25_1MS	(0x31)

/* Virtual Registers */

#define CyRIVR		(0x43*2)
#define CyTIVR		(0x42*2)
#define CyMIVR		(0x41*2)
#define      CyIVRMask (0x07)
#define      CyIVRRxEx (0x07)
#define      CyIVRRxOK (0x03)
#define      CyIVRTxOK (0x02)
#define      CyIVRMdmOK (0x01)
#define CyTDR		(0x63*2)
#define CyRDSR		(0x62*2)
#define      CyTIMEOUT		(0x80)
#define      CySPECHAR		(0x70)
#define      CyBREAK		(0x08)
#define      CyPARITY		(0x04)
#define      CyFRAME		(0x02)
#define      CyOVERRUN		(0x01)
#define CyMISR		(0x4C*2)
/* see CyMCOR_ and CyMSVR_ for bits*/
#define CyEOSRR		(0x60*2)

/* Channel Registers */

#define CyLIVR		(0x18*2)
#define      CyMscsr		(0x01)
#define      CyTdsr		(0x02)
#define      CyRgdsr		(0x03)
#define      CyRedsr		(0x07)
#define CyCCR		(0x05*2)
/* Format 1 */
#define      CyCHAN_RESET	(0x80)
#define      CyCHIP_RESET	(0x81)
#define      CyFlushTransFIFO	(0x82)
/* Format 2 */
#define      CyCOR_CHANGE	(0x40)
#define      CyCOR1ch		(0x02)
#define      CyCOR2ch		(0x04)
#define      CyCOR3ch		(0x08)
/* Format 3 */
#define      CySEND_SPEC_1	(0x21)
#define      CySEND_SPEC_2	(0x22)
#define      CySEND_SPEC_3	(0x23)
#define      CySEND_SPEC_4	(0x24)
/* Format 4 */
#define      CyCHAN_CTL		(0x10)
#define      CyDIS_RCVR		(0x01)
#define      CyENB_RCVR		(0x02)
#define      CyDIS_XMTR		(0x04)
#define      CyENB_XMTR		(0x08)
#define CySRER		(0x06*2)
#define      CyMdmCh		(0x80)
#define      CyRxData		(0x10)
#define      CyTxRdy		(0x04)
#define      CyTxMpty		(0x02)
#define      CyNNDT		(0x01)
#define CyCOR1		(0x08*2)
#define      CyPARITY_NONE	(0x00)
#define      CyPARITY_0		(0x20)
#define      CyPARITY_1		(0xA0)
#define      CyPARITY_E		(0x40)
#define      CyPARITY_O		(0xC0)
#define      Cy_1_STOP		(0x00)
#define      Cy_1_5_STOP	(0x04)
#define      Cy_2_STOP		(0x08)
#define      Cy_5_BITS		(0x00)
#define      Cy_6_BITS		(0x01)
#define      Cy_7_BITS		(0x02)
#define      Cy_8_BITS		(0x03)
#define CyCOR2		(0x09*2)
#define      CyIXM		(0x80)
#define      CyTxIBE		(0x40)
#define      CyETC		(0x20)
#define      CyAUTO_TXFL	(0x60)
#define      CyLLM		(0x10)
#define      CyRLM		(0x08)
#define      CyRtsAO		(0x04)
#define      CyCtsAE		(0x02)
#define      CyDsrAE		(0x01)
#define CyCOR3		(0x0A*2)
#define      CySPL_CH_DRANGE	(0x80)  /* special character detect range */
#define      CySPL_CH_DET1	(0x40)  /* enable special character detection
                                                               on SCHR4-SCHR3 */
#define      CyFL_CTRL_TRNSP	(0x20)  /* Flow Control Transparency */
#define      CySPL_CH_DET2	(0x10)  /* Enable special character detection
                                                               on SCHR2-SCHR1 */
#define      CyREC_FIFO		(0x0F)  /* Receive FIFO threshold */
#define CyCOR4		(0x1E*2)
#define CyCOR5		(0x1F*2)
#define CyCCSR		(0x0B*2)
#define      CyRxEN		(0x80)
#define      CyRxFloff		(0x40)
#define      CyRxFlon		(0x20)
#define      CyTxEN		(0x08)
#define      CyTxFloff		(0x04)
#define      CyTxFlon		(0x02)
#define CyRDCR		(0x0E*2)
#define CySCHR1		(0x1A*2)
#define CySCHR2 	(0x1B*2)
#define CySCHR3		(0x1C*2)
#define CySCHR4		(0x1D*2)
#define CySCRL		(0x22*2)
#define CySCRH		(0x23*2)
#define CyLNC		(0x24*2)
#define CyMCOR1 	(0x15*2)
#define CyMCOR2		(0x16*2)
#define CyRTPR		(0x21*2)
#define CyMSVR1		(0x6C*2)
#define CyMSVR2		(0x6D*2)
#define      CyDSR		(0x80)
#define      CyCTS		(0x40)
#define      CyRI		(0x20)
#define      CyDCD		(0x10)
#define      CyDTR              (0x02)
#define      CyRTS              (0x01)
#define CyPVSR		(0x6F*2)
#define CyRBPR		(0x78*2)
#define CyRCOR		(0x7C*2)
#define CyTBPR		(0x72*2)
#define CyTCOR		(0x76*2)

/* max number of chars in the FIFO */

#define CyMAX_CHAR_FIFO	12

/***************************************************************************/

/*
    Copyright 1994 Digital Equipment Corporation.

    This software may be used and distributed according to  the terms of the
    GNU Public License, incorporated herein by reference.

    The author may    be  reached as davies@wanton.lkg.dec.com  or   Digital
    Equipment Corporation, 550 King Street, Littleton MA 01460.

    =========================================================================
*/

/*
** DC21040 CSR<1..15> Register Address Map
*/
#define DE4X5_BMR    iobase+(0x000 << lp->bus)  /* Bus Mode Register */
#define DE4X5_TPD    iobase+(0x008 << lp->bus)  /* Transmit Poll Demand Reg */
#define DE4X5_RPD    iobase+(0x010 << lp->bus)  /* Receive Poll Demand Reg */
#define DE4X5_RRBA   iobase+(0x018 << lp->bus)  /* RX Ring Base Address Reg */
#define DE4X5_TRBA   iobase+(0x020 << lp->bus)  /* TX Ring Base Address Reg */
#define DE4X5_STS    iobase+(0x028 << lp->bus)  /* Status Register */
#define DE4X5_OMR    iobase+(0x030 << lp->bus)  /* Operation Mode Register */
#define DE4X5_IMR    iobase+(0x038 << lp->bus)  /* Interrupt Mask Register */
#define DE4X5_MFC    iobase+(0x040 << lp->bus)  /* Missed Frame Counter */
#define DE4X5_APROM  iobase+(0x048 << lp->bus)  /* Ethernet Address PROM */
#define DE4X5_DDR    iobase+(0x050 << lp->bus)  /* Data Diagnostic Register */
#define DE4X5_FDR    iobase+(0x058 << lp->bus)  /* Full Duplex Register */
#define DE4X5_SISR   iobase+(0x060 << lp->bus)  /* SIA Status Register */
#define DE4X5_SICR   iobase+(0x068 << lp->bus)  /* SIA Connectivity Register */
#define DE4X5_STRR   iobase+(0x070 << lp->bus)  /* SIA TX/RX Register */
#define DE4X5_SIGR   iobase+(0x078 << lp->bus)  /* SIA General Register */

/*
** EISA Register Address Map
*/
#define EISA_ID      iobase+0x0c80   /* EISA ID Registers */ 
#define EISA_ID0     iobase+0x0c80   /* EISA ID Register 0 */ 
#define EISA_ID1     iobase+0x0c81   /* EISA ID Register 1 */ 
#define EISA_ID2     iobase+0x0c82   /* EISA ID Register 2 */ 
#define EISA_ID3     iobase+0x0c83   /* EISA ID Register 3 */ 
#define EISA_CR      iobase+0x0c84   /* EISA Control Register */
#define EISA_REG0    iobase+0x0c88   /* EISA Configuration Register 0 */
#define EISA_REG1    iobase+0x0c89   /* EISA Configuration Register 1 */
#define EISA_REG2    iobase+0x0c8a   /* EISA Configuration Register 2 */
#define EISA_REG3    iobase+0x0c8f   /* EISA Configuration Register 3 */
#define EISA_APROM   iobase+0x0c90   /* Ethernet Address PROM */

/*
** PCI/EISA Configuration Registers Address Map
*/
#define PCI_CFID     iobase+0x0008   /* PCI Configuration ID Register */
#define PCI_CFCS     iobase+0x000c   /* PCI Command/Status Register */
#define PCI_CFRV     iobase+0x0018   /* PCI Revision Register */
#define PCI_CFLT     iobase+0x001c   /* PCI Latency Timer Register */
#define PCI_CBIO     iobase+0x0028   /* PCI Base I/O Register */
#define PCI_CBMA     iobase+0x002c   /* PCI Base Memory Address Register */
#define PCI_CFIT     iobase+0x0038   /* PCI Configuration Interrupt Register */

/*
** EISA Configuration Register 0 bit definitions
*/
#define ER0_BSW       0x80           /* EISA Bus Slave Width, 1: 32 bits */
#define ER0_BMW       0x40           /* EISA Bus Master Width, 1: 32 bits */
#define ER0_EPT       0x20           /* EISA PREEMPT Time, 0: 23 BCLKs */
#define ER0_ISTS      0x10           /* Interrupt Status (X) */
#define ER0_LI        0x08           /* Latch Interrupts */
#define ER0_INTL      0x06           /* INTerrupt Level */
#define ER0_INTT      0x01           /* INTerrupt Type, 0: Level, 1: Edge */

/*
** EISA Configuration Register 1 bit definitions
*/
#define ER1_IAM       0xe0           /* ISA Address Mode */
#define ER1_IAE       0x10           /* ISA Addressing Enable */
#define ER1_UPIN      0x0f           /* User Pins */

/*
** EISA Configuration Register 2 bit definitions
*/
#define ER2_BRS       0xc0           /* Boot ROM Size */
#define ER2_BRA       0x3c           /* Boot ROM Address <16:13> */

/*
** EISA Configuration Register 3 bit definitions
*/
#define ER3_BWE       0x40           /* Burst Write Enable */
#define ER3_BRE       0x04           /* Burst Read Enable */
#define ER3_LSR       0x02           /* Local Software Reset */

/*
** PCI Configuration ID Register (PCI_CFID)
*/
#define CFID_DID    0xff00           /* Device ID */
#define CFID_VID    0x00ff           /* Vendor ID */
#define DC21040_DID 0x0002           /* Unique Device ID # */
#define DC21040_VID 0x1011           /* DC21040 Manufacturer */

/*
** PCI Configuration Command/Status Register (PCI_CFCS)
*/
#define CFCS_DPE    0x80000000       /* Detected Parity Error (S) */
#define CFCS_SSE    0x40000000       /* Signal System Error   (S) */
#define CFCS_RMA    0x20000000       /* Receive Master Abort  (S) */
#define CFCS_RTA    0x10000000       /* Receive Target Abort  (S) */
#define CFCS_DST    0x06000000       /* DEVSEL Timing         (S) */
#define CFCS_DPR    0x01000000       /* Data Parity Report    (S) */
#define CFCS_FBB    0x00800000       /* Fast Back-To-Back     (S) */
#define CFCS_SLE    0x00000100       /* System Error Enable   (C) */
#define CFCS_PER    0x00000040       /* Parity Error Response (C) */
#define CFCS_MO     0x00000004       /* Master Operation      (C) */
#define CFCS_MSA    0x00000002       /* Memory Space Access   (C) */
#define CFCS_IOSA   0x00000001       /* I/O Space Access      (C) */

/*
** PCI Configuration Revision Register (PCI_CFRV)
*/
#define CFRV_BC     0xff000000       /* Base Class */
#define CFRV_SC     0x00ff0000       /* Subclass */
#define CFRV_SN     0x000000f0       /* Step Number */
#define CFRV_RN     0x0000000f       /* Revision Number */
#define BASE_CLASS  0x02000000       /* Indicates Network Controller */
#define SUB_CLASS   0x00000000       /* Indicates Ethernet Controller */
#define STEP_NUMBER 0x00000020       /* Increments for future chips */
#define REV_NUMBER  0x00000003       /* 0x00, 0x01, 0x02, 0x03: Rev in Step */
#define CFRV_MASK   0xffff0000       /* Register mask */

/*
** PCI Configuration Latency Timer Register (PCI_CFLT)
*/
#define CFLT_BC     0x0000ff00       /* Latency Timer bits */

/*
** PCI Configuration Base I/O Address Register (PCI_CBIO)
*/
#define CBIO_MASK   0x0000ff80       /* Base I/O Address Mask */
#define CBIO_IOSI   0x00000001       /* I/O Space Indicator (RO, value is 1) */

/*
** DC21040 Bus Mode Register (DE4X5_BMR)
*/
#define BMR_TAP    0x00060000       /* Transmit Automatic Polling */
#define BMR_DAS    0x00010000       /* Diagnostic Address Space */
#define BMR_CAL    0x0000c000       /* Cache Alignment */
#define BMR_PBL    0x00003f00       /* Programmable Burst Length */
#define BMR_BLE    0x00000080       /* Big/Little Endian */
#define BMR_DSL    0x0000007c       /* Descriptor Skip Length */
#define BMR_BAR    0x00000002       /* Bus ARbitration */
#define BMR_SWR    0x00000001       /* Software Reset */

#define TAP_NOPOLL 0x00000000       /* No automatic polling */
#define TAP_200US  0x00020000       /* TX automatic polling every 200us */
#define TAP_800US  0x00040000       /* TX automatic polling every 800us */
#define TAP_1_6MS  0x00060000       /* TX automatic polling every 1.6ms */

#define CAL_NOUSE  0x00000000       /* Not used */
#define CAL_8LONG  0x00004000       /* 8-longword alignment */
#define CAL_16LONG 0x00008000       /* 16-longword alignment */
#define CAL_32LONG 0x0000c000       /* 32-longword alignment */

#define PBL_0      0x00000000       /*  DMA burst length = amount in RX FIFO */
#define PBL_1      0x00000100       /*  1 longword  DMA burst length */
#define PBL_2      0x00000200       /*  2 longwords DMA burst length */
#define PBL_4      0x00000400       /*  4 longwords DMA burst length */
#define PBL_8      0x00000800       /*  8 longwords DMA burst length */
#define PBL_16     0x00001000       /* 16 longwords DMA burst length */
#define PBL_32     0x00002000       /* 32 longwords DMA burst length */

#define DSL_1      0x00000004       /*  1 longword  / descriptor */
#define DSL_2      0x00000008       /*  2 longwords / descriptor */
#define DSL_4      0x00000010       /*  4 longwords / descriptor */
#define DSL_8      0x00000020       /*  8 longwords / descriptor */
#define DSL_16     0x00000040       /* 16 longwords / descriptor */
#define DSL_32     0x00000080       /* 32 longwords / descriptor */

/*
** DC21040 Transmit Poll Demand Register (DE4X5_TPD)
*/
#define TPD        0x00000001       /* Transmit Poll Demand */

/*
** DC21040 Receive Poll Demand Register (DE4X5_RPD)
*/
#define RPD        0x00000001       /* Receive Poll Demand */

/*
** DC21040 Receive Ring Base Address Register (DE4X5_RRBA)
*/
#define RRBA       0xfffffffc       /* RX Descriptor List Start Address */

/*
** DC21040 Transmit Ring Base Address Register (DE4X5_TRBA)
*/
#define TRBA       0xfffffffc       /* TX Descriptor List Start Address */

/*
** DC21040 Status Register (DE4X5_STS)
*/
#define STS_EB     0x03800000       /* Error Bits */
#define STS_TS     0x00700000       /* Transmit Process State */
#define STS_RS     0x000e0000       /* Receive Process State */
#define STS_NIS    0x00010000       /* Normal Interrupt Summary */
#define STS_AIS    0x00008000       /* Abnormal Interrupt Summary */
#define STS_SE     0x00002000       /* System Error */
#define STS_LNF    0x00001000       /* Link Fail */
#define STS_FD     0x00000800       /* Full-Duplex Short Frame Received */
#define STS_AT     0x00000400       /* AUI/TP Pin */
#define STS_RWT    0x00000200       /* Receive Watchdog Time-Out */
#define STS_RPS    0x00000100       /* Receive Process Stopped */
#define STS_RU     0x00000080       /* Receive Buffer Unavailable */
#define STS_RI     0x00000040       /* Receive Interrupt */
#define STS_UNF    0x00000020       /* Transmit Underflow */
#define STS_TJT    0x00000008       /* Transmit Jabber Time-Out */
#define STS_TU     0x00000004       /* Transmit Buffer Unavailable */
#define STS_TPS    0x00000002       /* Transmit Process Stopped */
#define STS_TI     0x00000001       /* Transmit Interrupt */

#define EB_PAR     0x00000000       /* Parity Error */
#define EB_MA      0x00800000       /* Master Abort */
#define EB_TA      0x01000000       /* Target Abort */
#define EB_RES0    0x01800000       /* Reserved */
#define EB_RES1    0x02000000       /* Reserved */

#define TS_STOP    0x00000000       /* Stopped */
#define TS_FTD     0x00100000       /* Fetch Transmit Descriptor */
#define TS_WEOT    0x00200000       /* Wait for End Of Transmission */
#define TS_QDAT    0x00300000       /* Queue skb data into TX FIFO */
#define TS_RES     0x00400000       /* Reserved */
#define TS_SPKT    0x00500000       /* Setup Packet */
#define TS_SUSP    0x00600000       /* Suspended */
#define TS_CLTD    0x00700000       /* Close Transmit Descriptor */

#define RS_STOP    0x00000000       /* Stopped */
#define RS_FRD     0x00020000       /* Fetch Receive Descriptor */
#define RS_CEOR    0x00040000       /* Check for End of Receive Packet */
#define RS_WFRP    0x00060000       /* Wait for Receive Packet */
#define RS_SUSP    0x00080000       /* Suspended */
#define RS_CLRD    0x000a0000       /* Close Receive Descriptor */
#define RS_FLUSH   0x000c0000       /* Flush RX FIFO */
#define RS_QRFS    0x000e0000       /* Queue RX FIFO into RX Skb */

#define INT_CANCEL 0x0001ffff       /* For zeroing all interrupt sources */

/*
** DC21040 Operation Mode Register (DE4X5_OMR)
*/
#define OMR_CA     0x00020000       /* Capture Effect Enable */
#define OMR_BP     0x00010000       /* Back Pressure */
#define OMR_TR     0x0000c000       /* Threshold Control Bits */
#define OMR_ST     0x00002000       /* Start/Stop Transmission Command */
#define OMR_FC     0x00001000       /* Force Collision Mode */
#define OMR_OM     0x00000c00       /* Operating Mode */
#define OMR_FD     0x00000200       /* Full Duplex Mode */
#define OMR_FKD    0x00000100       /* Flaky Oscillator Disable */
#define OMR_PM     0x00000080       /* Pass All Multicast */
#define OMR_PR     0x00000040       /* Promiscuous Mode */
#define OMR_SB     0x00000020       /* Start/Stop Backoff Counter */
#define OMR_IF     0x00000010       /* Inverse Filtering */
#define OMR_PB     0x00000008       /* Pass Bad Frames */
#define OMR_HO     0x00000004       /* Hash Only Filtering Mode */
#define OMR_SR     0x00000002       /* Start/Stop Receive */
#define OMR_HP     0x00000001       /* Hash/Perfect Receive Filtering Mode */

#define TR_72      0x00000000       /* Threshold set to 72 bytes */
#define TR_96      0x00004000       /* Threshold set to 96 bytes */
#define TR_128     0x00008000       /* Threshold set to 128 bytes */
#define TR_160     0x0000c000       /* Threshold set to 160 bytes */

/*
** DC21040 Interrupt Mask Register (DE4X5_IMR)
*/
#define IMR_NIM    0x00010000       /* Normal Interrupt Summary Mask */
#define IMR_AIM    0x00008000       /* Abnormal Interrupt Summary Mask */
#define IMR_SEM    0x00002000       /* System Error Mask */
#define IMR_LFM    0x00001000       /* Link Fail Mask */
#define IMR_FDM    0x00000800       /* Full-Duplex Mask */
#define IMR_ATM    0x00000400       /* AUI/TP Switch Mask */
#define IMR_RWM    0x00000200       /* Receive Watchdog Time-Out Mask */
#define IMR_RSM    0x00000100       /* Receive Stopped Mask */
#define IMR_RUM    0x00000080       /* Receive Buffer Unavailable Mask */
#define IMR_RIM    0x00000040       /* Receive Interrupt Mask */
#define IMR_UNM    0x00000020       /* Underflow Interrupt Mask */
#define IMR_TJM    0x00000008       /* Transmit Time-Out Jabber Mask */
#define IMR_TUM    0x00000004       /* Transmit Buffer Unavailable Mask */
#define IMR_TSM    0x00000002       /* Transmission Stopped Mask */
#define IMR_TIM    0x00000001       /* Transmit Interrupt Mask */

/*
** DC21040 Missed Frame Counter (DE4X5_MFC)
*/
#define MFC_OVFL   0x00010000       /* Counter Overflow Bit */
#define MFC_CNTR   0x0000ffff       /* Counter Bits */

/*
** DC21040 Ethernet Address PROM (DE4X5_APROM)
*/
#define APROM_DN   0x80000000       /* Data Not Valid */
#define APROM_DT   0x000000ff       /* Address Byte */

/*
** DC21040 Full Duplex Register (DE4X5_FDR)
*/
#define FDR_FDACV  0x0000ffff       /* Full Duplex Auto Configuration Value */

/*
** DC21040 SIA Status Register (DE4X5_SISR)
*/
#define SISR_DAO   0x00000080       /* PLL All One */
#define SISR_DAZ   0x00000040       /* PLL All Zero */
#define SISR_DSP   0x00000020       /* PLL Self-Test Pass */
#define SISR_DSD   0x00000010       /* PLL Self-Test Done */
#define SISR_APS   0x00000008       /* Auto Polarity State */
#define SISR_LKF   0x00000004       /* Link Fail Status */
#define SISR_NCR   0x00000002       /* Network Connection Error */
#define SISR_PAUI  0x00000001       /* AUI_TP Indication */

#define SIA_RESET  0x00000000       /* SIA Reset */

/*
** DC21040 SIA Connectivity Register (DE4X5_SICR)
*/
#define SICR_OE57  0x00008000       /* Output Enable 5 6 7 */
#define SICR_OE24  0x00004000       /* Output Enable 2 4 */
#define SICR_OE13  0x00002000       /* Output Enable 1 3 */
#define SICR_IE    0x00001000       /* Input Enable */
#define SICR_EXT   0x00000000       /* SIA MUX Select External SIA Mode */
#define SICR_D_SIA 0x00000400       /* SIA MUX Select Diagnostics - SIA Sigs */
#define SICR_DPLL  0x00000800       /* SIA MUX Select Diagnostics - DPLL Sigs*/
#define SICR_APLL  0x00000a00       /* SIA MUX Select Diagnostics - DPLL Sigs*/
#define SICR_D_RxM 0x00000c00       /* SIA MUX Select Diagnostics - RxM Sigs */
#define SICR_M_RxM 0x00000d00       /* SIA MUX Select Diagnostics - RxM Sigs */
#define SICR_LNKT  0x00000e00       /* SIA MUX Select Diagnostics - Link Test*/
#define SICR_SEL   0x00000f00       /* SIA MUX Select AUI or TP with LEDs */
#define SICR_ASE   0x00000080       /* APLL Start Enable*/
#define SICR_SIM   0x00000040       /* Serial Interface Input Multiplexer */
#define SICR_ENI   0x00000020       /* Encoder Input Multiplexer */
#define SICR_EDP   0x00000010       /* SIA PLL External Input Enable */
#define SICR_AUI   0x00000008       /* 10Base-T or AUI */
#define SICR_CAC   0x00000004       /* CSR Auto Configuration */
#define SICR_PS    0x00000002       /* Pin AUI/TP Selection */
#define SICR_SRL   0x00000001       /* SIA Reset */
#define SICR_RESET 0xffff0000       /* Reset value for SICR */

/*
** DC21040 SIA Transmit and Receive Register (DE4X5_STRR)
*/
#define STRR_SPP   0x00004000       /* Set Polarity Plus */
#define STRR_APE   0x00002000       /* Auto Polarity Enable */
#define STRR_LTE   0x00001000       /* Link Test Enable */
#define STRR_SQE   0x00000800       /* Signal Quality Enable */
#define STRR_CLD   0x00000400       /* Collision Detect Enable */
#define STRR_CSQ   0x00000200       /* Collision Squelch Enable */
#define STRR_RSQ   0x00000100       /* Receive Squelch Enable */
#define STRR_CPEN  0x00000030       /* Compensation Enable */
#define STRR_LSE   0x00000008       /* Link Pulse Send Enable */
#define STRR_DREN  0x00000004       /* Driver Enable */
#define STRR_LBK   0x00000002       /* Loopback Enable */
#define STRR_ECEN  0x00000001       /* Encoder Enable */
#define STRR_RESET 0xffffffff       /* Reset value for STRR */

/*
** DC21040 SIA General Register (DE4X5_SIGR)
*/
#define SIGR_FRL   0x00002000       /* Force Receiver Low */
#define SIGR_DPST  0x00001000       /* PLL Self Test Start */
#define SIGR_FLF   0x00000400       /* Force Link Fail */
#define SIGR_FUSQ  0x00000200       /* Force Unsquelch */
#define SIGR_TSCK  0x00000100       /* Test Clock */
#define SIGR_RWR   0x00000020       /* Receive Watchdog Release */
#define SIGR_RWD   0x00000010       /* Receive Watchdog Disable */
#define SIGR_JCK   0x00000004       /* Jabber Clock */
#define SIGR_HUJ   0x00000002       /* Host Unjab */
#define SIGR_JBD   0x00000001       /* Jabber Disable */
#define SIGR_RESET 0xffff0000       /* Reset value for SIGR */

/*
** Receive Descriptor Bit Summary
*/
#define R_OWN      0x80000000       /* Own Bit */
#define RD_FL      0x7fff0000       /* Frame Length */
#define RD_ES      0x00008000       /* Error Summary */
#define RD_LE      0x00004000       /* Length Error */
#define RD_DT      0x00003000       /* Data Type */
#define RD_RF      0x00000800       /* Runt Frame */
#define RD_MF      0x00000400       /* Multicast Frame */
#define RD_FS      0x00000200       /* First Descriptor */
#define RD_LS      0x00000100       /* Last Descriptor */
#define RD_TL      0x00000080       /* Frame Too Long */
#define RD_CS      0x00000040       /* Collision Seen */
#define RD_FT      0x00000020       /* Frame Type */
#define RD_RJ      0x00000010       /* Receive Watchdog */
#define RD_DB      0x00000004       /* Dribbling Bit */
#define RD_CE      0x00000002       /* CRC Error */
#define RD_OF      0x00000001       /* Overflow */

#define RD_RER     0x02000000       /* Receive End Of Ring */
#define RD_RCH     0x01000000       /* Second Address Chained */
#define RD_RBS2    0x003ff800       /* Buffer 2 Size */
#define RD_RBS1    0x000007ff       /* Buffer 1 Size */

/*
** Transmit Descriptor Bit Summary
*/
#define T_OWN      0x80000000       /* Own Bit */
#define TD_ES      0x00008000       /* Error Summary */
#define TD_TO      0x00004000       /* Transmit Jabber Time-Out */
#define TD_LO      0x00000800       /* Loss Of Carrier */
#define TD_NC      0x00000400       /* No Carrier */
#define TD_LC      0x00000200       /* Late Collision */
#define TD_EC      0x00000100       /* Excessive Collisions */
#define TD_HF      0x00000080       /* Heartbeat Fail */
#define TD_CC      0x00000078       /* Collision Counter */
#define TD_LF      0x00000004       /* Link Fail */
#define TD_UF      0x00000002       /* Underflow Error */
#define TD_DE      0x00000001       /* Deferred */

#define TD_IC      0x80000000       /* Interrupt On Completion */
#define TD_LS      0x40000000       /* Last Segment */
#define TD_FS      0x20000000       /* First Segment */
#define TD_FT1     0x10000000       /* Filtering Type */
#define TD_SET     0x08000000       /* Setup Packet */
#define TD_AC      0x04000000       /* Add CRC Disable */
#define TD_TER     0x02000000       /* Transmit End Of Ring */
#define TD_TCH     0x01000000       /* Second Address Chained */
#define TD_DPD     0x00800000       /* Disabled Padding */
#define TD_FT0     0x00400000       /* Filtering Type */
#define TD_RBS2    0x003ff800       /* Buffer 2 Size */
#define TD_RBS1    0x000007ff       /* Buffer 1 Size */

#define PERFECT_F  0x00000000
#define HASH_F     TD_FT0
#define INVERSE_F  TD_FT1
#define HASH_O_F   TD_FT1| TD_F0

/*
** Miscellaneous
*/
#define PCI  0
#define EISA 1

#define NC         0               /* No Connection */
#define TP         1               /* 10Base-T */
#define BNC        2               /* Thinwire */
#define AUI        3               /* Thickwire */
  
#define HASH_TABLE_LEN   512       /* Bits */

#define SETUP_FRAME_LEN  192       /* Bytes */
#define PERF_PA_OFFSET   180       /* Bytes */
#define IMPERF_PA_OFFSET 156       /* Bytes */

#define POLL_DEMAND       1

#define MASK_INTERRUPTS   1
#define UNMASK_INTERRUPTS 0

/*
** Include the IOCTL stuff
*/
#include <linux/sockios.h>

#define	DE4X5IOCTL	SIOCDEVPRIVATE

struct de4x5_ioctl {
	unsigned short cmd;                /* Command to run */
	unsigned short len;                /* Length of the data buffer */
	unsigned char  *data;              /* Pointer to the data buffer */
};

/* 
** Recognised commands for the driver 
*/
#define DE4X5_GET_HWADDR	0x01 /* Get the hardware address */
#define DE4X5_SET_HWADDR	0x02 /* Get the hardware address */
#define DE4X5_SET_PROM  	0x03 /* Set Promiscuous Mode */
#define DE4X5_CLR_PROM  	0x04 /* Clear Promiscuous Mode */
#define DE4X5_SAY_BOO	        0x05 /* Say "Boo!" to the kernel log file */
#define DE4X5_GET_MCA   	0x06 /* Get a multicast address */
#define DE4X5_SET_MCA   	0x07 /* Set a multicast address */
#define DE4X5_CLR_MCA    	0x08 /* Clear a multicast address */
#define DE4X5_MCA_EN    	0x09 /* Enable a multicast address group */
#define DE4X5_GET_STATS  	0x0a /* Get the driver statistics */
#define DE4X5_CLR_STATS 	0x0b /* Zero out the driver statistics */
#define DE4X5_GET_OMR           0x0c /* Get the OMR Register contents */
#define DE4X5_SET_OMR           0x0d /* Set the OMR Register contents */
#define DE4X5_GET_REG           0x0e /* Get the DE4X5 Registers */




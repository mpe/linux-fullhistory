#if 0
static char *rcsid = "$Id: smc_ircc.h,v 1.5 1998/07/27 01:25:29 ratbert Exp $";
#endif

#ifndef SMC_IRCC_H
#define SMC_IRCC_H

#define FIR_XMIT	1
#define FIR_RECEIVE	2
#define SIR_XMIT	3
#define SIR_RECEIVE	4

#define MASTER			0x07
#define MASTER_POWERDOWN	1<<7
#define MASTER_RESET		1<<6
#define MASTER_INT_EN		1<<5
#define MASTER_ERROR_RESET	1<<4

/* Register block 0 */

#define IIR	0x01
#define IER	0x02
#define LSR	0x03
#define LCR_A	0x04
#define LCR_B	0x05
#define BSR	0x06

#define IIR_ACTIVE_FRAME	1<<7
#define IIR_EOM 		1<<6
#define IIR_RAW_MODE		1<<5
#define IIR_FIFO		1<<4

#define IER_ACTIVE_FRAME	1<<7
#define IER_EOM 		1<<6
#define IER_RAW_MODE		1<<5
#define IER_FIFO		1<<4

#define LSR_UNDER_RUN		1<<7
#define LSR_OVER_RUN		1<<6
#define LSR_FRAME_ERROR 	1<<5
#define LSR_SIZE_ERROR		1<<4
#define LSR_CRC_ERROR		1<<3
#define LSR_FRAME_ABORT 	1<<2

#define LCR_A_FIFO_RESET        1<<7
#define LCR_A_FAST              1<<6
#define LCR_A_GP_DATA           1<<5
#define LCR_A_RAW_TX            1<<4
#define LCR_A_RAW_RX            1<<3
#define LCR_A_ABORT             1<<2
#define LCR_A_DATA_DONE         1<<1

#define LCR_B_SCE_MODE_DISABLED 	0x00<<6
#define LCR_B_SCE_MODE_TRANSMIT 	0x01<<6
#define LCR_B_SCE_MODE_RECEIVE		0x02<<6
#define LCR_B_SCE_MODE_UNDEFINED	0x03<<6
#define LCR_B_SIP_ENABLE		1<<5
#define LCR_B_BRICK_WALL		1<<4

#define BSR_NOT_EMPTY	1<<7
#define BSR_FIFO_FULL	1<<6
#define BSR_TIMEOUT	1<<5

/* Register block 1 */

#define SCE_CFG_A	0x00
#define SCE_CFG_B	0x01
#define FIFO_THRESHOLD	0x02

#define CFG_A_AUX_IR		0x01<<7
#define CFG_A_HALF_DUPLEX	0x01<<2
#define CFG_A_TX_POLARITY	0x01<<1
#define CFG_A_RX_POLARITY	0x01

#define CFG_A_COM		0x00<<3
#define CFG_A_IRDA_SIR_A	0x01<<3
#define CFG_A_ASK_SIR		0x02<<3
#define CFG_A_IRDA_SIR_B	0x03<<3
#define CFG_A_IRDA_HDLC 	0x04<<3
#define CFG_A_IRDA_4PPM 	0x05<<3
#define CFG_A_CONSUMER		0x06<<3
#define CFG_A_RAW_IR		0x07<<3
#define CFG_A_OTHER		0x08<<3

#define IR_HDLC			0x04
#define IR_4PPM			0x01
#define IR_CONSUMER		0x02

#define CFG_B_LOOPBACK		0x01<<5
#define CFG_B_LPBCK_TX_CRC	0x01<<4
#define CFG_B_NOWAIT		0x01<<3
#define CFB_B_STRING_MOVE	0x01<<2
#define CFG_B_DMA_BURST 	0x01<<1
#define CFG_B_DMA_ENABLE	0x01

#define CFG_B_MUX_COM		0x00<<6
#define CFG_B_MUX_IR		0x01<<6
#define CFG_B_MUX_AUX		0x02<<6
#define CFG_B_INACTIVE		0x03<<6

/* Register block 2 - Consumer IR - not used */

/* Register block 3 - Identification Registers! */

#define SMSC_ID_HIGH	0x00   /* 0x10 */
#define SMSC_ID_LOW	0x01   /* 0xB8 */
#define CHIP_ID 	0x02   /* 0xF1 */
#define VERSION_NUMBER	0x03   /* 0x01 */
#define HOST_INTERFACE	0x04   /* low 4 = DMA, high 4 = IRQ */

/* Register block 4 - IrDA */
#define IR_CONTROL        0x00
#define BOF_COUNT_LO      0x01
#define BRICK_WALL_CNT_LO 0x02
#define BRICK_TX_CNT_HI   0x03
#define TX_DATA_SIZE_LO   0x04
#define RX_DATA_SIZE_HI   0x05
#define RX_DATA_SIZE_LO   0x06

#define SELECT_1152     0x01<<7
#define CRC_SELECT      0x01<<6

#endif

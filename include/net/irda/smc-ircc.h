/*********************************************************************
 *                
 * Filename:      smc-ircc.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Thomas Davis (tadavis@jps.net)
 *
 *     Copyright (c) 1998, 1999 Thomas Davis (tadavis@jps.net>
 *     All Rights Reserved
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     I, Thomas Davis, admit no liability nor provide warranty for any
 *     of this software. This material is provided "AS-IS" and at no charge.
 *     
 * Definitions for the SMC IrCC controller.
 *
 ********************************************************************/

#include <net/irda/irport.h>

#ifndef SMC_IRCC_H
#define SMC_IRCC_H

#define UART_MASTER			0x07
#define UART_MASTER_POWERDOWN	1<<7
#define UART_MASTER_RESET		1<<6
#define UART_MASTER_INT_EN		1<<5
#define UART_MASTER_ERROR_RESET	1<<4

/* Register block 0 */

#define UART_IIR	0x01
#define UART_IER	0x02
#define UART_LSR	0x03
#define UART_LCR_A	0x04
#define UART_LCR_B	0x05
#define UART_BSR	0x06

#define UART_IIR_ACTIVE_FRAME	1<<7
#define UART_IIR_EOM 		1<<6
#define UART_IIR_RAW_MODE		1<<5
#define UART_IIR_FIFO		1<<4

#define UART_IER_ACTIVE_FRAME	1<<7
#define UART_IER_EOM 		1<<6
#define UART_IER_RAW_MODE		1<<5
#define UART_IER_FIFO		1<<4

#define UART_LSR_UNDERRUN		1<<7
#define UART_LSR_OVERRUN		1<<6
#define UART_LSR_FRAME_ERROR 	1<<5
#define UART_LSR_SIZE_ERROR		1<<4
#define UART_LSR_CRC_ERROR		1<<3
#define UART_LSR_FRAME_ABORT 	1<<2

#define UART_LCR_A_FIFO_RESET        1<<7
#define UART_LCR_A_FAST              1<<6
#define UART_LCR_A_GP_DATA           1<<5
#define UART_LCR_A_RAW_TX            1<<4
#define UART_LCR_A_RAW_RX            1<<3
#define UART_LCR_A_ABORT             1<<2
#define UART_LCR_A_DATA_DONE         1<<1

#define UART_LCR_B_SCE_DISABLED 	0x00<<6
#define UART_LCR_B_SCE_TRANSMIT 	0x01<<6
#define UART_LCR_B_SCE_RECEIVE		0x02<<6
#define UART_LCR_B_SCE_UNDEFINED	0x03<<6
#define UART_LCR_B_SIP_ENABLE		1<<5
#define UART_LCR_B_BRICK_WALL		1<<4

#define UART_BSR_NOT_EMPTY	1<<7
#define UART_BSR_FIFO_FULL	1<<6
#define UART_BSR_TIMEOUT	1<<5

/* Register block 1 */

#define UART_SCE_CFGA	0x00
#define UART_SCE_CFGB	0x01
#define UART_FIFO_THRESHOLD	0x02

#define UART_CFGA_AUX_IR		0x01<<7
#define UART_CFGA_HALF_DUPLEX	0x01<<2
#define UART_CFGA_TX_POLARITY	0x01<<1
#define UART_CFGA_RX_POLARITY	0x01

#define UART_CFGA_COM		0x00<<3
#define UART_CFGA_IRDA_SIR_A	0x01<<3
#define UART_CFGA_ASK_SIR		0x02<<3
#define UART_CFGA_IRDA_SIR_B	0x03<<3
#define UART_CFGA_IRDA_HDLC 	0x04<<3
#define UART_CFGA_IRDA_4PPM 	0x05<<3
#define UART_CFGA_CONSUMER		0x06<<3
#define UART_CFGA_RAW_IR		0x07<<3
#define UART_CFGA_OTHER		0x08<<3

#define UART_IR_HDLC			0x04
#define UART_IR_4PPM			0x01
#define UART_IR_CONSUMER		0x02

#define UART_CFGB_LOOPBACK		0x01<<5
#define UART_CFGB_LPBCK_TX_CRC	0x01<<4
#define UART_CFGB_NOWAIT		0x01<<3
#define UART_CFGB_STRING_MOVE	0x01<<2
#define UART_CFGB_DMA_BURST 	0x01<<1
#define UART_CFGB_DMA_ENABLE	0x01

#define UART_CFGB_COM		0x00<<6
#define UART_CFGB_IR		0x01<<6
#define UART_CFGB_AUX		0x02<<6
#define UART_CFGB_INACTIVE		0x03<<6

/* Register block 2 - Consumer IR - not used */

/* Register block 3 - Identification Registers! */

#define UART_ID_HIGH	0x00   /* 0x10 */
#define UART_ID_LOW	0x01   /* 0xB8 */
#define UART_CHIP_ID 	0x02   /* 0xF1 */
#define UART_VERSION	0x03   /* 0x01 */
#define UART_INTERFACE	0x04   /* low 4 = DMA, high 4 = IRQ */

/* Register block 4 - IrDA */
#define UART_CONTROL        0x00
#define UART_BOF_COUNT_LO      0x01
#define UART_BRICKWALL_CNT_LO 0x02
#define UART_BRICKWALL_TX_CNT_HI   0x03
#define UART_TX_SIZE_LO   0x04
#define UART_RX_SIZE_HI   0x05
#define UART_RX_SIZE_LO   0x06

#define UART_1152     0x01<<7
#define UART_CRC      0x01<<6

/* For storing entries in the status FIFO */
struct st_fifo_entry {
	int status;
	int len;
};

struct st_fifo {
	struct st_fifo_entry entries[10];
	int head;
	int tail;
	int len;
};

/* Private data for each instance */
struct ircc_cb {
	struct net_device *netdev; /* Yes! we are some kind of netdevice */
	struct net_device_stats stats;
	
	struct irlap_cb    *irlap; /* The link layer we are binded to */
	
	struct chipio_t io;        /* IrDA controller information */
	struct iobuff_t tx_buff;   /* Transmit buffer */
	struct iobuff_t rx_buff;   /* Receive buffer */
	struct qos_info qos;       /* QoS capabilities for this device */

	struct irport_cb irport;
	
	__u32 flags;               /* Interface flags */

	struct st_fifo st_fifo;

	int tx_buff_offsets[10]; /* Offsets between frames in tx_buff */
	int tx_len;              /* Number of frames in tx_buff */


};

#endif

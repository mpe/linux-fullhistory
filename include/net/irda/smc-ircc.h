/*********************************************************************
 *                
 * Filename:      smc-ircc.h
 * Version:       0.3
 * Description:   Definitions for the SMC IrCC chipset
 * Status:        Experimental.
 * Author:        Thomas Davis (tadavis@jps.net)
 *
 *     Copyright (c) 1999-2000, Dag Brattli <dagb@cs.uit.no>
 *     Copyright (c) 1998-1999, Thomas Davis (tadavis@jps.net>
 *     All Rights Reserved
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *
 ********************************************************************/

#ifndef SMC_IRCC_H
#define SMC_IRCC_H

#include <linux/spinlock.h>

#include <net/irda/irport.h>

/* DMA modes needed */
#define DMA_TX_MODE              0x08    /* Mem to I/O, ++, demand. */
#define DMA_RX_MODE              0x04    /* I/O to mem, ++, demand. */

#define IRCC_MASTER              0x07
#define IRCC_MASTER_POWERDOWN	 1<<7
#define IRCC_MASTER_RESET        1<<6
#define IRCC_MASTER_INT_EN       1<<5
#define IRCC_MASTER_ERROR_RESET	 1<<4

/* Register block 0 */
#define IRCC_IIR                 0x01
#define IRCC_IER                 0x02
#define IRCC_LSR                 0x03
#define IRCC_LCR_A               0x04
#define IRCC_LCR_B               0x05
#define IRCC_BSR                 0x06

#define IRCC_IIR_ACTIVE_FRAME    1<<7
#define IRCC_IIR_EOM             1<<6
#define IRCC_IIR_RAW_MODE        1<<5
#define IRCC_IIR_FIFO		 1<<4

#define IRCC_IER_ACTIVE_FRAME	 1<<7
#define IRCC_IER_EOM 		 1<<6
#define IRCC_IER_RAW_MODE        1<<5
#define IRCC_IER_FIFO		 1<<4

#define IRCC_LSR_UNDERRUN        1<<7
#define IRCC_LSR_OVERRUN         1<<6
#define IRCC_LSR_FRAME_ERROR     1<<5
#define IRCC_LSR_SIZE_ERROR      1<<4
#define IRCC_LSR_CRC_ERROR       1<<3
#define IRCC_LSR_FRAME_ABORT 	 1<<2

#define IRCC_LCR_A_FIFO_RESET    1<<7
#define IRCC_LCR_A_FAST          1<<6
#define IRCC_LCR_A_GP_DATA       1<<5
#define IRCC_LCR_A_RAW_TX        1<<4
#define IRCC_LCR_A_RAW_RX        1<<3
#define IRCC_LCR_A_ABORT         1<<2
#define IRCC_LCR_A_DATA_DONE     1<<1

#define IRCC_LCR_B_SCE_DISABLED  0x00<<6
#define IRCC_LCR_B_SCE_TRANSMIT  0x01<<6
#define IRCC_LCR_B_SCE_RECEIVE	 0x02<<6
#define IRCC_LCR_B_SCE_UNDEFINED 0x03<<6
#define IRCC_LCR_B_SIP_ENABLE	 1<<5
#define IRCC_LCR_B_BRICK_WALL    1<<4

#define IRCC_BSR_NOT_EMPTY	 1<<7
#define IRCC_BSR_FIFO_FULL	 1<<6
#define IRCC_BSR_TIMEOUT	 1<<5

/* Register block 1 */
#define IRCC_SCE_CFGA	         0x00
#define IRCC_SCE_CFGB	         0x01
#define IRCC_FIFO_THRESHOLD	 0x02

#define IRCC_CFGA_AUX_IR	 0x01<<7
#define IRCC_CFGA_HALF_DUPLEX	 0x01<<2
#define IRCC_CFGA_TX_POLARITY	 0x01<<1
#define IRCC_CFGA_RX_POLARITY	 0x01

#define IRCC_CFGA_COM		 0x00<<3
#define IRCC_CFGA_IRDA_SIR_A	 0x01<<3
#define IRCC_CFGA_ASK_SIR	 0x02<<3
#define IRCC_CFGA_IRDA_SIR_B	 0x03<<3
#define IRCC_CFGA_IRDA_HDLC 	 0x04<<3
#define IRCC_CFGA_IRDA_4PPM 	 0x05<<3
#define IRCC_CFGA_CONSUMER	 0x06<<3
#define IRCC_CFGA_RAW_IR	 0x07<<3
#define IRCC_CFGA_OTHER		 0x08<<3

#define IRCC_IR_HDLC             0x04
#define IRCC_IR_4PPM             0x01
#define IRCC_IR_CONSUMER         0x02

#define IRCC_CFGB_LOOPBACK       0x01<<5
#define IRCC_CFGB_LPBCK_TX_CRC	 0x01<<4
#define IRCC_CFGB_NOWAIT	 0x01<<3
#define IRCC_CFGB_STRING_MOVE	 0x01<<2
#define IRCC_CFGB_DMA_BURST 	 0x01<<1
#define IRCC_CFGB_DMA_ENABLE	 0x01

#define IRCC_CFGB_COM		 0x00<<6
#define IRCC_CFGB_IR		 0x01<<6
#define IRCC_CFGB_AUX		 0x02<<6
#define IRCC_CFGB_INACTIVE	 0x03<<6

/* Register block 3 - Identification Registers! */
#define IRCC_ID_HIGH	         0x00   /* 0x10 */
#define IRCC_ID_LOW	         0x01   /* 0xB8 */
#define IRCC_CHIP_ID 	         0x02   /* 0xF1 */
#define IRCC_VERSION	         0x03   /* 0x01 */
#define IRCC_INTERFACE	         0x04   /* low 4 = DMA, high 4 = IRQ */

/* Register block 4 - IrDA */
#define IRCC_CONTROL             0x00
#define IRCC_BOF_COUNT_LO        0x01
#define IRCC_BRICKWALL_CNT_LO    0x02
#define IRCC_BRICKWALL_TX_CNT_HI 0x03
#define IRCC_TX_SIZE_LO          0x04
#define IRCC_RX_SIZE_HI          0x05
#define IRCC_RX_SIZE_LO          0x06

#define IRCC_1152                0x01<<7
#define IRCC_CRC                 0x01<<6

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
	struct irlap_cb    *irlap; /* The link layer we are binded to */
	
	struct chipio_t io;        /* IrDA controller information */
	struct iobuff_t tx_buff;   /* Transmit buffer */
	struct iobuff_t rx_buff;   /* Receive buffer */

	struct irport_cb *irport;

	spinlock_t lock;           /* For serializing operations */
	
	__u32 new_speed;
	__u32 flags;               /* Interface flags */

	struct st_fifo st_fifo;

	int tx_buff_offsets[10];   /* Offsets between frames in tx_buff */
	int tx_len;                /* Number of frames in tx_buff */
};

#endif /* SMC_IRCC_H */

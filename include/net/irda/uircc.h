/*********************************************************************
 *                
 * Filename:      uircc.h
 * Version:       0.1
 * Description:   Driver for the Sharp Universal Infrared 
 *                Communications Controller (UIRCC)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Dec 26 11:00:49 1998
 * Modified at:   Tue Jan 19 23:52:46 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#ifndef UIRCC_H
#define UIRCC_H

/* Control registers (write only) */
#define UIRCC_CR0           0x00 /* Control register 0 */
#define UIRCC_CR0_XMIT_RST  0x20 /* Transmit reset */
#define UIRCC_CR0_RECV_RST  0x10 /* Receive reset */
#define UIRCC_CR0_TMR_RST   0x08 /* Timer reset */
#define UIRCC_CR0_SYS_RST   0x04 /* System reset */
#define UIRCC_CR0_CARR_RST  0x02 /* Carrier latch reset */
#define UIRCC_CR0_CNT_SWT   0x01 /* Transmit/receive length counter reset */

#define UIRCC_CR1           0x01 /* Transmit/receive mode setting register */
#define UIRCC_CR1_RX_DMA    0x80 /* Rx DMA mode */
#define UIRCC_CR1_TX_DMA    0x20 /* Tx DMA mode */
#define UIRCC_CR1_DMA_BRST  0x10 /* DMA burst mode */
#define UIRCC_CR1_MUST_SET  0x0c /* Must be set */

#define UIRCC_CR2           0x02 /* Interrupt mask register */
#define UIRCC_CR2_RECV_OVR  0x40 /* Receive overrun error */
#define UIRCC_CR2_RECV_FRM  0x20 /* Receive frame error */
#define UIRCC_CR2_RECV_END  0x10 /* Receive end */
#define UIRCC_CR2_TMR_OUT   0x08 /* Timer time-out */
#define UIRCC_CR2_XMIT_UNR  0x04 /* Transmit under-run error */
#define UIRCC_CR2_XMIT_END  0x01 /* Transmit end */
#define UIRCC_CR2_RECV_MASK 0x70
#define UIRCC_CR2_XMIT_MASK 0x05

#define UIRCC_CR3           0x03 /* Transmit/receive control */
#define UIRCC_CR3_XMIT_EN   0x80 /* Transmit enable */
#define UIRCC_CR3_TX_CRC_EN 0x40 /* Transmit UIRCC_CRC enable */
#define UIRCC_CR3_RECV_EN   0x20 /* Receive enable */
#define UIRCC_CR3_RX_CRC_EN 0x10 /* Receive CRC enable */
#define UIRCC_CR3_ADDR_CMP  0x08 /* Address comparison enable */
#define UIRCC_CR3_MCAST_EN  0x04 /* Multicast enable */

#define UIRCC_CR4           0x04 /* Transmit data length low byte */
#define UIRCC_CR5           0x05 /* Transmit data length high byte */
#define UIRCC_CR6           0x06 /* Transmit data writing low byte */
#define UIRCC_CR7           0x07 /* Transmit data writing high byte */

#define UIRCC_CR8           0x08 /* Self pole address */

#define UIRCC_CR9           0x09 /* System control 1 */

#define UIRCC_CR10          0x0a /* Modem selection */
#define UIRCC_CR10_SIR      0x22 /* Set SIR mode */
#define UIRCC_CR10_FIR      0x88 /* Set FIR mode */

#define UIRCC_CR11          0x0b /* System control 2 (same as SR11) */
#define UIRCC_CR11_TMR_EN   0x08

#define UIRCC_CR12          0x0c /* Timer counter initial value (low byte) */
#define UIRCC_CR13          0x0d /* Timer counter initial value (high byte) */

/* Status registers (read only) */
#define UIRCC_SR0           0x00 /* Transmit/receive status register */
#define UIRCC_SR0_RX_RDY    0x80 /* Received data ready */
#define UIRCC_SR0_RX_OVR    0x40 /* Receive overrun error */
#define UIRCC_SR0_RX_CRCFRM 0x20 /* Receive CRC or framing error */

#define UIRCC_SR2           0x02 /* Interrupt mask status */

#define UIRCC_SR3           0x03 /* Interrupt factor register */
#define UIRCC_SR3_RX_OVR_ER 0x40 /* Receive overrun error */
#define UIRCC_SR3_RX_FRM_ER 0x20 /* Receive frameing error */
#define UIRCC_SR3_RX_EOF    0x10 /* Receive end of frame */
#define UIRCC_SR3_TMR_OUT   0x08 /* Timer timeout */
#define UIRCC_SR3_TXUR      0x04 /* Transmit underrun */
#define UIRCC_SR3_TX_DONE   0x01 /* Transmit all sent */

#define UIRCC_SR4           0x04 /* TX/RX data length counter low byte */
#define UIRCC_SR5           0x05 /* TX/RX data length counter high byte */

#define UIRCC_SR8           0x08 /* Chip version */

#define UIRCC_SR9           0x09 /* System status 1 */

#define UIRCC_SR10          0x0a /* Modem select status */

#define UIRCC_SR12          0x0c /* Timer counter status (low byte) */
#define UIRCC_SR13          0x0d /* Timer counter status (high byte) */

/* Private data for each instance */
struct uircc_cb {
	struct irda_device idev;
	
	__u8 cr3;                 /* Copy of register sr3 */
};

#define CR3_SET

#endif




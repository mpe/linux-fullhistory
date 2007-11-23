/*
 * ricoh.h 1.9 1999/10/25 20:03:34
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dhinds@pcmcia.sourceforge.org>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_RICOH_H
#define _LINUX_RICOH_H

#define RF5C_MODE_CTL		0x1f	/* Mode control */
#define RF5C_PWR_CTL		0x2f	/* Mixed voltage control */
#define RF5C_CHIP_ID		0x3a	/* Chip identification */
#define RF5C_MODE_CTL_3		0x3b	/* Mode control 3 */

/* I/O window address offset */
#define RF5C_IO_OFF(w)		(0x36+((w)<<1))

/* Flags for RF5C_MODE_CTL */
#define RF5C_MODE_ATA		0x01	/* ATA mode */
#define RF5C_MODE_LED_ENA	0x02	/* IRQ 12 is LED */
#define RF5C_MODE_CA21		0x04
#define RF5C_MODE_CA22		0x08
#define RF5C_MODE_CA23		0x10
#define RF5C_MODE_CA24		0x20
#define RF5C_MODE_CA25		0x40
#define RF5C_MODE_3STATE_BIT7	0x80

/* Flags for RF5C_PWR_CTL */
#define RF5C_PWR_VCC_3V		0x01
#define RF5C_PWR_IREQ_HIGH	0x02
#define RF5C_PWR_INPACK_ENA	0x04
#define RF5C_PWR_5V_DET		0x08
#define RF5C_PWR_TC_SEL		0x10	/* Terminal Count: irq 11 or 15 */
#define RF5C_PWR_DREQ_LOW	0x20
#define RF5C_PWR_DREQ_OFF	0x00	/* DREQ steering control */
#define RF5C_PWR_DREQ_INPACK	0x40
#define RF5C_PWR_DREQ_SPKR	0x80
#define RF5C_PWR_DREQ_IOIS16	0xc0

/* Values for RF5C_CHIP_ID */
#define RF5C_CHIP_RF5C296	0x32
#define RF5C_CHIP_RF5C396	0xb2

/* Flags for RF5C_MODE_CTL_3 */
#define RF5C_MCTL3_DISABLE	0x01	/* Disable PCMCIA interface */
#define RF5C_MCTL3_DMA_ENA	0x02

/* Register definitions for Ricoh PCI-to-CardBus bridges */

#ifndef PCI_VENDOR_ID_RICOH
#define PCI_VENDOR_ID_RICOH		0x1180
#endif
#ifndef PCI_DEVICE_ID_RICOH_RL5C465
#define PCI_DEVICE_ID_RICOH_RL5C465	0x0465
#endif
#ifndef PCI_DEVICE_ID_RICOH_RL5C466
#define PCI_DEVICE_ID_RICOH_RL5C466	0x0466
#endif
#ifndef PCI_DEVICE_ID_RICOH_RL5C475
#define PCI_DEVICE_ID_RICOH_RL5C475	0x0475
#endif
#ifndef PCI_DEVICE_ID_RICOH_RL5C476
#define PCI_DEVICE_ID_RICOH_RL5C476	0x0476
#endif
#ifndef PCI_DEVICE_ID_RICOH_RL5C478
#define PCI_DEVICE_ID_RICOH_RL5C478	0x0478
#endif

/* Extra bits in CB_BRIDGE_CONTROL */
#define RL5C46X_BCR_3E0_ENA		0x0800
#define RL5C46X_BCR_3E2_ENA		0x1000

/* Misc Control Register */
#define RL5C4XX_MISC			0x0082	/* 16 bit */
#define  RL5C4XX_MISC_HW_SUSPEND_ENA	0x0002
#define  RL5C4XX_MISC_VCCEN_POL		0x0100
#define  RL5C4XX_MISC_VPPEN_POL		0x0200
#define  RL5C46X_MISC_SUSPEND		0x0001
#define  RL5C46X_MISC_PWR_SAVE_2	0x0004
#define  RL5C46X_MISC_IFACE_BUSY	0x0008
#define  RL5C46X_MISC_B_LOCK		0x0010
#define  RL5C46X_MISC_A_LOCK		0x0020
#define  RL5C46X_MISC_PCI_LOCK		0x0040
#define  RL5C47X_MISC_IFACE_BUSY	0x0004
#define  RL5C47X_MISC_PCI_INT_MASK	0x0018
#define  RL5C47X_MISC_PCI_INT_DIS	0x0020
#define  RL5C47X_MISC_SUBSYS_WR		0x0040
#define  RL5C47X_MISC_SRIRQ_ENA		0x0080
#define  RL5C47X_MISC_5V_DISABLE	0x0400
#define  RL5C47X_MISC_LED_POL		0x0800

/* 16-bit Interface Control Register */
#define RL5C4XX_16BIT_CTL		0x0084	/* 16 bit */
#define  RL5C4XX_16CTL_IO_TIMING	0x0100
#define  RL5C4XX_16CTL_MEM_TIMING	0x0200
#define  RL5C46X_16CTL_LEVEL_1		0x0010
#define  RL5C46X_16CTL_LEVEL_2		0x0020

/* 16-bit IO and memory timing registers */
#define RL5C4XX_16BIT_IO_0		0x0088	/* 16 bit */
#define RL5C4XX_16BIT_MEM_0		0x0088	/* 16 bit */
#define  RL5C4XX_SETUP_MASK		0x0007
#define  RL5C4XX_SETUP_SHIFT		0
#define  RL5C4XX_CMD_MASK		0x01f0
#define  RL5C4XX_CMD_SHIFT		4
#define  RL5C4XX_HOLD_MASK		0x1c00
#define  RL5C4XX_HOLD_SHIFT		10

#endif /* _LINUX_RICOH_H */

/*
 * ibmstb4.h
 *
 *	This was dirived from the ibm405gp.h and other previus works in ppc4xx.h
 *
 *      Current maintainer
 *      Armin Kuster akuster@mvista.com
 *      DEC, 2001
 *
 *
 * Copyright 2001 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	Version 1.0 (01/10/10) - A. Kuster
 *	Initial version	
 *
 *	Version 1.1 02/01/17 - A. Kuster
 *	moved common offsets to ibm405.h
 */

#ifdef __KERNEL__
#ifndef __ASM_IBMSTB4_H__
#define __ASM_IBMSTB4_H__

#include <linux/config.h>
#include <platforms/4xx/ibm_ocp.h>

/* serial port defines */
#define STB04xxx_IO_BASE	((uint)0xe0000000)
#define PPC4xx_PCI_IO_ADDR	STB04xxx_IO_BASE
#define PPC4xx_ONB_IO_PADDR	STB04xxx_IO_BASE
#define PPC4xx_ONB_IO_VADDR	((uint)0xe0000000)
#define PPC4xx_ONB_IO_SIZE	((uint)14*64*1024)

/*
 * map STB04xxx internal i/o address (0x400x00xx) to an address
 * which is below the 2GB limit...
 *
 * 4000 000x	uart1		-> 0xe000 000x
 * 4001 00xx	ppu
 * 4002 00xx	smart card
 * 4003 000x	iic
 * 4004 000x	uart0
 * 4005 0xxx	timer
 * 4006 00xx	gpio
 * 4007 00xx	smart card
 * 400b 000x	iic
 * 400c 000x	scp
 * 400d 000x	modem
 * 400e 000x	uart2
*/
#define STB04xxx_MAP_IO_ADDR(a)	(((uint)(a)) + (STB04xxx_IO_BASE - 0x40000000))

#define RS_TABLE_SIZE		3
#define UART0_INT		20
#define UART0_IO_BASE		(u8 *)0x40040000
#define UART1_INT		21
#define UART1_IO_BASE		(u8 *)0x40000000
#define UART2_INT		31
#define UART2_IO_BASE		(u8 *)0x400e0000

#define IDE0_IO_ADDR	0x400F0000
#define IDE0_IO_SIZE	0x200

#define IIC0_BASE	0x40030000
#define IIC1_BASE	0x400b0000
#define OPB0_BASE	0x40000000
#define GPIO0_BASE	0x40060000

#define UART_NUMS	3

#define BD_EMAC_ADDR(e,i) bi_enetaddr[i]

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)		\
	STD_UART_OP(1)		\
	STD_UART_OP(2)
#endif

#if defined(CONFIG_UART0_TTYS1)
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(1)		\
	STD_UART_OP(0)		\
	STD_UART_OP(2)
#endif

#if defined(CONFIG_UART0_TTYS2)
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(2)		\
	STD_UART_OP(0)		\
	STD_UART_OP(1)
#endif

#define DCRN_BE_BASE		0x090
#define DCRN_DMA0_BASE		0x0C0
#define DCRN_DMA1_BASE		0x0C8
#define DCRN_DMA2_BASE		0x0D0
#define DCRN_DMA3_BASE		0x0D8
#define DCRNCAP_DMA_CC		1	/* have DMA chained count capability */
#define DCRN_DMASR_BASE		0x0E0
#define DCRN_PLB0_BASE		0x054
#define DCRN_PLB1_BASE		0x064
#define DCRN_POB0_BASE		0x0B0
#define DCRN_SCCR_BASE		0x120
#define DCRN_UIC0_BASE		0x040
#define DCRN_BE_BASE		0x090
#define DCRN_DMA0_BASE		0x0C0
#define DCRN_DMA1_BASE		0x0C8
#define DCRN_DMA2_BASE		0x0D0
#define DCRN_DMA3_BASE		0x0D8
#define DCRN_CIC_BASE 		0x030
#define DCRN_DMASR_BASE		0x0E0
#define DCRN_EBIMC_BASE		0x070
#define DCRN_DCRX_BASE		0x020
#define DCRN_CPMFR_BASE		0x102
#define DCRN_SCCR_BASE		0x120

#define CPM_IIC0	0x80000000	/* IIC 0 interface */
#define CPM_I1284	0x40000000	/* IEEE-1284 */
#define CPM_IIC1	0x20000000	/* IIC 1 interface */
#define CPM_CPU		0x10000000	/* PPC405B3 clock control */
#define CPM_AUD		0x08000000	/* Audio Decoder */
#define CPM_EBIU	0x04000000	/* External Bus Interface Unit */
#define CPM_SDRAM1	0x02000000	/* SDRAM 1 memory controller */
#define CPM_DMA		0x01000000	/* DMA controller */
#define CPM_RES_1	0x00800000	/* reserved */
#define CPM_RES_2	0x00400000	/* reserved */
#define CPM_RES_3	0x00200000	/* reserved */
#define CPM_UART1	0x00100000	/* Serial 1 / Infrared */
#define CPM_UART0	0x00080000	/* Serial 0 / 16550 */
#define CPM_DCRX	0x00040000	/* DCR Extension */
#define CPM_SC0		0x00020000	/* Smart Card 0 */
#define CPM_RES_4	0x00010000	/* reserved */
#define CPM_SC1		0x00008000	/* Smart Card 1 */
#define CPM_SDRAM0	0x00004000	/* SDRAM 0 memory controller */
#define CPM_XPT54	0x00002000	/* Transport - 54 Mhz */
#define CPM_CBS		0x00001000	/* Cross Bar Switch */
#define CPM_GPT		0x00000800	/* GPTPWM */
#define CPM_GPIO0	0x00000400	/* General Purpose IO 0 */
#define CPM_DENC	0x00000200	/* Digital video Encoder */
#define CPM_TMRCLK	0x00000100	/* CPU timers */
#define CPM_XPT27	0x00000080	/* Transport - 27 Mhz */
#define CPM_UIC		0x00000040	/* Universal Interrupt Controller */
#define CPM_RES_5	0x00000020	/* reserved */
#define CPM_MSI		0x00000010	/* Modem Serial Interface (SSP) */
#define CPM_UART2	0x00000008	/* Serial Control Port */
#define CPM_DSCR	0x00000004	/* Descrambler */
#define CPM_VID2	0x00000002	/* Video Decoder clock domain 2 */
#define CPM_RES_6	0x00000001	/* reserved */

/*			0x80000000 */
#define UIC_XPORT	0x40000000	/* 1 Transport */
#define UIC_AUDIO	0x20000000	/* 2 Audio Decoder */
#define UIC_VIDEO	0x10000000	/* 3 Video Decoder */
#define UIC_D0		0x08000000	/* 4 DMA Channel 0 */
#define UIC_D1		0x04000000	/* 5 DMA Channel 1 */
#define UIC_D2		0x02000000	/* 6 DMA Channel 2 */
#define UIC_D3		0x01000000	/* 7 DMA Channel 3 */
#define UIC_SC0		0x00800000	/* 8 SmartCard 0 Controller */
#define UIC_IIC0	0x00400000	/* 9 IIC 0 */
#define UIC_IIC1	0x00200000	/* 10 IIC 1 */
#define UIC_PWM0	0x00100000	/* 11 GPT_PWM 0: Capture Timers */
#define UIC_PWM1	0x00080000	/* 12 GPT_PWM 1: Compare Timers */
#define UIC_SCP		0x00040000	/* 13 Serial Control Port */
#define UIC_SSP		0x00020000	/* 14 Soft Modem/Synchronous Serial Port */
#define UIC_PWM2	0x00010000	/* 15 GPT_PWM 2: Down Counters */
#define UIC_SC1		0x00008000	/* 16 SmartCard 1 Controller */
#define UIC_EIR7	0x00004000	/* 17 External IRQ 7 */
#define UIC_EIR8	0x00002000	/* 18 External IRQ 8 */
#define UIC_EIR9	0x00001000	/* 19 External IRQ 9 */
#define UIC_U0		0x00000800	/* 20 UART0 */
#define UIC_IR_RCV	0x00000400	/* 21 Serial 1 / Infrared UART Receive */
#define UIC_IR_XMIT	0x00000200	/* 22 Serial 1 / Infrared UART Transmit */
#define UIC_IEEE1284	0x00000100	/* 23 IEEE-1284 / PPU */
#define UIC_DCRX	0x00000080	/* 24 DCRX */
#define UIC_EIR0	0x00000040	/* 25 External IRQ 0 */
#define UIC_EIR1	0x00000020	/* 26 External IRQ 1 */
#define UIC_EIR2	0x00000010	/* 27 External IRQ 2 */
#define UIC_EIR3	0x00000008	/* 28 External IRQ 3 */
#define UIC_EIR4	0x00000004	/* 29 External IRQ 4 */
#define UIC_EIR5	0x00000002	/* 30 External IRQ 5 */
#define UIC_EIR6	0x00000001	/* 31 External IRQ 6 */

#define DCRN_BEAR	(DCRN_BE_BASE + 0x0)	/* Bus Error Address Register */
#define DCRN_BESR	(DCRN_BE_BASE + 0x1)	/* Bus Error Syndrome Register */
/* DCRN_BESR */
#define BESR_DSES	0x80000000	/* Data-Side Error Status */
#define BESR_DMES	0x40000000	/* DMA Error Status */
#define BESR_RWS	0x20000000	/* Read/Write Status */
#define BESR_ETMASK	0x1C000000	/* Error Type */
#define ET_PROT		0
#define ET_PARITY	1
#define ET_NCFG		2
#define ET_BUSERR	4
#define ET_BUSTO	6

#define CHR1_CETE	0x00800000	/* CPU external timer enable */
#define CHR1_PCIPW	0x00008000	/* PCI Int enable/Peripheral Write enable */

#define DCRN_CICCR	(DCRN_CIC_BASE + 0x0)	/* CIC Control Register */
#define DCRN_DMAS1	(DCRN_CIC_BASE + 0x1)	/* DMA Select1 Register */
#define DCRN_DMAS2	(DCRN_CIC_BASE + 0x2)	/* DMA Select2 Register */
#define DCRN_CICVCR	(DCRN_CIC_BASE + 0x3)	/* CIC Video COntro Register */
#define DCRN_CICSEL3	(DCRN_CIC_BASE + 0x5)	/* CIC Select 3 Register */
#define DCRN_SGPO	(DCRN_CIC_BASE + 0x6)	/* CIC GPIO Output Register */
#define DCRN_SGPOD	(DCRN_CIC_BASE + 0x7)	/* CIC GPIO OD Register */
#define DCRN_SGPTC	(DCRN_CIC_BASE + 0x8)	/* CIC GPIO Tristate Ctrl Reg */
#define DCRN_SGPI	(DCRN_CIC_BASE + 0x9)	/* CIC GPIO Input Reg */

#define DCRN_DCRXICR	(DCRN_DCRX_BASE + 0x0)	/* Internal Control Register */
#define DCRN_DCRXISR	(DCRN_DCRX_BASE + 0x1)	/* Internal Status Register */
#define DCRN_DCRXECR	(DCRN_DCRX_BASE + 0x2)	/* External Control Register */
#define DCRN_DCRXESR	(DCRN_DCRX_BASE + 0x3)	/* External Status Register */
#define DCRN_DCRXTAR	(DCRN_DCRX_BASE + 0x4)	/* Target Address Register */
#define DCRN_DCRXTDR	(DCRN_DCRX_BASE + 0x5)	/* Target Data Register */
#define DCRN_DCRXIGR	(DCRN_DCRX_BASE + 0x6)	/* Interrupt Generation Register */
#define DCRN_DCRXBCR	(DCRN_DCRX_BASE + 0x7)	/* Line Buffer Control Register */

#define DCRN_BRCRH0	(DCRN_EBIMC_BASE + 0x0)	/* Bus Region Config High 0 */
#define DCRN_BRCRH1	(DCRN_EBIMC_BASE + 0x1)	/* Bus Region Config High 1 */
#define DCRN_BRCRH2	(DCRN_EBIMC_BASE + 0x2)	/* Bus Region Config High 2 */
#define DCRN_BRCRH3	(DCRN_EBIMC_BASE + 0x3)	/* Bus Region Config High 3 */
#define DCRN_BRCRH4	(DCRN_EBIMC_BASE + 0x4)	/* Bus Region Config High 4 */
#define DCRN_BRCRH5	(DCRN_EBIMC_BASE + 0x5)	/* Bus Region Config High 5 */
#define DCRN_BRCRH6	(DCRN_EBIMC_BASE + 0x6)	/* Bus Region Config High 6 */
#define DCRN_BRCRH7	(DCRN_EBIMC_BASE + 0x7)	/* Bus Region Config High 7 */
#define DCRN_BRCR0	(DCRN_EBIMC_BASE + 0x10)	/* BRC 0 */
#define DCRN_BRCR1	(DCRN_EBIMC_BASE + 0x11)	/* BRC 1 */
#define DCRN_BRCR2	(DCRN_EBIMC_BASE + 0x12)	/* BRC 2 */
#define DCRN_BRCR3	(DCRN_EBIMC_BASE + 0x13)	/* BRC 3 */
#define DCRN_BRCR4	(DCRN_EBIMC_BASE + 0x14)	/* BRC 4 */
#define DCRN_BRCR5	(DCRN_EBIMC_BASE + 0x15)	/* BRC 5 */
#define DCRN_BRCR6	(DCRN_EBIMC_BASE + 0x16)	/* BRC 6 */
#define DCRN_BRCR7	(DCRN_EBIMC_BASE + 0x17)	/* BRC 7 */
#define DCRN_BEAR0	(DCRN_EBIMC_BASE + 0x20)	/* Bus Error Address Register */
#define DCRN_BESR0	(DCRN_EBIMC_BASE + 0x21)	/* Bus Error Status Register */
#define DCRN_BIUCR	(DCRN_EBIMC_BASE + 0x2A)	/* Bus Interfac Unit Ctrl Reg */

#include <asm/ibm405.h>

#endif				/* __ASM_IBMSTB4_H__ */
#endif				/* __KERNEL__ */

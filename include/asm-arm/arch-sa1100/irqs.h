/*
 * linux/include/asm-arm/arch-sa1100/irqs.h
 *
 * Copyright (C) 1996 Russell King
 * Copyright (C) 1998 Deborah Wallach (updates for SA1100/Brutus).
 */
#include <linux/config.h>

#ifdef CONFIG_SA1101
#define NR_IRQS                 95

#define GPAIN0          32
#define GPAIN1		33
#define GPAIN2		34
#define GPAIN3		35
#define GPAIN4		36
#define GPAIN5		37
#define GPAIN6		38
#define GPAIN7		39
#define GPBIN0		40
#define GPBIN1		41
#define GPBIN2		42
#define GPBIN3		43
#define GPBIN4		44
#define GPBIN5		45
#define GPBIN6		46
#define RESERVED	47
#define KPXIN0		48
#define KPXIN1		49
#define KPXIN2		50
#define KPXIN3		51
#define KPXIN4		52
#define KPXIN5		53
#define KPXIN6		54
#define KPXIN7		55
#define KPYIN0		56
#define KPYIN1		57
#define KPYIN2		58
#define KPYIN3		59
#define KPYIN4		60
#define KPYIN5		61
#define KPYIN6		62
#define KPYIN7		63
#define KPYIN8		64
#define KPYIN9		65
#define KPYIN10		66
#define KPYIN11		67
#define KPYIN12		68
#define KPYIN13		69
#define KPYIN14		70
#define KPYIN15		71
#define MSTXINT		72
#define MSRXINT		73
#define TPTXINT		74
#define TPRXINT		75
#define INTREQTRC	76
#define INTREQTIM	77
#define INTREQRAV	78
#define INTREQINT	79
#define INTREQEMP	80
#define INTREQDAT	81
#define VIDEOINT	82
#define FIFOINT		83
#define NIRQHCIM	84
#define IRQHCIBUFFACC	85
#define IRQHCIRMTWKP	86
#define NHCIMFCLIR	87
#define USBERROR	88
#define S0_READY_NIREQ	89
#define S1_READY_NIREQ	90
#define S0_CDVALID	91
#define S1_CDVALID	92
#define S0_BVD1_STSCHG	93
#define S1_BVD1_STSCHG	94
#define USB_PORT_RESUME	95

#else
#define NR_IRQS                 32
#endif

#define	IRQ_GPIO0		0
#define	IRQ_GPIO1		1
#define	IRQ_GPIO2		2
#define	IRQ_GPIO3		3
#define	IRQ_GPIO4		4
#define	IRQ_GPIO5		5
#define	IRQ_GPIO6		6
#define	IRQ_GPIO7		7
#define	IRQ_GPIO8		8
#define	IRQ_GPIO9		9
#define	IRQ_GPIO10		10
#define	IRQ_GPIO11_27		11
#define	IRQ_LCD  		12	/* LCD controller                  */
#define IRQ_Ser0UDC		13	/* Ser. port 0 UDC                 */
#define IRQ_Ser1SDLC		14	/* Ser. port 1 SDLC                */
#define IRQ_Ser1UART		15	/* Ser. port 1 UART                */
#define IRQ_Ser2ICP		16	/* Ser. port 2 ICP                 */
#define IRQ_Ser3UART		17	/* Ser. port 3 UART                */
#define IRQ_Ser4MCP		18	/* Ser. port 4 MCP                 */
#define IRQ_Ser4SSP		19	/* Ser. port 4 SSP                 */
#define IRQ_DMA0 		20	/* DMA controller channel 0        */
#define IRQ_DMA1 		21	/* DMA controller channel 1        */
#define IRQ_DMA2 		22	/* DMA controller channel 2        */
#define IRQ_DMA3 		23	/* DMA controller channel 3        */
#define IRQ_DMA4 		24	/* DMA controller channel 4        */
#define IRQ_DMA5 		25	/* DMA controller channel 5        */
#define IRQ_OST0 		26	/* OS Timer match 0                */
#define IRQ_OST1 		27	/* OS Timer match 1                */
#define IRQ_OST2 		28	/* OS Timer match 2                */
#define IRQ_OST3 		29	/* OS Timer match 3                */
#define IRQ_RTC1Hz		30	/* RTC 1 Hz clock                  */
#define IRQ_RTCAlrm		31	/* RTC Alarm                       */

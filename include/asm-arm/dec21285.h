/*
 * include/asm-arm/dec21285.h
 *
 * Copyright (C) 1998 Russell King
 */
#define DC21285_PCI_IACK		0x79000000
#define DC21285_ARMCSR_BASE		0x42000000
#define DC21285_PCI_TYPE_0_CONFIG	0x7b000000
#define DC21285_PCI_TYPE_1_CONFIG	0x7a000000
#define DC21285_OUTBOUND_WRITE_FLUSH	0x78000000
#define DC21285_FLASH			0x41000000
#define DC21285_PCI_IO			0x7c000000
#define DC21285_PCI_MEM			0x80000000

#ifndef __ASSEMBLY__

/*
 * DEC21285
 */
#define CSR_SA110_CNTL		((volatile unsigned long *)0xfe00013c)
#define CSR_PCIADDR_EXTN	((volatile unsigned long *)0xfe000140)
#define CSR_PREFETCHMEMRANGE	((volatile unsigned long *)0xfe000144)
#define CSR_XBUS_CYCLE		((volatile unsigned long *)0xfe000148)
#define CSR_XBUS_IOSTROBE	((volatile unsigned long *)0xfe00014c)
#define CSR_DOORBELL_PCI	((volatile unsigned long *)0xfe000150)
#define CSR_DOORBELL_SA110	((volatile unsigned long *)0xfe000154)

#define CSR_UARTDR		((volatile unsigned long *)0xfe000160)
#define CSR_RXSTAT		((volatile unsigned long *)0xfe000164)
#define CSR_H_UBRLCR		((volatile unsigned long *)0xfe000168)
#define CSR_M_UBRLCR		((volatile unsigned long *)0xfe00016c)
#define CSR_L_UBRLCR		((volatile unsigned long *)0xfe000170)
#define CSR_UARTCON		((volatile unsigned long *)0xfe000174)
#define CSR_UARTFLG		((volatile unsigned long *)0xfe000178)

#define CSR_IRQ_STATUS		((volatile unsigned long *)0xfe000180)
#define CSR_IRQ_RAWSTATUS	((volatile unsigned long *)0xfe000184)
#define CSR_IRQ_ENABLE		((volatile unsigned long *)0xfe000188)
#define CSR_IRQ_DISABLE		((volatile unsigned long *)0xfe00018c)
#define CSR_IRQ_SOFT		((volatile unsigned long *)0xfe000190)

#define CSR_FIQ_STATUS		((volatile unsigned long *)0xfe000280)
#define CSR_FIQ_RAWSTATUS	((volatile unsigned long *)0xfe000284)
#define CSR_FIQ_ENABLE		((volatile unsigned long *)0xfe000288)
#define CSR_FIQ_DISABLE		((volatile unsigned long *)0xfe00028c)
#define CSR_FIQ_SOFT		((volatile unsigned long *)0xfe000290)

#define CSR_TIMER1_LOAD		((volatile unsigned long *)0xfe000300)
#define CSR_TIMER1_VALUE	((volatile unsigned long *)0xfe000304)
#define CSR_TIMER1_CNTL		((volatile unsigned long *)0xfe000308)
#define CSR_TIMER1_CLR		((volatile unsigned long *)0xfe00030c)

#define CSR_TIMER2_LOAD		((volatile unsigned long *)0xfe000320)
#define CSR_TIMER2_VALUE	((volatile unsigned long *)0xfe000324)
#define CSR_TIMER2_CNTL		((volatile unsigned long *)0xfe000328)
#define CSR_TIMER2_CLR		((volatile unsigned long *)0xfe00032c)

#define CSR_TIMER3_LOAD		((volatile unsigned long *)0xfe000340)
#define CSR_TIMER3_VALUE	((volatile unsigned long *)0xfe000344)
#define CSR_TIMER3_CNTL		((volatile unsigned long *)0xfe000348)
#define CSR_TIMER3_CLR		((volatile unsigned long *)0xfe00034c)

#define CSR_TIMER4_LOAD		((volatile unsigned long *)0xfe000360)
#define CSR_TIMER4_VALUE	((volatile unsigned long *)0xfe000364)
#define CSR_TIMER4_CNTL		((volatile unsigned long *)0xfe000368)
#define CSR_TIMER4_CLR		((volatile unsigned long *)0xfe00036c)

#else

#define CSR_SA110_CNTL		0x13c
#define CSR_PCIADDR_EXTN	0x140
#define CSR_PREFETCHMEMRANGE	0x144
#define CSR_XBUS_CYCLE		0x148
#define CSR_XBUS_IOSTROBE	0x14c
#define CSR_DOORBELL_PCI	0x150
#define CSR_DOORBELL_SA110	0x154

#define CSR_UARTDR		0x160
#define CSR_RXSTAT		0x164
#define CSR_H_UBRLCR		0x168
#define CSR_M_UBRLCR		0x16c
#define CSR_L_UBRLCR		0x170
#define CSR_UARTCON		0x174
#define CSR_UARTFLG		0x178

#define CSR_IRQ_STATUS		0x180
#define CSR_IRQ_RAWSTATUS	0x184
#define CSR_IRQ_ENABLE		0x188
#define CSR_IRQ_DISABLE		0x18c
#define CSR_IRQ_SOFT		0x190

#define CSR_FIQ_STATUS		0x280
#define CSR_FIQ_RAWSTATUS	0x284
#define CSR_FIQ_ENABLE		0x288
#define CSR_FIQ_DISABLE		0x28c
#define CSR_FIQ_SOFT		0x290

#define CSR_TIMER1_LOAD		0x300
#define CSR_TIMER1_VALUE	0x304
#define CSR_TIMER1_CNTL		0x308
#define CSR_TIMER1_CLR		0x30c

#define CSR_TIMER2_LOAD		0x320
#define CSR_TIMER2_VALUE	0x324
#define CSR_TIMER2_CNTL		0x328
#define CSR_TIMER2_CLR		0x32c

#define CSR_TIMER3_LOAD		0x340
#define CSR_TIMER3_VALUE	0x344
#define CSR_TIMER3_CNTL		0x348
#define CSR_TIMER3_CLR		0x34c

#define CSR_TIMER4_LOAD		0x360
#define CSR_TIMER4_VALUE	0x364
#define CSR_TIMER4_CNTL		0x368
#define CSR_TIMER4_CLR		0x36c

#endif

#define TIMER_CNTL_ENABLE	(1 << 7)
#define TIMER_CNTL_AUTORELOAD	(1 << 6)
#define TIMER_CNTL_DIV1		(0)
#define TIMER_CNTL_DIV16	(1 << 2)
#define TIMER_CNTL_DIV256	(2 << 2)
#define TIMER_CNTL_CNTEXT	(3 << 2)



/*
 * linux/include/asm-arm/arch-vnc/hardware.h
 *
 * Copyright (C) 1998 Corel Computer/Russell King.
 *
 * This file contains the hardware definitions of the VNC.
 */

/*    Logical    Physical
 * 0xfff00000	0x40000000	X-Bus
 * 0xffe00000	0x7c000000	PCI I/O space
 *
 * 0xfe000000	0x42000000	CSR
 * 0xfd000000	0x78000000	Outbound write flush
 * 0xfc000000	0x79000000	PCI IACK/special space
 *
 * 0xf9030000	0x7a080000	PCI Config type 1 card 4
 * 0xf9020000	0x7a040000	PCI Config type 1 card 3
 * 0xf9010000	0x7a020000	PCI Config type 1 card 2
 * 0xf9000000	0x7a010000	PCI Config type 1 card 1
 *
 * 0xf8030000	0x7b080000	PCI Config type 0 card 4
 * 0xf8020000	0x7b040000	PCI Config type 0 card 3
 * 0xf8010000	0x7b020000	PCI Config type 0 card 2
 * 0xf8000000	0x7b010000	PCI Config type 0 card 1
 * 
 */
 
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

#define TIMER_CNTL_ENABLE	(1 << 7)
#define TIMER_CNTL_AUTORELOAD	(1 << 6)
#define TIMER_CNTL_DIV1		(0)
#define TIMER_CNTL_DIV16	(1 << 2)
#define TIMER_CNTL_DIV256	(2 << 2)
#define TIMER_CNTL_CNTEXT	(3 << 2)

/* LEDs */
#define XBUS_LEDS		((volatile unsigned char *)0xfff12000)
#define XBUS_LED_AMBER		(1 << 0)
#define XBUS_LED_GREEN		(1 << 1)
#define XBUS_LED_RED		(1 << 2)
#define XBUS_LED_TOGGLE		(1 << 8)

/* PIC irq control */
#define PIC_LO			0x20
#define PIC_MASK_LO		0x21
#define PIC_HI			0xA0
#define PIC_MASK_HI		0xA1

#define IO_END			0xffffffff
#define IO_BASE			0xe0000000
#define IO_SIZE			(IO_END - IO_BASE)

#define HAS_PCIO

#define XBUS_SWITCH		((volatile unsigned char *)0xfff12000)
#define XBUS_SWITCH_SWITCH	((*XBUS_SWITCH) & 15)
#define XBUS_SWITCH_J17_13	((*XBUS_SWITCH) & (1 << 4))
#define XBUS_SWITCH_J17_11	((*XBUS_SWITCH) & (1 << 5))
#define XBUS_SWITCH_J17_9	((*XBUS_SWITCH) & (1 << 6))

#define PCIO_BASE		0xffe00000

#define KERNTOPHYS(a)		((unsigned long)(&a))

#define PARAMS_OFFSET		0x0100
#define PARAMS_BASE		(PAGE_OFFSET + PARAMS_OFFSET)

#define SAFE_ADDR		0x50000000


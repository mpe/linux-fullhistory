/*
 * linux/include/asm-arm/arch-vnc/hardware.h
 *
 * Copyright (C) 1998 Corel Computer/Russell King.
 *
 * This file contains the hardware definitions of the VNC.
 */

/*    Logical    Physical
 * 0xffe00000	0x7c000000	PCI I/O space
 * 0xfe000000	0x42000000	CSR
 * 0xfd000000	0x78000000	Outbound write flush
 * 0xfc000000	0x79000000	PCI IACK/special space
 * 0xf9000000	0x7a000000	PCI Config type 1
 * 0xf8000000	0x7b000000	PCI Config type 0
 * 
 */

#include <asm/dec21285.h>

#define IO_BASE_ARM_CSR		0xfe000000
#define PCI_IACK		0xfc000000
 
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
#define PCIO_BASE		0xffe00000

#define KERNTOPHYS(a)		((unsigned long)(&a))

//#define PARAMS_OFFSET		0x0100
//#define PARAMS_BASE		(PAGE_OFFSET + PARAMS_OFFSET)

#define FLUSH_BASE_PHYS		0x50000000

/* GPIO pins */
#define GPIO_CCLK		0x800
#define GPIO_DSCLK		0x400
#define GPIO_E2CLK		0x200
#define GPIO_IOLOAD		0x100
#define GPIO_RED_LED		0x080
#define GPIO_WDTIMER		0x040
#define GPIO_DATA		0x020
#define GPIO_IOCLK		0x010
#define GPIO_DONE		0x008
#define GPIO_FAN		0x004
#define GPIO_GREEN_LED		0x002
#define GPIO_RESET		0x001

/* CPLD pins */
#define CPLD_DSRESET		8
#define CPLD_UNMUTE		2

#ifndef __ASSEMBLY__
extern void gpio_modify_op(int mask, int set);
extern void gpio_modify_io(int mask, int in);
extern int  gpio_read(void);
extern void cpld_modify(int mask, int set);
#endif

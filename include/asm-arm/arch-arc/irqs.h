/*
 * linux/include/asm-arm/arch-arc/irqs.h
 *
 * Copyright (C) 1996 Russell King, Dave Gilbert (gilbertd@cs.man.ac.uk)
 */

#define IRQ_PRINTERBUSY		0
#define IRQ_SERIALRING		1
#define IRQ_PRINTERACK		2
#define IRQ_VSYNCPULSE		3
#define IRQ_POWERON		4
#define IRQ_TIMER0		5
#define IRQ_TIMER1		6
#define IRQ_IMMEDIATE		7
#define IRQ_EXPCARDFIQ		8
#define IRQ_SOUNDCHANGE		9
#define IRQ_SERIALPORT		10
#define IRQ_HARDDISK		11
#define IRQ_FLOPPYCHANGED	12
#define IRQ_EXPANSIONCARD	13
#define IRQ_KEYBOARDTX		14
#define IRQ_KEYBOARDRX		15

#define FIQ_FLOPPYDATA		0
#define FIQ_FLOPPYIRQ		1
#define FIQ_ECONET		2
#define FIQ_EXPANSIONCARD	6
#define FIQ_FORCE		7

#define FIQ_FD1772		FIQ_FLOPPYIRQ

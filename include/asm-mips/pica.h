/*
 * Hardware info about Acer PICA 61 and similar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Andreas Busse and Ralf Baechle
 */
#ifndef __ASM_MIPS_PICA_H 
#define __ASM_MIPS_PICA_H 

/*
 * The addresses below are virtual address. The mappings are
 * created on startup via wired entries in the tlb. The Mips
 * Magnum R3000 and R4000 machines are similar in many aspects,
 * but many hardware register are accessible at 0xb9000000 in
 * instead of 0xe0000000.
 */

/*
 * Revision numbers in PICA_ASIC_REVISION
 *
 * 0xf0000000 - Rev1
 * 0xf0000001 - Rev2
 * 0xf0000002 - Rev3
 */
#define PICA_ASIC_REVISION      0xe0000008

/*
 * The segments of the seven segment LED are mapped
 * to the control bits as follows:
 *
 *         (7)
 *      ---------
 *      |       |
 *  (2) |       | (6)
 *      |  (1)  |
 *      ---------
 *      |       |
 *  (3) |       | (5)
 *      |  (4)  |
 *      --------- . (0)
 */
#define PICA_LED                0xe000f000

/*
 * Some characters for the LED control registers
 * The original Mips machines seem to have a LED display
 * with integrated decoder while the Acer machines can
 * control each of the seven segments and the dot independently.
 * It only a toy, anyway...
 */
#define LED_DOT                 0x01
#define LED_SPACE               0x00
#define LED_0                   0xfc
#define LED_1                   0x60
#define LED_2                   0xda
#define LED_3                   0xf2
#define LED_4                   0x66
#define LED_5                   0xb6
#define LED_6                   0xbe
#define LED_7                   0xe0
#define LED_8                   0xfe
#define LED_9                   0xf6
#define LED_A                   0xee
#define LED_b                   0x3e
#define LED_C                   0x9c
#define LED_d                   0x7a
#define LED_E                   0x9e
#define LED_F                   0x8e

#ifndef __LANGUAGE_ASSEMBLY__

extern __inline__ void pica_set_led(unsigned int bits)
{
	volatile unsigned int *led_register = (unsigned int *) PICA_LED;

	*led_register = bits;
}

#endif

/*
 * i8042 keyboard controller for PICA chipset.
 * This address is just a guess and seems to differ
 * from the other mips machines...
 */
#define PICA_KEYBOARD_ADDRESS   0xe0005000
#define PICA_KEYBOARD_DATA      0xe0005000
#define PICA_KEYBOARD_COMMAND   0xe0005001

#ifndef __LANGUAGE_ASSEMBLY__

typedef struct {
	unsigned char data;
	unsigned char command;
} pica_keyboard_hardware;

typedef struct {
	unsigned char pad0[3];
	unsigned char data;
	unsigned char pad1[3];
	unsigned char command;
} mips_keyboard_hardware;

/*
 * For now
 */
#define keyboard_hardware       pica_keyboard_hardware

#endif

/*
 * i8042 keyboard controller for most other Mips machines.
 */
#define MIPS_KEYBOARD_ADDRESS   0xb9005000
#define MIPS_KEYBOARD_DATA      0xb9005003
#define MIPS_KEYBOARD_COMMAND   0xb9005007

#ifndef __LANGUAGE_ASSEMBLY__

#endif

/*
 * PICA timer registers and interrupt no.
 * Note that the hardware timer interrupt is actually on
 * cpu level 6, but to keep compatibility with PC stuff
 * it is remapped to vector 0. See arch/mips/kernel/entry.S.
 */
#define PICA_TIMER_INTERVAL     0xe0000228
#define PICA_TIMER_REGISTER     0xe0000230

/*
 * DRAM configuration register
 */
#ifndef __LANGUAGE_ASSEMBLY__
#ifdef __MIPSEL__
typedef struct {
	unsigned int bank2 : 3;
	unsigned int bank1 : 3;
	unsigned int mem_bus_width : 1;
	unsigned int reserved2 : 1;
	unsigned int page_mode : 1;
	unsigned int reserved1 : 23;
} dram_configuration;
#else /* defined (__MIPSEB__) */
typedef struct {
	unsigned int reserved1 : 23;
	unsigned int page_mode : 1;
	unsigned int reserved2 : 1;
	unsigned int mem_bus_width : 1;
	unsigned int bank1 : 3;
	unsigned int bank2 : 3;
} dram_configuration;
#endif
#endif /* __LANGUAGE_ASSEMBLY__ */

#define PICA_DRAM_CONFIG        0xe00fffe0

/*
 * PICA interrupt control registers
 */
#define PICA_IO_IRQ_SOURCE      0xe0100000
#define PICA_IO_IRQ_ENABLE      0xe0100002

/*
 * Pica interrupt enable bits
 */
#define PIE_PARALLEL            (1<<0)
#define PIE_FLOPPY              (1<<1)
#define PIE_SOUND               (1<<2)
#define PIE_VIDEO               (1<<3)
#define PIE_ETHERNET            (1<<4)
#define PIE_SCSI                (1<<5)
#define PIE_KEYBOARD            (1<<6)
#define PIE_MOUSE               (1<<7)
#define PIE_SERIAL1             (1<<8)
#define PIE_SERIAL2             (1<<9)

#endif /* __ASM_MIPS_PICA_H */

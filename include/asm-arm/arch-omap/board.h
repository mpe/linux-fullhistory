/*
 *  linux/include/asm-arm/arch-omap/board.h
 *
 *  Information structures for board-specific data
 *
 *  Copyright (C) 2004	Nokia Corporation
 *  Written by Juha Yrj�l� <juha.yrjola@nokia.com>
 */

#ifndef _OMAP_BOARD_H
#define _OMAP_BOARD_H

#include <linux/config.h>
#include <linux/types.h>

/* Different peripheral ids */
#define OMAP_TAG_CLOCK		0x4f01
#define OMAP_TAG_MMC		0x4f02
#define OMAP_TAG_UART		0x4f03
#define OMAP_TAG_USB		0x4f04
#define OMAP_TAG_LCD		0x4f05
#define OMAP_TAG_GPIO_SWITCH	0x4f06

#define OMAP_TAG_BOOT_REASON    0x4f80
#define OMAP_TAG_FLASH_PART	0x4f81

struct omap_clock_config {
	/* 0 for 12 MHz, 1 for 13 MHz and 2 for 19.2 MHz */
	u8 system_clock_type;
};

struct omap_mmc_config {
	u8 mmc_blocks;
	s16 mmc1_power_pin, mmc2_power_pin;
	s16 mmc1_switch_pin, mmc2_switch_pin;
};

struct omap_uart_config {
	u8 console_uart;
	u32 console_speed;
};

struct omap_usb_config {
	/* Configure drivers according to the connectors on your board:
	 *  - "A" connector (rectagular)
	 *	... for host/OHCI use, set "register_host".
	 *  - "B" connector (squarish) or "Mini-B"
	 *	... for device/gadget use, set "register_dev".
	 *  - "Mini-AB" connector (very similar to Mini-B)
	 *	... for OTG use as device OR host, initialize "otg"
	 */
	unsigned	register_host:1;
	unsigned	register_dev:1;
	u8		otg;	/* port number, 1-based:  usb1 == 2 */

	u8		hmc_mode;

	/* implicitly true if otg:  host supports remote wakeup? */
	u8		rwc;

	/* signaling pins used to talk to transceiver on usbN:
	 *  0 == usbN unused
	 *  2 == usb0-only, using internal transceiver
	 *  3 == 3 wire bidirectional
	 *  4 == 4 wire bidirectional
	 *  6 == 6 wire unidirectional (or TLL)
	 */
	u8		pins[3];
};

struct omap_lcd_config {
	char panel_name[16];
	char ctrl_name[16];
};

/* Cover:
 *      high -> closed
 *      low  -> open
 * Connection:
 *      high -> connected
 *      low  -> disconnected
 */
#define OMAP_GPIO_SWITCH_TYPE_COVER		0x0000
#define OMAP_GPIO_SWITCH_TYPE_CONNECTION	0x0001
#define OMAP_GPIO_SWITCH_FLAG_INVERTED          0x0001
struct omap_gpio_switch_config {
	char name[12];
	u16 gpio;
	int flags:4;
	int type:4;
	int key_code:24; /* Linux key code */
};

struct omap_flash_part_config {
	char part_table[0];
};

struct omap_boot_reason_config {
	char reason_str[12];
};


struct omap_board_config_entry {
	u16 tag;
	u16 len;
	u8  data[0];
};

struct omap_board_config_kernel {
	u16 tag;
	const void *data;
};

extern const void *__omap_get_config(u16 tag, size_t len, int nr);

#define omap_get_config(tag, type) \
	((const type *) __omap_get_config((tag), sizeof(type), 0))
#define omap_get_nr_config(tag, type, nr) \
	((const type *) __omap_get_config((tag), sizeof(type), (nr)))

extern const void *omap_get_var_config(u16 tag, size_t *len);

extern struct omap_board_config_kernel *omap_board_config;
extern int omap_board_config_size;

#endif

/*
 *  linux/drivers/block/promise.h
 *
 *  Copyright (C) 1995-6  Linus Torvalds & authors
 */

/*
 * Principal author: Peter Denison <peterd@pnd-pc.demon.co.uk>
 */

#ifndef IDE_PROMISE_H
#define IDE_PROMISE_H

#define	PROMISE_EXTENDED_COMMAND	0xF0
#define	PROMISE_READ			0xF2
#define	PROMISE_WRITE			0xF3
/* Extended commands - main command code = 0xf0 */
#define	PROMISE_GET_CONFIG		0x10
#define	PROMISE_IDENTIFY		0x20

struct translation_mode {
	u16	cyl;
	u8	head;
	u8	sect;
};

struct dc_ident {
	u8	type;
	u8	unknown1;
	u8	hw_revision;
	u8	firmware_major;
	u8	firmware_minor;
	u8	bios_address;
	u8	irq;
	u8	unknown2;
	u16	cache_mem;
	u16	unknown3;
	u8	id[2];
	u16	info;
	struct translation_mode current_tm[4];
	u8	pad[SECTOR_WORDS*4 - 32];
};

/*
 * Routines exported to ide.c:
 */
void do_promise_io (ide_drive_t *, struct request *);
int promise_cmd(ide_drive_t *, byte);
void setup_dc4030 (ide_hwif_t *);
int init_dc4030 (void);

#endif IDE_PROMISE_H

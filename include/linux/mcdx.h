/*
 * Definitions for the Mitsumi CDROM interface
 * Copyright (C) 1995 Heiko Schlittermann <heiko@lotte.sax.de>
 * VERSION: 2.2
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Thanks to
 *  The Linux Community at all and ...
 *  Martin Harris (he wrote the first Mitsumi Driver)
 *  Eberhard Moenkeberg (he gave me much support and the initial kick)
 *  Bernd Huebner, Ruediger Helsch (Unifix-Software Gmbh, they
 *      improved the original driver)
 *  Jon Tombs, Bjorn Ekwall (module support)
 *  Daniel v. Mosnenck (he sent me the Technical and Programming Reference)
 *  Gerd Knorr (he lent me his PhotoCD)
 *  Nils Faerber and Roger E. Wolff (extensively tested the LU portion)
 *  Andreas Kies (testing the mysterious hang up's)
 *  ... somebody forgotten?
 *  
 */

#ifndef __MCDX_H
#define __MCDX_H
/*
 * 	PLEASE CONFIGURE THIS ACCORIDNG TO YOURS HARDWARE/JUMPER SETTINGS.
 *
 *      o       MCDX_NDRIVES  :  number of used entries of the following table
 *      o       MCDX_DRIVEMAP :  table of {i/o base, irq} per controller
 *
 *      NOTE: I didn't get a drive at irq 9(2) working.  Not even alone.
 */
 /* #define I_WAS_IN_MCDX_H */
#define MCDX_NDRIVES 1
#define MCDX_DRIVEMAP {	{0x300, 11},	\
			{0x304, 05},  	\
			{0x000, 00},  	\
			{0x000, 00},  	\
			{0x000, 00},  	\
}
	  	
/* 
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!NO USER INTERVENTION NEEDED BELOW
 * If You are shure that all configuration is done, please uncomment the
 * line below. 
 */

#undef MCDX_DEBUG	/* This is *REALLY* only for developement! */

#ifdef MCDX_DEBUG
#define MCDX_TRACE(x) printk x
#define MCDX_TRACE_IOCTL(x) printk x
#else
#define MCDX_TRACE(x)
#define MCDX_TRACE_IOCTL(x)
#endif

/*      The name of the device */
#define MCDX "mcdx"

/*
 *      Per controller 4 bytes i/o are needed. 
 */
#define MCDX_IO_SIZE		4

/* 
 * Masks for the status byte, returned from every command, set if
 * the description is true 
 */
#define MCDX_RBIT_OPEN       0x80	/* door is open */
#define MCDX_RBIT_DISKSET    0x40	/* disk set (recognised) */
#define MCDX_RBIT_CHANGED    0x20	/* disk was changed */
#define MCDX_RBIT_CHECK      0x10	/* disk rotates, servo is on */
#define MCDX_RBIT_AUDIOTR    0x08	/* current track is audio */
#define MCDX_RBIT_RDERR      0x04	/* read error, refer SENSE KEY */
#define MCDX_RBIT_AUDIOBS    0x02	/* currently playing audio */
#define MCDX_RBIT_CMDERR     0x01	/* command, param or format error */

/* 
 * The I/O Register holding the h/w status of the drive,
 * can be read at i/o base + 1 
 */
#define MCDX_RBIT_DOOR       0x10	/* door is open */
#define MCDX_RBIT_STEN       0x04	/* if 0, i/o base contains drive status */
#define MCDX_RBIT_DTEN       0x02	/* if 0, i/o base contains data */

/*
 *    The commands.
 */
#define MCDX_CMD_GET_TOC		0x10
#define MCDX_CMD_GET_MDISK_INFO		0x11
#define MCDX_CMD_GET_SUBQ_CODE		0x20
#define MCDX_CMD_GET_STATUS		0x40
#define MCDX_CMD_SET_DRIVE_MODE		0x50
#define MCDX_CMD_RESET			0x60
#define MCDX_CMD_HOLD			0x70
#define MCDX_CMD_CONFIG			0x90
#define MCDX_CMD_SET_ATTENATOR		0xae
#define MCDX_CMD_PLAY			0xc0
#define MCDX_CMD_PLAY_2X		0xc1
#define MCDX_CMD_GET_DRIVE_MODE		0xc2
#define MCDX_CMD_SET_INTERLEAVE		0xc8
#define MCDX_CMD_GET_FIRMWARE		0xdc
#define MCDX_CMD_SET_DATA_MODE		0xa0
#define MCDX_CMD_STOP			0xf0
#define MCDX_CMD_EJECT			0xf6
#define MCDX_CMD_CLOSE_DOOR		0xf8
#define MCDX_CMD_LOCK_DOOR		0xfe

#define READ_AHEAD			4	/* 8 Sectors (4K) */

#define MCDX_CDBLK	2048	/* 2048 cooked data each blk */

#define MCDX_DATA_TIMEOUT	(HZ/10)		/* 0.1 second */

#ifndef I_WAS_IN_MCDX_H
#warning You have not edited mcdx.h
#warning Perhaps irq and i/o settings are wrong.
#endif

#endif 	/* __MCDX_H */

/*
 * Definitions for the Mitsumi CDROM interface
 * Copyright (C) 1995 Heiko Schlittermann <heiko@lotte.sax.de>
 * VERSION: 1.3
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
 *  Nils Faerber and Roger E. Wolff (extensivly tested the LU portion)
 *  ... somebody forgotten?
 *  
 */

/*
 *	The following lines are for user configuration
 *	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *	{0|1} -- 1 if you want the driver detect your drive, may crash and
 *	needs a long time to seek.  The higher the address the longer the
 *	seek.
 *
 *  WARNING: AUTOPROBE doesn't work.
 */
#define MCDX_AUTOPROBE 0

/*
 *	Drive specific settings according to the jumpers on the controller
 *	board(s).
 *	o	MCDX_NDRIVES  :  number of used entries of the following table
 *	o	MCDX_DRIVEMAP :  table of {i/o base, irq} per controller
 *
 *	NOTE: I didn't get a drive at irq 9(2) working.  Not even alone.
 */
#if MCDX_AUTOPROBE == 0
	#define MCDX_NDRIVES 1
	#define MCDX_DRIVEMAP {		\
			{0x300, 11},	\
			{0x304, 05},  	\
			{0x000, 00},  	\
			{0x000, 00},  	\
			{0x000, 00},  	\
	  	}
#else
	#error Autoprobing is not implemented yet.
#endif

#ifndef MCDX_QUIET
#define MCDX_QUIET   1
#endif

#ifndef MCDX_DEBUG
#define MCDX_DEBUG   0
#endif

/* *** make the following line uncommented, if you're sure,
 * *** all configuration is done */
/* #define I_WAS_HERE */

/*	The name of the device */
#define MCDX "mcdx"	

#if MCDX_QUIET == 1
#define INFO(x)
#else
#define INFO(x) warn x
#endif

#define WARN(x) warn x

#if MCDX_DEBUG == 1
#define TRACE(x) trace x
#define INIT 		0
#define MALLOC 		0
#define IOCTL 		0
#define PLAYTRK     0
#define SUBCHNL     0
#define TOCHDR      0
#define MS          0
#define PLAYMSF     0
#define READTOC     0
#define OPENCLOSE 	0
#define HW		    0
#define TALK		0
#define IRQ 		1
#define TRANSFER 	0
#define REQUEST	 	0
#else
#define TRACE(x)
#endif

/*	The following addresses are taken from the Mitsumi Reference 
 *  and describe the possible i/o range for the controller.
 */
#define MCDX_IO_BEGIN	((char*) 0x300)	/* first base of i/o addr */
#define MCDX_IO_END		((char*) 0x3fc)	/* last base of i/o addr */

/*	Per controller 4 bytes i/o are needed. */
#define MCDX_IO_SIZE		4

/*
 *	The Ports & bits
 */

#define MCDX_RBIT_OPEN       0x80
#define MCDX_RBIT_DISKSET    0x40
#define MCDX_RBIT_CHANGED    0x20
#define MCDX_RBIT_CHECK      0x10
#define MCDX_RBIT_AUDIOTR    0x08
#define MCDX_RBIT_RDERR      0x04
#define MCDX_RBIT_AUDIOBS    0x02
#define MCDX_RBIT_CMDERR     0x01
#define MCDX_RBIT_DOOR       0x10
#define MCDX_RBIT_STEN       0x04
#define MCDX_RBIT_DTEN       0x02


/*
 *	The commands.
 */

#define OPCODE	1		/* offset of opcode */
#define MCDX_CMD_REQUEST_TOC		1, 0x10
#define MCDX_CMD_REQUEST_STATUS		1, 0x40 
#define MCDX_CMD_RESET				1, 0x60
#define MCDX_CMD_REQUEST_DRIVE_MODE	1, 0xc2
#define MCDX_CMD_SET_INTERLEAVE		2, 0xc8, 0
#define MCDX_CMD_DATAMODE_SET		2, 0xa0, 0
	#define MCDX_DATAMODE1		0x01
	#define MCDX_DATAMODE2		0x02
#define MCDX_CMD_LOCK_DOOR		2, 0xfe, 0

#define READ_AHEAD			4	/* 8 Sectors (4K) */

/*	Useful macros */
#define e_door(x)		((x) & MCDX_RBIT_OPEN)
#define e_check(x)		(~(x) & MCDX_RBIT_CHECK)
#define e_notset(x)		(~(x) & MCDX_RBIT_DISKSET)
#define e_changed(x)	((x) & MCDX_RBIT_CHANGED)
#define e_audio(x)		((x) & MCDX_RBIT_AUDIOTR)
#define e_audiobusy(x)	((x) & MCDX_RBIT_AUDIOBS)
#define e_cmderr(x)		((x) & MCDX_RBIT_CMDERR)
#define e_readerr(x)	((x) & MCDX_RBIT_RDERR)

/**	no drive specific */
#define MCDX_CDBLK	2048	/* 2048 cooked data each blk */

#define MCDX_DATA_TIMEOUT	(HZ/10)	/* 0.1 second */

/*
 * Access to the msf array
 */
#define MSF_MIN		0			/* minute */
#define MSF_SEC		1			/* second */
#define MSF_FRM		2			/* frame  */

/*
 * Errors
 */
#define MCDX_E		1			/* unspec error */
#define MCDX_EOM	2			/* end of media */

#ifndef I_WAS_HERE
#warning You have not edited mcdx.h
#warning Perhaps irq and i/o settings are wrong.
#endif

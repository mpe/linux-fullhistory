
/* Defines for the Optics Storage 8000AT CDROM drive. */

#ifndef _LINUX_OPTCD_H

#define _LINUX_OPTCD_H

/* Drive registers */
#define OPTCD_PORTBASE	0x340
/* Read */
#define DATA_PORT	optcd_port	/* Read data/status */
#define STATUS_PORT	optcd_port+1	/* Indicate data/status availability */
/* Write */
#define COMIN_PORT	optcd_port	/* For passing command/parameter */
#define RESET_PORT	optcd_port+1	/* Write anything and wait 0.5 sec */
#define HCON_PORT	optcd_port+2	/* Host Xfer Configuration */


/* Command completion/status read from DATA register */
#define ST_DRVERR		0x80
#define ST_DOOR_OPEN		0x40
#define ST_MIXEDMODE_DISK	0x20
#define ST_MODE_BITS		0x1c
#define ST_M_STOP		0x00
#define ST_M_READ		0x04
#define ST_M_AUDIO		0x04
#define ST_M_PAUSE		0x08
#define ST_M_INITIAL		0x0c
#define ST_M_ERROR		0x10
#define ST_M_OTHERS		0x14
#define	ST_MODE2TRACK		0x02
#define	ST_DSK_CHG		0x01
#define ST_L_LOCK		0x01
#define ST_CMD_OK		0x00
#define ST_OP_OK		0x01
#define ST_PA_OK		0x02
#define ST_OP_ERROR		0x05
#define ST_PA_ERROR		0x06

/* Error codes (appear as command completion code from DATA register) */
/* Player related errors */
#define ERR_ILLCMD	0x11	/* Illegal command to player module */
#define ERR_ILLPARM	0x12	/* Illegal parameter to player module */
#define ERR_SLEDGE	0x13
#define ERR_FOCUS	0x14
#define ERR_MOTOR	0x15
#define ERR_RADIAL	0x16
#define ERR_PLL		0x17	/* PLL lock error */
#define ERR_SUB_TIM	0x18	/* Subcode timeout error */
#define ERR_SUB_NF	0x19	/* Subcode not found error */
#define ERR_TRAY	0x1a
#define ERR_TOC		0x1b	/* Table of Contents read error */
#define ERR_JUMP	0x1c
/* Data errors */
#define ERR_MODE	0x21
#define ERR_FORM	0x22
#define ERR_HEADADDR	0x23	/* Header Address not found */
#define ERR_CRC		0x24
#define ERR_ECC		0x25	/* Uncorrectable ECC error */
#define ERR_CRC_UNC	0x26	/* CRC error and uncorrectable error */
#define ERR_ILLBSYNC	0x27	/* Illegal block sync error */
#define ERR_VDST	0x28	/* VDST not found */
/* Timeout errors */
#define ERR_READ_TIM	0x31	/* Read timeout error */
#define ERR_DEC_STP	0x32	/* Decoder stopped */
#define ERR_DEC_TIM	0x33	/* Decoder interrupt timeout error */
/* Function abort codes */
#define ERR_KEY		0x41	/* Key -Detected abort */
#define ERR_READ_FINISH	0x42	/* Read Finish */
/* Second Byte diagnostic codes */
#define ERR_NOBSYNC	0x01	/* No block sync */
#define ERR_SHORTB	0x02	/* Short block */
#define ERR_LONGB	0x03	/* Long block */
#define ERR_SHORTDSP	0x04	/* Short DSP word */
#define ERR_LONGDSP	0x05	/* Long DSP word */


/* Status availability flags read from STATUS register */
#define FL_EJECT	0x20
#define FL_WAIT		0x10	/* active low */
#define FL_EOP		0x08	/* active low */
#define FL_STEN		0x04	/* Status available when low */
#define FL_DTEN		0x02	/* Data available when low */
#define FL_DRQ		0x01	/* active low */
#define FL_RESET	0xde	/* These bits are high after a reset */
#define FL_STDT		(FL_STEN|FL_DTEN)


/* Transfer mode, written to HCON register */
#define HCON_DTS	0x08
#define HCON_SDRQB	0x04
#define HCON_LOHI	0x02
#define HCON_DMA16	0x01


/* Drive command set, written to COMIN register */
/* Quick response commands */
#define COMDRVST	0x20	/* Drive Status Read */
#define COMERRST	0x21	/* Error Status Read */
#define COMIOCTLISTAT	0x22	/* Status Read; reset disk changed bit */
#define COMINITSINGLE	0x28	/* Initialize Single Speed */
#define COMINITDOUBLE	0x29	/* Initialize Double Speed */
#define COMUNLOCK	0x30	/* Unlock */
#define COMLOCK		0x31	/* Lock */
#define COMLOCKST	0x32	/* Lock/Unlock Status */
#define COMVERSION	0x40	/* Get Firmware Revision */
#define COMVOIDREADMODE	0x50	/* Void Data Read Mode */
/* Read commands */
#define COMFETCH	0x60	/* Prefetch Data */
#define COMREAD		0x61	/* Read */
#define COMREADRAW	0x62	/* Read Raw Data */
#define COMREADALL	0x63	/* Read All 2646 Bytes */
/* Player control commands */
#define COMLEADIN	0x70	/* Seek To Lead-in */
#define COMSEEK		0x71	/* Seek */
#define COMPAUSEON	0x80	/* Pause On */
#define COMPAUSEOFF	0x81	/* Pause Off */
#define COMSTOP		0x82	/* Stop */
#define COMOPEN		0x90	/* Open Tray Door */
#define COMCLOSE	0x91	/* Close Tray Door */
#define COMPLAY		0xa0	/* Audio Play */
#define COMPLAY_TNO	0xa2	/* Audio Play By Track Number */
#define COMSUBQ		0xb0	/* Read Sub-q Code */
#define COMLOCATION	0xb1	/* Read Head Position */
/* Audio control commands */
#define COMCHCTRL	0xc0	/* Audio Channel Control */
/* Miscellaneous (test) commands */
#define COMDRVTEST	0xd0	/* Write Test Bytes */
#define COMTEST		0xd1	/* Diagnostic Test */


#define BUSY_TIMEOUT		10000000	/* for busy wait */
#define SLEEP_TIMEOUT		400		/* for timer wait */
#define READ_TIMEOUT		3000		/* for poll wait */
#define RESET_WAIT		1000

#define SET_TIMER(func, jifs) \
	delay_timer.expires = jifs; \
	delay_timer.function = (void *) func; \
	add_timer(&delay_timer);
#define CLEAR_TIMER		del_timer(&delay_timer)

#define MAX_TRACKS		104

struct msf {
	unsigned char	min;
	unsigned char	sec;
	unsigned char	frame;
};

struct opt_Play_msf {
	struct msf	start;
	struct msf	end;
};

struct opt_DiskInfo {
	unsigned char	first;
	unsigned char	last;
	struct msf	diskLength;
	struct msf	firstTrack;
};

struct opt_Toc {
	unsigned char	ctrl_addr;
	unsigned char	track;
	unsigned char	pointIndex;
	struct msf	trackTime;
	struct msf	diskTime;
};


#define CURRENT_VALID \
	(CURRENT && MAJOR(CURRENT -> dev) == MAJOR_NR \
	 && CURRENT -> cmd == READ && CURRENT -> sector != -1)


#undef	DEBUG_DRIVE_IF		/* Low level drive interface */
#undef	DEBUG_COMMANDS		/* Commands sent to drive */
#undef	DEBUG_VFS		/* VFS interface */
#undef	DEBUG_CONV		/* Address conversions */
#undef	DEBUG_TOC		/* Q-channel and Table of Contents */
#undef	DEBUG_BUFFERS		/* Buffering and block size conversion */
#undef	DEBUG_REQUEST		/* Request mechanism */
#undef	DEBUG_STATE		/* State machine */


/* Low level drive interface */

/* Errors that can occur in the low level interface */
#define ERR_IF_CMD_TIMEOUT	0x100
#define ERR_IF_ERR_TIMEOUT	0x101
#define ERR_IF_RESP_TIMEOUT	0x102
#define ERR_IF_DATA_TIMEOUT	0x103
#define ERR_IF_NOSTAT		0x104
/* Errors in table of contents */
#define ERR_TOC_MISSINGINFO	0x120
#define ERR_TOC_MISSINGENTRY	0x121

/* End .h defines */
#endif _LINUX_OPTCD_H

/*
 * sbpcd.h   Specify interface address and interface type here.
 */

/*
 * the definitions for the first controller can get overridden by
 * the kernel command line ("lilo boot option").
 * Examples:
 *                                 sbpcd=0x230,SoundBlaster
 *                             or
 *                                 sbpcd=0x300,LaserMate
 *                             or
 *                                 sbpcd=0x330,SPEA
 *
 * These strings are case sensitive !!!
 */

/*
 * put your CDROM port base address into CDROM_PORT
 * and specify the type of your interface in SBPRO.
 *
 * SBPRO addresses typically are 0x0230 (=0x220+0x10), 0x0250, ...
 * LASERMATE (CI-101P, WDH-7001C) addresses typically are 0x0300, 0x0310, ...
 * SPEA addresses are 0x320, 0x330, 0x340, 0x350
 * there are some soundcards on the market with 0x0630, 0x0650, ...
 *
 * example: if your SBPRO audio address is 0x220, specify 0x230.
 *
 *
 * set SBPRO to 1 for "true" SoundBlaster card
 * set SBPRO to 0 for "poor" (no sound) interface cards
 *                and for "compatible" soundcards.
 * set SBPRO to 2 for the SPEA Media FX card
 *
 * most "compatible" sound boards like Galaxy need to set SBPRO to 0 !!!
 * if SBPRO gets set wrong, the drive will get found - but any
 * data access will give errors (audio access will work).
 * The OmniCD interface card from CreativeLabs needs SBPRO 1.
 *
 * mail to emoenke@gwdg.de if your "compatible" card needs SBPRO 1
 * (currently I do not know any "compatible" with SBPRO 1)
 * then I can include better information with the next release.
 */
#if !(SBPCD_ISSUE-1) /* first (or if you have only one) interface board: */
#define CDROM_PORT 0x0340
#define SBPRO     0
#endif

/*
 * If you have a "compatible" soundcard of type "SBPRO 0" or "SBPRO 2",
 * enter your sound card's base address here if you want sbpcd to turn
 * the CD sound channels on.
 *
 * Example: #define SOUND_BASE 0x220 enables the sound card's CD channels
 *          #define SOUND_BASE 0     leaves the soundcard untouched
 */
#define SOUND_BASE 0x220

/* ignore the rest if you have only one interface board & driver */

#if !(SBPCD_ISSUE-2) /* second interface board: */
#define CDROM_PORT 0x0320
#define SBPRO     0
#endif
#if !(SBPCD_ISSUE-3) /* third interface board: */
#define CDROM_PORT 0x0630
#define SBPRO     1
#endif
#if !(SBPCD_ISSUE-4) /* fourth interface board: */
#define CDROM_PORT 0x0634
#define SBPRO     0
#endif

/*==========================================================================*/
/*==========================================================================*/
/*
 * nothing to change below here if you are not experimenting
 */
#ifndef _LINUX_SBPCD_H

#define _LINUX_SBPCD_H

/*==========================================================================*/
/*==========================================================================*/
/*
 * DDI interface definitions
 * "invented" by Fred N. van Kempen..
 */
#define DDIOCSDBG	0x9000
#define DPRINTF(x)	sbpcd_dprintf x

/*==========================================================================*/
/*
 * Debug output levels
 */
#define DBG_INF		1	/* necessary information */
#define DBG_BSZ		2	/* BLOCK_SIZE trace */
#define DBG_REA		3	/* "read" status trace */
#define DBG_CHK		4	/* "media check" trace */
#define DBG_TIM		5	/* datarate timer test */
#define DBG_INI		6	/* initialization trace */
#define DBG_TOC		7	/* tell TocEntry values */
#define DBG_IOC         8	/* ioctl trace */
#define DBG_STA		9	/* "ResponseStatus" trace */
#define DBG_ERR		10	/* "xx_ReadError" trace */
#define DBG_CMD		11	/* "cmd_out" trace */
#define DBG_WRN		12	/* give explanation before auto-probing */
#define DBG_MUL         13      /* multi session code test */
#define DBG_ID		14	/* "drive_id !=0" test code */
#define DBG_IOX		15	/* some special information */
#define DBG_DID		16	/* drive ID test */
#define DBG_RES		17	/* drive reset info */
#define DBG_SPI		18	/* SpinUp test */
#define DBG_IOS		19	/* ioctl trace: "subchannel" */
#define DBG_IO2		20	/* ioctl trace: general */
#define DBG_UPC		21	/* show UPC information */
#define DBG_XA 		22	/* XA mode debugging */
#define DBG_LCK		23	/* door (un)lock info */
#define DBG_SQ 		24	/* dump SubQ frame */
#define DBG_AUD		25      /* "read audio" debugging */
#define DBG_SEQ		26      /* Sequoia interface configuration trace */
#define DBG_LCS		27      /* Longshine LCS-7260 debugging trace */
#define DBG_TEA		28      /* TEAC CD-55A debugging trace */
#define DBG_CD2		29      /* MKE CD200 debugging trace */
#define DBG_000		30      /* unnecessary information */

/*==========================================================================*/
/*==========================================================================*/

/*
 * bits of flags_cmd_out:
 */
#define f_respo3		0x100
#define f_putcmd		0x80
#define f_respo2		0x40
#define f_lopsta		0x20
#define f_getsta		0x10
#define f_ResponseStatus	0x08
#define f_obey_p_check		0x04
#define f_bit1			0x02
#define f_wait_if_busy		0x01

/*
 * diskstate_flags:
 */
#define x80_bit			0x80
#define upc_bit			0x40
#define volume_bit		0x20
#define toc_bit			0x10
#define multisession_bit	0x08
#define cd_size_bit		0x04
#define subq_bit		0x02
#define frame_size_bit		0x01

/*
 * disk states (bits of diskstate_flags):
 */
#define upc_valid (DriveStruct[d].diskstate_flags&upc_bit)
#define volume_valid (DriveStruct[d].diskstate_flags&volume_bit)
#define toc_valid (DriveStruct[d].diskstate_flags&toc_bit)
#define cd_size_valid (DriveStruct[d].diskstate_flags&cd_size_bit)
#define subq_valid (DriveStruct[d].diskstate_flags&subq_bit)
#define frame_size_valid (DriveStruct[d].diskstate_flags&frame_size_bit)

/*
 * bits of the status_byte (result of xx_ReadStatus):
 */
#define p_door_closed 0x80
#define p_caddy_in 0x40
#define p_spinning 0x20
#define p_check 0x10
#define p_busy_new 0x08
#define p_door_locked 0x04
#define p_bit_1 0x02 /* hopefully unused now */
#define p_lcs_door_locked 0x02 /* new use of old bit */
#define p_disk_ok 0x01
#define p_lcs_door_closed 0x01 /* new use of old bit */
/*
 * "old" drives status result bits:
 */
#define p_caddin_old 0x40
#define p_success_old 0x08
#define p_busy_old 0x04

/*
 * "generation specific" defs of the status_byte:
 */
#define p0_door_closed	0x80
#define p0_caddy_in	0x40
#define p0_spinning	0x20
#define p0_check	0x10
#define p0_success	0x08 /* unused */
#define p0_busy		0x04
#define p0_bit_1	0x02 /* unused */
#define p0_disk_ok	0x01

#define p1_door_closed	0x80
#define p1_disk_in	0x40
#define p1_spinning	0x20
#define p1_check	0x10
#define p1_busy		0x08
#define p1_door_locked	0x04
#define p1_bit_1	0x02 /* unused */
#define p1_disk_ok	0x01

#define p2_disk_ok	0x80
#define p2_door_locked	0x40
#define p2_spinning	0x20
#define p2_busy2	0x10
#define p2_busy1	0x08
#define p2_door_closed	0x04
#define p2_disk_in	0x02
#define p2_check	0x01

/*
 * used drive states:
 */
#define st_door_closed (DriveStruct[d].status_byte&p_door_closed)
#define st_caddy_in (DriveStruct[d].status_byte&p_caddy_in)
#define st_spinning (DriveStruct[d].status_byte&p_spinning)
#define st_check (DriveStruct[d].status_byte&p_check)
#define st_busy (DriveStruct[d].status_byte&p_busy_new)
#define st_door_locked (DriveStruct[d].status_byte&p_door_locked)
#define st_diskok (DriveStruct[d].status_byte&p_disk_ok)

/*
 * bits of the CDi_status register:
 */
#define s_not_result_ready 0x04  /* 0: "result ready" */
#define s_not_data_ready 0x02    /* 0: "data ready"   */
#define s_attention 0x01         /* 1: "attention required" */
/*
 * usable as:
 */
#define DRV_ATTN               ((inb(CDi_status)&s_attention)!=0)
#define DATA_READY             ((inb(CDi_status)&s_not_data_ready)==0)
#define RESULT_READY           ((inb(CDi_status)&s_not_result_ready)==0)

/*
 * drive types (firmware versions):
 */
#define drv_fam0 0x08   /* CR-52x family */
#define drv_199 (drv_fam0+0x01)    /* <200 */
#define drv_200 (drv_fam0+0x02)    /* <201 */
#define drv_201 (drv_fam0+0x03)    /* <210 */
#define drv_210 (drv_fam0+0x04)    /* <211 */
#define drv_211 (drv_fam0+0x05)    /* <300 */
#define drv_300 (drv_fam0+0x06)    /* >=300 */

#define drv_famL 0x10    /* Longshine family */
#define drv_260 (drv_famL+0x01)    /* LCS-7260 */

#define drv_fam1 0x20    /* CR-56x family */
#define drv_099 (drv_fam1+0x01)    /* <100 */
#define drv_100 (drv_fam1+0x02)    /* >=100 */

#define drv_famT 0x40    /* TEAC CD-55A */
#define drv_fam2 0x80    /* CD200 family */

#define fam0_drive (DriveStruct[d].drv_type&drv_fam0)
#define famL_drive (DriveStruct[d].drv_type&drv_famL)
#define fam1_drive (DriveStruct[d].drv_type&drv_fam1)
#define famT_drive (DriveStruct[d].drv_type&drv_famT)
#define fam2_drive (DriveStruct[d].drv_type&drv_fam2)
#define fam0L_drive (DriveStruct[d].drv_type&(drv_fam0|drv_famL))
#define fam1L_drive (DriveStruct[d].drv_type&(drv_fam1|drv_famL))
#define fam01_drive (DriveStruct[d].drv_type&(drv_fam0|drv_fam1))

/*
 * audio states:
 */
#define audio_playing 2
#define audio_pausing 1

/*
 * drv_pattern, drv_options:
 */
#define speed_auto	0x80
#define speed_300	0x40
#define speed_150	0x20
#define audio_mono	0x04

/*
 * values of cmd_type (0 else):
 */
#define READ_M1  0x01 /* "data mode 1": 2048 bytes per frame */
#define READ_M2  0x02 /* "data mode 2": 12+2048+280 bytes per frame */
#define READ_SC  0x04 /* "subchannel info": 96 bytes per frame */
#define READ_AU  0x08 /* "audio frame": 2352 bytes per frame */

/*
 * sense_byte:
 *
 *          values: 00
 *                  01
 *                  81
 *                  82 "raw audio" mode
 *                  xx from infobuf[0] after 85 00 00 00 00 00 00
 */

/* audio status (bin) */
#define aud_00 0x00 /* Audio status byte not supported or not valid */
#define audx11 0x0b /* Audio play operation in progress             */
#define audx12 0x0c /* Audio play operation paused                  */
#define audx13 0x0d /* Audio play operation successfully completed  */
#define audx14 0x0e /* Audio play operation stopped due to error    */
#define audx15 0x0f /* No current audio status to return            */

/* audio status (bcd) */
#define aud_11 0x11 /* Audio play operation in progress             */
#define aud_12 0x12 /* Audio play operation paused                  */
#define aud_13 0x13 /* Audio play operation successfully completed  */
#define aud_14 0x14 /* Audio play operation stopped due to error    */
#define aud_15 0x15 /* No current audio status to return            */


/*
 * highest allowed drive number (MINOR+1)
 */
#define NR_SBPCD 4

/*
 * we try to never disable interrupts - seems to work
 */
#define SBPCD_DIS_IRQ 0

/*
 * "write byte to port"
 */
#define OUT(x,y) outb(y,x)

/*
 * use "REP INSB" for strobing the data in:
 */
#define READ_DATA(port, buf, nr) insb(port, buf, nr)

/*==========================================================================*/

#define MIXER_CD_Volume	0x28 /* internal SB Pro register address */

/*==========================================================================*/
/*
 * Creative Labs Programmers did this:
 */
#define MAX_TRACKS	120 /* why more than 99? */

/*==========================================================================*/
/*
 * To make conversions easier (machine dependent!)
 */
typedef union _msf
{
  u_int n;
  u_char c[4];
}
MSF;

typedef union _blk
{
  u_int n;
  u_char c[4];
}
BLK;

/*==========================================================================*/

/*============================================================================
==============================================================================

COMMAND SET of "old" drives like CR-521, CR-522
               (the CR-562 family is different):

No.	Command			       Code
--------------------------------------------

Drive Commands:
 1	Seek				01	
 2	Read Data			02
 3	Read XA-Data			03
 4	Read Header			04
 5	Spin Up				05
 6	Spin Down			06
 7	Diagnostic			07
 8	Read UPC			08
 9	Read ISRC			09
10	Play Audio			0A
11	Play Audio MSF			0B
12	Play Audio Track/Index		0C

Status Commands:
13	Read Status			81	
14	Read Error			82
15	Read Drive Version		83
16	Mode Select			84
17	Mode Sense			85
18	Set XA Parameter		86
19	Read XA Parameter		87
20	Read Capacity			88
21	Read SUB_Q			89
22	Read Disc Code			8A
23	Read Disc Information		8B
24	Read TOC			8C
25	Pause/Resume			8D
26	Read Packet			8E
27	Read Path Check			00
 
 
all numbers (lba, msf-bin, msf-bcd, counts) to transfer high byte first

mnemo     7-byte command        #bytes response (r0...rn)
________ ____________________  ____ 

Read Status:
status:  81.                    (1)  one-byte command, gives the main
                                                          status byte
Read Error:
check1:  82 00 00 00 00 00 00.  (6)  r1: audio status

Read Packet:
check2:  8e xx 00 00 00 00 00. (xx)  gets xx bytes response, relating
                                        to commands 01 04 05 07 08 09

Play Audio:
play:    0a ll-bb-aa nn-nn-nn.  (0)  play audio, ll-bb-aa: starting block (lba),
                                                 nn-nn-nn: #blocks
Play Audio MSF:
         0b mm-ss-ff mm-ss-ff   (0)  play audio from/to

Play Audio Track/Index:
         0c ...

Pause/Resume:
pause:   8d pr 00 00 00 00 00.  (0)  pause (pr=00) 
                                     resume (pr=80) audio playing

Mode Select:
         84 00 nn-nn ??-?? 00   (0)  nn-nn: 2048 or 2340
                                     possibly defines transfer size

set_vol: 84 83 00 00 sw le 00.  (0)  sw(itch): lrxxxxxx (off=1)
                                     le(vel): min=0, max=FF, else half
				     (firmware 2.11)

Mode Sense:
get_vol: 85 03 00 00 00 00 00.  (2)  tell current audio volume setting

Read Disc Information:
tocdesc: 8b 00 00 00 00 00 00.  (6)  read the toc descriptor ("msf-bin"-format)

Read TOC:
tocent:  8c fl nn 00 00 00 00.  (8)  read toc entry #nn
                                       (fl=0:"lba"-, =2:"msf-bin"-format)

Read Capacity:
capacit: 88 00 00 00 00 00 00.  (5)  "read CD-ROM capacity"


Read Path Check:
ping:    00 00 00 00 00 00 00.  (2)  r0=AA, r1=55
                                     ("ping" if the drive is connected)

Read Drive Version:
ident:   83 00 00 00 00 00 00. (12)  gives "MATSHITAn.nn" 
                                     (n.nn = 2.01, 2.11., 3.00, ...)

Seek:
seek:    01 00 ll-bb-aa 00 00.  (0)  
seek:    01 02 mm-ss-ff 00 00.  (0)  

Read Data:
read:    02 xx-xx-xx nn-nn fl. (??)  read nn-nn blocks of 2048 bytes,
                                     starting at block xx-xx-xx  
                                     fl=0: "lba"-, =2:"msf-bcd"-coded xx-xx-xx

Read XA-Data:
read:    03 xx-xx-xx nn-nn fl. (??)  read nn-nn blocks of 2340 bytes, 
                                     starting at block xx-xx-xx
                                     fl=0: "lba"-, =2:"msf-bcd"-coded xx-xx-xx

Read SUB_Q:
         89 fl 00 00 00 00 00. (13)  r0: audio status, r4-r7: lba/msf, 
                                       fl=0: "lba", fl=2: "msf"

Read Disc Code:
         8a 00 00 00 00 00 00. (14)  possibly extended "check condition"-info

Read Header:
         04 00 ll-bb-aa 00 00.  (0)   4 bytes response with "check2"
         04 02 mm-ss-ff 00 00.  (0)   4 bytes response with "check2"

Spin Up:
         05 00 ll-bb-aa 00 00.  (0)  possibly implies a "seek"

Spin Down:
         06 ...

Diagnostic:
         07 00 ll-bb-aa 00 00.  (2)   2 bytes response with "check2"
         07 02 mm-ss-ff 00 00.  (2)   2 bytes response with "check2"

Read UPC:
         08 00 ll-bb-aa 00 00. (16)  
         08 02 mm-ss-ff 00 00. (16)  

Read ISRC:
         09 00 ll-bb-aa 00 00. (15)  15 bytes response with "check2"
         09 02 mm-ss-ff 00 00. (15)  15 bytes response with "check2"

Set XA Parameter:
         86 ...

Read XA Parameter:
         87 ...

==============================================================================
============================================================================*/

/*
 * commands
 *
 * CR-52x:      CMD0_
 * CR-56x:      CMD1_
 * CD200:       CMD2_
 * LCS-7260:    CMDL_
 * TEAC CD-55A: CMDT_
 */
#define CMD1_RESET	0x0a
#define CMD2_RESET	0x01
#define CMDT_RESET	0xc0
#define CMD1_LOCK_CTL	0x0c
#define CMD2_LOCK_CTL	0x1e
#define CMDL_LOCK_CTL	0x0e
#define CMDT_LOCK_CTL	0x1e
#define CMD1_TRAY_CTL	0x07
#define CMD2_TRAY_CTL	0x1b
#define CMDL_TRAY_CTL	0x0d
#define CMDT_TRAY_CTL	0x1b
#define CMD1_MULTISESS	0x8d
#define CMDL_MULTISESS	0x8c
#define CMD1_SUBCHANINF	0x11
#define CMD2_SUBCHANINF	0x
#define CMD2_x02	0x02
#define CMD1_x08	0x08
#define CMD2_x08	0x08
#define CMDT_x08	0x08
#define CMD2_SETSPEED	0xda

#define CMD0_PATH_CHECK	0x00
#define CMD1_PATH_CHECK	0x00
#define CMD2_PATH_CHECK	0x
#define CMDL_PATH_CHECK	0x00
#define CMD0_SEEK	0x01
#define CMD1_SEEK	0x01
#define CMD2_SEEK	0x2b
#define CMDL_SEEK	0x01
#define CMDT_SEEK	0x2b
#define CMD0_READ	0x02
#define CMD1_READ	0x10
#define CMD2_READ	0x28
#define CMDL_READ	0x02
#define CMDT_READ	0x28
#define CMD0_READ_XA	0x03
#define CMD2_READ_XA	0xd4
#define CMDL_READ_XA	0x03 /* really ?? */
#define CMD0_READ_HEAD	0x04
#define CMD0_SPINUP	0x05
#define CMD1_SPINUP	0x02
#define CMD2_SPINUP	CMD2_TRAY_CTL
#define CMDL_SPINUP	0x05
#define CMD0_SPINDOWN	0x06 /* really??? */
#define CMD1_SPINDOWN	0x06
#define CMD2_SPINDOWN	CMD2_TRAY_CTL
#define CMDL_SPINDOWN	0x0d
#define CMD0_DIAG	0x07
#define CMD0_READ_UPC	0x08
#define CMD1_READ_UPC	0x88
#define CMD2_READ_UPC	0x
#define CMDL_READ_UPC	0x08
#define CMD0_READ_ISRC	0x09
#define CMD0_PLAY	0x0a
#define CMD1_PLAY	0x
#define CMD2_PLAY	0x
#define CMDL_PLAY	0x0a
#define CMD0_PLAY_MSF	0x0b
#define CMD1_PLAY_MSF	0x0e
#define CMD2_PLAY_MSF	0x47
#define CMDL_PLAY_MSF	0x
#define CMDT_PLAY_MSF	0x47
#define CMD0_PLAY_TI	0x0c
#define CMD0_STATUS	0x81
#define CMD1_STATUS	0x05
#define CMD2_STATUS	0x00
#define CMDL_STATUS	0x81
#define CMDT_STATUS	0x00
#define CMD0_READ_ERR	0x82
#define CMD1_READ_ERR	0x82
#define CMD2_READ_ERR	0x03
#define CMDL_READ_ERR	0x82
#define CMDT_READ_ERR	0x03 /* get audio status */
#define CMD0_READ_VER	0x83
#define CMD1_READ_VER	0x83
#define CMD2_READ_VER	0x12
#define CMDT_READ_VER	0x12 /* ??? (unused) */
#define CMDL_READ_VER	0x83
#define CMD0_SETMODE	0x84
#define CMD1_SETMODE	0x09
#define CMD2_SETMODE	0x55
#define CMDL_SETMODE	0x84
#define CMDT_SETMODE	0x55
#define CMD0_GETMODE	0x85
#define CMD1_GETMODE	0x84
#define CMD2_GETMODE	0x5a
#define CMDL_GETMODE	0x85
#define CMDT_GETMODE	0x5a
#define CMD0_SET_XA	0x86
#define CMD0_GET_XA	0x87
#define CMD0_CAPACITY	0x88
#define CMD1_CAPACITY	0x85
#define CMD2_CAPACITY	0x25
#define CMDL_CAPACITY	0x88
#define CMD0_READSUBQ	0x89
#define CMD1_READSUBQ	0x87
#define CMD2_READSUBQ	0x42
#define CMDL_READSUBQ	0x89
#define CMDT_READSUBQ	0x42
#define CMD0_DISKCODE	0x8a
#define CMD0_DISKINFO	0x8b
#define CMD1_DISKINFO	0x8b
#define CMD2_DISKINFO	0x43
#define CMDL_DISKINFO	0x8b
#define CMDT_DISKINFO	0x43
#define CMD0_READTOC	0x8c
#define CMD1_READTOC	0x8c
#define CMD2_READTOC	0x
#define CMDL_READTOC	0x8c
#define CMD0_PAU_RES	0x8d
#define CMD1_PAU_RES	0x0d
#define CMD2_PAU_RES	0x4b
#define CMDL_PAU_RES	0x8d
#define CMDT_PAU_RES	0x4b
#define CMD0_PACKET	0x8e
#define CMD1_PACKET	0x8e
#define CMD2_PACKET	0x
#define CMDL_PACKET	0x8e

/*==========================================================================*/
/*==========================================================================*/
#endif _LINUX_SBPCD_H

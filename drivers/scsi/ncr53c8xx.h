/******************************************************************************
**  Device driver for the PCI-SCSI NCR538XX controller family.
**
**  Copyright (C) 1994  Wolfgang Stanglmeier
**
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
**
**  This driver has been ported to Linux from the FreeBSD NCR53C8XX driver
**  and is currently maintained by
**
**          Gerard Roudier              <groudier@club-internet.fr>
**
**  Being given that this driver originates from the FreeBSD version, and
**  in order to keep synergy on both, any suggested enhancements and corrections
**  received on Linux are automatically a potential candidate for the FreeBSD 
**  version.
**
**  The original driver has been written for 386bsd and FreeBSD by
**          Wolfgang Stanglmeier        <wolf@cologne.de>
**          Stefan Esser                <se@mi.Uni-Koeln.de>
**
**  And has been ported to NetBSD by
**          Charles M. Hannum           <mycroft@gnu.ai.mit.edu>
**
*******************************************************************************
*/

#ifndef NCR53C8XX_H
#define NCR53C8XX_H

/*
**	Name and revision of the driver
*/
#define SCSI_NCR_DRIVER_NAME		"ncr53c8xx - revision 1.14c"
 
/*
**	If SCSI_NCR_SPECIAL_FEATURES is defined,
**	the driver enables or not the following features according to chip id 
**	revision id:
**	DMODE   0xce
**		0x02	burst op-code fetch
**		0x04	enable read multiple
**		0x08	enable read line
**		0xc0	burst length 16/8/2
**	DCNTL   0xa0
**		0x20	enable pre-fetch
**		0x80	enable cache line size
**	CTEST3  0x01
**		0x01	set write and invalidate
**	CTEST4  0x80
**		0x80	burst disabled
**
**	If SCSI_NCR_TRUST_BIOS_SETTING is defined, the driver will use the 
**	initial value of corresponding bit fields, assuming they have been 
**	set by the SDMS BIOS.
**	When Linux is booted from another O/S, these assertion is false and 
**	the driver will not be able to guess it. 
*/

#if 0
#define SCSI_NCR_TRUST_BIOS_SETTING
#endif

#if 0
#define SCSI_NCR_SPECIAL_FEATURES
#endif

/*********** LINUX SPECIFIC SECTION ******************/

/*
**	Check supported Linux versions
*/

#if !defined(LINUX_VERSION_CODE)
#include <linux/version.h>
#endif
#include <linux/config.h>

/*
**	During make dep of linux-1.2.13, LINUX_VERSION_CODE is undefined
**	Under linux-1.3.X, all seems to be OK.
**	So, we have only to define it under 1.2.13
*/

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

#if !defined(LINUX_VERSION_CODE)
#define LINUX_VERSION_CODE LinuxVersionCode(1,2,13)
#endif

#if !defined(VERSION)
#define VERSION		((LINUX_VERSION_CODE >> 16) & 0xff)
#define PATCHLEVEL	((LINUX_VERSION_CODE >> 8)  & 0xff)
#define SUBLEVEL	((LINUX_VERSION_CODE >> 0)  & 0xff)
#endif

#if	VERSION == 0 || VERSION > 3
#	error Only Linux version 1 and probable 2 or 3 supported.
#endif

#if	VERSION == 1 && PATCHLEVEL == 2
#	if	SUBLEVEL != 13
#		error Only sublevel 13 of Linux 1.2 is supported.
#	endif
#endif

#if	VERSION == 1 && PATCHLEVEL == 3
#	if	SUBLEVEL < 45
#		error Only sublevels >=45 of Linux 1.3 are supported.
#	endif
#endif

/*
**	Normal IO or memory mapped IO.
**
**	Memory mapped IO only works with linux-1.3.X
**	If your motherboard does not work with memory mapped IO,
**	define SCSI_NCR_IOMAPPED for PATCHLEVEL 3 too.
*/

#if	LINUX_VERSION_CODE < LinuxVersionCode(1,3,0)
#	define	SCSI_NCR_IOMAPPED
#endif

#if	LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
#	define	SCSI_NCR_PROC_INFO_SUPPORT
#endif

#if	LINUX_VERSION_CODE >= LinuxVersionCode(1,3,72)
#	define SCSI_NCR_SHARE_IRQ
#endif

/*
**	Avoid to change these constants, unless you know what you are doing.
*/

#ifdef	CONFIG_SCSI_NCR53C8XX_MAX_TAGS
#if	CONFIG_SCSI_NCR53C8XX_MAX_TAGS < 2
#define SCSI_NCR_MAX_TAGS	(2)
#elif	CONFIG_SCSI_NCR53C8XX_MAX_TAGS > 12
#define SCSI_NCR_MAX_TAGS	(12)
#else
#define	SCSI_NCR_MAX_TAGS	CONFIG_SCSI_NCR53C8XX_MAX_TAGS
#endif
#else
#define SCSI_NCR_MAX_TAGS	(4)
#endif

#define SCSI_NCR_ALWAYS_SIMPLE_TAG

#ifdef CONFIG_SCSI_NCR53C8XX_TAGGED_QUEUE
#define SCSI_NCR_DEFAULT_TAGS	SCSI_NCR_MAX_TAGS
#else
#define SCSI_NCR_DEFAULT_TAGS	(0)
#endif

#ifdef CONFIG_SCSI_NCR53C8XX_IOMAPPED
#define	SCSI_NCR_IOMAPPED
#endif

#ifdef	CONFIG_SCSI_NCR53C8XX_SYNC
#if	CONFIG_SCSI_NCR53C8XX_SYNC == 0
#define	SCSI_NCR_DEFAULT_SYNC	(0)
#elif	CONFIG_SCSI_NCR53C8XX_SYNC < 5
#define	SCSI_NCR_DEFAULT_SYNC	(5000)
#elif	CONFIG_SCSI_NCR53C8XX_SYNC > 10
#define	SCSI_NCR_DEFAULT_SYNC	(10000)
#else
#define	SCSI_NCR_DEFAULT_SYNC	(CONFIG_SCSI_NCR53C8XX_SYNC * 1000)
#endif
#else
#define	SCSI_NCR_DEFAULT_SYNC	(10000)
#endif

#ifdef	CONFIG_SCSI_FORCE_ASYNCHRONOUS
#undef	SCSI_NCR_DEFAULT_SYNC
#define SCSI_NCR_DEFAULT_SYNC	(0)
#endif

#ifdef CONFIG_SCSI_NCR53C8XX_NO_DISCONNECT
#define SCSI_NCR_NO_DISCONNECT
#endif

#ifdef CONFIG_SCSI_NCR53C8XX_FORCE_SYNC_NEGO
#define SCSI_NCR_FORCE_SYNC_NEGO
#endif

#ifdef CONFIG_SCSI_NCR53C8XX_DISABLE_MPARITY_CHECK
#define SCSI_NCR_DISABLE_MPARITY_CHECK
#endif

#ifdef CONFIG_SCSI_NCR53C8XX_DISABLE_PARITY_CHECK
#define SCSI_NCR_DISABLE_PARITY_CHECK
#endif

#if 0
#define SCSI_NCR_SEGMENT_SIZE	(512)
#endif

#define SCSI_NCR_MAX_SCATTER	(128)
#define SCSI_NCR_MAX_TARGET	(16)
#define SCSI_NCR_MAX_HOST	(2)
#define SCSI_NCR_SETTLE_TIME	(2)
#define SCSI_NCR_TIMEOUT_ALERT	(3*HZ)

#define SCSI_NCR_CAN_QUEUE	(7*SCSI_NCR_MAX_TAGS)
#define SCSI_NCR_CMD_PER_LUN	(SCSI_NCR_MAX_TAGS)
#define SCSI_NCR_SG_TABLESIZE	(SCSI_NCR_MAX_SCATTER-1)

#if 1 /* defined CONFIG_SCSI_MULTI_LUN */
#define SCSI_NCR_MAX_LUN	(8)
#else
#define SCSI_NCR_MAX_LUN	(1)
#endif

#define SCSI_NCR_TIMER_INTERVAL	((HZ+5-1)/5)

/*
**	Define Scsi_Host_Template parameters
**
**	Used by hosts.c and ncr53c8xx.c with module configuration.
*/

#if defined(HOSTS_C) || defined(MODULE)

#if	LINUX_VERSION_CODE >= LinuxVersionCode(1,3,98)
#include <scsi/scsicam.h>
#else
#include <linux/scsicam.h>
#endif

int ncr53c8xx_abort(Scsi_Cmnd *);
int ncr53c8xx_detect(Scsi_Host_Template *tpnt);
int ncr53c8xx_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));

#if	LINUX_VERSION_CODE >= LinuxVersionCode(1,3,98)
int ncr53c8xx_reset(Scsi_Cmnd *, unsigned int);
#else
int ncr53c8xx_reset(Scsi_Cmnd *);
#endif

#ifdef MODULE
int ncr53c8xx_release(struct Scsi_Host *);
#else
#define ncr53c8xx_release NULL
#endif

#if	LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)

#define NCR53C8XX {NULL,NULL,NULL,NULL,SCSI_NCR_DRIVER_NAME, ncr53c8xx_detect,\
    	ncr53c8xx_release, /* info */ NULL, /* command, deprecated */ NULL, 		\
	ncr53c8xx_queue_command, ncr53c8xx_abort, ncr53c8xx_reset,	\
        NULL /* slave attach */, scsicam_bios_param, /* can queue */ SCSI_NCR_CAN_QUEUE,\
	/* id */ 7, SCSI_NCR_SG_TABLESIZE /* SG */, /* cmd per lun */ SCSI_NCR_CMD_PER_LUN, 		\
        /* present */ 0, /* unchecked isa dma */ 0, DISABLE_CLUSTERING} 


#else


#define NCR53C8XX {NULL, NULL, SCSI_NCR_DRIVER_NAME, ncr53c8xx_detect,\
    	ncr53c8xx_release, /* info */ NULL, /* command, deprecated */ NULL, 		\
	ncr53c8xx_queue_command, ncr53c8xx_abort, ncr53c8xx_reset,	\
        NULL /* slave attach */, scsicam_bios_param, /* can queue */ SCSI_NCR_CAN_QUEUE,\
	/* id */ 7, SCSI_NCR_SG_TABLESIZE /* SG */, /* cmd per lun */ SCSI_NCR_CMD_PER_LUN, 		\
        /* present */ 0, /* unchecked isa dma */ 0, DISABLE_CLUSTERING} 

#endif /* LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0) */

#endif /* defined(HOSTS_C) || defined(MODULE) */ 


#ifndef HOSTS_C

/*
**	Define the table of target capabilities by host and target
**
**	If you have problems with a scsi device, note the host unit and the
**	corresponding target number.
**
**	Edit the corresponding entry of the table below and try successively:
**		NQ7_Questionnable
**		NQ7_IdeLike
**
**	This bitmap is anded with the byte 7 of inquiry data on completion of
**	INQUIRY command.
**	The driver never see the zeroed bits and will ignore the corresponding
**	capabilities of the target.
*/

#define INQ7_SftRe	1
#define INQ7_CmdQueue	(1<<1)		/* Tagged Command */
#define INQ7_Reserved	(1<<2)
#define INQ7_Linked	(1<<3)
#define INQ7_Sync	(1<<4)		/* Synchronous Negotiation */
#define INQ7_WBus16	(1<<5)
#define INQ7_WBus32	(1<<6)
#define INQ7_RelAdr	(1<<7)

#define INQ7_IdeLike		0
#define INQ7_Scsi1Like		INQ7_IdeLike
#define INQ7_Perfect		0xff
#define INQ7_Questionnable	~(INQ7_CmdQueue|INQ7_Sync)
#define INQ7_VeryQuestionnable	\
			~(INQ7_CmdQueue|INQ7_Sync|INQ7_WBus16|INQ7_WBus32)

#define INQ7_Default		INQ7_Perfect

#define NCR53C8XX_TARGET_CAPABILITIES					\
/* Host 0 */								\
{									\
	{								\
	/* Target  0 */		INQ7_Default,				\
	/* Target  1 */		INQ7_Default,				\
	/* Target  2 */		INQ7_Default,				\
	/* Target  3 */		INQ7_Default,				\
	/* Target  4 */		INQ7_Default,				\
	/* Target  5 */		INQ7_Default,				\
	/* Target  6 */		INQ7_Default,				\
	/* Target  7 */		INQ7_Default,				\
	/* Target  8 */		INQ7_Default,				\
	/* Target  9 */		INQ7_Default,				\
	/* Target 10 */		INQ7_Default,				\
	/* Target 11 */		INQ7_Default,				\
	/* Target 12 */		INQ7_Default,				\
	/* Target 13 */		INQ7_Default,				\
	/* Target 14 */		INQ7_Default,				\
	/* Target 15 */		INQ7_Default,				\
	}								\
},									\
/* Host 1 */								\
{									\
	{								\
	/* Target  0 */		INQ7_Default,				\
	/* Target  1 */		INQ7_Default,				\
	/* Target  2 */		INQ7_Default,				\
	/* Target  3 */		INQ7_Default,				\
	/* Target  4 */		INQ7_Default,				\
	/* Target  5 */		INQ7_Default,				\
	/* Target  6 */		INQ7_Default,				\
	/* Target  7 */		INQ7_Default,				\
	/* Target  8 */		INQ7_Default,				\
	/* Target  9 */		INQ7_Default,				\
	/* Target 10 */		INQ7_Default,				\
	/* Target 11 */		INQ7_Default,				\
	/* Target 12 */		INQ7_Default,				\
	/* Target 13 */		INQ7_Default,				\
	/* Target 14 */		INQ7_Default,				\
	/* Target 15 */		INQ7_Default,				\
	}								\
}

/*
**	Replace the proc_dir_entry of the standard ncr driver.
*/

#if	LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
#if	defined(CONFIG_SCSI_NCR53C7xx) || !defined(CONFIG_SCSI_NCR53C8XX)
#define PROC_SCSI_NCR53C8XX	PROC_SCSI_NCR53C7xx
#endif
#endif

/*
**	NCR53C8XX Device Ids
*/

#ifndef PCI_DEVICE_ID_NCR_53C810
#define PCI_DEVICE_ID_NCR_53C810 1
#endif

#ifndef PCI_DEVICE_ID_NCR_53C810AP
#define PCI_DEVICE_ID_NCR_53C810AP 5
#endif

#ifndef PCI_DEVICE_ID_NCR_53C815
#define PCI_DEVICE_ID_NCR_53C815 4
#endif

#ifndef PCI_DEVICE_ID_NCR_53C820
#define PCI_DEVICE_ID_NCR_53C820 2
#endif

#ifndef PCI_DEVICE_ID_NCR_53C825
#define PCI_DEVICE_ID_NCR_53C825 3
#endif

#ifndef PCI_DEVICE_ID_NCR_53C860
#define PCI_DEVICE_ID_NCR_53C860 6
#endif

#ifndef PCI_DEVICE_ID_NCR_53C875
#define PCI_DEVICE_ID_NCR_53C875 0xf
#endif

#ifndef PCI_DEVICE_ID_NCR_53C885
#define PCI_DEVICE_ID_NCR_53C885 0xd
#endif

#ifndef PCI_DEVICE_ID_NCR_53C895
#define PCI_DEVICE_ID_NCR_53C895 0xc
#endif

#ifndef PCI_DEVICE_ID_NCR_53C896
#define PCI_DEVICE_ID_NCR_53C896 0xb
#endif

/**************** ORIGINAL CONTENT of ncrreg.h from FreeBSD ******************/

/*-----------------------------------------------------------------
**
**	The ncr 53c810 register structure.
**
**-----------------------------------------------------------------
*/

struct ncr_reg {
/*00*/  u_char    nc_scntl0;    /* full arb., ena parity, par->ATN  */

/*01*/  u_char    nc_scntl1;    /* no reset                         */
        #define   ISCON   0x10  /* connected to scsi		    */
        #define   CRST    0x08  /* force reset                      */

/*02*/  u_char    nc_scntl2;    /* no disconnect expected           */
	#define   SDU     0x80  /* cmd: disconnect will raise error */
	#define   CHM     0x40  /* sta: chained mode                */
	#define   WSS     0x08  /* sta: wide scsi send           [W]*/
	#define   WSR     0x01  /* sta: wide scsi received       [W]*/

/*03*/  u_char    nc_scntl3;    /* cnf system clock dependent       */
	#define   EWS     0x08  /* cmd: enable wide scsi         [W]*/

/*04*/  u_char    nc_scid;	/* cnf host adapter scsi address    */
	#define   RRE     0x40  /* r/w:e enable response to resel.  */
	#define   SRE     0x20  /* r/w:e enable response to select  */

/*05*/  u_char    nc_sxfer;	/* ### Sync speed and count         */

/*06*/  u_char    nc_sdid;	/* ### Destination-ID               */

/*07*/  u_char    nc_gpreg;	/* ??? IO-Pins                      */

/*08*/  u_char    nc_sfbr;	/* ### First byte in phase          */

/*09*/  u_char    nc_socl;
	#define   CREQ	  0x80	/* r/w: SCSI-REQ                    */
	#define   CACK	  0x40	/* r/w: SCSI-ACK                    */
	#define   CBSY	  0x20	/* r/w: SCSI-BSY                    */
	#define   CSEL	  0x10	/* r/w: SCSI-SEL                    */
	#define   CATN	  0x08	/* r/w: SCSI-ATN                    */
	#define   CMSG	  0x04	/* r/w: SCSI-MSG                    */
	#define   CC_D	  0x02	/* r/w: SCSI-C_D                    */
	#define   CI_O	  0x01	/* r/w: SCSI-I_O                    */

/*0a*/  u_char    nc_ssid;

/*0b*/  u_char    nc_sbcl;

/*0c*/  u_char    nc_dstat;
        #define   DFE     0x80  /* sta: dma fifo empty              */
        #define   MDPE    0x40  /* int: master data parity error    */
        #define   BF      0x20  /* int: script: bus fault           */
        #define   ABRT    0x10  /* int: script: command aborted     */
        #define   SSI     0x08  /* int: script: single step         */
        #define   SIR     0x04  /* int: script: interrupt instruct. */
        #define   IID     0x01  /* int: script: illegal instruct.   */

/*0d*/  u_char    nc_sstat0;
        #define   ILF     0x80  /* sta: data in SIDL register lsb   */
        #define   ORF     0x40  /* sta: data in SODR register lsb   */
        #define   OLF     0x20  /* sta: data in SODL register lsb   */
        #define   AIP     0x10  /* sta: arbitration in progress     */
        #define   LOA     0x08  /* sta: arbitration lost            */
        #define   WOA     0x04  /* sta: arbitration won             */
        #define   IRST    0x02  /* sta: scsi reset signal           */
        #define   SDP     0x01  /* sta: scsi parity signal          */

/*0e*/  u_char    nc_sstat1;
	#define   FF3210  0xf0	/* sta: bytes in the scsi fifo      */

/*0f*/  u_char    nc_sstat2;
        #define   ILF1    0x80  /* sta: data in SIDL register msb[W]*/
        #define   ORF1    0x40  /* sta: data in SODR register msb[W]*/
        #define   OLF1    0x20  /* sta: data in SODL register msb[W]*/
        #define   LDSC    0x02  /* sta: disconnect & reconnect      */

/*10*/  u_int32    nc_dsa;	/* --> Base page                    */

/*14*/  u_char    nc_istat;	/* --> Main Command and status      */
        #define   CABRT   0x80  /* cmd: abort current operation     */
        #define   SRST    0x40  /* mod: reset chip                  */
        #define   SIGP    0x20  /* r/w: message from host to ncr    */
        #define   SEM     0x10  /* r/w: message between host + ncr  */
        #define   CON     0x08  /* sta: connected to scsi           */
        #define   INTF    0x04  /* sta: int on the fly (reset by wr)*/
        #define   SIP     0x02  /* sta: scsi-interrupt              */
        #define   DIP     0x01  /* sta: host/script interrupt       */

/*15*/  u_char    nc_15_;
/*16*/	u_char	  nc_16_;
/*17*/  u_char    nc_17_;

/*18*/	u_char	  nc_ctest0;
/*19*/  u_char    nc_ctest1;

/*1a*/  u_char    nc_ctest2;
	#define   CSIGP   0x40

/*1b*/  u_char    nc_ctest3;
	#define   CLF	  0x04	/* clear scsi fifo		    */

/*1c*/  u_int32    nc_temp;	/* ### Temporary stack              */

/*20*/	u_char	  nc_dfifo;
/*21*/  u_char    nc_ctest4;
/*22*/  u_char    nc_ctest5;
/*23*/  u_char    nc_ctest6;

/*24*/  u_int32    nc_dbc;	/* ### Byte count and command       */
/*28*/  u_int32    nc_dnad;	/* ### Next command register        */
/*2c*/  u_int32    nc_dsp;	/* --> Script Pointer               */
/*30*/  u_int32    nc_dsps;	/* --> Script pointer save/opcode#2 */
/*34*/  u_int32    nc_scratcha;  /* ??? Temporary register a         */

/*38*/  u_char    nc_dmode;
/*39*/  u_char    nc_dien;
/*3a*/  u_char    nc_dwt;

/*3b*/  u_char    nc_dcntl;	/* --> Script execution control     */
        #define   SSM     0x10  /* mod: single step mode            */
        #define   STD     0x04  /* cmd: start dma mode              */
	#define	  NOCOM   0x01	/* cmd: protect sfbr while reselect */

/*3c*/  u_int32    nc_adder;

/*40*/  u_short   nc_sien;	/* -->: interrupt enable            */
/*42*/  u_short   nc_sist;	/* <--: interrupt status            */
        #define   STO     0x0400/* sta: timeout (select)            */
        #define   GEN     0x0200/* sta: timeout (general)           */
        #define   HTH     0x0100/* sta: timeout (handshake)         */
        #define   MA      0x80  /* sta: phase mismatch              */
        #define   CMP     0x40  /* sta: arbitration complete        */
        #define   SEL     0x20  /* sta: selected by another device  */
        #define   RSL     0x10  /* sta: reselected by another device*/
        #define   SGE     0x08  /* sta: gross error (over/underflow)*/
        #define   UDC     0x04  /* sta: unexpected disconnect       */
        #define   RST     0x02  /* sta: scsi bus reset detected     */
        #define   PAR     0x01  /* sta: scsi parity error           */

/*44*/  u_char    nc_slpar;
/*45*/  u_char    nc_swide;
/*46*/  u_char    nc_macntl;
/*47*/  u_char    nc_gpcntl;
/*48*/  u_char    nc_stime0;    /* cmd: timeout for select&handshake*/
/*49*/  u_char    nc_stime1;    /* cmd: timeout user defined        */
/*4a*/  u_short   nc_respid;    /* sta: Reselect-IDs                */

/*4c*/  u_char    nc_stest0;

/*4d*/  u_char    nc_stest1;
	#define   DBLEN   0x08	/* clock doubler running		*/
	#define   DBLSEL  0x04	/* clock doubler selected		*/
  

/*4e*/  u_char    nc_stest2;
	#define   ROF     0x40	/* reset scsi offset (after gross error!) */
	#define   EXT     0x02  /* extended filtering                     */

/*4f*/  u_char    nc_stest3;
	#define   TE     0x80	/* c: tolerAnt enable */
	#define   CSF    0x02	/* c: clear scsi fifo */

/*50*/  u_short   nc_sidl;	/* Lowlevel: latched from scsi data */
/*52*/  u_short   nc_52_;
/*54*/  u_short   nc_sodl;	/* Lowlevel: data out to scsi data  */
/*56*/  u_short   nc_56_;
/*58*/  u_short   nc_sbdl;	/* Lowlevel: data from scsi data    */
/*5a*/  u_short   nc_5a_;
/*5c*/  u_char    nc_scr0;	/* Working register B               */
/*5d*/  u_char    nc_scr1;	/*                                  */
/*5e*/  u_char    nc_scr2;	/*                                  */
/*5f*/  u_char    nc_scr3;	/*                                  */
/*60*/
};

/*-----------------------------------------------------------
**
**	Utility macros for the script.
**
**-----------------------------------------------------------
*/

#define REGJ(p,r) (offsetof(struct ncr_reg, p ## r))
#define REG(r) REGJ (nc_, r)

#ifndef TARGET_MODE
#define TARGET_MODE 0
#endif

typedef u_int32 ncrcmd;

/*-----------------------------------------------------------
**
**	SCSI phases
**
**-----------------------------------------------------------
*/

#define	SCR_DATA_OUT	0x00000000
#define	SCR_DATA_IN	0x01000000
#define	SCR_COMMAND	0x02000000
#define	SCR_STATUS	0x03000000
#define SCR_ILG_OUT	0x04000000
#define SCR_ILG_IN	0x05000000
#define SCR_MSG_OUT	0x06000000
#define SCR_MSG_IN      0x07000000

/*-----------------------------------------------------------
**
**	Data transfer via SCSI.
**
**-----------------------------------------------------------
**
**	MOVE_ABS (LEN)
**	<<start address>>
**
**	MOVE_IND (LEN)
**	<<dnad_offset>>
**
**	MOVE_TBL
**	<<dnad_offset>>
**
**-----------------------------------------------------------
*/

#define SCR_MOVE_ABS(l) ((0x08000000 ^ (TARGET_MODE << 1ul)) | (l))
#define SCR_MOVE_IND(l) ((0x28000000 ^ (TARGET_MODE << 1ul)) | (l))
#define SCR_MOVE_TBL     (0x18000000 ^ (TARGET_MODE << 1ul))

struct scr_tblmove {
        u_int32  size;
        u_int32  addr;
};

/*-----------------------------------------------------------
**
**	Selection
**
**-----------------------------------------------------------
**
**	SEL_ABS | SCR_ID (0..7)     [ | REL_JMP]
**	<<alternate_address>>
**
**	SEL_TBL | << dnad_offset>>  [ | REL_JMP]
**	<<alternate_address>>
**
**-----------------------------------------------------------
*/

#define	SCR_SEL_ABS	0x40000000
#define	SCR_SEL_ABS_ATN	0x41000000
#define	SCR_SEL_TBL	0x42000000
#define	SCR_SEL_TBL_ATN	0x43000000

struct scr_tblsel {
        u_char  sel_0;
        u_char  sel_sxfer;
        u_char  sel_id;
        u_char  sel_scntl3;
};

#define SCR_JMP_REL     0x04000000
#define SCR_ID(id)	(((u_int32)(id)) << 16)

/*-----------------------------------------------------------
**
**	Waiting for Disconnect or Reselect
**
**-----------------------------------------------------------
**
**	WAIT_DISC
**	dummy: <<alternate_address>>
**
**	WAIT_RESEL
**	<<alternate_address>>
**
**-----------------------------------------------------------
*/

#define	SCR_WAIT_DISC	0x48000000
#define SCR_WAIT_RESEL  0x50000000

/*-----------------------------------------------------------
**
**	Bit Set / Reset
**
**-----------------------------------------------------------
**
**	SET (flags {|.. })
**
**	CLR (flags {|.. })
**
**-----------------------------------------------------------
*/

#define SCR_SET(f)     (0x58000000 | (f))
#define SCR_CLR(f)     (0x60000000 | (f))

#define	SCR_CARRY	0x00000400
#define	SCR_TRG		0x00000200
#define	SCR_ACK		0x00000040
#define	SCR_ATN		0x00000008




/*-----------------------------------------------------------
**
**	Memory to memory move
**
**-----------------------------------------------------------
**
**	COPY (bytecount)
**	<< source_address >>
**	<< destination_address >>
**
**-----------------------------------------------------------
*/

#define SCR_COPY(n) (0xc0000000 | (n))

/*-----------------------------------------------------------
**
**	Register move and binary operations
**
**-----------------------------------------------------------
**
**	SFBR_REG (reg, op, data)        reg  = SFBR op data
**	<< 0 >>
**
**	REG_SFBR (reg, op, data)        SFBR = reg op data
**	<< 0 >>
**
**	REG_REG  (reg, op, data)        reg  = reg op data
**	<< 0 >>
**
**-----------------------------------------------------------
*/

#define SCR_REG_OFS(ofs) ((ofs) << 16ul)

#define SCR_SFBR_REG(reg,op,data) \
        (0x68000000 | (SCR_REG_OFS(REG(reg))) | (op) | ((data)<<8ul))

#define SCR_REG_SFBR(reg,op,data) \
        (0x70000000 | (SCR_REG_OFS(REG(reg))) | (op) | ((data)<<8ul))

#define SCR_REG_REG(reg,op,data) \
        (0x78000000 | (SCR_REG_OFS(REG(reg))) | (op) | ((data)<<8ul))


#define      SCR_LOAD   0x00000000
#define      SCR_SHL    0x01000000
#define      SCR_OR     0x02000000
#define      SCR_XOR    0x03000000
#define      SCR_AND    0x04000000
#define      SCR_SHR    0x05000000
#define      SCR_ADD    0x06000000
#define      SCR_ADDC   0x07000000

/*-----------------------------------------------------------
**
**	FROM_REG (reg)		  reg  = SFBR
**	<< 0 >>
**
**	TO_REG	 (reg)		  SFBR = reg
**	<< 0 >>
**
**	LOAD_REG (reg, data)	  reg  = <data>
**	<< 0 >>
**
**	LOAD_SFBR(data) 	  SFBR = <data>
**	<< 0 >>
**
**-----------------------------------------------------------
*/

#define	SCR_FROM_REG(reg) \
	SCR_REG_SFBR(reg,SCR_OR,0)

#define	SCR_TO_REG(reg) \
	SCR_SFBR_REG(reg,SCR_OR,0)

#define	SCR_LOAD_REG(reg,data) \
	SCR_REG_REG(reg,SCR_LOAD,data)

#define SCR_LOAD_SFBR(data) \
        (SCR_REG_SFBR (gpreg, SCR_LOAD, data))

/*-----------------------------------------------------------
**
**	Waiting for Disconnect or Reselect
**
**-----------------------------------------------------------
**
**	JUMP            [ | IFTRUE/IFFALSE ( ... ) ]
**	<<address>>
**
**	JUMPR           [ | IFTRUE/IFFALSE ( ... ) ]
**	<<distance>>
**
**	CALL            [ | IFTRUE/IFFALSE ( ... ) ]
**	<<address>>
**
**	CALLR           [ | IFTRUE/IFFALSE ( ... ) ]
**	<<distance>>
**
**	RETURN          [ | IFTRUE/IFFALSE ( ... ) ]
**	<<dummy>>
**
**	INT             [ | IFTRUE/IFFALSE ( ... ) ]
**	<<ident>>
**
**	INT_FLY         [ | IFTRUE/IFFALSE ( ... ) ]
**	<<ident>>
**
**	Conditions:
**	     WHEN (phase)
**	     IF   (phase)
**	     CARRY
**	     DATA (data, mask)
**
**-----------------------------------------------------------
*/

#define SCR_JUMP        0x80080000
#define SCR_JUMPR       0x80880000
#define SCR_CALL        0x88080000
#define SCR_CALLR       0x88880000
#define SCR_RETURN      0x90080000
#define SCR_INT         0x98080000
#define SCR_INT_FLY     0x98180000

#define IFFALSE(arg)   (0x00080000 | (arg))
#define IFTRUE(arg)    (0x00000000 | (arg))

#define WHEN(phase)    (0x00030000 | (phase))
#define IF(phase)      (0x00020000 | (phase))

#define DATA(D)        (0x00040000 | ((D) & 0xff))
#define MASK(D,M)      (0x00040000 | (((M ^ 0xff) & 0xff) << 8ul)|((D) & 0xff))

#define CARRYSET       (0x00200000)

/*-----------------------------------------------------------
**
**	SCSI  constants.
**
**-----------------------------------------------------------
*/

/*
**	Messages
*/

#define	M_COMPLETE	(0x00)
#define	M_EXTENDED	(0x01)
#define	M_SAVE_DP	(0x02)
#define	M_RESTORE_DP	(0x03)
#define	M_DISCONNECT	(0x04)
#define	M_ID_ERROR	(0x05)
#define	M_ABORT		(0x06)
#define	M_REJECT	(0x07)
#define	M_NOOP		(0x08)
#define	M_PARITY	(0x09)
#define	M_LCOMPLETE	(0x0a)
#define	M_FCOMPLETE	(0x0b)
#define	M_RESET		(0x0c)
#define	M_ABORT_TAG	(0x0d)
#define	M_CLEAR_QUEUE	(0x0e)
#define	M_INIT_REC	(0x0f)
#define	M_REL_REC	(0x10)
#define	M_TERMINATE	(0x11)
#define	M_SIMPLE_TAG	(0x20)
#define	M_HEAD_TAG	(0x21)
#define	M_ORDERED_TAG	(0x22)
#define	M_IGN_RESIDUE	(0x23)
#define	M_IDENTIFY   	(0x80)

#define	M_X_MODIFY_DP	(0x00)
#define	M_X_SYNC_REQ	(0x01)
#define	M_X_WIDE_REQ	(0x03)

/*
**	Status
*/

#define	S_GOOD		(0x00)
#define	S_CHECK_COND	(0x02)
#define	S_COND_MET	(0x04)
#define	S_BUSY		(0x08)
#define	S_INT		(0x10)
#define	S_INT_COND_MET	(0x14)
#define	S_CONFLICT	(0x18)
#define	S_TERMINATED	(0x20)
#define	S_QUEUE_FULL	(0x28)
#define	S_ILLEGAL	(0xff)
#define	S_SENSE		(0x80)

/*
 * End of ncrreg from FreeBSD
 */

#endif /* !defined HOSTS_C */

#endif /* defined NCR53C8XX_H */

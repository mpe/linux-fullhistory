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
#define SCSI_NCR_DRIVER_NAME		"ncr53c8xx - revision 3.1f"

/*
**	Check supported Linux versions
*/

#if !defined(LINUX_VERSION_CODE)
#include <linux/version.h>
#endif
#include <linux/config.h>

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

/*
 *	No more an option, enabled by default.
 */
#ifndef CONFIG_SCSI_NCR53C8XX_NVRAM_DETECT
#define CONFIG_SCSI_NCR53C8XX_NVRAM_DETECT
#endif

/*
**	These options are not tunable from 'make config'
*/
#define	SCSI_NCR_PROC_INFO_SUPPORT
#define SCSI_NCR_SHARE_IRQ

/*
**	If you want a driver as small as possible, donnot define the 
**	following options.
*/
#define SCSI_NCR_BOOT_COMMAND_LINE_SUPPORT
#define SCSI_NCR_DEBUG_INFO_SUPPORT
#define SCSI_NCR_PCI_FIX_UP_SUPPORT
#ifdef	SCSI_NCR_PROC_INFO_SUPPORT
#	ifdef	CONFIG_SCSI_NCR53C8XX_PROFILE
#		define	SCSI_NCR_PROFILE_SUPPORT
#	endif
#	define	SCSI_NCR_USER_COMMAND_SUPPORT
#	define	SCSI_NCR_USER_INFO_SUPPORT
#endif

/*==========================================================
**
** nvram settings - #define SCSI_NCR_NVRAM_SUPPORT to enable
**
**==========================================================
*/

#ifdef CONFIG_SCSI_NCR53C8XX_NVRAM_DETECT
#define SCSI_NCR_NVRAM_SUPPORT
/* #define SCSI_NCR_DEBUG_NVRAM */
#endif

/* ---------------------------------------------------------------------
** Take into account kernel configured parameters.
** Most of these options can be overridden at startup by a command line.
** ---------------------------------------------------------------------
*/

/*
 * For Ultra2 SCSI support option, use special features and allow 40Mhz 
 * synchronous data transfers.
 */
#define	SCSI_NCR_SETUP_SPECIAL_FEATURES		(3)
#define SCSI_NCR_SETUP_ULTRA_SCSI		(2)
#define SCSI_NCR_MAX_SYNC			(40)

/*
 * Allow tags from 2 to 64, default 8
 */
#ifdef	CONFIG_SCSI_NCR53C8XX_MAX_TAGS
#if	CONFIG_SCSI_NCR53C8XX_MAX_TAGS < 2
#define SCSI_NCR_MAX_TAGS	(2)
#elif	CONFIG_SCSI_NCR53C8XX_MAX_TAGS > 64
#define SCSI_NCR_MAX_TAGS	(64)
#else
#define	SCSI_NCR_MAX_TAGS	CONFIG_SCSI_NCR53C8XX_MAX_TAGS
#endif
#else
#define SCSI_NCR_MAX_TAGS	(8)
#endif

/*
 * Allow tagged command queuing support if configured with default number 
 * of tags set to max (see above).
 */
#ifdef	CONFIG_SCSI_NCR53C8XX_DEFAULT_TAGS
#define	SCSI_NCR_SETUP_DEFAULT_TAGS	CONFIG_SCSI_NCR53C8XX_DEFAULT_TAGS
#elif	defined CONFIG_SCSI_NCR53C8XX_TAGGED_QUEUE
#define	SCSI_NCR_SETUP_DEFAULT_TAGS	SCSI_NCR_MAX_TAGS
#else
#define	SCSI_NCR_SETUP_DEFAULT_TAGS	(0)
#endif

/*
 * Use normal IO if configured. Forced for alpha and ppc.
 */
#if defined(CONFIG_SCSI_NCR53C8XX_IOMAPPED)
#define	SCSI_NCR_IOMAPPED
#elif defined(__alpha__) || defined(__powerpc__)
#define	SCSI_NCR_IOMAPPED
#elif defined(__sparc__)
#undef SCSI_NCR_IOMAPPED
#endif

/*
 * Sync transfer frequency at startup.
 * Allow from 5Mhz to 40Mhz default 20 Mhz.
 */
#ifndef	CONFIG_SCSI_NCR53C8XX_SYNC
#define	CONFIG_SCSI_NCR53C8XX_SYNC	(20)
#elif	CONFIG_SCSI_NCR53C8XX_SYNC > SCSI_NCR_MAX_SYNC
#undef	CONFIG_SCSI_NCR53C8XX_SYNC
#define	CONFIG_SCSI_NCR53C8XX_SYNC	SCSI_NCR_MAX_SYNC
#endif

#if	CONFIG_SCSI_NCR53C8XX_SYNC == 0
#define	SCSI_NCR_SETUP_DEFAULT_SYNC	(255)
#elif	CONFIG_SCSI_NCR53C8XX_SYNC <= 5
#define	SCSI_NCR_SETUP_DEFAULT_SYNC	(50)
#elif	CONFIG_SCSI_NCR53C8XX_SYNC <= 20
#define	SCSI_NCR_SETUP_DEFAULT_SYNC	(250/(CONFIG_SCSI_NCR53C8XX_SYNC))
#elif	CONFIG_SCSI_NCR53C8XX_SYNC <= 33
#define	SCSI_NCR_SETUP_DEFAULT_SYNC	(11)
#else
#define	SCSI_NCR_SETUP_DEFAULT_SYNC	(10)
#endif

/*
 * Disallow disconnections at boot-up
 */
#ifdef CONFIG_SCSI_NCR53C8XX_NO_DISCONNECT
#define SCSI_NCR_SETUP_DISCONNECTION	(0)
#else
#define SCSI_NCR_SETUP_DISCONNECTION	(1)
#endif

/*
 * Force synchronous negotiation for all targets
 */
#ifdef CONFIG_SCSI_NCR53C8XX_FORCE_SYNC_NEGO
#define SCSI_NCR_SETUP_FORCE_SYNC_NEGO	(1)
#else
#define SCSI_NCR_SETUP_FORCE_SYNC_NEGO	(0)
#endif

/*
 * Disable master parity checking (flawed hardwares need that)
 */
#ifdef CONFIG_SCSI_NCR53C8XX_DISABLE_MPARITY_CHECK
#define SCSI_NCR_SETUP_MASTER_PARITY	(0)
#else
#define SCSI_NCR_SETUP_MASTER_PARITY	(1)
#endif

/*
 * Disable scsi parity checking (flawed devices may need that)
 */
#ifdef CONFIG_SCSI_NCR53C8XX_DISABLE_PARITY_CHECK
#define SCSI_NCR_SETUP_SCSI_PARITY	(0)
#else
#define SCSI_NCR_SETUP_SCSI_PARITY	(1)
#endif

/*
 * Vendor specific stuff
 */
#ifdef CONFIG_SCSI_NCR53C8XX_SYMBIOS_COMPAT
#define SCSI_NCR_SETUP_LED_PIN		(1)
#define SCSI_NCR_SETUP_DIFF_SUPPORT	(3)
#else
#define SCSI_NCR_SETUP_LED_PIN		(0)
#define SCSI_NCR_SETUP_DIFF_SUPPORT	(0)
#endif

/*
 * Settle time after reset at boot-up
 */
#define SCSI_NCR_SETUP_SETTLE_TIME	(2)

/*
**	Other parameters not configurable with "make config"
**	Avoid to change these constants, unless you know what you are doing.
*/

#define SCSI_NCR_ALWAYS_SIMPLE_TAG
#define SCSI_NCR_MAX_SCATTER	(127)
#define SCSI_NCR_MAX_TARGET	(16)

/* No need to use a too large adapter queue */
#if SCSI_NCR_MAX_TAGS <= 32
#define SCSI_NCR_CAN_QUEUE	(7*SCSI_NCR_MAX_TAGS)
#else
#define SCSI_NCR_CAN_QUEUE	(250)
#endif

#define SCSI_NCR_CMD_PER_LUN	(SCSI_NCR_MAX_TAGS)
#define SCSI_NCR_SG_TABLESIZE	(SCSI_NCR_MAX_SCATTER)

#define SCSI_NCR_TIMER_INTERVAL	(HZ)

#if 1 /* defined CONFIG_SCSI_MULTI_LUN */
#define SCSI_NCR_MAX_LUN	(8)
#else
#define SCSI_NCR_MAX_LUN	(1)
#endif

/*
**	Define Scsi_Host_Template parameters
**
**	Used by hosts.c and ncr53c8xx.c with module configuration.
*/

#if defined(HOSTS_C) || defined(MODULE)

#include <scsi/scsicam.h>

int ncr53c8xx_abort(Scsi_Cmnd *);
int ncr53c8xx_detect(Scsi_Host_Template *tpnt);
int ncr53c8xx_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int ncr53c8xx_reset(Scsi_Cmnd *, unsigned int);

#ifdef MODULE
int ncr53c8xx_release(struct Scsi_Host *);
#else
#define ncr53c8xx_release NULL
#endif

#if	LINUX_VERSION_CODE >= LinuxVersionCode(2,1,75)

#define NCR53C8XX {     name:           SCSI_NCR_DRIVER_NAME,	\
			detect:         ncr53c8xx_detect,	\
			release:        ncr53c8xx_release,	\
			queuecommand:   ncr53c8xx_queue_command,\
			abort:          ncr53c8xx_abort,	\
			reset:          ncr53c8xx_reset,	\
			bios_param:     scsicam_bios_param,	\
			can_queue:      SCSI_NCR_CAN_QUEUE,	\
			this_id:        7,			\
			sg_tablesize:   SCSI_NCR_SG_TABLESIZE,	\
			cmd_per_lun:    SCSI_NCR_CMD_PER_LUN,	\
			use_clustering: DISABLE_CLUSTERING} 

#else

#define NCR53C8XX {	NULL, NULL, NULL, NULL,				\
			SCSI_NCR_DRIVER_NAME,	ncr53c8xx_detect,	\
			ncr53c8xx_release,	NULL,	NULL,		\
			ncr53c8xx_queue_command,ncr53c8xx_abort,	\
			ncr53c8xx_reset, NULL,	scsicam_bios_param,	\
			SCSI_NCR_CAN_QUEUE,	7,			\
			SCSI_NCR_SG_TABLESIZE,	SCSI_NCR_CMD_PER_LUN,	\
			0,	0,	DISABLE_CLUSTERING} 
 
#endif /* LINUX_VERSION_CODE */

#endif /* defined(HOSTS_C) || defined(MODULE) */ 

#ifndef HOSTS_C

/*
**	IO functions definition for big/little endian support.
**	For now, the NCR is only supported in little endian addressing mode, 
**	and big endian byte ordering is only supported for the PPC.
**	MMIO is not used on PPC.
*/

#ifdef	__BIG_ENDIAN

#if	LINUX_VERSION_CODE < LinuxVersionCode(2,1,0)
#error	"BIG ENDIAN byte ordering needs kernel version >= 2.1.0"
#endif

#if defined(__powerpc__)
#define	inw_l2b		inw
#define	inl_l2b		inl
#define	outw_b2l	outw
#define	outl_b2l	outl
#elif defined(__sparc__)
#define	readw_l2b	readw
#define	readl_l2b	readl
#define	writew_b2l	writew
#define	writel_b2l	writel
#else
#error	"Support for BIG ENDIAN is only available for PowerPC and SPARC"
#endif

#else	/* Assumed x86 or alpha */

#define	inw_raw		inw
#define	inl_raw		inl
#define	outw_raw	outw
#define	outl_raw	outl
#define	readw_raw	readw
#define	readl_raw	readl
#define	writew_raw	writew
#define	writel_raw	writel

#endif

#ifdef	SCSI_NCR_BIG_ENDIAN
#error	"The NCR in BIG ENDIAN addressing mode is not (yet) supported"
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

#ifndef PCI_DEVICE_ID_NCR_53C875J
#define PCI_DEVICE_ID_NCR_53C875J 0x8f
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

/*
**   NCR53C8XX devices features table.
*/
typedef struct {
	unsigned short	device_id;
	unsigned short	revision_id;
	char	*name;
	unsigned char	burst_max;
	unsigned char	offset_max;
	unsigned char	nr_divisor;
	unsigned int	features;
#define FE_LED0		(1<<0)
#define FE_WIDE		(1<<1)
#define FE_ULTRA	(1<<2)
#define FE_ULTRA2	(1<<3)
#define FE_DBLR		(1<<4)
#define FE_QUAD		(1<<5)
#define FE_ERL		(1<<6)
#define FE_CLSE		(1<<7)
#define FE_WRIE		(1<<8)
#define FE_ERMP		(1<<9)
#define FE_BOF		(1<<10)
#define FE_DFS		(1<<11)
#define FE_PFEN		(1<<12)
#define FE_LDSTR	(1<<13)
#define FE_RAM		(1<<14)
#define FE_CLK80	(1<<15)
#define FE_RAM8K	(1<<16)
#define FE_64BIT	(1<<17)
#define FE_IO256	(1<<18)
#define FE_NOPM		(1<<19)
#define FE_CACHE_SET	(FE_ERL|FE_CLSE|FE_WRIE|FE_ERMP)
#define FE_SCSI_SET	(FE_WIDE|FE_ULTRA|FE_ULTRA2|FE_DBLR|FE_QUAD|F_CLK80)
#define FE_SPECIAL_SET	(FE_CACHE_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM)
} ncr_chip;

/*
**	DEL 397 - 53C875 Rev 3 - Part Number 609-0392410 - ITEM 3.
**	Memory Read transaction terminated by a retry followed by 
**	Memory Read Line command.
*/
#define FE_CACHE0_SET	(FE_CACHE_SET & ~FE_ERL)

/*
**	DEL 397 - 53C875 Rev 3 - Part Number 609-0392410 - ITEM 5.
**	On paper, this errata is harmless. But it is a good reason for 
**	using a shorter programmed burst length (64 DWORDS instead of 128).
*/

#define SCSI_NCR_CHIP_TABLE						\
{									\
 {PCI_DEVICE_ID_NCR_53C810, 0x0f, "810",  4,  8, 4,			\
 FE_ERL}								\
 ,									\
 {PCI_DEVICE_ID_NCR_53C810, 0xff, "810a", 4,  8, 4,			\
 FE_CACHE_SET|FE_LDSTR|FE_PFEN|FE_BOF}					\
 ,									\
 {PCI_DEVICE_ID_NCR_53C815, 0xff, "815",  4,  8, 4,			\
 FE_ERL|FE_BOF}								\
 ,									\
 {PCI_DEVICE_ID_NCR_53C820, 0xff, "820",  4,  8, 4,			\
 FE_WIDE|FE_ERL}							\
 ,									\
 {PCI_DEVICE_ID_NCR_53C825, 0x0f, "825",  4,  8, 4,			\
 FE_WIDE|FE_ERL|FE_BOF}							\
 ,									\
 {PCI_DEVICE_ID_NCR_53C825, 0xff, "825a", 6,  8, 4,			\
 FE_WIDE|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM}		\
 ,									\
 {PCI_DEVICE_ID_NCR_53C860, 0xff, "860",  4,  8, 5,			\
 FE_ULTRA|FE_CLK80|FE_CACHE_SET|FE_BOF|FE_LDSTR|FE_PFEN}		\
 ,									\
 {PCI_DEVICE_ID_NCR_53C875, 0x01, "875",  6, 16, 5,			\
 FE_WIDE|FE_ULTRA|FE_CLK80|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM}\
 ,									\
 {PCI_DEVICE_ID_NCR_53C875, 0x0f, "875",  6, 16, 5,			\
 FE_WIDE|FE_ULTRA|FE_DBLR|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM}\
 ,									\
 {PCI_DEVICE_ID_NCR_53C875, 0xff, "876",  6, 16, 5,			\
 FE_WIDE|FE_ULTRA|FE_DBLR|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM}\
 ,									\
 {PCI_DEVICE_ID_NCR_53C875J,0xff, "875J", 6, 16, 5,			\
 FE_WIDE|FE_ULTRA|FE_DBLR|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM}\
 ,									\
 {PCI_DEVICE_ID_NCR_53C885, 0xff, "885",  6, 16, 5,			\
 FE_WIDE|FE_ULTRA|FE_DBLR|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM}\
 ,									\
 {PCI_DEVICE_ID_NCR_53C895, 0xff, "895",  7, 31, 7,			\
 FE_WIDE|FE_ULTRA2|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM}\
 ,									\
 {PCI_DEVICE_ID_NCR_53C896, 0xff, "896",  7, 31, 7,			\
 FE_WIDE|FE_ULTRA2|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM|\
 FE_RAM8K|FE_64BIT|FE_IO256|FE_NOPM}\
}

/*
 * List of supported NCR chip ids
 */
#define SCSI_NCR_CHIP_IDS		\
{					\
	PCI_DEVICE_ID_NCR_53C810,	\
	PCI_DEVICE_ID_NCR_53C815,	\
	PCI_DEVICE_ID_NCR_53C820,	\
	PCI_DEVICE_ID_NCR_53C825,	\
	PCI_DEVICE_ID_NCR_53C860,	\
	PCI_DEVICE_ID_NCR_53C875,	\
	PCI_DEVICE_ID_NCR_53C875J,	\
	PCI_DEVICE_ID_NCR_53C885,	\
	PCI_DEVICE_ID_NCR_53C895,	\
	PCI_DEVICE_ID_NCR_53C896	\
}

/*
**	Initial setup.
**	Can be overriden at startup by a command line.
*/
#define SCSI_NCR_DRIVER_SETUP			\
{						\
	SCSI_NCR_SETUP_MASTER_PARITY,		\
	SCSI_NCR_SETUP_SCSI_PARITY,		\
	SCSI_NCR_SETUP_DISCONNECTION,		\
	SCSI_NCR_SETUP_SPECIAL_FEATURES,	\
	SCSI_NCR_SETUP_ULTRA_SCSI,		\
	SCSI_NCR_SETUP_FORCE_SYNC_NEGO,		\
	0,					\
	0,					\
	1,					\
	1,					\
	SCSI_NCR_SETUP_DEFAULT_TAGS,		\
	SCSI_NCR_SETUP_DEFAULT_SYNC,		\
	0x00,					\
	7,					\
	SCSI_NCR_SETUP_LED_PIN,			\
	1,					\
	SCSI_NCR_SETUP_SETTLE_TIME,		\
	SCSI_NCR_SETUP_DIFF_SUPPORT,		\
	0,					\
	1					\
}

/*
**	Boot fail safe setup.
**	Override initial setup from boot command line:
**	ncr53c8xx=safe:y
*/
#define SCSI_NCR_DRIVER_SAFE_SETUP		\
{						\
	0,					\
	1,					\
	0,					\
	0,					\
	0,					\
	0,					\
	0,					\
	0,					\
	1,					\
	2,					\
	0,					\
	255,					\
	0x00,					\
	255,					\
	0,					\
	0,					\
	10,					\
	1,					\
	1,					\
	1					\
}

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
	#define   ULTRA   0x80  /* cmd: ULTRA enable                */

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
        #define   DM      0x04  /* sta: DIFFSENS mismatch (895/6 only) */
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
	#define   FLF     0x08  /* cmd: flush dma fifo              */
	#define   CLF	  0x04	/* cmd: clear dma fifo		    */
	#define   FM      0x02  /* mod: fetch pin mode              */
	#define   WRIE    0x01  /* mod: write and invalidate enable */

/*1c*/  u_int32    nc_temp;	/* ### Temporary stack              */

/*20*/	u_char	  nc_dfifo;
/*21*/  u_char    nc_ctest4;
	#define   BDIS    0x80  /* mod: burst disable               */
	#define   MPEE    0x08  /* mod: master parity error enable  */

/*22*/  u_char    nc_ctest5;
	#define   DFS     0x20  /* mod: dma fifo size               */
/*23*/  u_char    nc_ctest6;

/*24*/  u_int32    nc_dbc;	/* ### Byte count and command       */
/*28*/  u_int32    nc_dnad;	/* ### Next command register        */
/*2c*/  u_int32    nc_dsp;	/* --> Script Pointer               */
/*30*/  u_int32    nc_dsps;	/* --> Script pointer save/opcode#2 */

/*34*/  u_char     nc_scratcha;  /* Temporary register a            */
/*35*/  u_char     nc_scratcha1;
/*36*/  u_char     nc_scratcha2;
/*37*/  u_char     nc_scratcha3;

/*38*/  u_char    nc_dmode;
	#define   BL_2    0x80  /* mod: burst length shift value +2 */
	#define   BL_1    0x40  /* mod: burst length shift value +1 */
	#define   ERL     0x08  /* mod: enable read line            */
	#define   ERMP    0x04  /* mod: enable read multiple        */
	#define   BOF     0x02  /* mod: burst op code fetch         */

/*39*/  u_char    nc_dien;
/*3a*/  u_char    nc_dwt;

/*3b*/  u_char    nc_dcntl;	/* --> Script execution control     */

	#define   CLSE    0x80  /* mod: cache line size enable      */
	#define   PFF     0x40  /* cmd: pre-fetch flush             */
	#define   PFEN    0x20  /* mod: pre-fetch enable            */
	#define   SSM     0x10  /* mod: single step mode            */
	#define   IRQM    0x08  /* mod: irq mode (1 = totem pole !) */
	#define   STD     0x04  /* cmd: start dma mode              */
	#define   IRQD    0x02  /* mod: irq disable                 */
 	#define	  NOCOM   0x01	/* cmd: protect sfbr while reselect */

/*3c*/  u_int32    nc_adder;

/*40*/  u_short   nc_sien;	/* -->: interrupt enable            */
/*42*/  u_short   nc_sist;	/* <--: interrupt status            */
        #define   SBMC    0x1000/* sta: SCSI Bus Mode Change (895/6 only) */
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
	#define   HSC    0x20	/* c: Halt SCSI Clock */
	#define   CSF    0x02	/* c: clear scsi fifo */

/*50*/  u_short   nc_sidl;	/* Lowlevel: latched from scsi data */
/*52*/  u_char    nc_stest4;
	#define   SMODE  0xc0	/* SCSI bus mode      (895/6 only) */
	#define    SMODE_HVD 0x40	/* High Voltage Differential       */
	#define    SMODE_SE  0x80	/* Single Ended                    */
	#define    SMODE_LVD 0xc0	/* Low Voltage Differential        */
	#define   LCKFRQ 0x20	/* Frequency Lock (895/6 only)     */

/*53*/	u_char    nc_53_;
/*54*/  u_short   nc_sodl;	/* Lowlevel: data out to scsi data  */
/*56*/	u_char    nc_ccntl0;	/* Chip Control 0 (896)             */
	#define   ENPMJ  0x80	/* Enable Phase Mismatch Jump       */
	#define   PMJCTL 0x40	/* Phase Mismatch Jump Control      */
	#define   ENNDJ  0x20	/* Enable Non Data PM Jump          */
	#define   DISFC  0x10	/* Disable Auto FIFO Clear          */
	#define   DILS   0x02	/* Disable Internal Load/Store      */
	#define   DPR    0x01	/* Disable Pipe Req                 */

/*57*/	u_char    nc_ccntl1;	/* Chip Control 1 (896)             */
	#define   ZMOD   0x80	/* High Impedance Mode              */
	#define   DDAC   0x08	/* Disable Dual Address Cycle       */
	#define   XTIMOD 0x04	/* 64-bit Table Ind. Indexing Mode  */
	#define   EXTIBMV 0x02	/* Enable 64-bit Table Ind. BMOV    */
	#define   EXDBMV 0x01	/* Enable 64-bit Direct BMOV        */
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
**	SCR_COPY   sets the NO FLUSH option by default.
**	SCR_COPY_F does not set this option.
**
**	For chips which do not support this option,
**	ncr_copy_and_bind() will remove this bit.
**-----------------------------------------------------------
*/

#define SCR_NO_FLUSH 0x01000000

#define SCR_COPY(n) (0xc0000000 | SCR_NO_FLUSH | (n))
#define SCR_COPY_F(n) (0xc0000000 | (n))

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
**	FROM_REG (reg)		  SFBR = reg
**	<< 0 >>
**
**	TO_REG	 (reg)		  reg  = SFBR
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
**	LOAD  from memory   to register.
**	STORE from register to memory.
**
**-----------------------------------------------------------
**
**	LOAD_ABS (LEN)
**	<<start address>>
**
**	LOAD_REL (LEN)        (DSA relative)
**	<<dsa_offset>>
**
**-----------------------------------------------------------
*/

#define SCR_NO_FLUSH2	0x02000000
#define SCR_DSA_REL2	0x10000000

#define SCR_LOAD_R(reg, how, n) \
        (0xe1000000 | how | (SCR_REG_OFS(REG(reg))) | (n))

#define SCR_STORE_R(reg, how, n) \
        (0xe0000000 | how | (SCR_REG_OFS(REG(reg))) | (n))

#define SCR_LOAD_ABS(reg, n)	SCR_LOAD_R(reg, SCR_NO_FLUSH2, n)
#define SCR_LOAD_REL(reg, n)	SCR_LOAD_R(reg, SCR_NO_FLUSH2|SCR_DSA_REL2, n)
#define SCR_LOAD_ABS_F(reg, n)	SCR_LOAD_R(reg, 0, n)
#define SCR_LOAD_REL_F(reg, n)	SCR_LOAD_R(reg, SCR_DSA_REL2, n)

#define SCR_STORE_ABS(reg, n)	SCR_STORE_R(reg, SCR_NO_FLUSH2, n)
#define SCR_STORE_REL(reg, n)	SCR_STORE_R(reg, SCR_NO_FLUSH2|SCR_DSA_REL2,n)
#define SCR_STORE_ABS_F(reg, n)	SCR_STORE_R(reg, 0, n)
#define SCR_STORE_REL_F(reg, n)	SCR_STORE_R(reg, SCR_DSA_REL2, n)


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

#define SCR_NO_OP       0x80000000
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

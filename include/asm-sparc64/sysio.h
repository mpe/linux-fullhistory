/* $Id: sysio.h,v 1.2 1997/04/03 12:26:45 davem Exp $
 * sysio.h: UltraSparc sun5 specific SBUS definitions.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SYSIO_H
#define __SPARC64_SYSIO_H

#include <linux/types.h>

/* SUN5 SYSIO Register Set, one per controller probed. */
struct sysio_regs {
/*0x0000*/	u64	upa_id;		/* SYSIO UPA Port ID Register		*/
/*0x0008*/	u64	upa_cfg;	/* SYSIO UPA Config Register		*/
/*0x0010*/	u64	control;	/* SYSIO Control Register		*/
/*0x0018*/	u64	_unused1;
/*0x0020*/	u64	ecc_control;	/* ECC Control Register			*/
/*0x0028*/	u64	_unused2;

		/* Uncorrectable Error Fault Registers */
/*0x0030*/	u64	ue_afsr;	/* UE Async Fault Status		*/
/*0x0038*/	u64	ue_afar;	/* UE Async Fault Address		*/

		/* Correctable Error Fault Registers */
/*0x0040*/	u64	ce_afsr;	/* CE Async Fault Status		*/
/*0x0048*/	u64	ce_afar;	/* CE Async Fault Address		*/

		u64	__pad0[0x16];

		/* Performance Monitoring Registers */
/*0x0100*/	u64	pmon_control;
/*0x0108*/	u64	pmon_counter;

		u64	__pad1[0x3de];

		/* SBUS Module Registers */
/*0x2000*/	u64	sbus_control;	/* SBUS Control Register		*/
/*0x2008*/	u64	_unused3;
/*0x2010*/	u64	sbus_afsr;	/* SBUS Async Fault Status		*/
/*0x2018*/	u64	sbus_afar;	/* SBUS Async Fault Address		*/

		/* SBUS Slot Configuration Registers.
		 * On Fusion/Electron/Pulsar desktops/servers slots 4-->6
		 * are for on-board devices, in particular for Electron/Pulsar
		 * they are:
		 *
		 *	slot 4) Audio
		 *	slot 5) MACIO
		 *	slot 6) SLAVIO
		 *
		 * On Sunfire/Wildfire enterprise boxen these upper slots
		 * are unused.
		 */
/*0x2020*/	u64	sbus_s0cfg;	/* SBUS Slot 0 Config			*/
/*0x2028*/	u64	sbus_s1cfg;	/* SBUS Slot 1 Config			*/
/*0x2030*/	u64	sbus_s2cfg;	/* SBUS Slot 2 Config			*/
/*0x2038*/	u64	sbus_s3cfg;	/* SBUS Slot 3 Config			*/
/*0x2040*/	u64	sbus_s4cfg;	/* SBUS Slot 4 Config			*/
/*0x2048*/	u64	sbus_s5cfg;	/* SBUS Slot 5 Config			*/
/*0x2050*/	u64	sbus_s6cfg;	/* SBUS Slot 6 Config			*/

		u64	__pad2[0x75];

		/* SBUS IOMMU lives here */
/*0x2400*/	u64	iommu_control;	/* IOMMU Control			*/
/*0x2408*/	u64	iommu_tsbbase;	/* IOMMU TSB Base			*/
/*0x2410*/	u64	iommu_flush;	/* IOMMU Flush Register			*/

		u64	__pad3[0x7d];

		/* SBUS/IOMMU Streaming Buffer Registers */
/*0x2800*/	u64	sbuf_control;	/* StrBuffer Control			*/
/*0x2808*/	u64	sbuf_pflush;	/* StrBuffer Page Flush			*/
/*0x2810*/	u64	sbus_fsync;	/* StrBuffer Flush Synchronization Reg	*/

		u64	__pad4[0x7d];

		/* Interrupt mapping/control registers */
/*0x2c00*/	u32	imap_slot0, _uim0;	/* SBUS Slot 0 Int Mapping	*/
/*0x2c08*/	u32	imap_slot1, _uim1;	/* SBUS Slot 1 Int Mapping	*/
/*0x2c10*/	u32	imap_slot2, _uim2;	/* SBUS Slot 2 Int Mapping	*/
/*0x2c18*/	u32	imap_slot3, _uim3;	/* SBUS Slot 3 Int Mapping	*/

		/* Interrupt Retry Timer. */
/*0x2c20*/	u32	irq_retry,  _irpad;

		u64	__pad5[0x7b];

		/* The following are only used on Fusion/Electron/Pulsar
		 * desktop systems, they mean nothing on Sunfire/Wildfire
		 */
/*0x3000*/	u32	imap_scsi,  _uis;	/* SCSI Int Mapping		*/
/*0x3008*/	u32	imap_eth,   _uie;	/* Ethernet Int Mapping		*/
/*0x3010*/	u32	imap_bpp,   _uip;	/* Parallel Port Int Mapping	*/
/*0x3018*/	u32	imap_audio, _uia;	/* Audio Int Mapping		*/
/*0x3020*/	u32	imap_pfail, _uipf;	/* Power Fail Int Mapping	*/
/*0x3028*/	u32	imap_kms,   _uik;	/* Kbd/Mouse/Serial Int Mapping	*/
/*0x3030*/	u32	imap_flpy,  _uif;	/* Floppy Int Mapping		*/
/*0x3038*/	u32	imap_shw,   _uishw;	/* Spare HW Int Mapping		*/
/*0x3040*/	u32	imap_kbd,   _uikbd;	/* Kbd Only Int Mapping		*/
/*0x3048*/	u32	imap_ms,    _uims;	/* Mouse Only Int Mapping	*/
/*0x3050*/	u32	imap_ser,   _uiser;	/* Serial Only Int Mapping	*/
/*0x3058*/	u64	_imap_unused;
/*0x3060*/	u32	imap_tim0,  _uit0;	/* Timer 0 Int Mapping		*/
/*0x3068*/	u32	imap_tim1,  _uit1;	/* Timer 1 Int Mapping		*/
/*0x3070*/	u32	imap_ue,    _uiue;	/* UE Int Mapping		*/
/*0x3078*/	u32	imap_ce,    _uice;	/* CE Int Mapping		*/
/*0x3080*/	u32	imap_sberr, _uisbe;	/* SBUS Err Int Mapping		*/
/*0x3088*/	u32	imap_pmgmt, _uipm;	/* Power Mgmt Int Mapping	*/
/*0x3090*/	u32	imap_gfx,   _uigfx;	/* OB Graphics Int Mapping	*/
/*0x3098*/	u32	imap_eupa,  _uieupa;	/* UPA Expansion Int Mapping	*/

		u64	__pad6[0x6c];

		/* Interrupt Clear Registers */
/*0x3400*/	u64	iclr_unused0;
/*0x3408*/	u32	iclr_slot0, _ucs0;
		u64	__pad7[0x7];
/*0x3448*/	u32	iclr_slot1, _ucs1;
		u64	__pad8[0x7];
/*0x3488*/	u32	iclr_slot2, _ucs2;
		u64	__pad9[0x7];
/*0x34c8*/	u32	iclr_slot3, _ucs3;
		u64	__pad10[0x66];
/*0x3800*/	u32	iclr_scsi,  _ucscsi;
/*0x3808*/	u32	iclr_eth,   _uceth;
/*0x3810*/	u32	iclr_bpp,   _ucbpp;
/*0x3818*/	u32	iclr_audio, _ucaudio;
/*0x3820*/	u32	iclr_pfail, _ucpfail;
/*0x3828*/	u32	iclr_kms,   _uckms;
/*0x3830*/	u32	iclr_flpt,  _ucflpy;
/*0x3838*/	u32	iclr_shw,   _ucshw;
/*0x3840*/	u32	iclr_kbd,   _uckbd;
/*0x3848*/	u32	iclr_ms,    _ucms;
/*0x3850*/	u32	iclr_ser,   _ucser;
/*0x3858*/	u64	iclr_unused1;
/*0x3860*/	u32	iclr_tim0,  _uctim0;
/*0x3868*/	u32	iclr_tim1,  _uctim1;
/*0x3870*/	u32	iclr_ue,    _ucue;
/*0x3878*/	u32	iclr_ce,    _ucce;
/*0x3880*/	u32	iclr_serr,  _ucserr;
/*0x3888*/	u32	iclr_pmgmt, _ucpmgmt;

		u64	__pad11[0x6e];

		/* Counters/Timers */
/*0x3c00*/	u32	tim0_cnt, _tim0_u0;
/*0x3c08*/	u32	tim0_lim, _tim0_u1;
/*0x3c10*/	u32	tim1_cnt, _tim1_u0;
/*0x3c18*/	u32	tim1_lim, _tim1_u1;

		u64	__pad12[0x7c];

		/* DMA Scoreboard Diagnostic Registers */
/*0x4000*/	u64	dscore_reg0;	/* DMA Scoreboard Diag Reg 0		*/
/*0x4008*/	u64	dscore_reg1;	/* DMA Scoreboard Diag Reg 1		*/

		u64	__pad13[0x7e];

		/* SBUS IOMMU Diagnostic Registers */
/*0x4400*/	u64	sbus_vdiag;	/* SBUS VADDR Diagnostic Register	*/
/*0x4408*/	u64	sbus_tcompare;	/* SBUS IOMMU TLB Tag Compare		*/

		u64	__pad14[0x1e];

		/* More IOMMU diagnostic things */
/*0x4500*/	u64	iommu_lru[16];	/* IOMMU LRU Queue Diagnostic Access	*/
/*0x4580*/	u64	iommu_tag[16];	/* IOMMU TLB Tag Diagnostic Access	*/
/*0x4600*/	u64	iommu_data[32];	/* IOMMU TLB Data RAM Diagnostic Access	*/

		/* Interrupt State Diagnostics */
/*0x4800*/	u64	sbus_istate;
/*0x4808*/	u64	obio_istate;

		u64	__pad15[0xfe];

		/* Streaming Buffer Diagnostic Area */
/*0x5000*/	u64	sbuf_data[128];	/* StrBuffer Data Ram Diagnostic	*/
/*0x5400*/	u64	sbuf_errs[128];	/* StrBuffer Error Status Diagnostics	*/
/*0x5800*/	u64	sbuf_ptag[16];	/* StrBuffer Page Tag Diagnostics	*/
/*0x5880*/	u64	_unusedXXX[16];
/*0x5900*/	u64	sbuf_ltag[16];	/* StrBuffer Line Tag Diagnostics	*/
};

/* SYSIO UPA Port ID */
#define SYSIO_UPPID_FESC	0xff00000000000000 /* FCode escape, 0xfc           */
#define SYSIO_UPPID_RESV1	0x00fffff800000000 /* Reserved                     */
#define SYSIO_UPPID_ENV		0x0000000400000000 /* Cannot generate ECC          */
#define SYSIO_UPPID_ORD		0x0000000200000000 /* One Outstanding Read         */
#define SYSIO_UPPID_RESV2	0x0000000180000000 /* Reserved                     */
#define SYSIO_UPPID_PDQ		0x000000007e000000 /* Data Queue size              */
#define SYSIO_UPPID_PRQ		0x0000000001e00000 /* Request Queue size           */
#define SYSIO_UPPID_UCAP	0x00000000001f0000 /* UPA Capabilities             */
#define SYSIO_UPPID_JEDEC	0x000000000000ffff /* JEDEC ID for SYSIO           */

/* SYSIO UPA Configuration Register */
#define SYSIO_UPCFG_RESV	0xffffffffffffff00 /* Reserved                     */
#define SYSIO_UPCFG_SCIQ1	0x00000000000000f0 /* Unused, always zero          */
#define SYSIO_UPCFG_SCIQ2	0x000000000000000f /* Requests Queue size (0x2)    */

/* SYSIO Control Register */
#define SYSIO_CONTROL_IMPL	0xf000000000000000 /* Implementation of this SYSIO */
#define SYSIO_CONTROL_VER	0x0f00000000000000 /* Version of this SYSIO        */
#define SYSIO_CONTROL_MID	0x00f8000000000000 /* UPA Module ID of SYSIO       */
#define SYSIO_CONTROL_IGN	0x0007c00000000000 /* Interrupt Group Number       */
#define SYSIO_CONTROL_RESV      0x00003ffffffffff0 /* Reserved                     */
#define SYSIO_CONTROL_APCKEN	0x0000000000000008 /* Address Parity Check Enable  */
#define SYSIO_CONTROL_APERR	0x0000000000000004 /* Incoming System Addr Parerr  */
#define SYSIO_CONTROL_IAP	0x0000000000000002 /* Invert UPA Parity            */
#define SYSIO_CONTROL_MODE	0x0000000000000001 /* SYSIO clock mode             */

/* SYSIO ECC Control Register */
#define SYSIO_ECNTRL_ECCEN	0x8000000000000000 /* Enable ECC Checking          */
#define SYSIO_ECNTRL_UEEN	0x4000000000000000 /* Enable UE Interrupts         */
#define SYSIO_ECNTRL_CEEN	0x2000000000000000 /* Enable CE Interrupts         */

/* Uncorrectable Error AFSR, AFAR holds low 40bits of faulting physical address. */
#define SYSIO_UEAFSR_PPIO	0x8000000000000000 /* Primary PIO is cause         */
#define SYSIO_UEAFSR_PDRD	0x4000000000000000 /* Primary DVMA read is cause   */
#define SYSIO_UEAFSR_PDWR	0x2000000000000000 /* Primary DVMA write is cause  */
#define SYSIO_UEAFSR_SPIO	0x1000000000000000 /* Secondary PIO is cause       */
#define SYSIO_UEAFSR_SDRD	0x0800000000000000 /* Secondary DVMA read is cause */
#define SYSIO_UEAFSR_SDWR	0x0400000000000000 /* Secondary DVMA write is cause*/
#define SYSIO_UEAFSR_RESV1	0x03ff000000000000 /* Reserved                     */
#define SYSIO_UEAFSR_DOFF	0x0000e00000000000 /* Doubleword Offset            */
#define SYSIO_UEAFSR_SIZE	0x00001c0000000000 /* Bad transfer size is 2**SIZE */
#define SYSIO_UEAFSR_MID	0x000003e000000000 /* UPA MID causing the fault    */
#define SYSIO_UEAFSR_RESV2	0x0000001fffffffff /* Reserved                     */

/* Correctable Error AFSR, AFAR holds low 40bits of faulting physical address. */
#define SYSIO_CEAFSR_PPIO	0x8000000000000000 /* Primary PIO is cause         */
#define SYSIO_CEAFSR_PDRD	0x4000000000000000 /* Primary DVMA read is cause   */
#define SYSIO_CEAFSR_PDWR	0x2000000000000000 /* Primary DVMA write is cause  */
#define SYSIO_CEAFSR_SPIO	0x1000000000000000 /* Secondary PIO is cause       */
#define SYSIO_CEAFSR_SDRD	0x0800000000000000 /* Secondary DVMA read is cause */
#define SYSIO_CEAFSR_SDWR	0x0400000000000000 /* Secondary DVMA write is cause*/
#define SYSIO_CEAFSR_RESV1	0x0300000000000000 /* Reserved                     */
#define SYSIO_CEAFSR_ESYND	0x00ff000000000000 /* Syndrome Bits                */
#define SYSIO_CEAFSR_DOFF	0x0000e00000000000 /* Double Offset                */
#define SYSIO_CEAFSR_SIZE	0x00001c0000000000 /* Bad transfer size is 2**SIZE */
#define SYSIO_CEAFSR_MID	0x000003e000000000 /* UPA MID causing the fault    */
#define SYSIO_CEAFSR_RESV2	0x0000001fffffffff /* Reserved                     */

/* DMA Scoreboard Diagnostic Register(s) */
#define SYSIO_DSCORE_VALID	0x0040000000000000 /* Entry is valid               */
#define SYSIO_DSCORE_C		0x0020000000000000 /* Transaction cacheable        */
#define SYSIO_DSCORE_READ	0x0010000000000000 /* Transaction was a read       */
#define SYSIO_DSCORE_TAG	0x000f000000000000 /* Transaction ID               */
#define SYSIO_DSCORE_ADDR	0x0000ffffffffff80 /* Transaction PADDR            */
#define SYSIO_DSCORE_SIZE	0x0000000000000030 /* Transaction size             */
#define SYSIO_DSCORE_SRC	0x000000000000000f /* Transaction source           */

/* SYSIO SBUS Control Register */
#define SYSIO_SBCNTRL_IMPL	0xf000000000000000 /* Implementation               */
#define SYSIO_SBCNTRL_REV	0x0f00000000000000 /* Revision                     */
#define SYSIO_SBCNTRL_RESV1	0x00c0000000000000 /* Reserved                     */
#define SYSIO_SBCNTRL_DPERR	0x003f000000000000 /* DMA Write Parity Error       */
#define SYSIO_SBCNTRL_RESV2	0x0000800000000000 /* Reserved                     */
#define SYSIO_SBCNTRL_PPERR	0x00007f0000000000 /* PIO Load Parity Error        */
#define SYSIO_SBCNTRL_RESV	0x000000fffffffc00 /* Reserved                     */
#define SYSIO_SBCNTRL_WEN	0x0000000000000200 /* Power Mgmt Wake Enable       */
#define SYSIO_SBCNTRL_EEN	0x0000000000000100 /* SBUS Error Interrupt Enable  */
#define SYSIO_SBCNTRL_RESV3	0x00000000000000c0 /* Reserved                     */
#define SYSIO_SBCNTRL_AEN	0x000000000000003f /* SBUS DVMA Arbitration Enable */

/* SYSIO SBUS AFSR, AFAR holds low 40 bits of physical address causing the fault. */
#define SYSIO_SBAFSR_PLE	0x8000000000000000 /* Primary Late PIO Error       */
#define SYSIO_SBAFSR_PTO	0x4000000000000000 /* Primary SBUS Timeout         */
#define SYSIO_SBAFSR_PBERR	0x2000000000000000 /* Primary SBUS Error ACK       */
#define SYSIO_SBAFSR_SLE	0x1000000000000000 /* Secondary Late PIO Error     */
#define SYSIO_SBAFSR_STO	0x0800000000000000 /* Secondary SBUS Timeout       */
#define SYSIO_SBAFSR_SBERR	0x0400000000000000 /* Secondary SBUS Error ACK     */
#define SYSIO_SBAFSR_RESV1	0x03ff000000000000 /* Reserved                     */
#define SYSIO_SBAFSR_RD		0x0000800000000000 /* Primary was late PIO read    */
#define SYSIO_SBAFSR_RESV2	0x0000600000000000 /* Reserved                     */
#define SYSIO_SBAFSR_SIZE	0x00001c0000000000 /* Size of transfer             */
#define SYSIO_SBAFSR_MID	0x000003e000000000 /* MID causing the error        */
#define SYSIO_SBAFSR_RESV3	0x0000001fffffffff /* Reserved                     */

/* SYSIO SBUS Slot Configuration Register */
#define SYSIO_SBSCFG_RESV1	0xfffffffff8000000 /* Reserved                     */
#define SYSIO_SBSCFG_SADDR	0x0000000007ff0000 /* Segment Address (PA[40:30])  */
#define SYSIO_SBSCFG_CP		0x0000000000008000 /* Bypasses are cacheable       */
#define SYSIO_SBSCFG_ETM	0x0000000000004000 /* Ext Transfer Mode supported  */
#define SYSIO_SBSCFG_PE		0x0000000000002000 /* SBUS Parity Checking Enable  */
#define SYSIO_SBSCFG_RESV2	0x0000000000001fe0 /* Reserved                     */
#define SYSIO_SBSCFG_BA64	0x0000000000000010 /* 64-byte bursts supported     */
#define SYSIO_SBSCFG_BA32	0x0000000000000008 /* 32-byte bursts supported     */
#define SYSIO_SBSCFG_BA16	0x0000000000000004 /* 16-byte bursts supported     */
#define SYSIO_SBSCFG_BA8	0x0000000000000002 /* 8-byte bursts supported      */
#define SYSIO_SBSCFG_BY		0x0000000000000001 /* IOMMU Bypass Enable          */

/* IOMMU things defined fully in asm-sparc64/iommu.h */

/* Streaming Buffer Control Register */
#define SYSIO_SBUFCTRL_IMPL	0xf000000000000000 /* Implementation               */
#define SYSIO_SBUFCTRL_REV	0x0f00000000000000 /* Revision                     */
#define SYSIO_SBUFCTRL_DE	0x0000000000000002 /* Diag Mode Enable             */
#define SYSIO_SBUFCTRL_SB_EN	0x0000000000000001 /* Streaming Buffer Enable      */

/* Streaming Buffer Page Invalidate/Flush Register */
#define SYSIO_SBUFFLUSH_ADDR	0x00000000ffffe000 /* DVMA Page to be flushed      */
#define SYSIO_SBUFFLUSH_RESV    0x0000000000001fff /* Ignored bits                 */

/* Streaming Buffer Flush Synchronization Register */
#define SYSIO_SBUFSYNC_ADDR	0x000001fffffffffc /* Physical address to update   */
#define SYSIO_SBUFSYNC_RESV	0x0000000000000003 /* Ignored bits                 */

/* SYSIO Interrupt mapping register(s). */
#define SYSIO_IMAP_VALID	0x80000000	/* This enables delivery.	   */
#define SYSIO_IMAP_TID		0x7c000000	/* Target ID (MID to send it to)   */
#define SYSIO_IMAP_RESV		0x03fff800	/* Reserved.			   */
#define SYSIO_IMAP_IGN		0x000007c0	/* Interrupt Group Number.	   */
#define SYSIO_IMAP_INO		0x0000003f	/* Interrupt Number Offset.	   */
#define SYSIO_IMAP_INR		0x000007ff	/* Interrupt # (Gfx/UPA_slave only)*/

/* SYSIO Interrupt clear pseudo register(s). */
#define SYSIO_ICLR_IDLE		0x00000000	/* Transition to idle state.	   */
#define SYSIO_ICLR_TRANSMIT	0x00000001	/* Transition to transmit state.   */
#define SYSIO_ICLR_RESV		0x00000002	/* Reserved.			   */
#define SYSIO_ICLR_PENDING	0x00000003	/* Transition to pending state.	   */

/* SYSIO Interrupt Retry Timer register. */
#define SYSIO_IRETRY_LIMIT	0x000000ff	/* The retry interval.		   */

/* SYSIO Interrupt State registers. XXX fields to be documented later */

/* SYSIO Counter register. XXX fields to be documented later */

/* SYSIO Limit register. XXX fields to be documented later */

/* SYSIO Performance Monitor Control register. XXX fields to be documented later */

/* SYSIO Performance Monitor Counter register. XXX fields to be documented later */

#endif /* !(__SPARC64_SYSIO_H) */

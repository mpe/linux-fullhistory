/* $Id: psycho.h,v 1.3 1998/03/15 13:24:28 ecd Exp $
 * psycho.h: UltraSparc AX specific PCI definitions.
 *
 * Copyright (C) 1997 Eddie C. Dost (ecd@skynet.be)
 */

#ifndef __SPARC64_PSYCHO_H
#define __SPARC64_PSYCHO_H

#include <linux/types.h>

/* Ultra AX PSYCHO Register Set, one per controller probed. */
struct psycho_regs {
/*0x0000*/	u64	upa_id;		/* PSYCHO UPA Port ID Register	*/
/*0x0008*/	u64	upa_cfg;	/* PSYCHO UPA Config Register	*/
/*0x0010*/	u64	control;	/* PSYCHO Control Register	*/
/*0x0018*/	u64	__pad0;
/*0x0020*/	u64	ecc_control;	/* ECC Control Register		*/
/*0x0028*/	u64	__pad1;

		/* Uncorrectable Error Fault Registers */
/*0x0030*/	u64	ue_afsr;	/* UE Async Fault Status	*/
/*0x0038*/	u64	ue_afar;	/* UE Async Fault Address	*/

		/* Correctable Error Fault Registers */
/*0x0040*/	u64	ce_afsr;	/* CE Async Fault Status	*/
/*0x0048*/	u64	ce_afar;	/* CE Async Fault Address	*/

		u64	__pad2[0x16];

		/* Performance Monitoring Registers */
/*0x0100*/	u64	pmon_control;
/*0x0108*/	u64	pmon_counter;

		u64	__pad3[0x1e];

		/* PCI Bus IOMMU lives here */
/*0x0200*/	u64	iommu_control;	/* IOMMU Control		*/
/*0x0208*/	u64	iommu_tsbbase;	/* IOMMU TSB Base		*/
/*0x0210*/	u64	iommu_flush;	/* IOMMU Flush Register		*/

		u64	__pad4[0x13d];

		/* Interrupt mapping/control registers */
/*0x0c00*/	u64	imap_a_slot0;	/* PCI A Slot 0 Int Mapping	*/
/*0x0c08*/	u64	imap_a_slot1;	/* PCI A Slot 1 Int Mapping	*/
/*0x0c10*/	u64	imap_a_slot2;	/* PCI A Slot 2 Int Mapping (IIi only)*/
/*0x0c18*/	u64	imap_a_slot3;	/* PCI A Slot 3 Int Mapping (IIi only)*/

/*0x0c20*/	u64	imap_b_slot0;	/* PCI B Slot 0 Int Mapping	*/
/*0x0c28*/	u64	imap_b_slot1;	/* PCI B Slot 1 Int Mapping	*/
/*0x0c30*/	u64	imap_b_slot2;	/* PCI B Slot 2 Int Mapping	*/
/*0x0c38*/	u64	imap_b_slot3;	/* PCI B Slot 3 Int Mapping	*/

		u64	__pad6[0x78];

/*0x1000*/	u64	imap_scsi;	/* SCSI Int Mapping		*/
/*0x1008*/	u64	imap_eth;	/* Ethernet Int Mapping		*/
/*0x1010*/	u64	imap_bpp;	/* Parallel Port Int Mapping	*/
/*0x1018*/	u64	imap_au_rec;	/* Audio Record Int Mapping	*/
/*0x1020*/	u64	imap_au_play;	/* Audio Playback Int Mapping	*/
/*0x1028*/	u64	imap_pfail;	/* Power Fail Int Mapping	*/
/*0x1030*/	u64	imap_kms;	/* Kbd/Mouse/Ser Int Mapping	*/
/*0x1038*/	u64	imap_flpy;	/* Floppy Int Mapping		*/
/*0x1040*/	u64	imap_shw;	/* Spare HW Int Mapping		*/
/*0x1048*/	u64	imap_kbd;	/* Kbd Only Int Mapping		*/
/*0x1050*/	u64	imap_ms;	/* Mouse Only Int Mapping	*/
/*0x1058*/	u64	imap_ser;	/* Serial Only Int Mapping	*/
/*0x1060*/	u64	imap_tim0;	/* Timer 0 Int Mapping		*/
/*0x1068*/	u64	imap_tim1;	/* Timer 1 Int Mapping		*/
/*0x1070*/	u64	imap_ue;	/* UE Int Mapping		*/
/*0x1078*/	u64	imap_ce;	/* CE Int Mapping		*/
/*0x1080*/	u64	imap_a_err;	/* PCI A Err Int Mapping	*/
/*0x1088*/	u64	imap_b_err;	/* PCI B Err Int Mapping	*/
/*0x1090*/	u64	imap_pmgmt;	/* Power Mgmt Int Mapping	*/
/*0x1098*/	u64	imap_gfx;	/* OB Graphics Int Mapping	*/
/*0x10a0*/	u64	imap_eupa;	/* UPA Expansion Int Mapping	*/

		u64	__pad7[0x6b];

		/* Interrupt Clear Registers */
/*0x1400*/	u64	iclr_a_slot0[4];	/* PCI A Slot 0 Clear Int Reg */
/*0x1420*/	u64	iclr_a_slot1[4];	/* PCI A Slot 1 Clear Int Reg */
/*0x1440*/	u64	iclr_a_slot2[4];	/* PCI A Slot 2 Clear Int Reg */
/*0x1460*/	u64	iclr_a_slot3[4];	/* PCI A Slot 3 Clear Int Reg */

/*0x1480*/	u64	iclr_b_slot0[4];	/* PCI B Slot 0 Clear Int Reg */
/*0x14a0*/	u64	iclr_b_slot1[4];	/* PCI B Slot 1 Clear Int Reg */
/*0x14c0*/	u64	iclr_b_slot2[4];	/* PCI B Slot 2 Clear Int Reg */
/*0x14e0*/	u64	iclr_b_slot3[4];	/* PCI B Slot 3 Clear Int Reg */

		u64	__pad9[0x60];

/*0x1800*/	u64	iclr_scsi;
/*0x1808*/	u64	iclr_eth;
/*0x1810*/	u64	iclr_bpp;
/*0x1818*/	u64	iclr_au_rec;
/*0x1820*/	u64	iclr_au_play;
/*0x1828*/	u64	iclr_pfail;
/*0x1830*/	u64	iclr_kms;
/*0x1838*/	u64	iclr_flpy;
/*0x1840*/	u64	iclr_shw;
/*0x1848*/	u64	iclr_kbd;
/*0x1850*/	u64	iclr_ms;
/*0x1858*/	u64	iclr_ser;
/*0x1860*/	u64	iclr_tim0;
/*0x1868*/	u64	iclr_tim1;
/*0x1870*/	u64	iclr_ue;
/*0x1878*/	u64	iclr_ce;
/*0x1880*/	u64	iclr_a_err;
/*0x1888*/	u64	iclr_b_err;
/*0x1890*/	u64	iclr_pmgmt;

		u64	__pad10[0x2d];

		/* Interrupt Retry Timer. */
/*0x1a00*/	u64	irq_retry;

		u64	__pad11[0x3f];

		/* Counters/Timers */
/*0x1c00*/	u64	tim0_cnt;
/*0x1c08*/	u64	tim0_lim;
/*0x1c10*/	u64	tim1_cnt;
/*0x1c18*/	u64	tim1_lim;
/*0x1c20*/	u64	pci_dma_wsync;	/* PCI DMA Write Sync Register (IIi) */

		u64	__pad12[0x7b];

		/* PCI Bus A Registers */
/*0x2000*/	u64	pci_a_control;	/* PCI Bus A Control Register	*/
/*0x2008*/	u64	__pad13;
/*0x2010*/	u64	pci_a_afsr;	/* PCI Bus A Async Fault Status	*/
/*0x2018*/	u64	pci_a_afar;	/* PCI Bus A Async Fault Address*/
/*0x2020*/	u64	pci_a_diag;	/* PCI Bus A Diag Register	*/
/*0x2028*/	u64	pci_tasr;	/* PCI Target Address Space Reg (IIi) */

		u64	__pad14[0xfa];

		/* PCI Bus A/IOMMU Streaming Buffer Registers */
/*0x2800*/	u64	sbuf_a_control;	/* StrBuffer Control		*/
/*0x2808*/	u64	sbuf_a_pflush;	/* StrBuffer Page Flush		*/
/*0x2810*/	u64	sbuf_a_fsync;	/* StrBuffer Flush Sync Reg	*/

		u64	__pad15[0x2fd];

		/* PCI Bus B Registers */
/*0x4000*/	u64	pci_b_control;	/* PCI Bus B Control Register	*/
/*0x4008*/	u64	__pad16;
/*0x4010*/	u64	pci_b_afsr;	/* PCI Bus B Async Fault Status	*/
/*0x4018*/	u64	pci_b_afar;	/* PCI Bus B Async Fault Address*/
/*0x4020*/	u64	pci_b_diag;	/* PCI Bus B Diag Register	*/

		u64	__pad17[0x7b];

		/* IOMMU diagnostic things */
/*0x4400*/	u64	iommu_vdiag;	/* VADDR Diagnostic Register	*/
/*0x4408*/	u64	iommu_tcompare;	/* IOMMU TLB Tag Compare	*/

		u64	__pad18[0x7e];

		/* PCI Bus B/IOMMU Streaming Buffer Registers */
/*0x4800*/	u64	sbuf_b_control;	/* StrBuffer Control		*/
/*0x4808*/	u64	sbuf_b_pflush;	/* StrBuffer Page Flush		*/
/*0x4810*/	u64	sbuf_b_fsync;	/* StrBuffer Flush Sync Reg	*/

		u64	__pad19[0xafd];

		/* DMA Scoreboard Diagnostic Registers */
/*0xa000*/	u64	dscore_reg0;	/* DMA Scoreboard Diag Reg 0	*/
/*0xa008*/	u64	dscore_reg1;	/* DMA Scoreboard Diag Reg 1	*/

		u64	__pad20[0x9e];

		/* More IOMMU diagnostic things */
/*0xa500*/	u64	iommu_lru[16];	/* IOMMU LRU Queue Diag		*/
/*0xa580*/	u64	iommu_tag[16];	/* IOMMU TLB Tag Diag		*/
/*0xa600*/	u64	iommu_data[16];	/* IOMMU TLB Data RAM Diag	*/

		u64	__pad21[0x30];

		/* Interrupt State Diagnostics */
/*0xa800*/	u64	pci_istate;
/*0xa808*/	u64	obio_istate;

		u64	__pad22[0xfe];

		/* Streaming Buffer A Diagnostic Area */
/*0xb000*/	u64	sbuf_a_data[128]; /* StrBuffer Data Ram Diag	*/
/*0xb400*/	u64	sbuf_a_errs[128]; /* StrBuffer Error Status Diag*/
/*0xb800*/	u64	sbuf_a_ptag[16]; /* StrBuffer Page Tag Diag	*/
/*0xb880*/	u64	__pad23[16];
/*0xb900*/	u64	sbuf_a_ltag[16]; /* StrBuffer Line Tag Diag	*/

		u64	__pad24[0xd0];

		/* Streaming Buffer B Diagnostic Area */
/*0xc000*/	u64	sbuf_b_data[128]; /* StrBuffer Data Ram Diag	*/
/*0xc400*/	u64	sbuf_b_errs[128]; /* StrBuffer Error Status Diag*/
/*0xc800*/	u64	sbuf_b_ptag[16]; /* StrBuffer Page Tag Diag	*/
/*0xc880*/	u64	__pad25[16];
/*0xc900*/	u64	sbuf_b_ltag[16]; /* StrBuffer Line Tag Diag	*/
};

/* PSYCHO UPA Port ID */
#define PSYCHO_UPPID_FESC	0xff00000000000000 /* FCode escape, 0xfc           */
#define PSYCHO_UPPID_RESV1	0x00fffff800000000 /* Reserved                     */
#define PSYCHO_UPPID_ENV	0x0000000400000000 /* Cannot generate ECC          */
#define PSYCHO_UPPID_ORD	0x0000000200000000 /* One Outstanding Read         */
#define PSYCHO_UPPID_RESV2	0x0000000180000000 /* Reserved                     */
#define PSYCHO_UPPID_PDQ	0x000000007e000000 /* Data Queue size              */
#define PSYCHO_UPPID_PRQ	0x0000000001e00000 /* Request Queue size           */
#define PSYCHO_UPPID_UCAP	0x00000000001f0000 /* UPA Capabilities             */
#define PSYCHO_UPPID_JEDEC	0x000000000000ffff /* JEDEC ID for PSYCHO          */

/* PSYCHO UPA Configuration Register */
#define PSYCHO_UPCFG_RESV	0xffffffffffffff00 /* Reserved                     */
#define PSYCHO_UPCFG_SCIQ1	0x00000000000000f0 /* Unused, always zero          */
#define PSYCHO_UPCFG_SCIQ2	0x000000000000000f /* Requests Queue size 0x2 	   */

/* PSYCHO Control Register */
#define PSYCHO_CONTROL_IMPL	0xf000000000000000 /* Implementation of this PSYCHO*/
#define PSYCHO_CONTROL_VER	0x0f00000000000000 /* Version of this PSYCHO       */
#define PSYCHO_CONTROL_MID	0x00f8000000000000 /* UPA Module ID of PSYCHO      */
#define PSYCHO_CONTROL_IGN	0x0007c00000000000 /* Interrupt Group Number       */
#define PSYCHO_CONTROL_RESV     0x00003ffffffffff0 /* Reserved                     */
#define PSYCHO_CONTROL_APCKEN	0x0000000000000008 /* Address Parity Check Enable  */
#define PSYCHO_CONTROL_APERR	0x0000000000000004 /* Incoming System Addr Parerr  */
#define PSYCHO_CONTROL_IAP	0x0000000000000002 /* Invert UPA Parity            */
#define PSYCHO_CONTROL_MODE	0x0000000000000001 /* PSYCHO clock mode            */

/* PSYCHO ECC Control Register */
#define PSYCHO_ECNTRL_ECCEN	0x8000000000000000 /* Enable ECC Checking	   */
#define PSYCHO_ECNTRL_UEEN	0x4000000000000000 /* Enable UE Interrupts         */
#define PSYCHO_ECNTRL_CEEN	0x2000000000000000 /* Enable CE Interrupts         */

/* Uncorrectable Error AFSR, AFAR holds low 40bits of faulting physical address. */
#define PSYCHO_UEAFSR_PPIO	0x8000000000000000 /* Primary PIO is cause         */
#define PSYCHO_UEAFSR_PDRD	0x4000000000000000 /* Primary DVMA read is cause   */
#define PSYCHO_UEAFSR_PDWR	0x2000000000000000 /* Primary DVMA write is cause  */
#define PSYCHO_UEAFSR_SPIO	0x1000000000000000 /* Secondary PIO is cause       */
#define PSYCHO_UEAFSR_SDRD	0x0800000000000000 /* Secondary DVMA read is cause */
#define PSYCHO_UEAFSR_SDWR	0x0400000000000000 /* Secondary DVMA write is cause*/
#define PSYCHO_UEAFSR_RESV1	0x03ff000000000000 /* Reserved                     */
#define PSYCHO_UEAFSR_BMSK	0x0000ffff00000000 /* Bytemask of failed transfer  */
#define PSYCHO_UEAFSR_DOFF	0x00000000e0000000 /* Doubleword Offset            */
#define PSYCHO_UEAFSR_MID	0x000000001f000000 /* UPA MID causing the fault    */
#define PSYCHO_UEAFSR_BLK	0x0000000000800000 /* Trans was block operation    */
#define PSYCHO_UEAFSR_RESV2	0x00000000007fffff /* Reserved                     */

/* Correctable Error AFSR, AFAR holds low 40bits of faulting physical address. */
#define PSYCHO_CEAFSR_PPIO	0x8000000000000000 /* Primary PIO is cause         */
#define PSYCHO_CEAFSR_PDRD	0x4000000000000000 /* Primary DVMA read is cause   */
#define PSYCHO_CEAFSR_PDWR	0x2000000000000000 /* Primary DVMA write is cause  */
#define PSYCHO_CEAFSR_SPIO	0x1000000000000000 /* Secondary PIO is cause       */
#define PSYCHO_CEAFSR_SDRD	0x0800000000000000 /* Secondary DVMA read is cause */
#define PSYCHO_CEAFSR_SDWR	0x0400000000000000 /* Secondary DVMA write is cause*/
#define PSYCHO_CEAFSR_RESV1	0x0300000000000000 /* Reserved                     */
#define PSYCHO_CEAFSR_ESYND	0x00ff000000000000 /* Syndrome Bits                */
#define PSYCHO_UEAFSR_SIZE	0x0000ffff00000000 /* Bytemask of failed transfer  */
#define PSYCHO_CEAFSR_DOFF	0x00000000e0000000 /* Double Offset                */
#define PSYCHO_CEAFSR_MID	0x000000001f000000 /* UPA MID causing the fault    */
#define PSYCHO_CEAFSR_BLK	0x0000000000800000 /* Trans was block operation    */
#define PSYCHO_CEAFSR_RESV2	0x00000000007fffff /* Reserved                     */

/* DMA Scoreboard Diagnostic Register(s) */
#define PSYCHO_DSCORE_VALID	0x8000000000000000 /* Entry is valid               */
#define PSYCHO_DSCORE_C		0x4000000000000000 /* Transaction cacheable        */
#define PSYCHO_DSCORE_READ	0x2000000000000000 /* Transaction was a read       */
#define PSYCHO_DSCORE_TAG	0x1f00000000000000 /* Transaction ID               */
#define PSYCHO_DSCORE_ADDR	0x00fffffffff80000 /* Transaction PADDR            */
#define PSYCHO_DSCORE_BMSK	0x000000000007fff8 /* Bytemask of pending transfer */
#define PSYCHO_DSCORE_SRC	0x0000000000000007 /* Transaction source           */

/* PSYCHO PCI Control Register */
#define PSYCHO_PCICTRL_RESV1	0xfffffff000000000 /* Reserved                     */
#define PSYCHO_PCICTRL_SBH_ERR	0x0000000800000000 /* Streaming byte hole error    */
#define PSYCHO_PCICTRL_SERR	0x0000000400000000 /* SERR signal asserted         */
#define PSYCHO_PCICTRL_SPEED	0x0000000200000000 /* PCI speed (1 is U2P clock)   */
#define PSYCHO_PCICTRL_RESV2	0x00000001ffc00000 /* Reserved                     */
#define PSYCHO_PCICTRL_ARB_PARK	0x0000000000200000 /* PCI arbitration parking      */
#define PSYCHO_PCICTRL_RESV3	0x00000000001ff800 /* Reserved                     */
#define PSYCHO_PCICTRL_SBH_INT	0x0000000000000400 /* Streaming byte hole int enab */
#define PSYCHO_PCICTRL_WEN	0x0000000000000200 /* Power Mgmt Wake Enable       */
#define PSYCHO_PCICTRL_EEN	0x0000000000000100 /* PCI Error Interrupt Enable   */
#define PSYCHO_PCICTRL_RESV4	0x00000000000000c0 /* Reserved                     */
#define PSYCHO_PCICTRL_AEN	0x000000000000003f /* PCI DVMA Arbitration Enable  */

/* PSYCHO PCI AFSR, AFAR holds low 40 bits of physical address causing the fault. */
#define PSYCHO_PCIAFSR_PMA	0x8000000000000000 /* Primary Master Abort Error   */
#define PSYCHO_PCIAFSR_PTA	0x4000000000000000 /* Primary Target Abort Error   */
#define PSYCHO_PCIAFSR_PRTRY	0x2000000000000000 /* Primary Excessive Retries    */
#define PSYCHO_PCIAFSR_PPERR	0x1000000000000000 /* Primary Parity Error         */
#define PSYCHO_PCIAFSR_SMA	0x0800000000000000 /* Secondary Master Abort Error */
#define PSYCHO_PCIAFSR_STA	0x0400000000000000 /* Secondary Target Abort Error */
#define PSYCHO_PCIAFSR_SRTRY	0x0200000000000000 /* Secondary Excessive Retries  */
#define PSYCHO_PCIAFSR_SPERR	0x0100000000000000 /* Secondary Parity Error       */
#define PSYCHO_PCIAFSR_RESV1	0x00ff000000000000 /* Reserved                     */
#define PSYCHO_PCIAFSR_SIZE	0x0000ffff00000000 /* Bytemask of failed transfer  */
#define PSYCHO_PCIAFSR_BLK	0x0000000080000000 /* Trans was block operation    */
#define PSYCHO_PCIAFSR_RESV2	0x0000000040000000 /* Reserved                     */
#define PSYCHO_PCIAFSR_MID	0x000000003e000000 /* MID causing the error        */
#define PSYCHO_PCIAFSR_RESV3	0x0000000001ffffff /* Reserved                     */

/* IOMMU things defined fully in asm-sparc64/iommu.h */

/* Streaming Buffer Control Register */
#define PSYCHO_SBUFCTRL_RESV	0xffffffffffffff80 /* Reserved                     */
#define PSYCHO_SBUFCTRL_LRU_LP	0x0000000000000070 /* LRU Lock Pointer             */
#define PSYCHO_SBUFCTRL_LRU_LE	0x0000000000000008 /* LRU Lock Enable              */
#define PSYCHO_SBUFCTRL_RR_DIS	0x0000000000000004 /* Rerun Disable                */
#define PSYCHO_SBUFCTRL_DE	0x0000000000000002 /* Diag Mode Enable             */
#define PSYCHO_SBUFCTRL_SB_EN	0x0000000000000001 /* Streaming Buffer Enable      */

/* Streaming Buffer Page Invalidate/Flush Register */
#define PSYCHO_SBUFFLUSH_ADDR	0x00000000ffffe000 /* DVMA Page to be flushed      */
#define PSYCHO_SBUFFLUSH_RESV	0x0000000000001fff /* Ignored bits                 */

/* Streaming Buffer Flush Synchronization Register */
#define PSYCHO_SBUFSYNC_ADDR	0x000001ffffffffc0 /* Physical address to update   */
#define PSYCHO_SBUFSYNC_RESV	0x000000000000003f /* Ignored bits                 */

/* PSYCHO Interrupt mapping register(s). */
#define PSYCHO_IMAP_RESV1	0xffffffff00000000 /* Reserved                     */
#define PSYCHO_IMAP_VALID	0x0000000080000000 /* This enables delivery.	   */
#define PSYCHO_IMAP_TID		0x000000007c000000 /* Target ID (MID to send it to)*/
#define PSYCHO_IMAP_RESV2	0x0000000003fff800 /* Reserved 			   */
#define PSYCHO_IMAP_IGN		0x00000000000007c0 /* Interrupt Group Number.	   */
#define PSYCHO_IMAP_INO		0x000000000000003f /* Interrupt Number Offset.	   */
#define PSYCHO_IMAP_INR		0x00000000000007ff /* Interrupt # (Gfx/UPA_slave)  */

/* PSYCHO Interrupt clear pseudo register(s). */
#define PSYCHO_ICLR_RESV1 	0xfffffffffffffff0 /* Reserved			   */
#define PSYCHO_ICLR_IDLE	0x0000000000000000 /* Transition to idle state.	   */
#define PSYCHO_ICLR_TRANSMIT	0x0000000000000001 /* Transition to transmit state */
#define PSYCHO_ICLR_RESV2	0x0000000000000002 /* Reserved.			   */
#define PSYCHO_ICLR_PENDING	0x0000000000000003 /* Transition to pending state. */

/* PSYCHO Interrupt Retry Timer register. */
#define PSYCHO_IRETRY_LIMIT	0x00000000000fffff /* The retry interval.	   */

/* PSYCHO Interrupt State registers. XXX fields to be documented later */

/* PSYCHO Counter register. XXX fields to be documented later */

/* PSYCHO Limit register. XXX fields to be documented later */

/* PSYCHO Performance Monitor Control register. XXX fields to be documented later */

/* PSYCHO Performance Monitor Counter register. XXX fields to be documented later */

#endif /* !(__SPARC64_PSYCHO_H) */

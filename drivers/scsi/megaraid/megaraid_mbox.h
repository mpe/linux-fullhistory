/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: megaraid_mbox.h
 */

#ifndef _MEGARAID_H_
#define _MEGARAID_H_


#include "mega_common.h"
#include "mbox_defs.h"
#include "megaraid_ioctl.h"


#define MEGARAID_VERSION	"2.20.4.0"
#define MEGARAID_EXT_VERSION	"(Release Date: Mon Sep 27 22:15:07 EDT 2004)"


/*
 * Define some PCI values here until they are put in the kernel
 */
#define PCI_DEVICE_ID_PERC4_DI_DISCOVERY		0x000E
#define PCI_SUBSYS_ID_PERC4_DI_DISCOVERY		0x0123

#define PCI_DEVICE_ID_PERC4_SC				0x1960
#define PCI_SUBSYS_ID_PERC4_SC				0x0520

#define PCI_DEVICE_ID_PERC4_DC				0x1960
#define PCI_SUBSYS_ID_PERC4_DC				0x0518

#define PCI_DEVICE_ID_PERC4_QC				0x0407
#define PCI_SUBSYS_ID_PERC4_QC				0x0531

#define PCI_DEVICE_ID_PERC4_DI_EVERGLADES		0x000F
#define PCI_SUBSYS_ID_PERC4_DI_EVERGLADES		0x014A

#define PCI_DEVICE_ID_PERC4E_SI_BIGBEND			0x0013
#define PCI_SUBSYS_ID_PERC4E_SI_BIGBEND			0x016c

#define PCI_DEVICE_ID_PERC4E_DI_KOBUK			0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_KOBUK			0x016d

#define PCI_DEVICE_ID_PERC4E_DI_CORVETTE		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_CORVETTE		0x016e

#define PCI_DEVICE_ID_PERC4E_DI_EXPEDITION		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_EXPEDITION		0x016f

#define PCI_DEVICE_ID_PERC4E_DI_GUADALUPE		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_GUADALUPE		0x0170

#define PCI_DEVICE_ID_PERC4E_DC_320_2E			0x0408
#define PCI_SUBSYS_ID_PERC4E_DC_320_2E			0x0002

#define PCI_DEVICE_ID_PERC4E_SC_320_1E			0x0408
#define PCI_SUBSYS_ID_PERC4E_SC_320_1E			0x0001

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_0		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_0		0xA520

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_1		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_1		0x0520

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_2		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_2		0x0518

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_0x		0x0407
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_0x		0x0530

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_2x		0x0407
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_2x		0x0532

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_4x		0x0407
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_4x		0x0531

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_1E		0x0408
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_1E		0x0001

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_2E		0x0408
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_2E		0x0002

#define PCI_DEVICE_ID_MEGARAID_I4_133_RAID		0x1960
#define PCI_SUBSYS_ID_MEGARAID_I4_133_RAID		0x0522

#define PCI_DEVICE_ID_MEGARAID_SATA_150_4		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SATA_150_4		0x4523

#define PCI_DEVICE_ID_MEGARAID_SATA_150_6		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SATA_150_6		0x0523

#define PCI_DEVICE_ID_MEGARAID_SATA_300_4x		0x0409
#define PCI_SUBSYS_ID_MEGARAID_SATA_300_4x		0x3004

#define PCI_DEVICE_ID_MEGARAID_SATA_300_8x		0x0409
#define PCI_SUBSYS_ID_MEGARAID_SATA_300_8x		0x3008

#define PCI_DEVICE_ID_INTEL_RAID_SRCU42X		0x0407
#define PCI_SUBSYS_ID_INTEL_RAID_SRCU42X		0x0532

#define PCI_DEVICE_ID_INTEL_RAID_SRCS16			0x1960
#define PCI_SUBSYS_ID_INTEL_RAID_SRCS16			0x0523

#define PCI_DEVICE_ID_INTEL_RAID_SRCU42E		0x0408
#define PCI_SUBSYS_ID_INTEL_RAID_SRCU42E		0x0002

#define PCI_DEVICE_ID_INTEL_RAID_SRCZCRX		0x0407
#define PCI_SUBSYS_ID_INTEL_RAID_SRCZCRX		0x0530

#define PCI_DEVICE_ID_INTEL_RAID_SRCS28X		0x0409
#define PCI_SUBSYS_ID_INTEL_RAID_SRCS28X		0x3008

#define PCI_DEVICE_ID_INTEL_RAID_SROMBU42E_ALIEF	0x0408
#define PCI_SUBSYS_ID_INTEL_RAID_SROMBU42E_ALIEF	0x3431

#define PCI_DEVICE_ID_INTEL_RAID_SROMBU42E_HARWICH	0x0408
#define PCI_SUBSYS_ID_INTEL_RAID_SROMBU42E_HARWICH	0x3499

#define PCI_DEVICE_ID_INTEL_RAID_SRCU41L_LAKE_SHETEK	0x1960
#define PCI_SUBSYS_ID_INTEL_RAID_SRCU41L_LAKE_SHETEK	0x0520

#define PCI_DEVICE_ID_FSC_MEGARAID_PCI_EXPRESS_ROMB	0x0408
#define PCI_SUBSYS_ID_FSC_MEGARAID_PCI_EXPRESS_ROMB	0x1065

#define PCI_DEVICE_ID_MEGARAID_ACER_ROMB_2E		0x0408
#define PCI_SUBSYS_ID_MEGARAID_ACER_ROMB_2E		0x004D

#define PCI_SUBSYS_ID_PERC3_QC				0x0471
#define PCI_SUBSYS_ID_PERC3_DC				0x0493
#define PCI_SUBSYS_ID_PERC3_SC				0x0475

#ifndef PCI_SUBSYS_ID_FSC
#define PCI_SUBSYS_ID_FSC				0x1734
#endif

#define MBOX_MAX_SCSI_CMDS	128	// number of cmds reserved for kernel
#define MBOX_MAX_USER_CMDS	32	// number of cmds for applications
#define MBOX_DEF_CMD_PER_LUN	64	// default commands per lun
#define MBOX_DEFAULT_SG_SIZE	26	// default sg size supported by all fw
#define MBOX_MAX_SG_SIZE	32	// maximum scatter-gather list size
#define MBOX_MAX_SECTORS	128	// maximum sectors per IO
#define MBOX_TIMEOUT		30	// timeout value for internal cmds
#define MBOX_BUSY_WAIT		10	// max usec to wait for busy mailbox
#define MBOX_RESET_WAIT		180	// wait these many seconds in reset
#define MBOX_RESET_EXT_WAIT	120	// extended wait reset

/*
 * maximum transfer that can happen through the firmware commands issued
 * internnaly from the driver.
 */
#define MBOX_IBUF_SIZE		4096


/**
 * mbox_ccb_t - command control block specific to mailbox based controllers
 * @raw_mbox		: raw mailbox pointer
 * @mbox		: mailbox
 * @mbox64		: extended mailbox
 * @mbox_dma_h		: maibox dma address
 * @sgl64		: 64-bit scatter-gather list
 * @sgl32		: 32-bit scatter-gather list
 * @sgl_dma_h		: dma handle for the scatter-gather list
 * @pthru		: passthru structure
 * @pthru_dma_h		: dma handle for the passthru structure
 * @epthru		: extended passthru structure
 * @epthru_dma_h	: dma handle for extended passthru structure
 * @buf_dma_h		: dma handle for buffers w/o sg list
 *
 * command control block specific to the mailbox based controllers
 */
typedef struct {
	uint8_t			*raw_mbox;
	mbox_t			*mbox;
	mbox64_t		*mbox64;
	dma_addr_t		mbox_dma_h;
	mbox_sgl64		*sgl64;
	mbox_sgl32		*sgl32;
	dma_addr_t		sgl_dma_h;
	mraid_passthru_t	*pthru;
	dma_addr_t		pthru_dma_h;
	mraid_epassthru_t	*epthru;
	dma_addr_t		epthru_dma_h;
	dma_addr_t		buf_dma_h;
} mbox_ccb_t;


/**
 * mraid_device_t - adapter soft state structure for mailbox controllers
 * @param una_mbox64		: 64-bit mbox - unaligned
 * @param una_mbox64_dma	: mbox dma addr - unaligned
 * @param mbox			: 32-bit mbox - aligned
 * @param mbox64		: 64-bit mbox - aligned
 * @param mbox_dma		: mbox dma addr - aligned
 * @param mailbox_lock		: exclusion lock for the mailbox
 * @param baseport		: base port of hba memory
 * @param baseaddr		: mapped addr of hba memory
 * @param mbox_pool		: pool of mailboxes
 * @param mbox_pool_handle	: handle for the mailbox pool memory
 * @param epthru_pool		: a pool for extended passthru commands
 * @param epthru_pool_handle	: handle to the pool above
 * @param sg_pool		: pool of scatter-gather lists for this driver
 * @param sg_pool_handle	: handle to the pool above
 * @param ccb_list		: list of our command control blocks
 * @param uccb_list		: list of cmd control blocks for mgmt module
 * @param umbox64		: array of mailbox for user commands (cmm)
 * @param pdrv_state		: array for state of each physical drive.
 * @param last_disp		: flag used to show device scanning
 * @param hw_error		: set if FW not responding
 * @param fast_load		: If set, skip physical device scanning
 * @channel_class		: channel class, RAID or SCSI
 *
 * Initialization structure for mailbox controllers: memory based and IO based
 * All the fields in this structure are LLD specific and may be discovered at
 * init() or start() time.
 *
 * NOTE: The fields of this structures are placed to minimize cache misses
 */
typedef struct {
	mbox64_t			*una_mbox64;
	dma_addr_t			una_mbox64_dma;
	mbox_t				*mbox;
	mbox64_t			*mbox64;
	dma_addr_t			mbox_dma;
	spinlock_t			mailbox_lock;
	unsigned long			baseport;
	unsigned long			baseaddr;
	struct mraid_pci_blk		mbox_pool[MBOX_MAX_SCSI_CMDS];
	struct dma_pool			*mbox_pool_handle;
	struct mraid_pci_blk		epthru_pool[MBOX_MAX_SCSI_CMDS];
	struct dma_pool			*epthru_pool_handle;
	struct mraid_pci_blk		sg_pool[MBOX_MAX_SCSI_CMDS];
	struct dma_pool			*sg_pool_handle;
	mbox_ccb_t			ccb_list[MBOX_MAX_SCSI_CMDS];
	mbox_ccb_t			uccb_list[MBOX_MAX_USER_CMDS];
	mbox64_t			umbox64[MBOX_MAX_USER_CMDS];

	uint8_t				pdrv_state[MBOX_MAX_PHYSICAL_DRIVES];
	uint32_t			last_disp;
	int				hw_error;
	int				fast_load;
	uint8_t				channel_class;
} mraid_device_t;

// route to raid device from adapter
#define ADAP2RAIDDEV(adp)	((mraid_device_t *)((adp)->raid_device))

#define MAILBOX_LOCK(rdev)	(&(rdev)->mailbox_lock)

// Find out if this channel is a RAID or SCSI
#define IS_RAID_CH(rdev, ch)	(((rdev)->channel_class >> (ch)) & 0x01)


#define RDINDOOR(rdev)		readl((rdev)->baseaddr + 0x20)
#define RDOUTDOOR(rdev)		readl((rdev)->baseaddr + 0x2C)
#define WRINDOOR(rdev, value)	writel(value, (rdev)->baseaddr + 0x20)
#define WROUTDOOR(rdev, value)	writel(value, (rdev)->baseaddr + 0x2C)

#endif // _MEGARAID_H_

// vim: set ts=8 sw=8 tw=78:

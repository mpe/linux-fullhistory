/*
 * AGPGART module version 0.99
 * Copyright (C) 1999 Jeff Hartmann
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _AGP_BACKEND_PRIV_H
#define _AGP_BACKEND_PRIV_H 1

enum aper_size_type {
	U8_APER_SIZE,
	U16_APER_SIZE,
	U32_APER_SIZE,
	LVL2_APER_SIZE,
	FIXED_APER_SIZE
};

typedef struct _gatt_mask {
	unsigned long mask;
	u32 type;
	/* totally device specific, for integrated chipsets that 
	 * might have different types of memory masks.  For other
	 * devices this will probably be ignored */
} gatt_mask;

typedef struct _aper_size_info_8 {
	int size;
	int num_entries;
	int page_order;
	u8 size_value;
} aper_size_info_8;

typedef struct _aper_size_info_16 {
	int size;
	int num_entries;
	int page_order;
	u16 size_value;
} aper_size_info_16;

typedef struct _aper_size_info_32 {
	int size;
	int num_entries;
	int page_order;
	u32 size_value;
} aper_size_info_32;

typedef struct _aper_size_info_lvl2 {
	int size;
	int num_entries;
	u32 size_value;
} aper_size_info_lvl2;

typedef struct _aper_size_info_fixed {
	int size;
	int num_entries;
	int page_order;
} aper_size_info_fixed;

struct agp_bridge_data {
	agp_version *version;
	void *aperture_sizes;
	void *previous_size;
	void *current_size;
	void *dev_private_data;
	struct pci_dev *dev;
	gatt_mask *masks;
	unsigned long *gatt_table;
	unsigned long *gatt_table_real;
	unsigned long scratch_page;
	unsigned long gart_bus_addr;
	unsigned long gatt_bus_addr;
	u32 mode;
	enum chipset_type type;
	enum aper_size_type size_type;
	unsigned long *key_list;
	atomic_t current_memory_agp;
	atomic_t agp_in_use;
	int max_memory_agp;	/* in number of pages */
	int needs_scratch_page;
	int aperture_size_idx;
	int num_aperture_sizes;
	int num_of_masks;
	int capndx;
	int cant_use_aperture;

	/* Links to driver specific functions */

	int (*fetch_size) (void);
	int (*configure) (void);
	void (*agp_enable) (u32);
	void (*cleanup) (void);
	void (*tlb_flush) (agp_memory *);
	unsigned long (*mask_memory) (unsigned long, int);
	void (*cache_flush) (void);
	int (*create_gatt_table) (void);
	int (*free_gatt_table) (void);
	int (*insert_memory) (agp_memory *, off_t, int);
	int (*remove_memory) (agp_memory *, off_t, int);
	agp_memory *(*alloc_by_type) (size_t, int);
	void (*free_by_type) (agp_memory *);
	unsigned long (*agp_alloc_page) (void);
	void (*agp_destroy_page) (unsigned long);
	int (*suspend)(void);
	void (*resume)(void);
	
};

#define OUTREG32(mmap, addr, val)   __raw_writel((val), (mmap)+(addr))
#define OUTREG16(mmap, addr, val)   __raw_writew((val), (mmap)+(addr))
#define OUTREG8(mmap, addr, val)   __raw_writeb((val), (mmap)+(addr))

#define INREG32(mmap, addr)         __raw_readl((mmap)+(addr))
#define INREG16(mmap, addr)         __raw_readw((mmap)+(addr))
#define INREG8(mmap, addr)         __raw_readb((mmap)+(addr))

#define KB(x) ((x) * 1024)
#define MB(x) (KB (KB (x)))
#define GB(x) (MB (KB (x)))

#define CACHE_FLUSH	agp_bridge.cache_flush
#define A_SIZE_8(x)	((aper_size_info_8 *) x)
#define A_SIZE_16(x)	((aper_size_info_16 *) x)
#define A_SIZE_32(x)	((aper_size_info_32 *) x)
#define A_SIZE_LVL2(x)  ((aper_size_info_lvl2 *) x)
#define A_SIZE_FIX(x)	((aper_size_info_fixed *) x)
#define A_IDX8()	(A_SIZE_8(agp_bridge.aperture_sizes) + i)
#define A_IDX16()	(A_SIZE_16(agp_bridge.aperture_sizes) + i)
#define A_IDX32()	(A_SIZE_32(agp_bridge.aperture_sizes) + i)
#define A_IDXLVL2()	(A_SIZE_LVL2(agp_bridge.aperture_sizes) + i)
#define A_IDXFIX()	(A_SIZE_FIX(agp_bridge.aperture_sizes) + i)
#define MAXKEY		(4096 * 32)

#define AGPGART_MODULE_NAME	"agpgart"
#define PFX			AGPGART_MODULE_NAME ": "

#define PGE_EMPTY(p) (!(p) || (p) == (unsigned long) agp_bridge.scratch_page)

#ifndef PCI_DEVICE_ID_VIA_82C691_0
#define PCI_DEVICE_ID_VIA_82C691_0      0x0691
#endif
#ifndef PCI_DEVICE_ID_VIA_8371_0
#define PCI_DEVICE_ID_VIA_8371_0      0x0391
#endif
#ifndef PCI_DEVICE_ID_VIA_8363_0
#define PCI_DEVICE_ID_VIA_8363_0      0x0305
#endif
#ifndef PCI_DEVICE_ID_VIA_82C694X_0
#define PCI_DEVICE_ID_VIA_82C694X_0      0x0605
#endif
#ifndef PCI_DEVICE_ID_INTEL_810_0
#define PCI_DEVICE_ID_INTEL_810_0       0x7120
#endif
#ifndef PCI_DEVICE_ID_INTEL_830_M_0
#define PCI_DEVICE_ID_INTEL_830_M_0	0x3575
#endif
#ifndef PCI_DEVICE_ID_INTEL_830_M_1
#define PCI_DEVICE_ID_INTEL_830_M_1     0x3577
#endif
#ifndef PCI_DEVICE_ID_INTEL_820_0
#define PCI_DEVICE_ID_INTEL_820_0       0x2500
#endif
#ifndef PCI_DEVICE_ID_INTEL_820_UP_0
#define PCI_DEVICE_ID_INTEL_820_UP_0    0x2501
#endif
#ifndef PCI_DEVICE_ID_INTEL_840_0
#define PCI_DEVICE_ID_INTEL_840_0		0x1a21
#endif
#ifndef PCI_DEVICE_ID_INTEL_845_0
#define PCI_DEVICE_ID_INTEL_845_0     0x1a30
#endif
#ifndef PCI_DEVICE_ID_INTEL_850_0
#define PCI_DEVICE_ID_INTEL_850_0     0x2530
#endif
#ifndef PCI_DEVICE_ID_INTEL_860_0
#define PCI_DEVICE_ID_INTEL_860_0     0x2531
#endif
#ifndef PCI_DEVICE_ID_INTEL_810_DC100_0
#define PCI_DEVICE_ID_INTEL_810_DC100_0 0x7122
#endif
#ifndef PCI_DEVICE_ID_INTEL_810_E_0
#define PCI_DEVICE_ID_INTEL_810_E_0     0x7124
#endif
#ifndef PCI_DEVICE_ID_INTEL_82443GX_0
#define PCI_DEVICE_ID_INTEL_82443GX_0   0x71a0
#endif
#ifndef PCI_DEVICE_ID_INTEL_810_1
#define PCI_DEVICE_ID_INTEL_810_1       0x7121
#endif
#ifndef PCI_DEVICE_ID_INTEL_810_DC100_1
#define PCI_DEVICE_ID_INTEL_810_DC100_1 0x7123
#endif
#ifndef PCI_DEVICE_ID_INTEL_810_E_1
#define PCI_DEVICE_ID_INTEL_810_E_1     0x7125
#endif
#ifndef PCI_DEVICE_ID_INTEL_815_0
#define PCI_DEVICE_ID_INTEL_815_0       0x1130
#endif
#ifndef PCI_DEVICE_ID_INTEL_815_1
#define PCI_DEVICE_ID_INTEL_815_1       0x1132
#endif
#ifndef PCI_DEVICE_ID_INTEL_82443GX_1
#define PCI_DEVICE_ID_INTEL_82443GX_1   0x71a1
#endif
#ifndef PCI_DEVICE_ID_AMD_IRONGATE_0
#define PCI_DEVICE_ID_AMD_IRONGATE_0    0x7006
#endif
#ifndef PCI_DEVICE_ID_AMD_761_0
#define PCI_DEVICE_ID_AMD_761_0         0x700e
#endif
#ifndef PCI_DEVICE_ID_AMD_762_0
#define PCI_DEVICE_ID_AMD_762_0		0x700C
#endif
#ifndef PCI_VENDOR_ID_AL
#define PCI_VENDOR_ID_AL		0x10b9
#endif
#ifndef PCI_DEVICE_ID_AL_M1541_0
#define PCI_DEVICE_ID_AL_M1541_0	0x1541
#endif
#ifndef PCI_DEVICE_ID_AL_M1621_0
#define PCI_DEVICE_ID_AL_M1621_0	0x1621
#endif
#ifndef PCI_DEVICE_ID_AL_M1631_0
#define PCI_DEVICE_ID_AL_M1631_0	0x1631
#endif
#ifndef PCI_DEVICE_ID_AL_M1632_0
#define PCI_DEVICE_ID_AL_M1632_0	0x1632
#endif
#ifndef PCI_DEVICE_ID_AL_M1641_0
#define PCI_DEVICE_ID_AL_M1641_0	0x1641
#endif
#ifndef PCI_DEVICE_ID_AL_M1647_0
#define PCI_DEVICE_ID_AL_M1647_0	0x1647
#endif
#ifndef PCI_DEVICE_ID_AL_M1651_0
#define PCI_DEVICE_ID_AL_M1651_0	0x1651
#endif

/* intel register */
#define INTEL_APBASE    0x10
#define INTEL_APSIZE    0xb4
#define INTEL_ATTBASE   0xb8
#define INTEL_AGPCTRL   0xb0
#define INTEL_NBXCFG    0x50
#define INTEL_ERRSTS    0x91

/* intel i830 registers */
#define I830_GMCH_CTRL             0x52
#define I830_GMCH_ENABLED          0x4
#define I830_GMCH_MEM_MASK         0x1
#define I830_GMCH_MEM_64M          0x1
#define I830_GMCH_MEM_128M         0
#define I830_GMCH_GMS_MASK         0x70
#define I830_GMCH_GMS_DISABLED     0x00
#define I830_GMCH_GMS_LOCAL        0x10
#define I830_GMCH_GMS_STOLEN_512   0x20
#define I830_GMCH_GMS_STOLEN_1024  0x30
#define I830_GMCH_GMS_STOLEN_8192  0x40
#define I830_RDRAM_CHANNEL_TYPE    0x03010
#define I830_RDRAM_ND(x)           (((x) & 0x20) >> 5)
#define I830_RDRAM_DDT(x)          (((x) & 0x18) >> 3)

/* This one is for I830MP w. an external graphic card */
#define INTEL_I830_ERRSTS          0x92

/* intel i820 registers */
#define INTEL_I820_RDCR     0x51
#define INTEL_I820_ERRSTS   0xc8

/* intel i840 registers */
#define INTEL_I840_MCHCFG   0x50
#define INTEL_I840_ERRSTS   0xc8
 
/* intel i845 registers */
#define INTEL_I845_AGPM     0x51
#define INTEL_I845_ERRSTS   0xc8

/* intel i850 registers */
#define INTEL_I850_MCHCFG   0x50
#define INTEL_I850_ERRSTS   0xc8

/* intel i860 registers */
#define INTEL_I860_MCHCFG	0x50
#define INTEL_I860_ERRSTS	0xc8

/* intel i810 registers */
#define I810_GMADDR 0x10
#define I810_MMADDR 0x14
#define I810_PTE_BASE          0x10000
#define I810_PTE_MAIN_UNCACHED 0x00000000
#define I810_PTE_LOCAL         0x00000002
#define I810_PTE_VALID         0x00000001
#define I810_SMRAM_MISCC       0x70
#define I810_GFX_MEM_WIN_SIZE  0x00010000
#define I810_GFX_MEM_WIN_32M   0x00010000
#define I810_GMS               0x000000c0
#define I810_GMS_DISABLE       0x00000000
#define I810_PGETBL_CTL        0x2020
#define I810_PGETBL_ENABLED    0x00000001
#define I810_DRAM_CTL          0x3000
#define I810_DRAM_ROW_0        0x00000001
#define I810_DRAM_ROW_0_SDRAM  0x00000001



/* VIA register */
#define VIA_APBASE      0x10
#define VIA_GARTCTRL    0x80
#define VIA_APSIZE      0x84
#define VIA_ATTBASE     0x88

/* SiS registers */
#define SIS_APBASE      0x10
#define SIS_ATTBASE     0x90
#define SIS_APSIZE      0x94
#define SIS_TLBCNTRL    0x97
#define SIS_TLBFLUSH    0x98

/* AMD registers */
#define AMD_APBASE      0x10
#define AMD_MMBASE      0x14
#define AMD_APSIZE      0xac
#define AMD_MODECNTL    0xb0
#define AMD_MODECNTL2   0xb2
#define AMD_GARTENABLE  0x02	/* In mmio region (16-bit register) */
#define AMD_ATTBASE     0x04	/* In mmio region (32-bit register) */
#define AMD_TLBFLUSH    0x0c	/* In mmio region (32-bit register) */
#define AMD_CACHEENTRY  0x10	/* In mmio region (32-bit register) */

/* ALi registers */
#define ALI_APBASE	0x10
#define ALI_AGPCTRL	0xb8
#define ALI_ATTBASE	0xbc
#define ALI_TLBCTRL	0xc0
#define ALI_TAGCTRL	0xc4
#define ALI_CACHE_FLUSH_CTRL	0xD0
#define ALI_CACHE_FLUSH_ADDR_MASK	0xFFFFF000
#define ALI_CACHE_FLUSH_EN	0x100

/* Serverworks Registers */
#define SVWRKS_APSIZE 0x10
#define SVWRKS_SIZE_MASK 0xfe000000

#define SVWRKS_MMBASE 0x14
#define SVWRKS_CACHING 0x4b
#define SVWRKS_FEATURE 0x68

/* func 1 registers */
#define SVWRKS_AGP_ENABLE 0x60
#define SVWRKS_COMMAND 0x04

/* Memory mapped registers */
#define SVWRKS_GART_CACHE 0x02
#define SVWRKS_GATTBASE   0x04
#define SVWRKS_TLBFLUSH   0x10
#define SVWRKS_POSTFLUSH  0x14
#define SVWRKS_DIRFLUSH   0x0c

#endif				/* _AGP_BACKEND_PRIV_H */

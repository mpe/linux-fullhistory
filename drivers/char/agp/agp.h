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
	u32 *key_list;
	atomic_t current_memory_agp;
	atomic_t agp_in_use;
	int max_memory_agp;	/* in number of pages */
	int needs_scratch_page;
	int aperture_size_idx;
	int num_aperture_sizes;
	int num_of_masks;
	int capndx;

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

	/* Links to vendor/device specific setup functions */
#ifdef CONFIG_AGP_INTEL
	void (*intel_generic_setup) (void);
#endif
#ifdef CONFIG_AGP_I810
	void (*intel_i810_setup) (struct pci_dev *);
#endif
#ifdef CONFIG_AGP_VIA
	void (*via_generic_setup) (void);
#endif
#ifdef CONFIG_AGP_SIS
	void (*sis_generic_setup) (void);
#endif
#ifdef CONFIG_AGP_AMD
	void (*amd_irongate_setup) (void);
#endif
#ifdef CONFIG_AGP_ALI
	void (*ali_generic_setup) (void);
#endif
};

#define OUTREG32(mmap, addr, val)   *(volatile u32 *)(mmap + (addr)) = (val)
#define OUTREG16(mmap, addr, val)   *(volatile u16 *)(mmap + (addr)) = (val)
#define OUTREG8 (mmap, addr, val)   *(volatile u8 *) (mmap + (addr)) = (val)

#define INREG32(mmap, addr)         *(volatile u32 *)(mmap + (addr))
#define INREG16(mmap, addr)         *(volatile u16 *)(mmap + (addr))
#define INREG8 (mmap, addr)         *(volatile u8 *) (mmap + (addr))

#define CACHE_FLUSH	agp_bridge.cache_flush
#define A_SIZE_8(x)	((aper_size_info_8 *) x)
#define A_SIZE_16(x)	((aper_size_info_16 *) x)
#define A_SIZE_32(x)	((aper_size_info_32 *) x)
#define A_SIZE_FIX(x)	((aper_size_info_fixed *) x)
#define A_IDX8()	(A_SIZE_8(agp_bridge.aperture_sizes) + i)
#define A_IDX16()	(A_SIZE_16(agp_bridge.aperture_sizes) + i)
#define A_IDX32()	(A_SIZE_32(agp_bridge.aperture_sizes) + i)
#define A_IDXFIX()	(A_SIZE_FIX(agp_bridge.aperture_sizes) + i)
#define MAXKEY		(4096 * 32)

#ifndef min
#define min(a,b)	(((a)<(b))?(a):(b))
#endif

#define PGE_EMPTY(p) (!(p) || (p) == (unsigned long) agp_bridge.scratch_page)

#ifndef PCI_DEVICE_ID_VIA_82C691_0
#define PCI_DEVICE_ID_VIA_82C691_0      0x0691
#endif
#ifndef PCI_DEVICE_ID_VIA_82C691_1
#define PCI_DEVICE_ID_VIA_82C691_1      0x8691
#endif
#ifndef PCI_DEVICE_ID_INTEL_810_0
#define PCI_DEVICE_ID_INTEL_810_0       0x7120
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
#ifndef PCI_DEVICE_ID_INTEL_82443GX_1
#define PCI_DEVICE_ID_INTEL_82443GX_1   0x71a1
#endif
#ifndef PCI_DEVICE_ID_AMD_IRONGATE_0
#define PCI_DEVICE_ID_AMD_IRONGATE_0    0x7006
#endif
#ifndef PCI_VENDOR_ID_AL
#define PCI_VENDOR_ID_AL		0x10b9
#endif
#ifndef PCI_DEVICE_ID_AL_M1541_0
#define PCI_DEVICE_ID_AL_M1541_0	0x1541
#endif

/* intel register */
#define INTEL_APBASE    0x10
#define INTEL_APSIZE    0xb4
#define INTEL_ATTBASE   0xb8
#define INTEL_AGPCTRL   0xb0
#define INTEL_NBXCFG    0x50
#define INTEL_ERRSTS    0x91

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
#define AMD_GARTENABLE  0x02	/* In mmio region (16-bit register) */
#define AMD_ATTBASE     0x04	/* In mmio region (32-bit register) */
#define AMD_TLBFLUSH    0x0c	/* In mmio region (32-bit register) */
#define AMD_CACHEENTRY  0x10	/* In mmio region (32-bit register) */

/* ALi registers */
#define ALI_APBASE	0x10
#define ALI_AGPCTRL	0xb8
#define ALI_ATTBASE	0xbc
#define ALI_TLBCTRL	0xc0

#endif				/* _AGP_BACKEND_PRIV_H */

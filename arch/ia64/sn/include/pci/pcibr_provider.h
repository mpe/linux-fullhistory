/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997,2000-2004 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_PCI_PCIBR_PROVIDER_H
#define _ASM_IA64_SN_PCI_PCIBR_PROVIDER_H

/* Workarounds */
#define PV907516 (1 << 1) /* TIOCP: Don't write the write buffer flush reg */

#define BUSTYPE_MASK                    0x1

/* Macros given a pcibus structure */
#define IS_PCIX(ps)     ((ps)->pbi_bridge_mode & BUSTYPE_MASK)
#define IS_PCI_BRIDGE_ASIC(asic) (asic == PCIIO_ASIC_TYPE_PIC || \
                asic == PCIIO_ASIC_TYPE_TIOCP)
#define IS_PIC_SOFT(ps)     (ps->pbi_bridge_type == PCIBR_BRIDGETYPE_PIC)


/* 
 * The different PCI Bridge types supported on the SGI Altix platforms
 */
#define PCIBR_BRIDGETYPE_UNKNOWN       -1
#define PCIBR_BRIDGETYPE_PIC            2
#define PCIBR_BRIDGETYPE_TIOCP          3

/*
 * Bridge 64bit Direct Map Attributes
 */
#define PCI64_ATTR_PREF                 (1ull << 59)
#define PCI64_ATTR_PREC                 (1ull << 58)
#define PCI64_ATTR_VIRTUAL              (1ull << 57)
#define PCI64_ATTR_BAR                  (1ull << 56)
#define PCI64_ATTR_SWAP                 (1ull << 55)
#define PCI64_ATTR_VIRTUAL1             (1ull << 54)

#define PCI32_LOCAL_BASE                0
#define PCI32_MAPPED_BASE               0x40000000
#define PCI32_DIRECT_BASE               0x80000000

#define IS_PCI32_MAPPED(x)              ((uint64_t)(x) < PCI32_DIRECT_BASE && \
                                         (uint64_t)(x) >= PCI32_MAPPED_BASE)
#define IS_PCI32_DIRECT(x)              ((uint64_t)(x) >= PCI32_MAPPED_BASE)


/*
 * Bridge PMU Address Transaltion Entry Attibutes
 */
#define PCI32_ATE_V                     (0x1 << 0)
#define PCI32_ATE_CO                    (0x1 << 1)
#define PCI32_ATE_PREC                  (0x1 << 2)
#define PCI32_ATE_PREF                  (0x1 << 3)
#define PCI32_ATE_BAR                   (0x1 << 4)
#define PCI32_ATE_ADDR_SHFT             12

#define MINIMAL_ATES_REQUIRED(addr, size) \
	(IOPG(IOPGOFF(addr) + (size) - 1) == IOPG((size) - 1))

#define MINIMAL_ATE_FLAG(addr, size) \
	(MINIMAL_ATES_REQUIRED((uint64_t)addr, size) ? 1 : 0)

/* bit 29 of the pci address is the SWAP bit */
#define ATE_SWAPSHIFT                   29
#define ATE_SWAP_ON(x)                  ((x) |= (1 << ATE_SWAPSHIFT))
#define ATE_SWAP_OFF(x)                 ((x) &= ~(1 << ATE_SWAPSHIFT))

/*
 * I/O page size
 */
#if PAGE_SIZE < 16384
#define IOPFNSHIFT                      12      /* 4K per mapped page */
#else
#define IOPFNSHIFT                      14      /* 16K per mapped page */
#endif

#define IOPGSIZE                        (1 << IOPFNSHIFT)
#define IOPG(x)                         ((x) >> IOPFNSHIFT)
#define IOPGOFF(x)                      ((x) & (IOPGSIZE-1))

#define PCIBR_DEV_SWAP_DIR              (1ull << 19)
#define PCIBR_CTRL_PAGE_SIZE            (0x1 << 21)

/*
 * PMU resources.
 */
struct ate_resource{
	uint64_t *ate;
	uint64_t num_ate;
	uint64_t lowest_free_index;
};

struct pcibus_info {
	struct pcibus_bussoft	pbi_buscommon;   /* common header */
	uint32_t                pbi_moduleid;
	short                   pbi_bridge_type;
	short                   pbi_bridge_mode;

	struct ate_resource     pbi_int_ate_resource;
	uint64_t                pbi_int_ate_size;
	
	uint64_t                pbi_dir_xbase;
	char                    pbi_hub_xid;

	uint64_t                pbi_devreg[8];
	spinlock_t              pbi_lock;

	uint32_t		pbi_valid_devices;
	uint32_t		pbi_enabled_devices;
};

/*
 * pcibus_info structure locking macros
 */
inline static unsigned long
pcibr_lock(struct pcibus_info *pcibus_info)
{
	unsigned long flag;
	spin_lock_irqsave(&pcibus_info->pbi_lock, flag);
	return(flag);
}
#define pcibr_unlock(pcibus_info, flag)  spin_unlock_irqrestore(&pcibus_info->pbi_lock, flag)

extern void *pcibr_bus_fixup(struct pcibus_bussoft *);
extern uint64_t pcibr_dma_map(struct pcidev_info *, unsigned long, size_t, unsigned int);
extern void pcibr_dma_unmap(struct pcidev_info *, dma_addr_t, int);

/*
 * prototypes for the bridge asic register access routines in pcibr_reg.c
 */
extern void             pcireg_control_bit_clr(struct pcibus_info *, uint64_t);
extern void             pcireg_control_bit_set(struct pcibus_info *, uint64_t);
extern uint64_t         pcireg_tflush_get(struct pcibus_info *);
extern uint64_t         pcireg_intr_status_get(struct pcibus_info *);
extern void             pcireg_intr_enable_bit_clr(struct pcibus_info *, uint64_t);
extern void             pcireg_intr_enable_bit_set(struct pcibus_info *, uint64_t);
extern void             pcireg_intr_addr_addr_set(struct pcibus_info *, int, uint64_t);
extern void             pcireg_force_intr_set(struct pcibus_info *, int);
extern uint64_t         pcireg_wrb_flush_get(struct pcibus_info *, int);
extern void             pcireg_int_ate_set(struct pcibus_info *, int, uint64_t);
extern uint64_t *	pcireg_int_ate_addr(struct pcibus_info *, int);
extern void 		pcibr_force_interrupt(struct sn_irq_info *sn_irq_info);
extern void 		pcibr_change_devices_irq(struct sn_irq_info *sn_irq_info);
extern int 		pcibr_ate_alloc(struct pcibus_info *, int);
extern void 		pcibr_ate_free(struct pcibus_info *, int);
extern void 		ate_write(struct pcibus_info *, int, int, uint64_t);
#endif

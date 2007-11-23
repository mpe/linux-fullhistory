/* $Id: sbus.h,v 1.10 1998/12/16 04:33:58 davem Exp $
 * sbus.h:  Defines for the Sun SBus.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

/* XXX This needs to be mostly redone for sun5 SYSIO. */

#ifndef _SPARC64_SBUS_H
#define _SPARC64_SBUS_H

#include <asm/oplib.h>
#include <asm/iommu.h>

/* We scan which devices are on the SBus using the PROM node device
 * tree.  SBus devices are described in two different ways.  You can
 * either get an absolute address at which to access the device, or
 * you can get a SBus 'slot' number and an offset within that slot.
 */

/* The base address at which to calculate device OBIO addresses. */
#define SUN_SBUS_BVADDR        0x00000000
#define SBUS_OFF_MASK          0x0fffffff

/* These routines are used to calculate device address from slot
 * numbers + offsets, and vice versa.
 */

extern __inline__ unsigned long sbus_devaddr(int slotnum, unsigned long offset)
{
  return (unsigned long) (SUN_SBUS_BVADDR+((slotnum)<<28)+(offset));
}

extern __inline__ int sbus_dev_slot(unsigned long dev_addr)
{
  return (int) (((dev_addr)-SUN_SBUS_BVADDR)>>28);
}

extern __inline__ unsigned long sbus_dev_offset(unsigned long dev_addr)
{
  return (unsigned long) (((dev_addr)-SUN_SBUS_BVADDR)&SBUS_OFF_MASK);
}

struct linux_sbus;

/* Linux SBUS device tables */
struct linux_sbus_device {
  struct linux_sbus_device *next;      /* next device on this SBus or null */
  struct linux_sbus_device *child;     /* For ledma and espdma on sun4m */
  struct linux_sbus *my_bus;           /* Back ptr to sbus */
  int prom_node;                       /* PROM device tree node for this device */
  char prom_name[32];                  /* PROM device name */

  struct linux_prom_registers reg_addrs[PROMREG_MAX];
  int num_registers, ranges_applied;

  unsigned int irqs[4];
  int num_irqs;

  unsigned long sbus_addr;             /* Absolute base address for device. */
  unsigned long sbus_vaddrs[PROMVADDR_MAX];
  unsigned long num_vaddrs;
  unsigned long offset;                /* Offset given by PROM */
  int slot;
};

/* This struct describes the SBus(s) found on this machine. */
struct linux_sbus {
	struct linux_sbus *next;             /* next SBus, if more than one SBus */
	struct linux_sbus_device *devices;   /* Link to devices on this SBus */
	struct iommu_struct *iommu;          /* IOMMU for this sbus if applicable */
	int prom_node;                       /* PROM device tree node for this SBus */
	char prom_name[64];                  /* Usually "sbus" or "sbi" */
	int clock_freq;
	struct linux_prom_ranges sbus_ranges[PROMREG_MAX];
	int num_sbus_ranges;
	int upaid;
	void *starfire_cookie;
};

extern struct linux_sbus *SBus_chain;

/* Device probing routines could find these handy */
#define for_each_sbus(bus) \
        for((bus) = SBus_chain; (bus); (bus)=(bus)->next)

#define for_each_sbusdev(device, bus) \
        for((device) = (bus)->devices; (device); (device)=(device)->next)
        
#define for_all_sbusdev(device, bus) \
	for((bus) = SBus_chain, ((device) = (bus) ? (bus)->devices : 0); (bus); (device)=((device)->next ? (device)->next : ((bus) = (bus)->next, (bus) ? (bus)->devices : 0)))

extern void mmu_set_sbus64(struct linux_sbus_device *, int);

/* If you did not get the buffer from mmu_get_*() or sparc_alloc_dvma()
 * then you must use this to get the 32-bit SBUS dvma address.
 * And in this case it is your responsibility to make sure the buffer
 * is GFP_DMA, ie. that it is not greater than MAX_DMA_ADDRESS.
 */
extern unsigned long phys_base;
#define sbus_dvma_addr(__addr)	((__u32)(__pa(__addr) - phys_base))

/* Apply promlib probed SBUS ranges to registers. */
extern void prom_apply_sbus_ranges(struct linux_sbus *sbus, 
				   struct linux_prom_registers *sbusregs, int nregs, 
				   struct linux_sbus_device *sdev);

#endif /* !(_SPARC64_SBUS_H) */

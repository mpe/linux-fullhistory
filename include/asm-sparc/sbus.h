/* $Id: sbus.h,v 1.9 1996/02/15 09:13:03 davem Exp $
 * sbus.h:  Defines for the Sun SBus.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SBUS_H
#define _SPARC_SBUS_H

#include <asm/oplib.h>
#include <asm/iommu.h>

/* We scan which devices are on the SBus using the PROM node device
 * tree.  SBus devices are described in two different ways.  You can
 * either get an absolute address at which to access the device, or
 * you can get a SBus 'slot' number and an offset within that slot.
 */

/* The base address at which to calculate device OBIO addresses. */
#define SUN_SBUS_BVADDR        0xf8000000
#define SBUS_OFF_MASK          0x01ffffff

/* These routines are used to calculate device address from slot
 * numbers + offsets, and vice versa.
 */

extern inline unsigned long sbus_devaddr(int slotnum, unsigned long offset)
{
  return (unsigned long) (SUN_SBUS_BVADDR+((slotnum)<<25)+(offset));
}

extern inline int sbus_dev_slot(unsigned long dev_addr)
{
  return (int) (((dev_addr)-SUN_SBUS_BVADDR)>>25);
}

extern inline unsigned long sbus_dev_offset(unsigned long dev_addr)
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
  char prom_name[64];                  /* PROM device name */
  char linux_name[64];                 /* Name used internally by Linux */

  struct linux_prom_registers reg_addrs[PROMREG_MAX];
  int num_registers;

  struct linux_prom_irqs irqs[PROMINTR_MAX];
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
	char prom_name[64];                  /* Usually "sbus" */
	int clock_freq;
};

extern struct linux_sbus *SBus_chain;

extern inline int
sbus_is_slave(struct linux_sbus_device *dev)
{
	/* Have to write this for sun4c's */
	return 0;
}

/* Device probing routines could find these handy */
#define for_each_sbus(bus) \
        for((bus) = SBus_chain; (bus); (bus)=(bus)->next)

#define for_each_sbusdev(device, bus) \
        for((device) = (bus)->devices; (device); (device)=(device)->next)

#endif /* !(_SPARC_SBUS_H) */

/* sbus.h:  Defines for the Sun SBus.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SBUS_H
#define _SPARC_SBUS_H

#include <asm/openprom.h>  /* For linux_prom_registers and linux_prom_irqs */

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

/* Handy macro */
#define STRUCT_ALIGN(addr) ((addr+7)&(~7))

/* Linus SBUS device tables */
struct linux_sbus_device {
  struct linux_sbus_device *next;      /* next device on this SBus or null */
  int prom_node;                       /* PROM device tree node for this device */
  char *prom_name;                     /* PROM device name */
  char *linux_name;                    /* Name used internally by Linux */

  /* device register addresses */
  struct linux_prom_registers reg_addrs[PROMREG_MAX];
  int num_registers;

  /* List of IRQ's this device uses. */
  struct linux_prom_irqs irqs[PROMINTR_MAX];
  int num_irqs;

  unsigned long sbus_addr;             /* Absolute base address for device. */
  unsigned long sbus_vaddrs[PROMVADDR_MAX];
  unsigned long num_vaddrs;
  unsigned long offset;                /* Offset given by PROM */
  int slot;                            /* Device slot number */
};

/* This struct describes the SBus-es found on this machine. */
struct linux_sbus {
  struct linux_sbus *next;             /* next SBus, if more than one SBus */
  struct linux_sbus_device *devices;   /* Link to devices on this SBus */
  int prom_node;                       /* PROM device tree node for this SBus */
  char *prom_name;                     /* Usually "sbus" */
  int clock_freq;                 /* Speed of this SBus */
};

extern struct linux_sbus Linux_SBus;

#endif /* !(_SPARC_SBUS_H) */

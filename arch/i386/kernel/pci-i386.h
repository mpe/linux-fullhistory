/*
 *	Low-Level PCI Access for i386 machines.
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 */

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#define PCI_PROBE_BIOS 1
#define PCI_PROBE_CONF1 2
#define PCI_PROBE_CONF2 4
#define PCI_NO_SORT 0x100
#define PCI_BIOS_SORT 0x200
#define PCI_NO_CHECKS 0x400
#define PCI_NO_PEER_FIXUP 0x800
#define PCI_ASSIGN_ROMS 0x1000
#define PCI_NO_IRQ_SCAN 0x2000

extern unsigned int pci_probe;

/* pci-i386.c */

void pcibios_resource_survey(void);

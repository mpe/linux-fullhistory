/*
 *	linux/arch/alpha/kernel/bios32.h
 *
 * This file contains declarations and inline functions for interfacing
 * with the PCI initialization routines in bios32.c.
 */


#define KB		1024
#define MB		(1024*KB)
#define GB		(1024*MB)

/*
 * We can't just blindly use 64K for machines with EISA busses; they
 * may also have PCI-PCI bridges present, and then we'd configure the
 * bridge incorrectly.
 *
 * Also, we start at 0x8000 or 0x9000, in hopes to get all devices'
 * IO space areas allocated *before* 0xC000; this is because certain
 * BIOSes (Millennium for one) use PCI Config space "mechanism #2"
 * accesses to probe the bus. If a device's registers appear at 0xC000,
 * it may see an INx/OUTx at that address during BIOS emulation of the
 * VGA BIOS, and some cards, notably Adaptec 2940UW, take mortal offense.
 *
 * Note that we may need this stuff for SRM_SETUP also, since certain
 * SRM consoles screw up and allocate I/O space addresses > 64K behind
 * PCI-to_PCI bridges, which can't pass I/O addresses larger than 64K,
 * AFAIK.
 */

#define EISA_DEFAULT_IO_BASE 0x9000	/* start above 8th slot */
#define DEFAULT_IO_BASE 0x8000		/* start at 8th slot */

/*
 * We try to make the DEFAULT_MEM_BASE addresses *always* have more than
 * a single bit set. This is so that devices like the broken Myrinet card
 * will always have a PCI memory address that will never match a IDSEL
 * address in PCI Config space, which can cause problems with early rev cards.
 */

/*
 * An XL is AVANTI (APECS) family, *but* it has only 27 bits of ISA address
 * that get passed through the PCI<->ISA bridge chip. Although this causes
 * us to set the PCI->Mem window bases lower than normal, we still allocate
 * PCI bus devices' memory addresses *below* the low DMA mapping window,
 * and hope they fit below 64Mb (to avoid conflicts), and so that they can
 * be accessed via SPARSE space.
 *
 * We accept the risk that a broken Myrinet card will be put into a true XL
 * and thus can more easily run into the problem described below.
 */
#define XL_DEFAULT_MEM_BASE (16*MB + 2*MB) /* 16M to 64M-1 is avail */

/*
 * APECS and LCA have only 34 bits for physical addresses, thus limiting PCI
 * bus memory addresses for SPARSE access to be less than 128Mb.
 */
#define APECS_AND_LCA_DEFAULT_MEM_BASE (64*MB + 2*MB)

/*
 * Because the MCPCIA core logic supports more bits for physical addresses,
 * it should allow an expanded range of SPARSE memory addresses.
 * However, we do not use them all, in order to avoid the HAE manipulation
 * that would be needed.
 */
#define RAWHIDE_DEFAULT_MEM_BASE (64*MB + 2*MB)

/*
 * Because CIA and PYXIS and T2 have more bits for physical addresses,
 * they support an expanded range of SPARSE memory addresses.
 */
#define DEFAULT_MEM_BASE (128*MB + 16*MB)


/*
 * PCI_MODIFY
 *
 * If this 0, then do not write to any of the PCI registers, merely
 * read them (i.e., use configuration as determined by SRM).  The SRM
 * seem do be doing a less than perfect job in configuring PCI
 * devices, so for now we do it ourselves.  Reconfiguring PCI devices
 * breaks console (RPB) callbacks, but those don't work properly with
 * 64 bit addresses anyways.
 *
 * The accepted convention seems to be that the console (POST
 * software) should fully configure boot devices and configure the
 * interrupt routing of *all* devices.  In particular, the base
 * addresses of non-boot devices need not be initialized.  For
 * example, on the AXPpci33 board, the base address a #9 GXE PCI
 * graphics card reads as zero (this may, however, be due to a bug in
 * the graphics card---there have been some rumor that the #9 BIOS
 * incorrectly resets that address to 0...).
 */

#define PCI_MODIFY	(!alpha_use_srm_setup)
     

/* 
 * A small note about bridges and interrupts.  The DECchip 21050 (and
 * later) adheres to the PCI-PCI bridge specification.  This says that
 * the interrupts on the other side of a bridge are swizzled in the
 * following manner:
 *
 * Dev    Interrupt   Interrupt 
 *        Pin on      Pin on 
 *        Device      Connector
 *
 *   4    A           A
 *        B           B
 *        C           C
 *        D           D
 * 
 *   5    A           B
 *        B           C
 *        C           D
 *        D           A
 *
 *   6    A           C
 *        B           D
 *        C           A
 *        D           B
 *
 *   7    A           D
 *        B           A
 *        C           B
 *        D           C
 *
 *   Where A = pin 1, B = pin 2 and so on and pin=0 = default = A.
 *   Thus, each swizzle is ((pin-1) + (device#-4)) % 4
 *
 *   The following code swizzles for exactly one bridge.  The routine
 *   common_swizzle below handles multiple bridges.  But there are a
 *   couple boards that do strange things, so we define this here.
 */

static inline unsigned char
bridge_swizzle(unsigned char pin, unsigned int slot) 
{
	return (((pin-1) + slot) % 4) + 1;
}

extern void layout_all_busses(unsigned long io_base, unsigned long mem_base);
extern void enable_ide(long ide_base);

struct pci_dev;

extern void
common_pci_fixup(int (*map_irq)(struct pci_dev *dev, int slot, int pin),
		 int (*swizzle)(struct pci_dev *dev, int *pin));

extern int common_swizzle(struct pci_dev *dev, int *pinp);

/* The following macro is used to implement the table-based irq mapping
   function for all single-bus Alphas.  */

#define COMMON_TABLE_LOOKUP						\
({ long _ctl_ = -1; 							\
   if (slot >= min_idsel && slot <= max_idsel && pin < irqs_per_slot)	\
     _ctl_ = irq_tab[slot - min_idsel][pin];				\
   _ctl_; })


/* The hose list.  */
extern struct linux_hose_info *hose_head, **hose_tail;
extern int hose_count;
extern int pci_probe_enabled;

/*
 * $Id: pmc551.c,v 1.7 2000/07/03 10:01:38 dwmw2 Exp $
 *
 * PMC551 PCI Mezzanine Ram Device
 *
 * Author:
 *       Mark Ferrell
 *       Copyright 1999,2000 Nortel Networks
 *
 * License: 
 *	 As part of this driver was derrived from the slram.c driver it falls
 *	 under the same license, which is GNU General Public License v2
 *
 * Description: 
 *	 This driver is intended to support the PMC551 PCI Ram device from
 *	 Ramix Inc.  The PMC551 is a PMC Mezzanine module for cPCI embeded
 *	 systems.  The device contains a single SROM that initally programs the
 *	 V370PDC chipset onboard the device, and various banks of DRAM/SDRAM
 *	 onboard.  This driver implements this PCI Ram device as an MTD (Memory
 *	 Technologies Device) so that it can be used to hold a filesystem, or
 *	 for added swap space in embeded systems.  Since the memory on this
 *	 board isn't as fast as main memory we do not try to hook it into main
 *	 memeory as that would simply reduce performance on the system.  Using
 *	 it as a block device allows us to use it as high speed swap or for a
 *	 high speed disk device of some sort.  Which becomes very usefull on
 *	 diskless systems in the embeded market I might add.
 *
 * Credits:
 *       Saeed Karamooz <saeed@ramix.com> of Ramix INC. for the initial
 *       example code of how to initialize this device and for help with
 *       questions I had concerning operation of the device.
 *
 *       Most of the MTD code for this driver was originally written for the
 *       slram.o module in the MTD drivers package written by David Hinds 
 *       <dhinds@allegro.stanford.edu> which allows the mapping of system
 *       memory into an mtd device.  Since the PMC551 memory module is
 *       accessed in the same fashion as system memory, the slram.c code
 *       became a very nice fit to the needs of this driver.  All we added was
 *       PCI detection/initialization to the driver and automaticly figure out
 *       the size via the PCI detection.o, later changes by Corey Minyard
 *       settup the card to utilize a 1M sliding apature.
 *
 *	 Corey Minyard <minyard@nortelnetworks.com>
 *       * Modified driver to utilize a sliding apature instead of mapping all
 *       memory into kernel space which turned out to be very wastefull.
 *       * Located a bug in the SROM's initialization sequence that made the
 *       memory unussable, added a fix to code to touch up the DRAM some.
 *
 * Bugs/FIXME's: 
 *       * MUST fix the init function to not spin on a register
 *       waiting for it to set .. this does not safely handle busted devices
 *       that never reset the register correctly which will cause the system to
 *       hang w/ a reboot beeing the only chance at recover.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <stdarg.h>
#include <linux/pci.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/pmc551.h>
#include <linux/mtd/compatmac.h>

#if LINUX_VERSION_CODE > 0x20300
#define PCI_BASE_ADDRESS(dev) (dev->resource[0].start)
#else
#define PCI_BASE_ADDRESS(dev) (dev->base_address[0])
#endif

static struct mtd_info *pmc551list = NULL;

static int pmc551_erase (struct mtd_info *mtd, struct erase_info *instr)
{
        struct mypriv *priv = mtd->priv;
        u32 start_addr_highbits;
        u32 end_addr_highbits;
        u32 start_addr_lowbits;
        u32 end_addr_lowbits;
        unsigned long end;

        end = instr->addr + instr->len;

        /* Is it too much memory?  The second check find if we wrap around
           past the end of a u32. */
        if ((end > mtd->size) || (end < instr->addr)) {
                return -EINVAL;
        }

        start_addr_highbits = instr->addr & PMC551_ADDR_HIGH_MASK;
        end_addr_highbits = end & PMC551_ADDR_HIGH_MASK;
        start_addr_lowbits = instr->addr & PMC551_ADDR_LOW_MASK;
        end_addr_lowbits = end & PMC551_ADDR_LOW_MASK;

        pci_write_config_dword ( priv->dev,
                                 PMC551_PCI_MEM_MAP0,
                                 (priv->mem_map0_base_val
                                  | start_addr_highbits));
        if (start_addr_highbits == end_addr_highbits) {
                /* The whole thing fits within one access, so just one shot
                   will do it. */
                memset(priv->start + start_addr_lowbits,
                       0xff,
                       instr->len);
        } else {
                /* We have to do multiple writes to get all the data
                   written. */
                memset(priv->start + start_addr_lowbits,
                       0xff,
                       priv->aperture_size - start_addr_lowbits);
                start_addr_highbits += priv->aperture_size;
                while (start_addr_highbits != end_addr_highbits) {
                        pci_write_config_dword ( priv->dev,
                                                 PMC551_PCI_MEM_MAP0,
                                                 (priv->mem_map0_base_val
                                                  | start_addr_highbits));
                        memset(priv->start,
                               0xff,
                               priv->aperture_size);
                        start_addr_highbits += priv->aperture_size;
                }
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
                memset(priv->start,
                       0xff,
                       end_addr_lowbits);
        }

	instr->state = MTD_ERASE_DONE;

        if (instr->callback) {
                (*(instr->callback))(instr);
	}

        return 0;
}


static void pmc551_unpoint (struct mtd_info *mtd, u_char *addr)
{}


static int pmc551_read (struct mtd_info *mtd,
                        loff_t from,
                        size_t len,
                        size_t *retlen,
                        u_char *buf)
{
        struct mypriv *priv = (struct mypriv *)mtd->priv;
        u32 start_addr_highbits;
        u32 end_addr_highbits;
        u32 start_addr_lowbits;
        u32 end_addr_lowbits;
        unsigned long end;
        u_char *copyto = buf;


        /* Is it past the end? */
        if (from > mtd->size) {
                return -EINVAL;
        }

        end = from + len;
        start_addr_highbits = from & PMC551_ADDR_HIGH_MASK;
        end_addr_highbits = end & PMC551_ADDR_HIGH_MASK;
        start_addr_lowbits = from & PMC551_ADDR_LOW_MASK;
        end_addr_lowbits = end & PMC551_ADDR_LOW_MASK;


        /* Only rewrite the first value if it doesn't match our current
           values.  Most operations are on the same page as the previous
           value, so this is a pretty good optimization. */
        if (priv->curr_mem_map0_val !=
                        (priv->mem_map0_base_val | start_addr_highbits)) {
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
        }

        if (start_addr_highbits == end_addr_highbits) {
                /* The whole thing fits within one access, so just one shot
                   will do it. */
                memcpy(copyto,
                       priv->start + start_addr_lowbits,
                       len);
                copyto += len;
        } else {
                /* We have to do multiple writes to get all the data
                   written. */
                memcpy(copyto,
                       priv->start + start_addr_lowbits,
                       priv->aperture_size - start_addr_lowbits);
                copyto += priv->aperture_size - start_addr_lowbits;
                start_addr_highbits += priv->aperture_size;
                while (start_addr_highbits != end_addr_highbits) {
                        pci_write_config_dword ( priv->dev,
                                                 PMC551_PCI_MEM_MAP0,
                                                 (priv->mem_map0_base_val
                                                  | start_addr_highbits));
                        memcpy(copyto,
                               priv->start,
                               priv->aperture_size);
                        copyto += priv->aperture_size;
                        start_addr_highbits += priv->aperture_size;
                        if (start_addr_highbits >= mtd->size) {
                                /* Make sure we have the right value here. */
                                priv->curr_mem_map0_val
                                = (priv->mem_map0_base_val
                                   | start_addr_highbits);
                                goto out;
                        }
                }
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
                memcpy(copyto,
                       priv->start,
                       end_addr_lowbits);
                copyto += end_addr_lowbits;
        }

out:
        *retlen = copyto - buf;
        return 0;
}

static int pmc551_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
        struct mypriv *priv = (struct mypriv *)mtd->priv;
        u32 start_addr_highbits;
        u32 end_addr_highbits;
        u32 start_addr_lowbits;
        u32 end_addr_lowbits;
        unsigned long end;
        const u_char *copyfrom = buf;


        /* Is it past the end? */
        if (to > mtd->size) {
                return -EINVAL;
        }

        end = to + len;
        start_addr_highbits = to & PMC551_ADDR_HIGH_MASK;
        end_addr_highbits = end & PMC551_ADDR_HIGH_MASK;
        start_addr_lowbits = to & PMC551_ADDR_LOW_MASK;
        end_addr_lowbits = end & PMC551_ADDR_LOW_MASK;


        /* Only rewrite the first value if it doesn't match our current
           values.  Most operations are on the same page as the previous
           value, so this is a pretty good optimization. */
        if (priv->curr_mem_map0_val !=
                        (priv->mem_map0_base_val | start_addr_highbits)) {
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
        }

        if (start_addr_highbits == end_addr_highbits) {
                /* The whole thing fits within one access, so just one shot
                   will do it. */
                memcpy(priv->start + start_addr_lowbits,
                       copyfrom,
                       len);
                copyfrom += len;
        } else {
                /* We have to do multiple writes to get all the data
                   written. */
                memcpy(priv->start + start_addr_lowbits,
                       copyfrom,
                       priv->aperture_size - start_addr_lowbits);
                copyfrom += priv->aperture_size - start_addr_lowbits;
                start_addr_highbits += priv->aperture_size;
                while (start_addr_highbits != end_addr_highbits) {
                        pci_write_config_dword ( priv->dev,
                                                 PMC551_PCI_MEM_MAP0,
                                                 (priv->mem_map0_base_val
                                                  | start_addr_highbits));
                        memcpy(priv->start,
                               copyfrom,
                               priv->aperture_size);
                        copyfrom += priv->aperture_size;
                        start_addr_highbits += priv->aperture_size;
                        if (start_addr_highbits >= mtd->size) {
                                /* Make sure we have the right value here. */
                                priv->curr_mem_map0_val
                                = (priv->mem_map0_base_val
                                   | start_addr_highbits);
                                goto out;
                        }
                }
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
                memcpy(priv->start,
                       copyfrom,
                       end_addr_lowbits);
                copyfrom += end_addr_lowbits;
        }

out:
        *retlen = copyfrom - buf;
        return 0;
}

/*
 * Fixup routines for the V370PDC
 * PCI device ID 0x020011b0
 *
 * This function basicly kick starts the DRAM oboard the card and gets it
 * ready to be used.  Before this is done the device reads VERY erratic, so
 * much that it can crash the Linux 2.2.x series kernels when a user cat's
 * /proc/pci .. though that is mainly a kernel bug in handling the PCI DEVSEL
 * register.  FIXME: stop spinning on registers .. must implement a timeout
 * mechanism
 * returns the size of the memory region found.
 */
static u32 fixup_pmc551 (struct pci_dev *dev)
{
#ifdef PMC551_DRAM_BUG
        u32 dram_data;
#endif
        u32 size, dcmd;
        u16 cmd, i;

        /* Sanity Check */
        if(!dev) {
                return -ENODEV;
        }

        /*
         * Get the size of the memory by reading all the DRAM size values
         * and adding them up.
         *
         * KLUDGE ALERT: the boards we are using have invalid column and
         * row mux values.  We fix them here, but this will break other
         * memory configurations.
         */
#ifdef PMC551_DRAM_BUG
        pci_read_config_dword(dev, PMC551_DRAM_BLK0, &dram_data);
        size = PMC551_DRAM_BLK_GET_SIZE(dram_data);
        dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
        dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
        pci_write_config_dword(dev, PMC551_DRAM_BLK0, dram_data);

        pci_read_config_dword(dev, PMC551_DRAM_BLK1, &dram_data);
        size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
        dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
        dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
        pci_write_config_dword(dev, PMC551_DRAM_BLK1, dram_data);

        pci_read_config_dword(dev, PMC551_DRAM_BLK2, &dram_data);
        size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
        dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
        dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
        pci_write_config_dword(dev, PMC551_DRAM_BLK2, dram_data);

        pci_read_config_dword(dev, PMC551_DRAM_BLK3, &dram_data);
        size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
        dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
        dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
        pci_write_config_dword(dev, PMC551_DRAM_BLK3, dram_data);
#endif /* PMC551_DRAM_BUG */

        /*
         * Oops .. something went wrong
         */
        if( (size &= PCI_BASE_ADDRESS_MEM_MASK) == 0) {
                return -ENODEV;
        }

        /*
         * Set to be prefetchable
         */
        pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &dcmd );
        dcmd |= 0x8;

        /*
         * Put it back the way it was
         */
        pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, dcmd );
        pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &dcmd );

        /*
         * Some screen fun
         */
        printk(KERN_NOTICE "pmc551: %dM (0x%x) of %sprefetchable memory at 0x%lx\n",
               size/1024/1024, size, ((dcmd&0x8) == 0)?"non-":"",
               PCI_BASE_ADDRESS(dev)&PCI_BASE_ADDRESS_MEM_MASK );

        /*
         * Turn on PCI memory and I/O bus access just for kicks
         */
        pci_write_config_word( dev, PCI_COMMAND,
                               PCI_COMMAND_MEMORY | PCI_COMMAND_IO );

        /*
         * Config DRAM
         */
        pci_write_config_word( dev, PMC551_SDRAM_MA, 0x0400 );
        pci_write_config_word( dev, PMC551_SDRAM_CMD, 0x00bf );

        /*
         * Wait untill command has gone through
         * FIXME: register spinning issue
         */
        do { pci_read_config_word( dev, PMC551_SDRAM_CMD, &cmd );
        } while ( (PCI_COMMAND_IO) & cmd );

        /*
         * Must be held high for some duration of time to take effect??
         */
        for ( i = 1; i<=8 ; i++) {
                pci_write_config_word (dev, PMC551_SDRAM_CMD, 0x0df);

                /*
                 * Make certain command has gone through
                 * FIXME: register spinning issue
                 */
                do { pci_read_config_word(dev, PMC551_SDRAM_CMD, &cmd);
                } while ( (PCI_COMMAND_IO) & cmd );
        }

        pci_write_config_word ( dev, PMC551_SDRAM_MA, 0x0020);
        pci_write_config_word ( dev, PMC551_SDRAM_CMD, 0x0ff);

        /*
         * Wait until command completes
         * FIXME: register spinning issue
         */
        do { pci_read_config_word ( dev, PMC551_SDRAM_CMD, &cmd);
        } while ( (PCI_COMMAND_IO) & cmd );

        pci_read_config_dword ( dev, PMC551_DRAM_CFG, &dcmd);
        dcmd |= 0x02000000;
        pci_write_config_dword ( dev, PMC551_DRAM_CFG, dcmd);

        /*
         * Check to make certain fast back-to-back, if not
         * then set it so
         */
        pci_read_config_word( dev, PCI_STATUS, &cmd);
        if((cmd&PCI_COMMAND_FAST_BACK) == 0) {
                cmd |= PCI_COMMAND_FAST_BACK;
                pci_write_config_word( dev, PCI_STATUS, cmd);
        }

        /*
         * Check to make certain the DEVSEL is set correctly, this device
         * has a tendancy to assert DEVSEL and TRDY when a write is performed
         * to the memory when memory is read-only
         */
        if((cmd&PCI_STATUS_DEVSEL_MASK) != 0x0) {
                cmd &= ~PCI_STATUS_DEVSEL_MASK;
                pci_write_config_word( dev, PCI_STATUS, cmd );
        }

        /*
         * Check to see the state of the memory
         * FIXME: perhaps hide some of this around an #ifdef DEBUG as
         * it doesn't effect or enhance cards functionality
         */
        pci_read_config_dword( dev, 0x74, &dcmd );
        printk(KERN_NOTICE "pmc551: DRAM_BLK3 Flags: %s,%s\n",
               ((0x2&dcmd) == 0)?"RW":"RO",
               ((0x1&dcmd) == 0)?"Off":"On" );

        pci_read_config_dword( dev, 0x70, &dcmd );
        printk(KERN_NOTICE "pmc551: DRAM_BLK2 Flags: %s,%s\n",
               ((0x2&dcmd) == 0)?"RW":"RO",
               ((0x1&dcmd) == 0)?"Off":"On" );

        pci_read_config_dword( dev, 0x6C, &dcmd );
        printk(KERN_NOTICE "pmc551: DRAM_BLK1 Flags: %s,%s\n",
               ((0x2&dcmd) == 0)?"RW":"RO",
               ((0x1&dcmd) == 0)?"Off":"On" );

        pci_read_config_dword( dev, 0x68, &dcmd );
        printk(KERN_NOTICE "pmc551: DRAM_BLK0 Flags: %s,%s\n",
               ((0x2&dcmd) == 0)?"RW":"RO",
               ((0x1&dcmd) == 0)?"Off":"On" );

        pci_read_config_word( dev, 0x4, &cmd );
        printk( KERN_NOTICE "pmc551: Memory Access %s\n",
                ((0x2&cmd) == 0)?"off":"on" );
        printk( KERN_NOTICE "pmc551: I/O Access %s\n",
                ((0x1&cmd) == 0)?"off":"on" );

        pci_read_config_word( dev, 0x6, &cmd );
        printk( KERN_NOTICE "pmc551: Devsel %s\n",
                ((PCI_STATUS_DEVSEL_MASK&cmd)==0x000)?"Fast":
                ((PCI_STATUS_DEVSEL_MASK&cmd)==0x200)?"Medium":
                ((PCI_STATUS_DEVSEL_MASK&cmd)==0x400)?"Slow":"Invalid" );

        printk( KERN_NOTICE "pmc551: %sFast Back-to-Back\n",
                ((PCI_COMMAND_FAST_BACK&cmd) == 0)?"Not ":"" );

        return size;
}

/*
 * Kernel version specific module stuffages
 */
#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define init_pmc551 init_module
#define cleanup_pmc551 cleanup_module
#endif
#define __exit
#endif


/*
 * PMC551 Card Initialization
 */
//static int __init init_pmc551(void)
int __init init_pmc551(void)
{
        struct pci_dev *PCI_Device = NULL;
        struct mypriv *priv;
        int count, found=0;
        struct mtd_info *mtd;
        u32 length = 0;


        printk(KERN_NOTICE "Ramix PMC551 PCI Mezzanine Ram Driver. (C) 1999,2000 Nortel Networks.\n");
        printk(KERN_INFO "$Id: pmc551.c,v 1.7 2000/07/03 10:01:38 dwmw2 Exp $\n");

        if(!pci_present()) {
                printk(KERN_NOTICE "pmc551: PCI not enabled.\n");
                return -ENODEV;
        }

        /*
         * PCU-bus chipset probe.
         */
        for( count = 0; count < MAX_MTD_DEVICES; count++ ) {

                if ( (PCI_Device = pci_find_device( PCI_VENDOR_ID_V3_SEMI,
                                                    PCI_DEVICE_ID_V3_SEMI_V370PDC, PCI_Device ) ) == NULL) {
                        break;
                }

                printk(KERN_NOTICE "pmc551: Found PCI V370PDC IRQ:%d\n",
                       PCI_Device->irq);

                /*
                 * The PMC551 device acts VERY wierd if you don't init it
                 * first.  i.e. it will not correctly report devsel.  If for
                 * some reason the sdram is in a wrote-protected state the
                 * device will DEVSEL when it is written to causing problems
                 * with the oldproc.c driver in
                 * some kernels (2.2.*)
                 */
                if((length = fixup_pmc551(PCI_Device)) <= 0) {
                        printk(KERN_NOTICE "pmc551: Cannot init SDRAM\n");
                        break;
                }

                mtd = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
                if (!mtd) {
                        printk(KERN_NOTICE "pmc551: Cannot allocate new MTD device.\n");
                        break;
                }

                memset(mtd, 0, sizeof(struct mtd_info));

                priv = kmalloc (sizeof(struct mypriv), GFP_KERNEL);
                if (!priv) {
                        printk(KERN_NOTICE "pmc551: Cannot allocate new MTD device.\n");
                        kfree(mtd);
                        break;
                }
                memset(priv, 0, sizeof(*priv));
                mtd->priv = priv;

                priv->dev = PCI_Device;
                priv->aperture_size = PMC551_APERTURE_SIZE;
                priv->start = ioremap((PCI_BASE_ADDRESS(PCI_Device)
                                       & PCI_BASE_ADDRESS_MEM_MASK),
                                      priv->aperture_size);
                priv->mem_map0_base_val = (PMC551_APERTURE_VAL
                                           | PMC551_PCI_MEM_MAP_REG_EN
                                           | PMC551_PCI_MEM_MAP_ENABLE);
                priv->curr_mem_map0_val = priv->mem_map0_base_val;

                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);

                mtd->size 		= length;
                mtd->flags 		= (MTD_CLEAR_BITS
                                | MTD_SET_BITS
                                | MTD_WRITEB_WRITEABLE
                                | MTD_VOLATILE);
                mtd->erase 		= pmc551_erase;
                mtd->point 		= NULL;
                mtd->unpoint 		= pmc551_unpoint;
                mtd->read 		= pmc551_read;
                mtd->write 		= pmc551_write;
                mtd->module 		= THIS_MODULE;
                mtd->type 		= MTD_RAM;
                mtd->name 		= "PMC551 RAM board";
                mtd->erasesize 		= 0x10000;

                if (add_mtd_device(mtd)) {
                        printk(KERN_NOTICE "pmc551: Failed to register new device\n");
                        kfree(mtd->priv);
                        kfree(mtd);
                        break;
                }
                printk(KERN_NOTICE "Registered pmc551 memory device.\n");
                printk(KERN_NOTICE "Mapped %dM of memory from 0x%p to 0x%p\n",
                       priv->aperture_size/1024/1024,
                       priv->start,
                       priv->start + priv->aperture_size);
                printk(KERN_NOTICE "Total memory is %dM\n", length/1024/1024);
		priv->nextpmc551 = pmc551list;
		pmc551list = mtd;
		found++;
        }

        if( !pmc551list ) {
                printk(KERN_NOTICE "pmc551: not detected,\n");
                return -ENODEV;
        } else {
                return 0;
		printk(KERN_NOTICE "pmc551: %d pmc551 devices loaded\n", found);
	}
}

/*
 * PMC551 Card Cleanup
 */
static void __exit cleanup_pmc551(void)
{
        int found=0;
        struct mtd_info *mtd;
	struct mypriv *priv;

	while((mtd=pmc551list)) {
		priv = (struct mypriv *)mtd->priv;
		pmc551list = priv->nextpmc551;
		
		if(priv->start) 
			iounmap(((struct mypriv *)mtd->priv)->start);
		
		kfree (mtd->priv);
		del_mtd_device(mtd);
		kfree(mtd);
		found++;
	}

	printk(KERN_NOTICE "pmc551: %d pmc551 devices unloaded\n", found);
}

#if LINUX_VERSION_CODE > 0x20300
module_init(init_pmc551);
module_exit(cleanup_pmc551);
#endif




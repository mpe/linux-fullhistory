/* 
**  RCpci45.c  
**
**
**
**  ---------------------------------------------------------------------
**  ---     Copyright (c) 1998, 1999, RedCreek Communications Inc.    ---
**  ---                   All rights reserved.                        ---
**  ---------------------------------------------------------------------
**
** Written by Pete Popov and Brian Moyle.
**
** Known Problems
** 
** None known at this time.
**
**  TODO:
**      -Get rid of the wait loops in the API and replace them
**       with system independent delays ...something like
**       "delayms(2)".  However, under normal circumstances, the 
**       delays are very short so they're not a problem.
**
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.

**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.

**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**   
**  Pete Popov, January 11,99: Fixed a couple of 2.1.x problems 
**  (virt_to_bus() not called), tested it under 2.2pre5, and added a 
**  #define to enable the use of the same file for both, the 2.0.x kernels 
**  as well as the 2.1.x.
**
**  Ported to 2.1.x by Alan Cox 1998/12/9. 
**
***************************************************************************/

static char *version =
"RedCreek Communications PCI linux driver version 2.00\n";


#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/timer.h>
#include <asm/irq.h>            /* For NR_IRQS only. */
#include <asm/bitops.h>
#include <asm/io.h>

#if LINUX_VERSION_CODE >= 0x020100
#define LINUX_2_1
#endif

#ifdef LINUX_2_1
#include <asm/uaccess.h>
#endif

#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>


#define RC_LINUX_MODULE
#include "rclanmtl.h"
#include "rcif.h"

#define RUN_AT(x) (jiffies + (x))

#define NEW_MULTICAST
#include <linux/delay.h>

#ifndef LINUX_2_1
#define ioremap vremap
#define iounmap vfree
#endif

/* PCI/45 Configuration space values */
#define RC_PCI45_VENDOR_ID  0x4916
#define RC_PCI45_DEVICE_ID  0x1960

#define MAX_ETHER_SIZE        1520  
#define MAX_NMBR_RCV_BUFFERS    96
#define RC_POSTED_BUFFERS_LOW_MARK MAX_NMBR_RCV_BUFFERS-16
#define BD_SIZE 3           /* Bucket Descriptor size */
#define BD_LEN_OFFSET 2     /* Bucket Descriptor offset to length field */


/* RedCreek LAN device Target ID */
#define RC_LAN_TARGET_ID  0x10 
/* RedCreek's OSM default LAN receive Initiator */
#define DEFAULT_RECV_INIT_CONTEXT  0xA17  


static U32 DriverControlWord =  0;

static void rc_timer(unsigned long);

/*
 * Driver Private Area, DPA.
 */
typedef struct
{

    /* 
     *    pointer to the device structure which is part
     * of the interface to the Linux kernel.
     */
    struct device *dev;            
     
    char devname[8];                /* "ethN" string */
    U8     id;                        /* the AdapterID */
    U32    pci_addr;               /* the pci address of the adapter */
    U32    bus;
    U32    function;
    struct timer_list timer;        /*  timer */
    struct enet_statistics  stats; /* the statistics structure */
    struct device *next;            /* points to the next RC adapter */
    unsigned long numOutRcvBuffers;/* number of outstanding receive buffers*/
    unsigned char shutdown;
    unsigned char reboot;
    unsigned char nexus;
    PU8    PLanApiPA;             /* Pointer to Lan Api Private Area */

}
DPA, *PDPA;

#define MAX_ADAPTERS 32

static PDPA  PCIAdapters[MAX_ADAPTERS] = 
{
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};


static int RCscan(void);
static struct device 
*RCfound_device(struct device *, int, int, int, int, int, int);

static int RCprobe1(struct device *);
static int RCopen(struct device *);
static int RC_xmit_packet(struct sk_buff *, struct device *);
static void RCinterrupt(int, void *, struct pt_regs *);
static int RCclose(struct device *dev);
static struct enet_statistics *RCget_stats(struct device *);
static int RCioctl(struct device *, struct ifreq *, int);
static int RCconfig(struct device *, struct ifmap *);
static void RCxmit_callback(U32, U16, PU32, U16);
static void RCrecv_callback(U32, U8, U32, PU32, U16);
static void RCreset_callback(U32, U32, U32, U16);
static void RCreboot_callback(U32, U32, U32, U16);
static int RC_allocate_and_post_buffers(struct device *, int);


/* A list of all installed RC devices, for removing the driver module. */
static struct device *root_RCdev = NULL;

#ifdef MODULE
int init_module(void)
#else
int rcpci_probe(struct netdevice *dev)
#endif
{
    int cards_found;

    printk(version);

    root_RCdev = NULL;
    cards_found = RCscan();
#ifdef MODULE
    return cards_found ? 0 : -ENODEV;
#else
    return -1;
#endif
}

static int RCscan()
{
    int cards_found = 0;
    struct device *dev = 0;

    if (pcibios_present()) 
    {
        static int pci_index = 0;
        unsigned char pci_bus, pci_device_fn;
        int scan_status;
        int board_index = 0;

        for (;pci_index < 0xff; pci_index++) 
        {
            unsigned char pci_irq_line;
            unsigned short pci_command, vendor, device, class;
            unsigned int pci_ioaddr;


            scan_status =  
                (pcibios_find_device (RC_PCI45_VENDOR_ID, 
                                      RC_PCI45_DEVICE_ID, 
                                      pci_index, 
                                      &pci_bus, 
                                      &pci_device_fn));
#ifdef RCDEBUG
            printk("rc scan_status = 0x%X\n", scan_status);
#endif
            if (scan_status != PCIBIOS_SUCCESSFUL)
                break;
            pcibios_read_config_word(pci_bus, 
                                     pci_device_fn, 
                                     PCI_VENDOR_ID, &vendor);
            pcibios_read_config_word(pci_bus, 
                                     pci_device_fn,
                                     PCI_DEVICE_ID, &device);
            pcibios_read_config_byte(pci_bus, 
                                     pci_device_fn,
                                     PCI_INTERRUPT_LINE, &pci_irq_line);
            pcibios_read_config_dword(pci_bus, 
                                      pci_device_fn,
                                      PCI_BASE_ADDRESS_0, &pci_ioaddr);
            pcibios_read_config_word(pci_bus, 
                                     pci_device_fn,
                                     PCI_CLASS_DEVICE, &class);

            pci_ioaddr &= ~0xf;

#ifdef RCDEBUG
            printk("rc: Found RedCreek PCI adapter\n");
            printk("rc: pci class = 0x%x  0x%x \n", class, class>>8);
            printk("rc: pci_bus = %d,  pci_device_fn = %d\n", pci_bus, pci_device_fn);
            printk("rc: pci_irq_line = 0x%x \n", pci_irq_line);
            printk("rc: pci_ioaddr = 0x%x\n", pci_ioaddr);
#endif

#if 1
            if (check_region(pci_ioaddr, 2*32768))
            {
                printk("rc: check_region failed\n");
                continue;
            }
            else
            {
                printk("rc: check_region passed\n");
            }
#endif
               
            /*
             * Get and check the bus-master and latency values.
             * Some PCI BIOSes fail to set the master-enable bit.
             */

            pcibios_read_config_word(pci_bus, 
                                     pci_device_fn,
                                     PCI_COMMAND, 
                                     &pci_command);
            if ( ! (pci_command & PCI_COMMAND_MASTER)) {
                printk("rc: PCI Master Bit has not been set!\n");
                            
                pci_command |= PCI_COMMAND_MASTER;
                pcibios_write_config_word(pci_bus, 
                                          pci_device_fn,
                                          PCI_COMMAND, 
                                          pci_command);
            }
            if ( ! (pci_command & PCI_COMMAND_MEMORY)) {
                /*
                 * If the BIOS did not set the memory enable bit, what else
                 * did it not initialize?  Skip this adapter.
                 */
                printk("rc: Adapter %d, PCI Memory Bit has not been set!\n",
                       cards_found);
                printk("rc: Bios problem? \n");
                continue;
            }
                    
            dev = RCfound_device(dev, pci_ioaddr, pci_irq_line,
                                 pci_bus, pci_device_fn,
                                 board_index++, cards_found);

            if (dev) {
                dev = 0;
                cards_found++;
            }
        }
    }
    printk("rc: found %d cards \n", cards_found);
    return cards_found;
}

static struct device *
RCfound_device(struct device *dev, int memaddr, int irq, 
               int bus, int function, int product_index, int card_idx)
{
    int dev_size = 32768;        
    unsigned long *vaddr=0;
    PDPA pDpa;
    int init_status;

    /* 
     * Allocate and fill new device structure. 
     * We need enough for struct device plus DPA plus the LAN API private
     * area, which requires a minimum of 16KB.  The top of the allocated
     * area will be assigned to struct device; the next chunk will be
     * assigned to DPA; and finally, the rest will be assigned to the
     * the LAN API layer.
     */
    dev = (struct device *) kmalloc(dev_size, GFP_DMA | GFP_KERNEL |GFP_ATOMIC);
    memset(dev, 0, dev_size);
#ifdef RCDEBUG
    printk("rc: dev = 0x%08X\n", (uint)dev);
#endif 

    /*
     * dev->priv will point to the start of DPA.
     */
    dev->priv = (void *)(((long)dev + sizeof(struct device) + 15) & ~15);
    pDpa = dev->priv;
    dev->name = pDpa->devname;

    pDpa->dev = dev;            /* this is just for easy reference */
    pDpa->function = function;
    pDpa->bus = bus;
    pDpa->id = card_idx;        /* the device number */
    pDpa->pci_addr = memaddr;
    PCIAdapters[card_idx] = pDpa;
#ifdef RCDEBUG
    printk("rc: pDpa = 0x%x, id = %d \n", (uint)pDpa, (uint)pDpa->id);
#endif

    /*
     * Save the starting address of the LAN API private area.  We'll
     * pass that to RCInitI2OMsgLayer().
     */
    pDpa->PLanApiPA = (void *)(((long)pDpa + sizeof(DPA) + 0xff) & ~0xff);
#ifdef RCDEBUG
    printk("rc: pDpa->PLanApiPA = 0x%x\n", (uint)pDpa->PLanApiPA);
#endif
    
    /* The adapter is accessable through memory-access read/write, not
     * I/O read/write.  Thus, we need to map it to some virtual address
     * area in order to access the registers are normal memory.
     */
    vaddr = (ulong *) ioremap (memaddr, 2*32768);
#ifdef RCDEBUG
    printk("rc: RCfound_device: 0x%x, priv = 0x%x, vaddr = 0x%x\n", 
           (uint)dev, (uint)dev->priv, (uint)vaddr);
#endif
    dev->base_addr = (unsigned long)vaddr;
    dev->irq = irq;
    dev->interrupt = 0;

    /*
     * Request a shared interrupt line.
     */
    if ( request_irq(dev->irq, (void *)RCinterrupt,
                     SA_INTERRUPT|SA_SHIRQ, "RedCreek VPN Adapter", dev) )
    {
        printk( "RC PCI 45: %s: unable to get IRQ %d\n", (PU8)dev->name, (uint)dev->irq );
        iounmap(vaddr);
        kfree(dev);
        return 0;
    }

    init_status = RCInitI2OMsgLayer(pDpa->id, dev->base_addr, 
                                    pDpa->PLanApiPA, (PU8)virt_to_bus((void *)pDpa->PLanApiPA),
                                    (PFNTXCALLBACK)RCxmit_callback,
                                    (PFNRXCALLBACK)RCrecv_callback,
                                    (PFNCALLBACK)RCreboot_callback);
#ifdef RCDEBUG
    printk("rc: I2O msg initted: status = 0x%x\n", init_status);
#endif
    if (init_status)
    {
        printk("rc: Unable to initialize msg layer\n");
        free_irq(dev->irq, dev);
        iounmap(vaddr);
        kfree(dev);
        return 0;
    }
    if (RCGetMAC(pDpa->id, dev->dev_addr, NULL))
    {
        printk("rc: Unable to get adapter MAC\n");
        free_irq(dev->irq, dev);
        iounmap(vaddr);
        kfree(dev);
        return 0;
    }

    DriverControlWord |= WARM_REBOOT_CAPABLE;
    RCReportDriverCapability(pDpa->id, DriverControlWord);

    dev->init = RCprobe1;
    ether_setup(dev);            /* linux kernel interface */

    pDpa->next = root_RCdev;
    root_RCdev = dev;

    if (register_netdev(dev) != 0) /* linux kernel interface */
    {
        printk("rc: unable to register device \n");
        free_irq(dev->irq, dev);
        iounmap(vaddr);
        kfree(dev);
        return 0;
    }
    return dev;
}

static int RCprobe1(struct device *dev)
{
    dev->open = RCopen;
    dev->hard_start_xmit = RC_xmit_packet;
    dev->stop = RCclose;
    dev->get_stats = RCget_stats;
    dev->do_ioctl = RCioctl;
    dev->set_config = RCconfig;
    return 0;
}

static int
RCopen(struct device *dev)
{
    int post_buffers = MAX_NMBR_RCV_BUFFERS;
    PDPA pDpa = (PDPA) dev->priv;
    int count = 0;
    int requested = 0;

#ifdef RCDEBUG
    printk("rc: RCopen\n");
#endif
    RCEnableI2OInterrupts(pDpa->id);

    if (pDpa->nexus)
    {
        /* This is not the first time RCopen is called.  Thus,
         * the interface was previously opened and later closed
         * by RCclose().  RCclose() does a Shutdown; to wake up
         * the adapter, a reset is mandatory before we can post
         * receive buffers.  However, if the adapter initiated 
         * a reboot while the interface was closed -- and interrupts
         * were turned off -- we need will need to reinitialize
         * the adapter, rather than simply waking it up.  
         */
        printk("rc: Waking up adapter...\n");
        RCResetLANCard(pDpa->id,0,0,0);
    }
    else
    {
        pDpa->nexus = 1;
    }

    while(post_buffers)
    {
        if (post_buffers > MAX_NMBR_POST_BUFFERS_PER_MSG)
            requested = MAX_NMBR_POST_BUFFERS_PER_MSG;
        else
            requested = post_buffers;
        count = RC_allocate_and_post_buffers(dev, requested);

        if ( count < requested )
        {
            /*
             * Check to see if we were able to post any buffers at all.
             */
            if (post_buffers == MAX_NMBR_RCV_BUFFERS)
            {
                printk("rc: Error RCopen: not able to allocate any buffers\r\n");
                return(-ENOMEM);                    
            }
            printk("rc: Warning RCopen: not able to allocate all requested buffers\r\n");
            break;            /* we'll try to post more buffers later */
        }
        else
            post_buffers -= count;
    }
    pDpa->numOutRcvBuffers = MAX_NMBR_RCV_BUFFERS - post_buffers;
    pDpa->shutdown = 0;        /* just in case */
#ifdef RCDEBUG
    printk("rc: RCopen: posted %d buffers\n", (uint)pDpa->numOutRcvBuffers);
#endif
    MOD_INC_USE_COUNT;
    return 0;
}

static int
RC_xmit_packet(struct sk_buff *skb, struct device *dev)
{

    PDPA pDpa = (PDPA) dev->priv;
    singleTCB tcb;
    psingleTCB ptcb = &tcb;
    RC_RETURN status = 0;
    
        if (dev->tbusy || pDpa->shutdown || pDpa->reboot)
        {
#ifdef RCDEBUG
            printk("rc: RC_xmit_packet: tbusy!\n");
#endif
            return 1;
        }
      
    if ( skb->len <= 0 ) 
    {
        printk("RC_xmit_packet: skb->len less than 0!\n");
        return 0;
    }

    /*
     * The user is free to reuse the TCB after RCI2OSendPacket() returns, since
     * the function copies the necessary info into its own private space.  Thus,
     * our TCB can be a local structure.  The skb, on the other hand, will be
     * freed up in our interrupt handler.
     */
    ptcb->bcount = 1;
    /* 
     * we'll get the context when the adapter interrupts us to tell us that
     * the transmision is done. At that time, we can free skb.
     */
    ptcb->b.context = (U32)skb;    
    ptcb->b.scount = 1;
    ptcb->b.size = skb->len;
    ptcb->b.addr = virt_to_bus((void *)skb->data);

#ifdef RCDEBUG
    printk("rc: RC xmit: skb = 0x%x, pDpa = 0x%x, id = %d, ptcb = 0x%x\n", 
           (uint)skb, (uint)pDpa, (uint)pDpa->id, (uint)ptcb);
#endif
    if ( (status = RCI2OSendPacket(pDpa->id, (U32)NULL, (PRCTCB)ptcb))
         != RC_RTN_NO_ERROR)
    {
#ifdef RCDEBUG
        printk("rc: RC send error 0x%x\n", (uint)status);
#endif
        dev->tbusy = 1;
        return 1;
    }
    else
    {
        dev->trans_start = jiffies;
        //       dev->tbusy = 0;
    }
    /*
     * That's it!
     */
    return 0;
}

/*
 * RCxmit_callback()
 *
 * The transmit callback routine. It's called by RCProcI2OMsgQ()
 * because the adapter is done with one or more transmit buffers and
 * it's returning them to us, or we asked the adapter to return the
 * outstanding transmit buffers by calling RCResetLANCard() with 
 * RC_RESOURCE_RETURN_PEND_TX_BUFFERS flag. 
 * All we need to do is free the buffers.
 */
static void 
RCxmit_callback(U32 Status, 
                U16 PcktCount, 
                PU32 BufferContext, 
                U16 AdapterID)
{
    struct sk_buff *skb;
    PDPA pDpa;
    struct device *dev;

        pDpa = PCIAdapters[AdapterID];
        if (!pDpa)
        {
            printk("rc: Fatal error: xmit callback, !pDpa\n");
            return;
        }
        dev = pDpa->dev;

        // printk("xmit_callback: Status = 0x%x\n", (uint)Status);
        if (Status != I2O_REPLY_STATUS_SUCCESS)
        {
            printk("rc: xmit_callback: Status = 0x%x\n", (uint)Status);
        }
#ifdef RCDEBUG
        if (pDpa->shutdown || pDpa->reboot)
            printk("rc: xmit callback: shutdown||reboot\n");
#endif

#ifdef RCDEBUG     
        printk("rc: xmit_callback: PcktCount = %d, BC = 0x%x\n", 
               (uint)PcktCount, (uint)BufferContext);
#endif
        while (PcktCount--)
        {
            skb = (struct sk_buff *)(BufferContext[0]);
#ifdef RCDEBUG
            printk("rc: skb = 0x%x\n", (uint)skb);
#endif
            BufferContext++;
#ifdef LINUX_2_1
            dev_kfree_skb (skb);
#else
            dev_kfree_skb (skb, FREE_WRITE);
#endif
        }
        dev->tbusy = 0;

}

static void
RCreset_callback(U32 Status, U32 p1, U32 p2, U16 AdapterID)
{
    PDPA pDpa;
    struct device *dev;
     
    pDpa = PCIAdapters[AdapterID];
    dev = pDpa->dev;
#ifdef RCDEBUG
    printk("rc: RCreset_callback Status 0x%x\n", (uint)Status);
#endif
    /*
     * Check to see why we were called.
     */
    if (pDpa->shutdown)
    {
        printk("rc: Shutting down interface\n");
        pDpa->shutdown = 0;
        pDpa->reboot = 0;
        MOD_DEC_USE_COUNT; 
    }
    else if (pDpa->reboot)
    {
        printk("rc: reboot, shutdown adapter\n");
        /*
         * We don't set any of the flags in RCShutdownLANCard()
         * and we don't pass a callback routine to it.
         * The adapter will have already initiated the reboot by
         * the time the function returns.
         */
        RCDisableI2OInterrupts(pDpa->id);
        RCShutdownLANCard(pDpa->id,0,0,0);
        printk("rc: scheduling timer...\n");
        init_timer(&pDpa->timer);
        pDpa->timer.expires = RUN_AT((30*HZ)/10); /* 3 sec. */
        pDpa->timer.data = (unsigned long)dev;
        pDpa->timer.function = &rc_timer;    /* timer handler */
        add_timer(&pDpa->timer);
    }



}

static void
RCreboot_callback(U32 Status, U32 p1, U32 p2, U16 AdapterID)
{
    PDPA pDpa;
    
    pDpa = PCIAdapters[AdapterID];
#ifdef RCDEBUG
    printk("rc: RCreboot: rcv buffers outstanding = %d\n", 
           (uint)pDpa->numOutRcvBuffers);
#endif
    if (pDpa->shutdown)
    {
        printk("rc: skipping reboot sequence -- shutdown already initiated\n");
        return;
    }
    pDpa->reboot = 1;
    /*
     * OK, we reset the adapter and ask it to return all
     * outstanding transmit buffers as well as the posted
     * receive buffers.  When the adapter is done returning
     * those buffers, it will call our RCreset_callback() 
     * routine.  In that routine, we'll call RCShutdownLANCard()
     * to tell the adapter that it's OK to start the reboot and
     * schedule a timer callback routine to execute 3 seconds 
     * later; this routine will reinitialize the adapter at that time.
     */
    RCResetLANCard(pDpa->id, 
                   RC_RESOURCE_RETURN_POSTED_RX_BUCKETS | 
                   RC_RESOURCE_RETURN_PEND_TX_BUFFERS,0,
                   (PFNCALLBACK)RCreset_callback);
}


int broadcast_packet(unsigned char * address)
{
    int i;
    for (i=0; i<6; i++)
        if (address[i] != 0xff) return 0;

    return 1;
}

/*
 * RCrecv_callback()
 * 
 * The receive packet callback routine.  This is called by
 * RCProcI2OMsgQ() after the adapter posts buffers which have been
 * filled (one ethernet packet per buffer).
 */
static void
RCrecv_callback(U32  Status, 
                U8   PktCount, 
                U32  BucketsRemain, 
                PU32 PacketDescBlock, 
                U16  AdapterID)
{

    U32 len, count;
    PDPA pDpa;
    struct sk_buff *skb;
    struct device *dev;
    singleTCB tcb;
    psingleTCB ptcb = &tcb;


        pDpa = PCIAdapters[AdapterID];
        dev = pDpa->dev;

        ptcb->bcount = 1;

#ifdef RCDEBUG
        printk("rc: RCrecv_callback: 0x%x, 0x%x, 0x%x\n",
               (uint)PktCount, (uint)BucketsRemain, (uint)PacketDescBlock);
#endif
        
#ifdef RCDEBUG
        if ((pDpa->shutdown || pDpa->reboot) && !Status)
            printk("shutdown||reboot && !Status: PktCount = %d\n",PktCount);
#endif

        if ( (Status != I2O_REPLY_STATUS_SUCCESS) || pDpa->shutdown)
        {
            /*
             * Free whatever buffers the adapter returned, but don't
             * pass them to the kernel.
             */
        
                if (!pDpa->shutdown && !pDpa->reboot)
                    printk("rc: RCrecv error: status = 0x%x\n", (uint)Status);
                else
                    printk("rc: Returning %d buffers, status = 0x%x\n", 
                           PktCount, (uint)Status);
            /*
             * TO DO: check the nature of the failure and put the adapter in
             * failed mode if it's a hard failure.  Send a reset to the adapter
             * and free all outstanding memory.
             */
            if (Status == I2O_REPLY_STATUS_ABORT_NO_DATA_TRANSFER)
            {
#ifdef RCDEBUG
                printk("RCrecv status ABORT NO DATA TRANSFER\n");
#endif
            }
            /* check for reset status: I2O_REPLY_STATUS_ABORT_NO_DATA_TRANSFER */ 
            if (PacketDescBlock)
            {
                while(PktCount--)
                {
                    skb = (struct sk_buff *)PacketDescBlock[0];
#ifndef LINUX_2_1
                    skb->free = 1;
                    skb->lock = 0;    
#endif
#ifdef RCDEBUG
                    printk("free skb 0x%p\n", skb);
#endif
#ifdef LINUX_2_1
                    dev_kfree_skb (skb);
#else
                    dev_kfree_skb(skb, FREE_READ);
#endif
                    pDpa->numOutRcvBuffers--;
                    PacketDescBlock += BD_SIZE; /* point to next context field */
                }
            }
            return;
        }
        else
        {
            while(PktCount--)
            {
                skb = (struct sk_buff *)PacketDescBlock[0];
#ifdef RCDEBUG
                if (pDpa->shutdown)
                    printk("shutdown: skb=0x%x\n", (uint)skb);

                printk("skb = 0x%x: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", (uint)skb,
                       (uint)skb->data[0], (uint)skb->data[1], (uint)skb->data[2],
                       (uint)skb->data[3], (uint)skb->data[4], (uint)skb->data[5]);
#endif
                if ( (memcmp(dev->dev_addr, skb->data, 6)) &&
                     (!broadcast_packet(skb->data)))
                {
                    /*
                     * Re-post the buffer to the adapter.  Since the adapter usually
                     * return 1 to 2 receive buffers at a time, it's not too inefficient
                     * post one buffer at a time but ... may be that should be 
                     * optimized at some point.
                     */
                    ptcb->b.context = (U32)skb;    
                    ptcb->b.scount = 1;
                    ptcb->b.size = MAX_ETHER_SIZE;
                    ptcb->b.addr = virt_to_bus((void *)skb->data);

                    if ( RCPostRecvBuffers(pDpa->id, (PRCTCB)ptcb ) != RC_RTN_NO_ERROR)
                    {
                        printk("rc: RCrecv_callback: post buffer failed!\n");
#ifdef LINUX_2_1
                        dev_kfree_skb (skb);
#else
                        skb->free = 1;
                        dev_kfree_skb(skb, FREE_READ);
#endif
                    }
                    else
                    {
                        pDpa->numOutRcvBuffers++;
                    }
                }
                else
                {
                    len = PacketDescBlock[2];
                    skb->dev = dev;
                    skb_put( skb, len ); /* adjust length and tail */
                    skb->protocol = eth_type_trans(skb, dev);
                    netif_rx(skb);    /* send the packet to the kernel */
                    dev->last_rx = jiffies;
                }
                pDpa->numOutRcvBuffers--;
                PacketDescBlock += BD_SIZE; /* point to next context field */
            }
        } 
    
        /*
         * Replenish the posted receive buffers. 
         * DO NOT replenish buffers if the driver has already
         * initiated a reboot or shutdown!
         */

        if (!pDpa->shutdown && !pDpa->reboot)
        {
            count = RC_allocate_and_post_buffers(dev, 
                                                 MAX_NMBR_RCV_BUFFERS-pDpa->numOutRcvBuffers);
            pDpa->numOutRcvBuffers += count;
        }

}

/*
 * RCinterrupt()
 * 
 * Interrupt handler. 
 * This routine sets up a couple of pointers and calls
 * RCProcI2OMsgQ(), which in turn process the message and
 * calls one of our callback functions.
 */
static void 
RCinterrupt(int irq, void *dev_id, struct pt_regs *regs)
{

    PDPA pDpa;
    struct device *dev = (struct device *)(dev_id);

    pDpa = (PDPA) (dev->priv);
     
    if (pDpa->shutdown)
        printk("rc: shutdown: service irq\n");

#ifdef RCDEBUG
    printk("RC irq: pDpa = 0x%x, dev = 0x%x, id = %d\n", 
           (uint)pDpa, (uint)dev, (uint)pDpa->id);
    printk("dev = 0x%x\n", (uint)dev);
#endif
    if (dev->interrupt)
        printk("%s: Re-entering the interrupt handler.\n", dev->name);
    dev->interrupt = 1;

    RCProcI2OMsgQ(pDpa->id);
    dev->interrupt = 0;

    return;
}

#define REBOOT_REINIT_RETRY_LIMIT 10
static void rc_timer(unsigned long data)
{
    struct device *dev = (struct device *)data;
    PDPA pDpa = (PDPA) (dev->priv);
    int init_status;
    static int retry = 0;
    int post_buffers = MAX_NMBR_RCV_BUFFERS;
    int count = 0;
    int requested = 0;

    if (pDpa->reboot)
    {

        init_status = RCInitI2OMsgLayer(pDpa->id, dev->base_addr, 
                                        pDpa->PLanApiPA, pDpa->PLanApiPA,
                                        (PFNTXCALLBACK)RCxmit_callback,
                                        (PFNRXCALLBACK)RCrecv_callback,
                                        (PFNCALLBACK)RCreboot_callback);

        switch(init_status)
        {
        case RC_RTN_NO_ERROR:
 
            pDpa->reboot = 0;
            pDpa->shutdown = 0;        /* just in case */
            RCReportDriverCapability(pDpa->id, DriverControlWord);
            RCEnableI2OInterrupts(pDpa->id);

            if (dev->flags & IFF_UP)
            {
                while(post_buffers)
                {
                    if (post_buffers > MAX_NMBR_POST_BUFFERS_PER_MSG)
                        requested = MAX_NMBR_POST_BUFFERS_PER_MSG;
                    else
                        requested = post_buffers;
                    count = RC_allocate_and_post_buffers(dev, requested);
                    post_buffers -= count;
                    if ( count < requested )
                        break;
                }
                pDpa->numOutRcvBuffers = 
                    MAX_NMBR_RCV_BUFFERS - post_buffers;
                printk("rc: posted %d buffers \r\n", 
                       (uint)pDpa->numOutRcvBuffers);
            }
            printk("rc: Initialization done.\n");
            return;
        case RC_RTN_FREE_Q_EMPTY:
            retry++;
            printk("rc: inbound free q emtpy\n");
            break;
        default:
            retry++;
            printk("rc: unexpected bad status after reboot\n");    
            break;
        }

        if (retry > REBOOT_REINIT_RETRY_LIMIT)
        {
            printk("rc: unable to reinitialize adapter after reboot\n");
            printk("rc: decrementing driver and closing interface\n");
            RCDisableI2OInterrupts(pDpa->id);
            dev->flags &= ~IFF_UP;
            MOD_DEC_USE_COUNT; 
        }
        else
        {
            printk("rc: rescheduling timer...\n");
            init_timer(&pDpa->timer);
            pDpa->timer.expires = RUN_AT((30*HZ)/10); /* 3 sec. */
            pDpa->timer.data = (unsigned long)dev;
            pDpa->timer.function = &rc_timer;    /* timer handler */
            add_timer(&pDpa->timer);
        }
    }
    else
    {
        printk("rc: timer??\n");
    }
}

static int
RCclose(struct device *dev)
{

    PDPA pDpa = (PDPA) dev->priv;

#ifdef RCDEBUG
    printk("rc: RCclose\r\n");
#endif
    if (pDpa->reboot)
    {
        printk("rc: skipping reset -- adapter already in reboot mode\n");
        dev->flags &= ~IFF_UP;
        pDpa->shutdown = 1;
        return 0;
    }
#ifdef RCDEBUG
    printk("rc: receive buffers outstanding: %d\n", 
           (uint)pDpa->numOutRcvBuffers);
#endif

    pDpa->shutdown = 1;

    /*
     * We can't allow the driver to be unloaded until the adapter returns
     * all posted receive buffers.  It doesn't hurt to tell the adapter
     * to return all posted receive buffers and outstanding xmit buffers,
     * even if there are none.
     */

    RCShutdownLANCard(pDpa->id, 
                      RC_RESOURCE_RETURN_POSTED_RX_BUCKETS | 
                      RC_RESOURCE_RETURN_PEND_TX_BUFFERS,0,
                      (PFNCALLBACK)RCreset_callback);

        dev->flags &= ~IFF_UP;
    return 0;
}

static struct enet_statistics *
RCget_stats(struct device *dev)
{
    RCLINKSTATS    RCstats;
   
    PDPA pDpa = dev->priv;

    if (!pDpa)
    {
        printk("rc: RCget_stats: !pDpa\n");
        return 0;
    }
    else if (!(dev->flags & IFF_UP))    
    {
#ifdef RCDEBUG
        printk("rc: RCget_stats: device down\n");
#endif
        return 0;
    }

    memset(&RCstats, 0, sizeof(RCLINKSTATS));
    if ( (RCGetLinkStatistics(pDpa->id, &RCstats, (void *)0)) == RC_RTN_NO_ERROR )
    {
#ifdef RCDEBUG
        printk("rc: TX_good 0x%x\n", (uint)RCstats.TX_good);
        printk("rc: TX_maxcol 0x%x\n", (uint)RCstats.TX_maxcol);
        printk("rc: TX_latecol 0x%x\n", (uint)RCstats.TX_latecol);
        printk("rc: TX_urun 0x%x\n", (uint)RCstats.TX_urun);
        printk("rc: TX_crs 0x%x\n", (uint)RCstats.TX_crs);
        printk("rc: TX_def 0x%x\n", (uint)RCstats.TX_def);
        printk("rc: TX_singlecol 0x%x\n", (uint)RCstats.TX_singlecol);
        printk("rc: TX_multcol 0x%x\n", (uint)RCstats.TX_multcol);
        printk("rc: TX_totcol 0x%x\n", (uint)RCstats.TX_totcol);

        printk("rc: Rcv_good 0x%x\n", (uint)RCstats.Rcv_good);
        printk("rc: Rcv_CRCerr 0x%x\n", (uint)RCstats.Rcv_CRCerr);
        printk("rc: Rcv_alignerr 0x%x\n", (uint)RCstats.Rcv_alignerr);
        printk("rc: Rcv_reserr 0x%x\n", (uint)RCstats.Rcv_reserr);
        printk("rc: Rcv_orun 0x%x\n", (uint)RCstats.Rcv_orun);
        printk("rc: Rcv_cdt 0x%x\n", (uint)RCstats.Rcv_cdt);
        printk("rc: Rcv_runt 0x%x\n", (uint)RCstats.Rcv_runt);
#endif

        pDpa->stats.rx_packets = RCstats.Rcv_good; /* total packets received    */
        pDpa->stats.tx_packets = RCstats.TX_good; /* total packets transmitted    */

        pDpa->stats.rx_errors = 
            RCstats.Rcv_CRCerr +
            RCstats.Rcv_alignerr + 
            RCstats.Rcv_reserr + 
            RCstats.Rcv_orun + 
            RCstats.Rcv_cdt + 
            RCstats.Rcv_runt; /* bad packets received        */

        pDpa->stats.tx_errors = 
            RCstats.TX_urun + 
            RCstats.TX_crs + 
            RCstats.TX_def + 
            RCstats.TX_totcol; /* packet transmit problems    */

        /*
         * This needs improvement.
         */
        pDpa->stats.rx_dropped = 0;        /* no space in linux buffers    */
        pDpa->stats.tx_dropped = 0;        /* no space available in linux    */
        pDpa->stats.multicast = 0;        /* multicast packets received    */
        pDpa->stats.collisions = RCstats.TX_totcol;

        /* detailed rx_errors: */
        pDpa->stats.rx_length_errors = 0;
        pDpa->stats.rx_over_errors = RCstats.Rcv_orun; /* receiver ring buff overflow    */
        pDpa->stats.rx_crc_errors = RCstats.Rcv_CRCerr;    /* recved pkt with crc error    */
        pDpa->stats.rx_frame_errors = 0; /* recv'd frame alignment error */
        pDpa->stats.rx_fifo_errors = 0; /* recv'r fifo overrun        */
        pDpa->stats.rx_missed_errors = 0; /* receiver missed packet    */

        /* detailed tx_errors */
        pDpa->stats.tx_aborted_errors = 0;
        pDpa->stats.tx_carrier_errors = 0;
        pDpa->stats.tx_fifo_errors = 0;
        pDpa->stats.tx_heartbeat_errors = 0;
        pDpa->stats.tx_window_errors = 0;

        return ((struct enet_statistics *)&(pDpa->stats));
    }
    return 0;
}

static int RCioctl(struct device *dev, struct ifreq *rq, int cmd)
{
    RCuser_struct RCuser;
    PDPA pDpa = dev->priv;

#if RCDEBUG
    printk("RCioctl: cmd = 0x%x\n", cmd);
#endif
 
    switch (cmd)  {
 
    case RCU_PROTOCOL_REV:
        /*
         * Assign user protocol revision, to tell user-level
         * controller program whether or not it's in sync.
         */
        rq->ifr_ifru.ifru_data = (caddr_t) USER_PROTOCOL_REV;
        break;
  

    case RCU_COMMAND:
    {
#ifdef LINUX_2_1
        if(copy_from_user(&RCuser, rq->ifr_data, sizeof(RCuser)))
             return -EFAULT;
#else
        int error;
        error=verify_area(VERIFY_WRITE, rq->ifr_data, sizeof(RCuser));
        if (error)  {
            return error;
        }
        memcpy_fromfs(&RCuser, rq->ifr_data, sizeof(RCuser));
#endif
        
#ifdef RCDEBUG
        printk("RCioctl: RCuser_cmd = 0x%x\n", RCuser.cmd);
#endif
  
        switch(RCuser.cmd)
        {
        case RCUC_GETFWVER:
            printk("RC GETFWVER\n");
            RCUD_GETFWVER = &RCuser.RCUS_GETFWVER;
            RCGetFirmwareVer(pDpa->id, (PU8) &RCUD_GETFWVER->FirmString, NULL);
            break;
        case RCUC_GETINFO:
            printk("RC GETINFO\n");
            RCUD_GETINFO = &RCuser.RCUS_GETINFO;
            RCUD_GETINFO -> mem_start = dev->base_addr;
            RCUD_GETINFO -> mem_end = dev->base_addr + 2*32768;
            RCUD_GETINFO -> base_addr = pDpa->pci_addr;
            RCUD_GETINFO -> irq = dev->irq;
            break;
        case RCUC_GETIPANDMASK:
            printk("RC GETIPANDMASK\n");
            RCUD_GETIPANDMASK = &RCuser.RCUS_GETIPANDMASK;
            RCGetRavlinIPandMask(pDpa->id, (PU32) &RCUD_GETIPANDMASK->IpAddr,
                                 (PU32) &RCUD_GETIPANDMASK->NetMask, NULL);
            break;
        case RCUC_GETLINKSTATISTICS:
            printk("RC GETLINKSTATISTICS\n");
            RCUD_GETLINKSTATISTICS = &RCuser.RCUS_GETLINKSTATISTICS;
            RCGetLinkStatistics(pDpa->id, (P_RCLINKSTATS) &RCUD_GETLINKSTATISTICS->StatsReturn, NULL);
            break;
        case RCUC_GETLINKSTATUS:
            printk("RC GETLINKSTATUS\n");
            RCUD_GETLINKSTATUS = &RCuser.RCUS_GETLINKSTATUS;
            RCGetLinkStatus(pDpa->id, (PU32) &RCUD_GETLINKSTATUS->ReturnStatus, NULL);
            break;
        case RCUC_GETMAC:
            printk("RC GETMAC\n");
            RCUD_GETMAC = &RCuser.RCUS_GETMAC;
            RCGetMAC(pDpa->id, (PU8) &RCUD_GETMAC->mac, NULL);
            break;
        case RCUC_GETPROM:
            printk("RC GETPROM\n");
            RCUD_GETPROM = &RCuser.RCUS_GETPROM;
            RCGetPromiscuousMode(pDpa->id, (PU32) &RCUD_GETPROM->PromMode, NULL);
            break;
        case RCUC_GETBROADCAST:
            printk("RC GETBROADCAST\n");
            RCUD_GETBROADCAST = &RCuser.RCUS_GETBROADCAST;
            RCGetBroadcastMode(pDpa->id, (PU32) &RCUD_GETBROADCAST->BroadcastMode, NULL);
            break;
        case RCUC_GETSPEED:
            printk("RC GETSPEED\n");
            if (!(dev->flags & IFF_UP))    
            {
                printk("RCioctl, GETSPEED error: interface down\n");
                return -ENODATA;
            }
            RCUD_GETSPEED = &RCuser.RCUS_GETSPEED;
            RCGetLinkSpeed(pDpa->id, (PU32) &RCUD_GETSPEED->LinkSpeedCode, NULL);
            printk("RC speed = 0x%ld\n", RCUD_GETSPEED->LinkSpeedCode);
            break;
        case RCUC_SETIPANDMASK:
            printk("RC SETIPANDMASK\n");
            RCUD_SETIPANDMASK = &RCuser.RCUS_SETIPANDMASK;
            printk ("RC New IP Addr = %d.%d.%d.%d, ", (U8) ((RCUD_SETIPANDMASK->IpAddr) & 0xff),
                    (U8) ((RCUD_SETIPANDMASK->IpAddr >>  8) & 0xff),
                    (U8) ((RCUD_SETIPANDMASK->IpAddr >> 16) & 0xff),
                    (U8) ((RCUD_SETIPANDMASK->IpAddr >> 24) & 0xff));
            printk ("RC New Mask = %d.%d.%d.%d\n", (U8) ((RCUD_SETIPANDMASK->NetMask) & 0xff),
                    (U8) ((RCUD_SETIPANDMASK->NetMask >>  8) & 0xff),
                    (U8) ((RCUD_SETIPANDMASK->NetMask >> 16) & 0xff),
                    (U8) ((RCUD_SETIPANDMASK->NetMask >> 24) & 0xff));
            RCSetRavlinIPandMask(pDpa->id, (U32) RCUD_SETIPANDMASK->IpAddr,
                                 (U32) RCUD_SETIPANDMASK->NetMask);
            break;
        case RCUC_SETMAC:
            printk("RC SETMAC\n");
            RCUD_SETMAC = &RCuser.RCUS_SETMAC;
            printk ("RC New MAC addr = %02X:%02X:%02X:%02X:%02X:%02X\n",
                    (U8) (RCUD_SETMAC->mac[0]), (U8) (RCUD_SETMAC->mac[1]), (U8) (RCUD_SETMAC->mac[2]),
                    (U8) (RCUD_SETMAC->mac[3]), (U8) (RCUD_SETMAC->mac[4]), (U8) (RCUD_SETMAC->mac[5]));
            RCSetMAC(pDpa->id, (PU8) &RCUD_SETMAC->mac);
            break;
        case RCUC_SETSPEED:
            printk("RC SETSPEED\n");
            RCUD_SETSPEED = &RCuser.RCUS_SETSPEED;
            RCSetLinkSpeed(pDpa->id, (U16) RCUD_SETSPEED->LinkSpeedCode);
            printk("RC New speed = 0x%d\n", RCUD_SETSPEED->LinkSpeedCode);
            break;
        case RCUC_SETPROM:
            printk("RC SETPROM\n");
            RCUD_SETPROM = &RCuser.RCUS_SETPROM;
            RCSetPromiscuousMode(pDpa->id,(U16)RCUD_SETPROM->PromMode);
            printk("RC New prom mode = 0x%d\n", RCUD_SETPROM->PromMode);
            break;
        case RCUC_SETBROADCAST:
            printk("RC SETBROADCAST\n");
            RCUD_SETBROADCAST = &RCuser.RCUS_SETBROADCAST;
            RCSetBroadcastMode(pDpa->id,(U16)RCUD_SETBROADCAST->BroadcastMode);
            printk("RC New broadcast mode = 0x%d\n", RCUD_SETBROADCAST->BroadcastMode);
            break;
        default:
            printk("RC command default\n");
            RCUD_DEFAULT = &RCuser.RCUS_DEFAULT;
            RCUD_DEFAULT -> rc = 0x11223344;
            break;
        }
#ifdef LINUX_2_1
        copy_to_user(rq->ifr_data, &RCuser, sizeof(RCuser));
#else
        memcpy_tofs(rq->ifr_data, &RCuser, sizeof(RCuser));
#endif
        break;
    }   /* RCU_COMMAND */ 

    default:
        printk("RC default\n");
        rq->ifr_ifru.ifru_data = (caddr_t) 0x12345678;
        break;
    }
    return 0;
}

static int RCconfig(struct device *dev, struct ifmap *map)
{
    /*
     * To be completed ...
      */
    printk("rc: RCconfig\n");
    return 0;
    if (dev->flags & IFF_UP)    /* can't act on a running interface */
        return -EBUSY;

     /* Don't allow changing the I/O address */
    if (map->base_addr != dev->base_addr) {
        printk(KERN_WARNING "RC pci45: Change I/O address not implemented\n");
        return -EOPNOTSUPP;
    }
    return 0;
}

void
cleanup_module(void)
{
    PDPA pDpa;
    struct device *next;


#ifdef RCDEBUG
    printk("rc: RC cleanup_module\n");
    printk("rc: root_RCdev = 0x%x\n", (uint)root_RCdev);
#endif


    while (root_RCdev)
    {
        pDpa = (PDPA) root_RCdev->priv;
#ifdef RCDEBUG
        printk("rc: cleanup 0x%08X\n", (uint)root_RCdev);
#endif
        printk("IOP reset: 0x%x\n", RCResetIOP(pDpa->id));
        unregister_netdev(root_RCdev);
        next = pDpa->next;

        iounmap((unsigned long *)root_RCdev->base_addr); 
        free_irq( root_RCdev->irq, root_RCdev );
        kfree(root_RCdev);
        root_RCdev = next;
    }
}

static int
RC_allocate_and_post_buffers(struct device *dev, int numBuffers)
{

    int i;
    PDPA pDpa = (PDPA)dev->priv;
    PU32 p;
    psingleB pB;
    struct sk_buff *skb;
    RC_RETURN status;

    if (!numBuffers)
        return 0;
    else if (numBuffers > MAX_NMBR_POST_BUFFERS_PER_MSG)
    {
#ifdef RCDEBUG
        printk("rc: Too many buffers requested!\n");
        printk("rc: attempting to allocate only 32 buffers\n");
#endif
        numBuffers = 32;
    }
    
    p = (PU32) kmalloc(sizeof(U32) + numBuffers*sizeof(singleB), GFP_ATOMIC);

#ifdef RCDEBUG
    printk("rc: TCB = 0x%x\n", (uint)p);
#endif

    if (!p)
    {
        printk("rc: RCopen: unable to allocate TCB\n");
        return 0;
    }

    p[0] = 0;                              /* Buffer Count */
    pB = (psingleB)((U32)p + sizeof(U32)); /* point to the first buffer */

#ifdef RCDEBUG
    printk("rc: p[0] = 0x%x, p = 0x%x, pB = 0x%x\n", (uint)p[0], (uint)p, (uint)pB);
    printk("rc: pB = 0x%x\n", (uint)pB);
#endif

    for (i=0; i<numBuffers; i++)
    {
        skb = dev_alloc_skb(MAX_ETHER_SIZE+2);
        if (!skb)
        {
            printk("rc: Doh! RCopen: unable to allocate enough skbs!\n");
            if (*p != 0)        /* did we allocate any buffers at all? */
            {
#ifdef RCDEBUG
                printk("rc: will post only %d buffers \n", (uint)(*p));
#endif
                break;
            }
            else 
            {
                kfree(p);    /* Free the TCB */
                return 0;
            }
        }
#ifdef RCDEBUG
        printk("post 0x%x\n", (uint)skb);
#endif
        skb_reserve(skb, 2);    /* Align IP on 16 byte boundaries */
        pB->context = (U32)skb;
        pB->scount = 1;        /* segment count */
        pB->size = MAX_ETHER_SIZE;
        pB->addr = virt_to_bus((void *)skb->data);
        p[0]++;
        pB++;
    }

    if ( (status = RCPostRecvBuffers(pDpa->id, (PRCTCB)p )) != RC_RTN_NO_ERROR)
    {
        printk("rc: Post buffer failed with error code 0x%x!\n", status);
        pB = (psingleB)((U32)p + sizeof(U32)); /* point to the first buffer */
        while(p[0])
        {
            skb = (struct sk_buff *)pB->context;
#ifndef LINUX_2_1
            skb->free = 1;    
#endif
#ifdef RCDEBUG
            printk("rc: freeing 0x%x\n", (uint)skb);
#endif
#ifdef LINUX_2_1
            dev_kfree_skb (skb);
#else
            dev_kfree_skb(skb, FREE_READ);
#endif
            p[0]--;
            pB++;
        }
#ifdef RCDEBUG
        printk("rc: freed all buffers, p[0] = %ld\n", p[0]);
#endif
    }
    kfree(p);
    return(p[0]);                /* return the number of posted buffers */
}

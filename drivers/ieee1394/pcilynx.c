/*
 * ti_pcilynx.c - Texas Instruments PCILynx driver
 * Copyright (C) 1999,2000 Andreas Bombe <andreas.bombe@munich.netsurf.de>,
 *                         Stephan Linz <linz@mazet.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * Lynx DMA usage:
 *
 * 0 is used for Lynx local bus transfers
 * 1 is async/selfid receive
 * 2 is iso receive
 * 3 is async transmit
 */


#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "pcilynx.h"


#if MAX_PCILYNX_CARDS > PCILYNX_MINOR_ROM_START
#error Max number of cards is bigger than PCILYNX_MINOR_ROM_START - this does not work.
#endif

/* print general (card independent) information */
#define PRINT_G(level, fmt, args...) printk(level "pcilynx: " fmt "\n" , ## args)
/* print card specific information */
#define PRINT(level, card, fmt, args...) printk(level "pcilynx%d: " fmt "\n" , card , ## args)


static struct ti_lynx cards[MAX_PCILYNX_CARDS];
static int num_of_cards = 0;


/*
 * PCL handling functions.
 */

static pcl_t alloc_pcl(struct ti_lynx *lynx)
{
        u8 m;
        int i, j;

        spin_lock(&lynx->lock);
        /* FIXME - use ffz() to make this readable */
        for (i = 0; i < LOCALRAM_SIZE; i++) {
                m = lynx->pcl_bmap[i];
                for (j = 0; j < 8; j++) {
                        if (m & 1<<j) {
                                continue;
                        }
                        m |= 1<<j;
                        lynx->pcl_bmap[i] = m;
                        spin_unlock(&lynx->lock);
                        return 8 * i + j;
                }
        }
        spin_unlock(&lynx->lock);

        return -1;
}

static void free_pcl(struct ti_lynx *lynx, pcl_t pclid)
{
        int off, bit;

        off = pclid / 8;
        bit = pclid % 8;

        if (pclid < 0) {
                return;
        }

        spin_lock(&lynx->lock);
        if (lynx->pcl_bmap[off] & 1<<bit) {
                lynx->pcl_bmap[off] &= ~(1<<bit);
        } else {
                PRINT(KERN_ERR, lynx->id, 
                      "attempted to free unallocated PCL %d", pclid);
        }
        spin_unlock(&lynx->lock);
}

/* functions useful for debugging */        
static void pretty_print_pcl(const struct ti_pcl *pcl)
{
        int i;

        printk("PCL next %08x, userdata %08x, status %08x, remtrans %08x, nextbuf %08x\n",
               pcl->next, pcl->user_data, pcl->pcl_status, 
               pcl->remaining_transfer_count, pcl->next_data_buffer);

        printk("PCL");
        for (i=0; i<13; i++) {
                printk(" c%x:%08x d%x:%08x",
                       i, pcl->buffer[i].control, i, pcl->buffer[i].pointer);
                if (!(i & 0x3) && (i != 12)) printk("\nPCL");
        }
        printk("\n");
}
        
static void print_pcl(const struct ti_lynx *lynx, pcl_t pclid)
{
        struct ti_pcl pcl;

        get_pcl(lynx, pclid, &pcl);
        pretty_print_pcl(&pcl);
}


static int add_card(struct pci_dev *dev);
static void remove_card(struct ti_lynx *lynx);
static int init_driver(void);




/***********************************
 * IEEE-1394 functionality section *
 ***********************************/


static int get_phy_reg(struct ti_lynx *lynx, int addr)
{
        int retval;
        int i = 0;

        unsigned long flags;

        if (addr > 15) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY register address %d out of range", addr);
                return -1;
        }

        spin_lock_irqsave(&lynx->phy_reg_lock, flags);

        do {
                reg_write(lynx, LINK_PHY, LINK_PHY_READ | LINK_PHY_ADDR(addr));
                retval = reg_read(lynx, LINK_PHY);

                if (i > 10000) {
                        PRINT(KERN_ERR, lynx->id, __FUNCTION__ 
                              ": runaway loop, aborting");
                        retval = -1;
                        break;
                }
                i++;
        } while ((retval & 0xf00) != LINK_PHY_RADDR(addr));

        reg_write(lynx, LINK_INT_STATUS, LINK_INT_PHY_REG_RCVD);
        spin_unlock_irqrestore(&lynx->phy_reg_lock, flags);

        if (retval != -1) {
                return retval & 0xff;
        } else {
                return -1;
        }
}

static int set_phy_reg(struct ti_lynx *lynx, int addr, int val)
{
        unsigned long flags;

        if (addr > 15) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY register address %d out of range", addr);
                return -1;
        }

        if (val > 0xff) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY register value %d out of range", val);
                return -1;
        }

        spin_lock_irqsave(&lynx->phy_reg_lock, flags);

        reg_write(lynx, LINK_PHY, LINK_PHY_WRITE | LINK_PHY_ADDR(addr)
                  | LINK_PHY_WDATA(val));

        spin_unlock_irqrestore(&lynx->phy_reg_lock, flags);

        return 0;
}

static int sel_phy_reg_page(struct ti_lynx *lynx, int page)
{
        int reg;

        if (page > 7) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY page %d out of range", page);
                return -1;
        }

        reg = get_phy_reg(lynx, 7);
        if (reg != -1) {
                reg &= 0x1f;
                reg |= (page << 5);
                set_phy_reg(lynx, 7, reg);
                return 0;
        } else {
                return -1;
        }
}

#if 0 /* not needed at this time */
static int sel_phy_reg_port(struct ti_lynx *lynx, int port)
{
        int reg;

        if (port > 15) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY port %d out of range", port);
                return -1;
        }

        reg = get_phy_reg(lynx, 7);
        if (reg != -1) {
                reg &= 0xf0;
                reg |= port;
                set_phy_reg(lynx, 7, reg);
                return 0;
        } else {
                return -1;
        }
}
#endif

static u32 get_phy_vendorid(struct ti_lynx *lynx)
{
        u32 pvid = 0;
        sel_phy_reg_page(lynx, 1);
        pvid |= (get_phy_reg(lynx, 10) << 16);
        pvid |= (get_phy_reg(lynx, 11) << 8);
        pvid |= get_phy_reg(lynx, 12);
        PRINT(KERN_INFO, lynx->id, "PHY vendor id 0x%06x", pvid);
        return pvid;
}

static u32 get_phy_productid(struct ti_lynx *lynx)
{
        u32 id = 0;
        sel_phy_reg_page(lynx, 1);
        id |= (get_phy_reg(lynx, 13) << 16);
        id |= (get_phy_reg(lynx, 14) << 8);
        id |= get_phy_reg(lynx, 15);
        PRINT(KERN_INFO, lynx->id, "PHY product id 0x%06x", id);
        return id;
}

static quadlet_t generate_own_selfid(struct ti_lynx *lynx,
                                     struct hpsb_host *host)
{
        quadlet_t lsid;
        char phyreg[7];
        int i;

        for (i = 0; i < 7; i++) {
                phyreg[i] = get_phy_reg(lynx, i);
        }

        /* FIXME? We assume a TSB21LV03A phy here.  This code doesn't support
           more than 3 ports on the PHY anyway. */

        lsid = 0x80400000 | ((phyreg[0] & 0xfc) << 22);
        lsid |= (phyreg[1] & 0x3f) << 16; /* gap count */
        lsid |= (phyreg[2] & 0xc0) << 8; /* max speed */
        /* lsid |= (phyreg[6] & 0x01) << 11; *//* contender (phy dependent) */
        lsid |= 1 << 11; /* set contender (hack) */
        lsid |= (phyreg[6] & 0x10) >> 3; /* initiated reset */

        //for (i = 0; i < (phyreg[2] & 0xf); i++) { /* ports */
        for (i = 0; i < (phyreg[2] & 0x1f); i++) { /* ports */
                if (phyreg[3 + i] & 0x4) {
                        lsid |= (((phyreg[3 + i] & 0x8) | 0x10) >> 3)
                                << (6 - i*2);
                } else {
                        lsid |= 1 << (6 - i*2);
                }
        }

        printk("-%d- generated own selfid 0x%x\n", lynx->id, lsid);
        return lsid;
}

static void handle_selfid(struct ti_lynx *lynx, struct hpsb_host *host, size_t size)
{
        quadlet_t *q = lynx->rcv_page;
        int phyid, isroot;
        quadlet_t lsid = 0;

        if (!lynx->phyic.reg_1394a) {
                lsid = generate_own_selfid(lynx, host);
        }

        phyid = get_phy_reg(lynx, 0);
        isroot = (phyid & 2) != 0;
        phyid >>= 2;
        PRINT(KERN_INFO, lynx->id, "SelfID process finished (phyid %d, %s)",
              phyid, (isroot ? "root" : "not root"));
        reg_write(lynx, LINK_ID, (0xffc0 | phyid) << 16);

        if (!lynx->phyic.reg_1394a && !size) {
                hpsb_selfid_received(host, lsid);
        }

        while (size > 0) {
                if (!lynx->phyic.reg_1394a 
                    && (q[0] & 0x3f800000) == ((phyid + 1) << 24)) {
                        hpsb_selfid_received(host, lsid);
                }

                if (q[0] == ~q[1]) {
                        printk("-%d- selfid packet 0x%x rcvd\n", lynx->id, q[0]);
                        hpsb_selfid_received(host, q[0]);
                } else {
                        printk("-%d- inconsistent selfid 0x%x/0x%x\n", lynx->id,
                               q[0], q[1]);
                }
                q += 2;
                size -= 8;
        }

        if (!lynx->phyic.reg_1394a && isroot && phyid != 0) {
                hpsb_selfid_received(host, lsid);
        }

        hpsb_selfid_complete(host, phyid, isroot);
}



/* This must be called with the async_queue_lock held. */
static void send_next_async(struct ti_lynx *lynx)
{
        struct ti_pcl pcl;
        struct hpsb_packet *packet = lynx->async_queue;

        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;
#ifdef __BIG_ENDIAN
        pcl.buffer[0].control = PCL_CMD_XMT | packet->speed_code << 14
                | packet->header_size;
#else
        pcl.buffer[0].control = PCL_CMD_XMT | packet->speed_code << 14
                | packet->header_size | PCL_BIGENDIAN;
#endif
        pcl.buffer[0].pointer = virt_to_bus(packet->header);
        pcl.buffer[1].control = PCL_LAST_BUFF | packet->data_size;
        pcl.buffer[1].pointer = virt_to_bus(packet->data);

        if (!packet->data_be) {
                pcl.buffer[1].control |= PCL_BIGENDIAN;
        }

        put_pcl(lynx, lynx->async_pcl, &pcl);
        run_pcl(lynx, lynx->async_pcl_start, 3);
}


static int lynx_detect(struct hpsb_host_template *tmpl)
{
        struct hpsb_host *host;
        int i;

        init_driver();

        for (i = 0; i < num_of_cards; i++) {
                host = hpsb_get_host(tmpl, 0);
                if (host == NULL) {
                        /* simply don't init more after out of mem */
                        return i;
                }
                host->hostdata = &cards[i];
                cards[i].host = host;
        }

        return num_of_cards;
}

static int lynx_initialize(struct hpsb_host *host)
{
        struct ti_lynx *lynx = host->hostdata;
        struct ti_pcl pcl;
        int i;
        u32 *pcli;

        lynx->async_queue = NULL;
        spin_lock_init(&lynx->async_queue_lock);
        spin_lock_init(&lynx->phy_reg_lock);
        
        pcl.next = pcl_bus(lynx, lynx->rcv_pcl);
        put_pcl(lynx, lynx->rcv_pcl_start, &pcl);
        
        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;
#ifdef __BIG_ENDIAN
        pcl.buffer[0].control = PCL_CMD_RCV | 2048;
        pcl.buffer[1].control = PCL_LAST_BUFF | 2048;
#else
        pcl.buffer[0].control = PCL_CMD_RCV | PCL_BIGENDIAN | 2048;
        pcl.buffer[1].control = PCL_LAST_BUFF | PCL_BIGENDIAN | 2048;
#endif
        pcl.buffer[0].pointer = virt_to_bus(lynx->rcv_page);
        pcl.buffer[1].pointer = virt_to_bus(lynx->rcv_page) + 2048;
        put_pcl(lynx, lynx->rcv_pcl, &pcl);
        
        pcl.next = pcl_bus(lynx, lynx->async_pcl);
        pcl.async_error_next = pcl_bus(lynx, lynx->async_pcl);
        put_pcl(lynx, lynx->async_pcl_start, &pcl);

        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;
        pcl.buffer[0].control = PCL_CMD_RCV | PCL_LAST_BUFF | 2048;
#ifndef __BIG_ENDIAN
        pcl.buffer[0].control |= PCL_BIGENDIAN;
#endif

        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                int page = i / ISORCV_PER_PAGE;
                int sec = i % ISORCV_PER_PAGE;

                pcl.buffer[0].pointer = virt_to_bus(lynx->iso_rcv.page[page])
                        + sec * MAX_ISORCV_SIZE;
                put_pcl(lynx, lynx->iso_rcv.pcl[i], &pcl);
        }

        pcli = (u32 *)&pcl;
        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                pcli[i] = pcl_bus(lynx, lynx->iso_rcv.pcl[i]);
        }
        put_pcl(lynx, lynx->iso_rcv.pcl_start, &pcl);

        /* 85 bytes for each FIFO - FIXME - optimize or make configurable */
        reg_write(lynx, FIFO_SIZES, 0x00555555);
        /* 20 byte threshold before triggering PCI transfer */
        reg_write(lynx, DMA_GLOBAL_REGISTER, 0x2<<24);
        /* 69 byte threshold on both send FIFOs before transmitting */
        reg_write(lynx, FIFO_XMIT_THRESHOLD, 0x4545);
        
        reg_set_bits(lynx, PCI_INT_ENABLE, PCI_INT_1394);
        reg_write(lynx, LINK_INT_ENABLE, LINK_INT_PHY_TIMEOUT
                  | LINK_INT_PHY_REG_RCVD  | LINK_INT_PHY_BUSRESET
                  | LINK_INT_ISO_STUCK     | LINK_INT_ASYNC_STUCK 
                  | LINK_INT_SENT_REJECT   | LINK_INT_TX_INVALID_TC
                  | LINK_INT_GRF_OVERFLOW  | LINK_INT_ITF_UNDERFLOW
                  | LINK_INT_ATF_UNDERFLOW);
        
        reg_write(lynx, DMA1_WORD0_CMP_VALUE, 0);
        reg_write(lynx, DMA1_WORD0_CMP_ENABLE, 0xa<<4);
        reg_write(lynx, DMA1_WORD1_CMP_VALUE, 0);
        reg_write(lynx, DMA1_WORD1_CMP_ENABLE, DMA_WORD1_CMP_MATCH_NODE_BCAST
                  | DMA_WORD1_CMP_MATCH_BROADCAST | DMA_WORD1_CMP_MATCH_LOCAL
                  | DMA_WORD1_CMP_MATCH_BUS_BCAST | DMA_WORD1_CMP_ENABLE_SELF_ID
                  | DMA_WORD1_CMP_ENABLE_MASTER);

        run_pcl(lynx, lynx->rcv_pcl_start, 1);

        reg_write(lynx, DMA_WORD0_CMP_VALUE(CHANNEL_ISO_RCV), 0);
        reg_write(lynx, DMA_WORD0_CMP_ENABLE(CHANNEL_ISO_RCV), 0x9<<4);
        reg_write(lynx, DMA_WORD1_CMP_VALUE(CHANNEL_ISO_RCV), 0);
        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV), 0);

        run_sub_pcl(lynx, lynx->iso_rcv.pcl_start, 0, CHANNEL_ISO_RCV);

        reg_write(lynx, LINK_CONTROL, LINK_CONTROL_RCV_CMP_VALID
                  | LINK_CONTROL_TX_ISO_EN   | LINK_CONTROL_RX_ISO_EN
                  | LINK_CONTROL_TX_ASYNC_EN | LINK_CONTROL_RX_ASYNC_EN
                  | LINK_CONTROL_RESET_TX    | LINK_CONTROL_RESET_RX
                  | LINK_CONTROL_CYCSOURCE   | LINK_CONTROL_CYCTIMEREN);
        
        /* attempt to enable contender bit -FIXME- would this work elsewhere? */
        reg_set_bits(lynx, GPIO_CTRL_A, 0x1);
        reg_write(lynx, GPIO_DATA_BASE + 0x3c, 0x1); 

        return 1;
}

static void lynx_release(struct hpsb_host *host)
{
        struct ti_lynx *lynx;
        
        if (host != NULL) {
                lynx = host->hostdata;
                remove_card(lynx);
        } else {
                unregister_chrdev(PCILYNX_MAJOR, PCILYNX_DRIVER_NAME);
        }
}


/*
 * FIXME - does not support iso/raw transmits yet and will choke on them.
 */
static int lynx_transmit(struct hpsb_host *host, struct hpsb_packet *packet)
{
        struct ti_lynx *lynx = host->hostdata;
        struct hpsb_packet *p;
        unsigned long flags;

        if (packet->data_size >= 4096) {
                PRINT(KERN_ERR, lynx->id, "transmit packet data too big (%d)",
                      packet->data_size);
                return 0;
        }

        packet->xnext = NULL;

        spin_lock_irqsave(&lynx->async_queue_lock, flags);

        if (lynx->async_queue == NULL) {
                lynx->async_queue = packet;
                send_next_async(lynx);
        } else {
                p = lynx->async_queue;
                while (p->xnext != NULL) {
                        p = p->xnext;
                }

                p->xnext = packet;
        }

        spin_unlock_irqrestore(&lynx->async_queue_lock, flags);

        return 1;
}

static int lynx_devctl(struct hpsb_host *host, enum devctl_cmd cmd, int arg)
{
        struct ti_lynx *lynx = host->hostdata;
        int retval = 0;
        struct hpsb_packet *packet, *lastpacket;
        unsigned long flags;

        switch (cmd) {
        case RESET_BUS:
                if (arg) {
                        arg = 3 << 6;
                } else {
                        arg = 1 << 6;
                }
                
                PRINT(KERN_INFO, lynx->id, "resetting bus on request%s",
                      (host->attempt_root ? " and attempting to become root"
                       : ""));

                spin_lock_irqsave(&lynx->phy_reg_lock, flags);
                reg_write(lynx, LINK_PHY, LINK_PHY_WRITE | LINK_PHY_ADDR(1) 
                          | LINK_PHY_WDATA(arg));
                spin_unlock_irqrestore(&lynx->phy_reg_lock, flags);
                break;

        case GET_CYCLE_COUNTER:
                retval = reg_read(lynx, CYCLE_TIMER);
                break;
                
        case SET_CYCLE_COUNTER:
                reg_write(lynx, CYCLE_TIMER, arg);
                break;

        case SET_BUS_ID:
                reg_write(lynx, LINK_ID, 
                          (arg << 22) | (reg_read(lynx, LINK_ID) & 0x003f0000));
                break;
                
        case ACT_CYCLE_MASTER:
                if (arg) {
                        reg_set_bits(lynx, LINK_CONTROL,
                                     LINK_CONTROL_CYCMASTER);
                } else {
                        reg_clear_bits(lynx, LINK_CONTROL,
                                       LINK_CONTROL_CYCMASTER);
                }
                break;

        case CANCEL_REQUESTS:
                spin_lock_irqsave(&lynx->async_queue_lock, flags);

                reg_write(lynx, DMA3_CHAN_CTRL, 0);
                packet = lynx->async_queue;
                lynx->async_queue = NULL;

                spin_unlock_irqrestore(&lynx->async_queue_lock, flags);

                while (packet != NULL) {
                        lastpacket = packet;
                        packet = packet->xnext;
                        hpsb_packet_sent(host, lastpacket, ACKX_ABORTED);
                }

                break;

        case MODIFY_USAGE:
                if (arg) {
                        MOD_INC_USE_COUNT;
                } else {
                        MOD_DEC_USE_COUNT;
                }
                break;

        case ISO_LISTEN_CHANNEL:
                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);
                
                if (lynx->iso_rcv.chan_count++ == 0) {
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                                  DMA_WORD1_CMP_ENABLE_MASTER);
                }

                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
                break;

        case ISO_UNLISTEN_CHANNEL:
                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);

                if (--lynx->iso_rcv.chan_count == 0) {
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                                  0);
                }

                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
                break;

        default:
                PRINT(KERN_ERR, lynx->id, "unknown devctl command %d", cmd);
                retval = -1;
        }

        return retval;
}


/***************************************
 * IEEE-1394 functionality section END *
 ***************************************/


/* VFS functions for local bus / aux device access.  Access to those
 * is implemented as a character device instead of block devices
 * because buffers are not wanted for this.  Therefore llseek (from
 * VFS) can be used for these char devices with obvious effects.
 */
static int mem_open(struct inode*, struct file*);
static int mem_release(struct inode*, struct file*);
static unsigned int aux_poll(struct file*, struct poll_table_struct*);
static ssize_t mem_read (struct file*, char*, size_t, loff_t*);
static ssize_t mem_write(struct file*, const char*, size_t, loff_t*);


static struct file_operations aux_ops = {
        /* FIXME: should have custom llseek with bounds checking*/
        read:     mem_read,
        write:    mem_write,
        poll:     aux_poll,
        open:     mem_open,
        release:  mem_release
};


static void aux_setup_pcls(struct ti_lynx *lynx)
{
        struct ti_pcl pcl;
        unsigned long membufbus = virt_to_bus(lynx->mem_dma_buffer);
        int i;

        /* This pcl is used to start any aux transfers, the pointer to next
           points to itself to avoid a dummy pcl (the PCL engine only executes
           the next pcl on startup.  The real chain is done by branch */
        pcl.next = pcl_bus(lynx, lynx->mem_pcl.start);
        pcl.buffer[0].control = PCL_CMD_BRANCH | PCL_COND_DMARDY_SET;
        pcl.buffer[0].pointer = pcl_bus(lynx, lynx->mem_pcl.max);
        pcl.buffer[1].control = PCL_CMD_BRANCH | PCL_COND_DMARDY_CLEAR;
        pcl.buffer[1].pointer = pcl_bus(lynx, lynx->mem_pcl.cmd);
        put_pcl(lynx, lynx->mem_pcl.start, &pcl);

        /* let maxpcl transfer exactly 32kB */
        pcl.next = PCL_NEXT_INVALID;
        for (i=0; i<8; i++) {
                pcl.buffer[i].control = 4000;
                pcl.buffer[i].pointer = membufbus + i * 4000;
        }
        pcl.buffer[0].control |= PCL_CMD_LBUS_TO_PCI /*| PCL_GEN_INTR*/;
        pcl.buffer[8].control = 768 | PCL_LAST_BUFF;
        pcl.buffer[8].pointer = membufbus + 8 * 4000;
        put_pcl(lynx, lynx->mem_pcl.max, &pcl);


        /* magic stuff - self and modpcl modifying pcl */
        pcl.next = pcl_bus(lynx, lynx->mem_pcl.mod);
        pcl.user_data = 4000;
        pcl.buffer[0].control = PCL_CMD_LOAD;
        pcl.buffer[0].pointer = pcl_bus(lynx, lynx->mem_pcl.cmd) 
                                + pcloffs(user_data);
        pcl.buffer[1].control = PCL_CMD_STOREQ;
        pcl.buffer[1].pointer = pcl_bus(lynx, lynx->mem_pcl.mod) 
                                + pcloffs(buffer[1].control);
        pcl.buffer[2].control = PCL_CMD_LOAD;
        pcl.buffer[2].pointer = membufbus;
        pcl.buffer[3].control = PCL_CMD_STOREQ;
        pcl.buffer[3].pointer = pcl_bus(lynx, lynx->mem_pcl.cmd) 
                                + pcloffs(buffer[1].pointer);
        pcl.buffer[4].control = PCL_CMD_STOREQ;
        pcl.buffer[4].pointer = pcl_bus(lynx, lynx->mem_pcl.cmd) 
                                + pcloffs(buffer[6].pointer);
        pcl.buffer[5].control = PCL_CMD_LOAD;
        pcl.buffer[5].pointer = membufbus + 4;
        pcl.buffer[6].control = PCL_CMD_STOREQ | PCL_LAST_CMD;
        put_pcl(lynx, lynx->mem_pcl.cmd, &pcl);

        /* modified by cmdpcl when actual transfer occurs */
        pcl.next = PCL_NEXT_INVALID;
        pcl.buffer[0].control = PCL_CMD_LBUS_TO_PCI; /* null transfer */
        for (i=1; i<13; i++) {
                pcl.buffer[i].control = 4000;
                pcl.buffer[i].pointer = membufbus + (i-1) * 4000;
        }
        put_pcl(lynx, lynx->mem_pcl.mod, &pcl);
}

static int mem_open(struct inode *inode, struct file *file)
{
        int cid = MINOR(inode->i_rdev);
        enum { rom, aux, ram } type;
        struct memdata *md;

        MOD_INC_USE_COUNT;
        
        if (cid < PCILYNX_MINOR_AUX_START) {
                /* just for completeness */
                MOD_DEC_USE_COUNT;
                return -ENXIO;
        } else if (cid < PCILYNX_MINOR_ROM_START) {
                cid -= PCILYNX_MINOR_AUX_START;
                if (cid >= num_of_cards || !cards[cid].aux_port) {
                        MOD_DEC_USE_COUNT;
                        return -ENXIO;
                }
                type = aux;
        } else if (cid < PCILYNX_MINOR_RAM_START) {
                cid -= PCILYNX_MINOR_ROM_START;
                if (cid >= num_of_cards || !cards[cid].local_rom) {
                        MOD_DEC_USE_COUNT;
                        return -ENXIO;
                }
                type = rom;
        } else {
                /* WARNING: Know what you are doing when opening RAM.
                 * It is currently used inside the driver! */
                cid -= PCILYNX_MINOR_RAM_START;
                if (cid >= num_of_cards || !cards[cid].local_ram) {
                        MOD_DEC_USE_COUNT;
                        return -ENXIO;
                }
                type = ram;
        }

        md = (struct memdata *)vmalloc(sizeof(struct memdata));
        if (md == NULL) {
                MOD_DEC_USE_COUNT;
                return -ENOMEM;
        }

        md->lynx = &cards[cid];
        md->cid = cid;

        switch (type) {
        case rom:
                md->type = rom;
                break;
        case ram:
                md->type = ram;
                break;
        case aux:
                md->aux_intr_last_seen = atomic_read(&cards[cid].aux_intr_seen);
                md->type = aux;
                break;
        }

        file->private_data = md;

        return 0;
}

static int mem_release(struct inode *inode, struct file *file)
{
        struct memdata *md = (struct memdata *)file->private_data;

        vfree(md);

        MOD_DEC_USE_COUNT;
        return 0;
}

static unsigned int aux_poll(struct file *file, poll_table *pt)
{
        struct memdata *md = (struct memdata *)file->private_data;
        int cid = md->cid;
        unsigned int mask;
        int intr_seen;

        /* reading and writing is always allowed */
        mask = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;

        if (md->type == aux) {
                poll_wait(file, &cards[cid].aux_intr_wait, pt);
                intr_seen = atomic_read(&cards[cid].aux_intr_seen);

                if (md->aux_intr_last_seen != intr_seen) {
                        mask |= POLLPRI;
                        /* md->aux_intr_last_seen = intr_seen; */
                        md->aux_intr_last_seen++; /* don't miss interrupts */
                        /* FIXME - make ioctl for configuring this */
                }
        }

        return mask;
}


/* 
 * do not DMA if count is too small because this will have a serious impact 
 * on performance - the value 2400 was found by experiment and may not work
 * everywhere as good as here - use mem_mindma option for modules to change 
 */
short mem_mindma = 2400;
MODULE_PARM(mem_mindma, "h");

static ssize_t mem_read(struct file *file, char *buffer, size_t count,
                        loff_t *offset)
{
        struct memdata *md = (struct memdata *)file->private_data;
        size_t bcount;
        size_t alignfix;
        int off = (int)*offset; /* avoid useless 64bit-arithmetic */
        void *membase;

        DECLARE_WAITQUEUE(wait, current);

        if ((off + count) > PCILYNX_MAX_MEMORY+1) {
                count = PCILYNX_MAX_MEMORY+1 - off;
        }
        if (count <= 0) {
                return 0;
        }


        down(&md->lynx->mem_dma_mutex);

        switch (md->type) {
        case rom:
                reg_write(md->lynx, LBUS_ADDR, LBUS_ADDR_SEL_ROM | off);
                membase = md->lynx->local_rom;
                break;
        case ram:
                reg_write(md->lynx, LBUS_ADDR, LBUS_ADDR_SEL_RAM | off);
                membase = md->lynx->local_ram;
                break;
        case aux:
                reg_write(md->lynx, LBUS_ADDR, LBUS_ADDR_SEL_AUX | off);
                membase = md->lynx->aux_port;
                break;
        default:
                panic("pcilynx%d: unsupported md->type %d in " __FUNCTION__,
                      md->lynx->id, md->type);
        }

        if (count < mem_mindma) {
                memcpy_fromio(md->lynx->mem_dma_buffer, membase+off, count);
                copy_to_user(buffer, md->lynx->mem_dma_buffer, count);
                bcount = 0;
                goto done;
        }
        
        bcount = count;
        alignfix = 4 - (off % 4);
        if (alignfix != 4) {
                if (bcount < alignfix) {
                        alignfix = bcount;
                }
                memcpy_fromio(md->lynx->mem_dma_buffer, membase+off, alignfix);
                copy_to_user(buffer, md->lynx->mem_dma_buffer, alignfix);
                if (bcount == alignfix) {
                        goto done;
                }
                bcount -= alignfix;
                buffer += alignfix;
                off += alignfix;
        }

        if (reg_read(md->lynx, DMA0_CHAN_CTRL) & DMA_CHAN_CTRL_BUSY) {
                PRINT(KERN_WARNING, md->lynx->id, "DMA ALREADY ACTIVE!");
        }

        add_wait_queue(&md->lynx->mem_dma_intr_wait, &wait);

        if (bcount > 32768) {
                current->state = TASK_INTERRUPTIBLE;

                reg_write(md->lynx, DMA0_READY, 1); /* select maxpcl */
                run_pcl(md->lynx, md->lynx->mem_pcl.start, 0);

                while (reg_read(md->lynx, DMA0_CHAN_CTRL)
                       & DMA_CHAN_CTRL_BUSY) {
                        if (signal_pending(current)) {
                                reg_write(md->lynx, DMA0_CHAN_CTRL, 0);
                                goto rmwait_done;
                        }
                        schedule();
                }

                copy_to_user(buffer, md->lynx->mem_dma_buffer, 32768);
                buffer += 32768;
                bcount -= 32768;
        }

        *(u32 *)(md->lynx->mem_dma_buffer) = 
                pcl_bus(md->lynx, md->lynx->mem_pcl.mod) 
                + pcloffs(buffer[bcount/4000+1].control);
        *(u32 *)(md->lynx->mem_dma_buffer+4) = PCL_LAST_BUFF | (bcount % 4000);

        current->state = TASK_INTERRUPTIBLE;

        reg_write(md->lynx, DMA0_READY, 0);
        run_pcl(md->lynx, md->lynx->mem_pcl.start, 0);

        while (reg_read(md->lynx, DMA0_CHAN_CTRL) & DMA_CHAN_CTRL_BUSY) {
                if (signal_pending(current)) {
                        reg_write(md->lynx, DMA0_CHAN_CTRL, 0);
                        goto rmwait_done;
                }
                schedule();
        }

        copy_to_user(buffer, md->lynx->mem_dma_buffer, bcount);
        bcount = 0;

        if (reg_read(md->lynx, DMA0_CHAN_CTRL) & DMA_CHAN_CTRL_BUSY) {
                PRINT(KERN_ERR, md->lynx->id, "DMA STILL ACTIVE!");
        }

 rmwait_done:
        reg_write(md->lynx, DMA0_CHAN_CTRL, 0);
        remove_wait_queue(&md->lynx->mem_dma_intr_wait, &wait);
 done:
        up(&md->lynx->mem_dma_mutex);

        count -= bcount;
        *offset += count;
        return (count ? count : -EINTR);
}


static ssize_t mem_write(struct file *file, const char *buffer, size_t count, 
                         loff_t *offset)
{
        struct memdata *md = (struct memdata *)file->private_data;

        if (((*offset) + count) > PCILYNX_MAX_MEMORY+1) {
                count = PCILYNX_MAX_MEMORY+1 - *offset;
        }
        if (count == 0 || *offset > PCILYNX_MAX_MEMORY) {
                return -ENOSPC;
        }

        /* FIXME: dereferencing pointers to PCI mem doesn't work everywhere */
        switch (md->type) {
        case aux:
                copy_from_user(md->lynx->aux_port+(*offset), buffer, count);
                break;
        case ram:
                copy_from_user(md->lynx->local_ram+(*offset), buffer, count);
                break;
        case rom:
                /* the ROM may be writeable */
                copy_from_user(md->lynx->local_rom+(*offset), buffer, count);
                break;
        }

        file->f_pos += count;
        return count;
}



/********************************************************
 * Global stuff (interrupt handler, init/shutdown code) *
 ********************************************************/


static void lynx_irq_handler(int irq, void *dev_id,
                             struct pt_regs *regs_are_unused)
{
        struct ti_lynx *lynx = (struct ti_lynx *)dev_id;
        struct hpsb_host *host = lynx->host;
        u32 intmask = reg_read(lynx, PCI_INT_STATUS);
        u32 linkint = reg_read(lynx, LINK_INT_STATUS);

        reg_write(lynx, PCI_INT_STATUS, intmask);
        reg_write(lynx, LINK_INT_STATUS, linkint);
        //printk("-%d- one interrupt: 0x%08x / 0x%08x\n", lynx->id, intmask, linkint);

        if (intmask & PCI_INT_AUX_INT) {
                atomic_inc(&lynx->aux_intr_seen);
                wake_up_interruptible(&lynx->aux_intr_wait);
        }

        if (intmask & PCI_INT_DMA0_HLT) {
                wake_up_interruptible(&lynx->mem_dma_intr_wait);
        }


        if (intmask & PCI_INT_1394) {
                if (linkint & LINK_INT_PHY_TIMEOUT) {
                        PRINT(KERN_INFO, lynx->id, "PHY timeout occured");
                }
                if (linkint & LINK_INT_PHY_BUSRESET) {
                        PRINT(KERN_INFO, lynx->id, "bus reset interrupt");
                        if (!host->in_bus_reset) {
                                hpsb_bus_reset(host);
                        }
                }
                if (linkint & LINK_INT_PHY_REG_RCVD) {
                        if (!host->in_bus_reset) {
                                printk("-%d- phy reg received without reset\n",
                                       lynx->id);
                        }
                }
                if (linkint & LINK_INT_ISO_STUCK) {
                        PRINT(KERN_INFO, lynx->id, "isochronous transmitter stuck");
                }
                if (linkint & LINK_INT_ASYNC_STUCK) {
                        PRINT(KERN_INFO, lynx->id, "asynchronous transmitter stuck");
                }
                if (linkint & LINK_INT_SENT_REJECT) {
                        PRINT(KERN_INFO, lynx->id, "sent reject");
                }
                if (linkint & LINK_INT_TX_INVALID_TC) {
                        PRINT(KERN_INFO, lynx->id, "invalid transaction code");
                }
                if (linkint & LINK_INT_GRF_OVERFLOW) {
                        PRINT(KERN_INFO, lynx->id, "GRF overflow");
                }
                if (linkint & LINK_INT_ITF_UNDERFLOW) {
                        PRINT(KERN_INFO, lynx->id, "ITF underflow");
                }
                if (linkint & LINK_INT_ATF_UNDERFLOW) {
                        PRINT(KERN_INFO, lynx->id, "ATF underflow");
                }
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ISO_RCV)) {
                PRINT(KERN_INFO, lynx->id, "iso receive");

                spin_lock(&lynx->iso_rcv.lock);

                lynx->iso_rcv.stat[lynx->iso_rcv.next] =
                        reg_read(lynx, DMA_CHAN_STAT(CHANNEL_ISO_RCV));

                lynx->iso_rcv.used++;
                lynx->iso_rcv.next = (lynx->iso_rcv.next + 1) % NUM_ISORCV_PCL;

                if ((lynx->iso_rcv.next == lynx->iso_rcv.last)
                    || !lynx->iso_rcv.chan_count) {
                        printk("stopped\n");
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV), 0);
                }

                run_sub_pcl(lynx, lynx->iso_rcv.pcl_start, lynx->iso_rcv.next,
                            CHANNEL_ISO_RCV);

                spin_unlock(&lynx->iso_rcv.lock);

                queue_task(&lynx->iso_rcv.tq, &tq_immediate);
                mark_bh(IMMEDIATE_BH);
        }

        if (intmask & PCI_INT_DMA3_HLT) {
                /* async send DMA completed */
                u32 ack;
                struct hpsb_packet *packet;
                
                spin_lock(&lynx->async_queue_lock);

                ack = reg_read(lynx, DMA3_CHAN_STAT);
                packet = lynx->async_queue;
                lynx->async_queue = packet->xnext;

                if (lynx->async_queue != NULL) {
                        send_next_async(lynx);
                }

                spin_unlock(&lynx->async_queue_lock);

                if (ack & DMA_CHAN_STAT_SPECIALACK) {
                        printk("-%d- special ack %d\n", lynx->id,
                               (ack >> 15) & 0xf);
                        ack = ACKX_SEND_ERROR;
                } else {
                        ack = (ack >> 15) & 0xf;
                }
                
                hpsb_packet_sent(host, packet, ack);
        }

        if (intmask & (PCI_INT_DMA1_HLT | PCI_INT_DMA1_PCL)) {
                /* general receive DMA completed */
                int stat = reg_read(lynx, DMA1_CHAN_STAT);

                printk("-%d- received packet size %d\n", lynx->id, 
                       stat & 0x1fff); 

                if (stat & DMA_CHAN_STAT_SELFID) {
                        handle_selfid(lynx, host, stat & 0x1fff);
                        reg_set_bits(lynx, LINK_CONTROL,
                                     LINK_CONTROL_RCV_CMP_VALID
                                     | LINK_CONTROL_TX_ASYNC_EN
                                     | LINK_CONTROL_RX_ASYNC_EN);
                } else {
                        hpsb_packet_received(host, lynx->rcv_page,
                                             stat & 0x1fff);
                }

                run_pcl(lynx, lynx->rcv_pcl_start, 1);
        }
}

static void iso_rcv_bh(struct ti_lynx *lynx)
{
        unsigned int idx;
        quadlet_t *data;
        unsigned long flags;

        spin_lock_irqsave(&lynx->iso_rcv.lock, flags);

        while (lynx->iso_rcv.used) {
                idx = lynx->iso_rcv.last;
                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);

                data = lynx->iso_rcv.page[idx / ISORCV_PER_PAGE]
                        + (idx % ISORCV_PER_PAGE) * MAX_ISORCV_SIZE;

                if (lynx->iso_rcv.stat[idx] 
                    & (DMA_CHAN_STAT_PCIERR | DMA_CHAN_STAT_PKTERR)) {
                        PRINT(KERN_INFO, lynx->id,
                              "iso receive error on %d to 0x%p", idx, data);
                } else {
                        hpsb_packet_received(lynx->host, data,
                                             lynx->iso_rcv.stat[idx] & 0x1fff);
                }

                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);
                lynx->iso_rcv.last = (idx + 1) % NUM_ISORCV_PCL;
                lynx->iso_rcv.used--;
        }

        if (lynx->iso_rcv.chan_count) {
                reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                          DMA_WORD1_CMP_ENABLE_MASTER);
        }
        spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
}


static int add_card(struct pci_dev *dev)
{
#define FAIL(fmt, args...) \
        PRINT_G(KERN_ERR, fmt , ## args); \
        num_of_cards--; \
        remove_card(lynx); \
        return 1

        struct ti_lynx *lynx; /* shortcut to currently handled device */
        unsigned long page;
        unsigned int i;

        if (num_of_cards == MAX_PCILYNX_CARDS) {
                PRINT_G(KERN_WARNING, "cannot handle more than %d cards.  "
                        "Adjust MAX_PCILYNX_CARDS in ti_pcilynx.h.",
                        MAX_PCILYNX_CARDS);
                return 1;
        }

        lynx = &cards[num_of_cards++];

        lynx->id = num_of_cards-1;
        lynx->dev = dev;

        pci_set_master(dev);

        if (!request_irq(dev->irq, lynx_irq_handler, SA_SHIRQ,
                         PCILYNX_DRIVER_NAME, lynx)) {
                PRINT(KERN_INFO, lynx->id, "allocated interrupt %d", dev->irq);
                lynx->state = have_intr;
        } else {
                FAIL("failed to allocate shared interrupt %d", dev->irq);
        }

#ifndef CONFIG_IEEE1394_PCILYNX_LOCALRAM
	lynx->pcl_mem = kmalloc(8 * sizeof(lynx->pcl_bmap) 
                                * sizeof(struct ti_pcl), GFP_KERNEL);

        if (lynx->pcl_mem != NULL) {
                lynx->state = have_pcl_mem;
                PRINT(KERN_INFO, lynx->id, 
                      "allocated PCL memory %d Bytes @ 0x%p",
                      8 * sizeof(lynx->pcl_bmap) * sizeof(struct ti_pcl),
                      lynx->pcl_mem);
        } else {
                FAIL("failed to allocate PCL memory area");
        }
#endif

        lynx->mem_dma_buffer = kmalloc(32768, GFP_KERNEL);
        if (lynx->mem_dma_buffer != NULL) {
                lynx->state = have_aux_buf;
        } else {
                FAIL("failed to allocate DMA buffer for aux");
        }

        page = get_free_page(GFP_KERNEL);
        if (page != 0) {
                lynx->rcv_page = (void *)page;
                lynx->state = have_1394_buffers;
        } else {
                FAIL("failed to allocate receive buffer");
        }

        for (i = 0; i < ISORCV_PAGES; i++) {
                page = get_free_page(GFP_KERNEL);
                if (page != 0) {
                        lynx->iso_rcv.page[i] = (void *)page;
                } else {
                        FAIL("failed to allocate iso receive buffers");
                }
        }

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13)
        lynx->registers = ioremap_nocache(dev->base_address[0],
                                          PCILYNX_MAX_REGISTER);
        lynx->local_ram = ioremap(dev->base_address[1], PCILYNX_MAX_MEMORY);
        lynx->aux_port = ioremap(dev->base_address[2], PCILYNX_MAX_MEMORY);
#else
        lynx->registers = ioremap_nocache(dev->resource[0].start,
                                          PCILYNX_MAX_REGISTER);
        lynx->local_ram = ioremap(dev->resource[1].start, PCILYNX_MAX_MEMORY);
        lynx->aux_port = ioremap(dev->resource[2].start, PCILYNX_MAX_MEMORY);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,15)
        lynx->local_rom = ioremap(dev->rom_address, PCILYNX_MAX_MEMORY);
#else
        lynx->local_rom = ioremap(dev->resource[PCI_ROM_RESOURCE].start,
                                  PCILYNX_MAX_MEMORY);
#endif
        lynx->state = have_iomappings;

        if (lynx->registers == NULL) {
                FAIL("failed to remap registers - card not accessible");
        }

#ifdef CONFIG_IEEE1394_PCILYNX_LOCALRAM
        if (lynx->local_ram == NULL) {
                FAIL("failed to remap local RAM which is required for "
                     "operation");
        }
#endif

        /* alloc_pcl return values are not checked, it is expected that the
         * provided PCL space is sufficient for the initial allocations */
        if (lynx->aux_port != NULL) {
                lynx->mem_pcl.start = alloc_pcl(lynx);
                lynx->mem_pcl.cmd = alloc_pcl(lynx);
                lynx->mem_pcl.mod = alloc_pcl(lynx);
                lynx->mem_pcl.max = alloc_pcl(lynx);
                aux_setup_pcls(lynx);
        
                sema_init(&lynx->mem_dma_mutex, 1);
        }
        lynx->rcv_pcl = alloc_pcl(lynx);
        lynx->rcv_pcl_start = alloc_pcl(lynx);
        lynx->async_pcl = alloc_pcl(lynx);
        lynx->async_pcl_start = alloc_pcl(lynx);

        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                lynx->iso_rcv.pcl[i] = alloc_pcl(lynx);
        }
        lynx->iso_rcv.pcl_start = alloc_pcl(lynx);

        /* all allocations successful - simple init stuff follows */

        lynx->lock = SPIN_LOCK_UNLOCKED;

        reg_write(lynx, PCI_INT_ENABLE, PCI_INT_AUX_INT | PCI_INT_DMA_ALL);

        init_waitqueue_head(&lynx->mem_dma_intr_wait);
        init_waitqueue_head(&lynx->aux_intr_wait);

        lynx->iso_rcv.tq.routine = (void (*)(void*))iso_rcv_bh;
        lynx->iso_rcv.tq.data = lynx;
        lynx->iso_rcv.lock = SPIN_LOCK_UNLOCKED;
        
        PRINT(KERN_INFO, lynx->id, "remapped memory spaces reg 0x%p, rom 0x%p, "
              "ram 0x%p, aux 0x%p", lynx->registers, lynx->local_rom,
              lynx->local_ram, lynx->aux_port);

        /* now, looking for PHY register set */
        if ((get_phy_reg(lynx, 2) & 0xe0) == 0xe0) {
                lynx->phyic.reg_1394a = 1;
                PRINT(KERN_INFO, lynx->id,
                      "found 1394a conform PHY (using extended register set)");
                lynx->phyic.vendor = get_phy_vendorid(lynx);
                lynx->phyic.product = get_phy_productid(lynx);
        } else {
                lynx->phyic.reg_1394a = 0;
                PRINT(KERN_INFO, lynx->id, "found old 1394 PHY");
        }

        return 0;
#undef FAIL
}

static void remove_card(struct ti_lynx *lynx)
{
        int i;

        switch (lynx->state) {
        case have_iomappings:
                reg_write(lynx, PCI_INT_ENABLE, 0);
                reg_write(lynx, MISC_CONTROL, MISC_CONTROL_SWRESET);
                iounmap(lynx->registers);
                iounmap(lynx->local_rom);
                iounmap(lynx->local_ram);
                iounmap(lynx->aux_port);
        case have_1394_buffers:
                for (i = 0; i < ISORCV_PAGES; i++) {
                        if (lynx->iso_rcv.page[i]) {
                                free_page((unsigned long)lynx->iso_rcv.page[i]);
                        }
                }
                free_page((unsigned long)lynx->rcv_page);
        case have_aux_buf:
                kfree(lynx->mem_dma_buffer);
	case have_pcl_mem:
#ifndef CONFIG_IEEE1394_PCILYNX_LOCALRAM
		kfree(lynx->pcl_mem);
#endif
        case have_intr:
                free_irq(lynx->dev->irq, lynx);
        case clear:
                /* do nothing - already freed */
        }

        lynx->state = clear;
}

static int init_driver()
{
        struct pci_dev *dev = NULL;
        int success = 0;

        if (num_of_cards) {
                PRINT_G(KERN_DEBUG, __PRETTY_FUNCTION__ " called again");
                return 0;
        }

        PRINT_G(KERN_INFO, "looking for PCILynx cards");

        while ((dev = pci_find_device(PCI_VENDOR_ID_TI,
                                      PCI_DEVICE_ID_TI_PCILYNX, dev)) 
               != NULL) {
                if (add_card(dev) == 0) {
                        success = 1;
                }
        }

        if (success == 0) {
                PRINT_G(KERN_WARNING, "no operable PCILynx cards found");
                return -ENXIO;
        }

        if (register_chrdev(PCILYNX_MAJOR, PCILYNX_DRIVER_NAME, &aux_ops)) {
                PRINT_G(KERN_ERR, "allocation of char major number %d failed\n",
                        PCILYNX_MAJOR);
                return -EBUSY;
        }

        return 0;
}


static size_t get_lynx_rom(struct hpsb_host *host, const quadlet_t **ptr)
{
        *ptr = lynx_csr_rom;
        return sizeof(lynx_csr_rom);
}

struct hpsb_host_template *get_lynx_template(void)
{
        static struct hpsb_host_template tmpl = {
                name:             "pcilynx",
                detect_hosts:     lynx_detect,
                initialize_host:  lynx_initialize,
                release_host:     lynx_release,
                get_rom:          get_lynx_rom,
                transmit_packet:  lynx_transmit,
                devctl:           lynx_devctl
        };

        return &tmpl;
}


#ifdef MODULE

/* EXPORT_NO_SYMBOLS; */

MODULE_AUTHOR("Andreas E. Bombe <andreas.bombe@munich.netsurf.de>");
MODULE_DESCRIPTION("driver for Texas Instruments PCI Lynx IEEE-1394 controller");
MODULE_SUPPORTED_DEVICE("pcilynx");

void cleanup_module(void)
{
        hpsb_unregister_lowlevel(get_lynx_template());
        PRINT_G(KERN_INFO, "removed " PCILYNX_DRIVER_NAME " module\n");
}

int init_module(void)
{
        if (hpsb_register_lowlevel(get_lynx_template())) {
                PRINT_G(KERN_ERR, "registering failed\n");
                return -ENXIO;
        } else {
                return 0;
        }
}

#endif /* MODULE */

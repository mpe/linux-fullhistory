/*
 * ohci1394.c - driver for OHCI 1394 boards
 * Copyright (C)1999,2000 Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>
 *                        Gord Peters <GordPeters@smarttech.com>
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
#include <linux/proc_fs.h>
#include <linux/tqueue.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "ohci1394.h"

/* print general (card independent) information */
#define PRINT_G(level, fmt, args...) \
printk(level "ohci1394: " fmt "\n" , ## args)

/* print card specific information */
#define PRINT(level, card, fmt, args...) \
printk(level "ohci1394_%d: " fmt "\n" , card , ## args)

int supported_chips[][2] = {
    { PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_OHCI1394 },
    { PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_OHCI1394_2 },
    { PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_OHCI1394 },
    { -1, -1 }
};

static struct ti_ohci cards[MAX_OHCI1394_CARDS];
static int num_of_cards = 0;

static int add_card(struct pci_dev *dev);
static void remove_card(struct ti_ohci *ohci);
static int init_driver(void);

/***********************************
 * IEEE-1394 functionality section *
 ***********************************/

#if 0 /* not needed at this time */
static int get_phy_reg(struct ti_ohci *ohci, int addr) 
{
	int timeout=10000;
	static quadlet_t r;

	if ((addr < 1) || (addr > 15)) {
		PRINT(KERN_ERR, ohci->id, __FUNCTION__
		      ": PHY register address %d out of range", addr);
		return -EFAULT;
	}

	spin_lock(&ohci->phy_reg_lock);

	/* initiate read request */
	reg_write(ohci, OHCI1394_PhyControl, 
		  ((addr<<8)&0x00000f00) | 0x00008000);

	/* wait */
	while (!(reg_read(ohci, OHCI1394_PhyControl)&0x80000000) && timeout)
	  timeout--;


	if (!timeout) {
		PRINT(KERN_ERR, ohci->id, "get_phy_reg timeout !!!\n");
		spin_unlock(&ohci->phy_reg_lock);
		return -EFAULT;
	}
	r = reg_read(ohci, OHCI1394_PhyControl);
  
	spin_unlock(&ohci->phy_reg_lock);
     
	return (r&0x00ff0000)>>16;
}

static int set_phy_reg(struct ti_ohci *ohci, int addr, unsigned char data) {
	int timeout=10000;
	u32 r;

	if ((addr < 1) || (addr > 15)) {
		PRINT(KERN_ERR, ohci->id, __FUNCTION__
		      ": PHY register address %d out of range", addr);
		return -EFAULT;
	}

	r = ((addr<<8)&0x00000f00) | 0x00004000 | ((u32)data & 0x000000ff);

	spin_lock(&ohci->phy_reg_lock);

	reg_write(ohci, OHCI1394_PhyControl, r);

	/* wait */
	while (!(reg_read(ohci, OHCI1394_PhyControl)&0x80000000) && timeout)
	  timeout--;

	spin_unlock(&ohci->phy_reg_lock);

	if (!timeout) {
		PRINT(KERN_ERR, ohci->id, "set_phy_reg timeout !!!\n");
		return -EFAULT;
	}

	return 0;
}
#endif /* unneeded functions */

inline static int handle_selfid(struct ti_ohci *ohci, struct hpsb_host *host,
				int phyid, int isroot)
{
	quadlet_t *q = ohci->self_id_buffer;
	quadlet_t self_id_count=reg_read(ohci, OHCI1394_SelfIDCount);
	size_t size;
	quadlet_t lsid;

	/* Self-id handling seems much easier than for the aic5800 chip.
	   All the self-id packets, including this device own self-id,
	   should be correctly arranged in the self_id_buffer at this
	   stage */

	/* Check status of self-id reception */
	if ((self_id_count&0x80000000) || 
	    ((self_id_count&0x00FF0000) != (q[0]&0x00FF0000))) {
		PRINT(KERN_ERR, ohci->id, 
		      "Error in reception of self-id packets");
		return -1;
	}    

	size = ((self_id_count&0x0000EFFC)>>2) - 1;
	q++;

	while (size > 0) {
		if (q[0] == ~q[1]) {
			printk("-%d- selfid packet 0x%x rcvd\n", 
			       ohci->id, q[0]);
			hpsb_selfid_received(host, q[0]);
			if (((q[0]&0x3f000000)>>24)==phyid) {
				lsid=q[0];
				printk("This node self-id is 0x%08x\n",lsid);
			}
		} else {
			printk("-%d- inconsistent selfid 0x%x/0x%x\n", ohci->id,
			       q[0], q[1]);
		}
		q += 2;
		size -= 2;
	}

	printk(" calling self-id complete\n");

	hpsb_selfid_complete(host, phyid, isroot);
	return 0;
}

static int ohci_detect(struct hpsb_host_template *tmpl)
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

static int ohci_soft_reset(struct ti_ohci *ohci) {
	int timeout=10000;

	reg_write(ohci, OHCI1394_HCControlSet, 0x00010000);
  
	while ((reg_read(ohci, OHCI1394_HCControlSet)&0x00010000) && timeout) 
	  timeout--;
	if (!timeout) {
		PRINT(KERN_ERR, ohci->id, "soft reset timeout !!!");
		return -EFAULT;
	}
	else PRINT(KERN_INFO, ohci->id, "soft reset finished");
	return 0;
}

static int ohci_initialize(struct hpsb_host *host)
{
	struct ti_ohci *ohci=host->hostdata;
	int retval, i;

	spin_lock_init(&ohci->phy_reg_lock);
  
	/* Soft reset */
	if ((retval=ohci_soft_reset(ohci))<0) return retval;
  
	/* Set the bus number */
	reg_write(ohci, OHCI1394_NodeID, 0x0000ffc0);

	/* Set Link Power Status (LPS) */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00080000);

	/* Enable posted writes */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00040000);

	/* Clear link control register */
	reg_write(ohci, OHCI1394_LinkControlClear, 0xffffffff);
  
	/* Enable cycle timer and cycle master */
	reg_write(ohci, OHCI1394_LinkControlSet, 0x00300000);

	/* Clear interrupt registers */
	reg_write(ohci, OHCI1394_IntEventClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IntMaskClear, 0xffffffff);

	/* Set up self-id dma buffer */
	reg_write(ohci, OHCI1394_SelfIDBuffer, 
		  virt_to_bus(ohci->self_id_buffer));

	/* enable self-id dma */
	reg_write(ohci, OHCI1394_LinkControlSet, 0x00000200);

	/* Set the configuration ROM mapping register */
	reg_write(ohci, OHCI1394_ConfigROMmap, 
		  virt_to_bus(ohci->csr_config_rom));

#if 1				/* Why is this step necessary ? */
	/* Write the config ROM header */
	reg_write(ohci, OHCI1394_ConfigROMhdr,0x04040000);

	/* Set bus options */
	reg_write(ohci, OHCI1394_BusOptions, 0xf064A002);
#endif

#if 1
	/* Accept phy packets into AR request context */ 
	reg_write(ohci, OHCI1394_LinkControlSet, 0x00000400);
#endif

	/* Enable link */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00020000);

	/* Initialize IR dma */

	/* make sure the context isn't running, dead, or active */
	if (!(reg_read(ohci, OHCI1394_IrRcvContextControlSet) & 0x00008F00)) {

                /* initialize IR program */
                for (i= 0; i < IR_NUM_DESC; i++) {

			/* end of descriptor list? */
                        if ((i + 1) < IR_NUM_DESC) {
                                ohci->IR_recv_prg[i]->control=
                                        (0x283C << 16) | IR_RECV_BUF_SIZE;
                                ohci->IR_recv_prg[i]->branchAddress=
                                        (virt_to_bus(ohci->IR_recv_prg[i + 1])
                                         & 0xfffffff0) | 0x1;
                        } else {
                                ohci->IR_recv_prg[i]->control=
                                        (0x283C << 16) | IR_RECV_BUF_SIZE;
                                ohci->IR_recv_prg[i]->branchAddress=
                                        (virt_to_bus(ohci->IR_recv_prg[0])
                                         & 0xfffffff0) | 0x1;
                        }

                        ohci->IR_recv_prg[i]->address=
                                virt_to_bus(ohci->IR_recv_buf[i]);
                        ohci->IR_recv_prg[i]->status= IR_RECV_BUF_SIZE;
		}

		/* Tell the controller where the first IR program is */
		reg_write(ohci, OHCI1394_IrRcvCommandPtr,
			  virt_to_bus(ohci->IR_recv_prg[0]) | 0x1 );

		/* Set bufferFill, isochHeader, multichannel for IR context */
		reg_write(ohci, OHCI1394_IrRcvContextControlSet, 0xd0000000);

		/* Set the context match register to match on all tags */
		reg_write(ohci, OHCI1394_IrRcvContextMatch, 0xf0000000);

		/* Clear the multi channel mask high and low registers */
		reg_write(ohci, OHCI1394_IRMultiChanMaskHiClear, 0xffffffff);
		reg_write(ohci, OHCI1394_IRMultiChanMaskLoClear, 0xffffffff);

                /* Set up isoRecvIntMask to generate interrupts for context 0
                   (thanks to Michael Greger for seeing that I forgot this) */
                reg_write(ohci, OHCI1394_IsoRecvIntMaskSet, 0x00000001);

		/* Run IR context */
		reg_write(ohci, OHCI1394_IrRcvContextControlSet, 0x00008000);
	}

	/* Initialize AR dma */
	/* make sure the context isn't running, dead, or active */
	if (!(reg_read(ohci, OHCI1394_AsRspRcvContextControlSet) & 0x00008F00)) {

                /* initialize AR program */
                for (i= 0; i < AR_RESP_NUM_DESC; i++) {

			/* end of descriptor list? */
                        if ((i + 1) < AR_RESP_NUM_DESC) {
                                ohci->AR_resp_prg[i]->control=
				  (0x283C << 16) | AR_RESP_BUF_SIZE;
                                ohci->AR_resp_prg[i]->branchAddress=
				  (virt_to_bus(ohci->AR_resp_prg[i + 1])
                                         & 0xfffffff0) | 0x1;
                        } else {
                                ohci->AR_resp_prg[i]->control=
                                        (0x283C << 16) | AR_RESP_BUF_SIZE;
                                ohci->AR_resp_prg[i]->branchAddress=
                                        (virt_to_bus(ohci->AR_resp_prg[0])
                                         & 0xfffffff0) | 0x1;
                        }

                        ohci->AR_resp_prg[i]->address=
                                virt_to_bus(ohci->AR_resp_buf[i]);
                        ohci->AR_resp_prg[i]->status= AR_RESP_BUF_SIZE;
		}

		/* Tell the controller where the first AR program is */
		reg_write(ohci, OHCI1394_AsRspRcvCommandPtr,
			  virt_to_bus(ohci->AR_resp_prg[0]) | 0x1 );

		/* Accept phy packets into AR request context */
		reg_write(ohci, OHCI1394_LinkControlSet, 0x00000400);

		/* Run AR context */
		reg_write(ohci, OHCI1394_AsRspRcvContextControlSet, 0x00008000);
	}

	/* Specify AT retries */
	reg_write(ohci, OHCI1394_ATRetries, 
		  OHCI1394_MAX_AT_REQ_RETRIES |
		  (OHCI1394_MAX_AT_RESP_RETRIES<<4) |
		  (OHCI1394_MAX_PHYS_RESP_RETRIES<<8));

#ifndef __BIG_ENDIAN
	reg_write(ohci, OHCI1394_HCControlClear, 0x40000000);
#else
	reg_write(ohci, OHCI1394_HCControlSet, 0x40000000);
#endif

	/* Enable interrupts */
	reg_write(ohci, OHCI1394_IntMaskSet, 
		  OHCI1394_masterIntEnable | 
		  OHCI1394_phyRegRcvd | 
		  OHCI1394_busReset | 
		  OHCI1394_selfIDComplete |
		  OHCI1394_RSPkt |
		  OHCI1394_RQPkt |
		  OHCI1394_ARRS |
		  OHCI1394_ARRQ |
		  OHCI1394_respTxComplete |
		  OHCI1394_reqTxComplete |
		  OHCI1394_isochRx
		  );

	return 1;
}

static void ohci_remove(struct hpsb_host *host)
{
	struct ti_ohci *ohci;
        
	if (host != NULL) {
		ohci = host->hostdata;
		remove_card(ohci);
	}
}


/* This must be called with the async_queue_lock held. */
static void send_next_async(struct ti_ohci *ohci)
{
	int i=0;
	struct hpsb_packet *packet = ohci->async_queue;
	struct dma_cmd prg;
#if 0
	quadlet_t *ptr = (quadlet_t *)ohci->AT_req_prg;
#endif
	//HPSB_TRACE();

        /* stop the channel program if it's still running */
        reg_write(ohci, OHCI1394_AsReqTrContextControlClear, 0x8000);
   
        /* Wait until it effectively stops */
        while (reg_read(ohci, OHCI1394_AsReqTrContextControlSet)
               & 0x400) {
                i++;
                if (i>5000) {
                        PRINT(KERN_ERR, ohci->id, 
                              "runaway loop in DmaAT. bailing out...");
                        break;
                }
        };

        if (packet->type == async)
        {

                /* re-format packet header according to ohci specification */
                packet->header[1] = (packet->header[1] & 0xFFFF) | 
                        (packet->header[0] & 0xFFFF0000);
                packet->header[0] = DMA_SPEED_200 |
                        (packet->header[0] & 0xFFFF);

                if (packet->data_size) { /* block transmit */
                        prg.control = OUTPUT_MORE_IMMEDIATE | 0x10;
                        prg.address = 0;
                        prg.branchAddress = 0;
                        prg.status = 0;
                        memcpy(ohci->AT_req_prg, &prg, 16);
                        memcpy(ohci->AT_req_prg + 1, packet->header, 16);
                        prg.control = OUTPUT_LAST | packet->data_size;
                        prg.address = virt_to_bus(packet->data);
                        memcpy(ohci->AT_req_prg + 2, &prg, 16);

                        reg_write(ohci, OHCI1394_AsReqTrCommandPtr, 
                                  virt_to_bus(ohci->AT_req_prg)|0x3);
                }
                else { /* quadlet transmit */
                        prg.control = OUTPUT_LAST_IMMEDIATE |
                                packet->header_size;
                        prg.address = 0;
                        prg.branchAddress = 0;
                        prg.status = 0;
                        memcpy(ohci->AT_req_prg, &prg, 16);
                        memcpy(ohci->AT_req_prg + 1, packet->header, 16);
#if 0
                        PRINT(KERN_INFO, ohci->id,
                              "dma_cmd: %08x %08x %08x %08x",
                              *ptr, *(ptr+1), *(ptr+2), *(ptr+3));
                        PRINT(KERN_INFO, ohci->id,
                              "header: %08x %08x %08x %08x",
                              *(ptr+4), *(ptr+5), *(ptr+6), *(ptr+7));
#endif
                        reg_write(ohci, OHCI1394_AsReqTrCommandPtr, 
                                  virt_to_bus(ohci->AT_req_prg)|0x2);
                }

        }
        else if (packet->type == raw)
        {
                prg.control = OUTPUT_LAST | packet->data_size;
                prg.address = virt_to_bus(packet->data);
                prg.branchAddress = 0;
                prg.status = 0;
                memcpy(ohci->AT_req_prg, &prg, 16);
#if 0
                PRINT(KERN_INFO, ohci->id,
                      "dma_cmd: %08x %08x %08x %08x",
                      *ptr, *(ptr+1), *(ptr+2), *(ptr+3));
#endif
                reg_write(ohci, OHCI1394_AsReqTrCommandPtr, 
                          virt_to_bus(ohci->AT_req_prg)|0x2);
        }

	/* run program */
	reg_write(ohci, OHCI1394_AsReqTrContextControlSet, 0x00008000);
}

static int ohci_transmit(struct hpsb_host *host, struct hpsb_packet *packet)
{
	struct ti_ohci *ohci = host->hostdata;
	struct hpsb_packet *p;
	unsigned long flags;

	if (packet->data_size >= 4096) {
		PRINT(KERN_ERR, ohci->id, "transmit packet data too big (%d)",
		      packet->data_size);
		return 0;
	}

	//HPSB_TRACE();
	packet->xnext = NULL;
    
	spin_lock_irqsave(&ohci->async_queue_lock, flags);

	if (ohci->async_queue == NULL) {
		ohci->async_queue = packet;
		send_next_async(ohci);
	} else {
		p = ohci->async_queue;
		while (p->xnext != NULL) {
			p = p->xnext;
		}
		
		p->xnext = packet;
	}
    
	spin_unlock_irqrestore(&ohci->async_queue_lock, flags);

	return 1;
}

static int ohci_devctl(struct hpsb_host *host, enum devctl_cmd cmd, int arg)
{
	struct ti_ohci *ohci = host->hostdata;
	int retval = 0;
	unsigned long flags;
        struct hpsb_packet *packet, *lastpacket;
	u32 r;

	switch (cmd) {
	      case RESET_BUS:
		PRINT(KERN_INFO, ohci->id, "resetting bus on request%s",
		      (host->attempt_root ? " and attempting to become root"
		       : ""));
		r = (host->attempt_root) ? 0x000041ff : 0x0000417f;
		reg_write(ohci, OHCI1394_PhyControl, r);
		break;

	      case GET_CYCLE_COUNTER:
		retval = reg_read(ohci, OHCI1394_IsochronousCycleTimer);
		break;
	
	      case SET_CYCLE_COUNTER:
		reg_write(ohci, OHCI1394_IsochronousCycleTimer, arg);
		break;
	
	      case SET_BUS_ID:
		PRINT(KERN_ERR, ohci->id, "devctl command SET_BUS_ID err");
		break;

	      case ACT_CYCLE_MASTER:
#if 0
		if (arg) {
			/* enable cycleTimer, cycleMaster, cycleSource */
			reg_write(ohci, OHCI1394_LinkControlSet, 0x00700000);
		} else {
			/* disable cycleTimer, cycleMaster, cycleSource */
			reg_write(ohci, OHCI1394_LinkControlClear, 0x00700000);
		};
#endif
		break;

	      case CANCEL_REQUESTS:
		spin_lock_irqsave(&ohci->async_queue_lock, flags);
		/* stop any chip activity */
		reg_write(ohci, OHCI1394_HCControlClear, 0x00020000);
                packet = ohci->async_queue;
		ohci->async_queue = NULL;                
		spin_unlock_irqrestore(&ohci->async_queue_lock, flags);

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

                spin_lock_irqsave(&ohci->IR_channel_lock, flags);

                if (!test_and_set_bit(arg, &ohci->IR_channel_usage)) {
                        PRINT(KERN_INFO, ohci->id,
                              "listening enabled on channel %d", arg);

                        if (arg > 31) {
                                u32 setMask= 0x00000001;
                                arg-= 32;
                                while(arg--) setMask= setMask << 1;
                                reg_write(ohci, OHCI1394_IRMultiChanMaskHiSet,
                                          setMask);
                        } else {
                                u32 setMask= 0x00000001;
                                while(arg--) setMask= setMask << 1;
                                reg_write(ohci, OHCI1394_IRMultiChanMaskLoSet,
                                          setMask);
                        }

                }

                spin_unlock_irqrestore(&ohci->IR_channel_lock, flags);
                break;

              case ISO_UNLISTEN_CHANNEL:

                spin_lock_irqsave(&ohci->IR_channel_lock, flags);

                if (test_and_clear_bit(arg, &ohci->IR_channel_usage)) {
                        PRINT(KERN_INFO, ohci->id,
                              "listening disabled on iso channel %d", arg);

                        if (arg > 31) {
                                u32 clearMask= 0x00000001;
                                arg-= 32;
                                while(arg--) clearMask= clearMask << 1;
                                reg_write(ohci,
                                          OHCI1394_IRMultiChanMaskHiClear,
                                          clearMask);
                        } else {
                                u32 clearMask= 0x00000001;
                                while(arg--) clearMask= clearMask << 1;
                                reg_write(ohci,
                                          OHCI1394_IRMultiChanMaskLoClear,
                                          clearMask);
                        }

                }

                spin_unlock_irqrestore(&ohci->IR_channel_lock, flags);
                break;

	      default:
		PRINT_G(KERN_ERR, "ohci_devctl cmd %d not implemented yet\n",
			cmd);
		break;
	}
	return retval;
}

/***************************************
 * IEEE-1394 functionality section END *
 ***************************************/


/********************************************************
 * Global stuff (interrupt handler, init/shutdown code) *
 ********************************************************/

static void stop_ar_resp_context(struct ti_ohci *ohci, char *msg)
{
	int i=0;

	/* stop the channel program if it's still running */
	reg_write(ohci, OHCI1394_AsRspRcvContextControlClear, 0x8000);
   
	/* Wait until it effectively stops */
	while (reg_read(ohci, OHCI1394_AsRspRcvContextControlSet) 
	       & 0x400) {
		i++;
		if (i>5000) {
			PRINT(KERN_ERR, ohci->id, 
			      "runaway loop in Dma Ar Resp. bailing out...");
			break;
		}
	}
	PRINT(KERN_ERR, ohci->id, "%s\n async response receive dma stopped\n", msg);
}

static void ohci_irq_handler(int irq, void *dev_id,
                             struct pt_regs *regs_are_unused)
{
	//int i;
	static quadlet_t event,node_id;
	struct ti_ohci *ohci = (struct ti_ohci *)dev_id;
	struct hpsb_host *host = ohci->host;
	int phyid = -1, isroot = 0;

	event=reg_read(ohci, OHCI1394_IntEventSet);

	/* Clear the interrupt register */
	reg_write(ohci, OHCI1394_IntEventClear, event);

	/* PRINT(KERN_INFO, ohci->id, "int event %08X mask %08X",
	   event,reg_read(ohci, OHCI1394_IntMaskSet)); */

	if (event & OHCI1394_busReset) {
#if 0
		PRINT(KERN_INFO, ohci->id, "bus reset interrupt");
#endif
		if (!host->in_bus_reset) {
			hpsb_bus_reset(host);
		}
		ohci->NumBusResets++;
	}
	if (event & OHCI1394_RQPkt) {
		PRINT(KERN_INFO, ohci->id, "RQPkt int received");
	}
	if (event & OHCI1394_RQPkt) {
		PRINT(KERN_INFO, ohci->id, "ControlContext: %08X",
		      reg_read(ohci, OHCI1394_AsReqRcvContextControlSet));
	}
	if (event & OHCI1394_RSPkt) {
		unsigned int idx,offset,rescount;

                spin_lock(&ohci->AR_resp_lock);

		idx = ohci->AR_resp_buf_th_ind;
		offset = ohci->AR_resp_buf_th_offset;

		rescount = ohci->AR_resp_prg[idx]->status&0xffff;
		ohci->AR_resp_bytes_left +=  AR_RESP_BUF_SIZE - rescount - offset;
		offset = AR_RESP_BUF_SIZE - rescount;

		if (!rescount) { /* We cross a buffer boundary */
			idx = (idx+1) % AR_RESP_NUM_DESC;

#if 0 
			/* This bit of code does not work */
			/* Let's see how many bytes were written in the async response 
			   receive buf since last interrupt. This is done by finding 
			   the next active context (See OHCI Spec p91) */
			while (ohci->AR_resp_bytes_left <= AR_RESP_TOTAL_BUF_SIZE) {
				if (ohci->AR_resp_prg[idx]->status&0x04000000) break;
				idx = (idx+1) % AR_RESP_NUM_DESC;
				PRINT(KERN_INFO,ohci->id,"crossing more than one buffer boundary !!!");
				ohci->AR_resp_bytes_left += AR_RESP_BUF_SIZE;
			}
#endif 
			/* ASSUMPTION: only one buffer boundary is crossed */
			rescount = ohci->AR_resp_prg[idx]->status&0xffff;
			offset = AR_RESP_BUF_SIZE - rescount;
			ohci->AR_resp_bytes_left += offset;
		}
		if (offset==AR_RESP_BUF_SIZE) {
			offset=0;
			idx = (idx+1) % AR_RESP_NUM_DESC;
		}
		ohci->AR_resp_buf_th_ind = idx;
		ohci->AR_resp_buf_th_offset = offset;

                /* is buffer processing too slow? (all buffers used) */
		if (ohci->AR_resp_bytes_left > AR_RESP_TOTAL_BUF_SIZE) {
			stop_ar_resp_context(ohci,"async response receive processing too slow");
                        spin_unlock(&ohci->AR_resp_lock);
			return;
		}
                spin_unlock(&ohci->AR_resp_lock);

		/* queue bottom half in immediate queue */
		queue_task(&ohci->AR_resp_pdl_task, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
	if (event & OHCI1394_isochRx) {
		quadlet_t isoRecvIntEvent;

		/* ASSUMPTION: We assume there is only one context for now. */

                spin_lock(&ohci->IR_recv_lock);

                /* Clear the isoRecvIntEvent register (very important!) */
                isoRecvIntEvent= reg_read(ohci, OHCI1394_IsoRecvIntEventSet);
		reg_write(ohci, OHCI1394_IsoRecvIntEventClear,
			  isoRecvIntEvent);

                ohci->IR_buf_used++;
                ohci->IR_buf_next_ind=
                        (ohci->IR_buf_next_ind + 1) % IR_NUM_DESC;

                /* is buffer processing too slow? (all buffers used) */
		if (ohci->IR_buf_next_ind == ohci->IR_buf_last_ind) {
                        int i= 0;

                        /* stop the context */
			reg_write(ohci,
				  OHCI1394_IrRcvContextControlClear, 0x8000);

			while (reg_read(ohci, OHCI1394_IrRcvContextControlSet) 
			       & 0x400) {
				i++;

				if (i>5000) {
					PRINT(KERN_ERR, ohci->id, "runaway loop in DmaIR. bailing out...");
					break;
				}

			}

                        spin_unlock(&ohci->IR_recv_lock);
			PRINT(KERN_ERR, ohci->id,
                              "iso receive processing too slow... stopped");
			return;
		}

                /* reset status field of next descriptor */
                ohci->IR_recv_prg[ohci->IR_buf_next_ind]->status=
                        IR_RECV_BUF_SIZE;

                spin_unlock(&ohci->IR_recv_lock);

		/* queue bottom half in immediate queue */
		queue_task(&ohci->IR_pdl_task, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
	if (event & OHCI1394_selfIDComplete) {
		if (host->in_bus_reset) {
			node_id = reg_read(ohci, OHCI1394_NodeID);
			if (node_id & 0x8000000) { /* NodeID valid */
				phyid =  node_id & 0x0000003f;
				isroot = (node_id & 0x40000000) != 0;

				PRINT(KERN_INFO, ohci->id,
				      "SelfID process finished (phyid %d, %s)",
				      phyid, (isroot ? "root" : "not root"));

				handle_selfid(ohci, host, phyid, isroot);
			}
			else 
			  PRINT(KERN_ERR, ohci->id, 
				"SelfID process finished but NodeID"
				" not valid: %08X",node_id);
		} 
		else PRINT(KERN_INFO, ohci->id, 
			   "phy reg received without reset\n");
	}
	if (event & OHCI1394_phyRegRcvd) {
#if 0
		if (host->in_bus_reset) {
			PRINT(KERN_INFO, ohci->id, "PhyControl: %08X", 
			      reg_read(ohci, OHCI1394_PhyControl));
		} else printk("-%d- phy reg received without reset\n",
			      ohci->id);
#endif
	}
	if (event & OHCI1394_reqTxComplete) { 
		/* async packet sent - transmitter ready */
		u32 ack;
		struct hpsb_packet *packet;

		if (ohci->async_queue) {

			spin_lock(&ohci->async_queue_lock);
	    
			ack=reg_read(ohci, OHCI1394_AsReqTrContextControlSet) 
			  & 0xF;

			packet = ohci->async_queue;
			ohci->async_queue = packet->xnext;
	    
			if (ohci->async_queue != NULL) {
				send_next_async(ohci);
			}
			spin_unlock(&ohci->async_queue_lock);
#if 0
			PRINT(KERN_INFO,ohci->id,
			      "packet sent with ack code %d",ack);
#endif
			hpsb_packet_sent(host, packet, ack);	    
		} else 
		  PRINT(KERN_INFO,ohci->id,
			"packet sent without async_queue (self-id?)");

		ohci->TxRdy++;
	}

	ohci->NumInterrupts++;
}


/* This is the bottom half that processes async response receive descriptor buffers. */
static void ohci_ar_resp_proc_desc(void *data)
{
	quadlet_t *buf_ptr;
	char *split_ptr;
	unsigned int split_left;
	struct ti_ohci *ohci= (struct ti_ohci*)data;
        unsigned int packet_length;
        unsigned int idx,offset,tcode;
        unsigned long flags;
	char msg[256];

        spin_lock_irqsave(&ohci->AR_resp_lock, flags);

	idx = ohci->AR_resp_buf_bh_ind;
	offset = ohci->AR_resp_buf_bh_offset;

	buf_ptr = ohci->AR_resp_buf[idx];
	buf_ptr += offset/4;

        while(ohci->AR_resp_bytes_left > 0) {

                /* check to see if a fatal error occurred */
                if ((ohci->AR_resp_prg[idx]->status >> 16) & 0x800) {
			sprintf(msg,"fatal async response receive error -- status is %d",
				ohci->AR_resp_prg[idx]->status & 0x1F);
			stop_ar_resp_context(ohci, msg);
                        spin_unlock_irqrestore(&ohci->AR_resp_lock, flags);
                        return;
                }

                spin_unlock_irqrestore(&ohci->AR_resp_lock, flags);

		/* Let's see what kind of packet is in there */
		tcode = (buf_ptr[0]>>4)&0xf;
		if (tcode==2) /* no-data receive */
		  packet_length=16;
		else if (tcode==6) /* quadlet receive */
		  packet_length=20;
		else if (tcode==7) { /* block receive */
			/* Where is the data length ? */
			if (offset+12>=AR_RESP_BUF_SIZE) 
			  packet_length=(ohci->AR_resp_buf[(idx+1)%AR_RESP_NUM_DESC]
					 [3-(AR_RESP_BUF_SIZE-offset)/4]>>16)+20;
			else 
			  packet_length=(buf_ptr[3]>>16)+20;
                        if (packet_length % 4)
			  packet_length += 4 - (packet_length % 4);
		}
		else /* something is wrong */ {
			sprintf(msg,"unexpected packet tcode %d in async response receive buffer",tcode);
			stop_ar_resp_context(ohci,msg);
			return;
		}
		if ((offset+packet_length)>AR_RESP_BUF_SIZE) {
			/* we have a split packet */
			if (packet_length>AR_RESP_SPLIT_PACKET_BUF_SIZE) {
				sprintf(msg,"packet size %d bytes exceed split packet buffer size %d bytes",
					packet_length,AR_RESP_SPLIT_PACKET_BUF_SIZE);
				stop_ar_resp_context(ohci, msg);
				return;
			}
			split_left = packet_length;
			split_ptr = (char *)ohci->AR_resp_spb;
			while (split_left>0) {
				memcpy(split_ptr,buf_ptr,AR_RESP_BUF_SIZE-offset);
				split_left -= AR_RESP_BUF_SIZE-offset;
				split_ptr += AR_RESP_BUF_SIZE-offset;
				ohci->AR_resp_prg[idx]->status = AR_RESP_BUF_SIZE;
				idx = (idx+1) % AR_RESP_NUM_DESC;
				buf_ptr = ohci->AR_resp_buf[idx];
				offset=0;
				while (split_left >= AR_RESP_BUF_SIZE) {
					memcpy(split_ptr,buf_ptr,AR_RESP_BUF_SIZE);
					split_ptr += AR_RESP_BUF_SIZE;
					split_left -= AR_RESP_BUF_SIZE;
					ohci->AR_resp_prg[idx]->status = AR_RESP_BUF_SIZE;
					idx = (idx+1) % AR_RESP_NUM_DESC;
					buf_ptr = ohci->AR_resp_buf[idx];
				}
				if (split_left>0) {
					memcpy(split_ptr,buf_ptr,split_left);
					offset = split_left;
					split_left=0;
					buf_ptr += split_left/4;
				}
			}
#if 0
			PRINT(KERN_INFO,ohci->id,"AR resp: received split packet tcode=%d length=%d",
			      tcode,packet_length);
#endif
			hpsb_packet_received(ohci->host, ohci->AR_resp_spb, packet_length);
			ohci->AR_resp_bytes_left -= packet_length;
		}
		else {
#if 0
			PRINT(KERN_INFO,ohci->id,"AR resp: received packet tcode=%d length=%d",
			      tcode,packet_length);
#endif
			hpsb_packet_received(ohci->host, buf_ptr, packet_length);
			offset += packet_length;
			buf_ptr += packet_length/4;
			ohci->AR_resp_bytes_left -= packet_length;
			if (offset==AR_RESP_BUF_SIZE) {
				ohci->AR_resp_prg[idx]->status = AR_RESP_BUF_SIZE;
				idx = (idx+1) % AR_RESP_NUM_DESC;
				buf_ptr = ohci->AR_resp_buf[idx];
				offset=0;
			}
		}
		
	}
	
	if (ohci->AR_resp_bytes_left<0) 
	  stop_ar_resp_context(ohci, "Sync problem in AR resp dma buffer");

	ohci->AR_resp_buf_bh_ind = idx;
	ohci->AR_resp_buf_bh_offset = offset;

        spin_unlock_irqrestore(&ohci->AR_resp_lock, flags);
}

/* This is the bottom half that processes iso receive descriptor buffers. */
static void ohci_ir_proc_desc(void *data)
{
	quadlet_t *buf_ptr;
	struct ti_ohci *ohci= (struct ti_ohci*)data;
        int bytes_left, data_length;
        unsigned int idx;
        unsigned long flags;

        spin_lock_irqsave(&ohci->IR_recv_lock, flags);

        while(ohci->IR_buf_used > 0)
        {
                idx= ohci->IR_buf_last_ind;

                /* check to see if a fatal error occurred */
                if ((ohci->IR_recv_prg[idx]->status >> 16) & 0x800) {
                        int i= 0;

                        /* stop the context */
			reg_write(ohci, OHCI1394_IrRcvContextControlClear,
                                  0x8000);

			while (reg_read(ohci, OHCI1394_IrRcvContextControlSet) 
			       & 0x400) {
				i++;

				if (i > 5000) {
					PRINT(KERN_ERR, ohci->id, "runaway loop in DmaIR. bailing out...");
					break;
				}

			}

                        spin_unlock_irqrestore(&ohci->IR_recv_lock, flags);
                        PRINT(KERN_ERR, ohci->id,
                              "fatal iso receive error -- status is %d",
                              ohci->IR_recv_prg[idx]->status & 0x1F);
                        return;
                }

                spin_unlock_irqrestore(&ohci->IR_recv_lock, flags);

                buf_ptr= bus_to_virt(ohci->IR_recv_prg[idx]->address);
                bytes_left= IR_RECV_BUF_SIZE;

                /* are we processing a split packet from last buffer */
                if (ohci->IR_sp_bytes_left) {

                        if (!ohci->IR_spb_bytes_used) {
                                /* packet is in process of being dropped */
                                if (ohci->IR_sp_bytes_left > bytes_left) {
                                        ohci->IR_sp_bytes_left-= bytes_left;
                                        bytes_left= 0;
                                } else {
                                        buf_ptr= bus_to_virt((unsigned long)
                                                &((quadlet_t*)ohci->IR_recv_prg
                                                [idx]->address)
                                                [ohci->IR_sp_bytes_left / 4]);
                                        bytes_left-= ohci->IR_sp_bytes_left;
                                        ohci->IR_sp_bytes_left= 0;
                                }

                        } else {
                                /* packet is being assembled */
                                if (ohci->IR_sp_bytes_left > bytes_left) {
                                        memcpy(&ohci->IR_spb
                                               [ohci->IR_spb_bytes_used / 4],
                                               buf_ptr, bytes_left);
                                        ohci->IR_spb_bytes_used+= bytes_left;
                                        ohci->IR_sp_bytes_left-= bytes_left;
                                        bytes_left= 0;
                                } else {
                                        memcpy(&ohci->IR_spb
                                               [ohci->IR_spb_bytes_used / 4],
                                               buf_ptr,
                                               ohci->IR_sp_bytes_left);
                                        ohci->IR_spb_bytes_used+=
                                                ohci->IR_sp_bytes_left;
                                        hpsb_packet_received(ohci->host,
                                                      ohci->IR_spb,
                                                      ohci->IR_spb_bytes_used);
                                        buf_ptr=
                                                bus_to_virt((unsigned long)
					       &((quadlet_t*)ohci->IR_recv_prg
						 [idx]->address)
					         [ohci->IR_sp_bytes_left / 4]);
                                        bytes_left-= ohci->IR_sp_bytes_left;
                                        ohci->IR_sp_bytes_left= 0;
                                        ohci->IR_spb_bytes_used= 0;
                                }

                        }

                }

                while(bytes_left > 0) {
                        data_length= (int)((buf_ptr[0] >> 16) & 0xffff);

                        if (data_length % 4)
                                data_length+= 4 - (data_length % 4);

                        /* is this a split packet? */
                        if ( (bytes_left - (data_length + 8)) < 0 ) {

                                if ( (data_length + 8) <=
                                     IR_SPLIT_PACKET_BUF_SIZE ) {
                                        memcpy(ohci->IR_spb, buf_ptr,
                                               bytes_left);
                                        ohci->IR_spb_bytes_used= bytes_left;
                                } else {
                                        PRINT(KERN_ERR, ohci->id, "Packet too large for split packet buffer... dropping it");
                                        PRINT(KERN_DEBUG, ohci->id, "Header: %8.8x\n", buf_ptr[0]);
                                        ohci->IR_spb_bytes_used= 0;
                                }

                                ohci->IR_sp_bytes_left=
                                        (data_length + 8) - bytes_left;
                        } else {
                                hpsb_packet_received(ohci->host, buf_ptr,
                                                     (data_length + 8));
                                buf_ptr= bus_to_virt((unsigned long)
                                               &((quadlet_t*)ohci->IR_recv_prg
                                                [idx]->address)
                                                [(IR_RECV_BUF_SIZE - bytes_left
                                                  + data_length + 8) / 4]);
                        }

                        bytes_left-= (data_length + 8);
                }

                spin_lock_irqsave(&ohci->IR_recv_lock, flags);
                ohci->IR_buf_last_ind= (idx + 1) % IR_NUM_DESC;
                ohci->IR_buf_used--;
        }

        spin_unlock_irqrestore(&ohci->IR_recv_lock, flags);
}

static int add_card(struct pci_dev *dev)
{
#define FAIL(fmt, args...) \
	PRINT_G(KERN_ERR, fmt , ## args); \
	  num_of_cards--; \
	    remove_card(ohci); \
	      return 1;

	struct ti_ohci *ohci;	/* shortcut to currently handled device */
	int i;

	if (num_of_cards == MAX_OHCI1394_CARDS) {
		PRINT_G(KERN_WARNING, "cannot handle more than %d cards.  "
			"Adjust MAX_OHCI1394_CARDS in ti_ohci1394.h.",
			MAX_OHCI1394_CARDS);
		return 1;
	}

	ohci = &cards[num_of_cards++];

	ohci->id = num_of_cards-1;
	ohci->dev = dev;
	
	ohci->state = 0;

	if (!request_irq(dev->irq, ohci_irq_handler, SA_SHIRQ,
			 OHCI1394_DRIVER_NAME, ohci)) {
		PRINT(KERN_INFO, ohci->id, "allocated interrupt %d", dev->irq);
	} else {
		FAIL("failed to allocate shared interrupt %d", dev->irq);
	}

	/* csr_config rom allocation */
	ohci->csr_config_rom = kmalloc(1024, GFP_KERNEL);
	if (ohci->csr_config_rom == NULL) {
		FAIL("failed to allocate buffer config rom");
	}
	memcpy(ohci->csr_config_rom, ohci_csr_rom, sizeof(ohci_csr_rom));

	/* self-id dma buffer allocation */
	ohci->self_id_buffer = kmalloc(2048, GFP_KERNEL);
	if (ohci->self_id_buffer == NULL) {
		FAIL("failed to allocate DMA buffer for self-id packets");
	}

	/* AR dma buffer and program allocation */
	ohci->AR_resp_buf=
	        kmalloc(AR_RESP_NUM_DESC * sizeof(quadlet_t*),
			GFP_KERNEL);

	if (ohci->AR_resp_buf == NULL) {
		FAIL("failed to allocate AR response receive DMA buffer");
	}

	ohci->AR_resp_prg=
		kmalloc(AR_RESP_NUM_DESC * sizeof(struct dma_cmd*),
			GFP_KERNEL);

	if (ohci->AR_resp_prg == NULL) {
		FAIL("failed to allocate AR response receive DMA program");
	}

	ohci->AR_resp_spb= kmalloc(AR_RESP_SPLIT_PACKET_BUF_SIZE, GFP_KERNEL);

	if (ohci->AR_resp_spb == NULL) {
		FAIL("failed to allocate AR response split packet buffer");
	}

	for (i= 0; i < AR_RESP_NUM_DESC; i++) {
                ohci->AR_resp_buf[i]= kmalloc(AR_RESP_BUF_SIZE, GFP_KERNEL);

                if (ohci->AR_resp_buf[i] != NULL) {
                        memset(ohci->AR_resp_buf[i], 0, AR_RESP_BUF_SIZE);
                } else {
                        FAIL("failed to allocate AR response DMA buffer");
                }

                ohci->AR_resp_prg[i]= kmalloc(sizeof(struct dma_cmd),
                                                   GFP_KERNEL);

                if (ohci->AR_resp_prg[i] != NULL) {
                        memset(ohci->AR_resp_prg[i], 0,
                               sizeof(struct dma_cmd));
                } else {
                        FAIL("failed to allocate AR response DMA buffer");
                }

	}

        ohci->AR_resp_buf_th_ind = 0;
        ohci->AR_resp_buf_th_offset = 0;
        ohci->AR_resp_buf_bh_ind = 0;
        ohci->AR_resp_buf_bh_offset = 0;
        ohci->AR_resp_bytes_left = 0;
        spin_lock_init(&ohci->AR_resp_lock);

        /* initialize AR response receive task */
        ohci->AR_resp_pdl_task.routine= ohci_ar_resp_proc_desc;
        ohci->AR_resp_pdl_task.data= (void*)ohci;

	/* AT dma program allocation */
	ohci->AT_req_prg = (struct dma_cmd *) kmalloc(AT_REQ_PRG_SIZE, 
						      GFP_KERNEL);
	if (ohci->AT_req_prg != NULL) {
		memset(ohci->AT_req_prg, 0, AT_REQ_PRG_SIZE);
	} else {
		FAIL("failed to allocate AT request DMA program");
	}

	/* IR dma buffer and program allocation */
	ohci->IR_recv_buf=
		kmalloc(IR_NUM_DESC * sizeof(quadlet_t*),
			GFP_KERNEL);

	if (ohci->IR_recv_buf == NULL) {
		FAIL("failed to allocate IR receive DMA buffer");
	}

	ohci->IR_recv_prg=
		kmalloc(IR_NUM_DESC * sizeof(struct dma_cmd*),
			GFP_KERNEL);

	if (ohci->IR_recv_prg == NULL) {
		FAIL("failed to allocate IR receive DMA program");
	}

	ohci->IR_spb= kmalloc(IR_SPLIT_PACKET_BUF_SIZE, GFP_KERNEL);

	if (ohci->IR_spb == NULL) {
		FAIL("failed to allocate IR split packet buffer");
	}

	for (i= 0; i < IR_NUM_DESC; i++) {
                ohci->IR_recv_buf[i]= kmalloc(IR_RECV_BUF_SIZE, GFP_KERNEL);

                if (ohci->IR_recv_buf[i] != NULL) {
                        memset(ohci->IR_recv_buf[i], 0, IR_RECV_BUF_SIZE);
                } else {
                        FAIL("failed to allocate IR DMA buffer");
                }

                ohci->IR_recv_prg[i]= kmalloc(sizeof(struct dma_cmd),
                                              GFP_KERNEL);

                if (ohci->IR_recv_prg[i] != NULL) {
                        memset(ohci->IR_recv_prg[i], 0,
                               sizeof(struct dma_cmd));
                } else {
                        FAIL("failed to allocate IR DMA buffer");
                }

	}

        ohci->IR_buf_used= 0;
        ohci->IR_buf_last_ind= 0;
        ohci->IR_buf_next_ind= 0;
        spin_lock_init(&ohci->IR_recv_lock);
        spin_lock_init(&ohci->IR_channel_lock);
        ohci->IR_channel_usage= 0x0000000000000000;

        /* initialize iso receive task */
        ohci->IR_pdl_task.routine= ohci_ir_proc_desc;
        ohci->IR_pdl_task.data= (void*)ohci;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13)
	ohci->registers = ioremap_nocache(dev->base_address[0],
					  OHCI1394_REGISTER_SIZE);
#else
	ohci->registers = ioremap_nocache(dev->resource[0].start,
					  OHCI1394_REGISTER_SIZE);
#endif

	if (ohci->registers == NULL) {
		FAIL("failed to remap registers - card not accessible");
	}

	PRINT(KERN_INFO, ohci->id, "remapped memory spaces reg 0x%p",
	      ohci->registers);

	return 0;
#undef FAIL
}

#ifdef CONFIG_PROC_FS

#define SR(fmt, reg0, reg1, reg2)\
p += sprintf(p,fmt,reg_read(ohci, reg0),\
	       reg_read(ohci, reg1),reg_read(ohci, reg2));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
static int ohci_get_status(char *buf)
#else
int ohci_get_info(char *buf, char **start, off_t fpos, 
		  int length, int dummy)
#endif
{
	struct ti_ohci *ohci=&cards[0];
	struct hpsb_host *host=ohci->host;
	char *p=buf;
	//unsigned char phyreg;
	//int i, nports;
	int i;

	p += sprintf(p,"IEEE-1394 OHCI Driver status report:\n");
	p += sprintf(p,"  bus number: 0x%x Node ID: 0x%x\n", 
		     (reg_read(ohci, OHCI1394_NodeID) & 0xFFC0) >> 6, 
		     reg_read(ohci, OHCI1394_NodeID)&0x3f);
#if 0
	p += sprintf(p,"  hardware version %d.%d GUID_ROM is %s\n\n", 
		     (reg_read(ohci, OHCI1394_Version) & 0xFF0000) >>16, 
		     reg_read(ohci, OHCI1394_Version) & 0xFF,
		     (reg_read(ohci, OHCI1394_Version) & 0x01000000) 
		     ? "set" : "clear");
#endif
	p += sprintf(p,"\n### Host data ###\n");
	p += sprintf(p,"node_count: %8d  ",host->node_count);
	p += sprintf(p,"node_id   : %08X\n",host->node_id);
	p += sprintf(p,"irm_id    : %08X  ",host->irm_id);
	p += sprintf(p,"busmgr_id : %08X\n",host->busmgr_id);
	p += sprintf(p,"%s %s %s\n",
		     host->initialized ? "initialized" : "",
		     host->in_bus_reset ? "in_bus_reset" : "",
		     host->attempt_root ? "attempt_root" : "");
	p += sprintf(p,"%s %s %s %s\n",
		     host->is_root ? "root" : "",
		     host->is_cycmst ? "cycle_master" : "",
		     host->is_irm ? "iso_res_mgr" : "",
		     host->is_busmgr ? "bus_mgr" : "");
  
	p += sprintf(p,"\n---Iso Receive DMA---\n");
        for (i= 0; i < IR_NUM_DESC; i++) {
                p += sprintf(p, "IR_recv_buf[%d] : %p  IR_recv_prg[%d]: %p\n",
                             i, ohci->IR_recv_buf[i], i, ohci->IR_recv_prg[i]);
	}

	
	p += sprintf(p,"\n---Async Reponse Receive DMA---\n");
        for (i= 0; i < AR_RESP_NUM_DESC; i++) {
                p += sprintf(p, "AR_resp_buf[%d] : %p  AR_resp_prg[%d]: %p\n",
                             i, ohci->AR_resp_buf[i], i, ohci->AR_resp_prg[i]);
	}
	p += sprintf(p, "Current AR resp buf in irq handler: %d offset: %d\n",
		     ohci->AR_resp_buf_th_ind,ohci->AR_resp_buf_th_offset);
	p += sprintf(p, "Current AR resp buf in bottom half: %d offset: %d\n",
		     ohci->AR_resp_buf_bh_ind,ohci->AR_resp_buf_bh_offset);
	
	/* ----- Register Dump ----- */
	p += sprintf(p,"\n### HC Register dump ###\n");
	SR("Version     : %08x  GUID_ROM    : %08x  ATRetries   : %08x\n",
	   OHCI1394_Version, OHCI1394_GUID_ROM, OHCI1394_ATRetries);
	SR("CSRReadData : %08x  CSRCompData : %08x  CSRControl  : %08x\n",
	   OHCI1394_CSRReadData, OHCI1394_CSRCompareData, OHCI1394_CSRControl);
	SR("ConfigROMhdr: %08x  BusID       : %08x  BusOptions  : %08x\n",
	   OHCI1394_ConfigROMhdr, OHCI1394_BusID, OHCI1394_BusOptions);
	SR("GUIDHi      : %08x  GUIDLo      : %08x  ConfigROMmap: %08x\n",
	   OHCI1394_GUIDHi, OHCI1394_GUIDLo, OHCI1394_ConfigROMmap);
	SR("PtdWrAddrLo : %08x  PtdWrAddrHi : %08x  VendorID    : %08x\n",
	   OHCI1394_PostedWriteAddressLo, OHCI1394_PostedWriteAddressHi, 
	   OHCI1394_VendorID);
	SR("HCControl   : %08x  SelfIDBuffer: %08x  SelfIDCount : %08x\n",
	   OHCI1394_HCControlSet, OHCI1394_SelfIDBuffer, OHCI1394_SelfIDCount);
	SR("IRMuChMaskHi: %08x  IRMuChMaskLo: %08x  IntEvent    : %08x\n",
	   OHCI1394_IRMultiChanMaskHiSet, OHCI1394_IRMultiChanMaskLoSet, 
	   OHCI1394_IntEventSet);
	SR("IntMask     : %08x  IsoXmIntEvnt: %08x  IsoXmIntMask: %08x\n",
	   OHCI1394_IntMaskSet, OHCI1394_IsoXmitIntEventSet, 
	   OHCI1394_IsoXmitIntMaskSet);
	SR("IsoRcvIntEvt: %08x  IsoRcvIntMsk: %08x  FairnessCtrl: %08x\n",
	   OHCI1394_IsoRecvIntEventSet, OHCI1394_IsoRecvIntMaskSet, 
	   OHCI1394_FairnessControl);
	SR("LinkControl : %08x  NodeID      : %08x  PhyControl  : %08x\n",
	   OHCI1394_LinkControlSet, OHCI1394_NodeID, OHCI1394_PhyControl);
	SR("IsoCyclTimer: %08x  AsRqFilterHi: %08x  AsRqFilterLo: %08x\n",
	   OHCI1394_IsochronousCycleTimer, 
	   OHCI1394_AsReqFilterHiSet, OHCI1394_AsReqFilterLoSet);
	SR("PhyReqFiltHi: %08x  PhyReqFiltLo: %08x  PhyUpperBnd : %08x\n",
	   OHCI1394_PhyReqFilterHiSet, OHCI1394_PhyReqFilterLoSet, 
	   OHCI1394_PhyUpperBound);
	SR("AsRqTrCxtCtl: %08x  AsRqTrCmdPtr: %08x  AsRsTrCtxCtl: %08x\n",
	   OHCI1394_AsReqTrContextControlSet, OHCI1394_AsReqTrCommandPtr, 
	   OHCI1394_AsRspTrContextControlSet);
	SR("AsRsTrCmdPtr: %08x  AsRqRvCtxCtl: %08x  AsRqRvCmdPtr: %08x\n",
	   OHCI1394_AsRspTrCommandPtr, OHCI1394_AsReqRcvContextControlSet,
	   OHCI1394_AsReqRcvCommandPtr);
	SR("AsRsRvCtxCtl: %08x  AsRsRvCmdPtr: %08x  IntEvent    : %08x\n",
	   OHCI1394_AsRspRcvContextControlSet, OHCI1394_AsRspRcvCommandPtr, 
	   OHCI1394_IntEventSet);
	
#if 0
	p += sprintf(p,"\n### Phy Register dump ###\n");
	phyreg=get_phy_reg(ohci,1);
	p += sprintf(p,"offset: %d val: 0x%02x -> RHB: %d IBR: %d Gap_count: %d\n",
		     1,phyreg,(phyreg&0x80) != 0, (phyreg&0x40) !=0, phyreg&0x3f);
	phyreg=get_phy_reg(ohci,2);
	nports=phyreg&0x1f;
	p += sprintf(p,"offset: %d val: 0x%02x -> SPD: %d E  : %d Ports    : %2d\n",
		     2,phyreg,
		     (phyreg&0xC0)>>6, (phyreg&0x20) !=0, nports);
	for (i=0;i<nports;i++) {
		phyreg=get_phy_reg(ohci,3+i);
		p += sprintf(p,"offset: %d val: 0x%02x -> [port %d] TPA: %d TPB: %d | %s %s\n",
			     3+i,phyreg,
			     i, (phyreg&0xC0)>>6, (phyreg&0x30)>>4,
			     (phyreg&0x08) ? "child" : "parent",
			     (phyreg&0x04) ? "connected" : "disconnected");
	}
	phyreg=get_phy_reg(ohci,3+i);
	p += sprintf(p,"offset: %d val: 0x%02x -> ENV: %s Reg_count: %d\n",
		     3+i,phyreg,
		     (((phyreg&0xC0)>>6)==0) ? "backplane" :
		     (((phyreg&0xC0)>>6)==1) ? "cable" : "reserved",
		     phyreg&0x3f);
#endif

#if 0
	p += sprintf(p,"AR_resp_prg ctrl: %08x\n",ohci->AR_resp_prg->control);
	p += sprintf(p,"AR_resp_prg status: %08x\n",ohci->AR_resp_prg->status);
#endif

	return  p - buf;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
static int ohci1394_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
        int len = ohci_get_status(page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}
#else
struct proc_dir_entry ohci_proc_entry = 
{
	0,			/* Inode number - dynamic */
	8,			/* Length of the file name */
	"ohci1394",		/* The file name */
	S_IFREG | S_IRUGO,	/* File mode */
	1,			/* Number of links */
	0, 0,			/* The uid and gid for the file */
	0,			/* The size of the file reported by ls. */
	NULL,			/* functions which can be done on the inode */
	ohci_get_info,		/* The read function for this file */
	NULL
}; 
#endif /* LINUX_VERSION_CODE */
#endif /* CONFIG_PROC_FS */

static void remove_card(struct ti_ohci *ohci)
{
	if (ohci->registers) 
	  iounmap(ohci->registers);

	/* Free AR response buffers and programs */
	if (ohci->AR_resp_buf) {
		int i;
                for (i= 0; i < AR_RESP_NUM_DESC; i++) {
			kfree(ohci->AR_resp_buf[i]);
		}
		kfree(ohci->AR_resp_buf);
	}
	if (ohci->AR_resp_prg) {
		int i;
		for (i= 0; i < AR_RESP_NUM_DESC; i++) {
			kfree(ohci->AR_resp_prg[i]);
		}
		kfree(ohci->AR_resp_prg);
	}
	kfree(ohci->AR_resp_spb);

	/* Free AT request buffer and program */
	if (ohci->AT_req_prg) 
	  kfree(ohci->AT_req_prg);

	/* Free Iso receive buffers and programs */
	if (ohci->IR_recv_buf) {
		int i;
                for (i= 0; i < IR_NUM_DESC; i++) {
			kfree(ohci->IR_recv_buf[i]);
		}
		kfree(ohci->IR_recv_buf);
	}
	if (ohci->IR_recv_prg) {
		int i;
		for (i= 0; i < IR_NUM_DESC; i++) {
			kfree(ohci->IR_recv_prg[i]);
		}
		kfree(ohci->IR_recv_prg);
	}
	kfree(ohci->IR_spb);

	/* Free self-id buffer */
	if (ohci->self_id_buffer)
	  kfree(ohci->self_id_buffer);
	
	/* Free config rom */
	if (ohci->csr_config_rom)
	  kfree(ohci->csr_config_rom);

	/* Free the IRQ */
	free_irq(ohci->dev->irq, ohci);

	ohci->state = 0;
}

static int init_driver()
{
	struct pci_dev *dev = NULL;
	int success = 0;
	int i;

	if (num_of_cards) {
		PRINT_G(KERN_DEBUG, __PRETTY_FUNCTION__ " called again");
		return 0;
	}

	PRINT_G(KERN_INFO, "looking for Ohci1394 cards");

	for (i = 0; supported_chips[i][0] != -1; i++) {
	    while ((dev = pci_find_device(supported_chips[i][0],
					  supported_chips[i][1], dev)) 
		   != NULL) {
		if (add_card(dev) == 0) {
		    success = 1;
		}
	    }
	}

	if (success == 0) {
		PRINT_G(KERN_WARNING, "no operable Ohci1394 cards found");
		return -ENXIO;
	}

#ifdef CONFIG_PROC_FS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	create_proc_read_entry ("ohci1394", 0, NULL, ohci1394_read_proc, NULL);
#else
	if (proc_register(&proc_root, &ohci_proc_entry)) {
		PRINT_G(KERN_ERR, "unable to register proc file\n");
		return -EIO;
	}
#endif
#endif
	return 0;
}

static size_t get_ohci_rom(struct hpsb_host *host, const quadlet_t **ptr)
{
	struct ti_ohci *ohci=host->hostdata;

#if 0
	PRINT(KERN_INFO, ohci->id, "request csr_rom address: %08X",
	      (u32)ohci->csr_config_rom);
#endif

	*ptr = ohci->csr_config_rom;
	return sizeof(ohci_csr_rom);
}

struct hpsb_host_template *get_ohci_template(void)
{
	static struct hpsb_host_template tmpl;
	static int initialized = 0;

	if (!initialized) {
		/* Initialize by field names so that a template structure
		 * reorganization does not influence this code. */
		tmpl.name = "ohci1394";
                
		tmpl.detect_hosts = ohci_detect;
		tmpl.initialize_host = ohci_initialize;
		tmpl.release_host = ohci_remove;
		tmpl.get_rom = get_ohci_rom;
		tmpl.transmit_packet = ohci_transmit;
		tmpl.devctl = ohci_devctl;

		initialized = 1;
	}

	return &tmpl;
}


#ifdef MODULE

/* EXPORT_NO_SYMBOLS; */

MODULE_AUTHOR("Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>");
MODULE_DESCRIPTION("driver for PCI Ohci IEEE-1394 controller");
MODULE_SUPPORTED_DEVICE("ohci1394");

void cleanup_module(void)
{
	hpsb_unregister_lowlevel(get_ohci_template());
#ifdef CONFIG_PROC_FS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	remove_proc_entry ("ohci1394", NULL);
#else
	proc_unregister(&proc_root, ohci_proc_entry.low_ino);
#endif
#endif
	PRINT_G(KERN_INFO, "removed " OHCI1394_DRIVER_NAME " module\n");
}

int init_module(void)
{

	if (hpsb_register_lowlevel(get_ohci_template())) {
		PRINT_G(KERN_ERR, "registering failed\n");
		return -ENXIO;
	} else {
		return 0;
	}
}

#endif /* MODULE */

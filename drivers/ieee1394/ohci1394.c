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

/*
 * Things known to be working:
 * . Async Request Transmit
 * . Async Response Receive
 * . Async Request Receive
 * . Async Response Transmit
 * . Iso Receive
 * 
 * Things not implemented:
 * . Iso Transmit
 * . DMA to user's space in iso receive mode
 * . DMA error recovery
 *
 * Things to be fixed:
 * . Config ROM
 *
 * Known bugs:
 * . Self-id are not received properly if card 
 *   is initialized with no other nodes on the
 *   bus.
 */

#include <linux/config.h>
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

#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/wrapper.h>
#include <linux/vmalloc.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "ohci1394.h"

#ifdef DBGMSG
#undef DBGMSG
#endif

#if OHCI1394_DEBUG
#define DBGMSG(card, fmt, args...) \
printk(KERN_INFO "ohci1394_%d: " fmt "\n" , card , ## args)
#else
#define DBGMSG(card, fmt, args...)
#endif

/* print general (card independent) information */
#define PRINT_G(level, fmt, args...) \
printk(level "ohci1394: " fmt "\n" , ## args)

/* print card specific information */
#define PRINT(level, card, fmt, args...) \
printk(level "ohci1394_%d: " fmt "\n" , card , ## args)

#define FAIL(fmt, args...) \
	PRINT_G(KERN_ERR, fmt , ## args); \
	  num_of_cards--; \
	    remove_card(ohci); \
	      return 1;

int supported_chips[][2] = {
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_OHCI1394 },
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_OHCI1394_2 },
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_OHCI1394 },
	{ PCI_VENDOR_ID_SONY, PCI_DEVICE_ID_SONY_CXD3222 },
	{ -1, -1 }
};

static struct ti_ohci cards[MAX_OHCI1394_CARDS];
static int num_of_cards = 0;

static int add_card(struct pci_dev *dev);
static void remove_card(struct ti_ohci *ohci);
static int init_driver(void);
static void dma_trm_bh(void *data);
static void dma_trm_reset(struct dma_trm_ctx *d);

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
		      "Error in reception of self-id packets"
		      "Self-id count: %08x q[0]: %08x",
		      self_id_count, q[0]);
		return -1;
	}
	
	size = ((self_id_count&0x0000EFFC)>>2) - 1;
	q++;

	while (size > 0) {
		if (q[0] == ~q[1]) {
			PRINT(KERN_INFO, ohci->id, "selfid packet 0x%x rcvd", 
			      q[0]);
			hpsb_selfid_received(host, q[0]);
			if (((q[0]&0x3f000000)>>24)==phyid) {
				lsid=q[0];
				PRINT(KERN_INFO, ohci->id, 
				      "This node self-id is 0x%08x", lsid);
			}
		} else {
			PRINT(KERN_ERR, ohci->id,
			      "inconsistent selfid 0x%x/0x%x", q[0], q[1]);
		}
		q += 2;
		size -= 2;
	}

	PRINT(KERN_INFO, ohci->id, "calling self-id complete");

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

static int run_context(struct ti_ohci *ohci, int reg, char *msg)
{
	u32 nodeId;

	/* check that the node id is valid */
	nodeId = reg_read(ohci, OHCI1394_NodeID);
	if (!(nodeId&0x80000000)) {
		PRINT(KERN_ERR, ohci->id, 
		      "Running dma failed because Node ID not valid");
		return -1;
	}

	/* check that the node number != 63 */
	if ((nodeId&0x3f)==63) {
		PRINT(KERN_ERR, ohci->id, 
		      "Running dma failed because Node ID == 63");
		return -1;
	}
	
	/* Run the dma context */
	reg_write(ohci, reg, 0x8000);
	
	if (msg) PRINT(KERN_INFO, ohci->id, "%s", msg);
	
	return 0;
}

static void stop_context(struct ti_ohci *ohci, int reg, char *msg)
{
	int i=0;

	/* stop the channel program if it's still running */
	reg_write(ohci, reg, 0x8000);
   
	/* Wait until it effectively stops */
	while (reg_read(ohci, reg) & 0x400) {
		i++;
		if (i>5000) {
			PRINT(KERN_ERR, ohci->id, 
			      "runaway loop while stopping context...");
			break;
		}
	}
	if (msg) PRINT(KERN_ERR, ohci->id, "%s\n dma prg stopped\n", msg);
}

/* Generate the dma receive prgs and start the context */
static void initialize_dma_rcv_ctx(struct dma_rcv_ctx *d)
{
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	int i;

	stop_context(ohci, d->ctrlClear, NULL);

	for (i=0; i<d->num_desc; i++) {
		
		/* end of descriptor list? */
		if ((i+1) < d->num_desc) {
			d->prg[i]->control = (0x283C << 16) | d->buf_size;
			d->prg[i]->branchAddress =
				(virt_to_bus(d->prg[i+1]) & 0xfffffff0) | 0x1;
		} else {
			d->prg[i]->control = (0x283C << 16) | d->buf_size;
			d->prg[i]->branchAddress =
				(virt_to_bus(d->prg[0]) & 0xfffffff0);
		}

		d->prg[i]->address = virt_to_bus(d->buf[i]);
		d->prg[i]->status = d->buf_size;
	}

        d->buf_ind = 0;
        d->buf_offset = 0;

	/* Tell the controller where the first AR program is */
	reg_write(ohci, d->cmdPtr, virt_to_bus(d->prg[0]) | 0x1);

	/* Run AR context */
	reg_write(ohci, d->ctrlSet, 0x00008000);

	PRINT(KERN_INFO, ohci->id, "Receive DMA ctx=%d initialized", d->ctx);
}

/* Initialize the dma transmit context */
static void initialize_dma_trm_ctx(struct dma_trm_ctx *d)
{
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);

	/* Stop the context */
	stop_context(ohci, d->ctrlClear, NULL);

        d->prg_ind = 0;
	d->sent_ind = 0;
	d->free_prgs = d->num_desc;
        d->branchAddrPtr = NULL;
	d->first = NULL;
	d->last = NULL;

	PRINT(KERN_INFO, ohci->id, "AT dma ctx=%d initialized", d->ctx);
}

/* Global initialization */
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

	/* Write the config ROM header */
	reg_write(ohci, OHCI1394_ConfigROMhdr, ohci->csr_config_rom[0]);

	/* Set bus options */
	reg_write(ohci, OHCI1394_BusOptions, ohci->csr_config_rom[2]);

	/* Write the GUID into the csr config rom */
	ohci->csr_config_rom[3] = reg_read(ohci, OHCI1394_GUIDHi);
	ohci->csr_config_rom[4] = reg_read(ohci, OHCI1394_GUIDLo);
	
	/* Don't accept phy packets into AR request context */ 
	reg_write(ohci, OHCI1394_LinkControlClear, 0x00000400);

	/* Enable link */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00020000);

	/* Initialize IR dma */
	for (i=0;i<4;i++) { /* FIXME : how many contexts are available ? */
		reg_write(ohci, OHCI1394_IrRcvContextControlClear+32*i,
			  0xffffffff);
		reg_write(ohci, OHCI1394_IrRcvContextMatch+32*i, 0);
		reg_write(ohci, OHCI1394_IrRcvCommandPtr+32*i, 0);
	}

	/* Set bufferFill, isochHeader, multichannel for IR context */
	reg_write(ohci, OHCI1394_IrRcvContextControlSet, 0xd0000000);
			
	/* Set the context match register to match on all tags */
	reg_write(ohci, OHCI1394_IrRcvContextMatch, 0xf0000000);

	/* Clear the multi channel mask high and low registers */
	reg_write(ohci, OHCI1394_IRMultiChanMaskHiClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IRMultiChanMaskLoClear, 0xffffffff);

	/* Clear the interrupt mask */
	reg_write(ohci, OHCI1394_IsoRecvIntMaskClear, 0xffffffff);

	/* Set up isoRecvIntMask to generate interrupts for context 0
	   (thanks to Michael Greger for seeing that I forgot this) */
	reg_write(ohci, OHCI1394_IsoRecvIntMaskSet, 0x00000001);

	initialize_dma_rcv_ctx(ohci->ir_context);

	/* Initialize AR dma */
	initialize_dma_rcv_ctx(ohci->ar_req_context);
	initialize_dma_rcv_ctx(ohci->ar_resp_context);

	/* Initialize AT dma */
	initialize_dma_trm_ctx(ohci->at_req_context);
	initialize_dma_trm_ctx(ohci->at_resp_context);

	/* 
	 * Accept AT requests from all nodes. This probably 
	 * will have to be controlled from the subsystem
	 * on a per node basis.
	 * (Tip by Emilie Chung <emilie.chung@axis.com>)
	 */
	reg_write(ohci,OHCI1394_AsReqFilterHiSet, 0x80000000);

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

/* Insert a packet in the AT DMA fifo and generate the DMA prg */
static void insert_packet(struct ti_ohci *ohci,
			  struct dma_trm_ctx *d, struct hpsb_packet *packet)
{
	u32 cycleTimer;
	int idx = d->prg_ind;

	d->prg[idx].begin.address = 0;
	d->prg[idx].begin.branchAddress = 0;
	if (d->ctx==1) {
		/* 
		 * For response packets, we need to put a timeout value in
		 * the 16 lower bits of the status... let's try 1 sec timeout 
		 */ 
		cycleTimer = reg_read(ohci, OHCI1394_IsochronousCycleTimer);
		d->prg[idx].begin.status = 
			(((((cycleTimer>>25)&0x7)+1)&0x7)<<13) | 
			((cycleTimer&0x01fff000)>>12);

		DBGMSG(ohci->id, "cycleTimer: %08x timeStamp: %08x",
		       cycleTimer, d->prg[idx].begin.status);
	}
	else 
		d->prg[idx].begin.status = 0;

	d->prg[idx].data[0] = packet->speed_code<<16 |
		(packet->header[0] & 0xFFFF);
	d->prg[idx].data[1] = (packet->header[1] & 0xFFFF) | 
		(packet->header[0] & 0xFFFF0000);
	d->prg[idx].data[2] = packet->header[2];
	d->prg[idx].data[3] = packet->header[3];

	if (packet->data_size) { /* block transmit */
		d->prg[idx].begin.control = OUTPUT_MORE_IMMEDIATE | 0x10;
		d->prg[idx].end.control = OUTPUT_LAST | packet->data_size;
		d->prg[idx].end.address = virt_to_bus(packet->data);
		d->prg[idx].end.branchAddress = 0;
		d->prg[idx].end.status = 0x4000;
		if (d->branchAddrPtr) 
			*(d->branchAddrPtr) = virt_to_bus(d->prg+idx) | 0x3;
		d->branchAddrPtr = &(d->prg[idx].end.branchAddress);
	}
	else { /* quadlet transmit */
		d->prg[idx].begin.control = 
			OUTPUT_LAST_IMMEDIATE | packet->header_size;
		if (d->branchAddrPtr) 
			*(d->branchAddrPtr) = virt_to_bus(d->prg+idx) | 0x2;
		d->branchAddrPtr = &(d->prg[idx].begin.branchAddress);
	}
	d->free_prgs--;

	/* queue the packet in the appropriate context queue */
	if (d->last) {
		d->last->xnext = packet;
		d->last = packet;
	}
	else {
		d->first = packet;
		d->last = packet;
	}
}

static int ohci_transmit(struct hpsb_host *host, struct hpsb_packet *packet)
{
	struct ti_ohci *ohci = host->hostdata;
	struct dma_trm_ctx *d;
	unsigned char tcode;
	int i=50;

	if (packet->data_size >= 4096) {
		PRINT(KERN_ERR, ohci->id, "transmit packet data too big (%d)",
		      packet->data_size);
		return 0;
	}
	packet->xnext = NULL;

	/* Decide wether we have a request or a response packet */
	tcode = (packet->header[0]>>4)&0xf;
	if ((tcode==TCODE_READQ)||
	    (tcode==TCODE_WRITEQ)||
	    (tcode==TCODE_READB)||
	    (tcode==TCODE_WRITEB)||
	    (tcode==TCODE_LOCK_REQUEST))
		d = ohci->at_req_context;

	else if ((tcode==TCODE_WRITE_RESPONSE)||
		 (tcode==TCODE_READQ_RESPONSE)||
		 (tcode==TCODE_READB_RESPONSE)||
		 (tcode==TCODE_LOCK_RESPONSE)) 
		d = ohci->at_resp_context;

	else {
		PRINT(KERN_ERR, ohci->id, 
		      "Unexpected packet tcode=%d in AT DMA", tcode);
		return 0;
	}

	spin_lock(&d->lock);

	if (d->free_prgs<1) {
		PRINT(KERN_INFO, ohci->id, 
		      "AT DMA ctx=%d Running out of prgs... waiting",d->ctx);
	}
	while (d->free_prgs<1) {
		spin_unlock(&d->lock);
		schedule();
		if (i-- <0) {
			stop_context(ohci, d->ctrlClear, 
				     "AT DMA runaway loop... bailing out");
			return 0;
		}
		spin_lock(&d->lock);
	}

	insert_packet(ohci, d, packet);

	/* Is the context running ? (should be unless it is 
	   the first packet to be sent in this context) */
	if (!(reg_read(ohci, d->ctrlSet) & 0x8000)) {
		DBGMSG(ohci->id,"Starting AT DMA ctx=%d",d->ctx);
		if (packet->data_size) 
			reg_write(ohci, d->cmdPtr, 
				  virt_to_bus(&(d->prg[d->prg_ind])) | 0x3);
		else 
			reg_write(ohci, d->cmdPtr, 
				  virt_to_bus(&(d->prg[d->prg_ind])) | 0x2);

		run_context(ohci, d->ctrlSet, NULL);
	}
	else {
		DBGMSG(ohci->id,"Waking AT DMA ctx=%d",d->ctx);
		/* wake up the dma context if necessary */
		if (!(reg_read(ohci, d->ctrlSet) & 0x400))
			reg_write(ohci, d->ctrlSet, 0x1000);
	}

	d->prg_ind = (d->prg_ind+1)%d->num_desc;
	spin_unlock(&d->lock);

	return 1;
}

static int ohci_devctl(struct hpsb_host *host, enum devctl_cmd cmd, int arg)
{
	struct ti_ohci *ohci = host->hostdata;
	int retval = 0;
	unsigned long flags;

	switch (cmd) {
	case RESET_BUS:
		PRINT(KERN_INFO, ohci->id, "resetting bus on request%s",
		      (host->attempt_root ? " and attempting to become root"
		       : ""));
		reg_write(ohci, OHCI1394_PhyControl, 
			  (host->attempt_root) ? 0x000041ff : 0x0000417f);
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
		DBGMSG(ohci->id, "Cancel request received");
		dma_trm_reset(ohci->at_req_context);
		dma_trm_reset(ohci->at_resp_context);
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

static void dma_trm_reset(struct dma_trm_ctx *d)
{
	struct ti_ohci *ohci;

	if (d==NULL) {
		PRINT_G(KERN_ERR, "dma_trm_reset called with NULL arg");
		return;
	}
	ohci = (struct ti_ohci *)(d->ohci);
	stop_context(ohci, d->ctrlClear, NULL);

	spin_lock(&d->lock);

	/* is there still any packet pending ? */
	while(d->first) {
		PRINT(KERN_INFO, ohci->id, 
		      "AT dma reset ctx=%d, aborting transmission", 
		      d->ctx);
		hpsb_packet_sent(ohci->host, d->first, ACKX_ABORTED);
		d->first = d->first->xnext;
	}
	d->first = d->last = NULL;
	d->branchAddrPtr=NULL;
	d->sent_ind = d->prg_ind;
	d->free_prgs = d->num_desc;
	spin_unlock(&d->lock);
}

static void ohci_irq_handler(int irq, void *dev_id,
                             struct pt_regs *regs_are_unused)
{
	quadlet_t event,node_id;
	struct ti_ohci *ohci = (struct ti_ohci *)dev_id;
	struct hpsb_host *host = ohci->host;
	int phyid = -1, isroot = 0;

	event=reg_read(ohci, OHCI1394_IntEventSet);

	if (event & OHCI1394_busReset) {
		if (!host->in_bus_reset) {
			PRINT(KERN_INFO, ohci->id, "Bus reset");

			/* Wait for the AT fifo to be flushed */
			dma_trm_reset(ohci->at_req_context);
			dma_trm_reset(ohci->at_resp_context);

			/* Subsystem call */
			hpsb_bus_reset(ohci->host);

			ohci->NumBusResets++;
		}
	}
	/*
	 * Problem: How can I ensure that the AT bottom half will be
	 * executed before the AR bottom half (both events may have
	 * occured within a single irq event)
	 * Quick hack: just launch it within the IRQ handler
	 */
	if (event & OHCI1394_reqTxComplete) { 
		struct dma_trm_ctx *d = ohci->at_req_context;
		DBGMSG(ohci->id, "Got reqTxComplete interrupt status=0x%08X",
		       reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			stop_context(ohci, d->ctrlClear, "reqTxComplete");
		else
			dma_trm_bh((void *)d);
	}
	if (event & OHCI1394_respTxComplete) { 
		struct dma_trm_ctx *d = ohci->at_resp_context;
		DBGMSG(ohci->id, "Got respTxComplete interrupt status=0x%08X",
		       reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			stop_context(ohci, d->ctrlClear, "respTxComplete");
		else
			dma_trm_bh((void *)d);
	}
	if (event & OHCI1394_RQPkt) {
		struct dma_rcv_ctx *d = ohci->ar_req_context;
		DBGMSG(ohci->id, "Got RQPkt interrupt status=0x%08X",
		       reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			stop_context(ohci, d->ctrlClear, "RQPkt");
		else {
			queue_task(&d->task, &tq_immediate);
			mark_bh(IMMEDIATE_BH);
		}
	}
	if (event & OHCI1394_RSPkt) {
		struct dma_rcv_ctx *d = ohci->ar_resp_context;
		DBGMSG(ohci->id, "Got RSPkt interrupt status=0x%08X",
		       reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			stop_context(ohci, d->ctrlClear, "RSPkt");
		else {
			queue_task(&d->task, &tq_immediate);
			mark_bh(IMMEDIATE_BH);
		}
	}
	if (event & OHCI1394_isochRx) {
		quadlet_t isoRecvIntEvent;
		struct dma_rcv_ctx *d = ohci->ir_context;
                isoRecvIntEvent = reg_read(ohci, OHCI1394_IsoRecvIntEventSet);
		reg_write(ohci, OHCI1394_IsoRecvIntEventClear,
			  isoRecvIntEvent);
		DBGMSG(ohci->id, "Got reqTxComplete interrupt status=0x%08X",
		       reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			stop_context(ohci, d->ctrlClear, "isochRx");
		else {
			queue_task(&d->task, &tq_immediate);
			mark_bh(IMMEDIATE_BH);
		}
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

			/* Accept Physical requests from all nodes. */
			reg_write(ohci,OHCI1394_AsReqFilterHiSet, 0xffffffff);
			reg_write(ohci,OHCI1394_AsReqFilterLoSet, 0xffffffff);
		} 
		else PRINT(KERN_INFO, ohci->id, 
			   "phy reg received without reset\n");
	}
	if (event & OHCI1394_phyRegRcvd) {
#if 0
		if (host->in_bus_reset) {
			PRINT(KERN_INFO, ohci->id, "PhyControl: %08X", 
			      reg_read(ohci, OHCI1394_PhyControl));
		} else 
			PRINT(KERN_ERR, ohci->id, 
			      "phy reg received without reset");
#endif
	}

	/* clear the interrupt event register */
	reg_write(ohci, OHCI1394_IntEventClear, event);
}

/* Put the buffer back into the dma context */
static void insert_dma_buffer(struct dma_rcv_ctx *d, int idx)
{
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	DBGMSG(ohci->id, "Inserting dma buf ctx=%d idx=%d", d->ctx, idx);

	d->prg[idx]->status = d->buf_size;
	d->prg[idx]->branchAddress &= 0xfffffff0;
	idx = (idx + d->num_desc - 1 ) % d->num_desc;
	d->prg[idx]->branchAddress |= 0x1;

	/* wake up the dma context if necessary */
	if (!(reg_read(ohci, d->ctrlSet) & 0x400)) {
		PRINT(KERN_INFO, ohci->id, 
		      "Waking dma cxt=%d ... processing is probably too slow",
		      d->ctx);
		reg_write(ohci, d->ctrlSet, 0x1000);
	}
}	

static int block_length(struct dma_rcv_ctx *d, int idx, 
			 quadlet_t *buf_ptr, int offset)
{
	int length=0;

	/* Where is the data length ? */
	if (offset+12>=d->buf_size) 
		length = (d->buf[(idx+1)%d->num_desc]
			  [3-(d->buf_size-offset)/4]>>16);
	else 
		length = (buf_ptr[3]>>16);
	if (length % 4) length += 4 - (length % 4);
	return length;
}

static int packet_length(struct dma_rcv_ctx *d, int idx, 
			 quadlet_t *buf_ptr, int offset)
{
	unsigned char tcode;
	int length;

	/* Let's see what kind of packet is in there */
	tcode = (buf_ptr[0]>>4)&0xf;

	if (d->ctx==0) { /* Async Receive Request */
		if (tcode==TCODE_READQ) return 16;
		else if (tcode==TCODE_WRITEQ ||
			 tcode==TCODE_READB) return 20;
		else if (tcode==TCODE_WRITEB ||
			 tcode==TCODE_LOCK_REQUEST) {
			return block_length(d, idx, buf_ptr, offset) + 20;
		}
		else if (tcode==0xE) { /* Phy packet */
			return 16;
		}
		else return -1;
	}
	else if (d->ctx==1) { /* Async Receive Response */
		if (tcode==TCODE_WRITE_RESPONSE) return 16;
		else if (tcode==TCODE_READQ_RESPONSE) return 20;
		else if (tcode==TCODE_READB_RESPONSE ||
			 tcode==TCODE_LOCK_RESPONSE) {
			return block_length(d, idx, buf_ptr, offset) + 20;
		}
		else return -1;
	}
	else if (d->ctx==2) { /* Iso receive */
		/* Assumption: buffer fill mode with header/trailer */
		length = (buf_ptr[0]>>16);
		if (length % 4) length += 4 - (length % 4);
		return length+8;
	}
	return -1;
}

/* Bottom half that processes dma receive buffers */
static void dma_rcv_bh(void *data)
{
	struct dma_rcv_ctx *d = (struct dma_rcv_ctx*)data;
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	unsigned int split_left, idx, offset, rescount;
	unsigned char tcode;
	int length, bytes_left;
	quadlet_t *buf_ptr;
	char *split_ptr;
	char msg[256];

	spin_lock(&d->lock);

	idx = d->buf_ind;
	offset = d->buf_offset;
	buf_ptr = d->buf[idx] + offset/4;

	rescount = d->prg[idx]->status&0xffff;
	bytes_left = d->buf_size - rescount - offset;

	while (bytes_left>0) {
		tcode = (buf_ptr[0]>>4)&0xf;
		length = packet_length(d, idx, buf_ptr, offset);

		if (length<4) { /* something is wrong */
			sprintf(msg,"unexpected tcode 0x%X in AR ctx=%d",
				tcode, d->ctx);
			stop_context(ohci, d->ctrlClear, msg);
			spin_unlock(&d->lock);
			return;
		}

		if ((offset+length)>d->buf_size) { /* Split packet */
			if (length>d->split_buf_size) {
				stop_context(ohci, d->ctrlClear,
					     "split packet size exceeded");
				d->buf_ind = idx;
				d->buf_offset = offset;
				spin_unlock(&d->lock);
				return;
			}
			if (d->prg[(idx+1)%d->num_desc]->status==d->buf_size) {
				/* other part of packet not written yet */
				/* this should never happen I think */
				/* anyway we'll get it on the next call */
				PRINT(KERN_INFO, ohci->id,
				      "Got only half a packet !!!");
				d->buf_ind = idx;
				d->buf_offset = offset;
				spin_unlock(&d->lock);
				return;
			}
			split_left = length;
			split_ptr = (char *)d->spb;
			memcpy(split_ptr,buf_ptr,d->buf_size-offset);
			split_left -= d->buf_size-offset;
			split_ptr += d->buf_size-offset;
			insert_dma_buffer(d, idx);
			idx = (idx+1) % d->num_desc;
			buf_ptr = d->buf[idx];
			offset=0;
			while (split_left >= d->buf_size) {
				memcpy(split_ptr,buf_ptr,d->buf_size);
				split_ptr += d->buf_size;
				split_left -= d->buf_size;
				insert_dma_buffer(d, idx);
				idx = (idx+1) % d->num_desc;
				buf_ptr = d->buf[idx];
			}
			if (split_left>0) {
				memcpy(split_ptr, buf_ptr, split_left);
				offset = split_left;
				buf_ptr += offset/4;
			}

			/* 
			 * We get one phy packet for each bus reset. 
			 * we know that from now on the bus topology may
			 * have changed. Just ignore it for the moment
			 */
			if (tcode != 0xE) {
				DBGMSG(ohci->id, "Split packet received from"
				       " node %d ack=0x%02X spd=%d tcode=0x%X"
				       " length=%d data=0x%08x ctx=%d",
				       (d->spb[1]>>16)&0x3f,
				       (d->spb[length/4-1]>>16)&0x1f,
				       (d->spb[length/4-1]>>21)&0x3,
				       tcode, length, d->spb[3], d->ctx);
				hpsb_packet_received(ohci->host, d->spb, 
						     length);
			}
			else 
				PRINT(KERN_INFO, ohci->id, 
				      "Got phy packet ctx=%d ... discarded",
				      d->ctx);
		}
		else {
			/* 
			 * We get one phy packet for each bus reset. 
			 * we know that from now on the bus topology may
			 * have changed. Just ignore it for the moment
			 */
			if (tcode != 0xE) {
				DBGMSG(ohci->id, "Packet received from node"
				       " %d ack=0x%02X spd=%d tcode=0x%X"
				       " length=%d data=0x%08x ctx=%d",
				       (buf_ptr[1]>>16)&0x3f,
				       (buf_ptr[length/4-1]>>16)&0x1f,
				       (buf_ptr[length/4-1]>>21)&0x3,
				       tcode, length, buf_ptr[3], d->ctx);
				hpsb_packet_received(ohci->host, buf_ptr, 
						     length);
			}
			else 
				PRINT(KERN_INFO, ohci->id, 
				      "Got phy packet ctx=%d ... discarded",
				      d->ctx);
			offset += length;
			buf_ptr += length/4;
			if (offset==d->buf_size) {
				insert_dma_buffer(d, idx);
				idx = (idx+1) % d->num_desc;
				buf_ptr = d->buf[idx];
				offset=0;
			}
		}
		rescount = d->prg[idx]->status & 0xffff;
		bytes_left = d->buf_size - rescount - offset;

	}

	d->buf_ind = idx;
	d->buf_offset = offset;

	spin_unlock(&d->lock);
}

/* Bottom half that processes sent packets */
static void dma_trm_bh(void *data)
{
	struct dma_trm_ctx *d = (struct dma_trm_ctx*)data;
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	struct hpsb_packet *packet;
	u32 ack;

	spin_lock(&d->lock);

	if (d->first==NULL) {
		stop_context(ohci, d->ctrlClear, 
			     "Packet sent ack received but queue is empty");
		spin_unlock(&d->lock);
		return;
	}
	packet = d->first;
	d->first = d->first->xnext;
	if (d->first==NULL) d->last=NULL;
	if (packet->data_size) 
		ack = d->prg[d->sent_ind].end.status>>16;
	else 
		ack = d->prg[d->sent_ind].begin.status>>16;
	d->sent_ind = (d->sent_ind+1)%d->num_desc;
	d->free_prgs++;
	spin_unlock(&d->lock);
	
	DBGMSG(ohci->id, "Packet sent to node %d ack=0x%X spd=%d ctx=%d",
	       (packet->header[0]>>16)&0x3f, ack&0x1f, (ack>>5)&0x3, d->ctx);
	hpsb_packet_sent(ohci->host, packet, ack&0xf);
}

static int free_dma_rcv_ctx(struct dma_rcv_ctx *d)
{
	int i;

	if (d==NULL) return -1;

	if (d->buf) {
		for (i=0; i<d->num_desc; i++) 
			if (d->buf[i]) kfree(d->buf[i]);
		kfree(d->buf);
	}
	if (d->prg) {
		for (i=0; i<d->num_desc; i++) 
			if (d->prg[i]) kfree(d->prg[i]);
		kfree(d->prg);
	}
	if (d->spb) kfree(d->spb);
	
	kfree(d);
	
	return 0;
}

static struct dma_rcv_ctx *
alloc_dma_rcv_ctx(struct ti_ohci *ohci, int ctx, int num_desc,
		  int buf_size, int split_buf_size, 
		  int ctrlSet, int ctrlClear, int cmdPtr)
{
	struct dma_rcv_ctx *d=NULL;
	int i;

	d = (struct dma_rcv_ctx *)kmalloc(sizeof(struct dma_rcv_ctx), 
					  GFP_KERNEL);

	if (d==NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate dma_rcv_ctx");
		return NULL;
	}

	d->ohci = (void *)ohci;
	d->ctx = ctx;

	d->num_desc = num_desc;
	d->buf_size = buf_size;
	d->split_buf_size = split_buf_size;
	d->ctrlSet = ctrlSet;
	d->ctrlClear = ctrlClear;
	d->cmdPtr = cmdPtr;

	d->buf = NULL;
	d->prg = NULL;
	d->spb = NULL;

	d->buf = kmalloc(d->num_desc * sizeof(quadlet_t*), GFP_KERNEL);

	if (d->buf == NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate dma buffer");
		free_dma_rcv_ctx(d);
		return NULL;
	}
	memset(d->buf, 0, d->num_desc * sizeof(quadlet_t*));

	d->prg = kmalloc(d->num_desc * 	sizeof(struct dma_cmd*), GFP_KERNEL);

	if (d->prg == NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate dma prg");
		free_dma_rcv_ctx(d);
		return NULL;
	}
	memset(d->prg, 0, d->num_desc * sizeof(struct dma_cmd*));

	d->spb = kmalloc(d->split_buf_size, GFP_KERNEL);

	if (d->spb == NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate split buffer");
		free_dma_rcv_ctx(d);
		return NULL;
	}

	for (i=0; i<d->num_desc; i++) {
                d->buf[i] = kmalloc(d->buf_size, GFP_KERNEL);
		
                if (d->buf[i] != NULL) {
			memset(d->buf[i], 0, d->buf_size);
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "failed to allocate dma buffer");
			free_dma_rcv_ctx(d);
			return NULL;
		}

                d->prg[i]= kmalloc(sizeof(struct dma_cmd), GFP_KERNEL);

                if (d->prg[i] != NULL) {
                        memset(d->prg[i], 0, sizeof(struct dma_cmd));
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "failed to allocate dma prg");
			free_dma_rcv_ctx(d);
			return NULL;
		}
	}

        spin_lock_init(&d->lock);

        /* initialize bottom handler */
        d->task.routine = dma_rcv_bh;
        d->task.data = (void*)d;

	return d;
}

static int free_dma_trm_ctx(struct dma_trm_ctx *d)
{
	if (d==NULL) return -1;
	if (d->prg) kfree(d->prg);
	kfree(d);
	return 0;
}

static struct dma_trm_ctx *
alloc_dma_trm_ctx(struct ti_ohci *ohci, int ctx, int num_desc,
		  int ctrlSet, int ctrlClear, int cmdPtr)
{
	struct dma_trm_ctx *d=NULL;

	d = (struct dma_trm_ctx *)kmalloc(sizeof(struct dma_trm_ctx), 
					  GFP_KERNEL);

	if (d==NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate dma_trm_ctx");
		return NULL;
	}

	d->ohci = (void *)ohci;
	d->ctx = ctx;
	d->num_desc = num_desc;
	d->ctrlSet = ctrlSet;
	d->ctrlClear = ctrlClear;
	d->cmdPtr = cmdPtr;
	d->prg = NULL;

	d->prg = kmalloc(d->num_desc * sizeof(struct at_dma_prg), GFP_KERNEL);

	if (d->prg == NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate at dma prg");
		free_dma_trm_ctx(d);
		return NULL;
	}
	memset(d->prg, 0, d->num_desc * sizeof(struct at_dma_prg));

        spin_lock_init(&d->lock);

        /* initialize bottom handler */
        d->task.routine = dma_trm_bh;
        d->task.data = (void*)d;

	return d;
}

static int add_card(struct pci_dev *dev)
{
	struct ti_ohci *ohci;	/* shortcut to currently handled device */

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

	ohci->ar_req_context = 
		alloc_dma_rcv_ctx(ohci, 0, AR_REQ_NUM_DESC,
				  AR_REQ_BUF_SIZE, AR_REQ_SPLIT_BUF_SIZE,
				  OHCI1394_AsReqRcvContextControlSet,
				  OHCI1394_AsReqRcvContextControlClear,
				  OHCI1394_AsReqRcvCommandPtr);

	if (ohci->ar_req_context == NULL) return 1;

	ohci->ar_resp_context = 
		alloc_dma_rcv_ctx(ohci, 1, AR_RESP_NUM_DESC,
				  AR_RESP_BUF_SIZE, AR_RESP_SPLIT_BUF_SIZE,
				  OHCI1394_AsRspRcvContextControlSet,
				  OHCI1394_AsRspRcvContextControlClear,
				  OHCI1394_AsRspRcvCommandPtr);
	
	if (ohci->ar_resp_context == NULL) return 1;

	ohci->at_req_context = 
		alloc_dma_trm_ctx(ohci, 0, AT_REQ_NUM_DESC,
				  OHCI1394_AsReqTrContextControlSet,
				  OHCI1394_AsReqTrContextControlClear,
				  OHCI1394_AsReqTrCommandPtr);
	
	if (ohci->at_req_context == NULL) return 1;

	ohci->at_resp_context = 
		alloc_dma_trm_ctx(ohci, 1, AT_RESP_NUM_DESC,
				  OHCI1394_AsRspTrContextControlSet,
				  OHCI1394_AsRspTrContextControlClear,
				  OHCI1394_AsRspTrCommandPtr);
	
	if (ohci->at_resp_context == NULL) return 1;
				      
	ohci->ir_context =
		alloc_dma_rcv_ctx(ohci, 2, IR_NUM_DESC,
				  IR_BUF_SIZE, IR_SPLIT_BUF_SIZE,
				  OHCI1394_IrRcvContextControlSet,
				  OHCI1394_IrRcvContextControlClear,
				  OHCI1394_IrRcvCommandPtr);

	if (ohci->ir_context == NULL) return 1;

        ohci->IR_channel_usage= 0x0000000000000000;
        spin_lock_init(&ohci->IR_channel_lock);

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
	struct dma_rcv_ctx *d=NULL;
	struct dma_trm_ctx *dt=NULL;

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
	d = ohci->ir_context;
#if 0
	for (i=0; i<d->num_desc; i++) {
		p += sprintf(p, "IR buf[%d] : %p prg[%d]: %p\n",
			     i, d->buf[i], i, d->prg[i]);
	}
#endif
	p += sprintf(p, "Current buf: %d offset: %d\n",
		     d->buf_ind,d->buf_offset);

	p += sprintf(p,"\n---Async Receive DMA---\n");
	d = ohci->ar_req_context;
#if 0
	for (i=0; i<d->num_desc; i++) {
		p += sprintf(p, "AR req buf[%d] : %p prg[%d]: %p\n",
			     i, d->buf[i], i, d->prg[i]);
	}
#endif
	p += sprintf(p, "Ar req current buf: %d offset: %d\n",
		     d->buf_ind,d->buf_offset);

	d = ohci->ar_resp_context;
#if 0
	for (i=0; i<d->num_desc; i++) {
		p += sprintf(p, "AR resp buf[%d] : %p prg[%d]: %p\n",
			     i, d->buf[i], i, d->prg[i]);
	}
#endif
	p += sprintf(p, "AR resp current buf: %d offset: %d\n",
		     d->buf_ind,d->buf_offset);

	p += sprintf(p,"\n---Async Transmit DMA---\n");
	dt = ohci->at_req_context;
	p += sprintf(p, "AT req prg: %d sent: %d free: %d branchAddrPtr: %p\n",
		     dt->prg_ind, dt->sent_ind, dt->free_prgs, 
		     dt->branchAddrPtr);
	p += sprintf(p, "AT req queue: first: %p last: %p\n",
		     dt->first, dt->last);
	dt = ohci->at_resp_context;
#if 0
	for (i=0; i<dt->num_desc; i++) {
		p += sprintf(p, "------- AT resp prg[%02d] ------\n",i);
		p += sprintf(p, "%p: control  : %08x\n",
			     &(dt->prg[i].begin.control),
			     dt->prg[i].begin.control);
		p += sprintf(p, "%p: address  : %08x\n",
			     &(dt->prg[i].begin.address),
			     dt->prg[i].begin.address);
		p += sprintf(p, "%p: brancAddr: %08x\n",
			     &(dt->prg[i].begin.branchAddress),
			     dt->prg[i].begin.branchAddress);
		p += sprintf(p, "%p: status   : %08x\n",
			     &(dt->prg[i].begin.status),
			     dt->prg[i].begin.status);
		p += sprintf(p, "%p: header[0]: %08x\n",
			     &(dt->prg[i].data[0]),
			     dt->prg[i].data[0]);
		p += sprintf(p, "%p: header[1]: %08x\n",
			     &(dt->prg[i].data[1]),
			     dt->prg[i].data[1]);
		p += sprintf(p, "%p: header[2]: %08x\n",
			     &(dt->prg[i].data[2]),
			     dt->prg[i].data[2]);
		p += sprintf(p, "%p: header[3]: %08x\n",
			     &(dt->prg[i].data[3]),
			     dt->prg[i].data[3]);
		p += sprintf(p, "%p: control  : %08x\n",
			     &(dt->prg[i].end.control),
			     dt->prg[i].end.control);
		p += sprintf(p, "%p: address  : %08x\n",
			     &(dt->prg[i].end.address),
			     dt->prg[i].end.address);
		p += sprintf(p, "%p: brancAddr: %08x\n",
			     &(dt->prg[i].end.branchAddress),
			     dt->prg[i].end.branchAddress);
		p += sprintf(p, "%p: status   : %08x\n",
			     &(dt->prg[i].end.status),
			     dt->prg[i].end.status);
	}
#endif
	p += sprintf(p, "AR resp prg: %d sent: %d free: %d"
		     " branchAddrPtr: %p\n",
		     dt->prg_ind, dt->sent_ind, dt->free_prgs, 
		     dt->branchAddrPtr);
	p += sprintf(p, "AT resp queue: first: %p last: %p\n",
		     dt->first, dt->last);

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
	for (i=0;i<4;i++) {
		p += sprintf(p,"IsoRCtxCtl%02d: %08x  IsoRCmdPtr%02d: %08x"
			     "  IsoRCxtMch%02d: %08x\n", i,
			     reg_read(ohci, 
				      OHCI1394_IrRcvContextControlSet+32*i),
			     i,reg_read(ohci, OHCI1394_IrRcvCommandPtr+32*i),
			     i,reg_read(ohci, 
					OHCI1394_IrRcvContextMatch+32*i));
	}

#if 0
	p += sprintf(p,"\n### Phy Register dump ###\n");
	phyreg=get_phy_reg(ohci,1);
	p += sprintf(p,"offset: %d val: 0x%02x -> RHB: %d"
		     "IBR: %d Gap_count: %d\n",
		     1,phyreg,(phyreg&0x80) != 0, 
		     (phyreg&0x40) !=0, phyreg&0x3f);
	phyreg=get_phy_reg(ohci,2);
	nports=phyreg&0x1f;
	p += sprintf(p,"offset: %d val: 0x%02x -> SPD: %d"
		     " E  : %d Ports    : %2d\n",
		     2,phyreg, (phyreg&0xC0)>>6, (phyreg&0x20) !=0, nports);
	for (i=0;i<nports;i++) {
		phyreg=get_phy_reg(ohci,3+i);
		p += sprintf(p,"offset: %d val: 0x%02x -> [port %d]"
			     " TPA: %d TPB: %d | %s %s\n",
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

	/* Free AR dma */
	free_dma_rcv_ctx(ohci->ar_req_context);
	free_dma_rcv_ctx(ohci->ar_resp_context);

	/* Free AT dma */
	free_dma_trm_ctx(ohci->at_req_context);
	free_dma_trm_ctx(ohci->at_resp_context);

	/* Free IR dma */
	free_dma_rcv_ctx(ohci->ir_context);

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

	DBGMSG(ohci->id, "request csr_rom address: %08X",
	       (u32)ohci->csr_config_rom);

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

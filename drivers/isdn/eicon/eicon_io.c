/* $Id: eicon_io.c,v 1.8 1999/10/08 22:09:34 armin Exp $
 *
 * ISDN low-level module for Eicon.Diehl active ISDN-Cards.
 * Code for communicating with hardware.
 *
 * Copyright 1999    by Armin Schindler (mac@melware.de)
 * Copyright 1999    Cytronics & Melware (info@melware.de)
 *
 * Thanks to	Eicon Technology Diehl GmbH & Co. oHG for 
 *		documents, informations and hardware. 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: eicon_io.c,v $
 * Revision 1.8  1999/10/08 22:09:34  armin
 * Some fixes of cards interface handling.
 * Bugfix of NULL pointer occurence.
 * Changed a few log outputs.
 *
 * Revision 1.7  1999/09/26 14:17:53  armin
 * Improved debug and log via readstat()
 *
 * Revision 1.6  1999/09/21 20:35:43  armin
 * added more error checking.
 *
 * Revision 1.5  1999/08/31 11:20:11  paul
 * various spelling corrections (new checksums may be needed, Karsten!)
 *
 * Revision 1.4  1999/08/22 20:26:47  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.3  1999/08/18 20:17:01  armin
 * Added XLOG function for all cards.
 * Bugfix of alloc_skb NULL pointer.
 *
 * Revision 1.2  1999/07/25 15:12:05  armin
 * fix of some debug logs.
 * enabled ISA-cards option.
 *
 * Revision 1.1  1999/03/29 11:19:45  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 *
 */


#include <linux/config.h>
#include "eicon.h"

void
eicon_io_rcv_dispatch(eicon_card *ccard) {
	ulong flags;
        struct sk_buff *skb, *skb2, *skb_new;
        eicon_IND *ind, *ind2, *ind_new;
        eicon_chan *chan;

        if (!ccard) {
	        eicon_log(ccard, 1, "eicon_err: NULL card in rcv_dispatch !\n");
                return;
        }

	while((skb = skb_dequeue(&ccard->rcvq))) {
        	ind = (eicon_IND *)skb->data;

		save_flags(flags);
		cli();
        	if ((chan = ccard->IdTable[ind->IndId]) == NULL) {
			if (DebugVar & 1) {
				switch(ind->Ind) {
					case IDI_N_DISC_ACK: 
						/* doesn't matter if this happens */ 
						break;
					default: 
						eicon_log(ccard, 1, "idi: Indication for unknown channel Ind=%d Id=%x\n", ind->Ind, ind->IndId);
						eicon_log(ccard, 1, "idi_hdl: Ch??: Ind=%d Id=%x Ch=%d MInd=%d MLen=%d Len=%d\n",
							ind->Ind,ind->IndId,ind->IndCh,ind->MInd,ind->MLength,ind->RBuffer.length);
				}
			}
			restore_flags(flags);
	                dev_kfree_skb(skb);
	                continue;
	        }
		restore_flags(flags);

		if (chan->e.complete) { /* check for rec-buffer chaining */
			if (ind->MLength == ind->RBuffer.length) {
				chan->e.complete = 1;
				idi_handle_ind(ccard, skb);
				continue;
			}
			else {
				chan->e.complete = 0;
				ind->Ind = ind->MInd;
				skb_queue_tail(&chan->e.R, skb);
				continue;
			}
		}
		else {
			save_flags(flags);
			cli();
			if (!(skb2 = skb_dequeue(&chan->e.R))) {
				chan->e.complete = 1;
                		eicon_log(ccard, 1, "eicon: buffer incomplete, but 0 in queue\n");
				restore_flags(flags);
	                	dev_kfree_skb(skb);
				continue;	
			}
	        	ind2 = (eicon_IND *)skb2->data;
			skb_new = alloc_skb(((sizeof(eicon_IND)-1)+ind->RBuffer.length+ind2->RBuffer.length),
					GFP_ATOMIC);
			if (!skb_new) {
                		eicon_log(ccard, 1, "eicon_io: skb_alloc failed in rcv_dispatch()\n");
				restore_flags(flags);
	                	dev_kfree_skb(skb);
	                	dev_kfree_skb(skb2);
				continue;	
			}
			ind_new = (eicon_IND *)skb_put(skb_new,
					((sizeof(eicon_IND)-1)+ind->RBuffer.length+ind2->RBuffer.length));
			ind_new->Ind = ind2->Ind;
			ind_new->IndId = ind2->IndId;
			ind_new->IndCh = ind2->IndCh;
			ind_new->MInd = ind2->MInd;
			ind_new->MLength = ind2->MLength;
			ind_new->RBuffer.length = ind2->RBuffer.length + ind->RBuffer.length;
			memcpy(&ind_new->RBuffer.P, &ind2->RBuffer.P, ind2->RBuffer.length);
			memcpy((&ind_new->RBuffer.P)+ind2->RBuffer.length, &ind->RBuffer.P, ind->RBuffer.length);
                	dev_kfree_skb(skb);
                	dev_kfree_skb(skb2);
			if (ind->MLength == ind->RBuffer.length) {
				chan->e.complete = 2;
				restore_flags(flags);
				idi_handle_ind(ccard, skb_new);
				continue;
			}
			else {
				chan->e.complete = 0;
				skb_queue_tail(&chan->e.R, skb_new);
				restore_flags(flags);
				continue;
			}
		}
	}
}

void
eicon_io_ack_dispatch(eicon_card *ccard) {
        struct sk_buff *skb;

        if (!ccard) {
		eicon_log(ccard, 1, "eicon_err: NULL card in ack_dispatch!\n");
                return;
        }
	while((skb = skb_dequeue(&ccard->rackq))) {
		idi_handle_ack(ccard, skb);
	}
}


/*
 *  IO-Functions for different card-types
 */

u8 ram_inb(eicon_card *card, void *adr) {
        eicon_pci_card *pcard;
        eicon_isa_card *icard;
        u32 addr = (u32) adr;
	
	pcard = &card->hwif.pci;
	icard = &card->hwif.isa;

        switch(card->type) {
                case EICON_CTYPE_MAESTRA:
                        outw((u16)addr, (u16)pcard->PCIreg + M_ADDR);
                        return(inb((u16)pcard->PCIreg + M_DATA));
                case EICON_CTYPE_MAESTRAP:
                case EICON_CTYPE_S2M:
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
                        return(readb(addr));
        }
 return(0);
}

u16 ram_inw(eicon_card *card, void *adr) {
        eicon_pci_card *pcard;
        eicon_isa_card *icard;
        u32 addr = (u32) adr;
	
	pcard = &card->hwif.pci;
	icard = &card->hwif.isa;

        switch(card->type) {
                case EICON_CTYPE_MAESTRA:
                        outw((u16)addr, (u16)pcard->PCIreg + M_ADDR);
                        return(inw((u16)pcard->PCIreg + M_DATA));
                case EICON_CTYPE_MAESTRAP:
                case EICON_CTYPE_S2M:
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
                        return(readw(addr));
        }
 return(0);
}

void ram_outb(eicon_card *card, void *adr, u8 data) {
        eicon_pci_card *pcard;
        eicon_isa_card *icard;
        u32 addr = (u32) adr;

	pcard = &card->hwif.pci;
	icard = &card->hwif.isa;

        switch(card->type) {
                case EICON_CTYPE_MAESTRA:
                        outw((u16)addr, (u16)pcard->PCIreg + M_ADDR);
                        outb((u8)data, (u16)pcard->PCIreg + M_DATA);
                        break;
                case EICON_CTYPE_MAESTRAP:
                case EICON_CTYPE_S2M:
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
                        writeb(data, addr);
                        break;
        }
}

void ram_outw(eicon_card *card, void *adr , u16 data) {
        eicon_pci_card *pcard;
        eicon_isa_card *icard;
        u32 addr = (u32) adr;

	pcard = &card->hwif.pci;
	icard = &card->hwif.isa;

        switch(card->type) {
                case EICON_CTYPE_MAESTRA:
                        outw((u16)addr, (u16)pcard->PCIreg + M_ADDR);
                        outw((u16)data, (u16)pcard->PCIreg + M_DATA);
                        break;
                case EICON_CTYPE_MAESTRAP:
                case EICON_CTYPE_S2M:
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
                        writew(data, addr);
                        break;
        }
}

void ram_copyfromcard(eicon_card *card, void *adrto, void *adr, int len) {
        int i;
        switch(card->type) {
                case EICON_CTYPE_MAESTRA:
                        for(i = 0; i < len; i++) {
                                writeb(ram_inb(card, adr + i), adrto + i);
                        }
                        break;
                case EICON_CTYPE_MAESTRAP:
                        memcpy(adrto, adr, len);
                        break;
                case EICON_CTYPE_S2M:
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
                        memcpy_fromio(adrto, adr, len);
                        break;
        }
}

void ram_copytocard(eicon_card *card, void *adrto, void *adr, int len) {
        int i;
        switch(card->type) {
                case EICON_CTYPE_MAESTRA:
                        for(i = 0; i < len; i++) {
                                ram_outb(card, adrto + i, readb(adr + i));
                        }
                        break;
                case EICON_CTYPE_MAESTRAP:
                        memcpy(adrto, adr, len);
                        break;
                case EICON_CTYPE_S2M:
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
                        memcpy_toio(adrto, adr, len);
                        break;
        }
}

/*
 * XLOG
 */
int
eicon_get_xlog(eicon_card *card, xlogreq_t *xlogreq)
{
	int timeout, i;
	int divas_shared_offset = 0;
	int len = 0;
	int stype = 0;
	__u32 time = 0;
	mi_pc_maint_t	*pcm = &xlogreq->pcm;
        eicon_pci_card *pci_card = &card->hwif.pci;
        eicon_isa_card *isa_card = &card->hwif.isa;
	eicon_pr_ram  *prram = 0;
        char *ram;

	switch(card->type) {
		case EICON_CTYPE_MAESTRAP:
			ram = (char *)pci_card->PCIram;
			prram = (eicon_pr_ram *)ram;
			divas_shared_offset = DIVAS_SHARED_OFFSET;
			len = sizeof(mi_pc_maint_t);
			break;
		case EICON_CTYPE_MAESTRA:
			prram = 0;
			divas_shared_offset = 0;
			len = sizeof(mi_pc_maint_t);
			break;
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
		case EICON_CTYPE_S2M:
			prram = (eicon_pr_ram *)isa_card->shmem;
			divas_shared_offset = 0xfb80;
			len = sizeof(mi_pc_maint_t) - 78;
			stype = 1;
			break;
		default:
			return -ENODEV;
	}

        memset(&(xlogreq->pcm), 0, sizeof(mi_pc_maint_t));

        xlogreq->pcm.rc = 0;
        xlogreq->pcm.req = 1; /* DO_LOG */

        ram = ((char *)prram) + MIPS_MAINT_OFFS - divas_shared_offset;

	ram_outb(card, ram+1, pcm->rc); 
	ram_outb(card, ram+0, pcm->req); 

        timeout = jiffies + 50;
        while (timeout > jiffies) {
		pcm->rc = ram_inb(card, ram+1);
		pcm->req = ram_inb(card, ram+0);
                if (!pcm->req) break;
                SLEEP(10);
        }

	if (pcm->req) {
		return XLOG_ERR_TIMEOUT;
	}

	if (pcm->rc != OK) {
		return XLOG_ERR_DONE;
	}
	
	ram_copyfromcard(card, pcm, ram, len);

	if (stype) {
		for (i=0; i<8; i++)
			((__u8 *)pcm)[11-i] = ((__u8 *)pcm)[9-i];
		time =	(__u32)pcm->data.w[2] * 3600 	* 1000 +
			(__u32)pcm->data.w[1] 		* 1000 +
			(__u32)pcm->data.b[1] 		* 20 +
			(__u32)pcm->data.b[0]		;
		pcm->data.w[1] = (__u16) (time >> 16);
		pcm->data.w[2] = (__u16) (time & 0x0000ffff);
		pcm->data.w[0] = 2;
	}

	return XLOG_OK;
}

/*
 *  Transmit-Function
 */
void
eicon_io_transmit(eicon_card *ccard) {
        eicon_pci_card *pci_card;
        eicon_isa_card *isa_card;
        struct sk_buff *skb;
        struct sk_buff *skb2;
        unsigned long flags;
        char *ram, *reg, *cfg;
	eicon_pr_ram  *prram = 0;
	eicon_isa_com	*com = 0;
	eicon_REQ *ReqOut = 0;
	eicon_REQ *reqbuf = 0;
	eicon_chan *chan;
	eicon_chan_ptr *chan2;
	int ReqCount;
	int scom = 0;
	int tmp = 0;
	int quloop = 1;
	int dlev = 0;

	pci_card = &ccard->hwif.pci;
	isa_card = &ccard->hwif.isa;

        if (!ccard) {
               	eicon_log(ccard, 1, "eicon_transmit: NULL card!\n");
                return;
        }

	switch(ccard->type) {
#ifdef CONFIG_ISDN_DRV_EICON_ISA
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
			scom = 1;
			com = (eicon_isa_com *)isa_card->shmem;
			break;
		case EICON_CTYPE_S2M:
			scom = 0;
			prram = (eicon_pr_ram *)isa_card->shmem;
			break;
#endif
		case EICON_CTYPE_MAESTRAP:
			scom = 0;
        		ram = (char *)pci_card->PCIram;
		        reg = (char *)pci_card->PCIreg;
        		cfg = (char *)pci_card->PCIcfg;
			prram = (eicon_pr_ram *)ram;
			break;
		case EICON_CTYPE_MAESTRA:
			scom = 0;
        		ram = (char *)pci_card->PCIram;
		        reg = (char *)pci_card->PCIreg;
        		cfg = (char *)pci_card->PCIcfg;
			prram = 0;
			break;
		default:
                	eicon_log(ccard, 1, "eicon_transmit: unsupported card-type!\n");
			return;
	}

	ReqCount = 0;
	if (!(skb2 = skb_dequeue(&ccard->sndq)))
		quloop = 0; 
	while(quloop) { 
                save_flags(flags);
                cli();
		if (scom) {
			if (ram_inb(ccard, &com->Req)) {
				if (!ccard->ReadyInt) {
					tmp = ram_inb(ccard, &com->ReadyInt) + 1;
					ram_outb(ccard, &com->ReadyInt, tmp);
					ccard->ReadyInt++;
				}
        	                restore_flags(flags);
                	        skb_queue_head(&ccard->sndq, skb2);
       	                	eicon_log(ccard, 32, "eicon: transmit: Card not ready\n");
	                        return;
			}
		} else {
	                if (!(ram_inb(ccard, &prram->ReqOutput) - ram_inb(ccard, &prram->ReqInput))) {
        	                restore_flags(flags);
                	        skb_queue_head(&ccard->sndq, skb2);
       	                	eicon_log(ccard, 32, "eicon: transmit: Card not ready\n");
	                        return;
        	        }
		}
		restore_flags(flags);
		chan2 = (eicon_chan_ptr *)skb2->data;
		chan = chan2->ptr;
		if (!chan->e.busy) {
		 if((skb = skb_dequeue(&chan->e.X))) { 
		  save_flags(flags);
		  cli();
		  reqbuf = (eicon_REQ *)skb->data;
		  if ((reqbuf->Reference) && (chan->e.B2Id == 0) && (reqbuf->ReqId & 0x1f)) {
			eicon_log(ccard, 16, "eicon: transmit: error Id=0 on %d (Net)\n", chan->No); 
		  } else {
			if (scom) {
				ram_outw(ccard, &com->XBuffer.length, reqbuf->XBuffer.length);
				ram_copytocard(ccard, &com->XBuffer.P, &reqbuf->XBuffer.P, reqbuf->XBuffer.length);
				ram_outb(ccard, &com->ReqCh, reqbuf->ReqCh);
				
			} else {
				/* get address of next available request buffer */
				ReqOut = (eicon_REQ *)&prram->B[ram_inw(ccard, &prram->NextReq)];
				ram_outw(ccard, &ReqOut->XBuffer.length, reqbuf->XBuffer.length);
				ram_copytocard(ccard, &ReqOut->XBuffer.P, &reqbuf->XBuffer.P, reqbuf->XBuffer.length);
				ram_outb(ccard, &ReqOut->ReqCh, reqbuf->ReqCh);
				ram_outb(ccard, &ReqOut->Req, reqbuf->Req); 
			}
			dlev = 160;
			if (reqbuf->ReqId & 0x1f) { /* if this is no ASSIGN */

				if (!reqbuf->Reference) { /* Signal Layer */
					if (scom)
						ram_outb(ccard, &com->ReqId, chan->e.D3Id); 
					else
						ram_outb(ccard, &ReqOut->ReqId, chan->e.D3Id); 

					chan->e.ReqCh = 0; 
				}
				else {			/* Net Layer */
					if (scom)
						ram_outb(ccard, &com->ReqId, chan->e.B2Id); 
					else
						ram_outb(ccard, &ReqOut->ReqId, chan->e.B2Id); 

					chan->e.ReqCh = 1;
					if (((reqbuf->Req & 0x0f) == 0x08) ||
					   ((reqbuf->Req & 0x0f) == 0x01)) { /* Send Data */
						chan->waitq = reqbuf->XBuffer.length;
						chan->waitpq += reqbuf->XBuffer.length;
						dlev = 128;
					}
				}

			} else {	/* It is an ASSIGN */

				if (scom)
					ram_outb(ccard, &com->ReqId, reqbuf->ReqId); 
				else
					ram_outb(ccard, &ReqOut->ReqId, reqbuf->ReqId); 

				if (!reqbuf->Reference) 
					chan->e.ReqCh = 0; 
				 else
					chan->e.ReqCh = 1; 
			} 
			if (scom)
			 	chan->e.ref = ccard->ref_out++;
			else
			 	chan->e.ref = ram_inw(ccard, &ReqOut->Reference);

			chan->e.Req = reqbuf->Req;
			ReqCount++; 
			if (scom)
				ram_outb(ccard, &com->Req, reqbuf->Req); 
			else
				ram_outw(ccard, &prram->NextReq, ram_inw(ccard, &ReqOut->next)); 

			chan->e.busy = 1;
	               	eicon_log(ccard, dlev, "eicon: Req=%d Id=%x Ch=%d Len=%d Ref=%d\n", 
					reqbuf->Req, 
					ram_inb(ccard, &ReqOut->ReqId),
					reqbuf->ReqCh, reqbuf->XBuffer.length,
					chan->e.ref); 
		  }
		  restore_flags(flags);
		  dev_kfree_skb(skb);
		 }
		 dev_kfree_skb(skb2);
		} 
		else {
		skb_queue_tail(&ccard->sackq, skb2);
               	eicon_log(ccard, 128, "eicon: transmit: busy chan %d\n", chan->No); 
		}

		if (scom)
			quloop = 0;
		else
			if (!(skb2 = skb_dequeue(&ccard->sndq)))
				quloop = 0;

	}
	if (!scom)
		ram_outb(ccard, &prram->ReqInput, (__u8)(ram_inb(ccard, &prram->ReqInput) + ReqCount)); 

	while((skb = skb_dequeue(&ccard->sackq))) { 
		skb_queue_tail(&ccard->sndq, skb);
	}
}


/*
 * IRQ handler 
 */
void
eicon_irq(int irq, void *dev_id, struct pt_regs *regs) {
	eicon_card *ccard = (eicon_card *)dev_id;
        eicon_pci_card *pci_card;
        eicon_isa_card *isa_card;
    	char *ram = 0;
	char *reg = 0;
	char *cfg = 0;	
	eicon_pr_ram  *prram = 0;
	eicon_isa_com	*com = 0;
        eicon_RC *RcIn;
        eicon_IND *IndIn;
	struct sk_buff *skb;
        int Count = 0;
	int Rc = 0;
	int Ind = 0;
	unsigned char *irqprobe = 0;
	int scom = 0;
	int tmp = 0;
	int dlev = 0;


        if (!ccard) {
                eicon_log(ccard, 1, "eicon_irq: spurious interrupt %d\n", irq);
                return;
        }

	if (ccard->type == EICON_CTYPE_QUADRO) {
		tmp = 4;
		while(tmp) {
			com = (eicon_isa_com *)ccard->hwif.isa.shmem;
			if ((readb(ccard->hwif.isa.intack))) { /* quadro found */
				break;
			}
			ccard = ccard->qnext;
			tmp--;
		}
	}

	pci_card = &ccard->hwif.pci;
	isa_card = &ccard->hwif.isa;

	switch(ccard->type) {
#ifdef CONFIG_ISDN_DRV_EICON_ISA
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
			scom = 1;
			com = (eicon_isa_com *)isa_card->shmem;
			irqprobe = &isa_card->irqprobe;
			break;
		case EICON_CTYPE_S2M:
			scom = 0;
			prram = (eicon_pr_ram *)isa_card->shmem;
			irqprobe = &isa_card->irqprobe;
			break;
#endif
		case EICON_CTYPE_MAESTRAP:
			scom = 0;
			ram = (char *)pci_card->PCIram;
			reg = (char *)pci_card->PCIreg;
			cfg = (char *)pci_card->PCIcfg;
			irqprobe = &pci_card->irqprobe;
			prram = (eicon_pr_ram *)ram;
			break;
		case EICON_CTYPE_MAESTRA:
			scom = 0;
			ram = (char *)pci_card->PCIram;
			reg = (char *)pci_card->PCIreg;
			cfg = (char *)pci_card->PCIcfg;
			irqprobe = &pci_card->irqprobe;
			prram = 0;
			break;
		default:
                	eicon_log(ccard, 1, "eicon_irq: unsupported card-type!\n");
			return;
	}

	if (*irqprobe) {
		switch(ccard->type) {
#ifdef CONFIG_ISDN_DRV_EICON_ISA
			case EICON_CTYPE_S:
			case EICON_CTYPE_SX:
			case EICON_CTYPE_SCOM:
			case EICON_CTYPE_QUADRO:
				if (readb(isa_card->intack)) {
        		               	writeb(0, &com->Rc);
					writeb(0, isa_card->intack);
				}
				(*irqprobe)++;
				break;
			case EICON_CTYPE_S2M:
				if (readb(isa_card->intack)) {
        		               	writeb(0, &prram->RcOutput);
					writeb(0, isa_card->intack);
				}
				(*irqprobe)++;
				break;
#endif
			case EICON_CTYPE_MAESTRAP:
	        		if (readb(&ram[0x3fe])) { 
        		               	writeb(0, &prram->RcOutput);
				        writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
				        writew(0, &cfg[MP_IRQ_RESET + 2]);
					writeb(0, &ram[0x3fe]);
       			        } 
				*irqprobe = 0;
				break;
			case EICON_CTYPE_MAESTRA:
				outb(0x08, pci_card->PCIreg + M_RESET);
				*irqprobe = 0;
				break;
		}
		return;
	}

	switch(ccard->type) {
#ifdef CONFIG_ISDN_DRV_EICON_ISA
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_QUADRO:
		case EICON_CTYPE_S2M:
			if (!(readb(isa_card->intack))) { /* card did not interrupt */
				eicon_log(ccard, 1, "eicon: IRQ: card reports no interrupt!\n");
				return;
			} 
			break;
#endif
		case EICON_CTYPE_MAESTRAP:
			if (!(readb(&ram[0x3fe]))) { /* card did not interrupt */
				eicon_log(ccard, 1, "eicon: IRQ: card reports no interrupt!\n");
				return;
			} 
			break;
		case EICON_CTYPE_MAESTRA:
			outw(0x3fe, pci_card->PCIreg + M_ADDR);
			if (!(inb(pci_card->PCIreg + M_DATA))) { /* card did not interrupt */
				eicon_log(ccard, 1, "eicon: IRQ: card reports no interrupt!\n");
				return;
			} 
			break;
	}

    if (scom) {

        /* if a return code is available ...  */
	if ((tmp = ram_inb(ccard, &com->Rc))) {
		eicon_RC *ack;
		if (tmp == READY_INT) {
                       	eicon_log(ccard, 64, "eicon: IRQ Rc=READY_INT\n");
			if (ccard->ReadyInt) {
				ccard->ReadyInt--;
				ram_outb(ccard, &com->Rc, 0);
			}
		} else {
			skb = alloc_skb(sizeof(eicon_RC), GFP_ATOMIC);
			if (!skb) {
                		eicon_log(ccard, 1, "eicon_io: skb_alloc failed in _irq()\n");
			} else {
				ack = (eicon_RC *)skb_put(skb, sizeof(eicon_RC));
				ack->Rc = tmp;
				ack->RcId = ram_inb(ccard, &com->RcId);
				ack->RcCh = ram_inb(ccard, &com->RcCh);
				ack->Reference = ccard->ref_in++;
               	        	eicon_log(ccard, 128, "eicon: IRQ Rc=%d Id=%x Ch=%d Ref=%d\n",
					tmp,ack->RcId,ack->RcCh,ack->Reference);
				skb_queue_tail(&ccard->rackq, skb);
				eicon_schedule_ack(ccard);
			}
			ram_outb(ccard, &com->Req, 0);
			ram_outb(ccard, &com->Rc, 0);
		}

	} else {

	        /* if an indication is available ...  */
		if ((tmp = ram_inb(ccard, &com->Ind))) {
			eicon_IND *ind;
			int len = ram_inw(ccard, &com->RBuffer.length);
			skb = alloc_skb((sizeof(eicon_IND) + len - 1), GFP_ATOMIC);
			if (!skb) {
                		eicon_log(ccard, 1, "eicon_io: skb_alloc failed in _irq()\n");
			} else {
				ind = (eicon_IND *)skb_put(skb, (sizeof(eicon_IND) + len - 1));
				ind->Ind = tmp;
				ind->IndId = ram_inb(ccard, &com->IndId);
				ind->IndCh = ram_inb(ccard, &com->IndCh);
				ind->MInd  = ram_inb(ccard, &com->MInd);
				ind->MLength = ram_inw(ccard, &com->MLength);
				ind->RBuffer.length = len;
				if ((tmp == 1) || (tmp == 8))
					dlev = 128;
				else
					dlev = 192;
                       		eicon_log(ccard, dlev, "eicon: IRQ Ind=%d Id=%x Ch=%d MInd=%d MLen=%d Len=%d\n",
					tmp,ind->IndId,ind->IndCh,ind->MInd,ind->MLength,len);
				ram_copyfromcard(ccard, &ind->RBuffer.P, &com->RBuffer.P, len);
				skb_queue_tail(&ccard->rcvq, skb);
				eicon_schedule_rx(ccard);
			}
			ram_outb(ccard, &com->Ind, 0);
		}
	}

    } else {

        /* if return codes are available ...  */
        if((Count = ram_inb(ccard, &prram->RcOutput))) {
		eicon_RC *ack;
                /* get the buffer address of the first return code */
                RcIn = (eicon_RC *)&prram->B[ram_inw(ccard, &prram->NextRc)];
                /* for all return codes do ...  */
                while(Count--) {

                        if((Rc=ram_inb(ccard, &RcIn->Rc))) {
				skb = alloc_skb(sizeof(eicon_RC), GFP_ATOMIC);
				if (!skb) {
                			eicon_log(ccard, 1, "eicon_io: skb_alloc failed in _irq()\n");
				} else {
					ack = (eicon_RC *)skb_put(skb, sizeof(eicon_RC));
					ack->Rc = Rc;
					ack->RcId = ram_inb(ccard, &RcIn->RcId);
					ack->RcCh = ram_inb(ccard, &RcIn->RcCh);
					ack->Reference = ram_inw(ccard, &RcIn->Reference);
        	                	eicon_log(ccard, 128, "eicon: IRQ Rc=%d Id=%x Ch=%d Ref=%d\n",
						Rc,ack->RcId,ack->RcCh,ack->Reference);
					skb_queue_tail(&ccard->rackq, skb);
					eicon_schedule_ack(ccard);
				}
                       		ram_outb(ccard, &RcIn->Rc, 0);
                        }
                        /* get buffer address of next return code   */
                        RcIn = (eicon_RC *)&prram->B[ram_inw(ccard, &RcIn->next)];
                }
                /* clear all return codes (no chaining!) */
                ram_outb(ccard, &prram->RcOutput, 0);
        }

        /* if indications are available ... */
        if((Count = ram_inb(ccard, &prram->IndOutput))) {
		eicon_IND *ind;
                /* get the buffer address of the first indication */
                IndIn = (eicon_IND *)&prram->B[ram_inw(ccard, &prram->NextInd)];
                /* for all indications do ... */
                while(Count--) {
			Ind = ram_inb(ccard, &IndIn->Ind);
			if(Ind) {
				int len = ram_inw(ccard, &IndIn->RBuffer.length);
				skb = alloc_skb((sizeof(eicon_IND) + len - 1), GFP_ATOMIC);
				if (!skb) {
                			eicon_log(ccard, 1, "eicon_io: skb_alloc failed in _irq()\n");
				} else {
					ind = (eicon_IND *)skb_put(skb, (sizeof(eicon_IND) + len - 1));
					ind->Ind = Ind;
					ind->IndId = ram_inb(ccard, &IndIn->IndId);
					ind->IndCh = ram_inb(ccard, &IndIn->IndCh);
					ind->MInd  = ram_inb(ccard, &IndIn->MInd);
					ind->MLength = ram_inw(ccard, &IndIn->MLength);
					ind->RBuffer.length = len;
					if ((Ind == 1) || (Ind == 8))
						dlev = 128;
					else
						dlev = 192;
                	        	eicon_log(ccard, dlev, "eicon: IRQ Ind=%d Id=%x Ch=%d MInd=%d MLen=%d Len=%d\n",
						Ind,ind->IndId,ind->IndCh,ind->MInd,ind->MLength,len);
	                                ram_copyfromcard(ccard, &ind->RBuffer.P, &IndIn->RBuffer.P, len);
					skb_queue_tail(&ccard->rcvq, skb);
					eicon_schedule_rx(ccard);
				}
				ram_outb(ccard, &IndIn->Ind, 0);
                        }
                        /* get buffer address of next indication  */
                        IndIn = (eicon_IND *)&prram->B[ram_inw(ccard, &IndIn->next)];
                }
                ram_outb(ccard, &prram->IndOutput, 0);
        }

    } 

	/* clear interrupt */
	switch(ccard->type) {
#ifdef CONFIG_ISDN_DRV_EICON_ISA
		case EICON_CTYPE_QUADRO:
			writeb(0, isa_card->intack);
			writeb(0, &com[0x401]);
			break;
		case EICON_CTYPE_S:
		case EICON_CTYPE_SX:
		case EICON_CTYPE_SCOM:
		case EICON_CTYPE_S2M:
			writeb(0, isa_card->intack);
			break;
#endif
		case EICON_CTYPE_MAESTRAP:
			writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
			writew(0, &cfg[MP_IRQ_RESET + 2]); 
			writeb(0, &ram[0x3fe]); 
			break;
		case EICON_CTYPE_MAESTRA:
			outb(0x08, pci_card->PCIreg + M_RESET);
			outw(0x3fe, pci_card->PCIreg + M_ADDR);
			outb(0, pci_card->PCIreg + M_DATA);
			break;
	}

  return;
}


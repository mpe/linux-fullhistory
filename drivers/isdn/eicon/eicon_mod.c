/* $Id: eicon_mod.c,v 1.5 1999/04/01 12:48:35 armin Exp $
 *
 * ISDN lowlevel-module for Eicon.Diehl active cards.
 * 
 * Copyright 1997    by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1998,99 by Armin Schindler (mac@melware.de) 
 * Copyright 1999    Cytronics & Melware (info@melware.de)
 * 
 * Thanks to    Eicon Technology Diehl GmbH & Co. oHG for
 *              documents, informations and hardware.
 *
 *              Deutsche Telekom AG for S2M support.
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
 * $Log: eicon_mod.c,v $
 * Revision 1.5  1999/04/01 12:48:35  armin
 * Changed some log outputs.
 *
 * Revision 1.4  1999/03/29 11:19:47  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.3  1999/03/02 12:37:47  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.2  1999/01/24 20:14:21  armin
 * Changed and added debug stuff.
 * Better data sending. (still problems with tty's flip buffer)
 *
 * Revision 1.1  1999/01/01 18:09:44  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#include "eicon.h"

#define INCLUDE_INLINE_FUNCS

static eicon_card *cards = (eicon_card *) NULL;

static char *eicon_revision = "$Revision: 1.5 $";

extern char *eicon_pci_revision;
extern char *eicon_isa_revision;
extern char *eicon_idi_revision;

#ifdef MODULE
#define MOD_USE_COUNT (GET_USE_COUNT (&__this_module))
#endif

#define EICON_CTRL_VERSION 1

ulong DebugVar;

/* Parameters to be set by insmod */
static int   membase      = -1;
static int   irq          = -1;
static char *id           = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

MODULE_DESCRIPTION(             "Driver for Eicon.Diehl active ISDN cards");
MODULE_AUTHOR(                  "Armin Schindler");
MODULE_SUPPORTED_DEVICE(        "ISDN subsystem");
MODULE_PARM_DESC(membase,	"Base address of first ISA card");
MODULE_PARM_DESC(irq,    	"IRQ of first card");
MODULE_PARM_DESC(id,   		"ID-String of first card");
MODULE_PARM(membase,    	"i");
MODULE_PARM(irq,          	"i");
MODULE_PARM(id,           	"s");

char *eicon_ctype_name[] = {
        "ISDN-S",
        "ISDN-SX",
        "ISDN-SCOM",
        "ISDN-QUADRO",
        "ISDN-S2M",
        "DIVA Server BRI/PCI",
        "DIVA Server 4BRI/PCI",
        "DIVA Server 4BRI/PCI",
        "DIVA Server PRI/PCI"
};

static int
getrel(char *p)
{
        int v = 0;
	char *tmp = 0;

	if ((tmp = strchr(p, '.')))
		p = tmp + 1;
        while (p[0] >= '0' && p[0] <= '9') {
                v = ((v < 0) ? 0 : (v * 10)) + (int) (p[0] - '0');
		p++;
	}
        return v;


}

static char *
eicon_getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else rev = "?.??";
	return rev;

}

static eicon_chan *
find_channel(eicon_card *card, int channel)
{
	if ((channel >= 0) && (channel < card->nchannels))
        	return &(card->bch[channel]);
	if (DebugVar & 1)
		printk(KERN_WARNING "eicon: Invalid channel %d\n", channel);
	return NULL;
}

/*
 * Free MSN list
 */
static void
eicon_clear_msn(eicon_card *card)
{
        struct msn_entry *p = card->msn_list;
        struct msn_entry *q;
	unsigned long flags;

	save_flags(flags);
	cli();
        card->msn_list = NULL;
	restore_flags(flags);
        while (p) {
                q  = p->next;
                kfree(p);
                p = q;
        }
}

/*
 * Find an MSN entry in the list.
 * If ia5 != 0, return IA5-encoded EAZ, else
 * return a bitmask with corresponding bit set.
 */
static __u16
eicon_find_msn(eicon_card *card, char *msn, int ia5)
{
        struct msn_entry *p = card->msn_list;
	__u8 eaz = '0';

	while (p) {
		if (!strcmp(p->msn, msn)) {
			eaz = p->eaz;
			break;
		}
		p = p->next;
	}
	if (!ia5)
		return (1 << (eaz - '0'));
	else
		return eaz;
}

/*
 * Find an EAZ entry in the list.
 * return a string with corresponding msn.
 */
char *
eicon_find_eaz(eicon_card *card, char eaz)
{
        struct msn_entry *p = card->msn_list;

	while (p) {
		if (p->eaz == eaz)
			return(p->msn);
		p = p->next;
	}
	return("\0");
}

#if 0
/*
 * Add or delete an MSN to the MSN list
 *
 * First character of msneaz is EAZ, rest is MSN.
 * If length of eazmsn is 1, delete that entry.
 */
static int
eicon_set_msn(eicon_card *card, char *eazmsn)
{
        struct msn_entry *p = card->msn_list;
        struct msn_entry *q = NULL;
	unsigned long flags;
	int i;
	
	if (!strlen(eazmsn))
		return 0;
	if (strlen(eazmsn) > 16)
		return -EINVAL;
	for (i = 0; i < strlen(eazmsn); i++)
		if (!isdigit(eazmsn[i]))
			return -EINVAL;
        if (strlen(eazmsn) == 1) {
		/* Delete a single MSN */
		while (p) {
			if (p->eaz == eazmsn[0]) {
				save_flags(flags);
				cli();
				if (q)
					q->next = p->next;
				else
					card->msn_list = p->next;
				restore_flags(flags);
				kfree(p);
				if (DebugVar & 8)
					printk(KERN_DEBUG
					       "Mapping for EAZ %c deleted\n",
					       eazmsn[0]);
				return 0;
			}
			q = p;
			p = p->next;
		}
		return 0;
        }
	/* Add a single MSN */
	while (p) {
		/* Found in list, replace MSN */
		if (p->eaz == eazmsn[0]) {
			save_flags(flags);
			cli();
			strcpy(p->msn, &eazmsn[1]);
			restore_flags(flags);
			if (DebugVar & 8)
				printk(KERN_DEBUG
				       "Mapping for EAZ %c changed to %s\n",
				       eazmsn[0],
				       &eazmsn[1]);
			return 0;
		}
		p = p->next;
	}
	/* Not found in list, add new entry */
	p = kmalloc(sizeof(msn_entry), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->eaz = eazmsn[0];
	strcpy(p->msn, &eazmsn[1]);
	p->next = card->msn_list;
	save_flags(flags);
	cli();
	card->msn_list = p;
	restore_flags(flags);
	if (DebugVar & 8)
		printk(KERN_DEBUG
		       "Mapping %c -> %s added\n",
		       eazmsn[0],
		       &eazmsn[1]);
	return 0;
}
#endif

static void
eicon_rcv_dispatch(struct eicon_card *card)
{
	switch (card->bus) {
		case EICON_BUS_ISA:
		case EICON_BUS_PCI:
			eicon_io_rcv_dispatch(card);
			break;
		case EICON_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
				       "eicon_ack_dispatch: Illegal bustype %d\n", card->bus);
	}
}

static void
eicon_ack_dispatch(struct eicon_card *card)
{
	switch (card->bus) {
		case EICON_BUS_ISA:
		case EICON_BUS_PCI:
			eicon_io_ack_dispatch(card);
			break;
		case EICON_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
			       		"eicon_ack_dispatch: Illegal bustype %d\n", card->bus);
	}
}

static void
eicon_transmit(struct eicon_card *card)
{
	switch (card->bus) {
		case EICON_BUS_ISA:
		case EICON_BUS_PCI:
			eicon_io_transmit(card);
			break;
		case EICON_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
				       "eicon_transmit: Illegal bustype %d\n", card->bus);
	}
}

static int
eicon_command(eicon_card * card, isdn_ctrl * c)
{
        ulong a;
        eicon_chan *chan;
	eicon_cdef cdef;
	isdn_ctrl cmd;
	char tmp[17];
	int ret = 0;
	unsigned long flags;
 
        switch (c->command) {
		case ISDN_CMD_IOCTL:
			memcpy(&a, c->parm.num, sizeof(ulong));
			switch (c->arg) {
				case EICON_IOCTL_GETVER:
					return(EICON_CTRL_VERSION);
				case EICON_IOCTL_GETTYPE:
					return(card->type);
				case EICON_IOCTL_GETMMIO:
					switch (card->bus) {
						case EICON_BUS_ISA:
							return (int)card->hwif.isa.shmem;
#if CONFIG_PCI
						case EICON_BUS_PCI:
							return card->hwif.pci.PCIram;
#endif
						default:
							if (DebugVar & 1)
								printk(KERN_WARNING
								       "eicon: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}
				case EICON_IOCTL_SETMMIO:
					if (card->flags & EICON_FLAGS_LOADED)
						return -EBUSY;
					switch (card->bus) {
						case EICON_BUS_ISA:
							if (eicon_isa_find_card(a,
								card->hwif.isa.irq,
								card->regname) < 0)
								return -EFAULT;
							card->hwif.isa.shmem = (eicon_isa_shmem *)a;
							return 0;
						default:
							if (DebugVar & 1)
								printk(KERN_WARNING
							      		"eicon: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}					
				case EICON_IOCTL_GETIRQ:
					switch (card->bus) {
						case EICON_BUS_ISA:
							return card->hwif.isa.irq;
#if CONFIG_PCI
						case EICON_BUS_PCI:
							return card->hwif.pci.irq;
#endif
						default:
							if (DebugVar & 1)
								printk(KERN_WARNING
								       "eicon: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}
				case EICON_IOCTL_SETIRQ:
					if (card->flags & EICON_FLAGS_LOADED)
						return -EBUSY;
					if ((a < 2) || (a > 15))
						return -EFAULT;
					switch (card->bus) {
						case EICON_BUS_ISA:
							card->hwif.isa.irq = a;
							return 0;
						default:
							if (DebugVar & 1)
								printk(KERN_WARNING
							      		"eicon: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}					
				case EICON_IOCTL_LOADBOOT:
					if (card->flags & EICON_FLAGS_RUNNING)
						return -EBUSY;  
					switch (card->bus) {
						case EICON_BUS_ISA:
							ret = eicon_isa_bootload(
								&(card->hwif.isa),
								&(((eicon_codebuf *)a)->isa));
							break;
						default:
							if (DebugVar & 1)
								printk(KERN_WARNING
								       "eicon: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}
					return ret;
				case EICON_IOCTL_LOADISA:
					if (card->flags & EICON_FLAGS_RUNNING)
						return -EBUSY;  
					switch (card->bus) {
						case EICON_BUS_ISA:
							ret = eicon_isa_load(
								&(card->hwif.isa),
								&(((eicon_codebuf *)a)->isa));
							if (!ret) {
                                                                card->flags |= EICON_FLAGS_LOADED;
                                                                card->flags |= EICON_FLAGS_RUNNING;
								if (card->hwif.isa.channels > 1) {
									cmd.command = ISDN_STAT_ADDCH;
									cmd.driver = card->myid;
									cmd.arg = card->hwif.isa.channels - 1;
									card->interface.statcallb(&cmd);
								}
								cmd.command = ISDN_STAT_RUN;    
								cmd.driver = card->myid;        
								cmd.arg = 0;                    
								card->interface.statcallb(&cmd);
							}
							break;
						default:
							if (DebugVar & 1)
								printk(KERN_WARNING
								       "eicon: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}
					return ret;

				case EICON_IOCTL_MANIF:
					if (!card->flags & EICON_FLAGS_RUNNING)
						return -ENODEV;
					if (!card->Feature & PROTCAP_MANIF)
						return -ENODEV;
					ret = eicon_idi_manage(
						card, 
						(eicon_manifbuf *)a);
					return ret;
#if CONFIG_PCI 
				case EICON_IOCTL_LOADPCI:
						if (card->flags & EICON_FLAGS_RUNNING)
							return -EBUSY;  
                                                if (card->bus == EICON_BUS_PCI) {
							switch(card->type) {
								case EICON_CTYPE_MAESTRA:
                                                		        ret = eicon_pci_load_bri(
		                                                                &(card->hwif.pci),
                		                                                &(((eicon_codebuf *)a)->pci)); 
									break;

								case EICON_CTYPE_MAESTRAP:
		                                                        ret = eicon_pci_load_pri(
                		                                                &(card->hwif.pci),
                                		                                &(((eicon_codebuf *)a)->pci)); 
									break;
							}
                                                        if (!ret) {
                                                                card->flags |= EICON_FLAGS_LOADED;
                                                                card->flags |= EICON_FLAGS_RUNNING;
								if (card->hwif.pci.channels > 1) {
									cmd.command = ISDN_STAT_ADDCH;
									cmd.driver = card->myid;
									cmd.arg = card->hwif.pci.channels - 1;
									card->interface.statcallb(&cmd);
								}
								cmd.command = ISDN_STAT_RUN;    
								cmd.driver = card->myid;        
								cmd.arg = 0;                    
								card->interface.statcallb(&cmd);
							} 
                                                        return ret;
						} else return -ENODEV;
#endif
#if 0
				case EICON_IOCTL_SETMSN:
					if ((ret = copy_from_user(tmp, (char *)a, sizeof(tmp))))
						return -EFAULT;
					if ((ret = eicon_set_msn(card, tmp)))
						return ret;
#if 0
					if (card->flags & EICON_FLAGS_RUNNING)
						return(eicon_capi_manufacturer_req_msn(card));
#endif
					return 0;
#endif
				case EICON_IOCTL_ADDCARD:
					if ((ret = copy_from_user(&cdef, (char *)a, sizeof(cdef))))
						return -EFAULT;
					if (!(eicon_addcard(0, cdef.membase, cdef.irq, cdef.id)))
						return -EIO;
					return 0;
				case EICON_IOCTL_DEBUGVAR:
					DebugVar = a;
					printk(KERN_DEBUG"Eicon: Debug Value set to %ld\n", DebugVar);
					return 0;
#ifdef MODULE
				case EICON_IOCTL_FREEIT:
					while (MOD_USE_COUNT > 0) MOD_DEC_USE_COUNT;
					MOD_INC_USE_COUNT;
					return 0;
#endif
				default:
					return -EINVAL;
			}
			break;
		case ISDN_CMD_DIAL:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			save_flags(flags);
			cli();
			if ((chan->fsm_state != EICON_STATE_NULL) && (chan->fsm_state != EICON_STATE_LISTEN)) {
				restore_flags(flags);
				if (DebugVar & 1)
					printk(KERN_WARNING "Dial on channel %d with state %d\n",
					chan->No, chan->fsm_state);
				return -EBUSY;
			}
			if (card->ptype == ISDN_PTYPE_EURO)
				tmp[0] = eicon_find_msn(card, c->parm.setup.eazmsn, 1);
			else
				tmp[0] = c->parm.setup.eazmsn[0];
			chan->fsm_state = EICON_STATE_OCALL;
			chan->callref = 0xffff;
			restore_flags(flags);
			
			ret = idi_connect_req(card, chan, c->parm.setup.phone,
						     c->parm.setup.eazmsn,
						     c->parm.setup.si1,
						     c->parm.setup.si2);
			if (ret) {
				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_DHUP;
				cmd.arg &= 0x1f;
				card->interface.statcallb(&cmd);
			}
			return ret;
		case ISDN_CMD_ACCEPTD:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			if (chan->fsm_state == EICON_STATE_ICALL) { 
				idi_connect_res(card, chan);
			}
			return 0;
		case ISDN_CMD_ACCEPTB:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			return 0;
		case ISDN_CMD_HANGUP:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			idi_hangup(card, chan);
			return 0;
		case ISDN_CMD_SETEAZ:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			if (strlen(c->parm.num)) {
				if (card->ptype == ISDN_PTYPE_EURO) {
					chan->eazmask = eicon_find_msn(card, c->parm.num, 0);
				}
				if (card->ptype == ISDN_PTYPE_1TR6) {
					int i;
					chan->eazmask = 0;
					for (i = 0; i < strlen(c->parm.num); i++)
						if (isdigit(c->parm.num[i]))
							chan->eazmask |= (1 << (c->parm.num[i] - '0'));
				}
			} else
				chan->eazmask = 0x3ff;
			eicon_idi_listen_req(card, chan);
			return 0;
		case ISDN_CMD_CLREAZ:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->eazmask = 0;
			eicon_idi_listen_req(card, chan);
			return 0;
		case ISDN_CMD_SETL2:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->l2prot = (c->arg >> 8);
			return 0;
		case ISDN_CMD_GETL2:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			return chan->l2prot;
		case ISDN_CMD_SETL3:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if ((c->arg >> 8) != ISDN_PROTO_L3_TRANS) {
				if (DebugVar & 1)
					printk(KERN_WARNING "L3 protocol unknown\n");
				return -1;
			}
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->l3prot = (c->arg >> 8);
			return 0;
		case ISDN_CMD_GETL3:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			return chan->l3prot;
		case ISDN_CMD_GETEAZ:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (DebugVar & 1)
				printk(KERN_DEBUG "eicon CMD_GETEAZ not implemented\n");
			return 0;
		case ISDN_CMD_SETSIL:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (DebugVar & 1)
				printk(KERN_DEBUG "eicon CMD_SETSIL not implemented\n");
			return 0;
		case ISDN_CMD_GETSIL:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (DebugVar & 1)
				printk(KERN_DEBUG "eicon CMD_GETSIL not implemented\n");
			return 0;
		case ISDN_CMD_LOCK:
			MOD_INC_USE_COUNT;
			return 0;
		case ISDN_CMD_UNLOCK:
			MOD_DEC_USE_COUNT;
			return 0;
        }
	
        return -EINVAL;
}

/*
 * Find card with given driverId
 */
static inline eicon_card *
eicon_findcard(int driverid)
{
        eicon_card *p = cards;

        while (p) {
                if (p->myid == driverid)
                        return p;
                p = p->next;
        }
        return (eicon_card *) 0;
}

/*
 * Wrapper functions for interface to linklevel
 */
static int
if_command(isdn_ctrl * c)
{
        eicon_card *card = eicon_findcard(c->driver);

        if (card)
                return (eicon_command(card, c));
        printk(KERN_ERR
             "eicon: if_command %d called with invalid driverId %d!\n",
               c->command, c->driver);
        return -ENODEV;
}

static int
if_writecmd(const u_char * buf, int len, int user, int id, int channel)
{
        eicon_card *card = eicon_findcard(id);

        if (card) {
                if (!card->flags & EICON_FLAGS_RUNNING)
                        return -ENODEV;
                return (len);
        }
        printk(KERN_ERR
               "eicon: if_writecmd called with invalid driverId!\n");
        return -ENODEV;
}

static int
if_readstatus(u_char * buf, int len, int user, int id, int channel)
{
#if 0
	/* Not yet used */
        eicon_card *card = eicon_findcard(id);
	
        if (card) {
                if (!card->flags & EICON_FLAGS_RUNNING)
                        return -ENODEV;
                return (eicon_readstatus(buf, len, user, card));
        }
        printk(KERN_ERR
               "eicon: if_readstatus called with invalid driverId!\n");
#endif
        return -ENODEV;
}

static int
if_sendbuf(int id, int channel, int ack, struct sk_buff *skb)
{
        eicon_card *card = eicon_findcard(id);
	eicon_chan *chan;
	
        if (card) {
                if (!card->flags & EICON_FLAGS_RUNNING) {
			dev_kfree_skb(skb);
                        return -ENODEV;
		}
        	if (!(chan = find_channel(card, channel))) {
			dev_kfree_skb(skb);
			return -ENODEV;
		}
		if (chan->fsm_state == EICON_STATE_ACTIVE)
			return (idi_send_data(card, chan, ack, skb));
		else {
			dev_kfree_skb(skb);
			return -ENODEV;
		}
        }
        printk(KERN_ERR
               "eicon: if_sendbuf called with invalid driverId!\n");
	dev_kfree_skb(skb);
        return -ENODEV;
}


/*
 * Allocate a new card-struct, initialize it
 * link it into cards-list.
 */
static void
eicon_alloccard(int Type, int membase, int irq, char *id)
{
	int i;
	int j;
	int qloop;
	char qid[5];
        eicon_card *card;
#if CONFIG_PCI
	eicon_pci_card *pcic;
#endif

	qloop = (Type == EICON_CTYPE_QUADRO)?2:0;
	for (i = 0; i <= qloop; i++) {
		if (!(card = (eicon_card *) kmalloc(sizeof(eicon_card), GFP_KERNEL))) {
			printk(KERN_WARNING
			       "eicon: (%s) Could not allocate card-struct.\n", id);
			return;
		}
		memset((char *) card, 0, sizeof(eicon_card));
		skb_queue_head_init(&card->sndq);
		skb_queue_head_init(&card->rcvq);
		skb_queue_head_init(&card->rackq);
		skb_queue_head_init(&card->sackq);
		card->snd_tq.routine = (void *) (void *) eicon_transmit;
		card->snd_tq.data = card;
		card->rcv_tq.routine = (void *) (void *) eicon_rcv_dispatch;
		card->rcv_tq.data = card;
		card->ack_tq.routine = (void *) (void *) eicon_ack_dispatch;
		card->ack_tq.data = card;
		card->interface.maxbufsize = 4000;
		card->interface.command = if_command;
		card->interface.writebuf_skb = if_sendbuf;
		card->interface.writecmd = if_writecmd;
		card->interface.readstat = if_readstatus;
		card->interface.features =
			ISDN_FEATURE_L2_X75I |
			ISDN_FEATURE_L2_HDLC |
			ISDN_FEATURE_L2_TRANS |
			ISDN_FEATURE_L3_TRANS |
			ISDN_FEATURE_P_UNKNOWN;
		card->interface.hl_hdrlen = 20;
		card->ptype = ISDN_PTYPE_UNKNOWN;
		strncpy(card->interface.id, id, sizeof(card->interface.id) - 1);
		card->myid = -1;
		card->type = Type;
		switch (Type) {
			case EICON_CTYPE_QUADRO:
				if (membase == -1)
					membase = EICON_ISA_MEMBASE;
				if (irq == -1)
					irq = EICON_ISA_IRQ;
				card->bus = EICON_BUS_ISA;
				card->hwif.isa.card = (void *)card;
				card->hwif.isa.shmem = (eicon_isa_shmem *)(membase + (i+1) * EICON_ISA_QOFFSET);
				card->hwif.isa.master = 0;
				strcpy(card->interface.id, id);
				if (id[strlen(id) - 1] == 'a') {
					card->interface.id[strlen(id) - 1] = 'a' + i + 1;
				} else {
					sprintf(qid, "_%c",'2' + i);
					strcat(card->interface.id, qid);
				}
				printk(KERN_INFO "Eicon: Quadro: Driver-Id %s added.\n",
					card->interface.id);
				if (i == 0) {
					eicon_card *p = cards;
					while(p) {
						if ((p->hwif.isa.master) && (p->hwif.isa.irq == irq)) {
							p->qnext = card;
							break;
						}
						p = p->next;
					}
					if (!p) {
						printk(KERN_WARNING "eicon_alloccard: Quadro Master not found.\n");
						kfree(card);
						return;
					}
				} else {
					cards->qnext = card;
				}
				card->hwif.isa.irq = irq;
				card->hwif.isa.type = Type;
				card->nchannels = 2;
				card->interface.channels = 1;
				break;
#if CONFIG_PCI
			case EICON_CTYPE_MAESTRA:
				(eicon_pci_card *)pcic = (eicon_pci_card *)membase;
                                card->bus = EICON_BUS_PCI;
				card->interface.features |=
					ISDN_FEATURE_L2_V11096 |
					ISDN_FEATURE_L2_V11019 |
					ISDN_FEATURE_L2_V11038 |
					ISDN_FEATURE_L2_MODEM;
                                card->hwif.pci.card = (void *)card;
				card->hwif.pci.PCIreg = pcic->PCIreg;
				card->hwif.pci.PCIcfg = pcic->PCIcfg;
                                card->hwif.pci.master = 1;
                                card->hwif.pci.mvalid = pcic->mvalid;
                                card->hwif.pci.ivalid = 0;
                                card->hwif.pci.irq = irq;
                                card->hwif.pci.type = Type;
				card->flags = 0;
                                card->nchannels = 2;
				card->interface.channels = 1;
				break;

			case EICON_CTYPE_MAESTRAP:
				(eicon_pci_card *)pcic = (eicon_pci_card *)membase;
                                card->bus = EICON_BUS_PCI;
				card->interface.features |=
					ISDN_FEATURE_L2_V11096 |
					ISDN_FEATURE_L2_V11019 |
					ISDN_FEATURE_L2_V11038 |
					ISDN_FEATURE_L2_MODEM;
                                card->hwif.pci.card = (void *)card;
                                card->hwif.pci.shmem = (eicon_pci_shmem *)pcic->shmem;
				card->hwif.pci.PCIreg = pcic->PCIreg;
				card->hwif.pci.PCIram = pcic->PCIram;
				card->hwif.pci.PCIcfg = pcic->PCIcfg;
                                card->hwif.pci.master = 1;
                                card->hwif.pci.mvalid = pcic->mvalid;
                                card->hwif.pci.ivalid = 0;
                                card->hwif.pci.irq = irq;
                                card->hwif.pci.type = Type;
				card->flags = 0;
                                card->nchannels = 30;
				card->interface.channels = 1;
				break;
#endif
			case EICON_CTYPE_ISABRI:
				if (membase == -1)
					membase = EICON_ISA_MEMBASE;
				if (irq == -1)
					irq = EICON_ISA_IRQ;
				card->bus = EICON_BUS_ISA;
				card->hwif.isa.card = (void *)card;
				card->hwif.isa.shmem = (eicon_isa_shmem *)membase;
				card->hwif.isa.master = 1;
				card->hwif.isa.irq = irq;
				card->hwif.isa.type = Type;
				card->nchannels = 2;
				card->interface.channels = 1;
				break;
			case EICON_CTYPE_ISAPRI:
				if (membase == -1)
					membase = EICON_ISA_MEMBASE;
				if (irq == -1)
					irq = EICON_ISA_IRQ;
				card->bus = EICON_BUS_ISA;
				card->hwif.isa.card = (void *)card;
				card->hwif.isa.shmem = (eicon_isa_shmem *)membase;
				card->hwif.isa.master = 1;
				card->hwif.isa.irq = irq;
				card->hwif.isa.type = Type;
				card->nchannels = 30;
				card->interface.channels = 1;
				break;
			default:
				printk(KERN_WARNING "eicon_alloccard: Invalid type %d\n", Type);
				kfree(card);
				return;
		}
		if (!(card->bch = (eicon_chan *) kmalloc(sizeof(eicon_chan) * (card->nchannels + 1)
							 , GFP_KERNEL))) {
			printk(KERN_WARNING
			       "eicon: (%s) Could not allocate bch-struct.\n", id);
			kfree(card);
			return;
		}
		for (j=0; j< (card->nchannels + 1); j++) {
			memset((char *)&card->bch[j], 0, sizeof(eicon_chan));
			card->bch[j].plci = 0x8000;
			card->bch[j].ncci = 0x8000;
			card->bch[j].l2prot = ISDN_PROTO_L2_X75I;
			card->bch[j].l3prot = ISDN_PROTO_L3_TRANS;
			card->bch[j].e.D3Id = 0;
			card->bch[j].e.B2Id = 0;
			card->bch[j].e.Req = 0;
			card->bch[j].No = j;
			skb_queue_head_init(&card->bch[j].e.X);
			skb_queue_head_init(&card->bch[j].e.R);
		}
		card->next = cards;
		cards = card;
	}
}

/*
 * register card at linklevel
 */
static int
eicon_registercard(eicon_card * card)
{
        switch (card->bus) {
		case EICON_BUS_ISA:
			/* TODO something to print */
			break;
		case EICON_BUS_PCI:
#if CONFIG_PCI
			eicon_pci_printpar(&card->hwif.pci); 
			break;
#endif
		case EICON_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
				       "eicon_registercard: Illegal BUS type %d\n",
			       card->bus);
			return -1;
        }
        if (!register_isdn(&card->interface)) {
                printk(KERN_WARNING
                       "eicon_registercard: Unable to register %s\n",
                       card->interface.id);
                return -1;
        }
        card->myid = card->interface.channels;
        sprintf(card->regname, "%s", card->interface.id);
        return 0;
}

#ifdef MODULE
static void
unregister_card(eicon_card * card)
{
        isdn_ctrl cmd;

        cmd.command = ISDN_STAT_UNLOAD;
        cmd.driver = card->myid;
        card->interface.statcallb(&cmd);
        switch (card->bus) {
		case EICON_BUS_ISA:
			eicon_isa_release(&card->hwif.isa);
			break;
		case EICON_BUS_PCI:
#if CONFIG_PCI
			eicon_pci_release(&card->hwif.pci);
			break;
#endif
		case EICON_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
				       "eicon: Invalid BUS type %d\n",
			       card->bus);
			break;
        }
}
#endif /* MODULE */

static void
eicon_freecard(eicon_card *card) {
	eicon_clear_msn(card);
	kfree(card->bch);
	kfree(card);
}

int
eicon_addcard(int Type, int membase, int irq, char *id)
{
	eicon_card *p;
	eicon_card *q = NULL;
	int registered;
	int added = 0;
	int failed = 0;

	if (!Type) /* ISA */
		if ((Type = eicon_isa_find_card(membase, irq, id)) < 0)
			return 0;
	eicon_alloccard(Type, membase, irq, id);
        p = cards;
        while (p) {
		registered = 0;
		if (!p->interface.statcallb) {
			/* Not yet registered.
			 * Try to register and activate it.
			 */
			added++;
			switch (p->bus) {
				case EICON_BUS_ISA:
					if (eicon_registercard(p))
						break;
					registered = 1;
					break;
				case EICON_BUS_PCI:
#if CONFIG_PCI
					if (eicon_registercard(p))
						break;
					registered = 1;
					break;
#endif
				case EICON_BUS_MCA:
				default:
					if (DebugVar & 1)
						printk(KERN_WARNING
						       "eicon: addcard: Invalid BUS type %d\n",
					       p->bus);
			}
		} else
			/* Card already registered */
			registered = 1;
                if (registered) {
			/* Init OK, next card ... */
                        q = p;
                        p = p->next;
                } else {
                        /* registering failed, remove card from list, free memory */
                        printk(KERN_WARNING
                               "eicon: Initialization of %s failed\n",
                               p->interface.id);
                        if (q) {
                                q->next = p->next;
                                eicon_freecard(p);
                                p = q->next;
                        } else {
                                cards = p->next;
                                eicon_freecard(p);
                                p = cards;
                        }
			failed++;
                }
	}
        return (added - failed);
}

#define DRIVERNAME "Eicon active ISDN driver"
#define DRIVERRELEASE "1"

#ifdef MODULE
#define eicon_init init_module
#endif

__initfunc(int
eicon_init(void))
{
	int tmp = 0;
	int release = 0;
	char tmprev[50];

	DebugVar = 1;

        printk(KERN_INFO "%s Rev: ", DRIVERNAME);
	strcpy(tmprev, eicon_revision);
	printk("%s/", eicon_getrev(tmprev));
	release += getrel(tmprev);
	strcpy(tmprev, eicon_pci_revision);
	printk("%s/", eicon_getrev(tmprev));
	release += getrel(tmprev);
	strcpy(tmprev, eicon_isa_revision);
	printk("%s/", eicon_getrev(tmprev));
	release += getrel(tmprev);
	strcpy(tmprev, eicon_idi_revision);
	printk("%s\n", eicon_getrev(tmprev));
	release += getrel(tmprev);
	sprintf(tmprev,"%d", release);
        printk(KERN_INFO "%s Release: %s.%s\n", DRIVERNAME,
		DRIVERRELEASE, tmprev);

	tmp = eicon_addcard(0, membase, irq, id);
#if CONFIG_PCI
	tmp += eicon_pci_find_card(id);
#endif
        if (!cards) {
#ifdef MODULE
                printk(KERN_INFO "Eicon: No cards defined, driver not loaded !\n");
#endif
		return -ENODEV;

	} else
		printk(KERN_INFO "Eicon: %d card%s added\n", tmp, (tmp>1)?"s":"");
        /* No symbols to export, hide all symbols */
        EXPORT_NO_SYMBOLS;
        return 0;
}

#ifdef MODULE
void
cleanup_module(void)
{
        eicon_card *card = cards;
        eicon_card *last;
        while (card) {
                unregister_card(card); 
                card = card->next;
        }
        card = cards;
        while (card) {
                last = card;
                card = card->next;
		eicon_freecard(last);
        }
        printk(KERN_INFO "%s unloaded\n", DRIVERNAME);
}

#else
__initfunc(void
eicon_setup(char *str, int *ints))
{
        int i, argc;

        argc = ints[0];
        i = 1;
        if (argc) {
		membase = irq = -1;
		if (argc) {
			membase = ints[i];
			i++;
			argc--;
		}
		if (argc) {
			irq = ints[i];
			i++;
			argc--;
		}
		if (strlen(str)) {
			strcpy(id, str);
		} else {
			strcpy(id, "eicon");
		} 
		/* eicon_addcard(0, membase, irq, id); */
       		printk(KERN_INFO "eicon: membase=0x%x irq=%d id=%s\n", membase, irq, id);
	}
}
#endif

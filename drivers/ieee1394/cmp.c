/* -*- c-basic-offset: 8 -*-
 *
 * cmp.c - Connection Management Procedures
 * Copyright (C) 2001 Kristian H�gsberg
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

/* TODO
 * ----
 *
 * - Implement IEC61883-1 output plugs and connection management.
 *   This should probably be part of the general subsystem, as it could
 *   be shared with dv1394.
 *
 * - Add IEC61883 unit directory when loading this module.  This
 *   requires a run-time changeable config rom.
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "hosts.h"
#include "highlevel.h"
#include "ieee1394.h"
#include "ieee1394_core.h"
#include "cmp.h"

struct plug {
	union {
		struct cmp_pcr pcr;
		quadlet_t quadlet;
	} u;
	void (*update)(struct cmp_pcr *plug, void *data);
	void *data;
};

struct cmp_host {
	struct hpsb_host *host;

	union {
		struct cmp_mpr ompr;
		quadlet_t ompr_quadlet;
	} u;
	struct plug opcr[2];

	union {
		struct cmp_mpr impr;
		quadlet_t impr_quadlet;
	} v;
	struct plug ipcr[2];

	struct list_head link;
};

enum {
	CMP_P2P_CONNECTION,
	CMP_BC_CONNECTION
};

#define CSR_PCR_MAP      0x900
#define CSR_PCR_MAP_END  0x9fc

static struct hpsb_highlevel *cmp_highlevel;

static LIST_HEAD(host_list);
static spinlock_t host_list_lock = SPIN_LOCK_UNLOCKED;

static struct cmp_host *
lookup_cmp_host(struct hpsb_host *host)
{
	struct cmp_host *ch;
	struct list_head *lh;
	unsigned long flags;

	ch = NULL;
	spin_lock_irqsave(&host_list_lock, flags);
	list_for_each(lh, &host_list) {
		ch = list_entry(lh, struct cmp_host, link);
		if (ch->host == host)
			break;
	}
	spin_unlock_irqrestore(&host_list_lock, flags);

	if (lh == &host_list)
		return NULL;
	else
		return ch;
}

struct cmp_pcr *
cmp_register_opcr(struct hpsb_host *host, int opcr_number, int payload,
		  void (*update)(struct cmp_pcr *pcr, void *data),
		  void *data)
{
	struct cmp_host *ch;
	struct plug *plug;

	ch = lookup_cmp_host(host);

	if (opcr_number >= ch->u.ompr.nplugs ||
	    ch->opcr[opcr_number].update != NULL)
		return NULL;

	plug = &ch->opcr[opcr_number];
	plug->u.pcr.online = 1;
	plug->u.pcr.bcast_count = 0;
	plug->u.pcr.p2p_count = 0;
	plug->u.pcr.overhead = 0;
	plug->u.pcr.payload = payload;
	plug->update = update;
	plug->data = data;

	return &plug->u.pcr;
}

void cmp_unregister_opcr(struct hpsb_host *host, struct cmp_pcr *opcr)
{
	struct cmp_host *ch;
	struct plug *plug;

	ch = lookup_cmp_host(host);
	plug = (struct plug *)opcr;
	if (plug - ch->opcr >= ch->u.ompr.nplugs) BUG();

	plug->u.pcr.online = 0;
	plug->update = NULL;
}

static void reset_plugs(struct cmp_host *ch)
{
	int i;

	ch->u.ompr.non_persistent_ext = 0xff;
	for (i = 0; i < ch->u.ompr.nplugs; i++) {
		ch->opcr[i].u.pcr.bcast_count = 0;
		ch->opcr[i].u.pcr.p2p_count = 0;
		ch->opcr[i].u.pcr.overhead = 0;
	}
}

static void cmp_add_host(struct hpsb_host *host)
{
	struct cmp_host *ch;

	ch = kmalloc(sizeof *ch, SLAB_KERNEL);
	if (ch == NULL) {
		HPSB_ERR("Failed to allocate cmp_host");
		return;
	}
	memset(ch, 0, sizeof *ch);
	ch->host = host;
	ch->u.ompr.rate = SPEED_100;
	ch->u.ompr.bcast_channel_base = 63;
	ch->u.ompr.nplugs = 2;
	reset_plugs(ch);

	spin_lock_irq(&host_list_lock);
	list_add_tail(&ch->link, &host_list);
	spin_unlock_irq(&host_list_lock);
}

static void cmp_host_reset(struct hpsb_host *host)
{
	struct cmp_host *ch;

	ch = lookup_cmp_host(host);
	if (ch == NULL) BUG();
	reset_plugs(ch);
}

static void cmp_remove_host(struct hpsb_host *host)
{
	struct cmp_host *ch;

	ch = lookup_cmp_host(host);
	if (ch == NULL) BUG();

	spin_lock_irq(&host_list_lock);
	list_del(&ch->link);
	spin_unlock_irq(&host_list_lock);

	kfree(ch);
}

static int pcr_read(struct hpsb_host *host, int nodeid, quadlet_t *buf,
		    u64 addr, unsigned int length)
{
	int csraddr = addr - CSR_REGISTER_BASE;
	int plug;
	struct cmp_host *ch;
	
	if (length != 4)
		return RCODE_TYPE_ERROR;

	ch = lookup_cmp_host(host);
	if (csraddr == 0x900) {
		*buf = cpu_to_be32(ch->u.ompr_quadlet);
		return RCODE_COMPLETE;   
	}
	else if (csraddr < 0x904 + ch->u.ompr.nplugs * 4) {
		plug = (csraddr - 0x904) / 4;
		*buf = cpu_to_be32(ch->opcr[plug].u.quadlet);
		return RCODE_COMPLETE;
	}
	else if (csraddr < 0x980) {
		return RCODE_ADDRESS_ERROR;
	}
	else if (csraddr == 0x980) {
		*buf = cpu_to_be32(ch->v.impr_quadlet);
		return RCODE_COMPLETE;   
	}
	else if (csraddr < 0x984 + ch->v.impr.nplugs * 4) {
		plug = (csraddr - 0x984) / 4;
		*buf = cpu_to_be32(ch->ipcr[plug].u.quadlet);
		return RCODE_COMPLETE;
	}
	else
		return RCODE_ADDRESS_ERROR;
}

static int pcr_lock(struct hpsb_host *host, int nodeid, quadlet_t *store,
		    u64 addr, quadlet_t data, quadlet_t arg, int extcode)
{
	int csraddr = addr - CSR_REGISTER_BASE;
	int plug;
	struct cmp_host *ch;

	ch = lookup_cmp_host(host);
	
	if (extcode != EXTCODE_COMPARE_SWAP) 
		return RCODE_TYPE_ERROR;
	
	if (csraddr == 0x900) {
		/* FIXME: Ignore writes to bits 30-31 and 0-7 */
		*store = cpu_to_be32(ch->u.ompr_quadlet);
		if (arg == cpu_to_be32(ch->u.ompr_quadlet))
			ch->u.ompr_quadlet = be32_to_cpu(data);

		return RCODE_COMPLETE;
	}
	if (csraddr < 0x904 + ch->u.ompr.nplugs * 4) {
		plug = (csraddr - 0x904) / 4;
		*store = cpu_to_be32(ch->opcr[plug].u.quadlet);

		if (arg == *store)
			ch->opcr[plug].u.quadlet = be32_to_cpu(data);

		if (be32_to_cpu(*store) != ch->opcr[plug].u.quadlet &&
		    ch->opcr[plug].update != NULL)
			ch->opcr[plug].update(&ch->opcr[plug].u.pcr,
					      ch->opcr[plug].data);

		return RCODE_COMPLETE;
	}
	else if (csraddr < 0x980) {
		return RCODE_ADDRESS_ERROR;
	}
	else if (csraddr == 0x980) {
		/* FIXME: Ignore writes to bits 24-31 and 0-7 */
		*store = cpu_to_be32(ch->u.ompr_quadlet);
		if (arg == cpu_to_be32(ch->u.ompr_quadlet))
			ch->u.ompr_quadlet = be32_to_cpu(data);

		return RCODE_COMPLETE;
	}
	else if (csraddr < 0x984 + ch->v.impr.nplugs * 4) {
		plug = (csraddr - 0x984) / 4;
		*store = cpu_to_be32(ch->ipcr[plug].u.quadlet);

		if (arg == *store)
			ch->ipcr[plug].u.quadlet = be32_to_cpu(data);

		if (be32_to_cpu(*store) != ch->ipcr[plug].u.quadlet &&
		    ch->ipcr[plug].update != NULL)
			ch->ipcr[plug].update(&ch->ipcr[plug].u.pcr,
					      ch->ipcr[plug].data);

		return RCODE_COMPLETE;
	}
	else
		return RCODE_ADDRESS_ERROR;
}


static struct hpsb_highlevel_ops cmp_highlevel_ops = {
	add_host:	cmp_add_host,
	remove_host:	cmp_remove_host,
        host_reset:	cmp_host_reset,
};

static struct hpsb_address_ops pcr_ops = {
        read: pcr_read,
        lock: pcr_lock,
};

/* Module interface */

MODULE_AUTHOR("Kristian Hogsberg <hogsberg@users.sf.net>");
MODULE_DESCRIPTION("Connection Management Procedures (CMP)");
MODULE_SUPPORTED_DEVICE("cmp");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(cmp_register_opcr);
EXPORT_SYMBOL(cmp_unregister_opcr);

static int __init cmp_init_module (void)
{
	cmp_highlevel = hpsb_register_highlevel ("cmp",
						 &cmp_highlevel_ops);
	if (cmp_highlevel == NULL) {
		HPSB_ERR("cmp: unable to register highlevel ops");
		return -EIO;
	}

	hpsb_register_addrspace(cmp_highlevel, &pcr_ops,
				CSR_REGISTER_BASE + CSR_PCR_MAP,
				CSR_REGISTER_BASE + CSR_PCR_MAP_END);

	HPSB_INFO("Loaded CMP driver");

	return 0;
}

static void __exit cmp_exit_module (void)
{
        hpsb_unregister_highlevel(cmp_highlevel);

	HPSB_INFO("Unloaded CMP driver");
}

module_init(cmp_init_module);
module_exit(cmp_exit_module);

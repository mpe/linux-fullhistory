/*
 * IEEE 1394 for Linux
 *
 * Exported symbols for module usage.
 *
 * Copyright (C) 1999 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>

#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "ieee1394_transactions.h"
#include "ieee1394_hotplug.h"
#include "highlevel.h"
#include "nodemgr.h"

EXPORT_SYMBOL(hpsb_register_lowlevel);
EXPORT_SYMBOL(hpsb_unregister_lowlevel);
EXPORT_SYMBOL(hpsb_get_host);
EXPORT_SYMBOL(hpsb_inc_host_usage);
EXPORT_SYMBOL(hpsb_dec_host_usage);

EXPORT_SYMBOL(alloc_hpsb_packet);
EXPORT_SYMBOL(free_hpsb_packet);
EXPORT_SYMBOL(hpsb_send_packet);
EXPORT_SYMBOL(hpsb_reset_bus);
EXPORT_SYMBOL(hpsb_bus_reset);
EXPORT_SYMBOL(hpsb_selfid_received);
EXPORT_SYMBOL(hpsb_selfid_complete);
EXPORT_SYMBOL(hpsb_packet_sent);
EXPORT_SYMBOL(hpsb_packet_received);

EXPORT_SYMBOL(get_tlabel);
EXPORT_SYMBOL(free_tlabel);
EXPORT_SYMBOL(fill_async_readquad);
EXPORT_SYMBOL(fill_async_readquad_resp);
EXPORT_SYMBOL(fill_async_readblock);
EXPORT_SYMBOL(fill_async_readblock_resp);
EXPORT_SYMBOL(fill_async_writequad);
EXPORT_SYMBOL(fill_async_writeblock);
EXPORT_SYMBOL(fill_async_write_resp);
EXPORT_SYMBOL(fill_async_lock);
EXPORT_SYMBOL(fill_async_lock_resp);
EXPORT_SYMBOL(fill_iso_packet);
EXPORT_SYMBOL(fill_phy_packet);
EXPORT_SYMBOL(hpsb_make_readqpacket);
EXPORT_SYMBOL(hpsb_make_readbpacket);
EXPORT_SYMBOL(hpsb_make_writeqpacket);
EXPORT_SYMBOL(hpsb_make_writebpacket);
EXPORT_SYMBOL(hpsb_make_lockpacket);
EXPORT_SYMBOL(hpsb_make_phypacket);
EXPORT_SYMBOL(hpsb_packet_success);
EXPORT_SYMBOL(hpsb_make_packet);
EXPORT_SYMBOL(hpsb_read);
EXPORT_SYMBOL(hpsb_write);
EXPORT_SYMBOL(hpsb_lock);

EXPORT_SYMBOL(hpsb_register_highlevel);
EXPORT_SYMBOL(hpsb_unregister_highlevel);
EXPORT_SYMBOL(hpsb_register_addrspace);
EXPORT_SYMBOL(hpsb_listen_channel);
EXPORT_SYMBOL(hpsb_unlisten_channel);
EXPORT_SYMBOL(highlevel_read);
EXPORT_SYMBOL(highlevel_write);
EXPORT_SYMBOL(highlevel_lock);
EXPORT_SYMBOL(highlevel_lock64);
EXPORT_SYMBOL(highlevel_add_host);
EXPORT_SYMBOL(highlevel_remove_host);
EXPORT_SYMBOL(highlevel_host_reset);
EXPORT_SYMBOL(highlevel_add_one_host);

EXPORT_SYMBOL(hpsb_guid_get_entry);
EXPORT_SYMBOL(hpsb_nodeid_get_entry);
EXPORT_SYMBOL(hpsb_get_host_by_ne);
EXPORT_SYMBOL(hpsb_guid_fill_packet);
EXPORT_SYMBOL(hpsb_register_protocol);
EXPORT_SYMBOL(hpsb_unregister_protocol);
EXPORT_SYMBOL(hpsb_release_unit_directory);

MODULE_LICENSE("GPL");

/*
 * IEEE 1394 for Linux
 *
 * Low level (host adapter) management.
 *
 * Copyright (C) 1999 Andreas E. Bombe
 * Copyright (C) 1999 Emanuel Pirker
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/slab.h>

#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"

static struct list_head hosts = LIST_HEAD_INIT(hosts);
static struct list_head host_drivers = LIST_HEAD_INIT(host_drivers);

spinlock_t hosts_lock = SPIN_LOCK_UNLOCKED;
spinlock_t host_drivers_lock = SPIN_LOCK_UNLOCKED;


static int dummy_transmit_packet(struct hpsb_host *h, struct hpsb_packet *p)
{
        return 0;
}

static int dummy_devctl(struct hpsb_host *h, enum devctl_cmd c, int arg)
{
        return -1;
}

static struct hpsb_host_operations dummy_ops = {
        transmit_packet:  dummy_transmit_packet,
        devctl:           dummy_devctl
};

/**
 * hpsb_ref_host - increase reference count for host controller.
 * @host: the host controller
 *
 * Increase the reference count for the specified host controller.
 * When holding a reference to a host, the memory allocated for the
 * host struct will not be freed and the host is guaranteed to be in a
 * consistent state.  The driver may be unloaded or the controller may
 * be removed (PCMCIA), but the host struct will remain valid.
 */

int hpsb_ref_host(struct hpsb_host *host)
{
        struct list_head *lh;
	unsigned long flags;
        int retval = 0;

        spin_lock_irqsave(&hosts_lock, flags);
        list_for_each(lh, &hosts) {
                if (host == list_entry(lh, struct hpsb_host, host_list)) {
                        host->ops->devctl(host, MODIFY_USAGE, 1);
			host->refcount++;
                        retval = 1;
			break;
        	}
        }
        spin_unlock_irqrestore(&hosts_lock, flags);

        return retval;
}

/**
 * hpsb_unref_host - decrease reference count for host controller.
 * @host: the host controller
 *
 * Decrease the reference count for the specified host controller.
 * When the reference count reaches zero, the memory allocated for the
 * &hpsb_host will be freed.
 */

void hpsb_unref_host(struct hpsb_host *host)
{
        unsigned long flags;

        host->ops->devctl(host, MODIFY_USAGE, 0);

        spin_lock_irqsave(&hosts_lock, flags);
        host->refcount--;

        if (!host->refcount && host->is_shutdown)
                kfree(host);
        spin_unlock_irqrestore(&hosts_lock, flags);
}

/**
 * hpsb_alloc_host - allocate a new host controller.
 * @drv: the driver that will manage the host controller
 * @extra: number of extra bytes to allocate for the driver
 *
 * Allocate a &hpsb_host and initialize the general subsystem specific
 * fields.  If the driver needs to store per host data, as drivers
 * usually do, the amount of memory required can be specified by the
 * @extra parameter.  Once allocated, the driver should initialize the
 * driver specific parts, enable the controller and make it available
 * to the general subsystem using hpsb_add_host().
 *
 * The &hpsb_host is allocated with an single initial reference
 * belonging to the driver.  Once the driver is done with the struct,
 * for example, when the driver is unloaded, it should release this
 * reference using hpsb_unref_host().
 *
 * Return Value: a pointer to the &hpsb_host if succesful, %NULL if
 * no memory was available.
 */

struct hpsb_host *hpsb_alloc_host(struct hpsb_host_driver *drv, size_t extra)
{
        struct hpsb_host *h;

        h = kmalloc(sizeof(struct hpsb_host) + extra, SLAB_KERNEL);
        if (!h) return NULL;
        memset(h, 0, sizeof(struct hpsb_host));

	h->hostdata = h + 1;
        h->driver = drv;
        h->ops = drv->ops;
	h->refcount = 1;

        INIT_LIST_HEAD(&h->pending_packets);
        spin_lock_init(&h->pending_pkt_lock);

        sema_init(&h->tlabel_count, 64);
        spin_lock_init(&h->tlabel_lock);

	atomic_set(&h->generation, 0);

	INIT_TQUEUE(&h->timeout_tq, (void (*)(void*))abort_timedouts, h);

        h->topology_map = h->csr.topology_map + 3;
        h->speed_map = (u8 *)(h->csr.speed_map + 2);

	return h;
}

void hpsb_add_host(struct hpsb_host *host)
{
        unsigned long flags;

        spin_lock_irqsave(&hosts_lock, flags);
        host->driver->number_of_hosts++;
        list_add_tail(&host->driver_list, &host->driver->hosts);
        list_add_tail(&host->host_list, &hosts);
        spin_unlock_irqrestore(&hosts_lock, flags);

        highlevel_add_host(host);
        host->ops->devctl(host, RESET_BUS, 0);
}

void hpsb_remove_host(struct hpsb_host *host)
{
        struct hpsb_host_driver *drv = host->driver;
        unsigned long flags;

        host->is_shutdown = 1;
        host->ops = &dummy_ops;
        highlevel_remove_host(host);

        spin_lock_irqsave(&hosts_lock, flags);
        list_del(&host->driver_list);
        list_del(&host->host_list);
        drv->number_of_hosts--;
        spin_unlock_irqrestore(&hosts_lock, flags);
}


struct hpsb_host_driver *hpsb_register_lowlevel(struct hpsb_host_operations *op,
                                                const char *name)
{
        struct hpsb_host_driver *drv;

        drv = kmalloc(sizeof(struct hpsb_host_driver), SLAB_KERNEL);
        if (!drv) return NULL;

        INIT_LIST_HEAD(&drv->list);
        INIT_LIST_HEAD(&drv->hosts);
        drv->number_of_hosts = 0;
        drv->name = name;
        drv->ops = op;

        spin_lock(&host_drivers_lock);
        list_add_tail(&drv->list, &host_drivers);
        spin_unlock(&host_drivers_lock);

        return drv;
}

void hpsb_unregister_lowlevel(struct hpsb_host_driver *drv)
{
        spin_lock(&host_drivers_lock);
        list_del(&drv->list);
        spin_unlock(&host_drivers_lock);

        kfree(drv);
}


/*
 * This function calls the given function for every host currently registered.
 */
void hl_all_hosts(void (*function)(struct hpsb_host*))
{
        struct list_head *lh;
        struct hpsb_host *host;

        spin_lock_irq(&hosts_lock);
        list_for_each (lh, &hosts) {
                host = list_entry(lh, struct hpsb_host, host_list);
                function(host);
	}
        spin_unlock_irq(&hosts_lock);
}

/*
 * AGPGART driver backend routines.
 * Copyright (C) 2002 Dave Jones.
 * Copyright (C) 1999 Jeff Hartmann.
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, DAVE JONES, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * TODO: 
 * - Allocate more than order 0 pages to avoid too much linear map splitting.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/miscdevice.h>
#include <linux/pm.h>
#include <linux/agp_backend.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include "agp.h"

/* Due to XFree86 brain-damage, we can't go to 1.0 until they
 * fix some real stupidity. It's only by chance we can bump
 * past 0.99 at all due to some boolean logic error. */
#define AGPGART_VERSION_MAJOR 0
#define AGPGART_VERSION_MINOR 100

struct agp_bridge_data agp_bridge_dummy = { .type = NOT_SUPPORTED };
struct agp_bridge_data *agp_bridge = &agp_bridge_dummy;

int agp_backend_acquire(void)
{
	if (agp_bridge->type == NOT_SUPPORTED)
		return -EINVAL;

	if (atomic_read(&agp_bridge->agp_in_use) != 0)
		return -EBUSY;

	atomic_inc(&agp_bridge->agp_in_use);
	return 0;
}

void agp_backend_release(void)
{
	if (agp_bridge->type == NOT_SUPPORTED)
		return;

	atomic_dec(&agp_bridge->agp_in_use);
}

struct agp_max_table {
	int mem;
	int agp;
};

static struct agp_max_table maxes_table[9] =
{
	{0, 0},
	{32, 4},
	{64, 28},
	{128, 96},
	{256, 204},
	{512, 440},
	{1024, 942},
	{2048, 1920},
	{4096, 3932}
};

static int agp_find_max (void)
{
	long memory, index, result;

	memory = (num_physpages << PAGE_SHIFT) >> 20;
	index = 1;

	while ((memory > maxes_table[index].mem) && (index < 8))
		index++;

	result = maxes_table[index - 1].agp +
	   ( (memory - maxes_table[index - 1].mem)  *
	     (maxes_table[index].agp - maxes_table[index - 1].agp)) /
	   (maxes_table[index].mem - maxes_table[index - 1].mem);

	printk(KERN_INFO PFX "Maximum main memory to use for agp memory: %ldM\n", result);
	result = result << (20 - PAGE_SHIFT);
	return result;
}

static struct agp_version agp_current_version =
{
	.major = AGPGART_VERSION_MAJOR,
	.minor = AGPGART_VERSION_MINOR,
};

static int agp_backend_initialize(struct pci_dev *dev)
{
	int size_value, rc, got_gatt=0, got_keylist=0;

	agp_bridge->max_memory_agp = agp_find_max();
	agp_bridge->version = &agp_current_version;

	if (agp_bridge->needs_scratch_page == TRUE) {
		void *addr;
		addr = agp_bridge->agp_alloc_page();

		if (addr == NULL) {
			printk(KERN_ERR PFX "unable to get memory for scratch page.\n");
			return -ENOMEM;
		}
		agp_bridge->scratch_page_real = virt_to_phys(addr);
		agp_bridge->scratch_page =
			agp_bridge->mask_memory(agp_bridge->scratch_page_real, 0);
	}

	size_value = agp_bridge->fetch_size();

	if (size_value == 0) {
		printk(KERN_ERR PFX "unable to determine aperture size.\n");
		rc = -EINVAL;
		goto err_out;
	}
	if (agp_bridge->create_gatt_table()) {
		printk(KERN_ERR PFX "unable to get memory for graphics translation table.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_gatt = 1;
	
	agp_bridge->key_list = vmalloc(PAGE_SIZE * 4);
	if (agp_bridge->key_list == NULL) {
		printk(KERN_ERR PFX "error allocating memory for key lists.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_keylist = 1;
	
	/* FIXME vmalloc'd memory not guaranteed contiguous */
	memset(agp_bridge->key_list, 0, PAGE_SIZE * 4);

	if (agp_bridge->configure()) {
		printk(KERN_ERR PFX "error configuring host chipset.\n");
		rc = -EINVAL;
		goto err_out;
	}

	printk(KERN_INFO PFX "AGP aperture is %dM @ 0x%lx\n",
	       size_value, agp_bridge->gart_bus_addr);

	return 0;

err_out:
	if (agp_bridge->needs_scratch_page == TRUE) {
		agp_bridge->agp_destroy_page(phys_to_virt(agp_bridge->scratch_page_real));
	}
	if (got_gatt)
		agp_bridge->free_gatt_table();
	if (got_keylist)
		vfree(agp_bridge->key_list);
	return rc;
}


/* cannot be __exit b/c as it could be called from __init code */
static void agp_backend_cleanup(void)
{
	if (agp_bridge->cleanup != NULL)
		agp_bridge->cleanup();
	if (agp_bridge->free_gatt_table != NULL)
		agp_bridge->free_gatt_table();
	if (agp_bridge->key_list)
		vfree(agp_bridge->key_list);

	if ((agp_bridge->agp_destroy_page!=NULL) &&
			(agp_bridge->needs_scratch_page == TRUE))
		agp_bridge->agp_destroy_page(phys_to_virt(agp_bridge->scratch_page_real));
}

static int agp_power(struct pm_dev *dev, pm_request_t rq, void *data)
{
	switch(rq)
	{
		case PM_SUSPEND:
			return agp_bridge->suspend();
		case PM_RESUME:
			agp_bridge->resume();
			return 0;
	}		
	return 0;
}

extern int agp_frontend_initialize(void);
extern void agp_frontend_cleanup(void);

static const drm_agp_t drm_agp = {
	&agp_free_memory,
	&agp_allocate_memory,
	&agp_bind_memory,
	&agp_unbind_memory,
	&agp_enable,
	&agp_backend_acquire,
	&agp_backend_release,
	&agp_copy_info
};

static int agp_count=0;

int agp_register_driver (struct agp_driver *drv)
{
	int ret_val;

	if (drv->dev == NULL) {
		printk (KERN_DEBUG PFX "Erk, registering with no pci_dev!\n");
		return -EINVAL;
	}

	if (agp_count==1) {
		printk (KERN_DEBUG PFX "Only one agpgart device currently supported.\n");
		return -ENODEV;
	}

	/* Grab reference on the chipset driver. */
	if (!try_module_get(drv->owner))
		return -EINVAL;

	ret_val = agp_backend_initialize(drv->dev);
	if (ret_val)
		goto err_out;

	ret_val = agp_frontend_initialize();
	if (ret_val)
		goto frontend_err;

	/* FIXME: What to do with this? */
	inter_module_register("drm_agp", THIS_MODULE, &drm_agp);

	pm_register(PM_PCI_DEV, PM_PCI_ID(agp_bridge->dev), agp_power);
	agp_count++;
	return 0;

frontend_err:
	agp_backend_cleanup();
err_out:
	agp_bridge->type = NOT_SUPPORTED;
	module_put(drv->owner);
	drv->dev = NULL;
	return ret_val;
}

int agp_unregister_driver(struct agp_driver *drv)
{
	if (drv->dev==NULL)
		return -ENODEV;

	agp_bridge->type = NOT_SUPPORTED;
	pm_unregister_all(agp_power);
	agp_frontend_cleanup();
	agp_backend_cleanup();
	inter_module_unregister("drm_agp");
	agp_count--;
	module_put(drv->owner);
	return 0;
}


int __init agp_init(void)
{
	static int already_initialised=0;

	if (already_initialised!=0)
		return 0;

	already_initialised = 1;

	memset(agp_bridge, 0, sizeof(struct agp_bridge_data));
	agp_bridge->type = NOT_SUPPORTED;

	printk(KERN_INFO "Linux agpgart interface v%d.%d (c) Dave Jones\n",
	       AGPGART_VERSION_MAJOR, AGPGART_VERSION_MINOR);
	return 0;
}

void __exit agp_exit(void)
{
	if (agp_count!=0)
		BUG();
}

#ifndef CONFIG_GART_IOMMU
module_init(agp_init);
module_exit(agp_exit);
#endif

EXPORT_SYMBOL(agp_backend_acquire);
EXPORT_SYMBOL(agp_backend_release);
EXPORT_SYMBOL_GPL(agp_register_driver);
EXPORT_SYMBOL_GPL(agp_unregister_driver);

MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_LICENSE("GPL and additional rights");

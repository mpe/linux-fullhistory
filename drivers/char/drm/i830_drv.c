/* i830_drv.c -- I810 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:56:22 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Jeff Hartmann <jhartmann@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *    Abraham vd Merwe <abraham@2d3d.co.za>
 *    Keith Whitwell <keith@tungstengraphics.com>
 */

#include <linux/config.h>
#include "drmP.h"
#include "drm.h"
#include "i830_drm.h"
#include "i830_drv.h"

#include "drm_pciids.h"

int postinit( struct drm_device *dev, unsigned long flags )
{
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;
	
	DRM_INFO( "Initialized %s %d.%d.%d %s on minor %d: %s\n",
		DRIVER_NAME,
		DRIVER_MAJOR,
		DRIVER_MINOR,
		DRIVER_PATCHLEVEL,
		DRIVER_DATE,
		dev->minor,
		pci_pretty_name(dev->pdev)
		);
	return 0;
}

static int version( drm_version_t *version )
{
	int len;

	version->version_major = DRIVER_MAJOR;
	version->version_minor = DRIVER_MINOR;
	version->version_patchlevel = DRIVER_PATCHLEVEL;
	DRM_COPY( version->name, DRIVER_NAME );
	DRM_COPY( version->date, DRIVER_DATE );
	DRM_COPY( version->desc, DRIVER_DESC );
	return 0;
}

static struct pci_device_id pciidlist[] = {
	i830_PCI_IDS
};

extern drm_ioctl_desc_t i830_ioctls[];
extern int i830_max_ioctl;

static struct drm_driver driver = {
	.driver_features = DRIVER_USE_AGP | DRIVER_REQUIRE_AGP | DRIVER_USE_MTRR | DRIVER_HAVE_DMA | DRIVER_DMA_QUEUE,
#if USE_IRQS
	.driver_features |= DRIVER_HAVE_IRQ | DRIVER_SHARED_IRQ,
#endif
	.dev_priv_size = sizeof(drm_i830_buf_priv_t),
	.pretakedown = i830_driver_pretakedown,
	.release = i830_driver_release,
	.dma_quiescent = i830_driver_dma_quiescent,
	.reclaim_buffers = i830_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
#if USE_IRQS
	.irq_preinstall = i830_driver_irq_preinstall,
	.irq_postinstall = i830_driver_irq_postinstall,
	.irq_uninstall = i830_driver_irq_uninstall,
	.irq_handler = i830_driver_irq_handler,
#endif
	.postinit = postinit,
	.version = version,
	.ioctls = i830_ioctls,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.ioctl = drm_ioctl,
		.mmap = drm_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
	},
	.pci_driver = {
		.name          = DRIVER_NAME,
		.id_table      = pciidlist,
	}

};

static int __init i830_init(void)
{
	driver.num_ioctls = i830_max_ioctl;
	return drm_init(&driver);
}

static void __exit i830_exit(void)
{
	drm_exit(&driver);
}

module_init(i830_init);
module_exit(i830_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL and additional rights");

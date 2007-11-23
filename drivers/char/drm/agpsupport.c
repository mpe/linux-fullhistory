/* agpsupport.c -- DRM support for AGP/GART backend -*- linux-c -*-
 * Created: Mon Dec 13 09:56:45 1999 by faith@precisioninsight.com
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 *
 */

#define __NO_VERSION__
#include "drmP.h"
#include <linux/module.h>

drm_agp_func_t drm_agp = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

/* The C standard says that 'void *' is not guaranteed to hold a function
   pointer, so we use this union to define a generic pointer that is
   guaranteed to hold any of the function pointers we care about. */
typedef union {
	void          (*free_memory)(agp_memory *);
	agp_memory    *(*allocate_memory)(size_t, u32);
	int           (*bind_memory)(agp_memory *, off_t);
	int           (*unbind_memory)(agp_memory *);
	void          (*enable)(u32);
	int           (*acquire)(void);
	void          (*release)(void);
	void          (*copy_info)(agp_kern_info *);
	unsigned long address;
} drm_agp_func_u;

typedef struct drm_agp_fill {
        const char     *name;
	drm_agp_func_u *f;
} drm_agp_fill_t;

static drm_agp_fill_t drm_agp_fill[] = {
	{ __MODULE_STRING(agp_free_memory),
	   (drm_agp_func_u *)&drm_agp.free_memory     },
	{ __MODULE_STRING(agp_allocate_memory), 
	   (drm_agp_func_u *)&drm_agp.allocate_memory },
	{ __MODULE_STRING(agp_bind_memory),     
	   (drm_agp_func_u *)&drm_agp.bind_memory     },
	{ __MODULE_STRING(agp_unbind_memory),   
	   (drm_agp_func_u *)&drm_agp.unbind_memory   },
	{ __MODULE_STRING(agp_enable),          
	   (drm_agp_func_u *)&drm_agp.enable          },
	{ __MODULE_STRING(agp_backend_acquire), 
	   (drm_agp_func_u *)&drm_agp.acquire         },
	{ __MODULE_STRING(agp_backend_release), 
	   (drm_agp_func_u *)&drm_agp.release         },
	{ __MODULE_STRING(agp_copy_info),       
	   (drm_agp_func_u *)&drm_agp.copy_info       },
	{ NULL, NULL }
};

int drm_agp_info(struct inode *inode, struct file *filp, unsigned int cmd,
		 unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	agp_kern_info    *kern;
	drm_agp_info_t   info;

	if (!dev->agp->acquired || !drm_agp.copy_info) return -EINVAL;

	kern                   = &dev->agp->agp_info;
	info.agp_version_major = kern->version.major;
	info.agp_version_minor = kern->version.minor;
	info.mode              = kern->mode;
	info.aperture_base     = kern->aper_base;
	info.aperture_size     = kern->aper_size * 1024 * 1024;
	info.memory_allowed    = kern->max_memory << PAGE_SHIFT;
	info.memory_used       = kern->current_memory << PAGE_SHIFT;
	info.id_vendor         = kern->device->vendor;
	info.id_device         = kern->device->device;

	copy_to_user_ret((drm_agp_info_t *)arg, &info, sizeof(info), -EFAULT);
	return 0;
}

int drm_agp_acquire(struct inode *inode, struct file *filp, unsigned int cmd,
		    unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	int              retcode;

	if (dev->agp->acquired || !drm_agp.acquire) return -EINVAL;
	if ((retcode = (*drm_agp.acquire)())) return retcode;
	dev->agp->acquired = 1;
	return 0;
}

int drm_agp_release(struct inode *inode, struct file *filp, unsigned int cmd,
		    unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;

	if (!dev->agp->acquired || !drm_agp.release) return -EINVAL;
	(*drm_agp.release)();
	dev->agp->acquired = 0;
	return 0;
	
}

int drm_agp_enable(struct inode *inode, struct file *filp, unsigned int cmd,
		   unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	drm_agp_mode_t   mode;

	if (!dev->agp->acquired || !drm_agp.enable) return -EINVAL;

	copy_from_user_ret(&mode, (drm_agp_mode_t *)arg, sizeof(mode),
			   -EFAULT);
	
	dev->agp->mode    = mode.mode;
	(*drm_agp.enable)(mode.mode);
	dev->agp->base    = dev->agp->agp_info.aper_base;
	dev->agp->enabled = 1;
	return 0;
}

int drm_agp_alloc(struct inode *inode, struct file *filp, unsigned int cmd,
		  unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	drm_agp_buffer_t request;
	drm_agp_mem_t    *entry;
	agp_memory       *memory;
	unsigned long    pages;
	u32 		 type;
	if (!dev->agp->acquired) return -EINVAL;
	copy_from_user_ret(&request, (drm_agp_buffer_t *)arg, sizeof(request),
			   -EFAULT);
	if (!(entry = drm_alloc(sizeof(*entry), DRM_MEM_AGPLISTS)))
		return -ENOMEM;
   
   	memset(entry, 0, sizeof(*entry));

	pages = (request.size + PAGE_SIZE - 1) / PAGE_SIZE;
	type = (u32) request.type;

	if (!(memory = drm_alloc_agp(pages, type))) {
		drm_free(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
		return -ENOMEM;
	}
	
	entry->handle    = (unsigned long)memory->memory;
	entry->memory    = memory;
	entry->bound     = 0;
	entry->pages     = pages;
	entry->prev      = NULL;
	entry->next      = dev->agp->memory;
	if (dev->agp->memory) dev->agp->memory->prev = entry;
	dev->agp->memory = entry;

	request.handle   = entry->handle;
        request.physical = memory->physical;

	if (copy_to_user((drm_agp_buffer_t *)arg, &request, sizeof(request))) {
		dev->agp->memory       = entry->next;
		dev->agp->memory->prev = NULL;
		drm_free_agp(memory, pages);
		drm_free(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
		return -EFAULT;
	}
	return 0;
}

static drm_agp_mem_t *drm_agp_lookup_entry(drm_device_t *dev,
					   unsigned long handle)
{
	drm_agp_mem_t *entry;

	for (entry = dev->agp->memory; entry; entry = entry->next) {
		if (entry->handle == handle) return entry;
	}
	return NULL;
}

int drm_agp_unbind(struct inode *inode, struct file *filp, unsigned int cmd,
		   unsigned long arg)
{
	drm_file_t	  *priv	 = filp->private_data;
	drm_device_t	  *dev	 = priv->dev;
	drm_agp_binding_t request;
	drm_agp_mem_t     *entry;

	if (!dev->agp->acquired) return -EINVAL;
	copy_from_user_ret(&request, (drm_agp_binding_t *)arg, sizeof(request),
			   -EFAULT);
	if (!(entry = drm_agp_lookup_entry(dev, request.handle)))
		return -EINVAL;
	if (!entry->bound) return -EINVAL;
	return drm_unbind_agp(entry->memory);
}

int drm_agp_bind(struct inode *inode, struct file *filp, unsigned int cmd,
		 unsigned long arg)
{
	drm_file_t	  *priv	 = filp->private_data;
	drm_device_t	  *dev	 = priv->dev;
	drm_agp_binding_t request;
	drm_agp_mem_t     *entry;
	int               retcode;
	int               page;
	
	if (!dev->agp->acquired || !drm_agp.bind_memory) return -EINVAL;
	copy_from_user_ret(&request, (drm_agp_binding_t *)arg, sizeof(request),
			   -EFAULT);
	if (!(entry = drm_agp_lookup_entry(dev, request.handle)))
		return -EINVAL;
	if (entry->bound) return -EINVAL;
	page = (request.offset + PAGE_SIZE - 1) / PAGE_SIZE;
	if ((retcode = drm_bind_agp(entry->memory, page))) return retcode;
	entry->bound = dev->agp->base + (page << PAGE_SHIFT);
	DRM_DEBUG("base = 0x%lx entry->bound = 0x%lx\n", 
		  dev->agp->base, entry->bound);
	return 0;
}

int drm_agp_free(struct inode *inode, struct file *filp, unsigned int cmd,
		 unsigned long arg)
{
	drm_file_t	 *priv	 = filp->private_data;
	drm_device_t	 *dev	 = priv->dev;
	drm_agp_buffer_t request;
	drm_agp_mem_t    *entry;
	
	if (!dev->agp->acquired) return -EINVAL;
	copy_from_user_ret(&request, (drm_agp_buffer_t *)arg, sizeof(request),
			   -EFAULT);
	if (!(entry = drm_agp_lookup_entry(dev, request.handle)))
		return -EINVAL;
	if (entry->bound) drm_unbind_agp(entry->memory);
   
	if (entry->prev) entry->prev->next = entry->next;
	else             dev->agp->memory  = entry->next;
	if (entry->next) entry->next->prev = entry->prev;
	drm_free_agp(entry->memory, entry->pages);
	drm_free(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
	return 0;
}

drm_agp_head_t *drm_agp_init(void)
{
	drm_agp_fill_t *fill;
	drm_agp_head_t *head         = NULL;
	int            agp_available = 1;

	for (fill = &drm_agp_fill[0]; fill->name; fill++) {
		char *n  = (char *)fill->name;
		*fill->f = (drm_agp_func_u)get_module_symbol(NULL, n);
		DRM_DEBUG("%s resolves to 0x%08lx\n", n, (*fill->f).address);
		if (!(*fill->f).address) agp_available = 0;
	}
   
	DRM_DEBUG("agp_available = %d\n", agp_available);

	if (agp_available) {
		if (!(head = drm_alloc(sizeof(*head), DRM_MEM_AGPLISTS)))
			return NULL;
		memset((void *)head, 0, sizeof(*head));
		(*drm_agp.copy_info)(&head->agp_info);
		head->memory = NULL;
		switch (head->agp_info.chipset) {
		case INTEL_GENERIC:    head->chipset = "Intel";            break;
		case INTEL_LX:         head->chipset = "Intel 440LX";      break;
		case INTEL_BX:         head->chipset = "Intel 440BX";      break;
		case INTEL_GX:         head->chipset = "Intel 440GX";      break;
		case INTEL_I810:       head->chipset = "Intel i810";       break;
		case VIA_GENERIC:      head->chipset = "VIA";              break;
		case VIA_VP3:          head->chipset = "VIA VP3";          break;
		case VIA_MVP3:         head->chipset = "VIA MVP3";         break;
		case VIA_APOLLO_PRO:   head->chipset = "VIA Apollo Pro";   break;
		case VIA_APOLLO_SUPER: head->chipset = "VIA Apollo Super"; break;
		case SIS_GENERIC:      head->chipset = "SiS";              break;
		case AMD_GENERIC:      head->chipset = "AMD";              break;
		case AMD_IRONGATE:     head->chipset = "AMD Irongate";     break;
		case ALI_GENERIC:      head->chipset = "ALi";              break;
		case ALI_M1541:        head->chipset = "ALi M1541";        break;
		default:
		}
		DRM_INFO("AGP %d.%d on %s @ 0x%08lx %dMB\n",
			 head->agp_info.version.major,
			 head->agp_info.version.minor,
			 head->chipset,
			 head->agp_info.aper_base,
			 head->agp_info.aper_size);
	}
	return head;
}

void drm_agp_uninit(void)
{
	drm_agp_fill_t *fill;
	
	for (fill = &drm_agp_fill[0]; fill->name; fill++) {
#if LINUX_VERSION_CODE >= 0x020400
		if ((*fill->f).address) put_module_symbol((*fill->f).address);
#endif
		(*fill->f).address = 0;
	}
}

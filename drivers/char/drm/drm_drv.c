/**
 * \file drm_drv.h 
 * Generic driver template
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 *
 * To use this template, you must at least define the following (samples
 * given for the MGA driver):
 *
 * \code
 * #define DRIVER_AUTHOR	"VA Linux Systems, Inc."
 *
 * #define DRIVER_NAME		"mga"
 * #define DRIVER_DESC		"Matrox G200/G400"
 * #define DRIVER_DATE		"20001127"
 *
 * #define DRIVER_MAJOR		2
 * #define DRIVER_MINOR		0
 * #define DRIVER_PATCHLEVEL	2
 *
 * #define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( mga_ioctls )
 *
 * #define drm_x		mga_##x
 * \endcode
 */

/*
 * Created: Thu Nov 23 03:10:50 2000 by gareth@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 */

#include "drmP.h"
#include "drm_core.h"

/** Ioctl table */
drm_ioctl_desc_t		  drm_ioctls[] = {
	[DRM_IOCTL_NR(DRM_IOCTL_VERSION)]       = { drm_version,     0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_UNIQUE)]    = { drm_getunique,   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAGIC)]     = { drm_getmagic,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_IRQ_BUSID)]     = { drm_irq_by_busid, 0, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAP)]       = { drm_getmap,      0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CLIENT)]    = { drm_getclient,   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_STATS)]     = { drm_getstats,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SET_VERSION)]   = { drm_setversion,  0, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_UNIQUE)]    = { drm_setunique,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_BLOCK)]         = { drm_noop,        1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNBLOCK)]       = { drm_noop,        1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AUTH_MAGIC)]    = { drm_authmagic,   1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_MAP)]       = { drm_addmap,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_MAP)]        = { drm_rmmap,       1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_SAREA_CTX)] = { drm_setsareactx, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_SAREA_CTX)] = { drm_getsareactx, 1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_CTX)]       = { drm_addctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_CTX)]        = { drm_rmctx,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MOD_CTX)]       = { drm_modctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CTX)]       = { drm_getctx,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SWITCH_CTX)]    = { drm_switchctx,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_NEW_CTX)]       = { drm_newctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RES_CTX)]       = { drm_resctx,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_DRAW)]      = { drm_adddraw,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_DRAW)]       = { drm_rmdraw,      1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_LOCK)]	        = { drm_lock,        1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNLOCK)]        = { drm_unlock,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_FINISH)]        = { drm_noop,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_BUFS)]      = { drm_addbufs,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MARK_BUFS)]     = { drm_markbufs,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_INFO_BUFS)]     = { drm_infobufs,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MAP_BUFS)]      = { drm_mapbufs,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FREE_BUFS)]     = { drm_freebufs,    1, 0 },
	/* The DRM_IOCTL_DMA ioctl should be defined by the driver. */

	[DRM_IOCTL_NR(DRM_IOCTL_CONTROL)]       = { drm_control,     1, 1 },

#if __OS_HAS_AGP
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ACQUIRE)]   = { drm_agp_acquire, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_RELEASE)]   = { drm_agp_release, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ENABLE)]    = { drm_agp_enable,  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_INFO)]      = { drm_agp_info,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ALLOC)]     = { drm_agp_alloc,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_FREE)]      = { drm_agp_free,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_BIND)]      = { drm_agp_bind,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_UNBIND)]    = { drm_agp_unbind,  1, 1 },
#endif

	[DRM_IOCTL_NR(DRM_IOCTL_SG_ALLOC)]      = { drm_sg_alloc,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_SG_FREE)]       = { drm_sg_free,     1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_WAIT_VBLANK)]   = { drm_wait_vblank, 0, 0 },
};

#define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( drm_ioctls )

/**
 * Take down the DRM device.
 *
 * \param dev DRM device structure.
 *
 * Frees every resource in \p dev.
 *
 * \sa drm_device and setup().
 */
int drm_takedown( drm_device_t *dev )
{
	drm_magic_entry_t *pt, *next;
	drm_map_t *map;
	drm_map_list_t *r_list;
	struct list_head *list, *list_next;
	drm_vma_entry_t *vma, *vma_next;
	int i;

	DRM_DEBUG( "\n" );

	if (dev->driver->pretakedown)
	  dev->driver->pretakedown(dev);

	if ( dev->irq_enabled ) drm_irq_uninstall( dev );

	down( &dev->struct_sem );
	del_timer( &dev->timer );

	if ( dev->devname ) {
		drm_free( dev->devname, strlen( dev->devname ) + 1,
			   DRM_MEM_DRIVER );
		dev->devname = NULL;
	}

	if ( dev->unique ) {
		drm_free( dev->unique, strlen( dev->unique ) + 1,
			   DRM_MEM_DRIVER );
		dev->unique = NULL;
		dev->unique_len = 0;
	}
				/* Clear pid list */
	for ( i = 0 ; i < DRM_HASH_SIZE ; i++ ) {
		for ( pt = dev->magiclist[i].head ; pt ; pt = next ) {
			next = pt->next;
			drm_free( pt, sizeof(*pt), DRM_MEM_MAGIC );
		}
		dev->magiclist[i].head = dev->magiclist[i].tail = NULL;
	}

				/* Clear AGP information */
	if (drm_core_has_AGP(dev) && dev->agp) {
		drm_agp_mem_t *entry;
		drm_agp_mem_t *nexte;

				/* Remove AGP resources, but leave dev->agp
                                   intact until drv_cleanup is called. */
		for ( entry = dev->agp->memory ; entry ; entry = nexte ) {
			nexte = entry->next;
			if ( entry->bound ) drm_unbind_agp( entry->memory );
			drm_free_agp( entry->memory, entry->pages );
			drm_free( entry, sizeof(*entry), DRM_MEM_AGPLISTS );
		}
		dev->agp->memory = NULL;

		if ( dev->agp->acquired ) drm_agp_do_release();

		dev->agp->acquired = 0;
		dev->agp->enabled  = 0;
	}

				/* Clear vma list (only built for debugging) */
	if ( dev->vmalist ) {
		for ( vma = dev->vmalist ; vma ; vma = vma_next ) {
			vma_next = vma->next;
			drm_free( vma, sizeof(*vma), DRM_MEM_VMAS );
		}
		dev->vmalist = NULL;
	}

	if( dev->maplist ) {
		list_for_each_safe( list, list_next, &dev->maplist->head ) {
			r_list = (drm_map_list_t *)list;

			if ( ( map = r_list->map ) ) {
				switch ( map->type ) {
				case _DRM_REGISTERS:
				case _DRM_FRAME_BUFFER:
					if (drm_core_has_MTRR(dev)) {
						if ( map->mtrr >= 0 ) {
							int retcode;
							retcode = mtrr_del( map->mtrr,
									    map->offset,
									    map->size );
							DRM_DEBUG( "mtrr_del=%d\n", retcode );
						}
					}
					drm_ioremapfree( map->handle, map->size, dev );
					break;
				case _DRM_SHM:
					vfree(map->handle);
					break;

				case _DRM_AGP:
					/* Do nothing here, because this is all
					 * handled in the AGP/GART driver.
					 */
					break;
				case _DRM_SCATTER_GATHER:
					/* Handle it */
					if (drm_core_check_feature(dev, DRIVER_SG) && dev->sg) {
						drm_sg_cleanup(dev->sg);
						dev->sg = NULL;
					}
					break;
				}
				drm_free(map, sizeof(*map), DRM_MEM_MAPS);
			}
			list_del( list );
			drm_free(r_list, sizeof(*r_list), DRM_MEM_MAPS);
 		}
		drm_free(dev->maplist, sizeof(*dev->maplist), DRM_MEM_MAPS);
		dev->maplist = NULL;
 	}

	if (drm_core_check_feature(dev, DRIVER_DMA_QUEUE) && dev->queuelist ) {
		for ( i = 0 ; i < dev->queue_count ; i++ ) {
			if ( dev->queuelist[i] ) {
				drm_free( dev->queuelist[i],
					  sizeof(*dev->queuelist[0]),
					  DRM_MEM_QUEUES );
				dev->queuelist[i] = NULL;
			}
		}
		drm_free( dev->queuelist,
			  dev->queue_slots * sizeof(*dev->queuelist),
			  DRM_MEM_QUEUES );
		dev->queuelist = NULL;
	}
	dev->queue_count = 0;

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
		drm_dma_takedown( dev );

	if ( dev->lock.hw_lock ) {
		dev->sigdata.lock = dev->lock.hw_lock = NULL; /* SHM removed */
		dev->lock.filp = NULL;
		wake_up_interruptible( &dev->lock.lock_queue );
	}
	up( &dev->struct_sem );

	return 0;
}



/**
 * Module initialization. Called via init_module at module load time, or via
 * linux/init/main.c (this is not currently supported).
 *
 * \return zero on success or a negative number on failure.
 *
 * Initializes an array of drm_device structures, and attempts to
 * initialize all available devices, using consecutive minors, registering the
 * stubs and initializing the AGP device.
 * 
 * Expands the \c DRIVER_PREINIT and \c DRIVER_POST_INIT macros before and
 * after the initialization for driver customization.
 */
int drm_init( struct drm_driver *driver )
{
	struct pci_dev *pdev = NULL;
	struct pci_device_id *pid;
	int i;

	DRM_DEBUG( "\n" );

	drm_mem_init();

	for (i=0; driver->pci_driver.id_table[i].vendor != 0; i++) {
		pid = (struct pci_device_id *)&driver->pci_driver.id_table[i];
		
		pdev=NULL;
		/* pass back in pdev to account for multiple identical cards */		
		while ((pdev = pci_get_subsys(pid->vendor, pid->device, pid->subvendor, pid->subdevice, pdev)) != NULL) {
			/* stealth mode requires a manual probe */
			pci_dev_get(pdev);
			drm_probe(pdev, pid, driver);
		}
	}
	return 0;
}
EXPORT_SYMBOL(drm_init);

/**
 * Called via cleanup_module() at module unload time.
 *
 * Cleans up all DRM device, calling takedown().
 * 
 * \sa drm_init().
 */
static void drm_cleanup( drm_device_t *dev )
{
	DRM_DEBUG( "\n" );

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}

	drm_takedown( dev );	

	drm_ctxbitmap_cleanup( dev );
	
	if (drm_core_has_MTRR(dev) && drm_core_has_AGP(dev) &&
	    dev->agp && dev->agp->agp_mtrr >= 0) {
		int retval;
		retval = mtrr_del( dev->agp->agp_mtrr,
				   dev->agp->agp_info.aper_base,
				   dev->agp->agp_info.aper_size*1024*1024 );
		DRM_DEBUG( "mtrr_del=%d\n", retval );
	}
	
	if (drm_core_has_AGP(dev) && dev->agp ) {
		drm_free( dev->agp, sizeof(*dev->agp), DRM_MEM_AGPLISTS );
		dev->agp = NULL;
	}

	if (dev->driver->postcleanup)
		dev->driver->postcleanup(dev);
	
	if ( drm_put_minor(dev) )
		DRM_ERROR( "Cannot unload module\n" );
}

void drm_exit (struct drm_driver *driver)
{
	int i;
	drm_device_t *dev = NULL;
	drm_minor_t *minor;
	
	DRM_DEBUG( "\n" );

	for (i = 0; i < drm_cards_limit; i++) {
		minor = &drm_minors[i];
		if (!minor->dev)
			continue;
		if (minor->dev->driver!=driver)
			continue;

		dev = minor->dev;
		
	}
	if (dev) {
		/* release the pci driver */
		if (dev->pdev)
			pci_dev_put(dev->pdev);
		drm_cleanup(dev);
	}
	
	DRM_INFO( "Module unloaded\n" );
}
EXPORT_SYMBOL(drm_exit);

/** File operations structure */
static struct file_operations drm_stub_fops = {
	.owner = THIS_MODULE,
	.open  = drm_stub_open
};

static int __init drm_core_init(void)
{
	int ret = -ENOMEM;
	
	drm_cards_limit = (drm_cards_limit < DRM_MAX_MINOR + 1 ? drm_cards_limit : DRM_MAX_MINOR + 1);
	drm_minors = drm_calloc(drm_cards_limit,
				sizeof(*drm_minors), DRM_MEM_STUB);
	if(!drm_minors) 
		goto err_p1;
	
	if (register_chrdev(DRM_MAJOR, "drm", &drm_stub_fops))
		goto err_p1;
	
	drm_class = drm_sysfs_create(THIS_MODULE, "drm");
	if (IS_ERR(drm_class)) {
		printk (KERN_ERR "DRM: Error creating drm class.\n");
		ret = PTR_ERR(drm_class);
		goto err_p2;
	}

	drm_proc_root = create_proc_entry("dri", S_IFDIR, NULL);
	if (!drm_proc_root) {
		DRM_ERROR("Cannot create /proc/dri\n");
		ret = -1;
		goto err_p3;
	}
		
	DRM_INFO( "Initialized %s %d.%d.%d %s\n",
		DRIVER_NAME,
		DRIVER_MAJOR,
		DRIVER_MINOR,
		DRIVER_PATCHLEVEL,
		DRIVER_DATE
		);
	return 0;
err_p3:
	drm_sysfs_destroy(drm_class);
err_p2:
	unregister_chrdev(DRM_MAJOR, "drm");
	drm_free(drm_minors, sizeof(*drm_minors) * drm_cards_limit, DRM_MEM_STUB);
err_p1:	
	return ret;
}

static void __exit drm_core_exit (void)
{
	remove_proc_entry("dri", NULL);
	drm_sysfs_destroy(drm_class);

	unregister_chrdev(DRM_MAJOR, "drm");

	drm_free(drm_minors, sizeof(*drm_minors) *
				drm_cards_limit, DRM_MEM_STUB);
}


module_init( drm_core_init );
module_exit( drm_core_exit );


/**
 * Get version information
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_version structure.
 * \return zero on success or negative number on failure.
 *
 * Fills in the version information in \p arg.
 */
int drm_version( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_version_t __user *argp = (void __user *)arg;
	drm_version_t version;
	int ret;

	if ( copy_from_user( &version, argp, sizeof(version) ) )
		return -EFAULT;

	/* version is a required function to return the personality module version */
	if ((ret = dev->driver->version(&version)))
		return ret;
		
	if ( copy_to_user( argp, &version, sizeof(version) ) )
		return -EFAULT;
	return 0;
}



/** 
 * Called whenever a process performs an ioctl on /dev/drm.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument.
 * \return zero on success or negative number on failure.
 *
 * Looks up the ioctl function in the ::ioctls table, checking for root
 * previleges if so required, and dispatches to the respective function.
 */
int drm_ioctl( struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ioctl_desc_t *ioctl;
	drm_ioctl_t *func;
	unsigned int nr = DRM_IOCTL_NR(cmd);
	int retcode = -EINVAL;

	atomic_inc( &dev->ioctl_count );
	atomic_inc( &dev->counts[_DRM_STAT_IOCTLS] );
	++priv->ioctl_count;

	DRM_DEBUG( "pid=%d, cmd=0x%02x, nr=0x%02x, dev 0x%lx, auth=%d\n",
		   current->pid, cmd, nr, (long)old_encode_dev(dev->device), 
		   priv->authenticated );
	
	if (nr < DRIVER_IOCTL_COUNT)
		ioctl = &drm_ioctls[nr];
	else if ((nr >= DRM_COMMAND_BASE) && (nr < DRM_COMMAND_BASE + dev->driver->num_ioctls))
		ioctl = &dev->driver->ioctls[nr - DRM_COMMAND_BASE];
	else
		goto err_i1;
	
	func = ioctl->func;
	/* is there a local override? */
	if ((nr == DRM_IOCTL_NR(DRM_IOCTL_DMA)) && dev->driver->dma_ioctl)
		func = dev->driver->dma_ioctl;
	
	if ( !func ) {
		DRM_DEBUG( "no function\n" );
		retcode = -EINVAL;
	} else if ( ( ioctl->root_only && !capable( CAP_SYS_ADMIN ) )||
		    ( ioctl->auth_needed && !priv->authenticated ) ) {
		retcode = -EACCES;
	} else {
		retcode = func( inode, filp, cmd, arg );
	}
	
err_i1:
	atomic_dec( &dev->ioctl_count );
	if (retcode) DRM_DEBUG( "ret = %x\n", retcode);
	return retcode;
}
EXPORT_SYMBOL(drm_ioctl);


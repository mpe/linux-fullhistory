#include <linux/config.h>
#include "drmP.h"

/* Misc. support (init.c) */
EXPORT_SYMBOL(drm_flags);
EXPORT_SYMBOL(drm_parse_options);
EXPORT_SYMBOL(drm_cpu_valid);

/* Device support (fops.c) */
EXPORT_SYMBOL(drm_open_helper);
EXPORT_SYMBOL(drm_flush);
EXPORT_SYMBOL(drm_release);
EXPORT_SYMBOL(drm_fasync);
EXPORT_SYMBOL(drm_read);
EXPORT_SYMBOL(drm_write_string);
EXPORT_SYMBOL(drm_poll);

/* Mapping support (vm.c) */
#if LINUX_VERSION_CODE < 0x020317
EXPORT_SYMBOL(drm_vm_nopage);
EXPORT_SYMBOL(drm_vm_shm_nopage);
EXPORT_SYMBOL(drm_vm_dma_nopage);
#else
/* Return type changed in 2.3.23 */
EXPORT_SYMBOL(drm_vm_nopage);
EXPORT_SYMBOL(drm_vm_shm_nopage);
EXPORT_SYMBOL(drm_vm_dma_nopage);
#endif

EXPORT_SYMBOL(drm_vm_open);
EXPORT_SYMBOL(drm_vm_close);
EXPORT_SYMBOL(drm_mmap_dma);
EXPORT_SYMBOL(drm_mmap);

/* Proc support (proc.c) */
EXPORT_SYMBOL(drm_proc_init);
EXPORT_SYMBOL(drm_proc_cleanup);

/* Memory management support (memory.c) */
EXPORT_SYMBOL(drm_mem_init);
EXPORT_SYMBOL(drm_mem_info);
EXPORT_SYMBOL(drm_alloc);
EXPORT_SYMBOL(drm_realloc);
EXPORT_SYMBOL(drm_strdup);
EXPORT_SYMBOL(drm_strfree);
EXPORT_SYMBOL(drm_free);
EXPORT_SYMBOL(drm_alloc_pages);
EXPORT_SYMBOL(drm_free_pages);
EXPORT_SYMBOL(drm_ioremap);
EXPORT_SYMBOL(drm_ioremapfree);
#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
EXPORT_SYMBOL(drm_alloc_agp);
EXPORT_SYMBOL(drm_free_agp);
EXPORT_SYMBOL(drm_bind_agp);
EXPORT_SYMBOL(drm_unbind_agp);
#endif

/* Buffer management support (bufs.c) */
EXPORT_SYMBOL(drm_order);
EXPORT_SYMBOL(drm_addmap);
EXPORT_SYMBOL(drm_addbufs);
EXPORT_SYMBOL(drm_infobufs);
EXPORT_SYMBOL(drm_markbufs);
EXPORT_SYMBOL(drm_freebufs);
EXPORT_SYMBOL(drm_mapbufs);

/* Buffer list management support (lists.c) */
EXPORT_SYMBOL(drm_waitlist_create);
EXPORT_SYMBOL(drm_waitlist_destroy);
EXPORT_SYMBOL(drm_waitlist_put);
EXPORT_SYMBOL(drm_waitlist_get);
EXPORT_SYMBOL(drm_freelist_create);
EXPORT_SYMBOL(drm_freelist_destroy);
EXPORT_SYMBOL(drm_freelist_put);
EXPORT_SYMBOL(drm_freelist_get);

/* DMA support (gen_dma.c) */
EXPORT_SYMBOL(drm_dma_setup);
EXPORT_SYMBOL(drm_dma_takedown);
EXPORT_SYMBOL(drm_free_buffer);
EXPORT_SYMBOL(drm_reclaim_buffers);
EXPORT_SYMBOL(drm_context_switch);
EXPORT_SYMBOL(drm_context_switch_complete);
EXPORT_SYMBOL(drm_clear_next_buffer);
EXPORT_SYMBOL(drm_select_queue);
EXPORT_SYMBOL(drm_dma_enqueue);
EXPORT_SYMBOL(drm_dma_get_buffers);
#if DRM_DMA_HISTOGRAM
EXPORT_SYMBOL(drm_histogram_slot);
EXPORT_SYMBOL(drm_histogram_compute);
#endif

/* Misc. IOCTL support (ioctl.c) */
EXPORT_SYMBOL(drm_irq_busid);
EXPORT_SYMBOL(drm_getunique);
EXPORT_SYMBOL(drm_setunique);

/* Context IOCTL support (context.c) */
EXPORT_SYMBOL(drm_resctx);
EXPORT_SYMBOL(drm_addctx);
EXPORT_SYMBOL(drm_modctx);
EXPORT_SYMBOL(drm_getctx);
EXPORT_SYMBOL(drm_switchctx);
EXPORT_SYMBOL(drm_newctx);
EXPORT_SYMBOL(drm_rmctx);

/* Drawable IOCTL support (drawable.c) */
EXPORT_SYMBOL(drm_adddraw);
EXPORT_SYMBOL(drm_rmdraw);

/* Authentication IOCTL support (auth.c) */
EXPORT_SYMBOL(drm_add_magic);
EXPORT_SYMBOL(drm_remove_magic);
EXPORT_SYMBOL(drm_getmagic);
EXPORT_SYMBOL(drm_authmagic);

/* Locking IOCTL support (lock.c) */
EXPORT_SYMBOL(drm_block);
EXPORT_SYMBOL(drm_unblock);
EXPORT_SYMBOL(drm_lock_take);
EXPORT_SYMBOL(drm_lock_transfer);
EXPORT_SYMBOL(drm_lock_free);
EXPORT_SYMBOL(drm_finish);
EXPORT_SYMBOL(drm_flush_unblock);
EXPORT_SYMBOL(drm_flush_block_and_flush);

/* Context Bitmap support (ctxbitmap.c) */
EXPORT_SYMBOL(drm_ctxbitmap_init);
EXPORT_SYMBOL(drm_ctxbitmap_cleanup);
EXPORT_SYMBOL(drm_ctxbitmap_next);
EXPORT_SYMBOL(drm_ctxbitmap_free);

/* AGP/GART support (agpsupport.c) */
#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
EXPORT_SYMBOL(drm_agp);
EXPORT_SYMBOL(drm_agp_init);
EXPORT_SYMBOL(drm_agp_uninit);
EXPORT_SYMBOL(drm_agp_acquire);
EXPORT_SYMBOL(drm_agp_release);
EXPORT_SYMBOL(drm_agp_enable);
EXPORT_SYMBOL(drm_agp_info);
EXPORT_SYMBOL(drm_agp_alloc);
EXPORT_SYMBOL(drm_agp_free);
EXPORT_SYMBOL(drm_agp_unbind);
EXPORT_SYMBOL(drm_agp_bind);
#endif

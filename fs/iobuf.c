/*
 * iobuf.c
 *
 * Keep track of the general-purpose IO-buffer structures used to track
 * abstract kernel-space io buffers.
 * 
 */

#include <linux/iobuf.h>
#include <linux/malloc.h>
#include <linux/slab.h>

static kmem_cache_t *kiobuf_cachep;

/*
 * The default IO completion routine for kiobufs: just wake up
 * the kiobuf, nothing more.  
 */

void simple_wakeup_kiobuf(struct kiobuf *kiobuf)
{
	wake_up(&kiobuf->wait_queue);
}


void __init kiobuf_init(void)
{
	kiobuf_cachep =  kmem_cache_create("kiobuf",
					   sizeof(struct kiobuf),
					   0,
					   SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!kiobuf_cachep)
		panic("Cannot create kernel iobuf cache\n");
}


int alloc_kiovec(int nr, struct kiobuf **bufp)
{
	int i;
	struct kiobuf *iobuf;
	
	for (i = 0; i < nr; i++) {
		iobuf = kmem_cache_alloc(kiobuf_cachep, SLAB_KERNEL);
		if (!iobuf) {
			free_kiovec(i, bufp);
			return -ENOMEM;
		}
		
		memset(iobuf, 0, sizeof(*iobuf));
		init_waitqueue_head(&iobuf->wait_queue);
		iobuf->end_io = simple_wakeup_kiobuf;
		iobuf->array_len = KIO_STATIC_PAGES;
		iobuf->pagelist  = iobuf->page_array;
		iobuf->maplist   = iobuf->map_array;
		*bufp++ = iobuf;
	}
	
	return 0;
}

void free_kiovec(int nr, struct kiobuf **bufp) 
{
	int i;
	struct kiobuf *iobuf;
	
	for (i = 0; i < nr; i++) {
		iobuf = bufp[i];
		if (iobuf->array_len > KIO_STATIC_PAGES) {
			kfree (iobuf->pagelist);
			kfree (iobuf->maplist);
		}
		kmem_cache_free(kiobuf_cachep, bufp[i]);
	}
}

int expand_kiobuf(struct kiobuf *iobuf, int wanted)
{
	unsigned long *	pagelist;
	struct page ** maplist;
	
	if (iobuf->array_len >= wanted)
		return 0;
	
	pagelist = (unsigned long *) 
		kmalloc(wanted * sizeof(unsigned long), GFP_KERNEL);
	if (!pagelist)
		return -ENOMEM;
	
	maplist = (struct page **) 
		kmalloc(wanted * sizeof(struct page **), GFP_KERNEL);
	if (!maplist) {
		kfree(pagelist);
		return -ENOMEM;
	}

	/* Did it grow while we waited? */
	if (iobuf->array_len >= wanted) {
		kfree(pagelist);
		kfree(maplist);
		return 0;
	}
	
	memcpy (pagelist, iobuf->pagelist, wanted * sizeof(unsigned long));
	memcpy (maplist,  iobuf->maplist,   wanted * sizeof(struct page **));

	if (iobuf->array_len > KIO_STATIC_PAGES) {
		kfree (iobuf->pagelist);
		kfree (iobuf->maplist);
	}
	
	iobuf->pagelist  = pagelist;
	iobuf->maplist   = maplist;
	iobuf->array_len = wanted;
	return 0;
}


void kiobuf_wait_for_io(struct kiobuf *kiobuf)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	add_wait_queue(&kiobuf->wait_queue, &wait);
repeat:
	run_task_queue(&tq_disk);
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	if (atomic_read(&kiobuf->io_count) != 0) {
		schedule();
		goto repeat;
	}
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&kiobuf->wait_queue, &wait);
}




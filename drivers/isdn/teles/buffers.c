/* $Id: buffers.c,v 1.3 1996/05/31 00:56:53 fritz Exp $
 *
 * $Log: buffers.c,v $
 * Revision 1.3  1996/05/31 00:56:53  fritz
 * removed cli() from BufPoolAdd, since it is called
 * with interrupts off anyway.
 *
 * Revision 1.2  1996/04/29 22:48:14  fritz
 * Removed compatibility-macros. No longer needed.
 *
 * Revision 1.1  1996/04/13 10:19:28  fritz
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "teles.h"
#include <linux/mm.h>
#include <linux/malloc.h>


void
BufPoolInit(struct BufPool *bp, int order, int bpps,
	    int maxpages)
{
#ifdef DEBUG_MAGIC
	generateerror
	    bp->magic = 010167;
#endif

#if 0
	printk(KERN_DEBUG "BufPoolInit bp %x\n", bp);
#endif

	bp->freelist = NULL;
	bp->pageslist = NULL;
	bp->pageorder = order;
	bp->pagescount = 0;
	bp->bpps = bpps;
	bp->bufsize = BUFFER_SIZE(order, bpps);
	bp->maxpages = maxpages;
}

int
BufPoolAdd(struct BufPool *bp, int priority)
{
	struct Pages   *ptr;
	byte           *bptr;
	int             i;
	struct BufHeader *bh = NULL, *prev, *first;

#if 0
	printk(KERN_DEBUG "BufPoolAdd bp %x\n", bp);
#endif

	ptr = (struct Pages *) __get_free_pages(priority, bp->pageorder, 0);
	if (!ptr) {
		printk(KERN_WARNING "BufPoolAdd couldn't get pages!\n");
		return (-1);
	}
#if 0
	printk(KERN_DEBUG "Order %d pages allocated at %x\n", bp->pageorder, ptr);
#endif

	ptr->next = bp->pageslist;
	bp->pageslist = ptr;
	bp->pagescount++;

	bptr = (byte *) ptr + sizeof(struct Pages *);

	i = bp->bpps;
	first = (struct BufHeader *) bptr;
	prev = NULL;
	while (i--) {
		bh = (struct BufHeader *) bptr;
#ifdef DEBUG_MAGIC
		bh->magic = 020167;
#endif
		bh->next = prev;
		prev = bh;
		bh->bp = bp;
		bptr += PART_SIZE(bp->pageorder, bp->bpps);
	}

	first->next = bp->freelist;
	bp->freelist = bh;
	return (0);
}

void
BufPoolFree(struct BufPool *bp)
{
	struct Pages   *p;

#if 0
	printk(KERN_DEBUG "BufPoolFree bp %x\n", bp);
#endif

	while (bp->pagescount--) {
		p = bp->pageslist->next;
		free_pages((unsigned long) bp->pageslist, bp->pageorder);
#if 0
		printk(KERN_DEBUG "Free pages %x order %d\n", bp->pageslist, bp->pageorder);
#endif
		bp->pageslist = p;
	}
}

int
BufPoolGet(struct BufHeader **bh,
	   struct BufPool *bp, int priority, void *heldby, int where)
{
	long            flags;
	int             i;

#ifdef DEBUG_MAGIC
	if (bp->magic != 010167) {
		printk(KERN_DEBUG "BufPoolGet: not a BufHeader\n");
		return (-1);
	}
#endif

	save_flags(flags);
	cli();
	i = 0;
	while (!0) {
		if (bp->freelist) {
			*bh = bp->freelist;
			bp->freelist = bp->freelist->next;
			(*bh)->heldby = heldby;
			(*bh)->where = where;
			restore_flags(flags);
			return (0);
		}
		if ((i == 0) && (bp->pagescount < bp->maxpages)) {
                        if (BufPoolAdd(bp, priority)) {
                                restore_flags(flags);
                                return -1;
                        }
			i++;
		} else {
			*bh = NULL;
			restore_flags(flags);
			return (-1);
		}
	}

}

void
BufPoolRelease(struct BufHeader *bh)
{
	struct BufPool *bp;
	long            flags;

#ifdef DEBUG_MAGIC
	if (bh->magic != 020167) {
		printk(KERN_DEBUG "BufPoolRelease: not a BufHeader\n");
		printk(KERN_DEBUG "called from %x\n", __builtin_return_address(0));
		return;
	}
#endif

	bp = bh->bp;

#ifdef DEBUG_MAGIC
	if (bp->magic != 010167) {
		printk(KERN_DEBUG "BufPoolRelease: not a BufPool\n");
		return;
	}
#endif

	save_flags(flags);
	cli();
	bh->next = bp->freelist;
	bp->freelist = bh;
	restore_flags(flags);
}

void
BufQueueLink(struct BufQueue *bq,
	     struct BufHeader *bh)
{
	unsigned long   flags;

	save_flags(flags);
	cli();
	if (!bq->head)
		bq->head = bh;
	if (bq->tail)
		bq->tail->next = bh;
	bq->tail = bh;
	bh->next = NULL;
	restore_flags(flags);
}

void
BufQueueLinkFront(struct BufQueue *bq,
		  struct BufHeader *bh)
{
	unsigned long   flags;

	save_flags(flags);
	cli();
	bh->next = bq->head;
	bq->head = bh;
	if (!bq->tail)
		bq->tail = bh;
	restore_flags(flags);
}

int
BufQueueUnlink(struct BufHeader **bh, struct BufQueue *bq)
{
	long            flags;

	save_flags(flags);
	cli();

	if (bq->head) {
		if (bq->tail == bq->head)
			bq->tail = NULL;
		*bh = bq->head;
		bq->head = (*bh)->next;
		restore_flags(flags);
		return (0);
	} else {
		restore_flags(flags);
		return (-1);
	}
}

void
BufQueueInit(struct BufQueue *bq)
{
#ifdef DEBUG_MAGIC
	bq->magic = 030167;
#endif
	bq->head = NULL;
	bq->tail = NULL;
}

void
BufQueueRelease(struct BufQueue *bq)
{
	struct BufHeader *bh;

	while (bq->head) {
		BufQueueUnlink(&bh, bq);
		BufPoolRelease(bh);
	}
}

int
BufQueueLength(struct BufQueue *bq)
{
	int             i = 0;
	struct BufHeader *bh;

	bh = bq->head;
	while (bh) {
		i++;
		bh = bh->next;
	}
	return (i);
}

void
BufQueueDiscard(struct BufQueue *q, int pr, void *heldby,
		int releasetoo)
{
	long            flags;
	struct BufHeader *sp;

	save_flags(flags);
	cli();

	while (!0) {
		sp = q->head;
		if (!sp)
			break;
		if ((sp->primitive == pr) && (sp->heldby == heldby)) {
			q->head = sp->next;
			if (q->tail == sp)
				q->tail = NULL;
			if (releasetoo)
				BufPoolRelease(sp);
		} else
			break;
	}

	sp = q->head;
	if (sp)
		while (sp->next) {
			if ((sp->next->primitive == pr) && (sp->next->heldby == heldby)) {
				if (q->tail == sp->next)
					q->tail = sp;
				if (releasetoo)
					BufPoolRelease(sp->next);
				sp->next = sp->next->next;
			} else
				sp = sp->next;
		}
	restore_flags(flags);
}

void
Sfree(byte * ptr)
{
#if 0
	printk(KERN_DEBUG "Sfree %x\n", ptr);
#endif
	kfree(ptr);
}

byte           *
Smalloc(int size, int pr, char *why)
{
	byte           *p;

	p = (byte *) kmalloc(size, pr);
#if 0
	printk(KERN_DEBUG "Smalloc %s size %d res %x\n", why, size, p);
#endif
	return (p);
}

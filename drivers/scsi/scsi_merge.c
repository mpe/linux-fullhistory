/*
 *  scsi_merge.c Copyright (C) 1999 Eric Youngdale
 *
 *  SCSI queueing library.
 *      Initial versions: Eric Youngdale (eric@andante.org).
 *                        Based upon conversations with large numbers
 *                        of people at Linux Expo.
 */

/*
 * This file contains queue management functions that are used by SCSI.
 * Typically this is used for several purposes.   First, we need to ensure
 * that commands do not grow so large that they cannot be handled all at
 * once by a host adapter.   The various flavors of merge functions included
 * here serve this purpose.
 *
 * Note that it would be quite trivial to allow the low-level driver the
 * flexibility to define it's own queue handling functions.  For the time
 * being, the hooks are not present.   Right now we are just using the
 * data in the host template as an indicator of how we should be handling
 * queues, and we select routines that are optimized for that purpose.
 *
 * Some hosts do not impose any restrictions on the size of a request.
 * In such cases none of the merge functions in this file are called,
 * and we allow ll_rw_blk to merge requests in the default manner.
 * This isn't guaranteed to be optimal, but it should be pretty darned
 * good.   If someone comes up with ideas of better ways of managing queues
 * to improve on the default behavior, then certainly fit it into this
 * scheme in whatever manner makes the most sense.   Please note that
 * since each device has it's own queue, we have considerable flexibility
 * in queue management.
 */

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>


#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include "hosts.h"
#include "constants.h"
#include <scsi/scsi_ioctl.h>

#ifdef CONFIG_SCSI_DEBUG_QUEUES
/*
 * Enable a bunch of additional consistency checking.   Turn this off
 * if you are benchmarking.
 */

static int dump_stats(struct request *req,
		      int use_clustering,
		      int dma_host,
		      int segments)
{
	struct buffer_head *bh;

	/*
	 * Dump the information that we have.  We know we have an
	 * inconsistency.
	 */
	printk("nr_segments is %lx\n", req->nr_segments);
	printk("counted segments is %x\n", segments);
	printk("Flags %d %d\n", use_clustering, dma_host);
	for (bh = req->bh; bh->b_reqnext != NULL; bh = bh->b_reqnext) 
	{
		printk("Segment 0x%p, blocks %d, addr 0x%lx\n",
		       bh,
		       bh->b_size >> 9,
		       virt_to_phys(bh->b_data - 1));
	}
	panic("Ththththaats all folks.  Too dangerous to continue.\n");
}


/*
 * Simple sanity check that we will use for the first go around
 * in order to ensure that we are doing the counting correctly.
 * This can be removed for optimization.
 */
#define SANITY_CHECK(req, _CLUSTER, _DMA)				\
    if( req->nr_segments != __count_segments(req, _CLUSTER, _DMA) )	\
    {									\
	__label__ here;							\
here:									\
	printk("Incorrect segment count at 0x%p", &&here);		\
	dump_stats(req, _CLUSTER, _DMA, __count_segments(req, _CLUSTER, _DMA)); \
    }
#else
#define SANITY_CHECK(req, _CLUSTER, _DMA)
#endif

/*
 * FIXME(eric) - the original disk code disabled clustering for MOD
 * devices.  I have no idea why we thought this was a good idea - my
 * guess is that it was an attempt to limit the size of requests to MOD
 * devices.
 */
#define CLUSTERABLE_DEVICE(SH,SD) (SH->use_clustering && \
				   SD->type != TYPE_MOD)

/*
 * This entire source file deals with the new queueing code.
 */

/*
 * Function:    __count_segments()
 *
 * Purpose:     Prototype for queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              use_clustering - 1 if this host wishes to use clustering
 *              dma_host - 1 if this host has ISA DMA issues (bus doesn't
 *                      expose all of the address lines, so that DMA cannot
 *                      be done from an arbitrary address).
 *
 * Returns:     Count of the number of SG segments for the request.
 *
 * Lock status: 
 *
 * Notes:       This is only used for diagnostic purposes.
 */
__inline static int __count_segments(struct request *req,
				     int use_clustering,
				     int dma_host)
{
	int ret = 1;
	struct buffer_head *bh;

	for (bh = req->bh; bh->b_reqnext != NULL; bh = bh->b_reqnext) {
		if (use_clustering) {
			/* 
			 * See if we can do this without creating another
			 * scatter-gather segment.  In the event that this is a
			 * DMA capable host, make sure that a segment doesn't span
			 * the DMA threshold boundary.  
			 */
			if (dma_host &&
			    virt_to_phys(bh->b_data - 1) == ISA_DMA_THRESHOLD) {
				ret++;
			} else if (CONTIGUOUS_BUFFERS(bh, bh->b_reqnext)) {
				/*
				 * This one is OK.  Let it go.
				 */
				continue;
			}
			ret++;
		} else {
			ret++;
		}
	}
	return ret;
}

/*
 * Function:    __scsi_merge_fn()
 *
 * Purpose:     Prototype for queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              bh      - Block which we may wish to merge into request
 *              use_clustering - 1 if this host wishes to use clustering
 *              dma_host - 1 if this host has ISA DMA issues (bus doesn't
 *                      expose all of the address lines, so that DMA cannot
 *                      be done from an arbitrary address).
 *
 * Returns:     1 if it is OK to merge the block into the request.  0
 *              if it is not OK.
 *
 * Lock status: io_request_lock is assumed to be held here.
 *
 * Notes:       Some drivers have limited scatter-gather table sizes, and
 *              thus they cannot queue an infinitely large command.  This
 *              function is called from ll_rw_blk before it attempts to merge
 *              a new block into a request to make sure that the request will
 *              not become too large.
 *
 *              This function is not designed to be directly called.  Instead
 *              it should be referenced from other functions where the
 *              use_clustering and dma_host parameters should be integer
 *              constants.  The compiler should thus be able to properly
 *              optimize the code, eliminating stuff that is irrelevant.
 *              It is more maintainable to do this way with a single function
 *              than to have 4 separate functions all doing roughly the
 *              same thing.
 */
__inline static int __scsi_merge_fn(request_queue_t * q,
				    struct request *req,
				    struct buffer_head *bh,
				    int use_clustering,
				    int dma_host)
{
	unsigned int sector, count;
	Scsi_Device *SDpnt;
	struct Scsi_Host *SHpnt;

	SDpnt = (Scsi_Device *) q->queuedata;
	SHpnt = SDpnt->host;

	count = bh->b_size >> 9;
	sector = bh->b_rsector;

	/*
	 * We come in here in one of two cases.   The first is that we
	 * are checking to see if we can add the buffer to the end of the
	 * request, the other is to see if we should add the request to the
	 * start.
	 */
	if (req->sector + req->nr_sectors == sector) {
		if (use_clustering) {
			/* 
			 * See if we can do this without creating another
			 * scatter-gather segment.  In the event that this is a
			 * DMA capable host, make sure that a segment doesn't span
			 * the DMA threshold boundary.  
			 */
			if (dma_host &&
			    virt_to_phys(req->bhtail->b_data - 1) == ISA_DMA_THRESHOLD) {
				goto new_segment;
			}
			if (CONTIGUOUS_BUFFERS(req->bhtail, bh)) {
				/*
				 * This one is OK.  Let it go.
				 */
				return 1;
			}
		}
		goto new_segment;
	} else if (req->sector - count == sector) {
		if (use_clustering) {
			/* 
			 * See if we can do this without creating another
			 * scatter-gather segment.  In the event that this is a
			 * DMA capable host, make sure that a segment doesn't span
			 * the DMA threshold boundary. 
			 */
			if (dma_host &&
			    virt_to_phys(bh->b_data - 1) == ISA_DMA_THRESHOLD) {
				goto new_segment;
			}
			if (CONTIGUOUS_BUFFERS(bh, req->bh)) {
				/*
				 * This one is OK.  Let it go.
				 */
				return 1;
			}
		}
		goto new_segment;
	} else {
		panic("Attempt to merge sector that doesn't belong");
	}
      new_segment:
	if (req->nr_segments < SHpnt->sg_tablesize) {
		/*
		 * This will form the start of a new segment.  Bump the 
		 * counter.
		 */
		req->nr_segments++;
		return 1;
	} else {
		return 0;
	}
}

/*
 * Function:    scsi_merge_fn_()
 *
 * Purpose:     queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              bh      - Block which we may wish to merge into request
 *
 * Returns:     1 if it is OK to merge the block into the request.  0
 *              if it is not OK.
 *
 * Lock status: io_request_lock is assumed to be held here.
 *
 * Notes:       Optimized for different cases depending upon whether
 *              ISA DMA is in use and whether clustering should be used.
 */
#define MERGEFCT(_FUNCTION, _CLUSTER, _DMA)		\
static int _FUNCTION(request_queue_t * q,		\
	       struct request * req,			\
	       struct buffer_head * bh)			\
{							\
    int ret;						\
    SANITY_CHECK(req, _CLUSTER, _DMA);			\
    ret =  __scsi_merge_fn(q, req, bh, _CLUSTER, _DMA); \
    return ret;						\
}

MERGEFCT(scsi_merge_fn_, 0, 0)
MERGEFCT(scsi_merge_fn_d, 0, 1)
MERGEFCT(scsi_merge_fn_c, 1, 0)
MERGEFCT(scsi_merge_fn_dc, 1, 1)
/*
 * Function:    __scsi_merge_requests_fn()
 *
 * Purpose:     Prototype for queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              next    - 2nd request that we might want to combine with req
 *              use_clustering - 1 if this host wishes to use clustering
 *              dma_host - 1 if this host has ISA DMA issues (bus doesn't
 *                      expose all of the address lines, so that DMA cannot
 *                      be done from an arbitrary address).
 *
 * Returns:     1 if it is OK to merge the two requests.  0
 *              if it is not OK.
 *
 * Lock status: io_request_lock is assumed to be held here.
 *
 * Notes:       Some drivers have limited scatter-gather table sizes, and
 *              thus they cannot queue an infinitely large command.  This
 *              function is called from ll_rw_blk before it attempts to merge
 *              a new block into a request to make sure that the request will
 *              not become too large.
 *
 *              This function is not designed to be directly called.  Instead
 *              it should be referenced from other functions where the
 *              use_clustering and dma_host parameters should be integer
 *              constants.  The compiler should thus be able to properly
 *              optimize the code, eliminating stuff that is irrelevant.
 *              It is more maintainable to do this way with a single function
 *              than to have 4 separate functions all doing roughly the
 *              same thing.
 */
__inline static int __scsi_merge_requests_fn(request_queue_t * q,
					     struct request *req,
					     struct request *next,
					     int use_clustering,
					     int dma_host)
{
	Scsi_Device *SDpnt;
	struct Scsi_Host *SHpnt;

	SDpnt = (Scsi_Device *) q->queuedata;
	SHpnt = SDpnt->host;

	/*
	 * If the two requests together are too large (even assuming that we
	 * can merge the boundary requests into one segment, then don't
	 * allow the merge.
	 */
	if (req->nr_segments + next->nr_segments - 1 > SHpnt->sg_tablesize) {
		return 0;
	}
	/*
	 * The main question is whether the two segments at the boundaries
	 * would be considered one or two.
	 */
	if (use_clustering) {
		/* 
		 * See if we can do this without creating another
		 * scatter-gather segment.  In the event that this is a
		 * DMA capable host, make sure that a segment doesn't span
		 * the DMA threshold boundary.  
		 */
		if (dma_host &&
		    virt_to_phys(req->bhtail->b_data - 1) == ISA_DMA_THRESHOLD) {
			goto dont_combine;
		}
		if (CONTIGUOUS_BUFFERS(req->bhtail, next->bh)) {
			/*
			 * This one is OK.  Let it go.
			 */
			req->nr_segments += next->nr_segments - 1;
			return 1;
		}
	}
      dont_combine:
	/*
	 * We know that the two requests at the boundary should not be combined.
	 * Make sure we can fix something that is the sum of the two.
	 * A slightly stricter test than we had above.
	 */
	if (req->nr_segments + next->nr_segments > SHpnt->sg_tablesize) {
		return 0;
	} else {
		/*
		 * This will form the start of a new segment.  Bump the 
		 * counter.
		 */
		req->nr_segments += next->nr_segments;
		return 1;
	}
}

/*
 * Function:    scsi_merge_requests_fn_()
 *
 * Purpose:     queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              bh      - Block which we may wish to merge into request
 *
 * Returns:     1 if it is OK to merge the block into the request.  0
 *              if it is not OK.
 *
 * Lock status: io_request_lock is assumed to be held here.
 *
 * Notes:       Optimized for different cases depending upon whether
 *              ISA DMA is in use and whether clustering should be used.
 */
#define MERGEREQFCT(_FUNCTION, _CLUSTER, _DMA)		\
static int _FUNCTION(request_queue_t * q,		\
		     struct request * req,		\
		     struct request * next)		\
{							\
    int ret;						\
    SANITY_CHECK(req, _CLUSTER, _DMA);			\
    ret =  __scsi_merge_requests_fn(q, req, next, _CLUSTER, _DMA); \
    return ret;						\
}

MERGEREQFCT(scsi_merge_requests_fn_, 0, 0)
MERGEREQFCT(scsi_merge_requests_fn_d, 0, 1)
MERGEREQFCT(scsi_merge_requests_fn_c, 1, 0)
MERGEREQFCT(scsi_merge_requests_fn_dc, 1, 1)
/*
 * Function:    __init_io()
 *
 * Purpose:     Prototype for io initialize function.
 *
 * Arguments:   SCpnt   - Command descriptor we wish to initialize
 *              sg_count_valid  - 1 if the sg count in the req is valid.
 *              use_clustering - 1 if this host wishes to use clustering
 *              dma_host - 1 if this host has ISA DMA issues (bus doesn't
 *                      expose all of the address lines, so that DMA cannot
 *                      be done from an arbitrary address).
 *
 * Returns:     1 on success.
 *
 * Lock status: 
 *
 * Notes:       Only the SCpnt argument should be a non-constant variable.
 *              This function is designed in such a way that it will be
 *              invoked from a series of small stubs, each of which would
 *              be optimized for specific circumstances.
 *
 *              The advantage of this is that hosts that don't do DMA
 *              get versions of the function that essentially don't have
 *              any of the DMA code.  Same goes for clustering - in the
 *              case of hosts with no need for clustering, there is no point
 *              in a whole bunch of overhead.
 *
 *              Finally, in the event that a host has set can_queue to SG_ALL
 *              implying that there is no limit to the length of a scatter
 *              gather list, the sg count in the request won't be valid
 *              (mainly because we don't need queue management functions
 *              which keep the tally uptodate.
 */
__inline static int __init_io(Scsi_Cmnd * SCpnt,
			      int sg_count_valid,
			      int use_clustering,
			      int dma_host)
{
	struct buffer_head *bh;
	struct buffer_head *bhprev;
	char *buff;
	int count;
	int i;
	struct request *req;
	struct scatterlist *sgpnt;
	int this_count;

	/*
	 * FIXME(eric) - don't inline this - it doesn't depend on the
	 * integer flags.   Come to think of it, I don't think this is even
	 * needed any more.  Need to play with it and see if we hit the
	 * panic.  If not, then don't bother.
	 */
	if (!SCpnt->request.bh) {
		/* 
		 * Case of page request (i.e. raw device), or unlinked buffer 
		 * Typically used for swapping, but this isn't how we do
		 * swapping any more.
		 */
		panic("I believe this is dead code.  If we hit this, I was wrong");
#if 0
		SCpnt->request_bufflen = SCpnt->request.nr_sectors << 9;
		SCpnt->request_buffer = SCpnt->request.buffer;
		SCpnt->use_sg = 0;
		/*
		 * FIXME(eric) - need to handle DMA here.
		 */
#endif
		return 1;
	}
	req = &SCpnt->request;
	/*
	 * First we need to know how many scatter gather segments are needed.
	 */
	if (!sg_count_valid) {
		count = __count_segments(req, use_clustering, dma_host);
	} else {
		count = req->nr_segments;
	}

	/*
	 * If the dma pool is nearly empty, then queue a minimal request
	 * with a single segment.  Typically this will satisfy a single
	 * buffer.
	 */
	if (dma_host && scsi_dma_free_sectors <= 10) {
		this_count = SCpnt->request.current_nr_sectors;
		goto single_segment;
	}
	/*
	 * Don't bother with scatter-gather if there is only one segment.
	 */
	if (count == 1) {
		this_count = SCpnt->request.nr_sectors;
		goto single_segment;
	}
	SCpnt->use_sg = count;

	/* 
	 * Allocate the actual scatter-gather table itself.
	 * scsi_malloc can only allocate in chunks of 512 bytes 
	 */
	SCpnt->sglist_len = (SCpnt->use_sg
			     * sizeof(struct scatterlist) + 511) & ~511;

	sgpnt = (struct scatterlist *) scsi_malloc(SCpnt->sglist_len);

	/*
	 * Now fill the scatter-gather table.
	 */
	if (!sgpnt) {
		/*
		 * If we cannot allocate the scatter-gather table, then
		 * simply write the first buffer all by itself.
		 */
		printk("Warning - running *really* short on DMA buffers\n");
		this_count = SCpnt->request.current_nr_sectors;
		goto single_segment;
	}
	/* 
	 * Next, walk the list, and fill in the addresses and sizes of
	 * each segment.
	 */
	memset(sgpnt, 0, SCpnt->sglist_len);
	SCpnt->request_buffer = (char *) sgpnt;
	SCpnt->request_bufflen = 0;
	bhprev = NULL;

	for (count = 0, bh = SCpnt->request.bh;
	     bh; bh = bh->b_reqnext) {
		if (use_clustering && bhprev != NULL) {
			if (dma_host &&
			    virt_to_phys(bhprev->b_data - 1) == ISA_DMA_THRESHOLD) {
				/* Nothing - fall through */
			} else if (CONTIGUOUS_BUFFERS(bhprev, bh)) {
				/*
				 * This one is OK.  Let it go.
				 */
				sgpnt[count - 1].length += bh->b_size;
				if (!dma_host) {
					SCpnt->request_bufflen += bh->b_size;
				}
				bhprev = bh;
				continue;
			}
		}
		count++;
		sgpnt[count - 1].address = bh->b_data;
		sgpnt[count - 1].length += bh->b_size;
		if (!dma_host) {
			SCpnt->request_bufflen += bh->b_size;
		}
		bhprev = bh;
	}

	/*
	 * Verify that the count is correct.
	 */
	if (count != SCpnt->use_sg) {
		panic("Incorrect sg segment count");
	}
	if (!dma_host) {
		return 1;
	}
	/*
	 * Now allocate bounce buffers, if needed.
	 */
	SCpnt->request_bufflen = 0;
	for (i = 0; i < count; i++) {
		SCpnt->request_bufflen += sgpnt[i].length;
		if (virt_to_phys(sgpnt[i].address) + sgpnt[i].length - 1 >
		    ISA_DMA_THRESHOLD && !sgpnt[count].alt_address) {
			sgpnt[i].alt_address = sgpnt[i].address;
			sgpnt[i].address =
			    (char *) scsi_malloc(sgpnt[i].length);
			/*
			 * If we cannot allocate memory for this DMA bounce
			 * buffer, then queue just what we have done so far.
			 */
			if (sgpnt[i].address == NULL) {
				printk("Warning - running low on DMA memory\n");
				SCpnt->request_bufflen -= sgpnt[i].length;
				SCpnt->use_sg = i;
				if (i == 0) {
					panic("DMA pool exhausted");
				}
				break;
			}
			if (SCpnt->request.cmd == WRITE) {
				memcpy(sgpnt[i].address, sgpnt[i].alt_address,
				       sgpnt[i].length);
			}
		}
	}
	return 1;

      single_segment:
	/*
	 * Come here if for any reason we choose to do this as a single
	 * segment.  Possibly the entire request, or possibly a small
	 * chunk of the entire request.
	 */
	bh = SCpnt->request.bh;
	buff = SCpnt->request.buffer;

	if (dma_host) {
		/*
		 * Allocate a DMA bounce buffer.  If the allocation fails, fall
		 * back and allocate a really small one - enough to satisfy
		 * the first buffer.
		 */
		if (virt_to_phys(SCpnt->request.bh->b_data)
		    + (this_count << 9) - 1 > ISA_DMA_THRESHOLD) {
			buff = (char *) scsi_malloc(this_count << 9);
			if (!buff) {
				printk("Warning - running low on DMA memory\n");
				this_count = SCpnt->request.current_nr_sectors;
				buff = (char *) scsi_malloc(this_count << 9);
				if (!buff) {
					panic("Unable to allocate DMA buffer\n");
				}
			}
			if (SCpnt->request.cmd == WRITE)
				memcpy(buff, (char *) SCpnt->request.buffer, this_count << 9);
		}
	}
	SCpnt->request_bufflen = this_count << 9;
	SCpnt->request_buffer = buff;
	SCpnt->use_sg = 0;
	return 1;
}

#define INITIO(_FUNCTION, _VALID, _CLUSTER, _DMA)	\
static int _FUNCTION(Scsi_Cmnd * SCpnt)			\
{							\
    return __init_io(SCpnt, _VALID, _CLUSTER, _DMA);	\
}

/*
 * ll_rw_blk.c now keeps track of the number of segments in
 * a request.  Thus we don't have to do it any more here.
 * We always force "_VALID" to 1.  Eventually clean this up
 * and get rid of the extra argument.
 */
#if 0
/* Old definitions */
INITIO(scsi_init_io_, 0, 0, 0)
INITIO(scsi_init_io_d, 0, 0, 1)
INITIO(scsi_init_io_c, 0, 1, 0)
INITIO(scsi_init_io_dc, 0, 1, 1)

/* Newer redundant definitions. */
INITIO(scsi_init_io_, 1, 0, 0)
INITIO(scsi_init_io_d, 1, 0, 1)
INITIO(scsi_init_io_c, 1, 1, 0)
INITIO(scsi_init_io_dc, 1, 1, 1)
#endif

INITIO(scsi_init_io_v, 1, 0, 0)
INITIO(scsi_init_io_vd, 1, 0, 1)
INITIO(scsi_init_io_vc, 1, 1, 0)
INITIO(scsi_init_io_vdc, 1, 1, 1)
/*
 * Function:    initialize_merge_fn()
 *
 * Purpose:     Initialize merge function for a host
 *
 * Arguments:   SHpnt   - Host descriptor.
 *
 * Returns:     Nothing.
 *
 * Lock status: 
 *
 * Notes:
 */
void initialize_merge_fn(Scsi_Device * SDpnt)
{
	request_queue_t *q;
	struct Scsi_Host *SHpnt;
	SHpnt = SDpnt->host;

	q = &SDpnt->request_queue;

	/*
	 * If the host has already selected a merge manager, then don't
	 * pick a new one.
	 */
	if (q->merge_fn != NULL) {
		return;
	}
	/*
	 * If this host has an unlimited tablesize, then don't bother with a
	 * merge manager.  The whole point of the operation is to make sure
	 * that requests don't grow too large, and this host isn't picky.
	 */
	if (SHpnt->sg_tablesize == SG_ALL) {
		if (!CLUSTERABLE_DEVICE(SHpnt, SDpnt) && SHpnt->unchecked_isa_dma == 0) {
			SDpnt->scsi_init_io_fn = scsi_init_io_v;
		} else if (!CLUSTERABLE_DEVICE(SHpnt, SDpnt) && SHpnt->unchecked_isa_dma != 0) {
			SDpnt->scsi_init_io_fn = scsi_init_io_vd;
		} else if (CLUSTERABLE_DEVICE(SHpnt, SDpnt) && SHpnt->unchecked_isa_dma == 0) {
			SDpnt->scsi_init_io_fn = scsi_init_io_vc;
		} else if (CLUSTERABLE_DEVICE(SHpnt, SDpnt) && SHpnt->unchecked_isa_dma != 0) {
			SDpnt->scsi_init_io_fn = scsi_init_io_vdc;
		}
		return;
	}
	/*
	 * Now pick out the correct function.
	 */
	if (!CLUSTERABLE_DEVICE(SHpnt, SDpnt) && SHpnt->unchecked_isa_dma == 0) {
		q->merge_fn = scsi_merge_fn_;
		q->merge_requests_fn = scsi_merge_requests_fn_;
		SDpnt->scsi_init_io_fn = scsi_init_io_v;
	} else if (!CLUSTERABLE_DEVICE(SHpnt, SDpnt) && SHpnt->unchecked_isa_dma != 0) {
		q->merge_fn = scsi_merge_fn_d;
		q->merge_requests_fn = scsi_merge_requests_fn_d;
		SDpnt->scsi_init_io_fn = scsi_init_io_vd;
	} else if (CLUSTERABLE_DEVICE(SHpnt, SDpnt) && SHpnt->unchecked_isa_dma == 0) {
		q->merge_fn = scsi_merge_fn_c;
		q->merge_requests_fn = scsi_merge_requests_fn_c;
		SDpnt->scsi_init_io_fn = scsi_init_io_vc;
	} else if (CLUSTERABLE_DEVICE(SHpnt, SDpnt) && SHpnt->unchecked_isa_dma != 0) {
		q->merge_fn = scsi_merge_fn_dc;
		q->merge_requests_fn = scsi_merge_requests_fn_dc;
		SDpnt->scsi_init_io_fn = scsi_init_io_vdc;
	}
}


#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#if 0

struct stram_desc
  {
    unsigned first:1;
    unsigned last:1;
    unsigned alloced:1;
    unsigned length:24;
  };

#define DP(ptr) ((struct stram_desc *) (ptr))

static unsigned long stramsize;
static unsigned long stramaddr;

void
atari_stram_init (void)
{
  struct stram_desc *dp;
  stramaddr = atari_stram_start;
  stramsize = atari_stram_size;

  /* initialize start boundary */
  dp = DP (stramaddr);
  dp->first = 1;
  dp->alloced = 0;
  dp->length = stramsize - 2 * sizeof (*dp);

  /* initialize end boundary */
  dp = DP (stramaddr + stramsize) - 1;
  dp->last = 1;
  dp->alloced = 0;
  dp->length = stramsize - 2 * sizeof (*dp);

#ifdef DEBUG
  printk ("stram end boundary is %p, length is %d\n", dp,
	  dp->length);
#endif
}

void *
atari_stram_alloc (long size)
{
  /* last chunk */
  struct stram_desc *dp;
  void *ptr;

  /* round off */
  size = (size + 3) & ~3;

#ifdef DEBUG
  printk ("stram_alloc: allocate %ld bytes\n", size);
#endif

  /*
   * get pointer to descriptor for last chunk by 
   * going backwards from end chunk
   */
  dp = DP (stramaddr + stramsize) - 1;
  dp = DP ((unsigned long) dp - dp->length) - 1;

  while ((dp->alloced || dp->length < size) && !dp->first)
    dp = DP ((unsigned long) dp - dp[-1].length) - 2;

  if (dp->alloced || dp->length < size)
    {
      printk ("no stram available for %ld allocation\n", size);
      return NULL;
    }

  if (dp->length < size + 2 * sizeof (*dp))
    {
      /* length too small to split; allocate the whole thing */
      dp->alloced = 1;
      ptr = (void *) (dp + 1);
      dp = DP ((unsigned long) ptr + dp->length);
      dp->alloced = 1;
#ifdef DEBUG
      printk ("stram_alloc: no split\n");
#endif
    }
  else
    {
      /* split the extent; use the end part */
      long newsize = dp->length - (2 * sizeof (*dp) + size);

#ifdef DEBUG
      printk ("stram_alloc: splitting %d to %ld\n", dp->length,
	      newsize);
#endif
      dp->length = newsize;
      dp = DP ((unsigned long) (dp + 1) + newsize);
      dp->first = dp->last = 0;
      dp->alloced = 0;
      dp->length = newsize;
      dp++;
      dp->first = dp->last = 0;
      dp->alloced = 1;
      dp->length = size;
      ptr = (void *) (dp + 1);
      dp = DP ((unsigned long) ptr + size);
      dp->alloced = 1;
      dp->length = size;
    }

#ifdef DEBUG
  printk ("stram_alloc: returning %p\n", ptr);
#endif
  return ptr;
}

void 
atari_stram_free (void *ptr)
{
  struct stram_desc *sdp = DP (ptr) - 1, *dp2;
  struct stram_desc *edp = DP ((unsigned long) ptr + sdp->length);

  /* deallocate the chunk */
  sdp->alloced = edp->alloced = 0;

  /* check if we should merge with the previous chunk */
  if (!sdp->first && !sdp[-1].alloced)
    {
      dp2 = DP ((unsigned long) sdp - sdp[-1].length) - 2;
      dp2->length += sdp->length + 2 * sizeof (*sdp);
      edp->length = dp2->length;
      sdp = dp2;
    }

  /* check if we should merge with the following chunk */
  if (!edp->last && !edp[1].alloced)
    {
      dp2 = DP ((unsigned long) edp + edp[1].length) + 2;
      dp2->length += edp->length + 2 * sizeof (*sdp);
      sdp->length = dp2->length;
      edp = dp2;
    }
}

#else

#include <linux/mm.h>
#include <linux/init.h>

/* ++roman:
 * 
 * New version of ST-Ram buffer allocation. Instead of using the
 * 1 MB - 4 KB that remain when the the ST-Ram chunk starts at $1000
 * (1 MB granularity!), such buffers are reserved like this:
 *
 *  - If the kernel resides in ST-Ram anyway, we can take the buffer
 *    from behind the current kernel data space the normal way
 *    (incrementing start_mem).
 *    
 *  - If the kernel is in TT-Ram, stram_init() initializes start and
 *    end of the available region. Buffers are allocated from there
 *    and mem_init() later marks the such used pages as reserved.
 *    Since each TT-Ram chunk is at least 4 MB in size, I hope there
 *    won't be an overrun of the ST-Ram region by normal kernel data
 *    space.
 *    
 * For that, ST-Ram may only be allocated while kernel initialization
 * is going on, or exactly: before mem_init() is called. There is also
 * no provision now for freeing ST-Ram buffers. It seems that isn't
 * really needed.
 *
 * ToDo:
 * Check the high level scsi code what is done when the
 * UNCHECKED_ISA_DMA flag is set. It guess, it is just a test for adr
 * < 16 Mega. There should be call to atari_stram_alloc() instead.
 *
 * Also ToDo:
 * Go through head.S and delete parts no longer needed (transparent
 * mapping of ST-Ram etc.)
 * 
 */
   

unsigned long rsvd_stram_beg, rsvd_stram_end;
    /* Start and end of the reserved ST-Ram region */
static unsigned long stram_end;
    /* Overall end of ST-Ram */


__initfunc(void atari_stram_init( void ))

{	int		i;

	for( i = 0; i < m68k_num_memory; ++i ) {
		if (m68k_memory[i].addr == 0) {
			rsvd_stram_beg = PTOV( 0x800 ); /* skip super-only first 2 KB! */
			rsvd_stram_end = rsvd_stram_beg;
			stram_end = rsvd_stram_beg - 0x800 + m68k_memory[i].size;
			return;
		}
	}
	/* Should never come here! (There is always ST-Ram!) */
}


void *atari_stram_alloc( long size, unsigned long *start_mem )

{
	static int				kernel_in_stram = -1;
	
	void	*adr = 0;
	
	if (kernel_in_stram < 0)
		kernel_in_stram = (PTOV( 0 ) == 0);

	if (kernel_in_stram) {
		/* Get memory from kernel data space */
		adr = (void *) *start_mem;
		*start_mem += size;
	}
	else {
		/* Get memory from rsvd_stram_beg */
		if (rsvd_stram_end + size < stram_end) {
			adr = (void *) rsvd_stram_end;
			rsvd_stram_end += size;
		}
	}
	
	return( adr );
}

void atari_stram_free( void *ptr )

{
	/* Sorry, this is a dummy. It isn't needed anyway. */
}

#endif



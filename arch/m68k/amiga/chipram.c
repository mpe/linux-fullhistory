/*
**  linux/amiga/chipram.c
**
**      Modified 03-May-94 by Geert Uytterhoeven
**                           (Geert.Uytterhoeven@cs.kuleuven.ac.be)
**          - 64-bit aligned allocations for full AGA compatibility
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/bootinfo.h>
#include <asm/amigahw.h>

struct chip_desc {
	unsigned first   :  1;
	unsigned last    :  1;
	unsigned alloced :  1;
	unsigned length  : 24;
	long pad;					/* We suppose this makes this struct 64 bits long!! */
};

#define DP(ptr) ((struct chip_desc *)(ptr))

static unsigned long chipsize;

void amiga_chip_init (void)
{
  struct chip_desc *dp;

  if (!AMIGAHW_PRESENT(CHIP_RAM))
    return;

  chipsize = boot_info.bi_amiga.chip_size;

  /* initialize start boundary */

  custom.color[0] = 0xfff;

  dp = DP(chipaddr);
  dp->first = 1;

  custom.color[0] = 0x0f00;

  dp->alloced = 0;
  dp->length = chipsize - 2*sizeof(*dp);

  /* initialize end boundary */
  dp = DP(chipaddr + chipsize) - 1;
  dp->last = 1;
  
  custom.color[0] = 0x00f0;

  dp->alloced = 0;
  dp->length = chipsize - 2*sizeof(*dp);

  custom.color[0] = 0x000f;

#ifdef DEBUG
  printk ("chipram end boundary is %p, length is %d\n", dp,
	  dp->length);
#endif
}

void *amiga_chip_alloc (long size)
{
	/* last chunk */
	struct chip_desc *dp;
	void *ptr;

	/* round off */
	size = (size + 7) & ~7;

#ifdef DEBUG
	printk ("chip_alloc: allocate %ld bytes\n", size);
#endif

	/*
	 * get pointer to descriptor for last chunk by 
	 * going backwards from end chunk
	 */
	dp = DP(chipaddr + chipsize) - 1;
	dp = DP((unsigned long)dp - dp->length) - 1;
	
	while ((dp->alloced || dp->length < size)
	       && !dp->first)
		dp = DP ((unsigned long)dp - dp[-1].length) - 2;

	if (dp->alloced || dp->length < size) {
		printk ("no chipmem available for %ld allocation\n", size);
		return NULL;
	}

	if (dp->length < (size + 2*sizeof(*dp))) {
		/* length too small to split; allocate the whole thing */
		dp->alloced = 1;
		ptr = (void *)(dp+1);
		dp = DP((unsigned long)ptr + dp->length);
		dp->alloced = 1;
#ifdef DEBUG
		printk ("chip_alloc: no split\n");
#endif
	} else {
		/* split the extent; use the end part */
		long newsize = dp->length - (2*sizeof(*dp) + size);

#ifdef DEBUG
		printk ("chip_alloc: splitting %d to %ld\n", dp->length,
			newsize);
#endif
		dp->length = newsize;
		dp = DP((unsigned long)(dp+1) + newsize);
		dp->first = dp->last = 0;
		dp->alloced = 0;
		dp->length = newsize;
		dp++;
		dp->first = dp->last = 0;
		dp->alloced = 1;
		dp->length = size;
		ptr = (void *)(dp+1);
		dp = DP((unsigned long)ptr + size);
		dp->alloced = 1;
		dp->length = size;
	}

#ifdef DEBUG
	printk ("chip_alloc: returning %p\n", ptr);
#endif

	if ((unsigned long)ptr & 7)
		panic("chip_alloc: alignment violation\n");

	return ptr;
}

void amiga_chip_free (void *ptr)
{
	struct chip_desc *sdp = DP(ptr) - 1, *dp2;
	struct chip_desc *edp = DP((unsigned long)ptr + sdp->length);

	/* deallocate the chunk */
	sdp->alloced = edp->alloced = 0;

	/* check if we should merge with the previous chunk */
	if (!sdp->first && !sdp[-1].alloced) {
		dp2 = DP((unsigned long)sdp - sdp[-1].length) - 2;
		dp2->length += sdp->length + 2*sizeof(*sdp);
		edp->length = dp2->length;
		sdp = dp2;
	}

	/* check if we should merge with the following chunk */
	if (!edp->last && !edp[1].alloced) {
		dp2 = DP((unsigned long)edp + edp[1].length) + 2;
		dp2->length += edp->length + 2*sizeof(*sdp);
		sdp->length = dp2->length;
		edp = dp2;
	}
}

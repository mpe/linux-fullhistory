
/* dvma support routines */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sun3mmu.h>
#include <asm/dvma.h>

unsigned long dvma_next_free = DVMA_START;

/* get needed number of free dma pages, or panic if not enough */

void *sun3_dvma_malloc(int len)
{
	unsigned long vaddr;
	
	/* if the next free pages have been accessed, skip them */
	while((dvma_next_free < DVMA_END) &&
	      (sun3_get_pte(dvma_next_free) & SUN3_PAGE_ACCESSED))
		dvma_next_free += SUN3_PTE_SIZE;

       	if((dvma_next_free + len) > DVMA_END) 
		panic("sun3_dvma_malloc: out of dvma pages");
	
	vaddr = dvma_next_free;
	dvma_next_free = PAGE_ALIGN(dvma_next_free + len);

	return (void *)vaddr;
}
     


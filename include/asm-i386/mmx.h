#ifndef _ASM_MMX_H
#define _ASM_MMX_H

/*
 *	MMX 3Dnow! helper operations
 */

#include <linux/types.h>
 
extern void *_mmx_memcpy(void *to, const void *from, size_t size);
extern void mmx_clear_page(long page);
extern void mmx_copy_page(long to, long from);

#endif

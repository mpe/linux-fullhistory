#ifndef _ASMARM_PGTABLE_H
#define _ASMARM_PGTABLE_H

#include <asm/proc-fns.h>
#include <asm/proc/pgtable.h>

#define module_map	vmalloc
#define module_unmap	vfree

extern int do_check_pgt_cache(int, int);

#endif /* _ASMARM_PGTABLE_H */

/* $Id: loadmmu.c,v 1.50 1998/02/05 14:19:02 jj Exp $
 * loadmmu.c:  This code loads up all the mm function pointers once the
 *             machine type has been determined.  It also sets the static
 *             mmu values such as PAGE_NONE, etc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/config.h>

#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/a.out.h>
#include <asm/mmu_context.h>
#include <asm/oplib.h>

unsigned long page_offset = 0xf0000000;
unsigned long stack_top = 0xf0000000 - PAGE_SIZE;

struct ctx_list *ctx_list_pool;
struct ctx_list ctx_free;
struct ctx_list ctx_used;

unsigned int pg_iobits;

extern void ld_mmu_sun4c(void);
extern void ld_mmu_srmmu(void);

__initfunc(void load_mmu(void))
{
	switch(sparc_cpu_model) {
	case sun4c:
	case sun4:
		ld_mmu_sun4c();
		break;
	case sun4m:
	case sun4d:
		ld_mmu_srmmu();
		break;
	case ap1000:
#if CONFIG_AP1000
		ld_mmu_apmmu();
		break;
#endif
	default:
		prom_printf("load_mmu: %d unsupported\n", (int)sparc_cpu_model);
		prom_halt();
	}
	btfixup();
}

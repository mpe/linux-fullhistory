/*
 * arch/arm/mm/mm-armv.c
 *
 * Common routines for ARM v3 and v4 architectures.
 *
 * Copyright (C) 1998 Russell King
 *
 * Do not compile this file directly!
 */

#ifndef MAPPING
#error MAPPING not defined - do not compile this file individually
#endif

static const struct mapping {
	unsigned long virtual;
	unsigned long physical;
	unsigned long length;
	int domain:4,
	    prot_read:1,
	    prot_write:1;
} mapping[] __initdata = {
	MAPPING
};

#define SIZEOFMAP (sizeof(mapping) / sizeof(mapping[0]))

__initfunc(unsigned long setup_io_pagetables(unsigned long start_mem))
{
	const struct mapping *mp;
	int i;

	for (i = 0, mp = mapping; i < SIZEOFMAP; i++, mp++) {
		unsigned long virtual, physical, length;
		int prot;

		virtual = mp->virtual;
		physical = mp->physical;
		length = mp->length;
		prot = (mp->prot_read ? PTE_AP_READ : 0) | (mp->prot_write ? PTE_AP_WRITE : 0);

		while ((virtual & 1048575 || physical & 1048575) && length >= PAGE_SIZE) {
			alloc_init_page(&start_mem, virtual, physical, mp->domain, prot);
			length -= PAGE_SIZE;
			virtual += PAGE_SIZE;
			physical += PAGE_SIZE;
		}

		prot = (mp->prot_read ? PMD_SECT_AP_READ : 0) |
		       (mp->prot_write ? PMD_SECT_AP_WRITE : 0);

		while (length >= 1048576) {
			alloc_init_section(&start_mem, virtual, physical, mp->domain, prot);
			length -= 1048576;
			virtual += 1048576;
			physical += 1048576;
		}

		prot = (mp->prot_read ? PTE_AP_READ : 0) | (mp->prot_write ? PTE_AP_WRITE : 0);

		while (length >= PAGE_SIZE) {
			alloc_init_page(&start_mem, virtual, physical, mp->domain, prot);
			length -= PAGE_SIZE;
			virtual += PAGE_SIZE;
			physical += PAGE_SIZE;
		}
	}

	return start_mem;
}

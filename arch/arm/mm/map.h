/*
 *  linux/arch/arm/mm/map.h
 *
 *  Copyright (C) 1999 Russell King
 *
 *  Page table mapping constructs and function prototypes
 */
struct map_desc {
	unsigned long virtual;
	unsigned long physical;
	unsigned long length;
	int domain:4,
	    prot_read:1,
	    prot_write:1,
	    cacheable:1,
	    bufferable:1;
};

extern struct map_desc	io_desc[];
extern unsigned int	io_desc_size;

extern void zonesize_init(unsigned int *);
extern void create_memmap_holes(void);
extern void pagetable_init(void);


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

struct mem_desc {
	unsigned long virt_start;
	unsigned long virt_end;
};

extern struct map_desc	io_desc[];
extern unsigned int	io_desc_size;
extern struct mem_desc	mem_desc[];
extern unsigned int	mem_desc_size;

extern void mark_usable_memory_areas(unsigned long start, unsigned long end);
extern unsigned long create_mem_holes(unsigned long start, unsigned long end);
extern unsigned long setup_page_tables(unsigned long start, unsigned long end);


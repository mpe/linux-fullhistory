extern inline unsigned long uvirt_to_phys(unsigned long adr);

/* vmalloced address to physical address */
extern inline unsigned long kvirt_to_phys(unsigned long adr)
{ return uvirt_to_phys(VMALLOC_VMADDR(adr)); }

/* vmalloced address to bus address */
extern inline unsigned long kvirt_to_bus(unsigned long adr)
{ return virt_to_bus(phys_to_virt(kvirt_to_phys(adr))); }

/* always vmalloced memory - not always continuous! */
void*	rvmalloc(unsigned long size);
void	rvfree(void* mem, unsigned long size);

/* either kmalloc() or bigphysarea() alloced memory - continuous */
void*	bmalloc(unsigned long size);
void	bfree(void* mem, unsigned long size);

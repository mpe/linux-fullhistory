#ifndef _M68K_IO_H
#define _M68K_IO_H

static inline unsigned char get_user_byte_io(const char * addr)
{
	register unsigned char _v;

	__asm__ __volatile__ ("moveb %1,%0":"=dm" (_v):"m" (*addr));
	return _v;
}
#define inb_p(addr) get_user_byte_io((char *)(addr))
#define inb(addr) get_user_byte_io((char *)(addr))

static inline void put_user_byte_io(char val,char *addr)
{
	__asm__ __volatile__ ("moveb %0,%1"
			      : /* no outputs */
			      :"idm" (val),"m" (*addr)
			      : "memory");
}
#define outb_p(x,addr) put_user_byte_io((x),(char *)(addr))
#define outb(x,addr) put_user_byte_io((x),(char *)(addr))

/*
 * Change virtual addresses to physical addresses and vv.
 * These are trivial on the 1:1 Linux/i386 mapping (but if we ever
 * make the kernel segment mapped at 0, we need to do translation
 * on the i386 as well)
 */
extern unsigned long mm_vtop(unsigned long addr);
extern unsigned long mm_ptov(unsigned long addr);

extern inline unsigned long virt_to_phys(volatile void * address)
{
	return (unsigned long) mm_vtop((unsigned long)address);
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *) mm_ptov(address);
}

/*
 * IO bus memory addresses are also 1:1 with the physical address
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt


#endif /* _M68K_IO_H */

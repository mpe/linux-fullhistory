#ifndef _M68K_IO_H
#define _M68K_IO_H

static inline unsigned char get_user_byte_io(const char * addr)
{
	register unsigned char _v;

	__asm__ __volatile__ ("moveb %1,%0":"=r" (_v):"m" (*addr));
	return _v;
}
#define inb_p(addr) get_user_byte_io((char *)(addr))
#define inb(addr) get_user_byte_io((char *)(addr))

static inline void put_user_byte_io(char val,char *addr)
{
	__asm__ __volatile__ ("moveb %0,%1"
			      : /* no outputs */
			      :"r" (val),"m" (*addr)
			      : "memory");
}
#define outb_p(x,addr) put_user_byte_io((x),(char *)(addr))
#define outb(x,addr) put_user_byte_io((x),(char *)(addr))

#endif /* _M68K_IO_H */

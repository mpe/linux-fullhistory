/*
 * I/O 'port' access routines
 */
#include <asm/byteorder.h>
#include <asm/io.h>

#define inb_asm(port) {( \
  unsigned char ret; \
  asm ( "lbz %0,0(%1)\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n" : "=r" (ret) : "r" (port+_IO_BASE)); \
  return ret; \
})

inline unsigned char
inb(int port)
{
  unsigned char ret;
  asm("/*inb*/\n");
  asm ( "lbz %0,0(%1)" : "=r" (ret) : "r" (port+_IO_BASE));
  return ret;
}

inline unsigned short
inw(int port)
{
  unsigned short ret;
  asm("/*inw*/\n");
  asm ( "lhbrx %0,%1,%2" : "=r" (ret) : "r" (port+_IO_BASE), "r" (0));
  return ret;
}

inline unsigned long
inl(int port)
{
  unsigned long ret;
  asm("/*inl*/\n");
  asm ( "lwbrx %0,%1,%2" : "=r" (ret) : "r" (port+_IO_BASE), "r" (0));
  return ret;
}

inline unsigned char
outb(unsigned char val,int port)
{
  asm("/*outb*/\n");
  asm ( "stb %0,0(%1)" :: "r" (val), "r" (port+_IO_BASE));
  return (val);
}

inline unsigned short
outw(unsigned short val,int port)
{
  asm("/*outw*/\n");
  asm ( "sthbrx %0,%1,%2" :: "r" (val), "r" (port+_IO_BASE), "r" (0));
  return (val);
}

inline unsigned long
outl(unsigned long val,int port)
{
  asm("/*outl*/\n");
  asm ( "stwbrx %0,%1,%2" :: "r" (val), "r" (port+_IO_BASE), "r" (0));
  return (val);
}

void insb(int port, char *ptr, int len)
{
  memcpy( (void *)ptr, (void *)(port+_IO_BASE), len);
}

void insw(int port, short *ptr, int len)
{
  asm ("mtctr	%2 \n\t"
       "subi	%1,%1,2 \n\t"
       "00:\n\t"
       "lhbrx	%2,0,%0 \n\t"
       "sthu	%2,2(%1) \n\t"
       "bdnz 00b \n\t"
       :: "r" (port+_IO_BASE), "r" (ptr), "r" (len));
}

void insw_unswapped(int port, short *ptr, int len)
{
  memcpy( (void *)ptr, (void *)(port+_IO_BASE), (len*sizeof(short)) );
}

void insl(int port, long *ptr, int len)
{
  asm ("mtctr	%2 \n\t"
       "subi	%1,%1,4 \n\t"
       "00:\n\t"
       "lhbrx	%2,0,%0 \n\t"
       "sthu	%2,4(%1) \n\t"
       "bdnz 00b \n\t"
       :: "r" (port+_IO_BASE), "r" (ptr), "r" (len));
}

void outsb(int port, char *ptr, int len)
{
  memcpy( (void *)ptr, (void *)(port+_IO_BASE), len );
}

void outsw(int port, short *ptr, int len)
{
  asm ("mtctr	%2\n\t"
       "subi	%1,%1,2\n\t"
       "00:lhzu	%2,2(%1)\n\t"
       "sthbrx	%2,0,%0\n\t"
       "bdnz	00b\n\t"
       :: "r" (port+_IO_BASE), "r" (ptr), "r" (len));
}

void outsw_unswapped(int port, short *ptr, int len)
{
  memcpy( (void *)ptr, (void *)(port+_IO_BASE), len*sizeof(short) );  
}

void outsl(int port, long *ptr, int len)
{
  asm ("mtctr	%2\n\t"
       "subi	%1,%1,4\n\t"
       "00:lwzu	%2,4(%1)\n\t"
       "sthbrx	%2,0,%0\n\t"
       "bdnz	00b\n\t"
       :: "r" (port+_IO_BASE), "r" (ptr), "r" (len));
}

void insl_unswapped(int port, long *ptr, int len)
{
        unsigned long *io_ptr = (unsigned long *)(_IO_BASE+port);
        /* Ensure I/O operations complete */
        __asm__ volatile("eieio");
        while (len-- > 0)
        {
                *ptr++ = (*io_ptr);
        }
}

void outsl_unswapped(int port, long *ptr, int len)
{
        unsigned long *io_ptr = (unsigned long *)(_IO_BASE+port);
        /* Ensure I/O operations complete */
        __asm__ volatile("eieio");
        while (len-- > 0)
        {
                *io_ptr = (*ptr++);
        }
}

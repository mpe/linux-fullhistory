#ifndef __SPARC_IO_H
#define __SPARC_IO_H

#include <asm/page.h>      /* IO address mapping routines need this */

/*
 * Defines for io operations on the Sparc. Whether a memory access is going
 * to i/o sparc is encoded in the pte. The type bits determine whether this
 * is i/o sparc, on board memory, or VME space for VME cards. I think VME
 * space only works on sun4's
 */

extern inline unsigned long inb_local(unsigned long addr)
{
  return 0;
}

extern inline void outb_local(unsigned char b, unsigned long addr)
{
  return;
}

extern inline unsigned long inb(unsigned long addr)
{
  return 0;
}

extern inline unsigned long inw(unsigned long addr)
{
  return 0;
}

extern inline unsigned long inl(unsigned long addr)
{
  return 0;
}

extern inline void outb(unsigned char b, unsigned long addr)
{
  return;
}

extern inline void outw(unsigned short b, unsigned long addr)
{
  return;
}

extern inline void outl(unsigned int b, unsigned long addr)
{
  return;
}

/*
 * Memory functions
 */
extern inline unsigned long readb(unsigned long addr)
{
  return 0;
}

extern inline unsigned long readw(unsigned long addr)
{
  return 0;
}

extern inline unsigned long readl(unsigned long addr)
{
  return 0;
}

extern inline void writeb(unsigned short b, unsigned long addr)
{
  return;
}

extern inline void writew(unsigned short b, unsigned long addr)
{
  return;
}

extern inline void writel(unsigned int b, unsigned long addr)
{
  return;
}

#define inb_p inb
#define outb_p outb

extern inline void mapioaddr(unsigned long physaddr, unsigned long virt_addr)
{
  unsigned long page_entry;

  page_entry = physaddr >> PAGE_SHIFT;
  page_entry |= (PTE_V | PTE_ACC | PTE_NC | PTE_IO);  /* kernel io addr */

  put_pte(virt_addr, page_entry);
  return;
}

#endif /* !(__SPARC_IO_H) */

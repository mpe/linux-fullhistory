/* $Id: PeeCeeI.c,v 1.3 1997/08/28 23:59:52 davem Exp $
 * PeeCeeI.c: The emerging standard...
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>

#ifdef CONFIG_PCI

#include <asm/io.h>

void outsb(unsigned long addr, const void *src, unsigned long count)
{
	const u8 *p = src;

	while(count--)
		outb(*p++, addr);
}

void outsw(unsigned long addr, const void *src, unsigned long count)
{
	if(count) {
		const u16 *ps = src;
		const u32 *pi;

		if(((u64)src) & 0x2) {
			outw(*ps++, addr);
			count--;
		}
		pi = (const u32 *)ps;
		while(count >= 2) {
			u32 w;

			w = *pi++;
			outw(w >> 16, addr);
			outw(w, addr);
			count -= 2;
		}
		ps = (const u16 *)pi;
		if(count)
			outw(*ps, addr);
	}
}

void outsl(unsigned long addr, const void *src, unsigned long count)
{
	if(count) {
		if((((u64)src) & 0x3) == 0) {
			const u32 *p = src;
			while(count--)
				outl(*p++, addr);
		} else {
			const u8 *pb;
			const u16 *ps = src;
			u32 l = 0, l2;
			const u32 *pi;

			switch(((u64)src) & 0x3) {
			case 0x2:
				count -= 1;
				l = *ps++;
				pi = (const u32 *)ps;
				while(count--) {
					l2 = *pi++;
					outl(((l <<16) | (l2 >> 16)), addr);
					l = l2;
				}
				ps = (const u16 *)pi;
				outl(((l << 16) | (*ps >> 16)), addr);
				break;

			case 0x1:
				count -= 1;
				pb = src;
				l = (*pb++ << 16);
				ps = (const u16 *)pb;
				l |= *ps++;
				pi = (const u32 *)ps;
				while(count--) {
					l2 = *pi++;
					outl(((l << 8) | (l2 >> 24)), addr);
					l = l2;
				}
				pb = (const u8 *)pi;
				outl(((l << 8) | (*pb >> 24)), addr);
				break;

			case 0x3:
				count -= 1;
				pb = src;
				l = (*pb++ >> 24);
				pi = (const u32 *)pb;
				while(count--) {
					l2 = *pi++;
					outl(((l << 24) | (l2 >> 8)), addr);
					l = l2;
				}
				ps = (const u16 *)pi;
				l2 = (*ps++ << 16);
				pb = (const u8 *)ps;
				l2 |= (*pb << 8);
				outl(((l << 24) | (l2 >> 8)), addr);
				break;
			}
		}
	}
}

void insb(unsigned long addr, void *dst, unsigned long count)
{
	if(count) {
		u32 *pi;
		u8 *pb = dst;

		while((((unsigned long)pb) & 0x3) && count--)
			*pb++ = inb(addr);
		pi = (u32 *)pb;
		while(count >= 4) {
			u32 w;

			w  = (inb(addr) << 24);
			w |= (inb(addr) << 16);
			w |= (inb(addr) << 8);
			w |= inb(addr);
			*pi++ = w;
			count -= 4;
		}
		pb = (u8 *)pi;
		while(count--)
			*pb++ = inb(addr);
	}
}

void insw(unsigned long addr, void *dst, unsigned long count)
{
	if(count) {
		u16 *ps = dst;
		u32 *pi;

		if(((unsigned long)ps) & 0x2) {
			*ps++ = inw(addr);
			count--;
		}
		pi = (u32 *)ps;
		while(count >= 2) {
			u32 w;

			w  = (inw(addr) << 16);
			w |= inw(addr);
			*pi++ = w;
			count -= 2;
		}
		ps = (u16 *)pi;
		if(count)
			*ps = inw(addr);
	}
}

void insl(unsigned long addr, void *dst, unsigned long count)
{
	if(count) {
		if((((unsigned long)dst) & 0x3) == 0) {
			u32 *pi = dst;
			while(count--)
				*pi++ = inl(addr);
		} else {
			u32 l = 0, l2, *pi;
			u16 *ps;
			u8 *pb;

			switch(((unsigned long)dst) & 3) {
			case 0x2:
				ps = dst;
				count -= 1;
				l = inl(addr);
				*ps++ = (l >> 16);
				pi = (u32 *)ps;
				while(count--) {
					l2 = inl(addr);
					*pi++ = (l << 16) | (l2 >> 16);
					l = l2;
				}
				ps = (u16 *)pi;
				*ps = (l << 16);
				break;

			case 0x1:
				pb = dst;
				count -= 1;
				*pb++ = (l >> 24);
				ps = (u16 *)pb;
				*ps++ = (l >> 8);
				pi = (u32 *)ps;
				while(count--) {
					l2 = inl(addr);
					*pi++ = ((l << 24) | (l2 >> 8));
					l = l2;
				}
				pb = (u8 *)pi;
				*pb = (l >> 8);
				break;

			case 0x3:
				pb = (u8 *)dst;
				count -= 1;
				l = inl(addr);
				*pb++ = l >> 24;
				pi = (u32 *)pb;
				while(count--) {
					l2 = inl(addr);
					*pi++ = ((l >> 24) | (l2 << 8));
					l = l2;
				}
				ps = (u16 *)pi;
				*ps++ = l >> 8;
				pb = (u8 *)ps;
				*pb = l;
				break;
			}
		}
	}
}

#endif /* CONFIG_PCI */

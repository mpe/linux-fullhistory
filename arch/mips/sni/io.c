/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Low level I/O functions for SNI.
 *
 * $Id: io.c,v 1.2 1998/03/27 08:53:50 ralf Exp $
 */
#include <linux/string.h>
#include <asm/mipsconfig.h>
#include <asm/addrspace.h>
#include <asm/system.h>
#include <asm/spinlock.h>
#include <asm/sni.h>

unsigned char sni_map_isa_cache;

#define unused __attribute__((unused))

/*
 * The PCIMT_CSMAPISA is shared by all processors; we need locking.
 *
 * XXX It's legal to use all the I/O memory access functions in interrupt
 * code, so we need to use the _irq locking stuff which may result in
 * significant IRQ latencies.
 */
static spinlock_t csmapisa_lock unused = SPIN_LOCK_UNLOCKED;

/*
 * Urgs...  We only can see a 16mb window of the 4gb EISA address space
 * at PCIMT_EISA_BASE.  Maladia segmentitis ...
 *
 * XXX Check out if accessing PCIMT_CSMAPISA really is slow.
 * For now assume so.
 */
static inline void update_isa_cache(unsigned long address)
{
	unsigned char upper;

	upper = address >> 24;
	if (sni_map_isa_cache != upper) {
		sni_map_isa_cache = upper;
		*(volatile unsigned char *)PCIMT_CSMAPISA = ~upper;
	}
}

static unsigned char sni_readb(unsigned long addr)
{
	unsigned char res;

	spin_lock_irq(&csmapisa_lock);
	update_isa_cache(addr);
	addr &= 0xffffff;
	res = *(volatile unsigned char *) (PCIMT_EISA_BASE + addr);
	spin_unlock_irq(&csmapisa_lock);

	return res;
}

static unsigned short sni_readw(unsigned long addr)
{
	unsigned short res;

	spin_lock_irq(&csmapisa_lock);
	update_isa_cache(addr);
	addr &= 0xffffff;
	res = *(volatile unsigned char *) (PCIMT_EISA_BASE + addr);
	spin_unlock_irq(&csmapisa_lock);

	return res;
}

static unsigned int sni_readl(unsigned long addr)
{
	unsigned int res;

	spin_lock_irq(&csmapisa_lock);
	update_isa_cache(addr);
	addr &= 0xffffff;
	res = *(volatile unsigned char *) (PCIMT_EISA_BASE + addr);
	spin_unlock_irq(&csmapisa_lock);

	return res;
}

static void sni_writeb(unsigned char val, unsigned long addr)
{
	spin_lock_irq(&csmapisa_lock);
	update_isa_cache(addr);
	addr &= 0xffffff;
	*(volatile unsigned char *) (PCIMT_EISA_BASE + addr) = val;
	spin_unlock_irq(&csmapisa_lock);
}

static void sni_writew(unsigned short val, unsigned long addr)
{
	spin_lock_irq(&csmapisa_lock);
	update_isa_cache(addr);
	addr &= 0xffffff;
	*(volatile unsigned char *) (PCIMT_EISA_BASE + addr) = val;
	spin_unlock_irq(&csmapisa_lock);
}

static void sni_writel(unsigned int val, unsigned long addr)
{
	spin_lock_irq(&csmapisa_lock);
	update_isa_cache(addr);
	addr &= 0xffffff;
	*(volatile unsigned char *) (PCIMT_EISA_BASE + addr) = val;
	spin_unlock_irq(&csmapisa_lock);
}

static void sni_memset_io(unsigned long addr, int val, unsigned long len)
{
	unsigned long waddr;

	waddr = PCIMT_EISA_BASE | (addr & 0xffffff);
	spin_lock_irq(&csmapisa_lock);
	while(len) {
		unsigned long fraglen;

		fraglen = (~addr + 1) & 0xffffff;
		fraglen = (fraglen < len) ? fraglen : len;
		update_isa_cache(addr);
		memset((char *)waddr, val, fraglen);
		addr += fraglen;
		waddr = waddr + fraglen - 0x1000000;
		len -= fraglen;
	}
	spin_unlock_irq(&csmapisa_lock);
}

static void sni_memcpy_fromio(unsigned long to, unsigned long from, unsigned long len)
{
	unsigned long waddr;

	waddr = PCIMT_EISA_BASE | (from & 0xffffff);
	spin_lock_irq(&csmapisa_lock);
	while(len) {
		unsigned long fraglen;

		fraglen = (~from + 1) & 0xffffff;
		fraglen = (fraglen < len) ? fraglen : len;
		update_isa_cache(from);
		memcpy((void *)to, (void *)waddr, fraglen);
		to += fraglen;
		from += fraglen;
		waddr = waddr + fraglen - 0x1000000;
		len -= fraglen;
	}
	spin_unlock_irq(&csmapisa_lock);
}

static void sni_memcpy_toio(unsigned long to, unsigned long from, unsigned long len)
{
	unsigned long waddr;

	waddr = PCIMT_EISA_BASE | (to & 0xffffff);
	spin_lock_irq(&csmapisa_lock);
	while(len) {
		unsigned long fraglen;

		fraglen = (~to + 1) & 0xffffff;
		fraglen = (fraglen < len) ? fraglen : len;
		update_isa_cache(to);
		memcpy((char *)to + PCIMT_EISA_BASE, (void *)from, fraglen);
		to += fraglen;
		from += fraglen;
		waddr = waddr + fraglen - 0x1000000;
		len -= fraglen;
	}
	spin_unlock_irq(&csmapisa_lock);
}

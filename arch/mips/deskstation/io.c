/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Low level I/O functions for Jazz family machine.
 *
 * FIXME: This implementation fits the Tyne.  How does the EISA rPC44 handle
 * the eight high address bits?
 */
#include <linux/string.h>
#include <asm/mipsconfig.h>
#include <asm/addrspace.h>
#include <asm/sni.h>

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is is mapped
 * for the processor.
 */
extern unsigned long isa_slot_offset;

static unsigned char deskstation_readb(unsigned long addr)
{
	return *(volatile unsigned char *) (isa_slot_offset + addr);
}

static unsigned short deskstation_readw(unsigned long addr)
{
	return *(volatile unsigned short *) (isa_slot_offset + addr);
}

static unsigned int deskstation_readl(unsigned long addr)
{
	return *(volatile unsigned int *) (isa_slot_offset + addr);
}

static void deskstation_writeb(unsigned char val, unsigned long addr)
{
	*(volatile unsigned char *) (isa_slot_offset + addr) = val;
}

static void deskstation_writew(unsigned short val, unsigned long addr)
{
	*(volatile unsigned char *) (isa_slot_offset + addr) = val;
}

static void deskstation_writel(unsigned int val, unsigned long addr)
{
	*(volatile unsigned char *) (isa_slot_offset + addr) = val;
}

static void deskstation_memset_io(unsigned long addr, int val, unsigned long len)
{
	addr += isa_slot_offset;
	memset((void *)addr, val, len);
}

static void deskstation_memcpy_fromio(unsigned long to, unsigned long from, unsigned long len)
{
	from += isa_slot_offset;
	memcpy((void *)to, (void *)from, len);
}

static void deskstation_memcpy_toio(unsigned long to, unsigned long from, unsigned long len)
{
	to += isa_slot_offset;
	memcpy((void *)to, (void *)from, len);
}

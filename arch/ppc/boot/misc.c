/*
 * misc.c
 * 
 * Adapted for PowerPC by Gary Thomas
 *
 * Rewritten by Cort Dougan (cort@cs.nmt.edu)
 * One day to be replaced by a single bootloader for chrp/prep/pmac. -- Cort
 */

#include "../coffboot/zlib.h"
#include "asm/residual.h"
#include <elf.h>
#include <linux/config.h>
#include <asm/page.h>
#ifdef CONFIG_MBX
#include <asm/mbx.h>
bd_t	hold_board_info;
#endif

/*
 * MBX: loads at:      	0x00200000
 *      board data at: 	end of ram
 * PREP:
 *  powerstack 1:
 *      network load at:   configurable - should set to link addr-0x400
 *                         exec. addr set to link addr
 *            such as load: 0x005ffc00 exec 0x00600000
 *      hd/floppy/tape load at:
 *  powerstack 2:
 *      loads at:       0x00400000
 *  IBM 830 (carolina):
 *      loads at:	???
 *
 * Please send me load/board info and such data for hardware not
 * listed here so I can keep track since things are getting tricky
 * with the different load addrs with different firmware.  This will
 * help to avoid breaking the load/boot process.
 * -- Cort
 */
char *avail_ram;
char *end_avail;

char cmd_line[256];
RESIDUAL hold_residual;
unsigned long initrd_start = 0, initrd_end = 0;
char *zimage_start;
int zimage_size;

char *vidmem = (char *)0xC00B8000;
int lines, cols;
int orig_x, orig_y;

void puts(const char *);
void putc(const char c);
void puthex(unsigned long val);
void _bcopy(char *src, char *dst, int len);
void * memcpy(void * __dest, __const void * __src,
			    int __n);
void gunzip(void *, int, unsigned char *, int *);

void pause()
{
	puts("pause\n");
}

void exit()
{
	puts("exit\n");
	while(1); 
}

#ifndef CONFIG_MBX
static void clear_screen()
{
	int i, j;
	for (i = 0;  i < lines;  i++) {
	  for (j = 0;  j < cols;  j++) {
	    vidmem[((i*cols)+j)*2] = ' ';
	    vidmem[((i*cols)+j)*2+1] = 0x07;
	  }
	}
}

static void scroll()
{
	int i;

	memcpy ( vidmem, vidmem + cols * 2, ( lines - 1 ) * cols * 2 );
	for ( i = ( lines - 1 ) * cols * 2; i < lines * cols * 2; i += 2 )
		vidmem[i] = ' ';
}

tstc(void)
{
	return (CRT_tstc() );
}

getc(void)
{
	while (1) {
		if (CRT_tstc()) return (CRT_getc());
	}
}

void 
putc(const char c)
{
	int x,y;

	x = orig_x;
	y = orig_y;

	if ( c == '\n' ) {
		x = 0;
		if ( ++y >= lines ) {
			scroll();
			y--;
		}
	} else if (c == '\b') {
		if (x > 0) {
			x--;
		}
	} else {
		vidmem [ ( x + cols * y ) * 2 ] = c; 
		if ( ++x >= cols ) {
			x = 0;
			if ( ++y >= lines ) {
				scroll();
				y--;
			}
		}
	}

	cursor(x, y);

	orig_x = x;
	orig_y = y;
}

void puts(const char *s)
{
	int x,y;
	char c;

	x = orig_x;
	y = orig_y;

	while ( ( c = *s++ ) != '\0' ) {
		if ( c == '\n' ) {
			x = 0;
			if ( ++y >= lines ) {
				scroll();
				y--;
			}
		} else {
			vidmem [ ( x + cols * y ) * 2 ] = c; 
			if ( ++x >= cols ) {
				x = 0;
				if ( ++y >= lines ) {
					scroll();
					y--;
				}
			}
		}
	}

	orig_x = x;
	orig_y = y;
}
#else
/* The MBX is just the serial port.
*/
tstc(void)
{
        return (serial_tstc());
}

getc(void)
{
        while (1) {
                if (serial_tstc()) return (serial_getc());
        }
}

void 
putc(const char c)
{
        serial_putchar(c);
}

void puts(const char *s)
{
        char c;

        while ( ( c = *s++ ) != '\0' ) {
                serial_putchar(c);
                if ( c == '\n' )
                        serial_putchar('\r');
        }
}

#endif /* CONFIG_MBX */

void * memcpy(void * __dest, __const void * __src,
			    int __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i=0;i<__n;i++) d[i] = s[i];
}

int memcmp(__const void * __dest, __const void * __src,
			    int __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i=0;i<__n;i++, d++, s++)
	{
		if (*d != *s)
		{
			return (*s - *d);
		}
	}
	return (0);
}

void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while(1);	/* Halt */
}

void *zalloc(void *x, unsigned items, unsigned size)
{
	void *p = avail_ram;
	
	size *= items;
	size = (size + 7) & -8;
	avail_ram += size;
	if (avail_ram > end_avail) {
		puts("oops... out of memory\n");
		pause();
	}
	return p;
}

void zfree(void *x, void *addr, unsigned nb)
{
}

#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0

#define DEFLATED	8


void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp)
{
	z_stream s;
	int r, i, flags;
	
	/* skip header */
	i = 10;
	flags = src[3];
	if (src[2] != DEFLATED || (flags & RESERVED) != 0) {
		puts("bad gzipped data\n");
		exit();
	}
	if ((flags & EXTRA_FIELD) != 0)
		i = 12 + src[10] + (src[11] << 8);
	if ((flags & ORIG_NAME) != 0)
		while (src[i++] != 0)
			;
	if ((flags & COMMENT) != 0)
		while (src[i++] != 0)
			;
	if ((flags & HEAD_CRC) != 0)
		i += 2;
	if (i >= *lenp) {
		puts("gunzip: ran out of data in header\n");
		exit();
	}
	
	s.zalloc = zalloc;
	s.zfree = zfree;
	r = inflateInit2(&s, -MAX_WBITS);
	if (r != Z_OK) {
		puts("inflateInit2 returned %d\n");
		exit();
	}
	s.next_in = src + i;
	s.avail_in = *lenp - i;
	s.next_out = dst;
	s.avail_out = dstlen;
	r = inflate(&s, Z_FINISH);
	if (r != Z_OK && r != Z_STREAM_END) {
		puts("inflate returned %d\n");
		exit();
	}
	*lenp = s.next_out - (unsigned char *) dst;
	inflateEnd(&s);
}

unsigned char sanity[0x2000];

unsigned long
decompress_kernel(unsigned long load_addr, int num_words, unsigned long cksum, RESIDUAL *residual)
{
	int timer;
	extern unsigned long start;
	char *cp, ch;
	unsigned long i;

	lines = 25;
	cols = 80;
	orig_x = 0;
	orig_y = 24;
	
	
#ifndef CONFIG_MBX
	vga_init(0xC0000000);
	/* copy the residual data */
	if (residual)
		memcpy(&hold_residual,residual,sizeof(RESIDUAL));
#else /* CONFIG_MBX */
	/* copy board data */
	if (residual)
		_bcopy((char *)residual, (char *)&hold_board_info,
		       sizeof(hold_board_info));
#endif /* CONFIG_MBX */
	
	/* MBX/prep put the board/residual data at the end of memory */
	if ( residual )
		end_avail = (char *)PAGE_ALIGN((unsigned long)residual);
	/* prep netboot looses the residual */
	else
		end_avail = (char *)0x00800000;
	

	puts("loaded at:     "); puthex(load_addr);
	puts(" "); puthex((unsigned long)(load_addr + (4*num_words))); puts("\n");
	if ( (unsigned long)load_addr != (unsigned long)&start )
	{
		puts("relocated to:  "); puthex((unsigned long)&start);
		puts(" ");
		puthex((unsigned long)((unsigned long)&start + (4*num_words)));
		puts("\n");
	}

	if ( residual )
	{
		puts("board data at: "); puthex((unsigned long)residual);
		puts(" ");
#ifdef CONFIG_MBX	
		puthex((unsigned long)((unsigned long)residual + sizeof(bd_t)));
#else
		puthex((unsigned long)((unsigned long)residual + sizeof(RESIDUAL)));
#endif	
		puts("\n");
		puts("relocated to:  ");
#ifdef CONFIG_MBX
		puthex((unsigned long)&hold_board_info);
#else
		puthex((unsigned long)&hold_residual);
#endif
		puts(" ");
#ifdef CONFIG_MBX
		puthex((unsigned long)((unsigned long)&hold_board_info + sizeof(bd_t)));
#else
		puthex((unsigned long)((unsigned long)&hold_residual + sizeof(RESIDUAL)));
#endif	
		puts("\n");
	}

	/* we have to subtract 0x10000 here to correct for objdump including the
	   size of the elf header which we strip -- Cort */
	zimage_start = (char *)(load_addr - 0x10000 + ZIMAGE_OFFSET);
	zimage_size = ZIMAGE_SIZE;
	puts("zimage at:     "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start)); puts("\n");

	if ( INITRD_OFFSET )
		initrd_start = load_addr - 0x10000 + INITRD_OFFSET;
	else
		initrd_start = 0;
	initrd_end = INITRD_SIZE + initrd_start;

	/*
	 * setup avail_ram - this is the first part of ram usable
	 * by the uncompress code. -- Cort
	 */
	avail_ram = (char *)PAGE_ALIGN((unsigned long)zimage_start+zimage_size);
	if ( (load_addr+(num_words*4)) > (unsigned long) avail_ram )
		avail_ram = (char *)(load_addr+(num_words*4));
	if ( ((unsigned long)&start+(num_words*4)) > (unsigned long) avail_ram )
		avail_ram = (char *)((unsigned long)&start+(num_words*4));
	
	/* relocate initrd */
	if ( initrd_start )
	{
		puts("initrd at:     "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
		/*
		 * Memory is really tight on the MBX (we can assume 4M)
		 * so put the initrd at the TOP of ram, and set end_avail
		 * to right after that.
		 *
		 * I should do something like this for prep, too and keep
		 * a variable end_of_DRAM to keep track of what we think the
		 * max ram is.
		 * -- Cort
		 */
		memcpy ((void *)PAGE_ALIGN(-PAGE_SIZE+(unsigned long)end_avail-INITRD_SIZE),
			(void *)initrd_start,
			INITRD_SIZE );
		initrd_start = PAGE_ALIGN(-PAGE_SIZE+(unsigned long)end_avail-INITRD_SIZE);
		initrd_end = initrd_start + INITRD_SIZE;
		end_avail = (char *)initrd_start;
		puts("relocated to:  "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
	}
	
	puts("avail ram:     "); puthex((unsigned long)avail_ram); puts(" ");
	puthex((unsigned long)end_avail); puts("\n");
	
#ifndef CONFIG_MBX
	CRT_tstc();  /* Forces keyboard to be initialized */
#endif	
	puts("\nLinux/PPC load: ");
	timer = 0;
	cp = cmd_line;
	while (timer++ < 5*1000) {
		if (tstc()) {
			while ((ch = getc()) != '\n' && ch != '\r') {
				if (ch == '\b') {
					if (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
				} else {
					*cp++ = ch;
					putc(ch);
				}
			}
			break;  /* Exit 'timer' loop */
		}
		mdelay(1);  /* 1 msec */
	}
	*cp = 0;
	puts("\n");

	/* mappings on early boot can only handle 16M */
	if ( (int)(&cmd_line[0]) > (16<<20))
		puts("cmd_line located > 16M\n");
	if ( (int)&hold_residual > (16<<20))
		puts("hold_residual located > 16M\n");
	if ( initrd_start > (16<<20))
		puts("initrd_start located > 16M\n");
       
	puts("Uncompressing Linux...");

	gunzip(0, 0x400000, zimage_start, &zimage_size);
	puts("done.\n");
	puts("Now booting the kernel\n");
#ifndef CONFIG_MBX	
	return (unsigned long)&hold_residual;
#else
	return (unsigned long)&hold_board_info;
#endif	
}

void puthex(unsigned long val)
{
	unsigned char buf[10];
	int i;
	for (i = 7;  i >= 0;  i--)
	{
		buf[i] = "0123456789ABCDEF"[val & 0x0F];
		val >>= 4;
	}
	buf[8] = '\0';
	puts(buf);
}

/*
 * PCI/ISA I/O support
 */

volatile unsigned char *ISA_io  = (unsigned char *)0x80000000;
volatile unsigned char *ISA_mem = (unsigned char *)0xC0000000;

void
outb(int port, char val)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	ISA_io[port] = val;
}

unsigned char
inb(int port)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	return (ISA_io[port]);
}

unsigned long
local_to_PCI(unsigned long addr)
{
	return ((addr & 0x7FFFFFFF) | 0x80000000);
}

void
_bcopy(char *src, char *dst, int len)
{
	while (len--) *dst++ = *src++;
}

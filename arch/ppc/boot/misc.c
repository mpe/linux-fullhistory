/*
 * misc.c
 * 
 * Adapted for PowerPC by Gary Thomas
 *
 * Rewritten by Cort Dougan (cort@cs.nmt.edu)
 * Soon to be replaced by a single bootloader for chrp/prep/pmac. -- Cort
 */

#include "../coffboot/zlib.h"
#include "asm/residual.h"
#include <elf.h>

/* this is where the INITRD gets moved to for safe keeping */
#define INITRD_DESTINATION /*0x00f00000*/ 0x01f00000
/* this will do for now - Cort */
char *avail_ram = (char *) 0x00800000; /* start with 8M */
/* assume 15M max since this is where we copy the initrd to -- Cort */
char *end_avail = (char *) INITRD_DESTINATION; 

RESIDUAL hold_residual;
unsigned long initrd_start = 0, initrd_end = 0;
char *zimage_start;
int zimage_size;
char cmd_line[256];

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
	
	
	/* Turn off MMU.  Since we are mapped 1-1, this is OK. */
	flush_instruction_cache();
	_put_HID0(_get_HID0() & ~0x0000C000);
	_put_MSR(_get_MSR() & ~0x0030);

	vga_init(0xC0000000);
	/*clear_screen();*/

	puts("loaded at:    "); puthex(load_addr);
	puts(" "); puthex((unsigned long)(load_addr + (4*num_words))); puts("\n");
	
	puts("relocated to: "); puthex((unsigned long)&start);
	puts(" "); puthex((unsigned long)((unsigned long)&start + (4*num_words))); puts("\n");
	
	zimage_start = (char *)(load_addr - 0x10000 + ZIMAGE_OFFSET);
	zimage_size = ZIMAGE_SIZE;
	puts("zimage at:    "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start)); puts("\n");

	if ( INITRD_OFFSET )
		initrd_start = load_addr - 0x10000 + INITRD_OFFSET;
	else
		initrd_start = 0;
	initrd_end = INITRD_SIZE + initrd_start;
  
	/* relocate initrd */
	if ( initrd_start )
	{
		puts("initrd at:    "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
		
		memcpy ((void *)INITRD_DESTINATION,(void *)initrd_start,
			INITRD_SIZE );
		initrd_end = INITRD_DESTINATION + INITRD_SIZE;
		initrd_start = INITRD_DESTINATION;
		puts("Moved initrd to: "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
	}
	
	CRT_tstc();  /* Forces keyboard to be initialized */
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
		udelay(1000);  /* 1 msec */
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
	return (unsigned long)&hold_residual;
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

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
#ifdef CONFIG_MBX
#include <asm/mbx.h>
bd_t	hold_board_info;
#endif

/* this is where the INITRD gets moved to for safe keeping */
#define INITRD_DESTINATION /*0x00f00000*/ 0x01800000
#ifdef CONFIG_8xx
char *avail_ram = (char *) 0x00200000;
char *end_avail = (char *) 0x00400000;
#else /* CONFIG_8xx */
/* this will do for now - Cort */
char *avail_ram = (char *) 0x00800000; /* start with 8M */
/* assume 15M max since this is where we copy the initrd to -- Cort */
char *end_avail = (char *) INITRD_DESTINATION; 
#endif /* CONFIG_8xx */

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
	
	
#ifndef CONFIG_8xx
	vga_init(0xC0000000);
	/* copy the residual data */
	if (residual)
		memcpy(&hold_residual,residual,sizeof(RESIDUAL));
#endif /* CONFIG_8xx */
#ifdef CONFIG_MBX	
	/* copy board data */
	if (residual)
		_bcopy((char *)residual, (char *)&hold_board_info,
		       sizeof(hold_board_info));
#endif /* CONFIG_8xx */
	

	puts("loaded at:     "); puthex(load_addr);
	puts(" "); puthex((unsigned long)(load_addr + (4*num_words))); puts("\n");
	puts("relocated to:  "); puthex((unsigned long)&start);
	puts(" "); puthex((unsigned long)((unsigned long)&start + (4*num_words))); puts("\n");

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

	zimage_start = (char *)(load_addr - 0x10000 + ZIMAGE_OFFSET);
	zimage_size = ZIMAGE_SIZE;
	puts("zimage at:     "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start)); puts("\n");

	if ( INITRD_OFFSET )
		initrd_start = load_addr - 0x10000 + INITRD_OFFSET;
	else
		initrd_start = 0;
	initrd_end = INITRD_SIZE + initrd_start;
  
	/* relocate initrd */
	if ( initrd_start )
	{
		puts("initrd at:     "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
		
		memcpy ((void *)INITRD_DESTINATION,(void *)initrd_start,
			INITRD_SIZE );
		initrd_end = INITRD_DESTINATION + INITRD_SIZE;
		initrd_start = INITRD_DESTINATION;
		puts("Moved initrd to:  "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
	}
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

	/* these _bcopy() calls are here so I can add breakpoints to the boot for mbx -- Cort */
	/*_bcopy( (char *)0x100,(char *)&sanity, 0x2000-0x100);*/
	gunzip(0, 0x400000, zimage_start, &zimage_size);
	/*_bcopy( (char *)&sanity,(char *)0x100,0x2000-0x100);*/
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

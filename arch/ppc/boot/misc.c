/*
 * misc.c
 *
 * $Id: misc.c,v 1.53 1998/12/15 17:40:15 cort Exp $
 * 
 * Adapted for PowerPC by Gary Thomas
 *
 * Rewritten by Cort Dougan (cort@cs.nmt.edu)
 * One day to be replaced by a single bootloader for chrp/prep/pmac. -- Cort
 */

#include <linux/types.h>
#include "../coffboot/zlib.h"
#include "asm/residual.h"
#include <linux/elf.h>
#include <linux/config.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#ifdef CONFIG_MBX
#include <asm/mbx.h>
#endif
#ifdef CONFIG_FADS
#include <asm/fads.h>
#endif
#if defined(CONFIG_SERIAL_CONSOLE) && !defined(CONFIG_MBX)
#include "ns16550.h"
struct NS16550 *com_port;
#endif /* CONFIG_SERIAL_CONSOLE */

/*
 * Please send me load/board info and such data for hardware not
 * listed here so I can keep track since things are getting tricky
 * with the different load addrs with different firmware.  This will
 * help to avoid breaking the load/boot process.
 * -- Cort
 */
char *avail_ram;
char *end_avail;

/* Because of the limited amount of memory on the MBX, it presents
 * loading problems.  The biggest is that we load this boot program
 * into a relatively low memory address, and the Linux kernel Bss often
 * extends into this space when it get loaded.  When the kernel starts
 * and zeros the BSS space, it also writes over the information we
 * save here and pass to the kernel (command line and board info).
 * On the MBX we grab some known memory holes to hold this information.
 */
char cmd_preset[] = "console=tty0 console=ttyS0,9600n8";
char	cmd_buf[256];
char	*cmd_line = cmd_buf;

#if defined(CONFIG_MBX) || defined(CONFIG_FADS)
char	*root_string = "root=/dev/nfs";
char	*nfsaddrs_string = "nfsaddrs=";
char	*nfsroot_string = "nfsroot=";
char	*defroot_string = "/sys/mbxroot";
int	do_ipaddrs(char **cmd_cp, int echo);
void	do_nfsroot(char **cmd_cp, char *dp);
int	strncmp(const char * cs,const char * ct,size_t count);
char	*strrchr(const char * s, int c);
#endif

RESIDUAL hold_resid_buf;
RESIDUAL *hold_residual = &hold_resid_buf;
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

#if !defined(CONFIG_MBX) && !defined(CONFIG_FADS)
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
#if defined(CONFIG_SERIAL_CONSOLE) && !defined(CONFIG_MBX)
	return (CRT_tstc() || NS16550_tstc(com_port));
#else
	return (CRT_tstc() );
#endif /* CONFIG_SERIAL_CONSOLE */
}

getc(void)
{
	while (1) {
#if defined(CONFIG_SERIAL_CONSOLE) && !defined(CONFIG_MBX)
		if (NS16550_tstc(com_port)) return (NS16550_getc(com_port));
#endif /* CONFIG_SERIAL_CONSOLE */
		if (CRT_tstc()) return (CRT_getc());
	}
}

void 
putc(const char c)
{
	int x,y;

#if defined(CONFIG_SERIAL_CONSOLE) && !defined(CONFIG_MBX)
	NS16550_putc(com_port, c);
	if ( c == '\n' ) NS16550_putc(com_port, '\r');
#endif /* CONFIG_SERIAL_CONSOLE */

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
#if defined(CONFIG_SERIAL_CONSOLE) && !defined(CONFIG_MBX)
	        NS16550_putc(com_port, c);
	        if ( c == '\n' ) NS16550_putc(com_port, '\r');
#endif /* CONFIG_SERIAL_CONSOLE */

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
	BATU *u;
	BATL *l;
#if defined(CONFIG_MBX) || defined(CONFIG_KB)
	char	*dp;
#endif

	lines = 25;
	cols = 80;
	orig_x = 0;
	orig_y = 24;

	
#if !defined(CONFIG_MBX) && !defined(CONFIG_FADS)
	/*
	 * IBM's have the MMU on, so we have to disable it or
	 * things get really unhappy in the kernel when
	 * trying to setup the BATs with the MMU on
	 * -- Cort
	 */
	flush_instruction_cache();
	_put_HID0(_get_HID0() & ~0x0000C000);
	_put_MSR(_get_MSR() & ~0x0030);
	vga_init(0xC0000000);

#if defined(CONFIG_SERIAL_CONSOLE) && !defined(CONFIG_MBX)
	com_port = (struct NS16550 *)NS16550_init(0);
#endif /* CONFIG_SERIAL_CONSOLE */

	if (residual)
		memcpy(hold_residual,residual,sizeof(RESIDUAL));
#else /* CONFIG_MBX */
	
	/* Grab some space for the command line and board info.  Since
	 * we no longer use the ELF header, but it was loaded, grab
	 * that space.
	 */
	cmd_line = (char *)(load_addr - 0x10000);
	hold_residual = (RESIDUAL *)(cmd_line + sizeof(cmd_buf));
	/* copy board data */
	if (residual)
		memcpy(hold_residual,residual,sizeof(bd_t));
#endif /* CONFIG_MBX */

	/* MBX/prep sometimes put the residual/board info at the end of mem 
	 * assume 16M for now  -- Cort
	 * To boot on standard MBX boards with 4M, we can't use initrd,
	 * and we have to assume less memory.  -- Dan
	 */
	if ( INITRD_OFFSET )
		end_avail = (char *)0x01000000;
	else
		end_avail = (char *)0x00400000;

	/* let residual data tell us it's higher */
	if ( (unsigned long)residual > 0x00800000 )
		end_avail = (char *)PAGE_ALIGN((unsigned long)residual);

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
#if defined(CONFIG_MBX) || defined(CONFIG_FADS)
		puthex((unsigned long)((unsigned long)residual + sizeof(bd_t)));
#else
		puthex((unsigned long)((unsigned long)residual + sizeof(RESIDUAL)));
#endif	
		puts("\n");
		puts("relocated to:  ");
		puthex((unsigned long)hold_residual);
		puts(" ");
#if defined(CONFIG_MBX) || defined(CONFIG_FADS)
		puthex((unsigned long)((unsigned long)hold_residual + sizeof(bd_t)));
#else
		puthex((unsigned long)((unsigned long)hold_residual + sizeof(RESIDUAL)));
#endif	
		puts("\n");
	}

	/* we have to subtract 0x10000 here to correct for objdump including the
	   size of the elf header which we strip -- Cort */
	zimage_start = (char *)(load_addr - 0x10000 + ZIMAGE_OFFSET);
	zimage_size = ZIMAGE_SIZE;

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
	if ( ((load_addr+(num_words*4)) > (unsigned long) avail_ram)
		&& (load_addr <= 0x01000000) )
		avail_ram = (char *)(load_addr+(num_words*4));
	if ( (((unsigned long)&start+(num_words*4)) > (unsigned long) avail_ram)
		&& (load_addr <= 0x01000000) )
		avail_ram = (char *)((unsigned long)&start+(num_words*4));
	
	/* relocate zimage */
	puts("zimage at:     "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start)); puts("\n");
	/*
	 * don't relocate the zimage if it was loaded above 16M since
	 * things get weird if we try to relocate -- Cort
	 * We don't relocate zimage on a base MBX board because of
	 * insufficient memory.  In this case we don't have initrd either,
	 * so use that as an indicator.  -- Dan
	 */
	if (( (unsigned long)zimage_start <= 0x01000000 ) && initrd_start)
	{
		memcpy ((void *)PAGE_ALIGN(-PAGE_SIZE+(unsigned long)end_avail-zimage_size),
			(void *)zimage_start, zimage_size );	
		zimage_start = (char *)PAGE_ALIGN(-PAGE_SIZE+(unsigned long)end_avail-zimage_size);
		end_avail = (char *)zimage_start;
		puts("relocated to:  "); puthex((unsigned long)zimage_start);
		puts(" ");
		puthex((unsigned long)zimage_size+(unsigned long)zimage_start);
		puts("\n");
	}

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
#if 0		
		memcpy ((void *)PAGE_ALIGN(-PAGE_SIZE+(unsigned long)end_avail-INITRD_SIZE),
			(void *)initrd_start,
			INITRD_SIZE );
		initrd_start = PAGE_ALIGN(-PAGE_SIZE+(unsigned long)end_avail-INITRD_SIZE);
		initrd_end = initrd_start + INITRD_SIZE;
		end_avail = (char *)initrd_start;
		puts("relocated to:  "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
#endif		
	}

#ifndef CONFIG_MBX
	/* this is safe, just use it */
	/* I don't know why it didn't work for me on the MBX with 20 MB
	 * memory.  I guess something was saved up there, but I can't
	 * figure it out......we are running on luck.  -- Dan.
	 */
	avail_ram = (char *)0x00400000;
	end_avail = (char *)0x00600000;
#endif
	puts("avail ram:     "); puthex((unsigned long)avail_ram); puts(" ");
	puthex((unsigned long)end_avail); puts("\n");

	
#if !defined(CONFIG_MBX) && !defined(CONFIG_FADS)
	CRT_tstc();  /* Forces keyboard to be initialized */
#endif

	puts("\nLinux/PPC load: ");
	timer = 0;
	cp = cmd_line;
	memcpy (cmd_line, cmd_preset, sizeof(cmd_preset));
	while ( *cp ) putc(*cp++);
	while (timer++ < 5*1000) {
		if (tstc()) {
			while ((ch = getc()) != '\n' && ch != '\r') {
				if (ch == '\b') {
					if (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
#ifdef CONFIG_MBX
				  } else if (ch == '?') {
					if (!do_ipaddrs(&cp, 1)) {
						  *cp++ = ch;
						  putc(ch);
					}
#endif
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
#ifdef CONFIG_MBX
	/* The MBX does not currently have any default boot strategy.
	 * If the command line is not filled in, we will automatically
	 * create the default network boot.
	 */
	if (cmd_line[0] == 0) {
		dp = root_string;
		while (*dp != 0)
			*cp++ = *dp++;
		*cp++ = ' ';

		dp = nfsaddrs_string;
		while (*dp != 0)
			*cp++ = *dp++;
		dp = cp;
		do_ipaddrs(&cp, 0);
		*cp++ = ' ';

		/* Add the server address to the root file system path.
		*/
		dp = strrchr(dp, ':');
		dp++;
		do_nfsroot(&cp, dp);
		*cp = 0;
	}
#endif
	puts("\n");

	/* mappings on early boot can only handle 16M */
	if ( (int)(cmd_line[0]) > (16<<20))
		puts("cmd_line located > 16M\n");
	if ( (int)hold_residual > (16<<20))
		puts("hold_residual located > 16M\n");
	if ( initrd_start > (16<<20))
		puts("initrd_start located > 16M\n");
       
	puts("Uncompressing Linux...");

	gunzip(0, 0x400000, zimage_start, &zimage_size);
	puts("done.\n");
	puts("Now booting the kernel\n");
	return (unsigned long)hold_residual;
}

#ifdef CONFIG_MBX
int
do_ipaddrs(char **cmd_cp, int echo)
{
	char	*cp, *ip, ch;
	unsigned char	ipd;
	int	i, j, retval;

	/* We need to create the string:
	 *	<my_ip>:<serv_ip>
	 */
	cp = *cmd_cp;
	retval = 0;

	if ((cp - 9) >= cmd_line) {
		if (strncmp(cp - 9, "nfsaddrs=", 9) == 0) {
			ip = (char *)0xfa000060;
			retval = 1;
			for (j=0; j<2; j++) {
				for (i=0; i<4; i++) {
					ipd = *ip++;

					ch = ipd/100;
					if (ch) {
						ch += '0';
						if (echo)
							putc(ch);
						*cp++ = ch;
						ipd -= 100 * (ch - '0');
					}

					ch = ipd/10;
					if (ch) {
						ch += '0';
						if (echo)
							putc(ch);
						*cp++ = ch;
						ipd -= 10 * (ch - '0');
					}

					ch = ipd + '0';
					if (echo)
						putc(ch);
					*cp++ = ch;

					ch = '.';
					if (echo)
						putc(ch);
					*cp++ = ch;
				}

				/* At the end of the string, remove the
				 * '.' and replace it with a ':'.
				 */
				*(cp - 1) = ':';
				if (echo) {
					putc('\b'); putc(':');
				}
			}

			/* At the end of the second string, remove the
			 * '.' from both the command line and the
			 * screen.
			 */
			--cp;
			putc('\b'); putc(' '); putc('\b');
		}
	}
	*cmd_cp = cp;
	return(retval);
}

void
do_nfsroot(char **cmd_cp, char *dp)
{
	char	*cp, *rp, *ep;

	/* The boot argument (i.e /sys/mbxroot/zImage) is stored
	 * at offset 0x0078 in NVRAM.  We use this path name to
	 * construct the root file system path.
	 */
	cp = *cmd_cp;

	/* build command string.
	*/
	rp = nfsroot_string;
	while (*rp != 0)
		*cp++ = *rp++;

	/* Add the server address to the path.
	*/
	while (*dp != ' ')
		*cp++ = *dp++;
	*cp++ = ':';

	rp = (char *)0xfa000078;
	ep = strrchr(rp, '/');

	if (ep != 0) {
		while (rp < ep)
			*cp++ = *rp++;
	}
	else {
		rp = defroot_string;
		while (*rp != 0)
			*cp++ = *rp++;
	}

	*cmd_cp = cp;
}

size_t strlen(const char * s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}

int strncmp(const char * cs,const char * ct,size_t count)
{
	register signed char __res = 0;

	while (count) {
		if ((__res = *cs - *ct++) != 0 || !*cs++)
			break;
		count--;
	}

	return __res;
}

char * strrchr(const char * s, int c)
{
       const char *p = s + strlen(s);
       do {
           if (*p == (char)c)
               return (char *)p;
       } while (--p >= s);
       return NULL;
}
#endif

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

/*
 * misc.c
 * 
 * This is a collection of several routines from gzip-1.0.3 
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 * puts by Nick Holloway 1993
 *
 * Adapted for PowerPC by Gary Thomas
 */

#include "gzip.h"
#include "lzw.h"
#include "asm/residual.h"

RESIDUAL hold_residual;
void dump_buf(unsigned char *p, int s);
#define EOF -1

DECLARE(uch, inbuf, INBUFSIZ);
DECLARE(uch, outbuf, OUTBUFSIZ+OUTBUF_EXTRA);
DECLARE(uch, window, WSIZE);

unsigned outcnt;
unsigned insize;
unsigned inptr;

extern char input_data[];
extern int input_len;

int input_ptr;

int method, exit_code, part_nb, last_member;
int test = 0;
int force = 0;
int verbose = 1;
long bytes_in, bytes_out;

char *output_data;
unsigned long output_ptr;

extern int end;
long free_mem_ptr = (long)&end;

int to_stdout = 0;
int hard_math = 0;

void (*work)(int inf, int outf);
void makecrc(void);

local int get_method(int);

char *vidmem = (char *)0xC00B8000;
int lines, cols;
int orig_x, orig_y;

void puts(const char *);

void *malloc(int size)
{
	void *p;

	if (size <0) error("Malloc error\n");
	if (free_mem_ptr <= 0) error("Memory error\n");

   while(1) {
	free_mem_ptr = (free_mem_ptr + 3) & ~3;	/* Align */

	p = (void *)free_mem_ptr;
	free_mem_ptr += size;

	/*
  	 * The part of the compressed kernel which has already been expanded
	 * is no longer needed. Therefore we can reuse it for malloc.
	 * With bigger kernels, this is necessary.
	 */
          
	if (free_mem_ptr < (long)&end) {
		if (free_mem_ptr > (long)&input_data[input_ptr])
			error("\nOut of memory\n");

		return p;
	}
#if 0	
	if (free_mem_ptr < 0x90000)
#endif	
	return p;
	puts("large kernel, low 1M tight...");
	free_mem_ptr = (long)input_data;
	}
}

void free(void *where)
{	/* Don't care */
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

__ptr_t memset(__ptr_t s, int c, size_t n)
{
	int i;
	char *ss = (char*)s;

	for (i=0;i<n;i++) ss[i] = c;
}

__ptr_t memcpy(__ptr_t __dest, __const __ptr_t __src,
			    size_t __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i=0;i<__n;i++) d[i] = s[i];
}

int memcmp(__ptr_t __dest, __const __ptr_t __src,
			    size_t __n)
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

extern ulg crc_32_tab[];   /* crc table, defined below */

/* ===========================================================================
 * Run a set of bytes through the crc shift register.  If s is a NULL
 * pointer, then initialize the crc shift register contents instead.
 * Return the current crc in either case.
 */
ulg updcrc(s, n)
    uch *s;                 /* pointer to bytes to pump through */
    unsigned n;             /* number of bytes in s[] */
{
    register ulg c;         /* temporary variable */

    static ulg crc = (ulg)0xffffffffL; /* shift register contents */

    if (s == NULL) {
	c = 0xffffffffL;
    } else {
	c = crc;
	while (n--) {
	    c = crc_32_tab[((int)c ^ (*s++)) & 0xff] ^ (c >> 8);
	}
    }
    crc = c;
    return c ^ 0xffffffffL;       /* (instead of ~c for 64-bit machines) */
}

/* ===========================================================================
 * Clear input and output buffers
 */
void clear_bufs()
{
    outcnt = 0;
    insize = inptr = 0;
    bytes_in = bytes_out = 0L;
}

/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
int fill_inbuf()
{
    int len, i;

    /* Read as much as possible */
puts("*");    
    insize = 0;
    do {
	len = INBUFSIZ-insize;
	if (len > (input_len-input_ptr+1)) len=input_len-input_ptr+1;
        if (len == 0 || len == EOF) break;

        for (i=0;i<len;i++) inbuf[insize+i] = input_data[input_ptr+i];
	insize += len;
	input_ptr += len;
    } while (insize < INBUFSIZ);

    if (insize == 0) {
	error("unable to fill buffer\n");
    }
    bytes_in += (ulg)insize;
    inptr = 1;
    return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
void flush_window()
{
    if (outcnt == 0) return;
    updcrc(window, outcnt);

    memcpy(&output_data[output_ptr], (char *)window, outcnt);

    bytes_out += (ulg)outcnt;
    output_ptr += (ulg)outcnt;
    outcnt = 0;
}

/*
 * Code to compute the CRC-32 table. Borrowed from 
 * gzip-1.0.3/makecrc.c.
 */

ulg crc_32_tab[256];

void
makecrc(void)
{
/* Not copyrighted 1990 Mark Adler	*/

  unsigned long c;      /* crc shift register */
  unsigned long e;      /* polynomial exclusive-or pattern */
  int i;                /* counter for all possible eight bit values */
  int k;                /* byte being shifted into crc apparatus */

  /* terms of polynomial defining this crc (except x^32): */
  static int p[] = {0,1,2,4,5,7,8,10,11,12,16,22,23,26};

  /* Make exclusive-or pattern from polynomial */
  e = 0;
  for (i = 0; i < sizeof(p)/sizeof(int); i++)
    e |= 1L << (31 - p[i]);

  crc_32_tab[0] = 0;

  for (i = 1; i < 256; i++)
  {
    c = 0;
    for (k = i | 256; k != 1; k >>= 1)
    {
      c = c & 1 ? (c >> 1) ^ e : c >> 1;
      if (k & 1)
        c ^= e;
    }
    crc_32_tab[i] = c;
  }
}

void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while(1);	/* Halt */
}

unsigned long
decompress_kernel(unsigned long load_addr, int num_words, unsigned long cksum, RESIDUAL *residual)
{
  unsigned long TotalMemory;
  output_data = (char *)0x0;	/* Points to 0 */
  lines = 25;
  cols = 80;
  orig_x = 0;
  orig_y = 24;

  
  /* Turn off MMU.  Since we are mapped 1-1, this is OK. */
  flush_instruction_cache();
  _put_HID0(_get_HID0() & ~0x0000C000);
  _put_MSR(_get_MSR() & ~0x0030);

  vga_init(0xC0000000);
  clear_screen();

  output_ptr = 0;

  exit_code = 0;
  test = 0;
  input_ptr = 0;
  part_nb = 0;

  clear_bufs();
  makecrc();

  puts("Loaded at ");  puthex(load_addr);  puts(", ");  puthex(num_words);  puts(" words");
  puts(", cksum = ");  puthex(cksum);  puts("\n");
  if (residual) {
    _bcopy(residual, &hold_residual, sizeof(hold_residual));
    puts("Residual data at ");  puthex(residual);  puts("\n");
    show_residual_data(residual);
    TotalMemory = residual->TotalMemory;
  } else {
    TotalMemory = 0x01000000;
  }

  puts("Uncompressing Linux...");

  method = get_method(0);

  work(0, 0);

  puts("done.\n");
  puts("Now booting the kernel\n");
  /*return (TotalMemory);*/              /* Later this can be a pointer to saved residual data */
  return &hold_residual;
}

show_residual_data(RESIDUAL *res)
{
  puts("Residual data: ");  puthex(res->ResidualLength);  puts(" bytes\n");
  puts("Total memory: ");  puthex(res->TotalMemory);  puts("\n");
#if 0
  puts("Residual structure = ");  puthex(sizeof(*res));  puts(" bytes\n");
  dump_buf(&hold_residual, 32);
  dump_buf(res, 32);
#endif
}

#if 0
verify_ram()
{
  unsigned long loc;
  puts("Clearing memory:");
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    *(unsigned long *)loc = 0x0;
  }
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    if (*(unsigned long *)loc != 0x0)
      {
	puts(" - failed at ");
	puthex(loc);
	puts(": ");
	puthex(*(unsigned long *)loc);
	while (1);
      }
  }
  puts("0");
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    *(unsigned long *)loc = 0xFFFFFFFF;
  }
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    if (*(unsigned long *)loc != 0xFFFFFFFF)
      {
	puts(" - failed at ");
	puthex(loc);
	puts(": ");
	puthex(*(unsigned long *)loc);
	while (1);
      }
  }
  puts("1");
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    *(unsigned long *)loc = loc;
  }
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    if (*(unsigned long *)loc != loc)
      {
	puts(" - failed at ");
	puthex(loc);
	puts(": ");
	puthex(*(unsigned long *)loc);
	while (1);
      }
  }
  puts("?");
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    *(unsigned long *)loc = 0xDEADB00B;
  }
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    if (*(unsigned long *)loc != 0xDEADB00B)
      {
	puts(" - failed at ");
	puthex(loc);
	puts(": ");
	puthex(*(unsigned long *)loc);
	while (1);
      }
  }
  puts(">");
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    *(unsigned long *)loc = 0x0;
  }
  for (loc = 0;  loc <= 0x400000;  loc += 4);
  {
    if (*(unsigned long *)loc != 0x0)
      {
	puts(" - failed at ");
	puthex(loc);
	puts(": ");
	puthex(*(unsigned long *)loc);
	while (1);
      }
  }
  puts("\n");
}

do_cksum(unsigned long loc)
{
  unsigned int ptr, cksum;
  puts("cksum[");  puthex(loc);  puts("] = ");  
  cksum = 0;
  for (ptr = loc;  ptr < (loc+0x40000);  ptr += 4)
    {
      cksum ^= *(unsigned long *)ptr;
    }
  puthex(cksum);  puts("  ");
  cksum = 0;  loc += 0x40000;
  for (ptr = loc;  ptr < (loc+0x40000);  ptr += 4)
    {
      cksum ^= *(unsigned long *)ptr;
    }
  puthex(cksum);  puts("  ");
  cksum = 0;  loc += 0x40000;
  for (ptr = loc;  ptr < (loc+0x40000);  ptr += 4)
    {
      cksum ^= *(unsigned long *)ptr;
    }
  puthex(cksum);  puts("  ");
  cksum = 0;  loc += 0x40000;
  for (ptr = loc;  ptr < (loc+0x40000);  ptr += 4)
    {
      cksum ^= *(unsigned long *)ptr;
    }
  puthex(cksum);  puts("\n");
}

cksum_data()
{
  unsigned int *ptr, len, cksum, cnt;
  cksum = cnt = 0;
  ptr = input_data;
  puts("Checksums: ");
  for (len = 0;  len < input_len;  len += 4) {
    cksum ^= *ptr++;
    if (len && ((len & 0x0FFF) == 0)) {
      if (cnt == 0) {
	puts("\n  [");
	puthex(ptr-1);
	puts("] ");
      }
      puthex(cksum);
      if (++cnt == 6) {
	cnt = 0;
      } else {
	puts(" ");
      }
    }
  }
  puts("\n");
  puts("Data cksum = ");  puthex(cksum);  puts("\n");
}

cksum_text()
{
  extern int start, etext;
  unsigned int *ptr, len, text_len, cksum, cnt;
  cksum = cnt = 0;
  ptr = &start;
  text_len = (unsigned int)&etext - (unsigned int)&start;
  puts("Checksums: ");
  for (len = 0;  len < text_len;  len += 4) {
    cksum ^= *ptr++;
    if (len && ((len & 0x0FFF) == 0)) {
      if (cnt == 0) {
	puts("\n  [");
	puthex(ptr-1);
	puts("] ");
      }
      puthex(cksum);
      if (++cnt == 6) {
	cnt = 0;
      } else {
	puts(" ");
      }
    }
  }
  puts("\n");
  puts("TEXT cksum = ");  puthex(cksum);  puts("\n");
}

verify_data(unsigned long load_addr)
{
  extern int start, etext;
  unsigned int *orig_ptr, *copy_ptr, len, errors;
  errors = 0;
  copy_ptr = input_data;
  orig_ptr = (unsigned int *)(load_addr + ((unsigned int)input_data - (unsigned int)&start));
  for (len = 0;  len < input_len;  len += 4) {
    if (*copy_ptr++ != *orig_ptr++) {
      errors++;
    }    
  }
  copy_ptr = input_data;
  orig_ptr = (unsigned int *)(load_addr + ((unsigned int)input_data - (unsigned int)&start));
  for (len = 0;  len < input_len;  len += 4) {
    if (*copy_ptr++ != *orig_ptr++) {
      dump_buf(copy_ptr-1, 128);
      dump_buf(orig_ptr-1, 128);
      puts("Total errors = ");  puthex(errors*4);  puts("\n");
      while (1) ;
    }    
  }
}

test_data(unsigned long load_addr)
{
  extern int start, etext;
  unsigned int *orig_ptr, *copy_ptr, len, errors;
  errors = 0;
  copy_ptr = input_data;
  orig_ptr = (unsigned int *)(load_addr + ((unsigned int)input_data - (unsigned int)&start));
  for (len = 0;  len < input_len;  len += 4) {
    if (*copy_ptr++ != *orig_ptr++) {
      errors++;
    }    
  }
  return (errors == 0);
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

#if 1
void puthex2(unsigned long val)
{
  unsigned char buf[4];
  int i;
  for (i = 1;  i >= 0;  i--)
    {
      buf[i] = "0123456789ABCDEF"[val & 0x0F];
      val >>= 4;
    }
  buf[2] = '\0';
  puts(buf);
}

void dump_buf(unsigned char *p, int s)
{
   int i, c;
   if ((unsigned int)s > (unsigned int)p)
   {
   	s = (unsigned int)s - (unsigned int)p;
   }
   while (s > 0)
   {
      puthex(p);  puts(": ");
      for (i = 0;  i < 16;  i++)
      {
         if (i < s)
         {
            puthex2(p[i] & 0xFF);
         } else
         {
            puts("  ");
         }
         if ((i % 2) == 1) puts(" ");
         if ((i % 8) == 7) puts(" ");
      }
      puts(" |");
      for (i = 0;  i < 16;  i++)
      {
	 char buf[2];
         if (i < s)
         {
            c = p[i] & 0xFF;
            if ((c < 0x20) || (c >= 0x7F)) c = '.';
         } else
         {
            c = ' ';
         }
	 buf[0] = c;  buf[1] = '\0';
         puts(buf);
      }
      puts("|\n");
      s -= 16;
      p += 16;
   }
}
#endif

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

/* ========================================================================
 * Check the magic number of the input file and update ofname if an
 * original name was given and to_stdout is not set.
 * Return the compression method, -1 for error, -2 for warning.
 * Set inptr to the offset of the next byte to be processed.
 * This function may be called repeatedly for an input file consisting
 * of several contiguous gzip'ed members.
 * IN assertions: there is at least one remaining compressed member.
 *   If the member is a zip file, it must be the only one.
 */
local int get_method(in)
    int in;        /* input file descriptor */
{
    uch flags;
    char magic[2]; /* magic header */

    magic[0] = (char)get_byte();
    magic[1] = (char)get_byte();

    method = -1;                 /* unknown yet */
    part_nb++;                   /* number of parts in gzip file */
    last_member = 0;
    /* assume multiple members in gzip file except for record oriented I/O */

    if (memcmp(magic, GZIP_MAGIC, 2) == 0
        || memcmp(magic, OLD_GZIP_MAGIC, 2) == 0) {

	work = unzip;
	method = (int)get_byte();
	flags  = (uch)get_byte();
	if ((flags & ENCRYPTED) != 0)
	    error("Input is encrypted\n");
	if ((flags & CONTINUATION) != 0)
	       error("Multi part input\n");
	if ((flags & RESERVED) != 0) {
	    error("Input has invalid flags\n");
	    exit_code = ERROR;
	    if (force <= 1) return -1;
	}
	(ulg)get_byte();	/* Get timestamp */
	((ulg)get_byte()) << 8;
	((ulg)get_byte()) << 16;
	((ulg)get_byte()) << 24;

	(void)get_byte();  /* Ignore extra flags for the moment */
	(void)get_byte();  /* Ignore OS type for the moment */

	if ((flags & EXTRA_FIELD) != 0) {
	    unsigned len = (unsigned)get_byte();
	    len |= ((unsigned)get_byte())<<8;
	    while (len--) (void)get_byte();
	}

	/* Get original file name if it was truncated */
	if ((flags & ORIG_NAME) != 0) {
	    if (to_stdout || part_nb > 1) {
		/* Discard the old name */
		while (get_byte() != 0) /* null */ ;
	    } else {
	    } /* to_stdout */
	} /* orig_name */

	/* Discard file comment if any */
	if ((flags & COMMENT) != 0) {
	    while (get_byte() != 0) /* null */ ;
	}
    } else
	error("unknown compression method");
    return method;
}

void
_bcopy(char *src, char *dst, int len)
{
  while (len--) *dst++ = *src++;
}

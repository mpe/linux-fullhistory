/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * Updates for PPC64 by Todd Inglett & Dave Engebretsen.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#define __KERNEL__
#include "zlib.h"
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/bootinfo.h>

void memmove(void *dst, void *im, int len);

extern void *finddevice(const char *);
extern int getprop(void *, const char *, void *, int);
extern void printf(const char *fmt, ...);
extern int sprintf(char *buf, const char *fmt, ...);
void gunzip(void *, int, unsigned char *, int *);
void *claim(unsigned int, unsigned int, unsigned int);
void flush_cache(void *, int);
void pause(void);
static struct bi_record *make_bi_recs(unsigned long);

#define RAM_START	0x00000000
#define RAM_END		(64<<20)

#define BOOT_START	((unsigned long)_start)
#define BOOT_END	((unsigned long)_end)

/* Value picked to match that used by yaboot */
#define PROG_START	0x01400000

char *avail_ram;
char *begin_avail, *end_avail;
char *avail_high;
unsigned int heap_use;
unsigned int heap_max;
unsigned long initrd_start = 0;
unsigned long initrd_size = 0;

extern char _end[];
extern char image_data[];
extern int image_len;
extern char initrd_data[];
extern int initrd_len;
extern char sysmap_data[];
extern int sysmap_len;
extern int uncompressed_size;
extern long vmlinux_end;

static char scratch[128<<10];	/* 128kB of scratch space for gunzip */

typedef void (*kernel_entry_t)( unsigned long,
                                unsigned long,
                                void *,
				struct bi_record *);

void
chrpboot(unsigned long a1, unsigned long a2, void *prom)
{
	unsigned len;
	void *dst = (void *)-1;
	unsigned long claim_addr;
	unsigned char *im;
	extern char _start;
	struct bi_record *bi_recs;
	kernel_entry_t kernel_entry;
    
	printf("chrpboot starting: loaded at 0x%x\n\r", (unsigned)&_start);

	if (initrd_len) {
		initrd_size = initrd_len;
		initrd_start = (RAM_END - initrd_size) & ~0xFFF;
		a1 = a2 = 0;
		claim(initrd_start, RAM_END - initrd_start, 0);
		printf("initial ramdisk moving 0x%lx <- 0x%lx (%lx bytes)\n\r",
		       initrd_start, (unsigned long)initrd_data, initrd_size);
		memcpy((void *)initrd_start, (void *)initrd_data, initrd_size);
	}

	im = image_data;
	len = image_len;
	uncompressed_size = PAGE_ALIGN(uncompressed_size);

	for(claim_addr = PROG_START; 
	    claim_addr <= PROG_START * 8; 
	    claim_addr += 0x100000) {
		printf("    trying: 0x%08lx\n\r", claim_addr);
		dst = claim(claim_addr, uncompressed_size, 0);
		if (dst != (void *)-1) break;
	}
	if (dst == (void *)-1) {
		printf("claim error, can't allocate kernel memory\n\r");
		return;
	}

	if (im[0] == 0x1f && im[1] == 0x8b) {
		avail_ram = scratch;
		begin_avail = avail_high = avail_ram;
		end_avail = scratch + sizeof(scratch);
		printf("gunzipping (0x%x <- 0x%x:0x%0x)...",
		       (unsigned)dst, (unsigned)im, (unsigned)im+len);
		gunzip(dst, uncompressed_size, im, &len);
		printf("done %u bytes\n\r", len);
		printf("%u bytes of heap consumed, max in use %u\n\r",
		       (unsigned)(avail_high - begin_avail), heap_max);
	} else {
		memmove(dst, im, len);
	}

	flush_cache(dst, len);

	bi_recs = make_bi_recs((unsigned long)dst + vmlinux_end);

	kernel_entry = (kernel_entry_t)dst;
	printf( "kernel:\n\r"
		"        entry addr = 0x%lx\n\r"
		"        a1         = 0x%lx,\n\r"
		"        a2         = 0x%lx,\n\r"
		"        prom       = 0x%lx,\n\r"
		"        bi_recs    = 0x%lx,\n\r",
		(unsigned long)kernel_entry, a1, a2,
		(unsigned long)prom, (unsigned long)bi_recs);

	kernel_entry( a1, a2, prom, bi_recs );

	printf("returned?\n\r");

	pause();
}

static struct bi_record *
make_bi_recs(unsigned long addr)
{
	struct bi_record *bi_recs;
	struct bi_record *rec;

	bi_recs = rec = bi_rec_init(addr);

	rec = bi_rec_alloc(rec, 2);
	rec->tag = BI_FIRST;
	/* rec->data[0] = ...;	# Written below before return */
	/* rec->data[1] = ...;	# Written below before return */

	rec = bi_rec_alloc_bytes(rec, strlen("chrpboot")+1);
	rec->tag = BI_BOOTLOADER_ID;
	sprintf( (char *)rec->data, "chrpboot");

	rec = bi_rec_alloc(rec, 2);
	rec->tag = BI_MACHTYPE;
	rec->data[0] = _MACH_pSeries;
	rec->data[1] = 1;

	if ( initrd_size > 0 ) {
		rec = bi_rec_alloc(rec, 2);
		rec->tag = BI_INITRD;
		rec->data[0] = initrd_start;
		rec->data[1] = initrd_size;
	}

#if 0
	if ( sysmap_len > 0 ) {
		rec = bi_rec_alloc(rec, 2);
		rec->tag = BI_SYSMAP;
		rec->data[0] = (unsigned long)sysmap_data;
		rec->data[1] = sysmap_len;
	}
#endif

	rec = bi_rec_alloc(rec, 1);
	rec->tag = BI_LAST;
	rec->data[0] = (bi_rec_field)bi_recs;

	/* Save the _end_ address of the bi_rec's in the first bi_rec
	 * data field for easy access by the kernel.
	 */
	bi_recs->data[0] = (bi_rec_field)rec;
	bi_recs->data[1] = (bi_rec_field)rec + rec->size - (bi_rec_field)bi_recs;

	return bi_recs;
}

struct memchunk {
	unsigned int size;
	unsigned int pad;
	struct memchunk *next;
};

static struct memchunk *freechunks;

void *zalloc(void *x, unsigned items, unsigned size)
{
	void *p;
	struct memchunk **mpp, *mp;

	size *= items;
	size = _ALIGN(size, sizeof(struct memchunk));
	heap_use += size;
	if (heap_use > heap_max)
		heap_max = heap_use;
	for (mpp = &freechunks; (mp = *mpp) != 0; mpp = &mp->next) {
		if (mp->size == size) {
			*mpp = mp->next;
			return mp;
		}
	}
	p = avail_ram;
	avail_ram += size;
	if (avail_ram > avail_high)
		avail_high = avail_ram;
	if (avail_ram > end_avail) {
		printf("oops... out of memory\n\r");
		pause();
	}
	return p;
}

void zfree(void *x, void *addr, unsigned nb)
{
	struct memchunk *mp = addr;

	nb = _ALIGN(nb, sizeof(struct memchunk));
	heap_use -= nb;
	if (avail_ram == addr + nb) {
		avail_ram = addr;
		return;
	}
	mp->size = nb;
	mp->next = freechunks;
	freechunks = mp;
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
		printf("bad gzipped data\n\r");
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
		printf("gunzip: ran out of data in header\n\r");
		exit();
	}

	s.zalloc = zalloc;
	s.zfree = zfree;
	r = inflateInit2(&s, -MAX_WBITS);
	if (r != Z_OK) {
		printf("inflateInit2 returned %d\n\r", r);
		exit();
	}
	s.next_in = src + i;
	s.avail_in = *lenp - i;
	s.next_out = dst;
	s.avail_out = dstlen;
	r = inflate(&s, Z_FINISH);
	if (r != Z_OK && r != Z_STREAM_END) {
		printf("inflate returned %d msg: %s\n\r", r, s.msg);
		exit();
	}
	*lenp = s.next_out - (unsigned char *) dst;
	inflateEnd(&s);
}


/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include "../coffboot/nonstdio.h"
#include "../coffboot/zlib.h"

extern void *finddevice(const char *);
extern int getprop(void *, const char *, void *, int);
void gunzip(void *, int, unsigned char *, int *);

#define get_16be(x)	(*(unsigned short *)(x))
#define get_32be(x)	(*(unsigned *)(x))

#define RAM_START	0x00000000
#define RAM_END		0x00800000	/* only 8M mapped with BATs */

#define RAM_FREE	0x00540000	/* after image of chrpboot */
#define PROG_START	0x00010000

char *avail_ram;
char *end_avail;

extern char image_data[];
extern int image_len;
extern char initrd_data[];
extern int initrd_len;


chrpboot(int a1, int a2, void *prom)
{
    int ns, oh, i;
    unsigned sa, len;
    void *dst;
    unsigned char *im;
    unsigned initrd_start, initrd_size;
    
    printf("chrpboot starting\n\r");
    /* setup_bats(); */

    if (initrd_len) {
	initrd_size = initrd_len;
	initrd_start = (RAM_END - initrd_size) & ~0xFFF;
	a1 = initrd_start;
	a2 = initrd_size;
	printf("initial ramdisk at %x (%u bytes)\n\r", initrd_start,
	       initrd_size);
	memcpy((char *)initrd_start, initrd_data, initrd_size);
	end_avail = (char *)initrd_start;
    } else
	end_avail = (char *) RAM_END;
    im = image_data;
    len = image_len;
    dst = (void *) PROG_START;

    if (im[0] == 0x1f && im[1] == 0x8b) {
	void *cp = (void *) RAM_FREE;
	avail_ram = (void *) (RAM_FREE + ((len + 7) & -8));
	memcpy(cp, im, len);
	printf("gunzipping... ");
	gunzip(dst, 0x400000, cp, &len);
	printf("done\n\r");

    } else {
	memmove(dst, im, len);
    }

    flush_cache(dst, len);

    sa = PROG_START+12;
    printf("start address = 0x%x\n\r", sa);

#if 0
    pause();
#endif
    (*(void (*)())sa)(a1, a2, prom, 0, 0);

    printf("returned?\n\r");

    pause();
}

void *zalloc(void *x, unsigned items, unsigned size)
{
    void *p = avail_ram;

    size *= items;
    size = (size + 7) & -8;
    avail_ram += size;
    if (avail_ram > end_avail) {
	printf("oops... out of memory\n\r");
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
	printf("inflate returned %d\n\r", r);
	exit();
    }
    *lenp = s.next_out - (unsigned char *) dst;
    inflateEnd(&s);
}

/*
 * uncompress.c
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * cramfs interfaces to the uncompression library. There's really just
 * three entrypoints:
 *
 *  - cramfs_uncompress_init() - called to initialize the thing.
 *  - cramfs_uncompress_exit() - tell me when you're done
 *  - cramfs_uncompress_block() - uncompress a block.
 *
 * NOTE NOTE NOTE! The uncompression is entirely single-threaded. We
 * only have one stream, and we'll initialize it only once even if it
 * then is used by multiple filesystems.
 */

#include <linux/kernel.h>

#include "inflate/zlib.h"

static z_stream stream;
static int initialized = 0;

int cramfs_uncompress_block(void *dst, int dstlen, void *src, int srclen)
{
	int err;

	stream.next_in = src;
	stream.avail_in = srclen;

	stream.next_out = dst;
	stream.avail_out = dstlen;

	inflateReset(&stream);

	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		printk("Error %d while decompressing!\n", err);
		printk("%p(%d)->%p(%d)\n", src, srclen, dst, dstlen);
	}
	return stream.total_out;
}

int cramfs_uncompress_init(void)
{
	if (!initialized++) {
		stream.next_in = NULL;
		stream.avail_in = 0;
		inflateInit(&stream);
	}
	return 0;
}

int cramfs_uncompress_exit(void)
{
	if (!--initialized)
		inflateEnd(&stream);
	return 0;
}

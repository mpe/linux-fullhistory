/*
 * csum_partial_copy - do IP checksumming and copy
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Don't look at this too closely - you'll go mad. The things
 * we do for performance..
 */

#define ldq_u(x,y) \
__asm__("ldq_u %0,%1":"=r" (x):"m" (*(unsigned long *)(y)))

#define stq_u(x,y) \
__asm__("stq_u %1,%0":"=m" (*(unsigned long *)(y)):"r" (x))

#define extql(x,y,z) \
__asm__ __volatile__("extql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define extqh(x,y,z) \
__asm__ __volatile__("extqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define mskql(x,y,z) \
__asm__ __volatile__("mskql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define mskqh(x,y,z) \
__asm__ __volatile__("mskqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define insql(x,y,z) \
__asm__ __volatile__("insql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define insqh(x,y,z) \
__asm__ __volatile__("insqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))

/*
 * Ok. This isn't fun, but this is the EASY case.
 */
static inline unsigned long csum_partial_copy_aligned(
	unsigned long *src, unsigned long *dst,
	long len, unsigned long checksum)
{
	unsigned long word, carry = 0;

	len -= 8;
	word = *src;
	while (len >= 0) {
		checksum += carry;
		src++;
		checksum += word;
		len -= 8;
		carry = checksum < word;
		*dst = word;
		word = *src;
		dst++;
	}
	len += 8;
	checksum += carry;
	if (len) {
		unsigned long tmp = *dst;
		mskql(word, len, word);
		checksum += word;
		mskqh(tmp, len, tmp);
		carry = checksum < word;
		*dst = word | tmp;
		checksum += carry;
	}
	return checksum;	
}

/*
 * This is even less fun, but this is still reasonably
 * easy.
 */
static inline unsigned long csum_partial_copy_dest_aligned(
	unsigned long *src, unsigned long *dst,
	unsigned long soff,
	long len, unsigned long checksum)
{
	unsigned long first, word, carry = 0;

	len -= 8;
	first = src[0];
	while (len >= 0) {
		unsigned long second;

		second = src[1];
		extql(first, soff, word);
		len -= 8;
		extqh(second, soff, first);
		src++;
		word |= first;
		checksum += carry;
		first = second;
		checksum += word;
		*dst = word;
		carry = checksum < word;
		dst++;
	}
	len += 8;
	checksum += carry;
	if (len) {
		unsigned long tmp;
		unsigned long second;
		second = src[1];
		tmp = *dst;
		extql(first, soff, word);
		extqh(second, soff, first);
		word |= first;
		mskql(word, len, word);
		checksum += word;
		mskqh(tmp, len, tmp);
		carry = checksum < word;
		*dst = word | tmp;
		checksum += carry;
	}
	return checksum;
}

/*
 * This is slightly less fun than the above..
 */
static inline unsigned long csum_partial_copy_src_aligned(
	unsigned long *src, unsigned long *dst,
	unsigned long doff,
	long len, unsigned long checksum)
{
	unsigned long word, carry = 0;
	unsigned long partial_dest;

	partial_dest = *dst;
	len -= 8;
	mskql(partial_dest, doff, partial_dest);
	word = *src;
	while (len >= 0) {
		unsigned long second_dest;

		len -= 8;
		checksum += carry;
		src++;
		checksum += word;
		insql(word, doff, second_dest);
		*dst = partial_dest | second_dest;
		insqh(word, doff, partial_dest);
		carry = checksum < word;
		word = *src;
		dst++;
	}
	len += doff;
	checksum += carry;
	if (len >= 0) {
		unsigned long second_dest;

		mskql(word, len-doff, word);
		len -= 8;
		src++;
		checksum += word;
		insql(word, doff, second_dest);
		*dst = partial_dest | second_dest;
		insqh(word, doff, partial_dest);
		carry = checksum < word;
		word = *src;
		dst++;
		checksum += carry;
	} else if (len & 7) {
		unsigned long second_dest;
		second_dest = *dst;
		mskql(word, len-doff, word);
		checksum += word;
		mskqh(second_dest, len, second_dest);
		carry = checksum < word;
		insql(word, doff, word);
		*dst = partial_dest | word | second_dest;
		checksum += carry;
	}
	return checksum;
}

/*
 * This is so totally un-fun that it's frightening. Don't
 * look at this too closely, you'll go blind.
 */
static inline unsigned long csum_partial_copy_unaligned(
	unsigned long * src, unsigned long * dst,
	unsigned long soff, unsigned long doff,
	long len, unsigned long checksum)
{
	unsigned long first, carry = 0;
	unsigned long partial_dest;

	partial_dest = dst[0];
	len -= 8;
	first = src[0];
	mskql(partial_dest, doff, partial_dest);
	while (len >= 0) {
		unsigned long second, word;
		unsigned long second_dest;

		second = src[1];
		extql(first, soff, word);
		len -= 8;
		checksum += carry;
		src++;
		extqh(second, soff, first);
		word |= first;
		first = second;
		checksum += word;
		insql(word, doff, second_dest);
		*dst = partial_dest | second_dest;
		carry = checksum < word;
		insqh(word, doff, partial_dest);
		dst++;
	}
	len += doff;
	checksum += carry;
	if (len >= 0) {
		unsigned long second, word;
		unsigned long second_dest;

		second = src[1];
		extql(first, soff, word);
		len -= 8;
		src++;
		extqh(second, soff, first);
		word |= first;
		first = second;
		mskql(word, len-doff, word);
		checksum += word;
		insql(word, doff, second_dest);
		*dst = partial_dest | second_dest;
		carry = checksum < word;
		insqh(word, doff, partial_dest);
		dst++;
	} else if (len & 7) {
		unsigned long second, word;
		unsigned long second_dest;
		second = src[1];
		extql(first, soff, word);
		extqh(second, soff, first);
		word |= first;
		second_dest = *dst;
		mskql(word, len-doff, word);
		checksum += word;
		mskqh(second_dest, len, second_dest);
		carry = checksum < word;
		insql(word, doff, word);
		*dst = partial_dest | word | second_dest;
		checksum += carry;
	}
	return checksum;
}

unsigned int csum_partial_copy(char *src, char *dst, int len, int sum)
{
	unsigned long checksum = (unsigned) sum;
	unsigned long soff = 7 & (unsigned long) src;
	unsigned long doff = 7 & (unsigned long) dst;

	src = (char *) (~7UL & (unsigned long) src);
	dst = (char *) (~7UL & (unsigned long) dst);
	if (len) {
		if (!soff) {
			if (!doff)
				checksum = csum_partial_copy_aligned(
					(unsigned long *) src,
					(unsigned long *) dst,
					len, checksum);
			else
				checksum = csum_partial_copy_src_aligned(
					(unsigned long *) src,
					(unsigned long *) dst,
					doff, len, checksum);
		} else {
			if (!doff)
				checksum = csum_partial_copy_dest_aligned(
					(unsigned long *) src,
					(unsigned long *) dst,
					soff, len, checksum);
			else
				checksum = csum_partial_copy_unaligned(
					(unsigned long *) src,
					(unsigned long *) dst,
					soff, doff, len, checksum);
		}
		/* 64 -> 33 bits */
		checksum = (checksum & 0xffffffff) + (checksum >> 32);
		/* 33 -> < 32 bits */
		checksum = (checksum & 0xffff) + (checksum >> 16);
		/* 32 -> 16 bits */
		checksum = (checksum & 0xffff) + (checksum >> 16);
		checksum = (checksum & 0xffff) + (checksum >> 16);
	}
	return checksum;
}

/*
 *  linux/fs/hfsplus/unicode.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Handler routines for unicode strings
 */

#include <linux/types.h>
#include <linux/nls.h>
#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

/* Fold the case of a unicode char, given the 16 bit value */
/* Returns folded char, or 0 if ignorable */
static inline u16 case_fold(u16 c)
{
        u16 tmp;

        tmp = case_fold_table[(c>>8)];
        if (tmp)
                tmp = case_fold_table[tmp + (c & 0xFF)];
        else
                tmp = c;
        return tmp;
}

/* Compare unicode strings, return values like normal strcmp */
int hfsplus_unistrcmp(const struct hfsplus_unistr *s1, const struct hfsplus_unistr *s2)
{
	u16 len1, len2, c1, c2;
	const hfsplus_unichr *p1, *p2;

	len1 = be16_to_cpu(s1->length);
	len2 = be16_to_cpu(s2->length);
	p1 = s1->unicode;
	p2 = s2->unicode;

	while (1) {
		c1 = c2 = 0;

		while (len1 && !c1) {
			c1 = case_fold(be16_to_cpu(*p1));
			p1++;
			len1--;
		}
		while (len2 && !c2) {
			c2 = case_fold(be16_to_cpu(*p2));
			p2++;
			len2--;
		}

		if (c1 != c2)
			return (c1 < c2) ? -1 : 1;
		if (!c1 && !c2)
			return 0;
	}
}

int hfsplus_uni2asc(struct super_block *sb, const struct hfsplus_unistr *ustr, char *astr, int *len_p)
{
	const hfsplus_unichr *ip;
	struct nls_table *nls = HFSPLUS_SB(sb).nls;
	u8 *op;
	u16 ustrlen, cc;
	int size, len;

	op = astr;
	ip = ustr->unicode;
	ustrlen = be16_to_cpu(ustr->length);
	len = *len_p;
	while (ustrlen > 0 && len > 0) {
		cc = be16_to_cpu(*ip++);
		switch (cc) {
		case 0:
			cc = 0x2400;
			break;
		case '/':
			cc = ':';
			break;
		}
		size = nls->uni2char(cc, op, len);
		if (size <= 0) {
			*op = '?';
			size = 1;
		}
		op += size;
		len -= size;
		ustrlen--;
	}
	*len_p = (char *)op - astr;
	if (ustrlen)
		return -ENAMETOOLONG;
	return 0;
}

int hfsplus_asc2uni(struct super_block *sb, struct hfsplus_unistr *ustr, const char *astr, int len)
{
	struct nls_table *nls = HFSPLUS_SB(sb).nls;
	int size;
	wchar_t c;
	u16 outlen = 0;

	while (outlen <= HFSPLUS_MAX_STRLEN && len > 0) {
		size = nls->char2uni(astr, len, &c);
		if (size <= 0) {
			c = '?';
			size = 1;
		}
		astr += size;
		len -= size;
		switch (c) {
		case 0x2400:
			c = 0;
			break;
		case ':':
			c = '/';
			break;
		}
		ustr->unicode[outlen] = cpu_to_be16(c);
		outlen++;
	}
	ustr->length = cpu_to_be16(outlen);
	if (len > 0)
		return -ENAMETOOLONG;
	return 0;
}

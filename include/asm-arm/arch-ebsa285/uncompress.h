/*
 * linux/include/asm-arm/arch-ebsa110/uncompress.h
 *
 * Copyright (C) 1996,1997,1998 Russell King
 */

#define BASE 0x42000160

static __inline__ void putc(char c)
{
	while (*((volatile unsigned int *)(BASE + 0x18)) & 8);
	*((volatile unsigned int *)(BASE)) = c;
}

/*
 * This does not append a newline
 */
static void puts(const char *s)
{
	while (*s) {
		putc(*s);
		if (*s == '\n')
			putc('\r');
		s++;
	}
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()

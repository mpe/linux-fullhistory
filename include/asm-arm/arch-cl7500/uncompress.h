/*
 * linux/include/asm-arm/arch-cl7500/uncompress.h
 *
 * Copyright (C) 1999 Nexus Electronics Ltd.
 */

#define BASE 0x03010000

static __inline__ void putc(char c)
{
	while (!(*((volatile unsigned int *)(BASE + 0xbf4)) & 0x20));
	*((volatile unsigned int *)(BASE + 0xbe0)) = c;
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

static __inline__ void arch_decomp_setup(void)
{
	int baud = 3686400 / (9600 * 16);

	*((volatile unsigned int *)(BASE + 0xBEC)) = 0x80;
	*((volatile unsigned int *)(BASE + 0xBE0)) = baud & 0xff;
	*((volatile unsigned int *)(BASE + 0xBE4)) = (baud & 0xff00) >> 8;
	*((volatile unsigned int *)(BASE + 0xBEC)) = 3; /* 8 bits */
	*((volatile unsigned int *)(BASE + 0xBF0)) = 3; /* DTR, RTS */
}

/*
 * nothing to do
 */
#define arch_decomp_wdog()

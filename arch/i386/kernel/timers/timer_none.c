#include <asm/timer.h>

static int init_none(void)
{
	return 0;
}

static void mark_offset_none(void)
{
	/* nothing needed */
}

static unsigned long get_offset_none(void)
{
	return 0;
}

static void delay_none(unsigned long loops)
{
	int d0;
	__asm__ __volatile__(
		"\tjmp 1f\n"
		".align 16\n"
		"1:\tjmp 2f\n"
		".align 16\n"
		"2:\tdecl %0\n\tjns 2b"
		:"=&a" (d0)
		:"0" (loops));
}

/* tsc timer_opts struct */
struct timer_opts timer_none = {
	.init =		init_none, 
	.mark_offset =	mark_offset_none, 
	.get_offset =	get_offset_none,
	.delay = delay_none,
};

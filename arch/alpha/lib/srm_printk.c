/*
 *	arch/alpha/lib/srm_printk.c
 */

#include <linux/kernel.h>
#include <asm/console.h>

long
srm_printk(const char *fmt, ...)
{
	static char buf[1024];
        va_list args;
        long i;

        va_start(args, fmt);
        i = vsprintf(buf,fmt,args);
        va_end(args);

	srm_puts(buf);	
        return i;
}

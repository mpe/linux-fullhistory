/*
 *	arch/alpha/lib/srm_puts.c
 */

#include <linux/string.h>
#include <asm/console.h>

void
srm_puts(const char *str)
{
	/* Expand \n to \r\n as we go.  */

	while (*str) {
		long len;
		const char *e = str;

		if (*str == '\n') {
			if (srm_dispatch(CCB_PUTS, 0, "\r", 1) < 0)
				return;
			++e;
		}

		e = strchr(e, '\n') ? : strchr(e, '\0');
		len = e - str;

		while (len > 0) {
			long written = srm_dispatch(CCB_PUTS, 0, str, len);
			if (written < 0)
				return;
			len -= written & 0xffffffff;
			str += written & 0xffffffff;
		}
	}
}

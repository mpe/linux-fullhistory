/*
 * linux/arch/alpha/mm/extable.c
 */

#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static inline unsigned
search_one_table(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 signed int value)
{
        while (first <= last) {
		const struct exception_table_entry *mid;
		long diff;

		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
                if (diff == 0)
                        return mid->fixup.unit;
                else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }
        return 0;
}

unsigned
search_exception_table(unsigned long addr)
{
	unsigned ret;
	signed int reladdr;

	/* Search the kernel's table first.  */
	{
		register unsigned long gp __asm__("$29");
		reladdr = addr - gp;
	}
	ret = search_one_table(__start___ex_table,
			       __stop___ex_table-1, reladdr);
	if (ret)
		return ret;

	/* FIXME -- search the module's tables here */

	return 0;
}

/*
 * linux/arch/alpha/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static inline unsigned
search_one_table(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 signed long value)
{
	/* Abort early if the search value is out of range.  */
	if (value != (signed int)value)
		return 0;

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

register unsigned long gp __asm__("$29");

unsigned
search_exception_table(unsigned long addr)
{
	unsigned ret;

#ifndef CONFIG_MODULE
	/* There is only the kernel to search.  */
	ret = search_one_table(__start___ex_table, __stop___ex_table - 1,
			       addr - gp);
	if (ret) return ret;
#else
	/* The kernel is the last "module" -- no need to treat it special. */
	struct module *mp;
	for (mp = module_list; mp ; mp = mp->next) {
		if (!mp->ex_table_start)
			continue;
		ret = search_one_table(mp->ex_table_start,
				       mp->ex_table_end - 1, addr - mp->gp);
		if (ret) return ret;
	}
#endif

	return 0;
}

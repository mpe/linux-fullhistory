/*
 * linux/arch/i386/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static inline unsigned long
search_one_table(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 unsigned long value)
{
	/* Some versions of the linker are buggy and do not align the
	   __start pointer along with the section, thus we may be low.  */
	if ((long)first & 3)
		(long)first = ((long)first | 3) + 1;

        while (first <= last) {
		const struct exception_table_entry *mid;
		long diff;

		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
                if (diff == 0)
                        return mid->fixup;
                else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }
        return 0;
}

unsigned long
search_exception_table(unsigned long addr)
{
	unsigned long ret;
#ifdef CONFIG_MODULES
	struct module *mp;
#endif

	/* Search the kernel's table first.  */
	ret = search_one_table(__start___ex_table,
			       __stop___ex_table-1, addr);
	if (ret)
		return ret;

#ifdef CONFIG_MODULES
	for (mp = module_list; mp != NULL; mp = mp->next) {
		if (mp->exceptinfo.start != NULL) {
			ret = search_one_table(mp->exceptinfo.start,
				mp->exceptinfo.stop-1, addr);
			if (ret)
				return ret;
		}
	}
#endif
	return 0;
}

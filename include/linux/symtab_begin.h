#include <linux/config.h>
#include <linux/linkage.h>

#ifdef CONFIG_MODVERSIONS
# undef _set_ver
# undef X

# ifndef __GENKSYMS__
#  ifdef MODULE
#    define _set_ver(sym,ver) \
	{ (void *) & sym ## _R ## ver, SYMBOL_NAME_STR(sym) "_R" #ver }
#  else /* !MODULE */
#    define _set_ver(sym,ver) \
	{ (void *) & sym, SYMBOL_NAME_STR(sym) "_R" #ver }
#  endif /* !MODULE */
#  define X(a) a
# endif /* !__GENKSYMS__ */
#else /* !CONFIG_MODVERSIONS */
# define X(sym) { (void *) & sym, SYMBOL_NAME_STR(sym)}
#endif /* !CONFIG_MODVERSIONS */

#define EMPTY {0,0}
	0, 0, 0, {

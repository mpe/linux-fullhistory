#include <linux/linkage.h>

#ifdef MODVERSIONS
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
#else /* !MODVERSIONS */
# define X(sym) { (void *) & sym, SYMBOL_NAME_STR(sym)}
#endif /* MODVERSIONS */
/*
 * Some symbols always need to be unversioned.  This includes
 * compiler generated calls to functions.
 */
#define XNOVERS(sym) { (void *) & sym, SYMBOL_NAME_STR(sym)}

#define EMPTY {0,0}
	0, 0, 0, {

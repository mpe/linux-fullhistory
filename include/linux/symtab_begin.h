#include <linux/linkage.h>

#ifdef MODVERSIONS
# undef _set_ver
# undef X
/*
 * These two macros _will_ get enough arguments from the X* macros
 * since "sym" expands to "symaddr, symstr" from the #define in *.ver
 */
# define _basic_version(symaddr,symstr)  symaddr, symstr
# define _alias_version(really,symaddr,symstr)  (void *) & really , symstr

# ifndef __GENKSYMS__
#  ifdef MODULE
#    define _set_ver(sym,ver) \
	(void *) & sym ## _R ## ver, SYMBOL_NAME_STR(sym) "_R" #ver
#  else /* !MODULE */
#    define _set_ver(sym,ver) \
	(void *) & sym, SYMBOL_NAME_STR(sym) "_R" #ver
#  endif /* !MODULE */
#  define X(sym) { _basic_version(sym) }
/*
 * For _really_ stacked modules:
 *
 * Use "Xalias(local_symbol, symbol_from_other_module)"
 * to make subsequent modules really use "local_symbol"
 * when they think that they are using "symbol_from_other_module"
 *
 * The "aliasing" module can still use "symbol_from_other_module",
 * but can now replace and/or modify the behaviour of that symbol.
 */
#  define Xalias(really,sym) { _alias_version(really,sym) }
# endif /* !__GENKSYMS__ */
#else /* !MODVERSIONS */
# define X(sym) { (void *) & sym, SYMBOL_NAME_STR(sym)}
# define Xalias(really,sym) { (void *) & really, SYMBOL_NAME_STR(sym)}
#endif /* MODVERSIONS */
/*
 * Some symbols always need to be unversioned.  This includes
 * compiler generated calls to functions.
 */
#define XNOVERS(sym) { (void *) & sym, SYMBOL_NAME_STR(sym)}

#define EMPTY {0,0}
	0, 0, 0, {

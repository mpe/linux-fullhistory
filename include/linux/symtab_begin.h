#ifdef CONFIG_MODVERSIONS /* CONFIG_MODVERSIONS */
#undef _set_ver
#undef X
#ifndef __GENKSYMS__
#ifdef MODULE
#define _set_ver(sym,ver) { (void *) & sym ## _R ## ver, "_" #sym "_R" #ver }
#else /* MODULE */
#define _set_ver(sym,ver) { (void *) & sym, "_" #sym "_R" #ver }
#endif /* MODULE */
#define X(a) a
#endif /* __GENKSYMS__ */
#else /* CONFIG_MODVERSIONS */
#define X(sym) { (void *) & sym, "_" #sym }
#endif /* CONFIG_MODVERSIONS */
#define EMPTY {0,0}
	0, 0, 0, {

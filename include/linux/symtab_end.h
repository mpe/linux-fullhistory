#ifdef CONFIG_MODVERSIONS /* CONFIG_MODVERSIONS */
#undef _set_ver
#if defined(MODULE) && !defined(__GENKSYMS__)
#define _set_ver(sym,vers) sym ## _R ## vers
#else
#define _set_ver(a,b) a
#endif
#endif /* CONFIG_MODVERSIONS */
#undef X
#undef EMPTY
	 /* mark end of table, last entry above ended with a comma! */
	 { (void *)0, (char *)0 }
	},
	/* no module refs, insmod will take care of that instead! */
	{ { (struct module *)0, (struct module_ref *)0 } }

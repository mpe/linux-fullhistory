/*
	ksyms.c.

	Process ksyms for ksymoops.

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.

	Fri Nov  6 10:38:42 EST 1998
	Version 0.6b
	Remove false warnings when comparing ksyms and lsmod.

	Tue Nov  3 02:31:01 EST 1998
	Version 0.6
	Read lsmod (/proc/modules).
	Move "Using_Version" copy to map.c.

	Wed Oct 28 13:47:23 EST 1998
	Version 0.4
	Split into separate sources.
 */

#include "ksymoops.h"
#include <malloc.h>
#include <string.h>

/* Scan one line from ksyms.  Split lines into the base symbols and the module
 * symbols.  Separate ss for base and each module.
 */
static void scan_ksyms_line(const char *line)
{
	int i;
	char **string = NULL;
	SYMBOL_SET *ssp;
	static char *prev_module = NULL;
	static regex_t     re_ksyms;
	static regmatch_t *re_ksyms_pmatch;
	static char const procname[] = "scan_ksyms_line";

	/* ksyms: address, symbol, optional module */
	re_compile(&re_ksyms,
		"^([0-9a-fA-F]{4,}) +([^ \t]+)([ \t]+\\[([^ ]+)\\])?$",
		REG_NEWLINE|REG_EXTENDED,
		&re_ksyms_pmatch);

	i = regexec(&re_ksyms, line,
		    re_ksyms.re_nsub+1, re_ksyms_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec %d\n", procname, i);
	if (i)
		return;

	/* string [1] - address, [2] - symbol, [3] - white space+module,
	 * [4] - module.
	 */
	re_strings(&re_ksyms, line, re_ksyms_pmatch, &string);
	if (string[4]) {
		if (!prev_module || strcmp(prev_module, string[4])) {
			/* start of a new module in ksyms */
			++ss_ksyms_modules;
			ss_ksyms_module = realloc(ss_ksyms_module,
				ss_ksyms_modules*sizeof(*ss_ksyms_module));
			if (!ss_ksyms_module)
				malloc_error("realloc ss_ksyms_module");
			ssp = ss_ksyms_module+ss_ksyms_modules-1;
			ss_init(ssp, string[4]);
			prev_module = strdup(string[4]);
			if (!prev_module)
				malloc_error("strdup prev_module");
		}
		ssp = ss_ksyms_module+ss_ksyms_modules-1;
	}
	else
		ssp = &ss_ksyms_base;
	add_symbol(ssp, string[1], ' ', 1, string[2]);
	re_strings_free(&re_ksyms, &string);
}

/* Read the symbols from ksyms.  */
void read_ksyms(const char *ksyms)
{
	FILE *f;
	char *line = NULL;
	int i, size;
	static char const procname[] = "read_ksyms";

	if (!ksyms)
		return;
	ss_init(&ss_ksyms_base, "ksyms_base");
	if (debug)
		fprintf(stderr, "DEBUG: %s %s\n", procname, ksyms);

	if (!regular_file(ksyms, procname))
		return;

	if (!(f = fopen_local(ksyms, "r", procname)))
		return;

	while (fgets_local(&line, &size, f, procname))
		scan_ksyms_line(line);

	fclose_local(f, procname);
	free(line);

	for (i = 0; i < ss_ksyms_modules; ++i) {
		ss_sort_na(ss_ksyms_module+i);
		extract_Version(ss_ksyms_module+i);
	}
	if (ss_ksyms_base.used) {
		ss_sort_na(&ss_ksyms_base);
		extract_Version(&ss_ksyms_base);
	}
	else {
		fprintf(stderr,
			"Warning, no kernel symbols in ksyms, is %s a valid "
			"ksyms file?\n",
			ksyms);
		++warnings;
	}

	if (debug > 1) {
		for (i = 0; i < ss_ksyms_modules; ++i) {
			fprintf(stderr,
				"DEBUG: %s %s used %d out of %d entries\n",
				procname,
				ss_ksyms_module[i].source,
				ss_ksyms_module[i].used,
				ss_ksyms_module[i].alloc);
		}
		fprintf(stderr,
			"DEBUG: %s %s used %d out of %d entries\n",
			procname, ss_ksyms_base.source, ss_ksyms_base.used,
			ss_ksyms_base.alloc);
	}
}

/* Map each ksyms module entry to the corresponding object entry.  Tricky,
 * see the comments in the docs about needing a unique symbol in each
 * module.
 */
static void map_ksym_to_module(SYMBOL_SET *ss)
{
	int i, j, matches;
	char *name = NULL;

	for (i = 0; i < ss->used; ++i) {
		matches = 0;
		for (j = 0; j < ss_objects; ++j) {
			name = (ss->symbol)[i].name;
			if (find_symbol_name(ss_object+j, name, NULL)) {
				++matches;
				ss->related = ss_object+j;
			}
		}
		if (matches == 1)
			break;		/* unique symbol over all objects */
		ss->related = NULL;	/* keep looking */
	}
	if (!(ss->related)) {
		fprintf(stderr,
			"Warning: cannot match loaded module %s to any "
			"module object.  Trace may not be reliable.\n",
			ss->source);
		++warnings;
	}
	else if (debug)
		fprintf(stderr,
			"DEBUG: ksyms %s matches to %s based on unique "
			"symbol %s\n",
			ss->source, ss->related->source, name);
}

/* Map all ksyms module entries to their corresponding objects */
void map_ksyms_to_modules(void)
{
	int i;
	SYMBOL_SET *ss, *ssc;

	for (i = 0; i < ss_ksyms_modules; ++i) {
		ss = ss_ksyms_module+i;
		map_ksym_to_module(ss);
		if (ss->related) {
			ssc = adjust_object_offsets(ss);
			compare_maps(ss, ssc, 1);
		}
	}
}

/* Read the modules from lsmod.  */
void read_lsmod(const char *lsmod)
{
	FILE *f;
	char *line = NULL;
	int i, size;
	char **string = NULL;
	static regex_t     re_lsmod;
	static regmatch_t *re_lsmod_pmatch;
	static char const procname[] = "read_lsmod";

	if (!lsmod)
		return;
	ss_init(&ss_lsmod, "lsmod");
	if (debug)
		fprintf(stderr, "DEBUG: %s %s\n", procname, lsmod);

	if (!regular_file(lsmod, procname))
		return;

	if (!(f = fopen_local(lsmod, "r", procname)))
		return;

	/* lsmod: module, size, use count, optional used by */
	re_compile(&re_lsmod,
		"^"
		"[ \t]*([^ \t]+)"				/* 1 module */
		"[ \t]*([^ \t]+)"				/* 2 size */
		"[ \t]*([^ \t]+)"				/* 3 count */
		"[ \t]*(.*)"					/* 4 used by */
		"$",
		REG_NEWLINE|REG_EXTENDED,
		&re_lsmod_pmatch);

	while (fgets_local(&line, &size, f, procname)) {
		i = regexec(&re_lsmod, line,
			    re_lsmod.re_nsub+1, re_lsmod_pmatch, 0);
		if (debug > 3)
			fprintf(stderr, "DEBUG: %s regexec %d\n", procname, i);
		if (i)
			continue;
		re_strings(&re_lsmod, line, re_lsmod_pmatch, &string);
		add_symbol(&ss_lsmod, string[2], ' ', 1, string[1]);
	}

	fclose_local(f, procname);
	free(line);
	re_strings_free(&re_lsmod, &string);
	if (ss_lsmod.used)
		ss_sort_na(&ss_lsmod);
	else {
		fprintf(stderr,
			"Warning, no symbols in lsmod, is %s a valid "
			"lsmod file?\n",
			lsmod);
		++warnings;
	}

	if (debug > 1)
		fprintf(stderr,
			"DEBUG: %s %s used %d out of %d entries\n",
			procname, ss_lsmod.source, ss_lsmod.used,
			ss_lsmod.alloc);
}

/* Compare modules from ksyms against module list in lsmod and vice versa.
 * There is one ss_ for each ksyms module and a single ss_lsmod to cross
 * check.
 */
void compare_ksyms_lsmod(void)
{
	int i, j;
	SYMBOL_SET *ss;
	SYMBOL *s;
	static char const procname[] = "compare_ksyms_lsmod";

	if (!(ss_lsmod.used && ss_ksyms_modules))
		return;

	s = ss_lsmod.symbol;
	for (i = 0; i < ss_lsmod.used; ++i, ++s) {
		for (j = 0; j < ss_ksyms_modules; ++j) {
			ss = ss_ksyms_module+j;
			if (strcmp(s->name, ss->source) == 0)
				break;
		}
		if (j >= ss_ksyms_modules) {
			fprintf(stderr,
				"Warning in %s, module %s is in lsmod but not "
				"in ksyms, probably no symbols exported\n",
				procname, s->name);
			++warnings;
		}
	}

	for (i = 0; i < ss_ksyms_modules; ++i) {
		ss = ss_ksyms_module+i;
		if (!find_symbol_name(&ss_lsmod, ss->source, NULL)) {
			fprintf(stderr,
				"Error in %s, module %s is in ksyms but not "
				"in lsmod\n",
				procname, ss->source);
			++errors;
		}
	}
}

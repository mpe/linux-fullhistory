/*
	map.c.

	Read System.map for ksymoops, create merged System.map.

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.

	Tue Nov  3 02:31:01 EST 1998
	Version 0.6
	Remove addresses 0-4095 from merged map after writing new map.
	Move "Using_Version" copy to map.c.

	Wed Oct 28 13:47:23 EST 1998
	Version 0.4
	Split into separate sources.
 */

#include "ksymoops.h"
#include <malloc.h>

/* Read the symbols from System.map */
void read_system_map(const char *system_map)
{
	FILE *f;
	char *line = NULL, **string = NULL;
	int i, size = 0;
	static char const procname[] = "read_system_map";

	if (!system_map)
		return;
	ss_init(&ss_system_map, "System.map");
	if (debug)
		fprintf(stderr, "DEBUG: %s %s\n", procname, system_map);

	if (!regular_file(system_map, procname))
		return;

	if (!(f = fopen_local(system_map, "r", procname)))
		return;

	while (fgets_local(&line, &size, f, procname)) {
		i = regexec(&re_nm, line, re_nm.re_nsub+1, re_nm_pmatch, 0);
		if (debug > 3)
			fprintf(stderr, "DEBUG: %s regexec %d\n", procname, i);
		if (i == 0) {
			re_strings(&re_nm, line, re_nm_pmatch, &string);
			add_symbol(&ss_system_map, string[1], *string[2],
				   1, string[3]);
		}
	}

	fclose_local(f, procname);
	re_strings_free(&re_nm, &string);
	free(line);
	if (ss_system_map.used) {
		ss_sort_na(&ss_system_map);
		extract_Version(&ss_system_map);
	}
	else {
		fprintf(stderr,
			"Warning, no kernel symbols in System.map, is %s a "
			"valid System.map file?\n",
			system_map);
		++warnings;
	}

	if (debug > 1)
		fprintf(stderr,
			"DEBUG: %s %s used %d out of %d entries\n",
			procname,
			ss_system_map.source,
			ss_system_map.used,
			ss_system_map.alloc);
}

/* Compare two maps, all symbols in the first should appear in the second. */
void compare_maps(const SYMBOL_SET *ss1, const SYMBOL_SET *ss2,
			 int precedence)
{
	int i, start = 0;
	SYMBOL *s1, *s2, **sdrop = precedence == 1 ? &s2 : &s1;
	const SYMBOL_SET **ssdrop = precedence == 1 ? &ss2 : &ss1;

	if (!(ss1->used && ss2->used))
		return;

	if (debug > 1)
		fprintf(stderr,
			"DEBUG: compare_maps %s vs %s, %s takes precedence\n",
			ss1->source, ss2->source,
			precedence == 1 ? ss1->source : ss2->source);

	for (i = 0; i < ss1->used; ++i) {
		s1 = ss1->symbol+i;
		if (!(s1->keep))
			continue;
		s2 = find_symbol_name(ss2, s1->name, &start);
		if (!s2) {
			/* Some types only appear in nm output, not in things
			 * like System.map.  Silently ignore them.
			 */
			if (s1->type == 'a' || s1->type == 't')
				continue;
			fprintf(stderr,
				"Warning: %s symbol %s not found in %s.  "
				"Ignoring %s entry\n",
				ss1->source, s1->name,
				ss2->source, (*ssdrop)->source);
			++warnings;
			if (*sdrop)
				(*sdrop)->keep = 0;
		}
		else if (s1->address != s2->address) {
			/* Type C symbols cannot be resolved from nm to ksyms,
			 * silently ignore them.
			 */
			if (s1->type == 'C' || s2->type == 'C')
				continue;
			fprintf(stderr,
				"Warning: mismatch on symbol %s %c, "
				"%s says %lx, %s says %lx.  "
				"Ignoring %s entry\n",
				s1->name, s1->type, ss1->source, s1->address,
				ss2->source, s2->address, (*ssdrop)->source);
			++warnings;
			if (*sdrop)
				(*sdrop)->keep = 0;
		}
		else
			++start;	/* step to next entry in ss2 */
	}
}

/* Append the second symbol set onto the first */
static void append_map(SYMBOL_SET *ss1, const SYMBOL_SET *ss2)
{
	int i;
	SYMBOL *s;

	if (!ss2 || !ss2->used)
		return;
	if (debug > 1)
		fprintf(stderr, "DEBUG: append_map %s to %s\n",
			ss2->source, ss1->source);

	for (i = 0; i < ss2->used; ++i) {
		s = ss2->symbol+i;
		if (s->keep)
			add_symbol_n(ss1, s->address, s->type, 1,
				s->name);
	}
}

/* Compare the various sources and build a merged system map */
void merge_maps(const char *save_system_map)
{
	int i;
	SYMBOL *s;
	FILE *f;
	static char const procname[] = "merge_maps";

	if (debug)
		fprintf(stderr, "DEBUG: %s\n", procname);

	/* Using_Versions only appears in ksyms, copy to other tables */
	if ((s = find_symbol_name(&ss_ksyms_base,
			"Using_Versions", 0))) {
		if (ss_system_map.used) {
			add_symbol_n(&ss_system_map, s->address,
				s->type, s->keep, s->name);
			ss_sort_na(&ss_system_map);
		}
		if (ss_vmlinux.used) {
			add_symbol_n(&ss_vmlinux, s->address, s->type,
				s->keep, s->name);
			ss_sort_na(&ss_vmlinux);
		}
	}

	compare_Version();	/* highlight any version problems first */
	compare_ksyms_lsmod();	/* highlight any missing modules next */
	compare_maps(&ss_ksyms_base, &ss_vmlinux, 2);
	compare_maps(&ss_system_map, &ss_vmlinux, 2);
	compare_maps(&ss_vmlinux, &ss_system_map, 1);
	compare_maps(&ss_ksyms_base, &ss_system_map, 2);

	if (ss_objects) {
		map_ksyms_to_modules();
	}

	ss_init(&ss_merged, "merged");
	append_map(&ss_merged, &ss_vmlinux);
	append_map(&ss_merged, &ss_ksyms_base);
	append_map(&ss_merged, &ss_system_map);
	for (i = 0; i < ss_ksyms_modules; ++i)
		append_map(&ss_merged, (ss_ksyms_module+i)->related);
	if (!ss_merged.used) {
		fprintf(stderr, "Warning, no symbols in merged map\n");
		++warnings;
	}

	/* drop duplicates, type a (registers) and gcc2_compiled. */
	ss_sort_atn(&ss_merged);
	s = ss_merged.symbol;
	for (i = 0; i < ss_merged.used-1; ++i) {
		if (s->type == 'a' ||
		    (s->type == 't' && !strcmp(s->name, "gcc2_compiled.")))
			s->keep = 0;
		else if (strcmp(s->name, (s+1)->name) == 0 &&
		    s->address == (s+1)->address) {
			if (s->type != ' ')
				(s+1)->keep = 0;
			else
				s->keep = 0;
		}
		++s;
	}
	ss_sort_atn(&ss_merged);	/* will remove dropped variables */

	if (save_system_map) {
		if (debug)
			fprintf(stderr, "DEBUG: writing merged map to %s\n",
				save_system_map);
		if (!(f = fopen_local(save_system_map, "w", procname)))
			return;
		s = ss_merged.symbol;
		for (i = 0; i < ss_merged.used; ++i) {
			if (s->keep)
				fprintf(f, "%s %c %s\n",
					format_address(s->address),
					s->type, s->name);
			++s;
		}
	}

	/* The merged map may contain symbols with an address of 0, e.g.
	 * Using_Versions.  These give incorrect results for low addresses in
	 * map_address, such addresses map to "Using_Versions+xxx".  Remove
	 * any addresses below (arbitrary) 4096 from the merged map.  AFAIK,
	 * Linux does not use the first page on any arch.
	 */
	for (i = 0; i < ss_merged.used; ++i) {
		if ((ss_merged.symbol+i)->address < 4096)
			(ss_merged.symbol+i)->keep = 0;
		else
			break;
	}
	if (i)
		ss_sort_atn(&ss_merged);	/* remove dropped variables */
}

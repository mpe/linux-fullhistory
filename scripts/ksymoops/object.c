/*
	object.c.

	object handling routines for ksymoops.  Read modules, vmlinux, etc. 

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.

	Wed Oct 28 13:47:23 EST 1998
	Version 0.4
	Split into separate sources.
 */

#include "ksymoops.h"
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>

/* Extract all symbols definitions from an object using nm */
static void read_nm_symbols(SYMBOL_SET *ss, const char *file)
{
	FILE *f;
	char *cmd, *line = NULL, **string = NULL;
	int i, size = 0;
	static char const procname[] = "read_nm_symbols";

	if (!regular_file(file, procname))
		return;

	cmd = malloc(strlen(path_nm)+strlen(file)+2);
	if (!cmd)
		malloc_error("nm command");
	strcpy(cmd, path_nm);
	strcat(cmd, " ");
	strcat(cmd, file);
	if (debug > 1)
		fprintf(stderr, "DEBUG: %s command '%s'\n", procname, cmd);
	if (!(f = popen_local(cmd, procname)))
		return;
	free(cmd);

	while (fgets_local(&line, &size, f, procname)) {
		i = regexec(&re_nm, line, re_nm.re_nsub+1, re_nm_pmatch, 0);
		if (debug > 3)
			fprintf(stderr, "DEBUG: %s regexec %d\n", procname, i);
		if (i == 0) {
			re_strings(&re_nm, line, re_nm_pmatch, &string);
			add_symbol(ss, string[1], *string[2], 1, string[3]);
		}
	}

	pclose_local(f, procname);
	re_strings_free(&re_nm, &string);
	free(line);
	if (debug > 1)
		fprintf(stderr,
			"DEBUG: %s %s used %d out of %d entries\n",
			procname, ss->source, ss->used, ss->alloc);
}

/* Read the symbols from vmlinux */
void read_vmlinux(const char *vmlinux)
{
	if (!vmlinux)
		return;
	ss_init(&ss_vmlinux, "vmlinux");
	read_nm_symbols(&ss_vmlinux, vmlinux);
	if (ss_vmlinux.used) {
		ss_sort_na(&ss_vmlinux);
		extract_Version(&ss_vmlinux);
	}
	else {
		fprintf(stderr,
			"Warning, no kernel symbols in vmlinux, is %s a valid "
			"vmlinux file?\n",
			vmlinux);
		++warnings;
	}
}


/* Read the symbols from one object (module) */
void read_object(const char *object, int i)
{
	ss_init(ss_object+i, object);
	read_nm_symbols(ss_object+i, object);
	if ((ss_object+i)->used) {
		ss_sort_na(ss_object+i);
		extract_Version(ss_object+i);
	}
	else {
		fprintf(stderr, "Warning, no symbols in %s\n", object);
		++warnings;
	}
}

/* Add a new entry to the list of objects */
static void add_ss_object(const char *file)
{
	++ss_objects;
	ss_object = realloc(ss_object, ss_objects*sizeof(*ss_object));
	if (!ss_object)
		malloc_error("realloc ss_object");
	ss_init(ss_object+ss_objects-1, file);
}

/* Run a directory and its subdirectories, looking for *.o files */
static void find_objects(const char *dir)
{
	FILE *f;
	char *cmd, *line = NULL;
	int size = 0, files = 0;
	static char const procname[] = "find_objects";
	static char const options[] = " -follow -name '*.o' -print";

	cmd = malloc(strlen(path_find)+1+strlen(dir)+strlen(options)+1);
	if (!cmd)
		malloc_error("find command");
	strcpy(cmd, path_find);
	strcat(cmd, " ");
	strcat(cmd, dir);
	strcat(cmd, options);
	if (debug > 1)
		fprintf(stderr, "DEBUG: %s command '%s'\n", procname, cmd);
	if (!(f = popen_local(cmd, procname)))
		return;
	free(cmd);

	while (fgets_local(&line, &size, f, procname)) {
		if (debug > 1)
			fprintf(stderr, "DEBUG: %s - %s\n", procname, line);
		add_ss_object(line);
		++files;
	}

	pclose_local(f, procname);
	if (!files) {
		fprintf(stderr,
			"Warning: no *.o files in %s.  "
			"Is %s a valid module directory?\n",
			dir, dir);
		++warnings;
	}
}

/* Take the user supplied list of objects which can include directories.
 * Expand directories into any *.o files.  The results are stored in
 * ss_object, leaving the user supplied options untouched.
 */
void expand_objects(char * const *object, int objects)
{
	struct stat statbuf;
	int i;
	const char *file;
	static char const procname[] = "expand_objects";

	for (i = 0; i < objects; ++i) {
		file = object[i];
		if (debug > 1)
			fprintf(stderr, "DEBUG: %s checking '%s' - ",
				procname, file);
		if (!stat(file, &statbuf) && S_ISDIR(statbuf.st_mode)) {
			if (debug > 1)
				fprintf(stderr, "directory, expanding\n");
			find_objects(file);
		}
		else {
			if (debug > 1)
				fprintf(stderr, "not directory\n");
			add_ss_object(file);
		}
	}
}

/* Map a symbol type to a section code. 0 - text, 1 - data, 2 - read only data,
 * 3 - C (cannot relocate), 4 - the rest.
 */
static int section(char type)
{
	switch (type) {
	case 'T':
	case 't':
		return 0;
	case 'D':
	case 'd':
		return 1;
	case 'R':
	case 'r':
		return 2;
	case 'C':
		return 3;
	default:
		return 4;
	}
}

/* Given ksyms module data which has a related object, create a copy of the
 * object data, adjusting the offsets to match where the module was loaded.
 */
SYMBOL_SET *adjust_object_offsets(SYMBOL_SET *ss)
{
	int i;
	elf_addr_t adjust[] = {0, 0, 0, 0, 0};
	SYMBOL *sk, *so;
	SYMBOL_SET *ssc;

	if (debug > 1)
		fprintf(stderr,
			"DEBUG: adjust_object_offsets %s\n", ss->source);

	ssc = ss_copy(ss->related);

	/* For common symbols, calculate the adjustment */
	for (i = 0; i < ss->used; ++i) {
		sk = ss->symbol+i;
		if ((so = find_symbol_name(ssc, sk->name, NULL)))
			adjust[section(so->type)] = sk->address - so->address;
	}
	for (i = 0; i < ssc->used; ++i) {
		so = ssc->symbol+i;
		/* Type C does not relocate well, silently ignore */
		if (so->type != 'C' && adjust[section(so->type)])
			so->address += adjust[section(so->type)];
		else
			so->keep = 0;  /* do not merge into final map */
	}

	ss->related = ssc;	/* map using adjusted copy */
	return(ssc);
}

/*
	misc.c.

	Miscellaneous routines for ksymoops.

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.

	Tue Nov  3 02:31:01 EST 1998
	Version 0.6
	Convert from a.out to bfd, using same format as ksymoops.

	Wed Oct 28 13:47:23 EST 1998
	Version 0.4
	Split into separate sources.
 */

#include "ksymoops.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void malloc_error(const char *msg)
{
	fprintf(stderr, "%s: fatal malloc error for %s\n", prefix, msg);
	exit(2);
}

/* Format an address with the correct number of leading zeroes */
const char *format_address(elf_addr_t address)
{
	/* Well oversized */
	static char format[10], text[200];
	if (!*format)
		snprintf(format, sizeof(format), "%%0%dlx",
			2*sizeof(address));
	snprintf(text, sizeof(text), format, address);
	return(text);
}

/* Find the full pathname of a program.  Code heavily based on
 * glibc-2.0.5/posix/execvp.c.
 */
char *find_fullpath(const char *program)
{
	char *fullpath = NULL;
	char *path, *p;
	size_t len;
	static const char procname[] = "find_fullpath";

	/* Don't search when it contains a slash.  */
	if (strchr(program, '/')) {
		if (!(fullpath = strdup(program)))
			malloc_error(procname);
		if (debug > 1)
			fprintf(stderr, "DEBUG: %s %s\n", procname, fullpath);
		return(fullpath);
	}

	path = getenv ("PATH");
	if (!path) {
		/* There is no `PATH' in the environment.  The default search
		   path is the current directory followed by the path `confstr'
		   returns for `_CS_PATH'.
		 */
		len = confstr(_CS_PATH, (char *) NULL, 0);
		if (!(path = malloc(1 + len)))
			malloc_error(procname);
		path[0] = ':';
		confstr(_CS_PATH, path+1, len);
	}

	len = strlen(program) + 1;
	if (!(fullpath = malloc(strlen(path) + len)))
		malloc_error(procname);
	p = path;
	do {
		path = p;
		p = strchr(path, ':');
		if (p == NULL)
			p = strchr(path, '\0');

		/* Two adjacent colons, or a colon at the beginning or the end
		 * of `PATH' means to search the current directory.
		 */
		if (p == path)
			memcpy(fullpath, program, len);
		else {
			/* Construct the pathname to try.  */
			memcpy(fullpath, path, p - path);
			fullpath[p - path] = '/';
			memcpy(&fullpath[(p - path) + 1], program, len);
		}

		/* If we have execute access, assume this is the program. */
		if (access(fullpath, X_OK) == 0) {
			if (debug > 1)
				fprintf(stderr, "DEBUG: %s %s\n",
					procname, fullpath);
			return(fullpath);
		}
	} while (*p++ != '\0');

	fprintf(stderr, "Error: %s %s could not find executable %s\n",
		prefix, procname, program);
	++errors;
	return(NULL);
}

/*
	io.c.

	Local I/O routines for ksymoops.

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.

	Tue Nov  3 02:31:01 EST 1998
	Version 0.6
	fwrite_local is redundant, replaced by bfd.

	Wed Oct 28 13:47:23 EST 1998
	Version 0.4
	Split into separate sources.

 */

#include "ksymoops.h"
#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>

int regular_file(const char *file, const char *msg)
{
	struct stat statbuf;
	if (stat(file, &statbuf)) {
		fprintf(stderr, "%s: %s stat %s failed",
			prefix, msg, file);
		perror(" ");
		++errors;
		return 0;
	}

	if (!S_ISREG(statbuf.st_mode)) {
		fprintf(stderr,
			"%s: %s %s is not a regular file, ignored\n",
			prefix, msg, file);
		++errors;
		return 0;
	}
	return 1;
}

FILE *fopen_local(const char *file, const char *mode, const char *msg)
{
	FILE *f;
	if (!(f = fopen(file, mode))) {
		fprintf(stderr, "%s: %s fopen '%s' failed",
			prefix, msg, file);
		perror(" ");
		++errors;
	}
	return f;
}

void fclose_local(FILE *f, const char *msg)
{
	int i;
	if ((i = fclose(f))) {
		fprintf(stderr, "%s: %s fclose failed %d", prefix, msg, i);
		perror(" ");
		++errors;
	}
}

/* Read a line, increasing the size of the line as necessary until \n is read */
#define INCREMENT 10	/* arbitrary */
char *fgets_local(char **line, int *size, FILE *f, const char *msg)
{
	char *l, *p, *r;
	int longline = 1;

	if (!*line) {
		*size = INCREMENT;
		*line = malloc(*size);
		if (!*line)
			malloc_error("fgets_local alloc line");
	}

	l = *line;
	while (longline) {
		r = fgets(l, *size-(l-*line), f);
		if (!r) {
			if (ferror(f)) {
				fprintf(stderr,
					"%s: %s fgets failed", prefix, msg);
				perror(" ");
				++errors;
			}
			if (l != *line)
				return(*line);
			else
				return(r);
		}
		if (!(p = strchr(*line, '\n'))) {
			*size += INCREMENT;
			*line = realloc(*line, *size);
			if (!*line)
				malloc_error("fgets_local realloc line");
			l = *line+*size-INCREMENT-1;
		}
		else {
			*p = '\0';
			longline = 0;
		}
	}

	if (debug > 3)
		fprintf(stderr, "DEBUG: %s line '%s'\n", msg, *line);
	return(*line);
}

FILE *popen_local(const char *cmd, const char *msg)
{
	FILE *f;
	if (!(f = popen(cmd, "r"))) {
		fprintf(stderr, "%s: %s popen '%s' failed",
			prefix, msg, cmd);
		perror(" ");
		++errors;
	}
	return f;
}

void pclose_local(FILE *f, const char *msg)
{
	int i;
	errno = 0;
	if ((i = pclose(f))) {
		fprintf(stderr, "%s: %s pclose failed 0x%x", prefix, msg, i);
		if (errno)
			perror(" ");
		else
			fprintf(stderr, "\n");
		++errors;
	}
}

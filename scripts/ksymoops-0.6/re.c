/*
	re.c.

	Regular expression processing for ksymoops.

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.

	Tue Nov  3 02:31:01 EST 1998
	Version 0.6
	PPC trace addresses are not bracketed, add new re.

	Wed Oct 28 13:47:23 EST 1998
	Version 0.4
	Split into separate sources.
 */

#include "ksymoops.h"
#include <malloc.h>
#include <string.h>

/* Compile a regular expression */
void re_compile(regex_t *preg, const char *regex, int cflags,
		regmatch_t **pmatch)
{
	int i, l;
	char *p;
	static char const procname[] = "re_compile";

	if (preg->re_nsub)
		return;		/* already compiled */

	if (debug)
		fprintf(stderr, "DEBUG: %s '%s'", procname, regex);
	if ((i = regcomp(preg, regex, cflags))) {
		l = regerror(i, preg, NULL, 0);
		++l;	/* doc is ambiguous, be safe */
		p = malloc(l);
		if (!p)
			malloc_error("regerror text");
		regerror(i, preg, p, l);
		fprintf(stderr,
			"%s: fatal %s error on '%s' - %s\n",
			prefix, procname, regex, p);
		exit(2);
	}
	if (debug)
		fprintf(stderr, " %d sub expression(s)\n", preg->re_nsub);
	/* [0] is entire match, [1] is first substring */
	*pmatch = malloc((preg->re_nsub+1)*sizeof(**pmatch));
	if (!*pmatch)
		malloc_error("pmatch");

}

/* Compile common regular expressions */
void re_compile_common(void)
{

	/* nm: address, type, symbol */
	re_compile(&re_nm,
		"^([0-9a-fA-F]{4,}) +([^ ]) +([^ ]+)$",
		REG_NEWLINE|REG_EXTENDED,
		&re_nm_pmatch);

	/* bracketed address preceded by optional white space */
	re_compile(&re_bracketed_address,
		"^[ \t]*" BRACKETED_ADDRESS,
		REG_NEWLINE|REG_EXTENDED,
		&re_bracketed_address_pmatch);

	/* unbracketed address preceded by optional white space */
	re_compile(&re_unbracketed_address,
		"^[ \t*]*" UNBRACKETED_ADDRESS,
		REG_NEWLINE|REG_EXTENDED,
		&re_unbracketed_address_pmatch);

}

/* Split text into the matching re substrings - Perl is so much easier :).
 * Each element of *string is set to a malloced copy of the substring or
 * NULL if the substring did not match (undef).  A zero length substring match
 * is represented by a zero length **string.
 */
void re_strings(regex_t *preg, const char *text, regmatch_t *pmatch,
		char ***string)
{
	int i;
	if (!*string) {
		*string = malloc((preg->re_nsub+1)*sizeof(**string));
		if (!*string)
			malloc_error("re_strings base");
		for (i = 0; i < preg->re_nsub+1; ++i)
			(*string)[i] = NULL;
	}
	for (i = 0; i < preg->re_nsub+1; ++i) {
		if (debug > 4)
			fprintf(stderr,
				"DEBUG: re_string %d offsets %d %d",
				i, pmatch[i].rm_so, pmatch[i].rm_eo);
		if (pmatch[i].rm_so == -1) {
			/* no match for this sub expression */
			free((*string)[i]);
			(*string)[i] = NULL;
			if (debug > 4)
				fprintf(stderr, " (undef)\n");
		}
		else {
			int l = pmatch[i].rm_eo - pmatch[i].rm_so + 1;
			char *p;
			p = malloc(l);
			if (!p)
				malloc_error("re_strings");
			strncpy(p, text+pmatch[i].rm_so, l-1);
			*(p+l-1) = '\0';
			(*string)[i] = p;
			if (debug > 4)
				fprintf(stderr, " '%s'\n", p);
		}
	}
}

/* Free the matching re substrings */
void re_strings_free(const regex_t *preg, char ***string)
{
	if (*string) {
		int i;
		for (i = 0; i < preg->re_nsub+1; ++i)
			free((*string)[i]);
		free(*string);
		*string = NULL;
	}
}

/* Check that there are enough strings for an re */
void re_string_check(int need, int available, const char *msg)
{
	if (need > available) {
		fprintf(stderr,
			"%s: fatal not enough re_strings in %s.  "
			"Need %d, available %d\n",
			prefix, msg, need, available);
		exit(2);
	}
}

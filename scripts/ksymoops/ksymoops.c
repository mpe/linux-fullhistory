/*
	ksymoops.c.

	Read a kernel Oops file and make the best stab at converting the code to
	instructions and mapping stack values to kernel symbols.

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.
*/

#define VERSION "0.6e"

/*

	Tue Jan  5 19:26:02 EST 1999
	Version 0.6e
	Added to kernel.

	Mon Jan  4 09:48:13 EST 1999
	Version 0.6d
	Add ARM support.

	Thu Nov 26 16:37:46 EST 1998
	Version 0.6c
	Typo in oops_code.
	Add -c option.
	Add -1 option.
	Report if options were specified or defaulted.

	Fri Nov  6 10:38:42 EST 1998
	Version 0.6b
	Remove false warnings when comparing ksyms and lsmod.

	Tue Nov  3 23:33:04 EST 1998
	Version 0.6a
	Performance inprovements.

	Tue Nov  3 02:31:01 EST 1998
	Version 0.6
	Read lsmod (/proc/modules).
	Ignore addresses 0-4095 when mapping address to symbol.
	Discard default objects if -o specified.
	Oops file must be regular.
	Add "invalid operand" to Oops_print.
	Move "Using_Version" copy to map.c.
	Add Makefile defaults for vmlinux, ksyms, objects, System.map, lsmod.
	Minor adjustment to re for ppc.
	Minor adjustment to re for objdump lines with <_EIP+xxx>.
	Convert from a.out to bfd, using same format as ksymoops.
	Added MIPS.
	PPC handling based on patches by "Ryan Nielsen" <ran@krazynet.com>

	Wed Oct 28 23:14:55 EST 1998
	Version 0.5
	No longer read vmlinux by default, it only duplicates System.map.

	Wed Oct 28 13:47:38 EST 1998
	Version 0.4
	Split into separate sources.

	Mon Oct 26 00:01:47 EST 1998
	Version 0.3c
	Add alpha (arm) processing.

	Mon Oct 26 00:01:47 EST 1998
	Version 0.3b
	Add sparc processing.
	Handle kernel symbol versions.

	Fri Oct 23 13:11:20 EST 1998
	Version 0.3
	Add -follow to find command for people who use symlinks to modules.
	Add Version_ checking.

	Thu Oct 22 22:28:30 EST 1998
	Version 0.2.
	Generalise text prefix handling.
	Handle messages on Code: line.
	Format addresses with leading zeroes.
	Minor bug fixes.

	Wed Oct 21 23:28:48 EST 1998
	Version 0.1.  Rewrite from scratch in C.

	CREDITS.
	Oops disassembly based on ksymoops.cc,
	  Copyright (C) 1995 Greg McGary <gkm@magilla.cichlid.com>
	m68k code based on ksymoops.cc changes by
	  Andreas Schwab <schwab@issan.informatik.uni-dortmund.de>
 */

#include "ksymoops.h"
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

char *prefix;
char *path_nm = "/usr/bin/nm";			/* env KSYMOOPS_NM */
char *path_find = "/usr/bin/find";		/* env KSYMOOPS_FIND */
char *path_objdump = "/usr/bin/objdump";	/* env KSYMOOPS_OBJDUMP */
int debug = 0;
int errors = 0;
int warnings = 0;

SYMBOL_SET  ss_vmlinux;
SYMBOL_SET  ss_ksyms_base;
SYMBOL_SET *ss_ksyms_module;
int         ss_ksyms_modules;
SYMBOL_SET  ss_lsmod;
SYMBOL_SET *ss_object;
int         ss_objects;
SYMBOL_SET  ss_system_map;

SYMBOL_SET  ss_merged;   /* merged map with info from all sources */
SYMBOL_SET  ss_Version;  /* Version_ numbers where available */

/* Regular expression stuff */

regex_t     re_nm;
regmatch_t *re_nm_pmatch;
regex_t     re_bracketed_address;
regmatch_t *re_bracketed_address_pmatch;
regex_t     re_unbracketed_address;
regmatch_t *re_unbracketed_address_pmatch;

static void usage(void)
{
	fprintf(stderr, "Version " VERSION "\n");
	fprintf(stderr, "usage: %s\n", prefix);
	fprintf(stderr,
		"\t\t[-v vmlinux]\tWhere to read vmlinux\n"
		"\t\t[-V]\t\tNo vmlinux is available\n"
		"\t\t[-o object_dir]\tDirectory containing modules\n"
		"\t\t[-O]\t\tNo modules is available\n"
		"\t\t[-k ksyms]\tWhere to read ksyms\n"
		"\t\t[-K]\t\tNo ksyms is available\n"
		"\t\t[-l lsmod]\tWhere to read lsmod\n"
		"\t\t[-L]\t\tNo lsmod is available\n"
		"\t\t[-m system.map]\tWhere to read System.map\n"
		"\t\t[-M]\t\tNo System.map is available\n"
		"\t\t[-s save.map]\tSave consolidated map\n"
		"\t\t[-d]\t\tIncrease debug level by 1\n"
		"\t\t[-h]\t\tPrint help text\n"
		"\t\t[-c code_bytes]\tHow many bytes in each unit of code\n"
		"\t\t[-1]\t\tOne shot toggle (exit after first Oops)\n"
		"\t\t<Oops.file\tOops report to decode\n"
		"\n"
		"\t\tAll flags can occur more than once.  With the exception "
			"of -o\n"
		"\t\tand -d which are cumulative, the last occurrence of each "
			"flag is\n"
		"\t\tused.  Note that \"-v my.vmlinux -V\" will be taken as "
			"\"No vmlinux\n"
		"\t\tavailable\" but \"-V -v my.vmlinux\" will read "
			"my.vmlinux.  You\n"
		"\t\twill be warned about such combinations.\n"
		"\n"
		"\t\tEach occurrence of -d increases the debug level.\n"
		"\n"
		"\t\tEach -o flag can refer to a directory or to a single "
			"object\n"
		"\t\tfile.  If a directory is specified then all *.o files in "
			"that\n"
		"\t\tdirectory and its subdirectories are assumed to be "
			"modules.\n"
		"\n"
		"\t\tIf any of the vmlinux, object_dir, ksyms or system.map "
		"options\n"
		"\t\tcontain the string *r (*m, *n, *s) then it is replaced "
		"at run\n"
		"\t\ttime by the current value of `uname -r` (-m, -n, -s).\n"
		"\n"
		"\t\tThe defaults can be changed in the Makefile, current "
		"defaults\n"
		"\t\tare\n\n"
		"\t\t\t"
#ifdef DEF_VMLINUX
		"-v " DEF_LINUX
#else
		"-V"
#endif
		"\n"
		"\t\t\t"
#ifdef DEF_OBJECTS
		"-o " DEF_OBJECTS
#else
		"-O"
#endif
		"\n"
		"\t\t\t"
#ifdef DEF_KSYMS
		"-k " DEF_KSYMS
#else
		"-K"
#endif
		"\n"
		"\t\t\t"
#ifdef DEF_LSMOD
		"-l " DEF_LSMOD
#else
		"-L"
#endif
		"\n"
		"\t\t\t"
#ifdef DEF_MAP
		"-m " DEF_MAP
#else
		"-M"
#endif
		"\n"
		"\t\t\t-c %d\n"	/* DEF_CODE_BYTES */
		"\t\t\tOops report is read from stdin\n"
		"\n",
	DEF_CODE_BYTES
	       );
}

/* Check if possibly conflicting options were specified */
static void multi_opt(int specl, int specu, char type, const char *using)
{
	if (specl && specu) {
		fprintf(stderr,
			"Warning - you specified both -%c and -%c.  Using '",
			type, toupper(type));
		++warnings;
		if (using) {
			fprintf(stderr, "-%c %s", type, using);
			if (type == 'o')
				fprintf(stderr, " ...");
			fprintf(stderr, "'\n");
		}
		else
			fprintf(stderr, "-%c'\n", toupper(type));
	}
	else if (specl > 1 && type != 'o') {
		fprintf(stderr,
			"Warning - you specified -%c more than once.  "
			"Using '-%c %s'\n",
			type, type, using);
		++warnings;
	}
	else if (specu > 1) {
		fprintf(stderr,
			"Warning - you specified -%c more than once.  "
			"Second and subsequent '-%c' ignored\n",
			toupper(type), toupper(type));
		++warnings;
	}
}

/* If a name contains *r (*m, *n, *s), replace with the current value of
 * `uname -r` (-m, -n, -s).  Actually uses uname system call rather than the
 * uname command but the result is the same.
 */
static void convert_uname(char **name)
{
	char *p, *newname, *oldname, *replacement;
	unsigned len;
	int free_oldname = 0;
	static char procname[] = "convert_uname";

	if (!*name)
		return;

	while ((p = strchr(*name, '*'))) {
		struct utsname buf;
		int i = uname(&buf);
		if (debug)
			fprintf(stderr, "DEBUG: %s %s in\n", procname, *name);
		if (i) {
			fprintf(stderr,
				"%s: uname failed, %s will not be processed\n",
				prefix, *name);
			perror(prefix);
			++errors;
			return;
		}
		switch (*(p+1)) {
		case 'r':
			replacement = buf.release;
			break;
		case 'm':
			replacement = buf.machine;
			break;
		case 'n':
			replacement = buf.nodename;
			break;
		case 's':
			replacement = buf.sysname;
			break;
		default:
			fprintf(stderr,
				"%s: invalid replacement character '*%c' "
				"in %s\n",
				prefix, *(p+1), *name);
			++errors;
			return;
		}
		len = strlen(*name)-2+strlen(replacement)+1;
		if (!(newname = malloc(len)))
			malloc_error(procname);
		strncpy(newname, *name, (p-*name));
		strcpy(newname+(p-*name), replacement);
		strcpy(newname+(p-*name)+strlen(replacement), p+2);
		p = newname+(p-*name)+strlen(replacement);	/* no rescan */
		oldname = *name;
		*name = newname;
		if (free_oldname)
			free(oldname);
		free_oldname = 1;
		if (debug)
			fprintf(stderr, "DEBUG: %s %s out\n", procname, *name);
	}
	return;
}

/* Report if the option was specified or defaulted */
static void spec_or_default(int spec, int *some_spec) {
	if (spec) {
		printf(" (specified)\n");
		if (some_spec)
			*some_spec = 1;
	}
	else
		printf(" (default)\n");
}

/* Parse the options.  Verbose but what's new with getopt? */
static void parse(int argc,
		  char **argv,
		  char **vmlinux,
		  char ***object,
		  int *objects,
		  char **ksyms,
		  char **lsmod,
		  char **system_map,
		  char **save_system_map,
		  char ***filename,
		  int *filecount,
		  int *spec_h,
		  int *code_bytes,
		  int *one_shot
		 )
{
	int spec_v = 0, spec_V = 0;
	int spec_o = 0, spec_O = 0;
	int spec_k = 0, spec_K = 0;
	int spec_l = 0, spec_L = 0;
	int spec_m = 0, spec_M = 0;
	int spec_s = 0;
	int spec_c = 0;

	int c, i, some_spec = 0;
	char *p;

	while ((c = getopt(argc, argv, "v:Vo:Ok:Kl:Lm:Ms:dhc:1")) != EOF) {
		if (debug && c != 'd')
			fprintf(stderr, "DEBUG: getopt '%c' '%s'\n", c, optarg);
		switch(c) {
		case 'v':
			*vmlinux = optarg;
			++spec_v;
			break;
		case 'V':
			*vmlinux = NULL;
			++spec_V;
			break;
		case 'o':
			if (!spec_o) {
				/* First -o, discard default value(s) */
				for (i = 0; i < *objects; ++i)
					free((*object)[i]);
				free(*object);
				*object = NULL;
				*objects = 0;
			}
			*object = realloc(*object,
				((*objects)+1)*sizeof(**object));
			if (!*object)
				malloc_error("object");
			if (!(p = strdup(optarg)))
				malloc_error("strdup -o");
			else {
				(*object)[(*objects)++] = p;
				++spec_o;
			}
			break;
		case 'O':
			++spec_O;
			for (i = 0; i < *objects; ++i)
				free((*object)[i]);
			free(*object);
			*object = NULL;
			*objects = 0;
			break;
		case 'k':
			*ksyms = optarg;
			++spec_k;
			break;
		case 'K':
			*ksyms = NULL;
			++spec_K;
			break;
		case 'l':
			*lsmod = optarg;
			++spec_l;
			break;
		case 'L':
			*lsmod = NULL;
			++spec_L;
			break;
		case 'm':
			*system_map = optarg;
			++spec_m;
			break;
		case 'M':
			*system_map = NULL;
			++spec_M;
			break;
		case 's':
			*save_system_map = optarg;
			++spec_s;
			break;
		case 'd':
			++debug;
			break;
		case 'h':
			usage();
			++*spec_h;
			break;
		case 'c':
			++spec_c;
			errno = 0;
			*code_bytes = strtoul(optarg, &p, 10);
			/* Oops_code_values assumes that code_bytes is a
			 * multiple of 2.
			 */
			if (!*optarg || *p || errno ||
				(*code_bytes != 1 &&
				 *code_bytes != 2 &&
				 *code_bytes != 4 &&
				 *code_bytes != 8)) {
				fprintf(stderr,
					"%s Invalid value for -c '%s'\n",
					prefix, optarg);
				++errors;
				if (errno)
					perror(" ");
				*code_bytes = DEF_CODE_BYTES;
			}
			break;
		case '1':
			*one_shot = !*one_shot;
			break;
		case '?':
			usage();
			exit(2);
		}
	}

	*filecount = argc - optind;
	*filename = argv + optind;

	/* Expand any requests for the current uname values */
	convert_uname(vmlinux);
	if (*objects) {
		for (i = 0; i < *objects; ++i)
			convert_uname(*object+i);
	}
	convert_uname(ksyms);
	convert_uname(lsmod);
	convert_uname(system_map);

	/* Check for multiple options specified */
	multi_opt(spec_v, spec_V, 'v', *vmlinux);
	multi_opt(spec_o, spec_O, 'o', *object ? **object : NULL);
	multi_opt(spec_k, spec_K, 'k', *ksyms);
	multi_opt(spec_l, spec_L, 'l', *lsmod);
	multi_opt(spec_m, spec_M, 'm', *system_map);

	printf("Options used:");
	if (*vmlinux)
		printf(" -v %s", *vmlinux);
	else
		printf(" -V");
	spec_or_default(spec_v || spec_V, &some_spec);
	
	printf("             ");
	if (*objects) {
		for (i = 0; i < *objects; ++i)
			printf(" -o %s", (*object)[i]);
	}
	else
		printf(" -O");
	spec_or_default(spec_o || spec_O, &some_spec);

	printf("             ");
	if (*ksyms)
		printf(" -k %s", *ksyms);
	else
		printf(" -K");
	spec_or_default(spec_k || spec_K, &some_spec);

	printf("             ");
	if (*lsmod)
		printf(" -l %s", *lsmod);
	else
		printf(" -L");
	spec_or_default(spec_l || spec_L, &some_spec);

	printf("             ");
	if (*system_map)
		printf(" -m %s", *system_map);
	else
		printf(" -M");
	spec_or_default(spec_m || spec_M, &some_spec);

	printf("             ");
	printf(" -c %d", *code_bytes);
	spec_or_default(spec_c, NULL);

	if (*one_shot) {
		printf("             ");
		printf(" -1");
	}

	printf("\n");

	if (!some_spec) {
		printf(
"You did not tell me where to find symbol information.  I will assume\n"
"that the log matches the kernel and modules that are running right now\n"
"and I'll use the default options above for symbol resolution.\n"
"If the current kernel and/or modules do not match the log, you can get\n"
"more accurate output by telling me the kernel version and where to find\n"
"map, modules, ksyms etc.  ksymoops -h explains the options.\n"
			"\n");
		++warnings;
	}
}

/* Read environment variables */
static void read_env(const char *external, char **internal)
{
	char *p;
	if ((p = getenv(external))) {
		*internal = p;
		if (debug)
			fprintf(stderr,
				"DEBUG: env override %s=%s\n",
				external, *internal);
	}
	else {
		if (debug)
			fprintf(stderr,
				"DEBUG: env default %s=%s\n",
				external, *internal);
	}
}


int main(int argc, char **argv)
{
	char *vmlinux = NULL;
	char **object = NULL;
	int objects = 0;
	char *ksyms = NULL;
	char *lsmod = NULL;
	char *system_map = NULL;
	char *save_system_map = NULL;
	char **filename;
	int filecount = 0;
	int spec_h = 0;		/* -h was specified */
	int code_bytes = DEF_CODE_BYTES;
	int one_shot = 0;
	int i, ret;

	prefix = *argv;
	setvbuf(stdout, NULL, _IONBF, 0);

#ifdef DEF_VMLINUX
	vmlinux = DEF_LINUX;
#endif
#ifdef DEF_OBJECTS
	{
		char *p;
		object = realloc(object, (objects+1)*sizeof(*object));
		if (!object)
			malloc_error("DEF_OBJECTS");
		if (!(p = strdup(DEF_OBJECTS)))
			malloc_error("DEF_OBJECTS");
		else
			object[objects++] = p;
	}
#endif
#ifdef DEF_KSYMS
	ksyms = DEF_KSYMS;
#endif
#ifdef DEF_LSMOD
	lsmod = DEF_LSMOD;
#endif
#ifdef DEF_MAP
	system_map = DEF_MAP;
#endif

	parse(argc,
	      argv,
	      &vmlinux,
	      &object,
	      &objects,
	      &ksyms,
	      &lsmod,
	      &system_map,
	      &save_system_map,
	      &filename,
	      &filecount,
	      &spec_h,
	      &code_bytes,
	      &one_shot
	     );

	if (spec_h && filecount == 0)
		return(0);	/* just the help text */

	if (errors)
		return(1);

	if (debug)
		fprintf(stderr, "DEBUG: level %d\n", debug);

	read_env("KSYMOOPS_NM", &path_nm);
	read_env("KSYMOOPS_FIND", &path_find);
	read_env("KSYMOOPS_OBJDUMP", &path_objdump);

	re_compile_common();
	ss_init_common();

	read_vmlinux(vmlinux);
	read_ksyms(ksyms);
	/* No point in reading modules unless ksyms shows modules loaded */
	if (ss_ksyms_modules) {
		expand_objects(object, objects);
		for (i = 0; i < ss_objects; ++i)
			read_object(ss_object[i].source, i);
	}
	else if (objects)
		printf("No modules in ksyms, skipping objects\n");
	/* No point in reading lsmod without ksyms */
	if (ss_ksyms_modules || ss_ksyms_base.used)
		read_lsmod(lsmod);
	else if (lsmod)
		printf("No ksyms, skipping lsmod\n");
	read_system_map(system_map);
	merge_maps(save_system_map);

	/* After all that work, it is finally time to read the Oops report */
	ret = Oops_read(filecount, filename, code_bytes, one_shot);

	if (warnings || errors) {
		printf("\n");
		if (warnings)
			printf("%d warning%s ",
			       warnings, warnings == 1 ? "" : "s");
		if (warnings && errors)
			printf("and ");
		if (errors)
			printf("%d error%s ", errors, errors == 1 ? "" : "s");
		printf("issued.  Results may not be reliable.\n");
		if (!ret)
			return(1);
	}

	return(ret);
}

/*
	oops.c.

	Oops processing for ksymoop.

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.

	Mon Jan  4 08:47:55 EST 1999
	Version 0.6d
	Add ARM support.

	Thu Nov 26 16:37:46 EST 1998
	Version 0.6c
	Typo in oops_code.
	Add -c option.

	Tue Nov  3 23:33:04 EST 1998
	Version 0.6a
	Performance inprovements.

	Tue Nov  3 02:31:01 EST 1998
	Version 0.6
	Oops file must be regular.
	Add "invalid operand" to Oops_print.
	Minor adjustment to re for ppc.
	Minor adjustment to re for objdump lines with <_EIP+xxx>.
	Convert from a.out to bfd, using same format as ksymoops.
	Added MIPS.
	PPC handling based on patches by "Ryan Nielsen" <ran@krazynet.com>

	Wed Oct 28 13:47:23 EST 1998
	Version 0.4
	Split into seperate sources.
 */

#include "ksymoops.h"
#include <bfd.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <memory.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Error detected by bfd */
static void Oops_bfd_perror(const char *msg)
{
	fprintf(stderr, "Error ");
	bfd_perror(msg);
	++errors;
}

/* Safest way to get correct output bfd format is to copy ksymoops' format. */
static int Oops_copy_bfd_format(bfd **ibfd, bfd **obfd, asection **isec,
				const char *file)
{
	char *me, **matches, **match;

	if (!(*obfd = bfd_openw(file, NULL))) {
		Oops_bfd_perror(file);
		return(0);
	}

	me = find_fullpath(prefix);
	if (!me)
		return(0);

	if (!(*ibfd = bfd_openr(me, NULL))) {
		Oops_bfd_perror(me);
		return(0);
	}
	free(me);	/* Who is Tommy? */

	if (!bfd_check_format_matches(*ibfd, bfd_object, &matches)) {
		Oops_bfd_perror(me);
		if (bfd_get_error() == bfd_error_file_ambiguously_recognized) {
			fprintf(stderr, "Matching formats:");
			match = matches;
			while (*match)
				fprintf(stderr, " %s", *match++);
			fprintf(stderr, "\n");
			free(matches);
		}
		return(0);
	}

	if (!(*isec = bfd_get_section_by_name(*ibfd, ".text"))) {
		Oops_bfd_perror("get_section");
		return(0);
	}

	bfd_set_format(*obfd, bfd_object);
	bfd_set_arch_mach(*obfd, bfd_get_arch(*ibfd), bfd_get_mach(*ibfd));

	if (!bfd_set_file_flags(*obfd, bfd_get_file_flags(*ibfd))) {
		Oops_bfd_perror("set_file_flags");
		return(0);
	}

	return(1);
}

/* Write the code values to a file using bfd. */
static int Oops_write_bfd_data(bfd *ibfd, bfd *obfd, asection *isec,
			       const char *code, int size)
{
	asection *osec;
	asymbol *osym;

	if (!bfd_set_start_address(obfd, 0)) {
		Oops_bfd_perror("set_start_address");
		return(0);
	}
	if (!(osec = bfd_make_section(obfd, ".text"))) {
		Oops_bfd_perror("make_section");
		return(0);
	}
	if (!bfd_set_section_flags(obfd, osec, 
		bfd_get_section_flags(ibfd, isec))) {
		Oops_bfd_perror("set_section_flags");
		return(0);
	}
	if (!bfd_set_section_alignment(obfd, osec, 
		bfd_get_section_alignment(ibfd, isec))) {
		Oops_bfd_perror("set_section_alignment");
		return(0);
	}
	osec->output_section = osec;
	if (!(osym = bfd_make_empty_symbol(obfd))) {
		Oops_bfd_perror("make_empty_symbol");
		return(0);
	}
	osym->name = "_EIP";
	osym->section = osec;
	osym->flags = BSF_GLOBAL;
	osym->value = 0;
	if (!bfd_set_symtab(obfd, &osym, 1)) {
		Oops_bfd_perror("set_symtab");
		return(0);
	}
	if (!bfd_set_section_size(obfd, osec, size)) {
		Oops_bfd_perror("set_section_size");
		return(0);
	}
	if (!bfd_set_section_vma(obfd, osec, 0)) {
		Oops_bfd_perror("set_section_vma");
		return(0);
	}
	if (!bfd_set_section_contents(obfd, osec, (PTR) code, 0, size)) {
		Oops_bfd_perror("set_section_contents");
		return(0);
	}
	if (!bfd_close(obfd)) {
		Oops_bfd_perror("close(obfd)");
		return(0);
	}
	if (!bfd_close(ibfd)) {
		Oops_bfd_perror("close(ibfd)");
		return(0);
	}
	return 1;
}

/* Write the Oops code to a temporary file with suitable header and trailer. */
static char *Oops_code_to_file(const char *code, int size)
{
	char *file;
	bfd *ibfd, *obfd;
	asection *isec;

	bfd_init();
	file = tmpnam(NULL);
	if (!Oops_copy_bfd_format(&ibfd, &obfd, &isec, file))
		return(NULL);
	if (!Oops_write_bfd_data(ibfd, obfd, isec, code, size))
		return(NULL);
	return(file);
}

/* Run objdump against the binary Oops code */
static FILE *Oops_objdump(const char *file)
{
	char *cmd;
	FILE *f;
	static char const options[] = "-dhf ";
	static char const procname[] = "Oops_objdump";

	cmd = malloc(strlen(path_objdump)+1+strlen(options)+strlen(file)+1);
	if (!cmd)
		malloc_error(procname);
	strcpy(cmd, path_objdump);
	strcat(cmd, " ");
	strcat(cmd, options);
	strcat(cmd, file);
	if (debug > 1)
		fprintf(stderr, "DEBUG: %s command '%s'\n", procname, cmd);
	f = popen_local(cmd, procname);
	free(cmd);
	return(f);
}

/* Process one code line from objdump, ignore everything else */
static void Oops_decode_one(SYMBOL_SET *ss, const char *line, elf_addr_t eip,
			    int adjust)
{
	int i;
	elf_addr_t address, eip_relative;
	char *line2, *map, **string = NULL;
	static regex_t     re_Oops_objdump;
	static regmatch_t *re_Oops_objdump_pmatch;
	static char const procname[] = "Oops_decode_one";

	/* objdump output.  Optional whitespace, hex digits, optional
	 * ' <_EIP+offset>', ':'.  The '+offset' after _EIP is also optional.
	 * Older binutils output 'xxxxxxxx <_EIP+offset>:', newer versions do
	 * '00000000 <_EIP>:' first followed by '      xx:' lines.
	 *
	 * Just to complicate things even more, objdump recognises jmp, call,
	 * etc., converts the code to something like this :-
	 * "   f: e8 32 34 00 00  call   3446 <_EIP+0x3446>"
	 * Recognise this and append the eip adjusted address, followed by the
	 * map_address text for that address.
	 *
	 * With any luck, objdump will take care of all such references which
	 * makes this routine architecture insensitive.  No need to test for
	 * i386 jmp, call or m68k swl etc.
	 */
	re_compile(&re_Oops_objdump,
			"^[ \t]*"
			"([0-9a-fA-F]+)"				/* 1 */
			"( <_EIP[^>]*>)?"				/* 2 */
			":"
			"("						/* 3 */
			".* +<_EIP\\+0?x?([0-9a-fA-F]+)>[ \t]*$"	/* 4 */
			")?"
			".*"
			,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_objdump_pmatch);

	i = regexec(&re_Oops_objdump, line, re_Oops_objdump.re_nsub+1,
		re_Oops_objdump_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec %d\n", procname, i);
	if (i != 0)
		return;

	re_strings(&re_Oops_objdump, line, re_Oops_objdump_pmatch, &string);
	errno = 0;
	address = strtoul(string[1], NULL, 16);
	if (errno) {
		fprintf(stderr,
			"%s Invalid hex value in objdump line, "
			"treated as zero - '%s'\n"
			"  objdump line '%s'\n",
			procname, string[1], line);
		perror(" ");
		++errors;
		address = 0;
	}
	address += eip + adjust;
	if (string[4]) {
		/* EIP relative data to be adjusted */
		errno = 0;
		eip_relative = strtoul(string[4], NULL, 16);
		if (errno) {
			fprintf(stderr,
				"%s Invalid hex value in objdump line, "
				"treated as zero - '%s'\n"
				"  objdump line '%s'\n",
				procname, string[4], line);
			perror(" ");
			++errors;
			eip_relative = 0;
		}
		eip_relative += eip + adjust;
		map = map_address(&ss_merged, eip_relative);
		/* new text is original line, eip_relative in hex, map text */
		i = strlen(line)+1+2*sizeof(eip_relative)+1+strlen(map)+1;
		line2 = malloc(i);
		if (!line2)
			malloc_error(procname);
		snprintf(line2, i, "%s %s %s",
			line, format_address(eip_relative), map);
		add_symbol_n(ss, address, 'C', 1, line2);
		free(line2);
	}
	else
		add_symbol_n(ss, address, 'C', 1, line);	/* as is */
	re_strings_free(&re_Oops_objdump, &string);
}

/* Maximum number of code bytes to process.  It needs to be a multiple of 2 for
 * code_byte (-c) swapping.  Sparc and alpha dump 36 bytes so use 64.
 */
#define CODE_SIZE 64

/******************************************************************************/
/*                     Start architecture sensitive code                      */
/******************************************************************************/

/* Extract the hex values from the Code: line and convert to binary */
static int Oops_code_values(const unsigned char* code_text, char *code,
			    int *adjust, char ***string, int string_max,
			    int code_bytes)
{
	int byte = 0, i, l;
	unsigned long c;
	char *value;
	const char *p;
	static regex_t     re_Oops_code_value;
	static regmatch_t *re_Oops_code_value_pmatch;
	static const char procname[] = "Oops_code_values";

	/* Given by re_Oops_code: code_text is a message (e.g. "general
	 * protection") or one or more hex fields separated by space or tab.
	 * Some architectures bracket the current instruction with '<'
	 * and '>', others use '(' and ')'.  The first character is
	 * nonblank.
	 */
	if (!isxdigit(*code_text)) {
		fprintf(stderr,
			"Warning, Code looks like message, not hex digits.  "
			"No disassembly attempted.\n");
		++warnings;
		return(0);
	}
	memset(code, '\0', CODE_SIZE);
	p = code_text;
	*adjust = 0;	/* EIP points to code byte 0 */

	/* Code values.  Hex values separated by white space.  On sparc, the
	 * current instruction is bracketed in '<' and '>'.
	 */
	re_compile(&re_Oops_code_value,
			"^"
			"([<(]?)"					/* 1 */
			"([0-9a-fA-F]+)"				/* 2 */
			"[>)]?"
			"[ \t]*"
			,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_code_value_pmatch);

	re_string_check(re_Oops_code_value.re_nsub+1, string_max, procname);
	while (regexec(&re_Oops_code_value, p, re_Oops_code_value.re_nsub+1,
			re_Oops_code_value_pmatch, 0) == 0) {
		re_strings(&re_Oops_code_value, p,
			re_Oops_code_value_pmatch, string);
		if (byte >= CODE_SIZE)
			break;
		errno = 0;
		value = (*string)[2];
		c = strtoul(value, NULL, 16);
		if (errno) {
			fprintf(stderr,
				"%s Invalid hex value in code_value line, "
				"treated as zero - '%s'\n"
				"  code_value line '%s'\n",
				procname, value, code_text);
			perror(" ");
			++errors;
			c = 0;
		}
		if ((*string)[1] && *((*string)[1]))
			*adjust = -byte;	/* this byte is EIP */
		/* i386 - 2 byte code, m68k - 4 byte, sparc - 8 byte.
		 * On some architectures Code: is a stream of bytes, on some it
		 * is a stream of shorts, on some it is a stream of ints.
		 * Consistent we're not!
		 */
		l = strlen(value);
		if (l%2) {
			fprintf(stderr,
				"%s invalid value 0x%s in Code line, not a "
				"multiple of 2 digits, value ignored\n",
				procname, value);
			++errors;
		}
		else while (l) {
			if (byte >= CODE_SIZE) {
				fprintf(stderr,
					"%s Warning: extra values in Code "
					"line, ignored - '%s'\n",
					procname, value);
				++warnings;
				break;
			}
			l -= 2;
			code[byte++] = (c >> l*4) & 0xff;
			value += 2;
		}
		p += re_Oops_code_value_pmatch[0].rm_eo;
	}

	if (*p) {
		fprintf(stderr,
			"Warning garbage '%s' at end of code line ignored "
			"by %s\n",
			p, procname);
		++warnings;
	}

	/* The code_bytes parameter says how many readable bytes form a single
	 * code unit in machine terms.  -c 1 says that the text is already in
	 * machine order, -c 2 (4, 8) says each chunk of 2 (4, 8) bytes must be
	 * swapped to get back to machine order.  Which end is up?
	 */
	if (code_bytes != 1) {
		if (byte % code_bytes) {
			fprintf(stderr,
				"Warning: the number of code bytes (%d) is not "
				"a multiple of -c (%d)\n"
				"Byte swapping may not give sensible results\n",
				byte, code_bytes);
			++warnings;
		}
		for (l = 0; l < byte; l+= code_bytes) {
			for (i = 0; i < code_bytes/2; ++i) {
				c = code[l+i];
				code[l+i] = code[l+code_bytes-i-1];
				code[l+code_bytes-i-1] = c;
			}
		}
	}

	return(1);
}

/* Look for the EIP: line, returns start of the relevant hex value */
static char *Oops_eip(const char *line, char ***string, int string_max)
{
	int i;
	static regex_t     re_Oops_eip_sparc;
	static regmatch_t *re_Oops_eip_sparc_pmatch;
	static regex_t     re_Oops_eip_ppc;
	static regmatch_t *re_Oops_eip_ppc_pmatch;
	static regex_t     re_Oops_eip_mips;
	static regmatch_t *re_Oops_eip_mips_pmatch;
	static regex_t     re_Oops_eip_other;
	static regmatch_t *re_Oops_eip_other_pmatch;
	static const char procname[] = "Oops_eip";

	/* Oops 'EIP:' line for sparc, actually PSR followed by PC */
	re_compile(&re_Oops_eip_sparc,
			"^PSR: [0-9a-fA-F]+ PC: " UNBRACKETED_ADDRESS,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_eip_sparc_pmatch);

	i = regexec(&re_Oops_eip_sparc, line, re_Oops_eip_sparc.re_nsub+1,
		re_Oops_eip_sparc_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec sparc %d\n", procname, i);
	if (i == 0) {
		re_string_check(re_Oops_eip_sparc.re_nsub+1, string_max,
			procname);
		re_strings(&re_Oops_eip_sparc, line, re_Oops_eip_sparc_pmatch,
			string);
		return((*string)[re_Oops_eip_sparc.re_nsub]);
	}

	/* Oops 'EIP:' line for PPC, all over the place */
	re_compile(&re_Oops_eip_ppc,
			"("
			  "(kernel pc )"
			  "|(trap at PC: )"
			  "|(bad area pc )"
			  "|(NIP: )"
			")"
			UNBRACKETED_ADDRESS,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_eip_ppc_pmatch);

	i = regexec(&re_Oops_eip_ppc, line, re_Oops_eip_ppc.re_nsub+1,
		re_Oops_eip_ppc_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec ppc %d\n", procname, i);
	if (i == 0) {
		re_string_check(re_Oops_eip_ppc.re_nsub+1, string_max,
			procname);
		re_strings(&re_Oops_eip_ppc, line, re_Oops_eip_ppc_pmatch,
			string);
		return((*string)[re_Oops_eip_ppc.re_nsub]);
	}

	/* Oops 'EIP:' line for MIPS, epc, optional white space, ':',
	 * optional white space, unbracketed address.
	 */
	re_compile(&re_Oops_eip_mips,
			"^(epc[ \t]*:+[ \t]*)"
			UNBRACKETED_ADDRESS,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_eip_mips_pmatch);

	i = regexec(&re_Oops_eip_mips, line, re_Oops_eip_mips.re_nsub+1,
		re_Oops_eip_mips_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec mips %d\n", procname, i);
	if (i == 0) {
		re_string_check(re_Oops_eip_mips.re_nsub+1, string_max,
			procname);
		re_strings(&re_Oops_eip_mips, line, re_Oops_eip_mips_pmatch,
			string);
		return((*string)[re_Oops_eip_mips.re_nsub]);
	}

	/* Oops 'EIP:' line for other architectures */
	re_compile(&re_Oops_eip_other,
			"^("
	/* i386 */	"(EIP:[ \t]+.*)"
	/* m68k */	"|(PC[ \t]*=[ \t]*)"
	/* ARM */	"|(pc *: *)"
			")"
			BRACKETED_ADDRESS
			,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_eip_other_pmatch);

	i = regexec(&re_Oops_eip_other, line, re_Oops_eip_other.re_nsub+1,
		re_Oops_eip_other_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec other %d\n", procname, i);
	if (i == 0) {
		re_string_check(re_Oops_eip_other.re_nsub+1, string_max,
			procname);
		re_strings(&re_Oops_eip_other, line, re_Oops_eip_other_pmatch,
			string);
		return((*string)[re_Oops_eip_other.re_nsub]);
	}
	return(NULL);
}

/* Set the eip from the EIP line */
static void Oops_set_eip(const char *value, elf_addr_t *eip, SYMBOL_SET *ss)
{
	static const char procname[] = "Oops_set_eip";
	errno = 0;
	*eip = strtoul(value, NULL, 16);
	if (errno) {
		fprintf(stderr,
			"%s Invalid hex value in EIP line, ignored - '%s'\n",
			procname, value);
		perror(" ");
		++errors;
		*eip = 0;
	}
	add_symbol_n(ss, *eip, 'E', 1, ">>EIP:");
}

/* Look for the MIPS ra line, returns start of the relevant hex value */
static char *Oops_ra(const char *line, char ***string, int string_max)
{
	int i;
	static regex_t     re_Oops_ra;
	static regmatch_t *re_Oops_ra_pmatch;
	static const char procname[] = "Oops_ra";

	/* Oops 'ra:' line for MIPS, ra, optional white space, one or
	 * more '=', optional white space, unbracketed address.
	 */
	re_compile(&re_Oops_ra,
			"(ra[ \t]*=+[ \t]*)"
			UNBRACKETED_ADDRESS,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_ra_pmatch);

	i = regexec(&re_Oops_ra, line, re_Oops_ra.re_nsub+1,
		re_Oops_ra_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec %d\n", procname, i);
	if (i == 0) {
		re_string_check(re_Oops_ra.re_nsub+1, string_max, procname);
		re_strings(&re_Oops_ra, line, re_Oops_ra_pmatch,
			string);
		return((*string)[re_Oops_ra.re_nsub]);
	}
	return(NULL);
}

/* Set the MIPS ra from the ra line */
static void Oops_set_ra(const char *value, SYMBOL_SET *ss)
{
	static const char procname[] = "Oops_set_ra";
	elf_addr_t ra;
	errno = 0;
	ra = strtoul(value, NULL, 16);
	if (errno) {
		fprintf(stderr,
			"%s Invalid hex value in ra line, ignored - '%s'\n",
			procname, value);
		perror(" ");
		++errors;
		ra = 0;
	}
	add_symbol_n(ss, ra, 'R', 1, ">>RA :");
}

/* Look for the Trace multilines :(.  Returns start of addresses. */
static const char *Oops_trace(const char *line, char ***string, int string_max)
{
	int i;
	const char *start = NULL;
	static int trace_line = 0;
	static regex_t     re_Oops_trace;
	static regmatch_t *re_Oops_trace_pmatch;
	static const char procname[] = "Oops_trace";

	/* ppc is different, not a bracketed address, just an address */
	/* ARM is different, two bracketed addresses on each line */

	/* Oops 'Trace' lines */
	re_compile(&re_Oops_trace,
			"^("					/*  1 */
			"(Call Trace: )"			/*  2 */
	/* alpha */	"|(Trace: )"				/*  3 */
	/* various */	"|(" BRACKETED_ADDRESS ")"	   	/* 4,5*/
	/* ppc */	"|(Call backtrace:)"			/*  6 */
	/* ppc */	"|(" UNBRACKETED_ADDRESS ")"		/* 7,8*/
	/* ARM */	"|(Function entered at (" BRACKETED_ADDRESS "))"	/* 9,10,11 */
			")",
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_trace_pmatch);

	i = regexec(&re_Oops_trace, line, re_Oops_trace.re_nsub+1,
		re_Oops_trace_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec %d\n", procname, i);
	if (i == 0) {
#undef MATCHED
#define MATCHED(n) (re_Oops_trace_pmatch[n].rm_so != -1)
		if (MATCHED(2) || MATCHED(3)) {
			trace_line = 1;
			start = line + re_Oops_trace_pmatch[0].rm_eo;
		}
		else if (MATCHED(6)) {
			trace_line = 2;		/* ppc */
			start = line + re_Oops_trace_pmatch[0].rm_eo;
		}
		else if (trace_line == 1 && MATCHED(5))
			start = line + re_Oops_trace_pmatch[5].rm_so;
		else if (trace_line == 2 && MATCHED(8))	/* ppc */
			start = line + re_Oops_trace_pmatch[8].rm_so;
		else if (MATCHED(10)){
			trace_line = 1;		/* ARM */
			start = line + re_Oops_trace_pmatch[10].rm_so;
		}
		else
			trace_line = 0;
	}
	else
		trace_line = 0;
	if (trace_line)
		return(start);
	return(NULL);
}

/* Process a trace call line, extract addresses */
static void Oops_trace_line(const char *line, const char *p, SYMBOL_SET *ss)
{
	char **string = NULL;
	regex_t *pregex;
	regmatch_t *pregmatch;
	static const char procname[] = "Oops_trace_line";

	/* ppc does not bracket its addresses */
	if (isxdigit(*p)) {
		pregex = &re_unbracketed_address;
		pregmatch = re_unbracketed_address_pmatch;
	}
	else {
		pregex = &re_bracketed_address;
		pregmatch = re_bracketed_address_pmatch;
	}

	/* Loop over [un]?bracketed addresses */
	while (1) {
		if (regexec(pregex, p, pregex->re_nsub+1, pregmatch, 0) == 0) {
			re_strings(pregex, p, pregmatch, &string);
			add_symbol(ss, string[1], 'T', 1, "Trace:");
			p += pregmatch[0].rm_eo;
		}
		else if (strncmp(p, "from ", 5) == 0)
			p += 5;		/* ARM does "address from address" */
		else
			break;
	}

	if (*p && !strcmp(p, "...")) {
		fprintf(stderr,
			"Warning garbage '%s' at end of trace line ignored "
			"by %s\n",
			p, procname);
		++warnings;
	}
	re_strings_free(pregex, &string);
}

/* Do pattern matching to decide if the line should be printed.  When reading a
 * syslog containing multiple Oops, you need the intermediate data (registers,
 * tss etc.) to go with the decoded text.  Sets text to the start of the useful
 * text, after any prefix.  Note that any leading white space is treated as part
 * of the prefix, later routines do not see any indentation.
 *
 * Note: If a line is not printed, it will not be scanned for any other text.
 */
static int Oops_print(const char *line, const char **text, char ***string,
		      int string_max)
{
	int i, print = 0;
	static int stack_line = 0, trace_line = 0;
	static regex_t     re_Oops_prefix;
	static regmatch_t *re_Oops_prefix_pmatch;
	static regex_t     re_Oops_print_s;
	static regmatch_t *re_Oops_print_s_pmatch;
	static regex_t     re_Oops_print_a;
	static regmatch_t *re_Oops_print_a_pmatch;
	static const char procname[] = "Oops_print";

	*text = line;

	/* Lines to be ignored.  For some reason the "amuse the user" print in
	 * some die_if_kernel routines causes regexec to run very slowly.
	 */

	if (strstr(*text, "\\|/ ____ \\|/")  ||
	    strstr(*text, "\"@'/ ,. \\`@\"") ||
	    strstr(*text, "/_| \\__/ |_\\")  ||
	    strstr(*text, "   \\__U_/"))
		return(1);	/* print but avoid regexec */

	/* Prefixes to be ignored */
	re_compile(&re_Oops_prefix,
			"^("			/* start of line */
			"([^ ]{3} [ 0-9][0-9] [0-9]{2}:[0-9]{2}:[0-9]{2} "
			  "[^ ]+ kernel: +)"	/* syslogd */
			"|(<[0-9]+>)"		/* kmsg */
			"|([ \t]+)"		/* leading white space */
			")+"			/* any prefixes, in any order */
			,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_prefix_pmatch);

	i = regexec(&re_Oops_prefix, *text, re_Oops_prefix.re_nsub+1,
		re_Oops_prefix_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec prefix %d\n", procname, i);
	if (i == 0)
		*text += re_Oops_prefix_pmatch[0].rm_eo;  /* step over prefix */


	/* Lots of possibilities.  Expand as required for all architectures.
	 *
	 * Trial and error shows that regex does not like a lot of sub patterns
	 * that start with "^".  So split the patterns into two groups, one set
	 * must appear at the start of the line, the other set can appear
	 * anywhere.
	 */

	/* These patterns must appear at the start of the line, after stripping
	 * the prefix above.
	 *
	 * The order below is required to handle multiline outupt.
	 * string 2 is defined if the text is 'Stack from '.
	 * string 3 is defined if the text is 'Stack: '.
	 * string 4 is defined if the text might be a stack continuation.
	 * string 5 is defined if the text is 'Call Trace: '.
	 * string 6 is defined if the text might be a trace continuation.
	 * string 7 is the address part of the BRACKETED_ADDRESS.
	 *
	 * string 8 is defined if the text contains a version number.  No Oops
	 * report contains this as of 2.1.125 but IMHO it should be added.  If
	 * anybody wants to print a VERSION_nnnn line in their Oops, this code
	 * is ready.
	 *
	 * string 9 is defined if the text is 'Trace: ' (alpha).
	 * string 10 is defined if the text is 'Call backtrace:' (ppc).
	 */
	re_compile(&re_Oops_print_s,
       /* arch type */					    /* Required order */
			"^("						/*  1 */
	/* i386 */	"(Stack: )"					/*  2 */
	/* m68k */	"|(Stack from )"				/*  3 */
	/* various */	"|([0-9a-fA-F]{4,})"				/*  4 */
	/* various */	"|(Call Trace: )"				/*  5 */
	/* various */	"|(" BRACKETED_ADDRESS ")"			/* 6,7*/
	/* various */	"|(Version_[0-9]+)"				/*  8 */
	/* alpha */	"|(Trace: )"					/*  9 */
	/* ppc */	"|(Call backtrace:)"				/* 10 */

			/* order does not matter from here on */
	
	/* various */	"|(Process .*stackpage=)"
	/* various */	"|(Call Trace:[ \t])"
	/* various */	"|(Code *:[ \t])"
	/* various */	"|(Kernel panic)"
	/* various */	"|(In swapper task)"

	/* i386 2.0 */	"|(Corrupted stack page)"
	/* i386 */	"|(invalid operand: )"
	/* i386 */	"|(Oops: )"
	/* i386 */	"|(Cpu: +[0-9])"
	/* i386 */	"|(current->tss)"
	/* i386 */	"|(\\*pde +=)"
	/* i386 */	"|(EIP: )"
	/* i386 */	"|(EFLAGS: )"
	/* i386 */	"|(eax: )"
	/* i386 */	"|(esi: )"
	/* i386 */	"|(ds: )"

	/* m68k */	"|(pc[:=])"
	/* m68k */	"|(68060 access)"
	/* m68k */	"|(Exception at )"
	/* m68k */	"|(d[04]: )"
	/* m68k */	"|(Frame format=)"
	/* m68k */	"|(wb [0-9] stat)"
	/* m68k */	"|(push data: )"
	/* m68k */	"|(baddr=)"
	/* any other m68K lines to print? */

	/* sparc */	"|(Bad unaligned kernel)"
	/* sparc */	"|(Forwarding unaligned exception)"
	/* sparc */	"|(: unhandled unaligned exception)"
	/* sparc */	"|(<sc)"
	/* sparc */	"|(pc *=)"
	/* sparc */	"|(r[0-9]+ *=)"
	/* sparc */	"|(gp *=)"
	/* any other sparc lines to print? */

	/* alpha */	"|(tsk->)"
	/* alpha */	"|(PSR: )"
	/* alpha */	"|([goli]0: )"
	/* alpha */	"|(Instruction DUMP: )"
	/* any other alpha lines to print? */

	/* ppc */	"|(MSR: )"
	/* ppc */	"|(TASK = )"
	/* ppc */	"|(last math )"
	/* ppc */	"|(GPR[0-9]+: )"
	/* any other ppc lines to print? */

	/* MIPS */	"|(\\$[0-9 ]+:)"
	/* MIPS */	"|(epc )"
	/* MIPS */	"|(Status:)"
	/* MIPS */	"|(Cause :)"
	/* any other MIPS lines to print? */

	/* ARM */	"|(Backtrace:)"
	/* ARM */	"|(Function entered at)"
	/* ARM */	"|(\\*pgd =)"
	/* ARM */	"|(Internal error)"
	/* ARM */	"|(pc :)"
	/* ARM */	"|(sp :)"
	/* ARM */	"|(r[0-9][0-9 ]:)"
	/* ARM */	"|(Flags:)"
	/* ARM */	"|(Control:)"
	/* any other ARM lines to print? */

			")",
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_print_s_pmatch);

	i = regexec(&re_Oops_print_s, *text, re_Oops_print_s.re_nsub+1,
		re_Oops_print_s_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec start %d\n", procname, i);
	print = 0;
	if (i == 0) {
#undef MATCHED
#define MATCHED(n) (re_Oops_print_s_pmatch[n].rm_so != -1)
		print = 1;
		/* Handle multiline messages, messy */
		if (!MATCHED(2) && !MATCHED(3) && !MATCHED(4))
			stack_line = 0;
		else if (MATCHED(2) || MATCHED(3))
			stack_line = 1;
		else if (stack_line && !MATCHED(4)) {
			print = 0;
			stack_line = 0;
		}
		if (!MATCHED(5) && !MATCHED(6) && !MATCHED(9) && !MATCHED(10))
			trace_line = 0;
		else if (MATCHED(5) || MATCHED(9) || MATCHED(10))
			trace_line = 1;
		else if (stack_line && !MATCHED(6)) {
			print = 0;
			trace_line = 0;
		}
		/* delay splitting into strings until we really them */
		if (MATCHED(8)) {
			re_string_check(re_Oops_print_s.re_nsub+1, string_max,
				procname);
			re_strings(&re_Oops_print_s, *text,
				re_Oops_print_s_pmatch,
				string);
			add_Version((*string)[8]+8, "Oops");
		}
	}

	/* These patterns can appear anywhere in the line, after stripping
	 * the prefix above.
	 */
	re_compile(&re_Oops_print_a,
       /* arch type */

	/* various */	"(Unable to handle kernel)"
	/* various */	"|(Aiee)"      /* anywhere in text is a bad sign (TM) */
	/* various */	"|(die_if_kernel)"	/* ditto */

	/* sparc */	"|(\\([0-9]\\): Oops )"
	/* sparc */	"|(: memory violation)"
	/* sparc */	"|(: Exception at)"
	/* sparc */	"|(: Arithmetic fault)"
	/* sparc */	"|(: Instruction fault)"
	/* sparc */	"|(: arithmetic trap)"
	/* sparc */	"|(: unaligned trap)"

	/* sparc      die_if_kernel has no fixed text, identify by (pid): text.
	 *            Somebody has been playful with the texts.
	 *
	 *            Alas adding this next pattern increases run time by 15% on
	 *            its own!  It would be considerably faster if sparc had
	 *            consistent error texts.
	 */
	/* sparc */	"|("
			   "\\([0-9]+\\): "
			   "("
			     "(Whee)"
			     "|(Oops)"
			     "|(Kernel)"
			     "|(Penguin)"
			     "|(Too many Penguin)"
			     "|(BOGUS)"
			   ")"
			 ")"

	/* ppc */	"|(kernel pc )"
	/* ppc */	"|(trap at PC: )"
	/* ppc */	"|(bad area pc )"
	/* ppc */	"|(NIP: )"

	/* MIPS */	"|( ra *=)"

			")",
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_print_a_pmatch);

	i = regexec(&re_Oops_print_a, *text, re_Oops_print_a.re_nsub+1,
		re_Oops_print_a_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec anywhere %d\n", procname, i);
	if (i == 0)
		print = 1;

	return(print);
}

/* Look for the Code: line.  Returns start of the code bytes. */
static const char *Oops_code(const char *line, char ***string, int string_max)
{
	int i;
	static regex_t     re_Oops_code;
	static regmatch_t *re_Oops_code_pmatch;
	static const char procname[] = "Oops_code";

	/* Oops 'Code: ' hopefully followed by at least one hex code.  sparc
	 * brackets the PC in '<' and '>'.  ARM brackets the PC in '(' and ')'.
	 */
	re_compile(&re_Oops_code,
			"^("						/*  1 */
	/* sparc */	  "(Instruction DUMP)"				/*  2 */
	/* various */	  "|(Code *)"					/*  3 */
			")"
			":[ \t]+"
			"("						/*  4 */
			  "(general protection.*)"
			  "|(<[0-9]+>)"
			  "|(([<(]?[0-9a-fA-F]+[>)]?[ \t]*)+)"
			")"
			"(.*)$"				/* trailing garbage */
			,
		REG_NEWLINE|REG_EXTENDED|REG_ICASE,
		&re_Oops_code_pmatch);

	i = regexec(&re_Oops_code, line, re_Oops_code.re_nsub+1,
		re_Oops_code_pmatch, 0);
	if (debug > 3)
		fprintf(stderr, "DEBUG: %s regexec %d\n", procname, i);
	if (i == 0) {
		re_string_check(re_Oops_code.re_nsub+1, string_max, procname);
		re_strings(&re_Oops_code, line, re_Oops_code_pmatch,
			string);
		if ((*string)[re_Oops_code.re_nsub] &&
		    *((*string)[re_Oops_code.re_nsub])) {
			fprintf(stderr,
				"Warning: trailing garbage ignored on Code: "
				"line\n"
				"  Text: '%s'\n"
				"  Garbage: '%s'\n",
				line, (*string)[re_Oops_code.re_nsub]);
			++warnings;
		}
		return((*string)[4]);
	}
	return(NULL);
}

/******************************************************************************/
/*                      End architecture sensitive code                       */
/******************************************************************************/

/* Decode the Oops Code: via objdump*/
static void Oops_decode(const unsigned char* code_text, elf_addr_t eip,
			SYMBOL_SET *ss, char ***string, int string_max,
			int code_bytes)
{
	FILE *f;
	char *file, *line = NULL, code[CODE_SIZE];
	int size = 0, adjust;
	static char const procname[] = "Oops_decode";

	if (debug)
		fprintf(stderr, "DEBUG: %s\n", procname);
	/* text to binary */
	if (!Oops_code_values(code_text, code, &adjust, string, string_max,
		code_bytes))
		return;
	/* binary to same format as ksymoops */
	if (!(file = Oops_code_to_file(code, CODE_SIZE)))
		return;
	/* objdump the pseudo object */
	if (!(f = Oops_objdump(file)))	
		return;
	while (fgets_local(&line, &size, f, procname)) {
		if (debug > 1)
			fprintf(stderr, "DEBUG: %s - %s\n", procname, line);
		Oops_decode_one(ss, line, eip, adjust);
	}
	pclose_local(f, procname);	/* opened in Oops_objdump */
	free(line);
	if (unlink(file)) {
		fprintf(stderr, "%s could not unlink %s", prefix, file);
		perror(" ");
	}
}

/* Reached the end of an Oops report, format the extracted data. */
static void Oops_format(const SYMBOL_SET *ss_format)
{
	int i;
	SYMBOL *s;
	static const char procname[] = "Oops_format";

	if (debug)
		fprintf(stderr, "DEBUG: %s\n", procname);

	compare_Version();	/* Oops might have a version one day */
	printf("\n");
	for (s = ss_format->symbol, i = 0; i < ss_format->used; ++i, ++s) {
		/* For type C data, print Code:, address, map, "name" (actually
		 * the text of an objdump line).  For other types print name,
		 * address, map.
		 */
		if (s->type == 'C')
			printf("Code:  %s %-30s %s\n",
				format_address(s->address),
				map_address(&ss_merged, s->address),
				s->name);
		else
			printf("%s %s %s\n",
				s->name,
				format_address(s->address),
				map_address(&ss_merged, s->address));
	}
	printf("\n");
}

/* Select next Oops input file */
static FILE *Oops_next_file(int *filecount, char * const **filename)
{
	static FILE *f = NULL;
	static const char procname[] = "Oops_next_file";
	static int first_file = 1;

	if (first_file) {
		f = stdin;
		first_file = 0;
	}
	while (*filecount) {
		if (f)
			fclose_local(f, procname);
		f = NULL;
		if (regular_file(**filename, procname))
			f = fopen_local(**filename, "r", procname);
		if (f) {
			if (debug)
				fprintf(stderr,
					"DEBUG: reading Oops report "
					"from %s\n", **filename);
		}
		++*filename;
		--*filecount;
		if (f)
			return(f);
	}
	return(f);
}

/* Read the Oops report */
#define MAX_STRINGS 300	/* Maximum strings in any Oops re */
int Oops_read(int filecount, char * const *filename, int code_bytes,
	      int one_shot)
{
	char *line = NULL, **string = NULL;
	const char *start, *text;
	int i, size = 0, lineno = 0, lastprint = 0;
	elf_addr_t eip = 0;
	FILE *f;
	SYMBOL_SET ss_format;
	static const char procname[] = "Oops_read";

	ss_init(&ss_format, "Oops log data");

	if (!filecount && isatty(0))
		printf("Reading Oops report from the terminal\n");

	string = malloc(MAX_STRINGS*sizeof(*string));
	if (!string)
		malloc_error(procname);
	memset(string, '\0', MAX_STRINGS*sizeof(*string));

	do {
		if (!(f = Oops_next_file(&filecount, &filename)))
			continue;
		while (fgets_local(&line, &size, f, procname)) {
			if (debug > 2)
				fprintf(stderr,
					"DEBUG: %s - %s\n", procname, line);
			++lineno;
			if (Oops_print(line, &text, &string, MAX_STRINGS)) {
				puts(line);
				lastprint = lineno;
				if ((start = Oops_eip(text,
					&string, MAX_STRINGS)))
					Oops_set_eip(start, &eip, &ss_format);
				if ((start = Oops_ra(text,
					&string, MAX_STRINGS)))
					Oops_set_ra(start, &ss_format);
				if ((start = Oops_trace(text,
					&string, MAX_STRINGS)))
					Oops_trace_line(text, start,
						&ss_format);
				if ((start = Oops_code(text,
					&string, MAX_STRINGS))) {
					Oops_decode(start, eip, &ss_format,
						&string, MAX_STRINGS,
						code_bytes);
					Oops_format(&ss_format);
					ss_free(&ss_format);
					if (one_shot)
						return(0);
				}
			}
			/* More than 5 (arbitrary) lines which were not printed
			 * and there is some saved data, assume we missed the
			 * Code: line.
			 */
			if (ss_format.used && lineno > lastprint+5) {
				fprintf(stderr,
					"Warning, Code line not seen, dumping "
					"what data is available\n");
				++warnings;
				Oops_format(&ss_format);
				ss_free(&ss_format);
				if (one_shot)
					return(0);
			}
		}
		if (ss_format.used) {
			fprintf(stderr,
				"Warning, Code line not seen, dumping "
				"what data is available\n");
			++warnings;
			Oops_format(&ss_format);
			ss_free(&ss_format);
			if (one_shot)
				return(0);
		}
	} while (filecount != 0);

	for (i = 0; i < sizeof(string); ++i) {
		free(string[i]);
		string[i] = NULL;
	}
	free(line);
	if (one_shot)
		return(3);	/* one shot mode, end of input, no data */
	return(0);
}
